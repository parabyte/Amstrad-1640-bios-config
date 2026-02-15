/*
 * nvr.c - Amstrad PC1640 NVR (Non-Volatile RAM) Configuration Utility
 *
 * Target: ELKS (Embeddable Linux Kernel Subset) on real Amstrad PC1640 hardware
 * Compiler: ia16-elf-gcc (ELKS cross-toolchain)
 *
 * Comprehensive system configuration, diagnostics, and hardware probe tool
 * covering every feature the PC1640 BIOS ROM exposes.
 *
 * Hardware Reference:
 *   Reverse-engineered from Amstrad PC1640 BIOS ROMs 40043.v3 / 40044.v3
 *   Verified against PCem emulator source (sarah-walker-pcem/pcem)
 *
 * I/O Port Map (PC1640-specific):
 *   0x60      - Keyboard scancode (PB.7=0) / System status 1 (PB.7=1)
 *   0x61      - PB register: speaker, nibble select, kbd reset, status mode
 *   0x62      - System status 2 / NVR nibble read (PB.2 selects nibble)
 *              Bit 5: speaker output state, Bit 6: NMI status
 *   0x64      - System status 1 latch (write)
 *   0x65      - System status 2 latch / NVR address (write)
 *   0x66      - Soft reset trigger (write any value)
 *   0x70      - MC146818 RTC/CMOS address register (write)
 *   0x71      - MC146818 RTC/CMOS data register (read/write)
 *   0x78      - Amstrad mouse X counter (read/write-to-reset)
 *   0x7A      - Amstrad mouse Y counter (read/write-to-reset)
 *   0x0378    - LPT1 data (read OR'd with language bits 0-2)
 *   0x0379    - LPT1 status: bits 0-2=language, bit 5=DIP latch, bits 6-7=display type
 *   0x037A    - LPT1 control
 *   0x03DE    - IDA disabled flag (reads 0x20 when internal display off)
 *   0xDEAD    - Dead-man diagnostic port (POST progress)
 *
 * (C) 2026 - Public Domain
 * Written for preservation of Amstrad PC1640 hardware
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ================================================================
 * Port I/O Primitives
 * ================================================================ */

static unsigned char inb(unsigned short port)
{
    unsigned char val;
    __asm__ volatile ("inb %%dx, %%al" : "=a" (val) : "d" (port));
    return val;
}

static void outb(unsigned char val, unsigned short port)
{
    __asm__ volatile ("outb %%al, %%dx" : : "a" (val), "d" (port));
}

/* Short delay for I/O bus settling (~1us on 8MHz 8086) */
static void io_delay(void)
{
    __asm__ volatile ("outb %%al, $0x80" : : "a" (0));
}

/* ================================================================
 * MC146818 RTC/CMOS Access (ports 0x70/0x71)
 * ================================================================ */

#define CMOS_ADDR_PORT  0x70
#define CMOS_DATA_PORT  0x71
#define CMOS_SIZE       64      /* PC1640: 64-byte CMOS only (mask 0x3F) */

/* RTC time/date registers */
#define RTC_SECONDS      0x00
#define RTC_ALARM_SEC    0x01
#define RTC_MINUTES      0x02
#define RTC_ALARM_MIN    0x03
#define RTC_HOURS        0x04
#define RTC_ALARM_HRS    0x05
#define RTC_DAY_OF_WEEK  0x06
#define RTC_DAY_OF_MONTH 0x07
#define RTC_MONTH        0x08
#define RTC_YEAR         0x09

/* RTC status registers */
#define RTC_REG_A        0x0A
#define RTC_REG_B        0x0B
#define RTC_REG_C        0x0C   /* Read-only, clears IRQ flags on read */
#define RTC_REG_D        0x0D   /* Read-only, VRT (battery) flag */

/* Status Register A bits */
#define RTC_A_UIP        0x80   /* Update In Progress */
#define RTC_A_DV_MASK    0x70   /* Divider select (oscillator) */
#define RTC_A_DV_SHIFT   4
#define RTC_A_RS_MASK    0x0F   /* Rate select (periodic interrupt) */

/* Status Register B bits */
#define RTC_B_SET        0x80   /* SET - stop updates for safe read/write */
#define RTC_B_PIE        0x40   /* Periodic Interrupt Enable */
#define RTC_B_AIE        0x20   /* Alarm Interrupt Enable */
#define RTC_B_UIE        0x10   /* Update-ended Interrupt Enable */
#define RTC_B_SQWE       0x08   /* Square Wave Enable (SQW pin) */
#define RTC_B_DM         0x04   /* Data Mode: 1=binary, 0=BCD */
#define RTC_B_24H        0x02   /* 24-hour mode */
#define RTC_B_DSE        0x01   /* Daylight Savings Enable */

/* Status Register C bits */
#define RTC_C_IRQF       0x80   /* IRQ flag (composite) */
#define RTC_C_PF         0x40   /* Periodic interrupt flag */
#define RTC_C_AF         0x20   /* Alarm flag */
#define RTC_C_UF         0x10   /* Update-ended flag */

/* Status Register D bits */
#define RTC_D_VRT        0x80   /* Valid RAM and Time (battery OK) */

/* CMOS configuration addresses */
#define CMOS_DIAG        0x0E   /* Diagnostic status byte (POST results) */
#define CMOS_SHUTDOWN    0x0F   /* Shutdown status byte */
#define CMOS_FLOPPY      0x10   /* Floppy drive types (hi=A, lo=B) */
#define CMOS_RSVD_11     0x11   /* Reserved */
#define CMOS_DISK        0x12   /* Hard disk types (hi=drv0, lo=drv1) */
#define CMOS_RSVD_13     0x13   /* Reserved */
#define CMOS_EQUIP       0x14   /* Equipment byte */
#define CMOS_BASEMEM_LO  0x15   /* Base memory low byte (KB) */
#define CMOS_BASEMEM_HI  0x16   /* Base memory high byte */
#define CMOS_EXTMEM_LO   0x17   /* Extended memory low byte (KB) */
#define CMOS_EXTMEM_HI   0x18   /* Extended memory high byte */
#define CMOS_DISK0_EXT   0x19   /* Hard disk 0 extended type (if 0x12 hi=0xF) */
#define CMOS_DISK1_EXT   0x1A   /* Hard disk 1 extended type (if 0x12 lo=0xF) */
#define CMOS_CHECKSUM_HI 0x2E   /* CMOS checksum high byte */
#define CMOS_CHECKSUM_LO 0x2F   /* CMOS checksum low byte */
#define CMOS_CENTURY     0x32   /* Century (BCD, e.g. 0x20) */

/* ================================================================
 * Amstrad System Ports
 * ================================================================ */

#define PORT_KBD_DATA       0x60   /* Keyboard data / system status 1 */
#define PORT_PB             0x61   /* PB register */
#define PORT_STATUS2        0x62   /* System status 2 (nibble-selected read) */
#define PORT_SYSSTAT1_WR    0x64   /* System status 1 latch (write) */
#define PORT_SYSSTAT2_WR    0x65   /* System status 2 / NVR latch (write) */
#define PORT_SOFT_RESET     0x66   /* Soft reset trigger (write) */
#define PORT_MOUSE_X        0x78   /* Amstrad mouse X counter */
#define PORT_MOUSE_Y        0x7A   /* Amstrad mouse Y counter */

/* PB register bits (port 0x61) */
#define PB_SPEAKER_GATE     0x01   /* PIT channel 2 gate */
#define PB_SPEAKER_ENABLE   0x02   /* Speaker amplifier enable */
#define PB_NIBBLE_SEL       0x04   /* 0=high nibble, 1=low nibble */
#define PB_KBD_RESET        0x40   /* Keyboard reset (rising edge) */
#define PB_STATUS_MODE      0x80   /* 0=keyboard data, 1=system status */

/* Standard PC ports */
#define PORT_PIC_CMD        0x20   /* 8259A PIC command */
#define PORT_PIC_DATA       0x21   /* 8259A PIC data (IMR) */
#define PORT_NMI_MASK       0xA0   /* NMI mask register */
#define PORT_PIT_CH2        0x42   /* 8253 PIT channel 2 count */
#define PORT_PIT_MODE       0x43   /* 8253 PIT mode control */
#define PORT_DMA_STAT       0x08   /* 8237A DMA status */

/* Serial/Parallel port addresses */
#define PORT_COM1_BASE      0x3F8
#define PORT_COM2_BASE      0x2F8
#define PORT_LPT1_DATA      0x378
#define PORT_LPT1_STATUS    0x379
#define PORT_LPT1_CTRL      0x37A
#define PORT_LPT2_DATA      0x3BC

/* Video ports */
#define PORT_CRTC_ADDR_CGA  0x3D4
#define PORT_CRTC_DATA_CGA  0x3D5
#define PORT_CGA_MODE       0x3D8
#define PORT_CGA_STATUS     0x3DA
#define PORT_VID_SWITCH     0x3DB   /* PC1640: bit 6 = CGA/EGA toggle */
#define PORT_VID_EXT        0x3DD   /* Amstrad extended video */
#define PORT_IDA_STATUS     0x3DE   /* IDA disabled flag (0x20 = off) */

/* Game port */
#define PORT_GAME           0x201

/* Dead-man diagnostic port */
#define PORT_DEAD           0xDEAD

/* LPT1 status register bit fields (PC1640-specific) */
#define LPT1_LANG_MASK      0x07   /* Bits 0-2: language code */
#define LPT1_DIP_LATCH      0x20   /* Bit 5: DIP switch latch */
#define LPT1_DISP_MASK      0xC0   /* Bits 6-7: display type */
#define LPT1_DISP_SHIFT     6

/* ================================================================
 * Debug Verbosity
 * ================================================================ */

static int debug_level = 0;

#define DBG(level, ...) do { \
    if (debug_level >= (level)) { \
        fprintf(stderr, "[DBG%d] ", (level)); \
        fprintf(stderr, __VA_ARGS__); \
    } \
} while(0)

/* ================================================================
 * CMOS Read/Write with PC1640 Masking
 * ================================================================ */

static unsigned char cmos_read(unsigned char addr)
{
    unsigned char val;
    addr &= 0x3F;  /* PC1640: 64-byte CMOS only */
    outb(addr, CMOS_ADDR_PORT);
    io_delay();
    val = inb(CMOS_DATA_PORT);
    DBG(3, "cmos_read(0x%02X) = 0x%02X\n", addr, val);
    return val;
}

static void cmos_write(unsigned char addr, unsigned char val)
{
    addr &= 0x3F;
    DBG(3, "cmos_write(0x%02X, 0x%02X)\n", addr, val);
    outb(addr, CMOS_ADDR_PORT);
    io_delay();
    outb(val, CMOS_DATA_PORT);
    io_delay();
}

/* ================================================================
 * BCD Conversion Helpers
 * ================================================================ */

static unsigned char bcd_to_bin(unsigned char bcd)
{
    return ((bcd >> 4) * 10) + (bcd & 0x0F);
}

static unsigned char bin_to_bcd(unsigned char bin)
{
    return ((bin / 10) << 4) | (bin % 10);
}

static int rtc_is_bcd(void)
{
    return !(cmos_read(RTC_REG_B) & RTC_B_DM);
}

static unsigned char rtc_to_bin(unsigned char val)
{
    return rtc_is_bcd() ? bcd_to_bin(val) : val;
}

static unsigned char bin_to_rtc(unsigned char val)
{
    return rtc_is_bcd() ? bin_to_bcd(val) : val;
}

/* Wait for RTC update cycle to complete */
static void rtc_wait_uip(void)
{
    int timeout = 10000;
    while ((cmos_read(RTC_REG_A) & RTC_A_UIP) && --timeout > 0)
        ;
    if (timeout == 0)
        DBG(1, "WARNING: RTC UIP timeout\n");
}

/* ================================================================
 * Amstrad System Status Access
 * ================================================================ */

/*
 * Read system status 2 via port 0x62 with nibble protocol.
 * From BIOS ROM disassembly at 0x0465-0x047B.
 *
 * PB bit 2 = 0: read high nibble (bits 7-4 of latch, returned as bits 3-0)
 * PB bit 2 = 1: read low nibble (bits 3-0 of latch, returned as bits 3-0)
 */
static unsigned char amstrad_read_sysstat2(void)
{
    unsigned char pb, hi, lo;

    pb = inb(PORT_PB);
    outb(pb & ~PB_NIBBLE_SEL, PORT_PB);
    io_delay();
    hi = inb(PORT_STATUS2) & 0x0F;

    outb(pb | PB_NIBBLE_SEL, PORT_PB);
    io_delay();
    lo = inb(PORT_STATUS2) & 0x0F;

    outb(pb, PORT_PB);  /* restore */

    DBG(2, "sysstat2: hi=0x%X lo=0x%X => 0x%02X\n", hi, lo, (hi << 4) | lo);
    return (hi << 4) | lo;
}

