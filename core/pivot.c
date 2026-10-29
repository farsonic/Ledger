/*
 * pivot.c - pivot tables (see pivot.h): the definition, working out the report, and writing it.
 *
 * Working out a report: the source's rows (records) that pass the filters are read; each Rows and
 * Columns field's distinct items are collected and sorted; the records' combinations of row items
 * make a tree (outer field's items first, then the inner ones under each), and the column items
 * another. Every record is added into an accumulator for each pair of its row node's and column
 * node's ancestors (the roots being the grand totals), so each cell of the report, subtotals and
 * totals too, is one accumulator. The layout (compact, outline, tabular) then decides where each
 * node's row and label go. Written from GridCraft's documented behaviour, not ported.
 */
#include "pivot.h"
#include "ops.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>
#include <math.h>

#define MAXACC 60000L             /* accumulators (report cells x values) a report may need */
#define MAXOUT 200000L            /* cells a report may have */

/* ------------------------------------------------------------------ memory */
static void field_free(PivotField *f)
{
  int i;
  free(f->name);
  for (i = 0; i < f->nhidden; i++) free(f->hidden[i]);
  free(f->hidden);
  memset(f, 0, sizeof *f);
}
static int field_copy(PivotField *d, const PivotField *s)
{
  int i;
  memset(d, 0, sizeof *d);
  d->sort = s->sort;
  d->name = str_dup(s->name ? s->name : "");
  if (s->nhidden && (d->hidden = (char **)calloc((size_t)s->nhidden, sizeof(char *))))
    for (i = 0; i < s->nhidden; i++) { d->hidden[i] = str_dup(s->hidden[i]); d->nhidden = i + 1; }
  return d->name != NULL;
}
static PivotField *fields_copy(const PivotField *f, int n)
{
  PivotField *c;
  int i;
  if (!n || !(c = (PivotField *)calloc((size_t)n, sizeof *c))) return NULL;
  for (i = 0; i < n; i++) field_copy(&c[i], &f[i]);
  return c;
}
static void fields_free(PivotField *f, int n) { int i; for (i = 0; i < n; i++) field_free(&f[i]); free(f); }
void pivot_free1(LPivot *p)
{
  int i;
  fields_free(p->rows, p->nrows); fields_free(p->cols, p->ncols); fields_free(p->filters, p->nfilters);
  for (i = 0; i < p->nvals; i++) { free(p->vals[i].name); free(p->vals[i].caption); }
  free(p->vals);
  free(p->kind); free(p->level);
  p->rows = p->cols = p->filters = NULL; p->vals = NULL; p->kind = p->level = NULL;
  p->nrows = p->ncols = p->nfilters = p->nvals = p->nkind = 0;
}
int pivot_copy1(LPivot *d, const LPivot *s)
{
  int i;
  *d = *s;
  d->rows = fields_copy(s->rows, s->nrows);
  d->cols = fields_copy(s->cols, s->ncols);
  d->filters = fields_copy(s->filters, s->nfilters);
  d->vals = s->nvals ? (PivotValue *)calloc((size_t)s->nvals, sizeof(PivotValue)) : NULL;
  for (i = 0; i < s->nvals && d->vals; i++) {
    d->vals[i] = s->vals[i];
    d->vals[i].name = str_dup(s->vals[i].name ? s->vals[i].name : "");
    d->vals[i].caption = s->vals[i].caption ? str_dup(s->vals[i].caption) : NULL;
  }
  if (!d->vals) d->nvals = 0;
  if (!d->rows) d->nrows = 0;
  if (!d->cols) d->ncols = 0;
  if (!d->filters) d->nfilters = 0;
  d->kind = d->level = NULL; d->nkind = 0;
  if (s->nkind && (d->kind = (u8 *)malloc((size_t)s->nkind)) && (d->level = (u8 *)malloc((size_t)s->nkind))) {
    memcpy(d->kind, s->kind, (size_t)s->nkind); memcpy(d->level, s->level, (size_t)s->nkind); d->nkind = s->nkind;
  } else { free(d->kind); d->kind = NULL; }
  return 1;
}
void pivots_free(LPivot *p, int n) { int i; for (i = 0; i < n; i++) pivot_free1(&p[i]); free(p); }
LPivot *pivots_copy(const LPivot *p, int n)
{
  LPivot *c;
  int i;
  if (!n || !(c = (LPivot *)calloc((size_t)n, sizeof *c))) return NULL;
  for (i = 0; i < n; i++) pivot_copy1(&c[i], &p[i]);
  return c;
}
void pivots_shift(Sheet *s, int rows, i32 at, i32 n)
{
  int i;
  for (i = 0; i < s->npivots; i++) {
    LPivot *p = &s->pivots[i];
    i32 *a = rows ? &p->r : &p->c;
    i32 d = n > 0 ? 0 : -n;
    if (n > 0) { if (*a >= at) *a += n; }
    else if (*a >= at + d) *a -= d;
    else if (*a >= at) *a = at;
    if (p->out.r1 >= 0) {
      /* the old report moves with its cells */
      i32 *o1 = rows ? &p->out.r1 : &p->out.c1, *o2 = rows ? &p->out.r2 : &p->out.c2, *b1 = rows ? &p->bodyrow : NULL;
      if (n > 0) { if (*o1 >= at) { *o1 += n; if (b1) *b1 += n; if (rows) p->datarow += n; } if (*o2 >= at) *o2 += n; if (!rows) { if (p->datacol >= at) p->datacol += n; if (p->gtcol >= at) p->gtcol += n; } }
      else { p->out.r1 = -1; }    /* (a deletion: the look is dropped until the next refresh) */
    }
  }
}

