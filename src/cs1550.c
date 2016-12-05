/*
    FUSE: Filesystem in Userspace
    Copyright (C) 2001-2007  Miklos Szeredi <miklos@szeredi.hu>

    This program can be distributed under the terms of the GNU GPL.
    See the file COPYING.

    If a block is not completely full, then pad it with ZERO
*/
#include "cs1550bitmap.c"
//#include "cs1550.h"

#define    FUSE_USE_VERSION 26

#include <fuse.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>

#define     BLOCK_SIZE      512     // disk's block size
#define     DISK            ".disk"    // keep reference to our .disk file
#define     MAX_FILENAME    8       // 8.3 filenames
#define     MAX_EXTENSION   3
#define     MAX_LENGTH      MAX_FILENAME * 2 + MAX_EXTENSION + 1  // length of dir + filename + extension + NULL

//extern bitmap map;

// How many files can there be in one directory?
#define     MAX_FILES_IN_DIR (BLOCK_SIZE - sizeof(int)) / ((MAX_FILENAME + 1) + (MAX_EXTENSION + 1) + sizeof(size_t) + sizeof(long))


// The attribute packed means to not align these things
struct cs1550_directory_entry
{
    int nFiles;     //  How many files are in this directory.
                    //  Needs to be less than MAX_FILES_IN_DIR

    struct cs1550_file_directory
    {
        char fname[MAX_FILENAME + 1];                   // filename (plus space for nul)
        char fext[MAX_EXTENSION + 1];                   // extension (plus space for nul)
        size_t fsize;                                   // file size
        long nStartBlock;                               // where the first block is on disk
    } __attribute__((packed)) files[MAX_FILES_IN_DIR];  // There is an array of these

    // This is some space to get this to be exactly the size of the disk block.
    // Don't use it for anything.
    char padding[BLOCK_SIZE - MAX_FILES_IN_DIR * sizeof(struct cs1550_file_directory) - sizeof(int)];
};

typedef struct cs1550_root_directory cs1550_root_directory;

#define MAX_DIRS_IN_ROOT (BLOCK_SIZE - sizeof(int)) / ((MAX_FILENAME + 1) + sizeof(long))

struct cs1550_root_directory
{
    int nDirectories;    // How many subdirectories are in the root
                        // Needs to be less than MAX_DIRS_IN_ROOT
    struct cs1550_directory
    {
        char dname[MAX_FILENAME + 1];                           // directory name (plus space for nul)
        long nStartBlock;                                       // where the directory block is on disk
    } __attribute__((packed)) directories[MAX_DIRS_IN_ROOT];    // There is an array of these

    // This is some space to get this to be exactly the size of the disk block.
    // Don't use it for anything.
    char padding[BLOCK_SIZE - MAX_DIRS_IN_ROOT * sizeof(struct cs1550_directory) - sizeof(int)];
};

typedef struct cs1550_directory_entry cs1550_directory_entry;

// How much data can one block hold?
#define    MAX_DATA_IN_BLOCK (BLOCK_SIZE - sizeof(long))

struct cs1550_disk_block
{
    // The next disk block, if needed. This is the next pointer in the linked 
    // allocation list
    long nNextBlock;

    // And all the rest of the space in the block can be used for actual data
    // storage.
    char data[MAX_DATA_IN_BLOCK];
};

typedef struct cs1550_disk_block cs1550_disk_block;

/*
 * Called whenever the system wants to know the file attributes, including
 * simply whether the file exists or not. 
 *
 * man -s 2 stat will show the fields of a stat structure
 */
