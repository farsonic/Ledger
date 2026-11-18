/* drawing.c - see drawing.h: a sheet's objects and sparklines kept, copied, moved with the grid */
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "drawing.h"
#include "slicer.h"
#include "data.h"

/* ------------------------------------------------------------------ pictures' bytes */
Blob *blob_new(const u8 *data, u32 len)
{
  Blob *b = (Blob *)calloc(1, sizeof *b);
  if (!b) return NULL;
  if (!(b->data = (u8 *)malloc(len ? len : 1))) { free(b); return NULL; }
  if (len) memcpy(b->data, data, len);
  b->len = len; b->refs = 1;
  return b;
}
Blob *blob_ref(Blob *b) { if (b) b->refs++; return b; }
void blob_free(Blob *b)
{
  if (!b || --b->refs > 0) return;
  free(b->data);
  free(b);
}

/* ------------------------------------------------------------------ objects */
static const char *const kind_names[] = { "Chart", "Picture", "Shape", "Slicer", "Control" };

Drawing *drw_of(Sheet *s)
{
  if (!s->drw && (s->drw = (Drawing *)calloc(1, sizeof(Drawing)))) s->drw->nextid = 1;
  return s->drw;
}
int drw_count(const Sheet *s) { return s->drw ? s->drw->n : 0; }
DObj *drw_obj(Sheet *s, int i) { return s->drw && i >= 0 && i < s->drw->n ? &s->drw->o[i] : NULL; }
DObj *drw_new(Sheet *s, int kind)
{
  Drawing *d = drw_of(s);
  DObj *n, *o;
  int i, k = 0;
  if (!d || !(n = (DObj *)realloc(d->o, (size_t)(d->n + 1) * sizeof(DObj)))) return NULL;
  d->o = n;
  o = &d->o[d->n++];
  memset(o, 0, sizeof *o);
  o->kind = (u8)kind;
  for (i = 0; i < d->n - 1; i++) if (d->o[i].id >= d->nextid) d->nextid = d->o[i].id + 1;
  o->id = d->nextid++;
  /* "Chart 2": one more than the highest number of its kind */
  for (i = 0; i < d->n - 1; i++) {
    const char *nm = d->o[i].name, *base = kind_names[kind < 5 ? kind : 2];
    size_t bl = strlen(base);
    if (!strncmp(nm, base, bl) && nm[bl] == ' ') { int v = atoi(nm + bl + 1); if (v > k) k = v; }
  }
  snprintf(o->name, sizeof o->name, "%s %d", kind_names[kind < 5 ? kind : 2], k + 1);
  o->from.r = o->from.c = 0;
  o->to.r = 15; o->to.c = 8;
  return o;
}
void dobj_clear(DObj *o)
{
  free(o->descr); free(o->text);
  blob_free(o->pic);
  chart_free(o->chart);
  slicer_free(o->slicer);
  formula_free(o->link);
  memset(o, 0, sizeof *o);
}
int dobj_copy(DObj *dst, const DObj *src)
{
  *dst = *src;
  dst->descr = src->descr ? str_dup(src->descr) : NULL;
  dst->text = src->text ? str_dup(src->text) : NULL;
  dst->pic = blob_ref(src->pic);
  dst->chart = src->chart ? chart_copy(src->chart) : NULL;
  dst->slicer = src->slicer ? slicer_copy(src->slicer) : NULL;
  dst->link = src->link ? formula_copy(src->link) : NULL;
  if ((src->descr && !dst->descr) || (src->text && !dst->text) || (src->chart && !dst->chart) || (src->slicer && !dst->slicer) || (src->link && !dst->link)) { dobj_clear(dst); return 0; }
  return 1;
}
void drw_remove(Sheet *s, int i)
{
  Drawing *d = s->drw;
  if (!d || i < 0 || i >= d->n) return;
  dobj_clear(&d->o[i]);
  memmove(&d->o[i], &d->o[i + 1], (size_t)(d->n - i - 1) * sizeof(DObj));
  d->n--;
  if (d->ngrp) drw_group_prune(s);
}
void drw_move_z(Sheet *s, int i, int to)
{
  Drawing *d = s->drw;
  DObj t;
  if (!d || i < 0 || i >= d->n) return;
  if (to < 0) to = 0;
  if (to >= d->n) to = d->n - 1;
  if (to == i) return;
  t = d->o[i];
  if (to > i) memmove(&d->o[i], &d->o[i + 1], (size_t)(to - i) * sizeof(DObj));
  else memmove(&d->o[to + 1], &d->o[to], (size_t)(i - to) * sizeof(DObj));
  d->o[to] = t;
}

