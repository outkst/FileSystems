/*
    BITMAP IMPLEMENTATION BASED ON:

    http://stackoverflow.com/a/1226129
    www.mathcs.emory.edu/~cheung/Courses/255/Syllabus/1-C-intro/bit-array.html
*/
//#include "cs1550.c"                         /* cs1550_root_directory */
//#include "cs1550.h"
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>

typedef unsigned char bitmap;               /* bitmap data structure */
static bitmap *map = NULL;       // 10240 / sizeof(char) = 1280

#define MAP_SIZE    1280                    /* bitmap holds 1280 bytes */
#define BLOCK_SIZE  512                     /* disk's block size */
#define DISK        ".disk"                 /* keep reference to our .disk file */
#define CHAR_BITS   8                       /* size of char */
#define MAX_BITS    10240                   /* total # of indices of the bitmap (5M bytes / 512 byte blocks / 8bits per index = 10,240 total blocks) */
#define GET_BM_INDEX(i) ((i) / CHAR_BITS)   /* get the correct index into the array */
#define GET_BIT_OFFSET(i) ((i) % CHAR_BITS) /* get the correct bit in the index */

void init_bitmap();
void clear_bit(int index);
int get_bit(int index);
void set_bit(int index);
int find_free_block();
void write_bitmap();
const char *byte_to_binary(int x);
//void rebuild_bitmap(bitmap *map, FILE *disk);

/*
    Initializes the bitmap by associating it with the
    given disk file.
*/
void init_bitmap() {
    printf("init_bitmap INITIALIZING BITMAP...\n");
    FILE *disk = fopen(DISK, "rb");                     // open file with respect to binary mode
    //fclose(disk);
    //printf("init_bitmap CLOSED DISK\n");
    if (disk == NULL) {
        //return -ENOENT;                                 // ERROR: file not opened successfully
        printf("init_bitmap DISK IS NULL\n");
    } else {
        // allocate the needed space for map
        map = calloc(MAP_SIZE, sizeof(bitmap));
        printf("init_bitmap ALLOCATED SPACE FOR map\n");
        printf("init_bitmap map[0]=%s\n", byte_to_binary(map[0]));

        // get the bitmap from the disk file
        fseek(disk, -(3*BLOCK_SIZE), SEEK_END);         // bitmap held in last three blocks of file
        fread(map, MAP_SIZE, 1, disk);                // pull this block as a directory entry
        printf("init_bitmap GRABBED BITMAP FROM LAST 3 BLOCKS ON DISK\n");

        // set certain regions of the bitmap to USED
        set_bit(0);                                // root
        printf("init_bitmap SET map AT BIT %d TO 1\n", 0);
        printf("init_bitmap map[0]=%s\n", byte_to_binary(map[0]));

        printf("init_bitmap map[%d]=%s\n", 1279, byte_to_binary(map[1279]));
        set_bit(MAX_BITS-3);                       // bitmap start
        set_bit(MAX_BITS-2);                       // bitmap mid
        set_bit(MAX_BITS-1);                       // bitmap end
        printf("init_bitmap map[%d]=%s\n", 1279, byte_to_binary(map[1279]));

        printf("init_bitmap DISK IS %p\n", disk);
        printf("init_bitmap INIT_BITMAP CLOSING DISK...\n");
        printf("init_bitmap map[0]=%s\n", byte_to_binary(map[0]));
        
        printf("set_bit BEFORE fclose() MAP AS POINTER= %p\n", map);
        fclose(disk);
        printf("set_bit AFTER fclose() MAP AS POINTER= %p\n", map);

        printf("init_bitmap CLOSED DISK\n");                                  // close the file
        //printf("init_bitmap map[0]=%d\n", map[0]);
    }

    printf("              -----------------------------------------------END init_bitmap\n");
}

/*
    Sets the bit at the given index to '1' (aka USED)
*/
void set_bit(int index) {
    printf("START set_bit-------------------------------------------------------------\n");
    if (map == NULL) { init_bitmap(map); }  // make sure bitmap is initialized
    printf("set_bit MAP AS POINTER= %p\n", map);
    printf("set_bit GET_BM_INDEX(%d)=%d, GET_BIT_OFFSET(%d)=%d\n", index, GET_BM_INDEX(index), index, GET_BIT_OFFSET(index));
    printf("set_bit (1 << GET_BIT_OFFSET(%d) = %s\n", index, byte_to_binary(1 << GET_BIT_OFFSET(index)));
    map[GET_BM_INDEX(index)] |= (1 << GET_BIT_OFFSET(index));
    printf("              -----------------------------------------------END set_bit\n");
}

