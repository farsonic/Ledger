/*
 * chart.c - see drawing.h: charts' model (series as formulas), making one from a range of cells,
 * Excel's quick layouts and colour sets, the values pulled out of the cells, and the chart laid out
 * as drawing primitives for every kind: column, bar, line, area (clustered, stacked, 100%), pie,
 * doughnut, scatter, bubble, radar, stock, combo, and Excel 2016's histogram, box & whisker,
 * waterfall, funnel, treemap and sunburst.
 *
 * Written for Ledger from the documented behaviour of Excel's charts and GridCraft's chart crate
 * (which Ledger follows); the layout code is Ledger's own.
 */
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>
#include "drawing.h"
#include "calc.h"
#include "ops.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif
#define MAXPTS 4000              /* points a series shows at most */

static const char *const knames[CK_N] = {
  "Clustered Column", "Stacked Column", "100% Stacked Column", "Clustered Bar", "Stacked Bar", "100% Stacked Bar",
  "Line", "Line with Markers", "Stacked Line", "Pie", "Doughnut", "Area", "Stacked Area", "100% Stacked Area",
  "Scatter", "Scatter with Lines", "Bubble", "Radar", "Histogram", "Waterfall", "Funnel", "Treemap", "Sunburst",
  "Box & Whisker", "Stock", "Combo"
};
const char *chart_kind_name(int k) { return k >= 0 && k < CK_N ? knames[k] : "Chart"; }
int chart_kind_by_name(const char *name)
{
  int k;
  for (k = 0; k < CK_N; k++) if (str_ieq(knames[k], name)) return k;
  return -1;
}
int chart_is_pielike(int k) { return k == CK_PIE || k == CK_DOUGHNUT || k == CK_FUNNEL || k == CK_TREEMAP || k == CK_SUNBURST; }
int chart_is_cartesian(int k) { return !chart_is_pielike(k) && k != CK_RADAR; }
static int is_bar(int k) { return k == CK_BAR || k == CK_BARSTACK || k == CK_BAR100; }
static int is_col(int k) { return k == CK_COL || k == CK_COLSTACK || k == CK_COL100; }
static int is_line(int k) { return k == CK_LINE || k == CK_LINEMARK || k == CK_LINESTACK; }
static int is_area(int k) { return k == CK_AREA || k == CK_AREASTACK || k == CK_AREA100; }
static int is_xy(int k) { return k == CK_SCATTER || k == CK_SCATTERLINE || k == CK_BUBBLE; }

/* ------------------------------------------------------------------ the model */
Chart *chart_new(int kind)
{
  Chart *c = (Chart *)calloc(1, sizeof *c);
  if (!c) return NULL;
  c->kind = (u8)(kind >= 0 && kind < CK_N ? kind : CK_COL);
  c->has_title = 1;
  c->legend = LG_BOTTOM;
  c->hgrid = 1;
  c->style = 1;
  return c;
}
static void series_clear(CSeries *s)
{
  formula_free(s->name); formula_free(s->cat); formula_free(s->val); formula_free(s->size);
  free(s->lit);
  memset(s, 0, sizeof *s);
}
void chart_free(Chart *c)
{
  int i;
  if (!c) return;
  for (i = 0; i < c->ns; i++) series_clear(&c->s[i]);
  free(c->s); free(c->title); free(c->xtitle); free(c->ytitle);
  free(c);
}
static Formula *fcopy(const Formula *f) { return f ? formula_copy(f) : NULL; }
Chart *chart_copy(const Chart *c)
{
  Chart *n = (Chart *)malloc(sizeof *n);
  int i;
  if (!n) return NULL;
  *n = *c;
  n->title = c->title ? str_dup(c->title) : NULL;
  n->xtitle = c->xtitle ? str_dup(c->xtitle) : NULL;
  n->ytitle = c->ytitle ? str_dup(c->ytitle) : NULL;
  n->s = NULL; n->ns = 0;
  if (c->ns && !(n->s = (CSeries *)calloc((size_t)c->ns, sizeof(CSeries)))) { chart_free(n); return NULL; }
  for (i = 0; i < c->ns; i++) {
    CSeries *d = &n->s[i];
    const CSeries *s = &c->s[i];
    *d = *s;
    d->name = fcopy(s->name); d->cat = fcopy(s->cat); d->val = fcopy(s->val); d->size = fcopy(s->size);
    d->lit = s->lit ? str_dup(s->lit) : NULL;
    n->ns = i + 1;
  }
  return n;
}
/* a formula that is one absolute reference to sheet!area */
static Formula *ref_formula(int sheet, Rect a)
{
  Formula *f = (Formula *)calloc(1, sizeof *f);
  if (!f || !(f->t = (Tok *)calloc(1, sizeof(Tok)))) { free(f); return NULL; }
  f->n = 1;
  f->t[0].op = (a.r1 == a.r2 && a.c1 == a.c2) ? T_REF : T_AREA;
  f->t[0].flags = RF_ABSR1 | RF_ABSC1 | RF_ABSR2 | RF_ABSC2 | RF_SHEET;
  f->t[0].a = (u16)sheet;
  f->t[0].r1 = a.r1; f->t[0].c1 = a.c1; f->t[0].r2 = a.r2; f->t[0].c2 = a.c2;
  return f;
}
static Rect rc(i32 r1, i32 c1, i32 r2, i32 c2) { Rect r; r.r1 = r1; r.c1 = c1; r.r2 = r2; r.c2 = c2; return r; }
static int is_text_or_empty(Book *b, int sheet, i32 r, i32 c)
{
  const Value *v = cell_value(b, sheet, r, c);
  return v->t == V_STR || v->t == V_EMPTY;
}
/* (see drawing.h) series from area, as Excel lays them out: a heading row when the first row holds
   text (and nothing but text after its first cell), a label column when the first column holds
   text - or a blank corner above it, which marks numbers there (years) as labels too */
int chart_set_source(Book *b, Chart *c, int sheet, Rect a, int byrows)
{
  i32 r, cc;
  int header = 0, labels = 0, any = 0, k, n;
  Rect d;
  CSeries *ns;
  i32 lr = sheet_last_row(b->sh[sheet]), lc = sheet_last_col(b->sh[sheet]);
  if (a.r2 > lr) a.r2 = lr;
  if (a.c2 > lc) a.c2 = lc;
  if (a.r2 < a.r1 || a.c2 < a.c1) return 0;
  if (a.r2 > a.r1) {
    for (cc = a.c1; cc <= a.c2; cc++) if (cell_value(b, sheet, a.r1, cc)->t == V_STR) any = 1;
    header = any;
    for (cc = a.c1 + 1; cc <= a.c2 && header; cc++) if (!is_text_or_empty(b, sheet, a.r1, cc)) header = 0;
  }
  if (a.c2 > a.c1) {
    labels = 1;
    if (!(header && cell_value(b, sheet, a.r1, a.c1)->t == V_EMPTY))
      for (r = a.r1 + header; r <= a.r2 && labels; r++) if (!is_text_or_empty(b, sheet, r, a.c1)) labels = 0;
  }
  d = rc(a.r1 + header, a.c1 + labels, a.r2, a.c2);
  if (d.r2 < d.r1 || d.c2 < d.c1) return 0;
  if (byrows < 0) byrows = (d.c2 - d.c1) > (d.r2 - d.r1);
  n = byrows ? d.r2 - d.r1 + 1 : d.c2 - d.c1 + 1;
  if (n > 255) n = 255;
  if (!(ns = (CSeries *)calloc((size_t)n, sizeof(CSeries)))) return 0;
  for (k = 0; k < n; k++) {
    CSeries *s = &ns[k];
    s->kind = CK_SAME;
    if (!byrows) {
      cc = d.c1 + k;
      if (header) s->name = ref_formula(sheet, rc(a.r1, cc, a.r1, cc));
      if (labels) s->cat = ref_formula(sheet, rc(d.r1, a.c1, d.r2, a.c1));
      s->val = ref_formula(sheet, rc(d.r1, cc, d.r2, cc));
    } else {
      r = d.r1 + k;
      if (labels) s->name = ref_formula(sheet, rc(r, a.c1, r, a.c1));
      if (header) s->cat = ref_formula(sheet, rc(a.r1, d.c1, a.r1, d.c2));
      s->val = ref_formula(sheet, rc(r, d.c1, r, d.c2));
    }
  }
  /* bubbles: the series go in threes as Excel takes them (x, y, size) when there are no labels */
  if (c->kind == CK_BUBBLE && n >= 3 && !labels && !byrows) {
    CSeries *b3 = &ns[0];
    formula_free(b3->cat); b3->cat = ns[0].val;
    b3->val = ns[1].val; ns[1].val = NULL;
    b3->size = ns[2].val; ns[2].val = NULL;
    formula_free(b3->name); b3->name = ns[1].name; ns[1].name = NULL;
    for (k = 1; k < n; k++) series_clear(&ns[k]);
    n = 1;
  } else if ((c->kind == CK_SCATTER || c->kind == CK_SCATTERLINE) && n >= 2 && !labels && !byrows) {
    /* scatter without labels: the first column is X for the rest */
    for (k = 1; k < n; k++) { formula_free(ns[k].cat); ns[k].cat = fcopy(ns[0].val); }
    series_clear(&ns[0]);
    memmove(&ns[0], &ns[1], (size_t)(n - 1) * sizeof(CSeries));
    n--;
  }
  /* bubbles need sizes: without a column of them, each value is its own bubble's size */
  if (c->kind == CK_BUBBLE) for (k = 0; k < n; k++) if (!ns[k].size) ns[k].size = fcopy(ns[k].val);
  for (k = 0; k < c->ns; k++) series_clear(&c->s[k]);
  free(c->s);
  c->s = ns; c->ns = n;
  c->hassrc = 1; c->srcsheet = (u16)sheet; c->src = a; c->byrows = (u8)byrows;
  return 1;
}
Chart *chart_from_range(Book *b, int sheet, Rect a, int kind, int byrows)
{
  Chart *c = chart_new(kind);
  int k, any = 0;
  if (!c) return NULL;
  if (!chart_set_source(b, c, sheet, a, byrows)) { chart_free(c); return NULL; }
  for (k = 0; k < c->ns && !any; k++) {
    const Formula *f = c->s[k].val;
    i32 r, cc;
    if (!f || f->t[0].op != T_AREA) { any = f && f->t[0].op == T_REF && cell_value(b, sheet, f->t[0].r1, f->t[0].c1)->t == V_NUM; continue; }
    for (r = f->t[0].r1; r <= f->t[0].r2 && !any; r++) for (cc = f->t[0].c1; cc <= f->t[0].c2 && !any; cc++) any = cell_value(b, sheet, r, cc)->t == V_NUM;
  }
  if (!any) { chart_free(c); return NULL; }
  if (kind == CK_PIE || kind == CK_DOUGHNUT || kind == CK_FUNNEL || kind == CK_TREEMAP || kind == CK_SUNBURST || c->ns > 1) c->legend = kind == CK_FUNNEL ? LG_NONE : LG_BOTTOM;
  else c->legend = LG_NONE;
  if (kind == CK_PIE || kind == CK_DOUGHNUT || kind == CK_TREEMAP || kind == CK_SUNBURST || kind == CK_FUNNEL) c->hgrid = 0;
  return c;
}
int chart_recommend(Book *b, int sheet, Rect a, int *kinds, int max)
{
  Chart *c = chart_from_range(b, sheet, a, CK_COL, -1);
  static const int two[] = { CK_SCATTER, CK_LINEMARK, CK_COL };
  static const int onebig[] = { CK_LINE, CK_COL, CK_AREA };
  static const int onesmall[] = { CK_COL, CK_BAR, CK_PIE, CK_DOUGHNUT, CK_LINEMARK };
  static const int manybig[] = { CK_LINE, CK_COL, CK_AREA, CK_COLSTACK };
  static const int manysmall[] = { CK_COL, CK_BAR, CK_COLSTACK, CK_LINEMARK, CK_BARSTACK };
  const int *list;
  int n, i, k = 0, points = 0, labelled, nonneg = 1;
  if (!c) return 0;
  {
    CData d;
    memset(&d, 0, sizeof d);
    if (chart_resolve(b, sheet, c, &d)) {
      points = d.ns ? d.s[0].n : 0;
      for (i = 0; i < d.ns; i++) { int j; for (j = 0; j < d.s[i].n; j++) if (d.s[i].v[j] < 0) nonneg = 0; }
    }
    cdata_free(&d);
  }
  labelled = c->ns && c->s[0].cat != NULL;
  if (c->ns == 2 && !labelled && !c->byrows) { list = two; n = 3; }
  else if (c->ns == 1) { if (points > 12) { list = onebig; n = 3; } else { list = onesmall; n = 5; } }
  else if (points > 12) { list = manybig; n = 4; }
  else { list = manysmall; n = 5; }
  for (i = 0; i < n && k < max; i++) {
    if ((list[i] == CK_PIE || list[i] == CK_DOUGHNUT) && (!nonneg || points < 2)) continue;
    kinds[k++] = list[i];
  }
  chart_free(c);
  return k;
}
static void set_text(char **p, const char *s) { free(*p); *p = s ? str_dup(s) : NULL; }
/* (GridCraft's quick layouts) 1 legend right; 2 legend top, data labels; 3 legend bottom;
   4 legend bottom, labels; 5 axis titles, no legend; 6 axis titles, legend right */
