/*
 * anatab.c - the Analysis commands and dialogs (see anatab.h) over the core's analysis.h:
 * Forecast Sheet, Solver Parameters and Solver Results, Data Analysis (the tool list, then the
 * chosen tool's form), Allow Edit Ranges, Lock Cell, Insert Checkbox and the checkbox cells
 * themselves. The dialogs follow GridCraft's (crates/ui-egui/src/dialogs/analysis.rs) and Excel's,
 * laid out for a 640 x 480 screen; like datatab.c's they are modal, made when asked for and
 * disposed of after (a dialog with a list is made again when the list changes).
 */
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <proto/exec.h>
#include <proto/intuition.h>
#include <proto/graphics.h>
#include <proto/muimaster.h>
#include <proto/utility.h>
#include <clib/alib_protos.h>
#include "app.h"
#include "theme.h"
#include "lang.h"
#include "ids.h"
#include "anatab.h"
#include "datatab.h"
#include "../core/calc.h"
#include "../core/data.h"
#include "../core/analysis.h"

#undef Label1
#undef LLabel1
#define Label1(l)  QLabel(l, MUIO_Label_SingleFrame)
#define LLabel1(l) QLabel(l, MUIO_Label_LeftAligned | MUIO_Label_SingleFrame)

static ULONG xget(Object *o, ULONG attr) { ULONG v = 0; get(o, attr, &v); return v; }
static const char *sget(Object *o) { const char *s = (const char *)xget(o, MUIA_String_Contents); return s ? s : ""; }
static void l1_utf8(const char *s, char *u, size_t cap)
{
  size_t o = 0;
  for (; *s && o + 4 < cap; s++) o += (size_t)utf8_put(u + o, (u8)*s);
  u[o] = 0;
}
static void utf8_l1(const char *u, char *out, size_t cap) { utf8_to_latin1(u ? u : "", u ? (u32)strlen(u) : 0, out, cap); }
static Object *str_obj(const char *init, int max) { return StringObject, StringFrame, MUIA_String_Contents, (ULONG)init, MUIA_String_MaxLen, max, MUIA_CycleChain, 1, End; }
static Object *secret_obj(void) { return StringObject, StringFrame, MUIA_String_Secret, TRUE, MUIA_String_MaxLen, 64, MUIA_CycleChain, 1, End; }
static Object *check_obj(int on)
{
  return ImageObject, ImageButtonFrame, MUIA_InputMode, MUIV_InputMode_Toggle, MUIA_Image_Spec, MUII_CheckMark,
    MUIA_Image_FreeVert, TRUE, MUIA_Selected, on, MUIA_Background, MUII_ButtonBack, MUIA_ShowSelState, FALSE, MUIA_CycleChain, 1, End;
}
static Object *checkrow(Object **chk, int on, const char *label) { return HGroup, Child, *chk = check_obj(on), Child, LLabel1(tr(label)), Child, HSpace(0), End; }
static const char **trl(const char *const *en, const char **out) { int i; for (i = 0; en[i]; i++) out[i] = tr(en[i]); out[i] = NULL; return out; }
static Object *cycle_obj(const char **labels, int active) { return CycleObject, MUIA_Cycle_Entries, (ULONG)labels, MUIA_Cycle_Active, active, MUIA_CycleChain, 1, End; }
static void notify_id(Object *o, ULONG attr, ULONG id) { DoMethod(o, MUIM_Notify, attr, MUIV_EveryTime, (ULONG)app, 2, MUIM_Application_ReturnID, id); }
static void press_id(Object *o, ULONG id) { DoMethod(o, MUIM_Notify, MUIA_Pressed, FALSE, (ULONG)app, 2, MUIM_Application_ReturnID, id); }
static int checked(Object *o) { return xget(o, MUIA_Selected) != 0; }
static void say(const char *msg) { ask("OK", "%s", tr(msg)); }
static void sayu(const char *u) { char l[400]; utf8_l1(u, l, sizeof l); ask("OK", "%s", l); }
static void rect_l1(Rect a, char *out) { rect_text(a, out); }
static void sel_text(char *out) { rect_l1(cur_sel(), out); }
static double numof(Object *o, double dflt) { const char *s = sget(o); char *e; double v; while (*s == ' ') s++; if (!*s) return dflt; v = strtod(s, &e); return e == s ? dflt : v; }
static void show_sheet(int at) { if (at >= 0) { book->active = at; set(grid, MUIA_Grid_Book, (ULONG)book); } refresh_all(); }
/* the block of filled cells round the active cell, or the selection */
static Rect region(void)
{
  Sheet *s = cur_sheet();
  Rect a = cur_sel();
  int grew = 1;
  if (a.r1 != a.r2 || a.c1 != a.c2) return a;
  while (grew) {
    i32 r, c;
    grew = 0;
    if (a.r1 > 0) for (c = a.c1 > 0 ? a.c1 - 1 : 0; c <= a.c2 + 1 && !grew; c++) if (sheet_cell(s, a.r1 - 1, c)) { a.r1--; grew = 1; }
    if (!grew) for (c = a.c1 > 0 ? a.c1 - 1 : 0; c <= a.c2 + 1 && !grew; c++) if (sheet_cell(s, a.r2 + 1, c)) { a.r2++; grew = 1; }
    if (!grew && a.c1 > 0) for (r = a.r1 > 0 ? a.r1 - 1 : 0; r <= a.r2 + 1 && !grew; r++) if (sheet_cell(s, r, a.c1 - 1)) { a.c1--; grew = 1; }
    if (!grew) for (r = a.r1 > 0 ? a.r1 - 1 : 0; r <= a.r2 + 1 && !grew; r++) if (sheet_cell(s, r, a.c2 + 1)) { a.c2++; grew = 1; }
  }
  return a;
}

