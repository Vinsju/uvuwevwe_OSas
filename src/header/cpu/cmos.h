#ifndef _CMOS_H
#define _CMOS_H

#include <stdint.h>
#include <stdbool.h>

/**
 * Driver CMOS Real-Time Clock (RTC), Book 4.3.3.
 * Referensi: https://wiki.osdev.org/CMOS
 *
 * CMOS diakses lewat 2 port I/O:
 * - CMOS_ADDRESS_PORT (0x70): pilih nomor register (bit 7 = matikan NMI)
 * - CMOS_DATA_PORT    (0x71): baca isi register yang dipilih
 */
#define CMOS_ADDRESS_PORT 0x70
#define CMOS_DATA_PORT    0x71
#define CMOS_NMI_DISABLE  0x80

// Register RTC
#define CMOS_REG_SECOND   0x00
#define CMOS_REG_MINUTE   0x02
#define CMOS_REG_HOUR     0x04
#define CMOS_REG_DAY      0x07
#define CMOS_REG_MONTH    0x08
#define CMOS_REG_YEAR     0x09
#define CMOS_REG_STATUS_A 0x0A
#define CMOS_REG_STATUS_B 0x0B

#define CMOS_STATUS_A_UPDATE_IN_PROGRESS 0x80 // RTC sedang memperbarui waktu, nilai belum stabil
#define CMOS_STATUS_B_24_HOUR            0x02 // 1 = format 24 jam, 0 = format 12 jam
#define CMOS_STATUS_B_BINARY             0x04 // 1 = biner, 0 = BCD
#define CMOS_HOUR_PM                     0x80 // Bit PM pada register jam (format 12 jam)

/**
 * Waktu dari RTC, sudah dikonversi ke biner dan format 24 jam.
 * Struct ini juga dipakai user program lewat syscall SYSCALL_GET_TIME.
 *
 * @param second 0 - 59
 * @param minute 0 - 59
 * @param hour   0 - 23
 * @param day    1 - 31
 * @param month  1 - 12
 * @param year   Tahun lengkap, misal 2026
 */
struct CMOSTime {
    uint8_t  second;
    uint8_t  minute;
    uint8_t  hour;
    uint8_t  day;
    uint8_t  month;
    uint16_t year;
} __attribute__((packed));

/**
 * Baca waktu sekarang dari CMOS RTC.
 * Pembacaan diulang sampai dua kali berturut-turut sama supaya tidak membaca
 * nilai setengah jadi ketika RTC sedang update.
 *
 * @param time Tempat hasil pembacaan
 */
void cmos_read_time(struct CMOSTime *time);

#endif
