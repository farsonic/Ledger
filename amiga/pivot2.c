/*
 * pivot2.c - Recommended PivotTables, PivotCharts, slicers, calculated fields, grouping and
 * expand/collapse on the Amiga side (see pivot2.h): their commands and dialogs, the pivot tables'
 * +/- buttons on the grid, and the slicers drawn and clicked over it.
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
#include "chartview.h"
#include "datatab.h"
#include "pivot2.h"
#include "../core/table.h"
#include "../core/pivot.h"
#include "../core/slicer.h"
/* (the command ranges mustn't meet) */
typedef char p2_ids_clear_of_tables[ID_TBL_LAST <= ID_P2_FIRST ? 1 : -1];

static ULONG xget(Object *o, ULONG attr) { ULONG v = 0; get(o, attr, &v); return v; }
static void to_utf8(const char *s, char *u, size_t cap)
{
  size_t o = 0;
  for (; *s && o + 4 < cap; s++) o += (size_t)utf8_put(u + o, (u8)*s);
  u[o] = 0;
}
static void to_latin1(const char *u, char *l, size_t cap) { utf8_to_latin1(u, (u32)strlen(u), l, cap); }
static Object *check_obj(int on)
{
  return ImageObject, ImageButtonFrame, MUIA_InputMode, MUIV_InputMode_Toggle, MUIA_Image_Spec, MUII_CheckMark,
    MUIA_Image_FreeVert, TRUE, MUIA_Selected, on, MUIA_Background, MUII_ButtonBack, MUIA_ShowSelState, FALSE, MUIA_CycleChain, 1, End;
}
static Object *string_obj(const char *init, int max)
{
  return StringObject, StringFrame, MUIA_String_Contents, (ULONG)init, MUIA_String_MaxLen, max, MUIA_CycleChain, 1, End;
}
static void say(const char *msg) { ask("OK", "%s", tr(msg)); }

/* ------------------------------------------------------------------ icons */
static const char *const icp[ICP_N - ICP_BASE] = {
  /* RECPIVOT: a report and a star */ "ci R 1 1 12 12 ca F 2 2 11 4 F 2 5 4 11 cd L 5 8 11 8 cy P 6 12 9 13 11 15 11 13 12 14 15 12 13 E 12 11 1",
  /* PIVOTCHART: columns over a report's corner */ "ci L 1 15 15 15 ca F 2 9 4 14 F 6 5 8 14 F 10 7 12 14 cd R 9 1 15 5 L 9 3 15 3 L 11 1 11 5",
  /* SLICER: a box of buttons, two picked */ "ci R 1 1 14 14 L 2 4 13 4 ca F 3 6 12 7 cd R 3 9 12 10 ca F 3 12 12 13",
  /* SLICERSET: a slicer and a gear */ "ci R 1 1 10 12 L 2 4 9 4 ca F 3 6 8 7 cd R 3 9 8 10 co O 12 12 3 E 12 12 1 L 12 8 12 9 L 12 15 12 16 L 8 12 9 12 L 15 12 16 12",
  /* CALCFIELD: fx over a column of values */ "ci R 1 1 14 14 ca L 4 4 4 11 L 4 4 7 4 L 4 7 6 7 co L 8 7 12 11 L 12 7 8 11",
  /* GROUPFIELD: items held by a bracket */ "cd L 7 3 14 3 L 7 7 14 7 L 7 11 14 11 ca L 3 2 3 12 L 3 2 5 2 L 3 12 5 12 L 1 7 3 7",
  /* UNGROUPFIELD: items, the bracket broken */ "cd L 7 3 14 3 L 7 7 14 7 L 7 11 14 11 ca L 3 2 3 5 L 3 9 3 12 L 3 2 5 2 L 3 12 5 12 cr L 1 14 5 14",
  /* EXPAND: a plus in a box over rows */ "ci R 1 1 8 8 ca L 3 4 6 4 L 4 3 4 6 cd L 10 3 15 3 L 4 11 15 11 L 4 14 15 14",
  /* COLLAPSE: a minus in a box over a row */ "ci R 1 1 8 8 ca L 3 4 6 4 cd L 10 3 15 3 L 4 12 15 12",
};
const char *pivot2_icon_desc(int icon) { return icon >= ICP_BASE && icon < ICP_N ? icp[icon - ICP_BASE] : NULL; }

