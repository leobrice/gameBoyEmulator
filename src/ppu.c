#include "ppu.h"

#include <string.h>

/* LCDC bits */
#define LCDC_ON        0x80
#define LCDC_WIN_MAP   0x40
#define LCDC_WIN_ON    0x20
#define LCDC_TILE_DATA 0x10
#define LCDC_BG_MAP    0x08
#define LCDC_OBJ_TALL  0x04
#define LCDC_OBJ_ON    0x02
#define LCDC_BG_ON     0x01

#define DOTS_PER_LINE  456
#define MODE2_END       80
#define MODE3_END      252

void ppu_reset(struct gb *gb)
{
	struct ppu *p = &gb->ppu;

	memset(p, 0, sizeof(*p));
	p->lcdc = 0x91;
	p->stat = 0x85;
	p->bgp  = 0xFC;
	p->obp0 = 0xFF;
	p->obp1 = 0xFF;
}

/* address of the row of a tile, in vram-relative terms */
static u16 tile_row(struct ppu *p, u8 tile, int row)
{
	u16 base;

	if (p->lcdc & LCDC_TILE_DATA)
		base = (u16)tile * 16;
	else
		base = (u16)(0x1000 + (s8)tile * 16);

	return base + row * 2;
}

static u8 tile_pixel(struct ppu *p, u16 row_addr, int bit)
{
	u8 lo = p->vram[row_addr & 0x1FFF];
	u8 hi = p->vram[(row_addr + 1) & 0x1FFF];

	return (u8)((((hi >> bit) & 1) << 1) | ((lo >> bit) & 1));
}

static u8 shade(u8 palette, u8 index)
{
	return (palette >> (index * 2)) & 3;
}

static void draw_background(struct ppu *p, u8 *line)
{
	u16 map = (p->lcdc & LCDC_BG_MAP) ? 0x1C00 : 0x1800;
	u8  y   = (u8)(p->ly + p->scy);
	int x;

	for (x = 0; x < GB_W; x++) {
		u8  xx   = (u8)(x + p->scx);
		u8  tile = p->vram[map + (y / 8) * 32 + (xx / 8)];
		u8  ci   = tile_pixel(p, tile_row(p, tile, y % 8), 7 - (xx % 8));

		p->bgprio[x] = ci;
		line[x] = shade(p->bgp, ci);
	}
}

static void draw_window(struct ppu *p, u8 *line)
{
	u16 map = (p->lcdc & LCDC_WIN_MAP) ? 0x1C00 : 0x1800;
	int wx  = (int)p->wx - 7;
	u8  y   = (u8)p->wline;
	int x;

	if (p->ly < p->wy || wx >= GB_W)
		return;

	for (x = wx < 0 ? 0 : wx; x < GB_W; x++) {
		int xx   = x - wx;
		u8  tile = p->vram[map + (y / 8) * 32 + ((xx / 8) & 31)];
		u8  ci   = tile_pixel(p, tile_row(p, tile, y % 8), 7 - (xx % 8));

		p->bgprio[x] = ci;
		line[x] = shade(p->bgp, ci);
	}
	p->wline++;
}

static void draw_sprites(struct ppu *p, u8 *line)
{
	int height = (p->lcdc & LCDC_OBJ_TALL) ? 16 : 8;
	int found[10];
	int count = 0;
	int i, n;

	/* the hardware takes the first ten sprites on this line, in oam order */
	for (i = 0; i < 40 && count < 10; i++) {
		int sy = p->oam[i * 4] - 16;

		if (p->ly >= sy && p->ly < sy + height)
			found[count++] = i;
	}

	/* on dmg the smaller x wins, ties going to the earlier oam entry: sort so
	   that we can paint back to front and let the winner land on top */
	for (i = 1; i < count; i++) {
		int cur = found[i];
		int x   = p->oam[cur * 4 + 1];

		for (n = i - 1; n >= 0 && p->oam[found[n] * 4 + 1] > x; n--)
			found[n + 1] = found[n];
		found[n + 1] = cur;
	}

	for (n = count - 1; n >= 0; n--) {
		const u8 *o    = &p->oam[found[n] * 4];
		int       sy   = o[0] - 16;
		int       sx   = o[1] - 8;
		u8        tile = o[2];
		u8        attr = o[3];
		int       row  = p->ly - sy;
		u16       addr;
		int       x;

		if (height == 16)
			tile &= 0xFE;
		if (attr & 0x40)                       /* vertical flip */
			row = height - 1 - row;

		addr = (u16)tile * 16 + row * 2;

		for (x = 0; x < 8; x++) {
			int px = sx + x;
			u8  ci;

			if (px < 0 || px >= GB_W)
				continue;

			ci = tile_pixel(p, addr, (attr & 0x20) ? x : 7 - x);
			if (ci == 0)                        /* colour 0 is transparent */
				continue;
			if ((attr & 0x80) && p->bgprio[px]) /* background has priority */
				continue;

			line[px] = shade((attr & 0x10) ? p->obp1 : p->obp0, ci);
		}
	}
}

static void draw_line(struct gb *gb)
{
	struct ppu *p    = &gb->ppu;
	u8         *line = &p->fb[p->ly * GB_W];

	if (p->lcdc & LCDC_BG_ON) {
		draw_background(p, line);
	} else {
		memset(line, 0, GB_W);
		memset(p->bgprio, 0, GB_W);
	}

	if (p->lcdc & LCDC_WIN_ON)
		draw_window(p, line);

	if (p->lcdc & LCDC_OBJ_ON)
		draw_sprites(p, line);
}

static void update_stat(struct gb *gb)
{
	struct ppu *p    = &gb->ppu;
	u8          mode = p->stat & 3;
	bool        lyc  = p->ly == p->lyc;
	bool        line;

	if (lyc)
		p->stat |= 0x04;
	else
		p->stat &= (u8)~0x04;

	line = ((p->stat & 0x40) && lyc)
	    || ((p->stat & 0x20) && mode == 2)
	    || ((p->stat & 0x10) && mode == 1)
	    || ((p->stat & 0x08) && mode == 0);

	/* the sources are wired together, so only a rising edge fires */
	if (line && !p->stat_line)
		gb_irq(gb, INT_LCD);
	p->stat_line = line;
}

static void next_line(struct gb *gb)
{
	struct ppu *p = &gb->ppu;

	p->ly++;
	p->line_drawn = false;

	if (p->ly == 144) {
		p->frame_ready = true;
		gb_irq(gb, INT_VBLANK);
	} else if (p->ly > 153) {
		p->ly = 0;
		p->wline = 0;
	}
}

void ppu_step(struct gb *gb, int cycles)
{
	struct ppu *p = &gb->ppu;
	u8 mode;

	if (!(p->lcdc & LCDC_ON)) {
		p->ly    = 0;
		p->dot   = 0;
		p->wline = 0;
		p->stat &= (u8)~0x03;
		p->stat_line  = false;
		p->line_drawn = false;
		return;
	}

	p->dot += cycles;
	while (p->dot >= DOTS_PER_LINE) {
		p->dot -= DOTS_PER_LINE;
		next_line(gb);
	}

	if (p->ly >= 144)          mode = 1;
	else if (p->dot < MODE2_END) mode = 2;
	else if (p->dot < MODE3_END) mode = 3;
	else                         mode = 0;

	/* we draw a whole scanline the moment the real hardware would start it */
	if (mode == 3 && !p->line_drawn) {
		draw_line(gb);
		p->line_drawn = true;
	}

	p->stat = (u8)((p->stat & ~0x03) | mode);
	update_stat(gb);
}
