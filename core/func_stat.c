/*
 * func_stat.c - the statistical worksheet functions (distributions, tests, regression, the
 * FORECAST.ETS family) and the compatibility names Excel keeps for the old ones (NORMDIST, TDIST,
 * CHIDIST, ...). See func.c for the conventions. Its table, func_stat_defs, is in name order and
 * merged with the others by func_init. One line per function for the Insert Function dialog is in
 * docs/functions-stat.txt.
 *
 * The special functions (log-gamma, the regularized incomplete gamma and beta functions, the normal
 * distribution and its inverse) are written here from their textbook definitions, using only what
 * every C library has (exp, log, pow, sqrt, sin): vbcc's 68040 maths library has no lgamma or erf.
 * Each is good to about 1e-14 relative, so results agree with Excel's to the 15 digits shown.
 */
#include "calc.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif
#define SQRT2PI 2.50662827463100050242
#define LN_SQRT2PI 0.91893853320467274178
#define LN_SQRTPI 0.57236494292470008707
#define TINY 1e-300
#define BIG 1.7976931348623157e308
static double nan_(void) { volatile double z = 0; return z / z; }
static double inf_(void) { volatile double b = BIG; return b * 10; }

/* ================================================================== special functions */
static double lg1p(double x)                  /* log(1 + x), accurate for small x */
{
  volatile double u = 1.0 + x;
  if (u == 1.0) return x;
  return log(u) * x / (u - 1.0);
}
static double em1(double x)                   /* exp(x) - 1, accurate for small x */
{
  if (fabs(x) < 1e-5) return x + x * x / 2 + x * x * x / 6;
  return exp(x) - 1.0;
}
/* Lanczos' approximation (g = 7, 9 terms) of the gamma function, x >= 0.5: the series A(x) and
   t = x + 6.5, so that gamma(x) = sqrt(2 pi) t^(x - 0.5) e^-t A(x) */
static const double lanczos[9] = {
  0.99999999999980993, 676.5203681218851, -1259.1392167224028, 771.32342877765313,
  -176.61502916214059, 12.507343278686905, -0.13857109526572012, 9.9843695780195716e-6,
  1.5056327351493116e-7
};
static double lanczos_sum(double x)            /* x already reduced by 1 */
{
  double a = lanczos[0];
  int i;
  for (i = 1; i < 9; i++) a += lanczos[i] / (x + i);
  return a;
}
/* log |gamma(x)| for x > 0 */
static double lgam(double x)
{
  double t, y;
  if (x < 0.5) return log(M_PI / fabs(sin(M_PI * x))) - lgam(1.0 - x);
  if (x == 1.0 || x == 2.0) return 0;
  if (x > 1e15) return x * (log(x) - 1.0);
  y = x - 1.0;
  t = y + 7.5;
  return LN_SQRT2PI + (y + 0.5) * log(t) - t + log(lanczos_sum(y));
}
/* gamma(x) for any x but 0 and the negative integers (NaN there) */
static double gam(double x)
{
  double y, t, tp;
  if (x <= 0 && x == floor(x)) return nan_();
  if (x < 0.5) return M_PI / (sin(M_PI * x) * gam(1.0 - x));
  if (x > 171.7) return inf_();
  if (x == floor(x) && x <= 30) { double r = 1; for (t = 2; t < x; t++) r *= t; return r; }
  y = x - 1.0;
  t = y + 7.5;
  tp = pow(t, (y + 0.5) * 0.5);                 /* in two halves: t^(x - 0.5) alone overflows past 141 */
  return SQRT2PI * tp * (tp * exp(-t)) * lanczos_sum(y);
}
/* the regularized incomplete gamma functions P(a, x) (lower) and Q(a, x) = 1 - P (upper), a > 0 */
static double gam_series(double a, double x)   /* P by its series, for x < a + 1 */
{
  double sum = 1.0 / a, del = sum, ap = a;
  int i;
  for (i = 0; i < 100000; i++) {
    ap += 1.0; del *= x / ap; sum += del;
    if (fabs(del) < fabs(sum) * 1e-17) break;
  }
  return sum * exp(-x + a * log(x) - lgam(a));
}
static double gam_cfrac(double a, double x)    /* Q by its continued fraction (Lentz), for x >= a + 1 */
{
  double b = x + 1.0 - a, c = 1.0 / TINY, d = 1.0 / b, h = d, an, del;
  int i;
  for (i = 1; i < 100000; i++) {
    an = -i * (i - a); b += 2.0;
    d = an * d + b; if (fabs(d) < TINY) d = TINY;
    c = b + an / c; if (fabs(c) < TINY) c = TINY;
    d = 1.0 / d; del = d * c; h *= del;
    if (fabs(del - 1.0) < 1e-16) break;
  }
  return exp(-x + a * log(x) - lgam(a)) * h;
}
static double gam_p(double a, double x)
{
  if (x <= 0) return 0;
  return x < a + 1.0 ? gam_series(a, x) : 1.0 - gam_cfrac(a, x);
}
static double gam_q(double a, double x)
{
  if (x <= 0) return 1;
  return x < a + 1.0 ? 1.0 - gam_series(a, x) : gam_cfrac(a, x);
}
/* the regularized incomplete beta function I_x(a, b), given x and y = 1 - x separately (so either
   can be tiny without losing digits); upper = 1 gives 1 - I_x(a, b) */
static double beta_cfrac(double a, double b, double x)
{
  double qab = a + b, qap = a + 1.0, qam = a - 1.0, c = 1.0, d = 1.0 - qab * x / qap, h, aa, del;
  int m;
  if (fabs(d) < TINY) d = TINY;
  d = 1.0 / d; h = d;
  for (m = 1; m < 100000; m++) {
    int m2 = 2 * m;
    aa = m * (b - m) * x / ((qam + m2) * (a + m2));
    d = 1.0 + aa * d; if (fabs(d) < TINY) d = TINY;
    c = 1.0 + aa / c; if (fabs(c) < TINY) c = TINY;
    d = 1.0 / d; h *= d * c;
    aa = -(a + m) * (qab + m) * x / ((a + m2) * (qap + m2));
    d = 1.0 + aa * d; if (fabs(d) < TINY) d = TINY;
    c = 1.0 + aa / c; if (fabs(c) < TINY) c = TINY;
    d = 1.0 / d; del = d * c; h *= del;
    if (fabs(del - 1.0) < 1e-16) break;
  }
  return h;
}
static double ibeta(double x, double y, double a, double b, int upper)
{
  double lbt, bt;
  if (x <= 0) return upper ? 1 : 0;
  if (y <= 0) return upper ? 0 : 1;
  lbt = lgam(a + b) - lgam(a) - lgam(b) + a * log(x) + b * log(y);
  bt = exp(lbt);
  if (x < (a + 1.0) / (a + b + 2.0)) {
    double r = bt * beta_cfrac(a, b, x) / a;
    return upper ? 1.0 - r : r;
  } else {
    double r = bt * beta_cfrac(b, a, y) / b;
    return upper ? r : 1.0 - r;
  }
}
/* the standard normal distribution: density, lower tail Phi(z), and erfc via Q(1/2, x^2) */
static double npdf(double z) { return exp(-0.5 * z * z) / SQRT2PI; }
static double erfc_(double x) { return x < 0 ? 2.0 - erfc_(-x) : gam_q(0.5, x * x); }
static double ncdf(double z) { return 0.5 * erfc_(-z / 1.41421356237309504880); }
/* its inverse: Abramowitz and Stegun 26.2.23 to start, then Halley's method on the lower tail */
static double ninv(double p)
{
  double q = p < 0.5 ? p : 1.0 - p, t, x;
  int i;
  t = sqrt(-2.0 * log(q));
  x = -(t - (2.515517 + 0.802853 * t + 0.010328 * t * t) / (1.0 + 1.432788 * t + 0.189269 * t * t + 0.001308 * t * t * t));
  for (i = 0; i < 8; i++) {
    double e = ncdf(x) - q, u = e * SQRT2PI * exp(0.5 * x * x), dx = u / (1.0 + 0.5 * x * u);
    x -= dx;
    if (fabs(dx) <= 1e-15 * fabs(x)) break;
  }
  return p < 0.5 ? x : -x;
}

/* ------------------------------------------------------------------ distributions */
/* Student's t: the upper tail P(T > t) for t >= 0 */
static double t_upper(double t, double df)
{
  double t2 = t * t;
  if (t == 0) return 0.5;
  return 0.5 * ibeta(df / (df + t2), t2 / (df + t2), df / 2, 0.5, 0);
}
static double t_cdf(double t, double df) { return t < 0 ? t_upper(-t, df) : 1.0 - t_upper(t, df); }
static double t_pdf(double t, double df)
{
  return exp(lgam((df + 1) / 2) - lgam(df / 2) - (df + 1) / 2 * lg1p(t * t / df)) / sqrt(df * M_PI);
}
/* chi-squared */
static double chi_cdf(double x, double df) { return gam_p(df / 2, x / 2); }
static double chi_upper(double x, double df) { return gam_q(df / 2, x / 2); }
static double gamma_pdf(double x, double a, double b)       /* shape a, scale b */
{
  if (x == 0) return a < 1 ? inf_() : a == 1 ? 1 / b : 0;
  return exp((a - 1) * log(x) - x / b - lgam(a) - a * log(b));
}
/* F with d1 and d2 degrees of freedom */
static double f_tail(double x, double d1, double d2, int upper)
{
  double s = d1 * x + d2;
  return ibeta(d1 * x / s, d2 / s, d1 / 2, d2 / 2, upper);
}
static double f_pdf(double x, double d1, double d2)
{
  if (x == 0) return d1 < 2 ? inf_() : d1 == 2 ? 1 : 0;
  return exp(0.5 * (d1 * log(d1 * x) + d2 * log(d2) - (d1 + d2) * log(d1 * x + d2)) - (lgam(d1 / 2) + lgam(d2 / 2) - lgam((d1 + d2) / 2))) / x;
}
static double lchoose(double n, double k) { return lgam(n + 1) - lgam(k + 1) - lgam(n - k + 1); }
static double binom_pmf(double k, double n, double p)
{
  if (p == 0) return k == 0;
  if (p == 1) return k == n;
  return exp(lchoose(n, k) + k * log(p) + (n - k) * lg1p(-p));
}
static double binom_cdf(double k, double n, double p)       /* P(X <= k) */
{
  if (k < 0) return 0;
  if (k >= n) return 1;
  if (p == 0) return 1;
  if (p == 1) return 0;
  return ibeta(1 - p, p, n - k, k + 1, 0);
}

/* the inverse of a continuous distribution function by Brent's method: cdf(x, par) = target with x
   in [lo, hi] (hi doubled until it brackets the root when unbounded); dec: cdf decreases in x */
