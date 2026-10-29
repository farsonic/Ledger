/*
 * gridx.c - what the grid draws for the Data tab's tools, from its geometry (GridGeo, datatab.h):
 *
 *  - the outline's bands, as GridCraft and Excel draw them: a strip left of the row numbers and one
 *    above the column letters, a lane per level; each expanded group a bracket ending in a "-"
 *    button on its summary line, a collapsed one a "+" button; the level buttons 1..n+1 in the
 *    corner. Clicks on them hide or show the detail (undo steps, through outline.c);
 *  - data validation's drop-down arrow beside the active cell when its rule is a list, and the
 *    rule's input message in a pale box under it;
 *  - Circle Invalid Data's red rings round the cells whose values break their rules.
 */
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <proto/exec.h>
#include <proto/graphics.h>
#include <graphics/gfxmacros.h>
#include "datatab.h"
#include "theme.h"
#include "lang.h"
#include "../core/data.h"

static LONG lane(void) { return theme_font()->tf_YSize + 4; }
LONG gx_band(Sheet *s, int rows)
{
  int n = outline_max_level(s, rows);
  return n ? (n + 1) * lane() + 3 : 0;
}
static int vis_index(const GridGeo *g, int rows, i32 i)
{
  int k, n = rows ? g->nvr : g->nvc;
  const i32 *v = rows ? g->vr : g->vc;
  for (k = 0; k < n; k++) if (v[k] == i) return k;
  return -1;
}
/* the screen span (along the lines) of line i if shown */
static int span(const GridGeo *g, int rows, i32 i, LONG *a, LONG *b)
{
  int k = vis_index(g, rows, i);
  if (k < 0) return 0;
  if (rows) { *a = g->vy[k]; *b = g->vy[k + 1] - 1; } else { *a = g->vx[k]; *b = g->vx[k + 1] - 1; }
  return 1;
}
/* the first and last shown lines of start..end: their screen extent */
static int span_range(const GridGeo *g, int rows, i32 start, i32 end, LONG *a, LONG *b)
{
  int k, n = rows ? g->nvr : g->nvc, any = 0;
  const i32 *v = rows ? g->vr : g->vc;
  const LONG *p = rows ? g->vy : g->vx;
  for (k = 0; k < n; k++)
    if (v[k] >= start && v[k] <= end) { if (!any) *a = p[k]; *b = p[k + 1] - 1; any = 1; }
  return any;
}
static void box(struct RastPort *rp, LONG x0, LONG y0, LONG x1, LONG y1, LONG fillpen, LONG linepen)
{
  if (fillpen >= 0) { SetAPen(rp, fillpen); RectFill(rp, x0, y0, x1, y1); }
  SetAPen(rp, linepen);
  Move(rp, x0, y0); Draw(rp, x1, y0); Draw(rp, x1, y1); Draw(rp, x0, y1); Draw(rp, x0, y0);
}
/* a small button with "+", "-" or a digit, centred at cx, cy */
static void button(struct RastPort *rp, const GridGeo *g, LONG cx, LONG cy, const char *label)
{
  LONG s = lane() - 4, x0 = cx - s / 2, y0 = cy - s / 2, x1 = x0 + s - 1, y1 = y0 + s - 1;
  box(rp, x0, y0, x1, y1, g->pen_white, g->pen_headline);
  SetAPen(rp, g->pen_headtext);
  if (!strcmp(label, "+") || !strcmp(label, "-")) {
    Move(rp, x0 + 2, (y0 + y1) / 2); Draw(rp, x1 - 2, (y0 + y1) / 2);
    if (label[0] == '+') { Move(rp, (x0 + x1) / 2, y0 + 2); Draw(rp, (x0 + x1) / 2, y1 - 2); }
  } else {
    struct TextFont *tf = theme_font();
    LONG tw;
    SetFont(rp, tf);
    SetDrMd(rp, JAM1);
    tw = TextLength(rp, (STRPTR)label, (ULONG)strlen(label));
    Move(rp, cx - tw / 2, y0 + (s - tf->tf_YSize) / 2 + tf->tf_Baseline);
    Text(rp, (STRPTR)label, (ULONG)strlen(label));
  }
}
#define MAXG 512
/* where things are: the level buttons and the groups' buttons, for drawing and for clicks */
typedef struct { LONG cx, cy; int rows, level; OGroup grp; int kind; } Btn;   /* kind 0 level, 1 group */
static int buttons(const GridGeo *g, Btn *out, int max, struct RastPort *rp)
{
  Sheet *s = g->book->sh[g->sheet];
  OGroup *gr;
  int axis, n = 0, k, ng, L;
  LONG ln = lane();
  if (!(gr = (OGroup *)malloc(MAXG * sizeof(OGroup)))) return 0;
  for (axis = 0; axis < 2; axis++) {
    int rows = axis == 0, levels = outline_max_level(s, rows);
    LONG band = rows ? g->bandw : g->bandh;
    if (!levels || !band) continue;
    /* the level buttons: rows' above their band (in the column headings' height), columns' left of theirs */
    for (L = 1; L <= levels + 1 && n < max; L++) {
      Btn *b = &out[n++];
      b->rows = rows; b->level = L; b->kind = 0;
      if (rows) { b->cx = (L - 1) * ln + ln / 2 + 1; b->cy = g->bandh + (g->hh - g->bandh) / 2; }
      else { b->cy = (L - 1) * ln + ln / 2 + 1; b->cx = g->bandw + (g->hw - g->bandw) / 2; }
    }
    ng = outline_groups(s, rows, gr, MAXG);
    for (k = 0; k < ng && n < max; k++) {
      i32 sm = ogroup_summary(&gr[k]);
      LONG a, b2, sa, sb, lc = (gr[k].level - 1) * ln + ln / 2 + 1;
      int shown = sm >= 0 && span(g, rows, sm, &sa, &sb);
      if (!gr[k].collapsed && rp && span_range(g, rows, gr[k].start, gr[k].end, &a, &b2)) {
        /* the bracket: along the detail to the summary's button, a tick at its far end */
        LONG from, to, tick;
        if (gr[k].before) { from = shown ? (sa + sb) / 2 : a; to = b2 - 1; tick = b2 - 1; }
        else { from = a + 1; to = shown ? (sa + sb) / 2 : b2; tick = a + 1; }
        SetAPen(rp, g->pen_headtext);
        if (rows) {
          if (from < g->hh) from = g->hh;
          if (to >= from) { Move(rp, lc, from); Draw(rp, lc, to); }
          if (tick >= g->hh) { Move(rp, lc, tick); Draw(rp, lc + ln / 2 - 1, tick); }
        } else {
          if (from < g->hw) from = g->hw;
          if (to >= from) { Move(rp, from, lc); Draw(rp, to, lc); }
          if (tick >= g->hw) { Move(rp, tick, lc); Draw(rp, tick, lc + ln / 2 - 1); }
        }
      }
      if (shown) {
        Btn *bt = &out[n++];
        bt->rows = rows; bt->level = gr[k].level; bt->kind = 1; bt->grp = gr[k];
        if (rows) { bt->cx = lc; bt->cy = (sa + sb) / 2; } else { bt->cy = lc; bt->cx = (sa + sb) / 2; }
      }
    }
  }
  free(gr);
  return n;
}
#define MAXB 300
void gx_draw_outline(struct RastPort *rp, const GridGeo *g)
{
  Btn *b;
  int n, k;
  if (!g->bandw && !g->bandh) return;
  /* the bands' ground */
  SetAPen(rp, g->pen_head);
  if (g->bandw) RectFill(rp, 0, 0, g->bandw - 1, g->h - 1);
  if (g->bandh) RectFill(rp, 0, 0, g->w - 1, g->bandh - 1);
  SetAPen(rp, g->pen_headline);
  if (g->bandw) { Move(rp, g->bandw - 1, g->hh); Draw(rp, g->bandw - 1, g->h - 1); }
  if (g->bandh) { Move(rp, g->hw, g->bandh - 1); Draw(rp, g->w - 1, g->bandh - 1); }
  if (!(b = (Btn *)malloc(MAXB * sizeof(Btn)))) return;
  n = buttons(g, b, MAXB, rp);
  for (k = 0; k < n; k++) {
    char t[4];
    if (b[k].kind == 0) sprintf(t, "%d", b[k].level);
    else strcpy(t, b[k].grp.collapsed ? "+" : "-");
    if (b[k].kind == 1 && (b[k].rows ? b[k].cy < g->hh : b[k].cx < g->hw)) continue;     /* (under the headings: scrolled away) */
    button(rp, g, b[k].cx, b[k].cy, t);
  }
  free(b);
}
int gx_click(const GridGeo *g, LONG x, LONG y)
{
  Btn *b;
  int n, k, hit = -1;
  LONG r = (lane() - 4) / 2 + 1, x0, y0, x1, y1;
  if (gx_arrow_box(g, &x0, &y0, &x1, &y1) && x >= x0 && x <= x1 && y >= y0 && y <= y1) return 2;
  if (!g->bandw && !g->bandh) return 0;
  if (!(x < g->bandw || y < g->bandh || (x < g->hw && y < g->hh))) return 0;
  if (!(b = (Btn *)malloc(MAXB * sizeof(Btn)))) return 0;
  n = buttons(g, b, MAXB, NULL);
  for (k = 0; k < n && hit < 0; k++) if (x >= b[k].cx - r && x <= b[k].cx + r && y >= b[k].cy - r && y <= b[k].cy + r) hit = k;
  if (hit >= 0) {
    Btn *t = &b[hit];
    if (t->kind == 0) book_show_levels(g->book, g->sheet, t->rows, t->level);
    else book_detail(g->book, g->sheet, t->rows, t->grp.start, t->grp.end, !t->grp.collapsed, t->grp.level);
  }
  free(b);
  /* a click anywhere in the bands is theirs */
  return hit >= 0 || x < g->bandw || y < g->bandh ? 1 : 0;
}

