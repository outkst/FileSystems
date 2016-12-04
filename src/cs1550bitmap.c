/*
    BITMAP IMPLEMENTATION BASED ON:

    http://stackoverflow.com/a/1226129
    www.mathcs.emory.edu/~cheung/Courses/255/Syllabus/1-C-intro/bit-array.html
*/
#include "cs1550.c"                     /* cs1550_root_directory */

typedef unsigned char bitmap;

#define CHAR_BIT    sizeof(char)            /* size of char */
#define MAX_BITS    1280                    /* total # of indices of the bitmap (5M bytes / 512 byte blocks / 8bits per index = 10,240 total blocks) */
#define GET_BM_INDEX(i) ((i) / CHAR_BIT)    /* get the correct index into the array */
#define GET_BIT_OFFSET(i) ((i) % CHAR_BIT)  /* get the correct bit in the index */


/*
    Initializes the bitmap by associating it with the
    given disk file.
*/
void init_bitmap(bitmap *map) {
    FILE *disk = fopen(DISK, "rb");                     // open file with respect to binary mode

    if (disk == NULL) {
        return -ENOENT;                                 // ERROR: file not opened successfully

    } else {
        // get the bitmap from the disk file
        fseek(disk, -(3*BLOCK_SIZE), SEEK_END);         // bitmap held in last three blocks of file
        fread(map, BLOCK_SIZE, 3, disk);                // pull this block as a directory entry

        // set certain regions of the bitmap to USED
        set_bit(map, 0);                                // root
        set_bit(map, MAX_BITS-2);                       // bitmap start
        set_bit(map, MAX_BITS-1);                       // bitmap mid
        set_bit(map, MAX_BITS);                         // bitmap end

        fclose(disk);                                   // close the file
    }
}

/*
    Sets the bit at the given index to '1' (aka USED)
*/
void set_bit(bitmap *map, int index) {
    if (map == NULL) { init_bitmap(map); }  // make sure bitmap is initialized
    
    map[GET_BM_INDEX(index)] |= (1 << GET_BIT_OFFSET(index));
}

/*
    Sets the bit at the given index to '0' (aka FREE).
*/
void clear_bit(bitmap *map, int index) {
    if (map == NULL) { init_bitmap(map); }  // make sure bitmap is initialized

    map[GET_BM_INDEX(index)] &= ~(1 << GET_BIT_OFFSET(index));
}

/*
    Gets the bit at the given index.
*/
int get_bit(bitmap *map, int index) {
    if (map == NULL) { init_bitmap(map); }  // make sure bitmap is initialized

    bitmap bit = map[GET_BM_INDEX(index)] & (1 << GET_BIT_OFFSET(index));
    return bit != 0;
}

/*
    Search for the first block that is empty and return it. Disregard
    block zero (0) and the last three blocks because it is the root block
    and the blocks used to store the bitmap, respectively.
*/
int find_free_block(bitmap *map) {
    if (map == NULL) { init_bitmap(map); }  // make sure bitmap is initialized

    int index = 1;                          // skip block 0 aka ROOT
    while (index < MAX_BITS-3) {            // (MAX-3) to skip where bitmap is stored
        int bit = get_bit(map, index);      // get next bit
        if (bit == 0) { return index; }     // found a free block

        index++;
    }

    return -1;                              // no free blocks available
}



/*
    Rebuilds a bitmap, given a disk file. This can be used to sanity check
    or also used if no bitmap currently exists within the disk file.
*/
void rebuild_bitmap(bitmap *map, FILE *disk) {
    // open the disk
    //FILE *disk = fopen(DISK, "rb");                             // open with respect to binary mode

    // make sure success
    if (disk == NULL) {
        return -ENOENT;

    } else {
        // set certain regions of the bitmap to USED
        set_bit(map, 0);                                        // root
        set_bit(map, MAX_BITS-2);                               // bitmap start
        set_bit(map, MAX_BITS-1);                               // bitmap mid
        set_bit(map, MAX_BITS);                                 // bitmap end

        cs1550_root_directory root;                             // pointer to root of disk file
        fread(&root, sizeof(cs1550_root_directory), 1, disk);   // put first 512bytes into root struct

        // loop through each directory to know what
        //        blocks are currently being used
        long dir_num, file_num;                                 // current dir/file number in list
        long file_block_span, block_num, block_loc;             // how many disk blocks the file spans, with count and location
        struct cs1550_directory_entry dir_entry;                // the current directory entry structure
        struct cs1550_disk_block file_block;                    // the current file entry block structure
        for (dir_num = 0; dir_num < root.nDirectories; dir_num++) {
            // mark this location of the block as USED in the bitmap
            set_bit(map, root.directories[dir_num].nStartBlock]);

            // loop through each directory to find what files exist
            //      and the blocks they are using
            fseek(disk, SEEK_SET, root.directories[dir_num].nStartBlock);   // seek to the starting block of the directory entry
            fread(&dir_entry, sizeof(cs1550_directory_entry), 1, disk);     // pull this block as a directory entry
            for (file_num = 0; file_num < dir_entry.nFiles; file_num++) {
                /* 
                    Get the file information to determine:
                        1) the block where the file starts
                        2) how many blocks the file spans
                        3) traversing of the blocks to find where each one is located
                */

                // calculate the number of blocks the file consumes (used for linked-list traversal).
                //      IMPORTANT: the actual storage size of a disk block is BLOCK_SIZE - sizeof(struct)
                file_block_span = (dir_entry.files[file_num].fsize + (BLOCK_SIZE - sizeof(struct cs1550_disk_block)) - 1)
                                    / (BLOCK_SIZE - sizeof(struct cs1550_disk_block));
                block_location = root.directories[dir_num].nStartBlock;         // grab starting block location for this file
                for (block_num=0; block_num < file_block_span; block_num++) {
                    fseek(disk, SEEK_SET, block_location);                      // seek to the starting block of this file entry
                    fread(&file_block, sizeof(cs1550_disk_block), 1, disk);     // pull this block as a block entry
                    set_bit(map, dir_entry.files[file_num].nStartBlock);        // mark the bitmap showing that this block is occupied
                    block_location = file_block.nNextBlock;                     // get the next block location for this file
                }
            }
        }
    }
}