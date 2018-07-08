/*	$Id$ */
/*
 * Copyright (c) 2014, 2018 Kristaps Dzonsons <kristaps@bsd.lv>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */
#include "config.h"

#include <sys/mman.h>
#include <sys/stat.h>

#include <assert.h>
#include <ctype.h>
#if HAVE_ERR
# include <err.h>
#endif
#include <expat.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "extern.h"

/*
 * List of HTML5 elements that are self-closing, if empty.
 */
static	const char *elemvoid[] = {
	"area",
	"base",
	"br",
	"col",
	"command",
	"embed",
	"hr",
	"img",
	"input",
	"keygen",
	"link",
	"meta",
	"param",
	"source",
	"track",
	"wbr",
	NULL
};

/*
 * List of phrasing content elements that are accepted as
 * withinText="yes" by default.
 * See ITS v2.0 section 2.5.3.
 */
static	const char *phrasing[] = {
	"a",
	"abbr",
	"area",
	"audio",
	"b",
	"bdi",
	"bdo",
	"br",
	"button",
	"canvas",
	"cite",
	"code",
	"data",
	"datalist",
	"del",
	"dfn",
	"em",
	"embed",
	"i",
	/* "iframe", */
	"img",
	"input",
	"ins",
	"kbd",
	"keygen",
	"label",
	"map",
	"mark",
	"math",
	"meter",
	/* "noscript", */
	"object",
	"output",
	"progress",
	"q",
	"ruby",
	"s",
	"samp",
	/* "script", */
	/* "select", */
	"small",
	"span",
	"strong",
	"sub",
	"sup",
	"svg",
	/* "textarea", */
	"time",
	"u",
	"var",
	"video",
	"wbr",
	NULL
};

static void
lerr(const char *fn, XML_Parser p, const char *fmt, ...)
	__attribute__((format(printf, 3, 4)));

static void
xend(void *dat, const XML_Char *s);

static void
xstart(void *dat, const XML_Char *s, const XML_Char **atts);

static void
lerr(const char *fn, XML_Parser p, const char *fmt, ...)
{
	va_list	 ap;

	fprintf(stderr, "%s:%zu:%zu: ", fn, 
		XML_GetCurrentLineNumber(p),
		XML_GetCurrentColumnNumber(p));

	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
	va_end(ap);

	fputc('\n', stderr);
}

static void
perr(const char *fn, XML_Parser p)
{

	lerr(fn, p, "%s", XML_ErrorString(XML_GetErrorCode(p)));
}

/*
 * Look up a given element to see if it can be "void", i.e., close
 * itself out.
 * For example, <p> is not void; <link /> is.
 */
static int
xmlvoid(const XML_Char *s)
{
	const char	**cp;

	for (cp = (const char **)elemvoid; NULL != *cp; cp++)
		if (0 == strcasecmp(s, *cp))
			return(1);

	return(0);
}

static void
hparse_reset(struct hparse *hp)
{

	frag_node_free(hp->frag_root);
	hp->frag_root = hp->frag_current = NULL;
	while (hp->stacksz > 0)
		free(hp->stack[--hp->stacksz].name);
}

static struct hparse *
hparse_alloc(XML_Parser p, enum pop op)
{
	struct hparse	*hp;

	if (NULL == (hp = calloc(1, sizeof(struct hparse))))
		err(EXIT_FAILURE, NULL);

	hp->p = p;
	hp->op = op;
	return(hp);
}

static void
hparse_free(struct hparse *hp)
{
	size_t	 i;

	while (hp->stacksz > 0)
		free(hp->stack[--hp->stacksz].name);

	for (i = 0; i < hp->wordsz; i++)
		free(hp->words[i]);

	frag_node_free(hp->frag_root);
	free(hp->words);
	free(hp);
}

static struct xparse *
xparse_alloc(const char *xliff, XML_Parser p)
{
	struct xparse	*xp;

	if (NULL == (xp = calloc(1, sizeof(struct xparse))))
		err(EXIT_FAILURE, NULL);

	xp->fname = xliff;
	xp->p = p;
	return(xp);
}

static void
xparse_free(struct xparse *xp)
{
	size_t	 i;

	for (i = 0; i < xp->xliffsz; i++) {
		free(xp->xliffs[i].source);
		free(xp->xliffs[i].target);
	}

	frag_node_free(xp->frag_root);
	free(xp->target);
	free(xp->source);
	free(xp->xliffs);
	free(xp);
}

/*
 * We're scanning a document and want to store a source word that we'll
 * eventually be putting into a template XLIFF file.
 * Do so, being sensitive to the current space-preservation state.
 */
