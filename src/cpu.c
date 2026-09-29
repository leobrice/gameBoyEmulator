#include "cpu.h"
#include "mmu.h"

#include <stdio.h>

/* t-cycles per opcode; conditional instructions list the untaken cost */
static const u8 op_cycles[256] = {
	 4,12, 8, 8, 4, 4, 8, 4,20, 8, 8, 8, 4, 4, 8, 4,
	 4,12, 8, 8, 4, 4, 8, 4,12, 8, 8, 8, 4, 4, 8, 4,
	 8,12, 8, 8, 4, 4, 8, 4, 8, 8, 8, 8, 4, 4, 8, 4,
	 8,12, 8, 8,12,12,12, 4, 8, 8, 8, 8, 4, 4, 8, 4,
	 4, 4, 4, 4, 4, 4, 8, 4, 4, 4, 4, 4, 4, 4, 8, 4,
	 4, 4, 4, 4, 4, 4, 8, 4, 4, 4, 4, 4, 4, 4, 8, 4,
	 4, 4, 4, 4, 4, 4, 8, 4, 4, 4, 4, 4, 4, 4, 8, 4,
	 8, 8, 8, 8, 8, 8, 4, 8, 4, 4, 4, 4, 4, 4, 8, 4,
	 4, 4, 4, 4, 4, 4, 8, 4, 4, 4, 4, 4, 4, 4, 8, 4,
	 4, 4, 4, 4, 4, 4, 8, 4, 4, 4, 4, 4, 4, 4, 8, 4,
	 4, 4, 4, 4, 4, 4, 8, 4, 4, 4, 4, 4, 4, 4, 8, 4,
	 4, 4, 4, 4, 4, 4, 8, 4, 4, 4, 4, 4, 4, 4, 8, 4,
	 8,12,12,16,12,16, 8,16, 8,16,12, 4,12,24, 8,16,
	 8,12,12, 4,12,16, 8,16, 8,16,12, 4,12, 4, 8,16,
	12,12, 8, 4, 4,16, 8,16,16, 4,16, 4, 4, 4, 8,16,
	12,12, 8, 4, 4,16, 8,16,12, 8,16, 4, 4, 4, 8,16
};

/* ---- register helpers ------------------------------------------------- */

static u16 get_hl(struct cpu *c) { return (u16)(c->r[RH] << 8 | c->r[RL]); }

static void set_hl(struct cpu *c, u16 v)
{
	c->r[RH] = v >> 8;
	c->r[RL] = v & 0xFF;
}

/* reg index 6 means "the byte hl points at" */
static u8 rd_r(struct gb *gb, int i)
{
	return i == RHL ? mmu_rb(gb, get_hl(&gb->cpu)) : gb->cpu.r[i];
}

static void wr_r(struct gb *gb, int i, u8 v)
{
	if (i == RHL)
		mmu_wb(gb, get_hl(&gb->cpu), v);
	else
		gb->cpu.r[i] = v;
}

/* the four 16-bit pairs, as encoded in bits 4-5 (sp variant) */
static u16 rd_rr(struct cpu *c, int i)
{
	switch (i) {
	case 0:  return (u16)(c->r[RB] << 8 | c->r[RC]);
	case 1:  return (u16)(c->r[RD] << 8 | c->r[RE]);
	case 2:  return get_hl(c);
	default: return c->sp;
	}
}

static void wr_rr(struct cpu *c, int i, u16 v)
{
	switch (i) {
	case 0:  c->r[RB] = v >> 8; c->r[RC] = v & 0xFF; break;
	case 1:  c->r[RD] = v >> 8; c->r[RE] = v & 0xFF; break;
	case 2:  set_hl(c, v); break;
	default: c->sp = v; break;
	}
}

static void set_flags(struct cpu *c, bool z, bool n, bool h, bool cy)
{
	c->f = (u8)((z ? FZ : 0) | (n ? FN : 0) | (h ? FH : 0) | (cy ? FC : 0));
}

/* ---- fetch and stack -------------------------------------------------- */

static u8 fetch(struct gb *gb)
{
	return mmu_rb(gb, gb->cpu.pc++);
}

static u16 fetch16(struct gb *gb)
{
	u16 v = mmu_rw(gb, gb->cpu.pc);

	gb->cpu.pc += 2;
	return v;
}

static void push(struct gb *gb, u16 v)
{
	gb->cpu.sp -= 2;
	mmu_ww(gb, gb->cpu.sp, v);
}

