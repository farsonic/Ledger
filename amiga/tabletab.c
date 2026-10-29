/*
 * tabletab.c - tables on the Amiga side (see tabletab.h): Insert > Table and Home > Format as
 * Table, the Table Design tab's commands, the contextual tabs' states, the style gallery's
 * pictures, and the PivotTable Analyze and Design commands that change a pivot table's options.
 */
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <proto/exec.h>
#include <proto/intuition.h>
#include <proto/graphics.h>
#include <proto/muimaster.h>
#include <clib/alib_protos.h>
#include "app.h"
#include "theme.h"
#include "lang.h"
#include "ids.h"
#include "tabletab.h"
#include "../core/table.h"
#include "../core/pivot.h"

static ULONG xget(Object *o, ULONG attr) { ULONG v = 0; get(o, attr, &v); return v; }
static void to_utf8(const char *s, char *u, size_t cap)
{
  size_t o = 0;
  for (; *s && o + 4 < cap; s++) o += (size_t)utf8_put(u + o, (u8)*s);
  u[o] = 0;
}
static void to_latin1(const char *u, char *l, size_t cap) { utf8_to_latin1(u, (u32)strlen(u), l, cap); }

/* the table at the active cell, and its index */
static LTable *table_here(int *ti)
{
  Sheet *s = cur_sheet();
  LTable *t = table_at(book, cur_index(), s->cur_r, s->cur_c);
  if (ti) *ti = t ? table_index(book, cur_index(), t) : -1;
  return t;
}
/* the block of filled cells round the active cell (the selection if it is more than one cell) */
static Rect region(void)
{
  Sheet *s = cur_sheet();
  Rect a = cur_sel();
  int grown = 1;
  if (a.r1 != a.r2 || a.c1 != a.c2) return a;
  a.r1 = a.r2 = s->cur_r; a.c1 = a.c2 = s->cur_c;
  while (grown) {
    i32 r, c;
    grown = 0;
    if (a.r1 > 0) { for (c = a.c1 > 0 ? a.c1 - 1 : 0; c <= a.c2 + 1; c++) if (sheet_cell(s, a.r1 - 1, c) && cell_value(book, cur_index(), a.r1 - 1, c)->t != V_EMPTY) { a.r1--; grown = 1; break; } }
    for (c = a.c1 > 0 ? a.c1 - 1 : 0; c <= a.c2 + 1; c++) if (sheet_cell(s, a.r2 + 1, c) && cell_value(book, cur_index(), a.r2 + 1, c)->t != V_EMPTY) { a.r2++; grown = 1; break; }
    if (a.c1 > 0) { for (r = a.r1 > 0 ? a.r1 - 1 : 0; r <= a.r2 + 1; r++) if (sheet_cell(s, r, a.c1 - 1) && cell_value(book, cur_index(), r, a.c1 - 1)->t != V_EMPTY) { a.c1--; grown = 1; break; } }
    for (r = a.r1 > 0 ? a.r1 - 1 : 0; r <= a.r2 + 1; r++) if (sheet_cell(s, r, a.c2 + 1) && cell_value(book, cur_index(), r, a.c2 + 1)->t != V_EMPTY) { a.c2++; grown = 1; break; }
    if (a.r2 - a.r1 > 100000 || a.c2 - a.c1 > 500) break;
  }
  return a;
}
static void abs_text(Rect a, char *out)
{
  char x[24];
  ref_text(a.r1, a.c1, 1, 1, out);
  if (a.r1 != a.r2 || a.c1 != a.c2) { ref_text(a.r2, a.c2, 1, 1, x); strcat(out, ":"); strcat(out, x); }
}
/* the first row looks like headings: every cell in it is text and something under it isn't empty */
static int looks_headed(Rect a)
{
  i32 c;
  int text = 0;
  for (c = a.c1; c <= a.c2; c++) {
    const Value *v = cell_value(book, cur_index(), a.r1, c);
    if (v->t == V_STR) text = 1;
    else if (v->t != V_EMPTY) return 0;
  }
  return text && a.r2 > a.r1;
}
/* a list to choose one of (the menus' forms of the ribbon's lists); -1 cancelled */
static int choose(const char *title, const char *const *labels, int n, int active)
{
  static const ULONG ends[] = { ID_DLGOK, ID_DLG1, 0 };
  Object *w, *lv, *list;
  const char **tl = (const char **)calloc((size_t)n + 1, sizeof(char *));
  int i, r = -1;
  ULONG id;
  if (!tl) return -1;
  for (i = 0; i < n; i++) tl[i] = tr(labels[i]);
  w = dialog_window(title, VGroup,
        Child, lv = ListviewObject, MUIA_Listview_List, list = ListObject, InputListFrame, MUIA_List_SourceArray, (ULONG)tl, End,
          MUIA_Listview_DoubleClick, TRUE, MUIA_CycleChain, 1, End,
      End, "OK");
  if (!w) { free(tl); return -1; }
  set(list, MUIA_List_Active, active >= 0 ? active : 0);
  DoMethod(lv, MUIM_Notify, MUIA_Listview_DoubleClick, TRUE, (ULONG)app, 2, MUIM_Application_ReturnID, ID_DLG1);
  set(w, MUIA_Window_ActiveObject, (ULONG)lv);
  id = run_modal(w, ends, NULL);
  if (id == ID_DLGOK || id == ID_DLG1) r = (int)(LONG)xget(list, MUIA_List_Active);
  MUI_DisposeObject(w);
  free(tl);
  return r;
}
static const char *const styles_en[] = {
  "Light 1", "Light 2", "Light 3", "Light 4", "Light 5", "Light 6", "Light 7", "Light 8", "Light 9", "Light 10", "Light 11", "Light 12",
  "Light 13", "Light 14", "Light 15", "Light 16", "Light 17", "Light 18", "Light 19", "Light 20", "Light 21",
  "Medium 1", "Medium 2", "Medium 3", "Medium 4", "Medium 5", "Medium 6", "Medium 7", "Medium 8", "Medium 9", "Medium 10", "Medium 11",
  "Medium 12", "Medium 13", "Medium 14", "Medium 15", "Medium 16", "Medium 17", "Medium 18", "Medium 19", "Medium 20", "Medium 21",
  "Medium 22", "Medium 23", "Medium 24", "Medium 25", "Medium 26", "Medium 27", "Medium 28",
  "Dark 1", "Dark 2", "Dark 3", "Dark 4", "Dark 5", "Dark 6", "Dark 7", "Dark 8", "Dark 9", "Dark 10", "Dark 11" };

