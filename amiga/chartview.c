/*
 * chartview.c - see chartview.h. The objects of the active sheet are drawn into the grid's back
 * buffer after its cells (charts as chart.c lays them out, pictures decoded and dithered to a cube
 * of pens as Quill does it, shapes, ink), then the selection's handles. Dragging draws an outline
 * straight onto the window in complement mode, so nothing is drawn again until the mouse is let
 * go. Filled areas are drawn a row at a time with RectFill, outlines with Draw.
 */
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <exec/memory.h>
#include <devices/inputevent.h>
#include <graphics/gfxmacros.h>
#include <proto/exec.h>
#include <proto/intuition.h>
#include <proto/graphics.h>
#include <proto/layers.h>
#include <proto/muimaster.h>
#include <clib/alib_protos.h>
#include "pivot2.h"
#include "../core/slicer.h"
#include "chartview.h"
#include "chartids.h"
#include "theme.h"
#include "../core/image.h"

#define MAXSEL 32
#define NCC 16                   /* charts laid out and kept */
#define NPC 6                    /* pictures decoded and kept */
enum { DG_NONE, DG_MOVE, DG_SIZE, DG_INK, DG_ERASE, DG_LASSO, DG_PLACE };

struct ObjView {
  Book *book; int sheet;
  int sel[MAXSEL]; int nsel;
  int elem;
  int tool; ULONG tcolor; int twidth; int tshape;
  int drag, handle, xor_on;
  LONG sx, sy, cx, cy;             /* where the drag began, where it is (grid pixels) */
  LONG bx0, by0, bx1, by1;         /* the dragged object's box when it began (grid pixels) */
  LONG *ink; int nink, capink;     /* a stroke being drawn: points in sheet pixels at 100% */
  int last_ink, erased;
  ULONG secs, micros; int click_obj;
  struct { const Chart *c; Book *b; u32 changes; int w, h, z; PrimList pl; } cc[NCC]; int ncc;
  struct { Blob *pic; LONG w, h; UBYTE *px; } pc[NPC]; int npc;
  LONG cube[216]; int cubelv, ncube; struct ColorMap *cm;
};

/* ------------------------------------------------------------------ text */
static struct { int px, bold; struct TextFont *tf; } fc[16];
static int nfc;
static struct TextFont *font_for(int px, int bold)
{
  int i;
  UWORD ys;
  if (px < 6) px = 6;
  if (px > 60) px = 60;
  for (i = 0; i < nfc; i++) if (fc[i].px == px && fc[i].bold == bold) return fc[i].tf ? fc[i].tf : theme_font();
  if (nfc >= 16) { for (i = 0; i < nfc; i++) if (fc[i].tf) theme_close_font(fc[i].tf); nfc = 0; }
  ys = (UWORD)((px * 11 + 5) / 10);
  fc[nfc].px = px; fc[nfc].bold = bold;
  fc[nfc].tf = theme_open_font(bold ? "QuillSansB.font" : "QuillSans.font", ys);
  nfc++;
  return fc[nfc - 1].tf ? fc[nfc - 1].tf : theme_font();
}
void ov_fonts_free(void)
{
  int i;
  for (i = 0; i < nfc; i++) if (fc[i].tf) theme_close_font(fc[i].tf);
  nfc = 0;
}
static void latin(const char *u, char *out, size_t cap) { utf8_to_latin1(u ? u : "", u ? (u32)strlen(u) : 0, out, cap); }
i32 ov_text_width(void *ctx, const char *utf8, int px, int bold)
{
  struct RastPort rp;
  char t[300];
  (void)ctx;
  latin(utf8, t, sizeof t);
  InitRastPort(&rp);
  SetFont(&rp, font_for(px, bold));
  return (i32)TextLength(&rp, (STRPTR)t, (ULONG)strlen(t));
}
/* text turned to read upwards, its top left corner at x, y (drawn through a one-plane bitmap) */
static void text_up(struct RastPort *rp, struct TextFont *tf, const char *t, LONG x, LONG y, LONG pen)
{
  struct RastPort trp;
  struct BitMap *bm;
  LONG w, h, i, j;
  InitRastPort(&trp);
  SetFont(&trp, tf);
  w = TextLength(&trp, (STRPTR)t, (ULONG)strlen(t));
  h = tf->tf_YSize;
  if (w <= 0 || w > 1000 || !(bm = AllocBitMap((ULONG)w, (ULONG)h, 1, BMF_CLEAR, NULL))) return;
  trp.BitMap = bm;
  SetAPen(&trp, 1); SetDrMd(&trp, JAM1);
  Move(&trp, 0, tf->tf_Baseline);
  Text(&trp, (STRPTR)t, (ULONG)strlen(t));
  SetAPen(rp, pen);
  for (j = 0; j < h; j++)
    for (i = 0; i < w; i++)
      if (ReadPixel(&trp, i, j)) WritePixel(rp, x + j, y + w - 1 - i);
  FreeBitMap(bm);
}

/* ------------------------------------------------------------------ filling */
/* a polygon (xy: n points) filled row by row, offset by ox, oy */
static void fill_poly(struct RastPort *rp, const i32 *xy, int n, LONG ox, LONG oy)
{
  LONG y0 = 0x7FFFFFF, y1 = -0x7FFFFFF, y, *xs;
  int i, j, m;
  if (n < 3) return;
  for (i = 0; i < n; i++) { if (xy[2 * i + 1] < y0) y0 = xy[2 * i + 1]; if (xy[2 * i + 1] > y1) y1 = xy[2 * i + 1]; }
  if (y1 - y0 > 4000 || !(xs = (LONG *)malloc((size_t)(n + 1) * sizeof(LONG)))) return;
  for (y = y0; y <= y1; y++) {
    double sy = y + 0.5;
    for (i = 0, m = 0; i < n; i++) {
      double ax = xy[2 * i], ay = xy[2 * i + 1], bx = xy[2 * ((i + 1) % n)], by = xy[2 * ((i + 1) % n) + 1];
      if ((ay <= sy && by > sy) || (by <= sy && ay > sy)) xs[m++] = (LONG)floor(ax + (sy - ay) * (bx - ax) / (by - ay) + 0.5);
    }
    for (i = 1; i < m; i++) { LONG t = xs[i]; for (j = i - 1; j >= 0 && xs[j] > t; j--) xs[j + 1] = xs[j]; xs[j + 1] = t; }
    for (i = 0; i + 1 < m; i += 2) if (xs[i + 1] > xs[i]) RectFill(rp, ox + xs[i], oy + y, ox + xs[i + 1] - 1, oy + y);
  }
  free(xs);
}
static void fill_ellipse(struct RastPort *rp, LONG cx, LONG cy, LONG rx, LONG ry)
{
  LONG dy;
  if (rx < 1 || ry < 1) { RectFill(rp, cx - rx, cy - ry, cx + rx, cy + ry); return; }
  for (dy = -ry; dy <= ry; dy++) {
    double f = 1.0 - (double)dy * dy / ((double)ry * ry);
    LONG dx = (LONG)(rx * sqrt(f > 0 ? f : 0) + 0.5);
    RectFill(rp, cx - dx, cy + dy, cx + dx, cy + dy);
  }
}
static void thick(struct RastPort *rp, LONG x0, LONG y0, LONG x1, LONG y1, int w)
{
  int i;
  LONG adx = x1 > x0 ? x1 - x0 : x0 - x1, ady = y1 > y0 ? y1 - y0 : y0 - y1;
  if (w <= 1) { Move(rp, x0, y0); Draw(rp, x1, y1); return; }
  for (i = -w / 2; i < w - w / 2; i++) {
    if (adx >= ady) { Move(rp, x0, y0 + i); Draw(rp, x1, y1 + i); }
    else { Move(rp, x0 + i, y0); Draw(rp, x1 + i, y1); }
  }
}

