/*
 * func_misc.c - worksheet functions (see func.c for the conventions): the date functions added
 * after the core set (DAYS360, ISOWEEKNUM, NETWORKDAYS.INTL, WORKDAY.INTL), the text functions
 * (TEXTBEFORE/TEXTAFTER/TEXTSPLIT, the REGEX family with a small regular expression engine of its
 * own, BAHTTEXT, the byte versions ...), INFO, the web functions, the cube functions and
 * EUROCONVERT. Its table, func_misc_defs, is in name order and merged with the others by
 * func_init. docs/functions-misc.txt describes each one.
 */
#include "calc.h"
#include "xml.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>
#include <ctype.h>

#define HAS(i) (n > (i) && !IS_MISSING(&a[i]))

static Value arr_value(Arr *ar) { Value v = val_empty(); v.t = V_ARR; v.a = ar; return v; }
/* a function of the core table by name (the byte versions of the text functions are the same
   functions in a single-byte language) */
static Value call_core(const char *name, Ctx *cx, Value *a, int n)
{
  int id = func_lookup(name);
  if (id < 0) return val_err(E_NAME);
  return func_table[id].fn(cx, a, n);
}

/* ------------------------------------------------------------------ dates */
static int date_arg(Ctx *cx, const Value *v, double *d)
{
  Value s = scalar(cx, v);
  int e = 0, k;
  if (s.t == V_STR) { if (!parse_number(s.s, d, &k)) e = E_VALUE; }
  else e = to_num(&s, d);
  val_free(&s);
  if (!e && *d < 0) e = E_NUM;
  if (!e) *d = floor(*d);
  return e;
}
static int days_in_month(int y, int m)
{
  static const int dm[] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
  if (m == 2 && ((y % 4 == 0 && y % 100 != 0) || y % 400 == 0)) return 29;
  return dm[(m - 1) % 12];
}
static Value f_days360(Ctx *cx, Value *a, int n)
{
  double s, f;
  int e, eu = 0, y1, m1, d1, y2, m2, d2;
  if ((e = date_arg(cx, &a[0], &s)) || (e = date_arg(cx, &a[1], &f))) return val_err(e);
  if (HAS(2) && (e = arg_bool(cx, &a[2], &eu))) return val_err(e);
  serial_ymd(s, &y1, &m1, &d1); serial_ymd(f, &y2, &m2, &d2);
  if (eu) {
    if (d1 == 31) d1 = 30;
    if (d2 == 31) d2 = 30;
  } else {
    /* US (NASD): a start on the last day of its month counts as the 30th; an end on the 31st
       counts as the 30th if the start is the 30th or 31st, else as the 1st of the next month */
    if (d1 == days_in_month(y1, m1)) d1 = 30;
    if (d2 == 31) { if (d1 < 30) { d2 = 1; m2++; } else d2 = 30; }
  }
  return val_num((y2 - y1) * 360.0 + (m2 - m1) * 30.0 + (d2 - d1));
}
static Value f_isoweeknum(Ctx *cx, Value *a, int n)
{
  double d, thu, jan1;
  int e, wd, y, m, dd;
  (void)n;
  if ((e = date_arg(cx, &a[0], &d))) return val_err(e);
  wd = (serial_weekday(d) + 6) % 7;              /* 0 Monday .. 6 Sunday */
  thu = d - wd + 3;
  serial_ymd(thu, &y, &m, &dd);
  jan1 = date_serial(y, 1, 1);
  return val_num(floor((thu - jan1) / 7) + 1);
}
/* the weekend of the .INTL functions: a code 1-7 or 11-17, or seven 0s and 1s from Monday.
   mask[w] for w = 0 Sunday .. 6 Saturday */
static int weekend_arg(Ctx *cx, Value *a, int n, int i, int mask[7])
{
  int k;
  for (k = 0; k < 7; k++) mask[k] = 0;
  if (!HAS(i)) { mask[0] = mask[6] = 1; return 0; }
  {
    Value s = scalar(cx, &a[i]);
    int e = 0;
    if (s.t == V_ERR) e = s.e;
    else if (s.t == V_STR) {
      if (strlen(s.s) != 7) e = E_VALUE;
      else for (k = 0; k < 7; k++) {
        if (s.s[k] != '0' && s.s[k] != '1') { e = E_VALUE; break; }
        mask[(k + 1) % 7] = s.s[k] == '1';
      }
    } else {
      double c;
      if (!(e = to_num(&s, &c))) {
        int code = (int)c;
        if (c != code) e = E_NUM;
        else if (code >= 1 && code <= 7) { mask[(code + 5) % 7] = 1; mask[(code + 6) % 7] = 1; }
        else if (code >= 11 && code <= 17) mask[(code - 11) % 7] = 1;
        else e = E_NUM;
      }
    }
    val_free(&s);
    return e;
  }
}
typedef struct { double *h; int n, cap, err; } Hols;
static int hols_cb(Ctx *cx, const Value *v, int direct, void *arg)
{
  Hols *h = (Hols *)arg;
  double d;
  (void)cx; (void)direct;
  if (v->t == V_ERR) { h->err = v->e; return 1; }
  if (v->t == V_EMPTY) return 0;
  if (v->t == V_STR) { int k; if (!parse_number(v->s, &d, &k)) { h->err = E_VALUE; return 1; } }
  else if (to_num(v, &d)) { h->err = E_VALUE; return 1; }
  if (d < 0) { h->err = E_NUM; return 1; }
  if (h->n < h->cap) h->h[h->n++] = floor(d);
  return 0;
}
static int is_hol(const Hols *h, double d) { int i; for (i = 0; i < h->n; i++) if (h->h[i] == d) return 1; return 0; }
static Value f_networkdaysintl(Ctx *cx, Value *a, int n)
{
  double s, f, d, cnt = 0, hb[1000];
  int e, mask[7], step;
  Hols h;
  h.h = hb; h.n = 0; h.cap = 1000; h.err = 0;
  if ((e = date_arg(cx, &a[0], &s)) || (e = date_arg(cx, &a[1], &f))) return val_err(e);
  if ((e = weekend_arg(cx, a, n, 2, mask))) return val_err(e);
  if (HAS(3)) { each_value(cx, &a[3], hols_cb, &h); if (h.err) return val_err(h.err); }
  if (fabs(f - s) > 400000) return val_err(E_NUM);
  step = s <= f ? 1 : -1;
  for (d = s; step > 0 ? d <= f : d >= f; d += step)
    if (!mask[serial_weekday(d)] && !is_hol(&h, d)) cnt++;
  return val_num(step > 0 ? cnt : -cnt);
}
static Value f_workdayintl(Ctx *cx, Value *a, int n)
{
  double s, k, d, hb[1000];
  int e, mask[7], i, all = 1;
  long left;
  Hols h;
  h.h = hb; h.n = 0; h.cap = 1000; h.err = 0;
  if ((e = date_arg(cx, &a[0], &s)) || (e = arg_num(cx, &a[1], &k))) return val_err(e);
  if ((e = weekend_arg(cx, a, n, 2, mask))) return val_err(e);
  for (i = 0; i < 7; i++) if (!mask[i]) all = 0;
  if (all) return val_err(E_VALUE);
  if (HAS(3)) { each_value(cx, &a[3], hols_cb, &h); if (h.err) return val_err(h.err); }
  k = k < 0 ? ceil(k) : floor(k);
  if (fabs(k) > 1000000) return val_err(E_NUM);
  left = (long)k; d = s;
  while (left != 0) {
    d += left > 0 ? 1 : -1;
    if (d < 0 || d > 2958465) return val_err(E_NUM);
    if (!mask[serial_weekday(d)] && !is_hol(&h, d)) left += left > 0 ? -1 : 1;
  }
  return val_num(d);
}

/* ------------------------------------------------------------------ text helpers */
/* UTF-8 to code points: malloc'd array (one spare element), *len its length */
static u32 *to_cps(const char *s, long *len)
{
  size_t n = strlen(s), i = 0;
  u32 *out = (u32 *)malloc(sizeof(u32) * (n + 1));
  long k = 0;
  if (!out) return NULL;
  while (i < n) { int used; out[k++] = utf8_get(s + i, n - i, &used); i += used > 0 ? (size_t)used : 1; }
  *len = k;
  return out;
}
static void add_cps(Buf *b, const u32 *c, long n)
{
  long i;
  char t[8];
  for (i = 0; i < n; i++) buf_add(b, t, (size_t)utf8_put(t, c[i]));
}
static Value cps_val(const u32 *c, long n)
{
  Buf b;
  Value v;
  buf_init(&b);
  add_cps(&b, c, n);
  v = val_str(b.data ? b.data : "");
  buf_free(&b);
  return v;
}
static u32 fold(u32 c)
{
  if (c >= 'A' && c <= 'Z') return c + 32;
  if ((c >= 0xC0 && c <= 0xDE && c != 0xD7)) return c + 32;
  if (c >= 0x391 && c <= 0x3A9) return c + 32;              /* Greek */
  if (c >= 0x410 && c <= 0x42F) return c + 32;              /* Cyrillic */
  if (c >= 0x100 && c <= 0x17F && !(c & 1)) return c + 1;   /* Latin Extended-A (roughly) */
  return c;
}
static Value f_unicode(Ctx *cx, Value *a, int n)
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
/* ASC: full-width ASCII forms to half-width; DBCS/JIS the other way */
static Value width_conv(Ctx *cx, Value *a, int to_full)
{
  char *s;
  long len, i;
  u32 *c;
  int e;
  Value v;
  if ((e = arg_str(cx, &a[0], &s))) return val_err(e);
  c = to_cps(s, &len);
  free(s);
  if (!c) return val_err(E_NUM);
  for (i = 0; i < len; i++) {
    if (to_full) { if (c[i] >= 0x21 && c[i] <= 0x7E) c[i] += 0xFEE0; else if (c[i] == 0x20) c[i] = 0x3000; }
    else { if (c[i] >= 0xFF01 && c[i] <= 0xFF5E) c[i] -= 0xFEE0; else if (c[i] == 0x3000) c[i] = 0x20; }
  }
  v = cps_val(c, len);
  free(c);
  return v;
}
static Value f_asc(Ctx *cx, Value *a, int n) { (void)n; return width_conv(cx, a, 0); }
static Value f_dbcs(Ctx *cx, Value *a, int n) { (void)n; return width_conv(cx, a, 1); }
static Value f_phonetic(Ctx *cx, Value *a, int n)
{
  char *s;
  int e;
  Value v;
  (void)n;
  if ((e = arg_str(cx, &a[0], &s))) return val_err(e);
  v = val_str(s);
  free(s);
  return v;
}
static Value f_findb(Ctx *cx, Value *a, int n) { return call_core("FIND", cx, a, n); }
static Value f_searchb(Ctx *cx, Value *a, int n) { return call_core("SEARCH", cx, a, n); }
static Value f_leftb(Ctx *cx, Value *a, int n) { return call_core("LEFT", cx, a, n); }
static Value f_rightb(Ctx *cx, Value *a, int n) { return call_core("RIGHT", cx, a, n); }
static Value f_midb(Ctx *cx, Value *a, int n) { return call_core("MID", cx, a, n); }
static Value f_lenb(Ctx *cx, Value *a, int n) { return call_core("LEN", cx, a, n); }
static Value f_replaceb(Ctx *cx, Value *a, int n) { return call_core("REPLACE", cx, a, n); }

