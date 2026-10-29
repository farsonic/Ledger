/*
 * whatif.c - What-If Analysis (see data.h): Goal Seek, Data Tables and scenarios, as GridCraft's
 * (crates/engine/src/cmd/whatif.rs, whatif/scenarios.rs). Each try puts a number straight into the
 * input cell and lets the evaluator work out what depends on it afresh (a new calculation
 * generation, so formulas are computed again when read); the workbook itself only changes when the
 * answer is written, as one undo step.
 */
#include "data.h"
#include "calc.h"
#include "ops.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>

/* a trial: cells set to numbers, then the value of the cell read (NAN if not a number) */
typedef struct { Book *b; Cell *cell[2]; Value old[2]; Formula *oldf[2]; int existed[2]; int sh[2]; i32 r[2], c[2]; int n; } Trial;
static int trial_begin(Trial *t, Book *b, int n, const int *sh, const i32 *r, const i32 *c)
{
  int i;
  t->b = b; t->n = 0;
  for (i = 0; i < n; i++) {
    Sheet *s = b->sh[sh[i]];
    t->sh[i] = sh[i]; t->r[i] = r[i]; t->c[i] = c[i];
    t->existed[i] = sheet_cell(s, r[i], c[i]) != NULL;
    if (!(t->cell[i] = sheet_cell_make(s, r[i], c[i]))) {
      while (--i >= 0) if (!t->existed[i]) sheet_cell_clear(b->sh[sh[i]], r[i], c[i]);
      return 0;
    }
  }
  t->n = n;
  /* (cells made may have moved others in their row: look them up again) */
  for (i = 0; i < n; i++) {
    t->cell[i] = sheet_cell(b->sh[sh[i]], r[i], c[i]);
    t->old[i] = t->cell[i]->v; t->oldf[i] = t->cell[i]->f;
    t->cell[i]->v = val_empty(); t->cell[i]->f = NULL;
  }
  return 1;
}
static void trial_set(Trial *t, int i, const Value *v)
{
  Cell *cell = sheet_cell(t->b->sh[t->sh[i]], t->r[i], t->c[i]);
  if (!cell) return;
  val_free(&cell->v);
  cell->v = val_copy(v);
}
static void trial_end(Trial *t)
{
  int i;
  for (i = 0; i < t->n; i++) {
    Cell *cell = sheet_cell(t->b->sh[t->sh[i]], t->r[i], t->c[i]);
    if (!cell) continue;
    val_free(&cell->v);
    cell->v = t->old[i]; cell->f = t->oldf[i];
    if (!t->existed[i]) sheet_cell_clear(t->b->sh[t->sh[i]], t->r[i], t->c[i]);
  }
  t->b->calcgen++;
}
static double read_num(Book *b, int sheet, i32 r, i32 c)
{
  const Value *v;
  b->calcgen++;
  if (!b->calcgen) b->calcgen = 1;
  v = cell_value(b, sheet, r, c);
  return v->t == V_NUM ? v->n : v->t == V_BOOL ? v->e : NAN;
}

