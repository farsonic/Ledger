/*
 * datatools.c - Text to Columns, the Advanced Filter and Consolidate (see data.h), as GridCraft's
 * (crates/engine/src/cmd/data/tools.rs, whatif/advanced_filter.rs, whatif/consolidate.rs) which
 * follow Excel's.
 */
#include "data.h"
#include "calc.h"
#include "ops.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>

/* ------------------------------------------------------------------ Text to Columns */
static int push(char ***parts, int *n, int *cap, const char *s, size_t len)
{
  if (*n >= 1000) return 0;
  if (*n == *cap) {
    int nc = *cap ? *cap * 2 : 8;
    char **np = (char **)realloc(*parts, (size_t)nc * sizeof(char *));
    if (!np) return 0;
    *parts = np; *cap = nc;
  }
  if (!((*parts)[*n] = str_ndup(s, len))) return 0;
  (*n)++;
  return 1;
}
static int is_delim(const TTCOpts *o, char ch)
{
  return ((o->delims & TTC_TAB) && ch == '\t') || ((o->delims & TTC_SEMI) && ch == ';') || ((o->delims & TTC_COMMA) && ch == ',') ||
         ((o->delims & TTC_SPACE) && ch == ' ') || ((o->delims & TTC_OTHER) && o->other && ch == o->other);
}
int ttc_split(const char *text, const TTCOpts *o, char ***parts)
{
  int n = 0, cap = 0;
  *parts = NULL;
  if (o->fixed) {
    /* break positions in characters; each part trimmed of spaces */
    size_t len = strlen(text), at = 0;
    long ch = 0;
    int k = 0;
    size_t start = 0;
    while (1) {
      int cut = at >= len || (k < o->nbreaks && ch == o->breaks[k]);
      if (cut) {
        size_t a = start, z = at;
        while (a < z && text[a] == ' ') a++;
        while (z > a && text[z - 1] == ' ') z--;
        if (!push(parts, &n, &cap, text + a, z - a)) break;
        start = at;
        while (k < o->nbreaks && o->breaks[k] <= ch) k++;
        if (at >= len) break;
        continue;
      }
      at = utf8_next(text, len, at);
      ch++;
    }
    return n;
  } else {
    Buf cur;
    int inq = 0, last = 0;
    const char *p;
    buf_init(&cur);
    for (p = text; *p; p++) {
      if (o->quote && *p == o->quote) {
        if (inq && p[1] == o->quote) { buf_addc(&cur, o->quote); p++; }
        else inq = !inq;
        last = 0;
        continue;
      }
      if (!inq && is_delim(o, *p)) {
        if (o->consecutive && last) continue;
        push(parts, &n, &cap, cur.data ? cur.data : "", cur.len);
        cur.len = 0; if (cur.data) cur.data[0] = 0;
        last = 1;
        continue;
      }
      last = 0;
      buf_addc(&cur, *p);
    }
    push(parts, &n, &cap, cur.data ? cur.data : "", cur.len);
    buf_free(&cur);
    return n;
  }
}
int book_text_to_columns(Book *b, int sheet, Rect src, const TTCOpts *o, i32 dr, i32 dc)
{
  i32 r, last;
  int most = 0, manual;
  if (sheet < 0 || sheet >= b->nsh) return -1;
  last = sheet_last_row(b->sh[sheet]);
  if (src.r2 > last) src.r2 = last;
  if (src.r2 < src.r1) return 0;
  manual = b->manualcalc;
  b->manualcalc = 1;
  book_begin(b);
  for (r = src.r1; r <= src.r2; r++) {
    char t[2048], **parts;
    u32 col;
    int n, j;
    const Value *v = cell_value(b, sheet, r, src.c1);
    if (v->t == V_EMPTY) continue;
    if (v->t == V_STR) { strncpy(t, v->s, sizeof t - 1); t[sizeof t - 1] = 0; }
    else book_shown_text(b, sheet, r, src.c1, t, sizeof t, &col);
    n = ttc_split(t, o, &parts);
    for (j = 0; j < n; j++) {
      if (dc + j < MAXCOL && dr + (r - src.r1) < MAXROW) book_input(b, sheet, dr + (r - src.r1), dc + j, parts[j], NULL);
      free(parts[j]);
    }
    free(parts);
    if (n > most) most = n;
  }
  book_end(b);
  b->manualcalc = manual;
  if (!manual) book_recalc(b);
  return most;
}

