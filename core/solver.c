/*
 * solver.c - Solver (Data > Analysis > Solver, see analysis.h): values for variable cells that
 * maximise, minimise or hit a value in an objective cell, subject to constraints.
 *
 * The workbook is the model: every evaluation puts numbers straight into the variable cells and
 * lets the evaluator work out what depends on them afresh (a new calculation generation), as
 * Goal Seek does, so any formula works; the cells are put back afterwards. Three methods, as
 * GridCraft has them (crates/engine/src/cmd/solver*, MIT licensed; this is C written from its
 * documented behaviour):
 *
 * - Simplex LP: the model is linearised from n + 1 evaluations and checked for linearity at one
 *   more point; a dense two-phase simplex (Dantzig's rule, Bland's when it stalls) solves it
 *   exactly. Integer, binary and all-different constraints use depth-first branch and bound.
 * - GRG Nonlinear (Excel's name for it): an augmented Lagrangian whose inner problems are solved by
 *   projected gradient descent with Barzilai-Borwein steps and an Armijo line search, on
 *   central-difference gradients; bounds are kept by projection. Multistart runs it again from
 *   random points within the bounds and keeps the best. Integers use a small branch and bound.
 * - Evolutionary: a genetic search for models that aren't smooth (tournament selection, blend
 *   crossover, Gaussian mutation within the bounds, feasibility first), then a compass search
 *   around the best member.
 *
 * Work is bounded by an evaluation budget. The model is saved as hidden names scoped to its sheet
 * (solver_opt, solver_adj, solver_lhs1...) as Excel saves it, so it travels in .xlsx files.
 */
#include "analysis.h"
#include "ops.h"
#include "calc.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>
#include <ctype.h>

#define MAX_VARS 200                 /* Excel's limit */
#define MAX_ROWS 1000                /* scalar constraints */
#define DEFAULT_EVALS 20000L
#define MAX_NODES_LP 10000
#define MAX_NODES_NLP 200
#define PRECISION 1e-6               /* Excel's constraint precision */
#define EPS 1e-9
#define LP_WORK 400000000.0          /* tableau cells touched per solve, at most */

static int finite_(double x) { return x == x && x - x == 0; }
static int str_nieq(const char *a, const char *b, size_t n) { return str_ncasecmp(a, b, n) == 0; }
static double dmax(double a, double b) { return a > b ? a : b; }
static double dmin(double a, double b) { return a < b ? a : b; }

/* ------------------------------------------------------------------ the model */
void solver_model_init(SolverModel *m, int sheet)
{
  memset(m, 0, sizeof *m);
  m->sheet = sheet; m->goal = SG_MAX; m->nonneg = 1; m->method = SM_GRG;
}
void solver_model_free(SolverModel *m) { free(m->cons); m->cons = NULL; m->ncons = 0; }
int solver_model_copy(SolverModel *dst, const SolverModel *src)
{
  *dst = *src;
  dst->cons = NULL;
  if (src->ncons) {
    if (!(dst->cons = (SolverCons *)malloc((size_t)src->ncons * sizeof(SolverCons)))) { dst->ncons = 0; return 0; }
    memcpy(dst->cons, src->cons, (size_t)src->ncons * sizeof(SolverCons));
  }
  return 1;
}
static void copy_trim(char *dst, size_t cap, const char *s, size_t n)
{
  while (n && (*s == ' ' || *s == '=')) { s++; n--; }
  while (n && s[n - 1] == ' ') n--;
  if (n >= cap) n = cap - 1;
  memcpy(dst, s, n); dst[n] = 0;
}
int solver_add(SolverModel *m, const char *lhs, int rel, const char *rhs)
{
  SolverCons *nc;
  if (m->ncons >= MAX_ROWS) return 0;
  if (!(nc = (SolverCons *)realloc(m->cons, (size_t)(m->ncons + 1) * sizeof(SolverCons)))) return 0;
  m->cons = nc;
  nc = &m->cons[m->ncons++];
  memset(nc, 0, sizeof *nc);
  copy_trim(nc->lhs, sizeof nc->lhs, lhs, strlen(lhs));
  nc->rel = (u8)rel;
  if (rel <= SR_GE && rhs) copy_trim(nc->rhs, sizeof nc->rhs, rhs, strlen(rhs));
  return 1;
}
void solver_remove(SolverModel *m, int i)
{
  if (i < 0 || i >= m->ncons) return;
  memmove(&m->cons[i], &m->cons[i + 1], (size_t)(m->ncons - i - 1) * sizeof(SolverCons));
  m->ncons--;
}
static const char *const rel_text[] = { "", "<=", "=", ">=", "int", "bin", "dif" };
void solver_cons_text(const SolverCons *c, char *out, size_t cap)
{
  if (c->rel >= SR_INT) snprintf(out, cap, "%s %s", c->lhs, rel_text[c->rel <= SR_DIF ? c->rel : 0]);
  else snprintf(out, cap, "%s %s %s", c->lhs, rel_text[c->rel], c->rhs);
}
int solver_parse_cons(const char *t, SolverCons *c)
{
  static const struct { const char *op; u8 rel; } ops[] = { { "<=", SR_LE }, { ">=", SR_GE }, { "=<", SR_LE }, { "=>", SR_GE }, { "==", SR_EQ } };
  const char *p;
  size_t n = strlen(t);
  int i;
  memset(c, 0, sizeof *c);
  for (i = 0; i < 5; i++)
    if ((p = strstr(t, ops[i].op))) {
      copy_trim(c->lhs, sizeof c->lhs, t, (size_t)(p - t));
      copy_trim(c->rhs, sizeof c->rhs, p + 2, strlen(p + 2));
      c->rel = ops[i].rel;
      return c->lhs[0] && c->rhs[0];
    }
  if ((p = strchr(t, '=')) && p > t) {
    copy_trim(c->lhs, sizeof c->lhs, t, (size_t)(p - t));
    copy_trim(c->rhs, sizeof c->rhs, p + 1, strlen(p + 1));
    c->rel = SR_EQ;
    return c->lhs[0] && c->rhs[0];
  }
  while (n && t[n - 1] == ' ') n--;
  {
    static const struct { const char *w; u8 rel; } words[] = { { "integer", SR_INT }, { "binary", SR_BIN }, { "alldifferent", SR_DIF },
                                                                { "int", SR_INT }, { "bin", SR_BIN }, { "dif", SR_DIF } };
    for (i = 0; i < 6; i++) {
      size_t k = strlen(words[i].w);
      if (n > k && t[n - k - 1] == ' ' && str_nieq(t + n - k, words[i].w, k)) {
        copy_trim(c->lhs, sizeof c->lhs, t, n - k);
        c->rel = words[i].rel;
        return c->lhs[0] != 0;
      }
    }
  }
  return 0;
}

/* ------------------------------------------------------------------ references */
/* the cells of a reference typed "B2:B4,D1" (a name of one will do) on one sheet, row by row in
   each area; at most cap. Returns how many (0 none, -1 bad, -2 too many, -3 more than one sheet) */
static int cells_of(Book *b, int defsheet, const char *text, int *sheet, i32 *rr, i32 *cc, int cap)
{
  char part[200];
  const char *p = text, *e;
  int n = 0, sh = -1;
  while (*p) {
    u16 s; Rect r; u8 fl;
    int used, i, psh;
    i32 x, y;
    char *q;
    e = p;
    { int quote = 0; while (*e && (quote || *e != ',')) { if (*e == '\'') quote = !quote; e++; } }
    copy_trim(part, sizeof part, p, (size_t)(e - p));
    p = *e ? e + 1 : e;
    if (!part[0]) continue;
    used = parse_ref(b, part, &s, &r, &fl);
    if (!used || part[used]) {
      used = 0;
      for (i = 0; i < b->nnames; i++)
        if (str_ieq(b->names[i].name, part) && (b->names[i].sheet == SHEET_SELF || b->names[i].sheet == defsheet) && b->names[i].formula_text) {
          const char *ft = b->names[i].formula_text;
          strncpy(part, ft[0] == '=' ? ft + 1 : ft, sizeof part - 1); part[sizeof part - 1] = 0;
          if ((q = strchr(part, ','))) *q = 0;
          used = parse_ref(b, part, &s, &r, &fl);
          if (used && part[used]) used = 0;
          if (used && s == SHEET_SELF && b->names[i].sheet != SHEET_SELF) s = b->names[i].sheet;
          break;
        }
      if (!used) return -1;
    }
    if (fl & (RF_COLS | RF_ROWS)) return -2;
    psh = s == SHEET_SELF ? defsheet : s;
    if (sh >= 0 && psh != sh) return -3;
    sh = psh;
    if ((long)(r.r2 - r.r1 + 1) * (r.c2 - r.c1 + 1) > cap - n) return -2;
    for (y = r.r1; y <= r.r2; y++)
      for (x = r.c1; x <= r.c2; x++) { rr[n] = y; cc[n] = x; n++; }
  }
  *sheet = sh < 0 ? defsheet : sh;
  return n;
}
static int parse_number_text(const char *s, double *v)
{
  char *end;
  while (*s == ' ' || *s == '=') s++;
  if (!*s) return 0;
  *v = strtod(s, &end);
  while (*end == ' ') end++;
  return *end == 0 && finite_(*v);
}
/* a sheet name as a reference writes it ('My Sheet') */
static void sheet_prefix(const char *name, char *out, size_t cap)
{
  const char *p;
  int plain = name[0] && !isdigit((u8)name[0]);
  size_t o = 0;
  for (p = name; *p; p++) if (!isalnum((u8)*p) && *p != '_' && *p != '.' && (u8)*p < 0x80) plain = 0;
  if (plain) { snprintf(out, cap, "%s!", name); return; }
  out[o++] = '\'';
  for (p = name; *p && o + 4 < cap; p++) { if (*p == '\'') out[o++] = '\''; out[o++] = *p; }
  out[o++] = '\''; out[o++] = '!'; out[o] = 0;
}
/* "B2:B4,D1" as absolute references on the sheet: "Sheet1!$B$2:$B$4,Sheet1!$D$1" (0 if it isn't one) */
static int abs_text(Book *b, int sheet, const char *text, char *out, size_t cap)
{
  char part[200], pre[80], a[24], c[24];
  const char *p = text, *e;
  size_t o = 0;
  sheet_prefix(b->sh[sheet]->name, pre, sizeof pre);
  out[0] = 0;
  while (*p) {
    u16 s; Rect r; u8 fl;
    int used;
    e = p;
    { int quote = 0; while (*e && (quote || *e != ',')) { if (*e == '\'') quote = !quote; e++; } }
    copy_trim(part, sizeof part, p, (size_t)(e - p));
    p = *e ? e + 1 : e;
    if (!part[0]) continue;
    used = parse_ref(b, part, &s, &r, &fl);
    if (!used || part[used]) {
      /* a name: kept as it is */
      if (o + strlen(part) + 2 >= cap) return 0;
      o += (size_t)sprintf(out + o, "%s%s", o ? "," : "", part);
      continue;
    }
    ref_text(r.r1, r.c1, 1, 1, a);
    if (r.r1 == r.r2 && r.c1 == r.c2) c[0] = 0;
    else { c[0] = ':'; ref_text(r.r2, r.c2, 1, 1, c + 1); }
    if (s != SHEET_SELF && s < b->nsh) sheet_prefix(b->sh[s]->name, pre, sizeof pre);
    if (o + strlen(pre) + strlen(a) + strlen(c) + 2 >= cap) return 0;
    o += (size_t)sprintf(out + o, "%s%s%s%s", o ? "," : "", pre, a, c);
  }
  return o > 0;
}
/* the other way: "Sheet1!$B$2:$B$4,Sheet1!$D$1" as "B2:B4,D1" */
static void local_text(const char *text, char *out, size_t cap)
{
  size_t o = 0;
  const char *p = text;
  int quote = 0;
  if (*p == '=') p++;
  while (*p && o + 1 < cap) {
    if (*p == '\'') { quote = !quote; p++; continue; }
    if (*p == '$' && !quote) { p++; continue; }
    if (*p == '!' && !quote) {
      /* drop the sheet name written before it */
      size_t k = o;
      while (k > 0 && out[k - 1] != ',') k--;
      o = k; p++;
      continue;
    }
    out[o++] = *p++;
  }
  out[o] = 0;
}

