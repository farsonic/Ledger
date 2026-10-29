/*
 * datatab.h - the Data tab's commands and dialogs (datatab.c) and what the grid draws for them
 * (gridx.c): validation's drop-down arrow, input message and invalid-data circles, and the outline's
 * bands of brackets, +/- buttons and level numbers beside the headings.
 */
#ifndef LEDGER_DATATAB_H
#define LEDGER_DATATAB_H

#include "ledger.h"

/* the commands (ids.h's ranges stop below 1000; these sit apart so they merge cleanly) */
enum {
  ID_DV = 1500, ID_DVDD, ID_DVPICK, ID_DVCLEAR, ID_CIRCLE, ID_CIRCLECLEAR, ID_TEXTCOLS, ID_FLASHFILL, ID_CONSOLIDATE,
  ID_WHATIFDD, ID_GOALSEEK, ID_DATATABLE, ID_SCENARIOS, ID_GROUP, ID_UNGROUP, ID_SUBTOTAL, ID_SHOWDETAIL, ID_HIDEDETAIL,
  ID_OUTLINESET, ID_CLEAROUTLINE, ID_ADVFILTER, ID_PROTECTSHEET, ID_PROTECTBOOK, ID_GETDATA, ID_GROUPDD, ID_UNGROUPDD,
  ID_DATA_LAST
};

/* (main.c) a command of the Data tab, or one a protected sheet or workbook refuses: 1 if dealt with
   here (nothing more to do), 0 to go on as usual. v is the ribbon list's entry for a list's id */
int  datatab_command(ULONG id, LONG v);
/* (main.c) a ribbon list's choice refused by protection: 1 refused (a message was shown) */
int  datatab_refused(ULONG id, LONG v);
/* (main.c) the text the formula bar shows for the active cell: its formula hidden by protection */
int  datatab_hide_formula(Book *b, int sheet, i32 r, i32 c);

/* (grid.c) what may change: cells (their contents), row heights, column widths. 0 refused, after
   telling the user */
enum { DT_CELLS, DT_ROWS, DT_COLS };
int  datatab_may(Book *b, int sheet, int what, Rect a);
/* (grid.c) an entry typed into r, c (UTF-8) against the cell's validation: 1 accept, 0 keep editing,
   -1 drop the entry (the user chose Cancel) */
int  datatab_entry_ok(Book *b, int sheet, i32 r, i32 c, const char *u);

/* (gridx.c) the grid's geometry as last drawn, for the extras */
typedef struct {
  Book *book; int sheet;
  LONG w, h, hw, hh, bandw, bandh;  /* hw, hh: the headings' size with the outline bands in them */
  int nvr, nvc, editing;
  const i32 *vr; const LONG *vy; const i32 *vc; const LONG *vx;
  LONG pen_head, pen_headline, pen_headtext, pen_accent, pen_white, pen_text;
  LONG (*pen)(void *ctx, ULONG rgb); void *ctx;
  i32 cur_r, cur_c;
} GridGeo;
LONG gx_band(Sheet *s, int rows);                         /* the outline band's thickness (0: none) */
void gx_draw_outline(struct RastPort *rp, const GridGeo *g);
void gx_draw_marks(struct RastPort *rp, const GridGeo *g);   /* drop-down arrow, circles, input message */
/* a click at grid point x, y: 0 not ours, 1 the outline changed (redraw), 2 the drop-down arrow */
int  gx_click(const GridGeo *g, LONG x, LONG y);
/* the drop-down arrow's box (grid coordinates); 0 if none shows */
int  gx_arrow_box(const GridGeo *g, LONG *x0, LONG *y0, LONG *x1, LONG *y1);
/* Circle Invalid Data: on or off for a sheet of a book (the circles are worked out again as it changes) */
void gx_circles(Book *b, int sheet, int on);
int  gx_circles_on(Book *b, int sheet);

#endif
