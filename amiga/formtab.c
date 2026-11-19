/*
 * formtab.c - the Formulas tab's newer tools (Recently Used, Use in Formula, Create from
 * Selection, Trace Precedents/Dependents with arrows drawn on the grid, Remove Arrows, Error
 * Checking, Evaluate Formula, Watch Window, Calculate Sheet, Calculation Options), the Review tab's
 * Spelling, Thesaurus, Check Accessibility and Show/Hide Note, and Home's Go To Special.
 */
#include "lxpriv.h"
#include "../core/audit.h"
#include "../core/spell.h"
#include "../core/respath.h"
#include "../core/calc.h"
#include "../core/ops.h"
#include "../core/comments.h"
#include "gmirror.h"              /* (last) drawing on a right-to-left sheet mirrored */

void lt_command(ULONG id, LONG v);

/* ------------------------------------------------------------------ lists: recent functions, names */
static char recentfn[10][32]; static int nrecentfn;
static const char *recentptrs[11];
static char namelabels[64][64]; static const char *nameptrs[66]; static int nnames;
void ft_recent_list(void)
{
  int i;
  for (i = 0; i < nrecentfn; i++) recentptrs[i] = recentfn[i];
  if (!nrecentfn) { recentptrs[0] = "SUM"; recentptrs[1] = "AVERAGE"; recentptrs[2] = "IF"; recentptrs[3] = "COUNT"; recentptrs[4] = "MAX"; i = 5; }
  recentptrs[i] = NULL;
  lx_lists[LXL_RECENT - LXL_FIRST].labels = recentptrs;
  lx_lists[LXL_RECENT - LXL_FIRST].n = i;
}
void ft_used_function(const char *name)
{
  int i;
  for (i = 0; i < nrecentfn && strcmp(recentfn[i], name); i++) ;
  if (i == nrecentfn && nrecentfn < 10) nrecentfn++;
  if (i >= nrecentfn) i = nrecentfn - 1;
  memmove(recentfn[1], recentfn[0], (size_t)i * sizeof recentfn[0]);
  strncpy(recentfn[0], name, sizeof recentfn[0] - 1); recentfn[0][sizeof recentfn[0] - 1] = 0;
  ft_recent_list();
}
void ft_names_list(void)
{
  int i;
  nnames = 0;
  for (i = 0; book && i < book->nnames && nnames < 63; i++) {
    if (book->names[i].sheet != SHEET_SELF && book->names[i].sheet != (u16)cur_index()) continue;
    lx_utf8_latin1(book->names[i].name, namelabels[nnames], sizeof namelabels[nnames]);
    nameptrs[nnames] = namelabels[nnames];
    nnames++;
  }
  nameptrs[nnames] = tr("Paste Names...");
  lx_lists[LXL_USENAMES - LXL_FIRST].labels = nameptrs;
  lx_lists[LXL_USENAMES - LXL_FIRST].n = nnames + 1;
}
/* text typed into the cell being edited, or a new formula started with it */
static void type_in(const char *latin1, int fn)
{
  char t[100];
  if (lx_xget(grid, MUIA_Grid_Editing)) { snprintf(t, sizeof t, fn ? "%s()" : "%s", latin1); DoMethod(grid, MUIM_Grid_Insert, (ULONG)t, fn ? 1 : 0); }
  else { snprintf(t, sizeof t, fn ? "=%s()" : "=%s", latin1); DoMethod(grid, MUIM_Grid_Edit, (ULONG)t, 1); DoMethod(grid, MUIM_Grid_Insert, (ULONG)"", fn ? 1 : 0); }
  set(win, MUIA_Window_ActiveObject, (ULONG)grid);
}

/* ------------------------------------------------------------------ Create from Selection */
static void create_names(void)
{
  static const ULONG ends[] = { ID_DLGOK, 0 };
  Rect a = cur_sel();
  Object *w, *t, *l, *b, *r;
  int top, left, n;
  Cell *c0 = sheet_cell(cur_sheet(), a.r1, a.c1 + 1 <= a.c2 ? a.c1 + 1 : a.c1), *c1 = sheet_cell(cur_sheet(), a.r1 + 1 <= a.r2 ? a.r1 + 1 : a.r1, a.c1);
  if (a.r1 == a.r2 && a.c1 == a.c2) { ask("OK", "%s", tr("Select a table with its labels first.")); return; }
  /* Excel's guess: labels where the edge holds text */
  top = c0 && c0->v.t == V_STR; left = c1 && c1->v.t == V_STR;
  w = dialog_window("Create Names from Selection", VGroup,
        Child, TextObject, MUIA_Text_Contents, (ULONG)tr("Create names from values in the:"), End,
        Child, ColGroup(4),
          Child, t = lx_check(top), Child, LLabel1(tr("Top row")), Child, l = lx_check(left), Child, LLabel1(tr("Left column")),
          Child, b = lx_check(0), Child, LLabel1(tr("Bottom row")), Child, r = lx_check(0), Child, LLabel1(tr("Right column")),
        End,
      End, NULL);
  if (!w) return;
  if (run_modal(w, ends, NULL) == ID_DLGOK) {
    n = book_create_names(book, cur_index(), a, lx_xget(t, MUIA_Selected) != 0, lx_xget(l, MUIA_Selected) != 0, lx_xget(b, MUIA_Selected) != 0, lx_xget(r, MUIA_Selected) != 0);
    if (!n) ask("OK", "%s", tr("No names were made: the labels are empty, or not good names."));
    book->dirty = 1;
    refresh_all();
  }
  MUI_DisposeObject(w);
}