/* ------------------------------------------------------------------ checkboxes */
int anatab_draw_check(struct RastPort *rp, Book *b, int sheet, i32 r, i32 c, LONG x0, LONG y0, LONG x1, LONG y1, LONG penbox, LONG pentick, LONG penback)
{
  const Value *v;
  LONG s, bx, by;
  if (!cell_is_checkbox(b, sheet, r, c)) return 0;
  v = cell_value(b, sheet, r, c);
  if (v->t != V_BOOL && v->t != V_EMPTY) return 0;
  s = y1 - y0 - 5;
  if (s > 12) s = 12;
  if (s > x1 - x0 - 4) s = x1 - x0 - 4;
  if (s < 5) return 1;
  bx = x0 + (x1 - x0 - s) / 2; by = y0 + (y1 - y0 - s) / 2;
  if (v->t == V_BOOL && v->e) {
    /* ticked: a filled box with a white tick, as Excel draws it */
    SetAPen(rp, pentick); RectFill(rp, bx, by, bx + s, by + s);
    SetAPen(rp, penback);
    Move(rp, bx + 2, by + s / 2); Draw(rp, bx + s * 2 / 5, by + s - 3); Draw(rp, bx + s - 2, by + 2);
    Move(rp, bx + 2, by + s / 2 + 1); Draw(rp, bx + s * 2 / 5, by + s - 2); Draw(rp, bx + s - 2, by + 3);
  } else {
    SetAPen(rp, penback); RectFill(rp, bx + 1, by + 1, bx + s - 1, by + s - 1);
    SetAPen(rp, penbox);
    Move(rp, bx, by); Draw(rp, bx + s, by); Draw(rp, bx + s, by + s); Draw(rp, bx, by + s); Draw(rp, bx, by);
  }
  return 1;
}
static void checkbox_toggle_sel(void)
{
  Rect a = cur_sel();
  i32 r, c;
  int any = 0;
  if ((long)(a.r2 - a.r1 + 1) * (a.c2 - a.c1 + 1) > 10000L) { a.r2 = a.r1; a.c2 = a.c1; }
  for (r = a.r1; r <= a.r2; r++) for (c = a.c1; c <= a.c2; c++) if (cell_is_checkbox(book, cur_index(), r, c)) any = 1;
  if (!any) return;
  if (!datatab_may(book, cur_index(), DT_CELLS, a)) return;
  book_begin(book);
  for (r = a.r1; r <= a.r2; r++) for (c = a.c1; c <= a.c2; c++) book_checkbox_toggle(book, cur_index(), r, c);
  book_end(book);
  refresh_all();
}

