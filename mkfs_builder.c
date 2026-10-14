// Build: gcc -O2 -std=c17 -Wall -Wextra mkfs_minivsfs.c -o mkfs_builder
#define _GNU_SOURCE
#define _FILE_OFFSET_BITS 64
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <inttypes.h>
#include <errno.h>
#include <time.h>
#include <assert.h>
#include <getopt.h>
#include <unistd.h>

#define BS 4096u             
#define INODE_SIZE 128u
#define ROOT_INO 1u
#define DIRECT_MAX 12

uint64_t g_random_seed = 0; 

#pragma pack(push, 1)
typedef struct {
    uint32_t magic;              
    uint32_t version;            
    uint32_t block_size;         
    uint64_t total_blocks;
    uint64_t inode_count;
    uint64_t inode_bitmap_start; 
    uint64_t inode_bitmap_blocks; 
    uint64_t data_bitmap_start;  
    uint64_t data_bitmap_blocks; 
    uint64_t inode_table_start; 
    uint64_t inode_table_blocks;
    uint64_t data_region_start;
    uint64_t data_region_blocks;
    uint64_t root_inode;         
    uint64_t mtime_epoch;
    uint32_t flags;              
    
    uint32_t checksum;            
} superblock_t;
#pragma pack(pop)
_Static_assert(sizeof(superblock_t) == 116, "superblock must fit in one block");

#pragma pack(push,1)
typedef struct {
    uint16_t mode;               
    uint16_t links;
    uint32_t uid;                
    uint32_t gid;                
    uint64_t size_bytes;
    uint64_t atime;              
    uint64_t mtime;            
    uint64_t ctime;              
    uint32_t direct[DIRECT_MAX]; 
    uint32_t reserved_0;         
    uint32_t reserved_1;         
    uint32_t reserved_2;         
    uint32_t proj_id;            
    uint32_t uid16_gid16;        
    uint64_t xattr_ptr;          

    uint64_t inode_crc;   
} inode_t;
#pragma pack(pop)
_Static_assert(sizeof(inode_t)==INODE_SIZE, "inode size mismatch");

#pragma pack(push,1)
typedef struct {
    uint32_t inode_no;           
    uint8_t type;               
    char name[58];               
    uint8_t checksum;            
} dirent64_t;
#pragma pack(pop)
_Static_assert(sizeof(dirent64_t)==64, "dirent size mismatch");

// ==========================DO NOT CHANGE THIS PORTION=========================
// These functions are there for your help. You should refer to the specifications to see how you can use them.
// ====================================CRC32====================================
uint32_t CRC32_TAB[256];
void crc32_init(void){
    for (uint32_t i=0;i<256;i++){
        uint32_t c=i;
        for(int j=0;j<8;j++) c = (c&1)?(0xEDB88320u^(c>>1)):(c>>1);
        CRC32_TAB[i]=c;
    }
}
uint32_t crc32(const void* data, size_t n){
    const uint8_t* p=(const uint8_t*)data; uint32_t c=0xFFFFFFFFu;
    for(size_t i=0;i<n;i++) c = CRC32_TAB[(c^p[i])&0xFF] ^ (c>>8);
    return c ^ 0xFFFFFFFFu;
}
// ====================================CRC32====================================

// WARNING: CALL THIS ONLY AFTER ALL OTHER SUPERBLOCK ELEMENTS HAVE BEEN FINALIZED
static uint32_t superblock_crc_finalize(superblock_t *sb) {
    sb->checksum = 0;
    uint32_t s = crc32((void *) sb, BS - 4);
    sb->checksum = s;
    return s;
}

// WARNING: CALL THIS ONLY AFTER ALL OTHER SUPERBLOCK ELEMENTS HAVE BEEN FINALIZED
void inode_crc_finalize(inode_t* ino){
    uint8_t tmp[INODE_SIZE]; memcpy(tmp, ino, INODE_SIZE);
    memset(&tmp[120], 0, 8);
    uint32_t c = crc32(tmp, 120);
    ino->inode_crc = (uint64_t)c;
}

