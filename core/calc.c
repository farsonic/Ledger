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
#define MAXGENS 6                 /* recalculations in a row while spill ranges change shape */

static int g_cut;                 /* times a chain was cut in this pass */
static int g_circular;            /* a circular reference was met */
static int g_spill_changed;       /* a dynamic array's spill range changed shape in this generation */
static const Value g_empty;       /* all zero: V_EMPTY */
static const Value g_zero = { V_NUM, 0, 0, 0.0, NULL, 0, 0, 0, 0, NULL };
/* where dynamic arrays spilled before this recalculation began: a cell read inside one computes its
   formula first (spilled cells are cleared at the start of each recalculation and made again) */
typedef struct { int sheet; Rect r; } Hint;
static Hint *g_hints;
static int g_nhints;

static Value eval_f(Ctx *cx, const Formula *f);
extern int formula_lz_none;
static void compute_cell(Book *b, int sheet, i32 r, i32 c, Cell *cell, int depth);

/* ------------------------------------------------------------------ spilling */
static int rects_meet(const Rect *a, const Rect *b) { return a->r1 <= b->r2 && b->r1 <= a->r2 && a->c1 <= b->c2 && b->c1 <= a->c2; }
/* the formulas whose old spill range meets area computed now, if still stale */
static void ensure_spills(Book *b, int sheet, const Rect *area, int depth)
{
  int i;
  for (i = 0; i < g_nhints; i++) {
    Hint h = g_hints[i];
    Cell *cell;
    if (h.sheet != sheet || !rects_meet(&h.r, area)) continue;
    if (sheet >= b->nsh || !(cell = sheet_cell(b->sh[sheet], h.r.r1, h.r.c1))) continue;
    if (cell->f && cell->calcgen != b->calcgen && !cell->calcstate) compute_cell(b, sheet, h.r.r1, h.r.c1, cell, depth + 1);
  }
}
/* the style a cell made afresh at r, c would have */
static u16 default_style(Sheet *s, i32 r, i32 c)
{
  Row *row = r < s->nrows ? s->rows[r] : NULL;
  return (u16)(row && row->style ? row->style : (c < s->ncols ? s->cols[c].style : 0));
}
/* a spilled cell given back: gone, or kept empty for its own format */
static void unspill(Sheet *s, i32 r, Cell *cell)
{
  i32 c = cell->col;
  val_free(&cell->v);
  cell->spill = 0;
  if (cell->style == default_style(s, r, c) && !cell->f) sheet_cell_clear(s, r, c);
}
/* every spilled cell of the book given back (at the start of a recalculation) */
static void strip_spills(Book *b)
{
  int si;
  for (si = 0; si < b->nsh; si++) {
    Sheet *s = b->sh[si];
    i32 r;
    for (r = 0; r < s->nrows; r++) {
      int i;
      if (!s->rows[r]) continue;
      for (i = s->rows[r]->n - 1; i >= 0; i--)
        if (s->rows[r]->c[i].spill) unspill(s, r, &s->rows[r]->c[i]);
    }
  }
}
/* the spill ranges of the book's dynamic arrays as they last were */
static void make_hints(Book *b)
{
  int si, cap = 0;
  free(g_hints); g_hints = NULL; g_nhints = 0;
  for (si = 0; si < b->nsh; si++) {
    Sheet *s = b->sh[si];
    i32 r;
    for (r = 0; r < s->nrows; r++) {
      int i;
      if (!s->rows[r]) continue;
      for (i = 0; i < s->rows[r]->n; i++) {
        Cell *cell = &s->rows[r]->c[i];
        if (!cell->f || (long)cell->f->sr * cell->f->sc <= 1) continue;
        if (g_nhints == cap) {
          Hint *nh;
          cap = cap ? cap * 2 : 16;
          if (!(nh = (Hint *)realloc(g_hints, (size_t)cap * sizeof(Hint)))) return;
          g_hints = nh;
        }
        g_hints[g_nhints].sheet = si;
        g_hints[g_nhints].r.r1 = r; g_hints[g_nhints].r.c1 = cell->col;
        g_hints[g_nhints].r.r2 = r + cell->f->sr - 1; g_hints[g_nhints].r.c2 = cell->col + cell->f->sc - 1;
        g_nhints++;
      }
    }
  }
}
/* the cells a formula at r, c spilled into last time, given back (when it is computed outside a
   full recalculation) */
