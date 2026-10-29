/*
 * toolpak.c - Data > Analysis > Data Analysis (see analysis.h): the Analysis ToolPak's tools. Each
 * works out a report from input ranges and writes it as values (not formulas) to an output range or
 * a new sheet, laid out as Excel lays its reports out.
 *
 * GridCraft's analysis.rs (MIT licensed) has thirteen of them; this has those and the rest of
 * Excel's list (two-factor Anova, Fourier Analysis, Random Number Generation, Sampling, z-Test),
 * written in C from the documented behaviour. Distribution values (t, F, z) come from the
 * worksheet functions (T.DIST.2T, F.DIST.RT, T.INV.2T...), so the reports agree with the formulas
 * a user would write.
 */
#include "analysis.h"
#include "calc.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdarg.h>
#include <math.h>

const char *const toolpak_names[TP_N] = {
  "Anova: Single Factor", "Anova: Two-Factor With Replication", "Anova: Two-Factor Without Replication", "Correlation", "Covariance",
  "Descriptive Statistics", "Exponential Smoothing", "F-Test Two-Sample for Variances", "Fourier Analysis", "Histogram",
  "Moving Average", "Random Number Generation", "Rank and Percentile", "Regression", "Sampling",
  "t-Test: Paired Two Sample for Means", "t-Test: Two-Sample Assuming Equal Variances", "t-Test: Two-Sample Assuming Unequal Variances",
  "z-Test: Two Sample for Means" };

#define MAX_INPUT 1000000L
#define MAX_REGRESSORS 16
#define MAX_MATRIX 256
#define MAX_BINS 100000

static int finite_(double x) { return x == x && x - x == 0; }

void toolpak_defaults(TPParams *p, int tool)
{
  memset(p, 0, sizeof *p);
  p->tool = tool;
  p->alpha = 0.05; p->conf = tool == TP_REGRESSION ? 95 : tool == TP_DESCR ? 95 : 0;
  p->damping = 0.3; p->interval = 3; p->rowsper = 1; p->summary = 1;
  p->nvars = 1; p->nnums = 10; p->dist = TPD_UNIFORM; p->p1 = 0; p->p2 = 1; p->p3 = 1; p->p4 = 1; p->p5 = 1;
  p->nsample = 1; p->outnew = 1;
}

/* ------------------------------------------------------------------ the report */
typedef struct { i32 r, c; Value v; u8 pct; } GCell;
typedef struct { GCell *c; int n, cap; int oom; } Grid;
static void g_put(Grid *g, i32 r, i32 c, Value v, int pct)
{
  if (v.t == V_EMPTY) return;
  if (g->n == g->cap) {
    int cap = g->cap ? g->cap * 2 : 64;
    GCell *nc = (GCell *)realloc(g->c, (size_t)cap * sizeof(GCell));
    if (!nc) { g->oom = 1; val_free(&v); return; }
    g->c = nc; g->cap = cap;
  }
  g->c[g->n].r = r; g->c[g->n].c = c; g->c[g->n].v = v; g->c[g->n].pct = (u8)pct;
  g->n++;
}
static void g_txt(Grid *g, i32 r, i32 c, const char *t) { if (t && *t) g_put(g, r, c, val_str(t), 0); }
static Value numv(double x) { return finite_(x) ? val_num(x) : val_err(E_DIV0); }
static void g_num(Grid *g, i32 r, i32 c, double x) { g_put(g, r, c, numv(x), 0); }
static void g_err(Grid *g, i32 r, i32 c, int e) { g_put(g, r, c, val_err(e), 0); }
static void g_free(Grid *g) { int i; for (i = 0; i < g->n; i++) val_free(&g->c[i].v); free(g->c); memset(g, 0, sizeof *g); }
static void g_labels(Grid *g, i32 r, i32 c, const char *const *items, int n) { int k; for (k = 0; k < n; k++) g_txt(g, r, c + k, items[k]); }

/* a worksheet function's value, the formula made from fmt (NAN if it isn't a number) */
static double wf(Book *b, const char *fmt, ...)
{
  char t[300];
  va_list ap;
  Value v;
  double x;
  va_start(ap, fmt);
  vsnprintf(t, sizeof t, fmt, ap);
  va_end(ap);
  v = book_eval_text(b, 0, t);
  x = v.t == V_NUM ? v.n : NAN;
  val_free(&v);
  return x;
}

/* ------------------------------------------------------------------ input */
int an_range(Book *b, int defsheet, const char *text, int *sheet, Rect *r)
{
  char u[200], *q;
  u16 s; u8 fl;
  int n, i;
  while (*text == ' ' || *text == '=') text++;
  strncpy(u, text, sizeof u - 1); u[sizeof u - 1] = 0;
  for (q = u + strlen(u); q > u && q[-1] == ' '; q--) q[-1] = 0;
  if (!u[0]) return 0;
  n = parse_ref(b, u, &s, r, &fl);
  if (!n || u[n]) {
    n = 0;
    for (i = 0; i < b->nnames; i++)
      if (str_ieq(b->names[i].name, u) && b->names[i].formula_text && (b->names[i].sheet == SHEET_SELF || b->names[i].sheet == defsheet)) {
        const char *ft = b->names[i].formula_text;
        n = parse_ref(b, ft[0] == '=' ? ft + 1 : ft, &s, r, &fl);
        if (n && s == SHEET_SELF && b->names[i].sheet != SHEET_SELF) s = b->names[i].sheet;
        break;
      }
    if (!n) return 0;
  }
  *sheet = s == SHEET_SELF ? defsheet : s;
  if (*sheet < 0 || *sheet >= b->nsh) return 0;
  if (fl & (RF_COLS | RF_ROWS)) {
    /* whole columns or rows: cut to what the sheet uses */
    Sheet *sh = b->sh[*sheet];
    if (fl & RF_COLS) { r->r1 = 0; r->r2 = sheet_last_row(sh) < 0 ? 0 : sheet_last_row(sh); }
    if (fl & RF_ROWS) { r->c1 = 0; r->c2 = sheet_last_col(sh) < 0 ? 0 : sheet_last_col(sh); }
  }
  return 1;
}

/* a variable: its name and its cells' values (borrowed) */
typedef struct { char name[64]; int n; const Value **v; } Var;
typedef struct { Var *v; int n; const Value **all; } Vars;
static void vars_free(Vars *vs) { free(vs->v); free(vs->all); memset(vs, 0, sizeof *vs); }
static void value_text(Book *b, const Value *v, char *out, size_t cap)
{
  Buf o;
  buf_init(&o);
  to_text(b, v, &o);
  snprintf(out, cap, "%s", o.data ? o.data : "");
  buf_free(&o);
}
/* the range split into variables down its columns (or along its rows); with labels the first cell
   of each names it, else defname with its number ("Column 1") */
static int variables(Book *b, int sheet, Rect r, int byrows, int labels, const char *defname, Vars *vs)
{
  int outer = byrows ? r.r2 - r.r1 + 1 : r.c2 - r.c1 + 1, inner = byrows ? r.c2 - r.c1 + 1 : r.r2 - r.r1 + 1, i, j, k = 0;
  memset(vs, 0, sizeof *vs);
  if ((long)outer * inner > MAX_INPUT) return 0;
  vs->v = (Var *)calloc((size_t)outer, sizeof(Var));
  vs->all = (const Value **)malloc((size_t)outer * inner * sizeof(Value *));
  if (!vs->v || !vs->all) { vars_free(vs); return 0; }
  vs->n = outer;
  for (i = 0; i < outer; i++) {
    Var *v = &vs->v[i];
    v->v = &vs->all[k];
    for (j = 0; j < inner; j++) vs->all[k++] = byrows ? cell_value(b, sheet, r.r1 + i, r.c1 + j) : cell_value(b, sheet, r.r1 + j, r.c1 + i);
    v->n = inner;
    snprintf(v->name, sizeof v->name, "%s%d", defname, i + 1);
    if (labels && inner) {
      char t[64];
      value_text(b, v->v[0], t, sizeof t);
      if (t[0]) snprintf(v->name, sizeof v->name, "%s", t);
      v->v++; v->n--;
    }
  }
  return 1;
}
/* a variable's numbers (blanks skipped when skip); 0 if something else is there. *out malloc'd */
static int numbers(const Var *v, int skip, double **out, int *n)
{
  int i;
  *n = 0;
  if (!(*out = (double *)malloc(((size_t)v->n + 1) * sizeof(double)))) return 0;
  for (i = 0; i < v->n; i++) {
    const Value *x = v->v[i];
    if (x->t == V_NUM) (*out)[(*n)++] = x->n;
    else if (x->t == V_EMPTY && skip) continue;
    else { free(*out); *out = NULL; return 0; }
  }
  return 1;
}

/* ------------------------------------------------------------------ statistics */
static double mean(const double *x, int n) { double s = 0; int i; if (!n) return NAN; for (i = 0; i < n; i++) s += x[i]; return s / n; }
static double ssq(const double *x, int n) { double m = mean(x, n), s = 0; int i; for (i = 0; i < n; i++) s += (x[i] - m) * (x[i] - m); return s; }
static double var_s(const double *x, int n) { return n < 2 ? NAN : ssq(x, n) / (n - 1); }
static double sum(const double *x, int n) { double s = 0; int i; for (i = 0; i < n; i++) s += x[i]; return s; }
static double sxy(const double *x, const double *y, int n) { double mx = mean(x, n), my = mean(y, n), s = 0; int i; for (i = 0; i < n; i++) s += (x[i] - mx) * (y[i] - my); return s; }
static int cmpd(const void *a, const void *b) { double x = *(const double *)a, y = *(const double *)b; return x < y ? -1 : x > y; }
static double correl(const double *x, const double *y, int n) { double d = sqrt(ssq(x, n) * ssq(y, n)); return n == 0 || !(d > 0) ? NAN : sxy(x, y, n) / d; }

