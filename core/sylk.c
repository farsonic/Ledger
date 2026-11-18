/*
 * sylk.c - SYLK (.slk, "symbolic link") files in and out, the text format of Multiplan and older
 * spreadsheets that Excel still reads and writes (GridCraft #1481). One record per line, fields split
 * by ';' (";;" is a semicolon in a field):
 *
 *   ID;P...                  the file's start
 *   C;Y<row>;X<col>;K<value>;E<formula>   a cell (Y and X left out: the last row or column); K is a
 *                            number, "text", TRUE/FALSE or an error; E a formula in R1C1 notation;
 *                            S;R<row>;C<col> shares another cell's formula
 *   P;P<code>                a number format, numbered from 0 in order (P;F.. and P;E.. are fonts)
 *   F;P<n>;Y<row>;X<col>     a cell's number format; F;W<first> <last> <width> column widths (characters)
 *   E                        the end
 *
 * Other records (B dimensions, O options, window settings, fonts) are passed over. Formulas are
 * turned into A1 references the way Excel does on opening (relative stays relative, absolute absolute)
 * and back again on saving. Text is UTF-8 if the file is, else Latin-1 (Windows' ANSI), and saved as
 * Latin-1, as Excel writes it. One sheet, as Excel saves the active sheet.
 * Portable C99.
 */
#include "xlsx.h"
#include "calc.h"
#include "ops.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>

/* ------------------------------------------------------------------ R1C1 and A1 */
static int word_char(int c) { return isalnum((u8)c) || c == '_' || c == '.' || c == '\\' || c == '?' || c == '$' || (u8)c >= 0x80; }

/* the number after R or C at *p: digits (absolute), [offset] (relative) or nothing (the cell's own);
   0 if it isn't a reference part */
static int rc_part(const char **p, long *n, int *abs)
{
  const char *s = *p;
  if (*s == '[') {
    char *e;
    long v = strtol(s + 1, &e, 10);
    if (e == s + 1 || *e != ']' || v < -MAXROW || v > MAXROW) return 0;
    *n = v; *abs = 0; *p = e + 1;
    return 1;
  }
  if (isdigit((u8)*s)) {
    long v = 0;
    while (isdigit((u8)*s)) { if (v < 10000000) v = v * 10 + (*s - '0'); s++; }
    *n = v; *abs = 1; *p = s;
    return 1;
  }
  *n = 0; *abs = 0;
  return 1;
}
/* one side written: "$D", "D", "$3", "3"; 0 off the sheet */
static int rc_side(Buf *o, long n, int abs, long base, long max, int col)
{
  long v = abs ? n - 1 : base + n;
  char t[8];
  if (v < 0 || v >= max) return 0;
  if (abs) buf_addc(o, '$');
  if (col) { col_name((i32)v, t); buf_adds(o, t); }
  else buf_printf(o, "%ld", v + 1);
  return 1;
}
/* the A1 text of the R1C1 reference at *p (the start of a word), for the cell at row, col;
   0 (nothing used) if the word is a name or a function */