/* ------------------------------------------------------------------ tracer arrows */
typedef struct { Book *b; int sheet; AuditRef from; i32 r, c; u8 dep, err; int level; } Arrow;
#define MAXARROWS 200
static Arrow arrows[MAXARROWS]; static int narrows;
static int levels[2];                 /* how far each kind has gone (precedents, dependents) */
static int has_arrow(const Arrow *a)
{
  int i;
  for (i = 0; i < narrows; i++)
    if (arrows[i].b == a->b && arrows[i].sheet == a->sheet && arrows[i].r == a->r && arrows[i].c == a->c && arrows[i].dep == a->dep &&
        arrows[i].from.sheet == a->from.sheet && !memcmp(&arrows[i].from.r, &a->from.r, sizeof(Rect))) return 1;
  return 0;
}
static void add_arrow(const Arrow *a) { if (narrows < MAXARROWS && !has_arrow(a)) arrows[narrows++] = *a; }
/* one more level of arrows: the first from the active cell, the next from the cells the last one reached */
static void trace(int dep)
{
  Sheet *s = cur_sheet();
  AuditRef refs[64];
  int i, n, k, added = 0, start = narrows;
  struct { int sheet; i32 r, c; } todo[64];
  int ntodo = 0;
  if (!levels[dep]) { todo[0].sheet = cur_index(); todo[0].r = s->cur_r; todo[0].c = s->cur_c; ntodo = 1; }
  else
    for (i = 0; i < narrows && ntodo < 64; i++) {
      const Arrow *a = &arrows[i];
      if (a->b != book || a->dep != dep || a->level != levels[dep] - 1) continue;
      /* precedents: go on from each source cell; dependents: from each target */
      if (dep) { todo[ntodo].sheet = a->from.sheet; todo[ntodo].r = a->from.r.r1; todo[ntodo].c = a->from.r.c1; ntodo++; }
      else if (a->from.r.r1 == a->from.r.r2 && a->from.r.c1 == a->from.r.c2) { todo[ntodo].sheet = a->from.sheet; todo[ntodo].r = a->from.r.r1; todo[ntodo].c = a->from.r.c1; ntodo++; }
    }
  for (k = 0; k < ntodo; k++) {
    n = dep ? audit_dependents(book, todo[k].sheet, todo[k].r, todo[k].c, refs, 64) : audit_precedents(book, todo[k].sheet, todo[k].r, todo[k].c, refs, 64);
    for (i = 0; i < n; i++) {
      Arrow a;
      memset(&a, 0, sizeof a);
      a.b = book; a.dep = (u8)dep; a.level = levels[dep];
      if (dep) {
        /* drawn from the cell traced to each dependent */
        a.from.sheet = todo[k].sheet; a.from.r.r1 = a.from.r.r2 = todo[k].r; a.from.r.c1 = a.from.r.c2 = todo[k].c;
        a.sheet = refs[i].sheet; a.r = refs[i].r.r1; a.c = refs[i].r.c1;
      } else {
        a.from = refs[i]; a.sheet = todo[k].sheet; a.r = todo[k].r; a.c = todo[k].c;
        a.err = cell_value(book, refs[i].sheet, refs[i].r.r1, refs[i].r.c1)->t == V_ERR;
      }
      add_arrow(&a);
      added++;
    }
  }
  if (narrows > start) levels[dep]++;
  else if (!added) { DisplayBeep(NULL); ask("OK", "%s", dep ? tr("No formulas refer to the active cell.") : levels[0] ? tr("There are no more precedents to trace.") : tr("The active cell has no formula that refers to other cells.")); }
  DoMethod(grid, MUIM_Grid_Refresh);
  update_controls();
}
static void remove_arrows(int which)
{
  int i, k;
  for (i = k = 0; i < narrows; i++) if (!(arrows[i].b == book && (which == 0 || (which == 1 && !arrows[i].dep) || (which == 2 && arrows[i].dep)))) arrows[k++] = arrows[i];
  narrows = k;
  if (which != 2) levels[0] = 0;
  if (which != 1) levels[1] = 0;
  DoMethod(grid, MUIM_Grid_Refresh);
  update_controls();
}
/* the centre of a cell on screen; 0 if it isn't shown */
static int centre(const GridGeom *g, i32 r, i32 c, LONG *x, LONG *y, LONG *x0, LONG *y0, LONG *x1, LONG *y1)
{
  int i, j;
  for (i = 0; i < g->nvr && g->vr[i] != r; i++) ;
  for (j = 0; j < g->nvc && g->vc[j] != c; j++) ;
  if (i >= g->nvr || j >= g->nvc) return 0;
  *x0 = g->vx[j]; *x1 = g->vx[j + 1] - 1; *y0 = g->vy[i]; *y1 = g->vy[i + 1] - 1;
  *x = (*x0 + *x1) / 2; *y = (*y0 + *y1) / 2;
  return 1;
}
static void arrow_line(struct RastPort *rp, LONG x0, LONG y0, LONG x1, LONG y1, LONG pen, int dashed)
{
  LONG dx = x1 - x0, dy = y1 - y0, len, ux, uy;
  SetAPen(rp, pen);
  SetDrPt(rp, dashed ? 0xF0F0 : 0xFFFF);
  Move(rp, x0, y0); Draw(rp, x1, y1);
  Move(rp, x0 + 1, y0); Draw(rp, x1 + 1, y1);
  SetDrPt(rp, 0xFFFF);
  /* the head: two strokes back from the point */
  len = 1;
  { LONG a = dx < 0 ? -dx : dx, b = dy < 0 ? -dy : dy; len = a > b ? a + b / 2 : b + a / 2; }
  if (len < 1) return;
  ux = dx * 8 / len; uy = dy * 8 / len;
  Move(rp, x1, y1); Draw(rp, x1 - ux - uy / 2, y1 - uy + ux / 2);
  Move(rp, x1, y1); Draw(rp, x1 - ux + uy / 2, y1 - uy - ux / 2);
  RectFill(rp, x0 - 2, y0 - 2, x0 + 2, y0 + 2);
}
/* marked cells (Go To Special) and pinned notes */
static struct { Book *b; int sheet; AuditCell *c; long n; Rect bound; } marks;
typedef struct { Book *b; int sheet; i32 r, c; } Pin;
static Pin pins[32]; static int npins;
static void pinned_note(struct RastPort *rp, const GridGeom *g, const Note *n)
{
  LONG x, y, x0, y0, x1, y1, bx, by, bw = 180, lh = theme_font()->tf_YSize, bh;
  char t[300], au[80];
  int lines = 0;
  const char *p;
  if (!centre(g, n->r, n->c, &x, &y, &x0, &y0, &x1, &y1)) return;
  lx_utf8_latin1(n->text ? n->text : "", t, sizeof t);
  lx_utf8_latin1(n->author ? n->author : "", au, sizeof au);
  SetFont(rp, theme_font());
  for (p = t; *p && lines < 8; lines++) { size_t k = strcspn(p, "\n"); p += k; if (*p) p++; }
  bh = (lines + 1) * lh + 8;
  bx = x1 + 10; by = y0;
  if (bx + bw >= g->w) bx = x0 - 10 - bw;
  if (by + bh >= g->h) by = g->h - bh - 1;
  theme_fill(rp, bx, by, bx + bw, by + bh, g->pen(g->pd, 0xFFFFE1));
  theme_rect(rp, bx, by, bx + bw, by + bh, g->pen(g->pd, 0x6D6D6D));
  SetAPen(rp, g->pen(g->pd, 0x6D6D6D));
  Move(rp, x1, y0); Draw(rp, bx < x1 ? bx + bw : bx, by + 4);
  SetAPen(rp, g->pen(g->pd, 0)); SetDrMd(rp, JAM1);
  if (au[0]) { SetFont(rp, theme_bold()); Move(rp, bx + 4, by + 3 + theme_bold()->tf_Baseline); Text(rp, (STRPTR)au, (ULONG)strlen(au)); SetFont(rp, theme_font()); }
  {
    LONG ly = by + 3 + lh;
    int k = 0;
    for (p = t; *p && k < 8; k++) {
      size_t m = strcspn(p, "\n");
      struct TextExtent te;
      ULONG fit = TextFit(rp, (STRPTR)p, (ULONG)m, &te, NULL, 1, (ULONG)(bw - 8), 100);
      Move(rp, bx + 4, ly + theme_font()->tf_Baseline); Text(rp, (STRPTR)p, fit);
      ly += lh; p += m; if (*p) p++;
    }
  }
}
void ft_draw(struct RastPort *rp, const GridGeom *g)
{
  int i;
  LONG blue = g->pen(g->pd, 0x2F5597), red = g->pen(g->pd, 0xD00000), black = g->pen(g->pd, 0x000000);
  for (i = 0; i < narrows; i++) {
    const Arrow *a = &arrows[i];
    LONG tx, ty, fx, fy, x0, y0, x1, y1, ax0, ay0, ax1, ay1;
    if (a->b != g->book || a->sheet != g->sheet) {
      /* an arrow from this sheet to a cell elsewhere (a dependent on another sheet) */
      if (a->b == g->book && a->from.sheet == g->sheet && a->sheet != g->sheet && centre(g, a->from.r.r1, a->from.r.c1, &fx, &fy, &x0, &y0, &x1, &y1)) {
        arrow_line(rp, fx, fy, fx + 30, fy - 20, black, 1);
        theme_rect(rp, fx + 30, fy - 28, fx + 42, fy - 18, black);
      }
      continue;
    }
    if (!centre(g, a->r, a->c, &tx, &ty, &x0, &y0, &x1, &y1)) continue;
    if (a->from.sheet != g->sheet) {
      /* from another sheet: a dashed arrow from a little sheet icon */
      LONG sx = x0 - 40 > g->hw ? x0 - 40 : x1 + 30, sy = y0 - 18 > g->hh ? y0 - 18 : y1 + 18;
      theme_rect(rp, sx - 6, sy - 5, sx + 6, sy + 5, black);
      SetAPen(rp, black); Move(rp, sx - 6, sy - 1); Draw(rp, sx + 6, sy - 1); Move(rp, sx - 2, sy - 5); Draw(rp, sx - 2, sy + 5);
      arrow_line(rp, sx, sy, tx, ty, black, 1);
      continue;
    }
    if (!centre(g, a->from.r.r1, a->from.r.c1, &fx, &fy, &ax0, &ay0, &ax1, &ay1)) continue;
    if (a->from.r.r1 != a->from.r.r2 || a->from.r.c1 != a->from.r.c2) {
      /* a range: boxed, the arrow from its top left */
      LONG bx1 = ax1, by1 = ay1;
      int k;
      for (k = 0; k < g->nvc; k++) if (g->vc[k] <= a->from.r.c2) bx1 = g->vx[k + 1] - 1;
      for (k = 0; k < g->nvr; k++) if (g->vr[k] <= a->from.r.r2) by1 = g->vy[k + 1] - 1;
      theme_rect(rp, ax0, ay0, bx1, by1, a->err ? red : blue);
      theme_rect(rp, ax0 + 1, ay0 + 1, bx1 - 1, by1 - 1, a->err ? red : blue);
    }
    arrow_line(rp, fx, fy, tx, ty, a->err ? red : blue, 0);
  }
  if (marks.c && marks.b == g->book && marks.sheet == g->sheet) {
    long k;
    LONG pen = g->pen(g->pd, 0x217346);
    for (k = 0; k < marks.n && k < 2000; k++) {
      LONG x, y, x0, y0, x1, y1;
      if (!centre(g, marks.c[k].r, marks.c[k].c, &x, &y, &x0, &y0, &x1, &y1)) continue;
      theme_rect(rp, x0 + 1, y0 + 1, x1 - 1, y1 - 1, pen);
    }
  }
  {
    /* a threaded comment's corner is purple (a note's red one is the grid's) */
    Sheet *s = g->book->sh[g->sheet];
    LONG purple = g->pen(g->pd, 0x7030A0);
    for (i = 0; i < s->nnotes; i++) {
      LONG x, y, x0, y0, x1, y1;
      int k;
      if (!s->notes[i].threaded || !centre(g, s->notes[i].r, s->notes[i].c, &x, &y, &x0, &y0, &x1, &y1)) continue;
      SetAPen(rp, purple);
      for (k = 0; k < 6; k++) { Move(rp, x1 - 5 + k, y0); Draw(rp, x1, y0 + 5 - k); }
    }
  }
  for (i = 0; i < npins; i++) {
    const Note *n;
    if (pins[i].b != g->book || pins[i].sheet != g->sheet) continue;
    if ((n = sheet_note(g->book->sh[g->sheet], pins[i].r, pins[i].c))) pinned_note(rp, g, n);
  }
}
void lx_selection_moved(void)
{
  free(marks.c); marks.c = NULL; marks.n = 0;
}