/*
 * Read system status 1 via port 0x60 with PB bit 7.
 * From BIOS ROM at 0x03F3-0x040F.
 */
static unsigned char amstrad_read_sysstat1(void)
{
    unsigned char pb, val;

    pb = inb(PORT_PB);
    outb(pb | PB_STATUS_MODE, PORT_PB);
    io_delay();
    val = inb(PORT_KBD_DATA);
    outb(pb, PORT_PB);

    DBG(2, "sysstat1: 0x%02X\n", val);
    return val;
}

/* ================================================================
 * CMOS Checksum (bytes 0x10-0x2D)
 * ================================================================ */

static unsigned short cmos_calc_checksum(void)
{
    unsigned short sum = 0;
    int i;
    for (i = 0x10; i <= 0x2D; i++)
        sum += cmos_read(i);
    DBG(2, "Calculated checksum: 0x%04X\n", sum);
    return sum;
}

static int cmos_verify_checksum(void)
{
    unsigned short calc, stored;
    calc = cmos_calc_checksum();
    stored = ((unsigned short)cmos_read(CMOS_CHECKSUM_HI) << 8) |
             cmos_read(CMOS_CHECKSUM_LO);
    DBG(1, "Checksum stored=0x%04X calc=0x%04X\n", stored, calc);
    return (calc == stored);
}

static void cmos_update_checksum(void)
{
    unsigned short sum = cmos_calc_checksum();
    cmos_write(CMOS_CHECKSUM_HI, (sum >> 8) & 0xFF);
    cmos_write(CMOS_CHECKSUM_LO, sum & 0xFF);
    DBG(1, "Checksum updated to 0x%04X\n", sum);
}

/* ================================================================
 * Display: Time & Date
 * ================================================================ */

static const char *day_names[] = {
    "???", "Sunday", "Monday", "Tuesday",
    "Wednesday", "Thursday", "Friday", "Saturday"
};

static const char *month_names[] = {
    "???", "January", "February", "March", "April",
    "May", "June", "July", "August",
    "September", "October", "November", "December"
};

static void show_time(void)
{
    unsigned char sec, min, hrs, dow, dom, mon, yr, cen;
    unsigned char regb;

    rtc_wait_uip();

    sec = cmos_read(RTC_SECONDS);
    min = cmos_read(RTC_MINUTES);
    hrs = cmos_read(RTC_HOURS);
    dow = cmos_read(RTC_DAY_OF_WEEK);
    dom = cmos_read(RTC_DAY_OF_MONTH);
    mon = cmos_read(RTC_MONTH);
    yr  = cmos_read(RTC_YEAR);
    cen = cmos_read(CMOS_CENTURY);
    regb = cmos_read(RTC_REG_B);

    DBG(2, "Raw: sec=%02X min=%02X hrs=%02X dow=%02X dom=%02X "
           "mon=%02X yr=%02X cen=%02X regB=%02X\n",
           sec, min, hrs, dow, dom, mon, yr, cen, regb);

    sec = rtc_to_bin(sec);
    min = rtc_to_bin(min);
    dom = rtc_to_bin(dom);
    mon = rtc_to_bin(mon);
    yr  = rtc_to_bin(yr);
    cen = rtc_to_bin(cen);
    dow = rtc_to_bin(dow);

    if (regb & RTC_B_24H) {
        hrs = rtc_to_bin(hrs);
    } else {
        int pm = hrs & 0x80;
        hrs &= 0x7F;
        hrs = rtc_to_bin(hrs);
        if (pm) { if (hrs < 12) hrs += 12; }
        else    { if (hrs == 12) hrs = 0;  }
    }

    if (dow > 7) dow = 0;
    if (mon > 12) mon = 0;

    printf("Date: %s %d %s %d%02d\n",
           day_names[dow], dom, month_names[mon], cen, yr);
    printf("Time: %02d:%02d:%02d\n", hrs, min, sec);
    printf("Mode: %s, %s\n",
           (regb & RTC_B_24H) ? "24-hour" : "12-hour",
           (regb & RTC_B_DM) ? "Binary" : "BCD");
}

/* ================================================================
 * Display: RTC Status Registers
 * ================================================================ */

/* Periodic interrupt rate table (from MC146818 datasheet) */
static const char *rate_freq[] = {
    "None",        /* 0 */
    "256 Hz",      /* 1 - 3.90625ms */
    "128 Hz",      /* 2 - 7.8125ms */
    "8192 Hz",     /* 3 - 122.070us */
    "4096 Hz",     /* 4 */
    "2048 Hz",     /* 5 */
    "1024 Hz",     /* 6 */
    "512 Hz",      /* 7 */
    "256 Hz",      /* 8 */
    "128 Hz",      /* 9 */
    "64 Hz",       /* 10 */
    "32 Hz",       /* 11 */
    "16 Hz",       /* 12 */
    "8 Hz",        /* 13 */
    "4 Hz",        /* 14 */
    "2 Hz"         /* 15 */
};

static const char *divider_name[] = {
    "4.194304 MHz (time base)", /* 0 */
    "1.048576 MHz",             /* 1 */
    "32.768 kHz",               /* 2 - standard crystal */
    "Test: any",                /* 3 */
    "Test: any",                /* 4 */
    "Reset / divider held",     /* 5 */
    "Reset / divider held",     /* 6 */
    "Reset / divider held"      /* 7 */
};

static void show_rtc_status(void)
{
    unsigned char rega, regb, regc, regd;

    rega = cmos_read(RTC_REG_A);
    regb = cmos_read(RTC_REG_B);
    regc = cmos_read(RTC_REG_C);  /* clears IRQ flags */
    regd = cmos_read(RTC_REG_D);

    printf("\nRTC Status Registers:\n");
    printf("  Register A (0x0A): 0x%02X\n", rega);
    printf("    Update In Progress: %s\n",
           (rega & RTC_A_UIP) ? "Yes (do not read time)" : "No");
    printf("    Divider: %d - %s\n",
           (rega & RTC_A_DV_MASK) >> RTC_A_DV_SHIFT,
           divider_name[(rega & RTC_A_DV_MASK) >> RTC_A_DV_SHIFT]);
    printf("    Rate select: %d - %s\n",
           rega & RTC_A_RS_MASK,
           rate_freq[rega & RTC_A_RS_MASK]);

    printf("  Register B (0x0B): 0x%02X\n", regb);
    printf("    SET (halt updates):     %s\n", (regb & RTC_B_SET)  ? "YES" : "no");
    printf("    Periodic IRQ enable:    %s\n", (regb & RTC_B_PIE)  ? "YES" : "no");
    printf("    Alarm IRQ enable:       %s\n", (regb & RTC_B_AIE)  ? "YES" : "no");
    printf("    Update-end IRQ enable:  %s\n", (regb & RTC_B_UIE)  ? "YES" : "no");
    printf("    Square wave output:     %s\n", (regb & RTC_B_SQWE) ? "YES" : "no");
    printf("    Data mode:              %s\n", (regb & RTC_B_DM)   ? "Binary" : "BCD");
    printf("    Hour format:            %s\n", (regb & RTC_B_24H)  ? "24-hour" : "12-hour");
    printf("    Daylight savings:       %s\n", (regb & RTC_B_DSE)  ? "YES" : "no");

    printf("  Register C (0x0C): 0x%02X  [read clears flags]\n", regc);
    printf("    IRQ flag (composite):   %s\n", (regc & RTC_C_IRQF) ? "SET" : "clear");
    printf("    Periodic flag:          %s\n", (regc & RTC_C_PF)   ? "SET" : "clear");
    printf("    Alarm flag:             %s\n", (regc & RTC_C_AF)   ? "SET" : "clear");
    printf("    Update-ended flag:      %s\n", (regc & RTC_C_UF)   ? "SET" : "clear");

    printf("  Register D (0x0D): 0x%02X\n", regd);
    printf("    Battery: %s\n",
           (regd & RTC_D_VRT) ? "OK (valid RAM & time)"
                              : "*** DEAD - REPLACE BATTERY ***");
}

/* ================================================================
 * Display: Alarm
 * ================================================================ */

static void show_alarm(void)
{
    unsigned char sec, min, hrs, regb;

    rtc_wait_uip();

    sec = cmos_read(RTC_ALARM_SEC);
    min = cmos_read(RTC_ALARM_MIN);
    hrs = cmos_read(RTC_ALARM_HRS);
    regb = cmos_read(RTC_REG_B);

    printf("\nRTC Alarm:\n");

    /* 0xC0-0xFF in alarm registers means "don't care" (wildcard) */
    if (sec >= 0xC0 && min >= 0xC0 && hrs >= 0xC0) {
        printf("  Alarm: Not set (all wildcards)\n");
    } else {
        printf("  Alarm time: ");
        if (hrs >= 0xC0)
            printf("**");
        else
            printf("%02d", rtc_to_bin(hrs));
        printf(":");
        if (min >= 0xC0)
            printf("**");
        else
            printf("%02d", rtc_to_bin(min));
        printf(":");
        if (sec >= 0xC0)
            printf("**");
        else
            printf("%02d", rtc_to_bin(sec));
        printf("\n");
        printf("  (** = wildcard/don't care)\n");
    }

    printf("  Alarm IRQ: %s\n",
           (regb & RTC_B_AIE) ? "ENABLED (routes to IRQ 1 on PC1640)"
                              : "Disabled");
}

static int set_alarm(const char *timestr)
{
    int hrs, min, sec;
    unsigned char regb;

    if (sscanf(timestr, "%d:%d:%d", &hrs, &min, &sec) != 3) {
        fprintf(stderr, "Error: Alarm format must be HH:MM:SS\n");
        fprintf(stderr, "  Use -1 for wildcard (e.g. -1:-1:00 = every minute at :00)\n");
        return 1;
    }

    regb = cmos_read(RTC_REG_B);
    cmos_write(RTC_REG_B, regb | RTC_B_SET);

    /* -1 means wildcard (don't care) = 0xC0 */
    cmos_write(RTC_ALARM_SEC, (sec < 0) ? 0xC0 : bin_to_rtc(sec));
    cmos_write(RTC_ALARM_MIN, (min < 0) ? 0xC0 : bin_to_rtc(min));
    cmos_write(RTC_ALARM_HRS, (hrs < 0) ? 0xC0 : bin_to_rtc(hrs));

    cmos_write(RTC_REG_B, regb & ~RTC_B_SET);

    printf("Alarm set to %s\n", timestr);
    printf("Note: Use 'nvr alarm-enable' to arm the alarm IRQ\n");
    return 0;
}

static void alarm_enable(int enable)
{
    unsigned char regb = cmos_read(RTC_REG_B);

    if (enable)
        regb |= RTC_B_AIE;
    else
        regb &= ~RTC_B_AIE;

    cmos_write(RTC_REG_B, regb);
    printf("Alarm IRQ %s\n", enable ? "ENABLED" : "disabled");
    if (enable)
        printf("  Note: On PC1640 alarm routes to IRQ 1 (shared with keyboard)\n");
}

/* ================================================================
 * Display: Floppy Drives
 * ================================================================ */

static const char *floppy_type_str(unsigned char type)
{
    switch (type) {
    case 0: return "Not installed";
    case 1: return "360 KB 5.25\" DD";
    case 2: return "1.2 MB 5.25\" HD";
    case 3: return "720 KB 3.5\" DD";
    case 4: return "1.44 MB 3.5\" HD";
    default: return "Unknown";
    }
}

static void show_floppy(void)
{
    unsigned char floppy = cmos_read(CMOS_FLOPPY);
    unsigned char equip = cmos_read(CMOS_EQUIP);
    int ndrives;

    printf("\nFloppy Drive Configuration:\n");
    printf("  CMOS byte 0x10: 0x%02X\n", floppy);
    printf("  Drive A: type %d - %s\n",
           (floppy >> 4) & 0x0F, floppy_type_str((floppy >> 4) & 0x0F));
    printf("  Drive B: type %d - %s\n",
           floppy & 0x0F, floppy_type_str(floppy & 0x0F));

    ndrives = (equip & 0x01) ? ((equip >> 6) & 0x03) + 1 : 0;
    printf("  Equipment says: %d drive(s) installed\n", ndrives);
    printf("  Disk-change line: active-low (PC1640 specific)\n");
}

