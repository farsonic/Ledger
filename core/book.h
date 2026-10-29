/*
 * book.h - Ledger's workbook: values, cells, sheets, styles, defined names and undo.
 * Portable C99 (the native tests and vbcc for the Amiga). Numbers are doubles: the 68040 has an FPU.
 *
 * A sheet keeps its cells by row: rows[r] holds the cells of row r sorted by column, so a sheet of
 * a few thousand cells costs a few thousand cells however far apart they are. Rows and columns
 * count from 0 here (row 0 is "1", column 0 is "A").
 */
#ifndef LEDGER_BOOK_H
#define LEDGER_BOOK_H

#include "util.h"

#define MAXROW 1048576            /* Excel's grid: 1,048,576 rows by 16,384 columns (XFD) */
#define MAXCOL 16384
#define MAXSHEETS 255

/* ------------------------------------------------------------------ values */
enum { V_EMPTY, V_NUM, V_STR, V_BOOL, V_ERR, V_REF, V_ARR };
/* errors, in Excel's order (their ERROR.TYPE numbers); E_CIRC shows as #CALC! (a circular reference) */
enum { E_NONE, E_NULL, E_DIV0, E_VALUE, E_REF, E_NAME, E_NUM, E_NA, E_GETTING, E_SPILL, E_CALC, E_N };

typedef struct Arr Arr;
/* a value: a number, a string (UTF-8, owned: val_free frees it), TRUE/FALSE, an error, a reference
   to a rectangle of cells (in the evaluator only) or an array (array constants and array results) */
typedef struct {
  u8 t;                           /* V_ */
  u8 e;                           /* V_ERR: E_; V_BOOL: 0 or 1 */
  u16 sheet;                      /* V_REF: the sheet */
  double n;                       /* V_NUM */
  char *s;                        /* V_STR */
  i32 r1, c1, r2, c2;             /* V_REF: the rectangle, r1 <= r2, c1 <= c2 */
  Arr *a;                         /* V_ARR (owned) */
} Value;
struct Arr { int rows, cols; Value *v; };     /* row by row */

Value  val_num(double n);
Value  val_str(const char *s);             /* copies s */
Value  val_strn(const char *s, size_t n);
Value  val_bool(int b);
Value  val_err(int e);
Value  val_empty(void);
Value  val_copy(const Value *v);           /* a deep copy */
void   val_free(Value *v);                 /* frees what it owns and leaves it empty */
Arr   *arr_new(int rows, int cols);        /* every element empty; NULL if too big or out of memory */
void   arr_free(Arr *a);
const char *err_name(int e);               /* "#DIV/0!" ... */
int    err_parse(const char *s, int *len); /* an error literal at s: its E_ and length; 0 if none */
/* text to a number as Excel's VALUE() reads it ("1,234.5", "50%", "$12", "1e3", dates and times
   such as "2026-10-27" or "3:30 PM"); 0 if it isn't one. *isdate is set when it was a date/time */
int    parse_number(const char *s, double *out, int *isdate);
/* a number written the way General format shows it, up to 11 significant digits by default */
void   num_general(double n, char *out, int width);

/* ------------------------------------------------------------------ dates: serial numbers, 1900 system
   (1 = 1 January 1900, with Excel's phantom 29 February 1900 kept so serials match) */
double date_serial(int y, int m, int d);   /* months and days may overflow, as DATE() allows */
void   serial_ymd(double serial, int *y, int *m, int *d);
int    serial_weekday(double serial);      /* 0 Sunday .. 6 Saturday */

/* ------------------------------------------------------------------ formulas */
/* a compiled formula is a sequence of tokens in postfix order (RPN). References keep absolute
   positions with flags saying which parts are relative ($), so copying a formula moves its
   relative parts. T_PAREN does nothing when evaluated: it keeps the user's brackets for printing */