static void clear_own_spill(Sheet *s, i32 r, i32 c, const Formula *f)
{
  i32 i, j;
  if ((long)f->sr * f->sc <= 1 || f->spillblocked) return;
  for (i = 0; i < f->sr && r + i < s->nrows; i++)
    for (j = 0; j < f->sc; j++) {
      Cell *cell;
      if (!i && !j) continue;
      if ((cell = sheet_cell(s, r + i, c + j)) && cell->spill) unspill(s, r + i, cell);
    }
}
/* one element as a cell shows it */
static Value cell_element(const Value *v)
{
  if (v->t == V_EMPTY) return val_num(0);         /* an empty cell spilled shows 0, as in Excel */
  if (v->t == V_LAMBDA || v->t == V_ARR || v->t == V_REF) return val_err(E_CALC);
  return val_copy(v);
}
/* rows by cols at r, c free for a spill: nothing typed in, no formula, no other spill, no merge */
static int spill_room(Sheet *s, i32 r, i32 c, i32 rows, i32 cols)
{
  i32 i, j;
  int m;
  if (r + rows > MAXROW || c + cols > MAXCOL) return 0;
  for (i = 0; i < rows; i++) {
    Row *row = r + i < s->nrows ? s->rows[r + i] : NULL;
    int k;
    if (!row) continue;
    for (k = 0; k < row->n; k++) {
      Cell *cell = &row->c[k];
      if (cell->col < c || cell->col >= c + cols || (!i && cell->col == c)) continue;
      if (cell->f || cell->spill || cell->v.t != V_EMPTY) return 0;
    }
  }
  for (m = 0; m < s->nmerges; m++) {
    Rect a; a.r1 = r; a.c1 = c; a.r2 = r + rows - 1; a.c2 = c + cols - 1;
    if (rects_meet(&a, &s->merges[m])) return 0;
  }
  (void)j;
  return 1;
}
/* the array's elements written into the cells right of and below r, c (not r, c itself) */
static void spill_write(Book *b, Sheet *s, i32 r, i32 c, const Arr *a, int force)
{
  i32 i, j;
  for (i = 0; i < a->rows; i++)
    for (j = 0; j < a->cols; j++) {
      Cell *cell;
      if ((!i && !j) || r + i >= MAXROW || c + j >= MAXCOL) continue;
      cell = sheet_cell(s, r + i, c + j);
      if (force && cell && (cell->f || (!cell->spill && cell->v.t != V_EMPTY))) continue;   /* {=...}: never over a typed cell */
      if (!(cell = sheet_cell_make(s, r + i, c + j))) continue;
      val_free(&cell->v);
      cell->v = cell_element(&a->v[i * a->cols + j]);
      cell->spill = 1;
      cell->calcgen = b->calcgen;
    }
}

