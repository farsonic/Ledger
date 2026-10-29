/*
 * cond.c - conditional formatting (see cond.h): which rules cover a cell, whether each one's test
 * passes, and the look that results. Tests that need the whole range (duplicates, top ten, the
 * average, data bars, colour scales) work it out once and keep it in the rule's cache until the
 * workbook changes or is recalculated.
 */
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>
#include "cond.h"
#include "calc.h"

struct CondCache {
  u32 calc, chg;                  /* the workbook's calcgen and changes when worked out */
  char **keys; int *counts; int nkeys;   /* CF_DUP, CF_UNIQUE: each value (as a key) and how often it comes */
  double t;                       /* CF_TOP: the threshold; CF_AVERAGE: the average */
  double lo, mid, hi;             /* CF_BAR, CF_SCALE: the smallest, middle and largest number */
  int any;                        /* some numbers were found */
};

/* ------------------------------------------------------------------ rules */
void cond_rule_init(CondRule *r)
{
  memset(r, 0, sizeof *r);
  r->color = COLOR_AUTO;
  r->rank = 10;
  r->bar[0] = 0x638EC6; r->bar[1] = 0xFFEB84; r->bar[2] = 0x63BE7B;
}
static void cache_free(CondRule *r)
{
  int i;
  if (!r->cache) return;
  for (i = 0; i < r->cache->nkeys; i++) free(r->cache->keys[i]);
  free(r->cache->keys); free(r->cache->counts);
  free(r->cache);
  r->cache = NULL;
}
void cond_rule_free(CondRule *r)
{
  cache_free(r);
  free(r->areas); formula_free(r->f1); formula_free(r->f2); free(r->text);
  r->areas = NULL; r->nareas = 0; r->f1 = r->f2 = NULL; r->text = NULL;
}
int cond_rule_copy(CondRule *dst, const CondRule *src)
{
  *dst = *src;
  dst->cache = NULL;
  dst->areas = NULL; dst->f1 = dst->f2 = NULL; dst->text = NULL;
  if (src->nareas && !(dst->areas = (Rect *)malloc((size_t)src->nareas * sizeof(Rect)))) goto fail;
  if (src->nareas) memcpy(dst->areas, src->areas, (size_t)src->nareas * sizeof(Rect));
  if (src->f1 && !(dst->f1 = formula_copy(src->f1))) goto fail;
  if (src->f2 && !(dst->f2 = formula_copy(src->f2))) goto fail;
  if (src->text && !(dst->text = str_dup(src->text))) goto fail;
  return 1;
fail:
  cond_rule_free(dst);
  return 0;
}
void cond_free_all(CondRule *r, int n)
{
  int i;
  for (i = 0; i < n; i++) cond_rule_free(&r[i]);
  free(r);
}
CondRule *cond_copy_all(const CondRule *r, int n)
{
  CondRule *c;
  int i;
  if (!n || !(c = (CondRule *)calloc((size_t)n, sizeof *c))) return NULL;
  for (i = 0; i < n; i++)
    if (!cond_rule_copy(&c[i], &r[i])) { cond_free_all(c, i); return NULL; }
  return c;
}
int cond_covers(const CondRule *r, i32 row, i32 col)
{
  int i;
  for (i = 0; i < r->nareas; i++)
    if (row >= r->areas[i].r1 && row <= r->areas[i].r2 && col >= r->areas[i].c1 && col <= r->areas[i].c2) return 1;
  return 0;
}

