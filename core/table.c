/*
 * table.c - tables and structured references (see table.h).
 *
 * Structured references are kept in formulas as T_TABLE tokens holding their text in a tidy form
 * ("Table1[Sales]", "[@Sales]", "Table1[[#Headers],[A]:[C]]"). They are resolved each time they
 * are evaluated, so a table growing, shrinking or moving needs nothing done to its formulas; a
 * table or column renamed has the texts rewritten (each_tref below).
 *
 * The style gallery's colours are Ledger's own, keyed by Excel's familiar style names
 * (TableStyleLight1..21, Medium1..28, Dark1..11) so files keep their style: the Office theme's
 * accent colours by number, lighter and darker tints of them, in three families of looks.
 */
#include "table.h"
#include "pivot.h"
#include "ops.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>
#include <math.h>

int table_file_mode;
i32 table_home_r, table_home_c;

/* ------------------------------------------------------------------ memory */
static void col_free(TableCol *c) { free(c->name); free(c->totlabel); free(c->totformula); free(c->calcformula); memset(c, 0, sizeof *c); }
static int col_copy(TableCol *d, const TableCol *s)
{
  memset(d, 0, sizeof *d);
  d->totfn = s->totfn;
  d->name = str_dup(s->name ? s->name : "");
  if (s->totlabel) d->totlabel = str_dup(s->totlabel);
  if (s->totformula) d->totformula = str_dup(s->totformula);
  if (s->calcformula) d->calcformula = str_dup(s->calcformula);
  return d->name != NULL;
}
static void table_free1(LTable *t)
{
  int i;
  for (i = 0; i < t->ncols; i++) col_free(&t->cols[i]);
  free(t->cols);
  t->cols = NULL; t->ncols = 0;
}
void tables_free(LTable *t, int n)
{
  int i;
  for (i = 0; i < n; i++) table_free1(&t[i]);
  free(t);
}
LTable *tables_copy(const LTable *t, int n)
{
  LTable *c;
  int i, k;
  if (!n || !(c = (LTable *)calloc((size_t)n, sizeof *c))) return NULL;
  for (i = 0; i < n; i++) {
    c[i] = t[i];
    c[i].cols = t[i].ncols ? (TableCol *)calloc((size_t)t[i].ncols, sizeof(TableCol)) : NULL;
    c[i].ncols = 0;
    for (k = 0; k < t[i].ncols && c[i].cols; k++) { col_copy(&c[i].cols[k], &t[i].cols[k]); c[i].ncols = k + 1; }
  }
  return c;
}
static void table_remove(Sheet *s, int i)
{
  table_free1(&s->tables[i]);
  memmove(&s->tables[i], &s->tables[i + 1], (size_t)(s->ntables - i - 1) * sizeof(LTable));
  if (!--s->ntables) { free(s->tables); s->tables = NULL; }
}

/* ------------------------------------------------------------------ finding */
LTable *table_at(Book *b, int sheet, i32 r, i32 c)
{
  Sheet *s;
  int i;
  if (sheet < 0 || sheet >= b->nsh) return NULL;
  s = b->sh[sheet];
  for (i = 0; i < s->ntables; i++) {
    LTable *t = &s->tables[i];
    if (r >= t->r.r1 && r <= t->r.r2 && c >= t->r.c1 && c <= t->r.c2) return t;
  }
  return NULL;
}
LTable *table_find(Book *b, const char *name, int *sheet)
{
  int s, i;
  for (s = 0; s < b->nsh; s++)
    for (i = 0; i < b->sh[s]->ntables; i++)
      if (str_ieq(b->sh[s]->tables[i].name, name)) { if (sheet) *sheet = s; return &b->sh[s]->tables[i]; }
  return NULL;
}
int table_index(Book *b, int sheet, const LTable *t)
{
  if (sheet < 0 || sheet >= b->nsh || !t) return -1;
  if (t < b->sh[sheet]->tables || t >= b->sh[sheet]->tables + b->sh[sheet]->ntables) return -1;
  return (int)(t - b->sh[sheet]->tables);
}
int table_data(const LTable *t, Rect *out)
{
  *out = t->r;
  out->r1 += t->header;
  out->r2 -= t->totals;
  if (out->r2 < out->r1) out->r2 = out->r1;
  return 1;
}
int table_header(const LTable *t, Rect *out) { *out = t->r; out->r2 = out->r1; return t->header; }
int table_totals(const LTable *t, Rect *out) { *out = t->r; out->r1 = out->r2; return t->totals; }
int table_col(const LTable *t, const char *name)
{
  int i;
  for (i = 0; i < t->ncols; i++) if (t->cols[i].name && str_ieq(t->cols[i].name, name)) return i;
  return -1;
}
int table_name_ok(Book *b, const char *name, const LTable *t)
{
  const char *p;
  u16 sh; Rect r; u8 fl;
  LTable *o;
  int i;
  if (!name || !name[0] || strlen(name) > 63) return 0;
  if (!isalpha((u8)name[0]) && name[0] != '_' && name[0] != '\\' && (u8)name[0] < 0x80) return 0;
  for (p = name; *p; p++) if (!isalnum((u8)*p) && *p != '_' && *p != '.' && *p != '\\' && (u8)*p < 0x80) return 0;
  if (str_ieq(name, "TRUE") || str_ieq(name, "FALSE") || (int)strlen(name) == parse_ref(b, name, &sh, &r, &fl)) return 0;
  /* R and C alone are R1C1 references to Excel */
  if (str_ieq(name, "R") || str_ieq(name, "C")) return 0;
  if ((o = table_find(b, name, NULL)) && o != t) return 0;
  for (i = 0; i < b->nnames; i++) if (str_ieq(b->names[i].name, name)) return 0;
  return 1;
}
void table_unique_name(Book *b, char *out)
{
  int k;
  for (k = 1; k < 100000; k++) { sprintf(out, "Table%d", k); if (table_name_ok(b, out, NULL)) return; }
}

/* ------------------------------------------------------------------ rows and columns in and out */
/* a column name not yet used in t ("Column3", "Sales2") */
static void unique_col(const LTable *t, const char *base, int skip, char *out, size_t cap)
{
  int k, i, clash;
  snprintf(out, cap, "%s", base);
  for (k = 2; ; k++) {
    clash = 0;
    for (i = 0; i < t->ncols; i++) if (i != skip && t->cols[i].name && str_ieq(t->cols[i].name, out)) clash = 1;
    if (!clash || k > 100000) return;
    snprintf(out, cap, "%s%d", base, k);
  }
}
static void put_text(Sheet *s, i32 r, i32 c, const char *text)
{
  Cell *cell = sheet_cell_make(s, r, c);
  if (!cell) return;
  val_free(&cell->v);
  formula_free(cell->f); cell->f = NULL;
  cell->v = val_str(text);
}
void tables_shift(Sheet *s, int rows, i32 at, i32 n)
{
  int i;
  for (i = 0; i < s->ntables; ) {
    LTable *t = &s->tables[i];
    i32 *a1 = rows ? &t->r.r1 : &t->r.c1, *a2 = rows ? &t->r.r2 : &t->r.c2;
    i32 o1 = *a1, o2 = *a2;
    if (n > 0) {
      if (at <= *a1) { *a1 += n; *a2 += n; }
      else if (at <= *a2) *a2 += n;
      if (*a2 >= (rows ? MAXROW : MAXCOL)) *a2 = (rows ? MAXROW : MAXCOL) - 1;
      if (*a1 > *a2) { table_remove(s, i); continue; }
      if (!rows && at > o1 && at <= o2) {
        /* columns put in inside the table: new columns, named */
        TableCol *nc = (TableCol *)realloc(t->cols, (size_t)(t->ncols + n) * sizeof(TableCol));
        i32 k, at0 = at - o1;
        if (nc) {
          t->cols = nc;
          memmove(&t->cols[at0 + n], &t->cols[at0], (size_t)(t->ncols - at0) * sizeof(TableCol));
          for (k = 0; k < n; k++) memset(&t->cols[at0 + k], 0, sizeof(TableCol));
          t->ncols += n;
          for (k = 0; k < n; k++) {
            char nm[80], base[32];
            sprintf(base, "Column%ld", (long)(at0 + k + 1));
            unique_col(t, base, (int)(at0 + k), nm, sizeof nm);
            t->cols[at0 + k].name = str_dup(nm);
            if (t->header) put_text(s, t->r.r1, at + k, nm);
          }
        }
      }
    } else {
      i32 d = -n, end = at + d;
      if (*a1 >= at && *a2 < end) { table_remove(s, i); continue; }
      if (rows) {
        if (t->header && t->r.r1 >= at && t->r.r1 < end) t->header = 0;
        if (t->totals && t->r.r2 >= at && t->r.r2 < end) t->totals = 0;
      } else {
        /* the deleted columns go from the list */
        i32 c0 = at > o1 ? at - o1 : 0, c1 = (end - 1 < o2 ? end - 1 : o2) - o1, k;
        if (c1 >= c0) {
          for (k = c0; k <= c1; k++) col_free(&t->cols[k]);
          memmove(&t->cols[c0], &t->cols[c1 + 1], (size_t)(t->ncols - c1 - 1) * sizeof(TableCol));
          t->ncols -= c1 - c0 + 1;
        }
      }
      if (*a1 >= end) *a1 -= d; else if (*a1 >= at) *a1 = at;
      if (*a2 >= end) *a2 -= d; else if (*a2 >= at) *a2 = at - 1;
      if (*a2 < *a1) { table_remove(s, i); continue; }
      if (rows && t->r.r2 - t->r.r1 + 1 < t->header + t->totals + 1) t->r.r2 = t->r.r1 + t->header + t->totals;
    }
    i++;
  }
}