/* ------------------------------------------------------------------ the tools */
#define FAIL(msg) do { *why = msg; goto out; } while (0)

static int descriptive(Book *b, int sheet, const TPParams *p, Grid *g, const char **why)
{
  Vars vs; Rect r; int sh, i, ok = 0;
  if (!an_range(b, sheet, p->in1, &sh, &r)) { *why = "The input range isn't valid."; return 0; }
  if (!variables(b, sh, r, p->byrows, p->labels, "Column", &vs)) { *why = "The input range is too large."; return 0; }
  if (!p->summary && p->conf <= 0 && p->kl <= 0 && p->ks <= 0) FAIL("Choose at least one of Summary statistics, Confidence Level, Kth Largest or Kth Smallest.");
  for (i = 0; i < vs.n; i++) {
    double *x = NULL, *srt;
    int n, row = 2, c = i * 2;
    if (!numbers(&vs.v[i], 1, &x, &n)) FAIL("The input range contains non-numeric data.");
    srt = x;
    qsort(srt, (size_t)n, sizeof(double), cmpd);
    g_txt(g, 0, c, vs.v[i].name);
    if (p->summary) {
      double m = mean(x, n), sd = sqrt(var_s(x, n)), s2 = 0, s3 = 0, s4 = 0;
      int k, run = 1, best = 1;
      double mode = 0;
      g_txt(g, row, c, "Mean"); g_num(g, row++, c + 1, m);
      g_txt(g, row, c, "Standard Error"); g_num(g, row++, c + 1, n < 2 ? NAN : sd / sqrt((double)n));
      g_txt(g, row, c, "Median");
      if (n) g_num(g, row++, c + 1, n % 2 ? srt[n / 2] : (srt[n / 2 - 1] + srt[n / 2]) / 2); else g_err(g, row++, c + 1, E_NUM);
      /* the mode: the value repeated most, the first to get there in the data's order */
      for (k = 1; k <= n; k++) {
        if (k < n && srt[k] == srt[k - 1]) { run++; continue; }
        if (run > best) best = run;
        run = 1;
      }
      g_txt(g, row, c, "Mode");
      if (best < 2) g_err(g, row++, c + 1, E_NA);
      else {
        for (k = 0; k < vs.v[i].n; k++) {
          const Value *xv = vs.v[i].v[k];
          int cnt = 0, q;
          if (xv->t != V_NUM) continue;
          for (q = 0; q < n; q++) if (srt[q] == xv->n) cnt++;
          if (cnt == best) { mode = xv->n; break; }
        }
        g_num(g, row++, c + 1, mode);
      }
      g_txt(g, row, c, "Standard Deviation"); g_num(g, row++, c + 1, sd);
      g_txt(g, row, c, "Sample Variance"); g_num(g, row++, c + 1, var_s(x, n));
      for (k = 0; k < n; k++) { double z = (x[k] - m) / sd; s2 += z * z; s3 += z * z * z; s4 += z * z * z * z; }
      g_txt(g, row, c, "Kurtosis");
      g_num(g, row++, c + 1, n < 4 || !(sd > 0) ? NAN : (double)n * (n + 1) / ((n - 1.0) * (n - 2) * (n - 3)) * s4 - 3.0 * (n - 1) * (n - 1) / ((n - 2.0) * (n - 3)));
      g_txt(g, row, c, "Skewness"); g_num(g, row++, c + 1, n < 3 || !(sd > 0) ? NAN : (double)n / ((n - 1.0) * (n - 2)) * s3);
      g_txt(g, row, c, "Range"); g_num(g, row++, c + 1, n ? srt[n - 1] - srt[0] : 0);
      g_txt(g, row, c, "Minimum"); g_num(g, row++, c + 1, n ? srt[0] : 0);
      g_txt(g, row, c, "Maximum"); g_num(g, row++, c + 1, n ? srt[n - 1] : 0);
      g_txt(g, row, c, "Sum"); g_num(g, row++, c + 1, sum(x, n));
      g_txt(g, row, c, "Count"); g_num(g, row++, c + 1, n);
    }
    if (p->kl > 0) {
      char t[40];
      snprintf(t, sizeof t, "Largest(%d)", p->kl); g_txt(g, row, c, t);
      if (p->kl <= n) g_num(g, row++, c + 1, srt[n - p->kl]); else g_err(g, row++, c + 1, E_NUM);
    }
    if (p->ks > 0) {
      char t[40];
      snprintf(t, sizeof t, "Smallest(%d)", p->ks); g_txt(g, row, c, t);
      if (p->ks <= n) g_num(g, row++, c + 1, srt[p->ks - 1]); else g_err(g, row++, c + 1, E_NUM);
    }
    if (p->conf > 0) {
      char t[60];
      snprintf(t, sizeof t, "Confidence Level(%.1f%%)", p->conf); g_txt(g, row, c, t);
      g_num(g, row++, c + 1, n < 2 ? NAN : wf(b, "T.INV.2T(%.17g,%d)", 1 - p->conf / 100, n - 1) * sqrt(var_s(x, n)) / sqrt((double)n));
    }
    free(x);
  }
  ok = 1;
out:
  vars_free(&vs);
  return ok;
}

static int histogram(Book *b, int sheet, const TPParams *p, Grid *g, const char **why)
{
  Rect r, br; int sh, bsh, n = 0, nb = 0, i, k, ok = 0;
  double *x = NULL, *bins = NULL;
  long *freq = NULL;
  i32 y, xx;
  if (!an_range(b, sheet, p->in1, &sh, &r)) { *why = "The input range isn't valid."; return 0; }
  if ((long)(r.r2 - r.r1 + 1) * (r.c2 - r.c1 + 1) > MAX_INPUT) { *why = "The input range is too large."; return 0; }
  x = (double *)malloc(((size_t)(r.r2 - r.r1 + 1) * (r.c2 - r.c1 + 1) + 1) * sizeof(double));
  if (!x) FAIL("Not enough memory.");
  for (y = r.r1; y <= r.r2; y++)
    for (xx = r.c1; xx <= r.c2; xx++) {
      const Value *v = cell_value(b, sh, y, xx);
      if (p->labels && y == r.r1 && xx == r.c1) continue;
      if (v->t == V_NUM) x[n++] = v->n;
      else if (v->t != V_EMPTY) FAIL("The input range contains non-numeric data.");
    }
  if (p->in2[0]) {
    if (!an_range(b, sheet, p->in2, &bsh, &br)) FAIL("The bin range isn't valid.");
    if ((long)(br.r2 - br.r1 + 1) * (br.c2 - br.c1 + 1) > MAX_BINS) FAIL("Too many bins (100,000 at most).");
    bins = (double *)malloc(((size_t)(br.r2 - br.r1 + 1) * (br.c2 - br.c1 + 1) + 1) * sizeof(double));
    if (!bins) FAIL("Not enough memory.");
    for (y = br.r1; y <= br.r2; y++)
      for (xx = br.c1; xx <= br.c2; xx++) {
        const Value *v = cell_value(b, bsh, y, xx);
        if (p->labels && y == br.r1 && xx == br.c1) continue;
        if (v->t == V_NUM) bins[nb++] = v->n;
        else if (v->t != V_EMPTY) FAIL("The bin range contains non-numeric data.");
      }
  } else {
    /* evenly spaced bins from the smallest to the largest, the square root of the count of them */
    double lo = HUGE_VAL, hi = -HUGE_VAL;
    int kk;
    for (i = 0; i < n; i++) { if (x[i] < lo) lo = x[i]; if (x[i] > hi) hi = x[i]; }
    if (!n) FAIL("The input range holds no numbers.");
    kk = (int)floor(sqrt((double)n));
    if (kk < 1) kk = 1;
    if (kk > 1000) kk = 1000;
    if (!(bins = (double *)malloc((size_t)(kk + 2) * sizeof(double)))) FAIL("Not enough memory.");
    if (hi == lo) bins[nb++] = lo;
    else for (i = 0; i <= kk; i++) bins[nb++] = lo + (hi - lo) * i / kk;
  }
  if (!nb) FAIL("The bin range holds no numbers.");
  qsort(bins, (size_t)nb, sizeof(double), cmpd);
  if (!(freq = (long *)calloc((size_t)nb + 1, sizeof(long)))) FAIL("Not enough memory.");
  for (i = 0; i < n; i++) {
    int lo = 0, hi = nb;
    while (lo < hi) { int mid = (lo + hi) / 2; if (bins[mid] < x[i]) lo = mid + 1; else hi = mid; }
    freq[lo]++;
  }
  {
    int pass, c0 = 0;
    int *order = (int *)malloc(((size_t)nb + 1) * sizeof(int));
    if (!order) FAIL("Not enough memory.");
    for (pass = 0; pass < (p->pareto ? 2 : 1); pass++) {
      long acc = 0;
      for (k = 0; k <= nb; k++) order[k] = k;
      if (pass) {
        /* the Pareto table: by frequency, largest first (ties keep their order) */
        int a, bb;
        for (a = 1; a <= nb; a++) for (bb = a; bb > 0 && freq[order[bb]] > freq[order[bb - 1]]; bb--) { int t = order[bb]; order[bb] = order[bb - 1]; order[bb - 1] = t; }
        c0 = p->cumulative ? 3 : 2;
      }
      g_txt(g, 0, c0, "Bin"); g_txt(g, 0, c0 + 1, "Frequency");
      if (p->cumulative) g_txt(g, 0, c0 + 2, "Cumulative %");
      for (k = 0; k <= nb; k++) {
        int q = order[k];
        if (q < nb) g_num(g, k + 1, c0, bins[q]); else g_txt(g, k + 1, c0, "More");
        g_num(g, k + 1, c0 + 1, (double)freq[q]);
        acc += freq[q];
        if (p->cumulative) g_put(g, k + 1, c0 + 2, numv(n ? (double)acc / n : NAN), 1);
      }
    }
    free(order);
  }
  ok = 1;
out:
  free(x); free(bins); free(freq);
  return ok;
}