void chart_quick_layout(Chart *c, int n)
{
  static const u8 lg[7] = { LG_RIGHT, LG_RIGHT, LG_TOP, LG_BOTTOM, LG_BOTTOM, LG_NONE, LG_RIGHT };
  if (n < 1 || n > 6) n = 1;
  c->legend = lg[n];
  c->labels = n == 2 || n == 4;
  c->hgrid = !(n == 2 || n == 4);
  c->has_title = 1;
  if (n >= 5) {
    if (!c->xtitle) set_text(&c->xtitle, "Axis Title");
    if (!c->ytitle) set_text(&c->ytitle, "Axis Title");
  } else { set_text(&c->xtitle, NULL); set_text(&c->ytitle, NULL); }
}
void chart_colors(Chart *c, int palette)
{
  int i;
  c->palette = (u8)(palette >= 0 && palette <= 7 ? palette : 0);
  for (i = 0; i < c->ns; i++) c->s[i].color = 0;
}
void chart_style(Chart *c, int style) { c->style = (u8)(style >= 1 && style <= 8 ? style : 1); }

/* ------------------------------------------------------------------ colours */
static const u32 accents[6] = { 0x4472C4, 0xED7D31, 0xA5A5A5, 0xFFC000, 0x5B9BD5, 0x70AD47 };
/* rgb mixed towards white (t > 0) or black (t < 0) by |t| (0..1) */
static u32 tint(u32 rgb, double t)
{
  int ch[3], i;
  ch[0] = (int)(rgb >> 16) & 255; ch[1] = (int)(rgb >> 8) & 255; ch[2] = (int)rgb & 255;
  for (i = 0; i < 3; i++) ch[i] = t >= 0 ? (int)(ch[i] + (255 - ch[i]) * t + 0.5) : (int)(ch[i] * (1 + t) + 0.5);
  return ((u32)ch[0] << 16) | ((u32)ch[1] << 8) | (u32)ch[2];
}
static u32 palette_rgb(int palette, int k, int n)
{
  static const double cyc[4] = { 0, -0.25, 0.4, -0.5 };
  if (palette == 0) return tint(accents[k % 6], cyc[(k / 6) % 4]);
  {
    /* one colour's shades, darkest first, spread over the n entries */
    u32 base = palette == 1 ? accents[0] : accents[(palette - 2) % 6];
    double t = n > 1 ? -0.5 + 1.2 * k / (n - 1) : 0;
    return tint(base, t);
  }
}
u32 chart_series_rgb(const Chart *c, int k, int n)
{
  if (k >= 0 && k < c->ns && c->s[k].color) return c->s[k].color & 0xFFFFFF;
  return palette_rgb(c->palette, k, n);
}

/* ------------------------------------------------------------------ the values */
/* the cells a formula's references cover, in order, through fn; array constants' elements too */
typedef void (*CellCb)(void *arg, int sheet, i32 r, i32 c, const Value *v);
static int each_cell(Book *b, int home, const Formula *f, CellCb fn, void *arg)
{
  int i, n = 0;
  if (!f) return 0;
  for (i = 0; i < f->n && n < MAXPTS; i++) {
    const Tok *t = &f->t[i];
    if (t->op == T_REF || t->op == T_AREA) {
      int sh = t->a == SHEET_SELF ? home : t->a;
      i32 r1 = t->r1, c1 = t->c1, r2 = t->op == T_REF ? t->r1 : t->r2, c2 = t->op == T_REF ? t->c1 : t->c2, r, c;
      if (sh < 0 || sh >= b->nsh) continue;
      if (t->flags & RF_COLS) { r1 = 0; r2 = sheet_last_row(b->sh[sh]); }
      if (t->flags & RF_ROWS) { c1 = 0; c2 = sheet_last_col(b->sh[sh]); }
      for (r = r1; r <= r2 && n < MAXPTS; r++)
        for (c = c1; c <= c2 && n < MAXPTS; c++) { fn(arg, sh, r, c, cell_value(b, sh, r, c)); n++; }
    } else if (t->op == T_ARRAY && t->arr) {
      int k;
      for (k = 0; k < t->arr->rows * t->arr->cols && n < MAXPTS; k++) { fn(arg, -1, 0, 0, &t->arr->v[k]); n++; }
    } else if (t->op == T_NUM || t->op == T_STR) {
      Value v = t->op == T_NUM ? val_num(t->n) : val_str(t->s);
      fn(arg, -1, 0, 0, &v); n++;
      val_free(&v);
    }
  }
  return n;
}
typedef struct { Book *b; double *v; int n; char fmt[64]; } NumAcc;
static void num_cb(void *arg, int sheet, i32 r, i32 c, const Value *v)
{
  NumAcc *a = (NumAcc *)arg;
  double d = NAN;
  if (v->t == V_NUM) d = v->n;
  else if (v->t == V_BOOL) d = v->e;
  else if (v->t == V_STR) { double x; int isd; if (parse_number(v->s, &x, &isd)) d = x; }
  if (a->v) a->v[a->n] = d;
  if (!a->fmt[0] && sheet >= 0 && v->t == V_NUM) {
    const char *code = book_fmt_code(a->b, a->b->styles[sheet_cell_style(a->b->sh[sheet], r, c)].numfmt);
    strncpy(a->fmt, code, sizeof a->fmt - 1);
  }
  a->n++;
}
static double *numbers(Book *b, int home, const Formula *f, int *n, char *fmt)
{
  NumAcc a;
  memset(&a, 0, sizeof a);
  a.b = b;
  *n = 0;
  if (!f) return NULL;
  a.v = (double *)malloc(MAXPTS * sizeof(double));
  if (!a.v) return NULL;
  each_cell(b, home, f, num_cb, &a);
  *n = a.n;
  if (fmt && !fmt[0] && a.fmt[0]) strcpy(fmt, a.fmt);
  return a.v;
}
typedef struct { Book *b; char **s; int n; } TxtAcc;
static void txt_cb(void *arg, int sheet, i32 r, i32 c, const Value *v)
{
  TxtAcc *a = (TxtAcc *)arg;
  char t[256];
  if (sheet >= 0) { u32 col; book_shown_text(a->b, sheet, r, c, t, sizeof t, &col); }
  else if (v->t == V_STR) { strncpy(t, v->s, sizeof t - 1); t[sizeof t - 1] = 0; }
  else if (v->t == V_NUM) num_general(v->n, t, 11);
  else t[0] = 0;
  a->s[a->n++] = str_dup(t);
}
static char **texts(Book *b, int home, const Formula *f, int *n)
{
  TxtAcc a;
  a.b = b; a.n = 0;
  *n = 0;
  if (!f || !(a.s = (char **)calloc(MAXPTS, sizeof(char *)))) return NULL;
  each_cell(b, home, f, txt_cb, &a);
  *n = a.n;
  return a.s;
}
void cdata_free(CData *d)
{
  int i;
  for (i = 0; i < d->ncat; i++) free(d->cat[i]);
  free(d->cat);
  for (i = 0; i < d->ns; i++) { free(d->s[i].name); free(d->s[i].v); free(d->s[i].x); free(d->s[i].size); }
  free(d->s);
  memset(d, 0, sizeof *d);
}
int chart_resolve(Book *b, int sheet, const Chart *c, CData *d)
{
  int k, i;
  memset(d, 0, sizeof *d);
  if (!c->ns) return 1;
  if (!(d->s = (CDSeries *)calloc((size_t)c->ns, sizeof(CDSeries)))) return 0;
  d->ns = c->ns;
  for (k = 0; k < c->ns; k++) {
    const CSeries *s = &c->s[k];
    CDSeries *o = &d->s[k];
    o->v = numbers(b, sheet, s->val, &o->n, d->fmt);
    if (!o->v) { o->v = (double *)calloc(1, sizeof(double)); o->n = 0; }
    if (s->name) {
      int nn = 0;
      char **t = texts(b, sheet, s->name, &nn);
      Buf nb;
      buf_init(&nb);
      for (i = 0; i < nn; i++) { if (i && t[i][0]) buf_adds(&nb, " "); buf_adds(&nb, t[i]); free(t[i]); }
      free(t);
      o->name = nb.data ? buf_take(&nb) : str_dup("");
      buf_free(&nb);
    } else if (s->lit) o->name = str_dup(s->lit);
    else { char t[24]; sprintf(t, "Series%d", k + 1); o->name = str_dup(t); }
    if (is_xy(c->kind) && s->cat) {
      int nx = 0;
      double *x = numbers(b, sheet, s->cat, &nx, NULL);
      for (i = 0; x && i < nx; i++) if (isnan(x[i])) break;
      if (x && nx > 0 && i == nx) { o->x = x; if (nx < o->n) o->n = nx; }
      else free(x);
    }
    if (c->kind == CK_BUBBLE && s->size) {
      int nz = 0;
      o->size = numbers(b, sheet, s->size, &nz, NULL);
      if (o->size && nz < o->n) o->n = nz;
    }
    if (c->kind == CK_BUBBLE && !o->size && (o->size = (double *)malloc((size_t)(o->n + 1) * sizeof(double)))) memcpy(o->size, o->v, (size_t)o->n * sizeof(double));
    o->rgb = chart_series_rgb(c, k, c->ns);
  }
  /* the categories: the first series' labels, else 1, 2, 3 ... */
  for (k = 0; k < c->ns && !c->s[k].cat; k++) ;
  if (k < c->ns && !is_xy(c->kind)) d->cat = texts(b, sheet, c->s[k].cat, &d->ncat);
  {
    int most = 0;
    for (i = 0; i < d->ns; i++) if (d->s[i].n > most) most = d->s[i].n;
    if (d->ncat < most) {
      char **nc = (char **)realloc(d->cat, (size_t)most * sizeof(char *));
      if (nc) {
        d->cat = nc;
        for (i = d->ncat; i < most; i++) { char t[16]; sprintf(t, "%d", i + 1); d->cat[i] = str_dup(t); }
        d->ncat = most;
      }
    }
  }
  return 1;
}

