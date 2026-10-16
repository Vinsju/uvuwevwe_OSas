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

/* =========================== BLOCK ALLOCATOR (2.6.2 / 2.6.3) ===========================
 * Helper alokasi block yang dipakai write() dan delete().
 * - DIRECT_BLOCKS  : i_block[0..11]  -> langsung menunjuk data block
 * - i_block[12]    : singly indirect  -> 1 block berisi 128 id data block
 * - i_block[13]    : doubly indirect  -> 1 block berisi 128 id block pointer
 * Disk 4 MiB = 8192 block, jadi triply indirect (i_block[14]) tidak pernah dibutuhkan.
 */
#define DIRECT_BLOCKS 12u
#define PTRS_PER_BLOCK (BLOCK_SIZE / sizeof(uint32_t))                                  // 128
#define MAX_FILE_BLOCKS (DIRECT_BLOCKS + PTRS_PER_BLOCK + PTRS_PER_BLOCK * PTRS_PER_BLOCK) // 16524
#define NO_GROUP 0xFFFFFFFFu

/* Cache 1 block bitmap supaya tidak read/write bitmap untuk setiap block */
struct BitmapCache
{
    uint8_t buf[BLOCK_SIZE];
    uint32_t group; // NO_GROUP = belum ada bitmap yang di-load
    bool dirty;
};

/* Tulis superblock dan BGD table (state di RAM) ke storage */
static void commit_metadata(void)
{
    uint8_t buf[BLOCK_SIZE];
    memset(buf, 0, BLOCK_SIZE);
    memcpy(buf, &sb, sizeof(sb));
    write_blocks(buf, SUPERBLOCK_BLOCK, 1);
    memset(buf, 0, BLOCK_SIZE);
    memcpy(buf, &bgd_table, sizeof(bgd_table));
    write_blocks(buf, BGD_TABLE_BLOCK, 1);
}

static void bitmap_cache_flush(struct BitmapCache *cache)
{
    if (cache->group != NO_GROUP && cache->dirty)
    {
        write_blocks(cache->buf, bgd_table.table[cache->group].bg_block_bitmap, 1);
        cache->dirty = false;
    }
}

static void bitmap_cache_load(struct BitmapCache *cache, uint32_t group)
{
    if (cache->group == group)
    {
        return;
    }
    bitmap_cache_flush(cache);
    read_blocks(cache->buf, bgd_table.table[group].bg_block_bitmap, 1);
    cache->group = group;
    cache->dirty = false;
}

/**
 * First Fit: ambil block kosong pertama (bit 0 pertama di block bitmap),
 * mulai dari group prefered_bgd. Return 0 jika disk penuh
 * (block 0 = boot sector, jadi tidak mungkin jadi id block data yang valid).
 */
static uint32_t allocate_block(struct BitmapCache *cache, uint32_t prefered_bgd)
{
    for (uint32_t k = 0; k < GROUPS_COUNT; k++)
    {
        uint32_t g = (prefered_bgd + k) % GROUPS_COUNT;
        struct EXT2BlockGroupDescriptor *bgd = &bgd_table.table[g];
        if (bgd->bg_free_blocks_count == 0)
        {
            continue;
        }

        bitmap_cache_load(cache, g);
        for (uint32_t bit = 0; bit < BLOCKS_PER_GROUP; bit++)
        {
            if (!(cache->buf[bit / 8] & (1u << (bit % 8))))
            {
                cache->buf[bit / 8] |= (uint8_t)(1u << (bit % 8));
                cache->dirty = true;
                bgd->bg_free_blocks_count--;
                sb.s_free_blocks_count--;
                return g * BLOCKS_PER_GROUP + bit;
            }
        }
    }
    return 0;
}