/* ------------------------------------------------------------------ Allow Edit Ranges */
static int parse_areas(const char *l1, Rect *out, int max)
{
  char buf[300], *tok, *p;
  int n = 0, sh;
  strncpy(buf, l1, sizeof buf - 1); buf[sizeof buf - 1] = 0;
  for (p = buf; *p; p++) if (*p == ',' || *p == ';') *p = ' ';
  for (tok = strtok(buf, " "); tok && n < max; tok = strtok(NULL, " ")) {
    char u[200];
    l1_utf8(tok, u, sizeof u);
    if (!an_range(book, cur_index(), u, &sh, &out[n]) || sh != cur_index()) return 0;
    n++;
  }
  return n;
}
static int edit_range_dialog(int i)
{
  static const ULONG ends[] = { ID_DLGOK, 0 };
  Sheet *s = cur_sheet();
  Object *w, *title, *refs, *pw;
  char t[80], r[300], pwu[200];
  int ok = 0, k;
  if (i >= 0) {
    EditRange *e = &s->eranges[i];
    utf8_l1(e->title, t, sizeof t);
    r[0] = 0;
    for (k = 0; k < e->nareas && strlen(r) < 250; k++) { char a[40]; rect_l1(e->areas[k], a); if (k) strcat(r, ","); strcat(r, a); }
  } else { sprintf(t, "Range%d", s->neranges + 1); sel_text(r); }
  w = dialog_window(i >= 0 ? "Modify Range" : "New Range", ColGroup(2),
        Child, Label1(tr("Title:")), Child, title = str_obj(t, 60),
        Child, Label1(tr("Refers to cells:")), Child, refs = str_obj(r, 290),
        Child, Label1(tr("Range password:")), Child, pw = secret_obj(),
      End, NULL);
  if (!w) return 0;
  set(w, MUIA_Window_ActiveObject, (ULONG)title);
  for (;;) {
    Rect areas[64];
    int n;
    char tu[200];
    if (run_modal(w, ends, NULL) != ID_DLGOK) break;
    if (!(n = parse_areas(sget(refs), areas, 64))) { say("Refers to cells must be cells of this sheet, as A1:B5 or A1:A9,C1:C9."); continue; }
    l1_utf8(sget(title), tu, sizeof tu);
    l1_utf8(sget(pw), pwu, sizeof pwu);
    if (pwu[0]) {
      char again[200];
      Object *w2, *p2;
      int same;
      w2 = dialog_window("Confirm Password", HGroup, Child, Label1(tr("Reenter password to proceed:")), Child, p2 = secret_obj(), End, NULL);
      if (!w2) continue;
      set(w2, MUIA_Window_ActiveObject, (ULONG)p2);
      same = run_modal(w2, ends, NULL) == ID_DLGOK && (l1_utf8(sget(p2), again, sizeof again), !strcmp(again, pwu));
      MUI_DisposeObject(w2);
      if (!same) { say("The confirmation password is not identical."); continue; }
    }
    ok = book_edit_range_set(book, cur_index(), i, tu, areas, n, pwu, i >= 0 && !pwu[0]);
    break;
  }
  MUI_DisposeObject(w);
  return ok;
}
static void dlg_edit_ranges(void)
{
  static const ULONG ends[] = { ID_DLGOK, ID_DLG1, ID_DLG2, ID_DLG3, ID_DLG4, 0 };
  for (;;) {
    Object *w, *list, *add, *mod, *del, *prot;
    Sheet *s = cur_sheet();
    ULONG id;
    LONG a;
    int i, k;
    w = dialog_window("Allow Users to Edit Ranges", VGroup,
          Child, LLabel1(tr("Ranges unlocked by a password when the sheet is protected:")),
          Child, HGroup,
            Child, ListviewObject, MUIA_Listview_Input, TRUE, MUIA_FixHeightTxt, (ULONG)"\n\n\n\n\n\n", MUIA_FixWidthTxt, (ULONG)"MMMMMMMMMMMMMMMMMMMMMMMMMMMMMM",
              MUIA_Listview_List, list = ListObject, InputListFrame,
              MUIA_List_ConstructHook, MUIV_List_ConstructHook_String, MUIA_List_DestructHook, MUIV_List_DestructHook_String, End, End,
            Child, VGroup, Child, add = QButton(tr("New...")), Child, mod = QButton(tr("Modify...")), Child, del = QButton(tr("Delete")), Child, VSpace(0), End,
          End,
          Child, HGroup, Child, prot = QButton(tr(s->protect ? "Unprotect Sheet..." : "Protect Sheet...")), Child, HSpace(0), End,
        End, "Close");
    if (!w) return;
    for (i = 0; i < s->neranges; i++) {
      char t[200], tl[80], r[100];
      utf8_l1(s->eranges[i].title, tl, sizeof tl);
      r[0] = 0;
      for (k = 0; k < s->eranges[i].nareas && strlen(r) < 70; k++) { char x[40]; rect_l1(s->eranges[i].areas[k], x); if (k) strcat(r, ","); strcat(r, x); }
      snprintf(t, sizeof t, "%s  =  %s%s", tl, r, edit_range_has_pw(&s->eranges[i]) ? "  (password)" : "");
      DoMethod(list, MUIM_List_InsertSingle, (ULONG)t, MUIV_List_Insert_Bottom);
    }
    press_id(add, ID_DLG1); press_id(mod, ID_DLG2); press_id(del, ID_DLG3); press_id(prot, ID_DLG4);
    DoMethod(list, MUIM_Notify, MUIA_Listview_DoubleClick, TRUE, (ULONG)app, 2, MUIM_Application_ReturnID, ID_DLG2);
    id = run_modal(w, ends, NULL);
    a = (LONG)xget(list, MUIA_List_Active);
    MUI_DisposeObject(w);
    if (id == ID_DLG1) { edit_range_dialog(-1); continue; }
    if (id == ID_DLG2) { if (a >= 0 && a < s->neranges) edit_range_dialog((int)a); continue; }
    if (id == ID_DLG3) { if (a >= 0) book_edit_range_delete(book, cur_index(), (int)a); continue; }
    if (id == ID_DLG4) { datatab_command(ID_PROTECTSHEET, 0); continue; }
    break;
  }
  refresh_all();
}
int anatab_unlock(Book *b, int sheet, Rect a)
{
  static const ULONG ends[] = { ID_DLGOK, 0 };
  Sheet *s;
  EditRange *e;
  if (!b || sheet < 0 || sheet >= b->nsh || !(s = b->sh[sheet])->neranges) return 0;
  while ((e = edit_range_shut(s, a))) {
    Object *w, *pw;
    char msg[200], tl[80], u[200];
    int ok;
    utf8_l1(e->title, tl, sizeof tl);
    snprintf(msg, sizeof msg, "%s \"%s\":", tr("A cell you are trying to change is password protected. Password for"), tl);
    w = dialog_window("Unlock Range", VGroup, Child, LLabel1(msg), Child, pw = secret_obj(), End, NULL);
    if (!w) return 0;
    DoMethod(pw, MUIM_Notify, MUIA_String_Acknowledge, MUIV_EveryTime, (ULONG)app, 2, MUIM_Application_ReturnID, ID_DLGOK);
    set(w, MUIA_Window_ActiveObject, (ULONG)pw);
    ok = run_modal(w, ends, NULL) == ID_DLGOK;
    if (ok) l1_utf8(sget(pw), u, sizeof u);
    MUI_DisposeObject(w);
    if (!ok) return 0;
    if (!edit_range_unlock(e, u)) { say("The password you supplied is not correct."); return 0; }
  }
  return edit_ranges_open(s, a);
}

/* ------------------------------------------------------------------ Solver */
static SolverModel sm;                     /* the model being edited (the sheet's saved one when it has one) */
static int sm_book_sheet = -1;
static Book *sm_book;
static const char *const goals_en[] = { "Max", "Min", "Value Of:", NULL };
static const char *const methods_en[] = { "GRG Nonlinear", "Simplex LP", "Evolutionary", NULL };
static const char *const rels_en[] = { "<=", "=", ">=", "int", "bin", "dif", NULL };
static const char *const method_help[] = {
  "GRG Nonlinear: a local optimum of a smooth model, from the variables' current values.",
  "Simplex LP: linear models, solved exactly; whole numbers by branch and bound.",
  "Evolutionary: a genetic search for models that aren't smooth; every variable needs bounds." };