/* ------------------------------------------------------------------ finding */
LPivot *pivot_at(Book *b, int sheet, i32 r, i32 c)
{
  Sheet *s;
  int i;
  if (sheet < 0 || sheet >= b->nsh) return NULL;
  s = b->sh[sheet];
  for (i = 0; i < s->npivots; i++) {
    LPivot *p = &s->pivots[i];
    if (p->out.r1 >= 0 && r >= p->out.r1 && r <= p->out.r2 && c >= p->out.c1 && c <= p->out.c2) return p;
    if (r == p->r && c == p->c) return p;
  }
  return NULL;
}
int pivot_index(Book *b, int sheet, const LPivot *p)
{
  if (sheet < 0 || sheet >= b->nsh || !p) return -1;
  if (p < b->sh[sheet]->pivots || p >= b->sh[sheet]->pivots + b->sh[sheet]->npivots) return -1;
  return (int)(p - b->sh[sheet]->pivots);
}
void pivot_unique_name(Book *b, char *out)
{
  int k, s, i, clash;
  for (k = 1; k < 100000; k++) {
    sprintf(out, "PivotTable%d", k);
    clash = 0;
    for (s = 0; s < b->nsh; s++) for (i = 0; i < b->sh[s]->npivots; i++) if (str_ieq(b->sh[s]->pivots[i].name, out)) clash = 1;
    if (!clash) return;
  }
}
int pivot_source(Book *b, const LPivot *p, int *sheet, Rect *r)
{
  const char *t = p->source;
  LTable *tb;
  u16 sh;
  u8 fl;
  int n;
  while (*t == ' ' || *t == '=') t++;
  if (!*t) return 0;
  if ((tb = table_find(b, t, sheet))) {
    if (!tb->header) return 0;
    *r = tb->r;
    r->r2 -= tb->totals;
    return 1;
  }
  n = parse_ref(b, t, &sh, r, &fl);
  if (n > 0 && !t[n]) {
    *sheet = sh == SHEET_SELF ? b->active : sh;
    if (*sheet < 0 || *sheet >= b->nsh) return 0;
    if (r->r2 >= MAXROW - 1 || (fl & RF_COLS)) { i32 lr = sheet_last_row(b->sh[*sheet]); if (lr < r->r1) return 0; r->r2 = lr; }
    if (r->c2 >= MAXCOL - 1 || (fl & RF_ROWS)) { i32 lc = sheet_last_col(b->sh[*sheet]); if (lc < r->c1) return 0; r->c2 = lc; }
    return r->c2 - r->c1 < 1000;
  }
  {
    /* a defined name standing for a range */
    int i;
    for (i = 0; i < b->nnames; i++)
      if (str_ieq(b->names[i].name, t) && b->names[i].f && b->names[i].f->n == 1 && (b->names[i].f->t[0].op == T_AREA || b->names[i].f->t[0].op == T_REF)) {
        const Tok *k = &b->names[i].f->t[0];
        *sheet = k->a == SHEET_SELF ? b->active : k->a;
        r->r1 = k->r1; r->c1 = k->c1; r->r2 = k->op == T_REF ? k->r1 : k->r2; r->c2 = k->op == T_REF ? k->c1 : k->c2;
        return *sheet < b->nsh;
      }
  }
  return 0;
}
void pivot_strings_free(char **v, int n) { int i; for (i = 0; i < n; i++) free(v[i]); free(v); }
/* the source's headings (unique; empty ones "ColumnN") */
static int source_fields(Book *b, int sheet, Rect src, char ***out)
{
  int n = src.c2 - src.c1 + 1, k, j;
  char **v = (char **)calloc((size_t)n + 1, sizeof(char *));
  *out = v;
  if (!v) return 0;
  for (k = 0; k < n; k++) {
    char h[256], nm[280];
    u32 col;
    int clash, suf = 2;
    book_shown_text(b, sheet, src.r1, src.c1 + k, h, sizeof h, &col);
    { char *e = h + strlen(h); while (e > h && e[-1] == ' ') *--e = 0; }
    if (!h[0]) sprintf(h, "Column%d", k + 1);
    strcpy(nm, h);
    do {
      clash = 0;
      for (j = 0; j < k; j++) if (str_ieq(v[j], nm)) clash = 1;
      if (clash) sprintf(nm, "%s%d", h, suf++);
    } while (clash);
    v[k] = str_dup(nm);
  }
  return n;
}
int pivot_fields(Book *b, const LPivot *p, char ***out)
{
  int sheet;
  Rect src;
  *out = NULL;
  if (!pivot_source(b, p, &sheet, &src)) return 0;
  return source_fields(b, sheet, src, out);
}
static const char *const funcnames[PF_N] = { "Sum", "Count", "Average", "Max", "Min", "Product", "Count", "StdDev", "StdDevp", "Var", "Varp" };
const char *pivot_func_name(int f) { return f >= 0 && f < PF_N ? funcnames[f] : "Sum"; }
void pivot_caption(const PivotValue *v, char *out, size_t cap)
{
  if (v->caption && v->caption[0]) snprintf(out, cap, "%s", v->caption);
  else snprintf(out, cap, "%s of %s", pivot_func_name(v->func), v->name ? v->name : "");
}

/* ------------------------------------------------------------------ items */
/* an item's label: how the report shows it and how filters name it */
void pivot_item_label(Book *b, const Value *v, char *out, size_t cap)
{
  Buf t;
  if (v->t == V_EMPTY || (v->t == V_STR && !v->s[0])) { snprintf(out, cap, "(blank)"); return; }
  buf_init(&t);
  if (v->t == V_NUM) { char g[40]; num_general(v->n, g, 15); buf_adds(&t, g); }
  else to_text(b, v, &t);
  snprintf(out, cap, "%s", t.data ? t.data : "");
  buf_free(&t);
}
typedef struct { Value v; char *label; u16 fmt; int rank; } Item;
typedef struct {
  int col;                        /* source column offset */
  Item *it; int n, cap;
  int *hash; int hcap;            /* open addressing on the lower-case label: item index + 1 */
  u8 sort;
} FItems;
static u32 lhash(const char *s) { u32 h = 5381; for (; *s; s++) h = h * 33 + (u32)tolower((u8)*s); return h; }
static int fitems_find(FItems *f, const char *label, int add, const Value *v, u16 fmt)
{
  u32 h;
  int k;
  if (f->n * 2 + 2 > f->hcap) {
    int nc = f->hcap ? f->hcap * 2 : 64, i;
    int *nh = (int *)calloc((size_t)nc, sizeof(int));
    if (!nh) return -1;
    for (i = 0; i < f->n; i++) { u32 hh = lhash(f->it[i].label) % (u32)nc; while (nh[hh]) hh = (hh + 1) % (u32)nc; nh[hh] = i + 1; }
    free(f->hash); f->hash = nh; f->hcap = nc;
  }
  h = lhash(label) % (u32)f->hcap;
  while ((k = f->hash[h])) { if (str_ieq(f->it[k - 1].label, label)) return k - 1; h = (h + 1) % (u32)f->hcap; }
  if (!add) return -1;
  if (f->n == f->cap) {
    int nc = f->cap ? f->cap * 2 : 32;
    Item *ni = (Item *)realloc(f->it, (size_t)nc * sizeof(Item));
    if (!ni) return -1;
    f->it = ni; f->cap = nc;
  }
  f->it[f->n].v = val_copy(v);
  f->it[f->n].label = str_dup(label);
  f->it[f->n].fmt = fmt;
  f->it[f->n].rank = f->n;
  f->hash[h] = f->n + 1;
  return f->n++;
}
static void fitems_free(FItems *f)
{
  int i;
  for (i = 0; i < f->n; i++) { val_free(&f->it[i].v); free(f->it[i].label); }
  free(f->it); free(f->hash);
}
/* items in report order: numbers, then text, then TRUE/FALSE, blanks last */
static FItems *g_sortf;
static int item_cmp(const void *a, const void *b)
{
  const Item *x = &g_sortf->it[*(const int *)a], *y = &g_sortf->it[*(const int *)b];
  int ex = x->v.t == V_EMPTY || (x->v.t == V_STR && !x->v.s[0]), ey = y->v.t == V_EMPTY || (y->v.t == V_STR && !y->v.s[0]), c;
  if (ex || ey) return ex - ey;
  if (x->v.t == V_ERR || y->v.t == V_ERR) return (x->v.t == V_ERR) - (y->v.t == V_ERR);
  c = val_compare(&x->v, &y->v);
  if (g_sortf->sort == PO_DESC) c = -c;
  return c ? c : (*(const int *)a - *(const int *)b);
}
static void fitems_rank(FItems *f)
{
  int *ord, i;
  if (f->sort == PO_SOURCE || !f->n) return;
  if (!(ord = (int *)malloc((size_t)f->n * sizeof(int)))) return;
  for (i = 0; i < f->n; i++) ord[i] = i;
  g_sortf = f;
  qsort(ord, (size_t)f->n, sizeof(int), item_cmp);
  for (i = 0; i < f->n; i++) f->it[ord[i]].rank = i;
  free(ord);
}
static int col_of(char **names, int n, const char *name)
{
  int i;
  for (i = 0; i < n; i++) if (name && str_ieq(names[i], name)) return i;
  return -1;
}
static int hidden_has(const PivotField *f, const char *label)
{
  int i;
  for (i = 0; i < f->nhidden; i++) if (str_ieq(f->hidden[i], label)) return 1;
  return 0;
}
int pivot_items(Book *b, const LPivot *p, const char *field, char ***out)
{
  int sheet, n, k, i;
  Rect src;
  char **names;
  FItems f;
  i32 r;
  *out = NULL;
  if (!pivot_source(b, p, &sheet, &src)) return 0;
  n = source_fields(b, sheet, src, &names);
  k = col_of(names, n, field);
  pivot_strings_free(names, n);
  if (k < 0) return 0;
  memset(&f, 0, sizeof f);
  for (r = src.r1 + 1; r <= src.r2; r++) {
    char lab[256];
    const Value *v = cell_value(b, sheet, r, src.c1 + k);
    pivot_item_label(b, v, lab, sizeof lab);
    fitems_find(&f, lab, 1, v, 0);
  }
  fitems_rank(&f);
  if ((*out = (char **)calloc((size_t)f.n + 1, sizeof(char *))))
    for (i = 0; i < f.n; i++) (*out)[f.it[i].rank] = str_dup(f.it[i].label);
  n = f.n;
  fitems_free(&f);
  return n;
}

