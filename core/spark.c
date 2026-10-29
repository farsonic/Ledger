/*
 * spark.c - see drawing.h: sparklines, the small line, column and win/loss charts drawn inside a
 * cell from a row or column of values (Excel 2010's, kept in files as x14 sparklineGroups).
 * Markers for every point or for the high, low, first, last and negative ones, each in its colour.
 */
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "drawing.h"

#define FILLRGB(c) (FILL_ON | ((c) & 0xFFFFFF))

void spark_group_init(SparkGroup *g, int type)
{
  memset(g, 0, sizeof *g);
  g->type = (u8)type;
  g->color = FILLRGB(0x376092);
  g->negcolor = FILLRGB(0xD00000);
  g->markcolor = FILLRGB(0xD00000);
  g->highcolor = FILLRGB(0xD00000);
  g->lowcolor = FILLRGB(0xD00000);
  g->firstcolor = FILLRGB(0xD00000);
  g->lastcolor = FILLRGB(0xD00000);
}
Spark *spark_at(Sheet *s, i32 r, i32 c, SparkGroup **g)
{
  Drawing *d = s->drw;
  int i, k;
  if (!d) return NULL;
  for (i = 0; i < d->ng; i++)
    for (k = 0; k < d->g[i].n; k++)
      if (d->g[i].sp[k].r == r && d->g[i].sp[k].c == c) { if (g) *g = &d->g[i]; return &d->g[i].sp[k]; }
  return NULL;
}
/* a formula that is one absolute reference (with its sheet) */
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
/* sparklines in cells of area taken out (and their whole groups, if whole_groups) */
static int clear_in(Sheet *s, Rect a, int whole)
{
  Drawing *d = s->drw;
  int i, k, j, n = 0;
  if (!d) return 0;
  for (i = 0; i < d->ng; i++) {
    SparkGroup *g = &d->g[i];
    int hit = 0;
    for (k = 0; k < g->n; k++) if (g->sp[k].r >= a.r1 && g->sp[k].r <= a.r2 && g->sp[k].c >= a.c1 && g->sp[k].c <= a.c2) hit = 1;
    if (!hit) continue;
    for (k = j = 0; k < g->n; k++) {
      Spark *sp = &g->sp[k];
      if (whole || (sp->r >= a.r1 && sp->r <= a.r2 && sp->c >= a.c1 && sp->c <= a.c2)) { formula_free(sp->f); n++; continue; }
      g->sp[j++] = *sp;
    }
    g->n = j;
  }
  for (i = j = 0; i < d->ng; i++) { if (d->g[i].n) d->g[j++] = d->g[i]; else spark_group_clear(&d->g[i]); }
  d->ng = j;
  return n;
}
int book_add_sparklines(Book *b, int sheet, int type, Rect data, Rect loc)
{
  Sheet *s;
  Drawing *d;
  SparkGroup g, *ng;
  int n, k, down;
  if (sheet < 0 || sheet >= b->nsh || data.r2 < data.r1 || data.c2 < data.c1) return 0;
  s = b->sh[sheet];
  /* the locations are one row or one column; each takes a row (or column) of the data. A single
     location with a single row or column of data takes it whole */
  n = loc.r1 == loc.r2 ? loc.c2 - loc.c1 + 1 : loc.r2 - loc.r1 + 1;
  if (loc.r1 != loc.r2 && loc.c1 != loc.c2) return 0;
  if (n == 1) down = (data.r2 - data.r1) >= (data.c2 - data.c1) ? 2 : 3;   /* the one sparkline gets all of it */
  else if (loc.r1 == loc.r2) down = data.c2 - data.c1 + 1 == n ? 1 : (data.r2 - data.r1 + 1 == n ? 0 : -1);
  else down = data.r2 - data.r1 + 1 == n ? 0 : (data.c2 - data.c1 + 1 == n ? 1 : -1);
  if (down < 0 || n > 10000) return 0;
  spark_group_init(&g, type);
  if (!(g.sp = (Spark *)calloc((size_t)n, sizeof(Spark)))) return 0;
  for (k = 0; k < n; k++) {
    Rect a = data;
    if (down == 0) a.r1 = a.r2 = data.r1 + k;           /* data row k */
    else if (down == 1) a.c1 = a.c2 = data.c1 + k;      /* data column k */
    g.sp[k].r = loc.r1 == loc.r2 ? loc.r1 : loc.r1 + k;
    g.sp[k].c = loc.r1 == loc.r2 ? loc.c1 + k : loc.c1;
    if (!(g.sp[k].f = ref_formula(sheet, a))) { g.n = k; spark_group_clear(&g); return 0; }
    g.n = k + 1;
  }
  drw_begin(b, sheet);
  clear_in(s, loc, 0);
  if (!(d = drw_of(s)) || !(ng = (SparkGroup *)realloc(d->g, (size_t)(d->ng + 1) * sizeof(SparkGroup)))) { spark_group_clear(&g); drw_end(b); return 0; }
  d->g = ng;
  d->g[d->ng++] = g;
  drw_end(b);
  return 1;
}
int book_clear_sparklines(Book *b, int sheet, Rect area, int whole)
{
  int n;
  if (sheet < 0 || sheet >= b->nsh) return 0;
  drw_begin(b, sheet);
  n = clear_in(b->sh[sheet], area, whole);
  drw_end(b);
  return n;
}
int spark_values(Book *b, int sheet, const Spark *sp, double *out, int max)
{
  const Formula *f = sp->f;
  int i, n = 0;
  if (!f) return 0;
  for (i = 0; i < f->n && n < max; i++) {
    const Tok *t = &f->t[i];
    int sh;
    i32 r, c, r2, c2;
    if (t->op != T_REF && t->op != T_AREA) continue;
    sh = t->a == SHEET_SELF ? sheet : t->a;
    if (sh < 0 || sh >= b->nsh) continue;
    r2 = t->op == T_REF ? t->r1 : t->r2; c2 = t->op == T_REF ? t->c1 : t->c2;
    for (r = t->r1; r <= r2 && n < max; r++)
      for (c = t->c1; c <= c2 && n < max; c++) {
        const Value *v = cell_value(b, sh, r, c);
        out[n++] = v->t == V_NUM ? v->n : v->t == V_BOOL ? v->e : NAN;
      }
  }
  return n;
}
void spark_group_range(Book *b, int sheet, const SparkGroup *g, double *lo, double *hi)
{
  double v[512];
  int k, i, n;
  *lo = NAN; *hi = NAN;
  if (g->minmode == 2) *lo = g->minv;
  if (g->maxmode == 2) *hi = g->maxv;
  if (g->minmode != 1 && g->maxmode != 1) return;
  {
    double mn = INFINITY, mx = -INFINITY;
    for (k = 0; k < g->n; k++) {
      n = spark_values(b, sheet, &g->sp[k], v, 512);
      for (i = 0; i < n; i++) if (!isnan(v[i])) { if (v[i] < mn) mn = v[i]; if (v[i] > mx) mx = v[i]; }
    }
    if (g->minmode == 1 && isfinite(mn)) *lo = mn;
    if (g->maxmode == 1 && isfinite(mx)) *hi = mx;
  }
}
static Prim *pr(PrimList *l, int t) { return prim_add(l, t, CE_NONE); }
int spark_render(const SparkGroup *g, const double *in, int n, int w, int h, double lo, double hi, PrimList *out)
{
  double pad = h / 10.0, x0, y0, iw, ih, mn = INFINITY, mx = -INFINITY, span;
  double vals[512];
  int i, hiidx = -1, loidx = -1, first = -1, last = -1;
  if (n <= 0 || w < 3 || h < 3) return 0;
  if (n > 512) n = 512;
  for (i = 0; i < n; i++) {
    double v = in[i];
    if (isnan(v) && g->blanks == 1) v = 0;
    vals[i] = v;
    if (isnan(v)) continue;
    if (first < 0) first = i;
    last = i;
    if (v < mn) { mn = v; loidx = i; }
    if (v > mx) { mx = v; hiidx = i; }
  }
  if (first < 0) return 1;
  if (pad < 1) pad = 1;
  if (pad > 3) pad = 3;
  x0 = pad; y0 = pad; iw = w - 2 * pad; ih = h - 2 * pad;
  if (g->type == SP_LINE) {
    double lo2 = isnan(lo) ? mn : lo, hi2 = isnan(hi) ? mx : hi;
    i32 *pts = (i32 *)malloc((size_t)n * 2 * sizeof(i32));
    int np = 0;
    span = hi2 - lo2;
#define SX(j) (n > 1 ? x0 + (j) * iw / (n - 1) : x0 + iw / 2)
#define SY(v) (span > 0 ? y0 + ih - ((v) - lo2) / span * ih : y0 + ih / 2)
    if (!pts) return 0;
    if (g->axis && lo2 < 0 && hi2 > 0) { Prim *p = pr(out, PR_LINE); if (p && (p->pts = (i32 *)malloc(4 * sizeof(i32)))) { p->np = 2; p->pts[0] = 0; p->pts[1] = p->pts[3] = (i32)SY(0); p->pts[2] = w - 1; p->line = FILLRGB(0); p->width = 1; } }
    for (i = 0; i <= n; i++) {
      if (i < n && !isnan(vals[i])) { pts[2 * np] = (i32)floor(SX(i) + 0.5); pts[2 * np + 1] = (i32)floor(SY(vals[i]) + 0.5); np++; }
      else if (i < n && g->blanks == 2) continue;
      if (i == n || isnan(vals[i])) {
        if (np >= 2) {
          Prim *p = pr(out, PR_LINE);
          if (p && (p->pts = (i32 *)malloc((size_t)np * 2 * sizeof(i32)))) { memcpy(p->pts, pts, (size_t)np * 2 * sizeof(i32)); p->np = np; p->line = g->color; p->width = (u8)(g->weight > 150 ? 2 : 1); }
        }
        np = 0;
      }
    }
    free(pts);
    for (i = 0; i < n; i++) {
      u32 col = 0;
      if (isnan(vals[i])) continue;
      if (g->markers) col = g->markcolor;
      if (g->negative && vals[i] < 0) col = g->negcolor;
      if (g->first && i == first) col = g->firstcolor;
      if (g->last && i == last) col = g->lastcolor;
      if (g->low && i == loidx) col = g->lowcolor;
      if (g->high && i == hiidx) col = g->highcolor;
      if (n == 1 && !col) col = g->color;
      if (col) {
        Prim *p = pr(out, PR_ELLIPSE);
        int rr = h >= 18 ? 2 : 1;
        if (p) { p->x = (i32)SX(i) - rr; p->y = (i32)SY(vals[i]) - rr; p->w = p->h = 2 * rr + 1; p->fill = p->line = col; }
      }
    }
#undef SX
#undef SY
  } else {
    int win = g->type == SP_WINLOSS;
    double lo2 = win ? -1 : (isnan(lo) ? (mn < 0 ? mn : 0) : lo), hi2 = win ? 1 : (isnan(hi) ? (mx > 0 ? mx : 0) : hi);
    double slot = iw / n, bw = slot * 0.8, base;
    if (bw < 1) bw = 1;
    span = hi2 - lo2;
#define VY(v) (span > 0 ? y0 + ih - ((v) - lo2) / span * ih : y0 + ih)
    base = VY(0);
    for (i = 0; i < n; i++) {
      double v = vals[i], top;
      u32 col = g->color;
      Prim *p;
      if (isnan(v)) continue;
      if (win) { if (v == 0) continue; v = v > 0 ? 1 : -1; }
      if (vals[i] < 0) col = g->negative || win ? g->negcolor : g->color;
      if (g->first && i == first) col = g->firstcolor;
      if (g->last && i == last) col = g->lastcolor;
      if (g->low && i == loidx) col = g->lowcolor;
      if (g->high && i == hiidx) col = g->highcolor;
      top = VY(v);
      if (!(p = pr(out, PR_RECT))) break;
      p->x = (i32)floor(x0 + i * slot + (slot - bw) / 2 + 0.5);
      p->w = (i32)floor(bw + 0.5); if (p->w < 1) p->w = 1;
      p->y = (i32)floor((top < base ? top : base) + 0.5);
      p->h = (i32)floor(fabs(top - base) + 0.5); if (p->h < 1) p->h = 1;
      p->fill = col;
    }
    if (g->axis && !win && lo2 < 0) { Prim *p = pr(out, PR_LINE); if (p && (p->pts = (i32 *)malloc(4 * sizeof(i32)))) { p->np = 2; p->pts[0] = 0; p->pts[1] = p->pts[3] = (i32)base; p->pts[2] = w - 1; p->line = FILLRGB(0); p->width = 1; } }
#undef VY
  }
  return !out->oom;
}