/* ------------------------------------------------------------------ helpers */
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
    if (a.r1 > 0) { for (c = a.c1 > 0 ? a.c1 - 1 : 0; c <= a.c2 + 1; c++) if (cell_value(book, cur_index(), a.r1 - 1, c)->t != V_EMPTY) { a.r1--; grown = 1; break; } }
    for (c = a.c1 > 0 ? a.c1 - 1 : 0; c <= a.c2 + 1; c++) if (cell_value(book, cur_index(), a.r2 + 1, c)->t != V_EMPTY) { a.r2++; grown = 1; break; }
    if (a.c1 > 0) { for (r = a.r1 > 0 ? a.r1 - 1 : 0; r <= a.r2 + 1; r++) if (cell_value(book, cur_index(), r, a.c1 - 1)->t != V_EMPTY) { a.c1--; grown = 1; break; } }
    for (r = a.r1 > 0 ? a.r1 - 1 : 0; r <= a.r2 + 1; r++) if (cell_value(book, cur_index(), r, a.c2 + 1)->t != V_EMPTY) { a.c2++; grown = 1; break; }
    if (a.r2 - a.r1 > 100000 || a.c2 - a.c1 > 500) break;
  }
  return a;
}
/* the source of the data round the active cell: its table's name, or Sheet!$A$1:$D$9 (UTF-8) */
static int data_source(char *out, size_t cap)
{
  Sheet *s = cur_sheet();
  LTable *t = table_at(book, cur_index(), s->cur_r, s->cur_c);
  Rect a;
  char x[24], y[24];
  const char *q;
  size_t o = 0;
  if (t) { snprintf(out, cap, "%s", t->name); return 1; }
  a = region();
  if (a.r2 <= a.r1) return 0;
  out[o++] = '\'';
  for (q = s->name; *q && o + 4 < cap; q++) { if (*q == '\'') out[o++] = '\''; out[o++] = *q; }
  out[o] = 0;
  ref_text(a.r1, a.c1, 1, 1, x); ref_text(a.r2, a.c2, 1, 1, y);
  snprintf(out + o, cap - o, "'!%s:%s", x, y);
  return 1;
}
static ObjView *OV(void) { return (ObjView *)xget(grid, MUIA_Grid_ObjView); }
static DObj *selected_slicer(int *obj)
{
  int k = ov_selected(OV());
  DObj *o = drw_obj(cur_sheet(), k);
  if (obj) *obj = k;
  return o && o->kind == OB_SLICER && o->slicer ? o : NULL;
}
static void show_sheet(int sh, i32 r, i32 c)
{
  book->active = sh;
  set(grid, MUIA_Grid_Book, (ULONG)book);
  DoMethod(grid, MUIM_Grid_Select, r, c, r, c, r, c);
  refresh_all();
}

/* ------------------------------------------------------------------ Recommended PivotTables */
#define NSUGG 7
static Object *rp_list, *rp_prev;
static PivotSuggest *rp_sg; static int rp_n;
static char rp_text[2400];
/* the preview of suggestion k: its report's first rows, in columns of the fixed font */
static void rp_show(void)
{
  LONG k = (LONG)xget(rp_list, MUIA_List_Active);
  char **cells;
  int rows, cols, r, c;
  size_t o = 0;
  rp_text[0] = 0;
  if (k >= 0 && k < rp_n && pivot_preview(book, &rp_sg[k].def, 10, &cells, &rows, &cols)) {
    for (r = 0; r < rows; r++) {
      for (c = 0; c < cols && c < 4; c++) {
        char l[64];
        int w = c ? 13 : 14, n;
        to_latin1(cells[(long)r * cols + c], l, sizeof l);
        n = (int)strlen(l);
        if (n > w - 1) { l[w - 1] = 0; n = w - 1; }
        if (o + w + 2 >= sizeof rp_text) break;
        if (c) { memset(rp_text + o, ' ', (size_t)(w - 1 - n)); o += (size_t)(w - 1 - n); memcpy(rp_text + o, l, (size_t)n); o += (size_t)n; rp_text[o++] = ' '; }
        else { memcpy(rp_text + o, l, (size_t)n); o += (size_t)n; memset(rp_text + o, ' ', (size_t)(w - n)); o += (size_t)(w - n); }
      }
      if (o + 2 < sizeof rp_text) rp_text[o++] = '\n';
    }
    rp_text[o] = 0;
    for (r = 0; r < rows * cols; r++) free(cells[r]);
    free(cells);
  }
  set(rp_prev, MUIA_Text_Contents, (ULONG)rp_text);
}
static void rp_each(ULONG id) { if (id == ID_DLG1) rp_show(); }
static void recommended(void)
{
  static const ULONG ends[] = { ID_DLGOK, 0 };
  char src[300], l[300];
  PivotSuggest *sg;
  Object *w;
  int n, i;
  {
    /* in a PivotTable: its source */
    LPivot *pv = pivot_here(NULL, NULL);
    if (pv) snprintf(src, sizeof src, "%s", pv->source);
    else if (!data_source(src, sizeof src)) { say("Select a cell in a range with headings, or in a table, first."); return; }
  }
  if (!(sg = (PivotSuggest *)calloc(NSUGG, sizeof(PivotSuggest)))) return;
  n = pivot_recommend(book, cur_index(), src, sg, NSUGG);
  if (!n) { free(sg); say("Ledger has no PivotTables to recommend for this data."); return; }
  rp_sg = sg; rp_n = n; rp_text[0] = 0;
  w = dialog_window("Recommended PivotTables", HGroup,
        Child, VGroup, MUIA_Weight, 45,
          Child, LLabel1(tr("Choose a PivotTable:")),
          Child, ListviewObject, MUIA_Listview_List, rp_list = ListObject, InputListFrame, MUIA_List_ConstructHook, MUIV_List_ConstructHook_String,
            MUIA_List_DestructHook, MUIV_List_DestructHook_String, MUIA_List_MinLineHeight, 0, MUIA_FixWidthTxt, (ULONG)"Sum of Sales by Region and Prod", End, MUIA_CycleChain, 1, End,
        End,
        Child, VGroup, MUIA_Weight, 55,
          Child, LLabel1(tr("Preview:")),
          Child, rp_prev = TextObject, TextFrame, MUIA_Background, MUII_TextBack, MUIA_Font, MUIV_Font_Fixed, MUIA_Text_SetVMax, FALSE,
            MUIA_FixHeightTxt, (ULONG)"\n\n\n\n\n\n\n\n\n\n", MUIA_FixWidthTxt, (ULONG)"MMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMM", End,
        End,
      End, "OK");
  if (!w) { for (i = 0; i < n; i++) pivot_free1(&sg[i].def); free(sg); return; }
  for (i = 0; i < n; i++) { to_latin1(sg[i].title, l, sizeof l); DoMethod(rp_list, MUIM_List_InsertSingle, (ULONG)l, MUIV_List_Insert_Bottom); }
  DoMethod(rp_list, MUIM_Notify, MUIA_List_Active, MUIV_EveryTime, (ULONG)app, 2, MUIM_Application_ReturnID, ID_DLG1);
  set(rp_list, MUIA_List_Active, 0);
  rp_show();
  if (run_modal(w, ends, rp_each) == ID_DLGOK) {
    LONG k = (LONG)xget(rp_list, MUIA_List_Active);
    if (k >= 0 && k < n) {
      /* on a new sheet, as Excel puts it */
      int sh = book_add_sheet(book, NULL, cur_index()), pi;
      if (sh >= 0 && (pi = book_pivot_add(book, sh, src, 2, 0)) >= 0) {
        LPivot np;
        pivot_copy1(&np, &sg[k].def);
        strcpy(np.name, book->sh[sh]->pivots[pi].name);
        np.r = 2; np.c = 0;
        book_pivot_set(book, sh, pi, &np);
        pivot_free1(&np);
        show_sheet(sh, 2, 0);
      } else if (sh >= 0) book_delete_sheet(book, sh);
    }
  }
  MUI_DisposeObject(w);
  for (i = 0; i < n; i++) pivot_free1(&sg[i].def);
  free(sg);
  rp_sg = NULL; rp_n = 0;
}

