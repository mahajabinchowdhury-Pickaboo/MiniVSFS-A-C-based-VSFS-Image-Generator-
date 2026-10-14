// Build: gcc -O2 -std=c17 -Wall -Wextra mkfs_adder.c -o mkfs_adder
#define _GNU_SOURCE
#define _FILE_OFFSET_BITS 64
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <time.h>
#include <getopt.h>
#include <sys/stat.h>
#include <unistd.h>

#define BS 4096u
#define INODE_SIZE 128u
#define ROOT_INO 1u
#define DIRECT_MAX 12
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

    uint8_t  checksum;
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
    fprintf(stderr, "Usage: %s --input <input.img> --output <output.img> --file <filename>\n", prog_name);
    fprintf(stderr, "  --input: input image filename\n");
    fprintf(stderr, "  --output: output image filename\n");
    fprintf(stderr, "  --file: file to add to filesystem\n");
}


int find_free_bit(const uint8_t* bitmap, int max_bits) {
    for (int byte_idx = 0; byte_idx < (max_bits + 7) / 8; byte_idx++) {
        if (bitmap[byte_idx] != 0xFF) {
            for (int bit_idx = 0; bit_idx < 8; bit_idx++) {
                int bit_pos = byte_idx * 8 + bit_idx;
                if (bit_pos >= max_bits) return -1;
                if (!(bitmap[byte_idx] & (1 << bit_idx))) {
                    return bit_pos;
                }
            }
        }
    }
    return -1;
}

void set_bit(uint8_t* bitmap, int bit_pos) {
    int byte_idx = bit_pos / 8;
    int bit_idx = bit_pos % 8;
    bitmap[byte_idx] |= (1 << bit_idx);
}

