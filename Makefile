SHELL    = /bin/sh
CC       = gcc

TARGET   = wavegain
CFLAGS  += -m32
DEFS     = -DHAVE_CONFIG_H
LIBS     = -lm
SOURCES := $(wildcard *.c)
HEADERS := $(wildcard *.h)

prefix   = /usr/local
bindir   = $(prefix)/bin

all: release

# DEBUG Build
# Add several compile- and run-time checks, warnings and preventions (Buffer Overflow, etc)
# -g3: Adds debugging information, level 3 (default for -g is level 2)
# -Og: Enables optimizations that do not interfere with debugging
# -Wall: Enables all the warnings about constructions that some users consider questionable, and that are easy to avoid
# -Wextra: Enables some extra warning flags that are not enabled by -Wall
debug: CFLAGS += -g3 -Og -Wall -Wextra -Wpedantic -D_FORTIFY_SOURCE=2
debug: clean $(TARGET)

# RELEASE Build
# Stripped binary, maximum optimization
# -s: Strip executable (remove all symbol table and relocation information)
# -O2: Optimize level 2 and activate default FORTIFY_SOURCE=2 in most setups
# -D_FORTIFY_SOURCE=2: explicitely enable additional compile-time and run-time checks for several libc functions
release: CFLAGS += -s -O2 -D_FORTIFY_SOURCE=2
release: clean $(TARGET)

# ASAN (Address Sanitizer) build.
# Find some run-time errors that debug build misses.
# -U_FORTIFY_SOURCE: Disable FORTIFY_SOURCE, supposedly does not play well with ASAN
# -fsanitize=address: enable ASAN
# -fno-omit-frame-pointer: recommended to generate better stack traces on debugging
# -static-libasan: needed on some platforms to avoid run-time errors without using LD_PRELOAD
asan: CFLAGS += -g3 -O2 -U_FORTIFY_SOURCE -fsanitize=address -fno-omit-frame-pointer -static-libasan
asan: clean $(TARGET)

$(TARGET): $(SOURCES) $(HEADERS)
	$(CC) $(CFLAGS) $(DEFS) -o $(TARGET) $(SOURCES) $(LIBS)

install: $(TARGET)
	install -s -D $(TARGET) $(DESTDIR)$(bindir)/$(TARGET)

uninstall:
	rm -f $(DESTDIR)$(bindir)/$(TARGET)

clean:
	rm -f $(TARGET)

distclean: clean

.PHONY : all debug release asan install uninstall clean distclean