/* ------------------------------------------------------------------ structured references: parsing */
enum { TI_ALL = 1, TI_DATA = 2, TI_HEADERS = 4, TI_TOTALS = 8, TI_THISROW = 16 };
typedef struct { char table[64]; int items; int hasname; char col1[256], col2[256]; } TSpec;

int table_ref_scan(const char *p)
{
  int depth = 0, n = 0;
  if (*p != '[') return 0;
  for (;;) {
    char ch = p[n];
    if (!ch) return 0;
    if (ch == '\'' && p[n + 1]) { n += 2; continue; }
    if (ch == '[') depth++;
    else if (ch == ']') { if (--depth == 0) return n + 1; }
    n++;
  }
}
static const char *const item_names[] = { "#All", "#Data", "#Headers", "#Totals", "#This Row" };
static int item_bit(const char *s, size_t n)
{
  int i;
  while (n && s[n - 1] == ' ') n--;
  for (i = 0; i < 5; i++) if (strlen(item_names[i]) == n && !strncasecmp(item_names[i], s, n)) return 1 << i;
  return 0;
}
/* a column name from s up to (not including) end, '-escapes removed and outer spaces trimmed */
static void unescape(const char *s, const char *end, char *out, size_t cap)
{
  size_t o = 0;
  while (s < end && *s == ' ') s++;
  while (end > s && end[-1] == ' ' && !(end - 1 > s && end[-2] == '\'')) end--;
  while (s < end && o + 1 < cap) {
    if (*s == '\'' && s + 1 < end) s++;
    out[o++] = *s++;
  }
  out[o] = 0;
}
/* the closing ] of a [ at p (escapes skipped), or NULL */
static const char *close_of(const char *p, const char *end)
{
  p++;
  while (p < end) {
    if (*p == '\'' && p + 1 < end) { p += 2; continue; }
    if (*p == ']') return p;
    if (*p == '[') return NULL;
    p++;
  }
  return NULL;
}
static int spec_parse(const char *text, TSpec *sp)
{
  const char *br = strchr(text, '['), *q, *end;
  size_t nl;
  int len;
  memset(sp, 0, sizeof *sp);
  if (!br) return 0;
  nl = (size_t)(br - text);
  if (nl >= sizeof sp->table) return 0;
  memcpy(sp->table, text, nl); sp->table[nl] = 0;
  sp->hasname = nl > 0;
  len = table_ref_scan(br);
  if (!len || br[len]) return 0;
  q = br + 1; end = br + len - 1;              /* the inside */
  while (q < end && *q == ' ') q++;
  if (q == end) return 1;                       /* Table1[]: the data */
  if (*q == '@') {
    sp->items = TI_THISROW;
    q++;
    while (q < end && *q == ' ') q++;
    if (q == end) return 1;
    if (*q != '[') { unescape(q, end, sp->col1, sizeof sp->col1); return 1; }
    /* @[A] or @[A]:[B] */
    {
      const char *c1 = close_of(q, end);
      if (!c1) return 0;
      unescape(q + 1, c1, sp->col1, sizeof sp->col1);
      q = c1 + 1;
      while (q < end && *q == ' ') q++;
      if (q < end && *q == ':') {
        const char *c2;
        q++;
        while (q < end && *q == ' ') q++;
        if (q >= end || *q != '[' || !(c2 = close_of(q, end))) return 0;
        unescape(q + 1, c2, sp->col2, sizeof sp->col2);
        q = c2 + 1;
      }
      while (q < end && *q == ' ') q++;
      return q == end;
    }
  }
  if (*q == '#') {
    int bit = item_bit(q, (size_t)(end - q));
    if (!bit) return 0;
    sp->items = bit;
    return 1;
  }
  if (*q != '[') { unescape(q, end, sp->col1, sizeof sp->col1); return 1; }
  /* a list: [#Item],[#Item],[Col]:[Col] */
  {
    int range = 0;
    while (q < end) {
      const char *c = close_of(q, end);
      if (!c) return 0;
      if (q[1] == '#') {
        int bit = item_bit(q + 1, (size_t)(c - q - 1));
        if (!bit) return 0;
        sp->items |= bit;
      } else if (range) { unescape(q + 1, c, sp->col2, sizeof sp->col2); range = 0; }
      else if (!sp->col1[0]) unescape(q + 1, c, sp->col1, sizeof sp->col1);
      else return 0;
      q = c + 1;
      while (q < end && *q == ' ') q++;
      if (q == end) break;
      if (*q == ':') { if (!sp->col1[0] || sp->col2[0]) return 0; range = 1; }
      else if (*q != ',') return 0;
      q++;
      while (q < end && *q == ' ') q++;
      if (q >= end || *q != '[') return 0;
    }
    if (range) return 0;
  }
  return 1;
}
/* a column name needs the double brackets (anything but letters, digits and _) */
static int col_plain(const char *s)
{
  if (!*s) return 0;
  for (; *s; s++) if (!isalnum((u8)*s) && *s != '_' && (u8)*s < 0x80) return 0;
  return 1;
}
static void col_out(Buf *o, const char *s)
{
  for (; *s; s++) {
    if (*s == '[' || *s == ']' || *s == '#' || *s == '\'') buf_addc(o, '\'');
    buf_addc(o, *s);
  }
}
/* the spec as text: file form spells @ as [#This Row] and always names the table */
static void spec_text(const TSpec *sp, int file, Buf *o)
{
  int items = sp->items, i, first = 1;
  int cols = sp->col1[0] != 0, range = sp->col2[0] != 0;
  buf_adds(o, sp->table);
  if (items == TI_THISROW && !file) {
    buf_adds(o, "[@");
    if (cols) {
      if (!range && col_plain(sp->col1)) col_out(o, sp->col1);
      else {
        buf_addc(o, '['); col_out(o, sp->col1); buf_addc(o, ']');
        if (range) { buf_adds(o, ":["); col_out(o, sp->col2); buf_addc(o, ']'); }
      }
    }
    buf_addc(o, ']');
    return;
  }
  if (!items && !cols) { buf_adds(o, "[]"); return; }
  if (!items && !range) {
    if (col_plain(sp->col1)) { buf_addc(o, '['); col_out(o, sp->col1); buf_addc(o, ']'); }
    else { buf_adds(o, "[["); col_out(o, sp->col1); buf_adds(o, "]]"); }
    return;
  }
  if (items && !cols && (items & (items - 1)) == 0) {
    /* one item alone */
    for (i = 0; i < 5; i++) if (items == 1 << i) { buf_addc(o, '['); buf_adds(o, item_names[i]); buf_addc(o, ']'); }
    return;
  }
  buf_addc(o, '[');
  for (i = 0; i < 5; i++)
    if (items & (1 << i)) { if (!first) buf_addc(o, ','); buf_addc(o, '['); buf_adds(o, item_names[i]); buf_addc(o, ']'); first = 0; }
  if (cols) {
    if (!first) buf_addc(o, ',');
    buf_addc(o, '['); col_out(o, sp->col1); buf_addc(o, ']');
    if (range) { buf_adds(o, ":["); col_out(o, sp->col2); buf_addc(o, ']'); }
  }
  buf_addc(o, ']');
}
char *table_ref_tidy(const char *text)
{
  TSpec sp;
  Buf o;
  char *r;
  if (!spec_parse(text, &sp)) return NULL;
  buf_init(&o);
  spec_text(&sp, 0, &o);
  r = buf_take(&o);
  return r;
}
/* the table a spec means, seen from cell r, c of sheet (its sheet in *tsheet) */
static LTable *spec_table(Book *b, const TSpec *sp, int sheet, i32 r, i32 c, int *tsheet)
{
  if (sp->hasname) return table_find(b, sp->table, tsheet);
  *tsheet = sheet;
  return r >= 0 ? table_at(b, sheet, r, c) : NULL;
}
/* the rectangle a spec means in table t; an E_ error, or 0 */
static int spec_rect(const LTable *t, const TSpec *sp, i32 row, Rect *out)
{
  Rect d;
  int items = sp->items;
  table_data(t, &d);
  out->c1 = t->r.c1; out->c2 = t->r.c2;
  if (sp->col1[0]) {
    int k1 = table_col(t, sp->col1), k2 = sp->col2[0] ? table_col(t, sp->col2) : k1;
    if (k1 < 0 || k2 < 0) return E_REF;
    if (k1 > k2) { int x = k1; k1 = k2; k2 = x; }
    out->c1 = t->r.c1 + k1; out->c2 = t->r.c1 + k2;
  }
  if (!items) items = TI_DATA;
  if (items & TI_THISROW) {
    if (row < d.r1 || row > d.r2 || (t->totals && row == t->r.r2)) return E_VALUE;
    out->r1 = out->r2 = row;
    return 0;
  }
  if (items & TI_ALL) { out->r1 = t->r.r1; out->r2 = t->r.r2; return 0; }
  if (items == TI_HEADERS) { if (!t->header) return E_REF; out->r1 = out->r2 = t->r.r1; return 0; }
  if (items == TI_TOTALS) { if (!t->totals) return E_REF; out->r1 = out->r2 = t->r.r2; return 0; }
  out->r1 = (items & TI_HEADERS) && t->header ? t->r.r1 : d.r1;
  out->r2 = (items & TI_TOTALS) && t->totals ? t->r.r2 : d.r2;
  if (!(items & TI_DATA)) {
    /* headers and totals without the data: not a rectangle */
    return E_REF;
  }
  return 0;
}
Value table_ref_value(Book *b, int sheet, i32 r, i32 c, const char *text)
{
  TSpec sp;
  LTable *t;
  int ts = sheet, e;
  Rect rc;
  Value v;
  if (!spec_parse(text, &sp)) return val_err(E_REF);
  if (!(t = spec_table(b, &sp, sheet, r, c, &ts))) return val_err(E_REF);
  if ((e = spec_rect(t, &sp, r, &rc))) return val_err(e);
  v = val_empty();
  v.t = V_REF; v.sheet = (u16)ts;
  v.r1 = rc.r1; v.c1 = rc.c1; v.r2 = rc.r2; v.c2 = rc.c2;
  return v;
}
int table_name_value(Book *b, const char *name, Value *out)
{
  int ts;
  LTable *t = table_find(b, name, &ts);
  Rect d;
  if (!t) return 0;
  table_data(t, &d);
  *out = val_empty();
  out->t = V_REF; out->sheet = (u16)ts;
  out->r1 = d.r1; out->c1 = d.c1; out->r2 = d.r2; out->c2 = d.c2;
  return 1;
}
void table_ref_text(Book *b, int sheet, const char *text, Buf *out)
{
  TSpec sp;
  if (!table_file_mode || !spec_parse(text, &sp)) { buf_adds(out, text); return; }
  if (!sp.hasname) {
    LTable *t = table_at(b, sheet, table_home_r, table_home_c);
    if (t) { strcpy(sp.table, t->name); sp.hasname = 1; }
  }
  spec_text(&sp, 1, out);
}

