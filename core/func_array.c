/*
 * func_array.c - the dynamic-array functions (SEQUENCE, FILTER, SORT, UNIQUE, TAKE, VSTACK ...),
 * the LAMBDA helpers (MAP, REDUCE, SCAN, BYROW, BYCOL, MAKEARRAY, ISOMITTED; LET and LAMBDA
 * themselves are worked out by calc.c), XMATCH/XLOOKUP/INDEX/ROW/COLUMN as dynamic arrays see
 * them, SUBTOTAL, AGGREGATE, CELL, FORMULATEXT, SHEET, SHEETS, AREAS and TRIMRANGE. Behaviour follows
 * Microsoft's documentation of each function. See func.c for the conventions; this file's table,
 * func_array_defs, is in name order and merged with the others by func_init.
 *
 * Also here: func_lift_spec, which arguments of each function take single values, so that a
 * dynamic-array formula maps the function over an array or range given there (=LEN(A1:A5)).
 */
#include "calc.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>

/* ------------------------------------------------------------------ helpers */
#define EL(a, r, c) (&(a)->v[(long)(r) * (a)->cols + (c)])
static Value arr_take(Arr *a) { Value v = val_empty(); v.t = V_ARR; v.a = a; return v; }
/* an optional number argument: def when missing */
static int opt_num(Ctx *cx, Value *a, int n, int i, double def, double *out)
{
  if (i >= n || IS_MISSING(&a[i])) { *out = def; return 0; }
  return arg_num(cx, &a[i], out);
}
static int opt_bool(Ctx *cx, Value *a, int n, int i, int def, int *out)
{
  if (i >= n || IS_MISSING(&a[i])) { *out = def; return 0; }
  return arg_bool(cx, &a[i], out);
}
/* an argument as an array (copies) */
static Arr *get_arr(Ctx *cx, const Value *v, int *err)
{
  if (v->t == V_ERR) { *err = v->e; return NULL; }
  if (IS_MISSING(v)) { *err = E_VALUE; return NULL; }
  return to_array(cx, v, err);
}
/* a value as a condition (FILTER's include): numbers non-zero, logicals; text #VALUE! */
static int truthy(const Value *v, int *out)
{
  switch (v->t) {
  case V_NUM: *out = v->n != 0; return 0;
  case V_BOOL: *out = v->e; return 0;
  case V_EMPTY: *out = 0; return 0;
  case V_ERR: return v->e;
  default: return E_VALUE;
  }
}
/* sorting order of SORT and friends: numbers, text, logicals, errors, then blanks (blanks last
   either way). Returns -1, 0, 1 */
static int sort_rank(const Value *v) { return v->t == V_NUM ? 0 : v->t == V_STR ? 1 : v->t == V_BOOL ? 2 : v->t == V_ERR ? 3 : 4; }
static int ord_cmp(const Value *a, const Value *b, int order)
{
  int ra = sort_rank(a), rb = sort_rank(b), c;
  if (ra == 4 || rb == 4) return ra == rb ? 0 : ra == 4 ? 1 : -1;
  if (ra != rb) c = ra < rb ? -1 : 1;
  else if (ra == 3) c = a->e < b->e ? -1 : a->e > b->e;
  else c = val_compare(a, b);
  return order < 0 ? -c : c;
}
/* the same value as UNIQUE and the lookups see it: same type, text ignoring case */
static int same_val(const Value *a, const Value *b)
{
  if (a->t != b->t) return 0;
  if (a->t == V_EMPTY) return 1;
  if (a->t == V_ERR) return a->e == b->e;
  return val_compare(a, b) == 0;
}
/* a stable merge sort of n indices with a comparison taking a context */
typedef int (*IdxCmp)(int a, int b, void *ctx);
static void msort(int *v, int *tmp, int n, IdxCmp cmp, void *ctx)
{
  int m, i, j, k;
  if (n < 2) return;
  m = n / 2;
  msort(v, tmp, m, cmp, ctx);
  msort(v + m, tmp, n - m, cmp, ctx);
  for (i = 0, j = m, k = 0; i < m && j < n; ) tmp[k++] = cmp(v[j], v[i], ctx) < 0 ? v[j++] : v[i++];
  while (i < m) tmp[k++] = v[i++];
  while (j < n) tmp[k++] = v[j++];
  memcpy(v, tmp, (size_t)n * sizeof(int));
}
static int *idx_new(int n) { int *p = (int *)malloc((size_t)(n > 0 ? n : 1) * 2 * sizeof(int)), i; if (p) for (i = 0; i < n; i++) p[i] = i; return p; }
/* rows (or, by_col, columns) of src picked by idx into a new array */
static Value pick(Arr *src, const int *idx, int n, int by_col)
{
  Arr *d = by_col ? arr_new(src->rows, n) : arr_new(n, src->cols);
  int i, k;
  if (!d) return val_err(n ? E_NUM : E_CALC);
  for (i = 0; i < n; i++)
    if (by_col) for (k = 0; k < src->rows; k++) *EL(d, k, i) = val_copy(EL(src, k, idx[i]));
    else for (k = 0; k < src->cols; k++) *EL(d, i, k) = val_copy(EL(src, idx[i], k));
  return arr_take(d);
}

/* ------------------------------------------------------------------ building arrays */
static Value f_sequence(Ctx *cx, Value *a, int n)
{
  double rows, cols, start, step;
  int e;
  Value v;
  long i;
  if ((e = arg_num(cx, &a[0], &rows)) || (e = opt_num(cx, a, n, 1, 1, &cols)) || (e = opt_num(cx, a, n, 2, 1, &start)) || (e = opt_num(cx, a, n, 3, 1, &step)))
    return val_err(e);
  rows = floor(rows); cols = floor(cols);
  if (rows < 0 || cols < 0) return val_err(E_VALUE);
  if (rows == 0 || cols == 0) return val_err(E_CALC);
  if (rows * cols > 1000000.0) return val_err(E_NUM);
  v = arr_make((int)rows, (int)cols);
  if (v.t != V_ARR) return v;
  for (i = 0; i < (long)rows * (long)cols; i++) v.a->v[i] = val_num(start + step * i);
  return v;
}
static u32 rng = 0x9E3779B9UL;
static double rnd01(void) { rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; return (rng & 0xFFFFFF) / 16777216.0; }
static Value f_randarray(Ctx *cx, Value *a, int n)
{
  double rows, cols, lo, hi;
  int e, whole;
  long i;
  Value v;
  if ((e = opt_num(cx, a, n, 0, 1, &rows)) || (e = opt_num(cx, a, n, 1, 1, &cols)) || (e = opt_num(cx, a, n, 2, 0, &lo)) ||
      (e = opt_num(cx, a, n, 3, 1, &hi)) || (e = opt_bool(cx, a, n, 4, 0, &whole)))
    return val_err(e);
  rows = floor(rows); cols = floor(cols);
  if (rows < 0 || cols < 0 || lo > hi) return val_err(E_VALUE);
  if (rows == 0 || cols == 0) return val_err(E_CALC);
  if (whole && (lo != floor(lo) || hi != floor(hi))) return val_err(E_VALUE);
  if (rows * cols > 1000000.0) return val_err(E_NUM);
  v = arr_make((int)rows, (int)cols);
  if (v.t != V_ARR) return v;
  if (cx->arrayish) *cx->arrayish = 1;
  for (i = 0; i < (long)rows * (long)cols; i++)
    v.a->v[i] = val_num(whole ? lo + floor(rnd01() * (hi - lo + 1)) : lo + rnd01() * (hi - lo));
  return v;
}
static Value f_makearray(Ctx *cx, Value *a, int n)
{
  double rows, cols;
  int e, r, c;
  Value v;
  (void)n;
  if ((e = arg_num(cx, &a[0], &rows)) || (e = arg_num(cx, &a[1], &cols))) return val_err(e);
  if (!calc_is_lambda(&a[2])) return val_err(E_VALUE);
  rows = floor(rows); cols = floor(cols);
  if (rows < 1 || cols < 1) return val_err(E_VALUE);
  if (rows * cols > 1000000.0) return val_err(E_NUM);
  v = arr_make((int)rows, (int)cols);
  if (v.t != V_ARR) return v;
  for (r = 0; r < rows; r++)
    for (c = 0; c < cols; c++) {
      Value args[2], x;
      args[0] = val_num(r + 1); args[1] = val_num(c + 1);
      x = calc_call(cx, &a[2], args, 2);
      if (x.t == V_REF || x.t == V_ARR) {
        i32 rr, cc;
        arg_shape(&x, &rr, &cc);
        if (rr * cc == 1) { Value s = scalar(cx, &x); val_free(&x); x = s; }
        else { val_free(&x); x = val_err(E_CALC); }
      }
      *EL(v.a, r, c) = x;
    }
  return v;
}