/* correlation (covar 0) or covariance (1) matrices */
static int matrix(Book *b, int sheet, const TPParams *p, int covar, Grid *g, const char **why)
{
  Vars vs; Rect r; int sh, i, j, ok = 0;
  double **x = NULL;
  int *n = NULL;
  if (!an_range(b, sheet, p->in1, &sh, &r)) { *why = "The input range isn't valid."; return 0; }
  if (!variables(b, sh, r, p->byrows, p->labels, "Column ", &vs)) { *why = "The input range is too large."; return 0; }
  if (vs.n > MAX_MATRIX) FAIL("Too many variables (256 at most).");
  x = (double **)calloc((size_t)vs.n, sizeof(double *)); n = (int *)calloc((size_t)vs.n, sizeof(int));
  if (!x || !n) FAIL("Not enough memory.");
  for (i = 0; i < vs.n; i++) if (!numbers(&vs.v[i], 0, &x[i], &n[i])) FAIL("The input range contains non-numeric data.");
  for (i = 0; i < vs.n; i++) {
    g_txt(g, 0, i + 1, vs.v[i].name); g_txt(g, i + 1, 0, vs.v[i].name);
    for (j = 0; j <= i; j++) {
      double v;
      if (!covar) v = i == j ? 1 : correl(x[i], x[j], n[i]);
      else v = n[i] ? sxy(x[i], x[j], n[i]) / n[i] : NAN;
      g_num(g, i + 1, j + 1, v);
    }
  }
  ok = 1;
out:
  if (x) for (i = 0; i < vs.n; i++) free(x[i]);
  free(x); free(n);
  vars_free(&vs);
  return ok;
}

/* inverts a small symmetric matrix (Gauss-Jordan with partial pivoting); 0 if singular */
static int invert(double *a, double *inv, int n)
{
  int i, j, col;
  double scale = 0;
  for (i = 0; i < n; i++) if (fabs(a[i * n + i]) > scale) scale = fabs(a[i * n + i]);
  for (i = 0; i < n; i++) for (j = 0; j < n; j++) inv[i * n + j] = i == j;
  for (col = 0; col < n; col++) {
    int piv = col;
    double pv, f;
    for (i = col + 1; i < n; i++) if (fabs(a[i * n + col]) > fabs(a[piv * n + col])) piv = i;
    pv = a[piv * n + col];
    if (!(fabs(pv) > 1e-12 * (scale > 0 ? scale : 1e-300))) return 0;
    for (j = 0; j < n; j++) { double t = a[col * n + j]; a[col * n + j] = a[piv * n + j]; a[piv * n + j] = t; t = inv[col * n + j]; inv[col * n + j] = inv[piv * n + j]; inv[piv * n + j] = t; }
    for (j = 0; j < n; j++) { a[col * n + j] /= pv; inv[col * n + j] /= pv; }
    for (i = 0; i < n; i++) {
      if (i == col || (f = a[i * n + col]) == 0) continue;
      for (j = 0; j < n; j++) { a[i * n + j] -= f * a[col * n + j]; inv[i * n + j] -= f * inv[col * n + j]; }
    }
  }
  return 1;
}

static int regression(Book *b, int sheet, const TPParams *p, Grid *g, const char **why)
{
  Vars yv, xv; Rect yr, xr; int ysh, xsh, k = 0, n, i, j, a, c, ok = 0, zero = p->constzero, row;
  double *y = NULL, **xs = NULL, *xm = NULL, *sxx = NULL, *inv = NULL, *bco = NULL, *se = NULL, *pred = NULL;
  double ym, sse = 0, ssr = 0, dfr, dfe, dft, sst, r2, msr, mse, f, conf = p->conf > 0 && p->conf < 100 ? p->conf : 95, t95, tc;
  char lo2[40], hi2[40];
  memset(&yv, 0, sizeof yv); memset(&xv, 0, sizeof xv);
  if (!an_range(b, sheet, p->in1, &ysh, &yr)) { *why = "The input Y range isn't valid."; return 0; }
  if (!an_range(b, sheet, p->in2, &xsh, &xr)) { *why = "The input X range isn't valid."; return 0; }
  if (yr.c1 != yr.c2) { *why = "Input Y range must be a single column."; return 0; }
  if (xr.r2 - xr.r1 != yr.r2 - yr.r1) { *why = "Input Y range and input X range must have the same number of rows."; return 0; }
  if (xr.c2 - xr.c1 + 1 > MAX_REGRESSORS) { *why = "Input X range must have 16 columns or fewer."; return 0; }
  if (!variables(b, ysh, yr, 0, p->labels, "Y", &yv) || !variables(b, xsh, xr, 0, p->labels, "X Variable ", &xv)) FAIL("The input range is too large.");
  if (!p->labels) strcpy(yv.v[0].name, "Y");
  k = xv.n;
  if (!numbers(&yv.v[0], 0, &y, &n)) FAIL("Input range contains non-numeric data.");
  if (!(xs = (double **)calloc((size_t)k, sizeof(double *)))) FAIL("Not enough memory.");
  for (j = 0; j < k; j++) { int nn; if (!numbers(&xv.v[j], 0, &xs[j], &nn)) FAIL("Input range contains non-numeric data."); }
  if (n <= (zero ? k : k + 1)) FAIL("Too few observations for this many X variables.");
  xm = (double *)calloc((size_t)k, sizeof(double)); sxx = (double *)calloc((size_t)k * k, sizeof(double)); inv = (double *)calloc((size_t)k * k, sizeof(double));
  bco = (double *)calloc((size_t)k + 1, sizeof(double)); se = (double *)calloc((size_t)k + 1, sizeof(double)); pred = (double *)calloc((size_t)n, sizeof(double));
  if (!xm || !sxx || !inv || !bco || !se || !pred) FAIL("Not enough memory.");
  ym = zero ? 0 : mean(y, n);
  for (j = 0; j < k; j++) xm[j] = zero ? 0 : mean(xs[j], n);
  {
    double *sy = (double *)calloc((size_t)k, sizeof(double)), *cp;
    if (!sy || !(cp = (double *)malloc((size_t)k * k * sizeof(double)))) { free(sy); FAIL("Not enough memory."); }
    for (i = 0; i < n; i++)
      for (a = 0; a < k; a++) {
        double da = xs[a][i] - xm[a];
        sy[a] += da * (y[i] - ym);
        for (c = 0; c < k; c++) sxx[a * k + c] += da * (xs[c][i] - xm[c]);
      }
    memcpy(cp, sxx, (size_t)k * k * sizeof(double));
    if (!invert(cp, inv, k)) { free(sy); free(cp); FAIL("The X variables are collinear; the regression can't be worked out."); }
    for (a = 0; a < k; a++) { bco[a + 1] = 0; for (c = 0; c < k; c++) bco[a + 1] += inv[a * k + c] * sy[c]; }
    free(sy); free(cp);
  }
  bco[0] = 0;
  if (!zero) { bco[0] = ym; for (j = 0; j < k; j++) bco[0] -= bco[j + 1] * xm[j]; }
  for (i = 0; i < n; i++) {
    pred[i] = bco[0];
    for (j = 0; j < k; j++) pred[i] += bco[j + 1] * xs[j][i];
    sse += (y[i] - pred[i]) * (y[i] - pred[i]);
    ssr += (pred[i] - ym) * (pred[i] - ym);
  }
  dfr = k; dfe = zero ? n - dfr : n - dfr - 1; dft = zero ? n : n - 1;
  {
    double s2 = sse / dfe, q = 0;
    if (!zero) {
      for (a = 0; a < k; a++) for (c = 0; c < k; c++) q += xm[a] * inv[a * k + c] * xm[c];
      se[0] = sqrt(s2 * (1.0 / n + q));
    } else se[0] = NAN;
    for (a = 0; a < k; a++) se[a + 1] = sqrt(s2 * inv[a * k + a]);
  }
  sst = ssr + sse;
  r2 = sst > 0 ? ssr / sst : NAN;
  g_txt(g, 0, 0, "SUMMARY OUTPUT");
  g_txt(g, 2, 0, "Regression Statistics");
  g_txt(g, 3, 0, "Multiple R"); g_num(g, 3, 1, sqrt(r2));
  g_txt(g, 4, 0, "R Square"); g_num(g, 4, 1, r2);
  g_txt(g, 5, 0, "Adjusted R Square"); g_num(g, 5, 1, 1 - (sse / dfe) / (sst / dft));
  g_txt(g, 6, 0, "Standard Error"); g_num(g, 6, 1, sqrt(sse / dfe));
  g_txt(g, 7, 0, "Observations"); g_num(g, 7, 1, n);
  g_txt(g, 9, 0, "ANOVA");
  { static const char *const h[] = { "df", "SS", "MS", "F", "Significance F" }; g_labels(g, 10, 1, h, 5); }
  msr = ssr / dfr; mse = sse / dfe; f = msr / mse;
  g_txt(g, 11, 0, "Regression"); g_num(g, 11, 1, dfr); g_num(g, 11, 2, ssr); g_num(g, 11, 3, msr); g_num(g, 11, 4, f);
  g_num(g, 11, 5, finite_(f) ? wf(b, "F.DIST.RT(%.17g,%.17g,%.17g)", f, dfr, dfe) : NAN);
  g_txt(g, 12, 0, "Residual"); g_num(g, 12, 1, dfe); g_num(g, 12, 2, sse); g_num(g, 12, 3, mse);
  g_txt(g, 13, 0, "Total"); g_num(g, 13, 1, dft); g_num(g, 13, 2, sst);
  snprintf(lo2, sizeof lo2, "Lower %.1f%%", conf); snprintf(hi2, sizeof hi2, "Upper %.1f%%", conf);
  { const char *h[8]; h[0] = "Coefficients"; h[1] = "Standard Error"; h[2] = "t Stat"; h[3] = "P-value"; h[4] = "Lower 95%"; h[5] = "Upper 95%"; h[6] = lo2; h[7] = hi2; g_labels(g, 15, 1, h, 8); }
  t95 = wf(b, "T.INV.2T(0.05,%.17g)", dfe);
  tc = wf(b, "T.INV.2T(%.17g,%.17g)", 1 - conf / 100, dfe);
  for (j = 0; j <= k; j++) {
    double t;
    row = 16 + j;
    g_txt(g, row, 0, j == 0 ? "Intercept" : xv.v[j - 1].name);
    g_num(g, row, 1, bco[j]);
    if (j == 0 && zero) { for (c = 2; c <= 8; c++) g_err(g, row, c, E_NA); continue; }
    t = bco[j] / se[j];
    g_num(g, row, 2, se[j]); g_num(g, row, 3, t);
    g_num(g, row, 4, finite_(t) ? wf(b, "T.DIST.2T(%.17g,%.17g)", fabs(t), dfe) : NAN);
    g_num(g, row, 5, bco[j] - t95 * se[j]); g_num(g, row, 6, bco[j] + t95 * se[j]);
    g_num(g, row, 7, bco[j] - tc * se[j]); g_num(g, row, 8, bco[j] + tc * se[j]);
  }
  row = 16 + k + 1 + 3;
  if (p->residuals || p->stdres) {
    char pr[80];
    double sd = sqrt(sse / (n - 1.0));
    g_txt(g, row, 0, "RESIDUAL OUTPUT");
    snprintf(pr, sizeof pr, "Predicted %s", yv.v[0].name);
    g_txt(g, row + 2, 0, "Observation"); g_txt(g, row + 2, 1, pr); g_txt(g, row + 2, 2, "Residuals");
    if (p->stdres) g_txt(g, row + 2, 3, "Standard Residuals");
    for (i = 0; i < n; i++) {
      g_num(g, row + 3 + i, 0, i + 1); g_num(g, row + 3 + i, 1, pred[i]); g_num(g, row + 3 + i, 2, y[i] - pred[i]);
      if (p->stdres) g_num(g, row + 3 + i, 3, sd > 0 ? (y[i] - pred[i]) / sd : NAN);
    }
  }
  if (p->normprob) {
    int c0 = p->residuals || p->stdres ? (p->stdres ? 5 : 4) : 0;
    double *srt = (double *)malloc((size_t)n * sizeof(double));
    if (!srt) FAIL("Not enough memory.");
    memcpy(srt, y, (size_t)n * sizeof(double));
    qsort(srt, (size_t)n, sizeof(double), cmpd);
    g_txt(g, row, c0, "PROBABILITY OUTPUT");
    g_txt(g, row + 2, c0, "Percentile"); g_txt(g, row + 2, c0 + 1, yv.v[0].name);
    for (i = 0; i < n; i++) { g_num(g, row + 3 + i, c0, 100.0 * (i + 0.5) / n); g_num(g, row + 3 + i, c0 + 1, srt[i]); }
    free(srt);
  }
  ok = 1;
out:
  free(y);
  if (xs) for (j = 0; j < k; j++) free(xs[j]);
  free(xs); free(xm); free(sxx); free(inv); free(bco); free(se); free(pred);
  vars_free(&yv); vars_free(&xv);
  return ok;
}

