#include <hw/rtc.h>
#include <arch/x86_64/io.h>
#include <acpi/fadt.h>
#include <xlibc/stdio.h>

static u64 g_boot_unix_ns;

static const u64 NS_PER_SEC         = 1000000000ULL;

static const u16 CMOS_ADDR          = 0x70;
static const u16 CMOS_DATA          = 0x71;
static const u8 NMI_DISABLE         = 0x80;

static const u8 REG_SECONDS         = 0x00;
static const u8 REG_MINUTES         = 0x02;
static const u8 REG_HOURS           = 0x04;
static const u8 REG_DAY             = 0x07;
static const u8 REG_MONTH           = 0x08;
static const u8 REG_YEAR            = 0x09;
static const u8 REG_STATUS_A        = 0x0A;
static const u8 REG_STATUS_B        = 0x0B;

static const u8 STATUS_A_UIP        = 0x80;
static const u8 STATUS_B_24HR       = 0x02;
static const u8 STATUS_B_BINARY     = 0x04;

/**
 * @brief Convert a Gregorian date to Unix epoch seconds
 * @note Uses the Linux mktime64 algorithm
 * @param year  Full year (e.g. 2026)
 * @param mon   Month 1-12
 * @param day   Day of month 1-31
 * @param hour  Hour 0-23
 * @param min   Minute 0-59
 * @param sec   Second 0-59
 * @return Seconds since 1970-01-01 00:00:00 UTC.
 */
static u64 date_to_unix(u32 year, u32 mon, u32 day, u32 hour, u32 min, u32 sec) {
    u32 y = year;
    u32 m = mon;

    // shift Jan/Feb to months 11/12 of the previous year (puts Feb last so leap day doesn't split the year boundary in the formula)
    if (m <= 2) {
        m += 10;
        y -= 1;
    } else {
        m -= 2;
    }

    i64 days = (i64)(y / 4 - y / 100 + y / 400 + 367 * m / 12 + day) + (i64)(y) * 365 - 719499;

    return (u64)(((days * 24 + hour) * 60 + min) * 60 + sec);
}

__PRIVILEGED_CODE static __hint_inline__ u8 cmos_read(u8 reg) {
    x64_outb(CMOS_ADDR, reg | NMI_DISABLE);
    return x64_inb(CMOS_DATA);
}

static __hint_inline__ u8 bcd_to_bin(u8 val) {
    return (u8)((val & 0x0F) + (val >> 4) * 10);
}

/**
 * @brief Reads the FADT century register index
 * @returns 0 if FADT is unavailable or the field is zero
 */
__PRIVILEGED_CODE static u8 get_fadt_century_register() {
    const fadt_table_t* fadt = fadt_get();
    if (!fadt) {
        return 0;
    }

    return fadt->century;
}

struct rtc_time {
    u8 sec, min, hr, day, mon, year, century;
};

/**
 * @brief Read all RTC time registers in one pass
 */
__PRIVILEGED_CODE static void read_rtc_raw(struct rtc_time* t, u8 century_reg) {
    t->sec = cmos_read(REG_SECONDS);
    t->min = cmos_read(REG_MINUTES);
    t->hr = cmos_read(REG_HOURS);
    t->day = cmos_read(REG_DAY);
    t->mon = cmos_read(REG_MONTH);
    t->year = cmos_read(REG_YEAR);
    t->century = century_reg ? cmos_read(century_reg) : 0;
}

__PRIVILEGED_CODE b8 rtc_init() {
    u8 century_reg = get_fadt_century_register();

    // double-read for consistency two consecutive reads must match to ensure didn't no read across an RTC update cycle (~244 us window)
    struct rtc_time t1, t2;
    while (cmos_read(REG_STATUS_A) & STATUS_A_UIP) {}
    read_rtc_raw(&t1, century_reg);
    for (;;) {
        while (cmos_read(REG_STATUS_A) & STATUS_A_UIP) {}
        read_rtc_raw(&t2, century_reg);
        if (t1.sec == t2.sec && t1.min == t2.min && t1.hr == t2.hr &&
            t1.day == t2.day && t1.mon == t2.mon && t1.year == t2.year &&
            t1.century == t2.century)
            break;
        t1 = t2;
    }

    u8 status_b = cmos_read(REG_STATUS_B);
    b8 is_binary = (status_b & STATUS_B_BINARY) != 0;
    b8 is_24hr = (status_b & STATUS_B_24HR) != 0;

    u8 sec = t2.sec;
    u8 min = t2.min;
    u8 hr = t2.hr;

    u8 day = t2.day;
    u8 mon = t2.mon;
    u8 year = t2.year;
    u8 cent = t2.century;

    if (!is_binary) {
        sec = bcd_to_bin(sec);
        min = bcd_to_bin(min);
        hr = bcd_to_bin((u8)(hr & 0x7F));
        day = bcd_to_bin(day);
        mon = bcd_to_bin(mon);
        year = bcd_to_bin(year);

        if (cent) {
            cent = bcd_to_bin(cent);
        }
    } else {
        hr = (u8)(hr & 0x7F);
    }

    if (!is_24hr) {
        b8 pm = (t2.hr & 0x80) != 0;
        hr = (u8)(hr % 12);
        if (pm) {
            hr = (u8)(hr + 12);
        }
    }

    uint32_t full_year;
    if (cent) {
        full_year = (u32)(cent) * 100 + year;
    } else {
        full_year = (year < 70) ? 2000u + year : 1900u + year;
    }

    u64 epoch_sec = date_to_unix(full_year, mon, day, hr, min, sec);
    g_boot_unix_ns = epoch_sec * NS_PER_SEC;

    printf(
        "[RTC]: %u-%02u-%02u %02u:%02u:%02u UTC (epoch=%lu)\n",
        full_year, (u32)(mon), (u32)(day),
        (u32)(hr), (u32)(min),
        (u32)(sec), epoch_sec
    );

    return true;
}

u64 rtc_boot_unix_ns() {
    return g_boot_unix_ns;
}