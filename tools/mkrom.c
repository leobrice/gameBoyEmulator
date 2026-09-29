/* mkrom.c -- hand assembles the game that ships inside the emulator
 *
 * There is no assembler here on purpose: the whole point of a project this
 * size is that you can read every byte that runs.  emit() lays down opcodes,
 * jr_fwd()/patch() close a forward branch, and subroutines are emitted before
 * their callers so every call is a backward reference to a known address.
 *
 * The game: steer a face around a walled field with the d-pad and run into
 * the diamond.  Start resets the score.
 */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef unsigned char u8;

static u8  rom[0x8000];
static int pos;

/* ---- assembler plumbing ----------------------------------------------- */

static void emit(int n, ...)
{
	va_list ap;
	int i;

	va_start(ap, n);
	for (i = 0; i < n; i++)
		rom[pos++] = (u8)va_arg(ap, int);
	va_end(ap);
}

#define E1(a)       emit(1, (a))
#define E2(a, b)    emit(2, (a), (b))
#define E3(a, b, c) emit(3, (a), (b), (c))

static int here(void) { return pos; }

static int jr_fwd(int op)          /* emits a jr with a hole in it */
{
	rom[pos++] = (u8)op;
	return pos++;
}

static void patch(int hole)
{
	rom[hole] = (u8)(pos - (hole + 1));
}

static void jr_back(int op, int target)
{
	rom[pos]     = (u8)op;
	rom[pos + 1] = (u8)(target - (pos + 2));
	pos += 2;
}

/* the handful of instructions the game leans on, spelled out */
static void ld_a(int n)       { E2(0x3E, n); }
static void ld_hl(int nn)     { E3(0x21, nn & 0xFF, nn >> 8); }
static void ld_de(int nn)     { E3(0x11, nn & 0xFF, nn >> 8); }
static void ld_bc(int nn)     { E3(0x01, nn & 0xFF, nn >> 8); }
static void ld_a_nn(int nn)   { E3(0xFA, nn & 0xFF, nn >> 8); }
static void ld_nn_a(int nn)   { E3(0xEA, nn & 0xFF, nn >> 8); }
static void ldh_a(int n)      { E2(0xF0, n); }    /* ldh a,($ff00+n) */
static void ldh_n(int n)      { E2(0xE0, n); }    /* ldh ($ff00+n),a */
static void call_abs(int a)   { E3(0xCD, a & 0xFF, a >> 8); }
static void ret_(void)        { E1(0xC9); }

/* ---- tile art ---------------------------------------------------------- */

/* ' ' transparent/lightest, '.' shade 1, '+' shade 2, '#' darkest */
static void emit_tile(const char *art)
{
	int row, col;

	for (row = 0; row < 8; row++) {
		u8 lo = 0, hi = 0;

		for (col = 0; col < 8; col++) {
			int shade;

			switch (art[row * 8 + col]) {
			case '.': shade = 1; break;
			case '+': shade = 2; break;
			case '#': shade = 3; break;
			default:  shade = 0; break;
			}
			lo = (u8)(lo | ((shade & 1) << (7 - col)));
			hi = (u8)(hi | (((shade >> 1) & 1) << (7 - col)));
		}
		rom[pos++] = lo;
		rom[pos++] = hi;
	}
}

static const char *const tiles[] = {
	/* 0: empty */
	"        " "        " "        " "        "
	"        " "        " "        " "        ",
	/* 1: wall */
	"++++++++" "+######+" "+######+" "+######+"
	"+######+" "+######+" "+######+" "++++++++",
	/* 2-11: digits */
	"        " "  ###   " " #   #  " " #   #  " " #   #  " " #   #  " "  ###   " "        ",
	"        " "   #    " "  ##    " "   #    " "   #    " "   #    " "  ###   " "        ",
	"        " "  ###   " " #   #  " "     #  " "   ##   " "  #     " " #####  " "        ",
	"        " " #####  " "    #   " "   ##   " "     #  " " #   #  " "  ###   " "        ",
	"        " "    ##  " "   # #  " "  #  #  " " #####  " "     #  " "     #  " "        ",
	"        " " #####  " " #      " " ####   " "     #  " " #   #  " "  ###   " "        ",
	"        " "   ##   " "  #     " " ####   " " #   #  " " #   #  " "  ###   " "        ",
	"        " " #####  " "     #  " "    #   " "   #    " "   #    " "   #    " "        ",
	"        " "  ###   " " #   #  " "  ###   " " #   #  " " #   #  " "  ###   " "        ",
	"        " "  ###   " " #   #  " " #   #  " "  ####  " "     #  " "   ##   " "        ",
	/* 12-16: S C O R E */
	"        " "  ####  " " #      " " #      " "  ###   " "     #  " " ####   " "        ",
	"        " "  ####  " " #      " " #      " " #      " " #      " "  ####  " "        ",
	"        " "  ###   " " #   #  " " #   #  " " #   #  " " #   #  " "  ###   " "        ",
	"        " " ####   " " #   #  " " ####   " " #  #   " " #   #  " " #   #  " "        ",
	"        " " #####  " " #      " " ####   " " #      " " #      " " #####  " "        ",
	/* 17: the player */
	"  ####  " " #....# " "#.#..#.#" "#......#"
	"#.#..#.#" "#..##..#" " #....# " "  ####  ",
	/* 18: the thing you are chasing */
	"   ##   " "  ####  " " ###### " "########"
	"########" " ###### " "  ####  " "   ##   "
};

