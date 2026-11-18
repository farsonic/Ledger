/*
 * datatab.c - the Data tab's commands and dialogs (and Review's protection), on the workbook
 * through the core's data.h: Data Validation and its drop-down list, Circle Invalid Data, Text to
 * Columns with a preview, Flash Fill, Remove Duplicates by column, Consolidate, the Advanced
 * Filter, What-If Analysis (Scenario Manager, Goal Seek, Data Table), the outline (Group, Ungroup,
 * Subtotal, Show and Hide Detail, Settings), Get Data from a text file, and Protect Sheet and
 * Protect Workbook, which also guard every other command here: a protected sheet's locked cells
 * refuse changes with a message.
 *
 * The dialogs follow GridCraft's (crates/ui-egui/src/dialogs.rs), laid out for a 640 x 480 screen.
 * Like dialogs.c's they are modal (run_modal), made when asked for and disposed of after.
 */
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <proto/exec.h>
#include <proto/intuition.h>
#include <proto/muimaster.h>
#include <proto/utility.h>
#include <proto/dos.h>
#include <libraries/asl.h>
#include <clib/alib_protos.h>
#include "app.h"
#include "theme.h"
#include "lang.h"
#include "ids.h"
#include "datatab.h"
#include "anatab.h"
#include "../core/calc.h"
#include "../core/cond.h"
#include "../core/data.h"
#include "../core/xlsx.h"

#undef Label1
#undef LLabel1
#define Label1(l)  QLabel(l, MUIO_Label_SingleFrame)
#define LLabel1(l) QLabel(l, MUIO_Label_LeftAligned | MUIO_Label_SingleFrame)