/* ------------------------------------------------------------------ structured references: rewriting */
typedef void (*TrefFn)(Book *b, int sheet, i32 r, i32 c, Tok *t, void *arg);
static void each_tok(Book *b, int sheet, i32 r, i32 c, Formula *f, TrefFn fn, void *arg)
{
  int i;
  if (!f) return;
  for (i = 0; i < f->n; i++) if (f->t[i].op == T_TABLE && f->t[i].s) fn(b, sheet, r, c, &f->t[i], arg);
}
/* every structured reference in the book: cells (with where they are), names and conditional
   formats (r -1) */
static void each_tref(Book *b, TrefFn fn, void *arg)
{
  int s, i;
  i32 r;
  for (s = 0; s < b->nsh; s++) {
    Sheet *sh = b->sh[s];
    for (r = 0; r < sh->nrows; r++)
      if (sh->rows[r]) for (i = 0; i < sh->rows[r]->n; i++) if (sh->rows[r]->c[i].f) each_tok(b, s, r, sh->rows[r]->c[i].col, sh->rows[r]->c[i].f, fn, arg);
    for (i = 0; i < sh->ncfs; i++) { each_tok(b, s, -1, -1, sh->cfs[i].f1, fn, arg); each_tok(b, s, -1, -1, sh->cfs[i].f2, fn, arg); }
  }
  for (i = 0; i < b->nnames; i++) each_tok(b, b->names[i].sheet == SHEET_SELF ? 0 : b->names[i].sheet, -1, -1, b->names[i].f, fn, arg);
}
static void set_spec(Tok *t, const TSpec *sp)
{
  Buf o;
  char *s;
  buf_init(&o);
  spec_text(sp, 0, &o);
  if ((s = buf_take(&o))) { free(t->s); t->s = s; }
}
typedef struct { const char *from, *to; int sheet; LTable *t; } Ren;
static void ren_table(Book *b, int sheet, i32 r, i32 c, Tok *t, void *arg)
{
  Ren *rn = (Ren *)arg;
  TSpec sp;
  (void)b; (void)r; (void)c;
  if (rn->sheet >= 0 && sheet != rn->sheet) return;
  if (!spec_parse(t->s, &sp) || !sp.hasname || !str_ieq(sp.table, rn->from)) return;
  snprintf(sp.table, sizeof sp.table, "%s", rn->to);
  set_spec(t, &sp);
}
static void ren_col(Book *b, int sheet, i32 r, i32 c, Tok *t, void *arg)
{
  Ren *rn = (Ren *)arg;
  TSpec sp;
  int ts;
  int changed = 0;
  if (!spec_parse(t->s, &sp) || spec_table(b, &sp, sheet, r, c, &ts) != rn->t) return;
  if (str_ieq(sp.col1, rn->from)) { snprintf(sp.col1, sizeof sp.col1, "%s", rn->to); changed = 1; }
  if (str_ieq(sp.col2, rn->from)) { snprintf(sp.col2, sizeof sp.col2, "%s", rn->to); changed = 1; }
  if (changed) set_spec(t, &sp);
}
/* the formula texts tables keep (calculated columns, custom totals) through fn too: each parsed,
   seen from its table's first data row, and printed back */
