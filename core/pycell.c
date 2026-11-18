/*
 * pycell.c - Python cells kept, not run (GridCraft #2637). Excel's Python in Excel cells are
 * formulas =PY(code, type) whose result Microsoft's cloud works out; Ledger, like GridCraft, keeps
 * the code and the last result the file holds and never runs anything.
 *
 * In Ledger a Python cell is the formula PY("code",0) (type 0 an Excel value, 1 a Python object),
 * the code in string literals joined by &, as GridCraft shows it. Calculation leaves its result as
 * the file gave it (Formula.pycache, spilling as an array result does); a Python cell typed or
 * changed in Ledger shows #GETTING_DATA, as it has no result yet.
 *
 * In .xlsx ([MS-XLSX] 2.3.10) the cell's formula is _xlfn._xlws.PY(index,type,args...) and the code
 * is pythonScript number index of the workbook's Python part (xl/python.xml, through the
 * http://schemas.microsoft.com/office/2023/09/relationships/Python relationship); %Px% in the code
 * stands for argument x (from 2). Portable C99.
 */
#include "book.h"
#include "xml.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>

#define PY_MAX_SCRIPTS 100000
#define PY_MAX_CODE 8192

int py_is(const Formula *f)
{
  const Tok *t;
  if (!f || !f->n) return 0;
  t = &f->t[f->n - 1];
  return t->op == T_FUNC && t->a == 0xFFFF && t->s && str_ieq(t->s, "PY") && (t->flags == 1 || t->flags == 2);
}
Value py_result(const Formula *f)
{
  Value v;
  if (!f->pycache || !f->pycache->rows || !f->pycache->cols) return val_err(E_GETTING);
  if (f->pycache->rows == 1 && f->pycache->cols == 1) return val_copy(&f->pycache->v[0]);
  v = val_empty(); v.t = V_ARR; v.a = f->pycache;
  return val_copy(&v);
}
/* after loading: each Python cell's result taken from the cells it covered in the file */
void py_adopt(Book *b)
{
  int si;
  for (si = 0; si < b->nsh; si++) {
    Sheet *s = b->sh[si];
    i32 r;
    for (r = 0; r < s->nrows; r++) {
      int i;
      if (!s->rows[r]) continue;
      for (i = 0; i < s->rows[r]->n; i++) {
        Cell *cell = &s->rows[r]->c[i];
        Formula *f = cell->f;
        i32 nr, nc, rr, cc, c = cell->col;
        Arr *a;
        if (!py_is(f) || f->pycache) continue;
        nr = f->sr > 0 ? f->sr : 1; nc = f->sc > 0 ? f->sc : 1;
        if ((long)nr * nc > 1000000L) nr = nc = 1;
        if (!(a = arr_new((int)nr, (int)nc))) continue;
        for (rr = 0; rr < nr; rr++)
          for (cc = 0; cc < nc; cc++) {
            Cell *x = rr || cc ? sheet_cell(s, r + rr, c + cc) : cell;
            a->v[rr * nc + cc] = x && (!x->f || x == cell) ? val_copy(&x->v) : val_empty();
          }
        f->pycache = a;
        f->legacy = 0;
      }
    }
  }
}

