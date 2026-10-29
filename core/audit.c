/*
 * audit.c - see audit.h. Written from the behaviour GridCraft documents for these commands
 * (formulas.tracePrecedents/Dependents, evaluateFormula, errorChecking, edit.goToSpecial,
 * formulas.createFromSelection, review.checkAccessibility) and Excel's.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "audit.h"
#include "calc.h"
#include "cond.h"
#include "ops.h"

/* ------------------------------------------------------------------ precedents and dependents */
static const Name *find_name(Book *b, int sheet, const char *n)
{
  int i;
  for (i = 0; i < b->nnames; i++) if (b->names[i].sheet == (u16)sheet && str_ieq(b->names[i].name, n)) return &b->names[i];
  for (i = 0; i < b->nnames; i++) if (b->names[i].sheet == SHEET_SELF && str_ieq(b->names[i].name, n)) return &b->names[i];
  return NULL;
}
static int add_ref(AuditRef *out, int n, int max, int sheet, Rect r)
{
  int i;
  if (r.r2 >= MAXROW) r.r2 = MAXROW - 1;
  if (r.c2 >= MAXCOL) r.c2 = MAXCOL - 1;
  for (i = 0; i < n; i++) if (out[i].sheet == sheet && !memcmp(&out[i].r, &r, sizeof r)) return n;
  if (n >= max) return n;
  out[n].sheet = sheet; out[n].r = r;
  return n + 1;
}
static int formula_refs(Book *b, int home, const Formula *f, AuditRef *out, int n, int max, int depth)
{
  int i;
  for (i = 0; f && i < f->n; i++) {
    const Tok *t = &f->t[i];
    if (t->op == T_REF || t->op == T_AREA) {
      Rect r;
      r.r1 = t->r1; r.c1 = t->c1;
      r.r2 = t->op == T_REF ? t->r1 : t->r2; r.c2 = t->op == T_REF ? t->c1 : t->c2;
      if (r.r1 > r.r2) { i32 x = r.r1; r.r1 = r.r2; r.r2 = x; }
      if (r.c1 > r.c2) { i32 x = r.c1; r.c1 = r.c2; r.c2 = x; }
      n = add_ref(out, n, max, t->a == SHEET_SELF ? home : t->a, r);
    } else if (t->op == T_NAME && t->s && depth < 8) {
      const Name *nm = find_name(b, home, t->s);
      if (nm && nm->f) n = formula_refs(b, nm->sheet == SHEET_SELF ? home : nm->sheet, nm->f, out, n, max, depth + 1);
    }
  }
  return n;
}
int audit_precedents(Book *b, int sheet, i32 r, i32 c, AuditRef *out, int max)
{
  Cell *cell = sheet_cell(b->sh[sheet], r, c);
  if (!cell || !cell->f) return 0;
  return formula_refs(b, sheet, cell->f, out, 0, max, 0);
}
int audit_dependents(Book *b, int sheet, i32 r, i32 c, AuditRef *out, int max)
{
  int si, n = 0;
  AuditRef refs[64];
  for (si = 0; si < b->nsh && n < max; si++) {
    Sheet *s = b->sh[si];
    i32 row;
    for (row = 0; row < s->nrows && n < max; row++) {
      Row *rw = s->rows[row];
      int i;
      if (!rw) continue;
      for (i = 0; i < rw->n && n < max; i++) {
        int k, nr;
        if (!rw->c[i].f) continue;
        nr = formula_refs(b, si, rw->c[i].f, refs, 0, 64, 0);
        for (k = 0; k < nr; k++)
          if (refs[k].sheet == sheet && r >= refs[k].r.r1 && r <= refs[k].r.r2 && c >= refs[k].r.c1 && c <= refs[k].r.c2) {
            Rect one;
            one.r1 = one.r2 = row; one.c1 = one.c2 = rw->c[i].col;
            n = add_ref(out, n, max, si, one);
            break;
          }
      }
    }
  }
  return n;
}