static void each_text(Book *b, TrefFn fn, void *arg)
{
  int s, i, k, j, pos;
  for (s = 0; s < b->nsh; s++)
    for (i = 0; i < b->sh[s]->ntables; i++) {
      LTable *t = &b->sh[s]->tables[i];
      for (k = 0; k < t->ncols; k++)
        for (j = 0; j < 2; j++) {
          char **txt = j ? &t->cols[k].totformula : &t->cols[k].calcformula;
          Formula *f;
          Buf o;
          if (!*txt || !(f = formula_parse(b, s, *txt, &pos))) continue;
          each_tok(b, s, j ? t->r.r2 : t->r.r1 + t->header, t->r.c1 + k, f, fn, arg);
          buf_init(&o);
          formula_text(b, s, f, &o);
          if (o.data) { free(*txt); *txt = buf_take(&o); }
          buf_free(&o);
          formula_free(f);
        }
    }
}
static void rename_column(Book *b, LTable *t, int k, const char *to)
{
  Ren rn;
  char *old = t->cols[k].name;
  if (!old || !strcmp(old, to)) return;
  rn.from = old; rn.to = to; rn.sheet = -1; rn.t = t;
  each_tref(b, ren_col, &rn);
  each_text(b, ren_col, &rn);
  t->cols[k].name = str_dup(to);
  free(old);
}
/* [@Col] in a cell of the table itself needn't name it (files always do) */
static void strip_own(Book *b, int sheet, i32 r, i32 c, Tok *t, void *arg)
{
  TSpec sp;
  LTable *own;
  (void)arg;
  if (r < 0 || !spec_parse(t->s, &sp) || !sp.hasname || sp.items != TI_THISROW) return;
  if (!(own = table_at(b, sheet, r, c)) || !str_ieq(own->name, sp.table)) return;
  sp.table[0] = 0; sp.hasname = 0;
  set_spec(t, &sp);
}
static void each_text(Book *b, TrefFn fn, void *arg);
void tables_tidy_loaded(Book *b) { each_tref(b, strip_own, NULL); each_text(b, strip_own, NULL); }
/* references to the table written as ordinary references (Convert to Range) */
static void unref(Book *b, int sheet, i32 r, i32 c, Tok *t, void *arg)
{
  TSpec sp;
  int ts;
  Value v;
  if (!spec_parse(t->s, &sp) || spec_table(b, &sp, sheet, r, c, &ts) != (LTable *)arg) return;
  v = table_ref_value(b, sheet, r, c, t->s);
  free(t->s); t->s = NULL;
  if (v.t != V_REF) { t->op = T_ERR; t->a = v.t == V_ERR ? v.e : E_REF; t->flags = 0; return; }
  t->op = (v.r1 == v.r2 && v.c1 == v.c2) ? T_REF : T_AREA;
  t->a = (u16)(ts == sheet ? SHEET_SELF : ts);
  t->r1 = v.r1; t->c1 = v.c1; t->r2 = v.r2; t->c2 = v.c2;
  t->flags = RF_ABSC1 | RF_ABSC2 | ((sp.items & TI_THISROW) ? 0 : RF_ABSR1 | RF_ABSR2);
}

/* ------------------------------------------------------------------ editing helpers */
/* recalculation held off during a bigger change (as ops.c does) */
static int hold(Book *b) { int m = b->manualcalc; b->manualcalc = 1; return m; }
static void release(Book *b, int m) { b->manualcalc = m; if (!m) book_recalc(b); }
static int row_empty(Sheet *s, i32 r, i32 c1, i32 c2)
{
  i32 c;
  for (c = c1; c <= c2; c++) { Cell *cell = sheet_cell(s, r, c); if (cell && (cell->f || cell->v.t != V_EMPTY)) return 0; }
  return 1;
}
/* the text a header cell shows (UTF-8) */
static void header_text(Book *b, int sheet, i32 r, i32 c, char *out, size_t cap)
{
  u32 col;
  Cell *cell = sheet_cell(b->sh[sheet], r, c);
  out[0] = 0;
  if (!cell) return;
  if (cell->v.t == V_STR && !cell->f) { snprintf(out, cap, "%s", cell->v.s); return; }
  book_shown_text(b, sheet, r, c, out, cap, &col);
}
static int subtotal_code(int fn)
{
  switch (fn) {
  case TF_AVERAGE: return 101; case TF_COUNTNUMS: return 102; case TF_COUNT: return 103; case TF_MAX: return 104;
  case TF_MIN: return 105; case TF_STDDEV: return 107; case TF_SUM: return 109; case TF_VAR: return 110;
  }
  return 0;
}
/* a structured reference to column k of t as formula text ("Table1[Sales]") */
static void col_ref_text(const LTable *t, int k, Buf *o)
{
  TSpec sp;
  memset(&sp, 0, sizeof sp);
  snprintf(sp.table, sizeof sp.table, "%s", t->name);
  sp.hasname = 1;
  snprintf(sp.col1, sizeof sp.col1, "%s", t->cols[k].name ? t->cols[k].name : "");
  spec_text(&sp, 0, o);
}
/* the totals row cell of column k written from its function */
static void write_total(Book *b, int sheet, LTable *t, int k)
{
  Sheet *s = b->sh[sheet];
  TableCol *tc = &t->cols[k];
  i32 r = t->r.r2, c = t->r.c1 + k;
  Cell *cell;
  if (!t->totals) return;
  if (tc->totfn == TF_NONE) {
    if (tc->totlabel && tc->totlabel[0]) put_text(s, r, c, tc->totlabel);
    else if ((cell = sheet_cell(s, r, c))) { val_free(&cell->v); formula_free(cell->f); cell->f = NULL; cell->v = val_empty(); }
    return;
  }
  {
    Buf fb;
    Formula *f;
    int pos;
    buf_init(&fb);
    if (tc->totfn == TF_CUSTOM && tc->totformula) buf_adds(&fb, tc->totformula);
    else { buf_printf(&fb, "SUBTOTAL(%d,", subtotal_code(tc->totfn)); col_ref_text(t, k, &fb); buf_addc(&fb, ')'); }
    f = fb.data ? formula_parse(b, sheet, fb.data, &pos) : NULL;
    buf_free(&fb);
    if (f && (cell = sheet_cell_make(s, r, c))) { val_free(&cell->v); formula_free(cell->f); cell->f = f; cell->calcgen = 0; }
    else formula_free(f);
  }
}
/* the header row's cells made the columns' names */
static void write_headers(Sheet *s, LTable *t)
{
  int k;
  if (!t->header) return;
  for (k = 0; k < t->ncols; k++) put_text(s, t->r.r1, t->r.c1 + k, t->cols[k].name ? t->cols[k].name : "");
}
/* the columns' names read from the header row (unique, empty ones ColumnN), written back */
static int read_headers(Book *b, int sheet, LTable *t, i32 hr)
{
  int k, n = t->r.c2 - t->r.c1 + 1;
  TableCol *cols = (TableCol *)calloc((size_t)n, sizeof(TableCol));
  if (!cols) return 0;
  for (k = 0; k < t->ncols; k++) col_free(&t->cols[k]);
  free(t->cols);
  t->cols = cols; t->ncols = n;
  for (k = 0; k < n; k++) {
    char h[256], nm[280];
    if (hr >= 0) header_text(b, sheet, hr, t->r.c1 + k, h, sizeof h);
    else h[0] = 0;
    if (!h[0]) sprintf(h, "Column%d", k + 1);
    t->ncols = k;                         /* (only the ones named so far count for clashes) */
    unique_col(t, h, -1, nm, sizeof nm);
    t->ncols = n;
    cols[k].name = str_dup(nm);
  }
  return 1;
}
static int overlaps(const Rect *a, const Rect *b) { return a->r1 <= b->r2 && b->r1 <= a->r2 && a->c1 <= b->c2 && b->c1 <= a->c2; }
static int area_free(Book *b, int sheet, Rect a, const LTable *except)
{
  Sheet *s = b->sh[sheet];
  int i;
  for (i = 0; i < s->ntables; i++) if (&s->tables[i] != except && overlaps(&a, &s->tables[i].r)) return 0;
  for (i = 0; i < s->nmerges; i++) if (overlaps(&a, &s->merges[i])) return 0;
  for (i = 0; i < s->npivots; i++) if (s->pivots[i].out.r1 >= 0 && overlaps(&a, &s->pivots[i].out)) return 0;
  return 1;
}
/* the sheet's AutoFilter, if it is on the table (its rows above the totals row) */
static void table_filter_rect(const LTable *t, Rect *f) { *f = t->r; f->r2 -= t->totals; }
static int filter_is_table(Sheet *s, const LTable *t)
{
  Rect f;
  table_filter_rect(t, &f);
  return s->filter.r1 >= 0 && t->header && s->filter.r1 == f.r1 && s->filter.c1 == f.c1 && s->filter.c2 == f.c2;
}