/* the numbers of a single row or column */
static int series(Book *b, int sheet, const char *text, int labels, int skip, double **x, int *n, char *name, size_t cap, const char **why)
{
  Vars vs; Rect r; int sh, ok;
  if (!an_range(b, sheet, text, &sh, &r)) { *why = "The input range isn't valid."; return 0; }
  if (r.r1 != r.r2 && r.c1 != r.c2) { *why = "Input range must be a single row or column."; return 0; }
  if (!variables(b, sh, r, r.r1 == r.r2 && r.c1 != r.c2, labels, "Variable ", &vs)) { *why = "The input range is too large."; return 0; }
  if (name) snprintf(name, cap, "%s", vs.v[0].name);
  ok = numbers(&vs.v[0], skip, x, n);
  if (!ok) *why = "Input range contains non-numeric data.";
  vars_free(&vs);
  return ok;
}
/* the forecasts f (NAN: none, #N/A) and with standard errors their running RMS over w points */
static void forecast_grid(Grid *g, const double *x, const double *f, int n, int w, int stderrs)
{
  int i, j;
  for (i = 0; i < n; i++) {
    if (finite_(f[i])) g_num(g, i, 0, f[i]); else g_err(g, i, 0, E_NA);
    if (stderrs) {
      double acc = 0;
      int good = i + 1 >= w;
      for (j = i - w + 1; good && j <= i; j++) { if (j < 0 || !finite_(f[j])) good = 0; else acc += (x[j] - f[j]) * (x[j] - f[j]); }
      if (good) g_num(g, i, 1, sqrt(acc / w)); else g_err(g, i, 1, E_NA);
    }
  }
}
static int smoothing(Book *b, int sheet, const TPParams *p, int expo, Grid *g, const char **why)
{
  double *x = NULL, *f = NULL;
  int n, i, ok = 0;
  if (!series(b, sheet, p->in1, p->labels, 0, &x, &n, NULL, 0, why)) return 0;
  if (!(f = (double *)malloc(((size_t)n + 1) * sizeof(double)))) FAIL("Not enough memory.");
  if (!expo) {
    double acc = 0;
    int k = p->interval;
    if (k < 1 || k > n) FAIL("The interval must be a whole number from 1 to the number of data points.");
    for (i = 0; i < n; i++) { acc += x[i]; if (i >= k) acc -= x[i - k]; f[i] = i + 1 >= k ? acc / k : NAN; }
    forecast_grid(g, x, f, n, k, p->stderrs);
  } else {
    double d = p->damping;
    if (!(d >= 0 && d <= 1)) FAIL("The damping factor must be between 0 and 1.");
    /* F(1) = A(0); F(t) = (1 - damping) A(t-1) + damping F(t-1) */
    for (i = 0; i < n; i++) f[i] = i == 0 ? NAN : i == 1 ? x[0] : (1 - d) * x[i - 1] + d * f[i - 1];
    forecast_grid(g, x, f, n, 3, p->stderrs);
  }
  ok = 1;
out:
  free(x); free(f);
  return ok;
}

static int rank_percentile(Book *b, int sheet, const TPParams *p, Grid *g, const char **why)
{
  Vars vs; Rect r; int sh, i, ok = 0;
  if (!an_range(b, sheet, p->in1, &sh, &r)) { *why = "The input range isn't valid."; return 0; }
  if (!variables(b, sh, r, p->byrows, p->labels, "Column", &vs)) { *why = "The input range is too large."; return 0; }
  for (i = 0; i < vs.n; i++) {
    int c = i * 4, n = 0, k, q, *pt;
    double *val, *asc;
    static const char *hdr[4];
    hdr[0] = "Point"; hdr[1] = vs.v[i].name; hdr[2] = "Rank"; hdr[3] = "Percent";
    g_labels(g, 0, c, hdr, 4);
    pt = (int *)malloc(((size_t)vs.v[i].n + 1) * sizeof(int)); val = (double *)malloc(((size_t)vs.v[i].n + 1) * sizeof(double));
    asc = (double *)malloc(((size_t)vs.v[i].n + 1) * sizeof(double));
    if (!pt || !val || !asc) { free(pt); free(val); free(asc); FAIL("Not enough memory."); }
    for (k = 0; k < vs.v[i].n; k++) {
      const Value *v = vs.v[i].v[k];
      if (v->t == V_NUM) { pt[n] = k + 1; val[n] = v->n; asc[n] = v->n; n++; }
      else if (v->t != V_EMPTY) { free(pt); free(val); free(asc); FAIL("Input range contains non-numeric data."); }
    }
    qsort(asc, (size_t)n, sizeof(double), cmpd);
    /* largest first, earlier points first among equals */
    for (k = 1; k < n; k++)
      for (q = k; q > 0 && (val[q] > val[q - 1] || (val[q] == val[q - 1] && pt[q] < pt[q - 1])); q--) {
        double tv = val[q]; int tp = pt[q];
        val[q] = val[q - 1]; pt[q] = pt[q - 1]; val[q - 1] = tv; pt[q - 1] = tp;
      }
    for (k = 0; k < n; k++) {
      int below = 0, above = 0, m;
      double pr;
      for (m = 0; m < n; m++) { if (asc[m] < val[k]) below++; if (asc[m] > val[k]) above++; }
      g_num(g, k + 1, c, pt[k]); g_num(g, k + 1, c + 1, val[k]); g_num(g, k + 1, c + 2, above + 1);
      /* PERCENTRANK.INC: the share below, cut to three digits */
      pr = n <= 1 ? 1 : floor((double)below / (n - 1) * 1000 + 1e-9) / 1000;
      g_put(g, k + 1, c + 3, val_num(pr), 1);
    }
    free(pt); free(val); free(asc);
  }
  ok = 1;
out:
  vars_free(&vs);
  return ok;
}