static int rc_ref(Buf *o, const char *start, const char **p, long row, long col)
{
  const char *s = *p, *q;
  long rn = 0, cn = 0;
  int ra = 0, ca = 0, hasr = 0, hasc = 0, inrange, ok = 1;
  Buf t;
  if (*s == 'R' || *s == 'r') { s++; if (!rc_part(&s, &rn, &ra)) return 0; hasr = 1; }
  if (*s == 'C' || *s == 'c') { s++; if (!rc_part(&s, &cn, &ca)) return 0; hasc = 1; }
  if (!hasr && !hasc) return 0;
  if (word_char(*s) || *s == '(' || *s == '[' || *s == '!') return 0;     /* ROUND(, RATE, R2D2 */
  for (q = *p; q > start && q[-1] == ' '; q--) ;
  inrange = (q > start && q[-1] == ':') || *s == ':';
  buf_init(&t);
  if (hasr && hasc) { ok = rc_side(&t, cn, ca, col, MAXCOL, 1) && rc_side(&t, rn, ra, row, MAXROW, 0); }
  else if (hasr) {
    ok = rc_side(&t, rn, ra, row, MAXROW, 0);
    if (ok && !inrange) { Buf u = t; buf_init(&t); buf_add(&t, u.data, u.len); buf_addc(&t, ':'); buf_add(&t, u.data, u.len); buf_free(&u); }
  } else {
    ok = rc_side(&t, cn, ca, col, MAXCOL, 1);
    if (ok && !inrange) { Buf u = t; buf_init(&t); buf_add(&t, u.data, u.len); buf_addc(&t, ':'); buf_add(&t, u.data, u.len); buf_free(&u); }
  }
  if (ok && t.data) buf_adds(o, t.data);
  else buf_adds(o, "#REF!");
  buf_free(&t);
  *p = s;
  return 1;
}
/* a quoted run ("text" or 'Sheet name') copied as it is, doubled quotes inside */
static const char *copy_quoted(Buf *o, const char *s)
{
  char q = *s;
  const char *e = s + 1;
  while (*e && !(*e == q && e[1] != q)) e += *e == q ? 2 : 1;
  if (*e) e++;
  buf_add(o, s, (size_t)(e - s));
  return e;
}
char *sylk_r1c1_to_a1(const char *f, long row, long col)
{
  Buf o;
  const char *s = f;
  buf_init(&o);
  while (*s) {
    if (*s == '"' || *s == '\'') { s = copy_quoted(&o, s); continue; }
    if ((s == f || !word_char(s[-1])) && strchr("RrCc", *s) && rc_ref(&o, f, &s, row, col)) continue;
    if (word_char(*s)) { const char *e = s; while (word_char(*e)) e++; buf_add(&o, s, (size_t)(e - s)); s = e; }
    else buf_addc(&o, *s++);
  }
  buf_addc(&o, 0);
  return o.oom ? (buf_free(&o), (char *)NULL) : buf_take(&o);
}

/* an A1 word: "$B$3" (cell), "$B"/"B" (column, when kind 1 allowed), "$3"/"3" (row, kind 2);
   which it is in *kind, the row/column and their $ */
static int a1_word(const char *s, size_t n, int *kind, long *r, long *c, int *ar, int *ac)
{
  size_t i = 0;
  int letters = 0, digits = 0;
  long cv = 0, rv = 0;
  *ar = *ac = 0;
  if (i < n && s[i] == '$') { *ac = 1; i++; }
  while (i < n && isalpha((u8)s[i]) && letters < 4) { cv = cv * 26 + (toupper((u8)s[i]) - 'A' + 1); letters++; i++; }
  if (letters && i < n && s[i] == '$') { *ar = 1; i++; }
  else if (!letters && *ac) { *ar = 1; *ac = 0; }       /* "$3": the $ belongs to the row */
  while (i < n && isdigit((u8)s[i]) && digits < 8) { rv = rv * 10 + (s[i] - '0'); digits++; i++; }
  if (i != n || letters > 3 || cv > MAXCOL || rv > MAXROW) return 0;
  if (letters && digits) { if (!rv) return 0; *kind = 0; }
  else if (letters) { if (*ar) return 0; *kind = 1; }
  else if (digits) { if (!rv) return 0; *kind = 2; }
  else return 0;
  *r = rv - 1; *c = cv - 1;
  return 1;
}
static void rc_write(Buf *o, char letter, long v, int abs, long base)
{
  buf_addc(o, letter);
  if (abs) buf_printf(o, "%ld", v + 1);
  else if (v != base) buf_printf(o, "[%ld]", v - base);
}
char *sylk_a1_to_r1c1(const char *f, long row, long col)
{
  Buf o;
  const char *s = f;
  buf_init(&o);
  while (*s) {
    if (*s == '"' || *s == '\'') { s = copy_quoted(&o, s); continue; }
    if (*s == '[') {                     /* structured references: as they are */
      int depth = 0;
      const char *e = s;
      do { if (*e == '[') depth++; else if (*e == ']') depth--; e++; } while (*e && depth);
      buf_add(&o, s, (size_t)(e - s)); s = e;
      continue;
    }
    if (word_char(*s)) {
      const char *e = s, *b;
      int kind, ar, ac;
      long r, c;
      while (word_char(*e)) e++;
      for (b = s; b > f && b[-1] == ' '; b--) ;
      if (*e != '(' && *e != '!' && *e != '[' && a1_word(s, (size_t)(e - s), &kind, &r, &c, &ar, &ac) &&
          (kind == 0 || *e == ':' || (b > f && b[-1] == ':'))) {
        if (kind != 2) { if (kind == 0) rc_write(&o, 'R', r, ar, row); rc_write(&o, 'C', c, ac, col); }
        else rc_write(&o, 'R', r, ar, row);
        s = e;
        continue;
      }
      buf_add(&o, s, (size_t)(e - s)); s = e;
      continue;
    }
    buf_addc(&o, *s++);
  }
  buf_addc(&o, 0);
  return o.oom ? (buf_free(&o), (char *)NULL) : buf_take(&o);
}

