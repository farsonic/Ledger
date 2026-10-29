/*
 * slicer.c - slicers (see slicer.h): their items, the filters their buttons make, and their
 * picture as drawing primitives. Written from GridCraft's documented behaviour (its slicers filter
 * tables through the AutoFilter); Ledger's filter pivot tables as Excel's do too.
 */
#include "slicer.h"
#include "table.h"
#include "pivot.h"
#include "ops.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>

Slicer *slicer_new(void)
{
  Slicer *s = (Slicer *)calloc(1, sizeof *s);
  if (!s) return NULL;
  s->cols = 1; s->header = 1; s->all = 1;
  strcpy(s->style, "SlicerStyleLight1");
  return s;
}
void slicer_free(Slicer *s)
{
  int i;
  if (!s) return;
  for (i = 0; i < s->nsel; i++) free(s->sel[i]);
  free(s->sel);
  free(s);
}
Slicer *slicer_copy(const Slicer *s)
{
  Slicer *n = (Slicer *)malloc(sizeof *n);
  int i;
  if (!n) return NULL;
  *n = *s;
  n->sel = NULL; n->nsel = 0;
  if (s->nsel && !(n->sel = (char **)calloc((size_t)s->nsel, sizeof(char *)))) { free(n); return NULL; }
  for (i = 0; i < s->nsel; i++) { n->sel[i] = str_dup(s->sel[i]); n->nsel = i + 1; }
  return n;
}
static void sel_clear(Slicer *s)
{
  int i;
  for (i = 0; i < s->nsel; i++) free(s->sel[i]);
  free(s->sel);
  s->sel = NULL; s->nsel = 0;
}
static int sel_has(const Slicer *s, const char *lab)
{
  int i;
  for (i = 0; i < s->nsel; i++) if (str_ieq(s->sel[i], lab)) return 1;
  return 0;
}
static void sel_add(Slicer *s, const char *lab)
{
  char **n;
  if (sel_has(s, lab) || !(n = (char **)realloc(s->sel, (size_t)(s->nsel + 1) * sizeof(char *)))) return;
  s->sel = n;
  n[s->nsel++] = str_dup(lab);
}
static void sel_remove(Slicer *s, const char *lab)
{
  int i;
  for (i = 0; i < s->nsel; i++) if (str_ieq(s->sel[i], lab)) { free(s->sel[i]); s->sel[i] = s->sel[--s->nsel]; return; }
}

/* ------------------------------------------------------------------ items */
static LPivot *pivot_named(Book *b, const char *name, int *sheet)
{
  int s, i;
  for (s = 0; s < b->nsh; s++)
    for (i = 0; i < b->sh[s]->npivots; i++) if (str_ieq(b->sh[s]->pivots[i].name, name)) { if (sheet) *sheet = s; return &b->sh[s]->pivots[i]; }
  return NULL;
}
static int cmp_lab(const void *a, const void *b)
{
  const char *x = *(const char *const *)a, *y = *(const char *const *)b;
  double dx, dy;
  int kx, ky;
  if (!strcmp(x, "(blank)") || !strcmp(y, "(blank)")) return !strcmp(x, "(blank)") - !strcmp(y, "(blank)");
  if (parse_number(x, &dx, &kx) && parse_number(y, &dy, &ky)) return dx < dy ? -1 : dx > dy;
  return str_ncasecmp(x, y, (size_t)-1);
}
int slicer_items(Book *b, const Slicer *s, char ***items, u8 **on)
{
  char **v = NULL;
  int n = 0, i;
  *items = NULL; if (on) *on = NULL;
  if (s->onpivot) {
    LPivot *p = pivot_named(b, s->source, NULL);
    if (!p) return 0;
    n = pivot_field_items_of(b, p, s->field, NULL, &v);
    if (s->sort == SS_DESC) for (i = 0; i < n / 2; i++) { char *t = v[i]; v[i] = v[n - 1 - i]; v[n - 1 - i] = t; }
  } else {
    int sh, k, cap = 0;
    LTable *t = table_find(b, s->source, &sh);
    i32 r;
    if (!t || (k = table_col(t, s->field)) < 0) return 0;
    for (r = t->r.r1 + t->header; r <= t->r.r2 - t->totals; r++) {
      char lab[256];
      u32 col;
      book_shown_text(b, sh, r, t->r.c1 + k, lab, sizeof lab, &col);
      if (!lab[0]) strcpy(lab, "(blank)");
      for (i = 0; i < n && !str_ieq(v[i], lab); i++) ;
      if (i < n) continue;
      if (n == cap) { char **nv = (char **)realloc(v, (size_t)(cap = cap ? cap * 2 : 16) * sizeof(char *)); if (!nv) break; v = nv; }
      v[n++] = str_dup(lab);
    }
    if (s->sort != SS_SOURCE && n > 1) qsort(v, (size_t)n, sizeof(char *), cmp_lab);
    if (s->sort == SS_DESC) for (i = 0; i < n / 2; i++) { char *x = v[i]; v[i] = v[n - 1 - i]; v[n - 1 - i] = x; }
  }
  *items = v;
  if (on && (*on = (u8 *)malloc((size_t)n + 1))) for (i = 0; i < n; i++) (*on)[i] = (u8)(s->all || sel_has(s, v[i]));
  return n;
}

