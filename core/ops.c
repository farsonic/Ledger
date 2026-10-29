/*
 * ops.c - see ops.h: copy and paste, fill, sort, duplicates, find and replace.
 *
 * Each operation is one undo step (book_begin .. book_end) and recalculates once at the end, not
 * after every cell it changes (calculation is held as manual meanwhile).
 */
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>
#include <ctype.h>
#include "ops.h"
#include "calc.h"

extern int ledger_date_order;

/* calculation held while an operation changes many cells, and done once after */
static int hold(Book *b) { int m = b->manualcalc; b->manualcalc = 1; book_begin(b); return m; }
static void release(Book *b, int m) { book_end(b); b->manualcalc = m; b->dirty = 1; if (!m) book_recalc(b); }

/* ------------------------------------------------------------------ copy and paste */
Clip *clip_copy(Book *b, int sheet, Rect a)
{
  Clip *cl;
  Sheet *s;
  i32 r, c;
  long n;
  if (sheet < 0 || sheet >= b->nsh) return NULL;
  s = b->sh[sheet];
  if (a.r2 > sheet_last_row(s)) a.r2 = sheet_last_row(s) > a.r1 ? sheet_last_row(s) : a.r1;   /* whole columns: as far as there is anything */
  if (a.c2 > sheet_last_col(s)) a.c2 = sheet_last_col(s) > a.c1 ? sheet_last_col(s) : a.c1;
  n = (long)(a.r2 - a.r1 + 1) * (a.c2 - a.c1 + 1);
  if (n <= 0 || n > 4000000L || !(cl = (Clip *)calloc(1, sizeof *cl))) return NULL;
  if (!(cl->cells = (ClipCell *)calloc((size_t)n, sizeof(ClipCell)))) { free(cl); return NULL; }
  cl->rows = a.r2 - a.r1 + 1; cl->cols = a.c2 - a.c1 + 1; cl->r0 = a.r1; cl->c0 = a.c1;
  for (r = a.r1; r <= a.r2; r++)
    for (c = a.c1; c <= a.c2; c++) {
      ClipCell *k = &cl->cells[(long)(r - a.r1) * cl->cols + (c - a.c1)];
      Cell *cell = sheet_cell(s, r, c);
      int st = sheet_cell_style(s, r, c);
      k->st = b->styles[st];
      k->font = b->fonts[k->st.font];
      if (k->st.numfmt >= 164) k->fmt = str_dup(book_fmt_code(b, k->st.numfmt));
      if (!cell) continue;
      if (cell->f || cell->v.t != V_EMPTY) {
        k->has = 1;
        k->v = val_copy(cell_value(b, sheet, r, c));
        if (cell->f) k->f = formula_copy(cell->f);
      }
    }
  return cl;
}
void clip_free(Clip *cl)
{
  long i;
  if (!cl) return;
  for (i = 0; i < (long)cl->rows * cl->cols; i++) { val_free(&cl->cells[i].v); formula_free(cl->cells[i].f); free(cl->cells[i].fmt); }
  free(cl->cells);
  free(cl);
}
/* a clip cell's look as a style of this book */
static int clip_style(Book *b, const ClipCell *k)
{
  Style st = k->st;
  int f = book_font(b, &k->font);
  if (st.numfmt >= 164) st.numfmt = (u16)book_fmt(b, k->fmt ? k->fmt : "General");
  st.font = (u16)(f < 0 ? 0 : f);
  f = book_style(b, &st);
  return f < 0 ? 0 : f;
}
int book_paste(Book *b, int sheet, i32 r, i32 c, const Clip *cl, int what, int transpose, Rect *out)
{
  i32 i, j, rows, cols;
  int m;
  Sheet *s;
  if (!cl || sheet < 0 || sheet >= b->nsh) return 0;
  rows = transpose ? cl->cols : cl->rows; cols = transpose ? cl->rows : cl->cols;
  if (r < 0 || c < 0 || r + rows > MAXROW || c + cols > MAXCOL) return 0;
  s = b->sh[sheet];
  m = hold(b);
  for (i = 0; i < cl->rows; i++)
    for (j = 0; j < cl->cols; j++) {
      const ClipCell *k = &cl->cells[(long)i * cl->cols + j];
      i32 dr = transpose ? r + j : r + i, dc = transpose ? c + i : c + j;
      if (what != PASTE_FORMATS) {
        if (!k->has) {
          Cell *cell = sheet_cell(s, dr, dc);
          if (cell && (cell->f || cell->v.t != V_EMPTY)) { Rect one; one.r1 = one.r2 = dr; one.c1 = one.c2 = dc; book_clear(b, sheet, one, CLR_CONTENTS); }
        } else if (k->f && what != PASTE_VALUES) {
          Formula *f = formula_copy(k->f);
          if (f) formula_offset(f, dr - (cl->r0 + i), dc - (cl->c0 + j));
          book_set_formula(b, sheet, dr, dc, f);
        } else book_set_value(b, sheet, dr, dc, &k->v);
      }
      if (what == PASTE_ALL || what == PASTE_FORMATS) {
        int st = clip_style(b, k);
        Cell *cell = sheet_cell(s, dr, dc);
        if (st || cell) book_set_style(b, sheet, dr, dc, st);
      } else if (what == PASTE_VALUES && k->has) {
        /* values keep their number format, so dates stay dates */
        int st = sheet_cell_style(s, dr, dc);
        Style ns = b->styles[st];
        ns.numfmt = k->st.numfmt < 164 ? k->st.numfmt : (u16)book_fmt(b, k->fmt ? k->fmt : "General");
        if (ns.numfmt != b->styles[st].numfmt) { int si = book_style(b, &ns); if (si >= 0) book_set_style(b, sheet, dr, dc, si); }
      }
    }
  release(b, m);
  if (out) { out->r1 = r; out->c1 = c; out->r2 = r + rows - 1; out->c2 = c + cols - 1; }
  return 1;
}
int book_paste_text(Book *b, int sheet, i32 r, i32 c, const char *text, Rect *out)
{
  const char *p = text;
  i32 row = r, col = c, maxc = c;
  int m;
  Buf f;
  if (sheet < 0 || sheet >= b->nsh) return 0;
  buf_init(&f);
  m = hold(b);
  while (*p && row < MAXROW) {
    const char *e = p;
    while (*e && *e != '\t' && *e != '\n' && *e != '\r') e++;
    f.len = 0;
    buf_add(&f, p, e - p);
    buf_addc(&f, 0);
    if (col < MAXCOL && (f.data[0] || sheet_cell(b->sh[sheet], row, col))) {
      if (!book_input(b, sheet, row, col, f.data, NULL)) { Value v = val_str(f.data); book_set_value(b, sheet, row, col, &v); val_free(&v); }
    }
    if (col > maxc) maxc = col;
    if (*e == '\t') { col++; p = e + 1; continue; }
    if (*e == '\r' && e[1] == '\n') e++;
    if (!*e) break;
    p = e + 1;
    if (!*p) break;
    row++; col = c;
  }
  release(b, m);
  buf_free(&f);
  if (out) { out->r1 = r; out->c1 = c; out->r2 = row; out->c2 = maxc; }
  return 1;
}
char *book_copy_text(Book *b, int sheet, Rect a)
{
  Buf o;
  i32 r, c;
  char t[512];
  Sheet *s;
  if (sheet < 0 || sheet >= b->nsh) return NULL;
  s = b->sh[sheet];
  if (a.r2 > sheet_last_row(s)) a.r2 = sheet_last_row(s) > a.r1 ? sheet_last_row(s) : a.r1;
  if (a.c2 > sheet_last_col(s)) a.c2 = sheet_last_col(s) > a.c1 ? sheet_last_col(s) : a.c1;
  buf_init(&o);
  for (r = a.r1; r <= a.r2; r++) {
    for (c = a.c1; c <= a.c2; c++) {
      u32 col;
      book_shown_text(b, sheet, r, c, t, sizeof t, &col);
      buf_add(&o, t, strlen(t));
      if (c < a.c2) buf_addc(&o, '\t');
    }
    buf_addc(&o, '\n');
  }
  buf_addc(&o, 0);
  return o.data;
}

