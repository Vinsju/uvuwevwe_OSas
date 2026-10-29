/**
 * uvuwevwe_OSas Shell (IF2130 Chapter 3.4)
 *
 * User program yang berjalan di ring 3 dan hanya berinteraksi dengan kernel lewat
 * system call int 0x30 (lihat header/syscall/syscall.h).
 *
 * Fitur:
 * - REPL dengan prompt berisi current working directory (awal di root)
 * - Buffer output: setiap command menulis ke buffer, baru dicetak ke layar di akhir
 * - Pipeline: cmd1 | cmd2 | ... (output satu command menjadi input command berikutnya)
 * - Redirect output ke file: cmd > file (timpa) dan cmd >> file (tambah di akhir)
 * - Built-in: cd, ls, mkdir, cat, cp, rm, mv, find, grep (+ echo, pwd, clear, help)
 * - Bonus: cp -r / rm -r untuk folder, grep dengan regex sederhana (^ $ . *)
 * - Path: absolut (/a/b) maupun relatif (a/b, ../x) untuk semua command
 *
 * Memory layout user (4 MiB pertama virtual memory harus sudah dipetakan oleh kernel):
 *   0x000000 - ...      : kode + data shell (flat binary, maksimal 1 MiB)
 *   0x100000 - 0x1FFFFF : buffer file (cat, cp, mv, grep)
 *   0x200000 - 0x27FFFF : 2 buffer output pipeline (ping-pong)
 *   ...      - 0x3FFFFC : user stack (tumbuh ke bawah)
 */
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "header/filesystem/ext2.h"
#include "header/syscall/syscall.h"
#include "header/stdlib/string.h"

/* ============================== KONFIGURASI ============================== */

#ifndef SHELL_FILE_BUF
#define SHELL_FILE_BUF ((uint8_t *)0x00100000)
#endif
#ifndef SHELL_FILE_BUF_SIZE
#define SHELL_FILE_BUF_SIZE 0x00100000u
#endif
#ifndef SHELL_PIPE_BUF_A
#define SHELL_PIPE_BUF_A ((char *)0x00200000)
#endif
#ifndef SHELL_PIPE_BUF_B
#define SHELL_PIPE_BUF_B ((char *)0x00240000)
#endif
#ifndef SHELL_PIPE_BUF_SIZE
#define SHELL_PIPE_BUF_SIZE 0x00040000u
#endif

#define SHELL_LINE_MAX     256
#define SHELL_MAX_TOKENS   32
#define SHELL_MAX_STAGES   8
#define SHELL_MAX_DEPTH    32
#define SHELL_PATH_MAX     512
#define SHELL_NAME_MAX     255
#define SHELL_DIR_BLOCKS   12
#define SHELL_DIR_BUF_SIZE (SHELL_DIR_BLOCKS * BLOCK_SIZE)
#define ROOT_INODE         1

#define COLOR_TEXT   0x0F
#define COLOR_DIR    0x0B
#define COLOR_USER   0x0A
#define COLOR_PATH   0x09
#define COLOR_ERROR  0x0C
#define COLOR_INFO   0x0E
#define COLOR_ESCAPE 0x1B /* penanda ganti warna di dalam buffer output */

/* ============================== SYSTEM CALL ============================== */

#ifndef SHELL_HOST_TEST
void syscall(uintptr_t eax, uintptr_t ebx, uintptr_t ecx, uintptr_t edx)
{
    /* Satu instruksi asm dengan constraint register, supaya compiler tidak menimpa
       ebx/ecx/edx yang sudah diisi sebelum int 0x30 (lebih aman dari mov terpisah) */
    __asm__ volatile("int $0x30"
                     : /* <Empty> */
                     : "a"(eax), "b"(ebx), "c"(ecx), "d"(edx)
                     : "memory");
}
#else
void syscall(uintptr_t eax, uintptr_t ebx, uintptr_t ecx, uintptr_t edx);
#endif

static void sys_puts(const char *str, uint32_t len, uint8_t color)
{
    if (len > 0)
    {
        syscall(SYSCALL_PUTS, (uintptr_t)str, len, color);
    }
}

static void sys_putc(char c, uint8_t color)
{
    syscall(SYSCALL_PUTCHAR, (uintptr_t)(uint8_t)c, color, 0);
}

static char sys_getchar(void)
{
    char c = 0;
    syscall(SYSCALL_KEYBOARD_GETCHAR, (uintptr_t)&c, 0, 0);
    return c;
}

/* ============================== STRING HELPER ============================== */

static uint32_t str_len(const char *s)
{
    uint32_t n = 0;
    while (s[n] != '\0')
    {
        n++;
    }
    return n;
}

static bool str_eq(const char *a, const char *b)
{
    while (*a != '\0' && *a == *b)
    {
        a++;
        b++;
    }
    return *a == *b;
}

static bool name_eq(const char *a, uint32_t a_len, const char *b, uint32_t b_len)
{
    return a_len == b_len && memcmp(a, b, a_len) == 0;
}

static bool is_dot_name(const char *name, uint32_t len)
{
    return (len == 1 && name[0] == '.') || (len == 2 && name[0] == '.' && name[1] == '.');
}

static void print(const char *s, uint8_t color)
{
    sys_puts(s, str_len(s), color);
}

/* Pesan error langsung ke layar (seperti stderr), tidak ikut masuk pipeline */
static void print_error(const char *cmd, const char *arg, const char *msg)
{
    print(cmd, COLOR_ERROR);
    print(": ", COLOR_ERROR);
    if (arg != NULL)
    {
        print(arg, COLOR_ERROR);
        print(": ", COLOR_ERROR);
    }
    print(msg, COLOR_ERROR);
    sys_putc('\n', COLOR_ERROR);
}

/* ============================== OUTPUT BUFFER ============================== */

struct OutBuf
{
    char *data;
    uint32_t len;
    uint32_t cap;
    bool overflow;
};

static void ob_init(struct OutBuf *out, char *data, uint32_t cap)
{
    out->data = data;
    out->len = 0;
    out->cap = cap;
    out->overflow = false;
}

static void ob_putc(struct OutBuf *out, char c)
{
    if (out->len < out->cap)
    {
        out->data[out->len++] = c;
    }
    else
    {
        out->overflow = true;
    }
}

/* Data mentah (isi file): byte COLOR_ESCAPE diganti '.' supaya tidak dianggap kode warna */
static void ob_write(struct OutBuf *out, const char *s, uint32_t n)
{
    for (uint32_t i = 0; i < n; i++)
    {
        ob_putc(out, s[i] == COLOR_ESCAPE ? '.' : s[i]);
    }
}