/* ------------------------------------------------------------------ filtering */
void slicer_apply(Book *b, Slicer *s)
{
  char **items;
  int n = slicer_items(b, s, &items, NULL), i;
  if (s->onpivot) {
    int sh, pi, k;
    LPivot *p = pivot_named(b, s->source, &sh), np;
    PivotField *f = NULL;
    if (!p) { pivot_strings_free(items, n); return; }
    pi = pivot_index(b, sh, p);
    pivot_copy1(&np, p);
    for (k = 0; k < np.nsfilters; k++) if (str_ieq(np.sfilters[k].name, s->field)) f = &np.sfilters[k];
    if (!f) {
      PivotField *nf = (PivotField *)realloc(np.sfilters, (size_t)(np.nsfilters + 1) * sizeof(PivotField));
      if (nf) { np.sfilters = nf; f = &nf[np.nsfilters++]; memset(f, 0, sizeof *f); f->name = str_dup(s->field); }
    }
    if (f) {
      for (k = 0; k < f->nhidden; k++) free(f->hidden[k]);
      free(f->hidden); f->hidden = NULL; f->nhidden = 0;
      if (!s->all && (f->hidden = (char **)calloc((size_t)n + 1, sizeof(char *))))
        for (i = 0; i < n; i++) if (!sel_has(s, items[i])) f->hidden[f->nhidden++] = str_dup(items[i]);
    }
    book_pivot_set(b, sh, pi, &np);
    pivot_free1(&np);
  } else {
    int sh, k;
    LTable *t = table_find(b, s->source, &sh);
    if (t && (k = table_col(t, s->field)) >= 0 && t->header) {
      Sheet *ws = b->sh[sh];
      Rect f = t->r;
      f.r2 -= t->totals;
      if (ws->filter.r1 != f.r1 || ws->filter.c1 != f.c1 || ws->filter.c2 != f.c2 || ws->filter.r2 != f.r2) {
        if (s->all && ws->filter.r1 < 0) { pivot_strings_free(items, n); return; }
        book_autofilter(b, sh, f, 1);
      }
      if (s->all) book_filter_column(b, sh, f.c1 + k, NULL, -1, 0);
      else {
        const char **vals = (const char **)calloc((size_t)s->nsel + 1, sizeof(char *));
        int nv = 0, blanks = 0;
        for (i = 0; vals && i < s->nsel; i++) { if (!strcmp(s->sel[i], "(blank)")) blanks = 1; else vals[nv++] = s->sel[i]; }
        if (vals) book_filter_column(b, sh, f.c1 + k, vals, nv, blanks);
        free(vals);
      }
    }
  }
  pivot_strings_free(items, n);
}
void slicers_apply_all(Book *b)
{
  int s, i;
  for (s = 0; s < b->nsh; s++)
    for (i = 0; i < drw_count(b->sh[s]); i++) {
      DObj *o = drw_obj(b->sh[s], i);
      if (o->kind == OB_SLICER && o->slicer && !o->slicer->all) slicer_apply(b, o->slicer);
    }
}
void slicer_unique_name(Book *b, const char *field, char *out, size_t cap)
{
  int k, s, i, clash;
  for (k = 0; k < 10000; k++) {
    if (k) snprintf(out, cap, "%s %d", field, k); else snprintf(out, cap, "%s", field);
    clash = 0;
    for (s = 0; s < b->nsh; s++)
      for (i = 0; i < drw_count(b->sh[s]); i++) { DObj *o = drw_obj(b->sh[s], i); if (o->kind == OB_SLICER && o->slicer && str_ieq(o->slicer->name, out)) clash = 1; }
    if (!clash) return;
  }
}
int book_slicer_add(Book *b, int sheet, int onpivot, const char *source, const char *field, i32 r, i32 c)
{
  Sheet *ws;
  DObj *o;
  Slicer *sl;
  int idx;
  if (sheet < 0 || sheet >= b->nsh) return -1;
  if (onpivot) {
    LPivot *p = pivot_named(b, source, NULL);
    char **f;
    int nf, k, ok = 0;
    if (!p) return -1;
    nf = pivot_fields(b, p, &f);
    for (k = 0; k < nf; k++) if (str_ieq(f[k], field)) ok = 1;
    pivot_strings_free(f, nf);
    if (!ok) return -1;
  } else {
    LTable *t = table_find(b, source, NULL);
    if (!t || !t->header || table_col(t, field) < 0) return -1;
  }
  if (!(sl = slicer_new())) return -1;
  ws = b->sh[sheet];
  sl->onpivot = (u8)(onpivot != 0);
  snprintf(sl->source, sizeof sl->source, "%s", source);
  snprintf(sl->field, sizeof sl->field, "%s", field);
  snprintf(sl->caption, sizeof sl->caption, "%s", field);
  slicer_unique_name(b, field, sl->name, sizeof sl->name);
  drw_begin(b, sheet);
  if (!(o = drw_new(ws, OB_SLICER))) { drw_end(b); slicer_free(sl); return -1; }
  idx = drw_count(ws) - 1;
  o->slicer = sl;
  snprintf(o->name, sizeof o->name, "%s", sl->name);
  o->anchor = AN_ONECELL;
  o->from.r = r; o->from.c = c; o->from.dx = 4; o->from.dy = 4;
  {
    /* 168 x 200 pixels, as GridCraft's */
    i32 x0, y0;
    x0 = sheet_col_px(ws, c) + 4; y0 = sheet_row_px(ws, r) + 4;
    dobj_place(ws, o, x0, y0, x0 + 168, y0 + 200);
  }
  drw_end(b);
  return idx;
}
static Slicer *slicer_obj(Book *b, int sheet, int obj)
{
  DObj *o;
  if (sheet < 0 || sheet >= b->nsh || !(o = drw_obj(b->sh[sheet], obj)) || o->kind != OB_SLICER) return NULL;
  return o->slicer;
}
/* the source's sheet noted for undo too (its filter or pivot table changes) */
static void note_source(Book *b, const Slicer *s)
{
  int sh = -1;
  if (s->onpivot) pivot_named(b, s->source, &sh);
  else table_find(b, s->source, &sh);
  if (sh >= 0) book_note_sheet(b, sh);
}
int book_slicer_click(Book *b, int sheet, int obj, int k, int multi)
{
  Slicer *s = slicer_obj(b, sheet, obj);
  char **items;
  int n, i;
  if (!s) return 0;
  n = slicer_items(b, s, &items, NULL);
  if (k < 0 || k >= n) { pivot_strings_free(items, n); return 0; }
  drw_begin(b, sheet);
  note_source(b, s);
  if (multi || s->multi) {
    if (s->all) { sel_clear(s); for (i = 0; i < n; i++) sel_add(s, items[i]); s->all = 0; }
    if (sel_has(s, items[k])) sel_remove(s, items[k]); else sel_add(s, items[k]);
    /* everything selected is no filter */
    for (i = 0; i < n && sel_has(s, items[i]); i++) ;
    if (i == n) { sel_clear(s); s->all = 1; }
  } else if (!s->all && s->nsel == 1 && sel_has(s, items[k])) { sel_clear(s); s->all = 1; }
  else { sel_clear(s); sel_add(s, items[k]); s->all = 0; }
  slicer_apply(b, s);
  drw_end(b);
  pivot_strings_free(items, n);
  return 1;
}
int book_slicer_clear(Book *b, int sheet, int obj)
{
  Slicer *s = slicer_obj(b, sheet, obj);
  if (!s) return 0;
  drw_begin(b, sheet);
  note_source(b, s);
  sel_clear(s); s->all = 1;
  slicer_apply(b, s);
  drw_end(b);
  return 1;
}
int book_slicer_set(Book *b, int sheet, int obj, const Slicer *ns)
{
  DObj *o;
  Slicer *c;
  if (!slicer_obj(b, sheet, obj) || !(c = slicer_copy(ns))) return 0;
  drw_begin(b, sheet);
  note_source(b, ns);
  o = drw_obj(b->sh[sheet], obj);
  slicer_free(o->slicer);
  o->slicer = c;
  if (c->cols < 1) c->cols = 1;
  if (c->cols > 20) c->cols = 20;
  slicer_apply(b, c);
  drw_end(b);
  return 1;
}
int book_slicer_delete(Book *b, int sheet, int obj)
{
  Slicer *s = slicer_obj(b, sheet, obj);
  if (!s) return 0;
  drw_begin(b, sheet);
  note_source(b, s);
  if (!s->all) { sel_clear(s); s->all = 1; slicer_apply(b, s); }
  drw_remove(b->sh[sheet], obj);
  drw_end(b);
  return 1;
}