/* ------------------------------------------------------------------ the gallery */
void tabletab_style_name(int k, int pivot, char *out)
{
  table_style_name(k, out);
  if (pivot) memcpy(out, "Pivot", 5);
}
void tabletab_preview(Object *obj, struct RastPort *rp, int k, LONG x, LONG y, LONG w, LONG h)
{
  struct ColorMap *cm = _screen(obj)->ViewPort.ColorMap;
  char name[32];
  TStyle st;
  LONG pens[8];
  u32 cols[8];
  int np = 0, r, c, i;
  LONG rh = h / 4, cw = w / 4;
  table_style_name(k, name);
  table_style_colors(name, &st);
  /* the colours needed, as pens */
  cols[np++] = st.hfill != NOCOL ? st.hfill : 0xFFFFFF;                     /* 0 header */
  cols[np++] = st.band != NOCOL ? st.band : st.body != NOCOL ? st.body : 0xFFFFFF;   /* 1 band */
  cols[np++] = st.body != NOCOL ? st.body : 0xFFFFFF;                        /* 2 body */
  cols[np++] = st.line != NOCOL ? st.line : st.hline != NOCOL ? st.hline : st.outline != NOCOL ? st.outline : 0xBFBFBF;   /* 3 lines */
  cols[np++] = st.hfont != NOCOL ? st.hfont : 0x000000;                      /* 4 header text */
  cols[np++] = st.bodyfont != NOCOL ? st.bodyfont : 0x595959;                /* 5 body text */
  for (i = 0; i < np; i++)
    pens[i] = ObtainBestPen(cm, ((cols[i] >> 16) & 255) * 0x01010101UL, ((cols[i] >> 8) & 255) * 0x01010101UL, (cols[i] & 255) * 0x01010101UL, OBP_Precision, PRECISION_IMAGE, TAG_DONE);
  for (r = 0; r < 4; r++) {
    LONG y0 = y + r * rh, y1 = y0 + rh - 1;
    LONG pen = r == 0 ? pens[0] : (r % 2 == 1) ? pens[1] : pens[2];
    SetAPen(rp, pen);
    RectFill(rp, x, y0, x + 4 * cw - 1, y1);
    /* a dash of text in each cell */
    SetAPen(rp, r == 0 ? pens[4] : pens[5]);
    for (c = 0; c < 4; c++) { Move(rp, x + c * cw + 1, y0 + rh / 2); Draw(rp, x + c * cw + cw - 3, y0 + rh / 2); }
    if (st.line != NOCOL || (r == 0 && st.hline != NOCOL)) { SetAPen(rp, pens[3]); Move(rp, x, y1); Draw(rp, x + 4 * cw - 1, y1); }
  }
  if (st.outline != NOCOL || st.grid != NOCOL) {
    SetAPen(rp, pens[3]);
    Move(rp, x, y); Draw(rp, x + 4 * cw - 1, y); Draw(rp, x + 4 * cw - 1, y + 4 * rh - 1); Draw(rp, x, y + 4 * rh - 1); Draw(rp, x, y);
    if (st.grid != NOCOL) for (c = 1; c < 4; c++) { Move(rp, x + c * cw, y + rh); Draw(rp, x + c * cw, y + 4 * rh - 1); }
  } else {
    SetAPen(rp, tpen(T_BORDER2));
    Move(rp, x, y); Draw(rp, x + 4 * cw - 1, y); Draw(rp, x + 4 * cw - 1, y + 4 * rh - 1); Draw(rp, x, y + 4 * rh - 1); Draw(rp, x, y);
  }
  for (i = 0; i < np; i++) ReleasePen(cm, pens[i]);
}

