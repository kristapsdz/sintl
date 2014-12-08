.SUFFIXES: .xml .html

VERSION 	 = 0.1.0
VDATE 		 = 2014-11-29
PREFIX 		 = /usr/local
CFLAGS 		+= -g -W -Wall -Wstrict-prototypes -Wno-unused-parameter -Wwrite-strings 
OBJS		 = compat-reallocarray.o \
		   compat-strlcat.o \
		   compat-strlcpy.o \
		   extract.o \
		   main.o 
SRCS		 = compat-reallocarray.c \
		   compat-strlcat.c \
		   compat-strlcpy.c \
		   extract.c \
		   main.c
TESTS 		 = test-reallocarray.c \
      		   test-strlcat.c \
      		   test-strlcpy.c 
VERSIONS	 = version_0_1_0.xml 
XMLS		 = index.xml
ATOM 		 = atom.xml
HTMLS 		 = index.html sintl.1.html
CSSS 		 = index.css 
BINDIR 		 = $(PREFIX)/bin
MANDIR 		 = $(PREFIX)/man
DOTAR 		 = Makefile \
		   $(SRCS) \
		   sintl.1 \
		   extern.h \
		   configure \
		   config.h.post \
		   config.h.pre \
		   $(TESTS)

sintl: $(OBJS)
	$(CC) -o $@ $(OBJS) -lexpat

www: $(HTMLS) $(ATOM) sintl.tar.gz sintl.tar.gz.sha512

installwww: www
	mkdir -p $(PREFIX)
	mkdir -p $(PREFIX)/snapshots
	install -m 0444 Makefile $(ATOM) $(HTMLS) $(CSSS) $(WWWDIR)
	install -m 0444 sintl.tar.gz $(WWWDIR)/snapshots/sintl-$(VERSION).tar.gz
	install -m 0444 sintl.tar.gz.sha512 $(WWWDIR)/snapshots/sintl-$(VERSION).tar.gz.sha512
	install -m 0444 sintl.tar.gz $(WWWDIR)/snapshots
	install -m 0444 sintl.tar.gz.sha512 $(WWWDIR)/snapshots

install: sintl
	mkdir -p $(DESTDIR)$(BINDIR)
	mkdir -p $(DESTDIR)$(MANDIR)/man1
	install -m 0755 sintl $(DESTDIR)$(BINDIR)
	install -m 0444 sintl.1 $(DESTDIR)$(MANDIR)/man1

sintl.tar.gz:
	mkdir -p .dist/sintl-$(VERSION)/
	install -m 0644 $(DOTAR) .dist/sintl-$(VERSION)
	( cd .dist/ && tar zcf ../$@ ./ )
	rm -rf .dist/

sintl.tar.gz.sha512: sintl.tar.gz
	openssl dgst -sha512 sintl.tar.gz >$@

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
	rm -f sintl $(ATOM) $(OBJS) $(HTMLS) sintl.tar.gz sintl.tar.gz.sha512
	rm -f config.h config.log
	rm -rf *.dSYM