/* ------------------------------------------------------------------ fill */
static const char *const daynames[2][7] = { { "Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday" },
                                             { "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat" } };
static const char *const monthnames[2][12] = { { "January", "February", "March", "April", "May", "June", "July", "August", "September", "October", "November", "December" },
                                               { "Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec" } };
/* a day or month name: its list (0-3: days long/short, months long/short) and place; -1 if neither */
static int name_list(const char *s, int *pos)
{
  int k, i;
  for (k = 0; k < 2; k++) for (i = 0; i < 7; i++) if (str_ieq(s, daynames[k][i])) { *pos = i; return k; }
  for (k = 0; k < 2; k++) for (i = 0; i < 12; i++) if (str_ieq(s, monthnames[k][i])) { *pos = i; return 2 + k; }
  return -1;
}
/* the name at pos (wrapping) of a list, in the case the original was written in */
static void list_name(int list, int pos, const char *like, char *out)
{
  const char *n = list < 2 ? daynames[list][((pos % 7) + 7) % 7] : monthnames[list - 2][((pos % 12) + 12) % 12];
  size_t i;
  int upper = like[0] && like[1] && isupper((u8)like[1]), lower = like[0] && islower((u8)like[0]);
  strcpy(out, n);
  for (i = 0; out[i]; i++) out[i] = (char)(upper ? toupper((u8)out[i]) : lower ? tolower((u8)out[i]) : out[i]);
}
/* text ending in a whole number ("Item 3", "Q1"): the length before the number and the number */
static int trailing_number(const char *s, size_t *pre, long *num)
{
  size_t n = strlen(s), i = n;
  while (i > 0 && isdigit((u8)s[i - 1])) i--;
  if (i == n || i == 0 || n - i > 9) return 0;
  *pre = i; *num = atol(s + i);
  return 1;
}
/* one line of a fill: n source cells (values in src[], formulas f[], styles st[], positions given
   by step from the first) continued into m more cells at put(k) */