/* ------------------------------------------------------------------ laying out: helpers */
#define TEXT_RGB 0x595959
#define DARK_RGB 0x262626
#define GRID_RGB 0xD9D9D9
#define AXIS_RGB 0xBFBFBF
#define UP_RGB 0x3E9B5F
#define DOWN_RGB 0xD64F45
#define FILLRGB(c) (FILL_ON | ((c) & 0xFFFFFF))

typedef struct {
  Book *b; int sheet;
  const Chart *c; CData *d;
  PrimList *l;
  TextWidthFn tw; void *ctx;
  int z;                           /* zoom, per cent */
  int fs, fst;                     /* label and title text sizes (pixels) */
  u32 text, back;                  /* text colour, chart background */
  int dark;                        /* a dark chart style */
} R;

static i32 tw(R *r, const char *s, int px, int bold)
{
  if (!s || !*s) return 0;
  if (r->tw) return r->tw(r->ctx, s, px, bold);
  return (i32)(strlen(s) * px * 55 / 100);
}
static Prim *rect(R *r, u16 el, double x, double y, double w, double h, u32 fill, u32 line)
{
  Prim *p = prim_add(r->l, PR_RECT, el);
  if (!p) return NULL;
  if (w < 0) { x += w; w = -w; }
  if (h < 0) { y += h; h = -h; }
  p->x = (i32)floor(x + 0.5); p->y = (i32)floor(y + 0.5);
  p->w = (i32)floor(x + w + 0.5) - p->x; p->h = (i32)floor(y + h + 0.5) - p->y;
  if (p->w < 1) p->w = 1;
  if (p->h < 1) p->h = 1;
  p->fill = fill; p->line = line; p->width = 1;
  return p;
}
static Prim *pts_prim(R *r, int t, u16 el, const double *xy, int n, u32 fill, u32 line, int width)
{
  Prim *p;
  int i;
  if (n < 2 || !(p = prim_add(r->l, t, el))) return NULL;
  if (!(p->pts = (i32 *)malloc((size_t)n * 2 * sizeof(i32)))) { r->l->n--; r->l->oom = 1; return NULL; }
  for (i = 0; i < 2 * n; i++) p->pts[i] = (i32)floor(xy[i] + 0.5);
  p->np = n; p->fill = fill; p->line = line; p->width = (u8)(width < 1 ? 1 : width);
  return p;
}
static Prim *line2(R *r, u16 el, double x0, double y0, double x1, double y1, u32 rgb, int width)
{
  double xy[4];
  xy[0] = x0; xy[1] = y0; xy[2] = x1; xy[3] = y1;
  return pts_prim(r, PR_LINE, el, xy, 2, 0, FILLRGB(rgb), width);
}
static Prim *ellipse(R *r, u16 el, double cx, double cy, double rx, double ry, u32 fill, u32 line)
{
  Prim *p = prim_add(r->l, PR_ELLIPSE, el);
  if (!p) return NULL;
  p->x = (i32)floor(cx - rx + 0.5); p->y = (i32)floor(cy - ry + 0.5);
  p->w = (i32)floor(2 * rx + 0.5); p->h = (i32)floor(2 * ry + 0.5);
  if (p->w < 1) p->w = 1;
  if (p->h < 1) p->h = 1;
  p->fill = fill; p->line = line; p->width = 1;
  return p;
}
static Prim *text(R *r, u16 el, double x, double y, const char *s, int px, u32 rgb, int bold, int align, int valign, int vert)
{
  Prim *p;
  if (!s || !*s || !(p = prim_add(r->l, PR_TEXT, el))) return NULL;
  if (!(p->text = str_dup(s))) { r->l->n--; return NULL; }
  p->x = (i32)floor(x + 0.5); p->y = (i32)floor(y + 0.5);
  p->size = (u8)(px > 255 ? 255 : px); p->bold = (u8)bold; p->align = (u8)align; p->valign = (u8)valign; p->vert = (u8)vert;
  p->line = FILLRGB(rgb);
  p->w = tw(r, s, px, bold); p->h = px * 6 / 5;
  return p;
}
/* a pie or doughnut slice from a0 to a1 (radians clockwise from 12 o'clock) */
static Prim *wedge(R *r, u16 el, double cx, double cy, double ro, double ri, double a0, double a1, u32 fill, u32 line)
{
  double xy[2 * 200];
  int n = 0, steps, i;
  steps = (int)((a1 - a0) / (M_PI / 45)) + 1;
  if (steps > 90) steps = 90;
  for (i = 0; i <= steps; i++) {
    double a = a0 + (a1 - a0) * i / steps;
    xy[n++] = cx + ro * sin(a); xy[n++] = cy - ro * cos(a);
  }
  if (ri > 0.5) {
    for (i = steps; i >= 0; i--) {
      double a = a0 + (a1 - a0) * i / steps;
      xy[n++] = cx + ri * sin(a); xy[n++] = cy - ri * cos(a);
    }
  } else { xy[n++] = cx; xy[n++] = cy; }
  return pts_prim(r, PR_POLY, el, xy, n / 2, fill, line, 1);
}
static void marker(R *r, u16 el, double x, double y, int k, u32 rgb, int size)
{
  /* Excel's automatic markers go round: diamond, square, triangle, x, circle... */
  double s = size, xy[8];
  switch (k % 4) {
  case 0: xy[0] = x; xy[1] = y - s; xy[2] = x + s; xy[3] = y; xy[4] = x; xy[5] = y + s; xy[6] = x - s; xy[7] = y; pts_prim(r, PR_POLY, el, xy, 4, FILLRGB(rgb), FILLRGB(rgb), 1); break;
  case 1: rect(r, el, x - s + 1, y - s + 1, 2 * s - 1, 2 * s - 1, FILLRGB(rgb), FILLRGB(rgb)); break;
  case 2: xy[0] = x; xy[1] = y - s; xy[2] = x + s; xy[3] = y + s - 1; xy[4] = x - s; xy[5] = y + s - 1; pts_prim(r, PR_POLY, el, xy, 3, FILLRGB(rgb), FILLRGB(rgb), 1); break;
  default: ellipse(r, el, x, y, s, s, FILLRGB(rgb), FILLRGB(rgb)); break;
  }
}
/* a value as the source cells format it (else General, with dp decimals when given) */
static void fmtnum(R *r, double v, int pct, char *out, size_t cap)
{
  Value x = val_num(v);
  u32 col;
  if (pct) { fmt_value(r->b, "0%", &x, out, cap, &col, 0); return; }
  if (r->d->fmt[0] && strcmp(r->d->fmt, "General")) { fmt_value(r->b, r->d->fmt, &x, out, cap, &col, 0); return; }
  if (fabs(v) < 1e-12) v = 0;
  num_general(v, out, 10);
}

void chart_nice_scale(double lo, double hi, int maxticks, double *min, double *max, double *major)
{
  double range, step, mag, f;
  if (!isfinite(lo) || !isfinite(hi)) { lo = 0; hi = 1; }
  if (hi < lo) { double t = lo; lo = hi; hi = t; }
  if (hi == lo) { if (hi == 0) hi = 1; else if (hi > 0) lo = 0; else hi = 0; }
  if (maxticks < 2) maxticks = 2;
  range = hi - lo;
  step = range / maxticks;
  mag = pow(10, floor(log10(step)));
  f = step / mag;
  step = (f <= 1 ? 1 : f <= 2 ? 2 : f <= 2.5 ? 2.5 : f <= 5 ? 5 : 10) * mag;
  *min = floor(lo / step + 1e-9) * step;
  *max = ceil(hi / step - 1e-9) * step;
  if (*max <= *min) *max = *min + step;
  *major = step;
}