/* Jumlah block pointer (indirect) tambahan yang dibutuhkan untuk menyimpan data_blocks */
static uint32_t pointer_blocks_needed(uint32_t data_blocks)
{
    uint32_t extra = 0;
    if (data_blocks > DIRECT_BLOCKS)
    {
        extra += 1; // singly indirect
    }
    if (data_blocks > DIRECT_BLOCKS + PTRS_PER_BLOCK)
    {
        uint32_t rest = data_blocks - DIRECT_BLOCKS - PTRS_PER_BLOCK;
        extra += 1 + (rest + PTRS_PER_BLOCK - 1) / PTRS_PER_BLOCK; // doubly + block level-2
    }
    return extra;
}

/**
 * Tulis data block ke-index dari src (ukuran total size byte).
 * Block terakhir yang tidak penuh di-padding 0 supaya tidak membaca melewati buffer.
 */
static void write_data_block(const uint8_t *src, uint32_t size, uint32_t index, uint32_t block_id)
{
    uint32_t offset = index * BLOCK_SIZE;
    if (src != NULL && offset + BLOCK_SIZE <= size)
    {
        write_blocks(src + offset, block_id, 1);
        return;
    }

    uint8_t tmp[BLOCK_SIZE];
    memset(tmp, 0, BLOCK_SIZE);
    if (src != NULL && offset < size)
    {
        memcpy(tmp, src + offset, size - offset);
    }
    write_blocks(tmp, block_id, 1);
}

/**
 * @brief ambil id block data ke-index milik node (direct / indirect / doubly indirect)
 * @return id block, 0 jika tidak ada
 */
uint32_t get_node_block(const struct EXT2Inode *node, uint32_t index)
{
    uint32_t ptrs[PTRS_PER_BLOCK];

    if (index < DIRECT_BLOCKS)
    {
        return node->i_block[index];
    }

    index -= DIRECT_BLOCKS;
    if (index < PTRS_PER_BLOCK)
    {
        if (node->i_block[12] == 0)
        {
            return 0;
        }
        read_blocks(ptrs, node->i_block[12], 1);
        return ptrs[index];
    }

    index -= PTRS_PER_BLOCK;
    if (index < PTRS_PER_BLOCK * PTRS_PER_BLOCK)
    {
        if (node->i_block[13] == 0)
        {
            return 0;
        }
        read_blocks(ptrs, node->i_block[13], 1);
        uint32_t child = ptrs[index / PTRS_PER_BLOCK];
        if (child == 0)
        {
            return 0;
        }
        read_blocks(ptrs, child, 1);
        return ptrs[index % PTRS_PER_BLOCK];
    }

    return 0;
}

/**
 * @brief deallocate block from the disk
 * @param locations block locations
 * @param blocks number of blocks
 * @param bitmap block bitmap
 * @param depth depth of the block (0 = data block, 1 = singly indirect, 2 = doubly indirect)
 * @param last_bgd last bgd that is used (bitmap milik group ini yang sedang ada di *bitmap)
 * @param bgd_loaded whether bgd is loaded or not
 * @return new last bgd
 */
uint32_t deallocate_block(uint32_t *locations, uint32_t blocks, struct BlockBuffer *bitmap, uint32_t depth, uint32_t *last_bgd, bool bgd_loaded)
{
    if (!bgd_loaded)
    {
        *last_bgd = NO_GROUP;
    }

    for (uint32_t i = 0; i < blocks; i++)
    {
        uint32_t id = locations[i];
        if (id == 0)
        {
            continue;
        }

        /* block pointer: bebaskan dulu semua block yang ditunjuk */
        if (depth > 0)
        {
            uint32_t children[PTRS_PER_BLOCK];
            read_blocks(children, id, 1);
            deallocate_block(children, PTRS_PER_BLOCK, bitmap, depth - 1, last_bgd, *last_bgd != NO_GROUP);
        }

        uint32_t g = id / BLOCKS_PER_GROUP;
        uint32_t bit = id % BLOCKS_PER_GROUP;
        if (*last_bgd != g)
        {
            if (*last_bgd != NO_GROUP)
            {
                write_blocks(bitmap->buf, bgd_table.table[*last_bgd].bg_block_bitmap, 1);
            }
            read_blocks(bitmap->buf, bgd_table.table[g].bg_block_bitmap, 1);
            *last_bgd = g;
        }

        if (bitmap->buf[bit / 8] & (1u << (bit % 8)))
        {
            bitmap->buf[bit / 8] &= (uint8_t)~(1u << (bit % 8));
            bgd_table.table[g].bg_free_blocks_count++;
            sb.s_free_blocks_count++;
        }
        locations[i] = 0;
    }

    return *last_bgd;
}

