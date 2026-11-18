/*
 * formula.c - formulas: the parser (text to postfix tokens), the printer (tokens back to text), and
 * reference adjustment for copying, filling, and rows or columns going in and out.
 *
 * Precedence, highest first, as in Excel: the reference operators (: then space then ,), negation,
 * percent, ^, * and /, + and -, &, then the comparisons. So -2^2 is 4 and 2^-1 is 0.5.
 */
#include "book.h"
#include "table.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>

#define MAXDEPTH 100             /* nesting of brackets and calls (hostile formulas) */
#define MAXTOKENS 8192

typedef struct {
  Book *b;
  int sheet;
  const char *s, *p;
  Formula *f;
  int cap;
  int depth;
  int err;                        /* set on the first error */
  int errpos;
  const char *scope[64];          /* LET and LAMBDA parameter names in force */
  int nscope;
  int lambdarg;                   /* parsing an argument of LAMBDA (where [x] is an optional parameter) */
} P;
int formula_file_mode;
int formula_lz_none;              /* Formula.lz for a formula without lazy functions (calc.c) */
static int in_scope(P *ps, const char *name, size_t len)
{
  int i;
  for (i = ps->nscope - 1; i >= 0; i--) if (strlen(ps->scope[i]) == len && !strncasecmp(ps->scope[i], name, len)) return 1;
  return 0;
}
static void parse_postfix_call(P *ps);

static int emit(P *ps, const Tok *t)
{
  if (ps->err) return 0;
  if (ps->f->n >= MAXTOKENS) { ps->err = 1; return 0; }
  if (ps->f->n == ps->cap) {
    int cap = ps->cap ? ps->cap * 2 : 16;
    Tok *nt = (Tok *)realloc(ps->f->t, (size_t)cap * sizeof(Tok));
    if (!nt) { ps->err = 1; return 0; }
    ps->f->t = nt; ps->cap = cap;
  }
  ps->f->t[ps->f->n++] = *t;
  return 1;
}
static int emit_op(P *ps, int op)
{
  Tok t;
  memset(&t, 0, sizeof t);
  t.op = (u8)op;
  return emit(ps, &t);
}
static void fail(P *ps)
{
  if (!ps->err) { ps->err = 1; ps->errpos = (int)(ps->p - ps->s); }
}
static void skip_ws(P *ps) { while (*ps->p == ' ' || *ps->p == '\n' || *ps->p == '\r' || *ps->p == '\t') ps->p++; }

