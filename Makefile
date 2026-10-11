# BlakeStone32X - speed test build
# make GENDEV=/path/to/toolchain GAMEDIR=/path/to/BLAKE BSTONE=/path/to/bstone-0.9.0-source
ROOTDIR  ?= $(GENDEV)
GAMEDIR  ?= ../BLAKE
BSTONE   ?= ../bstone
MAPNUM   ?= 1
NAME     ?= BlakeStone32X_B5
TITLE    ?= BLAKE STONE 32X B5
BLOBOFS  = 262144

PREFIX = $(ROOTDIR)/sh-elf/bin/sh-elf-
CC = $(PREFIX)gcc
AS = $(PREFIX)as
OBJC = $(PREFIX)objcopy

INC = -Imars -Isrc
CFLAGS = -c -std=c11 -m2 -mb -mtas -Wall -Wextra -Wno-unused-parameter -D__32X__ -DMARS \
         -fomit-frame-pointer -fno-common -ffunction-sections -fdata-sections $(INC)
LDFLAGS = -T mars/mars-ssf.ld -Wl,-Map=build/output.map -nostdlib -Wl,--gc-sections --specs=nosys.specs
LIBS = -L$(ROOTDIR)/sh-elf/lib/gcc/sh-elf/12.1.0 -L$(ROOTDIR)/sh-elf/sh-elf/lib -lc -lgcc -lnosys

OBJS = build/crt0.o build/main.o build/render.o build/tables.o build/font.o build/marshw.o build/sh2_fixed.o build/r_asm.o

all: build/$(NAME).32x

build:
	mkdir -p build

build/crt0.o: mars/crt0.s mars/src-md/m68k.bin | build
	$(AS) --big -Imars $< -o $@
build/r_asm.o: src/r_asm.s | build
	$(AS) --big $< -o $@
build/sh2_fixed.o: mars/sh2_fixed.s | build
	$(AS) --big $< -o $@
build/marshw.o: mars/marshw.c | build
	$(CC) $(CFLAGS) -O1 $< -o $@
build/%.o: src/%.c src/render.h | build
	$(CC) $(CFLAGS) -O2 $< -o $@

build/code.elf: $(OBJS)
	$(CC) $(LDFLAGS) $(OBJS) $(LIBS) -o $@

build/bsdata.bin: tools/mkblob.py tools/bsdata.py | build
	python3 tools/mkblob.py $(GAMEDIR) $(BSTONE) $(MAPNUM) $@

build/romheaderfix: romheaderfix/romheaderfix.c | build
	gcc -O2 -o $@ $<

build/$(NAME).32x: build/code.elf build/bsdata.bin build/romheaderfix
	$(OBJC) -O binary build/code.elf build/code.bin
	python3 tools/mkrom.py build/code.bin build/bsdata.bin $(BLOBOFS) $@
	build/romheaderfix "SEGA 32X" "$(TITLE)" $@

clean:
	rm -rf build
