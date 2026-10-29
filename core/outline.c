/*
 * outline.c - the outline (see data.h): rows or columns grouped at up to seven levels, the groups'
 * detail hidden and shown, the level buttons, Subtotal and Remove All, and the outline in .xlsx
 * (outlineLevel and collapsed on rows and columns, outlinePr's summaryBelow/summaryRight).
 *
 * As GridCraft's outline (crates/engine/src/outline.rs, cmd/data/grouping.rs), which follows
 * Excel's: a group is a run of lines at its level or deeper; its summary line sits just after the
 * run (before it when the sheet keeps summaries above or left) and carries the collapsed mark.
 */
#include "data.h"
#include "calc.h"
#include "ops.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

static i32 nlines(const Sheet *s, int rows) { return rows ? s->nrows : s->ncols; }
int outline_level(const Sheet *s, int rows, i32 i)
{
  if (rows) return i >= 0 && i < s->nrows && s->rows[i] ? s->rows[i]->outline : 0;
  return i >= 0 && i < s->ncols ? s->cols[i].outline : 0;
}
static int line_hidden(const Sheet *s, int rows, i32 i)
{
  if (rows) return i >= 0 && i < s->nrows && s->rows[i] ? s->rows[i]->hidden : 0;
  return i >= 0 && i < s->ncols ? s->cols[i].hidden : 0;
}
static u8 *collapsed_of(Sheet *s, int rows, i32 i, int make)
{
  if (rows) { Row *r = sheet_row(s, i, make); return r ? &r->collapsed : NULL; }
  else { ColInfo *c = sheet_col(s, i, make); return c ? &c->collapsed : NULL; }
}
static void set_hidden(Sheet *s, int rows, i32 i, int hide)
{
  if (rows) {
    Row *r = hide ? sheet_row(s, i, 1) : sheet_row(s, i, 0);
    if (r) r->hidden = (u8)hide;
  } else {
    ColInfo *c = hide ? sheet_col(s, i, 1) : sheet_col(s, i, 0);
    if (c) { c->hidden = (u8)hide; if (!hide && c->custom && c->width == 0) c->width = s->defwidth; }
  }
}
int outline_max_level(const Sheet *s, int rows)
{
  i32 i, n = nlines(s, rows);
  int m = 0;
  for (i = 0; i < n; i++) { int l = outline_level(s, rows, i); if (l > m) m = l; }
  return m > OUTLINE_MAX ? OUTLINE_MAX : m;
}
i32 ogroup_summary(const OGroup *g)
{
  if (g->before) return g->start > 0 ? g->start - 1 : -1;
  return g->end + 1;
}
int outline_groups(const Sheet *s, int rows, OGroup *out, int max)
{
  i32 start[OUTLINE_MAX + 1], i, n = nlines(s, rows), prev = -1;
  u8 open[OUTLINE_MAX + 1], hid[OUTLINE_MAX + 1];
  int k = 0, L, before = rows ? s->sumabove : s->sumleft;
  memset(open, 0, sizeof open);
#define CLOSE(from_, end_) do { int l_; for (l_ = OUTLINE_MAX; l_ >= (from_); l_--) if (open[l_]) { \
      if (k < max) { out[k].level = (u8)l_; out[k].start = start[l_]; out[k].end = (end_); out[k].collapsed = hid[l_]; out[k].before = (u8)before; k++; } \
      open[l_] = 0; } } while (0)
  for (i = 0; i < n; i++) {
    int lvl = outline_level(s, rows, i), h;
    if (!lvl) continue;
    if (lvl > OUTLINE_MAX) lvl = OUTLINE_MAX;
    if (prev >= 0 && prev + 1 != i) CLOSE(1, prev);
    else if (prev >= 0 && lvl < OUTLINE_MAX) CLOSE(lvl + 1, prev);
    h = line_hidden(s, rows, i);
    for (L = 1; L <= lvl; L++) {
      if (open[L]) hid[L] = (u8)(hid[L] & h);
      else { open[L] = 1; start[L] = i; hid[L] = (u8)h; }
    }
    prev = i;
  }
  if (prev >= 0) CLOSE(1, prev);
#undef CLOSE
  return k;
}
/* the end of an outline change: an undo step closed, the sums worked out again (SUBTOTAL looks at
   hidden rows) */
