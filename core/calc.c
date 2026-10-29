/*
 * calc.c - the evaluator: postfix tokens run on a stack of values, cells computed on demand with a
 * circularity check, and the recalculation of a whole book.
 *
 * Recalculation is "on demand within a pass": book_recalc moves the book to a new generation and
 * computes every formula cell; a formula reading another formula cell computes that one first if it
 * is still from the old generation. Long chains are cut at MAXCALL nested cells and finished by
 * further passes, so the C stack stays small on the Amiga.
 */
#include "calc.h"
#include "pagesetup.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>
#include <ctype.h>

#define MAXCALL 120               /* cells computed inside one another before cutting the chain */
#define MAXPASSES 64

static int g_cut;                 /* times a chain was cut in this pass */
static int g_circular;            /* a circular reference was met */
static const Value g_empty;       /* all zero: V_EMPTY */
static const Value g_zero = { V_NUM, 0, 0, 0.0, NULL, 0, 0, 0, 0, NULL };

static Value eval_f(Ctx *cx, const Formula *f);
static void compute_cell(Book *b, int sheet, i32 r, i32 c, Cell *cell, int depth);

/* ------------------------------------------------------------------ reading cells */
/* the value of cell r, c on sheet, computing it if stale */
static const Value *read_cell(Book *b, int sheet, i32 r, i32 c, int depth)
{
  Cell *cell;
  if (sheet < 0 || sheet >= b->nsh || !(cell = sheet_cell(b->sh[sheet], r, c))) return &g_empty;
  if (cell->f && cell->calcgen != b->calcgen) {
    if (cell->calcstate) { g_circular = 1; return b->lx && b->lx->iterate ? &cell->v : &g_zero; }   /* Excel shows 0 for a circular reference (iterating: the last value) */
    compute_cell(b, sheet, r, c, cell, depth + 1);
    cell = sheet_cell(b->sh[sheet], r, c);
    if (!cell) return &g_empty;
  }
  return &cell->v;
}
const Value *ref_cell(Ctx *cx, int sheet, i32 r, i32 c) { return read_cell(cx->b, sheet, r, c, cx->depth); }
const Value *cell_value(Book *b, int sheet, i32 r, i32 c) { return read_cell(b, sheet, r, c, 0); }

/* compute one formula cell and store its result */
static void compute_cell(Book *b, int sheet, i32 r, i32 c, Cell *cell, int depth)
{
  Ctx cx;
  Value v;
  int cuts = g_cut;
  if (depth > MAXCALL) { g_cut++; return; }
  cell->calcstate = 1;
  cx.b = b; cx.sheet = sheet; cx.row = r; cx.col = c; cx.depth = depth;
  v = eval_f(&cx, cell->f);
  /* the final value of a cell is never a reference; a range in a cell formula meets the cell's row
     or column; an array shows its first element (spilling is for later) */
  if (v.t == V_REF || v.t == V_ARR) {
    Value s = scalar(&cx, &v);
    val_free(&v);
    v = s;
  }
  if (v.t == V_EMPTY) v = val_num(0);                 /* =A1 of an empty cell shows 0 */
  cell = sheet_cell(b->sh[sheet], r, c);
  if (!cell) { val_free(&v); return; }
  val_free(&cell->v);
  cell->v = v;
  cell->calcstate = 0;
  if (g_cut == cuts) cell->calcgen = b->calcgen;      /* complete unless a chain below it was cut */
}
/* iterative calculation (File > Options): circular formulas worked out again and again, each from
   the others' last values, until no number changes by more than the maximum change or the
   maximum number of iterations is reached */
