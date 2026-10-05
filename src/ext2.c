#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "header/stdlib/string.h"
#include "header/filesystem/ext2.h"
#include "header/disk.h"

const uint8_t fs_signature[BLOCK_SIZE] = {
    'C',
    'o',
    'u',
    'r',
    's',
    'e',
    ' ',
    ' ',
    ' ',
    ' ',
    ' ',
    ' ',
    ' ',
    ' ',
    ' ',
    ' ',
    'D',
    'e',
    's',
    'i',
    'g',
    'n',
    'e',
    'd',
    ' ',
    'b',
    'y',
    ' ',
    ' ',
    ' ',
    ' ',
    ' ',
    'L',
    'a',
    'b',
    ' ',
    'S',
    'i',
    's',
    't',
    'e',
    'r',
    ' ',
    'I',
    'T',
    'B',
    ' ',
    ' ',
    'M',
    'a',
    'd',
    'e',
    ' ',
    'w',
    'i',
    't',
    'h',
    ' ',
    '<',
    '3',
    ' ',
    ' ',
    ' ',
    ' ',
    '-',
    '-',
    '-',
    '-',
    '-',
    '-',
    '-',
    '-',
    '-',
    '-',
    '-',
    '2',
    '0',
    '2',
    '5',
    '\n',
    [BLOCK_SIZE - 2] = 'O',
    [BLOCK_SIZE - 1] = 'k',
};

static struct EXT2Superblock sb;
static struct EXT2BlockGroupDescriptorTable bgd_table;

#define SUPERBLOCK_BLOCK 1u
#define BGD_TABLE_BLOCK 2u
#define GROUP0_META_START 3u

char *get_entry_name(void *entry)
{
    return (char *)entry + sizeof(struct EXT2DirectoryEntry);
}

/**
 * get the directory entry from the buffer
 * @param ptr the buffer that contains the directory table
 * @param offset the offset of the entry
 * @return the directory entry
 */
struct EXT2DirectoryEntry *get_directory_entry(void *ptr, uint32_t offset)
{
    return (struct EXT2DirectoryEntry *)((uint8_t *)ptr + offset);
}

/**
 * get the next directory entry from the current entry
 * @param entry the current entry
 * @return the next directory entry
 */
struct EXT2DirectoryEntry *get_next_directory_entry(struct EXT2DirectoryEntry *entry)
{
    return get_directory_entry(entry, entry->rec_len);
}

/**
 * get the record length of the entry
 * @param name_len the length of the name of the entry
 * @return the record length of the entry
 */
uint16_t get_entry_record_len(uint8_t name_len)
{
    uint32_t len = sizeof(struct EXT2DirectoryEntry) + name_len;
    return (uint16_t)((len + 3u) & ~3u);
}

/**
 * get the offset of the first child of the directory
 * @param ptr the buffer that contains the directory table
 * @return the offset of the first child of the directory
 */
uint32_t get_dir_first_child_offset(void *ptr)
{
    struct EXT2DirectoryEntry *dot = get_directory_entry(ptr, 0);
    struct EXT2DirectoryEntry *dotdot = get_next_directory_entry(dot);
    return (uint32_t)((uint8_t *)dotdot - (uint8_t *)ptr) + dotdot->rec_len;
}

/* =================== MAIN FUNCTION OF EXT32 FILESYSTEM ============================*/

/**
 * @brief get bgd index from inode, inode will starts at index 1
 * @param inode 1 to INODES_PER_GROUP * GROUP_COUNT
 * @return bgd index (0 to GROUP_COUNT - 1)
 */
uint32_t inode_to_bgd(uint32_t inode)
{
    return (inode - 1) / INODES_PER_GROUP;
}

/**
 * @brief get inode local index in the corrresponding bgd
 * @param inode 1 to INODES_PER_GROUP * GROUP_COUNT
 * @return local index
 */
uint32_t inode_to_local(uint32_t inode)
{
    return (inode - 1) % INODES_PER_GROUP;
}