/* ------------------------------------------------------------------ LAMBDA helpers */
/* a lambda's answer for one element: a single value (an array there is #CALC!) */
static Value one_value(Ctx *cx, Value x)
{
  if (x.t == V_REF || x.t == V_ARR) {
    i32 rr, cc;
    arg_shape(&x, &rr, &cc);
    if ((long)rr * cc == 1 && !calc_is_union(&x)) { Value s = scalar(cx, &x); val_free(&x); return s.t == V_EMPTY ? val_num(0) : s; }
    val_free(&x);
    return val_err(E_CALC);
  }
  if (x.t == V_LAMBDA) { val_free(&x); return val_err(E_CALC); }
  return x;
}
/* element r, c of an argument for a lambda: a range gives its cell as a reference */
static Value element(Ctx *cx, const Value *v, i32 r, i32 c)
{
  i32 rows, cols;
  arg_shape(v, &rows, &cols);
  if (rows == 1) r = 0;
  if (cols == 1) c = 0;
  if (r >= rows || c >= cols) return val_err(E_NA);
  if (v->t == V_REF) { Value e = *v; e.r1 = e.r2 = v->r1 + r; e.c1 = e.c2 = v->c1 + c; return e; }
  return val_copy(arg_at(cx, v, r, c));
}
static Value f_map(Ctx *cx, Value *a, int n)
{
  const Value *fn = &a[n - 1];
  i32 rows = 1, cols = 1, r, c;
  int i, k;
  Value v, *args;
  if (!calc_is_lambda(fn)) return fn->t == V_ERR ? val_err(fn->e) : val_err(E_VALUE);
  if (n < 2) return val_err(E_VALUE);
  for (i = 0; i < n - 1; i++) {
    i32 rr, cc;
    arg_shape(&a[i], &rr, &cc);
    if (rr > rows) rows = rr;
    if (cc > cols) cols = cc;
  }
  v = arr_make(rows, cols);
  if (v.t != V_ARR) return v;
  if (!(args = (Value *)malloc((size_t)n * sizeof(Value)))) { val_free(&v); return val_err(E_NUM); }
  for (r = 0; r < rows; r++)
    for (c = 0; c < cols; c++) {
      for (k = 0; k < n - 1; k++) args[k] = element(cx, &a[k], r, c);
      *EL(v.a, r, c) = one_value(cx, calc_call(cx, fn, args, n - 1));
      for (k = 0; k < n - 1; k++) val_free(&args[k]);
    }
  free(args);
  return v;
}
/* REDUCE and SCAN: the accumulator carried through the elements, row by row */
static Value reduce_impl(Ctx *cx, Value *a, int n, int scan)
{
  Value acc, out = val_empty();
  const Value *arr, *fn;
  i32 rows, cols, r, c;
  if (n < 3) return val_err(E_VALUE);
  arr = &a[1]; fn = &a[2];
  if (!calc_is_lambda(fn)) return fn->t == V_ERR ? val_err(fn->e) : val_err(E_VALUE);
  if (arr->t == V_ERR) return val_err(arr->e);
  acc = val_copy(&a[0]);                       /* (missing: ISOMITTED is TRUE for it) */
  arg_shape(arr, &rows, &cols);
  if (scan) { out = arr_make(rows, cols); if (out.t != V_ARR) { val_free(&acc); return out; } }
  for (r = 0; r < rows; r++)
    for (c = 0; c < cols; c++) {
      Value args[2], x;
      args[0] = acc;
      args[1] = element(cx, arr, r, c);
      x = calc_call(cx, fn, args, 2);
      val_free(&args[1]);
      val_free(&acc);
      if (x.t == V_REF) { Value s = (x.r1 == x.r2 && x.c1 == x.c2) ? scalar(cx, &x) : val_err(E_CALC); x = s; }
      acc = x;
      if (scan) *EL(out.a, r, c) = one_value(cx, val_copy(&acc));
    }
  if (scan) { val_free(&acc); return out; }
  if (acc.t == V_EMPTY) { acc = val_num(0); }
  return acc;
}
static Value f_reduce(Ctx *cx, Value *a, int n) { return reduce_impl(cx, a, n, 0); }
static Value f_scan(Ctx *cx, Value *a, int n) { return reduce_impl(cx, a, n, 1); }
/* BYROW/BYCOL: each row (column) given to the lambda as a reference or array */
static Value byrc(Ctx *cx, Value *a, int n, int bycol)
{
  i32 rows, cols, k, cnt;
  Value v;
  int err;
  Arr *src = NULL;
  if (n < 2 || !calc_is_lambda(&a[1])) return n > 1 && a[1].t == V_ERR ? val_err(a[1].e) : val_err(E_VALUE);
  if (a[0].t == V_ERR) return val_err(a[0].e);
  arg_shape(&a[0], &rows, &cols);
  if (a[0].t != V_REF && !(src = to_array(cx, &a[0], &err))) return val_err(err);
  cnt = bycol ? cols : rows;
  v = bycol ? arr_make(1, cols) : arr_make(rows, 1);
  if (v.t != V_ARR) { arr_free(src); return v; }
  for (k = 0; k < cnt; k++) {
    Value line;
    if (a[0].t == V_REF) {
      line = a[0];
      if (bycol) line.c1 = line.c2 = a[0].c1 + k; else line.r1 = line.r2 = a[0].r1 + k;
    } else {
      i32 i, len = bycol ? rows : cols;
      line = bycol ? arr_make(rows, 1) : arr_make(1, cols);
      if (line.t == V_ARR) for (i = 0; i < len; i++) line.a->v[i] = val_copy(bycol ? EL(src, i, k) : EL(src, k, i));
    }
    v.a->v[k] = one_value(cx, calc_call(cx, &a[1], &line, 1));
    val_free(&line);
  }
  arr_free(src);
  return v;
}
static Value f_byrow(Ctx *cx, Value *a, int n) { return byrc(cx, a, n, 0); }
static Value f_bycol(Ctx *cx, Value *a, int n) { return byrc(cx, a, n, 1); }
static Value f_isomitted(Ctx *cx, Value *a, int n) { (void)cx; (void)n; return val_bool(IS_MISSING(&a[0])); }
/* LET and LAMBDA are lazy: calc.c works them out without calling these */
static Value f_lazy(Ctx *cx, Value *a, int n) { (void)cx; (void)a; (void)n; return val_err(E_VALUE); }

/* ------------------------------------------------------------------ FILTER, SORT, SORTBY, UNIQUE */
static Value f_filter(Ctx *cx, Value *a, int n)
{
  int err, *idx, k = 0, by_col, i;
  Arr *src = get_arr(cx, &a[0], &err), *inc;
  Value v;
  if (!src) return val_err(err);
  if (!(inc = get_arr(cx, &a[1], &err))) { arr_free(src); return val_err(err); }
  /* include: a column as tall as the array (rows) or a row as wide (columns) */
  if (inc->cols == 1 && inc->rows == src->rows) by_col = 0;
  else if (inc->rows == 1 && inc->cols == src->cols) by_col = 1;
  else { arr_free(src); arr_free(inc); return val_err(E_VALUE); }
  if (!(idx = idx_new(by_col ? src->cols : src->rows))) { arr_free(src); arr_free(inc); return val_err(E_NUM); }
  for (i = 0; i < (by_col ? src->cols : src->rows); i++) {
    int b, e = truthy(&inc->v[i], &b);
    if (e) { free(idx); arr_free(src); arr_free(inc); return val_err(e); }
    if (b) idx[k++] = i;
  }
  if (!k) v = n > 2 && !IS_MISSING(&a[2]) ? val_copy(&a[2]) : val_err(E_CALC);
  else v = pick(src, idx, k, by_col);
  free(idx); arr_free(src); arr_free(inc);
  return v;
}
typedef struct { Arr *a; int nkeys; int keys[64]; int orders[64]; int by_col; Arr *by[64]; } SortCx;
/* SORT: compare rows (columns) a and b by the key columns (rows) */
static int sort_cmp(int x, int y, void *p)
{
  SortCx *s = (SortCx *)p;
  int k;
  for (k = 0; k < s->nkeys; k++) {
    const Value *va = s->by_col ? EL(s->a, s->keys[k], x) : EL(s->a, x, s->keys[k]);
    const Value *vb = s->by_col ? EL(s->a, s->keys[k], y) : EL(s->a, y, s->keys[k]);
    int c = ord_cmp(va, vb, s->orders[k]);
    if (c) return c;
  }
  return 0;
}
static Value f_sort(Ctx *cx, Value *a, int n)
{
  int err, i, by_col, cnt, *idx;
  SortCx s;
  Arr *src = get_arr(cx, &a[0], &err), *ki = NULL, *ko = NULL;
  Value v;
  if (!src) return val_err(err);
  memset(&s, 0, sizeof s);
  if ((err = opt_bool(cx, a, n, 3, 0, &by_col))) { arr_free(src); return val_err(err); }
  s.a = src; s.by_col = by_col;
  /* sort_index and sort_order: one each, or arrays of them */
  if (n > 1 && !IS_MISSING(&a[1])) { if (!(ki = get_arr(cx, &a[1], &err))) { arr_free(src); return val_err(err); } }
  if (n > 2 && !IS_MISSING(&a[2])) { if (!(ko = get_arr(cx, &a[2], &err))) { arr_free(src); arr_free(ki); return val_err(err); } }
  s.nkeys = ki ? ki->rows * ki->cols : 1;
  if (ko && ko->rows * ko->cols > 1 && ko->rows * ko->cols != s.nkeys) { arr_free(src); arr_free(ki); arr_free(ko); return val_err(E_VALUE); }
  if (s.nkeys > 64) s.nkeys = 64;
  for (i = 0; i < s.nkeys; i++) {
    double k = 1, o = 1;
    int e = 0;
    if (ki) e = to_num(&ki->v[i], &k);
    if (!e && ko) e = to_num(&ko->v[ko->rows * ko->cols > 1 ? i : 0], &o);
    if (e) { arr_free(src); arr_free(ki); arr_free(ko); return val_err(e); }
    k = floor(k);
    if (k < 1 || k > (by_col ? src->rows : src->cols) || (o != 1 && o != -1)) { arr_free(src); arr_free(ki); arr_free(ko); return val_err(E_VALUE); }
    s.keys[i] = (int)k - 1; s.orders[i] = (int)o;
  }
  arr_free(ki); arr_free(ko);
  cnt = by_col ? src->cols : src->rows;
  if (!(idx = idx_new(cnt))) { arr_free(src); return val_err(E_NUM); }
  msort(idx, idx + cnt, cnt, sort_cmp, &s);
  v = pick(src, idx, cnt, by_col);
  free(idx); arr_free(src);
  return v;
}
static int sortby_cmp(int x, int y, void *p)
{
  SortCx *s = (SortCx *)p;
  int k;
  for (k = 0; k < s->nkeys; k++) {
    int c = ord_cmp(&s->by[k]->v[x], &s->by[k]->v[y], s->orders[k]);
    if (c) return c;
  }
  return 0;
}
static Value f_sortby(Ctx *cx, Value *a, int n)
{
  int err, i, cnt = 0, *idx, by_col = -1;
  SortCx s;
  Arr *src = get_arr(cx, &a[0], &err);
  Value v = val_empty();
  if (!src) return val_err(err);
  memset(&s, 0, sizeof s);
  for (i = 1; i < n && s.nkeys < 64; i += 2) {
    Arr *b = get_arr(cx, &a[i], &err);
    double o = 1;
    int bc;
    if (!b) { v = val_err(err); break; }
    s.by[s.nkeys] = b;
    /* a column as tall as the array sorts rows, a row as wide sorts columns */
    if (b->cols == 1 && b->rows == src->rows && !(b->rows == 1 && src->rows == 1 && src->cols > 1)) bc = 0;
    else if (b->rows == 1 && b->cols == src->cols) bc = 1;
    else { v = val_err(E_VALUE); s.nkeys++; break; }
    if (by_col >= 0 && bc != by_col) { v = val_err(E_VALUE); s.nkeys++; break; }
    by_col = bc;
    if (i + 1 < n && (err = opt_num(cx, a, n, i + 1, 1, &o))) { v = val_err(err); s.nkeys++; break; }
    if (o != 1 && o != -1) { v = val_err(E_VALUE); s.nkeys++; break; }
    s.orders[s.nkeys++] = (int)o;
  }
  if (v.t == V_EMPTY) {
    cnt = by_col ? src->cols : src->rows;
    if (!(idx = idx_new(cnt))) v = val_err(E_NUM);
    else {
      msort(idx, idx + cnt, cnt, sortby_cmp, &s);
      v = pick(src, idx, cnt, by_col);
      free(idx);
    }
  }
  for (i = 0; i < s.nkeys; i++) arr_free(s.by[i]);
  arr_free(src);
  return v;
}
/* rows (columns) x and y of a the same, element by element */
static int same_line(Arr *a, int x, int y, int by_col)
{
  int k, len = by_col ? a->rows : a->cols;
  for (k = 0; k < len; k++)
    if (!same_val(by_col ? EL(a, k, x) : EL(a, x, k), by_col ? EL(a, k, y) : EL(a, y, k))) return 0;
  return 1;
}
static Value f_unique(Ctx *cx, Value *a, int n)
{
  int err, by_col, once, cnt, i, j, k = 0, *idx;
  Arr *src = get_arr(cx, &a[0], &err);
  Value v;
  if (!src) return val_err(err);
  if ((err = opt_bool(cx, a, n, 1, 0, &by_col)) || (err = opt_bool(cx, a, n, 2, 0, &once))) { arr_free(src); return val_err(err); }
  cnt = by_col ? src->cols : src->rows;
  if (!(idx = idx_new(cnt))) { arr_free(src); return val_err(E_NUM); }
  for (i = 0; i < cnt; i++) {
    int seen = 0, times = 0;
    for (j = 0; j < i && !seen; j++) if (same_line(src, i, j, by_col)) seen = 1;
    if (seen) continue;
    if (once) { for (j = 0; j < cnt; j++) if (same_line(src, i, j, by_col)) times++; if (times != 1) continue; }
    idx[k++] = i;
  }
  v = k ? pick(src, idx, k, by_col) : val_err(E_CALC);
  free(idx); arr_free(src);
  return v;
}