typedef double (*Cdf)(double x, const double *par);
static double solve(Cdf f, const double *par, double target, double lo, double hi, int bounded, int dec)
{
  double a = lo, b = hi, fa, fb, c, fc, d, e, tol, m, p, q, r, s;
  int i;
  fa = f(a, par) - target; fb = f(b, par) - target;
  if (dec) { fa = -fa; fb = -fb; }
  for (i = 0; !bounded && fb < 0 && i < 2000; i++) {
    a = b; fa = fb; b *= 2;
    fb = f(b, par) - target; if (dec) fb = -fb;
  }
  if (fa == 0) return a;
  if (fb == 0) return b;
  if ((fa > 0) == (fb > 0)) return nan_();
  c = a; fc = fa; d = e = b - a;
  for (i = 0; i < 500; i++) {
    if ((fb > 0) == (fc > 0)) { c = a; fc = fa; d = e = b - a; }
    if (fabs(fc) < fabs(fb)) { a = b; b = c; c = a; fa = fb; fb = fc; fc = fa; }
    tol = 4e-16 * fabs(b) + 1e-300;
    m = 0.5 * (c - b);
    if (fabs(m) <= tol || fb == 0) return b;
    if (fabs(e) >= tol && fabs(fa) > fabs(fb)) {
      s = fb / fa;
      if (a == c) { p = 2 * m * s; q = 1 - s; }
      else { q = fa / fc; r = fb / fc; p = s * (2 * m * q * (q - r) - (b - a) * (r - 1)); q = (q - 1) * (r - 1) * (s - 1); }
      if (p > 0) q = -q; else p = -p;
      if (2 * p < 3 * m * q - fabs(tol * q) && p < fabs(0.5 * e * q)) { e = d; d = p / q; }
      else { d = m; e = m; }
    } else { d = m; e = m; }
    a = b; fa = fb;
    b += fabs(d) > tol ? d : (m > 0 ? tol : -tol);
    fb = f(b, par) - target; if (dec) fb = -fb;
  }
  return b;
}
static double cdf_t_upper(double x, const double *p) { return t_upper(x, p[0]); }
static double cdf_chi(double x, const double *p) { return chi_cdf(x, p[0]); }
static double cdf_chi_upper(double x, const double *p) { return chi_upper(x, p[0]); }
static double cdf_gamma(double x, const double *p) { return gam_p(p[0], x); }
static double cdf_f(double x, const double *p) { return f_tail(x, p[0], p[1], 0); }
static double cdf_f_upper(double x, const double *p) { return f_tail(x, p[0], p[1], 1); }
static double cdf_beta(double x, const double *p) { return ibeta(x, 1 - x, p[0], p[1], 0); }
/* t > 0 with P(T > t) = q (q <= 1/2) */
static double t_inv_upper(double q, double df)
{
  double par[1];
  par[0] = df;
  if (q >= 0.5) return 0;
  return solve(cdf_t_upper, par, q, 0, 1, 0, 1);
}

/* ================================================================== argument helpers */
/* the numeric arguments from first on into x (count of them); a missing one keeps x's value */
static int nums(Ctx *cx, Value *a, int n, int first, int count, double *x)
{
  int i, e;
  for (i = 0; i < count; i++)
    if (first + i < n && !IS_MISSING(&a[first + i]) && (e = arg_num(cx, &a[first + i], &x[i]))) return e;
  return 0;
}
static int boolarg(Ctx *cx, Value *a, int n, int i, int *b)
{
  if (i >= n || IS_MISSING(&a[i])) return 0;
  return arg_bool(cx, &a[i], b);
}
static double trunc_(double x) { return x < 0 ? ceil(x) : floor(x); }
#define NUMS(first, count, arr) do { int e_ = nums(cx, a, n, first, count, arr); if (e_) return val_err(e_); } while (0)
#define BOOLARG(i, var) do { int e_ = boolarg(cx, a, n, i, &var); if (e_) return val_err(e_); } while (0)

/* the numbers of one argument (as SUM sees them), sorted when asked; *count; NULL with *err */
static double *numbers(Ctx *cx, Value *a, long *count, int all, int sort, int *err)
{
  Nums ns;
  if ((*err = gather(cx, a, 1, &ns, all))) { free(ns.v); return NULL; }
  if (!ns.v) ns.v = (double *)malloc(sizeof(double));
  if (sort && ns.n) qsort(ns.v, (size_t)ns.n, sizeof(double), dcmp);
  *count = ns.n;
  return ns.v;
}

/* pairs of numbers from two equally sized ranges or arrays: positions where both are numbers.
   E_NA when the sizes differ, or an error found in either */
typedef struct { double *x, *y; long n; } Pairs;
static int pairs(Ctx *cx, Value *ya, Value *xa, Pairs *p)
{
  i32 r1, c1, r2, c2, r, c;
  long i, total;
  memset(p, 0, sizeof *p);
  if (ya->t == V_ERR) return ya->e;
  if (xa->t == V_ERR) return xa->e;
  arg_shape(ya, &r1, &c1);
  arg_shape(xa, &r2, &c2);
  total = (long)r1 * c1;
  if (total != (long)r2 * c2) return E_NA;
  if (total > 4000000L) return E_NUM;
  p->x = (double *)malloc((size_t)(total + 1) * sizeof(double));
  p->y = (double *)malloc((size_t)(total + 1) * sizeof(double));
  if (!p->x || !p->y) { free(p->x); free(p->y); return E_NUM; }
  for (i = 0; i < total; i++) {
    const Value *yv, *xv;
    double yn;
    r = (i32)(i / c1); c = (i32)(i % c1);
    yv = arg_at(cx, ya, r, c);
    if (yv->t == V_ERR) { free(p->x); free(p->y); return yv->e; }
    if (yv->t != V_NUM) {
      xv = arg_at(cx, xa, (i32)(i / c2), (i32)(i % c2));
      if (xv->t == V_ERR) { free(p->x); free(p->y); return xv->e; }
      continue;
    }
    yn = yv->n;
    xv = arg_at(cx, xa, (i32)(i / c2), (i32)(i % c2));
    if (xv->t == V_ERR) { free(p->x); free(p->y); return xv->e; }
    if (xv->t != V_NUM) continue;
    p->y[p->n] = yn; p->x[p->n] = xv->n; p->n++;
  }
  return 0;
}
static void pairs_free(Pairs *p) { free(p->x); free(p->y); }
/* means and centred sums of a set of pairs */
typedef struct { double mx, my, sxx, syy, sxy; long n; } Moments;
static void moments(const Pairs *p, Moments *m)
{
  long i;
  memset(m, 0, sizeof *m);
  m->n = p->n;
  if (!p->n) return;
  for (i = 0; i < p->n; i++) { m->mx += p->x[i]; m->my += p->y[i]; }
  m->mx /= p->n; m->my /= p->n;
  for (i = 0; i < p->n; i++) {
    double dx = p->x[i] - m->mx, dy = p->y[i] - m->my;
    m->sxx += dx * dx; m->syy += dy * dy; m->sxy += dx * dy;
  }
}
static Value arr_value(Arr *ar) { Value v = val_empty(); v.t = V_ARR; v.a = ar; return v; }

/* ================================================================== the normal family */
static Value f_norm_dist(Ctx *cx, Value *a, int n)
{
  double x[3]; int cum = 0;
  NUMS(0, 3, x); BOOLARG(3, cum);
  if (x[2] <= 0) return val_err(E_NUM);
  {
    double z = (x[0] - x[1]) / x[2];
    return num_or_err(cum ? ncdf(z) : npdf(z) / x[2]);
  }
}
static Value f_normdist(Ctx *cx, Value *a, int n) { return f_norm_dist(cx, a, n); }
static Value f_norm_inv(Ctx *cx, Value *a, int n)
{
  double x[3];
  NUMS(0, 3, x);
  if (x[0] <= 0 || x[0] >= 1 || x[2] <= 0) return val_err(E_NUM);
  return num_or_err(x[1] + x[2] * ninv(x[0]));
}
static Value f_norm_s_dist(Ctx *cx, Value *a, int n)
{
  double z; int cum = 1;
  NUMS(0, 1, &z);
  if (n > 1) BOOLARG(1, cum);
  return num_or_err(cum ? ncdf(z) : npdf(z));
}
static Value f_normsdist(Ctx *cx, Value *a, int n) { double z; NUMS(0, 1, &z); return num_or_err(ncdf(z)); }
static Value f_norm_s_inv(Ctx *cx, Value *a, int n)
{
  double p;
  NUMS(0, 1, &p);
  if (p <= 0 || p >= 1) return val_err(E_NUM);
  return num_or_err(ninv(p));
}
static Value f_phi(Ctx *cx, Value *a, int n) { double z; NUMS(0, 1, &z); return num_or_err(npdf(z)); }
static Value f_gauss(Ctx *cx, Value *a, int n)
{
  double z;
  NUMS(0, 1, &z);
  return num_or_err(z < 0 ? -(0.5 - ncdf(z)) : 0.5 - ncdf(-z));
}
static Value f_standardize(Ctx *cx, Value *a, int n)
{
  double x[3];
  NUMS(0, 3, x);
  if (x[2] <= 0) return val_err(E_NUM);
  return num_or_err((x[0] - x[1]) / x[2]);
}
static Value f_lognorm_dist(Ctx *cx, Value *a, int n)
{
  double x[3]; int cum = 1;
  NUMS(0, 3, x);
  if (n > 3) BOOLARG(3, cum);
  if (x[0] <= 0 || x[2] <= 0) return val_err(E_NUM);
  {
    double z = (log(x[0]) - x[1]) / x[2];
    return num_or_err(cum ? ncdf(z) : npdf(z) / (x[0] * x[2]));
  }
}
static Value f_lognorm_inv(Ctx *cx, Value *a, int n)
{
  double x[3];
  NUMS(0, 3, x);
  if (x[0] <= 0 || x[0] >= 1 || x[2] <= 0) return val_err(E_NUM);
  return num_or_err(exp(x[1] + x[2] * ninv(x[0])));
}
static Value f_confidence_norm(Ctx *cx, Value *a, int n)
{
  double x[3];
  NUMS(0, 3, x);
  x[2] = trunc_(x[2]);
  if (x[0] <= 0 || x[0] >= 1 || x[1] <= 0 || x[2] < 1) return val_err(E_NUM);
  return num_or_err(-ninv(x[0] / 2) * x[1] / sqrt(x[2]));
}
static Value f_confidence_t(Ctx *cx, Value *a, int n)
{
  double x[3];
  NUMS(0, 3, x);
  x[2] = trunc_(x[2]);
  if (x[0] <= 0 || x[0] >= 1 || x[1] <= 0 || x[2] < 1) return val_err(E_NUM);
  if (x[2] == 1) return val_err(E_DIV0);
  return num_or_err(t_inv_upper(x[0] / 2, x[2] - 1) * x[1] / sqrt(x[2]));
}
static Value f_fisher(Ctx *cx, Value *a, int n)
{
  double x;
  NUMS(0, 1, &x);
  if (x <= -1 || x >= 1) return val_err(E_NUM);
  return num_or_err(0.5 * log((1 + x) / (1 - x)));
}
static Value f_fisherinv(Ctx *cx, Value *a, int n)
{
  double y;
  NUMS(0, 1, &y);
  if (fabs(y) > 350) return val_num(y > 0 ? 1 : -1);
  { double e = exp(2 * y); return num_or_err((e - 1) / (e + 1)); }
}

