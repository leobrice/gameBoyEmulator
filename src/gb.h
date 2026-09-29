/* gb.h -- the machine: every piece of emulator state lives here */
#ifndef GB_MACHINE_H
#define GB_MACHINE_H

#include "common.h"

/* IF / IE bits */
enum {
	INT_VBLANK = 0x01,
	INT_LCD    = 0x02,
	INT_TIMER  = 0x04,
	INT_SERIAL = 0x08,
	INT_JOYPAD = 0x10
};

/* gb.buttons: 1 = pressed */
enum {
	BTN_A      = 0x01,
	BTN_B      = 0x02,
	BTN_SELECT = 0x04,
	BTN_START  = 0x08,
	BTN_RIGHT  = 0x10,
	BTN_LEFT   = 0x20,
	BTN_UP     = 0x40,
	BTN_DOWN   = 0x80
};

/* memory bank controllers */
enum { MBC_NONE, MBC_1, MBC_2, MBC_3, MBC_5 };

struct cart {
	u8    *rom;
	size_t rom_size;
	u8    *ram;
	size_t ram_size;

	char   title[17];
	int    mbc;
	int    rom_banks;
	int    ram_banks;
	bool   battery;

	/* banking state */
	int    bank1;        /* low rom bank bits   */
	int    bank2;        /* high bits / ram bank */
	int    mode;         /* MBC1 banking mode   */
	bool   ram_enable;
	bool   ram_dirty;

	char  *save_path;
};

struct cpu {
	u8   r[8];           /* B C D E H L (HL) A -- index 6 is the (HL) slot */
	u8   f;
	u16  sp, pc;
	bool ime;            /* interrupt master enable */
	bool ime_pending;    /* EI takes effect after the next instruction */
	bool halted;
	bool stopped;
};

struct ppu {
	u8   vram[0x2000];
	u8   oam[0xA0];

	u8   lcdc, stat, scy, scx, ly, lyc, bgp, obp0, obp1, wy, wx;

	int  dot;            /* dot counter within the current scanline */
	int  wline;          /* window internal line counter */
	bool line_drawn;
	bool stat_line;      /* previous state of the STAT interrupt line */
	bool frame_ready;

	u8   fb[GB_W * GB_H];  /* shade index 0..3, 0 = lightest */
	u8   bgprio[GB_W];     /* bg colour index of the current line */
};

struct timer {
	u16 div;             /* the full 16-bit divider; DIV reads its high byte */
	u8  tima, tma, tac;
};

struct gb {
	struct cpu   cpu;
	struct ppu   ppu;
	struct timer timer;
	struct cart  cart;

	u8  wram[0x2000];
	u8  hram[0x7F];
	u8  io[0x80];        /* shadow for registers we do not emulate (sound) */
	u8  ie, iflag;

	u8  buttons;         /* see BTN_* */
	u8  joyp_sel;        /* bits 4/5 as last written to FF00 */

	u64 cycles;
	bool trace;
	bool serial_stdout;
};

void gb_init(struct gb *gb);
void gb_reset(struct gb *gb);
void gb_tick(struct gb *gb, int cycles);
void gb_run_frame(struct gb *gb);
void gb_irq(struct gb *gb, u8 bit);

#endif /* GB_MACHINE_H */
