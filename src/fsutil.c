#include "header/user/fsutil.h"

static uint32_t fs_strlen(const char *s) {
    uint32_t n = 0;
    while (s[n] != '\0') {
        n++;
    }
    return n;
}

static bool fs_mem_equal(const char *a, const char *b, uint32_t n) {
    for (uint32_t i=0; i<n; i++) {
        if (a[i] != b[i]) return false;
    }
    return true;
}

static void fs_join_path(char *out, const char *parent, const char *name, uint32_t name_len) {
    uint32_t p=0;

    for (uint32_t i=0; parent[i] != '\0' && p<FSUTIL_MAX_PATH-1; i++) {
        out[p++] = parent[i];
    }

    if (p == 0 || out[p-1] != '/') {
        if (p < FSUTIL_MAX_PATH-1) {
            out[p++] = '/';
        }
    }

    for (uint32_t i=0; i<name_len && p<FSUTIL_MAX_PATH-1; i++) {
        out[p++] = name[i];
    }

    out[p] = '\0';
}

struct FindFrame {
    uint32_t inode;
    char path[FSUTIL_MAX_PATH];
};

static struct FindFrame find_stack[FSUTIL_MAX_STACK];
static uint8_t          dir_buffer[FSUTIL_DIR_BUFFER];

uint32_t fs_find(const char *name, fsutil_read_dir_fn read_dir, fsutil_emit_fn emit) {
    uint32_t name_len = fs_strlen(name);
    uint32_t matches = 0;
    uint32_t top = 0;

    find_stack[top].inode = FSUTIL_ROOT_INODE;
    find_stack[top].path[0] = '\0';
    top++;

    while (top>0) {
        struct FindFrame current = find_stack[--top];

        for (uint32_t i=0; i<FSUTIL_DIR_BUFFER; i++) {
            dir_buffer[i] = 0;
        }

        struct EXT2DriverRequest request = {
            .buf = dir_buffer,
            .name = ".",
            .name_len = 1,
            .parent_inode = current.inode,
            .buffer_size = FSUTIL_DIR_BUFFER,
            .is_directory = true,
        };

        if (read_dir(&request) != 0) continue;
        
        uint32_t offset = 0;
        while (offset + sizeof(struct EXT2DirectoryEntry) <= FSUTIL_DIR_BUFFER) {
            struct EXT2DirectoryEntry *entry = (struct EXT2DirectoryEntry *)(dir_buffer + offset);

            if (entry->rec_len == 0) break;
            offset += entry->rec_len;

            if (entry->inode == 0) continue;

            char *entry_name = (char *) entry + sizeof(struct EXT2DirectoryEntry);
            uint32_t entry_len = entry->name_len;

            bool is_dot = (entry_len == 1 && entry_name[0] == '.') ||
                          (entry_len == 2 && entry_name[0] == '.' && entry_name[1] == '.');
            if (is_dot) continue;
 
            char path[FSUTIL_MAX_PATH];
            fs_join_path(path, current.path, entry_name, entry_len);
 
            // Cocok kalau panjang sama DAN semua karakter sama (exact match)
            if (entry_len == name_len && fs_mem_equal(entry_name, name, name_len)) {
                emit(path);
                matches++;
            }
 
            // Folder -> masukkan ke stack untuk ditelusuri nanti (push)
            if (entry->file_type == EXT2_FT_DIR && top < FSUTIL_MAX_STACK) {
                find_stack[top].inode = entry->inode;
                for (uint32_t i = 0; i < FSUTIL_MAX_PATH; i++) find_stack[top].path[i] = path[i];
                top++;
            }
        }
    }
    return matches;
}

#define GREP_MAX_LINE 256

uint32_t text_grep(const char *pattern, const char *text, uint32_t len, fsutil_emit_fn emit) {
    uint32_t pattern_len = fs_strlen(pattern);
    uint32_t matches     = 0;
    uint32_t line_start  = 0;   // indeks awal baris yang sedang diproses
 
    // i berjalan sampai len (inklusif) supaya baris terakhir tanpa '\n' tetap diproses
    for (uint32_t i = 0; i <= len; i++) {
        // Akhir baris: ketemu '\n', akhir teks, atau '\0' (buffer file biasanya
        // lebih besar dari isinya dan sisanya bernilai 0)
        bool line_end = (i == len) || text[i] == '\n' || text[i] == '\0';
        if (!line_end) continue;
 
        uint32_t line_len = i - line_start;
        const char *line  = text + line_start;
 
        // Coba cocokkan pola di setiap posisi j dalam baris
        bool found = false;
        for (uint32_t j = 0; j + pattern_len <= line_len; j++) {
            if (fs_mem_equal(line + j, pattern, pattern_len)) {
                found = true;
                break;
            }
        }
 
        // Baris kosong tidak ditampilkan (kecuali polanya kosong)
        if (found && (line_len > 0 || pattern_len == 0)) {
            // Salin baris ke buffer sendiri + '\0', karena emit butuh string C
            char out[GREP_MAX_LINE];
            uint32_t n = line_len < GREP_MAX_LINE - 1 ? line_len : GREP_MAX_LINE - 1;
            for (uint32_t k = 0; k < n; k++) out[k] = line[k];
            out[n] = '\0';
            emit(out);
            matches++;
        }
 
        if (i < len && text[i] == '\0') break;   // '\0' = isi file sudah habis
        line_start = i + 1;                      // baris berikutnya mulai setelah '\n'
    }
    return matches;
}
