/*
    File System Implementation

    Joe Meszar (jwm54@pitt.edu)
    CS1550 Project 4 (FALL 2016)
*/
/*
    FUSE: Filesystem in Userspace
    Copyright (C) 2001-2007  Miklos Szeredi <miklos@szeredi.hu>

    This program can be distributed under the terms of the GNU GPL.
    See the file COPYING.

    If a block is not completely full, then pad it with ZERO
*/
#include    "cs1550bitmap.c"

#define     FUSE_USE_VERSION 26

#include    <fuse.h>
#include    <stdio.h>
#include    <string.h>
#include    <errno.h>
#include    <fcntl.h>

#define     BLOCK_SIZE      512         // disk's block size
#define     DISK            ".disk"     // keep reference to our .disk file
#define     MAX_FILENAME    8           // 8.3 filenames
#define     MAX_EXTENSION   3           // 8.3 filenames
#define     MAX_LENGTH      MAX_FILENAME * 2 + MAX_EXTENSION + 1  // length of dir + filename + extension + NULL

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



#define MAX_DIRS_IN_ROOT (BLOCK_SIZE - sizeof(int)) / ((MAX_FILENAME + 1) + sizeof(long))

struct cs1550_root_directory
{
    int nDirectories;   // How many subdirectories are in the root (needs to be less than MAX_DIRS_IN_ROOT)

    struct cs1550_directory
    {
        char dname[MAX_FILENAME + 1];                           // directory name (plus space for nul)
        long nStartBlock;                                       // where the directory block is on disk
    } __attribute__((packed)) directories[MAX_DIRS_IN_ROOT];    // There is an array of these

    // This is some space to get this to be exactly the size of the disk block.
    // Don't use it for anything.
    char padding[BLOCK_SIZE - MAX_DIRS_IN_ROOT * sizeof(struct cs1550_directory) - sizeof(int)];
};



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

typedef struct cs1550_root_directory cs1550_root_directory;
typedef struct cs1550_directory_entry cs1550_directory_entry;
typedef struct cs1550_file_directory cs1550_file_directory;
typedef struct cs1550_disk_block cs1550_disk_block;

/*
    Searches the root structure for the given directory name, and
    returns the starting block in the file for the directory.

    RETURNS: The start block of the given directory; otherwise '-1'.
*/
static long find_directory(char *dir_name) {
    long index = -1;                        // assume directory does not exist

    printf("[find_directory] dir_name=%s\n", dir_name);

    // open the disk file
    FILE *disk = fopen(DISK, "rb");         // open with respect to binary mode

    // make sure could open the disk file
    if (disk == NULL) {
        // ERROR

    } else {
        printf("[find_directory] \n");
        cs1550_root_directory root;                                 // pointer to root of disk file
        fread(&root, sizeof(cs1550_root_directory), 1, disk);       // root struct is in first block of disk

        // search for the directory within the list of valid directories
        int i;
        printf("[find_directory] root.nDirectories=%d.\n", root.nDirectories);
        for (i=0; i < root.nDirectories; i++) {                     // loop through valid directories
            if (strcmp(root.directories[i].dname, dir_name) == 0) {
                index = root.directories[i].nStartBlock;            // found the directory
                break;
            }
        }

        fclose(disk);                                               // close the disk
    }

    return index;
}


/*
    Searches a directory structure at the given start block
    for the given filename and extension. Returns the file
    struct if found.

    RETURNS: The cs1550_file_directory struct of the given filename/extension; otherwise NULL.
*/
static cs1550_file_directory *get_file(long dir_start_block, char *file_name, char *ext_name) {
    cs1550_file_directory *disk_file = NULL;                    // assume file does not exist

    printf("[get_file] dir_start_block=%lu, file_name=%s, ext_name=%s\n", dir_start_block, file_name, ext_name);

    // open the disk file
    FILE *disk = fopen(DISK, "rb");                             // open with respect to binary mode

    // make sure could open the disk file
    if (disk == NULL) {
        // ERROR

    } else {
        cs1550_directory_entry dir;                             // pointer to dir of disk file
        fseek(disk, dir_start_block * BLOCK_SIZE, SEEK_SET);    // seek to the starting block of dir struct
        fread(&dir, sizeof(cs1550_directory_entry), 1, disk);   // get the directory at this start block

        // search for the file within the list of valid files
        int i;
        printf("[get_file] dir.nFiles=%d.\n", dir.nFiles);
        for (i=0; i < dir.nFiles; i++) {                        // loop through valid files
            if (strcmp(dir.files[i].fname, file_name) == 0) {
                if (strcmp(dir.files[i].fext, ext_name)) {
                    disk_file = &dir.files[i];                  // found the file
                    break;
                }
            }
        }

        fclose(disk);                                           // close the disk
    }

    return disk_file;
}

