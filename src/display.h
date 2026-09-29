/* display.h -- the thin seam between the emulator and a host window */
#ifndef GB_DISPLAY_H
#define GB_DISPLAY_H

#include "common.h"

/* returns 0 on success, -1 if no window could be opened */
int  display_init(const char *title, int scale);
void display_quit(void);

/* draws a frame of 160x144 shade indices (0 lightest .. 3 darkest) */
void display_frame(const u8 *fb);

/* pumps host events; returns false when the user wants to quit.
   *buttons receives the current BTN_* bitmask. */
bool display_poll(u8 *buttons);

/* sleeps just long enough to hold ~59.7 frames a second */
void display_sync(void);

#endif
