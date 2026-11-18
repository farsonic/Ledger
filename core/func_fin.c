/*
 * func_fin.c - the financial and database worksheet functions (see func.c for the conventions).
 * Its table, func_fin_defs, is in name order and merged with the others by func_init.
 *
 * Written from Excel's documentation of each function (its formulas, day-count bases and
 * examples). Dates are serial numbers, truncated to whole days. The day-count bases:
 *   0 US (NASD) 30/360, 1 actual/actual, 2 actual/360, 3 actual/365, 4 European 30/360.
 * Coupon dates run back from maturity in steps of 12/frequency months; a maturity on the last day
 * of a month puts every coupon on the last day of its month.
 *
 * The one-line descriptions for the Insert Function dialog are in docs/functions-fin.txt.
 */
#include "calc.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>

/* ------------------------------------------------------------------ arguments */
/* argument i as a number; def when missing (or absent) */
static int get_num(Ctx *cx, Value *a, int n, int i, double def, double *out)
{
  *out = def;
  if (i >= n || IS_MISSING(&a[i])) return 0;
  return arg_num(cx, &a[i], out);
}
/* a date: truncated to a whole day, #NUM! before 1900 or after 9999 */
static int get_date(Ctx *cx, Value *a, int n, int i, double *out)
{
  int e;
  if (i >= n || IS_MISSING(&a[i])) return E_VALUE;
  if ((e = arg_num(cx, &a[i], out))) return e;
  *out = floor(*out);
  if (*out < 0 || *out > 2958465) return E_NUM;
  return 0;
}
static int get_basis(Ctx *cx, Value *a, int n, int i, int *basis)
{
  double b;
  int e = get_num(cx, a, n, i, 0, &b);
  if (e) return e;
  b = floor(b);
  if (b < 0 || b > 4) return E_NUM;
  *basis = (int)b;
  return 0;
}
static int get_freq(Ctx *cx, Value *a, int n, int i, int *freq)
{
  double f;
  int e;
  if (i >= n || IS_MISSING(&a[i])) return E_VALUE;
  if ((e = arg_num(cx, &a[i], &f))) return e;
  f = floor(f);
  if (f != 1 && f != 2 && f != 4) return E_NUM;
  *freq = (int)f;
  return 0;
}
/* the first count arguments as numbers (missing ones 0) */
static int nums(Ctx *cx, Value *a, int n, double *x, int count)
{
  int i, e;
  for (i = 0; i < count; i++) if ((e = get_num(cx, a, n, i, 0, &x[i]))) return e;
  return 0;
}

/* ------------------------------------------------------------------ dates and day counts */
static int is_leap(int y) { return (y % 4 == 0 && y % 100 != 0) || y % 400 == 0; }
static int days_in(int y, int m)
{
  static const unsigned char md[12] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
  return m == 2 && is_leap(y) ? 29 : md[m - 1];
}
/* 30/360 days from s1 to s2: US (NASD, as Excel's financial functions count it) or European; us 2
   is YEARFRAC's US count, which tests the end-31 rule against the start's own day (before a February
   end becomes the 30th: YEARFRAC(28 Feb 2021, 31 Mar 2021) is 31/360 in Excel) */
static double days360(double s1, double s2, int us)
{
  int y1, m1, d1, y2, m2, d2;
  serial_ymd(s1, &y1, &m1, &d1);
  serial_ymd(s2, &y2, &m2, &d2);
  if (us) {
    int feb1 = m1 == 2 && d1 == days_in(y1, 2), feb2 = m2 == 2 && d2 == days_in(y2, 2), start30 = d1 >= 30;
    if (feb1 && feb2) d2 = 30;
    if (feb1) d1 = 30;
    if (d2 == 31 && (us == 2 ? start30 : d1 >= 30)) d2 = 30;
    if (d1 == 31) d1 = 30;
  } else {
    if (d1 == 31) d1 = 30;
    if (d2 == 31) d2 = 30;
  }
  return (y2 - y1) * 360.0 + (m2 - m1) * 30.0 + (d2 - d1);
}
/* 30/360 days as the coupon functions count them: a month's last day is its 30th; US: an end on
   the 31st after a start before the 30th (or in February) stays the 31st, an end on February's last
   day stays its 28th or 29th; European: February's last day is its 28th or 29th */
static double days360c(double s1, double s2, int us)
{
  int y1, m1, d1, y2, m2, d2, o2;
  double r;
  serial_ymd(s1, &y1, &m1, &d1);
  serial_ymd(s2, &y2, &m2, &d2);
  o2 = d2;
  if (d1 >= days_in(y1, m1) || d1 > 30) d1 = 30;
  if (d2 >= days_in(y2, m2) || d2 > 30) d2 = 30;
  if (us) {
    if ((m1 == 2 || d1 < 30) && o2 == 31) d2 = 31;
    else if (m2 == 2 && o2 == days_in(y2, 2)) d2 = o2;
  } else {
    if (m1 == 2 && d1 == 30) d1 = days_in(y1, 2);
    if (m2 == 2 && d2 == 30) d2 = days_in(y2, 2);
  }
  r = (y2 - y1) * 360.0 + (m2 - m1) * 30.0 + (d2 - d1);
  return r > 0 ? r : 0;
}
/* days from s1 to s2 within a coupon schedule, under a basis */
static double day_count(double s1, double s2, int basis)
{
  if (basis == 0) return days360c(s1, s2, 1);
  if (basis == 4) return days360c(s1, s2, 0);
  return s2 - s1;
}
/* YEARFRAC as Excel works it out, basis 1 included (a year's length is 365 or 366 depending on
   the 29 Februarys in the span, or the average of the years it touches when longer than a year) */
static double year_frac(double s1, double s2, int basis)
{
  int y1, m1, d1, y2, m2, d2;
  if (s1 > s2) { double t = s1; s1 = s2; s2 = t; }
  switch (basis) {
  case 0: return days360(s1, s2, 1) / 360;
  case 2: return (s2 - s1) / 360;
  case 3: return (s2 - s1) / 365;
  case 4: return days360(s1, s2, 0) / 360;
  default: break;
  }
  serial_ymd(s1, &y1, &m1, &d1);
  serial_ymd(s2, &y2, &m2, &d2);
  if (y1 == y2) return (s2 - s1) / (is_leap(y1) ? 366 : 365);
  if (y2 == y1 + 1 && (m2 < m1 || (m2 == m1 && d2 <= d1))) {
    int f1 = is_leap(y1) && (m1 < 2 || (m1 == 2 && d1 <= 29));
    int f2 = is_leap(y2) && (m2 > 2 || (m2 == 2 && d2 == 29));
    return (s2 - s1) / (f1 || f2 ? 366 : 365);
  }
  return (s2 - s1) / ((date_serial(y2 + 1, 1, 1) - date_serial(y1, 1, 1)) / (y2 - y1 + 1));
}
/* the date months after (before, when negative) an anchor date; eom keeps it on a month's end */
static double shift_months(int y, int m, int d, long months, int eom)
{
  long total = (long)y * 12 + (m - 1) + months;
  int ny = (int)(total / 12), nm = (int)(total % 12) + 1, dim;
  if (total < 0) return -1;
  dim = days_in(ny, nm);
  return date_serial(ny, nm, eom || d > dim ? dim : d);
}

/* the coupon period holding settlement: the coupon dates before (pcd) and after it (ncd), and the
   number of coupons still to be paid */
typedef struct { double pcd, ncd; long num; } Coup;
static int coupons(double settle, double mat, int freq, Coup *c)
{
  int y, m, d, ys, ms, ds, eom, step = 12 / freq;
  long k;
  if (settle >= mat) return E_NUM;
  serial_ymd(mat, &y, &m, &d);
  serial_ymd(settle, &ys, &ms, &ds);
  eom = d == days_in(y, m);
  k = ((long)(y - ys) * 12 + (m - ms)) / step - 1;
  if (k < 1) k = 1;
  while (shift_months(y, m, d, -k * step, eom) > settle) k++;
  c->pcd = shift_months(y, m, d, -k * step, eom);
  c->ncd = shift_months(y, m, d, -(k - 1) * step, eom);
  c->num = k;
  return 0;
}
/* the coupon period's length in days (E), days from its start to settlement (A) and from
   settlement to the next coupon (DSC), as COUPDAYS, COUPDAYBS and COUPDAYSNC give them */