/* ------------------------------------------------------------------ the whole range's figures */
/* each used cell of the rule's areas (whole columns stop at the sheet's last row) */
typedef int (*CellFn)(const Value *v, void *arg);
static void each_cell(Book *b, int sheet, const CondRule *r, CellFn fn, void *arg)
{
  Sheet *s = b->sh[sheet];
  i32 lr = sheet_last_row(s), lc = sheet_last_col(s), row;
  int i, k;
  for (i = 0; i < r->nareas; i++) {
    Rect a = r->areas[i];
    if (a.r2 > lr) a.r2 = lr;
    if (a.c2 > lc) a.c2 = lc;
    for (row = a.r1; row <= a.r2; row++) {
      Row *rw = row < s->nrows ? s->rows[row] : NULL;
      if (!rw) continue;
      for (k = 0; k < rw->n; k++) {
        i32 col = rw->c[k].col;
        if (col < a.c1 || col > a.c2) continue;
        if (fn(cell_value(b, sheet, row, col), arg)) return;
      }
    }
  }
}
/* a value as a key for finding duplicates: numbers by value, text ignoring case */
static void value_key(Book *b, const Value *v, char *out, size_t cap)
{
  size_t i;
  if (v->t == V_NUM) { snprintf(out, cap, "n%.15g", v->n); return; }
  if (v->t == V_BOOL) { snprintf(out, cap, "b%d", v->e); return; }
  if (v->t == V_ERR) { snprintf(out, cap, "e%d", v->e); return; }
  out[0] = 's';
  for (i = 0; v->t == V_STR && v->s && v->s[i] && i + 2 < cap; i++) out[i + 1] = (char)((v->s[i] >= 'A' && v->s[i] <= 'Z') ? v->s[i] + 32 : v->s[i]);
  out[i + 1] = 0;
  (void)b;
}
typedef struct { Book *b; CondCache *c; int cap; double *nums; int n, ncap; } Gather;
static int gather_key(const Value *v, void *arg)
{
  Gather *g = (Gather *)arg;
  char k[256];
  int i;
  if (v->t == V_EMPTY || (v->t == V_STR && (!v->s || !v->s[0]))) return 0;
  value_key(g->b, v, k, sizeof k);
  for (i = 0; i < g->c->nkeys; i++) if (!strcmp(g->c->keys[i], k)) { g->c->counts[i]++; return 0; }
  if (g->c->nkeys == g->cap) {
    int nc = g->cap ? g->cap * 2 : 64;
    char **nk = (char **)realloc(g->c->keys, (size_t)nc * sizeof(char *));
    int *nn;
    if (!nk) return 1;
    g->c->keys = nk;
    if (!(nn = (int *)realloc(g->c->counts, (size_t)nc * sizeof(int)))) return 1;
    g->c->counts = nn;
    g->cap = nc;
  }
  if (!(g->c->keys[g->c->nkeys] = str_dup(k))) return 1;
  g->c->counts[g->c->nkeys++] = 1;
  return g->c->nkeys > 20000;     /* (enough: very big ranges aren't searched further) */
}
static int gather_num(const Value *v, void *arg)
{
  Gather *g = (Gather *)arg;
  if (v->t != V_NUM) return 0;
  if (g->n == g->ncap) {
    int nc = g->ncap ? g->ncap * 2 : 256;
    double *nn = (double *)realloc(g->nums, (size_t)nc * sizeof(double));
    if (!nn) return 1;
    g->nums = nn; g->ncap = nc;
  }
  g->nums[g->n++] = v->n;
  return g->n > 200000;
}
static int cmp_dbl(const void *a, const void *b)
{
  double x = *(const double *)a, y = *(const double *)b;
  return x < y ? -1 : x > y;
}
/* the rule's figures, worked out again if the workbook changed since */
static CondCache *figures(Book *b, int sheet, CondRule *r)
{
  Gather g;
  CondCache *c = r->cache;
  if (c && c->calc == b->calcgen && c->chg == b->changes) return c;
  cache_free(r);
  if (!(c = r->cache = (CondCache *)calloc(1, sizeof *c))) return NULL;
  c->calc = b->calcgen; c->chg = b->changes;
  memset(&g, 0, sizeof g);
  g.b = b; g.c = c;
  if (r->type == CF_DUP || r->type == CF_UNIQUE) { each_cell(b, sheet, r, gather_key, &g); return c; }
  each_cell(b, sheet, r, gather_num, &g);
  if (g.n) {
    int i;
    double sum = 0;
    qsort(g.nums, (size_t)g.n, sizeof(double), cmp_dbl);
    c->any = 1;
    c->lo = g.nums[0]; c->hi = g.nums[g.n - 1];
    c->mid = g.n & 1 ? g.nums[g.n / 2] : (g.nums[g.n / 2 - 1] + g.nums[g.n / 2]) / 2;
    for (i = 0; i < g.n; i++) sum += g.nums[i];
    if (r->type == CF_AVERAGE) c->t = sum / g.n;
    else if (r->type == CF_TOP) {
      long k = r->percent ? (long)floor(g.n * (double)r->rank / 100.0) : (long)r->rank;
      if (k < 1) k = 1;
      if (k > g.n) k = g.n;
      c->t = r->bottom ? g.nums[k - 1] : g.nums[g.n - k];
    }
  }
  free(g.nums);
  return c;
}

