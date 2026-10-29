/*
 * valid.c - data validation (see data.h): rules on cells saying what they may hold (a whole number
 * or decimal within limits, a date or time, a text length, an item of a list, or anything a formula
 * of your own allows), with an input message and an error alert. Also the hooks by which book.c
 * keeps the Data tab's parts of a sheet (validation rules and scenarios) through copies, undo and
 * inserted or deleted rows and columns.
 *
 * Behaviour as GridCraft's data validation (crates/engine/src/cmd/data/validation.rs), which
 * follows Excel's: formulas are written for the first cell of the rule's cells and seen from each
 * cell tested; a custom formula sees the value being entered.
 */
#include "data.h"
#include "analysis.h"
#include "calc.h"
#include "cond.h"
#include "ops.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>

static char *dup0(const char *s) { return str_dup(s ? s : ""); }

void dv_rule_init(DVRule *r)
{
  memset(r, 0, sizeof *r);
  r->type = DV_ANY; r->op = CO_BETWEEN; r->errstyle = DVE_STOP;
  r->blank = 1; r->dropdown = 1; r->showinput = 1; r->showerror = 1;
}
void dv_rule_free(DVRule *r)
{
  free(r->areas); formula_free(r->f1); formula_free(r->f2);
  free(r->intitle); free(r->inmsg); free(r->errtitle); free(r->errmsg);
  r->areas = NULL; r->nareas = 0; r->f1 = r->f2 = NULL;
  r->intitle = r->inmsg = r->errtitle = r->errmsg = NULL;
}
int dv_rule_copy(DVRule *dst, const DVRule *src)
{
  *dst = *src;
  dst->areas = NULL; dst->f1 = dst->f2 = NULL;
  dst->intitle = dup0(src->intitle); dst->inmsg = dup0(src->inmsg); dst->errtitle = dup0(src->errtitle); dst->errmsg = dup0(src->errmsg);
  if (src->nareas && (dst->areas = (Rect *)malloc((size_t)src->nareas * sizeof(Rect)))) memcpy(dst->areas, src->areas, (size_t)src->nareas * sizeof(Rect));
  else dst->nareas = 0;
  if (src->f1) dst->f1 = formula_copy(src->f1);
  if (src->f2) dst->f2 = formula_copy(src->f2);
  if (!dst->intitle || !dst->inmsg || !dst->errtitle || !dst->errmsg || (src->nareas && !dst->areas) || (src->f1 && !dst->f1) || (src->f2 && !dst->f2)) {
    dv_rule_free(dst);
    return 0;
  }
  return 1;
}
static int in_rect(const Rect *a, i32 r, i32 c) { return r >= a->r1 && r <= a->r2 && c >= a->c1 && c <= a->c2; }
const DVRule *dv_at(const Sheet *s, i32 r, i32 c)
{
  int i, j;
  for (i = 0; i < s->ndvs; i++)
    for (j = 0; j < s->dvs[i].nareas; j++) if (in_rect(&s->dvs[i].areas[j], r, c)) return &s->dvs[i];
  return NULL;
}
/* the rule's formulas moved so they read the same cells now that its first cell is elsewhere */
static void reanchor(DVRule *r, i32 old_r, i32 old_c)
{
  i32 dr, dc;
  if (!r->nareas) return;
  dr = r->areas[0].r1 - old_r; dc = r->areas[0].c1 - old_c;
  if (!dr && !dc) return;
  if (r->f1) formula_offset(r->f1, dr, dc);
  if (r->f2) formula_offset(r->f2, dr, dc);
}
/* a minus cut, as up to four rectangles in out; how many */
static int subtract(Rect a, const Rect *cut, Rect *out)
{
  Rect i;
  int n = 0;
  if (cut->r1 > a.r2 || cut->r2 < a.r1 || cut->c1 > a.c2 || cut->c2 < a.c1) { out[0] = a; return 1; }
  i.r1 = a.r1 > cut->r1 ? a.r1 : cut->r1; i.r2 = a.r2 < cut->r2 ? a.r2 : cut->r2;
  i.c1 = a.c1 > cut->c1 ? a.c1 : cut->c1; i.c2 = a.c2 < cut->c2 ? a.c2 : cut->c2;
  if (i.r1 > a.r1) { out[n] = a; out[n].r2 = i.r1 - 1; n++; }
  if (i.r2 < a.r2) { out[n] = a; out[n].r1 = i.r2 + 1; n++; }
  if (i.c1 > a.c1) { out[n].r1 = i.r1; out[n].r2 = i.r2; out[n].c1 = a.c1; out[n].c2 = i.c1 - 1; n++; }
  if (i.c2 < a.c2) { out[n].r1 = i.r1; out[n].r2 = i.r2; out[n].c1 = i.c2 + 1; out[n].c2 = a.c2; n++; }
  return n;
}
/* the cut's cells taken off every rule of the sheet (rules left with none go) */
static void cut_rules(Sheet *s, const Rect *cut)
{
  int i, j, k;
  for (i = k = 0; i < s->ndvs; i++) {
    DVRule *r = &s->dvs[i];
    Rect *na = (Rect *)malloc((size_t)(r->nareas * 4 + 1) * sizeof(Rect));
    int n = 0;
    i32 orr = r->nareas ? r->areas[0].r1 : 0, oc = r->nareas ? r->areas[0].c1 : 0;
    if (!na) { s->dvs[k++] = *r; continue; }
    for (j = 0; j < r->nareas; j++) n += subtract(r->areas[j], cut, na + n);
    free(r->areas);
    r->areas = na; r->nareas = n;
    if (!n) { dv_rule_free(r); continue; }
    reanchor(r, orr, oc);
    s->dvs[k++] = *r;
  }
  s->ndvs = k;
  if (!k) { free(s->dvs); s->dvs = NULL; }
}
int book_dv_set(Book *b, int sheet, const Rect *areas, int n, const DVRule *rule)
{
  Sheet *s;
  int i;
  if (sheet < 0 || sheet >= b->nsh || n <= 0) return 0;
  s = b->sh[sheet];
  book_begin(b);
  book_note_sheet(b, sheet);
  for (i = 0; i < n; i++) cut_rules(s, &areas[i]);
  if (rule) {
    DVRule *nd = (DVRule *)realloc(s->dvs, (size_t)(s->ndvs + 1) * sizeof(DVRule));
    if (nd) {
      DVRule *r = &nd[s->ndvs];
      s->dvs = nd;
      if (dv_rule_copy(r, rule)) {
        free(r->areas);
        if ((r->areas = (Rect *)malloc((size_t)n * sizeof(Rect)))) { memcpy(r->areas, areas, (size_t)n * sizeof(Rect)); r->nareas = n; s->ndvs++; }
        else dv_rule_free(r);
      }
    }
  }
  book_end(b);
  b->dirty = 1;
  b->changes++;
  return 1;
}