/* a constraint asked for (Add or Change); 1 if OK */
static int constraint_dialog(SolverCons *c, int change)
{
  static const ULONG ends[] = { ID_DLGOK, 0 };
  static const char *rels[8];
  Object *w, *lhs, *rel, *rhs;
  char l[140], r[140];
  int ok = 0;
  utf8_l1(c->lhs, l, sizeof l); utf8_l1(c->rhs, r, sizeof r);
  w = dialog_window(change ? "Change Constraint" : "Add Constraint", ColGroup(3),
        Child, LLabel1(tr("Cell Reference:")), Child, HSpace(0), Child, LLabel1(tr("Constraint:")),
        Child, lhs = str_obj(l, 120), Child, rel = cycle_obj(trl(rels_en, rels), c->rel >= SR_LE && c->rel <= SR_DIF ? c->rel - 1 : 0), Child, rhs = str_obj(r, 120),
      End, NULL);
  if (!w) return 0;
  set(w, MUIA_Window_ActiveObject, (ULONG)lhs);
  for (;;) {
    int k;
    if (run_modal(w, ends, NULL) != ID_DLGOK) break;
    k = (int)xget(rel, MUIA_Cycle_Active) + 1;
    l1_utf8(sget(lhs), c->lhs, sizeof c->lhs);
    l1_utf8(sget(rhs), c->rhs, sizeof c->rhs);
    c->rel = (u8)k;
    if (!c->lhs[0]) { say("Enter a cell reference."); continue; }
    if (k <= SR_GE && !c->rhs[0]) { say("Enter a constraint value: a number or a cell reference."); continue; }
    if (k > SR_GE) c->rhs[0] = 0;
    ok = 1;
    break;
  }
  MUI_DisposeObject(w);
  return ok;
}
/* Solver Results: keep or restore, and the reports. Returns 1 to go back to Solver Parameters */
static int solver_results(SolverResult *res)
{
  static const ULONG ends[] = { ID_DLGOK, 0 };
  static const char *keeps[3];
  Object *w, *keep, *ans, *lim, *back;
  char msg[300];
  int again = 0, solved = res->sol != NULL, id;
  utf8_l1(res->message, msg, sizeof msg);
  keeps[0] = tr("Keep Solver Solution"); keeps[1] = tr("Restore Original Values"); keeps[2] = NULL;
  w = dialog_window("Solver Results", VGroup,
        Child, TextObject, MUIA_Text_Contents, (ULONG)msg, MUIA_Text_SetMax, FALSE, End,
        Child, HGroup, Child, keep = cycle_obj(keeps, 0), Child, HSpace(0), End,
        Child, HGroup, Child, LLabel1(tr("Reports:")), Child, ans = check_obj(0), Child, LLabel1(tr("Answer")), Child, lim = check_obj(0), Child, LLabel1(tr("Limits")), Child, HSpace(0), End,
        Child, checkrow(&back, 0, "Return to Solver Parameters Dialog"),
      End, NULL);
  if (!w) return 0;
  set(keep, MUIA_Disabled, !solved);
  set(ans, MUIA_Disabled, !solved); set(lim, MUIA_Disabled, !solved);
  id = (int)run_modal(w, ends, NULL);
  if (id == ID_DLGOK) {
    int k = solved && xget(keep, MUIA_Cycle_Active) == 0, rep;
    again = checked(back);
    solver_finish(book, &sm, res, k, 1);
    for (rep = 0; rep < 2 && solved; rep++)
      if (checked(rep ? lim : ans)) {
        const char *why;
        int at = solver_report(book, &sm, res, rep, &why);
        if (at < 0) sayu(why); else { again = 0; show_sheet(at); }
      }
  } else solver_finish(book, &sm, res, 0, 1);
  MUI_DisposeObject(w);
  refresh_all();
  return again;
}
static void dlg_solver(void)
{
  static const ULONG ends[] = { ID_DLGOK, ID_DLG1, ID_DLG2, ID_DLG3, ID_DLG4, ID_DLG5, 0 };
  static const char *goals[4], *methods[4];
  int sel = -1;
  if (sm_book != book || sm_book_sheet != cur_index()) {
    solver_model_free(&sm);
    if (!solver_load(book, cur_index(), &sm)) {
      char t[40];
      solver_model_init(&sm, cur_index());
      ref_text(cur_sheet()->cur_r, cur_sheet()->cur_c, 1, 1, t);
      l1_utf8(t, sm.objective, sizeof sm.objective);
    }
    sm_book = book; sm_book_sheet = cur_index();
  }
  for (;;) {
    Object *w, *obj, *goal, *val, *vars, *list, *add, *chg, *del, *reset, *nonneg, *meth, *multi, *evals, *help;
    char t[300], vtext[40];
    ULONG id;
    LONG a;
    int i;
    sm.sheet = cur_index();
    snprintf(vtext, sizeof vtext, "%.15g", sm.value);
    w = dialog_window("Solver Parameters", VGroup,
          Child, ColGroup(2),
            Child, Label1(tr("Set Objective:")), Child, obj = str_obj("", 60),
            Child, Label1(tr("To:")), Child, HGroup, Child, goal = cycle_obj(trl(goals_en, goals), sm.goal), Child, val = str_obj(vtext, 30), End,
            Child, Label1(tr("By Changing Variable Cells:")), Child, vars = str_obj("", 250),
          End,
          Child, LLabel1(tr("Subject to the Constraints:")),
          Child, HGroup,
            Child, ListviewObject, MUIA_Listview_Input, TRUE, MUIA_FixHeightTxt, (ULONG)"\n\n\n\n\n", MUIA_FixWidthTxt, (ULONG)"MMMMMMMMMMMMMMMMMMMMMMMMMMMM",
              MUIA_Listview_List, list = ListObject, InputListFrame,
              MUIA_List_ConstructHook, MUIV_List_ConstructHook_String, MUIA_List_DestructHook, MUIV_List_DestructHook_String, End, End,
            Child, VGroup, Child, add = QButton(tr("Add...")), Child, chg = QButton(tr("Change...")), Child, del = QButton(tr("Delete")),
              Child, reset = QButton(tr("Reset All")), Child, VSpace(0), End,
          End,
          Child, checkrow(&nonneg, sm.nonneg, "Make Unconstrained Variables Non-Negative"),
          Child, HGroup, Child, Label1(tr("Select a Solving Method:")), Child, meth = cycle_obj(trl(methods_en, methods), sm.method),
            Child, multi = check_obj(sm.multistart), Child, LLabel1(tr("Multistart")), End,
          Child, HGroup, Child, Label1(tr("Max evaluations:")), Child, evals = str_obj("", 10), Child, HSpace(0), End,
          Child, help = TextObject, MUIA_Text_Contents, (ULONG)tr(method_help[sm.method < 3 ? sm.method : 0]), MUIA_Text_SetMax, FALSE, End,
        End, "Solve");
    if (!w) return;
    utf8_l1(sm.objective, t, sizeof t); set(obj, MUIA_String_Contents, (ULONG)t);
    utf8_l1(sm.vars, t, sizeof t); set(vars, MUIA_String_Contents, (ULONG)t);
    if (sm.maxevals > 0) { sprintf(t, "%ld", sm.maxevals); set(evals, MUIA_String_Contents, (ULONG)t); }
    for (i = 0; i < sm.ncons; i++) { char u[300]; solver_cons_text(&sm.cons[i], u, sizeof u); utf8_l1(u, t, sizeof t); DoMethod(list, MUIM_List_InsertSingle, (ULONG)t, MUIV_List_Insert_Bottom); }
    if (sel >= 0 && sel < sm.ncons) set(list, MUIA_List_Active, sel);
    set(val, MUIA_Disabled, sm.goal != SG_VALUE);
    set(multi, MUIA_Disabled, sm.method != SM_GRG);
    press_id(add, ID_DLG1); press_id(chg, ID_DLG2); press_id(del, ID_DLG3); press_id(reset, ID_DLG4);
    notify_id(goal, MUIA_Cycle_Active, ID_DLG5); notify_id(meth, MUIA_Cycle_Active, ID_DLG5);
    DoMethod(list, MUIM_Notify, MUIA_Listview_DoubleClick, TRUE, (ULONG)app, 2, MUIM_Application_ReturnID, ID_DLG2);
    set(w, MUIA_Window_ActiveObject, (ULONG)obj);
    id = run_modal(w, ends, NULL);
    a = (LONG)xget(list, MUIA_List_Active);
    /* the boxes into the model, whatever ended it (but Cancel) */
    {
      l1_utf8(sget(obj), sm.objective, sizeof sm.objective);
      l1_utf8(sget(vars), sm.vars, sizeof sm.vars);
      sm.goal = (u8)xget(goal, MUIA_Cycle_Active);
      sm.value = numof(val, 0);
      sm.nonneg = (u8)checked(nonneg);
      sm.method = (u8)xget(meth, MUIA_Cycle_Active);
      sm.multistart = (u8)checked(multi);
      sm.maxevals = (long)numof(evals, 0);
    }
    MUI_DisposeObject(w);
    sel = (int)a;
    if (id == ID_DLG5) continue;
    if (id == ID_DLG1) { SolverCons c; memset(&c, 0, sizeof c); c.rel = SR_LE; if (constraint_dialog(&c, 0)) { solver_add(&sm, c.lhs, c.rel, c.rhs); sel = sm.ncons - 1; } continue; }
    if (id == ID_DLG2) { if (a >= 0 && a < sm.ncons) { SolverCons c = sm.cons[a]; if (constraint_dialog(&c, 1)) sm.cons[a] = c; } continue; }
    if (id == ID_DLG3) { if (a >= 0) solver_remove(&sm, (int)a); continue; }
    if (id == ID_DLG4) {
      if (ask("OK|Cancel", "%s", tr("Reset all Solver options and cell selections?")) == 1) { int sh = sm.sheet; solver_model_free(&sm); solver_model_init(&sm, sh); }
      continue;
    }
    if (id == ID_DLGOK) {
      SolverResult res;
      int st;
      if (!sm.objective[0]) { say("Enter the objective cell (Set Objective)."); continue; }
      set(win, MUIA_Window_Sleep, TRUE);
      set(statusbar, MUIA_Text_Contents, (ULONG)tr("Solver: working..."));
      st = solver_solve(book, &sm, &res);
      set(win, MUIA_Window_Sleep, FALSE);
      if (st == SS_BADMODEL) { sayu(res.message); solver_result_free(&res); continue; }
      if (res.sol) {
        /* the variable cells must be changeable on a protected sheet */
        int j, may = 1;
        for (j = 0; j < res.n && may; j++) { Rect one; one.r1 = one.r2 = res.vr[j]; one.c1 = one.c2 = res.vc[j]; if (prot_locked(book, sm.sheet, res.vr[j], res.vc[j])) may = datatab_may(book, sm.sheet, DT_CELLS, one); }
        if (!may) { solver_result_free(&res); continue; }
      }
      i = solver_results(&res);
      solver_result_free(&res);
      if (i) continue;
      break;
    }
    /* Close keeps what was typed in the model (as Excel does) */
    if (id == ID_DLGCANCEL) solver_save(book, &sm);
    break;
  }
  refresh_all();
}

