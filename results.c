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

#define	XLIFFURN "urn:oasis:names:tc:xliff:document:2.0"

static int
xcmp(const void *p1, const void *p2)
{
	const struct xliff *x1 = p1, *x2 = p2;

	return(strcmp(x1->source, x2->source));
}

static int
cmp(const void *p1, const void *p2)
{

	return(strcmp(*(const char **)p1, *(const char **)p2));
}

void
results_update(struct hparse *hp)
{
	char		*cp;
	size_t	 	 i, j, ssz, smax;
	struct xliff	*sorted;

	/* Allows us to de-dupe in place. */
	qsort(hp->words, hp->wordsz, sizeof(char *), cmp);

	sorted = NULL;
	ssz = smax = 0;

	/*
	 * Merge found words into the merge buffer.
	 */
	for (i = 0; i < hp->wordsz; i++) {
		cp = hp->words[i];
		/* De-dupe XML file words. */
		if (i && 0 == strcmp(cp, hp->words[i - 1]))
			continue;
		for (j = 0; j < hp->xp->xliffsz; j++)
			if (0 == strcmp(cp, hp->xp->xliffs[j].source))
				break;

		/* Append word to merge buffer. */
		if (ssz + 1 > smax) {
			smax = ssz + 512;
			sorted = reallocarray(sorted, 
				smax, sizeof(struct xliff));
			if (NULL == sorted) {
				perror(NULL);
				exit(EXIT_FAILURE);
			}
		}

		/* If in the XLIFF, append the source and target. */
		sorted[ssz].target = j == hp->xp->xliffsz ? 
			NULL : hp->xp->xliffs[j].target;
		sorted[ssz++].source = cp;
	}

	/*
	 * Merge from the existing XLIFF back into the words.
	 * TODO: this should have a command-line option, as at this
	 * point we're keeping things we're no longer translating.
	 */
	for (i = 0; i < hp->xp->xliffsz; i++) {
		cp = hp->xp->xliffs[i].source;
		for (j = 0; j < hp->wordsz; j++)
			if (0 == strcmp(cp, hp->words[j]))
				break;
		if (j < hp->wordsz)
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

		sorted[ssz].target = hp->xp->xliffs[i].target;
		sorted[ssz++].source = cp;
	}

	/* Output the sorted dictionary file. */
	qsort(sorted, ssz, sizeof(struct xliff), xcmp);

	puts("<xliff xmlns=\"" XLIFFURN "\" "
		"version=\"2.0\" srcLang=\"TODO\" trgLang=\"TODO\">");
	puts("\t<file id=\"file1\">");
	puts("\t\t<unit id=\"unit1\">");
	for (i = 0; i < ssz; i++) {
		puts("\t\t\t<segment>");
		printf("\t\t\t\t<source>%s</source>\n", 
			sorted[i].source);
		printf("\t\t\t\t<target>%s</target>\n", 
			NULL == sorted[i].target ? 
			"TODO" : sorted[i].target);
		puts("\t\t\t</segment>");
	}

	puts("\t\t</unit>");
	puts("\t</file>");
	puts("</xliff>");

	free(sorted);
}

void
results_extract(struct hparse *p)
{
	size_t	 i;

	qsort(p->words, p->wordsz, sizeof(char *), cmp);

	printf("<xliff xmlns=\"" XLIFFURN "\" "
		"version=\"2.0\" srcLang=\"%s\" trgLang=\"TODO\">\n",
		NULL == p->lang ? "TODO" : p->lang);
	puts("\t<file id=\"file1\">");
	puts("\t\t<unit id=\"unit1\">");
	for (i = 0; i < p->wordsz; i++) {
		if (i && 0 == strcmp(p->words[i], p->words[i - 1]))
			continue;
		puts("\t\t\t<segment>");
		printf("\t\t\t\t<source>%s</source>\n", p->words[i]);
		puts("\t\t\t\t<target>TODO</target>");
		puts("\t\t\t</segment>");
	}
	puts("\t\t</unit>");
	puts("\t</file>");
	puts("</xliff>");
}

