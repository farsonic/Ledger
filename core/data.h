/*
 * data.h - the Data tab's tools on the workbook (book.h): data validation (valid.c), the outline of
 * grouped rows and columns and subtotals (outline.c), sheet and workbook protection (protect.c),
 * Flash Fill (flash.c), Text to Columns, the Advanced Filter and Consolidate (datatools.c), and the
 * What-If tools: Goal Seek, Data Tables and scenarios (whatif.c). Each change is one undo step.
 *
 * Their behaviour follows GridCraft's Data tab (crates/engine/src/cmd/data*, whatif*, fill.rs), which
 * follows Excel's; the C here is written from that documented behaviour.
 */
#ifndef LEDGER_DATA_H
#define LEDGER_DATA_H

#include "book.h"
#include "xml.h"

/* ------------------------------------------------------------------ data validation (valid.c) */
enum { DV_ANY, DV_WHOLE, DV_DECIMAL, DV_LIST, DV_DATE, DV_TIME, DV_TEXTLEN, DV_CUSTOM, DV_N };
enum { DVE_STOP, DVE_WARNING, DVE_INFO };
/* a rule: the cells it covers, what they may hold, and the messages. op is one of cond.h's CO_
   (between, not between, =, <>, >, <, >=, <=). Formulas are seen from areas[0]'s top left cell, so
   their relative references move with the cell tested; a list's source is a formula too: a string
   ("a,b,c", the items between commas) or a reference or name */
typedef struct DVRule {
  u8 type, op, errstyle;          /* DV_, CO_, DVE_ */
  u8 blank;                       /* empty entries pass (Ignore blank) */
  u8 dropdown;                    /* a list shows its drop-down arrow on the cell */
  u8 showinput, showerror;        /* show the input message when the cell is selected; the error alert after a bad entry */
  Rect *areas; int nareas;
  Formula *f1, *f2;
  char *intitle, *inmsg, *errtitle, *errmsg;   /* UTF-8 ("" none) */
} DVRule;

void dv_rule_init(DVRule *r);               /* any value, blanks pass, drop-down and both messages on */
void dv_rule_free(DVRule *r);               /* what it holds */
int  dv_rule_copy(DVRule *dst, const DVRule *src);   /* deep; 0 out of memory */
/* the rule covering cell r, c (NULL none) */
const DVRule *dv_at(const Sheet *s, i32 r, i32 c);
/* rules taken off the cells of the n areas, then (rule not NULL) rule put on them (copied, its own
   areas replaced by these). An undo step; 0 if nothing could be done */
int  book_dv_set(Book *b, int sheet, const Rect *areas, int n, const DVRule *rule);
/* whether value v (or the typed entry, UTF-8, read as book_input reads it) passes the rule covering
   r, c: 1 passes (or no rule), 0 not; *rule gets the rule (NULL none). A custom formula sees the
   new value in the cell, as in Excel */
int  dv_check_value(Book *b, int sheet, i32 r, i32 c, const Value *v, const DVRule **rule);
int  dv_check_text(Book *b, int sheet, i32 r, i32 c, const char *typed, const DVRule **rule);
/* a list rule's items as the cell at r, c sees them, as shown (malloc'd strings in a malloc'd
   array: free both); how many */
int  dv_list_items(Book *b, int sheet, const DVRule *rule, i32 r, i32 c, char ***out);
/* the cells whose values break their rules (Circle Invalid Data): up to max of them; how many */
int  dv_invalid_cells(Book *b, int sheet, i32 *rows, i32 *cols, int max);
/* a value typed into the dialog's boxes: "=formula", a number, date or time, or text; for a list's
   source (list set) text without "=" is the items, "a,b,c". NULL if it doesn't parse */
Formula *dv_value(Book *b, int sheet, const char *typed, int list);
void dv_value_text(Book *b, int sheet, const Formula *f, int list, char *out, size_t cap);
/* the default error alert's words */
#define DV_DEFAULT_ERROR "This value doesn't match the data validation restrictions defined for this cell."
/* .xlsx: <dataValidations> written for sheet si; read from the element just started (consumed) */
void dv_xlsx_write(Book *b, int si, Buf *o);
void dv_xlsx_read(Book *b, int si, Xml *x);
/* (book.c) the data tools' parts of a sheet given back, copied (dst is a struct copy of src), their
   formulas through fn, and their cells after rows or columns were inserted (n > 0) or deleted */
void data_sheet_free(Sheet *s);
void data_sheet_clone(Sheet *dst, const Sheet *src);
void data_each_formula(Sheet *sh, int home, void (*fn)(Formula *f, int home, void *arg), void *arg);
void data_shift(Sheet *s, int rows, i32 at, i32 n);

