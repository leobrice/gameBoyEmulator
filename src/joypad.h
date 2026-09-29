/* joypad.h -- FF00 */
#ifndef GB_JOYPAD_H
#define GB_JOYPAD_H

#include "gb.h"

u8   joypad_read(struct gb *gb);
void joypad_write(struct gb *gb, u8 v);
void joypad_set(struct gb *gb, u8 buttons);

#endif