/* ------------------------------------------------------------------ PivotChart */
static void pivot_chart(void)
{
  static const ULONG ends[] = { ID_DLGOK, ID_DLG1, 0 };
  static const char *const kinds_en[] = { "Clustered Column", "Stacked Column", "Clustered Bar", "Line", "Line with Markers", "Pie", "Doughnut", "Area", "Radar", NULL };
  static const int kinds[] = { CK_COL, CK_COLSTACK, CK_BAR, CK_LINE, CK_LINEMARK, CK_PIE, CK_DOUGHNUT, CK_AREA, CK_RADAR };
  const char *lab[10];
  int sheet, pi, i, k;
  LPivot *p = pivot_here(&sheet, &pi);
  Object *w, *list, *lv;
  ULONG id;
  const char *why;
  if (!p) { say("Select a cell in a PivotTable first: the PivotChart shows its report."); return; }
  for (i = 0; kinds_en[i]; i++) lab[i] = tr(kinds_en[i]);
  lab[i] = NULL;
  w = dialog_window("Insert PivotChart", VGroup,
        Child, LLabel1(tr("Chart type:")),
        Child, lv = ListviewObject, MUIA_Listview_List, list = ListObject, InputListFrame, MUIA_List_SourceArray, (ULONG)lab, MUIA_FixWidthTxt, (ULONG)"Line with Markers MM", End,
          MUIA_Listview_DoubleClick, TRUE, MUIA_CycleChain, 1, End,
      End, "OK");
  if (!w) return;
  set(list, MUIA_List_Active, 0);
  DoMethod(lv, MUIM_Notify, MUIA_Listview_DoubleClick, TRUE, (ULONG)app, 2, MUIM_Application_ReturnID, ID_DLG1);
  id = run_modal(w, ends, NULL);
  k = (int)(LONG)xget(list, MUIA_List_Active);
  MUI_DisposeObject(w);
  if ((id != ID_DLGOK && id != ID_DLG1) || k < 0 || k >= (int)(sizeof kinds / sizeof kinds[0])) return;
  if (book_pivot_chart_add(book, sheet, pi, kinds[k], &why) < 0) { say(why); return; }
  refresh_all();
}