/* ------------------------------------------------------------------ the saved model */
static void put_name(Book *b, int sheet, const char *name, const char *text) { book_define_name(b, name, text, sheet); }
int solver_save(Book *b, const SolverModel *m)
{
  char t[600], nm[32];
  int i, k;
  if (m->sheet < 0 || m->sheet >= b->nsh) return 0;
  /* the old model's names go */
  for (i = b->nnames - 1; i >= 0; i--)
    if (b->names[i].sheet == (u16)m->sheet && str_nieq(b->names[i].name, "solver_", 7)) book_delete_name(b, i);
  if (abs_text(b, m->sheet, m->objective, t, sizeof t)) put_name(b, m->sheet, "solver_opt", t);
  sprintf(t, "%d", m->goal == SG_MIN ? 2 : m->goal == SG_VALUE ? 3 : 1); put_name(b, m->sheet, "solver_typ", t);
  sprintf(t, "%.15g", m->goal == SG_VALUE ? m->value : 0.0); put_name(b, m->sheet, "solver_val", t);
  if (abs_text(b, m->sheet, m->vars, t, sizeof t)) put_name(b, m->sheet, "solver_adj", t);
  for (i = k = 0; i < m->ncons; i++) {
    const SolverCons *c = &m->cons[i];
    double v;
    if (!abs_text(b, m->sheet, c->lhs, t, sizeof t)) continue;
    k++;
    sprintf(nm, "solver_lhs%d", k); put_name(b, m->sheet, nm, t);
    sprintf(nm, "solver_rel%d", k); sprintf(t, "%d", (int)c->rel); put_name(b, m->sheet, nm, t);
    sprintf(nm, "solver_rhs%d", k);
    if (c->rel == SR_INT) strcpy(t, "\"integer\"");
    else if (c->rel == SR_BIN) strcpy(t, "\"binary\"");
    else if (c->rel == SR_DIF) strcpy(t, "\"AllDifferent\"");
    else if (parse_number_text(c->rhs, &v)) sprintf(t, "%.15g", v);
    else if (!abs_text(b, m->sheet, c->rhs, t, sizeof t)) snprintf(t, sizeof t, "%s", c->rhs);
    put_name(b, m->sheet, nm, t);
  }
  sprintf(t, "%d", k); put_name(b, m->sheet, "solver_num", t);
  put_name(b, m->sheet, "solver_neg", m->nonneg ? "1" : "2");
  put_name(b, m->sheet, "solver_eng", m->method == SM_SIMPLEX ? "2" : m->method == SM_EVO ? "3" : "1");
  put_name(b, m->sheet, "solver_msl", m->multistart ? "1" : "2");
  if (m->population) { sprintf(t, "%d", m->population); put_name(b, m->sheet, "solver_ssz", t); }
  if (m->seed) { sprintf(t, "%lu", (unsigned long)m->seed); put_name(b, m->sheet, "solver_rsd", t); }
  if (m->maxevals) { sprintf(t, "%ld", m->maxevals); put_name(b, m->sheet, "solver_itr", t); }
  if (m->intgap > 0) { sprintf(t, "%.15g", m->intgap / 100); put_name(b, m->sheet, "solver_tol", t); }
  put_name(b, m->sheet, "solver_ver", "3");
  b->dirty = 1;
  return 1;
}
/* the text a saved name stands for ("" none) */
static const char *saved(Book *b, int sheet, const char *name)
{
  int i;
  for (i = 0; i < b->nnames; i++)
    if (str_ieq(b->names[i].name, name) && b->names[i].sheet == (u16)sheet && b->names[i].formula_text) {
      const char *t = b->names[i].formula_text;
      return t[0] == '=' ? t + 1 : t;
    }
  return "";
}
int solver_load(Book *b, int sheet, SolverModel *m)
{
  char nm[32], t[300];
  int i, n;
  const char *v;
  solver_model_init(m, sheet);
  if (sheet < 0 || sheet >= b->nsh || !*(v = saved(b, sheet, "solver_opt"))) return 0;
  local_text(v, m->objective, sizeof m->objective);
  v = saved(b, sheet, "solver_typ");
  m->goal = (u8)(atoi(v) == 2 ? SG_MIN : atoi(v) == 3 ? SG_VALUE : SG_MAX);
  m->value = atof(saved(b, sheet, "solver_val"));
  local_text(saved(b, sheet, "solver_adj"), m->vars, sizeof m->vars);
  n = atoi(saved(b, sheet, "solver_num"));
  for (i = 1; i <= n && i <= MAX_ROWS; i++) {
    char lhs[128], rhs[128];
    int rel;
    sprintf(nm, "solver_lhs%d", i); local_text(saved(b, sheet, nm), lhs, sizeof lhs);
    sprintf(nm, "solver_rel%d", i); rel = atoi(saved(b, sheet, nm));
    sprintf(nm, "solver_rhs%d", i); v = saved(b, sheet, nm);
    if (!lhs[0] || rel < SR_LE || rel > SR_DIF) continue;
    strncpy(t, v, sizeof t - 1); t[sizeof t - 1] = 0;
    { double d; if (parse_number_text(t, &d)) snprintf(rhs, sizeof rhs, "%.15g", d); else local_text(t, rhs, sizeof rhs); }
    solver_add(m, lhs, rel, rhs);
  }
  m->nonneg = atoi(saved(b, sheet, "solver_neg")) != 2;
  v = saved(b, sheet, "solver_eng");
  m->method = (u8)(atoi(v) == 2 ? SM_SIMPLEX : atoi(v) == 3 ? SM_EVO : SM_GRG);
  m->multistart = atoi(saved(b, sheet, "solver_msl")) == 1;
  m->population = atoi(saved(b, sheet, "solver_ssz"));
  m->seed = (u32)strtoul(saved(b, sheet, "solver_rsd"), NULL, 10);
  m->maxevals = atol(saved(b, sheet, "solver_itr"));
  m->intgap = atof(saved(b, sheet, "solver_tol")) * 100;
  return 1;
}
const char *solver_method_name(int method) { return method == SM_SIMPLEX ? "Simplex LP" : method == SM_EVO ? "Evolutionary" : "GRG Nonlinear"; }

/* ------------------------------------------------------------------ the problem */
typedef struct { i32 lr, lc; u8 sense; u8 hasrhs; i32 rr, rc; double rconst; } SRow;
typedef struct {
  Book *b; int sheet;
  int n; i32 *vr, *vc; Value *old; u8 *existed;
  i32 objr, objc;
  SRow *rows; int nrows;
  double *lo, *hi; u8 *integer; int *dif; int ndif;   /* dif[j]: the all-different group (-1 none) */
  long evals, budget;
  int target;                      /* the Limits report: the objective is this variable */
  double *g;                       /* scratch: a row's lhs - rhs */
  u32 rng;
} Ev;

static void ev_free(Ev *e)
{
  free(e->vr); free(e->vc); free(e->old); free(e->existed); free(e->rows); free(e->lo); free(e->hi); free(e->integer); free(e->dif); free(e->g);
  memset(e, 0, sizeof *e);
}
static double as_num(const Value *v) { return v->t == V_NUM ? v->n : v->t == V_EMPTY ? 0 : v->t == V_BOOL ? v->e : NAN; }
/* the variable cells taken over: made if absent, their values kept to put back */
static int ev_begin(Ev *e)
{
  int j;
  Sheet *s = e->b->sh[e->sheet];
  if (!(e->old = (Value *)calloc((size_t)e->n + 1, sizeof(Value))) || !(e->existed = (u8 *)calloc((size_t)e->n + 1, 1))) return 0;
  for (j = 0; j < e->n; j++) {
    Cell *c = sheet_cell(s, e->vr[j], e->vc[j]);
    e->existed[j] = c != NULL;
    if (!c && !(c = sheet_cell_make(s, e->vr[j], e->vc[j]))) return 0;
  }
  for (j = 0; j < e->n; j++) { Cell *c = sheet_cell(s, e->vr[j], e->vc[j]); e->old[j] = c->v; c->v = val_empty(); }
  return 1;
}
static void ev_end(Ev *e)
{
  int j;
  Sheet *s = e->b->sh[e->sheet];
  if (!e->old) return;
  for (j = 0; j < e->n; j++) {
    Cell *c = sheet_cell(s, e->vr[j], e->vc[j]);
    if (!c) { val_free(&e->old[j]); continue; }
    val_free(&c->v);
    c->v = e->old[j];
    if (!e->existed[j]) sheet_cell_clear(s, e->vr[j], e->vc[j]);
  }
  free(e->old); e->old = NULL;
  e->b->calcgen++;
  if (!e->b->calcgen) e->b->calcgen = 1;
}
/* the model at x: the objective, and each row's lhs - rhs in e->g. 0 once the budget is spent */
static int ev_eval(Ev *e, const double *x, double *f)
{
  Sheet *s = e->b->sh[e->sheet];
  int j, i;
  if (e->evals >= e->budget) return 0;
  e->evals++;
  for (j = 0; j < e->n; j++) {
    Cell *c = sheet_cell(s, e->vr[j], e->vc[j]);
    if (c) { val_free(&c->v); c->v = val_num(x[j]); }
  }
  e->b->calcgen++;
  if (!e->b->calcgen) e->b->calcgen = 1;
  *f = e->target >= 0 ? x[e->target] : as_num(cell_value(e->b, e->sheet, e->objr, e->objc));
  for (i = 0; i < e->nrows; i++) {
    SRow *r = &e->rows[i];
    double l = as_num(cell_value(e->b, e->sheet, r->lr, r->lc));
    double rv = r->hasrhs ? as_num(cell_value(e->b, e->sheet, r->rr, r->rc)) : r->rconst;
    e->g[i] = l - rv;
  }
  return 1;
}

/* the model checked and split into variable bounds and general rows. 0 with *why if it is no good */
static int build(Book *b, const SolverModel *m, Ev *e, const char **why)
{
  i32 *rr = NULL, *cc = NULL, *rr2 = NULL, *cc2 = NULL;
  u8 *haslo = NULL;
  int n, k, sh, j, i, ok = 0;
  Cell *cell;
  memset(e, 0, sizeof *e);
  e->b = b; e->target = -1;
  if (m->sheet < 0 || m->sheet >= b->nsh) { *why = "The model's sheet is gone."; return 0; }
  e->sheet = m->sheet;
  rr = (i32 *)malloc(MAX_ROWS * sizeof(i32)); cc = (i32 *)malloc(MAX_ROWS * sizeof(i32));
  rr2 = (i32 *)malloc(MAX_ROWS * sizeof(i32)); cc2 = (i32 *)malloc(MAX_ROWS * sizeof(i32));
  if (!rr || !cc || !rr2 || !cc2) { *why = "Not enough memory."; goto out; }
  if (cells_of(b, m->sheet, m->objective, &sh, rr, cc, 1) != 1 || sh != m->sheet) { *why = "The objective must be a single cell on the model's sheet."; goto out; }
  e->objr = rr[0]; e->objc = cc[0];
  cell = sheet_cell(b->sh[m->sheet], e->objr, e->objc);
  if (!cell || !cell->f) { *why = "The objective cell must contain a formula."; goto out; }
  n = cells_of(b, m->sheet, m->vars, &sh, rr, cc, MAX_VARS);
  if (n == -2) { *why = "Too many variable cells (200 at most)."; goto out; }
  if (n <= 0) { *why = "The variable cells must be a reference like B2:B4."; goto out; }
  if (sh != m->sheet) { *why = "The variable cells must be on the objective cell's sheet."; goto out; }
  e->vr = (i32 *)malloc((size_t)n * sizeof(i32)); e->vc = (i32 *)malloc((size_t)n * sizeof(i32));
  e->lo = (double *)malloc((size_t)n * sizeof(double)); e->hi = (double *)malloc((size_t)n * sizeof(double));
  e->integer = (u8 *)calloc((size_t)n, 1); e->dif = (int *)malloc((size_t)n * sizeof(int)); haslo = (u8 *)calloc((size_t)n, 1);
  if (!e->vr || !e->vc || !e->lo || !e->hi || !e->integer || !e->dif || !haslo) { *why = "Not enough memory."; goto out; }
  for (j = 0; j < n; j++) {
    int d;
    for (d = 0; d < e->n && (e->vr[d] != rr[j] || e->vc[d] != cc[j]); d++) ;
    if (d < e->n) continue;          /* each cell once */
    cell = sheet_cell(b->sh[m->sheet], rr[j], cc[j]);
    if (cell && cell->f) { *why = "Variable cells must hold constants, not formulas."; goto out; }
    if (rr[j] == e->objr && cc[j] == e->objc) { *why = "The objective cell can't be a variable cell."; goto out; }
    e->vr[e->n] = rr[j]; e->vc[e->n] = cc[j];
    e->lo[e->n] = -HUGE_VAL; e->hi[e->n] = HUGE_VAL; e->dif[e->n] = -1;
    e->n++;
  }
  if (!(e->rows = (SRow *)malloc(MAX_ROWS * sizeof(SRow)))) { *why = "Not enough memory."; goto out; }
  for (i = 0; i < m->ncons; i++) {
    const SolverCons *c = &m->cons[i];
    int nl = cells_of(b, m->sheet, c->lhs, &sh, rr, cc, MAX_ROWS), nr = 0, rsh = m->sheet;
    double rconst = 0;
    int isconst = 0;
    if (nl == -2) { *why = "Too many constraint cells (1,000 at most)."; goto out; }
    if (nl <= 0) { *why = "A constraint's cell reference isn't valid."; goto out; }
    if (sh != m->sheet) { *why = "Constraint cells must be on the objective cell's sheet."; goto out; }
    if (c->rel >= SR_INT) {
      int grp = c->rel == SR_DIF ? e->ndif++ : -1;
      for (k = 0; k < nl; k++) {
        for (j = 0; j < e->n && (e->vr[j] != rr[k] || e->vc[j] != cc[k]); j++) ;
        if (j == e->n) { *why = "Integer, binary and all-different constraints must be on variable cells."; goto out; }
        e->integer[j] = 1;
        if (c->rel == SR_BIN) { e->lo[j] = dmax(e->lo[j], 0); e->hi[j] = dmin(e->hi[j], 1); haslo[j] = 1; }
        if (c->rel == SR_DIF) { e->dif[j] = grp; e->lo[j] = dmax(e->lo[j], 1); e->hi[j] = dmin(e->hi[j], nl); haslo[j] = 1; }
      }
      continue;
    }
    if (parse_number_text(c->rhs, &rconst)) isconst = 1;
    else {
      nr = cells_of(b, m->sheet, c->rhs, &rsh, rr2, cc2, MAX_ROWS);
      if (nr <= 0) {
        /* a formula of constants, as Excel allows ("=10*2") */
        Value v = book_eval_text(b, m->sheet, c->rhs[0] == '=' ? c->rhs + 1 : c->rhs);
        if (v.t == V_NUM) { isconst = 1; rconst = v.n; }
        val_free(&v);
        if (!isconst) { *why = "A constraint's right side must be a number or a reference."; goto out; }
      } else if (rsh != m->sheet) { *why = "Constraint cells must be on the objective cell's sheet."; goto out; }
      else if (nr != 1 && nr != nl) { *why = "A constraint's two sides must be the same size (or the right side one cell)."; goto out; }
    }
    for (k = 0; k < nl; k++) {
      for (j = 0; j < e->n && (e->vr[j] != rr[k] || e->vc[j] != cc[k]); j++) ;
      if (j < e->n && isconst) {
        /* a constant bound on a variable cell is a box bound */
        if (c->rel != SR_GE) e->hi[j] = dmin(e->hi[j], rconst);
        if (c->rel != SR_LE) { e->lo[j] = dmax(e->lo[j], rconst); haslo[j] = 1; }
        continue;
      }
      if (e->nrows >= MAX_ROWS) { *why = "Too many constraints (1,000 cells at most)."; goto out; }
      {
        SRow *r = &e->rows[e->nrows++];
        r->lr = rr[k]; r->lc = cc[k]; r->sense = c->rel; r->hasrhs = !isconst; r->rconst = rconst;
        if (!isconst) { r->rr = rr2[nr == 1 ? 0 : k]; r->rc = cc2[nr == 1 ? 0 : k]; }
      }
    }
  }
  if (m->nonneg) for (j = 0; j < e->n; j++) if (!haslo[j]) e->lo[j] = dmax(e->lo[j], 0);
  if (!(e->g = (double *)malloc(((size_t)e->nrows + 1) * sizeof(double)))) { *why = "Not enough memory."; goto out; }
  e->budget = m->maxevals > 0 ? m->maxevals : DEFAULT_EVALS;
  if (e->budget > 2000000L) e->budget = 2000000L;
  ok = 1;
out:
  free(rr); free(cc); free(rr2); free(cc2); free(haslo);
  return ok;
}

/* ------------------------------------------------------------------ Load/Save Model */
/* references as abs_text writes them, without the model sheet's own name ("$B$2:$B$4,$D$1") */
static int abs_local(Book *b, int sheet, const char *text, char *out, size_t cap)
{
  char t[600], pre[80];
  const char *p;
  size_t o = 0, pl;
  if (!abs_text(b, sheet, text, t, sizeof t)) return 0;
  sheet_prefix(b->sh[sheet]->name, pre, sizeof pre);
  pl = strlen(pre);
  for (p = t; *p && o + 1 < cap; ) {
    if ((p == t || p[-1] == ',') && !strncmp(p, pre, pl)) { p += pl; continue; }
    out[o++] = *p++;
  }
  out[o] = 0;
  return 1;
}
static int one_area(const char *t) { int q = 0; for (; *t; t++) { if (*t == '\'') q = !q; else if (*t == ',' && !q) return 0; } return 1; }
/* the model's formulas, one per cell down a column (GridCraft's Save Model: the objective and goal,
   the variable cells, one AND() per constraint that shows TRUE while it holds, the options as an array
   constant); how many (at most cap), 0 with *why if it can't be written */