static double coup_days(const Coup *c, int freq, int basis)
{
  return basis == 1 ? c->ncd - c->pcd : basis == 3 ? 365.0 / freq : 360.0 / freq;
}
static double coup_daybs(const Coup *c, double settle, int basis) { return day_count(c->pcd, settle, basis); }
static double coup_daysnc(const Coup *c, double settle, int freq, int basis)
{
  if (basis == 0 || basis == 4) return coup_days(c, freq, basis) - coup_daybs(c, settle, basis);
  return c->ncd - settle;
}

/* ------------------------------------------------------------------ COUP* */
static Value coup_impl(Ctx *cx, Value *a, int n, int what)
{
  double s, m;
  int f, b, e;
  Coup c;
  if ((e = get_date(cx, a, n, 0, &s)) || (e = get_date(cx, a, n, 1, &m)) || (e = get_freq(cx, a, n, 2, &f)) ||
      (e = get_basis(cx, a, n, 3, &b))) return val_err(e);
  if ((e = coupons(s, m, f, &c))) return val_err(e);
  switch (what) {
  case 0: return val_num(coup_daybs(&c, s, b));
  case 1: return val_num(coup_days(&c, f, b));
  case 2: return val_num(coup_daysnc(&c, s, f, b));
  case 3: return val_num(c.ncd);
  case 4: return val_num((double)c.num);
  default: return val_num(c.pcd);
  }
}
static Value f_coupdaybs(Ctx *cx, Value *a, int n) { return coup_impl(cx, a, n, 0); }
static Value f_coupdays(Ctx *cx, Value *a, int n) { return coup_impl(cx, a, n, 1); }
static Value f_coupdaysnc(Ctx *cx, Value *a, int n) { return coup_impl(cx, a, n, 2); }
static Value f_coupncd(Ctx *cx, Value *a, int n) { return coup_impl(cx, a, n, 3); }
static Value f_coupnum(Ctx *cx, Value *a, int n) { return coup_impl(cx, a, n, 4); }
static Value f_couppcd(Ctx *cx, Value *a, int n) { return coup_impl(cx, a, n, 5); }

/* ------------------------------------------------------------------ bonds: PRICE, YIELD, DURATION */
typedef struct { double E, A, DSC; long N; } Bond;
static int bond_terms(double settle, double mat, int freq, int basis, Bond *t)
{
  Coup c;
  int e = coupons(settle, mat, freq, &c);
  if (e) return e;
  t->E = coup_days(&c, freq, basis);
  t->A = coup_daybs(&c, settle, basis);
  t->DSC = coup_daysnc(&c, settle, freq, basis);
  t->N = c.num;
  return 0;
}
/* Excel's PRICE: the coupons and redemption discounted from settlement, less accrued interest */
static double bond_price(const Bond *t, double rate, double yld, double red, int freq)
{
  double cpn = 100 * rate / freq, v = 1 + yld / freq, dsc = t->DSC / t->E, p;
  long k;
  p = red / pow(v, (double)t->N - 1 + dsc);
  for (k = 1; k <= t->N; k++) p += cpn / pow(v, (double)k - 1 + dsc);
  return p - cpn * t->A / t->E;
}
/* solve price(y) = target for y by Newton's method with a bisection guard */
typedef double (*PriceFn)(const void *ctx, double y);
static int solve_yield(PriceFn fn, const void *ctx, double target, double guess, double *out)
{
  double y = guess, lo = -0.999999, hi = 1e10;
  int it;
  for (it = 0; it < 200; it++) {
    double f = fn(ctx, y) - target, h = 1e-7 * (fabs(y) > 1 ? fabs(y) : 1), df, ny;
    if (fabs(f) < 1e-12 * (fabs(target) > 1 ? fabs(target) : 1)) { *out = y; return 0; }
    /* price falls as the yield rises */
    if (f > 0) lo = y; else hi = y;
    df = (fn(ctx, y + h) - fn(ctx, y - h)) / (2 * h);
    ny = df != 0 ? y - f / df : (lo + hi) / 2;
    if (!(ny > lo && ny < hi)) ny = hi < 1e9 ? (lo + hi) / 2 : y * 2 + 1;
    if (fabs(ny - y) < 1e-15 * (fabs(y) > 1 ? fabs(y) : 1)) { *out = ny; return 0; }
    y = ny;
  }
  return E_NUM;
}
typedef struct { const Bond *t; double rate, red; int freq; } BondCtx;
static double bond_price_fn(const void *p, double y) { const BondCtx *c = p; return bond_price(c->t, c->rate, y, c->red, c->freq); }

/* settlement, maturity, rate, yld-or-price, redemption, frequency, [basis] */
static int bond_args(Ctx *cx, Value *a, int n, double *s, double *m, double *x, int *f, int *b, Bond *t)
{
  int e;
  if ((e = get_date(cx, a, n, 0, s)) || (e = get_date(cx, a, n, 1, m)) || (e = get_num(cx, a, n, 2, 0, &x[0])) ||
      (e = get_num(cx, a, n, 3, 0, &x[1])) || (e = get_num(cx, a, n, 4, 0, &x[2])) || (e = get_freq(cx, a, n, 5, f)) ||
      (e = get_basis(cx, a, n, 6, b))) return e;
  if (*s >= *m || x[0] < 0 || x[2] <= 0) return E_NUM;
  return bond_terms(*s, *m, *f, *b, t);
}
static Value f_price(Ctx *cx, Value *a, int n)
{
  double s, m, x[3];
  int f, b, e;
  Bond t;
  if ((e = bond_args(cx, a, n, &s, &m, x, &f, &b, &t))) return val_err(e);
  if (x[1] < 0) return val_err(E_NUM);
  return num_or_err(bond_price(&t, x[0], x[1], x[2], f));
}
static Value f_yield(Ctx *cx, Value *a, int n)
{
  double s, m, x[3], y;
  int f, b, e;
  Bond t;
  BondCtx bc;
  if ((e = bond_args(cx, a, n, &s, &m, x, &f, &b, &t))) return val_err(e);
  if (x[1] <= 0) return val_err(E_NUM);
  if (t.N <= 1) {
    /* one coupon period or less to go: Excel's closed form */
    double cpn = x[0] / f, par = x[1] / 100 + t.A / t.E * cpn;
    return num_or_err((x[2] / 100 + cpn - par) / par * f * t.E / t.DSC);
  }
  bc.t = &t; bc.rate = x[0]; bc.red = x[2]; bc.freq = f;
  if ((e = solve_yield(bond_price_fn, &bc, x[1], x[0] > 0 ? x[0] : 0.05, &y))) return val_err(e);
  return num_or_err(y);
}
/* Macaulay duration of a bond with face value 100, in years */
static Value duration_impl(Ctx *cx, Value *a, int n, int modified)
{
  double s, m, cpn, yld, v, dsc, w = 0, p = 0;
  int f, b, e;
  long k;
  Bond t;
  if ((e = get_date(cx, a, n, 0, &s)) || (e = get_date(cx, a, n, 1, &m)) || (e = get_num(cx, a, n, 2, 0, &cpn)) ||
      (e = get_num(cx, a, n, 3, 0, &yld)) || (e = get_freq(cx, a, n, 4, &f)) || (e = get_basis(cx, a, n, 5, &b))) return val_err(e);
  if (s >= m || cpn < 0 || yld < 0) return val_err(E_NUM);
  if ((e = bond_terms(s, m, f, b, &t))) return val_err(e);
  v = 1 + yld / f;
  dsc = t.DSC / t.E;
  for (k = 1; k <= t.N; k++) {
    double tk = (double)k - 1 + dsc, cf = 100 * cpn / f + (k == t.N ? 100 : 0), pv = cf / pow(v, tk);
    w += tk * pv;
    p += pv;
  }
  if (p == 0) return val_err(E_DIV0);
  w = w / p / f;
  if (modified) w /= v;
  return num_or_err(w);
}
static Value f_duration(Ctx *cx, Value *a, int n) { return duration_impl(cx, a, n, 0); }
static Value f_mduration(Ctx *cx, Value *a, int n) { return duration_impl(cx, a, n, 1); }