/* ------------------------------------------------------------------ slicers */
static void insert_slicer(void)
{
  static const ULONG ends[] = { ID_DLGOK, 0 };
  Sheet *s = cur_sheet();
  LTable *t = table_at(book, cur_index(), s->cur_r, s->cur_c);
  int sheet, pi, n, i, made = 0;
  LPivot *p = t ? NULL : pivot_here(&sheet, &pi);
  char **f = NULL, (*l1)[64] = NULL, srcname[256];
  const char **lab = NULL;
  UBYTE *on = NULL;
  Object *w, *tick, *sc;
  i32 r, c;
  if (!t && !p) { say("Select a cell in a table or a PivotTable first: a slicer filters its data."); return; }
  if (t) {
    if (!t->header) { say("The table needs a header row for slicers."); return; }
    n = t->ncols;
    if ((f = (char **)calloc((size_t)n + 1, sizeof(char *)))) for (i = 0; i < n; i++) f[i] = str_dup(t->cols[i].name ? t->cols[i].name : "");
    snprintf(srcname, sizeof srcname, "%s", t->name);
    r = t->r.r1; c = t->r.c2 + 2;
  } else {
    n = pivot_fields(book, p, &f);
    snprintf(srcname, sizeof srcname, "%s", p->name);
    r = p->out.r1 >= 0 ? p->out.r1 : p->r; c = (p->out.r1 >= 0 ? p->out.c2 : p->c) + 2;
  }
  if (!f || !n) { pivot_strings_free(f, n); return; }
  l1 = calloc((size_t)n + 1, sizeof *l1);
  lab = (const char **)calloc((size_t)n + 1, sizeof *lab);
  on = (UBYTE *)calloc((size_t)n + 1, 1);
  if (!l1 || !lab || !on) goto out;
  for (i = 0; i < n; i++) { to_latin1(f[i], l1[i], sizeof l1[i]); lab[i] = l1[i]; }
  sc = NewObject(flatscroll_class()->mcc_Class, NULL, TAG_DONE);
  w = dialog_window("Insert Slicers", VGroup,
        Child, LLabel1(tr("Tick the fields to make a slicer for:")),
        Child, HGroup, MUIA_Group_Spacing, 0,
          Child, tick = NewObject(ticklist_class()->mcc_Class, NULL, MUIA_Tick_Labels, (ULONG)lab, MUIA_Tick_States, (ULONG)on,
                                  MUIA_Tick_Count, n, MUIA_Tick_Prop, (ULONG)sc, TAG_DONE),
          Child, sc,
        End,
      End, "OK");
  if (!w) goto out;
  DoMethod(sc, MUIM_Notify, MUIA_Prop_First, MUIV_EveryTime, (ULONG)tick, 3, MUIM_Set, MUIA_Tick_First, MUIV_TriggerValue);
  if (run_modal(w, ends, NULL) == ID_DLGOK) {
    int sh = t ? cur_index() : sheet;
    book_begin(book);
    for (i = 0; i < n; i++)
      if (on[i]) {
        /* each one a little below and right of the one before, as Excel cascades them */
        int k = book_slicer_add(book, sh, p != NULL, srcname, f[i], r + made * 2, c + made);
        if (k >= 0) made++;
      }
    book_end(book);
    if (made) refresh_all();
  }
  MUI_DisposeObject(w);
out:
  free(l1); free(lab); free(on);
  pivot_strings_free(f, n);
}
static void slicer_settings(void)
{
  static const ULONG ends[] = { ID_DLGOK, 0 };
  static const char *sorts_en[] = { "Ascending (A to Z)", "Descending (Z to A)", "Data source order", NULL };
  const char *sorts[4];
  int obj, i;
  DObj *o = selected_slicer(&obj);
  Slicer ns;
  char name[64], cap[160], cols[8], src[300], fld[300], info[640];
  Object *w, *nm, *cp, *co, *so, *hd;
  if (!o) { say("Select a slicer first: click its frame."); return; }
  ns = *o->slicer;
  for (i = 0; i < 3; i++) sorts[i] = tr(sorts_en[i]);
  sorts[3] = NULL;
  to_latin1(ns.name, name, sizeof name);
  to_latin1(ns.caption, cap, sizeof cap);
  to_latin1(ns.source, src, sizeof src);
  to_latin1(ns.field, fld, sizeof fld);
  snprintf(info, sizeof info, "%s %s   %s %s", tr("Source:"), src, tr("Field:"), fld);
  sprintf(cols, "%d", (int)ns.cols);
  w = dialog_window("Slicer Settings", VGroup,
        Child, LLabel1(info),
        Child, ColGroup(2),
          Child, QLabel(tr("Name:"), MUIO_Label_SingleFrame), Child, nm = string_obj(name, 40),
          Child, QLabel(tr("Caption:"), MUIO_Label_SingleFrame), Child, cp = string_obj(cap, 120),
          Child, QLabel(tr("Columns:"), MUIO_Label_SingleFrame), Child, co = StringObject, StringFrame, MUIA_String_Contents, (ULONG)cols, MUIA_String_MaxLen, 4, MUIA_String_Accept, (ULONG)"0123456789", MUIA_CycleChain, 1, End,
          Child, QLabel(tr("Sort items:"), MUIO_Label_SingleFrame), Child, so = CycleObject, MUIA_Cycle_Entries, (ULONG)sorts, MUIA_Cycle_Active, ns.sort % 3, MUIA_CycleChain, 1, End,
          Child, QLabel(tr("Display header:"), MUIO_Label_SingleFrame), Child, HGroup, Child, hd = check_obj(ns.header), Child, HSpace(0), End,
        End,
      End, "OK");
  if (!w) return;
  if (run_modal(w, ends, NULL) == ID_DLGOK) {
    Slicer *c = slicer_copy(o->slicer);
    char u[300];
    if (c) {
      to_utf8((const char *)xget(nm, MUIA_String_Contents), u, sizeof u);
      if (u[0]) snprintf(c->name, sizeof c->name, "%s", u);
      to_utf8((const char *)xget(cp, MUIA_String_Contents), u, sizeof u);
      snprintf(c->caption, sizeof c->caption, "%s", u);
      c->cols = (u8)atoi((const char *)xget(co, MUIA_String_Contents));
      c->sort = (u8)xget(so, MUIA_Cycle_Active);
      c->header = (u8)(xget(hd, MUIA_Selected) ? 1 : 0);
      book_slicer_set(book, cur_index(), obj, c);
      slicer_free(c);
      refresh_all();
    }
  }
  MUI_DisposeObject(w);
}

