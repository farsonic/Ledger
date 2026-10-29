/*
 * func.c - the worksheet functions. Behaviour follows Excel's documentation: argument coercion
 * (numbers typed straight into SUM count, text in cells is skipped), error propagation, and the
 * same results for the edge cases people meet (ROUND half away from zero, MOD's sign, ...).
 */
#include "calc.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>
#include <ctype.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* ------------------------------------------------------------------ number gathering */
/* the numbers of a list of arguments, as SUM/AVERAGE/MIN/... see them: numbers anywhere; logical
   values and numeric text only when typed straight in (or, with all = 1, everywhere: AVERAGEA ...) */
int nums_add(Nums *ns, double d)
{
  if (ns->n == ns->cap) {
    long cap = ns->cap ? ns->cap * 2 : 64;
    double *nv;
    if (cap > 4000000L) { ns->err = E_NUM; return 1; }
    if (!(nv = (double *)realloc(ns->v, (size_t)cap * sizeof(double)))) { ns->err = E_NUM; return 1; }
    ns->v = nv; ns->cap = cap;
  }
  ns->v[ns->n++] = d;
  return 0;
}
static int nums_cb(Ctx *cx, const Value *v, int direct, void *arg)
{
  Nums *ns = (Nums *)arg;
  double d;
  (void)cx;
  switch (v->t) {
  case V_NUM: return nums_add(ns, v->n);
  case V_ERR: ns->err = v->e; return 1;
  case V_BOOL: if (direct || ns->all) return nums_add(ns, v->e); return 0;
  case V_STR:
    if (direct) { if (to_num(v, &d)) { ns->err = E_VALUE; return 1; } return nums_add(ns, d); }
    if (ns->all) return nums_add(ns, 0);
    return 0;
  case V_EMPTY: if (direct) return nums_add(ns, 0); return 0;
  default: return 0;
  }
}
int gather(Ctx *cx, Value *args, int argc, Nums *ns, int all)
{
  int i;
  memset(ns, 0, sizeof *ns);
  ns->all = all;
  for (i = 0; i < argc && !ns->err; i++) {
    if (args[i].t == V_ERR) { ns->err = args[i].e; break; }
    each_value(cx, &args[i], nums_cb, ns);
  }
  return ns->err;
}
Value num_or_err(double d)
{
  if (!(d == d) || d > 1.7976931348623157e308 || d < -1.7976931348623157e308) return val_err(E_NUM);
  return val_num(d);
}
int dcmp(const void *a, const void *b) { double x = *(const double *)a, y = *(const double *)b; return x < y ? -1 : x > y; }

/* ------------------------------------------------------------------ maths */
static Value f_sum(Ctx *cx, Value *a, int n) { Nums ns; double s = 0; long i; if (gather(cx, a, n, &ns, 0)) { free(ns.v); return val_err(ns.err); } for (i = 0; i < ns.n; i++) s += ns.v[i]; free(ns.v); return num_or_err(s); }
static Value f_product(Ctx *cx, Value *a, int n) { Nums ns; double s = 1; long i; if (gather(cx, a, n, &ns, 0)) { free(ns.v); return val_err(ns.err); } for (i = 0; i < ns.n; i++) s *= ns.v[i]; if (!ns.n) s = 0; free(ns.v); return num_or_err(s); }
static Value f_sumsq(Ctx *cx, Value *a, int n) { Nums ns; double s = 0; long i; if (gather(cx, a, n, &ns, 0)) { free(ns.v); return val_err(ns.err); } for (i = 0; i < ns.n; i++) s += ns.v[i] * ns.v[i]; free(ns.v); return num_or_err(s); }
static Value avg_impl(Ctx *cx, Value *a, int n, int all)
{
  Nums ns; double s = 0; long i;
  if (gather(cx, a, n, &ns, all)) { free(ns.v); return val_err(ns.err); }
  if (!ns.n) { free(ns.v); return val_err(E_DIV0); }
  for (i = 0; i < ns.n; i++) s += ns.v[i];
  s /= ns.n;
  free(ns.v);
  return num_or_err(s);
}
static Value f_average(Ctx *cx, Value *a, int n) { return avg_impl(cx, a, n, 0); }
static Value f_averagea(Ctx *cx, Value *a, int n) { return avg_impl(cx, a, n, 1); }
static Value minmax(Ctx *cx, Value *a, int n, int max, int all)
{
  Nums ns; double m; long i;
  if (gather(cx, a, n, &ns, all)) { free(ns.v); return val_err(ns.err); }
  if (!ns.n) { free(ns.v); return val_num(0); }
  m = ns.v[0];
  for (i = 1; i < ns.n; i++) if (max ? ns.v[i] > m : ns.v[i] < m) m = ns.v[i];
  free(ns.v);
  return val_num(m);
}
static Value f_min(Ctx *cx, Value *a, int n) { return minmax(cx, a, n, 0, 0); }
static Value f_max(Ctx *cx, Value *a, int n) { return minmax(cx, a, n, 1, 0); }
static Value f_mina(Ctx *cx, Value *a, int n) { return minmax(cx, a, n, 0, 1); }
static Value f_maxa(Ctx *cx, Value *a, int n) { return minmax(cx, a, n, 1, 1); }
/* COUNT counts numbers (and dates); typed-in numeric text and logicals count too */
typedef struct { long n; int kind; } Cnt;
static int count_cb(Ctx *cx, const Value *v, int direct, void *arg)
{
  Cnt *c = (Cnt *)arg;
  double d;
  (void)cx;
  if (c->kind == 0) { if (v->t == V_NUM || (direct && (v->t == V_BOOL || (v->t == V_STR && !to_num(v, &d))))) c->n++; }
  else if (c->kind == 1) { if (v->t != V_EMPTY) c->n++; }
  else if (v->t == V_EMPTY || (v->t == V_STR && !v->s[0])) c->n++;
  return 0;
}
static Value count_impl(Ctx *cx, Value *a, int n, int kind)
{
  Cnt c;
  int i;
  c.n = 0; c.kind = kind;
  for (i = 0; i < n; i++) {
    if (kind == 1 && a[i].t != V_REF && a[i].t != V_ARR && !IS_MISSING(&a[i])) { c.n++; continue; }
    each_value(cx, &a[i], count_cb, &c);
  }
  return val_num((double)c.n);
}
static Value f_count(Ctx *cx, Value *a, int n) { return count_impl(cx, a, n, 0); }
static Value f_counta(Ctx *cx, Value *a, int n) { return count_impl(cx, a, n, 1); }
static Value f_countblank(Ctx *cx, Value *a, int n)
{
  /* the empty cells of the range: its size minus the non-empty ones */
  i32 rows, cols;
  Cnt c;
  (void)n;
  if (a[0].t != V_REF) return val_err(E_VALUE);
  arg_shape(&a[0], &rows, &cols);
  c.n = 0; c.kind = 1;
  each_value(cx, &a[0], count_cb, &c);
  {
    /* empty text counts as blank too */
    Cnt e; e.n = 0; e.kind = 3;
    (void)e;
  }
  return val_num((double)rows * cols - c.n);
}
/* one-argument maths */
typedef double (*D1)(double);
static Value math1(Ctx *cx, Value *a, D1 fn, int domain)
{
  double x, r;
  int e = arg_num(cx, &a[0], &x);
  if (e) return val_err(e);
  if (domain == 1 && x < 0) return val_err(E_NUM);
  if (domain == 2 && x <= 0) return val_err(E_NUM);
  if (domain == 3 && (x < -1 || x > 1)) return val_err(E_NUM);
  r = fn(x);
  return num_or_err(r);
}
static double d_sign(double x) { return x > 0 ? 1 : x < 0 ? -1 : 0; }
static double d_int(double x) { return floor(x); }
static double d_deg(double x) { return x * 180 / M_PI; }
static double d_rad(double x) { return x * M_PI / 180; }
static Value f_abs(Ctx *cx, Value *a, int n) { (void)n; return math1(cx, a, fabs, 0); }
static Value f_sqrt(Ctx *cx, Value *a, int n) { (void)n; return math1(cx, a, sqrt, 1); }
static Value f_exp(Ctx *cx, Value *a, int n) { (void)n; return math1(cx, a, exp, 0); }
static Value f_ln(Ctx *cx, Value *a, int n) { (void)n; return math1(cx, a, log, 2); }
static Value f_log10(Ctx *cx, Value *a, int n) { (void)n; return math1(cx, a, log10, 2); }
static Value f_sin(Ctx *cx, Value *a, int n) { (void)n; return math1(cx, a, sin, 0); }
static Value f_cos(Ctx *cx, Value *a, int n) { (void)n; return math1(cx, a, cos, 0); }
static Value f_tan(Ctx *cx, Value *a, int n) { (void)n; return math1(cx, a, tan, 0); }
static Value f_asin(Ctx *cx, Value *a, int n) { (void)n; return math1(cx, a, asin, 3); }
static Value f_acos(Ctx *cx, Value *a, int n) { (void)n; return math1(cx, a, acos, 3); }
static Value f_atan(Ctx *cx, Value *a, int n) { (void)n; return math1(cx, a, atan, 0); }
static Value f_sinh(Ctx *cx, Value *a, int n) { (void)n; return math1(cx, a, sinh, 0); }
static Value f_cosh(Ctx *cx, Value *a, int n) { (void)n; return math1(cx, a, cosh, 0); }
static Value f_tanh(Ctx *cx, Value *a, int n) { (void)n; return math1(cx, a, tanh, 0); }
static Value f_sign(Ctx *cx, Value *a, int n) { (void)n; return math1(cx, a, d_sign, 0); }
static Value f_int(Ctx *cx, Value *a, int n) { (void)n; return math1(cx, a, d_int, 0); }
static Value f_degrees(Ctx *cx, Value *a, int n) { (void)n; return math1(cx, a, d_deg, 0); }
static Value f_radians(Ctx *cx, Value *a, int n) { (void)n; return math1(cx, a, d_rad, 0); }
static Value f_pi(Ctx *cx, Value *a, int n) { (void)cx; (void)a; (void)n; return val_num(M_PI); }
static Value f_atan2(Ctx *cx, Value *a, int n)
{
  double x, y; int e;
  (void)n;
  if ((e = arg_num(cx, &a[0], &x)) || (e = arg_num(cx, &a[1], &y))) return val_err(e);
  if (x == 0 && y == 0) return val_err(E_DIV0);
  return val_num(atan2(y, x));
}
static Value f_log(Ctx *cx, Value *a, int n)
{
  double x, base = 10; int e;
  if ((e = arg_num(cx, &a[0], &x))) return val_err(e);
  if (n > 1 && !IS_MISSING(&a[1]) && (e = arg_num(cx, &a[1], &base))) return val_err(e);
  if (x <= 0 || base <= 0) return val_err(E_NUM);
  if (base == 1) return val_err(E_DIV0);
  return num_or_err(log(x) / log(base));
}
static Value f_power(Ctx *cx, Value *a, int n)
{
  double x, y; int e;
  (void)n;
  if ((e = arg_num(cx, &a[0], &x)) || (e = arg_num(cx, &a[1], &y))) return val_err(e);
  if (x == 0 && y == 0) return val_err(E_NUM);
  if (x == 0 && y < 0) return val_err(E_DIV0);
  if (x < 0 && y != floor(y)) return val_err(E_NUM);
  return num_or_err(pow(x, y));
}
static Value f_mod(Ctx *cx, Value *a, int n)
{
  double x, y, r; int e;
  (void)n;
  if ((e = arg_num(cx, &a[0], &x)) || (e = arg_num(cx, &a[1], &y))) return val_err(e);
  if (y == 0) return val_err(E_DIV0);
  r = x - y * floor(x / y);                 /* the sign of the divisor, as Excel */
  return val_num(r);
}
static Value f_quotient(Ctx *cx, Value *a, int n)
{
  double x, y; int e;
  (void)n;
  if ((e = arg_num(cx, &a[0], &x)) || (e = arg_num(cx, &a[1], &y))) return val_err(e);
  if (y == 0) return val_err(E_DIV0);
  return val_num(x / y < 0 ? ceil(x / y) : floor(x / y));
}
/* round half away from zero at d decimals (d may be negative), steady against binary noise */
static double round_dec(double x, int d, int mode)
{
  double m = pow(10.0, d), y = x * m, r;
  if (mode == 0) r = floor(fabs(y) + 0.5 + 1e-9);
  else if (mode == 1) r = ceil(fabs(y) - 1e-9);
  else r = floor(fabs(y) + 1e-9);
  r = x < 0 ? -r : r;
  return r / m;
}
static Value round_impl(Ctx *cx, Value *a, int n, int mode)
{
  double x, d = 0; int e;
  if ((e = arg_num(cx, &a[0], &x))) return val_err(e);
  if (n > 1 && (e = arg_num(cx, &a[1], &d))) return val_err(e);
  if (d > 15) d = 15;
  if (d < -15) d = -15;
  return val_num(round_dec(x, (int)(d < 0 ? ceil(d) : floor(d)), mode));
}
static Value f_round(Ctx *cx, Value *a, int n) { return round_impl(cx, a, n, 0); }
static Value f_roundup(Ctx *cx, Value *a, int n) { return round_impl(cx, a, n, 1); }
static Value f_rounddown(Ctx *cx, Value *a, int n) { return round_impl(cx, a, n, 2); }
static Value f_trunc(Ctx *cx, Value *a, int n) { return round_impl(cx, a, n, 2); }
static Value f_mround(Ctx *cx, Value *a, int n)
{
  double x, m; int e;
  (void)n;
  if ((e = arg_num(cx, &a[0], &x)) || (e = arg_num(cx, &a[1], &m))) return val_err(e);
  if (m == 0) return val_num(0);
  if ((x > 0 && m < 0) || (x < 0 && m > 0)) return val_err(E_NUM);
  return val_num(floor(x / m + 0.5 + 1e-12) * m);
}
static Value ceilfloor(Ctx *cx, Value *a, int n, int up, int math)
{
  double x, sig = 1; int e;
  if ((e = arg_num(cx, &a[0], &x))) return val_err(e);
  if (n > 1 && !IS_MISSING(&a[1]) && (e = arg_num(cx, &a[1], &sig))) return val_err(e);
  else if (n < 2 || IS_MISSING(&a[1])) sig = x < 0 && !math ? -1 : 1;
  if (sig == 0) return val_num(0);
  if (math) {
    sig = fabs(sig);
    if (n > 2) {
      double mode = 0;
      arg_num(cx, &a[2], &mode);
      if (mode != 0 && x < 0) up = !up;        /* away from zero for negatives */
    }
  } else if (x > 0 && sig < 0) return val_err(E_NUM);
  {
    /* (both negative: the quotient is positive, so CEILING goes away from zero and FLOOR towards it, as in Excel) */
    double q = x / sig, r = up ? ceil(q - 1e-12) : floor(q + 1e-12);
    return val_num(r * sig);
  }
}
static Value f_ceiling(Ctx *cx, Value *a, int n) { return ceilfloor(cx, a, n, 1, 0); }
static Value f_floor(Ctx *cx, Value *a, int n) { return ceilfloor(cx, a, n, 0, 0); }
static Value f_ceilingmath(Ctx *cx, Value *a, int n) { return ceilfloor(cx, a, n, 1, 1); }
static Value f_floormath(Ctx *cx, Value *a, int n) { return ceilfloor(cx, a, n, 0, 1); }
static Value evenodd(Ctx *cx, Value *a, int odd)
{
  double x, r; int e;
  if ((e = arg_num(cx, &a[0], &x))) return val_err(e);
  r = ceil(fabs(x) - 1e-12);
  if (odd) { if (fmod(r, 2) == 0) r += 1; }
  else if (fmod(r, 2) != 0) r += 1;
  return val_num(x < 0 ? -r : r);
}
static Value f_even(Ctx *cx, Value *a, int n) { (void)n; return evenodd(cx, a, 0); }
static Value f_odd(Ctx *cx, Value *a, int n) { (void)n; return evenodd(cx, a, 1); }
static Value f_fact(Ctx *cx, Value *a, int n)
{
  double x, r = 1; int e;
  (void)n;
  if ((e = arg_num(cx, &a[0], &x))) return val_err(e);
  if (x < 0 || x > 170) return val_err(E_NUM);
  for (x = floor(x); x > 1; x--) r *= x;
  return val_num(r);
}
static double combin(double n, double k)
{
  double r = 1, i;
  if (k > n - k) k = n - k;
  for (i = 1; i <= k; i++) r = r * (n - k + i) / i;
  return floor(r + 0.5);
}
static Value f_combin(Ctx *cx, Value *a, int n)
{
  double x, k; int e;
  (void)n;
  if ((e = arg_num(cx, &a[0], &x)) || (e = arg_num(cx, &a[1], &k))) return val_err(e);
  x = floor(x); k = floor(k);
  if (x < 0 || k < 0 || k > x) return val_err(E_NUM);
  return num_or_err(combin(x, k));
}
static Value f_permut(Ctx *cx, Value *a, int n)
{
  double x, k, r = 1, i; int e;
  (void)n;
  if ((e = arg_num(cx, &a[0], &x)) || (e = arg_num(cx, &a[1], &k))) return val_err(e);
  x = floor(x); k = floor(k);
  if (x < 0 || k < 0 || k > x) return val_err(E_NUM);
  for (i = 0; i < k; i++) r *= x - i;
  return num_or_err(r);
}
static double gcd2(double a, double b) { while (b > 0.5) { double t = fmod(a, b); a = b; b = t; } return a; }
static Value gcdlcm(Ctx *cx, Value *a, int n, int lcm)
{
  Nums ns; double r; long i;
  if (gather(cx, a, n, &ns, 0)) { free(ns.v); return val_err(ns.err); }
  r = lcm ? 1 : 0;
  for (i = 0; i < ns.n; i++) {
    double x = floor(ns.v[i]);
    if (x < 0) { free(ns.v); return val_err(E_NUM); }
    if (lcm) { if (x == 0) { r = 0; break; } r = r / gcd2(r, x) * x; }
    else r = gcd2(r, x);
  }
  free(ns.v);
  return num_or_err(r);
}
static Value f_gcd(Ctx *cx, Value *a, int n) { return gcdlcm(cx, a, n, 0); }
static Value f_lcm(Ctx *cx, Value *a, int n) { return gcdlcm(cx, a, n, 1); }
/* a small xorshift generator so RAND is the same on the Amiga and in the tests' seeds */
static u32 rng_state = 2463534242UL;
void ledger_seed(u32 s) { rng_state = s ? s : 2463534242UL; }
static double rnd(void)
{
  rng_state ^= rng_state << 13; rng_state ^= rng_state >> 17; rng_state ^= rng_state << 5;
  return (rng_state & 0xFFFFFF) / 16777216.0;
}
static Value f_rand(Ctx *cx, Value *a, int n) { (void)cx; (void)a; (void)n; return val_num(rnd()); }
static Value f_randbetween(Ctx *cx, Value *a, int n)
{
  double lo, hi; int e;
  (void)n;
  if ((e = arg_num(cx, &a[0], &lo)) || (e = arg_num(cx, &a[1], &hi))) return val_err(e);
  lo = ceil(lo); hi = floor(hi);
  if (lo > hi) return val_err(E_NUM);
  return val_num(lo + floor(rnd() * (hi - lo + 1)));
}
static Value f_sumproduct(Ctx *cx, Value *a, int n)
{
  i32 rows, cols, r, c;
  double s = 0;
  int i;
  arg_shape(&a[0], &rows, &cols);
  for (i = 1; i < n; i++) { i32 rr, cc; arg_shape(&a[i], &rr, &cc); if (rr != rows || cc != cols) return val_err(E_VALUE); }
  if ((long)rows * cols > 4000000L) return val_err(E_NUM);
  for (r = 0; r < rows; r++)
    for (c = 0; c < cols; c++) {
      double p = 1;
      for (i = 0; i < n; i++) {
        const Value *v = arg_at(cx, &a[i], r, c);
        if (v->t == V_ERR) return val_err(v->e);
        p *= v->t == V_NUM ? v->n : 0;      /* text and logicals count as 0 */
      }
      s += p;
    }
  return num_or_err(s);
}

