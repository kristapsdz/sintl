/*	$Id$ */
/*
 * Copyright (c) 2018 Kristaps Dzonsons <kristaps@bsd.lv>
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

#include <assert.h>
#include <ctype.h>
#if HAVE_ERR
# include <err.h>
#endif
#include <expat.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "extern.h"

/* #define DEBUG 1 */

void
frag_node_free(struct frag *f)
{
	size_t	  i;
	char	**atts;

	if (NULL == f)
		return;

#ifdef DEBUG
	warnx("%s", __func__);
#endif

	for (i = 0; i < f->childsz; i++)
		frag_node_free(f->child[i]);

	if (NULL != f->atts)
		for (atts = f->atts; NULL != *atts; atts += 2) {
			free(atts[0]);
			free(atts[1]);
		}

	free(f->atts);
	free(f->child);
	free(f->val);
	free(f);
}

/*
 * Open a scope for element "s" attributes "attrs" in the scope already
 * opened by "cur".
 * This will set "root" and "cur" as required.
 */
void
frag_node_start(struct frag **root, struct frag **cur,
	const XML_Char *s, const XML_Char **atts)
{
	struct frag	 *f;
	size_t		  i = 0;
	const XML_Char	**attp;

	if (NULL == *root) {
#ifdef DEBUG
	warnx("%s: (new root)", __func__);
#endif
		assert(NULL == *cur);
		*root = calloc(1, sizeof(struct frag));
		if (NULL == *root)
			err(EXIT_FAILURE, NULL);
		(*root)->type = FRAG_ROOT;
		*cur = *root;
	}

#ifdef DEBUG
	warnx("%s: %s", __func__, s);
#endif

	assert(NULL != *cur);
	assert(NULL != *root);

	for (attp = atts; NULL != *attp; attp += 2, i += 2) 
		continue;

	f = calloc(1, sizeof(struct frag));
	if (NULL == f)
		err(EXIT_FAILURE, NULL);

	f->type = FRAG_NODE;
	f->val = strdup(s);
	f->valsz = strlen(f->val);
	f->parent = *cur;
	f->atts = calloc(i + 1, sizeof(char *));
	if (NULL == f->atts)
		err(EXIT_FAILURE, NULL);
	for (i = 0, attp = atts; NULL != *attp; attp += 2, i += 2) {
		f->atts[i] = strdup(attp[0]);
		f->atts[i + 1] = strdup(attp[1]);
 	}

	(*cur)->child = reallocarray
		((*cur)->child, (*cur)->childsz + 1,
		 sizeof(struct frag *));
	if (NULL == (*cur)->child)
		err(EXIT_FAILURE, NULL);
	(*cur)->child[(*cur)->childsz] = f;
	if ((*cur)->childsz)
		(*cur)->child[(*cur)->childsz - 1]->next = f;
	(*cur)->childsz++;
	*cur = f;
}

/*
 * Add binary data "s" of length "len" to the current text scope,
 * creating it in the current scope if not already done.
 * This will set "root" and "cur" as required.
 */
void
frag_node_text(struct frag **root, struct frag **cur, 
	const XML_Char *s, int len)
{
	struct frag	*f;
	size_t		 i;

	if (NULL == *root) {
#ifdef DEBUG
	warnx("%s: (new root)", __func__);
#endif
		assert(NULL == *cur);
		*root = calloc(1, sizeof(struct frag));
		if (NULL == *root)
			err(EXIT_FAILURE, NULL);
		(*root)->type = FRAG_ROOT;
		*cur = *root;
	}

#ifdef DEBUG
	warnx("%s: (text)", __func__);
#endif

	assert(NULL != *root);
	assert(NULL != *cur);

	f = (*cur)->childsz ? 
		(*cur)->child[(*cur)->childsz - 1] : NULL;

	if (NULL == f || FRAG_TEXT != f->type) {
		f = calloc(1, sizeof(struct frag));
		if (NULL == f)
			err(EXIT_FAILURE, NULL);
		f->type = FRAG_TEXT;
		f->parent = *cur;
		(*cur)->child = reallocarray
			((*cur)->child, (*cur)->childsz + 1,
			 sizeof(struct frag *));
		if (NULL == (*cur)->child)
			err(EXIT_FAILURE, NULL);
		(*cur)->child[(*cur)->childsz] = f;
		if ((*cur)->childsz)
			(*cur)->child[(*cur)->childsz - 1]->next = f;
		(*cur)->childsz++;
	}

	if (0 == f->has_nonws)
		for (i = 0; i < (size_t)len; i++)
			if ( ! isspace((unsigned char)s[i])) {
				f->has_nonws = 1;
				break;
			}

	f->val = realloc(f->val, f->valsz + len);
	if (NULL == f->val)
		err(EXIT_FAILURE, NULL);
	memcpy(f->val + f->valsz, s, len);
	f->valsz += len;
}

