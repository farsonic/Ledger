/*
 * table.h - tables (Home > Format as Table, Insert > Table): a named block of cells with a header
 * row, an optional totals row, banding and a style from the built-in gallery, and the structured
 * references formulas use to point into them (Table1[Sales], Table1[[#Headers],[Sales]],
 * [@Sales], Table1[#Totals] ...).
 *
 * A table lives in its sheet's tables[] (book.h). Its look is worked out as the grid draws (the
 * cells keep their own formatting, as in Excel, which also applies the table's style itself). The
 * totals row holds real SUBTOTAL formulas, so files open the same in Excel and LibreOffice.
 */
#ifndef LEDGER_TABLE_H
#define LEDGER_TABLE_H

#include "book.h"

/* a totals row function (Excel's totalsRowFunction) */
enum { TF_NONE, TF_SUM, TF_AVERAGE, TF_COUNT, TF_COUNTNUMS, TF_MAX, TF_MIN, TF_STDDEV, TF_VAR, TF_CUSTOM, TF_N };
typedef struct {
  char *name;                     /* the column's name (UTF-8): the header cell's text */
  u8 totfn;                       /* TF_ */
  char *totlabel;                 /* the totals row's text in this column (TF_NONE), or NULL */
  char *totformula;               /* TF_CUSTOM: the totals row's formula (without "="), or NULL */
  char *calcformula;              /* a calculated column's formula (without "="), or NULL */
} TableCol;
typedef struct LTable {
  char name[64];                  /* unique in the book, as defined names are */
  u32 id;                         /* the file's table id */
  Rect r;                         /* the whole table: header row, data, totals row */
  u8 header, totals;              /* has a header row / a totals row */
  u8 bandrows, bandcols, firstcol, lastcol, filterbtn;
  char style[32];                 /* "TableStyleMedium2"; "" none */
  TableCol *cols; int ncols;      /* r.c2 - r.c1 + 1 of them */
} LTable;

/* ------------------------------------------------------------------ the sheet's tables (book.c's hooks) */
void    tables_free(LTable *t, int n);
LTable *tables_copy(const LTable *t, int n);       /* NULL if n is 0 or out of memory */
/* rows (rows 1) or columns inserted (n > 0) or deleted (n < 0) at at: the tables follow; a table
   wholly deleted goes, deleted columns go from its column list, inserted ones are named */
void    tables_shift(Sheet *s, int rows, i32 at, i32 n);
/* after a sheet was duplicated: its tables renamed so names stay unique in the book */
void    tables_rename_copies(Book *b, int sheet);
/* after loading a file: [@Col] references inside their own table written without its name */
void    tables_tidy_loaded(Book *b);
/* typing into a cell (book_input): before it is stored, whether a table grows by it or a header
   changes (then the sheet is noted for undo; returns non-zero), and after, the table updated */
int     table_input_pre(Book *b, int sheet, i32 r, i32 c, const char *text);
void    table_input_post(Book *b, int sheet, i32 r, i32 c, int how);

/* ------------------------------------------------------------------ finding them */
LTable *table_at(Book *b, int sheet, i32 r, i32 c);            /* the table holding a cell, or NULL */
LTable *table_find(Book *b, const char *name, int *sheet);     /* by name (ignoring case), or NULL */
int     table_index(Book *b, int sheet, const LTable *t);      /* its place in tables[], -1 */
/* the parts of a table: 0 if it has none (data rows always exist) */
int     table_data(const LTable *t, Rect *out);
int     table_header(const LTable *t, Rect *out);
int     table_totals(const LTable *t, Rect *out);
int     table_col(const LTable *t, const char *name);          /* a column's index by name, -1 */
/* a name usable for a new table ("Table1", "Table2" ...) */
void    table_unique_name(Book *b, char *out);
/* a name is acceptable for table t (NULL: a new one): letters, digits, _ and ., not a cell
   reference, not taken by another table or a defined name */
int     table_name_ok(Book *b, const char *name, const LTable *t);

/* ------------------------------------------------------------------ editing (each an undo step) */
/* a table made of area: header says its first row is the headings (else a header row is put in
   above, Column1, Column2 ...). Header cells are made unique text. style "" gives TableStyleMedium2.
   Returns the table's index in the sheet's tables[], -1 if it overlaps another table, a merge or
   a pivot table */
