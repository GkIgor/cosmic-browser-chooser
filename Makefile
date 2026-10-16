CC ?= cc
CFLAGS ?= -O2 -pipe
PKG_CONFIG ?= pkg-config
PACKAGES = gtk4 gio-2.0 gio-unix-2.0

browser-chooser: chooser.c
	$(CC) $(CFLAGS) -std=c11 -Wall -Wextra $(shell $(PKG_CONFIG) --cflags $(PACKAGES)) -o $@ $< $(shell $(PKG_CONFIG) --libs $(PACKAGES))

.PHONY: clean
clean:
	rm -f browser-chooser