static void parse_expr(P *ps);
/* a number literal: 12, 1.5, .5, 1e3, 1.2E-4 */
static int parse_num(P *ps)
{
  const char *p = ps->p;
  char buf[64];
  int n = 0;
  Tok t;
  if (!isdigit((u8)*p) && !(*p == '.' && isdigit((u8)p[1]))) return 0;
  while ((isdigit((u8)*p) || *p == '.') && n < 60) buf[n++] = *p++;
  if ((*p == 'e' || *p == 'E') && (isdigit((u8)p[1]) || ((p[1] == '+' || p[1] == '-') && isdigit((u8)p[2])))) {
    buf[n++] = *p++;
    if (*p == '+' || *p == '-') buf[n++] = *p++;
    while (isdigit((u8)*p) && n < 62) buf[n++] = *p++;
  }
  buf[n] = 0;
  if (strchr(buf, '.') != strrchr(buf, '.')) { fail(ps); return 1; }
  memset(&t, 0, sizeof t);
  t.op = T_NUM;
  t.n = strtod(buf, NULL);
  ps->p = p;
  emit(ps, &t);
  return 1;
}
/* "text" with "" for a quote */
static int parse_string(P *ps)
{
  Buf b;
  const char *p = ps->p + 1;
  Tok t;
  if (*ps->p != '"') return 0;
  buf_init(&b);
  for (;;) {
    if (!*p) { buf_free(&b); fail(ps); return 1; }
    if (*p == '"') { if (p[1] == '"') { buf_addc(&b, '"'); p += 2; continue; } p++; break; }
    buf_addc(&b, *p++);
  }
  memset(&t, 0, sizeof t);
  t.op = T_STR;
  t.s = buf_take(&b);
  if (!t.s) t.s = str_dup("");
  ps->p = p;
  if (!emit(ps, &t)) free(t.s);
  return 1;
}
/* {1,2;3,4}: constants only, rows split by ; */
static int parse_array(P *ps)
{
  Value tmp[1024];
  int n = 0, rows = 1, cols = 0, col = 0, i;
  Tok t;
  if (*ps->p != '{') return 0;
  ps->p++;
  for (;;) {
    Value v = val_empty();
    double d;
    int neg = 0, len;
    skip_ws(ps);
    if (*ps->p == '-') { neg = 1; ps->p++; }
    if (*ps->p == '"') {
      Buf b;
      const char *p = ps->p + 1;
      buf_init(&b);
      while (*p && !(*p == '"' && p[1] != '"')) { if (*p == '"') p++; buf_addc(&b, *p++); }
      if (*p != '"') { buf_free(&b); fail(ps); break; }
      v = val_str(b.data ? b.data : "");
      buf_free(&b);
      ps->p = p + 1;
    } else if (isdigit((u8)*ps->p) || *ps->p == '.') {
      char *end;
      d = strtod(ps->p, &end);
      ps->p = end;
      v = val_num(neg ? -d : d);
    } else if (!strncasecmp(ps->p, "TRUE", 4)) { v = val_bool(1); ps->p += 4; }
    else if (!strncasecmp(ps->p, "FALSE", 5)) { v = val_bool(0); ps->p += 5; }
    else if (*ps->p == '#' && err_parse(ps->p, &len)) { v = val_err(err_parse(ps->p, NULL)); ps->p += len; }
    else { fail(ps); break; }
    if (n >= 1024) { val_free(&v); fail(ps); break; }
    tmp[n++] = v;
    col++;
    skip_ws(ps);
    if (*ps->p == ',') { ps->p++; continue; }
    if (*ps->p == ';') {
      if (cols && col != cols) { fail(ps); break; }
      cols = col; col = 0; rows++; ps->p++;
      continue;
    }
    if (*ps->p == '}') {
      if (cols && col != cols) { fail(ps); break; }
      cols = col;
      ps->p++;
      break;
    }
    fail(ps);
    break;
  }
  if (ps->err) { for (i = 0; i < n; i++) val_free(&tmp[i]); return 1; }
  memset(&t, 0, sizeof t);
  t.op = T_ARRAY;
  if (!(t.arr = arr_new(rows, cols))) { for (i = 0; i < n; i++) val_free(&tmp[i]); fail(ps); return 1; }
  for (i = 0; i < n; i++) t.arr->v[i] = tmp[i];
  if (!emit(ps, &t)) arr_free(t.arr);
  return 1;
}
/* a function call: NAME( args ) */
static void parse_call(P *ps, const char *name, size_t len)
{
  char nm[64];
  int argc = 0, id, local = 0, binder, saved = ps->nscope;
  Tok t;
  if (len > 63) { fail(ps); return; }
  memcpy(nm, name, len); nm[len] = 0;
  /* Excel writes newer functions with _xlfn. in files (and _xlws. for FILTER and SORT), and LET
     names called as functions with _xlpm. */
  if (!strncasecmp(nm, "_xlfn.", 6)) memmove(nm, nm + 6, strlen(nm + 6) + 1);
  if (!strncasecmp(nm, "_xlws.", 6)) memmove(nm, nm + 6, strlen(nm + 6) + 1);
  if (!strncasecmp(nm, "_xlpm.", 6)) { memmove(nm, nm + 6, strlen(nm + 6) + 1); local = 1; }
  if (in_scope(ps, nm, strlen(nm))) local = 1;
  binder = local ? 0 : str_ieq(nm, "LET") ? 1 : str_ieq(nm, "LAMBDA") ? 2 : 0;
  ps->p++;                                   /* ( */
  if (++ps->depth > MAXDEPTH) { fail(ps); return; }
  skip_ws(ps);
  if (*ps->p == ')') ps->p++;
  else for (;;) {
    int start = ps->f->n;
    skip_ws(ps);
    if (*ps->p == ',' || *ps->p == ')') emit_op(ps, T_MISSING);
    else { int la = ps->lambdarg; ps->lambdarg = binder == 2; parse_expr(ps); ps->lambdarg = la; }
    if (ps->err) return;
    argc++;
    skip_ws(ps);
    /* LET(name, value, ..., body) and LAMBDA(param, ..., body): a lone name followed by more
       arguments is a parameter; the arguments after it see it */
    if (binder && *ps->p == ',' && ps->f->n == start + 1 && ps->f->t[start].op == T_NAME && (binder == 2 || argc % 2 == 1)) {
      ps->f->t[start].flags |= TN_LOCAL;
      if (ps->nscope < 64) ps->scope[ps->nscope++] = ps->f->t[start].s;
    }
    if (*ps->p == ',') { ps->p++; if (argc >= 255) { fail(ps); return; } continue; }
    if (*ps->p == ')') { ps->p++; break; }
    fail(ps);
    return;
  }
  ps->nscope = saved;
  ps->depth--;
  /* the file forms of @ and # */
  if (argc == 1 && !local && str_ieq(nm, "SINGLE")) { emit_op(ps, T_AT); return; }
  if (argc == 1 && !local && str_ieq(nm, "ANCHORARRAY")) { emit_op(ps, T_SPILL); return; }
  memset(&t, 0, sizeof t);
  t.op = T_FUNC;
  t.flags = (u8)argc;
  id = local ? -1 : func_lookup(nm);
  if (id < 0) {
    /* unknown: kept by name; a LET or defined name holding a LAMBDA when evaluated, else #NAME? */
    t.a = 0xFFFF;
    t.s = str_dup(nm);
    t.r1 = local;
  } else t.a = (u16)id;
  if (!emit(ps, &t)) free(t.s);
  parse_postfix_call(ps);
}
/* a call of what came before: LAMBDA(x,x+1)(2) */
static void parse_postfix_call(P *ps)
{
  while (!ps->err && *ps->p == '(') {
    int argc = 0;
    Tok t;
    ps->p++;
    if (++ps->depth > MAXDEPTH) { fail(ps); return; }
    skip_ws(ps);
    if (*ps->p == ')') ps->p++;
    else for (;;) {
      skip_ws(ps);
      if (*ps->p == ',' || *ps->p == ')') emit_op(ps, T_MISSING);
      else parse_expr(ps);
      if (ps->err) return;
      argc++;
      skip_ws(ps);
      if (*ps->p == ',') { ps->p++; if (argc >= 254) { fail(ps); return; } continue; }
      if (*ps->p == ')') { ps->p++; break; }
      fail(ps);
      return;
    }
    ps->depth--;
    memset(&t, 0, sizeof t);
    t.op = T_CALL;
    t.flags = (u8)argc;
    emit(ps, &t);
  }
}
/* a trim reference: A1.:E10 (leading blanks off), A1:.E10 (trailing), A1.:.E10 (both); 1 if one was read */
static int parse_trimref(P *ps)
{
  char tmp[260];
  const char *p = ps->p;
  int n = 0, i, q = 0, mode = 0, colon = -1, len, removed = 0, lastdot = -1;
  u16 sheet;
  Rect r;
  u8 flags;
  Tok t;
  for (i = 0; p[i] && i < 256; i++) {
    char ch = p[i];
    if (ch == '\'') { q = !q; tmp[n++] = ch; continue; }
    if (q) { tmp[n++] = ch; continue; }
    if (!(isalnum((u8)ch) || ch == '$' || ch == '!' || ch == '.' || ch == ':' || ch == '_' || (u8)ch >= 0x80)) break;
    if (ch == ':' && colon < 0) {
      colon = n;
      if (n && tmp[n - 1] == '.') { n--; mode |= 1; removed++; lastdot = i - 1; }
      tmp[n++] = ':';
      if (p[i + 1] == '.') { mode |= 2; removed++; lastdot = ++i; }
      continue;
    }
    tmp[n++] = ch;
  }
  tmp[n] = 0;
  if (!mode || colon < 0) return 0;
  len = parse_ref(ps->b, tmp, &sheet, &r, &flags);
  if (len <= colon) return 0;
  if (len + removed <= lastdot) return 0;
  memset(&t, 0, sizeof t);
  t.op = T_AREA; t.flags = flags; t.a = sheet;
  t.r1 = r.r1; t.c1 = r.c1; t.r2 = r.r2; t.c2 = r.c2;
  emit(ps, &t);
  memset(&t, 0, sizeof t);
  t.op = T_TRIM; t.flags = (u8)mode;
  emit(ps, &t);
  ps->p += len + removed;
  return 1;
}
/* a structured reference: the table's name (len characters at name, perhaps none) and its brackets at ps->p */
/* [name] followed by a comma: LAMBDA's optional parameter */
static int optional_param(const char *p)
{
  p++;
  while (isalnum((u8)*p) || *p == '_' || *p == '.' || (u8)*p >= 0x80) p++;
  if (*p != ']') return 0;
  p++;
  while (*p == ' ') p++;
  return *p == ',';
}
static void parse_tref(P *ps, const char *name, size_t len)
{
  int n = table_ref_scan(ps->p);
  char *text, *tidy;
  Tok t;
  if (!n) { fail(ps); return; }
  if (!(text = str_ndup(name, len + (size_t)n))) { fail(ps); return; }
  tidy = table_ref_tidy(text);
  free(text);
  if (!tidy) { fail(ps); return; }
  memset(&t, 0, sizeof t);
  t.op = T_TABLE;
  t.s = tidy;
  ps->p += n;
  if (!emit(ps, &t)) free(tidy);
}
static void parse_primary(P *ps)
{
  Tok t;
  int n;
  u16 sheet;
  Rect r;
  u8 flags;
  skip_ws(ps);
  memset(&t, 0, sizeof t);
  if (!*ps->p) { fail(ps); return; }
  if (parse_trimref(ps)) return;
  if (parse_num(ps) || parse_string(ps) || parse_array(ps)) return;
  if (*ps->p == '(') {
    ps->p++;
    if (++ps->depth > MAXDEPTH) { fail(ps); return; }
    parse_expr(ps);
    skip_ws(ps);
    /* (A1:B2,D4): the union of references */
    while (!ps->err && *ps->p == ',') { ps->p++; parse_expr(ps); emit_op(ps, T_UNION); skip_ws(ps); }
    if (*ps->p != ')') { fail(ps); return; }
    ps->p++;
    ps->depth--;
    emit_op(ps, T_PAREN);
    parse_postfix_call(ps);
    return;
  }
  /* [name]: an optional LAMBDA parameter (elsewhere [Col] is a structured reference to the formula's table) */
  if (*ps->p == '[' && ps->lambdarg && optional_param(ps->p)) {
    const char *st = ++ps->p;
    while (isalnum((u8)*ps->p) || *ps->p == '_' || *ps->p == '.' || (u8)*ps->p >= 0x80) ps->p++;
    if (ps->p == st || *ps->p != ']') { fail(ps); return; }
    if (ps->p - st > 6 && !strncasecmp(st, "_xlpm.", 6)) st += 6;
    t.op = T_NAME; t.flags = TN_OPT | TN_LOCAL;
    t.s = str_ndup(st, (size_t)(ps->p - st));
    ps->p++;
    if (!t.s || !emit(ps, &t)) { free(t.s); fail(ps); }
    return;
  }
  if (*ps->p == '#' && (n = 0, err_parse(ps->p, &n))) {
    t.op = T_ERR;
    t.a = (u16)err_parse(ps->p, NULL);
    ps->p += n;
    emit(ps, &t);
    return;
  }
  if (*ps->p == '[') { parse_tref(ps, ps->p, 0); return; }
  /* a reference (possibly with a sheet name) */
  if ((n = parse_ref(ps->b, ps->p, &sheet, &r, &flags))) {
    t.op = (r.r1 == r.r2 && r.c1 == r.c2 && !(flags & (RF_COLS | RF_ROWS)) && !strchr(ps->p, ':')) ? T_REF : T_AREA;
    /* a single cell written as A1 (no colon within the reference text) is a T_REF */
    {
      const char *q = ps->p, *colon = NULL;
      int k;
      for (k = 0; k < n; k++) if (q[k] == ':') colon = q + k;
      t.op = colon ? T_AREA : T_REF;
    }
    t.flags = flags;
    t.a = sheet;
    t.r1 = r.r1; t.c1 = r.c1; t.r2 = r.r2; t.c2 = r.c2;
    ps->p += n;
    emit(ps, &t);
    if (t.op == T_REF && *ps->p == '#') { ps->p++; emit_op(ps, T_SPILL); }   /* A1#: its spill range */
    return;
  }
  /* a name: a function (followed by "("), TRUE/FALSE, or a defined name */
  if (isalpha((u8)*ps->p) || *ps->p == '_' || *ps->p == '\\' || (u8)*ps->p >= 0x80) {
    const char *st = ps->p;
    size_t len;
    while (isalnum((u8)*ps->p) || *ps->p == '_' || *ps->p == '.' || *ps->p == '\\' || (u8)*ps->p >= 0x80) ps->p++;
    len = (size_t)(ps->p - st);
    if (*ps->p == '(') { parse_call(ps, st, len); return; }
    if (*ps->p == '[') { parse_tref(ps, st, len); return; }
    if (len == 4 && !strncasecmp(st, "TRUE", 4)) { t.op = T_BOOL; t.a = 1; emit(ps, &t); return; }
    if (len == 5 && !strncasecmp(st, "FALSE", 5)) { t.op = T_BOOL; t.a = 0; emit(ps, &t); return; }
    t.op = T_NAME;
    /* files: _xlpm.x is a LET/LAMBDA name, _xleta.SUM a function given by name */
    if (len > 6 && !strncasecmp(st, "_xlpm.", 6)) { st += 6; len -= 6; t.flags = TN_LOCAL; }
    else if (len > 7 && !strncasecmp(st, "_xleta.", 7)) { st += 7; len -= 7; }
    if (in_scope(ps, st, len)) t.flags |= TN_LOCAL;
    t.s = str_ndup(st, len);
    if (!t.s || !emit(ps, &t)) { free(t.s); fail(ps); }
    return;
  }
  fail(ps);
}
/* primary (: primary)* - the range operator joins references (A1:B2 is already one token; this is
   for INDEX(...):C5 and names) */