/* ------------------------------------------------------------------ checking */
/* f (as seen from the rule's first cell) evaluated at r, c */
static Value eval_at(Book *b, int sheet, const DVRule *rule, const Formula *f, i32 r, i32 c)
{
  Formula *g;
  Value v;
  if (!f) return val_err(E_NA);
  if (!rule->nareas || (rule->areas[0].r1 == r && rule->areas[0].c1 == c)) return book_eval_at(b, sheet, r, c, f);
  if (!(g = formula_copy(f))) return val_err(E_VALUE);
  formula_offset(g, r - rule->areas[0].r1, c - rule->areas[0].c1);
  v = book_eval_at(b, sheet, r, c, g);
  formula_free(g);
  return v;
}
static int as_number(const Value *v, double *n)
{
  int k;
  if (v->t == V_NUM) { *n = v->n; return 1; }
  if (v->t == V_BOOL) { *n = v->e; return 1; }
  if (v->t == V_STR && parse_number(v->s, n, &k)) return 1;
  return 0;
}
static int compare(int op, double x, int has1, double a, int has2, double b)
{
  double lo, hi;
  if (!has1) return 0;
  switch (op) {
  case CO_BETWEEN: if (!has2) return 0; lo = a < b ? a : b; hi = a < b ? b : a; return x >= lo && x <= hi;
  case CO_NOTBETWEEN: if (!has2) return 0; lo = a < b ? a : b; hi = a < b ? b : a; return x < lo || x > hi;
  case CO_EQ: return x == a;
  case CO_NE: return x != a;
  case CO_GT: return x > a;
  case CO_LT: return x < a;
  case CO_GE: return x >= a;
  case CO_LE: return x <= a;
  }
  return 0;
}
/* a value as text, as typed (numbers in General) */
static void value_text(const Value *v, char *out, size_t cap)
{
  out[0] = 0;
  if (v->t == V_NUM) num_general(v->n, out, 15);
  else if (v->t == V_STR) { strncpy(out, v->s, cap - 1); out[cap - 1] = 0; }
  else if (v->t == V_BOOL) strcpy(out, v->e ? "TRUE" : "FALSE");
  else if (v->t == V_ERR) strcpy(out, err_name(v->e));
}
static size_t utf8_len(const char *s)
{
  size_t n = 0;
  for (; *s; s++) if (((u8)*s & 0xC0) != 0x80) n++;
  return n;
}
/* whether item text t is the entry: equal ignoring case, or the same number */
static int item_matches(const char *t, const char *entry)
{
  double a, b;
  int k;
  if (str_ieq(t, entry)) return 1;
  return parse_number(t, &a, &k) && parse_number(entry, &b, &k) && a == b;
}
/* the check itself; swap: the value is put in the cell meanwhile (for formulas that read it) */
static int check(Book *b, int sheet, i32 r, i32 c, const Value *v, const char *typed, const DVRule **out, int swap)
{
  Sheet *s = b->sh[sheet];
  const DVRule *rule = dv_at(s, r, c);
  Cell *cell = NULL;
  Value oldv;
  Formula *oldf = NULL;
  int existed = 0, ok = 0;
  char text[512];
  if (out) *out = rule;
  if (!rule || rule->type == DV_ANY) return 1;
  if (v->t == V_EMPTY) return rule->blank;
  if (typed) { strncpy(text, typed, sizeof text - 1); text[sizeof text - 1] = 0; }
  else value_text(v, text, sizeof text);
  oldv = val_empty();
  if (swap) {
    existed = sheet_cell(s, r, c) != NULL;
    if ((cell = sheet_cell_make(s, r, c))) {
      oldv = cell->v; oldf = cell->f;
      cell->v = val_copy(v); cell->f = NULL;
      b->calcgen++;
    }
  }
  switch (rule->type) {
  case DV_LIST: {
    char **items;
    int n = dv_list_items(b, sheet, rule, r, c, &items), i;
    for (i = 0; i < n; i++) { if (!ok && item_matches(items[i], text)) ok = 1; free(items[i]); }
    free(items);
    break; }
  case DV_CUSTOM: {
    Value res = eval_at(b, sheet, rule, rule->f1, r, c);
    double d;
    ok = (res.t == V_BOOL && res.e) || (res.t == V_NUM && res.n != 0) || (res.t == V_STR && str_ieq(res.s, "TRUE")) ||
         (res.t == V_STR && as_number(&res, &d) && d != 0);
    val_free(&res);
    break; }
  default: {
    Value a = eval_at(b, sheet, rule, rule->f1, r, c), bb = val_empty();
    double x = 0, na = 0, nb = 0;
    int h1 = as_number(&a, &na), h2 = 0, isnum;
    if (rule->op == CO_BETWEEN || rule->op == CO_NOTBETWEEN) { bb = eval_at(b, sheet, rule, rule->f2, r, c); h2 = as_number(&bb, &nb); }
    if (rule->type == DV_TEXTLEN) { x = (double)utf8_len(text); isnum = 1; }
    else { isnum = v->t == V_NUM; x = isnum ? v->n : 0; }
    if (isnum && rule->type == DV_WHOLE && x != floor(x)) isnum = 0;
    if (isnum && rule->type == DV_TIME && (x < 0 || x >= 1) && h1 && na < 1 && (!h2 || nb < 1)) x -= floor(x);
    ok = isnum && compare(rule->op, x, h1, na, h2, nb);
    val_free(&a); val_free(&bb);
    break; }
  }
  if (cell) {
    cell = sheet_cell(s, r, c);
    if (cell) { val_free(&cell->v); cell->v = oldv; cell->f = oldf; if (!existed) sheet_cell_clear(s, r, c); }
    b->calcgen++;
  }
  return ok;
}
int dv_check_value(Book *b, int sheet, i32 r, i32 c, const Value *v, const DVRule **rule)
{
  if (rule) *rule = NULL;
  if (sheet < 0 || sheet >= b->nsh) return 1;
  return check(b, sheet, r, c, v, NULL, rule, 1);
}
int dv_check_text(Book *b, int sheet, i32 r, i32 c, const char *typed, const DVRule **rule)
{
  Value v;
  double n;
  int kind, ok, pos;
  if (rule) *rule = NULL;
  if (sheet < 0 || sheet >= b->nsh) return 1;
  if (typed[0] == '=' && typed[1]) {
    /* a formula: its result is what's checked */
    Formula *f = formula_parse(b, sheet, typed + 1, &pos);
    if (!f) return 1;
    v = book_eval_at(b, sheet, r, c, f);
    formula_free(f);
    ok = check(b, sheet, r, c, &v, NULL, rule, 1);
    val_free(&v);
    return ok;
  }
  if (typed[0] == '\'') v = val_str(typed + 1);
  else if (!typed[0]) v = val_empty();
  else if (str_ieq(typed, "TRUE")) v = val_bool(1);
  else if (str_ieq(typed, "FALSE")) v = val_bool(0);
  else if (parse_number(typed, &n, &kind)) v = val_num(n);
  else v = val_str(typed);
  ok = check(b, sheet, r, c, &v, typed[0] == '\'' ? typed + 1 : typed, rule, 1);
  val_free(&v);
  return ok;
}
/* the item list grown by one (copied) */
static int add_item(char ***items, int *n, int *cap, const char *s, size_t len)
{
  if (*n >= 5000) return 0;
  if (*n == *cap) {
    int nc = *cap ? *cap * 2 : 16;
    char **ni = (char **)realloc(*items, (size_t)nc * sizeof(char *));
    if (!ni) return 0;
    *items = ni; *cap = nc;
  }
  if (!((*items)[*n] = str_ndup(s, len))) return 0;
  (*n)++;
  return 1;
}
/* the cells of a rectangle as shown, empty ones skipped */
static void range_items(Book *b, int sheet, Rect a, char ***items, int *n, int *cap)
{
  Sheet *s = b->sh[sheet];
  i32 r, c, lr = sheet_last_row(s), lc = sheet_last_col(s);
  char t[512];
  u32 col;
  if (a.r2 > lr) a.r2 = lr;
  if (a.c2 > lc) a.c2 = lc;
  for (r = a.r1; r <= a.r2; r++)
    for (c = a.c1; c <= a.c2; c++) {
      const Value *v = cell_value(b, sheet, r, c);
      if (v->t == V_EMPTY) continue;
      book_shown_text(b, sheet, r, c, t, sizeof t, &col);
      if (!add_item(items, n, cap, t, strlen(t))) return;
    }
}
int dv_list_items(Book *b, int sheet, const DVRule *rule, i32 r, i32 c, char ***out)
{
  char **items = NULL;
  int n = 0, cap = 0;
  const Formula *f;
  Formula *g = NULL;
  *out = NULL;
  if (!rule || rule->type != DV_LIST || !(f = rule->f1) || !f->n) return 0;
  /* seen from r, c */
  if (rule->nareas && (rule->areas[0].r1 != r || rule->areas[0].c1 != c) && (g = formula_copy(f))) {
    formula_offset(g, r - rule->areas[0].r1, c - rule->areas[0].c1);
    f = g;
  }
  {
    const Tok *t = &f->t[0];
    int nt = f->n;
    while (nt > 1 && f->t[nt - 1].op == T_PAREN) nt--;
    if (nt == 1 && t->op == T_STR && t->s) {
      /* "a,b,c": the items between commas, trimmed */
      const char *p = t->s;
      while (*p) {
        const char *e = strchr(p, ',');
        const char *a = p, *z = e ? e : p + strlen(p);
        while (a < z && *a == ' ') a++;
        while (z > a && z[-1] == ' ') z--;
        if (z > a) add_item(&items, &n, &cap, a, (size_t)(z - a));
        if (!e) break;
        p = e + 1;
      }
    } else if (nt == 1 && (t->op == T_REF || t->op == T_AREA)) {
      Rect a;
      int sh = t->a == SHEET_SELF ? sheet : t->a;
      a.r1 = t->r1; a.c1 = t->c1; a.r2 = t->op == T_AREA ? t->r2 : t->r1; a.c2 = t->op == T_AREA ? t->c2 : t->c1;
      if (sh >= 0 && sh < b->nsh) range_items(b, sh, a, &items, &n, &cap);
    } else if (nt == 1 && t->op == T_NAME && t->s) {
      /* a name standing for a range */
      int i;
      for (i = 0; i < b->nnames; i++)
        if (str_ieq(b->names[i].name, t->s) && b->names[i].f && b->names[i].f->n >= 1) {
          const Tok *u = &b->names[i].f->t[0];
          if (u->op == T_REF || u->op == T_AREA) {
            Rect a;
            int sh = u->a == SHEET_SELF ? (b->names[i].sheet == SHEET_SELF ? sheet : b->names[i].sheet) : u->a;
            a.r1 = u->r1; a.c1 = u->c1; a.r2 = u->op == T_AREA ? u->r2 : u->r1; a.c2 = u->op == T_AREA ? u->c2 : u->c1;
            if (sh >= 0 && sh < b->nsh) range_items(b, sh, a, &items, &n, &cap);
          }
          break;
        }
    } else {
      /* a formula: its result (an array's elements) */
      Value v = book_eval_at(b, sheet, r, c, f);
      char t2[512];
      if (v.t == V_ARR && v.a) {
        int k;
        for (k = 0; k < v.a->rows * v.a->cols; k++) if (v.a->v[k].t != V_EMPTY && v.a->v[k].t != V_ERR) { value_text(&v.a->v[k], t2, sizeof t2); add_item(&items, &n, &cap, t2, strlen(t2)); }
      } else if (v.t != V_EMPTY && v.t != V_ERR) { value_text(&v, t2, sizeof t2); add_item(&items, &n, &cap, t2, strlen(t2)); }
      val_free(&v);
    }
  }
  formula_free(g);
  *out = items;
  return n;
}
int dv_invalid_cells(Book *b, int sheet, i32 *rows, i32 *cols, int max)
{
  Sheet *s;
  int i, j, n = 0;
  if (sheet < 0 || sheet >= b->nsh) return 0;
  s = b->sh[sheet];
  for (i = 0; i < s->ndvs && n < max; i++)
    for (j = 0; j < s->dvs[i].nareas && n < max; j++) {
      Rect a = s->dvs[i].areas[j];
      i32 r, c, lr = sheet_last_row(s), lc = sheet_last_col(s);
      if (a.r2 > lr) a.r2 = lr;
      if (a.c2 > lc) a.c2 = lc;
      for (r = a.r1; r <= a.r2 && n < max; r++)
        for (c = a.c1; c <= a.c2 && n < max; c++) {
          const Value *v;
          const DVRule *rule;
          if (!sheet_cell(s, r, c)) continue;
          v = cell_value(b, sheet, r, c);
          if (v->t == V_EMPTY) continue;
          if (dv_at(s, r, c) != &s->dvs[i]) continue;          /* (cells of a later rule are its) */
          if (!check(b, sheet, r, c, v, NULL, &rule, 0)) { rows[n] = r; cols[n] = c; n++; }
        }
    }
  return n;
}