uint32_t allocate_node(void)
{
    uint8_t bitmap[BLOCK_SIZE];
    uint8_t buf[BLOCK_SIZE];

    for (uint32_t g = 0; g < GROUPS_COUNT; g++)
    {
        struct EXT2BlockGroupDescriptor *bgd = &bgd_table.table[g];
        if (bgd->bg_free_inodes_count == 0)
        {
            continue;
        }

        read_blocks(bitmap, bgd->bg_inode_bitmap, 1);
        uint32_t bit = 0;
        while (bit < INODES_PER_GROUP && (bitmap[bit / 8] & (1u << (bit % 8))))
        {
            bit++;
        }
        if (bit == INODES_PER_GROUP)
        {
            continue;
        }

        bitmap[bit / 8] |= (uint8_t)(1u << (bit % 8));
        write_blocks(bitmap, bgd->bg_inode_bitmap, 1);

        bgd->bg_free_inodes_count--;
        sb.s_free_inodes_count--;

        memset(buf, 0, BLOCK_SIZE);
        memcpy(buf, &sb, sizeof(sb));
        write_blocks(buf, SUPERBLOCK_BLOCK, 1);
        memset(buf, 0, BLOCK_SIZE);
        memcpy(buf, &bgd_table, sizeof(bgd_table));
        write_blocks(buf, BGD_TABLE_BLOCK, 1);

        return g * INODES_PER_GROUP + bit + 1;
    }
    return 0;
}

/**
 * @brief deallocate node from the disk, will also deallocate its used blocks
 * also all of the blocks of indirect blocks if necessary
 * @param inode that needs to be deallocated
 */
void deallocate_node(uint32_t inode);

/**
 * @brief deallocate node blocks
 * @param locations node->block
 * @param blocks number of blocks
 */
void deallocate_blocks(void *loc, uint32_t blocks);

/**
 * @brief deallocate block from the disk
 * @param locations block locations
 * @param blocks number of blocks
 * @param bitmap block bitmap
 * @param depth depth of the block
 * @param last_bgd last bgd that is used
 * @param bgd_loaded whether bgd is loaded or not
 * @return new last bgd
 */
uint32_t deallocate_block(uint32_t *locations, uint32_t blocks, struct BlockBuffer *bitmap, uint32_t depth, uint32_t *last_bgd, bool bgd_loaded);

/**
 * @brief write node->block in the given node, will allocate
 * at least node->blocks number of blocks, if first 12 item of node-> block
 * is not enough, will use indirect blocks
 * @param ptr the buffer that needs to be written
 * @param node pointer of the node
 * @param preffered_bgd it is located at the node inode bgd
 *
 * @attention only implement until doubly indirect block, if you want to implement triply indirect block please increase the storage size to at least 256MB
 */
void allocate_node_blocks(void *ptr, struct EXT2Inode *node, uint32_t prefered_bgd)
{
    const uint8_t *src = (const uint8_t *)ptr;
    uint8_t bitmap[BLOCK_SIZE];
    uint8_t buf[BLOCK_SIZE];

    for (uint32_t i = 0; i < node->i_blocks && i < 12; i++)
    {
        for (uint32_t k = 0; k < GROUPS_COUNT; k++)
        {
            uint32_t g = (prefered_bgd + k) % GROUPS_COUNT;
            struct EXT2BlockGroupDescriptor *bgd = &bgd_table.table[g];
            if (bgd->bg_free_blocks_count == 0)
            {
                continue;
            }

            /* cari bit 0 pertama di block bitmap */
            read_blocks(bitmap, bgd->bg_block_bitmap, 1);
            uint32_t bit = 0;
            while (bit < BLOCKS_PER_GROUP && (bitmap[bit / 8] & (1u << (bit % 8))))
            {
                bit++;
            }
            if (bit == BLOCKS_PER_GROUP)
            {
                continue;
            }

            bitmap[bit / 8] |= (uint8_t)(1u << (bit % 8));
            write_blocks(bitmap, bgd->bg_block_bitmap, 1);
            bgd->bg_free_blocks_count--;
            sb.s_free_blocks_count--;

            node->i_block[i] = g * BLOCKS_PER_GROUP + bit;
            write_blocks(src + i * BLOCK_SIZE, node->i_block[i], 1);
            break;
        }
    }
    /* TODO: i_blocks > 12 -> pakai i_block[12] (indirect) dan i_block[13] (doubly indirect) */

    memset(buf, 0, BLOCK_SIZE);
    memcpy(buf, &sb, sizeof(sb));
    write_blocks(buf, SUPERBLOCK_BLOCK, 1);
    memset(buf, 0, BLOCK_SIZE);
    memcpy(buf, &bgd_table, sizeof(bgd_table));
    write_blocks(buf, BGD_TABLE_BLOCK, 1);
}