// WARNING: CALL THIS ONLY AFTER ALL OTHER SUPERBLOCK ELEMENTS HAVE BEEN FINALIZED
void dirent_checksum_finalize(dirent64_t* de) {
    const uint8_t* p = (const uint8_t*)de;
    uint8_t x = 0;
    for (int i = 0; i < 63; i++) x ^= p[i];   
    de->checksum = x;
}

void print_usage(const char* prog_name) {
    fprintf(stderr, "Usage: %s --image <output.img> --size-kib <180..4096> --inodes <128..512>\n", prog_name);
    fprintf(stderr, "  --image: output image filename\n");
    fprintf(stderr, "  --size-kib: total size in kilobytes (multiple of 4, range 180-4096)\n");
    fprintf(stderr, "  --inodes: number of inodes (range 128-512)\n");
}

int main(int argc, char *argv[]) {
    crc32_init();
    char *image_filename = NULL;
    int size_kib = 0;
    int inode_count = 0;
    
    static struct option long_options[] = {
        {"image", required_argument, 0, 'i'},
        {"size-kib", required_argument, 0, 's'},
        {"inodes", required_argument, 0, 'n'},
        {0, 0, 0, 0}
    };
    
    int c;
    int option_index = 0;
    
    while ((c = getopt_long(argc, argv, "i:s:n:", long_options, &option_index)) != -1) {
        switch (c) {
            case 'i':
                image_filename = optarg;
                break;
            case 's':
                size_kib = atoi(optarg);
                break;
            case 'n':
                inode_count = atoi(optarg);
                break;
            case '?':
                print_usage(argv[0]);
                return 1;
            default:
                print_usage(argv[0]);
                return 1;
        }
    }

    if (!image_filename || size_kib == 0 || inode_count == 0) {
        fprintf(stderr, "Error: Missing required arguments\n");
        print_usage(argv[0]);
        return 1;
    }
    
    if (size_kib < 180 || size_kib > 4096 || size_kib % 4 != 0) {
        fprintf(stderr, "Error: size-kib must be between 180-4096 and multiple of 4\n");
        return 1;
    }
    
    if (inode_count < 128 || inode_count > 512) {
        fprintf(stderr, "Error: inodes must be between 128-512\n");
        return 1;
    }
    

    uint64_t total_blocks = (uint64_t)size_kib * 1024 / BS;
    uint64_t inode_table_blocks = (inode_count * INODE_SIZE + BS - 1) / BS;
    uint64_t data_region_start = 3 + inode_table_blocks; 
    uint64_t data_region_blocks = total_blocks - data_region_start;
    
    if (data_region_blocks <= 0) {
        fprintf(stderr, "Error: Not enough space for data region\n");
        return 1;
    }
    

    time_t now = time(NULL);
    if (now == -1) {
        fprintf(stderr, "Error: Could not get current time\n");
        return 1;
    }
    

    superblock_t superblock = {0};
    superblock.magic = 0x4D565346;
    superblock.version = 1;
    superblock.block_size = BS;
    superblock.total_blocks = total_blocks;
    superblock.inode_count = inode_count;
    superblock.inode_bitmap_start = 1;
    superblock.inode_bitmap_blocks = 1;
    superblock.data_bitmap_start = 2;
    superblock.data_bitmap_blocks = 1;
    superblock.inode_table_start = 3;
    superblock.inode_table_blocks = inode_table_blocks;
    superblock.data_region_start = data_region_start;
    superblock.data_region_blocks = data_region_blocks;
    superblock.root_inode = ROOT_INO;
    superblock.mtime_epoch = now;
    superblock.flags = 0;
    

    inode_t root_inode = {0};
    root_inode.mode = 0040000; 
    root_inode.links = 2;      
    root_inode.uid = 0;
    root_inode.gid = 0;
    root_inode.size_bytes = 2 * sizeof(dirent64_t); 
    root_inode.atime = now;
    root_inode.mtime = now;
    root_inode.ctime = now;
    root_inode.direct[0] = data_region_start; 
    for (int i = 1; i < DIRECT_MAX; i++) {
        root_inode.direct[i] = 0; 
    }
    root_inode.reserved_0 = 0;
    root_inode.reserved_1 = 0;
    root_inode.reserved_2 = 0;
    root_inode.proj_id = 2; // Setting our group ID here
    root_inode.uid16_gid16 = 0;
    root_inode.xattr_ptr = 0;
    

    dirent64_t dot_entry = {0};
    dot_entry.inode_no = ROOT_INO;
    dot_entry.type = 2; 
    strcpy(dot_entry.name, ".");
    
    dirent64_t dotdot_entry = {0};
    dotdot_entry.inode_no = ROOT_INO;
    dotdot_entry.type = 2; 
    strcpy(dotdot_entry.name, "..");
    

    FILE *fp = fopen(image_filename, "wb");
    if (!fp) {
        fprintf(stderr, "Error: Could not create output file '%s': %s\n", 
                image_filename, strerror(errno));
        return 1;
    }

    uint8_t *block_buffer = calloc(1, BS);
    if (!block_buffer) {
        fprintf(stderr, "Error: Memory allocation failed\n");
        fclose(fp);
        return 1;
    }
    
    superblock_crc_finalize(&superblock);
    memcpy(block_buffer, &superblock, sizeof(superblock));
    if (fwrite(block_buffer, BS, 1, fp) != 1) {
        fprintf(stderr, "Error: Failed to write superblock\n");
        free(block_buffer);
        fclose(fp);
        return 1;
    }
    

    memset(block_buffer, 0, BS);
    block_buffer[0] = 0x01; 
    if (fwrite(block_buffer, BS, 1, fp) != 1) {
        fprintf(stderr, "Error: Failed to write inode bitmap\n");
        free(block_buffer);
        fclose(fp);
        return 1;
    }
    
    memset(block_buffer, 0, BS);
    block_buffer[0] = 0x01; 
    if (fwrite(block_buffer, BS, 1, fp) != 1) {
        fprintf(stderr, "Error: Failed to write data bitmap\n");
        free(block_buffer);
        fclose(fp);
        return 1;
    }

    for (uint64_t i = 0; i < inode_table_blocks; i++) {
        memset(block_buffer, 0, BS);
        
        if (i == 0) {
            inode_crc_finalize(&root_inode);
            memcpy(block_buffer, &root_inode, sizeof(inode_t));
        }

        
        if (fwrite(block_buffer, BS, 1, fp) != 1) {
            fprintf(stderr, "Error: Failed to write inode table block %lu\n", i);
            free(block_buffer);
            fclose(fp);
            return 1;
        }
    }
    

    for (uint64_t i = 0; i < data_region_blocks; i++) {
        memset(block_buffer, 0, BS);
        
        if (i == 0) {
            dirent_checksum_finalize(&dot_entry);
            dirent_checksum_finalize(&dotdot_entry);
            memcpy(block_buffer, &dot_entry, sizeof(dirent64_t));
            memcpy(block_buffer + sizeof(dirent64_t), &dotdot_entry, sizeof(dirent64_t));
        }
    
        
        if (fwrite(block_buffer, BS, 1, fp) != 1) {
            fprintf(stderr, "Error: Failed to write data block %lu\n", i);
            free(block_buffer);
            fclose(fp);
            return 1;
        }
    }
    
    free(block_buffer);
    fclose(fp);
    printf("Successfully created MiniVSFS image: %s\n", image_filename);
    printf("  Size: %d KiB (%lu blocks)\n", size_kib, total_blocks);
    printf("  Inodes: %d\n", inode_count);
    
    return 0;
}