static void calc_iterate(Book *b)
{
  static int busy;
  int it, s;
  if (busy) return;
  busy = 1;
  for (it = 1; it < b->lx->maxiter; it++) {
    double change = 0;
    i32 r;
    b->calcgen++;
    if (!b->calcgen) b->calcgen = 1;
    for (s = 0; s < b->nsh; s++) {
      Sheet *sh = b->sh[s];
      for (r = 0; r < sh->nrows; r++) {
        int i;
        if (!sh->rows[r]) continue;
        for (i = 0; i < sh->rows[r]->n; i++) {
          Cell *cell = &sh->rows[r]->c[i];
          double old = cell->v.t == V_NUM ? cell->v.n : 0;
          if (!cell->f || cell->calcgen == b->calcgen) continue;
          cell->calcstate = 0;
          compute_cell(b, s, r, cell->col, cell, 0);
          if (!sh->rows[r]) break;
          cell = &sh->rows[r]->c[i];
          if (cell->v.t == V_NUM && fabs(cell->v.n - old) > change) change = fabs(cell->v.n - old);
        }
      }
    }
    if (change <= b->lx->maxchange) break;
  }
  busy = 0;
}
void book_recalc(Book *b)
{
  int pass, s;
  i32 r;
  b->calcgen++;
  if (!b->calcgen) b->calcgen = 1;
  g_circular = 0;
  for (pass = 0; pass < MAXPASSES; pass++) {
    long stale = 0;
    g_cut = 0;
    for (s = 0; s < b->nsh; s++) {
      Sheet *sh = b->sh[s];
      for (r = 0; r < sh->nrows; r++) {
        int i;
        if (!sh->rows[r]) continue;
        for (i = 0; i < sh->rows[r]->n; i++) {
          Cell *cell = &sh->rows[r]->c[i];
          if (cell->f && cell->calcgen != b->calcgen) {
            cell->calcstate = 0;
            compute_cell(b, s, r, cell->col, cell, 0);
            if (!sh->rows[r]) break;
            cell = &sh->rows[r]->c[i];
            if (cell->calcgen != b->calcgen) stale++;
          }
        }
      }
    }
    if (!stale) break;
  }
  if (g_circular && b->lx && b->lx->iterate) calc_iterate(b);
}
int book_circular(void) { return g_circular; }

/* ------------------------------------------------------------------ conversions */
int to_num(const Value *v, double *out)
{
  switch (v->t) {
  case V_NUM: *out = v->n; return 0;
  case V_BOOL: *out = v->e; return 0;
  case V_EMPTY: *out = 0; return 0;
  case V_ERR: return v->e;
  case V_STR: {
    int isdate;
    if (!v->s[0]) return E_VALUE;
    if (parse_number(v->s, out, &isdate)) return 0;
    return E_VALUE; }
  default: return E_VALUE;
  }
}
int to_bool(const Value *v, int *out)
{
  switch (v->t) {
  case V_BOOL: *out = v->e; return 0;
  case V_NUM: *out = v->n != 0; return 0;
  case V_EMPTY: *out = 0; return 0;
  case V_ERR: return v->e;
  case V_STR:
    if (str_ieq(v->s, "TRUE")) { *out = 1; return 0; }
    if (str_ieq(v->s, "FALSE")) { *out = 0; return 0; }
    return E_VALUE;
  default: return E_VALUE;
  }
}
/* a number as text the way & and TEXT-less conversions write it: up to 15 significant digits */
static void num_text15(double n, char *out)
{
  char *e;
  if (n == 0) { strcpy(out, "0"); return; }
  if (fabs(n) < 1e15 && n == floor(n)) { sprintf(out, "%.0f", n); return; }
  sprintf(out, "%.15G", n);
  e = strchr(out, 'E');
  if (e) {
    /* 1.5E+20 style */
    char mant[40], *q;
    int ex = atoi(e + 1);
    memcpy(mant, out, (size_t)(e - out)); mant[e - out] = 0;
    if (strchr(mant, '.')) { q = mant + strlen(mant) - 1; while (*q == '0') *q-- = 0; if (*q == '.') *q = 0; }
    sprintf(out, "%sE%c%02d", mant, ex < 0 ? '-' : '+', ex < 0 ? -ex : ex);
  } else if (strchr(out, '.')) {
    char *q = out + strlen(out) - 1;
    while (*q == '0') *q-- = 0;
    if (*q == '.') *q = 0;
  }
}
void to_text(Book *b, const Value *v, Buf *out)
{
  char tmp[64];
  (void)b;
  switch (v->t) {
  case V_NUM: num_text15(v->n, tmp); buf_adds(out, tmp); break;
  case V_STR: buf_adds(out, v->s); break;
  case V_BOOL: buf_adds(out, v->e ? "TRUE" : "FALSE"); break;
  case V_ERR: buf_adds(out, err_name(v->e)); break;
  default: break;
  }
}