/* ------------------------------------------------------------------ the outline (outline.c) */
#define OUTLINE_MAX 7
/* a group: lines start..end at level or deeper; its summary line just after end (just before start
   when the sheet keeps summaries above / left); collapsed when all its lines are hidden */
typedef struct { u8 level, collapsed, before; i32 start, end; } OGroup;
i32  ogroup_summary(const OGroup *g);       /* its summary line (-1: none, off the sheet) */
int  outline_level(const Sheet *s, int rows, i32 i);
int  outline_max_level(const Sheet *s, int rows);
/* every group, inner ones before the group round them; at most max, how many */
int  outline_groups(const Sheet *s, int rows, OGroup *out, int max);
/* Group (on) or Ungroup lines a..b of rows or columns: their level up or down by one */
int  book_group(Book *b, int sheet, int rows, i32 a, i32 c, int on);
/* Hide Detail / Show Detail: the innermost group holding lines a..b (or the one they summarise);
   level > 0: the group at that level */
int  book_detail(Book *b, int sheet, int rows, i32 a, i32 c, int hide, int level);
/* the outline's level buttons: lines shown down to level n (1: only the top summaries) */
int  book_show_levels(Book *b, int sheet, int rows, int n);
/* Clear Outline: every level taken off (and the lines it hid shown) */
int  book_outline_clear(Book *b, int sheet);
/* summary rows below their detail, summary columns right of theirs */
int  book_outline_settings(Book *b, int sheet, int below, int right);
/* Subtotal: at each change in column groupcol of area's rows (its first row the headings), a row
   with SUBTOTAL(fn, ...) of each column in cols[0..ncols) (fn 1 average, 2 count, 3 counta, 4 max,
   5 min, 6 product, 7 stdev, 8 stdevp, 9 sum, 10 var, 11 varp), a grand total, and an outline of
   the groups. replace: earlier subtotals in the area taken out first; below: totals under each
   group (else above). The rows added, or -1 */
int  book_subtotal(Book *b, int sheet, Rect area, i32 groupcol, int fn, const i32 *cols, int ncols, int replace, int below);
/* Remove All: the area's subtotal rows taken out and its outline cleared; how many rows went */
int  book_subtotal_remove(Book *b, int sheet, Rect area);
const char *subtotal_label(int fn);         /* "Total", "Average"... */

/* ------------------------------------------------------------------ protection (protect.c) */
/* what users of a protected sheet may still do (Sheet.protallow) */
enum { PA_SELLOCKED = 1, PA_SELUNLOCKED = 2, PA_FMTCELLS = 4, PA_FMTCOLS = 8, PA_FMTROWS = 16, PA_INSCOLS = 32, PA_INSROWS = 64,
       PA_INSLINKS = 128, PA_DELCOLS = 256, PA_DELROWS = 512, PA_SORT = 1024, PA_FILTER = 2048, PA_PIVOT = 4096, PA_OBJECTS = 8192,
       PA_SCENARIOS = 16384 };
#define PA_DEFAULT (PA_SELLOCKED | PA_SELUNLOCKED)
/* Excel's legacy password hash (what sheetProtection's password attribute holds); 0 for "" */
u16  prot_hash(const char *pw);
/* whether cell r, c can't be changed: the sheet protected and the cell's style locked */
int  prot_locked(Book *b, int sheet, i32 r, i32 c);
/* whether any cell of the area is locked on a protected sheet */
int  prot_area_locked(Book *b, int sheet, Rect a);
/* whether the cell's formula is hidden from the formula bar (protected and its style hidden) */
int  prot_hidden(Book *b, int sheet, i32 r, i32 c);
/* protect (with a password, "" or NULL none, and the PA_ allowances) or unprotect the sheet; 0 if the
   password is wrong */
int  book_protect_sheet(Book *b, int sheet, const char *pw, u16 allow);
int  book_unprotect_sheet(Book *b, int sheet, const char *pw);
int  book_protect_book(Book *b, const char *pw);
int  book_unprotect_book(Book *b, const char *pw);
/* .xlsx: the sheet's <sheetProtection/> and the workbook's <workbookProtection/> (nothing when off);
   read from the element just started */
void prot_xlsx_write_sheet(const Sheet *s, Buf *o);
void prot_xlsx_write_book(const Book *b, Buf *o);
void prot_xlsx_read_sheet(Sheet *s, Xml *x);
void prot_xlsx_read_book(Book *b, Xml *x);

/* ------------------------------------------------------------------ Flash Fill (flash.c) */
/* the column of r, c filled from the column(s) to its left, by the pattern in the examples typed in
   it (rows of the block with values): parts of the source between delimiters, by position, case
   changes, initials and joins with literal text. How many cells were filled (0: no pattern) */
