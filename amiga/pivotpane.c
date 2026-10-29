/*
 * pivotpane.c - pivot tables on the Amiga side (see tabletab.h): Insert PivotTable, the PivotTable
 * Fields pane (the source's fields, and the Filters, Columns, Rows and Values areas they are put
 * in), Value Field Settings, and a field's items to show. The pane works on the report as it goes:
 * each change is worked out and shown in the sheet behind it; Cancel puts the pivot table back as
 * it was when the pane opened.
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
#include "../core/pivot.h"

static ULONG xget(Object *o, ULONG attr) { ULONG v = 0; get(o, attr, &v); return v; }
static void to_utf8(const char *s, char *u, size_t cap)
{
  size_t o = 0;
  for (; *s && o + 4 < cap; s++) o += (size_t)utf8_put(u + o, (u8)*s);
  u[o] = 0;
}
static void to_latin1(const char *u, char *l, size_t cap) { utf8_to_latin1(u, (u32)strlen(u), l, cap); }
static Object *list_obj(int rows)
{
  return ListviewObject, MUIA_Listview_List, ListObject, InputListFrame, MUIA_List_ConstructHook, MUIV_List_ConstructHook_String,
    MUIA_List_DestructHook, MUIV_List_DestructHook_String, MUIA_List_MinLineHeight, 0, End,
    MUIA_Listview_DoubleClick, TRUE, MUIA_CycleChain, 1, MUIA_FixHeightTxt, (ULONG)(rows > 4 ? "\n\n\n\n\n\n" : "\n\n\n"), End;
}
static Object *list_of(Object *lv) { return (Object *)xget(lv, MUIA_Listview_List); }

/* ------------------------------------------------------------------ the pivot table and field at the active cell */
LPivot *pivot_here(int *sheet, int *pi)
{
  Sheet *s = cur_sheet();
  LPivot *p = pivot_at(book, cur_index(), s->cur_r, s->cur_c);
  if (sheet) *sheet = cur_index();
  if (pi) *pi = p ? pivot_index(book, cur_index(), p) : -1;
  return p;
}
int pivot_field_here(int *area, int *fi)
{
  Sheet *s = cur_sheet();
  LPivot *p = pivot_here(NULL, NULL);
  i32 r = s->cur_r, c = s->cur_c;
  int kind, lv;
  if (!p || p->out.r1 < 0 || !p->kind || r < p->out.r1 || r > p->out.r2 || r - p->out.r1 >= p->nkind) return 0;
  kind = p->kind[r - p->out.r1];
  lv = p->level[r - p->out.r1];
  if (kind == PK_FILTER) { *area = PA_FILTERS; *fi = (int)(r - p->out.r1); return *fi < p->nfilters; }
  if (c >= p->datacol && p->nvals) {
    /* a value column: value fields repeat across the columns */
    if (r >= p->datarow || r == p->datarow - 1 || !p->ncols) { *area = PA_VALUES; *fi = (int)((c - p->datacol) % p->nvals); return 1; }
    if (r > p->bodyrow && r <= p->bodyrow + p->ncols) { *area = PA_COLS; *fi = (int)(r - p->bodyrow - 1); return 1; }
    return 0;
  }
  if (c < p->datacol && p->nrows) {
    if (p->layout != PL_COMPACT) { *area = PA_ROWS; *fi = (int)(c - p->c); return *fi < p->nrows; }
    if (kind == PK_DATA || kind == PK_SUB || kind == PK_ITEM || kind == PK_COLL) { *area = PA_ROWS; *fi = lv > 0 ? lv - 1 : 0; return *fi < p->nrows; }
    if (kind == PK_HEAD && r == p->datarow - 1) { *area = PA_ROWS; *fi = 0; return 1; }
  }
  return 0;
}