/* ------------------------------------------------------------------ Goal Seek */
typedef struct { Book *b; Trial *t; int fsh; i32 fr, fc; double target; } SeekCtx;
static double fval(SeekCtx *k, double x)
{
  Value v = val_num(x);
  trial_set(k->t, 0, &v);
  return read_num(k->b, k->fsh, k->fr, k->fc) - k->target;
}
static int finite_(double x) { return x == x && x - x == 0; }
/* the secant search with bisection once bracketed (GridCraft's seek) */
static double seek(SeekCtx *k, double x0, int max_iter, double tol, int *conv)
{
  double f0 = fval(k, x0), bx = x0, bf = f0, xa = x0, fa = f0, xb, fb, step0, lo = 0, flo = 0, hi = 0, fhi = 0;
  int it = 1, bracket = 0, slow = 0;
#define CONSIDER(x, fx) do { if (finite_(fx) && (!finite_(bf) || fabs(fx) < fabs(bf))) { bx = (x); bf = (fx); } } while (0)
  if (finite_(f0) && fabs(f0) <= tol) { *conv = 1; return x0; }
  step0 = x0 == 0 ? 0.01 : fabs(x0) * 0.01;
  xb = x0 + step0; fb = fval(k, xb);
  CONSIDER(xb, fb);
  while (it < max_iter) {
    double secant, next, fnext;
    if (finite_(fb) && fabs(fb) <= tol) break;
    if (!bracket && finite_(fa) && finite_(fb) && ((fa < 0) != (fb < 0))) { bracket = 1; lo = xa; flo = fa; hi = xb; fhi = fb; }
    secant = finite_(fa) && finite_(fb) && fb != fa ? xb - fb * (xb - xa) / (fb - fa) : NAN;
    if (bracket) {
      double a = lo < hi ? lo : hi, z = lo < hi ? hi : lo;
      next = (slow >= 2 || !(secant > a && secant < z)) ? (lo + hi) / 2 : secant;
    } else {
      double span = fabs(xb - xa);
      if (span < 1e-300) span = 1e-300;
      if (!finite_(fb)) next = (xa + xb) / 2;
      else if (!finite_(secant)) next = xb + 2 * (xb - xa);
      else if (fabs(secant - xb) > 100 * span + 1) next = xb + (secant > xb ? 1 : -1) * (100 * span + 1);
      else next = secant;
    }
    if (!finite_(next)) break;
    fnext = fval(k, next);
    it++;
    CONSIDER(next, fnext);
    if (bracket) {
      double width = fabs(hi - lo), nw;
      if (finite_(fnext)) {
        if ((fnext < 0) == (flo < 0)) { lo = next; flo = fnext; } else { hi = next; fhi = fnext; }
      } else if (fabs(flo) < fabs(fhi)) { hi = next; } else { lo = next; }
      nw = fabs(hi - lo);
      slow = nw > width * 0.5 ? slow + 1 : 0;
      if (nw <= 2.2e-16 * (1 + (fabs(lo) > fabs(hi) ? fabs(lo) : fabs(hi)))) break;
    }
    xa = xb; fa = fb; xb = next; fb = fnext;
  }
#undef CONSIDER
  *conv = finite_(bf) && fabs(bf) <= tol;
  return bx;
}
int book_goal_seek(Book *b, int sheet, i32 fr, i32 fc, double target, int csh, i32 cr, i32 cc, double *found, const char **why)
{
  Cell *fcell, *ccell;
  Trial t;
  SeekCtx k;
  double x0, x;
  int conv = 0, sh[1];
  i32 rr[1], cl[1];
  Value v;
  *why = NULL;
  if (sheet < 0 || sheet >= b->nsh || csh < 0 || csh >= b->nsh) { *why = "Reference isn't valid."; return -1; }
  fcell = sheet_cell(b->sh[sheet], fr, fc);
  ccell = sheet_cell(b->sh[csh], cr, cc);
  if (!fcell || !fcell->f) { *why = "Cell must contain a formula."; return -1; }
  if (ccell && ccell->f) { *why = "Cell must contain a value."; return -1; }
  x0 = ccell && ccell->v.t == V_NUM ? ccell->v.n : 0;
  sh[0] = csh; rr[0] = cr; cl[0] = cc;
  if (!trial_begin(&t, b, 1, sh, rr, cl)) { *why = "Not enough memory."; return -1; }
  k.b = b; k.t = &t; k.fsh = sheet; k.fr = fr; k.fc = fc; k.target = target;
  x = seek(&k, x0, 100, 0.001, &conv);
  trial_end(&t);
  v = val_num(x);
  book_set_value(b, csh, cr, cc, &v);
  if (b->manualcalc) book_recalc(b);
  *found = x;
  return conv;
}