/* ------------------------------------------------------------------ reshaping */
/* TAKE and DROP: rows/columns from the start (positive) or the end (negative) */
static Value take_drop(Ctx *cx, Value *a, int n, int drop)
{
  int err, r0, r1, c0, c1, r, c;
  double rows, cols;
  Arr *src = get_arr(cx, &a[0], &err), *d;
  if (!src) return val_err(err);
  if (IS_MISSING(&a[1]) && (n < 3 || IS_MISSING(&a[2]))) { arr_free(src); return val_err(E_VALUE); }
  if ((err = opt_num(cx, a, n, 1, drop ? 0 : src->rows, &rows)) || (err = opt_num(cx, a, n, 2, drop ? 0 : src->cols, &cols))) { arr_free(src); return val_err(err); }
  rows = rows < 0 ? -floor(-rows) : floor(rows); cols = cols < 0 ? -floor(-cols) : floor(cols);
  if (!drop) {
    if (rows == 0 || cols == 0) { arr_free(src); return val_err(E_CALC); }
    if (rows > src->rows) rows = src->rows;
    if (rows < -src->rows) rows = -src->rows;
    if (cols > src->cols) cols = src->cols;
    if (cols < -src->cols) cols = -src->cols;
    r0 = rows > 0 ? 0 : src->rows + (int)rows; r1 = rows > 0 ? (int)rows : src->rows;
    c0 = cols > 0 ? 0 : src->cols + (int)cols; c1 = cols > 0 ? (int)cols : src->cols;
  } else {
    if (fabs(rows) >= src->rows || fabs(cols) >= src->cols) { arr_free(src); return val_err(E_CALC); }
    r0 = rows > 0 ? (int)rows : 0; r1 = rows < 0 ? src->rows + (int)rows : src->rows;
    c0 = cols > 0 ? (int)cols : 0; c1 = cols < 0 ? src->cols + (int)cols : src->cols;
  }
  if (!(d = arr_new(r1 - r0, c1 - c0))) { arr_free(src); return val_err(E_CALC); }
  for (r = r0; r < r1; r++) for (c = c0; c < c1; c++) *EL(d, r - r0, c - c0) = val_copy(EL(src, r, c));
  arr_free(src);
  return arr_take(d);
}
static Value f_take(Ctx *cx, Value *a, int n) { return take_drop(cx, a, n, 0); }
static Value f_drop(Ctx *cx, Value *a, int n) { return take_drop(cx, a, n, 1); }
/* CHOOSEROWS and CHOOSECOLS: the numbers (from the end when negative) in any arguments, arrays too */
static Value choose_rc(Ctx *cx, Value *a, int n, int by_col)
{
  int err, i, k = 0, cap = 0, *idx = NULL, len;
  Arr *src = get_arr(cx, &a[0], &err);
  Value v;
  if (!src) return val_err(err);
  len = by_col ? src->cols : src->rows;
  for (i = 1; i < n; i++) {
    Arr *ix = get_arr(cx, &a[i], &err);
    int j;
    if (!ix) { free(idx); arr_free(src); return val_err(err); }
    for (j = 0; j < ix->rows * ix->cols; j++) {
      double d;
      int e = to_num(&ix->v[j], &d);
      if (!e && (d = d < 0 ? -floor(-d) : floor(d), d == 0 || fabs(d) > len)) e = E_VALUE;
      if (e) { arr_free(ix); free(idx); arr_free(src); return val_err(e); }
      if (k == cap) {
        int *ni;
        cap = cap ? cap * 2 : 16;
        if (!(ni = (int *)realloc(idx, (size_t)cap * sizeof(int)))) { arr_free(ix); free(idx); arr_free(src); return val_err(E_NUM); }
        idx = ni;
      }
      idx[k++] = d > 0 ? (int)d - 1 : len + (int)d;
    }
    arr_free(ix);
  }
  v = pick(src, idx, k, by_col);
  free(idx); arr_free(src);
  return v;
}
static Value f_chooserows(Ctx *cx, Value *a, int n) { return choose_rc(cx, a, n, 0); }
static Value f_choosecols(Ctx *cx, Value *a, int n) { return choose_rc(cx, a, n, 1); }
/* VSTACK and HSTACK: the arrays one after another, the short ones padded with #N/A */
static Value stack(Ctx *cx, Value *a, int n, int horiz)
{
  Arr **parts = (Arr **)malloc((size_t)n * sizeof(Arr *)), *d;
  int i, err = E_NUM, rows = 0, cols = 0, at = 0, r, c;
  if (!parts) return val_err(E_NUM);
  for (i = 0; i < n; i++) {
    if (!(parts[i] = IS_MISSING(&a[i]) ? arr_new(1, 1) : to_array(cx, &a[i], &err))) { while (i-- > 0) arr_free(parts[i]); free(parts); return val_err(err); }
    if (horiz) { cols += parts[i]->cols; if (parts[i]->rows > rows) rows = parts[i]->rows; }
    else { rows += parts[i]->rows; if (parts[i]->cols > cols) cols = parts[i]->cols; }
  }
  d = arr_new(rows, cols);
  for (i = 0; i < n; i++) {
    Arr *p = parts[i];
    if (d) for (r = 0; r < (horiz ? rows : p->rows); r++)
      for (c = 0; c < (horiz ? p->cols : cols); c++) {
        Value *dst = horiz ? EL(d, r, at + c) : EL(d, at + r, c);
        *dst = r < p->rows && c < p->cols ? val_copy(EL(p, r, c)) : val_err(E_NA);
      }
    at += horiz ? p->cols : p->rows;
    arr_free(p);
  }
  free(parts);
  return d ? arr_take(d) : val_err(E_NUM);
}
static Value f_vstack(Ctx *cx, Value *a, int n) { return stack(cx, a, n, 0); }
static Value f_hstack(Ctx *cx, Value *a, int n) { return stack(cx, a, n, 1); }
/* TOCOL and TOROW: ignore 1 blanks, 2 errors, 3 both; row by row unless scan_by_column */
static Value to_line(Ctx *cx, Value *a, int n, int row)
{
  int err, bycol, r, c, k = 0;
  double ign;
  Arr *src = get_arr(cx, &a[0], &err), *d;
  if (!src) return val_err(err);
  if ((err = opt_num(cx, a, n, 1, 0, &ign)) || (err = opt_bool(cx, a, n, 2, 0, &bycol))) { arr_free(src); return val_err(err); }
  ign = floor(ign);
  if (ign < 0 || ign > 3) { arr_free(src); return val_err(E_VALUE); }
  if (!(d = row ? arr_new(1, src->rows * src->cols) : arr_new(src->rows * src->cols, 1))) { arr_free(src); return val_err(E_NUM); }
  for (r = 0; r < (bycol ? src->cols : src->rows); r++)
    for (c = 0; c < (bycol ? src->rows : src->cols); c++) {
      Value *x = bycol ? EL(src, c, r) : EL(src, r, c);
      if ((((int)ign & 1) && x->t == V_EMPTY) || (((int)ign & 2) && x->t == V_ERR)) continue;
      d->v[k++] = val_copy(x);
    }
  arr_free(src);
  if (!k) { arr_free(d); return val_err(E_CALC); }
  if (row) d->cols = k; else d->rows = k;
  return arr_take(d);
}
static Value f_tocol(Ctx *cx, Value *a, int n) { return to_line(cx, a, n, 0); }
static Value f_torow(Ctx *cx, Value *a, int n) { return to_line(cx, a, n, 1); }
/* WRAPROWS and WRAPCOLS: a row or column cut into lines of wrap_count, the last padded */
static Value wrap(Ctx *cx, Value *a, int n, int cols_first)
{
  int err, len, lines, i;
  double w;
  Arr *src = get_arr(cx, &a[0], &err), *d;
  if (!src) return val_err(err);
  if (src->rows != 1 && src->cols != 1) { arr_free(src); return val_err(E_VALUE); }
  if ((err = arg_num(cx, &a[1], &w))) { arr_free(src); return val_err(err); }
  w = floor(w);
  if (w < 1) { arr_free(src); return val_err(E_NUM); }
  len = src->rows * src->cols;
  if (w > len) w = len > 0 ? (w > 1000000 ? 1000000 : w) : 1;
  lines = (len + (int)w - 1) / (int)w;
  if (!(d = cols_first ? arr_new((int)w, lines) : arr_new(lines, (int)w))) { arr_free(src); return val_err(E_NUM); }
  for (i = 0; i < lines * (int)w; i++) {
    int line = i / (int)w, k = i % (int)w;
    Value *dst = cols_first ? EL(d, k, line) : EL(d, line, k);
    *dst = i < len ? val_copy(&src->v[i]) : n > 2 && !IS_MISSING(&a[2]) ? scalar(cx, &a[2]) : val_err(E_NA);
  }
  arr_free(src);
  return arr_take(d);
}
static Value f_wraprows(Ctx *cx, Value *a, int n) { return wrap(cx, a, n, 0); }
static Value f_wrapcols(Ctx *cx, Value *a, int n) { return wrap(cx, a, n, 1); }
static Value f_expand(Ctx *cx, Value *a, int n)
{
  int err, r, c;
  double rows, cols;
  Arr *src = get_arr(cx, &a[0], &err), *d;
  if (!src) return val_err(err);
  if ((err = opt_num(cx, a, n, 1, src->rows, &rows)) || (err = opt_num(cx, a, n, 2, src->cols, &cols))) { arr_free(src); return val_err(err); }
  rows = floor(rows); cols = floor(cols);
  if (rows < src->rows || cols < src->cols) { arr_free(src); return val_err(E_VALUE); }
  if (rows * cols > 1000000.0 || !(d = arr_new((int)rows, (int)cols))) { arr_free(src); return val_err(E_NUM); }
  for (r = 0; r < rows; r++)
    for (c = 0; c < cols; c++)
      *EL(d, r, c) = r < src->rows && c < src->cols ? val_copy(EL(src, r, c)) : n > 3 && !IS_MISSING(&a[3]) ? scalar(cx, &a[3]) : val_err(E_NA);
  arr_free(src);
  return arr_take(d);
}