typedef struct { Book *b; int sheet; i32 r0, c0, dr, dc; } Line;
static void line_put(Line *L, i32 k, const Value *v, const Formula *f, i32 from_k, int style)
{
  i32 r = L->r0 + k * L->dr, c = L->c0 + k * L->dc;
  if (f) {
    Formula *g = formula_copy(f);
    if (g) formula_offset(g, (k - from_k) * L->dr, (k - from_k) * L->dc);
    book_set_formula(L->b, L->sheet, r, c, g);
  } else if (v->t == V_EMPTY) {
    Rect one; one.r1 = one.r2 = r; one.c1 = one.c2 = c;
    book_clear(L->b, L->sheet, one, CLR_CONTENTS);
  } else book_set_value(L->b, L->sheet, r, c, v);
  book_set_style(L->b, L->sheet, r, c, style);
}
static void fill_line(Line *L, i32 n, i32 m)
{
  Sheet *s = L->b->sh[L->sheet];
  Value *v = (Value *)calloc((size_t)n, sizeof(Value));
  const Formula **f = (const Formula **)calloc((size_t)n, sizeof(Formula *));
  int *st = (int *)calloc((size_t)n, sizeof(int));
  i32 k, nums = 0, texts = 0, forms = 0;
  if (!v || !f || !st) { free(v); free(f); free(st); return; }
  for (k = 0; k < n; k++) {
    i32 r = L->r0 + k * L->dr, c = L->c0 + k * L->dc;
    Cell *cell = sheet_cell(s, r, c);
    st[k] = sheet_cell_style(s, r, c);
    if (!cell) { v[k] = val_empty(); continue; }
    if (cell->f) { f[k] = cell->f; forms++; v[k] = val_empty(); continue; }
    v[k] = val_copy(&cell->v);
    if (v[k].t == V_NUM) nums++; else if (v[k].t == V_STR) texts++;
  }
  if (nums == n && !forms) {
    /* numbers: one is repeated, except a date counts on by days; several make a linear series */
    double step = n > 1 ? (v[n - 1].n - v[0].n) / (n - 1) : fmt_is_date(book_fmt_code(L->b, L->b->styles[st[0]].numfmt)) ? 1 : 0;
    for (k = n; k < n + m; k++) { Value x = val_num(v[0].n + step * k); line_put(L, k, &x, NULL, 0, st[k % n]); }
  } else if (texts == n && !forms) {
    /* names of days and months go on round; numbered text counts on; else repeated */
    int list, pos, pos2;
    size_t pre;
    long num, num2 = 0;
    if ((list = name_list(v[0].s, &pos)) >= 0) {
      int step = n > 1 && name_list(v[1].s, &pos2) == list ? pos2 - pos : 1;
      for (k = n; k < n + m; k++) { char t[16]; Value x; list_name(list, pos + step * k, v[0].s, t); x = val_str(t); line_put(L, k, &x, NULL, 0, st[k % n]); val_free(&x); }
    } else if (trailing_number(v[0].s, &pre, &num)) {
      long step = 1;
      if (n > 1 && trailing_number(v[1].s, &pre, &num2)) step = num2 - num;
      for (k = n; k < n + m; k++) {
        char t[300]; Value x;
        size_t p2;
        long base;
        trailing_number(v[k % n].s, &p2, &base);
        /* (the text before the number copied by hand: vbcc's library takes %.*s with a precision of
           0 as none at all, so a value that is only a number got itself in front of the new one) */
        if (p2 > sizeof t - 24) p2 = sizeof t - 24;
        memcpy(t, v[k % n].s, p2);
        snprintf(t + p2, sizeof t - p2, "%ld", num + step * k);
        x = val_str(t); line_put(L, k, &x, NULL, 0, st[k % n]); val_free(&x);
      }
    } else for (k = n; k < n + m; k++) line_put(L, k, &v[k % n], NULL, 0, st[k % n]);
  } else {
    /* a mixture, or formulas: repeated, formulas moving with the copy */
    for (k = n; k < n + m; k++) line_put(L, k, &v[k % n], f[k % n], k % n, st[k % n]);
  }
  for (k = 0; k < n; k++) val_free(&v[k]);
  free(v); free(f); free(st);
}
int book_fill(Book *b, int sheet, Rect src, Rect dst)
{
  int m;
  i32 i;
  Line L;
  if (sheet < 0 || sheet >= b->nsh) return 0;
  L.b = b; L.sheet = sheet;
  m = hold(b);
  if (dst.r2 > src.r2 || dst.r1 < src.r1) {
    int down = dst.r2 > src.r2;
    for (i = src.c1; i <= src.c2; i++) {
      L.c0 = i; L.dc = 0;
      L.dr = down ? 1 : -1; L.r0 = down ? src.r1 : src.r2;
      fill_line(&L, src.r2 - src.r1 + 1, down ? dst.r2 - src.r2 : src.r1 - dst.r1);
    }
  } else if (dst.c2 > src.c2 || dst.c1 < src.c1) {
    int right = dst.c2 > src.c2;
    for (i = src.r1; i <= src.r2; i++) {
      L.r0 = i; L.dr = 0;
      L.dc = right ? 1 : -1; L.c0 = right ? src.c1 : src.c2;
      fill_line(&L, src.c2 - src.c1 + 1, right ? dst.c2 - src.c2 : src.c1 - dst.c1);
    }
  }
  release(b, m);
  return 1;
}
int book_series(Book *b, int sheet, Rect a, int across, double step, double stop)
{
  const Value *first;
  double x;
  i32 r, c, k = 0;
  int m;
  if (sheet < 0 || sheet >= b->nsh) return 0;
  first = cell_value(b, sheet, a.r1, a.c1);
  if (first->t != V_NUM) return 0;
  x = first->n;
  m = hold(b);
  for (r = a.r1; r <= a.r2; r++)
    for (c = a.c1; c <= a.c2; c++) {
      i32 i = across ? c - a.c1 : r - a.r1;
      Value v;
      (void)k;
      if (!i) continue;
      v = val_num(x + step * i);
      if (stop == stop && (step >= 0 ? v.n > stop + 1e-9 : v.n < stop - 1e-9)) continue;
      book_set_value(b, sheet, r, c, &v);
      if (across ? r == a.r1 : c == a.c1) {}
    }
  release(b, m);
  return 1;
}

