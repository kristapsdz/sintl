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
#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "extern.h"

/*
 * A translation context (do or don't).
 * These begin with an element ("name") and may be nested with
 * similarly-named elements.
 */
struct	stack {
	char	 	*name; /* element name of context */
	size_t	 	 nested; /* nested same-name elements */
	int		 translate; /* translate or not */
	int		 preserve; /* preserve whitespace */
};

/*
 * A key-value pair for translation.
 */
struct	xliff {
	char		 *source; /* key */
	char		 *target; /* value */
};

/*
 * Parse tracker for a document that's either going to be translated or
 * scanned for translatable parts.
 * These switch on whether the "xliffs" argument is NULL.
 * If it is NULL, then we're looking for stuff to translate.
 * Otherwise, we fill in with the translation.
 */
struct	hparse {
	XML_Parser	  p;
	const char	 *fname; /* file being parsed */
	char		**words; /* if scanning, scanned words */
	size_t		  wordsz; /* number of words */
	size_t		  wordmax; /* word buffer size */
	char	 	 *ident; /* currently-scanning word */
	size_t		  identsz; /* length of current word */
	size_t		  identmax; /* maximum word buffer */
	struct stack	  stack[64]; /* stack of contexts */
	size_t		  stacksz; /* stack size */
	struct xliff	 *xliffs; /* translation parts */
	size_t		  xliffsz; /* number of translation patrs */
};

/*
 * Parse tracker for an XLIFF file that we're parsing.
 * This will be passed in (assuming success) to the "struct hparse" as
 * the xliffs pointer.
 */
struct	xparse {
	XML_Parser	  p;
	const char	 *fname; /* xliff filename */
	struct xliff	 *xliffs; /* current xliffs */
	size_t		  xliffsz; /* current size of xliffs */
	size_t		  xliffmax; /* xliff buffer size */
	char		 *ident; /* currently-scanning word */
	size_t		  identsz; /* length of current word */
	size_t		  identmax; /* maximum word buffer */
	char		 *source; /* current source in segment */
	char		 *target; /* current target in segment */
};

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

	assert(NULL == p->xliffs);
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
translate(struct hparse *p)
{
	char	*cp;
	size_t	 i;

	assert(NULL != p->xliffs);
	assert(p->stack[p->stacksz - 1].translate);

	cp = (0 == p->stack[p->stacksz - 1].preserve) ?
		strip(p->ident, p->identsz) : p->ident;
	p->identsz = 0;

	if (NULL == cp)
		return;
	for (i = 0; i < p->xliffsz; i++)
		if (0 == strcmp(p->xliffs[i].source, cp)) {
			printf("%s", p->xliffs[i].target);
			return;
		}

	printf("%s", p->ident);
}

/*
 * Append an XLIFF word fragment into the nil-terminated ident buffer
 * for later processing.
 */