/* ------------------------------------------------------------------ Data Analysis */
enum { FK_TEXT, FK_NUM, FK_INT, FK_CHECK, FK_CYCLE, FK_END };
typedef struct { u8 kind; const char *label; u16 off; const char *const *opts; } TF;
#define O(f) ((u16)offsetof(TPParams, f))
static const char *const grouped_en[] = { "Columns", "Rows", NULL };
static const char *const dists_en[] = { "Uniform", "Normal", "Bernoulli", "Binomial", "Poisson", "Patterned", "Discrete", NULL };
static const char *const methods2_en[] = { "Random", "Periodic", NULL };
#define IN1 { FK_TEXT, "Input Range:", O(in1), NULL }
#define GROUPED { FK_CYCLE, "Grouped By:", O(byrows), grouped_en }
#define LABELS { FK_CHECK, "Labels in first row", O(labels), NULL }
#define ALPHA { FK_NUM, "Alpha:", O(alpha), NULL }
#define V12 { FK_TEXT, "Variable 1 Range:", O(in1), NULL }, { FK_TEXT, "Variable 2 Range:", O(in2), NULL }
#define END_ { FK_END, NULL, 0, NULL }
static const TF tf_anova1[] = { IN1, GROUPED, LABELS, ALPHA, END_ };
static const TF tf_anova2r[] = { IN1, { FK_INT, "Rows per sample:", O(rowsper), NULL }, ALPHA, END_ };
static const TF tf_anova2[] = { IN1, { FK_CHECK, "Labels", O(labels), NULL }, ALPHA, END_ };
static const TF tf_matrix[] = { IN1, GROUPED, LABELS, END_ };
static const TF tf_descr[] = { IN1, GROUPED, LABELS, { FK_CHECK, "Summary statistics", O(summary), NULL }, { FK_NUM, "Confidence Level for Mean (%, 0 none):", O(conf), NULL },
                               { FK_INT, "Kth Largest (0 none):", O(kl), NULL }, { FK_INT, "Kth Smallest (0 none):", O(ks), NULL }, END_ };
