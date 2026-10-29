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
/* show values as (running total: in the first row field; rank: largest to smallest among the
   items of the same parent, as GridCraft has them) */
enum { PS_NORMAL, PS_PCT_GRAND, PS_PCT_COL, PS_PCT_ROW, PS_RUNTOTAL, PS_RANK, PS_N };
/* a field's items grouped: dates by years, quarters, months or days; numbers by intervals */
enum { PG_NONE, PG_YEARS, PG_QUARTERS, PG_MONTHS, PG_DAYS, PG_RANGE, PG_N };
/* report layout */
enum { PL_COMPACT, PL_OUTLINE, PL_TABULAR };
/* item order in a Rows or Columns field */
enum { PO_ASC, PO_DESC, PO_SOURCE };

/* items grouped by hand under a name of their own (Group Selection) */
typedef struct { char *name; char **items; int n; } PivotGroup;
typedef struct {
  char *name;                     /* the source column's heading (UTF-8) */
  u8 sort;                        /* PO_ */
  char **hidden; int nhidden;     /* items left out (their labels as the report shows them) */
  u8 group;                       /* PG_: the items are the groups (a date field can be in an area
                                     once for each of years, quarters, months and days) */
  double gstart, gend, gstep;     /* PG_RANGE: from, to, by */
  PivotGroup *mg; int nmg;        /* items grouped by hand (their labels; the rest stay themselves) */
  char **collapsed; int ncollapsed;   /* items whose detail is hidden (Expand/Collapse) */
} PivotField;
/* a calculated field: a formula over other fields ("=Sales*0.1", "='Unit Price'*Units"), worked
   out as Excel does on the sums of the fields it names for each report cell */
typedef struct { char *name, *formula; } PivotCalc;
/* a +/- button of the last report: the item in cell r, c of a Rows (area PA_ROWS) or Columns
   field fi, its label, and whether it is collapsed */
typedef struct { i32 r, c; u8 area, fi, collapsed; char *label; } PivotBtn;
typedef struct {
  char *name;                     /* the source column */
  u8 func, showas;                /* PF_, PS_ */
  char *caption;                  /* "Sum of Sales"; NULL: made from func and name */
} PivotValue;
/* what each row of the last report was (for its look) */
enum { PK_DATA, PK_SUB, PK_GRAND, PK_HEAD, PK_FILTER, PK_BLANK, PK_ITEM, PK_COLL };   /* PK_COLL: a collapsed item */
typedef struct LPivot {
  char name[64];                  /* "PivotTable1" */
  char source[256];               /* "Sheet1!$A$1:$D$40" or a table's name (UTF-8) */
  i32 r, c;                       /* the report body's top left (filters go above it) */
  PivotField *rows, *cols, *filters; int nrows, ncols, nfilters;
  PivotValue *vals; int nvals;
  PivotCalc *calcs; int ncalcs;   /* calculated fields (Values may name them) */
  PivotField *sfilters; int nsfilters;  /* slicers' filters: hidden items of source fields (raw labels), not shown */
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
  PivotBtn *btns; int nbtns;      /* the +/- buttons */
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

/* ------------------------------------------------------------------ grouping, calculated fields, expand/collapse */
/* a value's label under a field's grouping (dates, intervals, groups by hand) */
void    pivot_field_label(Book *b, const PivotField *f, const Value *v, char *out, size_t cap);
/* a field's items as the report labels them under its grouping (report order); f NULL: the raw
   items of source field name (what slicers list) */
int     pivot_field_items_of(Book *b, const LPivot *p, const char *name, const PivotField *f, char ***out);
/* the name an area's field shows in headers ("Years" for a date field's coarser levels) */
void    pivot_field_caption(const LPivot *p, int area, int i, char *out, size_t cap);
/* what a source field holds: 0 text (or mixed), 1 numbers, 2 dates */
int     pivot_field_type(Book *b, const LPivot *p, const char *name, double *mn, double *mx);
/* field i of area (PA_ROWS, PA_COLS) grouped: dates by levels (a mask of 1 << PG_YEARS ...: one
   field for each, coarsest first), numbers from start to end by step, or items by hand (under
   name); ungrouped (every level of it back to one field). 1 if done */
int     pivot_group_dates(LPivot *p, int area, int i, unsigned levels);
int     pivot_group_range(LPivot *p, int area, int i, double start, double end, double step);
int     pivot_group_items(LPivot *p, int area, int i, const char *name, char *const *items, int n);
int     pivot_ungroup(LPivot *p, int area, int i);
/* the next free "Group1" name of a field */
void    pivot_group_name(const PivotField *f, char *out, size_t cap);
/* an item collapsed (its detail hidden) or expanded; collapse/expand every item of a field */
int     pivot_set_collapsed(PivotField *f, const char *label, int collapsed);
int     pivot_collapse_field(Book *b, LPivot *p, int area, int i, int collapsed);
int     pivot_is_collapsed(const PivotField *f, const char *label);
/* the button of the last report at cell r, c (and its pivot), or NULL */
PivotBtn *pivot_button_at(Book *b, int sheet, i32 r, i32 c, LPivot **pp);
/* the button's item expanded or collapsed and the report worked out again (an undo step) */
int     book_pivot_toggle(Book *b, int sheet, i32 r, i32 c);
/* a calculated field added (or its formula replaced); removed. 0 (with *why) if the formula
   doesn't parse or names no field */
int     pivot_calc_set(Book *b, LPivot *p, const char *name, const char *formula, const char **why);
int     pivot_calc_remove(LPivot *p, const char *name);
int     pivot_calc_index(const LPivot *p, const char *name);

/* ------------------------------------------------------------------ Recommended PivotTables, previews, PivotCharts */
typedef struct { char title[300]; LPivot def; } PivotSuggest;
/* up to max suggestions for source (as GridCraft makes them: a category by a number, two
   categories, a date by quarters, two numbers, a count, an average); how many. Free each def */
int     pivot_recommend(Book *b, int sheet, const char *source, PivotSuggest *out, int max);
/* the report of a definition worked out without writing it: rows x cols texts (UTF-8, malloc'd in a
   malloc'd array, row by row), at most maxrows rows. 0 if it can't be */
int     pivot_preview(Book *b, const LPivot *p, int maxrows, char ***cells, int *rows, int *cols);
/* "Sheet2!PivotTable1": what a PivotChart keeps to find its pivot table; and back */
void    pivot_chart_link(Book *b, int sheet, const LPivot *p, char *out, size_t cap);
LPivot *pivot_by_link(Book *b, const char *link, int *sheet);
/* a PivotChart of pivot pi (kind CK_, at the report's right): its object's index, -1 (and *why) if
   the pivot has no values and rows or columns. An undo step */
int     book_pivot_chart_add(Book *b, int sheet, int pi, int kind, const char **why);

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
/* a field added to Rows or Columns at the end even if it is there already (a date field's levels:
   group PG_), as files have them */
int     pivot_def_add_level(LPivot *p, int area, const char *field, int group);
/* a copy of a definition, and one freed (not the struct itself) */
int     pivot_copy1(LPivot *d, const LPivot *s);
void    pivot_free1(LPivot *p);

#endif
