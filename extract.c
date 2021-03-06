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
	"meta", /* FIXME: only if itemprop */
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

	fragseq_clear(&hp->frag);
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
		free(hp->words[i].source);

	fragseq_clear(&hp->frag);
	free(hp->words);
	free(hp->lang);
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
		fragseq_clear(&xp->xliffs[i].target);
	}

	fragseq_clear(&xp->frag);
	fragseq_clear(&xp->target);
	free(xp->source);
	free(xp->xliffs);
	free(xp->srclang);
	free(xp->trglang);
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
	int	 reduce = 0;

	assert(POP_EXTRACT == p->op);
	assert(NULL != p->frag.root);

	if (p->frag.root != p->frag.cur) {
		lerr(p->fname, p->p, 
			"translation scope broken");
		XML_StopParser(p->p, 0);
		return 0;
	}

	cp = frag_serialise(&p->frag, 1, &reduce);
	fragseq_clear(&p->frag);

	if (NULL == cp)
		return 1;

	/* Expand word list, if necessary. */

	if (p->wordsz + 1 > p->wordmax) {
		p->wordmax += 512;
		p->words = reallocarray
			(p->words, p->wordmax, sizeof(struct word));
		if (NULL == p->words)
			err(EXIT_FAILURE, NULL);
	}

	p->words[p->wordsz].source = cp;
	p->words[p->wordsz].line = 
		XML_GetCurrentLineNumber(p->p);
	p->words[p->wordsz].col = 
		XML_GetCurrentColumnNumber(p->p);
	p->wordsz++;
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
	int	 reduce = 0, rc = 1;

	assert(POP_JOIN == hp->op);
	assert(hp->stack[hp->stacksz - 1].translate);

	if (hp->frag.root != hp->frag.cur) {
		lerr(hp->fname, hp->p, 
			"translation scope broken");
		XML_StopParser(hp->p, 0);
		return 0;
	}

	cp = frag_serialise(&hp->frag, 1, &reduce);

	if (NULL == cp) {
		if (NULL != hp->frag.copy)
			printf("%.*s", 
				(int)hp->frag.copysz, 
				hp->frag.copy);
		fragseq_clear(&hp->frag);
		return 1;
	}

	for (i = 0; i < hp->xp->xliffsz; i++)
		if (0 == strcmp(hp->xp->xliffs[i].source, cp)) {
			if (reduce)
				frag_print_merge(&hp->frag,
					hp->xp->xliffs[i].source,
					&hp->xp->xliffs[i].target);
			else
				frag_print_merge(&hp->frag, NULL,
					&hp->xp->xliffs[i].target);
			free(cp);
			fragseq_clear(&hp->frag);
			return 1;
		}

	lerr(hp->fname, hp->p, "no translation found");

	if ( ! hp->copy) {
		rc = 0;
		XML_StopParser(hp->p, 0);
	} else
		printf("%s", cp);

	free(cp);
	fragseq_clear(&hp->frag);
	return rc;
}

static void
xtext(void *dat, const XML_Char *s, int len)
{
	struct xparse	*p = dat;

	if (NULL != p->frag.cur &&
	    FRAG_NODE == p->frag.cur->type &&
	    p->frag.cur->is_null) {
		lerr(p->fname, p->p, "content "
			"within null element");
		XML_StopParser(p->p, 0);
		return;
	}

	assert(len >= 0);
	frag_node_text(&p->frag, s, (size_t)len, 1);
}

static void
xneststart(void *dat, const XML_Char *s, const XML_Char **atts)
{
	struct xparse	*p = dat;
	const char	*rtype;

	rtype = NEST_TARGET == p->nesttype ? "target" : "source";

	/* This is an XML file, so it's case sensitive. */

	if (0 == strcmp(s, rtype))
		++p->nest;

	if (NULL != p->frag.cur &&
	    FRAG_NODE == p->frag.cur->type &&
	    p->frag.cur->is_null) {
		lerr(p->fname, p->p, "content "
			"within null element");
		XML_StopParser(p->p, 0);
		return;
	}

	frag_node_start(&p->frag, s, atts, 0 == strcmp("x", s));
}