/* ================================================================== t, chi-squared and F */
static Value t_dist_impl(Ctx *cx, Value *a, int n, int kind)
{
  /* kind 0: T.DIST(x, df, cum), 1: T.DIST.RT, 2: T.DIST.2T, 3: TDIST(x, df, tails) */
  double x[3]; int cum = 0;
  x[2] = 0;
  NUMS(0, kind == 3 ? 3 : 2, x);
  if (kind == 0) BOOLARG(2, cum);
  x[1] = trunc_(x[1]);
  if (kind == 3) {
    x[2] = trunc_(x[2]);
    if (x[2] != 1 && x[2] != 2) return val_err(E_NUM);
    kind = x[2] == 1 ? 1 : 2;
    if (x[0] < 0) return val_err(E_NUM);
  }
  if (x[1] < 1 || x[1] > 1e10) return val_err(E_NUM);
  if (kind == 2 && x[0] < 0) return val_err(E_NUM);
  if (kind == 0) return num_or_err(cum ? t_cdf(x[0], x[1]) : t_pdf(x[0], x[1]));
  if (kind == 1) return num_or_err(x[0] >= 0 ? t_upper(x[0], x[1]) : 1 - t_upper(-x[0], x[1]));
  return num_or_err(2 * t_upper(x[0], x[1]));
}
static Value f_t_dist(Ctx *cx, Value *a, int n) { return t_dist_impl(cx, a, n, 0); }
static Value f_t_dist_rt(Ctx *cx, Value *a, int n) { return t_dist_impl(cx, a, n, 1); }
static Value f_t_dist_2t(Ctx *cx, Value *a, int n) { return t_dist_impl(cx, a, n, 2); }
static Value f_tdist(Ctx *cx, Value *a, int n) { return t_dist_impl(cx, a, n, 3); }
static Value f_t_inv(Ctx *cx, Value *a, int n)
{
  double x[2];
  NUMS(0, 2, x);
  x[1] = trunc_(x[1]);
  if (x[0] <= 0 || x[0] >= 1 || x[1] < 1 || x[1] > 1e10) return val_err(E_NUM);
  if (x[0] == 0.5) return val_num(0);
  return num_or_err(x[0] < 0.5 ? -t_inv_upper(x[0], x[1]) : t_inv_upper(1 - x[0], x[1]));
}
static Value f_t_inv_2t(Ctx *cx, Value *a, int n)
{
  double x[2];
  NUMS(0, 2, x);
  x[1] = trunc_(x[1]);
  if (x[0] <= 0 || x[0] > 1 || x[1] < 1 || x[1] > 1e10) return val_err(E_NUM);
  return num_or_err(t_inv_upper(x[0] / 2, x[1]));
}
static Value chi_dist_impl(Ctx *cx, Value *a, int n, int kind)
{
  /* kind 0: CHISQ.DIST(x, df, cum), 1: CHISQ.DIST.RT / CHIDIST */
  double x[2]; int cum = 0;
  NUMS(0, 2, x);
  if (kind == 0) BOOLARG(2, cum);
  x[1] = trunc_(x[1]);
  if (x[0] < 0 || x[1] < 1 || x[1] > 1e10) return val_err(E_NUM);
  if (kind == 1) return num_or_err(chi_upper(x[0], x[1]));
  return num_or_err(cum ? chi_cdf(x[0], x[1]) : gamma_pdf(x[0], x[1] / 2, 2));
}
static Value f_chisq_dist(Ctx *cx, Value *a, int n) { return chi_dist_impl(cx, a, n, 0); }
static Value f_chisq_dist_rt(Ctx *cx, Value *a, int n) { return chi_dist_impl(cx, a, n, 1); }
static Value chi_inv_impl(Ctx *cx, Value *a, int n, int upper)
{
  double x[2], par[1];
  NUMS(0, 2, x);
  x[1] = trunc_(x[1]);
  if (x[0] < 0 || x[0] > 1 || x[1] < 1 || x[1] > 1e10) return val_err(E_NUM);
  if (x[0] == (upper ? 1 : 0)) return val_num(0);
  if (x[0] == (upper ? 0 : 1)) return val_err(E_NUM);
  par[0] = x[1];
  return num_or_err(upper ? solve(cdf_chi_upper, par, x[0], 0, x[1] + 1, 0, 1) : solve(cdf_chi, par, x[0], 0, x[1] + 1, 0, 0));
}
static Value f_chisq_inv(Ctx *cx, Value *a, int n) { return chi_inv_impl(cx, a, n, 0); }
static Value f_chisq_inv_rt(Ctx *cx, Value *a, int n) { return chi_inv_impl(cx, a, n, 1); }
static Value f_dist_impl(Ctx *cx, Value *a, int n, int kind)
{
  /* kind 0: F.DIST(x, d1, d2, cum), 1: F.DIST.RT / FDIST */
  double x[3]; int cum = 0;
  NUMS(0, 3, x);
  if (kind == 0) BOOLARG(3, cum);
  x[1] = trunc_(x[1]); x[2] = trunc_(x[2]);
  if (x[0] < 0 || x[1] < 1 || x[2] < 1 || x[1] >= 1e10 || x[2] >= 1e10) return val_err(E_NUM);
  if (kind == 1) return num_or_err(f_tail(x[0], x[1], x[2], 1));
  return num_or_err(cum ? f_tail(x[0], x[1], x[2], 0) : f_pdf(x[0], x[1], x[2]));
}
static Value f_f_dist(Ctx *cx, Value *a, int n) { return f_dist_impl(cx, a, n, 0); }
static Value f_f_dist_rt(Ctx *cx, Value *a, int n) { return f_dist_impl(cx, a, n, 1); }
static Value f_inv_impl(Ctx *cx, Value *a, int n, int upper)
{
  double x[3];
  NUMS(0, 3, x);
  x[1] = trunc_(x[1]); x[2] = trunc_(x[2]);
  if (x[0] < 0 || x[0] > 1 || x[1] < 1 || x[2] < 1 || x[1] >= 1e10 || x[2] >= 1e10) return val_err(E_NUM);
  if (x[0] == (upper ? 1 : 0)) return val_num(0);
  if (x[0] == (upper ? 0 : 1)) return val_err(E_NUM);
  return num_or_err(upper ? solve(cdf_f_upper, &x[1], x[0], 0, 1, 0, 1) : solve(cdf_f, &x[1], x[0], 0, 1, 0, 0));
}
static Value f_f_inv(Ctx *cx, Value *a, int n) { return f_inv_impl(cx, a, n, 0); }
static Value f_f_inv_rt(Ctx *cx, Value *a, int n) { return f_inv_impl(cx, a, n, 1); }

/* ================================================================== gamma, beta and the others */
static Value f_gamma(Ctx *cx, Value *a, int n)
{
  double x;
  NUMS(0, 1, &x);
  if (x <= 0 && x == floor(x)) return val_err(E_NUM);
  return num_or_err(gam(x));
}
static Value f_gammaln(Ctx *cx, Value *a, int n)
{
  double x;
  NUMS(0, 1, &x);
  if (x <= 0) return val_err(E_NUM);
  return num_or_err(lgam(x));
}
static Value f_gamma_dist(Ctx *cx, Value *a, int n)
{
  double x[3]; int cum = 0;
  NUMS(0, 3, x); BOOLARG(3, cum);
  if (x[0] < 0 || x[1] <= 0 || x[2] <= 0) return val_err(E_NUM);
  return num_or_err(cum ? gam_p(x[1], x[0] / x[2]) : gamma_pdf(x[0], x[1], x[2]));
}
static Value f_gamma_inv(Ctx *cx, Value *a, int n)
{
  double x[3];
  NUMS(0, 3, x);
  if (x[0] < 0 || x[0] >= 1 || x[1] <= 0 || x[2] <= 0) return val_err(E_NUM);
  if (x[0] == 0) return val_num(0);
  return num_or_err(x[2] * solve(cdf_gamma, &x[1], x[0], 0, x[1] + 1, 0, 0));
}
static Value f_beta_dist(Ctx *cx, Value *a, int n)
{
  /* BETA.DIST(x, alpha, beta, cum, [A], [B]); BETADIST(x, alpha, beta, [A], [B]) is cumulative */
  double x[3], ab[2]; int cum = 1;
  ab[0] = 0; ab[1] = 1;
  NUMS(0, 3, x); BOOLARG(3, cum);
  NUMS(4, 2, ab);
  if (x[1] <= 0 || x[2] <= 0 || ab[0] >= ab[1] || x[0] < ab[0] || x[0] > ab[1]) return val_err(E_NUM);
  {
    double w = ab[1] - ab[0], u = (x[0] - ab[0]) / w, v = (ab[1] - x[0]) / w;
    if (cum) return num_or_err(ibeta(u, v, x[1], x[2], 0));
    if ((u == 0 && x[1] < 1) || (v == 0 && x[2] < 1)) return val_err(E_NUM);
    if (u == 0) return val_num(x[1] == 1 ? x[2] / w : 0);
    if (v == 0) return val_num(x[2] == 1 ? x[1] / w : 0);
    return num_or_err(exp((x[1] - 1) * log(u) + (x[2] - 1) * log(v) - (lgam(x[1]) + lgam(x[2]) - lgam(x[1] + x[2]))) / w);
  }
}
static Value f_betadist(Ctx *cx, Value *a, int n)
{
  double x[3], ab[2];
  ab[0] = 0; ab[1] = 1;
  NUMS(0, 3, x); NUMS(3, 2, ab);
  if (x[1] <= 0 || x[2] <= 0 || ab[0] >= ab[1] || x[0] < ab[0] || x[0] > ab[1]) return val_err(E_NUM);
  {
    double w = ab[1] - ab[0];
    return num_or_err(ibeta((x[0] - ab[0]) / w, (ab[1] - x[0]) / w, x[1], x[2], 0));
  }
}
static Value f_beta_inv(Ctx *cx, Value *a, int n)
{
  double x[3], ab[2];
  ab[0] = 0; ab[1] = 1;
  NUMS(0, 3, x); NUMS(3, 2, ab);
  if (x[0] <= 0 || x[0] > 1 || x[1] <= 0 || x[2] <= 0 || ab[0] >= ab[1]) return val_err(E_NUM);
  if (x[0] == 1) return val_num(ab[1]);
  return num_or_err(ab[0] + (ab[1] - ab[0]) * solve(cdf_beta, &x[1], x[0], 0, 1, 1, 0));
}
static Value f_expon_dist(Ctx *cx, Value *a, int n)
{
  double x[2]; int cum = 0;
  NUMS(0, 2, x); BOOLARG(2, cum);
  if (x[0] < 0 || x[1] <= 0) return val_err(E_NUM);
  return num_or_err(cum ? -em1(-x[1] * x[0]) : x[1] * exp(-x[1] * x[0]));
}
static Value f_weibull_dist(Ctx *cx, Value *a, int n)
{
  double x[3]; int cum = 0;
  NUMS(0, 3, x); BOOLARG(3, cum);
  if (x[0] < 0 || x[1] <= 0 || x[2] <= 0) return val_err(E_NUM);
  {
    double t = pow(x[0] / x[2], x[1]);
    if (cum) return num_or_err(-em1(-t));
    if (x[0] == 0) return x[1] < 1 ? val_err(E_NUM) : val_num(x[1] == 1 ? 1 / x[2] : 0);
    return num_or_err(x[1] / x[2] * pow(x[0] / x[2], x[1] - 1) * exp(-t));
  }
}

/* ------------------------------------------------------------------ discrete distributions */
static Value f_binom_dist(Ctx *cx, Value *a, int n)
{
  double x[3]; int cum = 0;
  NUMS(0, 3, x); BOOLARG(3, cum);
  x[0] = trunc_(x[0]); x[1] = trunc_(x[1]);
  if (x[0] < 0 || x[0] > x[1] || x[2] < 0 || x[2] > 1) return val_err(E_NUM);
  return num_or_err(cum ? binom_cdf(x[0], x[1], x[2]) : binom_pmf(x[0], x[1], x[2]));
}
static Value f_binom_dist_range(Ctx *cx, Value *a, int n)
{
  double x[4];
  NUMS(0, 3, x);
  x[0] = trunc_(x[0]); x[2] = trunc_(x[2]);
  x[3] = x[2];
  NUMS(3, 1, &x[3]);
  x[3] = trunc_(x[3]);
  if (x[0] < 0 || x[1] < 0 || x[1] > 1 || x[2] < 0 || x[2] > x[0] || x[3] < x[2] || x[3] > x[0]) return val_err(E_NUM);
  if (x[3] == x[2]) return num_or_err(binom_pmf(x[2], x[0], x[1]));
  {
    double s = 0, k;
    if (x[3] - x[2] > 1000) return num_or_err(binom_cdf(x[3], x[0], x[1]) - binom_cdf(x[2] - 1, x[0], x[1]));
    for (k = x[2]; k <= x[3]; k++) s += binom_pmf(k, x[0], x[1]);
    return num_or_err(s);
  }
}
static Value f_binom_inv(Ctx *cx, Value *a, int n)
{
  /* the smallest k with P(X <= k) >= alpha */
  double x[3], lo, hi;
  NUMS(0, 3, x);
  x[0] = trunc_(x[0]);
  if (x[0] < 0 || x[1] < 0 || x[1] > 1 || x[2] < 0 || x[2] > 1) return val_err(E_NUM);
  lo = 0; hi = x[0];
  while (lo < hi) {
    double mid = floor((lo + hi) / 2);
    if (binom_cdf(mid, x[0], x[1]) >= x[2] * (1 - 1e-15)) hi = mid; else lo = mid + 1;
  }
  return val_num(lo);
}
static Value f_negbinom_dist(Ctx *cx, Value *a, int n)
{
  /* NEGBINOM.DIST(failures, successes, p, cum); NEGBINOMDIST has no cum (the probability) */
  double x[3]; int cum = 0;
  NUMS(0, 3, x); BOOLARG(3, cum);
  x[0] = trunc_(x[0]); x[1] = trunc_(x[1]);
  if (x[0] < 0 || x[1] < 1 || x[2] < 0 || x[2] > 1) return val_err(E_NUM);
  if (cum) return num_or_err(ibeta(x[2], 1 - x[2], x[1], x[0] + 1, 0));
  if (x[2] == 0) return val_num(0);
  if (x[2] == 1) return val_num(x[0] == 0);
  return num_or_err(exp(lchoose(x[0] + x[1] - 1, x[1] - 1) + x[1] * log(x[2]) + x[0] * lg1p(-x[2])));
}
static Value f_poisson_dist(Ctx *cx, Value *a, int n)
{
  double x[2]; int cum = 0;
  NUMS(0, 2, x); BOOLARG(2, cum);
  x[0] = trunc_(x[0]);
  if (x[0] < 0 || x[1] < 0) return val_err(E_NUM);
  if (x[1] == 0) return val_num(cum || x[0] == 0 ? 1 : 0);
  if (cum) return num_or_err(gam_q(x[0] + 1, x[1]));
  return num_or_err(exp(-x[1] + x[0] * log(x[1]) - lgam(x[0] + 1)));
}
static double hyp_pmf(double s, double ns, double ps, double np)
{
  return exp(lchoose(ps, s) + lchoose(np - ps, ns - s) - lchoose(np, ns));
}
static Value f_hypgeom_dist(Ctx *cx, Value *a, int n)
{
  /* HYPGEOM.DIST(sample_s, number_sample, population_s, number_pop, cum); HYPGEOMDIST without cum */
  double x[4]; int cum = 0, i;
  NUMS(0, 4, x); BOOLARG(4, cum);
  for (i = 0; i < 4; i++) x[i] = trunc_(x[i]);
  if (x[0] < 0 || x[0] > x[1] || x[0] > x[2] || x[1] <= 0 || x[1] > x[3] || x[2] <= 0 || x[2] > x[3] || x[3] <= 0 ||
      x[0] < x[1] - x[3] + x[2]) return val_err(E_NUM);
  if (!cum) return num_or_err(hyp_pmf(x[0], x[1], x[2], x[3]));
  {
    double s = 0, k, lo = x[1] - x[3] + x[2];
    if (lo < 0) lo = 0;
    for (k = lo; k <= x[0]; k++) s += hyp_pmf(k, x[1], x[2], x[3]);
    return num_or_err(s > 1 ? 1 : s);
  }
}