void
frag_node_end(struct frag **cur, const XML_Char *s)
{

#ifdef DEBUG
	warnx("%s: %s", __func__, s);
#endif

	assert(NULL != *cur);
	assert(FRAG_NODE == (*cur)->type);
	assert(0 == strcmp(s, (*cur)->val));
	(*cur)->node_closed = 1;
	*cur = (*cur)->parent;
}

static void
append(char **buf, size_t *sz, size_t *max, const XML_Char *s, size_t len)
{

	if (NULL == s || 0 == len)
		return;

	if (*sz + len + 1 > *max) {
		*max = *sz + len + 1;
		if (NULL == (*buf = realloc(*buf, *max)))
			err(EXIT_FAILURE, NULL);
	}

	assert(NULL != *buf);
	memcpy(*buf + *sz, s, len);
	*sz += len;
	(*buf)[*sz] = '\0';
}

static void
strip(char *buf, size_t *start, const char *nstr, size_t len)
{
	size_t	 i;

	for (i = 0; i < len; (*start)++) {
		buf[*start] = nstr[i];
		if ( ! isspace((unsigned char)nstr[i++]))
			continue;
		buf[*start] = ' ';
		while (i < len && isspace((unsigned char)nstr[i]))
			i++;
	}

	buf[*start] = '\0';
}

static void
frag_serialise_r(const struct frag *f, 
	char **buf, size_t *sz, size_t *max, int preserve)
{
	size_t	 	  i, sv;
	const char	**attp;

	assert(NULL != f);

	if (FRAG_NODE == f->type) {
		append(buf, sz, max, "<", 1);
		append(buf, sz, max, f->val, f->valsz);
		attp = (const char **)f->atts;
		for ( ; NULL != *attp; attp += 2) {
			append(buf, sz, max, " ", 1);
			append(buf, sz, max, attp[0], strlen(attp[0]));
			append(buf, sz, max, "=\"", 2);
			append(buf, sz, max, attp[1], strlen(attp[1]));
			append(buf, sz, max, "\"", 1);
		}
		append(buf, sz, max, ">", 1);
	} else if (FRAG_TEXT == f->type && ! preserve) {
		sv = *sz;
		append(buf, sz, max, f->val, f->valsz);
		*sz = sv;
		strip(*buf, sz, f->val, f->valsz);
	} else if (FRAG_TEXT == f->type)
		append(buf, sz, max, f->val, f->valsz);

	for (i = 0; i < f->childsz; i++)
		frag_serialise_r(f->child[i], buf, sz, max, preserve);

	if (FRAG_NODE == f->type && f->node_closed) {
		append(buf, sz, max, "</", 2);
		append(buf, sz, max, f->val, f->valsz);
		append(buf, sz, max, ">", 1);
	}
}