/*
    Looks up the input path to determine if it is a directory
        or a file. If it is a directory, return the appropriate
        permissions. If it is a file, return the appropriate
        permissions AND actual size. The size returned is accurate
        enough to determine EOF.

    RETURNS:    0           SUCCESS
                -ENOENT     path/file not found

    REFERENCE:  man -s 2 stat
*/
static int cs1550_getattr(const char *path, struct stat *stbuf)
{
    // hold method's status
    int status = -ENOENT;                   // default is error

    // hold info about the path
    char dir[MAX_LENGTH];                   // directory name
    char filename[MAX_LENGTH];              // file name
    char ext[MAX_LENGTH];                   // file extension name

    // initialize to hold dir/file info
    memset(stbuf, 0, sizeof(struct stat));
    
    // is path the root dir?
    if (strcmp(path, "/") == 0) {           // path is the root dir
        stbuf->st_mode = S_IFDIR | 0755;    // file type and mode
        stbuf->st_nlink = 2;                // number of hard links

        status = 0;                         // SUCCESS

    } else {
        // make sure path is within maximum length
        if (strlen(path) < MAX_LENGTH) {
            /*
                Get the directory and file information from the path

                On success, the function returns the number of variables filled. 
                In the case of an input failure before any data could be successfully read, 
                EOF is returned.
            */
            int scan_result = sscanf(path, "/%[^/]/%[^.].%s", dir, filename, ext);

            if ((scan_result == EOF) ||                 // EOF error
                (scan_result <= 0) ||                   // nothing was filled (no dir, no filename, no ext)
                (strlen(dir) > MAX_FILENAME) ||         // make sure directory within max length
                (strlen(filename) > MAX_FILENAME) ||    // make sure filename within max length
                (strlen(ext) > MAX_EXTENSION))          // make sure extension within max length
            {
                // ERROR

            } else {
                // find the directory (if it exists)
                printf("[cs1550_getattr] calling find_directory with dir=%s\n", dir);
                long dir_block = find_directory(dir);       // returns the starting block of the directory entry
                printf("[cs1550_getattr] dir_block=%lu\n", dir_block);
                if (dir_block < 0) {
                    // DIRECTORY NOT FOUND

                } else if (scan_result == 1) {              // RETURN DIRECTORY INFO

                    stbuf->st_mode = S_IFDIR | 0755;        // file type and mode
                    stbuf->st_nlink = 2;                    // number of hard links

                    status = 0;                             // SUCCESS

                } else {                                    // RETURN FILE INFO

                    if (scan_result == 2) { ext[0] = '\0';} // make extension NULL if blank

                    // find the filename (if it exists)
                    cs1550_file_directory *file;
                    file = get_file(dir_block, filename, ext);

                    if (file == NULL) {
                        // FILE NOT FOUND

                    } else {                                // RETURN FILE INFO
                        // found the file
                        stbuf->st_mode = S_IFREG | 0666;    // file type and mode
                        stbuf->st_nlink = 1;                // number of hard links
                        stbuf->st_size = file->fsize;       // total file size, in bytes
                        
                        status = 0;                         // SUCCESS
                    }
                }
            }
        }
    }

    // return 0 if success; otherwise -ENOENT
    return status;
}