/* ------------------------------------------------------------------ testing a cell */
/* a rule's formula worked out for the cell tested (its relative references moved there) */
static Value eval_for(Book *b, int sheet, const CondRule *r, const Formula *f, i32 row, i32 col)
{
  Formula *g;
  Value v;
  if (!f) return val_empty();
  if (!r->nareas || (row == r->areas[0].r1 && col == r->areas[0].c1)) return book_eval_at(b, sheet, row, col, f);
  if (!(g = formula_copy(f))) return val_err(E_VALUE);
  formula_offset(g, row - r->areas[0].r1, col - r->areas[0].c1);
  v = book_eval_at(b, sheet, row, col, g);
  formula_free(g);
  return v;
}
/* text in text, ignoring case (ASCII) */
static int has_text(const char *s, const char *t, int where)   /* where: 0 anywhere, 1 at the start, 2 at the end */
{
  size_t ls = strlen(s), lt = strlen(t), i;
  if (lt > ls) return 0;
  if (where == 1) return !strncasecmp(s, t, lt);
  if (where == 2) return !strncasecmp(s + ls - lt, t, lt);
  for (i = 0; i + lt <= ls; i++) if (!strncasecmp(s + i, t, lt)) return 1;
  return 0;
}
static int test(Book *b, int sheet, CondRule *r, i32 row, i32 col, const Value *v)
{
  CondCache *c;
  switch (r->type) {
  case CF_CELL: {
    Value a = eval_for(b, sheet, r, r->f1, row, col), z = val_empty(), cv;
    int k1, k2 = 0, ok = 0;
    if (r->op == CO_BETWEEN || r->op == CO_NOTBETWEEN) z = eval_for(b, sheet, r, r->f2, row, col);
    /* an empty cell counts as 0 against a number, as in Excel */
    cv = v->t == V_EMPTY ? (a.t == V_STR ? val_str("") : val_num(0)) : val_copy(v);
    if (cv.t != V_ERR && a.t != V_ERR && z.t != V_ERR) {
      k1 = val_compare(&cv, &a);
      if (r->op == CO_BETWEEN || r->op == CO_NOTBETWEEN) {
        int in;
        k2 = val_compare(&cv, &z);
        /* either way round: between 10 and 1 is between 1 and 10 */
        in = (k1 >= 0 && k2 <= 0) || (k1 <= 0 && k2 >= 0);
        ok = r->op == CO_BETWEEN ? in : !in;
      } else
        ok = r->op == CO_EQ ? k1 == 0 : r->op == CO_NE ? k1 != 0 : r->op == CO_GT ? k1 > 0 : r->op == CO_LT ? k1 < 0 : r->op == CO_GE ? k1 >= 0 : k1 <= 0;
    }
    val_free(&a); val_free(&z); val_free(&cv);
    return ok;
  }
  case CF_TEXT: case CF_NOTTEXT: case CF_BEGINS: case CF_ENDS: {
    Buf t;
    int ok;
    if (v->t == V_ERR) return 0;
    buf_init(&t);
    to_text(b, v, &t);
    buf_addc(&t, 0);
    ok = has_text(t.data ? t.data : "", r->text ? r->text : "", r->type == CF_BEGINS ? 1 : r->type == CF_ENDS ? 2 : 0);
    buf_free(&t);
    return r->type == CF_NOTTEXT ? !ok : ok;
  }
  case CF_DUP: case CF_UNIQUE: {
    char k[256];
    int i;
    if (v->t == V_EMPTY || (v->t == V_STR && (!v->s || !v->s[0])) || !(c = figures(b, sheet, r))) return 0;
    value_key(b, v, k, sizeof k);
    for (i = 0; i < c->nkeys; i++) if (!strcmp(c->keys[i], k)) return r->type == CF_DUP ? c->counts[i] > 1 : c->counts[i] == 1;
    return 0;
  }
  case CF_TOP:
    if (v->t != V_NUM || !(c = figures(b, sheet, r)) || !c->any) return 0;
    return r->bottom ? v->n <= c->t : v->n >= c->t;
  case CF_AVERAGE:
    if (v->t != V_NUM || !(c = figures(b, sheet, r)) || !c->any) return 0;
    return r->below ? (r->equal ? v->n <= c->t : v->n < c->t) : (r->equal ? v->n >= c->t : v->n > c->t);
  case CF_BLANKS: case CF_NOBLANKS: {
    int blank = v->t == V_EMPTY;
    if (v->t == V_STR) { const char *p = v->s ? v->s : ""; while (*p == ' ') p++; blank = !*p; }
    return r->type == CF_BLANKS ? blank : !blank;
  }
  case CF_ERRORS: return v->t == V_ERR;
  case CF_NOERRORS: return v->t != V_ERR;
  case CF_EXPR: {
    Value a = eval_for(b, sheet, r, r->f1, row, col);
    int ok = 0;
    if (a.t == V_NUM) ok = a.n != 0;
    else if (a.t == V_BOOL) ok = a.e != 0;
    val_free(&a);
    return ok;
  }
  }
  return 0;
}
static u32 mix(u32 a, u32 z, double f)
{
  int k;
  u32 out = 0;
  if (f < 0) f = 0;
  if (f > 1) f = 1;
  f = floor(f * 16 + 0.5) / 16;   /* (sixteen steps: few pens on the Amiga's screen) */
  for (k = 0; k < 24; k += 8) {
    double ca = (a >> k) & 255, cz = (z >> k) & 255;
    out |= (u32)(ca + (cz - ca) * f + 0.5) << k;
  }
  return out;
}
int cond_look(Book *b, int sheet, i32 r, i32 c, CondLook *out)
{
  Sheet *s;
  int i, any = 0;
  const Value *v = NULL;
  memset(out, 0, sizeof *out);
  out->color = COLOR_AUTO; out->bar = -1;
  if (sheet < 0 || sheet >= b->nsh) return 0;
  s = b->sh[sheet];
  for (i = 0; i < s->ncfs; i++) {
    CondRule *rule = &s->cfs[i];
    if (!cond_covers(rule, r, c)) continue;
    if (!v) v = cell_value(b, sheet, r, c);
    if (rule->type == CF_BAR || rule->type == CF_SCALE) {
      CondCache *cc;
      double f;
      if (v->t != V_NUM || !(cc = figures(b, sheet, rule)) || !cc->any) continue;
      if (rule->type == CF_BAR) {
        if (out->bar >= 0) continue;
        /* the shortest bar a tenth of the cell, the longest all of it */
        f = cc->hi > cc->lo ? (v->n - cc->lo) / (cc->hi - cc->lo) : 1;
        out->bar = (int)(100 + f * 900 + 0.5);
        out->barcolor = rule->bar[0];
      } else {
        if (out->fill) continue;
        if (rule->three)
          out->fill = FILL_ON | (v->n <= cc->mid ? mix(rule->bar[0], rule->bar[1], cc->mid > cc->lo ? (v->n - cc->lo) / (cc->mid - cc->lo) : 1)
                                                 : mix(rule->bar[1], rule->bar[2], cc->hi > cc->mid ? (v->n - cc->mid) / (cc->hi - cc->mid) : 1));
        else out->fill = FILL_ON | mix(rule->bar[0], rule->bar[2], cc->hi > cc->lo ? (v->n - cc->lo) / (cc->hi - cc->lo) : 1);
      }
      any = 1;
      continue;
    }
    if (!test(b, sheet, rule, r, c, v)) continue;
    /* the first rule to set a part of the look wins it */
    if (!out->fill && rule->fill) out->fill = rule->fill;
    if (out->color == COLOR_AUTO && rule->color != COLOR_AUTO) out->color = rule->color;
    out->bold |= rule->bold; out->italic |= rule->italic; out->underline |= rule->underline; out->strike |= rule->strike;
    any = 1;
    if (rule->stop) break;
  }
  return any;
}

