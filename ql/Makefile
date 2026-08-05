NAME    := game
QCC     := qcc
AS68    := as68
PYTHON  := python3
# Path to your own Q-emuLator.app install (or equivalent) -- override
# per-machine with `make run QEMULATOR_APP=/path/to/QemuLator.app` or
# by exporting the same variable in your shell; this default is just
# this project's own dev machine.
QEMULATOR_APP ?= /Users/jcwiart/Applications/CrossOver/QemuLator 4.app

C_SRCS   := $(wildcard src/*.c)
ASM_SRCS := $(wildcard asm/*.s)
HEADERS  := $(wildcard src/*.h)
OBJS     := $(patsubst src/%.c,build/%.o,$(C_SRCS)) $(patsubst asm/%.s,build/%.o,$(ASM_SRCS))

all: build/$(NAME).qlpak

build:
	mkdir -p build

# Blanket header dependency: qcc doesn't emit .d files, and this project
# is small enough that any .c depending on any .h (directly or via
# nested #include) just rebuilding on any header change is cheap and
# safe -- the alternative (no dependency at all) silently links stale
# .o files whenever only a header changes, which is exactly what
# happened to render.o after a JOKER_SPRITE_INDEX edit in render.h: the
# .o didn't rebuild, so game_bin kept blitting the old sprite index.
build/%.o: src/%.c $(HEADERS) | build
	$(QCC) -Isrc -c $< -o $@

build/%.o: asm/%.s | build
	$(AS68) $< -o $@

build/$(NAME)_bin: $(OBJS)
	$(QCC) $(OBJS) -o $@

build/$(NAME).qlpak: build/$(NAME)_bin boot $(NAME).QCF tools/mkqlpak.py
	cp boot build/boot
	cp $(NAME).QCF build/$(NAME).QCF
	cd build && $(PYTHON) ../tools/mkqlpak.py $(NAME)

# Bootable microdrive image for real QL hardware / the MiSTer QL core --
# see tools/mkmdv.py for format notes. Not part of `all`: only needed
# when actually deploying outside QemuLator.
build/$(NAME).mdv: build/$(NAME)_bin boot tools/mkmdv.py
	cp boot build/boot
	cd build && $(PYTHON) ../tools/mkmdv.py $(NAME)

mdv: build/$(NAME).mdv

run: build/$(NAME).qlpak
	open -a "$(QEMULATOR_APP)" "$(CURDIR)/build/$(NAME).qlpak"

clean:
	rm -rf build

.PHONY: all run clean mdv
