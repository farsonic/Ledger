/*
 * gmirror.h - right-to-left sheets: the grid is laid out and drawn in logical pixels (as if the
 * sheet ran left to right) and these stand in for graphics.library's drawing calls in the files
 * that draw on it (grid.c, gridx.c, viewtab.c, formtab.c, pivot2.c), mirroring each x while
 * grid_mirror.on (only while grid.c draws a right-to-left sheet). Text is placed mirrored but reads
 * left to right as it should. Include it after every other header.
 */
#ifndef LEDGER_GMIRROR_H
#define LEDGER_GMIRROR_H

#include <proto/graphics.h>
#include "theme.h"
#include "../core/gridgeo.h"

extern GgMirror grid_mirror;     /* (grid.c) */
#define GMX(x) gg_mx(&grid_mirror, (long)(x))

static void gm_move(struct RastPort *rp, LONG x, LONG y) { Move(rp, GMX(x), y); }
static void gm_draw(struct RastPort *rp, LONG x, LONG y) { Draw(rp, GMX(x), y); }
static void gm_rectfill(struct RastPort *rp, LONG x0, LONG y0, LONG x1, LONG y1)
{
  if (grid_mirror.on) { LONG t = GMX(x1); x1 = GMX(x0); x0 = t; }
  RectFill(rp, x0, y0, x1, y1);
}
static void gm_theme_fill(struct RastPort *rp, LONG x0, LONG y0, LONG x1, LONG y1, LONG pen)
{
  if (grid_mirror.on) { LONG t = GMX(x1); x1 = GMX(x0); x0 = t; }
  theme_fill(rp, x0, y0, x1, y1, pen);
}
static void gm_ellipse(struct RastPort *rp, LONG cx, LONG cy, LONG rx, LONG ry) { DrawEllipse(rp, GMX(cx), cy, rx, ry); }
/* text from the pen's place: its logical span mirrored, the pen left at its logical end */
static void gm_text(struct RastPort *rp, STRPTR s, ULONG n)
{
  LONG lx, tw;
  if (!grid_mirror.on) { Text(rp, s, n); return; }
  lx = GMX(rp->cp_x); tw = TextLength(rp, s, n);
  Move(rp, GMX(lx + tw - 1), rp->cp_y);
  Text(rp, s, n);
  Move(rp, GMX(lx + tw), rp->cp_y);
}

#undef Move
#undef Draw
#undef RectFill
#undef DrawEllipse
#undef Text
#define Move(rp, x, y) gm_move(rp, x, y)
#define Draw(rp, x, y) gm_draw(rp, x, y)
#define RectFill(rp, x0, y0, x1, y1) gm_rectfill(rp, x0, y0, x1, y1)
#define DrawEllipse(rp, cx, cy, rx, ry) gm_ellipse(rp, cx, cy, rx, ry)
#define Text(rp, s, n) gm_text(rp, s, n)
#define theme_fill(rp, x0, y0, x1, y1, pen) gm_theme_fill(rp, x0, y0, x1, y1, pen)

#endif