static int set_floppy(const char *drv, const char *typestr)
{
    int type = atoi(typestr);
    unsigned char floppy;

    if (type < 0 || type > 4) {
        fprintf(stderr, "Error: Floppy type must be 0-4:\n");
        fprintf(stderr, "  0 = Not installed    3 = 720 KB 3.5\" DD\n");
        fprintf(stderr, "  1 = 360 KB 5.25\" DD  4 = 1.44 MB 3.5\" HD\n");
        fprintf(stderr, "  2 = 1.2 MB 5.25\" HD\n");
        return 1;
    }

    floppy = cmos_read(CMOS_FLOPPY);

    if (drv[0] == 'A' || drv[0] == 'a' || drv[0] == '0') {
        floppy = (floppy & 0x0F) | (type << 4);
        printf("Drive A set to: %s\n", floppy_type_str(type));
    } else if (drv[0] == 'B' || drv[0] == 'b' || drv[0] == '1') {
        floppy = (floppy & 0xF0) | type;
        printf("Drive B set to: %s\n", floppy_type_str(type));
    } else {
        fprintf(stderr, "Error: Drive must be A or B\n");
        return 1;
    }

    cmos_write(CMOS_FLOPPY, floppy);
    cmos_update_checksum();
    return 0;
}

/* ================================================================
 * Display: Hard Disk Drives
 * ================================================================ */

/*
 * Standard AT BIOS hard disk type table.
 * The PC1640 BIOS reads CMOS 0x12 to determine drive types.
 * If the nibble is 0x0F, the extended type register is consulted.
 * Type 0 = not installed.
 * The table below covers types 1-15 (the common ones).
 */
struct hd_type_entry {
    unsigned short cyls;
    unsigned char  heads;
    unsigned short precomp;
    unsigned short landing;
    unsigned char  sectors;
};

static const struct hd_type_entry hd_types[] = {
    /* type  cyls  hds  precomp  landing  spt */
    {  306,   4,   128,   305,   17 },  /* 1 - 10MB */
    {  615,   4,   300,   615,   17 },  /* 2 - 20MB */
    {  615,   6,   300,   615,   17 },  /* 3 - 30MB */
    {  940,   8,   512,   940,   17 },  /* 4 - 62MB */
    {  940,   6,   512,   940,   17 },  /* 5 - 46MB */
    {  615,   4,    -1,   615,   17 },  /* 6 - 20MB (no precomp) */
    {  462,   8,   256,   511,   17 },  /* 7 - 30MB */
    {  733,   5,    -1,   733,   17 },  /* 8 - 30MB */
    {  900,  15,    -1,   901,   17 },  /* 9 - 112MB */
    {  820,   3,    -1,   820,   17 },  /* 10 - 20MB */
    {  855,   5,    -1,   855,   17 },  /* 11 - 35MB */
    {  855,   7,    -1,   855,   17 },  /* 12 - 49MB */
    {  306,   8,   128,   319,   17 },  /* 13 - 20MB */
    {  733,   7,    -1,   733,   17 },  /* 14 - 42MB */
    {    0,   0,     0,     0,    0 },  /* 15 = extended type, see 0x19/0x1A */
};

static void show_harddisk(void)
{
    unsigned char diskbyte, type0, type1, ext0, ext1;

    diskbyte = cmos_read(CMOS_DISK);
    type0 = (diskbyte >> 4) & 0x0F;
    type1 = diskbyte & 0x0F;
    ext0 = cmos_read(CMOS_DISK0_EXT);
    ext1 = cmos_read(CMOS_DISK1_EXT);

    printf("\nHard Disk Configuration:\n");
    printf("  CMOS byte 0x12: 0x%02X\n", diskbyte);

    /* Drive 0 */
    printf("  Drive 0 (C:): ");
    if (type0 == 0) {
        printf("Not installed\n");
    } else if (type0 == 0x0F) {
        printf("Extended type %d (from CMOS 0x19)\n", ext0);
    } else if (type0 <= 14) {
        printf("Type %d - %d cyl, %d heads, %d spt",
               type0, hd_types[type0-1].cyls, hd_types[type0-1].heads,
               hd_types[type0-1].sectors);
        printf(" (~%lu MB)\n",
               (unsigned long)hd_types[type0-1].cyls *
               hd_types[type0-1].heads *
               hd_types[type0-1].sectors * 512UL / (1024UL * 1024UL));
    } else {
        printf("Unknown type %d\n", type0);
    }

    /* Drive 1 */
    printf("  Drive 1 (D:): ");
    if (type1 == 0) {
        printf("Not installed\n");
    } else if (type1 == 0x0F) {
        printf("Extended type %d (from CMOS 0x1A)\n", ext1);
    } else if (type1 <= 14) {
        printf("Type %d - %d cyl, %d heads, %d spt",
               type1, hd_types[type1-1].cyls, hd_types[type1-1].heads,
               hd_types[type1-1].sectors);
        printf(" (~%lu MB)\n",
               (unsigned long)hd_types[type1-1].cyls *
               hd_types[type1-1].heads *
               hd_types[type1-1].sectors * 512UL / (1024UL * 1024UL));
    } else {
        printf("Unknown type %d\n", type1);
    }
}

static int set_harddisk(const char *drv, const char *typestr)
{
    int type = atoi(typestr);
    unsigned char diskbyte;

    if (type < 0 || type > 15) {
        fprintf(stderr, "Error: Hard disk type must be 0-15:\n");
        fprintf(stderr, "  0  = Not installed\n");
        fprintf(stderr, "  1  = 10 MB (306 cyl, 4 heads)\n");
        fprintf(stderr, "  2  = 20 MB (615 cyl, 4 heads)\n");
        fprintf(stderr, "  3  = 30 MB (615 cyl, 6 heads)\n");
        fprintf(stderr, "  4  = 62 MB (940 cyl, 8 heads)\n");
        fprintf(stderr, "  5  = 46 MB (940 cyl, 6 heads)\n");
        fprintf(stderr, "  6  = 20 MB (615 cyl, 4 heads, no precomp)\n");
        fprintf(stderr, "  7  = 30 MB (462 cyl, 8 heads)\n");
        fprintf(stderr, "  8  = 30 MB (733 cyl, 5 heads)\n");
        fprintf(stderr, "  9  = 112 MB (900 cyl, 15 heads)\n");
        fprintf(stderr, " 10  = 20 MB (820 cyl, 3 heads)\n");
        fprintf(stderr, " 11  = 35 MB (855 cyl, 5 heads)\n");
        fprintf(stderr, " 12  = 49 MB (855 cyl, 7 heads)\n");
        fprintf(stderr, " 13  = 20 MB (306 cyl, 8 heads)\n");
        fprintf(stderr, " 14  = 42 MB (733 cyl, 7 heads)\n");
        fprintf(stderr, " 15  = Extended type (uses CMOS 0x19/0x1A)\n");
        return 1;
    }

    diskbyte = cmos_read(CMOS_DISK);

    if (drv[0] == '0' || drv[0] == 'C' || drv[0] == 'c') {
        diskbyte = (diskbyte & 0x0F) | (type << 4);
        printf("Drive 0 (C:) set to type %d\n", type);
    } else if (drv[0] == '1' || drv[0] == 'D' || drv[0] == 'd') {
        diskbyte = (diskbyte & 0xF0) | type;
        printf("Drive 1 (D:) set to type %d\n", type);
    } else {
        fprintf(stderr, "Error: Drive must be 0/C or 1/D\n");
        return 1;
    }

    cmos_write(CMOS_DISK, diskbyte);
    cmos_update_checksum();
    return 0;
}

/* ================================================================
 * Display: Equipment Byte
 * ================================================================ */

static void show_equipment(void)
{
    unsigned char equip = cmos_read(CMOS_EQUIP);
    int nfloppy;

    printf("\nEquipment Byte (CMOS 0x14): 0x%02X\n", equip);

    printf("  Bit 0 - Floppy drives:     %s\n",
           (equip & 0x01) ? "Installed" : "Not installed");

    printf("  Bit 1 - Math coprocessor:  %s\n",
           (equip & 0x02) ? "8087 installed" : "Not installed");

    printf("  Bits 2-3 (reserved):       0x%X\n", (equip >> 2) & 0x03);

    printf("  Bits 4-5 - Initial video:  ");
    switch ((equip >> 4) & 0x03) {
    case 0: printf("EGA/VGA (built-in PEGA)\n"); break;
    case 1: printf("40-column CGA\n"); break;
    case 2: printf("80-column CGA\n"); break;
    case 3: printf("MDA/Hercules\n"); break;
    }

    nfloppy = (equip & 0x01) ? ((equip >> 6) & 0x03) + 1 : 0;
    printf("  Bits 6-7 - Floppy count:   %d drive(s)\n", nfloppy);
}

static int set_equipment(const char *field, const char *valstr)
{
    unsigned char equip = cmos_read(CMOS_EQUIP);
    int val = atoi(valstr);

    if (strcmp(field, "fpu") == 0 || strcmp(field, "coprocessor") == 0 ||
        strcmp(field, "8087") == 0) {
        if (val)
            equip |= 0x02;
        else
            equip &= ~0x02;
        printf("Math coprocessor: %s\n", val ? "Installed" : "Not installed");
    }
    else if (strcmp(field, "video") == 0) {
        if (val < 0 || val > 3) {
            fprintf(stderr, "Error: Video mode 0-3:\n");
            fprintf(stderr, "  0 = EGA/VGA   1 = 40-col CGA\n");
            fprintf(stderr, "  2 = 80-col CGA 3 = MDA/Hercules\n");
            return 1;
        }
        equip = (equip & ~0x30) | ((val & 0x03) << 4);
        printf("Initial video mode set to %d\n", val);
    }
    else if (strcmp(field, "floppy-count") == 0) {
        if (val < 0 || val > 4) {
            fprintf(stderr, "Error: Floppy count 0-4\n");
            return 1;
        }
        if (val == 0) {
            equip &= ~0x01;
            equip &= ~0xC0;
        } else {
            equip |= 0x01;
            equip = (equip & ~0xC0) | (((val - 1) & 0x03) << 6);
        }
        printf("Floppy count set to %d\n", val);
    }
    else {
        fprintf(stderr, "Unknown equipment field: %s\n", field);
        fprintf(stderr, "Fields: fpu, video, floppy-count\n");
        return 1;
    }

    cmos_write(CMOS_EQUIP, equip);
    cmos_update_checksum();
    return 0;
}

/* ================================================================
 * Display: Memory
 * ================================================================ */

static void show_memory(void)
{
    unsigned short basemem, extmem;

    basemem = cmos_read(CMOS_BASEMEM_LO) |
              ((unsigned short)cmos_read(CMOS_BASEMEM_HI) << 8);
    extmem = cmos_read(CMOS_EXTMEM_LO) |
             ((unsigned short)cmos_read(CMOS_EXTMEM_HI) << 8);

    printf("\nMemory Configuration:\n");
    printf("  Base memory:     %u KB", basemem);
    if (basemem == 640)
        printf(" (standard PC1640)");
    printf("\n");
    printf("  Extended memory: %u KB", extmem);
    if (extmem == 0)
        printf(" (normal - 8086 has no extended memory)");
    printf("\n");
}

static int set_basemem(const char *valstr)
{
    int kb = atoi(valstr);

    if (kb < 64 || kb > 640) {
        fprintf(stderr, "Error: Base memory must be 64-640 KB\n");
        return 1;
    }

    cmos_write(CMOS_BASEMEM_LO, kb & 0xFF);
    cmos_write(CMOS_BASEMEM_HI, (kb >> 8) & 0xFF);
    cmos_update_checksum();
    printf("Base memory set to %d KB\n", kb);
    return 0;
}

/* ================================================================
 * Display: Diagnostic Status
 * ================================================================ */

static void show_diagnostics(void)
{
    unsigned char diag = cmos_read(CMOS_DIAG);
    unsigned char shut = cmos_read(CMOS_SHUTDOWN);

    printf("\nDiagnostic Status (CMOS 0x0E): 0x%02X\n", diag);
    if (diag & 0x80) printf("  Bit 7: RTC lost power (battery failed during outage)\n");
    if (diag & 0x40) printf("  Bit 6: CMOS checksum bad\n");
    if (diag & 0x20) printf("  Bit 5: Invalid configuration info\n");
    if (diag & 0x10) printf("  Bit 4: Memory size mismatch (POST vs CMOS)\n");
    if (diag & 0x08) printf("  Bit 3: Hard disk controller init failed\n");
    if (diag & 0x04) printf("  Bit 2: Time invalid\n");
    if (diag & 0x02) printf("  Bit 1: Installed adapters error\n");
    if (diag & 0x01) printf("  Bit 0: Timeout reading adapter ROM\n");
    if (diag == 0x00) printf("  All clear - no errors\n");

    printf("\nShutdown Status (CMOS 0x0F): 0x%02X", shut);
    switch (shut) {
    case 0x00: printf(" - Normal POST\n"); break;
    case 0x01: printf(" - Chip set init for real mode return\n"); break;
    case 0x04: printf(" - Jump to bootstrap (INT 19h)\n"); break;
    case 0x05: printf(" - User-defined warm boot\n"); break;
    case 0x09: printf(" - Return to real mode (block move)\n"); break;
    case 0x0A: printf(" - Jump to DWORD at 0040:0067\n"); break;
    default:   printf(" - Code 0x%02X\n", shut); break;
    }
}