/* ------------------------------------------------------------------ reading */
int sylk_is(const char *data, size_t len)
{
  size_t i = len >= 3 && (u8)data[0] == 0xEF && (u8)data[1] == 0xBB && (u8)data[2] == 0xBF ? 3 : 0;
  return len >= i + 3 && data[i] == 'I' && data[i + 1] == 'D' && data[i + 2] == ';';
}
static int valid_utf8(const char *s, size_t n)
{
  size_t i = 0;
  while (i < n) {
    u8 c = (u8)s[i];
    int k = c < 0x80 ? 0 : (c & 0xE0) == 0xC0 ? 1 : (c & 0xF0) == 0xE0 ? 2 : (c & 0xF8) == 0xF0 ? 3 : -1;
    if (k < 0 || (k && c < 0xC2 && k == 1)) return 0;
    for (i++; k > 0; k--, i++) if (i >= n || ((u8)s[i] & 0xC0) != 0x80) return 0;
  }
  return 1;
}
/* the fields of one line (";;" a semicolon): written into f split by NULs, how many (at[] into f) */
static int split_fields(const char *s, size_t n, Buf *f, const char **at, int max)
{
  size_t i, start = 0, off[64];
  int k = 0;
  f->len = 0;
  if (max > 64) max = 64;
  for (i = 0; i <= n; i++) {
    if (i < n && s[i] == ';' && i + 1 < n && s[i + 1] == ';') { buf_addc(f, ';'); i++; continue; }
    if (i == n || s[i] == ';') { buf_addc(f, 0); if (k < max) off[k++] = start; start = f->len; continue; }
    buf_addc(f, s[i]);
  }
  if (f->oom) return 0;
  for (i = 0; i < (size_t)k; i++) at[i] = f->data + off[i];
  return k;
}
static long position(const char *a, long max)
{
  char *e;
  long v = strtol(a, &e, 10);
  return e == a || v < 1 || v > max ? -1 : v - 1;
}
typedef struct { long r, c; char *f; } SFormula;     /* formulas read, for S (shared) records */