char *
frag_serialise(const struct frag *f, int preserve, int minimise, int *reduce)
{
	size_t	 i, sz = 0, max = 0, nt, nn;
	char	*buf = NULL;
	const struct frag *ff;

	if (NULL == f)
		return NULL;

	if (minimise) {
		/*
		 * First pass of minimisation: remove all fragments that
		 * are simply whitespace and empty nodes.
		 */

		ff = f;
		while (NULL != ff) {
			nn = nt = 0;

			/* Only whitespace? */

			if (1 == ff->childsz &&
			    FRAG_TEXT == ff->child[0]->type &&
			    0 == ff->child[0]->has_nonws)
				return NULL;

			/* Count all nodes/whitespace. */

			for (i = 0; i < ff->childsz; i++) {
				nn += FRAG_NODE == ff->child[i]->type;
				nt += FRAG_TEXT == ff->child[i]->type &&
					0 == ff->child[i]->has_nonws;
			}

			/* Only node/whitespace? */

			if (1 != nn || nn + nt != ff->childsz) {
				ff = NULL;
				break;
			}

			/* Descend into node. */

			if (FRAG_NODE == ff->child[0]->type) 
				ff = ff->child[0];
			else if (FRAG_NODE == ff->child[1]->type) 
				ff = ff->child[1];
			else if (FRAG_NODE == ff->child[2]->type) 
				ff = ff->child[2];
			else
				abort();

			if (0 == ff->childsz)
				return NULL;
		}
	}

	if (NULL != reduce) {
		ff = f;
		while (NULL != ff) {
			nn = nt = 0;

			/* Only whitespace?  Great. */

			if (1 == ff->childsz &&
			    FRAG_TEXT == ff->child[0]->type &&
			    ff->child[0]->has_nonws) {
				ff = ff->child[0];
				break;
			}

			/* Count all nodes/whitespace. */

			for (i = 0; i < ff->childsz; i++) {
				nn += FRAG_NODE == ff->child[i]->type;
				nt += FRAG_TEXT == ff->child[i]->type &&
					0 == ff->child[i]->has_nonws;
			}

			/* Only node/whitespace? */

			if (1 != nn || nn + nt != ff->childsz) {
				ff = NULL;
				break;
			}

			/* Descend into node. */

			if (FRAG_NODE == ff->child[0]->type) 
				ff = ff->child[0];
			else if (FRAG_NODE == ff->child[1]->type) 
				ff = ff->child[1];
			else if (FRAG_NODE == ff->child[2]->type) 
				ff = ff->child[2];
			else
				abort();
		}
		if (NULL != ff) {
			f = ff;
			*reduce = 1;
		}
	}

	frag_serialise_r(f, &buf, &sz, &max, preserve);

	if (0 == sz) {
		free(buf);
		return NULL;
	} else if (preserve)
		return buf;

	for (i = 0; i < sz; i++)
		if ( ! isspace((unsigned char)buf[i]))
			break;

	if (i == sz) {
		free(buf);
		return NULL;
	}

	return buf;
}

static void
frag_print(const char *cp, size_t sz, int preserve)
{
	size_t	 i;

	if (preserve) {
		printf("%.*s", (int)sz, cp);
		return;
	}

	for (i = 0; i < sz; ) {
		putchar(cp[i++]);
		if ( ! isspace((unsigned char)cp[i - 1])) 
			continue;
		while (i < sz && isspace((unsigned char)cp[i]))
			i++;
	}
}

static void
frag_merge_r(const struct frag *f, const char *source, 
	const char *target, int preserve)
{
	size_t	 i;
	const char **attp;

	if (FRAG_TEXT == f->type) {
		if (0 == strncmp(source, f->val, f->valsz))
			printf("%s", target);
		else
			frag_print(f->val, f->valsz, preserve);
	} else if (FRAG_NODE == f->type) {
		printf("<%.*s", (int)f->valsz, f->val);
		attp = (const char **)f->atts;
		for ( ; NULL != *attp; attp += 2)
			printf(" %s=\"%s\"", attp[0], attp[1]);
		putchar('>');
	}

	for (i = 0; i < f->childsz; i++)
		frag_merge_r(f->child[i], source, target, preserve);

	if (FRAG_NODE == f->type)
		printf("</%.*s>", (int)f->valsz, f->val);
}

void
frag_print_merge(const struct frag *root, const char *source, 
	const char *target, int preserve)
{

	frag_merge_r(root, source, target, preserve);
}