/* ------------------------------------------------------------------ odd first and last periods */
/* quasi-coupon dates around an anchor coupon date: q(j) = anchor + j periods */
typedef struct { int y, m, d, eom, step; } Quasi;
static void quasi_init(Quasi *q, double anchor, int freq)
{
  serial_ymd(anchor, &q->y, &q->m, &q->d);
  q->eom = q->d == days_in(q->y, q->m);
  q->step = 12 / freq;
}
static double quasi(const Quasi *q, long j) { return shift_months(q->y, q->m, q->d, j * q->step, q->eom); }
/* the normal length of the quasi-coupon period from p0 to p1 */
static double quasi_len(double p0, double p1, int freq, int basis)
{
  return basis == 1 ? p1 - p0 : basis == 3 ? 365.0 / freq : 360.0 / freq;
}
/* days of [from, to] counted within the quasi period [p0, p1], over its normal length */
static double quasi_part(double p0, double p1, double from, double to, int freq, int basis)
{
  double lo = from > p0 ? from : p0, hi = to < p1 ? to : p1;
  if (hi <= lo) return 0;
  return day_count(lo, hi, basis) / quasi_len(p0, p1, freq, basis);
}

/* an odd (short or long) first period: issue .. first coupon, settlement inside it. The price is
   the cash flows discounted in quasi-coupon periods from settlement (Excel's ODDFPRICE formulas for
   a short and a long first period are both this) */
typedef struct {
  double first_cpn;   /* the first coupon, in coupon units (sum of DC/NL) */
  double accrued;     /* accrued interest at settlement, in coupon units (sum of A/NL) */
  double t1;          /* settlement to the first coupon, in periods (Nq + DSC/NL) */
  long n;             /* coupons after the first one, to maturity */
} OddF;
static int oddf_terms(double settle, double mat, double issue, double first, int freq, int basis, OddF *o)
{
  Quasi q;
  long j, k;
  double p0, p1;
  int ym, mm, dm;
  if (!(mat > first && first > settle && settle > issue)) return E_NUM;
  quasi_init(&q, first, freq);
  o->first_cpn = 0; o->accrued = 0; o->t1 = 0;
  /* walk back from the first coupon over the quasi periods covering issue .. first coupon */
  for (j = 0;; j--) {
    p1 = quasi(&q, j); p0 = quasi(&q, j - 1);
    o->first_cpn += quasi_part(p0, p1, issue, first, freq, basis);
    o->accrued += quasi_part(p0, p1, issue, settle, freq, basis);
    if (settle >= p0 && settle < p1) {
      double nl = basis == 1 ? p1 - p0 : quasi_len(p0, p1, freq, basis), dsc;
      dsc = (basis == 0 || basis == 4) ? nl - day_count(p0, settle, basis) : p1 - settle;
      o->t1 += dsc / nl;
    } else if (settle < p0) o->t1 += 1;
    if (p0 <= issue) break;
    if (j < -100000) return E_NUM;
  }
  /* coupons from the first one to maturity: maturity must fall on the schedule (or the count rounds) */
  serial_ymd(mat, &ym, &mm, &dm);
  k = ((long)(ym - q.y) * 12 + (mm - q.m)) / q.step;
  while (k > 0 && quasi(&q, k) > mat) k--;
  while (quasi(&q, k + 1) <= mat) k++;
  if (quasi(&q, k) < mat) k++;
  o->n = k;
  return 0;
}
typedef struct { const OddF *o; double rate, red; int freq; } OddFCtx;
static double oddf_price(const OddF *o, double rate, double yld, double red, int freq)
{
  double cpn = 100 * rate / freq, v = 1 + yld / freq, p;
  long k;
  p = red / pow(v, o->t1 + (double)o->n);
  p += cpn * o->first_cpn / pow(v, o->t1);
  for (k = 1; k <= o->n; k++) p += cpn / pow(v, o->t1 + (double)k);
  return p - cpn * o->accrued;
}
static double oddf_price_fn(const void *p, double y) { const OddFCtx *c = p; return oddf_price(c->o, c->rate, y, c->red, c->freq); }
/* settlement, maturity, issue, first_coupon, rate, yld-or-price, redemption, frequency, [basis] */
static int oddf_args(Ctx *cx, Value *a, int n, double *x, int *f, OddF *o)
{
  double s, m, is, fc;
  int b, e;
  if ((e = get_date(cx, a, n, 0, &s)) || (e = get_date(cx, a, n, 1, &m)) || (e = get_date(cx, a, n, 2, &is)) ||
      (e = get_date(cx, a, n, 3, &fc)) || (e = get_num(cx, a, n, 4, 0, &x[0])) || (e = get_num(cx, a, n, 5, 0, &x[1])) ||
      (e = get_num(cx, a, n, 6, 0, &x[2])) || (e = get_freq(cx, a, n, 7, f)) || (e = get_basis(cx, a, n, 8, &b))) return e;
  if (x[0] < 0 || x[2] <= 0) return E_NUM;
  return oddf_terms(s, m, is, fc, *f, b, o);
}
static Value f_oddfprice(Ctx *cx, Value *a, int n)
{
  double x[3];
  int f, e;
  OddF o;
  if ((e = oddf_args(cx, a, n, x, &f, &o))) return val_err(e);
  if (x[1] < 0) return val_err(E_NUM);
  return num_or_err(oddf_price(&o, x[0], x[1], x[2], f));
}
static Value f_oddfyield(Ctx *cx, Value *a, int n)
{
  double x[3], y;
  int f, e;
  OddF o;
  OddFCtx c;
  if ((e = oddf_args(cx, a, n, x, &f, &o))) return val_err(e);
  if (x[1] <= 0) return val_err(E_NUM);
  c.o = &o; c.rate = x[0]; c.red = x[2]; c.freq = f;
  if ((e = solve_yield(oddf_price_fn, &c, x[1], x[0] > 0 ? x[0] : 0.05, &y))) return val_err(e);
  return num_or_err(y);
}

/* an odd last period: last interest date .. maturity, settlement after the last interest date.
   Sums over the quasi-coupon periods from the last interest date (Excel's ODDLPRICE formula) */
typedef struct { double dc, a, dsc; } OddL;
static int oddl_terms(double settle, double mat, double last, int freq, int basis, OddL *o)
{
  Quasi q;
  long j;
  if (!(mat > settle && settle > last)) return E_NUM;
  quasi_init(&q, last, freq);
  o->dc = o->a = o->dsc = 0;
  for (j = 0;; j++) {
    double p0 = quasi(&q, j), p1 = quasi(&q, j + 1);
    o->dc += quasi_part(p0, p1, last, mat, freq, basis);
    o->a += quasi_part(p0, p1, last, settle, freq, basis);
    o->dsc += quasi_part(p0, p1, settle, mat, freq, basis);
    if (p1 >= mat) break;
    if (j > 100000) return E_NUM;
  }
  return 0;
}
static int oddl_args(Ctx *cx, Value *a, int n, double *x, int *f, OddL *o)
{
  double s, m, li;
  int b, e;
  if ((e = get_date(cx, a, n, 0, &s)) || (e = get_date(cx, a, n, 1, &m)) || (e = get_date(cx, a, n, 2, &li)) ||
      (e = get_num(cx, a, n, 3, 0, &x[0])) || (e = get_num(cx, a, n, 4, 0, &x[1])) || (e = get_num(cx, a, n, 5, 0, &x[2])) ||
      (e = get_freq(cx, a, n, 6, f)) || (e = get_basis(cx, a, n, 7, &b))) return e;
  if (x[0] < 0 || x[2] <= 0) return E_NUM;
  return oddl_terms(s, m, li, *f, b, o);
}
static Value f_oddlprice(Ctx *cx, Value *a, int n)
{
  double x[3], cpn;
  int f, e;
  OddL o;
  if ((e = oddl_args(cx, a, n, x, &f, &o))) return val_err(e);
  if (x[1] < 0) return val_err(E_NUM);
  cpn = 100 * x[0] / f;
  return num_or_err((x[2] + o.dc * cpn) / (1 + o.dsc * x[1] / f) - o.a * cpn);
}
static Value f_oddlyield(Ctx *cx, Value *a, int n)
{
  double x[3], cpn, par;
  int f, e;
  OddL o;
  if ((e = oddl_args(cx, a, n, x, &f, &o))) return val_err(e);
  if (x[1] <= 0) return val_err(E_NUM);
  cpn = 100 * x[0] / f;
  par = x[1] + o.a * cpn;
  return num_or_err((x[2] + o.dc * cpn - par) / par * f / o.dsc);
}