/* ------------------------------------------------------------------ conditional sums and counts */
/* SUMIF(range, criteria, [sum_range]) and the IFS family: pairs of range and criteria */
static Value ifs_impl(Ctx *cx, Value *a, int n, int kind)
{
  /* kind: 0 SUMIFS, 1 COUNTIFS, 2 AVERAGEIFS, 3 MAXIFS, 4 MINIFS; for the first three the value
     range (if any) is a[0] and pairs follow */
  const Value *vals = kind == 1 ? NULL : &a[0];
  int first = kind == 1 ? 0 : 1, npairs = (n - first) / 2, k;
  i32 rows, cols, r, c;
  Crit crits[64];
  double sum = 0, m = 0;
  long cnt = 0;
  if ((n - first) % 2 || npairs < 1 || npairs > 64) return val_err(E_VALUE);
  arg_shape(&a[first], &rows, &cols);
  if ((long)rows * cols > 2000000L) {
    /* whole columns: stop at the data */
    Sheet *s = a[first].t == V_REF && a[first].sheet < cx->b->nsh ? cx->b->sh[a[first].sheet] : NULL;
    if (s) { i32 lr = sheet_last_row(s); if (rows > lr - a[first].r1 + 1) rows = lr - a[first].r1 + 1; }
    if (rows < 0) rows = 0;
  }
  for (k = 0; k < npairs; k++) {
    i32 rr, cc;
    arg_shape(&a[first + 2 * k], &rr, &cc);
    if (cc != cols) { int j; for (j = 0; j < k; j++) crit_free(&crits[j]); return val_err(E_VALUE); }
    crit_make(cx, &a[first + 2 * k + 1], &crits[k]);
  }
  for (r = 0; r < rows; r++)
    for (c = 0; c < cols; c++) {
      int ok = 1;
      for (k = 0; k < npairs && ok; k++) ok = crit_match(&crits[k], arg_at(cx, &a[first + 2 * k], r, c));
      if (!ok) continue;
      cnt++;
      if (vals) {
        const Value *v = arg_at(cx, vals, r, c);
        if (v->t == V_NUM) {
          if (kind == 3) { if (cnt == 1 || v->n > m) m = v->n; }
          else if (kind == 4) { if (cnt == 1 || v->n < m) m = v->n; }
          sum += v->n;
        } else if (kind >= 2) cnt--;          /* averages skip non-numbers */
      }
    }
  for (k = 0; k < npairs; k++) crit_free(&crits[k]);
  switch (kind) {
  case 1: return val_num((double)cnt);
  case 2: return cnt ? num_or_err(sum / cnt) : val_err(E_DIV0);
  case 3: case 4: return val_num(cnt ? m : 0);
  default: return num_or_err(sum);
  }
}
static Value f_sumifs(Ctx *cx, Value *a, int n) { return ifs_impl(cx, a, n, 0); }
static Value f_countifs(Ctx *cx, Value *a, int n) { return ifs_impl(cx, a, n, 1); }
static Value f_averageifs(Ctx *cx, Value *a, int n) { return ifs_impl(cx, a, n, 2); }
static Value f_maxifs(Ctx *cx, Value *a, int n) { return ifs_impl(cx, a, n, 3); }
static Value f_minifs(Ctx *cx, Value *a, int n) { return ifs_impl(cx, a, n, 4); }
/* SUMIF(range, crit, [sum_range]): reorder into SUMIFS(sum_range, range, crit) */
static Value sumif_impl(Ctx *cx, Value *a, int n, int kind)
{
  Value args[3];
  args[0] = n > 2 && !IS_MISSING(&a[2]) ? a[2] : a[0];
  if (args[0].t == V_REF && a[0].t == V_REF) {
    /* the sum range takes the criteria range's shape from its top-left cell */
    args[0].r2 = args[0].r1 + (a[0].r2 - a[0].r1);
    args[0].c2 = args[0].c1 + (a[0].c2 - a[0].c1);
  }
  args[1] = a[0]; args[2] = a[1];
  return ifs_impl(cx, args, 3, kind);
}
static Value f_sumif(Ctx *cx, Value *a, int n) { return sumif_impl(cx, a, n, 0); }
static Value f_averageif(Ctx *cx, Value *a, int n) { return sumif_impl(cx, a, n, 2); }
static Value f_countif(Ctx *cx, Value *a, int n) { (void)n; return ifs_impl(cx, a, 2, 1); }