/* ------------------------------------------------------------------ icons */
const char *const drw_icon_names[DRW_NICONS] = { "Star", "Heart", "Tick", "Cross", "Arrow Right", "Arrow Up", "House", "Envelope",
                                                 "Telephone", "Person", "Clock", "Calendar", "Light Bulb", "Flag", "Gear", "Warning" };
static const char *const icon_desc[DRW_NICONS] = {
  "Q 10 8 1 10 6 15 6 11 9 13 14 8 11 3 14 5 9 1 6 6 6",
  "E 5 6 3 E 11 6 3 Q 4 2 7 14 7 8 14 2 7",
  "P 3 2 8 6 12 14 3 P 3 2 9 6 13 14 4",
  "L 3 3 13 13 L 13 3 3 13 L 4 3 14 13 L 14 3 4 13",
  "Q 7 1 6 9 6 9 2 15 8 9 14 9 10 1 10",
  "Q 7 6 15 6 7 2 7 8 1 14 7 10 7 10 15",
  "Q 3 8 1 15 8 1 8 F 3 8 13 15 F 11 2 12 5",
  "R 1 3 15 13 P 3 1 3 8 9 15 3",
  "Q 9 3 1 6 1 8 5 6 7 9 10 11 8 15 10 13 15 4 13",
  "E 8 4 3 Q 6 3 15 4 9 7 8 9 8 12 9 13 15",
  "O 8 8 7 L 8 8 8 3 L 8 8 12 10",
  "R 1 3 15 15 F 1 3 15 6 L 5 1 5 4 L 11 1 11 4 F 4 9 5 10 F 8 9 9 10 F 12 9 13 10 F 4 12 5 13 F 8 12 9 13",
  "O 8 6 5 F 6 11 10 13 L 6 15 10 15",
  "L 3 1 3 15 L 4 1 4 15 Q 4 4 2 13 2 10 6 13 10 4 10",
  "O 8 8 5 E 8 8 2 L 8 1 8 3 L 8 13 8 15 L 1 8 3 8 L 13 8 15 8 L 3 3 4 4 L 12 12 13 13 L 13 3 12 4 L 3 13 4 12",
  "P 4 8 1 15 14 1 14 8 1 L 8 5 8 10 E 8 12 1",
};
const char *drw_icon_desc(int k) { return k >= 0 && k < DRW_NICONS ? icon_desc[k] : NULL; }
int drw_icon_by_name(const char *name)
{
  int k;
  for (k = 0; k < DRW_NICONS; k++) { size_t n = strlen(drw_icon_names[k]); if (!strncmp(name, drw_icon_names[k], n) && (!name[n] || name[n] == ' ')) return k; }
  return -1;
}