/* ------------------------------------------------------------------ making tables */
/* Create Table: the range and whether it has headings; then the table in style (NULL: the default) */
static void create_table(const char *style)
{
  static const ULONG ends[] = { ID_DLGOK, 0 };
  Rect a = region();
  char txt[64];
  Object *w, *str, *chk;
  int headed = looks_headed(a);
  abs_text(a, txt);
  w = dialog_window("Create Table", VGroup,
        Child, LLabel1(tr("Where is the data for your table?")),
        Child, str = StringObject, StringFrame, MUIA_String_Contents, (ULONG)txt, MUIA_String_MaxLen, 60, MUIA_CycleChain, 1, End,
        Child, HGroup,
          Child, chk = ImageObject, ImageButtonFrame, MUIA_InputMode, MUIV_InputMode_Toggle, MUIA_Image_Spec, MUII_CheckMark,
            MUIA_Image_FreeVert, TRUE, MUIA_Selected, headed, MUIA_Background, MUII_ButtonBack, MUIA_ShowSelState, FALSE, MUIA_CycleChain, 1, End,
          Child, LLabel1(tr("My table has headers")), Child, HSpace(0),
        End,
      End, "OK");
  if (!w) return;
  DoMethod(str, MUIM_Notify, MUIA_String_Acknowledge, MUIV_EveryTime, (ULONG)app, 2, MUIM_Application_ReturnID, ID_DLGOK);
  set(w, MUIA_Window_ActiveObject, (ULONG)str);
  if (run_modal(w, ends, NULL) == ID_DLGOK) {
    const char *t = (const char *)xget(str, MUIA_String_Contents);
    u16 sh; Rect r; u8 fl;
    int n = t ? parse_ref(book, t, &sh, &r, &fl) : 0;
    if (n <= 0 || t[n] || (sh != SHEET_SELF && sh != cur_index())) ask("OK", "%s", tr("That isn't a range on this sheet."));
    else if (book_table_add(book, cur_index(), r, xget(chk, MUIA_Selected) != 0, style) < 0)
      ask("OK", "%s", tr("A table can't overlap another table, a PivotTable or merged cells."));
    refresh_all();
  }
  MUI_DisposeObject(w);
}
static void format_as_table(int k)
{
  char name[32];
  int ti;
  if (k < 0 || k >= TSTYLE_N) return;
  table_style_name(k, name);
  if (table_here(&ti)) { book_table_style(book, cur_index(), ti, name); refresh_all(); }
  else create_table(name);
}
static void rename_table(void)
{
  int ti;
  LTable *t = table_here(&ti);
  char buf[64], u[128];
  if (!t) return;
  to_latin1(t->name, buf, sizeof buf);
  if (!ask_string("Table Name", "Table name:", buf, sizeof buf)) return;
  to_utf8(buf, u, sizeof u);
  if (!book_table_rename(book, cur_index(), ti, u)) ask("OK", "%s", tr("That name can't be used: it must start with a letter, have no spaces, not look like a cell, and not be taken."));
  refresh_all();
}
static void resize_table(void)
{
  int ti;
  LTable *t = table_here(&ti);
  char buf[64];
  if (!t) return;
  abs_text(t->r, buf);
  if (!ask_string("Resize Table", "New range for the table:", buf, sizeof buf)) return;
  {
    u16 sh; Rect r; u8 fl;
    int n = parse_ref(book, buf, &sh, &r, &fl);
    if (n <= 0 || buf[n] || !book_table_resize(book, cur_index(), ti, r))
      ask("OK", "%s", tr("The header row must stay in the same row, and the new range must overlap the table."));
  }
  refresh_all();
}