/* ------------------------------------------------------------------ XMATCH, XLOOKUP */
/* the position of x in vec (len elements): match_mode 0 exact, -1 exact or next smaller, 1 exact or
   next larger, 2 wildcards; search_mode 1 first to last, -1 last to first, 2 / -2 sorted (searched as
   1). -1 if none */
static long xsearch(const Value *x, Arr *vec, int mm, int sm)
{
  long len = (long)vec->rows * vec->cols, i, best = -1;
  int wild = mm == 2 && x->t == V_STR;
  for (i = 0; i < len; i++) {
    long k = sm == -1 ? len - 1 - i : i;
    const Value *v = &vec->v[k];
    if (wild) { if (v->t == V_STR && wild_match(x->s, v->s)) return k; continue; }
    if (same_val(v, x) && v->t != V_EMPTY) return k;
    if (mm == 1 || mm == -1) {
      int c;
      if (v->t != x->t || v->t == V_EMPTY || v->t == V_ERR) continue;
      c = val_compare(v, x);
      if ((mm == -1 && c < 0) || (mm == 1 && c > 0)) {
        const Value *b = best >= 0 ? &vec->v[best] : NULL;
        if (!b || (mm == -1 ? val_compare(v, b) > 0 : val_compare(v, b) < 0)) best = k;
      }
    }
  }
  return best;
}
static int x_modes(Ctx *cx, Value *a, int n, int i, int *mm, int *sm)
{
  double m, s;
  int e;
  if ((e = opt_num(cx, a, n, i, 0, &m)) || (e = opt_num(cx, a, n, i + 1, 1, &s))) return e;
  if ((m != 0 && m != -1 && m != 1 && m != 2) || (s != 1 && s != -1 && s != 2 && s != -2)) return E_VALUE;
  *mm = (int)m; *sm = (int)s;
  return 0;
}
static Value f_xmatch(Ctx *cx, Value *a, int n)
{
  int err, mm, sm;
  long pos;
  Value x = scalar(cx, &a[0]);
  Arr *vec;
  if (x.t == V_ERR) return x;
  if ((err = x_modes(cx, a, n, 2, &mm, &sm))) { val_free(&x); return val_err(err); }
  if (!(vec = get_arr(cx, &a[1], &err))) { val_free(&x); return val_err(err); }
  if (vec->rows != 1 && vec->cols != 1) { arr_free(vec); val_free(&x); return val_err(E_VALUE); }
  pos = xsearch(&x, vec, mm, sm);
  arr_free(vec); val_free(&x);
  return pos < 0 ? val_err(E_NA) : val_num((double)pos + 1);
}
static Value f_xlookup(Ctx *cx, Value *a, int n)
{
  int err, mm, sm, vertical;
  long pos;
  i32 lr, lc, rr, rc;
  Value x = scalar(cx, &a[0]);
  Arr *vec, *res;
  if (x.t == V_ERR) return x;
  if ((err = x_modes(cx, a, n, 4, &mm, &sm))) { val_free(&x); return val_err(err); }
  if (a[2].t == V_ERR) { val_free(&x); return val_err(a[2].e); }
  if (!(vec = get_arr(cx, &a[1], &err))) { val_free(&x); return val_err(err); }
  lr = vec->rows; lc = vec->cols;
  if (lr != 1 && lc != 1) { arr_free(vec); val_free(&x); return val_err(E_VALUE); }
  arg_shape(&a[2], &rr, &rc);
  /* a column looks down, a row across; a single cell goes by the result's shape */
  vertical = lc == 1 && (lr > 1 || rc == 1 || rr != 1);
  if ((vertical && rr != lr) || (!vertical && rc != lc)) { arr_free(vec); val_free(&x); return val_err(E_VALUE); }
  pos = xsearch(&x, vec, mm, sm);
  arr_free(vec); val_free(&x);
  if (pos < 0) return n > 3 && !IS_MISSING(&a[3]) ? val_copy(&a[3]) : val_err(E_NA);
  if (a[2].t == V_REF) {
    /* the matching row (column) of the result range, as a reference */
    Value r = a[2];
    if (vertical) r.r1 = r.r2 = a[2].r1 + (i32)pos; else r.c1 = r.c2 = a[2].c1 + (i32)pos;
    return r;
  }
  if (!(res = get_arr(cx, &a[2], &err))) return val_err(err);
  {
    int idx[1];
    Value v;
    idx[0] = (int)pos;
    v = pick(res, idx, 1, !vertical);
    arr_free(res);
    if (v.t == V_ARR && v.a->rows * v.a->cols == 1) { Value s = val_copy(&v.a->v[0]); val_free(&v); return s; }
    return v;
  }
}

/* ------------------------------------------------------------------ INDEX, ROW, COLUMN, AREAS */
static Value f_index(Ctx *cx, Value *a, int n)
{
  double r = 0, c = 0, area = 1;
  int e;
  i32 rows, cols;
  Value src = a[0];
  if (src.t == V_ERR) return val_err(src.e);
  if ((n > 1 && !IS_MISSING(&a[1]) && (e = arg_num(cx, &a[1], &r))) || (n > 2 && !IS_MISSING(&a[2]) && (e = arg_num(cx, &a[2], &c))) ||
      (e = opt_num(cx, a, n, 3, 1, &area)))
    return val_err(e);
  if (calc_is_union(&src)) {
    area = floor(area);
    if (area < 1 || area > src.a->rows * src.a->cols) return val_err(E_REF);
    src = src.a->v[(int)area - 1];
  } else if (floor(area) != 1) return val_err(E_REF);
  arg_shape(&src, &rows, &cols);
  r = floor(r); c = floor(c);
  if (n == 2 && rows == 1 && cols > 1) { c = r; r = 0; }   /* INDEX(row, k) */
  if (r < 0 || c < 0 || r > rows || c > cols) return val_err(E_REF);
  if (src.t == V_REF) {
    Value v = src;
    if (r > 0) v.r1 = v.r2 = src.r1 + (i32)r - 1;
    if (c > 0) v.c1 = v.c2 = src.c1 + (i32)c - 1;
    return v;
  }
  if (r > 0 && c > 0) return val_copy(arg_at(cx, &src, (i32)r - 1, (i32)c - 1));
  if (r == 0 && c == 0) return val_copy(&src);
  {
    /* a whole row or column of an array */
    int err;
    Arr *s2 = to_array(cx, &src, &err);
    Value v;
    int idx[1];
    if (!s2) return val_err(err);
    if (rows == 1 && r == 0 && c > 0) { Value x = val_copy(EL(s2, 0, (int)c - 1)); arr_free(s2); return x; }
    if (cols == 1 && c == 0 && r > 0) { Value x = val_copy(EL(s2, (int)r - 1, 0)); arr_free(s2); return x; }
    idx[0] = r > 0 ? (int)r - 1 : (int)c - 1;
    v = pick(s2, idx, 1, r == 0);
    arr_free(s2);
    return v;
  }
}
/* ROW and COLUMN: of a range, all its rows (columns) in a dynamic-array formula */
static Value rowcol(Ctx *cx, Value *a, int n, int col)
{
  i32 k, cnt;
  Value v;
  if (n == 0 || IS_MISSING(&a[0])) return val_num((double)(col ? cx->col : cx->row) + 1);
  if (a[0].t == V_ERR) return val_err(a[0].e);
  if (a[0].t != V_REF) return val_err(E_VALUE);
  cnt = col ? a[0].c2 - a[0].c1 + 1 : a[0].r2 - a[0].r1 + 1;
  if (!cx->dyn || cnt == 1) return val_num((double)(col ? a[0].c1 : a[0].r1) + 1);
  v = col ? arr_make(1, cnt) : arr_make(cnt, 1);
  if (v.t != V_ARR) return v;
  for (k = 0; k < cnt; k++) v.a->v[k] = val_num((double)(col ? a[0].c1 : a[0].r1) + k + 1);
  return v;
}
static Value f_row(Ctx *cx, Value *a, int n) { return rowcol(cx, a, n, 0); }
static Value f_column(Ctx *cx, Value *a, int n) { return rowcol(cx, a, n, 1); }
static Value f_areas(Ctx *cx, Value *a, int n)
{
  (void)cx; (void)n;
  if (a[0].t == V_ERR) return val_err(a[0].e);
  if (calc_is_union(&a[0])) return val_num(a[0].a->rows * a[0].a->cols);
  if (a[0].t == V_REF) return val_num(1);
  return val_err(E_VALUE);
}

