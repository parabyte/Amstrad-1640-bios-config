# Amstrad PC1640 NVR Utility - Makefile for ELKS
#
# Build with the ELKS cross-compiler toolchain (ia16-elf-gcc)
# See: https://github.com/jbruchon/elks
#
# Prerequisites:
#   ia16-elf-gcc cross-compiler (from gcc-ia16 project)
#   ELKS libc headers and libraries
#
# Installation of ia16-elf toolchain:
#   On Debian/Ubuntu:
#     sudo add-apt-repository ppa:tkchia/build-ia16
#     sudo apt-get update
#     sudo apt-get install gcc-ia16-elf libia16-elf-dev
#
#   Or build from source:
#     https://github.com/tkchia/gcc-ia16
#

# Cross-compiler prefix
CROSS_COMPILE ?= ia16-elf-

CC      = $(CROSS_COMPILE)gcc
LD      = $(CROSS_COMPILE)ld
OBJDUMP = $(CROSS_COMPILE)objdump
SIZE    = $(CROSS_COMPILE)size
STRIP   = $(CROSS_COMPILE)strip

# ELKS target flags
# -melks        : target ELKS a.out format
# -mtune=i8086  : generate 8086-compatible code (PC1640 has an 8086)
# -mcmodel=small: small memory model (code+data < 64KB)
# -Os           : optimise for size (important for 8086 targets)
CFLAGS  = -melks -mtune=i8086 -mcmodel=small -Os
CFLAGS += -Wall -Wextra -Wno-unused-parameter
CFLAGS += -fno-builtin

# Linker flags for ELKS
LDFLAGS = -melks -mtune=i8086 -mcmodel=small

# Source and target
TARGET  = nvr
SRCS    = nvr.c
OBJS    = $(SRCS:.c=.o)

# Build rules
.PHONY: all clean disasm size help install

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(LDFLAGS) -o $@ $(OBJS)
	@echo "Built: $(TARGET) (ELKS ia16 binary)"
	@$(SIZE) $@ 2>/dev/null || true

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

# Strip debug symbols for smaller binary
strip: $(TARGET)
	$(STRIP) $(TARGET)
	@echo "Stripped: $(TARGET)"
	@$(SIZE) $(TARGET) 2>/dev/null || true

# Disassemble the binary for verification
disasm: $(TARGET)
	$(OBJDUMP) -d -M i8086 $(TARGET) > $(TARGET).dis
	@echo "Disassembly written to $(TARGET).dis"

# Show binary size
size: $(TARGET)
	$(SIZE) $(TARGET)

# Install to ELKS root filesystem (adjust path as needed)
ELKS_ROOT ?= /tmp/elks-root
install: $(TARGET)
	mkdir -p $(ELKS_ROOT)/usr/bin
	cp $(TARGET) $(ELKS_ROOT)/usr/bin/
	@echo "Installed to $(ELKS_ROOT)/usr/bin/$(TARGET)"

# Clean build artifacts
clean:
	rm -f $(TARGET) $(OBJS) $(TARGET).dis

# Native build for testing on Linux x86 (NOT for real hardware)
# This builds a native Linux binary for testing the UI/logic only.
# Port I/O will require root and ioperm()/iopl().
.PHONY: native
native:
	gcc -O2 -Wall -Wextra -o nvr-native nvr.c
	@echo "Built: nvr-native (Linux x86 native - for testing only)"

help:
	@echo "Amstrad PC1640 NVR Utility - Build Targets"
	@echo ""
	@echo "  make              Build for ELKS (requires ia16-elf-gcc)"
	@echo "  make strip        Build and strip symbols"
	@echo "  make disasm       Generate disassembly listing"
	@echo "  make size         Show binary size"
	@echo "  make install      Install to ELKS root filesystem"
	@echo "  make native       Build native Linux binary (testing only)"
	@echo "  make clean        Remove build artifacts"
	@echo ""
	@echo "Variables:"
	@echo "  CROSS_COMPILE     Cross-compiler prefix (default: ia16-elf-)"
	@echo "  ELKS_ROOT         ELKS rootfs path (default: /tmp/elks-root)"