static void clear_diagnostics(void)
{
    cmos_write(CMOS_DIAG, 0x00);
    printf("Diagnostic status cleared\n");
}

/* ================================================================
 * Display: Amstrad-Specific System Status
 * ================================================================ */

/* Language code table (from BIOS ROM & PCem source) */
static const char *language_name(unsigned char code)
{
    switch (code & 0x07) {
    case 0: return "Diagnostic mode";
    case 1: return "Italian";
    case 2: return "Swedish";
    case 3: return "Danish";
    case 4: return "Spanish";
    case 5: return "French";
    case 6: return "German";
    case 7: return "English";
    default: return "Unknown";
    }
}

static const char *display_type_name(unsigned char code)
{
    switch (code & 0x03) {
    case 0: return "EGA (built-in Paradise PEGA)";
    case 1: return "Unknown (reserved)";
    case 2: return "CGA";
    case 3: return "MDA/Hercules";
    default: return "Unknown";
    }
}

static void show_amstrad_language(void)
{
    unsigned char lpt_status = inb(PORT_LPT1_STATUS);
    unsigned char lang = lpt_status & LPT1_LANG_MASK;

    printf("\nLanguage Selection (DIP switches -> port 0x379 bits 0-2):\n");
    printf("  LPT1 status byte: 0x%02X\n", lpt_status);
    printf("  Language code:    %d - %s\n", lang, language_name(lang));
    printf("  Available codes:\n");
    printf("    0 = Diagnostic   4 = Spanish\n");
    printf("    1 = Italian      5 = French\n");
    printf("    2 = Swedish      6 = German\n");
    printf("    3 = Danish       7 = English\n");
}

static void show_display_type(void)
{
    unsigned char lpt_status = inb(PORT_LPT1_STATUS);
    unsigned char disp = (lpt_status & LPT1_DISP_MASK) >> LPT1_DISP_SHIFT;
    unsigned char ida = inb(PORT_IDA_STATUS);

    printf("\nDisplay Type Detection:\n");
    printf("  LPT1 status bits 6-7: %d - %s\n", disp, display_type_name(disp));
    printf("  IDA status (0x3DE):   0x%02X", ida);
    if (ida & 0x20)
        printf(" - Internal Display Adapter DISABLED\n");
    else
        printf(" - Internal Display Adapter active\n");

    printf("  Video mode switch (port 0x3DB bit 6): ");
    printf("write-only (toggles CGA/EGA)\n");
}

static void show_amstrad_full(void)
{
    unsigned char pb, stat2, stat2_raw, stat1;
    unsigned char lpt_status;

    printf("\nAmstrad PC1640 System Status:\n");
    printf("------------------------------\n");

    /* PB Register */
    pb = inb(PORT_PB);
    printf("\n  PB Register (port 0x61): 0x%02X\n", pb);
    printf("    Bit 0 - Speaker gate:      %s\n", (pb & PB_SPEAKER_GATE)   ? "ON" : "off");
    printf("    Bit 1 - Speaker enable:    %s\n", (pb & PB_SPEAKER_ENABLE) ? "ON" : "off");
    printf("    Bit 2 - Nibble select:     %s nibble\n", (pb & PB_NIBBLE_SEL) ? "Low" : "High");
    printf("    Bit 6 - Keyboard reset:    %s\n", (pb & PB_KBD_RESET) ? "ACTIVE" : "inactive");
    printf("    Bit 7 - Port 0x60 mode:    %s\n", (pb & PB_STATUS_MODE) ? "System status" : "Keyboard data");

    /* System Status 2 raw + combined */
    stat2_raw = inb(PORT_STATUS2);
    printf("\n  Port 0x62 raw read: 0x%02X\n", stat2_raw);
    printf("    Bit 5 - Speaker output:    %s\n", (stat2_raw & 0x20) ? "HIGH" : "low");
    printf("    Bit 6 - NMI status:        %s\n", (stat2_raw & 0x40) ? "ACTIVE" : "inactive");

    stat2 = amstrad_read_sysstat2();
    printf("  System Status 2 (combined):  0x%02X\n", stat2);

    /* System Status 1 */
    stat1 = amstrad_read_sysstat1();
    printf("\n  System Status 1 (port 0x60): 0x%02X\n", stat1);
    printf("    (Value = (sysstat1_latch | 0x0D) & 0x7F)\n");

    /* LPT1 status - language + display */
    lpt_status = inb(PORT_LPT1_STATUS);
    printf("\n  LPT1 Status (port 0x379):    0x%02X\n", lpt_status);
    printf("    Bits 0-2 - Language:       %d (%s)\n",
           lpt_status & LPT1_LANG_MASK,
           language_name(lpt_status & LPT1_LANG_MASK));
    printf("    Bit 5   - DIP latch:       %s\n",
           (lpt_status & LPT1_DIP_LATCH) ? "SW10" : "SW9/none");
    printf("    Bits 6-7 - Display type:   %d (%s)\n",
           (lpt_status & LPT1_DISP_MASK) >> LPT1_DISP_SHIFT,
           display_type_name((lpt_status & LPT1_DISP_MASK) >> LPT1_DISP_SHIFT));
}

/* ================================================================
 * Display: Serial & Parallel Ports
 * ================================================================ */

static int detect_com_port(unsigned short base)
{
    unsigned char lcr_orig, test;

    /* Save line control register */
    lcr_orig = inb(base + 3);

    /* Write test pattern to LCR */
    outb(0xAA, base + 3);
    io_delay();
    test = inb(base + 3);

    /* Restore */
    outb(lcr_orig, base + 3);

    return (test == 0xAA);
}

static int detect_lpt_port(unsigned short base)
{
    unsigned char orig, test;

    orig = inb(base);
    outb(0xAA, base);
    io_delay();
    test = inb(base);
    outb(orig, base);

    /* On PC1640, LPT1 at 0x378 reads back with language bits OR'd in */
    return (test == 0xAA || (base == PORT_LPT1_DATA && (test & 0xF8) == 0xA8));
}

static void show_ports(void)
{
    printf("\nSerial & Parallel Ports:\n");

    printf("  COM1 (0x3F8): %s\n",
           detect_com_port(PORT_COM1_BASE) ? "Detected (IRQ 4)" : "Not found");
    printf("  COM2 (0x2F8): %s\n",
           detect_com_port(PORT_COM2_BASE) ? "Detected (IRQ 3)" : "Not found");

    printf("  LPT1 (0x378): %s\n",
           detect_lpt_port(PORT_LPT1_DATA) ? "Detected (Amstrad-overloaded)" : "Not found");
    printf("  LPT2 (0x3BC): %s\n",
           detect_lpt_port(PORT_LPT2_DATA) ? "Detected" : "Not found");
}

/* ================================================================
 * Display: Amstrad Mouse Port
 * ================================================================ */

static void show_mouse(void)
{
    unsigned char mx, my;

    printf("\nAmstrad Mouse Port:\n");
    printf("  Type: Amstrad proprietary (NOT serial/PS2)\n");
    printf("  X counter port: 0x78 (read=position, write=reset)\n");
    printf("  Y counter port: 0x7A (read=position, write=reset)\n");
    printf("  Buttons: via keyboard scancodes:\n");
    printf("    Left press:  0x7E   Left release:  0xFE\n");
    printf("    Right press: 0x7D   Right release: 0xFD\n");

    mx = inb(PORT_MOUSE_X);
    my = inb(PORT_MOUSE_Y);
    printf("\n  Current X counter: %d (0x%02X)\n", (signed char)mx, mx);
    printf("  Current Y counter: %d (0x%02X)\n", (signed char)my, my);
}

static void mouse_reset(void)
{
    outb(0, PORT_MOUSE_X);
    outb(0, PORT_MOUSE_Y);
    printf("Mouse counters reset to 0\n");
}

static void mouse_test(void)
{
    unsigned char mx2, my2;
    int i;

    printf("\nMouse Movement Test (5 seconds):\n");
    printf("  Move the mouse to see counter changes...\n\n");

    /* Reset counters */
    outb(0, PORT_MOUSE_X);
    outb(0, PORT_MOUSE_Y);

    for (i = 0; i < 50; i++) {
        unsigned char mx, my;
        mx = inb(PORT_MOUSE_X);
        my = inb(PORT_MOUSE_Y);
        if (mx != 0 || my != 0) {
            printf("  X: %4d  Y: %4d\r", (signed char)mx, (signed char)my);
        }
        /* ~100ms delay - crude busy loop */
        {
            volatile unsigned long j;
            for (j = 0; j < 50000UL; j++)
                ;
        }
    }
    mx2 = inb(PORT_MOUSE_X);
    my2 = inb(PORT_MOUSE_Y);
    printf("\n  Final: X=%d Y=%d\n", (signed char)mx2, (signed char)my2);
    if (mx2 == 0 && my2 == 0)
        printf("  No movement detected - mouse may not be connected\n");
    else
        printf("  Mouse is responding\n");
}

/* ================================================================
 * Speaker / Sound Test
 * ================================================================ */

static void speaker_on(unsigned short freq)
{
    unsigned short divisor;
    unsigned char pb;

    if (freq == 0)
        freq = 1000;

    /* PIT channel 2 divisor: 1193182 / freq */
    divisor = (unsigned short)(1193182UL / (unsigned long)freq);

    /* Set PIT channel 2 to mode 3 (square wave) */
    outb(0xB6, PORT_PIT_MODE);  /* channel 2, mode 3, lobyte/hibyte */
    io_delay();
    outb(divisor & 0xFF, PORT_PIT_CH2);
    io_delay();
    outb((divisor >> 8) & 0xFF, PORT_PIT_CH2);
    io_delay();

    /* Enable speaker: set PB bits 0 (gate) and 1 (enable) */
    pb = inb(PORT_PB);
    outb(pb | PB_SPEAKER_GATE | PB_SPEAKER_ENABLE, PORT_PB);
}

static void speaker_off(void)
{
    unsigned char pb = inb(PORT_PB);
    outb(pb & ~(PB_SPEAKER_GATE | PB_SPEAKER_ENABLE), PORT_PB);
}

static void speaker_test(void)
{
    volatile unsigned long j;

    printf("Speaker test:\n");

    printf("  440 Hz (A4)...\n");
    speaker_on(440);
    for (j = 0; j < 500000UL; j++) ;
    speaker_off();
    for (j = 0; j < 100000UL; j++) ;

    printf("  880 Hz (A5)...\n");
    speaker_on(880);
    for (j = 0; j < 500000UL; j++) ;
    speaker_off();
    for (j = 0; j < 100000UL; j++) ;

    printf("  1000 Hz...\n");
    speaker_on(1000);
    for (j = 0; j < 500000UL; j++) ;
    speaker_off();
    for (j = 0; j < 100000UL; j++) ;

    printf("  2000 Hz...\n");
    speaker_on(2000);
    for (j = 0; j < 500000UL; j++) ;
    speaker_off();

    printf("  Done.\n");
}

static int speaker_beep(const char *freqstr)
{
    int freq = atoi(freqstr);
    volatile unsigned long j;

    if (freq < 20 || freq > 20000) {
        fprintf(stderr, "Error: Frequency must be 20-20000 Hz\n");
        return 1;
    }

    printf("Beep at %d Hz...\n", freq);
    speaker_on(freq);
    for (j = 0; j < 500000UL; j++) ;
    speaker_off();
    return 0;
}

/* ================================================================
 * PIC (Interrupt Controller) Status
 * ================================================================ */