static void
xnestend(void *dat, const XML_Char *s)
{
	struct xparse	 *p = dat;
	const char	 *rtype;
	XML_ParsingStatus st;

	XML_GetParsingStatus(p->p, &st);
	if (XML_FINISHED == st.parsing) 
		return;

	rtype = NEST_TARGET == p->nesttype ? "target" : "source";

	/* This is an XML file, so it's case sensitive. */

	if (strcmp(s, rtype) || --p->nest > 0) {
		frag_node_end(&p->frag, s);
		return;
	}

	XML_SetElementHandler(p->p, xstart, xend);
	XML_SetDefaultHandlerExpand(p->p, NULL);

	if (NEST_TARGET == p->nesttype) {
		fragseq_clear(&p->target);
		p->target = p->frag;
		memset(&p->frag, 0, sizeof(struct fragseq));
		if (NULL == p->target.root)
			lerr(p->fname, p->p, "empty <target>");
	} else {
		free(p->source);
		p->source = strndup(p->frag.copy, p->frag.copysz);
		if (NULL == p->source)
			err(EXIT_FAILURE, NULL);
		fragseq_clear(&p->frag);
		if (NULL == p->source)
			lerr(p->fname, p->p, "empty <source>");
	}
}

static void
xstart(void *dat, const XML_Char *s, const XML_Char **atts)
{
	struct xparse	 *p = dat;
	const XML_Char	**attp;
	const char	 *ver;

	if (0 == strcmp(s, "xliff")) {
		ver = NULL;
		for (attp = atts; NULL != *attp; attp += 2) 
			if (0 == strcmp(attp[0], "version"))
				ver = attp[1];
		if (NULL == ver) {
			lerr(p->fname, p->p, "<xliff> without version");
			XML_StopParser(p->p, 0);
			return;
		} else if (strcmp(ver, "1.2")) {
			lerr(p->fname, p->p, "<xliff> version must be 1.2");
			XML_StopParser(p->p, 0);
			return;
		}
	} else if (0 == strcmp(s, "file")) {
		if (NULL != p->srclang || NULL != p->trglang) {
			lerr(p->fname, p->p, "<file> already invoked");
			XML_StopParser(p->p, 0);
			return;
		}
		for (attp = atts; NULL != *attp; attp += 2)
			if (0 == strcmp(attp[0], "source-language")) {
				free(p->srclang);
				p->srclang = strdup(attp[1]);
				if (NULL == p->srclang)
					err(EXIT_FAILURE, NULL);
			} else if (0 == strcmp(attp[0], "target-language")) {
				free(p->trglang);
				p->trglang = strdup(attp[1]);
				if (NULL == p->trglang)
					err(EXIT_FAILURE, NULL);
			}
	} else if (0 == strcmp(s, "source")) {
		p->nest = 1;
		p->nesttype = NEST_SOURCE;
		XML_SetDefaultHandlerExpand(p->p, xtext);
		XML_SetElementHandler(p->p, xneststart, xnestend);
	} else if (0 == strcmp(s, "target")) {
		p->nest = 1;
		p->nesttype = NEST_TARGET;
		XML_SetDefaultHandlerExpand(p->p, xtext);
		XML_SetElementHandler(p->p, xneststart, xnestend);
	} 
}