Book *sylk_load(const char *data, size_t len, int *skipped)
{
  Book *b;
  char *text = NULL;
  size_t i, n;
  long row = 0, col = 0;
  char **fmts = NULL;
  int nfmts = 0, bad = 0, saved;
  SFormula *sf = NULL;
  long nsf = 0, capsf = 0;
  Buf fb, ub;
  if (skipped) *skipped = 0;
  if (!sylk_is(data, len)) return NULL;
  if (len >= 3 && (u8)data[0] == 0xEF) { data += 3; len -= 3; }
  if (!valid_utf8(data, len)) {
    /* an older export, in a single-byte code page: each byte its Latin-1 character */
    Buf t;
    buf_init(&t);
    for (i = 0; i < len; i++) { char u[4]; buf_add(&t, u, (size_t)utf8_put(u, (u8)data[i])); }
    if (t.oom) { buf_free(&t); return NULL; }
    n = t.len; text = buf_take(&t);
    if (!text) return NULL;
    data = text; len = n;
  }
  if (!(b = book_new())) { free(text); return NULL; }
  saved = b->manualcalc; b->manualcalc = 1;
  buf_init(&fb); buf_init(&ub);
  for (i = 0; i < len; ) {
    size_t e = i, ln;
    const char *at[64];
    int nf, k;
    while (e < len && data[e] != '\n' && data[e] != '\r') e++;
    ln = e - i;
    nf = split_fields(data + i, ln, &fb, at, 64);
    i = e;
    while (i < len && (data[i] == '\n' || data[i] == '\r')) i++;
    if (fb.oom || !nf) continue;
    if (!strcmp(at[0], "E")) break;
    if (!strcmp(at[0], "C")) {
      const char *val = NULL, *form = NULL;
      long shr = -1, shc = -1;
      int shared = 0, rbad = 0;
      for (k = 1; k < nf; k++) {
        const char *a = at[k] + 1;
        switch (at[k][0]) {
        case 'Y': if ((row = position(a, MAXROW)) < 0) rbad = 1; break;
        case 'X': if ((col = position(a, MAXCOL)) < 0) rbad = 1; break;
        case 'K': val = a; break;
        case 'E': form = a; break;
        case 'S': shared = 1; break;
        case 'R': if (shared) shr = position(a, MAXROW); break;
        case 'C': if (shared) shc = position(a, MAXCOL); break;
        }
      }
      if (rbad) { bad++; row = row < 0 ? 0 : row; col = col < 0 ? 0 : col; continue; }
      if (shared && shr >= 0 && shc >= 0) {
        long j;
        for (j = 0; j < nsf; j++) if (sf[j].r == shr && sf[j].c == shc) { form = sf[j].f; break; }
      }
      if (val) {
        Value v;
        int el;
        size_t vl = strlen(val);
        if (val[0] == '"') v = val_strn(val + 1, vl > 1 && val[vl - 1] == '"' ? vl - 2 : vl - 1);
        else if (str_ieq(val, "TRUE") || str_ieq(val, "FALSE")) v = val_bool(str_ieq(val, "TRUE"));
        else if (err_parse(val, &el) && (size_t)el == vl) v = val_err(err_parse(val, &el));
        else {
          char *x;
          double d = strtod(val, &x);
          v = x != val && !*x ? val_num(d) : val_str(val);
        }
        book_set_value(b, 0, (i32)row, (i32)col, &v);
        val_free(&v);
      }
      if (form) {
        char *a1 = sylk_r1c1_to_a1(form, row, col);
        int ok = 0;
        if (a1) {
          ub.len = 0; buf_addc(&ub, '='); buf_adds(&ub, a1);
          ok = !ub.oom && book_input(b, 0, (i32)row, (i32)col, ub.data, NULL);
          free(a1);
        }
        if (!ok) bad++;
        else if (!shared) {
          if (nsf == capsf) {
            SFormula *g = (SFormula *)realloc(sf, (size_t)(capsf = capsf ? capsf * 2 : 16) * sizeof *sf);
            if (!g) continue;
            sf = g;
          }
          if ((sf[nsf].f = str_dup(form))) { sf[nsf].r = row; sf[nsf].c = col; nsf++; }
        }
      }
    } else if (!strcmp(at[0], "F")) {
      long fmt = -1;
      for (k = 1; k < nf; k++) {
        const char *a = at[k] + 1;
        long v;
        switch (at[k][0]) {
        case 'Y': if ((v = position(a, MAXROW)) >= 0) row = v; break;
        case 'X': if ((v = position(a, MAXCOL)) >= 0) col = v; break;
        case 'P': if (isdigit((u8)*a)) fmt = strtol(a, NULL, 10); break;
        case 'W': {
          long c1, c2;
          double w;
          char *x;
          c1 = strtol(a, &x, 10); c2 = strtol(x, &x, 10); w = strtod(x, NULL);
          if (c1 >= 1 && c2 >= c1 && c2 <= MAXCOL && w >= 0 && w <= 255) {
            long c;
            for (c = c1 - 1; c < c2 && c < c1 + 1000; c++) book_set_col_width(b, 0, (i32)c, (u16)(w * 7 + 0.5));
          }
          break; }
        }
      }
      if (fmt >= 0 && fmt < nfmts && fmts[fmt][0] && !str_ieq(fmts[fmt], "General")) {
        Style st = b->styles[sheet_cell_style(b->sh[0], (i32)row, (i32)col)];
        st.numfmt = (u16)book_fmt(b, fmts[fmt]);
        book_set_style(b, 0, (i32)row, (i32)col, book_style(b, &st));
      }
    } else if (!strcmp(at[0], "P")) {
      if (nf > 1 && at[1][0] == 'P') {
        char **g = (char **)realloc(fmts, (size_t)(nfmts + 1) * sizeof *fmts);
        if (g) { fmts = g; if ((fmts[nfmts] = str_dup(at[1] + 1))) nfmts++; }
      }
    }
  }
  while (nfmts) free(fmts[--nfmts]);
  free(fmts);
  while (nsf) free(sf[--nsf].f);
  free(sf);
  buf_free(&fb); buf_free(&ub);
  free(text);
  b->manualcalc = saved;
  book_forget_undo(b);
  book_recalc(b);
  b->dirty = 0;
  if (skipped) *skipped = bad;
  return b;
}

