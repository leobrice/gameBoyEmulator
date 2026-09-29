/* cpu.h -- the sharp lr35902 */
#ifndef GB_CPU_H
#define GB_CPU_H

#include "gb.h"

/* register slots, numbered the way the opcodes encode them */
enum { RB = 0, RC, RD, RE, RH, RL, RHL, RA };

#define FZ 0x80
#define FN 0x40
#define FH 0x20
#define FC 0x10

void cpu_reset(struct gb *gb);
int  cpu_step(struct gb *gb);   /* returns the t-cycles consumed */

#endif