static void ob_puts(struct OutBuf *out, const char *s)
{
    ob_write(out, s, str_len(s));
}

static void ob_color(struct OutBuf *out, uint8_t color)
{
    ob_putc(out, COLOR_ESCAPE);
    ob_putc(out, (char)color);
}

/* Cetak buffer ke layar, menerjemahkan penanda warna */
static void ob_flush_to_screen(struct OutBuf *out)
{
    uint8_t color = COLOR_TEXT;
    uint32_t start = 0;
    uint32_t i = 0;
    while (i < out->len)
    {
        if (out->data[i] == COLOR_ESCAPE && i + 1 < out->len)
        {
            sys_puts(out->data + start, i - start, color);
            color = (uint8_t)out->data[i + 1];
            i += 2;
            start = i;
        }
        else
        {
            i++;
        }
    }
    sys_puts(out->data + start, out->len - start, color);
    if (out->overflow)
    {
        print("\n[output terpotong: buffer penuh]\n", COLOR_ERROR);
    }
}

/* Buang penanda warna supaya output bersih saat dijadikan input command berikutnya */
static void ob_strip_colors(struct OutBuf *out)
{
    uint32_t w = 0;
    for (uint32_t r = 0; r < out->len; r++)
    {
        if (out->data[r] == COLOR_ESCAPE && r + 1 < out->len)
        {
            r++;
            continue;
        }
        out->data[w++] = out->data[r];
    }
    out->len = w;
}

/* ============================== FILE SYSTEM ============================== */

static int8_t fs_call(uint32_t service, struct EXT2DriverRequest *request)
{
    int8_t retcode = -1;
    syscall(service, (uintptr_t)request, (uintptr_t)&retcode, 0);
    return retcode;
}

static struct EXT2DriverRequest make_request(void *buf, uint32_t size, const char *name,
                                             uint32_t name_len, uint32_t parent, bool is_dir)
{
    struct EXT2DriverRequest request = {
        .buf = buf,
        .name = (char *)name,
        .name_len = (uint8_t)name_len,
        .parent_inode = parent,
        .buffer_size = size,
        .is_directory = is_dir,
    };
    return request;
}

/* Baca isi folder (child name di dalam parent) ke buf berukuran SHELL_DIR_BUF_SIZE */
static int8_t fs_read_dir(uint32_t parent, const char *name, uint32_t name_len, uint8_t *buf)
{
    memset(buf, 0, SHELL_DIR_BUF_SIZE);
    struct EXT2DriverRequest request = make_request(buf, SHELL_DIR_BUF_SIZE, name, name_len, parent, true);
    return fs_call(SYSCALL_FS_READ_DIRECTORY, &request);
}

/* Baca folder berdasarkan inode-nya: setiap folder punya entry "." yang menunjuk dirinya */
static int8_t fs_read_dir_inode(uint32_t inode, uint8_t *buf)
{
    return fs_read_dir(inode, ".", 1, buf);
}

static int8_t fs_file_size(uint32_t parent, const char *name, uint32_t name_len, uint32_t *size)
{
    struct EXT2DriverRequest request = make_request(NULL, 0, name, name_len, parent, false);
    int8_t retcode = -1;
    syscall(SYSCALL_FS_FILE_SIZE, (uintptr_t)&request, (uintptr_t)&retcode, (uintptr_t)size);
    return retcode;
}

static int8_t fs_read_file(uint32_t parent, const char *name, uint32_t name_len, void *buf, uint32_t size)
{
    struct EXT2DriverRequest request = make_request(buf, size, name, name_len, parent, false);
    return fs_call(SYSCALL_FS_READ, &request);
}

static int8_t fs_write(uint32_t parent, const char *name, uint32_t name_len, void *buf, uint32_t size, bool is_dir)
{
    struct EXT2DriverRequest request = make_request(buf, size, name, name_len, parent, is_dir);
    return fs_call(SYSCALL_FS_WRITE, &request);
}

static int8_t fs_delete(uint32_t parent, const char *name, uint32_t name_len, bool is_dir)
{
    struct EXT2DriverRequest request = make_request(NULL, 0, name, name_len, parent, is_dir);
    return fs_call(SYSCALL_FS_DELETE, &request);
}

/* Iterator entry folder hasil read_directory (blok demi blok, mengikuti rec_len) */
struct DirIter
{
    uint8_t *buf;
    uint32_t block;
    uint32_t offset;
};

static void dir_iter_init(struct DirIter *it, uint8_t *buf)
{
    it->buf = buf;
    it->block = 0;
    it->offset = 0;
}

static struct EXT2DirectoryEntry *dir_iter_next(struct DirIter *it)
{
    while (it->block < SHELL_DIR_BLOCKS)
    {
        uint8_t *block = it->buf + it->block * BLOCK_SIZE;
        /* Block folder yang valid selalu punya rec_len > 0 di offset 0,
           block yang masih nol berarti sudah melewati block terakhir folder */
        if (it->offset == 0 && ((struct EXT2DirectoryEntry *)block)->rec_len == 0)
        {
            return NULL;
        }
        if (it->offset >= BLOCK_SIZE)
        {
            it->block++;
            it->offset = 0;
            continue;
        }

        struct EXT2DirectoryEntry *entry = (struct EXT2DirectoryEntry *)(block + it->offset);
        if (entry->rec_len == 0 || it->offset + entry->rec_len > BLOCK_SIZE)
        {
            it->block++;
            it->offset = 0;
            continue;
        }
        it->offset += entry->rec_len;
        if (entry->inode != 0)
        {
            return entry;
        }
    }
    return NULL;
}

static char *entry_name(struct EXT2DirectoryEntry *entry)
{
    return (char *)entry + sizeof(struct EXT2DirectoryEntry);
}

static bool entry_is_dot(struct EXT2DirectoryEntry *entry)
{
    return is_dot_name(entry_name(entry), entry->name_len);
}

/* Cari entry name di folder dir_inode. Mengisi inode dan tipe (EXT2_FT_DIR / EXT2_FT_REG_FILE) */
static bool dir_lookup(uint32_t dir_inode, const char *name, uint32_t name_len,
                       uint32_t *out_inode, uint8_t *out_type)
{
    uint8_t buf[SHELL_DIR_BUF_SIZE];
    if (fs_read_dir_inode(dir_inode, buf) != 0)
    {
        return false;
    }
    struct DirIter it;
    dir_iter_init(&it, buf);
    struct EXT2DirectoryEntry *entry;
    while ((entry = dir_iter_next(&it)) != NULL)
    {
        if (name_eq(entry_name(entry), entry->name_len, name, name_len))
        {
            *out_inode = entry->inode;
            *out_type = entry->file_type;
            return true;
        }
    }
    return false;
}

