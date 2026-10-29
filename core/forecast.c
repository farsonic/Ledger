/*
 * forecast.c - Data > Forecast > Forecast Sheet (see analysis.h): a new sheet with a copy of the
 * timeline and values (sorted by time), a FORECAST.ETS column, the FORECAST.ETS.CONFINT confidence
 * bounds, optional FORECAST.ETS.STAT statistics, a table round it and a line (or column) chart.
 * Like Excel's and GridCraft's (crates/engine/src/cmd/forecast.rs, MIT licensed; this is C written
 * from its documented behaviour), the forecast cells are live formulas over the copied history,
 * so the forecast follows edits to it.
 */
#include "analysis.h"
#include "calc.h"
#include "drawing.h"
#include "table.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>

#define MAX_HISTORY 10000
#define MAX_PERIODS 10000
#define MAX_WORK 500000L             /* history rows x forecast rows: every forecast refits the model */

void forecast_defaults(ForecastOpts *o)
{
  memset(o, 0, sizeof *o);
  o->seasonality = -1; o->conf = 0.95; o->fill = 1; o->aggregate = 1;
}

typedef struct { double t; Value v; } Pt;
static int cmp_pt(const void *a, const void *b) { double x = ((const Pt *)a)->t, y = ((const Pt *)b)->t; return x < y ? -1 : x > y; }

/* the two ranges' cells: n of them (a header first when hdr); 0 with *why if no good */
static int cells(Book *b, int sheet, const char *text, int *sh, Rect *r, int *n, const char **why)
{
  if (!an_range(b, sheet, text, sh, r)) { *why = "The timeline or values range isn't valid."; return 0; }
  if (r->r1 != r->r2 && r->c1 != r->c2) { *why = "The timeline and the values are each one row or one column."; return 0; }
  *n = r->r1 == r->r2 ? r->c2 - r->c1 + 1 : r->r2 - r->r1 + 1;
  if (*n > MAX_HISTORY + 1) { *why = "At most 10,000 rows of history."; return 0; }
  return 1;
}
static const Value *at_k(Book *b, int sh, Rect r, int k) { return r.r1 == r.r2 && r.c1 != r.c2 ? cell_value(b, sh, r.r1, r.c1 + k) : cell_value(b, sh, r.r1 + k, r.c1); }
static void at_rc(Rect r, int k, i32 *rr, i32 *cc) { if (r.r1 == r.r2 && r.c1 != r.c2) { *rr = r.r1; *cc = r.c1 + k; } else { *rr = r.r1 + k; *cc = r.c1; } }