#define TILE_COUNT   (int)(sizeof(tiles) / sizeof(tiles[0]))
#define TILE_WALL    1
#define TILE_DIGIT   2
#define TILE_LETTER  12
#define TILE_PLAYER  17
#define TILE_TARGET  18

/* ---- addresses --------------------------------------------------------- */

#define MSG_ADDR    0x0500
#define TILE_ADDR   0x0600
#define DMA_ADDR    0x0780

#define OAM_SHADOW  0xC000      /* player at +0, target at +4 */
#define SEED        0xC100
#define SCORE_ONES  0xC101
#define SCORE_TENS  0xC102
#define SCORE_HUNS  0xC103

#define MIN_Y  32               /* sprite coordinates: screen y + 16 */
#define MAX_Y  144
#define MIN_X  16               /* screen x + 8 */
#define MAX_X  152
#define HOME_Y 72
#define HOME_X 80

static const u8 logo[48] = {
	0xCE,0xED,0x66,0x66,0xCC,0x0D,0x00,0x0B,0x03,0x73,0x00,0x83,
	0x00,0x0C,0x00,0x0D,0x00,0x08,0x11,0x1F,0x88,0x89,0x00,0x0E,
	0xDC,0xCC,0x6E,0xE6,0xDD,0xDD,0xD9,0x99,0xBB,0xBB,0x67,0x63,
	0x6E,0x0E,0xEC,0xCC,0xDD,0xDC,0x99,0x9F,0xBB,0xB9,0x33,0x3E
};

static const char msg[] = "catch the diamond -- d-pad to move, start to reset\n";

static int write_file(const char *path)
{
	FILE *f = fopen(path, "wb");

	if (!f) {
		perror(path);
		return -1;
	}
	fwrite(rom, 1, sizeof(rom), f);
	fclose(f);
	return 0;
}

static int write_c_array(const char *path)
{
	FILE *f = fopen(path, "wb");
	size_t i;

	if (!f) {
		perror(path);
		return -1;
	}

	fprintf(f, "/* generated by tools/mkrom.c -- do not edit */\n\n");
	fprintf(f, "const unsigned char builtin_rom[] = {");
	for (i = 0; i < sizeof(rom); i++) {
		if (i % 16 == 0)
			fprintf(f, "\n\t");
		fprintf(f, "0x%02X,", rom[i]);
	}
	fprintf(f, "\n};\n\n");
	fprintf(f, "const unsigned long builtin_rom_size = %luUL;\n",
	        (unsigned long)sizeof(rom));
	fclose(f);
	return 0;
}