static const TF tf_expo[] = { IN1, { FK_NUM, "Damping factor:", O(damping), NULL }, LABELS, { FK_CHECK, "Standard Errors", O(stderrs), NULL }, END_ };
static const TF tf_ftest[] = { V12, LABELS, ALPHA, END_ };
static const TF tf_fourier[] = { IN1, LABELS, { FK_CHECK, "Inverse", O(inverse), NULL }, END_ };
static const TF tf_hist[] = { IN1, { FK_TEXT, "Bin Range (blank: automatic):", O(in2), NULL }, LABELS, { FK_CHECK, "Pareto (sorted histogram)", O(pareto), NULL },
                              { FK_CHECK, "Cumulative Percentage", O(cumulative), NULL }, END_ };
static const TF tf_movavg[] = { IN1, LABELS, { FK_INT, "Interval:", O(interval), NULL }, { FK_CHECK, "Standard Errors", O(stderrs), NULL }, END_ };
static const TF tf_random[] = { { FK_INT, "Number of Variables:", O(nvars), NULL }, { FK_INT, "Number of Random Numbers:", O(nnums), NULL },
                                { FK_CYCLE, "Distribution:", O(dist), dists_en }, { FK_NUM, "p1", O(p1), NULL }, { FK_NUM, "p2", O(p2), NULL }, { FK_NUM, "p3", O(p3), NULL },
                                { FK_NUM, "p4", O(p4), NULL }, { FK_NUM, "p5", O(p5), NULL }, { FK_TEXT, "Value and Probability Input Range:", O(in2), NULL },
                                { FK_INT, "Random Seed (0 none):", O(seed), NULL }, END_ };
static const TF tf_rank[] = { IN1, GROUPED, LABELS, END_ };
static const TF tf_regr[] = { { FK_TEXT, "Input Y Range:", O(in1), NULL }, { FK_TEXT, "Input X Range:", O(in2), NULL }, LABELS, { FK_CHECK, "Constant is Zero", O(constzero), NULL },
                              { FK_NUM, "Confidence Level (%):", O(conf), NULL }, { FK_CHECK, "Residuals", O(residuals), NULL }, { FK_CHECK, "Standardized Residuals", O(stdres), NULL },
                              { FK_CHECK, "Normal Probability", O(normprob), NULL }, END_ };
static const TF tf_sampling[] = { IN1, LABELS, { FK_CYCLE, "Sampling Method:", O(periodic), methods2_en }, { FK_INT, "Period or Number of Samples:", O(nsample), NULL }, END_ };
static const TF tf_ttest[] = { V12, { FK_NUM, "Hypothesized Mean Difference:", O(hmd), NULL }, LABELS, ALPHA, END_ };
static const TF tf_ztest[] = { V12, { FK_NUM, "Hypothesized Mean Difference:", O(hmd), NULL }, { FK_NUM, "Variable 1 Variance (known):", O(var1), NULL },
                               { FK_NUM, "Variable 2 Variance (known):", O(var2), NULL }, LABELS, ALPHA, END_ };
static const TF *const tool_fields[TP_N] = { tf_anova1, tf_anova2r, tf_anova2, tf_matrix, tf_matrix, tf_descr, tf_expo, tf_ftest, tf_fourier, tf_hist,
                                             tf_movavg, tf_random, tf_rank, tf_regr, tf_sampling, tf_ttest, tf_ttest, tf_ttest, tf_ztest };
/* each distribution's parameter names (p1..p5; NULL: not used) */
static const char *const dist_params[TPD_N][5] = {
  { "Between:", "and:", NULL, NULL, NULL }, { "Mean:", "Standard deviation:", NULL, NULL, NULL }, { "p Value:", NULL, NULL, NULL, NULL },
  { "p Value:", "Number of trials:", NULL, NULL, NULL }, { "Lambda:", NULL, NULL, NULL, NULL },
  { "From:", "To:", "In steps of:", "Repeating each number:", "Repeating each sequence:" }, { NULL, NULL, NULL, NULL, NULL } };