/* ------------------------------------------------------------------ the frame: title, legend, plot area */
typedef struct { const char *label; u32 rgb; int kind; } LegEnt;   /* kind: 0 square, 1 line, 2 marker, 3 line and marker */
static int series_kind(const Chart *c, int k)
{
  int sk = c->s[k].kind;
  if (c->kind == CK_COMBO) {
    if (sk != CK_SAME && sk < CK_N) return sk;
    return k == 0 || c->ns == 1 ? CK_COL : CK_LINE;
  }
  return c->kind;
}
static int legend_kind(const Chart *c, int k)
{
  int sk = series_kind(c, k);
  if (sk == CK_LINEMARK || (sk == CK_RADAR && c->style != 3)) return 3;
  if (is_line(sk) || sk == CK_SCATTERLINE || sk == CK_RADAR || sk == CK_STOCK) return 1;
  if (sk == CK_SCATTER || sk == CK_BUBBLE) return 2;
  return 0;
}
/* the legend in the box x, y, w, h (vertical: a column) */
static void draw_legend(R *r, const LegEnt *e, int n, double x, double y, double w, double h, int vertical)
{
  int i, fs = r->fs, sw = fs * 7 / 10 + 1, gap = fs / 2 + 2, line = fs * 13 / 10;
  double cx = x, cy = y;
  rect(r, CE_LEGEND, x, y, w, h, r->c->efill[CE_LEGEND], r->c->eline[CE_LEGEND]);
  if (!vertical) {
    /* rows, centred */
    double rowstart = 0, roww = 0;
    int first = 0;
    cy = y + 2;
    for (i = 0; i <= n; i++) {
      double ew = i < n ? sw + 4 + tw(r, e[i].label, fs, 0) + gap : 0;
      if (i == n || (roww + ew > w && i > first)) {
        int j;
        cx = x + (w - roww + gap) / 2;
        for (j = first; j < i; j++) {
          double my = cy + line / 2.0;
          if (e[j].kind == 1 || e[j].kind == 3) { line2(r, CE_LEGEND, cx, my, cx + sw + 4, my, e[j].rgb, 2); if (e[j].kind == 3) marker(r, CE_LEGEND, cx + sw / 2.0 + 2, my, j, e[j].rgb, 2); cx += sw + 8; }
          else if (e[j].kind == 2) { marker(r, CE_LEGEND, cx + sw / 2.0, my, j, e[j].rgb, 3); cx += sw + 4; }
          else { rect(r, CE_LEGEND, cx, my - sw / 2.0, sw, sw, FILLRGB(e[j].rgb), 0); cx += sw + 4; }
          text(r, CE_LEGEND, cx, my, e[j].label, fs, r->text, 0, 0, 1, 0);
          cx += tw(r, e[j].label, fs, 0) + gap;
        }
        cy += line; first = i; roww = 0;
        if (cy + line > y + h + 1) break;
      }
      roww += ew;
      (void)rowstart;
    }
  } else {
    cy = y + (h - n * line) / 2.0;
    if (cy < y) cy = y;
    for (i = 0; i < n && cy + line <= y + h + 1; i++) {
      double my = cy + line / 2.0;
      cx = x + 2;
      if (e[i].kind == 1 || e[i].kind == 3) { line2(r, CE_LEGEND, cx, my, cx + sw + 4, my, e[i].rgb, 2); if (e[i].kind == 3) marker(r, CE_LEGEND, cx + sw / 2.0 + 2, my, i, e[i].rgb, 2); cx += sw + 8; }
      else if (e[i].kind == 2) { marker(r, CE_LEGEND, cx + sw / 2.0, my, i, e[i].rgb, 3); cx += sw + 4; }
      else { rect(r, CE_LEGEND, cx, my - sw / 2.0, sw, sw, FILLRGB(e[i].rgb), 0); cx += sw + 4; }
      text(r, CE_LEGEND, cx, my, e[i].label, fs, r->text, 0, 0, 1, 0);
      cy += line;
    }
  }
}
/* the legend's entries: the series, or the categories of a pie-like chart (point colours) */
static int legend_entries(R *r, LegEnt *e, int max)
{
  const Chart *c = r->c;
  CData *d = r->d;
  int n = 0, i;
  if (chart_is_pielike(c->kind) && c->kind != CK_FUNNEL) {
    int np = d->ns ? d->s[0].n : 0;
    for (i = 0; i < np && i < d->ncat && n < max; i++) { e[n].label = d->cat[i]; e[n].rgb = palette_rgb(c->palette, i, np); e[n].kind = 0; n++; }
    return n;
  }
  if (c->kind == CK_WATERFALL) {
    static const char *const wf[3] = { "Increase", "Decrease", "Total" };
    for (i = 0; i < 3; i++) { e[n].label = wf[i]; e[n].rgb = palette_rgb(c->palette, i, 3); e[n].kind = 0; n++; }
    return n;
  }
  for (i = 0; i < d->ns && n < max; i++) { e[n].label = d->s[i].name; e[n].rgb = d->s[i].rgb; e[n].kind = legend_kind(c, i); n++; }
  return n;
}