/* ------------------------------------------------------------------ the file's Python part */
static char **scripts;
static int nscripts;
void py_scripts_free(void)
{
  while (nscripts) free(scripts[--nscripts]);
  free(scripts); scripts = NULL;
}
/* _xHHHH_ (the file's escape for characters XML can't hold) decoded, in place */
static void decode_x(char *s)
{
  char *o = s;
  while (*s) {
    if (s[0] == '_' && s[1] == 'x' && isxdigit((u8)s[2]) && isxdigit((u8)s[3]) && isxdigit((u8)s[4]) && isxdigit((u8)s[5]) && s[6] == '_') {
      char h[5];
      unsigned v;
      h[0] = s[2]; h[1] = s[3]; h[2] = s[4]; h[3] = s[5]; h[4] = 0;
      v = (unsigned)strtoul(h, NULL, 16);
      o += utf8_put(o, v);           /* (never longer than the 7 characters it replaces) */
      s += 7;
      continue;
    }
    *o++ = *s++;
  }
  *o = 0;
}
void py_scripts_read(const char *data, size_t len)
{
  Xml x;
  int in_code = 0;
  Buf code;
  py_scripts_free();
  buf_init(&code);
  xml_init(&x, data, len);
  while (xml_next(&x) != XT_EOF) {
    if (x.type == XT_START && !strcmp(x.name, "pythonScript")) { code.len = 0; buf_addc(&code, 0); code.len = 0; }
    else if (x.type == XT_START && !strcmp(x.name, "code")) in_code = 1;
    else if (x.type == XT_TEXT && in_code && code.len < PY_MAX_CODE * 8) buf_add(&code, x.text.data, x.text.len);
    else if (x.type == XT_END && !strcmp(x.name, "code")) in_code = 0;
    else if (x.type == XT_END && !strcmp(x.name, "pythonScript") && nscripts < PY_MAX_SCRIPTS) {
      char **ns = (char **)realloc(scripts, (size_t)(nscripts + 1) * sizeof *ns);
      if (!ns) break;
      scripts = ns;
      buf_addc(&code, 0);
      if ((scripts[nscripts] = str_dup(code.data ? code.data : ""))) { decode_x(scripts[nscripts]); nscripts++; }
      code.len = 0;
    }
  }
  xml_free(&x);
  buf_free(&code);
}
/* a formula's arguments at top level (text, a pointer and length each); how many */
static int split_args(const char *s, const char **at, size_t *len, int max, const char **end)
{
  int n = 0, depth = 0, q = 0;
  const char *p = s, *start = s;
  for (; *p; p++) {
    if (*p == '"' && !(q & 2)) q ^= 1;
    else if (*p == '\'' && !(q & 1)) q ^= 2;
    if (q) continue;
    if (*p == '(' || *p == '{' || *p == '[') depth++;
    else if ((*p == ')' || *p == '}' || *p == ']') && depth) depth--;
    else if (*p == ')' && !depth) break;
    if ((*p == ',' || *p == ';') && !depth) { if (n < max) { at[n] = start; len[n] = (size_t)(p - start); n++; } start = p + 1; }
  }
  if (*p != ')') return -1;
  if (n < max) { at[n] = start; len[n] = (size_t)(p - start); n++; }
  *end = p + 1;
  return n;
}
static void add_literal(Buf *o, const char *code)
{
  size_t k = 0;
  int chunk = 0;
  buf_addc(o, '"');
  for (; code[k]; k++) {
    /* 255 characters to a literal, as Excel writes them */
    if ((code[k] & 0xC0) != 0x80 && ++chunk > 255) { buf_adds(o, "\"&\""); chunk = 1; }
    if (code[k] == '"') buf_addc(o, '"');
    buf_addc(o, code[k]);
  }
  buf_addc(o, '"');
}
char *py_from_file(const char *t)
{
  static const char head[] = "_xlfn._xlws.PY(";
  const char *at[64], *end;
  size_t len[64];
  int n, k, type;
  long idx;
  char num[24];
  Buf o;
  const char *code, *p;
  while (*t == '=') t++;
  if (str_ncasecmp(t, head, sizeof head - 1)) return NULL;
  if ((n = split_args(t + sizeof head - 1, at, len, 64, &end)) < 2 || *end) return NULL;
  for (k = 0; k < 2; k++) { while (len[k] && *at[k] == ' ') { at[k]++; len[k]--; } }
  if (len[0] == 0 || len[0] >= sizeof num || len[1] != 1 || (at[1][0] != '0' && at[1][0] != '1')) return NULL;
  memcpy(num, at[0], len[0]); num[len[0]] = 0;
  for (k = 0; num[k]; k++) if (!isdigit((u8)num[k])) return NULL;      /* (a literal code: read as it is) */
  idx = atol(num); type = at[1][0] - '0';
  if (idx < 0 || idx >= nscripts) return NULL;                         /* no such script: the cached value only */
  code = scripts[idx];
  buf_init(&o);
  buf_adds(&o, "PY(");
  {
    /* %Px% put back as argument x's text */
    Buf c;
    buf_init(&c);
    for (p = code; *p; ) {
      if (p[0] == '%' && p[1] == 'P' && isdigit((u8)p[2])) {
        const char *q = p + 2;
        long x = 0;
        while (isdigit((u8)*q) && x < 1000000) x = x * 10 + (*q++ - '0');
        if (*q == '%' && x >= 2 && x < n) { buf_add(&c, at[x], len[x]); p = q + 1; continue; }
      }
      buf_addc(&c, *p++);
    }
    buf_addc(&c, 0);
    add_literal(&o, c.data ? c.data : "");
    buf_free(&c);
  }
  buf_printf(&o, ",%d)", type);
  buf_addc(&o, 0);
  return o.oom ? (buf_free(&o), (char *)NULL) : buf_take(&o);
}