/* ================================================================== descriptive statistics */
static Value var_impl(Ctx *cx, Value *a, int n, int pop, int all)
{
  Nums ns; double mean = 0, ss = 0; long i;
  if (gather(cx, a, n, &ns, all)) { free(ns.v); return val_err(ns.err); }
  if (ns.n < (pop ? 1 : 2)) { free(ns.v); return val_err(E_DIV0); }
  for (i = 0; i < ns.n; i++) mean += ns.v[i];
  mean /= ns.n;
  for (i = 0; i < ns.n; i++) ss += (ns.v[i] - mean) * (ns.v[i] - mean);
  free(ns.v);
  return val_num(ss / (pop ? ns.n : ns.n - 1));
}
static Value f_vara(Ctx *cx, Value *a, int n) { return var_impl(cx, a, n, 0, 1); }
static Value f_varpa(Ctx *cx, Value *a, int n) { return var_impl(cx, a, n, 1, 1); }
static Value f_stdevpa(Ctx *cx, Value *a, int n) { Value v = var_impl(cx, a, n, 1, 1); if (v.t == V_NUM) v.n = sqrt(v.n); return v; }
static Value f_devsq(Ctx *cx, Value *a, int n)
{
  Nums ns; double mean = 0, ss = 0; long i;
  if (gather(cx, a, n, &ns, 0)) { free(ns.v); return val_err(ns.err); }
  if (!ns.n) { free(ns.v); return val_err(E_NUM); }
  for (i = 0; i < ns.n; i++) mean += ns.v[i];
  mean /= ns.n;
  for (i = 0; i < ns.n; i++) ss += (ns.v[i] - mean) * (ns.v[i] - mean);
  free(ns.v);
  return val_num(ss);
}
/* SKEW, SKEW.P and KURT: the centred moments */
static Value moment_impl(Ctx *cx, Value *a, int n, int kind)
{
  Nums ns; double mean = 0, m2 = 0, m3 = 0, m4 = 0, N, r; long i;
  if (gather(cx, a, n, &ns, 0)) { free(ns.v); return val_err(ns.err); }
  N = (double)ns.n;
  if (ns.n < (kind == 0 ? 3 : kind == 1 ? 1 : 4)) { free(ns.v); return val_err(E_DIV0); }
  for (i = 0; i < ns.n; i++) mean += ns.v[i];
  mean /= N;
  for (i = 0; i < ns.n; i++) { double d = ns.v[i] - mean; m2 += d * d; m3 += d * d * d; m4 += d * d * d * d; }
  free(ns.v);
  if (m2 <= 0) return val_err(E_DIV0);
  if (kind == 1) { double s = sqrt(m2 / N); r = m3 / N / (s * s * s); }
  else if (kind == 0) { double s = sqrt(m2 / (N - 1)); r = N / ((N - 1) * (N - 2)) * m3 / (s * s * s); }
  else { double v = m2 / (N - 1); r = N * (N + 1) / ((N - 1) * (N - 2) * (N - 3)) * m4 / (v * v) - 3 * (N - 1) * (N - 1) / ((N - 2) * (N - 3)); }
  return num_or_err(r);
}
static Value f_skew(Ctx *cx, Value *a, int n) { return moment_impl(cx, a, n, 0); }
static Value f_skew_p(Ctx *cx, Value *a, int n) { return moment_impl(cx, a, n, 1); }
static Value f_kurt(Ctx *cx, Value *a, int n) { return moment_impl(cx, a, n, 2); }
static Value f_geomean(Ctx *cx, Value *a, int n)
{
  Nums ns; double s = 0; long i;
  if (gather(cx, a, n, &ns, 0)) { free(ns.v); return val_err(ns.err); }
  if (!ns.n) { free(ns.v); return val_err(E_NUM); }
  for (i = 0; i < ns.n; i++) { if (ns.v[i] <= 0) { free(ns.v); return val_err(E_NUM); } s += log(ns.v[i]); }
  s = exp(s / ns.n);
  free(ns.v);
  return num_or_err(s);
}
static Value f_harmean(Ctx *cx, Value *a, int n)
{
  Nums ns; double s = 0; long i;
  if (gather(cx, a, n, &ns, 0)) { free(ns.v); return val_err(ns.err); }
  if (!ns.n) { free(ns.v); return val_err(E_NUM); }
  for (i = 0; i < ns.n; i++) { if (ns.v[i] <= 0) { free(ns.v); return val_err(E_NUM); } s += 1 / ns.v[i]; }
  s = ns.n / s;
  free(ns.v);
  return num_or_err(s);
}
static Value f_trimmean(Ctx *cx, Value *a, int n)
{
  double p, s = 0, *v; long cnt, k, i; int e;
  NUMS(1, 1, &p);
  if (!(v = numbers(cx, &a[0], &cnt, 0, 1, &e))) return val_err(e);
  if (p < 0 || p >= 1 || !cnt) { free(v); return val_err(E_NUM); }
  k = (long)floor(cnt * p / 2 + 1e-12);
  for (i = k; i < cnt - k; i++) s += v[i];
  s /= (double)(cnt - 2 * k);
  free(v);
  return num_or_err(s);
}
static Value f_rank_avg(Ctx *cx, Value *a, int n)
{
  double x, asc = 0, *v; long cnt, i, less = 0, eq = 0; int e;
  NUMS(0, 1, &x);
  NUMS(2, 1, &asc);
  if (a[1].t != V_REF && a[1].t != V_ARR) return val_err(a[1].t == V_ERR ? a[1].e : E_VALUE);
  if (!(v = numbers(cx, &a[1], &cnt, 0, 0, &e))) return val_err(e);
  for (i = 0; i < cnt; i++) {
    if (v[i] == x) eq++;
    else if (asc != 0 ? v[i] < x : v[i] > x) less++;
  }
  free(v);
  if (!eq) return val_err(E_NA);
  return val_num(less + (eq + 1) / 2.0);
}
static Value pctrank_impl(Ctx *cx, Value *a, int n, int excl)
{
  double x, sig = 3, *v, r, m; long cnt, i; int e;
  NUMS(1, 1, &x);
  NUMS(2, 1, &sig);
  sig = trunc_(sig);
  if (sig < 1) return val_err(E_NUM);
  if (!(v = numbers(cx, &a[0], &cnt, 0, 1, &e))) return val_err(e);
  if (!cnt) { free(v); return val_err(E_NUM); }
  if (x < v[0] || x > v[cnt - 1]) { free(v); return val_err(E_NA); }
  for (i = 0; i < cnt && v[i] < x; i++) ;
  /* v[i] is the first value >= x */
  if (v[i] == x) r = excl ? (i + 1.0) / (cnt + 1) : cnt == 1 ? 1 : (double)i / (cnt - 1);
  else {
    double fr = (x - v[i - 1]) / (v[i] - v[i - 1]);
    r = excl ? (i + fr) / (cnt + 1) : (i - 1 + fr) / (cnt - 1);
  }
  free(v);
  m = pow(10, sig);
  return val_num(floor(r * m + 1e-9) / m);
}
static Value f_percentrank_inc(Ctx *cx, Value *a, int n) { return pctrank_impl(cx, a, n, 0); }
static Value f_percentrank_exc(Ctx *cx, Value *a, int n) { return pctrank_impl(cx, a, n, 1); }
static Value f_quartile_exc(Ctx *cx, Value *a, int n)
{
  double q, *v, h, r; long cnt, lo; int e;
  NUMS(1, 1, &q);
  q = trunc_(q);
  if (q <= 0 || q >= 4) return val_err(E_NUM);
  if (!(v = numbers(cx, &a[0], &cnt, 0, 1, &e))) return val_err(e);
  h = q / 4 * (cnt + 1) - 1;
  if (!cnt || h < 0 || h > cnt - 1) { free(v); return val_err(E_NUM); }
  lo = (long)floor(h);
  r = lo + 1 < cnt ? v[lo] + (h - lo) * (v[lo + 1] - v[lo]) : v[lo];
  free(v);
  return val_num(r);
}
static Value f_mode_mult(Ctx *cx, Value *a, int n)
{
  Nums ns; long i, k, best = 1, *cnt, nm = 0; Arr *ar;
  if (gather(cx, a, n, &ns, 0)) { free(ns.v); return val_err(ns.err); }
  if (!(cnt = (long *)calloc((size_t)ns.n + 1, sizeof(long)))) { free(ns.v); return val_err(E_NUM); }
  for (i = 0; i < ns.n; i++) {
    for (k = 0; k < i && ns.v[k] != ns.v[i]; k++) ;
    if (k < i) { cnt[i] = -1; cnt[k]++; if (cnt[k] > best) best = cnt[k]; }
    else cnt[i] = 1;
  }
  for (i = 0; i < ns.n; i++) if (cnt[i] == best && best > 1) nm++;
  if (!nm || !(ar = arr_new((int)nm, 1))) { free(cnt); free(ns.v); return val_err(nm ? E_NUM : E_NA); }
  for (i = 0, k = 0; i < ns.n; i++) if (cnt[i] == best) ar->v[k++] = val_num(ns.v[i]);
  free(cnt); free(ns.v);
  return arr_value(ar);
}
static Value f_frequency(Ctx *cx, Value *a, int n)
{
  double *d, *b, *sb; long nd, nb, i, k; int e; Arr *ar; long *cnt;
  if (!(d = numbers(cx, &a[0], &nd, 0, 0, &e))) return val_err(e);
  if (!(b = numbers(cx, &a[1], &nb, 0, 0, &e))) { free(d); return val_err(e); }
  if (!(ar = arr_new((int)nb + 1, 1)) || !(cnt = (long *)calloc((size_t)nb + 1, sizeof(long))) || !(sb = (double *)malloc(((size_t)nb + 1) * sizeof(double)))) {
    free(d); free(b); return val_err(E_NUM);
  }
  memcpy(sb, b, (size_t)nb * sizeof(double));
  qsort(sb, (size_t)nb, sizeof(double), dcmp);
  /* each value goes in the first bin (in order) it is no more than; the bins keep their places */
  for (i = 0; i < nd; i++) {
    long lo = 0, hi = nb;
    while (lo < hi) { long mid = (lo + hi) / 2; if (d[i] <= sb[mid]) hi = mid; else lo = mid + 1; }
    if (lo == nb) cnt[nb]++;
    else for (k = 0; k < nb; k++) if (b[k] == sb[lo]) { cnt[k]++; break; }
  }
  for (k = 0; k <= nb; k++) ar->v[k] = val_num((double)cnt[k]);
  free(d); free(b); free(sb); free(cnt);
  return arr_value(ar);
}
static Value f_prob(Ctx *cx, Value *a, int n)
{
  Pairs p; double lo, hi, s = 0, tot = 0; long i; int e;
  NUMS(2, 1, &lo);
  hi = lo;
  NUMS(3, 1, &hi);
  if ((e = pairs(cx, &a[1], &a[0], &p))) return val_err(e);
  for (i = 0; i < p.n; i++) {
    if (p.y[i] < 0 || p.y[i] > 1) { pairs_free(&p); return val_err(E_NUM); }
    tot += p.y[i];
    if (p.x[i] >= lo && p.x[i] <= hi) s += p.y[i];
  }
  pairs_free(&p);
  if (fabs(tot - 1) > 1e-7) return val_err(E_NUM);
  return val_num(s);
}

