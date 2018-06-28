include Makefile.configure

VERSION 	 = 0.1.2
OBJS		 = compats.o \
		   extract.o \
		   main.o \
		   results.o 
SRCS		 = extract.c \
		   main.c \
		   results.c
XMLS		 = index.xml
HTMLS 		 = index.html index.fr.html sintl.1.html
CSSS 		 = mandoc.css index.css 
BINDIR 		 = $(PREFIX)/bin
MANDIR 		 = $(PREFIX)/man
DOTAR 		 = Makefile \
		   $(SRCS) \
		   sintl.1 \
		   extern.h \
		   compats.c \
		   configure \
		   tests.c

sintl: $(OBJS)
	$(CC) -o $@ $(OBJS) -lexpat

www: $(HTMLS) sintl.tar.gz sintl.tar.gz.sha512

installwww: www
	mkdir -p $(PREFIX)
	mkdir -p $(PREFIX)/snapshots
	install -m 0444 Makefile $(HTMLS) $(CSSS) $(PREFIX)
	install -m 0444 sintl.tar.gz $(PREFIX)/snapshots/sintl-$(VERSION).tar.gz
	install -m 0444 sintl.tar.gz.sha512 $(PREFIX)/snapshots/sintl-$(VERSION).tar.gz.sha512
	install -m 0444 sintl.tar.gz $(PREFIX)/snapshots
	install -m 0444 sintl.tar.gz.sha512 $(PREFIX)/snapshots

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

$(OBJS): extern.h

atom.xml: versions.xml
	sblg -o $@ -a versions.xml

sintl.1.html: sintl.1
	mandoc -Ostyle=mandoc.css -Thtml sintl.1 >$@

index.html: index.xml
	sblg -t index.xml -o $@ versions.xml

index.fr.html: index.xml sintl
	sblg -t index.xml -o- versions.xml | ./sintl -j fr.xliff >$@

clean:
	rm -f sintl $(OBJS) $(HTMLS) sintl.tar.gz sintl.tar.gz.sha512

distclean: clean
	rm -f Makefile.configure config.h config.log