static u16 pop(struct gb *gb)
{
	u16 v = mmu_rw(gb, gb->cpu.sp);

	gb->cpu.sp += 2;
	return v;
}

/* ---- alu -------------------------------------------------------------- */

static void alu(struct gb *gb, int op, u8 v)
{
	struct cpu *c  = &gb->cpu;
	u8          a  = c->r[RA];
	u8          cy = (c->f & FC) ? 1 : 0;
	u16         r;

	switch (op) {
	case 0: /* ADD */
		r = (u16)a + v;
		set_flags(c, (r & 0xFF) == 0, false, ((a & 0xF) + (v & 0xF)) > 0xF, r > 0xFF);
		c->r[RA] = r & 0xFF;
		break;
	case 1: /* ADC */
		r = (u16)a + v + cy;
		set_flags(c, (r & 0xFF) == 0, false, ((a & 0xF) + (v & 0xF) + cy) > 0xF, r > 0xFF);
		c->r[RA] = r & 0xFF;
		break;
	case 2: /* SUB */
		r = (u16)(a - v);
		set_flags(c, (r & 0xFF) == 0, true, (a & 0xF) < (v & 0xF), a < v);
		c->r[RA] = r & 0xFF;
		break;
	case 3: /* SBC */
		r = (u16)(a - v - cy);
		set_flags(c, (r & 0xFF) == 0, true, (a & 0xF) < ((v & 0xF) + cy),
		          (int)a < (int)v + cy);
		c->r[RA] = r & 0xFF;
		break;
	case 4: /* AND */
		c->r[RA] = a & v;
		set_flags(c, c->r[RA] == 0, false, true, false);
		break;
	case 5: /* XOR */
		c->r[RA] = a ^ v;
		set_flags(c, c->r[RA] == 0, false, false, false);
		break;
	case 6: /* OR */
		c->r[RA] = a | v;
		set_flags(c, c->r[RA] == 0, false, false, false);
		break;
	default: /* CP -- a sub that keeps the result to itself */
		set_flags(c, a == v, true, (a & 0xF) < (v & 0xF), a < v);
		break;
	}
}

static u8 inc8(struct cpu *c, u8 v)
{
	u8 r = v + 1;

	set_flags(c, r == 0, false, (v & 0xF) == 0xF, (c->f & FC) != 0);
	return r;
}

static u8 dec8(struct cpu *c, u8 v)
{
	u8 r = v - 1;

	set_flags(c, r == 0, true, (v & 0xF) == 0, (c->f & FC) != 0);
	return r;
}

static void add_hl(struct cpu *c, u16 v)
{
	u16 hl = get_hl(c);
	u32 r  = (u32)hl + v;

	set_flags(c, (c->f & FZ) != 0, false, ((hl & 0x0FFF) + (v & 0x0FFF)) > 0x0FFF,
	          r > 0xFFFF);
	set_hl(c, (u16)r);
}

/* sp + signed byte, used by both e8 instructions */
static u16 add_sp(struct cpu *c, s8 e)
{
	u16 sp = c->sp;

	set_flags(c, false, false, ((sp & 0x0F) + (e & 0x0F)) > 0x0F,
	          ((sp & 0xFF) + (u8)e) > 0xFF);
	return (u16)(sp + e);
}

static void daa(struct cpu *c)
{
	u8 a  = c->r[RA];
	u8 cy = 0;

	if (!(c->f & FN)) {
		if ((c->f & FC) || a > 0x99) { a += 0x60; cy = FC; }
		if ((c->f & FH) || (a & 0x0F) > 0x09) a += 0x06;
	} else {
		if (c->f & FC) { a -= 0x60; cy = FC; }
		if (c->f & FH) a -= 0x06;
	}

	c->r[RA] = a;
	c->f = (u8)((a == 0 ? FZ : 0) | (c->f & FN) | cy);
}

/* ---- cb prefix -------------------------------------------------------- */

