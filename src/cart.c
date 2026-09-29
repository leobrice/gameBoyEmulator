#include "cart.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const struct {
	u8   code;
	int  mbc;
	bool battery;
} cart_types[] = {
	{ 0x00, MBC_NONE, false }, { 0x01, MBC_1, false }, { 0x02, MBC_1, false },
	{ 0x03, MBC_1, true  },    { 0x05, MBC_2, false }, { 0x06, MBC_2, true  },
	{ 0x08, MBC_NONE, false }, { 0x09, MBC_NONE, true },
	{ 0x0F, MBC_3, true  },    { 0x10, MBC_3, true  }, { 0x11, MBC_3, false },
	{ 0x12, MBC_3, false },    { 0x13, MBC_3, true  },
	{ 0x19, MBC_5, false },    { 0x1A, MBC_5, false }, { 0x1B, MBC_5, true  },
	{ 0x1C, MBC_5, false },    { 0x1D, MBC_5, false }, { 0x1E, MBC_5, true  },
};

static const size_t ram_sizes[] = { 0, 2048, 8192, 32768, 131072, 65536 };

const char *cart_mbc_name(int mbc)
{
	switch (mbc) {
	case MBC_NONE: return "ROM only";
	case MBC_1:    return "MBC1";
	case MBC_2:    return "MBC2";
	case MBC_3:    return "MBC3";
	case MBC_5:    return "MBC5";
	}
	return "?";
}

static void load_ram(struct cart *c)
{
	FILE *f;

	if (!c->battery || !c->ram || !c->save_path)
		return;

	f = fopen(c->save_path, "rb");
	if (!f)
		return;
	if (fread(c->ram, 1, c->ram_size, f) != c->ram_size)
		fprintf(stderr, "warning: short save file %s\n", c->save_path);
	fclose(f);
}

void cart_save_ram(struct cart *c)
{
	FILE *f;

	if (!c->battery || !c->ram || !c->ram_dirty || !c->save_path)
		return;

	f = fopen(c->save_path, "wb");
	if (!f) {
		fprintf(stderr, "warning: cannot write %s\n", c->save_path);
		return;
	}
	fwrite(c->ram, 1, c->ram_size, f);
	fclose(f);
	c->ram_dirty = false;
}

/* everything past getting the bytes in memory is the same either way */
static int cart_parse(struct cart *c, const char *path)
{
	u8     type;
	size_t i;

	memcpy(c->title, c->rom + 0x134, 16);
	c->title[16] = '\0';
	for (i = 0; i < 16; i++)
		if ((u8)c->title[i] < 0x20 || (u8)c->title[i] > 0x7E)
			c->title[i] = '\0';

	type = c->rom[0x147];
	c->mbc = -1;
	for (i = 0; i < sizeof(cart_types) / sizeof(cart_types[0]); i++) {
		if (cart_types[i].code == type) {
			c->mbc     = cart_types[i].mbc;
			c->battery = cart_types[i].battery;
			break;
		}
	}
	if (c->mbc < 0) {
		fprintf(stderr, "unsupported cartridge type 0x%02X, assuming MBC1\n", type);
		c->mbc = MBC_1;
	}

	c->rom_banks = 2 << c->rom[0x148];
	if ((size_t)c->rom_banks * 0x4000 > c->rom_size)
		c->rom_banks = (int)(c->rom_size / 0x4000);
	if (c->rom_banks < 2)
		c->rom_banks = 2;

	if (c->mbc == MBC_2) {
		c->ram_size = 512;            /* 512 x 4 bits, on the mapper itself */
	} else {
		u8 code = c->rom[0x149];
		c->ram_size = code < sizeof(ram_sizes) / sizeof(ram_sizes[0]) ? ram_sizes[code] : 0;
	}
	c->ram_banks = c->ram_size ? (int)(c->ram_size / 0x2000) : 0;
	if (c->ram_banks < 1)
		c->ram_banks = 1;

	if (c->ram_size) {
		c->ram = calloc(1, c->ram_size);
		if (!c->ram) {
			fprintf(stderr, "out of memory\n");
			return -1;
		}
	}

	c->bank1 = 1;
	c->bank2 = 0;
	c->mode  = 0;

	if (c->battery && path) {
		size_t n = strlen(path);

		c->save_path = malloc(n + 5);
		if (c->save_path) {
			memcpy(c->save_path, path, n);
			memcpy(c->save_path + n, ".sav", 5);
		}
		load_ram(c);
	}

	return 0;
}

int cart_load(struct cart *c, const char *path)
{
	FILE *f;
	long  size;

	memset(c, 0, sizeof(*c));

	f = fopen(path, "rb");
	if (!f) {
		fprintf(stderr, "cannot open %s\n", path);
		return -1;
	}
	fseek(f, 0, SEEK_END);
	size = ftell(f);
	fseek(f, 0, SEEK_SET);

	if (size < 0x150) {
		fprintf(stderr, "%s: too small to be a rom (%ld bytes)\n", path, size);
		fclose(f);
		return -1;
	}

	c->rom_size = (size_t)size;
	c->rom = malloc(c->rom_size);
	if (!c->rom || fread(c->rom, 1, c->rom_size, f) != c->rom_size) {
		fprintf(stderr, "%s: read failed\n", path);
		fclose(f);
		free(c->rom);
		c->rom = NULL;
		return -1;
	}
	fclose(f);

	return cart_parse(c, path);
}