/* ------------------------------------------------------------------ the picture */
#define SL_ACCENT 0x4472C4
#define SL_SELFILL 0xBDD7EE
#define SL_OFFFILL 0xFFFFFF
#define SL_BORDER 0xBFBFBF
#define SL_TEXT 0x262626
#define SL_DIM 0x7F7F7F
typedef struct { int pad, hh, bh, gap, bw, cols, icon, ix_multi, ix_clear; } SLay;
static int zz(int v, int zoom) { int r = v * zoom / 100; return r < 1 ? 1 : r; }
static void slicer_layout(const Slicer *s, int w, int zoom, SLay *l)
{
  l->pad = zz(6, zoom); l->gap = zz(3, zoom);
  l->hh = s->header ? zz(22, zoom) : 0;
  l->bh = zz(20, zoom);
  l->cols = s->cols < 1 ? 1 : s->cols;
  l->bw = (w - 2 * l->pad - (l->cols - 1) * l->gap) / l->cols;
  if (l->bw < 4) l->bw = 4;
  l->icon = zz(16, zoom);
  l->ix_clear = w - l->pad - l->icon;
  l->ix_multi = l->ix_clear - l->icon - l->gap;
}
static Prim *rect(PrimList *o, i32 x, i32 y, i32 w, i32 h, u32 fill, u32 line)
{
  Prim *p = prim_add(o, PR_RECT, CE_NONE);
  if (p) { p->x = x; p->y = y; p->w = w; p->h = h; p->fill = fill; p->line = line; p->width = 1; }
  return p;
}
static void line(PrimList *o, i32 x0, i32 y0, i32 x1, i32 y1, u32 col)
{
  Prim *p = prim_add(o, PR_LINE, CE_NONE);
  if (!p || !(p->pts = (i32 *)malloc(4 * sizeof(i32)))) return;
  p->np = 2; p->pts[0] = x0; p->pts[1] = y0; p->pts[2] = x1; p->pts[3] = y1; p->line = col; p->width = 1;
}
static void text(PrimList *o, i32 x, i32 y, const char *t, int size, int bold, int align, u32 col)
{
  Prim *p = prim_add(o, PR_TEXT, CE_NONE);
  if (!p) return;
  p->x = x; p->y = y; p->text = str_dup(t); p->size = (u8)(size > 255 ? 255 : size); p->bold = (u8)bold; p->align = (u8)align; p->valign = 1; p->line = col;
}
/* a text cut to fit width pixels ("..." at its end) */
static void fit(const char *t, int w, int px, int bold, TextWidthFn tw, void *ctx, char *out, size_t cap)
{
  size_t n;
  snprintf(out, cap, "%s", t);
  if (!tw || tw(ctx, out, px, bold) <= w) return;
  for (n = strlen(out); n > 0; n--) {
    while (n > 0 && ((u8)out[n] & 0xC0) == 0x80) n--;
    if (n + 4 > cap) continue;
    strcpy(out + n, "...");
    if (tw(ctx, out, px, bold) <= w) return;
  }
}
int slicer_render(Book *b, const Slicer *s, int w, int h, int zoom, TextWidthFn tw, void *ctx, PrimList *out)
{
  SLay l;
  char **items, t[300];
  u8 *on;
  int n, k, px = zz(11, zoom) + 2;
  slicer_layout(s, w, zoom, &l);
  rect(out, 0, 0, w, h, FILL_ON | 0xFFFFFF, FILL_ON | SL_BORDER);
  if (s->header) {
    fit(s->caption[0] ? s->caption : s->field, l.ix_multi - 2 * l.pad, px, 1, tw, ctx, t, sizeof t);
    text(out, l.pad, l.hh / 2 + 1, t, px, 1, 0, FILL_ON | SL_TEXT);
    /* Multi-Select: three ticked lines; Clear Filter: a funnel crossed (red when there is a filter) */
    {
      i32 x = l.ix_multi, y = (l.hh - l.icon) / 2 + 1, q = l.icon;
      rect(out, x, y, q, q, s->multi ? FILL_ON | SL_SELFILL : 0, FILL_ON | (s->multi ? SL_ACCENT : SL_BORDER));
      for (k = 0; k < 3; k++) { i32 yy = y + q * (4 + 4 * k) / 16; line(out, x + q * 3 / 16, yy, x + q * 5 / 16, yy, FILL_ON | SL_TEXT); line(out, x + q * 7 / 16, yy, x + q * 13 / 16, yy, FILL_ON | SL_TEXT); }
      x = l.ix_clear;
      rect(out, x, y, q, q, 0, FILL_ON | SL_BORDER);
      {
        u32 c = FILL_ON | (s->all ? SL_DIM : SL_TEXT);
        line(out, x + q * 2 / 16, y + q * 3 / 16, x + q * 11 / 16, y + q * 3 / 16, c);
        line(out, x + q * 2 / 16, y + q * 3 / 16, x + q * 6 / 16, y + q * 8 / 16, c);
        line(out, x + q * 11 / 16, y + q * 3 / 16, x + q * 7 / 16, y + q * 8 / 16, c);
        line(out, x + q * 6 / 16, y + q * 8 / 16, x + q * 6 / 16, y + q * 13 / 16, c);
        line(out, x + q * 7 / 16, y + q * 8 / 16, x + q * 7 / 16, y + q * 12 / 16, c);
        if (!s->all) { line(out, x + q * 9 / 16, y + q * 9 / 16, x + q * 14 / 16, y + q * 14 / 16, FILL_ON | 0xC00000); line(out, x + q * 14 / 16, y + q * 9 / 16, x + q * 9 / 16, y + q * 14 / 16, FILL_ON | 0xC00000); }
      }
    }
  }
  n = slicer_items(b, s, &items, &on);
  for (k = 0; k < n; k++) {
    i32 x = l.pad + (k % l.cols) * (l.bw + l.gap), y = l.hh + l.pad / 2 + (k / l.cols) * (l.bh + l.gap);
    if (y + l.bh > h - 2) break;
    rect(out, x, y, l.bw, l.bh, FILL_ON | (on && on[k] ? SL_SELFILL : SL_OFFFILL), FILL_ON | (on && on[k] ? SL_ACCENT : SL_BORDER));
    fit(items[k], l.bw - 6, px - 1, 0, tw, ctx, t, sizeof t);
    text(out, x + l.bw / 2, y + l.bh / 2 + 1, t, px - 1, 0, 1, FILL_ON | SL_TEXT);
  }
  free(on);
  pivot_strings_free(items, n);
  return !out->oom;
}
int slicer_hit(Book *b, const Slicer *s, int w, int h, int zoom, i32 x, i32 y)
{
  SLay l;
  int n, k, col, row;
  char **items;
  slicer_layout(s, w, zoom, &l);
  if (x < 0 || y < 0 || x >= w || y >= h) return SH_NONE;
  if (y < l.hh) {
    if (x >= l.ix_clear && x < l.ix_clear + l.icon) return SH_CLEAR;
    if (x >= l.ix_multi && x < l.ix_multi + l.icon) return SH_MULTI;
    return SH_HEADER;
  }
  y -= l.hh + l.pad / 2; x -= l.pad;
  if (x < 0 || y < 0) return SH_HEADER;
  col = x / (l.bw + l.gap); row = y / (l.bh + l.gap);
  if (col >= l.cols || x % (l.bw + l.gap) >= l.bw || y % (l.bh + l.gap) >= l.bh) return SH_HEADER;
  k = row * l.cols + col;
  n = slicer_items(b, s, &items, NULL);
  pivot_strings_free(items, n);
  return k < n ? k : SH_HEADER;
}