static TPParams tpp;                        /* the last tool's settings, kept between uses */
static struct { Object *obj[16], *lab[16]; int n; const TF *tf; } tpf;
static void tp_dist_labels(void)
{
  int i, k, d;
  if (tpf.tf != tf_random) return;
  for (i = 0; tpf.tf[i].kind != FK_END; i++) if (tpf.tf[i].off == O(dist)) break;
  d = (int)xget(tpf.obj[i], MUIA_Cycle_Active);
  for (k = 0; k < 5; k++) {
    const char *l = d >= 0 && d < TPD_N ? dist_params[d][k] : NULL;
    set(tpf.lab[i + 1 + k], MUIA_Text_Contents, (ULONG)(l ? tr(l) : "-"));
    set(tpf.obj[i + 1 + k], MUIA_Disabled, l == NULL);
  }
  set(tpf.obj[i + 6], MUIA_Disabled, d != TPD_DISCRETE);
  set(tpf.obj[1], MUIA_Disabled, d == TPD_PATTERNED);
}
static void tp_each(ULONG id) { if (id == ID_DLG1) tp_dist_labels(); }
/* the chosen tool's form; 0 cancelled */
static int tool_form(int tool)
{
  static const ULONG ends[] = { ID_DLGOK, 0 };
  static const char *cyc[16][8];
  static const char *outs[3];
  Object *w, *form, *outmode, *out, *newsh;
  const TF *tf = tool_fields[tool];
  int i, ok = 0;
  form = MUI_NewObject(MUIC_Group, MUIA_Group_Columns, 2, TAG_DONE);
  if (!form) return 0;
  tpf.tf = tf; tpf.n = 0;
  for (i = 0; tf[i].kind != FK_END && i < 16; i++) {
    char *field = (char *)&tpp + tf[i].off, t[200];
    Object *o, *l, *ch = NULL;
    switch (tf[i].kind) {
    case FK_TEXT: utf8_l1(field, t, sizeof t); o = str_obj(t, 150); break;
    case FK_NUM: snprintf(t, sizeof t, "%.15g", *(double *)field); o = str_obj(t, 30); break;
    case FK_INT: snprintf(t, sizeof t, "%ld", tf[i].off == O(seed) ? (long)*(u32 *)field : (long)*(int *)field); o = str_obj(t, 12); break;
    case FK_CHECK: ch = check_obj(*(u8 *)field); o = HGroup, Child, ch, Child, HSpace(0), End; break;
    default: o = cycle_obj(trl(tf[i].opts, cyc[i]), 0); break;
    }
    if (tf[i].kind == FK_CYCLE) {
      int v = tf[i].off == O(dist) ? tpp.dist : *(u8 *)field;
      set(o, MUIA_Cycle_Active, v);
    }
    l = TextObject, MUIA_Text_Contents, (ULONG)tr(tf[i].label), MUIA_Text_PreParse, (ULONG)"\33r", MUIA_Text_SetMax, FALSE, End;
    DoMethod(form, OM_ADDMEMBER, (ULONG)l);
    DoMethod(form, OM_ADDMEMBER, (ULONG)o);
    tpf.obj[i] = tf[i].kind == FK_CHECK ? ch : o;
    tpf.lab[i] = l;
    if (tf[i].off == O(dist)) notify_id(o, MUIA_Cycle_Active, ID_DLG1);
  }
  tpf.n = i;
  outs[0] = tr("Output Range:"); outs[1] = tr("New Worksheet Ply:"); outs[2] = NULL;
  {
    char o1[80], o2[40];
    utf8_l1(tpp.out, o1, sizeof o1); utf8_l1(tpp.newsheet, o2, sizeof o2);
    w = dialog_window(toolpak_names[tool], VGroup,
          Child, form,
          Child, QTitleObject(tr("Output options")),
          Child, ColGroup(2),
            Child, outmode = cycle_obj(outs, tpp.outnew ? 1 : 0), Child, HGroup, Child, out = str_obj(o1, 60), Child, newsh = str_obj(o2, 31), End,
          End,
        End, NULL);
  }
  if (!w) return 0;
  tp_dist_labels();
  for (;;) {
    if (run_modal(w, ends, tp_each) != ID_DLGOK) break;
    for (i = 0; i < tpf.n; i++) {
      char *field = (char *)&tpp + tf[i].off;
      Object *o = tpf.obj[i];
      switch (tf[i].kind) {
      case FK_TEXT: l1_utf8(sget(o), field, 160); break;
      case FK_NUM: *(double *)field = numof(o, *(double *)field); break;
      case FK_INT: if (tf[i].off == O(seed)) *(u32 *)field = (u32)numof(o, 0); else *(int *)field = (int)numof(o, *(int *)field); break;
      case FK_CHECK: *(u8 *)field = (u8)checked(o); break;
      default: if (tf[i].off == O(dist)) tpp.dist = (int)xget(o, MUIA_Cycle_Active); else *(u8 *)field = (u8)xget(o, MUIA_Cycle_Active); break;
      }
    }
    tpp.outnew = xget(outmode, MUIA_Cycle_Active) == 1;
    l1_utf8(sget(out), tpp.out, sizeof tpp.out);
    l1_utf8(sget(newsh), tpp.newsheet, sizeof tpp.newsheet);
    if (!tpp.outnew && !tpp.out[0]) { say("Enter the output range's top left cell."); continue; }
    tpp.tool = tool;
    tpp.overwrite = 0;
    {
      Rect where;
      const char *why;
      int at;
      set(win, MUIA_Window_Sleep, TRUE);
      at = toolpak_run(book, cur_index(), &tpp, &where, &why);
      set(win, MUIA_Window_Sleep, FALSE);
      if (at == -2) {
        char msg[200], ref[40];
        rect_l1(where, ref);
        snprintf(msg, sizeof msg, "%s %s", tr("Output range will overwrite existing data. Press OK to overwrite data in range:"), ref);
        if (ask("OK|Cancel", "%s", msg) != 1) continue;
        tpp.overwrite = 1;
        set(win, MUIA_Window_Sleep, TRUE);
        at = toolpak_run(book, cur_index(), &tpp, &where, &why);
        set(win, MUIA_Window_Sleep, FALSE);
      }
      if (at < 0) { sayu(why ? why : "The tool couldn't make its report."); continue; }
      if (tpp.outnew) show_sheet(at);
      else { cur_sheet()->sel = where; refresh_all(); }
      ok = 1;
    }
    break;
  }
  MUI_DisposeObject(w);
  return ok;
}
static void dlg_toolpak(void)
{
  static const ULONG ends[] = { ID_DLGOK, 0 };
  static int last = TP_DESCR;
  Object *w, *list;
  int i;
  LONG a;
  w = dialog_window("Data Analysis", VGroup,
        Child, LLabel1(tr("Analysis Tools")),
        Child, ListviewObject, MUIA_Listview_Input, TRUE, MUIA_FixHeightTxt, (ULONG)"\n\n\n\n\n\n\n\n\n\n\n\n", MUIA_FixWidthTxt, (ULONG)"MMMMMMMMMMMMMMMMMMMMMMMMMMMMMM",
          MUIA_Listview_List, list = ListObject, InputListFrame,
          MUIA_List_ConstructHook, MUIV_List_ConstructHook_String, MUIA_List_DestructHook, MUIV_List_DestructHook_String, End, End,
      End, NULL);
  if (!w) return;
  for (i = 0; i < TP_N; i++) DoMethod(list, MUIM_List_InsertSingle, (ULONG)tr(toolpak_names[i]), MUIV_List_Insert_Bottom);
  set(list, MUIA_List_Active, last);
  DoMethod(list, MUIM_Notify, MUIA_Listview_DoubleClick, TRUE, (ULONG)app, 2, MUIM_Application_ReturnID, ID_DLGOK);
  set(w, MUIA_Window_ActiveObject, (ULONG)list);
  i = run_modal(w, ends, NULL) == ID_DLGOK;
  a = (LONG)xget(list, MUIA_List_Active);
  MUI_DisposeObject(w);
  if (!i || a < 0 || a >= TP_N) return;
  if (tpp.tool != (int)a || !tpp.alpha) {
    char t[40];
    toolpak_defaults(&tpp, (int)a);
    rect_text(cur_sel(), t);
    if (a != TP_RANDOM) strcpy(tpp.in1, t);
  }
  last = (int)a;
  tool_form((int)a);
}