static void show_pic(void)
{
    unsigned char imr, isr, irr;

    printf("\n8259A PIC Status:\n");

    /* Read IMR (Interrupt Mask Register) */
    imr = inb(PORT_PIC_DATA);

    /* Read IRR (Interrupt Request Register) via OCW3 */
    outb(0x0A, PORT_PIC_CMD);  /* OCW3: read IRR */
    io_delay();
    irr = inb(PORT_PIC_CMD);

    /* Read ISR (In-Service Register) via OCW3 */
    outb(0x0B, PORT_PIC_CMD);  /* OCW3: read ISR */
    io_delay();
    isr = inb(PORT_PIC_CMD);

    printf("  IMR (Interrupt Mask):     0x%02X\n", imr);
    printf("  IRR (Interrupt Request):  0x%02X\n", irr);
    printf("  ISR (In-Service):         0x%02X\n", isr);
    printf("\n");
    printf("  IRQ  Mask  Req  Svc  Function (PC1640)\n");
    printf("  ---  ----  ---  ---  -----------------\n");
    printf("   0    %c     %c    %c   Timer (8253 CH0, 18.2 Hz)\n",
           (imr & 0x01) ? 'M' : '.', (irr & 0x01) ? 'R' : '.', (isr & 0x01) ? 'S' : '.');
    printf("   1    %c     %c    %c   Keyboard + RTC alarm (Amstrad!)\n",
           (imr & 0x02) ? 'M' : '.', (irr & 0x02) ? 'R' : '.', (isr & 0x02) ? 'S' : '.');
    printf("   2    %c     %c    %c   Reserved\n",
           (imr & 0x04) ? 'M' : '.', (irr & 0x04) ? 'R' : '.', (isr & 0x04) ? 'S' : '.');
    printf("   3    %c     %c    %c   COM2 (serial port 2)\n",
           (imr & 0x08) ? 'M' : '.', (irr & 0x08) ? 'R' : '.', (isr & 0x08) ? 'S' : '.');
    printf("   4    %c     %c    %c   COM1 (serial port 1)\n",
           (imr & 0x10) ? 'M' : '.', (irr & 0x10) ? 'R' : '.', (isr & 0x10) ? 'S' : '.');
    printf("   5    %c     %c    %c   LPT2 (parallel port 2)\n",
           (imr & 0x20) ? 'M' : '.', (irr & 0x20) ? 'R' : '.', (isr & 0x20) ? 'S' : '.');
    printf("   6    %c     %c    %c   Floppy disk controller\n",
           (imr & 0x40) ? 'M' : '.', (irr & 0x40) ? 'R' : '.', (isr & 0x40) ? 'S' : '.');
    printf("   7    %c     %c    %c   LPT1 (parallel port 1)\n",
           (imr & 0x80) ? 'M' : '.', (irr & 0x80) ? 'R' : '.', (isr & 0x80) ? 'S' : '.');

    printf("\n  Legend: M=Masked  R=Request pending  S=In service\n");
    printf("  Note: PC1640 has single PIC (no secondary - 8086 system)\n");
    printf("  Note: RTC alarm routes to IRQ 1 (shared with keyboard)\n");
}

/* ================================================================
 * DMA Controller Status
 * ================================================================ */

static void show_dma(void)
{
    unsigned char status;

    printf("\n8237A DMA Controller Status:\n");

    status = inb(PORT_DMA_STAT);

    printf("  Status register (port 0x08): 0x%02X\n", status);
    printf("    Ch0: TC=%c  Req=%c  (DRAM refresh)\n",
           (status & 0x01) ? 'Y' : 'N', (status & 0x10) ? 'Y' : 'N');
    printf("    Ch1: TC=%c  Req=%c  (Available)\n",
           (status & 0x02) ? 'Y' : 'N', (status & 0x20) ? 'Y' : 'N');
    printf("    Ch2: TC=%c  Req=%c  (Floppy disk)\n",
           (status & 0x04) ? 'Y' : 'N', (status & 0x40) ? 'Y' : 'N');
    printf("    Ch3: TC=%c  Req=%c  (Available)\n",
           (status & 0x08) ? 'Y' : 'N', (status & 0x80) ? 'Y' : 'N');

    printf("  TC = Terminal Count reached\n");
    printf("  Page registers: ch1=0x83  ch2=0x81  ch3=0x82\n");
}

/* ================================================================
 * PIT (Timer) Status
 * ================================================================ */

static void show_pit(void)
{
    unsigned char lo, hi;
    unsigned short count;

    printf("\n8253 PIT (Programmable Interval Timer):\n");
    printf("  Base frequency: 1,193,182 Hz\n");

    /* Latch channel 2 count */
    outb(0x80, PORT_PIT_MODE);  /* Latch channel 2 */
    io_delay();
    lo = inb(PORT_PIT_CH2);
    hi = inb(PORT_PIT_CH2);
    count = lo | ((unsigned short)hi << 8);

    printf("  Channel 0: System timer (IRQ 0, ~18.2 Hz tick)\n");
    printf("  Channel 1: DRAM refresh (hidden)\n");
    printf("  Channel 2: Speaker tone generator\n");
    printf("    Current count: %u (0x%04X)\n", count, count);
    if (count > 0) {
        printf("    Frequency: ~%lu Hz\n", 1193182UL / (unsigned long)count);
    }

    {
        unsigned char pb = inb(PORT_PB);
        printf("    Speaker gate (PB.0): %s\n",
               (pb & PB_SPEAKER_GATE) ? "ON" : "off");
        printf("    Speaker enable (PB.1): %s\n",
               (pb & PB_SPEAKER_ENABLE) ? "ON" : "off");
    }
}

/* ================================================================
 * Full CMOS Dump
 * ================================================================ */

static void dump_cmos(void)
{
    int i;
    unsigned char data[CMOS_SIZE];

    rtc_wait_uip();
    for (i = 0; i < CMOS_SIZE; i++)
        data[i] = cmos_read(i);

    printf("\nCMOS RAM Dump (64 bytes):\n");
    printf("       00 01 02 03 04 05 06 07 08 09 0A 0B 0C 0D 0E 0F\n");
    printf("       -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- --\n");

    for (i = 0; i < CMOS_SIZE; i++) {
        if ((i % 16) == 0)
            printf("  %02X:  ", i);
        printf("%02X ", data[i]);
        if ((i % 16) == 15)
            printf("\n");
    }

    printf("\n  Regions:\n");
    printf("    0x00-0x09: RTC time/date registers\n");
    printf("    0x0A-0x0D: RTC status registers (A-D)\n");
    printf("    0x0E:      Diagnostic status\n");
    printf("    0x0F:      Shutdown status\n");
    printf("    0x10:      Floppy drive types\n");
    printf("    0x12:      Hard disk types\n");
    printf("    0x14:      Equipment byte\n");
    printf("    0x15-0x16: Base memory (KB)\n");
    printf("    0x17-0x18: Extended memory (KB)\n");
    printf("    0x19-0x1A: HD extended types\n");
    printf("    0x2E-0x2F: Checksum\n");
    printf("    0x32:      Century (BCD)\n");

    printf("\n  Checksum (0x10-0x2D): %s\n",
           cmos_verify_checksum() ? "VALID" : "*** INVALID ***");
}

/* ================================================================
 * CMOS Compare: show differences between two dumps
 * ================================================================ */

static int compare_cmos(const char *filename)
{
    FILE *fp;
    unsigned char file_data[CMOS_SIZE], live_data[CMOS_SIZE];
    int i, diffs = 0;

    fp = fopen(filename, "rb");
    if (!fp) {
        perror("Error opening file");
        return 1;
    }
    if (fread((char *)file_data, 1, CMOS_SIZE, fp) != CMOS_SIZE) {
        fprintf(stderr, "Error: File too small (expected %d bytes)\n", CMOS_SIZE);
        fclose(fp);
        return 1;
    }
    fclose(fp);

    rtc_wait_uip();
    for (i = 0; i < CMOS_SIZE; i++)
        live_data[i] = cmos_read(i);

    printf("\nCMOS Compare: live vs %s\n", filename);
    printf("  Addr  Live  File  Description\n");
    printf("  ----  ----  ----  -----------\n");

    for (i = 0; i < CMOS_SIZE; i++) {
        if (live_data[i] != file_data[i]) {
            printf("  0x%02X  0x%02X  0x%02X", i, live_data[i], file_data[i]);

            switch (i) {
            case 0x00: printf("  Seconds"); break;
            case 0x01: printf("  Alarm seconds"); break;
            case 0x02: printf("  Minutes"); break;
            case 0x03: printf("  Alarm minutes"); break;
            case 0x04: printf("  Hours"); break;
            case 0x05: printf("  Alarm hours"); break;
            case 0x06: printf("  Day of week"); break;
            case 0x07: printf("  Day of month"); break;
            case 0x08: printf("  Month"); break;
            case 0x09: printf("  Year"); break;
            case 0x0A: printf("  Register A"); break;
            case 0x0B: printf("  Register B"); break;
            case 0x0C: printf("  Register C (flags)"); break;
            case 0x0D: printf("  Register D (battery)"); break;
            case 0x0E: printf("  Diagnostic status"); break;
            case 0x0F: printf("  Shutdown status"); break;
            case 0x10: printf("  Floppy types"); break;
            case 0x12: printf("  Hard disk types"); break;
            case 0x14: printf("  Equipment byte"); break;
            case 0x15: printf("  Base mem low"); break;
            case 0x16: printf("  Base mem high"); break;
            case 0x17: printf("  Ext mem low"); break;
            case 0x18: printf("  Ext mem high"); break;
            case 0x19: printf("  HD0 ext type"); break;
            case 0x1A: printf("  HD1 ext type"); break;
            case 0x2E: printf("  Checksum high"); break;
            case 0x2F: printf("  Checksum low"); break;
            case 0x32: printf("  Century"); break;
            }
            printf("\n");
            diffs++;
        }
    }

    if (diffs == 0)
        printf("  No differences found\n");
    else
        printf("\n  Total: %d byte(s) differ\n", diffs);

    return 0;
}

/* ================================================================
 * Raw CMOS Read/Write
 * ================================================================ */

static int raw_read(const char *addrstr)
{
    unsigned int addr = (unsigned int)strtol(addrstr, NULL, 0);

    if (addr >= CMOS_SIZE) {
        fprintf(stderr, "Error: Address must be 0x00-0x3F\n");
        return 1;
    }

    printf("CMOS[0x%02X] = 0x%02X (%u)\n", addr, cmos_read(addr), cmos_read(addr));
    return 0;
}

static int raw_write(const char *addrstr, const char *valstr)
{
    unsigned int addr = (unsigned int)strtol(addrstr, NULL, 0);
    unsigned int val = (unsigned int)strtol(valstr, NULL, 0);

    if (addr >= CMOS_SIZE) {
        fprintf(stderr, "Error: Address must be 0x00-0x3F\n");
        return 1;
    }
    if (val > 0xFF) {
        fprintf(stderr, "Error: Value must be 0x00-0xFF\n");
        return 1;
    }

    cmos_write(addr, val);

    if (addr >= 0x10 && addr <= 0x2D) {
        cmos_update_checksum();
        DBG(1, "Checksum auto-updated\n");
    }

    printf("CMOS[0x%02X] = 0x%02X written\n", addr, val);
    return 0;
}

/* ================================================================
 * Set Time / Date
 * ================================================================ */

static int set_time(const char *timestr)
{
    int hrs, min, sec;
    unsigned char regb;

    if (sscanf(timestr, "%d:%d:%d", &hrs, &min, &sec) != 3) {
        fprintf(stderr, "Error: Time format must be HH:MM:SS\n");
        return 1;
    }
    if (hrs < 0 || hrs > 23 || min < 0 || min > 59 || sec < 0 || sec > 59) {
        fprintf(stderr, "Error: Invalid time values\n");
        return 1;
    }

    regb = cmos_read(RTC_REG_B);
    cmos_write(RTC_REG_B, regb | RTC_B_SET);

    cmos_write(RTC_SECONDS, bin_to_rtc(sec));
    cmos_write(RTC_MINUTES, bin_to_rtc(min));
    cmos_write(RTC_HOURS, bin_to_rtc(hrs));

    cmos_write(RTC_REG_B, regb & ~RTC_B_SET);

    printf("Time set to %02d:%02d:%02d\n", hrs, min, sec);
    return 0;
}

static int set_date(const char *datestr)
{
    int day, mon, year;
    unsigned char regb, yr, cen;

    if (sscanf(datestr, "%d/%d/%d", &day, &mon, &year) != 3) {
        fprintf(stderr, "Error: Date format must be DD/MM/YYYY\n");
        return 1;
    }
    if (day < 1 || day > 31 || mon < 1 || mon > 12 ||
        year < 1980 || year > 2099) {
        fprintf(stderr, "Error: Invalid date (1980-2099)\n");
        return 1;
    }

    cen = year / 100;
    yr = year % 100;

    regb = cmos_read(RTC_REG_B);
    cmos_write(RTC_REG_B, regb | RTC_B_SET);

    cmos_write(RTC_DAY_OF_MONTH, bin_to_rtc(day));
    cmos_write(RTC_MONTH, bin_to_rtc(mon));
    cmos_write(RTC_YEAR, bin_to_rtc(yr));
    cmos_write(CMOS_CENTURY, bin_to_rtc(cen));

    cmos_write(RTC_REG_B, regb & ~RTC_B_SET);

    printf("Date set to %02d/%02d/%04d\n", day, mon, year);
    return 0;
}