/**
 * @brief deallocate node blocks
 * @param locations node->block
 * @param blocks number of blocks
 */
void deallocate_blocks(void *loc, uint32_t blocks)
{
    uint32_t locations[15];
    memcpy(locations, loc, sizeof(locations)); // salinan aligned dari node->i_block
    struct BlockBuffer bitmap;
    uint32_t last_bgd = NO_GROUP;

    uint32_t direct = blocks < DIRECT_BLOCKS ? blocks : DIRECT_BLOCKS;
    deallocate_block(locations, direct, &bitmap, 0, &last_bgd, false);
    if (blocks > DIRECT_BLOCKS)
    {
        deallocate_block(&locations[12], 1, &bitmap, 1, &last_bgd, last_bgd != NO_GROUP);
    }
    if (blocks > DIRECT_BLOCKS + PTRS_PER_BLOCK)
    {
        deallocate_block(&locations[13], 1, &bitmap, 2, &last_bgd, last_bgd != NO_GROUP);
    }

    if (last_bgd != NO_GROUP)
    {
        write_blocks(bitmap.buf, bgd_table.table[last_bgd].bg_block_bitmap, 1);
    }
    commit_metadata();
}

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
    struct BitmapCache cache;
    cache.group = NO_GROUP;
    cache.dirty = false;

    uint32_t single[PTRS_PER_BLOCK]; // isi block singly indirect
    uint32_t dbl[PTRS_PER_BLOCK];    // isi block doubly indirect (level 1)
    uint32_t child[PTRS_PER_BLOCK];  // isi block level 2 yang sedang diisi
    uint32_t child_idx = NO_GROUP;   // index dbl[] milik child (NO_GROUP = belum ada)

    for (uint32_t i = 0; i < 15; i++)
    {
        node->i_block[i] = 0;
    }

    for (uint32_t i = 0; i < node->i_blocks && i < MAX_FILE_BLOCKS; i++)
    {
        /* siapkan block pointer yang dibutuhkan untuk block data ke-i */
        if (i >= DIRECT_BLOCKS && i < DIRECT_BLOCKS + PTRS_PER_BLOCK && node->i_block[12] == 0)
        {
            node->i_block[12] = allocate_block(&cache, prefered_bgd);
            if (node->i_block[12] == 0)
            {
                break;
            }
            memset(single, 0, BLOCK_SIZE);
        }
        if (i >= DIRECT_BLOCKS + PTRS_PER_BLOCK)
        {
            uint32_t outer = (i - DIRECT_BLOCKS - PTRS_PER_BLOCK) / PTRS_PER_BLOCK;
            if (node->i_block[13] == 0)
            {
                node->i_block[13] = allocate_block(&cache, prefered_bgd);
                if (node->i_block[13] == 0)
                {
                    break;
                }
                memset(dbl, 0, BLOCK_SIZE);
            }
            if (dbl[outer] == 0)
            {
                if (child_idx != NO_GROUP)
                {
                    write_blocks(child, dbl[child_idx], 1);
                }
                dbl[outer] = allocate_block(&cache, prefered_bgd);
                if (dbl[outer] == 0)
                {
                    break;
                }
                memset(child, 0, BLOCK_SIZE);
                child_idx = outer;
            }
        }

        /* alokasi block data (First Fit) lalu catat id-nya */
        uint32_t id = allocate_block(&cache, prefered_bgd);
        if (id == 0)
        {
            break; // disk penuh (seharusnya sudah dicegah oleh write())
        }

        if (i < DIRECT_BLOCKS)
        {
            node->i_block[i] = id;
        }
        else if (i < DIRECT_BLOCKS + PTRS_PER_BLOCK)
        {
            single[i - DIRECT_BLOCKS] = id;
        }
        else
        {
            child[(i - DIRECT_BLOCKS - PTRS_PER_BLOCK) % PTRS_PER_BLOCK] = id;
        }
        write_data_block(src, node->i_size, i, id);
    }

    /* simpan block pointer yang masih ada di memory */
    if (node->i_block[12] != 0)
    {
        write_blocks(single, node->i_block[12], 1);
    }
    if (child_idx != NO_GROUP)
    {
        write_blocks(child, dbl[child_idx], 1);
    }
    if (node->i_block[13] != 0)
    {
        write_blocks(dbl, node->i_block[13], 1);
    }

    bitmap_cache_flush(&cache);
    commit_metadata();
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