/* ------------------------------------------------------------------ scalars, arrays, shapes */
Value scalar(Ctx *cx, const Value *v)
{
  if (v->t == V_REF) {
    i32 r, c;
    if (v->r1 == v->r2 && v->c1 == v->c2) { r = v->r1; c = v->c1; }
    else if (v->c1 == v->c2 && cx->row >= v->r1 && cx->row <= v->r2 && cx->sheet == v->sheet) { r = cx->row; c = v->c1; }
    else if (v->r1 == v->r2 && cx->col >= v->c1 && cx->col <= v->c2 && cx->sheet == v->sheet) { r = v->r1; c = cx->col; }
    else return val_err(E_VALUE);
    return val_copy(ref_cell(cx, v->sheet, r, c));
  }
  if (v->t == V_ARR) return v->a && v->a->rows > 0 && v->a->cols > 0 ? val_copy(&v->a->v[0]) : val_err(E_VALUE);
  if (v->t == V_EMPTY) return val_empty();
  return val_copy(v);
}
int arg_num(Ctx *cx, const Value *v, double *out)
{
  Value s;
  int e;
  if (IS_MISSING(v)) { *out = 0; return 0; }
  s = scalar(cx, v);
  e = to_num(&s, out);
  val_free(&s);
  return e;
}
int arg_int(Ctx *cx, const Value *v, long *out)
{
  double d;
  int e = arg_num(cx, v, &d);
  if (e) return e;
  if (d > 2147483647.0 || d < -2147483648.0) return E_NUM;
  *out = (long)d;
  return 0;
}
int arg_bool(Ctx *cx, const Value *v, int *out)
{
  Value s;
  int e;
  if (IS_MISSING(v)) { *out = 0; return 0; }
  s = scalar(cx, v);
  e = to_bool(&s, out);
  val_free(&s);
  return e;
}
int arg_str(Ctx *cx, const Value *v, char **out)
{
  Value s;
  Buf b;
  if (IS_MISSING(v)) { *out = str_dup(""); return *out ? 0 : E_NUM; }
  s = scalar(cx, v);
  if (s.t == V_ERR) { int e = s.e; val_free(&s); return e; }
  buf_init(&b);
  to_text(cx->b, &s, &b);
  val_free(&s);
  *out = buf_take(&b);
  if (!*out) *out = str_dup("");
  return *out ? 0 : E_NUM;
}
void arg_shape(const Value *v, i32 *rows, i32 *cols)
{
  if (v->t == V_REF) { *rows = v->r2 - v->r1 + 1; *cols = v->c2 - v->c1 + 1; }
  else if (v->t == V_ARR && v->a) { *rows = v->a->rows; *cols = v->a->cols; }
  else { *rows = 1; *cols = 1; }
}
const Value *arg_at(Ctx *cx, const Value *v, i32 r, i32 c)
{
  if (v->t == V_REF) return ref_cell(cx, v->sheet, v->r1 + r, v->c1 + c);
  if (v->t == V_ARR && v->a) {
    if (r < v->a->rows && c < v->a->cols) return &v->a->v[r * v->a->cols + c];
    return &g_empty;
  }
  return v;
}
/* the used part of a reference: whole columns stop at the sheet's last row */
static void clip_ref(Ctx *cx, const Value *v, i32 *r2, i32 *c2)
{
  Sheet *s = v->sheet < cx->b->nsh ? cx->b->sh[v->sheet] : NULL;
  *r2 = v->r2; *c2 = v->c2;
  if (!s) return;
  if (*r2 > s->nrows - 1) *r2 = s->nrows - 1;
  {
    i32 lr = sheet_last_row(s), lc;
    if (*r2 > lr) *r2 = lr;
    if (v->c2 - v->c1 > 256) { lc = sheet_last_col(s); if (*c2 > lc) *c2 = lc; }
  }
}
Arr *to_array(Ctx *cx, const Value *v, int *err)
{
  Arr *a;
  i32 r, c, rows, cols;
  *err = 0;
  if (v->t == V_ARR && v->a) {
    Value cp = val_copy(v);
    if (cp.t != V_ARR) { *err = E_NUM; return NULL; }
    return cp.a;
  }
  if (v->t != V_REF) {
    if (!(a = arr_new(1, 1))) { *err = E_NUM; return NULL; }
    a->v[0] = v->t == V_EMPTY ? val_empty() : val_copy(v);
    return a;
  }
  arg_shape(v, &rows, &cols);
  if ((long)rows * cols > 1000000L) {
    /* whole columns: only as far as the data goes */
    i32 r2, c2;
    clip_ref(cx, v, &r2, &c2);
    rows = r2 - v->r1 + 1; cols = c2 - v->c1 + 1;
    if (rows < 1) rows = 1;
    if (cols < 1) cols = 1;
  }
  if (!(a = arr_new(rows, cols))) { *err = E_NUM; return NULL; }
  for (r = 0; r < rows; r++)
    for (c = 0; c < cols; c++) a->v[r * cols + c] = val_copy(ref_cell(cx, v->sheet, v->r1 + r, v->c1 + c));
  return a;
}
int each_value(Ctx *cx, const Value *v, EachFn fn, void *arg)
{
  if (v->t == V_REF) {
    Sheet *s = v->sheet < cx->b->nsh ? cx->b->sh[v->sheet] : NULL;
    i32 r, r2;
    if (!s) return 0;
    r2 = v->r2 < s->nrows - 1 ? v->r2 : s->nrows - 1;
    /* visit only the cells that exist, in order */
    for (r = v->r1; r <= r2; r++) {
      Row *row = s->rows[r];
      int i, n;
      if (!row) continue;
      n = row->n;
      for (i = 0; i < n; i++) {
        i32 c = row->c[i].col;
        const Value *cv;
        if (c < v->c1) continue;
        if (c > v->c2) break;
        cv = ref_cell(cx, v->sheet, r, c);
        if (fn(cx, cv, 0, arg)) return 1;
        row = s->rows[r];
        if (!row) break;
        n = row->n;
      }
    }
    return 0;
  }
  if (v->t == V_ARR && v->a) {
    int i;
    for (i = 0; i < v->a->rows * v->a->cols; i++) if (fn(cx, &v->a->v[i], 0, arg)) return 1;
    return 0;
  }
  if (IS_MISSING(v)) return 0;
  return fn(cx, v, 1, arg);
}