/* ------------------------------------------------------------------ the report */
typedef struct { double sum, prod, mn, mx, ss; long n, na; } Acc;
typedef struct { int depth, parent, item; int first, last; } Node;   /* first/last: its range of leaves */
typedef struct { Node *n; int nn, cap; int *leafnode; int nleaves; } Tree;
typedef struct { Value v; u16 fmt; u8 indent; } OCell;
typedef struct {
  Book *b;
  LPivot *p;
  int ssheet; Rect src;
  int R, C, V;
  FItems *rf, *cf;                /* the Rows and Columns fields' items */
  int *vcol;                      /* each value field's source column */
  int nrec; int *rrank, *crank;   /* each record's items (ranks), R and C per record */
  i32 *recrow;                    /* each record's source row */
  Tree rt, ct;
  int *recrl, *reccl;             /* each record's row and column leaf */
  Acc *acc;
} Work;

static int tree_add(Tree *t, int depth, int parent, int item)
{
  if (t->nn == t->cap) {
    int nc = t->cap ? t->cap * 2 : 64;
    Node *nn = (Node *)realloc(t->n, (size_t)nc * sizeof(Node));
    if (!nn) return -1;
    t->n = nn; t->cap = nc;
  }
  t->n[t->nn].depth = depth; t->n[t->nn].parent = parent; t->n[t->nn].item = item;
  t->n[t->nn].first = t->n[t->nn].last = -1;
  return t->nn++;
}
static const int *g_keys; static int g_klen;
static int key_cmp(const void *a, const void *b)
{
  const int *x = g_keys + (long)*(const int *)a * g_klen, *y = g_keys + (long)*(const int *)b * g_klen;
  int i;
  for (i = 0; i < g_klen; i++) if (x[i] != y[i]) return x[i] < y[i] ? -1 : 1;
  return *(const int *)a - *(const int *)b;
}
/* the tree of the records' item combinations (keys: klen ranks per record); each record's leaf node in recleaf */
static int build_tree(Tree *t, const int *keys, int klen, int nrec, int *recleaf)
{
  int *ord, i, d, *path, prev = -1;
  memset(t, 0, sizeof *t);
  if (tree_add(t, 0, -1, -1) < 0) return 0;
  if (!klen) { for (i = 0; i < nrec; i++) recleaf[i] = 0; t->n[0].first = 0; return 1; }
  if (!(ord = (int *)malloc(((size_t)nrec + 1) * sizeof(int))) || !(path = (int *)malloc(((size_t)klen + 1) * sizeof(int)))) { free(ord); return 0; }
  for (i = 0; i < nrec; i++) ord[i] = i;
  g_keys = keys; g_klen = klen;
  qsort(ord, (size_t)nrec, sizeof(int), key_cmp);
  for (i = 0; i < nrec; i++) {
    const int *k = keys + (long)ord[i] * klen;
    int from = 0;
    if (prev >= 0) {
      const int *pk = keys + (long)prev * klen;
      while (from < klen && pk[from] == k[from]) from++;
    }
    for (d = from; d < klen; d++) {
      int nd = tree_add(t, d + 1, d ? path[d - 1] : 0, k[d]);
      if (nd < 0) { free(ord); free(path); return 0; }
      path[d] = nd;
    }
    recleaf[ord[i]] = path[klen - 1];
    prev = ord[i];
  }
  free(ord); free(path);
  return 1;
}
static void acc_add(Acc *a, const Value *v)
{
  if (v->t == V_EMPTY || (v->t == V_STR && !v->s[0])) return;
  if (!a->na) { a->prod = 1; a->mn = HUGE_VAL; a->mx = -HUGE_VAL; }
  a->na++;
  if (v->t == V_NUM) {
    double x = v->n;
    if (!a->n) { a->prod = 1; a->mn = HUGE_VAL; a->mx = -HUGE_VAL; }
    a->n++; a->sum += x; a->ss += x * x; a->prod *= x;
    if (x < a->mn) a->mn = x;
    if (x > a->mx) a->mx = x;
  }
}
/* an accumulator's result for func: V_EMPTY when nothing was added */
static Value acc_value(const Acc *a, int func)
{
  double var;
  if (!a->na) return val_empty();
  switch (func) {
  case PF_COUNT: return val_num((double)a->na);
  case PF_COUNTNUMS: return val_num((double)a->n);
  case PF_AVERAGE: return a->n ? num_or_err(a->sum / a->n) : val_err(E_DIV0);
  case PF_MAX: return val_num(a->n ? a->mx : 0);
  case PF_MIN: return val_num(a->n ? a->mn : 0);
  case PF_PRODUCT: return a->n ? num_or_err(a->prod) : val_num(0);
  case PF_STDDEV: case PF_VAR:
    if (a->n < 2) return val_err(E_DIV0);
    var = (a->ss - a->sum * a->sum / a->n) / (a->n - 1);
    if (var < 0) var = 0;
    return num_or_err(func == PF_VAR ? var : sqrt(var));
  case PF_STDDEVP: case PF_VARP:
    if (a->n < 1) return val_err(E_DIV0);
    var = (a->ss - a->sum * a->sum / a->n) / a->n;
    if (var < 0) var = 0;
    return num_or_err(func == PF_VARP ? var : sqrt(var));
  default: return val_num(a->sum);
  }
}
static Acc *acc_at(Work *w, int rn, int cn, int v) { return &w->acc[((long)rn * w->ct.nn + cn) * w->V + v]; }
static void work_free(Work *w)
{
  int i;
  for (i = 0; w->rf && i < w->R; i++) fitems_free(&w->rf[i]);
  for (i = 0; w->cf && i < w->C; i++) fitems_free(&w->cf[i]);
  free(w->rf); free(w->cf); free(w->vcol); free(w->rrank); free(w->crank); free(w->recrow);
  free(w->rt.n); free(w->ct.n); free(w->recrl); free(w->reccl); free(w->acc);
}
/* the records, items, trees and accumulators; 0 with *why on failure */
static int work_out(Work *w, const char **why)
{
  Book *b = w->b;
  LPivot *p = w->p;
  char **names;
  int nf, i, k, *fcols = NULL, *rowcols = NULL, *colcols = NULL, cap = 0;
  i32 r;
  long need;
  if (!pivot_source(b, p, &w->ssheet, &w->src)) { *why = "The source of the PivotTable isn't a range or table any more."; return 0; }
  nf = source_fields(b, w->ssheet, w->src, &names);
  /* the fields used, by column; missing ones are left out */
  rowcols = (int *)malloc(((size_t)p->nrows + 1) * sizeof(int));
  colcols = (int *)malloc(((size_t)p->ncols + 1) * sizeof(int));
  fcols = (int *)malloc(((size_t)p->nfilters + 1) * sizeof(int));
  w->vcol = (int *)malloc(((size_t)p->nvals + 1) * sizeof(int));
  if (!rowcols || !colcols || !fcols || !w->vcol) { free(rowcols); free(colcols); free(fcols); pivot_strings_free(names, nf); *why = "Out of memory."; return 0; }
  w->R = w->C = w->V = 0;
  for (i = 0; i < p->nrows; i++) rowcols[i] = col_of(names, nf, p->rows[i].name);
  for (i = 0; i < p->ncols; i++) colcols[i] = col_of(names, nf, p->cols[i].name);
  for (i = 0; i < p->nfilters; i++) fcols[i] = col_of(names, nf, p->filters[i].name);
  for (i = 0; i < p->nvals; i++) w->vcol[i] = col_of(names, nf, p->vals[i].name);
  pivot_strings_free(names, nf);
  w->R = p->nrows; w->C = p->ncols; w->V = p->nvals;
  w->rf = (FItems *)calloc((size_t)w->R + 1, sizeof(FItems));
  w->cf = (FItems *)calloc((size_t)w->C + 1, sizeof(FItems));
  if (!w->rf || !w->cf) goto oom;
  for (i = 0; i < w->R; i++) { w->rf[i].col = rowcols[i]; w->rf[i].sort = p->rows[i].sort; }
  for (i = 0; i < w->C; i++) { w->cf[i].col = colcols[i]; w->cf[i].sort = p->cols[i].sort; }
  /* the records that pass */
  for (r = w->src.r1 + 1; r <= w->src.r2; r++) {
    int pass = 1, any = 0;
    i32 c;
    char lab[256];
    for (c = w->src.c1; c <= w->src.c2 && !any; c++) if (cell_value(b, w->ssheet, r, c)->t != V_EMPTY) any = 1;
    if (!any) continue;
    for (i = 0; i < p->nfilters && pass; i++) {
      if (fcols[i] < 0 || !p->filters[i].nhidden) continue;
      pivot_item_label(b, cell_value(b, w->ssheet, r, w->src.c1 + fcols[i]), lab, sizeof lab);
      if (hidden_has(&p->filters[i], lab)) pass = 0;
    }
    for (i = 0; i < w->R && pass; i++) if (rowcols[i] >= 0 && p->rows[i].nhidden) { pivot_item_label(b, cell_value(b, w->ssheet, r, w->src.c1 + rowcols[i]), lab, sizeof lab); if (hidden_has(&p->rows[i], lab)) pass = 0; }
    for (i = 0; i < w->C && pass; i++) if (colcols[i] >= 0 && p->cols[i].nhidden) { pivot_item_label(b, cell_value(b, w->ssheet, r, w->src.c1 + colcols[i]), lab, sizeof lab); if (hidden_has(&p->cols[i], lab)) pass = 0; }
    if (!pass) continue;
    if (w->nrec == cap) {
      int nc = cap ? cap * 2 : 256;
      int *nr = (int *)realloc(w->rrank, ((size_t)nc * (w->R ? w->R : 1)) * sizeof(int));
      int *ncr;
      i32 *nrow;
      if (!nr) goto oom;
      w->rrank = nr;
      if (!(ncr = (int *)realloc(w->crank, ((size_t)nc * (w->C ? w->C : 1)) * sizeof(int)))) goto oom;
      w->crank = ncr;
      if (!(nrow = (i32 *)realloc(w->recrow, (size_t)nc * sizeof(i32)))) goto oom;
      w->recrow = nrow;
      cap = nc;
    }
    for (k = 0; k < w->R + w->C; k++) {
      FItems *f = k < w->R ? &w->rf[k] : &w->cf[k - w->R];
      const Value *v;
      Cell *cell;
      int idx;
      Value e = val_empty();
      v = f->col >= 0 ? cell_value(b, w->ssheet, r, w->src.c1 + f->col) : &e;
      pivot_item_label(b, v, lab, sizeof lab);
      cell = f->col >= 0 ? sheet_cell(b->sh[w->ssheet], r, w->src.c1 + f->col) : NULL;
      idx = fitems_find(f, lab, 1, v, cell ? b->styles[cell->style].numfmt : 0);
      if (idx < 0) goto oom;
      if (k < w->R) w->rrank[(long)w->nrec * w->R + k] = idx; else w->crank[(long)w->nrec * w->C + (k - w->R)] = idx;
    }
    w->recrow[w->nrec++] = r;
  }
  free(rowcols); free(colcols); free(fcols); rowcols = colcols = fcols = NULL;
  /* item indices to ranks */
  for (i = 0; i < w->R; i++) fitems_rank(&w->rf[i]);
  for (i = 0; i < w->C; i++) fitems_rank(&w->cf[i]);
  for (k = 0; k < w->nrec; k++) {
    for (i = 0; i < w->R; i++) w->rrank[(long)k * w->R + i] = w->rf[i].it[w->rrank[(long)k * w->R + i]].rank;
    for (i = 0; i < w->C; i++) w->crank[(long)k * w->C + i] = w->cf[i].it[w->crank[(long)k * w->C + i]].rank;
  }
  w->recrl = (int *)malloc(((size_t)w->nrec + 1) * sizeof(int));
  w->reccl = (int *)malloc(((size_t)w->nrec + 1) * sizeof(int));
  if (!w->recrl || !w->reccl) goto oom;
  if (!build_tree(&w->rt, w->rrank, w->R, w->nrec, w->recrl) || !build_tree(&w->ct, w->crank, w->C, w->nrec, w->reccl)) goto oom;
  need = (long)w->rt.nn * w->ct.nn * (w->V ? w->V : 1);
  if (need > MAXACC) { *why = "The PivotTable would be too big: use fewer items in Rows and Columns."; return 0; }
  if (!(w->acc = (Acc *)calloc((size_t)need, sizeof(Acc)))) goto oom;
  for (k = 0; k < w->nrec; k++) {
    int v;
    for (v = 0; v < w->V; v++) {
      const Value *x;
      Value e = val_empty();
      int rn, cn;
      x = w->vcol[v] >= 0 ? cell_value(b, w->ssheet, w->recrow[k], w->src.c1 + w->vcol[v]) : &e;
      for (rn = w->recrl[k]; rn >= 0; rn = w->rt.n[rn].parent)
        for (cn = w->reccl[k]; cn >= 0; cn = w->ct.n[cn].parent) acc_add(acc_at(w, rn, cn, v), x);
    }
  }
  return 1;
oom:
  free(rowcols); free(colcols); free(fcols);
  *why = "Out of memory.";
  return 0;
}
/* a node's label value (its item's value) and format */
static const Item *node_item(Work *w, Tree *t, FItems *f, int node)
{
  int d = t->n[node].depth, i;
  FItems *fi;
  if (d < 1) return NULL;
  fi = &f[d - 1];
  for (i = 0; i < fi->n; i++) if (fi->it[i].rank == t->n[node].item) return &fi->it[i];
  (void)w;
  return NULL;
}
/* children of a node in order (preorder means a node's children follow it, each with its subtree) */
static int next_sibling(Tree *t, int node)
{
  int d = t->n[node].depth, k;
  for (k = node + 1; k < t->nn; k++) { if (t->n[k].depth < d) return -1; if (t->n[k].depth == d) return t->n[k].parent == t->n[node].parent ? k : -1; }
  return -1;
}
static int first_child(Tree *t, int node) { return node + 1 < t->nn && t->n[node + 1].parent == node ? node + 1 : -1; }