static ULONG xget(Object *o, ULONG attr) { ULONG v = 0; get(o, attr, &v); return v; }
static const char *sget(Object *o) { const char *s = (const char *)xget(o, MUIA_String_Contents); return s ? s : ""; }
static void latin1_utf8(const char *s, char *u, size_t cap)
{
  size_t o = 0;
  for (; *s && o + 4 < cap; s++) o += (size_t)utf8_put(u + o, (u8)*s);
  u[o] = 0;
}
static void to_l1(const char *u, char *out, size_t cap) { utf8_to_latin1(u ? u : "", u ? (u32)strlen(u) : 0, out, cap); }
static Object *str_obj(const char *init, int max) { return StringObject, StringFrame, MUIA_String_Contents, (ULONG)init, MUIA_String_MaxLen, max, MUIA_CycleChain, 1, End; }
static Object *secret_obj(void) { return StringObject, StringFrame, MUIA_String_Secret, TRUE, MUIA_String_MaxLen, 64, MUIA_CycleChain, 1, End; }
static Object *check_obj(int on)
{
  return ImageObject, ImageButtonFrame, MUIA_InputMode, MUIV_InputMode_Toggle, MUIA_Image_Spec, MUII_CheckMark,
    MUIA_Image_FreeVert, TRUE, MUIA_Selected, on, MUIA_Background, MUII_ButtonBack, MUIA_ShowSelState, FALSE, MUIA_CycleChain, 1, End;
}
static Object *cycle_obj(const char **labels, int active) { return CycleObject, MUIA_Cycle_Entries, (ULONG)labels, MUIA_Cycle_Active, active, MUIA_CycleChain, 1, End; }
static Object *checkrow(Object **chk, int on, const char *label)
{
  return HGroup, Child, *chk = check_obj(on), Child, LLabel1(tr(label)), Child, HSpace(0), End;
}
static const char **trl(const char *const *en, const char **out)
{
  int i;
  for (i = 0; en[i]; i++) out[i] = tr(en[i]);
  out[i] = NULL;
  return out;
}
static void notify_id(Object *o, ULONG attr, ULONG id) { DoMethod(o, MUIM_Notify, attr, MUIV_EveryTime, (ULONG)app, 2, MUIM_Application_ReturnID, id); }
static void press_id(Object *o, ULONG id) { DoMethod(o, MUIM_Notify, MUIA_Pressed, FALSE, (ULONG)app, 2, MUIM_Application_ReturnID, id); }
static int checked(Object *o) { return xget(o, MUIA_Selected) != 0; }
static void say(const char *msg) { ask("OK", "%s", msg); }
/* a range or cell typed (Latin-1: "B2", "Sheet2!A1:C9", a name): its sheet (sh) and rectangle; 0 if it isn't one */
static int parse_range(const char *l1, int *sh, Rect *r)
{
  char u[300];
  u16 s;
  u8 fl;
  int n, i;
  latin1_utf8(l1, u, sizeof u);
  while (u[0] == ' ' || u[0] == '=') memmove(u, u + 1, strlen(u));
  n = parse_ref(book, u, &s, r, &fl);
  if (!n || u[n]) {
    for (i = 0; i < book->nnames; i++)
      if (str_ieq(book->names[i].name, u) && book->names[i].formula_text) {
        const char *ft = book->names[i].formula_text;
        n = parse_ref(book, ft[0] == '=' ? ft + 1 : ft, &s, r, &fl);
        break;
      }
    if (!n) return 0;
  }
  if (fl & RF_COLS) { r->r1 = 0; r->r2 = MAXROW - 1; }
  if (fl & RF_ROWS) { r->c1 = 0; r->c2 = MAXCOL - 1; }
  *sh = s == SHEET_SELF ? cur_index() : s;
  return *sh >= 0 && *sh < book->nsh;
}
static void range_text(Rect a, int abs_, char *out)
{
  char x[16], y[16];
  ref_text(a.r1, a.c1, (u8)abs_, (u8)abs_, x);
  if (a.r1 == a.r2 && a.c1 == a.c2) { strcpy(out, x); return; }
  ref_text(a.r2, a.c2, (u8)abs_, (u8)abs_, y);
  sprintf(out, "%s:%s", x, y);
}
/* the block of filled cells round the active cell, or the selection (trimmed to what's used) */
static Rect data_region(void)
{
  Sheet *s = cur_sheet();
  Rect a = cur_sel();
  int grew = 1;
  if (a.r1 != a.r2 || a.c1 != a.c2) {
    if (a.r2 > sheet_last_row(s)) a.r2 = sheet_last_row(s) > a.r1 ? sheet_last_row(s) : a.r1;
    if (a.c2 > sheet_last_col(s)) a.c2 = sheet_last_col(s) > a.c1 ? sheet_last_col(s) : a.c1;
    return a;
  }
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
static int looks_like_header(Rect a)
{
  i32 c;
  int text = 0, nums = 0;
  if (a.r2 <= a.r1) return 0;
  for (c = a.c1; c <= a.c2; c++) {
    const Value *v = cell_value(book, cur_index(), a.r1, c), *w = cell_value(book, cur_index(), a.r1 + 1, c);
    if (v->t == V_NUM) return 0;
    if (v->t == V_STR) text++;
    if (w->t == V_NUM) nums++;
  }
  return text > 0 && (nums > 0 || text == a.c2 - a.c1 + 1);
}
/* a column's name for the lists: its heading ("Sales (B)") or "Column B" */
static void col_label(Rect a, i32 c, int header, char *out, size_t cap)
{
  char cn[8], h[64];
  u32 colr;
  col_name(c, cn);
  h[0] = 0;
  if (header) grid_cell_text(book, cur_index(), a.r1, c, 0, h, sizeof h, &colr);
  if (header && h[0]) snprintf(out, cap, "%s (%s)", h, cn);
  else snprintf(out, cap, "%s %s", tr("Column"), cn);
}

/* ------------------------------------------------------------------ protection's guard */
static void protected_msg(void)
{
  say("The cell or chart you're trying to change is on a protected sheet.\n"
      "To make a change, unprotect the sheet (Review, Protect Sheet).\nYou might be asked for a password.");
}
int datatab_may(Book *b, int sheet, int what, Rect a)
{
  Sheet *s;
  if (!b || sheet < 0 || sheet >= b->nsh || !(s = b->sh[sheet])->protect) return 1;
  if (what == DT_CELLS ? !prot_area_locked(b, sheet, a) : what == DT_ROWS ? (s->protallow & PA_FMTROWS) != 0 : (s->protallow & PA_FMTCOLS) != 0) return 1;
  if (what == DT_CELLS && s->neranges && anatab_unlock(b, sheet, a)) return 1;     /* (anatab) Allow Edit Ranges' passwords */
  protected_msg();
  return 0;
}
int datatab_hide_formula(Book *b, int sheet, i32 r, i32 c) { return prot_hidden(b, sheet, r, c); }
/* what a command needs on a protected sheet */
enum { G_NONE, G_CELLS, G_FORMAT, G_ROWS, G_COLS, G_INSROWS, G_INSCOLS, G_DELROWS, G_DELCOLS, G_SORT, G_FILTER, G_LINKS, G_OBJECTS, G_ALWAYS, G_STRUCT };
static int guard_of(ULONG id, LONG v)
{
  switch (id) {
  case ID_CUT: case ID_PASTE: case ID_PASTEVALUES: case ID_PASTEFORMULAS: case ID_PASTETRANSPOSE: case ID_CLEARALL: case ID_CLEARCONTENTS:
  case ID_FILLDOWN: case ID_FILLRIGHT: case ID_FILLUP: case ID_FILLLEFT: case ID_FILLSERIES: case ID_AUTOSUM: case ID_DATE: case ID_TIME:
  case ID_TEXTCOLS: case ID_FLASHFILL: case ID_REMOVEDUP: case ID_AUTOSUMDD: case ID_FILLDD: case ID_PASTEDD:
    return G_CELLS;
  case ID_PASTEFORMATS: case ID_CLEARFORMATS: case ID_BRUSH: case ID_GROW: case ID_SHRINK: case ID_BOLD: case ID_ITALIC: case ID_UNDERLINE: case ID_STRIKE:
  case ID_ATOP: case ID_AMIDDLE: case ID_ABOTTOM: case ID_LEFT: case ID_CENTER: case ID_RIGHT: case ID_WRAP: case ID_INDENT: case ID_OUTDENT:
  case ID_MERGECENTER: case ID_CURRENCY: case ID_PERCENT: case ID_COMMA: case ID_INCDEC: case ID_DECDEC: case ID_FORMATCELLS:
  case ID_CONDNEW: case ID_CONDMANAGE: case ID_CONDCLEAR: case ID_CONDCLEARALL:
  case ID_FONTDD: case ID_SIZEDD: case ID_FONTCOLOR: case ID_FILLCOLOR: case ID_BORDERDD: case ID_MERGEDD: case ID_NUMFMTDD: case ID_CELLSTYLEDD: case ID_CONDDD:
    return G_FORMAT;
  case ID_ROWHEIGHT: case ID_AUTOFITROW: case ID_HIDEROWS: case ID_UNHIDEROWS: return G_ROWS;
  case ID_COLWIDTH: case ID_AUTOFITCOL: case ID_HIDECOLS: case ID_UNHIDECOLS: return G_COLS;
  case ID_INSROWS: return G_INSROWS;
  case ID_INSCOLS: return G_INSCOLS;
  case ID_DELROWS: return G_DELROWS;
  case ID_DELCOLS: return G_DELCOLS;
  case ID_SORTAZ: case ID_SORTZA: case ID_CUSTOMSORT: return G_SORT;
  case ID_FILTER: case ID_CLEARFILTER: case ID_REAPPLY: case ID_FILTERCOL: case ID_ADVFILTER: return G_FILTER;
  case ID_LINK: case ID_LINKDEL: return G_LINKS;
  case ID_NOTENEW: case ID_NOTEDEL: return G_OBJECTS;
  case ID_INSCELLS: case ID_DELCELLS: case ID_DV: case ID_DVCLEAR: case ID_GROUP: case ID_UNGROUP: case ID_GROUPDD: case ID_CLEAROUTLINE:
  case ID_SUBTOTAL: case ID_SHOWDETAIL: case ID_HIDEDETAIL: case ID_OUTLINESET: case ID_CONSOLIDATE: case ID_GOALSEEK: case ID_DATATABLE: case ID_REPLACE:
    return G_ALWAYS;
  case ID_DVDD: return v == 0 || v == 3 ? G_ALWAYS : G_NONE;
  case ID_INSSHEET: case ID_NEWSHEET: case ID_RENAMESHEET: case ID_DELSHEET: case ID_MOVESHEETL: case ID_MOVESHEETR: case ID_HIDESHEET:
  case ID_UNHIDESHEET: case ID_DUPSHEET: case ID_TABCOLORDD: case ID_IMPORTCSV:
    return G_STRUCT;
  case ID_INSERTDD: return v == 0 ? G_ALWAYS : v == 1 ? G_INSROWS : v == 2 ? G_INSCOLS : G_STRUCT;
  case ID_DELETEDD: return v == 0 ? G_ALWAYS : v == 1 ? G_DELROWS : v == 2 ? G_DELCOLS : G_STRUCT;
  case ID_FORMATDD: return v == 0 || v == 1 || v == 5 || v == 7 ? G_ROWS : v == 2 || v == 3 || v == 4 || v == 6 || v == 8 ? G_COLS : (v >= 9 && v <= 10) || (v >= 12 && v <= 14) ? G_STRUCT : v == 15 ? G_NONE : G_FORMAT;
  case ID_CLEARDD: return v == 1 ? G_FORMAT : G_CELLS;
  case ID_SORTDD: return v <= 2 ? G_SORT : G_FILTER;
  }
  return G_NONE;
}
int datatab_refused(ULONG id, LONG v)
{
  Sheet *s = cur_sheet();
  int g = guard_of(id, v);
  u16 al = s->protallow;
  if (g == G_STRUCT) {
    if (!book->lockstruct) return 0;
    say("The workbook's structure is protected: sheets can't be added, deleted, renamed, moved or hidden.\nUnprotect it first (Review, Protect Workbook).");
    return 1;
  }
  if (!s->protect || g == G_NONE) return 0;
  switch (g) {
  case G_CELLS: if (!prot_area_locked(book, cur_index(), cur_sel()) || (s->neranges && anatab_unlock(book, cur_index(), cur_sel()))) return 0; break;
  case G_FORMAT: if (al & PA_FMTCELLS) return 0; break;
  case G_ROWS: if (al & PA_FMTROWS) return 0; break;
  case G_COLS: if (al & PA_FMTCOLS) return 0; break;
  case G_INSROWS: if (al & PA_INSROWS) return 0; break;
  case G_INSCOLS: if (al & PA_INSCOLS) return 0; break;
  case G_DELROWS: if ((al & PA_DELROWS) && !prot_area_locked(book, cur_index(), cur_sel())) return 0; break;
  case G_DELCOLS: if ((al & PA_DELCOLS) && !prot_area_locked(book, cur_index(), cur_sel())) return 0; break;
  case G_SORT: if ((al & PA_SORT) && !prot_area_locked(book, cur_index(), data_region())) return 0; break;
  case G_FILTER: if (al & PA_FILTER) return 0; break;
  case G_LINKS: if (al & PA_INSLINKS) return 0; break;
  case G_OBJECTS: if (al & PA_OBJECTS) return 0; break;
  }
  protected_msg();
  return 1;
}

/* ------------------------------------------------------------------ data validation */
static const char *const dv_allow[] = { "Any value", "Whole number", "Decimal", "List", "Date", "Time", "Text length", "Custom", NULL };
static const char *const dv_ops[] = { "between", "not between", "equal to", "not equal to", "greater than", "less than", "greater than or equal to",
                                      "less than or equal to", NULL };
static const char *const dv_styles[] = { "Stop", "Warning", "Information", NULL };
static struct { Object *allow, *op, *lab1, *lab2, *v1, *v2, *blank, *drop; } dvd;
/* the boxes' captions and what can be used, as the kind of rule and the comparison change */
static void dv_fields(void)
{
  int t = (int)xget(dvd.allow, MUIA_Cycle_Active), op = (int)xget(dvd.op, MUIA_Cycle_Active);
  int two = t != DV_ANY && t != DV_LIST && t != DV_CUSTOM && (op == CO_BETWEEN || op == CO_NOTBETWEEN);
  set(dvd.op, MUIA_Disabled, t == DV_ANY || t == DV_LIST || t == DV_CUSTOM);
  set(dvd.v1, MUIA_Disabled, t == DV_ANY);
  set(dvd.v2, MUIA_Disabled, !two);
  set(dvd.drop, MUIA_Disabled, t != DV_LIST);
  set(dvd.lab1, MUIA_Text_Contents, (ULONG)tr(t == DV_LIST ? "Source:" : t == DV_CUSTOM ? "Formula:" : two ? "Minimum:" : "Value:"));
  set(dvd.lab2, MUIA_Text_Contents, (ULONG)tr(two ? "Maximum:" : ""));
}
static void dv_each(ULONG id) { if (id == ID_DLG1) dv_fields(); }
/* areas typed as "A1:B5 C7" (spaces or commas between) */
static int parse_areas(const char *l1, Rect *out, int max)
{
  char buf[300], *p, *tok;
  int n = 0, sh;
  strncpy(buf, l1, sizeof buf - 1); buf[sizeof buf - 1] = 0;
  for (p = buf; *p; p++) if (*p == ',' || *p == ';') *p = ' ';
  for (tok = strtok(buf, " "); tok && n < max; tok = strtok(NULL, " ")) {
    if (!parse_range(tok, &sh, &out[n]) || sh != cur_index()) return 0;
    n++;
  }
  return n;
}
static void dlg_validation(void)
{
  static const ULONG ends[] = { ID_DLGOK, ID_DLG2, 0 };
  static const char *pages_en[] = { "Settings", "Input Message", "Error Alert", NULL };
  static const char *pl[4], *allow[10], *ops[10], *styles[4];
  Sheet *s = cur_sheet();
  const DVRule *cur = dv_at(s, s->cur_r, s->cur_c);
  DVRule r;
  Object *w, *reg, *clear, *apply, *showin, *intitle, *inmsg, *showerr, *estyle, *etitle, *emsg;
  char v1[200] = "", v2[200] = "", areas[200], t1[200], t2[200], t3[300], t4[300];
  Rect sel = cur_sel(), ar[16];
  ULONG id;
  int n;
  if (sel.r2 >= MAXROW - 1 && sel.r2 > sheet_last_row(s) + 1000) sel.r2 = sheet_last_row(s) > sel.r1 ? sheet_last_row(s) : sel.r1;
  range_text(sel, 0, areas);
  if (cur && sel.r1 == sel.r2 && sel.c1 == sel.c2 && cur->nareas) {
    /* one cell with a rule: the rule's cells */
    int k;
    areas[0] = 0;
    for (k = 0; k < cur->nareas && strlen(areas) < 160; k++) { char a[40]; range_text(cur->areas[k], 0, a); if (k) strcat(areas, " "); strcat(areas, a); }
  }
  if (cur) {
    char u[300];
    Formula *f;
    /* the values seen from the active cell, as typed */
    if (cur->f1 && (f = formula_copy(cur->f1))) {
      if (cur->nareas) formula_offset(f, s->cur_r - cur->areas[0].r1, s->cur_c - cur->areas[0].c1);
      dv_value_text(book, cur_index(), f, cur->type == DV_LIST, u, sizeof u); to_l1(u, v1, sizeof v1);
      formula_free(f);
    }
    if (cur->f2 && (f = formula_copy(cur->f2))) {
      if (cur->nareas) formula_offset(f, s->cur_r - cur->areas[0].r1, s->cur_c - cur->areas[0].c1);
      dv_value_text(book, cur_index(), f, 0, u, sizeof u); to_l1(u, v2, sizeof v2);
      formula_free(f);
    }
  }
  to_l1(cur ? cur->intitle : "", t1, sizeof t1); to_l1(cur ? cur->inmsg : "", t3, sizeof t3);
  to_l1(cur ? cur->errtitle : "", t2, sizeof t2); to_l1(cur ? cur->errmsg : "", t4, sizeof t4);
  trl(pages_en, pl); trl(dv_allow, allow); trl(dv_ops, ops); trl(dv_styles, styles);
  reg = RegisterGroup(pl), MUIA_Register_Frame, TRUE, MUIA_CycleChain, 1,
    Child, VGroup,
      Child, ColGroup(2),
        Child, Label1(tr("Allow:")), Child, dvd.allow = cycle_obj(allow, cur ? cur->type : DV_ANY),
        Child, Label1(tr("Data:")), Child, dvd.op = cycle_obj(ops, cur ? cur->op : CO_BETWEEN),
        Child, dvd.lab1 = TextObject, MUIA_Text_PreParse, (ULONG)"\33r", MUIA_FixWidthTxt, (ULONG)"Maximum:MM", End, Child, dvd.v1 = str_obj(v1, 190),
        Child, dvd.lab2 = TextObject, MUIA_Text_PreParse, (ULONG)"\33r", End, Child, dvd.v2 = str_obj(v2, 190),
        Child, Label1(tr("Apply to:")), Child, apply = str_obj(areas, 190),
      End,
      Child, checkrow(&dvd.blank, cur ? cur->blank : 1, "Ignore blank"),
      Child, checkrow(&dvd.drop, cur ? cur->dropdown : 1, "In-cell drop-down"),
      Child, TextObject, MUIA_Text_Contents, (ULONG)tr("\33cA list's source: items with commas between (Yes,No,Maybe),\nor =$A$1:$A$9 for a range's values. Custom: a formula that is TRUE for good values."), End,
      Child, VSpace(0),
    End,
    Child, VGroup,
      Child, checkrow(&showin, cur ? cur->showinput : 1, "Show input message when cell is selected"),
      Child, ColGroup(2),
        Child, Label1(tr("Title:")), Child, intitle = str_obj(t1, 32),
        Child, Label1(tr("Input message:")), Child, inmsg = str_obj(t3, 255),
      End,
      Child, VSpace(0),
    End,
    Child, VGroup,
      Child, checkrow(&showerr, cur ? cur->showerror : 1, "Show error alert after invalid data is entered"),
      Child, ColGroup(2),
        Child, Label1(tr("Style:")), Child, estyle = cycle_obj(styles, cur ? cur->errstyle : DVE_STOP),
        Child, Label1(tr("Title:")), Child, etitle = str_obj(t2, 32),
        Child, Label1(tr("Error message:")), Child, emsg = str_obj(t4, 255),
      End,
      Child, VSpace(0),
    End,
  End;
  w = dialog_window("Data Validation", VGroup, Child, reg, Child, HGroup, Child, clear = QButton(tr("Clear All")), Child, HSpace(0), End, End, NULL);
  if (!w) return;
  notify_id(dvd.allow, MUIA_Cycle_Active, ID_DLG1);
  notify_id(dvd.op, MUIA_Cycle_Active, ID_DLG1);
  press_id(clear, ID_DLG2);
  dv_fields();
  for (;;) {
    id = run_modal(w, ends, dv_each);
    if (id != ID_DLGOK && id != ID_DLG2) break;
    if (!(n = parse_areas(sget(apply), ar, 16))) { say("The cells to apply the rule to aren't a range of this sheet (for example A1:A20)."); continue; }
    if (id == ID_DLG2) { book_dv_set(book, cur_index(), ar, n, NULL); break; }
    dv_rule_init(&r);
    r.type = (u8)xget(dvd.allow, MUIA_Cycle_Active); r.op = (u8)xget(dvd.op, MUIA_Cycle_Active);
    r.blank = (u8)checked(dvd.blank); r.dropdown = (u8)checked(dvd.drop);
    r.showinput = (u8)checked(showin); r.showerror = (u8)checked(showerr); r.errstyle = (u8)xget(estyle, MUIA_Cycle_Active);
    {
      char u[600];
      free(r.intitle); latin1_utf8(sget(intitle), u, sizeof u); r.intitle = str_dup(u);
      free(r.inmsg); latin1_utf8(sget(inmsg), u, sizeof u); r.inmsg = str_dup(u);
      free(r.errtitle); latin1_utf8(sget(etitle), u, sizeof u); r.errtitle = str_dup(u);
      free(r.errmsg); latin1_utf8(sget(emsg), u, sizeof u); r.errmsg = str_dup(u);
      if (r.type != DV_ANY) {
        int two = r.type != DV_LIST && r.type != DV_CUSTOM && (r.op == CO_BETWEEN || r.op == CO_NOTBETWEEN);
        latin1_utf8(sget(dvd.v1), u, sizeof u);
        r.f1 = dv_value(book, cur_index(), u, r.type == DV_LIST);
        if (two) { latin1_utf8(sget(dvd.v2), u, sizeof u); r.f2 = dv_value(book, cur_index(), u, 0); }
        if (!r.f1 || (two && !r.f2)) { dv_rule_free(&r); say(two ? "Enter both the minimum and the maximum: numbers, dates, or formulas starting with =." : "Enter the value: a number, a date, text, or a formula starting with =."); continue; }
        if (r.type != DV_LIST || (r.f1->n && r.f1->t[0].op != T_STR)) {
          /* typed as seen from the active cell; kept as seen from the rule's first cell */
          if (r.f1) formula_offset(r.f1, ar[0].r1 - s->cur_r, ar[0].c1 - s->cur_c);
          if (r.f2) formula_offset(r.f2, ar[0].r1 - s->cur_r, ar[0].c1 - s->cur_c);
        }
      }
    }
    book_dv_set(book, cur_index(), ar, n, &r);
    dv_rule_free(&r);
    break;
  }
  MUI_DisposeObject(w);
  refresh_all();
}
/* the drop-down list's window under the active cell: a click picks */
static void pick_list(void)
{
  static const ULONG ends[] = { ID_DLGOK, 0 };
  Sheet *s = cur_sheet();
  const DVRule *r = dv_at(s, s->cur_r, s->cur_c);
  char **items, (*l1)[80], cur[200];
  int n, i, active = -1;
  LONG *box = (LONG *)xget(grid, MUIA_Grid_CellBox), x, y, wd, ht;
  struct Window *gw = _window(grid);
  Object *w, *lv, *list;
  u32 colr;
  if (!r || r->type != DV_LIST || !box || box[0] < 0 || !gw) return;
  n = dv_list_items(book, cur_index(), r, s->cur_r, s->cur_c, &items);
  if (n <= 0) { free(items); return; }
  if (!(l1 = calloc((size_t)n, sizeof *l1))) { for (i = 0; i < n; i++) free(items[i]); free(items); return; }
  grid_cell_text(book, cur_index(), s->cur_r, s->cur_c, 0, cur, sizeof cur, &colr);
  for (i = 0; i < n; i++) { to_l1(items[i], l1[i], sizeof l1[i]); if (active < 0 && !Stricmp((STRPTR)l1[i], (STRPTR)cur)) active = i; }
  wd = box[2] - box[0] + 1 + 18;
  if (wd < 110) wd = 110;
  ht = (n < 8 ? n : 8) * (theme_font()->tf_YSize + 2) + 6;
  x = gw->LeftEdge + box[0]; y = gw->TopEdge + box[3] + 1;
  if (y + ht > gw->WScreen->Height) y = gw->TopEdge + box[1] - ht - 1;
  if (x + wd > gw->WScreen->Width) x = gw->WScreen->Width - wd;
  w = QWindowObject, MUIA_Window_Borderless, TRUE, MUIA_Window_CloseGadget, FALSE, MUIA_Window_DepthGadget, FALSE, MUIA_Window_SizeGadget, FALSE,
    MUIA_Window_DragBar, FALSE, MUIA_Window_LeftEdge, x, MUIA_Window_TopEdge, y, MUIA_Window_Width, wd, MUIA_Window_Height, ht, MUIA_Window_Activate, TRUE,
    WindowContents, VGroup, InnerSpacing(0, 0), QDLG,
      Child, lv = ListviewObject, MUIA_Listview_Input, TRUE, MUIA_CycleChain, 1,
        MUIA_Listview_List, list = ListObject, InputListFrame, MUIA_List_ConstructHook, MUIV_List_ConstructHook_String, MUIA_List_DestructHook, MUIV_List_DestructHook_String, End,
      End,
    End,
  End;
  if (w) {
    for (i = 0; i < n; i++) DoMethod(list, MUIM_List_InsertSingle, (ULONG)l1[i], MUIV_List_Insert_Bottom);
    if (active >= 0) set(list, MUIA_List_Active, active);
    DoMethod(lv, MUIM_Notify, MUIA_Listview_DoubleClick, TRUE, (ULONG)app, 2, MUIM_Application_ReturnID, ID_DLGOK);
    DoMethod(list, MUIM_Notify, MUIA_List_Active, MUIV_EveryTime, (ULONG)app, 2, MUIM_Application_ReturnID, ID_DLGOK);
    DoMethod(w, MUIM_Notify, MUIA_Window_Activate, FALSE, (ULONG)app, 2, MUIM_Application_ReturnID, ID_DLGCANCEL);
    set(w, MUIA_Window_ActiveObject, (ULONG)lv);
    if (run_modal(w, ends, NULL) == ID_DLGOK) {
      LONG a = (LONG)xget(list, MUIA_List_Active);
      if (a >= 0 && a < n) {
        char t[300];
        snprintf(t, sizeof t, "%s%s", items[a][0] == '=' ? "'" : "", items[a]);
        book_input(book, cur_index(), s->cur_r, s->cur_c, t, NULL);
      }
    }
    MUI_DisposeObject(w);
  }
  for (i = 0; i < n; i++) free(items[i]);
  free(items); free(l1);
  refresh_all();
}
int datatab_entry_ok(Book *b, int sheet, i32 r, i32 c, const char *u)
{
  const DVRule *rule;
  char title[100], msg[600], m2[700];
  int k;
  if (b != book || dv_check_text(b, sheet, r, c, u, &rule) || !rule || !rule->showerror) return 1;
  to_l1(rule->errtitle && rule->errtitle[0] ? rule->errtitle : "Ledger", title, sizeof title);
  to_l1(rule->errmsg && rule->errmsg[0] ? rule->errmsg : tr(DV_DEFAULT_ERROR), msg, sizeof msg);
  switch (rule->errstyle) {
  case DVE_WARNING:
    snprintf(m2, sizeof m2, "%s\n\n%s", msg, tr("Continue?"));
    k = (int)MUI_Request(app, win, 0, (STRPTR)title, (STRPTR)tr("Yes|No|Cancel"), (STRPTR)"%s", m2);
    return k == 1 ? 1 : k == 2 ? 0 : -1;
  case DVE_INFO:
    k = (int)MUI_Request(app, win, 0, (STRPTR)title, (STRPTR)tr("OK|Cancel"), (STRPTR)"%s", msg);
    return k == 1 ? 1 : -1;
  }
  k = (int)MUI_Request(app, win, 0, (STRPTR)title, (STRPTR)tr("Retry|Cancel"), (STRPTR)"%s", msg);
  return k == 1 ? 0 : -1;
}

/* ------------------------------------------------------------------ Text to Columns */
static struct { Object *kind, *tab, *semi, *comma, *space, *other, *otherch, *cons, *qual, *widths, *dest, *list; Rect src; } tc;
static void ttc_opts(TTCOpts *o)
{
  static const char quals[] = { '"', '\'', 0 };
  const char *w, *oc = sget(tc.otherch);
  memset(o, 0, sizeof *o);
  o->fixed = xget(tc.kind, MUIA_Cycle_Active) == 1;
  o->delims = (checked(tc.tab) ? TTC_TAB : 0) | (checked(tc.semi) ? TTC_SEMI : 0) | (checked(tc.comma) ? TTC_COMMA : 0) | (checked(tc.space) ? TTC_SPACE : 0) |
              (checked(tc.other) && oc[0] ? TTC_OTHER : 0);
  o->other = oc[0];
  o->consecutive = checked(tc.cons);
  o->quote = quals[xget(tc.qual, MUIA_Cycle_Active) % 3];
  for (w = sget(tc.widths); *w && o->nbreaks < 32; ) {
    while (*w && (*w < '0' || *w > '9')) w++;
    if (!*w) break;
    o->breaks[o->nbreaks] = atoi(w);
    if (o->breaks[o->nbreaks] > 0 && (!o->nbreaks || o->breaks[o->nbreaks] > o->breaks[o->nbreaks - 1])) o->nbreaks++;
    while (*w >= '0' && *w <= '9') w++;
  }
}
/* the first rows split, columns shown between bars */
static void ttc_preview(void)
{
  TTCOpts o;
  i32 r;
  ttc_opts(&o);
  set(tc.widths, MUIA_Disabled, !o.fixed);
  DoMethod(tc.list, MUIM_List_Clear);
  for (r = tc.src.r1; r <= tc.src.r2 && r < tc.src.r1 + 8; r++) {
    char t[600], u[600], **parts, line[400];
    u32 colr;
    int n, k;
    const Value *v = cell_value(book, cur_index(), r, tc.src.c1);
    if (v->t == V_STR) { strncpy(u, v->s, sizeof u - 1); u[sizeof u - 1] = 0; }
    else { book_shown_text(book, cur_index(), r, tc.src.c1, u, sizeof u, &colr); }
    n = ttc_split(u, &o, &parts);
    line[0] = 0;
    for (k = 0; k < n; k++) {
      to_l1(parts[k], t, sizeof t);
      if (strlen(line) + strlen(t) + 4 < sizeof line) { if (k) strcat(line, " | "); strcat(line, t); }
      free(parts[k]);
    }
    free(parts);
    DoMethod(tc.list, MUIM_List_InsertSingle, (ULONG)line, MUIV_List_Insert_Bottom);
  }
}
static void ttc_each(ULONG id) { if (id == ID_DLG1) ttc_preview(); }
static void dlg_text_to_columns(void)
{
  static const ULONG ends[] = { ID_DLGOK, 0 };
  static const char *kinds_en[] = { "Delimited: characters such as commas or tabs separate each field", "Fixed width: fields are lined up in columns", NULL };
  static const char *quals_en[] = { "\"", "'", "{none}", NULL };
  static const char *kinds[4], *quals[4];
  Sheet *s = cur_sheet();
  Rect a = cur_sel();
  Object *w;
  char dest[40];
  if (a.r2 > sheet_last_row(s)) a.r2 = sheet_last_row(s);
  if (a.r2 < a.r1) { say("Select a column of cells with text to split first."); return; }
  if (a.c2 != a.c1) { say("Text to Columns works on one column at a time. Select cells in a single column."); return; }
  tc.src = a;
  ref_text(a.r1, a.c1, 0, 0, dest);
  trl(kinds_en, kinds); trl(quals_en, quals);
  w = dialog_window("Convert Text to Columns", VGroup,
        Child, tc.kind = cycle_obj(kinds, 0),
        Child, HGroup,
          Child, VGroup, GroupFrameT(tr("Delimiters")),
            Child, checkrow(&tc.tab, 1, "Tab"), Child, checkrow(&tc.semi, 0, "Semicolon"), Child, checkrow(&tc.comma, 1, "Comma"), Child, checkrow(&tc.space, 0, "Space"),
            Child, HGroup, Child, tc.other = check_obj(0), Child, LLabel1(tr("Other:")), Child, tc.otherch = str_obj("", 2), End,
          End,
          Child, VGroup,
            Child, checkrow(&tc.cons, 0, "Treat consecutive delimiters as one"),
            Child, ColGroup(2),
              Child, Label1(tr("Text qualifier:")), Child, tc.qual = cycle_obj(quals, 0),
              Child, Label1(tr("Column breaks:")), Child, tc.widths = str_obj("", 60),
              Child, Label1(tr("Destination:")), Child, tc.dest = str_obj(dest, 30),
            End,
            Child, TextObject, MUIA_Text_Contents, (ULONG)tr("Fixed width: the character positions\nwhere columns start, as 5, 12, 20"), End,
            Child, VSpace(0),
          End,
        End,
        Child, LLabel1(tr("Data preview:")),
        Child, ListviewObject, MUIA_Listview_Input, FALSE, MUIA_FixHeightTxt, (ULONG)"\n\n\n\n\n", MUIA_Listview_List, tc.list = ListObject, ReadListFrame,
          MUIA_List_ConstructHook, MUIV_List_ConstructHook_String, MUIA_List_DestructHook, MUIV_List_DestructHook_String, End, End,
      End, "Finish");
  if (!w) return;
  notify_id(tc.kind, MUIA_Cycle_Active, ID_DLG1); notify_id(tc.qual, MUIA_Cycle_Active, ID_DLG1);
  notify_id(tc.tab, MUIA_Selected, ID_DLG1); notify_id(tc.semi, MUIA_Selected, ID_DLG1); notify_id(tc.comma, MUIA_Selected, ID_DLG1);
  notify_id(tc.space, MUIA_Selected, ID_DLG1); notify_id(tc.other, MUIA_Selected, ID_DLG1); notify_id(tc.cons, MUIA_Selected, ID_DLG1);
  notify_id(tc.otherch, MUIA_String_Contents, ID_DLG1); notify_id(tc.widths, MUIA_String_Contents, ID_DLG1);
  ttc_preview();
  if (run_modal(w, ends, ttc_each) == ID_DLGOK) {
    TTCOpts o;
    int sh;
    Rect d;
    ttc_opts(&o);
    if (!parse_range(sget(tc.dest), &sh, &d) || sh != cur_index()) say("The destination isn't a cell of this sheet.");
    else if (datatab_may(book, cur_index(), DT_CELLS, d)) {
      book_text_to_columns(book, cur_index(), tc.src, &o, d.r1, d.c1);
      refresh_all();
    }
  }
  MUI_DisposeObject(w);
}

/* ------------------------------------------------------------------ Remove Duplicates, by column */
static Object *rd_tick;
static void rd_each(ULONG id) { if (id == ID_DLG3 || id == ID_DLG4) set(rd_tick, MUIA_Tick_All, id == ID_DLG3); }
static void dlg_remove_duplicates(void)
{
  static const ULONG ends[] = { ID_DLGOK, 0 };
  Rect a = data_region();
  int n, i, hdr, got;
  char (*lab)[64];
  const char **labp;
  UBYTE *on;
  Object *w, *all, *none, *sc, *hdrchk;
  static char msg[200];
  if (a.r2 <= a.r1 && !sheet_cell(cur_sheet(), a.r1, a.c1)) { say("Select a cell in a table of data first."); return; }
  n = a.c2 - a.c1 + 1;
  if (n > 200) n = 200;
  hdr = looks_like_header(a);
  lab = calloc((size_t)n + 1, sizeof *lab); labp = (const char **)calloc((size_t)n + 1, sizeof *labp); on = (UBYTE *)calloc((size_t)n + 1, 1);
  if (!lab || !labp || !on) { free(lab); free(labp); free(on); return; }
  for (i = 0; i < n; i++) { col_label(a, a.c1 + i, hdr, lab[i], sizeof lab[i]); labp[i] = lab[i]; on[i] = 1; }
  sc = NewObject(flatscroll_class()->mcc_Class, NULL, TAG_DONE);
  w = dialog_window("Remove Duplicates", VGroup,
        Child, TextObject, MUIA_Text_Contents, (ULONG)tr("Rows whose ticked columns repeat an earlier row are deleted."), End,
        Child, HGroup, Child, all = QButton(tr("Select All")), Child, none = QButton(tr("Unselect All")), Child, HSpace(0), Child, hdrchk = check_obj(hdr), Child, LLabel1(tr("My data has headers")), End,
        Child, HGroup, MUIA_Group_Spacing, 0,
          Child, rd_tick = NewObject(ticklist_class()->mcc_Class, NULL, MUIA_Tick_Labels, (ULONG)labp, MUIA_Tick_States, (ULONG)on, MUIA_Tick_Count, n, MUIA_Tick_Prop, (ULONG)sc, TAG_DONE),
          Child, sc,
        End,
      End, "OK");
  if (w) {
    DoMethod(sc, MUIM_Notify, MUIA_Prop_First, MUIV_EveryTime, (ULONG)rd_tick, 3, MUIM_Set, MUIA_Tick_First, MUIV_TriggerValue);
    press_id(all, ID_DLG3); press_id(none, ID_DLG4);
    if (run_modal(w, ends, rd_each) == ID_DLGOK) {
      u32 mask = 0;
      int any = 0;
      for (i = 0; i < n; i++) if (on[i]) { any = 1; if (i < 32) mask |= 1UL << i; }
      if (!any) say("Select at least one column.");
      else {
        int all_on = 1;
        for (i = 0; i < n; i++) if (!on[i]) all_on = 0;
        got = book_remove_duplicates(book, cur_index(), a, all_on || n > 32 ? 0 : mask, checked(hdrchk));
        refresh_all();
        snprintf(msg, sizeof msg, got ? tr("%d duplicate rows were found and removed.") : tr("No duplicate rows were found."), got);
        say(msg);
      }
    }
    MUI_DisposeObject(w);
  }
  free(lab); free(labp); free(on);
  rd_tick = NULL;
}

/* ------------------------------------------------------------------ the outline */
/* rows or columns: whole columns selected mean columns; a block asks (1 rows, 0 columns, -1 cancel) */
static int rows_or_cols(Rect a, const char *what)
{
  if (a.r1 == 0 && a.r2 >= MAXROW - 1 && !(a.c1 == 0 && a.c2 >= MAXCOL - 1)) return 0;
  if (a.c1 == 0 && a.c2 >= MAXCOL - 1) return 1;
  switch (ask("Rows|Columns|Cancel", "%s", what)) { case 1: return 1; case 2: return 0; }
  return -1;
}
static void group(int on)
{
  Rect a = cur_sel();
  int rows = rows_or_cols(a, on ? "Group the selected rows or columns?" : "Ungroup the selected rows or columns?");
  if (rows < 0) return;
  if (rows) { if (a.r2 - a.r1 > 100000) a.r2 = sheet_last_row(cur_sheet()) > a.r1 ? sheet_last_row(cur_sheet()) : a.r1; book_group(book, cur_index(), 1, a.r1, a.r2, on); }
  else book_group(book, cur_index(), 0, a.c1, a.c2, on);
  refresh_all();
}
static void detail(int hide)
{
  Rect a = cur_sel();
  Sheet *s = cur_sheet();
  int rows = !(a.r1 == 0 && a.r2 >= MAXROW - 1);
  if (rows && !outline_max_level(s, 1) && outline_max_level(s, 0)) rows = 0;
  if (!(rows ? book_detail(book, cur_index(), 1, a.r1, a.r2, hide, 0) : book_detail(book, cur_index(), 0, a.c1, a.c2, hide, 0)))
    say("There's no group here. Select a cell in a group (or its summary row) first.");
  refresh_all();
}
static void dlg_outline_settings(void)
{
  static const ULONG ends[] = { ID_DLGOK, 0 };
  Sheet *s = cur_sheet();
  Object *w, *below, *right;
  w = dialog_window("Outline Settings", VGroup,
        Child, LLabel1(tr("Direction:")),
        Child, checkrow(&below, !s->sumabove, "Summary rows below detail"),
        Child, checkrow(&right, !s->sumleft, "Summary columns to right of detail"),
      End, NULL);
  if (!w) return;
  if (run_modal(w, ends, NULL) == ID_DLGOK) book_outline_settings(book, cur_index(), checked(below), checked(right));
  MUI_DisposeObject(w);
  refresh_all();
}
static const char *const st_funcs[] = { "Sum", "Count", "Average", "Max", "Min", "Product", "Count Numbers", "StdDev", "StdDevp", "Var", "Varp", NULL };
static const int st_codes[] = { 9, 3, 1, 4, 5, 6, 2, 7, 8, 10, 11 };
static Object *st_tick;
static void dlg_subtotal(void)
{
  static const ULONG ends[] = { ID_DLGOK, ID_DLG2, 0 };
  static const char *fns[14], *cols[202];
  Rect a = data_region();
  int n, i;
  char (*lab)[64];
  const char **labp;
  UBYTE *on;
  Object *w, *at, *fn, *sc, *repl, *pgb, *below, *rmall;
  ULONG id;
  if (a.r2 <= a.r1) { say("Select a cell in a list with a heading row first (sorted by the column to total at each change in)."); return; }
  n = a.c2 - a.c1 + 1;
  if (n > 200) n = 200;
  lab = calloc((size_t)n + 1, sizeof *lab); labp = (const char **)calloc((size_t)n + 1, sizeof *labp); on = (UBYTE *)calloc((size_t)n + 1, 1);
  if (!lab || !labp || !on) { free(lab); free(labp); free(on); return; }
  for (i = 0; i < n; i++) { col_label(a, a.c1 + i, 1, lab[i], sizeof lab[i]); labp[i] = lab[i]; cols[i] = lab[i]; }
  cols[n] = NULL;
  /* the last column with numbers under its heading is totalled to start with */
  for (i = n - 1; i > 0; i--) if (cell_value(book, cur_index(), a.r1 + 1, a.c1 + i)->t == V_NUM) { on[i] = 1; break; }
  trl(st_funcs, fns);
  sc = NewObject(flatscroll_class()->mcc_Class, NULL, TAG_DONE);
  w = dialog_window("Subtotal", VGroup,
        Child, ColGroup(2),
          Child, Label1(tr("At each change in:")), Child, at = cycle_obj(cols, 0),
          Child, Label1(tr("Use function:")), Child, fn = cycle_obj(fns, 0),
        End,
        Child, LLabel1(tr("Add subtotal to:")),
        Child, HGroup, MUIA_Group_Spacing, 0,
          Child, st_tick = NewObject(ticklist_class()->mcc_Class, NULL, MUIA_Tick_Labels, (ULONG)labp, MUIA_Tick_States, (ULONG)on, MUIA_Tick_Count, n, MUIA_Tick_Prop, (ULONG)sc, TAG_DONE),
          Child, sc,
        End,
        Child, checkrow(&repl, 1, "Replace current subtotals"),
        Child, checkrow(&pgb, 0, "Page break between groups (Ledger doesn't print)"),
        Child, checkrow(&below, 1, "Summary below data"),
        Child, HGroup, Child, rmall = QButton(tr("Remove All")), Child, HSpace(0), End,
      End, "OK");
  if (w) {
    set(pgb, MUIA_Disabled, TRUE);
    DoMethod(sc, MUIM_Notify, MUIA_Prop_First, MUIV_EveryTime, (ULONG)st_tick, 3, MUIM_Set, MUIA_Tick_First, MUIV_TriggerValue);
    press_id(rmall, ID_DLG2);
    id = run_modal(w, ends, NULL);
    if (id == ID_DLG2) { book_subtotal_remove(book, cur_index(), a); refresh_all(); }
    else if (id == ID_DLGOK) {
      i32 cl[200];
      int nc = 0;
      for (i = 0; i < n; i++) if (on[i]) cl[nc++] = a.c1 + i;
      if (!nc) say("Tick at least one column to add a subtotal to.");
      else {
        book_subtotal(book, cur_index(), a, a.c1 + (i32)xget(at, MUIA_Cycle_Active), st_codes[xget(fn, MUIA_Cycle_Active) % 11], cl, nc, checked(repl), checked(below));
        refresh_all();
      }
    }
    MUI_DisposeObject(w);
  }
  free(lab); free(labp); free(on);
  st_tick = NULL;
}

/* ------------------------------------------------------------------ the Advanced Filter */
static int adv_sheet = -1; static Book *adv_book; static Rect adv_list, adv_crit; static int adv_hascrit, adv_unique;
static void dlg_advanced_filter(void)
{
  static const ULONG ends[] = { ID_DLGOK, 0 };
  static const char *acts_en[] = { "Filter the list, in place", "Copy to another location", NULL };
  static const char *acts[3];
  Rect a = data_region();
  Object *w, *act, *lst, *crit, *copy, *uniq;
  char ls[40];
  range_text(a, 1, ls);
  trl(acts_en, acts);
  w = dialog_window("Advanced Filter", VGroup,
        Child, act = cycle_obj(acts, 0),
        Child, ColGroup(2),
          Child, Label1(tr("List range:")), Child, lst = str_obj(ls, 60),
          Child, Label1(tr("Criteria range:")), Child, crit = str_obj("", 60),
          Child, Label1(tr("Copy to:")), Child, copy = str_obj("", 60),
        End,
        Child, checkrow(&uniq, 0, "Unique records only"),
        Child, TextObject, MUIA_Text_Contents, (ULONG)tr("\33cCriteria: a heading row with the list's column names, conditions under it\n(>100, <>x, =exact, text that starts so). Each row is one way to match."), End,
      End, NULL);
  if (!w) return;
  for (;;) {
    int sh, csh, n;
    Rect l, c, d;
    if (run_modal(w, ends, NULL) != ID_DLGOK) break;
    if (!parse_range(sget(lst), &sh, &l) || sh != cur_index() || l.r2 <= l.r1) { say("The list range isn't a range of this sheet with a heading row."); continue; }
    if (sget(crit)[0] && (!parse_range(sget(crit), &csh, &c) || csh != cur_index())) { say("The criteria range isn't a range of this sheet."); continue; }
    if (xget(act, MUIA_Cycle_Active) == 1) {
      if (!parse_range(sget(copy), &csh, &d)) { say("Copy to: give the cell to copy the matching rows to."); continue; }
      n = book_advanced_filter(book, cur_index(), l, sget(crit)[0] ? &c : NULL, csh, d.r1, d.c1, checked(uniq));
    } else {
      n = book_advanced_filter(book, cur_index(), l, sget(crit)[0] ? &c : NULL, -1, 0, 0, checked(uniq));
      adv_book = book; adv_sheet = cur_index(); adv_list = l; adv_crit = c; adv_hascrit = sget(crit)[0] != 0; adv_unique = checked(uniq);
    }
    (void)n;
    break;
  }
  MUI_DisposeObject(w);
  refresh_all();
}

/* ------------------------------------------------------------------ Consolidate */
static struct { Object *list, *ref; } cs;
static void cs_each(ULONG id)
{
  if (id == ID_DLG1) {
    int sh;
    Rect r;
    if (!sget(cs.ref)[0]) return;
    if (!parse_range(sget(cs.ref), &sh, &r)) { say("That isn't a range: give one as Sheet2!A1:C10."); return; }
    DoMethod(cs.list, MUIM_List_InsertSingle, (ULONG)sget(cs.ref), MUIV_List_Insert_Bottom);
    set(cs.ref, MUIA_String_Contents, (ULONG)"");
  } else if (id == ID_DLG2) DoMethod(cs.list, MUIM_List_Remove, MUIV_List_Remove_Active);
}
static void dlg_consolidate(void)
{
  static const ULONG ends[] = { ID_DLGOK, 0 };
  static const char *fns_en[] = { "Sum", "Count", "Average", "Max", "Min", "Product", "Count Numbers", NULL };
  static const int codes[] = { 9, 3, 1, 4, 5, 6, 2 };
  static const char *fns[8];
  Object *w, *fn, *add, *del, *top, *left;
  char sel[80], ref[60];
  Rect a = cur_sel();
  range_text(a, 1, ref);
  snprintf(sel, sizeof sel, "'%s'!%s", cur_sheet()->name, ref);
  trl(fns_en, fns);
  w = dialog_window("Consolidate", VGroup,
        Child, ColGroup(2), Child, Label1(tr("Function:")), Child, fn = cycle_obj(fns, 0), End,
        Child, LLabel1(tr("Reference:")),
        Child, HGroup, Child, cs.ref = str_obj(sel, 80), Child, add = QButton(tr("Add")), End,
        Child, LLabel1(tr("All references:")),
        Child, HGroup,
          Child, ListviewObject, MUIA_Listview_Input, TRUE, MUIA_FixHeightTxt, (ULONG)"\n\n\n\n", MUIA_Listview_List, cs.list = ListObject, InputListFrame,
            MUIA_List_ConstructHook, MUIV_List_ConstructHook_String, MUIA_List_DestructHook, MUIV_List_DestructHook_String, End, End,
          Child, VGroup, Child, del = QButton(tr("Delete")), Child, VSpace(0), End,
        End,
        Child, LLabel1(tr("Use labels in:")),
        Child, HGroup, Child, top = check_obj(0), Child, LLabel1(tr("Top row")), Child, left = check_obj(0), Child, LLabel1(tr("Left column")), Child, HSpace(0), End,
        Child, TextObject, MUIA_Text_Contents, (ULONG)tr("The result goes at the active cell."), End,
      End, NULL);
  if (!w) return;
  press_id(add, ID_DLG1); press_id(del, ID_DLG2);
  DoMethod(cs.ref, MUIM_Notify, MUIA_String_Acknowledge, MUIV_EveryTime, (ULONG)app, 2, MUIM_Application_ReturnID, ID_DLG1);
  if (run_modal(w, ends, cs_each) == ID_DLGOK) {
    int n = (int)xget(cs.list, MUIA_List_Entries), i, k = 0, sh[64];
    Rect src[64];
    if (!n && sget(cs.ref)[0]) cs_each(ID_DLG1), n = (int)xget(cs.list, MUIA_List_Entries);
    for (i = 0; i < n && k < 64; i++) {
      char *e = NULL;
      DoMethod(cs.list, MUIM_List_GetEntry, i, (ULONG)&e);
      if (e && parse_range(e, &sh[k], &src[k])) k++;
    }
    if (!k) say("Add at least one reference to consolidate.");
    else if (datatab_may(book, cur_index(), DT_CELLS, a)) {
      book_consolidate(book, cur_index(), cur_sheet()->cur_r, cur_sheet()->cur_c, sh, src, k, codes[xget(fn, MUIA_Cycle_Active) % 7], checked(top), checked(left));
      refresh_all();
    }
  }
  MUI_DisposeObject(w);
}

/* ------------------------------------------------------------------ What-If Analysis */
static void dlg_goal_seek(void)
{
  static const ULONG ends[] = { ID_DLGOK, 0 };
  Object *w, *set_, *to, *chg;
  char cell[40];
  ref_text(cur_sheet()->cur_r, cur_sheet()->cur_c, 0, 0, cell);
  w = dialog_window("Goal Seek", ColGroup(2),
        Child, Label1(tr("Set cell:")), Child, set_ = str_obj(cell, 40),
        Child, Label1(tr("To value:")), Child, to = str_obj("", 40),
        Child, Label1(tr("By changing cell:")), Child, chg = str_obj("", 40),
      End, NULL);
  if (!w) return;
  set(w, MUIA_Window_ActiveObject, (ULONG)to);
  for (;;) {
    int fs, cs2, r, kind;
    Rect fr, cr;
    double target, x;
    const char *why;
    char u[100], msg[300], g1[40], g2[40];
    if (run_modal(w, ends, NULL) != ID_DLGOK) break;
    latin1_utf8(sget(to), u, sizeof u);
    if (!parse_range(sget(set_), &fs, &fr) || !parse_range(sget(chg), &cs2, &cr)) { say("Set cell and By changing cell must be cells, as B5."); continue; }
    if (!parse_number(u, &target, &kind)) { say("To value must be a number."); continue; }
    if (!datatab_may(book, cs2, DT_CELLS, cr)) continue;
    set(win, MUIA_Window_Sleep, TRUE);
    r = book_goal_seek(book, fs, fr.r1, fr.c1, target, cs2, cr.r1, cr.c1, &x, &why);
    set(win, MUIA_Window_Sleep, FALSE);
    if (r < 0) { say(why); continue; }
    refresh_all();
    num_general(target, g1, 11);
    { const Value *v = cell_value(book, fs, fr.r1, fr.c1); if (v->t == V_NUM) num_general(v->n, g2, 11); else strcpy(g2, "-"); }
    snprintf(msg, sizeof msg, "%s %s.\n\n%s %s\n%s %s", tr("Goal Seeking with cell"), sget(set_), tr("Target value:"), g1, tr("Current value:"), g2);
    snprintf(msg + strlen(msg), sizeof msg - strlen(msg), "\n\n%s", tr(r ? "A solution was found." : "It may not have found a solution."));
    if (ask("OK|Cancel", "%s", msg) != 1) { int sh; Rect ar; book_undo(book, &sh, &ar); refresh_all(); }
    break;
  }
  MUI_DisposeObject(w);
}
static void dlg_data_table(void)
{
  static const ULONG ends[] = { ID_DLGOK, 0 };
  Object *w, *rin, *cin;
  Rect a = cur_sel();
  if (a.r2 <= a.r1 || a.c2 <= a.c1) { say("Select the table first: input values down its left column and/or across its top row, formulas at the other edge."); return; }
  w = dialog_window("Data Table", VGroup,
        Child, ColGroup(2),
          Child, Label1(tr("Row input cell:")), Child, rin = str_obj("", 40),
          Child, Label1(tr("Column input cell:")), Child, cin = str_obj("", 40),
        End,
        Child, TextObject, MUIA_Text_Contents, (ULONG)tr("\33cThe results are written as values (worked out now)."), End,
      End, NULL);
  if (!w) return;
  for (;;) {
    int sh, sh2;
    Rect r1, c1;
    int hr, hc;
    if (run_modal(w, ends, NULL) != ID_DLGOK) break;
    hr = sget(rin)[0] != 0; hc = sget(cin)[0] != 0;
    if ((!hr && !hc) || (hr && (!parse_range(sget(rin), &sh, &r1) || sh != cur_index())) || (hc && (!parse_range(sget(cin), &sh2, &c1) || sh2 != cur_index()))) {
      say("Give a row input cell, a column input cell or both: cells of this sheet outside the table.");
      continue;
    }
    if (!datatab_may(book, cur_index(), DT_CELLS, a)) break;
    set(win, MUIA_Window_Sleep, TRUE);
    if (book_data_table(book, cur_index(), a, hr ? r1.r1 : -1, hr ? r1.c1 : -1, hc ? c1.r1 : -1, hc ? c1.c1 : -1) < 0) { set(win, MUIA_Window_Sleep, FALSE); say("Input cell reference is not valid."); continue; }
    set(win, MUIA_Window_Sleep, FALSE);
    refresh_all();
    break;
  }
  MUI_DisposeObject(w);
}
/* a scenario's values asked for, each changing cell's (16 at most shown) */
static int scenario_values(const char *name, const i32 *r, const i32 *c, int n, char vals[][80])
{
  static const ULONG ends[] = { ID_DLGOK, 0 };
  Object *w, *grp, *str[16];
  char lab[16][24], title[100];
  int i, ok;
  if (n > 16) n = 16;
  grp = ColGroup(2), End;
  if (!grp) return 0;
  for (i = 0; i < n; i++) {
    ref_text(r[i], c[i], 1, 1, lab[i]);
    strcat(lab[i], ":");
    DoMethod(grp, OM_ADDMEMBER, (ULONG)Label1(lab[i]));
    DoMethod(grp, OM_ADDMEMBER, (ULONG)(str[i] = str_obj(vals[i], 78)));
  }
  snprintf(title, sizeof title, "%s", tr("Scenario Values"));
  w = dialog_window(title, VGroup, Child, TextObject, MUIA_Text_Contents, (ULONG)name, MUIA_Text_PreParse, (ULONG)"\33b", End,
                    Child, LLabel1(tr("Enter values for each of the changing cells:")), Child, grp, End, NULL);
  if (!w) return 0;
  ok = run_modal(w, ends, NULL) == ID_DLGOK;
  if (ok) for (i = 0; i < n; i++) { strncpy(vals[i], sget(str[i]), 79); vals[i][79] = 0; }
  MUI_DisposeObject(w);
  return ok;
}
static void scenario_add(void)
{
  static const ULONG ends[] = { ID_DLGOK, 0 };
  Object *w, *nm, *cells, *cm;
  char sel[80];
  Rect a = cur_sel();
  range_text(a, 1, sel);
  w = dialog_window("Add Scenario", ColGroup(2),
        Child, Label1(tr("Scenario name:")), Child, nm = str_obj("", 60),
        Child, Label1(tr("Changing cells:")), Child, cells = str_obj(sel, 70),
        Child, Label1(tr("Comment:")), Child, cm = str_obj("", 200),
      End, NULL);
  if (!w) return;
  set(w, MUIA_Window_ActiveObject, (ULONG)nm);
  for (;;) {
    Rect ar[8];
    int na, i, n = 0;
    i32 rr[32], cc[32];
    static char vals[32][80];
    char un[200], uc[600], name[64];
    const char *vp[32];
    if (run_modal(w, ends, NULL) != ID_DLGOK) break;
    if (!sget(nm)[0]) { say("Give the scenario a name."); continue; }
    if (!(na = parse_areas(sget(cells), ar, 8))) { say("Changing cells: give cells of this sheet, as B2:B4 or B2,D7."); continue; }
    for (i = 0; i < na; i++) {
      i32 r, c;
      for (r = ar[i].r1; r <= ar[i].r2 && n < 32; r++) for (c = ar[i].c1; c <= ar[i].c2 && n < 32; c++) { rr[n] = r; cc[n] = c; n++; }
    }
    for (i = 0; i < n; i++) { char e[300]; grid_edit_text(book, cur_index(), rr[i], cc[i], e, sizeof e); strncpy(vals[i], e, 79); vals[i][79] = 0; }
    strncpy(name, sget(nm), sizeof name - 1); name[sizeof name - 1] = 0;
    if (!scenario_values(name, rr, cc, n, vals)) continue;
    {
      static char uv[32][240];
      for (i = 0; i < n; i++) { latin1_utf8(vals[i], uv[i], sizeof uv[i]); vp[i] = uv[i]; }
    }
    latin1_utf8(name, un, sizeof un); latin1_utf8(sget(cm), uc, sizeof uc);
    if (!book_scenario_add(book, cur_index(), un, rr, cc, vp, n, uc)) say("The scenario couldn't be added (at most 32 changing cells).");
    break;
  }
  MUI_DisposeObject(w);
}
static void sm_fill(Object *list)
{
  Sheet *s = cur_sheet();
  int i;
  DoMethod(list, MUIM_List_Clear);
  for (i = 0; i < s->nscens; i++) { char t[80]; to_l1(s->scens[i].name, t, sizeof t); DoMethod(list, MUIM_List_InsertSingle, (ULONG)t, MUIV_List_Insert_Bottom); }
}
static void dlg_scenarios(void)
{
  static const ULONG ends[] = { ID_DLGOK, ID_DLG1, ID_DLG2, ID_DLG3, ID_DLG4, 0 };
  Object *w, *list, *add, *del, *show, *sum, *info;
  for (;;) {
    ULONG id;
    LONG a;
    w = dialog_window("Scenario Manager", VGroup,
          Child, LLabel1(tr("Scenarios:")),
          Child, HGroup,
            Child, ListviewObject, MUIA_Listview_Input, TRUE, MUIA_FixHeightTxt, (ULONG)"\n\n\n\n\n\n", MUIA_Listview_List, list = ListObject, InputListFrame,
              MUIA_List_ConstructHook, MUIV_List_ConstructHook_String, MUIA_List_DestructHook, MUIV_List_DestructHook_String, End, End,
            Child, VGroup, Child, add = QButton(tr("Add...")), Child, del = QButton(tr("Delete")), Child, show = QButton(tr("Show")), Child, sum = QButton(tr("Summary...")), Child, VSpace(0), End,
          End,
          Child, info = TextObject, MUIA_Text_Contents, (ULONG)tr("A scenario keeps values for some cells, to put back with Show."), End,
        End, "Close");
    if (!w) return;
    sm_fill(list);
    press_id(add, ID_DLG1); press_id(del, ID_DLG2); press_id(show, ID_DLG3); press_id(sum, ID_DLG4);
    id = run_modal(w, ends, NULL);
    a = (LONG)xget(list, MUIA_List_Active);
    MUI_DisposeObject(w);
    (void)info;
    if (id == ID_DLG1) { scenario_add(); continue; }
    if (id == ID_DLG2) { if (a >= 0) book_scenario_delete(book, cur_index(), (int)a); continue; }
    if (id == ID_DLG3) { if (a >= 0) { book_scenario_show(book, cur_index(), (int)a); refresh_all(); } continue; }
    if (id == ID_DLG4) {
      char res[80] = "";
      int sh, at;
      Rect r;
      if (!cur_sheet()->nscens) { say("Add a scenario first."); continue; }
      if (!ask_string("Scenario Summary", "Result cells:", res, sizeof res)) continue;
      at = res[0] && parse_range(res, &sh, &r) ? book_scenario_summary(book, cur_index(), &r, 1) : book_scenario_summary(book, cur_index(), NULL, 0);
      if (at >= 0) { book->active = at; set(grid, MUIA_Grid_Book, (ULONG)book); }
      refresh_all();
      break;
    }
    break;
  }
  refresh_all();
}

/* ------------------------------------------------------------------ protection */
static int ask_password(const char *title, const char *label, char *out, int cap)
{
  static const ULONG ends[] = { ID_DLGOK, 0 };
  Object *w, *pw;
  int ok;
  w = dialog_window(title, HGroup, Child, Label1(tr(label)), Child, pw = secret_obj(), End, NULL);
  if (!w) return 0;
  DoMethod(pw, MUIM_Notify, MUIA_String_Acknowledge, MUIV_EveryTime, (ULONG)app, 2, MUIM_Application_ReturnID, ID_DLGOK);
  set(w, MUIA_Window_ActiveObject, (ULONG)pw);
  ok = run_modal(w, ends, NULL) == ID_DLGOK;
  if (ok) latin1_utf8(sget(pw), out, (size_t)cap);
  MUI_DisposeObject(w);
  return ok;
}
static const char *const pa_names[] = { "Select locked cells", "Select unlocked cells", "Format cells", "Format columns", "Format rows", "Insert columns",
                                        "Insert rows", "Insert hyperlinks", "Delete columns", "Delete rows", "Sort", "Use AutoFilter", "Use PivotTable reports",
                                        "Edit objects", "Edit scenarios" };
static void protect_sheet(void)
{
  static const ULONG ends[] = { ID_DLGOK, 0 };
  Sheet *s = cur_sheet();
  char pw[200], pw2[200];
  if (s->protect) {
    pw[0] = 0;
    if ((s->pwhash || s->protkey[0]) && !ask_password("Unprotect Sheet", "Password:", pw, sizeof pw)) return;
    set(win, MUIA_Window_Sleep, TRUE);
    if (!book_unprotect_sheet(book, cur_index(), pw)) { set(win, MUIA_Window_Sleep, FALSE); say("The password you supplied is not correct. Check that the Caps Lock key is off and be sure to use the correct capitalisation."); return; }
    set(win, MUIA_Window_Sleep, FALSE);
  } else {
    Object *w, *pwo, *sc, *tick;
    static const char *labs[16];
    static UBYTE on[16];
    int i;
    for (i = 0; i < 15; i++) { labs[i] = tr(pa_names[i]); on[i] = (u8)((PA_DEFAULT >> i) & 1); }
    sc = NewObject(flatscroll_class()->mcc_Class, NULL, TAG_DONE);
    w = dialog_window("Protect Sheet", VGroup,
          Child, LLabel1(tr("Protect the sheet and the contents of locked cells.")),
          Child, HGroup, Child, Label1(tr("Password to unprotect sheet (optional):")), Child, pwo = secret_obj(), End,
          Child, LLabel1(tr("Allow all users of this sheet to:")),
          Child, HGroup, MUIA_Group_Spacing, 0,
            Child, tick = NewObject(ticklist_class()->mcc_Class, NULL, MUIA_Tick_Labels, (ULONG)labs, MUIA_Tick_States, (ULONG)on, MUIA_Tick_Count, 15, MUIA_Tick_Prop, (ULONG)sc, TAG_DONE),
            Child, sc,
          End,
        End, NULL);
    if (!w) return;
    DoMethod(sc, MUIM_Notify, MUIA_Prop_First, MUIV_EveryTime, (ULONG)tick, 3, MUIM_Set, MUIA_Tick_First, MUIV_TriggerValue);
    set(w, MUIA_Window_ActiveObject, (ULONG)pwo);
    if (run_modal(w, ends, NULL) == ID_DLGOK) {
      u16 allow = 0;
      latin1_utf8(sget(pwo), pw, sizeof pw);
      for (i = 0; i < 15; i++) if (on[i]) allow |= (u16)(1 << i);
      if (!pw[0] || (ask_password("Confirm Password", "Reenter password to proceed:", pw2, sizeof pw2) && (!strcmp(pw, pw2) || (say("The confirmation password is not identical."), 0))))
        book_protect_sheet(book, cur_index(), pw, allow);
    }
    MUI_DisposeObject(w);
  }
  refresh_all();
}
static void protect_book(void)
{
  char pw[200], pw2[200];
  if (book->lockstruct) {
    pw[0] = 0;
    if ((book->bookpwhash || book->bookkey[0]) && !ask_password("Unprotect Workbook", "Password:", pw, sizeof pw)) return;
    set(win, MUIA_Window_Sleep, TRUE);
    if (!book_unprotect_book(book, pw)) { set(win, MUIA_Window_Sleep, FALSE); say("The password you supplied is not correct."); return; }
    set(win, MUIA_Window_Sleep, FALSE);
  } else {
    if (!ask_password("Protect Structure", "Password (optional):", pw, sizeof pw)) return;
    if (pw[0] && (!ask_password("Confirm Password", "Reenter password to proceed:", pw2, sizeof pw2) || strcmp(pw, pw2))) { say("The confirmation password is not identical."); return; }
    book_protect_book(book, pw);
  }
  refresh_all();
}

/* ------------------------------------------------------------------ Get Data: a text file, previewed */
static struct { Object *delim, *other, *list, *where; char *text; size_t len; } gd;
static char gd_delim(void)
{
  static const char ds[] = { 0, ',', ';', '\t', ' ' };
  int k = (int)xget(gd.delim, MUIA_Cycle_Active);
  if (k == 5) return sget(gd.other)[0] ? sget(gd.other)[0] : ',';
  if (k == 0) {
    /* as csv_load guesses: the most common of comma, semicolon and tab in the first line */
    size_t i;
    int c = 0, s = 0, t = 0;
    for (i = 0; i < gd.len && gd.text[i] != '\n'; i++) { if (gd.text[i] == ',') c++; else if (gd.text[i] == ';') s++; else if (gd.text[i] == '\t') t++; }
    return t > c && t > s ? '\t' : s > c ? ';' : ',';
  }
  return ds[k];
}
static void gd_preview(void)
{
  TTCOpts o;
  size_t i = 0;
  int lines = 0;
  char d = gd_delim();
  memset(&o, 0, sizeof o);
  o.quote = '"';
  if (d == ',') o.delims = TTC_COMMA; else if (d == ';') o.delims = TTC_SEMI; else if (d == '\t') o.delims = TTC_TAB; else if (d == ' ') o.delims = TTC_SPACE; else { o.delims = TTC_OTHER; o.other = d; }
  set(gd.other, MUIA_Disabled, xget(gd.delim, MUIA_Cycle_Active) != 5);
  DoMethod(gd.list, MUIM_List_Clear);
  while (i < gd.len && lines < 8) {
    char ln[600], line[400], **parts, t[200];
    size_t k = 0;
    int n, j;
    while (i < gd.len && gd.text[i] != '\n' && k < sizeof ln - 1) { if (gd.text[i] != '\r') ln[k++] = gd.text[i]; i++; }
    while (i < gd.len && gd.text[i] != '\n') i++;
    i++;
    ln[k] = 0;
    n = ttc_split(ln, &o, &parts);
    line[0] = 0;
    for (j = 0; j < n; j++) { to_l1(parts[j], t, sizeof t); if (strlen(line) + strlen(t) + 4 < sizeof line) { if (j) strcat(line, " | "); strcat(line, t); } free(parts[j]); }
    free(parts);
    DoMethod(gd.list, MUIM_List_InsertSingle, (ULONG)line, MUIV_List_Insert_Bottom);
    lines++;
  }
}
static void gd_each(ULONG id) { if (id == ID_DLG1) gd_preview(); }
static int ask_file(const char *title, char *path, size_t cap, const char *pattern)
{
  struct FileRequester *fr;
  int ok = 0;
  if (!(fr = (struct FileRequester *)MUI_AllocAslRequestTags(ASL_FileRequest, ASLFR_TitleText, (ULONG)tr(title), ASLFR_InitialPattern, (ULONG)pattern, ASLFR_DoPatterns, TRUE, TAG_DONE))) return 0;
  if (MUI_AslRequestTags(fr, ASLFR_Window, (ULONG)_window(grid), TAG_DONE)) {
    strncpy(path, (const char *)fr->fr_Drawer, cap - 1); path[cap - 1] = 0;
    ok = AddPart((STRPTR)path, fr->fr_File, (ULONG)cap) && fr->fr_File[0];
  }
  MUI_FreeAslRequest(fr);
  return ok;
}
static void get_data(void)
{
  static const ULONG ends[] = { ID_DLGOK, 0 };
  static const char *delims_en[] = { "Automatic", "Comma", "Semicolon", "Tab", "Space", "Other", NULL };
  static const char *where_en[] = { "A new sheet", "The active cell of this sheet", NULL };
  static const char *delims[8], *wheres[4];
  char path[512];
  size_t len;
  u8 *data;
  Object *w;
  path[0] = 0;
  if (!ask_file("Get Data from Text/CSV", path, sizeof path, "#?.(csv|txt|tsv|prn)")) return;
  if (!(data = file_read(path, &len))) { ask("OK", "Ledger couldn't read\n%s", path); return; }
  gd.text = (char *)data; gd.len = len;
  trl(delims_en, delims); trl(where_en, wheres);
  w = dialog_window("Import Text File", VGroup,
        Child, TextObject, MUIA_Text_Contents, (ULONG)FilePart((STRPTR)path), MUIA_Text_PreParse, (ULONG)"\33b", End,
        Child, ColGroup(2),
          Child, Label1(tr("Delimiter:")), Child, HGroup, Child, gd.delim = cycle_obj(delims, 0), Child, gd.other = str_obj("", 2), End,
          Child, Label1(tr("Put the data in:")), Child, gd.where = cycle_obj(wheres, 0),
        End,
        Child, LLabel1(tr("Preview:")),
        Child, ListviewObject, MUIA_Listview_Input, FALSE, MUIA_FixHeightTxt, (ULONG)"\n\n\n\n\n\n", MUIA_Listview_List, gd.list = ListObject, ReadListFrame,
          MUIA_List_ConstructHook, MUIV_List_ConstructHook_String, MUIA_List_DestructHook, MUIV_List_DestructHook_String, End, End,
      End, "Load");
  if (w) {
    notify_id(gd.delim, MUIA_Cycle_Active, ID_DLG1); notify_id(gd.other, MUIA_String_Contents, ID_DLG1);
    gd_preview();
    if (run_modal(w, ends, gd_each) == ID_DLGOK) {
      int newsheet = xget(gd.where, MUIA_Cycle_Active) == 0;
      int either, both;
      Book *b = csv_load_report(gd.text, gd.len, gd_delim(), &either, &both);
      if (b) csv_dates_note(either, both);
      if (newsheet && book->lockstruct) { datatab_refused(ID_IMPORTCSV, 0); }
      else if (b) {
        int at = cur_index();
        i32 r0 = 0, c0 = 0;
        Rect all;
        Clip *cl;
        if (newsheet) {
          char base[64], nm[32];
          at = book_add_sheet(book, NULL, book->nsh);
          strncpy(base, (const char *)FilePart((STRPTR)path), sizeof base - 1); base[sizeof base - 1] = 0;
          if (strrchr(base, '.')) *strrchr(base, '.') = 0;
          base[31] = 0;
          if (at >= 0) { book_unique_sheet_name(book, base, nm); book_rename_sheet(book, at, nm); }
        } else { r0 = cur_sheet()->cur_r; c0 = cur_sheet()->cur_c; }
        all.r1 = 0; all.c1 = 0; all.r2 = sheet_last_row(b->sh[0]); all.c2 = sheet_last_col(b->sh[0]);
        if (at >= 0 && all.r2 >= 0 && (cl = clip_copy(b, 0, all))) { book_paste(book, at, r0, c0, cl, PASTE_ALL, 0, NULL); clip_free(cl); }
        if (at >= 0 && newsheet) { book->active = at; set(grid, MUIA_Grid_Book, (ULONG)book); }
      }
      if (b) book_free(b);
      refresh_all();
    }
    MUI_DisposeObject(w);
  }
  free(data);
  gd.text = NULL;
}

/* ------------------------------------------------------------------ Flash Fill */
static void flash_fill(void)
{
  Sheet *s = cur_sheet();
  Rect one;
  one.r1 = 0; one.r2 = sheet_last_row(s) > 0 ? sheet_last_row(s) : 0; one.c1 = one.c2 = s->cur_c;
  if (!datatab_may(book, cur_index(), DT_CELLS, one)) return;
  if (!book_flash_fill(book, cur_index(), s->cur_r, s->cur_c))
    say("Flash Fill couldn't see a pattern to fill in.\n\nType an example or two of what you want in the column next to your data,\nselect a cell in that column, and try again.");
  refresh_all();
}

/* ------------------------------------------------------------------ the commands */
int datatab_command(ULONG id, LONG v)
{
  Sheet *s = cur_sheet();
  if (datatab_refused(id, v)) return 1;
  switch (id) {
  case ID_DV: dlg_validation(); return 1;
  case ID_DVDD:
    if (v == 0) dlg_validation();
    else if (v == 1) { gx_circles(book, cur_index(), 1); refresh_all(); }
    else if (v == 2) { gx_circles(book, cur_index(), 0); refresh_all(); }
    else { Rect a = cur_sel(); book_dv_set(book, cur_index(), &a, 1, NULL); refresh_all(); }
    return 1;
  case ID_DVCLEAR: { Rect a = cur_sel(); book_dv_set(book, cur_index(), &a, 1, NULL); refresh_all(); return 1; }
  case ID_DVPICK: pick_list(); return 1;
  case ID_CIRCLE: gx_circles(book, cur_index(), 1); refresh_all(); return 1;
  case ID_CIRCLECLEAR: gx_circles(book, cur_index(), 0); refresh_all(); return 1;
  case ID_TEXTCOLS: dlg_text_to_columns(); return 1;
  case ID_FLASHFILL: flash_fill(); return 1;
  case ID_REMOVEDUP: dlg_remove_duplicates(); return 1;
  case ID_CONSOLIDATE: dlg_consolidate(); return 1;
  case ID_WHATIFDD:
    if (v == 0) dlg_scenarios();
    else if (v == 1) { if (!datatab_refused(ID_GOALSEEK, 0)) dlg_goal_seek(); }
    else if (!datatab_refused(ID_DATATABLE, 0)) dlg_data_table();
    return 1;
  case ID_GOALSEEK: dlg_goal_seek(); return 1;
  case ID_DATATABLE: dlg_data_table(); return 1;
  case ID_SCENARIOS: dlg_scenarios(); return 1;
  case ID_GROUP: group(1); return 1;
  case ID_UNGROUP: group(0); return 1;
  case ID_GROUPDD: if (v == 0) group(1); else if (v == 1) group(0); else { book_outline_clear(book, cur_index()); refresh_all(); } return 1;
  case ID_CLEAROUTLINE: book_outline_clear(book, cur_index()); refresh_all(); return 1;
  case ID_SUBTOTAL: dlg_subtotal(); return 1;
  case ID_SHOWDETAIL: detail(0); return 1;
  case ID_HIDEDETAIL: detail(1); return 1;
  case ID_OUTLINESET: dlg_outline_settings(); return 1;
  case ID_ADVFILTER: dlg_advanced_filter(); return 1;
  case ID_CLEARFILTER:
    /* rows the Advanced Filter hid (no AutoFilter here): shown again */
    if (s->filter.r1 < 0) { book_filter_show_all(book, cur_index()); refresh_all(); return 1; }
    return 0;
  case ID_REAPPLY:
    if (s->filter.r1 < 0 && adv_book == book && adv_sheet == cur_index()) {
      book_advanced_filter(book, cur_index(), adv_list, adv_hascrit ? &adv_crit : NULL, -1, 0, 0, adv_unique);
      refresh_all();
      return 1;
    }
    return 0;
  case ID_PROTECTSHEET: protect_sheet(); return 1;
  case ID_PROTECTBOOK: protect_book(); return 1;
  case ID_GETDATA: get_data(); return 1;
  }
  return 0;
}