/*
    looks up the input path, ensuring that it is a directory, and then
        lists the contents of that path. Uses include 'stat', 'ls -a',
        or even TAB completion from terminal.

    NOTE: The output is emulated within FUSE by using the filler() method

    RETURNS:    0           SUCCESS
                -ENOENT     directory is not valid, or not found

    REFERENCE: man -s 2 readdir
*/
static int cs1550_readdir(const char *path, void *buf, fuse_fill_dir_t filler,
    off_t offset, struct fuse_file_info *fi)
{
    // Since we're building with -Wall (all warnings reported) we need
    // to "use" every parameter, so let's just cast them to void to
    // satisfy the compiler
    (void) offset;
    (void) fi;

    int status = 0;                 // default to good

    char dir_name[MAX_LENGTH];      // holds directory name
    char filename[MAX_LENGTH];      // holds filename
    int num;                        // used to iterate through entries

    printf("[cs1550_readdir] path = %s\n", path);

    if (strlen(path) > MAX_LENGTH) {
        // invalid path
        status = -ENOENT;                           // ERROR: path is longer than maximum length allowed

    } else {
        // get the directory, filename, extension
        int scan_result = sscanf(path, "/%[^/]/%[^.]", dir_name, filename);

        printf("[cs1550_readdir] scan_result=%d\n", scan_result);
        printf("[cs1550_readdir] dir_name=%s\n", dir_name);

        if ((strcmp(path, "/") != 0) && 
            ((scan_result == EOF) ||                // EOF error
            (scan_result <= 0) ||                   // path provided was not root or subdir of root
            (strlen(dir_name) > MAX_FILENAME)))     // make sure directory within max length
        {
            printf("[cs1550_readdir] PROPER FAILURE\n");
            status = -ENOENT;                       // ERROR: given a path that is not a subdir within root

        } else {
            // open the disk file
            FILE *disk = fopen(DISK, "rb");         // open with respect to binary mode

            if (disk == NULL) {
                // ERROR: could not open disk

            } else {
                printf("[cs1550_readdir] GOOD\n");

                // the filler function allows us to add entries to the listing
                filler(buf, ".", NULL, 0);          // default
                filler(buf, "..", NULL, 0);         // default

                // get reference to the root struct
                cs1550_root_directory root;                                 // pointer to root of disk file
                fread(&root, sizeof(cs1550_root_directory), 1, disk);       // put first 512bytes into root struct

                if (scan_result <= 0) {
                    printf("[cs1550_readdir] LISTING CONTENTS OF ROOT\n");

                    // list contents of root directory (directories only)
                    for (num=0; num < root.nDirectories; num++) {
                        filler(buf, root.directories[num].dname, NULL, 0);  // add this directory to the output
                    }
                    
                } else {
                    // list contents of subdirectory (filenames only)
                    printf("[cs1550_readdir] LISTING CONTENTS OF SUBDIR\n");

                    // get the subdirectory's location that was referenced
                    long offset = 0;
                    for (num=0; num < root.nDirectories; num++) {
                        if (strcmp(root.directories[num].dname, dir_name) == 0) {
                            offset = root.directories[num].nStartBlock * BLOCK_SIZE;    // found the reference
                        }
                    }

                    // get reference to subdirectory's contents using offset
                    cs1550_directory_entry dir_entry;
                    fseek(disk, offset, SEEK_SET);                                      // goto subdir's location on disk
                    fread(&dir_entry, sizeof(cs1550_directory_entry), 1, disk);         // read in subdir struct

                    // output the filename, extension, and filesize
                    for (num=0; num < dir_entry.nFiles; num++) {
                        filler(buf, dir_entry.files[num].fname + 1, NULL, 0);           // add this file to the output
                    }
                }
                fclose(disk);       // close the disk file
            }
        }
    }

    /*
    // add the user stuff (subdirs or files)
    // the +1 skips the leading '/' on the filenames
    filler(buf, newpath + 1, NULL, 0);
    */
   return status;
}