/* ------------------------------------------------------------------ cartesian charts */
typedef struct {
  double x, y, w, h;               /* the plot area */
  double min, max, major;          /* the value axis */
  double smin, smax, smajor;       /* the secondary one */
  int secondary;
  int horiz;                       /* bars: categories down the left, values across */
  int pct;                         /* 100% stacked: values are fractions */
  int ncat;
} Plot;
static double vpos(const Plot *p, double v, int sec)
{
  double lo = sec ? p->smin : p->min, hi = sec ? p->smax : p->max, f = hi > lo ? (v - lo) / (hi - lo) : 0;
  if (f < -0.05) f = -0.05;
  if (f > 1.05) f = 1.05;
  return p->horiz ? p->x + f * p->w : p->y + p->h - f * p->h;
}
static double cpos(const Plot *p, double i)   /* the middle of category slot i */
{
  return p->horiz ? p->y + (i + 0.5) * p->h / (p->ncat ? p->ncat : 1) : p->x + (i + 0.5) * p->w / (p->ncat ? p->ncat : 1);
}
static int stacked(int k) { return k == CK_COLSTACK || k == CK_COL100 || k == CK_BARSTACK || k == CK_BAR100 || k == CK_LINESTACK || k == CK_AREASTACK || k == CK_AREA100; }
static int pct100(int k) { return k == CK_COL100 || k == CK_BAR100 || k == CK_AREA100; }
static double val(const CData *d, int k, int i) { return k < d->ns && i < d->s[k].n ? d->s[k].v[i] : NAN; }
/* the extent of the values to show on the (secondary) value axis */
static void value_range(R *r, int sec, double *lo, double *hi)
{
  const Chart *c = r->c;
  CData *d = r->d;
  int k, i, ncat = d->ncat;
  int st = stacked(c->kind);
  *lo = INFINITY; *hi = -INFINITY;
  if (pct100(c->kind)) { *lo = 0; *hi = 1; for (i = 0; i < ncat; i++) for (k = 0; k < d->ns; k++) if (val(d, k, i) < 0) *lo = -1; return; }
  if (st) {
    for (i = 0; i < ncat; i++) {
      double pos = 0, neg = 0;
      for (k = 0; k < d->ns; k++) { double v = val(d, k, i); if (isnan(v)) continue; if (v >= 0 || c->kind == CK_LINESTACK) pos += v; else neg += v; }
      if (pos > *hi) *hi = pos;
      if (pos < *lo) *lo = pos;
      if (neg < *lo) *lo = neg;
    }
  } else if (c->kind == CK_WATERFALL) {
    double t = 0;
    for (i = 0; i < ncat; i++) { double v = val(d, 0, i); if (isnan(v)) continue; t += v; if (t > *hi) *hi = t; if (t < *lo) *lo = t; if (t - v > *hi) *hi = t - v; if (t - v < *lo) *lo = t - v; }
  } else {
    for (k = 0; k < d->ns; k++) {
      if ((c->kind == CK_COMBO && (c->s[k].secondary != 0) != (sec != 0))) continue;
      for (i = 0; i < d->s[k].n; i++) { double v = d->s[k].v[i]; if (isnan(v)) continue; if (v > *hi) *hi = v; if (v < *lo) *lo = v; }
    }
  }
  if (!isfinite(*lo)) { *lo = 0; *hi = 1; }
  /* bars and areas stand on zero */
  if (*lo > 0 && c->kind != CK_LINE && c->kind != CK_LINEMARK && c->kind != CK_STOCK && !(c->kind == CK_COMBO && sec)) *lo = 0;
  if (*hi < 0) *hi = 0;
}
static void axis_scale(R *r, const CAxis *ax, double lo, double hi, double len, double *min, double *max, double *major)
{
  int ticks = (int)(len / (r->fs * 2.6));
  if (ticks < 2) ticks = 2;
  if (ticks > 10) ticks = 10;
  if (ax->minset) lo = ax->min;
  if (ax->maxset) hi = ax->max;
  chart_nice_scale(lo, hi, ticks, min, max, major);
  if (ax->minset) *min = ax->min;
  if (ax->maxset) *max = ax->max;
  if (ax->majset && ax->major > 0 && (*max - *min) / ax->major < 200) *major = ax->major;
  if (*max <= *min) *max = *min + 1;
}
/* value axis labels' widest */
static i32 vlabels_width(R *r, double min, double max, double major, int pct)
{
  double v;
  i32 w = 0;
  char t[64];
  int n = 0;
  for (v = min; v <= max + major * 1e-6 && n < 60; v += major, n++) { fmtnum(r, v, pct, t, sizeof t); if (tw(r, t, r->fs, 0) > w) w = tw(r, t, r->fs, 0); }
  return w;
}
static void draw_value_axis(R *r, Plot *p, int sec)
{
  double min = sec ? p->smin : p->min, max = sec ? p->smax : p->max, major = sec ? p->smajor : p->major, v;
  char t[64];
  int n = 0;
  for (v = min; v <= max + major * 1e-6 && n < 60; v += major, n++) {
    double q = vpos(p, v, sec);
    fmtnum(r, fabs(v) < major * 1e-9 ? 0 : v, p->pct, t, sizeof t);
    if (p->horiz) {
      if (r->c->hgrid && !sec) line2(r, CE_NONE, q, p->y, q, p->y + p->h, GRID_RGB, 1);
      text(r, CE_YAXIS, q, p->y + p->h + 3, t, r->fs, r->text, 0, 1, 0, 0);
    } else {
      if (r->c->hgrid && !sec) line2(r, CE_NONE, p->x, q, p->x + p->w, q, GRID_RGB, 1);
      if (sec) text(r, CE_YAXIS, p->x + p->w + 4, q, t, r->fs, r->text, 0, 0, 1, 0);
      else text(r, CE_YAXIS, p->x - 4, q, t, r->fs, r->text, 0, 2, 1, 0);
    }
  }
}
static void draw_cat_axis(R *r, Plot *p, int between)
{
  int i, step = 1;
  double slot = (p->horiz ? p->h : p->w) / (p->ncat ? p->ncat : 1), base;
  CData *d = r->d;
  i32 widest = 0;
  for (i = 0; i < p->ncat; i++) { i32 w = tw(r, d->cat[i], r->fs, 0); if (w > widest) widest = w; }
  if (!p->horiz) { while (step < p->ncat && slot * step < widest + 6) step++; }
  else { while (step < p->ncat && slot * step < r->fs + 2) step++; }
  base = vpos(p, p->min < 0 && p->max > 0 ? 0 : p->min, 0);
  if (p->horiz) line2(r, CE_XAXIS, base, p->y, base, p->y + p->h, AXIS_RGB, 1);
  else line2(r, CE_XAXIS, p->x, base, p->x + p->w, base, AXIS_RGB, 1);
  for (i = 0; i < p->ncat; i += step) {
    double q = cpos(p, i);
    if (r->c->vgrid) {
      double e = (p->horiz ? p->y : p->x) + i * slot;
      if (p->horiz) line2(r, CE_NONE, p->x, e, p->x + p->w, e, GRID_RGB, 1);
      else line2(r, CE_NONE, e, p->y, e, p->y + p->h, GRID_RGB, 1);
    }
    if (p->horiz) text(r, CE_XAXIS, p->x - 4, q, d->cat[i], r->fs, r->text, 0, 2, 1, 0);
    else text(r, CE_XAXIS, q, p->y + p->h + 3, d->cat[i], r->fs, r->text, 0, 1, 0, 0);
  }
  (void)between;
}
static void label_at(R *r, double x, double y, double v, int pct, int valign, int align)
{
  char t[64];
  if (isnan(v)) return;
  fmtnum(r, v, pct, t, sizeof t);
  text(r, CE_NONE, x, y, t, r->fs, r->text, 0, align, valign, 0);
}
/* columns or bars of the series whose kind is a column or bar kind */
static void draw_bars(R *r, Plot *p, int kind)
{
  CData *d = r->d;
  const Chart *c = r->c;
  int k, i, nb = 0, st = stacked(kind), bi = 0;
  double slot = (p->horiz ? p->h : p->w) / (p->ncat ? p->ncat : 1), group = slot / 2.5 * 1.5, bw;
  int outline = c->style == 2 || c->style == 8;
  for (k = 0; k < d->ns; k++) if (is_col(series_kind(c, k)) || is_bar(series_kind(c, k)) || c->kind == CK_HISTOGRAM) nb++;
  if (!nb) return;
  if (c->kind == CK_HISTOGRAM) group = slot;
  bw = st ? group : group / nb;
  for (i = 0; i < p->ncat; i++) {
    double pos = 0, neg = 0, tot = 0;
    if (pct100(kind)) for (k = 0; k < d->ns; k++) { double v = val(d, k, i); if (!isnan(v)) tot += fabs(v); }
    bi = 0;
    for (k = 0; k < d->ns; k++) {
      int sk = series_kind(c, k), sec = c->kind == CK_COMBO && c->s[k].secondary;
      double v = val(d, k, i), a, b, s0;
      u32 rgb = d->s[k].rgb, lc;
      if (!(is_col(sk) || is_bar(sk) || c->kind == CK_HISTOGRAM)) continue;
      if (isnan(v)) { bi++; continue; }
      if (pct100(kind)) v = tot > 0 ? v / tot : 0;
      if (st) { if (v >= 0) { a = pos; pos += v; b = pos; } else { a = neg; neg += v; b = neg; } }
      else { a = 0; b = v; }
      if (!st && p->min > 0 && !sec) a = p->min;
      s0 = (p->horiz ? p->y : p->x) + i * slot + (slot - group) / 2 + (st ? 0 : bi * bw);
      lc = outline ? FILLRGB(tint(rgb, -0.5)) : c->kind == CK_HISTOGRAM ? FILLRGB(0xFFFFFF) : 0;
      if (c->s[k].line) lc = c->s[k].line;
      if (p->horiz) rect(r, (u16)(CE_SERIES + k), vpos(p, a, sec), s0, vpos(p, b, sec) - vpos(p, a, sec), bw, FILLRGB(rgb), lc);
      else rect(r, (u16)(CE_SERIES + k), s0, vpos(p, b, sec), bw, vpos(p, a, sec) - vpos(p, b, sec), FILLRGB(rgb), lc);
      if (c->labels) {
        double mid = s0 + bw / 2, end = vpos(p, st ? (a + b) / 2 : b, sec);
        if (p->horiz) label_at(r, st ? end : end + 3, mid, st || pct100(kind) ? val(d, k, i) : v, 0, 1, st ? 1 : 0);
        else label_at(r, mid, st ? end : end - 2, val(d, k, i), 0, st ? 1 : 2, 1);
      }
      bi++;
    }
  }
}
static void draw_lines(R *r, Plot *p, int area)
{
  CData *d = r->d;
  const Chart *c = r->c;
  int k, i;
  double *xy = (double *)malloc((size_t)(p->ncat * 2 + 4) * 2 * sizeof(double)), *cum = (double *)calloc((size_t)p->ncat + 1, sizeof(double)), *tot = (double *)calloc((size_t)p->ncat + 1, sizeof(double));
  int lw = c->style == 3 ? 3 : 2;
  if (!xy || !cum || !tot) { free(xy); free(cum); free(tot); return; }
  for (i = 0; i < p->ncat; i++) for (k = 0; k < d->ns; k++) { double v = val(d, k, i); if (!isnan(v)) tot[i] += fabs(v); }
  /* areas are drawn last series first when not stacked, so the front one is the first */
  for (k = area && !stacked(c->kind) ? d->ns - 1 : 0; area && !stacked(c->kind) ? k >= 0 : k < d->ns; k += area && !stacked(c->kind) ? -1 : 1) {
    int sk = series_kind(c, k), sec = c->kind == CK_COMBO && c->s[k].secondary, n = 0;
    int st = stacked(sk), mark = sk == CK_LINEMARK && !c->s[k].nomarker;
    u32 rgb = d->s[k].rgb;
    if (area ? !is_area(sk) : !is_line(sk)) continue;
    if (area) {
      /* the top edge, then back along the bottom (the series below, or zero) */
      for (i = 0; i < p->ncat; i++) {
        double v = val(d, k, i), b = st ? cum[i] : 0;
        if (isnan(v)) v = 0;
        if (pct100(c->kind)) v = tot[i] > 0 ? v / tot[i] : 0;
        xy[n++] = cpos(p, i); xy[n++] = vpos(p, b + v, sec);
      }
      for (i = p->ncat - 1; i >= 0; i--) { xy[n++] = cpos(p, i); xy[n++] = vpos(p, st ? cum[i] : (p->min > 0 ? p->min : 0), sec); }
      pts_prim(r, PR_POLY, (u16)(CE_SERIES + k), xy, n / 2, FILLRGB(rgb), c->s[k].line ? c->s[k].line : (c->style == 2 ? FILLRGB(tint(rgb, -0.4)) : 0), 1);
      for (i = 0; i < p->ncat; i++) {
        double v = val(d, k, i);
        if (isnan(v)) v = 0;
        if (pct100(c->kind)) v = tot[i] > 0 ? v / tot[i] : 0;
        if (c->labels) label_at(r, cpos(p, i), vpos(p, (st ? cum[i] : 0) + v / 2, sec), val(d, k, i), 0, 1, 1);
        if (st) cum[i] += v;
      }
      continue;
    }
    /* a line: runs of points, broken at empty cells */
    for (i = 0; i <= p->ncat; i++) {
      double v = i < p->ncat ? val(d, k, i) : NAN;
      if (!isnan(v)) {
        if (st) { cum[i] += v; v = cum[i]; }
        xy[n++] = cpos(p, i); xy[n++] = vpos(p, v, sec);
      }
      if (isnan(v) || i == p->ncat) {
        if (n >= 4) pts_prim(r, PR_LINE, (u16)(CE_SERIES + k), xy, n / 2, 0, FILLRGB(c->s[k].line ? c->s[k].line : rgb), lw);
        n = 0;
      }
    }
    for (i = 0; i < p->ncat; i++) {
      double v = val(d, k, i);
      if (isnan(v)) continue;
      if (st) v = cum[i];
      if (mark) marker(r, (u16)(CE_SERIES + k), cpos(p, i), vpos(p, v, sec), k, rgb, c->style == 3 ? 4 : 3);
      if (c->labels) label_at(r, cpos(p, i) + 4, vpos(p, v, sec) - 3, val(d, k, i), 0, 2, 0);
    }
  }
  free(xy); free(cum); free(tot);
}
static void draw_stock(R *r, Plot *p)
{
  CData *d = r->d;
  int i, o = d->ns >= 4;   /* open, high, low, close; or high, low, close */
  double slot = p->w / (p->ncat ? p->ncat : 1), bw = slot * 0.4;
  for (i = 0; i < p->ncat; i++) {
    double x = cpos(p, i), hi = val(d, o, i), lo = val(d, o + 1, i), cl = val(d, o + 2, i);
    if (!isnan(hi) && !isnan(lo)) line2(r, CE_SERIES + (u16)o, x, vpos(p, hi, 0), x, vpos(p, lo, 0), 0x404040, 1);
    if (o && !isnan(val(d, 0, i)) && !isnan(cl)) {
      double op = val(d, 0, i);
      rect(r, CE_SERIES, x - bw / 2, vpos(p, op > cl ? op : cl, 0), bw, fabs(vpos(p, op, 0) - vpos(p, cl, 0)) + 1, FILLRGB(cl >= op ? 0xFFFFFF : 0x404040), FILLRGB(0x404040));
    } else if (!isnan(cl)) line2(r, CE_SERIES + (u16)(o + 2), x, vpos(p, cl, 0), x + bw / 2 + 2, vpos(p, cl, 0), 0x404040, 2);
    if (r->c->labels && !isnan(cl)) label_at(r, x + bw / 2 + 3, vpos(p, cl, 0), cl, 0, 1, 0);
  }
}
static void draw_waterfall(R *r, Plot *p)
{
  CData *d = r->d;
  int i;
  double slot = p->w / (p->ncat ? p->ncat : 1), bw = slot * 0.6, t = 0, prev = NAN;
  for (i = 0; i < p->ncat; i++) {
    double v = val(d, 0, i), x0;
    u32 rgb;
    if (isnan(v)) continue;
    x0 = p->x + i * slot + (slot - bw) / 2;
    rgb = palette_rgb(r->c->palette, v >= 0 ? 0 : 1, 3);
    rect(r, CE_SERIES, x0, vpos(p, v >= 0 ? t + v : t, 0), bw, fabs(vpos(p, t, 0) - vpos(p, t + v, 0)), FILLRGB(rgb), 0);
    if (!isnan(prev)) line2(r, CE_NONE, x0 - (slot - bw), vpos(p, t, 0), x0, vpos(p, t, 0), 0x7F7F7F, 1);
    t += v; prev = t;
    label_at(r, x0 + bw / 2, vpos(p, v >= 0 ? t : t - v, 0) - 2, v, 0, 2, 1);
  }
}
/* histogram bins of the first series (Scott's rule, as Excel's automatic bins) */
static int histogram(R *r, char ***labels, double **counts)
{
  CData *d = r->d;
  double mn = INFINITY, mx = -INFINITY, sum = 0, ss = 0, bw, sd;
  int n = 0, i, nb;
  if (!d->ns) return 0;
  for (i = 0; i < d->s[0].n; i++) { double v = d->s[0].v[i]; if (isnan(v)) continue; n++; sum += v; if (v < mn) mn = v; if (v > mx) mx = v; }
  if (!n) return 0;
  for (i = 0; i < d->s[0].n; i++) { double v = d->s[0].v[i]; if (!isnan(v)) ss += (v - sum / n) * (v - sum / n); }
  sd = n > 1 ? sqrt(ss / (n - 1)) : 0;
  bw = sd > 0 ? 3.5 * sd / pow((double)n, 1.0 / 3.0) : 1;
  nb = bw > 0 ? (int)ceil((mx - mn) / bw) : 1;
  if (nb < 1) nb = 1;
  if (nb > 50) { nb = 50; bw = (mx - mn) / 50; }
  *labels = (char **)calloc((size_t)nb, sizeof(char *));
  *counts = (double *)calloc((size_t)nb, sizeof(double));
  if (!*labels || !*counts) { free(*labels); free(*counts); return 0; }
  for (i = 0; i < d->s[0].n; i++) {
    double v = d->s[0].v[i];
    int b;
    if (isnan(v)) continue;
    b = bw > 0 ? (int)ceil((v - mn) / bw) - 1 : 0;
    if (b < 0) b = 0;
    if (b >= nb) b = nb - 1;
    (*counts)[b]++;
  }
  for (i = 0; i < nb; i++) {
    char a[32], b[32], t[80];
    num_general(mn + i * bw, a, 6); num_general(mn + (i + 1) * bw, b, 6);
    snprintf(t, sizeof t, "%c%s, %s]", i ? '(' : '[', a, b);
    (*labels)[i] = str_dup(t);
  }
  return nb;
}
static double quartile_exc(const double *s, int n, double q)
{
  double h = (n + 1) * q;
  int lo = (int)floor(h);
  if (n <= 0) return NAN;
  if (lo < 1) return s[0];
  if (lo >= n) return s[n - 1];
  return s[lo - 1] + (h - lo) * (s[lo] - s[lo - 1]);
}
static int dcmp_(const void *a, const void *b) { double x = *(const double *)a, y = *(const double *)b; return x < y ? -1 : x > y; }
static void draw_box(R *r, Plot *p)
{
  CData *d = r->d;
  int k;
  double slot = p->w / (p->ncat ? p->ncat : 1), bw = slot * 0.5;
  for (k = 0; k < d->ns; k++) {
    double *s = (double *)malloc((size_t)(d->s[k].n + 1) * sizeof(double)), q1, q2, q3, iqr, lo = INFINITY, hi = -INFINITY, mean = 0, x;
    int n = 0, i;
    u32 rgb = d->s[k].rgb;
    if (!s) continue;
    for (i = 0; i < d->s[k].n; i++) if (!isnan(d->s[k].v[i])) { s[n++] = d->s[k].v[i]; mean += d->s[k].v[i]; }
    if (!n) { free(s); continue; }
    mean /= n;
    qsort(s, (size_t)n, sizeof(double), dcmp_);
    q1 = quartile_exc(s, n, 0.25); q2 = quartile_exc(s, n, 0.5); q3 = quartile_exc(s, n, 0.75); iqr = q3 - q1;
    for (i = 0; i < n; i++) if (s[i] >= q1 - 1.5 * iqr && s[i] <= q3 + 1.5 * iqr) { if (s[i] < lo) lo = s[i]; if (s[i] > hi) hi = s[i]; }
    x = cpos(p, k);
    line2(r, (u16)(CE_SERIES + k), x, vpos(p, hi, 0), x, vpos(p, q3, 0), 0x404040, 1);
    line2(r, (u16)(CE_SERIES + k), x, vpos(p, lo, 0), x, vpos(p, q1, 0), 0x404040, 1);
    line2(r, (u16)(CE_SERIES + k), x - bw / 4, vpos(p, hi, 0), x + bw / 4, vpos(p, hi, 0), 0x404040, 1);
    line2(r, (u16)(CE_SERIES + k), x - bw / 4, vpos(p, lo, 0), x + bw / 4, vpos(p, lo, 0), 0x404040, 1);
    rect(r, (u16)(CE_SERIES + k), x - bw / 2, vpos(p, q3, 0), bw, vpos(p, q1, 0) - vpos(p, q3, 0), FILLRGB(rgb), FILLRGB(tint(rgb, -0.4)));
    line2(r, (u16)(CE_SERIES + k), x - bw / 2, vpos(p, q2, 0), x + bw / 2, vpos(p, q2, 0), 0xFFFFFF, 1);
    /* the mean as a cross, the outliers as rings */
    line2(r, (u16)(CE_SERIES + k), x - 3, vpos(p, mean, 0) - 3, x + 3, vpos(p, mean, 0) + 3, 0xFFFFFF, 1);
    line2(r, (u16)(CE_SERIES + k), x - 3, vpos(p, mean, 0) + 3, x + 3, vpos(p, mean, 0) - 3, 0xFFFFFF, 1);
    for (i = 0; i < n; i++) if (s[i] < lo || s[i] > hi) ellipse(r, (u16)(CE_SERIES + k), x, vpos(p, s[i], 0), 3, 3, 0, FILLRGB(rgb));
    if (r->c->labels) label_at(r, x + bw / 2 + 3, vpos(p, q2, 0), q2, 0, 1, 0);
    free(s);
  }
}
/* the box left for the plot after axis labels and titles; the cartesian chart drawn there */
static void draw_cartesian(R *r, double x, double y, double w, double h)
{
  const Chart *c = r->c;
  CData *d = r->d;
  Plot p;
  double lo, hi, slo = 0, shi = 1;
  i32 lw, cw = 0, sw = 0;
  int k, i, horiz = is_bar(c->kind), xytitle = 0;
  char **hl = NULL;
  double *hc = NULL;
  int nh = 0;
  char **savecat = NULL;
  int savencat = 0;
  memset(&p, 0, sizeof p);
  p.horiz = horiz;
  p.pct = pct100(c->kind);
  if (c->kind == CK_HISTOGRAM) {
    /* the bins become the categories of one series of counts */
    nh = histogram(r, &hl, &hc);
    savecat = d->cat; savencat = d->ncat;
    d->cat = hl; d->ncat = nh;
    if (d->ns) { free(d->s[0].v); d->s[0].v = hc; d->s[0].n = nh; hc = NULL; }
  }
  if (c->kind == CK_BOXWHISKER) {
    /* one box a series, named below it */
    savecat = d->cat; savencat = d->ncat;
    d->cat = (char **)calloc((size_t)d->ns + 1, sizeof(char *));
    for (k = 0; d->cat && k < d->ns; k++) d->cat[k] = d->s[k].name;
    d->ncat = d->cat ? d->ns : 0;
  }
  p.ncat = d->ncat;
  value_range(r, 0, &lo, &hi);
  if (c->kind == CK_BOXWHISKER) { lo = INFINITY; hi = -INFINITY; for (k = 0; k < d->ns; k++) for (i = 0; i < d->s[k].n; i++) if (!isnan(d->s[k].v[i])) { if (d->s[k].v[i] < lo) lo = d->s[k].v[i]; if (d->s[k].v[i] > hi) hi = d->s[k].v[i]; } if (!isfinite(lo)) { lo = 0; hi = 1; } }
  for (k = 0; k < c->ns; k++) if (c->kind == CK_COMBO && c->s[k].secondary) p.secondary = 1;
  if (p.secondary) value_range(r, 1, &slo, &shi);
  /* room for the axis titles and labels */
  if (c->ytitle && *c->ytitle) { x += r->fs * 3 / 2 + 2; w -= r->fs * 3 / 2 + 2; xytitle = 1; }
  if (c->xtitle && *c->xtitle) h -= r->fs * 3 / 2 + 2;
  axis_scale(r, &c->va, lo, hi, horiz ? w : h, &p.min, &p.max, &p.major);
  if (p.secondary) { CAxis none; memset(&none, 0, sizeof none); axis_scale(r, &none, slo, shi, h, &p.smin, &p.smax, &p.smajor); }
  lw = vlabels_width(r, p.min, p.max, p.major, p.pct);
  if (p.secondary) sw = vlabels_width(r, p.smin, p.smax, p.smajor, 0) + 6;
  if (horiz) { for (i = 0; i < d->ncat; i++) { i32 t = tw(r, d->cat[i], r->fs, 0); if (t > cw) cw = t; } if (cw > w / 3) cw = (i32)(w / 3); }
  p.x = x + (horiz ? cw : lw) + 6;
  p.y = y + r->fs / 2 + 2;
  p.w = w - (p.x - x) - sw - 8;
  p.h = h - (p.y - y) - r->fs * 6 / 5 - 4;
  if (horiz) {
    double t;
    (void)t;
  }
  if (p.w < 10) p.w = 10;
  if (p.h < 10) p.h = 10;
  rect(r, CE_PLOT, p.x, p.y, p.w, p.h, c->efill[CE_PLOT] ? c->efill[CE_PLOT] : (c->style == 4 ? FILLRGB(0xF2F2F2) : 0), c->eline[CE_PLOT]);
  draw_value_axis(r, &p, 0);
  if (p.secondary) draw_value_axis(r, &p, 1);
  draw_cat_axis(r, &p, 1);
  if (c->kind == CK_STOCK) draw_stock(r, &p);
  else if (c->kind == CK_WATERFALL) draw_waterfall(r, &p);
  else if (c->kind == CK_BOXWHISKER) draw_box(r, &p);
  else {
    int kind = c->kind == CK_COMBO || c->kind == CK_HISTOGRAM ? CK_COL : c->kind;
    draw_lines(r, &p, 1);
    draw_bars(r, &p, kind);
    draw_lines(r, &p, 0);
  }
  if (xytitle) text(r, CE_YAXIS, x - r->fs * 3 / 2, p.y + p.h / 2, c->ytitle, r->fs, r->text, 1, 1, 1, 1);
  if (c->xtitle && *c->xtitle) text(r, CE_XAXIS, p.x + p.w / 2, y + h + 2, c->xtitle, r->fs, r->text, 1, 1, 0, 0);
  if (savecat) {
    if (c->kind == CK_HISTOGRAM) { for (i = 0; i < d->ncat; i++) free(d->cat[i]); }
    free(d->cat);
    d->cat = savecat; d->ncat = savencat;
  }
  free(hc);
}
/* scatter and bubble: two value axes */
static void draw_xy(R *r, double x, double y, double w, double h)
{
  const Chart *c = r->c;
  CData *d = r->d;
  Plot p, q;
  double xlo = INFINITY, xhi = -INFINITY, ylo = INFINITY, yhi = -INFINITY, smax = 0, xmin, xmax, xmaj;
  int k, i, lw = c->style == 3 ? 3 : 2;
  i32 lwid;
  double *xy;
  memset(&p, 0, sizeof p);
  for (k = 0; k < d->ns; k++)
    for (i = 0; i < d->s[k].n; i++) {
      double xv = d->s[k].x ? d->s[k].x[i] : i + 1, yv = d->s[k].v[i];
      if (isnan(yv) || isnan(xv)) continue;
      if (xv < xlo) xlo = xv;
      if (xv > xhi) xhi = xv;
      if (yv < ylo) ylo = yv;
      if (yv > yhi) yhi = yv;
      if (d->s[k].size && d->s[k].size[i] > smax) smax = d->s[k].size[i];
    }
  if (!isfinite(xlo)) { xlo = 0; xhi = 1; ylo = 0; yhi = 1; }
  if (xlo > 0 && xhi - xlo < xlo * 0.5 && 0) xlo = 0;
  if (ylo > 0) ylo = 0;
  if (xlo > 0) xlo = 0;
  if (c->ytitle && *c->ytitle) { text(r, CE_YAXIS, x + r->fs * 3 / 4, y + h / 2, c->ytitle, r->fs, r->text, 1, 1, 1, 1); x += r->fs * 3 / 2 + 2; w -= r->fs * 3 / 2 + 2; }
  if (c->xtitle && *c->xtitle) { h -= r->fs * 3 / 2 + 2; text(r, CE_XAXIS, x + w / 2, y + h + 2, c->xtitle, r->fs, r->text, 1, 1, 0, 0); }
  axis_scale(r, &c->va, ylo, yhi, h, &p.min, &p.max, &p.major);
  axis_scale(r, &c->ha, xlo, xhi, w, &xmin, &xmax, &xmaj);
  lwid = vlabels_width(r, p.min, p.max, p.major, 0);
  p.x = x + lwid + 6; p.y = y + r->fs / 2 + 2;
  p.w = w - (p.x - x) - 10; p.h = h - (p.y - y) - r->fs * 6 / 5 - 4;
  if (p.w < 10) p.w = 10;
  if (p.h < 10) p.h = 10;
  rect(r, CE_PLOT, p.x, p.y, p.w, p.h, c->efill[CE_PLOT] ? c->efill[CE_PLOT] : (c->style == 4 ? FILLRGB(0xF2F2F2) : 0), c->eline[CE_PLOT]);
  draw_value_axis(r, &p, 0);
  /* the X axis: a value axis across */
  q = p; q.horiz = 1; q.min = xmin; q.max = xmax; q.major = xmaj;
  {
    double v;
    char t[64];
    int n = 0;
    for (v = xmin; v <= xmax + xmaj * 1e-6 && n < 60; v += xmaj, n++) {
      double px = vpos(&q, v, 0);
      if (c->vgrid) line2(r, CE_NONE, px, p.y, px, p.y + p.h, GRID_RGB, 1);
      num_general(fabs(v) < xmaj * 1e-9 ? 0 : v, t, 10);
      text(r, CE_XAXIS, px, p.y + p.h + 3, t, r->fs, r->text, 0, 1, 0, 0);
    }
    line2(r, CE_XAXIS, p.x, vpos(&p, p.min < 0 && p.max > 0 ? 0 : p.min, 0), p.x + p.w, vpos(&p, p.min < 0 && p.max > 0 ? 0 : p.min, 0), AXIS_RGB, 1);
  }
  xy = (double *)malloc(MAXPTS * 2 * sizeof(double));
  for (k = 0; k < d->ns && xy; k++) {
    u32 rgb = d->s[k].rgb;
    int n = 0;
    for (i = 0; i < d->s[k].n; i++) {
      double xv = d->s[k].x ? d->s[k].x[i] : i + 1, yv = d->s[k].v[i];
      if (isnan(yv) || isnan(xv)) continue;
      xy[n++] = vpos(&q, xv, 0); xy[n++] = vpos(&p, yv, 0);
    }
    if (c->kind == CK_SCATTERLINE && n >= 4) pts_prim(r, PR_LINE, (u16)(CE_SERIES + k), xy, n / 2, 0, FILLRGB(c->s[k].line ? c->s[k].line : rgb), lw);
    for (i = 0; i < n / 2; i++) {
      if (c->kind == CK_BUBBLE) {
        double sz = d->s[k].size ? d->s[k].size[i] : 1, rr = smax > 0 && sz > 0 ? sqrt(sz / smax) * (p.w < p.h ? p.w : p.h) / 8 : 2;
        ellipse(r, (u16)(CE_SERIES + k), xy[2 * i], xy[2 * i + 1], rr, rr, FILLRGB(rgb), FILLRGB(tint(rgb, -0.3)));
      } else if (!c->s[k].nomarker) marker(r, (u16)(CE_SERIES + k), xy[2 * i], xy[2 * i + 1], k, rgb, c->style == 3 ? 4 : 3);
    }
    if (c->labels) {
      int j = 0;
      for (i = 0; i < d->s[k].n; i++) {
        double yv = d->s[k].v[i], xv = d->s[k].x ? d->s[k].x[i] : i + 1;
        if (isnan(yv) || isnan(xv)) continue;
        label_at(r, xy[2 * j] + 4, xy[2 * j + 1] - 3, yv, 0, 2, 0);
        j++;
      }
    }
  }
  free(xy);
}
static void draw_radar(R *r, double x, double y, double w, double h)
{
  const Chart *c = r->c;
  CData *d = r->d;
  int n = d->ncat, k, i, t;
  double cx = x + w / 2, cy = y + h / 2 + r->fs / 2, rad = (w < h ? w : h) / 2 - r->fs * 2, min, max, major, lo = 0, hi = -INFINITY;
  double *xy;
  if (n < 1 || rad < 8) return;
  for (k = 0; k < d->ns; k++) for (i = 0; i < d->s[k].n; i++) if (!isnan(d->s[k].v[i])) { if (d->s[k].v[i] > hi) hi = d->s[k].v[i]; if (d->s[k].v[i] < lo) lo = d->s[k].v[i]; }
  if (!isfinite(hi)) hi = 1;
  axis_scale(r, &c->va, lo, hi, rad, &min, &max, &major);
  xy = (double *)malloc((size_t)(n + 1) * 2 * sizeof(double));
  if (!xy) return;
  /* the web: a polygon at each tick, a spoke to each category */
  for (t = 0; t <= 20 && min + t * major <= max + major * 1e-6; t++) {
    double rr = (t * major) / (max - min) * rad;
    char lab[64];
    for (i = 0; i < n; i++) { double a = 2 * M_PI * i / n; xy[2 * i] = cx + rr * sin(a); xy[2 * i + 1] = cy - rr * cos(a); }
    xy[2 * n] = xy[0]; xy[2 * n + 1] = xy[1];
    if (c->hgrid || t == 0) pts_prim(r, PR_LINE, CE_NONE, xy, n + 1, 0, FILLRGB(GRID_RGB), 1);
    fmtnum(r, min + t * major, 0, lab, sizeof lab);
    text(r, CE_YAXIS, cx - 3, cy - rr, lab, r->fs * 9 / 10, r->text, 0, 2, 1, 0);
  }
  for (i = 0; i < n; i++) {
    double a = 2 * M_PI * i / n;
    line2(r, CE_NONE, cx, cy, cx + rad * sin(a), cy - rad * cos(a), GRID_RGB, 1);
    text(r, CE_XAXIS, cx + (rad + 4) * sin(a), cy - (rad + 4) * cos(a), d->cat[i], r->fs, r->text, 0, sin(a) > 0.2 ? 0 : sin(a) < -0.2 ? 2 : 1, cos(a) > 0.2 ? 2 : cos(a) < -0.2 ? 0 : 1, 0);
  }
  rect(r, CE_PLOT, cx - rad, cy - rad, 2 * rad, 2 * rad, 0, 0);
  for (k = 0; k < d->ns; k++) {
    int m = 0;
    for (i = 0; i < n; i++) {
      double v = val(d, k, i), a = 2 * M_PI * i / n, rr;
      if (isnan(v)) v = min;
      rr = (v - min) / (max - min) * rad;
      xy[m++] = cx + rr * sin(a); xy[m++] = cy - rr * cos(a);
    }
    xy[m++] = xy[0]; xy[m++] = xy[1];
    pts_prim(r, PR_LINE, (u16)(CE_SERIES + k), xy, m / 2, 0, FILLRGB(c->s[k].line ? c->s[k].line : d->s[k].rgb), c->style == 3 ? 3 : 2);
    for (i = 0; i < n; i++) {
      if (c->style != 3) marker(r, (u16)(CE_SERIES + k), xy[2 * i], xy[2 * i + 1], k, d->s[k].rgb, 3);
      if (c->labels) label_at(r, xy[2 * i] + 4, xy[2 * i + 1] - 3, val(d, k, i), 0, 2, 0);
    }
  }
  free(xy);
}
/* ------------------------------------------------------------------ one-series charts */
static void draw_pie(R *r, double x, double y, double w, double h, int doughnut)
{
  const Chart *c = r->c;
  CData *d = r->d;
  double tot = 0, a = 0, cx = x + w / 2, cy = y + h / 2, rad = (w < h ? w : h) / 2 - 4;
  int i, n = d->ns ? d->s[0].n : 0;
  u32 sep = c->style == 5 ? FILLRGB(0x404040) : FILLRGB(0xFFFFFF);
  if (rad < 4 || !n) return;
  for (i = 0; i < n; i++) if (!isnan(d->s[0].v[i]) && d->s[0].v[i] > 0) tot += d->s[0].v[i];
  rect(r, CE_PLOT, cx - rad, cy - rad, 2 * rad, 2 * rad, 0, 0);
  if (tot <= 0) { ellipse(r, CE_PLOT, cx, cy, rad, rad, 0, FILLRGB(GRID_RGB)); return; }
  for (i = 0; i < n; i++) {
    double v = d->s[0].v[i], a1;
    u32 rgb = c->s[0].color && c->ns == 1 && 0 ? c->s[0].color : palette_rgb(c->palette, i, n);
    if (isnan(v) || v <= 0) continue;
    a1 = a + 2 * M_PI * v / tot;
    wedge(r, (u16)CE_SERIES, cx, cy, rad, doughnut ? rad * 0.5 : 0, a, a1, FILLRGB(rgb), sep);
    if (c->labels) {
      double m = (a + a1) / 2, lr = doughnut ? rad * 0.75 : rad * 0.62;
      char t[64];
      fmtnum(r, v, 0, t, sizeof t);
      text(r, CE_NONE, cx + lr * sin(m), cy - lr * cos(m), t, r->fs, 0xFFFFFF, 1, 1, 1, 0);
    }
    a = a1;
  }
}
static void draw_funnel(R *r, double x, double y, double w, double h)
{
  CData *d = r->d;
  int i, n = d->ns ? d->s[0].n : 0;
  double mx = 0, lw = 0, slot, bx, bw;
  if (!n) return;
  for (i = 0; i < n; i++) { double v = d->s[0].v[i]; if (!isnan(v) && fabs(v) > mx) mx = fabs(v); if (i < d->ncat && tw(r, d->cat[i], r->fs, 0) > lw) lw = tw(r, d->cat[i], r->fs, 0); }
  if (lw > w / 3) lw = w / 3;
  slot = h / n; bx = x + lw + 8; bw = w - lw - 12;
  rect(r, CE_PLOT, bx, y, bw, h, 0, 0);
  for (i = 0; i < n; i++) {
    double v = d->s[0].v[i], len, cy = y + i * slot;
    char t[64];
    if (i < d->ncat) text(r, CE_XAXIS, bx - 4, cy + slot / 2, d->cat[i], r->fs, r->text, 0, 2, 1, 0);
    if (isnan(v) || mx <= 0) continue;
    len = fabs(v) / mx * bw;
    rect(r, (u16)CE_SERIES, bx + (bw - len) / 2, cy + slot * 0.1, len, slot * 0.8, FILLRGB(d->s[0].rgb), 0);
    fmtnum(r, v, 0, t, sizeof t);
    text(r, CE_NONE, bx + bw / 2, cy + slot / 2, t, r->fs, 0xFFFFFF, 0, 1, 1, 0);
  }
}
/* squarified treemap of positive values into x, y, w, h */
typedef struct { double v; int i; } TItem;
static int titem_cmp(const void *a, const void *b) { double x = ((const TItem *)a)->v, y = ((const TItem *)b)->v; return x > y ? -1 : x < y; }
static double worst(const TItem *it, int n, double len, double scale)
{
  double s = 0, mn = INFINITY, mx = 0, rr;
  int i;
  for (i = 0; i < n; i++) { double a = it[i].v * scale; s += a; if (a < mn) mn = a; if (a > mx) mx = a; }
  if (s <= 0 || len <= 0) return INFINITY;
  rr = (len * len * mx) / (s * s);
  { double r2 = (s * s) / (len * len * mn); return rr > r2 ? rr : r2; }
}
static void draw_treemap(R *r, double x, double y, double w, double h)
{
  CData *d = r->d;
  int n = d->ns ? d->s[0].n : 0, m = 0, i, start;
  TItem *it;
  double tot = 0, scale;
  if (!n || !(it = (TItem *)malloc((size_t)n * sizeof(TItem)))) return;
  for (i = 0; i < n; i++) { double v = d->s[0].v[i]; if (!isnan(v) && v > 0) { it[m].v = v; it[m].i = i; m++; tot += v; } }
  qsort(it, (size_t)m, sizeof(TItem), titem_cmp);
  rect(r, CE_PLOT, x, y, w, h, 0, 0);
  if (tot <= 0) { free(it); return; }
  scale = w * h / tot;
  start = 0;
  while (start < m) {
    double len = w < h ? w : h, rowsum = 0, thick, off;
    int end = start + 1, k;
    while (end < m && worst(it + start, end + 1 - start, len, scale) <= worst(it + start, end - start, len, scale)) end++;
    for (k = start; k < end; k++) rowsum += it[k].v * scale;
    thick = rowsum / len;
    off = 0;
    for (k = start; k < end; k++) {
      double a = it[k].v * scale, l = a / thick, rx, ry, rw, rh;
      u32 rgb = palette_rgb(r->c->palette, it[k].i, n);
      if (w >= h) { rx = x; ry = y + off; rw = thick; rh = l; } else { rx = x + off; ry = y; rw = l; rh = thick; }
      rect(r, (u16)CE_SERIES, rx, ry, rw, rh, FILLRGB(rgb), FILLRGB(0xFFFFFF));
      if (it[k].i < d->ncat && rw > 20 && rh > r->fs + 4) {
        text(r, CE_NONE, rx + 3, ry + 2, d->cat[it[k].i], r->fs, 0xFFFFFF, 0, 0, 0, 0);
        if (r->c->labels && rh > 2 * r->fs + 6) { char t[64]; fmtnum(r, it[k].v, 0, t, sizeof t); text(r, CE_NONE, rx + 3, ry + 3 + r->fs * 6 / 5, t, r->fs, 0xFFFFFF, 0, 0, 0, 0); }
      }
      off += l;
    }
    if (w >= h) { x += thick; w -= thick; } else { y += thick; h -= thick; }
    start = end;
  }
  free(it);
}