/* ------------------------------------------------------------------ Insert PivotTable */
void pivot_dialog(const char *source)
{
  static const ULONG ends[] = { ID_DLGOK, 0 };
  static const char *where_en[] = { "New Worksheet", "Existing Worksheet", NULL };
  const char *where[3];
  char src[256], loc[64];
  Object *w, *str, *cyc, *locs;
  int k;
  to_latin1(source, src, sizeof src);
  ref_text(cur_sheet()->cur_r, cur_sheet()->cur_c, 1, 1, loc);
  for (k = 0; k < 2; k++) where[k] = tr(where_en[k]);
  where[2] = NULL;
  w = dialog_window("Create PivotTable", VGroup,
        Child, LLabel1(tr("Choose the data that you want to analyze:")),
        Child, HGroup, Child, QLabel(tr("Table/Range:"), MUIO_Label_SingleFrame),
          Child, str = StringObject, StringFrame, MUIA_String_Contents, (ULONG)src, MUIA_String_MaxLen, 250, MUIA_CycleChain, 1, End, End,
        Child, LLabel1(tr("Choose where to put the PivotTable:")),
        Child, cyc = CycleObject, MUIA_Cycle_Entries, (ULONG)where, MUIA_CycleChain, 1, End,
        Child, HGroup, Child, QLabel(tr("Location:"), MUIO_Label_SingleFrame),
          Child, locs = StringObject, StringFrame, MUIA_String_Contents, (ULONG)loc, MUIA_String_MaxLen, 60, MUIA_CycleChain, 1, End, End,
      End, "OK");
  if (!w) return;
  set(w, MUIA_Window_ActiveObject, (ULONG)str);
  if (run_modal(w, ends, NULL) == ID_DLGOK) {
    char u[256];
    int sh, pi, newsheet = xget(cyc, MUIA_Cycle_Active) == 0;
    i32 r = 2, c = 0;
    to_utf8((const char *)xget(str, MUIA_String_Contents), u, sizeof u);
    if (newsheet) {
      int at = book_add_sheet(book, NULL, cur_index());
      if (at < 0) { MUI_DisposeObject(w); return; }
      book->active = at;
      sh = at;
    } else {
      const char *t = (const char *)xget(locs, MUIA_String_Contents);
      u16 s16; Rect rr; u8 fl;
      int n = t ? parse_ref(book, t, &s16, &rr, &fl) : 0;
      if (n <= 0 || t[n]) { ask("OK", "%s", tr("The location isn't a cell.")); MUI_DisposeObject(w); return; }
      sh = s16 == SHEET_SELF ? cur_index() : s16;
      r = rr.r1; c = rr.c1;
    }
    pi = book_pivot_add(book, sh, u[0] == '=' ? u + 1 : u, r, c);
    if (pi < 0) {
      ask("OK", "%s", tr("The data isn't a range with headings or a table."));
      if (newsheet) { book_delete_sheet(book, sh); }
    } else {
      book->active = sh;
      set(grid, MUIA_Grid_Book, (ULONG)book);
      refresh_all();
      DoMethod(grid, MUIM_Grid_Select, r, c, r, c, r, c);
      MUI_DisposeObject(w);
      w = NULL;
      pivot_fields_pane();
    }
    refresh_all();
  }
  if (w) MUI_DisposeObject(w);
}

