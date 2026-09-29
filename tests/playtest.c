/* playtest.c -- plays the built-in snake game with nobody at the keyboard
 *
 * Reads the snake's head and the food straight out of work ram, steers at the
 * food, then drives the snake into a wall and restarts it.  If this passes,
 * the cpu, the ppu, oam dma and the joypad all agree with each other, which is
 * most of the emulator in one test.
 */
#include "../src/gb.h"
#include "../src/cart.h"
#include "../src/builtin.h"
#include "../src/joypad.h"

#include <stdio.h>

/* work ram offsets, matching the defines in tools/mkrom.c */
#define HEAD_COL   0x00
#define HEAD_ROW   0x01
#define DIR        0x02
#define DEAD       0x05
#define SCORE_ONES 0x09
#define FOOD_COL   0x0C
#define FOOD_ROW   0x0D

#define MAP(r, c)  gb.ppu.vram[0x1800 + (r) * 32 + (c)]
#define TILE_BLANK 0
#define TILE_DIGIT 2
#define TILE_G     17
#define TILE_FOOD  23

#define WANTED 5

static struct gb gb;

/* can the head move onto this cell without dying?  the map is the game state,
   so the same tiles the rom tests are the ones we look at */
static int walkable(int col, int row)
{
	u8 tile;

	if (col < 1 || col > 18 || row < 2 || row > 16)
		return 0;
	tile = MAP(row, col);
	return tile == TILE_BLANK || tile == TILE_FOOD;
}

/* 0 right, 1 left, 2 up, 3 down */
static int leads_somewhere(u8 dir)
{
	int col = gb.wram[HEAD_COL];
	int row = gb.wram[HEAD_ROW];

	switch (dir) {
	case 0:  col++; break;
	case 1:  col--; break;
	case 2:  row--; break;
	default: row++; break;
	}
	return walkable(col, row);
}

/* head for the food, but never into a wall or our own tail, and never a
   straight reversal because the game refuses those anyway */
static u8 steer(void)
{
	u8  hc = gb.wram[HEAD_COL], hr = gb.wram[HEAD_ROW];
	u8  fc = gb.wram[FOOD_COL], fr = gb.wram[FOOD_ROW];
	u8  dir = gb.wram[DIR];
	u8  cand[4];
	int n = 0, i, d;

	if (fc > hc)      cand[n++] = 0;
	else if (fc < hc) cand[n++] = 1;
	if (fr > hr)      cand[n++] = 3;
	else if (fr < hr) cand[n++] = 2;

	for (d = 0; d < 4; d++) {          /* then anything else that is safe */
		for (i = 0; i < n; i++)
			if (cand[i] == d)
				break;
		if (i == n)
			cand[n++] = (u8)d;
	}

	for (i = 0; i < n; i++)
		if (cand[i] != (dir ^ 1) && leads_somewhere(cand[i]))
			return cand[i];
	return dir;
}

static u8 button(u8 dir)
{
	switch (dir) {
	case 0:  return BTN_RIGHT;
	case 1:  return BTN_LEFT;
	case 2:  return BTN_UP;
	default: return BTN_DOWN;
	}
}

static void frame(u8 keys)
{
	joypad_set(&gb, keys);
	gb_run_frame(&gb);
}

int main(void)
{
	int frames, eaten = 0;
	u8  last = 0;

	gb_init(&gb);
	gb.serial_stdout = false;

	if (cart_load_mem(&gb.cart, builtin_rom, builtin_rom_size) < 0) {
		printf("FAIL: the built-in rom did not load\n");
		return 1;
	}
	gb_reset(&gb);

	/* let it boot, then play */
	for (frames = 0; frames < 6000 && eaten < WANTED; frames++) {
		frame(frames < 30 ? 0 : button(steer()));

		if (gb.cpu.stopped) {
			printf("FAIL: the cpu stopped at frame %d\n", frames);
			return 1;
		}
		if (gb.wram[DEAD]) {
			printf("FAIL: died after eating %d, at %u,%u\n",
			       eaten, gb.wram[HEAD_COL], gb.wram[HEAD_ROW]);
			return 1;
		}
		if (gb.wram[SCORE_ONES] != last) {
			last = gb.wram[SCORE_ONES];
			eaten++;
		}
	}

	if (eaten < WANTED) {
		printf("FAIL: ate %d of %d in %d frames\n", eaten, WANTED, frames);
		return 1;
	}
	printf("ok   ate %d food in %d frames\n", eaten, frames);

	/* the score has to reach the screen, one frame behind the catch */
	frame(0);
	if (MAP(0, 10) != TILE_DIGIT + gb.wram[SCORE_ONES]) {
		printf("FAIL: score tile is %u, expected %u\n",
		       MAP(0, 10), TILE_DIGIT + gb.wram[SCORE_ONES]);
		return 1;
	}
	printf("ok   score %u drawn to the background map\n", gb.wram[SCORE_ONES]);

	/* the head is the only sprite, and it gets there by dma */
	if (gb.ppu.oam[0] != gb.wram[HEAD_ROW] * 8 + 16 ||
	    gb.ppu.oam[1] != gb.wram[HEAD_COL] * 8 + 8) {
		printf("FAIL: head sprite at %u,%u, expected %u,%u\n",
		       gb.ppu.oam[1], gb.ppu.oam[0],
		       gb.wram[HEAD_COL] * 8 + 8, gb.wram[HEAD_ROW] * 8 + 16);
		return 1;
	}
	printf("ok   oam dma put the head sprite where the snake is\n");

	/* now crash on purpose: hold one direction until a wall turns up */
	for (frames = 0; frames < 400 && !gb.wram[DEAD]; frames++)
		frame(BTN_UP);

	if (!gb.wram[DEAD]) {
		printf("FAIL: the snake never hit anything\n");
		return 1;
	}
	if (MAP(9, 5) != TILE_G) {
		printf("FAIL: game over was not drawn (tile %u)\n", MAP(9, 5));
		return 1;
	}
	printf("ok   collision killed it and drew game over\n");

	/* start puts it back */
	for (frames = 0; frames < 20; frames++)
		frame(BTN_START);

	if (gb.wram[DEAD] || gb.wram[SCORE_ONES] != 0 || MAP(9, 5) != TILE_BLANK) {
		printf("FAIL: start did not restart the game\n");
		return 1;
	}
	printf("ok   start began a fresh game\n");

	cart_free(&gb.cart);
	return 0;
}