/* ------------------------------------------------------------------ the whole drawing */
void spark_group_clear(SparkGroup *g)
{
  int i;
  for (i = 0; i < g->n; i++) formula_free(g->sp[i].f);
  free(g->sp);
  g->sp = NULL; g->n = 0;
}
void drw_free(Drawing *d)
{
  int i;
  if (!d) return;
  for (i = 0; i < d->n; i++) dobj_clear(&d->o[i]);
  free(d->o);
  for (i = 0; i < d->ng; i++) spark_group_clear(&d->g[i]);
  free(d->g);
  blob_free(d->bg);
  free(d->grp);
  free(d);
}
Drawing *drw_copy(const Drawing *d)
{
  Drawing *c;
  int i, k;
  if (!d || !(c = (Drawing *)calloc(1, sizeof *c))) return NULL;
  c->nextid = d->nextid;
  c->bg = blob_ref(d->bg); c->bgtype = d->bgtype;
  if (!drw_groups_copy(c, d)) goto fail;
  if (d->n && !(c->o = (DObj *)calloc((size_t)d->n, sizeof(DObj)))) goto fail;
  for (i = 0; i < d->n; i++) { if (!dobj_copy(&c->o[i], &d->o[i])) goto fail; c->n = i + 1; }
  if (d->ng && !(c->g = (SparkGroup *)calloc((size_t)d->ng, sizeof(SparkGroup)))) goto fail;
  for (i = 0; i < d->ng; i++) {
    c->g[i] = d->g[i];
    c->g[i].sp = NULL; c->g[i].n = 0;
    c->ng = i + 1;
    if (d->g[i].n && !(c->g[i].sp = (Spark *)calloc((size_t)d->g[i].n, sizeof(Spark)))) goto fail;
    for (k = 0; k < d->g[i].n; k++) {
      c->g[i].sp[k] = d->g[i].sp[k];
      c->g[i].sp[k].f = d->g[i].sp[k].f ? formula_copy(d->g[i].sp[k].f) : NULL;
      c->g[i].n = k + 1;
    }
  }
  return c;
fail:
  drw_free(c);
  return NULL;
}

/* one coordinate of a corner after rows or columns came (n > 0) or went at at: a corner in the
   rows deleted goes to the first row after them, with no offset */
static void shift_coord(i32 *v, i32 *off, i32 at, i32 n, i32 max)
{
  if (n > 0) { if (*v >= at) { *v += n; if (*v >= max) { *v = max - 1; } } }
  else if (*v >= at - n) *v += n;
  else if (*v >= at) { *v = at; *off = 0; }
}
void drw_shift(Sheet *s, int rows, i32 at, i32 n)
{
  Drawing *d = s->drw;
  int i, k, j;
  i32 max = rows ? MAXROW : MAXCOL;
  if (!d) return;
  for (i = 0; i < d->n; i++) {
    DObj *o = &d->o[i];
    if (o->anchor == AN_ABSOLUTE) continue;
    if (rows) {
      i32 h0 = o->to.r - o->from.r;
      shift_coord(&o->from.r, &o->from.dy, at, n, max);
      if (o->anchor == AN_ONECELL) { o->to.r = o->from.r + h0; if (o->to.r >= max) o->to.r = max - 1; }
      else shift_coord(&o->to.r, &o->to.dy, at, n, max);
      if (o->to.r < o->from.r || (o->to.r == o->from.r && o->to.dy < o->from.dy)) { o->to.r = o->from.r; o->to.dy = o->from.dy + 1; }
    } else {
      i32 w0 = o->to.c - o->from.c;
      shift_coord(&o->from.c, &o->from.dx, at, n, max);
      if (o->anchor == AN_ONECELL) { o->to.c = o->from.c + w0; if (o->to.c >= max) o->to.c = max - 1; }
      else shift_coord(&o->to.c, &o->to.dx, at, n, max);
      if (o->to.c < o->from.c || (o->to.c == o->from.c && o->to.dx < o->from.dx)) { o->to.c = o->from.c; o->to.dx = o->from.dx + 1; }
    }
  }
  /* sparklines move with their cells, and go with them */
  for (j = 0; j < d->ng; j++) {
    SparkGroup *g = &d->g[j];
    for (i = k = 0; i < g->n; i++) {
      Spark sp = g->sp[i];
      i32 *a = rows ? &sp.r : &sp.c;
      if (n > 0) { if (*a >= at) *a += n; if (*a >= max) { formula_free(sp.f); continue; } }
      else if (*a >= at && *a < at - n) { formula_free(sp.f); continue; }
      else if (*a >= at - n) *a += n;
      g->sp[k++] = sp;
    }
    g->n = k;
  }
  for (j = k = 0; j < d->ng; j++) { if (d->g[j].n) d->g[k++] = d->g[j]; else spark_group_clear(&d->g[j]); }
  d->ng = k;
}
void drw_each_formula(Sheet *s, int home, void (*fn)(Formula *f, int home, void *arg), void *arg)
{
  Drawing *d = s->drw;
  int i, k;
  if (!d) return;
  for (i = 0; i < d->n; i++) {
    Chart *c = d->o[i].chart;
    if (d->o[i].link) fn(d->o[i].link, home, arg);
    if (!c) continue;
    for (k = 0; k < c->ns; k++) {
      CSeries *se = &c->s[k];
      if (se->name) fn(se->name, home, arg);
      if (se->cat) fn(se->cat, home, arg);
      if (se->val) fn(se->val, home, arg);
      if (se->size) fn(se->size, home, arg);
    }
  }
  for (i = 0; i < d->ng; i++) for (k = 0; k < d->g[i].n; k++) if (d->g[i].sp[k].f) fn(d->g[i].sp[k].f, home, arg);
}