static uint32_t dir_parent_inode(uint32_t dir_inode)
{
    uint32_t parent = ROOT_INODE;
    uint8_t type;
    if (!dir_lookup(dir_inode, "..", 2, &parent, &type))
    {
        return ROOT_INODE;
    }
    return parent;
}

/* true jika folder ancestor berada di jalur node -> root (termasuk node itu sendiri) */
static bool is_ancestor(uint32_t ancestor, uint32_t node)
{
    for (uint32_t step = 0; step < 256; step++)
    {
        if (node == ancestor)
        {
            return true;
        }
        if (node == ROOT_INODE)
        {
            return false;
        }
        node = dir_parent_inode(node);
    }
    return false;
}

/* ============================== CURRENT WORKING DIRECTORY ============================== */

struct Cwd
{
    uint32_t depth;                     /* 0 = root */
    uint32_t inode[SHELL_MAX_DEPTH];    /* inode folder pada setiap kedalaman */
    uint16_t path_len[SHELL_MAX_DEPTH]; /* panjang path string pada setiap kedalaman */
    char path[SHELL_PATH_MAX];          /* "" untuk root, "/a/b" untuk folder lain */
};

static struct Cwd cwd;

static void cwd_reset(struct Cwd *c)
{
    c->depth = 0;
    c->inode[0] = ROOT_INODE;
    c->path_len[0] = 0;
    c->path[0] = '\0';
}

static uint32_t cwd_inode(const struct Cwd *c)
{
    return c->inode[c->depth];
}

static const char *cwd_display(const struct Cwd *c)
{
    return c->depth == 0 ? "/" : c->path;
}

/**
 * Jalankan path (absolut atau relatif) mulai dari c, hanya melewati folder.
 * len = jumlah karakter path yang dipakai. Mengembalikan NULL jika berhasil,
 * atau pesan error.
 */
static const char *cwd_walk(struct Cwd *c, const char *path, uint32_t len)
{
    uint32_t i = 0;
    if (len > 0 && path[0] == '/')
    {
        cwd_reset(c);
        i = 1;
    }

    while (i < len)
    {
        uint32_t start = i;
        while (i < len && path[i] != '/')
        {
            i++;
        }
        const char *comp = path + start;
        uint32_t comp_len = i - start;
        i++; /* lewati '/' */

        if (comp_len == 0 || (comp_len == 1 && comp[0] == '.'))
        {
            continue;
        }
        if (comp_len == 2 && comp[0] == '.' && comp[1] == '.')
        {
            if (c->depth > 0)
            {
                c->depth--;
                c->path[c->path_len[c->depth]] = '\0';
            }
            continue;
        }
        if (comp_len > SHELL_NAME_MAX)
        {
            return "Nama terlalu panjang";
        }

        uint8_t buf[SHELL_DIR_BUF_SIZE];
        int8_t rc = fs_read_dir(cwd_inode(c), comp, comp_len, buf);
        if (rc == 1)
        {
            return "Bukan sebuah folder";
        }
        if (rc != 0)
        {
            return "Folder tidak ditemukan";
        }

        uint32_t new_len = c->path_len[c->depth] + 1 + comp_len;
        if (c->depth + 1 >= SHELL_MAX_DEPTH || new_len + 1 > SHELL_PATH_MAX)
        {
            return "Path terlalu panjang";
        }
        uint32_t child = ((struct EXT2DirectoryEntry *)buf)->inode; /* entry "." */
        char *dst = c->path + c->path_len[c->depth];
        dst[0] = '/';
        memcpy(dst + 1, comp, comp_len);
        c->depth++;
        c->inode[c->depth] = child;
        c->path_len[c->depth] = (uint16_t)new_len;
        c->path[new_len] = '\0';
    }
    return NULL;
}

/* Resolve path ke sebuah folder. Hasil disimpan di out (salinan cwd yang sudah di-walk) */
static const char *resolve_dir(const char *path, struct Cwd *out)
{
    *out = cwd;
    return cwd_walk(out, path, str_len(path));
}

/**
 * Pisahkan path menjadi folder parent + nama terakhir, contoh "a/b/c.txt" -> (inode a/b, "c.txt").
 * Nama terakhir tidak boleh kosong, "." atau "..".
 */
static const char *resolve_parent(const char *path, uint32_t *parent, const char **name, uint32_t *name_len)
{
    uint32_t len = str_len(path);
    while (len > 1 && path[len - 1] == '/')
    {
        len--; /* "folder/" diperlakukan sama dengan "folder" */
    }

    uint32_t slash = len;
    for (uint32_t i = 0; i < len; i++)
    {
        if (path[i] == '/')
        {
            slash = i;
        }
    }

    struct Cwd tmp = cwd;
    const char *base = path;
    uint32_t base_len = len;
    if (slash < len)
    {
        const char *err = cwd_walk(&tmp, path, slash == 0 ? 1 : slash);
        if (err != NULL)
        {
            return err;
        }
        base = path + slash + 1;
        base_len = len - slash - 1;
    }

    if (base_len == 0 || is_dot_name(base, base_len))
    {
        return "Path tidak valid";
    }
    if (base_len > SHELL_NAME_MAX)
    {
        return "Nama terlalu panjang";
    }
    *parent = cwd_inode(&tmp);
    *name = base;
    *name_len = base_len;
    return NULL;
}

/* ============================== COMMAND CONTEXT ============================== */

struct CmdCtx
{
    int argc;
    char **argv;
    const char *in;     /* output command sebelumnya di pipeline (NULL jika tidak ada) */
    uint32_t in_len;
    struct OutBuf *out; /* stdout command */
    bool to_screen;     /* true jika output langsung ke layar (stage terakhir) */
};

static const char *write_error_message(int8_t rc)
{
    switch (rc)
    {
    case 1:
        return "Sudah ada file/folder dengan nama tersebut";
    case 2:
        return "Folder parent tidak valid";
    default:
        return "Gagal menulis (storage penuh?)";
    }
}

static const char *read_error_message(int8_t rc)
{
    switch (rc)
    {
    case 1:
        return "Adalah sebuah folder";
    case 2:
        return "File terlalu besar";
    case 3:
        return "File tidak ditemukan";
    case 4:
        return "Folder parent tidak valid";
    default:
        return "Gagal membaca file";
    }
}

/* Baca seluruh file ke SHELL_FILE_BUF. Mengembalikan NULL + ukuran, atau pesan error */
static const char *load_file(uint32_t parent, const char *name, uint32_t name_len, uint32_t *size)
{
    int8_t rc = fs_file_size(parent, name, name_len, size);
    if (rc != 0)
    {
        return read_error_message(rc);
    }
    if (*size > SHELL_FILE_BUF_SIZE)
    {
        return "File terlalu besar untuk buffer shell (maks 1 MiB)";
    }
    rc = fs_read_file(parent, name, name_len, SHELL_FILE_BUF, SHELL_FILE_BUF_SIZE);
    if (rc != 0)
    {
        return read_error_message(rc);
    }
    return NULL;
}

