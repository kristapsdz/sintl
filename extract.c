/*	$Id$ */
/*
 * Copyright (c) 2014 Kristaps Dzonsons <kristaps@bsd.lv>
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

	hp->identsz = 0;
	while (hp->stacksz > 0)
		free(hp->stack[--hp->stacksz].name);
}

static struct hparse *
hparse_alloc(XML_Parser p, enum pop op)
{
	struct hparse	*hp;

	if (NULL == (hp = calloc(1, sizeof(struct hparse)))) {
		perror(NULL);
		exit(EXIT_FAILURE);
	}

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
	free(hp->words);
	free(hp->ident);
	free(hp);
}

static struct xparse *
xparse_alloc(const char *xliff, XML_Parser p)
{
	struct xparse	*xp;

	if (NULL == (xp = calloc(1, sizeof(struct xparse)))) {
		perror(NULL);
		exit(EXIT_FAILURE);
	}

	xp->fname = xliff;
	xp->p = p;
	return(xp);
}

static void
xparse_free(struct xparse *xp)
{
	size_t	 i;

	free(xp->target);
	free(xp->source);
	for (i = 0; i < xp->xliffsz; i++) {
		free(xp->xliffs[i].source);
		free(xp->xliffs[i].target);
	}
	free(xp->xliffs);
	free(xp->ident);
	free(xp);
}

/*
 * Strip the nil-terminated character string "cp" of length "sz" of its
 * leading and trailing whitespace.
 * Return NULL if there's nothing left when we're done.
 */
static char *
strip(char *cp, size_t sz)
{
	char	*start, *end;

	start = cp;
	while (isspace(*start)) {
		start++;
		sz--;
	}

	if (0 == sz)
		return(NULL);

	end = start + sz - 1;
	while (end > start && isspace(*end))
		*end-- = '\0';

	assert(end > start);
	return(start);
}

/*
 * We're scanning a document and want to cache a source word that we'll
 * eventually be putting into a template XLIFF file.
 * Do so, being sensitive to the current space-preservation state.
 */
static void
cache(struct hparse *p)
{
	char	*cp;

	assert(POP_EXTRACT == p->op);
	assert(p->identsz > 0);

	/* Expand word list, if necessary. */
	if (p->wordsz + 1 > p->wordmax) {
		p->wordmax += 512;
		p->words = reallocarray
			(p->words, p->wordmax, sizeof(char *));
		if (NULL == p->words) {
			perror(NULL);
			exit(EXIT_FAILURE);
		}
	}

	/* 
	 * If we're not preserving space, then strip the spaces out and
	 * store them.
	 * Otherwise, use the original identifier verbatim.
	 */
	cp = (0 == p->stack[p->stacksz - 1].preserve) ?
		strip(p->ident, p->identsz) : p->ident;
	p->identsz = 0;

	/* Ignore completely-empty identifier. */
	if (NULL == cp)
		return;

	p->words[p->wordsz] = strdup(cp);
	if (NULL == p->words[p->wordsz]) {
		perror(NULL);
		exit(EXIT_FAILURE);
	}
	p->wordsz++;
}

/*
 * We're translating a document.
 * Look up the word in our translation table.
 * If it exists, then emit it.
 * If it doesn't, then emit what already exists on the page.
 */
static void
translate(struct hparse *hp)
{
	char	*cp;
	size_t	 i;

	assert(POP_JOIN == hp->op);
	assert(hp->stack[hp->stacksz - 1].translate);

	cp = (0 == hp->stack[hp->stacksz - 1].preserve) ?
		strip(hp->ident, hp->identsz) : hp->ident;
	hp->identsz = 0;

	if (NULL == cp)
		return;
	for (i = 0; i < hp->xliffsz; i++)
		if (0 == strcmp(hp->xliffs[i].source, cp)) {
			printf("%s", hp->xliffs[i].target);
			return;
		}

	lerr(hp->fname, hp->p, "No translation found");
	printf("%s", hp->ident);
}

static void
append(struct xparse *p, const XML_Char *s, int len)
{

	if (p->identsz + len + 1 > p->identmax) {
		p->identmax = p->identsz + len + 1;
		p->ident = realloc(p->ident, p->identmax);
		if (NULL == p->ident) {
			perror(NULL);
			exit(EXIT_FAILURE);
		}
	}

	memcpy(p->ident + p->identsz, s, len);
	p->identsz += len;
	p->ident[p->identsz] = '\0';
}

/*
 * Append an XLIFF word fragment into the nil-terminated ident buffer
 * for later processing.
 */
static void
xtext(void *dat, const XML_Char *s, int len)
{

	append(dat, s, len);
}

static void
xneststart(void *dat, const XML_Char *s, const XML_Char **atts)
{
	struct xparse	 *p = dat;
	const XML_Char	**attp;

	if (0 == strcmp(s, "target"))
		++p->nest;

	append(dat, "<", 1);
	append(dat, s, strlen(s));
	for (attp = atts; NULL != *attp; attp += 2) {
		append(dat, " ", 1);
		append(dat, attp[0], strlen(attp[0]));
		append(dat, "=\"", 2);
		append(dat, attp[1], strlen(attp[1]));
		append(dat, "\"", 1);
	}
	append(dat, ">", 1);

}