/* ------------------------------------------------------------------ Evaluate Formula */
static int is_binary(int op) { return (op >= T_ADD && op <= T_GE) || op == T_RANGE || op == T_UNION || op == T_ISECT; }
static int is_unary(int op) { return op == T_NEG || op == T_POS || op == T_PCT || op == T_PAREN; }
static int arity(const Tok *t) { return is_binary(t->op) ? 2 : is_unary(t->op) ? 1 : t->op == T_FUNC ? t->flags : 0; }
static int is_const(const Tok *t) { return t->op == T_NUM || t->op == T_STR || t->op == T_BOOL || t->op == T_ERR || t->op == T_ARRAY || t->op == T_MISSING; }
/* each token's subtree start (start[i] .. i) and its parent (-1 for the root) */
static int shape(const Formula *f, int *start, int *parent)
{
  int *stack = (int *)malloc((size_t)(f->n + 1) * sizeof(int)), sp = 0, i, k;
  if (!stack) return 0;
  for (i = 0; i < f->n; i++) {
    int a = arity(&f->t[i]);
    parent[i] = -1;
    if (a > sp) { free(stack); return 0; }
    start[i] = i;
    for (k = 0; k < a; k++) { int ch = stack[--sp]; parent[ch] = i; if (start[ch] < start[i]) start[i] = start[ch]; }
    stack[sp++] = i;
  }
  free(stack);
  return sp == 1;
}
/* a reference (or name) handed straight to a function stays a reference: SUM(A1:A3) works it out
   itself. So do ranges built with : , and space */
static int stays_ref(const Formula *f, const int *parent, int i)
{
  int p = parent[i], op = f->t[i].op;
  while (p >= 0 && f->t[p].op == T_PAREN) p = parent[p];
  if (op == T_RANGE || op == T_UNION || op == T_ISECT) return p < 0 || f->t[p].op == T_FUNC || f->t[p].op == T_RANGE || f->t[p].op == T_UNION || f->t[p].op == T_ISECT;
  if (op == T_AREA || op == T_NAME) return p >= 0 && (f->t[p].op == T_FUNC || f->t[p].op == T_RANGE || f->t[p].op == T_UNION || f->t[p].op == T_ISECT);
  if (op == T_REF) return p >= 0 && (f->t[p].op == T_RANGE || f->t[p].op == T_UNION || f->t[p].op == T_ISECT);
  return 0;
}
/* the next token to work out: the first (in evaluation order) whose operands are all worked out */
static int next_tok(EvalSteps *e, int *from)
{
  Formula *f = e->f;
  int *start, *parent, i, k, found = -1;
  if (!f || f->n <= 1) return -1;
  start = (int *)malloc((size_t)f->n * sizeof(int)); parent = (int *)malloc((size_t)f->n * sizeof(int));
  if (!start || !parent || !shape(f, start, parent)) { free(start); free(parent); return -1; }
  for (i = 0; i < f->n && found < 0; i++) {
    const Tok *t = &f->t[i];
    int ok = 1;
    if (is_const(t) || stays_ref(f, parent, i)) continue;
    /* its operands: constants, or references that stay */
    for (k = start[i]; k < i; k++) if (parent[k] == i && !is_const(&f->t[k]) && !stays_ref(f, parent, k)) ok = 0;
    if (ok) { found = i; *from = start[i]; }
  }
  free(start); free(parent);
  return found;
}
int eval_begin(EvalSteps *e, Book *b, int sheet, i32 r, i32 c)
{
  Cell *cell = sheet_cell(b->sh[sheet], r, c);
  memset(e, 0, sizeof *e);
  if (!cell || !cell->f) return 0;
  e->b = b; e->sheet = sheet; e->r = r; e->c = c;
  e->f = formula_copy(cell->f);
  return e->f != NULL;
}
void eval_end(EvalSteps *e) { formula_free(e->f); e->f = NULL; }
static void tok_free(Tok *t) { free(t->s); t->s = NULL; if (t->arr) { arr_free(t->arr); t->arr = NULL; } }
void eval_text(EvalSteps *e, char *out, size_t cap)
{
  Buf o;
  buf_init(&o);
  if (e->f) formula_text(e->b, e->sheet, e->f, &o);
  snprintf(out, cap, "=%s", o.data ? o.data : "");
  buf_free(&o);
}
/* the tokens from..to as a formula of their own (copies) */
static Formula *slice(const Formula *f, int from, int to)
{
  Formula part, *c;
  part.t = f->t + from; part.n = to - from + 1; part.volatile_ = f->volatile_;
  c = formula_copy(&part);
  return c;
}
int eval_next(EvalSteps *e, char *part, size_t cap)
{
  int from, i = next_tok(e, &from);
  part[0] = 0;
  if (i < 0) return 0;
  {
    Formula *p = slice(e->f, from, i);
    Buf o;
    if (!p) return 0;
    buf_init(&o);
    formula_text(e->b, e->sheet, p, &o);
    snprintf(part, cap, "%s", o.data ? o.data : "");
    buf_free(&o);
    formula_free(p);
  }
  return 1;
}
int eval_done(EvalSteps *e) { int from; return next_tok(e, &from) < 0; }
int eval_step(EvalSteps *e)
{
  int from, i = next_tok(e, &from), k;
  Formula *p;
  Value v;
  Tok nt;
  if (i < 0) return 0;
  memset(&nt, 0, sizeof nt);
  if (e->f->t[i].op == T_PAREN && from == i - 1) {
    /* brackets round a value: just the value */
    nt = e->f->t[from]; e->f->t[from].s = NULL; e->f->t[from].arr = NULL;
  } else {
    if (!(p = slice(e->f, from, i))) return 0;
    v = book_eval_at(e->b, e->sheet, e->r, e->c, p);
    formula_free(p);
    switch (v.t) {
    case V_NUM: nt.op = T_NUM; nt.n = v.n; break;
    case V_STR: nt.op = T_STR; nt.s = v.s; v.s = NULL; break;
    case V_BOOL: nt.op = T_BOOL; nt.a = v.e; break;
    case V_ERR: nt.op = T_ERR; nt.a = v.e; break;
    case V_ARR: nt.op = T_ARRAY; nt.arr = v.a; v.a = NULL; break;
    default: nt.op = T_NUM; nt.n = 0;
    }
    val_free(&v);
  }
  for (k = from; k <= i; k++) tok_free(&e->f->t[k]);
  e->f->t[from] = nt;
  memmove(e->f->t + from + 1, e->f->t + i + 1, (size_t)(e->f->n - i - 1) * sizeof(Tok));
  e->f->n -= i - from;
  e->steps++;
  return 1;
}