static void
xend(void *dat, const XML_Char *s)
{
	struct xparse	 *p = dat;
	XML_ParsingStatus st;

	XML_GetParsingStatus(p->p, &st);
	if (XML_FINISHED == st.parsing) 
		return;

	XML_SetDefaultHandlerExpand(p->p, NULL);

	if (0 == strcmp(s, "trans-unit")) {
		if (NULL == p->source || 
		    NULL == p->target.root) {
			lerr(p->fname, p->p, "no <source> or <target>");
			fragseq_clear(&p->target);
			free(p->source);
			p->source = NULL;
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
		p->xliffs[p->xliffsz].line = 
			XML_GetCurrentLineNumber(p->p);
		p->xliffs[p->xliffsz].col = 
			XML_GetCurrentColumnNumber(p->p);
		p->xliffs[p->xliffsz].source = p->source;
		p->xliffs[p->xliffsz].target = p->target;
		p->source = NULL;
		memset(&p->target, 0, sizeof(struct fragseq));
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

	if (NULL != p->frag.cur &&
	    FRAG_NODE == p->frag.cur->type &&
	    p->frag.cur->is_null) {
		lerr(p->fname, p->p, "content in null element");
		XML_StopParser(p->p, 0);
		return;
	}
	
	if (0 == p->stacksz || 
	    0 == p->stack[p->stacksz - 1].translate) {
		if (POP_JOIN == p->op)
			printf("%.*s", len, s);
		return;
	}

	assert(len >= 0);
	frag_node_text(&p->frag, s, (size_t)len,
		p->stack[p->stacksz - 1].preserve);
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
	const char	 *its = NULL;

	if (0 == strcasecmp(s, "xliff")) {
		lerr(p->fname, p->p, 
			"encountered <xliff>: did you "
			"pass the wrong file?");
		XML_StopParser(p->p, 0);
		return;
	}

	/* 
	 * Warn if we don't have the correct XML namespace.
	 * FIXME: we should really only start translating within the
	 * declared ITS namespace, not for the whole document.
	 */

	if (0 == strcasecmp(s, "html")) {
		free(p->lang);
		p->lang = NULL;
		for (attp = atts; NULL != *attp; attp += 2) 
			if (0 == strcasecmp(attp[0], "xmlns:its")) {
				its = attp[1];
			} else if (0 == strcasecmp(attp[0], "lang")) {
				free(p->lang);
				p->lang = strdup(attp[1]);
				if (NULL == p->lang)
					err(EXIT_FAILURE, NULL);
			}
		if (NULL == its)
			lerr(p->fname, p->p, 
				"missing <html> xmlns:its");
		if (NULL == p->lang)
			lerr(p->fname, p->p, 
				"missing <html> language");
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
		if (NULL != p->frag.cur &&
		    FRAG_NODE == p->frag.cur->type &&
		    p->frag.cur->is_null) {
			lerr(p->fname, p->p, "content "
				"within null element");
			XML_StopParser(p->p, 0);
			return;
		}
		frag_node_start(&p->frag, s, atts, xmlvoid(s));
		
		/* HTML5 is case insensitive. */

		if (0 == strcasecmp(s, p->stack[p->stacksz - 1].name))
			p->stack[p->stacksz - 1].nested++;
		return;
	}

	/* Store/translate any existing keywords. */

	if (NULL != p->frag.root && POP_JOIN == p->op) {
		if ( ! translate(p))
			return;
	} else if (NULL != p->frag.root) {
		if ( ! store(p))
			return;
	}

	/*
	 * If we're translating, then echo the tags.
	 * Don't emit "its:translate", "xml:space", or the xmlns:its
	 * declaration.
	 * FIXME: make this optional.
	 * Make sure we accomodate for void elements.
	 */

	if (POP_JOIN == p->op) {
		printf("<%s", s);
		for (attp = atts; NULL != *attp; attp += 2) {
			if (POP_JOIN == p->op &&
			    0 == strcasecmp(s, "html") &&
			    0 == strcasecmp(attp[0], "xmlns:its"))
				continue;
			if (POP_JOIN == p->op &&
			    0 == strcasecmp(attp[0], "its:translate"))
				continue;
			if (POP_JOIN == p->op &&
			    0 == strcasecmp(attp[0], "xml:space"))
				continue;
			if (POP_JOIN == p->op &&
			    0 == strcasecmp(s, "html") &&
			    0 == strcasecmp(attp[0], "lang") &&
			    NULL == p->xp->trglang)
				continue;
			if (POP_JOIN == p->op &&
			    0 == strcasecmp(s, "html") &&
			    0 == strcasecmp(attp[0], "lang") &&
			    NULL != p->xp->trglang) {
				printf(" lang=\"%s\"", p->xp->trglang);
				continue;
			}
			printf(" %s=\"%s\"", attp[0], attp[1]);
		}
		if (POP_JOIN == p->op &&
		    0 == strcasecmp(s, "html") &&
		    NULL == p->lang &&
		    NULL != p->xp->trglang) 
			printf(" lang=\"%s\"", p->xp->trglang);
		if (xmlvoid(s))
			putchar('/');
		putchar('>');
	}

	/* 
	 * Check if we should begin translating.
	 * These attributes are case insensitive. 
	 */

	for (attp = atts; NULL != *attp; attp += 2)
		if (0 == strcasecmp(attp[0], "its:translate")) {
			if (0 == strcasecmp(attp[1], "yes"))
				dotrans = 1;
			else if (0 == strcasecmp(attp[1], "no"))
				dotrans = -1;
		} else if (0 == strcasecmp(attp[0], "xml:space")) {
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
	 * (Note that HTML5 is case insensitive.)
	 */

	if (0 == dotrans && 0 == preserve) {
		assert(p->stacksz > 0);
		if (0 == strcasecmp(s, p->stack[p->stacksz - 1].name))
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
	XML_ParsingStatus st;

	XML_GetParsingStatus(p->p, &st);
	if (XML_FINISHED == st.parsing) 
		return;

	assert(p->stacksz > 0);

	/* 
	 * Set if we're at the end of our current scope.
	 * Note that we're case insensitive.
	 */

	end = 0 == strcasecmp(p->stack[p->stacksz - 1].name, s) && 
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
		frag_node_end(&p->frag, s);
		if (0 == strcasecmp(p->stack[p->stacksz - 1].name, s))
			p->stack[p->stacksz - 1].nested--;
		return;
	}

	/* 
	 * We've ended an element that might have contained content that
	 * we want to translate.
	 * First, flush any existing translatable content.
	 */

	if (NULL != p->frag.root && POP_JOIN == p->op) {
		if ( ! translate(p))
			return;
	} else if (NULL != p->frag.root) {
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

	if (strcasecmp(p->stack[p->stacksz - 1].name, s))
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
	char 	 b[4096];
	ssize_t	 sz;
	int 	 rc = XML_STATUS_OK;

	XML_ParserReset(hp->p, NULL);
	XML_SetDefaultHandlerExpand(hp->p, htext);
	XML_SetElementHandler(hp->p, hstart, hend);
	XML_SetUserData(hp->p, hp);

	if (NULL == map) {
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
			return 1;
	} else {
		if (XML_STATUS_OK == XML_Parse(hp->p, map, mapsz, 1))
			return 1;
		perr(hp->fname, hp->p);
	}

	return 0;
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
extract(XML_Parser p, int copy, int argc, char *argv[])
{
	struct hparse	*hp;
	int		 rc;

	hp = hparse_alloc(p, POP_EXTRACT);

	if (0 != (rc = scanner(hp, argc, argv)))
		results_extract(hp, copy);

	hparse_free(hp);
	return(rc);
}

/*
 * Translate the files in argv with the dictionary in xliff, echoing the
 * translated versions.
 */
int
join(const char *xliff, XML_Parser p, 
	int copy, int argc, char *argv[])
{
	struct xparse	*xp;
	struct hparse	*hp;
	char		*map;
	size_t		 mapsz;
	int		 fd, rc, c = 0;

	if (-1 == (fd = map_open(xliff, &mapsz, &map)))
		return 0;

	xp = xparse_alloc(xliff, p);

	XML_ParserReset(p, NULL);
	XML_SetDefaultHandlerExpand(p, NULL);
	XML_SetElementHandler(p, xstart, xend);
	XML_SetUserData(p, xp);
	rc = XML_Parse(p, map, mapsz, 1);
	map_close(fd, map, mapsz);

	if (XML_STATUS_OK == rc) {
		hp = hparse_alloc(p, POP_JOIN);
		hp->xp = xp;
		hp->copy = copy;
		c = scanner(hp, argc, argv);
		assert(NULL == hp->words);
		hparse_free(hp);
	} else
		perr(xliff, p);

	xparse_free(xp);
	return c;
}

/*
 * Update (not in-line) the dictionary file xliff with the contents of
 * argv, outputting the merged XLIFF file.
 */
int
update(const char *xliff, XML_Parser p, int copy, 
	int keep, int quiet, int argc, char *argv[])
{
	struct xparse	*xp;
	struct hparse	*hp;
	char		*map;
	size_t		 mapsz;
	int		 fd, rc;

	if (-1 == (fd = map_open(xliff, &mapsz, &map)))
		return 0;

	xp = xparse_alloc(xliff, p);

	XML_ParserReset(p, NULL);
	XML_SetDefaultHandlerExpand(p, NULL);
	XML_SetElementHandler(p, xstart, xend);
	XML_SetUserData(p, xp);
	rc = XML_Parse(p, map, mapsz, 1);

	map_close(fd, map, mapsz);

	if (XML_STATUS_OK == rc) {
		hp = hparse_alloc(p, POP_EXTRACT);
		hp->xp = xp;
		if (0 != (rc = scanner(hp, argc, argv)))
			results_update(hp, copy, keep, quiet);
		hparse_free(hp);
	} else
		perr(xliff, p);

	xparse_free(xp);
	return XML_STATUS_OK == rc;
}