/* ------------------------------------------------------------------ rows and columns moving */
void cond_shift_areas(Sheet *s, int rows, i32 at, i32 n)
{
  int i, j, k, m;
  for (i = m = 0; i < s->ncfs; i++) {
    CondRule *r = &s->cfs[i];
    cache_free(r);
    for (j = k = 0; j < r->nareas; j++) {
      Rect a = r->areas[j];
      i32 *a1 = rows ? &a.r1 : &a.c1, *a2 = rows ? &a.r2 : &a.c2, lim = rows ? MAXROW : MAXCOL;
      if (n > 0) {
        if (*a1 >= at) *a1 += n;
        if (*a2 >= at && *a2 < lim - 1) *a2 += n;      /* (whole columns stay whole) */
        if (*a1 >= lim) continue;
        if (*a2 >= lim) *a2 = lim - 1;
      } else {
        i32 d = -n;
        if (*a1 >= at && *a2 < at + d) continue;          /* deleted altogether */
        if (*a1 >= at + d) *a1 -= d; else if (*a1 >= at) *a1 = at;
        if (*a2 >= lim - 1) {} else if (*a2 >= at + d) *a2 -= d; else if (*a2 >= at) *a2 = at - 1;
      }
      r->areas[k++] = a;
    }
    r->nareas = k;
    if (!k) { cond_rule_free(r); continue; }
    s->cfs[m++] = *r;
  }
  s->ncfs = m;
}

