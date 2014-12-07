.SUFFIXES: .xml .html

VERSION 	 = 0.1.0
VDATE 		 = 2014-11-29
PREFIX 		 = /usr/local
CFLAGS 		+= -g -W -Wall -Wstrict-prototypes -Wno-unused-parameter -Wwrite-strings 
OBJS		 = compat-reallocarray.o \
		   compat-strlcat.o \
		   compat-strlcpy.o \
		   extract.o \
		   join.o \
		   main.o 
SRCS		 = compat-reallocarray.c \
		   compat-strlcat.c \
		   compat-strlcpy.c \
		   extract.c \
		   join.c \
		   main.c
TESTS 		 = test-reallocarray.c \
      		   test-strlcat.c \
      		   test-strlcpy.c 
VERSIONS	 = version_0_1_0.xml 
XMLS		 = index.xml
ATOM 		 = atom.xml
HTMLS 		 = index.html sitstool.1.html
CSSS 		 = index.css 
BINDIR 		 = $(PREFIX)/bin
MANDIR 		 = $(PREFIX)/man
DOTAR 		 = Makefile \
		   $(SRCS) \
		   sitstool.1 \
		   extern.h \
		   configure \
		   config.h.post \
		   config.h.pre \
		   $(TESTS)

sitstool: $(OBJS)
	$(CC) -o $@ $(OBJS) -lexpat

www: $(HTMLS) $(ATOM) sitstool.tar.gz sitstool.tar.gz.sha512

installwww: www
	mkdir -p $(PREFIX)
	mkdir -p $(PREFIX)/snapshots
	install -m 0444 Makefile $(ATOM) $(HTMLS) $(CSSS) $(WWWDIR)
	install -m 0444 sitstool.tar.gz $(WWWDIR)/snapshots/sitstool-$(VERSION).tar.gz
	install -m 0444 sitstool.tar.gz.sha512 $(WWWDIR)/snapshots/sitstool-$(VERSION).tar.gz.sha512
	install -m 0444 sitstool.tar.gz $(WWWDIR)/snapshots
	install -m 0444 sitstool.tar.gz.sha512 $(WWWDIR)/snapshots

install: sitstool
	mkdir -p $(DESTDIR)$(BINDIR)
	mkdir -p $(DESTDIR)$(MANDIR)/man1
	install -m 0755 sitstool $(DESTDIR)$(BINDIR)
	install -m 0444 sitstool.1 $(DESTDIR)$(MANDIR)/man1

sitstool.tar.gz:
	mkdir -p .dist/sitstool-$(VERSION)/
	install -m 0644 $(DOTAR) .dist/sitstool-$(VERSION)
	( cd .dist/ && tar zcf ../$@ ./ )
	rm -rf .dist/

sitstool.tar.gz.sha512: sitstool.tar.gz
	openssl dgst -sha512 sitstool.tar.gz >$@

config.h: config.h.pre config.h.post configure $(TESTS)
	rm -f config.log
	CC="$(CC)" CFLAGS="$(CFLAGS)" ./configure

$(OBJS): extern.h config.h

atom.xml: atom-template.xml

$(ARTICLES): article.xml

index.html: index.xml $(ARTICLES) $(VERSIONS)
	sblg -o- -t index.xml $(ARTICLES) $(VERSIONS) | \
		sed -e "s!@VERSION@!$(VERSION)!g" -e "s!@VDATE@!$(VDATE)!g" >$@

atom.xml: $(VERSIONS)
	sblg -o $@ -a $(VERSIONS)

.1.1.html:
	mandoc -Thtml $< >$@

clean:
	rm -f sitstool $(ATOM) $(OBJS) $(HTMLS) sitstool.tar.gz sitstool.tar.gz.sha512
	rm -f config.h config.log
	rm -rf *.dSYM
