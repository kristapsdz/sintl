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
#ifndef EXTERN_H
#define EXTERN_H

enum	pop {
	POP_JOIN,
	POP_EXTRACT
};

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
 * These switch on the "op".
 * If it is POP_EXTRACT, then we're looking for stuff to translate.
 * Otherwise, we fill in with the translation.
 */
struct	hparse {
	XML_Parser	  p;
	const char	 *fname; /* file being parsed */
	enum pop	  op; /* what we're doing */
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
	size_t		  nest; /* nesting in extraction */
};

__BEGIN_DECLS

int	extract(XML_Parser, int, char *[]);
int	join(const char *, XML_Parser, int, char *[]);
int	update(const char *, XML_Parser, int, char *[]);

void	results_extract(struct hparse *);
void	results_update(struct hparse *);

__END_DECLS

#endif 