/* ------------------------------------------------------------------ statistics */
static Value f_median(Ctx *cx, Value *a, int n)
{
  Nums ns; double r;
  if (gather(cx, a, n, &ns, 0)) { free(ns.v); return val_err(ns.err); }
  if (!ns.n) { free(ns.v); return val_err(E_NUM); }
  qsort(ns.v, (size_t)ns.n, sizeof(double), dcmp);
  r = ns.n % 2 ? ns.v[ns.n / 2] : (ns.v[ns.n / 2 - 1] + ns.v[ns.n / 2]) / 2;
  free(ns.v);
  return val_num(r);
}
static Value f_mode(Ctx *cx, Value *a, int n)
{
  Nums ns; double best = 0; long i, bestc = 1, k;
  if (gather(cx, a, n, &ns, 0)) { free(ns.v); return val_err(ns.err); }
  /* the first value (in order) that repeats most */
  for (i = 0; i < ns.n; i++) {
    long c = 0;
    for (k = 0; k < ns.n; k++) if (ns.v[k] == ns.v[i]) c++;
    if (c > bestc) { bestc = c; best = ns.v[i]; }
  }
  free(ns.v);
  return bestc > 1 ? val_num(best) : val_err(E_NA);
}
static Value var_impl(Ctx *cx, Value *a, int n, int pop, int sd, int all)
{
  Nums ns; double mean = 0, ss = 0; long i;
  if (gather(cx, a, n, &ns, all)) { free(ns.v); return val_err(ns.err); }
  if (ns.n < (pop ? 1 : 2)) { free(ns.v); return val_err(E_DIV0); }
  for (i = 0; i < ns.n; i++) mean += ns.v[i];
  mean /= ns.n;
  for (i = 0; i < ns.n; i++) ss += (ns.v[i] - mean) * (ns.v[i] - mean);
  ss /= pop ? ns.n : ns.n - 1;
  free(ns.v);
  return val_num(sd ? sqrt(ss) : ss);
}
static Value f_stdev(Ctx *cx, Value *a, int n) { return var_impl(cx, a, n, 0, 1, 0); }
static Value f_stdevp(Ctx *cx, Value *a, int n) { return var_impl(cx, a, n, 1, 1, 0); }
static Value f_var(Ctx *cx, Value *a, int n) { return var_impl(cx, a, n, 0, 0, 0); }
static Value f_varp(Ctx *cx, Value *a, int n) { return var_impl(cx, a, n, 1, 0, 0); }
static Value f_stdeva(Ctx *cx, Value *a, int n) { return var_impl(cx, a, n, 0, 1, 1); }
static Value f_avedev(Ctx *cx, Value *a, int n)
{
  Nums ns; double mean = 0, s = 0; long i;
  if (gather(cx, a, n, &ns, 0)) { free(ns.v); return val_err(ns.err); }
  if (!ns.n) { free(ns.v); return val_err(E_NUM); }
  for (i = 0; i < ns.n; i++) mean += ns.v[i];
  mean /= ns.n;
  for (i = 0; i < ns.n; i++) s += fabs(ns.v[i] - mean);
  free(ns.v);
  return val_num(s / ns.n);
}
static Value kth(Ctx *cx, Value *a, int large)
{
  Nums ns; double k; int e; Value r;
  if ((e = arg_num(cx, &a[1], &k))) return val_err(e);
  if (gather(cx, a, 1, &ns, 0)) { free(ns.v); return val_err(ns.err); }
  k = ceil(k - 1e-9);
  if (k < 1 || k > ns.n) { free(ns.v); return val_err(E_NUM); }
  qsort(ns.v, (size_t)ns.n, sizeof(double), dcmp);
  r = val_num(large ? ns.v[ns.n - (long)k] : ns.v[(long)k - 1]);
  free(ns.v);
  return r;
}
static Value f_large(Ctx *cx, Value *a, int n) { (void)n; return kth(cx, a, 1); }
static Value f_small(Ctx *cx, Value *a, int n) { (void)n; return kth(cx, a, 0); }
static Value f_rank(Ctx *cx, Value *a, int n)
{
  Nums ns; double x, asc = 0; long i, rank = 1; int e, found = 0;
  if ((e = arg_num(cx, &a[0], &x))) return val_err(e);
  if (n > 2 && (e = arg_num(cx, &a[2], &asc))) return val_err(e);
  if (gather(cx, &a[1], 1, &ns, 0)) { free(ns.v); return val_err(ns.err); }
  for (i = 0; i < ns.n; i++) {
    if (ns.v[i] == x) found = 1;
    if (asc != 0 ? ns.v[i] < x : ns.v[i] > x) rank++;
  }
  free(ns.v);
  return found ? val_num((double)rank) : val_err(E_NA);
}
static Value pct_impl(Ctx *cx, Value *a, double p, int excl)
{
  Nums ns; double h, r;
  if (gather(cx, a, 1, &ns, 0)) { free(ns.v); return val_err(ns.err); }
  if (!ns.n || p < 0 || p > 1) { free(ns.v); return val_err(E_NUM); }
  qsort(ns.v, (size_t)ns.n, sizeof(double), dcmp);
  if (excl) {
    h = p * (ns.n + 1) - 1;
    if (h < 0 || h > ns.n - 1) { free(ns.v); return val_err(E_NUM); }
  } else h = p * (ns.n - 1);
  {
    long lo = (long)floor(h);
    double fr = h - lo;
    r = lo + 1 < ns.n ? ns.v[lo] + fr * (ns.v[lo + 1] - ns.v[lo]) : ns.v[lo];
  }
  free(ns.v);
  return val_num(r);
}
static Value f_percentile(Ctx *cx, Value *a, int n) { double p; int e; (void)n; if ((e = arg_num(cx, &a[1], &p))) return val_err(e); return pct_impl(cx, a, p, 0); }
static Value f_percentileexc(Ctx *cx, Value *a, int n) { double p; int e; (void)n; if ((e = arg_num(cx, &a[1], &p))) return val_err(e); return pct_impl(cx, a, p, 1); }
static Value f_quartile(Ctx *cx, Value *a, int n)
{
  double q; int e;
  (void)n;
  if ((e = arg_num(cx, &a[1], &q))) return val_err(e);
  q = floor(q);
  if (q < 0 || q > 4) return val_err(E_NUM);
  return pct_impl(cx, a, q / 4, 0);
}
static Value f_correl(Ctx *cx, Value *a, int n)
{
  /* pairs where both are numbers */
  i32 rows, cols, r, c, r2, c2;
  double sx = 0, sy = 0, sxx = 0, syy = 0, sxy = 0, k = 0;
  (void)n;
  arg_shape(&a[0], &rows, &cols);
  arg_shape(&a[1], &r2, &c2);
  if ((long)rows * cols != (long)r2 * c2) return val_err(E_NA);
  for (r = 0; r < rows; r++)
    for (c = 0; c < cols; c++) {
      long idx = (long)r * cols + c;
      const Value *x = arg_at(cx, &a[0], r, c), *y;
      double xv;
      if (x->t != V_NUM) continue;
      xv = x->n;
      y = arg_at(cx, &a[1], (i32)(idx / c2), (i32)(idx % c2));
      if (y->t != V_NUM) continue;
      sx += xv; sy += y->n; sxx += xv * xv; syy += y->n * y->n; sxy += xv * y->n; k++;
    }
  if (k < 2) return val_err(E_DIV0);
  {
    double cov = sxy - sx * sy / k, vx = sxx - sx * sx / k, vy = syy - sy * sy / k;
    if (vx <= 0 || vy <= 0) return val_err(E_DIV0);
    return val_num(cov / sqrt(vx * vy));
  }
}

/* ------------------------------------------------------------------ logic */
static Value f_if(Ctx *cx, Value *a, int n)
{
  int b, e;
  Value cond = scalar(cx, &a[0]);
  e = to_bool(&cond, &b);
  val_free(&cond);
  if (e) return val_err(e);
  if (b) return IS_MISSING(&a[1]) ? val_num(0) : val_copy(&a[1]);
  if (n < 3) return val_bool(0);
  return IS_MISSING(&a[2]) ? val_num(0) : val_copy(&a[2]);
}
static Value f_ifs(Ctx *cx, Value *a, int n)
{
  int i;
  if (n % 2) return val_err(E_VALUE);
  for (i = 0; i < n; i += 2) {
    int b, e = arg_bool(cx, &a[i], &b);
    if (e) return val_err(e);
    if (b) return val_copy(&a[i + 1]);
  }
  return val_err(E_NA);
}
static Value f_iferror(Ctx *cx, Value *a, int n)
{
  Value s = scalar(cx, &a[0]);
  (void)n;
  if (s.t == V_ERR) { val_free(&s); return IS_MISSING(&a[1]) ? val_num(0) : val_copy(&a[1]); }
  val_free(&s);
  return val_copy(&a[0]);
}
static Value f_ifna(Ctx *cx, Value *a, int n)
{
  Value s = scalar(cx, &a[0]);
  (void)n;
  if (s.t == V_ERR && s.e == E_NA) { val_free(&s); return val_copy(&a[1]); }
  val_free(&s);
  return val_copy(&a[0]);
}
typedef struct { int all, any, n, odd, err; } Logic;
static int logic_cb(Ctx *cx, const Value *v, int direct, void *arg)
{
  Logic *l = (Logic *)arg;
  int b;
  (void)cx;
  if (v->t == V_ERR) { l->err = v->e; return 1; }
  if (v->t == V_STR && !direct) return 0;
  if (v->t == V_EMPTY) return 0;
  if (to_bool(v, &b)) { l->err = E_VALUE; return 1; }
  l->n++;
  if (b) { l->any = 1; l->odd ^= 1; } else l->all = 0;
  return 0;
}
static Value logic_impl(Ctx *cx, Value *a, int n, int kind)
{
  Logic l;
  int i;
  l.all = 1; l.any = 0; l.n = 0; l.odd = 0; l.err = 0;
  for (i = 0; i < n && !l.err; i++) each_value(cx, &a[i], logic_cb, &l);
  if (l.err) return val_err(l.err);
  if (!l.n) return val_err(E_VALUE);
  return val_bool(kind == 0 ? l.all : kind == 1 ? l.any : l.odd);
}
static Value f_and(Ctx *cx, Value *a, int n) { return logic_impl(cx, a, n, 0); }
static Value f_or(Ctx *cx, Value *a, int n) { return logic_impl(cx, a, n, 1); }
static Value f_xor(Ctx *cx, Value *a, int n) { return logic_impl(cx, a, n, 2); }
static Value f_not(Ctx *cx, Value *a, int n) { int b, e = arg_bool(cx, &a[0], &b); (void)n; return e ? val_err(e) : val_bool(!b); }
static Value f_true(Ctx *cx, Value *a, int n) { (void)cx; (void)a; (void)n; return val_bool(1); }
static Value f_false(Ctx *cx, Value *a, int n) { (void)cx; (void)a; (void)n; return val_bool(0); }
static Value f_switch(Ctx *cx, Value *a, int n)
{
  Value x = scalar(cx, &a[0]);
  int i;
  if (x.t == V_ERR) return x;
  for (i = 1; i + 1 < n; i += 2) {
    Value c = scalar(cx, &a[i]);
    int eq = c.t != V_ERR && val_compare(&x, &c) == 0;
    val_free(&c);
    if (eq) { val_free(&x); return val_copy(&a[i + 1]); }
  }
  val_free(&x);
  if (i < n) return val_copy(&a[i]);              /* the default */
  return val_err(E_NA);
}
static Value f_choose(Ctx *cx, Value *a, int n)
{
  double k; int e;
  if ((e = arg_num(cx, &a[0], &k))) return val_err(e);
  k = floor(k);
  if (k < 1 || k >= n) return val_err(E_VALUE);
  return val_copy(&a[(int)k]);
}

/* ------------------------------------------------------------------ information */
static Value is_impl(Ctx *cx, Value *a, int kind)
{
  Value s;
  int r = 0;
  if (kind == 0 && a[0].t == V_REF) {
    /* ISBLANK: the cell itself, a formula giving "" is not blank */
    const Value *v = ref_cell(cx, a[0].sheet, a[0].r1, a[0].c1);
    Cell *cell = a[0].sheet < cx->b->nsh ? sheet_cell(cx->b->sh[a[0].sheet], a[0].r1, a[0].c1) : NULL;
    return val_bool(v->t == V_EMPTY && !(cell && cell->f));
  }
  if (kind == 7) return val_bool(a[0].t == V_REF);
  s = scalar(cx, &a[0]);
  switch (kind) {
  case 0: r = s.t == V_EMPTY; break;
  case 1: r = s.t == V_NUM; break;
  case 2: r = s.t == V_STR; break;
  case 3: r = s.t == V_ERR; break;
  case 4: r = s.t == V_ERR && s.e != E_NA; break;
  case 5: r = s.t == V_ERR && s.e == E_NA; break;
  case 6: r = s.t == V_BOOL; break;
  case 8: r = s.t != V_STR; break;
  }
  val_free(&s);
  return val_bool(r);
}
static Value f_isblank(Ctx *cx, Value *a, int n) { (void)n; return is_impl(cx, a, 0); }
static Value f_isnumber(Ctx *cx, Value *a, int n) { (void)n; return is_impl(cx, a, 1); }
static Value f_istext(Ctx *cx, Value *a, int n) { (void)n; return is_impl(cx, a, 2); }
static Value f_iserror(Ctx *cx, Value *a, int n) { (void)n; return is_impl(cx, a, 3); }
static Value f_iserr(Ctx *cx, Value *a, int n) { (void)n; return is_impl(cx, a, 4); }
static Value f_isna(Ctx *cx, Value *a, int n) { (void)n; return is_impl(cx, a, 5); }
static Value f_islogical(Ctx *cx, Value *a, int n) { (void)n; return is_impl(cx, a, 6); }
static Value f_isref(Ctx *cx, Value *a, int n) { (void)n; return is_impl(cx, a, 7); }
static Value f_isnontext(Ctx *cx, Value *a, int n) { (void)n; return is_impl(cx, a, 8); }
static Value f_iseven(Ctx *cx, Value *a, int n) { double x; int e = arg_num(cx, &a[0], &x); (void)n; return e ? val_err(e) : val_bool(fmod(floor(fabs(x)), 2) == 0); }
static Value f_isodd(Ctx *cx, Value *a, int n) { double x; int e = arg_num(cx, &a[0], &x); (void)n; return e ? val_err(e) : val_bool(fmod(floor(fabs(x)), 2) != 0); }
static Value f_isformula(Ctx *cx, Value *a, int n)
{
  Cell *cell;
  (void)n;
  if (a[0].t != V_REF || a[0].sheet >= cx->b->nsh) return val_err(E_VALUE);
  cell = sheet_cell(cx->b->sh[a[0].sheet], a[0].r1, a[0].c1);
  return val_bool(cell && cell->f);
}
static Value f_na(Ctx *cx, Value *a, int n) { (void)cx; (void)a; (void)n; return val_err(E_NA); }
static Value f_errortype(Ctx *cx, Value *a, int n)
{
  Value s = scalar(cx, &a[0]);
  (void)n;
  if (s.t != V_ERR) { val_free(&s); return val_err(E_NA); }
  return val_num(s.e);
}
static Value f_type(Ctx *cx, Value *a, int n)
{
  Value s;
  int t;
  (void)n;
  if (a[0].t == V_ARR || (a[0].t == V_REF && (a[0].r1 != a[0].r2 || a[0].c1 != a[0].c2))) return val_num(64);
  s = scalar(cx, &a[0]);
  t = s.t == V_NUM || s.t == V_EMPTY ? 1 : s.t == V_STR ? 2 : s.t == V_BOOL ? 4 : 16;
  val_free(&s);
  return val_num(t);
}
static Value f_n(Ctx *cx, Value *a, int n)
{
  Value s = scalar(cx, &a[0]);
  Value r;
  (void)n;
  if (s.t == V_NUM) return s;
  r = s.t == V_BOOL ? val_num(s.e) : s.t == V_ERR ? val_err(s.e) : val_num(0);
  val_free(&s);
  return r;
}