/* ------------------------------------------------------------------ TRIMRANGE */
/* see calc.h. Bounds of the non-blank part of a range: only the cells that exist are looked at */
Value calc_trim(Ctx *cx, const Value *v, int rmode, int cmode)
{
  i32 r0 = -1, r1 = -1, c0 = -1, c1 = -1;
  if (v->t == V_ERR) return val_err(v->e);
  if ((rmode < 0 || rmode > 3) || (cmode < 0 || cmode > 3)) return val_err(E_VALUE);
  if (v->t == V_REF) {
    Sheet *s = v->sheet < cx->b->nsh ? cx->b->sh[v->sheet] : NULL;
    i32 r, last;
    Value out = *v;
    if (!s) return val_err(E_REF);
    if (!rmode && !cmode) return *v;
    last = v->r2 < s->nrows - 1 ? v->r2 : s->nrows - 1;
    for (r = v->r1; r <= last; r++) {
      Row *row = s->rows[r];
      int i;
      if (!row) continue;
      for (i = 0; i < row->n; i++) {
        i32 c = row->c[i].col;
        const Value *cv;
        if (c < v->c1 || c > v->c2) continue;
        cv = ref_cell(cx, v->sheet, r, c);
        if (cv->t == V_EMPTY) continue;
        if (r0 < 0) r0 = r;
        r1 = r;
        if (c0 < 0 || c < c0) c0 = c;
        if (c > c1) c1 = c;
        row = s->rows[r];
        if (!row) break;
      }
    }
    if (r0 < 0) return val_err(E_CALC);
    if (rmode & 1) out.r1 = r0;
    if (rmode & 2) out.r2 = r1;
    if (cmode & 1) out.c1 = c0;
    if (cmode & 2) out.c2 = c1;
    return out;
  }
  if (v->t == V_ARR && !calc_is_union(v)) {
    i32 r, c;
    Arr *d;
    if (!rmode && !cmode) return val_copy(v);
    for (r = 0; r < v->a->rows; r++)
      for (c = 0; c < v->a->cols; c++)
        if (EL(v->a, r, c)->t != V_EMPTY) {
          if (r0 < 0) r0 = r;
          r1 = r;
          if (c0 < 0 || c < c0) c0 = c;
          if (c > c1) c1 = c;
        }
    if (r0 < 0) return val_err(E_CALC);
    if (!(rmode & 1)) r0 = 0;
    if (!(rmode & 2)) r1 = v->a->rows - 1;
    if (!(cmode & 1)) c0 = 0;
    if (!(cmode & 2)) c1 = v->a->cols - 1;
    if (!(d = arr_new(r1 - r0 + 1, c1 - c0 + 1))) return val_err(E_NUM);
    for (r = r0; r <= r1; r++) for (c = c0; c <= c1; c++) *EL(d, r - r0, c - c0) = val_copy(EL(v->a, r, c));
    return arr_take(d);
  }
  return val_err(E_VALUE);
}
static Value f_trimrange(Ctx *cx, Value *a, int n)
{
  double rm, cm;
  int e;
  if ((e = opt_num(cx, a, n, 1, 3, &rm)) || (e = opt_num(cx, a, n, 2, 3, &cm))) return val_err(e);
  if (rm != floor(rm) || cm != floor(cm)) { rm = floor(rm); cm = floor(cm); }
  return calc_trim(cx, &a[0], (int)rm, (int)cm);
}

/* ------------------------------------------------------------------ SUBTOTAL and AGGREGATE */
/* the cell holds a SUBTOTAL or AGGREGATE (left out, so subtotals aren't counted twice) */
static int has_subtotal(Cell *cell)
{
  int i;
  if (!cell || !cell->f) return 0;
  for (i = 0; i < cell->f->n; i++) {
    const Tok *t = &cell->f->t[i];
    if (t->op == T_FUNC && t->a != 0xFFFF && (!strcmp(func_name(t->a), "SUBTOTAL") || !strcmp(func_name(t->a), "AGGREGATE"))) return 1;
  }
  return 0;
}
enum { SK_NESTED = 1, SK_HIDDEN = 2, SK_ERRORS = 4, SK_FILTERED = 8 };
/* the values of a reference or array as an array, the cells skipped left empty */
static int gather_skip(Ctx *cx, const Value *v, int skip, Arr **out)
{
  int err, r, c;
  Arr *d;
  if (v->t == V_ERR) return v->e;
  if (!(d = to_array(cx, v, &err))) return err;
  if (v->t == V_REF) {
    Sheet *s = v->sheet < cx->b->nsh ? cx->b->sh[v->sheet] : NULL;
    for (r = 0; s && r < d->rows; r++) {
      i32 rr = v->r1 + r;
      Row *row = rr < s->nrows ? s->rows[rr] : NULL;
      int hide = row && (((skip & SK_HIDDEN) && (row->hidden || row->filtered)) || ((skip & SK_FILTERED) && row->filtered));
      for (c = 0; c < d->cols; c++)
        if (hide || ((skip & SK_NESTED) && has_subtotal(sheet_cell(s, rr, v->c1 + c)))) val_free(EL(d, r, c));
    }
  }
  if (skip & SK_ERRORS) for (r = 0; r < d->rows * d->cols; r++) if (d->v[r].t == V_ERR) val_free(&d->v[r]);
  *out = d;
  return 0;
}
static Value call_by_name(Ctx *cx, const char *name, Value *args, int argc)
{
  int id = func_lookup(name);
  if (id < 0) return val_err(E_NAME);
  if (argc < func_table[id].minargs || argc > func_table[id].maxargs) return val_err(E_VALUE);
  return func_table[id].fn(cx, args, argc);
}
static const char *const agg_names[20] = { "", "AVERAGE", "COUNT", "COUNTA", "MAX", "MIN", "PRODUCT", "STDEV.S", "STDEV.P", "SUM", "VAR.S",
  "VAR.P", "MEDIAN", "MODE.SNGL", "LARGE", "SMALL", "PERCENTILE.INC", "QUARTILE.INC", "PERCENTILE.EXC", "QUARTILE.EXC" };
/* the function fn (1..13) over the values of refs, skipping as asked */
static Value agg_over(Ctx *cx, int fn, Value *refs, int nrefs, int skip)
{
  Value *vals = (Value *)calloc((size_t)nrefs + 1, sizeof(Value)), v;
  int i, e;
  if (!vals) return val_err(E_NUM);
  for (i = 0; i < nrefs; i++) {
    Arr *d;
    if ((e = gather_skip(cx, &refs[i], skip, &d))) { while (i-- > 0) val_free(&vals[i]); free(vals); return val_err(e); }
    vals[i] = arr_take(d);
  }
  v = call_by_name(cx, agg_names[fn], vals, nrefs);
  for (i = 0; i < nrefs; i++) val_free(&vals[i]);
  free(vals);
  return v;
}
static Value f_subtotal(Ctx *cx, Value *a, int n)
{
  double fn;
  int e, i, skip = SK_NESTED | SK_FILTERED;
  static const char *const names[12] = { "", "AVERAGE", "COUNT", "COUNTA", "MAX", "MIN", "PRODUCT", "STDEV", "STDEVP", "SUM", "VAR", "VARP" };
  Value *vals, v;
  if ((e = arg_num(cx, &a[0], &fn))) return val_err(e);
  fn = floor(fn);
  if (fn > 100) { fn -= 100; skip |= SK_HIDDEN; }
  if (fn < 1 || fn > 11) return val_err(E_VALUE);
  for (i = 1; i < n; i++) if (a[i].t != V_REF && a[i].t != V_ERR && !calc_is_union(&a[i])) return val_err(E_VALUE);
  if (!(vals = (Value *)calloc((size_t)n, sizeof(Value)))) return val_err(E_NUM);
  for (i = 1; i < n; i++) {
    Arr *d;
    Value *src = &a[i];
    if (calc_is_union(src)) {
      /* each area on its own */
      int k, cnt = src->a->rows * src->a->cols, tot = 0;
      Arr *all;
      Arr *parts[64];
      if (cnt > 64) cnt = 64;
      for (k = 0; k < cnt; k++) {
        if ((e = gather_skip(cx, &src->a->v[k], skip, &parts[k]))) { while (k-- > 0) arr_free(parts[k]); goto fail; }
        tot += parts[k]->rows * parts[k]->cols;
      }
      all = arr_new(1, tot > 0 ? tot : 1);
      for (tot = 0, k = 0; k < cnt; k++) {
        int m;
        for (m = 0; all && m < parts[k]->rows * parts[k]->cols; m++) { all->v[tot++] = parts[k]->v[m]; parts[k]->v[m] = val_empty(); }
        arr_free(parts[k]);
      }
      if (!all) { e = E_NUM; goto fail; }
      vals[i - 1] = arr_take(all);
      continue;
    }
    if ((e = gather_skip(cx, src, skip, &d))) goto fail;
    vals[i - 1] = arr_take(d);
  }
  v = call_by_name(cx, names[(int)fn], vals, n - 1);
  for (i = 0; i < n - 1; i++) val_free(&vals[i]);
  free(vals);
  return v;
fail:
  for (i = 0; i < n - 1; i++) val_free(&vals[i]);
  free(vals);
  return val_err(e);
}
static Value f_aggregate(Ctx *cx, Value *a, int n)
{
  double fn, opt;
  int e, skip;
  if ((e = arg_num(cx, &a[0], &fn)) || (e = opt_num(cx, a, n, 1, 0, &opt))) return val_err(e);
  fn = floor(fn); opt = floor(opt);
  if (fn < 1 || fn > 19 || opt < 0 || opt > 7) return val_err(E_VALUE);
  if (n < 3) return val_err(E_VALUE);
  /* options: 0-3 leave out nested SUBTOTAL/AGGREGATE; 1, 3, 5, 7 hidden rows; 2, 3, 6, 7 errors */
  skip = (opt <= 3 ? SK_NESTED : 0) | (((int)opt & 1) ? SK_HIDDEN : 0) | ((opt == 2 || opt == 3 || opt == 6 || opt == 7) ? SK_ERRORS : 0);
  if (fn <= 13) return agg_over(cx, (int)fn, &a[2], n - 2, skip);
  {
    /* LARGE, SMALL, PERCENTILE and QUARTILE: (array, k) */
    Arr *d;
    Value args[2], v;
    if (n < 4) return val_err(E_VALUE);
    if ((e = gather_skip(cx, &a[2], skip, &d))) return val_err(e);
    args[0] = arr_take(d);
    args[1] = a[3];
    if (fn == 19) {
      /* QUARTILE.EXC as PERCENTILE.EXC of a quarter */
      double q;
      if ((e = arg_num(cx, &a[3], &q))) { val_free(&args[0]); return val_err(e); }
      q = floor(q);
      if (q < 1 || q > 3) { val_free(&args[0]); return val_err(E_NUM); }
      args[1] = val_num(q / 4);
      v = call_by_name(cx, "PERCENTILE.EXC", args, 2);
    } else v = call_by_name(cx, agg_names[(int)fn], args, 2);
    val_free(&args[0]);
    return v;
  }
}