typedef struct { int node, v; u8 kind; } ColE;     /* a data column: column node, value field, 0 item / 1 subtotal / 2 grand */
typedef struct { int node; u8 kind; } RowE;        /* a report row: row node, PK_ */
typedef struct { ColE *c; int nc, capc; RowE *r; int nr, capr; } Lay;
static int addcol(Lay *l, int node, int v, int kind)
{
  if (l->nc == l->capc) { int nc = l->capc ? l->capc * 2 : 32; ColE *n = (ColE *)realloc(l->c, (size_t)nc * sizeof(ColE)); if (!n) return 0; l->c = n; l->capc = nc; }
  l->c[l->nc].node = node; l->c[l->nc].v = v; l->c[l->nc].kind = (u8)kind; l->nc++;
  return 1;
}
static int addrow(Lay *l, int node, int kind)
{
  if (l->nr == l->capr) { int nc = l->capr ? l->capr * 2 : 64; RowE *n = (RowE *)realloc(l->r, (size_t)nc * sizeof(RowE)); if (!n) return 0; l->r = n; l->capr = nc; }
  l->r[l->nr].node = node; l->r[l->nr].kind = (u8)kind; l->nr++;
  return 1;
}
static void col_entries(Work *w, Lay *l, int node)
{
  int ch, v, nv = w->V ? w->V : 1;
  if (w->ct.n[node].depth == w->C) { for (v = 0; v < nv; v++) addcol(l, node, w->V ? v : -1, 0); return; }
  for (ch = first_child(&w->ct, node); ch >= 0; ch = next_sibling(&w->ct, ch)) col_entries(w, l, ch);
  if (node && w->p->subtotals && w->C >= 2) for (v = 0; v < nv; v++) addcol(l, node, w->V ? v : -1, 1);
}
static void row_entries(Work *w, Lay *l, int node)
{
  LPivot *p = w->p;
  int ch, d = w->rt.n[node].depth;
  if (d == w->R) { addrow(l, node, PK_DATA); return; }
  if (node) {
    if (p->layout != PL_TABULAR) addrow(l, node, p->subtotals && p->subtop ? PK_SUB : PK_ITEM);
  }
  for (ch = first_child(&w->rt, node); ch >= 0; ch = next_sibling(&w->rt, ch)) row_entries(w, l, ch);
  if (node && p->subtotals && (p->layout == PL_TABULAR || !p->subtop)) addrow(l, node, PK_SUB);
  if (node && d == 1 && p->blankrows) addrow(l, node, PK_BLANK);
}
/* the value shown for row node rn, column entry ce */
static Value cell_of(Work *w, int rn, const ColE *ce)
{
  const PivotValue *pv;
  Value v, d;
  double den;
  int cn = ce->node;
  if (ce->v < 0) return val_empty();
  pv = &w->p->vals[ce->v];
  v = acc_value(acc_at(w, rn, cn, ce->v), pv->func);
  if (pv->showas == PS_NORMAL || v.t != V_NUM) return v;
  switch (pv->showas) {
  case PS_PCT_GRAND: d = acc_value(acc_at(w, 0, 0, ce->v), pv->func); break;
  case PS_PCT_COL: d = acc_value(acc_at(w, 0, cn, ce->v), pv->func); break;
  default: d = acc_value(acc_at(w, rn, 0, ce->v), pv->func); break;
  }
  if (d.t != V_NUM) { val_free(&d); return val_err(E_DIV0); }
  den = d.n;
  if (den == 0) return val_err(E_DIV0);
  return val_num(v.n / den);
}
/* the report in a grid of cells: rows x cols, with each row's kind and level */
typedef struct { OCell *cell; int rows, cols; u8 *kind, *level; int bodyrow, datarow, datacol, gtcol; } Out;
static OCell *oc(Out *o, int r, int c) { return &o->cell[(long)r * o->cols + c]; }
static void oset_str(Out *o, int r, int c, const char *s) { if (r < o->rows && c < o->cols) { val_free(&oc(o, r, c)->v); oc(o, r, c)->v = val_str(s); } }
static void oset_item(Out *o, int r, int c, const Item *it, int indent)
{
  OCell *x;
  if (!it || r >= o->rows || c >= o->cols) return;
  x = oc(o, r, c);
  val_free(&x->v);
  x->v = it->v.t == V_EMPTY || (it->v.t == V_STR && !it->v.s[0]) ? val_str("(blank)") : val_copy(&it->v);
  x->fmt = it->fmt;
  x->indent = (u8)indent;
}
static void out_free(Out *o)
{
  long i;
  if (o->cell) for (i = 0; i < (long)o->rows * o->cols; i++) val_free(&o->cell[i].v);
  free(o->cell); free(o->kind); free(o->level);
  memset(o, 0, sizeof *o);
}
static int lay_out(Work *w, Out *o, const char **why)
{
  LPivot *p = w->p;
  Lay l;
  int L, H, nfr, i, k, r;
  u8 *started;
  char cap[300];
  memset(&l, 0, sizeof l);
  memset(o, 0, sizeof *o);
  /* columns */
  if (w->C) {
    col_entries(w, &l, 0);
    if (p->grandrows) { int v; for (v = 0; v < (w->V ? w->V : 1); v++) addcol(&l, 0, w->V ? v : -1, 2); }
  } else { int v; for (v = 0; v < w->V; v++) addcol(&l, 0, v, 0); }
  /* rows */
  if (w->R) {
    row_entries(w, &l, 0);
    /* (a trailing blank row isn't wanted) */
    if (l.nr && l.r[l.nr - 1].kind == PK_BLANK) l.nr--;
    if (p->grandcols) addrow(&l, 0, PK_GRAND);
  } else if (w->V || w->C) addrow(&l, 0, PK_DATA);
  L = (w->R || w->C) ? (p->layout == PL_COMPACT ? 1 : (w->R ? w->R : 1)) : 0;
  H = w->C ? 1 + w->C + (w->V > 1) : 1;
  if (!w->R && !w->C && !w->V) { H = 1; L = 1; }
  nfr = p->nfilters ? p->nfilters + 1 : 0;
  o->rows = nfr + H + l.nr;
  o->cols = L + l.nc;
  if (o->cols < (p->nfilters ? 2 : 1)) o->cols = p->nfilters ? 2 : 1;
  if ((long)o->rows * o->cols > MAXOUT) { free(l.c); free(l.r); *why = "The PivotTable would be too big: use fewer items in Rows and Columns."; return 0; }
  o->cell = (OCell *)calloc((size_t)o->rows * o->cols, sizeof(OCell));
  o->kind = (u8 *)calloc((size_t)o->rows + 1, 1);
  o->level = (u8 *)calloc((size_t)o->rows + 1, 1);
  started = (u8 *)calloc((size_t)w->rt.nn + 1, 1);
  if (!o->cell || !o->kind || !o->level || !started) { free(l.c); free(l.r); free(started); out_free(o); *why = "Out of memory."; return 0; }
  o->bodyrow = nfr; o->datarow = nfr + H; o->datacol = L; o->gtcol = -1;
  /* report filters: name, then (All), the one item shown, or (Multiple Items) */
  for (i = 0; i < p->nfilters; i++) {
    char **items;
    int n = pivot_items(w->b, p, p->filters[i].name, &items), shown = 0, last = -1;
    for (k = 0; k < n; k++) if (!hidden_has(&p->filters[i], items[k])) { shown++; last = k; }
    oset_str(o, i, 0, p->filters[i].name);
    oset_str(o, i, 1, !p->filters[i].nhidden ? "(All)" : shown == 1 ? items[last] : "(Multiple Items)");
    o->kind[i] = PK_FILTER;
    pivot_strings_free(items, n);
  }
  if (nfr) o->kind[nfr - 1] = PK_BLANK;
  for (r = 0; r < H; r++) o->kind[nfr + r] = PK_HEAD;
  /* headers */
  {
    int hr = nfr + H - 1;                        /* the row-label header row */
    if (w->C) {
      if (w->V == 1) { pivot_caption(&p->vals[0], cap, sizeof cap); oset_str(o, nfr, 0, cap); }
      if (p->colheaders || 1) oset_str(o, nfr, L, p->layout == PL_COMPACT ? "Column Labels" : p->cols[0].name);
      for (k = 0; k < l.nc; k++) {
        ColE *ce = &l.c[k];
        int node = ce->node, lv;
        if (ce->kind == 2) {
          if (w->V > 1) { char t2[320]; pivot_caption(&p->vals[ce->v], cap, sizeof cap); snprintf(t2, sizeof t2, "Total %s", cap); oset_str(o, nfr + 1 + w->C, L + k, t2); }
          else oset_str(o, nfr + 1, L + k, "Grand Total");
          if (o->gtcol < 0) o->gtcol = L + k;
          continue;
        }
        if (ce->kind == 1) {
          const Item *it = node_item(w, &w->ct, w->cf, node);
          char t2[320];
          snprintf(t2, sizeof t2, "%s Total", it ? it->label : "");
          if (w->V > 1 && (k == 0 || l.c[k - 1].node != node || l.c[k - 1].kind != 1)) oset_str(o, nfr + w->ct.n[node].depth, L + k, t2);
          else if (w->V <= 1) oset_str(o, nfr + w->ct.n[node].depth, L + k, t2);
        } else {
          /* each level's label where its span starts */
          int chain[64], nch = 0, nd;
          for (nd = node; nd > 0 && nch < 64; nd = w->ct.n[nd].parent) chain[nch++] = nd;
          for (lv = nch - 1; lv >= 0; lv--) {
            int an = chain[lv], start = 1;
            if (k > 0) {
              int pn;
              for (pn = l.c[k - 1].node; pn > 0 && w->ct.n[pn].depth > w->ct.n[an].depth; pn = w->ct.n[pn].parent) ;
              if (l.c[k - 1].kind == 0 && pn == an) start = 0;
            }
            if (start) oset_item(o, nfr + w->ct.n[an].depth, L + k, node_item(w, &w->ct, w->cf, an), 0);
          }
        }
        if (w->V > 1) { pivot_caption(&p->vals[ce->v], cap, sizeof cap); oset_str(o, nfr + 1 + w->C, L + k, cap); }
      }
    } else {
      for (k = 0; k < l.nc; k++) { pivot_caption(&p->vals[l.c[k].v], cap, sizeof cap); oset_str(o, hr, L + k, cap); }
    }
    if (L) {
      if (p->layout == PL_COMPACT) { if (w->R) oset_str(o, hr, 0, "Row Labels"); }
      else for (k = 0; k < w->R; k++) oset_str(o, hr, k, p->rows[k].name);
    }
    if (!w->R && !w->C && !w->V) oset_str(o, hr, 0, "PivotTable");
  }
  /* body */
  for (i = 0; i < l.nr; i++) {
    int orow = nfr + H + i, node = l.r[i].node, d = w->rt.n[node].depth;
    RowE *re = &l.r[i];
    o->kind[orow] = re->kind;
    o->level[orow] = (u8)d;
    if (re->kind == PK_BLANK) continue;
    /* the label */
    if (re->kind == PK_GRAND) { oset_str(o, orow, 0, "Grand Total"); }
    else if (w->R) {
      const Item *it = node_item(w, &w->rt, w->rf, node);
      int lc = p->layout == PL_COMPACT ? 0 : d - 1;
      if (re->kind == PK_SUB && (p->layout == PL_TABULAR || !p->subtop)) {
        char t2[300];
        snprintf(t2, sizeof t2, "%s Total", it ? it->label : "");
        oset_str(o, orow, lc, t2);
        if (p->layout == PL_COMPACT) oc(o, orow, 0)->indent = (u8)(d - 1);
      } else if (p->layout == PL_TABULAR) {
        /* the leaf row carries the labels of the groups it starts */
        int nd, chain[64], nch = 0;
        for (nd = node; nd > 0 && nch < 64; nd = w->rt.n[nd].parent) chain[nch++] = nd;
        for (k = nch - 1; k >= 0; k--) {
          int an = chain[k];
          if (!started[an] || an == node) {
            oset_item(o, orow, w->rt.n[an].depth - 1, node_item(w, &w->rt, w->rf, an), 0);
            started[an] = 1;
          }
        }
      } else oset_item(o, orow, lc, it, p->layout == PL_COMPACT ? d - 1 : 0);
    }
    if (re->kind == PK_ITEM) continue;
    for (k = 0; k < l.nc; k++) {
      OCell *x = oc(o, orow, L + k);
      x->v = cell_of(w, node, &l.c[k]);
      if (l.c[k].v >= 0 && p->vals[l.c[k].v].showas != PS_NORMAL) x->fmt = 10;   /* 0.00% */
    }
  }
  free(started);
  free(l.c); free(l.r);
  return 1;
}

