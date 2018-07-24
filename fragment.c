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

	f->is_null = null;
	f->type = FRAG_NODE;
	f->val = strdup(s);
	f->valsz = strlen(f->val);
	f->parent = q->cur;
	f->atts = calloc(i + 1, sizeof(char *));
	f->id = q->elemsz;
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

	/* Add to list of all elements. */

	q->elems = reallocarray
		(q->elems,
		 q->elemsz + 1,
		 sizeof(struct frag *));
	if (NULL == q->elems)
		err(EXIT_FAILURE, NULL);
	q->elems[q->elemsz++] = f;
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
frag_node_end(struct fragseq *q, const XML_Char *s)
{

	assert(NULL != q->cur);
	assert(FRAG_NODE == q->cur->type);
	assert(0 == strcmp(s, q->cur->val));

	frag_copy_elem(q, q->cur->is_null, s, NULL);
	q->cur->node_closed = 1;
	q->cur = q->cur->parent;
}

static void
frag_append(char **buf, size_t *sz, 
	size_t *max, const XML_Char *s, size_t len)
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

/*
 * Recursively serialise "f" into the dynamic buffer.
 */
static void
frag_serialise_r(const struct frag *f, 
	char **buf, size_t *sz, size_t *max)
{
	size_t	 	  i, nbufsz;
	char		  nbuf[32];

	assert(NULL != f);

	if (FRAG_NODE == f->type) {
		frag_append(buf, sz, max, "<", 1);
		snprintf(nbuf, sizeof(nbuf), "%zu", f->id);
		nbufsz = strlen(nbuf);
		if (f->is_null) {
			frag_append(buf, sz, max, "x id=\"", 6);
			frag_append(buf, sz, max, nbuf, nbufsz);
			frag_append(buf, sz, max, "\" />", 4);
		} else {
			frag_append(buf, sz, max, "g id=\"", 6);
			frag_append(buf, sz, max, nbuf, nbufsz);
			frag_append(buf, sz, max, "\">", 2);
		}
		frag_append(buf, sz, max, ">", 1);
	} else if (FRAG_TEXT == f->type)
		frag_append(buf, sz, max, f->val, f->valsz);

	for (i = 0; i < f->childsz; i++)
		frag_serialise_r(f->child[i], buf, sz, max);

	if (FRAG_NODE == f->type && f->node_closed && ! f->is_null)
		frag_append(buf, sz, max, "</g>", 4);
}

static int
frag_canreduce(const struct frag *f)
{

	return (FRAG_TEXT == f->type && 0 == f->has_nonws) ||
	       (FRAG_NODE == f->type && 0 == f->childsz);
}

/*
 * If "minimise", then ignore empty nodes.
 * Empty nodes are things like <img />, optionally surrounded by space.
 * If "reduce" is non-NULL, then strip away surrounding material to get
 * to translatable content.
 * If any stripping occurs, set "reduce" to be non-zero.
 */