/* ------------------------------------------------------------------ primitives */
void ov_paint(struct RastPort *rp, const PrimList *pl, LONG x, LONG y, PenFn pen, void *ctx)
{
  int i, k;
  SetDrMd(rp, JAM1);
  for (i = 0; i < pl->n; i++) {
    const Prim *p = &pl->p[i];
    switch (p->t) {
    case PR_RECT:
      if (p->fill) { SetAPen(rp, pen(ctx, p->fill)); RectFill(rp, x + p->x, y + p->y, x + p->x + p->w - 1, y + p->y + p->h - 1); }
      if (p->line) {
        LONG a = x + p->x, b = y + p->y, c = a + p->w - 1, d = b + p->h - 1;
        SetAPen(rp, pen(ctx, p->line));
        Move(rp, a, b); Draw(rp, c, b); Draw(rp, c, d); Draw(rp, a, d); Draw(rp, a, b);
      }
      break;
    case PR_LINE:
      SetAPen(rp, pen(ctx, p->line));
      for (k = 0; k + 1 < p->np; k++) thick(rp, x + p->pts[2 * k], y + p->pts[2 * k + 1], x + p->pts[2 * k + 2], y + p->pts[2 * k + 3], p->width);
      break;
    case PR_POLY:
      if (p->fill) { SetAPen(rp, pen(ctx, p->fill)); fill_poly(rp, p->pts, p->np, x, y); }
      if (p->line) {
        SetAPen(rp, pen(ctx, p->line));
        Move(rp, x + p->pts[0], y + p->pts[1]);
        for (k = 1; k <= p->np; k++) Draw(rp, x + p->pts[2 * (k % p->np)], y + p->pts[2 * (k % p->np) + 1]);
      }
      break;
    case PR_ELLIPSE: {
      LONG rx = p->w / 2, ry = p->h / 2, cx = x + p->x + rx, cy = y + p->y + ry;
      if (p->fill) { SetAPen(rp, pen(ctx, p->fill)); fill_ellipse(rp, cx, cy, rx, ry); }
      if (p->line) { SetAPen(rp, pen(ctx, p->line)); DrawEllipse(rp, cx, cy, rx > 0 ? rx : 1, ry > 0 ? ry : 1); }
      break; }
    case PR_TEXT: {
      struct TextFont *tf = font_for(p->size, p->bold);
      char t[300];
      LONG w, h, tx, ty;
      latin(p->text, t, sizeof t);
      SetFont(rp, tf);
      w = TextLength(rp, (STRPTR)t, (ULONG)strlen(t)); h = tf->tf_YSize;
      if (p->vert) {
        /* turned: its box is h wide, w high */
        tx = x + p->x - (p->align == 1 ? h / 2 : p->align == 2 ? h : 0);
        ty = y + p->y - (p->valign == 1 ? w / 2 : p->valign == 2 ? w : 0);
        text_up(rp, tf, t, tx, ty, pen(ctx, p->line));
        break;
      }
      tx = x + p->x - (p->align == 1 ? w / 2 : p->align == 2 ? w : 0);
      ty = y + p->y - (p->valign == 1 ? h / 2 : p->valign == 2 ? h : 0);
      SetAPen(rp, pen(ctx, p->line));
      Move(rp, tx, ty + tf->tf_Baseline);
      Text(rp, (STRPTR)t, (ULONG)strlen(t));
      break; }
    }
  }
}

/* ------------------------------------------------------------------ shapes */
static LONG lw_px(u16 linew, LONG zoom) { LONG w = (LONG)(linew ? linew : 75) * 96 * zoom / (72L * 100 * 100); return w < 1 ? 1 : w > 30 ? 30 : w; }
/* the shape's text, wrapped in its box */
static void shape_text(struct RastPort *rp, const DObj *o, LONG x0, LONG y0, LONG x1, LONG y1, LONG zoom, PenFn pen, void *ctx)
{
  struct TextFont *tf = font_for((int)((o->fsize ? o->fsize : 110) * 96L * zoom / (720L * 100)), o->bold);
  char t[1200], lines[40][160];
  int nl = 0, i;
  const char *p;
  LONG maxw = x1 - x0 - 8, lh, ty;
  if (!o->text || !*o->text || maxw < 4) return;
  latin(o->text, t, sizeof t);
  SetFont(rp, tf);
  SetSoftStyle(rp, o->italic ? FSF_ITALIC : 0, FSF_ITALIC);
  lh = tf->tf_YSize;
  for (p = t; *p && nl < 40; ) {
    /* as many words as fit, or up to the line's end */
    const char *e = p, *brk = NULL;
    int n;
    while (*e && *e != '\n') {
      if (*e == ' ') brk = e;
      if (TextLength(rp, (STRPTR)p, (ULONG)(e - p + 1)) > maxw) break;
      e++;
    }
    if (*e && *e != '\n' && brk && brk > p) e = brk;
    if (e == p && *e && *e != '\n') e++;
    n = (int)(e - p) < 159 ? (int)(e - p) : 159;
    memcpy(lines[nl], p, (size_t)n); lines[nl][n] = 0;
    nl++;
    p = e;
    if (*p == ' ' || *p == '\n') p++;
  }
  ty = o->valign == 1 ? (y0 + y1 - nl * lh) / 2 : o->valign == 2 ? y1 - 4 - nl * lh : y0 + 3;
  SetAPen(rp, pen(ctx, o->tcolor ? o->tcolor : (o->shape == SK_TEXTBOX || !o->fill ? FILL_ON : FILL_ON | 0xFFFFFF)));
  for (i = 0; i < nl; i++) {
    LONG w = TextLength(rp, (STRPTR)lines[i], (ULONG)strlen(lines[i])), tx = o->halign == 1 ? (x0 + x1 - w) / 2 : o->halign == 2 ? x1 - 4 - w : x0 + 4;
    if (ty + i * lh + lh > y1 + 2 && i) break;
    Move(rp, tx, ty + i * lh + tf->tf_Baseline);
    Text(rp, (STRPTR)lines[i], (ULONG)strlen(lines[i]));
  }
  SetSoftStyle(rp, 0, FSF_ITALIC);
}
/* an icon from drawing.c's set, stretched over the box */
static void icon_draw(struct RastPort *rp, const char *p, LONG x0, LONG y0, LONG x1, LONG y1, int w)
{
  LONG bw = x1 - x0, bh = y1 - y0;
#define IX(v) (x0 + (v) * bw / 16)
#define IY(v) (y0 + (v) * bh / 16)
  while (p && *p) {
    char c = *p++;
    LONG a, b, e, f, n, i;
    i32 xy[64];
    while (*p == ' ') p++;
    switch (c) {
    case 'L': a = strtol(p, (char **)&p, 10); b = strtol(p, (char **)&p, 10); e = strtol(p, (char **)&p, 10); f = strtol(p, (char **)&p, 10); thick(rp, IX(a), IY(b), IX(e), IY(f), w); break;
    case 'R': a = strtol(p, (char **)&p, 10); b = strtol(p, (char **)&p, 10); e = strtol(p, (char **)&p, 10); f = strtol(p, (char **)&p, 10);
      thick(rp, IX(a), IY(b), IX(e), IY(b), w); thick(rp, IX(e), IY(b), IX(e), IY(f), w); thick(rp, IX(e), IY(f), IX(a), IY(f), w); thick(rp, IX(a), IY(f), IX(a), IY(b), w); break;
    case 'F': a = strtol(p, (char **)&p, 10); b = strtol(p, (char **)&p, 10); e = strtol(p, (char **)&p, 10); f = strtol(p, (char **)&p, 10); RectFill(rp, IX(a), IY(b), IX(e), IY(f)); break;
    case 'O': a = strtol(p, (char **)&p, 10); b = strtol(p, (char **)&p, 10); e = strtol(p, (char **)&p, 10);
      for (i = 0; i < w; i++) DrawEllipse(rp, IX(a), IY(b), e * bw / 16 - i > 0 ? e * bw / 16 - i : 1, e * bh / 16 - i > 0 ? e * bh / 16 - i : 1);
      break;
    case 'E': a = strtol(p, (char **)&p, 10); b = strtol(p, (char **)&p, 10); e = strtol(p, (char **)&p, 10); fill_ellipse(rp, IX(a), IY(b), e * bw / 16, e * bh / 16); break;
    case 'P': case 'Q':
      n = strtol(p, (char **)&p, 10);
      for (i = 0; i < n && i < 32; i++) { xy[2 * i] = (i32)IX(strtol(p, (char **)&p, 10)); xy[2 * i + 1] = (i32)IY(strtol(p, (char **)&p, 10)); }
      if (n > 32) n = 32;
      if (c == 'Q') fill_poly(rp, xy, (int)n, 0, 0);
      else for (i = 0; i + 1 < n; i++) thick(rp, xy[2 * i], xy[2 * i + 1], xy[2 * i + 2], xy[2 * i + 3], w);
      break;
    default: break;
    }
  }
#undef IX
#undef IY
}
static const UWORD half_pat[2] = { 0xAAAA, 0x5555 };
void ov_shape(struct RastPort *rp, const DObj *o, LONG x0, LONG y0, LONG x1, LONG y1, LONG zoom, PenFn pen, void *ctx)
{
  int w = lw_px(o->linew, zoom);
  i32 xy[2 * 64];
  int n = 0, i;
  SetDrMd(rp, JAM1);
  switch (o->shape) {
  case SK_RECT: case SK_TEXTBOX:
    if (o->fill) { SetAPen(rp, pen(ctx, o->fill)); RectFill(rp, x0, y0, x1, y1); }
    if (o->line) { SetAPen(rp, pen(ctx, o->line)); for (i = 0; i < w; i++) { Move(rp, x0 + i, y0 + i); Draw(rp, x1 - i, y0 + i); Draw(rp, x1 - i, y1 - i); Draw(rp, x0 + i, y1 - i); Draw(rp, x0 + i, y0 + i); } }
    break;
  case SK_ROUNDRECT: {
    LONG r = ((x1 - x0) < (y1 - y0) ? (x1 - x0) : (y1 - y0)) / 6;
    int k;
    static const int cxs[4] = { 1, 1, 0, 0 }, cys[4] = { 0, 1, 1, 0 };
    for (k = 0; k < 4; k++) {
      LONG ccx = cxs[k] ? x1 - r : x0 + r, ccy = cys[k] ? y1 - r : y0 + r;
      int j;
      for (j = 0; j <= 6; j++) {
        double a = (k * 90 - 90 + j * 15) * 3.14159265 / 180;
        xy[2 * n] = (i32)(ccx + r * cos(a)); xy[2 * n + 1] = (i32)(ccy + r * sin(a)); n++;
      }
    }
    break; }
  case SK_ELLIPSE: {
    LONG rx = (x1 - x0) / 2, ry = (y1 - y0) / 2;
    if (o->fill) { SetAPen(rp, pen(ctx, o->fill)); fill_ellipse(rp, x0 + rx, y0 + ry, rx, ry); }
    if (o->line) { SetAPen(rp, pen(ctx, o->line)); for (i = 0; i < w; i++) DrawEllipse(rp, x0 + rx, y0 + ry, rx - i > 0 ? rx - i : 1, ry - i > 0 ? ry - i : 1); }
    break; }
  case SK_TRIANGLE:
    xy[0] = (i32)((x0 + x1) / 2); xy[1] = (i32)y0; xy[2] = (i32)x1; xy[3] = (i32)y1; xy[4] = (i32)x0; xy[5] = (i32)y1; n = 3;
    break;
  case SK_LINE: case SK_ARROW: {
    LONG ax = o->flipx ? x1 : x0, ay = o->flipy ? y1 : y0, bx = o->flipx ? x0 : x1, by = o->flipy ? y0 : y1;
    SetAPen(rp, pen(ctx, o->line ? o->line : FILL_ON));
    thick(rp, ax, ay, bx, by, w);
    if (o->shape == SK_ARROW) {
      double dx = bx - ax, dy = by - ay, l = sqrt(dx * dx + dy * dy), hl = 3 * w + 6, hw = 2 * w + 3;
      i32 t[6];
      if (l < 1) break;
      dx /= l; dy /= l;
      t[0] = (i32)bx; t[1] = (i32)by;
      t[2] = (i32)(bx - dx * hl - dy * hw); t[3] = (i32)(by - dy * hl + dx * hw);
      t[4] = (i32)(bx - dx * hl + dy * hw); t[5] = (i32)(by - dy * hl - dx * hw);
      fill_poly(rp, t, 3, 0, 0);
    }
    break; }
  case SK_INK: {
    /* the stroke's points, stretched with its box; a highlighter (6pt or wider) half see-through */
    const char *p = o->text ? o->text : "";
    LONG px0 = 0, py0 = 0, mx = 1, my = 1;
    double sx, sy;
    int first = 1, hl = o->linew >= 600;
    const char *q;
    for (q = p; *q; ) { long a = strtol(q, (char **)&q, 10), b; if (*q == ',') q++; b = strtol(q, (char **)&q, 10); if (a > mx) mx = a; if (b > my) my = b; while (*q == ';') q++; }
    sx = (double)(x1 - x0) / mx; sy = (double)(y1 - y0) / my;
    if (fabs(sx * 100 / zoom - 1) < 0.15) sx = zoom / 100.0;
    if (fabs(sy * 100 / zoom - 1) < 0.15) sy = zoom / 100.0;
    SetAPen(rp, pen(ctx, o->line ? o->line : FILL_ON));
    if (hl) SetAfPt(rp, (UWORD *)half_pat, 1);
    while (*p) {
      long a = strtol(p, (char **)&p, 10), b;
      LONG X, Y;
      if (*p == ',') p++;
      b = strtol(p, (char **)&p, 10);
      X = x0 + (LONG)(a * sx); Y = y0 + (LONG)(b * sy);
      if (!first) {
        if (hl) { LONG lx = px0 < X ? px0 : X, rx2 = px0 < X ? X : px0, ty2 = (py0 < Y ? py0 : Y) - w / 2, by2 = (py0 < Y ? Y : py0) + w / 2; RectFill(rp, lx - w / 4, ty2, rx2 + w / 4, by2); }
        else thick(rp, px0, py0, X, Y, w);
      }
      px0 = X; py0 = Y; first = 0;
      while (*p == ';') p++;
    }
    if (hl) SetAfPt(rp, NULL, 0);
    break; }
  case SK_ICON:
    SetAPen(rp, pen(ctx, o->line ? o->line : FILL_ON | 0x404040));
    icon_draw(rp, drw_icon_desc(o->text ? atoi(o->text) : 0), x0, y0, x1, y1, (int)((x1 - x0) / 20 + 1));
    break;
  }
  if (n >= 3) {
    if (o->fill) { SetAPen(rp, pen(ctx, o->fill)); fill_poly(rp, xy, n, 0, 0); }
    if (o->line) {
      SetAPen(rp, pen(ctx, o->line));
      for (i = 0; i < n; i++) thick(rp, xy[2 * i], xy[2 * i + 1], xy[2 * ((i + 1) % n)], xy[2 * ((i + 1) % n) + 1], w);
    }
  }
  if (o->shape <= SK_TRIANGLE || o->shape == SK_TEXTBOX) shape_text(rp, o, x0, y0, x1, y1, zoom, pen, ctx);
}