static int cb_op(struct gb *gb)
{
	struct cpu *c  = &gb->cpu;
	u8          op = fetch(gb);
	int         i  = op & 7;
	u8          v  = rd_r(gb, i);
	u8          cy = (c->f & FC) ? 1 : 0;
	u8          r;

	if (op < 0x40) {
		switch (op >> 3) {
		case 0: r = (u8)(v << 1 | v >> 7);  set_flags(c, r == 0, 0, 0, v & 0x80); break; /* RLC  */
		case 1: r = (u8)(v >> 1 | v << 7);  set_flags(c, r == 0, 0, 0, v & 0x01); break; /* RRC  */
		case 2: r = (u8)(v << 1 | cy);      set_flags(c, r == 0, 0, 0, v & 0x80); break; /* RL   */
		case 3: r = (u8)(v >> 1 | cy << 7); set_flags(c, r == 0, 0, 0, v & 0x01); break; /* RR   */
		case 4: r = (u8)(v << 1);           set_flags(c, r == 0, 0, 0, v & 0x80); break; /* SLA  */
		case 5: r = (u8)(v >> 1 | (v & 0x80)); set_flags(c, r == 0, 0, 0, v & 0x01); break; /* SRA */
		case 6: r = (u8)(v >> 4 | v << 4);  set_flags(c, r == 0, 0, 0, false);     break; /* SWAP */
		default:r = (u8)(v >> 1);           set_flags(c, r == 0, 0, 0, v & 0x01); break; /* SRL  */
		}
		wr_r(gb, i, r);
	} else {
		int bit = (op >> 3) & 7;

		if (op < 0x80) {           /* BIT -- carry is left alone */
			c->f = (u8)((v & (1 << bit) ? 0 : FZ) | FH | (c->f & FC));
			return (i == RHL) ? 12 : 8;
		} else if (op < 0xC0) {    /* RES */
			wr_r(gb, i, (u8)(v & ~(1 << bit)));
		} else {                   /* SET */
			wr_r(gb, i, (u8)(v | (1 << bit)));
		}
	}

	return (i == RHL) ? 16 : 8;
}

/* ---- interrupts ------------------------------------------------------- */

static const u16 int_vector[5] = { 0x40, 0x48, 0x50, 0x58, 0x60 };

static int service_interrupt(struct gb *gb)
{
	u8 pending = gb->ie & gb->iflag & 0x1F;
	int i;

	if (!pending)
		return 0;

	/* a pending interrupt wakes the cpu even when ime is clear */
	gb->cpu.halted = false;
	if (!gb->cpu.ime)
		return 0;

	for (i = 0; i < 5; i++) {
		if (pending & (1 << i)) {
			gb->cpu.ime = false;
			gb->iflag &= (u8)~(1 << i);
			push(gb, gb->cpu.pc);
			gb->cpu.pc = int_vector[i];
			return 20;
		}
	}
	return 0;
}

/* ---- trace ------------------------------------------------------------ */

static void trace(struct gb *gb)
{
	struct cpu *c = &gb->cpu;

	printf("A:%02X F:%02X B:%02X C:%02X D:%02X E:%02X H:%02X L:%02X SP:%04X PC:%04X "
	       "PCMEM:%02X,%02X,%02X,%02X\n",
	       c->r[RA], c->f, c->r[RB], c->r[RC], c->r[RD], c->r[RE], c->r[RH], c->r[RL],
	       c->sp, c->pc,
	       mmu_rb(gb, c->pc), mmu_rb(gb, (u16)(c->pc + 1)),
	       mmu_rb(gb, (u16)(c->pc + 2)), mmu_rb(gb, (u16)(c->pc + 3)));
}

/* ---- the step --------------------------------------------------------- */

void cpu_reset(struct gb *gb)
{
	struct cpu *c = &gb->cpu;

	/* the state the boot rom leaves behind on a dmg */
	c->r[RA] = 0x01; c->f = 0xB0;
	c->r[RB] = 0x00; c->r[RC] = 0x13;
	c->r[RD] = 0x00; c->r[RE] = 0xD8;
	c->r[RH] = 0x01; c->r[RL] = 0x4D;
	c->sp = 0xFFFE;
	c->pc = 0x0100;
	c->ime = false;
	c->ime_pending = false;
	c->halted = false;
	c->stopped = false;
}