/* ------------------------------------------------------------------ Error Checking */
int audit_errors(Book *b, AuditCell *out, int max)
{
  int si, n = 0;
  for (si = 0; si < b->nsh && n < max; si++) {
    Sheet *s = b->sh[si];
    i32 row;
    for (row = 0; row < s->nrows && n < max; row++) {
      Row *rw = s->rows[row];
      int i;
      if (!rw) continue;
      for (i = 0; i < rw->n && n < max; i++) {
        const Value *v = cell_value(b, si, row, rw->c[i].col);
        rw = s->rows[row];
        if (v->t != V_ERR) continue;
        out[n].sheet = si; out[n].r = row; out[n].c = rw->c[i].col; out[n].err = v->e;
        n++;
      }
    }
  }
  return n;
}
const char *audit_error_help(int err)
{
  switch (err) {
  case E_DIV0: return "Divide by Zero Error: the formula divides by zero or by an empty cell.";
  case E_VALUE: return "Value Error: an argument or operand is of the wrong type, such as text where a number is needed.";
  case E_REF: return "Invalid Cell Reference: the formula refers to cells that were deleted or lie off the sheet.";
  case E_NAME: return "Invalid Name Error: the formula uses a name or function Ledger doesn't know.";
  case E_NUM: return "Invalid Number: a number is too big, too small, or out of the function's range.";
  case E_NA: return "Value Not Available: a lookup didn't find what it was looking for.";
  case E_NULL: return "Null Error: the ranges given with a space don't overlap.";
  case E_SPILL: return "Spill Error: the result's range isn't empty.";
  case E_CALC: return "Circular Reference or Calculation Error: the formula depends on its own result, or can't be worked out.";
  default: return "The cell shows an error.";
  }
}

