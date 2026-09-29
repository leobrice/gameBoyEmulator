/* mmu.h -- the memory map */
#ifndef GB_MMU_H
#define GB_MMU_H

#include "gb.h"

u8   mmu_rb(struct gb *gb, u16 addr);
void mmu_wb(struct gb *gb, u16 addr, u8 v);
u16  mmu_rw(struct gb *gb, u16 addr);
void mmu_ww(struct gb *gb, u16 addr, u16 v);

#endif