/* Baca satu inode dari disk */
static void read_node(uint32_t inode, struct EXT2Inode *node) {
    uint8_t buf[BLOCK_SIZE];
    uint32_t local =inode_to_local(inode);
    uint32_t block = bgd_table.table[inode_to_bgd(inode)].bg_inode_table + local / INODES_PER_TABLE;
    uint32_t offset = (local % INODES_PER_TABLE) * INODE_SIZE;
    read_blocks(buf, block, 1);
    memcpy(node, buf+offset, INODE_SIZE);
}


/* Cek Inode Valid */
static bool is_inode_used(uint32_t inode) {
    if (inode == 0 || inode > sb.s_inodes_count) {
        return false;
    }
    uint8_t bitmap[BLOCK_SIZE];
    uint32_t local = inode_to_local(inode);
    read_blocks(bitmap, bgd_table.table[inode_to_bgd(inode)].bg_inode_bitmap, 1);
    return (bitmap[local/8] >> (local%8)) & 1u;
}

/* Cari entry di directory parent_inode */
static int8_t find_entry(struct EXT2Inode *parent, const char *name, uint8_t name_len, bool want_dir, uint32_t *found) {
    uint8_t buf[BLOCK_SIZE];
    bool wrong_type = false;

    for (uint32_t i = 0; i < parent->i_blocks && i < 12; i++) {
        read_blocks(buf, parent->i_block[i], 1);
        uint32_t offset = 0;

        while (offset < BLOCK_SIZE) {
            struct EXT2DirectoryEntry *entry = get_directory_entry(buf, offset);

            if(entry->rec_len == 0) {
                break;
            }

            if (entry->inode != 0 && entry->name_len == name_len && memcmp(get_entry_name(entry), name, name_len) == 0) {
                bool is_dir = (entry->file_type == EXT2_FT_DIR);
                if (is_dir == want_dir) {
                    *found = entry->inode;
                    return 0;
                }
                wrong_type = true;
            }
            offset += entry->rec_len;
        }
    }
    return wrong_type ? 1 : 2;
}

/* Read Directory */
int8_t read_directory(struct EXT2DriverRequest *prequest) {
    struct EXT2DriverRequest request = *prequest;

    /* Cek Inode yang terpakaid dan bertipe Folder*/
    if (!is_inode_used(request.parent_inode)) {
        return 3;
    }

    struct EXT2Inode parent;
    read_node(request.parent_inode, &parent);
    if (!(parent.i_mode & EXT2_S_IFDIR)) {
        return 3;
    }

    /* Cari folder sesuai nama */
    uint32_t target;
    int8_t status = find_entry(&parent, request.name, request.name_len, true, &target);
    if (status != 0) {
        return status;
    }

    struct EXT2Inode dir;
    read_node(target, &dir);
    if (dir.i_blocks == 0 || request.buffer_size < dir.i_blocks * BLOCK_SIZE) {
        return -1;
    }

    for (uint32_t i=0; i<dir.i_blocks && i<12; i++) {
        read_blocks((uint8_t *)request.buf + i * BLOCK_SIZE, dir.i_block[i], 1);
    }

    return 0;
}

