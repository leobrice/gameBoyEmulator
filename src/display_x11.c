/* display_x11.c -- a window, four shades of green, and a keyboard */
#include "display.h"
#include "gb.h"

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/XKBlib.h>
#include <X11/keysym.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* the washed out green of an original dmg */
static const u32 shades[4] = { 0xE0F8D0, 0x88C070, 0x346856, 0x081820 };

static Display *dpy;
static Window   win;
static GC       gc;
static XImage  *img;
static u32     *pixels;
static Atom     wm_delete;
static int      scale = 3;
static u8       buttons;
static bool     running = true;
static struct timespec next_frame;

static u8 key_to_button(KeySym k)
{
	switch (k) {
	case XK_Up:        return BTN_UP;
	case XK_Down:      return BTN_DOWN;
	case XK_Left:      return BTN_LEFT;
	case XK_Right:     return BTN_RIGHT;
	case XK_z: case XK_Z: return BTN_A;
	case XK_x: case XK_X: return BTN_B;
	case XK_Return: case XK_space: return BTN_START;
	case XK_BackSpace: case XK_Shift_R: return BTN_SELECT;
	}
	return 0;
}

int display_init(const char *title, int s)
{
	int screen, depth;
	Bool supported;

	scale = s > 0 ? s : 3;

	dpy = XOpenDisplay(NULL);
	if (!dpy)
		return -1;

	screen = DefaultScreen(dpy);
	depth  = DefaultDepth(dpy, screen);
	if (depth < 24) {
		fprintf(stderr, "need a truecolor display (got depth %d)\n", depth);
		XCloseDisplay(dpy);
		dpy = NULL;
		return -1;
	}

	win = XCreateSimpleWindow(dpy, RootWindow(dpy, screen), 0, 0,
	                          GB_W * scale, GB_H * scale, 0,
	                          BlackPixel(dpy, screen), BlackPixel(dpy, screen));

	XStoreName(dpy, win, title);
	XSelectInput(dpy, win, ExposureMask | KeyPressMask | KeyReleaseMask | StructureNotifyMask);

	/* without this, held keys arrive as a stream of release/press pairs */
	XkbSetDetectableAutoRepeat(dpy, True, &supported);

	wm_delete = XInternAtom(dpy, "WM_DELETE_WINDOW", False);
	XSetWMProtocols(dpy, win, &wm_delete, 1);

	{	/* the window manager should leave the aspect alone */
		XSizeHints *hints = XAllocSizeHints();

		if (hints) {
			hints->flags = PMinSize;
			hints->min_width  = GB_W;
			hints->min_height = GB_H;
			XSetWMNormalHints(dpy, win, hints);
			XFree(hints);
		}
	}

	gc = XCreateGC(dpy, win, 0, NULL);

	pixels = malloc((size_t)GB_W * GB_H * scale * scale * sizeof(u32));
	if (!pixels) {
		display_quit();
		return -1;
	}

	img = XCreateImage(dpy, DefaultVisual(dpy, screen), (unsigned)depth, ZPixmap, 0,
	                   (char *)pixels, GB_W * scale, GB_H * scale, 32, 0);
	if (!img) {
		display_quit();
		return -1;
	}

	XMapWindow(dpy, win);
	XFlush(dpy);

	clock_gettime(CLOCK_MONOTONIC, &next_frame);
	return 0;
}

void display_quit(void)
{
	if (img) {
		XDestroyImage(img);     /* takes the pixel buffer with it */
		img = NULL;
	} else {
		free(pixels);
	}
	pixels = NULL;
	if (dpy) {
		XCloseDisplay(dpy);
		dpy = NULL;
	}
}

void display_frame(const u8 *fb)
{
	int y, x, sy, sx;
	int pitch = GB_W * scale;

	if (!dpy)
		return;

	for (y = 0; y < GB_H; y++) {
		u32 *row = pixels + (size_t)y * scale * pitch;

		for (x = 0; x < GB_W; x++) {
			u32 c = shades[fb[y * GB_W + x] & 3];

			for (sx = 0; sx < scale; sx++)
				row[x * scale + sx] = c;
		}
		for (sy = 1; sy < scale; sy++)
			memcpy(row + (size_t)sy * pitch, row, (size_t)pitch * sizeof(u32));
	}

	XPutImage(dpy, win, gc, img, 0, 0, 0, 0, (unsigned)pitch, (unsigned)(GB_H * scale));
	XFlush(dpy);
}

bool display_poll(u8 *out)
{
	XEvent ev;

	if (!dpy)
		return true;

	while (XPending(dpy)) {
		XNextEvent(dpy, &ev);
		switch (ev.type) {
		case KeyPress: {
			KeySym k = XLookupKeysym(&ev.xkey, 0);

			if (k == XK_Escape)
				running = false;
			buttons |= key_to_button(k);
			break;
		}
		case KeyRelease:
			buttons &= (u8)~key_to_button(XLookupKeysym(&ev.xkey, 0));
			break;
		case ClientMessage:
			if ((Atom)ev.xclient.data.l[0] == wm_delete)
				running = false;
			break;
		}
	}

	*out = buttons;
	return running;
}

void display_sync(void)
{
	static const long frame_ns = 16742706;   /* 70224 cycles at 4.194304 MHz */
	struct timespec now;

	next_frame.tv_nsec += frame_ns;
	while (next_frame.tv_nsec >= 1000000000L) {
		next_frame.tv_nsec -= 1000000000L;
		next_frame.tv_sec++;
	}

	clock_gettime(CLOCK_MONOTONIC, &now);
	if (now.tv_sec > next_frame.tv_sec ||
	    (now.tv_sec == next_frame.tv_sec && now.tv_nsec > next_frame.tv_nsec)) {
		next_frame = now;            /* we fell behind: give up on the lost time */
		return;
	}

	clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &next_frame, NULL);
}