/* ------------------------------------------------------------------ Go To Special */
static void goto_special_dlg(void)
{
  static const char *const kinds_en[] = { "Blanks", "Constants", "Formulas", "Notes", "Last cell", "Current region", "Visible cells only",
                                          "Conditional formats", "Errors", "Numbers", "Text", "Logicals", NULL };
  static const char *kinds[16];
  static int last = 2;
  static const ULONG ends[] = { ID_DLGOK, 0 };
  Object *w, *list;
  int i;
  lx_trl(kinds_en, kinds);
  w = dialog_window("Go To Special", VGroup,
        Child, TextObject, MUIA_Text_Contents, (ULONG)tr("Select the cells holding:"), End,
        Child, ListviewObject, MUIA_Listview_List, list = ListObject, InputListFrame, MUIA_List_SourceArray, (ULONG)kinds, End, MUIA_CycleChain, 1, End,
        Child, TextObject, MUIA_Text_Contents, (ULONG)tr("\33cWithin the selection, or the whole sheet if one cell is selected."), End,
      End, NULL);
  if (!w) return;
  set(list, MUIA_List_Active, last);
  DoMethod(list, MUIM_Notify, MUIA_Listview_DoubleClick, TRUE, (ULONG)app, 2, MUIM_Application_ReturnID, ID_DLGOK);
  if (run_modal(w, ends, NULL) == ID_DLGOK && (i = (int)(LONG)lx_xget(list, MUIA_List_Active)) >= 0) {
    Rect bound;
    Sheet *s = cur_sheet();
    AuditCell *cells = (AuditCell *)malloc(4000 * sizeof(AuditCell));
    long n;
    last = i;
    n = goto_special(book, cur_index(), cur_sel(), s->cur_r, s->cur_c, i, &bound, cells, cells ? 4000 : 0);
    if (n <= 0 || bound.r2 < bound.r1) { free(cells); ask("OK", "%s", tr("No cells were found.")); }
    else {
      char msg[80];
      DoMethod(grid, MUIM_Grid_Select, bound.r1, bound.c1, bound.r2, bound.c2, bound.r1, bound.c1);
      free(marks.c);
      marks.b = book; marks.sheet = cur_index(); marks.bound = bound; marks.c = i == GS_LASTCELL || i == GS_REGION ? (free(cells), (AuditCell *)NULL) : cells; marks.n = n < 4000 ? n : 4000;
      DoMethod(grid, MUIM_Grid_Refresh);
      update_controls();
      if (marks.c) { sprintf(msg, tr("%ld cells found (marked in green)."), n); set(statusbar, MUIA_ShortHelp, (ULONG)msg); }
    }
  }
  MUI_DisposeObject(w);
}