static int model_rows(Book *b, const SolverModel *m, char rows[][400], int cap, const char **why)
{
  char t[170], u[170];
  int i, n = 0;
  if (!abs_local(b, m->sheet, m->objective, t, sizeof t)) { *why = "Enter the objective cell (Set Objective)."; return 0; }
  if (m->goal == SG_VALUE) snprintf(rows[n++], 400, "=%s=%.15g", t, m->value);
  else snprintf(rows[n++], 400, "=%s(%s)", m->goal == SG_MIN ? "MIN" : "MAX", t);
  if (!abs_local(b, m->sheet, m->vars, t, sizeof t)) { *why = "The variable cells must be a reference like B2:B4."; return 0; }
  snprintf(rows[n++], 400, "=COUNT(%s)", t);
  for (i = 0; i < m->ncons && n < cap - 1; i++) {
    const SolverCons *c = &m->cons[i];
    double v;
    if (!abs_local(b, m->sheet, c->lhs, t, sizeof t) || !one_area(t)) { *why = "Save Model needs each side of a constraint to be one range."; return 0; }
    if (c->rel == SR_INT) snprintf(rows[n++], 400, "=AND(%s=INT(%s))", t, t);
    else if (c->rel == SR_BIN) snprintf(rows[n++], 400, "=AND((%s=0)+(%s=1))", t, t);
    else if (c->rel == SR_DIF) snprintf(rows[n++], 400, "=AND(ROWS(UNIQUE(TOCOL(%s)))=COUNT(%s))", t, t);
    else {
      if (parse_number_text(c->rhs, &v)) snprintf(u, sizeof u, "%.15g", v);
      else if (!abs_local(b, m->sheet, c->rhs, u, sizeof u) || !one_area(u)) { *why = "Save Model needs each side of a constraint to be one range."; return 0; }
      snprintf(rows[n++], 400, "=AND(%s%s%s)", t, rel_text[c->rel], u);
    }
  }
  /* method, non-negative, population, mutation rate, seed, require bounds, convergence, stall
     generations, evaluations, integer optimality (%), and Ledger's Multistart */
  snprintf(rows[n++], 400, "={%d,%d,%d,0.075,%lu,1,0.0001,30,%ld,%.15g,%d}", m->method == SM_SIMPLEX ? 2 : m->method == SM_EVO ? 3 : 1,
           m->nonneg ? 1 : 2, m->population > 0 ? m->population : 100, (unsigned long)m->seed, m->maxevals > 0 ? m->maxevals : DEFAULT_EVALS,
           m->intgap, m->multistart ? 1 : 2);
  return n;
}
#define MODEL_CAP (MAX_ROWS + 3)
int solver_save_model(Book *b, const SolverModel *m, i32 r, i32 c, int overwrite, const char **why)
{
  char (*rows)[400];
  int n, i, k;
  Sheet *s;
  *why = NULL;
  if (m->sheet < 0 || m->sheet >= b->nsh) { *why = "The model's sheet is gone."; return 0; }
  s = b->sh[m->sheet];
  if (!(rows = (char (*)[400])malloc(MODEL_CAP * sizeof *rows))) { *why = "Not enough memory."; return 0; }
  if (!(n = model_rows(b, m, rows, MODEL_CAP, why))) { free(rows); return 0; }
  if (r + n > MAXROW || c + 11 > MAXCOL) { free(rows); *why = "The saved model doesn't fit below that cell."; return 0; }
  /* not over the model's own cells, nor where the options spill */
  {
    Ev e;
    const char *w2;
    if (build(b, m, &e, &w2)) {
      int j;
      for (j = 0; j < e.n; j++) if (e.vc[j] == c && e.vr[j] >= r && e.vr[j] < r + n) { *why = "The range overlaps the model's cells; choose another place to save it."; break; }
      if (e.objc == c && e.objr >= r && e.objr < r + n) *why = "The range overlaps the model's cells; choose another place to save it.";
      for (j = 0; j < e.nrows && !*why; j++) if ((e.rows[j].lc == c && e.rows[j].lr >= r && e.rows[j].lr < r + n) || (e.rows[j].hasrhs && e.rows[j].rc == c && e.rows[j].rr >= r && e.rows[j].rr < r + n))
        *why = "The range overlaps the model's cells; choose another place to save it.";
    }
    ev_free(&e);
    if (*why) { free(rows); return 0; }
  }
  for (k = 1; k < 11; k++) {
    Cell *x = sheet_cell(s, r + n - 1, c + k);
    if (x && (x->f || (x->v.t != V_EMPTY && !x->spill))) { free(rows); *why = "The saved model's options need the 10 cells to the right of its last cell; choose another place to save it."; return 0; }
  }
  if (!overwrite)
    for (i = 0; i < n; i++) {
      Cell *x = sheet_cell(s, r + i, c);
      if (x && (x->f || x->v.t != V_EMPTY) && !x->spill) { free(rows); *why = "overwrite"; return -1; }
    }
  book_begin(b);
  for (i = 0; i < n; i++) book_input(b, m->sheet, r + i, c, rows[i], NULL);
  book_end(b);
  free(rows);
  return n;
}
/* one constraint row's AND(...) inside read back */
static int read_cons(const char *in, SolverCons *c)
{
  char t[300];
  size_t n = strlen(in);
  const char *p;
  int q = 0, depth = 0;
  memset(c, 0, sizeof *c);
  if (n >= sizeof t) return 0;
  if (str_nieq(in, "ROWS(UNIQUE(TOCOL(", 18)) {
    const char *e = strchr(in + 18, ')');
    if (!e) return 0;
    copy_trim(t, sizeof t, in + 18, (size_t)(e - in - 18)); local_text(t, c->lhs, sizeof c->lhs); c->rel = SR_DIF;
    return c->lhs[0] != 0;
  }
  if (in[0] == '(' && (p = strstr(in, "=0)+("))) {
    copy_trim(t, sizeof t, in + 1, (size_t)(p - in - 1)); local_text(t, c->lhs, sizeof c->lhs); c->rel = SR_BIN;
    return c->lhs[0] != 0;
  }
  if ((p = strstr(in, "=INT(")) && in[n - 1] == ')') {
    copy_trim(t, sizeof t, in, (size_t)(p - in)); local_text(t, c->lhs, sizeof c->lhs); c->rel = SR_INT;
    return c->lhs[0] != 0;
  }
  for (p = in; *p; p++) {
    if (*p == '\'' || *p == '"') q = !q;
    if (q) continue;
    if (*p == '(') depth++;
    else if (*p == ')') depth--;
    else if (!depth && (*p == '<' || *p == '>' || *p == '=')) {
      int two = (p[0] == '<' || p[0] == '>') && p[1] == '=';
      c->rel = (u8)(*p == '<' ? SR_LE : *p == '>' ? SR_GE : SR_EQ);
      if ((*p == '<' || *p == '>') && !two) return 0;
      copy_trim(t, sizeof t, in, (size_t)(p - in)); local_text(t, c->lhs, sizeof c->lhs);
      local_text(p + (two ? 2 : 1), c->rhs, sizeof c->rhs);
      return c->lhs[0] && c->rhs[0];
    }
  }
  return 0;
}
/* "NAME(inner)" -> inner (case ignored); 0 if it isn't that call */
static int call_inner(const char *t, const char *name, char *out, size_t cap)
{
  size_t k = strlen(name), n = strlen(t);
  if (n < k + 2 || !str_nieq(t, name, k) || t[k] != '(' || t[n - 1] != ')') return 0;
  copy_trim(out, cap, t + k + 1, n - k - 2);
  return 1;
}
int solver_load_model(Book *b, int sheet, Rect area, SolverModel *m, const char **why)
{
  char t[600], in[600];
  int n, last;
  i32 r;
  *why = "The range doesn't hold a Solver model saved with Save Model.";
  if (sheet < 0 || sheet >= b->nsh) return 0;
  solver_model_init(m, sheet);
  /* a single cell: down from it to the options row */
  last = area.r1 == area.r2 ? area.r1 + MODEL_CAP - 1 : area.r2;
  for (r = area.r1, n = 0; r <= last && r < MAXROW; r++, n++) {
    Cell *x = sheet_cell(b->sh[sheet], r, area.c1);
    if (!x || !x->f) goto bad;
    book_edit_text(b, sheet, r, area.c1, t, sizeof t);
    copy_trim(in, sizeof in, t + 1, strlen(t + 1));
    if (n == 0) {
      char *eq;
      if (call_inner(in, "MAX", t, sizeof t)) { m->goal = SG_MAX; local_text(t, m->objective, sizeof m->objective); }
      else if (call_inner(in, "MIN", t, sizeof t)) { m->goal = SG_MIN; local_text(t, m->objective, sizeof m->objective); }
      else if ((eq = strchr(in, '='))) {
        double v;
        *eq = 0;
        if (!parse_number_text(eq + 1, &v)) goto bad;
        m->goal = SG_VALUE; m->value = v; local_text(in, m->objective, sizeof m->objective);
      } else goto bad;
    } else if (n == 1) {
      if (!call_inner(in, "COUNT", t, sizeof t)) goto bad;
      local_text(t, m->vars, sizeof m->vars);
    } else if (in[0] == '{') {
      double v[16];
      int k = 0;
      char *p = in + 1, *e;
      while (k < 16) {
        v[k] = strtod(p, &e);
        if (e == p) break;
        k++;
        while (*e == ' ') e++;
        if (*e != ',' && *e != ';') break;
        p = e + 1;
      }
      if (k < 8) { *why = "The model's options row doesn't hold the 8 or more numbers Save Model writes."; goto bad; }
      m->method = (u8)(v[0] == 2 ? SM_SIMPLEX : v[0] == 3 ? SM_EVO : SM_GRG);
      m->nonneg = v[1] != 2;
      m->population = v[2] > 0 && v[2] < 100000 ? (int)v[2] : 0;
      if (m->population == 100) m->population = 0;
      m->seed = v[4] > 0 && v[4] < 4294967296.0 ? (u32)v[4] : 0;
      if (k > 8) m->maxevals = v[8] > 0 && v[8] < 1e9 ? (long)v[8] : 0;
      if (m->maxevals == DEFAULT_EVALS) m->maxevals = 0;
      if (k > 9) m->intgap = v[9] >= 0 && v[9] <= 100 ? v[9] : 0;
      if (k > 10) m->multistart = v[10] == 1;
      *why = NULL;
      return 1;
    } else {
      SolverCons c;
      if (!call_inner(in, "AND", t, sizeof t) || !read_cons(t, &c)) goto bad;
      if (!solver_add(m, c.lhs, c.rel, c.rhs)) { *why = "Not enough memory."; goto bad; }
    }
  }
bad:
  if (!*why) *why = "The range doesn't hold a Solver model saved with Save Model.";
  solver_model_free(m);
  solver_model_init(m, sheet);
  return 0;
}
/* ------------------------------------------------------------------ outcomes */
enum { OUT_SOLVED, OUT_INFEASIBLE, OUT_UNBOUNDED, OUT_LIMIT };
/* a relaxation: within lo..hi, x gets the point, *obj the objective to minimise, *conv whether it
   converged (vs. running out of budget) */
typedef int (*Relax)(void *ctx, const double *lo, const double *hi, double *x, double *obj, int *conv);