/* ------------------------------------------------------------------ tests of means and variances */
static void pair_row(Grid *g, i32 row, const char *label, double a, double b2, int two)
{
  g_txt(g, row, 0, label); g_num(g, row, 1, a);
  if (two) g_num(g, row, 2, b2);
}
static int two_samples(Book *b, int sheet, const TPParams *p, int skip, double **x, int *n1, double **y, int *n2, char *na, char *nb, const char **why)
{
  if (!series(b, sheet, p->in1, p->labels, skip, x, n1, na, 64, why)) return 0;
  if (!series(b, sheet, p->in2, p->labels, skip, y, n2, nb, 64, why)) { free(*x); *x = NULL; return 0; }
  if (!p->labels) { strcpy(na, "Variable 1"); strcpy(nb, "Variable 2"); }
  if (*n1 < 2 || *n2 < 2) { *why = "Each input range needs at least two numbers."; free(*x); free(*y); *x = *y = NULL; return 0; }
  return 1;
}
static int t_test(Book *b, int sheet, const TPParams *p, Grid *g, const char **why)
{
  double *x = NULL, *y = NULL, m1, m2, v1, v2, t, df;
  int n1, n2, row = 3, paired = p->tool == TP_TPAIRED, i;
  char na[64], nb[64];
  if (!(p->alpha > 0 && p->alpha < 1)) { *why = "Alpha must be between 0 and 1."; return 0; }
  if (!two_samples(b, sheet, p, !paired, &x, &n1, &y, &n2, na, nb, why)) return 0;
  m1 = mean(x, n1); m2 = mean(y, n2); v1 = var_s(x, n1); v2 = var_s(y, n2);
  g_txt(g, 0, 0, toolpak_names[p->tool]);
  g_txt(g, 2, 1, na); g_txt(g, 2, 2, nb);
  pair_row(g, row++, "Mean", m1, m2, 1);
  pair_row(g, row++, "Variance", v1, v2, 1);
  pair_row(g, row++, "Observations", n1, n2, 1);
  if (paired) {
    double *d;
    if (n1 != n2) { free(x); free(y); *why = "Input ranges must have the same number of values."; return 0; }
    if (!(d = (double *)malloc((size_t)n1 * sizeof(double)))) { free(x); free(y); *why = "Not enough memory."; return 0; }
    pair_row(g, row++, "Pearson Correlation", correl(x, y, n1), 0, 0);
    for (i = 0; i < n1; i++) d[i] = x[i] - y[i];
    t = (mean(d, n1) - p->hmd) / (sqrt(var_s(d, n1)) / sqrt((double)n1));
    df = n1 - 1;
    free(d);
  } else if (p->tool == TP_TEQUAL) {
    double pooled = ((n1 - 1) * v1 + (n2 - 1) * v2) / (n1 + n2 - 2.0);
    pair_row(g, row++, "Pooled Variance", pooled, 0, 0);
    t = (m1 - m2 - p->hmd) / sqrt(pooled * (1.0 / n1 + 1.0 / n2));
    df = n1 + n2 - 2;
  } else {
    double q1 = v1 / n1, q2 = v2 / n2;
    /* Welch-Satterthwaite degrees of freedom, rounded to a whole number as Excel does */
    df = floor((q1 + q2) * (q1 + q2) / (q1 * q1 / (n1 - 1) + q2 * q2 / (n2 - 1)) + 0.5);
    t = (m1 - m2 - p->hmd) / sqrt(q1 + q2);
  }
  pair_row(g, row++, "Hypothesized Mean Difference", p->hmd, 0, 0);
  pair_row(g, row++, "df", df, 0, 0);
  pair_row(g, row++, "t Stat", t, 0, 0);
  pair_row(g, row++, "P(T<=t) one-tail", finite_(t) ? wf(b, "T.DIST.RT(%.17g,%.17g)", fabs(t), df) : NAN, 0, 0);
  pair_row(g, row++, "t Critical one-tail", wf(b, "T.INV(%.17g,%.17g)", 1 - p->alpha, df), 0, 0);
  pair_row(g, row++, "P(T<=t) two-tail", finite_(t) ? wf(b, "T.DIST.2T(%.17g,%.17g)", fabs(t), df) : NAN, 0, 0);
  pair_row(g, row++, "t Critical two-tail", wf(b, "T.INV.2T(%.17g,%.17g)", p->alpha, df), 0, 0);
  free(x); free(y);
  return 1;
}
static int z_test(Book *b, int sheet, const TPParams *p, Grid *g, const char **why)
{
  double *x = NULL, *y = NULL, m1, m2, z, nz;
  int n1, n2, row = 3;
  char na[64], nb[64];
  if (!(p->alpha > 0 && p->alpha < 1)) { *why = "Alpha must be between 0 and 1."; return 0; }
  if (!(p->var1 > 0) || !(p->var2 > 0)) { *why = "The known variances must be positive numbers."; return 0; }
  if (!two_samples(b, sheet, p, 1, &x, &n1, &y, &n2, na, nb, why)) return 0;
  m1 = mean(x, n1); m2 = mean(y, n2);
  z = (m1 - m2 - p->hmd) / sqrt(p->var1 / n1 + p->var2 / n2);
  nz = wf(b, "NORM.S.DIST(%.17g,TRUE)", fabs(z));
  g_txt(g, 0, 0, toolpak_names[TP_ZTEST]);
  g_txt(g, 2, 1, na); g_txt(g, 2, 2, nb);
  pair_row(g, row++, "Mean", m1, m2, 1);
  pair_row(g, row++, "Known Variance", p->var1, p->var2, 1);
  pair_row(g, row++, "Observations", n1, n2, 1);
  pair_row(g, row++, "Hypothesized Mean Difference", p->hmd, 0, 0);
  pair_row(g, row++, "z", z, 0, 0);
  pair_row(g, row++, "P(Z<=z) one-tail", 1 - nz, 0, 0);
  pair_row(g, row++, "z Critical one-tail", wf(b, "NORM.S.INV(%.17g)", 1 - p->alpha), 0, 0);
  pair_row(g, row++, "P(Z<=z) two-tail", 2 * (1 - nz), 0, 0);
  pair_row(g, row++, "z Critical two-tail", wf(b, "NORM.S.INV(%.17g)", 1 - p->alpha / 2), 0, 0);
  free(x); free(y);
  return 1;
}
static int f_test(Book *b, int sheet, const TPParams *p, Grid *g, const char **why)
{
  double *x = NULL, *y = NULL, v1, v2, f, d1, d2;
  int n1, n2, row = 3;
  char na[64], nb[64];
  if (!(p->alpha > 0 && p->alpha < 1)) { *why = "Alpha must be between 0 and 1."; return 0; }
  if (!two_samples(b, sheet, p, 1, &x, &n1, &y, &n2, na, nb, why)) return 0;
  v1 = var_s(x, n1); v2 = var_s(y, n2); d1 = n1 - 1; d2 = n2 - 1; f = v1 / v2;
  g_txt(g, 0, 0, toolpak_names[TP_FTEST]);
  g_txt(g, 2, 1, na); g_txt(g, 2, 2, nb);
  pair_row(g, row++, "Mean", mean(x, n1), mean(y, n2), 1);
  pair_row(g, row++, "Variance", v1, v2, 1);
  pair_row(g, row++, "Observations", n1, n2, 1);
  pair_row(g, row++, "df", d1, d2, 1);
  pair_row(g, row++, "F", f, 0, 0);
  /* the tail on F's side: the left when F < 1 */
  if (!finite_(f)) { pair_row(g, row++, "P(F<=f) one-tail", NAN, 0, 0); pair_row(g, row++, "F Critical one-tail", NAN, 0, 0); }
  else if (f < 1) {
    pair_row(g, row++, "P(F<=f) one-tail", wf(b, "F.DIST(%.17g,%.17g,%.17g,TRUE)", f, d1, d2), 0, 0);
    pair_row(g, row++, "F Critical one-tail", wf(b, "F.INV(%.17g,%.17g,%.17g)", p->alpha, d1, d2), 0, 0);
  } else {
    pair_row(g, row++, "P(F<=f) one-tail", wf(b, "F.DIST.RT(%.17g,%.17g,%.17g)", f, d1, d2), 0, 0);
    pair_row(g, row++, "F Critical one-tail", wf(b, "F.INV.RT(%.17g,%.17g,%.17g)", p->alpha, d1, d2), 0, 0);
  }
  free(x); free(y);
  return 1;
}
/* one ANOVA table row: SS, df, MS, and F, its P-value and F crit when den_df > 0 */
static void anova_row(Book *b, Grid *g, i32 row, const char *label, double ss, double df, double msden, double dfden, double alpha)
{
  double ms = ss / df, f = ms / msden;
  g_txt(g, row, 0, label); g_num(g, row, 1, ss); g_num(g, row, 2, df); g_num(g, row, 3, ms);
  if (dfden > 0) {
    g_num(g, row, 4, f);
    g_num(g, row, 5, finite_(f) ? wf(b, "F.DIST.RT(%.17g,%.17g,%.17g)", f, df, dfden) : NAN);
    g_num(g, row, 6, wf(b, "F.INV.RT(%.17g,%.17g,%.17g)", alpha, df, dfden));
  }
}
static const char *const anova_head[] = { "Source of Variation", "SS", "df", "MS", "F", "P-value", "F crit" };
static const char *const summary_head[] = { "Count", "Sum", "Average", "Variance" };
static void summary_row(Grid *g, i32 row, i32 c, const char *name, const double *x, int n)
{
  if (name) g_txt(g, row, c - 1, name);
  g_num(g, row, c, n); g_num(g, row, c + 1, sum(x, n)); g_num(g, row, c + 2, mean(x, n)); g_num(g, row, c + 3, var_s(x, n));
}
static int anova_single(Book *b, int sheet, const TPParams *p, Grid *g, const char **why)
{
  Vars vs; Rect r; int sh, i, ok = 0, total = 0, row;
  double ssb = 0, ssw = 0, grand = 0, **x = NULL;
  int *n = NULL;
  if (!(p->alpha > 0 && p->alpha < 1)) { *why = "Alpha must be between 0 and 1."; return 0; }
  if (!an_range(b, sheet, p->in1, &sh, &r)) { *why = "The input range isn't valid."; return 0; }
  if (!variables(b, sh, r, p->byrows, p->labels, p->byrows ? "Row " : "Column ", &vs)) { *why = "The input range is too large."; return 0; }
  x = (double **)calloc((size_t)vs.n, sizeof(double *)); n = (int *)calloc((size_t)vs.n, sizeof(int));
  if (!x || !n) FAIL("Not enough memory.");
  for (i = 0; i < vs.n; i++) {
    if (!numbers(&vs.v[i], 1, &x[i], &n[i])) FAIL("Input range contains non-numeric data.");
    if (!n[i]) FAIL("Anova needs at least two groups, each with at least one number.");
    total += n[i]; grand += sum(x[i], n[i]);
  }
  if (vs.n < 2) FAIL("Anova needs at least two groups, each with at least one number.");
  grand /= total;
  g_txt(g, 0, 0, "Anova: Single Factor");
  g_txt(g, 2, 0, "SUMMARY");
  g_txt(g, 3, 0, "Groups"); g_labels(g, 3, 1, summary_head, 4);
  for (i = 0; i < vs.n; i++) {
    double m = mean(x[i], n[i]);
    summary_row(g, 4 + i, 1, vs.v[i].name, x[i], n[i]);
    ssb += n[i] * (m - grand) * (m - grand);
    ssw += ssq(x[i], n[i]);
  }
  row = 4 + vs.n + 2;
  g_txt(g, row, 0, "ANOVA");
  g_labels(g, row + 1, 0, anova_head, 7);
  anova_row(b, g, row + 2, "Between Groups", ssb, vs.n - 1, ssw / (total - vs.n), total - vs.n, p->alpha);
  anova_row(b, g, row + 3, "Within Groups", ssw, total - vs.n, 0, 0, p->alpha);
  g_txt(g, row + 5, 0, "Total"); g_num(g, row + 5, 1, ssb + ssw); g_num(g, row + 5, 2, total - 1);
  ok = 1;
out:
  if (x) for (i = 0; i < vs.n; i++) free(x[i]);
  free(x); free(n);
  vars_free(&vs);
  return ok;
}
/* the numbers of a block of the range (every cell a number) */
static int block(Book *b, int sh, i32 r1, i32 c1, i32 r2, i32 c2, double *out)
{
  i32 r, c;
  int n = 0;
  for (r = r1; r <= r2; r++)
    for (c = c1; c <= c2; c++) {
      const Value *v = cell_value(b, sh, r, c);
      if (v->t != V_NUM) return -1;
      out[n++] = v->n;
    }
  return n;
}
static int anova_two(Book *b, int sheet, const TPParams *p, Grid *g, const char **why)
{
  Rect r; int sh, ok = 0, rows, cols, i, j, row;
  double *x = NULL, *buf = NULL, grand, sst = 0, ssr = 0, ssc = 0;
  char nm[64];
  if (!(p->alpha > 0 && p->alpha < 1)) { *why = "Alpha must be between 0 and 1."; return 0; }
  if (!an_range(b, sheet, p->in1, &sh, &r)) { *why = "The input range isn't valid."; return 0; }
  if (p->labels) { r.r1++; r.c1++; }
  rows = r.r2 - r.r1 + 1; cols = r.c2 - r.c1 + 1;
  if (rows < 2 || cols < 2) { *why = "The input range needs at least two rows and two columns of data."; return 0; }
  if ((long)rows * cols > MAX_INPUT) { *why = "The input range is too large."; return 0; }
  x = (double *)malloc((size_t)rows * cols * sizeof(double)); buf = (double *)malloc(((size_t)(rows > cols ? rows : cols) + 1) * sizeof(double));
  if (!x || !buf) FAIL("Not enough memory.");
  if (block(b, sh, r.r1, r.c1, r.r2, r.c2, x) < 0) FAIL("Input range contains non-numeric data.");
  grand = mean(x, rows * cols);
  for (i = 0; i < rows * cols; i++) sst += (x[i] - grand) * (x[i] - grand);
  g_txt(g, 0, 0, "Anova: Two-Factor Without Replication");
  g_txt(g, 2, 0, "SUMMARY"); g_labels(g, 2, 1, summary_head, 4);
  for (i = 0; i < rows; i++) {
    double m;
    for (j = 0; j < cols; j++) buf[j] = x[i * cols + j];
    m = mean(buf, cols); ssr += cols * (m - grand) * (m - grand);
    if (p->labels) value_text(b, cell_value(b, sh, r.r1 + i, r.c1 - 1), nm, sizeof nm); else nm[0] = 0;
    if (!nm[0]) snprintf(nm, sizeof nm, "Row %d", i + 1);
    summary_row(g, 3 + i, 1, nm, buf, cols);
  }
  for (j = 0; j < cols; j++) {
    double m;
    for (i = 0; i < rows; i++) buf[i] = x[i * cols + j];
    m = mean(buf, rows); ssc += rows * (m - grand) * (m - grand);
    if (p->labels) value_text(b, cell_value(b, sh, r.r1 - 1, r.c1 + j), nm, sizeof nm); else nm[0] = 0;
    if (!nm[0]) snprintf(nm, sizeof nm, "Column %d", j + 1);
    summary_row(g, 4 + rows + j, 1, nm, buf, rows);
  }
  row = 4 + rows + cols + 2;
  {
    double sse = sst - ssr - ssc, dfe = (rows - 1.0) * (cols - 1), mse = sse / dfe;
    g_txt(g, row, 0, "ANOVA");
    g_labels(g, row + 1, 0, anova_head, 7);
    anova_row(b, g, row + 2, "Rows", ssr, rows - 1, mse, dfe, p->alpha);
    anova_row(b, g, row + 3, "Columns", ssc, cols - 1, mse, dfe, p->alpha);
    anova_row(b, g, row + 4, "Error", sse, dfe, 0, 0, p->alpha);
    g_txt(g, row + 6, 0, "Total"); g_num(g, row + 6, 1, sst); g_num(g, row + 6, 2, rows * cols - 1.0);
  }
  ok = 1;
out:
  free(x); free(buf);
  return ok;
}
static int anova_rep(Book *b, int sheet, const TPParams *p, Grid *g, const char **why)
{
  Rect r; int sh, ok = 0, R = p->rowsper, a, bc, i, j, k, row, N;
  double *x = NULL, *buf = NULL, grand, ssa = 0, ssb = 0, sscell = 0, ssw = 0, sst = 0;
  char nm[64];
  if (!(p->alpha > 0 && p->alpha < 1)) { *why = "Alpha must be between 0 and 1."; return 0; }
  if (!an_range(b, sheet, p->in1, &sh, &r)) { *why = "The input range isn't valid."; return 0; }
  /* the range takes in the column labels' row and the sample labels' column */
  if (R < 2) { *why = "Rows per sample must be at least 2."; return 0; }
  if ((r.r2 - r.r1) % R || r.r2 - r.r1 < 2 * R || r.c2 - r.c1 < 2) { *why = "The input range must hold labels, at least two columns and two samples, each of Rows per sample rows."; return 0; }
  a = (r.r2 - r.r1) / R; bc = r.c2 - r.c1; N = a * bc * R;
  if ((long)N > MAX_INPUT) { *why = "The input range is too large."; return 0; }
  x = (double *)malloc((size_t)N * sizeof(double)); buf = (double *)malloc(((size_t)N + 1) * sizeof(double));
  if (!x || !buf) FAIL("Not enough memory.");
  /* x[(i * R + q) * bc + j]: sample i, its row q, column j */
  if (block(b, sh, r.r1 + 1, r.c1 + 1, r.r2, r.c2, x) < 0) FAIL("Input range contains non-numeric data.");
  grand = mean(x, N);
  for (k = 0; k < N; k++) sst += (x[k] - grand) * (x[k] - grand);
  g_txt(g, 0, 0, "Anova: Two-Factor With Replication");
  g_txt(g, 2, 0, "SUMMARY");
  for (j = 0; j < bc; j++) { value_text(b, cell_value(b, sh, r.r1, r.c1 + 1 + j), nm, sizeof nm); if (!nm[0]) snprintf(nm, sizeof nm, "Column %d", j + 1); g_txt(g, 2, 1 + j, nm); }
  g_txt(g, 2, 1 + bc, "Total");
  row = 3;
  for (i = 0; i <= a; i++) {
    int stat;
    double *cnt;
    if (i < a) { value_text(b, cell_value(b, sh, r.r1 + 1 + i * R, r.c1), nm, sizeof nm); if (!nm[0]) snprintf(nm, sizeof nm, "Sample %d", i + 1); }
    else strcpy(nm, "Total");
    g_txt(g, row++, 0, nm);
    for (stat = 0; stat < 4; stat++) g_txt(g, row + stat, 0, summary_head[stat]);
    for (j = 0; j <= bc; j++) {
      int n = 0, q, ii;
      for (ii = (i < a ? i : 0); ii < (i < a ? i + 1 : a); ii++)
        for (q = 0; q < R; q++)
          for (k = (j < bc ? j : 0); k < (j < bc ? j + 1 : bc); k++) buf[n++] = x[(ii * R + q) * bc + k];
      cnt = buf;
      g_num(g, row, 1 + j, n); g_num(g, row + 1, 1 + j, sum(cnt, n)); g_num(g, row + 2, 1 + j, mean(cnt, n)); g_num(g, row + 3, 1 + j, var_s(cnt, n));
      if (i < a && j < bc) { double m = mean(cnt, n); sscell += R * (m - grand) * (m - grand); ssw += ssq(cnt, n); }
      if (i < a && j == bc) { double m = mean(cnt, n); ssa += n * (m - grand) * (m - grand); }
      if (i == a && j < bc) { double m = mean(cnt, n); ssb += n * (m - grand) * (m - grand); }
    }
    row += 5;
  }
  row += 1;
  {
    double dfa = a - 1, dfb = bc - 1, dfab = (a - 1.0) * (bc - 1), dfw = (double)a * bc * (R - 1), msw = ssw / dfw;
    g_txt(g, row, 0, "ANOVA");
    g_labels(g, row + 1, 0, anova_head, 7);
    anova_row(b, g, row + 2, "Sample", ssa, dfa, msw, dfw, p->alpha);
    anova_row(b, g, row + 3, "Columns", ssb, dfb, msw, dfw, p->alpha);
    anova_row(b, g, row + 4, "Interaction", sscell - ssa - ssb, dfab, msw, dfw, p->alpha);
    anova_row(b, g, row + 5, "Within", ssw, dfw, 0, 0, p->alpha);
    g_txt(g, row + 7, 0, "Total"); g_num(g, row + 7, 1, sst); g_num(g, row + 7, 2, N - 1.0);
  }
  ok = 1;
out:
  free(x); free(buf);
  return ok;
}