/* ------------------------------------------------------------------ information */
/* Excel's CELL("format") code of a number format */
static void fmt_letter(const char *code, char *out)
{
  char sec[128];
  const char *semi = strchr(code, ';');
  size_t n = semi ? (size_t)(semi - code) : strlen(code);
  int dec = 0, comma = 0, i, inq = 0;
  const char *p, *dot;
  if (n >= sizeof sec) n = sizeof sec - 1;
  memcpy(sec, code, n); sec[n] = 0;
  if (!strcmp(code, "General") || !code[0]) { strcpy(out, "G"); goto tail; }
  if (fmt_is_date(code)) {
    int h = strchr(sec, 'h') || strchr(sec, 'H'), s = strstr(sec, ":ss") != NULL, ap = strstr(sec, "AM/PM") || strstr(sec, "am/pm");
    if (h) strcpy(out, ap ? (s ? "D6" : "D7") : (s ? "D8" : "D9"));
    else if (strstr(sec, "mmm") && strstr(sec, "yy") && strchr(sec, 'd')) strcpy(out, "D1");
    else if (strstr(sec, "mmm") && strchr(sec, 'd')) strcpy(out, "D2");
    else if (strstr(sec, "mmm") || (strchr(sec, 'm') && strstr(sec, "yy") && !strchr(sec, 'd'))) strcpy(out, "D3");
    else if (strstr(sec, "yy")) strcpy(out, "D4");
    else strcpy(out, "D5");
    goto tail;
  }
  dot = NULL;
  for (p = sec; *p; p++) {
    if (*p == '"') inq = !inq;
    if (inq) continue;
    if (*p == '.' && !dot) dot = p;
    if (*p == ',') comma = 1;
  }
  if (dot) for (p = dot + 1; *p == '0' || *p == '#'; p++) dec++;
  if (strchr(sec, 'E') || strchr(sec, 'e')) sprintf(out, "S%d", dec);
  else if (strchr(sec, '%')) sprintf(out, "P%d", dec);
  else if (strchr(sec, '$') || strstr(sec, "[$")) sprintf(out, "C%d", dec);
  else if (strchr(sec, '?') || strchr(sec, '/')) strcpy(out, "G");
  else if (comma) sprintf(out, ",%d", dec);
  else sprintf(out, "F%d", dec);
tail:
  /* negatives in colour: "-"; brackets for positives: "()" */
  if (semi) {
    const char *neg = semi + 1;
    if (neg[0] == '[') strcat(out, "-");
  }
  for (i = 0; sec[i]; i++) if (sec[i] == '(') { strcat(out, "()"); break; }
}
static Value f_cell(Ctx *cx, Value *a, int n)
{
  char *info;
  int e, sheet = cx->sheet;
  i32 r = cx->row, c = cx->col;
  Sheet *s;
  Cell *cell;
  const Value *v;
  Value out;
  Style st;
  if ((e = arg_str(cx, &a[0], &info))) return val_err(e);
  if (n > 1 && !IS_MISSING(&a[1])) {
    if (a[1].t == V_ERR) { free(info); return val_err(a[1].e); }
    if (a[1].t != V_REF) { free(info); return val_err(E_VALUE); }
    sheet = a[1].sheet; r = a[1].r1; c = a[1].c1;
  }
  if (sheet >= cx->b->nsh) { free(info); return val_err(E_REF); }
  s = cx->b->sh[sheet];
  cell = sheet_cell(s, r, c);
  v = ref_cell(cx, sheet, r, c);
  st = cx->b->styles[sheet_cell_style(s, r, c)];
  if (str_ieq(info, "address")) {
    char t[32], sh[80];
    Buf b;
    ref_text(r, c, 1, 1, t);
    buf_init(&b);
    if (sheet != cx->sheet) { snprintf(sh, sizeof sh, "%s!", s->name); buf_adds(&b, sh); }
    buf_adds(&b, t);
    out = val_str(b.data ? b.data : "");
    buf_free(&b);
  } else if (str_ieq(info, "col")) out = val_num(c + 1);
  else if (str_ieq(info, "row")) out = val_num(r + 1);
  else if (str_ieq(info, "contents")) out = v->t == V_EMPTY ? val_num(0) : val_copy(v);
  else if (str_ieq(info, "type")) out = val_str(v->t == V_EMPTY ? "b" : (v->t == V_STR && !(cell && cell->f)) ? "l" : "v");
  else if (str_ieq(info, "width")) {
    u16 px = sheet_col_width(s, c);
    out = val_num(px <= 5 ? 0 : floor((px - 5) / 7.0 + 0.5));
  } else if (str_ieq(info, "filename")) out = val_str("");
  else if (str_ieq(info, "format") || str_ieq(info, "color") || str_ieq(info, "parentheses")) {
    char f[16];
    fmt_letter(book_fmt_code(cx->b, st.numfmt), f);
    if (str_ieq(info, "format")) out = val_str(f);
    else if (str_ieq(info, "color")) out = val_num(strchr(f, '-') != NULL);
    else out = val_num(strstr(f, "()") != NULL);
  } else if (str_ieq(info, "prefix")) {
    const char *p = "";
    if (v->t == V_STR && !(cell && cell->f))
      p = st.halign == HA_RIGHT ? "\"" : st.halign == HA_CENTER ? "^" : st.halign == HA_FILL ? "\\" : st.halign == HA_LEFT ? "'" : "'";
    out = val_str(p);
  } else if (str_ieq(info, "protect")) out = val_num(st.locked ? 1 : 0);
  else out = val_err(E_VALUE);
  free(info);
  return out;
}
static Value f_formulatext(Ctx *cx, Value *a, int n)
{
  Cell *cell;
  Buf b;
  Value v;
  (void)n;
  if (a[0].t == V_ERR) return val_err(a[0].e);
  if (a[0].t != V_REF || a[0].sheet >= cx->b->nsh) return val_err(E_NA);
  cell = sheet_cell(cx->b->sh[a[0].sheet], a[0].r1, a[0].c1);
  if (!cell || !cell->f) return val_err(E_NA);
  buf_init(&b);
  if (cell->f->cse) buf_addc(&b, '{');
  buf_addc(&b, '=');
  formula_text(cx->b, a[0].sheet, cell->f, &b);
  if (cell->f->cse) buf_addc(&b, '}');
  v = val_str(b.data ? b.data : "");
  buf_free(&b);
  return v;
}
static Value f_sheet(Ctx *cx, Value *a, int n)
{
  if (n == 0 || IS_MISSING(&a[0])) return val_num(cx->sheet + 1);
  if (a[0].t == V_REF) return val_num(a[0].sheet + 1);
  if (calc_is_union(&a[0])) return val_num(a[0].a->v[0].sheet + 1);
  if (a[0].t == V_ERR) return val_err(a[0].e);
  {
    char *s;
    int e = arg_str(cx, &a[0], &s), k;
    if (e) return val_err(e);
    k = book_find_sheet(cx->b, s);
    free(s);
    return k < 0 ? val_err(E_NA) : val_num(k + 1);
  }
}
static Value f_sheets(Ctx *cx, Value *a, int n)
{
  if (n == 0 || IS_MISSING(&a[0])) return val_num(cx->b->nsh);
  if (a[0].t == V_REF || calc_is_union(&a[0])) return val_num(1);
  if (a[0].t == V_ERR) return val_err(a[0].e);
  return val_err(E_VALUE);
}

/* ------------------------------------------------------------------ GROUPBY and PIVOTBY */
typedef struct { Arr *f; } KeyCx;
static int key_eq(Arr *f, int x, int y)
{
  int c;
  for (c = 0; c < f->cols; c++) if (!same_val(EL(f, x, c), EL(f, y, c))) return 0;
  return 1;
}
typedef struct { Arr *f; int col; int order; } KSort;
static int key_cmp(int x, int y, void *p)
{
  KSort *k = (KSort *)p;
  int c, r;
  if (k->col >= 0) { r = ord_cmp(EL(k->f, x, k->col), EL(k->f, y, k->col), k->order); if (r) return r; }
  for (c = 0; c < k->f->cols; c++) { r = ord_cmp(EL(k->f, x, c), EL(k->f, y, c), 1); if (r) return r; }
  return 0;
}
/* distinct key rows of f among rows[] (n of them), in sorted order: out gets row numbers of f */
static int distinct_keys(Arr *f, const int *rows, int n, int sort, int *out)
{
  int i, j, k = 0;
  KSort ks;
  int *tmp;
  for (i = 0; i < n; i++) {
    for (j = 0; j < k; j++) if (key_eq(f, out[j], rows[i])) break;
    if (j == k) out[k++] = rows[i];
  }
  ks.f = f; ks.col = sort && abs(sort) <= f->cols ? abs(sort) - 1 : -1; ks.order = sort < 0 ? -1 : 1;
  if ((tmp = (int *)malloc((size_t)(k ? k : 1) * sizeof(int)))) { msort(out, tmp, k, key_cmp, &ks); free(tmp); }
  return k;
}
/* the aggregate of the values of vals' column vc in the rows listed */
static Value agg_rows(Ctx *cx, const Value *fn, Arr *vals, int vc, const int *rows, int n)
{
  Value col, v;
  int i;
  if (!n) return val_empty();
  col = arr_make(n, 1);
  if (col.t != V_ARR) return col;
  for (i = 0; i < n; i++) col.a->v[i] = val_copy(EL(vals, rows[i], vc));
  v = calc_call(cx, fn, &col, 1);
  val_free(&col);
  if (v.t == V_REF || v.t == V_ARR) {
    i32 r, c;
    arg_shape(&v, &r, &c);
    if ((long)r * c == 1) { Value s = scalar(cx, &v); val_free(&v); v = s; }
    else { val_free(&v); v = val_err(E_CALC); }
  }
  return v;
}
/* field_headers: 0 none, 1 there but not shown, 2 none but made up, 3 there and shown; missing:
   there and shown when the values' first row is text and the next isn't */
