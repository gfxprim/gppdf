CFLAGS=-W -Wall -Wextra -O2 $(shell gfxprim-config --cflags)
LDLIBS=-lgfxprim $(shell gfxprim-config --libs-widgets) -lmupdf
BIN=gppdf
DEP=$(BIN:=.dep)

all: $(DEP) $(BIN)

%.dep: %.c
	$(CC) $(CFLAGS) -M $< -o $@

-include $(DEP)

install:
	install -m 644 -D $(BIN).json $(DESTDIR)/etc/gp_apps/$(BIN)/layout.json
	install -D $(BIN) -t $(DESTDIR)/usr/bin/

clean:
	rm -f $(BIN) *.dep *.o
