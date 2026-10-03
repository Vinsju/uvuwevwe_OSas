#ifndef _DISK_H
#define _DISK_H

#include <stdint.h>

#define BLOCK_SIZE 512

void read_blocks(uint32_t lba, uint8_t *buffer, uint32_t number_of_blocks);
void write_blocks(uint32_t lba, uint8_t *buffer, uint32_t number_of_blocks);

#endif