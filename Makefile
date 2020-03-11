include Makefile.configure

VERSION 	  = 0.2.10
OBJS		  = compats.o \
		    extract.o \
		    fragment.o \
		    main.o \
		    results.o 
SRCS		  = extract.c \
		    fragment.c \
		    main.c \
		    results.c
XMLS		  = index.xml
HTMLS 		  = atom.xml index.html sintl.1.html
CSSS 		  = index.css 
DOTAR 		  = Makefile \
		    $(SRCS) \
		    sintl.1 \
		    extern.h \
		    compats.c \
		    tests.c
WWWDIR		  = /var/www/vhosts/kristaps.bsd.lv/htdocs/sintl
LIBS_EXPAT	 != pkg-config --libs expat 2>/dev/null || echo "-lexpat"
CFLAGS_EXPAT	 != pkg-config --cflags expat 2>/dev/null || echo ""
CFLAGS		 += $(CFLAGS_EXPAT)
LDADD		 += $(LIBS_EXPAT)

sintl: $(OBJS)
	$(CC) -o $@ $(OBJS) $(LDFLAGS) $(LDADD)

www: $(HTMLS) sintl.tar.gz sintl.tar.gz.sha512

installwww: www
	mkdir -p $(WWWDIR)
	mkdir -p $(WWWDIR)/snapshots
	$(INSTALL_DATA) Makefile $(HTMLS) $(CSSS) $(WWWDIR)
	$(INSTALL_DATA) sintl.tar.gz $(WWWDIR)/snapshots/sintl-$(VERSION).tar.gz
	$(INSTALL_DATA) sintl.tar.gz.sha512 $(WWWDIR)/snapshots/sintl-$(VERSION).tar.gz.sha512
	$(INSTALL_DATA) sintl.tar.gz $(WWWDIR)/snapshots
	$(INSTALL_DATA) sintl.tar.gz.sha512 $(WWWDIR)/snapshots

install: sintl
	mkdir -p $(DESTDIR)$(BINDIR)
	mkdir -p $(DESTDIR)$(MANDIR)/man1
	$(INSTALL_PROGRAM) sintl $(DESTDIR)$(BINDIR)
	$(INSTALL_MAN) sintl.1 $(DESTDIR)$(MANDIR)/man1

sintl.tar.gz:
	mkdir -p .dist/sintl-$(VERSION)/
	install -m 0644 $(DOTAR) .dist/sintl-$(VERSION)
	install -m 0755 configure .dist/sintl-$(VERSION)
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
	sblg -a versions.xml

sintl.1.html: sintl.1
	mandoc -Ostyle=https://bsd.lv/css/mandoc.css -Thtml sintl.1 >$@

index.html: index.xml versions.xml sample-input.html sample-xliff.html sample-output.html
	sblg -s cmdline -t index.xml -o $@ versions.xml sample-input.html sample-xliff.html sample-output.html

clean:
	rm -f sintl $(OBJS) $(HTMLS) sintl.tar.gz sintl.tar.gz.sha512
	rm -f sample-input.html sample-xliff.html sample-output.html sample-output.xml

regress:
	# Do nothing.

distcheck: sintl.tar.gz.sha512
	mandoc -Tlint -Werror sintl.1
	newest=`grep "<h1>" versions.xml | head -n1 | sed 's![ 	]*!!g'` ; \
	       [ "$$newest" = "<h1>$(VERSION)</h1>" ] || \
		{ echo "Version $(VERSION) not newest in versions.xml" 1>&2 ; exit 1 ; }
	rm -rf .distcheck
	[ "`openssl dgst -sha512 -hex sintl.tar.gz`" = "`cat sintl.tar.gz.sha512`" ] || \
 		{ echo "Checksum does not match." 1>&2 ; exit 1 ; }
	mkdir -p .distcheck
	tar -zvxpf sintl.tar.gz -C .distcheck
	( cd .distcheck/sintl-$(VERSION) && ./configure PREFIX=prefix )
	( cd .distcheck/sintl-$(VERSION) && $(MAKE) )
	( cd .distcheck/sintl-$(VERSION) && $(MAKE) regress )
	( cd .distcheck/sintl-$(VERSION) && $(MAKE) install )
	rm -rf .distcheck

distclean: clean
	rm -f Makefile.configure config.h config.log