enum {
  T_END, T_NUM, T_STR, T_BOOL, T_ERR, T_REF, T_AREA, T_NAME, T_FUNC, T_MISSING, T_PAREN, T_ARRAY,
  T_ADD, T_SUB, T_MUL, T_DIV, T_POW, T_CAT, T_EQ, T_NE, T_LT, T_GT, T_LE, T_GE,
  T_NEG, T_POS, T_PCT, T_RANGE, T_UNION, T_ISECT,
  T_TABLE                         /* a structured reference (table.c): s is its text, "Table1[Col]", "[@Col]" */
};
/* reference flags: which coordinates have a $, and whole rows/columns (A:A, 1:1) */
enum { RF_ABSR1 = 1, RF_ABSC1 = 2, RF_ABSR2 = 4, RF_ABSC2 = 8, RF_COLS = 16, RF_ROWS = 32, RF_SHEET = 64 };
#define SHEET_SELF 0xFFFF        /* a reference without a sheet name: the formula's own sheet */
typedef struct {
  u8 op;                          /* T_ */
  u8 flags;                       /* T_REF/T_AREA: RF_; T_FUNC: argument count */
  u16 a;                          /* T_REF/T_AREA: sheet (SHEET_SELF); T_FUNC: function id; T_BOOL/T_ERR: value */
  i32 r1, c1, r2, c2;             /* T_REF uses r1, c1 */
  double n;                       /* T_NUM */
  char *s;                        /* T_STR, T_NAME (owned) */
  Arr *arr;                       /* T_ARRAY (owned) */
} Tok;
typedef struct { Tok *t; int n; int volatile_; } Formula;

/* ------------------------------------------------------------------ styles */
typedef struct {
  char name[32];                  /* font family (UTF-8) */
  u16 size;                       /* tenths of a point (110 = 11 pt) */
  u8 bold, italic, underline, strike;   /* underline: 0 none, 1 single, 2 double */
  u8 vert;                        /* 0 normal, 1 superscript, 2 subscript */
  u32 color;                      /* 0xRRGGBB, or COLOR_AUTO */
} Font;
#define COLOR_AUTO 0xFF000000UL
#define FILL_ON 0x01000000UL       /* a fill colour is FILL_ON | 0xRRGGBB; 0 means no fill */
enum { BS_NONE, BS_THIN, BS_MEDIUM, BS_THICK, BS_DOUBLE, BS_DOTTED, BS_DASHED, BS_HAIR };
enum { HA_GENERAL, HA_LEFT, HA_CENTER, HA_RIGHT, HA_FILL, HA_JUSTIFY, HA_CENTERSEL };
enum { VA_BOTTOM, VA_CENTER, VA_TOP };
enum { B_LEFT, B_RIGHT, B_TOP, B_BOTTOM };
/* a cell's look. Cells refer to the book's style table by index; 0 is the default style */
typedef struct {
  u16 font;                       /* Book.fonts */
  u16 numfmt;                     /* number format id: built-in (< 164) or Book.fmts */
  u32 fill;
  u8 border[4];                   /* B_ order: BS_ */
  u32 bcolor[4];
  u8 halign, valign, wrap, indent, shrink;
  i16 rotation;                   /* degrees, -90..90 (0 none) */
  u8 locked, hidden;
} Style;

/* ------------------------------------------------------------------ cells, sheets, the book */
typedef struct {
  u16 col;
  u16 style;
  Value v;                        /* the value (for a formula, its last result) */
  Formula *f;                     /* NULL: a constant */
  u32 calcgen;                    /* the recalculation that last computed it */
  u8 calcstate;                   /* 0 idle, 1 being computed (circularity check) */
} Cell;
typedef struct { Cell *c; int n, cap; u16 height; u16 style; u8 hidden, custom, filtered; } Row;   /* filtered: hidden by the AutoFilter */
typedef struct { u16 width; u16 style; u8 hidden, custom; } ColInfo;
typedef struct { i32 r1, c1, r2, c2; } Rect;
/* an AutoFilter column's choice: the shown values (as the cells show them, UTF-8) that pass, and
   whether empty cells do */
typedef struct { i32 col; int n; char **vals; u8 blanks; } FilterCol;
/* a conditional format rule (cond.c): the cells it covers, what it tests and the look it gives.
   Formulas are as seen from the first area's top left cell: their relative references move with
   the cell tested. Rules earlier in Sheet.cfs come first (Excel's priority 1 is cfs[0]) */
enum { CF_CELL, CF_TEXT, CF_NOTTEXT, CF_BEGINS, CF_ENDS, CF_DUP, CF_UNIQUE, CF_TOP, CF_AVERAGE, CF_BLANKS, CF_NOBLANKS,
       CF_ERRORS, CF_NOERRORS, CF_EXPR, CF_BAR, CF_SCALE, CF_N };
enum { CO_BETWEEN, CO_NOTBETWEEN, CO_EQ, CO_NE, CO_GT, CO_LT, CO_GE, CO_LE, CO_N };
/* a note on a cell (Excel's notes, once called comments): who wrote it and what it says (UTF-8).
   A threaded comment (comments.h) is a note with threaded set and its replies */