/* ------------------------------------------------------------------ text */
static Value str_result(Buf *b) { Value v = val_str(b->data ? b->data : ""); buf_free(b); return v; }
static Value f_len(Ctx *cx, Value *a, int n) { char *s; int e = arg_str(cx, &a[0], &s); Value r; (void)n; if (e) return val_err(e); r = val_num((double)u8_len(s)); free(s); return r; }
static Value lr_impl(Ctx *cx, Value *a, int n, int right)
{
  char *s;
  double k = 1;
  long len;
  int e;
  Value r;
  if (n > 1 && !IS_MISSING(&a[1]) && (e = arg_num(cx, &a[1], &k))) return val_err(e);
  if (k < 0) return val_err(E_VALUE);
  if ((e = arg_str(cx, &a[0], &s))) return val_err(e);
  len = u8_len(s);
  if (k > len) k = (double)len;
  if (right) r = val_str(u8_at(s, len - (long)k));
  else { const char *end = u8_at(s, (long)k); r = val_strn(s, (size_t)(end - s)); }
  free(s);
  return r;
}
static Value f_left(Ctx *cx, Value *a, int n) { return lr_impl(cx, a, n, 0); }
static Value f_right(Ctx *cx, Value *a, int n) { return lr_impl(cx, a, n, 1); }
static Value f_mid(Ctx *cx, Value *a, int n)
{
  char *s;
  double st, k;
  int e;
  Value r;
  const char *p, *q;
  (void)n;
  if ((e = arg_num(cx, &a[1], &st)) || (e = arg_num(cx, &a[2], &k))) return val_err(e);
  if (st < 1 || k < 0) return val_err(E_VALUE);
  if ((e = arg_str(cx, &a[0], &s))) return val_err(e);
  p = u8_at(s, (long)st - 1);
  q = u8_at(p, (long)k > 100000000L ? 100000000L : (long)k);
  r = val_strn(p, (size_t)(q - p));
  free(s);
  return r;
}
/* case changes on ASCII and Latin-1 letters (UTF-8 two-byte forms) */
static void case_map(char *s, int mode)
{
  int start = 1;
  unsigned char *p = (unsigned char *)s;
  while (*p) {
    int up = mode == 0 || (mode == 2 && start), letter = 0;
    if (*p < 0x80) {
      letter = isalpha(*p) != 0;
      if (letter) *p = (unsigned char)(up ? toupper(*p) : tolower(*p));
      start = !isalnum(*p) && *p != '\'';
      if (*p == '\'') start = 0;
      p++;
    } else if (*p == 0xC3 && p[1]) {
      u32 cp = 0xC0 + (p[1] & 0x3F) + 0x40 * 0;
      cp = ((u32)(p[0] & 0x1F) << 6) | (p[1] & 0x3F);
      if (cp >= 0xC0 && cp <= 0xFE && cp != 0xD7 && cp != 0xF7 && cp != 0xDF) {
        if (up && cp >= 0xE0) cp -= 32;
        else if (!up && cp < 0xE0) cp += 32;
        p[1] = (unsigned char)(0x80 | (cp & 0x3F));
      }
      start = 0;
      p += 2;
    } else { start = 0; p++; while ((*p & 0xC0) == 0x80) p++; }
  }
}
static Value case_impl(Ctx *cx, Value *a, int mode)
{
  char *s;
  int e = arg_str(cx, &a[0], &s);
  Value r;
  if (e) return val_err(e);
  if (mode == 1) case_map(s, 1);
  else if (mode == 0) case_map(s, 0);
  else { case_map(s, 1); case_map(s, 2); }
  r = val_str(s);
  free(s);
  return r;
}
static Value f_upper(Ctx *cx, Value *a, int n) { (void)n; return case_impl(cx, a, 0); }
/* HYPERLINK(link, [friendly name]): the cell shows the name (or the link); the grid opens the link */
static Value f_hyperlink(Ctx *cx, Value *a, int n) { return scalar(cx, n >= 2 && !IS_MISSING(&a[1]) ? &a[1] : &a[0]); }
static Value f_lower(Ctx *cx, Value *a, int n) { (void)n; return case_impl(cx, a, 1); }
static Value f_proper(Ctx *cx, Value *a, int n) { (void)n; return case_impl(cx, a, 2); }
static Value f_trim(Ctx *cx, Value *a, int n)
{
  char *s;
  int e = arg_str(cx, &a[0], &s), sp = 0;
  Buf b;
  const char *p;
  (void)n;
  if (e) return val_err(e);
  buf_init(&b);
  for (p = s; *p == ' '; p++) ;
  for (; *p; p++) {
    if (*p == ' ') { sp = 1; continue; }
    if (sp && b.len) buf_addc(&b, ' ');
    sp = 0;
    buf_addc(&b, *p);
  }
  free(s);
  return str_result(&b);
}
static Value f_clean(Ctx *cx, Value *a, int n)
{
  char *s;
  int e = arg_str(cx, &a[0], &s);
  Buf b;
  const char *p;
  (void)n;
  if (e) return val_err(e);
  buf_init(&b);
  for (p = s; *p; p++) if ((u8)*p >= 32) buf_addc(&b, *p);
  free(s);
  return str_result(&b);
}
typedef struct { Buf *b; int err; const char *delim; int skip; int first; } Cat;
static int cat_cb(Ctx *cx, const Value *v, int direct, void *arg)
{
  Cat *c = (Cat *)arg;
  (void)direct;
  if (v->t == V_ERR) { c->err = v->e; return 1; }
  if (c->skip && (v->t == V_EMPTY || (v->t == V_STR && !v->s[0]))) return 0;
  if (!c->first && c->delim) buf_adds(c->b, c->delim);
  c->first = 0;
  to_text(cx->b, v, c->b);
  return 0;
}
static Value f_concat(Ctx *cx, Value *a, int n)
{
  Buf b;
  Cat c;
  int i;
  buf_init(&b);
  c.b = &b; c.err = 0; c.delim = NULL; c.skip = 0; c.first = 1;
  for (i = 0; i < n && !c.err; i++) each_value(cx, &a[i], cat_cb, &c);
  if (c.err) { buf_free(&b); return val_err(c.err); }
  if (b.len > 32767) { buf_free(&b); return val_err(E_VALUE); }
  return str_result(&b);
}
static Value f_concatenate(Ctx *cx, Value *a, int n)
{
  Buf b;
  int i;
  buf_init(&b);
  for (i = 0; i < n; i++) {
    Value s = scalar(cx, &a[i]);
    if (s.t == V_ERR) { buf_free(&b); return s; }
    to_text(cx->b, &s, &b);
    val_free(&s);
  }
  return str_result(&b);
}
static Value f_textjoin(Ctx *cx, Value *a, int n)
{
  char *d;
  int skip, e, i;
  Buf b;
  Cat c;
  if ((e = arg_str(cx, &a[0], &d))) return val_err(e);
  if ((e = arg_bool(cx, &a[1], &skip))) { free(d); return val_err(e); }
  buf_init(&b);
  c.b = &b; c.err = 0; c.delim = d; c.skip = skip; c.first = 1;
  for (i = 2; i < n && !c.err; i++) {
    if (!skip && a[i].t == V_REF && a[i].sheet < cx->b->nsh) {
      /* every cell of the range, empty ones too (each_value visits only those that exist); the
         range cut to the sheet's used part */
      Sheet *sh = cx->b->sh[a[i].sheet];
      i32 r, cc, r2 = a[i].r2 < sheet_last_row(sh) ? a[i].r2 : sheet_last_row(sh), c2 = a[i].c2 < sheet_last_col(sh) ? a[i].c2 : sheet_last_col(sh);
      for (r = a[i].r1; r <= r2 && !c.err; r++)
        for (cc = a[i].c1; cc <= c2 && !c.err; cc++) cat_cb(cx, ref_cell(cx, a[i].sheet, r, cc), 0, &c);
    } else each_value(cx, &a[i], cat_cb, &c);
  }
  free(d);
  if (c.err) { buf_free(&b); return val_err(c.err); }
  return str_result(&b);
}
static Value f_rept(Ctx *cx, Value *a, int n)
{
  char *s;
  double k;
  int e;
  Buf b;
  long i;
  (void)n;
  if ((e = arg_num(cx, &a[1], &k))) return val_err(e);
  if (k < 0) return val_err(E_VALUE);
  if ((e = arg_str(cx, &a[0], &s))) return val_err(e);
  if ((double)strlen(s) * floor(k) > 32767) { free(s); return val_err(E_VALUE); }
  buf_init(&b);
  for (i = 0; i < (long)k; i++) buf_adds(&b, s);
  free(s);
  return str_result(&b);
}
/* FIND (case-sensitive, no wildcards) and SEARCH (ignores case, wildcards) */
static Value find_impl(Ctx *cx, Value *a, int n, int search)
{
  char *what, *in;
  double st = 1;
  int e;
  long len, i;
  if (n > 2 && !IS_MISSING(&a[2]) && (e = arg_num(cx, &a[2], &st))) return val_err(e);
  if ((e = arg_str(cx, &a[0], &what))) return val_err(e);
  if ((e = arg_str(cx, &a[1], &in))) { free(what); return val_err(e); }
  len = u8_len(in);
  if (st < 1 || st > len + 1) { free(what); free(in); return val_err(E_VALUE); }
  for (i = (long)st - 1; i <= len; i++) {
    const char *p = u8_at(in, i);
    int hit;
    if (search) {
      /* the pattern may match a prefix of the rest: try every length */
      Buf pat;
      buf_init(&pat);
      buf_adds(&pat, what);
      buf_adds(&pat, "*");
      hit = wild_match(pat.data ? pat.data : "*", p);
      buf_free(&pat);
    } else hit = !strncmp(p, what, strlen(what));
    if (hit) { free(what); free(in); return val_num((double)i + 1); }
    if (!*p) break;
  }
  free(what); free(in);
  return val_err(E_VALUE);
}
static Value f_find(Ctx *cx, Value *a, int n) { return find_impl(cx, a, n, 0); }
static Value f_search(Ctx *cx, Value *a, int n) { return find_impl(cx, a, n, 1); }
static Value f_substitute(Ctx *cx, Value *a, int n)
{
  char *s, *old, *nw;
  double which = 0;
  int e, k = 0;
  Buf b;
  const char *p;
  size_t ol;
  if (n > 3 && (e = arg_num(cx, &a[3], &which))) return val_err(e);
  if (n > 3 && which < 1) return val_err(E_VALUE);
  if ((e = arg_str(cx, &a[0], &s))) return val_err(e);
  if ((e = arg_str(cx, &a[1], &old))) { free(s); return val_err(e); }
  if ((e = arg_str(cx, &a[2], &nw))) { free(s); free(old); return val_err(e); }
  ol = strlen(old);
  buf_init(&b);
  if (!ol) buf_adds(&b, s);
  else for (p = s; *p; ) {
    if (!strncmp(p, old, ol)) {
      k++;
      if (!which || k == (int)which) { buf_adds(&b, nw); p += ol; continue; }
    }
    buf_addc(&b, *p++);
  }
  free(s); free(old); free(nw);
  if (b.len > 32767) { buf_free(&b); return val_err(E_VALUE); }
  return str_result(&b);
}
static Value f_replace(Ctx *cx, Value *a, int n)
{
  char *s, *nw;
  double st, k;
  int e;
  Buf b;
  const char *p, *q;
  (void)n;
  if ((e = arg_num(cx, &a[1], &st)) || (e = arg_num(cx, &a[2], &k))) return val_err(e);
  if (st < 1 || k < 0) return val_err(E_VALUE);
  if ((e = arg_str(cx, &a[0], &s))) return val_err(e);
  if ((e = arg_str(cx, &a[3], &nw))) { free(s); return val_err(e); }
  p = u8_at(s, (long)st - 1);
  q = u8_at(p, (long)k);
  buf_init(&b);
  buf_add(&b, s, (size_t)(p - s));
  buf_adds(&b, nw);
  buf_adds(&b, q);
  free(s); free(nw);
  return str_result(&b);
}
static Value f_exact(Ctx *cx, Value *a, int n)
{
  char *x, *y;
  int e, r;
  (void)n;
  if ((e = arg_str(cx, &a[0], &x))) return val_err(e);
  if ((e = arg_str(cx, &a[1], &y))) { free(x); return val_err(e); }
  r = !strcmp(x, y);
  free(x); free(y);
  return val_bool(r);
}
static Value f_value(Ctx *cx, Value *a, int n)
{
  Value s = scalar(cx, &a[0]);
  double d;
  int isdate;
  (void)n;
  if (s.t == V_NUM || s.t == V_ERR) return s;
  if (s.t == V_EMPTY) return val_num(0);
  if (s.t == V_STR && parse_number(s.s, &d, &isdate)) { val_free(&s); return val_num(d); }
  val_free(&s);
  return val_err(E_VALUE);
}
/* NUMBERVALUE(text, [decimal separator], [group separator]): text read as a number whatever the
   locale: spaces and group separators left out, the decimal separator taken as the point, and each
   % at the end dividing by 100 */