/* ------------------------------------------------------------------ editing */
int book_table_add(Book *b, int sheet, Rect a, int header, const char *style)
{
  Sheet *s;
  LTable *nt, t;
  int m, idx;
  if (sheet < 0 || sheet >= b->nsh) return -1;
  s = b->sh[sheet];
  if (a.r1 < 0 || a.c1 < 0 || a.r2 >= MAXROW - 2 || a.c2 >= MAXCOL || a.r2 < a.r1 || a.c2 < a.c1 || a.c2 - a.c1 >= 1000) return -1;
  if (header && a.r2 == a.r1) a.r2++;
  if (!area_free(b, sheet, a, NULL)) return -1;
  memset(&t, 0, sizeof t);
  book_begin(b);
  book_note_sheet(b, sheet);
  m = hold(b);
  if (!header) {
    Rect row = a;
    row.r2 = row.r1;
    book_insert_cells(b, sheet, row, 1);
    a.r2++;
  }
  t.r = a;
  t.header = 1; t.bandrows = 1; t.filterbtn = 1;
  snprintf(t.style, sizeof t.style, "%s", style && style[0] ? style : "TableStyleMedium2");
  table_unique_name(b, t.name);
  read_headers(b, sheet, &t, header ? a.r1 : -1);
  write_headers(s, &t);
  /* the sheet's AutoFilter goes if the table covers it (the table has its own buttons) */
  if (s->filter.r1 >= 0 && overlaps(&s->filter, &a)) {
    i32 r;
    for (r = s->filter.r1; r <= s->filter.r2 && r < s->nrows; r++) if (s->rows[r]) s->rows[r]->filtered = 0;
    s->filter.r1 = -1;
    while (s->nfcols) { int k; for (k = 0; k < s->fcols[s->nfcols - 1].n; k++) free(s->fcols[s->nfcols - 1].vals[k]); free(s->fcols[s->nfcols - 1].vals); s->nfcols--; }
  }
  nt = (LTable *)realloc(s->tables, (size_t)(s->ntables + 1) * sizeof(LTable));
  if (!nt) { table_free1(&t); release(b, m); book_end(b); return -1; }
  s->tables = nt;
  idx = s->ntables++;
  s->tables[idx] = t;
  b->dirty = 1;
  release(b, m);
  book_end(b);
  return idx;
}
static LTable *get(Book *b, int sheet, int ti)
{
  if (sheet < 0 || sheet >= b->nsh || ti < 0 || ti >= b->sh[sheet]->ntables) return NULL;
  return &b->sh[sheet]->tables[ti];
}
int book_table_rename(Book *b, int sheet, int ti, const char *name)
{
  LTable *t = get(b, sheet, ti);
  Ren rn;
  char old[64];
  if (!t || !table_name_ok(b, name, t)) return 0;
  if (!strcmp(t->name, name)) return 1;
  book_begin(b);
  book_note_book(b);
  t = get(b, sheet, ti);
  strcpy(old, t->name);
  rn.from = old; rn.to = name; rn.sheet = -1; rn.t = t;
  each_tref(b, ren_table, &rn);
  each_text(b, ren_table, &rn);
  snprintf(t->name, sizeof t->name, "%s", name);
  b->dirty = 1;
  book_end(b);
  if (!b->manualcalc) book_recalc(b);
  return 1;
}
int book_table_option(Book *b, int sheet, int ti, int which, int on)
{
  LTable *t = get(b, sheet, ti);
  Sheet *s;
  int m, k;
  if (!t) return 0;
  s = b->sh[sheet];
  on = on != 0;
  book_begin(b);
  book_note_sheet(b, sheet);
  m = hold(b);
  t = get(b, sheet, ti);
  switch (which) {
  case TO_BANDROWS: t->bandrows = (u8)on; break;
  case TO_BANDCOLS: t->bandcols = (u8)on; break;
  case TO_FIRSTCOL: t->firstcol = (u8)on; break;
  case TO_LASTCOL: t->lastcol = (u8)on; break;
  case TO_FILTERBTN:
    if (!on && filter_is_table(s, t)) book_autofilter(b, sheet, s->filter, 0);
    t->filterbtn = (u8)on;
    break;
  case TO_TOTALS:
    if (on == t->totals) break;
    if (on) {
      int any = 0;
      if (t->r.r2 + 1 >= MAXROW) break;
      if (!row_empty(s, t->r.r2 + 1, t->r.c1, t->r.c2)) { Rect row = t->r; row.r1 = row.r2 = t->r.r2 + 1; book_insert_cells(b, sheet, row, 1); t = get(b, sheet, ti); }
      t->r.r2++;
      t->totals = 1;
      for (k = 0; k < t->ncols; k++) if (t->cols[k].totfn || t->cols[k].totlabel) any = 1;
      if (!any && t->ncols) {
        /* as Excel starts it: "Total" under the first column, a sum (or count) under the last */
        Rect d;
        int numeric = 0;
        i32 r;
        table_data(t, &d);
        for (r = d.r1; r <= d.r2; r++) { const Value *v = cell_value(b, sheet, r, t->r.c2); if (v->t == V_NUM) numeric = 1; }
        if (t->ncols > 1) t->cols[0].totlabel = str_dup("Total");
        t->cols[t->ncols - 1].totfn = (u8)(numeric ? TF_SUM : TF_COUNT);
      }
      for (k = 0; k < t->ncols; k++) write_total(b, sheet, t, k);
    } else {
      Rect tr;
      table_totals(t, &tr);
      book_clear(b, sheet, tr, CLR_CONTENTS);
      t = get(b, sheet, ti);
      t->totals = 0;
      t->r.r2--;
    }
    break;
  case TO_HEADER:
    if (on == t->header) break;
    if (on) {
      Rect above;
      above = t->r; above.r1 = above.r2 = t->r.r1 - 1;
      if (t->r.r1 > 0 && row_empty(s, t->r.r1 - 1, t->r.c1, t->r.c2) && area_free(b, sheet, above, t)) t->r.r1--;
      else { Rect row = t->r; row.r2 = row.r1; book_insert_cells(b, sheet, row, 1); t = get(b, sheet, ti); t->r.r2++; }
      t->header = 1;
      write_headers(s, t);
    } else {
      Rect hr;
      if (filter_is_table(s, t)) book_autofilter(b, sheet, s->filter, 0);
      table_header(t, &hr);
      book_clear(b, sheet, hr, CLR_CONTENTS);
      t = get(b, sheet, ti);
      t->header = 0;
      t->r.r1++;
    }
    break;
  }
  b->dirty = 1;
  release(b, m);
  book_end(b);
  return 1;
}
int book_table_style(Book *b, int sheet, int ti, const char *style)
{
  LTable *t = get(b, sheet, ti);
  if (!t) return 0;
  book_begin(b);
  book_note_sheet(b, sheet);
  t = get(b, sheet, ti);
  snprintf(t->style, sizeof t->style, "%s", style ? style : "");
  b->dirty = 1;
  book_end(b);
  return 1;
}
int book_table_total(Book *b, int sheet, int ti, int col, int fn)
{
  LTable *t = get(b, sheet, ti);
  int m;
  if (!t || col < 0 || col >= t->ncols || fn < 0 || fn >= TF_N || fn == TF_CUSTOM) return 0;
  book_begin(b);
  book_note_sheet(b, sheet);
  m = hold(b);
  t = get(b, sheet, ti);
  t->cols[col].totfn = (u8)fn;
  free(t->cols[col].totlabel); t->cols[col].totlabel = NULL;
  if (t->totals) write_total(b, sheet, t, col);
  b->dirty = 1;
  release(b, m);
  book_end(b);
  return 1;
}
int book_table_resize(Book *b, int sheet, int ti, Rect a)
{
  LTable *t = get(b, sheet, ti), nt;
  Sheet *s;
  int m, totals, k;
  if (!t) return 0;
  s = b->sh[sheet];
  if (a.r2 < a.r1 || a.c2 < a.c1 || a.c2 - a.c1 >= 1000 || a.r2 >= MAXROW - 2) return 0;
  if (t->header && a.r1 != t->r.r1) return 0;
  if (!overlaps(&a, &t->r) || a.r2 < a.r1 + t->header) return 0;
  if (!area_free(b, sheet, a, t)) return 0;
  book_begin(b);
  book_note_book(b);
  m = hold(b);
  t = get(b, sheet, ti);
  totals = t->totals;
  if (totals) { book_table_option(b, sheet, ti, TO_TOTALS, 0); t = get(b, sheet, ti); if (a.r2 > t->r.r2 + 1) a.r2--; else if (a.r2 == t->r.r2 + 1) a.r2--; }
  nt = *t;
  nt.r = a;
  nt.cols = (TableCol *)calloc((size_t)(a.c2 - a.c1 + 1), sizeof(TableCol));
  nt.ncols = 0;
  if (nt.cols) {
    for (k = 0; k <= a.c2 - a.c1; k++) {
      i32 c = a.c1 + k;
      if (c >= t->r.c1 && c <= t->r.c2) col_copy(&nt.cols[k], &t->cols[c - t->r.c1]);
      else {
        char h[256], nm[280];
        if (t->header) header_text(b, sheet, a.r1, c, h, sizeof h); else h[0] = 0;
        if (!h[0]) sprintf(h, "Column%d", k + 1);
        nt.ncols = k;
        unique_col(&nt, h, -1, nm, sizeof nm);
        nt.cols[k].name = str_dup(nm);
      }
      nt.ncols = k + 1;
    }
    table_free1(t);
    *t = nt;
    write_headers(s, t);
    if (totals) book_table_option(b, sheet, ti, TO_TOTALS, 1);
  }
  b->dirty = 1;
  release(b, m);
  book_end(b);
  return 1;
}
int book_table_convert(Book *b, int sheet, int ti)
{
  LTable *t = get(b, sheet, ti);
  Sheet *s;
  i32 r, c;
  int m;
  if (!t) return 0;
  s = b->sh[sheet];
  book_begin(b);
  book_note_book(b);
  m = hold(b);
  t = get(b, sheet, ti);
  each_tref(b, unref, t);
  /* the table's look stays, as the cells' own formatting */
  for (r = t->r.r1; r <= t->r.r2; r++)
    for (c = t->r.c1; c <= t->r.c2; c++) {
      TblLook lk;
      Style st;
      Font f;
      Cell *cell;
      int k, si;
      if (!table_look(b, sheet, r, c, &lk)) continue;
      st = b->styles[sheet_cell_style(s, r, c)];
      f = b->fonts[st.font];
      if (!st.fill && lk.fill) st.fill = lk.fill;
      if (f.color == COLOR_AUTO && lk.color != COLOR_AUTO) f.color = lk.color;
      if (lk.bold) f.bold = 1;
      for (k = 0; k < 4; k++) if (!st.border[k] && lk.border[k]) { st.border[k] = lk.border[k]; st.bcolor[k] = lk.bcolor[k]; }
      if ((si = book_font(b, &f)) >= 0) st.font = (u16)si;
      if ((si = book_style(b, &st)) >= 0 && (cell = sheet_cell_make(s, r, c))) cell->style = (u16)si;
    }
  if (filter_is_table(s, t)) book_autofilter(b, sheet, s->filter, 0);
  table_remove(s, ti);
  b->dirty = 1;
  release(b, m);
  book_end(b);
  return 1;
}
int book_table_dedupe(Book *b, int sheet, int ti, u32 mask)
{
  LTable *t = get(b, sheet, ti);
  Rect d, area;
  int n, m;
  if (!t) return 0;
  table_data(t, &d);
  area = d;
  if (t->header) area.r1 = t->r.r1;
  book_begin(b);
  book_note_sheet(b, sheet);
  m = hold(b);
  n = book_remove_duplicates(b, sheet, area, mask, t->header);
  if (n > 0) {
    Rect gone = d;
    t = get(b, sheet, ti);
    gone.r1 = d.r2 - n + 1;
    if (gone.r1 <= d.r1) gone.r1 = d.r1 + 1;     /* a row stays */
    if (gone.r1 <= gone.r2) {
      book_delete_cells(b, sheet, gone, 1);
      t = get(b, sheet, ti);
      t->r.r2 -= gone.r2 - gone.r1 + 1;
    }
  }
  b->dirty = 1;
  release(b, m);
  book_end(b);
  return n;
}
int table_activate_filter(Book *b, int sheet, i32 r, i32 c)
{
  LTable *t = table_at(b, sheet, r, c);
  Rect f;
  if (!t || !t->header || !t->filterbtn || r != t->r.r1) return 0;
  if (filter_is_table(b->sh[sheet], t)) return 1;
  table_filter_rect(t, &f);
  return book_autofilter(b, sheet, f, 1);
}
int table_filter_button(Sheet *s, i32 r, i32 c)
{
  int i;
  for (i = 0; i < s->ntables; i++) {
    LTable *t = &s->tables[i];
    if (t->header && t->filterbtn && r == t->r.r1 && c >= t->r.c1 && c <= t->r.c2) return 1;
  }
  return 0;
}
void tables_rename_copies(Book *b, int sheet)
{
  Sheet *s = b->sh[sheet];
  int i, k, o;
  for (i = 0; i < s->ntables; i++) {
    int clash = 0;
    for (o = 0; o < b->nsh; o++) {
      if (o == sheet) continue;
      for (k = 0; k < b->sh[o]->ntables; k++) if (str_ieq(b->sh[o]->tables[k].name, s->tables[i].name)) clash = 1;
    }
    if (clash) {
      char nm[64], old[64];
      Ren rn;
      strcpy(old, s->tables[i].name);
      s->tables[i].name[0] = 0;                 /* (not a clash with itself) */
      table_unique_name(b, nm);
      strcpy(s->tables[i].name, nm);
      rn.from = old; rn.to = nm; rn.sheet = sheet; rn.t = &s->tables[i];
      each_tref(b, ren_table, &rn);
    }
  }
}