typedef struct { i32 r, c; char *author, *text; u8 threaded, visible, resolved; char *replies; } Note;
/* a hyperlink on a cell: a web or mail address, or a place in the workbook ("#Sheet2!A1"), and its tip */
typedef struct { i32 r, c; char *target, *tip; } Link;
typedef struct CondCache CondCache;
struct LTable; struct LPivot;    /* tables (table.h) and pivot tables (pivot.h) */
typedef struct {
  u8 type, op, stop;              /* CF_, CO_ (CF_CELL), stop if true */
  u8 bottom, percent;             /* CF_TOP: the bottom ones, rank is a percentage */
  u8 below, equal;                /* CF_AVERAGE: below the average, or equal to it too */
  i32 rank;                       /* CF_TOP: how many (or what per cent) */
  Rect *areas; int nareas;
  Formula *f1, *f2;               /* CF_CELL: the value(s) compared with; CF_EXPR: the formula */
  char *text;                     /* CF_TEXT, CF_NOTTEXT, CF_BEGINS, CF_ENDS (UTF-8) */
  u32 fill, color;                /* the look: fill (0 none, else FILL_ON | rgb), font colour (COLOR_AUTO: unchanged) */
  u8 bold, italic, underline, strike;   /* 1 on, 0 unchanged */
  u32 bar[3];                     /* CF_BAR: bar colour in [0]; CF_SCALE: low, middle, high colours */
  u8 three;                       /* CF_SCALE: three colours (else two) */
  CondCache *cache;               /* (worked out when drawn: duplicates, ranks, averages, ranges) */
} CondRule;

typedef struct {
  char name[32];                  /* UTF-8, at most 31 characters as in Excel */
  Row **rows; i32 nrows;          /* rows[r] (NULL: empty) for r < nrows */
  ColInfo *cols; i32 ncols;       /* column info for c < ncols; the rest are default */
  u16 defwidth, defheight;        /* default column width and row height, in pixels (64, 20) */
  Rect *merges; int nmerges;
  i32 freezer, freezec;           /* frozen panes: rows above, columns left (0: none) */
  u32 tabcolor;                   /* 0 none, else FILL_ON | rgb */
  Rect filter;                    /* the AutoFilter's range, its heading row first (r1 -1: none) */
  FilterCol *fcols; int nfcols;   /* the columns filtered */
  CondRule *cfs; int ncfs;        /* conditional formatting, first rule first */
  Note *notes; int nnotes;        /* notes on cells, in no order */
  Link *links; int nlinks;        /* hyperlinks on cells, in no order */
  struct LTable *tables; int ntables;   /* tables (Format as Table), table.h */
  struct LPivot *pivots; int npivots;   /* pivot tables whose report is on this sheet, pivot.h */
  u8 hidden, gridlines, headings, showzeros, rtl;
  i32 cur_r, cur_c;               /* where the cursor was, and the selection */
  Rect sel;
  i32 top, left;                  /* the scroll position when last shown */
  u16 zoom;                       /* per cent */
  struct PageSetup *page;         /* page setup (pagesetup.h), NULL: the defaults */
} Sheet;

typedef struct { char name[64]; u16 sheet; char *formula_text; Formula *f; } Name;   /* sheet: SHEET_SELF = workbook scope */

/* undo: each step holds the cells, sheets or whole book as they were before */
typedef struct UndoStep UndoStep;

typedef struct {
  Sheet **sh; int nsh, cap;
  int active;                     /* the sheet shown */
  Font *fonts; int nfonts;
  Style *styles; int nstyles;
  char **fmts; int nfmts;         /* custom number formats: id 164 + i */
  Name *names; int nnames;
  u32 calcgen;
  u32 changes;                    /* counts edits (conditional formatting's caches check it) */
  int manualcalc;                 /* automatic recalculation off */
  int date1904;
  int dirty;                      /* unsaved changes */
  char title[128], author[64];
  UndoStep *undo, *redo;          /* stacks (newest first) */
  int undo_depth;
  UndoStep *open;                 /* the step being recorded (book_begin .. book_end) */
  int nesting;
  double now_override;            /* tests: NOW() returns this when non-zero */
  struct LayoutBook *lx;          /* more properties, the theme, calculation options (pagesetup.h), NULL: defaults */
} Book;