/* ------------------------------------------------------------------ sort and duplicates */
/* ASCII case-insensitive comparison, as strcasecmp */
static int str_icmp(const char *a, const char *b)
{
  for (;; a++, b++) {
    int x = tolower((u8)*a), y = tolower((u8)*b);
    if (x != y || !x) return x - y;
  }
}
/* the order of two values for sorting: empty always last; numbers < text < booleans < errors */
static int sort_rank(const Value *v) { return v->t == V_NUM ? 0 : v->t == V_STR ? 1 : v->t == V_BOOL ? 2 : v->t == V_ERR ? 3 : 4; }
static int sort_cmp(const Value *a, const Value *b, int desc)
{
  int ra = sort_rank(a), rb = sort_rank(b), d;
  if (ra == 4 || rb == 4) return (ra == 4) - (rb == 4);           /* empties last either way */
  if (ra != rb) d = ra < rb ? -1 : 1;
  else if (ra == 0) d = a->n < b->n ? -1 : a->n > b->n;
  else if (ra == 1) d = str_icmp(a->s, b->s);
  else if (ra == 2) d = (int)a->e - (int)b->e;
  else d = (int)a->e - (int)b->e;
  return desc ? -d : d;
}
typedef struct { Value *keys; i32 row; } SortRow;
static const SortKey *s_keys; static int s_nkeys;
static int row_cmp(const SortRow *a, const SortRow *b)
{
  int k, d;
  for (k = 0; k < s_nkeys; k++) if ((d = sort_cmp(&a->keys[k], &b->keys[k], s_keys[k].desc))) return d;
  return a->row < b->row ? -1 : a->row > b->row;
}
/* a stable merge sort of n rows (qsort isn't stable) */
static void merge_sort(SortRow *v, SortRow *tmp, long n)
{
  long h, i, j, k;
  if (n < 2) return;
  h = n / 2;
  merge_sort(v, tmp, h); merge_sort(v + h, tmp, n - h);
  for (i = 0, j = h, k = 0; i < h && j < n;) tmp[k++] = row_cmp(&v[j], &v[i]) < 0 ? v[j++] : v[i++];
  while (i < h) tmp[k++] = v[i++];
  while (j < n) tmp[k++] = v[j++];
  memcpy(v, tmp, (size_t)n * sizeof *v);
}
/* rows of a clip put back in a new order: dest row i takes clip row order[i] (formulas moved) */
static void put_rows(Book *b, int sheet, const Clip *cl, const i32 *order, i32 nrows)
{
  i32 i, j;
  for (i = 0; i < nrows; i++) {
    i32 from = order[i];
    for (j = 0; j < cl->cols; j++) {
      const ClipCell *k = &cl->cells[(long)from * cl->cols + j];
      i32 dr = cl->r0 + i, dc = cl->c0 + j;
      Cell *cell = sheet_cell(b->sh[sheet], dr, dc);
      int st;
      if (!k->has) { if (cell && (cell->f || cell->v.t != V_EMPTY)) { Rect one; one.r1 = one.r2 = dr; one.c1 = one.c2 = dc; book_clear(b, sheet, one, CLR_CONTENTS); } }
      else if (k->f) { Formula *f = formula_copy(k->f); if (f) formula_offset(f, i - from, 0); book_set_formula(b, sheet, dr, dc, f); }
      else book_set_value(b, sheet, dr, dc, &k->v);
      st = clip_style(b, k);
      if (st || sheet_cell(b->sh[sheet], dr, dc)) book_set_style(b, sheet, dr, dc, st);
    }
  }
}
int book_sort(Book *b, int sheet, Rect a, const SortKey *keys, int nkeys, int header)
{
  Clip *cl;
  SortRow *rows, *tmp;
  i32 *order, n, i;
  Value *kv;
  int k, m;
  Sheet *s;
  if (sheet < 0 || sheet >= b->nsh || nkeys < 1) return 0;
  s = b->sh[sheet];
  if (a.r2 > sheet_last_row(s)) a.r2 = sheet_last_row(s);
  if (header) a.r1++;
  if (a.r2 <= a.r1) return 1;
  if (!(cl = clip_copy(b, sheet, a))) return 0;
  a.r2 = a.r1 + cl->rows - 1;
  n = cl->rows;
  rows = (SortRow *)malloc((size_t)n * sizeof *rows); tmp = (SortRow *)malloc((size_t)n * sizeof *tmp);
  order = (i32 *)malloc((size_t)n * sizeof *order); kv = (Value *)calloc((size_t)n * nkeys, sizeof(Value));
  if (!rows || !tmp || !order || !kv) { free(rows); free(tmp); free(order); free(kv); clip_free(cl); return 0; }
  for (i = 0; i < n; i++) {
    rows[i].row = i; rows[i].keys = kv + (long)i * nkeys;
    for (k = 0; k < nkeys; k++) {
      i32 c = keys[k].col - cl->c0;
      rows[i].keys[k] = c >= 0 && c < cl->cols && cl->cells[(long)i * cl->cols + c].has ? cl->cells[(long)i * cl->cols + c].v : val_empty();
    }
  }
  s_keys = keys; s_nkeys = nkeys;
  merge_sort(rows, tmp, n);
  for (i = 0; i < n; i++) order[i] = rows[i].row;
  m = hold(b);
  book_begin(b);
  if (s->nnotes || s->nlinks) {
    /* notes and links in the sorted rows go with them */
    int j;
    book_note_sheet(b, sheet);
    for (j = 0; j < s->nnotes + s->nlinks; j++) {
      i32 *pr = j < s->nnotes ? &s->notes[j].r : &s->links[j - s->nnotes].r, pc = j < s->nnotes ? s->notes[j].c : s->links[j - s->nnotes].c;
      if (*pr < a.r1 || *pr > a.r2 || pc < cl->c0 || pc >= cl->c0 + cl->cols) continue;
      for (i = 0; i < n; i++) if (order[i] == *pr - a.r1) { *pr = a.r1 + i; break; }
    }
  }
  put_rows(b, sheet, cl, order, n);
  book_end(b);
  release(b, m);
  free(rows); free(tmp); free(order); free(kv);
  clip_free(cl);
  return 1;
}
int book_remove_duplicates(Book *b, int sheet, Rect a, u32 cols, int header)
{
  Clip *cl;
  i32 *order, n, keep = 0, i, k, j;
  int m;
  Sheet *s;
  if (sheet < 0 || sheet >= b->nsh) return 0;
  s = b->sh[sheet];
  if (a.r2 > sheet_last_row(s)) a.r2 = sheet_last_row(s);
  if (header) a.r1++;
  if (a.r2 <= a.r1) return 0;
  if (!(cl = clip_copy(b, sheet, a))) return 0;
  n = cl->rows;
  if (!(order = (i32 *)malloc((size_t)n * sizeof *order))) { clip_free(cl); return 0; }
  for (i = 0; i < n; i++) {
    int dup = 0;
    for (k = 0; k < keep && !dup; k++) {
      int same = 1;
      for (j = 0; j < cl->cols && same; j++) {
        const ClipCell *x = &cl->cells[(long)i * cl->cols + j], *y = &cl->cells[(long)order[k] * cl->cols + j];
        Value e = val_empty();
        if (cols && j < 32 && !(cols & (1UL << j))) continue;
        if (sort_cmp(x->has ? &x->v : &e, y->has ? &y->v : &e, 0)) same = 0;
      }
      dup = same;
    }
    if (!dup) order[keep++] = i;
  }
  if (keep < n) {
    Rect gone;
    m = hold(b);
    put_rows(b, sheet, cl, order, keep);
    gone.r1 = cl->r0 + keep; gone.r2 = cl->r0 + n - 1; gone.c1 = cl->c0; gone.c2 = cl->c0 + cl->cols - 1;
    book_clear(b, sheet, gone, CLR_ALL);
    release(b, m);
  }
  free(order);
  clip_free(cl);
  return (int)(n - keep);
}