/* ------------------------------------------------------------------ Fourier Analysis */
/* a complex number written as COMPLEX() writes it ("3", "1+2i", "-i") */
static void complex_text(double re, double im, char *out)
{
  char a[40], c[40];
  if (fabs(re) < 1e-300) re = 0;
  if (fabs(im) < 1e-300) im = 0;
  num_general(re, a, 15);
  if (im == 0) { strcpy(out, a); return; }
  if (im == 1) strcpy(c, "");
  else if (im == -1) strcpy(c, "-");
  else num_general(im, c, 15);
  if (re == 0) sprintf(out, "%si", c);
  else sprintf(out, "%s%s%si", a, im > 0 ? "+" : "", c);
}
/* "1+2i" read back (or a number); 0 if it isn't one */
static int complex_parse(const char *s, double *re, double *im)
{
  char *e;
  double a;
  *re = *im = 0;
  if (!*s) return 0;
  if ((s[0] == 'i' || s[0] == 'j') && !s[1]) { *im = 1; return 1; }
  a = strtod(s, &e);
  if (e == s && (*s == '+' || *s == '-') && (e[1] == 'i' || e[1] == 'j') && !e[2]) { *im = *s == '-' ? -1 : 1; return 1; }
  if (e == s) return 0;
  if (!*e) { *re = a; return 1; }
  if ((*e == 'i' || *e == 'j') && !e[1]) { *im = a; return 1; }
  *re = a;
  s = e;
  if ((*s == '+' || *s == '-') && (s[1] == 'i' || s[1] == 'j') && !s[2]) { *im = *s == '-' ? -1 : 1; return 1; }
  a = strtod(s, &e);
  if (e == s || (*e != 'i' && *e != 'j') || e[1]) return 0;
  *im = a;
  return 1;
}
static int fourier(Book *b, int sheet, const TPParams *p, Grid *g, const char **why)
{
  Vars vs; Rect r; int sh, n, i, j, k, len, ok = 0;
  double *re = NULL, *im = NULL;
  if (!an_range(b, sheet, p->in1, &sh, &r)) { *why = "The input range isn't valid."; return 0; }
  if (r.r1 != r.r2 && r.c1 != r.c2) { *why = "Input range must be a single row or column."; return 0; }
  if (!variables(b, sh, r, r.r1 == r.r2 && r.c1 != r.c2, p->labels, "", &vs)) { *why = "The input range is too large."; return 0; }
  n = vs.v[0].n;
  for (k = 1; k < n; k <<= 1) ;
  if (n < 2 || k != n || n > 4096) FAIL("The input range must hold 2, 4, 8 ... 4096 values (a power of 2).");
  re = (double *)malloc((size_t)n * sizeof(double)); im = (double *)malloc((size_t)n * sizeof(double));
  if (!re || !im) FAIL("Not enough memory.");
  for (i = 0; i < n; i++) {
    const Value *v = vs.v[0].v[i];
    if (v->t == V_NUM) { re[i] = v->n; im[i] = 0; }
    else if (v->t == V_STR && complex_parse(v->s, &re[i], &im[i])) ;
    else FAIL("The input range must hold numbers (or complex numbers as text).");
  }
  /* the transform in place: bits reversed, then butterflies */
  for (i = 1, j = 0; i < n; i++) {
    int bit = n >> 1;
    for (; j & bit; bit >>= 1) j ^= bit;
    j ^= bit;
    if (i < j) { double t = re[i]; re[i] = re[j]; re[j] = t; t = im[i]; im[i] = im[j]; im[j] = t; }
  }
  for (len = 2; len <= n; len <<= 1) {
    double ang = (p->inverse ? 2 : -2) * 3.141592653589793 / len;
    for (i = 0; i < n; i += len)
      for (j = 0; j < len / 2; j++) {
        double wr = cos(ang * j), wi = sin(ang * j);
        int u = i + j, v = i + j + len / 2;
        double xr = re[v] * wr - im[v] * wi, xi = re[v] * wi + im[v] * wr;
        re[v] = re[u] - xr; im[v] = im[u] - xi;
        re[u] += xr; im[u] += xi;
      }
  }
  k = 0;
  if (p->labels) g_txt(g, k++, 0, vs.v[0].name);
  for (i = 0; i < n; i++) {
    char t[100];
    double a = re[i], c = im[i];
    if (p->inverse) { a /= n; c /= n; }
    /* (rounding left in a part that is exactly zero) */
    if (fabs(a) < 1e-12 * (fabs(c) + 1e-300) || fabs(a) < 1e-14) a = fabs(a) < 1e-14 ? 0 : a;
    if (fabs(c) < 1e-14) c = 0;
    complex_text(a, c, t);
    g_txt(g, k++, 0, t);
  }
  ok = 1;
out:
  free(re); free(im);
  vars_free(&vs);
  return ok;
}

