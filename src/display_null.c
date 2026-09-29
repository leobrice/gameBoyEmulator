/* display_null.c -- the backend used when there is no window system */
#include "display.h"

int display_init(const char *title, int scale)
{
	(void)title;
	(void)scale;
	return -1;
}

void display_quit(void) {}
void display_frame(const u8 *fb) { (void)fb; }
void display_sync(void) {}

bool display_poll(u8 *buttons)
{
	(void)buttons;
	return true;
}