/* ------------------------------------------------------------------ geometry */
i32 sheet_col_px(Sheet *s, i32 c)
{
  i32 x = 0, k;
  if (c > MAXCOL) c = MAXCOL;
  for (k = 0; k < c; k++) x += sheet_col_width(s, k);
  return x;
}
i32 sheet_row_px(Sheet *s, i32 r)
{
  i32 y = 0, k;
  if (r > MAXROW) r = MAXROW;
  /* rows past the last one stored are all the default height */
  for (k = 0; k < r && k < s->nrows; k++) y += sheet_row_height(s, k);
  if (r > s->nrows) y += (r - s->nrows) * s->defheight;
  return y;
}
i32 sheet_col_at(Sheet *s, i32 x, i32 *off)
{
  i32 c = 0, w;
  if (x < 0) x = 0;
  while (c < MAXCOL - 1 && x >= (w = sheet_col_width(s, c))) { x -= w; c++; }
  if (off) *off = x;
  return c;
}
i32 sheet_row_at(Sheet *s, i32 y, i32 *off)
{
  i32 r = 0, h;
  if (y < 0) y = 0;
  while (r < MAXROW - 1 && y >= (h = sheet_row_height(s, r))) {
    if (r >= s->nrows && s->defheight) {
      /* the rest are default rows: jump */
      i32 k = y / s->defheight;
      if (r + k > MAXROW - 1) k = MAXROW - 1 - r;
      r += k; y -= k * s->defheight;
      break;
    }
    y -= h; r++;
  }
  if (off) *off = y;
  return r;
}
void dobj_box(Sheet *s, const DObj *o, i32 *x0, i32 *y0, i32 *x1, i32 *y1)
{
  *x0 = sheet_col_px(s, o->from.c) + o->from.dx;
  *y0 = sheet_row_px(s, o->from.r) + o->from.dy;
  *x1 = sheet_col_px(s, o->to.c) + o->to.dx;
  *y1 = sheet_row_px(s, o->to.r) + o->to.dy;
  if (*x1 < *x0) *x1 = *x0;
  if (*y1 < *y0) *y1 = *y0;
}
void dobj_place(Sheet *s, DObj *o, i32 x0, i32 y0, i32 x1, i32 y1)
{
  if (x0 < 0) { x1 -= x0; x0 = 0; }
  if (y0 < 0) { y1 -= y0; y0 = 0; }
  if (x1 < x0 + 1) x1 = x0 + 1;
  if (y1 < y0 + 1) y1 = y0 + 1;
  o->from.c = sheet_col_at(s, x0, &o->from.dx);
  o->from.r = sheet_row_at(s, y0, &o->from.dy);
  o->to.c = sheet_col_at(s, x1, &o->to.dx);
  o->to.r = sheet_row_at(s, y1, &o->to.dy);
}
void drw_begin(Book *b, int sheet)
{
  book_begin(b);
  if (sheet >= 0 && sheet < b->nsh) book_note_sheet(b, sheet);
}
void drw_end(Book *b)
{
  book_end(b);
  b->dirty = 1;
}