/* ------------------------------------------------------------------ pivot table options */
/* the pivot table at the active cell changed by fn (on a copy of its definition) */
static void pivot_change(void (*fn)(LPivot *p, LONG v), LONG v)
{
  int sh, pi;
  LPivot *p = pivot_here(&sh, &pi), np;
  if (!p) return;
  pivot_copy1(&np, p);
  fn(&np, v);
  book_pivot_set(book, sh, pi, &np);
  pivot_free1(&np);
  refresh_all();
}
static void ch_subtot(LPivot *p, LONG v) { p->subtotals = v != 0; p->subtop = v == 2; }
static void ch_grand(LPivot *p, LONG v) { p->grandrows = v == 1 || v == 2; p->grandcols = v == 1 || v == 3; }
static void ch_layout(LPivot *p, LONG v) { p->layout = (u8)(v == 1 ? PL_OUTLINE : v == 2 ? PL_TABULAR : PL_COMPACT); }
static void ch_blank(LPivot *p, LONG v) { p->blankrows = v == 0; }
static void ch_rowhead(LPivot *p, LONG v) { p->rowheaders = !p->rowheaders; (void)v; }
static void ch_colhead(LPivot *p, LONG v) { p->colheaders = !p->colheaders; (void)v; }
static void ch_bandrows(LPivot *p, LONG v) { p->bandrows = !p->bandrows; (void)v; }
static void ch_bandcols(LPivot *p, LONG v) { p->bandcols = !p->bandcols; (void)v; }
static void ch_style(LPivot *p, LONG v) { tabletab_style_name((int)v, 1, p->style); }
static void ch_clear(LPivot *p, LONG v)
{
  while (p->nrows) pivot_def_remove(p, PA_ROWS, 0);
  while (p->ncols) pivot_def_remove(p, PA_COLS, 0);
  while (p->nfilters) pivot_def_remove(p, PA_FILTERS, 0);
  while (p->nvals) pivot_def_remove(p, PA_VALUES, 0);
  (void)v;
}
static void ch_remove(LPivot *p, LONG v) { pivot_def_remove(p, (int)(v >> 8), (int)(v & 255)); }
static void ch_sort(LPivot *p, LONG v)
{
  int area = (int)(v >> 8) & 255, fi = (int)(v & 255), desc = (int)(v >> 16);
  PivotField *f = area == PA_ROWS ? p->rows : area == PA_COLS ? p->cols : NULL;
  int n = area == PA_ROWS ? p->nrows : area == PA_COLS ? p->ncols : 0;
  if (f && fi < n) f[fi].sort = (u8)(desc ? PO_DESC : PO_ASC);
}
/* Move Field: 0 up, 1 down, 2 to Filters, 3 Rows, 4 Columns, 5 Values */
static int mv_area, mv_index;
static void ch_move(LPivot *p, LONG v)
{
  int n = mv_area == PA_ROWS ? p->nrows : mv_area == PA_COLS ? p->ncols : mv_area == PA_FILTERS ? p->nfilters : p->nvals;
  switch (v) {
  case 0: if (mv_index > 0) pivot_def_move(p, mv_area, mv_index, mv_area, mv_index - 1); break;
  case 1: if (mv_index + 1 < n) pivot_def_move(p, mv_area, mv_index, mv_area, mv_index + 1); break;
  case 2: pivot_def_move(p, mv_area, mv_index, PA_FILTERS, 0); break;
  case 3: pivot_def_move(p, mv_area, mv_index, PA_ROWS, 0); break;
  case 4: pivot_def_move(p, mv_area, mv_index, PA_COLS, 0); break;
  case 5: pivot_def_move(p, mv_area, mv_index, PA_VALUES, 0); break;
  }
}
static void pivot_rename(void)
{
  int sh, pi, s, i;
  LPivot *p = pivot_here(&sh, &pi);
  char buf[64], u[128];
  if (!p) return;
  to_latin1(p->name, buf, sizeof buf);
  if (!ask_string("PivotTable Name", "PivotTable name:", buf, sizeof buf) || !buf[0]) return;
  to_utf8(buf, u, sizeof u);
  for (s = 0; s < book->nsh; s++)
    for (i = 0; i < book->sh[s]->npivots; i++)
      if ((s != sh || i != pi) && str_ieq(book->sh[s]->pivots[i].name, u)) { ask("OK", "%s", tr("Another PivotTable has that name.")); return; }
  book_begin(book);
  book_note_sheet(book, sh);
  snprintf(book->sh[sh]->pivots[pi].name, sizeof book->sh[sh]->pivots[pi].name, "%s", u);
  book->dirty = 1;
  book_end(book);
  refresh_all();
}
static void pivot_source_change(void)
{
  int sh, pi;
  LPivot *p = pivot_here(&sh, &pi), np;
  char buf[256], u[256];
  int ss;
  Rect r;
  if (!p) return;
  to_latin1(p->source, buf, sizeof buf);
  if (!ask_string("Change PivotTable Data Source", "Table/Range:", buf, sizeof buf)) return;
  to_utf8(buf, u, sizeof u);
  pivot_copy1(&np, p);
  snprintf(np.source, sizeof np.source, "%s", u[0] == '=' ? u + 1 : u);
  if (!pivot_source(book, &np, &ss, &r)) ask("OK", "%s", tr("That isn't a range or a table."));
  else book_pivot_set(book, sh, pi, &np);
  pivot_free1(&np);
  refresh_all();
}