static void parse_range(P *ps)
{
  parse_primary(ps);
  while (!ps->err) {
    skip_ws(ps);
    if (*ps->p != ':') break;
    ps->p++;
    parse_primary(ps);
    emit_op(ps, T_RANGE);
  }
}
static void parse_unary(P *ps)
{
  skip_ws(ps);
  if (*ps->p == '@') { ps->p++; if (++ps->depth > MAXDEPTH) { fail(ps); return; } parse_unary(ps); ps->depth--; emit_op(ps, T_AT); return; }
  if (*ps->p == '-') { ps->p++; if (++ps->depth > MAXDEPTH) { fail(ps); return; } parse_unary(ps); ps->depth--; emit_op(ps, T_NEG); return; }
  if (*ps->p == '+') { ps->p++; if (++ps->depth > MAXDEPTH) { fail(ps); return; } parse_unary(ps); ps->depth--; emit_op(ps, T_POS); return; }
  parse_range(ps);
}
static void parse_percent(P *ps)
{
  parse_unary(ps);
  for (;;) { skip_ws(ps); if (*ps->p != '%' || ps->err) break; ps->p++; emit_op(ps, T_PCT); }
}
static void parse_power(P *ps)
{
  parse_percent(ps);
  for (;;) { skip_ws(ps); if (*ps->p != '^' || ps->err) break; ps->p++; parse_percent(ps); emit_op(ps, T_POW); }
}
static void parse_term(P *ps)
{
  parse_power(ps);
  for (;;) {
    int op;
    skip_ws(ps);
    if (ps->err || (*ps->p != '*' && *ps->p != '/')) break;
    op = *ps->p++ == '*' ? T_MUL : T_DIV;
    parse_power(ps);
    emit_op(ps, op);
  }
}
static void parse_additive(P *ps)
{
  parse_term(ps);
  for (;;) {
    int op;
    skip_ws(ps);
    if (ps->err || (*ps->p != '+' && *ps->p != '-')) break;
    op = *ps->p++ == '+' ? T_ADD : T_SUB;
    parse_term(ps);
    emit_op(ps, op);
  }
}
static void parse_concat(P *ps)
{
  parse_additive(ps);
  for (;;) { skip_ws(ps); if (ps->err || *ps->p != '&') break; ps->p++; parse_additive(ps); emit_op(ps, T_CAT); }
}
static void parse_expr(P *ps)
{
  parse_concat(ps);
  for (;;) {
    int op;
    const char *p;
    skip_ws(ps);
    p = ps->p;
    if (ps->err) break;
    if (p[0] == '<' && p[1] == '>') { op = T_NE; ps->p += 2; }
    else if (p[0] == '<' && p[1] == '=') { op = T_LE; ps->p += 2; }
    else if (p[0] == '>' && p[1] == '=') { op = T_GE; ps->p += 2; }
    else if (p[0] == '<') { op = T_LT; ps->p++; }
    else if (p[0] == '>') { op = T_GT; ps->p++; }
    else if (p[0] == '=') { op = T_EQ; ps->p++; }
    else break;
    parse_concat(ps);
    emit_op(ps, op);
  }
}