/* ------------------------------------------------------------------ the dialog's boxes */
Formula *dv_value(Book *b, int sheet, const char *typed, int list)
{
  Buf t;
  Formula *f;
  const char *p;
  int pos;
  while (*typed == ' ') typed++;
  if (!*typed) return NULL;
  if (!list || *typed == '=' || *typed == '"') return *typed == '"' ? formula_parse(b, sheet, typed, &pos) : cond_value(b, sheet, typed);
  /* the items as one string, its quotes doubled */
  buf_init(&t);
  buf_addc(&t, '"');
  for (p = typed; *p; p++) { if (*p == '"') buf_addc(&t, '"'); buf_addc(&t, *p); }
  buf_adds(&t, "\"");
  f = t.data && !t.oom ? formula_parse(b, sheet, t.data, &pos) : NULL;
  buf_free(&t);
  return f;
}
void dv_value_text(Book *b, int sheet, const Formula *f, int list, char *out, size_t cap)
{
  out[0] = 0;
  if (!f) return;
  if (list && f->n == 1 && f->t[0].op == T_STR && f->t[0].s) { strncpy(out, f->t[0].s, cap - 1); out[cap - 1] = 0; return; }
  if (list) {
    Buf t;
    buf_init(&t);
    formula_text(b, sheet, f, &t);
    snprintf(out, cap, "=%s", t.data ? t.data : "");
    buf_free(&t);
    return;
  }
  cond_value_text(b, sheet, f, out, cap);
}

