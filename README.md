# Amstrad PC1640 NVR Configuration Utility — Comprehensive Edition

POSIX-style NVR (Non-Volatile RAM) configuration and system diagnostic
utility for the Amstrad PC1640, targeting **ELKS** (Embeddable Linux
Kernel Subset) running on real hardware.

This replaces the original `NVR.EXE` that shipped on the Amstrad User Disk,
and goes far beyond it — covering every hardware feature the BIOS exposes.

## Hardware Reference

All register definitions and I/O protocols were reverse-engineered from the
original Amstrad PC1640 BIOS ROMs (`40043.v3` / `40044.v3`, interleaved
hi/lo byte pair, 8KB each = 16KB combined), and verified against the PCem
emulator source code.

### I/O Port Map (PC1640-Specific)

| Port     | Dir   | Function                                      |
|----------|-------|-----------------------------------------------|
| `0x60`   | Read  | Keyboard data / system status 1 (PB.7 sel)    |
| `0x61`   | R/W   | PB register (speaker, keyboard, nibble sel)    |
| `0x62`   | Read  | System status 2 (PB.2 selects nibble)          |
| `0x64`   | Write | System status 1 latch                          |
| `0x65`   | Write | System status 2 latch / NVR address            |
| `0x66`   | Write | Soft reset trigger                             |
| `0x70`   | Write | MC146818 RTC/CMOS address register             |
| `0x71`   | R/W   | MC146818 RTC/CMOS data register                |
| `0x78`   | R/W   | Amstrad mouse X counter                        |
| `0x7A`   | R/W   | Amstrad mouse Y counter                        |
| `0x0201` | Read  | Game/joystick port                             |
| `0x0378` | R/W   | LPT1 data (OR'd with language bits on read)    |
| `0x0379` | Read  | LPT1 status: lang (0-2), DIP (5), display (6-7)|
| `0x03DB` | Write | Video CGA/EGA switch (bit 6)                   |
| `0x03DE` | Read  | IDA status (0x20 = internal display disabled)  |
| `0xDEAD` | Read  | Dead-man diagnostic port (POST progress)       |

### NVR Protocol

The PC1640's "NVR" is a standard **MC146818** RTC/CMOS chip with
64 bytes of battery-backed RAM (addresses `0x00`-`0x3F`).

**Read:** `OUT 0x70, address` → `IN 0x71` returns data
**Write:** `OUT 0x70, address` → `OUT 0x71, data`

### Amstrad-Specific Differences from Standard AT

- CMOS address mask is `0x3F` (64 bytes only, not 128)
- Bit 7 of port `0x70` does **NOT** control NMI mask
- RTC alarm interrupt routes to **IRQ 1** (not IRQ 8)
- Single 8259A PIC only (no secondary — 8086 system)
- Port `0x62` reads system configuration with nibble select via PB bit 2
- Port `0x66` triggers a soft reset — never write to it accidentally
- Amstrad mouse uses ports `0x78`/`0x7A` with keyboard scancodes for buttons
- LPT1 status register is overloaded with language and display type info

### CMOS Register Layout

| Addr  | Register            | Format |
|-------|---------------------|--------|
| `0x00`| Seconds             | BCD    |
| `0x01`| Alarm seconds       | BCD/WC |
| `0x02`| Minutes             | BCD    |
| `0x03`| Alarm minutes       | BCD/WC |
| `0x04`| Hours               | BCD    |
| `0x05`| Alarm hours         | BCD/WC |
| `0x06`| Day of week         | BCD    |
| `0x07`| Day of month        | BCD    |
| `0x08`| Month               | BCD    |
| `0x09`| Year (2-digit)      | BCD    |
| `0x0A`| Status Register A   | Binary |
| `0x0B`| Status Register B   | Binary |
| `0x0C`| Status Register C   | R/O    |
| `0x0D`| Status Register D   | R/O    |
| `0x0E`| Diagnostic status   | Binary |
| `0x0F`| Shutdown status     | Binary |
| `0x10`| Floppy drive types  | Binary |
| `0x12`| Hard disk types     | Binary |
| `0x14`| Equipment byte      | Binary |
| `0x15`| Base memory (low)   | Binary |
| `0x16`| Base memory (high)  | Binary |
| `0x17`| Extended mem (low)  | Binary |
| `0x18`| Extended mem (high) | Binary |
| `0x19`| HD 0 extended type  | Binary |
| `0x1A`| HD 1 extended type  | Binary |
| `0x2E`| Checksum (high)     | Binary |
| `0x2F`| Checksum (low)      | Binary |
| `0x32`| Century             | BCD    |

WC = Wildcard (`0xC0`-`0xFF` means "don't care")

## Building

### Prerequisites

Install the ia16-elf cross-compiler:

```sh
# Debian/Ubuntu
sudo add-apt-repository ppa:tkchia/build-ia16
sudo apt-get update
sudo apt-get install gcc-ia16-elf libia16-elf-dev

# Or from source: https://github.com/tkchia/gcc-ia16
```

### Build for ELKS

```sh
make                # Build ELKS binary
make strip          # Build and strip for smaller size
make disasm         # Generate disassembly listing
```

### Build native (for testing UI only)

```sh
make native         # Linux x86 native binary
```

### Install to ELKS root filesystem

```sh
make install ELKS_ROOT=/path/to/elks/rootfs
```

## Usage

```
nvr [options] <command> [args...]
```

### Options

| Option         | Description                       |
|---------------|-----------------------------------|
| `-d`          | Enable debug output               |
| `-dd`         | More verbose debug                |
| `-ddd`        | Maximum debug (port-level traces) |
| `-h, --help`  | Show help                         |

### Commands — Configuration Display

| Command         | Alias   | Description                           |
|-----------------|---------|---------------------------------------|
| `show`          |         | Show all configuration (default)      |
| `time`          |         | Show current date and time            |
| `alarm`         |         | Show alarm settings                   |
| `floppy`        |         | Show floppy drive configuration       |
| `harddisk`      | `hd`    | Show hard disk configuration          |
| `equipment`     | `equip` | Show equipment byte breakdown         |
| `memory`        | `mem`   | Show memory configuration             |
| `status`        |         | Show RTC status registers (detailed)  |
| `diag`          |         | Show diagnostic & shutdown status     |
| `battery`       | `bat`   | Show battery health report            |

### Commands — Amstrad-Specific

| Command         | Alias   | Description                           |
|-----------------|---------|---------------------------------------|
| `amstrad`       |         | Show all Amstrad system status        |
| `language`      | `lang`  | Show language selection (DIP switches)|
| `display`       | `video` | Show display type detection           |
| `mouse`         |         | Show mouse port counters              |
| `mouse-test`    |         | Interactive mouse test (5 seconds)    |
| `mouse-reset`   |         | Reset mouse counters to 0             |

### Commands — Hardware Diagnostics

| Command         | Alias     | Description                         |
|-----------------|-----------|-------------------------------------|
| `ports`         |           | Detect serial/parallel ports        |
| `gameport`      | `joystick`| Show game/joystick port status      |
| `pic`           |           | Show 8259A PIC status               |
| `dma`           |           | Show 8237A DMA status               |
| `pit`           | `timer`   | Show 8253 PIT timer status          |
| `deadman`       | `dead`    | Read dead-man diagnostic port       |
| `speaker-test`  |           | Play test tones through speaker     |
| `beep FREQ`     |           | Play tone at FREQ Hz (20-20000)     |

### Commands — Time/Date Setting

| Command                | Description                              |
|------------------------|------------------------------------------|
| `set-time HH:MM:SS`   | Set the RTC time                         |
| `set-date DD/MM/YYYY`  | Set the RTC date                         |
| `set-dow N`             | Set day of week (1=Sunday, 7=Saturday)   |
| `set-alarm HH:MM:SS`  | Set alarm (-1 for wildcard)              |
| `alarm-enable`         | Enable alarm interrupt                   |
| `alarm-disable`        | Disable alarm interrupt                  |
| `watch`                | Continuously display time (Ctrl+C stop)  |

### Commands — Drive Configuration

| Command                  | Description                              |
|--------------------------|------------------------------------------|
| `set-floppy A\|B TYPE`  | Set floppy type (0-4)                    |
| `set-harddisk 0\|1 TYPE`| Set hard disk type (0-15)                |

### Commands — Equipment Configuration

| Command                 | Description                               |
|-------------------------|-------------------------------------------|
| `set-equip FIELD VAL`  | Set equipment: fpu, video, floppy-count   |
| `set-basemem KB`        | Set base memory (64-640 KB)               |

### Commands — RTC Mode Configuration

| Command            | Description                                    |
|--------------------|------------------------------------------------|
| `set-rtc 24h 0\|1` | Toggle 24-hour / 12-hour mode                  |
| `set-rtc bcd 0\|1` | Toggle BCD / binary data mode                  |
| `set-rtc sqw 0\|1` | Toggle square wave output                      |
| `set-rtc dse 0\|1` | Toggle daylight savings                        |
| `set-rtc pie 0\|1` | Toggle periodic interrupt                      |
| `set-rtc uie 0\|1` | Toggle update-ended interrupt                  |
| `set-rtc rate N`   | Set periodic rate (0-15)                       |

### Commands — CMOS Operations

| Command                | Alias   | Description                        |
|------------------------|---------|------------------------------------|
| `dump`                 |         | Hex dump all 64 CMOS bytes         |
| `read ADDR`            |         | Read single CMOS byte              |
| `write ADDR VAL`       |         | Write single CMOS byte             |
| `fill START END VAL`   |         | Fill CMOS range with value         |
| `checksum`             |         | Verify/repair CMOS checksum        |
| `save FILE`            |         | Save CMOS image to file            |
| `load FILE`            |         | Load CMOS image from file          |
| `compare FILE`         | `diff`  | Compare live CMOS vs saved file    |
| `factory-reset`        |         | Reset to PC1640 factory defaults   |
| `clear-diag`           |         | Clear diagnostic status byte       |

### Commands — Debug

| Command                | Alias    | Description                       |
|------------------------|----------|-----------------------------------|
| `probe`                |          | Full hardware port probe          |
| `trace`                |          | NVR port protocol trace           |
| `inb PORT`             |          | Read I/O port (hex)               |
| `outb PORT VAL`        |          | Write I/O port (hex)              |
| `soft-reset`           | `reboot` | Trigger soft reset via port 0x66  |

### Examples

```sh
nvr                         # Show full configuration
nvr time                    # Show date/time
nvr set-time 14:30:00       # Set time to 2:30 PM
nvr set-date 25/12/2026     # Set date
nvr set-dow 4               # Set day to Wednesday
nvr set-floppy A 4          # Set drive A to 1.44MB 3.5"
nvr set-harddisk 0 2        # Set drive C: to type 2 (20MB)
nvr set-equip video 0       # Set initial video to EGA
nvr set-equip fpu 1         # Mark 8087 as installed
nvr set-basemem 640         # Set base memory to 640KB
nvr set-alarm -1:-1:00      # Alarm every minute at :00
nvr alarm-enable            # Arm the alarm
nvr set-rtc 24h 1           # Switch to 24-hour mode
nvr battery                 # Check battery health
nvr mouse-test              # Test Amstrad mouse (5 sec)
nvr speaker-test            # Test PC speaker
nvr beep 440                # Play 440 Hz tone
nvr dump                    # Hex dump CMOS
nvr save backup.nvr         # Backup CMOS to file
nvr load backup.nvr         # Restore CMOS from file
nvr compare backup.nvr      # Show what changed vs backup
nvr factory-reset           # Reset to factory defaults
nvr -ddd probe              # Maximum-verbosity hardware probe
nvr inb 0xDEAD              # Read dead-man port
nvr pic                     # Show interrupt controller status
nvr checksum                # Verify and fix checksum
nvr watch                   # Continuously display time
```

### Floppy Drive Types

| Type | Description     |
|------|-----------------|
| 0    | Not installed   |
| 1    | 360KB 5.25"     |
| 2    | 1.2MB 5.25"     |
| 3    | 720KB 3.5"      |
| 4    | 1.44MB 3.5"     |

### Hard Disk Types

| Type | Cylinders | Heads | Sectors | Approx Size |
|------|-----------|-------|---------|-------------|
| 0    | —         | —     | —       | Not installed |
| 1    | 306       | 4     | 17      | 10 MB       |
| 2    | 615       | 4     | 17      | 20 MB       |
| 3    | 615       | 6     | 17      | 30 MB       |
| 4    | 940       | 8     | 17      | 62 MB       |
| 5    | 940       | 6     | 17      | 46 MB       |
| 6    | 615       | 4     | 17      | 20 MB       |
| 7    | 462       | 8     | 17      | 30 MB       |
| 8    | 733       | 5     | 17      | 30 MB       |
| 9    | 900       | 15    | 17      | 112 MB      |
| 10   | 820       | 3     | 17      | 20 MB       |
| 11   | 855       | 5     | 17      | 35 MB       |
| 12   | 855       | 7     | 17      | 49 MB       |
| 13   | 306       | 8     | 17      | 20 MB       |
| 14   | 733       | 7     | 17      | 42 MB       |
| 15   | Extended (uses CMOS 0x19/0x1A) | | | |

### Language Codes (DIP Switches)

| Code | Language    |
|------|-------------|
| 0    | Diagnostic  |
| 1    | Italian     |
| 2    | Swedish     |
| 3    | Danish      |
| 4    | Spanish     |
| 5    | French      |
| 6    | German      |
| 7    | English     |

### Display Types

| Code | Display                      |
|------|------------------------------|
| 0    | EGA (built-in Paradise PEGA) |
| 2    | CGA                          |
| 3    | MDA/Hercules                 |

### PC1640 IRQ Assignments

| IRQ | Function                                 |
|-----|------------------------------------------|
| 0   | Timer (8253 channel 0, 18.2 Hz)          |
| 1   | Keyboard + **RTC alarm** (Amstrad!)      |
| 2   | Reserved                                 |
| 3   | COM2                                     |
| 4   | COM1                                     |
| 5   | LPT2                                     |
| 6   | Floppy disk controller                   |
| 7   | LPT1                                     |

## Notes

- **Must run as root** for direct port I/O access
- The PC1640 has an 8086 CPU at 8MHz — the binary is compiled for 8086
- Battery is in the monitor base (4× AA) — check with `nvr battery`
- If the battery is dead, all CMOS settings are lost on power-off
- The `save`/`load` commands can backup and restore the full CMOS state
- The `compare` command is useful for diagnosing unexpected changes
- The `probe` command reads but does not modify hardware state
- `factory-reset` restores: 720KB floppy A, no HD, EGA video, 640KB base, 24h BCD mode
- **Never** write to port `0x66` — it triggers a soft reset (use `soft-reset` command intentionally)
- Alarm wildcards: value ≥ `0xC0` means "don't care" (e.g. `-1:-1:00` fires every minute)
- The Amstrad mouse is proprietary — NOT PS/2 or serial

## BIOS ROM Analysis

The NVR protocol was determined by disassembling the interleaved BIOS ROMs:

- `40044.v3` — high byte ROM (8192 bytes)
- `40043.v3` — low byte ROM (8192 bytes)
- `40100`    — Paradise PEGA video BIOS (32768 bytes)

Key BIOS routines identified:

| ROM Offset | Function                                   |
|------------|-------------------------------------------|
| `0x00C9`   | POST entry (CLI, clear ports)              |
| `0x00E0`   | `OUT 0x66` — soft reset during warm boot   |
| `0x03F3`   | System config verify via port `0x64`/`0x60`|
| `0x0465`   | NVR byte read via port `0x65`/`0x62`       |
| `0x0793`   | NVR address select via port `0x65`         |
| `0x089E`   | `INT 19h` — bootstrap loader              |

The BIOS contains POST messages in **7 languages**: Danish, French,
Italian, Swedish, Spanish, German, and English.

## License

Public domain. Written for hardware preservation.
