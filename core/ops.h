/*
 * ops.h - the bigger editing operations on ranges of cells, built on book.h: copy and paste, fill,
 * sort, removing duplicates, find and replace, and a cell's text for editing. Each change is one
 * undo step.
 */
#ifndef LEDGER_OPS_H
#define LEDGER_OPS_H

#include "book.h"

/* ------------------------------------------------------------------ copy and paste */
/* what Ledger copied: the cells of a rectangle as they were, independent of the workbook (styles as
   values, so they paste into another workbook too; formulas with where they came from) */
typedef struct {
  u8 has;                         /* the cell had contents */
  Value v;                        /* its value (a formula's last result) */
  Formula *f;                     /* its formula, or NULL */
  Style st; Font font;            /* its look */
  char *fmt;                      /* its number format's code when it is a custom one (else NULL) */
} ClipCell;
typedef struct {
  i32 rows, cols;
  i32 r0, c0;                     /* where it was copied from */
  ClipCell *cells;                /* row by row */
} Clip;
Clip *clip_copy(Book *b, int sheet, Rect area);
void  clip_free(Clip *c);
enum { PASTE_ALL, PASTE_VALUES, PASTE_FORMULAS, PASTE_FORMATS };
/* the clip pasted with its top-left at r, c (transposed: rows become columns). Formulas move as
   copied formulas do (their relative references follow). The area pasted goes in *out. 0 if it
   would run off the grid */
int   book_paste(Book *b, int sheet, i32 r, i32 c, const Clip *cl, int what, int transpose, Rect *out);
/* tab-separated text (rows on lines) put in from r, c as if typed, cell by cell; the area in *out */
int   book_paste_text(Book *b, int sheet, i32 r, i32 c, const char *text, Rect *out);
/* the area as tab-separated text of what the cells show (UTF-8, malloc'd) */
char *book_copy_text(Book *b, int sheet, Rect area);

/* ------------------------------------------------------------------ fill and series */
/* dst holds src and reaches beyond it on one side: the cells beyond are filled from src as the fill
   handle does (series of numbers, dates, numbered text, day and month names continued; formulas
   copied with their references moved; anything else repeated) */
int   book_fill(Book *b, int sheet, Rect src, Rect dst);
/* a linear series down (cols 0) or across the area: the first cell's number plus step each time,
   up to stop (no stop: NAN) */
int   book_series(Book *b, int sheet, Rect area, int across, double step, double stop);

/* ------------------------------------------------------------------ sort, duplicates */
typedef struct { i32 col; int desc; int list; } SortKey;   /* (col: the sheet's column, within the area) */
/* SortKey.list: a custom list's order (Excel's built-in lists): 0 none (A to Z), SL_DAYS the days of the
   week, SL_MONTHS the months (their short or long names, case ignored). Values not in the list come
   after the listed ones, A to Z (GridCraft #3949); empty cells last */
enum { SL_NONE, SL_DAYS, SL_MONTHS };
/* the rows of area (the first one left where it is if header) put in order by the keys: numbers
   before text before TRUE/FALSE before errors, empty cells last whichever way; text ignoring case;
   equal rows keep their order. Formulas move with their rows */
int   book_sort(Book *b, int sheet, Rect area, const SortKey *keys, int nkeys, int header);
/* rows of area whose cells (in cols, a bit per column of area from the left; 0: all) match an
   earlier row's taken out and the rest moved up; how many went */
int   book_remove_duplicates(Book *b, int sheet, Rect area, u32 cols, int header);

/* ------------------------------------------------------------------ find and replace */
enum { FIND_CASE = 1, FIND_WHOLE = 2, FIND_FORMULAS = 4, FIND_BACK = 8, FIND_BOOK = 16 };
/* the next cell after *sheet, *r, *c (row by row) whose text holds what (UTF-8): with FIND_FORMULAS
   its formula, else what it shows; FIND_BOOK goes on into the other sheets. Wraps round; the start
   cell is looked at last. 1 found (the position updated), 0 none */
int   book_find(Book *b, int *sheet, i32 *r, i32 *c, const char *what, int flags);
/* each what in one cell's entry (its formula or value as typed) replaced by with, the cell entered
   again; how many were replaced (0: no change) */
int   book_replace_cell(Book *b, int sheet, i32 r, i32 c, const char *what, const char *with, int flags);
/* every match in the sheet (or the book): how many cells changed */
int   book_replace_all(Book *b, int sheet, const char *what, const char *with, int flags);
/* the same within area of the sheet (Excel's search of a selection of more than one cell: that sheet
   only, whatever FIND_BOOK says); area NULL is the whole sheet (book) as above */
int   book_find_in(Book *b, int *sheet, i32 *r, i32 *c, const char *what, int flags, const Rect *area);
int   book_replace_all_in(Book *b, int sheet, const char *what, const char *with, int flags, const Rect *area);

/* the text that would be typed to make the cell as it is (UTF-8): "=formula", a number in full, a
   date or time as dates are written, a percentage with %, text with ' if it would read as a number */
void  book_edit_text(Book *b, int sheet, i32 r, i32 c, char *out, size_t cap);
/* the cell's value as shown with its number format (UTF-8), width in characters for General */
void  book_shown_text(Book *b, int sheet, i32 r, i32 c, char *out, size_t cap, u32 *color);


/* the references in a formula being typed ("=A1+SUM(B2:C4)"): where each is, how long, and the
   cells it means; text in quotes and function names are skipped. Returns how many (at most max).
   colour[k] numbers the distinct references in order (the same text, the same number) */
typedef struct { int pos, len; u16 sheet; Rect r; int colour; } RefSpan;
int   formula_ref_spans(Book *b, const char *text, RefSpan *out, int max);

#endif
