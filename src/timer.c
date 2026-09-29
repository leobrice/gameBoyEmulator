#include "timer.h"

/* TIMA counts a falling edge of one bit of the divider, chosen by TAC */
static const u16 tac_bit[4] = { 1 << 9, 1 << 3, 1 << 5, 1 << 7 };

static void set_div(struct gb *gb, u16 next)
{
	u16 prev = gb->timer.div;

	gb->timer.div = next;

	if (gb->timer.tac & 0x04) {
		u16 bit = tac_bit[gb->timer.tac & 3];

		if ((prev & bit) && !(next & bit)) {
			if (++gb->timer.tima == 0) {
				gb->timer.tima = gb->timer.tma;
				gb_irq(gb, INT_TIMER);
			}
		}
	}
}

void timer_step(struct gb *gb, int cycles)
{
	/* step in fours: the fastest tap is bit 3, so no edge can be missed */
	while (cycles >= 4) {
		set_div(gb, gb->timer.div + 4);
		cycles -= 4;
	}
	if (cycles > 0)
		set_div(gb, gb->timer.div + cycles);
}

void timer_write_div(struct gb *gb)
{
	set_div(gb, 0);   /* any write resets the whole divider, edge included */
}

void timer_write_tac(struct gb *gb, u8 v)
{
	gb->timer.tac = v & 0x07;
}