/* ------------------------------------------------------------------ .xlsx */
static const char *const dvtypes[DV_N] = { "none", "whole", "decimal", "list", "date", "time", "textLength", "custom" };
static const char *const dvops[CO_N] = { "between", "notBetween", "equal", "notEqual", "greaterThan", "lessThan", "greaterThanOrEqual", "lessThanOrEqual" };
static void attr(Buf *o, const char *name, const char *v)
{
  if (!v || !*v) return;
  buf_printf(o, " %s=\"", name);
  xml_escape(o, v, strlen(v));
  buf_adds(o, "\"");
}
static void formula_el(Book *b, int si, const char *tag, const Formula *f, Buf *o)
{
  Buf t;
  if (!f) return;
  buf_init(&t);
  xlsx_formula_text(b, si, f, &t);
  buf_printf(o, "<%s>", tag);
  xml_escape(o, t.data ? t.data : "", t.len);
  buf_printf(o, "</%s>", tag);
  buf_free(&t);
}
void dv_xlsx_write(Book *b, int si, Buf *o)
{
  Sheet *s = b->sh[si];
  int i, j;
  if (!s->ndvs) return;
  buf_printf(o, "<dataValidations count=\"%d\">", s->ndvs);
  for (i = 0; i < s->ndvs; i++) {
    const DVRule *r = &s->dvs[i];
    buf_adds(o, "<dataValidation");
    if (r->type != DV_ANY && r->type < DV_N) buf_printf(o, " type=\"%s\"", dvtypes[r->type]);
    if (r->errstyle == DVE_WARNING) buf_adds(o, " errorStyle=\"warning\"");
    else if (r->errstyle == DVE_INFO) buf_adds(o, " errorStyle=\"information\"");
    if (r->op != CO_BETWEEN && r->op < CO_N && r->type != DV_LIST && r->type != DV_CUSTOM && r->type != DV_ANY) buf_printf(o, " operator=\"%s\"", dvops[r->op]);
    if (r->blank) buf_adds(o, " allowBlank=\"1\"");
    if (r->type == DV_LIST && !r->dropdown) buf_adds(o, " showDropDown=\"1\"");     /* (the file's flag hides it) */
    if (r->showinput) buf_adds(o, " showInputMessage=\"1\"");
    if (r->showerror) buf_adds(o, " showErrorMessage=\"1\"");
    attr(o, "errorTitle", r->errtitle); attr(o, "error", r->errmsg); attr(o, "promptTitle", r->intitle); attr(o, "prompt", r->inmsg);
    buf_adds(o, " sqref=\"");
    for (j = 0; j < r->nareas; j++) { char t[40]; rect_text(r->areas[j], t); buf_printf(o, "%s%s", j ? " " : "", t); }
    buf_adds(o, "\">");
    if (r->type != DV_ANY) {
      formula_el(b, si, "formula1", r->f1, o);
      if (r->op == CO_BETWEEN || r->op == CO_NOTBETWEEN) formula_el(b, si, "formula2", r->f2, o);
    }
    buf_adds(o, "</dataValidation>");
  }
  buf_adds(o, "</dataValidations>");
}
static int flag(const char *v) { return v && (!strcmp(v, "1") || !strcmp(v, "true")); }
/* "A1:B4 C7" into areas; how many */
static int parse_areas(Book *b, const char *s, Rect **out)
{
  Rect *a = NULL;
  int n = 0;
  *out = NULL;
  while (*s && n < 1000) {
    u16 sh; Rect rc; u8 fl; int k;
    Rect *na;
    while (*s == ' ' || *s == ',') s++;
    if (!*s) break;
    if (!(k = parse_ref(b, s, &sh, &rc, &fl))) break;
    s += k;
    if (!(na = (Rect *)realloc(a, (size_t)(n + 1) * sizeof(Rect)))) break;
    a = na; a[n++] = rc;
  }
  *out = a;
  return n;
}
void dv_xlsx_read(Book *b, int si, Xml *x)
{
  Sheet *s = b->sh[si];
  DVRule r;
  const char *v;
  int depth = x->depth, k, pos;
  Buf f1, f2, sq;
  Buf *cur = NULL;
  dv_rule_init(&r);
  r.blank = flag(xml_attr(x, "allowBlank"));
  r.showinput = flag(xml_attr(x, "showInputMessage"));
  r.showerror = flag(xml_attr(x, "showErrorMessage"));
  r.dropdown = !flag(xml_attr(x, "showDropDown"));
  if ((v = xml_attr(x, "type"))) for (k = 0; k < DV_N; k++) if (!strcmp(dvtypes[k], v)) r.type = (u8)k;
  if ((v = xml_attr(x, "operator"))) for (k = 0; k < CO_N; k++) if (!strcmp(dvops[k], v)) r.op = (u8)k;
  if ((v = xml_attr(x, "errorStyle"))) r.errstyle = !strcmp(v, "warning") ? DVE_WARNING : !strcmp(v, "information") ? DVE_INFO : DVE_STOP;
  r.errtitle = dup0(xml_attr(x, "errorTitle")); r.errmsg = dup0(xml_attr(x, "error"));
  r.intitle = dup0(xml_attr(x, "promptTitle")); r.inmsg = dup0(xml_attr(x, "prompt"));
  buf_init(&f1); buf_init(&f2); buf_init(&sq);
  if ((v = xml_attr(x, "sqref"))) buf_adds(&sq, v);
  if (!x->pending_end) {
    /* formula1, formula2 (their text, or an x14 <xm:f> inside), and an x14 <xm:sqref> */
    while (xml_next(x) != XT_EOF) {
      if (x->type == XT_END && x->depth < depth) break;
      if (x->type == XT_START) {
        if (!strcmp(x->name, "formula1")) cur = &f1;
        else if (!strcmp(x->name, "formula2")) cur = &f2;
        else if (!strcmp(x->name, "sqref")) cur = &sq;
      } else if (x->type == XT_END && (!strcmp(x->name, "formula1") || !strcmp(x->name, "formula2") || !strcmp(x->name, "sqref"))) cur = NULL;
      else if (x->type == XT_TEXT && cur && cur->len < 8192) buf_add(cur, x->text.data, x->text.len);
    }
  }
  r.nareas = sq.data ? parse_areas(b, sq.data, &r.areas) : 0;
  if (f1.data && f1.data[0]) r.f1 = formula_parse(b, si, f1.data[0] == '=' ? f1.data + 1 : f1.data, &pos);
  if (f2.data && f2.data[0]) r.f2 = formula_parse(b, si, f2.data[0] == '=' ? f2.data + 1 : f2.data, &pos);
  buf_free(&f1); buf_free(&f2); buf_free(&sq);
  if (r.nareas && r.intitle && r.inmsg && r.errtitle && r.errmsg && (r.type == DV_ANY || r.f1) && s->ndvs < 10000) {
    DVRule *nd = (DVRule *)realloc(s->dvs, (size_t)(s->ndvs + 1) * sizeof(DVRule));
    if (nd) { s->dvs = nd; nd[s->ndvs++] = r; return; }
  }
  dv_rule_free(&r);
}