/* the points sorted by time, the header's names; *step the timeline's constant step */
static int history(Book *b, int sheet, const char *tl, const char *vl, Pt **pts, int *np, char *tname, char *vname, double *step,
                   int *sh, Rect *tr, int *hdr, const char **why)
{
  Rect vr;
  int vsh, nt, nv, k, i;
  double st = 0;
  *pts = NULL; *np = 0;
  if (!cells(b, sheet, tl, sh, tr, &nt, why)) return 0;
  if (vl) {
    if (!cells(b, sheet, vl, &vsh, &vr, &nv, why)) return 0;
    if (vsh != *sh) { *why = "The timeline and the values must be on one sheet."; return 0; }
    if (nv != nt) { *why = "The timeline and the values need the same number of cells."; return 0; }
  } else { vr = *tr; nv = nt; vsh = *sh; }
  *hdr = at_k(b, *sh, *tr, 0)->t == V_STR || (vl && at_k(b, *sh, vr, 0)->t == V_STR);
  strcpy(tname, "Timeline"); strcpy(vname, "Values");
  if (*hdr) {
    Buf o;
    buf_init(&o); to_text(b, at_k(b, *sh, *tr, 0), &o); if (o.data && o.data[0]) snprintf(tname, 64, "%s", o.data); buf_free(&o);
    if (vl) { buf_init(&o); to_text(b, at_k(b, *sh, vr, 0), &o); if (o.data && o.data[0]) snprintf(vname, 64, "%s", o.data); buf_free(&o); }
  }
  if (!(*pts = (Pt *)malloc(((size_t)nt + 1) * sizeof(Pt)))) { *why = "Not enough memory."; return 0; }
  for (k = *hdr; k < nt; k++) {
    const Value *t = at_k(b, *sh, *tr, k), *v = vl ? at_k(b, *sh, vr, k) : NULL;
    if (t->t == V_NUM) { (*pts)[*np].t = t->n; (*pts)[*np].v = v && v->t == V_NUM ? *v : val_empty(); (*np)++; }
    else if (t->t == V_EMPTY && (!v || v->t == V_EMPTY)) continue;
    else { *why = "The timeline must hold numbers or dates."; free(*pts); *pts = NULL; return 0; }
  }
  qsort(*pts, (size_t)*np, sizeof(Pt), cmp_pt);
  /* the step: the smallest gap, every other gap a whole number of them (missing points allowed) */
  for (i = 1; i < *np; i++) { double d = (*pts)[i].t - (*pts)[i - 1].t; if (d > 0 && (st == 0 || d < st)) st = d; }
  if (*np < 2 || st <= 0) { *why = "The timeline needs at least two points a constant step apart."; free(*pts); *pts = NULL; return 0; }
  for (i = 1; i < *np; i++) {
    double q = ((*pts)[i].t - (*pts)[i - 1].t) / st;
    if (fabs(q - floor(q + 0.5)) > 1e-6) { *why = "The timeline needs a constant step between its points."; free(*pts); *pts = NULL; return 0; }
  }
  /* more than 30% of the points missing: no constant step after all (as FORECAST.ETS sees it) */
  if (((*pts)[*np - 1].t - (*pts)[0].t) / st + 1 > *np / 0.7 + 1e-9) { *why = "The timeline needs a constant step between its points."; free(*pts); *pts = NULL; return 0; }
  *step = st;
  return 1;
}
int forecast_timeline(Book *b, int sheet, const char *timeline, double *last, double *step, int *npoints)
{
  Pt *pts;
  int np, sh, hdr;
  char tn[64], vn[64];
  Rect tr;
  const char *why;
  if (!history(b, sheet, timeline, NULL, &pts, &np, tn, vn, step, &sh, &tr, &hdr, &why)) return 0;
  *last = pts[np - 1].t; *npoints = np;
  free(pts);
  return 1;
}

static void col_letters(i32 c, char *out) { col_name(c, out); }
/* a sheet name as references write it */
static void qname(const char *name, char *out, size_t cap)
{
  const char *p;
  int plain = name[0] != 0;
  size_t o = 0;
  for (p = name; *p; p++) if (!((*p >= 'A' && *p <= 'Z') || (*p >= 'a' && *p <= 'z') || (*p >= '0' && *p <= '9') || *p == '_')) plain = 0;
  if (plain && !(name[0] >= '0' && name[0] <= '9')) { snprintf(out, cap, "%s", name); return; }
  out[o++] = '\'';
  for (p = name; *p && o + 3 < cap; p++) { if (*p == '\'') out[o++] = '\''; out[o++] = *p; }
  out[o++] = '\''; out[o] = 0;
}
static void put_text(Book *b, int sh, i32 r, i32 c, const char *t) { Value v = val_str(t); book_set_value(b, sh, r, c, &v); val_free(&v); }
static void put_f(Book *b, int sh, i32 r, i32 c, const char *f, int style)
{
  char t[600];
  snprintf(t, sizeof t, "=%s", f);
  book_input(b, sh, r, c, t, NULL);
  if (style) book_set_style(b, sh, r, c, style);
}
static Formula *fparse(Book *b, int sheet, const char *t) { int pos; return formula_parse(b, sheet, t, &pos); }