static int
store(struct hparse *p)
{
	char	*cp = NULL;

	assert(POP_EXTRACT == p->op);
	assert(NULL != p->frag_root);

	if (p->frag_root != p->frag_current) {
		lerr(p->fname, p->p, 
			"translation scope broken");
		XML_StopParser(p->p, 0);
		return 0;
	}

	cp = frag_serialise(p->frag_root, 
		p->stack[p->stacksz - 1].preserve);
	frag_node_free(p->frag_root);
	p->frag_root = p->frag_current = NULL;

	if (NULL == cp)
		return 1;

	/* Expand word list, if necessary. */

	if (p->wordsz + 1 > p->wordmax) {
		p->wordmax += 512;
		p->words = reallocarray
			(p->words, p->wordmax, sizeof(char *));
		if (NULL == p->words)
			err(EXIT_FAILURE, NULL);
	}

	p->words[p->wordsz++] = cp;
	return 1;
}

/*
 * We're translating a document.
 * Look up the word in our translation table.
 * If it exists, then emit it.
 * If it doesn't, then emit what already exists on the page.
 * If it's just white-space, then emit the white-space.
 */
static int
translate(struct hparse *hp)
{
	char	*cp;
	size_t	 i;

	assert(POP_JOIN == hp->op);
	assert(hp->stack[hp->stacksz - 1].translate);

	if (hp->frag_root != hp->frag_current) {
		lerr(hp->fname, hp->p, 
			"translation scope broken");
		XML_StopParser(hp->p, 0);
		return 0;
	}

	cp = frag_serialise(hp->frag_root, 
		hp->stack[hp->stacksz - 1].preserve);

	if (NULL == cp) {
		cp = frag_serialise(hp->frag_root, 1);
		if (NULL != cp)
			printf("%s", cp);
		free(cp);
		frag_node_free(hp->frag_root);
		hp->frag_root = hp->frag_current = NULL;
		return 1;
	}

	frag_node_free(hp->frag_root);
	hp->frag_root = hp->frag_current = NULL;

	for (i = 0; i < hp->xliffsz; i++)
		if (0 == strcmp(hp->xliffs[i].source, cp)) {
			printf("%s", hp->xliffs[i].target);
			free(cp);
			return 1;
		}

	lerr(hp->fname, hp->p, "no translation found");
	printf("%s", cp);
	free(cp);
	return 1;
}

static void
xtext(void *dat, const XML_Char *s, int len)
{
	struct xparse	*p = dat;

	frag_node_text(&p->frag_root, 
		&p->frag_current, s, len);
}

static void
xneststart(void *dat, const XML_Char *s, const XML_Char **atts)
{
	struct xparse	 *p = dat;

	/* FIXME */
	if (0 == strcmp(s, "target"))
		++p->nest;

	frag_node_start(&p->frag_root, &p->frag_current, s, atts);
}

static void
xnestend(void *dat, const XML_Char *s)
{
	struct xparse	*p = dat;
	const char	*rtype;

	rtype = NEST_TARGET == p->nesttype ? "target" : "source";

	if (strcmp(s, rtype) || --p->nest > 0) {
		frag_node_end(&p->frag_current, s);
		return;
	}

	XML_SetElementHandler(p->p, xstart, xend);
	XML_SetDefaultHandlerExpand(p->p, NULL);

	if (NEST_TARGET == p->nesttype) {
		free(p->target);
		p->target = frag_serialise(p->frag_root, 0);
		frag_node_free(p->frag_root);
		p->frag_root = p->frag_current = NULL;
		if (NULL == p->target)
			lerr(p->fname, p->p, "empty <target>");
	} else {
		free(p->source);
		p->source = frag_serialise(p->frag_root, 0);
		frag_node_free(p->frag_root);
		p->frag_root = p->frag_current = NULL;
		if (NULL == p->source)
			lerr(p->fname, p->p, "empty <source>");
	}
}

/*
 * Start an element in an XLIFF parse sequence.
 * We only care about <source>, <target>, and <segment>.
 * For the former two, start buffering for content.
 */
static void
xstart(void *dat, const XML_Char *s, const XML_Char **atts)
{
	struct xparse	 *p = dat;

	if (0 == strcmp(s, "source")) {
		p->nest = 1;
		p->nesttype = NEST_SOURCE;
		XML_SetDefaultHandlerExpand(p->p, xtext);
		XML_SetElementHandler(p->p, xneststart, xnestend);
	} else if (0 == strcmp(s, "target")) {
		p->nest = 1;
		p->nesttype = NEST_TARGET;
		XML_SetDefaultHandlerExpand(p->p, xtext);
		XML_SetElementHandler(p->p, xneststart, xnestend);
	} else if (0 == strcmp(s, "segment"))
		p->source = p->target = NULL;
}