int cpu_step(struct gb *gb)
{
	struct cpu *c = &gb->cpu;
	bool ime_was_pending;
	u8   op;
	int  cyc;

	cyc = service_interrupt(gb);
	if (cyc)
		return cyc;

	if (c->halted)
		return 4;

	/* ei only takes hold after the instruction that follows it */
	ime_was_pending = c->ime_pending;

	if (gb->trace)
		trace(gb);

	op  = fetch(gb);
	cyc = op_cycles[op];

	if (op >= 0x40 && op < 0x80) {
		if (op == 0x76) {                 /* HALT */
			c->halted = true;
		} else {
			wr_r(gb, (op >> 3) & 7, rd_r(gb, op & 7));
		}
	} else if (op >= 0x80 && op < 0xC0) {
		alu(gb, (op >> 3) & 7, rd_r(gb, op & 7));
	} else if (op < 0x40) {
		switch (op & 0x0F) {
		case 0x1:                          /* LD rr,d16 */
			wr_rr(c, op >> 4, fetch16(gb));
			break;
		case 0x2: case 0xA: {              /* LD (rr),A and LD A,(rr) */
			u16 addr;

			switch (op >> 4) {
			case 0:  addr = rd_rr(c, 0); break;
			case 1:  addr = rd_rr(c, 1); break;
			case 2:  addr = get_hl(c); set_hl(c, addr + 1); break;
			default: addr = get_hl(c); set_hl(c, addr - 1); break;
			}
			if ((op & 0x0F) == 0x2)
				mmu_wb(gb, addr, c->r[RA]);
			else
				c->r[RA] = mmu_rb(gb, addr);
			break;
		}
		case 0x3:                          /* INC rr */
			wr_rr(c, op >> 4, (u16)(rd_rr(c, op >> 4) + 1));
			break;
		case 0xB:                          /* DEC rr */
			wr_rr(c, op >> 4, (u16)(rd_rr(c, op >> 4) - 1));
			break;
		case 0x9:                          /* ADD HL,rr */
			add_hl(c, rd_rr(c, op >> 4));
			break;
		case 0x4: case 0xC:                /* INC r */
			wr_r(gb, (op >> 3) & 7, inc8(c, rd_r(gb, (op >> 3) & 7)));
			break;
		case 0x5: case 0xD:                /* DEC r */
			wr_r(gb, (op >> 3) & 7, dec8(c, rd_r(gb, (op >> 3) & 7)));
			break;
		case 0x6: case 0xE:                /* LD r,d8 */
			wr_r(gb, (op >> 3) & 7, fetch(gb));
			break;
		default:
			switch (op) {
			case 0x00:                      /* NOP */
				break;
			case 0x08:                      /* LD (a16),SP */
				mmu_ww(gb, fetch16(gb), c->sp);
				break;
			case 0x10:                      /* STOP */
				fetch(gb);
				c->stopped = true;
				break;
			case 0x07:                      /* RLCA */
				c->r[RA] = (u8)(c->r[RA] << 1 | c->r[RA] >> 7);
				set_flags(c, false, false, false, c->r[RA] & 0x01);
				break;
			case 0x0F:                      /* RRCA */
				set_flags(c, false, false, false, c->r[RA] & 0x01);
				c->r[RA] = (u8)(c->r[RA] >> 1 | c->r[RA] << 7);
				break;
			case 0x17: {                    /* RLA */
				u8 cy = (c->f & FC) ? 1 : 0;

				set_flags(c, false, false, false, c->r[RA] & 0x80);
				c->r[RA] = (u8)(c->r[RA] << 1 | cy);
				break;
			}
			case 0x1F: {                    /* RRA */
				u8 cy = (c->f & FC) ? 1 : 0;

				set_flags(c, false, false, false, c->r[RA] & 0x01);
				c->r[RA] = (u8)(c->r[RA] >> 1 | cy << 7);
				break;
			}
			case 0x27:                      /* DAA */
				daa(c);
				break;
			case 0x2F:                      /* CPL */
				c->r[RA] = (u8)~c->r[RA];
				c->f |= FN | FH;
				break;
			case 0x37:                      /* SCF */
				c->f = (u8)((c->f & FZ) | FC);
				break;
			case 0x3F:                      /* CCF */
				c->f = (u8)((c->f & FZ) | ((c->f & FC) ? 0 : FC));
				break;
			default: {                      /* JR, JR cc */
				s8   e    = (s8)fetch(gb);
				bool take = true;

				if (op != 0x18) {
					int cc = (op >> 3) & 3;

					take = (cc == 0) ? !(c->f & FZ) : (cc == 1) ? (c->f & FZ) != 0
					     : (cc == 2) ? !(c->f & FC) : (c->f & FC) != 0;
				}
				if (take) {
					c->pc = (u16)(c->pc + e);
					if (op != 0x18)
						cyc += 4;
				}
				break;
			}
			}
			break;
		}
	} else {
		switch (op) {
		case 0xCB:
			cyc = cb_op(gb);
			break;

		case 0xC3:                          /* JP a16 */
			c->pc = fetch16(gb);
			break;
		case 0xE9:                          /* JP HL */
			c->pc = get_hl(c);
			break;
		case 0xCD:                          /* CALL a16 */
		{
			u16 addr = fetch16(gb);

			push(gb, c->pc);
			c->pc = addr;
			break;
		}
		case 0xC9:                          /* RET */
			c->pc = pop(gb);
			break;
		case 0xD9:                          /* RETI */
			c->pc = pop(gb);
			c->ime = true;
			break;

		case 0xC2: case 0xCA: case 0xD2: case 0xDA:   /* JP cc,a16 */
		case 0xC4: case 0xCC: case 0xD4: case 0xDC:   /* CALL cc,a16 */
		case 0xC0: case 0xC8: case 0xD0: case 0xD8: { /* RET cc */
			int  cc   = (op >> 3) & 3;
			bool take = (cc == 0) ? !(c->f & FZ) : (cc == 1) ? (c->f & FZ) != 0
			          : (cc == 2) ? !(c->f & FC) : (c->f & FC) != 0;

			if ((op & 7) == 0) {                     /* RET cc */
				if (take) {
					c->pc = pop(gb);
					cyc += 12;
				}
			} else {
				u16 addr = fetch16(gb);

				if (take) {
					if ((op & 7) == 4) {             /* CALL cc */
						push(gb, c->pc);
						cyc += 12;
					} else {                         /* JP cc */
						cyc += 4;
					}
					c->pc = addr;
				}
			}
			break;
		}

		case 0xC1: case 0xD1: case 0xE1:    /* POP rr */
			wr_rr(c, (op >> 4) & 3, pop(gb));
			break;
		case 0xF1: {                        /* POP AF -- the low nibble of f is fixed at 0 */
			u16 v = pop(gb);

			c->r[RA] = v >> 8;
			c->f = v & 0xF0;
			break;
		}
		case 0xC5: case 0xD5: case 0xE5:    /* PUSH rr */
			push(gb, rd_rr(c, (op >> 4) & 3));
			break;
		case 0xF5:                          /* PUSH AF */
			push(gb, (u16)(c->r[RA] << 8 | c->f));
			break;

		case 0xE0:                          /* LDH (a8),A */
			mmu_wb(gb, (u16)(0xFF00 + fetch(gb)), c->r[RA]);
			break;
		case 0xF0:                          /* LDH A,(a8) */
			c->r[RA] = mmu_rb(gb, (u16)(0xFF00 + fetch(gb)));
			break;
		case 0xE2:                          /* LD (C),A */
			mmu_wb(gb, (u16)(0xFF00 + c->r[RC]), c->r[RA]);
			break;
		case 0xF2:                          /* LD A,(C) */
			c->r[RA] = mmu_rb(gb, (u16)(0xFF00 + c->r[RC]));
			break;
		case 0xEA:                          /* LD (a16),A */
			mmu_wb(gb, fetch16(gb), c->r[RA]);
			break;
		case 0xFA:                          /* LD A,(a16) */
			c->r[RA] = mmu_rb(gb, fetch16(gb));
			break;

		case 0xE8:                          /* ADD SP,r8 */
			c->sp = add_sp(c, (s8)fetch(gb));
			break;
		case 0xF8:                          /* LD HL,SP+r8 */
			set_hl(c, add_sp(c, (s8)fetch(gb)));
			break;
		case 0xF9:                          /* LD SP,HL */
			c->sp = get_hl(c);
			break;

		case 0xF3:                          /* DI */
			c->ime = false;
			c->ime_pending = false;
			break;
		case 0xFB:                          /* EI */
			c->ime_pending = true;
			break;

		default:
			if ((op & 0xC7) == 0xC6) {      /* ALU A,d8 */
				alu(gb, (op >> 3) & 7, fetch(gb));
			} else if ((op & 0xC7) == 0xC7) { /* RST */
				push(gb, c->pc);
				c->pc = op & 0x38;
			} else {
				fprintf(stderr, "illegal opcode %02X at %04X\n", op, (u16)(c->pc - 1));
				c->stopped = true;
			}
			break;
		}
	}

	if (ime_was_pending) {
		c->ime = true;
		c->ime_pending = false;
	}

	return cyc;
}