/* ------------------------------------------------------------------ calculated fields */
static Object *cf_formula, *cf_fields, *cf_name;
static char **cf_names; static int cf_n;
static void cf_each(ULONG id)
{
  LONG k;
  char t[600], l[300];
  const char *cur;
  if (id != ID_DLG1) return;
  k = (LONG)xget(cf_fields, MUIA_List_Active);
  if (k < 0 || k >= cf_n) return;
  /* the field put in at the formula's end (quoted when it has spaces) */
  cur = (const char *)xget(cf_formula, MUIA_String_Contents);
  to_latin1(cf_names[k], l, sizeof l);
  snprintf(t, sizeof t, strchr(l, ' ') ? "%s'%s'" : "%s%s", cur ? cur : "=", l);
  set(cf_formula, MUIA_String_Contents, (ULONG)t);
}
static void calc_field(void)
{
  static const ULONG ends[] = { ID_DLGOK, ID_DLG2, 0 };
  int sheet, pi, i;
  LPivot *p = pivot_here(&sheet, &pi), np;
  Object *w, *lv, *ins, *del;
  char nm[64], l[300];
  ULONG id;
  if (!p) { say("Select a cell in a PivotTable first."); return; }
  cf_n = pivot_fields(book, p, &cf_names);
  sprintf(nm, "Field%d", p->ncalcs + 1);
  w = dialog_window("Insert Calculated Field", VGroup,
        Child, ColGroup(2),
          Child, QLabel(tr("Name:"), MUIO_Label_SingleFrame), Child, cf_name = string_obj(nm, 60),
          Child, QLabel(tr("Formula:"), MUIO_Label_SingleFrame), Child, cf_formula = string_obj("= 0", 500),
        End,
        Child, LLabel1(tr("Fields (double-click to put one in the formula):")),
        Child, lv = ListviewObject, MUIA_Listview_List, cf_fields = ListObject, InputListFrame, MUIA_List_ConstructHook, MUIV_List_ConstructHook_String,
          MUIA_List_DestructHook, MUIV_List_DestructHook_String, End, MUIA_Listview_DoubleClick, TRUE, MUIA_CycleChain, 1, End,
        Child, HGroup, Child, ins = QButton(tr("Insert Field")), Child, del = QButton(tr("Delete")), Child, HSpace(0), End,
        Child, LLabel1(tr("The formula works on each cell's sums of the fields it names.")),
      End, "OK");
  if (!w) { pivot_strings_free(cf_names, cf_n); return; }
  for (i = 0; i < cf_n; i++) { to_latin1(cf_names[i], l, sizeof l); DoMethod(cf_fields, MUIM_List_InsertSingle, (ULONG)l, MUIV_List_Insert_Bottom); }
  DoMethod(lv, MUIM_Notify, MUIA_Listview_DoubleClick, TRUE, (ULONG)app, 2, MUIM_Application_ReturnID, ID_DLG1);
  DoMethod(ins, MUIM_Notify, MUIA_Pressed, FALSE, (ULONG)app, 2, MUIM_Application_ReturnID, ID_DLG1);
  DoMethod(del, MUIM_Notify, MUIA_Pressed, FALSE, (ULONG)app, 2, MUIM_Application_ReturnID, ID_DLG2);
  set(w, MUIA_Window_ActiveObject, (ULONG)cf_name);
  id = run_modal(w, ends, cf_each);
  if (id == ID_DLGOK || id == ID_DLG2) {
    char un[200], uf[1200];
    const char *why = "";
    to_utf8((const char *)xget(cf_name, MUIA_String_Contents), un, sizeof un);
    to_utf8((const char *)xget(cf_formula, MUIA_String_Contents), uf, sizeof uf);
    pivot_copy1(&np, p);
    if (id == ID_DLG2) {
      if (pivot_calc_remove(&np, un)) book_pivot_set(book, sheet, pi, &np);
    } else if (pivot_calc_set(book, &np, un, uf, &why)) {
      /* (put in Values, as Excel's Add does) */
      int has = 0;
      for (i = 0; i < np.nvals; i++) if (str_ieq(np.vals[i].name, un)) has = 1;
      if (!has) pivot_def_add(&np, PA_VALUES, un, PF_SUM);
      book_pivot_set(book, sheet, pi, &np);
    } else say(why);
    pivot_free1(&np);
    refresh_all();
  }
  MUI_DisposeObject(w);
  pivot_strings_free(cf_names, cf_n);
  cf_names = NULL; cf_n = 0;
}