/* ------------------------------------------------------------------ the view */
ObjView *ov_new(void)
{
  ObjView *ov = (ObjView *)calloc(1, sizeof *ov);
  if (ov) { ov->sheet = -1; ov->last_ink = -1; ov->click_obj = -1; ov->elem = CE_CHART; ov->tcolor = 0x1F5FC9; ov->twidth = 200; }
  return ov;
}
static void cache_clear(ObjView *ov)
{
  int i;
  for (i = 0; i < ov->ncc; i++) prims_free(&ov->cc[i].pl);
  ov->ncc = 0;
}
static void pics_clear(ObjView *ov)
{
  int i;
  for (i = 0; i < ov->npc; i++) { free(ov->pc[i].px); blob_free(ov->pc[i].pic); }
  ov->npc = 0;
}
void ov_cleanup(ObjView *ov, Object *obj)
{
  int i;
  if (!ov) return;
  pics_clear(ov);
  if (ov->cm) for (i = 0; i < ov->ncube; i++) ReleasePen(ov->cm, ov->cube[i]);
  ov->ncube = 0; ov->cm = NULL;
  (void)obj;
}
void ov_free(ObjView *ov)
{
  if (!ov) return;
  cache_clear(ov);
  pics_clear(ov);
  free(ov->ink);
  free(ov);
}
static Sheet *SH(GridView *gv) { return gv->book->sh[gv->sheet]; }
/* an object's box on the screen (grid pixels) */
static void obj_box(GridView *gv, const DObj *o, LONG *x0, LONG *y0, LONG *x1, LONG *y1)
{
  *x0 = gv->colx(gv->g, o->from.c) + o->from.dx * gv->zoom / 100;
  *y0 = gv->rowy(gv->g, o->from.r) + o->from.dy * gv->zoom / 100;
  *x1 = gv->colx(gv->g, o->to.c) + o->to.dx * gv->zoom / 100;
  *y1 = gv->rowy(gv->g, o->to.r) + o->to.dy * gv->zoom / 100;
  if (*x1 < *x0) *x1 = *x0;
  if (*y1 < *y0) *y1 = *y0;
}
/* the selection checked against the sheet (objects come and go with undo) */
static void sel_check(ObjView *ov, GridView *gv)
{
  int i, k, n = drw_count(SH(gv));
  if (ov->book != gv->book || ov->sheet != gv->sheet) { ov->nsel = 0; ov->book = gv->book; ov->sheet = gv->sheet; ov->elem = CE_CHART; ov->last_ink = -1; }
  for (i = k = 0; i < ov->nsel; i++) if (ov->sel[i] < n) ov->sel[k++] = ov->sel[i];
  ov->nsel = k;
  if (ov->last_ink >= n) ov->last_ink = -1;
}
static int is_sel(ObjView *ov, int i) { int k; for (k = 0; k < ov->nsel; k++) if (ov->sel[k] == i) return 1; return 0; }
static LONG gpen(void *ctx, ULONG rgb)
{
  GridView *gv = (GridView *)ctx;
  return gv->pen(gv->g, rgb & 0xFFFFFF);
}
/* a chart laid out at w x h (kept until the workbook changes) */
static const PrimList *chart_prims(ObjView *ov, GridView *gv, const Chart *c, int w, int h)
{
  int i, k;
  for (i = 0; i < ov->ncc; i++)
    if (ov->cc[i].c == c && ov->cc[i].b == gv->book && ov->cc[i].changes == gv->book->changes && ov->cc[i].w == w && ov->cc[i].h == h && ov->cc[i].z == gv->zoom) return &ov->cc[i].pl;
  /* a slot: one for this chart, or the oldest */
  for (k = 0; k < ov->ncc && ov->cc[k].c != c; k++) ;
  if (k == ov->ncc) {
    if (ov->ncc < NCC) k = ov->ncc++;
    else { prims_free(&ov->cc[0].pl); memmove(&ov->cc[0], &ov->cc[1], (NCC - 1) * sizeof ov->cc[0]); k = NCC - 1; prims_init(&ov->cc[k].pl); }
  } else prims_free(&ov->cc[k].pl);
  prims_init(&ov->cc[k].pl);
  ov->cc[k].c = c; ov->cc[k].b = gv->book; ov->cc[k].changes = gv->book->changes; ov->cc[k].w = w; ov->cc[k].h = h; ov->cc[k].z = (int)gv->zoom;
  chart_render(gv->book, gv->sheet, c, w, h, (int)gv->zoom, ov_text_width, NULL, &ov->cc[k].pl);
  return &ov->cc[k].pl;
}
/* ---- pictures: dithered to a cube of pens (as Quill's view does) */
static const UBYTE bayer[4][4] = { { 0, 8, 2, 10 }, { 12, 4, 14, 6 }, { 3, 11, 1, 9 }, { 15, 7, 13, 5 } };
static void get_cube(ObjView *ov, Object *obj)
{
  LONG depth = GetBitMapAttr(_screen(obj)->RastPort.BitMap, BMA_DEPTH);
  int r, g, b, lv;
  if (ov->ncube) return;
  ov->cm = _screen(obj)->ViewPort.ColorMap;
  lv = depth >= 8 ? 6 : depth >= 5 ? 3 : 2;
  ov->cubelv = lv;
  for (r = 0; r < lv; r++) for (g = 0; g < lv; g++) for (b = 0; b < lv; b++)
    ov->cube[ov->ncube++] = ObtainBestPen(ov->cm, (ULONG)(r * 255 / (lv - 1)) * 0x01010101UL, (ULONG)(g * 255 / (lv - 1)) * 0x01010101UL,
                                          (ULONG)(b * 255 / (lv - 1)) * 0x01010101UL, OBP_Precision, PRECISION_IMAGE, TAG_DONE);
}
static UBYTE *pic_pens(ObjView *ov, GridView *gv, const DObj *o, LONG w, LONG h)
{
  int i, j, lv;
  u8 *rgb;
  UBYTE *px;
  LONG x, y;
  for (i = 0; i < ov->npc; i++) if (ov->pc[i].pic == o->pic && ov->pc[i].w == w && ov->pc[i].h == h) return ov->pc[i].px;
  if (w < 1 || h < 1 || w * h > 2000000 || !o->pic) return NULL;
  get_cube(ov, gv->obj);
  lv = ov->cubelv;
  if (!(rgb = img_decode(o->pic->data, o->pic->len, (u32)w, (u32)h))) return NULL;
  if (!(px = (UBYTE *)malloc((size_t)(w * h)))) { free(rgb); return NULL; }
  for (y = 0; y < h; y++)
    for (x = 0; x < w; x++) {
      const u8 *c = rgb + (y * w + x) * 3;
      int t = bayer[y & 3][x & 3] * 255 / 16, q[3];
      for (j = 0; j < 3; j++) { q[j] = (c[j] * (lv - 1) + t) / 255; if (q[j] > lv - 1) q[j] = lv - 1; }
      px[y * w + x] = (UBYTE)ov->cube[(q[0] * lv + q[1]) * lv + q[2]];
    }
  free(rgb);
  if (ov->npc >= NPC) { free(ov->pc[0].px); blob_free(ov->pc[0].pic); memmove(&ov->pc[0], &ov->pc[1], (NPC - 1) * sizeof ov->pc[0]); ov->npc--; }
  ov->pc[ov->npc].pic = blob_ref(o->pic); ov->pc[ov->npc].w = w; ov->pc[ov->npc].h = h; ov->pc[ov->npc].px = px;
  ov->npc++;
  return px;
}
static void draw_picture(ObjView *ov, GridView *gv, const DObj *o, LONG x0, LONG y0, LONG x1, LONG y1)
{
  LONG w = x1 - x0, h = y1 - y0, row, a, b;
  UBYTE *px = pic_pens(ov, gv, o, w, h);
  struct RastPort *rp = gv->rp;
  if (!px) {
    /* it can't be shown: a crossed box */
    SetAPen(rp, gpen(gv, 0x808080));
    Move(rp, gv->ox + x0, gv->oy + y0); Draw(rp, gv->ox + x1, gv->oy + y0); Draw(rp, gv->ox + x1, gv->oy + y1); Draw(rp, gv->ox + x0, gv->oy + y1); Draw(rp, gv->ox + x0, gv->oy + y0);
    Draw(rp, gv->ox + x1, gv->oy + y1); Move(rp, gv->ox + x1, gv->oy + y0); Draw(rp, gv->ox + x0, gv->oy + y1);
    return;
  }
  for (row = 0; row < h; row++) {
    LONG yy = y0 + row;
    if (yy < gv->hh || yy >= gv->h) continue;      /* (WriteChunkyPixels isn't clipped by a layer's region everywhere) */
    a = x0 < gv->hw ? gv->hw : x0; b = x1 - 1 >= gv->w ? gv->w - 1 : x1 - 1;
    if (b >= a) WriteChunkyPixels(rp, (ULONG)(gv->ox + a), (ULONG)(gv->oy + yy), (ULONG)(gv->ox + b), (ULONG)(gv->oy + yy), px + row * w + (a - x0), w);
  }
  if (o->line) {
    int k, lw = lw_px(o->linew, gv->zoom);
    SetAPen(rp, gpen(gv, o->line));
    for (k = 0; k < lw; k++) { Move(rp, gv->ox + x0 + k, gv->oy + y0 + k); Draw(rp, gv->ox + x1 - k, gv->oy + y0 + k); Draw(rp, gv->ox + x1 - k, gv->oy + y1 - k); Draw(rp, gv->ox + x0 + k, gv->oy + y1 - k); Draw(rp, gv->ox + x0 + k, gv->oy + y0 + k); }
  }
}
void ov_background(ObjView *ov, GridView *gv)
{
  Drawing *dr = SH(gv)->drw;
  DObj t;
  u32 w0, h0;
  LONG tw, th, x0, y0, x, y, row;
  UBYTE *px;
  if (!dr || !dr->bg || img_info(dr->bg->data, dr->bg->len, &w0, &h0) == IMG_NONE) return;
  tw = (LONG)w0 * gv->zoom / 100; th = (LONG)h0 * gv->zoom / 100;
  if (tw < 4 || th < 4) return;
  memset(&t, 0, sizeof t);
  t.pic = dr->bg;
  if (!(px = pic_pens(ov, gv, &t, tw, th))) return;
  /* tiles from A1's corner, wherever the sheet is scrolled to */
  x0 = gv->colx(gv->g, 0); y0 = gv->rowy(gv->g, 0);
  while (x0 + tw <= gv->hw) x0 += tw;
  while (y0 + th <= gv->hh) y0 += th;
  for (y = y0; y < gv->h; y += th)
    for (x = x0; x < gv->w; x += tw)
      for (row = 0; row < th; row++) {
        LONG yy = y + row, a = x < gv->hw ? gv->hw : x, b = x + tw - 1 >= gv->w ? gv->w - 1 : x + tw - 1;
        if (yy < gv->hh || yy >= gv->h || b < a) continue;
        WriteChunkyPixels(gv->rp, (ULONG)(gv->ox + a), (ULONG)(gv->oy + yy), (ULONG)(gv->ox + b), (ULONG)(gv->oy + yy), px + row * tw + (a - x), tw);
      }
}
void ov_sparks(ObjView *ov, GridView *gv)
{
  Sheet *s = SH(gv);
  int i, k;
  double v[512];
  if (!s->drw || !s->drw->ng) return;
  for (i = 0; i < s->drw->ng; i++) {
    SparkGroup *g = &s->drw->g[i];
    double lo, hi;
    int have_range = 0;
    for (k = 0; k < g->n; k++) {
      Spark *sp = &g->sp[k];
      LONG x0 = gv->colx(gv->g, sp->c), x1 = gv->colx(gv->g, sp->c + 1), y0 = gv->rowy(gv->g, sp->r), y1 = gv->rowy(gv->g, sp->r + 1);
      PrimList pl;
      int n;
      if (x1 - x0 < 4 || y1 - y0 < 4 || x1 <= gv->hw || y1 <= gv->hh || x0 >= gv->w || y0 >= gv->h) continue;
      if (x0 < gv->hw || y0 < gv->hh) continue;      /* (partly under the headings: left out) */
      if (!have_range) { spark_group_range(gv->book, gv->sheet, g, &lo, &hi); have_range = 1; }
      n = spark_values(gv->book, gv->sheet, sp, v, 512);
      prims_init(&pl);
      if (spark_render(g, v, n, (int)(x1 - x0 - 3), (int)(y1 - y0 - 3), lo, hi, &pl)) ov_paint(gv->rp, &pl, gv->ox + x0 + 1, gv->oy + y0 + 1, gpen, gv);
      prims_free(&pl);
    }
  }
  (void)ov;
}
static void handle_box(struct RastPort *rp, LONG x, LONG y, LONG fill, LONG edge)
{
  SetAPen(rp, fill); RectFill(rp, x - 2, y - 2, x + 2, y + 2);
  SetAPen(rp, edge); Move(rp, x - 3, y - 3); Draw(rp, x + 3, y - 3); Draw(rp, x + 3, y + 3); Draw(rp, x - 3, y + 3); Draw(rp, x - 3, y - 3);
}
/* the handles' places: 8 round a box (corners and edges' middles), 2 at a line's ends */
static int handles(const DObj *o, LONG x0, LONG y0, LONG x1, LONG y1, LONG *hx, LONG *hy)
{
  if (o->kind == OB_SHAPE && (o->shape == SK_LINE || o->shape == SK_ARROW)) {
    hx[0] = o->flipx ? x1 : x0; hy[0] = o->flipy ? y1 : y0; hx[1] = o->flipx ? x0 : x1; hy[1] = o->flipy ? y0 : y1;
    return 2;
  }
  hx[0] = x0; hy[0] = y0; hx[1] = (x0 + x1) / 2; hy[1] = y0; hx[2] = x1; hy[2] = y0; hx[3] = x1; hy[3] = (y0 + y1) / 2;
  hx[4] = x1; hy[4] = y1; hx[5] = (x0 + x1) / 2; hy[5] = y1; hx[6] = x0; hy[6] = y1; hx[7] = x0; hy[7] = (y0 + y1) / 2;
  return 8;
}
void ov_draw(ObjView *ov, GridView *gv)
{
  Sheet *s = SH(gv);
  int i, n = drw_count(s);
  struct Region *rg = NULL, *old = NULL;
  struct RastPort *rp = gv->rp;
  sel_check(ov, gv);
  if (!n) return;
  if (rp->Layer && (rg = NewRegion())) {
    struct Rectangle r;
    r.MinX = (WORD)(gv->ox + gv->hw); r.MinY = (WORD)(gv->oy + gv->hh); r.MaxX = (WORD)(gv->ox + gv->w - 1); r.MaxY = (WORD)(gv->oy + gv->h - 1);
    OrRectRegion(rg, &r);
    old = InstallClipRegion(rp->Layer, rg);
  }
  for (i = 0; i < n; i++) {
    DObj *o = drw_obj(s, i);
    LONG x0, y0, x1, y1;
    obj_box(gv, o, &x0, &y0, &x1, &y1);
    if (x1 < gv->hw || y1 < gv->hh || x0 >= gv->w || y0 >= gv->h || (x1 - x0 < 2 && y1 - y0 < 2 && o->kind != OB_SHAPE)) continue;
    if (o->kind == OB_CHART && o->chart) {
      if (x1 - x0 >= 20 && y1 - y0 >= 20) ov_paint(rp, chart_prims(ov, gv, o->chart, (int)(x1 - x0), (int)(y1 - y0)), gv->ox + x0, gv->oy + y0, gpen, gv);
    } else if (o->kind == OB_PICTURE) draw_picture(ov, gv, o, x0, y0, x1, y1);
    else if (o->kind == OB_SHAPE) ov_shape(rp, o, gv->ox + x0, gv->oy + y0, gv->ox + x1, gv->oy + y1, gv->zoom, gpen, gv);
    else if (o->kind == OB_SLICER) pivot2_draw_slicer(gv, i, x0, y0, x1, y1);    /* (pivot2.c) */
  }
  /* the selection: an outline and handles; a chart's selected part boxed */
  for (i = 0; i < ov->nsel; i++) {
    DObj *o = drw_obj(s, ov->sel[i]);
    LONG x0, y0, x1, y1, hx[8], hy[8];
    int k, nh;
    LONG acc = tpen(T_ACCENT), wh = gpen(gv, 0xFFFFFF);
    if (!o) continue;
    obj_box(gv, o, &x0, &y0, &x1, &y1);
    x0 += gv->ox; x1 += gv->ox; y0 += gv->oy; y1 += gv->oy;
    nh = handles(o, x0, y0, x1, y1, hx, hy);
    if (nh == 8) { SetAPen(rp, acc); Move(rp, x0 - 1, y0 - 1); Draw(rp, x1 + 1, y0 - 1); Draw(rp, x1 + 1, y1 + 1); Draw(rp, x0 - 1, y1 + 1); Draw(rp, x0 - 1, y0 - 1); }
    for (k = 0; k < nh; k++) handle_box(rp, hx[k], hy[k], wh, acc);
    if (i == 0 && o->kind == OB_CHART && o->chart && ov->elem != CE_CHART && x1 - x0 >= 20) {
      const PrimList *pl = chart_prims(ov, gv, o->chart, (int)(x1 - x0), (int)(y1 - y0));
      LONG ex0 = 0x7FFFFFF, ey0 = 0x7FFFFFF, ex1 = -1, ey1 = -1;
      int j;
      for (j = 0; j < pl->n; j++) {
        const Prim *p = &pl->p[j];
        LONG a0, b0, a1, b1;
        if (p->elem != ov->elem) continue;
        if (p->t == PR_LINE || p->t == PR_POLY) { int q; a0 = b0 = 0x7FFFFFF; a1 = b1 = -0x7FFFFFF; for (q = 0; q < p->np; q++) { if (p->pts[2 * q] < a0) a0 = p->pts[2 * q]; if (p->pts[2 * q] > a1) a1 = p->pts[2 * q]; if (p->pts[2 * q + 1] < b0) b0 = p->pts[2 * q + 1]; if (p->pts[2 * q + 1] > b1) b1 = p->pts[2 * q + 1]; } }
        else if (p->t == PR_TEXT) { LONG tw = p->vert ? p->h : p->w, th = p->vert ? p->w : p->h; a0 = p->x - (p->align == 1 ? tw / 2 : p->align == 2 ? tw : 0); b0 = p->y - (p->valign == 1 ? th / 2 : p->valign == 2 ? th : 0); a1 = a0 + tw; b1 = b0 + th; }
        else { a0 = p->x; b0 = p->y; a1 = p->x + p->w; b1 = p->y + p->h; }
        if (a0 < ex0) ex0 = a0;
        if (b0 < ey0) ey0 = b0;
        if (a1 > ex1) ex1 = a1;
        if (b1 > ey1) ey1 = b1;
      }
      if (ex1 >= ex0) {
        ex0 += x0 - 2; ex1 += x0 + 2; ey0 += y0 - 2; ey1 += y0 + 2;
        SetAPen(rp, acc);
        Move(rp, ex0, ey0); Draw(rp, ex1, ey0); Draw(rp, ex1, ey1); Draw(rp, ex0, ey1); Draw(rp, ex0, ey0);
        handle_box(rp, ex0, ey0, acc, wh); handle_box(rp, ex1, ey0, acc, wh); handle_box(rp, ex0, ey1, acc, wh); handle_box(rp, ex1, ey1, acc, wh);
      }
    }
  }
  if (rg) { InstallClipRegion(rp->Layer, old); DisposeRegion(rg); }
}

