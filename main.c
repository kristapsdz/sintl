/*	$Id$ */
/*
 * Copyright (c) 2014, 2016, 2018 Kristaps Dzonsons <kristaps@bsd.lv>
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
#if HAVE_SANDBOX_INIT
# include <sandbox.h>
#endif
#if HAVE_ERR
# include <err.h>
#endif
#include <expat.h>
#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#if HAVE_PLEDGE
# include <unistd.h>
#endif

#include "extern.h"

enum	op {
	OP_JOIN,
	OP_EXTRACT,
	OP_UPDATE
};

#if HAVE_SANDBOX_INIT
static void
sandbox(void)
{
	char	*ep;
	int	 rc;

	rc = sandbox_init(kSBXProfileNoNetwork, SANDBOX_NAMED, &ep);
	if (0 == rc)
		return;
	perror(ep);
	sandbox_free_error(ep);
	exit(EXIT_FAILURE);
}
#elif HAVE_PLEDGE
static void
sandbox(void)
{

	if (-1 == pledge("stdio rpath", NULL))
		err(EXIT_FAILURE, "pledge");
}
#else
static void
sandbox(void)
{
	/* Do nothing at all. */
}
#endif

int
main(int argc, char *argv[])
{
	int		 ch, rc, keep = 0, copy = 0, quiet = 0;
	const char	*xliff = NULL;
	enum op	 	 op = OP_EXTRACT;
	XML_Parser	 p;

	sandbox();

	while (-1 != (ch = getopt(argc, argv, "cej:kqu:")))
		switch (ch) {
		case 'c':
			copy = 1;
			break;
		case 'e':
			op = OP_EXTRACT;
			xliff = NULL;
			break;
		case 'k':
			keep = 1;
			break;
		case 'j':
			op = OP_JOIN;
			xliff = optarg;
			break;
		case 'q':
			quiet = 1;
			break;
		case 'u':
			op = OP_UPDATE;
			xliff = optarg;
			break;
		default:
			goto usage;
		}

	argc -= optind;
	argv += optind;

	if (NULL == (p = XML_ParserCreate(NULL)))
		errx(EXIT_FAILURE, "XML_ParserCreate");

	switch (op) {
	case (OP_EXTRACT):
		rc = extract(p, copy, argc, argv);
		break;
	case (OP_JOIN):
		assert(NULL != xliff);
		rc = join(xliff, p, argc, argv);
		break;
	case (OP_UPDATE):
		assert(NULL != xliff);
		rc = update(xliff, p, copy, 
			keep, quiet, argc, argv);
		break;
	default:
		abort();
	}

	XML_ParserFree(p);
	return rc ? EXIT_SUCCESS : EXIT_FAILURE;

usage:
	fprintf(stderr, "usage: %s [-cekq] "
		"[-j xliff] [-u xliff] html5...\n", getprogname());
	return EXIT_FAILURE;
}