/* ------------------------------------------------------------------ saving */
static char **out_code;
static int nout;
void py_save_begin(void) { nout = 0; out_code = NULL; }
/* the code of PY("..."&"...",t) in text (the file form of a Python cell), malloc'd; *type its type */
static char *literal_code(const char *t, int *type)
{
  Buf c;
  const char *p = t;
  while (*p == '=') p++;
  if (str_ncasecmp(p, "PY(", 3)) return NULL;
  p += 3;
  buf_init(&c);
  for (;;) {
    while (*p == ' ') p++;
    if (*p != '"') { buf_free(&c); return NULL; }
    for (p++; *p; p++) {
      if (*p == '"') { if (p[1] == '"') { buf_addc(&c, '"'); p++; continue; } break; }
      buf_addc(&c, *p);
    }
    if (*p != '"') { buf_free(&c); return NULL; }
    p++;
    while (*p == ' ') p++;
    if (*p == '&') { p++; continue; }
    break;
  }
  *type = 0;
  if (*p == ',') { p++; while (*p == ' ') p++; if (*p != '0' && *p != '1') { buf_free(&c); return NULL; } *type = *p++ - '0'; while (*p == ' ') p++; }
  if (*p != ')' || p[1]) { buf_free(&c); return NULL; }
  buf_addc(&c, 0);
  return buf_take(&c);
}
char *py_to_file(const char *text)
{
  int type;
  char *code = literal_code(text, &type), t[48], **nc;
  if (!code) return NULL;
  if (!(nc = (char **)realloc(out_code, (size_t)(nout + 1) * sizeof *nc))) { free(code); return NULL; }
  out_code = nc; out_code[nout] = code;
  snprintf(t, sizeof t, "_xlfn._xlws.PY(%d,%d)", nout, type);
  nout++;
  return str_dup(t);
}
int py_saved(void) { return nout; }
/* the Python part of the scripts py_to_file gave out, and the list emptied */
void py_save_part(Buf *o)
{
  int i;
  buf_adds(o, "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
              "<python xmlns=\"http://schemas.microsoft.com/office/spreadsheetml/2023/python\"><pythonScripts>");
  for (i = 0; i < nout; i++) {
    const char *p;
    buf_adds(o, "<pythonScript><code>");
    for (p = out_code[i]; *p; p++) {
      if (*p == '\r') buf_adds(o, "_x000D_");
      else if (*p == '_' && p[1] == 'x' && isxdigit((u8)p[2])) buf_adds(o, "_x005F_");     /* (a literal _x... kept) */
      else xml_escape(o, p, 1);
    }
    buf_adds(o, "</code></pythonScript>");
  }
  buf_adds(o, "</pythonScripts></python>");
}
void py_save_end(void)
{
  while (nout) free(out_code[--nout]);
  free(out_code); out_code = NULL;
}
/* any Python cell in the book (the part's relationship and content type are written first) */
int py_used(Book *b)
{
  int si;
  for (si = 0; si < b->nsh; si++) {
    Sheet *s = b->sh[si];
    i32 r;
    for (r = 0; r < s->nrows; r++) {
      int i;
      if (s->rows[r]) for (i = 0; i < s->rows[r]->n; i++) if (py_is(s->rows[r]->c[i].f)) return 1;
    }
  }
  return 0;
}
