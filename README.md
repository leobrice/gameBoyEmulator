# gameboy

A small Game Boy (DMG) emulator in C99, in the spirit of
[Cinoop](https://cturt.github.io/cinoop.html): no dependencies worth the name,
one file per chip, and short enough to read in an afternoon.

It ships with a game inside it, so there is nothing to download and nothing to
find before you can play:

```sh
make && ./gameboy
```

Steer the face around the field with the d-pad and run into the diamond. Start
resets the score. That game is hand assembled by [tools/mkrom.c](tools/mkrom.c)
and linked into the binary, which also makes it the emulator's own test rom.

## Build

```sh
make                 # builds ./gameboy
make BACKEND=null    # builds without X11, headless only
make test            # plays the built-in game automatically and checks the result
```

The only optional dependency is libX11. Without it the build falls back to a
null display and still runs.

## Run

```sh
./gameboy                        # the built-in game
./gameboy --scale 5              # bigger window
./gameboy tetris.gb              # a rom of your own
./gameboy -H -f 60 -d frame.ppm  # headless, dump a frame to look at
```

| option | meaning |
| --- | --- |
| `-s, --scale N` | window scale factor (default 3) |
| `-f, --frames N` | stop after N frames |
| `-H, --headless` | do not open a window |
| `-d, --dump FILE` | write the final frame as a binary PPM |
| `-t, --trace` | log CPU state every instruction |
| `-q, --quiet` | do not echo the serial port |

Keys: arrows are the d-pad, `z` is A, `x` is B, enter is start, backspace is
select, escape quits. Cartridge RAM with a battery is saved next to the ROM as
`<rom>.sav` on exit.

## How it fits together

| file | what lives there |
| --- | --- |
| [src/gb.h](src/gb.h) | every byte of machine state, in one struct |
| [src/cpu.c](src/cpu.c) | the LR35902: decode, ALU, CB prefix, interrupts |
| [src/mmu.c](src/mmu.c) | the memory map and the IO registers |
| [src/ppu.c](src/ppu.c) | scanline rendering, LCD modes, STAT interrupts |
| [src/cart.c](src/cart.c) | ROM loading, MBC1/2/3/5 banking, saves |
| [src/timer.c](src/timer.c) | the divider and TIMA |
| [src/joypad.c](src/joypad.c) | FF00 |
| [src/gb.c](src/gb.c) | the step loop that keeps the chips in sync |
| [src/display_x11.c](src/display_x11.c) | window, palette, keyboard, frame pacing |
| [tools/mkrom.c](tools/mkrom.c) | assembles the built-in game, byte by byte |
| [tests/playtest.c](tests/playtest.c) | plays that game with no human involved |

The CPU is decoded by leaning on the shape of the opcode table rather than by
writing out 500 cases. `0x40`–`0x7F` is one `LD r,r'`, `0x80`–`0xBF` is one ALU
dispatch, and the whole CB page is eight operations crossed with eight
registers. Only the genuinely irregular opcodes get their own case, so
[src/cpu.c](src/cpu.c) stays under 600 lines with the full instruction set in it.

Registers live in `u8 r[8]`, indexed the way the opcodes encode them
(B C D E H L (HL) A), which is what makes that decoding fall out so cleanly.
Slot 6 is the `(HL)` escape hatch and reads or writes memory instead.

## The built-in game

[tools/mkrom.c](tools/mkrom.c) is a 550-line program that emits a 32 KiB
cartridge: `emit()` lays down opcodes, `jr_fwd()`/`patch()` close a forward
branch, and every subroutine is emitted before its callers so each `call` is a
backward reference to an address already known. Tiles are written as string art
and converted to 2bpp planes. There is no assembler in the build on purpose —
in a project this size you should be able to read every byte that runs.

The ROM writes its own tile data, builds a walled field, runs OAM DMA from a
shadow table in work RAM through a stub copied into HRAM, polls the joypad the
way the hardware wants, and keeps a three digit score.

That makes it a thorough test. [tests/playtest.c](tests/playtest.c) links the
emulator as a library, reads the sprite positions out of work RAM, steers the
player at the target, and asserts the score reaches the screen — which exercises
the CPU, the PPU, OAM DMA and the joypad in one go.

## What it does and does not do

Implemented: the full instruction set, interrupts, the divider and timer,
background, window and sprites with DMG priority rules, OAM DMA, the joypad,
the serial port (printed to stdout, which is how test ROMs report), and MBC1,
MBC2, MBC3 and MBC5 banking with battery saves.

Not implemented, in rough order of how much they matter:

- **Sound.** The APU registers are stored and read back, and nothing else.
- **Sub-instruction timing.** Cycles are charged per instruction from a table
  rather than per memory access, and a scanline is drawn in one go when the
  hardware would start it. Games that race the beam mid-scanline will be wrong.
- **The HALT bug**, the TIMA reload delay, and OAM/VRAM access blocking.
- **RTC** on MBC3, and Game Boy Color anything.

## Next steps

The obvious one is [Blargg's test ROMs](https://github.com/retrio/gb-test-roms):
`cpu_instrs` reports through the serial port, which already prints to stdout, so
`./gameboy -H -f 3000 cpu_instrs.gb` is a real conformance check for the work
above. After that, sub-instruction timing and an APU.

For games, [hh.gbdev.io](https://hh.gbdev.io) has hundreds of free homebrew
cartridges that run on a DMG.

## Thanks

To [CTurt's Cinoop](https://cturt.github.io/cinoop.html) for showing that an
emulator can be small, and to the [Pan Docs](https://gbdev.io/pandocs/) for
being the reason any of this is knowable.
