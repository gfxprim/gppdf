CFLAGS=-W -Wall -Wextra -O2 $(shell gfxprim-config --cflags)
LDLIBS=-lgfxprim $(shell gfxprim-config --libs-widgets) -lmupdf

OS=$(shell grep '^ID=' '/etc/os-release')

ifeq ($(OS), ID=gentoo)
NO_HACK=1
endif

ifndef NO_HACK
$(info Hacking around static devel libraries for libmupdf!)
LDLIBS+=-lmupdf-third -lm
# libfreetype6
LDLIBS+=$(shell pkg-config --libs freetype2)
LDLIBS+=$(shell pkg-config --libs zlib)
LDLIBS+=$(shell pkg-config --libs libopenjp2)
LDLIBS+=$(shell pkg-config --libs libjpeg)
# libharfbuzz0b
LDLIBS+=$(shell pkg-config --libs harfbuzz)
# libjbig2dec0
LDLIBS+=-ljbig2dec
endif

BIN=gppdf
DEP=$(BIN:=.dep)

all: $(DEP) $(BIN)

%.dep: %.c
	$(CC) $(CFLAGS) -M $< -o $@

-include $(DEP)

install:
	install -m 644 -D layout.json $(DESTDIR)/etc/gp_apps/$(BIN)/layout.json
	install -D $(BIN) -t $(DESTDIR)/usr/bin/

clean:
	rm -f $(BIN) *.dep *.o