/* ------------------------------------------------------------------ accrued interest */
static Value f_accrint(Ctx *cx, Value *a, int n)
{
  double is, fi, s, rate, par, sum = 0, start;
  int f, b, e, method = 1;
  long j;
  Quasi q;
  if ((e = get_date(cx, a, n, 0, &is)) || (e = get_date(cx, a, n, 1, &fi)) || (e = get_date(cx, a, n, 2, &s)) ||
      (e = get_num(cx, a, n, 3, 0, &rate)) || (e = get_num(cx, a, n, 4, 1000, &par)) || (e = get_freq(cx, a, n, 5, &f)) ||
      (e = get_basis(cx, a, n, 6, &b))) return val_err(e);
  if (n > 7 && !IS_MISSING(&a[7]) && (e = arg_bool(cx, &a[7], &method))) return val_err(e);
  if (is >= s || rate <= 0 || par <= 0) return val_err(E_NUM);
  quasi_init(&q, fi, f);
  /* the quasi period holding a date: q(j) <= date < q(j+1) */
  start = is;
  if (!method && s > fi) {
    for (j = 0; quasi(&q, j + 1) <= s; j++) ;
    start = quasi(&q, j);
  }
  {
    int ys, ms, ds;
    serial_ymd(start, &ys, &ms, &ds);
    j = ((long)(ys - q.y) * 12 + (ms - q.m)) / q.step - 1;
    while (quasi(&q, j) > start) j--;
    while (quasi(&q, j + 1) <= start) j++;
  }
  for (;; j++) {
    double p0 = quasi(&q, j), p1 = quasi(&q, j + 1);
    if (p0 < 0 || p1 < 0) return val_err(E_NUM);
    if (p0 >= s) break;
    sum += quasi_part(p0, p1, start, s, f, b);
  }
  return num_or_err(par * rate / f * sum);
}
static Value f_accrintm(Ctx *cx, Value *a, int n)
{
  double is, s, rate, par;
  int b, e;
  if ((e = get_date(cx, a, n, 0, &is)) || (e = get_date(cx, a, n, 1, &s)) || (e = get_num(cx, a, n, 2, 0, &rate)) ||
      (e = get_num(cx, a, n, 3, 1000, &par)) || (e = get_basis(cx, a, n, 4, &b))) return val_err(e);
  if (is >= s || rate <= 0 || par <= 0) return val_err(E_NUM);
  return num_or_err(par * rate * year_frac(is, s, b));
}

/* ------------------------------------------------------------------ discounted and at-maturity securities */
/* settlement, maturity, x, y, [basis]: the year fraction from settlement to maturity */
static int sec_args(Ctx *cx, Value *a, int n, double *x, double *yf)
{
  double s, m;
  int b, e;
  if ((e = get_date(cx, a, n, 0, &s)) || (e = get_date(cx, a, n, 1, &m)) || (e = get_num(cx, a, n, 2, 0, &x[0])) ||
      (e = get_num(cx, a, n, 3, 0, &x[1])) || (e = get_basis(cx, a, n, 4, &b))) return e;
  if (s >= m || x[0] <= 0 || x[1] <= 0) return E_NUM;
  *yf = year_frac(s, m, b);
  return 0;
}
/* YEARFRAC, in place of the core's (whose basis 1 took every year as 365.25 days) */
static Value f_yearfrac(Ctx *cx, Value *a, int n)
{
  double s1, s2;
  int e, basis = 0;
  if ((e = get_date(cx, a, n, 0, &s1)) || (e = get_date(cx, a, n, 1, &s2)) || (e = get_basis(cx, a, n, 2, &basis))) return val_err(e);
  if (basis == 0) return num_or_err((s1 <= s2 ? days360(s1, s2, 2) : days360(s2, s1, 2)) / 360);
  return num_or_err(year_frac(s1, s2, basis));
}
static Value f_disc(Ctx *cx, Value *a, int n) { double x[2], yf; int e = sec_args(cx, a, n, x, &yf); return e ? val_err(e) : num_or_err((1 - x[0] / x[1]) / yf); }
static Value f_pricedisc(Ctx *cx, Value *a, int n) { double x[2], yf; int e = sec_args(cx, a, n, x, &yf); return e ? val_err(e) : num_or_err(x[1] * (1 - x[0] * yf)); }
static Value f_yielddisc(Ctx *cx, Value *a, int n) { double x[2], yf; int e = sec_args(cx, a, n, x, &yf); return e ? val_err(e) : num_or_err((x[1] - x[0]) / x[0] / yf); }
static Value f_intrate(Ctx *cx, Value *a, int n) { double x[2], yf; int e = sec_args(cx, a, n, x, &yf); return e ? val_err(e) : num_or_err((x[1] - x[0]) / x[0] / yf); }
static Value f_received(Ctx *cx, Value *a, int n)
{
  double x[2], yf, d;
  int e = sec_args(cx, a, n, x, &yf);
  if (e) return val_err(e);
  d = 1 - x[1] * yf;
  if (d <= 0) return val_err(E_NUM);
  return num_or_err(x[0] / d);
}
/* settlement, maturity, issue, rate, yld-or-price, [basis] */
static int mat_args(Ctx *cx, Value *a, int n, double *x, double *im, double *is, double *sm)
{
  double s, m, iss;
  int b, e;
  if ((e = get_date(cx, a, n, 0, &s)) || (e = get_date(cx, a, n, 1, &m)) || (e = get_date(cx, a, n, 2, &iss)) ||
      (e = get_num(cx, a, n, 3, 0, &x[0])) || (e = get_num(cx, a, n, 4, 0, &x[1])) || (e = get_basis(cx, a, n, 5, &b))) return e;
  if (s >= m || iss >= s || x[0] < 0 || x[1] < 0) return E_NUM;
  *im = year_frac(iss, m, b); *is = year_frac(iss, s, b); *sm = year_frac(s, m, b);
  return 0;
}
static Value f_pricemat(Ctx *cx, Value *a, int n)
{
  double x[2], im, is, sm;
  int e = mat_args(cx, a, n, x, &im, &is, &sm);
  if (e) return val_err(e);
  return num_or_err(100 * ((1 + im * x[0]) / (1 + sm * x[1]) - is * x[0]));
}
static Value f_yieldmat(Ctx *cx, Value *a, int n)
{
  double x[2], im, is, sm;
  int e = mat_args(cx, a, n, x, &im, &is, &sm);
  if (e) return val_err(e);
  if (x[1] <= 0) return val_err(E_NUM);
  return num_or_err(((1 + im * x[0]) / (x[1] / 100 + is * x[0]) - 1) / sm);
}
/* Treasury bills: settlement, maturity (within a year), discount rate or price */
static int tbill_args(Ctx *cx, Value *a, int n, double *dsm, double *x)
{
  double s, m;
  int e, y, mo, d;
  if ((e = get_date(cx, a, n, 0, &s)) || (e = get_date(cx, a, n, 1, &m)) || (e = get_num(cx, a, n, 2, 0, x))) return e;
  if (s >= m || *x <= 0) return E_NUM;
  serial_ymd(s, &y, &mo, &d);
  if (m > shift_months(y, mo, d, 12, 0)) return E_NUM;
  *dsm = m - s;
  return 0;
}
static Value f_tbillprice(Ctx *cx, Value *a, int n)
{
  double dsm, d, p;
  int e = tbill_args(cx, a, n, &dsm, &d);
  if (e) return val_err(e);
  p = 100 * (1 - d * dsm / 360);
  return p <= 0 ? val_err(E_NUM) : num_or_err(p);
}
static Value f_tbillyield(Ctx *cx, Value *a, int n)
{
  double dsm, pr;
  int e = tbill_args(cx, a, n, &dsm, &pr);
  if (e) return val_err(e);
  return num_or_err((100 - pr) / pr * 360 / dsm);
}
static Value f_tbilleq(Ctx *cx, Value *a, int n)
{
  double dsm, d, p, t;
  int e = tbill_args(cx, a, n, &dsm, &d);
  if (e) return val_err(e);
  if (dsm <= 182) {
    double den = 360 - d * dsm;
    return den == 0 ? val_err(E_NUM) : num_or_err(365 * d / den);
  }
  /* longer than half a year: the bond-equivalent yield with a coupon at half-year */
  p = 100 * (1 - d * dsm / 360);
  if (p <= 0) return val_err(E_NUM);
  t = dsm / 365;
  return num_or_err((-t + sqrt(t * t - (2 * t - 1) * (1 - 100 / p))) / (t - 0.5));
}