/*
 * Finished a <source>, <target>, or <segment>.
 * For the former tow, we want to verify that we've buffered a real word
 * we're going to use.
 * For the latter, we want both terms in place.
 * We then add it to the dictionary of translations.
 */
static void
xend(void *dat, const XML_Char *s)
{
	struct xparse	*p = dat;

	XML_SetDefaultHandlerExpand(p->p, NULL);

	if (0 == strcmp(s, "segment")) {
		if (NULL == p->source || NULL == p->target) {
			lerr(p->fname, p->p, "no <source> or <target>");
			free(p->source);
			free(p->target);
			p->source = p->target = NULL;
			return;
		}
		if (p->xliffsz + 1 > p->xliffmax) {
			p->xliffmax += 512;
			p->xliffs = reallocarray
				(p->xliffs, 
				 p->xliffmax,
				 sizeof(struct xliff));
			if (NULL == p->xliffs)
				err(EXIT_FAILURE, NULL);
		}
		p->xliffs[p->xliffsz].source = p->source;
		p->xliffs[p->xliffsz].target = p->target;
		p->source = p->target = NULL;
		p->xliffsz++;
	}
}

/*
 * While scanning a document, process incoming text.
 * If we're currently looking to translate, then buffer the word for
 * later processing.
 * If we're not looking to translate (or we're just text without any
 * enclosing node?), then spit out the word if we're translating, else
 * just ignore the word if scanning.
 */
static void
htext(void *dat, const XML_Char *s, int len)
{
	struct hparse	*p = dat;
	
	if (0 == p->stacksz || 
	    0 == p->stack[p->stacksz - 1].translate) {
		if (POP_JOIN == p->op)
			printf("%.*s", len, s);
		return;
	}

	frag_node_text(&p->frag_root, 
		&p->frag_current, s, len);
}

/*
 * Start an element in a document we're translating or extracting.
 */
static void
hstart(void *dat, const XML_Char *s, const XML_Char **atts)
{
	struct hparse	 *p = dat;
	const XML_Char	**attp;
	int		  dotrans = 0, preserve = 0, phrase = 0;
	const char	**elems;

	/* Warn if we don't have the correct XML namespace. */

	if (0 == strcasecmp(s, "html")) {
		for (attp = atts; NULL != *attp; attp += 2) 
			if (0 == strcasecmp(attp[0], "xmlns:its"))
				break;
		if (NULL == *attp)
			lerr(p->fname, p->p, "missing xmlns:its");
	}

	/*
	 * If we're already in a translating context and we have one of
	 * the protected phrase elements, then append the element to our
	 * buffer and do no further processing.
	 */

	if (p->stacksz && p->stack[p->stacksz - 1].translate)
		for (elems = phrasing; NULL != *elems; elems++)
			if (0 == strcasecmp(s, *elems)) {
				phrase = 1;
				break;
			}

	if (phrase) {
		frag_node_start(&p->frag_root, 
			&p->frag_current, s, atts);
		if (0 == strcmp(s, p->stack[p->stacksz - 1].name))
			p->stack[p->stacksz - 1].nested++;
		return;
	}

	/* Store/translate any existing keywords. */

	if (NULL != p->frag_root && POP_JOIN == p->op) {
		if ( ! translate(p))
			return;
	} else if (NULL != p->frag_root) {
		if ( ! store(p))
			return;
	}

	/*
	 * If we're translating, then echo the tags.
	 * Make sure we accomodate for minimisations.
	 */

	if (POP_JOIN == p->op) {
		printf("<%s", s);
		for (attp = atts; NULL != *attp; attp += 2) 
			printf(" %s=\"%s\"", attp[0], attp[1]);
		if (xmlvoid(s))
			putchar('/');
		putchar('>');
	}

	/* Check if we should begin translating. */

	for (attp = atts; NULL != *attp; attp += 2)
		if (0 == strcmp(attp[0], "its:translate")) {
			if (0 == strcasecmp(attp[1], "yes"))
				dotrans = 1;
			else if (0 == strcasecmp(attp[1], "no"))
				dotrans = -1;
		} else if (0 == strcmp(attp[0], "xml:space")) {
			if (0 == strcasecmp(attp[1], "preserve"))
				preserve = 1;
			else if (0 == strcasecmp(attp[1], "default"))
				preserve = -1;
		}

	/* Default for whole document. */

	if (0 == p->stacksz && 0 == dotrans)
		dotrans = 1;
	if (0 == p->stacksz && 0 == preserve)
		preserve = -1;

	/*
	 * If we're not changing our translation context, see if we've
	 * entered a nested context and mark it, if so.
	 */

	if (0 == dotrans && 0 == preserve) {
		assert(p->stacksz > 0);
		if (0 == strcmp(s, p->stack[p->stacksz - 1].name))
			p->stack[p->stacksz - 1].nested++;
		return;
	}

	/* By default, inherit from parent. */

	if (0 == dotrans)
		dotrans = p->stack[p->stacksz - 1].translate;
	if (0 == preserve)
		preserve = p->stack[p->stacksz - 1].preserve;

	/*
	 * Create our new translation context.
	 * This can be either translating or not.
	 * TODO: if we're in a new translation context that's the same
	 * as the existing one, just increment our nestedness.
	 */

	if (NULL == (p->stack[p->stacksz].name = strdup(s)))
		err(EXIT_FAILURE, NULL);

	p->stack[p->stacksz].translate = 1 == dotrans;
	p->stack[p->stacksz].preserve = 1 == preserve;
	p->stack[p->stacksz++].nested = 0;
	if (p->stacksz == 64) {
		/* FIXME */
		fprintf(stderr, "%s: Stack size exceeded\n", p->fname);
		exit(EXIT_FAILURE);
	}
}