/* ------------------------------------------------------------------ book.c's hooks */
void data_sheet_free(Sheet *s)
{
  int i;
  for (i = 0; i < s->ndvs; i++) dv_rule_free(&s->dvs[i]);
  free(s->dvs);
  s->dvs = NULL; s->ndvs = 0;
  for (i = 0; i < s->nscens; i++) scenario_free(&s->scens[i]);
  free(s->scens);
  s->scens = NULL; s->nscens = 0;
  edit_ranges_free(s);
}
void data_sheet_clone(Sheet *dst, const Sheet *src)
{
  int i;
  dst->dvs = NULL; dst->ndvs = 0; dst->scens = NULL; dst->nscens = 0;
  edit_ranges_clone(dst, src);
  if (src->ndvs && (dst->dvs = (DVRule *)calloc((size_t)src->ndvs, sizeof(DVRule))))
    for (i = 0; i < src->ndvs; i++) if (dv_rule_copy(&dst->dvs[dst->ndvs], &src->dvs[i])) dst->ndvs++;
  if (src->nscens && (dst->scens = (Scenario *)calloc((size_t)src->nscens, sizeof(Scenario))))
    for (i = 0; i < src->nscens; i++) {
      const Scenario *a = &src->scens[i];
      Scenario *z = &dst->scens[dst->nscens];
      int k, ok = 1;
      *z = *a;
      z->comment = dup0(a->comment);
      z->r = (i32 *)malloc((size_t)(a->n + 1) * sizeof(i32)); z->c = (i32 *)malloc((size_t)(a->n + 1) * sizeof(i32));
      z->vals = (char **)calloc((size_t)a->n + 1, sizeof(char *));
      if (!z->comment || !z->r || !z->c || !z->vals) ok = 0;
      for (k = 0; ok && k < a->n; k++) { z->r[k] = a->r[k]; z->c[k] = a->c[k]; if (!(z->vals[k] = dup0(a->vals[k]))) ok = 0; }
      if (ok) dst->nscens++; else { z->n = k; scenario_free(z); }
    }
}
void data_each_formula(Sheet *sh, int home, void (*fn)(Formula *f, int home, void *arg), void *arg)
{
  int i;
  for (i = 0; i < sh->ndvs; i++) { if (sh->dvs[i].f1) fn(sh->dvs[i].f1, home, arg); if (sh->dvs[i].f2) fn(sh->dvs[i].f2, home, arg); }
}
/* a line index after lines were inserted (n > 0) or deleted at at; -1 if it was deleted */
static i32 moved(i32 i, i32 at, i32 n, i32 lim)
{
  if (n > 0) return i >= at ? (i + n < lim ? i + n : -1) : i;
  if (i >= at && i < at - n) return -1;
  return i >= at - n ? i + n : i;
}
void data_shift(Sheet *s, int rows, i32 at, i32 n)
{
  int i, j, k, m;
  i32 lim = rows ? MAXROW : MAXCOL;
  edit_ranges_shift(s, rows, at, n);
  for (i = m = 0; i < s->ndvs; i++) {
    DVRule *r = &s->dvs[i];
    i32 ar = r->nareas ? r->areas[0].r1 : 0, ac = r->nareas ? r->areas[0].c1 : 0, *pa = rows ? &ar : &ac;
    /* where the first cell went (as a point; formulas moved with the rest of the book) */
    if (n > 0 && *pa >= at) *pa += n;
    else if (n < 0 && *pa >= at - n) *pa += n;
    for (j = k = 0; j < r->nareas; j++) {
      Rect a = r->areas[j];
      i32 *a1 = rows ? &a.r1 : &a.c1, *a2 = rows ? &a.r2 : &a.c2;
      if (n > 0) {
        if (*a1 >= at) *a1 += n;
        if (*a2 >= at && *a2 < lim - 1) *a2 += n;
        if (*a1 >= lim) continue;
        if (*a2 >= lim) *a2 = lim - 1;
      } else {
        i32 d = -n;
        if (*a1 >= at && *a2 < at + d) continue;
        if (*a1 >= at + d) *a1 -= d; else if (*a1 >= at) *a1 = at;
        if (*a2 >= lim - 1) {} else if (*a2 >= at + d) *a2 -= d; else if (*a2 >= at) *a2 = at - 1;
      }
      r->areas[k++] = a;
    }
    r->nareas = k;
    if (!k) { dv_rule_free(r); continue; }
    reanchor(r, ar, ac);
    s->dvs[m++] = *r;
  }
  s->ndvs = m;
  /* scenarios' cells follow (deleted ones drop out) */
  for (i = 0; i < s->nscens; i++) {
    Scenario *sc = &s->scens[i];
    for (j = k = 0; j < sc->n; j++) {
      i32 p = moved(rows ? sc->r[j] : sc->c[j], at, n, lim);
      if (p < 0) { free(sc->vals[j]); continue; }
      sc->r[k] = rows ? p : sc->r[j]; sc->c[k] = rows ? sc->c[j] : p; sc->vals[k] = sc->vals[j];
      k++;
    }
    sc->n = k;
  }
}
