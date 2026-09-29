/* playtest.c -- drives the built-in game the way a player would
 *
 * Reads the sprite positions straight out of work ram, steers the player at
 * the target, and checks that the score on screen goes up.  If this passes,
 * the cpu, the ppu, oam dma, the joypad and the game all agree with each
 * other, which is most of the emulator in one test.
 */
#include "../src/gb.h"
#include "../src/cart.h"
#include "../src/builtin.h"
#include "../src/joypad.h"

#include <stdio.h>
#include <stdlib.h>

#define OAM_SHADOW  0x0000      /* $c000, as an offset into wram */
#define SCORE_ONES  0x0101
#define MAP_ONES    0x180A      /* $980a, as an offset into vram */
#define TILE_DIGIT  2

#define TARGET_SCORE 5
#define MAX_FRAMES   4000

static struct gb gb;

static u8 steer(u8 py, u8 px, u8 ty, u8 tx)
{
	u8 keys = 0;

	if (ty > py + 1)      keys |= BTN_DOWN;
	else if (ty + 1 < py) keys |= BTN_UP;

	if (tx > px + 1)      keys |= BTN_RIGHT;
	else if (tx + 1 < px) keys |= BTN_LEFT;

	return keys;
}

int main(void)
{
	int frame;
	int caught = 0;
	u8  last_score = 0;

	gb_init(&gb);
	gb.serial_stdout = false;

	if (cart_load_mem(&gb.cart, builtin_rom, builtin_rom_size) < 0) {
		printf("FAIL: the built-in rom did not load\n");
		return 1;
	}
	gb_reset(&gb);

	for (frame = 0; frame < MAX_FRAMES; frame++) {
		u8 py = gb.wram[OAM_SHADOW + 0];
		u8 px = gb.wram[OAM_SHADOW + 1];
		u8 ty = gb.wram[OAM_SHADOW + 4];
		u8 tx = gb.wram[OAM_SHADOW + 5];
		u8 score;

		joypad_set(&gb, frame > 60 ? steer(py, px, ty, tx) : 0);
		gb_run_frame(&gb);

		if (gb.cpu.stopped) {
			printf("FAIL: the cpu stopped at frame %d\n", frame);
			return 1;
		}

		score = gb.wram[SCORE_ONES];
		if (score != last_score) {
			caught++;
			last_score = score;
		}
		if (caught >= TARGET_SCORE)
			break;
	}

	if (caught < TARGET_SCORE) {
		printf("FAIL: caught %d of %d targets in %d frames\n",
		       caught, TARGET_SCORE, frame);
		printf("      player at %u,%u  target at %u,%u\n",
		       gb.wram[OAM_SHADOW + 1], gb.wram[OAM_SHADOW + 0],
		       gb.wram[OAM_SHADOW + 5], gb.wram[OAM_SHADOW + 4]);
		return 1;
	}
	printf("ok   caught %d targets in %d frames\n", caught, frame);

	/* the game draws the score at the top of a frame, so the newest catch is
	   still one frame away from the screen */
	joypad_set(&gb, 0);
	gb_run_frame(&gb);

	/* the score has to have reached the screen, not just work ram */
	if (gb.ppu.vram[MAP_ONES] != TILE_DIGIT + gb.wram[SCORE_ONES]) {
		printf("FAIL: score tile is %u, expected %u\n",
		       gb.ppu.vram[MAP_ONES], TILE_DIGIT + gb.wram[SCORE_ONES]);
		return 1;
	}
	printf("ok   score %u drawn to the background map\n", gb.wram[SCORE_ONES]);

	/* the sprites must have been dma'd into real oam */
	if (gb.ppu.oam[0] != gb.wram[OAM_SHADOW + 0] ||
	    gb.ppu.oam[1] != gb.wram[OAM_SHADOW + 1]) {
		printf("FAIL: oam dma did not copy the player sprite\n");
		return 1;
	}
	printf("ok   oam dma copied the sprites\n");

	cart_free(&gb.cart);
	return 0;
}