/* ------------------------------------------------------------------ Data Tables */
int book_data_table(Book *b, int sheet, Rect a, i32 rin_r, i32 rin_c, i32 cin_r, i32 cin_c)
{
  Sheet *s;
  Trial t;
  int sh[2], n = 0, mode, manual;
  i32 rr[2], cl[2], r, c;
  Value *out;
  long cells, k = 0;
  if (sheet < 0 || sheet >= b->nsh || a.r2 <= a.r1 || a.c2 <= a.c1) return -1;
  if (rin_r < 0 && cin_r < 0) return -1;
  if ((rin_r >= a.r1 && rin_r <= a.r2 && rin_c >= a.c1 && rin_c <= a.c2) || (cin_r >= a.r1 && cin_r <= a.r2 && cin_c >= a.c1 && cin_c <= a.c2)) return -1;
  cells = (long)(a.r2 - a.r1) * (a.c2 - a.c1);
  if (cells > 100000 || !(out = (Value *)calloc((size_t)cells, sizeof(Value)))) return -1;
  s = b->sh[sheet];
  mode = rin_r >= 0 && cin_r >= 0 ? 2 : cin_r >= 0 ? 0 : 1;      /* 0 values down (column input), 1 across (row input), 2 both */
  if (rin_r >= 0) { sh[n] = sheet; rr[n] = rin_r; cl[n] = rin_c; n++; }
  if (cin_r >= 0) { sh[n] = sheet; rr[n] = cin_r; cl[n] = cin_c; n++; }
  {
    /* the edge values, read before the inputs change */
    Value *down = (Value *)calloc((size_t)(a.r2 - a.r1) + 1, sizeof(Value)), *across = (Value *)calloc((size_t)(a.c2 - a.c1) + 1, sizeof(Value));
    if (!down || !across) { free(down); free(across); free(out); return -1; }
    for (r = a.r1 + 1; r <= a.r2; r++) down[r - a.r1 - 1] = val_copy(cell_value(b, sheet, r, a.c1));
    for (c = a.c1 + 1; c <= a.c2; c++) across[c - a.c1 - 1] = val_copy(cell_value(b, sheet, a.r1, c));
    if (!trial_begin(&t, b, n, sh, rr, cl)) {
      trial_end(&t);
      for (r = 0; r <= a.r2 - a.r1; r++) val_free(&down[r]);
      for (c = 0; c <= a.c2 - a.c1; c++) val_free(&across[c]);
      free(down); free(across); free(out); return -1;
    }
    for (r = a.r1 + 1; r <= a.r2; r++)
      for (c = a.c1 + 1; c <= a.c2; c++, k++) {
        const Value *res;
        if (mode == 0) { trial_set(&t, 0, &down[r - a.r1 - 1]); b->calcgen++; res = cell_value(b, sheet, a.r1, c); }
        else if (mode == 1) { trial_set(&t, 0, &across[c - a.c1 - 1]); b->calcgen++; res = cell_value(b, sheet, r, a.c1); }
        else { trial_set(&t, 0, &across[c - a.c1 - 1]); trial_set(&t, 1, &down[r - a.r1 - 1]); b->calcgen++; res = cell_value(b, sheet, a.r1, a.c1); }
        out[k] = val_copy(res);
      }
    trial_end(&t);
    for (r = 0; r <= a.r2 - a.r1; r++) val_free(&down[r]);
    for (c = 0; c <= a.c2 - a.c1; c++) val_free(&across[c]);
    free(down); free(across);
  }
  manual = b->manualcalc;
  b->manualcalc = 1;
  book_begin(b);
  for (r = a.r1 + 1, k = 0; r <= a.r2; r++)
    for (c = a.c1 + 1; c <= a.c2; c++, k++) { book_set_value(b, sheet, r, c, &out[k]); val_free(&out[k]); }
  book_end(b);
  b->manualcalc = manual;
  book_recalc(b);
  free(out);
  (void)s;
  return (int)cells;
}