/*
 * End an element an input document slated for translation or
 * extraction.
 */
static void
hend(void *dat, const XML_Char *s)
{
	struct hparse	 *p = dat;
	const char	**elems;
	int 		  phrase = 0, end = 0;

	assert(p->stacksz > 0);

	/* Set if we're at the end of our current scope. */

	end = 0 == strcmp(p->stack[p->stacksz - 1].name, s) && 
		0 == p->stack[p->stacksz - 1].nested;

	/* Set if we're ending a phrasing element in a translation. */

	if (p->stack[p->stacksz - 1].translate)
		for (elems = phrasing; NULL != *elems; elems++)
			if (0 == strcasecmp(s, *elems)) {
				phrase = 1;
				break;
			}

	/*
	 * If we're not ending a scope and we have phrasing content in a
	 * translation, then put the element itself into the buffer.
	 */

	if (0 == end && phrase) {
		frag_node_end(&p->frag_current, s);
		if (0 == strcmp(p->stack[p->stacksz - 1].name, s))
			p->stack[p->stacksz - 1].nested--;
		return;
	}

	/* 
	 * We've ended an element that might have contained content that
	 * we want to translate.
	 * First, flush any existing translatable content.
	 */

	if (NULL != p->frag_root && POP_JOIN == p->op) {
		if ( ! translate(p))
			return;
	} else if (NULL != p->frag_root) {
		if ( ! store(p))
			return;
	}

	/* Echo if we're translating unless we've already closed. */

	if (POP_JOIN == p->op && ! xmlvoid(s))
		printf("</%s>", s);

	/* 
	 * Check if we're closing a translation context.
	 * If we are, decrement nesting.
	 * Otherwise, free the saved context name and pop context.
	 */

	if (strcmp(p->stack[p->stacksz - 1].name, s))
		return;
	if (0 == p->stack[p->stacksz - 1].nested)
		free(p->stack[--p->stacksz].name);
	else
		p->stack[p->stacksz - 1].nested--;
}

static int
map_open(const char *fn, size_t *mapsz, char **map)
{
	struct stat	 st;
	int	 	 fd;

	if (-1 == (fd = open(fn, O_RDONLY))) {
		perror(fn);
		return(-1);
	} else if (-1 == fstat(fd, &st)) {
		perror(fn);
		close(fd);
		return(-1);
	} else if ( ! S_ISREG(st.st_mode)) {
		fprintf(stderr, "%s: not regular\n", fn);
		close(fd);
		return(-1);
	} else if (st.st_size >= (1U << 31)) {
		fprintf(stderr, "%s: too large\n", fn);
		close(fd);
		return(-1);
	}

	*mapsz = st.st_size;
	*map = mmap(NULL, st.st_size, PROT_READ, MAP_SHARED, fd, 0);

	if (MAP_FAILED == *map) {
		perror(fn);
		close(fd);
		fd = -1;
	}

	return(fd);
}

static void
map_close(int fd, void *map, size_t mapsz)
{

	munmap(map, mapsz);
	close(fd);
}

/*
 * Given a file buffer and the file buffer size, invoke the XML parser
 * on the buffer for scanning.
 * Accomodate for NULL maps (read directly from stdin).
 */