/* ------------------------------------------------------------------ two variables */
static Value pair_impl(Ctx *cx, Value *a, int n, int kind)
{
  /* kind: 0 PEARSON, 1 RSQ, 2 SLOPE, 3 INTERCEPT, 4 STEYX, 5 COVARIANCE.P, 6 COVARIANCE.S,
     7 FORECAST(x, known_y, known_x); the first argument is known_y */
  Pairs p; Moments m; double x = 0; int e;
  if (kind == 7) { NUMS(0, 1, &x); a++; }
  if ((e = pairs(cx, &a[0], &a[1], &p))) return val_err(e);
  moments(&p, &m);
  pairs_free(&p);
  switch (kind) {
  case 0: case 1:
    if (m.n < 1 || m.sxx == 0 || m.syy == 0) return val_err(E_DIV0);
    { double r = m.sxy / sqrt(m.sxx * m.syy); return num_or_err(kind ? r * r : r); }
  case 2: case 3: case 7:
    if (m.n < 1 || m.sxx == 0) return val_err(E_DIV0);
    {
      double b = m.sxy / m.sxx;
      return num_or_err(kind == 2 ? b : kind == 3 ? m.my - b * m.mx : m.my + b * (x - m.mx));
    }
  case 4:
    if (m.n < 3 || m.sxx == 0) return val_err(E_DIV0);
    { double r = (m.syy - m.sxy * m.sxy / m.sxx) / (m.n - 2); return num_or_err(sqrt(r < 0 ? 0 : r)); }
  case 5: if (m.n < 1) return val_err(E_DIV0); return num_or_err(m.sxy / m.n);
  default: if (m.n < 2) return val_err(E_DIV0); return num_or_err(m.sxy / (m.n - 1));
  }
}
static Value f_pearson(Ctx *cx, Value *a, int n) { (void)n; return pair_impl(cx, a, 2, 0); }
static Value f_rsq(Ctx *cx, Value *a, int n) { (void)n; return pair_impl(cx, a, 2, 1); }
static Value f_slope(Ctx *cx, Value *a, int n) { (void)n; return pair_impl(cx, a, 2, 2); }
static Value f_intercept(Ctx *cx, Value *a, int n) { (void)n; return pair_impl(cx, a, 2, 3); }
static Value f_steyx(Ctx *cx, Value *a, int n) { (void)n; return pair_impl(cx, a, 2, 4); }
static Value f_covariance_p(Ctx *cx, Value *a, int n) { (void)n; return pair_impl(cx, a, 2, 5); }
static Value f_covariance_s(Ctx *cx, Value *a, int n) { (void)n; return pair_impl(cx, a, 2, 6); }
static Value f_forecast(Ctx *cx, Value *a, int n) { (void)n; return pair_impl(cx, a, 3, 7); }

/* ------------------------------------------------------------------ hypothesis tests */
/* mean and centred sum of squares of the numbers of one argument */
static int mean_ss(Ctx *cx, Value *a, double *mean, double *ss, long *cnt)
{
  double *v; long i; int e;
  if (!(v = numbers(cx, a, cnt, 0, 0, &e))) return e;
  *mean = *ss = 0;
  for (i = 0; i < *cnt; i++) *mean += v[i];
  if (*cnt) *mean /= *cnt;
  for (i = 0; i < *cnt; i++) *ss += (v[i] - *mean) * (v[i] - *mean);
  free(v);
  return 0;
}
static Value f_t_test(Ctx *cx, Value *a, int n)
{
  double x[2], t, df;
  NUMS(2, 2, x);
  x[0] = trunc_(x[0]); x[1] = trunc_(x[1]);
  if ((x[0] != 1 && x[0] != 2) || x[1] < 1 || x[1] > 3) return val_err(E_NUM);
  if (x[1] == 1) {
    Pairs p; Moments m; long i; int e; double md = 0, ss = 0;
    i32 r1, c1, r2, c2;
    arg_shape(&a[0], &r1, &c1); arg_shape(&a[1], &r2, &c2);
    if ((long)r1 * c1 != (long)r2 * c2) return val_err(E_NA);
    if ((e = pairs(cx, &a[0], &a[1], &p))) return val_err(e);
    for (i = 0; i < p.n; i++) md += p.y[i] - p.x[i];
    if (p.n) md /= p.n;
    for (i = 0; i < p.n; i++) { double d = p.y[i] - p.x[i] - md; ss += d * d; }
    m.n = p.n;
    pairs_free(&p);
    if (m.n < 2 || ss == 0) return val_err(E_DIV0);
    t = md / sqrt(ss / (m.n - 1) / m.n);
    df = m.n - 1;
  } else {
    double m1, s1, m2, s2; long n1, n2; int e;
    if ((e = mean_ss(cx, &a[0], &m1, &s1, &n1)) || (e = mean_ss(cx, &a[1], &m2, &s2, &n2))) return val_err(e);
    if (n1 < 2 || n2 < 2) return val_err(E_DIV0);
    if (x[1] == 2) {
      double sp = (s1 + s2) / (n1 + n2 - 2);
      if (sp == 0) return val_err(E_DIV0);
      t = (m1 - m2) / sqrt(sp * (1.0 / n1 + 1.0 / n2));
      df = n1 + n2 - 2;
    } else {
      double v1 = s1 / (n1 - 1) / n1, v2 = s2 / (n2 - 1) / n2;
      if (v1 + v2 == 0) return val_err(E_DIV0);
      t = (m1 - m2) / sqrt(v1 + v2);
      df = (v1 + v2) * (v1 + v2) / (v1 * v1 / (n1 - 1) + v2 * v2 / (n2 - 1));
    }
  }
  return num_or_err(x[0] * t_upper(fabs(t), df));
}
static Value f_f_test(Ctx *cx, Value *a, int n)
{
  double m1, s1, m2, s2, f, p; long n1, n2; int e;
  (void)n;
  if ((e = mean_ss(cx, &a[0], &m1, &s1, &n1)) || (e = mean_ss(cx, &a[1], &m2, &s2, &n2))) return val_err(e);
  if (n1 < 2 || n2 < 2 || s1 == 0 || s2 == 0) return val_err(E_DIV0);
  f = (s1 / (n1 - 1)) / (s2 / (n2 - 1));
  p = f_tail(f, n1 - 1, n2 - 1, 1);
  return num_or_err(2 * (p < 0.5 ? p : 1 - p));
}
static Value f_z_test(Ctx *cx, Value *a, int n)
{
  double x, sigma, m, ss; long cnt; int e;
  NUMS(1, 1, &x);
  if ((e = mean_ss(cx, &a[0], &m, &ss, &cnt))) return val_err(e);
  if (cnt < 1) return val_err(E_NA);
  if (n > 2 && !IS_MISSING(&a[2])) { NUMS(2, 1, &sigma); }
  else { if (cnt < 2) return val_err(E_DIV0); sigma = sqrt(ss / (cnt - 1)); }
  if (sigma <= 0) return val_err(sigma == 0 ? E_DIV0 : E_NUM);
  return num_or_err(ncdf(-(m - x) / (sigma / sqrt((double)cnt))));
}
static Value f_chisq_test(Ctx *cx, Value *a, int n)
{
  i32 r1, c1, r2, c2, r, c;
  double st = 0, df;
  (void)n;
  if (a[0].t == V_ERR) return val_err(a[0].e);
  if (a[1].t == V_ERR) return val_err(a[1].e);
  arg_shape(&a[0], &r1, &c1); arg_shape(&a[1], &r2, &c2);
  if (r1 != r2 || c1 != c2) return val_err(E_NA);
  if ((long)r1 * c1 > 4000000L) return val_err(E_NUM);
  for (r = 0; r < r1; r++)
    for (c = 0; c < c1; c++) {
      const Value *o = arg_at(cx, &a[0], r, c);
      double ov;
      const Value *ex;
      if (o->t == V_ERR) return val_err(o->e);
      if (o->t != V_NUM) continue;
      ov = o->n;
      ex = arg_at(cx, &a[1], r, c);
      if (ex->t == V_ERR) return val_err(ex->e);
      if (ex->t != V_NUM) continue;
      if (ex->n == 0) return val_err(E_DIV0);
      st += (ov - ex->n) * (ov - ex->n) / ex->n;
    }
  if (r1 > 1 && c1 > 1) df = (double)(r1 - 1) * (c1 - 1);
  else df = (double)r1 * c1 - 1;
  if (df < 1) return val_err(E_NA);
  return num_or_err(chi_upper(st, df));
}

/* ================================================================== least squares: LINEST and friends */
/* the data of a regression: n points, k variables (columns of x), y */
typedef struct { long n, k; double *x, *y; int yrow; i32 yrows, ycols; } Reg;
static void reg_free(Reg *g) { free(g->x); free(g->y); }
/* known_y and known_x into g (x by point: x[i * k + j]); known_x missing gives 1, 2, 3 ... */
static int reg_data(Ctx *cx, Value *ky, Value *kx, Reg *g)
{
  Arr *ya, *xa = NULL; int err; long i, j;
  memset(g, 0, sizeof *g);
  if (!(ya = to_array(cx, ky, &err))) return err;
  g->yrows = ya->rows; g->ycols = ya->cols;
  g->n = (long)ya->rows * ya->cols;
  g->yrow = ya->rows == 1 && ya->cols > 1;
  if (kx && !IS_MISSING(kx)) {
    if (!(xa = to_array(cx, kx, &err))) { arr_free(ya); return err; }
    if (xa->rows == ya->rows && xa->cols == ya->cols) g->k = 1;
    else if (ya->cols == 1 && xa->rows == ya->rows) g->k = xa->cols;
    else if (ya->rows == 1 && xa->cols == ya->cols) g->k = xa->rows;
    else { arr_free(ya); arr_free(xa); return E_REF; }
  } else g->k = 1;
  g->y = (double *)malloc((size_t)g->n * sizeof(double) + 1);
  g->x = (double *)malloc((size_t)(g->n * g->k) * sizeof(double) + 1);
  if (!g->y || !g->x) { arr_free(ya); if (xa) arr_free(xa); reg_free(g); return E_NUM; }
  for (i = 0; i < g->n; i++) {
    Value *v = &ya->v[i];
    if (v->t == V_ERR) { err = v->e; goto bad; }
    if (v->t != V_NUM) { err = E_VALUE; goto bad; }
    g->y[i] = v->n;
  }
  for (i = 0; i < g->n; i++)
    for (j = 0; j < g->k; j++) {
      Value *v;
      if (!xa) { g->x[i] = (double)(i + 1); continue; }
      if (g->k == 1) v = &xa->v[i];
      else if (ya->cols == 1) v = &xa->v[i * xa->cols + j];
      else v = &xa->v[j * xa->cols + i];
      if (v->t == V_ERR) { err = v->e; goto bad; }
      if (v->t != V_NUM) { err = E_VALUE; goto bad; }
      g->x[i * g->k + j] = v->n;
    }
  arr_free(ya); if (xa) arr_free(xa);
  return 0;
bad:
  arr_free(ya); if (xa) arr_free(xa); reg_free(g);
  return err;
}
/* the fit: coefficients b[0..k] (b[0] the constant, b[j] for variable j), and what LINEST's
   statistics need. Modified Gram-Schmidt (twice) on the columns; a column that depends on the ones
   before it is dropped (its coefficient 0), as Excel does */
