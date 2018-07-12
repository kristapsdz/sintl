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

static void
frag_append_text(struct fragseq *q, const XML_Char *s, size_t len)
{

	q->copy = realloc(q->copy, q->copysz + len);
	if (NULL == q->copy)
		err(EXIT_FAILURE, NULL);
	memcpy(q->copy + q->copysz, s, len);
	q->copysz += len;
}

static void
frag_copy_elem(struct fragseq *q, int null,
	const XML_Char *s, const XML_Char **atts)
{
	const XML_Char	**attp;

	if (NULL == atts && ! null) {
		frag_append_text(q, "</", 2);
		frag_append_text(q, s, strlen(s));
		frag_append_text(q, ">", 1);
		return;
	} else if (NULL == atts)
		return;

	frag_append_text(q, "<", 1);
	frag_append_text(q, s, strlen(s));

	for (attp = atts; NULL != *attp; attp += 2) {
		frag_append_text(q, " ", 1);
		frag_append_text(q, attp[0], strlen(attp[0]));
		frag_append_text(q, "=\"", 2);
		frag_append_text(q, attp[1], strlen(attp[1]));
		frag_append_text(q, "\"", 1);
	}

	if (null)
		frag_append_text(q, "/", 1);

	frag_append_text(q, ">", 1);
}

/* #define DEBUG 1 */

