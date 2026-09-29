#include "mmu.h"
#include "cart.h"
#include "joypad.h"
#include "timer.h"

#include <stdio.h>

static void oam_dma(struct gb *gb, u8 v)
{
	u16 src = (u16)v << 8;
	int i;

	/* the real thing takes 160 machine cycles and locks the bus; we copy at once */
	for (i = 0; i < 0xA0; i++)
		gb->ppu.oam[i] = mmu_rb(gb, src + i);
}

static u8 io_rb(struct gb *gb, u16 addr)
{
	switch (addr) {
	case 0xFF00: return joypad_read(gb);
	case 0xFF04: return gb->timer.div >> 8;
	case 0xFF05: return gb->timer.tima;
	case 0xFF06: return gb->timer.tma;
	case 0xFF07: return gb->timer.tac | 0xF8;
	case 0xFF0F: return gb->iflag | 0xE0;
	case 0xFF40: return gb->ppu.lcdc;
	case 0xFF41: return gb->ppu.stat | 0x80;
	case 0xFF42: return gb->ppu.scy;
	case 0xFF43: return gb->ppu.scx;
	case 0xFF44: return gb->ppu.ly;
	case 0xFF45: return gb->ppu.lyc;
	case 0xFF47: return gb->ppu.bgp;
	case 0xFF48: return gb->ppu.obp0;
	case 0xFF49: return gb->ppu.obp1;
	case 0xFF4A: return gb->ppu.wy;
	case 0xFF4B: return gb->ppu.wx;
	default:     return gb->io[addr & 0x7F];
	}
}

static void io_wb(struct gb *gb, u16 addr, u8 v)
{
	switch (addr) {
	case 0xFF00: joypad_write(gb, v); return;
	case 0xFF01: gb->io[0x01] = v; return;
	case 0xFF02:
		/* a transfer request prints the byte: test roms report through here */
		if (v & 0x80) {
			if (gb->serial_stdout) {
				putchar(gb->io[0x01]);
				fflush(stdout);
			}
			gb->io[0x02] = v & 0x7F;
			gb_irq(gb, INT_SERIAL);
		} else {
			gb->io[0x02] = v;
		}
		return;
	case 0xFF04: timer_write_div(gb); return;
	case 0xFF05: gb->timer.tima = v; return;
	case 0xFF06: gb->timer.tma = v; return;
	case 0xFF07: timer_write_tac(gb, v); return;
	case 0xFF0F: gb->iflag = v & 0x1F; return;
	case 0xFF40: gb->ppu.lcdc = v; return;
	case 0xFF41: gb->ppu.stat = (gb->ppu.stat & 0x07) | (v & 0x78); return;
	case 0xFF42: gb->ppu.scy = v; return;
	case 0xFF43: gb->ppu.scx = v; return;
	case 0xFF44: return;                 /* LY is read only */
	case 0xFF45: gb->ppu.lyc = v; return;
	case 0xFF46: oam_dma(gb, v); gb->io[0x46] = v; return;
	case 0xFF47: gb->ppu.bgp = v; return;
	case 0xFF48: gb->ppu.obp0 = v; return;
	case 0xFF49: gb->ppu.obp1 = v; return;
	case 0xFF4A: gb->ppu.wy = v; return;
	case 0xFF4B: gb->ppu.wx = v; return;
	default:     gb->io[addr & 0x7F] = v; return;
	}
}

u8 mmu_rb(struct gb *gb, u16 addr)
{
	switch (addr >> 12) {
	case 0x0: case 0x1: case 0x2: case 0x3:
	case 0x4: case 0x5: case 0x6: case 0x7:
		return cart_rb(&gb->cart, addr);

	case 0x8: case 0x9:
		return gb->ppu.vram[addr - 0x8000];

	case 0xA: case 0xB:
		return cart_ram_rb(&gb->cart, addr);

	case 0xC: case 0xD:
		return gb->wram[addr - 0xC000];

	case 0xE:
		return gb->wram[addr - 0xE000];

	default:
		if (addr < 0xFE00) return gb->wram[addr - 0xE000];   /* echo ram */
		if (addr < 0xFEA0) return gb->ppu.oam[addr - 0xFE00];
		if (addr < 0xFF00) return 0xFF;                      /* unusable */
		if (addr < 0xFF80) return io_rb(gb, addr);
		if (addr < 0xFFFF) return gb->hram[addr - 0xFF80];
		return gb->ie;
	}
}

void mmu_wb(struct gb *gb, u16 addr, u8 v)
{
	switch (addr >> 12) {
	case 0x0: case 0x1: case 0x2: case 0x3:
	case 0x4: case 0x5: case 0x6: case 0x7:
		cart_wb(&gb->cart, addr, v);
		return;

	case 0x8: case 0x9:
		gb->ppu.vram[addr - 0x8000] = v;
		return;

	case 0xA: case 0xB:
		cart_ram_wb(&gb->cart, addr, v);
		return;

	case 0xC: case 0xD:
		gb->wram[addr - 0xC000] = v;
		return;

	case 0xE:
		gb->wram[addr - 0xE000] = v;
		return;

	default:
		if (addr < 0xFE00)      gb->wram[addr - 0xE000] = v;
		else if (addr < 0xFEA0) gb->ppu.oam[addr - 0xFE00] = v;
		else if (addr < 0xFF00) ;                            /* unusable */
		else if (addr < 0xFF80) io_wb(gb, addr, v);
		else if (addr < 0xFFFF) gb->hram[addr - 0xFF80] = v;
		else                    gb->ie = v;
		return;
	}
}

u16 mmu_rw(struct gb *gb, u16 addr)
{
	return (u16)mmu_rb(gb, addr) | ((u16)mmu_rb(gb, addr + 1) << 8);
}

void mmu_ww(struct gb *gb, u16 addr, u16 v)
{
	mmu_wb(gb, addr, v & 0xFF);
	mmu_wb(gb, addr + 1, v >> 8);
}