/* ============================== cd / pwd / ls ============================== */

static void cmd_cd(struct CmdCtx *ctx)
{
    if (ctx->argc > 2)
    {
        print_error("cd", NULL, "Terlalu banyak argumen");
        return;
    }
    if (ctx->argc == 1)
    {
        cwd_reset(&cwd);
        return;
    }
    struct Cwd tmp;
    const char *err = resolve_dir(ctx->argv[1], &tmp);
    if (err != NULL)
    {
        print_error("cd", ctx->argv[1], err);
        return;
    }
    cwd = tmp;
}

static void cmd_pwd(struct CmdCtx *ctx)
{
    ob_puts(ctx->out, cwd_display(&cwd));
    ob_putc(ctx->out, '\n');
}

static void cmd_ls(struct CmdCtx *ctx)
{
    if (ctx->argc > 2)
    {
        print_error("ls", NULL, "Terlalu banyak argumen");
        return;
    }

    uint32_t dir = cwd_inode(&cwd);
    if (ctx->argc == 2)
    {
        struct Cwd tmp;
        const char *err = resolve_dir(ctx->argv[1], &tmp);
        if (err != NULL)
        {
            print_error("ls", ctx->argv[1], err);
            return;
        }
        dir = cwd_inode(&tmp);
    }

    uint8_t buf[SHELL_DIR_BUF_SIZE];
    if (fs_read_dir_inode(dir, buf) != 0)
    {
        print_error("ls", NULL, "Gagal membaca folder");
        return;
    }

    struct DirIter it;
    dir_iter_init(&it, buf);
    struct EXT2DirectoryEntry *entry;
    uint32_t col = 0;
    bool any = false;
    while ((entry = dir_iter_next(&it)) != NULL)
    {
        if (entry_is_dot(entry))
        {
            continue;
        }
        any = true;
        uint32_t len = entry->name_len;
        bool is_dir = entry->file_type == EXT2_FT_DIR;
        if (ctx->to_screen)
        {
            /* Ke layar: berjajar dengan warna, folder diberi '/' */
            uint32_t width = len + (is_dir ? 1 : 0) + 2;
            if (col > 0 && col + width > 80)
            {
                ob_putc(ctx->out, '\n');
                col = 0;
            }
            ob_color(ctx->out, is_dir ? COLOR_DIR : COLOR_TEXT);
            ob_write(ctx->out, entry_name(entry), len);
            if (is_dir)
            {
                ob_putc(ctx->out, '/');
            }
            ob_color(ctx->out, COLOR_TEXT);
            ob_puts(ctx->out, "  ");
            col += width;
        }
        else
        {
            /* Ke pipeline: satu nama per baris (seperti ls Unix saat di-pipe) */
            ob_write(ctx->out, entry_name(entry), len);
            ob_putc(ctx->out, '\n');
        }
    }
    if (ctx->to_screen && any)
    {
        ob_putc(ctx->out, '\n');
    }
}

/* ============================== mkdir / cat / echo ============================== */

static void cmd_mkdir(struct CmdCtx *ctx)
{
    if (ctx->argc < 2)
    {
        print_error("mkdir", NULL, "Penggunaan: mkdir <nama folder>...");
        return;
    }
    for (int i = 1; i < ctx->argc; i++)
    {
        uint32_t parent, name_len;
        const char *name;
        const char *err = resolve_parent(ctx->argv[i], &parent, &name, &name_len);
        if (err != NULL)
        {
            print_error("mkdir", ctx->argv[i], err);
            continue;
        }
        int8_t rc = fs_write(parent, name, name_len, NULL, 0, true);
        if (rc != 0)
        {
            print_error("mkdir", ctx->argv[i], write_error_message(rc));
        }
    }
}

static void cmd_cat(struct CmdCtx *ctx)
{
    if (ctx->argc == 1)
    {
        /* Tanpa argumen: teruskan input pipeline */
        if (ctx->in != NULL)
        {
            ob_write(ctx->out, ctx->in, ctx->in_len);
        }
        else
        {
            print_error("cat", NULL, "Penggunaan: cat <file>...");
        }
        return;
    }
    for (int i = 1; i < ctx->argc; i++)
    {
        uint32_t parent, name_len, size;
        const char *name;
        const char *err = resolve_parent(ctx->argv[i], &parent, &name, &name_len);
        if (err == NULL)
        {
            err = load_file(parent, name, name_len, &size);
        }
        if (err != NULL)
        {
            print_error("cat", ctx->argv[i], err);
            continue;
        }
        ob_write(ctx->out, (const char *)SHELL_FILE_BUF, size);
        if (ctx->to_screen && size > 0 && SHELL_FILE_BUF[size - 1] != '\n')
        {
            ob_putc(ctx->out, '\n'); /* supaya prompt berikutnya mulai di baris baru */
        }
    }
}

static void cmd_echo(struct CmdCtx *ctx)
{
    for (int i = 1; i < ctx->argc; i++)
    {
        if (i > 1)
        {
            ob_putc(ctx->out, ' ');
        }
        ob_puts(ctx->out, ctx->argv[i]);
    }
    ob_putc(ctx->out, '\n');
}

/* ============================== cp / rm / mv ============================== */

/* Salin satu file. Jika tujuan sudah ada sebagai file, ditimpa (perilaku cp Unix) */
static bool copy_file(const char *cmd, uint32_t src_parent, const char *src_name, uint32_t src_len,
                      uint32_t dst_parent, const char *dst_name, uint32_t dst_len)
{
    if (src_parent == dst_parent && name_eq(src_name, src_len, dst_name, dst_len))
    {
        print_error(cmd, dst_name, "Sumber dan tujuan adalah file yang sama");
        return false;
    }

    uint32_t size;
    const char *err = load_file(src_parent, src_name, src_len, &size);
    if (err != NULL)
    {
        print_error(cmd, src_name, err);
        return false;
    }

    int8_t rc = fs_write(dst_parent, dst_name, dst_len, SHELL_FILE_BUF, size, false);
    if (rc == 1)
    {
        uint32_t existing;
        uint8_t type;
        if (dir_lookup(dst_parent, dst_name, dst_len, &existing, &type) && type == EXT2_FT_DIR)
        {
            print_error(cmd, dst_name, "Sudah ada folder dengan nama tersebut");
            return false;
        }
        if (fs_delete(dst_parent, dst_name, dst_len, false) == 0)
        {
            rc = fs_write(dst_parent, dst_name, dst_len, SHELL_FILE_BUF, size, false);
        }
    }
    if (rc != 0)
    {
        print_error(cmd, dst_name, write_error_message(rc));
        return false;
    }
    return true;
}