/* ------------------------------------------------------------------ typing into and beside tables */
enum { TG_HEADER = 1, TG_DOWN, TG_RIGHT, TG_CALC };
int table_input_pre(Book *b, int sheet, i32 r, i32 c, const char *text)
{
  Sheet *s = b->sh[sheet];
  Cell *cell = sheet_cell(s, r, c);
  int empty = !cell || (!cell->f && cell->v.t == V_EMPTY), i;
  for (i = 0; i < s->ntables; i++) {
    LTable *t = &s->tables[i];
    Rect d;
    table_data(t, &d);
    if (t->header && r == t->r.r1 && c >= t->r.c1 && c <= t->r.c2) { book_note_book(b); return TG_HEADER << 16 | i; }
    if (!text[0] || !empty) continue;
    if (r == t->r.r2 + 1 && !t->totals && c >= t->r.c1 && c <= t->r.c2 && !table_at(b, sheet, r, c) && r < MAXROW - 1) { book_note_sheet(b, sheet); return TG_DOWN << 16 | i; }
    if (c == t->r.c2 + 1 && r >= t->r.r1 && r <= t->r.r2 - t->totals && c < MAXCOL && !table_at(b, sheet, r, c)) {
      i32 rr;
      int clear = 1;
      for (rr = t->r.r1; rr <= t->r.r2; rr++) if (rr != r) { Cell *o = sheet_cell(s, rr, c); if (o && (o->f || o->v.t != V_EMPTY)) clear = 0; }
      if (clear) { book_note_sheet(b, sheet); return TG_RIGHT << 16 | i; }
    }
    if ((text[0] == '=' || text[0] == '+') && text[1] && r >= d.r1 && r <= d.r2 && c >= t->r.c1 && c <= t->r.c2 && d.r2 > d.r1) {
      i32 rr;
      int clear = 1;
      for (rr = d.r1; rr <= d.r2 && clear; rr++) if (rr != r) { Cell *o = sheet_cell(s, rr, c); if (o && (o->f || o->v.t != V_EMPTY)) clear = 0; }
      if (clear) { book_note_sheet(b, sheet); return TG_CALC << 16 | i; }
    }
  }
  return 0;
}
/* the calculated columns' formulas put into row r (a new row) */
static void fill_calc(Book *b, int sheet, LTable *t, i32 r)
{
  int k, pos;
  for (k = 0; k < t->ncols; k++) {
    Formula *f;
    Cell *cell;
    if (!t->cols[k].calcformula) continue;
    if ((cell = sheet_cell(b->sh[sheet], r, t->r.c1 + k)) && (cell->f || cell->v.t != V_EMPTY)) continue;
    /* the formula as written for the first data row, moved to this one */
    if (!(f = formula_parse(b, sheet, t->cols[k].calcformula, &pos))) continue;
    formula_offset(f, r - (t->r.r1 + t->header), 0);
    if ((cell = sheet_cell_make(b->sh[sheet], r, t->r.c1 + k))) { val_free(&cell->v); formula_free(cell->f); cell->f = f; cell->calcgen = 0; }
    else formula_free(f);
  }
}
void table_input_post(Book *b, int sheet, i32 r, i32 c, int how)
{
  Sheet *s = b->sh[sheet];
  int ti = how & 0xFFFF, k;
  LTable *t;
  if (ti >= s->ntables) return;
  t = &s->tables[ti];
  switch (how >> 16) {
  case TG_HEADER: {
    char h[256], nm[280];
    k = (int)(c - t->r.c1);
    header_text(b, sheet, r, c, h, sizeof h);
    if (!h[0]) sprintf(h, "Column%d", k + 1);
    unique_col(t, h, k, nm, sizeof nm);
    rename_column(b, t, k, nm);
    put_text(s, r, c, nm);
    break; }
  case TG_DOWN:
    t->r.r2++;
    fill_calc(b, sheet, t, r);
    break;
  case TG_RIGHT: {
    char h[256], nm[280];
    TableCol *nc = (TableCol *)realloc(t->cols, (size_t)(t->ncols + 1) * sizeof(TableCol));
    if (!nc) break;
    t->cols = nc;
    memset(&t->cols[t->ncols], 0, sizeof(TableCol));
    t->ncols++;
    t->r.c2++;
    k = t->ncols - 1;
    if (t->header && r == t->r.r1) header_text(b, sheet, r, c, h, sizeof h); else h[0] = 0;
    if (!h[0]) sprintf(h, "Column%d", k + 1);
    unique_col(t, h, k, nm, sizeof nm);
    t->cols[k].name = str_dup(nm);
    if (t->header) put_text(s, t->r.r1, c, nm);
    /* a formula typed beside the table's data: a calculated column too */
    {
      Cell *src = sheet_cell(s, r, c);
      Rect d;
      table_data(t, &d);
      if (!src || !src->f || r < d.r1 || r > d.r2) break;
    }
    } /* fall through */
  case TG_CALC: {
    Cell *src = sheet_cell(s, r, c);
    Rect d;
    i32 rr;
    Buf ft;
    if (!src || !src->f) break;
    table_data(t, &d);
    k = (int)(c - t->r.c1);
    /* kept as written for the first data row (so new rows get it moved down) */
    {
      Formula *f0 = formula_copy(src->f);
      if (f0) {
        formula_offset(f0, d.r1 - r, 0);
        buf_init(&ft);
        formula_text(b, sheet, f0, &ft);
        free(t->cols[k].calcformula);
        t->cols[k].calcformula = buf_take(&ft);
        formula_free(f0);
      }
    }
    for (rr = d.r1; rr <= d.r2; rr++) {
      Formula *f;
      Cell *cell;
      if (rr == r || !(f = formula_copy(src->f))) continue;
      formula_offset(f, rr - r, 0);
      if ((cell = sheet_cell_make(s, rr, c))) { val_free(&cell->v); formula_free(cell->f); cell->f = f; cell->calcgen = 0; }
      else formula_free(f);
      src = sheet_cell(s, r, c);
      if (!src) break;
    }
    break; }
  }
  b->dirty = 1;
}

