#ifndef _FSUTIL_H
#define _FSUTIL_H

#include <stdint.h>
#include <stdbool.h>
#include "../filesystem/ext2.h"


#define FSUTIL_MAX_PATH   128              
#define FSUTIL_MAX_STACK  64                
#define FSUTIL_DIR_BUFFER (BLOCK_SIZE * 12) 
#define FSUTIL_ROOT_INODE 1              


typedef int8_t (*fsutil_read_dir_fn)(struct EXT2DriverRequest *request);

typedef void (*fsutil_emit_fn)(const char *text);


uint32_t fs_find(const char *name, fsutil_read_dir_fn read_dir, fsutil_emit_fn emit);


uint32_t text_grep(const char *pattern, const char *text, uint32_t len, fsutil_emit_fn emit);

#endif