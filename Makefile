.PHONY: regress clean distcheck distclean

include Makefile.configure

VERSION 	  = 0.2.11
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

LDADD_PKG	!= pkg-config --libs expat || echo "-lexpat"
CFLAGS_PKG 	!= pkg-config --cflags expat || echo ""
LDADD		+= $(LDADD_PKG)
CFLAGS		+= $(CFLAGS_PKG)

all: sintl

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
	rm -rf .dist/
	mkdir -p .dist/sintl-$(VERSION)
	mkdir -p .dist/sintl-$(VERSION)/regress/join-pass
	mkdir -p .dist/sintl-$(VERSION)/regress/join-fail
	install -m 0644 $(DOTAR) .dist/sintl-$(VERSION)
	install -m 0644 regress/join-pass/*\.* .dist/sintl-$(VERSION)/regress/join-pass
	install -m 0644 regress/join-fail/*\.* .dist/sintl-$(VERSION)/regress/join-fail
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

# - regress/join-pass
#   Runs sintl -j IN_XLIFF IN_XML > OUT_HAVE_HTML
#   Checks that OUT_HAVE_HTML matches OUT_WANT_HTML.
# - regress/join-fail
#   Runs sintl -j IN_XLIFF IN_XML
#   Expects the command to fail (badly-formed).

regress: all
	@tmp=`mktemp` ; \
	set +e ; \
	for f in regress/join-pass/*.xml ; do \
		./sintl -j regress/join-pass/`basename $$f .xml`.xliff $$f > $$tmp ; \
		if [ $$? -ne 0 ] ; \
		then \
			echo "$$f: fail (command fail)" ; \
			rm -f $$tmp ; \
			exit 1 ; \
		fi ; \
		diff $$tmp regress/join-pass/`basename $$f .xml`.html >/dev/null 2>&1 ; \
		if [ $$? -ne 0 ] ; \
		then \
			echo "$$f: fail (diff)" ; \
			rm -f $$tmp ; \
			exit 1 ; \
		fi ; \
		echo "$$f: ok" ; \
	done ; \
	rm -f $$tmp ; \
	for f in regress/join-fail/*.xml ; do \
		./sintl -j regress/join-fail/`basename $$f .xml`.xliff $$f >/dev/null 2>&1 ; \
		if [ $$? -eq 0 ] ; \
		then \
			echo "$$f: fail (expected errors)" ; \
			exit 1 ; \
		fi ; \
		echo "$$f: ok" ; \
	done 

distcheck: sintl.tar.gz.sha512
	mandoc -Tlint -Werror sintl.1
	newest=`grep "<h1>" versions.xml | head -1 | sed 's![ 	]*!!g'` ; \
	       [ "$$newest" = "<h1>$(VERSION)</h1>" ] || \
		{ echo "Version $(VERSION) not newest in versions.xml" 1>&2 ; exit 1 ; }
	rm -rf .distcheck
	[ "`openssl dgst -sha512 -hex sintl.tar.gz`" = "`cat sintl.tar.gz.sha512`" ] || \
 		{ echo "Checksum does not match." 1>&2 ; exit 1 ; }
	mkdir -p .distcheck
	( cd .distcheck && tar -zvxpf ../sintl.tar.gz )
	( cd .distcheck/sintl-$(VERSION) && ./configure PREFIX=prefix )
	( cd .distcheck/sintl-$(VERSION) && $(MAKE) )
	( cd .distcheck/sintl-$(VERSION) && $(MAKE) regress )
	( cd .distcheck/sintl-$(VERSION) && $(MAKE) install )
	rm -rf .distcheck

distclean: clean
	rm -f Makefile.configure config.h config.log