/* ------------------------------------------------------------------ primitives */
void prims_init(PrimList *l) { memset(l, 0, sizeof *l); }
void prims_free(PrimList *l)
{
  int i;
  for (i = 0; i < l->n; i++) { free(l->p[i].pts); free(l->p[i].text); }
  free(l->p);
  memset(l, 0, sizeof *l);
}
Prim *prim_add(PrimList *l, int t, u16 elem)
{
  Prim *p;
  if (l->n == l->cap) {
    int cap = l->cap ? l->cap * 2 : 64;
    Prim *np;
    if (l->n > 200000 || !(np = (Prim *)realloc(l->p, (size_t)cap * sizeof(Prim)))) { l->oom = 1; return NULL; }
    l->p = np; l->cap = cap;
  }
  p = &l->p[l->n++];
  memset(p, 0, sizeof *p);
  p->t = (u8)t; p->elem = elem;
  return p;
}
/* distance squared from x, y to the segment a-b, compared with r squared */
static int near_seg(i32 x, i32 y, i32 ax, i32 ay, i32 bx, i32 by, i32 r)
{
  double dx = bx - ax, dy = by - ay, t, px, py, l2 = dx * dx + dy * dy;
  t = l2 > 0 ? ((x - ax) * dx + (y - ay) * dy) / l2 : 0;
  if (t < 0) t = 0;
  if (t > 1) t = 1;
  px = ax + t * dx - x; py = ay + t * dy - y;
  return px * px + py * py <= (double)r * r;
}
static int in_poly(const i32 *p, int n, i32 x, i32 y)
{
  int i, j, in = 0;
  for (i = 0, j = n - 1; i < n; j = i++) {
    i32 xi = p[2 * i], yi = p[2 * i + 1], xj = p[2 * j], yj = p[2 * j + 1];
    if (((yi > y) != (yj > y)) && (x < (double)(xj - xi) * (y - yi) / (double)(yj - yi) + xi)) in = !in;
  }
  return in;
}
int prims_hit(const PrimList *l, i32 x, i32 y)
{
  int i, k;
  for (i = l->n - 1; i >= 0; i--) {
    const Prim *p = &l->p[i];
    if (p->elem == CE_NONE) continue;
    switch (p->t) {
    case PR_RECT: case PR_TEXT: {
      i32 x0 = p->x, y0 = p->y, w = p->w, h = p->h;
      if (p->t == PR_TEXT) {
        /* text: the box its anchor and size suggest */
        if (!w) continue;
        if (p->vert) { i32 t = w; w = h; h = t; }
        x0 -= p->align == 1 ? w / 2 : p->align == 2 ? w : 0;
        y0 -= p->valign == 1 ? h / 2 : p->valign == 2 ? h : 0;
      }
      if (x >= x0 && x < x0 + w && y >= y0 && y < y0 + h && (p->fill || p->t == PR_TEXT || p->elem == CE_CHART || p->elem == CE_PLOT || p->elem == CE_LEGEND)) return p->elem;
      break; }
    case PR_ELLIPSE: {
      double rx = p->w / 2.0, ry = p->h / 2.0, cx = p->x + rx, cy = p->y + ry;
      if (rx < 3) rx = 3;
      if (ry < 3) ry = 3;
      if ((x - cx) * (x - cx) / (rx * rx) + (y - cy) * (y - cy) / (ry * ry) <= 1.0) return p->elem;
      break; }
    case PR_POLY:
      if (p->np >= 3 && in_poly(p->pts, p->np, x, y)) return p->elem;
      break;
    case PR_LINE:
      for (k = 0; k + 1 < p->np; k++) if (near_seg(x, y, p->pts[2 * k], p->pts[2 * k + 1], p->pts[2 * k + 2], p->pts[2 * k + 3], 3 + p->width / 2)) return p->elem;
      break;
    }
  }
  return CE_NONE;
}