/* a number as text, as & writes it (15 significant digits) */
static void val_text(Ctx *cx, const Value *v, int strict, Buf *out)
{
  if (v->t == V_STR && strict) {
    const char *p;
    buf_addc(out, '"');
    for (p = v->s; *p; p++) { if (*p == '"') buf_addc(out, '"'); buf_addc(out, *p); }
    buf_addc(out, '"');
  } else to_text(cx->b, v, out);
}
static int format_arg(Ctx *cx, Value *a, int n, int i, int *strict)
{
  double f = 0;
  int e;
  *strict = 0;
  if (HAS(i) && (e = arg_num(cx, &a[i], &f))) return e;
  if (f != 0 && f != 1) return E_VALUE;
  *strict = f == 1;
  return 0;
}
static Value f_valuetotext(Ctx *cx, Value *a, int n)
{
  int strict, e;
  Value s;
  Buf b;
  if ((e = format_arg(cx, a, n, 1, &strict))) return val_err(e);
  s = scalar(cx, &a[0]);
  buf_init(&b);
  val_text(cx, &s, strict, &b);
  val_free(&s);
  s = val_str(b.data ? b.data : "");
  buf_free(&b);
  return s;
}
static Value f_arraytotext(Ctx *cx, Value *a, int n)
{
  int strict, e;
  i32 rows, cols, r, c;
  Buf b;
  Value v;
  if ((e = format_arg(cx, a, n, 1, &strict))) return val_err(e);
  arg_shape(&a[0], &rows, &cols);
  buf_init(&b);
  if (strict) buf_addc(&b, '{');
  for (r = 0; r < rows; r++) for (c = 0; c < cols; c++) {
    if (r > 0 && c == 0) buf_adds(&b, strict ? ";" : ", ");
    else if (c > 0) buf_adds(&b, strict ? "," : ", ");
    val_text(cx, arg_at(cx, &a[0], r, c), strict, &b);
  }
  if (strict) buf_addc(&b, '}');
  v = val_str(b.data ? b.data : "");
  buf_free(&b);
  return v;
}

/* ------------------------------------------------------------------ TEXTBEFORE, TEXTAFTER, TEXTSPLIT */
typedef struct { u32 *c; long n; } Str32;
typedef struct { Str32 d[64]; int n; } Delims;
static int delim_cb(Ctx *cx, const Value *v, int direct, void *arg)
{
  Delims *ds = (Delims *)arg;
  Buf b;
  (void)direct;
  if (v->t == V_ERR) return 1;
  if (ds->n >= 64) return 1;
  buf_init(&b);
  to_text(cx->b, v, &b);
  ds->d[ds->n].c = to_cps(b.data ? b.data : "", &ds->d[ds->n].n);
  buf_free(&b);
  if (ds->d[ds->n].c) ds->n++;
  return 0;
}
static int delims_get(Ctx *cx, const Value *v, Delims *ds)
{
  ds->n = 0;
  if (v->t == V_ERR) return v->e;
  if (IS_MISSING(v)) return 0;
  if (v->t == V_REF || v->t == V_ARR) {
    i32 r, c, rows, cols;
    arg_shape(v, &rows, &cols);
    for (r = 0; r < rows; r++) for (c = 0; c < cols; c++) {
      const Value *x = arg_at(cx, v, r, c);
      if (x->t == V_ERR) return x->e;
      delim_cb(cx, x, 0, ds);
    }
    return 0;
  }
  { Value s = scalar(cx, v); int e = s.t == V_ERR ? s.e : 0; if (!e) delim_cb(cx, &s, 1, ds); val_free(&s); return e; }
}
static void delims_free(Delims *ds) { int i; for (i = 0; i < ds->n; i++) free(ds->d[i].c); ds->n = 0; }
static int all_empty(const Delims *ds) { int i; for (i = 0; i < ds->n; i++) if (ds->d[i].n) return 0; return 1; }
/* the longest delimiter at t[i..] (or ending at t[..i] when back), 0 if none */
static long delim_at(const u32 *t, long len, long i, const Delims *ds, int icase, int back)
{
  long best = 0;
  int k;
  for (k = 0; k < ds->n; k++) {
    long l = ds->d[k].n, j, st = back ? i - l : i;
    if (!l || l <= best || st < 0 || st + l > len) continue;
    for (j = 0; j < l; j++) {
      u32 x = t[st + j], y = ds->d[k].c[j];
      if (icase ? fold(x) != fold(y) : x != y) break;
    }
    if (j == l) best = l;
  }
  return best;
}
/* the matches of the delimiters in t, start and end of each (malloc'd pairs, *count of them).
   Every position matches with length 0 when all delimiters are empty */