/* ------------------------------------------------------------------ writing the report */
static int hold(Book *b) { int m = b->manualcalc; b->manualcalc = 1; return m; }
static void release(Book *b, int m) { b->manualcalc = m; if (!m) book_recalc(b); }
static void clear_rect(Sheet *s, Rect a)
{
  i32 r, c;
  for (r = a.r1; r <= a.r2 && r < s->nrows; r++) {
    if (!s->rows[r]) continue;
    for (c = a.c1; c <= a.c2; c++) if (sheet_cell(s, r, c)) sheet_cell_clear(s, r, c);
  }
}
static int write_out(Book *b, int sheet, LPivot *p, Out *o)
{
  Sheet *s = b->sh[sheet];
  i32 top = p->r - (o->bodyrow), r, c;
  long i;
  if (top < 0) { p->r -= top; top = 0; }
  if (top + o->rows > MAXROW || p->c + o->cols > MAXCOL) return 0;
  if (p->out.r1 >= 0) clear_rect(s, p->out);
  for (r = 0; r < o->rows; r++)
    for (c = 0; c < o->cols; c++) {
      OCell *x = oc(o, r, c);
      Cell *cell;
      if (x->v.t == V_EMPTY && !x->fmt && !x->indent) { if (sheet_cell(s, top + r, p->c + c)) sheet_cell_clear(s, top + r, p->c + c); continue; }
      if (!(cell = sheet_cell_make(s, top + r, p->c + c))) continue;
      val_free(&cell->v); formula_free(cell->f); cell->f = NULL;
      cell->v = x->v; x->v = val_empty();
      cell->style = 0;
      if (x->fmt || x->indent) {
        Style st = b->styles[0];
        int si;
        st.numfmt = x->fmt; st.indent = x->indent;
        if (x->indent) st.halign = HA_LEFT;
        if ((si = book_style(b, &st)) >= 0) cell->style = (u16)si;
      }
    }
  p->out.r1 = top; p->out.c1 = p->c; p->out.r2 = top + o->rows - 1; p->out.c2 = p->c + o->cols - 1;
  free(p->kind); free(p->level);
  p->kind = o->kind; p->level = o->level; o->kind = o->level = NULL;
  p->nkind = o->rows;
  p->bodyrow = top + o->bodyrow; p->datarow = top + o->datarow; p->datacol = p->c + o->datacol;
  p->gtcol = o->gtcol >= 0 ? p->c + o->gtcol : -1;
  (void)i;
  return 1;
}
int book_pivot_refresh(Book *b, int sheet, int pi, const char **why)
{
  LPivot *p;
  Work w;
  Out o;
  int ok = 0, m;
  const char *dummy;
  if (!why) why = &dummy;
  *why = "";
  if (sheet < 0 || sheet >= b->nsh || pi < 0 || pi >= b->sh[sheet]->npivots) return 0;
  p = &b->sh[sheet]->pivots[pi];
  memset(&w, 0, sizeof w);
  w.b = b; w.p = p;
  memset(&o, 0, sizeof o);
  m = hold(b);
  if (b->calcgen == 0) book_recalc(b);
  if (work_out(&w, why) && lay_out(&w, &o, why)) {
    book_begin(b);
    book_note_sheet(b, sheet);
    p = &b->sh[sheet]->pivots[pi];
    ok = write_out(b, sheet, p, &o);
    if (!ok) *why = "The PivotTable doesn't fit on the sheet there.";
    b->dirty = 1;
    book_end(b);
  }
  out_free(&o);
  work_free(&w);
  release(b, m);
  return ok;
}
void book_pivots_refresh_all(Book *b)
{
  int s, i;
  for (s = 0; s < b->nsh; s++) for (i = 0; i < b->sh[s]->npivots; i++) book_pivot_refresh(b, s, i, NULL);
}
int book_pivot_add(Book *b, int sheet, const char *source, i32 r, i32 c)
{
  Sheet *s;
  LPivot np, *na;
  int ss, idx;
  Rect src;
  if (sheet < 0 || sheet >= b->nsh) return -1;
  s = b->sh[sheet];
  memset(&np, 0, sizeof np);
  snprintf(np.source, sizeof np.source, "%s", source);
  if (!pivot_source(b, &np, &ss, &src) || src.r2 <= src.r1) return -1;
  pivot_unique_name(b, np.name);
  np.r = r; np.c = c;
  np.layout = PL_COMPACT; np.subtotals = 1; np.subtop = 1; np.grandrows = 1; np.grandcols = 1;
  np.rowheaders = 1; np.colheaders = 1;
  np.out.r1 = -1;
  strcpy(np.style, "PivotStyleLight16");
  book_begin(b);
  book_note_sheet(b, sheet);
  if (!(na = (LPivot *)realloc(s->pivots, (size_t)(s->npivots + 1) * sizeof(LPivot)))) { book_end(b); return -1; }
  s->pivots = na;
  idx = s->npivots++;
  s->pivots[idx] = np;
  book_pivot_refresh(b, sheet, idx, NULL);
  b->dirty = 1;
  book_end(b);
  return idx;
}
int book_pivot_delete(Book *b, int sheet, int pi)
{
  Sheet *s;
  int m;
  if (sheet < 0 || sheet >= b->nsh || pi < 0 || pi >= b->sh[sheet]->npivots) return 0;
  s = b->sh[sheet];
  m = hold(b);
  book_begin(b);
  book_note_sheet(b, sheet);
  if (s->pivots[pi].out.r1 >= 0) clear_rect(s, s->pivots[pi].out);
  pivot_free1(&s->pivots[pi]);
  memmove(&s->pivots[pi], &s->pivots[pi + 1], (size_t)(s->npivots - pi - 1) * sizeof(LPivot));
  if (!--s->npivots) { free(s->pivots); s->pivots = NULL; }
  b->dirty = 1;
  book_end(b);
  release(b, m);
  return 1;
}
int book_pivot_set(Book *b, int sheet, int pi, const LPivot *np)
{
  LPivot cp, *p;
  int ok;
  if (sheet < 0 || sheet >= b->nsh || pi < 0 || pi >= b->sh[sheet]->npivots) return 0;
  book_begin(b);
  book_note_sheet(b, sheet);
  p = &b->sh[sheet]->pivots[pi];
  pivot_copy1(&cp, np);
  /* the report as it is on the sheet stays known, so the refresh clears it */
  cp.out = p->out;
  free(cp.kind); free(cp.level);
  cp.kind = p->kind; cp.level = p->level; cp.nkind = p->nkind;
  p->kind = p->level = NULL; p->nkind = 0;
  pivot_free1(p);
  *p = cp;
  ok = book_pivot_refresh(b, sheet, pi, NULL);
  book_end(b);
  return ok;
}