Book  *book_new(void);                    /* one empty sheet, "Sheet1" */
void   book_free(Book *b);
int    book_add_sheet(Book *b, const char *name, int at);   /* index, or -1 (bad or taken name, full) */
int    book_delete_sheet(Book *b, int i);
int    book_duplicate_sheet(Book *b, int i);                  /* the copy's index, or -1 */
/* defined names: name made or changed to stand for text (a formula without "=", e.g. "Sheet1!$A$1:$B$4")
   for the workbook (sheet SHEET_SELF) or one sheet; 0 if the name or the formula is no good */
int    book_define_name(Book *b, const char *name, const char *text, int sheet);
int    book_delete_name(Book *b, int i);
int    book_rename_sheet(Book *b, int i, const char *name);
int    book_move_sheet(Book *b, int from, int to);
int    book_find_sheet(Book *b, const char *name);          /* -1 if none (case-insensitive) */
void   book_unique_sheet_name(Book *b, const char *base, char *out);   /* "Sheet4", "Data (2)" */

/* cells */
Cell  *sheet_cell(Sheet *s, i32 r, i32 c);                  /* NULL if empty */
Cell  *sheet_cell_make(Sheet *s, i32 r, i32 c);             /* created if absent; NULL out of memory */
void   sheet_cell_clear(Sheet *s, i32 r, i32 c);            /* removes it altogether */
i32    sheet_last_row(Sheet *s);                            /* -1 if the sheet is empty */
i32    sheet_last_col(Sheet *s);
int    sheet_cell_style(Sheet *s, i32 r, i32 c);            /* the cell's, else its row's, else its column's */
u16    sheet_col_width(Sheet *s, i32 c);                     /* pixels (0 when hidden) */
u16    sheet_row_height(Sheet *s, i32 r);
int    sheet_merge_at(Sheet *s, i32 r, i32 c, Rect *m);     /* the merged area holding r,c, if any */

/* editing through the book (recorded for undo; formulas are recomputed unless manual calc) */
void   book_begin(Book *b);               /* group the following changes into one undo step */
void   book_end(Book *b);
void   book_note_sheet(Book *b, int sheet);   /* (inside a step) the whole sheet noted for undo before it changes */
void   book_note_book(Book *b);               /* (inside a step) every sheet noted for undo (formulas everywhere change) */
/* what the user typed into a cell: a formula ("=..."), a number, date, percentage, TRUE/FALSE, an
   error, or text ("'..." forces text). An empty string clears the value (keeping the format).
   Typing a date or percentage into a General cell gives it a matching number format, as in Excel.
   Returns 0 if a formula doesn't parse (*errpos: where; nothing is changed) */
int    book_input(Book *b, int sheet, i32 r, i32 c, const char *text, int *errpos);
int    book_set_value(Book *b, int sheet, i32 r, i32 c, const Value *v);
int    book_set_formula(Book *b, int sheet, i32 r, i32 c, Formula *f);   /* f is taken (freed on failure) */
int    book_set_style(Book *b, int sheet, i32 r, i32 c, int style);
int    book_clear(Book *b, int sheet, Rect area, int what); /* CLR_ flags */
enum { CLR_CONTENTS = 1, CLR_FORMATS = 2, CLR_ALL = 3 };
int    book_set_col_width(Book *b, int sheet, i32 c, u16 px);
int    book_set_row_height(Book *b, int sheet, i32 r, u16 px);
int    book_hide_cols(Book *b, int sheet, i32 c1, i32 c2, int hide);
int    book_hide_rows(Book *b, int sheet, i32 r1, i32 r2, int hide);
/* rows and columns in and out: formulas everywhere follow (references beyond move, references to
   deleted cells become #REF!) */
int    book_insert_rows(Book *b, int sheet, i32 r, i32 n);
int    book_delete_rows(Book *b, int sheet, i32 r, i32 n);
int    book_insert_cols(Book *b, int sheet, i32 c, i32 n);
int    book_delete_cols(Book *b, int sheet, i32 c, i32 n);
/* insert cells shifting down/right and delete shifting up/left, within the area's columns/rows */
int    book_insert_cells(Book *b, int sheet, Rect area, int down);
int    book_delete_cells(Book *b, int sheet, Rect area, int up);
int    book_merge(Book *b, int sheet, Rect area, int merge);
int    book_freeze(Book *b, int sheet, i32 rows, i32 cols);