/* ------------------------------------------------------------------ writing */
/* a field's text in Latin-1, ';' doubled, line breaks as spaces */
static void field_text(Buf *o, const char *s)
{
  size_t n = strlen(s), k;
  char *l = (char *)malloc(n + 4);
  if (!l) { o->oom = 1; return; }
  n = utf8_to_latin1(s, n, l, n + 4);
  for (k = 0; k < n; k++) {
    if (l[k] == ';') buf_adds(o, ";;");
    else if (l[k] == '\n' || l[k] == '\r') buf_addc(o, ' ');
    else buf_addc(o, l[k]);
  }
  free(l);
}
char *sylk_save(Book *b, int sheet)
{
  Sheet *s;
  Buf o;
  i32 r, c, lr, lc;
  int *map = NULL, nmap = 0, k;
  u16 *ids = NULL;
  if (sheet < 0 || sheet >= b->nsh) return NULL;
  s = b->sh[sheet];
  lr = sheet_last_row(s); lc = sheet_last_col(s);
  buf_init(&o);
  buf_adds(&o, "ID;PLedger;N;E\r\n");
  /* the number formats used, General first */
  if (!(map = (int *)calloc((size_t)b->nstyles + 1, sizeof *map)) || !(ids = (u16 *)calloc((size_t)b->nstyles + 2, sizeof *ids))) { free(map); free(ids); return NULL; }
  ids[nmap++] = 0;
  buf_adds(&o, "P;PGeneral\r\n");
  for (k = 0; k < b->nstyles; k++) {
    int j, id = b->styles[k].numfmt;
    for (j = 0; j < nmap && ids[j] != id; j++) ;
    if (j == nmap) { ids[nmap++] = (u16)id; buf_adds(&o, "P;P"); field_text(&o, book_fmt_code(b, id)); buf_adds(&o, "\r\n"); }
    map[k] = j;
  }
  for (c = 0; c < s->ncols; c++)
    if (s->cols[c].custom && s->cols[c].width) buf_printf(&o, "F;W%ld %ld %d\r\n", (long)c + 1, (long)c + 1, (s->cols[c].width + 3) / 7);
  buf_printf(&o, "B;Y%ld;X%ld;D0 0 %ld %ld\r\n", (long)(lr + 1), (long)(lc + 1), (long)(lr < 0 ? 0 : lr), (long)(lc < 0 ? 0 : lc));
  for (r = 0; r <= lr; r++) {
    Row *rw = r < s->nrows ? s->rows[r] : NULL;
    int i;
    if (!rw) continue;
    for (i = 0; i < rw->n; i++) {
      Cell *cl = &rw->c[i];
      const Value *v = &cl->v;
      c = cl->col;
      if (cl->spill) continue;
      if (cl->style && cl->style < b->nstyles && map[cl->style]) buf_printf(&o, "F;P%d;Y%ld;X%ld\r\n", map[cl->style], (long)r + 1, (long)c + 1);
      if (!cl->f && v->t == V_EMPTY) continue;
      buf_printf(&o, "C;Y%ld;X%ld", (long)r + 1, (long)c + 1);
      if (v->t == V_NUM) { char t[40]; snprintf(t, sizeof t, ";K%.17g", v->n); buf_adds(&o, t); }
      else if (v->t == V_STR) { buf_adds(&o, ";K\""); field_text(&o, v->s ? v->s : ""); buf_addc(&o, '"'); }
      else if (v->t == V_BOOL) buf_adds(&o, v->e ? ";KTRUE" : ";KFALSE");
      else if (v->t == V_ERR) { buf_adds(&o, ";K"); buf_adds(&o, err_name(v->e)); }
      if (cl->f) {
        char t[4096], *rc;
        book_edit_text(b, sheet, r, c, t, sizeof t);
        if (t[0] == '=' && (rc = sylk_a1_to_r1c1(t + 1, r, c))) { buf_adds(&o, ";E"); field_text(&o, rc); free(rc); }
      }
      buf_adds(&o, "\r\n");
    }
  }
  buf_adds(&o, "E\r\n");
  free(map); free(ids);
  if (o.oom) { buf_free(&o); return NULL; }
  return buf_take(&o);
}