int main(int argc, char *argv[]) {
    crc32_init();
    
    char *input_filename = NULL;
    char *output_filename = NULL;
    char *file_to_add = NULL;
    
    static struct option long_options[] = {
        {"input", required_argument, 0, 'i'},
        {"output", required_argument, 0, 'o'},
        {"file", required_argument, 0, 'f'},
        {0, 0, 0, 0}
    };
    
    int c;
    int option_index = 0;
    
    while ((c = getopt_long(argc, argv, "i:o:f:", long_options, &option_index)) != -1) {
        switch (c) {
            case 'i':
                input_filename = optarg;
                break;
            case 'o':
                output_filename = optarg;
                break;
            case 'f':
                file_to_add = optarg;
                break;
            case '?':
                print_usage(argv[0]);
                return 1;
            default:
                print_usage(argv[0]);
                return 1;
        }
    }
    
    if (!input_filename || !output_filename || !file_to_add) {
        fprintf(stderr, "Error: Missing required arguments\n");
        print_usage(argv[0]);
        return 1;
    }
    
    struct stat file_stat;
    if (stat(file_to_add, &file_stat) != 0) {
        fprintf(stderr, "Error: File '%s' not found: %s\n", file_to_add, strerror(errno));
        return 1;
    }
    
    if (!S_ISREG(file_stat.st_mode)) {
        fprintf(stderr, "Error: '%s' is not a regular file\n", file_to_add);
        return 1;
    }
    
    uint64_t file_size = file_stat.st_size;
    uint64_t blocks_needed = (file_size + BS - 1) / BS;
    if (blocks_needed > DIRECT_MAX) {
        fprintf(stderr, "Error: File too large. Maximum supported size is %d bytes (%d blocks)\n", 
                DIRECT_MAX * BS, DIRECT_MAX);
        return 1;
    }
    
    FILE *input_fp = fopen(input_filename, "rb");
    if (!input_fp) {
        fprintf(stderr, "Error: Could not open input file '%s': %s\n", 
                input_filename, strerror(errno));
        return 1;
    }
    
    superblock_t superblock;
    if (fread(&superblock, sizeof(superblock_t), 1, input_fp) != 1) {
        fprintf(stderr, "Error: Could not read superblock\n");
        fclose(input_fp);
        return 1;
    }
    
    if (superblock.magic != 0x4D565346) {
        fprintf(stderr, "Error: Invalid filesystem magic number\n");
        fclose(input_fp);
        return 1;
    }
    
    uint64_t total_size = superblock.total_blocks * BS;
    uint8_t *fs_image = malloc(total_size);
    if (!fs_image) {
        fprintf(stderr, "Error: Memory allocation failed\n");
        fclose(input_fp);
        return 1;
    }
    
    fseek(input_fp, 0, SEEK_SET);
    if (fread(fs_image, total_size, 1, input_fp) != 1) {
        fprintf(stderr, "Error: Could not read filesystem image\n");
        free(fs_image);
        fclose(input_fp);
        return 1;
    }
    fclose(input_fp);
    
    uint8_t *inode_bitmap = fs_image + superblock.inode_bitmap_start * BS;
    uint8_t *data_bitmap = fs_image + superblock.data_bitmap_start * BS;
    inode_t *inode_table = (inode_t*)(fs_image + superblock.inode_table_start * BS);
    uint8_t *data_region = fs_image + superblock.data_region_start * BS;
    
    int free_inode_idx = find_free_bit(inode_bitmap, superblock.inode_count);
    if (free_inode_idx == -1) {
        fprintf(stderr, "Error: No free inodes available\n");
        free(fs_image);
        return 1;
    }
    
    uint32_t allocated_blocks[DIRECT_MAX];
    int allocated_count = 0;
    
    for (uint64_t i = 0; i < blocks_needed; i++) {
        int free_block_idx = find_free_bit(data_bitmap, superblock.data_region_blocks);
        if (free_block_idx == -1) {
            fprintf(stderr, "Error: Not enough free data blocks\n");
            free(fs_image);
            return 1;
        }
        allocated_blocks[allocated_count++] = superblock.data_region_start + free_block_idx;
        set_bit(data_bitmap, free_block_idx);
    }
    
    FILE *file_fp = fopen(file_to_add, "rb");
    if (!file_fp) {
        fprintf(stderr, "Error: Could not open file '%s': %s\n", 
                file_to_add, strerror(errno));
        free(fs_image);
        return 1;
    }
    
    for (int i = 0; i < allocated_count; i++) {
        uint64_t block_offset = (allocated_blocks[i] - superblock.data_region_start) * BS;
        uint8_t *block_ptr = data_region + block_offset;
        
        size_t to_read = BS;
        if (i == allocated_count - 1) {
            to_read = file_size - (i * BS);
        }
        
        memset(block_ptr, 0, BS); 
        if (fread(block_ptr, to_read, 1, file_fp) != 1 && to_read > 0) {
            fprintf(stderr, "Error: Could not read file data\n");
            fclose(file_fp);
            free(fs_image);
            return 1;
        }
    }
    fclose(file_fp);
    
    time_t now = time(NULL);
    inode_t *new_inode = &inode_table[free_inode_idx];
    memset(new_inode, 0, sizeof(inode_t));
    
    new_inode->mode = 0100000;  
    new_inode->links = 1;       
    new_inode->uid = 0;
    new_inode->gid = 0;
    new_inode->size_bytes = file_size;
    new_inode->atime = now;
    new_inode->mtime = file_stat.st_mtime;
    new_inode->ctime = now;
    
    for (int i = 0; i < allocated_count; i++) {
        new_inode->direct[i] = allocated_blocks[i];
    }
    for (int i = allocated_count; i < DIRECT_MAX; i++) {
        new_inode->direct[i] = 0; 
    }
    
    new_inode->reserved_0 = 0;
    new_inode->reserved_1 = 0;
    new_inode->reserved_2 = 0;
    new_inode->proj_id = 2;
    new_inode->uid16_gid16 = 0;
    new_inode->xattr_ptr = 0;
    
    set_bit(inode_bitmap, free_inode_idx);
    
    inode_t *root_inode = &inode_table[0]; 
    uint64_t root_data_block = root_inode->direct[0] - superblock.data_region_start;
    dirent64_t *root_dir_entries = (dirent64_t*)(data_region + root_data_block * BS);
    
    int entries_per_block = BS / sizeof(dirent64_t);
    dirent64_t *free_entry = NULL;
    
    for (int i = 0; i < entries_per_block; i++) {
        if (root_dir_entries[i].inode_no == 0) {
            free_entry = &root_dir_entries[i];
            break;
        }
    }
    
    if (!free_entry) {
        fprintf(stderr, "Error: No free directory entries in root directory\n");
        free(fs_image);
        return 1;
    }
    
    char *filename = strrchr(file_to_add, '/');
    char *filename_win = strrchr(file_to_add, '\\');
    
    if (filename_win > filename) {
        filename = filename_win;
    }
    
    if (filename) {
        filename++; 
    } else {
        filename = file_to_add;
    }
    
    if (strlen(filename) >= 58) {
        fprintf(stderr, "Error: Filename too long (max 57 characters)\n");
        free(fs_image);
        return 1;
    }
    
    for (int i = 0; i < entries_per_block; i++) {
        if (root_dir_entries[i].inode_no != 0 && 
            strcmp(root_dir_entries[i].name, filename) == 0) {
            fprintf(stderr, "Error: File '%s' already exists in filesystem\n", filename);
            free(fs_image);
            return 1;
        }
    }
    
    free_entry->inode_no = free_inode_idx + 1; 
    free_entry->type = 1; 
    strcpy(free_entry->name, filename);
    
    root_inode->size_bytes += sizeof(dirent64_t);
    root_inode->links++; 
    root_inode->mtime = now;
    root_inode->ctime = now;
    
    inode_crc_finalize(new_inode);
    inode_crc_finalize(root_inode);
    dirent_checksum_finalize(free_entry);
    
    superblock_t *sb = (superblock_t*)fs_image;
    superblock_crc_finalize(sb);
    
    FILE *output_fp = fopen(output_filename, "wb");
    if (!output_fp) {
        fprintf(stderr, "Error: Could not create output file '%s': %s\n", 
                output_filename, strerror(errno));
        free(fs_image);
        return 1;
    }
    
    if (fwrite(fs_image, total_size, 1, output_fp) != 1) {
        fprintf(stderr, "Error: Could not write output file\n");
        fclose(output_fp);
        free(fs_image);
        return 1;
    }
    
    fclose(output_fp);
    free(fs_image);
    printf("Successfully added file '%s' to filesystem\n", filename);
    printf("  File size: %lu bytes (%lu blocks)\n", file_size, blocks_needed);
    printf("  Inode number: %d\n", free_inode_idx + 1);
    printf("  Output image: %s\n", output_filename);
    
    return 0;
}