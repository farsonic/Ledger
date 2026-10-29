/*
 * cond.h - conditional formatting: rules that change a cell's look by what it holds (greater than,
 * text containing, duplicates, top ten, above average, blanks, errors, a formula of your own) and
 * the data bars and colour scales that show values graphically. The rules live in Sheet.cfs (book.h).
 */
#ifndef LEDGER_COND_H
#define LEDGER_COND_H
#include "book.h"

/* what the rules make of one cell: the parts no rule set keep the cell's own */
typedef struct {
  u32 fill;                       /* 0: the cell's own, else FILL_ON | rgb */
  u32 color;                      /* COLOR_AUTO: the cell's own */
  u8 bold, italic, underline, strike;   /* 1: turned on */
  int bar;                        /* a data bar's length in thousandths of the cell's width (-1: none) */
  u32 barcolor;
} CondLook;

void cond_rule_init(CondRule *r);
void cond_rule_free(CondRule *r);                         /* what it holds (not r itself) */
int  cond_rule_copy(CondRule *dst, const CondRule *src);  /* a deep copy without the cache; 0 out of memory */
void cond_free_all(CondRule *r, int n);                   /* an array of rules and the array */
CondRule *cond_copy_all(const CondRule *r, int n);        /* NULL if n is 0 or out of memory */
int  cond_covers(const CondRule *r, i32 row, i32 col);
/* the look the rules give cell r, c; 0 if none applies (out still set to "nothing") */
int  cond_look(Book *b, int sheet, i32 r, i32 c, CondLook *out);
/* rows or columns inserted (n > 0) or deleted (n < 0) at at: the rules' cells follow (book.c) */
void cond_shift_areas(Sheet *s, int rows, i32 at, i32 n);

/* editing, each an undo step: a rule added (copied) at priority place at (0 first; -1 last), one
   replaced, deleted or moved, and rules taken off an area (NULL: the whole sheet; returns how many
   rules changed) */
int  book_cond_add(Book *b, int sheet, const CondRule *rule, int at);
int  book_cond_replace(Book *b, int sheet, int i, const CondRule *rule);
int  book_cond_delete(Book *b, int sheet, int i);
int  book_cond_move(Book *b, int sheet, int i, int to);
int  book_cond_clear(Book *b, int sheet, const Rect *area);

/* a value typed into a rule's box: "=formula", a number or date, or text. NULL if it doesn't parse */
Formula *cond_value(Book *b, int sheet, const char *typed);
/* and back for editing: 5, abc, =A1 */
void cond_value_text(Book *b, int sheet, const Formula *f, char *out, size_t cap);
/* the rule in words ("Cell value > 5"), and its cells ("A1:A10 C1:C5") */
void cond_describe(Book *b, int sheet, const CondRule *r, char *out, size_t cap);
void cond_areas_text(const CondRule *r, char *out, size_t cap);
/* cells given as "A1:A10 C1:C5" (or with commas) into r's areas; 0 if they're no good */
int  cond_parse_areas(Book *b, const char *s, CondRule *r);
#endif