static int cs1550_getattr(const char *path, struct stat *stbuf)
{
    printf("cs1550_getattr STARTING...\n");
    printf("cs1550_getattr path=%s\n", path);
    int status = -ENOENT;                   // default is error

    // hold the directory, filename, and extension (if any)
    char dir[MAX_LENGTH];
    char filename[MAX_LENGTH];
    char ext[MAX_LENGTH];

    memset(stbuf, 0, sizeof(struct stat));
    
    // is path the root dir?
    if (strcmp(path, "/") == 0) {           // path is the root dir
        printf("cs1550_getattr PATH IS ROOT\n");
        stbuf->st_mode = S_IFDIR | 0755;
        stbuf->st_nlink = 2;

        status = 0;                         // SUCCESS

    } else {
        // make sure path is within maximum length
        if (strlen(path) < MAX_LENGTH) {
            printf("cs1550_getattr PATH IS LESS THAN MAXLEN. GOOD!\n");
            /*
                Get the directory and file information

                On success, the function returns the number of variables filled. 
                In the case of an input failure before any data could be successfully read, 
                EOF is returned.
            */
            int scan_result = sscanf(path, "/%[^/]/%[^.].%s", dir, filename, ext);

            if ((scan_result == EOF) ||                 // EOF error
                (scan_result == 0) ||                   // nothing was filled (no dir, no filename, no ext)
                (strlen(dir) > MAX_FILENAME) ||         // make sure directory within max length
                (strlen(filename) > MAX_FILENAME) ||    // make sure filename within max length
                (strlen(ext) > MAX_EXTENSION))          // make sure extension within max length
            { 
                // ERROR
                printf("cs1550_getattr HIT ERROR CASES\n");

            } else {

                // open the disk file
                FILE *disk = fopen(DISK, "rb");         // open with respect to binary mode

                // make sure could open the disk file
                if (disk == NULL) {
                    // ERROR
                    printf("cs1550_getattr DISK IS NULL\n");

                } else {
                    printf("cs1550_getattr SO FAR SO GOOD, ABOUT TO fread\n");
                    cs1550_root_directory root;                                     // pointer to root of disk file
                    int res = fread(&root, sizeof(cs1550_root_directory), 1, disk);           // put first 512bytes into root struct
                    printf("cs1550_getattr CALLED fread TO GET ROOT: %d\n", res);

                    // search for the directory from list of valid directories 
                    int location;
                    printf("cs1550_getattr root.nDirectories=%d.\n", root.nDirectories);
                    for (location=0; location < root.nDirectories; location++) {    // loop through valid directories
                        printf("cs1550_getattr root.directories[location].dname=%s, dir=%s\n", root.directories[location].dname, dir);
                        if (strcmp(root.directories[location].dname, dir) == 0) {

                            // found the directory
                            printf("cs1550_getattr FOUND the directory!\n");

                            // if only given a directory, return the directory info
                            if (scan_result == 1) {
                                // only given directory
                                stbuf->st_mode = S_IFDIR | 0755;
                                stbuf->st_nlink = 2;
                                
                                status = 0;                         // SUCCESS 
                                
                            } else {
                                // path contained a filename

                                // check if file exists by getting directory and looking through it
                                // fseek returns 0 if upon success; otherwise -1
                                int fseekret = fseek(disk, root.directories[location].nStartBlock * BLOCK_SIZE, SEEK_SET);

                                if (fseekret == 0) {
                                    cs1550_directory_entry working_dir;
                                    fread(&working_dir, sizeof(cs1550_directory_entry), 1, disk);   // put first 512bytes into root struct

                                    if (scan_result == 2) { ext[0] = '\0';}                         // make extension NULL if blank

                                    // loop over files to see if file actually exists (fname fext)
                                    int file_location;
                                    for (file_location=0; file_location < working_dir.nFiles; file_location++) {
                                        if ((strcmp(working_dir.files[file_location].fname, filename) == 0) 
                                            && (strcmp(working_dir.files[file_location].fext, ext) == 0)) {

                                            // found the file
                                            stbuf->st_mode = S_IFREG | 0666; 
                                            stbuf->st_nlink = 1;                // file links
                                            stbuf->st_size = 0;                 // file size - make sure you replace with real size!
                                            
                                            status = 0;                         // SUCCESS
                                            break;
                                        }
                                    }
                                }
                            }
                            break;
                        }
                    }
                    fclose(disk);   // close the disk
                }
            }
        }
    }

    // return 0 if success; otherwise -ENOENT
    return status;
}

/* 
 * Called whenever the contents of a directory are desired. Could be from an 'ls'
 * or could even be when a user hits TAB to do autocompletion
 */
static int cs1550_readdir(const char *path, void *buf, fuse_fill_dir_t filler,
    off_t offset, struct fuse_file_info *fi)
{
    // Since we're building with -Wall (all warnings reported) we need
    // to "use" every parameter, so let's just cast them to void to
    // satisfy the compiler
    (void) offset;
    (void) fi;

    // This line assumes we have no subdirectories, need to change
    if (strcmp(path, "/") != 0)
       return -ENOENT;

    // the filler function allows us to add entries to the listing
    // read the fuse.h file for a description (in the ../include dir)
    filler(buf, ".", NULL, 0);
    filler(buf, "..", NULL, 0);

    /*
    // add the user stuff (subdirs or files)
    // the +1 skips the leading '/' on the filenames
    filler(buf, newpath + 1, NULL, 0);
    */
   return 0;
}

