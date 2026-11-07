#include "header/cpu/cmos.h"
#include "header/cpu/portio.h"

static uint8_t cmos_read_register(uint8_t reg) {
    out(CMOS_ADDRESS_PORT, CMOS_NMI_DISABLE | reg);
    return in(CMOS_DATA_PORT);
}

static bool cmos_update_in_progress(void) {
    return (cmos_read_register(CMOS_REG_STATUS_A) & CMOS_STATUS_A_UPDATE_IN_PROGRESS) != 0;
}

static uint8_t bcd_to_binary(uint8_t value) {
    return (uint8_t)((value & 0x0F) + (value >> 4) * 10);
}

/* Baca register mentah (masih mengikuti format RTC: BCD / biner, 12 / 24 jam) */
static void cmos_read_raw(struct CMOSTime *raw) {
    while (cmos_update_in_progress());
    raw->second = cmos_read_register(CMOS_REG_SECOND);
    raw->minute = cmos_read_register(CMOS_REG_MINUTE);
    raw->hour   = cmos_read_register(CMOS_REG_HOUR);
    raw->day    = cmos_read_register(CMOS_REG_DAY);
    raw->month  = cmos_read_register(CMOS_REG_MONTH);
    raw->year   = cmos_read_register(CMOS_REG_YEAR);
}

static bool cmos_time_equal(const struct CMOSTime *a, const struct CMOSTime *b) {
    return a->second == b->second && a->minute == b->minute && a->hour == b->hour
        && a->day == b->day && a->month == b->month && a->year == b->year;
}

void cmos_read_time(struct CMOSTime *time) {
    struct CMOSTime first, second;

    // Ulangi sampai dua pembacaan sama: hindari nilai campuran saat RTC update
    cmos_read_raw(&second);
    do {
        first = second;
        cmos_read_raw(&second);
    } while (!cmos_time_equal(&first, &second));

    uint8_t status_b = cmos_read_register(CMOS_REG_STATUS_B);
    bool is_pm = (second.hour & CMOS_HOUR_PM) != 0;
    second.hour &= (uint8_t) ~CMOS_HOUR_PM;

    if (!(status_b & CMOS_STATUS_B_BINARY)) {
        second.second = bcd_to_binary(second.second);
        second.minute = bcd_to_binary(second.minute);
        second.hour   = bcd_to_binary(second.hour);
        second.day    = bcd_to_binary(second.day);
        second.month  = bcd_to_binary(second.month);
        second.year   = bcd_to_binary((uint8_t) second.year);
    }

    // Format 12 jam: 12 AM = 00, 1 - 11 PM = 13 - 23
    if (!(status_b & CMOS_STATUS_B_24_HOUR)) {
        if (second.hour == 12) {
            second.hour = 0;
        }
        if (is_pm) {
            second.hour += 12;
        }
    }

    second.year += 2000; // Register tahun hanya 2 digit
    *time = second;
}