/* ------------------------------------------------------------------ comparing */
static int type_rank(const Value *v) { return v->t == V_NUM || v->t == V_EMPTY ? 0 : v->t == V_STR ? 1 : v->t == V_BOOL ? 2 : 3; }
/* case-insensitive comparison of UTF-8 text (ASCII and Latin-1 letters folded) */
static int text_cmp(const char *a, const char *b)
{
  for (;;) {
    int ua, ub, ca, cb;
    u32 x, y;
    if (!*a || !*b) return *a ? 1 : *b ? -1 : 0;
    x = utf8_get(a, strlen(a), &ua); y = utf8_get(b, strlen(b), &ub);
    ca = (int)(x < 128 ? (u32)tolower((int)x) : (x >= 0xC0 && x <= 0xDE && x != 0xD7) ? x + 32 : x);
    cb = (int)(y < 128 ? (u32)tolower((int)y) : (y >= 0xC0 && y <= 0xDE && y != 0xD7) ? y + 32 : y);
    if (ca != cb) return ca < cb ? -1 : 1;
    a += ua; b += ub;
  }
}
int val_compare(const Value *a, const Value *b)
{
  Value ea = *a, eb = *b;
  int ra, rb;
  /* empty takes the other side's type */
  if (a->t == V_EMPTY) { if (b->t == V_STR) { ea.t = V_STR; ea.s = (char *)""; } else if (b->t == V_BOOL) { ea.t = V_BOOL; ea.e = 0; } else { ea.t = V_NUM; ea.n = 0; } }
  if (b->t == V_EMPTY) { if (a->t == V_STR) { eb.t = V_STR; eb.s = (char *)""; } else if (a->t == V_BOOL) { eb.t = V_BOOL; eb.e = 0; } else { eb.t = V_NUM; eb.n = 0; } }
  ra = type_rank(&ea); rb = type_rank(&eb);
  if (ra != rb) return ra < rb ? -1 : 1;
  switch (ea.t) {
  case V_NUM: return ea.n < eb.n ? -1 : ea.n > eb.n ? 1 : 0;
  case V_STR: return text_cmp(ea.s, eb.s);
  case V_BOOL: return ea.e < eb.e ? -1 : ea.e > eb.e ? 1 : 0;
  default: return 0;
  }
}