void formula_free(Formula *f)
{
  int i;
  if (!f) return;
  for (i = 0; i < f->n; i++) { free(f->t[i].s); arr_free(f->t[i].arr); }
  arr_free(f->pycache);
  free(f->t);
  if (f->lz != &formula_lz_none) free(f->lz);
  free(f);
}
Formula *formula_copy(const Formula *f)
{
  Formula *c = (Formula *)calloc(1, sizeof *c);
  int i;
  if (!c) return NULL;
  c->volatile_ = f->volatile_;
  c->legacy = f->legacy; c->cse = f->cse; c->arrayeval = f->arrayeval;
  c->arows = f->arows; c->acols = f->acols; c->sr = f->sr; c->sc = f->sc;
  if (f->pycache) {
    Value v, cp;
    v = val_empty(); v.t = V_ARR; v.a = f->pycache;
    cp = val_copy(&v);
    c->pycache = cp.t == V_ARR ? cp.a : NULL;
  }
  if (f->n && !(c->t = (Tok *)malloc((size_t)f->n * sizeof(Tok)))) { free(c); return NULL; }
  for (i = 0; i < f->n; i++) {
    c->t[i] = f->t[i];
    if (f->t[i].s) c->t[i].s = str_dup(f->t[i].s);
    if (f->t[i].arr) {
      Value v, cp;
      v = val_empty(); v.t = V_ARR; v.a = f->t[i].arr;
      cp = val_copy(&v);
      c->t[i].arr = cp.t == V_ARR ? cp.a : NULL;
    }
    c->n = i + 1;
  }
  return c;
}
int func_volatile(int id);
/* see book.h */
Formula *formula_parse(Book *b, int sheet, const char *text, int *errpos)
{
  P ps;
  int i, depth;
  memset(&ps, 0, sizeof ps);
  ps.b = b; ps.sheet = sheet; ps.s = ps.p = text;
  if (!(ps.f = (Formula *)calloc(1, sizeof(Formula)))) return NULL;
  parse_expr(&ps);
  skip_ws(&ps);
  if (!ps.err && *ps.p) fail(&ps);
  /* the postfix sequence must leave exactly one value */
  for (i = depth = 0; !ps.err && i < ps.f->n; i++) {
    Tok *t = &ps.f->t[i];
    switch (t->op) {
    case T_ADD: case T_SUB: case T_MUL: case T_DIV: case T_POW: case T_CAT: case T_EQ: case T_NE: case T_LT:
    case T_GT: case T_LE: case T_GE: case T_RANGE: case T_UNION: case T_ISECT: depth--; break;
    case T_NEG: case T_POS: case T_PCT: case T_PAREN: case T_AT: case T_SPILL: case T_TRIM: break;
    case T_CALL: depth -= t->flags; break;
    case T_FUNC: depth -= t->flags - 1; if (t->a != 0xFFFF && func_volatile(t->a)) ps.f->volatile_ = 1; break;
    default: depth++;
    }
    if (depth < 1) fail(&ps);
  }
  if (!ps.err && depth != 1) fail(&ps);
  if (ps.err) {
    if (errpos) *errpos = ps.errpos;
    formula_free(ps.f);
    return NULL;
  }
  return ps.f;
}