/* ------------------------------------------------------------------ styles */
/* the Office theme: dark 1, light 1, then accents 1-6 */
static const u32 accents[6] = { 0x4472C4, 0xED7D31, 0xA5A5A5, 0xFFC000, 0x5B9BD5, 0x70AD47 };
/* a colour lightened toward white (t > 0) or darkened (t < 0), t in thousandths */
static u32 tint(u32 c, int t)
{
  int k, out = 0;
  for (k = 16; k >= 0; k -= 8) {
    int v = (int)((c >> k) & 255);
    v = t >= 0 ? v + (255 - v) * t / 1000 : v * (1000 + t) / 1000;
    out = out << 8 | (v < 0 ? 0 : v > 255 ? 255 : v);
  }
  return (u32)out;
}
static const char *const fams[3] = { "Light", "Medium", "Dark" };
static const int famn[3] = { 21, 28, 11 };
void table_style_name(int i, char *out)
{
  int f = 0;
  while (f < 2 && i >= famn[f]) { i -= famn[f]; f++; }
  if (i >= famn[f]) i = famn[f] - 1;
  sprintf(out, "TableStyle%s%d", fams[f], i + 1);
}
/* family (0 Light, 1 Medium, 2 Dark) and number of a style name ("TableStyleMedium2",
   "PivotStyleLight16"); -1 if it isn't one */
