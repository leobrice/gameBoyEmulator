/* cart.h -- cartridge loading and bank switching */
#ifndef GB_CART_H
#define GB_CART_H

#include "gb.h"

int  cart_load(struct cart *c, const char *path);
int  cart_load_mem(struct cart *c, const u8 *data, size_t size);
void cart_free(struct cart *c);
void cart_save_ram(struct cart *c);

u8   cart_rb(struct cart *c, u16 addr);      /* 0000-7FFF */
void cart_wb(struct cart *c, u16 addr, u8 v);
u8   cart_ram_rb(struct cart *c, u16 addr);  /* A000-BFFF */
void cart_ram_wb(struct cart *c, u16 addr, u8 v);

const char *cart_mbc_name(int mbc);

#endif