/* ------------------------------------------------------------------ grouping */
static int field_at_cell(LPivot **pp, int *sheet, int *pi, int *area, int *fi)
{
  LPivot *p = pivot_here(sheet, pi);
  if (!p) { say("Select a cell in a PivotTable first."); return 0; }
  if (!pivot_field_here(area, fi) || (*area != PA_ROWS && *area != PA_COLS)) { say("Select an item of a Rows or Columns field first."); return 0; }
  *pp = p;
  return 1;
}
static int dbl(const char *s, double *v) { char *e; *v = strtod(s, &e); return e != s; }
static void group_field(void)
{
  static const ULONG ends[] = { ID_DLGOK, 0 };
  int sheet, pi, area, fi, type, changed = 0;
  LPivot *p, np;
  PivotField *f;
  double mn = 0, mx = 0;
  Object *w;
  char a[40], b2[40], c2[40];
  if (!field_at_cell(&p, &sheet, &pi, &area, &fi)) return;
  f = &(area == PA_ROWS ? p->rows : p->cols)[fi];
  type = pivot_field_type(book, p, f->name, &mn, &mx);
  pivot_copy1(&np, p);
  if (type == 2) {
    /* dates: by years, quarters, months, days */
    static const char *const lv_en[4] = { "Years", "Quarters", "Months", "Days" };
    const char *labs[5];
    UBYTE on[4];
    Object *tick;
    int k, any = 0;
    for (k = 0; k < 4; k++) { labs[k] = tr(lv_en[k]); on[k] = 0; }
    labs[4] = NULL;
    for (k = 0; k < (area == PA_ROWS ? p->nrows : p->ncols); k++) {
      PivotField *g = &(area == PA_ROWS ? p->rows : p->cols)[k];
      if (str_ieq(g->name, f->name) && g->group >= PG_YEARS && g->group <= PG_DAYS) { on[g->group - PG_YEARS] = 1; any = 1; }
    }
    if (!any) on[2] = 1;
    { int y, m, d; serial_ymd(mn, &y, &m, &d); sprintf(a, "%d/%d/%d", d, m, y); serial_ymd(mx, &y, &m, &d); sprintf(b2, "%d/%d/%d", d, m, y); }
    w = dialog_window("Grouping", VGroup,
          Child, ColGroup(2), Child, QLabel(tr("Starting at:"), MUIO_Label_SingleFrame), Child, TextObject, TextFrame, MUIA_Text_Contents, (ULONG)a, End,
            Child, QLabel(tr("Ending at:"), MUIO_Label_SingleFrame), Child, TextObject, TextFrame, MUIA_Text_Contents, (ULONG)b2, End, End,
          Child, LLabel1(tr("By:")),
          Child, tick = NewObject(ticklist_class()->mcc_Class, NULL, MUIA_Tick_Labels, (ULONG)labs, MUIA_Tick_States, (ULONG)on, MUIA_Tick_Count, 4, TAG_DONE),
        End, "OK");
    if (w && run_modal(w, ends, NULL) == ID_DLGOK) {
      unsigned lv = 0;
      for (k = 0; k < 4; k++) if (on[k]) lv |= 1u << (PG_YEARS + k);
      if (!lv) say("Tick at least one of Years, Quarters, Months and Days.");
      else changed = pivot_group_dates(&np, area, fi, lv);
    }
    if (w) MUI_DisposeObject(w);
  } else if (type == 1) {
    /* numbers: from, to, by */
    Object *s1, *s2, *s3;
    double st = f->group == PG_RANGE ? f->gstart : mn, en = f->group == PG_RANGE ? f->gend : mx, by = f->group == PG_RANGE ? f->gstep : 10;
    char g1[40], g2[40], g3[40];
    num_general(st, g1, 12); num_general(en, g2, 12); num_general(by, g3, 12);
    w = dialog_window("Grouping", VGroup,
          Child, ColGroup(2),
            Child, QLabel(tr("Starting at:"), MUIO_Label_SingleFrame), Child, s1 = string_obj(g1, 30),
            Child, QLabel(tr("Ending at:"), MUIO_Label_SingleFrame), Child, s2 = string_obj(g2, 30),
            Child, QLabel(tr("By:"), MUIO_Label_SingleFrame), Child, s3 = string_obj(g3, 30),
          End,
        End, "OK");
    if (w && run_modal(w, ends, NULL) == ID_DLGOK) {
      double x, y, z;
      if (!dbl((const char *)xget(s1, MUIA_String_Contents), &x) || !dbl((const char *)xget(s2, MUIA_String_Contents), &y) || !dbl((const char *)xget(s3, MUIA_String_Contents), &z) || z <= 0 || y < x)
        say("Starting at, Ending at and By must be numbers, By more than 0.");
      else changed = pivot_group_range(&np, area, fi, x, y, z);
    }
    if (w) MUI_DisposeObject(w);
  } else {
    /* items by hand: those ticked made a group */
    char **items, (*l1)[64], gname[64], gl[64];
    const char **labs;
    UBYTE *on;
    int n = pivot_field_items_of(book, p, f->name, f, &items), k, nn;
    Object *tick, *sc, *gs;
    nn = n > 300 ? 300 : n;
    l1 = calloc((size_t)nn + 1, sizeof *l1);
    labs = (const char **)calloc((size_t)nn + 1, sizeof *labs);
    on = (UBYTE *)calloc((size_t)nn + 1, 1);
    pivot_group_name(f, gname, sizeof gname);
    to_latin1(gname, gl, sizeof gl);
    (void)c2;
    if (l1 && labs && on) {
      for (k = 0; k < nn; k++) { to_latin1(items[k], l1[k], sizeof l1[k]); labs[k] = l1[k]; }
      sc = NewObject(flatscroll_class()->mcc_Class, NULL, TAG_DONE);
      w = dialog_window("Group Selection", VGroup,
            Child, LLabel1(tr("Tick the items to group:")),
            Child, HGroup, MUIA_Group_Spacing, 0,
              Child, tick = NewObject(ticklist_class()->mcc_Class, NULL, MUIA_Tick_Labels, (ULONG)labs, MUIA_Tick_States, (ULONG)on, MUIA_Tick_Count, nn, MUIA_Tick_Prop, (ULONG)sc, TAG_DONE),
              Child, sc,
            End,
            Child, HGroup, Child, QLabel(tr("Group name:"), MUIO_Label_SingleFrame), Child, gs = string_obj(gl, 60), End,
          End, "OK");
      if (w) {
        DoMethod(sc, MUIM_Notify, MUIA_Prop_First, MUIV_EveryTime, (ULONG)tick, 3, MUIM_Set, MUIA_Tick_First, MUIV_TriggerValue);
        if (run_modal(w, ends, NULL) == ID_DLGOK) {
          char **pick = (char **)calloc((size_t)nn + 1, sizeof(char *)), u[200];
          int np2 = 0;
          for (k = 0; pick && k < nn; k++) if (on[k]) pick[np2++] = items[k];
          to_utf8((const char *)xget(gs, MUIA_String_Contents), u, sizeof u);
          if (np2 < 1 || !u[0]) say("Tick at least one item and give the group a name.");
          else changed = pivot_group_items(&np, area, fi, u, pick, np2);
          free(pick);
        }
        MUI_DisposeObject(w);
      }
    }
    free(l1); free(labs); free(on);
    pivot_strings_free(items, n);
  }
  if (changed) { book_pivot_set(book, sheet, pi, &np); refresh_all(); }
  pivot_free1(&np);
}
static void ungroup_field(void)
{
  int sheet, pi, area, fi;
  LPivot *p, np;
  if (!field_at_cell(&p, &sheet, &pi, &area, &fi)) return;
  pivot_copy1(&np, p);
  if (pivot_ungroup(&np, area, fi)) { book_pivot_set(book, sheet, pi, &np); refresh_all(); }
  pivot_free1(&np);
}
static void collapse_field(int collapse)
{
  int sheet, pi, area, fi;
  LPivot *p, np;
  if (!field_at_cell(&p, &sheet, &pi, &area, &fi)) return;
  pivot_copy1(&np, p);
  if (pivot_collapse_field(book, &np, area, fi, collapse)) { book_pivot_set(book, sheet, pi, &np); refresh_all(); }
  else say("The innermost field has no detail to show or hide.");
  pivot_free1(&np);
}