/* ------------------------------------------------------------------ Error Checking */
static AuditCell *errs; static int nerrs, erri;
static Object *err_text, *err_help;
static void err_show(void)
{
  static char t[600];
  char ref[48], f[300], nm[40];
  AuditCell *e;
  if (!nerrs || erri < 0 || erri >= nerrs) { set(err_text, MUIA_Text_Contents, (ULONG)tr("\33cThe error check is complete for the whole workbook.")); set(err_help, MUIA_Text_Contents, (ULONG)""); return; }
  e = &errs[erri];
  if (e->sheet != cur_index()) { book->active = e->sheet; set(grid, MUIA_Grid_Book, (ULONG)book); }
  DoMethod(grid, MUIM_Grid_Select, e->r, e->c, e->r, e->c, e->r, e->c);
  ref_text(e->r, e->c, 0, 0, ref);
  lx_utf8_latin1(book->sh[e->sheet]->name, nm, sizeof nm);
  grid_edit_text(book, e->sheet, e->r, e->c, f, sizeof f);
  snprintf(t, sizeof t, "\33b%s %s!%s\33n  (%d of %d)\n%s", tr("Error in cell"), nm, ref, erri + 1, nerrs, f);
  set(err_text, MUIA_Text_Contents, (ULONG)t);
  set(err_help, MUIA_Text_Contents, (ULONG)tr(audit_error_help(e->err)));
  refresh_all();
}
static void evaluate_dlg(void);
static void err_each(ULONG id)
{
  if (id == ID_DLG1) { if (erri < nerrs - 1) erri++; else erri = nerrs; err_show(); }
  else if (id == ID_DLG2) { if (erri > 0) erri--; err_show(); }
  else if (id == ID_DLG3) {
    /* Ignore Error: off the list */
    if (erri >= 0 && erri < nerrs) { memmove(&errs[erri], &errs[erri + 1], (size_t)(nerrs - erri - 1) * sizeof *errs); nerrs--; }
    err_show();
  } else if (id == ID_DLG4) evaluate_dlg();
}
static void error_checking(void)
{
  static const ULONG ends[] = { ID_DLGOK, ID_DLG5, 0 };
  Object *w, *next, *prev, *ign, *steps, *edit;
  ULONG r;
  free(errs);
  errs = (AuditCell *)malloc(2000 * sizeof(AuditCell));
  nerrs = errs ? audit_errors(book, errs, 2000) : 0;
  erri = 0;
  if (!nerrs) { ask("OK", "%s", tr("The error check is complete for the whole workbook: no cell shows an error.")); return; }
  /* start from the active cell */
  {
    int i;
    Sheet *s = cur_sheet();
    for (i = 0; i < nerrs; i++) if (errs[i].sheet > cur_index() || (errs[i].sheet == cur_index() && (errs[i].r > s->cur_r || (errs[i].r == s->cur_r && errs[i].c >= s->cur_c)))) { erri = i; break; }
  }
  w = dialog_window("Error Checking", HGroup,
        Child, VGroup,
          Child, err_text = TextObject, TextFrame, MUIA_Background, MUII_TextBack, MUIA_Text_Contents, (ULONG)"", MUIA_FixWidthTxt, (ULONG)"WWWWWWWWWWWWWWWWWWWWWWWWWWWW", End,
          Child, err_help = TextObject, MUIA_Text_Contents, (ULONG)"", End,
          Child, VSpace(0),
        End,
        Child, VGroup, MUIA_HorizWeight, 0,
          Child, steps = QButton(tr("Show Calculation Steps...")), Child, ign = QButton(tr("Ignore Error")), Child, edit = QButton(tr("Edit in Formula Bar")),
          Child, VSpace(0),
          Child, HGroup, Child, prev = QButton(tr("Previous")), Child, next = QButton(tr("Next")), End,
        End,
      End, "Close");
  if (!w) return;
  DoMethod(next, MUIM_Notify, MUIA_Pressed, FALSE, (ULONG)app, 2, MUIM_Application_ReturnID, ID_DLG1);
  DoMethod(prev, MUIM_Notify, MUIA_Pressed, FALSE, (ULONG)app, 2, MUIM_Application_ReturnID, ID_DLG2);
  DoMethod(ign, MUIM_Notify, MUIA_Pressed, FALSE, (ULONG)app, 2, MUIM_Application_ReturnID, ID_DLG3);
  DoMethod(steps, MUIM_Notify, MUIA_Pressed, FALSE, (ULONG)app, 2, MUIM_Application_ReturnID, ID_DLG4);
  DoMethod(edit, MUIM_Notify, MUIA_Pressed, FALSE, (ULONG)app, 2, MUIM_Application_ReturnID, ID_DLG5);
  err_show();
  r = run_modal(w, ends, err_each);
  MUI_DisposeObject(w);
  if (r == ID_DLG5) DoMethod(grid, MUIM_Grid_Edit, 0, 2);
}

/* ------------------------------------------------------------------ Evaluate Formula */
static EvalSteps ev;
static Object *ev_text, *ev_next, *ev_btn;
static void ev_show(void)
{
  char u[600], part[300], l[600], lp[300];
  static char t[1200];
  eval_text(&ev, u, sizeof u);
  lx_utf8_latin1(u, l, sizeof l);
  if (eval_next(&ev, part, sizeof part)) {
    lx_utf8_latin1(part, lp, sizeof lp);
    snprintf(t, sizeof t, "%s", l);
    set(ev_next, MUIA_Text_Contents, (ULONG)lp);
    set(ev_btn, MUIA_Disabled, FALSE);
  } else {
    snprintf(t, sizeof t, "%s", l);
    set(ev_next, MUIA_Text_Contents, (ULONG)tr("(the result)"));
    set(ev_btn, MUIA_Disabled, TRUE);
  }
  set(ev_text, MUIA_Text_Contents, (ULONG)t);
}
static void ev_each(ULONG id)
{
  if (id == ID_DLG1) { eval_step(&ev); ev_show(); }
  else if (id == ID_DLG2) { int sh = ev.sheet; i32 r = ev.r, c = ev.c; eval_end(&ev); eval_begin(&ev, book, sh, r, c); ev_show(); }
}
static void evaluate_dlg(void)
{
  static const ULONG ends[] = { ID_DLGOK, 0 };
  Sheet *s = cur_sheet();
  Object *w, *restart;
  char ref[60], nm[40], head[120];
  if (!eval_begin(&ev, book, cur_index(), s->cur_r, s->cur_c)) { ask("OK", "%s", tr("The active cell has no formula to evaluate.")); return; }
  ref_text(s->cur_r, s->cur_c, 1, 1, ref);
  lx_utf8_latin1(s->name, nm, sizeof nm);
  snprintf(head, sizeof head, "%s  %s!%s", tr("Reference:"), nm, ref);
  w = dialog_window("Evaluate Formula", VGroup,
        Child, TextObject, MUIA_Text_Contents, (ULONG)head, End,
        Child, TextObject, MUIA_Text_Contents, (ULONG)tr("Evaluation:"), End,
        Child, ev_text = TextObject, TextFrame, MUIA_Background, MUII_TextBack, MUIA_Text_Contents, (ULONG)"", MUIA_FixWidthTxt, (ULONG)"WWWWWWWWWWWWWWWWWWWWWWWWWWWWWW", End,
        Child, HGroup, Child, Label1(tr("Next:")), Child, ev_next = TextObject, TextFrame, MUIA_Text_Contents, (ULONG)"", End, End,
        Child, TextObject, MUIA_Text_Contents, (ULONG)tr("\33cEvaluate works out the part shown as Next; references\nand the innermost operations come first."), End,
        Child, HGroup, Child, ev_btn = QButton(tr("Evaluate")), Child, restart = QButton(tr("Restart")), Child, HSpace(0), End,
      End, "Close");
  if (!w) { eval_end(&ev); return; }
  DoMethod(ev_btn, MUIM_Notify, MUIA_Pressed, FALSE, (ULONG)app, 2, MUIM_Application_ReturnID, ID_DLG1);
  DoMethod(restart, MUIM_Notify, MUIA_Pressed, FALSE, (ULONG)app, 2, MUIM_Application_ReturnID, ID_DLG2);
  ev_show();
  run_modal(w, ends, ev_each);
  MUI_DisposeObject(w);
  eval_end(&ev);
}

