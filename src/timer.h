/* timer.h -- DIV / TIMA / TMA / TAC */
#ifndef GB_TIMER_H
#define GB_TIMER_H

#include "gb.h"

void timer_step(struct gb *gb, int cycles);
void timer_write_div(struct gb *gb);
void timer_write_tac(struct gb *gb, u8 v);

#endif
