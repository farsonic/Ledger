/*
 * calc.h - the evaluator's helpers shared with the function library (func.c). Not for the UI.
 */
#ifndef LEDGER_CALC_H
#define LEDGER_CALC_H

#include "book.h"

/* where a formula is being evaluated */
typedef struct {
  Book *b;
  int sheet;
  i32 row, col;
  int depth;                      /* nesting of names and cell evaluations */
} Ctx;

/* a function: args are as evaluated (references stay references, missing arguments are V_EMPTY
   with e = 1); it returns a new value (owned by the caller) */
typedef Value (*FuncFn)(Ctx *cx, Value *args, int argc);
typedef struct { const char *name; u8 minargs, maxargs; u8 flags; FuncFn fn; } FuncDef;
enum { FF_VOLATILE = 1, FF_XLFN = 2 };   /* FF_XLFN: files write it with the _xlfn. prefix (new since Excel 2007) */
extern const FuncDef *func_table;   /* every function, in name order (after func_init) */
extern int func_count;
void func_init(void);

/* helpers of func.c the other files of the library share */
typedef struct { double *v; long n, cap; int err; int all; long count_all; } Nums;
int   nums_add(Nums *ns, double d);
/* the numbers of a list of arguments as SUM sees them (all = 1: as AVERAGEA does); 0 or an E_ error.
   ns->v is malloc'd: free it */
int   gather(Ctx *cx, Value *args, int argc, Nums *ns, int all);
Value num_or_err(double d);          /* #NUM! for NaN and infinities */
int   dcmp(const void *a, const void *b);   /* qsort order of doubles */

#define IS_MISSING(v) ((v)->t == V_EMPTY && (v)->e == 1)

/* a reference or array element by element: the value of each cell (computed when needed) */
const Value *ref_cell(Ctx *cx, int sheet, i32 r, i32 c);
/* one value from anything: a single-cell reference is its cell, a range is intersected with the
   formula's row or column (as Excel did before dynamic arrays), an array gives its first element.
   The result is a copy owned by the caller */
Value scalar(Ctx *cx, const Value *v);
/* conversions (on a scalar): 0 on success, else an E_ error */
int   to_num(const Value *v, double *out);          /* text that looks like a number converts; empty is 0 */
int   to_bool(const Value *v, int *out);
void  to_text(Book *b, const Value *v, Buf *out);   /* numbers as General */
/* a value as a number for an argument: scalar, then to_num */
int   arg_num(Ctx *cx, const Value *v, double *out);
int   arg_int(Ctx *cx, const Value *v, long *out);   /* truncated toward zero */
int   arg_bool(Ctx *cx, const Value *v, int *out);
/* text argument into a malloc'd UTF-8 string (*out); 0 or an E_ error */
int   arg_str(Ctx *cx, const Value *v, char **out);
/* walk every value of an argument: scalars once, references cell by cell, arrays element by
   element. fn returns non-zero to stop. direct is 1 for a value typed straight into the call */
typedef int (*EachFn)(Ctx *cx, const Value *v, int direct, void *arg);
int   each_value(Ctx *cx, const Value *v, EachFn fn, void *arg);
/* a reference or array as an array of values (copies), for lookups and array maths; NULL with
   *err set when too large or not a range */
Arr  *to_array(Ctx *cx, const Value *v, int *err);
/* the shape of an argument: rows and columns (1 by 1 for a scalar) */
void  arg_shape(const Value *v, i32 *rows, i32 *cols);
/* element r, c of a reference or array (or the scalar itself), as a pointer valid until the next call */
const Value *arg_at(Ctx *cx, const Value *v, i32 r, i32 c);
/* compare two scalars as Excel's = and < do: numbers < text < FALSE < TRUE, text ignoring case.
   Returns -1, 0, 1 */
int   val_compare(const Value *a, const Value *b);
/* criteria for SUMIF/COUNTIF and the like: ">5", "<>x", "a*b", 7 */
typedef struct { int op; Value v; int wild; int isnum; double n; } Crit;
int   crit_make(Ctx *cx, const Value *arg, Crit *c);
int   crit_match(const Crit *c, const Value *v);
void  crit_free(Crit *c);
int   wild_match(const char *pat, const char *s);   /* * ? and ~, ignoring case */
/* UTF-8 helpers for the text functions */
long  u8_len(const char *s);                         /* characters */
const char *u8_at(const char *s, long i);            /* the i-th character (0-based), or the end */

/* number formatting (numfmt.c): value v in format code; *color gets a [Color] from the code
   (COLOR_AUTO if none). width is the room in characters for General (0 = 11) */
void  fmt_value(Book *b, const char *code, const Value *v, char *out, size_t cap, u32 *color, int width);
int   fmt_is_date(const char *code);                 /* the format shows a date or time */

#endif