/* ------------------------------------------------------------------ the Watch Window (kept open beside the workbook) */
typedef struct { Book *b; int sheet; i32 r, c; } Watch;
static Watch watches[50]; static int nwatches;
static Object *wwin, *wlist;
static char wlines[50][160];
static void watch_fill(void)
{
  int i, k;
  Object *w;
  Book *b;
  if (!wwin || !lx_xget(wwin, MUIA_Window_Open)) return;
  set(wlist, MUIA_List_Quiet, TRUE);
  DoMethod(wlist, MUIM_List_Clear);
  for (i = k = 0; i < nwatches; i++) {
    char ref[16], val[200], f[200], nm[40], bn[40];
    const char *pth = NULL;
    u32 col;
    int s;
    /* the watch's workbook must still be open */
    for (s = 0; s < win_slots(); s++) if (win_info(s, &w, &b, &pth, NULL) && b == watches[i].b) break;
    if (s == win_slots() || watches[i].sheet >= watches[i].b->nsh) continue;
    watches[k++] = watches[i];
    ref_text(watches[i].r, watches[i].c, 0, 0, ref);
    grid_cell_text(watches[i].b, watches[i].sheet, watches[i].r, watches[i].c, 0, val, sizeof val, &col);
    grid_edit_text(watches[i].b, watches[i].sheet, watches[i].r, watches[i].c, f, sizeof f);
    lx_utf8_latin1(watches[i].b->sh[watches[i].sheet]->name, nm, sizeof nm);
    strncpy(bn, pth && pth[0] ? (const char *)FilePart((STRPTR)pth) : tr("Book1"), sizeof bn - 1); bn[sizeof bn - 1] = 0;
    snprintf(wlines[k - 1], sizeof wlines[k - 1], "%s\t%s\t%s\t%s\t%s", bn, nm, ref, val, f[0] == '=' ? f : "");
    DoMethod(wlist, MUIM_List_InsertSingle, (ULONG)wlines[k - 1], MUIV_List_Insert_Bottom);
  }
  nwatches = k;
  set(wlist, MUIA_List_Quiet, FALSE);
}
/* the list's display hook: a line split at its tabs into the columns */
static char *wcols[5];
static LONG watch_display(__reg("a0") struct Hook *h, __reg("a2") char **array, __reg("a1") char *entry)
{
  static const char *const heads[5] = { "Book", "Sheet", "Cell", "Value", "Formula" };
  static char buf[160];
  int i;
  if (!entry || !h) { for (i = 0; i < 5; i++) array[i] = (char *)tr(heads[i]); return 0; }
  strncpy(buf, entry, sizeof buf - 1); buf[sizeof buf - 1] = 0;
  wcols[0] = buf;
  for (i = 1; i < 5; i++) { char *t = strchr(wcols[i - 1], '\t'); if (t) { *t = 0; wcols[i] = t + 1; } else wcols[i] = (char *)""; }
  for (i = 0; i < 5; i++) array[i] = wcols[i];
  return 0;
}
static struct Hook watch_hook = { { NULL, NULL }, (ULONG (*)())watch_display, NULL, NULL };
static void watch_open(void)
{
  if (!wwin) {
    Object *add, *del;
    wwin = QWindowObject, MUIA_Window_Title, (ULONG)tr("Watch Window"), MUIA_Window_ID, MAKE_ID('W','A','T','C'),
      WindowContents, VGroup, QDLG,
        Child, HGroup, Child, add = QButton(tr("Add Watch")), Child, del = QButton(tr("Delete Watch")), Child, HSpace(0), End,
        Child, ListviewObject, MUIA_Listview_List, wlist = ListObject, InputListFrame, MUIA_List_Format, (ULONG)"BAR,BAR,BAR,BAR P=\33r,", MUIA_List_Title, TRUE,
          MUIA_List_DisplayHook, (ULONG)&watch_hook, End, MUIA_CycleChain, 1, End,
      End, End;
    if (!wwin) return;
    DoMethod(app, OM_ADDMEMBER, (ULONG)wwin);
    DoMethod(wwin, MUIM_Notify, MUIA_Window_CloseRequest, TRUE, (ULONG)wwin, 3, MUIM_Set, MUIA_Window_Open, FALSE);
    DoMethod(add, MUIM_Notify, MUIA_Pressed, FALSE, (ULONG)app, 2, MUIM_Application_ReturnID, ID_LX_WATCHADD);
    DoMethod(del, MUIM_Notify, MUIA_Pressed, FALSE, (ULONG)app, 2, MUIM_Application_ReturnID, ID_LX_WATCHDEL);
    DoMethod(wlist, MUIM_Notify, MUIA_Listview_DoubleClick, TRUE, (ULONG)app, 2, MUIM_Application_ReturnID, ID_LX_WATCHPICK);
  }
  set(wwin, MUIA_Window_Open, TRUE);
  watch_fill();
}
static void watch_add(void)
{
  Rect a = cur_sel();
  i32 r, c;
  for (r = a.r1; r <= a.r2 && r <= a.r1 + 49; r++)
    for (c = a.c1; c <= a.c2 && c <= a.c1 + 49 && nwatches < 50; c++) {
      int i;
      for (i = 0; i < nwatches; i++) if (watches[i].b == book && watches[i].sheet == cur_index() && watches[i].r == r && watches[i].c == c) break;
      if (i < nwatches) continue;
      watches[nwatches].b = book; watches[nwatches].sheet = cur_index(); watches[nwatches].r = r; watches[nwatches].c = c;
      nwatches++;
    }
  watch_fill();
}

