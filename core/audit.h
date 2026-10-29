/*
 * audit.h - the Formulas tab's auditing tools and other workbook-wide look-ups: a cell's
 * precedents and dependents (Trace Precedents/Dependents draws them as arrows), Evaluate Formula's
 * step by step working, Error Checking, Go To Special, Create Names from Selection, Workbook
 * Statistics and Check Accessibility, as GridCraft has them.
 */
#ifndef LEDGER_AUDIT_H
#define LEDGER_AUDIT_H

#include "book.h"

typedef struct { int sheet; Rect r; } AuditRef;
/* the cells and ranges a cell's formula refers to directly (defined names followed to their
   ranges); how many (at most max) */
int  audit_precedents(Book *b, int sheet, i32 r, i32 c, AuditRef *out, int max);
/* the cells whose formulas refer directly to cell r, c (each as a one-cell AuditRef); how many */
int  audit_dependents(Book *b, int sheet, i32 r, i32 c, AuditRef *out, int max);

/* Evaluate Formula: the formula of a cell worked out one part at a time, innermost first */
typedef struct {
  Book *b; int sheet; i32 r, c;
  Formula *f;                     /* what is left: evaluated parts replaced by their values */
  int steps;
} EvalSteps;
int  eval_begin(EvalSteps *e, Book *b, int sheet, i32 r, i32 c);   /* 0 if the cell has no formula */
void eval_end(EvalSteps *e);
/* the formula as it stands ("=" and all), and the part the next step works out ("" when done) */
void eval_text(EvalSteps *e, char *out, size_t cap);
int  eval_next(EvalSteps *e, char *part, size_t cap);   /* 0 when finished */
int  eval_step(EvalSteps *e);                            /* one part worked out; 0 when finished */
int  eval_done(EvalSteps *e);

/* Error Checking: the cells showing errors, sheet by sheet from sheet, row by row; how many */
typedef struct { int sheet; i32 r, c; int err; } AuditCell;
int  audit_errors(Book *b, AuditCell *out, int max);
/* what an error means, for the dialog (English) */
const char *audit_error_help(int err);

/* Go To Special: the cells of kind (GS_) in area (or the whole sheet when area is one cell);
   their bounding box in *bound, the cells (up to max) in out; how many there are */
enum { GS_BLANKS, GS_CONSTANTS, GS_FORMULAS, GS_NOTES, GS_LASTCELL, GS_REGION, GS_VISIBLE, GS_CONDFMT, GS_ERRORS,
       GS_NUMBERS, GS_TEXT, GS_LOGICALS, GS_N };
long goto_special(Book *b, int sheet, Rect area, i32 cur_r, i32 cur_c, int kind, Rect *bound, AuditCell *out, int max);
/* the block of filled cells round r, c (Ctrl-*'s current region) */
Rect current_region_at(Book *b, int sheet, i32 r, i32 c);

/* Create from Selection: names from the labels in the area's top row, left column, bottom row or
   right column, each standing for the rest of its column or row; how many were made */
int  book_create_names(Book *b, int sheet, Rect area, int top, int left, int bottom, int right);

/* Check Accessibility (GridCraft's checks): merged cells and sheets with default names, as lines
   of text "Sheet\tIssue" into out; how many */
int  audit_accessibility(Book *b, char *out, size_t cap);

#endif