/* ------------------------------------------------------------------ depreciation */
static Value f_syd(Ctx *cx, Value *a, int n)
{
  double x[4];
  int e = nums(cx, a, n, x, 4);
  if (e) return val_err(e);
  if (x[2] <= 0 || x[3] <= 0 || x[3] > x[2] || x[1] < 0) return val_err(E_NUM);
  return num_or_err((x[0] - x[1]) * (x[2] - x[3] + 1) * 2 / (x[2] * (x[2] + 1)));
}
static Value f_db(Ctx *cx, Value *a, int n)
{
  double cost, salv, life, per, month, rate, total = 0, d = 0;
  int e, p;
  if ((e = get_num(cx, a, n, 0, 0, &cost)) || (e = get_num(cx, a, n, 1, 0, &salv)) || (e = get_num(cx, a, n, 2, 0, &life)) ||
      (e = get_num(cx, a, n, 3, 0, &per)) || (e = get_num(cx, a, n, 4, 12, &month))) return val_err(e);
  per = floor(per); month = floor(month);
  if (cost < 0 || salv < 0 || life <= 0 || per <= 0 || month < 1 || month > 12 || per > life + 1 || (month == 12 && per > life))
    return val_err(E_NUM);
  if (cost == 0) return val_num(0);
  rate = 1 - pow(salv / cost, 1 / life);
  rate = floor(rate * 1000 + 0.5) / 1000;
  for (p = 1; p <= (int)per; p++) {
    if (p == 1) d = cost * rate * month / 12;
    else if (p == (int)floor(life) + 1) d = (cost - total) * rate * (12 - month) / 12;
    else d = (cost - total) * rate;
    total += d;
  }
  return num_or_err(d);
}
/* double-declining balance in period per (which may be fractional) */
static double ddb(double cost, double salv, double life, double per, double factor)
{
  double rate = factor / life, oldv, newv, d;
  if (rate >= 1) { rate = 1; oldv = per == 1 ? cost : 0; }
  else oldv = cost * pow(1 - rate, per - 1);
  newv = cost * pow(1 - rate, per);
  d = newv < salv ? oldv - salv : oldv - newv;
  return d < 0 ? 0 : d;
}
static Value f_ddb(Ctx *cx, Value *a, int n)
{
  double cost, salv, life, per, factor;
  int e;
  if ((e = get_num(cx, a, n, 0, 0, &cost)) || (e = get_num(cx, a, n, 1, 0, &salv)) || (e = get_num(cx, a, n, 2, 0, &life)) ||
      (e = get_num(cx, a, n, 3, 0, &per)) || (e = get_num(cx, a, n, 4, 2, &factor))) return val_err(e);
  if (cost < 0 || salv < 0 || life <= 0 || per <= 0 || per > life || factor <= 0) return val_err(E_NUM);
  return num_or_err(ddb(cost, salv, life, per, factor));
}
/* VDB's declining balance over periods 1 .. per (fractional last), switching to straight line over
   the remaining life life1 when that gives more */
static double vdb_inter(double cost, double salv, double life, double life1, double per, double factor)
{
  double vdb = 0, rest = cost - salv, sl = 0, end = ceil(per);
  int now_sl = 0;
  long i, nend = (long)end;
  for (i = 1; i <= nend; i++) {
    double term;
    if (!now_sl) {
      double g = ddb(cost, salv, life, (double)i, factor);
      sl = rest / (life1 - (double)(i - 1));
      if (sl > g) { term = sl; now_sl = 1; }
      else { term = g; rest -= g; }
    } else term = sl;
    if (i == nend) term *= per + 1 - end;
    vdb += term;
  }
  return vdb;
}
static Value f_vdb(Ctx *cx, Value *a, int n)
{
  double cost, salv, life, st, en, factor, is, ie, r = 0;
  int e, noswitch = 0;
  if ((e = get_num(cx, a, n, 0, 0, &cost)) || (e = get_num(cx, a, n, 1, 0, &salv)) || (e = get_num(cx, a, n, 2, 0, &life)) ||
      (e = get_num(cx, a, n, 3, 0, &st)) || (e = get_num(cx, a, n, 4, 0, &en)) || (e = get_num(cx, a, n, 5, 2, &factor))) return val_err(e);
  if (n > 6 && !IS_MISSING(&a[6]) && (e = arg_bool(cx, &a[6], &noswitch))) return val_err(e);
  if (cost < 0 || salv < 0 || life <= 0 || st < 0 || en < st || en > life || factor <= 0) return val_err(E_NUM);
  is = floor(st); ie = ceil(en);
  if (noswitch) {
    long i;
    for (i = (long)is + 1; i <= (long)ie; i++) {
      double term = ddb(cost, salv, life, (double)i, factor);
      if (i == (long)is + 1) term *= (en < is + 1 ? en : is + 1) - st;
      else if (i == (long)ie) term *= en + 1 - ie;
      r += term;
    }
  } else {
    double part = 0, c;
    if (st > is) {
      double tv = cost - vdb_inter(cost, salv, life, life, is, factor);
      part += (st - is) * vdb_inter(tv, salv, life, life - is, 1, factor);
    }
    if (en < ie) {
      double tv = cost - vdb_inter(cost, salv, life, life, ie - 1, factor);
      part += (ie - en) * vdb_inter(tv, salv, life, life - ie + 1, 1, factor);
    }
    c = cost - vdb_inter(cost, salv, life, life, is, factor);
    r = vdb_inter(c, salv, life, life - is, ie - is, factor) - part;
  }
  return num_or_err(r);
}
/* French depreciation: AMORLINC straight line, AMORDEGRC degressive with a coefficient; the first
   period prorated from the purchase date to the end of the first period */
static int amor_args(Ctx *cx, Value *a, int n, double *x, int *basis)
{
  double dp, fp;
  int e;
  if ((e = get_num(cx, a, n, 0, 0, &x[0])) || (e = get_date(cx, a, n, 1, &dp)) || (e = get_date(cx, a, n, 2, &fp)) ||
      (e = get_num(cx, a, n, 3, 0, &x[1])) || (e = get_num(cx, a, n, 4, 0, &x[2])) || (e = get_num(cx, a, n, 5, 0, &x[3])) ||
      (e = get_basis(cx, a, n, 6, basis))) return e;
  if (*basis == 2 || dp > fp || x[0] <= 0 || x[1] < 0 || x[1] > x[0] || x[2] < 0 || x[3] <= 0) return E_NUM;
  x[2] = floor(x[2]);
  x[4] = year_frac(dp, fp, *basis);
  return 0;
}
static double round0(double x) { return x < 0 ? -floor(-x + 0.5) : floor(x + 0.5); }
static Value f_amorlinc(Ctx *cx, Value *a, int n)
{
  double x[5], one, first, full, delta;
  int b, e = amor_args(cx, a, n, x, &b);
  if (e) return val_err(e);
  one = x[0] * x[3];
  delta = x[0] - x[1];
  first = x[4] * x[3] * x[0];
  if (first > delta) first = delta;
  full = floor((delta - first) / one);
  if (x[2] == 0) return num_or_err(first);
  if (x[2] <= full) return num_or_err(one);
  if (x[2] == full + 1) return num_or_err(delta - one * full - first);
  return val_num(0);
}
static Value f_amordegrc(Ctx *cx, Value *a, int n)
{
  double x[5], life, coef, rate, cost, rest, nrate;
  long p, per;
  int b, e = amor_args(cx, a, n, x, &b);
  if (e) return val_err(e);
  life = 1 / x[3];
  coef = life < 3 ? 1 : life < 5 ? 1.5 : life <= 6 ? 2 : 2.5;
  rate = x[3] * coef;
  cost = x[0];
  nrate = round0(x[4] * rate * cost);
  cost -= nrate;
  rest = cost - x[1];
  per = (long)x[2];
  for (p = 0; p < per; p++) {
    nrate = round0(rate * cost);
    rest -= nrate;
    if (rest < 0) return val_num(per - p <= 1 ? round0(cost * 0.5) : 0);
    cost -= nrate;
  }
  return num_or_err(nrate);
}