/* ------------------------------------------------------------------ the Advanced Filter */
/* one criterion cell against a value: the cell's text as Excel reads it in a criteria range (text
   alone means "begins with", =text means exactly, operators as COUNTIF's) */
static int crit_ok(Book *b, int sheet, const Value *cv, const Value *v)
{
  Ctx cx;
  Crit c;
  Value arg;
  int ok;
  cx.b = b; cx.sheet = sheet; cx.row = 0; cx.col = 0; cx.depth = 0;
  if (cv->t == V_EMPTY) return 1;
  if (cv->t == V_STR) {
    const char *s = cv->s;
    char buf[600];
    if (!*s) return 1;
    if (strchr("<>=", *s)) arg = val_str(s);
    else { snprintf(buf, sizeof buf, "%s*", s); arg = val_str(buf); }
  } else arg = val_copy(cv);
  if (crit_make(&cx, &arg, &c)) { crit_free(&c); val_free(&arg); return 0; }
  ok = crit_match(&c, v);
  crit_free(&c);
  val_free(&arg);
  return ok;
}
/* whether data row r of list passes the criteria range */
static int row_passes(Book *b, int sheet, Rect list, Rect crit, int csheet, i32 r)
{
  i32 cr, cc;
  for (cr = crit.r1 + 1; cr <= crit.r2; cr++) {
    int ok = 1, any = 0;
    for (cc = crit.c1; cc <= crit.c2 && ok; cc++) {
      const Value *head = cell_value(b, csheet, crit.r1, cc), *cv = cell_value(b, csheet, cr, cc);
      Cell *ccell = sheet_cell(b->sh[csheet], cr, cc);
      i32 lc;
      int found = -1;
      if (cv->t == V_EMPTY && !(ccell && ccell->f)) continue;
      any = 1;
      if (head->t == V_STR && head->s[0])
        for (lc = list.c1; lc <= list.c2 && found < 0; lc++) {
          const Value *lh = cell_value(b, sheet, list.r1, lc);
          char t[256];
          u32 colr;
          book_shown_text(b, sheet, list.r1, lc, t, sizeof t, &colr);
          if ((lh->t == V_STR && str_ieq(lh->s, head->s)) || str_ieq(t, head->s)) found = lc;
        }
      if (found >= 0) ok = crit_ok(b, sheet, cv, cell_value(b, sheet, r, found));
      else if (ccell && ccell->f) {
        /* a computed criterion: its formula written for the first data row, seen from row r */
        Formula *f = formula_copy(ccell->f);
        Value res;
        if (!f) { ok = 0; break; }
        formula_offset(f, r - (list.r1 + 1), 0);
        res = book_eval_at(b, sheet, r, list.c1, f);
        ok = (res.t == V_BOOL && res.e) || (res.t == V_NUM && res.n != 0);
        val_free(&res);
        formula_free(f);
      } else ok = 0;                       /* a heading that isn't the list's matches nothing */
    }
    if (ok || !any) return 1;
  }
  return crit.r2 <= crit.r1;
}
/* rows r and k of the list hold the same (as shown, ignoring case) */
static int same_row(Book *b, int sheet, Rect list, i32 r, i32 k)
{
  i32 c;
  for (c = list.c1; c <= list.c2; c++) {
    char t1[256], t2[256];
    u32 col;
    book_shown_text(b, sheet, r, c, t1, sizeof t1, &col);
    book_shown_text(b, sheet, k, c, t2, sizeof t2, &col);
    if (!str_ieq(t1, t2)) return 0;
  }
  return 1;
}
int book_advanced_filter(Book *b, int sheet, Rect list, const Rect *crit, int csh, i32 cr, i32 cc, int unique)
{
  Sheet *s;
  i32 r, *keep = NULL, last;
  int nk = 0, k, manual;
  if (sheet < 0 || sheet >= b->nsh || list.r2 <= list.r1) return -1;
  s = b->sh[sheet];
  last = sheet_last_row(s);
  if (list.r2 > last) list.r2 = last;
  if (!(keep = (i32 *)malloc((size_t)(list.r2 - list.r1 + 1) * sizeof(i32)))) return -1;
  for (r = list.r1 + 1; r <= list.r2; r++) {
    int ok = !crit || row_passes(b, sheet, list, *crit, sheet, r);
    if (ok && unique) for (k = 0; k < nk && ok; k++) if (same_row(b, sheet, list, keep[k], r)) ok = 0;
    if (ok) keep[nk++] = r;
  }
  manual = b->manualcalc;
  b->manualcalc = 1;
  book_begin(b);
  if (csh < 0) {
    /* in place: the rest hidden (as a filter hides them) */
    book_note_sheet(b, sheet);
    for (r = list.r1 + 1, k = 0; r <= list.r2; r++) {
      int show = k < nk && keep[k] == r;
      Row *row = show ? sheet_row(s, r, 0) : sheet_row(s, r, 1);
      if (show) k++;
      if (row) row->filtered = (u8)!show;
    }
  } else if (csh < b->nsh) {
    /* copied: the headings, then the rows that passed, as values and looks */
    Clip *cl;
    Rect one;
    i32 out = cr;
    one = list; one.r2 = list.r1;
    if ((cl = clip_copy(b, sheet, one))) { book_paste(b, csh, out++, cc, cl, PASTE_VALUES, 0, NULL); book_paste(b, csh, out - 1, cc, cl, PASTE_FORMATS, 0, NULL); clip_free(cl); }
    for (k = 0; k < nk && out < MAXROW; k++) {
      one.r1 = one.r2 = keep[k];
      if ((cl = clip_copy(b, sheet, one))) { book_paste(b, csh, out, cc, cl, PASTE_VALUES, 0, NULL); book_paste(b, csh, out, cc, cl, PASTE_FORMATS, 0, NULL); clip_free(cl); }
      out++;
    }
  }
  book_end(b);
  b->manualcalc = manual;
  b->dirty = 1; b->changes++;
  if (!manual) book_recalc(b);
  free(keep);
  return nk;
}
int book_filter_show_all(Book *b, int sheet)
{
  Sheet *s;
  i32 r;
  int n = 0;
  if (sheet < 0 || sheet >= b->nsh) return 0;
  s = b->sh[sheet];
  for (r = 0; r < s->nrows; r++) if (s->rows[r] && s->rows[r]->filtered) n++;
  if (!n) return 0;
  book_begin(b);
  book_note_sheet(b, sheet);
  for (r = 0; r < s->nrows; r++) if (s->rows[r]) s->rows[r]->filtered = 0;
  book_end(b);
  b->dirty = 1; b->changes++;
  if (!b->manualcalc) book_recalc(b);
  return n;
}

