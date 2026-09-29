#include "joypad.h"

u8 joypad_read(struct gb *gb)
{
	u8 sel = gb->joyp_sel & 0x30;
	u8 low = 0x0F;

	/* a selection line is active when its bit reads 0, and a pressed key reads 0 */
	if (!(sel & 0x10))
		low &= ~((gb->buttons >> 4) & 0x0F);   /* right left up down */
	if (!(sel & 0x20))
		low &= ~(gb->buttons & 0x0F);          /* a b select start */

	return 0xC0 | sel | low;
}

void joypad_write(struct gb *gb, u8 v)
{
	gb->joyp_sel = v & 0x30;
}

void joypad_set(struct gb *gb, u8 buttons)
{
	u8 pressed = buttons & ~gb->buttons;

	gb->buttons = buttons;
	if (pressed)
		gb_irq(gb, INT_JOYPAD);
}
