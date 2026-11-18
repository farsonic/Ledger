/*
 * gridgeo.h - the grid's geometry, portable so it can be tested natively: which columns (rows)
 * show along an axis and where, frozen panes and View > Split's bars, what is under a point, the
 * heading edges a drag resizes, and right-to-left sheets (Sheet.rtl), whose grid is drawn mirrored
 * as Excel draws it: column A at the right, columns growing leftwards, the row headings on the
 * right, the horizontal scroll bar running the other way.
 *
 * amiga/grid.c lays the grid out and works out everything (hits, drags, the fill handle, frozen
 * panes, overflowing text) in logical pixels, as if the sheet ran left to right; only the drawing
 * (gmirror.h) and the mouse's x (gg_mx) are mirrored, so all of it holds both ways.
 */
#ifndef LEDGER_GRIDGEO_H
#define LEDGER_GRIDGEO_H

#include "util.h"

typedef long (*GgSizeFn)(void *ctx, i32 i);   /* item i's size in pixels (0: hidden) */

/* one axis: columns across (from the grid's left) or rows down (from its top) */
typedef struct {
  long head;                 /* the headings' size: the cells start here */
  long extent;               /* the grid's size along the axis */
  i32 frozen;                /* the frozen items at the start (0 none) */
  i32 first;                 /* the first item of the scrolling part */
  long split;                /* View > Split: the bar's place from the cells' start, in pixels (0 none) */
  i32 sfirst;                /* (split) the first item of the pane before the bar */
  int before_active;         /* (split) the active pane is the one before the bar */
  long barw;                 /* (split) the bar's thickness */
  i32 max;                   /* how many items there are (MAXCOL, MAXROW) */
  GgSizeFn size, gap;        /* sizes; gap (may be NULL): extra pixels before an item of the scrolling part */
  void *ctx;
} GgAxis;

/* the items that fit, frozen (or the pane before the bar) first: idx[0..n-1] and their starts in
   pos[0..n] (pos[n]: where the last ends); *nfix of them are the frozen ones, *bar the split bar's
   place (0 none). Returns n */
int  gg_layout(const GgAxis *a, i32 *idx, long *pos, int cap, int *nfix, long *bar);
/* item i's start, shown or not (bar: the layout's) */
long gg_pos(const GgAxis *a, long bar, i32 i);
/* (split) item i is placed in the pane before the bar */
int  gg_in_first_pane(const GgAxis *a, long bar, i32 i);
/* the item at p: the first whose end is past p (beyond the last shown, the last); in the headings
   (p < head) the first scrolling one */
i32  gg_hit(const i32 *idx, const long *pos, int n, int nfix, long head, long p);
/* the item whose end edge is near p (lo pixels before it to hi after), the last shown first; -1 none */
i32  gg_edge(const i32 *idx, const long *pos, int n, long p, long lo, long hi);
/* the index in idx of item i, -1 if it isn't shown */
int  gg_shown(const i32 *idx, int n, i32 i);

/* right-to-left: logical pixels (the sheet as if it ran left to right) to the screen's and back */
typedef struct {
  int on;                    /* the sheet is right to left */
  long w;                    /* the grid's width */
  long dx;                   /* where the grid's left edge is in the RastPort drawn into */
} GgMirror;
long gg_mx(const GgMirror *m, long x);                       /* a pixel's x; the same both ways */
void gg_mspan(const GgMirror *m, long *x0, long *x1);       /* pixels x0..x1 (x0 <= x1 kept) */
/* column item's pixels on the screen (from the layout's idx, pos); 0 if it isn't shown */
int  gg_item_span(const GgMirror *m, const i32 *idx, const long *pos, int n, i32 item, long *x0, long *x1);
/* the column under screen x (as gg_hit) */
i32  gg_hit_screen(const GgMirror *m, const i32 *idx, const long *pos, int n, int nfix, long head, long sx);
/* the horizontal scroll bar's first visible entry, the same both ways (mirrored: the thumb starts
   at the right) */
long gg_bar_first(int rtl, long entries, long visible, long first);
/* the arrow keys: a step across (-1 left, 1 right on the screen) as a step in columns */
int  gg_key_dc(int rtl, int dc);
/* a cell's horizontal alignment (HA_) to lay its text out by in logical pixels: General follows
   the sheet's direction (mirrored, text goes right and numbers left), Left and Right stay where
   they say on the screen */
int  gg_halign(int rtl, int ha);

#endif