/* ------------------------------------------------------------------ the mouse */
/* the topmost object at grid pixel x, y (-1 none) */
static int near_seg(LONG x, LONG y, LONG ax, LONG ay, LONG bx, LONG by, LONG r)
{
  double dx = bx - ax, dy = by - ay, l2 = dx * dx + dy * dy, t = l2 > 0 ? ((x - ax) * dx + (y - ay) * dy) / l2 : 0, px, py;
  if (t < 0) t = 0;
  if (t > 1) t = 1;
  px = ax + t * dx - x; py = ay + t * dy - y;
  return px * px + py * py <= (double)r * r;
}
static int ink_hit(GridView *gv, const DObj *o, LONG x, LONG y, LONG x0, LONG y0, LONG r)
{
  const char *p = o->text ? o->text : "";
  LONG px0 = 0, py0 = 0;
  int first = 1;
  while (*p) {
    long a = strtol(p, (char **)&p, 10), b;
    LONG X, Y;
    if (*p == ',') p++;
    b = strtol(p, (char **)&p, 10);
    X = x0 + a * gv->zoom / 100; Y = y0 + b * gv->zoom / 100;
    if (!first && near_seg(x, y, px0, py0, X, Y, r)) return 1;
    if (first && near_seg(x, y, X, Y, X, Y, r)) return 1;
    px0 = X; py0 = Y; first = 0;
    while (*p == ';') p++;
  }
  return 0;
}
static int obj_at(GridView *gv, LONG x, LONG y)
{
  Sheet *s = SH(gv);
  int i;
  for (i = drw_count(s) - 1; i >= 0; i--) {
    DObj *o = drw_obj(s, i);
    LONG x0, y0, x1, y1;
    obj_box(gv, o, &x0, &y0, &x1, &y1);
    if (o->kind == OB_SHAPE && (o->shape == SK_LINE || o->shape == SK_ARROW)) {
      if (near_seg(x, y, o->flipx ? x1 : x0, o->flipy ? y1 : y0, o->flipx ? x0 : x1, o->flipy ? y0 : y1, 4)) return i;
      continue;
    }
    if (o->kind == OB_SHAPE && o->shape == SK_INK) { if (x >= x0 - 4 && x <= x1 + 4 && y >= y0 - 4 && y <= y1 + 4 && ink_hit(gv, o, x, y, x0, y0, 4)) return i; continue; }
    if (x >= x0 && x <= x1 && y >= y0 && y <= y1) return i;
  }
  return -1;
}
/* the handle of the selected object at x, y (-1 none) */
static int handle_at(ObjView *ov, GridView *gv, LONG x, LONG y)
{
  DObj *o;
  LONG x0, y0, x1, y1, hx[8], hy[8];
  int k, n;
  if (ov->nsel != 1 || !(o = drw_obj(SH(gv), ov->sel[0]))) return -1;
  obj_box(gv, o, &x0, &y0, &x1, &y1);
  n = handles(o, x0, y0, x1, y1, hx, hy);
  for (k = 0; k < n; k++) if (x >= hx[k] - 4 && x <= hx[k] + 4 && y >= hy[k] - 4 && y <= hy[k] + 4) return k;
  return -1;
}
/* the outline being dragged, drawn (and taken off again) in complement mode on the window */
static void xor_box(ObjView *ov, GridView *gv)
{
  struct RastPort *rp = _rp(gv->obj);
  LONG l = _mleft(gv->obj), t = _mtop(gv->obj), x0, y0, x1, y1;
  APTR clip;
  x0 = ov->bx0; y0 = ov->by0; x1 = ov->bx1; y1 = ov->by1;
  clip = MUI_AddClipping(muiRenderInfo(gv->obj), (WORD)(l + gv->hw), (WORD)(t + gv->hh), (WORD)(gv->w - gv->hw), (WORD)(gv->h - gv->hh));
  SetDrMd(rp, COMPLEMENT);
  if (ov->drag == DG_MOVE || ov->drag == DG_SIZE) {
    LONG dx = ov->cx - ov->sx, dy = ov->cy - ov->sy;
    if (ov->drag == DG_MOVE) { x0 += dx; x1 += dx; y0 += dy; y1 += dy; }
    else {
      DObj *o = drw_obj(SH(gv), ov->sel[0]);
      if (o && o->kind == OB_SHAPE && (o->shape == SK_LINE || o->shape == SK_ARROW)) {
        /* a line: its dragged end */
        LONG ax = o->flipx ? x1 : x0, ay = o->flipy ? y1 : y0, bx = o->flipx ? x0 : x1, by = o->flipy ? y0 : y1;
        if (ov->handle == 0) { ax += dx; ay += dy; } else { bx += dx; by += dy; }
        Move(rp, l + ax, t + ay); Draw(rp, l + bx, t + by);
        SetDrMd(rp, JAM1);
        MUI_RemoveClipping(muiRenderInfo(gv->obj), (struct Region *)clip);
        return;
      }
      if (ov->handle == 0 || ov->handle == 6 || ov->handle == 7) x0 += dx;
      if (ov->handle >= 2 && ov->handle <= 4) x1 += dx;
      if (ov->handle <= 2) y0 += dy;
      if (ov->handle >= 4 && ov->handle <= 6) y1 += dy;
    }
  } else { x0 = ov->sx; y0 = ov->sy; x1 = ov->cx; y1 = ov->cy; }
  if (ov->drag == DG_PLACE && (ov->tshape == SK_LINE || ov->tshape == SK_ARROW)) { Move(rp, l + x0, t + y0); Draw(rp, l + x1, t + y1); }
  else {
    if (x1 < x0) { LONG q = x0; x0 = x1; x1 = q; }
    if (y1 < y0) { LONG q = y0; y0 = y1; y1 = q; }
    Move(rp, l + x0, t + y0); Draw(rp, l + x1, t + y0); Draw(rp, l + x1, t + y1); Draw(rp, l + x0, t + y1); Draw(rp, l + x0, t + y0);
  }
  SetDrMd(rp, JAM1);
  MUI_RemoveClipping(muiRenderInfo(gv->obj), (struct Region *)clip);
}
/* a grid pixel as a sheet pixel at 100% */
static int to_sheet(GridView *gv, LONG x, LONG y, i32 *sx, i32 *sy)
{
  i32 r, c;
  if (x < gv->hw) x = gv->hw;
  if (y < gv->hh) y = gv->hh;
  gv->cell_at(gv->g, x, y, &r, &c);
  *sx = sheet_col_px(SH(gv), c) + (i32)((x - gv->colx(gv->g, c)) * 100 / gv->zoom);
  *sy = sheet_row_px(SH(gv), r) + (i32)((y - gv->rowy(gv->g, r)) * 100 / gv->zoom);
  return 1;
}
static void changed(GridView *gv) { if (gv->notify) gv->notify(gv->g, 1); }
static void selmoved(GridView *gv) { if (gv->notify) gv->notify(gv->g, 0); }
/* the drag finished: the object moved or sized, the stroke made an object, the shape placed */
static void drag_done(ObjView *ov, GridView *gv)
{
  Sheet *s = SH(gv);
  LONG dx = ov->cx - ov->sx, dy = ov->cy - ov->sy;
  int d = ov->drag;
  ov->drag = DG_NONE;
  if (d == DG_MOVE || d == DG_SIZE) {
    int k;
    if (!dx && !dy) return;
    drw_begin(gv->book, gv->sheet);
    for (k = 0; k < (d == DG_MOVE ? ov->nsel : 1); k++) {
      DObj *o = drw_obj(s, ov->sel[k]);
      i32 x0, y0, x1, y1, ddx = (i32)(dx * 100 / gv->zoom), ddy = (i32)(dy * 100 / gv->zoom);
      if (!o) continue;
      dobj_box(s, o, &x0, &y0, &x1, &y1);
      if (d == DG_MOVE) { if (x0 + ddx < 0) ddx = -x0; if (y0 + ddy < 0) ddy = -y0; x0 += ddx; x1 += ddx; y0 += ddy; y1 += ddy; }
      else if (o->kind == OB_SHAPE && (o->shape == SK_LINE || o->shape == SK_ARROW)) {
        i32 ax = o->flipx ? x1 : x0, ay = o->flipy ? y1 : y0, bx = o->flipx ? x0 : x1, by = o->flipy ? y0 : y1;
        if (ov->handle == 0) { ax += ddx; ay += ddy; } else { bx += ddx; by += ddy; }
        o->flipx = (u8)(bx < ax); o->flipy = (u8)(by < ay);
        x0 = ax < bx ? ax : bx; x1 = ax < bx ? bx : ax; y0 = ay < by ? ay : by; y1 = ay < by ? by : ay;
      } else {
        if (ov->handle == 0 || ov->handle == 6 || ov->handle == 7) x0 += ddx;
        if (ov->handle >= 2 && ov->handle <= 4) x1 += ddx;
        if (ov->handle <= 2) y0 += ddy;
        if (ov->handle >= 4 && ov->handle <= 6) y1 += ddy;
        if (x1 < x0) { i32 q = x0; x0 = x1; x1 = q; }
        if (y1 < y0) { i32 q = y0; y0 = y1; y1 = q; }
        if (x1 - x0 < 4) x1 = x0 + 4;
        if (y1 - y0 < 4) y1 = y0 + 4;
      }
      if (o->kind == OB_SHAPE && o->shape == SK_INK && d == DG_SIZE) {
        /* a stroke: its points stretched too */
        i32 ox0, oy0, ox1, oy1;
        Buf nb;
        const char *p = o->text ? o->text : "";
        dobj_box(s, o, &ox0, &oy0, &ox1, &oy1);
        buf_init(&nb);
        while (*p) {
          long a = strtol(p, (char **)&p, 10), b;
          if (*p == ',') p++;
          b = strtol(p, (char **)&p, 10);
          buf_printf(&nb, "%s%ld,%ld", nb.len ? ";" : "", ox1 > ox0 ? a * (x1 - x0) / (ox1 - ox0) : a, oy1 > oy0 ? b * (y1 - y0) / (oy1 - oy0) : b);
          while (*p == ';') p++;
        }
        if (nb.data) { free(o->text); o->text = buf_take(&nb); }
        buf_free(&nb);
      }
      dobj_place(s, o, x0, y0, x1, y1);
    }
    drw_end(gv->book);
    changed(gv);
  } else if (d == DG_INK) {
    i32 x0 = 0x7FFFFFF, y0 = 0x7FFFFFF, x1 = -1, y1 = -1;
    int k;
    DObj *o;
    Buf nb;
    if (ov->nink < 2) return;
    for (k = 0; k < ov->nink; k++) {
      if (ov->ink[2 * k] < x0) x0 = ov->ink[2 * k];
      if (ov->ink[2 * k] > x1) x1 = ov->ink[2 * k];
      if (ov->ink[2 * k + 1] < y0) y0 = ov->ink[2 * k + 1];
      if (ov->ink[2 * k + 1] > y1) y1 = ov->ink[2 * k + 1];
    }
    buf_init(&nb);
    for (k = 0; k < ov->nink; k++) buf_printf(&nb, "%s%ld,%ld", k ? ";" : "", (long)(ov->ink[2 * k] - x0), (long)(ov->ink[2 * k + 1] - y0));
    drw_begin(gv->book, gv->sheet);
    if ((o = drw_new(s, OB_SHAPE))) {
      o->shape = SK_INK; o->line = FILL_ON | (ov->tcolor & 0xFFFFFF); o->linew = (u16)ov->twidth;
      o->text = buf_take(&nb);
      snprintf(o->name, sizeof o->name, "Ink %lu", (unsigned long)o->id);
      o->anchor = AN_ONECELL;
      dobj_place(s, o, x0, y0, x1 > x0 ? x1 : x0 + 1, y1 > y0 ? y1 : y0 + 1);
      ov->last_ink = drw_count(s) - 1;
    }
    buf_free(&nb);
    drw_end(gv->book);
    changed(gv);
  } else if (d == DG_ERASE) {
    if (ov->erased) { drw_end(gv->book); changed(gv); }
    ov->erased = 0;
  } else if (d == DG_LASSO) {
    LONG x0 = ov->sx < ov->cx ? ov->sx : ov->cx, x1 = ov->sx < ov->cx ? ov->cx : ov->sx, y0 = ov->sy < ov->cy ? ov->sy : ov->cy, y1 = ov->sy < ov->cy ? ov->cy : ov->sy;
    int i;
    ov->nsel = 0;
    for (i = 0; i < drw_count(s) && ov->nsel < MAXSEL; i++) {
      LONG a0, b0, a1, b1;
      obj_box(gv, drw_obj(s, i), &a0, &b0, &a1, &b1);
      if (a0 >= x0 && a1 <= x1 && b0 >= y0 && b1 <= y1) ov->sel[ov->nsel++] = i;
    }
    ov->elem = CE_CHART;
    gv->redraw(gv->g);
    selmoved(gv);
  } else if (d == DG_PLACE) {
    /* a shape drawn by dragging (or a click: its usual size) */
    i32 ax, ay, bx, by;
    DObj *o;
    static const char *const nm[SK_N] = { "Rectangle", "Rounded Rectangle", "Oval", "Triangle", "Straight Connector", "Straight Arrow Connector", "TextBox", "Graphic", "Ink" };
    to_sheet(gv, ov->sx, ov->sy, &ax, &ay);
    to_sheet(gv, ov->cx, ov->cy, &bx, &by);
    if ((bx - ax) * (bx - ax) + (by - ay) * (by - ay) < 36) { bx = ax + (ov->tshape == SK_LINE || ov->tshape == SK_ARROW ? 120 : 144); by = ay + (ov->tshape == SK_LINE || ov->tshape == SK_ARROW ? 0 : 72); }
    drw_begin(gv->book, gv->sheet);
    if ((o = drw_new(s, OB_SHAPE))) {
      o->shape = (u8)ov->tshape;
      snprintf(o->name, sizeof o->name, "%s %lu", nm[o->shape % SK_N], (unsigned long)o->id);
      if (o->shape == SK_LINE || o->shape == SK_ARROW) { o->line = FILL_ON | 0x4472C4; o->linew = 100; o->flipx = (u8)(bx < ax); o->flipy = (u8)(by < ay); }
      else if (o->shape == SK_TEXTBOX) { o->fill = FILL_ON | 0xFFFFFF; o->line = FILL_ON | 0xBFBFBF; }
      else { o->fill = FILL_ON | 0x4472C4; o->line = FILL_ON | 0x2F528F; o->halign = 1; o->valign = 1; }
      dobj_place(s, o, ax < bx ? ax : bx, ay < by ? ay : by, ax < bx ? bx : ax, ay < by ? by : ay);
      ov->nsel = 1; ov->sel[0] = drw_count(s) - 1; ov->elem = CE_CHART;
    }
    drw_end(gv->book);
    ov->tool = TOOL_SELECT;
    changed(gv);
    selmoved(gv);
    if (o && o->shape == SK_TEXTBOX) DoMethod(_app(gv->obj), MUIM_Application_ReturnID, ID_X_EDITTEXT);
  }
}
static void ink_add(ObjView *ov, GridView *gv, LONG x, LONG y)
{
  i32 sx, sy;
  if (ov->nink >= 4000) return;
  if (ov->nink == ov->capink) {
    int cap = ov->capink ? ov->capink * 2 : 128;
    LONG *n = (LONG *)realloc(ov->ink, (size_t)cap * 2 * sizeof(LONG));
    if (!n) return;
    ov->ink = n; ov->capink = cap;
  }
  to_sheet(gv, x, y, &sx, &sy);
  if (ov->nink && ov->ink[2 * ov->nink - 2] == sx && ov->ink[2 * ov->nink - 1] == sy) return;
  ov->ink[2 * ov->nink] = sx; ov->ink[2 * ov->nink + 1] = sy;
  ov->nink++;
}
/* ink strokes under x, y rubbed out */
static void erase_at(ObjView *ov, GridView *gv, LONG x, LONG y)
{
  Sheet *s = SH(gv);
  int i;
  for (i = drw_count(s) - 1; i >= 0; i--) {
    DObj *o = drw_obj(s, i);
    LONG x0, y0, x1, y1;
    if (o->kind != OB_SHAPE || o->shape != SK_INK) continue;
    obj_box(gv, o, &x0, &y0, &x1, &y1);
    if (x < x0 - 6 || x > x1 + 6 || y < y0 - 6 || y > y1 + 6 || !ink_hit(gv, o, x, y, x0, y0, 6)) continue;
    if (!ov->erased) { drw_begin(gv->book, gv->sheet); ov->erased = 1; }
    drw_remove(s, i);
    ov->nsel = 0;
    gv->redraw(gv->g);
  }
}
int ov_mouse(ObjView *ov, GridView *gv, struct IntuiMessage *im, LONG x, LONG y)
{
  int in_cells = x >= gv->hw && y >= gv->hh && x < gv->w && y < gv->h;
  if (!ov || !gv->book) return 0;
  sel_check(ov, gv);
  if (im->Class == IDCMP_MOUSEMOVE || im->Class == IDCMP_INTUITICKS) {
    if (!ov->drag) return 0;
    if (im->Class == IDCMP_INTUITICKS) return 1;
    if (ov->drag == DG_INK) {
      struct RastPort *rp = _rp(gv->obj);
      LONG l = _mleft(gv->obj), t = _mtop(gv->obj);
      APTR clip = MUI_AddClipping(muiRenderInfo(gv->obj), (WORD)(l + gv->hw), (WORD)(t + gv->hh), (WORD)(gv->w - gv->hw), (WORD)(gv->h - gv->hh));
      SetAPen(rp, gpen(gv, ov->tcolor));
      SetDrMd(rp, JAM1);
      if (ov->twidth >= 600) SetAfPt(rp, (UWORD *)half_pat, 1);
      thick(rp, l + ov->cx, t + ov->cy, l + x, t + y, lw_px((u16)ov->twidth, gv->zoom));
      SetAfPt(rp, NULL, 0);
      MUI_RemoveClipping(muiRenderInfo(gv->obj), (struct Region *)clip);
      ov->cx = x; ov->cy = y;
      ink_add(ov, gv, x, y);
      return 1;
    }
    if (ov->drag == DG_ERASE) { erase_at(ov, gv, x, y); return 1; }
    if (ov->xor_on) xor_box(ov, gv);
    ov->cx = x; ov->cy = y;
    xor_box(ov, gv); ov->xor_on = 1;
    return 1;
  }
  if (im->Class != IDCMP_MOUSEBUTTONS) return 0;
  if (im->Code == SELECTUP) {
    if (!ov->drag) return 0;
    if (ov->xor_on) { xor_box(ov, gv); ov->xor_on = 0; }
    drag_done(ov, gv);
    gv->redraw(gv->g);
    return 1;
  }
  if (im->Code != SELECTDOWN || !in_cells) return 0;
  /* the tools */
  if (ov->tool == TOOL_PEN) { ov->drag = DG_INK; ov->nink = 0; ov->cx = x; ov->cy = y; ink_add(ov, gv, x, y); return 1; }
  if (ov->tool == TOOL_ERASER) { ov->drag = DG_ERASE; ov->erased = 0; erase_at(ov, gv, x, y); return 1; }
  if (ov->tool == TOOL_PLACE) { ov->drag = DG_PLACE; ov->sx = ov->cx = x; ov->sy = ov->cy = y; ov->xor_on = 0; return 1; }
  {
    int h = handle_at(ov, gv, x, y), i = h >= 0 ? ov->sel[0] : obj_at(gv, x, y);
    int dbl = DoubleClick(ov->secs, ov->micros, im->Seconds, im->Micros) && i == ov->click_obj && i >= 0;
    int shift = (im->Qualifier & (IEQUALIFIER_LSHIFT | IEQUALIFIER_RSHIFT)) != 0;
    ov->secs = im->Seconds; ov->micros = im->Micros; ov->click_obj = i;
    if (i < 0) {
      if (ov->tool == TOOL_LASSO) { ov->drag = DG_LASSO; ov->sx = ov->cx = x; ov->sy = ov->cy = y; ov->xor_on = 0; return 1; }
      if (ov->nsel) { ov->nsel = 0; ov->elem = CE_CHART; gv->redraw(gv->g); selmoved(gv); }
      return 0;
    }
    if (h < 0) {
      DObj *o = drw_obj(SH(gv), i);
      if (o->kind == OB_SLICER) {
        /* a slicer's buttons filter (pivot2.c); its header and frame select and move it */
        LONG x0, y0, x1, y1;
        obj_box(gv, o, &x0, &y0, &x1, &y1);
        if (pivot2_slicer_click(gv, i, x - x0, y - y0, x1 - x0, y1 - y0, shift)) { changed(gv); gv->redraw(gv->g); return 1; }
      }
      if (shift && !is_sel(ov, i) && ov->nsel < MAXSEL) ov->sel[ov->nsel++] = i;
      else if (!is_sel(ov, i)) { ov->nsel = 1; ov->sel[0] = i; ov->elem = CE_CHART; }
      else if (ov->nsel > 1 && !shift) { int k; for (k = 0; k < ov->nsel && ov->sel[k] != i; k++) ; ov->sel[k] = ov->sel[0]; ov->sel[0] = i; }
      /* a click inside a selected chart picks out the part under it */
      if (o->kind == OB_CHART && o->chart && ov->nsel == 1) {
        LONG x0, y0, x1, y1;
        obj_box(gv, o, &x0, &y0, &x1, &y1);
        if (x1 - x0 >= 20 && y1 - y0 >= 20) {
          int e = prims_hit(chart_prims(ov, gv, o->chart, (int)(x1 - x0), (int)(y1 - y0)), x - x0, y - y0);
          ov->elem = e == CE_NONE || e == CE_XAXIS || e == CE_YAXIS ? CE_CHART : e;
        }
      }
      gv->redraw(gv->g);
      selmoved(gv);
      if (dbl) {
        ULONG id = o->kind == OB_SHAPE && o->shape <= SK_TEXTBOX && o->shape != SK_LINE && o->shape != SK_ARROW ? ID_X_EDITTEXT :
                   o->kind == OB_CHART && ov->elem == CE_TITLE ? ID_X_CHARTTITLE : ID_X_FORMATSEL;
        DoMethod(_app(gv->obj), MUIM_Application_ReturnID, id);
        return 1;
      }
    }
    /* drag it (or its handle) */
    {
      DObj *o = drw_obj(SH(gv), ov->sel[0]);
      obj_box(gv, o, &ov->bx0, &ov->by0, &ov->bx1, &ov->by1);
      ov->drag = h >= 0 ? DG_SIZE : DG_MOVE; ov->handle = h;
      ov->sx = ov->cx = x; ov->sy = ov->cy = y; ov->xor_on = 0;
    }
    return 1;
  }
}
int ov_busy(ObjView *ov) { return ov && (ov->nsel || ov->tool != TOOL_SELECT || ov->drag); }
int ov_key(ObjView *ov, GridView *gv, UWORD code, UWORD q)
{
  Sheet *s;
  int shift = (q & (IEQUALIFIER_LSHIFT | IEQUALIFIER_RSHIFT)) != 0;
  if (!ov || !gv->book) return 0;
  sel_check(ov, gv);
  s = SH(gv);
  if (code & IECODE_UP_PREFIX) return 0;
  if (code == 0x45 && (ov->tool != TOOL_SELECT || ov->nsel)) {        /* Esc: the tool put down, the object let go */
    if (ov->tool != TOOL_SELECT) ov->tool = TOOL_SELECT; else { ov->nsel = 0; ov->elem = CE_CHART; }
    gv->redraw(gv->g); selmoved(gv);
    return 1;
  }
  if (!ov->nsel) return 0;
  if (code == 0x46 || code == 0x41) {                                 /* Del, Backspace: deleted */
    int k, j;
    drw_begin(gv->book, gv->sheet);
    /* highest first, so the others keep their numbers */
    for (k = 0; k < ov->nsel; k++) for (j = k + 1; j < ov->nsel; j++) if (ov->sel[j] > ov->sel[k]) { int t = ov->sel[j]; ov->sel[j] = ov->sel[k]; ov->sel[k] = t; }
    for (k = 0; k < ov->nsel; k++) {
      DObj *o = drw_obj(s, ov->sel[k]);
      if (o && o->kind == OB_SLICER) book_slicer_delete(gv->book, gv->sheet, ov->sel[k]);   /* (its filter goes too) */
      else drw_remove(s, ov->sel[k]);
    }
    drw_end(gv->book);
    ov->nsel = 0; ov->elem = CE_CHART; ov->last_ink = -1;
    changed(gv); selmoved(gv); gv->redraw(gv->g);
    return 1;
  }
  if (code >= 0x4C && code <= 0x4F) {                                 /* the arrows nudge it */
    int k;
    i32 dx = code == 0x4E ? 1 : code == 0x4F ? -1 : 0, dy = code == 0x4D ? 1 : code == 0x4C ? -1 : 0;
    if (shift) { dx *= 10; dy *= 10; }
    drw_begin(gv->book, gv->sheet);
    for (k = 0; k < ov->nsel; k++) {
      DObj *o = drw_obj(s, ov->sel[k]);
      i32 x0, y0, x1, y1;
      dobj_box(s, o, &x0, &y0, &x1, &y1);
      if (x0 + dx < 0 || y0 + dy < 0) continue;
      dobj_place(s, o, x0 + dx, y0 + dy, x1 + dx, y1 + dy);
    }
    drw_end(gv->book);
    changed(gv); gv->redraw(gv->g);
    return 1;
  }
  if (code == 0x44 || code == 0x43) {                                 /* Return: its text, or its format */
    DObj *o = drw_obj(s, ov->sel[0]);
    DoMethod(_app(gv->obj), MUIM_Application_ReturnID, o && o->kind == OB_SHAPE ? ID_X_EDITTEXT : ID_X_FORMATSEL);
    return 1;
  }
  return 1;      /* (other keys don't go to the cells while an object is selected) */
}