/* ------------------------------------------------------------------ editing */
/* the sheet as it is noted for undo, then the change */
static int begin(Book *b, int sheet)
{
  if (sheet < 0 || sheet >= b->nsh) return 0;
  book_begin(b);
  book_note_sheet(b, sheet);
  return 1;
}
static void end(Book *b)
{
  book_end(b);
  b->dirty = 1;
}
int book_cond_add(Book *b, int sheet, const CondRule *rule, int at)
{
  Sheet *s;
  CondRule *n;
  if (!begin(b, sheet)) return 0;
  s = b->sh[sheet];
  if (at < 0 || at > s->ncfs) at = s->ncfs;
  if (!(n = (CondRule *)realloc(s->cfs, (size_t)(s->ncfs + 1) * sizeof *n))) { end(b); return 0; }
  s->cfs = n;
  memmove(&s->cfs[at + 1], &s->cfs[at], (size_t)(s->ncfs - at) * sizeof *n);
  if (!cond_rule_copy(&s->cfs[at], rule)) { memmove(&s->cfs[at], &s->cfs[at + 1], (size_t)(s->ncfs - at) * sizeof *n); end(b); return 0; }
  s->ncfs++;
  end(b);
  return 1;
}
int book_cond_replace(Book *b, int sheet, int i, const CondRule *rule)
{
  CondRule c;
  if (sheet < 0 || sheet >= b->nsh || i < 0 || i >= b->sh[sheet]->ncfs || !cond_rule_copy(&c, rule)) return 0;
  begin(b, sheet);
  cond_rule_free(&b->sh[sheet]->cfs[i]);
  b->sh[sheet]->cfs[i] = c;
  end(b);
  return 1;
}
int book_cond_delete(Book *b, int sheet, int i)
{
  Sheet *s;
  if (sheet < 0 || sheet >= b->nsh || i < 0 || i >= b->sh[sheet]->ncfs) return 0;
  begin(b, sheet);
  s = b->sh[sheet];
  cond_rule_free(&s->cfs[i]);
  memmove(&s->cfs[i], &s->cfs[i + 1], (size_t)(s->ncfs - i - 1) * sizeof(CondRule));
  s->ncfs--;
  end(b);
  return 1;
}
int book_cond_move(Book *b, int sheet, int i, int to)
{
  Sheet *s;
  CondRule t;
  if (sheet < 0 || sheet >= b->nsh) return 0;
  s = b->sh[sheet];
  if (i < 0 || i >= s->ncfs || to < 0 || to >= s->ncfs || to == i) return 0;
  begin(b, sheet);
  t = s->cfs[i];
  if (to < i) memmove(&s->cfs[to + 1], &s->cfs[to], (size_t)(i - to) * sizeof t);
  else memmove(&s->cfs[i], &s->cfs[i + 1], (size_t)(to - i) * sizeof t);
  s->cfs[to] = t;
  end(b);
  return 1;
}
/* area a less hole h: up to four rectangles into out; how many */
static int subtract(Rect a, Rect h, Rect *out)
{
  int n = 0;
  Rect m;
  if (h.r2 < a.r1 || h.r1 > a.r2 || h.c2 < a.c1 || h.c1 > a.c2) { out[0] = a; return 1; }
  if (h.r1 > a.r1) { m = a; m.r2 = h.r1 - 1; out[n++] = m; }
  if (h.r2 < a.r2) { m = a; m.r1 = h.r2 + 1; out[n++] = m; }
  m.r1 = h.r1 > a.r1 ? h.r1 : a.r1; m.r2 = h.r2 < a.r2 ? h.r2 : a.r2;
  if (h.c1 > a.c1) { m.c1 = a.c1; m.c2 = h.c1 - 1; out[n++] = m; }
  if (h.c2 < a.c2) { m.c1 = h.c2 + 1; m.c2 = a.c2; out[n++] = m; }
  return n;
}
int book_cond_clear(Book *b, int sheet, const Rect *area)
{
  Sheet *s;
  int i, j, changed = 0;
  if (sheet < 0 || sheet >= b->nsh || !b->sh[sheet]->ncfs) return 0;
  begin(b, sheet);
  s = b->sh[sheet];
  if (!area) { changed = s->ncfs; cond_free_all(s->cfs, s->ncfs); s->cfs = NULL; s->ncfs = 0; end(b); return changed; }
  for (i = 0; i < s->ncfs; ) {
    CondRule *r = &s->cfs[i];
    Rect *na = (Rect *)malloc((size_t)r->nareas * 4 * sizeof(Rect));
    int k = 0, hit = 0;
    if (!na) { i++; continue; }
    for (j = 0; j < r->nareas; j++) {
      int got = subtract(r->areas[j], *area, na + k);
      if (got != 1 || memcmp(&na[k], &r->areas[j], sizeof(Rect))) hit = 1;
      k += got;
    }
    if (!hit) { free(na); i++; continue; }
    changed++;
    free(r->areas); r->areas = na; r->nareas = k;
    cache_free(r);
    if (!k) {
      cond_rule_free(r);
      memmove(&s->cfs[i], &s->cfs[i + 1], (size_t)(s->ncfs - i - 1) * sizeof(CondRule));
      s->ncfs--;
      continue;
    }
    i++;
  }
  end(b);
  return changed;
}