char *
frag_serialise(const struct fragseq *q, int minimise, int *reduce)
{
	size_t	 i, sz = 0, max = 0, nt, nn, nsz;
	char	*buf = NULL;
	const struct frag *ff, *f;

	if (NULL == q || NULL == (f = q->root))
		return NULL;

	/*
	 * Minimisation pass: return NULL if we encounter standalone
	 * nodes, with or without surrounding whitespace.
	 * For example, " <b><img /></b> ".
	 * TODO: this should be a bottom-up accumulation of non-space
	 * text content count.
	 */

	if (minimise)
		for (ff = f; NULL != ff; ) {
			if (0 == ff->childsz)
				return NULL;

			/* Only whitespace? */

			if (1 == ff->childsz &&
			    FRAG_TEXT == ff->child[0]->type &&
			    0 == ff->child[0]->has_nonws)
				return NULL;

			/* Count number of text/nodes in children. */

			for (nn = nt = i = 0; i < ff->childsz; i++) {
				nn += FRAG_NODE == ff->child[i]->type;
				nt += FRAG_TEXT == ff->child[i]->type &&
					0 == ff->child[i]->has_nonws;
			}

			/* Stop if not one node and whitespace. */

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

	/*
	 * Reduction: top-down, strip away superfluous elements in our
	 * fragment tree.  
	 * This is limited to surrounding empty elements, possibly
	 * buffered with white-space.
	 * For example, " <b>foo</b> " -> "foo".
	 * For example, " <b><i>foo</i> <i>bar</i></b> " -> "<i>foo</i>
	 * <i>bar</i>".
	 */

	if (NULL != reduce) {
		for (ff = f; NULL != ff; ) {
			assert(ff->childsz);

			/* Only child is text: send to output. */

			if (1 == ff->childsz &&
			    FRAG_TEXT == ff->child[0]->type &&
			    ff->child[0]->has_nonws) {
				frag_serialise_r
					(ff->child[0], 
					 &buf, &sz, &max);
				*reduce = f != ff;
				break;
			}

			/*
			 * If just a single node w/whitespace buffering,
			 * recurse into the node.
			 * Otherwise, trim surrounding white-space and
			 * send remaining parts into the output.
			 */

			for (nn = nt = i = 0; i < ff->childsz; i++) {
				nn += FRAG_NODE == ff->child[i]->type;
				nt += FRAG_TEXT == ff->child[i]->type &&
					0 == ff->child[i]->has_nonws;
			}

			if (1 == nn && nn + nt == ff->childsz) {
				if (FRAG_NODE == ff->child[0]->type) 
					ff = ff->child[0];
				else if (FRAG_NODE == ff->child[1]->type) 
					ff = ff->child[1];
				else if (FRAG_NODE == ff->child[2]->type) 
					ff = ff->child[2];
				else
					abort();
				continue;
			}

			nsz = ff->childsz;
			assert(nsz);

			for (nsz = ff->childsz; nsz > 0; nsz--)
				if ( ! frag_canreduce(ff->child[nsz - 1]))
					break;
			for (i = 0; i < nsz; i++)
				if ( ! frag_canreduce(ff->child[i]))
					break;
			for ( ; i < nsz; i++)
				frag_serialise_r
					(ff->child[i], 
					 &buf, &sz, &max);

			*reduce = f != ff;
			break;
		}

		/* 
		 * Trim spacing in the output.
		 * This happens for (1) singleton text reduced children
		 * and (2) text and node series.
		 */

		for (i = 0; i < sz; i++)
			if ( ! isspace((unsigned char)buf[i]))
				break;

		if (i < sz) {
			memmove(buf, &buf[i], sz - i);
			sz -= i;
			buf[sz] = '\0';
			for ( ; sz > 0; sz--) {
				if ( ! isspace((unsigned char)buf[sz - 1]))
					break;
				buf[sz - 1] = '\0';
			}
			*reduce = 1;
		} else if (i == sz)
			sz = 0;
	} else
		frag_serialise_r(f, &buf, &sz, &max);

	/* This is set if it's just whitespace. */

	if (0 == sz) {
		free(buf);
		return NULL;
	} 

	return buf;
}

static void
frag_print_reduced(const struct frag *f)
{
	const char	**attp;

	if (FRAG_TEXT == f->type) {
		assert(f->val);
		printf("%.*s", (int)f->valsz, f->val);
		return;
	}

	assert(FRAG_NODE == f->type);
	assert(f->childsz < 2);

	printf("<%.*s", (int)f->valsz, f->val);
	attp = (const char **)f->atts;
	for ( ; NULL != *attp; attp += 2)
		printf(" %s=\"%s\"", attp[0], attp[1]);
	if (f->is_null)
		putchar('/');
	putchar('>');
	if (f->childsz) {
		assert( ! f->is_null);
		frag_print_reduced(f->child[0]);
	}
	if ( ! f->is_null)
		printf("</%.*s>", (int)f->valsz, f->val);
}

static void
frag_print_merge_r(const struct frag *f,
	const char *source, const char *target)
{
	size_t	 	  i, nn = 0, nt = 0, sz;
	const char 	**attp;
	const char	 *cp;

	if (FRAG_NODE == f->type) {
		printf("<%.*s", (int)f->valsz, f->val);
		attp = (const char **)f->atts;
		for ( ; NULL != *attp; attp += 2)
			printf(" %s=\"%s\"", attp[0], attp[1]);
		if (f->is_null) {
			putchar('/');
			assert(0 == f->childsz);
		}
		putchar('>');
		if (f->is_null) 
			return;
	}

	/* 
	 * Only text: the algorithm is complete.
	 * Make sure we account for surrounding white-space.
	 */

	if (1 == f->childsz &&
	    FRAG_TEXT == f->child[0]->type &&
	    f->child[0]->has_nonws) {
		cp = f->child[0]->val;
		sz = f->child[0]->valsz;
		if (sz && isspace((unsigned char)cp[0]))
			putchar(' ');
		printf("%s", target);
		if (sz && isspace((unsigned char)cp[sz - 1]))
			putchar(' ');
		goto out;
	}

	/* See if we're going to recursively step. */

	for (i = 0; i < f->childsz; i++) {
		nn += FRAG_NODE == f->child[i]->type;
		nt += FRAG_TEXT == f->child[i]->type &&
			0 == f->child[i]->has_nonws;
	}

	/* 
	 * The algorithm is complete.
	 * Make sure we account for surrounding white-space.
	 */

	if (1 != nn || nn + nt != f->childsz) {
		for (i = 0; i < f->childsz; i++)  {
			if ( ! frag_canreduce(f->child[i]))
				break;
			frag_print_reduced(f->child[i]);
		}

		if (i < f->childsz &&
	  	    FRAG_TEXT == f->child[i]->type &&
		    f->child[i]->valsz &&
		    f->child[i]->has_nonws &&
		    isspace((unsigned char)f->child[i]->val[0]))
			putchar(' ');

		printf("%s", target);

		for (i = f->childsz; i > 0; i--)  {
			if ( ! frag_canreduce(f->child[i - 1]))
				break;
			frag_print_reduced(f->child[i - 1]);
		}

		if (i > 0 &&
	  	    FRAG_TEXT == f->child[i - 1]->type &&
		    f->child[i - 1]->valsz &&
		    f->child[i - 1]->has_nonws) {
			cp = f->child[i - 1]->val;
			sz = f->child[i - 1]->valsz;
			if (isspace((unsigned char)cp[sz - 1]))
				putchar(' ');
		}
		goto out;
	}

	/* Descend into node. */

	for (i = 0; i < f->childsz; i++)
		if (FRAG_NODE == f->child[i]->type)
			frag_print_merge_r
				(f->child[i], source, target);
		else
			printf("%.*s", (int)f->child[i]->valsz,
				f->child[i]->val);

out:
	if (FRAG_NODE == f->type)
		printf("</%.*s>", (int)f->valsz, f->val);
}

/*
 * Take a fragment "q" and a "source" that matches into some part of the
 * post-reduced "q".
 * Find the "source" in "q" by reversing the reduction and emitting the
 * "target" instead.
 * This should ONLY be run on reduced trees.
 */
void
frag_print_merge(const struct fragseq *q, 
	const char *source, const char *target)
{

	frag_print_merge_r(q->root, source, target);
}

/*
 * Clear "p", but do not free() it.
 */
void
fragseq_clear(struct fragseq *p)
{

	frag_node_free(p->root);
	free(p->copy);
	free(p->elems);
	p->root = p->cur = NULL;
	p->copy = NULL;
	p->elems = NULL;
	p->copysz = p->elemsz = 0;
}