/* ------------------------------------------------------------------ the Fields pane */
static Object *pv_flist, *pv_area[4];
static int pv_sheet, pv_pi, pv_cur = -1;      /* the pivot table; the area last picked in */
static char **pv_fields; static int pv_nf;
static LPivot *pv_p(void) { return pv_pi >= 0 && pv_sheet < book->nsh && pv_pi < book->sh[pv_sheet]->npivots ? &book->sh[pv_sheet]->pivots[pv_pi] : NULL; }
static int area_count(const LPivot *p, int a) { return a == 0 ? p->nfilters : a == 1 ? p->ncols : a == 2 ? p->nrows : p->nvals; }
static int area_pa(int a) { return a == 0 ? PA_FILTERS : a == 1 ? PA_COLS : a == 2 ? PA_ROWS : PA_VALUES; }
static void pv_fill(void)
{
  LPivot *p = pv_p();
  int i, a;
  char t[300], l[300];
  if (!p) return;
  {
    LONG act = (LONG)xget(list_of(pv_flist), MUIA_List_Active);
    set(list_of(pv_flist), MUIA_List_Quiet, TRUE);
    DoMethod(list_of(pv_flist), MUIM_List_Clear);
    for (i = 0; i < pv_nf; i++) {
      to_latin1(pv_fields[i], l, sizeof l);
      snprintf(t, sizeof t, "%s %s", pivot_def_has(p, pv_fields[i]) ? "[x]" : "[  ]", l);
      DoMethod(list_of(pv_flist), MUIM_List_InsertSingle, (ULONG)t, MUIV_List_Insert_Bottom);
    }
    set(list_of(pv_flist), MUIA_List_Quiet, FALSE);
    if (act >= 0 && act < pv_nf) nnset(list_of(pv_flist), MUIA_List_Active, act);
  }
  for (a = 0; a < 4; a++) {
    Object *l2 = list_of(pv_area[a]);
    LONG act = (LONG)xget(l2, MUIA_List_Active);
    set(l2, MUIA_List_Quiet, TRUE);
    DoMethod(l2, MUIM_List_Clear);
    for (i = 0; i < area_count(p, a); i++) {
      if (a == 3) pivot_caption(&p->vals[i], t, sizeof t);
      else snprintf(t, sizeof t, "%s", (a == 0 ? p->filters : a == 1 ? p->cols : p->rows)[i].name);
      to_latin1(t, l, sizeof l);
      DoMethod(l2, MUIM_List_InsertSingle, (ULONG)l, MUIV_List_Insert_Bottom);
    }
    set(l2, MUIA_List_Quiet, FALSE);
    if (a == pv_cur && act >= 0) nnset(l2, MUIA_List_Active, act < area_count(p, a) ? act : area_count(p, a) - 1);
  }
}
/* a numeric field (its first value under the heading is a number) goes to Values, others to Rows */
static int numeric_field(const char *name)
{
  LPivot *p = pv_p();
  int ss, k;
  Rect src;
  i32 r;
  if (!p || !pivot_source(book, p, &ss, &src)) return 0;
  for (k = 0; k < pv_nf && !str_ieq(pv_fields[k], name); k++) ;
  if (k == pv_nf) return 0;
  for (r = src.r1 + 1; r <= src.r2 && r <= src.r1 + 50; r++) {
    const Value *v = cell_value(book, ss, r, src.c1 + k);
    if (v->t == V_NUM) return 1;
    if (v->t != V_EMPTY) return 0;
  }
  return 0;
}
/* the definition changed by fn and the report worked out again */
static void pv_apply(LPivot *np)
{
  book_pivot_set(book, pv_sheet, pv_pi, np);
  refresh_all();
  pv_fill();
}
static void pv_each(ULONG id)
{
  LPivot *p = pv_p(), np;
  LONG f = (LONG)xget(list_of(pv_flist), MUIA_List_Active);
  int a, i;
  if (!p) return;
  if (id >= ID_PV_AREA0 && id <= ID_PV_AREA3) {
    a = (int)(id - ID_PV_AREA0);
    if ((LONG)xget(list_of(pv_area[a]), MUIA_List_Active) < 0) return;
    pv_cur = a;
    for (i = 0; i < 4; i++) if (i != a) nnset(list_of(pv_area[i]), MUIA_List_Active, MUIV_List_Active_Off);
    return;
  }
  pivot_copy1(&np, p);
  switch (id) {
  case ID_PV_FLIST:                              /* a field double-clicked: in or out */
    if (f < 0 || f >= pv_nf) break;
    if (pivot_def_has(&np, pv_fields[f])) {
      for (a = 0; a < 4; a++)
        for (i = area_count(&np, a) - 1; i >= 0; i--) {
          const char *nm = a == 3 ? np.vals[i].name : (a == 0 ? np.filters : a == 1 ? np.cols : np.rows)[i].name;
          if (str_ieq(nm, pv_fields[f])) pivot_def_remove(&np, area_pa(a), i);
        }
    } else pivot_def_add(&np, numeric_field(pv_fields[f]) ? PA_VALUES : PA_ROWS, pv_fields[f], PF_SUM);
    pv_apply(&np);
    break;
  case ID_PV_TOFILTERS: case ID_PV_TOCOLS: case ID_PV_TOROWS: case ID_PV_TOVALUES: {
    int to = id == ID_PV_TOFILTERS ? PA_FILTERS : id == ID_PV_TOCOLS ? PA_COLS : id == ID_PV_TOROWS ? PA_ROWS : PA_VALUES;
    if (f < 0 || f >= pv_nf) break;
    if (to != PA_VALUES) {
      /* out of the other axis areas first (a field is in one of them at a time) */
      for (a = 0; a < 3; a++)
        for (i = area_count(&np, a) - 1; i >= 0; i--)
          if (area_pa(a) != to && str_ieq((a == 0 ? np.filters : a == 1 ? np.cols : np.rows)[i].name, pv_fields[f])) pivot_def_remove(&np, area_pa(a), i);
    }
    pivot_def_add(&np, to, pv_fields[f], numeric_field(pv_fields[f]) ? PF_SUM : PF_COUNT);
    pv_apply(&np);
    break; }
  case ID_PV_UP: case ID_PV_DOWN: case ID_PV_REMOVE: {
    LONG k;
    if (pv_cur < 0) break;
    k = (LONG)xget(list_of(pv_area[pv_cur]), MUIA_List_Active);
    if (k < 0 || k >= area_count(&np, pv_cur)) break;
    if (id == ID_PV_REMOVE) pivot_def_remove(&np, area_pa(pv_cur), (int)k);
    else if (id == ID_PV_UP && k > 0) { pivot_def_move(&np, area_pa(pv_cur), (int)k, area_pa(pv_cur), (int)k - 1); nnset(list_of(pv_area[pv_cur]), MUIA_List_Active, k - 1); }
    else if (id == ID_PV_DOWN && k + 1 < area_count(&np, pv_cur)) { pivot_def_move(&np, area_pa(pv_cur), (int)k, area_pa(pv_cur), (int)k + 1); nnset(list_of(pv_area[pv_cur]), MUIA_List_Active, k + 1); }
    pv_apply(&np);
    break; }
  case ID_PV_SETTINGS: case ID_PV_ITEMS: {
    LONG k;
    if (pv_cur < 0) break;
    k = (LONG)xget(list_of(pv_area[pv_cur]), MUIA_List_Active);
    if (k < 0) break;
    if (pv_cur == 3) { if (pivot_value_settings(pv_sheet, pv_pi, (int)k)) { refresh_all(); pv_fill(); } }
    else if (pivot_field_items(pv_sheet, pv_pi, area_pa(pv_cur), (int)k)) { refresh_all(); pv_fill(); }
    break; }
  }
  pivot_free1(&np);
}
void pivot_fields_pane(void)
{
  static const ULONG ends[] = { ID_DLGOK, 0 };
  static const char *const titles[4] = { "Filters", "Columns", "Rows", "Values" };
  Object *w, *b[9], *closeb;
  LPivot *p = pivot_here(&pv_sheet, &pv_pi), keep;
  struct Screen *scr;
  LONG left;
  int i;
  ULONG id;
  if (!p) return;
  pv_nf = pivot_fields(book, p, &pv_fields);
  pv_cur = -1;
  pivot_copy1(&keep, p);
  for (i = 0; i < 4; i++) pv_area[i] = list_obj(3);
  pv_flist = list_obj(6);
  scr = (struct Screen *)xget(win, MUIA_Window_Screen);
  left = scr && scr->Width > 330 ? scr->Width - 300 : 0;
  w = QWindowObject, MUIA_Window_Title, (ULONG)tr("PivotTable Fields"), MUIA_Window_RefWindow, (ULONG)win,
    MUIA_Window_LeftEdge, left, MUIA_Window_TopEdge, MUIV_Window_TopEdge_Centered,
    WindowContents, VGroup, QDLG,
      Child, QTitleObject(tr("Choose fields to add to the report:")),
      Child, pv_flist,
      Child, HGroup, MUIA_Group_Spacing, 2,
        Child, b[0] = QButton(tr("Filters")), Child, b[1] = QButton(tr("Columns")), Child, b[2] = QButton(tr("Rows")), Child, b[3] = QButton(tr("Values")),
      End,
      Child, LLabel1(tr("Drag fields between areas: pick one, then move it.")),
      Child, ColGroup(2), MUIA_Group_Spacing, 2,
        Child, QTitleObject(tr(titles[0])), Child, QTitleObject(tr(titles[1])),
        Child, pv_area[0], Child, pv_area[1],
        Child, QTitleObject(tr(titles[2])), Child, QTitleObject(tr(titles[3])),
        Child, pv_area[2], Child, pv_area[3],
      End,
      Child, HGroup, MUIA_Group_Spacing, 2,
        Child, b[4] = QButton(tr("Up")), Child, b[5] = QButton(tr("Down")), Child, b[6] = QButton(tr("Remove")),
        Child, b[7] = QButton(tr("Settings...")), Child, b[8] = QButton(tr("Items...")),
      End,
      Child, HGroup, Child, HSpace(0), Child, closeb = QButton(tr("Close")), End,
    End,
  End;
  if (!w) { pivot_strings_free(pv_fields, pv_nf); pivot_free1(&keep); return; }
  {
    static const ULONG ids[9] = { ID_PV_TOFILTERS, ID_PV_TOCOLS, ID_PV_TOROWS, ID_PV_TOVALUES, ID_PV_UP, ID_PV_DOWN, ID_PV_REMOVE, ID_PV_SETTINGS, ID_PV_ITEMS };
    for (i = 0; i < 9; i++) DoMethod(b[i], MUIM_Notify, MUIA_Pressed, FALSE, (ULONG)app, 2, MUIM_Application_ReturnID, ids[i]);
  }
  DoMethod(closeb, MUIM_Notify, MUIA_Pressed, FALSE, (ULONG)app, 2, MUIM_Application_ReturnID, ID_DLGOK);
  DoMethod(pv_flist, MUIM_Notify, MUIA_Listview_DoubleClick, TRUE, (ULONG)app, 2, MUIM_Application_ReturnID, ID_PV_FLIST);
  for (i = 0; i < 4; i++) {
    DoMethod(list_of(pv_area[i]), MUIM_Notify, MUIA_List_Active, MUIV_EveryTime, (ULONG)app, 2, MUIM_Application_ReturnID, ID_PV_AREA0 + i);
    DoMethod(pv_area[i], MUIM_Notify, MUIA_Listview_DoubleClick, TRUE, (ULONG)app, 2, MUIM_Application_ReturnID, ID_PV_SETTINGS + (i == 3 ? 0 : 1));
  }
  pv_fill();
  id = run_modal(w, ends, pv_each);
  if (id == ID_DLGCANCEL && pv_p()) { book_pivot_set(book, pv_sheet, pv_pi, &keep); refresh_all(); }
  MUI_DisposeObject(w);
  pivot_free1(&keep);
  pivot_strings_free(pv_fields, pv_nf);
  pv_fields = NULL; pv_nf = 0; pv_flist = NULL;
  for (i = 0; i < 4; i++) pv_area[i] = NULL;
}

