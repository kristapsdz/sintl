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

enum	fragtype {
	FRAG_ROOT, /* root of fragment tree */
	FRAG_TEXT, /* text node */
	FRAG_NODE /* element node */
};

/*
 * Tree to be extracted or translated (origin HTML5), tree of content
 * identifying a translation (XLIFF), or tree of a translation itself
 * (XLIFF).
 */
struct	frag {
	char		 *val; /* element name or text data */
	size_t		  valsz; /* string length of valsz */
	int		  node_closed; /* if node, whether closed */
	int		  has_nonws; /* if text, whether has non-ws */
	int		  is_null; /* if node, whether is null */
	char		**atts; /* if node, attributes */
	enum fragtype	  type; /* type of node */
	struct frag	**child; /* array of child nodes */
	size_t		  childsz; /* number of child nodes */
	struct frag	 *next; /* next node */
	struct frag	 *parent; /* parent (NULL if root) */
	size_t		  id; /* index in fragseq->elemsz */
};

/*
 * Sequence of fragments.
 */
struct	fragseq {
	struct frag	 *root; /* root of fragment tree */
	struct frag	 *cur; /* current node in fragment parse */
	char		 *copy; /* verbatim copy of all text */
	size_t		  copysz; /* length of copy */
	struct frag	**elems;
	size_t		  elemsz;
};

/*
 * A key-value pair for translation.
 */
struct	xliff {
	char		*source; /* key */
	struct fragseq	 target; /* target */
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
	struct fragseq	  frag;
	struct stack	  stack[64]; /* stack of contexts */
	size_t		  stacksz; /* stack size */
	const struct xparse *xp;
	char	 	 *lang; /* <html> language definition */
};

enum	xnesttype {
	NEST_TARGET,
	NEST_SOURCE
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
	struct fragseq	  frag;
	char		 *source; /* current source in segment */
	struct fragseq	  target; /* current target in segment */
	size_t		  nest; /* nesting in extraction */
	enum xnesttype	  nesttype; /* type of nesting */
	char	 	 *srclang; /* <xliff> srcLang definition */
	char	 	 *trglang; /* <xliff> trgLang definition */
};

__BEGIN_DECLS

int	 extract(XML_Parser, int, int, char *[]);
int	 join(const char *, XML_Parser, int, char *[]);
int	 update(const char *, XML_Parser, int, int, char *[]);

void	 frag_node_start(struct fragseq *, 
		const XML_Char *, const XML_Char **, int);
void	 frag_node_text(struct fragseq *,
	 	const XML_Char *, size_t, int);
void	 frag_node_end(struct fragseq *, const XML_Char *);
char	*frag_serialise(const struct fragseq *, int, int *);
void	 frag_print_merge(const struct fragseq *, 
		const char *, const struct fragseq *);
void	 fragseq_clear(struct fragseq *);

void	 results_extract(struct hparse *, int);
void	 results_update(struct hparse *, int);

__END_DECLS

#endif 