/* ------------------------------------------------------------------ the simplex */
/* work: tableau cells touched so far (a budget shared by branch and bound) */
static int simplex(double *t, int m, int w, int *basis, double *obj, const u8 *allowed, double *work)
{
  int stalled = 0, W = w + 1;
  for (;;) {
    int j, i, enter = -1, leave = -1;
    double best = -EPS, lr = 0;
    *work += (double)(m + 1) * W;
    if (*work > LP_WORK) return OUT_LIMIT;
    for (j = 0; j < w; j++) {
      if (!allowed[j] || obj[j] >= -EPS) continue;
      if (stalled > 50) { enter = j; break; }
      if (obj[j] < best) { best = obj[j]; enter = j; }
    }
    if (enter < 0) return OUT_SOLVED;
    for (i = 0; i < m; i++) {
      double a = t[i * W + enter], ratio;
      if (a <= EPS) continue;
      ratio = t[i * W + w] / a;
      if (leave < 0 || ratio < lr - 1e-12 || (ratio <= lr + 1e-12 && basis[i] < basis[leave])) { leave = i; lr = ratio; }
    }
    if (leave < 0) return OUT_UNBOUNDED;
    stalled = lr <= EPS ? stalled + 1 : 0;
    {
      double *pr = &t[leave * W], pv = pr[enter], f;
      for (j = 0; j < W; j++) pr[j] /= pv;
      for (i = 0; i < m; i++) {
        double *row = &t[i * W];
        if (i == leave || (f = row[enter]) == 0) continue;
        for (j = 0; j < W; j++) row[j] -= f * pr[j];
      }
      if ((f = obj[enter]) != 0) for (j = 0; j < W; j++) obj[j] -= f * pr[j];
    }
    basis[leave] = enter;
  }
}
/* minimise c.x subject to rows a (m by n, row by row) sense rhs and lo <= x <= hi */
static int lp_solve(int n, const double *c, int m0, const double *a, const u8 *sense, const double *rhs, const double *lo, const double *hi,
                    double *x, double *objv, double *work)
{
  int *cvar = NULL, *basis = NULL, ny = 0, nb = 0, m, w, W, i, j, k, ns = 0, na = 0, slack, art, res = OUT_LIMIT;
  double *csign = NULL, *off = NULL, *t = NULL, *obj = NULL, *nrhs = NULL, *cost = NULL, scale = 1;
  u8 *nsense = NULL, *allowed = NULL;
  for (j = 0; j < n; j++) if (lo[j] > hi[j]) return OUT_INFEASIBLE;
  cvar = (int *)malloc((size_t)(2 * n + 1) * sizeof(int)); csign = (double *)malloc((size_t)(2 * n + 1) * sizeof(double));
  off = (double *)calloc((size_t)n + 1, sizeof(double));
  if (!cvar || !csign || !off) goto out;
  /* x_j = offset + sign * y_k with y >= 0; finite upper bounds of shifted variables become rows */
  for (j = 0; j < n; j++) {
    if (finite_(lo[j])) { off[j] = lo[j]; cvar[ny] = j; csign[ny++] = 1; if (finite_(hi[j])) nb++; }
    else if (finite_(hi[j])) { off[j] = hi[j]; cvar[ny] = j; csign[ny++] = -1; }
    else { cvar[ny] = j; csign[ny++] = 1; cvar[ny] = j; csign[ny++] = -1; }
  }
  m = m0 + nb;
  nsense = (u8 *)malloc((size_t)m + 1); nrhs = (double *)malloc(((size_t)m + 1) * sizeof(double));
  if (!nsense || !nrhs) goto out;
  for (i = 0; i < m0; i++) {
    double sh = 0;
    for (j = 0; j < n; j++) sh += a[i * n + j] * off[j];
    nsense[i] = sense[i]; nrhs[i] = rhs[i] - sh;
  }
  for (i = m0, k = 0; k < ny; k++) if (csign[k] > 0 && finite_(lo[cvar[k]]) && finite_(hi[cvar[k]])) { nsense[i] = SR_LE; nrhs[i] = hi[cvar[k]] - lo[cvar[k]]; i++; }
  /* rows with a negative right side are negated, so every right side is >= 0 */
  for (i = 0; i < m; i++) {
    if (nrhs[i] < 0 && nsense[i] != SR_EQ) nsense[i] = nsense[i] == SR_LE ? SR_GE : SR_LE;
    if (nsense[i] != SR_EQ) ns++;
    if (nsense[i] != SR_LE) na++;
    scale = dmax(scale, fabs(nrhs[i]));
  }
  w = ny + ns + na; W = w + 1;
  t = (double *)calloc((size_t)(m ? m : 1) * W, sizeof(double)); obj = (double *)calloc((size_t)W, sizeof(double));
  basis = (int *)malloc(((size_t)m + 1) * sizeof(int)); allowed = (u8 *)malloc((size_t)W); cost = (double *)calloc((size_t)W, sizeof(double));
  if (!t || !obj || !basis || !allowed || !cost) goto out;
  slack = ny; art = ny + ns;
  for (i = 0; i < m; i++) {
    double *row = &t[i * W], sg = nrhs[i] < 0 ? -1 : 1;
    int sen = nsense[i];
    if (i < m0) for (k = 0; k < ny; k++) row[k] = a[i * n + cvar[k]] * csign[k] * sg;
    else {
      /* the bound rows, in the order made above */
      int bi = i - m0, cnt = 0;
      for (k = 0; k < ny; k++) if (csign[k] > 0 && finite_(lo[cvar[k]]) && finite_(hi[cvar[k]])) { if (cnt == bi) { row[k] = sg; break; } cnt++; }
    }
    row[w] = nrhs[i] * sg;
    if (sen == SR_LE) { row[slack] = 1; basis[i] = slack++; }
    else if (sen == SR_GE) { row[slack++] = -1; row[art] = 1; basis[i] = art++; }
    else { row[art] = 1; basis[i] = art++; }
  }
  /* phase 1: the artificials driven to zero */
  if (na) {
    for (j = ny + ns; j < w; j++) obj[j] = 1;
    for (i = 0; i < m; i++) if (basis[i] >= ny + ns) for (j = 0; j < W; j++) obj[j] -= t[i * W + j];
    for (j = 0; j < w; j++) allowed[j] = 1;
    if ((res = simplex(t, m, w, basis, obj, allowed, work)) == OUT_LIMIT) goto out;
    if (-obj[w] > 1e-7 * scale) { res = OUT_INFEASIBLE; goto out; }
    for (i = 0; i < m; i++) {
      if (basis[i] < ny + ns) continue;
      for (j = 0; j < ny + ns && fabs(t[i * W + j]) <= EPS; j++) ;
      if (j < ny + ns) {
        double *pr = &t[i * W], pv = pr[j], f;
        int q, r2;
        for (q = 0; q < W; q++) pr[q] /= pv;
        for (r2 = 0; r2 < m; r2++) { double *row = &t[r2 * W]; if (r2 == i || (f = row[j]) == 0) continue; for (q = 0; q < W; q++) row[q] -= f * pr[q]; }
        basis[i] = j;
      }
    }
  }
  /* phase 2: the real costs; artificials may not come back */
  for (k = 0; k < ny; k++) cost[k] = c[cvar[k]] * csign[k];
  for (j = 0; j < W; j++) obj[j] = cost[j];
  for (i = 0; i < m; i++) { double cb = basis[i] < W ? cost[basis[i]] : 0; if (cb != 0) for (j = 0; j < W; j++) obj[j] -= cb * t[i * W + j]; }
  for (j = 0; j < w; j++) allowed[j] = j < ny + ns;
  if ((res = simplex(t, m, w, basis, obj, allowed, work)) != OUT_SOLVED) goto out;
  for (j = 0; j < n; j++) x[j] = off[j];
  for (i = 0; i < m; i++) if (basis[i] < ny) x[cvar[basis[i]]] += csign[basis[i]] * t[i * W + w];
  *objv = 0;
  for (j = 0; j < n; j++) *objv += c[j] * x[j];
out:
  free(cvar); free(csign); free(off); free(t); free(obj); free(basis); free(allowed); free(cost); free(nsense); free(nrhs);
  return res;
}

/* ------------------------------------------------------------------ branch and bound */
/* the best whole-number point; dif: all-different groups (values 1..n, each once). *limited set
   when it stopped early */
static int branch_bound(int n, const double *lo, const double *hi, const u8 *integer, const int *dif, int maxnodes, double gap,
                        Relax relax, void *ctx, double *best, double *bestobj, int *limited)
{
  double **stack = NULL, *x = NULL, obj;
  int nst = 0, cap = 0, nodes = 0, have = 0, root = 1, res, conv, j, k;
  *limited = 0;
  if (!(x = (double *)malloc((size_t)n * sizeof(double)))) return OUT_LIMIT;
#define PUSH(L, H) do { double *nd = (double *)malloc((size_t)n * 2 * sizeof(double)); \
    if (!nd) { *limited = 1; break; } memcpy(nd, L, (size_t)n * sizeof(double)); memcpy(nd + n, H, (size_t)n * sizeof(double)); \
    if (nst == cap) { double **ns = (double **)realloc(stack, (size_t)(cap ? cap * 2 : 32) * sizeof(double *)); if (!ns) { free(nd); *limited = 1; break; } stack = ns; cap = cap ? cap * 2 : 32; } \
    stack[nst++] = nd; } while (0)
  PUSH(lo, hi);
  while (nst > 0) {
    double *nd = stack[--nst], *l = nd, *h = nd + n;
    int fj = -1;
    double fbest = 1e-6;
    if (nodes >= maxnodes) { *limited = 1; free(nd); break; }
    nodes++;
    res = relax(ctx, l, h, x, &obj, &conv);
    if (res == OUT_UNBOUNDED && root) { free(nd); while (nst > 0) free(stack[--nst]); free(stack); free(x); return OUT_UNBOUNDED; }
    root = 0;
    if (res == OUT_LIMIT) { *limited = 1; free(nd); break; }
    if (res != OUT_SOLVED) { free(nd); continue; }
    if (!conv) *limited = 1;
    if (have && obj >= *bestobj - gap * dmax(fabs(*bestobj), 1e-12) - 1e-9 * (1 + fabs(*bestobj))) { free(nd); continue; }
    /* the most fractional whole-number variable */
    for (j = 0; j < n; j++) if (integer[j]) { double f = fabs(x[j] - floor(x[j] + 0.5)); if (f > fbest) { fbest = f; fj = j; } }
    if (fj >= 0) {
      double v = x[fj], keep;
      /* the nearer side explored first (pushed last) */
      if (v - floor(v) < 0.5) {
        keep = l[fj]; l[fj] = dmax(l[fj], ceil(v)); PUSH(l, h); l[fj] = keep;
        keep = h[fj]; h[fj] = dmin(h[fj], floor(v)); PUSH(l, h); h[fj] = keep;
      } else {
        keep = h[fj]; h[fj] = dmin(h[fj], floor(v)); PUSH(l, h); h[fj] = keep;
        keep = l[fj]; l[fj] = dmax(l[fj], ceil(v)); PUSH(l, h); l[fj] = keep;
      }
      free(nd);
      continue;
    }
    /* all different: a value two of a group share splits the node */
    if (dif) {
      int dj = -1;
      double dv = 0;
      for (j = 0; j < n && dj < 0; j++) {
        if (dif[j] < 0) continue;
        for (k = j + 1; k < n; k++)
          if (dif[k] == dif[j] && floor(x[k] + 0.5) == floor(x[j] + 0.5)) {
            /* the one that can move */
            dj = h[j] > l[j] ? j : k; dv = floor(x[dj] + 0.5);
            break;
          }
      }
      if (dj >= 0) {
        double keep;
        if (h[dj] <= l[dj]) { free(nd); continue; }
        keep = l[dj]; l[dj] = dmax(l[dj], dv + 1); if (l[dj] <= h[dj]) PUSH(l, h); l[dj] = keep;
        keep = h[dj]; h[dj] = dmin(h[dj], dv - 1); if (l[dj] <= h[dj]) PUSH(l, h); h[dj] = keep;
        free(nd);
        continue;
      }
    }
    for (j = 0; j < n; j++) best[j] = integer[j] ? floor(x[j] + 0.5) : x[j];
    *bestobj = obj; have = 1;
    free(nd);
  }
  while (nst > 0) free(stack[--nst]);
  free(stack); free(x);
#undef PUSH
  if (have) return OUT_SOLVED;
  return *limited ? OUT_LIMIT : OUT_INFEASIBLE;
}

/* (analysis.h) the simplex and branch and bound alone, for the tests */
typedef struct { int n, m; const double *c, *a, *rhs; const u8 *sense; double work; } LPCtx;
static int lp_relax(void *ctx, const double *lo, const double *hi, double *x, double *obj, int *conv)
{
  LPCtx *k = (LPCtx *)ctx;
  *conv = 1;
  return lp_solve(k->n, k->c, k->m, k->a, k->sense, k->rhs, lo, hi, x, obj, &k->work);
}
int solver_lp(int n, const double *c, int m, const double *a, const u8 *sense, const double *rhs,
              const double *lo, const double *hi, const u8 *integer, double *x, double *obj)
{
  LPCtx k;
  int j, any = 0, lim, r;
  k.n = n; k.m = m; k.c = c; k.a = a; k.rhs = rhs; k.sense = sense; k.work = 0;
  for (j = 0; j < n; j++) if (integer && integer[j]) any = 1;
  if (!any) { int conv; r = lp_relax(&k, lo, hi, x, obj, &conv); }
  else r = branch_bound(n, lo, hi, integer, NULL, MAX_NODES_LP, 0, lp_relax, &k, x, obj, &lim);
  return r == OUT_SOLVED ? 0 : r == OUT_INFEASIBLE ? 1 : r == OUT_UNBOUNDED ? 2 : 3;
}

/* ------------------------------------------------------------------ Simplex LP on the model */
typedef struct {
  int n, m; double *c, *a, *rhs; u8 *sense; double work;
} LinModel;
/* the model linearised at x0 (n + 1 evaluations, checked at one more point). 0 fine, 1 not linear
   (or errors), 2 out of budget */
static int linearize(Ev *e, const SolverModel *mdl, const double *x0, LinModel *lm)
{
  int n = e->n, m = e->nrows, i, j, extra = mdl->goal == SG_VALUE;
  double f0, f, *g0 = NULL, *x = NULL, *cf = NULL, fc, *xt = NULL, *k = NULL;
  int res = 2;
  memset(lm, 0, sizeof *lm);
  g0 = (double *)malloc(((size_t)m + 1) * sizeof(double)); x = (double *)malloc((size_t)n * sizeof(double));
  cf = (double *)malloc((size_t)n * sizeof(double)); xt = (double *)calloc((size_t)n + 1, sizeof(double)); k = (double *)malloc(((size_t)m + 1) * sizeof(double));
  lm->a = (double *)calloc((size_t)(m + 1) * n + 1, sizeof(double)); lm->rhs = (double *)malloc(((size_t)m + 2) * sizeof(double));
  lm->sense = (u8 *)malloc((size_t)m + 2); lm->c = (double *)calloc((size_t)n + 1, sizeof(double));
  if (!g0 || !x || !cf || !xt || !k || !lm->a || !lm->rhs || !lm->sense || !lm->c) goto out;
  if (!ev_eval(e, x0, &f0)) goto out;
  res = 1;
  if (!finite_(f0)) goto out;
  for (i = 0; i < m; i++) { g0[i] = e->g[i]; if (!finite_(g0[i])) goto out; }
  res = 2;
  for (j = 0; j < n; j++) {
    memcpy(x, x0, (size_t)n * sizeof(double));
    x[j] += 1;
    if (!ev_eval(e, x, &f)) goto out;
    cf[j] = f - f0;
    for (i = 0; i < m; i++) lm->a[i * n + j] = e->g[i] - g0[i];
  }
  fc = f0;
  for (j = 0; j < n; j++) fc -= cf[j] * x0[j];
  for (i = 0; i < m; i++) { k[i] = g0[i]; for (j = 0; j < n; j++) k[i] -= lm->a[i * n + j] * x0[j]; }
  /* the check at a spread-out point */
  for (j = 0; j < n; j++) xt[j] = x0[j] + (j % 2 == 0 ? 1.7 + 0.3 * (j % 5) : -0.6 - 0.2 * (j % 3));
  if (!ev_eval(e, xt, &f)) goto out;
  res = 1;
  {
    double pred = fc, terms = 0;
    for (j = 0; j < n; j++) { pred += cf[j] * xt[j]; terms += fabs(cf[j] * xt[j]); }
    if (!finite_(f) || fabs(f - pred) > 1e-6 * (1 + fabs(f) + terms)) goto out;
    for (i = 0; i < m; i++) {
      double p = k[i], tm = 0;
      for (j = 0; j < n; j++) { p += lm->a[i * n + j] * xt[j]; tm += fabs(lm->a[i * n + j] * xt[j]); }
      if (!finite_(e->g[i]) || fabs(e->g[i] - p) > 1e-6 * (1 + fabs(e->g[i]) + tm)) goto out;
    }
  }
  for (i = 0; i < m; i++) { lm->sense[i] = e->rows[i].sense; lm->rhs[i] = -k[i]; }
  lm->n = n; lm->m = m;
  if (mdl->goal == SG_VALUE) {
    for (j = 0; j < n; j++) lm->a[m * n + j] = cf[j];
    lm->sense[m] = SR_EQ; lm->rhs[m] = mdl->value - fc; lm->m = m + extra;
  } else for (j = 0; j < n; j++) lm->c[j] = mdl->goal == SG_MAX ? -cf[j] : cf[j];
  res = 0;
out:
  free(g0); free(x); free(cf); free(xt); free(k);
  if (res) { free(lm->a); free(lm->rhs); free(lm->sense); free(lm->c); memset(lm, 0, sizeof *lm); }
  return res;
}
static int lin_relax(void *ctx, const double *lo, const double *hi, double *x, double *obj, int *conv)
{
  LinModel *k = (LinModel *)ctx;
  *conv = 1;
  return lp_solve(k->n, k->c, k->m, k->a, k->sense, k->rhs, lo, hi, x, obj, &k->work);
}