/* ------------------------------------------------------------------ criteria */
int wild_match(const char *pat, const char *s)
{
  /* iterative glob with backtracking on the last * */
  const char *star = NULL, *ss = NULL;
  while (*s) {
    if (*pat == '~' && (pat[1] == '*' || pat[1] == '?' || pat[1] == '~')) {
      if (tolower((u8)pat[1]) == tolower((u8)*s)) { pat += 2; s++; continue; }
    } else if (*pat == '?') {
      int u;
      utf8_get(s, strlen(s), &u);
      pat++; s += u;
      continue;
    } else if (*pat == '*') { star = ++pat; ss = s; continue; }
    else if (*pat && tolower((u8)*pat) == tolower((u8)*s)) { pat++; s++; continue; }
    if (star) { pat = star; s = ++ss; continue; }
    return 0;
  }
  while (*pat == '*') pat++;
  return !*pat;
}
enum { C_EQ, C_NE, C_LT, C_GT, C_LE, C_GE };
int crit_make(Ctx *cx, const Value *arg, Crit *c)
{
  Value s = scalar(cx, arg);
  memset(c, 0, sizeof *c);
  c->op = C_EQ;
  if (s.t == V_STR) {
    const char *p = s.s;
    double d;
    int isdate;
    if (p[0] == '<' && p[1] == '>') { c->op = C_NE; p += 2; }
    else if (p[0] == '<' && p[1] == '=') { c->op = C_LE; p += 2; }
    else if (p[0] == '>' && p[1] == '=') { c->op = C_GE; p += 2; }
    else if (p[0] == '<') { c->op = C_LT; p++; }
    else if (p[0] == '>') { c->op = C_GT; p++; }
    else if (p[0] == '=') { c->op = C_EQ; p++; }
    if (*p && parse_number(p, &d, &isdate)) { c->isnum = 1; c->n = d; c->v = val_num(d); }
    else if (str_ieq(p, "TRUE") || str_ieq(p, "FALSE")) c->v = val_bool(str_ieq(p, "TRUE"));
    else if (*p == '#' && err_parse(p, NULL)) c->v = val_err(err_parse(p, NULL));
    else { c->v = val_str(p); c->wild = (c->op == C_EQ || c->op == C_NE) && strpbrk(p, "*?~") != NULL; }
    val_free(&s);
  } else if (s.t == V_EMPTY) { c->v = val_str(""); }
  else {
    c->v = s;
    if (s.t == V_NUM) { c->isnum = 1; c->n = s.n; }
  }
  return 0;
}
void crit_free(Crit *c) { val_free(&c->v); }
int crit_match(const Crit *c, const Value *v)
{
  int cmp;
  if (c->v.t == V_STR && !c->v.s[0]) {
    /* "" or "=" matches empty cells (and empty text); "<>" matches anything not empty */
    int empty = v->t == V_EMPTY || (v->t == V_STR && !v->s[0]);
    return c->op == C_NE ? !empty : c->op == C_EQ ? empty : 0;
  }
  if (c->isnum) {
    double d;
    if (v->t == V_NUM) d = v->n;
    else if (v->t == V_STR && c->op == C_EQ) { int k; if (!parse_number(v->s, &d, &k)) return 0; }
    else return c->op == C_NE;
    switch (c->op) {
    case C_EQ: return d == c->n; case C_NE: return d != c->n; case C_LT: return d < c->n;
    case C_GT: return d > c->n; case C_LE: return d <= c->n; default: return d >= c->n;
    }
  }
  if (c->v.t == V_STR) {
    if (v->t != V_STR) return c->op == C_NE;
    if (c->wild) { int m = wild_match(c->v.s, v->s); return c->op == C_EQ ? m : !m; }
    cmp = text_cmp(v->s, c->v.s);
  } else {
    if (v->t != c->v.t) return c->op == C_NE;
    cmp = val_compare(v, &c->v);
  }
  switch (c->op) {
  case C_EQ: return cmp == 0; case C_NE: return cmp != 0; case C_LT: return cmp < 0;
  case C_GT: return cmp > 0; case C_LE: return cmp <= 0; default: return cmp >= 0;
  }
}