/* Salin folder src_dir (rekursif) menjadi dst_parent/dst_name */
static bool copy_dir_recursive(const char *cmd, uint32_t src_dir, uint32_t dst_parent,
                               const char *dst_name, uint32_t dst_len)
{
    int8_t rc = fs_write(dst_parent, dst_name, dst_len, NULL, 0, true);
    if (rc != 0)
    {
        print_error(cmd, dst_name, write_error_message(rc));
        return false;
    }
    uint32_t new_dir;
    uint8_t type;
    if (!dir_lookup(dst_parent, dst_name, dst_len, &new_dir, &type))
    {
        print_error(cmd, dst_name, "Gagal membuat folder tujuan");
        return false;
    }

    uint8_t buf[SHELL_DIR_BUF_SIZE];
    if (fs_read_dir_inode(src_dir, buf) != 0)
    {
        print_error(cmd, NULL, "Gagal membaca folder sumber");
        return false;
    }
    bool ok = true;
    struct DirIter it;
    dir_iter_init(&it, buf);
    struct EXT2DirectoryEntry *entry;
    while ((entry = dir_iter_next(&it)) != NULL)
    {
        if (entry_is_dot(entry))
        {
            continue;
        }
        char *name = entry_name(entry);
        uint32_t len = entry->name_len;
        if (entry->file_type == EXT2_FT_DIR)
        {
            ok = copy_dir_recursive(cmd, entry->inode, new_dir, name, len) && ok;
        }
        else
        {
            ok = copy_file(cmd, src_dir, name, len, new_dir, name, len) && ok;
        }
    }
    return ok;
}

/* Hapus folder dir_inode beserta seluruh isinya, lalu hapus entry-nya dari parent */
static bool remove_recursive(const char *cmd, uint32_t parent, const char *name, uint32_t name_len, uint32_t dir_inode)
{
    uint8_t buf[SHELL_DIR_BUF_SIZE];
    if (fs_read_dir_inode(dir_inode, buf) != 0)
    {
        print_error(cmd, NULL, "Gagal membaca folder");
        return false;
    }
    bool ok = true;
    struct DirIter it;
    dir_iter_init(&it, buf);
    struct EXT2DirectoryEntry *entry;
    while ((entry = dir_iter_next(&it)) != NULL)
    {
        if (entry_is_dot(entry))
        {
            continue;
        }
        if (entry->file_type == EXT2_FT_DIR)
        {
            ok = remove_recursive(cmd, dir_inode, entry_name(entry), entry->name_len, entry->inode) && ok;
        }
        else if (fs_delete(dir_inode, entry_name(entry), entry->name_len, false) != 0)
        {
            ok = false;
        }
    }
    int8_t rc = fs_delete(parent, name, name_len, true);
    if (rc != 0)
    {
        print_error(cmd, NULL, "Gagal menghapus folder");
        return false;
    }
    return ok;
}

/* Ambil flag -r dari argumen, sisa argumen dipindahkan ke args */
static int collect_args(struct CmdCtx *ctx, bool *recursive, char **args, int max)
{
    int count = 0;
    *recursive = false;
    for (int i = 1; i < ctx->argc; i++)
    {
        if (str_eq(ctx->argv[i], "-r") || str_eq(ctx->argv[i], "-R"))
        {
            *recursive = true;
        }
        else if (count < max)
        {
            args[count++] = ctx->argv[i];
        }
        else
        {
            return -1;
        }
    }
    return count;
}

/**
 * Tentukan tujuan cp/mv: jika dst adalah folder yang sudah ada, tujuan = dst/<nama sumber>.
 * Selain itu tujuan = parent(dst)/<nama terakhir dst>.
 */
static const char *resolve_destination(const char *dst, const char *src_name, uint32_t src_len,
                                       uint32_t *dst_parent, const char **dst_name, uint32_t *dst_len)
{
    struct Cwd tmp;
    if (resolve_dir(dst, &tmp) == NULL)
    {
        *dst_parent = cwd_inode(&tmp);
        *dst_name = src_name;
        *dst_len = src_len;
        return NULL;
    }
    return resolve_parent(dst, dst_parent, dst_name, dst_len);
}

static void cmd_cp(struct CmdCtx *ctx)
{
    bool recursive;
    char *args[2];
    if (collect_args(ctx, &recursive, args, 2) != 2)
    {
        print_error("cp", NULL, "Penggunaan: cp [-r] <sumber> <tujuan>");
        return;
    }

    uint32_t src_parent, src_len, src_inode;
    const char *src_name;
    uint8_t src_type;
    const char *err = resolve_parent(args[0], &src_parent, &src_name, &src_len);
    if (err == NULL && !dir_lookup(src_parent, src_name, src_len, &src_inode, &src_type))
    {
        err = "File/folder tidak ditemukan";
    }
    if (err != NULL)
    {
        print_error("cp", args[0], err);
        return;
    }

    uint32_t dst_parent, dst_len;
    const char *dst_name;
    err = resolve_destination(args[1], src_name, src_len, &dst_parent, &dst_name, &dst_len);
    if (err != NULL)
    {
        print_error("cp", args[1], err);
        return;
    }

    if (src_type == EXT2_FT_DIR)
    {
        if (!recursive)
        {
            print_error("cp", args[0], "Adalah sebuah folder (gunakan cp -r)");
            return;
        }
        if (is_ancestor(src_inode, dst_parent))
        {
            print_error("cp", args[0], "Tidak bisa menyalin folder ke dalam dirinya sendiri");
            return;
        }
        copy_dir_recursive("cp", src_inode, dst_parent, dst_name, dst_len);
        return;
    }
    copy_file("cp", src_parent, src_name, src_len, dst_parent, dst_name, dst_len);
}