/* ------------------------------------------------------------------ spelling */
static Spell spell;
static int spell_loaded;
static Object *spwin, *spword, *splist, *spto, *spbtn[5];
static struct { int sheet; i32 r, c; u32 off, ws, we; char word[128]; int found; } sp;
static void spell_load_all(void)
{
  FILE *f;
  char line[128], path[160];
  if (spell_loaded) return;
  spell_loaded = 1;
  if (!res_join(path, sizeof path, res_dict_dir, "british-english") || !spell_load(&spell, path)) return;
  if ((f = fopen("PROGDIR:Ledger.words", "r"))) {
    while (fgets(line, sizeof line, f)) { line[strcspn(line, "\r\n")] = 0; if (line[0]) spell_add(&spell, line, (u32)strlen(line)); }
    fclose(f);
  }
}
static void spell_find(void)
{
  static char shown[300];
  char *sug[10];
  int n, i;
  DoMethod(splist, MUIM_List_Clear);
  if (!spell_next_cell(&spell, book, 1, &sp.sheet, &sp.r, &sp.c, &sp.off, &sp.ws, &sp.we) || (sp.found && sp.found > 2000)) {
    sp.found = 0; sp.word[0] = 0;
    set(spword, MUIA_Text_Contents, (ULONG)tr("\33cThe spelling check is complete for the entire workbook."));
    set(spto, MUIA_String_Contents, (ULONG)"");
    for (i = 0; i < 5; i++) set(spbtn[i], MUIA_Disabled, TRUE);
    return;
  }
  sp.found++;
  for (i = 0; i < 5; i++) set(spbtn[i], MUIA_Disabled, FALSE);
  {
    const char *t = cell_value(book, sp.sheet, sp.r, sp.c)->s;
    u32 len = sp.we - sp.ws;
    char lat[130], cell[16], ctx[200];
    if (len >= sizeof sp.word) len = sizeof sp.word - 1;
    memcpy(sp.word, t + sp.ws, len); sp.word[len] = 0;
    if (sp.sheet != book->active) { book->active = sp.sheet; set(grid, MUIA_Grid_Book, (ULONG)book); }
    DoMethod(grid, MUIM_Grid_Select, sp.r, sp.c, sp.r, sp.c, sp.r, sp.c);
    lx_utf8_latin1(sp.word, lat, sizeof lat);
    ref_text(sp.r, sp.c, 0, 0, cell);
    lx_utf8_latin1(t, ctx, sizeof ctx);
    snprintf(shown, sizeof shown, "%s \33b%s\33n  (%s)\n%s", tr("Not in Dictionary:"), lat, cell, ctx);
    set(spword, MUIA_Text_Contents, (ULONG)shown);
  }
  n = spell_suggest(&spell, sp.word, (u32)strlen(sp.word), sug, 10);
  for (i = 0; i < n; i++) {
    char lat[130];
    lx_utf8_latin1(sug[i], lat, sizeof lat);
    DoMethod(splist, MUIM_List_InsertSingle, (ULONG)lat, MUIV_List_Insert_Bottom);
    free(sug[i]);
  }
  if (!n) {
    char lat[130];
    DoMethod(splist, MUIM_List_InsertSingle, (ULONG)tr("(no suggestions)"), MUIV_List_Insert_Bottom);
    lx_utf8_latin1(sp.word, lat, sizeof lat);
    set(spto, MUIA_String_Contents, (ULONG)lat);
  } else set(splist, MUIA_List_Active, 0);
  refresh_all();
}
static void spelling_open(void)
{
  Sheet *s = cur_sheet();
  spell_load_all();
  if (!spell.n) { ask("OK", "%s", tr("Ledger has no dictionary: it should be in\nthe Dict drawer next to Ledger or in Bureau:Dict.")); return; }
  if (!spwin) {
    Object *close;
    spwin = QWindowObject, MUIA_Window_Title, (ULONG)tr("Spelling: English (UK)"), MUIA_Window_ID, MAKE_ID('S','P','E','L'),
      WindowContents, VGroup, QDLG,
        Child, spword = TextObject, TextFrame, MUIA_Background, MUII_TextBack, MUIA_Text_Contents, (ULONG)"", MUIA_FixWidthTxt, (ULONG)"WWWWWWWWWWWWWWWWWWWWWWWWWW", End,
        Child, HGroup, Child, Label1(tr("Change to:")), Child, spto = lx_str("", 128), End,
        Child, HGroup,
          Child, ListviewObject, MUIA_Listview_List, splist = ListObject, InputListFrame, MUIA_List_ConstructHook, MUIV_List_ConstructHook_String,
            MUIA_List_DestructHook, MUIV_List_DestructHook_String, End, End,
          Child, VGroup, MUIA_HorizWeight, 0,
            Child, spbtn[2] = QButton(tr("Ignore Once")), Child, spbtn[3] = QButton(tr("Ignore All")), Child, spbtn[4] = QButton(tr("Add to Dictionary")),
            Child, spbtn[0] = QButton(tr("Change")), Child, spbtn[1] = QButton(tr("Change All")), Child, VSpace(0), Child, close = QButton(tr("Close")),
          End,
        End,
      End, End;
    if (!spwin) return;
    DoMethod(app, OM_ADDMEMBER, (ULONG)spwin);
    DoMethod(spwin, MUIM_Notify, MUIA_Window_CloseRequest, TRUE, (ULONG)spwin, 3, MUIM_Set, MUIA_Window_Open, FALSE);
    DoMethod(close, MUIM_Notify, MUIA_Pressed, FALSE, (ULONG)spwin, 3, MUIM_Set, MUIA_Window_Open, FALSE);
    DoMethod(spbtn[0], MUIM_Notify, MUIA_Pressed, FALSE, (ULONG)app, 2, MUIM_Application_ReturnID, ID_LX_SPCHANGE);
    DoMethod(spbtn[1], MUIM_Notify, MUIA_Pressed, FALSE, (ULONG)app, 2, MUIM_Application_ReturnID, ID_LX_SPCHANGEALL);
    DoMethod(spbtn[2], MUIM_Notify, MUIA_Pressed, FALSE, (ULONG)app, 2, MUIM_Application_ReturnID, ID_LX_SPIGNORE);
    DoMethod(spbtn[3], MUIM_Notify, MUIA_Pressed, FALSE, (ULONG)app, 2, MUIM_Application_ReturnID, ID_LX_SPIGNOREALL);
    DoMethod(spbtn[4], MUIM_Notify, MUIA_Pressed, FALSE, (ULONG)app, 2, MUIM_Application_ReturnID, ID_LX_SPADD);
    DoMethod(splist, MUIM_Notify, MUIA_Listview_DoubleClick, TRUE, (ULONG)app, 2, MUIM_Application_ReturnID, ID_LX_SPCHANGE);
    DoMethod(splist, MUIM_Notify, MUIA_List_Active, MUIV_EveryTime, (ULONG)app, 2, MUIM_Application_ReturnID, ID_LX_SPPICK);
    DoMethod(spto, MUIM_Notify, MUIA_String_Acknowledge, MUIV_EveryTime, (ULONG)app, 2, MUIM_Application_ReturnID, ID_LX_SPCHANGE);
  }
  sp.sheet = cur_index(); sp.r = s->cur_r; sp.c = s->cur_c; sp.off = 0; sp.found = 0;
  set(spwin, MUIA_Window_Open, TRUE);
  spell_find();
}
static void spelling_command(ULONG id)
{
  char u[260];
  if (id == ID_LX_SPPICK) {
    char *pick = NULL;
    DoMethod(splist, MUIM_List_GetEntry, MUIV_List_GetEntry_Active, (ULONG)&pick);
    if (pick && pick[0] != '(') set(spto, MUIA_String_Contents, (ULONG)pick);
    return;
  }
  if (!sp.word[0]) { spell_find(); return; }
  switch (id) {
  case ID_LX_SPCHANGE: case ID_LX_SPCHANGEALL: {
    const char *to = lx_sget(spto);
    if (!to[0]) { DisplayBeep(NULL); return; }
    lx_latin1_utf8(to, u, sizeof u);
    if (id == ID_LX_SPCHANGE) {
      if (spell_change(book, sp.sheet, sp.r, sp.c, sp.ws, sp.we, u)) sp.off = sp.ws + (u32)strlen(u);
    } else {
      spell_change_all(book, 1, sp.sheet, sp.word, u);
      sp.off = sp.ws + (u32)strlen(u);
    }
    break; }
  case ID_LX_SPIGNORE: break;
  case ID_LX_SPIGNOREALL: spell_add(&spell, sp.word, (u32)strlen(sp.word)); break;
  case ID_LX_SPADD: {
    FILE *f = fopen("PROGDIR:Ledger.words", "a");
    spell_add(&spell, sp.word, (u32)strlen(sp.word));
    if (f) { fprintf(f, "%s\n", sp.word); fclose(f); }
    break; }
  }
  spell_find();
}
static void thesaurus(void)
{
  Sheet *s = cur_sheet();
  const Value *v = cell_value(book, cur_index(), s->cur_r, s->cur_c);
  char *rel[20], lat[20][64], word[64];
  const char *ptrs[21];
  int n, i, k;
  u32 ws, we;
  spell_load_all();
  if (v->t != V_STR || !spell_word_at(v->s, (u32)strlen(v->s), 0, &ws, &we) || we - ws >= sizeof word) { ask("OK", "%s", tr("Select a cell holding a word to look up.")); return; }
  memcpy(word, v->s + ws, we - ws); word[we - ws] = 0;
  n = spell_related(&spell, word, rel, 20);
  if (!n) { char m[120], wl[64]; lx_utf8_latin1(word, wl, sizeof wl); snprintf(m, sizeof m, tr("No related words were found for \"%s\"."), wl); ask("OK", "%s", m); return; }
  for (i = 0; i < n; i++) { lx_utf8_latin1(rel[i], lat[i], sizeof lat[i]); ptrs[i] = lat[i]; }
  k = lx_pick("Thesaurus", "Related words (Insert replaces the cell's first word):", ptrs, n, 0);
  if (k >= 0) { spell_change(book, cur_index(), s->cur_r, s->cur_c, ws, we, rel[k]); refresh_all(); }
  for (i = 0; i < n; i++) free(rel[i]);
}
static void accessibility(void)
{
  char out[2000], lat[2000], *p;
  int n = audit_accessibility(book, out, sizeof out);
  if (!n) { ask("OK", "%s", tr("Inspection Results: no accessibility issues found.")); return; }
  lx_utf8_latin1(out, lat, sizeof lat);
  for (p = lat; *p; p++) if (*p == '\t') *p = ':';
  {
    char msg[2200];
    snprintf(msg, sizeof msg, "%s\n\n%s", tr("Inspection Results: things to fix"), lat);
    ask("OK", "%s", msg);
  }
}
static void note_show_hide(void)
{
  Sheet *s = cur_sheet();
  int i;
  if (!sheet_note(s, s->cur_r, s->cur_c)) return;
  for (i = 0; i < npins; i++) if (pins[i].b == book && pins[i].sheet == cur_index() && pins[i].r == s->cur_r && pins[i].c == s->cur_c) break;
  if (i < npins) { memmove(&pins[i], &pins[i + 1], (size_t)(npins - i - 1) * sizeof(Pin)); npins--; }
  else if (npins < 32) { pins[npins].b = book; pins[npins].sheet = cur_index(); pins[npins].r = s->cur_r; pins[npins].c = s->cur_c; npins++; }
  DoMethod(grid, MUIM_Grid_Refresh);
  update_controls();
}