static void
frag_node_free(struct frag *f)
{
	size_t	  i;
	char	**atts;

	if (NULL == f)
		return;

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
frag_node_start(struct fragseq *q,
	const XML_Char *s, const XML_Char **atts, int null)
{
	struct frag	 *f;
	size_t		  i = 0;
	const XML_Char	**attp;

	frag_copy_elem(q, null, s, atts);

	if (NULL == q->root) {
		assert(NULL == q->cur);
		q->root = calloc(1, sizeof(struct frag));
		if (NULL == q->root)
			err(EXIT_FAILURE, NULL);
		q->root->type = FRAG_ROOT;
		q->cur = q->root;
	}

	assert(NULL != q->cur);
	assert(NULL != q->root);

	for (attp = atts; NULL != *attp; attp += 2, i += 2) 
		continue;

	f = calloc(1, sizeof(struct frag));
	if (NULL == f)
		err(EXIT_FAILURE, NULL);

	f->type = FRAG_NODE;
	f->val = strdup(s);
	f->valsz = strlen(f->val);
	f->parent = q->cur;
	f->atts = calloc(i + 1, sizeof(char *));
	if (NULL == f->atts)
		err(EXIT_FAILURE, NULL);
	for (i = 0, attp = atts; NULL != *attp; attp += 2, i += 2) {
		f->atts[i] = strdup(attp[0]);
		f->atts[i + 1] = strdup(attp[1]);
 	}

	q->cur->child = reallocarray
		(q->cur->child, q->cur->childsz + 1,
		 sizeof(struct frag *));
	if (NULL == q->cur->child)
		err(EXIT_FAILURE, NULL);
	q->cur->child[q->cur->childsz] = f;
	if (q->cur->childsz)
		q->cur->child[q->cur->childsz - 1]->next = f;
	q->cur->childsz++;
	q->cur = f;
}

/*
 * Add binary data "s" of length "len" to the current text scope,
 * creating it in the current scope if not already done.
 * This will set "root" and "cur" as required.
 */
void
frag_node_text(struct fragseq *q,
	const XML_Char *s, size_t len, int preserve)
{
	struct frag	*f;
	size_t		 i;

	frag_append_text(q, s, len);

	/* Allocate root, if not existing. */

	if (NULL == q->root) {
		assert(NULL == q->cur);
		q->root = calloc(1, sizeof(struct frag));
		if (NULL == q->root)
			err(EXIT_FAILURE, NULL);
		q->root->type = FRAG_ROOT;
		q->cur = q->root;
	}

	assert(NULL != q->root);
	assert(NULL != q->cur);

	f = q->cur->childsz ? 
		q->cur->child[q->cur->childsz - 1] : NULL;

	/* Allocate text node, if applicable. */

	if (NULL == f || FRAG_TEXT != f->type) {
		f = calloc(1, sizeof(struct frag));
		if (NULL == f)
			err(EXIT_FAILURE, NULL);
		f->type = FRAG_TEXT;
		f->parent = q->cur;
		q->cur->child = reallocarray
			(q->cur->child, q->cur->childsz + 1,
			 sizeof(struct frag *));
		if (NULL == q->cur->child)
			err(EXIT_FAILURE, NULL);
		q->cur->child[q->cur->childsz] = f;
		if (q->cur->childsz)
			q->cur->child[q->cur->childsz - 1]->next = f;
		q->cur->childsz++;
	}

	/* See if we have any non-spaces. */

	if (0 == f->has_nonws)
		for (i = 0; i < len; i++)
			if ( ! isspace((unsigned char)s[i])) {
				f->has_nonws = 1;
				break;
			}

	/*
	 * If we're in preserve mode, then copy in all of our data.
	 * If we're not, then collapse contiguous white-space and also
	 * convert newlines to spaces.
	 */

	if (preserve) {
		f->val = realloc(f->val, f->valsz + len);
		if (NULL == f->val)
			err(EXIT_FAILURE, NULL);
		memcpy(f->val + f->valsz, s, len);
		f->valsz += len;
		return;
	} 

	f->val = realloc(f->val, f->valsz + len);
	if (NULL == f->val)
		err(EXIT_FAILURE, NULL);
	for (i = 0; i < len; ) {
		if (f->valsz &&
		    isspace((unsigned char)s[i]) &&
		    isspace((unsigned char)f->val[f->valsz - 1])) {
			i++;
			continue;
		}
		f->val[f->valsz++] = s[i++];
		if (isspace((unsigned char)f->val[f->valsz - 1]))
			f->val[f->valsz - 1] = ' ';
	}
}

void
frag_node_end(struct fragseq *q, const XML_Char *s, int null)
{

	frag_copy_elem(q, null, s, NULL);
	assert(NULL != q->cur);
	assert(FRAG_NODE == q->cur->type);
	assert(0 == strcmp(s, q->cur->val));
	q->cur->node_closed = 1;
	q->cur = q->cur->parent;
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
frag_serialise_r(const struct frag *f, 
	char **buf, size_t *sz, size_t *max)
{
	size_t	 	  i;
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
	} else if (FRAG_TEXT == f->type)
		append(buf, sz, max, f->val, f->valsz);

	for (i = 0; i < f->childsz; i++)
		frag_serialise_r(f->child[i], buf, sz, max);

	if (FRAG_NODE == f->type && f->node_closed) {
		append(buf, sz, max, "</", 2);
		append(buf, sz, max, f->val, f->valsz);
		append(buf, sz, max, ">", 1);
	}
}

char *
frag_serialise(const struct fragseq *q, 
	int keepempty, int minimise, int *reduce)
{
	size_t	 i, sz = 0, max = 0, nt, nn;
	char	*buf = NULL;
	const struct frag *ff, *f;

	if (NULL == q || NULL == (f = q->root))
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

	frag_serialise_r(f, &buf, &sz, &max);

	if (0 == sz) {
		free(buf);
		return NULL;
	} else if (keepempty)
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
frag_merge_r(const struct frag *f, 
	const char *source, const char *target)
{
	size_t	 i;
	const char **attp;

	if (FRAG_TEXT == f->type) {
		if (0 == strncmp(source, f->val, f->valsz))
			printf("%s", target);
		else
			printf("%.*s", (int)f->valsz, f->val);
	} else if (FRAG_NODE == f->type) {
		printf("<%.*s", (int)f->valsz, f->val);
		attp = (const char **)f->atts;
		for ( ; NULL != *attp; attp += 2)
			printf(" %s=\"%s\"", attp[0], attp[1]);
		putchar('>');
	}

	for (i = 0; i < f->childsz; i++)
		frag_merge_r(f->child[i], source, target);

	if (FRAG_NODE == f->type)
		printf("</%.*s>", (int)f->valsz, f->val);
}

void
frag_print_merge(const struct fragseq *q, 
	const char *source, const char *target)
{

	assert(NULL != q);
	frag_merge_r(q->root, source, target);
}

void
fragseq_clear(struct fragseq *p)
{

	frag_node_free(p->root);
	free(p->copy);
	p->root = p->cur = NULL;
	p->copy = NULL;
	p->copysz = 0;
}