static void cmd_rm(struct CmdCtx *ctx)
{
    bool recursive;
    char *args[SHELL_MAX_TOKENS];
    int count = collect_args(ctx, &recursive, args, SHELL_MAX_TOKENS);
    if (count <= 0)
    {
        print_error("rm", NULL, "Penggunaan: rm [-r] <file/folder>...");
        return;
    }

    for (int i = 0; i < count; i++)
    {
        uint32_t parent, name_len, inode;
        const char *name;
        uint8_t type;
        const char *err = resolve_parent(args[i], &parent, &name, &name_len);
        if (err == NULL && !dir_lookup(parent, name, name_len, &inode, &type))
        {
            err = "File/folder tidak ditemukan";
        }
        if (err != NULL)
        {
            print_error("rm", args[i], err);
            continue;
        }

        if (type != EXT2_FT_DIR)
        {
            int8_t rc = fs_delete(parent, name, name_len, false);
            if (rc != 0)
            {
                print_error("rm", args[i], "Gagal menghapus file");
            }
            continue;
        }
        if (!recursive)
        {
            print_error("rm", args[i], "Adalah sebuah folder (gunakan rm -r)");
            continue;
        }
        if (is_ancestor(inode, cwd_inode(&cwd)))
        {
            print_error("rm", args[i], "Tidak bisa menghapus folder yang sedang/akan dibuka");
            continue;
        }
        remove_recursive("rm", parent, name, name_len, inode);
    }
}

static void cmd_mv(struct CmdCtx *ctx)
{
    if (ctx->argc != 3)
    {
        print_error("mv", NULL, "Penggunaan: mv <sumber> <tujuan>");
        return;
    }

    uint32_t src_parent, src_len, src_inode;
    const char *src_name;
    uint8_t src_type;
    const char *err = resolve_parent(ctx->argv[1], &src_parent, &src_name, &src_len);
    if (err == NULL && !dir_lookup(src_parent, src_name, src_len, &src_inode, &src_type))
    {
        err = "File/folder tidak ditemukan";
    }
    if (err != NULL)
    {
        print_error("mv", ctx->argv[1], err);
        return;
    }

    uint32_t dst_parent, dst_len;
    const char *dst_name;
    err = resolve_destination(ctx->argv[2], src_name, src_len, &dst_parent, &dst_name, &dst_len);
    if (err != NULL)
    {
        print_error("mv", ctx->argv[2], err);
        return;
    }
    if (src_parent == dst_parent && name_eq(src_name, src_len, dst_name, dst_len))
    {
        return; /* tujuan sama dengan sumber, tidak ada yang perlu dilakukan */
    }

    if (src_type == EXT2_FT_DIR)
    {
        if (is_ancestor(src_inode, dst_parent))
        {
            print_error("mv", ctx->argv[1], "Tidak bisa memindahkan folder ke dalam dirinya sendiri");
            return;
        }
        if (is_ancestor(src_inode, cwd_inode(&cwd)))
        {
            print_error("mv", ctx->argv[1], "Tidak bisa memindahkan folder yang sedang dibuka");
            return;
        }
        uint32_t existing;
        uint8_t type;
        if (dir_lookup(dst_parent, dst_name, dst_len, &existing, &type))
        {
            print_error("mv", ctx->argv[2], "Sudah ada file/folder dengan nama tersebut");
            return;
        }
        /* File system tidak punya operasi rename, jadi folder dipindah dengan salin + hapus */
        if (copy_dir_recursive("mv", src_inode, dst_parent, dst_name, dst_len))
        {
            remove_recursive("mv", src_parent, src_name, src_len, src_inode);
        }
        return;
    }

    if (copy_file("mv", src_parent, src_name, src_len, dst_parent, dst_name, dst_len))
    {
        if (fs_delete(src_parent, src_name, src_len, false) != 0)
        {
            print_error("mv", ctx->argv[1], "Gagal menghapus file sumber");
        }
    }
}

/* ============================== find ============================== */

static char find_path[SHELL_PATH_MAX];

static void find_recursive(struct CmdCtx *ctx, uint32_t dir, uint32_t path_len,
                           const char *target, uint32_t target_len, uint32_t depth)
{
    if (depth > 64)
    {
        return;
    }
    uint8_t buf[SHELL_DIR_BUF_SIZE];
    if (fs_read_dir_inode(dir, buf) != 0)
    {
        return;
    }
    struct DirIter it;
    dir_iter_init(&it, buf);
    struct EXT2DirectoryEntry *entry;
    while ((entry = dir_iter_next(&it)) != NULL)
    {
        if (entry_is_dot(entry))
        {
            continue;
        }
        uint32_t len = entry->name_len;
        if (path_len + 1 + len + 1 > SHELL_PATH_MAX)
        {
            continue;
        }
        find_path[path_len] = '/';
        memcpy(find_path + path_len + 1, entry_name(entry), len);
        uint32_t new_len = path_len + 1 + len;
        bool is_dir = entry->file_type == EXT2_FT_DIR;

        if (name_eq(entry_name(entry), len, target, target_len))
        {
            if (ctx->to_screen)
            {
                ob_color(ctx->out, is_dir ? COLOR_DIR : COLOR_TEXT);
            }
            ob_write(ctx->out, find_path, new_len);
            if (ctx->to_screen)
            {
                ob_color(ctx->out, COLOR_TEXT);
            }
            ob_putc(ctx->out, '\n');
        }
        if (is_dir)
        {
            find_recursive(ctx, entry->inode, new_len, target, target_len, depth + 1);
        }
    }
}

static void cmd_find(struct CmdCtx *ctx)
{
    if (ctx->argc != 2)
    {
        print_error("find", NULL, "Penggunaan: find <nama>");
        return;
    }
    /* Depth First Search dari root: EXT2 membentuk tree (Book: EXT2, Graph, Tree) */
    find_recursive(ctx, ROOT_INODE, 0, ctx->argv[1], str_len(ctx->argv[1]), 0);
}

/* ============================== grep (regex sederhana) ============================== */

/*
 * Regex sederhana (berdasarkan matcher Rob Pike):
 *   c   karakter literal     .   karakter apa saja
 *   ^   awal baris           $   akhir baris
 *   *   nol atau lebih karakter sebelumnya
 */
static bool re_match_here(const char *re, const char *text, const char *end);

static bool re_match_star(char c, const char *re, const char *text, const char *end)
{
    do
    {
        if (re_match_here(re, text, end))
        {
            return true;
        }
    } while (text < end && (*text++ == c || c == '.'));
    return false;
}

static bool re_match_here(const char *re, const char *text, const char *end)
{
    if (re[0] == '\0')
    {
        return true;
    }
    if (re[1] == '*')
    {
        return re_match_star(re[0], re + 2, text, end);
    }
    if (re[0] == '$' && re[1] == '\0')
    {
        return text == end;
    }
    if (text < end && (re[0] == '.' || re[0] == *text))
    {
        return re_match_here(re + 1, text + 1, end);
    }
    return false;
}

static bool re_match(const char *re, const char *text, const char *end)
{
    if (re[0] == '^')
    {
        return re_match_here(re + 1, text, end);
    }
    do
    {
        if (re_match_here(re, text, end))
        {
            return true;
        }
    } while (text++ < end);
    return false;
}