static void
xtext(void *dat, const XML_Char *s, int len)
{
	struct hparse	*p = dat;

	if (p->identsz + len > p->identmax) {
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
 * Start an element in an XLIFF parse sequence.
 * We only care about <source>, <target>, and <segment>.
 * For the former two, start buffering for content.
 */
static void
xstart(void *dat, const XML_Char *s, const XML_Char **atts)
{
	struct xparse	 *p = dat;

	/* FIXME: verify no nesting. */
	if (0 == strcmp(s, "source"))
		XML_SetDefaultHandlerExpand(p->p, xtext);
	else if (0 == strcmp(s, "target"))
		XML_SetDefaultHandlerExpand(p->p, xtext);
	else if (0 == strcmp(s, "segment"))
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
			fprintf(stderr, "%s:%zu:%zu: Empty "
				"source directive\n", p->fname,
				XML_GetCurrentLineNumber(p->p),
				XML_GetCurrentColumnNumber(p->p));
			return;
		} 
		cp = strip(p->ident, p->identsz);
		p->identsz = 0;
		if (NULL == cp) {
			fprintf(stderr, "%s:%zu:%zu: Empty "
				"source directive\n", p->fname,
				XML_GetCurrentLineNumber(p->p),
				XML_GetCurrentColumnNumber(p->p));
			return;
		}
		p->source = strdup(cp);
	} else if (0 == strcmp(s, "target")) {
		if (NULL == p->source) {
			fprintf(stderr, "%s:%zu:%zu: No "
				"source for target\n", p->fname,
				XML_GetCurrentLineNumber(p->p),
				XML_GetCurrentColumnNumber(p->p));
			return;
		}
		free(p->target);
		p->target = NULL;
		if (0 == p->identsz) {
			fprintf(stderr, "%s:%zu:%zu: Empty "
				"target directive\n", p->fname,
				XML_GetCurrentLineNumber(p->p),
				XML_GetCurrentColumnNumber(p->p));
			return;
		}
		cp = strip(p->ident, p->identsz);
		p->identsz = 0;
		if (NULL == cp) {
			fprintf(stderr, "%s:%zu:%zu: Empty "
				"target directive\n", p->fname,
				XML_GetCurrentLineNumber(p->p),
				XML_GetCurrentColumnNumber(p->p));
			return;
		}
		p->target = strdup(cp);
	} else if (0 == strcmp(s, "segment")) {
		if (NULL == p->source || NULL == p->target) {
			fprintf(stderr, "%s:%zu:%zu: Missing "
				"source or target\n", p->fname,
				XML_GetCurrentLineNumber(p->p),
				XML_GetCurrentColumnNumber(p->p));
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
		if (NULL != p->xliffs)
			printf("%.*s", len, s);
		return;
	}

	if (p->identsz + len > p->identmax) {
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
	int		  nt;

	/* Cache/translate any existing keywords. */
	if (p->identsz > 0 && NULL != p->xliffs)
		translate(p);
	else if (p->identsz > 0)
		cache(p);

	/*
	 * If we're translating, then echo the tags.
	 * Make sure we accomodate for minimisations.
	 */
	if (NULL != p->xliffs) {
		printf("<%s", s);
		for (attp = atts; NULL != *attp; attp += 2) 
			printf(" %s=\"%s\"", attp[0], attp[1]);
		if (xmlvoid(s))
			putchar('/');
		putchar('>');
	}

	/* Check if we should begin translating. */
	for (nt = 0, attp = atts; NULL != *attp; attp += 2)
		if (0 == strcmp(attp[0], "its:translate")) {
			if (0 == strcasecmp(attp[1], "yes"))
				nt = 1;
			else if (0 == strcasecmp(attp[1], "no"))
				nt = -1;
		}

	/* Default for whole document. */
	if (0 == p->stacksz && 0 == nt)
		nt = -1;

	/*
	 * If we're not changing our translation context, see if we've
	 * entered a nested context and mark it, if so.
	 */
	if (0 == nt) {
		assert(p->stacksz > 0);
		if (0 == strcmp(s, p->stack[p->stacksz - 1].name))
			p->stack[p->stacksz - 1].nested++;
		return;
	}

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
	if (p->identsz > 0 && NULL != p->xliffs)
		translate(p);
	else if (p->identsz > 0)
		cache(p);

	/* Echo if we're translating, unless we've already closed. */
	if (NULL != p->xliffs && ! xmlvoid(s))
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
dofile(struct hparse *p, const char *map, 
	size_t mapsz, const char *fname)
{

	XML_ParserReset(p->p, NULL);
	XML_SetDefaultHandlerExpand(p->p, htext);
	XML_SetElementHandler(p->p, hstart, hend);
	XML_SetUserData(p->p, p);

	if (XML_STATUS_OK == XML_Parse(p->p, map, mapsz, 1))
		return(1);

	fprintf(stderr, "%s:%zu:%zu: %s\n", 
		fname, 
		XML_GetCurrentLineNumber(p->p),
		XML_GetCurrentColumnNumber(p->p),
		XML_ErrorString(XML_GetErrorCode(p->p)));
	return(0);
}

static int
cmp(const void *p1, const void *p2)
{

	return(strcmp(*(const char **)p1, *(const char **)p2));
}

static void
results(struct hparse *p)
{
	size_t	 i;

	qsort(p->words, p->wordsz, sizeof(char *), cmp);

	puts("<xliff xmlns=\"urn:oasis:names:tc:xliff:document:2.0\" "
		"version=\"2.0\" srcLang=\"TODO\" trgLang=\"TODO\">");
	puts("  <file id=\"file1\">");
	puts("    <unit id=\"unit1\">");
	for (i = 0; i < p->wordsz; i++) {
		if (i && 0 == strcmp(p->words[i], p->words[i - 1]))
			continue;
		puts("      <segment>");
		printf("        <source>%s</source>\n", p->words[i]);
		puts("        <target>TODO</target>");
		puts("      </segment>");
	}
	puts("    </unit>");
	puts("  </file>");
	puts("</xliff>");
}

static int
map_open(const char *fn, size_t *mapsz, void **map)
{
	struct stat	 st;
	int	 	 fd;

	if (-1 == (fd = open(fn, O_RDONLY, 0))) {
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
	*map = mmap(NULL, *mapsz, PROT_READ, 
		MAP_FILE | MAP_SHARED, fd, 0);

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

static int
scanner(struct hparse *parse, int argc, char *argv[])
{
	int		 i, fd, rc;
	char		*map;
	size_t		 mapsz;

	for (i = 0; i < argc; i++) {
		fd = map_open(argv[i], &mapsz, (void **)&map);
		if (-1 == fd)
			break;

		parse->fname = argv[i];
		rc = dofile(parse, map, mapsz, argv[i]);
		map_close(fd, map, mapsz);

		while (parse->stacksz > 0)
			free(parse->stack[--parse->stacksz].name);

		if (0 == rc)
			break;
	}

	return(i == argc);
}

int
extract(XML_Parser p, int argc, char *argv[])
{
	struct hparse	*parse;
	int		 rc;
	size_t		 i;

	parse = calloc(1, sizeof(struct hparse));
	if (NULL == parse) {
		perror(NULL);
		exit(EXIT_FAILURE);
	}

	parse->p = p;
	if (0 != (rc = scanner(parse, argc, argv)))
		results(parse);

	for (i = 0; i < parse->wordsz; i++)
		free(parse->words[i]);
	free(parse->words);
	free(parse->ident);
	free(parse);
	return(rc);
}

int
join(const char *xliff, XML_Parser p, int argc, char *argv[])
{
	struct xparse	*xp;
	struct hparse	*hp;
	char		*map;
	size_t		 i, mapsz;
	int		 fd, rc;

	xp = calloc(1, sizeof(struct xparse));
	if (NULL == xp) {
		perror(NULL);
		exit(EXIT_FAILURE);
	}

	fd = map_open(xliff, &mapsz, (void **)&map);
	if (-1 == fd) {
		free(xp);
		return(0);
	}

	xp->p = p;
	xp->fname = xliff;

	XML_ParserReset(p, NULL);
	XML_SetDefaultHandlerExpand(p, NULL);
	XML_SetElementHandler(p, xstart, xend);
	XML_SetUserData(p, xp);
	rc = XML_Parse(p, map, mapsz, 1);
	map_close(fd, map, mapsz);

	if (XML_STATUS_OK != rc) {
		fprintf(stderr, "%s:%zu:%zu: %s\n", 
			xliff, 
			XML_GetCurrentLineNumber(p),
			XML_GetCurrentColumnNumber(p),
			XML_ErrorString(XML_GetErrorCode(p)));
		rc = 0;
	} else if (0 == xp->xliffsz) {
		fprintf(stderr, "%s: No entries\n", xliff);
		rc = 0;
	} else
		rc = 1;	

	if (rc > 0) {
		hp = calloc(1, sizeof(struct hparse));
		if (NULL == hp) {
			perror(NULL);
			exit(EXIT_FAILURE);
		}
		hp->p = p;
		hp->xliffs = xp->xliffs;
		hp->xliffsz = xp->xliffsz;
		rc = scanner(hp, argc, argv);
		assert(NULL == hp->words);
		free(hp->ident);
		free(hp);
	}

	free(xp->target);
	free(xp->source);
	for (i = 0; i < xp->xliffsz; i++) {
		free(xp->xliffs[i].source);
		free(xp->xliffs[i].target);
	}
	free(xp->xliffs);
	free(xp->ident);
	free(xp);
	return(rc);
}