static Value f_numbervalue(Ctx *cx, Value *a, int n)
{
  Value s = scalar(cx, &a[0]);
  char *ds = NULL, *gs = NULL, *t, *end;
  const char *p;
  int e, dots = 0, pct = 0;
  size_t k = 0;
  double d;
  if (s.t == V_ERR || s.t == V_NUM) return s;
  if (s.t == V_EMPTY) return val_num(0);
  if (n > 1 && !IS_MISSING(&a[1]) && (e = arg_str(cx, &a[1], &ds))) { val_free(&s); return val_err(e); }
  if (n > 2 && !IS_MISSING(&a[2]) && (e = arg_str(cx, &a[2], &gs))) { val_free(&s); free(ds); return val_err(e); }
  if (s.t != V_STR) { Buf b; buf_init(&b); to_text(cx->b, &s, &b); buf_addc(&b, 0); val_free(&s); s = val_str(b.data ? b.data : ""); buf_free(&b); }
  if (!(t = (char *)malloc(strlen(s.s) + 2))) { val_free(&s); free(ds); free(gs); return val_err(E_VALUE); }
  for (p = s.s; *p; p++) {
    char ch = *p;
    if (ch == ' ' || ch == '\t') continue;
    if (pct && ch != '%') { e = 1; break; }                       /* nothing but % after a % */
    if (ch == '%') { pct++; continue; }
    if (ch == (ds && ds[0] ? ds[0] : '.')) { if (dots++) break; t[k++] = '.'; continue; }
    if (ch == (gs && gs[0] ? gs[0] : ',')) { if (dots) break; continue; }   /* a group separator after the point: no good */
    t[k++] = ch;
  }
  t[k] = 0;
  e = *p != 0 || !k;
  d = e ? 0 : strtod(t, &end);
  if (!e && *end) e = 1;
  free(t); free(ds); free(gs);
  if (s.s && !s.s[0]) { val_free(&s); return val_num(0); }
  val_free(&s);
  if (e) return val_err(E_VALUE);
  while (pct--) d /= 100;
  return val_num(d);
}
static Value f_text(Ctx *cx, Value *a, int n)
{
  Value s = scalar(cx, &a[0]);
  char *code, out[256];
  int e;
  u32 col;
  (void)n;
  if (s.t == V_ERR) return s;
  if ((e = arg_str(cx, &a[1], &code))) { val_free(&s); return val_err(e); }
  if (s.t == V_STR) {
    double d;
    int k;
    if (parse_number(s.s, &d, &k)) { val_free(&s); s = val_num(d); }
  }
  fmt_value(cx->b, code, &s, out, sizeof out, &col, 255);
  free(code);
  val_free(&s);
  return val_str(out);
}
static Value fixed_impl(Ctx *cx, Value *a, int n, int dollar)
{
  double x, d = 2;
  int e, nocomma = 0;
  char code[128], out[256];                       /* (room for DOLLAR's two parts with thirty decimals) */
  Value v;
  u32 col;
  if ((e = arg_num(cx, &a[0], &x))) return val_err(e);
  if (n > 1 && !IS_MISSING(&a[1]) && (e = arg_num(cx, &a[1], &d))) return val_err(e);
  if (!dollar && n > 2 && (e = arg_bool(cx, &a[2], &nocomma))) return val_err(e);
  if (d > 127) return val_err(E_VALUE);
  if (d < 0) { x = round_dec(x, (int)d, 0); d = 0; }
  /* the decimals' zeros added one by one: vbcc's library takes %.*s with a precision of 0 as no
     precision and wrote all thirty (FIXED(1234.567,-2) came out as 0,000,...,001,200 on the Amiga) */
  sprintf(code, "%s%s%s", dollar ? "\"$\"" : "", nocomma ? "0" : "#,##0", d >= 1 ? "." : "");
  { int k, nd = (int)d > 30 ? 30 : (int)d; size_t l = strlen(code); for (k = 0; k < nd; k++) code[l++] = '0'; code[l] = 0; }
  if (dollar) { char neg[132]; sprintf(neg, ";(%.60s)", code); strcat(code, neg); }   /* (code is at most 39 characters here) */
  v = val_num(x);
  fmt_value(cx->b, code, &v, out, sizeof out, &col, 255);
  return val_str(out);
}
static Value f_fixed(Ctx *cx, Value *a, int n) { return fixed_impl(cx, a, n, 0); }
static Value f_dollar(Ctx *cx, Value *a, int n) { return fixed_impl(cx, a, n, 1); }
static Value f_char(Ctx *cx, Value *a, int n)
{
  double x;
  int e;
  char out[8];
  (void)n;
  if ((e = arg_num(cx, &a[0], &x))) return val_err(e);
  if (x < 1 || x > 255) return val_err(E_VALUE);
  out[utf8_put(out, (u32)x)] = 0;
  return val_str(out);
}
static Value f_unichar(Ctx *cx, Value *a, int n)
{
  double x;
  int e;
  char out[8];
  (void)n;
  if ((e = arg_num(cx, &a[0], &x))) return val_err(e);
  if (x < 1 || x > 0x10FFFF) return val_err(E_VALUE);
  out[utf8_put(out, (u32)x)] = 0;
  return val_str(out);
}
static Value f_code(Ctx *cx, Value *a, int n)
{
  char *s;
  int e, used;
  u32 cp;
  (void)n;
  if ((e = arg_str(cx, &a[0], &s))) return val_err(e);
  if (!*s) { free(s); return val_err(E_VALUE); }
  cp = utf8_get(s, strlen(s), &used);
  free(s);
  return val_num(cp);
}
static Value f_t(Ctx *cx, Value *a, int n)
{
  Value s = scalar(cx, &a[0]);
  (void)n;
  if (s.t == V_STR || s.t == V_ERR) return s;
  val_free(&s);
  return val_str("");
}

/* ------------------------------------------------------------------ lookup and reference */
/* the position of x in a one-dimensional range (rows or columns), as MATCH: mode 0 exact (with
   wildcards for text), 1 largest <= x (sorted ascending), -1 smallest >= x (sorted descending).
   -1 if not found */
static long find_pos(Ctx *cx, const Value *range, int vertical, i32 fixed, i32 len, const Value *x, int mode)
{
  i32 i;
  long best = -1;
  int wild = mode == 0 && x->t == V_STR && strpbrk(x->s, "*?~");
  if (mode == 0) {
    for (i = 0; i < len; i++) {
      const Value *v = vertical ? arg_at(cx, range, i, fixed) : arg_at(cx, range, fixed, i);
      if (wild) { if (v->t == V_STR && wild_match(x->s, v->s)) return i; }
      else if (v->t != V_EMPTY && v->t == x->t && val_compare(v, x) == 0) return i;
      else if (v->t != V_EMPTY && (v->t == V_NUM) != (x->t == V_NUM)) continue;
    }
    return -1;
  }
  /* approximate: binary search as Excel does (garbage in, the same garbage out) */
  {
    long lo = 0, hi = (long)len - 1;
    while (lo <= hi) {
      long mid = (lo + hi) / 2;
      const Value *v = vertical ? arg_at(cx, range, (i32)mid, fixed) : arg_at(cx, range, fixed, (i32)mid);
      int c;
      if (v->t == V_EMPTY || (v->t != x->t && !(v->t == V_NUM && x->t == V_NUM))) {
        /* skip blanks and other types by scanning down */
        long k = mid;
        while (k >= lo) {
          v = vertical ? arg_at(cx, range, (i32)k, fixed) : arg_at(cx, range, fixed, (i32)k);
          if (v->t == x->t) break;
          k--;
        }
        if (k < lo) { lo = mid + 1; continue; }
        mid = k;
      }
      c = val_compare(v, x);
      if (mode == 1) { if (c <= 0) { best = mid; lo = mid + 1; } else hi = mid - 1; }
      else { if (c >= 0) { best = mid; lo = mid + 1; } else hi = mid - 1; }
    }
  }
  return best;
}
static Value f_match(Ctx *cx, Value *a, int n)
{
  Value x = scalar(cx, &a[0]);
  double mode = 1;
  i32 rows, cols;
  long pos;
  int e;
  if (x.t == V_ERR) return x;
  if (n > 2 && !IS_MISSING(&a[2]) && (e = arg_num(cx, &a[2], &mode))) { val_free(&x); return val_err(e); }
  arg_shape(&a[1], &rows, &cols);
  if (rows > 1 && cols > 1) { val_free(&x); return val_err(E_NA); }
  pos = find_pos(cx, &a[1], cols == 1, 0, cols == 1 ? rows : cols, &x, mode > 0 ? 1 : mode < 0 ? -1 : 0);
  val_free(&x);
  return pos < 0 ? val_err(E_NA) : val_num((double)pos + 1);
}
static Value vhlookup(Ctx *cx, Value *a, int n, int vertical)
{
  Value x = scalar(cx, &a[0]);
  double idx;
  int e, approx = 1;
  i32 rows, cols;
  long pos;
  if (x.t == V_ERR) return x;
  if ((e = arg_num(cx, &a[2], &idx))) { val_free(&x); return val_err(e); }
  if (n > 3 && !IS_MISSING(&a[3]) && (e = arg_bool(cx, &a[3], &approx))) { val_free(&x); return val_err(e); }
  arg_shape(&a[1], &rows, &cols);
  idx = floor(idx);
  if (idx < 1) { val_free(&x); return val_err(E_VALUE); }
  if (idx > (vertical ? cols : rows)) { val_free(&x); return val_err(E_REF); }
  if (vertical && (long)rows > 1000000L) { Sheet *s = a[1].t == V_REF && a[1].sheet < cx->b->nsh ? cx->b->sh[a[1].sheet] : NULL; if (s) rows = sheet_last_row(s) - a[1].r1 + 1; }
  pos = find_pos(cx, &a[1], vertical, 0, vertical ? rows : cols, &x, approx ? 1 : 0);
  val_free(&x);
  if (pos < 0) return val_err(E_NA);
  {
    const Value *v = vertical ? arg_at(cx, &a[1], (i32)pos, (i32)idx - 1) : arg_at(cx, &a[1], (i32)idx - 1, (i32)pos);
    return v->t == V_EMPTY ? val_num(0) : val_copy(v);
  }
}
static Value f_vlookup(Ctx *cx, Value *a, int n) { return vhlookup(cx, a, n, 1); }
static Value f_hlookup(Ctx *cx, Value *a, int n) { return vhlookup(cx, a, n, 0); }
static Value f_lookup(Ctx *cx, Value *a, int n)
{
  Value x = scalar(cx, &a[0]);
  i32 rows, cols;
  long pos;
  int vertical;
  const Value *res;
  if (x.t == V_ERR) return x;
  arg_shape(&a[1], &rows, &cols);
  vertical = rows >= cols;
  pos = find_pos(cx, &a[1], vertical, 0, vertical ? rows : cols, &x, 1);
  val_free(&x);
  if (pos < 0) return val_err(E_NA);
  if (n > 2) {
    i32 r2, c2;
    arg_shape(&a[2], &r2, &c2);
    res = r2 >= c2 ? arg_at(cx, &a[2], (i32)pos, 0) : arg_at(cx, &a[2], 0, (i32)pos);
  } else res = vertical ? arg_at(cx, &a[1], (i32)pos, cols - 1) : arg_at(cx, &a[1], rows - 1, (i32)pos);
  return val_copy(res);
}
static Value f_xlookup(Ctx *cx, Value *a, int n)
{
  Value x = scalar(cx, &a[0]);
  i32 rows, cols, r2, c2;
  long pos = -1, i, len;
  double mode = 0, dir = 1;
  int vertical;
  if (x.t == V_ERR) return x;
  if (n > 4 && !IS_MISSING(&a[4])) arg_num(cx, &a[4], &mode);
  if (n > 5 && !IS_MISSING(&a[5])) arg_num(cx, &a[5], &dir);
  arg_shape(&a[1], &rows, &cols);
  vertical = cols == 1;
  len = vertical ? rows : cols;
  if (mode == 0 || mode == 2) {
    for (i = 0; i < len; i++) {
      long k = dir < 0 ? len - 1 - i : i;
      const Value *v = vertical ? arg_at(cx, &a[1], (i32)k, 0) : arg_at(cx, &a[1], 0, (i32)k);
      if ((mode == 2 && x.t == V_STR && v->t == V_STR && wild_match(x.s, v->s)) || (v->t == x.t && val_compare(v, &x) == 0)) { pos = k; break; }
    }
  } else {
    /* next smaller (-1) or next larger (1) */
    const Value *bestv = NULL;
    for (i = 0; i < len; i++) {
      const Value *v = vertical ? arg_at(cx, &a[1], (i32)i, 0) : arg_at(cx, &a[1], 0, (i32)i);
      int c;
      if (v->t != x.t) continue;
      c = val_compare(v, &x);
      if (c == 0) { pos = i; bestv = NULL; break; }
      if ((mode < 0 && c < 0 && (!bestv || val_compare(v, bestv) > 0)) || (mode > 0 && c > 0 && (!bestv || val_compare(v, bestv) < 0))) { bestv = v; pos = i; }
    }
  }
  val_free(&x);
  if (pos < 0) return n > 3 && !IS_MISSING(&a[3]) ? val_copy(&a[3]) : val_err(E_NA);
  arg_shape(&a[2], &r2, &c2);
  if (a[2].t == V_REF) {
    /* the whole matching row or column of the result range */
    Value r = a[2];
    if (vertical) { r.r1 = r.r2 = a[2].r1 + (i32)pos; }
    else { r.c1 = r.c2 = a[2].c1 + (i32)pos; }
    return r;
  }
  return val_copy(vertical ? arg_at(cx, &a[2], (i32)pos, 0) : arg_at(cx, &a[2], 0, (i32)pos));
}
static Value f_index(Ctx *cx, Value *a, int n)
{
  double r = 0, c = 0;
  int e;
  i32 rows, cols;
  if (n > 1 && !IS_MISSING(&a[1]) && (e = arg_num(cx, &a[1], &r))) return val_err(e);
  if (n > 2 && !IS_MISSING(&a[2]) && (e = arg_num(cx, &a[2], &c))) return val_err(e);
  arg_shape(&a[0], &rows, &cols);
  r = floor(r); c = floor(c);
  if (n == 2 && rows == 1) { c = r; r = 0; }          /* INDEX(row_range, k) */
  if (r < 0 || c < 0 || r > rows || c > cols) return val_err(E_REF);
  if (a[0].t == V_REF) {
    Value v = a[0];
    if (r > 0) v.r1 = v.r2 = a[0].r1 + (i32)r - 1;
    if (c > 0) v.c1 = v.c2 = a[0].c1 + (i32)c - 1;
    return v;
  }
  if (r == 0) r = 1;
  if (c == 0) c = 1;
  return val_copy(arg_at(cx, &a[0], (i32)r - 1, (i32)c - 1));
}
static Value f_row(Ctx *cx, Value *a, int n)
{
  if (n == 0 || IS_MISSING(&a[0])) return val_num((double)cx->row + 1);
  if (a[0].t != V_REF) return val_err(E_VALUE);
  return val_num((double)a[0].r1 + 1);
}
static Value f_column(Ctx *cx, Value *a, int n)
{
  if (n == 0 || IS_MISSING(&a[0])) return val_num((double)cx->col + 1);
  if (a[0].t != V_REF) return val_err(E_VALUE);
  return val_num((double)a[0].c1 + 1);
}
static Value f_rows(Ctx *cx, Value *a, int n) { i32 r, c; (void)cx; (void)n; if (a[0].t == V_ERR) return val_err(a[0].e); arg_shape(&a[0], &r, &c); return val_num(r); }
static Value f_columns(Ctx *cx, Value *a, int n) { i32 r, c; (void)cx; (void)n; if (a[0].t == V_ERR) return val_err(a[0].e); arg_shape(&a[0], &r, &c); return val_num(c); }
static Value f_offset(Ctx *cx, Value *a, int n)
{
  double dr, dc, h, w;
  int e;
  Value v;
  if (a[0].t != V_REF) return val_err(E_VALUE);
  if ((e = arg_num(cx, &a[1], &dr)) || (e = arg_num(cx, &a[2], &dc))) return val_err(e);
  h = a[0].r2 - a[0].r1 + 1; w = a[0].c2 - a[0].c1 + 1;
  if (n > 3 && !IS_MISSING(&a[3]) && (e = arg_num(cx, &a[3], &h))) return val_err(e);
  if (n > 4 && !IS_MISSING(&a[4]) && (e = arg_num(cx, &a[4], &w))) return val_err(e);
  if (h < 1 || w < 1) return val_err(E_REF);
  v = a[0];
  v.r1 = a[0].r1 + (i32)dr; v.c1 = a[0].c1 + (i32)dc;
  v.r2 = v.r1 + (i32)h - 1; v.c2 = v.c1 + (i32)w - 1;
  if (v.r1 < 0 || v.c1 < 0 || v.r2 >= MAXROW || v.c2 >= MAXCOL) return val_err(E_REF);
  return v;
}
/* one R1C1 part: R (n absolute, [n] relative to base, nothing: base itself); 0 if none there */
static int r1c1_part(const char **p, char letter, i32 base, i32 lim, i32 *out)
{
  const char *q = *p;
  long v;
  char *end;
  if ((*q | 32) != (letter | 32)) return 0;
  q++;
  if (*q == '[') {
    v = strtol(q + 1, &end, 10);
    if (end == q + 1 || *end != ']') return 0;
    *out = base + (i32)v; q = end + 1;
  } else if (*q >= '0' && *q <= '9') {
    v = strtol(q, &end, 10);
    *out = (i32)v - 1; q = end;
  } else *out = base;
  if (*out < 0 || *out >= lim) return 0;
  *p = q;
  return 1;
}
/* an R1C1 address ("R2C3", "R[-1]C", "Sheet2!R1C1:R2C2"), relative parts from the formula's cell;
   1 if it is one */