static long *find_delims(const u32 *t, long len, const Delims *ds, int icase, int back, long *count)
{
  long *m = (long *)malloc(sizeof(long) * 2 * (size_t)(len + 2)), k = 0, i;
  if (!m) return NULL;
  if (all_empty(ds)) { for (i = 0; i <= len; i++) { m[2 * k] = m[2 * k + 1] = i; k++; } *count = k; return m; }
  if (!back) {
    for (i = 0; i < len;) { long l = delim_at(t, len, i, ds, icase, 0); if (l) { m[2 * k] = i; m[2 * k + 1] = i + l; k++; i += l; } else i++; }
  } else {
    long j, tmp;
    for (i = len; i > 0;) { long l = delim_at(t, len, i, ds, icase, 1); if (l) { m[2 * k] = i - l; m[2 * k + 1] = i; k++; i -= l; } else i--; }
    for (i = 0, j = k - 1; i < j; i++, j--) {
      tmp = m[2 * i]; m[2 * i] = m[2 * j]; m[2 * j] = tmp;
      tmp = m[2 * i + 1]; m[2 * i + 1] = m[2 * j + 1]; m[2 * j + 1] = tmp;
    }
  }
  *count = k;
  return m;
}
static Value before_after(Ctx *cx, Value *a, int n, int before)
{
  char *s;
  u32 *t;
  long len, cnt, *m, hit = -1, inst = 1, st, en;
  double d;
  int e, mode = 0, mend = 0;
  Delims ds;
  Value r;
  if (HAS(2)) { if ((e = arg_num(cx, &a[2], &d))) return val_err(e); inst = (long)d; }
  if (HAS(3)) { if ((e = arg_num(cx, &a[3], &d))) return val_err(e); if (d != 0 && d != 1) return val_err(E_VALUE); mode = d == 1; }
  if (HAS(4) && (e = arg_bool(cx, &a[4], &mend))) return val_err(e);
  if ((e = arg_str(cx, &a[0], &s))) return val_err(e);
  if ((e = delims_get(cx, &a[1], &ds))) { free(s); return val_err(e); }
  t = to_cps(s, &len);
  free(s);
  if (!t) { delims_free(&ds); return val_err(E_NUM); }
  if (inst == 0 || labs(inst) > (len > 1 ? len : 1)) { free(t); delims_free(&ds); return val_err(E_VALUE); }
  m = find_delims(t, len, &ds, mode, inst < 0, &cnt);
  delims_free(&ds);
  if (!m) { free(t); return val_err(E_NUM); }
  if (mend) {
    if (inst > 0) { if (!cnt || m[2 * (cnt - 1)] != len) { m[2 * cnt] = m[2 * cnt + 1] = len; cnt++; } }
    else if (!cnt || m[0] != 0) { memmove(m + 2, m, sizeof(long) * 2 * (size_t)cnt); m[0] = m[1] = 0; cnt++; }
  }
  if (inst > 0 && inst <= cnt) hit = inst - 1;
  else if (inst < 0 && -inst <= cnt) hit = cnt + inst;
  if (hit < 0) {
    free(t); free(m);
    if (n > 5) return IS_MISSING(&a[5]) ? val_err(E_NA) : scalar(cx, &a[5]);
    return val_err(E_NA);
  }
  st = m[2 * hit]; en = m[2 * hit + 1];
  r = before ? cps_val(t, st) : cps_val(t + en, len - en);
  free(t); free(m);
  return r;
}
static Value f_textbefore(Ctx *cx, Value *a, int n) { return before_after(cx, a, n, 1); }
static Value f_textafter(Ctx *cx, Value *a, int n) { return before_after(cx, a, n, 0); }
static Value f_textsplit(Ctx *cx, Value *a, int n)
{
  char *s;
  u32 *t;
  long len, nr, *rm, i, j, rows = 0, cols = 1;
  int e, mode = 0, ign = 0;
  double d;
  Delims cd, rd;
  Value pad;
  Arr *out;
  /* pieces: per row, the start and end of each cell */
  long *cells = NULL, ncells = 0, cap = 0, *rowstart = NULL;
  if (HAS(3) && (e = arg_bool(cx, &a[3], &ign))) return val_err(e);
  if (HAS(4)) { if ((e = arg_num(cx, &a[4], &d))) return val_err(e); if (d != 0 && d != 1) return val_err(E_VALUE); mode = d == 1; }
  if ((e = arg_str(cx, &a[0], &s))) return val_err(e);
  if ((e = delims_get(cx, &a[1], &cd))) { free(s); return val_err(e); }
  if (n > 2 && (e = delims_get(cx, &a[2], &rd))) { free(s); delims_free(&cd); return val_err(e); }
  if (n <= 2) rd.n = 0;
  if (all_empty(&cd) && all_empty(&rd)) { free(s); delims_free(&cd); delims_free(&rd); return val_err(E_VALUE); }
  t = to_cps(s, &len);
  free(s);
  if (!t) { delims_free(&cd); delims_free(&rd); return val_err(E_NUM); }
  /* the rows */
  if (all_empty(&rd)) { rm = (long *)malloc(sizeof(long) * 2); nr = 0; }
  else rm = find_delims(t, len, &rd, mode, 0, &nr);
  rowstart = (long *)malloc(sizeof(long) * (size_t)(nr + 2));
  if (!rm || !rowstart) { free(t); free(rm); free(rowstart); delims_free(&cd); delims_free(&rd); return val_err(E_NUM); }
  for (i = 0; i <= nr; i++) {
    long rs = i == 0 ? 0 : rm[2 * (i - 1) + 1], re = i < nr ? rm[2 * i] : len, nc, *cm, k, first = ncells;
    if (ign && rs == re) continue;
    if (all_empty(&cd)) { cm = (long *)malloc(sizeof(long) * 2); nc = 0; }
    else cm = find_delims(t + rs, re - rs, &cd, mode, 0, &nc);
    if (!cm) continue;
    for (k = 0; k <= nc; k++) {
      long cs = k == 0 ? 0 : cm[2 * (k - 1) + 1], ce = k < nc ? cm[2 * k] : re - rs;
      if (ign && cs == ce) continue;
      if (ncells + 1 > cap) { long *nv; cap = cap ? cap * 2 : 64; nv = (long *)realloc(cells, sizeof(long) * 2 * (size_t)cap); if (!nv) break; cells = nv; }
      cells[2 * ncells] = rs + cs; cells[2 * ncells + 1] = rs + ce; ncells++;
    }
    free(cm);
    if (ncells == first) continue;
    rowstart[rows++] = first;
    if (ncells - first > cols) cols = ncells - first;
  }
  rowstart[rows] = ncells;
  free(rm);
  delims_free(&cd); delims_free(&rd);
  if (!rows) { free(t); free(cells); free(rowstart); return val_err(E_CALC); }
  if ((long)rows * cols > 1000000L || !(out = arr_new((int)rows, (int)cols))) { free(t); free(cells); free(rowstart); return val_err(E_NUM); }
  pad = n > 5 && !IS_MISSING(&a[5]) ? scalar(cx, &a[5]) : val_err(E_NA);
  for (i = 0; i < rows; i++) {
    long nc = rowstart[i + 1] - rowstart[i];
    for (j = 0; j < cols; j++) {
      Value *v = &out->v[i * cols + j];
      if (j < nc) { long k = rowstart[i] + j; *v = cps_val(t + cells[2 * k], cells[2 * k + 1] - cells[2 * k]); }
      else *v = val_copy(&pad);
    }
  }
  val_free(&pad);
  free(t); free(cells); free(rowstart);
  return arr_value(out);
}

/* ------------------------------------------------------------------ regular expressions */
/* A backtracking matcher for the PCRE-style patterns of REGEXTEST/REGEXEXTRACT/REGEXREPLACE:
   literals and escapes (\d \w \s \b and their negations, \t \n \xhh \x{h..}, \1-\9), ., [...]
   classes with ranges, ^ $, groups (capturing, (?:...), (?<name>...)), lookahead (?=...) (?!...),
   alternation, the quantifiers * + ? {n} {n,} {n,m} and their lazy forms, (?i) at the start.
   The pattern becomes a tree, the tree a small program, and the program runs with an explicit
   backtracking stack (no recursion but for lookaheads) and a step limit. */