/**
 * @brief update the node to the disk
 * @param node pointer of node
 * @param inode location of the node
 */
void sync_node(struct EXT2Inode *node, uint32_t inode)
{
    uint8_t buf[BLOCK_SIZE];
    uint32_t local = inode_to_local(inode);
    uint32_t block = bgd_table.table[inode_to_bgd(inode)].bg_inode_table + local / INODES_PER_TABLE;
    uint32_t offset = (local % INODES_PER_TABLE) * INODE_SIZE;

    read_blocks(buf, block, 1);
    memcpy(buf + offset, node, INODE_SIZE);
    write_blocks(buf, block, 1);
}

/**
 * @brief create a new directory using given node
 * first item of directory table is its node location (name will be .)
 * second item of directory is its parent location (name will be ..)
 * @param node pointer of inode
 * @param inode inode that already allocated
 * @param parent_inode inode of parent directory (if root directory, the parent is itself)
 */
void init_directory_table(struct EXT2Inode *node, uint32_t inode, uint32_t parent_inode)
{
    uint8_t buf[BLOCK_SIZE];
    memset(buf, 0, BLOCK_SIZE);

    /* entry 0: "." -> dirinya sendiri */
    struct EXT2DirectoryEntry *dot = get_directory_entry(buf, 0);
    dot->inode = inode;
    dot->name_len = 1;
    dot->file_type = EXT2_FT_DIR;
    dot->rec_len = get_entry_record_len(1);
    get_entry_name(dot)[0] = '.';

    /* entry 1: ".." -> parent (root: parent = dirinya sendiri) */
    struct EXT2DirectoryEntry *dotdot = get_next_directory_entry(dot);
    dotdot->inode = parent_inode;
    dotdot->name_len = 2;
    dotdot->file_type = EXT2_FT_DIR;
    dotdot->rec_len = get_entry_record_len(2);
    get_entry_name(dotdot)[0] = '.';
    get_entry_name(dotdot)[1] = '.';

    /* entry 2: slot kosong (inode = 0) yang memakan sisa block,
     * supaya iterasi lewat rec_len tidak pernah macet di rec_len = 0 */
    struct EXT2DirectoryEntry *first_child = get_next_directory_entry(dotdot);
    first_child->inode = 0;
    first_child->rec_len = (uint16_t)(BLOCK_SIZE - (uint32_t)((uint8_t *)first_child - buf));

    memset(node, 0, sizeof(*node));
    node->i_mode = EXT2_S_IFDIR;
    node->i_size = BLOCK_SIZE;
    node->i_blocks = 1;
    allocate_node_blocks(buf, node, inode_to_bgd(inode));

    /* allocate_node_blocks sudah menyimpan BGD table; tambah used_dirs lalu simpan lagi */
    bgd_table.table[inode_to_bgd(inode)].bg_used_dirs_count++;
    memset(buf, 0, BLOCK_SIZE);
    memcpy(buf, &bgd_table, sizeof(bgd_table));
    write_blocks(buf, BGD_TABLE_BLOCK, 1);
}
/**
 * @brief check whether filesystem signature is missing or not in boot sector
 *
 * @return true if memcmp(boot_sector, fs_signature) returning inequality
 */

