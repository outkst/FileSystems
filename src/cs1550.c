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

#include    <errno.h>
#include    <fcntl.h>
#include    <fuse.h>
#include    <stdio.h>
#include    <stdlib.h>
#include    <string.h>

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

    RETURNS:    0+  SUCCESS. The start block offset of the given directory
                -1  The directory does not exist
*/
static long find_directory(char *dir_name) {
    long index = -1;                        // assume directory does not exist

    // open the disk file
    FILE *disk = fopen(DISK, "rb");         // open with respect to binary mode

    // make sure could open the disk file
    if (disk == NULL) {
        // ERROR

    } else {
        cs1550_root_directory root;                                 // pointer to root of disk file
        fread(&root, sizeof(cs1550_root_directory), 1, disk);       // root struct is in first block of disk

        // search for the directory within the list of valid directories
        int i;
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
    Given an offset to a disk block, will return the cs1550_directory_entry structure.
*/
static cs1550_directory_entry *get_directory(long index) {
    cs1550_directory_entry *dir;                                // assume dir entry does not exist
    dir = (cs1550_directory_entry*)calloc(1, sizeof(cs1550_directory_entry));

    // open the disk file
    FILE *disk = fopen(DISK, "rb");                             // open with respect to binary mode

    // make sure could open the disk file
    if (disk == NULL) {
        // ERROR

    } else {
        fseek(disk, index * BLOCK_SIZE, SEEK_SET);              // seek to the starting block of dir struct
        fread(dir, sizeof(cs1550_directory_entry), 1, disk);    // get the directory at this start block
        fclose(disk);                                           // close the disk
    }

    return dir;
}


/*
    Searches through the given directory entry's file list and returns
    the index of where the file directory is located within the directory structure.

    RETURNS:    0+          SUCCESS; location of the file in the dir struct
                -1          not found
*/
static int find_file(cs1550_directory_entry *dir, char *file_name, char *ext_name) {
    int index = -1;

    int i;
    cs1550_file_directory file_dir;
    for (i=0; i < dir->nFiles; i++) {                   // loop through valid files
        file_dir = dir->files[i];
        if (strcmp(file_dir.fname, file_name) == 0) {
            if (strcmp(file_dir.fext, ext_name) == 0) {
                index = i;                  // found the file struct
                break;
            }
        }
    }

    return index;
}


/*
    Searches through the given directory entry's file list and returns
    the file directory with the given filename and extension; 

    RETURNS:    cs1550_file_directory*      the file struct of the given filename/extension
                NULL                        the filename/extension was not found
*/
static cs1550_file_directory *get_file(cs1550_directory_entry *dir, char *file_name, char *ext_name) {
    cs1550_file_directory *disk_file = NULL;            // assume file does not exist


    int i;
    cs1550_file_directory file_dir;
    for (i=0; i < dir->nFiles; i++) {                   // loop through valid files
        file_dir = dir->files[i];
        if (strcmp(file_dir.fname, file_name) == 0) {
            if (strcmp(file_dir.fext, ext_name) == 0) {
                disk_file = &file_dir;                  // found the file struct
                break;
            }
        }
    }

    return disk_file;
}


/*
    Given a starting disk block index on the disk, will traverse the given
    block_num nodes and return the disk block at this location.
*/
static cs1550_disk_block *get_disk_block(long index, int block_num) {
    cs1550_disk_block *disk_block;
    disk_block = (cs1550_disk_block*)calloc(1, sizeof(cs1550_disk_block));

    // open the disk file
    FILE *disk = fopen(DISK, "rb");                             // open with respect to binary mode

    // make sure could open the disk file
    if (disk == NULL) {
        // ERROR

    } else {
        // traverse the given number of nodes
        int i;
        for (i = 0; i <= block_num; i++) {
            fseek(disk, index * BLOCK_SIZE, SEEK_SET);             // seek to the position of the next disk block for this file
            fread(disk_block, sizeof(cs1550_disk_block), 1, disk);      // get the disk block at this start block
            index = disk_block->nNextBlock;                        // get the disk location of the next block associated with this file
        }

        fclose(disk);                                                   // close the disk file
    }

    return disk_block;
}


/*
    Given a disk block, will write it out to disk at the given location.
*/
static void write_block_to_disk(cs1550_disk_block *block, long index) {
    // open the disk file
    FILE *disk = fopen(DISK, "r+b");                         // open read/write binary mode

    // make sure could open the disk file
    if (disk == NULL) {
        // ERROR

    } else {
        fseek(disk, index * BLOCK_SIZE, SEEK_SET);          // seek to the starting block of file block struct
        fwrite(block, sizeof(cs1550_disk_block), 1, disk);  // get the file block at this start block
        fclose(disk);                                       // close the disk
    }
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
                long dir_block = find_directory(dir);       // find the directory

                if (dir_block < 0) {
                    // DIRECTORY NOT FOUND

                } else if (scan_result == 1) {              // RETURN DIRECTORY INFO

                    stbuf->st_mode = S_IFDIR | 0755;        // file type and mode
                    stbuf->st_nlink = 2;                    // number of hard links
                    status = 0;                             // SUCCESS

                } else {                                    // RETURN FILE INFO
                    // get the dir entry struct
                    cs1550_directory_entry *dir_entry;      // holds the directory entry
                    dir_entry = get_directory(dir_block);   // gets the directory entry


                    if (scan_result == 2) { ext[0] = '\0';} // make extension NULL if blank

                    // find the filename (if it exists)
                    cs1550_file_directory *file;
                    file = get_file(dir_entry, filename, ext);

                    if (file == NULL) {
                        // FILE NOT FOUND

                    } else {
                        // found the file
                        stbuf->st_mode = S_IFREG | 0666;    // file type and mode
                        stbuf->st_nlink = 1;                // number of hard links
                        stbuf->st_size = file->fsize;       // total file size, in bytes
                        
                        status = 0;                         // SUCCESS
                    }

                    // cleanup pointers
                    //free(dir_entry);
                    //free(file);
                }
            }
        }
    }

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


    if (strlen(path) > MAX_LENGTH) {
        // invalid path
        status = -ENOENT;                           // ERROR: path is longer than maximum length allowed

    } else {
        // get the directory, filename, extension
        int scan_result = sscanf(path, "/%[^/]/%[^.]", dir_name, filename);


        if ((strcmp(path, "/") != 0) && 
            ((scan_result == EOF) ||                // EOF error
            (scan_result <= 0) ||                   // path provided was not root or subdir of root
            (strlen(dir_name) > MAX_FILENAME)))     // make sure directory within max length
        {
            status = -ENOENT;                       // ERROR: given a path that is not a subdir within root

        } else {
            // open the disk file
            FILE *disk = fopen(DISK, "rb");         // open with respect to binary mode

            if (disk == NULL) {
                // ERROR: could not open disk

            } else {

                // the filler function allows us to add entries to the listing
                filler(buf, ".", NULL, 0);          // default output
                filler(buf, "..", NULL, 0);         // default output

                // get reference to the root struct
                cs1550_root_directory root;                                 // pointer to root of disk file
                fread(&root, sizeof(cs1550_root_directory), 1, disk);       // put first 512bytes into root struct

                if (scan_result <= 0) {

                    // list contents of root directory (directories only)
                    for (num=0; num < root.nDirectories; num++) {
                        filler(buf, root.directories[num].dname, NULL, 0);  // add this directory to the output
                    }
                    
                } else {
                    // list contents of subdirectory (filenames only)

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
                    char filename[MAX_LENGTH];
                    for (num=0; num < dir_entry.nFiles; num++) {
                        // see if file has extension
                        strcpy(filename, dir_entry.files[num].fname);
                        if (strlen(dir_entry.files[num].fext) > 0) {
                            strcat(filename, ".");
                            strcat(filename, dir_entry.files[num].fext);
                        }
                        filler(buf, filename, NULL, 0);           // add this file to the output
                    }
                }
                fclose(disk);       // close the disk file
            }
        }
    }

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

    int status = 0;
    long free_block;
    char dir_name[MAX_LENGTH];
    char file[MAX_LENGTH];
    FILE *disk;

    // if the path contains a slash within the string then
    //      it's not within the root directory
    int scan_result = sscanf(path, "/%[^/]/%[^.]", dir_name, file);


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

                fseek(disk, free_block * BLOCK_SIZE, SEEK_SET);     // goto free block
                fwrite(new_dir, sizeof(cs1550_directory_entry), 1, disk);   // write new dir entry to disk

                // create root dir struct
                struct cs1550_directory *new_dir_entry;            // create a new directory stub
                new_dir_entry = (struct cs1550_directory*)calloc(1, sizeof(struct cs1550_directory));

                strcpy(new_dir_entry->dname, dir_name);            // name of the actual directory

                new_dir_entry->nStartBlock = free_block;           // make the start block the beginning of the free block found

                root.directories[root.nDirectories] = *new_dir_entry;   // add directory to list of valid directories
                root.nDirectories++;

                // write out the root to disk
                fseek(disk, 0, SEEK_SET);     // goto free block
                fwrite(&root, sizeof(struct cs1550_root_directory), 1, disk);   // write root to disk


                // free up space
                free(new_dir);
                free(new_dir_entry);
            }
        }
    }
    
    if (status == 0) {
        fclose(disk);           // close the disk
        write_bitmap();         // update the bitmap on disk
        
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

        if (scan_result == 2) { ext[0] = '\0';}     // make extension NULL if blank
        
        if ((scan_result == EOF) ||                 // EOF error
            (strlen(dir) > MAX_FILENAME) ||         // make sure directory within max length
            (strlen(filename) > MAX_FILENAME) ||    // make sure filename within max length
            (strlen(ext) > MAX_EXTENSION))          // make sure extension within max length
        {
            status = -ENAMETOOLONG;                 // ERROR: path or file invalid

        } else if (scan_result <= 1) {
            status = -EPERM;                        // ERROR: cannot create file in root

        } else {
            // make sure the filename/ext has not already been created

            // get the directory location
            long dir_block = find_directory(dir);               // returns the starting block of the directory entry

            cs1550_directory_entry *dir_entry;
            dir_entry = get_directory(dir_block);               // gets the actual dir entry struct

            if (dir_block < 0) {
                status = -ENOENT;                               // ERROR: directory not found

            } else if (dir_entry->nFiles >= MAX_FILES_IN_DIR) {
                status = -ENOSPC;                               // ERROR: max number of files created in this directory

            } else {
                // get the list of files for this directory
                cs1550_file_directory *file = get_file(dir_entry, filename, ext);

                if (file == NULL) {
                    // check if space exists
                    long free_block;
                    if ((free_block = find_free_block()) == -1) {
                        status = -ENOSPC;                       // ERROR: no space left on disk

                    } else {
                        // create the file
                        cs1550_file_directory *new_file;         // create a new file dir struct
                        new_file = (cs1550_file_directory*)calloc(1, sizeof(cs1550_file_directory));

                        strcpy(new_file->fname, filename);      // file name
                        strcpy(new_file->fext, ext);            // extension name
                        new_file->nStartBlock = find_free_block();     // offset on disk of starting block
                        new_file->fsize = 0;                    // default size


                        // add to directory entry
                        dir_entry->files[dir_entry->nFiles] = *new_file;    // add this file to the list of files in the directory
                        dir_entry->nFiles++;                                // increment number of valid files in this directory


                        // write out the root to disk
                        FILE *disk = fopen(DISK, "r+b");                                    // open read/write binary mode
                        fseek(disk, dir_block * BLOCK_SIZE, SEEK_SET);                      // goto free block
                        fwrite(dir_entry, sizeof(struct cs1550_directory_entry), 1, disk);  // write root to disk
                        fclose(disk);
                    }
                } else {
                    status = -EEXIST;                           // ERROR: file already exists
                }

                // cleanup pointers
                //free(file);
            }

            // cleanup pointers
            //free(dir_entry);
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
    (void) fi;

    // check to make sure path exists
    // check that size is > 0
    // check that offset is <= to the file size
    // read in data
    // set size and return, or error

    char dir[MAX_LENGTH];           // directory
    char filename[MAX_LENGTH];      // filename
    char ext[MAX_LENGTH];           // extension

    // break path up into directory, filename, and extension
    int scan_result = sscanf(path, "/%[^/]/%[^.].%s", dir, filename, ext);

    // make sure size and offset are valid
    if ((size == 0) ||
        (offset > size)) {
        return -EFBIG;                                                            // ERROR: offset is beyond file size

    } else if (scan_result < 2) {
        return -EISDIR;                                                           // ERROR: trying to read out a directory

    } else {
        // check to make sure path (file) exists by getting the file
        long dir_block = find_directory(dir);                                       // get block offset to where this dir entry is held
        cs1550_directory_entry *dir_entry = get_directory(dir_block);               // get the actual dir entry struct
        cs1550_file_directory *file_entry = get_file(dir_entry, filename, ext);     // get the filename struct
        long block_loc = file_entry->nStartBlock;                                   // store location, on disk, of block

        // determine how many blocks will be needed to traverse
        long num_blocks_needed = (1 + (((size - 1) / MAX_DATA_IN_BLOCK)));          // get ceiling value of blocks needed
        long start_block = offset / MAX_DATA_IN_BLOCK;                              // the block to start writing/appending to
        long start_offset = offset % MAX_DATA_IN_BLOCK;                             // specific offset within starting block
        size_t bytes_read = 0;                                                      // number of bytes written so far
        
        // read data into buf from disk
        cs1550_disk_block *disk_block = get_disk_block(block_loc, start_block);     // get data block
        long block_num, data_offset=0;
        size_t bytes_left;                                                          // amount of bytes left to write out
        for (block_num = 0; block_num < num_blocks_needed; block_num++) {
            bytes_left = size - bytes_read;                                        // calculate amount of bytes left to write
            data_offset = ((block_num == 0) ? start_offset : 0);                    // if first block, start at proper offset

            // copy over # bytes_left or MAX_DATA_IN_BLOCK to this block
            strncpy((buf + bytes_read), (disk_block->data + data_offset), ((bytes_left < MAX_DATA_IN_BLOCK) ? bytes_left : MAX_DATA_IN_BLOCK));
            bytes_read += ((bytes_left < MAX_DATA_IN_BLOCK) ? bytes_left : MAX_DATA_IN_BLOCK);

            // grab another free block if needed
            if (block_num != (num_blocks_needed - 1)) {                             // -1 to check against 0-based
                block_loc = find_free_block();                                      // get free block for next write
                if (block_loc < 0) { return -ENOSPC; }                              // ERROR: no space left
                disk_block->nNextBlock = block_loc;                                 // store location to next block
            } else {
                disk_block->nNextBlock = 0;                                         // store location to next block   
            }

            // switch to the next block
            if (block_num != (num_blocks_needed - 1)) {
                disk_block = get_disk_block(block_loc, block_num+1);                // +1 to get NEXT block number
            }
        }
    }

    return size;
}


/* 
    Writes the data passed in via buf into the file at path, starting
    at the given offset within the file block. Each block will be
    written back to disk upon completion.
 */
static int cs1550_write(const char *path, const char *buf, size_t size, 
   off_t offset, struct fuse_file_info *fi)
{
    (void) fi;

    char dir[MAX_LENGTH];           // directory
    char filename[MAX_LENGTH];      // filename
    char ext[MAX_LENGTH];           // extension

    // break path up into directory, filename, and extension
    sscanf(path, "/%[^/]/%[^.].%s", dir, filename, ext);

    // check to make sure path (file) exists by getting the file
    long dir_block = find_directory(dir);                                   // get block offset to where this dir entry is held
    cs1550_directory_entry *dir_entry = get_directory(dir_block);           // get the actual dir entry struct
    int file_index = find_file(dir_entry, filename, ext);                   // get the index of the file within the dir entry struct
    cs1550_file_directory *file_entry = get_file(dir_entry, filename, ext); // get the filename struct
    long block_loc = file_entry->nStartBlock;                               // store location, on disk, of block

    // update file size within the file struct
    file_entry->fsize = size;
    dir_entry->files[file_index] = *file_entry;

    // write directory entry back to disk with updated size
    FILE *disk = fopen(DISK, "r+b");                                        // open read/write binary mode
    fseek(disk, dir_block * BLOCK_SIZE, SEEK_SET);                          // goto free block
    fwrite(dir_entry, sizeof(struct cs1550_directory_entry), 1, disk);      // write root to disk
    fclose(disk);

    // make sure size and offset are valid
    if ((size == 0) ||
        (offset > size)) {
        return -EFBIG;                                                    // ERROR: offset is beyond file size

    } else {

        // determine how many blocks will be needed
        long num_blocks_needed = (1 + (((size - 1) / MAX_DATA_IN_BLOCK)));          // get ceiling value of blocks needed
        long start_block = offset / MAX_DATA_IN_BLOCK;                              // the block to start writing/appending to
        long start_offset = offset % MAX_DATA_IN_BLOCK;                             // specific offset within starting block
        size_t bytes_wrote = 0;                                                     // number of bytes written so far

        // write out the data to file's disk blocks
        cs1550_disk_block *disk_block = get_disk_block(block_loc, start_block);     // get data block
        long block_num, data_offset=0;
        size_t bytes_left;                                                          // amount of bytes left to write out
        for (block_num = 0; block_num < num_blocks_needed; block_num++) {
            bytes_left = size - bytes_wrote;                                        // calculate amount of bytes left to write
            data_offset = ((block_num == 0) ? start_offset : 0);                    // if first block, start at proper offset

            // copy over # bytes_left or MAX_DATA_IN_BLOCK to this block
            strncpy((disk_block->data + data_offset), (buf + bytes_wrote), ((bytes_left < MAX_DATA_IN_BLOCK) ? bytes_left : MAX_DATA_IN_BLOCK));
            bytes_wrote += ((bytes_left < MAX_DATA_IN_BLOCK) ? bytes_left : MAX_DATA_IN_BLOCK);

            // grab another free block if needed
            if (block_num != (num_blocks_needed - 1)) {                             // -1 to check against 0-based
                block_loc = find_free_block();                                      // get free block for next write
                if (block_loc < 0) { return -ENOSPC; }                              // ERROR: no space left
                disk_block->nNextBlock = block_loc;                                 // store location to next block
            } else {
                disk_block->nNextBlock = 0;                                         // store location to next block   
            }

            // write this block to disk at the given location
            write_block_to_disk(disk_block, block_loc);                             // write out to disk

            // switch to the next block
            if (block_num != (num_blocks_needed - 1)) {
                disk_block = get_disk_block(block_loc, block_num+1);                // +1 to get NEXT block number
            }
        }
    }

    // cleanup pointers
    //free(dir_entry);
    //free(file_entry);
    
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