int main(int argc, char **argv)
{
	int wait_vblank, memcpy_, memset_, read_input, move_target, score_inc;
	int draw_score, draw_border, draw_label, move_player, check_hit, do_start;
	int entry, loop, l1, l2, h1, h2, i, sum;

	memset(rom, 0x00, sizeof(rom));

	/* ---- header -------------------------------------------------------- */
	pos = 0x0100;
	E1(0x00);                                  /* nop                       */
	E3(0xC3, 0x00, 0x00);                      /* jp entry -- patched below */
	memcpy(rom + 0x104, logo, sizeof(logo));
	memcpy(rom + 0x134, "CATCH", 5);
	rom[0x147] = 0x00;                         /* rom only                  */
	rom[0x148] = 0x00;                         /* 32 KiB                    */
	rom[0x149] = 0x00;                         /* no cartridge ram          */
	rom[0x14A] = 0x01;
	rom[0x14B] = 0x33;

	/* ---- data ---------------------------------------------------------- */
	memcpy(rom + MSG_ADDR, msg, sizeof(msg));

	pos = TILE_ADDR;
	for (i = 0; i < TILE_COUNT; i++)
		emit_tile(tiles[i]);

	pos = DMA_ADDR;                            /* this runs from hram       */
	E2(0xE0, 0x46);                            /* ldh ($46),a  start dma    */
	ld_a(0x28);                                /* ld a,40                   */
	l1 = here();
	E1(0x3D);                                  /* dec a                     */
	jr_back(0x20, l1);                         /* jr nz                     */
	ret_();

	/* ---- subroutines, emitted before anything that calls them ---------- */
	pos = 0x0150;

	wait_vblank = here();
	l1 = here();
	ldh_a(0x44);                               /* ldh a,(ly)                */
	E2(0xFE, 144);
	jr_back(0x28, l1);                         /* still in vblank: wait out */
	l1 = here();
	ldh_a(0x44);
	E2(0xFE, 144);
	jr_back(0x20, l1);                         /* now wait for the next one */
	ret_();

	memcpy_ = here();                          /* hl -> de, bc bytes        */
	l1 = here();
	E1(0x2A);                                  /* ld a,(hl+)                */
	E1(0x12);                                  /* ld (de),a                 */
	E1(0x13);                                  /* inc de                    */
	E1(0x0B);                                  /* dec bc                    */
	E1(0x78);                                  /* ld a,b                    */
	E1(0xB1);                                  /* or c                      */
	jr_back(0x20, l1);
	ret_();

	memset_ = here();                          /* a into bc bytes at hl     */
	E1(0x57);                                  /* ld d,a                    */
	l1 = here();
	E1(0x7A);                                  /* ld a,d                    */
	E1(0x22);                                  /* ld (hl+),a                */
	E1(0x0B);                                  /* dec bc                    */
	E1(0x78);
	E1(0xB1);
	jr_back(0x20, l1);
	ret_();

	read_input = here();                       /* leaves the keys in b      */
	ld_a(0x20);                                /* select the d-pad          */
	ldh_n(0x00);
	ldh_a(0x00);
	ldh_a(0x00);
	E1(0x2F);                                  /* cpl: pressed reads as 0   */
	E2(0xE6, 0x0F);
	E1(0x47);                                  /* ld b,a                    */
	ld_a(0x10);                                /* select the buttons        */
	ldh_n(0x00);
	ldh_a(0x00);
	ldh_a(0x00);
	ldh_a(0x00);
	ldh_a(0x00);
	E1(0x2F);
	E2(0xE6, 0x0F);
	E2(0xCB, 0x37);                            /* swap a                    */
	E1(0xB0);                                  /* or b                      */
	E1(0x47);                                  /* ld b,a                    */
	ld_a(0x30);                                /* deselect                  */
	ldh_n(0x00);
	ret_();

	move_target = here();                      /* div plus a running seed   */
	ldh_a(0x04);
	E1(0x5F);                                  /* ld e,a                    */
	ld_a_nn(SEED);
	E1(0x83);                                  /* add a,e                   */
	E2(0xC6, 0x57);
	ld_nn_a(SEED);
	E2(0xE6, 0x7F);
	E2(0xC6, MIN_X);
	ld_nn_a(OAM_SHADOW + 5);                   /* target x                  */
	ldh_a(0x04);
	E1(0x5F);
	ld_a_nn(SEED);
	E1(0x83);
	E2(0xC6, 0x9D);
	ld_nn_a(SEED);
	E2(0xE6, 0x3F);
	E2(0xC6, 40);
	ld_nn_a(OAM_SHADOW + 4);                   /* target y                  */
	ret_();

	score_inc = here();                        /* three decimal digits      */
	ld_hl(SCORE_ONES);
	l2 = here();
	E1(0x7E);                                  /* ld a,(hl)                 */
	E1(0x3C);                                  /* inc a                     */
	E2(0xFE, 10);
	h1 = jr_fwd(0x20);                         /* jr nz,store               */
	E1(0xAF);                                  /* xor a                     */
	E1(0x22);                                  /* ld (hl+),a  carry over    */
	E1(0x7D);                                  /* ld a,l -- past the end?   */
	E2(0xFE, (SCORE_HUNS + 1) & 0xFF);
	h2 = jr_fwd(0x28);                         /* wrapped: give up and stop */
	jr_back(0x18, l2);
	patch(h1);
	E1(0x77);                                  /* ld (hl),a                 */
	patch(h2);
	ret_();

	draw_score = here();                       /* only safe during vblank   */
	ld_a_nn(SCORE_HUNS);
	E2(0xC6, TILE_DIGIT);
	ld_nn_a(0x9808);
	ld_a_nn(SCORE_TENS);
	E2(0xC6, TILE_DIGIT);
	ld_nn_a(0x9809);
	ld_a_nn(SCORE_ONES);
	E2(0xC6, TILE_DIGIT);
	ld_nn_a(0x980A);
	ret_();

	draw_border = here();
	ld_hl(0x9820);                             /* map row 1                 */
	E2(0x0E, 20);                              /* ld c,20                   */
	ld_a(TILE_WALL);
	l1 = here();
	E1(0x22);
	E1(0x0D);                                  /* dec c                     */
	jr_back(0x20, l1);
	ld_hl(0x9A20);                             /* map row 17                */
	E2(0x0E, 20);
	ld_a(TILE_WALL);
	l1 = here();
	E1(0x22);
	E1(0x0D);
	jr_back(0x20, l1);
	ld_hl(0x9840);                             /* rows 2..16, both edges    */
	E2(0x0E, 15);
	l1 = here();
	ld_a(TILE_WALL);
	E1(0x77);                                  /* ld (hl),a                 */
	E1(0xE5);                                  /* push hl                   */
	ld_de(19);
	E1(0x19);                                  /* add hl,de                 */
	E1(0x77);
	E1(0xE1);                                  /* pop hl                    */
	ld_de(32);
	E1(0x19);
	E1(0x0D);
	jr_back(0x20, l1);
	ret_();

	draw_label = here();                       /* the word SCORE            */
	ld_hl(0x9802);
	ld_a(TILE_LETTER);
	E2(0x0E, 5);
	l1 = here();
	E1(0x22);
	E1(0x3C);                                  /* inc a                     */
	E1(0x0D);
	jr_back(0x20, l1);
	ret_();

	move_player = here();                      /* two pixels a frame        */
	E2(0xCB, 0x50);                            /* bit 2,b -- up             */
	h1 = jr_fwd(0x28);
	ld_a_nn(OAM_SHADOW);
	E2(0xFE, MIN_Y + 2);
	h2 = jr_fwd(0x38);                         /* jr c                      */
	E2(0xD6, 2);
	ld_nn_a(OAM_SHADOW);
	patch(h1); patch(h2);
	E2(0xCB, 0x58);                            /* bit 3,b -- down           */
	h1 = jr_fwd(0x28);
	ld_a_nn(OAM_SHADOW);
	E2(0xFE, MAX_Y - 1);
	h2 = jr_fwd(0x30);                         /* jr nc                     */
	E2(0xC6, 2);
	ld_nn_a(OAM_SHADOW);
	patch(h1); patch(h2);
	E2(0xCB, 0x48);                            /* bit 1,b -- left           */
	h1 = jr_fwd(0x28);
	ld_a_nn(OAM_SHADOW + 1);
	E2(0xFE, MIN_X + 2);
	h2 = jr_fwd(0x38);
	E2(0xD6, 2);
	ld_nn_a(OAM_SHADOW + 1);
	patch(h1); patch(h2);
	E2(0xCB, 0x40);                            /* bit 0,b -- right          */
	h1 = jr_fwd(0x28);
	ld_a_nn(OAM_SHADOW + 1);
	E2(0xFE, MAX_X - 1);
	h2 = jr_fwd(0x30);
	E2(0xC6, 2);
	ld_nn_a(OAM_SHADOW + 1);
	patch(h1); patch(h2);
	ret_();

	check_hit = here();                        /* boxes within 8 pixels     */
	ld_a_nn(OAM_SHADOW);
	E1(0x4F);                                  /* ld c,a                    */
	ld_a_nn(OAM_SHADOW + 4);
	E1(0x91);                                  /* sub c                     */
	h1 = jr_fwd(0x30);                         /* jr nc                     */
	E1(0x2F);                                  /* cpl                       */
	E1(0x3C);                                  /* inc a -- absolute value   */
	patch(h1);
	E2(0xFE, 8);
	E1(0xD0);                                  /* ret nc                    */
	ld_a_nn(OAM_SHADOW + 1);
	E1(0x4F);
	ld_a_nn(OAM_SHADOW + 5);
	E1(0x91);
	h1 = jr_fwd(0x30);
	E1(0x2F);
	E1(0x3C);
	patch(h1);
	E2(0xFE, 8);
	E1(0xD0);
	call_abs(score_inc);
	call_abs(move_target);
	ret_();

	do_start = here();
	E2(0xCB, 0x78);                            /* bit 7,b -- start          */
	E1(0xC8);                                  /* ret z                     */
	E1(0xAF);                                  /* xor a                     */
	ld_nn_a(SCORE_ONES);
	ld_nn_a(SCORE_TENS);
	ld_nn_a(SCORE_HUNS);
	ld_a(HOME_Y);
	ld_nn_a(OAM_SHADOW);
	ld_a(HOME_X);
	ld_nn_a(OAM_SHADOW + 1);
	call_abs(move_target);
	ret_();

	/* ---- entry --------------------------------------------------------- */
	entry = here();
	E1(0xF3);                                  /* di                        */
	E3(0x31, 0xFE, 0xFF);                      /* ld sp,$fffe               */

	ld_hl(MSG_ADDR);                           /* say hello on the wire     */
	l1 = here();
	E1(0x2A);
	E1(0xB7);                                  /* or a                      */
	h1 = jr_fwd(0x28);
	ldh_n(0x01);
	ld_a(0x81);
	ldh_n(0x02);
	jr_back(0x18, l1);
	patch(h1);

	call_abs(wait_vblank);                     /* only then touch the lcd   */
	E1(0xAF);
	ldh_n(0x40);                               /* lcd off                   */

	ld_hl(TILE_ADDR);
	ld_de(0x8000);
	ld_bc(TILE_COUNT * 16);
	call_abs(memcpy_);

	ld_hl(0x9800);                             /* blank the map             */
	ld_bc(0x0400);
	E1(0xAF);
	call_abs(memset_);

	ld_hl(OAM_SHADOW);                         /* and the sprite shadow     */
	ld_bc(0x00A0);
	E1(0xAF);
	call_abs(memset_);

	call_abs(draw_border);
	call_abs(draw_label);

	ld_hl(DMA_ADDR);                           /* dma has to run from hram  */
	ld_de(0xFF80);
	ld_bc(8);
	call_abs(memcpy_);

	ld_a(HOME_Y);
	ld_nn_a(OAM_SHADOW);
	ld_a(HOME_X);
	ld_nn_a(OAM_SHADOW + 1);
	ld_a(TILE_PLAYER);
	ld_nn_a(OAM_SHADOW + 2);
	ld_a(TILE_TARGET);
	ld_nn_a(OAM_SHADOW + 6);
	call_abs(move_target);

	ld_a(0xE4);                                /* four distinct shades      */
	ldh_n(0x47);                               /* bgp                       */
	ld_a(0xE4);
	ldh_n(0x48);                               /* obp0                      */
	E1(0xAF);
	ldh_n(0x42);                               /* scy                       */
	ldh_n(0x43);                               /* scx                       */

	ld_a(0x93);                                /* lcd on, $8000 tiles,      */
	ldh_n(0x40);                               /* sprites on, bg on         */

	loop = here();
	call_abs(wait_vblank);
	ld_a(OAM_SHADOW >> 8);
	call_abs(0xFF80);                          /* the dma stub in hram      */
	call_abs(draw_score);
	call_abs(read_input);
	call_abs(do_start);
	call_abs(move_player);
	call_abs(check_hit);
	jr_back(0x18, loop);

	if (pos > MSG_ADDR) {
		fprintf(stderr, "code overran the data at %04X\n", pos);
		return 1;
	}
	rom[0x0102] = (u8)(entry & 0xFF);          /* the jp's operand, at $0101 */
	rom[0x0103] = (u8)(entry >> 8);

	/* the one value a real boot rom checks */
	sum = 0;
	for (i = 0x134; i <= 0x14C; i++)
		sum = sum - rom[i] - 1;
	rom[0x14D] = (u8)sum;

	if (argc > 1 && write_file(argv[1]) < 0)
		return 1;
	if (argc > 2 && write_c_array(argv[2]) < 0)
		return 1;

	printf("assembled %d bytes of code, %d tiles\n", pos - 0x0150, TILE_COUNT);
	return 0;
}