/* ------------------------------------------------------------------ printing */
/* precedence of an operator token for bracketing (higher binds tighter) */
static int prec(int op)
{
  switch (op) {
  case T_EQ: case T_NE: case T_LT: case T_GT: case T_LE: case T_GE: return 1;
  case T_CAT: return 2;
  case T_ADD: case T_SUB: return 3;
  case T_MUL: case T_DIV: return 4;
  case T_POW: return 5;
  case T_PCT: return 6;
  case T_NEG: case T_POS: return 7;
  case T_RANGE: case T_UNION: case T_ISECT: return 9;
  default: return 10;
  }
}
static const char *op_text(int op)
{
  switch (op) {
  case T_ADD: return "+"; case T_SUB: return "-"; case T_MUL: return "*"; case T_DIV: return "/"; case T_POW: return "^";
  case T_CAT: return "&"; case T_EQ: return "="; case T_NE: return "<>"; case T_LT: return "<"; case T_GT: return ">";
  case T_LE: return "<="; case T_GE: return ">="; case T_RANGE: return ":"; case T_UNION: return ","; case T_ISECT: return " ";
  }
  return "?";
}
/* a sheet name as a formula writes it: quoted when it has spaces or punctuation or looks like a
   reference */
static void sheet_prefix(Book *b, u16 sheet, Buf *out)
{
  const char *name;
  int quote = 0;
  const char *p;
  if (!b || sheet >= b->nsh) { buf_adds(out, "#REF!"); return; }
  name = b->sh[sheet]->name;
  for (p = name; *p; p++) if (!isalnum((u8)*p) && *p != '_' && (u8)*p < 0x80) quote = 1;
  if (isdigit((u8)name[0])) quote = 1;
  {
    u16 s2; Rect r; u8 f;
    if (parse_ref(NULL, name, &s2, &r, &f) == (int)strlen(name)) quote = 1;
  }
  if (quote) {
    buf_addc(out, '\'');
    for (p = name; *p; p++) { if (*p == '\'') buf_addc(out, '\''); buf_addc(out, *p); }
    buf_addc(out, '\'');
  } else buf_adds(out, name);
  buf_addc(out, '!');
}
static void ref_tok_text(Book *b, int sheet, const Tok *t, Buf *out)
{
  char tmp[32];
  if (t->a != SHEET_SELF && ((t->flags & RF_SHEET) || (int)t->a != sheet)) sheet_prefix(b, t->a, out);
  if (t->op == T_AREA && (t->flags & RF_COLS)) {
    char c1[8], c2[8];
    col_name(t->c1, c1); col_name(t->c2, c2);
    buf_printf(out, "%s%s:%s%s", t->flags & RF_ABSC1 ? "$" : "", c1, t->flags & RF_ABSC2 ? "$" : "", c2);
    return;
  }
  if (t->op == T_AREA && (t->flags & RF_ROWS)) {
    buf_printf(out, "%s%ld:%s%ld", t->flags & RF_ABSR1 ? "$" : "", (long)t->r1 + 1, t->flags & RF_ABSR2 ? "$" : "", (long)t->r2 + 1);
    return;
  }
  ref_text(t->r1, t->c1, (t->flags & RF_ABSR1) != 0, (t->flags & RF_ABSC1) != 0, tmp);
  buf_adds(out, tmp);
  if (t->op == T_AREA) {
    ref_text(t->r2, t->c2, (t->flags & RF_ABSR2) != 0, (t->flags & RF_ABSC2) != 0, tmp);
    buf_addc(out, ':');
    buf_adds(out, tmp);
  }
}
static void num_text(double n, Buf *out)
{
  char tmp[64];
  if (n == (double)(long)n && n > -1e15 && n < 1e15) sprintf(tmp, "%ld", (long)n);
  else {
    sprintf(tmp, "%.15g", n);
    /* the shortest that reads back the same */
    {
      int p;
      for (p = 1; p <= 17; p++) { char t2[64]; sprintf(t2, "%.*g", p, n); if (strtod(t2, NULL) == n) { strcpy(tmp, t2); break; } }
    }
  }
  buf_adds(out, tmp);
}
void val_literal(const Value *v, Buf *out);
/* one value written as a formula literal */
void val_literal(const Value *v, Buf *out)
{
  const char *p;
  switch (v->t) {
  case V_NUM: num_text(v->n, out); break;
  case V_BOOL: buf_adds(out, v->e ? "TRUE" : "FALSE"); break;
  case V_ERR: buf_adds(out, err_name(v->e)); break;
  case V_STR:
    buf_addc(out, '"');
    for (p = v->s; *p; p++) { if (*p == '"') buf_addc(out, '"'); buf_addc(out, *p); }
    buf_addc(out, '"');
    break;
  default: break;
  }
}
/* a defined name of that name (so SUM is a name, not the function given by name) */
static int book_has_name(Book *b, const char *name)
{
  int i;
  if (!b) return 0;
  for (i = 0; i < b->nnames; i++) if (str_ieq(b->names[i].name, name)) return 1;
  return 0;
}
/* see book.h: the printer runs the postfix sequence with a stack of texts and their precedence */
void formula_text(Book *b, int sheet, const Formula *f, Buf *out)
{
  typedef struct { char *s; int prec; } Item;
  Item *st;
  int sp = 0, i, ok = 1;
  if (!f || !f->n) return;
  if (!(st = (Item *)calloc((size_t)f->n + 1, sizeof(Item)))) return;
  for (i = 0; i < f->n && ok; i++) {
    const Tok *t = &f->t[i];
    Buf tb;
    int pr = 10;
    buf_init(&tb);
    switch (t->op) {
    case T_NUM: num_text(t->n, &tb); break;
    case T_STR: { Value v = val_empty(); v.t = V_STR; v.s = t->s; val_literal(&v, &tb); break; }
    case T_BOOL: buf_adds(&tb, t->a ? "TRUE" : "FALSE"); break;
    case T_ERR: buf_adds(&tb, err_name(t->a)); break;
    case T_REF: case T_AREA: ref_tok_text(b, sheet, t, &tb); break;
    case T_NAME:
      if (t->flags & TN_OPT) buf_addc(&tb, '[');
      if (formula_file_mode && (t->flags & TN_LOCAL)) buf_adds(&tb, "_xlpm.");
      else if (formula_file_mode && func_lookup(t->s) >= 0 && !book_has_name(b, t->s)) buf_adds(&tb, "_xleta.");
      buf_adds(&tb, t->s);
      if (t->flags & TN_OPT) buf_addc(&tb, ']');
      break;
    case T_TABLE: table_ref_text(b, sheet, t->s ? t->s : "", &tb); break;
    case T_MISSING: break;
    case T_ARRAY: {
      int r, c;
      buf_addc(&tb, '{');
      for (r = 0; t->arr && r < t->arr->rows; r++) {
        if (r) buf_addc(&tb, ';');
        for (c = 0; c < t->arr->cols; c++) { if (c) buf_addc(&tb, ','); val_literal(&t->arr->v[r * t->arr->cols + c], &tb); }
      }
      buf_addc(&tb, '}');
      break; }
    case T_PAREN:
      if (sp < 1) { ok = 0; break; }
      buf_addc(&tb, '('); buf_adds(&tb, st[sp - 1].s); buf_addc(&tb, ')');
      free(st[--sp].s);
      break;
    case T_AT: case T_SPILL: case T_TRIM:
      if (sp < 1) { ok = 0; break; }
      if (formula_file_mode) {
        buf_adds(&tb, t->op == T_AT ? "_xlfn.SINGLE(" : t->op == T_SPILL ? "_xlfn.ANCHORARRAY(" : "_xlfn.TRIMRANGE(");
        buf_adds(&tb, st[sp - 1].s);
        if (t->op == T_TRIM) buf_printf(&tb, ",%d,%d", t->flags, t->flags);
        buf_addc(&tb, ')');
      } else if (t->op == T_AT) {
        pr = 7;
        buf_addc(&tb, '@');
        if (st[sp - 1].prec < pr) { buf_addc(&tb, '('); buf_adds(&tb, st[sp - 1].s); buf_addc(&tb, ')'); }
        else buf_adds(&tb, st[sp - 1].s);
      } else if (t->op == T_SPILL) {
        buf_adds(&tb, st[sp - 1].s);
        buf_addc(&tb, '#');
      } else {
        /* the dots go round the colon of the range */
        const char *x = st[sp - 1].s, *colon = strrchr(x, ':');
        if (!colon) buf_adds(&tb, x);
        else {
          buf_add(&tb, x, (size_t)(colon - x));
          buf_adds(&tb, t->flags & 1 ? ".:" : ":");
          if (t->flags & 2) buf_addc(&tb, '.');
          buf_adds(&tb, colon + 1);
        }
      }
      free(st[--sp].s);
      break;
    case T_CALL: {
      int k, argc = t->flags;
      if (sp < argc + 1) { ok = 0; break; }
      buf_adds(&tb, st[sp - argc - 1].s);
      free(st[sp - argc - 1].s);
      buf_addc(&tb, '(');
      for (k = sp - argc; k < sp; k++) { if (k > sp - argc) buf_addc(&tb, ','); buf_adds(&tb, st[k].s); free(st[k].s); }
      sp -= argc + 1;
      buf_addc(&tb, ')');
      break; }
    case T_NEG: case T_POS: case T_PCT:
      if (sp < 1) { ok = 0; break; }
      pr = prec(t->op);
      if (t->op != T_PCT) buf_addc(&tb, t->op == T_NEG ? '-' : '+');
      if (st[sp - 1].prec < pr) { buf_addc(&tb, '('); buf_adds(&tb, st[sp - 1].s); buf_addc(&tb, ')'); }
      else buf_adds(&tb, st[sp - 1].s);
      if (t->op == T_PCT) buf_addc(&tb, '%');
      free(st[--sp].s);
      break;
    case T_FUNC: {
      int k, argc = t->flags;
      if (sp < argc) { ok = 0; break; }
      if (formula_file_mode && t->a == 0xFFFF && t->r1) buf_adds(&tb, "_xlpm.");
      if (formula_file_mode && t->a != 0xFFFF && (!strcmp(func_name(t->a), "FILTER") || !strcmp(func_name(t->a), "SORT"))) buf_adds(&tb, "_xlfn._xlws.");
      buf_adds(&tb, t->a == 0xFFFF ? (t->s ? t->s : "?") : func_name(t->a));
      buf_addc(&tb, '(');
      for (k = sp - argc; k < sp; k++) { if (k > sp - argc) buf_addc(&tb, ','); buf_adds(&tb, st[k].s); free(st[k].s); }
      sp -= argc;
      buf_addc(&tb, ')');
      break; }
    default: {
      /* a binary operator; ^ binds left to right in Excel (2^3^2 = 64) */
      Item l, r;
      if (sp < 2) { ok = 0; break; }
      r = st[--sp]; l = st[--sp];
      pr = prec(t->op);
      if (l.prec < pr) { buf_addc(&tb, '('); buf_adds(&tb, l.s); buf_addc(&tb, ')'); } else buf_adds(&tb, l.s);
      buf_adds(&tb, op_text(t->op));
      if (r.prec <= pr && r.prec < 10) { buf_addc(&tb, '('); buf_adds(&tb, r.s); buf_addc(&tb, ')'); } else buf_adds(&tb, r.s);
      free(l.s); free(r.s);
      break; }
    }
    if (!ok) { buf_free(&tb); break; }
    st[sp].s = buf_take(&tb);
    if (!st[sp].s) st[sp].s = str_dup("");
    st[sp].prec = pr;
    sp++;
  }
  if (ok && sp == 1) buf_adds(out, st[0].s);
  for (i = 0; i < sp; i++) free(st[i].s);
  free(st);
}