/* ------------------------------------------------------------------ find and replace */
/* where what is in s from start (case folded unless FIND_CASE; whole cell with FIND_WHOLE), or NULL */
static const char *find_in(const char *s, const char *what, int flags)
{
  size_t n = strlen(what);
  const char *p;
  if (!n) return NULL;
  if (flags & FIND_WHOLE) return (flags & FIND_CASE ? !strcmp(s, what) : str_ieq(s, what)) ? s : NULL;
  for (p = s; *p; p++) if (flags & FIND_CASE ? !strncmp(p, what, n) : !str_ncasecmp(p, what, n)) return p;
  return NULL;
}
/* the text find looks in: the formula (or entry) or what the cell shows */
static int cell_matches(Book *b, int sheet, i32 r, i32 c, const char *what, int flags)
{
  char t[1024];
  Cell *cell = sheet_cell(b->sh[sheet], r, c);
  u32 col;
  if (!cell || (!cell->f && cell->v.t == V_EMPTY)) return 0;
  if (flags & FIND_FORMULAS) book_edit_text(b, sheet, r, c, t, sizeof t);
  else book_shown_text(b, sheet, r, c, t, sizeof t, &col);
  return find_in(t, what, flags) != NULL;
}
/* the occupied cells of a sheet in row-major order, as positions (malloc'd); how many in *n */
static i32 *occupied(Sheet *s, long *n)
{
  long k = 0, cap = 0;
  i32 r, *v = NULL;
  int i;
  for (r = 0; r < s->nrows; r++) if (s->rows[r]) cap += s->rows[r]->n;
  if (!cap || !(v = (i32 *)malloc((size_t)cap * 2 * sizeof(i32)))) { *n = 0; return NULL; }
  for (r = 0; r < s->nrows; r++) if (s->rows[r]) for (i = 0; i < s->rows[r]->n; i++) { v[k * 2] = r; v[k * 2 + 1] = s->rows[r]->c[i].col; k++; }
  *n = k;
  return v;
}
int book_find(Book *b, int *sheet, i32 *r, i32 *c, const char *what, int flags)
{
  int sh = *sheet, k, back = (flags & FIND_BACK) != 0, nsh = flags & FIND_BOOK ? b->nsh : 1;
  /* the start sheet after (before) the cell, the other sheets in turn, then the start sheet up to the cell */
  for (k = 0; k <= nsh; k++) {
    int s = ((sh + (back ? -k : k)) % b->nsh + b->nsh) % b->nsh, part = k == 0 ? 1 : k == nsh ? 2 : 0;
    long n, i;
    i32 *v;
    if (sh < 0 || sh >= b->nsh) return 0;
    v = occupied(b->sh[s], &n);
    for (i = 0; i < n; i++) {
      long j = back ? n - 1 - i : i;
      i32 rr = v[j * 2], cc = v[j * 2 + 1];
      int after = rr > *r || (rr == *r && cc > *c), before = rr < *r || (rr == *r && cc < *c);
      if (part == 1 && !(back ? before : after)) continue;
      if (part == 2 && (back ? before : after)) continue;
      if (cell_matches(b, s, rr, cc, what, flags)) { *sheet = s; *r = rr; *c = cc; free(v); return 1; }
    }
    free(v);
  }
  return 0;
}
int book_replace_cell(Book *b, int sheet, i32 r, i32 c, const char *what, const char *with, int flags)
{
  char t[2048];
  Buf o;
  const char *p, *q;
  int n = 0;
  size_t wl = strlen(what);
  if (!wl) return 0;
  book_edit_text(b, sheet, r, c, t, sizeof t);
  buf_init(&o);
  if (flags & FIND_WHOLE) {
    if (!find_in(t, what, flags)) return 0;
    buf_add(&o, with, strlen(with)); n = 1;
  } else {
    for (p = t; (q = find_in(p, what, flags)); p = q + wl) { buf_add(&o, p, q - p); buf_add(&o, with, strlen(with)); n++; }
    buf_add(&o, p, strlen(p));
  }
  buf_addc(&o, 0);
  if (n && !book_input(b, sheet, r, c, o.data, NULL)) n = 0;     /* a formula broken by the change is left as it was */
  buf_free(&o);
  return n;
}
int book_replace_all(Book *b, int sheet, const char *what, const char *with, int flags)
{
  int s, changed = 0, m = hold(b);
  for (s = 0; s < b->nsh; s++) {
    long n, i;
    i32 *v;
    if (!(flags & FIND_BOOK) && s != sheet) continue;
    v = occupied(b->sh[s], &n);
    for (i = 0; i < n; i++)
      if (cell_matches(b, s, v[i * 2], v[i * 2 + 1], what, flags | FIND_FORMULAS) && book_replace_cell(b, s, v[i * 2], v[i * 2 + 1], what, with, flags)) changed++;
    free(v);
  }
  release(b, m);
  return changed;
}