int  book_flash_fill(Book *b, int sheet, i32 r, i32 c);
/* (tests) the pattern learnt from n examples src[i] -> dst[i] applied to s; malloc'd, NULL none */
char *flash_guess(const char *const *src, const char *const *dst, int n, const char *s);

/* ------------------------------------------------------------------ Text to Columns and friends (datatools.c) */
enum { TTC_TAB = 1, TTC_SEMI = 2, TTC_COMMA = 4, TTC_SPACE = 8, TTC_OTHER = 16 };
typedef struct {
  int fixed;                      /* fixed width (else delimited) */
  int delims;                     /* TTC_ */
  char other;                     /* TTC_OTHER's character */
  int consecutive;                /* consecutive delimiters as one */
  char quote;                     /* text qualifier (0 none) */
  int breaks[32]; int nbreaks;    /* fixed width: the character positions where columns start (ascending, > 0) */
} TTCOpts;
/* one line split: its parts (malloc'd strings in a malloc'd array: free both); how many */
int  ttc_split(const char *text, const TTCOpts *o, char ***parts);
/* the cells of the area's first column split into columns from dest r, c (as if typed); the most
   columns made, or -1 */
int  book_text_to_columns(Book *b, int sheet, Rect src, const TTCOpts *o, i32 dr, i32 dc);
/* the Advanced Filter: rows of list (its first row the headings) that match the criteria range
   (headings over conditions: a row's conditions all hold, any row will do; NULL: every row), shown
   in place (the rest hidden) or copied to sheet csh at cr, cc (csh < 0: in place); unique: repeated
   rows once. How many rows matched */
int  book_advanced_filter(Book *b, int sheet, Rect list, const Rect *crit, int csh, i32 cr, i32 cc, int unique);
/* every row the Advanced Filter (or AutoFilter) hid shown again */
int  book_filter_show_all(Book *b, int sheet);
/* Consolidate: the n source areas (sheets srcsh[], areas src[]) combined with fn (as SUBTOTAL's
   numbers: 1 average, 2 count, 4 max, 5 min, 6 product, 9 sum...) into sheet ds at r, c; by the
   labels in their top row and/or left column, else by position. The size written, or 0 */
int  book_consolidate(Book *b, int ds, i32 r, i32 c, const int *srcsh, const Rect *src, int n, int fn, int toprow, int leftcol);

/* ------------------------------------------------------------------ What-If Analysis (whatif.c) */
/* Goal Seek: the changing cell's value that makes the formula cell's equal target (secant steps,
   bisection once bracketed; at most 100 tries, within 0.001). It is written into the changing cell
   (an undo step). 1 found, 0 not (the best try is left), -1 bad cells (*why says why) */
int  book_goal_seek(Book *b, int sheet, i32 fr, i32 fc, double target, int csh, i32 cr, i32 cc, double *found, const char **why);
/* a Data Table on area: one variable (values down its first column, formulas across its top row,
   colin the input cell; or across with rowin) or two (formula in the corner). Results written as
   values. Input cells on sheet sheet (r < 0: none). How many cells, -1 if no good */
int  book_data_table(Book *b, int sheet, Rect area, i32 rin_r, i32 rin_c, i32 cin_r, i32 cin_c);
/* scenarios: named sets of values for some cells of a sheet */
typedef struct Scenario { char name[64]; char *comment; int n; i32 *r, *c; char **vals; } Scenario;
int  book_scenario_add(Book *b, int sheet, const char *name, const i32 *r, const i32 *c, const char *const *vals, int n, const char *comment);
int  book_scenario_delete(Book *b, int sheet, int i);
int  book_scenario_show(Book *b, int sheet, int i);
/* a new sheet "Scenario Summary": the changing cells (and result cells) for the current values and
   each scenario; its index or -1 */
int  book_scenario_summary(Book *b, int sheet, const Rect *results, int nres);
void scenario_free(Scenario *sc);
void scen_xlsx_write(const Sheet *s, Buf *o);
void scen_xlsx_read(Sheet *s, Xml *x);

/* ------------------------------------------------------------------ the outline in .xlsx (outline.c) */
/* attributes added to a <row> or <col> (outlineLevel, collapsed), the sheet's <outlinePr> inside
   <sheetPr> and <sheetFormatPr>'s outlineLevelRow/Col, and reading them back */
void outline_xlsx_row_attrs(u8 level, u8 collapsed, Buf *o);
void outline_xlsx_read_line(Xml *x, u8 *level, u8 *collapsed);
void outline_xlsx_sheetpr(const Sheet *s, Buf *o);
void outline_xlsx_formatpr(const Sheet *s, Buf *o);
void outline_xlsx_read_pr(Sheet *s, Xml *x);
/* xlsx.c's formula writer (newer functions with _xlfn.), for the parts written elsewhere */
void xlsx_formula_text(Book *b, int sheet, const Formula *f, Buf *o);

#endif