static void
xnestend(void *dat, const XML_Char *s)
{
	struct xparse	*p = dat;
	char		*cp;

	if (strcmp(s, "target") || --p->nest > 0) {
		append(dat, "</", 2);
		append(dat, s, strlen(s));
		append(dat, ">", 1);
		return;
	}

	XML_SetElementHandler(p->p, xstart, xend);
	if (NULL == p->source) {
		lerr(p->fname, p->p, "No target source");
		return;
	}
	free(p->target);
	p->target = NULL;
	if (0 == p->identsz) {
		lerr(p->fname, p->p, "Empty target element");
		return;
	}
	cp = strip(p->ident, p->identsz);
	p->identsz = 0;
	if (NULL == cp) {
		lerr(p->fname, p->p, "Empty target element");
		return;
	}
	p->target = strdup(cp);
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

	if (0 == strcmp(s, "source"))
		XML_SetDefaultHandlerExpand(p->p, xtext);
	else if (0 == strcmp(s, "target")) {
		/* Handle nesting. */
		p->nest = 1;
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
	char		*cp;

	if (0 == strcmp(s, "source")) {
		free(p->source);
		p->source = NULL;
		if (0 == p->identsz) {
			lerr(p->fname, p->p, "Empty source element");
			return;
		} 
		cp = strip(p->ident, p->identsz);
		p->identsz = 0;
		if (NULL == cp) {
			lerr(p->fname, p->p, "Empty source element");
			return;
		}
		p->source = strdup(cp);
	} else if (0 == strcmp(s, "segment")) {
		if (NULL == p->source || NULL == p->target) {
			lerr(p->fname, p->p, "No source or target");
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
			if (NULL == p->xliffs) {
				perror(NULL);
				exit(EXIT_FAILURE);
			}
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

	if (p->identsz + len + 1 > p->identmax) {
		p->identmax = p->identsz + len + 1;
		p->ident = realloc(p->ident, p->identmax);
		if (NULL == p->ident) {
			perror(NULL);
			exit(EXIT_FAILURE);
		}
	}

	memcpy(p->ident + p->identsz, s, len);
	p->identsz += len;
	p->ident[p->identsz] = '\0';
}

/*
 * Start an element that's used in document we're translating or
 * scanning for translations.
 */
static void
hstart(void *dat, const XML_Char *s, const XML_Char **atts)
{
	struct hparse	 *p = dat;
	const XML_Char	**attp;
	int		  nt, pres;

	/* Cache/translate any existing keywords. */
	if (p->identsz > 0 && POP_JOIN == p->op)
		translate(p);
	else if (p->identsz > 0)
		cache(p);

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
	for (nt = pres = 0, attp = atts; NULL != *attp; attp += 2)
		if (0 == strcmp(attp[0], "its:translate")) {
			if (0 == strcasecmp(attp[1], "yes"))
				nt = 1;
			else if (0 == strcasecmp(attp[1], "no"))
				nt = -1;
		} else if (0 == strcmp(attp[0], "xml:space")) {
			if (0 == strcasecmp(attp[1], "preserve"))
				pres = 1;
			else if (0 == strcasecmp(attp[1], "default"))
				pres = -1;
		}

	/* Default for whole document. */
	if (0 == p->stacksz && 0 == nt)
		nt = -1;
	if (0 == p->stacksz && 0 == pres)
		pres = -1;

	/*
	 * If we're not changing our translation context, see if we've
	 * entered a nested context and mark it, if so.
	 */
	if (0 == nt && 0 == pres) {
		assert(p->stacksz > 0);
		if (0 == strcmp(s, p->stack[p->stacksz - 1].name))
			p->stack[p->stacksz - 1].nested++;
		return;
	}

	/* By default, inherit from parent. */
	if (0 == nt)
		nt = p->stack[p->stacksz - 1].translate;
	if (0 == pres)
		pres = p->stack[p->stacksz - 1].preserve;

	/*
	 * Create our new translation context.
	 * This can be either translating or not.
	 * TODO: if we're in a new translation context that's the same
	 * as the existing one, just increment our nestedness.
	 */
	if (NULL == (p->stack[p->stacksz].name = strdup(s))) {
		perror(NULL);
		exit(EXIT_FAILURE);
	}

	p->stack[p->stacksz].translate = 1 == nt;
	p->stack[p->stacksz].preserve = 1 == pres;
	p->stack[p->stacksz++].nested = 0;
	if (p->stacksz == 64) {
		/* FIXME */
		fprintf(stderr, "%s: Stack size exceeded\n", p->fname);
		exit(EXIT_FAILURE);
	}
}

/*
 * End an element for a document we're either translating or are
 * scanning for translatable parts.
 */
static void
hend(void *dat, const XML_Char *s)
{
	struct hparse	*p = dat;

	/* Flush any existing keywords. */
	if (p->identsz > 0 && POP_JOIN == p->op)
		translate(p);
	else if (p->identsz > 0)
		cache(p);

	/* Echo if we're translating, unless we've already closed. */
	if (POP_JOIN == p->op && ! xmlvoid(s))
		printf("</%s>", s);

	/* Check if we're closing a translation context. */
	assert(p->stacksz > 0);
	if (strcmp(p->stack[p->stacksz - 1].name, s))
		return;

	/* 
	 * If we're in a nested translation context, just decrement the
	 * nesting size.
	 * Otherwise, free the saved context name and decrement to the
	 * prior translation context.
	 */
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