/* ------------------------------------------------------------------ reading cells */
/* the value of cell r, c on sheet, computing it if stale */
static const Value *read_cell(Book *b, int sheet, i32 r, i32 c, int depth)
{
  Cell *cell;
  if (sheet < 0 || sheet >= b->nsh) return &g_empty;
  cell = sheet_cell(b->sh[sheet], r, c);
  if (g_nhints && (!cell || (!cell->f && !cell->spill && cell->v.t == V_EMPTY))) {
    /* perhaps inside a spill range not made yet in this recalculation */
    Rect pt; pt.r1 = pt.r2 = r; pt.c1 = pt.c2 = c;
    ensure_spills(b, sheet, &pt, depth);
    cell = sheet_cell(b->sh[sheet], r, c);
  }
  if (!cell) return &g_empty;
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
/* the spill ranges meeting a reference made before the reference is walked */
static void ref_ready(Ctx *cx, const Value *v)
{
  Rect a;
  if (!g_nhints || v->t != V_REF) return;
  a.r1 = v->r1; a.c1 = v->c1; a.r2 = v->r2; a.c2 = v->c2;
  ensure_spills(cx->b, v->sheet, &a, cx->depth);
}

static int is_multi(const Value *v);
/* compute one formula cell and store its result; a dynamic array spills over the cells right and
   below (#SPILL! when something is in the way), a {=...} formula fills its range */
static void compute_cell(Book *b, int sheet, i32 r, i32 c, Cell *cell, int depth)
{
  Ctx cx;
  Value v;
  Formula *f = cell->f;
  Sheet *s = b->sh[sheet];
  int cuts = g_cut;
  i32 oldr = f->sr, oldc = f->sc, nr = 1, nc = 1;
  u8 arrayish = 0, blocked = 0;
  if (depth > MAXCALL) { g_cut++; return; }
  cell->calcstate = 1;
  clear_own_spill(s, r, c, f);
  cell = sheet_cell(s, r, c);
  memset(&cx, 0, sizeof cx);
  cx.b = b; cx.sheet = sheet; cx.row = r; cx.col = c; cx.depth = depth;
  cx.dyn = !f->legacy; cx.arrayish = &arrayish;
  v = eval_f(&cx, f);
  if (f->cse && f->arows > 0 && f->acols > 0) {
    /* {=...}: the result fitted to the formula's range, a single row or column repeated, #N/A past its end */
    int err;
    Arr *src = v.t == V_REF || v.t == V_ARR ? to_array(&cx, &v, &err) : NULL;
    Arr *a = arr_new(f->arows, f->acols);
    i32 i, j;
    if (a) {
      for (i = 0; i < a->rows; i++)
        for (j = 0; j < a->cols; j++) {
          Value *d = &a->v[i * a->cols + j];
          if (!src) *d = v.t == V_REF || v.t == V_ARR ? val_err(err ? err : E_VALUE) : val_copy(&v);
          else {
            i32 si = src->rows == 1 ? 0 : i, sj = src->cols == 1 ? 0 : j;
            *d = si < src->rows && sj < src->cols ? val_copy(&src->v[si * src->cols + sj]) : val_err(E_NA);
          }
        }
      spill_write(b, s, r, c, a, 1);
      val_free(&v);
      v = cell_element(&a->v[0]);
      nr = a->rows; nc = a->cols;
      arr_free(a);
    }
    arr_free(src);
    arrayish = 1;
  } else if (!f->legacy && is_multi(&v)) {
    int err;
    Arr *a = to_array(&cx, &v, &err);
    val_free(&v);
    arrayish = 1;
    if (!a) v = val_err(err ? err : E_NUM);
    else {
      nr = a->rows; nc = a->cols;
      if (!spill_room(s, r, c, nr, nc)) { v = val_err(E_SPILL); blocked = 1; }
      else { spill_write(b, s, r, c, a, 0); v = cell_element(&a->v[0]); }
      arr_free(a);
    }
  } else {
    /* the final value of a cell is never a reference; a range in a cell formula meets the cell's row
       or column; an array shows its first element */
    if (v.t == V_REF || v.t == V_ARR) {
      Value sv = scalar(&cx, &v);
      if (v.t == V_ARR) arrayish = 1;
      val_free(&v);
      v = sv;
    }
    if (v.t == V_LAMBDA || v.t == V_REF || v.t == V_ARR) { val_free(&v); v = val_err(E_CALC); }
  }
  if (v.t == V_EMPTY) v = val_num(0);                 /* =A1 of an empty cell shows 0 */
  cell = sheet_cell(s, r, c);
  if (!cell || cell->f != f) { val_free(&v); return; }
  val_free(&cell->v);
  cell->v = v;
  cell->calcstate = 0;
  f->sr = nr; f->sc = nc; f->spillblocked = blocked;
  f->arrayeval = arrayish;
  if ((nr != oldr || nc != oldc) && ((long)nr * nc > 1 || (long)oldr * oldc > 1)) g_spill_changed = 1;
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
  int pass, s, gen;
  i32 r;
  g_circular = 0;
  for (gen = 0; gen < MAXGENS; gen++) {
    b->calcgen++;
    if (!b->calcgen) b->calcgen = 1;
    make_hints(b);
    strip_spills(b);
    g_spill_changed = 0;
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
              if (i >= sh->rows[r]->n) break;
              cell = &sh->rows[r]->c[i];
              if (cell->f && cell->calcgen != b->calcgen) stale++;
            }
          }
        }
      }
      if (!stale) break;
    }
    if (!g_spill_changed) break;
  }
  if (g_circular && b->lx && b->lx->iterate) calc_iterate(b);
  free(g_hints); g_hints = NULL; g_nhints = 0;
}
int book_circular(void) { return g_circular; }
/* see book.h */
int book_spill_range(Book *b, int sheet, i32 r, i32 c, Rect *area, int *blocked)
{
  Sheet *s;
  Cell *cell;
  i32 rr;
  if (sheet < 0 || sheet >= b->nsh) return 0;
  s = b->sh[sheet];
  if (!(cell = sheet_cell(s, r, c))) return 0;
  if (cell->f && !cell->f->legacy && (long)cell->f->sr * cell->f->sc > 1) {
    area->r1 = r; area->c1 = c; area->r2 = r + cell->f->sr - 1; area->c2 = c + cell->f->sc - 1;
    if (blocked) *blocked = cell->f->spillblocked;
    return 1;
  }
  if (!cell->spill) return 0;
  /* a spilled cell: its formula is above it or to its left */
  for (rr = r; rr >= 0; rr--) {
    Row *row = rr < s->nrows ? s->rows[rr] : NULL;
    int i;
    if (!row) continue;
    for (i = 0; i < row->n && row->c[i].col <= c; i++) {
      Formula *f = row->c[i].f;
      if (f && !f->spillblocked && (long)f->sr * f->sc > 1 && rr + f->sr > r && row->c[i].col + f->sc > c) {
        area->r1 = rr; area->c1 = row->c[i].col; area->r2 = rr + f->sr - 1; area->c2 = row->c[i].col + f->sc - 1;
        if (blocked) *blocked = 0;
        return 1;
      }
    }
  }
  return 0;
}
/* see book.h */
void book_adopt_spills(Book *b)
{
  int si;
  for (si = 0; si < b->nsh; si++) {
    Sheet *s = b->sh[si];
    i32 r;
    for (r = 0; r < s->nrows; r++) {
      int i;
      if (!s->rows[r]) continue;
      for (i = 0; i < s->rows[r]->n; i++) {
        Formula *f = s->rows[r]->c[i].f;
        i32 c = s->rows[r]->c[i].col, rr, cc;
        if (!f || f->legacy || (long)f->sr * f->sc <= 1) continue;
        for (rr = r; rr < r + f->sr && rr < s->nrows; rr++)
          for (cc = c; cc < c + f->sc; cc++) {
            Cell *x;
            if ((rr == r && cc == c) || !(x = sheet_cell(s, rr, cc)) || x->f) continue;
            x->spill = 1;
          }
      }
    }
  }
}

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
int calc_is_union(const Value *v) { return v->t == V_ARR && v->a && v->a->rows * v->a->cols > 0 && v->a->v[0].t == V_REF; }
static Value cell_element_or_self(const Value *v) { return v->t == V_LAMBDA ? val_err(E_CALC) : val_copy(v); }
Value arr_make(int rows, int cols)
{
  Value v = val_empty();
  if (!(v.a = arr_new(rows, cols))) return val_err(E_NUM);
  v.t = V_ARR;
  return v;
}
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
  if (v->t == V_ARR) {
    if (calc_is_union(v)) return v->a->rows * v->a->cols == 1 ? scalar(cx, &v->a->v[0]) : val_err(E_VALUE);
    return v->a && v->a->rows > 0 && v->a->cols > 0 ? cell_element_or_self(&v->a->v[0]) : val_err(E_VALUE);
  }
  if (v->t == V_EMPTY) return val_empty();
  if (v->t == V_LAMBDA) return val_err(E_CALC);
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
  if (calc_is_union(v)) {
    if (v->a->rows * v->a->cols == 1) return to_array(cx, &v->a->v[0], err);
    *err = E_VALUE; return NULL;
  }
  if (v->t == V_LAMBDA) { *err = E_CALC; return NULL; }
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
  ref_ready(cx, v);
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
    ref_ready(cx, v);
    r2 = v->r2 < s->nrows - 1 ? v->r2 : s->nrows - 1;
    /* visit only the cells that exist, in order (cells computed meanwhile may come and go: find
       the next column each time) */
    for (r = v->r1; r <= r2; r++) {
      i32 c = v->c1;
      for (;;) {
        Row *row = r < s->nrows ? s->rows[r] : NULL;
        int lo = 0, hi;
        const Value *cv;
        i32 col;
        if (!row) break;
        hi = row->n;
        while (lo < hi) { int mid = (lo + hi) / 2; if (row->c[mid].col < c) lo = mid + 1; else hi = mid; }
        if (lo >= row->n || row->c[lo].col > v->c2) break;
        col = row->c[lo].col;
        cv = ref_cell(cx, v->sheet, r, col);
        if (fn(cx, cv, 0, arg)) return 1;
        if (col >= v->c2) break;
        c = col + 1;
      }
    }
    return 0;
  }
  if (v->t == V_ARR && v->a) {
    int i;
    for (i = 0; i < v->a->rows * v->a->cols; i++) {
      if (v->a->v[i].t == V_REF) { if (each_value(cx, &v->a->v[i], fn, arg)) return 1; }
      else if (fn(cx, &v->a->v[i], 0, arg)) return 1;
    }
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
    if (cx->arrayish) *cx->arrayish = 1;
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

/* ------------------------------------------------------------------ LET, LAMBDA and lazy functions */
/* LET and LAMBDA names: one binding per link, each holding a reference to the one it extends */
struct Env { int refs; Env *up; const char *name; Value v; };
/* a LAMBDA value: its parameters and body (tokens of a formula being evaluated) and the names it
   saw when made; or a function given by name (fid >= 0) */
typedef struct {
  int refs;
  const Formula *f;
  int lo, hi;                     /* the body's tokens */
  int np, nreq;                   /* parameters, and how many aren't [optional] */
  int fid;
  Env *env;
  const Tok *params[1];           /* (np of them) */
} Lambda;
static void env_release(Env *e)
{
  while (e && --e->refs == 0) {
    Env *up = e->up;
    val_free(&e->v);
    free(e);
    e = up;
  }
}
static Env *env_bind(Env *up, const char *name, Value v)
{
  Env *e = (Env *)malloc(sizeof *e);
  if (!e) { val_free(&v); return NULL; }
  e->refs = 1; e->up = up; e->name = name; e->v = v;
  if (up) up->refs++;
  return e;
}
void calc_lambda_ref(void *p, int delta)
{
  Lambda *l = (Lambda *)p;
  if (!l) return;
  l->refs += delta;
  if (l->refs <= 0) { env_release(l->env); free(l); }
}
int calc_is_lambda(const Value *v) { return v->t == V_LAMBDA && v->a; }
static Value lambda_value(Lambda *l) { Value v = val_empty(); v.t = V_LAMBDA; v.a = (Arr *)(void *)l; return v; }
static Value builtin_lambda(int fid)
{
  Lambda *l = (Lambda *)calloc(1, sizeof *l);
  if (!l) return val_err(E_NUM);
  l->refs = 1; l->fid = fid;
  return lambda_value(l);
}

/* which functions are lazy: their arguments are worked out only as needed */
enum { LZ_NONE, LZ_IF, LZ_IFS, LZ_IFERROR, LZ_IFNA, LZ_CHOOSE, LZ_SWITCH, LZ_LET, LZ_LAMBDA, LZ_N };
static int g_lzid[LZ_N];
static const char *const lz_names[LZ_N] = { "", "IF", "IFS", "IFERROR", "IFNA", "CHOOSE", "SWITCH", "LET", "LAMBDA" };
static const char **g_lift;       /* per function id: its mask (func_lift_spec) or NULL */
static void lazy_init(void)
{
  static int done;
  int k;
  if (done) return;
  done = 1;
  func_init();
  for (k = 1; k < LZ_N; k++) g_lzid[k] = func_lookup(lz_names[k]);
  if ((g_lift = (const char **)calloc((size_t)func_count + 1, sizeof *g_lift))) {
    for (k = 0; func_lift_spec[k]; k++) {
      char nm[64];
      const char *sp = strchr(func_lift_spec[k], ' ');
      size_t n = sp ? (size_t)(sp - func_lift_spec[k]) : 0;
      int id;
      if (!n || n >= sizeof nm) continue;
      memcpy(nm, func_lift_spec[k], n); nm[n] = 0;
      if ((id = func_lookup(nm)) >= 0) g_lift[id] = sp + 1;
    }
  }
}
static int lazy_kind(int id)
{
  int k;
  if (id == 0xFFFF) return 0;
  for (k = 1; k < LZ_N; k++) if (g_lzid[k] == id) return k;
  return 0;
}
/* how many values a token takes off the stack */
static int tok_pops(const Tok *t)
{
  switch (t->op) {
  case T_NEG: case T_POS: case T_PCT: case T_PAREN: case T_AT: case T_SPILL: case T_TRIM: return 1;
  case T_FUNC: return t->flags;
  case T_CALL: return t->flags + 1;
  case T_NUM: case T_STR: case T_BOOL: case T_ERR: case T_REF: case T_AREA: case T_NAME: case T_MISSING: case T_ARRAY: return 0;
  default: return 2;
  }
}
/* Formula.lz: sub[i] where token i's operand (its subexpression) begins; head[i] the last lazy
   function whose operands begin at i, next[j] the one before it */
static void make_lz(Formula *f)
{
  int i, any = 0, sp = 0, *sub, *head, *next, *stack;
  for (i = 0; i < f->n; i++) if (f->t[i].op == T_FUNC && lazy_kind(f->t[i].a)) any = 1;
  if (!any || !(f->lz = (int *)malloc((size_t)f->n * 4 * sizeof(int)))) { f->lz = &formula_lz_none; return; }
  sub = f->lz; head = sub + f->n; next = head + f->n; stack = next + f->n;
  for (i = 0; i < f->n; i++) {
    int k = tok_pops(&f->t[i]), st = i;
    head[i] = next[i] = -1;
    while (k-- > 0 && sp > 0) st = stack[--sp];
    sub[i] = st;
    stack[sp++] = st;
  }
  for (i = 0; i < f->n; i++)
    if (f->t[i].op == T_FUNC && lazy_kind(f->t[i].a)) { next[i] = head[sub[i]]; head[sub[i]] = i; }
}
static Value eval_range(Ctx *cx, const Formula *f, int lo, int hi);
static Value call_func(Ctx *cx, int id, Value *args, int argc);

/* a lambda called: its parameters bound to the arguments, its body worked out */
Value calc_call(Ctx *cx, const Value *fv, Value *args, int argc)
{
  Lambda *l;
  Env *e;
  Ctx sub;
  Value v;
  int k;
  if (!calc_is_lambda(fv)) return fv->t == V_ERR ? val_err(fv->e) : val_err(E_VALUE);
  l = (Lambda *)(void *)fv->a;
  if (l->fid >= 0 && !l->f) return call_func(cx, l->fid, args, argc);
  if (argc > l->np || (argc && argc < l->nreq)) return val_err(E_VALUE);
  if (cx->depth >= MAXCALL) return val_err(E_NUM);   /* a LAMBDA calling itself without end */
  e = l->env;
  if (e) e->refs++;
  for (k = 0; k < l->np; k++) {
    Value a = k < argc ? val_copy(&args[k]) : val_empty();
    Env *ne;
    if (k >= argc) a.e = 1;                           /* missing: ISOMITTED is TRUE */
    if (!(ne = env_bind(e, l->params[k]->s, a))) { env_release(e); return val_err(E_NUM); }
    env_release(e);
    e = ne;
  }
  sub = *cx;
  sub.env = e;
  sub.depth++;
  v = eval_range(&sub, l->f, l->lo, l->hi);
  env_release(e);
  return v;
}

/* ------------------------------------------------------------------ names */
static Value eval_name(Ctx *cx, const char *name)
{
  int i, found = -1;
  Env *e;
  for (e = cx->env; e; e = e->up) if (str_ieq(e->name, name)) return val_copy(&e->v);
  for (i = 0; i < cx->b->nnames; i++)
    if (str_ieq(cx->b->names[i].name, name)) {
      if (cx->b->names[i].sheet == cx->sheet) { found = i; break; }
      if (cx->b->names[i].sheet == SHEET_SELF) found = i;
    }
  if (found < 0 || !cx->b->names[found].f) {
    /* a function given by name: BYROW(A1:C3, SUM) */
    int id = found < 0 ? func_lookup(name) : -1;
    if (id >= 0 && !lazy_kind(id)) return builtin_lambda(id);
    return val_err(E_NAME);
  }
  if (cx->depth > MAXCALL) return val_err(E_CALC);
  {
    Ctx sub = *cx;
    sub.depth++;
    sub.env = NULL;                                  /* a defined name doesn't see the caller's LET names */
    return eval_f(&sub, cx->b->names[found].f);
  }
}

/* ------------------------------------------------------------------ functions, mapped over arrays */
static int is_multi(const Value *v)
{
  return (v->t == V_REF && (v->r1 != v->r2 || v->c1 != v->c2)) || (v->t == V_ARR && v->a && v->a->rows * v->a->cols > 1 && !calc_is_union(v));
}
/* an argument a function of single values maps over: in a dynamic-array formula any array or
   range; in a formula from before dynamic arrays only arrays (a range meets the formula's row) */
static int liftable(const Ctx *cx, const Value *v) { return is_multi(v) && (cx->dyn || v->t == V_ARR); }
static char mask_at(const char *m, int i) { size_t n = strlen(m); return n ? m[(size_t)i < n ? (size_t)i : n - 1] : 'a'; }
/* a function of single values given arrays: called for each element (a single row or column
   stretching across, #N/A past the end of a shorter one) */
static Value lift(Ctx *cx, const FuncDef *fd, const char *mask, Value *args, int argc)
{
  i32 rows = 1, cols = 1, r, c;
  int i;
  Value out, *tmp;
  u8 *own;
  /* once mapping, every range or array where a single value goes takes part (array context) */
  for (i = 0; i < argc; i++)
    if (mask_at(mask, i) == 's' && is_multi(&args[i])) {
      i32 ar, ac;
      arg_shape(&args[i], &ar, &ac);
      if (ar > rows) rows = ar;
      if (ac > cols) cols = ac;
    }
  out = arr_make(rows, cols);
  if (out.t != V_ARR) return out;
  if (cx->arrayish) *cx->arrayish = 1;
  if (!(tmp = (Value *)malloc((size_t)argc * sizeof(Value) + (size_t)argc))) { val_free(&out); return val_err(E_NUM); }
  own = (u8 *)(tmp + argc);
  for (r = 0; r < rows; r++)
    for (c = 0; c < cols; c++) {
      Value res;
      int na = 0;
      for (i = 0; i < argc; i++) {
        own[i] = 0;
        tmp[i] = args[i];
        if (mask_at(mask, i) == 's' && is_multi(&args[i])) {
          i32 ar, ac, rr, cc;
          arg_shape(&args[i], &ar, &ac);
          rr = ar == 1 ? 0 : r; cc = ac == 1 ? 0 : c;
          if (rr >= ar || cc >= ac) { na = 1; tmp[i] = val_empty(); continue; }
          if (args[i].t == V_REF) { tmp[i].r1 = tmp[i].r2 = args[i].r1 + rr; tmp[i].c1 = tmp[i].c2 = args[i].c1 + cc; }
          else { tmp[i] = val_copy(&args[i].a->v[rr * ac + cc]); own[i] = 1; }
        }
      }
      if (na) res = val_err(E_NA);
      else {
        res = fd->fn(cx, tmp, argc);
        if (res.t == V_REF || res.t == V_ARR) { Value sv = scalar(cx, &res); val_free(&res); res = sv; }
      }
      for (i = 0; i < argc; i++) if (own[i]) val_free(&tmp[i]);
      out.a->v[r * cols + c] = res;
    }
  free(tmp);
  return out;
}
static Value call_func(Ctx *cx, int id, Value *args, int argc)
{
  const FuncDef *fd;
  if (id < 0 || id >= func_count) return val_err(E_NAME);
  fd = &func_table[id];
  if (argc < fd->minargs || argc > fd->maxargs) return val_err(E_VALUE);
  if (g_lift && g_lift[id]) {
    int i;
    for (i = 0; i < argc; i++)
      if (mask_at(g_lift[id], i) == 's' && liftable(cx, &args[i])) return lift(cx, fd, g_lift[id], args, argc);
  }
  return fd->fn(cx, args, argc);
}
/* a function kept by name (not built in): a LET name or defined name holding a LAMBDA */
static Value call_named(Ctx *cx, const char *name, Value *args, int argc)
{
  Value fv = eval_name(cx, name), v;
  if (fv.t != V_LAMBDA) { int e = fv.t == V_ERR ? fv.e : E_NAME; val_free(&fv); return val_err(e == E_NAME ? E_NAME : E_VALUE); }
  v = calc_call(cx, &fv, args, argc);
  val_free(&fv);
  return v;
}

/* the tokens of argument k of the lazy function at token j: [*lo, *hi) */
static int arg_lo(const Formula *f, int j, int k, int want_hi)
{
  int e = j, m;
  for (m = f->t[j].flags - 1; m > k; m--) e = f->lz[e - 1];
  return want_hi ? e : f->lz[e - 1];
}
#define ARGLO(k) arg_lo(f, j, (k), 0)
#define ARGHI(k) arg_lo(f, j, (k), 1)
/* a lazy function: IF and friends work out only the arguments they need; LET binds names; LAMBDA
   makes a value of its body */
static Value lazy_call(Ctx *cx, const Formula *f, int j)
{
  const Tok *t = &f->t[j];
  int argc = t->flags, kind = lazy_kind(t->a), k, e;
  const FuncDef *fd = &func_table[t->a];
  Value v, c;
  if (kind != LZ_LET && kind != LZ_LAMBDA && (argc < fd->minargs || argc > fd->maxargs)) return val_err(E_VALUE);
#define ARG(i) eval_range(cx, f, ARGLO(i), ARGHI(i))
#define MISSING_ARG(i) (ARGHI(i) == ARGLO(i) + 1 && f->t[ARGLO(i)].op == T_MISSING)
  switch (kind) {
  case LZ_LET: {
    Env *base = cx->env, *env = base;
    if (argc < 3 || argc % 2 == 0) return val_err(E_VALUE);
    if (env) env->refs++;
    for (k = 0; k + 1 < argc; k += 2) {
      Ctx sub2 = *cx;
      Env *ne;
      if (ARGHI(k) != ARGLO(k) + 1 || f->t[ARGLO(k)].op != T_NAME) { env_release(env); return val_err(E_VALUE); }
      sub2.env = env;
      v = eval_range(&sub2, f, ARGLO(k + 1), ARGHI(k + 1));
      if (!(ne = env_bind(env, f->t[ARGLO(k)].s, v))) { env_release(env); return val_err(E_NUM); }
      env_release(env);
      env = ne;
    }
    {
      Ctx sub2 = *cx;
      sub2.env = env;
      v = eval_range(&sub2, f, ARGLO(argc - 1), ARGHI(argc - 1));
    }
    env_release(env);
    return v; }
  case LZ_LAMBDA: {
    Lambda *l;
    int np = argc - 1, opt = 0;
    if (argc < 1) return val_err(E_VALUE);
    for (k = 0; k < np; k++) {
      if (ARGHI(k) != ARGLO(k) + 1 || f->t[ARGLO(k)].op != T_NAME) return val_err(E_VALUE);
      if (f->t[ARGLO(k)].flags & TN_OPT) opt = 1;
      else if (opt) return val_err(E_VALUE);           /* required after optional */
    }
    if (!(l = (Lambda *)calloc(1, sizeof *l + (size_t)(np ? np : 1) * sizeof(Tok *)))) return val_err(E_NUM);
    l->refs = 1; l->f = f; l->lo = ARGLO(argc - 1); l->hi = ARGHI(argc - 1); l->np = np; l->fid = -1;
    for (k = 0; k < np; k++) { l->params[k] = &f->t[ARGLO(k)]; if (!(f->t[ARGLO(k)].flags & TN_OPT)) l->nreq = k + 1; }
    l->env = cx->env;
    if (l->env) l->env->refs++;
    return lambda_value(l); }
  default: break;
  }
  /* IF and friends: the deciding argument first; given an array (dynamic arrays) every argument is
     worked out and the function maps over the elements */
  c = ARG(0);
  if (liftable(cx, &c)) {
    Value *args = (Value *)malloc((size_t)argc * sizeof(Value));
    if (!args) { val_free(&c); return val_err(E_NUM); }
    args[0] = c;
    for (k = 1; k < argc; k++) args[k] = ARG(k);
    v = call_func(cx, t->a, args, argc);
    for (k = 0; k < argc; k++) val_free(&args[k]);
    free(args);
    return v;
  }
  switch (kind) {
  case LZ_IF: {
    int b;
    Value sc = scalar(cx, &c);
    val_free(&c);
    e = to_bool(&sc, &b);
    val_free(&sc);
    if (e) return val_err(e);
    if (b) return MISSING_ARG(1) ? val_num(0) : ARG(1);
    if (argc < 3) return val_bool(0);
    return MISSING_ARG(2) ? val_num(0) : ARG(2); }
  case LZ_IFERROR: case LZ_IFNA: {
    Value sc = scalar(cx, &c);
    int hit = sc.t == V_ERR && (kind == LZ_IFERROR || sc.e == E_NA);
    val_free(&sc);
    if (!hit) return c;
    val_free(&c);
    return MISSING_ARG(1) ? val_num(0) : ARG(1); }
  case LZ_CHOOSE: {
    double d;
    e = arg_num(cx, &c, &d);
    val_free(&c);
    if (e) return val_err(e);
    d = floor(d);
    if (d < 1 || d >= argc) return val_err(E_VALUE);
    return MISSING_ARG((int)d) ? val_num(0) : ARG((int)d); }
  case LZ_IFS: {
    if (argc % 2) { val_free(&c); return val_err(E_VALUE); }
    for (k = 0; k < argc; k += 2) {
      int b;
      if (k) {
        c = ARG(k);
        if (liftable(cx, &c)) {
          /* an array condition further on: everything worked out, mapped */
          Value *args = (Value *)malloc((size_t)argc * sizeof(Value));
          int i;
          val_free(&c);
          if (!args) return val_err(E_NUM);
          for (i = 0; i < argc; i++) args[i] = ARG(i);
          v = call_func(cx, t->a, args, argc);
          for (i = 0; i < argc; i++) val_free(&args[i]);
          free(args);
          return v;
        }
      }
      e = arg_bool(cx, &c, &b);
      val_free(&c);
      if (e) return val_err(e);
      if (b) return ARG(k + 1);
    }
    return val_err(E_NA); }
  case LZ_SWITCH: {
    Value x = scalar(cx, &c);
    val_free(&c);
    if (x.t == V_ERR) return x;
    for (k = 1; k + 1 < argc; k += 2) {
      Value cv = ARG(k), cs;
      int eq;
      if (liftable(cx, &cv)) {
        Value *args = (Value *)malloc((size_t)argc * sizeof(Value));
        int i;
        val_free(&cv);
        if (!args) { val_free(&x); return val_err(E_NUM); }
        args[0] = x;
        for (i = 1; i < argc; i++) args[i] = ARG(i);
        v = call_func(cx, t->a, args, argc);
        for (i = 0; i < argc; i++) val_free(&args[i]);
        free(args);
        return v;
      }
      cs = scalar(cx, &cv);
      val_free(&cv);
      eq = cs.t != V_ERR && val_compare(&x, &cs) == 0 && !((x.t == V_STR) != (cs.t == V_STR));
      val_free(&cs);
      if (eq) { val_free(&x); return ARG(k + 1); }
    }
    val_free(&x);
    if (k < argc) return ARG(k);                      /* the default */
    return val_err(E_NA); }
  }
#undef ARG
#undef MISSING_ARG
  val_free(&c);
  return val_err(E_VALUE);
}

/* ------------------------------------------------------------------ operators of the dynamic-array era */
/* (A1:B2,D4): the references side by side in an array (flattening unions within) */
static Value make_union(const Value *a, const Value *b)
{
  int na = calc_is_union(a) ? a->a->rows * a->a->cols : 1, nb = calc_is_union(b) ? b->a->rows * b->a->cols : 1, i;
  Value v;
  if (a->t == V_ERR) return val_err(a->e);
  if (b->t == V_ERR) return val_err(b->e);
  if ((a->t != V_REF && !calc_is_union(a)) || (b->t != V_REF && !calc_is_union(b))) return val_err(E_VALUE);
  v = arr_make(1, na + nb);
  if (v.t != V_ARR) return v;
  for (i = 0; i < na; i++) v.a->v[i] = calc_is_union(a) ? a->a->v[i] : *a;
  for (i = 0; i < nb; i++) v.a->v[na + i] = calc_is_union(b) ? b->a->v[i] : *b;
  return v;
}
/* A1#: the spill range of the dynamic array formula at A1 */
static Value spill_ref(Ctx *cx, const Value *a)
{
  Cell *cell;
  Value v;
  if (a->t == V_ERR) return val_err(a->e);
  if (a->t != V_REF || a->sheet >= cx->b->nsh) return val_err(E_REF);
  read_cell(cx->b, a->sheet, a->r1, a->c1, cx->depth);      /* worked out first */
  cell = sheet_cell(cx->b->sh[a->sheet], a->r1, a->c1);
  if (!cell || !cell->f || cell->f->legacy) return val_err(E_REF);
  if (cell->f->spillblocked) return val_err(E_SPILL);
  v = *a;
  v.r1 = a->r1; v.c1 = a->c1;
  v.r2 = a->r1 + (cell->f->sr > 0 ? cell->f->sr : 1) - 1;
  v.c2 = a->c1 + (cell->f->sc > 0 ? cell->f->sc : 1) - 1;
  return v;
}

/* ------------------------------------------------------------------ the evaluator */
static Value eval_f(Ctx *cx, const Formula *f)
{
  if (!f || !f->n) return val_err(E_VALUE);
  lazy_init();
  if (!f->lz) make_lz((Formula *)f);
  return eval_range(cx, f, 0, f->n);
}
/* tokens lo..hi-1 of f (a whole subexpression) */
static Value eval_range(Ctx *cx, const Formula *f, int lo, int hi)
{
  Value local[24], *st = local;
  int sp = 0, i, n = hi - lo;
  const int *head = f->lz && f->lz != &formula_lz_none ? f->lz + f->n : NULL, *next = head ? head + f->n : NULL;
  Value result;
  if (n <= 0) return val_err(E_VALUE);
  if (n > 24 && !(st = (Value *)malloc((size_t)n * sizeof(Value)))) return val_err(E_NUM);
  for (i = lo; i < hi; i++) {
    const Tok *t = &f->t[i];
    Value v = val_empty();
    if (head && head[i] >= 0) {
      /* a lazy function whose arguments begin here (the outermost one within this range) */
      int j = head[i];
      while (j >= 0 && j >= hi) j = next[j];
      if (j >= 0) {
        st[sp++] = lazy_call(cx, f, j);
        i = j;
        continue;
      }
    }
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
    case T_AT: {
      Value *a;
      if (sp < 1) goto bad;
      a = &st[sp - 1];
      v = a->t == V_REF && (a->r1 != a->r2 || a->c1 != a->c2) ? scalar(cx, a) : a->t == V_ARR ? scalar(cx, a) : val_copy(a);
      val_free(&st[--sp]);
      break; }
    case T_SPILL:
      if (sp < 1) goto bad;
      v = spill_ref(cx, &st[sp - 1]);
      val_free(&st[--sp]);
      break;
    case T_TRIM:
      if (sp < 1) goto bad;
      v = calc_trim(cx, &st[sp - 1], t->flags, t->flags);
      val_free(&st[--sp]);
      break;
    case T_UNION:
      if (sp < 2) goto bad;
      v = make_union(&st[sp - 2], &st[sp - 1]);
      /* the references moved into the union: nothing to free but the arrays of unions */
      val_free(&st[--sp]); val_free(&st[--sp]);
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
      if (t->a == 0xFFFF || t->a >= func_count) v = t->s ? call_named(cx, t->s, &st[sp - argc], argc) : val_err(E_NAME);
      else v = call_func(cx, t->a, &st[sp - argc], argc);
      while (argc-- > 0) val_free(&st[--sp]);
      break; }
    case T_CALL: {
      int argc = t->flags;
      if (sp < argc + 1) goto bad;
      v = calc_call(cx, &st[sp - argc - 1], &st[sp - argc], argc);
      while (argc-- >= 0) val_free(&st[--sp]);
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
  memset(&cx, 0, sizeof cx);
  cx.b = b; cx.sheet = sheet; cx.row = 0; cx.col = 0; cx.depth = 0; cx.dyn = 1;
  v = eval_f(&cx, f);
  formula_free(f);
  if (v.t == V_LAMBDA) { val_free(&v); v = val_err(E_CALC); }
  if (v.t == V_REF) { Value s = scalar(&cx, &v); val_free(&v); v = s; }
  return v;
}
Value book_eval_at(Book *b, int sheet, i32 r, i32 c, const Formula *f)
{
  Ctx cx;
  Value v;
  memset(&cx, 0, sizeof cx);
  cx.b = b; cx.sheet = sheet; cx.row = r; cx.col = c; cx.depth = 0;
  v = eval_f(&cx, f);
  if (v.t == V_LAMBDA) { val_free(&v); v = val_err(E_CALC); }
  if (v.t == V_REF) { Value s = scalar(&cx, &v); val_free(&v); v = s; }
  return v;
}