/* ------------------------------------------------------------------ GRG Nonlinear */
typedef struct { Ev *e; u8 goal; double value; double *lam; double mu; double *cons; } Nl;
/* the objective to minimise and the constraints in the <= 0 / = 0 form */
static double shape(Ev *e, int goal, double value, double f, double *cons)
{
  int i;
  for (i = 0; i < e->nrows; i++) cons[i] = e->rows[i].sense == SR_GE ? -e->g[i] : e->g[i];
  return goal == SG_MAX ? -f : goal == SG_MIN ? f : (f - value) * (f - value);
}
static double aug(Ev *e, double phi, const double *cons, const double *lam, double mu)
{
  int i;
  double v = phi;
  for (i = 0; i < e->nrows; i++) {
    double c = cons[i], l = lam[i];
    if (e->rows[i].sense == SR_EQ) v += l * c + 0.5 * mu * c * c;
    else { double t = l + mu * c; if (t < 0) t = 0; v += (t * t - l * l) / (2 * mu); }
  }
  return v;
}
static double violation(Ev *e, const double *cons)
{
  int i;
  double v = 0;
  for (i = 0; i < e->nrows; i++) { double c = e->rows[i].sense == SR_EQ ? fabs(cons[i]) : dmax(cons[i], 0); if (c > v) v = c; }
  return v;
}
static double cscale(Ev *e)
{
  int i;
  double s = 1;
  for (i = 0; i < e->nrows; i++) s = dmax(s, fabs(e->rows[i].rconst));
  return s;
}
static void project(double *x, const double *lo, const double *hi, int n)
{
  int j;
  for (j = 0; j < n; j++) { if (x[j] < lo[j]) x[j] = lo[j]; if (x[j] > hi[j]) x[j] = hi[j]; }
}
/* the augmented Lagrangian at x (0: out of budget) */
static int lval_at(Ev *e, int goal, double value, const double *x, const double *lam, double mu, double *cons, double *phi, double *lv)
{
  double f;
  if (!ev_eval(e, x, &f)) return 0;
  *phi = shape(e, goal, value, f, cons);
  *lv = aug(e, *phi, cons, lam, mu);
  return 1;
}
static int gradient(Ev *e, int goal, double value, const double *x, const double *lo, const double *hi, const double *lam, double mu,
                    double *gr, double *xa, double *ca)
{
  int j, n = e->n;
  for (j = 0; j < n; j++) {
    double xj = x[j], h = 1e-6 * dmax(fabs(xj), 1), a = xj - h >= lo[j] ? xj - h : xj, bb = xj + h <= hi[j] ? xj + h : xj, pa, pb, la, lb, d;
    gr[j] = 0;
    if (bb <= a) continue;
    memcpy(xa, x, (size_t)n * sizeof(double));
    xa[j] = a;
    if (!lval_at(e, goal, value, xa, lam, mu, ca, &pa, &la)) return 0;
    xa[j] = bb;
    if (!lval_at(e, goal, value, xa, lam, mu, ca, &pb, &lb)) return 0;
    d = (lb - la) / (bb - a);
    gr[j] = finite_(d) ? d : 0;
  }
  return 1;
}
/* a local minimum within lo..hi from x0 (GridCraft's nlp) */
static int nlp(Ev *e, int goal, double value, const double *x0, const double *lo, const double *hi, double *xout, double *objout, int *conv)
{
  int n = e->n, m = e->nrows, outer, inner, ls, res = OUT_LIMIT, converged = 0, oob = 0, have_alpha = 0, j, i;
  double *x = NULL, *lam = NULL, *cons = NULL, *g = NULL, *gn = NULL, *xn = NULL, *cn = NULL, *s = NULL, *xa = NULL, *ca = NULL;
  double mu = 10, phi, phi0, prevviol, lcur, alpha = 1, carried = 0, viol, scale = cscale(e), f;
  *conv = 0;
  x = (double *)malloc((size_t)n * sizeof(double) + 8); lam = (double *)calloc((size_t)m + 1, sizeof(double));
  cons = (double *)malloc(((size_t)m + 1) * sizeof(double)); cn = (double *)malloc(((size_t)m + 1) * sizeof(double)); ca = (double *)malloc(((size_t)m + 1) * sizeof(double));
  g = (double *)malloc((size_t)n * sizeof(double) + 8); gn = (double *)malloc((size_t)n * sizeof(double) + 8);
  xn = (double *)malloc((size_t)n * sizeof(double) + 8); s = (double *)malloc((size_t)n * sizeof(double) + 8); xa = (double *)malloc((size_t)n * sizeof(double) + 8);
  if (!x || !lam || !cons || !cn || !ca || !g || !gn || !xn || !s || !xa) goto out;
  memcpy(x, x0, (size_t)n * sizeof(double));
  project(x, lo, hi, n);
  if (!ev_eval(e, x, &f)) goto out;
  phi = shape(e, goal, value, f, cons);
  if (!finite_(phi)) { res = OUT_INFEASIBLE; goto out; }
  phi0 = phi;
  prevviol = violation(e, cons);
  for (outer = 0; outer < 40; outer++) {
    double gnorm = 0;
    int inner_done = 0, quiet = 0;
    lcur = aug(e, phi, cons, lam, mu);
    if (!gradient(e, goal, value, x, lo, hi, lam, mu, g, xa, ca)) { oob = 1; break; }
    for (j = 0; j < n; j++) gnorm = dmax(gnorm, fabs(g[j]));
    alpha = have_alpha ? carried : gnorm > 0 ? 1 / gnorm : 1;
    for (inner = 0; inner < 500; inner++) {
      int accepted = 0;
      double a = alpha, phn = 0, ln = 0, step = 0, xmax = 1, sy = 0, ss = 0;
      for (ls = 0; ls < 40; ls++) {
        double dec = 0;
        for (j = 0; j < n; j++) xn[j] = x[j] - a * g[j];
        project(xn, lo, hi, n);
        if (!lval_at(e, goal, value, xn, lam, mu, cn, &phn, &ln)) break;
        for (j = 0; j < n; j++) dec += g[j] * (x[j] - xn[j]);
        if (finite_(ln) && ln <= lcur - 1e-4 * dec) { accepted = 1; break; }
        a *= 0.25;
      }
      if (e->evals >= e->budget) oob = 1;
      if (!accepted) { inner_done = 1; break; }
      /* variables or objective running off to infinity: unbounded */
      if (fabs(phn) > 1e15 * (1 + fabs(phi0))) { res = OUT_UNBOUNDED; goto out; }
      for (j = 0; j < n; j++) if (fabs(xn[j]) > 1e15 * (1 + fabs(x0[j]))) { res = OUT_UNBOUNDED; goto out; }
      for (j = 0; j < n; j++) { s[j] = xn[j] - x[j]; step = dmax(step, fabs(s[j])); xmax = dmax(xmax, fabs(xn[j])); }
      quiet = (step <= 1e-10 * xmax || fabs(lcur - ln) <= 1e-13 * (1 + fabs(lcur))) ? quiet + 1 : 0;
      memcpy(x, xn, (size_t)n * sizeof(double));
      memcpy(cons, cn, (size_t)m * sizeof(double));
      phi = phn; lcur = ln;
      if (quiet >= 3) { inner_done = 1; break; }
      if (!gradient(e, goal, value, x, lo, hi, lam, mu, gn, xa, ca)) { oob = 1; break; }
      /* the Barzilai-Borwein step */
      for (j = 0; j < n; j++) { double yv = gn[j] - g[j]; sy += s[j] * yv; ss += s[j] * s[j]; }
      alpha = sy > 0 ? dmin(dmax(ss / sy, 1e-12), 1e12) : dmin(a * 4, 1e12);
      memcpy(g, gn, (size_t)n * sizeof(double));
      for (j = 0; j < n && g[j] == 0; j++) ;
      if (j == n) { inner_done = 1; break; }
    }
    carried = alpha; have_alpha = 1;
    if (oob) break;
    viol = violation(e, cons);
    if (viol <= PRECISION * scale && inner_done) { converged = 1; if (!m) break; }
    for (i = 0; i < m; i++) {
      double l = lam[i] + mu * cons[i];
      lam[i] = e->rows[i].sense == SR_EQ ? l : dmax(l, 0);
    }
    if (viol > 0.25 * prevviol) mu = dmin(mu * 10, 1e10);
    if (converged && viol <= PRECISION * scale * 1e-2) break;
    prevviol = viol;
    converged = 0;
  }
  viol = violation(e, cons);
  if (viol > PRECISION * scale * 10) { res = oob ? OUT_LIMIT : OUT_INFEASIBLE; goto out; }
  memcpy(xout, x, (size_t)n * sizeof(double));
  *objout = phi;
  *conv = (converged || viol <= PRECISION * scale) && !oob;
  res = OUT_SOLVED;
out:
  free(x); free(lam); free(cons); free(cn); free(ca); free(g); free(gn); free(xn); free(s); free(xa);
  return res;
}

/* random numbers (a small LCG: the same seed gives the same run) */
static double rnd(Ev *e) { e->rng = e->rng * 1664525UL + 1013904223UL; return ((e->rng >> 8) & 0xFFFFFF) / 16777216.0; }
static double gauss(Ev *e)
{
  double u = rnd(e), v = rnd(e);
  if (u < 1e-12) u = 1e-12;
  return sqrt(-2 * log(u)) * cos(6.283185307179586 * v);
}

typedef struct { Ev *e; u8 goal; double value; const double *x0; int multistart, starts; } NlCtx;
/* GRG from x0 (projected into lo..hi), and with multistart from random points too: the best */
static int nl_relax(void *ctx, const double *lo, const double *hi, double *x, double *obj, int *conv)
{
  NlCtx *k = (NlCtx *)ctx;
  Ev *e = k->e;
  int n = e->n, res, r2, c2, s, j, have = 0;
  double *start, *y, o2;
  if (!(start = (double *)malloc((size_t)n * 2 * sizeof(double) + 8))) return OUT_LIMIT;
  y = start + n;
  memcpy(start, k->x0, (size_t)n * sizeof(double));
  project(start, lo, hi, n);
  res = nlp(e, k->goal, k->value, start, lo, hi, x, obj, conv);
  if (res == OUT_SOLVED) have = 1;
  if (res == OUT_UNBOUNDED || !k->multistart) { free(start); return res; }
  for (s = 0; s < k->starts && e->evals < e->budget; s++) {
    for (j = 0; j < n; j++) {
      double l = lo[j], h = hi[j], c = k->x0[j], w = 10 * dmax(1, fabs(c));
      if (finite_(l) && finite_(h)) start[j] = l + (h - l) * rnd(e);
      else if (finite_(l)) start[j] = l + fabs(gauss(e)) * dmax(w, fabs(c - l));
      else if (finite_(h)) start[j] = h - fabs(gauss(e)) * dmax(w, fabs(h - c));
      else start[j] = c + gauss(e) * w;
    }
    r2 = nlp(e, k->goal, k->value, start, lo, hi, y, &o2, &c2);
    if (r2 == OUT_UNBOUNDED) { free(start); return r2; }
    if (r2 == OUT_SOLVED && (!have || o2 < *obj - 1e-12 * (1 + fabs(*obj)))) {
      memcpy(x, y, (size_t)n * sizeof(double)); *obj = o2; *conv = c2; have = 1;
    }
  }
  free(start);
  if (have) { if (e->evals >= e->budget) *conv = 0; return OUT_SOLVED; }
  return res;
}