/* ============================ DIRECTORY ENTRY HELPER ============================ */

static bool is_dot_name(const char *name, uint8_t name_len)
{
    return (name_len == 1 && name[0] == '.') ||
           (name_len == 2 && name[0] == '.' && name[1] == '.');
}

/**
 * Tambah entry baru ke directory parent.
 * Cari slot pertama yang muat di block-block directory (memecah rec_len entry yang
 * punya sisa ruang). Kalau semua block penuh, tambah 1 block baru ke directory.
 * @return true jika berhasil
 */
static bool add_dir_entry(struct EXT2Inode *parent, uint32_t parent_inode, const char *name,
                          uint8_t name_len, uint32_t inode, uint8_t file_type)
{
    uint8_t buf[BLOCK_SIZE];
    uint16_t need = get_entry_record_len(name_len);

    for (uint32_t i = 0; i < parent->i_blocks && i < DIRECT_BLOCKS; i++)
    {
        read_blocks(buf, parent->i_block[i], 1);
        uint32_t offset = 0;

        while (offset < BLOCK_SIZE)
        {
            struct EXT2DirectoryEntry *entry = get_directory_entry(buf, offset);
            if (entry->rec_len == 0 || offset + entry->rec_len > BLOCK_SIZE)
            {
                break;
            }

            uint16_t used = (entry->inode == 0) ? 0 : get_entry_record_len((uint8_t)entry->name_len);
            if (entry->rec_len - used >= need)
            {
                struct EXT2DirectoryEntry *new_entry = entry;
                uint16_t new_len = entry->rec_len;
                if (used != 0)
                {
                    entry->rec_len = used; // entry lama dipotong sesuai ukurannya
                    new_entry = get_next_directory_entry(entry);
                    new_len = (uint16_t)(new_len - used);
                }

                memset(new_entry, 0, new_len);
                new_entry->inode = inode;
                new_entry->rec_len = new_len; // entry baru mengambil sisa ruang
                new_entry->name_len = name_len;
                new_entry->file_type = file_type;
                memcpy(get_entry_name(new_entry), name, name_len);

                write_blocks(buf, parent->i_block[i], 1);
                return true;
            }
            offset += entry->rec_len;
        }
    }

    /* semua block directory penuh -> tambah 1 block baru (hanya direct block) */
    if (parent->i_blocks >= DIRECT_BLOCKS)
    {
        return false;
    }

    struct BitmapCache cache;
    cache.group = NO_GROUP;
    cache.dirty = false;
    uint32_t block = allocate_block(&cache, inode_to_bgd(parent_inode));
    if (block == 0)
    {
        return false;
    }
    bitmap_cache_flush(&cache);

    memset(buf, 0, BLOCK_SIZE);
    struct EXT2DirectoryEntry *entry = get_directory_entry(buf, 0);
    entry->inode = inode;
    entry->rec_len = BLOCK_SIZE;
    entry->name_len = name_len;
    entry->file_type = file_type;
    memcpy(get_entry_name(entry), name, name_len);
    write_blocks(buf, block, 1);

    parent->i_block[parent->i_blocks] = block;
    parent->i_blocks++;
    parent->i_size += BLOCK_SIZE;
    sync_node(parent, parent_inode);
    commit_metadata();
    return true;
}

/**
 * Hapus entry (nama + tipe cocok) dari directory parent.
 * - entry pertama di block  -> inode di-set 0 (slot kosong, rec_len tetap)
 * - entry lain              -> digabung ke rec_len entry sebelumnya
 * @return true jika entry ditemukan dan dihapus
 */