static int set_dow(const char *dowstr)
{
    int dow = atoi(dowstr);
    unsigned char regb;

    if (dow < 1 || dow > 7) {
        fprintf(stderr, "Error: Day of week 1-7 (1=Sunday, 7=Saturday)\n");
        return 1;
    }

    regb = cmos_read(RTC_REG_B);
    cmos_write(RTC_REG_B, regb | RTC_B_SET);
    cmos_write(RTC_DAY_OF_WEEK, bin_to_rtc(dow));
    cmos_write(RTC_REG_B, regb & ~RTC_B_SET);

    printf("Day of week set to %d (%s)\n", dow, day_names[dow]);
    return 0;
}

/* ================================================================
 * RTC Mode Configuration
 * ================================================================ */

static int set_rtc_mode(const char *mode, const char *valstr)
{
    unsigned char regb = cmos_read(RTC_REG_B);

    if (strcmp(mode, "24h") == 0) {
        if (atoi(valstr))
            regb |= RTC_B_24H;
        else
            regb &= ~RTC_B_24H;
        cmos_write(RTC_REG_B, regb);
        printf("Hour format set to %s\n", (regb & RTC_B_24H) ? "24-hour" : "12-hour");
    }
    else if (strcmp(mode, "bcd") == 0) {
        if (atoi(valstr))
            regb &= ~RTC_B_DM;  /* BCD = DM bit clear */
        else
            regb |= RTC_B_DM;   /* Binary = DM bit set */
        cmos_write(RTC_REG_B, regb);
        printf("Data mode set to %s\n", (regb & RTC_B_DM) ? "Binary" : "BCD");
    }
    else if (strcmp(mode, "sqw") == 0) {
        if (atoi(valstr))
            regb |= RTC_B_SQWE;
        else
            regb &= ~RTC_B_SQWE;
        cmos_write(RTC_REG_B, regb);
        printf("Square wave output %s\n", (regb & RTC_B_SQWE) ? "ENABLED" : "disabled");
    }
    else if (strcmp(mode, "dse") == 0) {
        if (atoi(valstr))
            regb |= RTC_B_DSE;
        else
            regb &= ~RTC_B_DSE;
        cmos_write(RTC_REG_B, regb);
        printf("Daylight savings %s\n", (regb & RTC_B_DSE) ? "ENABLED" : "disabled");
    }
    else if (strcmp(mode, "pie") == 0) {
        if (atoi(valstr))
            regb |= RTC_B_PIE;
        else
            regb &= ~RTC_B_PIE;
        cmos_write(RTC_REG_B, regb);
        printf("Periodic interrupt %s\n", (regb & RTC_B_PIE) ? "ENABLED" : "disabled");
    }
    else if (strcmp(mode, "uie") == 0) {
        if (atoi(valstr))
            regb |= RTC_B_UIE;
        else
            regb &= ~RTC_B_UIE;
        cmos_write(RTC_REG_B, regb);
        printf("Update-ended interrupt %s\n", (regb & RTC_B_UIE) ? "ENABLED" : "disabled");
    }
    else if (strcmp(mode, "rate") == 0) {
        int rate = atoi(valstr);
        unsigned char rega;
        if (rate < 0 || rate > 15) {
            fprintf(stderr, "Error: Rate select 0-15\n");
            return 1;
        }
        rega = cmos_read(RTC_REG_A);
        rega = (rega & ~RTC_A_RS_MASK) | (rate & RTC_A_RS_MASK);
        cmos_write(RTC_REG_A, rega);
        printf("Periodic rate set to %d (%s)\n", rate, rate_freq[rate]);
    }
    else {
        fprintf(stderr, "Unknown RTC mode: %s\n", mode);
        fprintf(stderr, "Modes: 24h, bcd, sqw, dse, pie, uie, rate\n");
        return 1;
    }

    return 0;
}

/* ================================================================
 * Save / Load CMOS
 * ================================================================ */

static int save_cmos(const char *filename)
{
    FILE *fp;
    unsigned char data[CMOS_SIZE];
    int i;

    rtc_wait_uip();
    for (i = 0; i < CMOS_SIZE; i++)
        data[i] = cmos_read(i);

    fp = fopen(filename, "wb");
    if (!fp) {
        perror("Error opening file for writing");
        return 1;
    }
    if (fwrite((const char *)data, 1, CMOS_SIZE, fp) != CMOS_SIZE) {
        perror("Error writing CMOS data");
        fclose(fp);
        return 1;
    }
    fclose(fp);
    printf("CMOS saved to %s (%d bytes)\n", filename, CMOS_SIZE);
    return 0;
}

static int load_cmos(const char *filename)
{
    FILE *fp;
    unsigned char data[CMOS_SIZE];
    long fsize;
    int i;
    unsigned char regb;

    fp = fopen(filename, "rb");
    if (!fp) {
        perror("Error opening file");
        return 1;
    }

    fseek(fp, 0, SEEK_END);
    fsize = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    if (fsize != CMOS_SIZE && fsize != 128) {
        fprintf(stderr, "Error: File size %ld, expected %d or 128\n",
                fsize, CMOS_SIZE);
        fclose(fp);
        return 1;
    }

    if (fread((char *)data, 1, CMOS_SIZE, fp) != CMOS_SIZE) {
        perror("Error reading CMOS data");
        fclose(fp);
        return 1;
    }
    fclose(fp);

    regb = cmos_read(RTC_REG_B);
    cmos_write(RTC_REG_B, regb | RTC_B_SET);

    for (i = 0; i < CMOS_SIZE; i++) {
        if (i == RTC_REG_C || i == RTC_REG_D)
            continue;  /* read-only */
        cmos_write(i, data[i]);
    }

    cmos_write(RTC_REG_B, data[RTC_REG_B] & ~RTC_B_SET);

    printf("CMOS loaded from %s (%d bytes)\n", filename, CMOS_SIZE);
    printf("WARNING: Verify time and date are correct!\n");
    return 0;
}

/* ================================================================
 * CMOS Factory Reset
 * ================================================================ */

static void factory_reset(void)
{
    unsigned char regb;
    int i;

    printf("Resetting CMOS to PC1640 factory defaults...\n");

    regb = cmos_read(RTC_REG_B);
    cmos_write(RTC_REG_B, regb | RTC_B_SET);

    /* Register A: standard 32.768 kHz divider, 1024 Hz periodic rate */
    cmos_write(RTC_REG_A, 0x26);  /* DV=010, RS=0110 */

    /* Register B: 24-hour, BCD, no interrupts */
    cmos_write(RTC_REG_B, 0x02);  /* 24H=1, all else off */

    /* Clear diagnostic + shutdown status */
    cmos_write(CMOS_DIAG, 0x00);
    cmos_write(CMOS_SHUTDOWN, 0x00);

    /* Floppy: drive A = 720KB 3.5" (type 3, standard PC1640) */
    cmos_write(CMOS_FLOPPY, 0x30);

    /* Hard disk: none */
    cmos_write(CMOS_DISK, 0x00);

    /* Equipment: floppy present, no FPU, EGA video, 1 floppy drive */
    cmos_write(CMOS_EQUIP, 0x01);

    /* Base memory: 640 KB */
    cmos_write(CMOS_BASEMEM_LO, 0x80);
    cmos_write(CMOS_BASEMEM_HI, 0x02);

    /* Extended memory: 0 (8086 has none) */
    cmos_write(CMOS_EXTMEM_LO, 0x00);
    cmos_write(CMOS_EXTMEM_HI, 0x00);

    /* Clear extended HD types */
    cmos_write(CMOS_DISK0_EXT, 0x00);
    cmos_write(CMOS_DISK1_EXT, 0x00);

    /* Century */
    cmos_write(CMOS_CENTURY, 0x20);

    /* Clear alarm registers (wildcard = disabled) */
    cmos_write(RTC_ALARM_SEC, 0xC0);
    cmos_write(RTC_ALARM_MIN, 0xC0);
    cmos_write(RTC_ALARM_HRS, 0xC0);

    /* Clear remaining config bytes */
    for (i = 0x1B; i <= 0x2D; i++)
        cmos_write(i, 0x00);
    for (i = 0x33; i <= 0x3F; i++)
        cmos_write(i, 0x00);

    /* Resume updates */
    cmos_write(RTC_REG_B, 0x02);

    /* Update checksum last */
    cmos_update_checksum();

    printf("CMOS reset to factory defaults:\n");
    printf("  Floppy A: 720 KB 3.5\"  Floppy B: None\n");
    printf("  Hard disk: None\n");
    printf("  Video: EGA (built-in PEGA)\n");
    printf("  Memory: 640 KB base, 0 KB extended\n");
    printf("  RTC: 24-hour BCD mode\n");
    printf("  Checksum updated\n");
}

/* ================================================================
 * Debug: Comprehensive Hardware Probe
 * ================================================================ */

static void debug_probe(void)
{
    unsigned char val;

    printf("\nAmstrad PC1640 Comprehensive Hardware Probe\n");
    printf("============================================\n");
    printf("  WARNING: Some reads may have side effects\n\n");

    /* ---- Amstrad-specific ports ---- */
    printf("Amstrad System Ports:\n");

    val = inb(PORT_PB);
    printf("  0x61 PB Register:       0x%02X\n", val);

    val = inb(PORT_STATUS2);
    printf("  0x62 Status2 (raw):     0x%02X\n", val);

    val = amstrad_read_sysstat2();
    printf("  0x62 Status2 (combined):0x%02X\n", val);

    val = amstrad_read_sysstat1();
    printf("  0x60 Status1:           0x%02X\n", val);

    val = inb(PORT_MOUSE_X);
    printf("  0x78 Mouse X:           0x%02X (%d)\n", val, (signed char)val);

    val = inb(PORT_MOUSE_Y);
    printf("  0x7A Mouse Y:           0x%02X (%d)\n", val, (signed char)val);

    val = inb(PORT_IDA_STATUS);
    printf("  0x3DE IDA status:       0x%02X%s\n", val,
           (val & 0x20) ? " (IDA disabled)" : " (IDA active)");

    /* ---- LPT1 (Amstrad-overloaded) ---- */
    printf("\nLPT1 (Amstrad-overloaded):\n");
    val = inb(PORT_LPT1_DATA);
    printf("  0x378 Data:             0x%02X\n", val);
    val = inb(PORT_LPT1_STATUS);
    printf("  0x379 Status:           0x%02X\n", val);
    printf("        Language:         %d (%s)\n",
           val & LPT1_LANG_MASK, language_name(val & LPT1_LANG_MASK));
    printf("        DIP latch:        %s\n", (val & LPT1_DIP_LATCH) ? "SW10" : "SW9/none");
    printf("        Display type:     %d (%s)\n",
           (val & LPT1_DISP_MASK) >> LPT1_DISP_SHIFT,
           display_type_name((val & LPT1_DISP_MASK) >> LPT1_DISP_SHIFT));
    val = inb(PORT_LPT1_CTRL);
    printf("  0x37A Control:          0x%02X\n", val);

    /* ---- PIC ---- */
    printf("\n8259A PIC:\n");
    val = inb(PORT_PIC_DATA);
    printf("  0x21 IMR:               0x%02X\n", val);
    outb(0x0A, PORT_PIC_CMD);
    io_delay();
    val = inb(PORT_PIC_CMD);
    printf("  0x20 IRR:               0x%02X\n", val);
    outb(0x0B, PORT_PIC_CMD);
    io_delay();
    val = inb(PORT_PIC_CMD);
    printf("  0x20 ISR:               0x%02X\n", val);

    /* ---- DMA ---- */
    printf("\n8237A DMA:\n");
    val = inb(PORT_DMA_STAT);
    printf("  0x08 Status:            0x%02X\n", val);

    /* ---- CMOS key registers ---- */
    printf("\nMC146818 CMOS (selected):\n");
    printf("  0x0A Reg A:             0x%02X\n", cmos_read(RTC_REG_A));
    printf("  0x0B Reg B:             0x%02X\n", cmos_read(RTC_REG_B));
    printf("  0x0C Reg C:             0x%02X (flags cleared by read)\n", cmos_read(RTC_REG_C));
    val = cmos_read(RTC_REG_D);
    printf("  0x0D Reg D:             0x%02X (%s)\n", val,
           (val & RTC_D_VRT) ? "battery OK" : "BATTERY DEAD");
    printf("  0x0E Diagnostic:        0x%02X\n", cmos_read(CMOS_DIAG));
    printf("  0x0F Shutdown:          0x%02X\n", cmos_read(CMOS_SHUTDOWN));
    printf("  0x10 Floppy:            0x%02X\n", cmos_read(CMOS_FLOPPY));
    printf("  0x12 Hard disk:         0x%02X\n", cmos_read(CMOS_DISK));
    printf("  0x14 Equipment:         0x%02X\n", cmos_read(CMOS_EQUIP));

    {
        unsigned short bm = cmos_read(CMOS_BASEMEM_LO) |
                            ((unsigned short)cmos_read(CMOS_BASEMEM_HI) << 8);
        printf("  0x15-16 Base mem:       %u KB\n", bm);
    }

    printf("  0x2E-2F Checksum:       0x%02X%02X (%s)\n",
           cmos_read(CMOS_CHECKSUM_HI), cmos_read(CMOS_CHECKSUM_LO),
           cmos_verify_checksum() ? "valid" : "INVALID");
    printf("  0x32 Century:           0x%02X\n", cmos_read(CMOS_CENTURY));

    /* ---- Serial port detection ---- */
    printf("\nSerial Ports:\n");
    printf("  COM1 (0x3F8):           %s\n",
           detect_com_port(PORT_COM1_BASE) ? "Present" : "Not found");
    printf("  COM2 (0x2F8):           %s\n",
           detect_com_port(PORT_COM2_BASE) ? "Present" : "Not found");

    /* ---- Parallel port detection ---- */
    printf("\nParallel Ports:\n");
    printf("  LPT1 (0x378):           Present (Amstrad)\n");
    printf("  LPT2 (0x3BC):           %s\n",
           detect_lpt_port(PORT_LPT2_DATA) ? "Present" : "Not found");

    /* ---- Dead-man port ---- */
    printf("\nDiagnostic:\n");
    val = inb(PORT_DEAD);
    printf("  0xDEAD Dead-man:        0x%02X\n", val);

    /* ---- Platform ID ---- */
    printf("\nPlatform Identification:\n");
    {
        unsigned short bm = cmos_read(CMOS_BASEMEM_LO) |
                            ((unsigned short)cmos_read(CMOS_BASEMEM_HI) << 8);
        printf("  Base memory:            %u KB %s\n", bm,
               (bm == 640) ? "(PC1640 standard)" : "");
    }
    printf("  Video BIOS:             Paradise PEGA v2.015 (at C000:0000)\n");
    printf("  System BIOS:            Amstrad PC1640 (C) 1987 Amstrad plc\n");
    printf("  CPU:                    8086 @ 8 MHz\n");
    printf("  Chipset:                Amstrad custom\n");
}