static void done(Book *b)
{
  book_end(b);
  b->dirty = 1;
  b->changes++;
  if (!b->manualcalc) book_recalc(b);
}
static int begin(Book *b, int sheet)
{
  if (sheet < 0 || sheet >= b->nsh) return 0;
  book_begin(b);
  book_note_sheet(b, sheet);
  return 1;
}
#define MAXG 4096
int book_group(Book *b, int sheet, int rows, i32 a, i32 c, int on)
{
  Sheet *s;
  i32 i, lim = rows ? MAXROW : MAXCOL;
  OGroup *g;
  int ng, k, ends = 0;
  if (a > c) { i32 t = a; a = c; c = t; }
  if (a < 0 || c >= lim || !begin(b, sheet)) return 0;
  s = b->sh[sheet];
  if (c - a > 100000) c = a + 100000;
  for (i = a; i <= c; i++) {
    u8 *lv;
    if (rows) { Row *r = on ? sheet_row(s, i, 1) : sheet_row(s, i, 0); lv = r ? &r->outline : NULL; }
    else { ColInfo *ci = on ? sheet_col(s, i, 1) : sheet_col(s, i, 0); lv = ci ? &ci->outline : NULL; }
    if (!lv) continue;
    if (on) { if (*lv < OUTLINE_MAX) (*lv)++; }
    else if (*lv) { (*lv)--; if (!*lv) set_hidden(s, rows, i, 0); }
  }
  /* ungrouped: a summary line whose group is gone has nothing left to collapse */
  if (!on && (g = (OGroup *)malloc(MAXG * sizeof(OGroup)))) {
    int before = rows ? s->sumabove : s->sumleft;
    ng = outline_groups(s, rows, g, MAXG);
    for (k = 0; k < ng; k++) if (before ? g[k].start == a : g[k].end == c) ends = 1;
    if (!ends) { u8 *cp = collapsed_of(s, rows, before ? a - 1 : c + 1, 0); if (cp) *cp = 0; }
    free(g);
  }
  done(b);
  return 1;
}
int book_detail(Book *b, int sheet, int rows, i32 a, i32 c, int hide, int level)
{
  Sheet *s;
  OGroup *g;
  int ng, k, j, deepest = 0, pass, any = 0;
  u8 *pick;
  if (a > c) { i32 t = a; a = c; c = t; }
  if (sheet < 0 || sheet >= b->nsh || !(g = (OGroup *)malloc(MAXG * sizeof(OGroup)))) return 0;
  s = b->sh[sheet];
  ng = outline_groups(s, rows, g, MAXG);
  if (!(pick = (u8 *)calloc((size_t)ng + 1, 1))) { free(g); return 0; }
  /* the groups holding the lines; failing that, the ones they summarise: the deepest of them */
  for (pass = 0; pass < 2 && !any; pass++) {
    deepest = 0;
    for (k = 0; k < ng; k++) {
      i32 sm = ogroup_summary(&g[k]);
      int hit = pass == 0 ? g[k].start <= c && g[k].end >= a : sm >= 0 && sm >= a && sm <= c;
      if (hit && (level <= 0 || g[k].level == level) && g[k].level > deepest) deepest = g[k].level;
    }
    for (k = 0; k < ng; k++) {
      i32 sm = ogroup_summary(&g[k]);
      int hit = pass == 0 ? g[k].start <= c && g[k].end >= a : sm >= 0 && sm >= a && sm <= c;
      if (hit && g[k].level == deepest && deepest) { pick[k] = 1; any = 1; }
    }
  }
  if (!any) { free(pick); free(g); return 0; }
  begin(b, sheet);
  for (k = 0; k < ng; k++) {
    i32 i, sm = ogroup_summary(&g[k]);
    if (!pick[k]) continue;
    for (i = g[k].start; i <= g[k].end; i++) {
      int h = hide;
      /* inner groups collapsed on their own stay collapsed when this one is shown */
      if (!h)
        for (j = 0; j < ng; j++) {
          i32 sj = ogroup_summary(&g[j]);
          u8 *cp;
          if (g[j].level > g[k].level && g[j].start >= g[k].start && g[j].end <= g[k].end && sj >= 0 && sj != sm &&
              i >= g[j].start && i <= g[j].end && (cp = collapsed_of(s, rows, sj, 0)) && *cp) { h = 1; break; }
        }
      set_hidden(s, rows, i, h);
    }
    if (sm >= 0 && sm < (rows ? MAXROW : MAXCOL)) {
      u8 *cp = collapsed_of(s, rows, sm, hide);
      if (cp) *cp = (u8)hide;
    }
  }
  free(pick); free(g);
  done(b);
  return 1;
}
int book_show_levels(Book *b, int sheet, int rows, int n)
{
  Sheet *s;
  OGroup *g;
  int ng, k;
  i32 i, nl;
  if (sheet < 0 || sheet >= b->nsh || !(g = (OGroup *)malloc(MAXG * sizeof(OGroup)))) return 0;
  s = b->sh[sheet];
  ng = outline_groups(s, rows, g, MAXG);
  begin(b, sheet);
  nl = nlines(s, rows);
  for (i = 0; i < nl; i++) { int l = outline_level(s, rows, i); if (l) set_hidden(s, rows, i, l >= n); }
  for (k = 0; k < ng; k++) {
    i32 sm = ogroup_summary(&g[k]);
    u8 *cp;
    if (sm >= 0 && sm < (rows ? MAXROW : MAXCOL) && (cp = collapsed_of(s, rows, sm, g[k].level >= n))) *cp = (u8)(g[k].level >= n);
  }
  free(g);
  done(b);
  return 1;
}
int book_outline_clear(Book *b, int sheet)
{
  Sheet *s;
  i32 i;
  if (!begin(b, sheet)) return 0;
  s = b->sh[sheet];
  for (i = 0; i < s->nrows; i++) if (s->rows[i] && (s->rows[i]->outline || s->rows[i]->collapsed)) {
    if (s->rows[i]->outline) s->rows[i]->hidden = 0;
    s->rows[i]->outline = 0; s->rows[i]->collapsed = 0;
  }
  for (i = 0; i < s->ncols; i++) if (s->cols[i].outline || s->cols[i].collapsed) {
    if (s->cols[i].outline) set_hidden(s, 0, i, 0);
    s->cols[i].outline = 0; s->cols[i].collapsed = 0;
  }
  done(b);
  return 1;
}
int book_outline_settings(Book *b, int sheet, int below, int right)
{
  Sheet *s;
  OGroup *g;
  int axis, ng, k;
  if (!begin(b, sheet)) return 0;
  s = b->sh[sheet];
  if ((g = (OGroup *)malloc(MAXG * sizeof(OGroup)))) {
    for (axis = 0; axis < 2; axis++) {
      int rows = axis == 0, want = rows ? !below : !right, was = rows ? s->sumabove : s->sumleft;
      if (want == was) continue;
      /* the collapsed marks move to the summary lines on the new side */
      ng = outline_groups(s, rows, g, MAXG);
      for (k = 0; k < ng; k++) { i32 sm = ogroup_summary(&g[k]); u8 *cp; g[k].collapsed = 0; if (sm >= 0 && (cp = collapsed_of(s, rows, sm, 0)) && *cp) { g[k].collapsed = 1; *cp = 0; } }
      if (rows) s->sumabove = (u8)want; else s->sumleft = (u8)want;
      for (k = 0; k < ng; k++) {
        i32 sm;
        u8 *cp;
        if (!g[k].collapsed) continue;
        g[k].before = (u8)want;
        sm = ogroup_summary(&g[k]);
        if (sm >= 0 && sm < (rows ? MAXROW : MAXCOL) && (cp = collapsed_of(s, rows, sm, 1))) *cp = 1;
      }
    }
    free(g);
  }
  done(b);
  return 1;
}

