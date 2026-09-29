/* main.c -- argument handling and the outer loop */
#include "gb.h"
#include "builtin.h"
#include "cart.h"
#include "cpu.h"
#include "display.h"
#include "joypad.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void usage(const char *argv0)
{
	fprintf(stderr,
		"usage: %s [options] [rom.gb]\n"
		"\n"
		"With no rom, the snake game built into the emulator runs: turn\n"
		"with the d-pad, eat the rings, start or space begins a new game.\n"
		"\n"
		"  -s, --scale N     window scale factor (default 3)\n"
		"  -f, --frames N    stop after N frames\n"
		"  -H, --headless    run without opening a window\n"
		"  -d, --dump FILE   write the final frame as a binary ppm\n"
		"  -t, --trace       log cpu state to stdout every instruction\n"
		"  -q, --quiet       do not echo the serial port\n"
		"  -h, --help        this text\n"
		"\n"
		"keys: arrows = d-pad, z = a, x = b, enter or space = start,\n"
		"      backspace = select, escape = quit\n",
		argv0);
}

static int dump_ppm(const struct gb *gb, const char *path)
{
	static const u8 grey[4] = { 0xE0, 0xA8, 0x50, 0x10 };
	FILE *f = fopen(path, "wb");
	int   i;

	if (!f) {
		fprintf(stderr, "cannot write %s\n", path);
		return -1;
	}

	fprintf(f, "P6\n%d %d\n255\n", GB_W, GB_H);
	for (i = 0; i < GB_W * GB_H; i++) {
		u8 v = grey[gb->ppu.fb[i] & 3];

		fputc(v, f);
		fputc(v, f);
		fputc(v, f);
	}
	fclose(f);
	return 0;
}

int main(int argc, char **argv)
{
	static struct gb gb;               /* 40k of machine: too big for the stack */
	const char *rom_path  = NULL;
	const char *dump_path = NULL;
	int   scale    = 3;
	long  frames   = 0;                /* 0 = run until the user quits */
	bool  headless = false;
	bool  trace    = false;
	bool  quiet    = false;
	bool  windowed;
	long  frame    = 0;
	int   i;

	for (i = 1; i < argc; i++) {
		const char *a = argv[i];

		if (!strcmp(a, "-h") || !strcmp(a, "--help")) {
			usage(argv[0]);
			return 0;
		} else if (!strcmp(a, "-H") || !strcmp(a, "--headless")) {
			headless = true;
		} else if (!strcmp(a, "-t") || !strcmp(a, "--trace")) {
			trace = true;
		} else if (!strcmp(a, "-s") || !strcmp(a, "--scale")) {
			if (++i == argc) { usage(argv[0]); return 1; }
			scale = atoi(argv[i]);
		} else if (!strcmp(a, "-f") || !strcmp(a, "--frames")) {
			if (++i == argc) { usage(argv[0]); return 1; }
			frames = atol(argv[i]);
		} else if (!strcmp(a, "-d") || !strcmp(a, "--dump")) {
			if (++i == argc) { usage(argv[0]); return 1; }
			dump_path = argv[i];
		} else if (!strcmp(a, "-q") || !strcmp(a, "--quiet")) {
			quiet = true;
		} else if (a[0] == '-' && a[1]) {
			fprintf(stderr, "unknown option %s\n", a);
			usage(argv[0]);
			return 1;
		} else {
			rom_path = a;
		}
	}

	gb_init(&gb);
	gb.trace = trace;
	gb.serial_stdout = !quiet;

	if (rom_path) {
		if (cart_load(&gb.cart, rom_path) < 0)
			return 1;
	} else if (cart_load_mem(&gb.cart, builtin_rom, builtin_rom_size) < 0) {
		fprintf(stderr, "the built-in rom is broken, which should not happen\n");
		return 1;
	}

	gb_reset(&gb);

	fprintf(stderr, "%s: \"%s\", %s, %d rom banks, %zu bytes of ram%s\n",
	        rom_path ? rom_path : "built-in",
	        gb.cart.title[0] ? gb.cart.title : "(untitled)",
	        cart_mbc_name(gb.cart.mbc), gb.cart.rom_banks, gb.cart.ram_size,
	        gb.cart.battery ? ", battery" : "");

	windowed = !headless && display_init(gb.cart.title[0] ? gb.cart.title : "gameboy", scale) == 0;
	if (!headless && !windowed)
		fprintf(stderr, "no display available, running headless\n");

	for (;;) {
		u8 buttons = 0;

		if (windowed && !display_poll(&buttons))
			break;
		joypad_set(&gb, buttons);

		gb_run_frame(&gb);
		frame++;

		if (windowed) {
			display_frame(gb.ppu.fb);
			display_sync();
		}

		if (gb.cpu.stopped) {
			fprintf(stderr, "cpu stopped after %ld frames\n", frame);
			break;
		}
		if (frames && frame >= frames)
			break;
		if (!windowed && !frames && frame > 100000) {
			fprintf(stderr, "headless run cut off after %ld frames\n", frame);
			break;
		}
	}

	if (dump_path)
		dump_ppm(&gb, dump_path);

	if (windowed)
		display_quit();
	cart_free(&gb.cart);

	return 0;
}