typedef struct { double *b, *se; int *drop; double ssreg, ssres, r2, sey, F, df; int kept; } Fit;
static void fit_free(Fit *f) { free(f->b); free(f->se); free(f->drop); }
static int reg_fit(const Reg *g, int cnst, Fit *f)
{
  long n = g->n, p = g->k + 1, i, j, l, pass;
  double *q, *R, *qy, ym = 0, sst = 0;
  memset(f, 0, sizeof *f);
  q = (double *)calloc((size_t)(n * p) + 1, sizeof(double));
  R = (double *)calloc((size_t)(p * p) + 1, sizeof(double));
  qy = (double *)calloc((size_t)p + 1, sizeof(double));
  f->b = (double *)calloc((size_t)p + 1, sizeof(double));
  f->se = (double *)calloc((size_t)p + 1, sizeof(double));
  f->drop = (int *)calloc((size_t)p + 1, sizeof(int));
  if (!q || !R || !qy || !f->b || !f->se || !f->drop) { free(q); free(R); free(qy); fit_free(f); return E_NUM; }
  /* column 0 the constant (or dropped), then the variables */
  for (i = 0; i < n; i++) {
    q[i * p] = cnst ? 1 : 0;
    for (j = 1; j < p; j++) q[i * p + j] = g->x[i * g->k + j - 1];
  }
  if (!cnst) f->drop[0] = 1;
  for (j = 0; j < p; j++) {
    double norm0 = 0, nrm = 0;
    if (f->drop[j]) continue;
    for (i = 0; i < n; i++) norm0 += q[i * p + j] * q[i * p + j];
    norm0 = sqrt(norm0);
    for (pass = 0; pass < 2; pass++)
      for (l = 0; l < j; l++) {
        double d = 0;
        if (f->drop[l]) continue;
        for (i = 0; i < n; i++) d += q[i * p + l] * q[i * p + j];
        R[l * p + j] += d;
        for (i = 0; i < n; i++) q[i * p + j] -= d * q[i * p + l];
      }
    for (i = 0; i < n; i++) nrm += q[i * p + j] * q[i * p + j];
    nrm = sqrt(nrm);
    if (norm0 == 0 || nrm <= 1e-11 * norm0) { f->drop[j] = 1; continue; }
    R[j * p + j] = nrm;
    for (i = 0; i < n; i++) q[i * p + j] /= nrm;
  }
  for (j = 0; j < p; j++) {
    if (f->drop[j]) continue;
    for (i = 0; i < n; i++) qy[j] += q[i * p + j] * g->y[i];
    f->kept++;
  }
  /* back substitution: R b = Q'y */
  for (j = p - 1; j >= 0; j--) {
    double s = qy[j];
    if (f->drop[j]) continue;
    for (l = j + 1; l < p; l++) if (!f->drop[l]) s -= R[j * p + l] * f->b[l];
    f->b[j] = s / R[j * p + j];
  }
  /* the sums of squares */
  for (i = 0; i < n; i++) ym += g->y[i];
  ym /= n;
  for (i = 0; i < n; i++) {
    double yh = f->b[0], d;
    for (j = 1; j < p; j++) yh += f->b[j] * g->x[i * g->k + j - 1];
    d = g->y[i] - yh;
    f->ssres += d * d;
    sst += cnst ? (g->y[i] - ym) * (g->y[i] - ym) : g->y[i] * g->y[i];
  }
  f->ssreg = sst - f->ssres;
  f->df = (double)(n - f->kept);
  f->r2 = sst == 0 ? 1 : f->ssreg / sst;
  f->sey = f->df > 0 ? sqrt(f->ssres / f->df) : 0;
  {
    double v1 = f->kept - (cnst ? 1 : 0);
    f->F = f->df > 0 && f->ssres > 0 && v1 > 0 ? (f->ssreg / v1) / (f->ssres / f->df) : 0;
  }
  /* standard errors: sigma^2 times the diagonal of (R'R)^-1, the row sums of squares of R^-1 */
  if (f->df > 0) {
    double *ri = (double *)calloc((size_t)(p * p) + 1, sizeof(double)), s2 = f->ssres / f->df;
    if (!ri) { free(q); free(R); free(qy); fit_free(f); return E_NUM; }
    for (j = 0; j < p; j++) {
      if (f->drop[j]) continue;
      ri[j * p + j] = 1 / R[j * p + j];
      for (l = j - 1; l >= 0; l--) {
        double s = 0; long m;
        if (f->drop[l]) continue;
        for (m = l + 1; m <= j; m++) if (!f->drop[m]) s += R[l * p + m] * ri[m * p + j];
        ri[l * p + j] = -s / R[l * p + l];
      }
    }
    for (l = 0; l < p; l++) {
      double s = 0;
      if (f->drop[l]) continue;
      for (j = l; j < p; j++) if (!f->drop[j]) s += ri[l * p + j] * ri[l * p + j];
      f->se[l] = sqrt(s * s2);
    }
    free(ri);
  }
  free(q); free(R); free(qy);
  return 0;
}
static Value linest_impl(Ctx *cx, Value *a, int n, int lg)
{
  Reg g; Fit f; int cnst = 1, stats = 0, e; long i, j, k; Arr *ar;
  BOOLARG(2, cnst); BOOLARG(3, stats);
  if ((e = reg_data(cx, &a[0], n > 1 ? &a[1] : NULL, &g))) return val_err(e);
  if (lg) for (i = 0; i < g.n; i++) { if (g.y[i] <= 0) { reg_free(&g); return val_err(E_NUM); } g.y[i] = log(g.y[i]); }
  if ((e = reg_fit(&g, cnst, &f))) { reg_free(&g); return val_err(e); }
  k = g.k;
  reg_free(&g);
  if (!(ar = arr_new(stats ? 5 : 1, (int)k + 1))) { fit_free(&f); return val_err(E_NUM); }
  /* row 1: m_k ... m_1, b; row 2 their standard errors; then r2, sey; F, df; ssreg, ssresid */
  for (j = 0; j <= k; j++) {
    long c = j == k ? 0 : k - j;               /* column j shows coefficient c (0 = the constant) */
    ar->v[j] = num_or_err(lg ? exp(f.b[c]) : f.b[c]);
    if (stats) ar->v[(int)k + 1 + j] = c == 0 && !cnst ? val_err(E_NA) : num_or_err(f.se[c]);
  }
  if (stats) {
    for (i = 2; i < 5; i++) for (j = 0; j <= k; j++) ar->v[i * (k + 1) + j] = val_err(E_NA);
    ar->v[2 * (k + 1)] = num_or_err(f.r2);
    ar->v[3 * (k + 1)] = f.df > 0 && f.ssres > 0 ? num_or_err(f.F) : val_err(E_NUM);
    ar->v[4 * (k + 1)] = num_or_err(f.ssreg);
    if (k >= 1) {
      ar->v[2 * (k + 1) + 1] = num_or_err(f.sey);
      ar->v[3 * (k + 1) + 1] = num_or_err(f.df);
      ar->v[4 * (k + 1) + 1] = num_or_err(f.ssres);
    }
  }
  fit_free(&f);
  return arr_value(ar);
}
static Value f_linest(Ctx *cx, Value *a, int n) { return linest_impl(cx, a, n, 0); }
static Value f_logest(Ctx *cx, Value *a, int n) { return linest_impl(cx, a, n, 1); }
static Value trend_impl(Ctx *cx, Value *a, int n, int lg)
{
  /* TREND / GROWTH(known_y, [known_x], [new_x], [const]) */
  Reg g; Fit f; int cnst = 1, e; long i, j, m; Arr *nx = NULL, *ar; int rows, cols;
  BOOLARG(3, cnst);
  if ((e = reg_data(cx, &a[0], n > 1 ? &a[1] : NULL, &g))) return val_err(e);
  if (lg) for (i = 0; i < g.n; i++) { if (g.y[i] <= 0) { reg_free(&g); return val_err(E_NUM); } g.y[i] = log(g.y[i]); }
  if ((e = reg_fit(&g, cnst, &f))) { reg_free(&g); return val_err(e); }
  /* the new points: new_x, else known_x (or 1, 2, 3 ...) in known_y's shape */
  if (n > 2 && !IS_MISSING(&a[2])) {
    if (!(nx = to_array(cx, &a[2], &e))) { reg_free(&g); fit_free(&f); return val_err(e); }
    if (g.k == 1) { rows = nx->rows; cols = nx->cols; m = (long)rows * cols; }
    else if (!g.yrow) { if (nx->cols != g.k) { e = E_REF; goto bad; } rows = nx->rows; cols = 1; m = rows; }
    else { if (nx->rows != g.k) { e = E_REF; goto bad; } rows = 1; cols = nx->cols; m = cols; }
  } else {
    m = g.n;
    if (g.k == 1) { rows = g.yrows; cols = g.ycols; }
    else if (!g.yrow) { rows = (int)g.n; cols = 1; }
    else { rows = 1; cols = (int)g.n; }
  }
  if (!(ar = arr_new(rows, cols))) { e = E_NUM; goto bad; }
  for (i = 0; i < m; i++) {
    double yh = f.b[0];
    for (j = 0; j < g.k; j++) {
      double xv;
      if (nx) {
        Value *v = g.k == 1 ? &nx->v[i] : !g.yrow ? &nx->v[i * nx->cols + j] : &nx->v[j * nx->cols + i];
        if (v->t == V_ERR) { arr_free(ar); e = v->e; goto bad; }
        if (v->t != V_NUM) { arr_free(ar); e = E_VALUE; goto bad; }
        xv = v->n;
      } else xv = g.x[i * g.k + j];
      yh += f.b[j + 1] * xv;
    }
    ar->v[i] = num_or_err(lg ? exp(yh) : yh);
  }
  if (nx) arr_free(nx);
  reg_free(&g); fit_free(&f);
  return arr_value(ar);
bad:
  if (nx) arr_free(nx);
  reg_free(&g); fit_free(&f);
  return val_err(e);
}
static Value f_trend(Ctx *cx, Value *a, int n) { return trend_impl(cx, a, n, 0); }
static Value f_growth(Ctx *cx, Value *a, int n) { return trend_impl(cx, a, n, 1); }

/* ================================================================== FORECAST.ETS: exponential smoothing
   The additive error, trend and season model (AAA) of Hyndman, Koehler, Ord and Snyder,
   "Forecasting with Exponential Smoothing" (2008):
     forecast y'(t) = l(t-1) + b(t-1) + s(t-m), error e = y(t) - y'(t)
     l(t) = l(t-1) + b(t-1) + alpha e, b(t) = b(t-1) + alpha beta e, s(t) = s(t-m) + gamma e
   alpha, beta and gamma (each in [0, 1]) minimise the sum of squared errors: a coarse grid, then a
   pattern search. Excel doesn't document its optimiser, so fitted parameters (and the last digits
   of forecasts of noisy data) can differ from Excel's; exact data (straight lines, repeating
   seasons) forecast exactly. The season length is found from the autocorrelation of the detrended
   series: the highest local peak above 2/sqrt(n). */