/* ------------------------------------------------------------------ loans and rates */
static double pmt_(double rate, double nper, double pv, double fv, int type)
{
  double p;
  if (rate == 0) return -(pv + fv) / nper;
  p = pow(1 + rate, nper);
  return -(rate * (fv + pv * p)) / ((1 + rate * type) * (p - 1));
}
static double fv_(double rate, double nper, double pmt, double pv, int type)
{
  double p;
  if (rate == 0) return -(pv + pmt * nper);
  p = pow(1 + rate, nper);
  return -(pv * p + pmt * (1 + rate * type) * (p - 1) / rate);
}
static double ipmt_(double rate, double per, double nper, double pv, int type)
{
  double pmt = pmt_(rate, nper, pv, 0, type), ip;
  if (per == 1 && type) return 0;
  ip = fv_(rate, per - 1, pmt, pv, type) * rate;
  if (type) ip /= 1 + rate;
  return ip;
}
static Value cum_impl(Ctx *cx, Value *a, int n, int princ)
{
  double x[6], s = 0;
  long p, st, en;
  int e = nums(cx, a, n, x, 6);
  if (e) return val_err(e);
  st = (long)ceil(x[3]); en = (long)floor(x[4]);
  if (x[0] <= 0 || x[1] <= 0 || x[2] <= 0 || x[3] < 1 || x[4] < x[3] || x[4] > x[1] || (x[5] != 0 && x[5] != 1)) return val_err(E_NUM);
  for (p = st; p <= en; p++) {
    double ip = ipmt_(x[0], (double)p, x[1], x[2], x[5] != 0);
    s += princ ? pmt_(x[0], x[1], x[2], 0, x[5] != 0) - ip : ip;
  }
  return num_or_err(s);
}
static Value f_cumipmt(Ctx *cx, Value *a, int n) { return cum_impl(cx, a, n, 0); }
static Value f_cumprinc(Ctx *cx, Value *a, int n) { return cum_impl(cx, a, n, 1); }
static Value f_ispmt(Ctx *cx, Value *a, int n)
{
  double x[4];
  int e = nums(cx, a, n, x, 4);
  if (e) return val_err(e);
  if (x[2] == 0) return val_err(E_DIV0);
  return num_or_err(x[3] * x[0] * (x[1] / x[2] - 1));
}
static Value f_effect(Ctx *cx, Value *a, int n)
{
  double x[2];
  int e = nums(cx, a, n, x, 2);
  if (e) return val_err(e);
  x[1] = floor(x[1]);
  if (x[0] <= 0 || x[1] < 1) return val_err(E_NUM);
  return num_or_err(pow(1 + x[0] / x[1], x[1]) - 1);
}
static Value f_nominal(Ctx *cx, Value *a, int n)
{
  double x[2];
  int e = nums(cx, a, n, x, 2);
  if (e) return val_err(e);
  x[1] = floor(x[1]);
  if (x[0] <= 0 || x[1] < 1) return val_err(E_NUM);
  return num_or_err(x[1] * (pow(1 + x[0], 1 / x[1]) - 1));
}
static Value f_pduration(Ctx *cx, Value *a, int n)
{
  double x[3];
  int e = nums(cx, a, n, x, 3);
  if (e) return val_err(e);
  if (x[0] <= 0 || x[1] <= 0 || x[2] <= 0) return val_err(E_NUM);
  return num_or_err((log(x[2]) - log(x[1])) / log(1 + x[0]));
}
static Value f_rri(Ctx *cx, Value *a, int n)
{
  double x[3];
  int e = nums(cx, a, n, x, 3);
  if (e) return val_err(e);
  if (x[0] <= 0 || x[1] == 0) return val_err(E_NUM);
  return num_or_err(pow(x[2] / x[1], 1 / x[0]) - 1);
}
/* fractional dollar prices: 1.02 in sixteenths is 1 2/16 */
static Value dollar_impl(Ctx *cx, Value *a, int n, int to_dec)
{
  double x, frac, ip, fp, p10;
  int e;
  if ((e = get_num(cx, a, n, 0, 0, &x)) || (e = get_num(cx, a, n, 1, 0, &frac))) return val_err(e);
  frac = floor(frac);
  if (frac < 0) return val_err(E_NUM);
  if (frac == 0) return val_err(E_DIV0);
  p10 = pow(10, ceil(log10(frac)));
  ip = x < 0 ? -floor(-x) : floor(x);
  fp = x - ip;
  return num_or_err(to_dec ? ip + fp * p10 / frac : ip + fp * frac / p10);
}
static Value f_dollarde(Ctx *cx, Value *a, int n) { return dollar_impl(cx, a, n, 1); }
static Value f_dollarfr(Ctx *cx, Value *a, int n) { return dollar_impl(cx, a, n, 0); }