static int r1c1_ref(Ctx *cx, const char *s, u16 *sheet, Rect *r)
{
  const char *bang = strrchr(s, '!'), *p = s;
  *sheet = SHEET_SELF;
  if (bang) {
    char nm[64];
    size_t l = (size_t)(bang - s);
    int k;
    if (l >= 2 && s[0] == '\'' && s[l - 1] == '\'') { s++; l -= 2; }
    if (l == 0 || l >= sizeof nm) return 0;
    memcpy(nm, s, l); nm[l] = 0;
    if ((k = book_find_sheet(cx->b, nm)) < 0) return 0;
    *sheet = (u16)k;
    p = bang + 1;
  }
  if (!r1c1_part(&p, 'R', cx->row, MAXROW, &r->r1) || !r1c1_part(&p, 'C', cx->col, MAXCOL, &r->c1)) return 0;
  r->r2 = r->r1; r->c2 = r->c1;
  if (*p == ':') {
    p++;
    if (!r1c1_part(&p, 'R', cx->row, MAXROW, &r->r2) || !r1c1_part(&p, 'C', cx->col, MAXCOL, &r->c2)) return 0;
    if (r->r2 < r->r1) { i32 t = r->r1; r->r1 = r->r2; r->r2 = t; }
    if (r->c2 < r->c1) { i32 t = r->c1; r->c1 = r->c2; r->c2 = t; }
  }
  return *p == 0;
}
static Value f_indirect(Ctx *cx, Value *a, int n)
{
  char *s;
  int e, len;
  u16 sheet;
  Rect r;
  u8 flags;
  Value v = val_empty();
  (void)n;
  if ((e = arg_str(cx, &a[0], &s))) return val_err(e);
  if (n > 1 && !IS_MISSING(&a[1])) {
    int a1 = 1;
    if ((e = arg_bool(cx, &a[1], &a1))) { free(s); return val_err(e); }
    if (!a1) {
      len = r1c1_ref(cx, s, &sheet, &r);
      free(s);
      if (!len) return val_err(E_REF);
      v.t = V_REF;
      v.sheet = (u16)(sheet == SHEET_SELF ? cx->sheet : sheet);
      v.r1 = r.r1; v.c1 = r.c1; v.r2 = r.r2; v.c2 = r.c2;
      return v;
    }
  }
  len = parse_ref(cx->b, s, &sheet, &r, &flags);
  if (!len || s[len]) { free(s); return val_err(E_REF); }
  free(s);
  v.t = V_REF;
  v.sheet = (u16)(sheet == SHEET_SELF ? cx->sheet : sheet);
  v.r1 = r.r1; v.c1 = r.c1; v.r2 = r.r2; v.c2 = r.c2;
  return v;
}
static Value f_address(Ctx *cx, Value *a, int n)
{
  double r, c, absn = 1;
  int e, a1 = 1;
  char out[64], col[8];
  Buf b;
  if ((e = arg_num(cx, &a[0], &r)) || (e = arg_num(cx, &a[1], &c))) return val_err(e);
  if (n > 2 && !IS_MISSING(&a[2]) && (e = arg_num(cx, &a[2], &absn))) return val_err(e);
  if (n > 3 && !IS_MISSING(&a[3]) && (e = arg_bool(cx, &a[3], &a1))) return val_err(e);
  if (r < 1 || c < 1 || r > MAXROW || c > MAXCOL) return val_err(E_VALUE);
  buf_init(&b);
  if (n > 4 && !IS_MISSING(&a[4])) {
    char *sh;
    if (!arg_str(cx, &a[4], &sh)) { buf_adds(&b, sh); buf_addc(&b, '!'); free(sh); }
  }
  col_name((i32)c - 1, col);
  if (a1) sprintf(out, "%s%s%s%ld", absn == 1 || absn == 3 ? "$" : "", col, absn == 1 || absn == 2 ? "$" : "", (long)r);
  else sprintf(out, "R%s%ld%sC%s%ld%s", absn == 1 || absn == 2 ? "" : "[", (long)r, absn == 1 || absn == 2 ? "" : "]",
               absn == 1 || absn == 3 ? "" : "[", (long)c, absn == 1 || absn == 3 ? "" : "]");
  buf_adds(&b, out);
  return str_result(&b);
}
static Value f_transpose(Ctx *cx, Value *a, int n)
{
  int err;
  Arr *src = to_array(cx, &a[0], &err), *dst;
  i32 r, c;
  Value v;
  (void)n;
  if (!src) return val_err(err);
  if (!(dst = arr_new(src->cols, src->rows))) { arr_free(src); return val_err(E_NUM); }
  for (r = 0; r < src->rows; r++) for (c = 0; c < src->cols; c++) { dst->v[c * src->rows + r] = src->v[r * src->cols + c]; src->v[r * src->cols + c] = val_empty(); }
  arr_free(src);
  v = val_empty(); v.t = V_ARR; v.a = dst;
  return v;
}