/* ------------------------------------------------------------------ commands */
void tabletab_command(ULONG id, LONG v)
{
  int ti, sh, pi, area, fi;
  LTable *t = table_here(&ti);
  LPivot *p;
  switch (id) {
  case ID_TBLNEW: if (t) ask("OK", "%s", tr("The active cell is in a table already.")); else create_table(NULL); break;
  case ID_FMTTABLEDD: case ID_TBLSTYLEDD: format_as_table((int)v); break;
  case ID_TBLSTYLEMENU: { int k = choose("Table Styles", styles_en, TSTYLE_N, t ? table_style_index(t->style) : 22); if (k >= 0) format_as_table(k); break; }
  case ID_PIVOTNEW:
    if (t) pivot_dialog(t->name);
    else { char src[80]; Rect a = region(); abs_text(a, src); pivot_dialog(src); }
    break;
  case ID_TBLNAME: rename_table(); break;
  case ID_TBLRESIZE: resize_table(); break;
  case ID_TBLPIVOT: if (t) pivot_dialog(t->name); break;
  case ID_TBLDEDUPE:
    if (t) {
      char msg[120];
      int n = book_table_dedupe(book, cur_index(), ti, 0);
      snprintf(msg, sizeof msg, tr("%d duplicate rows were removed."), n);
      refresh_all();
      ask("OK", "%s", msg);
    }
    break;
  case ID_TBLCONVERT:
    if (t && ask("Yes|No", "%s", tr("Convert the table to an ordinary range of cells?"))) { book_table_convert(book, cur_index(), ti); refresh_all(); }
    break;
  case ID_TBLHEADER: case ID_TBLTOTALS: case ID_TBLBANDROWS: case ID_TBLBANDCOLS: case ID_TBLFIRSTCOL: case ID_TBLLASTCOL: case ID_TBLFILTERBTN:
    if (t) {
      static const struct { ULONG id; int opt; } map[] = { { ID_TBLHEADER, TO_HEADER }, { ID_TBLTOTALS, TO_TOTALS }, { ID_TBLBANDROWS, TO_BANDROWS },
        { ID_TBLBANDCOLS, TO_BANDCOLS }, { ID_TBLFIRSTCOL, TO_FIRSTCOL }, { ID_TBLLASTCOL, TO_LASTCOL }, { ID_TBLFILTERBTN, TO_FILTERBTN } };
      int k, on = 0;
      for (k = 0; k < 7; k++) if (map[k].id == id) {
        switch (map[k].opt) {
        case TO_HEADER: on = !t->header; break; case TO_TOTALS: on = !t->totals; break; case TO_BANDROWS: on = !t->bandrows; break;
        case TO_BANDCOLS: on = !t->bandcols; break; case TO_FIRSTCOL: on = !t->firstcol; break; case TO_LASTCOL: on = !t->lastcol; break;
        default: on = !t->filterbtn; break;
        }
        book_table_option(book, cur_index(), ti, map[k].opt, on);
      }
    }
    refresh_all();
    break;
  case ID_TBLTOTALDD:
    if (t) {
      i32 c = cur_sheet()->cur_c;
      if (!t->totals) book_table_option(book, cur_index(), ti, TO_TOTALS, 1);
      book_table_total(book, cur_index(), ti, (int)(c - book->sh[cur_index()]->tables[ti].r.c1), (int)v);
      refresh_all();
    }
    break;
  case ID_PVNAME: pivot_rename(); break;
  case ID_PVFIELDS: case ID_PVADDFIELD: if (pivot_here(&sh, &pi)) pivot_fields_pane(); break;
  case ID_PVREFRESH:
    if ((p = pivot_here(&sh, &pi))) {
      const char *why;
      if (!book_pivot_refresh(book, sh, pi, &why)) ask("OK", "%s", tr(why));
      refresh_all();
    }
    break;
  case ID_PVSOURCE: pivot_source_change(); break;
  case ID_PVREMOVEFIELD: if (pivot_field_here(&area, &fi)) pivot_change(ch_remove, (LONG)(area << 8 | fi)); break;
  case ID_PVMOVEDD: if (pivot_field_here(&mv_area, &mv_index)) pivot_change(ch_move, v); break;
  case ID_PVVALUESET:
    if ((p = pivot_here(&sh, &pi))) {
      int vi = 0;
      if (pivot_field_here(&area, &fi) && area == PA_VALUES) vi = fi;
      if (p->nvals) { if (pivot_value_settings(sh, pi, vi)) refresh_all(); }
      else ask("OK", "%s", tr("The PivotTable has no fields in Values yet."));
    }
    break;
  case ID_PVFILTER:
    if (pivot_here(&sh, &pi)) {
      if (pivot_field_here(&area, &fi) && area != PA_VALUES) { if (pivot_field_items(sh, pi, area, fi)) refresh_all(); }
      else pivot_fields_pane();
    }
    break;
  case ID_PVSORTAZ: case ID_PVSORTZA:
    if (pivot_field_here(&area, &fi) && (area == PA_ROWS || area == PA_COLS)) pivot_change(ch_sort, (LONG)((id == ID_PVSORTZA) << 16 | area << 8 | fi));
    break;
  case ID_PVCLEAR: pivot_change(ch_clear, 0); break;
  case ID_PVDELETE:
    if ((p = pivot_here(&sh, &pi)) && ask("Yes|No", "%s", tr("Delete the PivotTable and its report?"))) { book_pivot_delete(book, sh, pi); refresh_all(); }
    break;
  case ID_PVSUBTOTDD: pivot_change(ch_subtot, v); break;
  case ID_PVGRANDDD: pivot_change(ch_grand, v); break;
  case ID_PVLAYOUTDD: pivot_change(ch_layout, v); break;
  case ID_PVBLANKDD: pivot_change(ch_blank, v); break;
  case ID_PVROWHEAD: pivot_change(ch_rowhead, 0); break;
  case ID_PVCOLHEAD: pivot_change(ch_colhead, 0); break;
  case ID_PVBANDROWS: pivot_change(ch_bandrows, 0); break;
  case ID_PVBANDCOLS: pivot_change(ch_bandcols, 0); break;
  case ID_PVSTYLEDD: pivot_change(ch_style, v); break;
  case ID_PVSUBTOTMENU: case ID_PVGRANDMENU: case ID_PVLAYOUTMENU: case ID_PVBLANKMENU: {
    static const char *const sub[] = { "Do Not Show Subtotals", "Show All Subtotals at Bottom of Group", "Show All Subtotals at Top of Group" };
    static const char *const grand[] = { "Off for Rows and Columns", "On for Rows and Columns", "On for Rows Only", "On for Columns Only" };
    static const char *const lay[] = { "Show in Compact Form", "Show in Outline Form", "Show in Tabular Form" };
    static const char *const blank[] = { "Insert Blank Line after Each Item", "Remove Blank Line after Each Item" };
    int k;
    if (!pivot_here(&sh, &pi)) break;
    if (id == ID_PVSUBTOTMENU) { if ((k = choose("Subtotals", sub, 3, 2)) >= 0) pivot_change(ch_subtot, k); }
    else if (id == ID_PVGRANDMENU) { if ((k = choose("Grand Totals", grand, 4, 1)) >= 0) pivot_change(ch_grand, k); }
    else if (id == ID_PVLAYOUTMENU) { if ((k = choose("Report Layout", lay, 3, 0)) >= 0) pivot_change(ch_layout, k); }
    else if ((k = choose("Blank Rows", blank, 2, 1)) >= 0) pivot_change(ch_blank, k);
    break; }
  }
}