/* ------------------------------------------------------------------ editing a definition */
static PivotField **area_list(LPivot *p, int area, int **n)
{
  switch (area) {
  case PA_ROWS: *n = &p->nrows; return &p->rows;
  case PA_COLS: *n = &p->ncols; return &p->cols;
  case PA_FILTERS: *n = &p->nfilters; return &p->filters;
  }
  return NULL;
}
int pivot_def_has(const LPivot *p, const char *field)
{
  int i;
  for (i = 0; i < p->nrows; i++) if (str_ieq(p->rows[i].name, field)) return 1;
  for (i = 0; i < p->ncols; i++) if (str_ieq(p->cols[i].name, field)) return 1;
  for (i = 0; i < p->nfilters; i++) if (str_ieq(p->filters[i].name, field)) return 1;
  for (i = 0; i < p->nvals; i++) if (str_ieq(p->vals[i].name, field)) return 1;
  return 0;
}
int pivot_def_add(LPivot *p, int area, const char *field, int func)
{
  if (area == PA_VALUES) {
    PivotValue *nv = (PivotValue *)realloc(p->vals, (size_t)(p->nvals + 1) * sizeof(PivotValue));
    if (!nv || p->nvals >= 32) return 0;
    p->vals = nv;
    memset(&nv[p->nvals], 0, sizeof(PivotValue));
    nv[p->nvals].name = str_dup(field);
    nv[p->nvals].func = (u8)(func >= 0 && func < PF_N ? func : PF_SUM);
    p->nvals++;
    return 1;
  } else {
    int *n, i;
    PivotField **l = area_list(p, area, &n), *nf;
    if (!l || *n >= 16) return 0;
    /* a field is in one of Rows, Columns and Filters at a time */
    for (i = PA_ROWS; i <= PA_FILTERS; i++) {
      int *m, k;
      PivotField **o;
      if (i == PA_VALUES || !(o = area_list(p, i, &m))) continue;
      for (k = 0; k < *m; k++) if (str_ieq((*o)[k].name, field)) return i == area;
    }
    if (!(nf = (PivotField *)realloc(*l, (size_t)(*n + 1) * sizeof(PivotField)))) return 0;
    *l = nf;
    memset(&nf[*n], 0, sizeof(PivotField));
    nf[*n].name = str_dup(field);
    (*n)++;
    return 1;
  }
}
int pivot_def_remove(LPivot *p, int area, int i)
{
  if (area == PA_VALUES) {
    if (i < 0 || i >= p->nvals) return 0;
    free(p->vals[i].name); free(p->vals[i].caption);
    memmove(&p->vals[i], &p->vals[i + 1], (size_t)(p->nvals - i - 1) * sizeof(PivotValue));
    p->nvals--;
    return 1;
  } else {
    int *n;
    PivotField **l = area_list(p, area, &n);
    if (!l || i < 0 || i >= *n) return 0;
    field_free(&(*l)[i]);
    memmove(&(*l)[i], &(*l)[i + 1], (size_t)(*n - i - 1) * sizeof(PivotField));
    (*n)--;
    return 1;
  }
}
int pivot_def_move(LPivot *p, int area, int i, int to_area, int to)
{
  if (area == to_area) {
    if (area == PA_VALUES) {
      PivotValue t;
      if (i < 0 || i >= p->nvals || to < 0 || to >= p->nvals) return 0;
      t = p->vals[i];
      if (to > i) memmove(&p->vals[i], &p->vals[i + 1], (size_t)(to - i) * sizeof t);
      else memmove(&p->vals[to + 1], &p->vals[to], (size_t)(i - to) * sizeof t);
      p->vals[to] = t;
    } else {
      int *n;
      PivotField **l = area_list(p, area, &n), t;
      if (!l || i < 0 || i >= *n || to < 0 || to >= *n) return 0;
      t = (*l)[i];
      if (to > i) memmove(&(*l)[i], &(*l)[i + 1], (size_t)(to - i) * sizeof t);
      else memmove(&(*l)[to + 1], &(*l)[to], (size_t)(i - to) * sizeof t);
      (*l)[to] = t;
    }
    return 1;
  }
  {
    char name[256];
    int func = PF_SUM;
    if (area == PA_VALUES) { if (i < 0 || i >= p->nvals) return 0; snprintf(name, sizeof name, "%s", p->vals[i].name); }
    else {
      int *n;
      PivotField **l = area_list(p, area, &n);
      if (!l || i < 0 || i >= *n) return 0;
      snprintf(name, sizeof name, "%s", (*l)[i].name);
    }
    pivot_def_remove(p, area, i);
    return pivot_def_add(p, to_area, name, func);
  }
}