/*
    Adds the new directory to the root level ONLY, and updates
        the .disk file appropriately by adding an entry in the 
        root's list of directories and pointing to an entry for
        the new directory within block on disk.

    RETURNS:    0               SUCCESS
                -ENAMETOOLONG   directory name too long
                -EPERM          directory is not within the root directory
                -EEXIST         directory already exists
                -ENOSPC         no space left on disk to create
                -ENOENT         disk file could not be opened
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

    //printf("cs1550_mkdir scan_result = %d\n", scan_result);

    if (scan_result != 1) {
        status = -EPERM;                // ERROR: can ONLY create dir within '/' root

    } else if (strlen(dir_name) > MAX_FILENAME) {
        status = -ENAMETOOLONG;         // ERROR: directory name too long

    } else if ((free_block = find_free_block()) == -1) {
        status = -ENOSPC;               // ERROR: no space left on disk

    } else {
        /* TRY TO CREATE THE DIRECTORY */

        // open the disk file
        disk = fopen(DISK, "r+b");      // open for reading + writing (binary)

        if (disk == NULL) {
            status = -ENOENT;           // ERROR: file not opened successfully

        } else {
            // get the root within the disk file
            cs1550_root_directory root;                             // pointer to root of disk file
            fread(&root, sizeof(cs1550_root_directory), 1, disk);   // root in disk exists within first 512bytes

            // make sure the root can hold another directory listing
            if (root.nDirectories >= MAX_DIRS_IN_ROOT) {
                status = -ENOSPC;                                   // ERROR: not enough space

            } else {
                // create directory inside the free block
                cs1550_directory_entry *new_dir;                    // create a new directory struct to put in free block
                new_dir = (struct cs1550_directory_entry*)calloc(1, sizeof(struct cs1550_directory_entry));
                new_dir->nFiles = 0;                                // no files exist at first
                //printf("cs1550_mkdir CREATED cs1550_directory_entry structure\n");

                fseek(disk, free_block * BLOCK_SIZE, SEEK_SET);     // goto free block
                fwrite(new_dir, sizeof(cs1550_directory_entry), 1, disk);   // write new dir entry to disk
                //printf("cs1550_mkdir SEEKED TO FREE BLOCK %d and wrote dir_entry struct\n", free_block);

                // create root dir struct
                struct cs1550_directory *new_dir_entry;            // create a new directory stub
                new_dir_entry = (struct cs1550_directory*)calloc(1, sizeof(struct cs1550_directory));
                //printf("cs1550_mkdir CREATED cs1550_directory structure\n");

                strcpy(new_dir_entry->dname, dir_name);            // name of the actual directory
                //printf("cs1550_mkdir COPIED dir_name '%s' to cs1550_directory struct dname '%s'\n", dir_name, new_dir_entry->dname);

                new_dir_entry->nStartBlock = free_block;           // make the start block the beginning of the free block found
                //printf("cs1550_mkdir COPIED free_block '%d' to new_dir_entry.nStartBlock '%lu'\n", free_block, new_dir_entry->nStartBlock);

                //printf("cs1550_mkdir UPDATING root nDirectories by adding new_dir_entry\n");
                root.directories[root.nDirectories] = *new_dir_entry;   // add directory to list of valid directories
                root.nDirectories++;
                //printf("cs1550_mkdir nDirectories=%d", root.nDirectories);

                // write out the root to disk
                //printf("cs1550_mkdir WRITING OUT THE NEW ROOT TO DISK\n");
                fseek(disk, 0, SEEK_SET);     // goto free block
                fwrite(&root, sizeof(struct cs1550_root_directory), 1, disk);   // write root to disk

                //printf("cs1550_mkdir OUTPUTTING new_dir_entry from within root: dname='%s' nstartblock='%lu'\n", root.directories[root.nDirectories-1].dname, root.directories[root.nDirectories-1].nStartBlock);
            }
        }
    }
    
    printf("cs1550_mkdir status=%d\n", status);
    if (status == 0) {
        fclose(disk);           // close the disk
        write_bitmap();         // update the bitmap on disk
        
        printf("cs1550_mkdir UPDATED BITMAP. max dirs allowed in root: %lu\n", MAX_DIRS_IN_ROOT);
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
    Does the actual creation of a file. Mode and dev can be ignored.

    Adds a new file to a subdirectory, and updates the .disk file
    appropriately with the modified directory entry structure.

    root->directories[dir_num].nStartBlock
    directory_entry
    file_directory

    RETURNS:    0               SUCCESS
                -ENAMETOOLONG   file name is beyond 8.3 characters
                -EPERM          file is trying to be created in root dir
                -EEXIST         file already exists

    REFERENCE: man -s 2 mknod
*/
static int cs1550_mknod(const char *path, mode_t mode, dev_t dev)
{
    (void) mode;
    (void) dev;

    int status = 0;                         // assume SUCCESS

    // hold the directory, filename, and extension
    char dir[MAX_LENGTH];                   // directory
    char filename[MAX_LENGTH];              // filename
    char ext[MAX_LENGTH];                   // extension
    
    if (strcmp(path, "/") == 0) {
        status = -EPERM;                    // ERROR: file cannot be created in root dir

    } else if (strlen(path) >= MAX_LENGTH) {
        status = -ENAMETOOLONG;             // ERROR: path name too long

    } else {
        /*
            Get the directory and file information

            On success, the function returns the number of variables filled. 
            In the case of an input failure before any data could be successfully read, 
            EOF is returned.
        */
        int scan_result = sscanf(path, "/%[^/]/%[^.].%s", dir, filename, ext);

        if ((scan_result == EOF) ||                 // EOF error
            (scan_result <= 0) ||                   // nothing was filled (no dir, no filename, no ext)
            (strlen(dir) > MAX_FILENAME) ||         // make sure directory within max length
            (strlen(filename) > MAX_FILENAME) ||    // make sure filename within max length
            (strlen(ext) > MAX_EXTENSION))          // make sure extension within max length
        {
            status = -ENAMETOOLONG;                 // ERROR: path or file invalid

        } else {
            // make sure the filename/ext has not already been created


        }

    }


    return status;
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

    // 
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
