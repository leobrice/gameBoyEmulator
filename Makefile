# gameboy -- a small dmg emulator
#
#   make                build ./gameboy
#   make run ROM=x.gb   build and run a rom
#   make test           build the bundled test rom and run it headless
#   make BACKEND=null   build without any window system

CC      ?= cc
CFLAGS  ?= -O2
CFLAGS  += -std=c99 -D_POSIX_C_SOURCE=200809L -Wall -Wextra -Wno-unused-parameter
LDLIBS  ?=

BIN     := gameboy
OBJDIR  := build
SRCS    := src/main.c src/gb.c src/cpu.c src/mmu.c src/ppu.c \
           src/timer.c src/cart.c src/joypad.c

# pick a display backend unless one was asked for
BACKEND ?= auto
ifeq ($(BACKEND),auto)
  BACKEND := $(shell ls /usr/include/X11/Xlib.h >/dev/null 2>&1 && echo x11 || echo null)
endif

ifeq ($(BACKEND),x11)
  SRCS   += src/display_x11.c
  LDLIBS += -lX11
else
  SRCS   += src/display_null.c
endif

OBJS := $(SRCS:src/%.c=$(OBJDIR)/%.o) $(OBJDIR)/builtin_rom.o
DEPS := $(OBJS:.o=.d)

# everything but main and the display: what the test harness links against
CORE := $(filter-out $(OBJDIR)/main.o $(OBJDIR)/display_%.o,$(OBJS))

.PHONY: all run test clean

all: $(BIN)

$(BIN): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $(OBJS) $(LDFLAGS) $(LDLIBS)

$(OBJDIR)/%.o: src/%.c | $(OBJDIR)
	$(CC) $(CFLAGS) -MMD -MP -c -o $@ $<

$(OBJDIR):
	mkdir -p $(OBJDIR)

$(OBJDIR)/mkrom: tools/mkrom.c | $(OBJDIR)
	$(CC) $(CFLAGS) -o $@ $<

# one run of mkrom produces both the standalone rom and the bytes we link in
$(OBJDIR)/builtin_rom.c tests/snake.gb &: $(OBJDIR)/mkrom
	$(OBJDIR)/mkrom tests/snake.gb $(OBJDIR)/builtin_rom.c

$(OBJDIR)/builtin_rom.o: $(OBJDIR)/builtin_rom.c
	$(CC) $(CFLAGS) -c -o $@ $<

run: $(BIN)
	./$(BIN) $(ROM)

$(OBJDIR)/playtest: tests/playtest.c $(CORE)
	$(CC) $(CFLAGS) -o $@ $< $(CORE)

test: $(BIN) tests/snake.gb $(OBJDIR)/playtest
	@$(OBJDIR)/playtest
	@tests/run.sh ./$(BIN)

clean:
	rm -rf $(OBJDIR) $(BIN) tests/snake.gb

-include $(DEPS)