int forecast_sheet(Book *b, int sheet, const ForecastOpts *o, const char **why)
{
  Pt *pts = NULL;
  int np, sh, hdr, at, i, k, periods, nh = 0, width, tstyle = 0, vstyle = 0, manual = b->manualcalc;
  char tname[64], vname[64], hd[5][160], tail[40], q[80], f[600], tl[40], vl[40], cl[8];
  double step, last;
  Rect tr;
  i32 lastrow, endrow, r, tr0, tc0;
  long cap;
  *why = NULL;
  if (b->lockstruct) { *why = "The workbook is protected and cannot be changed."; return -1; }
  if (!history(b, sheet, o->timeline, o->values, &pts, &np, tname, vname, &step, &sh, &tr, &hdr, why)) return -1;
  for (i = 0; i < np; i++) if (pts[i].v.t == V_NUM) nh++;
  if (!nh) { free(pts); *why = "There are no values to forecast."; return -1; }
  if (o->seasonality > 8760 || o->seasonality < -1 || o->seasonality == 1) { free(pts); *why = "Seasonality is automatic, none (0) or a season length from 2 to 8,760."; return -1; }
  if (o->conf != 0 && !(o->conf > 0 && o->conf < 1)) { free(pts); *why = "The confidence interval is between 0 and 100%."; return -1; }
  last = pts[np - 1].t;
  cap = MAX_WORK / np;
  if (cap > MAX_PERIODS) cap = MAX_PERIODS;
  if (cap < 1) cap = 1;
  if (o->end != 0) {
    double s = (o->end - last) / step;
    if (s < 1e-9) { free(pts); *why = "The forecast end must be after the last point of the timeline."; return -1; }
    if (ceil(s - 1e-9) > cap) { free(pts); *why = "The forecast end is too far on for this much history."; return -1; }
    periods = (int)ceil(s - 1e-9);
  } else {
    periods = (np + 2) / 3;
    if (periods > cap) periods = (int)cap;
    if (periods < 1) periods = 1;
  }
  /* the source's number formats for the copies */
  at_rc(tr, hdr, &tr0, &tc0);
  {
    Style st;
    int s0 = sheet_cell_style(b->sh[sh], tr0, tc0);
    st = b->styles[s0]; memset(&st.border, 0, sizeof st.border); st.fill = 0; tstyle = book_style(b, &st);
  }
  {
    Rect vr; int vsh, nv;
    if (cells(b, sheet, o->values, &vsh, &vr, &nv, why)) {
      i32 rr, cc; Style st; int s0;
      at_rc(vr, hdr, &rr, &cc);
      s0 = sheet_cell_style(b->sh[vsh], rr, cc);
      st = b->styles[s0]; memset(&st.border, 0, sizeof st.border); st.fill = 0; vstyle = book_style(b, &st);
    }
    *why = NULL;
  }
  if (tstyle < 0) tstyle = 0;
  if (vstyle < 0) vstyle = 0;
  /* headings, made unique */
  snprintf(hd[0], sizeof hd[0], "%s", tname); snprintf(hd[1], sizeof hd[1], "%s", vname);
  snprintf(hd[2], sizeof hd[2], "Forecast(%s)", vname);
  width = 3;
  if (o->conf > 0) { snprintf(hd[3], sizeof hd[3], "Lower Confidence Bound(%s)", vname); snprintf(hd[4], sizeof hd[4], "Upper Confidence Bound(%s)", vname); width = 5; }
  for (i = 1; i < width; i++) {
    char base[160];
    int n2 = 2;
    strcpy(base, hd[i]);
    for (k = 0; k < i; k++) if (str_ieq(hd[k], hd[i])) { snprintf(hd[i], sizeof hd[i], "%.150s%d", base, n2++); k = -1; }
  }
  b->manualcalc = 1;
  book_begin(b);
  book_note_book(b);
  if ((at = an_add_sheet(b, NULL, sheet)) < 0) { book_end(b); b->manualcalc = manual; free(pts); *why = "A new sheet couldn't be added."; return -1; }
  lastrow = np + 1;                  /* 1-based row of the last history point */
  endrow = lastrow + periods;
  for (i = 0; i < width; i++) put_text(b, at, 0, i, hd[i]);
  for (i = 0; i < np; i++) {
    Value t = val_num(pts[i].t);
    book_set_value(b, at, i + 1, 0, &t);
    if (tstyle) book_set_style(b, at, i + 1, 0, tstyle);
    if (pts[i].v.t == V_NUM) { book_set_value(b, at, i + 1, 1, &pts[i].v); if (vstyle) book_set_style(b, at, i + 1, 1, vstyle); }
  }
  /* the forecast lines start at the last point so they join the values */
  snprintf(f, sizeof f, "B%ld", (long)lastrow); put_f(b, at, lastrow - 1, 2, f, vstyle);
  if (o->conf > 0) { snprintf(f, sizeof f, "C%ld", (long)lastrow); put_f(b, at, lastrow - 1, 3, f, vstyle); put_f(b, at, lastrow - 1, 4, f, vstyle); }
  snprintf(tail, sizeof tail, "%d,%d", o->seasonality < 0 ? 1 : o->seasonality, o->fill ? 1 : 0);
  if (o->aggregate > 1 && o->aggregate <= 7) snprintf(tail + strlen(tail), sizeof tail - strlen(tail), ",%d", o->aggregate);
  snprintf(tl, sizeof tl, "$A$2:$A$%ld", (long)lastrow); snprintf(vl, sizeof vl, "$B$2:$B$%ld", (long)lastrow);
  for (k = 1; k <= periods; k++) {
    Value t = val_num(last + step * k);
    r = lastrow + k;               /* 1-based */
    book_set_value(b, at, r - 1, 0, &t);
    if (tstyle) book_set_style(b, at, r - 1, 0, tstyle);
    snprintf(f, sizeof f, "FORECAST.ETS(A%ld,%s,%s,%s)", (long)r, vl, tl, tail); put_f(b, at, r - 1, 2, f, vstyle);
    if (o->conf > 0) {
      char ci[300];
      snprintf(ci, sizeof ci, "FORECAST.ETS.CONFINT(A%ld,%s,%s,%.15g,%s)", (long)r, vl, tl, o->conf, tail);
      snprintf(f, sizeof f, "C%ld-%s", (long)r, ci); put_f(b, at, r - 1, 3, f, vstyle);
      snprintf(f, sizeof f, "C%ld+%s", (long)r, ci); put_f(b, at, r - 1, 4, f, vstyle);
    }
  }
  if (o->stats) {
    static const char *const lab[] = { "Alpha", "Beta", "Gamma", "MASE", "SMAPE", "MAE", "RMSE" };
    i32 sc = width + 1;
    put_text(b, at, 0, sc, "Statistic"); put_text(b, at, 0, sc + 1, "Value");
    for (k = 0; k < 7; k++) {
      put_text(b, at, k + 1, sc, lab[k]);
      snprintf(f, sizeof f, "FORECAST.ETS.STAT(%s,%s,%d,%s)", vl, tl, k + 1, tail); put_f(b, at, k + 1, sc + 1, f, 0);
    }
    book_set_col_width(b, at, sc, 80);
  }
  for (i = 0; i < width; i++) book_set_col_width(b, at, i, (u16)(i < 2 ? 90 : i == 2 ? 130 : 200));
  {
    Rect tb;
    tb.r1 = 0; tb.c1 = 0; tb.r2 = endrow - 1; tb.c2 = width - 1;
    book_table_add(b, at, tb, 1, "TableStyleMedium2");
  }
  /* the chart, right of the table (and the statistics) */
  {
    Sheet *s = b->sh[at];
    Chart *c = chart_new(o->column ? CK_COMBO : CK_LINE);
    DObj *ob;
    i32 ccol = o->stats ? width + 4 : width + 1, x, y;
    qname(s->name, q, sizeof q);
    if (c && (c->s = (CSeries *)calloc((size_t)width, sizeof(CSeries)))) {
      for (i = 1; i < width; i++) {
        CSeries *se = &c->s[c->ns++];
        col_letters(i, cl);
        snprintf(f, sizeof f, "%s!$%s$1", q, cl); se->name = fparse(b, at, f);
        snprintf(f, sizeof f, "%s!$A$2:$A$%ld", q, (long)endrow); se->cat = fparse(b, at, f);
        snprintf(f, sizeof f, "%s!$%s$2:$%s$%ld", q, cl, cl, (long)endrow); se->val = fparse(b, at, f);
        se->kind = o->column ? (u8)(i >= 3 ? CK_LINE : CK_COL) : CK_SAME;
        se->nomarker = 1;
      }
      c->legend = LG_BOTTOM;
      c->hgrid = 1;
      c->has_title = 0;
      if ((ob = drw_new(s, OB_CHART))) {
        ob->chart = c; c = NULL;
        x = sheet_col_px(s, ccol); y = sheet_row_px(s, 1);
        dobj_place(s, ob, x, y, x + 480, y + 288);
      }
    }
    if (c) chart_free(c);
  }
  book_end(b);
  b->manualcalc = manual;
  if (!manual) book_recalc(b);
  b->active = at;
  free(pts);
  return at;
}