static void cmd_grep(struct CmdCtx *ctx)
{
    if (ctx->argc < 2 || ctx->argc > 3)
    {
        print_error("grep", NULL, "Penggunaan: grep <pola> [file]  atau  <command> | grep <pola>");
        return;
    }

    const char *data;
    uint32_t size;
    if (ctx->argc == 3)
    {
        uint32_t parent, name_len;
        const char *name;
        const char *err = resolve_parent(ctx->argv[2], &parent, &name, &name_len);
        if (err == NULL)
        {
            err = load_file(parent, name, name_len, &size);
        }
        if (err != NULL)
        {
            print_error("grep", ctx->argv[2], err);
            return;
        }
        data = (const char *)SHELL_FILE_BUF;
    }
    else if (ctx->in != NULL)
    {
        data = ctx->in;
        size = ctx->in_len;
    }
    else
    {
        print_error("grep", NULL, "Tidak ada input (berikan file atau gunakan pipeline)");
        return;
    }

    const char *pattern = ctx->argv[1];
    uint32_t start = 0;
    while (start < size)
    {
        uint32_t end = start;
        while (end < size && data[end] != '\n')
        {
            end++;
        }
        if (re_match(pattern, data + start, data + end))
        {
            ob_write(ctx->out, data + start, end - start);
            ob_putc(ctx->out, '\n');
        }
        start = end + 1;
    }
}

/* ============================== clear / help ============================== */

static void cmd_clear(struct CmdCtx *ctx)
{
    (void)ctx;
    syscall(SYSCALL_CLEAR_SCREEN, 0, 0, 0);
}

static void cmd_help(struct CmdCtx *ctx);

struct Command
{
    const char *name;
    void (*run)(struct CmdCtx *ctx);
    const char *usage;
};

static const struct Command commands[] = {
    {"cd", cmd_cd, "cd [folder]          pindah folder (.. untuk naik, tanpa argumen ke root)"},
    {"ls", cmd_ls, "ls [folder]          tampilkan isi folder"},
    {"mkdir", cmd_mkdir, "mkdir <nama>         buat folder baru"},
    {"cat", cmd_cat, "cat <file>           tampilkan isi file"},
    {"cp", cmd_cp, "cp [-r] <src> <dst>  salin file (-r untuk folder)"},
    {"rm", cmd_rm, "rm [-r] <path>       hapus file (-r untuk folder)"},
    {"mv", cmd_mv, "mv <src> <dst>       pindah / ganti nama file atau folder"},
    {"find", cmd_find, "find <nama>          cari file/folder di seluruh file system"},
    {"grep", cmd_grep, "grep <pola> [file]   cari baris yang cocok (regex: ^ $ . *)"},
    {"echo", cmd_echo, "echo <teks>          tulis teks (echo isi > file untuk membuat file)"},
    {"pwd", cmd_pwd, "pwd                  tampilkan folder sekarang"},
    {"clear", cmd_clear, "clear                bersihkan layar"},
    {"help", cmd_help, "help                 tampilkan daftar command"},
};

#define COMMAND_COUNT (sizeof(commands) / sizeof(commands[0]))

static void cmd_help(struct CmdCtx *ctx)
{
    ob_puts(ctx->out, "Command yang tersedia (pipeline: a | b, redirect: a > file / a >> file):\n");
    for (uint32_t i = 0; i < COMMAND_COUNT; i++)
    {
        ob_puts(ctx->out, "  ");
        ob_puts(ctx->out, commands[i].usage);
        ob_putc(ctx->out, '\n');
    }
}

/* ============================== PARSER & PIPELINE ============================== */

static char token_storage[SHELL_LINE_MAX * 2];

#define TOKEN_WORD     0
#define TOKEN_PIPE     1 /* |  */
#define TOKEN_REDIRECT 2 /* >  */
#define TOKEN_APPEND   3 /* >> */

/**
 * Pecah baris menjadi token. Spasi memisahkan token, "..." atau '...' untuk teks dengan spasi.
 * '|', '>', dan '>>' (di luar tanda kutip) menjadi token khusus (kind[i] != TOKEN_WORD).
 * Mengembalikan jumlah token, atau -1 jika tanda kutip tidak ditutup / token terlalu banyak.
 */
static int tokenize(const char *line, char **tokens, uint8_t *kind)
{
    int count = 0;
    uint32_t w = 0;
    uint32_t i = 0;
    while (line[i] != '\0')
    {
        while (line[i] == ' ' || line[i] == '\t')
        {
            i++;
        }
        if (line[i] == '\0')
        {
            break;
        }
        if (count >= SHELL_MAX_TOKENS)
        {
            return -1;
        }
        if (line[i] == '|' || line[i] == '>')
        {
            tokens[count] = token_storage + w;
            if (line[i] == '|')
            {
                kind[count] = TOKEN_PIPE;
            }
            else if (line[i + 1] == '>')
            {
                kind[count] = TOKEN_APPEND;
                token_storage[w++] = line[i++];
            }
            else
            {
                kind[count] = TOKEN_REDIRECT;
            }
            token_storage[w++] = line[i++];
            token_storage[w++] = '\0';
            count++;
            continue;
        }

        tokens[count] = token_storage + w;
        kind[count] = TOKEN_WORD;
        while (line[i] != '\0' && line[i] != ' ' && line[i] != '\t' && line[i] != '|' && line[i] != '>')
        {
            if (line[i] == '"' || line[i] == '\'')
            {
                char quote = line[i++];
                while (line[i] != '\0' && line[i] != quote)
                {
                    token_storage[w++] = line[i++];
                }
                if (line[i] != quote)
                {
                    return -1;
                }
                i++;
            }
            else
            {
                token_storage[w++] = line[i++];
            }
        }
        token_storage[w++] = '\0';
        count++;
    }
    return count;
}

static const struct Command *find_command(const char *name)
{
    for (uint32_t i = 0; i < COMMAND_COUNT; i++)
    {
        if (str_eq(commands[i].name, name))
        {
            return &commands[i];
        }
    }
    return NULL;
}