bool is_directory_empty(uint32_t inode);

bool is_empty_storage(void)
{
    uint8_t boot[BLOCK_SIZE];
    read_blocks(boot, BOOT_SECTOR, 1);
    return memcmp(boot, fs_signature, BLOCK_SIZE) != 0;
}

/**
 * @brief create a new EXT2 filesystem. Will write fs_signature into boot sector,
 * initialize super block, bgd table, block and inode bitmap, and create root directory
 */
void create_ext2(void)
{
    uint8_t zero[BLOCK_SIZE];
    uint8_t bitmap[BLOCK_SIZE];
    memset(zero, 0, BLOCK_SIZE);

    /* 1. boot sector */
    write_blocks(fs_signature, BOOT_SECTOR, 1);

    memset(&sb, 0, sizeof(sb));
    memset(&bgd_table, 0, sizeof(bgd_table));

    uint32_t total_free_blocks = 0;
    for (uint32_t g = 0; g < GROUPS_COUNT; g++)
    {
        struct EXT2BlockGroupDescriptor *bgd = &bgd_table.table[g];
        uint32_t meta = (g == 0) ? GROUP0_META_START : g * BLOCKS_PER_GROUP;

        bgd->bg_block_bitmap = meta;
        bgd->bg_inode_bitmap = meta + 1;
        bgd->bg_inode_table = meta + 2;

        write_blocks(zero, bgd->bg_inode_bitmap, 1);
        for (uint32_t b = 0; b < INODES_TABLE_BLOCK_COUNT; b++)
        {
            write_blocks(zero, bgd->bg_inode_table + b, 1);
        }

        uint32_t used = (meta + 2 + INODES_TABLE_BLOCK_COUNT) - g * BLOCKS_PER_GROUP;
        memset(bitmap, 0, BLOCK_SIZE);
        for (uint32_t i = 0; i < used; i++)
        {
            bitmap[i / 8] |= (uint8_t)(1u << (i % 8));
        }
        write_blocks(bitmap, bgd->bg_block_bitmap, 1);

        bgd->bg_free_blocks_count = (uint16_t)(BLOCKS_PER_GROUP - used);
        bgd->bg_free_inodes_count = INODES_PER_GROUP;
        bgd->bg_used_dirs_count = 0;
        total_free_blocks += BLOCKS_PER_GROUP - used;
    }

    sb.s_inodes_count = INODES_PER_GROUP * GROUPS_COUNT;
    sb.s_blocks_count = DISK_SPACE / BLOCK_SIZE;
    sb.s_r_blocks_count = 0;
    sb.s_free_blocks_count = total_free_blocks;
    sb.s_free_inodes_count = INODES_PER_GROUP * GROUPS_COUNT;
    sb.s_first_data_block = SUPERBLOCK_BLOCK;
    sb.s_first_ino = 1;
    sb.s_blocks_per_group = BLOCKS_PER_GROUP;
    sb.s_frags_per_group = BLOCKS_PER_GROUP;
    sb.s_inodes_per_group = INODES_PER_GROUP;
    sb.s_magic = EXT2_SUPER_MAGIC;

    uint32_t root = allocate_node();
    struct EXT2Inode root_node;
    init_directory_table(&root_node, root, root);
    sync_node(&root_node, root);
}
/**
 * @brief Initialize file system driver state, if is_empty_storage() then create_ext2()
 * Else, read and cache super block (located at block 1) and bgd table (located at block 2) into state
 */
void initialize_filesystem_ext2(void)
{
    if (is_empty_storage())
    {
        create_ext2();
        return;
    }

    uint8_t buf[BLOCK_SIZE];
    read_blocks(buf, SUPERBLOCK_BLOCK, 1);
    memcpy(&sb, buf, sizeof(sb));

    read_blocks(buf, BGD_TABLE_BLOCK, 1);
    memcpy(&bgd_table, buf, sizeof(bgd_table));
}