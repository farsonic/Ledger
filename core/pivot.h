/*
 * pivot.h - pivot tables (Insert > PivotTable): a summary of a source range or table, its rows
 * grouped by the items of the fields put in Rows and Columns, the fields in Values summed (or
 * counted, averaged ...) for each group, with subtotals and grand totals, and report filters.
 *
 * As in GridCraft, a pivot table is its definition plus the cells of its report: refreshing works
 * the report out again from the source and writes it into the cells (plain values; the look is
 * added as the grid draws, like a table's). The definition lives in the report sheet's pivots[].
 */
#ifndef LEDGER_PIVOT_H
#define LEDGER_PIVOT_H

#include "book.h"
#include "table.h"

/* summarize values by */
enum { PF_SUM, PF_COUNT, PF_AVERAGE, PF_MAX, PF_MIN, PF_PRODUCT, PF_COUNTNUMS, PF_STDDEV, PF_STDDEVP, PF_VAR, PF_VARP, PF_N };
/* show values as */
enum { PS_NORMAL, PS_PCT_GRAND, PS_PCT_COL, PS_PCT_ROW, PS_N };
/* report layout */
enum { PL_COMPACT, PL_OUTLINE, PL_TABULAR };
/* item order in a Rows or Columns field */
enum { PO_ASC, PO_DESC, PO_SOURCE };

typedef struct {
  char *name;                     /* the source column's heading (UTF-8) */
  u8 sort;                        /* PO_ */
  char **hidden; int nhidden;     /* items left out (their labels as the report shows them) */
} PivotField;
typedef struct {
  char *name;                     /* the source column */
  u8 func, showas;                /* PF_, PS_ */
  char *caption;                  /* "Sum of Sales"; NULL: made from func and name */
} PivotValue;
/* what each row of the last report was (for its look) */
enum { PK_DATA, PK_SUB, PK_GRAND, PK_HEAD, PK_FILTER, PK_BLANK, PK_ITEM };
typedef struct LPivot {
  char name[64];                  /* "PivotTable1" */
  char source[256];               /* "Sheet1!$A$1:$D$40" or a table's name (UTF-8) */
  i32 r, c;                       /* the report body's top left (filters go above it) */
  PivotField *rows, *cols, *filters; int nrows, ncols, nfilters;
  PivotValue *vals; int nvals;
  u8 layout;                      /* PL_ */
  u8 subtotals, subtop;           /* subtotals shown; at the top of their group (compact, outline) */
  u8 grandrows, grandcols;        /* the Grand Total column (totals of each row) / the Grand Total row */
  u8 blankrows;                   /* a blank row after each item of the outer field */
  u8 bandrows, bandcols, rowheaders, colheaders;
  char style[32];                 /* "PivotStyleLight16" */
  /* the last report */
  Rect out;                       /* every cell written (filters too); r1 -1: none */
  u8 *kind; int nkind;            /* PK_ of each row of out */
  u8 *level;                      /* each row's depth among the row fields (0 grand, 1 outer ...) */
  i32 bodyrow;                    /* the body's first row (headers start there) */
  i32 datarow, datacol;           /* the first row and column of values */
  i32 gtcol;                      /* the first Grand Total column, -1 none */
} LPivot;

/* ------------------------------------------------------------------ the sheet's pivots (book.c's hooks) */
void    pivots_free(struct LPivot *p, int n);
struct LPivot *pivots_copy(const struct LPivot *p, int n);
void    pivots_shift(Sheet *s, int rows, i32 at, i32 n);
int     pivot_look(Book *b, int sheet, i32 r, i32 c, TblLook *out);   /* (table_look's for pivots) */

/* ------------------------------------------------------------------ finding them */
LPivot *pivot_at(Book *b, int sheet, i32 r, i32 c);           /* the pivot whose report holds a cell */
int     pivot_index(Book *b, int sheet, const LPivot *p);
void    pivot_unique_name(Book *b, char *out);
/* the source's sheet and range (heading row first); 0 if it doesn't resolve */
int     pivot_source(Book *b, const LPivot *p, int *sheet, Rect *r);
/* the source's field names (headings; empty ones Column1 ...; malloc'd array of malloc'd UTF-8) */
int     pivot_fields(Book *b, const LPivot *p, char ***out);
/* the distinct items of a field as the report labels them (sorted; malloc'd array of malloc'd) */
int     pivot_items(Book *b, const LPivot *p, const char *field, char ***out);
void    pivot_strings_free(char **v, int n);
/* an item's label as the report shows it and filters name it ("(blank)" for an empty cell) */
void    pivot_item_label(Book *b, const Value *v, char *out, size_t cap);
/* a value field's caption ("Sum of Sales") */
void    pivot_caption(const PivotValue *v, char *out, size_t cap);
const char *pivot_func_name(int func);    /* "Sum", "Count" ... */

/* ------------------------------------------------------------------ editing (each an undo step) */
/* a new, empty pivot table of source with its report at sheet r, c; its index in the sheet's
   pivots[], -1 if the source doesn't resolve */
int     book_pivot_add(Book *b, int sheet, const char *source, i32 r, i32 c);
/* the report worked out again and written (after the definition changed or the source did); 0
   if the source doesn't resolve or the report would be too big. *why gets a reason (English) */
int     book_pivot_refresh(Book *b, int sheet, int pi, const char **why);
void    book_pivots_refresh_all(Book *b);       /* every pivot table (after loading) */
/* the report and the definition gone */
int     book_pivot_delete(Book *b, int sheet, int pi);
/* the definition as a whole replaced by a copy of np (then refreshed): what the field list and the
   design options use */
int     book_pivot_set(Book *b, int sheet, int pi, const LPivot *np);
/* the areas: 0 rows, 1 columns, 2 values, 3 filters. A field added (at the end), removed, moved */
enum { PA_ROWS, PA_COLS, PA_VALUES, PA_FILTERS };
int     pivot_def_add(LPivot *p, int area, const char *field, int func);
int     pivot_def_remove(LPivot *p, int area, int i);
int     pivot_def_move(LPivot *p, int area, int i, int to_area, int to);
int     pivot_def_has(const LPivot *p, const char *field);    /* the field is in some area */
/* a copy of a definition, and one freed (not the struct itself) */
int     pivot_copy1(LPivot *d, const LPivot *s);
void    pivot_free1(LPivot *p);

#endif