/* ------------------------------------------------------------------ scenarios */
void scenario_free(Scenario *sc)
{
  int i;
  for (i = 0; i < sc->n; i++) free(sc->vals[i]);
  free(sc->vals); free(sc->r); free(sc->c); free(sc->comment);
  memset(sc, 0, sizeof *sc);
}
static int scen_find(Sheet *s, const char *name)
{
  int i;
  for (i = 0; i < s->nscens; i++) if (str_ieq(s->scens[i].name, name)) return i;
  return -1;
}
static int scen_make(Scenario *z, const char *name, const i32 *r, const i32 *c, const char *const *vals, int n, const char *comment)
{
  int k;
  memset(z, 0, sizeof *z);
  strncpy(z->name, name, sizeof z->name - 1);
  z->comment = str_dup(comment ? comment : "");
  z->r = (i32 *)malloc((size_t)(n + 1) * sizeof(i32)); z->c = (i32 *)malloc((size_t)(n + 1) * sizeof(i32));
  z->vals = (char **)calloc((size_t)n + 1, sizeof(char *));
  if (!z->comment || !z->r || !z->c || !z->vals) { scenario_free(z); return 0; }
  for (k = 0; k < n; k++) {
    z->r[k] = r[k]; z->c[k] = c[k];
    if (!(z->vals[k] = str_dup(vals[k] ? vals[k] : ""))) { z->n = k; scenario_free(z); return 0; }
    z->n = k + 1;
  }
  return 1;
}
int book_scenario_add(Book *b, int sheet, const char *name, const i32 *r, const i32 *c, const char *const *vals, int n, const char *comment)
{
  Sheet *s;
  Scenario z;
  int i;
  if (sheet < 0 || sheet >= b->nsh || !name || !*name || n <= 0 || n > 32) return 0;
  s = b->sh[sheet];
  if (!scen_make(&z, name, r, c, vals, n, comment)) return 0;
  book_begin(b);
  book_note_sheet(b, sheet);
  if ((i = scen_find(s, name)) >= 0) { scenario_free(&s->scens[i]); s->scens[i] = z; }
  else {
    Scenario *ns = (Scenario *)realloc(s->scens, (size_t)(s->nscens + 1) * sizeof(Scenario));
    if (!ns) { scenario_free(&z); book_end(b); return 0; }
    s->scens = ns; ns[s->nscens++] = z;
  }
  book_end(b);
  b->dirty = 1; b->changes++;
  return 1;
}
int book_scenario_delete(Book *b, int sheet, int i)
{
  Sheet *s;
  if (sheet < 0 || sheet >= b->nsh || i < 0 || i >= b->sh[sheet]->nscens) return 0;
  s = b->sh[sheet];
  book_begin(b);
  book_note_sheet(b, sheet);
  scenario_free(&s->scens[i]);
  memmove(&s->scens[i], &s->scens[i + 1], (size_t)(s->nscens - i - 1) * sizeof(Scenario));
  s->nscens--;
  book_end(b);
  b->dirty = 1; b->changes++;
  return 1;
}
int book_scenario_show(Book *b, int sheet, int i)
{
  Scenario *sc;
  int k, manual;
  if (sheet < 0 || sheet >= b->nsh || i < 0 || i >= b->sh[sheet]->nscens) return 0;
  sc = &b->sh[sheet]->scens[i];
  manual = b->manualcalc;
  b->manualcalc = 1;
  book_begin(b);
  for (k = 0; k < sc->n; k++) book_input(b, sheet, sc->r[k], sc->c[k], sc->vals[k], NULL);
  book_end(b);
  b->manualcalc = manual;
  if (!manual) book_recalc(b);
  return 1;
}
/* the value cell r, c of sheet has with scenario sc's values in its cells (all put back after) */
static Value with_scenario(Book *b, int sheet, const Scenario *sc, i32 r, i32 c)
{
  int nn = sc->n < 32 ? sc->n : 32, q;
  Value old[32], v;
  Formula *oldf[32];
  u8 made[32];
  for (q = 0; q < nn; q++) {
    Cell *cl;
    double d;
    int kind;
    made[q] = sheet_cell(b->sh[sheet], sc->r[q], sc->c[q]) == NULL;
    old[q] = val_empty(); oldf[q] = NULL;
    if (!(cl = sheet_cell_make(b->sh[sheet], sc->r[q], sc->c[q]))) continue;
    old[q] = cl->v; oldf[q] = cl->f; cl->f = NULL;
    cl->v = parse_number(sc->vals[q], &d, &kind) ? val_num(d) : val_str(sc->vals[q]);
  }
  b->calcgen++;
  v = val_copy(cell_value(b, sheet, r, c));
  for (q = 0; q < nn; q++) {
    Cell *cl = sheet_cell(b->sh[sheet], sc->r[q], sc->c[q]);
    if (!cl) continue;
    val_free(&cl->v); cl->v = old[q]; cl->f = oldf[q];
    if (made[q]) sheet_cell_clear(b->sh[sheet], sc->r[q], sc->c[q]);
  }
  b->calcgen++;
  return v;
}
int book_scenario_summary(Book *b, int sheet, const Rect *results, int nres)
{
  Sheet *s;
  int at, i, k, j, manual;
  i32 row = 0;
  char t[600], ref[16], nm[32];
  Value v;
  if (sheet < 0 || sheet >= b->nsh || !b->sh[sheet]->nscens) return -1;
  manual = b->manualcalc;
  b->manualcalc = 1;
  book_begin(b);
  book_note_book(b);
  book_unique_sheet_name(b, "Scenario Summary", nm);
  if ((at = book_add_sheet(b, nm, b->nsh)) < 0) { book_end(b); b->manualcalc = manual; return -1; }
  if (at <= sheet) sheet++;
  s = b->sh[sheet];
  /* columns: B the cell, C its current value, D on each scenario's */
  book_input(b, at, row, 0, "Scenario Summary", NULL);
  row += 2;
  book_input(b, at, row, 2, "Current Values:", NULL);
  for (i = 0; i < s->nscens; i++) { snprintf(t, sizeof t, "'%s", s->scens[i].name); book_input(b, at, row, 3 + i, t, NULL); }
  row++;
  book_input(b, at, row++, 0, "Changing Cells:", NULL);
  for (i = 0; i < s->nscens; i++)
    for (k = 0; k < s->scens[i].n; k++) {
      i32 cr = s->scens[i].r[k], cc = s->scens[i].c[k];
      int seen = 0, ii, kk;
      /* every changing cell once */
      for (ii = 0; ii <= i && !seen; ii++)
        for (kk = 0; kk < s->scens[ii].n && !seen; kk++)
          if ((ii < i || kk < k) && s->scens[ii].r[kk] == cr && s->scens[ii].c[kk] == cc) seen = 1;
      if (seen) continue;
      ref_text(cr, cc, 1, 1, ref);
      snprintf(t, sizeof t, "'%s", ref);
      book_input(b, at, row, 1, t, NULL);
      v = val_copy(cell_value(b, sheet, cr, cc)); book_set_value(b, at, row, 2, &v); val_free(&v);
      for (j = 0; j < s->nscens; j++) {
        int m;
        for (m = 0; m < s->scens[j].n; m++) if (s->scens[j].r[m] == cr && s->scens[j].c[m] == cc) break;
        if (m < s->scens[j].n) book_input(b, at, row, 3 + j, s->scens[j].vals[m], NULL);
      }
      row++;
    }
  if (nres > 0) {
    book_input(b, at, row++, 0, "Result Cells:", NULL);
    for (i = 0; i < nres; i++) {
      i32 rr, cc;
      for (rr = results[i].r1; rr <= results[i].r2 && rr < results[i].r1 + 32; rr++)
        for (cc = results[i].c1; cc <= results[i].c2 && cc < results[i].c1 + 32; cc++) {
          ref_text(rr, cc, 1, 1, ref);
          snprintf(t, sizeof t, "'%s", ref);
          book_input(b, at, row, 1, t, NULL);
          v = val_copy(cell_value(b, sheet, rr, cc)); book_set_value(b, at, row, 2, &v); val_free(&v);
          for (j = 0; j < s->nscens; j++) { v = with_scenario(b, sheet, &s->scens[j], rr, cc); book_set_value(b, at, row, 3 + j, &v); val_free(&v); }
          row++;
        }
    }
  }
  book_end(b);
  b->manualcalc = manual;
  if (!manual) book_recalc(b);
  return at;
}