/* ------------------------------------------------------------------ dates and times */
extern double ledger_now(void);    /* set by the platform (the Amiga's clock, or the tests) */
static Value f_today(Ctx *cx, Value *a, int n) { (void)a; (void)n; return val_num(floor(cx->b->now_override ? cx->b->now_override : ledger_now())); }
static Value f_now(Ctx *cx, Value *a, int n) { (void)a; (void)n; return val_num(cx->b->now_override ? cx->b->now_override : ledger_now()); }
static Value f_date(Ctx *cx, Value *a, int n)
{
  double y, m, d, s;
  int e;
  (void)n;
  if ((e = arg_num(cx, &a[0], &y)) || (e = arg_num(cx, &a[1], &m)) || (e = arg_num(cx, &a[2], &d))) return val_err(e);
  if (y < 0 || y > 9999 || m < -120000 || m > 120000 || d < -3000000 || d > 3000000) return val_err(E_NUM);
  s = date_serial((int)y, (int)m, (int)d);
  if (s < 0) return val_err(E_NUM);
  return val_num(s);
}
static Value f_time(Ctx *cx, Value *a, int n)
{
  double h, m, s, t;
  int e;
  (void)n;
  if ((e = arg_num(cx, &a[0], &h)) || (e = arg_num(cx, &a[1], &m)) || (e = arg_num(cx, &a[2], &s))) return val_err(e);
  t = (floor(h) * 3600 + floor(m) * 60 + floor(s)) / 86400.0;
  if (t < 0) return val_err(E_NUM);
  return val_num(t - floor(t));
}
static int arg_date(Ctx *cx, const Value *v, double *d)
{
  Value s = scalar(cx, v);
  int e = 0, k;
  if (s.t == V_STR) { if (!parse_number(s.s, d, &k)) e = E_VALUE; }
  else e = to_num(&s, d);
  val_free(&s);
  if (!e && *d < 0) e = E_NUM;
  return e;
}
static Value ymd_part(Ctx *cx, Value *a, int part)
{
  double d;
  int y, m, dd, e = arg_date(cx, &a[0], &d);
  if (e) return val_err(e);
  serial_ymd(d, &y, &m, &dd);
  return val_num(part == 0 ? y : part == 1 ? m : dd);
}
static Value f_year(Ctx *cx, Value *a, int n) { (void)n; return ymd_part(cx, a, 0); }
static Value f_month(Ctx *cx, Value *a, int n) { (void)n; return ymd_part(cx, a, 1); }
static Value f_day(Ctx *cx, Value *a, int n) { (void)n; return ymd_part(cx, a, 2); }
static Value hms_part(Ctx *cx, Value *a, int part)
{
  double d, secs;
  int e = arg_date(cx, &a[0], &d);
  if (e) return val_err(e);
  secs = floor((d - floor(d)) * 86400 + 0.5);
  if (part == 0) return val_num(floor(secs / 3600));
  if (part == 1) return val_num(floor(fmod(secs, 3600) / 60));
  return val_num(fmod(secs, 60));
}
static Value f_hour(Ctx *cx, Value *a, int n) { (void)n; return hms_part(cx, a, 0); }
static Value f_minute(Ctx *cx, Value *a, int n) { (void)n; return hms_part(cx, a, 1); }
static Value f_second(Ctx *cx, Value *a, int n) { (void)n; return hms_part(cx, a, 2); }
static Value f_weekday(Ctx *cx, Value *a, int n)
{
  double d, type = 1;
  int e, w;
  if ((e = arg_date(cx, &a[0], &d))) return val_err(e);
  if (n > 1 && (e = arg_num(cx, &a[1], &type))) return val_err(e);
  w = serial_weekday(d);                          /* 0 Sunday */
  switch ((int)type) {
  case 1: case 17: return val_num(w + 1);
  case 2: case 11: return val_num(w == 0 ? 7 : w);
  case 3: return val_num(w == 0 ? 6 : w - 1);
  case 12: case 13: case 14: case 15: case 16: { int start = (int)type - 10; return val_num(((w - start) % 7 + 7) % 7 + 1); }
  default: return val_err(E_NUM);
  }
}
static Value f_weeknum(Ctx *cx, Value *a, int n)
{
  double d, type = 1, jan1;
  int e, y, m, dd, wd;
  if ((e = arg_date(cx, &a[0], &d))) return val_err(e);
  if (n > 1 && (e = arg_num(cx, &a[1], &type))) return val_err(e);
  serial_ymd(d, &y, &m, &dd);
  jan1 = date_serial(y, 1, 1);
  if (type == 21) {
    /* ISO weeks */
    int wdi = (serial_weekday(d) + 6) % 7;        /* Monday 0 */
    double thursday = floor(d) - wdi + 3;
    int ty, tm, td;
    serial_ymd(thursday, &ty, &tm, &td);
    return val_num(floor((thursday - date_serial(ty, 1, 1)) / 7) + 1);
  }
  wd = serial_weekday(jan1);
  {
    int start = type == 2 || type == 11 ? 1 : type >= 12 && type <= 17 ? (int)type - 10 : 0;
    int off = ((wd - start) % 7 + 7) % 7;
    return val_num(floor((floor(d) - jan1 + off) / 7) + 1);
  }
}
static Value f_edate(Ctx *cx, Value *a, int n)
{
  double d, k;
  int e, y, m, dd;
  static const int mdays[12] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
  (void)n;
  if ((e = arg_date(cx, &a[0], &d)) || (e = arg_num(cx, &a[1], &k))) return val_err(e);
  serial_ymd(d, &y, &m, &dd);
  m += (int)k;
  while (m > 12) { m -= 12; y++; }
  while (m < 1) { m += 12; y--; }
  {
    int lim = mdays[m - 1] + (m == 2 && ((y % 4 == 0 && y % 100 != 0) || y % 400 == 0));
    if (dd > lim) dd = lim;
  }
  return val_num(date_serial(y, m, dd));
}
static Value f_eomonth(Ctx *cx, Value *a, int n)
{
  double d, k;
  int e, y, m, dd;
  (void)n;
  if ((e = arg_date(cx, &a[0], &d)) || (e = arg_num(cx, &a[1], &k))) return val_err(e);
  serial_ymd(d, &y, &m, &dd);
  return val_num(date_serial(y, m + (int)k + 1, 1) - 1);
}
static Value f_days(Ctx *cx, Value *a, int n)
{
  double x, y;
  int e;
  (void)n;
  if ((e = arg_date(cx, &a[0], &x)) || (e = arg_date(cx, &a[1], &y))) return val_err(e);
  return val_num(floor(x) - floor(y));
}
static Value f_datedif(Ctx *cx, Value *a, int n)
{
  double s, f;
  char *unit;
  int e, y1, m1, d1, y2, m2, d2;
  Value r;
  (void)n;
  if ((e = arg_date(cx, &a[0], &s)) || (e = arg_date(cx, &a[1], &f))) return val_err(e);
  if (s > f) return val_err(E_NUM);
  if ((e = arg_str(cx, &a[2], &unit))) return val_err(e);
  serial_ymd(s, &y1, &m1, &d1);
  serial_ymd(f, &y2, &m2, &d2);
  if (str_ieq(unit, "D")) r = val_num(floor(f) - floor(s));
  else if (str_ieq(unit, "M") || str_ieq(unit, "Y") || str_ieq(unit, "YM")) {
    int months = (y2 - y1) * 12 + (m2 - m1) - (d2 < d1);
    r = str_ieq(unit, "M") ? val_num(months) : str_ieq(unit, "Y") ? val_num(months / 12) : val_num(months % 12);
  } else if (str_ieq(unit, "MD")) {
    int dd = d2 - d1;
    if (dd < 0) { int pm = m2 - 1, py = y2; if (pm < 1) { pm = 12; py--; } dd = (int)(date_serial(y2, m2, 1) - date_serial(py, pm, d1)); }
    r = val_num(dd);
  } else if (str_ieq(unit, "YD")) {
    double anniv = date_serial(y2, m1, d1);
    if (anniv > f) anniv = date_serial(y2 - 1, m1, d1);
    r = val_num(floor(f) - anniv);
  } else r = val_err(E_NUM);
  free(unit);
  return r;
}
/* working days between two dates, Saturdays and Sundays off, holidays (a range or array) too */
typedef struct { double *h; int n; } Hol;
static int hol_cb(Ctx *cx, const Value *v, int direct, void *arg)
{
  Hol *h = (Hol *)arg;
  (void)cx; (void)direct;
  if (v->t == V_NUM && h->n < 1000) h->h[h->n++] = floor(v->n);
  return 0;
}
static int is_holiday(const Hol *h, double d) { int i; for (i = 0; i < h->n; i++) if (h->h[i] == d) return 1; return 0; }
static Value f_networkdays(Ctx *cx, Value *a, int n)
{
  double s, f, d, step, cnt = 0, hb[1000];
  int e;
  Hol h;
  h.h = hb; h.n = 0;
  if ((e = arg_date(cx, &a[0], &s)) || (e = arg_date(cx, &a[1], &f))) return val_err(e);
  if (n > 2) each_value(cx, &a[2], hol_cb, &h);
  s = floor(s); f = floor(f);
  step = s <= f ? 1 : -1;
  if (fabs(f - s) > 400000) return val_err(E_NUM);
  for (d = s; step > 0 ? d <= f : d >= f; d += step) {
    int w = serial_weekday(d);
    if (w != 0 && w != 6 && !is_holiday(&h, d)) cnt++;
  }
  return val_num(step > 0 ? cnt : -cnt);
}
static Value f_workday(Ctx *cx, Value *a, int n)
{
  double s, k, d, hb[1000];
  int e;
  long left;
  Hol h;
  h.h = hb; h.n = 0;
  if ((e = arg_date(cx, &a[0], &s)) || (e = arg_num(cx, &a[1], &k))) return val_err(e);
  if (n > 2) each_value(cx, &a[2], hol_cb, &h);
  d = floor(s);
  left = (long)k;
  if (left > 100000 || left < -100000) return val_err(E_NUM);
  while (left != 0) {
    int w;
    d += left > 0 ? 1 : -1;
    w = serial_weekday(d);
    if (w != 0 && w != 6 && !is_holiday(&h, d)) left += left > 0 ? -1 : 1;
  }
  return val_num(d);
}
static Value f_datevalue(Ctx *cx, Value *a, int n)
{
  char *s;
  double d;
  int e, k;
  (void)n;
  if ((e = arg_str(cx, &a[0], &s))) return val_err(e);
  e = parse_number(s, &d, &k) && (k == 1 || k == 2);
  free(s);
  return e ? val_num(floor(d)) : val_err(E_VALUE);
}
static Value f_timevalue(Ctx *cx, Value *a, int n)
{
  char *s;
  double d;
  int e, k;
  (void)n;
  if ((e = arg_str(cx, &a[0], &s))) return val_err(e);
  e = parse_number(s, &d, &k) && (k >= 2 && k <= 3);
  free(s);
  return e ? val_num(d - floor(d)) : val_err(E_VALUE);
}
static Value f_yearfrac(Ctx *cx, Value *a, int n)
{
  double s, f, basis = 0;
  int e, y1, m1, d1, y2, m2, d2;
  if ((e = arg_date(cx, &a[0], &s)) || (e = arg_date(cx, &a[1], &f))) return val_err(e);
  if (n > 2 && (e = arg_num(cx, &a[2], &basis))) return val_err(e);
  if (s > f) { double t = s; s = f; f = t; }
  serial_ymd(s, &y1, &m1, &d1);
  serial_ymd(f, &y2, &m2, &d2);
  switch ((int)basis) {
  case 0: if (d1 == 31) d1 = 30; if (d2 == 31 && d1 >= 30) d2 = 30; return val_num(((y2 - y1) * 360.0 + (m2 - m1) * 30 + (d2 - d1)) / 360);
  case 1: return val_num((f - s) / 365.25);
  case 2: return val_num((f - s) / 360);
  case 3: return val_num((f - s) / 365);
  case 4: if (d1 == 31) d1 = 30; if (d2 == 31) d2 = 30; return val_num(((y2 - y1) * 360.0 + (m2 - m1) * 30 + (d2 - d1)) / 360);
  default: return val_err(E_NUM);
  }
}

/* ------------------------------------------------------------------ finance */
/* payments: rate per period, nper periods, pv present value, fv future value, type 0 end/1 start */
static double fin_pmt(double rate, double nper, double pv, double fv, int type)
{
  if (rate == 0) return -(pv + fv) / nper;
  {
    double p = pow(1 + rate, nper);
    return -(rate * (fv + pv * p)) / ((1 + rate * type) * (p - 1));
  }
}
static double fin_fv(double rate, double nper, double pmt, double pv, int type)
{
  if (rate == 0) return -(pv + pmt * nper);
  {
    double p = pow(1 + rate, nper);
    return -(pv * p + pmt * (1 + rate * type) * (p - 1) / rate);
  }
}
static double fin_pv(double rate, double nper, double pmt, double fv, int type)
{
  if (rate == 0) return -(fv + pmt * nper);
  {
    double p = pow(1 + rate, nper);
    return -(fv + pmt * (1 + rate * type) * (p - 1) / rate) / p;
  }
}
static int fin_args(Ctx *cx, Value *a, int n, double *x, int count)
{
  int i, e;
  for (i = 0; i < count; i++) {
    x[i] = 0;
    if (i < n && !IS_MISSING(&a[i]) && (e = arg_num(cx, &a[i], &x[i]))) return e;
  }
  return 0;
}
static Value f_pmt(Ctx *cx, Value *a, int n) { double x[5]; int e = fin_args(cx, a, n, x, 5); if (e) return val_err(e); if (x[1] == 0) return val_err(E_NUM); return num_or_err(fin_pmt(x[0], x[1], x[2], x[3], x[4] != 0)); }
static Value f_fv(Ctx *cx, Value *a, int n) { double x[5]; int e = fin_args(cx, a, n, x, 5); if (e) return val_err(e); return num_or_err(fin_fv(x[0], x[1], x[2], x[3], x[4] != 0)); }
static Value f_pv(Ctx *cx, Value *a, int n) { double x[5]; int e = fin_args(cx, a, n, x, 5); if (e) return val_err(e); return num_or_err(fin_pv(x[0], x[1], x[2], x[3], x[4] != 0)); }
static Value f_nper(Ctx *cx, Value *a, int n)
{
  double x[5], r;
  int e = fin_args(cx, a, n, x, 5);
  if (e) return val_err(e);
  if (x[0] == 0) { if (x[1] == 0) return val_err(E_NUM); return num_or_err(-(x[2] + x[3]) / x[1]); }
  {
    double t = x[4] != 0, num = x[1] * (1 + x[0] * t) - x[3] * x[0], den = x[2] * x[0] + x[1] * (1 + x[0] * t);
    if (num / den <= 0) return val_err(E_NUM);
    r = log(num / den) / log(1 + x[0]);
  }
  return num_or_err(r);
}
static double fin_ipmt(double rate, double per, double nper, double pv, double fv, int type)
{
  double pmt = fin_pmt(rate, nper, pv, fv, type), ip;
  if (per == 1 && type) return 0;
  ip = fin_fv(rate, per - 1, pmt, pv, type) * rate;
  if (type) ip /= 1 + rate;
  return ip;
}
static Value f_ipmt(Ctx *cx, Value *a, int n) { double x[6]; int e = fin_args(cx, a, n, x, 6); if (e) return val_err(e); if (x[1] < 1 || x[1] > x[2]) return val_err(E_NUM); return num_or_err(fin_ipmt(x[0], x[1], x[2], x[3], x[4], x[5] != 0)); }
static Value f_ppmt(Ctx *cx, Value *a, int n)
{
  double x[6];
  int e = fin_args(cx, a, n, x, 6);
  if (e) return val_err(e);
  if (x[1] < 1 || x[1] > x[2]) return val_err(E_NUM);
  return num_or_err(fin_pmt(x[0], x[2], x[3], x[4], x[5] != 0) - fin_ipmt(x[0], x[1], x[2], x[3], x[4], x[5] != 0));
}
static Value f_npv(Ctx *cx, Value *a, int n)
{
  double rate, s = 0;
  Nums ns;
  long i;
  int e;
  if ((e = arg_num(cx, &a[0], &rate))) return val_err(e);
  if (gather(cx, a + 1, n - 1, &ns, 0)) { free(ns.v); return val_err(ns.err); }
  for (i = 0; i < ns.n; i++) s += ns.v[i] / pow(1 + rate, (double)i + 1);
  free(ns.v);
  return num_or_err(s);
}
static Value f_irr(Ctx *cx, Value *a, int n)
{
  Nums ns;
  double r = 0.1;
  int it, e;
  if (n > 1 && !IS_MISSING(&a[1]) && (e = arg_num(cx, &a[1], &r))) return val_err(e);
  if (gather(cx, a, 1, &ns, 0)) { free(ns.v); return val_err(ns.err); }
  for (it = 0; it < 100; it++) {
    double f = 0, df = 0, nr;
    long i;
    for (i = 0; i < ns.n; i++) {
      double p = pow(1 + r, (double)i);
      f += ns.v[i] / p;
      df -= i * ns.v[i] / (p * (1 + r));
    }
    if (df == 0) break;
    nr = r - f / df;
    if (fabs(nr - r) < 1e-10) { free(ns.v); return val_num(nr); }
    r = nr;
    if (r <= -1) break;
  }
  free(ns.v);
  return val_err(E_NUM);
}
static Value f_rate(Ctx *cx, Value *a, int n)
{
  double x[6], r;
  int it, e = fin_args(cx, a, n, x, 6);
  if (e) return val_err(e);
  r = n > 5 && !IS_MISSING(&a[5]) ? x[5] : 0.1;
  for (it = 0; it < 100; it++) {
    /* Newton on f(r) = pv*(1+r)^n + pmt*(1+r*t)*((1+r)^n-1)/r + fv */
    double t = x[4] != 0, p, f, df, h = 1e-7, nr;
    if (r == 0) r = 1e-9;
    p = pow(1 + r, x[0]);
    f = x[2] * p + x[1] * (1 + r * t) * (p - 1) / r + x[3];
    {
      double r2 = r + h, p2 = pow(1 + r2, x[0]);
      double f2 = x[2] * p2 + x[1] * (1 + r2 * t) * (p2 - 1) / r2 + x[3];
      df = (f2 - f) / h;
    }
    if (df == 0) break;
    nr = r - f / df;
    if (fabs(nr - r) < 1e-10) return val_num(nr);
    r = nr;
  }
  return val_err(E_NUM);
}
static Value f_sln(Ctx *cx, Value *a, int n) { double x[3]; int e = fin_args(cx, a, n, x, 3); if (e) return val_err(e); if (x[2] == 0) return val_err(E_DIV0); return val_num((x[0] - x[1]) / x[2]); }