/* ------------------------------------------------------------------ Subtotal */
const char *subtotal_label(int fn)
{
  static const char *const names[] = { "Total", "Average", "Count", "Count", "Max", "Min", "Product", "StdDev", "StdDevp", "Total", "Var", "Varp" };
  return fn >= 1 && fn <= 11 ? names[fn] : "Total";
}
/* whether a formula calls SUBTOTAL */
static int has_subtotal(const Formula *f)
{
  static int id = -2;
  int i;
  if (id == -2) id = func_lookup("SUBTOTAL");
  if (!f) return 0;
  for (i = 0; i < f->n; i++)
    if (f->t[i].op == T_FUNC && ((id >= 0 && f->t[i].a == id) || (f->t[i].a == 0xFFFF && f->t[i].s && str_ieq(f->t[i].s, "SUBTOTAL")))) return 1;
  return 0;
}
static int subtotal_row(Sheet *s, i32 r, i32 c1, i32 c2)
{
  Row *row = r >= 0 && r < s->nrows ? s->rows[r] : NULL;
  int i;
  if (!row) return 0;
  for (i = 0; i < row->n; i++) if (row->c[i].col >= c1 && row->c[i].col <= c2 && has_subtotal(row->c[i].f)) return 1;
  return 0;
}
static void bold_on(Style *st, Font *f, const void *arg) { (void)st; (void)arg; f->bold = 1; }
/* a subtotal row written at r: its label in the group column, SUBTOTAL over r1..r2 in each column */
static void write_total(Book *b, int sheet, Rect area, i32 r, i32 gc, const char *label, int fn, const i32 *cols, int ncols, i32 r1, i32 r2, i32 stylerow)
{
  char t[600], a1[16], a2[16];
  int k;
  Rect line;
  i32 c;
  Sheet *s = b->sh[sheet];
  /* the look of the row above (number formats), then bold */
  for (c = area.c1; c <= area.c2; c++) { Cell *src = sheet_cell(s, stylerow, c); if (src && src->style) book_set_style(b, sheet, r, c, src->style); }
  snprintf(t, sizeof t, "'%s", label);
  book_input(b, sheet, r, gc, t, NULL);
  for (k = 0; k < ncols; k++) {
    if (cols[k] == gc) continue;
    ref_text(r1, cols[k], 0, 0, a1); ref_text(r2, cols[k], 0, 0, a2);
    snprintf(t, sizeof t, "=SUBTOTAL(%d,%s:%s)", fn, a1, a2);
    book_input(b, sheet, r, cols[k], t, NULL);
  }
  line = area; line.r1 = line.r2 = r;
  book_restyle(b, sheet, line, bold_on, NULL);
}
int book_subtotal(Book *b, int sheet, Rect area, i32 gc, int fn, const i32 *cols, int ncols, int replace, int below)
{
  Sheet *s;
  i32 first, r, *gs = NULL, *ge = NULL, added = 0, manual;
  char **gname = NULL;
  int ng = 0, cap = 0, k, i;
  char prev[512], cur[512], label[600];
  u32 colr;
  if (sheet < 0 || sheet >= b->nsh || area.r2 <= area.r1 || gc < area.c1 || gc > area.c2 || ncols <= 0) return -1;
  s = b->sh[sheet];
  if (area.r2 > sheet_last_row(s)) area.r2 = sheet_last_row(s);
  if (area.r2 <= area.r1) return -1;
  manual = b->manualcalc;
  b->manualcalc = 1;                     /* (one recalculation at the end, not one per cell) */
  book_begin(b);
  book_note_book(b);
  if (replace) area.r2 -= book_subtotal_remove(b, sheet, area);
  first = area.r1 + 1;
  /* the groups: runs of equal shown text in the group column, broken by subtotal rows already there */
  prev[0] = 0;
  for (r = first; r <= area.r2 + 1; r++) {
    int boundary = r > area.r2 || subtotal_row(s, r, area.c1, area.c2);
    if (!boundary) book_shown_text(b, sheet, r, gc, cur, sizeof cur, &colr);
    if (ng && ge[ng - 1] < 0 && (boundary || strcmp(cur, prev))) ge[ng - 1] = r - 1;
    if (boundary) continue;
    if (!ng || ge[ng - 1] >= 0) {
      if (ng == cap) {
        int nc = cap ? cap * 2 : 64;
        i32 *a = (i32 *)realloc(gs, (size_t)nc * sizeof(i32)), *z;
        char **nn;
        if (a) gs = a;
        z = (i32 *)realloc(ge, (size_t)nc * sizeof(i32));
        if (z) ge = z;
        nn = (char **)realloc(gname, (size_t)nc * sizeof(char *));
        if (nn) gname = nn;
        if (!a || !z || !nn) break;
        cap = nc;
      }
      gs[ng] = r; ge[ng] = -1; gname[ng] = str_dup(cur); ng++;
      strcpy(prev, cur);
    }
  }
  if (ng && ge[ng - 1] < 0) ge[ng - 1] = area.r2;
  if (below) {
    if (s->sumabove) book_outline_settings(b, sheet, 1, !s->sumleft);
    for (k = ng - 1; k >= 0; k--) {
      i32 at = ge[k] + 1;
      book_insert_rows(b, sheet, at, 1);
      snprintf(label, sizeof label, "%s %s", gname[k] ? gname[k] : "", subtotal_label(fn));
      write_total(b, sheet, area, at, gc, label, fn, cols, ncols, gs[k], ge[k], ge[k]);
      book_group(b, sheet, 1, gs[k], ge[k], 1);
      added++;
    }
    if (ng) {
      i32 last = area.r2 + added, at = last + 1;
      book_insert_rows(b, sheet, at, 1);
      snprintf(label, sizeof label, "Grand %s", subtotal_label(fn));
      write_total(b, sheet, area, at, gc, label, fn, cols, ncols, first, last, last);
      book_group(b, sheet, 1, first, last, 1);
      added++;
    }
  } else {
    if (!s->sumabove) book_outline_settings(b, sheet, 0, !s->sumleft);
    for (k = ng - 1; k >= 0; k--) {
      i32 at = gs[k];
      book_insert_rows(b, sheet, at, 1);
      snprintf(label, sizeof label, "%s %s", gname[k] ? gname[k] : "", subtotal_label(fn));
      write_total(b, sheet, area, at, gc, label, fn, cols, ncols, gs[k] + 1, ge[k] + 1, gs[k] + 1);
      book_group(b, sheet, 1, gs[k] + 1, ge[k] + 1, 1);
      added++;
    }
    if (ng) {
      i32 last = area.r2 + added;
      book_insert_rows(b, sheet, first, 1);
      snprintf(label, sizeof label, "Grand %s", subtotal_label(fn));
      write_total(b, sheet, area, first, gc, label, fn, cols, ncols, first + 1, last + 1, first + 1);
      book_group(b, sheet, 1, first + 1, last + 1, 1);
      added++;
    }
  }
  for (i = 0; i < ng; i++) free(gname[i]);
  free(gname); free(gs); free(ge);
  b->manualcalc = manual;
  done(b);
  return added;
}
int book_subtotal_remove(Book *b, int sheet, Rect area)
{
  Sheet *s;
  i32 r, gone = 0, last;
  if (sheet < 0 || sheet >= b->nsh) return 0;
  s = b->sh[sheet];
  last = sheet_last_row(s);
  if (area.r2 > last) area.r2 = last;
  book_begin(b);
  book_note_book(b);
  for (r = area.r2; r >= area.r1; r--)
    if (subtotal_row(s, r, area.c1, area.c2)) { book_delete_rows(b, sheet, r, 1); gone++; }
  /* the outline of what's left */
  for (r = area.r1; r <= area.r2 - gone && r < s->nrows; r++)
    if (s->rows[r] && (s->rows[r]->outline || s->rows[r]->collapsed)) { s->rows[r]->outline = 0; s->rows[r]->collapsed = 0; s->rows[r]->hidden = 0; }
  if (area.r2 - gone + 1 < s->nrows && s->rows[area.r2 - gone + 1]) s->rows[area.r2 - gone + 1]->collapsed = 0;
  done(b);
  return gone;
}