/* styles: the index of a style equal to *st (added if new); -1 out of memory */
int    book_style(Book *b, const Style *st);
int    book_font(Book *b, const Font *f);
int    book_fmt(Book *b, const char *code);               /* a number format id for code (built-in ids first) */
const char *book_fmt_code(Book *b, int id);              /* the code for an id ("General" if unknown) */
/* change one aspect of the style of every cell in area: fn edits a copy of each distinct style */
typedef void (*StyleEdit)(Style *st, Font *f, const void *arg);
int    book_restyle(Book *b, int sheet, Rect area, StyleEdit fn, const void *arg);

/* undo/redo; 0 when there is nothing to undo or redo. *sheet and *area get where it happened */
int    book_undo(Book *b, int *sheet, Rect *area);
int    book_redo(Book *b, int *sheet, Rect *area);
int    book_can_undo(Book *b);
int    book_can_redo(Book *b);
void   book_forget_undo(Book *b);      /* after loading: nothing to undo */

/* notes: the one on a cell (NULL none); one written (text NULL or "" takes it off), an undo step */
const Note *sheet_note(Sheet *s, i32 r, i32 c);
int    book_set_note(Book *b, int sheet, i32 r, i32 c, const char *text, const char *author);
/* hyperlinks the same way (target NULL or "" takes it off) */
const Link *sheet_link(Sheet *s, i32 r, i32 c);
int    book_set_link(Book *b, int sheet, i32 r, i32 c, const char *target, const char *tip);
/* where a cell's link goes: its hyperlink, or the first argument of a HYPERLINK() formula; 0 if none */
int    book_cell_link(Book *b, int sheet, i32 r, i32 c, char *out, size_t cap);

/* AutoFilter: buttons on area's first row (on), or none (off, every filtered row shown again) */
int    book_autofilter(Book *b, int sheet, Rect area, int on);
/* column col of the AutoFilter shows only rows whose cell shows one of vals (n of them), and empty
   ones if blanks; n < 0 shows every row again. Rows that fail any column are hidden */
int    book_filter_column(Book *b, int sheet, i32 col, const char *const *vals, int n, int blanks);
/* the AutoFilter's columns tested again (after values changed or a sort) */
int    book_filter_reapply(Book *b, int sheet);
/* the values column col shows in the AutoFilter's rows, each once, sorted (malloc'd strings in a
   malloc'd array: free both); *blanks set if some are empty. Returns how many */
int    book_filter_values(Book *b, int sheet, i32 col, char ***out, int *blanks);
const FilterCol *sheet_filter_col(Sheet *s, i32 col);     /* NULL if col isn't filtered */

/* ------------------------------------------------------------------ addresses */
void   col_name(i32 c, char *out);         /* 0 -> "A", 27 -> "AB" */
int    parse_col(const char *s, i32 *c);   /* letters at s; their count, 0 if none or too big */
/* "B3", "$B$3", "A1:C4", "C:C", "3:5", "Sheet2!A1"; *sheet is SHEET_SELF without a sheet name.
   Returns the characters used, 0 if it isn't a reference */
int    parse_ref(Book *b, const char *s, u16 *sheet, Rect *r, u8 *flags);
void   ref_text(i32 r, i32 c, u8 absr, u8 absc, char *out);
void   rect_text(Rect r, char *out);       /* "A1" or "A1:C4" */

/* formulas (formula.c) */
Formula *formula_parse(Book *b, int sheet, const char *text, int *errpos);   /* text after "=" */
void     formula_free(Formula *f);
Formula *formula_copy(const Formula *f);
/* the formula as text (without "="), as seen from sheet */
void     formula_text(Book *b, int sheet, const Formula *f, Buf *out);
/* move the relative references by dr, dc (copying a formula from one cell to another); references
   pushed off the grid become #REF! */
void     formula_offset(Formula *f, i32 dr, i32 dc);
/* rows/columns inserted (n > 0) or deleted (n < 0) at index at on sheet target, the formula living
   on sheet home */
void     formula_shift(Formula *f, int home, int target, int rows, i32 at, i32 n);
int      func_lookup(const char *name);    /* function id, -1 if unknown */
const char *func_name(int id);

/* calculation (calc.c) */
void   book_recalc(Book *b);               /* everything */
Value  book_eval_text(Book *b, int sheet, const char *formula);   /* evaluate without storing ("1+2") */
Value  book_eval_at(Book *b, int sheet, i32 r, i32 c, const Formula *f);   /* f evaluated as if in cell r, c (never V_REF) */
/* the value of a cell for display or reading: computed if needed. Never V_REF */
const Value *cell_value(Book *b, int sheet, i32 r, i32 c);

#endif
