#include "gb.h"
#include "cpu.h"
#include "ppu.h"
#include "timer.h"
#include "mmu.h"

#include <string.h>

void gb_init(struct gb *gb)
{
	memset(gb, 0, sizeof(*gb));
	gb->serial_stdout = true;
}

void gb_reset(struct gb *gb)
{
	memset(gb->wram, 0, sizeof(gb->wram));
	memset(gb->hram, 0, sizeof(gb->hram));
	memset(gb->io, 0, sizeof(gb->io));

	cpu_reset(gb);
	ppu_reset(gb);

	gb->timer.div  = 0xABCC;   /* where the boot rom leaves the divider */
	gb->timer.tima = 0x00;
	gb->timer.tma  = 0x00;
	gb->timer.tac  = 0x00;

	gb->ie     = 0x00;
	gb->iflag  = 0x01;
	gb->buttons  = 0;
	gb->joyp_sel = 0x30;
	gb->cycles = 0;
}

void gb_irq(struct gb *gb, u8 bit)
{
	gb->iflag |= bit;
}

void gb_tick(struct gb *gb, int cycles)
{
	gb->cycles += (u64)cycles;
	timer_step(gb, cycles);
	ppu_step(gb, cycles);
}

void gb_run_frame(struct gb *gb)
{
	/* signed on purpose: an unsigned counter wraps when the last instruction
	   costs more than is left, and the loop never ends */
	long budget = FRAME_CYCLES * 2;   /* keeps us honest when the lcd is off */

	gb->ppu.frame_ready = false;

	while (budget > 0) {
		int cycles = cpu_step(gb);

		gb_tick(gb, cycles);
		budget -= cycles;

		if (gb->ppu.frame_ready || gb->cpu.stopped)
			return;
	}
}