/* ------------------------------------------------------------------ random numbers and sampling */
extern double ledger_now(void);
typedef struct { u32 s; } Rng;
static u32 rng_next(Rng *g) { u32 x = g->s; x ^= x << 13; x ^= x >> 17; x ^= x << 5; return g->s = x; }
static double rng_u(Rng *g) { u32 a = rng_next(g) >> 5, c = rng_next(g) >> 6; return (a * 67108864.0 + c) / 9007199254740992.0; }
static void rng_seed(Rng *g, u32 seed)
{
  if (!seed) seed = (u32)(fmod(ledger_now(), 1.0) * 4294967295.0) ^ 0x9E3779B9u;
  g->s = seed ? seed : 1;
  rng_next(g); rng_next(g);
}
static double rng_gauss(Rng *g)
{
  double u = rng_u(g), v = rng_u(g);
  if (u < 1e-300) u = 1e-300;
  return sqrt(-2 * log(u)) * cos(6.283185307179586 * v);
}
static double rng_poisson(Rng *g, double lam)
{
  if (lam > 500) { double x = floor(lam + sqrt(lam) * rng_gauss(g) + 0.5); return x < 0 ? 0 : x; }
  {
    double l = exp(-lam), pr = 1;
    int k = 0;
    do { k++; pr *= rng_u(g); } while (pr > l);
    return k - 1;
  }
}
static int random_numbers(Book *b, int sheet, const TPParams *p, Grid *g, const char **why)
{
  Rng rg;
  int i, j, ok = 0, nv = p->nvars < 1 ? 1 : p->nvars;
  double *vals = NULL, *probs = NULL;
  int nd = 0;
  rng_seed(&rg, p->seed);
  if (p->dist == TPD_PATTERNED) {
    /* from p1 to p2 in steps of p3, each number p4 times, the sequence p5 times; in each column */
    long rep = (long)p->p4, seq = (long)p->p5, k, q, row = 0;
    double x;
    if (!(p->p3 > 0) || rep < 1 || seq < 1 || p->p2 < p->p1) { *why = "Patterned needs From <= To, a positive step and repeats of at least 1."; return 0; }
    if ((p->p2 - p->p1) / p->p3 * rep * seq > 1000000) { *why = "Too many numbers (1,000,000 at most)."; return 0; }
    for (q = 0; q < seq; q++)
      for (x = p->p1; x <= p->p2 + p->p3 * 1e-9; x += p->p3)
        for (k = 0; k < rep; k++, row++)
          for (j = 0; j < nv; j++) g_num(g, row, j, x);
    return 1;
  }
  if (p->nnums < 1 || (long)p->nnums * nv > 1000000) { *why = "The number of random numbers must be from 1 to 1,000,000 in all."; return 0; }
  if (p->dist == TPD_DISCRETE) {
    Rect r; int sh;
    double tot = 0;
    i32 y;
    if (!an_range(b, sheet, p->in2, &sh, &r) || r.c2 - r.c1 != 1) { *why = "The Value and Probability Input Range must be two columns."; return 0; }
    vals = (double *)malloc(((size_t)(r.r2 - r.r1) + 2) * sizeof(double)); probs = (double *)malloc(((size_t)(r.r2 - r.r1) + 2) * sizeof(double));
    if (!vals || !probs) FAIL("Not enough memory.");
    for (y = r.r1; y <= r.r2; y++) {
      const Value *v = cell_value(b, sh, y, r.c1), *w = cell_value(b, sh, y, r.c2);
      if (v->t != V_NUM || w->t != V_NUM || w->n < 0) FAIL("The Value and Probability Input Range must hold numbers and probabilities.");
      vals[nd] = v->n; probs[nd++] = w->n; tot += w->n;
    }
    if (fabs(tot - 1) > 1e-9) FAIL("The probabilities must add up to 1.");
  }
  if ((p->dist == TPD_BERNOULLI || p->dist == TPD_BINOMIAL) && !(p->p1 >= 0 && p->p1 <= 1)) FAIL("The p value must be between 0 and 1.");
  if (p->dist == TPD_BINOMIAL && !(p->p2 >= 0 && p->p2 <= 100000)) FAIL("The number of trials must be from 0 to 100,000.");
  if (p->dist == TPD_POISSON && !(p->p1 > 0)) FAIL("Lambda must be positive.");
  if (p->dist == TPD_NORMAL && !(p->p2 >= 0)) FAIL("The standard deviation can't be negative.");
  for (i = 0; i < p->nnums; i++)
    for (j = 0; j < nv; j++) {
      double x = 0;
      switch (p->dist) {
      case TPD_UNIFORM: x = p->p1 + (p->p2 - p->p1) * rng_u(&rg); break;
      case TPD_NORMAL: x = p->p1 + p->p2 * rng_gauss(&rg); break;
      case TPD_BERNOULLI: x = rng_u(&rg) < p->p1; break;
      case TPD_BINOMIAL: { long t, n = (long)p->p2; for (t = 0; t < n; t++) x += rng_u(&rg) < p->p1; break; }
      case TPD_POISSON: x = rng_poisson(&rg, p->p1); break;
      case TPD_DISCRETE: { double u = rng_u(&rg), acc = 0; int q; x = vals[nd - 1]; for (q = 0; q < nd; q++) { acc += probs[q]; if (u < acc) { x = vals[q]; break; } } break; }
      }
      g_num(g, i, j, x);
    }
  ok = 1;
out:
  free(vals); free(probs);
  return ok;
}
static int sampling(Book *b, int sheet, const TPParams *p, Grid *g, const char **why)
{
  Rect r; int sh, n = 0, i, ok = 0;
  double *x = NULL;
  i32 y, c;
  Rng rg;
  if (!an_range(b, sheet, p->in1, &sh, &r)) { *why = "The input range isn't valid."; return 0; }
  if ((long)(r.r2 - r.r1 + 1) * (r.c2 - r.c1 + 1) > MAX_INPUT) { *why = "The input range is too large."; return 0; }
  if (!(x = (double *)malloc(((size_t)(r.r2 - r.r1 + 1) * (r.c2 - r.c1 + 1) + 1) * sizeof(double)))) FAIL("Not enough memory.");
  /* column by column, as Excel reads the range */
  for (c = r.c1; c <= r.c2; c++)
    for (y = r.r1; y <= r.r2; y++) {
      const Value *v = cell_value(b, sh, y, c);
      if (p->labels && y == r.r1) continue;
      if (v->t == V_NUM) x[n++] = v->n;
      else if (v->t != V_EMPTY) FAIL("Input range contains non-numeric data.");
    }
  if (p->nsample < 1) FAIL("The period or number of samples must be at least 1.");
  if (p->periodic) {
    if (p->nsample > n) FAIL("The period can't be larger than the input range.");
    for (i = p->nsample - 1; i < n; i += p->nsample) g_num(g, i / p->nsample, 0, x[i]);
  } else {
    if (!n) FAIL("The input range holds no numbers.");
    if (p->nsample > 1000000) FAIL("Too many samples.");
    rng_seed(&rg, p->seed);
    for (i = 0; i < p->nsample; i++) { int k = (int)(rng_u(&rg) * n); if (k >= n) k = n - 1; g_num(g, i, 0, x[k]); }
  }
  ok = 1;
out:
  free(x);
  return ok;
}