/* ------------------------------------------------------------------ Go To Special */
static int filled(Book *b, int sheet, i32 r, i32 c)
{
  Cell *cell = sheet_cell(b->sh[sheet], r, c);
  (void)b;
  return cell && (cell->f || cell->v.t != V_EMPTY);
}
Rect current_region_at(Book *b, int sheet, i32 r, i32 c)
{
  Rect a;
  int grew = 1;
  a.r1 = a.r2 = r; a.c1 = a.c2 = c;
  while (grew) {
    i32 k;
    grew = 0;
    if (a.r1 > 0) for (k = (a.c1 > 0 ? a.c1 - 1 : 0); k <= a.c2 + 1 && k < MAXCOL; k++) if (filled(b, sheet, a.r1 - 1, k)) { a.r1--; grew = 1; break; }
    if (a.r2 < MAXROW - 1) for (k = (a.c1 > 0 ? a.c1 - 1 : 0); k <= a.c2 + 1 && k < MAXCOL; k++) if (filled(b, sheet, a.r2 + 1, k)) { a.r2++; grew = 1; break; }
    if (a.c1 > 0) for (k = (a.r1 > 0 ? a.r1 - 1 : 0); k <= a.r2 + 1 && k < MAXROW; k++) if (filled(b, sheet, k, a.c1 - 1)) { a.c1--; grew = 1; break; }
    if (a.c2 < MAXCOL - 1) for (k = (a.r1 > 0 ? a.r1 - 1 : 0); k <= a.r2 + 1 && k < MAXROW; k++) if (filled(b, sheet, k, a.c2 + 1)) { a.c2++; grew = 1; break; }
    if (a.r2 - a.r1 > 100000 || a.c2 - a.c1 > 5000) break;
  }
  return a;
}
static int matches(Book *b, int sheet, i32 r, i32 c, int kind)
{
  Sheet *s = b->sh[sheet];
  Cell *cell = sheet_cell(s, r, c);
  const Value *v;
  int i;
  switch (kind) {
  case GS_BLANKS: return !cell || (!cell->f && cell->v.t == V_EMPTY);
  case GS_CONSTANTS: return cell && !cell->f && cell->v.t != V_EMPTY;
  case GS_FORMULAS: return cell && cell->f;
  case GS_NOTES: return sheet_note(s, r, c) != NULL;
  case GS_VISIBLE: return sheet_row_height(s, r) && sheet_col_width(s, c);
  case GS_CONDFMT: for (i = 0; i < s->ncfs; i++) if (cond_covers(&s->cfs[i], r, c)) return 1; return 0;
  }
  if (!cell || (!cell->f && cell->v.t == V_EMPTY)) return 0;
  v = cell_value(b, sheet, r, c);
  switch (kind) {
  case GS_ERRORS: return v->t == V_ERR;
  case GS_NUMBERS: return v->t == V_NUM;
  case GS_TEXT: return v->t == V_STR;
  case GS_LOGICALS: return v->t == V_BOOL;
  }
  return 0;
}
long goto_special(Book *b, int sheet, Rect area, i32 cur_r, i32 cur_c, int kind, Rect *bound, AuditCell *out, int max)
{
  Sheet *s = b->sh[sheet];
  long n = 0;
  i32 r, c, lr = sheet_last_row(s), lc = sheet_last_col(s);
  bound->r1 = bound->c1 = 0; bound->r2 = bound->c2 = -1;
  if (kind == GS_LASTCELL) { bound->r1 = bound->r2 = lr < 0 ? 0 : lr; bound->c1 = bound->c2 = lc < 0 ? 0 : lc; return 1; }
  if (kind == GS_REGION) { *bound = current_region_at(b, sheet, cur_r, cur_c); return 1; }
  /* one cell selected: the used part of the sheet */
  if (area.r1 == area.r2 && area.c1 == area.c2) { area.r1 = area.c1 = 0; area.r2 = lr; area.c2 = lc; }
  if (area.r2 > lr && kind != GS_BLANKS) area.r2 = lr;
  if (area.c2 > lc && kind != GS_BLANKS) area.c2 = lc;
  if (area.r2 >= MAXROW) area.r2 = MAXROW - 1;
  if (area.c2 >= MAXCOL) area.c2 = MAXCOL - 1;
  if (kind == GS_BLANKS) { if (area.r2 > lr) area.r2 = lr; if (area.c2 > lc) area.c2 = lc; }
  if ((double)(area.r2 - area.r1 + 1) * (area.c2 - area.c1 + 1) > 4e6) return 0;
  for (r = area.r1; r <= area.r2; r++)
    for (c = area.c1; c <= area.c2; c++) {
      if (!matches(b, sheet, r, c, kind)) continue;
      if (!n) { bound->r1 = bound->r2 = r; bound->c1 = bound->c2 = c; }
      else { if (r > bound->r2) bound->r2 = r; if (c < bound->c1) bound->c1 = c; if (c > bound->c2) bound->c2 = c; }
      if (n < max) { out[n].sheet = sheet; out[n].r = r; out[n].c = c; out[n].err = 0; }
      n++;
    }
  return n;
}