static bool remove_dir_entry(struct EXT2Inode *parent, const char *name, uint8_t name_len, bool is_dir)
{
    uint8_t buf[BLOCK_SIZE];

    for (uint32_t i = 0; i < parent->i_blocks && i < DIRECT_BLOCKS; i++)
    {
        read_blocks(buf, parent->i_block[i], 1);
        uint32_t offset = 0;
        struct EXT2DirectoryEntry *prev = NULL;

        while (offset < BLOCK_SIZE)
        {
            struct EXT2DirectoryEntry *entry = get_directory_entry(buf, offset);
            if (entry->rec_len == 0 || offset + entry->rec_len > BLOCK_SIZE)
            {
                break;
            }

            if (entry->inode != 0 && entry->name_len == name_len &&
                memcmp(get_entry_name(entry), name, name_len) == 0 &&
                (entry->file_type == EXT2_FT_DIR) == is_dir)
            {
                uint16_t len = entry->rec_len;
                if (prev == NULL)
                {
                    memset(entry, 0, len);
                    entry->rec_len = len;
                }
                else
                {
                    prev->rec_len = (uint16_t)(prev->rec_len + len);
                    memset(entry, 0, len);
                }
                write_blocks(buf, parent->i_block[i], 1);
                return true;
            }

            prev = entry;
            offset += entry->rec_len;
        }
    }
    return false;
}

/**
 * @brief check whether a directory table has children or not
 * @param inode of a directory table
 * @return true jika directory hanya berisi "." dan ".."
 */
bool is_directory_empty(uint32_t inode)
{
    struct EXT2Inode dir;
    uint8_t buf[BLOCK_SIZE];
    read_node(inode, &dir);

    for (uint32_t i = 0; i < dir.i_blocks && i < DIRECT_BLOCKS; i++)
    {
        read_blocks(buf, dir.i_block[i], 1);
        uint32_t offset = 0;

        while (offset < BLOCK_SIZE)
        {
            struct EXT2DirectoryEntry *entry = get_directory_entry(buf, offset);
            if (entry->rec_len == 0 || offset + entry->rec_len > BLOCK_SIZE)
            {
                break;
            }
            if (entry->inode != 0 && !is_dot_name(get_entry_name(entry), (uint8_t)entry->name_len))
            {
                return false;
            }
            offset += entry->rec_len;
        }
    }
    return true;
}

/**
 * @brief deallocate node from the disk, will also deallocate its used blocks
 * also all of the blocks of indirect blocks if necessary
 * @param inode that needs to be deallocated
 */
void deallocate_node(uint32_t inode)
{
    struct EXT2Inode node;
    uint8_t bitmap[BLOCK_SIZE];
    read_node(inode, &node);

    /* 1. bebaskan semua data block + block pointer */
    deallocate_blocks(node.i_block, node.i_blocks);

    /* 2. bebaskan inode di inode bitmap */
    uint32_t g = inode_to_bgd(inode);
    uint32_t local = inode_to_local(inode);
    read_blocks(bitmap, bgd_table.table[g].bg_inode_bitmap, 1);
    if (bitmap[local / 8] & (1u << (local % 8)))
    {
        bitmap[local / 8] &= (uint8_t)~(1u << (local % 8));
        write_blocks(bitmap, bgd_table.table[g].bg_inode_bitmap, 1);
        bgd_table.table[g].bg_free_inodes_count++;
        sb.s_free_inodes_count++;
        if ((node.i_mode & EXT2_S_IFDIR) && bgd_table.table[g].bg_used_dirs_count > 0)
        {
            bgd_table.table[g].bg_used_dirs_count--;
        }
    }

    /* 3. kosongkan entry di inode table */
    memset(&node, 0, sizeof(node));
    sync_node(&node, inode);

    commit_metadata();
}

/* =============================== 2.6.2 WRITE =============================== */

/**
 * @brief EXT2 write, write a file or a folder to file system
 * - request.is_directory == true  -> buat folder baru (buf & buffer_size diabaikan)
 * - request.is_directory == false -> buat file dengan isi buf sebanyak buffer_size byte,
 *   menggunakan tepat ceil(buffer_size / BLOCK_SIZE) data block (First Fit)
 * @return Error code: 0 success - 1 file/folder already exist - 2 invalid parent folder - -1 unknown
 */
