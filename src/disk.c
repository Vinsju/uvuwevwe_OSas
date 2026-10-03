#include <stdint.h>
#include <stdbool.h>
#include "header/disk.h"
#include "header/cpu/portio.h"

#define ATA_PRIMARY_DATA        0x1F0
#define ATA_PRIMARY_SECTOR_CNT  0x1F2
#define ATA_PRIMARY_LBA_LOW     0x1F3
#define ATA_PRIMARY_LBA_MID     0x1F4
#define ATA_PRIMARY_LBA_HIGH    0x1F5
#define ATA_PRIMARY_DRIVE       0x1F6
#define ATA_PRIMARY_STATUS      0x1F7
#define ATA_PRIMARY_COMMAND     0x1F7

#define ATA_CMD_READ  0x20
#define ATA_CMD_WRITE 0x30

#define ATA_STATUS_BSY  0x80
#define ATA_STATUS_DRQ  0x08
#define ATA_STATUS_ERR  0x01

static void wait_disk(void) {
    uint8_t status;

    do {
        status = in(ATA_PRIMARY_STATUS);
    } while (status & ATA_STATUS_BSY);
}

static bool wait_for_drq(void) {
    uint8_t status;

    do {
        status = in(ATA_PRIMARY_STATUS);

        if (status & ATA_STATUS_ERR) {
            return false;
        }
    } while (!(status & ATA_STATUS_DRQ));

    return true;
}

static void send_lba(uint32_t lba) {
    out(ATA_PRIMARY_SECTOR_CNT, 1);

    out(ATA_PRIMARY_LBA_LOW, (uint8_t)(lba & 0xFF));
    out(ATA_PRIMARY_LBA_MID, (uint8_t)((lba >> 8) & 0xFF));
    out(ATA_PRIMARY_LBA_HIGH, (uint8_t)((lba >> 16) & 0xFF));

    out(ATA_PRIMARY_DRIVE, 0xE0 | ((lba >> 24) & 0x0F));
}

void read_blocks(uint32_t lba, uint8_t *buffer, uint32_t number_of_blocks) {
    for (uint32_t block = 0; block < number_of_blocks; block++) {

        send_lba(lba + block);

        out(ATA_PRIMARY_COMMAND, ATA_CMD_READ);

        wait_disk();

        if (!wait_for_drq()) {
            return;
        }

        for (uint32_t i = 0; i < BLOCK_SIZE / 2; i++) {
            uint16_t data = inw(ATA_PRIMARY_DATA);

            buffer[block * BLOCK_SIZE + i * 2] =
                (uint8_t)(data & 0xFF);

            buffer[block * BLOCK_SIZE + i * 2 + 1] =
                (uint8_t)((data >> 8) & 0xFF);
        }
    }
}

void write_blocks(uint32_t lba, uint8_t *buffer, uint32_t number_of_blocks) {
    for (uint32_t block = 0; block < number_of_blocks; block++) {

        send_lba(lba + block);

        out(ATA_PRIMARY_COMMAND, ATA_CMD_WRITE);

        wait_disk();

        if (!wait_for_drq()) {
            return;
        }

        for (uint32_t i = 0; i < BLOCK_SIZE / 2; i++) {
            uint16_t data =
                (uint16_t)buffer[block * BLOCK_SIZE + i * 2]
                | ((uint16_t)buffer[block * BLOCK_SIZE + i * 2 + 1] << 8);

            outw(ATA_PRIMARY_DATA, data);
        }

        wait_disk();
    }
}