enum { RN_CHAR, RN_ANY, RN_CLASS, RN_BOL, RN_EOL, RN_WORDB, RN_NWORDB, RN_BACKREF, RN_GROUP, RN_CAT, RN_ALT, RN_REP, RN_LOOK, RN_EMPTY };
typedef struct { int type, a, b, c, child, next; u32 ch; } RNode;   /* REP: a min, b max (-1 inf), c greedy; GROUP: a capture (-1 none); LOOK: a negative */
enum { RC_D = 1, RC_ND = 2, RC_W = 4, RC_NW = 8, RC_S = 16, RC_NS = 32 };
typedef struct { int neg, flags, n; u32 lo[64], hi[64]; } RClass;
enum { OP_CHAR, OP_ANY, OP_CLASS, OP_BOL, OP_EOL, OP_WORDB, OP_NWORDB, OP_BACKREF, OP_SPLIT, OP_JMP, OP_SAVE, OP_MARK, OP_CHK, OP_LOOK, OP_MATCH };
typedef struct { int op, x, y; u32 ch; } RInst;
#define RX_MAXNODE 4000
#define RX_MAXCODE 20000
#define RX_MAXCLASS 128
#define RX_MAXGROUP 32
typedef struct {
  RNode node[RX_MAXNODE]; int nnode;
  RClass cls[RX_MAXCLASS]; int ncls;
  RInst *code; int ncode;
  int ngroup, nreg, icase, err;
  const u32 *p; long plen, pi;
} Rx;
static int rx_node(Rx *r, int type)
{
  RNode *nd;
  if (r->nnode >= RX_MAXNODE) { r->err = 1; return 0; }
  nd = &r->node[r->nnode];
  memset(nd, 0, sizeof *nd);
  nd->type = type; nd->child = nd->next = -1;
  return r->nnode++;
}
static int rx_peek(Rx *r) { return r->pi < r->plen ? (int)r->p[r->pi] : -1; }
static int hexval(u32 c) { return c >= '0' && c <= '9' ? (int)c - '0' : c >= 'a' && c <= 'f' ? (int)c - 'a' + 10 : c >= 'A' && c <= 'F' ? (int)c - 'A' + 10 : -1; }
/* an escape after the backslash: a character (returns 1, *ch) or a class letter (returns 2, *ch the letter) */
static int rx_escape(Rx *r, u32 *ch)
{
  u32 c;
  if (r->pi >= r->plen) { r->err = 1; return 0; }
  c = r->p[r->pi++];
  switch (c) {
  case 'd': case 'D': case 'w': case 'W': case 's': case 'S': *ch = c; return 2;
  case 'n': *ch = '\n'; return 1;
  case 't': *ch = '\t'; return 1;
  case 'r': *ch = '\r'; return 1;
  case 'f': *ch = '\f'; return 1;
  case 'v': *ch = '\v'; return 1;
  case 'e': *ch = 27; return 1;
  case 'a': *ch = 7; return 1;
  case '0': *ch = 0; return 1;
  case 'x': {
    u32 v = 0;
    if (rx_peek(r) == '{') {
      r->pi++;
      while (r->pi < r->plen && r->p[r->pi] != '}') { int h = hexval(r->p[r->pi++]); if (h < 0) { r->err = 1; return 0; } v = v * 16 + (u32)h; }
      if (r->pi >= r->plen) { r->err = 1; return 0; }
      r->pi++;
    } else {
      int k;
      for (k = 0; k < 2 && r->pi < r->plen && hexval(r->p[r->pi]) >= 0; k++) v = v * 16 + (u32)hexval(r->p[r->pi++]);
    }
    *ch = v; return 1; }
  default:
    if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')) { r->err = 1; return 0; }   /* unknown letter escapes */
    *ch = c; return 1;
  }
}
static int class_flag(u32 letter)
{
  switch (letter) { case 'd': return RC_D; case 'D': return RC_ND; case 'w': return RC_W; case 'W': return RC_NW; case 's': return RC_S; default: return RC_NS; }
}
static int rx_class(Rx *r)
{
  RClass *cl;
  int id, first = 1;
  if (r->ncls >= RX_MAXCLASS) { r->err = 1; return 0; }
  id = r->ncls++;
  cl = &r->cls[id];
  memset(cl, 0, sizeof *cl);
  if (rx_peek(r) == '^') { cl->neg = 1; r->pi++; }
  for (;;) {
    u32 lo, hi;
    int k;
    if (r->pi >= r->plen) { r->err = 1; return 0; }
    if (r->p[r->pi] == ']' && !first) { r->pi++; break; }
    first = 0;
    lo = r->p[r->pi++];
    if (lo == '[' && rx_peek(r) == ':') {          /* [:alpha:] and friends */
      static const char *const names[] = { "alpha", "digit", "alnum", "space", "upper", "lower", "punct", "xdigit", "word", NULL };
      long e = r->pi + 1;
      char nm[16];
      int len = 0, w;
      while (e < r->plen && r->p[e] != ':' && len < 15) nm[len++] = (char)r->p[e++];
      nm[len] = 0;
      if (e + 1 < r->plen && r->p[e] == ':' && r->p[e + 1] == ']') {
        for (w = 0; names[w]; w++) if (!strcmp(names[w], nm)) break;
        if (!names[w]) { r->err = 1; return 0; }
        r->pi = e + 2;
#define ADDR(a_, b_) do { if (cl->n < 64) { cl->lo[cl->n] = (a_); cl->hi[cl->n] = (b_); cl->n++; } } while (0)
        switch (w) {
        case 0: ADDR('a', 'z'); ADDR('A', 'Z'); break;
        case 1: cl->flags |= RC_D; break;
        case 2: ADDR('a', 'z'); ADDR('A', 'Z'); cl->flags |= RC_D; break;
        case 3: cl->flags |= RC_S; break;
        case 4: ADDR('A', 'Z'); break;
        case 5: ADDR('a', 'z'); break;
        case 6: ADDR('!', '/'); ADDR(':', '@'); ADDR('[', '`'); ADDR('{', '~'); break;
        case 7: ADDR('0', '9'); ADDR('a', 'f'); ADDR('A', 'F'); break;
        default: cl->flags |= RC_W; break;
        }
        continue;
      }
    }
    if (lo == '\\') {
      u32 ch;
      k = rx_escape(r, &ch);
      if (r->err) return 0;
      if (k == 2) { cl->flags |= class_flag(ch); continue; }
      lo = ch;
    }
    hi = lo;
    if (rx_peek(r) == '-' && r->pi + 1 < r->plen && r->p[r->pi + 1] != ']') {
      r->pi++;
      hi = r->p[r->pi++];
      if (hi == '\\') { u32 ch; if (rx_escape(r, &ch) != 1) { r->err = 1; return 0; } hi = ch; }
      if (hi < lo) { r->err = 1; return 0; }
    }
    ADDR(lo, hi);
#undef ADDR
  }
  return id;
}
static int rx_alt(Rx *r, int depth);
static int rx_atom(Rx *r, int depth)
{
  int c = rx_peek(r), nd;
  u32 ch;
  if (depth > 50) { r->err = 1; return 0; }
  r->pi++;
  switch (c) {
  case '.': return rx_node(r, RN_ANY);
  case '^': return rx_node(r, RN_BOL);
  case '$': return rx_node(r, RN_EOL);
  case '[': nd = rx_node(r, RN_CLASS); r->node[nd].a = rx_class(r); return nd;
  case '(': {
    int cap = -1, look = -1, inner;
    if (rx_peek(r) == '?') {
      long q = r->pi + 1;
      int k = q < r->plen ? (int)r->p[q] : -1;
      if (k == ':') r->pi += 2;
      else if (k == '=' || k == '!') { look = k == '!'; r->pi += 2; }
      else if (k == '<' && q + 1 < r->plen && r->p[q + 1] != '=' && r->p[q + 1] != '!') {
        r->pi = q + 1;
        while (r->pi < r->plen && r->p[r->pi] != '>') r->pi++;
        if (r->pi >= r->plen) { r->err = 1; return 0; }
        r->pi++;
        cap = ++r->ngroup;
      } else if (k == 'P' && q + 1 < r->plen && r->p[q + 1] == '<') {
        r->pi = q + 2;
        while (r->pi < r->plen && r->p[r->pi] != '>') r->pi++;
        if (r->pi >= r->plen) { r->err = 1; return 0; }
        r->pi++;
        cap = ++r->ngroup;
      } else { r->err = 1; return 0; }
    } else cap = ++r->ngroup;
    if (r->ngroup >= RX_MAXGROUP) { r->err = 1; return 0; }
    inner = rx_alt(r, depth + 1);
    if (rx_peek(r) != ')') { r->err = 1; return 0; }
    r->pi++;
    if (look >= 0) { nd = rx_node(r, RN_LOOK); r->node[nd].a = look; }
    else { nd = rx_node(r, RN_GROUP); r->node[nd].a = cap; }
    r->node[nd].child = inner;
    return nd; }
  case '\\':
    if (rx_peek(r) == 'b' || rx_peek(r) == 'B') { r->pi++; return rx_node(r, r->p[r->pi - 1] == 'b' ? RN_WORDB : RN_NWORDB); }
    if (rx_peek(r) == 'A') { r->pi++; return rx_node(r, RN_BOL); }
    if (rx_peek(r) == 'z' || rx_peek(r) == 'Z') { r->pi++; return rx_node(r, RN_EOL); }
    if (rx_peek(r) >= '1' && rx_peek(r) <= '9') { nd = rx_node(r, RN_BACKREF); r->node[nd].a = (int)(r->p[r->pi++] - '0'); return nd; }
    {
      int k = rx_escape(r, &ch);
      if (r->err) return 0;
      if (k == 2) {
        int id;
        if (r->ncls >= RX_MAXCLASS) { r->err = 1; return 0; }
        id = r->ncls++;
        memset(&r->cls[id], 0, sizeof r->cls[id]);
        r->cls[id].flags = class_flag(ch);
        nd = rx_node(r, RN_CLASS); r->node[nd].a = id;
        return nd;
      }
      nd = rx_node(r, RN_CHAR); r->node[nd].ch = ch;
      return nd;
    }
  case '*': case '+': case '?': case ')': case '|': r->err = 1; return 0;
  default:
    nd = rx_node(r, RN_CHAR); r->node[nd].ch = (u32)c;
    return nd;
  }
}
/* {n}, {n,}, {n,m} at the parser's position: 1 and the bounds, or 0 (then '{' is a literal) */
static int rx_braces(Rx *r, int *mn, int *mx)
{
  long i = r->pi + 1;
  long v1 = -1, v2 = -1;
  int comma = 0;
  while (i < r->plen && r->p[i] >= '0' && r->p[i] <= '9') { v1 = (v1 < 0 ? 0 : v1) * 10 + (long)(r->p[i] - '0'); if (v1 > 1000) return 0; i++; }
  if (i < r->plen && r->p[i] == ',') {
    comma = 1; i++;
    while (i < r->plen && r->p[i] >= '0' && r->p[i] <= '9') { v2 = (v2 < 0 ? 0 : v2) * 10 + (long)(r->p[i] - '0'); if (v2 > 1000) return 0; i++; }
  }
  if (i >= r->plen || r->p[i] != '}' || v1 < 0) return 0;
  *mn = (int)v1; *mx = comma ? (int)v2 : (int)v1;
  if (*mx >= 0 && *mx < *mn) return 0;
  r->pi = i + 1;
  return 1;
}
static int rx_seq(Rx *r, int depth)
{
  int cat = rx_node(r, RN_CAT), last = -1;
  while (!r->err && r->pi < r->plen && rx_peek(r) != '|' && rx_peek(r) != ')') {
    int at, c, mn, mx;
    if (rx_peek(r) == '{' && rx_braces(r, &mn, &mx)) { r->err = 1; break; }
    at = rx_atom(r, depth);
    if (r->err) break;
    for (;;) {
      c = rx_peek(r);
      if (c == '*') { mn = 0; mx = -1; r->pi++; }
      else if (c == '+') { mn = 1; mx = -1; r->pi++; }
      else if (c == '?') { mn = 0; mx = 1; r->pi++; }
      else if (c == '{' && rx_braces(r, &mn, &mx)) { }
      else break;
      {
        int rep = rx_node(r, RN_REP);
        if (r->err) return 0;
        r->node[rep].a = mn; r->node[rep].b = mx; r->node[rep].c = 1; r->node[rep].child = at;
        if (rx_peek(r) == '?') { r->node[rep].c = 0; r->pi++; }
        else if (rx_peek(r) == '+') r->pi++;          /* possessive: taken as greedy */
        at = rep;
      }
    }
    if (last < 0) r->node[cat].child = at; else r->node[last].next = at;
    last = at;
  }
  return cat;
}
static int rx_alt(Rx *r, int depth)
{
  int first = rx_seq(r, depth), alt, last;
  if (rx_peek(r) != '|' || r->err) return first;
  alt = rx_node(r, RN_ALT);
  r->node[alt].child = first; last = first;
  while (!r->err && rx_peek(r) == '|') {
    int s;
    r->pi++;
    s = rx_seq(r, depth);
    r->node[last].next = s; last = s;
  }
  return alt;
}
static int rx_emit(Rx *r, int op, int x, int y, u32 ch)
{
  if (r->ncode >= RX_MAXCODE) { r->err = 1; return 0; }
  r->code[r->ncode].op = op; r->code[r->ncode].x = x; r->code[r->ncode].y = y; r->code[r->ncode].ch = ch;
  return r->ncode++;
}
static void rx_gen(Rx *r, int nd)
{
  RNode *x;
  int k;
  if (r->err || nd < 0) return;
  x = &r->node[nd];
  switch (x->type) {
  case RN_CHAR: rx_emit(r, OP_CHAR, 0, 0, x->ch); break;
  case RN_ANY: rx_emit(r, OP_ANY, 0, 0, 0); break;
  case RN_CLASS: rx_emit(r, OP_CLASS, x->a, 0, 0); break;
  case RN_BOL: rx_emit(r, OP_BOL, 0, 0, 0); break;
  case RN_EOL: rx_emit(r, OP_EOL, 0, 0, 0); break;
  case RN_WORDB: rx_emit(r, OP_WORDB, 0, 0, 0); break;
  case RN_NWORDB: rx_emit(r, OP_NWORDB, 0, 0, 0); break;
  case RN_BACKREF: rx_emit(r, OP_BACKREF, x->a, 0, 0); break;
  case RN_CAT: for (k = x->child; k >= 0; k = r->node[k].next) rx_gen(r, k); break;
  case RN_GROUP:
    if (x->a >= 0) rx_emit(r, OP_SAVE, 2 * x->a, 0, 0);
    rx_gen(r, x->child);
    if (x->a >= 0) rx_emit(r, OP_SAVE, 2 * x->a + 1, 0, 0);
    break;
  case RN_LOOK: {
    int at = rx_emit(r, OP_LOOK, 0, x->a, 0);
    rx_gen(r, x->child);
    rx_emit(r, OP_MATCH, 0, 0, 0);
    if (!r->err) r->code[at].x = r->ncode;
    break; }
  case RN_ALT: {
    int *jumps = (int *)malloc(sizeof(int) * 256), nj = 0, j;
    if (!jumps) { r->err = 1; return; }
    for (k = x->child; k >= 0; k = r->node[k].next) {
      if (r->node[k].next >= 0) {
        int sp = rx_emit(r, OP_SPLIT, 0, 0, 0);
        if (r->err) { free(jumps); return; }
        r->code[sp].x = r->ncode;
        rx_gen(r, k);
        if (nj < 256) jumps[nj++] = rx_emit(r, OP_JMP, 0, 0, 0); else r->err = 1;
        if (r->err) { free(jumps); return; }
        r->code[sp].y = r->ncode;
      } else rx_gen(r, k);
    }
    for (j = 0; j < nj && !r->err; j++) r->code[jumps[j]].x = r->ncode;
    free(jumps);
    break; }
  case RN_REP: {
    int i, greedy = x->c, mn = x->a, mx = x->b, child = x->child;
    for (i = 0; i < mn && !r->err; i++) rx_gen(r, child);
    if (mx < 0) {
      int reg = r->nreg++, top = r->ncode, sp = rx_emit(r, OP_SPLIT, 0, 0, 0);
      if (r->err) return;
      rx_emit(r, OP_MARK, reg, 0, 0);
      if (greedy) r->code[sp].x = sp + 1; else r->code[sp].y = sp + 1;
      rx_gen(r, child);
      rx_emit(r, OP_CHK, reg, 0, 0);
      rx_emit(r, OP_JMP, top, 0, 0);
      if (r->err) return;
      if (greedy) r->code[sp].y = r->ncode; else r->code[sp].x = r->ncode;
    } else {
      int *splits = (int *)malloc(sizeof(int) * (size_t)(mx - mn + 1)), ns = 0;
      if (!splits) { r->err = 1; return; }
      for (i = mn; i < mx && !r->err; i++) {
        int sp = rx_emit(r, OP_SPLIT, 0, 0, 0);
        if (r->err) break;
        splits[ns++] = sp;
        if (greedy) r->code[sp].x = sp + 1; else r->code[sp].y = sp + 1;
        rx_gen(r, child);
      }
      for (i = 0; i < ns && !r->err; i++) { if (greedy) r->code[splits[i]].y = r->ncode; else r->code[splits[i]].x = r->ncode; }
      free(splits);
    }
    break; }
  default: break;
  }
}
/* compile pattern pat; 0 on success (free with rx_free), else E_VALUE */
static int rx_compile(Rx *r, const char *pat, int icase)
{
  long len;
  u32 *p = to_cps(pat, &len);
  int root;
  memset(r, 0, sizeof *r);
  if (!p) return E_VALUE;
  r->p = p; r->plen = len; r->icase = icase;
  if (len >= 4 && p[0] == '(' && p[1] == '?' && p[2] == 'i' && p[3] == ')') { r->icase = 1; r->pi = 4; }
  root = rx_alt(r, 0);
  if (r->pi < r->plen) r->err = 1;              /* an unmatched ) */
  r->code = (RInst *)malloc(sizeof(RInst) * RX_MAXCODE);
  if (!r->code) r->err = 1;
  if (!r->err) { rx_gen(r, root); rx_emit(r, OP_MATCH, 0, 0, 0); }
  free(p); r->p = NULL;
  if (r->nreg > 1000) r->err = 1;
  if (r->err) { free(r->code); r->code = NULL; return E_VALUE; }
  return 0;
}
static void rx_free(Rx *r) { free(r->code); r->code = NULL; }
static int is_word(u32 c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_'; }
static int is_space(u32 c) { return c == ' ' || (c >= 9 && c <= 13); }
static int class_has(const RClass *cl, u32 c, int icase)
{
  int k, in = 0;
  u32 f = fold(c), up = c >= 'a' && c <= 'z' ? c - 32 : c;
  for (k = 0; k < cl->n && !in; k++) {
    if (c >= cl->lo[k] && c <= cl->hi[k]) in = 1;
    else if (icase && ((f >= cl->lo[k] && f <= cl->hi[k]) || (up >= cl->lo[k] && up <= cl->hi[k]))) in = 1;
  }
  if (!in && cl->flags) {
    int d = c >= '0' && c <= '9', w = is_word(c), s = is_space(c);
    if (((cl->flags & RC_D) && d) || ((cl->flags & RC_ND) && !d) || ((cl->flags & RC_W) && w) || ((cl->flags & RC_NW) && !w) ||
        ((cl->flags & RC_S) && s) || ((cl->flags & RC_NS) && !s)) in = 1;
  }
  return cl->neg ? !in : in;
}
/* the backtracking stack: a thread to resume (kind 0) or a slot to restore (1 capture, 2 register) */
typedef struct { int kind, idx; long pos; } RFrame;
typedef struct { const u32 *s; long len; long *caps; long *regs; long steps; } RRun;
static int rx_run(Rx *r, RRun *st, int pc, long sp)
{
  RFrame *stk = NULL;
  long nstk = 0, cap = 0;
  int result = 0;
  for (;;) {
    RInst *in;
    int fail = 0;
    if (++st->steps > 2000000L) { result = -1; break; }
    in = &r->code[pc];
    switch (in->op) {
    case OP_CHAR:
      if (sp < st->len && (st->s[sp] == in->ch || (r->icase && fold(st->s[sp]) == fold(in->ch)))) { sp++; pc++; } else fail = 1;
      break;
    case OP_ANY: if (sp < st->len && st->s[sp] != '\n') { sp++; pc++; } else fail = 1; break;
    case OP_CLASS: if (sp < st->len && class_has(&r->cls[in->x], st->s[sp], r->icase)) { sp++; pc++; } else fail = 1; break;
    case OP_BOL: if (sp == 0) pc++; else fail = 1; break;
    case OP_EOL: if (sp == st->len || (sp == st->len - 1 && st->s[sp] == '\n')) pc++; else fail = 1; break;
    case OP_WORDB: case OP_NWORDB: {
      int b = (sp > 0 && is_word(st->s[sp - 1])) != (sp < st->len && is_word(st->s[sp]));
      if (b == (in->op == OP_WORDB)) pc++; else fail = 1;
      break; }
    case OP_BACKREF: {
      long s0 = st->caps[2 * in->x], s1 = st->caps[2 * in->x + 1], k;
      if (s0 < 0 || s1 < 0) { pc++; break; }
      if (sp + (s1 - s0) > st->len) { fail = 1; break; }
      for (k = 0; k < s1 - s0; k++) {
        u32 x = st->s[s0 + k], y = st->s[sp + k];
        if (x != y && !(r->icase && fold(x) == fold(y))) break;
      }
      if (k < s1 - s0) fail = 1; else { sp += s1 - s0; pc++; }
      break; }
    case OP_LOOK: {
      long *save = (long *)malloc(sizeof(long) * 2 * RX_MAXGROUP);
      int m;
      if (!save) { result = -1; goto done; }
      memcpy(save, st->caps, sizeof(long) * 2 * RX_MAXGROUP);
      m = rx_run(r, st, pc + 1, sp);
      memcpy(st->caps, save, sizeof(long) * 2 * RX_MAXGROUP);
      free(save);
      if (m < 0) { result = -1; goto done; }
      if ((m == 1) != (in->y == 1)) pc = in->x; else fail = 1;
      break; }
    case OP_JMP: pc = in->x; break;
    case OP_SPLIT: case OP_SAVE: case OP_MARK: {
      if (nstk == cap) {
        RFrame *nv;
        cap = cap ? cap * 2 : 256;
        if (cap > 4000000L || !(nv = (RFrame *)realloc(stk, sizeof(RFrame) * (size_t)cap))) { result = -1; goto done; }
        stk = nv;
      }
      if (in->op == OP_SPLIT) { stk[nstk].kind = 0; stk[nstk].idx = in->y; stk[nstk].pos = sp; nstk++; pc = in->x; }
      else if (in->op == OP_SAVE) { stk[nstk].kind = 1; stk[nstk].idx = in->x; stk[nstk].pos = st->caps[in->x]; nstk++; st->caps[in->x] = sp; pc++; }
      else { stk[nstk].kind = 2; stk[nstk].idx = in->x; stk[nstk].pos = st->regs[in->x]; nstk++; st->regs[in->x] = sp; pc++; }
      break; }
    case OP_CHK: if (st->regs[in->x] == sp) fail = 1; else pc++; break;
    case OP_MATCH: st->caps[1] = sp; result = 1; goto done;
    }
    if (fail) {
      for (;;) {
        RFrame *f;
        if (!nstk) { result = 0; goto done; }
        f = &stk[--nstk];
        if (f->kind == 1) st->caps[f->idx] = f->pos;
        else if (f->kind == 2) st->regs[f->idx] = f->pos;
        else { pc = f->idx; sp = f->pos; break; }
      }
    }
  }
done:
  free(stk);
  return result;
}
/* the first match at or after position from: 1 (caps[0..2*RX_MAXGROUP) set), 0 none, -1 too costly */
static int rx_search(Rx *r, const u32 *s, long len, long from, long *caps)
{
  RRun st;
  long start, k;
  int m;
  long *regs = (long *)malloc(sizeof(long) * (size_t)(r->nreg + 1));
  if (!regs) return -1;
  st.s = s; st.len = len; st.caps = caps; st.regs = regs; st.steps = 0;
  for (start = from; start <= len; start++) {
    for (k = 0; k < 2 * RX_MAXGROUP; k++) caps[k] = -1;
    for (k = 0; k <= r->nreg; k++) regs[k] = -1;
    caps[0] = start;
    m = rx_run(r, &st, 0, start);
    if (m) { free(regs); if (m > 0) caps[0] = start; return m; }
  }
  free(regs);
  return 0;
}
/* the common arguments: text, pattern and case_sensitivity (argument ci); E_ error or 0 */
static int rx_args(Ctx *cx, Value *a, int n, int ci, Rx *r, u32 **t, long *len)
{
  char *s, *p;
  int e, icase = 0;
  double d;
  if (HAS(ci)) { if ((e = arg_num(cx, &a[ci], &d))) return e; if (d != 0 && d != 1) return E_VALUE; icase = d == 1; }
  if ((e = arg_str(cx, &a[1], &p))) return e;
  if ((e = arg_str(cx, &a[0], &s))) { free(p); return e; }
  e = rx_compile(r, p, icase);
  free(p);
  if (e) { free(s); return e; }
  *t = to_cps(s, len);
  free(s);
  if (!*t) { rx_free(r); return E_NUM; }
  return 0;
}
static Value f_regextest(Ctx *cx, Value *a, int n)
{
  Rx *r = (Rx *)malloc(sizeof(Rx));
  u32 *t;
  long len, caps[2 * RX_MAXGROUP];
  int e, m;
  if (!r) return val_err(E_NUM);
  if ((e = rx_args(cx, a, n, 2, r, &t, &len))) { free(r); return val_err(e); }
  m = rx_search(r, t, len, 0, caps);
  rx_free(r); free(r); free(t);
  return m < 0 ? val_err(E_VALUE) : val_bool(m);
}
static Value f_regexextract(Ctx *cx, Value *a, int n)
{
  Rx *r = (Rx *)malloc(sizeof(Rx));
  u32 *t;
  long len, caps[2 * RX_MAXGROUP];
  int e, m, mode = 0;
  double d;
  Value res;
  if (!r) return val_err(E_NUM);
  if (HAS(2)) { if ((e = arg_num(cx, &a[2], &d))) { free(r); return val_err(e); } if (d != 0 && d != 1 && d != 2) { free(r); return val_err(E_VALUE); } mode = (int)d; }
  if ((e = rx_args(cx, a, n, 3, r, &t, &len))) { free(r); return val_err(e); }
  m = rx_search(r, t, len, 0, caps);
  if (m <= 0) res = val_err(m < 0 ? E_VALUE : E_NA);
  else if (mode == 0) res = cps_val(t + caps[0], caps[1] - caps[0]);
  else if (mode == 2) {
    int g, ng = r->ngroup;
    Arr *ar;
    if (!ng) res = cps_val(t + caps[0], caps[1] - caps[0]);
    else if (!(ar = arr_new(1, ng))) res = val_err(E_NUM);
    else {
      for (g = 1; g <= ng; g++) ar->v[g - 1] = caps[2 * g] >= 0 && caps[2 * g + 1] >= 0 ? cps_val(t + caps[2 * g], caps[2 * g + 1] - caps[2 * g]) : val_str("");
      res = arr_value(ar);
    }
  } else {
    long *ms = NULL, nm = 0, mcap = 0, pos = 0, i;
    while (m > 0) {
      if (nm == mcap) { long *nv; mcap = mcap ? mcap * 2 : 16; if (mcap > 2000000L || !(nv = (long *)realloc(ms, sizeof(long) * 2 * (size_t)mcap))) { m = -1; break; } ms = nv; }
      ms[2 * nm] = caps[0]; ms[2 * nm + 1] = caps[1]; nm++;
      pos = caps[1] > caps[0] ? caps[1] : caps[1] + 1;
      if (pos > len) break;
      m = rx_search(r, t, len, pos, caps);
    }
    if (m < 0) res = val_err(E_VALUE);
    else {
      Arr *ar = arr_new((int)nm, 1);
      if (!ar) res = val_err(E_NUM);
      else { for (i = 0; i < nm; i++) ar->v[i] = cps_val(t + ms[2 * i], ms[2 * i + 1] - ms[2 * i]); res = arr_value(ar); }
    }
    free(ms);
  }
  rx_free(r); free(r); free(t);
  return res;
}
/* the replacement with $0..$99, ${n} and $$ filled in */
static void rx_expand(Buf *out, const u32 *rep, long rlen, const u32 *t, const long *caps, int ngroup)
{
  long i;
  for (i = 0; i < rlen; i++) {
    if (rep[i] == '$' && i + 1 < rlen) {
      long j = i + 1, g = -1;
      if (rep[j] == '$') { buf_addc(out, '$'); i = j; continue; }
      if (rep[j] == '{') {
        long k = j + 1, v = 0;
        while (k < rlen && rep[k] >= '0' && rep[k] <= '9' && v < 1000) v = v * 10 + (long)(rep[k++] - '0');
        if (k < rlen && rep[k] == '}' && k > j + 1) { g = v; j = k + 1; }
      } else if (rep[j] >= '0' && rep[j] <= '9') {
        g = (long)(rep[j] - '0'); j++;
        if (j < rlen && rep[j] >= '0' && rep[j] <= '9' && g * 10 + (long)(rep[j] - '0') <= ngroup) { g = g * 10 + (long)(rep[j] - '0'); j++; }
      }
      if (g >= 0) {
        if (g <= ngroup && caps[2 * g] >= 0 && caps[2 * g + 1] >= 0) add_cps(out, t + caps[2 * g], caps[2 * g + 1] - caps[2 * g]);
        i = j - 1;
        continue;
      }
    }
    add_cps(out, rep + i, 1);
  }
}
static Value f_regexreplace(Ctx *cx, Value *a, int n)
{
  Rx *r = (Rx *)malloc(sizeof(Rx));
  u32 *t, *rep;
  char *rs;
  long len, rlen, caps[2 * RX_MAXGROUP], pos = 0, last = 0, occ = 0, total = 0, k;
  long (*all)[2 * RX_MAXGROUP] = NULL;
  int e, m;
  double d;
  Buf out;
  Value res;
  if (!r) return val_err(E_NUM);
  if (HAS(3)) { if ((e = arg_num(cx, &a[3], &d))) { free(r); return val_err(e); } occ = (long)d; }
  if ((e = arg_str(cx, &a[2], &rs))) { free(r); return val_err(e); }
  rep = to_cps(rs, &rlen);
  free(rs);
  if (!rep) { free(r); return val_err(E_NUM); }
  if ((e = rx_args(cx, a, n, 4, r, &t, &len))) { free(r); free(rep); return val_err(e); }
  /* every match first (a negative occurrence counts from the end) */
  {
    long cap = 0;
    while ((m = rx_search(r, t, len, pos, caps)) > 0) {
      if (total == cap) {
        long (*nv)[2 * RX_MAXGROUP];
        cap = cap ? cap * 2 : 8;
        if (cap > 100000L || !(nv = (long (*)[2 * RX_MAXGROUP])realloc(all, sizeof(*all) * (size_t)cap))) { m = -1; break; }
        all = nv;
      }
      memcpy(all[total++], caps, sizeof caps);
      pos = caps[1] > caps[0] ? caps[1] : caps[1] + 1;
      if (pos > len) break;
    }
  }
  if (m < 0) { free(all); free(t); free(rep); rx_free(r); free(r); return val_err(E_VALUE); }
  buf_init(&out);
  for (k = 0; k < total; k++) {
    long want = occ > 0 ? occ - 1 : occ < 0 ? total + occ : k;
    if (k != want) continue;
    add_cps(&out, t + last, all[k][0] - last);
    rx_expand(&out, rep, rlen, t, all[k], r->ngroup);
    last = all[k][1];
    if (out.len > 32767) break;
  }
  add_cps(&out, t + last, len - last);
  res = out.len > 32767 ? val_err(E_VALUE) : val_str(out.data ? out.data : "");
  buf_free(&out);
  free(all); free(t); free(rep); rx_free(r); free(r);
  return res;
}

/* ------------------------------------------------------------------ BAHTTEXT */
static const char *const th_digit[] = { "ศูนย์", "หนึ่ง", "สอง", "สาม", "สี่", "ห้า", "หก", "เจ็ด", "แปด", "เก้า" };
/* one block of up to six digits (1..999999) in Thai words; a final one after higher digits is
   เอ็ด (higher: the number has digits in a higher block) */
static void baht_block(Buf *b, long v, int higher)
{
  static const char *const unit[] = { "แสน", "หมื่น", "พัน", "ร้อย" };
  static const long pw[] = { 100000, 10000, 1000, 100 };
  int k;
  long tens, ones;
  for (k = 0; k < 4; k++) {
    long d = (v / pw[k]) % 10;
    if (d) { buf_adds(b, th_digit[d]); buf_adds(b, unit[k]); }
  }
  tens = (v / 10) % 10; ones = v % 10;
  if (tens) {
    if (tens == 2) buf_adds(b, "ยี่");
    else if (tens > 2) buf_adds(b, th_digit[tens]);
    buf_adds(b, "สิบ");
  }
  if (ones == 1 && (v >= 10 || higher)) buf_adds(b, "เอ็ด");
  else if (ones) buf_adds(b, th_digit[ones]);
}
static void baht_number(Buf *b, double v)     /* v >= 1, whole */
{
  if (v >= 1000000) {
    double hi = floor(v / 1000000);
    long lo = (long)(v - hi * 1000000);
    baht_number(b, hi);
    buf_adds(b, "ล้าน");
    if (lo) baht_block(b, lo, 1);
  } else baht_block(b, (long)v, 0);
}
static Value f_bahttext(Ctx *cx, Value *a, int n)
{
  double x, cents, baht;
  int e, neg;
  long sat;
  Buf b;
  Value v;
  (void)n;
  if ((e = arg_num(cx, &a[0], &x))) return val_err(e);
  neg = x < 0; x = fabs(x);
  if (x >= 1e15) return val_err(E_NUM);
  cents = floor(x * 100 + 0.5 + 1e-7);
  baht = floor(cents / 100); sat = (long)(cents - baht * 100);
  buf_init(&b);
  if (neg && cents > 0) buf_adds(&b, "ลบ");
  if (baht >= 1) baht_number(&b, baht);
  else if (!sat) buf_adds(&b, th_digit[0]);
  if (baht >= 1 || !sat) buf_adds(&b, "บาท");
  if (!sat) buf_adds(&b, "ถ้วน");
  else { baht_block(&b, sat, 0); buf_adds(&b, "สตางค์"); }
  v = val_str(b.data ? b.data : "");
  buf_free(&b);
  return v;
}

/* ------------------------------------------------------------------ information, web, cube, add-in */
static Value f_info(Ctx *cx, Value *a, int n)
{
  char *s;
  int e;
  Value v;
  (void)n;
  if ((e = arg_str(cx, &a[0], &s))) return val_err(e);
  if (str_ieq(s, "directory")) v = val_str("PROGDIR:");
  else if (str_ieq(s, "numfile")) v = val_num(cx->b->nsh);
  else if (str_ieq(s, "origin")) v = val_str("$A:$A$1");
  else if (str_ieq(s, "osversion")) v = val_str("AmigaOS 3");
  else if (str_ieq(s, "recalc")) v = val_str("Automatic");
  else if (str_ieq(s, "release")) v = val_str("16.0");
  else if (str_ieq(s, "system")) v = val_str("pcdos");
  else if (str_ieq(s, "memavail") || str_ieq(s, "memused") || str_ieq(s, "totmem")) v = val_err(E_NA);
  else v = val_err(E_VALUE);
  free(s);
  return v;
}
static Value f_encodeurl(Ctx *cx, Value *a, int n)
{
  char *s;
  const unsigned char *p;
  int e;
  Buf b;
  Value v;
  (void)n;
  if ((e = arg_str(cx, &a[0], &s))) return val_err(e);
  buf_init(&b);
  for (p = (const unsigned char *)s; *p; p++) {
    if (isalnum(*p) && *p < 128) buf_addc(&b, *p);
    else if (*p == '-' || *p == '_' || *p == '.' || *p == '~') buf_addc(&b, *p);
    else buf_printf(&b, "%%%02X", *p);
  }
  free(s);
  v = val_str(b.data ? b.data : "");
  buf_free(&b);
  return v;
}
static Value f_webservice(Ctx *cx, Value *a, int n)
{
  char *s;
  int e;
  (void)n;
  if ((e = arg_str(cx, &a[0], &s))) return val_err(e);
  free(s);
  return val_err(E_VALUE);           /* no network from a worksheet */
}
static Value f_cube(Ctx *cx, Value *a, int n) { (void)cx; (void)a; (void)n; return val_err(E_NA); }   /* no cube connections */

/* FILTERXML(xml, xpath): the XML parsed into a tree, then a subset of XPath 1.0: absolute and
   relative steps with / and //, names, *, @attr, text(), . and .., predicates [n], [last()],
   [@a], [@a='v'], [name='v']. The matches as a column, numbers as numbers */
typedef struct XNode { char *name; char *text; char **an, **av; int nattr; struct XNode *parent, *child, *next, *last; int isattr; } XNode;
static XNode *xn_new(const char *name, XNode *parent)
{
  XNode *x = (XNode *)calloc(1, sizeof(XNode));
  if (!x) return NULL;
  x->name = str_dup(name ? name : "");
  x->parent = parent;
  if (parent) { if (parent->last) parent->last->next = x; else parent->child = x; parent->last = x; }
  return x;
}
static void xn_free(XNode *x)
{
  while (x) {
    XNode *nx = x->next;
    int i;
    xn_free(x->child);
    for (i = 0; i < x->nattr; i++) { free(x->an[i]); free(x->av[i]); }
    free(x->an); free(x->av); free(x->name); free(x->text); free(x);
    x = nx;
  }
}
static XNode *xml_tree(const char *s)
{
  Xml x;
  XNode *root = xn_new("", NULL), *cur = root;
  int t, depth = 0, seen = 0;
  if (!root) return NULL;
  /* a document must start with markup */
  { const char *p = s; while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') p++; if (*p != '<') { xn_free(root); return NULL; } }
  xml_init(&x, s, strlen(s));
  while ((t = xml_next(&x)) != XT_EOF) {
    if (t == XT_START) {
      XNode *nd;
      int i;
      if (depth == 0 && seen) { xn_free(root); xml_free(&x); return NULL; }   /* one root element */
      seen = 1;
      if (!(nd = xn_new(x.name, cur))) break;
      if (x.nattr) {
        nd->an = (char **)malloc(sizeof(char *) * (size_t)x.nattr); nd->av = (char **)malloc(sizeof(char *) * (size_t)x.nattr);
        if (nd->an && nd->av) for (i = 0; i < x.nattr; i++) { nd->an[i] = str_dup(x.an[i]); nd->av[i] = str_dup(x.av[i]); nd->nattr++; }
      }
      cur = nd; depth++;
    } else if (t == XT_END) {
      if (cur->parent) cur = cur->parent;
      depth--;
    } else if (t == XT_TEXT && depth > 0) {
      XNode *nd = xn_new("#text", cur);
      if (nd) nd->text = str_dup(x.text.data ? x.text.data : "");
    }
  }
  xml_free(&x);
  if (!seen || depth != 0) { xn_free(root); return NULL; }
  return root;
}
static void xn_string(const XNode *x, Buf *out)
{
  const XNode *c;
  if (x->text) { buf_adds(out, x->text); return; }
  for (c = x->child; c; c = c->next) xn_string(c, out);
}
typedef struct { XNode **v; int n, cap; } XSet;
static void xs_add(XSet *s, XNode *x)
{
  int i;
  for (i = 0; i < s->n; i++) if (s->v[i] == x) return;
  if (s->n == s->cap) { XNode **nv; int c = s->cap ? s->cap * 2 : 16; if (c > 1000000 || !(nv = (XNode **)realloc(s->v, sizeof(XNode *) * (size_t)c))) return; s->v = nv; s->cap = c; }
  s->v[s->n++] = x;
}
static const char *xn_attr(const XNode *x, const char *name)
{
  int i;
  for (i = 0; i < x->nattr; i++) if (!strcmp(x->an[i], name)) return x->av[i];
  return NULL;
}
/* attribute nodes are made on demand and kept under a holder so they are freed with the tree */
static XNode *attr_node(XNode *holder, XNode *el, const char *name, const char *val)
{
  XNode *x = xn_new(name, holder);
  (void)el;
  if (x) { x->text = str_dup(val); x->isattr = 1; }
  return x;
}
/* does node x match the step test (name, "*", "text()", "node()") */
static int step_match(const XNode *x, const char *test)
{
  if (!strcmp(test, "node()")) return 1;
  if (!strcmp(test, "text()")) return x->text != NULL && !x->isattr;
  if (x->text) return 0;
  if (!strcmp(test, "*")) return 1;
  return !strcmp(x->name, test);
}
static void descend(XNode *x, const char *test, XSet *out, int self)
{
  XNode *c;
  if (self && step_match(x, test)) xs_add(out, x);
  for (c = x->child; c; c = c->next) descend(c, test, out, 1);
}
static int xpath_eval(XNode *root, XNode *holder, const char *path, XSet *res)
{
  XSet cur, nxt;
  const char *p = path;
  int first = 1;
  memset(&cur, 0, sizeof cur);
  xs_add(&cur, root);
  if (*p != '/') { if (root->child) { cur.n = 0; xs_add(&cur, root); } }
  while (*p) {
    int deep = 0, i;
    char test[128], pred[256];
    size_t k = 0;
    if (*p == '/') { p++; if (*p == '/') { deep = 1; p++; } }
    else if (!first) { free(cur.v); return E_VALUE; }
    first = 0;
    while (*p && *p != '/' && *p != '[' && k < sizeof test - 1) test[k++] = *p++;
    test[k] = 0;
    if (!k) { free(cur.v); return E_VALUE; }
    memset(&nxt, 0, sizeof nxt);
    for (i = 0; i < cur.n; i++) {
      XNode *x = cur.v[i], *c;
      if (!strcmp(test, ".")) { if (deep) descend(x, "node()", &nxt, 1); else xs_add(&nxt, x); continue; }
      if (!strcmp(test, "..")) { if (x->parent) xs_add(&nxt, x->parent); continue; }
      if (test[0] == '@') {
        XSet scope;
        int j, m;
        memset(&scope, 0, sizeof scope);
        if (deep) descend(x, "*", &scope, 1); else xs_add(&scope, x);
        for (j = 0; j < scope.n; j++) for (m = 0; m < scope.v[j]->nattr; m++)
          if (!strcmp(test + 1, "*") || !strcmp(scope.v[j]->an[m], test + 1)) xs_add(&nxt, attr_node(holder, scope.v[j], scope.v[j]->an[m], scope.v[j]->av[m]));
        free(scope.v);
        continue;
      }
      if (deep) { for (c = x->child; c; c = c->next) descend(c, test, &nxt, 1); }
      else for (c = x->child; c; c = c->next) if (step_match(c, test)) xs_add(&nxt, c);
    }
    free(cur.v);
    cur = nxt;
    /* predicates, each filtering the step's nodes (positions counted per parent) */
    while (*p == '[') {
      const char *q = ++p;
      int depthb = 0;
      XSet f;
      size_t pl;
      while (*p && (*p != ']' || depthb)) { if (*p == '[') depthb++; else if (*p == ']') depthb--; p++; }
      if (*p != ']') { free(cur.v); return E_VALUE; }
      pl = (size_t)(p - q); p++;
      if (pl >= sizeof pred) { free(cur.v); return E_VALUE; }
      memcpy(pred, q, pl); pred[pl] = 0;
      memset(&f, 0, sizeof f);
      for (i = 0; i < cur.n; i++) {
        XNode *x = cur.v[i];
        int keep = 0;
        if (isdigit((unsigned char)pred[0]) || !strcmp(pred, "last()")) {
          /* the position among the same parent's nodes in this set */
          int pos = 0, cnt = 0, j;
          for (j = 0; j < cur.n; j++) if (cur.v[j]->parent == x->parent) { cnt++; if (j <= i) pos++; }
          keep = isdigit((unsigned char)pred[0]) ? pos == atoi(pred) : pos == cnt;
        } else {
          char lhs[128], *eq = strchr(pred, '='), val[256];
          size_t ll = eq ? (size_t)(eq - pred) : strlen(pred);
          if (ll >= sizeof lhs) ll = sizeof lhs - 1;
          memcpy(lhs, pred, ll); lhs[ll] = 0;
          while (ll && lhs[ll - 1] == ' ') lhs[--ll] = 0;
          val[0] = 0;
          if (eq) {
            const char *v = eq + 1;
            size_t vl;
            while (*v == ' ') v++;
            if (*v == '\'' || *v == '"') { char qc = *v++; const char *ve = strchr(v, qc); vl = ve ? (size_t)(ve - v) : strlen(v); }
            else { vl = strlen(v); while (vl && v[vl - 1] == ' ') vl--; }
            if (vl >= sizeof val) vl = sizeof val - 1;
            memcpy(val, v, vl); val[vl] = 0;
          }
          if (lhs[0] == '@') { const char *av = xn_attr(x, lhs + 1); keep = av && (!eq || !strcmp(av, val)); }
          else {
            XNode *c;
            for (c = x->child; c && !keep; c = c->next) if (!c->text && !strcmp(c->name, lhs)) {
              Buf b; buf_init(&b); xn_string(c, &b);
              keep = !eq || !strcmp(b.data ? b.data : "", val);
              buf_free(&b);
            }
          }
        }
        if (keep) xs_add(&f, x);
      }
      free(cur.v);
      cur = f;
    }
  }
  *res = cur;
  return 0;
}
static Value f_filterxml(Ctx *cx, Value *a, int n)
{
  char *xml, *path;
  int e, i;
  XNode *root, *holder;
  XSet res;
  Arr *ar;
  (void)n;
  if ((e = arg_str(cx, &a[0], &xml))) return val_err(e);
  if ((e = arg_str(cx, &a[1], &path))) { free(xml); return val_err(e); }
  root = xml_tree(xml);
  free(xml);
  if (!root) { free(path); return val_err(E_VALUE); }
  holder = xn_new("", NULL);
  memset(&res, 0, sizeof res);
  e = holder ? xpath_eval(root, holder, path, &res) : E_NUM;
  free(path);
  if (e || !res.n || !(ar = arr_new(res.n, 1))) { xn_free(root); xn_free(holder); free(res.v); return val_err(e ? e : res.n ? E_NUM : E_VALUE); }
  for (i = 0; i < res.n; i++) {
    Buf b;
    double d;
    int k;
    buf_init(&b);
    xn_string(res.v[i], &b);
    if (b.data && b.data[0] && parse_number(b.data, &d, &k) && !k) ar->v[i] = val_num(d);
    else ar->v[i] = val_str(b.data ? b.data : "");
    buf_free(&b);
  }
  xn_free(root); xn_free(holder); free(res.v);
  if (ar->rows == 1) { Value v = ar->v[0]; ar->v[0] = val_empty(); arr_free(ar); return v; }
  return arr_value(ar);
}

/* EUROCONVERT(number, source, target, [full_precision], [triangulation_precision]): the fixed
   rates of the currencies replaced by the euro, each with its calculation precision */
static const struct { const char *code; double rate; int dec; } euro[] = {
  { "BEF", 40.3399, 0 }, { "LUF", 40.3399, 0 }, { "DEM", 1.95583, 2 }, { "ESP", 166.386, 0 }, { "FRF", 6.55957, 2 },
  { "IEP", 0.787564, 2 }, { "ITL", 1936.27, 0 }, { "NLG", 2.20371, 2 }, { "ATS", 13.7603, 2 }, { "PTE", 200.482, 2 },
  { "FIM", 5.94573, 2 }, { "GRD", 340.75, 2 }, { "SIT", 239.64, 2 }, { "CYP", 0.585274, 2 }, { "MTL", 0.4293, 2 },
  { "SKK", 30.126, 2 }, { "EEK", 15.6466, 2 }, { "LVL", 0.702804, 2 }, { "LTL", 3.4528, 2 }, { "EUR", 1.0, 2 },
};
static int euro_find(Ctx *cx, const Value *v)
{
  char *s;
  size_t i;
  int r = -1;
  if (arg_str(cx, v, &s)) return -1;
  for (i = 0; i < sizeof euro / sizeof euro[0]; i++) if (str_ieq(s, euro[i].code)) { r = (int)i; break; }
  free(s);
  return r;
}
static double round_to(double x, int d) { double m = pow(10.0, d); return (x < 0 ? -1 : 1) * floor(fabs(x) * m + 0.5 + 1e-9) / m; }
static Value f_euroconvert(Ctx *cx, Value *a, int n)
{
  double x, eur, out, tri = 0;
  int e, src, tgt, full = 0;
  if ((e = arg_num(cx, &a[0], &x))) return val_err(e);
  if (a[1].t == V_ERR) return val_err(a[1].e);
  if (a[2].t == V_ERR) return val_err(a[2].e);
  if ((src = euro_find(cx, &a[1])) < 0 || (tgt = euro_find(cx, &a[2])) < 0) return val_err(E_VALUE);
  if (HAS(3) && (e = arg_bool(cx, &a[3], &full))) return val_err(e);
  if (HAS(4)) { if ((e = arg_num(cx, &a[4], &tri))) return val_err(e); tri = floor(tri); if (tri < 3) return val_err(E_VALUE); }
  if (src == tgt) return val_num(x);
  eur = x / euro[src].rate;
  if (tri >= 3 && strcmp(euro[src].code, "EUR") && eur != 0) {
    int digits = (int)(tri > 17 ? 17 : tri), mag = (int)floor(log10(fabs(eur))) + 1;
    eur = round_to(eur, digits - mag);
  }
  out = eur * euro[tgt].rate;
  return num_or_err(full ? out : round_to(out, euro[tgt].dec));
}

/* ------------------------------------------------------------------ the table (name order) */
#define X FF_XLFN
#define V FF_VOLATILE
const FuncDef func_misc_defs[] = {
  { "ARRAYTOTEXT", 1, 2, X, f_arraytotext }, { "ASC", 1, 1, 0, f_asc }, { "BAHTTEXT", 1, 1, X, f_bahttext },
  { "CUBEKPIMEMBER", 3, 4, 0, f_cube }, { "CUBEMEMBER", 2, 3, 0, f_cube }, { "CUBEMEMBERPROPERTY", 3, 3, 0, f_cube },
  { "CUBERANKEDMEMBER", 3, 4, 0, f_cube }, { "CUBESET", 2, 5, 0, f_cube }, { "CUBESETCOUNT", 1, 1, 0, f_cube },
  { "CUBEVALUE", 1, 255, 0, f_cube }, { "DAYS360", 2, 3, 0, f_days360 }, { "DBCS", 1, 1, X, f_dbcs },
  { "ENCODEURL", 1, 1, X, f_encodeurl }, { "EUROCONVERT", 3, 5, 0, f_euroconvert }, { "FILTERXML", 2, 2, X, f_filterxml },
  { "FINDB", 2, 3, 0, f_findb }, { "INFO", 1, 1, V, f_info }, { "ISOWEEKNUM", 1, 1, X, f_isoweeknum },
  { "JIS", 1, 1, 0, f_dbcs }, { "LEFTB", 1, 2, 0, f_leftb }, { "LENB", 1, 1, 0, f_lenb }, { "MIDB", 3, 3, 0, f_midb },
  { "NETWORKDAYS.INTL", 2, 4, X, f_networkdaysintl }, { "PHONETIC", 1, 1, 0, f_phonetic },
  { "REGEXEXTRACT", 2, 4, X, f_regexextract }, { "REGEXREPLACE", 3, 5, X, f_regexreplace }, { "REGEXTEST", 2, 3, X, f_regextest },
  { "REPLACEB", 4, 4, 0, f_replaceb }, { "RIGHTB", 1, 2, 0, f_rightb }, { "SEARCHB", 2, 3, 0, f_searchb },
  { "TEXTAFTER", 2, 6, X, f_textafter }, { "TEXTBEFORE", 2, 6, X, f_textbefore }, { "TEXTSPLIT", 2, 6, X, f_textsplit },
  { "UNICODE", 1, 1, X, f_unicode }, { "VALUETOTEXT", 1, 2, X, f_valuetotext }, { "WEBSERVICE", 1, 1, X, f_webservice },
  { "WORKDAY.INTL", 2, 4, X, f_workdayintl },
};
#undef X
#undef V
const int func_misc_count = (int)(sizeof func_misc_defs / sizeof func_misc_defs[0]);