/* 
    Creates a directory. We can ignore mode since we're not dealing with
    permissions, as long as getattr returns appropriate ones for us.
 */
static int cs1550_mkdir(const char *path, mode_t mode)
{
    (void) mode;

    int status = 0, free_block;
    char dir_name[MAX_LENGTH];
    char file[MAX_LENGTH];
    FILE *disk;

    // if the path contains a slash within the string then
    //      it's not within the root directory
    int scan_result = sscanf(path, "/%[^/]/%[^.]", dir_name, file);

    printf("cs1550_mkdir scan_result = %d\n", scan_result);

    if (scan_result != 1) {
        status = -EPERM;                // ERROR: can ONLY create dir within '/' root
        printf("!! ERROR !! CAN ONLY CREATE DIR WITHIN '/' ROOT\n");

    } else if (strlen(dir_name) > MAX_FILENAME) {
        status = -ENAMETOOLONG;         // ERROR: directory name too long
        printf("!! ERROR !! DIRECTORY NAME TOO LONG\n");

    } else if ((free_block = find_free_block()) == -1) {
        status = -ENOSPC;               // ERROR: no space left on disk
        printf("!! ERROR !! NO SPACE LEFT ON DISK\n");

    } else {
        /* TRY TO CREATE THE DIRECTORY */

        // open the disk file
        disk = fopen(DISK, "r+b");      // open for reading + writing (binary)
        printf("cs1550_mkdir OPENING DISK FOR READ+WRITE BINARY\n");

        if (disk == NULL) {
            status = -ENOENT;           // ERROR: file not opened successfully
            printf("cs1550_mkdir DISK COULD NOT BE OPENED\n");

        } else {
            // get the root within the disk file
            cs1550_root_directory root;                             // pointer to root of disk file
            fread(&root, sizeof(cs1550_root_directory), 1, disk);   // root in disk exists within first 512bytes
            printf("cs1550_mkdir READ IN cs1550_root_directory ROOT FROM DISK\n");

            // make sure the root can hold another directory listing
            if (root.nDirectories >= MAX_DIRS_IN_ROOT) {
                status = -ENOMEM;                                   // ERROR: not enough space
                printf("!! ERROR !! NOT ENOUGH SPACE\n");

            } else {
                // create directory inside the free block
                cs1550_directory_entry *new_dir;                    // create a new directory struct to put in free block
                new_dir = (struct cs1550_directory_entry*)calloc(1, sizeof(struct cs1550_directory_entry));
                new_dir->nFiles = 0;                                // no files exist at first
                printf("cs1550_mkdir CREATED cs1550_directory_entry structure\n");

                fseek(disk, free_block * BLOCK_SIZE, SEEK_SET);     // goto free block
                fwrite(new_dir, sizeof(cs1550_directory_entry), 1, disk);   // write new dir entry to disk
                printf("cs1550_mkdir SEEKED TO FREE BLOCK %d and wrote dir_entry struct\n", free_block);

                // create root dir struct
                struct cs1550_directory *new_dir_entry;            // create a new directory stub
                new_dir_entry = (struct cs1550_directory*)calloc(1, sizeof(struct cs1550_directory));
                printf("cs1550_mkdir CREATED cs1550_directory structure\n");

                strcpy(new_dir_entry->dname, dir_name);            // name of the actual directory
                printf("cs1550_mkdir COPIED dir_name '%s' to cs1550_directory struct dname '%s'\n", dir_name, new_dir_entry->dname);

                new_dir_entry->nStartBlock = free_block;           // make the start block the beginning of the free block found
                printf("cs1550_mkdir COPIED free_block '%d' to new_dir_entry.nStartBlock '%lu'\n", free_block, new_dir_entry->nStartBlock);

                printf("cs1550_mkdir UPDATING root nDirectories by adding new_dir_entry\n");
                root.directories[root.nDirectories] = *new_dir_entry;   // add directory to list of valid directories
                root.nDirectories++;
                printf("cs1550_mkdir nDirectories=%d", root.nDirectories);

                // write out the root to disk
                printf("cs1550_mkdir WRITING OUT THE NEW ROOT TO DISK\n");
                fseek(disk, 0, SEEK_SET);     // goto free block
                fwrite(&root, sizeof(struct cs1550_root_directory), 1, disk);   // write root to disk

                printf("cs1550_mkdir OUTPUTTING new_dir_entry from within root: dname='%s' nstartblock='%lu'\n", root.directories[root.nDirectories-1].dname, root.directories[root.nDirectories-1].nStartBlock);
            }
        }
    }
    
    printf("cs1550_mkdir status=%d\n", status);
    if (status == 0) {
        fclose(disk);           // close the disk
        printf("cs1550_mkdir CLOSED DISK\n");

        //printf("[%d][%d][%d]\n", map[0], map[1], map[2]);
        //printf("[%s]\n", byte_to_binary(map[1]);
        write_bitmap();      // update the bitmap on disk
        printf("cs1550_mkdir UPDATED BITMAP\n");
    }

    return status;
}