/* ================================================================
 * Debug: NVR Protocol Trace
 * ================================================================ */

static void debug_nvr_trace(void)
{
    int i;

    printf("\nNVR Protocol Trace (port 0x65 -> port 0x62):\n");
    printf("  Addr  Wr65  Rd62(hi)  Rd62(lo)  Combined\n");
    printf("  ----  ----  --------  --------  --------\n");

    for (i = 0; i < 16; i++) {
        unsigned char hi_nib, lo_nib, pb;

        outb((unsigned char)i, PORT_SYSSTAT2_WR);
        io_delay();

        pb = inb(PORT_PB);

        outb(pb & ~PB_NIBBLE_SEL, PORT_PB);
        io_delay();
        hi_nib = inb(PORT_STATUS2) & 0x0F;

        outb(pb | PB_NIBBLE_SEL, PORT_PB);
        io_delay();
        lo_nib = inb(PORT_STATUS2) & 0x0F;

        outb(pb, PORT_PB);

        printf("  0x%02X  0x%02X    0x%X       0x%X      0x%02X\n",
               i, i, hi_nib, lo_nib, (hi_nib << 4) | lo_nib);
    }
}

/* ================================================================
 * Debug: Port Read/Write
 * ================================================================ */

static int port_read(const char *portstr)
{
    unsigned int port = (unsigned int)strtol(portstr, NULL, 0);
    unsigned char val;

    if (port > 0xFFFF) {
        fprintf(stderr, "Error: Port must be 0x0000-0xFFFF\n");
        return 1;
    }

    val = inb(port);
    printf("IN  port 0x%04X = 0x%02X (%u)\n", port, val, val);
    return 0;
}

static int port_write(const char *portstr, const char *valstr)
{
    unsigned int port = (unsigned int)strtol(portstr, NULL, 0);
    unsigned int val = (unsigned int)strtol(valstr, NULL, 0);

    if (port > 0xFFFF) {
        fprintf(stderr, "Error: Port must be 0x0000-0xFFFF\n");
        return 1;
    }
    if (val > 0xFF) {
        fprintf(stderr, "Error: Value must be 0x00-0xFF\n");
        return 1;
    }

    outb(val, port);
    printf("OUT port 0x%04X = 0x%02X\n", port, val);
    return 0;
}

/* ================================================================
 * Dead-Man Diagnostic Port
 * ================================================================ */

static void show_deadman(void)
{
    unsigned char val = inb(PORT_DEAD);
    printf("\nDead-Man Diagnostic Port (0xDEAD): 0x%02X\n", val);
    printf("  This port stores the last POST progress code.\n");
    printf("  If the system hangs during POST, this value\n");
    printf("  indicates which test stage failed.\n");
}

/* ================================================================
 * Soft Reset
 * ================================================================ */

static void soft_reset(void)
{
    printf("Triggering soft reset via port 0x66...\n");
    printf("System will reboot NOW!\n");
    outb(0x00, PORT_SOFT_RESET);
    /* Should not reach here */
    printf("ERROR: Soft reset failed!\n");
}

/* ================================================================
 * Game Port Detection
 * ================================================================ */

static void show_gameport(void)
{
    unsigned char val;

    printf("\nGame Port (Joystick):\n");
    printf("  Port: 0x201\n");

    val = inb(PORT_GAME);
    printf("  Raw read: 0x%02X\n", val);
    printf("    Button 1: %s\n", (val & 0x10) ? "Released" : "PRESSED");
    printf("    Button 2: %s\n", (val & 0x20) ? "Released" : "PRESSED");
    printf("    Button 3: %s\n", (val & 0x40) ? "Released" : "PRESSED");
    printf("    Button 4: %s\n", (val & 0x80) ? "Released" : "PRESSED");
    printf("    Axis bits: 0x%X (timing-based, snapshot only)\n", val & 0x0F);
}

/* ================================================================
 * Summary: show all configuration at once
 * ================================================================ */

static void show_all(void)
{
    printf("Amstrad PC1640 NVR Full Configuration\n");
    printf("======================================\n");
    show_time();
    show_alarm();
    show_rtc_status();
    show_floppy();
    show_harddisk();
    show_equipment();
    show_memory();
    show_diagnostics();
    show_amstrad_full();
    show_display_type();
    show_amstrad_language();
    show_ports();
    show_mouse();
    show_gameport();
    show_pic();
    show_dma();
    show_pit();
    show_deadman();
    printf("\nCMOS checksum: %s\n",
           cmos_verify_checksum() ? "Valid" : "*** INVALID ***");
}

/* ================================================================
 * RTC Watch Mode - continuously display time
 * ================================================================ */

static void watch_time(void)
{
    printf("RTC Watch Mode (Ctrl+C to stop):\n\n");

    for (;;) {
        unsigned char sec, min, hrs, regb;

        rtc_wait_uip();
        sec = cmos_read(RTC_SECONDS);
        min = cmos_read(RTC_MINUTES);
        hrs = cmos_read(RTC_HOURS);
        regb = cmos_read(RTC_REG_B);

        sec = rtc_to_bin(sec);
        min = rtc_to_bin(min);
        if (regb & RTC_B_24H) {
            hrs = rtc_to_bin(hrs);
        } else {
            int pm = hrs & 0x80;
            hrs &= 0x7F;
            hrs = rtc_to_bin(hrs);
            if (pm) { if (hrs < 12) hrs += 12; }
            else    { if (hrs == 12) hrs = 0;  }
        }

        printf("  %02d:%02d:%02d\r", hrs, min, sec);

        /* Wait roughly until next second */
        {
            unsigned char last_sec = sec;
            long timeout = 100000L;
            while (rtc_to_bin(cmos_read(RTC_SECONDS)) == last_sec && --timeout > 0)
                ;
        }
    }
}

/* ================================================================
 * CMOS Fill Range
 * ================================================================ */

static int fill_cmos(const char *startstr, const char *endstr, const char *valstr)
{
    unsigned int start = (unsigned int)strtol(startstr, NULL, 0);
    unsigned int end = (unsigned int)strtol(endstr, NULL, 0);
    unsigned int val = (unsigned int)strtol(valstr, NULL, 0);
    unsigned int i;

    if (start >= CMOS_SIZE || end >= CMOS_SIZE || start > end) {
        fprintf(stderr, "Error: Range must be within 0x00-0x3F\n");
        return 1;
    }
    if (val > 0xFF) {
        fprintf(stderr, "Error: Value must be 0x00-0xFF\n");
        return 1;
    }

    for (i = start; i <= end; i++) {
        if (i == RTC_REG_C || i == RTC_REG_D)
            continue;
        cmos_write(i, val);
    }

    cmos_update_checksum();
    printf("CMOS 0x%02X-0x%02X filled with 0x%02X\n", start, end, val);
    return 0;
}

/* ================================================================
 * Battery Health Reporting
 * ================================================================ */

static void show_battery(void)
{
    unsigned char regd, diag;

    regd = cmos_read(RTC_REG_D);
    diag = cmos_read(CMOS_DIAG);

    printf("\nBattery Status:\n");
    printf("  Register D VRT flag: %s\n",
           (regd & RTC_D_VRT) ? "SET (battery OK, RAM valid)" : "CLEAR (battery dead!)");
    printf("  Diagnostic bit 7:   %s\n",
           (diag & 0x80) ? "SET (power was lost)" : "CLEAR (continuous power)");

    if (!(regd & RTC_D_VRT)) {
        printf("\n  *** WARNING: Battery is dead or disconnected! ***\n");
        printf("  All CMOS settings will be lost on power-off.\n");
        printf("  Replace the 4x AA batteries in the monitor base.\n");
    } else if (diag & 0x80) {
        printf("\n  Battery was previously depleted or disconnected.\n");
        printf("  CMOS may contain incorrect settings.\n");
        printf("  Use 'nvr factory-reset' to restore defaults.\n");
    } else {
        printf("\n  Battery and CMOS RAM are healthy.\n");
    }
}

/* ================================================================
 * Usage
 * ================================================================ */