static int
dofile(struct hparse *hp, const char *map, size_t mapsz)
{

	XML_ParserReset(hp->p, NULL);
	XML_SetDefaultHandlerExpand(hp->p, htext);
	XML_SetElementHandler(hp->p, hstart, hend);
	XML_SetUserData(hp->p, hp);

	if (NULL == map) {
		char b[4096];
		ssize_t sz;
		int rc = XML_STATUS_OK;

		do {
			sz = read(STDIN_FILENO, b, sizeof(b));
			if (sz < 0) {
				perror(hp->fname);
				break;
			}
			rc = XML_Parse(hp->p, b, sz, sz == 0);
		} while (XML_STATUS_OK == rc && sz > 0);

		if (XML_STATUS_OK != rc)
			perr(hp->fname, hp->p);
		if (XML_STATUS_OK == rc && 0 == sz)
			return(1);
	} else {
		if (XML_STATUS_OK == XML_Parse(hp->p, map, mapsz, 1))
			return(1);
		perr(hp->fname, hp->p);
	}

	return(0);
}

/*
 * Invoke the HTML5 parser on a series of files; or if no files are
 * specified, as read from standard input.
 */
static int
scanner(struct hparse *hp, int argc, char *argv[])
{
	int		 i, fd, rc;
	char		*map;
	size_t		 mapsz;

	if (0 == argc) {
		hp->fname = "<stdin>";
		return(dofile(hp, NULL, 0));
	}

	for (i = 0; i < argc; i++) {
		if (-1 == (fd = map_open(argv[i], &mapsz, &map)))
			break;
		hp->fname = argv[i];
		rc = dofile(hp, map, mapsz);
		map_close(fd, map, mapsz);
		hparse_reset(hp);
		if (0 == rc)
			break;
	}

	return(i == argc);
}

/*
 * Extract all translatable strings from argv and create an XLIFF file
 * template from the results.
 */
int
extract(XML_Parser p, int argc, char *argv[])
{
	struct hparse	*hp;
	int		 rc;

	hp = hparse_alloc(p, POP_EXTRACT);

	if (0 != (rc = scanner(hp, argc, argv)))
		results_extract(hp);

	hparse_free(hp);
	return(rc);
}

/*
 * Translate the files in argv with the dictionary in xliff, echoing the
 * translated versions.
 */
int
join(const char *xliff, XML_Parser p, int argc, char *argv[])
{
	struct xparse	*xp;
	struct hparse	*hp;
	char		*map;
	size_t		 mapsz;
	int		 fd, rc;

	if (-1 == (fd = map_open(xliff, &mapsz, &map)))
		return(0);

	xp = xparse_alloc(xliff, p);

	XML_ParserReset(p, NULL);
	XML_SetDefaultHandlerExpand(p, NULL);
	XML_SetElementHandler(p, xstart, xend);
	XML_SetUserData(p, xp);
	rc = XML_Parse(p, map, mapsz, 1);
	map_close(fd, map, mapsz);

	if (XML_STATUS_OK == rc) {
		hp = hparse_alloc(p, POP_JOIN);
		hp->xliffs = xp->xliffs;
		hp->xliffsz = xp->xliffsz;
		rc = scanner(hp, argc, argv);
		assert(NULL == hp->words);
		hparse_free(hp);
	} else
		perr(xliff, p);

	xparse_free(xp);
	return(XML_STATUS_OK == rc);
}

/*
 * Update (not in-line) the dictionary file xliff with the contents of
 * argv, outputting the merged XLIFF file.
 */
int
update(const char *xliff, XML_Parser p, int argc, char *argv[])
{
	struct xparse	*xp;
	struct hparse	*hp;
	char		*map;
	size_t		 mapsz;
	int		 fd, rc;

	if (-1 == (fd = map_open(xliff, &mapsz, &map)))
		return(0);

	xp = xparse_alloc(xliff, p);

	XML_ParserReset(p, NULL);
	XML_SetDefaultHandlerExpand(p, NULL);
	XML_SetElementHandler(p, xstart, xend);
	XML_SetUserData(p, xp);
	rc = XML_Parse(p, map, mapsz, 1);

	map_close(fd, map, mapsz);

	if (XML_STATUS_OK == rc) {
		hp = hparse_alloc(p, POP_EXTRACT);
		hp->xliffs = xp->xliffs;
		hp->xliffsz = xp->xliffsz;
		if (0 != (rc = scanner(hp, argc, argv)))
			results_update(hp);
		hparse_free(hp);
	} else
		perr(xliff, p);

	xparse_free(xp);
	return(XML_STATUS_OK == rc);
}
