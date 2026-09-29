/* mkrom.c -- hand assembles the snake game that ships inside the emulator
 *
 * There is no assembler here on purpose: in a project this size you should be
 * able to read every byte that runs.  emit() lays down opcodes, jr_fwd() and
 * patch() close a forward branch, and every subroutine is emitted before its
 * callers so each call is a backward reference to an address already known.
 *
 * The game keeps the snake's body in the background tile map, which means the
 * map doubles as the collision test: walk onto a blank tile and you move, onto
 * the food tile and you grow, onto anything else and you are dead.  The body
 * cells also live in a ring buffer so the tail knows which cell to rub out.
 * Only the head is a sprite, which keeps oam dma in the picture.
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
	/* 12-20: S C O R E G A M V */
	"        " "  ####  " " #      " " #      " "  ###   " "     #  " " ####   " "        ",
	"        " "  ####  " " #      " " #      " " #      " " #      " "  ####  " "        ",
	"        " "  ###   " " #   #  " " #   #  " " #   #  " " #   #  " "  ###   " "        ",
	"        " " ####   " " #   #  " " ####   " " #  #   " " #   #  " " #   #  " "        ",
	"        " " #####  " " #      " " ####   " " #      " " #      " " #####  " "        ",
	"        " "  ####  " " #      " " #  ##  " " #   #  " " #   #  " "  ###   " "        ",
	"        " "  ###   " " #   #  " " #   #  " " #####  " " #   #  " " #   #  " "        ",
	"        " " #   #  " " ## ##  " " # # #  " " #   #  " " #   #  " " #   #  " "        ",
	"        " " #   #  " " #   #  " " #   #  " "  # #   " "  # #   " "   #    " "        ",
	/* 21: the snake's body */
	" ++++++ " "+######+" "+######+" "+######+"
	"+######+" "+######+" "+######+" " ++++++ ",
	/* 22: its head, the one sprite in the game */
	" ###### " "########" "##.##.##" "########"
	"########" "###..###" "########" " ###### ",
	/* 23: the food */
	"   ##   " "  #..#  " " #....# " "#......#"
	"#......#" " #....# " "  #..#  " "   ##   "
};

#define TILE_COUNT   (int)(sizeof(tiles) / sizeof(tiles[0]))
#define TILE_BLANK   0
#define TILE_WALL    1
#define TILE_DIGIT   2
#define TILE_LETTER  12
#define TILE_BODY    21
#define TILE_HEAD    22
#define TILE_FOOD    23

/* ---- rom layout -------------------------------------------------------- */

#define MSG_ADDR    0x1000
#define TILE_ADDR   0x1100
#define OVER_ADDR   0x1400      /* the nine tiles spelling GAME OVER */
#define DMA_ADDR    0x1500

/* ---- work ram ---------------------------------------------------------- */

#define HEAD_COL    0xC000
#define HEAD_ROW    0xC001
#define DIR         0xC002      /* 0 right, 1 left, 2 up, 3 down */
#define PENDING     0xC003
#define TIMER       0xC004
#define DEAD        0xC005
#define SEED        0xC006
#define HEAD_IDX    0xC007
#define TAIL_IDX    0xC008
#define SCORE_ONES  0xC009
#define SCORE_TENS  0xC00A
#define SCORE_HUNS  0xC00B
#define FOOD_COL    0xC00C
#define FOOD_ROW    0xC00D
#define RING        0xC100      /* 128 cells, two bytes each */
#define OAM_SHADOW  0xC200      /* dma needs a page boundary */

#define STEP_FRAMES 8           /* one move every eighth frame */
#define START_COL   9
#define START_ROW   9

static const u8 logo[48] = {
	0xCE,0xED,0x66,0x66,0xCC,0x0D,0x00,0x0B,0x03,0x73,0x00,0x83,
	0x00,0x0C,0x00,0x0D,0x00,0x08,0x11,0x1F,0x88,0x89,0x00,0x0E,
	0xDC,0xCC,0x6E,0xE6,0xDD,0xDD,0xD9,0x99,0xBB,0xBB,0x67,0x63,
	0x6E,0x0E,0xEC,0xCC,0xDD,0xDC,0x99,0x9F,0xBB,0xB9,0x33,0x3E
};