/* ------------------------------------------------------------------ Consolidate */
typedef struct { long count, nums; double sum, prod, mn, mx; } Agg;
static void agg_add(Agg *a, const Value *v)
{
  if (v->t == V_EMPTY) return;
  a->count++;
  if (v->t != V_NUM) return;
  if (!a->nums) { a->mn = a->mx = v->n; a->prod = 1; }
  a->nums++; a->sum += v->n; a->prod *= v->n;
  if (v->n < a->mn) a->mn = v->n;
  if (v->n > a->mx) a->mx = v->n;
}
static Value agg_result(const Agg *a, int fn)
{
  if (!a->count) return val_empty();
  switch (fn) {
  case 1: return a->nums ? val_num(a->sum / a->nums) : val_err(E_DIV0);
  case 2: return val_num((double)a->nums);
  case 3: return val_num((double)a->count);
  case 4: return val_num(a->nums ? a->mx : 0);
  case 5: return val_num(a->nums ? a->mn : 0);
  case 6: return val_num(a->nums ? a->prod : 0);
  }
  return val_num(a->sum);
}
#define MAXKEYS 2000
typedef struct { char *label; i32 pos; } Key;      /* a label (lower case) or a position */
static int key_index(Key *keys, int *n, const char *label, i32 pos)
{
  int i;
  for (i = 0; i < *n; i++) if (label ? keys[i].label && str_ieq(keys[i].label, label) : !keys[i].label && keys[i].pos == pos) return i;
  if (*n >= MAXKEYS) return -1;
  keys[*n].label = label ? str_dup(label) : NULL; keys[*n].pos = pos;
  return (*n)++;
}
int book_consolidate(Book *b, int ds, i32 r, i32 c, const int *srcsh, const Rect *src, int n, int fn, int toprow, int leftcol)
{
  Key *rk, *ck;
  int nr = 0, nc = 0, i, j, manual;
  Agg **ag;
  int *cmap;
  if (ds < 0 || ds >= b->nsh || n <= 0) return 0;
  rk = (Key *)calloc(MAXKEYS, sizeof(Key)); ck = (Key *)calloc(MAXKEYS, sizeof(Key));
  ag = (Agg **)calloc(MAXKEYS, sizeof(Agg *));        /* (a row of 64 made when first used) */
  cmap = (int *)malloc(MAXCOL * sizeof(int));
  if (!rk || !ck || !ag || !cmap) { free(rk); free(ck); free(ag); free(cmap); return 0; }
  for (i = 0; i < n; i++) {
    int sh = srcsh[i];
    Rect a = src[i];
    i32 rr, cc, r0, c0;
    char t[256];
    u32 colr;
    if (sh < 0 || sh >= b->nsh) continue;
    if (a.r2 > sheet_last_row(b->sh[sh])) a.r2 = sheet_last_row(b->sh[sh]);
    if (a.c2 > sheet_last_col(b->sh[sh])) a.c2 = sheet_last_col(b->sh[sh]);
    r0 = a.r1 + (toprow ? 1 : 0); c0 = a.c1 + (leftcol ? 1 : 0);
    if (r0 > a.r2 || c0 > a.c2) continue;
    for (cc = c0; cc <= a.c2; cc++) {
      cmap[cc] = -1;
      if (toprow) {
        book_shown_text(b, sh, a.r1, cc, t, sizeof t, &colr);
        if (t[0]) cmap[cc] = key_index(ck, &nc, t, 0);
      } else cmap[cc] = key_index(ck, &nc, NULL, cc - c0);
      if (cmap[cc] >= 64) cmap[cc] = -1;           /* (64 columns at most) */
    }
    for (rr = r0; rr <= a.r2; rr++) {
      int ri;
      if (leftcol) {
        book_shown_text(b, sh, rr, a.c1, t, sizeof t, &colr);
        if (!t[0]) continue;
        ri = key_index(rk, &nr, t, 0);
      } else ri = key_index(rk, &nr, NULL, rr - r0);
      if (ri < 0 || (!ag[ri] && !(ag[ri] = (Agg *)calloc(64, sizeof(Agg))))) continue;
      for (cc = c0; cc <= a.c2; cc++) if (cmap[cc] >= 0) agg_add(&ag[ri][cmap[cc]], cell_value(b, sh, rr, cc));
    }
  }
  manual = b->manualcalc;
  b->manualcalc = 1;
  book_begin(b);
  {
    i32 or0 = r + (toprow ? 1 : 0), oc0 = c + (leftcol ? 1 : 0);
    if (toprow) for (j = 0; j < nc; j++) if (ck[j].label && oc0 + j < MAXCOL) { Value v = val_str(ck[j].label); book_set_value(b, ds, r, oc0 + j, &v); val_free(&v); }
    if (leftcol) for (i = 0; i < nr; i++) if (rk[i].label && or0 + i < MAXROW) { Value v = val_str(rk[i].label); book_set_value(b, ds, or0 + i, c, &v); val_free(&v); }
    for (i = 0; i < nr; i++)
      for (j = 0; j < nc && j < 64; j++) {
        Agg none;
        Value v;
        memset(&none, 0, sizeof none);
        v = agg_result(ag[i] ? &ag[i][j] : &none, fn);
        if (or0 + i >= MAXROW || oc0 + j >= MAXCOL) {}
        else if (v.t != V_EMPTY) book_set_value(b, ds, or0 + i, oc0 + j, &v);
        else if (sheet_cell(b->sh[ds], or0 + i, oc0 + j)) { Rect one; one.r1 = one.r2 = or0 + i; one.c1 = one.c2 = oc0 + j; book_clear(b, ds, one, CLR_CONTENTS); }
        val_free(&v);
      }
  }
  book_end(b);
  b->manualcalc = manual;
  if (!manual) book_recalc(b);
  for (i = 0; i < nr; i++) { free(rk[i].label); free(ag[i]); }
  for (j = 0; j < nc; j++) free(ck[j].label);
  free(rk); free(ck); free(ag); free(cmap);
  return nr * nc;
}