/* ------------------------------------------------------------------ the look */
int pivot_look(Book *b, int sheet, i32 r, i32 c, TblLook *lk)
{
  Sheet *s = b->sh[sheet];
  LPivot *p = NULL;
  TStyle st;
  int i, kind;
  u32 accent, light;
  memset(lk, 0, sizeof *lk);
  lk->color = COLOR_AUTO;
  for (i = 0; i < s->npivots; i++) {
    LPivot *q = &s->pivots[i];
    if (q->out.r1 >= 0 && q->kind && r >= q->out.r1 && r <= q->out.r2 && c >= q->out.c1 && c <= q->out.c2 && r - q->out.r1 < q->nkind) { p = q; break; }
  }
  if (!p) return 0;
  kind = p->kind[r - p->out.r1];
  table_style_colors(p->style, &st);
  if (!p->style[0]) { if (kind == PK_HEAD || kind == PK_SUB || kind == PK_GRAND) lk->bold = 1; return 1; }
  accent = st.hfill != NOCOL ? st.hfill : st.hline != NOCOL ? st.hline : st.tline != NOCOL ? st.tline : 0x4472C4;
  light = st.band != NOCOL ? st.band : st.body != NOCOL ? st.body : 0xDDEBF7;
  switch (kind) {
  case PK_FILTER:
    if (c == p->out.c1) lk->bold = 1;
    if (c <= p->out.c1 + 1) { lk->fill = FILL_ON | light; lk->border[B_BOTTOM] = BS_THIN; lk->bcolor[B_BOTTOM] = 0xFFFFFF; }
    break;
  case PK_HEAD:
    if (p->colheaders) {
      if (st.hfill != NOCOL) { lk->fill = FILL_ON | st.hfill; if (st.hfont != NOCOL) lk->color = st.hfont; }
      else lk->fill = FILL_ON | light;
      lk->bold = 1;
      if (r == p->datarow - 1) { lk->border[B_BOTTOM] = BS_THIN; lk->bcolor[B_BOTTOM] = accent; }
    }
    break;
  case PK_SUB:
    lk->bold = 1;
    if (p->layout != PL_COMPACT || p->level[r - p->out.r1] == 1) { lk->border[B_TOP] = BS_THIN; lk->bcolor[B_TOP] = light; }
    break;
  case PK_GRAND:
    lk->bold = 1;
    lk->fill = FILL_ON | light;
    lk->border[B_TOP] = st.tdouble ? BS_DOUBLE : BS_THIN; lk->bcolor[B_TOP] = accent;
    break;
  case PK_DATA: case PK_ITEM:
    if (p->bandrows && ((r - p->datarow) & 1)) lk->fill = FILL_ON | light;
    if (p->bandcols && c >= p->datacol && ((c - p->datacol) & 1)) lk->fill = FILL_ON | light;
    if (kind == PK_ITEM) lk->bold = 1;
    break;
  }
  if (p->gtcol >= 0 && c >= p->gtcol && kind != PK_FILTER && kind != PK_BLANK) lk->bold = 1;
  if (p->rowheaders && c < p->datacol && (kind == PK_DATA || kind == PK_SUB) && p->level[r - p->out.r1] < (p->nrows ? p->nrows : 1)) lk->bold = 1;
  return 1;
}