/* ------------------------------------------------------------------ for charttab.c */
int ov_selected(ObjView *ov) { return ov && ov->nsel ? ov->sel[0] : -1; }
int ov_selection(ObjView *ov, int *list, int max)
{
  int k;
  if (!ov) return 0;
  for (k = 0; k < ov->nsel && k < max; k++) list[k] = ov->sel[k];
  return k;
}
void ov_select(ObjView *ov, int i)
{
  if (!ov) return;
  ov->nsel = i >= 0 ? 1 : 0;
  ov->sel[0] = i;
  ov->elem = CE_CHART;
}
int ov_elem(ObjView *ov) { return ov ? ov->elem : CE_CHART; }
void ov_set_elem(ObjView *ov, int e) { if (ov) ov->elem = e; }
void ov_tool(ObjView *ov, int tool, ULONG color, int width, int shape)
{
  if (!ov) return;
  ov->tool = tool; ov->tcolor = color; ov->twidth = width; ov->tshape = shape;
  if (tool != TOOL_SELECT && tool != TOOL_LASSO) { ov->nsel = 0; ov->elem = CE_CHART; }
}
int ov_tool_now(ObjView *ov, ULONG *color, int *width)
{
  if (!ov) return TOOL_SELECT;
  if (color) *color = ov->tcolor;
  if (width) *width = ov->twidth;
  return ov->tool;
}
int ov_last_ink(ObjView *ov) { return ov ? ov->last_ink : -1; }