#define ETS_MAXSEASON 8760
typedef struct { double *y; long n; double t0, step; } Series;
static int ets_series(Ctx *cx, Value *vals, Value *tl, int fill, int agg, Series *s)
{
  Pairs p; long i, j, k, nt, *ord;
  double *t, *v, step = 0;
  int e, missing = 0;
  memset(s, 0, sizeof *s);
  {
    /* the timeline must be all numbers */
    i32 r, c, rr, cc;
    arg_shape(tl, &r, &c); arg_shape(vals, &rr, &cc);
    if ((long)r * c != (long)rr * cc) return E_NA;
    for (rr = 0; rr < r; rr++) for (cc = 0; cc < c; cc++) {
      const Value *x = arg_at(cx, tl, rr, cc);
      if (x->t == V_ERR) return x->e;
      if (x->t != V_NUM) return E_VALUE;
    }
  }
  if ((e = pairs(cx, vals, tl, &p))) return e;
  if (p.n < 2) { pairs_free(&p); return E_NUM; }
  /* sort by time, then combine equal times */
  ord = (long *)malloc((size_t)p.n * sizeof(long));
  t = (double *)malloc((size_t)p.n * sizeof(double));
  v = (double *)malloc((size_t)p.n * sizeof(double));
  if (!ord || !t || !v) { free(ord); free(t); free(v); pairs_free(&p); return E_NUM; }
  for (i = 0; i < p.n; i++) ord[i] = i;
  for (i = 1; i < p.n; i++) {               /* insertion sort: timelines are nearly always in order */
    long o = ord[i];
    for (j = i; j > 0 && p.x[ord[j - 1]] > p.x[o]; j--) ord[j] = ord[j - 1];
    ord[j] = o;
  }
  for (i = 0, nt = 0; i < p.n; i = j) {
    double sum = 0, mx = p.y[ord[i]], mn = mx, *grp = &v[nt];
    long cntg;
    for (j = i; j < p.n && p.x[ord[j]] == p.x[ord[i]]; j++) {
      double y = p.y[ord[j]];
      sum += y; if (y > mx) mx = y; if (y < mn) mn = y;
    }
    cntg = j - i;
    t[nt] = p.x[ord[i]];
    switch (agg) {
    case 2: case 3: *grp = (double)cntg; break;
    case 4: *grp = mx; break;
    case 6: *grp = mn; break;
    case 7: *grp = sum; break;
    case 5: {
      double *tmp = (double *)malloc((size_t)cntg * sizeof(double));
      if (!tmp) { *grp = sum / cntg; break; }
      for (k = 0; k < cntg; k++) tmp[k] = p.y[ord[i + k]];
      qsort(tmp, (size_t)cntg, sizeof(double), dcmp);
      *grp = cntg % 2 ? tmp[cntg / 2] : (tmp[cntg / 2 - 1] + tmp[cntg / 2]) / 2;
      free(tmp);
      break;
    }
    default: *grp = sum / cntg;
    }
    nt++;
  }
  pairs_free(&p); free(ord);
  if (nt < 2) { free(t); free(v); return E_NUM; }
  /* the step: the smallest gap; every gap a whole number of steps */
  for (i = 1; i < nt; i++) if (step == 0 || t[i] - t[i - 1] < step) step = t[i] - t[i - 1];
  for (i = 1; i < nt; i++) {
    double q = (t[i] - t[i - 1]) / step;
    if (fabs(q - floor(q + 0.5)) > 1e-6) { free(t); free(v); return E_NUM; }
    missing += (int)floor(q + 0.5) - 1;
  }
  if (missing > 0.3 * (nt + missing) + 1e-9) { free(t); free(v); return E_NUM; }
  s->n = nt + missing;
  if (!(s->y = (double *)malloc((size_t)s->n * sizeof(double)))) { free(t); free(v); return E_NUM; }
  for (i = 0, k = 0; i < nt; i++) {
    long pos = (long)floor((t[i] - t[0]) / step + 0.5);
    while (k < pos) {                           /* a gap: interpolate or zero */
      double fr = (double)(k - (long)floor((t[i - 1] - t[0]) / step + 0.5)) / (pos - (long)floor((t[i - 1] - t[0]) / step + 0.5));
      s->y[k] = fill ? v[i - 1] + fr * (v[i] - v[i - 1]) : 0;
      k++;
    }
    s->y[k++] = v[i];
  }
  s->t0 = t[0]; s->step = step;
  free(t); free(v);
  return 0;
}
static void line_fit(const double *y, long n, double *a, double *b)   /* y = a + b i */
{
  double mx = (n - 1) / 2.0, my = 0, sxy = 0, sxx = 0;
  long i;
  for (i = 0; i < n; i++) my += y[i];
  my /= n;
  for (i = 0; i < n; i++) { sxy += (i - mx) * (y[i] - my); sxx += (i - mx) * (i - mx); }
  *b = sxx > 0 ? sxy / sxx : 0;
  *a = my - *b * mx;
}
static long ets_detect(const double *y, long n)
{
  double a, b, var = 0, scale = 0, *r, *acf, bound, best_r;
  long i, k, maxlag, best = 0;
  if (n < 4) return 1;
  if (!(r = (double *)malloc((size_t)n * sizeof(double)))) return 1;
  line_fit(y, n, &a, &b);
  for (i = 0; i < n; i++) { r[i] = y[i] - (a + b * i); var += r[i] * r[i]; scale += y[i] * y[i]; }
  maxlag = n / 2; if (maxlag > ETS_MAXSEASON) maxlag = ETS_MAXSEASON;
  if (!(var > 1e-20 * (scale > 0 ? scale : 1)) || !(acf = (double *)malloc((size_t)(maxlag + 2) * sizeof(double)))) { free(r); return 1; }
  for (k = 0; k <= maxlag + 1 && k < n; k++) {
    double s = 0;
    for (i = k; i < n; i++) s += r[i] * r[i - k];
    acf[k] = s / var;
  }
  bound = 2 / sqrt((double)n); best_r = bound;
  for (k = 2; k <= maxlag; k++) {
    double next = k + 1 < n && k + 1 <= maxlag + 1 ? acf[k + 1] : -BIG;
    if (acf[k] > acf[k - 1] && acf[k] >= next && acf[k] > best_r) { best = k; best_r = acf[k]; }
  }
  free(acf); free(r);
  return best ? best : 1;
}
typedef struct { double l, b, *s; long m; } EtsState;
/* the starting states: level and trend from a line through the first seasons' means, the season
   from the first two seasons' differences from it */
static void ets_init(const double *y, long n, long m, EtsState *st)
{
  long i;
  if (m >= 2 && n >= 2 * m) {
    double m1 = 0, m2 = 0, b;
    for (i = 0; i < m; i++) { m1 += y[i]; m2 += y[m + i]; }
    m1 /= m; m2 /= m;
    b = (m2 - m1) / m;
    st->b = b;
    st->l = m1 - b * ((m - 1) / 2.0 + 1);        /* the level at t = -1 */
    {
      double mean = 0;
      for (i = 0; i < m; i++) {
        st->s[i] = ((y[i] - (m1 + (i - (m - 1) / 2.0) * b)) + (y[m + i] - (m2 + (i - (m - 1) / 2.0) * b))) / 2;
        mean += st->s[i];
      }
      mean /= m;
      for (i = 0; i < m; i++) st->s[i] -= mean;
    }
  } else {
    double a, b;
    long k = n < 10 ? n : 10;
    line_fit(y, k, &a, &b);
    st->l = a - b; st->b = b;
    for (i = 0; i < m; i++) st->s[i] = 0;
  }
}
/* run the model over the data; returns the sum of squared one-step errors; errors[] if given */
static double ets_run(const double *y, long n, long m, const EtsState *init, const double *p, EtsState *out, double *errors)
{
  double l = init->l, b = init->b, sse = 0, *s = out->s;
  long t;
  if (m > 0) memcpy(s, init->s, (size_t)m * sizeof(double));
  for (t = 0; t < n; t++) {
    double sv = m > 0 ? s[t % m] : 0, f = l + b + sv, e = y[t] - f, nl;
    nl = l + b + p[0] * e;
    b = b + p[0] * p[1] * e;
    l = nl;
    if (m > 0) s[t % m] = sv + p[2] * e;
    sse += e * e;
    if (errors) errors[t] = e;
  }
  out->l = l; out->b = b; out->m = m;
  return sse;
}
typedef struct { double p[3], sse; long m, n; EtsState st; double *err; } EtsFit;
static int ets_fit(const double *y, long n, long m, EtsFit *f)
{
  EtsState init, work;
  double best[3], bsse, step;
  int i, j, k, np = m > 0 ? 3 : 2;
  memset(f, 0, sizeof *f);
  if (m < 2) m = 0;
  init.s = (double *)calloc((size_t)m + 1, sizeof(double));
  work.s = (double *)calloc((size_t)m + 1, sizeof(double));
  f->st.s = (double *)calloc((size_t)m + 1, sizeof(double));
  f->err = (double *)calloc((size_t)n + 1, sizeof(double));
  if (!init.s || !work.s || !f->st.s || !f->err) { free(init.s); free(work.s); free(f->st.s); free(f->err); return E_NUM; }
  init.m = m;
  ets_init(y, n, m, &init);
  /* a coarse grid over the parameters, then a pattern search from its best point */
  best[0] = 0.5; best[1] = 0.1; best[2] = m > 0 ? 0.1 : 0;
  bsse = ets_run(y, n, m, &init, best, &work, NULL);
  for (i = 1; i <= 9; i += 2)
    for (j = 0; j <= 9; j += 3)
      for (k = 0; k <= (m > 0 ? 9 : 0); k += 3) {
        double p[3], s;
        p[0] = i / 10.0; p[1] = j / 10.0; p[2] = k / 10.0;
        s = ets_run(y, n, m, &init, p, &work, NULL);
        if (s < bsse - 1e-12 * bsse) { bsse = s; memcpy(best, p, sizeof best); }
      }
  for (step = 0.05; step > 1e-7; ) {
    int moved = 0, d;
    for (d = 0; d < np; d++) {
      int sg;
      for (sg = -1; sg <= 1; sg += 2) {
        double p[3], s;
        memcpy(p, best, sizeof p);
        p[d] += sg * step;
        if (p[d] < 0) p[d] = 0;
        if (p[d] > 1) p[d] = 1;
        s = ets_run(y, n, m, &init, p, &work, NULL);
        if (s < bsse - 1e-13 * bsse) { bsse = s; memcpy(best, p, sizeof best); moved = 1; }
      }
    }
    if (!moved) step /= 2;
  }
  memcpy(f->p, best, sizeof best);
  f->sse = ets_run(y, n, m, &init, best, &f->st, f->err);
  f->m = m; f->n = n;
  free(init.s); free(work.s);
  return 0;
}
static void ets_free(EtsFit *f) { free(f->st.s); free(f->err); }
static double ets_at(const EtsFit *f, double k)        /* k steps after the last point */
{
  double s = 0;
  if (f->m > 0) s = f->st.s[(long)dmod(f->n - 1 + k, (double)f->m)];
  return f->st.l + k * f->st.b + s;
}
static double ets_forecast(const EtsFit *f, double h)
{
  double lo = floor(h), v = ets_at(f, lo);
  return h == lo ? v : v + (h - lo) * (ets_at(f, lo + 1) - v);
}
static double ets_sd(const EtsFit *f, double h)
{
  double s2 = f->sse / (f->n > 0 ? f->n : 1), sum = 0, a = f->p[0], ab = f->p[0] * f->p[1], g = f->p[2];
  long j, k = (long)ceil(h < 1 ? 1 : h) - 1;
  for (j = 1; j <= k; j++) { double c = a + ab * j + (f->m > 0 && j % f->m == 0 ? g : 0); sum += c * c; }
  return sqrt(s2 * (1 + sum));
}
/* the options at a[si] (seasonality), a[ci] (data completion), a[gi] (aggregation) */
static int ets_options(Ctx *cx, Value *a, int n, int si, int ci, int gi, long *season, int *fill, int *agg)
{
  double x; int e;
  *season = -1; *fill = 1; *agg = 1;
  if (si >= 0 && si < n && !IS_MISSING(&a[si])) {
    if ((e = arg_num(cx, &a[si], &x))) return e;
    x = trunc_(x);
    if (x < 0 || x > ETS_MAXSEASON) return E_NUM;
    *season = x == 1 ? -1 : (long)x;
  }
  if (ci < n && !IS_MISSING(&a[ci])) {
    if ((e = arg_num(cx, &a[ci], &x))) return e;
    if (x != 0 && x != 1) return E_NUM;
    *fill = x != 0;
  }
  if (gi < n && !IS_MISSING(&a[gi])) {
    if ((e = arg_num(cx, &a[gi], &x))) return e;
    x = trunc_(x);
    if (x < 1 || x > 7) return E_NUM;
    *agg = (int)x;
  }
  return 0;
}
static Value ets_impl(Ctx *cx, Value *a, int n, int kind)
{
  /* kind 0 FORECAST.ETS(target, values, timeline, [seasonality], [completion], [aggregation]),
     1 FORECAST.ETS.CONFINT(target, values, timeline, [level], [seasonality], [completion], [aggregation]),
     2 FORECAST.ETS.SEASONALITY(values, timeline, [completion], [aggregation]),
     3 FORECAST.ETS.STAT(values, timeline, type, [seasonality], [completion], [aggregation]) */
  double target = 0, level = 0.95, stat = 0, h = 0, r = 0;
  long season, m; int fill, agg, e, vi = kind >= 2 ? 0 : 1;
  Series s; EtsFit f;
  if (kind < 2) NUMS(0, 1, &target);
  if (kind == 1) { NUMS(3, 1, &level); if (level <= 0 || level >= 1) return val_err(E_NUM); }
  if (kind == 3) { NUMS(2, 1, &stat); stat = trunc_(stat); if (stat < 1 || stat > 8) return val_err(E_NUM); }
  if ((e = ets_options(cx, a, n, kind == 0 ? 3 : kind == 1 ? 4 : kind == 2 ? -1 : 3,
                       kind == 0 ? 4 : kind == 1 ? 5 : kind == 2 ? 2 : 4,
                       kind == 0 ? 5 : kind == 1 ? 6 : kind == 2 ? 3 : 5, &season, &fill, &agg))) return val_err(e);
  if ((e = ets_series(cx, &a[vi], &a[vi + 1], fill, agg, &s))) return val_err(e);
  m = season < 0 ? ets_detect(s.y, s.n) : season;
  if (m > s.n / 2 && m > 1) m = 1;
  if (kind == 2) { free(s.y); return val_num((double)m); }
  if (kind == 3 && stat == 8) { free(s.y); return val_num(s.step); }
  if (kind < 2) {
    h = (target - s.t0) / s.step - (s.n - 1);
    if (h < -1e-9 || h > 1e9) { free(s.y); return val_err(E_NUM); }
    if (h < 0) h = 0;
  }
  if ((e = ets_fit(s.y, s.n, m, &f))) { free(s.y); return val_err(e); }
  if (kind == 0) r = ets_forecast(&f, h);
  else if (kind == 1) r = ninv((1 + level) / 2) * ets_sd(&f, h);
  else {
    long i; double mae = 0, k;
    for (i = 0; i < s.n; i++) mae += fabs(f.err[i]);
    mae /= s.n;
    switch ((int)stat) {
    case 1: case 2: case 3: r = f.p[(int)stat - 1]; break;
    case 4:
      for (i = 1, k = 0; i < s.n; i++) k += fabs(s.y[i] - s.y[i - 1]);
      k /= s.n - 1;
      if (k == 0) { ets_free(&f); free(s.y); return val_err(E_DIV0); }
      r = mae / k; break;
    case 5:
      for (i = 0, k = 0, r = 0; i < s.n; i++) {
        double d = (fabs(s.y[i]) + fabs(s.y[i] - f.err[i])) / 2;
        if (d > 0) { r += fabs(f.err[i]) / d; k++; }
      }
      r = k > 0 ? r / k : 0; break;
    case 6: r = mae; break;
    default:
      for (i = 0, r = 0; i < s.n; i++) r += f.err[i] * f.err[i];
      r = sqrt(r / s.n);
    }
  }
  ets_free(&f); free(s.y);
  return num_or_err(r);
}
static Value f_forecast_ets(Ctx *cx, Value *a, int n) { return ets_impl(cx, a, n, 0); }
static Value f_forecast_ets_confint(Ctx *cx, Value *a, int n) { return ets_impl(cx, a, n, 1); }
static Value f_forecast_ets_seasonality(Ctx *cx, Value *a, int n) { return ets_impl(cx, a, n, 2); }
static Value f_forecast_ets_stat(Ctx *cx, Value *a, int n) { return ets_impl(cx, a, n, 3); }

