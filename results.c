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

#include <assert.h>
#include <expat.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "extern.h"

static int
xcmp(const void *p1, const void *p2)
{
	const struct xliff *x1 = p1, *x2 = p2;

	return strcmp(x1->source, x2->source);
}

static int
cmp(const void *p1, const void *p2)
{
	const struct word *x1 = p1, *x2 = p2;

	return strcmp(x1->source, x2->source);
}

void
results_update(struct hparse *hp, int copy, int keep)
{
	char		*cp;
	size_t	 	 i, j, ssz, smax;
	struct xliff	*sorted;

	/* Allows us to de-dupe in place. */

	qsort(hp->words, hp->wordsz, sizeof(struct word), cmp);

	sorted = NULL;
	ssz = smax = 0;

	/* Merge found words in input into the merge buffer. */

	for (i = 0; i < hp->wordsz; i++) {
		cp = hp->words[i].source;
		if (i && 0 == strcmp(cp, hp->words[i - 1].source))
			continue;
		if (ssz + 1 > smax) {
			smax = ssz + 512;
			sorted = reallocarray(sorted, 
				smax, sizeof(struct xliff));
			if (NULL == sorted) {
				perror(NULL);
				exit(EXIT_FAILURE);
			}
		}

		/* 
		 * Are we finding this in the xliff?
		 * If so, use the xliff's target information.
		 * Otherwise, copy only the source.
		 */

		for (j = 0; j < hp->xp->xliffsz; j++)
			if (0 == strcmp(cp, hp->xp->xliffs[j].source))
				break;

		if (j == hp->xp->xliffsz) {
			fprintf(stderr, "%s:%zu:%zu: new translation\n",
				hp->fname,
				hp->words[i].line,
				hp->words[i].col);
			memset(&sorted[ssz], 0, sizeof(struct xliff));
			sorted[ssz].source = cp;
		} else
			sorted[ssz] = hp->xp->xliffs[j];

		ssz++;
	}

	/* Merge from the existing XLIFF back into the words. */

	for (i = 0; i < hp->xp->xliffsz; i++) {
		cp = hp->xp->xliffs[i].source;
		for (j = 0; j < hp->wordsz; j++)
			if (0 == strcmp(cp, hp->words[j].source))
				break;
		if (j < hp->wordsz)
			continue;
	
		if ( ! keep) {
			fprintf(stderr, "%s:%zu:%zu: discarding "
				"unused translation\n",
				hp->xp->fname,
				hp->xp->xliffs[i].line,
				hp->xp->xliffs[i].col);
			continue;
		}

		if (ssz + 1 > smax) {
			smax = ssz + 512;
			sorted = reallocarray(sorted, 
				smax, sizeof(struct xliff));
			if (NULL == sorted) {
				perror(NULL);
				exit(EXIT_FAILURE);
			}
		}
		sorted[ssz++] = hp->xp->xliffs[i];
	}

	/* Output the sorted dictionary file. */
	qsort(sorted, ssz, sizeof(struct xliff), xcmp);

	printf("<xliff version=\"1.2\">\n"
	       "\t<file source-language=\"%s\" "
	          "target-language=\"%s\" tool=\"sintl\">\n"
	       "\t\t<body>\n",
	       NULL == hp->xp->srclang ? "TODO" : hp->xp->srclang,
	       NULL == hp->xp->trglang ? "TODO" : hp->xp->trglang);

	for (i = 0; i < ssz; i++) 
		if (0 == sorted[i].target.copysz && copy)
			printf("\t\t\t<trans-unit id=\"%zu\">\n"
			       "\t\t\t\t<source>%s</source>\n"
			       "\t\t\t\t<target>%s</target>\n"
			       "\t\t\t</trans-unit>\n",
			       i + 1, sorted[i].source,
			       sorted[i].source);
		else if (0 == sorted[i].target.copysz)
			printf("\t\t\t<trans-unit id=\"%zu\">\n"
			       "\t\t\t\t<source>%s</source>\n"
			       "\t\t\t</trans-unit>\n",
			       i + 1, sorted[i].source);
		else
			printf("\t\t\t<trans-unit id=\"%zu\">\n"
			       "\t\t\t\t<source>%s</source>\n"
			       "\t\t\t\t<target>%.*s</target>\n"
			       "\t\t\t</trans-unit>\n",
			       i + 1, sorted[i].source,
			       (int)sorted[i].target.copysz,
			       sorted[i].target.copy);

	puts("\t\t</body>");
	puts("\t</file>");
	puts("</xliff>");

	free(sorted);
}

void
results_extract(struct hparse *p, int copy)
{
	size_t	 i, j;

	qsort(p->words, p->wordsz, sizeof(struct word), cmp);

	printf("<xliff version=\"1.2\">\n"
	       "\t<file source-language=\"%s\" "
	          "target-language=\"TODO\" tool=\"sintl\">\n"
	       "\t\t<body>\n",
	       NULL == p->lang ? "TODO" : p->lang);
	for (i = j = 0; i < p->wordsz; i++) {
		if (i && 0 == strcmp(p->words[i].source, p->words[i - 1].source))
			continue;
		printf("\t\t\t<trans-unit id=\"%zu\">\n"
		       "\t\t\t\t<source>%s</source>\n", 
		       ++j, p->words[i].source);
		if (copy)
			printf("\t\t\t\t<target>%s</target>\n", 
				p->words[i].source);
		puts("\t\t\t</trans-unit>");
	}
	puts("\t\t</body>\n"
	     "\t</file>\n"
	     "</xliff>");
}