static int headers_mode(Ctx *cx, Value *a, int n, int i, Arr *vals, int *mode)
{
  double m;
  int e, c, first = 1, second = 0;
  if (i < n && !IS_MISSING(&a[i])) {
    if ((e = arg_num(cx, &a[i], &m))) return e;
    if (m < 0 || m > 3) return E_VALUE;
    *mode = (int)m;
    return 0;
  }
  for (c = 0; c < vals->cols; c++) {
    if (EL(vals, 0, c)->t != V_STR) first = 0;
    if (vals->rows > 1 && EL(vals, 1, c)->t != V_STR) second = 1;
  }
  *mode = first && second ? 3 : 0;
  return 0;
}
typedef struct { Value *v; int rows, cols, cap; } Grid;
static Value *grid_at(Grid *g, int r, int c)
{
  if (r >= g->cap) {
    int cap = g->cap ? g->cap * 2 : 16;
    Value *nv;
    while (cap <= r) cap *= 2;
    if (!(nv = (Value *)realloc(g->v, (size_t)cap * g->cols * sizeof(Value)))) return NULL;
    memset(nv + (long)g->cap * g->cols, 0, (size_t)(cap - g->cap) * g->cols * sizeof(Value));
    g->v = nv; g->cap = cap;
  }
  if (r >= g->rows) g->rows = r + 1;
  return &g->v[(long)r * g->cols + c];
}
static void grid_put(Grid *g, int r, int c, Value v) { Value *p = grid_at(g, r, c); if (p) { val_free(p); *p = v; } else val_free(&v); }
static Value grid_take(Grid *g)
{
  Value out;
  int i;
  if (!g->rows) { free(g->v); return val_err(E_CALC); }
  out = arr_make(g->rows, g->cols);
  if (out.t == V_ARR) for (i = 0; i < g->rows * g->cols; i++) out.a->v[i] = g->v[i];
  else for (i = 0; i < g->rows * g->cols; i++) val_free(&g->v[i]);
  free(g->v);
  return out;
}
/* rows of the data that pass: past the header row, and the filter */
static int data_rows(Ctx *cx, Value *a, int n, int fi, int nrows, int hdr, int *rows, int *err)
{
  int k = 0, r;
  Arr *flt = NULL;
  *err = 0;
  if (fi < n && !IS_MISSING(&a[fi]) && !(flt = get_arr(cx, &a[fi], err))) return 0;
  for (r = hdr; r < nrows; r++) {
    if (flt) {
      int b, e = r < flt->rows * flt->cols ? truthy(&flt->v[r], &b) : (b = 0, 0);
      if (e) { *err = e; arr_free(flt); return 0; }
      if (!b) continue;
    }
    rows[k++] = r;
  }
  arr_free(flt);
  return k;
}
static Value f_groupby(Ctx *cx, Value *a, int n)
{
  int err, mode, hdr, nr, nk, i, k, line = 0, *rows, *keys, *grp, ng;
  double total, sort;
  Arr *rf = get_arr(cx, &a[0], &err), *vals;
  Grid g;
  if (!rf) return val_err(err);
  if (!(vals = get_arr(cx, &a[1], &err))) { arr_free(rf); return val_err(err); }
  if (!calc_is_lambda(&a[2]) || rf->rows != vals->rows) { arr_free(rf); arr_free(vals); return val_err(E_VALUE); }
  if ((err = headers_mode(cx, a, n, 3, vals, &mode)) || (err = opt_num(cx, a, n, 4, 1, &total)) || (err = opt_num(cx, a, n, 5, 1, &sort))) {
    arr_free(rf); arr_free(vals); return val_err(err);
  }
  hdr = mode == 1 || mode == 3;
  rows = (int *)malloc((size_t)vals->rows * 3 * sizeof(int) + sizeof(int));
  if (!rows) { arr_free(rf); arr_free(vals); return val_err(E_NUM); }
  keys = rows + vals->rows; grp = keys + vals->rows;
  nr = data_rows(cx, a, n, 6, vals->rows, hdr, rows, &err);
  if (err) { free(rows); arr_free(rf); arr_free(vals); return val_err(err); }
  nk = distinct_keys(rf, rows, nr, (int)sort, keys);
  memset(&g, 0, sizeof g);
  g.cols = rf->cols + vals->cols;
  if (mode >= 2) {
    for (k = 0; k < rf->cols; k++) {
      char t[32];
      if (hdr) grid_put(&g, 0, k, val_copy(EL(rf, 0, k)));
      else { sprintf(t, "Field %d", k + 1); grid_put(&g, 0, k, val_str(t)); }
    }
    for (k = 0; k < vals->cols; k++) {
      char t[32];
      if (hdr) grid_put(&g, 0, rf->cols + k, val_copy(EL(vals, 0, k)));
      else { sprintf(t, "Value %d", k + 1); grid_put(&g, 0, rf->cols + k, val_str(t)); }
    }
    line = 1;
  }
  if (total < 0) {
    grid_put(&g, line, 0, val_str("Total"));
    for (k = 0; k < vals->cols; k++) grid_put(&g, line, rf->cols + k, agg_rows(cx, &a[2], vals, k, rows, nr));
    line++;
  }
  {
    /* the groups, then perhaps sorted by a value column */
    int start = line, *order = (int *)malloc((size_t)(nk ? nk : 1) * sizeof(int));
    for (i = 0; i < nk; i++) {
      ng = 0;
      for (k = 0; k < nr; k++) if (key_eq(rf, rows[k], keys[i])) grp[ng++] = rows[k];
      for (k = 0; k < rf->cols; k++) grid_put(&g, line, k, val_copy(EL(rf, keys[i], k)));
      for (k = 0; k < vals->cols; k++) grid_put(&g, line, rf->cols + k, agg_rows(cx, &a[2], vals, k, grp, ng));
      line++;
    }
    if (order && fabs(sort) > rf->cols && fabs(sort) <= g.cols && nk > 1) {
      int vc = (int)fabs(sort) - 1, o = sort < 0 ? -1 : 1, x, y;
      /* a simple stable insertion sort of the group lines by that column */
      for (x = start + 1; x < line; x++)
        for (y = x; y > start && ord_cmp(grid_at(&g, y, vc), grid_at(&g, y - 1, vc), o) < 0; y--) {
          int c;
          for (c = 0; c < g.cols; c++) { Value t = *grid_at(&g, y, c); *grid_at(&g, y, c) = *grid_at(&g, y - 1, c); *grid_at(&g, y - 1, c) = t; }
        }
    }
    free(order);
  }
  if (total > 0) {
    grid_put(&g, line, 0, val_str("Total"));
    for (k = 0; k < vals->cols; k++) grid_put(&g, line, rf->cols + k, agg_rows(cx, &a[2], vals, k, rows, nr));
    line++;
  }
  free(rows); arr_free(rf); arr_free(vals);
  return grid_take(&g);
}
static Value f_pivotby(Ctx *cx, Value *a, int n)
{
  int err, mode, hdr, nr, nrk, nck, i, j, k, line = 0, *rows, *rk, *ck, *sel, ns, lead, ncols, valrow;
  double rtot, rsort, ctot, csort;
  Arr *rf = get_arr(cx, &a[0], &err), *cf, *vals;
  Grid g;
  if (!rf) return val_err(err);
  if (!(cf = get_arr(cx, &a[1], &err))) { arr_free(rf); return val_err(err); }
  if (!(vals = get_arr(cx, &a[2], &err))) { arr_free(rf); arr_free(cf); return val_err(err); }
  if (!calc_is_lambda(&a[3]) || rf->rows != vals->rows || cf->rows != vals->rows) { arr_free(rf); arr_free(cf); arr_free(vals); return val_err(E_VALUE); }
  if ((err = headers_mode(cx, a, n, 4, vals, &mode)) || (err = opt_num(cx, a, n, 5, 1, &rtot)) || (err = opt_num(cx, a, n, 6, 1, &rsort)) ||
      (err = opt_num(cx, a, n, 7, 1, &ctot)) || (err = opt_num(cx, a, n, 8, 1, &csort))) {
    arr_free(rf); arr_free(cf); arr_free(vals); return val_err(err);
  }
  hdr = mode == 1 || mode == 3;
  rows = (int *)malloc((size_t)vals->rows * 4 * sizeof(int) + sizeof(int));
  if (!rows) { arr_free(rf); arr_free(cf); arr_free(vals); return val_err(E_NUM); }
  rk = rows + vals->rows; ck = rk + vals->rows; sel = ck + vals->rows;
  nr = data_rows(cx, a, n, 9, vals->rows, hdr, rows, &err);
  if (err) { free(rows); arr_free(rf); arr_free(cf); arr_free(vals); return val_err(err); }
  nrk = distinct_keys(rf, rows, nr, (int)rsort, rk);
  nck = distinct_keys(cf, rows, nr, (int)csort, ck);
  lead = rf->cols;
  ncols = nck + (ctot != 0);
  valrow = vals->cols > 1 || mode >= 2;
  memset(&g, 0, sizeof g);
  g.cols = lead + ncols * vals->cols;
  if ((long)g.cols * (nrk + 3 + cf->cols) > 1000000L) { free(rows); arr_free(rf); arr_free(cf); arr_free(vals); return val_err(E_NUM); }
  /* column headings: one row per column field (Total at the end or start), the value names under them */
  for (k = 0; k < cf->cols; k++) {
    for (i = 0; i < lead; i++) grid_put(&g, line, i, val_empty());
    for (j = 0; j < ncols; j++) {
      int cj = ctot < 0 ? j - 1 : j, v;
      for (v = 0; v < vals->cols; v++)
        grid_put(&g, line, lead + j * vals->cols + v, (cj < 0 || cj >= nck) ? (k == 0 ? val_str("Total") : val_empty()) : val_copy(EL(cf, ck[cj], k)));
    }
    line++;
  }
  if (valrow) {
    for (i = 0; i < lead; i++) {
      char t[32];
      if (mode >= 2 && hdr) grid_put(&g, line, i, val_copy(EL(rf, 0, i)));
      else if (mode >= 2) { sprintf(t, "Field %d", i + 1); grid_put(&g, line, i, val_str(t)); }
      else grid_put(&g, line, i, val_empty());
    }
    for (j = 0; j < ncols; j++) {
      int v;
      for (v = 0; v < vals->cols; v++) {
        char t[32];
        if (hdr) grid_put(&g, line, lead + j * vals->cols + v, val_copy(EL(vals, 0, v)));
        else { sprintf(t, "Value %d", v + 1); grid_put(&g, line, lead + j * vals->cols + v, val_str(t)); }
      }
    }
    line++;
  }
  /* body: each row key (Total first or last), each column key */
  for (i = (rtot < 0 ? -1 : 0); i < nrk + (rtot > 0 ? 1 : 0); i++) {
    int ri = i < nrk ? i : -1;
    if (ri < 0) { grid_put(&g, line, 0, val_str("Total")); for (k = 1; k < lead; k++) grid_put(&g, line, k, val_empty()); }
    else for (k = 0; k < lead; k++) grid_put(&g, line, k, val_copy(EL(rf, rk[ri], k)));
    for (j = 0; j < ncols; j++) {
      int cj = ctot < 0 ? j - 1 : j, v;
      ns = 0;
      for (k = 0; k < nr; k++)
        if ((ri < 0 || key_eq(rf, rows[k], rk[ri])) && (cj < 0 || cj >= nck || key_eq(cf, rows[k], ck[cj]))) sel[ns++] = rows[k];
      for (v = 0; v < vals->cols; v++) grid_put(&g, line, lead + j * vals->cols + v, agg_rows(cx, &a[3], vals, v, sel, ns));
    }
    line++;
  }
  free(rows); arr_free(rf); arr_free(cf); arr_free(vals);
  return grid_take(&g);
}