/* ------------------------------------------------------------------ text in and out */
Formula *cond_value(Book *b, int sheet, const char *typed)
{
  double d;
  int kind, pos;
  Buf t;
  Formula *f;
  const char *p;
  while (*typed == ' ') typed++;
  if (!*typed) return NULL;
  if (*typed == '=') return formula_parse(b, sheet, typed + 1, &pos);
  if (parse_number(typed, &d, &kind)) { char n[40]; snprintf(n, sizeof n, "%.15g", d); return formula_parse(b, sheet, n, &pos); }
  /* text: quoted, its quotes doubled */
  buf_init(&t);
  buf_addc(&t, '"');
  for (p = typed; *p; p++) { if (*p == '"') buf_addc(&t, '"'); buf_addc(&t, *p); }
  buf_adds(&t, "\"");
  buf_addc(&t, 0);
  f = t.data ? formula_parse(b, sheet, t.data, &pos) : NULL;
  buf_free(&t);
  return f;
}
void cond_value_text(Book *b, int sheet, const Formula *f, char *out, size_t cap)
{
  Buf t;
  const char *s;
  size_t n;
  out[0] = 0;
  if (!f) return;
  buf_init(&t);
  formula_text(b, sheet, f, &t);
  buf_addc(&t, 0);
  s = t.data ? t.data : "";
  n = strlen(s);
  if (f->n == 1 && (f->t[0].op == T_NUM)) snprintf(out, cap, "%s", s);
  else if (f->n == 1 && f->t[0].op == T_STR && n >= 2) {
    /* a plain string: its quotes off, doubled quotes single again */
    size_t i, o = 0;
    for (i = 1; i + 1 < n && o + 1 < cap; i++) { out[o++] = s[i]; if (s[i] == '"' && s[i + 1] == '"') i++; }
    out[o] = 0;
  } else snprintf(out, cap, "=%s", s);
  buf_free(&t);
}
void cond_describe(Book *b, int sheet, const CondRule *r, char *out, size_t cap)
{
  static const char *const ops[] = { "between", "not between", "=", "<>", ">", "<", ">=", "<=" };
  char v1[120], v2[120];
  cond_value_text(b, sheet, r->f1, v1, sizeof v1);
  cond_value_text(b, sheet, r->f2, v2, sizeof v2);
  switch (r->type) {
  case CF_CELL:
    if (r->op == CO_BETWEEN || r->op == CO_NOTBETWEEN) snprintf(out, cap, "Cell value %s %s and %s", ops[r->op], v1, v2);
    else snprintf(out, cap, "Cell value %s %s", ops[r->op < CO_N ? r->op : CO_EQ], v1);
    break;
  case CF_TEXT: snprintf(out, cap, "Text contains \"%s\"", r->text ? r->text : ""); break;
  case CF_NOTTEXT: snprintf(out, cap, "Text doesn't contain \"%s\"", r->text ? r->text : ""); break;
  case CF_BEGINS: snprintf(out, cap, "Text begins with \"%s\"", r->text ? r->text : ""); break;
  case CF_ENDS: snprintf(out, cap, "Text ends with \"%s\"", r->text ? r->text : ""); break;
  case CF_DUP: snprintf(out, cap, "Duplicate values"); break;
  case CF_UNIQUE: snprintf(out, cap, "Unique values"); break;
  case CF_TOP: snprintf(out, cap, "%s %ld%s", r->bottom ? "Bottom" : "Top", (long)r->rank, r->percent ? "%" : ""); break;
  case CF_AVERAGE: snprintf(out, cap, "%s average", r->below ? (r->equal ? "Equal to or below" : "Below") : (r->equal ? "Equal to or above" : "Above")); break;
  case CF_BLANKS: snprintf(out, cap, "Blank cells"); break;
  case CF_NOBLANKS: snprintf(out, cap, "Cells that aren't blank"); break;
  case CF_ERRORS: snprintf(out, cap, "Cells with errors"); break;
  case CF_NOERRORS: snprintf(out, cap, "Cells without errors"); break;
  case CF_EXPR: snprintf(out, cap, "Formula: %s", v1); break;
  case CF_BAR: snprintf(out, cap, "Data bar"); break;
  case CF_SCALE: snprintf(out, cap, "%d-colour scale", r->three ? 3 : 2); break;
  default: snprintf(out, cap, "?");
  }
}
void cond_areas_text(const CondRule *r, char *out, size_t cap)
{
  int i;
  size_t o = 0;
  out[0] = 0;
  for (i = 0; i < r->nareas && o + 24 < cap; i++) {
    char t[32];
    rect_text(r->areas[i], t);
    o += (size_t)snprintf(out + o, cap - o, "%s%s", i ? " " : "", t);
  }
}
int cond_parse_areas(Book *b, const char *s, CondRule *r)
{
  Rect *a = NULL;
  int n = 0;
  while (*s) {
    u16 sh;
    Rect rc;
    u8 fl;
    int k;
    Rect *na;
    while (*s == ' ' || *s == ',' || *s == ';') s++;
    if (!*s) break;
    if (!(k = parse_ref(b, s, &sh, &rc, &fl))) { free(a); return 0; }
    s += k;
    if (!(na = (Rect *)realloc(a, (size_t)(n + 1) * sizeof(Rect)))) { free(a); return 0; }
    a = na;
    a[n++] = rc;
  }
  if (!n) return 0;
  free(r->areas);
  r->areas = a; r->nareas = n;
  return 1;
}