/* ------------------------------------------------------------------ the ribbon's and menus' states */
static void menu_tick(ULONG id, int on, int enable)
{
  Object *strip = win ? (Object *)xget(win, MUIA_Window_Menustrip) : NULL, *it;
  if (strip && (it = (Object *)DoMethod(strip, MUIM_FindUData, id))) { set(it, MUIA_Menuitem_Checked, on); set(it, MUIA_Menuitem_Enabled, enable); }
}
void tabletab_state(ULONG *ctx, const char **name)
{
  static char nm[64];
  Sheet *s = cur_sheet();
  LTable *t = table_at(book, cur_index(), s->cur_r, s->cur_c);
  int sh, pi, area, fi;
  LPivot *p = pivot_here(&sh, &pi);
  *ctx = 0;
  nm[0] = 0;
  if (t) {
    *ctx |= CBIT(CB_INTABLE);
    if (t->header) *ctx |= CBIT(CB_HEADER);
    if (t->totals) *ctx |= CBIT(CB_TOTALS);
    if (t->bandrows) *ctx |= CBIT(CB_BANDROWS);
    if (t->bandcols) *ctx |= CBIT(CB_BANDCOLS);
    if (t->firstcol) *ctx |= CBIT(CB_FIRSTCOL);
    if (t->lastcol) *ctx |= CBIT(CB_LASTCOL);
    if (t->filterbtn) *ctx |= CBIT(CB_FILTERBTN);
    to_latin1(t->name, nm, sizeof nm);
  }
  if (p) {
    *ctx |= CBIT(CB_INPIVOT);
    if (p->rowheaders) *ctx |= CBIT(CB_PVROWHEAD);
    if (p->colheaders) *ctx |= CBIT(CB_PVCOLHEAD);
    if (p->bandrows) *ctx |= CBIT(CB_PVBANDROWS);
    if (p->bandcols) *ctx |= CBIT(CB_PVBANDCOLS);
    if (pivot_field_here(&area, &fi)) *ctx |= CBIT(CB_PVFIELD) | (area == PA_VALUES ? CBIT(CB_PVVALUE) : 0);
    if (!t) to_latin1(p->name, nm, sizeof nm);
  }
  *name = nm;
  /* the menu items */
  menu_tick(ID_TBLHEADER, t && t->header, t != NULL);
  menu_tick(ID_TBLTOTALS, t && t->totals, t != NULL);
  menu_tick(ID_TBLBANDROWS, t && t->bandrows, t != NULL);
  menu_tick(ID_TBLBANDCOLS, t && t->bandcols, t != NULL);
  menu_tick(ID_TBLFIRSTCOL, t && t->firstcol, t != NULL);
  menu_tick(ID_TBLLASTCOL, t && t->lastcol, t != NULL);
  menu_tick(ID_TBLFILTERBTN, t && t->filterbtn, t != NULL);
  menu_tick(ID_PVROWHEAD, p && p->rowheaders, p != NULL);
  menu_tick(ID_PVCOLHEAD, p && p->colheaders, p != NULL);
  menu_tick(ID_PVBANDROWS, p && p->bandrows, p != NULL);
  menu_tick(ID_PVBANDCOLS, p && p->bandcols, p != NULL);
}