/* ------------------------------------------------------------------ WordArt */
#define RGB_(v) (FILL_ON | (v))
const WordArtStyle drw_wordart_styles[DRW_NWORDART] = {
  { "Classic", RGB_(0x4472C4), 0, 0, 0, 1, 0, 360 },
  { "Midnight", RGB_(0x1B2A49), 0, 0, RGB_(0x8A94A6), 1, 0, 400 },
  { "Sunrise", RGB_(0xF2A33A), RGB_(0xB5451B), 150, 0, 1, 0, 400 },
  { "Lagoon", RGB_(0x1F9E9A), RGB_(0x0B4F5C), 100, RGB_(0x9CC9C7), 1, 1, 360 },
  { "Ember", RGB_(0xD64527), 0, 0, RGB_(0x5A1A0E), 1, 0, 440 },
  { "Mint Outline", RGB_(0xFFFFFF), RGB_(0x2E9B6A), 200, 0, 1, 0, 400 },
  { "Ink Stamp", RGB_(0x2F3A8F), RGB_(0x2F3A8F), 50, 0, 1, 0, 320 },
  { "Orchid", RGB_(0x8E4FB5), 0, 0, RGB_(0xD9C2E8), 0, 1, 400 },
  { "Graphite", RGB_(0x4A4F57), RGB_(0xFFFFFF), 100, RGB_(0x9AA0A8), 1, 0, 360 },
  { "Lime Pop", RGB_(0xA6D61C), RGB_(0x3F5A00), 150, RGB_(0x6E8B2C), 1, 1, 400 },
};
void drw_wordart_style(DObj *o, int style)
{
  const WordArtStyle *w = &drw_wordart_styles[style >= 0 && style < DRW_NWORDART ? style : 0];
  o->wordart = 1;
  o->tcolor = w->fill; o->toutline = w->outline; o->toutw = w->outw; o->tshadow = w->shadow;
  o->bold = w->bold; o->italic = w->italic; o->fsize = w->size;
  o->fill = 0; o->line = 0;
  o->halign = 1; o->valign = 1;
}
int book_wordart_add(Book *b, int sheet, int style, const char *text, i32 x, i32 y)
{
  Sheet *s;
  DObj *o;
  long chars = 0, w, h;
  const char *p;
  int idx = -1;
  if (sheet < 0 || sheet >= b->nsh) return -1;
  s = b->sh[sheet];
  if (s->protect && !(s->protallow & PA_OBJECTS)) return -1;   /* (as GridCraft: not on a protected sheet) */
  if (!text || !*text) text = DRW_WORDART_TEXT;
  for (p = text; *p; p++) if ((*p & 0xC0) != 0x80) chars++;
  if (chars > 1000) chars = 1000;
  drw_begin(b, sheet);
  if ((o = drw_new(s, OB_SHAPE))) {
    o->shape = SK_TEXTBOX;
    drw_wordart_style(o, style);
    o->text = str_dup(text);
    snprintf(o->name, sizeof o->name, "WordArt %lu", (unsigned long)o->id);
    /* about 0.6 of the size across per character, one and a half times it high (pixels at 96 dpi) */
    w = chars * o->fsize * 96 * 6 / (720 * 10) + 24;
    h = (long)o->fsize * 96 * 15 / (720 * 10) + 12;
    if (w > 4000) w = 4000;
    dobj_place(s, o, x, y, x + (i32)w, y + (i32)h);
    idx = drw_count(s) - 1;
  }
  drw_end(b);
  return idx;
}