/* FVSCHEDULE: blank cells count as 0, text in cells is #VALUE! */
typedef struct { double p; int err; } Sched;
static int sched_cb(Ctx *cx, const Value *v, int direct, void *arg)
{
  Sched *s = arg;
  double d;
  (void)cx;
  if (v->t == V_EMPTY) return 0;
  if (v->t == V_ERR) { s->err = v->e; return 1; }
  if (v->t == V_NUM) d = v->n;
  else if (direct && !to_num(v, &d)) ;
  else { s->err = E_VALUE; return 1; }
  s->p *= 1 + d;
  return 0;
}
static Value f_fvschedule(Ctx *cx, Value *a, int n)
{
  Sched s;
  int e;
  (void)n;
  if ((e = arg_num(cx, &a[0], &s.p))) return val_err(e);
  s.err = 0;
  each_value(cx, &a[1], sched_cb, &s);
  if (s.err) return val_err(s.err);
  return num_or_err(s.p);
}
static Value f_mirr(Ctx *cx, Value *a, int n)
{
  Nums ns;
  double fr, rr, pos = 0, neg = 0, nn;
  long i;
  int e;
  (void)n;
  if ((e = arg_num(cx, &a[1], &fr)) || (e = arg_num(cx, &a[2], &rr))) return val_err(e);
  if (gather(cx, a, 1, &ns, 0)) { free(ns.v); return val_err(ns.err); }
  for (i = 0; i < ns.n; i++) {
    if (ns.v[i] > 0) pos += ns.v[i] / pow(1 + rr, (double)i);
    else if (ns.v[i] < 0) neg += ns.v[i] / pow(1 + fr, (double)i);
  }
  nn = (double)ns.n;
  free(ns.v);
  if (pos == 0 || neg == 0 || nn < 2) return val_err(E_DIV0);
  /* the positive flows carried forward to the end at the reinvestment rate, against the negative
     ones brought back to the start at the finance rate */
  return num_or_err(pow(-pos * pow(1 + rr, nn - 1) / neg, 1 / (nn - 1)) - 1);
}
/* XNPV and XIRR: values and dates of the same size; every date on or after the first */
static int xflows(Ctx *cx, Value *va, Value *da, double **v, double **d, long *cnt)
{
  Arr *x, *y;
  long i, k;
  int e = 0;
  *v = *d = NULL;
  if (va->t == V_ERR) return va->e;
  if (da->t == V_ERR) return da->e;
  if (!(x = to_array(cx, va, &e))) return e;
  if (!(y = to_array(cx, da, &e))) { arr_free(x); return e; }
  k = (long)x->rows * x->cols;
  if (k != (long)y->rows * y->cols) e = E_NUM;
  else if (!(*v = malloc(sizeof(double) * (size_t)k)) || !(*d = malloc(sizeof(double) * (size_t)k))) e = E_NUM;
  for (i = 0; !e && i < k; i++) {
    const Value *p = &x->v[i], *q = &y->v[i];
    if (p->t == V_ERR) e = p->e;
    else if (q->t == V_ERR) e = q->e;
    else if (p->t != V_NUM || (q->t != V_NUM && to_num(q, &(*d)[i]))) e = E_VALUE;
    else {
      (*v)[i] = p->n;
      if (q->t == V_NUM) (*d)[i] = q->n;
      (*d)[i] = floor((*d)[i]);
      if ((*d)[i] < 0 || (i > 0 && (*d)[i] < (*d)[0])) e = E_NUM;
    }
  }
  arr_free(x); arr_free(y);
  if (e) { free(*v); free(*d); *v = *d = NULL; }
  *cnt = k;
  return e;
}
static double xnpv(double r, const double *v, const double *d, long k)
{
  double s = 0;
  long i;
  for (i = 0; i < k; i++) s += v[i] / pow(1 + r, (d[i] - d[0]) / 365);
  return s;
}
static Value f_xnpv(Ctx *cx, Value *a, int n)
{
  double r, *v, *d, s;
  long k;
  int e;
  (void)n;
  if ((e = arg_num(cx, &a[0], &r))) return val_err(e);
  if ((e = xflows(cx, &a[1], &a[2], &v, &d, &k))) return val_err(e);
  s = r <= -1 ? NAN : xnpv(r, v, d, k);
  free(v); free(d);
  return num_or_err(s);
}
static Value f_xirr(Ctx *cx, Value *a, int n)
{
  double r = 0.1, *v, *d;
  long k, i;
  int e, it, pos = 0, neg = 0;
  if ((e = get_num(cx, a, n, 2, 0.1, &r))) return val_err(e);
  if ((e = xflows(cx, &a[0], &a[1], &v, &d, &k))) return val_err(e);
  for (i = 0; i < k; i++) { if (v[i] > 0) pos = 1; if (v[i] < 0) neg = 1; }
  e = E_NUM;
  if (pos && neg) {
    for (it = 0; it < 200; it++) {
      double f = 0, df = 0, nr;
      for (i = 0; i < k; i++) {
        double t = (d[i] - d[0]) / 365, p = pow(1 + r, t);
        f += v[i] / p;
        df -= t * v[i] / (p * (1 + r));
      }
      if (df == 0 || !isfinite(f)) break;
      nr = r - f / df;
      if (nr <= -1) nr = (r - 1) / 2;
      if (fabs(nr - r) < 1e-12 * (fabs(r) > 1 ? fabs(r) : 1)) { r = nr; e = 0; break; }
      r = nr;
    }
  }
  free(v); free(d);
  return e ? val_err(e) : num_or_err(r);
}

/* ------------------------------------------------------------------ database functions */
/* DSUM(database, field, criteria) and the like. The database's first row names its fields; the
   criteria's first row names fields too, and each row below is a set of conditions that must all
   hold (AND across), a record matching any row (OR down). An empty condition matches anything;
   text without a comparison matches text that begins with it ("Ap" = "Ap*"); "=text" matches
   exactly. A formula in the criteria under a heading that isn't a field (computed criteria) is
   worked out for each record, its relative references moved down to the record's row */
static int text_ieq(const Value *v, const char *s)
{
  return v->t == V_STR && str_ieq(v->s, s);
}
static int db_field(Book *b, const Arr *db, const Value *head, int *col)
{
  int j;
  Buf t;
  if (head->t == V_NUM) {
    double k = floor(head->n);
    if (k < 1 || k > db->cols) return 0;
    *col = (int)k - 1;
    return 1;
  }
  if (head->t == V_EMPTY) return 0;
  buf_init(&t);
  to_text(b, head, &t);
  for (j = 0; j < db->cols; j++) if (text_ieq(&db->v[j], t.data ? t.data : "")) break;
  buf_free(&t);
  if (j == db->cols) return 0;
  *col = j;
  return 1;
}
typedef struct {
  int col;            /* database column, or -1 for a computed criterion */
  int empty;          /* no condition */
  Crit c;
  Formula *f;         /* computed criterion (not owned) */
  i32 r, cc;          /* its cell */
} DbCond;
enum { D_AVG, D_COUNT, D_COUNTA, D_GET, D_MAX, D_MIN, D_PRODUCT, D_STDEV, D_STDEVP, D_SUM, D_VAR, D_VARP };
static Value db_impl(Ctx *cx, Value *a, int n, int kind)
{
  Arr *db = NULL, *cr = NULL;
  DbCond *conds = NULL;
  int e = 0, field = -1, i, j, nc = 0;
  long rec, matches = 0;
  double sum = 0, prod = 1, mx = 0, mn = 0, mean = 0, m2 = 0;
  Value got = val_empty();
  Value res;
  Sheet *csh = NULL;
  if (a[0].t == V_ERR) return val_err(a[0].e);
  if (n > 2 && a[2].t == V_ERR) return val_err(a[2].e);
  if (!(db = to_array(cx, &a[0], &e))) return val_err(e);
  if (n < 3 || !(cr = to_array(cx, &a[2], &e))) { arr_free(db); return val_err(e ? e : E_VALUE); }
  if (db->rows < 1 || cr->rows < 1) { e = E_VALUE; goto done; }
  /* the field */
  if (!IS_MISSING(&a[1])) {
    Value f = scalar(cx, &a[1]);
    if (f.t == V_ERR) e = f.e;
    else if (!db_field(cx->b, db, &f, &field)) e = E_VALUE;
    val_free(&f);
    if (e) goto done;
  } else if (kind != D_COUNT && kind != D_COUNTA) { e = E_VALUE; goto done; }
  /* the conditions, row by row below the criteria's headings */
  if (a[2].t == V_REF && a[2].sheet < cx->b->nsh) csh = cx->b->sh[a[2].sheet];
  if (cr->rows > 1 && !(conds = calloc((size_t)(cr->rows - 1) * cr->cols, sizeof *conds))) { e = E_NUM; goto done; }
  nc = cr->cols;
  for (i = 1; i < cr->rows; i++)
    for (j = 0; j < cr->cols; j++) {
      DbCond *d = &conds[(i - 1) * nc + j];
      const Value *cv = &cr->v[i * nc + j];
      Cell *cell = csh ? sheet_cell(csh, a[2].r1 + i, a[2].c1 + j) : NULL;
      int col;
      d->col = db_field(cx->b, db, &cr->v[j], &col) && cr->v[j].t != V_NUM ? col : -1;
      if (cv->t == V_EMPTY || (cv->t == V_STR && !cv->s[0] && !(cell && cell->f))) { d->empty = 1; continue; }
      if (d->col < 0) {
        if (cell && cell->f) { d->f = cell->f; d->r = a[2].r1 + i; d->cc = a[2].c1 + j; }
        else d->empty = -1;      /* a condition on no field: matches nothing */
        continue;
      }
      if (cv->t == V_STR && !strchr("<>=", cv->s[0])) {
        /* begins with: text*, unless it reads as a number */
        double dn;
        int k;
        if (parse_number(cv->s, &dn, &k)) crit_make(cx, cv, &d->c);
        else {
          size_t L = strlen(cv->s);
          Value t;
          char *s = malloc(L + 2);
          if (!s) { e = E_NUM; goto done; }
          memcpy(s, cv->s, L); s[L] = '*'; s[L + 1] = 0;
          t = val_str(s);
          free(s);
          crit_make(cx, &t, &d->c);
          val_free(&t);
        }
      } else crit_make(cx, cv, &d->c);
    }
  /* each record */
  for (rec = 1; rec < db->rows; rec++) {
    int any = cr->rows < 2;
    for (i = 1; i < cr->rows && !any; i++) {
      int all = 1;
      for (j = 0; j < nc && all; j++) {
        DbCond *d = &conds[(i - 1) * nc + j];
        if (d->empty == 1) continue;
        if (d->empty == -1) { all = 0; continue; }
        if (d->f) {
          Formula *fc = formula_copy(d->f);
          Value v;
          int t = 0;
          if (!fc) { all = 0; continue; }
          formula_offset(fc, (i32)(rec - 1), 0);
          v = book_eval_at(cx->b, a[2].sheet, d->r, d->cc, fc);
          formula_free(fc);
          if (v.t == V_ERR || to_bool(&v, &t)) t = 0;
          val_free(&v);
          if (!t) all = 0;
        } else if (!crit_match(&d->c, &db->v[rec * db->cols + d->col])) all = 0;
      }
      if (all) any = 1;
    }
    if (!any) continue;
    {
      const Value *v = field >= 0 ? &db->v[rec * db->cols + field] : NULL;
      if (kind == D_COUNTA) { if (!v || v->t != V_EMPTY) matches++; continue; }
      if (kind == D_COUNT && !v) { matches++; continue; }
      if (kind == D_GET) {
        if (++matches == 1) got = val_copy(v);
        continue;
      }
      if (v->t != V_NUM) continue;
      matches++;
      sum += v->n; prod *= v->n;
      if (matches == 1 || v->n > mx) mx = v->n;
      if (matches == 1 || v->n < mn) mn = v->n;
      {
        double dlt = v->n - mean;
        mean += dlt / (double)matches;
        m2 += dlt * (v->n - mean);
      }
    }
  }
done:
  if (conds) {
    for (i = 0; i < (cr->rows - 1) * nc; i++) if (!conds[i].empty && !conds[i].f && conds[i].col >= 0) crit_free(&conds[i].c);
    free(conds);
  }
  arr_free(db);
  arr_free(cr);
  if (e) { val_free(&got); return val_err(e); }
  switch (kind) {
  case D_AVG: return matches ? num_or_err(sum / (double)matches) : val_err(E_DIV0);
  case D_COUNT: case D_COUNTA: return val_num((double)matches);
  case D_GET:
    if (matches == 1) { res = got; if (res.t == V_EMPTY) res = val_num(0); return res; }
    val_free(&got);
    return val_err(matches ? E_NUM : E_VALUE);
  case D_MAX: return val_num(matches ? mx : 0);
  case D_MIN: return val_num(matches ? mn : 0);
  case D_PRODUCT: return val_num(matches ? prod : 0);
  case D_SUM: return num_or_err(sum);
  case D_STDEV: return matches > 1 ? num_or_err(sqrt(m2 / (double)(matches - 1))) : val_err(E_DIV0);
  case D_VAR: return matches > 1 ? num_or_err(m2 / (double)(matches - 1)) : val_err(E_DIV0);
  case D_STDEVP: return matches ? num_or_err(sqrt(m2 / (double)matches)) : val_err(E_DIV0);
  default: return matches ? num_or_err(m2 / (double)matches) : val_err(E_DIV0);
  }
}
static Value f_daverage(Ctx *cx, Value *a, int n) { return db_impl(cx, a, n, D_AVG); }
static Value f_dcount(Ctx *cx, Value *a, int n) { return db_impl(cx, a, n, D_COUNT); }
static Value f_dcounta(Ctx *cx, Value *a, int n) { return db_impl(cx, a, n, D_COUNTA); }
static Value f_dget(Ctx *cx, Value *a, int n) { return db_impl(cx, a, n, D_GET); }
static Value f_dmax(Ctx *cx, Value *a, int n) { return db_impl(cx, a, n, D_MAX); }
static Value f_dmin(Ctx *cx, Value *a, int n) { return db_impl(cx, a, n, D_MIN); }
static Value f_dproduct(Ctx *cx, Value *a, int n) { return db_impl(cx, a, n, D_PRODUCT); }
static Value f_dstdev(Ctx *cx, Value *a, int n) { return db_impl(cx, a, n, D_STDEV); }
static Value f_dstdevp(Ctx *cx, Value *a, int n) { return db_impl(cx, a, n, D_STDEVP); }
static Value f_dsum(Ctx *cx, Value *a, int n) { return db_impl(cx, a, n, D_SUM); }
static Value f_dvar(Ctx *cx, Value *a, int n) { return db_impl(cx, a, n, D_VAR); }
static Value f_dvarp(Ctx *cx, Value *a, int n) { return db_impl(cx, a, n, D_VARP); }

