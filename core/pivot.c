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
#include "drawing.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>
#include <math.h>

#define MAXACC 60000L             /* accumulators (report cells x values) a report may need */
#define MAXOUT 200000L            /* cells a report may have */

/* ------------------------------------------------------------------ memory */
static char **strs_copy(char *const *v, int n)
{
  char **c;
  int i;
  if (!n || !(c = (char **)calloc((size_t)n, sizeof(char *)))) return NULL;
  for (i = 0; i < n; i++) c[i] = str_dup(v[i] ? v[i] : "");
  return c;
}
static void strs_free(char **v, int n) { int i; for (i = 0; i < n; i++) free(v[i]); free(v); }
static void field_free(PivotField *f)
{
  int i;
  free(f->name);
  strs_free(f->hidden, f->nhidden);
  strs_free(f->collapsed, f->ncollapsed);
  for (i = 0; i < f->nmg; i++) { free(f->mg[i].name); strs_free(f->mg[i].items, f->mg[i].n); }
  free(f->mg);
  memset(f, 0, sizeof *f);
}
static int field_copy(PivotField *d, const PivotField *s)
{
  int i;
  *d = *s;
  d->name = str_dup(s->name ? s->name : "");
  d->hidden = strs_copy(s->hidden, s->nhidden);
  if (!d->hidden) d->nhidden = 0;
  d->collapsed = strs_copy(s->collapsed, s->ncollapsed);
  if (!d->collapsed) d->ncollapsed = 0;
  d->mg = NULL; d->nmg = 0;
  if (s->nmg && (d->mg = (PivotGroup *)calloc((size_t)s->nmg, sizeof(PivotGroup))))
    for (i = 0; i < s->nmg; i++) {
      d->mg[i].name = str_dup(s->mg[i].name ? s->mg[i].name : "");
      d->mg[i].items = strs_copy(s->mg[i].items, s->mg[i].n);
      d->mg[i].n = d->mg[i].items ? s->mg[i].n : 0;
      d->nmg = i + 1;
    }
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
static void btns_free(LPivot *p)
{
  int i;
  for (i = 0; i < p->nbtns; i++) free(p->btns[i].label);
  free(p->btns);
  p->btns = NULL; p->nbtns = 0;
}
void pivot_free1(LPivot *p)
{
  int i;
  fields_free(p->rows, p->nrows); fields_free(p->cols, p->ncols); fields_free(p->filters, p->nfilters);
  fields_free(p->sfilters, p->nsfilters);
  for (i = 0; i < p->nvals; i++) { free(p->vals[i].name); free(p->vals[i].caption); }
  free(p->vals);
  for (i = 0; i < p->ncalcs; i++) { free(p->calcs[i].name); free(p->calcs[i].formula); }
  free(p->calcs);
  free(p->kind); free(p->level);
  btns_free(p);
  p->rows = p->cols = p->filters = p->sfilters = NULL; p->vals = NULL; p->kind = p->level = NULL; p->calcs = NULL;
  p->nrows = p->ncols = p->nfilters = p->nsfilters = p->nvals = p->nkind = p->ncalcs = 0;
}
int pivot_copy1(LPivot *d, const LPivot *s)
{
  int i;
  *d = *s;
  d->rows = fields_copy(s->rows, s->nrows);
  d->cols = fields_copy(s->cols, s->ncols);
  d->filters = fields_copy(s->filters, s->nfilters);
  d->sfilters = fields_copy(s->sfilters, s->nsfilters);
  d->vals = s->nvals ? (PivotValue *)calloc((size_t)s->nvals, sizeof(PivotValue)) : NULL;
  for (i = 0; i < s->nvals && d->vals; i++) {
    d->vals[i] = s->vals[i];
    d->vals[i].name = str_dup(s->vals[i].name ? s->vals[i].name : "");
    d->vals[i].caption = s->vals[i].caption ? str_dup(s->vals[i].caption) : NULL;
  }
  d->calcs = s->ncalcs ? (PivotCalc *)calloc((size_t)s->ncalcs, sizeof(PivotCalc)) : NULL;
  for (i = 0; i < s->ncalcs && d->calcs; i++) {
    d->calcs[i].name = str_dup(s->calcs[i].name ? s->calcs[i].name : "");
    d->calcs[i].formula = str_dup(s->calcs[i].formula ? s->calcs[i].formula : "");
  }
  if (!d->calcs) d->ncalcs = 0;
  if (!d->vals) d->nvals = 0;
  if (!d->rows) d->nrows = 0;
  if (!d->cols) d->ncols = 0;
  if (!d->filters) d->nfilters = 0;
  if (!d->sfilters) d->nsfilters = 0;
  d->kind = d->level = NULL; d->nkind = 0;
  if (s->nkind && (d->kind = (u8 *)malloc((size_t)s->nkind)) && (d->level = (u8 *)malloc((size_t)s->nkind))) {
    memcpy(d->kind, s->kind, (size_t)s->nkind); memcpy(d->level, s->level, (size_t)s->nkind); d->nkind = s->nkind;
  } else { free(d->kind); d->kind = NULL; }
  d->btns = NULL; d->nbtns = 0;
  if (s->nbtns && (d->btns = (PivotBtn *)calloc((size_t)s->nbtns, sizeof(PivotBtn))))
    for (i = 0; i < s->nbtns; i++) { d->btns[i] = s->btns[i]; d->btns[i].label = str_dup(s->btns[i].label ? s->btns[i].label : ""); d->nbtns = i + 1; }
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
    if (n > 0) {
      int k;
      for (k = 0; k < p->nbtns; k++) { i32 *b = rows ? &p->btns[k].r : &p->btns[k].c; if (*b >= at) *b += n; }
    } else btns_free(p);
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
static const char *const monthnames[12] = { "Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec" };
static int is_int(double x) { return x == floor(x) && fabs(x) < 1e15; }
static void num_text(double x, char *out, size_t cap) { char g[40]; num_general(x, g, 15); snprintf(out, cap, "%s", g); }
/* a number's group under a date or interval grouping: its label and the key it sorts by; 0 if the
   grouping doesn't apply (not a number) */
static int group_label(const PivotField *f, const Value *v, char *out, size_t cap, double *key)
{
  int y, m, d;
  if (!f || f->group == PG_NONE || v->t != V_NUM) return 0;
  if (f->group == PG_RANGE) {
    double st = f->gstep > 0 ? f->gstep : 1, x = v->n, k, lo;
    char a[40], b2[40];
    if (x < f->gstart) { num_text(f->gstart, a, sizeof a); snprintf(out, cap, "<%s", a); *key = -1e300; return 1; }
    if (x > f->gend) { num_text(f->gend, a, sizeof a); snprintf(out, cap, ">%s", a); *key = 1e300; return 1; }
    k = floor((x - f->gstart) / st + 1e-9);
    lo = f->gstart + k * st;
    num_text(lo, a, sizeof a);
    num_text(is_int(f->gstart) && is_int(st) ? lo + st - 1 : lo + st, b2, sizeof b2);
    snprintf(out, cap, "%s-%s", a, b2);
    *key = k;
    return 1;
  }
  if (v->n < 0 || v->n > 2958465) return 0;
  serial_ymd(floor(v->n), &y, &m, &d);
  if (m < 1 || m > 12) return 0;
  switch (f->group) {
  case PG_YEARS: snprintf(out, cap, "%d", y); *key = y; break;
  case PG_QUARTERS: snprintf(out, cap, "Qtr%d", (m - 1) / 3 + 1); *key = (m - 1) / 3 + 1; break;
  case PG_MONTHS: snprintf(out, cap, "%s", monthnames[m - 1]); *key = m; break;
  default: snprintf(out, cap, "%d-%s", d, monthnames[m - 1]); *key = m * 100 + d; break;
  }
  return 1;
}
void pivot_field_label(Book *b, const PivotField *f, const Value *v, char *out, size_t cap)
{
  double key;
  int i, k;
  if (group_label(f, v, out, cap, &key)) return;
  pivot_item_label(b, v, out, cap);
  if (f && f->nmg)
    for (i = 0; i < f->nmg; i++)
      for (k = 0; k < f->mg[i].n; k++)
        if (str_ieq(f->mg[i].items[k], out)) { snprintf(out, cap, "%s", f->mg[i].name); return; }
}
typedef struct { Value v; char *label; u16 fmt; int rank; u8 keyed; double key; } Item;
typedef struct {
  int col;                        /* source column offset */
  Item *it; int n, cap;
  int *hash; int hcap;            /* open addressing on the lower-case label: item index + 1 */
  u8 sort;
  const PivotField *pf;           /* its grouping */
} FItems;
static u32 lhash(const char *s) { u32 h = 5381; for (; *s; s++) h = h * 33 + (u32)tolower((u8)*s); return h; }
/* the item of a source value under the field's grouping (added if new); its index, -1 out of memory */
static int fitems_find(Book *b, FItems *f, const Value *v, u16 fmt, char *lab, size_t cap)
{
  u32 h;
  int k, keyed = 0;
  double key = 0;
  if (group_label(f->pf, v, lab, cap, &key)) keyed = 1;
  else pivot_field_label(b, f->pf, v, lab, cap);
  if (f->n * 2 + 2 > f->hcap) {
    int nc = f->hcap ? f->hcap * 2 : 64, i;
    int *nh = (int *)calloc((size_t)nc, sizeof(int));
    if (!nh) return -1;
    for (i = 0; i < f->n; i++) { u32 hh = lhash(f->it[i].label) % (u32)nc; while (nh[hh]) hh = (hh + 1) % (u32)nc; nh[hh] = i + 1; }
    free(f->hash); f->hash = nh; f->hcap = nc;
  }
  h = lhash(lab) % (u32)f->hcap;
  while ((k = f->hash[h])) { if (str_ieq(f->it[k - 1].label, lab)) return k - 1; h = (h + 1) % (u32)f->hcap; }
  if (f->n == f->cap) {
    int nc = f->cap ? f->cap * 2 : 32;
    Item *ni = (Item *)realloc(f->it, (size_t)nc * sizeof(Item));
    if (!ni) return -1;
    f->it = ni; f->cap = nc;
  }
  /* an item grouped by hand shows its group's name */
  f->it[f->n].v = keyed ? val_str(lab) : val_copy(v);
  if (!keyed && f->pf && f->pf->nmg && v->t != V_EMPTY) {
    char raw[256];
    pivot_item_label(b, v, raw, sizeof raw);
    if (strcmp(raw, lab)) { val_free(&f->it[f->n].v); f->it[f->n].v = val_str(lab); fmt = 0; }
  }
  f->it[f->n].label = str_dup(lab);
  f->it[f->n].fmt = keyed ? 0 : fmt;
  f->it[f->n].rank = f->n;
  f->it[f->n].keyed = (u8)keyed;
  f->it[f->n].key = key;
  f->hash[h] = f->n + 1;
  return f->n++;
}
static void fitems_free(FItems *f)
{
  int i;
  for (i = 0; i < f->n; i++) { val_free(&f->it[i].v); free(f->it[i].label); }
  free(f->it); free(f->hash);
}
/* items in report order: numbers (and groups by their key), then text, then TRUE/FALSE, blanks last */
static FItems *g_sortf;
static int item_cmp(const void *a, const void *b)
{
  const Item *x = &g_sortf->it[*(const int *)a], *y = &g_sortf->it[*(const int *)b];
  int ex = x->v.t == V_EMPTY || (x->v.t == V_STR && !x->v.s[0]), ey = y->v.t == V_EMPTY || (y->v.t == V_STR && !y->v.s[0]), c;
  if (ex || ey) return ex - ey;
  if (x->v.t == V_ERR || y->v.t == V_ERR) return (x->v.t == V_ERR) - (y->v.t == V_ERR);
  if (x->keyed || y->keyed) {
    Value vx = x->keyed ? val_num(x->key) : x->v, vy = y->keyed ? val_num(y->key) : y->v;
    c = val_compare(&vx, &vy);
  } else c = val_compare(&x->v, &y->v);
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
int pivot_field_items_of(Book *b, const LPivot *p, const char *field, const PivotField *pf, char ***out)
{
  int sheet, n, k, i;
  Rect src;
  char **names;
  FItems f;
  i32 r;
  *out = NULL;
  if (!pivot_source(b, p, &sheet, &src)) return 0;
  n = source_fields(b, sheet, src, &names);
  k = col_of(names, n, pf ? pf->name : field);
  pivot_strings_free(names, n);
  if (k < 0) return 0;
  memset(&f, 0, sizeof f);
  f.pf = pf;
  f.sort = pf ? pf->sort : PO_ASC;
  if (f.sort == PO_SOURCE) f.sort = PO_ASC;
  for (r = src.r1 + 1; r <= src.r2; r++) {
    char lab[256];
    fitems_find(b, &f, cell_value(b, sheet, r, src.c1 + k), 0, lab, sizeof lab);
  }
  fitems_rank(&f);
  if ((*out = (char **)calloc((size_t)f.n + 1, sizeof(char *))))
    for (i = 0; i < f.n; i++) (*out)[f.it[i].rank] = str_dup(f.it[i].label);
  n = f.n;
  fitems_free(&f);
  return n;
}
/* the first field of the areas named name (its grouping applies), or NULL */
static const PivotField *field_named(const LPivot *p, const char *name)
{
  int i;
  for (i = 0; i < p->nrows; i++) if (str_ieq(p->rows[i].name, name)) return &p->rows[i];
  for (i = 0; i < p->ncols; i++) if (str_ieq(p->cols[i].name, name)) return &p->cols[i];
  for (i = 0; i < p->nfilters; i++) if (str_ieq(p->filters[i].name, name)) return &p->filters[i];
  return NULL;
}
int pivot_items(Book *b, const LPivot *p, const char *field, char ***out)
{
  const PivotField *f = field_named(p, field);
  PivotField tmp;
  if (f) return pivot_field_items_of(b, p, field, f, out);
  memset(&tmp, 0, sizeof tmp);
  tmp.name = (char *)field;
  return pivot_field_items_of(b, p, field, &tmp, out);
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
  char **names; int nf;           /* the source's fields */
  int R, C, V, X;                 /* X: sums of the source fields the calculated fields use */
  FItems *rf, *cf;                /* the Rows and Columns fields' items */
  int *vcol, *vcalc;              /* each value field's source column, or its calculated field (-1 neither) */
  int *xcol;                      /* the source columns summed for calculated fields */
  int nrec; int *rrank, *crank;   /* each record's items (ranks), R and C per record */
  i32 *recrow;                    /* each record's source row */
  Tree rt, ct;
  int *rnext;                     /* each row node's next sibling (-1 none) */
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
static Acc *acc_at(Work *w, int rn, int cn, int v) { return &w->acc[((long)rn * w->ct.nn + cn) * (w->V + w->X) + v]; }
static void work_free(Work *w)
{
  int i;
  for (i = 0; w->rf && i < w->R; i++) fitems_free(&w->rf[i]);
  for (i = 0; w->cf && i < w->C; i++) fitems_free(&w->cf[i]);
  free(w->rf); free(w->cf); free(w->vcol); free(w->vcalc); free(w->xcol); free(w->rrank); free(w->crank); free(w->recrow);
  free(w->rt.n); free(w->ct.n); free(w->rnext); free(w->recrl); free(w->reccl); free(w->acc);
  if (w->names) pivot_strings_free(w->names, w->nf);
}

/* ------------------------------------------------------------------ calculated fields */
/* a formula's field names, each handed to fn (which may write what replaces it into out): bare
   names (not a function's) and names in single quotes; strings, numbers and the rest copied as they
   are. 0 if fn refuses a name or the formula is broken */
typedef int (*CalcNameFn)(void *ctx, const char *name, Buf *out);
static int calc_scan(const char *f, Buf *out, CalcNameFn fn, void *ctx)
{
  const char *p = f;
  char name[256];
  while (*p == ' ' || *p == '=') p++;
  if (!*p) return 0;
  while (*p) {
    u8 ch = (u8)*p;
    if (ch == '"') {
      const char *q = p + 1;
      for (;;) { if (!*q) return 0; if (*q == '"') { if (q[1] == '"') { q += 2; continue; } break; } q++; }
      if (out) buf_add(out, p, (size_t)(q + 1 - p));
      p = q + 1;
    } else if (ch == '\'') {
      size_t n = 0;
      p++;
      for (;;) {
        if (!*p) return 0;
        if (*p == '\'') { if (p[1] == '\'') { p++; } else break; }
        if (n + 1 < sizeof name) name[n++] = *p;
        p++;
      }
      name[n] = 0;
      p++;
      if (!fn(ctx, name, out)) return 0;
    } else if (isdigit(ch) || (ch == '.' && isdigit((u8)p[1]))) {
      const char *q = p;
      while (isdigit((u8)*q) || *q == '.') q++;
      if ((*q == 'e' || *q == 'E') && (isdigit((u8)q[1]) || ((q[1] == '+' || q[1] == '-') && isdigit((u8)q[2])))) { q += 2; while (isdigit((u8)*q)) q++; }
      if (out) buf_add(out, p, (size_t)(q - p));
      p = q;
    } else if (isalpha(ch) || ch == '_' || ch >= 0x80) {
      const char *q = p, *r;
      size_t n;
      while (isalnum((u8)*q) || *q == '_' || *q == '.' || (u8)*q >= 0x80) q++;
      n = (size_t)(q - p);
      for (r = q; *r == ' '; r++) ;
      if (*r == '(' || (n == 4 && !str_ncasecmp(p, "TRUE", 4)) || (n == 5 && !str_ncasecmp(p, "FALSE", 5))) { if (out) buf_add(out, p, n); }
      else {
        if (n >= sizeof name) n = sizeof name - 1;
        memcpy(name, p, n); name[n] = 0;
        if (!fn(ctx, name, out)) return 0;
      }
      p = q;
    } else {
      if (out) buf_add(out, p, 1);
      p++;
    }
  }
  return 1;
}
int pivot_calc_index(const LPivot *p, const char *name)
{
  int i;
  for (i = 0; name && i < p->ncalcs; i++) if (str_ieq(p->calcs[i].name, name)) return i;
  return -1;
}
/* the source columns a calculated field sums (through the calculated fields it names) */
typedef struct { Work *w; int depth, ok; } RefCtx;
static int ref_name(void *ctx, const char *name, Buf *out)
{
  RefCtx *c = (RefCtx *)ctx;
  Work *w = c->w;
  int k = col_of(w->names, w->nf, name), i;
  (void)out;
  if (k >= 0) {
    int *nx;
    for (i = 0; i < w->X; i++) if (w->xcol[i] == k) return 1;
    if (!(nx = (int *)realloc(w->xcol, (size_t)(w->X + 1) * sizeof(int)))) return 0;
    w->xcol = nx; w->xcol[w->X++] = k;
    return 1;
  }
  if ((i = pivot_calc_index(w->p, name)) >= 0) {
    if (c->depth >= 16) return 1;          /* (a cycle: #REF! when worked out) */
    c->depth++;
    calc_scan(w->p->calcs[i].formula, NULL, ref_name, c);
    c->depth--;
    return 1;
  }
  return 1;                                 /* (an unknown name gives #NAME? when worked out) */
}
static void put_literal(Buf *o, const Value *v)
{
  switch (v->t) {
  case V_NUM: buf_printf(o, "(%.17g)", v->n); break;
  case V_BOOL: buf_adds(o, v->e ? "TRUE" : "FALSE"); break;
  case V_ERR: buf_adds(o, err_name(v->e)); break;
  case V_STR: {
    const char *s;
    buf_adds(o, "\"");
    for (s = v->s; *s; s++) { if (*s == '"') buf_adds(o, "\""); buf_add(o, s, 1); }
    buf_adds(o, "\"");
    break; }
  default: buf_adds(o, "0"); break;
  }
}
static Value calc_value(Work *w, int ci, int rn, int cn, int depth);
typedef struct { Work *w; int rn, cn, depth, refs, any; } EvCtx;
static int ev_name(void *ctx, const char *name, Buf *out)
{
  EvCtx *c = (EvCtx *)ctx;
  Work *w = c->w;
  int k = col_of(w->names, w->nf, name), i;
  Value v;
  if (k >= 0) {
    for (i = 0; i < w->X && w->xcol[i] != k; i++) ;
    c->refs++;
    if (i < w->X) {
      Acc *a = acc_at(w, c->rn, c->cn, w->V + i);
      if (a->na) c->any = 1;
      buf_printf(out, "(%.17g)", a->sum);
    } else buf_adds(out, "0");
    return 1;
  }
  if ((i = pivot_calc_index(w->p, name)) >= 0) {
    c->refs++;
    v = calc_value(w, i, c->rn, c->cn, c->depth + 1);
    if (v.t != V_EMPTY) c->any = 1;
    put_literal(out, &v);
    val_free(&v);
    return 1;
  }
  buf_adds(out, "#NAME?");
  return 1;
}
/* calculated field ci for row node rn and column node cn: its formula on the sums */
static Value calc_value(Work *w, int ci, int rn, int cn, int depth)
{
  EvCtx c;
  Buf o;
  Value v;
  if (depth > 16) return val_err(E_REF);
  memset(&c, 0, sizeof c);
  c.w = w; c.rn = rn; c.cn = cn; c.depth = depth;
  buf_init(&o);
  if (!calc_scan(w->p->calcs[ci].formula, &o, ev_name, &c)) { buf_free(&o); return val_err(E_NAME); }
  if (c.refs && !c.any) { buf_free(&o); return val_empty(); }
  v = book_eval_text(w->b, w->ssheet, o.data ? o.data : "0");
  buf_free(&o);
  if (v.t == V_EMPTY) v = val_num(0);
  return v;
}
typedef struct { char **names; int nf; const LPivot *p; } CheckCtx;
static int check_name(void *ctx, const char *name, Buf *out)
{
  CheckCtx *c = (CheckCtx *)ctx;
  if (out) buf_adds(out, "1");
  return col_of(c->names, c->nf, name) >= 0 || pivot_calc_index(c->p, name) >= 0;
}
int pivot_calc_set(Book *b, LPivot *p, const char *name, const char *formula, const char **why)
{
  CheckCtx c;
  int i, ok, pos;
  PivotCalc *nc;
  Formula *f = NULL;
  Buf o;
  const char *dummy;
  if (!why) why = &dummy;
  while (*name == ' ') name++;
  if (!name[0]) { *why = "Type a name for the calculated field."; return 0; }
  c.nf = pivot_fields(b, p, &c.names);
  c.p = p;
  for (i = 0; i < c.nf; i++) if (str_ieq(c.names[i], name)) break;
  if (i < c.nf) { pivot_strings_free(c.names, c.nf); *why = "A field of the source already has that name."; return 0; }
  buf_init(&o);
  ok = calc_scan(formula, &o, check_name, &c);
  pivot_strings_free(c.names, c.nf);
  /* the formula must parse once its fields are numbers */
  if (ok && o.data) f = formula_parse(b, 0, o.data, &pos);
  buf_free(&o);
  if (!ok) { *why = "The formula names a field the PivotTable hasn't got, or isn't complete."; return 0; }
  if (!f) { *why = "The formula isn't one Ledger can work out."; return 0; }
  formula_free(f);
  i = pivot_calc_index(p, name);
  if (i < 0) {
    if (!(nc = (PivotCalc *)realloc(p->calcs, (size_t)(p->ncalcs + 1) * sizeof(PivotCalc)))) return 0;
    p->calcs = nc;
    i = p->ncalcs++;
    p->calcs[i].name = str_dup(name);
    p->calcs[i].formula = NULL;
  }
  free(p->calcs[i].formula);
  while (*formula == ' ') formula++;
  p->calcs[i].formula = str_dup(formula[0] == '=' ? formula + 1 : formula);
  return 1;
}
int pivot_calc_remove(LPivot *p, const char *name)
{
  int i = pivot_calc_index(p, name), k;
  if (i < 0) return 0;
  for (k = p->nvals - 1; k >= 0; k--) if (str_ieq(p->vals[k].name, name)) pivot_def_remove(p, PA_VALUES, k);
  free(p->calcs[i].name); free(p->calcs[i].formula);
  memmove(&p->calcs[i], &p->calcs[i + 1], (size_t)(p->ncalcs - i - 1) * sizeof(PivotCalc));
  p->ncalcs--;
  return 1;
}

/* ------------------------------------------------------------------ the records */
/* the records, items, trees and accumulators; 0 with *why on failure */
static int work_out(Work *w, const char **why)
{
  Book *b = w->b;
  LPivot *p = w->p;
  int i, k, *fcols = NULL, *rowcols = NULL, *colcols = NULL, *scols = NULL, cap = 0, S;
  i32 r;
  long need;
  if (!pivot_source(b, p, &w->ssheet, &w->src)) { *why = "The source of the PivotTable isn't a range or table any more."; return 0; }
  w->nf = source_fields(b, w->ssheet, w->src, &w->names);
  /* the fields used, by column; missing ones are left out */
  rowcols = (int *)malloc(((size_t)p->nrows + 1) * sizeof(int));
  colcols = (int *)malloc(((size_t)p->ncols + 1) * sizeof(int));
  fcols = (int *)malloc(((size_t)p->nfilters + 1) * sizeof(int));
  scols = (int *)malloc(((size_t)p->nsfilters + 1) * sizeof(int));
  w->vcol = (int *)malloc(((size_t)p->nvals + 1) * sizeof(int));
  w->vcalc = (int *)malloc(((size_t)p->nvals + 1) * sizeof(int));
  if (!rowcols || !colcols || !fcols || !scols || !w->vcol || !w->vcalc) goto oom;
  w->R = w->C = w->V = w->X = 0;
  for (i = 0; i < p->nrows; i++) rowcols[i] = col_of(w->names, w->nf, p->rows[i].name);
  for (i = 0; i < p->ncols; i++) colcols[i] = col_of(w->names, w->nf, p->cols[i].name);
  for (i = 0; i < p->nfilters; i++) fcols[i] = col_of(w->names, w->nf, p->filters[i].name);
  for (i = 0; i < p->nsfilters; i++) scols[i] = col_of(w->names, w->nf, p->sfilters[i].name);
  for (i = 0; i < p->nvals; i++) {
    w->vcol[i] = col_of(w->names, w->nf, p->vals[i].name);
    w->vcalc[i] = w->vcol[i] < 0 ? pivot_calc_index(p, p->vals[i].name) : -1;
  }
  w->R = p->nrows; w->C = p->ncols; w->V = p->nvals;
  for (i = 0; i < w->V; i++)
    if (w->vcalc[i] >= 0) {
      RefCtx rc;
      rc.w = w; rc.depth = 0; rc.ok = 1;
      calc_scan(p->calcs[w->vcalc[i]].formula, NULL, ref_name, &rc);
    }
  S = w->V + w->X;
  w->rf = (FItems *)calloc((size_t)w->R + 1, sizeof(FItems));
  w->cf = (FItems *)calloc((size_t)w->C + 1, sizeof(FItems));
  if (!w->rf || !w->cf) goto oom;
  for (i = 0; i < w->R; i++) { w->rf[i].col = rowcols[i]; w->rf[i].sort = p->rows[i].sort; w->rf[i].pf = &p->rows[i]; }
  for (i = 0; i < w->C; i++) { w->cf[i].col = colcols[i]; w->cf[i].sort = p->cols[i].sort; w->cf[i].pf = &p->cols[i]; }
  /* the records that pass */
  for (r = w->src.r1 + 1; r <= w->src.r2; r++) {
    int pass = 1, any = 0;
    i32 c;
    char lab[256];
    for (c = w->src.c1; c <= w->src.c2 && !any; c++) if (cell_value(b, w->ssheet, r, c)->t != V_EMPTY) any = 1;
    if (!any) continue;
    for (i = 0; i < p->nfilters && pass; i++) {
      if (fcols[i] < 0 || !p->filters[i].nhidden) continue;
      pivot_field_label(b, &p->filters[i], cell_value(b, w->ssheet, r, w->src.c1 + fcols[i]), lab, sizeof lab);
      if (hidden_has(&p->filters[i], lab)) pass = 0;
    }
    for (i = 0; i < p->nsfilters && pass; i++) {
      if (scols[i] < 0 || !p->sfilters[i].nhidden) continue;
      pivot_item_label(b, cell_value(b, w->ssheet, r, w->src.c1 + scols[i]), lab, sizeof lab);
      if (hidden_has(&p->sfilters[i], lab)) pass = 0;
    }
    for (i = 0; i < w->R && pass; i++) if (rowcols[i] >= 0 && p->rows[i].nhidden) { pivot_field_label(b, &p->rows[i], cell_value(b, w->ssheet, r, w->src.c1 + rowcols[i]), lab, sizeof lab); if (hidden_has(&p->rows[i], lab)) pass = 0; }
    for (i = 0; i < w->C && pass; i++) if (colcols[i] >= 0 && p->cols[i].nhidden) { pivot_field_label(b, &p->cols[i], cell_value(b, w->ssheet, r, w->src.c1 + colcols[i]), lab, sizeof lab); if (hidden_has(&p->cols[i], lab)) pass = 0; }
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
      cell = f->col >= 0 ? sheet_cell(b->sh[w->ssheet], r, w->src.c1 + f->col) : NULL;
      idx = fitems_find(b, f, v, cell ? b->styles[cell->style].numfmt : 0, lab, sizeof lab);
      if (idx < 0) goto oom;
      if (k < w->R) w->rrank[(long)w->nrec * w->R + k] = idx; else w->crank[(long)w->nrec * w->C + (k - w->R)] = idx;
    }
    w->recrow[w->nrec++] = r;
  }
  free(rowcols); free(colcols); free(fcols); free(scols); rowcols = colcols = fcols = scols = NULL;
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
  /* each row node's next sibling */
  if (!(w->rnext = (int *)malloc(((size_t)w->rt.nn + 1) * sizeof(int)))) goto oom;
  {
    int *last = (int *)malloc(((size_t)w->rt.nn + 1) * sizeof(int));
    if (!last) goto oom;
    for (i = 0; i < w->rt.nn; i++) { w->rnext[i] = -1; last[i] = -1; }
    for (i = 1; i < w->rt.nn; i++) { int pa = w->rt.n[i].parent; if (last[pa] >= 0) w->rnext[last[pa]] = i; last[pa] = i; }
    free(last);
  }
  need = (long)w->rt.nn * w->ct.nn * (S ? S : 1);
  if (need > MAXACC) { *why = "The PivotTable would be too big: use fewer items in Rows and Columns."; return 0; }
  if (!(w->acc = (Acc *)calloc((size_t)need, sizeof(Acc)))) goto oom;
  for (k = 0; k < w->nrec; k++) {
    int v;
    for (v = 0; v < S; v++) {
      const Value *x;
      Value e = val_empty();
      int rn, cn, col = v < w->V ? w->vcol[v] : w->xcol[v - w->V];
      if (col < 0) continue;
      x = cell_value(b, w->ssheet, w->recrow[k], w->src.c1 + col);
      for (rn = w->recrl[k]; rn >= 0; rn = w->rt.n[rn].parent)
        for (cn = w->reccl[k]; cn >= 0; cn = w->ct.n[cn].parent) acc_add(acc_at(w, rn, cn, v), x);
      (void)e;
    }
  }
  return 1;
oom:
  free(rowcols); free(colcols); free(fcols); free(scols);
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
  if (t->n[node].item < fi->n && fi->it[t->n[node].item].rank == t->n[node].item) return &fi->it[t->n[node].item];
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
/* a node of a Rows or Columns field collapsed (its detail hidden) */
static int node_collapsed(Work *w, int col, int node)
{
  Tree *t = col ? &w->ct : &w->rt;
  int d = t->n[node].depth;
  const PivotField *f;
  const Item *it;
  if (d < 1 || d >= (col ? w->C : w->R)) return 0;
  f = col ? &w->p->cols[d - 1] : &w->p->rows[d - 1];
  if (!f->ncollapsed) return 0;
  it = node_item(w, t, col ? w->cf : w->rf, node);
  return it && pivot_is_collapsed(f, it->label);
}

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
  if (w->ct.n[node].depth == w->C || node_collapsed(w, 1, node)) { for (v = 0; v < nv; v++) addcol(l, node, w->V ? v : -1, 0); return; }
  for (ch = first_child(&w->ct, node); ch >= 0; ch = next_sibling(&w->ct, ch)) col_entries(w, l, ch);
  if (node && w->p->subtotals && w->C >= 2) for (v = 0; v < nv; v++) addcol(l, node, w->V ? v : -1, 1);
}
static void row_entries(Work *w, Lay *l, int node)
{
  LPivot *p = w->p;
  int ch, d = w->rt.n[node].depth;
  if (d == w->R) { addrow(l, node, PK_DATA); return; }
  if (node && node_collapsed(w, 0, node)) {
    addrow(l, node, PK_COLL);
    if (d == 1 && p->blankrows) addrow(l, node, PK_BLANK);
    return;
  }
  if (node) {
    if (p->layout != PL_TABULAR) addrow(l, node, p->subtotals && p->subtop ? PK_SUB : PK_ITEM);
  }
  for (ch = first_child(&w->rt, node); ch >= 0; ch = w->rnext[ch]) row_entries(w, l, ch);
  if (node && p->subtotals && (p->layout == PL_TABULAR || !p->subtop)) addrow(l, node, PK_SUB);
  if (node && d == 1 && p->blankrows) addrow(l, node, PK_BLANK);
}
/* value field v's value for row node rn and column node cn, before Show Values As */
static Value raw_of(Work *w, int rn, int cn, int v)
{
  if (w->vcol[v] >= 0) return acc_value(acc_at(w, rn, cn, v), w->p->vals[v].func);
  if (w->vcalc[v] >= 0) return calc_value(w, w->vcalc[v], rn, cn, 0);
  return val_empty();
}
/* the child of row node nd with item it, -1 none */
static int row_child(Work *w, int nd, int it)
{
  int ch;
  for (ch = first_child(&w->rt, nd); ch >= 0; ch = w->rnext[ch]) if (w->rt.n[ch].item == it) return ch;
  return -1;
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
  v = raw_of(w, rn, cn, ce->v);
  if (pv->showas == PS_NORMAL || v.t != V_NUM) return v;
  if (pv->showas == PS_RUNTOTAL) {
    /* the sum over the first row field's items up to this one, the rest of the path the same */
    int path[64], np = 0, nd, s;
    double tot = 0;
    if (!rn) return v;
    for (nd = rn; nd > 0 && np < 64; nd = w->rt.n[nd].parent) path[np++] = w->rt.n[nd].item;
    for (s = first_child(&w->rt, 0); s >= 0; s = w->rnext[s]) {
      int k, at = s;
      for (k = np - 2; k >= 0 && at >= 0; k--) at = row_child(w, at, path[k]);
      if (at >= 0) { Value x = raw_of(w, at, cn, ce->v); if (x.t == V_NUM) tot += x.n; val_free(&x); }
      if (w->rt.n[s].item == path[np - 1]) break;
    }
    return val_num(tot);
  }
  if (pv->showas == PS_RANK) {
    /* largest first among the items of the same parent */
    int ch, rank = 1;
    if (!rn) return val_empty();
    for (ch = first_child(&w->rt, w->rt.n[rn].parent); ch >= 0; ch = w->rnext[ch]) {
      Value x;
      if (ch == rn) continue;
      x = raw_of(w, ch, cn, ce->v);
      if (x.t == V_NUM && x.n > v.n) rank++;
      val_free(&x);
    }
    return val_num(rank);
  }
  switch (pv->showas) {
  case PS_PCT_GRAND: d = raw_of(w, 0, 0, ce->v); break;
  case PS_PCT_COL: d = raw_of(w, 0, cn, ce->v); break;
  default: d = raw_of(w, rn, 0, ce->v); break;
  }
  if (d.t != V_NUM) { val_free(&d); return val_err(E_DIV0); }
  den = d.n;
  if (den == 0) return val_err(E_DIV0);
  return val_num(v.n / den);
}
static int pct_showas(int s) { return s == PS_PCT_GRAND || s == PS_PCT_COL || s == PS_PCT_ROW; }
/* the report in a grid of cells: rows x cols, with each row's kind and level, the +/- buttons, and
   what a PivotChart plots (category cells, and the data columns with their names) */
typedef struct {
  OCell *cell; int rows, cols; u8 *kind, *level; int bodyrow, datarow, datacol, gtcol;
  PivotBtn *btn; int nbtn, capbtn;
  int *catr, *catc, ncat;
  int *serc; char **sern; int nser;
} Out;
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
static void obtn(Out *o, int r, int c, int area, int fi, int collapsed, const Item *it)
{
  if (!it || fi > 255) return;
  if (o->nbtn == o->capbtn) {
    int nc = o->capbtn ? o->capbtn * 2 : 32;
    PivotBtn *nb = (PivotBtn *)realloc(o->btn, (size_t)nc * sizeof(PivotBtn));
    if (!nb) return;
    o->btn = nb; o->capbtn = nc;
  }
  o->btn[o->nbtn].r = r; o->btn[o->nbtn].c = c; o->btn[o->nbtn].area = (u8)area; o->btn[o->nbtn].fi = (u8)fi;
  o->btn[o->nbtn].collapsed = (u8)collapsed; o->btn[o->nbtn].label = str_dup(it->label);
  o->nbtn++;
}
static void out_free(Out *o)
{
  long i;
  if (o->cell) for (i = 0; i < (long)o->rows * o->cols; i++) val_free(&o->cell[i].v);
  free(o->cell); free(o->kind); free(o->level);
  for (i = 0; i < o->nbtn; i++) free(o->btn[i].label);
  free(o->btn);
  free(o->catr); free(o->catc); free(o->serc);
  for (i = 0; i < o->nser; i++) free(o->sern[i]);
  free(o->sern);
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
  o->catr = (int *)malloc(((size_t)l.nr + 1) * sizeof(int));
  o->catc = (int *)malloc(((size_t)l.nr + 1) * sizeof(int));
  o->serc = (int *)malloc(((size_t)l.nc + 1) * sizeof(int));
  o->sern = (char **)calloc((size_t)l.nc + 1, sizeof(char *));
  if (!o->cell || !o->kind || !o->level || !started || !o->catr || !o->catc || !o->serc || !o->sern) { free(l.c); free(l.r); free(started); out_free(o); *why = "Out of memory."; return 0; }
  o->bodyrow = nfr; o->datarow = nfr + H; o->datacol = L; o->gtcol = -1;
  /* report filters: name, then (All), the one item shown, or (Multiple Items) */
  for (i = 0; i < p->nfilters; i++) {
    char **items;
    int n = pivot_field_items_of(w->b, p, p->filters[i].name, &p->filters[i], &items), shown = 0, last = -1;
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
      if (p->layout == PL_COMPACT) oset_str(o, nfr, L, "Column Labels");
      else { pivot_field_caption(p, PA_COLS, 0, cap, sizeof cap); oset_str(o, nfr, L, cap); }
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
            int an = chain[lv], start = 1, dd = w->ct.n[an].depth;
            if (k > 0) {
              int pn;
              for (pn = l.c[k - 1].node; pn > 0 && w->ct.n[pn].depth > dd; pn = w->ct.n[pn].parent) ;
              if (l.c[k - 1].kind == 0 && pn == an) start = 0;
            }
            if (start) {
              const Item *it = node_item(w, &w->ct, w->cf, an);
              oset_item(o, nfr + dd, L + k, it, 0);
              if (dd < w->C) obtn(o, nfr + dd, L + k, PA_COLS, dd - 1, an == node, it);
            }
          }
          /* a PivotChart's series: the column's items (and value field) */
          if (o->nser < l.nc) {
            Buf nb;
            buf_init(&nb);
            for (lv = nch - 1; lv >= 0; lv--) { const Item *it = node_item(w, &w->ct, w->cf, chain[lv]); if (nb.len) buf_adds(&nb, " - "); buf_adds(&nb, it ? it->label : ""); }
            if (w->V > 1 || !nch) { if (ce->v >= 0) { pivot_caption(&p->vals[ce->v], cap, sizeof cap); if (nb.len) buf_adds(&nb, " - "); buf_adds(&nb, cap); } }
            o->serc[o->nser] = L + k; o->sern[o->nser++] = nb.data ? buf_take(&nb) : str_dup("");
            buf_free(&nb);
          }
        }
        if (w->V > 1) { pivot_caption(&p->vals[ce->v], cap, sizeof cap); oset_str(o, nfr + 1 + w->C, L + k, cap); }
      }
    } else {
      for (k = 0; k < l.nc; k++) {
        pivot_caption(&p->vals[l.c[k].v], cap, sizeof cap);
        oset_str(o, hr, L + k, cap);
        o->serc[o->nser] = L + k; o->sern[o->nser++] = str_dup(cap);
      }
    }
    if (L) {
      if (p->layout == PL_COMPACT) { if (w->R) oset_str(o, hr, 0, "Row Labels"); }
      else for (k = 0; k < w->R; k++) { pivot_field_caption(p, PA_ROWS, k, cap, sizeof cap); oset_str(o, hr, k, cap); }
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
    if (re->kind == PK_DATA || re->kind == PK_COLL) {
      o->catr[o->ncat] = orow;
      o->catc[o->ncat++] = p->layout == PL_COMPACT || d < 1 ? 0 : d - 1;
    }
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
          int an = chain[k], dd = w->rt.n[an].depth;
          if (!started[an] || an == node) {
            const Item *ai = node_item(w, &w->rt, w->rf, an);
            oset_item(o, orow, dd - 1, ai, 0);
            if (dd < w->R) obtn(o, orow, dd - 1, PA_ROWS, dd - 1, an == node && re->kind == PK_COLL, ai);
            started[an] = 1;
          }
        }
      } else {
        oset_item(o, orow, lc, it, p->layout == PL_COMPACT ? d - 1 : 0);
        if (d >= 1 && d < w->R) obtn(o, orow, lc, PA_ROWS, d - 1, re->kind == PK_COLL, it);
      }
    }
    if (re->kind == PK_ITEM) continue;
    for (k = 0; k < l.nc; k++) {
      OCell *x = oc(o, orow, L + k);
      x->v = cell_of(w, node, &l.c[k]);
      if (l.c[k].v >= 0 && pct_showas(p->vals[l.c[k].v].showas)) x->fmt = 10;   /* 0.00% */
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
  btns_free(p);
  for (i = 0; i < o->nbtn; i++) { o->btn[i].r += top; o->btn[i].c += p->c; }
  p->btns = o->btn; p->nbtns = o->nbtn;
  o->btn = NULL; o->nbtn = o->capbtn = 0;
  return 1;
}
/* ------------------------------------------------------------------ PivotCharts */
static int needs_quotes(const char *n)
{
  const char *q;
  if (!n[0] || isdigit((u8)n[0])) return 1;
  for (q = n; *q; q++) if (!isalnum((u8)*q) && *q != '_' && *q != '.' && (u8)*q < 0x80) return 1;
  return 0;
}
static void sheet_prefix(Buf *o, const char *name)
{
  if (needs_quotes(name)) {
    const char *q;
    buf_adds(o, "'");
    for (q = name; *q; q++) { if (*q == '\'') buf_adds(o, "'"); buf_add(o, q, 1); }
    buf_adds(o, "'");
  } else buf_adds(o, name);
}
void pivot_chart_link(Book *b, int sheet, const LPivot *p, char *out, size_t cap)
{
  Buf o;
  buf_init(&o);
  sheet_prefix(&o, b->sh[sheet]->name);
  buf_adds(&o, "!");
  buf_adds(&o, p->name);
  snprintf(out, cap, "%s", o.data ? o.data : "");
  buf_free(&o);
}
LPivot *pivot_by_link(Book *b, const char *link, int *sheet)
{
  const char *t = link, *bang;
  char sh[256], nm[64];
  size_t n;
  int s, i;
  if (!t) return NULL;
  while (*t == ' ' || *t == '=') t++;
  if (*t == '[') { const char *e = strchr(t, ']'); if (e) t = e + 1; }
  if (!(bang = strrchr(t, '!'))) return NULL;
  n = (size_t)(bang - t);
  if (n >= sizeof sh) return NULL;
  if (t[0] == '\'' && n >= 2 && t[n - 1] == '\'') {
    size_t k, o = 0;
    for (k = 1; k + 1 < n; k++) { if (t[k] == '\'' && t[k + 1] == '\'') k++; sh[o++] = t[k]; }
    sh[o] = 0;
  } else { memcpy(sh, t, n); sh[n] = 0; }
  snprintf(nm, sizeof nm, "%s", bang + 1);
  for (s = 0; s < b->nsh; s++)
    if (str_ieq(b->sh[s]->name, sh))
      for (i = 0; i < b->sh[s]->npivots; i++) if (str_ieq(b->sh[s]->pivots[i].name, nm)) { if (sheet) *sheet = s; return &b->sh[s]->pivots[i]; }
  /* (the sheet renamed since: pivot table names are the book's own) */
  for (s = 0; s < b->nsh; s++)
    for (i = 0; i < b->sh[s]->npivots; i++) if (str_ieq(b->sh[s]->pivots[i].name, nm)) { if (sheet) *sheet = s; return &b->sh[s]->pivots[i]; }
  return NULL;
}
/* cells of the report (rows top + r[k], columns left + c[k]) as a reference: runs down a column
   joined into a union */
