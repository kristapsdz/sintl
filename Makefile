include Makefile.configure

VERSION 	 = 0.2.1
OBJS		 = compats.o \
		   extract.o \
		   fragment.o \
		   main.o \
		   results.o 
SRCS		 = extract.c \
		   fragment.c \
		   main.c \
		   results.c
XMLS		 = index.xml
HTMLS 		 = index.html sintl.1.html
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
WWWDIR		 = /var/www/vhosts/kristaps.bsd.lv/htdocs/sintl

sintl: $(OBJS)
	$(CC) -o $@ $(OBJS) -lexpat

www: $(HTMLS) sintl.tar.gz sintl.tar.gz.sha512

installwww: www
	mkdir -p $(WWWDIR)
	mkdir -p $(WWWDIR)/snapshots
	install -m 0444 Makefile $(HTMLS) $(CSSS) $(WWWDIR)
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

sample-input.html: sample-input.xml
	( echo '<article data-sblg-article="1" data-sblg-tags="code">'; \
	  highlight -t 2 -S html -l -f --out-format=xhtml --enclose-pre sample-input.xml ; \
	  echo '</article>'; ) >$@

sample-output.xml: sample-input.xml sample-input.xliff sintl
	./sintl -j sample-input.xliff sample-input.xml >$@

sample-output.html: sample-output.xml
	( echo '<article data-sblg-article="1" data-sblg-tags="code">'; \
	  highlight -t 2 -S html -l -f --out-format=xhtml --enclose-pre sample-output.xml ; \
	  echo '</article>'; ) >$@

sample-xliff.html: sample-input.xliff
	( echo '<article data-sblg-article="1" data-sblg-tags="code">'; \
	  highlight -t 2 -S xml -l -f --out-format=xhtml --enclose-pre sample-input.xliff ; \
	  echo '</article>'; ) >$@

$(OBJS): extern.h

atom.xml: versions.xml
	sblg -o $@ -a versions.xml

sintl.1.html: sintl.1
	mandoc -Ostyle=mandoc.css -Thtml sintl.1 >$@

index.html: index.xml versions.xml sample-input.html sample-xliff.html sample-output.html
	sblg -s cmdline -t index.xml -o $@ versions.xml sample-input.html sample-xliff.html sample-output.html

clean:
	rm -f sintl $(OBJS) $(HTMLS) sintl.tar.gz sintl.tar.gz.sha512
	rm -f sample-input.html sample-xliff.html sample-output.html sample-output.xml

distclean: clean
	rm -f Makefile.configure config.h config.log