/* ------------------------------------------------------------------ Value Field Settings */
int pivot_value_settings(int sheet, int pi, int vi)
{
  static const ULONG ends[] = { ID_DLGOK, 0 };
  static const char *const funcs_en[PF_N + 1] = { "Sum", "Count", "Average", "Max", "Min", "Product", "Count Numbers", "StdDev", "StdDevp", "Var", "Varp", NULL };
  static const char *const shows_en[PS_N + 1] = { "No Calculation", "% of Grand Total", "% of Column Total", "% of Row Total", "Running Total In", "Rank Largest to Smallest", NULL };
  const char *funcs[PF_N + 1], *shows[PS_N + 1];
  LPivot *p, np;
  PivotValue *v;
  char cap[300], l[300], src[300], title[340];
  Object *w, *name, *fc, *sc;
  int i, changed = 0;
  if (sheet < 0 || sheet >= book->nsh || pi < 0 || pi >= book->sh[sheet]->npivots) return 0;
  p = &book->sh[sheet]->pivots[pi];
  if (vi < 0 || vi >= p->nvals) return 0;
  v = &p->vals[vi];
  for (i = 0; i < PF_N; i++) funcs[i] = tr(funcs_en[i]);
  funcs[PF_N] = NULL;
  for (i = 0; i < PS_N; i++) shows[i] = tr(shows_en[i]);
  shows[PS_N] = NULL;
  pivot_caption(v, cap, sizeof cap);
  to_latin1(cap, l, sizeof l);
  to_latin1(v->name, src, sizeof src);
  snprintf(title, sizeof title, "%s %s", tr("Source Name:"), src);
  w = dialog_window("Value Field Settings", VGroup,
        Child, LLabel1(title),
        Child, HGroup, Child, QLabel(tr("Custom Name:"), MUIO_Label_SingleFrame),
          Child, name = StringObject, StringFrame, MUIA_String_Contents, (ULONG)l, MUIA_String_MaxLen, 250, MUIA_CycleChain, 1, End, End,
        Child, LLabel1(tr("Summarize value field by:")),
        Child, fc = CycleObject, MUIA_Cycle_Entries, (ULONG)funcs, MUIA_Cycle_Active, v->func, MUIA_CycleChain, 1, End,
        Child, LLabel1(tr("Show values as:")),
        Child, sc = CycleObject, MUIA_Cycle_Entries, (ULONG)shows, MUIA_Cycle_Active, v->showas, MUIA_CycleChain, 1, End,
      End, "OK");
  if (!w) return 0;
  if (run_modal(w, ends, NULL) == ID_DLGOK) {
    char u[300], def[300];
    PivotValue *nv;
    pivot_copy1(&np, p);
    nv = &np.vals[vi];
    nv->func = (u8)xget(fc, MUIA_Cycle_Active);
    nv->showas = (u8)xget(sc, MUIA_Cycle_Active);
    to_utf8((const char *)xget(name, MUIA_String_Contents), u, sizeof u);
    /* a caption left as it was made (or emptied) follows the function chosen */
    free(nv->caption); nv->caption = NULL;
    pivot_caption(nv, def, sizeof def);
    if (u[0] && strcmp(u, cap) && strcmp(u, def)) nv->caption = str_dup(u);
    book_pivot_set(book, sheet, pi, &np);
    pivot_free1(&np);
    changed = 1;
  }
  MUI_DisposeObject(w);
  return changed;
}