/* ------------------------------------------------------------------ USDOLLAR: DOLLAR by its old name */
static Value f_usdollar(Ctx *cx, Value *a, int n)
{
  int id = func_lookup("DOLLAR");
  if (id < 0) return val_err(E_NAME);
  return func_table[id].fn(cx, a, n);
}

/* ================================================================== the table, in name order */
#define X FF_XLFN
const FuncDef func_stat_defs[] = {
  { "BETA.DIST", 4, 6, X, f_beta_dist },
  { "BETA.INV", 3, 5, X, f_beta_inv },
  { "BETADIST", 3, 5, 0, f_betadist },
  { "BETAINV", 3, 5, 0, f_beta_inv },
  { "BINOM.DIST", 4, 4, X, f_binom_dist },
  { "BINOM.DIST.RANGE", 3, 4, X, f_binom_dist_range },
  { "BINOM.INV", 3, 3, X, f_binom_inv },
  { "BINOMDIST", 4, 4, 0, f_binom_dist },
  { "CHIDIST", 2, 2, 0, f_chisq_dist_rt },
  { "CHIINV", 2, 2, 0, f_chisq_inv_rt },
  { "CHISQ.DIST", 3, 3, X, f_chisq_dist },
  { "CHISQ.DIST.RT", 2, 2, X, f_chisq_dist_rt },
  { "CHISQ.INV", 2, 2, X, f_chisq_inv },
  { "CHISQ.INV.RT", 2, 2, X, f_chisq_inv_rt },
  { "CHISQ.TEST", 2, 2, X, f_chisq_test },
  { "CHITEST", 2, 2, 0, f_chisq_test },
  { "CONFIDENCE", 3, 3, 0, f_confidence_norm },
  { "CONFIDENCE.NORM", 3, 3, X, f_confidence_norm },
  { "CONFIDENCE.T", 3, 3, X, f_confidence_t },
  { "COVAR", 2, 2, 0, f_covariance_p },
  { "COVARIANCE.P", 2, 2, X, f_covariance_p },
  { "COVARIANCE.S", 2, 2, X, f_covariance_s },
  { "CRITBINOM", 3, 3, 0, f_binom_inv },
  { "DEVSQ", 1, 255, 0, f_devsq },
  { "EXPON.DIST", 3, 3, X, f_expon_dist },
  { "EXPONDIST", 3, 3, 0, f_expon_dist },
  { "F.DIST", 4, 4, X, f_f_dist },
  { "F.DIST.RT", 3, 3, X, f_f_dist_rt },
  { "F.INV", 3, 3, X, f_f_inv },
  { "F.INV.RT", 3, 3, X, f_f_inv_rt },
  { "F.TEST", 2, 2, X, f_f_test },
  { "FDIST", 3, 3, 0, f_f_dist_rt },
  { "FINV", 3, 3, 0, f_f_inv_rt },
  { "FISHER", 1, 1, 0, f_fisher },
  { "FISHERINV", 1, 1, 0, f_fisherinv },
  { "FORECAST", 3, 3, 0, f_forecast },
  { "FORECAST.ETS", 3, 6, X, f_forecast_ets },
  { "FORECAST.ETS.CONFINT", 3, 7, X, f_forecast_ets_confint },
  { "FORECAST.ETS.SEASONALITY", 2, 4, X, f_forecast_ets_seasonality },
  { "FORECAST.ETS.STAT", 3, 6, X, f_forecast_ets_stat },
  { "FORECAST.LINEAR", 3, 3, X, f_forecast },
  { "FREQUENCY", 2, 2, 0, f_frequency },
  { "FTEST", 2, 2, 0, f_f_test },
  { "GAMMA", 1, 1, X, f_gamma },
  { "GAMMA.DIST", 4, 4, X, f_gamma_dist },
  { "GAMMA.INV", 3, 3, X, f_gamma_inv },
  { "GAMMADIST", 4, 4, 0, f_gamma_dist },
  { "GAMMAINV", 3, 3, 0, f_gamma_inv },
  { "GAMMALN", 1, 1, 0, f_gammaln },
  { "GAMMALN.PRECISE", 1, 1, X, f_gammaln },
  { "GAUSS", 1, 1, X, f_gauss },
  { "GEOMEAN", 1, 255, 0, f_geomean },
  { "GROWTH", 1, 4, 0, f_growth },
  { "HARMEAN", 1, 255, 0, f_harmean },
  { "HYPGEOM.DIST", 5, 5, X, f_hypgeom_dist },
  { "HYPGEOMDIST", 4, 4, 0, f_hypgeom_dist },
  { "INTERCEPT", 2, 2, 0, f_intercept },
  { "KURT", 1, 255, 0, f_kurt },
  { "LINEST", 1, 4, 0, f_linest },
  { "LOGEST", 1, 4, 0, f_logest },
  { "LOGINV", 3, 3, 0, f_lognorm_inv },
  { "LOGNORM.DIST", 4, 4, X, f_lognorm_dist },
  { "LOGNORM.INV", 3, 3, X, f_lognorm_inv },
  { "LOGNORMDIST", 3, 3, 0, f_lognorm_dist },
  { "MODE.MULT", 1, 255, X, f_mode_mult },
  { "NEGBINOM.DIST", 4, 4, X, f_negbinom_dist },
  { "NEGBINOMDIST", 3, 3, 0, f_negbinom_dist },
  { "NORM.DIST", 4, 4, X, f_norm_dist },
  { "NORM.INV", 3, 3, X, f_norm_inv },
  { "NORM.S.DIST", 2, 2, X, f_norm_s_dist },
  { "NORM.S.INV", 1, 1, X, f_norm_s_inv },
  { "NORMDIST", 4, 4, 0, f_normdist },
  { "NORMINV", 3, 3, 0, f_norm_inv },
  { "NORMSDIST", 1, 1, 0, f_normsdist },
  { "NORMSINV", 1, 1, 0, f_norm_s_inv },
  { "PEARSON", 2, 2, 0, f_pearson },
  { "PERCENTRANK", 2, 3, 0, f_percentrank_inc },
  { "PERCENTRANK.EXC", 2, 3, X, f_percentrank_exc },
  { "PERCENTRANK.INC", 2, 3, X, f_percentrank_inc },
  { "PHI", 1, 1, X, f_phi },
  { "POISSON", 3, 3, 0, f_poisson_dist },
  { "POISSON.DIST", 3, 3, X, f_poisson_dist },
  { "PROB", 3, 4, 0, f_prob },
  { "QUARTILE.EXC", 2, 2, X, f_quartile_exc },
  { "RANK.AVG", 2, 3, X, f_rank_avg },
  { "RSQ", 2, 2, 0, f_rsq },
  { "SKEW", 1, 255, 0, f_skew },
  { "SKEW.P", 1, 255, X, f_skew_p },
  { "SLOPE", 2, 2, 0, f_slope },
  { "STANDARDIZE", 3, 3, 0, f_standardize },
  { "STDEVPA", 1, 255, 0, f_stdevpa },
  { "STEYX", 2, 2, 0, f_steyx },
  { "T.DIST", 3, 3, X, f_t_dist },
  { "T.DIST.2T", 2, 2, X, f_t_dist_2t },
  { "T.DIST.RT", 2, 2, X, f_t_dist_rt },
  { "T.INV", 2, 2, X, f_t_inv },
  { "T.INV.2T", 2, 2, X, f_t_inv_2t },
  { "T.TEST", 4, 4, X, f_t_test },
  { "TDIST", 3, 3, 0, f_tdist },
  { "TINV", 2, 2, 0, f_t_inv_2t },
  { "TREND", 1, 4, 0, f_trend },
  { "TRIMMEAN", 2, 2, 0, f_trimmean },
  { "TTEST", 4, 4, 0, f_t_test },
  { "USDOLLAR", 1, 2, 0, f_usdollar },
  { "VARA", 1, 255, 0, f_vara },
  { "VARPA", 1, 255, 0, f_varpa },
  { "WEIBULL", 4, 4, 0, f_weibull_dist },
  { "WEIBULL.DIST", 4, 4, X, f_weibull_dist },
  { "Z.TEST", 2, 3, X, f_z_test },
  { "ZTEST", 2, 3, 0, f_z_test },
};
#undef X
const int func_stat_count = (int)(sizeof func_stat_defs / sizeof func_stat_defs[0]);