/* ------------------------------------------------------------------ .xlsx */
void scen_xlsx_write(const Sheet *s, Buf *o)
{
  int i, k;
  if (!s->nscens) return;
  buf_adds(o, "<scenarios current=\"0\" show=\"0\">");
  for (i = 0; i < s->nscens; i++) {
    const Scenario *sc = &s->scens[i];
    buf_adds(o, "<scenario name=\""); xml_escape(o, sc->name, strlen(sc->name));
    buf_printf(o, "\" locked=\"1\" count=\"%d\"", sc->n);
    if (sc->comment && sc->comment[0]) { buf_adds(o, " comment=\""); xml_escape(o, sc->comment, strlen(sc->comment)); buf_adds(o, "\""); }
    buf_adds(o, ">");
    for (k = 0; k < sc->n; k++) {
      char ref[16];
      ref_text(sc->r[k], sc->c[k], 0, 0, ref);
      buf_printf(o, "<inputCells r=\"%s\" val=\"", ref);
      xml_escape(o, sc->vals[k], strlen(sc->vals[k]));
      buf_adds(o, "\"/>");
    }
    buf_adds(o, "</scenario>");
  }
  buf_adds(o, "</scenarios>");
}
void scen_xlsx_read(Sheet *s, Xml *x)
{
  int depth = x->depth;
  Scenario cur;
  int open = 0;
  i32 rr[32], cc[32];
  char *vals[32];
  int n = 0, k;
  if (x->pending_end) return;
  memset(&cur, 0, sizeof cur);
  while (xml_next(x) != XT_EOF) {
    if (x->type == XT_END && x->depth < depth) break;
    if (x->type == XT_START && !strcmp(x->name, "scenario")) {
      const char *nm = xml_attr(x, "name"), *cm = xml_attr(x, "comment");
      memset(&cur, 0, sizeof cur);
      strncpy(cur.name, nm ? nm : "Scenario", sizeof cur.name - 1);
      cur.comment = str_dup(cm ? cm : "");
      open = 1; n = 0;
    } else if (x->type == XT_START && !strcmp(x->name, "inputCells") && open && n < 32) {
      const char *r = xml_attr(x, "r"), *v = xml_attr(x, "val");
      i32 col;
      int k2 = r ? parse_col(r[0] == '$' ? r + 1 : r, &col) : 0;
      const char *d = k2 ? r + k2 + (r[0] == '$') : NULL;
      if (d && *d == '$') d++;
      if (d && atol(d) >= 1 && atol(d) <= MAXROW) { rr[n] = (i32)atol(d) - 1; cc[n] = col; vals[n] = str_dup(v ? v : ""); if (vals[n]) n++; }
    } else if (x->type == XT_END && !strcmp(x->name, "scenario") && open) {
      Scenario z;
      Scenario *ns;
      if (n && scen_make(&z, cur.name, rr, cc, (const char *const *)vals, n, cur.comment) && (ns = (Scenario *)realloc(s->scens, (size_t)(s->nscens + 1) * sizeof(Scenario)))) {
        s->scens = ns; ns[s->nscens++] = z;
      }
      for (k = 0; k < n; k++) free(vals[k]);
      free(cur.comment); cur.comment = NULL;
      open = 0; n = 0;
    }
  }
  for (k = 0; k < n; k++) free(vals[k]);
  free(cur.comment);
}
