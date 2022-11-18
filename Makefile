CC?=gcc
CFLAGS+=-Wall -Wno-unused-function -O0 -g -D_FILE_OFFSET_BITS=64 -DFUSE_USE_VERSION=26 
LDFLAGS+=-Wall -lfuse -lpthread
srcdir=src
builddir=build

all: $(builddir) distillerfs

$(builddir):
	mkdir $(builddir)

distillerfs: $(builddir)/distillerfs.o $(builddir)/utils.o $(builddir)/toml.o 
	$(CC) $(CFLAGS) -o distillerfs $(builddir)/distillerfs.o $(builddir)/utils.o $(builddir)/toml.o $(LDFLAGS)

$(builddir)/distillerfs.o: $(srcdir)/distillerfs.c
	$(CC) $(CFLAGS) -o $(builddir)/distillerfs.o -c $(srcdir)/distillerfs.c $(CFLAGS)

$(builddir)/utils.o: $(srcdir)/utils.c $(srcdir)/utils.h
	$(CC) $(CFLAGS) -o $(builddir)/utils.o -c $(srcdir)/utils.c $(CFLAGS)

$(builddir)/toml.o: $(srcdir)/toml.c $(srcdir)/toml.h
	$(CC) $(CFLAGS) -o $(builddir)/toml.o -c $(srcdir)/toml.c $(CFLAGS)

clean:
	rm -rf $(builddir)/

install:
	mkdir -p $(DESTDIR)/usr/share/man/man1 $(DESTDIR)/usr/bin $(DESTDIR)/etc
	gzip < distillerfs.1 > $(DESTDIR)/usr/share/man/man1/distillerfs.1.gz
	cp distillerfs $(DESTDIR)/usr/bin/

mrproper: clean
	rm -rf distillerfs