/* ------------------------------------------------------------------ Evolutionary */
/* a member's ranking: feasible first, then the objective */
static int better(double v1, double p1, double v2, double p2, double tol)
{
  if (v1 <= tol && v2 <= tol) return p1 < p2;
  if (v1 <= tol) return 1;
  if (v2 <= tol) return 0;
  return v1 < v2;
}
/* whole numbers rounded, all-different groups made a permutation (by rank) */
static void repair(Ev *e, double *x)
{
  int n = e->n, j, k, g;
  double rk[MAX_VARS];
  for (j = 0; j < n; j++) {
    if (e->integer[j]) x[j] = floor(x[j] + 0.5);
    if (x[j] < e->lo[j]) x[j] = e->lo[j];
    if (x[j] > e->hi[j]) x[j] = e->hi[j];
  }
  for (g = 0; g < e->ndif; g++) {
    for (j = 0; j < n; j++) {
      int rank = 1;
      if (e->dif[j] != g) continue;
      for (k = 0; k < n; k++) if (e->dif[k] == g && k != j && (x[k] < x[j] || (x[k] == x[j] && k < j))) rank++;
      rk[j] = rank;
    }
    for (j = 0; j < n; j++) if (e->dif[j] == g) x[j] = rk[j];
  }
}
static int evo_eval(Ev *e, int goal, double value, const double *x, double *phi, double *viol, double *cons)
{
  double f;
  if (!ev_eval(e, x, &f)) return 0;
  *phi = shape(e, goal, value, f, cons);
  if (!finite_(*phi)) *phi = HUGE_VAL;
  *viol = violation(e, cons);
  if (!finite_(*viol)) *viol = HUGE_VAL;
  return 1;
}
/* *stall: it stopped because the best stopped improving (else converged or out of budget) */
static int evolutionary(Ev *e, int goal, double value, const double *x0, int pop, double *best, double *bestphi, int *conv, int *stall)
{
  int n = e->n, P = pop < 10 ? 10 : pop > 400 ? 400 : pop, i, j, gen, nostall = 0, have = 0, res = OUT_LIMIT;
  double *X = NULL, *Y = NULL, *phi = NULL, *viol = NULL, *nphi = NULL, *nviol = NULL, *cons = NULL, *t = NULL, tol;
  double bphi = HUGE_VAL, bviol = HUGE_VAL;
  *conv = 0; *stall = 0;
  tol = PRECISION * cscale(e);
  X = (double *)malloc((size_t)P * n * sizeof(double) + 8); Y = (double *)malloc((size_t)P * n * sizeof(double) + 8);
  phi = (double *)malloc((size_t)P * sizeof(double)); viol = (double *)malloc((size_t)P * sizeof(double));
  nphi = (double *)malloc((size_t)P * sizeof(double)); nviol = (double *)malloc((size_t)P * sizeof(double));
  cons = (double *)malloc(((size_t)e->nrows + 1) * sizeof(double)); t = (double *)malloc((size_t)n * sizeof(double) + 8);
  if (!X || !Y || !phi || !viol || !nphi || !nviol || !cons || !t) goto out;
  for (i = 0; i < P; i++) {
    double *xi = &X[i * n];
    for (j = 0; j < n; j++) xi[j] = i == 0 ? x0[j] : e->lo[j] + (e->hi[j] - e->lo[j]) * rnd(e);
    repair(e, xi);
    if (!evo_eval(e, goal, value, xi, &phi[i], &viol[i], cons)) goto done;
    if (!have || better(viol[i], phi[i], bviol, bphi, tol)) { memcpy(best, xi, (size_t)n * sizeof(double)); bphi = phi[i]; bviol = viol[i]; have = 1; }
  }
  for (gen = 0; gen < 100000 && e->evals < e->budget; gen++) {
    int improved = 0, bi = 0;
    double spread = 0;
    for (i = 1; i < P; i++) if (better(viol[i], phi[i], viol[bi], phi[bi], tol)) bi = i;
    /* the best is kept; the rest are children */
    memcpy(&Y[0], &X[bi * n], (size_t)n * sizeof(double)); nphi[0] = phi[bi]; nviol[0] = viol[bi];
    for (i = 1; i < P; i++) {
      int a1 = (int)(rnd(e) * P) % P, a2 = (int)(rnd(e) * P) % P, b1 = (int)(rnd(e) * P) % P, b2 = (int)(rnd(e) * P) % P;
      int pa = better(viol[a1], phi[a1], viol[a2], phi[a2], tol) ? a1 : a2, pb = better(viol[b1], phi[b1], viol[b2], phi[b2], tol) ? b1 : b2;
      double *c = &Y[i * n];
      for (j = 0; j < n; j++) {
        double u = X[pa * n + j], v = X[pb * n + j], lo = dmin(u, v), hi = dmax(u, v), d = hi - lo;
        c[j] = lo - 0.5 * d + rnd(e) * 2 * d;      /* blend crossover (BLX-0.5) */
        if (rnd(e) < 0.075) c[j] += gauss(e) * 0.1 * (e->hi[j] - e->lo[j]);   /* mutation */
      }
      repair(e, c);
      if (!evo_eval(e, goal, value, c, &nphi[i], &nviol[i], cons)) { P = i; break; }
    }
    memcpy(X, Y, (size_t)P * n * sizeof(double));
    memcpy(phi, nphi, (size_t)P * sizeof(double)); memcpy(viol, nviol, (size_t)P * sizeof(double));
    for (i = 0; i < P; i++)
      if (better(viol[i], phi[i], bviol, bphi, tol)) {
        if (bviol > tol || phi[i] < bphi - 1e-9 * (1 + fabs(bphi))) improved = 1;
        memcpy(best, &X[i * n], (size_t)n * sizeof(double)); bphi = phi[i]; bviol = viol[i];
      }
    nostall = improved ? 0 : nostall + 1;
    for (j = 0; j < n; j++) {
      double lo = HUGE_VAL, hi = -HUGE_VAL;
      for (i = 0; i < P; i++) { lo = dmin(lo, X[i * n + j]); hi = dmax(hi, X[i * n + j]); }
      spread = dmax(spread, (hi - lo) / (1 + e->hi[j] - e->lo[j]));
    }
    if (spread < 1e-4) { *conv = 1; break; }
    if (nostall >= 50) { *stall = 1; break; }
  }
  /* a compass search around the best */
  {
    double step = 0.1;
    while (step > 1e-7 && e->evals < e->budget) {
      int moved = 0;
      for (j = 0; j < n && e->evals < e->budget; j++) {
        int d;
        for (d = -1; d <= 1; d += 2) {
          double p, v;
          memcpy(t, best, (size_t)n * sizeof(double));
          t[j] += d * (e->integer[j] ? dmax(1, floor(step * (e->hi[j] - e->lo[j]) + 0.5)) : step * (e->hi[j] - e->lo[j]));
          repair(e, t);
          if (t[j] == best[j]) continue;
          if (!evo_eval(e, goal, value, t, &p, &v, cons)) break;
          if (better(v, p, bviol, bphi, tol)) { memcpy(best, t, (size_t)n * sizeof(double)); bphi = p; bviol = v; moved = 1; }
        }
      }
      if (!moved) step /= 4;
    }
  }
done:
  if (have && bviol <= tol * 10) { *bestphi = bphi; res = OUT_SOLVED; }
  else res = have && e->evals < e->budget ? OUT_INFEASIBLE : OUT_LIMIT;
out:
  free(X); free(Y); free(phi); free(viol); free(nphi); free(nviol); free(cons); free(t);
  return res;
}

/* ------------------------------------------------------------------ solving */
typedef struct { int outcome, notlinear, conv, stall; } Run;
/* the model's method for goal within lo..hi from x0; x the answer */
static Run optimise(Ev *e, const SolverModel *m, int goal, double value, const double *x0, const double *lo, const double *hi, double *x, double *obj)
{
  Run r;
  int j, anyint = 0, lim = 0;
  double gap = m->intgap > 0 ? m->intgap / 100 : 0;
  memset(&r, 0, sizeof r);
  for (j = 0; j < e->n; j++) { if (lo[j] > hi[j]) { r.outcome = OUT_INFEASIBLE; return r; } if (e->integer[j]) anyint = 1; }
  if (m->method == SM_SIMPLEX) {
    SolverModel g = *m;
    LinModel lm;
    int k;
    g.goal = (u8)goal; g.value = value;
    k = linearize(e, &g, x0, &lm);
    if (k == 1) { r.notlinear = 1; r.outcome = OUT_INFEASIBLE; return r; }
    if (k == 2) { r.outcome = OUT_LIMIT; return r; }
    if (anyint || e->ndif) r.outcome = branch_bound(e->n, lo, hi, e->integer, e->dif, MAX_NODES_LP, gap, lin_relax, &lm, x, obj, &lim);
    else r.outcome = lin_relax(&lm, lo, hi, x, obj, &r.conv);
    r.conv = !lim;
    free(lm.a); free(lm.rhs); free(lm.sense); free(lm.c);
  } else if (m->method == SM_GRG) {
    NlCtx k;
    k.e = e; k.goal = (u8)goal; k.value = value; k.x0 = x0; k.multistart = m->multistart;
    k.starts = m->population > 0 ? m->population : 20;
    if (anyint || e->ndif) { r.outcome = branch_bound(e->n, lo, hi, e->integer, e->dif, MAX_NODES_NLP, gap, nl_relax, &k, x, obj, &lim); r.conv = !lim; }
    else r.outcome = nl_relax(&k, lo, hi, x, obj, &r.conv);
  } else {
    double *l2 = (double *)malloc((size_t)e->n * sizeof(double) + 8), *h2 = (double *)malloc((size_t)e->n * sizeof(double) + 8);
    double *slo = e->lo, *shi = e->hi;
    if (!l2 || !h2) { free(l2); free(h2); r.outcome = OUT_LIMIT; return r; }
    memcpy(l2, lo, (size_t)e->n * sizeof(double)); memcpy(h2, hi, (size_t)e->n * sizeof(double));
    e->lo = l2; e->hi = h2;
    r.outcome = evolutionary(e, goal, value, x0, m->population > 0 ? m->population : 100, x, obj, &r.conv, &r.stall);
    e->lo = slo; e->hi = shi;
    free(l2); free(h2);
  }
  return r;
}
void solver_result_free(SolverResult *r)
{
  free(r->vr); free(r->vc); free(r->orig); free(r->sol);
  memset(r, 0, sizeof *r);
}
static void say(SolverResult *r, int status, const char *msg) { r->status = status; snprintf(r->message, sizeof r->message, "%s", msg); }
int solver_solve(Book *b, const SolverModel *m, SolverResult *r)
{
  Ev e;
  const char *why = NULL;
  double *x0 = NULL, *x = NULL, obj = 0, f;
  int j, manual = b->manualcalc;
  Run run;
  memset(r, 0, sizeof *r);
  if (!build(b, m, &e, &why)) { say(r, SS_BADMODEL, why); ev_free(&e); return r->status; }
  if (m->method == SM_EVO)
    for (j = 0; j < e.n; j++)
      if (!finite_(e.lo[j]) || !finite_(e.hi[j])) {
        say(r, SS_BADMODEL, "The Evolutionary method needs a lower and an upper bound on every variable cell (constraints such as A1:A3 <= 10).");
        ev_free(&e);
        return r->status;
      }
  r->n = e.n;
  r->vr = (i32 *)malloc((size_t)e.n * sizeof(i32)); r->vc = (i32 *)malloc((size_t)e.n * sizeof(i32));
  r->orig = (double *)malloc((size_t)e.n * sizeof(double)); x0 = (double *)malloc((size_t)e.n * sizeof(double)); x = (double *)malloc((size_t)e.n * sizeof(double));
  if (!r->vr || !r->vc || !r->orig || !x0 || !x) { free(x0); free(x); ev_free(&e); say(r, SS_BADMODEL, "Not enough memory."); return r->status; }
  for (j = 0; j < e.n; j++) {
    double v = as_num(cell_value(b, m->sheet, e.vr[j], e.vc[j]));
    r->vr[j] = e.vr[j]; r->vc[j] = e.vc[j]; r->orig[j] = v;
    x0[j] = finite_(v) ? v : 0;
  }
  r->objorig = as_num(cell_value(b, m->sheet, e.objr, e.objc));
  e.rng = m->seed ? m->seed : 12345u;
  b->manualcalc = 0;
  if (!ev_begin(&e)) { ev_end(&e); b->manualcalc = manual; free(x0); free(x); ev_free(&e); say(r, SS_BADMODEL, "Not enough memory."); return r->status; }
  run = optimise(&e, m, m->goal, m->value, x0, e.lo, e.hi, x, &obj);
  if (run.notlinear) say(r, SS_NOTLINEAR, "The linearity conditions required by this LP Solver are not satisfied. Try GRG Nonlinear.");
  else if (run.outcome == OUT_SOLVED && m->method == SM_EVO)
    say(r, run.stall ? SS_CANNOTIMPROVE : run.conv ? SS_CONVERGED : SS_LIMITFEAS,
        run.stall ? "Solver cannot improve the current solution. All Constraints are satisfied." :
        run.conv ? "Solver has converged to the current solution. All Constraints are satisfied." :
        "Solver stopped at its evaluation limit; the best solution found satisfies the constraints.");
  else if (run.outcome == OUT_SOLVED && run.conv)
    say(r, m->method == SM_SIMPLEX ? SS_OPTIMAL : SS_CONVERGED, m->method == SM_SIMPLEX ?
        "Solver found a solution. All Constraints and optimality conditions are satisfied." :
        "Solver found a solution. All Constraints and optimality conditions are satisfied (a local optimum).");
  else if (run.outcome == OUT_SOLVED) say(r, SS_LIMITFEAS, "Solver stopped at its evaluation limit; the best solution found satisfies the constraints.");
  else if (run.outcome == OUT_LIMIT) say(r, SS_LIMIT, "Solver stopped at its evaluation limit without finding a feasible solution.");
  else if (run.outcome == OUT_UNBOUNDED) say(r, SS_UNBOUNDED, "The Objective Cell values do not converge: the problem is unbounded.");
  else say(r, SS_INFEASIBLE, "Solver could not find a feasible solution.");
  if (run.outcome == OUT_SOLVED && !run.notlinear) {
    r->sol = x; x = NULL;
    for (j = 0; j < e.n; j++) {
      /* (whole numbers and tiny rounding shown clean) */
      if (e.integer[j]) r->sol[j] = floor(r->sol[j] + 0.5);
      else if (fabs(r->sol[j]) < 1e-12) r->sol[j] = 0;
    }
    e.budget = e.evals + 1;
    e.target = -1;
    r->objective = ev_eval(&e, r->sol, &f) ? f : NAN;
  }
  r->evals = e.evals;
  ev_end(&e);
  b->manualcalc = manual;
  free(x0); free(x);
  ev_free(&e);
  return r->status;
}
int solver_finish(Book *b, const SolverModel *m, const SolverResult *r, int keep, int save)
{
  int j;
  if (keep && r->sol) {
    book_begin(b);
    for (j = 0; j < r->n; j++) { Value v = val_num(r->sol[j]); book_set_value(b, m->sheet, r->vr[j], r->vc[j], &v); }
    book_end(b);
  }
  if (save) solver_save(b, m);
  if (!b->manualcalc) book_recalc(b);
  return 1;
}

/* ------------------------------------------------------------------ reports */
/* a cell's name for the reports: the text left of it and above it */
static void cell_label(Book *b, int sheet, i32 r, i32 c, char *out, size_t cap)
{
  i32 k;
  const char *left = NULL, *top = NULL;
  for (k = c - 1; k >= 0 && !left; k--) { const Value *v = cell_value(b, sheet, r, k); if (v->t == V_STR && v->s[0]) left = v->s; }
  for (k = r - 1; k >= 0 && !top; k--) { const Value *v = cell_value(b, sheet, k, c); if (v->t == V_STR && v->s[0]) top = v->s; }
  snprintf(out, cap, "%s%s%s", left ? left : "", left && top ? " " : "", top ? top : "");
}
static void put_text(Book *b, int sh, i32 r, i32 c, const char *t) { Value v = val_str(t); book_set_value(b, sh, r, c, &v); val_free(&v); }
static void put_num(Book *b, int sh, i32 r, i32 c, double x) { Value v = finite_(x) ? val_num(x) : val_err(E_VALUE); book_set_value(b, sh, r, c, &v); }
static void bold_edit(Style *st, Font *f, const void *arg) { (void)st; (void)arg; f->bold = 1; }
static void bold(Book *b, int sh, i32 r, i32 c1, i32 c2) { Rect a; a.r1 = a.r2 = r; a.c1 = c1; a.c2 = c2; book_restyle(b, sh, a, bold_edit, NULL); }
static void absref(i32 r, i32 c, char *out) { ref_text(r, c, 1, 1, out); }
/* (cellctl.c too) a new sheet at index at, or at the end when sheet-scoped names would lose their
   sheet (inserting doesn't move a name's scope) */