/*
    Sets the bit at the given index to '0' (aka FREE).
*/
void clear_bit(int index) {
    printf("START clear_bit-------------------------------------------------------------\n");
    if (map == NULL) { init_bitmap(map); }  // make sure bitmap is initialized

    map[GET_BM_INDEX(index)] &= ~(1 << GET_BIT_OFFSET(index));
    printf("              -----------------------------------------------END clear_bit\n");
}

/*
    Gets the bit at the given index.
*/
int get_bit(int index) {
    printf("START get_bit-------------------------------------------------------------\n");
    printf("get_bit MAP AS POINTER= %p\n", map);
    printf("(1) GET_BIT map's POINTER VALUE: %p\n", map);
    if (map == NULL) { init_bitmap(map); }  // make sure bitmap is initialized
    printf("(2) GET_BIT map's POINTER VALUE: %p\n", map);

    printf("get_bit GETTING BIT FOR DISK INDEX %d, AKA MAP INDEX %d\n", index, GET_BM_INDEX(index));

    //printf("get_bit map's pointer: %p", map);
    //printf("get_bit map[0]=%d\n", map[0]);

    printf("get_bit GET_BM_INDEX(%d)=%d\n", index, GET_BM_INDEX(index));
    printf("get_bit GET_BIT_OFFSET(%d)=%d\n", index, GET_BIT_OFFSET(index));
    printf("get_bit map[GET_BM_INDEX(%d)]=%d\n", index, map[GET_BM_INDEX(index)]);
    printf("(1 << GET_BIT_OFFSET(%d)) = %d\n", index, (1 << GET_BIT_OFFSET(index)));
    bitmap bit = map[GET_BM_INDEX(index)] & (1 << GET_BIT_OFFSET(index));

    printf("get_bit bit=%d\n", bit);
    printf("get_bit bit=%s\n", byte_to_binary(bit));

    printf("              -----------------------------------------------END get_bit\n");

    return bit != 0;
}

/*
    Search for the first block that is empty and return it. Disregard
    block zero (0) and the last three blocks because it is the root block
    and the blocks used to store the bitmap, respectively.
*/
int find_free_block() {
    printf("START find_free_block-------------------------------------------------------------\n");

    if (map == NULL) { 
        printf("CALLING init_bitmap...\n");
        init_bitmap(map);
    }  // make sure bitmap iswialized

    printf("find_free_block MAP AS POINTER= %p\n", map);
    //return 1;

    int index = 1;                          // skip block 0 aka ROOT
    while (index < MAX_BITS-3) {            // (MAX-3) to skip where bitmap is stored
        int bit = get_bit(index);      // get next bit
        printf("find_free_block index=%d, bit=%d", index, bit);
        if (bit == 0) { 
            printf("              -----------------------------------------------END find_free_block\n");
            return index; 
        }     // found a free block

        index++;
    }
    printf("              -----------------------------------------------END find_free_block\n");
    return -1;                              // no free blocks available
}


/*
    Takes the given bitmap and writes it out to the DISK.
*/
void write_bitmap() {
    printf("START write_bitmap-------------------------------------------------------------\n");
    printf("write_bitmap POINTER VALUE OF MAP:%p\n", map);

    if (map == NULL) { init_bitmap(map); }      // make sure bitmap is initialized
    printf("write_bitmap [%s]\n", byte_to_binary(map[0]));

    FILE *disk = fopen(DISK, "r+b");            // open file read/write with respect to binary mode

    if (disk == NULL) {
        //return -ENOENT;                       // ERROR: file not opened successfully
        
    } else {
        // write the bitmap to the disk file
        int res;
        res = fseek(disk, -(3*BLOCK_SIZE), SEEK_END); // bitmap held in last three blocks of file
        if (res < 0) { printf("write_bitmap ERROR WITH fseek\n"); }

        res = fwrite(map, MAP_SIZE, 1, disk);         // write bitmap to disk
        if (res < 0) { printf("write_bitmap ERROR WITH fwrite\n"); }

        printf("write_bitmap CLOSING DISK FILE\n");
        res = fclose(disk);                           // close the file
        if (res < 0) { printf("write_bitmap ERROR WITH CLOSING DISK\n"); }
    }

    printf("              -----------------------------------------------END write_bitmap\n");
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
//         set_bit(MAX_BITS-3);                               // bitmap start
//         set_bit(MAX_BITS-2);                               // bitmap mid
//         set_bit(MAX_BITS-1);                                 // bitmap end

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