static int style_parse(const char *name, int *n)
{
  const char *p = name;
  int f;
  if (!strncmp(p, "TableStyle", 10)) p += 10;
  else if (!strncmp(p, "PivotStyle", 10)) p += 10;
  else return -1;
  for (f = 0; f < 3; f++)
    if (!strncmp(p, fams[f], strlen(fams[f])) && isdigit((u8)p[strlen(fams[f])])) { *n = atoi(p + strlen(fams[f])); return *n >= 1 ? f : -1; }
  return -1;
}
int table_style_index(const char *name)
{
  int n, f = style_parse(name ? name : "", &n), i, base = 0;
  if (f < 0 || strncmp(name, "TableStyle", 10) || n > famn[f]) return -1;
  for (i = 0; i < f; i++) base += famn[i];
  return base + n - 1;
}
void table_style_colors(const char *name, TStyle *o)
{
  int n = 2, f = style_parse(name ? name : "", &n), k, g;
  u32 a;
  memset(o, 0, sizeof *o);
  o->hfill = o->body = o->band = o->line = o->grid = o->outline = o->hline = o->tline = o->tfill = o->colfill = NOCOL;
  o->hfont = o->bodyfont = o->tfont = NOCOL;
  o->tdouble = 1;
  if (!name || !name[0]) return;                /* no style: plain */
  if (f < 0) { f = 1; n = 2; }
  k = (n - 1) % 7; g = (n - 1) / 7;
  a = k ? accents[k - 1] : 0x000000;
  switch (f) {
  case 0:                                       /* Light: lines and pale bands */
    if (g == 0) {
      o->hfont = k ? tint(a, -250) : 0x000000;
      o->hline = a; o->outline = NOCOL; o->tline = a;
      o->band = k ? tint(a, 800) : 0xD9D9D9;
      o->bodyfont = k ? tint(a, -250) : NOCOL;
      o->line = NOCOL;
    } else if (g == 1) {
      o->hfill = a; o->hfont = 0xFFFFFF;
      o->outline = a; o->line = a; o->tline = a;
      o->band = NOCOL;
    } else {
      o->hfont = 0x000000; o->hline = a; o->grid = a; o->line = a; o->outline = a; o->tline = a;
      o->band = k ? tint(a, 800) : 0xD9D9D9;
    }
    break;
  case 1:                                       /* Medium: a filled header and bands */
    if (g == 0) {
      o->hfill = a; o->hfont = 0xFFFFFF;
      o->band = k ? tint(a, 800) : 0xD9D9D9; o->tline = a;
      o->line = k ? tint(a, 400) : 0xBFBFBF;
    } else if (g == 1) {
      o->hfill = a; o->hfont = 0xFFFFFF;
      o->body = k ? tint(a, 800) : 0xD9D9D9; o->band = k ? tint(a, 600) : 0xBFBFBF;
      o->grid = 0xFFFFFF; o->line = 0xFFFFFF; o->tline = 0xFFFFFF;
      o->tfill = a; o->tfont = 0xFFFFFF; o->colfill = a;
    } else if (g == 2) {
      o->hfill = 0x000000; o->hfont = 0xFFFFFF;
      o->band = k ? tint(a, 800) : 0xD9D9D9; o->line = k ? tint(a, 400) : 0xA6A6A6; o->outline = k ? a : 0x000000;
      o->tline = 0x000000;
    } else {
      o->hfont = 0x000000; o->hline = a;
      o->body = k ? tint(a, 800) : 0xF2F2F2; o->band = k ? tint(a, 600) : 0xD9D9D9;
      o->line = k ? tint(a, 400) : 0xBFBFBF; o->grid = o->line; o->outline = k ? tint(a, 400) : 0x7F7F7F; o->tline = a;
    }
    break;
  default:                                      /* Dark: dark header, deep bands */
    if (g == 0 || n > 11) {
      o->hfill = 0x000000; o->hfont = 0xFFFFFF;
      o->body = k ? a : 0x737373; o->band = k ? tint(a, -250) : 0x404040; o->bodyfont = 0xFFFFFF;
      o->tfill = k ? tint(a, -500) : 0x262626; o->tfont = 0xFFFFFF; o->tline = 0xFFFFFF;
      o->colfill = k ? tint(a, -250) : 0x404040;
    } else {
      /* Dark 8-11: pairs of accents (8 neutral) */
      u32 a2 = n == 8 ? 0x000000 : accents[(n - 9) * 2 + 1];
      a = n == 8 ? 0x000000 : accents[(n - 9) * 2];
      o->hfill = a2; o->hfont = 0xFFFFFF;
      o->body = n == 8 ? 0xD9D9D9 : tint(a, 800); o->band = n == 8 ? 0xBFBFBF : tint(a, 600);
      o->tfill = a; o->tfont = 0xFFFFFF; o->tline = a2; o->tdouble = 0;
      o->colfill = n == 8 ? 0xBFBFBF : tint(a, 400);
    }
    break;
  }
}
static int f_dark(u32 c) { return (((c >> 16) & 255) * 3 + ((c >> 8) & 255) * 6 + (c & 255)) < 1280; }
static void set_border(TblLook *lk, int side, int kind, u32 col)
{
  if (col == NOCOL) return;
  lk->border[side] = (u8)kind; lk->bcolor[side] = col;
}
int table_look(Book *b, int sheet, i32 r, i32 c, TblLook *lk)
{
  Sheet *s;
  LTable *t = NULL;
  TStyle st;
  int i, hdr, tot, first, last;
  i32 dr, dc;
  memset(lk, 0, sizeof *lk);
  lk->color = COLOR_AUTO;
  if (sheet < 0 || sheet >= b->nsh) return 0;
  s = b->sh[sheet];
  for (i = 0; i < s->ntables; i++)
    if (r >= s->tables[i].r.r1 && r <= s->tables[i].r.r2 && c >= s->tables[i].r.c1 && c <= s->tables[i].r.c2) { t = &s->tables[i]; break; }
  if (!t) return s->npivots ? pivot_look(b, sheet, r, c, lk) : 0;
  table_style_colors(t->style, &st);
  hdr = t->header && r == t->r.r1;
  tot = t->totals && r == t->r.r2;
  first = t->firstcol && c == t->r.c1;
  last = t->lastcol && c == t->r.c2;
  dr = r - t->r.r1 - t->header;
  dc = c - t->r.c1;
  if (st.outline != NOCOL) {
    if (r == t->r.r1) set_border(lk, B_TOP, BS_THIN, st.outline);
    if (r == t->r.r2) set_border(lk, B_BOTTOM, BS_THIN, st.outline);
    if (c == t->r.c1) set_border(lk, B_LEFT, BS_THIN, st.outline);
    if (c == t->r.c2) set_border(lk, B_RIGHT, BS_THIN, st.outline);
  }
  if (st.grid != NOCOL && !hdr) {
    if (c > t->r.c1) set_border(lk, B_LEFT, BS_THIN, st.grid);
    if (c < t->r.c2) set_border(lk, B_RIGHT, BS_THIN, st.grid);
  }
  if (hdr) {
    if (st.hfill != NOCOL) lk->fill = FILL_ON | st.hfill;
    if (st.hfont != NOCOL) lk->color = st.hfont;
    lk->bold = 1;
    set_border(lk, B_BOTTOM, BS_THIN, st.hline);
    return 1;
  }
  if (tot) {
    lk->bold = 1;
    if (st.tfill != NOCOL) lk->fill = FILL_ON | st.tfill;
    if (st.tfont != NOCOL) lk->color = st.tfont;
    set_border(lk, B_TOP, st.tdouble ? BS_DOUBLE : BS_THIN, st.tline);
    return 1;
  }
  if ((t->bandrows && dr % 2 == 0) || (t->bandcols && dc % 2 == 0)) { if (st.band != NOCOL) lk->fill = FILL_ON | st.band; else if (st.body != NOCOL) lk->fill = FILL_ON | st.body; }
  else if (st.body != NOCOL) lk->fill = FILL_ON | st.body;
  if (st.bodyfont != NOCOL) lk->color = st.bodyfont;
  if (st.line != NOCOL && r < t->r.r2) set_border(lk, B_BOTTOM, BS_THIN, st.line);
  if (first || last) {
    lk->bold = 1;
    if (st.colfill != NOCOL) { lk->fill = FILL_ON | st.colfill; if (st.bodyfont == NOCOL && f_dark(st.colfill)) lk->color = 0xFFFFFF; }
  }
  return 1;
}

/* ------------------------------------------------------------------ SUBTOTAL */
/* a cell's formula uses SUBTOTAL or AGGREGATE itself (such cells are left out, so totals of
   totals don't count twice) */
static int has_subtotal(const Formula *f)
{
  int i;
  for (i = 0; f && i < f->n; i++)
    if (f->t[i].op == T_FUNC && f->t[i].a != 0xFFFF) {
      const char *nm = func_name(f->t[i].a);
      if (!strcmp(nm, "SUBTOTAL") || !strcmp(nm, "AGGREGATE")) return 1;
    }
  return 0;
}
static Value f_subtotal(Ctx *cx, Value *a, int n)
{
  long fn;
  int e, k, skiphidden;
  Nums ns;
  double sum = 0, prod = 1, mx = -HUGE_VAL, mn = HUGE_VAL, m, ss = 0;
  long cnt = 0, cnta = 0, i;
  if ((e = arg_int(cx, &a[0], &fn))) return val_err(e);
  skiphidden = fn > 100;
  if (skiphidden) fn -= 100;
  if (fn < 1 || fn > 11) return val_err(E_VALUE);
  memset(&ns, 0, sizeof ns);
  for (k = 1; k < n; k++) {
    Sheet *s;
    i32 r, c, r2;
    if (a[k].t != V_REF) { free(ns.v); return val_err(a[k].t == V_ERR ? a[k].e : E_VALUE); }
    if (a[k].sheet >= cx->b->nsh) continue;
    s = cx->b->sh[a[k].sheet];
    r2 = a[k].r2 < s->nrows - 1 ? a[k].r2 : s->nrows - 1;
    for (r = a[k].r1; r <= r2; r++) {
      Row *row = s->rows[r];
      if (!row || row->filtered || (skiphidden && row->hidden)) continue;
      for (c = a[k].c1; c <= a[k].c2; c++) {
        Cell *cell = sheet_cell(s, r, c);
        const Value *v;
        if (!cell || (cell->f && has_subtotal(cell->f))) continue;
        v = ref_cell(cx, a[k].sheet, r, c);
        if (v->t == V_ERR) { free(ns.v); return val_err(v->e); }
        if (v->t != V_EMPTY) cnta++;
        if (v->t == V_NUM) { nums_add(&ns, v->n); }
        row = s->rows[r];
        if (!row) break;
      }
    }
  }
  cnt = ns.n;
  for (i = 0; i < ns.n; i++) { double d = ns.v[i]; sum += d; prod *= d; if (d > mx) mx = d; if (d < mn) mn = d; }
  m = cnt ? sum / cnt : 0;
  for (i = 0; i < ns.n; i++) ss += (ns.v[i] - m) * (ns.v[i] - m);
  free(ns.v);
  switch (fn) {
  case 1: return cnt ? num_or_err(m) : val_err(E_DIV0);
  case 2: return val_num((double)cnt);
  case 3: return val_num((double)cnta);
  case 4: return val_num(cnt ? mx : 0);
  case 5: return val_num(cnt ? mn : 0);
  case 6: return num_or_err(cnt ? prod : 0);
  case 7: return cnt > 1 ? num_or_err(sqrt(ss / (cnt - 1))) : val_err(E_DIV0);
  case 8: return cnt > 0 ? num_or_err(sqrt(ss / cnt)) : val_err(E_DIV0);
  case 9: return num_or_err(sum);
  case 10: return cnt > 1 ? num_or_err(ss / (cnt - 1)) : val_err(E_DIV0);
  default: return cnt > 0 ? num_or_err(ss / cnt) : val_err(E_DIV0);
  }
}
const FuncDef table_func_defs[] = {
  { "SUBTOTAL", 2, 255, 0, f_subtotal },
};
const int table_func_count = (int)(sizeof table_func_defs / sizeof table_func_defs[0]);