/* ------------------------------------------------------------------ threaded comments */
static void thread_text(const Note *n, char *out, size_t cap)
{
  char a[100], t[600], la[100], lt[600];
  int k;
  size_t len;
  lx_utf8_latin1(n->author ? n->author : "", la, sizeof la);
  lx_utf8_latin1(n->text ? n->text : "", lt, sizeof lt);
  snprintf(out, cap, "\33b%s\33n%s\n%s", la[0] ? la : tr("(no name)"), n->resolved ? tr("  (resolved)") : "", lt);
  for (k = 0; comment_reply(n, k, a, sizeof a, t, sizeof t); k++) {
    lx_utf8_latin1(a, la, sizeof la); lx_utf8_latin1(t, lt, sizeof lt);
    len = strlen(out);
    snprintf(out + len, cap - len, "\n  \33b%s\33n: %s", la, lt);
  }
}
static void comment_dlg(void)
{
  static const ULONG ends[] = { ID_DLGOK, 0 };
  Sheet *s = cur_sheet();
  const Note *n = sheet_note(s, s->cur_r, s->cur_c);
  Object *w, *text, *res = NULL;
  char thread[2000], au[70];
  if (n && !n->threaded) {
    if (ask("Convert|Cancel", "%s", tr("The cell has a note. Turn it into a comment, so it can have replies?")) != 1) return;
    book_begin(book); book_note_sheet(book, cur_index()); ((Note *)n)->threaded = 1; book_end(book); book->dirty = 1;
    n = sheet_note(cur_sheet(), s->cur_r, s->cur_c);
  }
  lx_latin1_utf8(username, au, sizeof au);
  if (n) {
    thread_text(n, thread, sizeof thread);
    w = dialog_window("Reply to Comment", VGroup,
          Child, TextObject, TextFrame, MUIA_Background, MUII_TextBack, MUIA_Text_Contents, (ULONG)thread, MUIA_FixWidthTxt, (ULONG)"WWWWWWWWWWWWWWWWWWWWWWWWWW", End,
          Child, HGroup, Child, Label1(tr("Reply:")), Child, text = lx_str("", 500), End,
          Child, HGroup, Child, res = lx_check(n->resolved), Child, LLabel1(tr("Resolve thread")), Child, HSpace(0), End,
        End, "Post");
  } else {
    w = dialog_window("New Comment", VGroup,
          Child, TextObject, MUIA_Text_Contents, (ULONG)tr("Start a conversation: others can reply under it."), End,
          Child, text = lx_str("", 500),
        End, "Post");
  }
  if (!w) return;
  set(w, MUIA_Window_ActiveObject, (ULONG)text);
  if (run_modal(w, ends, NULL) == ID_DLGOK) {
    char u[1000];
    lx_latin1_utf8(lx_sget(text), u, sizeof u);
    if (n) {
      if (u[0]) book_add_reply(book, cur_index(), s->cur_r, s->cur_c, u, au[0] ? au : "Ledger");
      if (res && (lx_xget(res, MUIA_Selected) != 0) != (n->resolved != 0)) book_resolve_comment(book, cur_index(), s->cur_r, s->cur_c, lx_xget(res, MUIA_Selected) != 0);
    } else if (u[0]) book_set_comment(book, cur_index(), s->cur_r, s->cur_c, u, au[0] ? au : "Ledger");
    refresh_all();
  }
  MUI_DisposeObject(w);
}
static void comment_go(int threaded, int forward)
{
  Sheet *s = cur_sheet();
  int sh = cur_index();
  i32 r = s->cur_r, c = s->cur_c;
  if (!comment_step(book, threaded, forward, &sh, &r, &c)) { DisplayBeep(NULL); return; }
  if (sh != cur_index()) { book->active = sh; set(grid, MUIA_Grid_Book, (ULONG)book); }
  DoMethod(grid, MUIM_Grid_Select, r, c, r, c, r, c);
  refresh_all();
}
/* Show Comments: every thread of the workbook, to read and go to */
static void comments_pane(void)
{
  static const ULONG ends[] = { ID_DLGOK, 0 };
  static char lines[200][160];
  static const char *ptrs[201];
  static struct { int sheet; i32 r, c; } where[200];
  Object *w, *list;
  int si, i, n = 0;
  for (si = 0; si < book->nsh && n < 199; si++) {
    Sheet *s = book->sh[si];
    for (i = 0; i < s->nnotes && n < 199; i++) {
      const Note *nt = &s->notes[i];
      char ref[16], nm[40], la[60], lt[100];
      if (!nt->threaded) continue;
      ref_text(nt->r, nt->c, 0, 0, ref);
      lx_utf8_latin1(s->name, nm, sizeof nm); lx_utf8_latin1(nt->author ? nt->author : "", la, sizeof la); lx_utf8_latin1(nt->text ? nt->text : "", lt, sizeof lt);
      snprintf(lines[n], sizeof lines[n], "%s!%s  %s: %s  [%d %s]%s", nm, ref, la, lt, comment_nreplies(nt), tr("replies"), nt->resolved ? tr(" (resolved)") : "");
      ptrs[n] = lines[n]; where[n].sheet = si; where[n].r = nt->r; where[n].c = nt->c; n++;
    }
  }
  ptrs[n] = NULL;
  if (!n) { ask("OK", "%s", tr("The workbook has no comments (Review > New Comment).")); return; }
  w = dialog_window("Comments", VGroup,
        Child, ListviewObject, MUIA_Listview_List, list = ListObject, InputListFrame, MUIA_List_SourceArray, (ULONG)ptrs, MUIA_List_Active, 0, End, MUIA_CycleChain, 1, End,
      End, "Go To");
  if (!w) return;
  DoMethod(list, MUIM_Notify, MUIA_Listview_DoubleClick, TRUE, (ULONG)app, 2, MUIM_Application_ReturnID, ID_DLGOK);
  if (run_modal(w, ends, NULL) == ID_DLGOK) {
    LONG a = (LONG)lx_xget(list, MUIA_List_Active);
    if (a >= 0 && a < n) {
      if (where[a].sheet != cur_index()) { book->active = where[a].sheet; set(grid, MUIA_Grid_Book, (ULONG)book); }
      DoMethod(grid, MUIM_Grid_Select, where[a].r, where[a].c, where[a].r, where[a].c, where[a].r, where[a].c);
      refresh_all();
    }
  }
  MUI_DisposeObject(w);
}