/* 
 * Removes a directory.
 */
static int cs1550_rmdir(const char *path)
{
    (void) path;
    return 0;
}

/* 
 * Does the actual creation of a file. Mode and dev can be ignored.
 *
 */
static int cs1550_mknod(const char *path, mode_t mode, dev_t dev)
{
    (void) mode;
    (void) dev;
    return 0;
}

/*
 * Deletes a file
 */
static int cs1550_unlink(const char *path)
{
    (void) path;

    return 0;
}

/* 
 * Read size bytes from file into buf starting from offset
 *
 */
static int cs1550_read(const char *path, char *buf, size_t size, off_t offset,
   struct fuse_file_info *fi)
{
    (void) buf;
    (void) offset;
    (void) fi;
    (void) path;

    // check to make sure path exists
    // check that size is > 0
    // check that offset is <= to the file size
    // read in data
    // set size and return, or error

    size = 0;

    return size;
}

/* 
 * Write size bytes from buf into file starting from offset
 *
 */
static int cs1550_write(const char *path, const char *buf, size_t size, 
   off_t offset, struct fuse_file_info *fi)
{
    (void) buf;
    (void) offset;
    (void) fi;
    (void) path;

    // check to make sure path exists
    // check that size is > 0
    // check that offset is <= to the file size
    // write data
    // set size (should be same as input) and return, or error

    return size;
}

/******************************************************************************
 *
 *  DO NOT MODIFY ANYTHING BELOW THIS LINE
 *
 *****************************************************************************/

/*
 * truncate is called when a new file is created (with a 0 size) or when an
 * existing file is made shorter. We're not handling deleting files or 
 * truncating existing ones, so all we need to do here is to initialize
 * the appropriate directory entry.
 *
 */
static int cs1550_truncate(const char *path, off_t size)
{
    (void) path;
    (void) size;

    return 0;
}


/* 
 * Called when we open a file
 *
 */
static int cs1550_open(const char *path, struct fuse_file_info *fi)
{
    (void) path;
    (void) fi;
    /*
        // if we can't find the desired file, return an error
        return -ENOENT;
    */

    // It's not really necessary for this project to anything in open

    /* We're not going to worry about permissions for this project, but 
       if we were and we don't have them to the file we should return an error

        return -EACCES;
    */

    return 0; // success!
}

/*
 * Called when close is called on a file descriptor, but because it might
 * have been dup'ed, this isn't a guarantee we won't ever need the file 
 * again. For us, return success simply to avoid the unimplemented error
 * in the debug log.
 */
static int cs1550_flush (const char *path , struct fuse_file_info *fi)
{
    (void) path;
    (void) fi;

    return 0; // success!
}


// register our new functions as the implementations of the syscalls
static struct fuse_operations hello_oper = {
    .getattr    = cs1550_getattr,
    .readdir    = cs1550_readdir,
    .mkdir      = cs1550_mkdir,
    .rmdir      = cs1550_rmdir,
    .read       = cs1550_read,
    .write      = cs1550_write,
    .mknod      = cs1550_mknod,
    .unlink     = cs1550_unlink,
    .truncate   = cs1550_truncate,
    .flush      = cs1550_flush,
    .open       = cs1550_open,
};

// Don't change this.
int main(int argc, char *argv[])
{
    return fuse_main(argc, argv, &hello_oper, NULL);
}