static void cells_ref(Buf *o, const char *sheetname, const int *r, const int *c, int n, i32 top, i32 left)
{
  int k = 0, parts = 0;
  Buf t;
  buf_init(&t);
  while (k < n) {
    int e = k;
    char a[24], z[24];
    while (e + 1 < n && c[e + 1] == c[k] && r[e + 1] == r[e] + 1) e++;
    if (parts++) buf_adds(&t, ",");
    sheet_prefix(&t, sheetname);
    ref_text(top + r[k], left + c[k], 1, 1, a);
    buf_printf(&t, "!%s", a);
    if (e > k) { ref_text(top + r[e], left + c[k], 1, 1, z); buf_printf(&t, ":%s", z); }
    k = e + 1;
  }
  if (parts > 1) buf_adds(o, "(");
  buf_add(o, t.data ? t.data : "", t.len);
  if (parts > 1) buf_adds(o, ")");
  buf_free(&t);
}
static void series_clear(Chart *ch)
{
  int k;
  for (k = 0; k < ch->ns; k++) { formula_free(ch->s[k].name); formula_free(ch->s[k].cat); formula_free(ch->s[k].val); formula_free(ch->s[k].size); free(ch->s[k].lit); }
  free(ch->s);
  ch->s = NULL; ch->ns = 0;
}
/* every PivotChart of the pivot table: its series made again from the report just written */
#define MAXCHARTCATS 500
static void rebind_charts(Book *b, int sheet, LPivot *p, Out *o)
{
  char link[300];
  int s, i, k, ncat = o->ncat < MAXCHARTCATS ? o->ncat : MAXCHARTCATS;
  pivot_chart_link(b, sheet, p, link, sizeof link);
  for (s = 0; s < b->nsh; s++)
    for (i = 0; i < drw_count(b->sh[s]); i++) {
      DObj *ob = drw_obj(b->sh[s], i);
      Chart *ch = ob->kind == OB_CHART ? ob->chart : NULL;
      int ps;
      CSeries *ns;
      if (!ch || !ch->pivot || pivot_by_link(b, ch->pivot, &ps) != p || ps != sheet) continue;
      if (s != sheet) book_note_sheet(b, s);
      free(ch->pivot); ch->pivot = str_dup(link);
      series_clear(ch);
      if (!ncat || !o->nser || !(ns = (CSeries *)calloc((size_t)o->nser, sizeof(CSeries)))) continue;
      ch->s = ns;
      {
        Buf ct, t;
        int *cc = (int *)malloc((size_t)ncat * sizeof(int)), j, e;
        buf_init(&ct); buf_init(&t);
        cells_ref(&ct, b->sh[sheet]->name, o->catr, o->catc, ncat, p->out.r1, p->out.c1);
        for (k = 0; k < o->nser && cc; k++) {
          ns[k].kind = CK_SAME;
          ns[k].lit = str_dup(o->sern[k]);
          ns[k].cat = formula_parse(b, s, ct.data, &e);
          t.len = 0;
          for (j = 0; j < ncat; j++) cc[j] = o->serc[k];
          cells_ref(&t, b->sh[sheet]->name, o->catr, cc, ncat, p->out.r1, p->out.c1);
          ns[k].val = formula_parse(b, s, t.data, &e);
          ch->ns = k + 1;
        }
        free(cc);
        buf_free(&ct); buf_free(&t);
      }
    }
}
/* charts linked to pivot table oldname get its new name */
static void relink_charts(Book *b, int sheet, const char *oldname, const char *newname)
{
  int s, i;
  for (s = 0; s < b->nsh; s++)
    for (i = 0; i < drw_count(b->sh[s]); i++) {
      DObj *ob = drw_obj(b->sh[s], i);
      Chart *ch = ob->kind == OB_CHART ? ob->chart : NULL;
      const char *bang;
      Buf t;
      if (!ch || !ch->pivot || !(bang = strrchr(ch->pivot, '!')) || !str_ieq(bang + 1, oldname)) continue;
      if (s != sheet) book_note_sheet(b, s);
      buf_init(&t);
      sheet_prefix(&t, b->sh[sheet]->name);
      buf_printf(&t, "!%s", newname);
      free(ch->pivot); ch->pivot = buf_take(&t);
      buf_free(&t);
    }
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
    else rebind_charts(b, sheet, p, &o);
    b->dirty = 1;
    book_end(b);
  }
  out_free(&o);
  work_free(&w);
  release(b, m);
  return ok;
}
/* GETPIVOTDATA (see pivot.h): the report worked out again (not read from its cells, which may be
   stale while the sheet recalculates), then walked down by the field and item pairs */