void pivot2_command(unsigned long id)
{
  int obj;
  switch (id) {
  case ID_P2_RECPIVOT: recommended(); break;
  case ID_P2_PIVOTCHART: pivot_chart(); break;
  case ID_P2_SLICER: insert_slicer(); break;
  case ID_P2_SLICERSET: slicer_settings(); break;
  case ID_P2_SLICERCLEAR:
    if (selected_slicer(&obj)) { book_slicer_clear(book, cur_index(), obj); refresh_all(); }
    else say("Select a slicer first: click its frame.");
    break;
  case ID_P2_CALCFIELD: calc_field(); break;
  case ID_P2_GROUP: group_field(); break;
  case ID_P2_UNGROUP: ungroup_field(); break;
  case ID_P2_EXPAND: collapse_field(0); break;
  case ID_P2_COLLAPSE: collapse_field(1); break;
  }
}

/* ------------------------------------------------------------------ the grid's +/- buttons */
/* the button of a pivot item in visible cell i, j: a small box at the cell's right */
static int btn_box(const GridGeo *g, int i, int j, LONG *x0, LONG *y0, LONG *x1, LONG *y1)
{
  LONG h = g->vy[i + 1] - g->vy[i], bs = 9;
  if (h < bs + 2 || g->vx[j + 1] - g->vx[j] < bs + 6) return 0;
  *x1 = g->vx[j + 1] - 3; *x0 = *x1 - bs + 1;
  *y0 = g->vy[i] + (h - bs) / 2; *y1 = *y0 + bs - 1;
  return 1;
}
static int vis_index(const i32 *v, int n, i32 x) { int k; for (k = 0; k < n; k++) if (v[k] == x) return k; return -1; }
void pivot2_draw_buttons(struct RastPort *rp, const void *geo)
{
  const GridGeo *g = (const GridGeo *)geo;
  Sheet *s = g->book->sh[g->sheet];
  int p, k;
  for (p = 0; p < s->npivots; p++)
    for (k = 0; k < s->pivots[p].nbtns; k++) {
      PivotBtn *b = &s->pivots[p].btns[k];
      int i = vis_index(g->vr, g->nvr, b->r), j = vis_index(g->vc, g->nvc, b->c);
      LONG x0, y0, x1, y1, cx, cy;
      if (i < 0 || j < 0 || !btn_box(g, i, j, &x0, &y0, &x1, &y1) || x1 >= g->w || y1 >= g->h || x0 < g->hw || y0 < g->hh) continue;
      SetAPen(rp, g->pen_white); RectFill(rp, x0, y0, x1, y1);
      SetAPen(rp, g->pen_headline);
      Move(rp, x0, y0); Draw(rp, x1, y0); Draw(rp, x1, y1); Draw(rp, x0, y1); Draw(rp, x0, y0);
      cx = (x0 + x1) / 2; cy = (y0 + y1) / 2;
      SetAPen(rp, g->pen_text);
      Move(rp, x0 + 2, cy); Draw(rp, x1 - 2, cy);
      if (b->collapsed) { Move(rp, cx, y0 + 2); Draw(rp, cx, y1 - 2); }
    }
}
int pivot2_click(const void *geo, long x, long y)
{
  const GridGeo *g = (const GridGeo *)geo;
  Sheet *s = g->book->sh[g->sheet];
  int p, k;
  for (p = 0; p < s->npivots; p++)
    for (k = 0; k < s->pivots[p].nbtns; k++) {
      PivotBtn *b = &s->pivots[p].btns[k];
      int i = vis_index(g->vr, g->nvr, b->r), j = vis_index(g->vc, g->nvc, b->c);
      LONG x0, y0, x1, y1;
      if (i < 0 || j < 0 || !btn_box(g, i, j, &x0, &y0, &x1, &y1)) continue;
      if (x >= x0 - 1 && x <= x1 + 1 && y >= y0 - 1 && y <= y1 + 1) return book_pivot_toggle(g->book, g->sheet, b->r, b->c);
    }
  return 0;
}

