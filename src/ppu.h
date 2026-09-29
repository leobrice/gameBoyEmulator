/* ppu.h -- the LCD controller */
#ifndef GB_PPU_H
#define GB_PPU_H

#include "gb.h"

void ppu_reset(struct gb *gb);
void ppu_step(struct gb *gb, int cycles);

#endif