/* ------------------------------------------------------------------ the whole chart */
int chart_render(Book *b, int sheet, const Chart *c, int w, int h, int zoom, TextWidthFn twf, void *ctx, PrimList *out)
{
  R r;
  CData d;
  LegEnt ents[64];
  int ne = 0;
  double x0 = 6, y0 = 6, x1 = w - 6, y1 = h - 6;
  const char *title = NULL;
  char tbuf[256];
  memset(&r, 0, sizeof r);
  r.b = b; r.sheet = sheet; r.c = c; r.d = &d; r.l = out; r.tw = twf; r.ctx = ctx;
  r.z = zoom < 10 ? 100 : zoom;
  r.fs = 13 * r.z / 100; r.fst = 18 * r.z / 100;
  if (r.fs < 6) r.fs = 6;
  if (r.fst < 7) r.fst = 7;
  r.dark = c->style == 5;
  r.text = r.dark ? 0xF2F2F2 : TEXT_RGB;
  if (!chart_resolve(b, sheet, c, &d)) return 0;
  /* the chart area */
  rect(&r, CE_CHART, 0, 0, w, h, c->efill[CE_CHART] ? c->efill[CE_CHART] : FILLRGB(r.dark ? 0x404040 : 0xFFFFFF), c->eline[CE_CHART] ? c->eline[CE_CHART] : FILLRGB(GRID_RGB));
  /* the title */
  if (c->has_title) {
    if (c->title && *c->title) title = c->title;
    else if (d.ns == 1 && d.s[0].name && *d.s[0].name && c->s[0].name) title = d.s[0].name;
    else title = "Chart Title";
    snprintf(tbuf, sizeof tbuf, "%s", title);
    {
      Prim *tp;
      if (c->efill[CE_TITLE] || c->eline[CE_TITLE]) {
        i32 tw0 = tw(&r, tbuf, r.fst, 0);
        rect(&r, CE_TITLE, w / 2.0 - tw0 / 2.0 - 4, y0, tw0 + 8, r.fst * 6 / 5 + 2, c->efill[CE_TITLE], c->eline[CE_TITLE]);
      }
      tp = text(&r, CE_TITLE, w / 2.0, y0 + 1, tbuf, r.fst, r.dark ? 0xFFFFFF : DARK_RGB, 0, 1, 0, 0);
      (void)tp;
    }
    y0 += r.fst * 6 / 5 + 6;
  }
  /* the legend */
  if (c->legend != LG_NONE) ne = legend_entries(&r, ents, 64);
  if (ne) {
    int line = r.fs * 13 / 10, i;
    if (c->legend == LG_LEFT || c->legend == LG_RIGHT) {
      i32 lwid = 0;
      for (i = 0; i < ne; i++) { i32 t = tw(&r, ents[i].label, r.fs, 0) + r.fs + 12; if (t > lwid) lwid = t; }
      if (lwid > (x1 - x0) / 3) lwid = (i32)((x1 - x0) / 3);
      if (c->legend == LG_RIGHT) { draw_legend(&r, ents, ne, x1 - lwid, y0, lwid, y1 - y0, 1); x1 -= lwid + 4; }
      else { draw_legend(&r, ents, ne, x0, y0, lwid, y1 - y0, 1); x0 += lwid + 4; }
    } else {
      /* rows: as many as the entries need */
      double roww = 0, avail = x1 - x0;
      int rows = 1;
      for (i = 0; i < ne; i++) {
        double ew = r.fs * 7 / 10 + 1 + 8 + tw(&r, ents[i].label, r.fs, 0) + r.fs / 2 + 2;
        if (roww + ew > avail && roww > 0) { rows++; roww = 0; }
        roww += ew;
      }
      if (rows > 3) rows = 3;
      if (c->legend == LG_BOTTOM) { draw_legend(&r, ents, ne, x0, y1 - rows * line - 2, x1 - x0, rows * line + 2, 0); y1 -= rows * line + 6; }
      else { draw_legend(&r, ents, ne, x0, y0, x1 - x0, rows * line + 2, 0); y0 += rows * line + 6; }
    }
  }
  if (x1 - x0 > 8 && y1 - y0 > 8) {
    if (c->kind == CK_PIE || c->kind == CK_DOUGHNUT) draw_pie(&r, x0, y0, x1 - x0, y1 - y0, c->kind == CK_DOUGHNUT);
    else if (c->kind == CK_SUNBURST) draw_pie(&r, x0, y0, x1 - x0, y1 - y0, 1);
    else if (c->kind == CK_FUNNEL) draw_funnel(&r, x0, y0, x1 - x0, y1 - y0);
    else if (c->kind == CK_TREEMAP) draw_treemap(&r, x0, y0, x1 - x0, y1 - y0);
    else if (c->kind == CK_RADAR) draw_radar(&r, x0, y0, x1 - x0, y1 - y0);
    else if (is_xy(c->kind)) draw_xy(&r, x0, y0, x1 - x0, y1 - y0);
    else draw_cartesian(&r, x0, y0, x1 - x0, y1 - y0);
  }
  cdata_free(&d);
  return !out->oom;
}