/* ------------------------------------------------------------------ UTF-8 */
long u8_len(const char *s)
{
  long n = 0;
  while (*s) { if (((u8)*s & 0xC0) != 0x80) n++; s++; }
  return n;
}
const char *u8_at(const char *s, long i)
{
  while (*s && i > 0) { s++; while (((u8)*s & 0xC0) == 0x80) s++; i--; }
  return s;
}

/* ------------------------------------------------------------------ operators */
static int op_scalar(Ctx *cx, int op, const Value *a, const Value *b, Value *out)
{
  double x, y, r;
  int e;
  if (op == T_CAT) {
    Buf buf;
    if (a->t == V_ERR) { *out = val_err(a->e); return 0; }
    if (b->t == V_ERR) { *out = val_err(b->e); return 0; }
    buf_init(&buf);
    to_text(cx->b, a, &buf);
    to_text(cx->b, b, &buf);
    *out = val_str(buf.data ? buf.data : "");
    buf_free(&buf);
    return 0;
  }
  if (op >= T_EQ && op <= T_GE) {
    int c;
    if (a->t == V_ERR) { *out = val_err(a->e); return 0; }
    if (b->t == V_ERR) { *out = val_err(b->e); return 0; }
    c = val_compare(a, b);
    switch (op) {
    case T_EQ: *out = val_bool(c == 0); break; case T_NE: *out = val_bool(c != 0); break;
    case T_LT: *out = val_bool(c < 0); break; case T_GT: *out = val_bool(c > 0); break;
    case T_LE: *out = val_bool(c <= 0); break; default: *out = val_bool(c >= 0); break;
    }
    return 0;
  }
  if ((e = to_num(a, &x))) { *out = val_err(e); return 0; }
  if ((e = to_num(b, &y))) { *out = val_err(e); return 0; }
  switch (op) {
  case T_ADD: r = x + y; break;
  case T_SUB: r = x - y; break;
  case T_MUL: r = x * y; break;
  case T_DIV: if (y == 0) { *out = val_err(E_DIV0); return 0; } r = x / y; break;
  case T_POW:
    if (x == 0 && y == 0) { *out = val_err(E_NUM); return 0; }
    if (x == 0 && y < 0) { *out = val_err(E_DIV0); return 0; }
    if (x < 0 && y != floor(y)) {
      /* odd roots of negatives are allowed (-8^(1/3) = -2) */
      double inv = 1 / y;
      if (fabs(inv - floor(inv + 0.5)) < 1e-10 && ((long)floor(inv + 0.5)) % 2) { r = -pow(-x, y); break; }
      *out = val_err(E_NUM); return 0;
    }
    r = pow(x, y);
    break;
  default: r = 0;
  }
  if (!(r == r) || r > 1.7976931348623157e308 || r < -1.7976931348623157e308) { *out = val_err(E_NUM); return 0; }
  *out = val_num(r);
  return 0;
}
/* a binary operator, element by element when either side is a range or array */
static Value binop(Ctx *cx, int op, Value *a, Value *b)
{
  int multi_a = (a->t == V_REF && (a->r1 != a->r2 || a->c1 != a->c2)) || a->t == V_ARR;
  int multi_b = (b->t == V_REF && (b->r1 != b->r2 || b->c1 != b->c2)) || b->t == V_ARR;
  Value out;
  if (!multi_a && !multi_b) {
    Value sa = scalar(cx, a), sb = scalar(cx, b);
    op_scalar(cx, op, &sa, &sb, &out);
    val_free(&sa); val_free(&sb);
    return out;
  }
  {
    int err;
    Arr *aa = to_array(cx, a, &err), *ab, *res;
    i32 rows, cols, r, c;
    if (!aa) return val_err(err);
    if (!(ab = to_array(cx, b, &err))) { arr_free(aa); return val_err(err); }
    rows = aa->rows > ab->rows ? aa->rows : ab->rows;
    cols = aa->cols > ab->cols ? aa->cols : ab->cols;
    if (!(res = arr_new(rows, cols))) { arr_free(aa); arr_free(ab); return val_err(E_NUM); }
    for (r = 0; r < rows; r++)
      for (c = 0; c < cols; c++) {
        /* a single row or column stretches across the other side (broadcasting) */
        i32 ra = aa->rows == 1 ? 0 : r, ca = aa->cols == 1 ? 0 : c, rb = ab->rows == 1 ? 0 : r, cb = ab->cols == 1 ? 0 : c;
        if (ra >= aa->rows || ca >= aa->cols || rb >= ab->rows || cb >= ab->cols) res->v[r * cols + c] = val_err(E_NA);
        else op_scalar(cx, op, &aa->v[ra * aa->cols + ca], &ab->v[rb * ab->cols + cb], &res->v[r * cols + c]);
      }
    arr_free(aa); arr_free(ab);
    out = val_empty(); out.t = V_ARR; out.a = res;
    return out;
  }
}
static Value unop(Ctx *cx, int op, Value *a)
{
  Value one = val_num(op == T_PCT ? 0.01 : op == T_NEG ? -1 : 1), r;
  if (op == T_POS) { Value s = a->t == V_REF || a->t == V_ARR ? val_copy(a) : scalar(cx, a); return s; }
  r = binop(cx, T_MUL, a, &one);
  return r;
}