int an_add_sheet(Book *b, const char *base, int at)
{
  char nm[32];
  int i;
  for (i = 0; i < b->nnames; i++) if (b->names[i].sheet != SHEET_SELF && b->names[i].sheet >= at) { at = b->nsh; break; }
  if (!base) return book_add_sheet(b, NULL, at);
  book_unique_sheet_name(b, base, nm);
  return book_add_sheet(b, nm, at);
}
/* ------------------------------------------------------------------ the Sensitivity report */
/* n x n system a x = r solved in place by elimination with partial pivoting (x in r); 0 if singular */
static int lsolve(int n, double *a, double *r)
{
  int i, j, k;
  double big = 0;
  for (i = 0; i < n * n; i++) big = dmax(big, fabs(a[i]));
  for (k = 0; k < n; k++) {
    int p = k;
    for (i = k + 1; i < n; i++) if (fabs(a[i * n + k]) > fabs(a[p * n + k])) p = i;
    if (fabs(a[p * n + k]) <= 1e-11 * (big > 0 ? big : 1)) return 0;
    if (p != k) { double t; for (j = 0; j < n; j++) { t = a[k * n + j]; a[k * n + j] = a[p * n + j]; a[p * n + j] = t; } t = r[k]; r[k] = r[p]; r[p] = t; }
    for (i = k + 1; i < n; i++) {
      double f = a[i * n + k] / a[k * n + k];
      if (f == 0) continue;
      for (j = k; j < n; j++) a[i * n + j] -= f * a[k * n + j];
      r[i] -= f * r[k];
    }
  }
  for (k = n - 1; k >= 0; k--) { double t = r[k]; for (j = k + 1; j < n; j++) t -= a[k * n + j] * r[j]; r[k] = t / a[k * n + k]; }
  return 1;
}
#define SENS_INF 1e30
typedef struct {
  double *gf;                      /* the objective's gradient (the coefficients for a linear model) */
  double *rc;                      /* each variable's reduced cost (gradient) */
  double *oinc, *odec;             /* Simplex: the objective coefficient's allowable increase and decrease */
  double *sp;                      /* each constraint row's shadow price (Lagrange multiplier) */
  double *rinc, *rdec;             /* Simplex: the right side's allowable increase and decrease (NAN: not known) */
} Sens;
/* the active constraints at x, the normals of each (rows: the row's gradient; bounds: a unit
   vector), and the multipliers' signs for maximising (+1 >= 0, -1 <= 0, 0 either). Excel's Sensitivity
   report from them: shadow prices by least squares over the free variables (constraints whose
   multiplier has the wrong sign dropped, as GridCraft does), and for a linear model at a vertex the
   ranges from the active set's square system */
static int sensitivity(Ev *e, const SolverModel *m, const double *x, int linear, Sens *o)
{
  int n = e->n, mr = e->nrows, i, j, k, na = 0, ok = 0, nf;
  double sigma = m->goal == SG_MIN ? -1 : 1, f0, f1, f2;
  double *G = NULL, *gv = NULL, *xx = NULL, *N = NULL, *sg = NULL, *M = NULL, *r = NULL, *w = NULL;
  int *kind = NULL, *idx = NULL, *fr = NULL;      /* active k: kind 0 a row (idx its row), 1 a lower bound, 2 an upper bound (idx the variable) */
  u8 *drop = NULL;
  G = (double *)calloc((size_t)mr * n + 1, sizeof(double)); gv = (double *)calloc((size_t)mr + 1, sizeof(double));
  xx = (double *)malloc(((size_t)n + 1) * sizeof(double));
  kind = (int *)malloc(((size_t)mr + 2 * n + 1) * sizeof(int)); idx = (int *)malloc(((size_t)mr + 2 * n + 1) * sizeof(int));
  sg = (double *)malloc(((size_t)mr + 2 * n + 1) * sizeof(double)); fr = (int *)malloc(((size_t)n + 1) * sizeof(int));
  drop = (u8 *)calloc((size_t)mr + 2 * n + 1, 1);
  if (!G || !gv || !xx || !kind || !idx || !sg || !fr || !drop) goto out;
  for (j = 0; j < n; j++) { o->rc[j] = 0; o->oinc[j] = o->odec[j] = NAN; }
  for (i = 0; i < mr; i++) { o->sp[i] = 0; o->rinc[i] = o->rdec[i] = NAN; }
  if (!ev_eval(e, x, &f0)) goto out;
  memcpy(gv, e->g, (size_t)mr * sizeof(double));
  /* gradients: a unit step for a linear model (exact), central differences otherwise */
  for (j = 0; j < n; j++) {
    double h = linear ? 1 : 1e-6 * (1 + fabs(x[j]));
    memcpy(xx, x, (size_t)n * sizeof(double));
    xx[j] = x[j] + h;
    if (!ev_eval(e, xx, &f1)) goto out;
    for (i = 0; i < mr; i++) G[i * n + j] = e->g[i];
    if (linear) { o->gf[j] = f1 - f0; for (i = 0; i < mr; i++) G[i * n + j] -= gv[i]; continue; }
    xx[j] = x[j] - h;
    if (!ev_eval(e, xx, &f2)) goto out;
    o->gf[j] = (f1 - f2) / (2 * h);
    for (i = 0; i < mr; i++) G[i * n + j] = (G[i * n + j] - e->g[i]) / (2 * h);
  }
  ev_eval(e, x, &f0);
  /* the active set */
  for (i = 0; i < mr; i++) {
    double sc = PRECISION * (1 + fabs(as_num(cell_value(e->b, e->sheet, e->rows[i].lr, e->rows[i].lc))));
    if (e->rows[i].sense == SR_EQ || fabs(gv[i]) <= sc) { kind[na] = 0; idx[na] = i; sg[na] = e->rows[i].sense == SR_LE ? 1 : e->rows[i].sense == SR_GE ? -1 : 0; na++; }
  }
  for (j = 0; j < n; j++) {
    int atlo = finite_(e->lo[j]) && fabs(x[j] - e->lo[j]) <= PRECISION * (1 + fabs(e->lo[j]));
    int athi = finite_(e->hi[j]) && fabs(x[j] - e->hi[j]) <= PRECISION * (1 + fabs(e->hi[j]));
    if (atlo && athi) { kind[na] = 1; idx[na] = j; sg[na] = 0; na++; }          /* fixed: either sign */
    else if (atlo) { kind[na] = 1; idx[na] = j; sg[na] = -1; na++; }
    else if (athi) { kind[na] = 2; idx[na] = j; sg[na] = 1; na++; }
  }
  /* the multipliers of the active rows by least squares on the variables not at a bound */
  for (j = nf = 0; j < n; j++) {
    int at = 0;
    for (k = 0; k < na; k++) if (kind[k] && idx[k] == j) at = 1;
    if (!at) fr[nf++] = j;
  }
  {
    int nr = 0, pass;
    for (k = 0; k < na; k++) nr += kind[k] == 0;
    M = (double *)malloc(((size_t)nr * nr + 1) * sizeof(double)); r = (double *)malloc(((size_t)nr + 1) * sizeof(double));
    w = (double *)malloc(((size_t)n * n + n + 1) * sizeof(double));
    if (!M || !r || !w) goto out;
    for (pass = 0; pass <= nr; pass++) {
      int rows[MAX_ROWS], q = 0, a2, b2, worst = -1;
      double wv = 0;
      for (k = 0; k < na; k++) if (kind[k] == 0 && !drop[k]) rows[q++] = k;
      for (i = 0; i < mr; i++) o->sp[i] = 0;
      if (!q) break;
      for (a2 = 0; a2 < q; a2++) {
        const double *ga = &G[idx[rows[a2]] * n];
        double t = 0;
        for (j = 0; j < nf; j++) t += ga[fr[j]] * sigma * o->gf[fr[j]];
        r[a2] = t;
        for (b2 = 0; b2 < q; b2++) {
          const double *gb = &G[idx[rows[b2]] * n];
          double u = 0;
          for (j = 0; j < nf; j++) u += ga[fr[j]] * gb[fr[j]];
          M[a2 * q + b2] = u + (a2 == b2 ? 1e-12 : 0);
        }
      }
      if (!lsolve(q, M, r)) for (a2 = 0; a2 < q; a2++) r[a2] = 0;
      for (a2 = 0; a2 < q; a2++) {
        double v = r[a2] * sg[rows[a2]];
        if (v < -1e-9 * (1 + fabs(r[a2])) && -v > wv) { wv = -v; worst = rows[a2]; }
        o->sp[idx[rows[a2]]] = sigma * r[a2];
      }
      if (worst < 0) break;
      drop[worst] = 1;
    }
  }
  /* reduced costs: the objective's gradient less the active rows' share */
  for (j = 0; j < n; j++) {
    double t = o->gf[j];
    for (i = 0; i < mr; i++) if (o->sp[i] != 0) t -= o->sp[i] * G[i * n + j];
    o->rc[j] = fabs(t) <= 1e-9 * (1 + fabs(o->gf[j])) ? 0 : t;
  }
  for (j = 0; j < nf; j++) o->rc[fr[j]] = 0;
  /* the ranges, for a linear model at a nondegenerate vertex */
  if (linear && na == n && (N = (double *)malloc(((size_t)n * n + 1) * sizeof(double)))) {
    double *nu = w, *t1 = w + n;
    int good = 1;
    /* the normals N (row k: active constraint k), multipliers nu: N^T nu = sigma gf */
    for (k = 0; k < n; k++) for (j = 0; j < n; j++) N[j * n + k] = kind[k] == 0 ? G[idx[k] * n + j] : (idx[k] == j);
    for (j = 0; j < n; j++) nu[j] = sigma * o->gf[j];
    if (!lsolve(n, N, nu)) good = 0;
    for (k = 0; k < n && good; k++) if (nu[k] * sg[k] < -1e-9 * (1 + fabs(nu[k]))) good = 0;   /* not optimal there */
    if (good) {
      for (k = 0; k < n; k++) {
        if (kind[k] == 0) o->sp[idx[k]] = sigma * nu[k];
        else o->rc[idx[k]] = sigma * nu[k];
      }
      for (j = 0; j < n; j++) if (o->rc[j] == 0) { for (k = 0; k < n; k++) if (kind[k] && idx[k] == j) o->rc[j] = sigma * nu[k]; }
      /* the objective coefficient of j changed by delta: nu + sigma delta w, N^T w = e_j */
      for (j = 0; j < n; j++) {
        double up = SENS_INF, dn = SENS_INF;
        for (k = 0; k < n; k++) for (i = 0; i < n; i++) N[i * n + k] = kind[k] == 0 ? G[idx[k] * n + i] : (idx[k] == i);
        for (i = 0; i < n; i++) t1[i] = i == j;
        if (!lsolve(n, N, t1)) continue;
        for (k = 0; k < n; k++) {
          double wk = sigma * t1[k];
          if (!sg[k] || fabs(wk) < 1e-12) continue;
          /* need sg[k] * (nu[k] + delta * wk) >= 0 */
          if (sg[k] * wk < 0) up = dmin(up, fabs(nu[k] / wk));
          else dn = dmin(dn, fabs(nu[k] / wk));
        }
        o->oinc[j] = up; o->odec[j] = dn;
      }
      /* a row's right side changed by t: x + t d, N d = e_k; until another constraint or bound is met */
      for (i = 0; i < mr; i++) {
        int kk = -1, g;
        for (k = 0; k < n; k++) if (kind[k] == 0 && idx[k] == i) kk = k;
        if (kk < 0) {
          double slack = -gv[i];
          if (e->rows[i].sense == SR_LE) { o->rinc[i] = SENS_INF; o->rdec[i] = slack; }
          else { o->rinc[i] = -slack; o->rdec[i] = SENS_INF; }
          continue;
        }
        for (k = 0; k < n; k++) for (j = 0; j < n; j++) N[k * n + j] = kind[k] == 0 ? G[idx[k] * n + j] : (idx[k] == j);
        for (k = 0; k < n; k++) t1[k] = k == kk;
        if (!lsolve(n, N, t1)) continue;
        for (g = 0; g < 2; g++) {
          double dir = g ? -1 : 1, lim = SENS_INF;
          int i2;
          /* the right side up by t moves lhs - rhs of row i by -t: x moves by -t d... as the row stays
             binding, lhs follows; the other rows' and the free variables' room */
          for (i2 = 0; i2 < mr; i2++) {
            double rate = 0;
            int act = 0;
            for (k = 0; k < n; k++) if (kind[k] == 0 && idx[k] == i2) act = 1;
            if (act) continue;
            for (j = 0; j < n; j++) rate += G[i2 * n + j] * t1[j] * dir;
            if (e->rows[i2].sense == SR_LE && rate > 1e-12) lim = dmin(lim, -gv[i2] / rate);
            if (e->rows[i2].sense == SR_GE && rate < -1e-12) lim = dmin(lim, gv[i2] / -rate);
          }
          for (j = 0; j < n; j++) {
            double rate = t1[j] * dir;
            if (rate > 1e-12 && finite_(e->hi[j])) lim = dmin(lim, (e->hi[j] - x[j]) / rate);
            if (rate < -1e-12 && finite_(e->lo[j])) lim = dmin(lim, (x[j] - e->lo[j]) / -rate);
          }
          if (lim < 0) lim = 0;
          if (g) o->rdec[i] = lim; else o->rinc[i] = lim;
        }
      }
    }
  }
  ok = 1;
out:
  free(G); free(gv); free(xx); free(kind); free(idx); free(sg); free(fr); free(drop); free(N); free(M); free(r); free(w);
  return ok;
}
/* whether a Sensitivity report applies to the model (no integers, not Evolutionary, Max or Min) */
int solver_sensitivity_applies(const SolverModel *m, const char **why)
{
  int i;
  for (i = 0; i < m->ncons; i++) if (m->cons[i].rel >= SR_INT) { *why = "Sensitivity Reports are not meaningful on problems with integer constraints."; return 0; }
  if (m->method == SM_EVO) { *why = "The Sensitivity report is available for Simplex LP and GRG Nonlinear."; return 0; }
  if (m->goal == SG_VALUE) { *why = "The Sensitivity report needs a Max or Min objective."; return 0; }
  return 1;
}