/* ------------------------------------------------------------------ a field's items */
int pivot_field_items(int sheet, int pi, int area, int fi)
{
  static const ULONG ends[] = { ID_DLGOK, 0 };
  LPivot *p, np;
  PivotField *f;
  char **items, (*l1)[64], title[100];
  const char **lab;
  UBYTE *on;
  int n, nall, i, k, changed = 0;
  Object *w, *tick, *sc, *all, *none;
  ULONG id;
  if (sheet < 0 || sheet >= book->nsh || pi < 0 || pi >= book->sh[sheet]->npivots) return 0;
  p = &book->sh[sheet]->pivots[pi];
  f = area == PA_ROWS ? p->rows : area == PA_COLS ? p->cols : area == PA_FILTERS ? p->filters : NULL;
  if (!f || fi < 0 || fi >= (area == PA_ROWS ? p->nrows : area == PA_COLS ? p->ncols : p->nfilters)) return 0;
  f = &f[fi];
  nall = n = pivot_items(book, p, f->name, &items);
  if (n > 500) n = 500;
  l1 = calloc((size_t)n + 1, sizeof *l1);
  lab = (const char **)calloc((size_t)n + 1, sizeof *lab);
  on = (UBYTE *)calloc((size_t)n + 1, 1);
  if (!l1 || !lab || !on) goto out;
  for (i = 0; i < n; i++) {
    to_latin1(items[i], l1[i], sizeof l1[i]);
    lab[i] = l1[i];
    on[i] = 1;
    for (k = 0; k < f->nhidden; k++) if (str_ieq(f->hidden[k], items[i])) on[i] = 0;
  }
  to_latin1(f->name, title, sizeof title);
  sc = NewObject(flatscroll_class()->mcc_Class, NULL, TAG_DONE);
  w = dialog_window("Filter", VGroup,
        Child, TextObject, MUIA_Text_Contents, (ULONG)title, MUIA_Text_PreParse, (ULONG)"\33b", End,
        Child, LLabel1(tr("Show the items ticked:")),
        Child, HGroup, MUIA_Group_Spacing, 0,
          Child, tick = NewObject(ticklist_class()->mcc_Class, NULL, MUIA_Tick_Labels, (ULONG)lab, MUIA_Tick_States, (ULONG)on,
                                  MUIA_Tick_Count, n, MUIA_Tick_Prop, (ULONG)sc, TAG_DONE),
          Child, sc,
        End,
        Child, HGroup, Child, all = QButton(tr("Select All")), Child, none = QButton(tr("Clear")), Child, HSpace(0), End,
      End, "OK");
  if (!w) goto out;
  DoMethod(sc, MUIM_Notify, MUIA_Prop_First, MUIV_EveryTime, (ULONG)tick, 3, MUIM_Set, MUIA_Tick_First, MUIV_TriggerValue);
  DoMethod(all, MUIM_Notify, MUIA_Pressed, FALSE, (ULONG)tick, 3, MUIM_Set, MUIA_Tick_All, TRUE);
  DoMethod(none, MUIM_Notify, MUIA_Pressed, FALSE, (ULONG)tick, 3, MUIM_Set, MUIA_Tick_All, FALSE);
  id = run_modal(w, ends, NULL);
  if (id == ID_DLGOK) {
    PivotField *nf;
    int shown = 0;
    for (i = 0; i < n; i++) shown += on[i] != 0;
    if (!shown) ask("OK", "%s", tr("At least one item must be shown."));
    else {
      pivot_copy1(&np, p);
      nf = &(area == PA_ROWS ? np.rows : area == PA_COLS ? np.cols : np.filters)[fi];
      for (k = 0; k < nf->nhidden; k++) free(nf->hidden[k]);
      free(nf->hidden); nf->hidden = NULL; nf->nhidden = 0;
      if (shown < n && (nf->hidden = (char **)calloc((size_t)n, sizeof(char *))))
        for (i = 0; i < n; i++) if (!on[i]) nf->hidden[nf->nhidden++] = str_dup(items[i]);
      book_pivot_set(book, sheet, pi, &np);
      pivot_free1(&np);
      changed = 1;
    }
  }
  MUI_DisposeObject(w);
out:
  free(l1); free(lab); free(on);
  pivot_strings_free(items, nall);
  return changed;
}
