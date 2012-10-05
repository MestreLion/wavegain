SHELL    = /bin/sh
CC       = gcc

TARGET   = wavegain
DEFS     = -DHAVE_CONFIG_H
LIBS     = -lm
SOURCES := $(wildcard *.c)
HEADERS := $(wildcard *.h)

prefix   = /usr/local
bindir   = $(prefix)/bin

all: release

debug: CFLAGS += -g -O0 -Wall -Wextra
debug: clean $(TARGET)

release: CFLAGS += -s
release: clean $(TARGET)

$(TARGET): $(SOURCES) $(HEADERS)
	$(CC) $(CFLAGS) $(DEFS) -o $(TARGET) $(SOURCES) $(LIBS)

install: $(TARGET)
	install -s -D $(TARGET) $(DESTDIR)$(bindir)/$(TARGET)

uninstall:
	rm -f $(DESTDIR)$(bindir)/$(TARGET)

clean:
	rm -f $(TARGET)

distclean: clean

.PHONY : all debug release install uninstall clean distclean