int     book_table_add(Book *b, int sheet, Rect area, int header, const char *style);
int     book_table_rename(Book *b, int sheet, int ti, const char *name);   /* 0 if the name is no good */
/* a new range: the header row stays where it is; 0 if it isn't acceptable */
int     book_table_resize(Book *b, int sheet, int ti, Rect area);
/* the table taken away, its cells left as they are and its formulas' structured references
   written as ordinary ones */
int     book_table_convert(Book *b, int sheet, int ti);
/* the options of Table Design: which (TO_) set on or off */
enum { TO_HEADER, TO_TOTALS, TO_BANDROWS, TO_BANDCOLS, TO_FIRSTCOL, TO_LASTCOL, TO_FILTERBTN };
int     book_table_option(Book *b, int sheet, int ti, int which, int on);
int     book_table_style(Book *b, int sheet, int ti, const char *style);
/* the totals row's function under column col (TF_; TF_NONE clears the cell) */
int     book_table_total(Book *b, int sheet, int ti, int col, int fn);
/* rows of the table's data repeating an earlier row (in the columns of mask, a bit per column; 0
   all) taken out, the table shrinking; how many went */
int     book_table_dedupe(Book *b, int sheet, int ti, u32 mask);
/* the table's filter buttons made the sheet's AutoFilter (so a button's list works on it) */
int     table_activate_filter(Book *b, int sheet, i32 r, i32 c);
/* whether the grid shows a filter button on cell r, c of a table (header row, buttons on) */
int     table_filter_button(Sheet *s, i32 r, i32 c);

/* ------------------------------------------------------------------ structured references */
/* the length of a structured reference's bracketed part starting at p ("[...]", nested brackets
   and '-escapes), 0 if there is none or it isn't closed */
int     table_ref_scan(const char *p);
/* the text stored for a structured reference (name and brackets) in its tidy form: "Table1[Col]",
   "[@Col]", "Table1[[#Headers],[A]:[C]]". NULL if it doesn't parse (malloc'd otherwise) */
char   *table_ref_tidy(const char *text);
/* what it refers to, as seen from cell r, c of sheet: a V_REF, or an error */
Value   table_ref_value(Book *b, int sheet, i32 r, i32 c, const char *text);
/* a table's name alone in a formula (=SUM(Table1)): its data; 0 if no table has the name */
int     table_name_value(Book *b, const char *name, Value *out);
/* the text formula_text prints for one (as stored; in file form when table_file_mode is set:
   qualified with the table's name and with [#This Row] for @) */
void    table_ref_text(Book *b, int sheet, const char *text, Buf *out);
extern int table_file_mode;
extern i32 table_home_r, table_home_c;   /* file form: the cell the formula is in (for unqualified ones) */

/* ------------------------------------------------------------------ styles */
/* the built-in gallery: 21 Light, 28 Medium, 11 Dark */
#define TSTYLE_N 60
void    table_style_name(int i, char *out);        /* "TableStyleMedium2" (out: 32 bytes) */
int     table_style_index(const char *name);        /* -1 if not a built-in one */
/* the colours a style draws with (0xRRGGBB; NOCOL none) */
#define NOCOL 0xFF000000UL
typedef struct {
  u32 hfill, hfont;               /* header row: fill, font colour */
  u32 body, band, bodyfont;       /* data rows: fill, band fill, font colour */
  u32 line;                       /* lines between data rows (NOCOL none) */
  u32 grid;                       /* lines between columns too (NOCOL none) */
  u32 outline;                    /* the table's outline (NOCOL none) */
  u32 hline;                      /* the line under the header row */
  u32 tline;                      /* the line over the totals row (double) */
  u32 tfill, tfont;               /* totals row fill and font colour */
  u8 tdouble;                     /* the totals line is double */
  u32 colfill;                    /* first/last column emphasis fill (NOCOL: bold only) */
} TStyle;
void    table_style_colors(const char *name, TStyle *out);
/* what tables and pivot tables do to a cell's look as the grid draws it */
typedef struct {
  u32 fill;                       /* 0 none, else FILL_ON | rgb */
  u32 color;                      /* COLOR_AUTO: the cell's own */
  u8 bold;
  u8 border[4];                   /* B_ order, BS_ (0 none) */
  u32 bcolor[4];
} TblLook;
int     table_look(Book *b, int sheet, i32 r, i32 c, TblLook *out);   /* 0: not in a table or pivot */

/* SUBTOTAL, for totals rows (merged into the function table by func_init; the evaluator's own
   version, if any, replaces it) */
#include "calc.h"
extern const FuncDef table_func_defs[];
extern const int table_func_count;

#endif
