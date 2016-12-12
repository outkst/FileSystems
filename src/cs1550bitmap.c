/*
    File System Implementation

    Joe Meszar (jwm54@pitt.edu)
    CS1550 Project 4 (FALL 2016)

    REFERENCES
    ----------
    BITMAP:                     www.mathcs.emory.edu/~cheung/Courses/255/Syllabus/1-C-intro/bit-array.html
    BYTE-TO-BINARY:             http://stackoverflow.com/a/112956
    CEILING INTEGER DIVISION:   http://stackoverflow.com/a/2745086
*/

#include <stdio.h>                  /* FILE SEEK_END prntf() */
#include <string.h>                 /* strcat() */
#include <errno.h>                  /* ENOENT */

typedef unsigned char bitmap;       /* bitmap data structure */

#define DISK        ".disk"         /* keep reference to the physical '.disk' file */
#define BLOCK_SIZE  512             /* disk's block size, in bytes */
#define DISK_SIZE   5242880         /* size, in bytes, of disk; assuming disk size is 5M bytes
                                        (5,242,880bytes AKA 41,943,040bits | [dd bs=1K count=5K if=/dev/zero of=.disk]) */
#define SIZEOF_BITMAP  8            /* size, in bits, of the data type used for the bitmap. 
                                        (needed because C doesn't have a 'bit' data type) */
#define MAP_SIZE                    (DISK_SIZE / BLOCK_SIZE)                /* total size, in bytes, needed for the bitmap (==10240) */
#define MAP_INDICES                 (MAP_SIZE / SIZEOF_BITMAP)              /* total number of indices for the bitmap (==1280) */
#define MAP_DISK_BLOCKS_NEEDED      (1 + ((MAP_INDICES - 1) / BLOCK_SIZE))  /* how many blocks, on disk, required to hold the bitmap */
#define GET_BM_INDEX(i)             ((i) / SIZEOF_BITMAP)                   /* given a disk file index, returns an index into the bitmap array */
#define GET_BIT_OFFSET(i)           ((i) % SIZEOF_BITMAP)                   /* returns a single bit in the index */

static bitmap *map = NULL;              /* will be MAP_SIZE when intialized */

void clear_bit(int index);              /* clears the bit at a given disk file index */
int find_free_block(void);              /* finds a free block by looking at the bitmap */
int get_bit(int index);                 /* gets the bit at the given disk file index */
void init_bitmap(void);                 /* initializes the bitmap by zero'ing and setting defaults */
void set_bit(int index);                /* sets the bit at the given disk file index */
void write_bitmap(void);                /* writes out the bitmap to the very end of the disk file */

const char *byte_to_binary(int x);      /* used to debug and output the bit-state of a bitmap's index */


/*
    Initializes the bitmap by associating it with the defined disk file. Performs
        default operations on the bitmap to mark the root and location of the bitmap
        within the file as occupied. The following disk index locations are marked,
        assuming a 5M disk file that the bitmap will be associated with:

        0           root struct
        (MAX-2)     bitmap struct
        (MAX-1)     bitmap struct
        (MAX)       bitmap struct
*/
void init_bitmap(void) {

    FILE *disk = fopen(DISK, "rb");                 // open disk file with respect to binary mode

    if (disk == NULL) {
        //return -ENOENT;                           // ERROR: file not opened successfully

    } else {
        // allocate the needed space for map
        map = calloc(MAP_SIZE, 1);                  // use defined size, in bytes

        // get the bitmap from the disk file
        int offset = BLOCK_SIZE * MAP_DISK_BLOCKS_NEEDED;   // bitmap held in last three blocks of file
    
        fseek(disk, -(offset), SEEK_END);                   // set position to beginning of bitmap struct
        fread(map, MAP_SIZE, 1, disk);                      // read the bitmap in from disk

        // set special regions of the bitmap to USED
        set_bit(0);                                         // reserve this space for the root struct

        // set the needed amount of bits to reserve space for the bitmap
        int i;
        for (i=1; i<=MAP_DISK_BLOCKS_NEEDED; i++) {
            set_bit(MAP_SIZE-i);                            // reserve this space for the bitmap struct
        }
        
        fclose(disk);                                       // close the disk file
    }

}

/*
    Sets the bit at the given index to '1' (aka USED)
*/
void set_bit(int index) {

    if (map == NULL) { init_bitmap(); }  // make sure bitmap is initialized

    map[GET_BM_INDEX(index)] |= (1 << GET_BIT_OFFSET(index));

}

/*
    Sets the bit at the given index to '0' (aka FREE).
*/
void clear_bit(int index) {

    if (map == NULL) { init_bitmap(); }  // make sure bitmap is initialized

    map[GET_BM_INDEX(index)] &= ~(1 << GET_BIT_OFFSET(index));

}

/*
    Gets the bit at the given index.
*/
int get_bit(int index) {

    if (map == NULL) { init_bitmap(); }  // make sure bitmap is initialized

    bitmap bit = map[GET_BM_INDEX(index)] & (1 << GET_BIT_OFFSET(index));


    return bit != 0;
}

/*
    Search for the first block that is empty and return it. Disregard
    block zero (0) and the last three blocks because it is the root block
    and the blocks used to store the bitmap, respectively.
*/
int find_free_block(void) {

    if (map == NULL) { init_bitmap(); }                         // make sure bitmap is initialized

    int index = 1;                                              // skip block 0 aka ROOT struct
    int disk_end = MAP_SIZE - MAP_DISK_BLOCKS_NEEDED;           // skip where MAP struct is stored
    while (index < disk_end) {
        int bit = get_bit(index);                               // get next bit
        if (bit == 0) { 
            set_bit(index);                                     // mark this free bit as occupied
            return index;                                       // found a free block
        }
        index++;
    }
    return -1;                                                  // no free blocks available
}


/*
    Takes the given bitmap and writes it out to the DISK.
*/
void write_bitmap(void) {

    if (map == NULL) { init_bitmap(); }         // make sure bitmap is initialized

    FILE *disk = fopen(DISK, "r+b");            // open file read/write with respect to binary mode

    if (disk == NULL) {
        //return -ENOENT;                       // ERROR: file not opened successfully

    } else {
        // write the bitmap to the disk file
        long offset = BLOCK_SIZE * MAP_DISK_BLOCKS_NEEDED;  // bitmap held in last three blocks of file
        fseek(disk, -(offset), SEEK_END);                   // bitmap held in last three blocks of file
        fwrite(map, MAP_SIZE, 1, disk);                     // write bitmap to disk
        fclose(disk);                                       // close the file
    }

}


const char *byte_to_binary(int x)
{
    static char b[9];
    b[0] = '\0';

    int z;
    for (z = 128; z > 0; z >>= 1)
    {
        strcat(b, ((x & z) == z) ? "1" : "0");
    }

    return b;
}


/*
    Rebuilds a bitmap, given a disk file. This can be used to sanity check
    or also used if no bitmap currently exists within the disk file.
*/
// void rebuild_bitmap(bitmap *map, FILE *disk) {
//     // open the disk
//     //FILE *disk = fopen(DISK, "rb");                             // open with respect to binary mode

//     // make sure success
//     if (disk == NULL) {
//         return -ENOENT;

//     } else {
//         // set certain regions of the bitmap to USED
//         set_bit(0);                                        // root
//         set_bit(MAP_BLOCK_SIZE_ON_DISK-3);                               // bitmap start
//         set_bit(MAP_BLOCK_SIZE_ON_DISK-2);                               // bitmap mid
//         set_bit(MAP_BLOCK_SIZE_ON_DISK-1);                                 // bitmap end

//         cs1550_root_directory root;                             // pointer to root of disk file
//         fread(&root, sizeof(cs1550_root_directory), 1, disk);   // put first 512bytes into root struct

//         // loop through each directory to know what
//         //        blocks are currently being used
//         long dir_num, file_num;                                 // current dir/file number in list
//         long file_block_span, block_num, block_loc;             // how many disk blocks the file spans, with count and location
//         struct cs1550_directory_entry dir_entry;                // the current directory entry structure
//         struct cs1550_disk_block file_block;                    // the current file entry block structure
//         for (dir_num = 0; dir_num < root.nDirectories; dir_num++) {
//             // mark this location of the block as USED in the bitmap
//             set_bit(root.directories[dir_num].nStartBlock]);

//             // loop through each directory to find what files exist
//             //      and the blocks they are using
//             fseek(disk, SEEK_SET, root.directories[dir_num].nStartBlock);   // seek to the starting block of the directory entry
//             fread(&dir_entry, sizeof(cs1550_directory_entry), 1, disk);     // pull this block as a directory entry
//             for (file_num = 0; file_num < dir_entry.nFiles; file_num++) {
//                 /* 
//                     Get the file information to determine:
//                         1) the block where the file starts
//                         2) how many blocks the file spans
//                         3) traversing of the blocks to find where each one is located
//                 */

//                 // calculate the number of blocks the file consumes (used for linked-list traversal).
//                 //      IMPORTANT: the actual storage size of a disk block is BLOCK_SIZE - sizeof(struct)
//                 file_block_span = (dir_entry.files[file_num].fsize + (BLOCK_SIZE - sizeof(struct cs1550_disk_block)) - 1)
//                                     / (BLOCK_SIZE - sizeof(struct cs1550_disk_block));
//                 block_location = root.directories[dir_num].nStartBlock;         // grab starting block location for this file
//                 for (block_num=0; block_num < file_block_span; block_num++) {
//                     fseek(disk, SEEK_SET, block_location);                      // seek to the starting block of this file entry
//                     fread(&file_block, sizeof(cs1550_disk_block), 1, disk);     // pull this block as a block entry
//                     set_bit(dir_entry.files[file_num].nStartBlock);        // mark the bitmap showing that this block is occupied
//                     block_location = file_block.nNextBlock;                     // get the next block location for this file
//                 }
//             }
//         }
//     }
// }