/* Tulis hasil pipeline ke file (redirect). append = true untuk >> */
static void write_output_file(const char *path, const char *data, uint32_t len, bool append)
{
    uint32_t parent, name_len, existing;
    const char *name;
    uint8_t type;
    const char *err = resolve_parent(path, &parent, &name, &name_len);
    if (err != NULL)
    {
        print_error("shell", path, err);
        return;
    }

    const char *content = data;
    uint32_t size = len;
    bool exists = dir_lookup(parent, name, name_len, &existing, &type);
    if (exists && type == EXT2_FT_DIR)
    {
        print_error("shell", path, "Adalah sebuah folder");
        return;
    }
    if (exists && append)
    {
        uint32_t old_size;
        err = load_file(parent, name, name_len, &old_size);
        if (err == NULL && old_size + len > SHELL_FILE_BUF_SIZE)
        {
            err = "File terlalu besar untuk buffer shell (maks 1 MiB)";
        }
        if (err != NULL)
        {
            print_error("shell", path, err);
            return;
        }
        memcpy(SHELL_FILE_BUF + old_size, data, len);
        content = (const char *)SHELL_FILE_BUF;
        size = old_size + len;
    }
    if (exists && fs_delete(parent, name, name_len, false) != 0)
    {
        print_error("shell", path, "Gagal menimpa file");
        return;
    }
    int8_t rc = fs_write(parent, name, name_len, (void *)content, size, false);
    if (rc != 0)
    {
        print_error("shell", path, write_error_message(rc));
    }
}

static void execute_line(const char *line)
{
    char *tokens[SHELL_MAX_TOKENS];
    uint8_t kind[SHELL_MAX_TOKENS];
    int count = tokenize(line, tokens, kind);
    if (count < 0)
    {
        print_error("shell", NULL, "Tanda kutip tidak ditutup atau argumen terlalu banyak");
        return;
    }
    if (count == 0)
    {
        return;
    }

    /* Redirect hanya boleh di akhir: ... > file atau ... >> file */
    const char *redirect_path = NULL;
    bool redirect_append = false;
    for (int i = 0; i < count; i++)
    {
        if (kind[i] == TOKEN_REDIRECT || kind[i] == TOKEN_APPEND)
        {
            if (i != count - 2 || kind[count - 1] != TOKEN_WORD || i == 0)
            {
                print_error("shell", NULL, "Redirect harus berbentuk: <command> > <file>");
                return;
            }
            redirect_path = tokens[count - 1];
            redirect_append = kind[i] == TOKEN_APPEND;
            count -= 2;
            break;
        }
    }

    /* Kelompokkan token per stage pipeline */
    int stage_start[SHELL_MAX_STAGES];
    int stage_argc[SHELL_MAX_STAGES];
    int stages = 0;
    int start = 0;
    for (int i = 0; i <= count; i++)
    {
        if (i == count || kind[i] == TOKEN_PIPE)
        {
            if (i == start)
            {
                print_error("shell", NULL, "Pipeline tidak lengkap di sekitar '|'");
                return;
            }
            if (stages >= SHELL_MAX_STAGES)
            {
                print_error("shell", NULL, "Pipeline terlalu panjang");
                return;
            }
            stage_start[stages] = start;
            stage_argc[stages] = i - start;
            stages++;
            start = i + 1;
        }
    }

    struct OutBuf bufs[2];
    ob_init(&bufs[0], SHELL_PIPE_BUF_A, SHELL_PIPE_BUF_SIZE);
    ob_init(&bufs[1], SHELL_PIPE_BUF_B, SHELL_PIPE_BUF_SIZE);

    for (int s = 0; s < stages; s++)
    {
        struct OutBuf *out = &bufs[s % 2];
        struct OutBuf *prev = &bufs[(s + 1) % 2];
        ob_init(out, out->data, out->cap);

        struct CmdCtx ctx;
        ctx.argc = stage_argc[s];
        ctx.argv = tokens + stage_start[s];
        ctx.out = out;
        ctx.to_screen = (s == stages - 1) && redirect_path == NULL;
        if (s > 0)
        {
            ob_strip_colors(prev);
            ctx.in = prev->data;
            ctx.in_len = prev->len;
        }
        else
        {
            ctx.in = NULL;
            ctx.in_len = 0;
        }

        const struct Command *command = find_command(ctx.argv[0]);
        if (command == NULL)
        {
            print_error(ctx.argv[0], NULL, "Command tidak ditemukan (ketik help)");
            return;
        }
        command->run(&ctx);
        if (s < stages - 1 && out->overflow)
        {
            print_error(ctx.argv[0], NULL, "Output terlalu besar untuk pipeline, sisanya dipotong");
        }
    }

    struct OutBuf *result = &bufs[(stages - 1) % 2];
    if (redirect_path != NULL)
    {
        ob_strip_colors(result);
        if (result->overflow)
        {
            print_error("shell", redirect_path, "Output terlalu besar, file tidak ditulis");
            return;
        }
        write_output_file(redirect_path, result->data, result->len, redirect_append);
        return;
    }
    ob_flush_to_screen(result);
}

/* ============================== REPL ============================== */

static void print_prompt(void)
{
    print("uvuwevwe_OSas", COLOR_USER);
    print(":", COLOR_TEXT);
    print(cwd_display(&cwd), COLOR_PATH);
    print("$ ", COLOR_TEXT);
}

static uint32_t read_line(char *line)
{
    uint32_t len = 0;
    while (true)
    {
        char c = sys_getchar();
        if (c == 0)
        {
            continue;
        }
        if (c == '\n')
        {
            sys_putc('\n', COLOR_TEXT);
            break;
        }
        if (c == '\b')
        {
            if (len > 0)
            {
                len--;
                sys_putc('\b', COLOR_TEXT);
            }
            continue;
        }
        if (c == '\t')
        {
            c = ' ';
        }
        if (c >= ' ' && c <= '~' && len < SHELL_LINE_MAX - 1)
        {
            line[len++] = c;
            sys_putc(c, COLOR_TEXT);
        }
    }
    line[len] = '\0';
    return len;
}

static void print_splash(void)
{
    print("                                             ___  ___\n", COLOR_DIR);
    print("  _  ___ ___  ___ __ _______ ____ __ _____  / _ \\/ __| __ _ ___\n", COLOR_DIR);
    print(" | || \\ V / || \\ V  V / -_) V /\\ V  V / -_)| (_) \\__ \\/ _` (_-<\n", COLOR_DIR);
    print("  \\_,_|\\_/ \\_,_|\\_/\\_/\\___|\\_/  \\_/\\_/\\___|_\\___/|___/\\__,_/__/\n", COLOR_DIR);
    print("                                         |___|\n", COLOR_DIR);
    print("\n  Shell IF2130 - ketik ", COLOR_TEXT);
    print("help", COLOR_INFO);
    print(" untuk daftar command\n\n", COLOR_TEXT);
}

int main(void)
{
    static char line[SHELL_LINE_MAX];

    syscall(SYSCALL_KEYBOARD_ACTIVATE, 0, 0, 0);
    syscall(SYSCALL_CLEAR_SCREEN, 0, 0, 0);
    cwd_reset(&cwd);
    print_splash();

    while (true)
    {
        print_prompt();
        read_line(line);
        execute_line(line);
    }
    return 0;
}