/* the built-in game arrives this way: copied so that cart_free stays simple */
int cart_load_mem(struct cart *c, const u8 *data, size_t size)
{
	memset(c, 0, sizeof(*c));

	if (size < 0x150)
		return -1;

	c->rom_size = size;
	c->rom = malloc(size);
	if (!c->rom)
		return -1;
	memcpy(c->rom, data, size);

	return cart_parse(c, NULL);
}

void cart_free(struct cart *c)
{
	cart_save_ram(c);
	free(c->rom);
	free(c->ram);
	free(c->save_path);
	memset(c, 0, sizeof(*c));
}

/* which rom bank is mapped at 4000-7FFF */
static int hi_bank(struct cart *c)
{
	int bank;

	switch (c->mbc) {
	case MBC_NONE: bank = 1; break;
	case MBC_1:    bank = (c->bank2 << 5) | c->bank1; break;
	case MBC_2:    bank = c->bank1; break;
	case MBC_3:    bank = c->bank1; break;
	case MBC_5:    bank = c->bank1; break;
	default:       bank = 1; break;
	}
	if (bank == 0 && c->mbc != MBC_5)
		bank = 1;
	return bank & (c->rom_banks - 1);
}

/* which rom bank is mapped at 0000-3FFF (MBC1 mode 1 can remap it) */
static int lo_bank(struct cart *c)
{
	if (c->mbc == MBC_1 && c->mode)
		return (c->bank2 << 5) & (c->rom_banks - 1);
	return 0;
}

u8 cart_rb(struct cart *c, u16 addr)
{
	size_t off;

	if (addr < 0x4000)
		off = (size_t)lo_bank(c) * 0x4000 + addr;
	else
		off = (size_t)hi_bank(c) * 0x4000 + (addr - 0x4000);

	return off < c->rom_size ? c->rom[off] : 0xFF;
}

void cart_wb(struct cart *c, u16 addr, u8 v)
{
	switch (c->mbc) {
	case MBC_NONE:
		break;

	case MBC_1:
		if (addr < 0x2000)      c->ram_enable = (v & 0x0F) == 0x0A;
		else if (addr < 0x4000) c->bank1 = (v & 0x1F) ? (v & 0x1F) : 1;
		else if (addr < 0x6000) c->bank2 = v & 0x03;
		else                    c->mode  = v & 0x01;
		break;

	case MBC_2:
		if (addr < 0x4000) {
			if (addr & 0x0100)
				c->bank1 = (v & 0x0F) ? (v & 0x0F) : 1;
			else
				c->ram_enable = (v & 0x0F) == 0x0A;
		}
		break;

	case MBC_3:
		/* writes above 0x6000 latch the real time clock, which we do not keep */
		if (addr < 0x2000)      c->ram_enable = (v & 0x0F) == 0x0A;
		else if (addr < 0x4000) c->bank1 = (v & 0x7F) ? (v & 0x7F) : 1;
		else if (addr < 0x6000) c->bank2 = v;           /* >= 0x08 selects RTC */
		break;

	case MBC_5:
		if (addr < 0x2000)      c->ram_enable = (v & 0x0F) == 0x0A;
		else if (addr < 0x3000) c->bank1 = (c->bank1 & 0x100) | v;
		else if (addr < 0x4000) c->bank1 = (c->bank1 & 0xFF) | ((v & 1) << 8);
		else if (addr < 0x6000) c->bank2 = v & 0x0F;
		break;
	}
}

static long ram_offset(struct cart *c, u16 addr)
{
	int bank = 0;

	if (!c->ram || !c->ram_enable)
		return -1;

	switch (c->mbc) {
	case MBC_1: bank = c->mode ? c->bank2 : 0; break;
	case MBC_2: return (addr - 0xA000) & 0x1FF;
	case MBC_3:
		if (c->bank2 >= 0x08)
			return -1;                            /* an RTC register, not ram */
		bank = c->bank2;
		break;
	case MBC_5: bank = c->bank2; break;
	default:    bank = 0; break;
	}

	if (bank >= c->ram_banks)
		bank &= c->ram_banks - 1;

	return (long)bank * 0x2000 + (addr - 0xA000);
}

u8 cart_ram_rb(struct cart *c, u16 addr)
{
	long off = ram_offset(c, addr);

	if (off < 0 || (size_t)off >= c->ram_size)
		return 0xFF;
	if (c->mbc == MBC_2)
		return c->ram[off] | 0xF0;
	return c->ram[off];
}

void cart_ram_wb(struct cart *c, u16 addr, u8 v)
{
	long off = ram_offset(c, addr);

	if (off < 0 || (size_t)off >= c->ram_size)
		return;
	c->ram[off] = (c->mbc == MBC_2) ? (v & 0x0F) : v;
	c->ram_dirty = true;
}