/* ------------------------------------------------------------------ Forecast Sheet */
static const char *const seasons_en[] = { "Detect Automatically", "None", "Set Manually", NULL };
static const char *const fills_en[] = { "Interpolation", "Zeros", NULL };
static const char *const aggs_en[] = { "Average", "Count", "CountA", "Max", "Median", "Min", "Sum", NULL };
static const char *const charts_en[] = { "Line", "Column", NULL };
static void dlg_forecast(void)
{
  static const ULONG ends[] = { ID_DLGOK, 0 };
  static const char *seasons[4], *fills[3], *aggs[8], *charts[3];
  Object *w, *tl, *vl, *end, *season, *slen, *ci, *conf, *fill, *agg, *stats, *chart;
  Rect a = region();
  ForecastOpts o;
  char t1[40], t2[40], te[40], u[200];
  double last, step;
  int np;
  Rect c1 = a, c2 = a;
  forecast_defaults(&o);
  c1.c2 = c1.c1; c2.c1 = c2.c2 = a.c1 + 1;
  rect_text(c1, t1); rect_text(c2, t2);
  te[0] = 0;
  if (forecast_timeline(book, cur_index(), t1, &last, &step, &np)) num_general(last + step * ((np + 2) / 3), te, 11);
  if (a.c2 <= a.c1) { say("Select a timeline column and a values column for the forecast."); return; }
  w = dialog_window("Create Forecast Worksheet", VGroup,
        Child, ColGroup(2),
          Child, Label1(tr("Timeline Range:")), Child, tl = str_obj(t1, 120),
          Child, Label1(tr("Values Range:")), Child, vl = str_obj(t2, 120),
          Child, Label1(tr("Forecast End:")), Child, end = str_obj(te, 30),
          Child, Label1(tr("Seasonality:")), Child, HGroup, Child, season = cycle_obj(trl(seasons_en, seasons), 0), Child, slen = str_obj("", 6), End,
          Child, Label1(tr("Confidence Interval:")), Child, HGroup, Child, ci = check_obj(1), Child, conf = str_obj("95", 6), Child, LLabel1("%"), Child, HSpace(0), End,
          Child, Label1(tr("Fill Missing Points Using:")), Child, fill = cycle_obj(trl(fills_en, fills), 0),
          Child, Label1(tr("Aggregate Duplicates Using:")), Child, agg = cycle_obj(trl(aggs_en, aggs), 0),
          Child, Label1(tr("Chart:")), Child, chart = cycle_obj(trl(charts_en, charts), 0),
        End,
        Child, checkrow(&stats, 0, "Include Forecast Statistics"),
      End, "Create");
  if (!w) return;
  for (;;) {
    const char *why;
    int at, kind;
    if (run_modal(w, ends, NULL) != ID_DLGOK) break;
    l1_utf8(sget(tl), o.timeline, sizeof o.timeline);
    l1_utf8(sget(vl), o.values, sizeof o.values);
    l1_utf8(sget(end), u, sizeof u);
    o.end = 0;
    if (u[0] && !parse_number(u, &o.end, &kind)) { say("Forecast End must be a number or a date on the timeline."); continue; }
    switch (xget(season, MUIA_Cycle_Active)) { case 0: o.seasonality = -1; break; case 1: o.seasonality = 0; break; default: o.seasonality = (int)numof(slen, 0); }
    o.conf = checked(ci) ? numof(conf, 95) / 100 : 0;
    o.fill = xget(fill, MUIA_Cycle_Active) == 0;
    o.aggregate = (int)xget(agg, MUIA_Cycle_Active) + 1;
    o.stats = (u8)checked(stats);
    o.column = xget(chart, MUIA_Cycle_Active) == 1;
    set(win, MUIA_Window_Sleep, TRUE);
    at = forecast_sheet(book, cur_index(), &o, &why);
    set(win, MUIA_Window_Sleep, FALSE);
    if (at < 0) { sayu(why ? why : "The forecast couldn't be made."); continue; }
    show_sheet(at);
    break;
  }
  MUI_DisposeObject(w);
}

/* ------------------------------------------------------------------ the commands */
void anatab_command(ULONG id)
{
  switch (id) {
  case ID_AN_FORECAST:
    if (book->lockstruct) { say("The workbook's structure is protected: sheets can't be added.\nUnprotect it first (Review, Protect Workbook)."); break; }
    dlg_forecast();
    break;
  case ID_AN_SOLVER: dlg_solver(); break;
  case ID_AN_TOOLPAK: dlg_toolpak(); break;
  case ID_AN_LOCK: {
    Sheet *s = cur_sheet();
    if (s->protect && !(s->protallow & PA_FMTCELLS)) { datatab_may(book, cur_index(), DT_CELLS, cur_sel()); break; }
    book_lock_cells(book, cur_index(), cur_sel(), !cell_locked(book, cur_index(), s->cur_r, s->cur_c));
    refresh_all();
    break; }
  case ID_AN_EDITRANGES:
    if (cur_sheet()->protect) { say("Allow Edit Ranges can't be changed while the sheet is protected.\nUnprotect it first (Review, Protect Sheet)."); break; }
    dlg_edit_ranges();
    break;
  case ID_AN_CHECKBOX:
    if (!datatab_may(book, cur_index(), DT_CELLS, cur_sel())) break;
    if (!book_checkbox(book, cur_index(), cur_sel())) say("Select fewer cells (100,000 at most).");
    refresh_all();
    break;
  case ID_AN_TOGGLE: checkbox_toggle_sel(); break;
  }
}