static void usage(const char *prog)
{
    printf(
        "Amstrad PC1640 NVR Configuration Utility - Comprehensive Edition\n"
        "For use with ELKS on original PC1640 hardware\n"
        "\n"
        "Usage: %s [options] <command> [args...]\n"
        "\n"
        "Options:\n"
        "  -d, --debug          Increase debug verbosity (repeat for more)\n"
        "  -h, --help           Show this help\n"
        "\n"
        "=== Configuration Display ===\n"
        "  show                 Show full system configuration (default)\n"
        "  time                 Show current date and time\n"
        "  alarm                Show alarm settings\n"
        "  floppy               Show floppy drive configuration\n"
        "  harddisk             Show hard disk configuration\n"
        "  equipment            Show equipment byte breakdown\n"
        "  memory               Show memory configuration\n"
        "  status               Show RTC status registers (detailed)\n"
        "  diag                 Show diagnostic & shutdown status\n"
        "  battery              Show battery health\n"
        "\n"
        "=== Amstrad-Specific ===\n"
        "  amstrad              Show all Amstrad system status (ports/latches)\n"
        "  language             Show language selection (DIP switches)\n"
        "  display              Show display type detection\n"
        "  mouse                Show Amstrad mouse port status\n"
        "  mouse-test           Interactive mouse movement test (5 sec)\n"
        "  mouse-reset          Reset mouse counters to 0\n"
        "\n"
        "=== Hardware Diagnostics ===\n"
        "  ports                Detect serial/parallel ports\n"
        "  gameport             Show game/joystick port status\n"
        "  pic                  Show 8259A PIC status (IRQ mask/request)\n"
        "  dma                  Show 8237A DMA status\n"
        "  pit                  Show 8253 PIT timer status\n"
        "  deadman              Read dead-man diagnostic port (0xDEAD)\n"
        "  speaker-test         Play test tones through PC speaker\n"
        "  beep FREQ            Play tone at FREQ Hz (20-20000)\n"
        "\n"
        "=== Time/Date Setting ===\n"
        "  set-time HH:MM:SS   Set the RTC time\n"
        "  set-date DD/MM/YYYY Set the RTC date\n"
        "  set-dow N            Set day of week (1=Sun - 7=Sat)\n"
        "  set-alarm HH:MM:SS  Set alarm time (-1 for wildcard)\n"
        "  alarm-enable         Enable alarm interrupt\n"
        "  alarm-disable        Disable alarm interrupt\n"
        "  watch                Continuously display time (Ctrl+C to stop)\n"
        "\n"
        "=== Drive Configuration ===\n"
        "  set-floppy A|B TYPE  Set floppy type (0-4)\n"
        "  set-harddisk 0|1 TYPE Set hard disk type (0-15)\n"
        "\n"
        "=== Equipment Configuration ===\n"
        "  set-equip FIELD VAL  Set equipment field:\n"
        "                       fpu 0|1, video 0-3, floppy-count 0-4\n"
        "  set-basemem KB       Set base memory (64-640)\n"
        "\n"
        "=== RTC Mode Configuration ===\n"
        "  set-rtc MODE VAL     Set RTC mode:\n"
        "                       24h 0|1, bcd 0|1, sqw 0|1,\n"
        "                       dse 0|1, pie 0|1, uie 0|1,\n"
        "                       rate 0-15\n"
        "\n"
        "=== CMOS Operations ===\n"
        "  dump                 Hex dump of all 64 CMOS bytes\n"
        "  read ADDR            Read single CMOS byte (0x00-0x3F)\n"
        "  write ADDR VAL       Write single CMOS byte\n"
        "  fill START END VAL   Fill CMOS range with value\n"
        "  checksum             Verify/recalculate CMOS checksum\n"
        "  save FILE            Save CMOS to binary file\n"
        "  load FILE            Load CMOS from binary file\n"
        "  compare FILE         Compare live CMOS vs saved file\n"
        "  factory-reset        Reset CMOS to PC1640 factory defaults\n"
        "  clear-diag           Clear diagnostic status byte\n"
        "\n"
        "=== Debug ===\n"
        "  probe                Full hardware port probe\n"
        "  trace                NVR port protocol trace\n"
        "  inb PORT             Read I/O port (hex)\n"
        "  outb PORT VAL        Write I/O port (hex)\n"
        "  soft-reset           Trigger soft reset via port 0x66\n"
        "\n"
        "Floppy types: 0=None 1=360K 5.25\" 2=1.2M 5.25\" 3=720K 3.5\" 4=1.44M 3.5\"\n"
        "HD types: 0=None 1-14=Standard geometries 15=Extended (CMOS 0x19/0x1A)\n"
        "Video modes: 0=EGA 1=40col-CGA 2=80col-CGA 3=MDA/Hercules\n"
        "\n"
        "Notes:\n"
        "  - Must run as root for port I/O access\n"
        "  - PC1640 uses 64-byte CMOS (0x00-0x3F, not 128)\n"
        "  - RTC alarm routes to IRQ 1 (not IRQ 8)\n"
        "  - Port 0x70 bit 7 does NOT control NMI on Amstrad\n"
        "  - Mouse uses ports 0x78/0x7A + keyboard scancodes\n"
        "  - LPT1 status is overloaded with language/display info\n"
        "\n"
        "Examples:\n"
        "  %s show\n"
        "  %s set-time 14:30:00\n"
        "  %s set-date 25/12/2026\n"
        "  %s set-floppy A 4\n"
        "  %s set-harddisk 0 2\n"
        "  %s set-alarm -1:-1:00\n"
        "  %s set-equip video 0\n"
        "  %s set-rtc 24h 1\n"
        "  %s dump\n"
        "  %s save backup.nvr\n"
        "  %s compare backup.nvr\n"
        "  %s factory-reset\n"
        "  %s -ddd probe\n",
        prog, prog, prog, prog, prog, prog, prog,
        prog, prog, prog, prog, prog, prog, prog
    );
}

/* ================================================================
 * Main
 * ================================================================ */

int main(int argc, char *argv[])
{
    int i;
    const char *cmd;

    /* Parse options */
    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-d") == 0 || strcmp(argv[i], "--debug") == 0) {
            debug_level++;
        } else if (strncmp(argv[i], "-d", 2) == 0 && argv[i][2] == 'd') {
            /* Handle -dd, -ddd etc */
            const char *p = argv[i] + 1;
            while (*p == 'd') { debug_level++; p++; }
        } else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            usage(argv[0]);
            return 0;
        } else {
            break;
        }
    }

    if (i >= argc)
        cmd = "show";
    else
        cmd = argv[i];

    DBG(1, "Debug level: %d, Command: %s\n", debug_level, cmd);

    /* ---- Configuration Display ---- */
    if (strcmp(cmd, "show") == 0) {
        show_all();
    }
    else if (strcmp(cmd, "time") == 0) {
        show_time();
    }
    else if (strcmp(cmd, "alarm") == 0) {
        show_alarm();
    }
    else if (strcmp(cmd, "floppy") == 0) {
        show_floppy();
    }
    else if (strcmp(cmd, "harddisk") == 0 || strcmp(cmd, "hd") == 0) {
        show_harddisk();
    }
    else if (strcmp(cmd, "equipment") == 0 || strcmp(cmd, "equip") == 0) {
        show_equipment();
    }
    else if (strcmp(cmd, "memory") == 0 || strcmp(cmd, "mem") == 0) {
        show_memory();
    }
    else if (strcmp(cmd, "status") == 0) {
        show_rtc_status();
    }
    else if (strcmp(cmd, "diag") == 0) {
        show_diagnostics();
    }
    else if (strcmp(cmd, "battery") == 0 || strcmp(cmd, "bat") == 0) {
        show_battery();
    }

    /* ---- Amstrad-specific ---- */
    else if (strcmp(cmd, "amstrad") == 0) {
        show_amstrad_full();
    }
    else if (strcmp(cmd, "language") == 0 || strcmp(cmd, "lang") == 0) {
        show_amstrad_language();
    }
    else if (strcmp(cmd, "display") == 0 || strcmp(cmd, "video") == 0) {
        show_display_type();
    }
    else if (strcmp(cmd, "mouse") == 0) {
        show_mouse();
    }
    else if (strcmp(cmd, "mouse-test") == 0) {
        mouse_test();
    }
    else if (strcmp(cmd, "mouse-reset") == 0) {
        mouse_reset();
    }

    /* ---- Hardware diagnostics ---- */
    else if (strcmp(cmd, "ports") == 0) {
        show_ports();
    }
    else if (strcmp(cmd, "gameport") == 0 || strcmp(cmd, "joystick") == 0) {
        show_gameport();
    }
    else if (strcmp(cmd, "pic") == 0) {
        show_pic();
    }
    else if (strcmp(cmd, "dma") == 0) {
        show_dma();
    }
    else if (strcmp(cmd, "pit") == 0 || strcmp(cmd, "timer") == 0) {
        show_pit();
    }
    else if (strcmp(cmd, "deadman") == 0 || strcmp(cmd, "dead") == 0) {
        show_deadman();
    }
    else if (strcmp(cmd, "speaker-test") == 0) {
        speaker_test();
    }
    else if (strcmp(cmd, "beep") == 0) {
        if (i + 1 >= argc) {
            fprintf(stderr, "Error: beep requires frequency argument\n");
            return 1;
        }
        return speaker_beep(argv[i + 1]);
    }

    /* ---- Time/date setting ---- */
    else if (strcmp(cmd, "set-time") == 0) {
        if (i + 1 >= argc) {
            fprintf(stderr, "Usage: set-time HH:MM:SS\n");
            return 1;
        }
        return set_time(argv[i + 1]);
    }
    else if (strcmp(cmd, "set-date") == 0) {
        if (i + 1 >= argc) {
            fprintf(stderr, "Usage: set-date DD/MM/YYYY\n");
            return 1;
        }
        return set_date(argv[i + 1]);
    }
    else if (strcmp(cmd, "set-dow") == 0) {
        if (i + 1 >= argc) {
            fprintf(stderr, "Usage: set-dow 1-7 (1=Sunday)\n");
            return 1;
        }
        return set_dow(argv[i + 1]);
    }
    else if (strcmp(cmd, "set-alarm") == 0) {
        if (i + 1 >= argc) {
            fprintf(stderr, "Usage: set-alarm HH:MM:SS (-1 for wildcard)\n");
            return 1;
        }
        return set_alarm(argv[i + 1]);
    }
    else if (strcmp(cmd, "alarm-enable") == 0) {
        alarm_enable(1);
    }
    else if (strcmp(cmd, "alarm-disable") == 0) {
        alarm_enable(0);
    }
    else if (strcmp(cmd, "watch") == 0) {
        watch_time();
    }

    /* ---- Drive configuration ---- */
    else if (strcmp(cmd, "set-floppy") == 0) {
        if (i + 2 >= argc) {
            fprintf(stderr, "Usage: set-floppy A|B TYPE\n");
            return 1;
        }
        return set_floppy(argv[i + 1], argv[i + 2]);
    }
    else if (strcmp(cmd, "set-harddisk") == 0 || strcmp(cmd, "set-hd") == 0) {
        if (i + 2 >= argc) {
            fprintf(stderr, "Usage: set-harddisk 0|1 TYPE\n");
            return 1;
        }
        return set_harddisk(argv[i + 1], argv[i + 2]);
    }

    /* ---- Equipment configuration ---- */
    else if (strcmp(cmd, "set-equip") == 0) {
        if (i + 2 >= argc) {
            fprintf(stderr, "Usage: set-equip FIELD VAL\n");
            fprintf(stderr, "Fields: fpu, video, floppy-count\n");
            return 1;
        }
        return set_equipment(argv[i + 1], argv[i + 2]);
    }
    else if (strcmp(cmd, "set-basemem") == 0) {
        if (i + 1 >= argc) {
            fprintf(stderr, "Usage: set-basemem KB\n");
            return 1;
        }
        return set_basemem(argv[i + 1]);
    }

    /* ---- RTC mode configuration ---- */
    else if (strcmp(cmd, "set-rtc") == 0) {
        if (i + 2 >= argc) {
            fprintf(stderr, "Usage: set-rtc MODE VAL\n");
            fprintf(stderr, "Modes: 24h bcd sqw dse pie uie rate\n");
            return 1;
        }
        return set_rtc_mode(argv[i + 1], argv[i + 2]);
    }

    /* ---- CMOS operations ---- */
    else if (strcmp(cmd, "dump") == 0) {
        dump_cmos();
    }
    else if (strcmp(cmd, "read") == 0) {
        if (i + 1 >= argc) {
            fprintf(stderr, "Usage: read ADDR\n");
            return 1;
        }
        return raw_read(argv[i + 1]);
    }
    else if (strcmp(cmd, "write") == 0) {
        if (i + 2 >= argc) {
            fprintf(stderr, "Usage: write ADDR VAL\n");
            return 1;
        }
        return raw_write(argv[i + 1], argv[i + 2]);
    }
    else if (strcmp(cmd, "fill") == 0) {
        if (i + 3 >= argc) {
            fprintf(stderr, "Usage: fill START END VAL\n");
            return 1;
        }
        return fill_cmos(argv[i + 1], argv[i + 2], argv[i + 3]);
    }
    else if (strcmp(cmd, "checksum") == 0) {
        if (cmos_verify_checksum()) {
            printf("CMOS checksum is valid\n");
        } else {
            printf("CMOS checksum is INVALID - recalculating...\n");
            cmos_update_checksum();
            printf("Checksum updated\n");
        }
    }
    else if (strcmp(cmd, "save") == 0) {
        if (i + 1 >= argc) {
            fprintf(stderr, "Usage: save FILE\n");
            return 1;
        }
        return save_cmos(argv[i + 1]);
    }
    else if (strcmp(cmd, "load") == 0) {
        if (i + 1 >= argc) {
            fprintf(stderr, "Usage: load FILE\n");
            return 1;
        }
        return load_cmos(argv[i + 1]);
    }
    else if (strcmp(cmd, "compare") == 0 || strcmp(cmd, "diff") == 0) {
        if (i + 1 >= argc) {
            fprintf(stderr, "Usage: compare FILE\n");
            return 1;
        }
        return compare_cmos(argv[i + 1]);
    }
    else if (strcmp(cmd, "factory-reset") == 0) {
        factory_reset();
    }
    else if (strcmp(cmd, "clear-diag") == 0) {
        clear_diagnostics();
    }

    /* ---- Debug ---- */
    else if (strcmp(cmd, "probe") == 0) {
        debug_probe();
    }
    else if (strcmp(cmd, "trace") == 0) {
        debug_nvr_trace();
    }
    else if (strcmp(cmd, "inb") == 0) {
        if (i + 1 >= argc) {
            fprintf(stderr, "Usage: inb PORT\n");
            return 1;
        }
        return port_read(argv[i + 1]);
    }
    else if (strcmp(cmd, "outb") == 0) {
        if (i + 2 >= argc) {
            fprintf(stderr, "Usage: outb PORT VAL\n");
            return 1;
        }
        return port_write(argv[i + 1], argv[i + 2]);
    }
    else if (strcmp(cmd, "soft-reset") == 0 || strcmp(cmd, "reboot") == 0) {
        soft_reset();
    }
    else {
        fprintf(stderr, "Unknown command: %s\n", cmd);
        fprintf(stderr, "Use '%s --help' for usage information\n", argv[0]);
        return 1;
    }

    return 0;
}