/* ------------------------------------------------------------------ adjusting references */
static void make_referr(Tok *t) { t->op = T_ERR; t->a = E_REF; t->flags = 0; }
void formula_offset(Formula *f, i32 dr, i32 dc)
{
  int i;
  for (i = 0; i < f->n; i++) {
    Tok *t = &f->t[i];
    if (t->op != T_REF && t->op != T_AREA) continue;
    if (!(t->flags & RF_COLS)) {
      if (!(t->flags & RF_ABSR1)) t->r1 += dr;
      if (t->op == T_AREA && !(t->flags & RF_ABSR2)) t->r2 += dr;
    }
    if (!(t->flags & RF_ROWS)) {
      if (!(t->flags & RF_ABSC1)) t->c1 += dc;
      if (t->op == T_AREA && !(t->flags & RF_ABSC2)) t->c2 += dc;
    }
    if (t->op == T_REF) { t->r2 = t->r1; t->c2 = t->c1; }
    if (t->r1 < 0 || t->c1 < 0 || t->r2 >= MAXROW || t->c2 >= MAXCOL || t->r2 < 0 || t->c2 < 0 || t->r1 >= MAXROW || t->c1 >= MAXCOL)
      make_referr(t);
    else if (t->op == T_AREA) {
      if (t->r1 > t->r2) { i32 x = t->r1; t->r1 = t->r2; t->r2 = x; }
      if (t->c1 > t->c2) { i32 x = t->c1; t->c1 = t->c2; t->c2 = x; }
    }
  }
}
/* see book.h */
void formula_shift(Formula *f, int home, int target, int rows, i32 at, i32 n)
{
  int i;
  for (i = 0; i < f->n; i++) {
    Tok *t = &f->t[i];
    i32 *p1, *p2, lim;
    int sheet;
    if (t->op != T_REF && t->op != T_AREA) continue;
    sheet = t->a == SHEET_SELF ? home : t->a;
    if (sheet != target) continue;
    if (rows && (t->flags & RF_COLS)) continue;            /* whole columns don't move with rows */
    if (!rows && (t->flags & RF_ROWS)) continue;
    p1 = rows ? &t->r1 : &t->c1;
    p2 = rows ? &t->r2 : &t->c2;
    if (t->op == T_REF) p2 = p1;
    lim = rows ? MAXROW : MAXCOL;
    if (n > 0) {
      if (*p1 >= at) *p1 = *p1 + n >= lim ? lim - 1 : *p1 + n;
      if (p2 != p1 && *p2 >= at) *p2 = *p2 + n >= lim ? lim - 1 : *p2 + n;
    } else {
      i32 d = -n, end = at + d;           /* [at, end) deleted */
      if (p2 == p1) {
        if (*p1 >= end) *p1 -= d;
        else if (*p1 >= at) { make_referr(t); continue; }
      } else {
        if (*p1 >= at && *p2 < end) { make_referr(t); continue; }
        if (*p1 >= end) *p1 -= d; else if (*p1 >= at) *p1 = at;
        if (*p2 >= end) *p2 -= d; else if (*p2 >= at) *p2 = at - 1;
      }
    }
    if (t->op == T_REF) { t->r2 = t->r1; t->c2 = t->c1; }
  }
}