/* ------------------------------------------------------------------ .xlsx */
void outline_xlsx_row_attrs(u8 level, u8 collapsed, Buf *o)
{
  if (level) buf_printf(o, " outlineLevel=\"%d\"", level > OUTLINE_MAX ? OUTLINE_MAX : level);
  if (collapsed) buf_adds(o, " collapsed=\"1\"");
}
void outline_xlsx_read_line(Xml *x, u8 *level, u8 *collapsed)
{
  const char *v;
  if ((v = xml_attr(x, "outlineLevel"))) { int l = atoi(v); *level = (u8)(l < 0 ? 0 : l > OUTLINE_MAX ? OUTLINE_MAX : l); }
  if ((v = xml_attr(x, "collapsed"))) *collapsed = (u8)(!strcmp(v, "1") || !strcmp(v, "true"));
}
void outline_xlsx_sheetpr(const Sheet *s, Buf *o)
{
  if (s->sumabove || s->sumleft) buf_printf(o, "<outlinePr summaryBelow=\"%d\" summaryRight=\"%d\"/>", !s->sumabove, !s->sumleft);
}
void outline_xlsx_formatpr(const Sheet *s, Buf *o)
{
  int r = outline_max_level(s, 1), c = outline_max_level(s, 0);
  if (r) buf_printf(o, " outlineLevelRow=\"%d\"", r);
  if (c) buf_printf(o, " outlineLevelCol=\"%d\"", c);
}
void outline_xlsx_read_pr(Sheet *s, Xml *x)
{
  const char *v;
  if ((v = xml_attr(x, "summaryBelow"))) s->sumabove = (u8)(!strcmp(v, "0") || !strcmp(v, "false"));
  if ((v = xml_attr(x, "summaryRight"))) s->sumleft = (u8)(!strcmp(v, "0") || !strcmp(v, "false"));
}