/* ------------------------------------------------------------------ a cell's text */
void book_edit_text(Book *b, int sheet, i32 r, i32 c, char *out, size_t cap)
{
  Sheet *s;
  Cell *cell;
  const char *code;
  out[0] = 0;
  if (sheet < 0 || sheet >= b->nsh || cap < 2) return;
  s = b->sh[sheet];
  if (!(cell = sheet_cell(s, r, c))) return;
  if (cell->f) {
    Buf t;
    buf_init(&t);
    formula_text(b, sheet, cell->f, &t);
    snprintf(out, cap, "=%s", t.data ? t.data : "");
    buf_free(&t);
    return;
  }
  code = book_fmt_code(b, b->styles[cell->style].numfmt);
  switch (cell->v.t) {
  case V_STR: {
    double n;
    int k;
    /* text that would be read as something else gets an apostrophe */
    if (parse_number(cell->v.s, &n, &k) || str_ieq(cell->v.s, "TRUE") || str_ieq(cell->v.s, "FALSE") || cell->v.s[0] == '=' || cell->v.s[0] == '\'' ||
        (cell->v.s[0] == '#' && err_parse(cell->v.s, NULL))) snprintf(out, cap, "'%s", cell->v.s);
    else snprintf(out, cap, "%s", cell->v.s);
    break; }
  case V_BOOL: snprintf(out, cap, "%s", cell->v.e ? "TRUE" : "FALSE"); break;
  case V_ERR: snprintf(out, cap, "%s", err_name(cell->v.e)); break;
  case V_NUM: {
    double n = cell->v.n;
    if (fmt_is_date(code)) {
      /* dates as dates (day order as set), times as h:mm:ss */
      int y, m, d, has_d = n >= 1, has_t = n != floor(n);
      char dt[24] = "", tm[16] = "";
      if (has_d || !has_t) { serial_ymd(n, &y, &m, &d); snprintf(dt, sizeof dt, "%d/%d/%d", ledger_date_order ? m : d, ledger_date_order ? d : m, y); }
      if (has_t) {
        long secs = (long)floor((n - floor(n)) * 86400.0 + 0.5);
        snprintf(tm, sizeof tm, "%ld:%02ld:%02ld", secs / 3600 % 24, secs / 60 % 60, secs % 60);
      }
      snprintf(out, cap, "%s%s%s", dt, dt[0] && tm[0] ? " " : "", tm);
    } else if (strchr(code, '%') && !strchr(code, '"')) {
      char g[40];
      num_general(n * 100, g, 20);
      snprintf(out, cap, "%s%%", g);
    } else {
      char g[40];
      num_general(n, g, 20);
      snprintf(out, cap, "%s", g);
    }
    break; }
  default: break;
  }
}
void book_shown_text(Book *b, int sheet, i32 r, i32 c, char *out, size_t cap, u32 *color)
{
  const Value *v;
  Cell *cell;
  *color = COLOR_AUTO;
  out[0] = 0;
  if (sheet < 0 || sheet >= b->nsh) return;
  if (!(cell = sheet_cell(b->sh[sheet], r, c))) return;
  v = cell_value(b, sheet, r, c);
  if (v->t == V_EMPTY) return;
  fmt_value(b, book_fmt_code(b, b->styles[cell->style].numfmt), v, out, cap, color, 20);
}