static const char msg[] = "snake -- d-pad to turn, start to restart\n";

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
	FILE  *f = fopen(path, "wb");
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
	int wait_vblank, memcpy_, memset_, read_input, rand_, cell_addr;
	int push_cell, pop_tail, put_body, score_inc, draw_score, draw_border;
	int draw_label, spawn_food, draw_over, step_snake, set_dir, init_game;
	int handle_keys, place_head, maybe_step;
	int entry, loop, l1, l2, h1, h2, h3, h4, i, sum;

	memset(rom, 0x00, sizeof(rom));

	/* ---- header -------------------------------------------------------- */
	pos = 0x0100;
	E1(0x00);                                  /* nop                       */
	E3(0xC3, 0x00, 0x00);                      /* jp entry, patched below   */
	memcpy(rom + 0x104, logo, sizeof(logo));
	memcpy(rom + 0x134, "SNAKE", 5);
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

	pos = OVER_ADDR;                           /* G A M E _ O V E R         */
	E3(TILE_LETTER + 5, TILE_LETTER + 6, TILE_LETTER + 7);
	E3(TILE_LETTER + 4, TILE_BLANK, TILE_LETTER + 2);
	E3(TILE_LETTER + 8, TILE_LETTER + 4, TILE_LETTER + 3);

	pos = DMA_ADDR;                            /* this runs from hram       */
	E2(0xE0, 0x46);                            /* ldh ($46),a  start dma    */
	ld_a(0x28);
	l1 = here();
	E1(0x3D);                                  /* dec a                     */
	jr_back(0x20, l1);
	ret_();

	/* ---- subroutines, callees before their callers --------------------- */
	pos = 0x0150;

	wait_vblank = here();
	l1 = here();
	ldh_a(0x44);
	E2(0xFE, 144);
	jr_back(0x28, l1);                         /* wait out the current one  */
	l1 = here();
	ldh_a(0x44);
	E2(0xFE, 144);
	jr_back(0x20, l1);                         /* then wait for the next    */
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
	E1(0x0B);
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
	E1(0x47);
	ld_a(0x30);                                /* deselect                  */
	ldh_n(0x00);
	ret_();

	rand_ = here();                            /* div stirred into a seed   */
	ldh_a(0x04);
	E1(0x4F);                                  /* ld c,a                    */
	ld_a_nn(SEED);
	E1(0x81);                                  /* add a,c                   */
	E2(0xC6, 0x57);
	ld_nn_a(SEED);
	ret_();

	cell_addr = here();                        /* d=col e=row -> hl in map  */
	E2(0x26, 0x00);                            /* ld h,0                    */
	E1(0x6B);                                  /* ld l,e                    */
	E1(0x29); E1(0x29); E1(0x29); E1(0x29); E1(0x29);   /* hl = row * 32    */
	E1(0x7D);                                  /* ld a,l                    */
	E1(0x82);                                  /* add a,d -- col is < 32,   */
	E1(0x6F);                                  /* ld l,a     so no carry    */
	E1(0x7C);                                  /* ld a,h                    */
	E2(0xC6, 0x98);                            /* add a,$98                 */
	E1(0x67);                                  /* ld h,a                    */
	ret_();

	push_cell = here();                        /* d,e onto the ring         */
	ld_a_nn(HEAD_IDX);
	E1(0x87);                                  /* add a,a -- two bytes each */
	E1(0x6F);                                  /* ld l,a                    */
	E2(0x26, RING >> 8);                       /* ld h,$c1                  */
	E1(0x7A);                                  /* ld a,d                    */
	E1(0x22);                                  /* ld (hl+),a                */
	E1(0x7B);                                  /* ld a,e                    */
	E1(0x77);                                  /* ld (hl),a                 */
	ld_a_nn(HEAD_IDX);
	E1(0x3C);
	E2(0xE6, 0x7F);
	ld_nn_a(HEAD_IDX);
	ret_();

	pop_tail = here();                         /* oldest cell into d,e      */
	ld_a_nn(TAIL_IDX);
	E1(0x87);
	E1(0x6F);
	E2(0x26, RING >> 8);
	E1(0x2A);                                  /* ld a,(hl+)                */
	E1(0x57);                                  /* ld d,a                    */
	E1(0x7E);                                  /* ld a,(hl)                 */
	E1(0x5F);                                  /* ld e,a                    */
	ld_a_nn(TAIL_IDX);
	E1(0x3C);
	E2(0xE6, 0x7F);
	ld_nn_a(TAIL_IDX);
	ret_();

	put_body = here();                         /* draw d,e and remember it  */
	E1(0xD5);                                  /* push de                   */
	call_abs(cell_addr);
	ld_a(TILE_BODY);
	E1(0x77);                                  /* ld (hl),a                 */
	E1(0xD1);                                  /* pop de                    */
	call_abs(push_cell);
	ret_();

	score_inc = here();                        /* three decimal digits      */
	ld_hl(SCORE_ONES);
	l2 = here();
	E1(0x7E);
	E1(0x3C);
	E2(0xFE, 10);
	h1 = jr_fwd(0x20);                         /* jr nz,store               */
	E1(0xAF);                                  /* xor a                     */
	E1(0x22);                                  /* ld (hl+),a -- carry over  */
	E1(0x7D);                                  /* ld a,l -- past the end?   */
	E2(0xFE, (SCORE_HUNS + 1) & 0xFF);
	h2 = jr_fwd(0x28);
	jr_back(0x18, l2);
	patch(h1);
	E1(0x77);
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
	E2(0x0E, 20);
	ld_a(TILE_WALL);
	l1 = here();
	E1(0x22);
	E1(0x0D);
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
	E1(0x77);
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
	E1(0x3C);
	E1(0x0D);
	jr_back(0x20, l1);
	ret_();

	spawn_food = here();                       /* keep trying blank cells   */
	E2(0x06, 32);                              /* ld b,32 attempts          */
	l1 = here();
	call_abs(rand_);
	E2(0xE6, 0x1F);
	E2(0xFE, 18);
	h1 = jr_fwd(0x30);                         /* jr nc,again -- out of row */
	E1(0x3C);                                  /* inc a -- cols are 1..18   */
	E1(0x57);                                  /* ld d,a                    */
	call_abs(rand_);
	E2(0xE6, 0x0F);
	E2(0xFE, 15);
	h2 = jr_fwd(0x30);
	E2(0xC6, 2);                               /* rows are 2..16            */
	E1(0x5F);                                  /* ld e,a                    */
	E1(0xD5);                                  /* push de                   */
	call_abs(cell_addr);
	E1(0x7E);                                  /* ld a,(hl)                 */
	E1(0xB7);                                  /* or a -- must be blank     */
	h3 = jr_fwd(0x20);
	ld_a(TILE_FOOD);
	E1(0x77);
	E1(0xD1);                                  /* pop de                    */
	E1(0x7A);
	ld_nn_a(FOOD_COL);
	E1(0x7B);
	ld_nn_a(FOOD_ROW);
	ret_();
	patch(h3);
	E1(0xD1);                                  /* pop de                    */
	patch(h1); patch(h2);
	E1(0x05);                                  /* dec b                     */
	jr_back(0x20, l1);
	ret_();                                    /* board full: leave it be   */

	draw_over = here();                        /* GAME OVER across the middle */
	E2(0x16, 5);                               /* ld d,5                    */
	E2(0x1E, 9);                               /* ld e,9                    */
	call_abs(cell_addr);
	E1(0x54);                                  /* ld d,h                    */
	E1(0x5D);                                  /* ld e,l                    */
	ld_hl(OVER_ADDR);
	ld_bc(9);
	call_abs(memcpy_);
	ret_();

	step_snake = here();
	ld_a_nn(PENDING);                          /* the turn takes effect now */
	ld_nn_a(DIR);
	ld_a_nn(HEAD_COL);
	E1(0x57);                                  /* ld d,a                    */
	ld_a_nn(HEAD_ROW);
	E1(0x5F);                                  /* ld e,a                    */
	ld_a_nn(DIR);
	E2(0xFE, 0);
	h1 = jr_fwd(0x20);
	E1(0x14);                                  /* inc d -- right            */
	h4 = jr_fwd(0x18);
	patch(h1);
	E2(0xFE, 1);
	h1 = jr_fwd(0x20);
	E1(0x15);                                  /* dec d -- left             */
	h2 = jr_fwd(0x18);
	patch(h1);
	E2(0xFE, 2);
	h1 = jr_fwd(0x20);
	E1(0x1D);                                  /* dec e -- up               */
	h3 = jr_fwd(0x18);
	patch(h1);
	E1(0x1C);                                  /* inc e -- down             */
	patch(h2); patch(h3); patch(h4);

	E1(0xD5);                                  /* push de -- the new head   */
	call_abs(cell_addr);
	E1(0x7E);                                  /* ld a,(hl) -- what is there */
	E1(0xD1);                                  /* pop de -- a survives      */
	E2(0xFE, TILE_FOOD);
	h1 = jr_fwd(0x28);                         /* jr z,eat                  */
	E1(0xB7);                                  /* or a -- blank?            */
	h2 = jr_fwd(0x28);                         /* jr z,move                 */
	ld_a(1);                                   /* wall or our own body      */
	ld_nn_a(DEAD);
	call_abs(draw_over);
	ret_();

	patch(h2);                                 /* move: rub out the tail    */
	E1(0xD5);
	call_abs(pop_tail);
	call_abs(cell_addr);
	E1(0xAF);
	E1(0x77);                                  /* ld (hl),a                 */
	E1(0xD1);
	h3 = jr_fwd(0x18);

	patch(h1);                                 /* eat: keep the tail, score */
	E1(0xD5);
	call_abs(score_inc);
	call_abs(spawn_food);
	E1(0xD1);

	patch(h3);                                 /* the old head becomes body */
	E1(0xD5);
	ld_a_nn(HEAD_COL);
	E1(0x57);
	ld_a_nn(HEAD_ROW);
	E1(0x5F);
	call_abs(put_body);
	E1(0xD1);
	E1(0x7A);                                  /* ld a,d                    */
	ld_nn_a(HEAD_COL);
	E1(0x7B);                                  /* ld a,e                    */
	ld_nn_a(HEAD_ROW);
	ret_();

	set_dir = here();                          /* a = wanted direction      */
	E1(0x4F);                                  /* ld c,a                    */
	ld_a_nn(DIR);
	E2(0xEE, 0x01);                            /* xor 1 -- the way back     */
	E1(0xB9);                                  /* cp c                      */
	E1(0xC8);                                  /* ret z -- no reversing     */
	E1(0x79);                                  /* ld a,c                    */
	ld_nn_a(PENDING);
	ret_();

	init_game = here();
	ldh_a(0x40);                               /* only wait for vblank if   */
	E2(0xCB, 0x7F);                            /* the lcd is actually on,   */
	h1 = jr_fwd(0x28);                         /* or we would wait forever  */
	call_abs(wait_vblank);
	patch(h1);
	E1(0xAF);
	ldh_n(0x40);                               /* lcd off to redraw it all  */
	ld_hl(0x9800);
	ld_bc(0x0400);
	E1(0xAF);
	call_abs(memset_);
	call_abs(draw_border);
	call_abs(draw_label);
	E1(0xAF);
	ld_nn_a(DEAD);
	ld_nn_a(DIR);                              /* 0 = heading right         */
	ld_nn_a(PENDING);
	ld_nn_a(HEAD_IDX);
	ld_nn_a(TAIL_IDX);
	ld_nn_a(SCORE_ONES);
	ld_nn_a(SCORE_TENS);
	ld_nn_a(SCORE_HUNS);
	ld_a(STEP_FRAMES);
	ld_nn_a(TIMER);
	ld_a(START_COL);
	ld_nn_a(HEAD_COL);
	ld_a(START_ROW);
	ld_nn_a(HEAD_ROW);
	E2(0x16, START_COL - 3);                   /* three segments of tail    */
	E2(0x1E, START_ROW);
	call_abs(put_body);
	E2(0x16, START_COL - 2);
	E2(0x1E, START_ROW);
	call_abs(put_body);
	E2(0x16, START_COL - 1);
	E2(0x1E, START_ROW);
	call_abs(put_body);
	call_abs(spawn_food);
	ld_a(0x93);                                /* lcd on, $8000 tiles,      */
	ldh_n(0x40);                               /* sprites on, bg on         */
	ret_();

	handle_keys = here();                      /* b holds the keys          */
	ld_a_nn(DEAD);
	E1(0xB7);
	h1 = jr_fwd(0x20);                         /* jr nz,dead                */
	E2(0xCB, 0x40);                            /* bit 0,b -- right          */
	h2 = jr_fwd(0x28);
	E1(0xAF);
	call_abs(set_dir);
	patch(h2);
	E2(0xCB, 0x48);                            /* bit 1,b -- left           */
	h2 = jr_fwd(0x28);
	ld_a(1);
	call_abs(set_dir);
	patch(h2);
	E2(0xCB, 0x50);                            /* bit 2,b -- up             */
	h2 = jr_fwd(0x28);
	ld_a(2);
	call_abs(set_dir);
	patch(h2);
	E2(0xCB, 0x58);                            /* bit 3,b -- down           */
	h2 = jr_fwd(0x28);
	ld_a(3);
	call_abs(set_dir);
	patch(h2);
	ret_();
	patch(h1);
	E2(0xCB, 0x78);                            /* bit 7,b -- start          */
	E1(0xC8);                                  /* ret z                     */
	call_abs(init_game);
	ret_();

	place_head = here();                       /* the head sprite follows   */
	ld_a_nn(HEAD_ROW);
	E1(0x87); E1(0x87); E1(0x87);              /* row * 8                   */
	E2(0xC6, 16);                              /* sprites sit 16 down       */
	ld_nn_a(OAM_SHADOW);
	ld_a_nn(HEAD_COL);
	E1(0x87); E1(0x87); E1(0x87);
	E2(0xC6, 8);                               /* and 8 across              */
	ld_nn_a(OAM_SHADOW + 1);
	ret_();

	maybe_step = here();                       /* one move every so often   */
	ld_a_nn(DEAD);
	E1(0xB7);
	E1(0xC0);                                  /* ret nz                    */
	ld_a_nn(TIMER);
	E1(0x3D);                                  /* dec a                     */
	ld_nn_a(TIMER);                            /* does not touch the flags  */
	E1(0xC0);                                  /* ret nz                    */
	ld_a(STEP_FRAMES);
	ld_nn_a(TIMER);
	call_abs(step_snake);
	ret_();

	/* ---- entry --------------------------------------------------------- */
	entry = here();
	E1(0xF3);                                  /* di                        */
	E3(0x31, 0xFE, 0xFF);                      /* ld sp,$fffe               */

	ld_hl(MSG_ADDR);                           /* say hello on the wire     */
	l1 = here();
	E1(0x2A);
	E1(0xB7);
	h1 = jr_fwd(0x28);
	ldh_n(0x01);
	ld_a(0x81);
	ldh_n(0x02);
	jr_back(0x18, l1);
	patch(h1);

	call_abs(wait_vblank);
	E1(0xAF);
	ldh_n(0x40);                               /* lcd off                   */

	ld_hl(TILE_ADDR);
	ld_de(0x8000);
	ld_bc(TILE_COUNT * 16);
	call_abs(memcpy_);

	ld_hl(OAM_SHADOW);                         /* one sprite, rest hidden   */
	ld_bc(0x00A0);
	E1(0xAF);
	call_abs(memset_);
	ld_a(TILE_HEAD);
	ld_nn_a(OAM_SHADOW + 2);

	ld_hl(DMA_ADDR);                           /* dma has to run from hram  */
	ld_de(0xFF80);
	ld_bc(8);
	call_abs(memcpy_);

	ld_a(0xE4);                                /* four distinct shades      */
	ldh_n(0x47);                               /* bgp                       */
	ld_a(0xE4);
	ldh_n(0x48);                               /* obp0                      */
	E1(0xAF);
	ldh_n(0x42);                               /* scy                       */
	ldh_n(0x43);                               /* scx                       */

	ldh_a(0x04);                               /* seed off the divider      */
	ld_nn_a(SEED);

	call_abs(init_game);                       /* turns the lcd back on     */

	loop = here();
	call_abs(read_input);
	call_abs(handle_keys);
	call_abs(wait_vblank);                     /* everything below writes   */
	ld_a(OAM_SHADOW >> 8);                     /* to vram, so stay in the   */
	call_abs(0xFF80);                          /* blanking interval         */
	call_abs(draw_score);
	call_abs(maybe_step);
	call_abs(place_head);
	jr_back(0x18, loop);

	if (pos > MSG_ADDR) {
		fprintf(stderr, "code overran the data at %04X\n", pos);
		return 1;
	}
	rom[0x0102] = (u8)(entry & 0xFF);          /* the jp's operand          */
	rom[0x0103] = (u8)(entry >> 8);

	sum = 0;                                   /* the header checksum       */
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