/* ------------------------------------------------------------------ the table, in name order */
const FuncDef func_fin_defs[] = {
  { "ACCRINT", 6, 8, 0, f_accrint },
  { "ACCRINTM", 3, 5, 0, f_accrintm },
  { "AMORDEGRC", 6, 7, 0, f_amordegrc },
  { "AMORLINC", 6, 7, 0, f_amorlinc },
  { "COUPDAYBS", 3, 4, 0, f_coupdaybs },
  { "COUPDAYS", 3, 4, 0, f_coupdays },
  { "COUPDAYSNC", 3, 4, 0, f_coupdaysnc },
  { "COUPNCD", 3, 4, 0, f_coupncd },
  { "COUPNUM", 3, 4, 0, f_coupnum },
  { "COUPPCD", 3, 4, 0, f_couppcd },
  { "CUMIPMT", 6, 6, 0, f_cumipmt },
  { "CUMPRINC", 6, 6, 0, f_cumprinc },
  { "DAVERAGE", 3, 3, 0, f_daverage },
  { "DB", 4, 5, 0, f_db },
  { "DCOUNT", 3, 3, 0, f_dcount },
  { "DCOUNTA", 3, 3, 0, f_dcounta },
  { "DDB", 4, 5, 0, f_ddb },
  { "DGET", 3, 3, 0, f_dget },
  { "DISC", 4, 5, 0, f_disc },
  { "DMAX", 3, 3, 0, f_dmax },
  { "DMIN", 3, 3, 0, f_dmin },
  { "DOLLARDE", 2, 2, 0, f_dollarde },
  { "DOLLARFR", 2, 2, 0, f_dollarfr },
  { "DPRODUCT", 3, 3, 0, f_dproduct },
  { "DSTDEV", 3, 3, 0, f_dstdev },
  { "DSTDEVP", 3, 3, 0, f_dstdevp },
  { "DSUM", 3, 3, 0, f_dsum },
  { "DURATION", 5, 6, 0, f_duration },
  { "DVAR", 3, 3, 0, f_dvar },
  { "DVARP", 3, 3, 0, f_dvarp },
  { "EFFECT", 2, 2, 0, f_effect },
  { "FVSCHEDULE", 2, 2, 0, f_fvschedule },
  { "INTRATE", 4, 5, 0, f_intrate },
  { "ISPMT", 4, 4, 0, f_ispmt },
  { "MDURATION", 5, 6, 0, f_mduration },
  { "MIRR", 3, 3, 0, f_mirr },
  { "NOMINAL", 2, 2, 0, f_nominal },
  { "ODDFPRICE", 8, 9, 0, f_oddfprice },
  { "ODDFYIELD", 8, 9, 0, f_oddfyield },
  { "ODDLPRICE", 7, 8, 0, f_oddlprice },
  { "ODDLYIELD", 7, 8, 0, f_oddlyield },
  { "PDURATION", 3, 3, FF_XLFN, f_pduration },
  { "PRICE", 6, 7, 0, f_price },
  { "PRICEDISC", 4, 5, 0, f_pricedisc },
  { "PRICEMAT", 5, 6, 0, f_pricemat },
  { "RECEIVED", 4, 5, 0, f_received },
  { "RRI", 3, 3, FF_XLFN, f_rri },
  { "SYD", 4, 4, 0, f_syd },
  { "TBILLEQ", 3, 3, 0, f_tbilleq },
  { "TBILLPRICE", 3, 3, 0, f_tbillprice },
  { "TBILLYIELD", 3, 3, 0, f_tbillyield },
  { "VDB", 5, 7, 0, f_vdb },
  { "XIRR", 2, 3, 0, f_xirr },
  { "XNPV", 3, 3, 0, f_xnpv },
  { "YEARFRAC", 2, 3, 0, f_yearfrac }, { "YIELD", 6, 7, 0, f_yield },
  { "YIELDDISC", 4, 5, 0, f_yielddisc },
  { "YIELDMAT", 5, 6, 0, f_yieldmat },
};
const int func_fin_count = (int)(sizeof func_fin_defs / sizeof func_fin_defs[0]);