int solver_report(Book *b, const SolverModel *m, const SolverResult *res, int kind, const char **why)
{
  Ev e;
  int at, sh, i, j, k, manual = b->manualcalc;
  i32 row;
  char t[300], lab[200], ref[24], base[32];
  double *vals = NULL, f;
  *why = NULL;
  if (!res->sol) { *why = "There is no solution to report."; return -1; }
  if (b->lockstruct) { *why = "The workbook is protected and cannot be changed."; return -1; }
  if (!build(b, m, &e, why)) { ev_free(&e); return -1; }
  if (kind == 2 && !solver_sensitivity_applies(m, why)) { ev_free(&e); return -1; }
  if (kind == 1) {
    for (j = 0; j < e.n; j++) if (e.integer[j]) { *why = "The Limits report doesn't apply to models with integer, binary or all-different constraints."; ev_free(&e); return -1; }
    if (m->method == SM_EVO) { *why = "The Limits report is available for Simplex LP and GRG Nonlinear."; ev_free(&e); return -1; }
  }
  sh = m->sheet;
  b->manualcalc = 0;
  /* the values at the solution: the objective, the constraints' cells */
  if (!(vals = (double *)malloc(((size_t)e.nrows * 5 + 4 + (size_t)e.n * 5) * sizeof(double))) || !ev_begin(&e)) {
    ev_end(&e); free(vals); ev_free(&e); b->manualcalc = manual; *why = "Not enough memory."; return -1;
  }
  e.budget = 1L << 30;
  ev_eval(&e, res->sol, &f);
  vals[0] = f;
  for (i = 0; i < e.nrows; i++) {
    vals[1 + 2 * i] = as_num(cell_value(b, sh, e.rows[i].lr, e.rows[i].lc));
    vals[2 + 2 * i] = e.rows[i].hasrhs ? as_num(cell_value(b, sh, e.rows[i].rr, e.rows[i].rc)) : e.rows[i].rconst;
  }
  if (kind == 1) {
    /* each variable pushed to its lowest and highest with the others' constraints kept */
    double *x = (double *)malloc((size_t)e.n * 3 * sizeof(double) + 8), *lo, *hi, obj;
    long per = (m->maxevals > 0 ? m->maxevals : DEFAULT_EVALS) / (2 * e.n + 1) + e.n + 3;
    if (x) {
      lo = x + e.n; hi = lo + e.n;
      for (j = 0; j < e.n; j++) {
        int g;
        memcpy(lo, res->sol, (size_t)e.n * sizeof(double)); memcpy(hi, res->sol, (size_t)e.n * sizeof(double));
        lo[j] = e.lo[j]; hi[j] = e.hi[j];
        for (g = 0; g < 2; g++) {
          Run rn;
          double *slot = &vals[1 + 2 * e.nrows + 4 * j + 2 * g];
          e.target = j; e.evals = 0; e.budget = per;
          rn = optimise(&e, m, g == 0 ? SG_MIN : SG_MAX, 0, res->sol, lo, hi, x, &obj);
          e.target = -1; e.budget = e.evals + 1;
          if (rn.outcome == OUT_SOLVED && ev_eval(&e, x, &f)) { slot[0] = x[j]; slot[1] = f; }
          else { slot[0] = rn.outcome == OUT_UNBOUNDED ? HUGE_VAL : NAN; slot[1] = NAN; }
        }
      }
      free(x);
    }
  }
  if (kind == 2) {
    /* vals: after the rows' values, gf rc oinc odec per variable, then sp rinc rdec per row */
    Sens sn;
    double *v2 = vals + 1 + 2 * e.nrows;
    sn.gf = v2; sn.rc = v2 + e.n; sn.oinc = v2 + 2 * e.n; sn.odec = v2 + 3 * e.n;
    sn.sp = v2 + 4 * e.n; sn.rinc = sn.sp + e.nrows; sn.rdec = sn.rinc + e.nrows;
    e.budget = e.evals + 4L * e.n + 8;
    if (!sensitivity(&e, m, res->sol, m->method == SM_SIMPLEX, &sn)) {
      ev_end(&e); free(vals); ev_free(&e); b->manualcalc = manual; *why = "Not enough memory."; return -1;
    }
    ev_eval(&e, res->sol, &f);
  }
  ev_end(&e);
  b->manualcalc = manual;
  book_begin(b);
  book_note_book(b);
  {
    static const char *const kinds[] = { "Answer", "Limits", "Sensitivity" };
    for (k = 1; k < 1000; k++) { sprintf(base, "%s Report %d", kinds[kind], k); if (book_find_sheet(b, base) < 0) break; }
  }
  if ((at = an_add_sheet(b, base, sh)) < 0) { book_end(b); free(vals); ev_free(&e); *why = "A new sheet couldn't be added."; return -1; }
  if (at <= sh) sh++;
  b->manualcalc = 1;
  snprintf(t, sizeof t, "Ledger %s Report", kind == 2 ? "Sensitivity" : kind ? "Limits" : "Answer");
  put_text(b, at, 0, 0, t); bold(b, at, 0, 0, 0);
  snprintf(t, sizeof t, "Worksheet: %s", b->sh[sh]->name); put_text(b, at, 1, 0, t);
  row = 3;
  if (kind == 0) {
    static const char *const h1[] = { "Cell", "Name", "Original Value", "Final Value" };
    static const char *const h3[] = { "Cell", "Name", "Cell Value", "Formula", "Status", "Slack" };
    snprintf(t, sizeof t, "Result: %s", res->message); put_text(b, at, row++, 0, t);
    snprintf(t, sizeof t, "Engine: %s", solver_method_name(m->method)); put_text(b, at, row, 0, t);
    row += 2;
    if (m->goal == SG_VALUE) snprintf(t, sizeof t, "Objective Cell (Value Of %.15g)", m->value);
    else snprintf(t, sizeof t, "Objective Cell (%s)", m->goal == SG_MIN ? "Min" : "Max");
    put_text(b, at, row, 0, t); bold(b, at, row, 0, 0); row++;
    for (k = 0; k < 4; k++) put_text(b, at, row, 1 + k, h1[k]);
    bold(b, at, row, 1, 4); row++;
    absref(e.objr, e.objc, ref); put_text(b, at, row, 1, ref);
    cell_label(b, sh, e.objr, e.objc, lab, sizeof lab); if (lab[0]) put_text(b, at, row, 2, lab);
    put_num(b, at, row, 3, res->objorig); put_num(b, at, row, 4, vals[0]);
    row += 3;
    put_text(b, at, row, 0, "Variable Cells"); bold(b, at, row, 0, 0); row++;
    for (k = 0; k < 4; k++) put_text(b, at, row, 1 + k, h1[k]);
    put_text(b, at, row, 5, "Integer"); bold(b, at, row, 1, 5); row++;
    for (j = 0; j < e.n; j++, row++) {
      int isbin = 0, c2;
      absref(e.vr[j], e.vc[j], ref); put_text(b, at, row, 1, ref);
      cell_label(b, sh, e.vr[j], e.vc[j], lab, sizeof lab); if (lab[0]) put_text(b, at, row, 2, lab);
      put_num(b, at, row, 3, res->orig[j]); put_num(b, at, row, 4, res->sol[j]);
      for (c2 = 0; c2 < m->ncons; c2++) if (m->cons[c2].rel == SR_BIN && e.lo[j] == 0 && e.hi[j] == 1) isbin = 1;
      put_text(b, at, row, 5, e.dif[j] >= 0 ? "AllDiff" : isbin && e.integer[j] ? "Binary" : e.integer[j] ? "Integer" : "Contin");
    }
    row += 2;
    put_text(b, at, row, 0, "Constraints"); bold(b, at, row, 0, 0); row++;
    for (k = 0; k < 6; k++) put_text(b, at, row, 1 + k, h3[k]);
    bold(b, at, row, 1, 6); row++;
    for (i = 0; i < e.nrows; i++, row++) {
      double v = vals[1 + 2 * i], rv = vals[2 + 2 * i], slack = fabs(v - rv);
      int binding = e.rows[i].sense == SR_EQ || slack <= PRECISION * (1 + fabs(rv));
      char rs[40];
      absref(e.rows[i].lr, e.rows[i].lc, ref); put_text(b, at, row, 1, ref);
      cell_label(b, sh, e.rows[i].lr, e.rows[i].lc, lab, sizeof lab); if (lab[0]) put_text(b, at, row, 2, lab);
      put_num(b, at, row, 3, v);
      if (e.rows[i].hasrhs) absref(e.rows[i].rr, e.rows[i].rc, rs); else snprintf(rs, sizeof rs, "%.15g", rv);
      snprintf(t, sizeof t, "%s%s%s", ref, rel_text[e.rows[i].sense], rs); put_text(b, at, row, 4, t);
      put_text(b, at, row, 5, binding ? "Binding" : "Not Binding");
      put_num(b, at, row, 6, binding ? 0 : slack);
    }
    /* the variable bounds as constraints too */
    for (j = 0; j < e.n; j++) {
      int g;
      for (g = 0; g < 2; g++) {
        double bnd = g ? e.hi[j] : e.lo[j], v = res->sol[j];
        if (!finite_(bnd)) continue;
        absref(e.vr[j], e.vc[j], ref); put_text(b, at, row, 1, ref);
        cell_label(b, sh, e.vr[j], e.vc[j], lab, sizeof lab); if (lab[0]) put_text(b, at, row, 2, lab);
        put_num(b, at, row, 3, v);
        snprintf(t, sizeof t, "%s%s%.15g", ref, g ? "<=" : ">=", bnd); put_text(b, at, row, 4, t);
        put_text(b, at, row, 5, fabs(v - bnd) <= PRECISION * (1 + fabs(bnd)) ? "Binding" : "Not Binding");
        put_num(b, at, row, 6, fabs(v - bnd) <= PRECISION * (1 + fabs(bnd)) ? 0 : fabs(v - bnd));
        row++;
      }
      if (e.integer[j]) {
        absref(e.vr[j], e.vc[j], ref); put_text(b, at, row, 1, ref);
        put_num(b, at, row, 3, res->sol[j]);
        snprintf(t, sizeof t, "%s=%s", ref, e.dif[j] >= 0 ? "AllDifferent" : e.lo[j] == 0 && e.hi[j] == 1 ? "Binary" : "Integer");
        put_text(b, at, row, 4, t); put_text(b, at, row, 5, "Binding"); put_num(b, at, row, 6, 0);
        row++;
      }
    }
  } else if (kind == 2) {
    /* Excel's: the variables' final values and reduced costs, the constraints' shadow prices; for
       Simplex LP the ranges, for GRG reduced gradients and Lagrange multipliers */
    int lp = m->method == SM_SIMPLEX;
    double *v2 = vals + 1 + 2 * e.nrows, *sp = v2 + 4 * e.n, *rinc = sp + e.nrows, *rdec = rinc + e.nrows;
    static const char *const hv[] = { "Cell", "Name", "Final Value", "Reduced Cost", "Objective Coefficient", "Allowable Increase", "Allowable Decrease" };
    static const char *const hc[] = { "Cell", "Name", "Final Value", "Shadow Price", "Constraint R.H. Side", "Allowable Increase", "Allowable Decrease" };
    int cols = lp ? 7 : 4;
    snprintf(t, sizeof t, "Engine: %s", solver_method_name(m->method)); put_text(b, at, row, 0, t);
    row += 2;
    put_text(b, at, row, 0, "Variable Cells"); bold(b, at, row, 0, 0); row++;
    for (k = 0; k < cols; k++) put_text(b, at, row, 1 + k, k == 3 && !lp ? "Reduced Gradient" : hv[k]);
    bold(b, at, row, 1, cols); row++;
    for (j = 0; j < e.n; j++, row++) {
      absref(e.vr[j], e.vc[j], ref); put_text(b, at, row, 1, ref);
      cell_label(b, sh, e.vr[j], e.vc[j], lab, sizeof lab); if (lab[0]) put_text(b, at, row, 2, lab);
      put_num(b, at, row, 3, res->sol[j]); put_num(b, at, row, 4, v2[e.n + j]);
      if (lp) {
        put_num(b, at, row, 5, v2[j]);
        for (k = 0; k < 2; k++) { double d = v2[(2 + k) * e.n + j]; if (d == d) put_num(b, at, row, 6 + k, d); else { Value na = val_err(E_NA); book_set_value(b, at, row, 6 + k, &na); } }
      }
    }
    row += 2;
    put_text(b, at, row, 0, "Constraints"); bold(b, at, row, 0, 0); row++;
    for (k = 0; k < cols; k++) put_text(b, at, row, 1 + k, k == 3 && !lp ? "Lagrange Multiplier" : hc[k]);
    bold(b, at, row, 1, cols); row++;
    for (i = 0; i < e.nrows; i++, row++) {
      absref(e.rows[i].lr, e.rows[i].lc, ref); put_text(b, at, row, 1, ref);
      cell_label(b, sh, e.rows[i].lr, e.rows[i].lc, lab, sizeof lab); if (lab[0]) put_text(b, at, row, 2, lab);
      put_num(b, at, row, 3, vals[1 + 2 * i]); put_num(b, at, row, 4, sp[i]);
      if (lp) {
        put_num(b, at, row, 5, vals[2 + 2 * i]);
        for (k = 0; k < 2; k++) { double d = (k ? rdec : rinc)[i]; if (d == d) put_num(b, at, row, 6 + k, d); else { Value na = val_err(E_NA); book_set_value(b, at, row, 6 + k, &na); } }
      }
    }
    /* bounds on the variable cells typed as constraints (B1 <= 4), as Excel lists them; not the
       non-negativity Ledger adds */
    for (j = 0; j < e.n; j++) {
      int g;
      for (g = 0; g < 2; g++) {
        double bnd = g ? e.hi[j] : e.lo[j], v = res->sol[j];
        int at_b = fabs(v - bnd) <= PRECISION * (1 + fabs(bnd));
        if (!finite_(bnd) || (!g && bnd == 0 && m->nonneg)) continue;
        absref(e.vr[j], e.vc[j], ref); put_text(b, at, row, 1, ref);
        cell_label(b, sh, e.vr[j], e.vc[j], lab, sizeof lab); if (lab[0]) put_text(b, at, row, 2, lab);
        put_num(b, at, row, 3, v); put_num(b, at, row, 4, at_b ? v2[e.n + j] : 0);
        if (lp) {
          put_num(b, at, row, 5, bnd);
          if (at_b) { Value na = val_err(E_NA); book_set_value(b, at, row, 6, &na); book_set_value(b, at, row, 7, &na); }
          else { put_num(b, at, row, 6 + !g, SENS_INF); put_num(b, at, row, 7 - !g, fabs(v - bnd)); }
        }
        row++;
      }
    }
  } else {
    static const char *const h1[] = { "Objective Cell", "Name", "Value" };
    static const char *const h2[] = { "Variable Cell", "Name", "Value", "Lower Limit", "Objective Result", "Upper Limit", "Objective Result" };
    for (k = 0; k < 3; k++) put_text(b, at, row, 1 + k, h1[k]);
    bold(b, at, row, 1, 3); row++;
    absref(e.objr, e.objc, ref); put_text(b, at, row, 1, ref);
    cell_label(b, sh, e.objr, e.objc, lab, sizeof lab); if (lab[0]) put_text(b, at, row, 2, lab);
    put_num(b, at, row, 3, vals[0]);
    row += 3;
    for (k = 0; k < 7; k++) put_text(b, at, row, 1 + k, h2[k]);
    bold(b, at, row, 1, 7); row++;
    for (j = 0; j < e.n; j++, row++) {
      double *slot = &vals[1 + 2 * e.nrows + 4 * j];
      int g;
      absref(e.vr[j], e.vc[j], ref); put_text(b, at, row, 1, ref);
      cell_label(b, sh, e.vr[j], e.vc[j], lab, sizeof lab); if (lab[0]) put_text(b, at, row, 2, lab);
      put_num(b, at, row, 3, res->sol[j]);
      for (g = 0; g < 2; g++) {
        if (slot[2 * g] == HUGE_VAL) put_text(b, at, row, 4 + 2 * g, "Unbounded");
        else if (slot[2 * g] != slot[2 * g]) { Value na = val_err(E_NA); book_set_value(b, at, row, 4 + 2 * g, &na); }
        else { put_num(b, at, row, 4 + 2 * g, slot[2 * g]); put_num(b, at, row, 5 + 2 * g, slot[2 * g + 1]); }
      }
    }
  }
  book_set_col_width(b, at, 0, 14);
  for (k = 1; k <= 7; k++) book_set_col_width(b, at, k, (u16)(k == 1 ? 80 : (k == 4 && kind == 0) || (k >= 5 && kind == 2) ? 130 : 100));
  book_end(b);
  b->manualcalc = manual;
  if (!manual) book_recalc(b);
  b->active = at;
  free(vals);
  ev_free(&e);
  return at;
}