int8_t write(struct EXT2DriverRequest *request)
{
    if (request == NULL)
    {
        return -1;
    }
    struct EXT2DriverRequest req = *request;

    /* parent harus inode yang terpakai, bertipe folder, dan punya datablock */
    if (!is_inode_used(req.parent_inode))
    {
        return 2;
    }
    struct EXT2Inode parent;
    read_node(req.parent_inode, &parent);
    if (!(parent.i_mode & EXT2_S_IFDIR) || parent.i_blocks == 0)
    {
        return 2;
    }

    if (req.name == NULL || req.name_len == 0)
    {
        return -1;
    }

    /* nama tidak boleh sudah dipakai file ATAU folder lain di parent yang sama */
    uint32_t existing;
    if (is_dot_name(req.name, req.name_len) ||
        find_entry(&parent, req.name, req.name_len, req.is_directory, &existing) != 2)
    {
        return 1;
    }

    if (!req.is_directory && req.buffer_size > 0 && req.buf == NULL)
    {
        return -1;
    }

    /* cek kapasitas dulu supaya tidak ada penulisan setengah jalan */
    uint32_t data_blocks = req.is_directory ? 1 : (req.buffer_size + BLOCK_SIZE - 1) / BLOCK_SIZE;
    if (data_blocks > MAX_FILE_BLOCKS)
    {
        return -1;
    }
    uint32_t total_blocks = data_blocks + pointer_blocks_needed(data_blocks);
    if (sb.s_free_inodes_count == 0 || total_blocks > sb.s_free_blocks_count)
    {
        return -1;
    }

    /* alokasi inode baru + tulis datablock */
    uint32_t inode = allocate_node();
    if (inode == 0)
    {
        return -1;
    }

    struct EXT2Inode node;
    if (req.is_directory)
    {
        init_directory_table(&node, inode, req.parent_inode);
    }
    else
    {
        memset(&node, 0, sizeof(node));
        node.i_mode = EXT2_S_IFREG;
        node.i_size = req.buffer_size;
        node.i_blocks = data_blocks;
        allocate_node_blocks(req.buf, &node, inode_to_bgd(inode));
    }
    sync_node(&node, inode);

    /* buat DirectoryEntry baru di parent */
    uint8_t file_type = req.is_directory ? EXT2_FT_DIR : EXT2_FT_REG_FILE;
    if (!add_dir_entry(&parent, req.parent_inode, req.name, req.name_len, inode, file_type))
    {
        deallocate_node(inode); // rollback
        return -1;
    }

    /* commit semua metadata file system ke storage */
    commit_metadata();
    return 0;
}

/* =============================== 2.6.3 DELETE =============================== */

/**
 * @brief EXT2 delete, delete a file or empty directory in file system
 * @param request buf and buffer_size is unused, is_dir == true means delete folder (possible file with name same as folder)
 * @return Error code: 0 success - 1 not found - 2 folder is not empty - 3 parent folder invalid -1 unknown
 */
int8_t delete (struct EXT2DriverRequest request)
{
    if (!is_inode_used(request.parent_inode))
    {
        return 3;
    }
    struct EXT2Inode parent;
    read_node(request.parent_inode, &parent);
    if (!(parent.i_mode & EXT2_S_IFDIR) || parent.i_blocks == 0)
    {
        return 3;
    }

    /* "." dan ".." tidak boleh dihapus (root juga otomatis terlindungi) */
    if (request.name == NULL || request.name_len == 0 || is_dot_name(request.name, request.name_len))
    {
        return -1;
    }

    uint32_t target;
    if (find_entry(&parent, request.name, request.name_len, request.is_directory, &target) != 0)
    {
        return 1;
    }
    if (target == request.parent_inode || !is_inode_used(target))
    {
        return -1;
    }

    /* folder hanya boleh dihapus kalau kosong */
    if (request.is_directory && !is_directory_empty(target))
    {
        return 2;
    }

    if (!remove_dir_entry(&parent, request.name, request.name_len, request.is_directory))
    {
        return -1;
    }
    deallocate_node(target);
    return 0;
}