/* ------------------------------------------------------------------ references in a formula */
int formula_ref_spans(Book *b, const char *t, RefSpan *out, int max)
{
  int i = 0, n = 0, q = 0, k, colours = 0;
  if (!t || t[0] != '=') return 0;
  for (i = 1; t[i] && n < max; i++) {
    char c = t[i], prev = t[i - 1];
    u16 sh; Rect r; u8 fl;
    int len;
    if (c == '"') { q = !q; continue; }
    if (q) continue;
    /* a reference starts after an operator, a bracket, a comma or a space */
    if (isalnum((u8)prev) || prev == '_' || prev == '.' || prev == '$' || prev == '!' || prev == ':' || prev == '\'') continue;
    if (!(isalpha((u8)c) || c == '$' || c == '\'')) continue;
    len = parse_ref(b, t + i, &sh, &r, &fl);
    if (len <= 0) continue;
    if (t[i + len] == '(' || isalnum((u8)t[i + len]) || t[i + len] == '_') continue;     /* a function or a longer name */
    out[n].pos = i; out[n].len = len; out[n].sheet = sh; out[n].r = r;
    if (fl & RF_COLS) { out[n].r.r1 = 0; out[n].r.r2 = MAXROW - 1; }
    if (fl & RF_ROWS) { out[n].r.c1 = 0; out[n].r.c2 = MAXCOL - 1; }
    out[n].colour = -1;
    for (k = 0; k < n; k++) if (out[k].len == len && !str_ncasecmp(t + out[k].pos, t + i, (size_t)len)) out[n].colour = out[k].colour;
    if (out[n].colour < 0) out[n].colour = colours++;
    n++;
    i += len - 1;
  }
  return n;
}