/* ------------------------------------------------------------------ the table: names must stay in alphabetical order (binary search) */
#define V FF_VOLATILE
static const FuncDef core_defs[] = {
  { "ABS", 1, 1, 0, f_abs }, { "ACOS", 1, 1, 0, f_acos }, { "ADDRESS", 2, 5, 0, f_address }, { "AND", 1, 255, 0, f_and },
  { "ASIN", 1, 1, 0, f_asin }, { "ATAN", 1, 1, 0, f_atan }, { "ATAN2", 2, 2, 0, f_atan2 }, { "AVEDEV", 1, 255, 0, f_avedev },
  { "AVERAGE", 1, 255, 0, f_average }, { "AVERAGEA", 1, 255, 0, f_averagea }, { "AVERAGEIF", 2, 3, 0, f_averageif },
  { "AVERAGEIFS", 3, 255, 0, f_averageifs }, { "CEILING", 1, 2, 0, f_ceiling }, { "CEILING.MATH", 1, 3, 0, f_ceilingmath },
  { "CHAR", 1, 1, 0, f_char }, { "CHOOSE", 2, 255, 0, f_choose }, { "CLEAN", 1, 1, 0, f_clean }, { "CODE", 1, 1, 0, f_code },
  { "COLUMN", 0, 1, 0, f_column }, { "COLUMNS", 1, 1, 0, f_columns }, { "COMBIN", 2, 2, 0, f_combin }, { "CONCAT", 1, 255, 0, f_concat },
  { "CONCATENATE", 1, 255, 0, f_concatenate }, { "CORREL", 2, 2, 0, f_correl }, { "COS", 1, 1, 0, f_cos }, { "COSH", 1, 1, 0, f_cosh },
  { "COUNT", 1, 255, 0, f_count }, { "COUNTA", 1, 255, 0, f_counta }, { "COUNTBLANK", 1, 1, 0, f_countblank },
  { "COUNTIF", 2, 2, 0, f_countif }, { "COUNTIFS", 2, 255, 0, f_countifs }, { "DATE", 3, 3, 0, f_date },
  { "DATEDIF", 3, 3, 0, f_datedif }, { "DATEVALUE", 1, 1, 0, f_datevalue }, { "DAY", 1, 1, 0, f_day }, { "DAYS", 2, 2, 0, f_days },
  { "DEGREES", 1, 1, 0, f_degrees }, { "DOLLAR", 1, 2, 0, f_dollar }, { "EDATE", 2, 2, 0, f_edate }, { "EOMONTH", 2, 2, 0, f_eomonth },
  { "ERROR.TYPE", 1, 1, 0, f_errortype }, { "EVEN", 1, 1, 0, f_even }, { "EXACT", 2, 2, 0, f_exact }, { "EXP", 1, 1, 0, f_exp },
  { "FACT", 1, 1, 0, f_fact }, { "FALSE", 0, 0, 0, f_false }, { "FIND", 2, 3, 0, f_find }, { "FIXED", 1, 3, 0, f_fixed },
  { "FLOOR", 1, 2, 0, f_floor }, { "FLOOR.MATH", 1, 3, 0, f_floormath }, { "FV", 3, 5, 0, f_fv }, { "GCD", 1, 255, 0, f_gcd },
  { "HLOOKUP", 3, 4, 0, f_hlookup }, { "HOUR", 1, 1, 0, f_hour }, { "HYPERLINK", 1, 2, 0, f_hyperlink }, { "IF", 2, 3, 0, f_if }, { "IFERROR", 2, 2, 0, f_iferror },
  { "IFNA", 2, 2, 0, f_ifna }, { "IFS", 2, 254, 0, f_ifs }, { "INDEX", 2, 4, 0, f_index }, { "INDIRECT", 1, 2, V, f_indirect },
  { "INT", 1, 1, 0, f_int }, { "IPMT", 4, 6, 0, f_ipmt }, { "IRR", 1, 2, 0, f_irr }, { "ISBLANK", 1, 1, 0, f_isblank },
  { "ISERR", 1, 1, 0, f_iserr }, { "ISERROR", 1, 1, 0, f_iserror }, { "ISEVEN", 1, 1, 0, f_iseven }, { "ISFORMULA", 1, 1, 0, f_isformula },
  { "ISLOGICAL", 1, 1, 0, f_islogical }, { "ISNA", 1, 1, 0, f_isna }, { "ISNONTEXT", 1, 1, 0, f_isnontext },
  { "ISNUMBER", 1, 1, 0, f_isnumber }, { "ISODD", 1, 1, 0, f_isodd }, { "ISREF", 1, 1, 0, f_isref }, { "ISTEXT", 1, 1, 0, f_istext },
  { "LARGE", 2, 2, 0, f_large }, { "LCM", 1, 255, 0, f_lcm }, { "LEFT", 1, 2, 0, f_left }, { "LEN", 1, 1, 0, f_len },
  { "LN", 1, 1, 0, f_ln }, { "LOG", 1, 2, 0, f_log }, { "LOG10", 1, 1, 0, f_log10 }, { "LOOKUP", 2, 3, 0, f_lookup },
  { "LOWER", 1, 1, 0, f_lower }, { "MATCH", 2, 3, 0, f_match }, { "MAX", 1, 255, 0, f_max }, { "MAXA", 1, 255, 0, f_maxa },
  { "MAXIFS", 3, 255, 0, f_maxifs }, { "MEDIAN", 1, 255, 0, f_median }, { "MID", 3, 3, 0, f_mid }, { "MIN", 1, 255, 0, f_min },
  { "MINA", 1, 255, 0, f_mina }, { "MINIFS", 3, 255, 0, f_minifs }, { "MINUTE", 1, 1, 0, f_minute }, { "MOD", 2, 2, 0, f_mod },
  { "MODE", 1, 255, 0, f_mode }, { "MODE.SNGL", 1, 255, 0, f_mode }, { "MONTH", 1, 1, 0, f_month }, { "MROUND", 2, 2, 0, f_mround },
  { "N", 1, 1, 0, f_n }, { "NA", 0, 0, 0, f_na }, { "NETWORKDAYS", 2, 3, 0, f_networkdays }, { "NOT", 1, 1, 0, f_not },
  { "NOW", 0, 0, V, f_now }, { "NPER", 3, 5, 0, f_nper }, { "NPV", 2, 255, 0, f_npv }, { "NUMBERVALUE", 1, 3, 0, f_numbervalue },
  { "ODD", 1, 1, 0, f_odd }, { "OFFSET", 3, 5, V, f_offset }, { "OR", 1, 255, 0, f_or }, { "PERCENTILE", 2, 2, 0, f_percentile },
  { "PERCENTILE.EXC", 2, 2, 0, f_percentileexc }, { "PERCENTILE.INC", 2, 2, 0, f_percentile }, { "PERMUT", 2, 2, 0, f_permut },
  { "PI", 0, 0, 0, f_pi }, { "PMT", 3, 5, 0, f_pmt }, { "POWER", 2, 2, 0, f_power }, { "PPMT", 4, 6, 0, f_ppmt },
  { "PRODUCT", 1, 255, 0, f_product }, { "PROPER", 1, 1, 0, f_proper }, { "PV", 3, 5, 0, f_pv }, { "QUARTILE", 2, 2, 0, f_quartile },
  { "QUARTILE.INC", 2, 2, 0, f_quartile }, { "QUOTIENT", 2, 2, 0, f_quotient }, { "RADIANS", 1, 1, 0, f_radians },
  { "RAND", 0, 0, V, f_rand }, { "RANDBETWEEN", 2, 2, V, f_randbetween }, { "RANK", 2, 3, 0, f_rank }, { "RANK.EQ", 2, 3, 0, f_rank },
  { "RATE", 3, 6, 0, f_rate }, { "REPLACE", 4, 4, 0, f_replace }, { "REPT", 2, 2, 0, f_rept }, { "RIGHT", 1, 2, 0, f_right },
  { "ROUND", 2, 2, 0, f_round }, { "ROUNDDOWN", 2, 2, 0, f_rounddown }, { "ROUNDUP", 2, 2, 0, f_roundup }, { "ROW", 0, 1, 0, f_row },
  { "ROWS", 1, 1, 0, f_rows }, { "SEARCH", 2, 3, 0, f_search }, { "SECOND", 1, 1, 0, f_second }, { "SIGN", 1, 1, 0, f_sign },
  { "SIN", 1, 1, 0, f_sin }, { "SINH", 1, 1, 0, f_sinh }, { "SLN", 3, 3, 0, f_sln }, { "SMALL", 2, 2, 0, f_small },
  { "SQRT", 1, 1, 0, f_sqrt }, { "STDEV", 1, 255, 0, f_stdev }, { "STDEV.P", 1, 255, 0, f_stdevp }, { "STDEV.S", 1, 255, 0, f_stdev },
  { "STDEVA", 1, 255, 0, f_stdeva }, { "STDEVP", 1, 255, 0, f_stdevp }, { "SUBSTITUTE", 3, 4, 0, f_substitute },
  { "SUM", 1, 255, 0, f_sum }, { "SUMIF", 2, 3, 0, f_sumif }, { "SUMIFS", 3, 255, 0, f_sumifs }, { "SUMPRODUCT", 1, 255, 0, f_sumproduct },
  { "SUMSQ", 1, 255, 0, f_sumsq }, { "SWITCH", 3, 255, 0, f_switch }, { "T", 1, 1, 0, f_t }, { "TAN", 1, 1, 0, f_tan },
  { "TANH", 1, 1, 0, f_tanh }, { "TEXT", 2, 2, 0, f_text }, { "TEXTJOIN", 3, 255, 0, f_textjoin }, { "TIME", 3, 3, 0, f_time },
  { "TIMEVALUE", 1, 1, 0, f_timevalue }, { "TODAY", 0, 0, V, f_today }, { "TRANSPOSE", 1, 1, 0, f_transpose }, { "TRIM", 1, 1, 0, f_trim },
  { "TRUE", 0, 0, 0, f_true }, { "TRUNC", 1, 2, 0, f_trunc }, { "TYPE", 1, 1, 0, f_type }, { "UNICHAR", 1, 1, 0, f_unichar },
  { "UPPER", 1, 1, 0, f_upper }, { "VALUE", 1, 1, 0, f_value }, { "VAR", 1, 255, 0, f_var }, { "VAR.P", 1, 255, 0, f_varp },
  { "VAR.S", 1, 255, 0, f_var }, { "VARP", 1, 255, 0, f_varp }, { "VLOOKUP", 3, 4, 0, f_vlookup }, { "WEEKDAY", 1, 2, 0, f_weekday },
  { "WEEKNUM", 1, 2, 0, f_weeknum }, { "WORKDAY", 2, 3, 0, f_workday }, { "XLOOKUP", 3, 6, 0, f_xlookup }, { "XOR", 1, 255, 0, f_xor },
  { "YEAR", 1, 1, 0, f_year }, { "YEARFRAC", 2, 3, 0, f_yearfrac },
};
#undef V
/* the functions of the other files of the library, each its own table in name order; merged with
   the core's into one table in name order the first time a function is looked up */
extern const FuncDef func_stat_defs[], func_eng_defs[], func_fin_defs[], func_misc_defs[], func_array_defs[];
extern const int func_stat_count, func_eng_count, func_fin_count, func_misc_count, func_array_count;
extern const FuncDef table_func_defs[];
extern const int table_func_count;
#define MAXFUNCS 640
static FuncDef merged[MAXFUNCS];
const FuncDef *func_table = merged;
int func_count;
static int def_cmp(const void *a, const void *b) { return strcmp(((const FuncDef *)a)->name, ((const FuncDef *)b)->name); }
static void add_defs(const FuncDef *d, int n)
{
  int i, j;
  for (i = 0; i < n && func_count < MAXFUNCS; i++) {
    for (j = 0; j < func_count; j++) if (!strcmp(merged[j].name, d[i].name)) break;
    if (j < func_count) merged[j] = d[i];      /* a later table's version replaces the core's */
    else merged[func_count++] = d[i];
  }
}
void func_init(void)
{
  if (func_count) return;
  add_defs(core_defs, (int)(sizeof core_defs / sizeof core_defs[0]));
  add_defs(table_func_defs, table_func_count);   /* (table.c: SUBTOTAL for totals rows, unless a later table has its own) */
  add_defs(func_stat_defs, func_stat_count);
  add_defs(func_eng_defs, func_eng_count);
  add_defs(func_fin_defs, func_fin_count);
  add_defs(func_misc_defs, func_misc_count);
  add_defs(func_array_defs, func_array_count);
  qsort(merged, (size_t)func_count, sizeof merged[0], def_cmp);
}

int func_lookup(const char *name)
{
  int lo = 0, hi, mid, c;
  char up[64];
  size_t i;
  func_init();
  hi = func_count - 1;
  for (i = 0; name[i] && i < 63; i++) up[i] = (char)toupper((u8)name[i]);
  up[i] = 0;
  while (lo <= hi) {
    mid = (lo + hi) / 2; c = strcmp(up, func_table[mid].name);
    if (!c) return mid;
    if (c < 0) hi = mid - 1; else lo = mid + 1;
  }
  return -1;
}
const char *func_name(int id) { return id >= 0 && id < func_count ? func_table[id].name : "?"; }
int func_volatile(int id) { return id >= 0 && id < func_count && (func_table[id].flags & FF_VOLATILE); }