/* ------------------------------------------------------------------ names */
static Value eval_name(Ctx *cx, const char *name)
{
  int i, found = -1;
  for (i = 0; i < cx->b->nnames; i++)
    if (str_ieq(cx->b->names[i].name, name)) {
      if (cx->b->names[i].sheet == cx->sheet) { found = i; break; }
      if (cx->b->names[i].sheet == SHEET_SELF) found = i;
    }
  if (found < 0 || !cx->b->names[found].f) return val_err(E_NAME);
  if (cx->depth > MAXCALL) return val_err(E_CALC);
  {
    Ctx sub = *cx;
    sub.depth++;
    return eval_f(&sub, cx->b->names[found].f);
  }
}

/* ------------------------------------------------------------------ the evaluator */
static Value eval_f(Ctx *cx, const Formula *f)
{
  Value local[24], *st = local;
  int sp = 0, i;
  Value result;
  if (!f || !f->n) return val_err(E_VALUE);
  if (f->n > 24 && !(st = (Value *)malloc((size_t)f->n * sizeof(Value)))) return val_err(E_NUM);
  for (i = 0; i < f->n; i++) {
    const Tok *t = &f->t[i];
    Value v = val_empty();
    switch (t->op) {
    case T_NUM: v = val_num(t->n); break;
    case T_STR: v = val_str(t->s); break;
    case T_BOOL: v = val_bool(t->a); break;
    case T_ERR: v = val_err(t->a); break;
    case T_MISSING: v.e = 1; break;
    case T_PAREN: continue;
    case T_ARRAY: { Value a = val_empty(); a.t = V_ARR; a.a = t->arr; v = val_copy(&a); break; }
    case T_REF: case T_AREA:
      v.t = V_REF;
      v.sheet = (u16)(t->a == SHEET_SELF ? cx->sheet : t->a);
      v.r1 = t->r1; v.c1 = t->c1;
      v.r2 = t->op == T_REF ? t->r1 : t->r2; v.c2 = t->op == T_REF ? t->c1 : t->c2;
      if (v.sheet >= cx->b->nsh) v = val_err(E_REF);
      break;
    case T_NAME: v = eval_name(cx, t->s); break;
    case T_NEG: case T_POS: case T_PCT:
      if (sp < 1) goto bad;
      v = unop(cx, t->op, &st[sp - 1]);
      val_free(&st[--sp]);
      break;
    case T_RANGE: {
      Value *a, *b;
      if (sp < 2) goto bad;
      a = &st[sp - 2]; b = &st[sp - 1];
      if (a->t == V_REF && b->t == V_REF && a->sheet == b->sheet) {
        v.t = V_REF; v.sheet = a->sheet;
        v.r1 = a->r1 < b->r1 ? a->r1 : b->r1; v.c1 = a->c1 < b->c1 ? a->c1 : b->c1;
        v.r2 = a->r2 > b->r2 ? a->r2 : b->r2; v.c2 = a->c2 > b->c2 ? a->c2 : b->c2;
      } else v = val_err(a->t == V_ERR ? a->e : b->t == V_ERR ? b->e : E_VALUE);
      val_free(&st[--sp]); val_free(&st[--sp]);
      break; }
    case T_FUNC: {
      int argc = t->flags;
      if (sp < argc) goto bad;
      if (t->a == 0xFFFF || t->a >= func_count) v = val_err(E_NAME);
      else {
        const FuncDef *fd = &func_table[t->a];
        if (argc < fd->minargs || argc > fd->maxargs) v = val_err(E_VALUE);
        else v = fd->fn(cx, &st[sp - argc], argc);
      }
      while (argc-- > 0) val_free(&st[--sp]);
      break; }
    default:
      if (sp < 2) goto bad;
      v = binop(cx, t->op, &st[sp - 2], &st[sp - 1]);
      val_free(&st[--sp]); val_free(&st[--sp]);
      break;
    }
    st[sp++] = v;
  }
  if (sp != 1) goto bad;
  result = st[0];
  if (st != local) free(st);
  return result;
bad:
  while (sp > 0) val_free(&st[--sp]);
  if (st != local) free(st);
  return val_err(E_VALUE);
}

Value book_eval_text(Book *b, int sheet, const char *text)
{
  int pos;
  Formula *f = formula_parse(b, sheet, text[0] == '=' ? text + 1 : text, &pos);
  Ctx cx;
  Value v;
  if (!f) return val_err(E_NAME);
  cx.b = b; cx.sheet = sheet; cx.row = 0; cx.col = 0; cx.depth = 0;
  v = eval_f(&cx, f);
  formula_free(f);
  if (v.t == V_REF) { Value s = scalar(&cx, &v); val_free(&v); v = s; }
  return v;
}
Value book_eval_at(Book *b, int sheet, i32 r, i32 c, const Formula *f)
{
  Ctx cx;
  Value v;
  cx.b = b; cx.sheet = sheet; cx.row = r; cx.col = c; cx.depth = 0;
  v = eval_f(&cx, f);
  if (v.t == V_REF) { Value s = scalar(&cx, &v); val_free(&v); v = s; }
  return v;
}