static int pair_field(const LPivot *p, int area, const char *name)
{
  int i, n = area == PA_ROWS ? p->nrows : p->ncols;
  char cap[256];
  for (i = 0; i < n; i++) {
    pivot_field_caption(p, area, i, cap, sizeof cap);
    if (str_ieq(cap, name)) return i;
  }
  for (i = 0; i < n; i++) if (str_ieq((area == PA_ROWS ? p->rows : p->cols)[i].name, name)) return i;
  return -1;
}
static int item_matches(const Item *it, const PivotItemArg *a)
{
  if (!it) return 0;
  if (a->isnum && it->v.t == V_NUM && it->v.n == a->num) return 1;
  return str_ieq(it->label, a->text);
}
Value pivot_get_data(Book *b, int sheet, i32 r, i32 c, const char *datafield, const PivotItemArg *pairs, int npairs)
{
  LPivot *p = pivot_at(b, sheet, r, c);
  Work w;
  const char *why = "";
  int v = -1, i, k, rn = 0, cn = 0, kr = 0, kc = 0;
  int ri[64], ci[64];             /* the pair given for each row and column field, -1 none */
  char cap[300];
  Value res = val_err(E_REF);
  if (!p || npairs > 126) return val_err(E_REF);
  for (i = 0; i < p->nvals && v < 0; i++) {
    pivot_caption(&p->vals[i], cap, sizeof cap);
    if (str_ieq(cap, datafield)) v = i;
  }
  for (i = 0; i < p->nvals && v < 0; i++) if (str_ieq(p->vals[i].name, datafield)) v = i;
  if (v < 0 || p->nrows > 64 || p->ncols > 64) return val_err(E_REF);
  for (i = 0; i < 64; i++) ri[i] = ci[i] = -1;
  for (k = 0; k < npairs; k++) {
    int f = pair_field(p, PA_ROWS, pairs[k].field);
    if (f >= 0) { ri[f] = k; continue; }
    if ((f = pair_field(p, PA_COLS, pairs[k].field)) >= 0) { ci[f] = k; continue; }
    return val_err(E_REF);       /* (a field the report doesn't show) */
  }
  /* only what the report shows: the fields named from the outermost in, without gaps */
  while (kr < p->nrows && ri[kr] >= 0) kr++;
  while (kc < p->ncols && ci[kc] >= 0) kc++;
  for (i = kr; i < p->nrows; i++) if (ri[i] >= 0) return val_err(E_REF);
  for (i = kc; i < p->ncols; i++) if (ci[i] >= 0) return val_err(E_REF);
  if ((kr == 0 && p->nrows && !p->grandcols) || (kc == 0 && p->ncols && !p->grandrows)) return val_err(E_REF);
  if ((kr > 0 && kr < p->nrows && !p->subtotals) || (kc > 0 && kc < p->ncols && (!p->subtotals || p->ncols < 2))) return val_err(E_REF);
  memset(&w, 0, sizeof w);
  w.b = b; w.p = p;
  if (work_out(&w, &why)) {
    int ok = 1;
    for (i = 0; i < kr && ok; i++) {
      int ch;
      for (ch = first_child(&w.rt, rn); ch >= 0; ch = w.rnext[ch]) if (item_matches(node_item(&w, &w.rt, w.rf, ch), &pairs[ri[i]])) break;
      if (ch < 0) ok = 0; else rn = ch;
    }
    for (i = 0; i < kc && ok; i++) {
      int ch;
      for (ch = first_child(&w.ct, cn); ch >= 0; ch = next_sibling(&w.ct, ch)) if (item_matches(node_item(&w, &w.ct, w.cf, ch), &pairs[ci[i]])) break;
      if (ch < 0) ok = 0; else cn = ch;
    }
    if (ok) {
      ColE ce;
      ce.node = cn; ce.v = v; ce.kind = (u8)(kc == p->ncols ? 0 : kc ? 1 : 2);
      res = cell_of(&w, rn, &ce);
      if (res.t == V_EMPTY) res = val_err(E_REF);
    }
  }
  work_free(&w);
  return res;
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
  if (strcmp(p->name, np->name)) relink_charts(b, sheet, p->name, np->name);
  pivot_copy1(&cp, np);
  /* the report as it is on the sheet stays known, so the refresh clears it */
  cp.out = p->out;
  free(cp.kind); free(cp.level);
  cp.kind = p->kind; cp.level = p->level; cp.nkind = p->nkind;
  p->kind = p->level = NULL; p->nkind = 0;
  btns_free(&cp);
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

/* ------------------------------------------------------------------ grouping */
static PivotField *area_field(LPivot *p, int area, int i, int *n)
{
  PivotField *f = area == PA_ROWS ? p->rows : area == PA_COLS ? p->cols : area == PA_FILTERS ? p->filters : NULL;
  *n = area == PA_ROWS ? p->nrows : area == PA_COLS ? p->ncols : area == PA_FILTERS ? p->nfilters : 0;
  return f && i >= 0 && i < *n ? &f[i] : NULL;
}
static const char *const levelnames[PG_N] = { "", "Years", "Quarters", "Months", "Days", "" };
void pivot_field_caption(const LPivot *p, int area, int i, char *out, size_t cap)
{
  int n, k;
  const PivotField *f = area_field((LPivot *)p, area, i, &n), *all;
  if (!f) { snprintf(out, cap, "%s", ""); return; }
  all = f - i;
  /* a date field in the area more than once: its coarser levels go by their level's name */
  if (f->group >= PG_YEARS && f->group <= PG_DAYS)
    for (k = 0; k < n; k++)
      if (k != i && str_ieq(all[k].name, f->name) && all[k].group > f->group && all[k].group <= PG_DAYS) { snprintf(out, cap, "%s", levelnames[f->group]); return; }
  snprintf(out, cap, "%s", f->name);
}
int pivot_field_type(Book *b, const LPivot *p, const char *name, double *mn, double *mx)
{
  int sheet, n, k, nums = 0, other = 0, dates = 0;
  Rect src;
  char **names;
  i32 r;
  if (!pivot_source(b, p, &sheet, &src)) return 0;
  n = source_fields(b, sheet, src, &names);
  k = col_of(names, n, name);
  pivot_strings_free(names, n);
  if (k < 0) return 0;
  for (r = src.r1 + 1; r <= src.r2; r++) {
    const Value *v = cell_value(b, sheet, r, src.c1 + k);
    if (v->t == V_NUM) {
      if (!nums) *mn = *mx = v->n;
      if (v->n < *mn) *mn = v->n;
      if (v->n > *mx) *mx = v->n;
      nums++;
      if (fmt_is_date(book_fmt_code(b, b->styles[sheet_cell_style(b->sh[sheet], r, src.c1 + k)].numfmt))) dates++;
    } else if (v->t != V_EMPTY) other++;
  }
  if (!nums || other) return 0;
  return dates == nums ? 2 : 1;
}
static int field_insert(LPivot *p, int area, int at, const PivotField *f)
{
  int n, *np;
  PivotField **l, *nf;
  area_field(p, area, 0, &n);
  l = area == PA_ROWS ? &p->rows : area == PA_COLS ? &p->cols : area == PA_FILTERS ? &p->filters : NULL;
  np = area == PA_ROWS ? &p->nrows : area == PA_COLS ? &p->ncols : &p->nfilters;
  if (!l || !(nf = (PivotField *)realloc(*l, (size_t)(n + 1) * sizeof(PivotField)))) return 0;
  *l = nf;
  memmove(&nf[at + 1], &nf[at], (size_t)(n - at) * sizeof(PivotField));
  field_copy(&nf[at], f);
  (*np)++;
  return 1;
}
/* the run of fields of the same source column as field i (a date field's levels): first and count */
static int level_run(LPivot *p, int area, int i, int *first)
{
  int n, a, z;
  PivotField *f = area_field(p, area, i, &n), *all;
  if (!f) return 0;
  all = f - i;
  for (a = i; a > 0 && str_ieq(all[a - 1].name, f->name); a--) ;
  for (z = i; z + 1 < n && str_ieq(all[z + 1].name, f->name); z++) ;
  *first = a;
  return z - a + 1;
}
int pivot_ungroup(LPivot *p, int area, int i)
{
  int n, a, k, cnt = level_run(p, area, i, &a), j;
  PivotField *f;
  if (!cnt) return 0;
  for (k = cnt - 1; k > 0; k--) pivot_def_remove(p, area, a + k);
  f = area_field(p, area, a, &n);
  f->group = PG_NONE; f->gstart = f->gend = f->gstep = 0;
  for (j = 0; j < f->nmg; j++) { free(f->mg[j].name); strs_free(f->mg[j].items, f->mg[j].n); }
  free(f->mg); f->mg = NULL; f->nmg = 0;
  strs_free(f->hidden, f->nhidden); f->hidden = NULL; f->nhidden = 0;
  strs_free(f->collapsed, f->ncollapsed); f->collapsed = NULL; f->ncollapsed = 0;
  return 1;
}
int pivot_group_dates(LPivot *p, int area, int i, unsigned levels)
{
  int n, a, g, at;
  PivotField base, *f;
  if (!(levels & ((1u << PG_YEARS) | (1u << PG_QUARTERS) | (1u << PG_MONTHS) | (1u << PG_DAYS)))) return 0;
  if (!area_field(p, area, i, &n) || !pivot_ungroup(p, area, i)) return 0;
  level_run(p, area, i, &a);
  f = area_field(p, area, a, &n);
  field_copy(&base, f);
  base.sort = PO_ASC;
  pivot_def_remove(p, area, a);
  at = a;
  for (g = PG_YEARS; g <= PG_DAYS; g++)
    if (levels & (1u << g)) { base.group = (u8)g; if (!field_insert(p, area, at++, &base)) break; }
  field_free(&base);
  return at > a;
}
int pivot_group_range(LPivot *p, int area, int i, double start, double end, double step)
{
  int n, a;
  PivotField *f;
  if (!(step > 0) || end < start || (end - start) / step > 100000) return 0;
  if (!area_field(p, area, i, &n) || !pivot_ungroup(p, area, i)) return 0;
  level_run(p, area, i, &a);
  f = area_field(p, area, a, &n);
  f->group = PG_RANGE; f->gstart = start; f->gend = end; f->gstep = step;
  return 1;
}
void pivot_group_name(const PivotField *f, char *out, size_t cap)
{
  int k, j, clash;
  for (k = 1; k < 10000; k++) {
    snprintf(out, cap, "Group%d", k);
    for (clash = 0, j = 0; j < f->nmg; j++) if (str_ieq(f->mg[j].name, out)) clash = 1;
    if (!clash) return;
  }
}
int pivot_group_items(LPivot *p, int area, int i, const char *name, char *const *items, int n)
{
  int nn, j, k, m;
  PivotField *f = area_field(p, area, i, &nn);
  PivotGroup *ng;
  if (!f || n < 1 || !name || !name[0] || f->group != PG_NONE) return 0;
  /* the items leave the groups they were in (an item is in one group) */
  for (j = 0; j < f->nmg; j++)
    for (k = f->mg[j].n - 1; k >= 0; k--)
      for (m = 0; m < n; m++)
        if (str_ieq(f->mg[j].items[k], items[m])) { free(f->mg[j].items[k]); f->mg[j].items[k] = f->mg[j].items[--f->mg[j].n]; break; }
  for (j = f->nmg - 1; j >= 0; j--)
    if (!f->mg[j].n) { free(f->mg[j].name); free(f->mg[j].items); f->mg[j] = f->mg[--f->nmg]; }
  for (j = 0; j < f->nmg && !str_ieq(f->mg[j].name, name); j++) ;
  if (j == f->nmg) {
    if (!(ng = (PivotGroup *)realloc(f->mg, (size_t)(f->nmg + 1) * sizeof(PivotGroup)))) return 0;
    f->mg = ng;
    memset(&ng[j], 0, sizeof ng[j]);
    ng[j].name = str_dup(name);
    f->nmg++;
  }
  {
    char **ni = (char **)realloc(f->mg[j].items, (size_t)(f->mg[j].n + n) * sizeof(char *));
    if (!ni) return 0;
    f->mg[j].items = ni;
    for (m = 0; m < n; m++) ni[f->mg[j].n++] = str_dup(items[m]);
  }
  return 1;
}

/* ------------------------------------------------------------------ expand and collapse */
int pivot_is_collapsed(const PivotField *f, const char *label)
{
  int i;
  for (i = 0; i < f->ncollapsed; i++) if (str_ieq(f->collapsed[i], label)) return 1;
  return 0;
}
int pivot_set_collapsed(PivotField *f, const char *label, int collapsed)
{
  int i;
  for (i = 0; i < f->ncollapsed; i++)
    if (str_ieq(f->collapsed[i], label)) {
      if (collapsed) return 1;
      free(f->collapsed[i]);
      f->collapsed[i] = f->collapsed[--f->ncollapsed];
      return 1;
    }
  if (collapsed) {
    char **nc = (char **)realloc(f->collapsed, (size_t)(f->ncollapsed + 1) * sizeof(char *));
    if (!nc) return 0;
    f->collapsed = nc;
    nc[f->ncollapsed++] = str_dup(label);
  }
  return 1;
}
int pivot_collapse_field(Book *b, LPivot *p, int area, int i, int collapsed)
{
  int n, k, ni;
  PivotField *f = area_field(p, area, i, &n);
  char **items;
  if (!f || area == PA_FILTERS || i + 1 >= n) return 0;     /* (the innermost field has nothing to hide) */
  strs_free(f->collapsed, f->ncollapsed); f->collapsed = NULL; f->ncollapsed = 0;
  if (!collapsed) return 1;
  ni = pivot_field_items_of(b, p, f->name, f, &items);
  for (k = 0; k < ni; k++) pivot_set_collapsed(f, items[k], 1);
  pivot_strings_free(items, ni);
  return 1;
}
PivotBtn *pivot_button_at(Book *b, int sheet, i32 r, i32 c, LPivot **pp)
{
  Sheet *s;
  int i, k;
  if (sheet < 0 || sheet >= b->nsh) return NULL;
  s = b->sh[sheet];
  for (i = 0; i < s->npivots; i++)
    for (k = 0; k < s->pivots[i].nbtns; k++)
      if (s->pivots[i].btns[k].r == r && s->pivots[i].btns[k].c == c) { if (pp) *pp = &s->pivots[i]; return &s->pivots[i].btns[k]; }
  return NULL;
}
int book_pivot_toggle(Book *b, int sheet, i32 r, i32 c)
{
  LPivot *p, np;
  PivotBtn *bt = pivot_button_at(b, sheet, r, c, &p);
  PivotField *f;
  int n, ok;
  if (!bt) return 0;
  pivot_copy1(&np, p);
  if (!(f = area_field(&np, bt->area, bt->fi, &n))) { pivot_free1(&np); return 0; }
  pivot_set_collapsed(f, bt->label, !bt->collapsed);
  ok = book_pivot_set(b, sheet, pivot_index(b, sheet, p), &np);
  pivot_free1(&np);
  return ok;
}

/* ------------------------------------------------------------------ Recommended PivotTables */
static void sugg_init(PivotSuggest *g, const char *source)
{
  memset(g, 0, sizeof *g);
  snprintf(g->def.source, sizeof g->def.source, "%s", source);
  strcpy(g->def.name, "PivotTable1");
  g->def.layout = PL_COMPACT; g->def.subtotals = 1; g->def.subtop = 1; g->def.grandrows = 1; g->def.grandcols = 1;
  g->def.rowheaders = 1; g->def.colheaders = 1; g->def.out.r1 = -1;
  strcpy(g->def.style, "PivotStyleLight16");
}
/* (written from GridCraft's documented suggestions: engine/src/cmd/pivot/charts.rs) */
int pivot_recommend(Book *b, int sheet, const char *source, PivotSuggest *out, int max)
{
  LPivot tmp;
  int ss, nf, k, ncat = 0, nnum = 0, ndate = 0, n = 0, *cats, *nums, *dates;
  long records = 0;
  Rect src;
  char **names;
  i32 r;
  (void)sheet;
  memset(&tmp, 0, sizeof tmp);
  snprintf(tmp.source, sizeof tmp.source, "%s", source);
  if (!pivot_source(b, &tmp, &ss, &src) || src.r2 <= src.r1) return 0;
  nf = source_fields(b, ss, src, &names);
  cats = (int *)malloc(((size_t)nf + 1) * sizeof(int));
  nums = (int *)malloc(((size_t)nf + 1) * sizeof(int));
  dates = (int *)malloc(((size_t)nf + 1) * sizeof(int));
  if (!cats || !nums || !dates) { free(cats); free(nums); free(dates); pivot_strings_free(names, nf); return 0; }
  for (r = src.r1 + 1; r <= src.r2; r++) {
    i32 c;
    for (c = src.c1; c <= src.c2; c++) if (cell_value(b, ss, r, c)->t != V_EMPTY) { records++; break; }
  }
  if (records < 1) records = 1;
  for (k = 0; k < nf; k++) {
    double mn, mx;
    int t = pivot_field_type(b, &tmp, names[k], &mn, &mx);
    if (t == 2) dates[ndate++] = k;
    else if (t == 1) nums[nnum++] = k;
    else {
      char **items;
      int d = pivot_items(b, &tmp, names[k], &items);
      pivot_strings_free(items, d);
      if (d >= 1 && (d < records || records <= 2) && d <= 1000) cats[ncat++] = k;
    }
  }
#define SUGG(...) do { if (n < max) { sugg_init(&out[n], source); snprintf(out[n].title, sizeof out[n].title, __VA_ARGS__); } } while (0)
  if (ncat && nnum && n < max) {
    SUGG("Sum of %s by %s", names[nums[0]], names[cats[0]]);
    pivot_def_add(&out[n].def, PA_ROWS, names[cats[0]], 0); pivot_def_add(&out[n].def, PA_VALUES, names[nums[0]], PF_SUM); n++;
  }
  if (ncat > 1 && nnum && n < max) {
    SUGG("Sum of %s by %s and %s", names[nums[0]], names[cats[0]], names[cats[1]]);
    pivot_def_add(&out[n].def, PA_ROWS, names[cats[0]], 0); pivot_def_add(&out[n].def, PA_COLS, names[cats[1]], 0);
    pivot_def_add(&out[n].def, PA_VALUES, names[nums[0]], PF_SUM); n++;
  }
  if (ndate && nnum && n < max) {
    SUGG("Sum of %s by %s (quarters)", names[nums[0]], names[dates[0]]);
    pivot_def_add(&out[n].def, PA_ROWS, names[dates[0]], 0); out[n].def.rows[0].group = PG_QUARTERS;
    pivot_def_add(&out[n].def, PA_VALUES, names[nums[0]], PF_SUM); n++;
  }
  if (ncat && nnum > 1 && n < max) {
    SUGG("Sum of %s and %s by %s", names[nums[0]], names[nums[1]], names[cats[0]]);
    pivot_def_add(&out[n].def, PA_ROWS, names[cats[0]], 0); pivot_def_add(&out[n].def, PA_VALUES, names[nums[0]], PF_SUM);
    pivot_def_add(&out[n].def, PA_VALUES, names[nums[1]], PF_SUM); n++;
  }
  if (ncat && n < max) {
    SUGG("Count of %s by %s", names[cats[0]], names[cats[0]]);
    pivot_def_add(&out[n].def, PA_ROWS, names[cats[0]], 0); pivot_def_add(&out[n].def, PA_VALUES, names[cats[0]], PF_COUNT); n++;
  }
  if (ncat > 1 && nnum && n < max) {
    SUGG("Average of %s by %s", names[nums[0]], names[cats[1]]);
    pivot_def_add(&out[n].def, PA_ROWS, names[cats[1]], 0); pivot_def_add(&out[n].def, PA_VALUES, names[nums[0]], PF_AVERAGE); n++;
  }
  if (!ncat && !ndate && nnum && n < max) {
    SUGG("Sum of %s", names[nums[0]]);
    pivot_def_add(&out[n].def, PA_VALUES, names[nums[0]], PF_SUM); n++;
  }
#undef SUGG
  free(cats); free(nums); free(dates);
  pivot_strings_free(names, nf);
  return n;
}
int pivot_preview(Book *b, const LPivot *p, int maxrows, char ***cells, int *rows, int *cols)
{
  LPivot cp;
  Work w;
  Out o;
  const char *why;
  int ok = 0, r, c;
  *cells = NULL; *rows = *cols = 0;
  pivot_copy1(&cp, p);
  memset(&w, 0, sizeof w);
  w.b = b; w.p = &cp;
  if (work_out(&w, &why) && lay_out(&w, &o, &why)) {
    int nr = o.rows < maxrows ? o.rows : maxrows;
    char **t = (char **)calloc((size_t)nr * o.cols + 1, sizeof(char *));
    if (t) {
      for (r = 0; r < nr; r++)
        for (c = 0; c < o.cols; c++) {
          OCell *x = oc(&o, r, c);
          char buf[256], ind[300];
          u32 col;
          buf[0] = 0;
          if (x->v.t != V_EMPTY) fmt_value(b, book_fmt_code(b, x->fmt), &x->v, buf, sizeof buf, &col, 0);
          snprintf(ind, sizeof ind, "%*s%s", x->indent * 2, "", buf);
          t[(long)r * o.cols + c] = str_dup(ind);
        }
      *cells = t; *rows = nr; *cols = o.cols;
      ok = 1;
    }
    out_free(&o);
  }
  work_free(&w);
  pivot_free1(&cp);
  return ok;
}
int book_pivot_chart_add(Book *b, int sheet, int pi, int kind, const char **why)
{
  LPivot *p;
  Sheet *s;
  DObj *o;
  int idx;
  const char *dummy;
  char link[300];
  if (!why) why = &dummy;
  if (sheet < 0 || sheet >= b->nsh || pi < 0 || pi >= b->sh[sheet]->npivots) { *why = "There's no PivotTable there."; return -1; }
  p = &b->sh[sheet]->pivots[pi];
  if (!p->nvals || (!p->nrows && !p->ncols)) { *why = "Add a field to the Values area and one to Rows or Columns before creating a PivotChart."; return -1; }
  s = b->sh[sheet];
  drw_begin(b, sheet);
  if (!(o = drw_new(s, OB_CHART)) || !(o->chart = chart_new(kind >= 0 && kind < CK_N ? kind : CK_COL))) {
    if (o) drw_remove(s, drw_count(s) - 1);
    drw_end(b);
    *why = "Out of memory.";
    return -1;
  }
  idx = drw_count(s) - 1;
  pivot_chart_link(b, sheet, p, link, sizeof link);
  o->chart->pivot = str_dup(link);
  o->chart->has_title = 1;
  o->chart->legend = chart_is_pielike(o->chart->kind) ? LG_RIGHT : LG_BOTTOM;
  o->anchor = AN_TWOCELL;
  o->from.r = p->out.r1 >= 0 ? p->out.r1 : p->r; o->from.c = (p->out.r1 >= 0 ? p->out.c2 : p->c) + 2;
  o->from.dx = o->from.dy = 0;
  o->to.r = o->from.r + 15; o->to.c = o->from.c + 7; o->to.dx = o->to.dy = 0;
  book_pivot_refresh(b, sheet, pi, NULL);
  {
    DObj *ob = drw_obj(s, idx);
    if (ob && ob->chart && ob->chart->ns == 1 && !ob->chart->title) ob->chart->title = str_dup("Total");
  }
  drw_end(b);
  return idx;
}
int pivot_def_add_level(LPivot *p, int area, const char *field, int group)
{
  PivotField f;
  int n, ok;
  if (area != PA_ROWS && area != PA_COLS && area != PA_FILTERS) return 0;
  area_field(p, area, 0, &n);
  if (n >= 16) return 0;
  memset(&f, 0, sizeof f);
  f.name = (char *)field;
  f.group = (u8)group;
  ok = field_insert(p, area, n, &f);
  return ok;
}