/* ------------------------------------------------------------------ writing the report */
static void pct_edit(Style *st, Font *f, const void *arg) { (void)f; st->numfmt = (u16)*(const int *)arg; }
int toolpak_run(Book *b, int sheet, const TPParams *p, Rect *where, const char **why)
{
  Grid g;
  int ok = 0, osh = sheet, i, manual = b->manualcalc, isnew = 0;
  i32 rows = 0, cols = 0, r0 = 0, c0 = 0;
  Rect area;
  *why = NULL;
  memset(&g, 0, sizeof g);
  if (sheet < 0 || sheet >= b->nsh) { *why = "No sheet."; return -1; }
  switch (p->tool) {
  case TP_ANOVA1: ok = anova_single(b, sheet, p, &g, why); break;
  case TP_ANOVA2R: ok = anova_rep(b, sheet, p, &g, why); break;
  case TP_ANOVA2: ok = anova_two(b, sheet, p, &g, why); break;
  case TP_CORREL: ok = matrix(b, sheet, p, 0, &g, why); break;
  case TP_COVAR: ok = matrix(b, sheet, p, 1, &g, why); break;
  case TP_DESCR: ok = descriptive(b, sheet, p, &g, why); break;
  case TP_EXPSMOOTH: ok = smoothing(b, sheet, p, 1, &g, why); break;
  case TP_FTEST: ok = f_test(b, sheet, p, &g, why); break;
  case TP_FOURIER: ok = fourier(b, sheet, p, &g, why); break;
  case TP_HISTOGRAM: ok = histogram(b, sheet, p, &g, why); break;
  case TP_MOVAVG: ok = smoothing(b, sheet, p, 0, &g, why); break;
  case TP_RANDOM: ok = random_numbers(b, sheet, p, &g, why); break;
  case TP_RANK: ok = rank_percentile(b, sheet, p, &g, why); break;
  case TP_REGRESSION: ok = regression(b, sheet, p, &g, why); break;
  case TP_SAMPLING: ok = sampling(b, sheet, p, &g, why); break;
  case TP_TPAIRED: case TP_TEQUAL: case TP_TUNEQUAL: ok = t_test(b, sheet, p, &g, why); break;
  case TP_ZTEST: ok = z_test(b, sheet, p, &g, why); break;
  default: *why = "Unknown tool.";
  }
  if (g.oom) { ok = 0; *why = "Not enough memory."; }
  if (!ok) { g_free(&g); if (!*why) *why = "The tool couldn't make its report."; return -1; }
  for (i = 0; i < g.n; i++) { if (g.c[i].r + 1 > rows) rows = g.c[i].r + 1; if (g.c[i].c + 1 > cols) cols = g.c[i].c + 1; }
  if (rows < 1) rows = 1;
  if (cols < 1) cols = 1;
  if (!p->outnew) {
    Rect o;
    if (!an_range(b, sheet, p->out, &osh, &o)) { g_free(&g); *why = "The output range isn't valid."; return -1; }
    r0 = o.r1; c0 = o.c1;
    if (r0 + rows > MAXROW || c0 + cols > MAXCOL) { g_free(&g); *why = "The report doesn't fit on the sheet below and right of the output cell."; return -1; }
    area.r1 = r0; area.c1 = c0; area.r2 = r0 + rows - 1; area.c2 = c0 + cols - 1;
    if (!p->overwrite) {
      Sheet *s = b->sh[osh];
      i32 y;
      for (y = area.r1; y <= area.r2 && y < s->nrows; y++) {
        Row *row = s->rows[y];
        int k;
        if (!row) continue;
        for (k = 0; k < row->n; k++)
          if (row->c[k].col >= area.c1 && row->c[k].col <= area.c2 && (row->c[k].f || row->c[k].v.t != V_EMPTY)) { g_free(&g); *where = area; return -2; }
      }
    }
  } else {
    if (b->lockstruct) { g_free(&g); *why = "The workbook is protected and cannot be changed."; return -1; }
    if (p->newsheet[0] && book_find_sheet(b, p->newsheet) >= 0) { g_free(&g); *why = "A sheet with that name already exists."; return -1; }
    area.r1 = area.c1 = 0; area.r2 = rows - 1; area.c2 = cols - 1;
  }
  b->manualcalc = 1;
  book_begin(b);
  if (p->outnew) {
    book_note_book(b);
    if ((osh = an_add_sheet(b, p->newsheet[0] ? p->newsheet : NULL, sheet)) < 0) {
      book_end(b); b->manualcalc = manual; g_free(&g); *why = "The new sheet's name isn't valid."; return -1;
    }
    isnew = 1;
  } else book_clear(b, osh, area, CLR_CONTENTS);
  for (i = 0; i < g.n; i++) { book_set_value(b, osh, r0 + g.c[i].r, c0 + g.c[i].c, &g.c[i].v); }
  {
    int pf = book_fmt(b, "0.00%"), any = 0;
    for (i = 0; i < g.n; i++)
      if (g.c[i].pct) { Rect one; one.r1 = one.r2 = r0 + g.c[i].r; one.c1 = one.c2 = c0 + g.c[i].c; book_restyle(b, osh, one, pct_edit, &pf); any = 1; }
    (void)any;
  }
  if (isnew) {
    i32 c;
    for (c = 0; c < cols && c < 12; c++) book_set_col_width(b, osh, c, 100);
  }
  book_end(b);
  b->manualcalc = manual;
  if (!manual) book_recalc(b);
  if (isnew) b->active = osh;
  g_free(&g);
  *where = area;
  return osh;
}