/* ------------------------------------------------------------------ the commands */
void ft_command(ULONG id, LONG v)
{
  switch (id) {
  case ID_LX_RECENTDD:
    ft_recent_list();
    if (v < 0) v = lx_pick("Recently Used", "Insert the function:", recentptrs, lx_lists[LXL_RECENT - LXL_FIRST].n, 0);
    if (v >= 0 && v < lx_lists[LXL_RECENT - LXL_FIRST].n) { char f[32]; strcpy(f, recentptrs[v]); ft_used_function(f); type_in(f, 1); }
    break;
  case ID_LX_USENAMEDD:
    ft_names_list();
    if (v < 0 || v == nnames) {
      if (!nnames) { ask("OK", "%s", tr("The workbook has no names yet (Formulas > Define Name).")); break; }
      v = lx_pick("Paste Name", "Paste name:", nameptrs, nnames, 0);
    }
    if (v >= 0 && v < nnames) type_in(namelabels[v], 0);
    break;
  case ID_LX_CREATENAMES: create_names(); break;
  case ID_LX_TRACEPREC: trace(0); break;
  case ID_LX_TRACEDEP: trace(1); break;
  case ID_LX_REMOVEARROWS: remove_arrows(0); break;
  case ID_LX_REMOVEDD: remove_arrows(v < 0 ? 0 : (int)v); break;
  case ID_LX_ERRCHECK: error_checking(); break;
  case ID_LX_EVALUATE: evaluate_dlg(); break;
  case ID_LX_WATCH: watch_open(); break;
  case ID_LX_WATCHADD: watch_add(); break;
  case ID_LX_WATCHDEL: {
    LONG a = (LONG)lx_xget(wlist, MUIA_List_Active);
    if (a >= 0 && a < nwatches) { memmove(&watches[a], &watches[a + 1], (size_t)(nwatches - a - 1) * sizeof(Watch)); nwatches--; watch_fill(); }
    break; }
  case ID_LX_WATCHPICK: {
    LONG a = (LONG)lx_xget(wlist, MUIA_List_Active);
    if (a >= 0 && a < nwatches && watches[a].b == book) {
      book->active = watches[a].sheet; set(grid, MUIA_Grid_Book, (ULONG)book);
      DoMethod(grid, MUIM_Grid_Select, watches[a].r, watches[a].c, watches[a].r, watches[a].c, watches[a].r, watches[a].c);
      refresh_all();
    }
    break; }
  case ID_LX_CALCSHEET: book_recalc(book); refresh_all(); break;
  case ID_LX_CALCOPTDD:
    if (v == 0 || v == 1) { book->manualcalc = v == 1; if (!book->manualcalc) book_recalc(book); book->dirty = 1; refresh_all(); }
    else lt_command(ID_LX_CALCOPTS, -1);
    break;
  case ID_LX_SPELLING: spelling_open(); break;
  case ID_LX_SPCHANGE: case ID_LX_SPCHANGEALL: case ID_LX_SPIGNORE: case ID_LX_SPIGNOREALL: case ID_LX_SPADD: case ID_LX_SPPICK: spelling_command(id); break;
  case ID_LX_THESAURUS: thesaurus(); break;
  case ID_LX_ACCESS: accessibility(); break;
  case ID_LX_NOTESHOWHIDE: note_show_hide(); break;
  case ID_LX_COMMENTNEW: comment_dlg(); break;
  case ID_LX_COMMENTDEL: {
    Sheet *s = cur_sheet();
    const Note *n = sheet_note(s, s->cur_r, s->cur_c);
    if (n && n->threaded) { book_set_note(book, cur_index(), s->cur_r, s->cur_c, NULL, NULL); refresh_all(); }
    break; }
  case ID_LX_COMMENTPREV: case ID_LX_COMMENTNEXT: comment_go(1, id == ID_LX_COMMENTNEXT); break;
  case ID_LX_COMMENTSHOW: comments_pane(); break;
  case ID_LX_CONVERTNOTES: {
    int n = book_convert_notes(book, -1);
    char m[80];
    sprintf(m, tr("%d notes turned into comments."), n);
    ask("OK", "%s", m);
    refresh_all();
    break; }
  case ID_LX_GOTOSPECIAL: goto_special_dlg(); break;
  }
}
int ft_bit(int b)
{
  Sheet *s;
  Cell *c;
  int i;
  switch (b) {
  case LXB_ARROWS: for (i = 0; i < narrows; i++) if (arrows[i].b == book) return 1; return 0;
  case LXB_HASFORMULA: s = cur_sheet(); c = sheet_cell(s, s->cur_r, s->cur_c); return c && c->f;
  case LXB_COMMENTS: s = cur_sheet(); { const Note *n = sheet_note(s, s->cur_r, s->cur_c); return n && n->threaded; }
  case LXB_NOTESHOWN:
    s = cur_sheet();
    for (i = 0; i < npins; i++) if (pins[i].b == book && pins[i].sheet == cur_index() && pins[i].r == s->cur_r && pins[i].c == s->cur_c) return 1;
    return 0;
  }
  return 0;
}
void ft_update(void)
{
  Rect a = cur_sel();
  /* the selection moved on from what Go To Special selected: its marks go */
  if (marks.c && marks.b == book && (a.r1 != marks.bound.r1 || a.c1 != marks.bound.c1 || a.r2 != marks.bound.r2 || a.c2 != marks.bound.c2)) {
    lx_selection_moved();
    DoMethod(grid, MUIM_Grid_Refresh);
  }
  ft_names_list();
  watch_fill();
}
void ft_init(void) { ft_recent_list(); }
void ft_cleanup(void)
{
  spell_free(&spell);
  free(errs); errs = NULL;
  free(marks.c); marks.c = NULL;
}