/* ------------------------------------------------------------------ slicers on the grid */
void pivot2_draw_slicer(struct GridView *gv, int obj, long x0, long y0, long x1, long y1)
{
  DObj *o = drw_obj(gv->book->sh[gv->sheet], obj);
  PrimList pl;
  if (!o || !o->slicer || x1 - x0 < 20 || y1 - y0 < 16) return;
  prims_init(&pl);
  slicer_render(gv->book, o->slicer, (int)(x1 - x0), (int)(y1 - y0), (int)gv->zoom, ov_text_width, NULL, &pl);
  ov_paint(gv->rp, &pl, gv->ox + x0, gv->oy + y0, gv->pen, gv->g);
  prims_free(&pl);
}
int pivot2_slicer_click(struct GridView *gv, int obj, long x, long y, long w, long h, int shift)
{
  DObj *o = drw_obj(gv->book->sh[gv->sheet], obj);
  int k;
  if (!o || !o->slicer) return 0;
  k = slicer_hit(gv->book, o->slicer, (int)w, (int)h, (int)gv->zoom, x, y);
  if (k >= 0) book_slicer_click(gv->book, gv->sheet, obj, k, shift);
  else if (k == SH_CLEAR) book_slicer_clear(gv->book, gv->sheet, obj);
  else if (k == SH_MULTI) { Slicer *c = slicer_copy(o->slicer); if (c) { c->multi = !c->multi; drw_begin(gv->book, gv->sheet); slicer_free(o->slicer); o->slicer = c; drw_end(gv->book); } }
  else return 0;
  return 1;
}