/* ------------------------------------------------------------------ Create from Selection */
/* a label made into a name: letters, digits, _ and . kept, spaces and the rest as _, a leading digit
   given a _ in front (Excel's way); 0 if nothing is left */
static int name_of(const char *label, char *out, size_t cap)
{
  size_t n = 0;
  const char *p;
  for (p = label; *p && n + 2 < cap; p++) {
    u8 ch = (u8)*p;
    if (isalnum(ch) || ch == '_' || ch == '.' || ch >= 0x80) { if (!n && isdigit(ch)) out[n++] = '_'; out[n++] = (char)ch; }
    else if (n && out[n - 1] != '_') out[n++] = '_';
  }
  while (n && out[n - 1] == '_') n--;
  out[n] = 0;
  return n > 0;
}
static int name_for(Book *b, int sheet, i32 lr, i32 lc, Rect target)
{
  char label[256], name[64], text[160], a[16], c2[16];
  u32 col;
  Buf o;
  int quote = 0;
  const char *p;
  book_shown_text(b, sheet, lr, lc, label, sizeof label, &col);
  if (!label[0] || !name_of(label, name, sizeof name)) return 0;
  col_name(target.c1, a); col_name(target.c2, c2);
  buf_init(&o);
  for (p = b->sh[sheet]->name; *p; p++) if (!isalnum((u8)*p) && *p != '_') quote = 1;
  if (quote) buf_addc(&o, '\'');
  for (p = b->sh[sheet]->name; *p; p++) { if (*p == '\'') buf_addc(&o, '\''); buf_addc(&o, *p); }
  if (quote) buf_addc(&o, '\'');
  if (target.r1 == target.r2 && target.c1 == target.c2) snprintf(text, sizeof text, "!$%s$%ld", a, (long)target.r1 + 1);
  else snprintf(text, sizeof text, "!$%s$%ld:$%s$%ld", a, (long)target.r1 + 1, c2, (long)target.r2 + 1);
  buf_adds(&o, text);
  {
    int ok = o.data && book_define_name(b, name, o.data, SHEET_SELF);
    buf_free(&o);
    return ok;
  }
}
int book_create_names(Book *b, int sheet, Rect a, int top, int left, int bottom, int right)
{
  int n = 0, manual = b->manualcalc;
  i32 k;
  Rect body = a, t;
  if (top) body.r1++;
  if (bottom) body.r2--;
  if (left) body.c1++;
  if (right) body.c2--;
  if (body.r1 > body.r2 || body.c1 > body.c2) return 0;
  b->manualcalc = 1;                       /* (one recalculation at the end) */
  for (k = body.c1; k <= body.c2; k++) {
    t.c1 = t.c2 = k; t.r1 = body.r1; t.r2 = body.r2;
    if (top) n += name_for(b, sheet, a.r1, k, t);
    if (bottom) n += name_for(b, sheet, a.r2, k, t);
  }
  for (k = body.r1; k <= body.r2; k++) {
    t.r1 = t.r2 = k; t.c1 = body.c1; t.c2 = body.c2;
    if (left) n += name_for(b, sheet, k, a.c1, t);
    if (right) n += name_for(b, sheet, k, a.c2, t);
  }
  b->manualcalc = manual;
  if (!manual) book_recalc(b);
  return n;
}

/* ------------------------------------------------------------------ Check Accessibility */
int audit_accessibility(Book *b, char *out, size_t cap)
{
  int i, n = 0;
  size_t len = 0;
  out[0] = 0;
  for (i = 0; i < b->nsh; i++) {
    Sheet *s = b->sh[i];
    char line[160];
    if (s->nmerges) { snprintf(line, sizeof line, "%s\tMerged cells (%d): screen readers read them badly\n", s->name, s->nmerges); n++; if (len + strlen(line) < cap) { strcpy(out + len, line); len += strlen(line); } }
    if (!strncmp(s->name, "Sheet", 5)) { snprintf(line, sizeof line, "%s\tDefault sheet name: give the sheet a name that says what it holds\n", s->name); n++; if (len + strlen(line) < cap) { strcpy(out + len, line); len += strlen(line); } }
  }
  return n;
}