/* ------------------------------------------------------------------ validation */
int gx_arrow_box(const GridGeo *g, LONG *x0, LONG *y0, LONG *x1, LONG *y1)
{
  Sheet *s = g->book->sh[g->sheet];
  const DVRule *r;
  int i, j;
  LONG bs;
  if (g->editing || !s->ndvs || !(r = dv_at(s, g->cur_r, g->cur_c)) || r->type != DV_LIST || !r->dropdown) return 0;
  if ((i = vis_index(g, 1, g->cur_r)) < 0 || (j = vis_index(g, 0, g->cur_c)) < 0) return 0;
  bs = g->vy[i + 1] - g->vy[i] - 1;
  if (bs > theme_font()->tf_YSize + 6) bs = theme_font()->tf_YSize + 6;
  if (bs < 8) return 0;
  *x0 = g->vx[j + 1] + 1; *x1 = *x0 + bs - 1;
  *y1 = g->vy[i + 1] - 1; *y0 = *y1 - bs + 1;
  return *x1 < g->w;
}
/* the circles: worked out again when the book has changed */
static Book *cbook; static int csheet = -1; static u32 cchanges = 0xFFFFFFFFUL; static int con;
static i32 crow[200], ccol[200]; static int nc;
void gx_circles(Book *b, int sheet, int on) { cbook = b; csheet = sheet; con = on; cchanges = 0xFFFFFFFFUL; nc = 0; }
int gx_circles_on(Book *b, int sheet) { return con && cbook == b && csheet == sheet; }
/* words wrapped into lines of at most w pixels (Latin-1 in t, changed in place: line starts in st[]) */
static int wrap(struct RastPort *rp, char *t, LONG w, char **st, int max)
{
  int n = 0;
  char *p = t;
  while (*p && n < max) {
    char *e = p, *brk = NULL;
    while (*e && *e != '\n' && TextLength(rp, (STRPTR)p, (ULONG)(e - p + 1)) <= w) { if (*e == ' ') brk = e; e++; }
    if (*e && *e != '\n' && brk) e = brk;
    if (e == p && *e && *e != '\n') e++;
    st[n++] = p;
    if (!*e) break;
    *e = 0;
    p = e + 1;
  }
  return n;
}
void gx_draw_marks(struct RastPort *rp, const GridGeo *g)
{
  Sheet *s = g->book->sh[g->sheet];
  LONG x0, y0, x1, y1;
  int k;
  /* the drop-down arrow */
  if (gx_arrow_box(g, &x0, &y0, &x1, &y1)) {
    LONG cx = (x0 + x1) / 2, cy = (y0 + y1) / 2 + 1;
    box(rp, x0, y0, x1, y1, g->pen_head, g->pen_headline);
    SetAPen(rp, g->pen_headtext);
    for (k = 0; k < 4; k++) { Move(rp, cx - 3 + k, cy - 2 + k); Draw(rp, cx + 3 - k, cy - 2 + k); }
  }
  /* the circles */
  if (gx_circles_on(g->book, g->sheet)) {
    LONG red = g->pen(g->ctx, 0xE00000);
    if (cchanges != g->book->changes) { nc = dv_invalid_cells(g->book, g->sheet, crow, ccol, 200); cchanges = g->book->changes; }
    SetAPen(rp, red);
    for (k = 0; k < nc; k++) {
      int i = vis_index(g, 1, crow[k]), j = vis_index(g, 0, ccol[k]);
      LONG cx, cy, rx, ry;
      if (i < 0 || j < 0) continue;
      cx = (g->vx[j] + g->vx[j + 1]) / 2; cy = (g->vy[i] + g->vy[i + 1]) / 2;
      rx = (g->vx[j + 1] - g->vx[j]) / 2 + 3; ry = (g->vy[i + 1] - g->vy[i]) / 2 + 2;
      DrawEllipse(rp, cx, cy, rx, ry);
      DrawEllipse(rp, cx, cy, rx - 1, ry - 1);
    }
  }
  /* the input message */
  if (!g->editing && s->ndvs) {
    const DVRule *r = dv_at(s, g->cur_r, g->cur_c);
    int i = vis_index(g, 1, g->cur_r), j = vis_index(g, 0, g->cur_c);
    if (r && r->showinput && ((r->intitle && r->intitle[0]) || (r->inmsg && r->inmsg[0])) && i >= 0 && j >= 0) {
      char title[200], msg[1000], *lines[12];
      int nl, lh = theme_font()->tf_YSize;
      LONG bw = 180;
      utf8_to_latin1(r->intitle ? r->intitle : "", r->intitle ? (u32)strlen(r->intitle) : 0, title, sizeof title);
      utf8_to_latin1(r->inmsg ? r->inmsg : "", r->inmsg ? (u32)strlen(r->inmsg) : 0, msg, sizeof msg);
      SetFont(rp, theme_font());
      SetSoftStyle(rp, 0, FSF_BOLD | FSF_ITALIC | FSF_UNDERLINED);
      nl = wrap(rp, msg, bw - 10, lines, 12);
      x0 = g->vx[j] + 8; y0 = g->vy[i + 1] + 4;
      x1 = x0 + bw; y1 = y0 + 6 + (nl + (title[0] ? 1 : 0)) * lh;
      if (x1 >= g->w) { x1 = g->w - 2; x0 = x1 - bw; }
      if (y1 >= g->h) { y1 = g->vy[i] - 4; y0 = y1 - 6 - (nl + (title[0] ? 1 : 0)) * lh; }
      if (x0 < g->hw) x0 = g->hw;
      box(rp, x0, y0, x1, y1, g->pen(g->ctx, 0xFFFFE1), g->pen(g->ctx, 0x6D6D6D));
      SetDrMd(rp, JAM1);
      SetAPen(rp, g->pen(g->ctx, 0x000000));
      y0 += 3;
      if (title[0]) {
        SetFont(rp, theme_bold());
        Move(rp, x0 + 5, y0 + theme_bold()->tf_Baseline); Text(rp, (STRPTR)title, (ULONG)strlen(title));
        SetFont(rp, theme_font());
        y0 += lh;
      }
      for (k = 0; k < nl; k++, y0 += lh) { Move(rp, x0 + 5, y0 + theme_font()->tf_Baseline); Text(rp, (STRPTR)lines[k], (ULONG)strlen(lines[k])); }
    }
  }
}