/* ------------------------------------------------------------------ the table */
#define X FF_XLFN
#define V FF_VOLATILE
const FuncDef func_array_defs[] = {
  { "AGGREGATE", 3, 255, X, f_aggregate }, { "AREAS", 1, 1, 0, f_areas }, { "BYCOL", 2, 2, X, f_bycol }, { "BYROW", 2, 2, X, f_byrow },
  { "CELL", 1, 2, V, f_cell }, { "CHOOSECOLS", 2, 255, X, f_choosecols }, { "CHOOSEROWS", 2, 255, X, f_chooserows },
  { "COLUMN", 0, 1, 0, f_column }, { "DROP", 2, 3, X, f_drop }, { "EXPAND", 2, 4, X, f_expand }, { "FILTER", 2, 3, X, f_filter },
  { "FORMULATEXT", 1, 1, X, f_formulatext }, { "GROUPBY", 3, 8, X, f_groupby }, { "HSTACK", 1, 254, X, f_hstack },
  { "INDEX", 2, 4, 0, f_index }, { "ISOMITTED", 1, 1, X, f_isomitted }, { "LAMBDA", 0, 254, X, f_lazy }, { "LET", 3, 253, X, f_lazy },
  { "MAKEARRAY", 3, 3, X, f_makearray }, { "MAP", 2, 254, X, f_map }, { "PIVOTBY", 4, 11, X, f_pivotby },
  { "RANDARRAY", 0, 5, X | V, f_randarray }, { "REDUCE", 3, 3, X, f_reduce }, { "ROW", 0, 1, 0, f_row }, { "SCAN", 3, 3, X, f_scan },
  { "SEQUENCE", 1, 4, X, f_sequence }, { "SHEET", 0, 1, X, f_sheet }, { "SHEETS", 0, 1, X, f_sheets }, { "SORT", 1, 4, X, f_sort },
  { "SORTBY", 2, 254, X, f_sortby }, { "SUBTOTAL", 2, 255, 0, f_subtotal }, { "TAKE", 2, 3, X, f_take }, { "TOCOL", 1, 3, X, f_tocol },
  { "TOROW", 1, 3, X, f_torow }, { "TRIMRANGE", 1, 3, X, f_trimrange }, { "UNIQUE", 1, 3, X, f_unique }, { "VSTACK", 1, 254, X, f_vstack },
  { "WRAPCOLS", 2, 3, X, f_wrapcols }, { "WRAPROWS", 2, 3, X, f_wraprows }, { "XLOOKUP", 3, 6, X, f_xlookup }, { "XMATCH", 2, 4, X, f_xmatch },
};
#undef X
#undef V
const int func_array_count = (int)(sizeof func_array_defs / sizeof func_array_defs[0]);

/* which arguments take single values (s) and which anything (a); the last letter repeats. In a
   dynamic-array formula a range or array given where a single value goes maps the function over it */
const char *const func_lift_spec[] = {
  /* maths and trigonometry */
  "ABS s", "ACOS s", "ACOSH s", "ACOT s", "ACOTH s", "ARABIC s", "ASIN s", "ASINH s", "ATAN s", "ATAN2 s", "ATANH s", "BASE s",
  "CEILING s", "CEILING.MATH s", "CEILING.PRECISE s", "COMBIN s", "COMBINA s", "COS s", "COSH s", "COT s", "COTH s", "CSC s", "CSCH s",
  "DECIMAL s", "DEGREES s", "EVEN s", "EXP s", "FACT s", "FACTDOUBLE s", "FLOOR s", "FLOOR.MATH s", "FLOOR.PRECISE s", "INT s",
  "ISO.CEILING s", "LN s", "LOG s", "LOG10 s", "MOD s", "MROUND s", "ODD s", "PERMUT s", "PERMUTATIONA s", "POWER s", "QUOTIENT s",
  "RADIANS s", "ROMAN s", "ROUND s", "ROUNDDOWN s", "ROUNDUP s", "SEC s", "SECH s", "SIGN s", "SIN s", "SINH s", "SQRT s", "SQRTPI s",
  "TAN s", "TANH s", "TRUNC s", "RANDBETWEEN s", "GAMMA s", "GAMMALN s", "GAMMALN.PRECISE s", "GAUSS s", "PHI s", "FISHER s", "FISHERINV s",
  /* text */
  "CHAR s", "CLEAN s", "CODE s", "CONCATENATE s", "DOLLAR s", "EXACT s", "FIND s", "FINDB s", "FIXED s", "LEFT s", "LEFTB s", "LEN s",
  "LENB s", "LOWER s", "MID s", "MIDB s", "NUMBERVALUE s", "PROPER s", "REPLACE s", "REPLACEB s", "REPT s", "RIGHT s", "RIGHTB s",
  "SEARCH s", "SEARCHB s", "SUBSTITUTE s", "T s", "TEXT s", "TRIM s", "UNICHAR s", "UNICODE s", "UPPER s", "VALUE s", "ASC s", "DBCS s",
  "JIS s", "BAHTTEXT s", "USDOLLAR s", "TEXTBEFORE sas", "TEXTAFTER sas", "REGEXTEST s", "REGEXEXTRACT s", "REGEXREPLACE s", "ENCODEURL s",
  "HYPERLINK s",
  /* dates */
  "DATE s", "DATEDIF s", "DATEVALUE s", "DAY s", "DAYS s", "DAYS360 s", "EDATE s", "EOMONTH s", "HOUR s", "ISOWEEKNUM s", "MINUTE s",
  "MONTH s", "SECOND s", "TIME s", "TIMEVALUE s", "WEEKDAY s", "WEEKNUM s", "YEAR s", "YEARFRAC s", "NETWORKDAYS ssa", "WORKDAY ssa",
  "NETWORKDAYS.INTL sssa", "WORKDAY.INTL sssa",
  /* logic and information */
  "IF s", "IFS s", "IFERROR s", "IFNA s", "CHOOSE s", "SWITCH s", "NOT s", "ISBLANK s", "ISERR s", "ISERROR s", "ISEVEN s", "ISFORMULA s",
  "ISLOGICAL s", "ISNA s", "ISNONTEXT s", "ISNUMBER s", "ISODD s", "ISREF a", "ISTEXT s", "ERROR.TYPE s", "N s", "INDIRECT s",
  "ADDRESS s", "FORMULATEXT s", "SHEET s", "CELL s",
  /* lookups: the value looked for */
  "VLOOKUP saas", "HLOOKUP saas", "MATCH saa", "XMATCH saa", "XLOOKUP saaass", "LOOKUP saa", "INDEX asss",
  "COUNTIF as", "SUMIF asa", "AVERAGEIF asa", "LARGE as", "SMALL as", "PERCENTILE as", "PERCENTILE.INC as", "PERCENTILE.EXC as",
  "QUARTILE as", "QUARTILE.INC as", "QUARTILE.EXC as", "RANK sas", "RANK.EQ sas", "RANK.AVG sas", "PERCENTRANK.INC as", "PERCENTRANK.EXC as",
  /* financial */
  "FV s", "PV s", "PMT s", "NPER s", "RATE s", "IPMT s", "PPMT s", "SLN s", "SYD s", "DDB s", "DB s", "VDB s", "EFFECT s", "NOMINAL s",
  "ISPMT s", "PDURATION s", "RRI s", "CUMIPMT s", "CUMPRINC s", "DOLLARDE s", "DOLLARFR s", "DISC s", "INTRATE s", "RECEIVED s",
  "PRICE s", "PRICEDISC s", "PRICEMAT s", "YIELD s", "YIELDDISC s", "YIELDMAT s", "TBILLEQ s", "TBILLPRICE s", "TBILLYIELD s",
  "ACCRINT s", "ACCRINTM s", "COUPDAYBS s", "COUPDAYS s", "COUPDAYSNC s", "COUPNCD s", "COUPNUM s", "COUPPCD s", "DURATION s", "MDURATION s",
  /* statistical distributions */
  "BETA.DIST s", "BETA.INV s", "BINOM.DIST s", "BINOM.INV s", "CHISQ.DIST s", "CHISQ.DIST.RT s", "CHISQ.INV s", "CHISQ.INV.RT s",
  "CONFIDENCE.NORM s", "CONFIDENCE.T s", "EXPON.DIST s", "F.DIST s", "F.DIST.RT s", "F.INV s", "F.INV.RT s", "GAMMA.DIST s", "GAMMA.INV s",
  "HYPGEOM.DIST s", "LOGNORM.DIST s", "LOGNORM.INV s", "NEGBINOM.DIST s", "NORM.DIST s", "NORM.INV s", "NORM.S.DIST s", "NORM.S.INV s",
  "POISSON.DIST s", "STANDARDIZE s", "T.DIST s", "T.DIST.2T s", "T.DIST.RT s", "T.INV s", "T.INV.2T s", "WEIBULL.DIST s",
  "BETADIST s", "BETAINV s", "BINOMDIST s", "CHIDIST s", "CHIINV s", "CONFIDENCE s", "CRITBINOM s", "EXPONDIST s", "FDIST s", "FINV s",
  "GAMMADIST s", "GAMMAINV s", "HYPGEOMDIST s", "LOGINV s", "LOGNORMDIST s", "NEGBINOMDIST s", "NORMDIST s", "NORMINV s", "NORMSDIST s",
  "NORMSINV s", "POISSON s", "TDIST s", "TINV s", "WEIBULL s",
  /* engineering */
  "BESSELI s", "BESSELJ s", "BESSELK s", "BESSELY s", "BIN2DEC s", "BIN2HEX s", "BIN2OCT s", "BITAND s", "BITLSHIFT s", "BITOR s",
  "BITRSHIFT s", "BITXOR s", "COMPLEX s", "CONVERT s", "DEC2BIN s", "DEC2HEX s", "DEC2OCT s", "DELTA s", "ERF s", "ERF.PRECISE s", "ERFC s",
  "ERFC.PRECISE s", "GESTEP s", "HEX2BIN s", "HEX2DEC s", "HEX2OCT s", "IMABS s", "IMAGINARY s", "IMARGUMENT s", "IMCONJUGATE s", "IMCOS s",
  "IMCOSH s", "IMCOT s", "IMCSC s", "IMCSCH s", "IMDIV s", "IMEXP s", "IMLN s", "IMLOG10 s", "IMLOG2 s", "IMPOWER s", "IMREAL s", "IMSEC s",
  "IMSECH s", "IMSIN s", "IMSINH s", "IMSQRT s", "IMSUB s", "IMTAN s", "OCT2BIN s", "OCT2DEC s", "OCT2HEX s",
  NULL
};
