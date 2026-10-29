/*
 * dialogs.c - Ledger's dialog windows: Format Cells, Insert Function, the Name Manager, Sort, Find
 * and Replace, notes, links, conditional formatting, the AutoFilter's lists, Go To, Series, Insert/Delete Cells, Unhide, custom number formats and the
 * workbook's statistics.
 *
 * Each is modal (run_modal in main.c): made when asked for, run until OK or Cancel, then disposed
 * of. They act on the window being worked in through the globals in app.h.
 */
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <proto/exec.h>
#include <proto/intuition.h>
#include <proto/muimaster.h>
#include <proto/utility.h>
#include <clib/alib_protos.h>
#include "app.h"
#include "theme.h"
#include "lang.h"
#include "ids.h"
#include "../core/calc.h"
#include "../core/funcdesc.h"
#include "../core/cond.h"

#undef Label
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
static Object *str_obj(const char *init, int max)
{
  return StringObject, StringFrame, MUIA_String_Contents, (ULONG)init, MUIA_String_MaxLen, max, MUIA_CycleChain, 1, End;
}
static Object *check_obj(int on)
{
  return ImageObject, ImageButtonFrame, MUIA_InputMode, MUIV_InputMode_Toggle, MUIA_Image_Spec, MUII_CheckMark,
    MUIA_Image_FreeVert, TRUE, MUIA_Selected, on, MUIA_Background, MUII_ButtonBack, MUIA_ShowSelState, FALSE, MUIA_CycleChain, 1, End;
}
static u8 checked_(Object *o) { return xget(o, MUIA_Selected) ? 1 : 0; }
static Object *cycle_obj(const char **labels, int active)
{
  return CycleObject, MUIA_Cycle_Entries, (ULONG)labels, MUIA_Cycle_Active, active, MUIA_CycleChain, 1, End;
}
/* tr() over a NULL-terminated list, into out (kept by the caller while the cycle lives) */
static const char **trl(const char *const *en, const char **out)
{
  int i;
  for (i = 0; en[i]; i++) out[i] = tr(en[i]);
  out[i] = NULL;
  return out;
}

/* ------------------------------------------------------------------ Format Cells */
static const char *const fc_cats[] = { "General", "Number", "Currency", "Accounting", "Short Date", "Long Date", "Time", "Percentage",
                                       "Fraction", "Scientific", "Text", "Custom", NULL };
static const char *const fc_halign[] = { "General", "Left", "Centre", "Right", "Fill", "Justify", "Centre Across Selection", NULL };
static const char *const fc_valign[] = { "Bottom", "Centre", "Top", NULL };
static const char *const fc_lines[] = { "None", "Thin", "Medium", "Thick", "Double", "Dotted", "Dashed", NULL };
static const char *const fc_colors[] = { "Automatic", "Black", "White", "Red", "Dark Red", "Orange", "Gold", "Yellow", "Green", "Dark Green",
                                         "Teal", "Blue", "Dark Blue", "Light Blue", "Purple", "Grey", "Light Grey", "Custom...", NULL };
static const ULONG fc_colorvals[] = { COLOR_AUTO, 0x000000, 0xFFFFFF, 0xFF0000, 0xC00000, 0xFFA500, 0xBF9000, 0xFFFF00, 0x00B050, 0x006100,
                                      0x008080, 0x0070C0, 0x002060, 0xDDEBF7, 0x7030A0, 0x808080, 0xD9D9D9, COLOR_MORE };
static const char *const fc_fills[] = { "No Fill", "Yellow", "Light Yellow", "Light Green", "Green", "Light Blue", "Blue", "Light Red",
                                        "Orange", "Light Grey", "Grey", "Lavender", "Dark Blue", "Black", "Custom...", NULL };
static const ULONG fc_fillvals[] = { 0, 0xFFFF00, 0xFFF2CC, 0xE2EFDA, 0x70AD47, 0xDDEBF7, 0x4472C4, 0xFCE4D6, 0xF4B084, 0xF2F2F2, 0xBFBFBF,
                                     0xE4DFEC, 0x203764, 0x000000, COLOR_MORE };
static int color_index(ULONG c, const ULONG *vals, int n)
{
  int i;
  for (i = 0; i < n; i++) if (vals[i] == c) return i;
  return n - 1;                                            /* (Custom...) */
}
static struct { Object *cat, *dec, *code, *sample, *ha, *va, *wrap, *indent, *font, *size, *b, *i, *u, *s, *color, *line[4], *fill, *locked, *hidden; ULONG custcolor, custfill; } fc;
/* the sample: the active cell's value in the code as it stands */
static void fc_sample(void)
{
  char out[128], u[300], l1[128];
  u32 col;
  const Value *v = cell_value(book, cur_index(), cur_sheet()->cur_r, cur_sheet()->cur_c);
  Value x = val_num(1234.5678);
  latin1_utf8(sget(fc.code), u, sizeof u);
  fmt_value(book, u, v->t == V_EMPTY ? &x : v, out, sizeof out, &col, 11);
  utf8_to_latin1(out, (u32)strlen(out), l1, sizeof l1);
  set(fc.sample, MUIA_Text_Contents, (ULONG)l1);
}
/* a category or the decimals changed: the code made afresh (not for Custom, which is typed) */
static void fc_each(ULONG id)
{
  if (id == ID_DLG1) {
    int cat = (int)xget(fc.cat, MUIA_Cycle_Active);
    char code[160];
    if (cat < 11) {
      numfmt_code(cat, atoi(sget(fc.dec)), code, sizeof code);
      SetAttrs(fc.code, MUIA_NoNotify, TRUE, MUIA_String_Contents, (ULONG)code, TAG_DONE);
    }
    fc_sample();
  } else if (id == ID_DLG2) {
    set(fc.cat, MUIA_Cycle_Active, 11);
    fc_sample();
  } else if (id == ID_DLG3) {
    if (xget(fc.color, MUIA_Cycle_Active) == 17) { LONG c = ask_color(fc.custcolor); if (c >= 0) fc.custcolor = (ULONG)c; }
  } else if (id == ID_DLG4) {
    if (xget(fc.fill, MUIA_Cycle_Active) == 14) { LONG c = ask_color(fc.custfill); if (c >= 0) fc.custfill = (ULONG)c; }
  }
}
void dlg_format_cells(int page)
{
  static const char *pages_en[] = { "Number", "Alignment", "Font", "Border", "Fill", "Protection", NULL };
  static const char *pl[8], *cats[16], *has[10], *vas[6], *lines[4][10], *colors[20], *fills[18];
  static const ULONG ends[] = { ID_DLGOK, 0 };
  const Style *st0 = &book->styles[sheet_cell_style(cur_sheet(), cur_sheet()->cur_r, cur_sheet()->cur_c)];
  const Font *f0 = &book->fonts[st0->font];
  Style st = *st0;
  Font f = *f0;
  char code[160], dec[8], size[16], fam[64], lbuf[4][8];
  Object *w, *reg;
  int k, cat;
  const char *cname;
  unsigned mask = 0;
  strncpy(code, book_fmt_code(book, st0->numfmt), sizeof code - 1); code[sizeof code - 1] = 0;
  cname = numfmt_name(code);
  for (cat = 0; fc_cats[cat] && strcmp(fc_cats[cat], cname); cat++) ;
  if (!fc_cats[cat] || !strcmp(cname, "Date")) cat = 11;
  {
    const char *dot = strchr(code, '.');
    int n = 0;
    if (dot) for (dot++; *dot == '0'; dot++) n++;
    sprintf(dec, "%d", n);
  }
  sprintf(size, "%d", f0->size / 10);
  utf8_to_latin1(f0->name, (u32)strlen(f0->name), fam, sizeof fam);
  fc.custcolor = f0->color & 0xFFFFFF; fc.custfill = st0->fill & 0xFFFFFF;
  trl(pages_en, pl); trl(fc_cats, cats); trl(fc_halign, has); trl(fc_valign, vas); trl(fc_colors, colors); trl(fc_fills, fills);
  for (k = 0; k < 4; k++) trl(fc_lines, lines[k]);
  (void)lbuf;
  reg = RegisterGroup(pl), MUIA_Register_Frame, TRUE, MUIA_CycleChain, 1,
    /* Number */
    Child, VGroup,
      Child, ColGroup(2),
        Child, Label1(tr("Category:")), Child, fc.cat = cycle_obj(cats, cat),
        Child, Label1(tr("Decimal places:")), Child, fc.dec = StringObject, StringFrame, MUIA_String_Contents, (ULONG)dec, MUIA_String_Accept, (ULONG)"0123456789", MUIA_String_MaxLen, 3, MUIA_CycleChain, 1, End,
        Child, Label1(tr("Format code:")), Child, fc.code = str_obj(code, 150),
        Child, Label1(tr("Sample:")), Child, fc.sample = TextObject, TextFrame, MUIA_Text_Contents, (ULONG)"", End,
      End,
      Child, TextObject, MUIA_Text_Contents, (ULONG)tr("\33cA code can have up to four sections: positive;negative;zero;text.\n0 a digit, # a digit if needed, , thousands, % per cent, d m y h s dates and times."), End,
      Child, VSpace(0),
    End,
    /* Alignment */
    Child, VGroup,
      Child, ColGroup(2),
        Child, Label1(tr("Horizontal:")), Child, fc.ha = cycle_obj(has, st0->halign),
        Child, Label1(tr("Vertical:")), Child, fc.va = cycle_obj(vas, st0->valign),
        Child, Label1(tr("Indent:")), Child, fc.indent = StringObject, StringFrame, MUIA_String_Integer, st0->indent, MUIA_String_Accept, (ULONG)"0123456789", MUIA_String_MaxLen, 3, MUIA_CycleChain, 1, End,
        Child, Label1(tr("Wrap text:")), Child, HGroup, Child, fc.wrap = check_obj(st0->wrap), Child, HSpace(0), End,
      End,
      Child, VSpace(0),
    End,
    /* Font */
    Child, VGroup,
      Child, ColGroup(2),
        Child, Label1(tr("Font:")), Child, fc.font = str_obj(fam, 40),
        Child, Label1(tr("Size:")), Child, fc.size = StringObject, StringFrame, MUIA_String_Contents, (ULONG)size, MUIA_String_Accept, (ULONG)"0123456789.", MUIA_String_MaxLen, 6, MUIA_CycleChain, 1, End,
        Child, Label1(tr("Colour:")), Child, fc.color = cycle_obj(colors, color_index(f0->color & 0xFF000000UL ? COLOR_AUTO : f0->color, fc_colorvals, 18)),
      End,
      Child, HGroup,
        Child, fc.b = check_obj(f0->bold), Child, LLabel1(tr("Bold")),
        Child, fc.i = check_obj(f0->italic), Child, LLabel1(tr("Italic")),
        Child, fc.u = check_obj(f0->underline != 0), Child, LLabel1(tr("Underline")),
        Child, fc.s = check_obj(f0->strike), Child, LLabel1(tr("Strikethrough")),
        Child, HSpace(0),
      End,
      Child, VSpace(0),
    End,
    /* Border */
    Child, VGroup,
      Child, ColGroup(2),
        Child, Label1(tr("Left:")), Child, fc.line[0] = cycle_obj(lines[0], st0->border[B_LEFT] < 7 ? st0->border[B_LEFT] : 1),
        Child, Label1(tr("Right:")), Child, fc.line[1] = cycle_obj(lines[1], st0->border[B_RIGHT] < 7 ? st0->border[B_RIGHT] : 1),
        Child, Label1(tr("Top:")), Child, fc.line[2] = cycle_obj(lines[2], st0->border[B_TOP] < 7 ? st0->border[B_TOP] : 1),
        Child, Label1(tr("Bottom:")), Child, fc.line[3] = cycle_obj(lines[3], st0->border[B_BOTTOM] < 7 ? st0->border[B_BOTTOM] : 1),
      End,
      Child, TextObject, MUIA_Text_Contents, (ULONG)tr("\33cThese lines go round each selected cell."), End,
      Child, VSpace(0),
    End,
    /* Fill */
    Child, VGroup,
      Child, ColGroup(2),
        Child, Label1(tr("Background:")), Child, fc.fill = cycle_obj(fills, st0->fill ? color_index(st0->fill & 0xFFFFFF, fc_fillvals, 15) : 0),
      End,
      Child, VSpace(0),
    End,
    /* Protection (datatab: Protect Sheet) */
    Child, VGroup,
      Child, HGroup, Child, fc.locked = check_obj(st0->locked), Child, LLabel1(tr("Locked")), Child, HSpace(0), End,
      Child, HGroup, Child, fc.hidden = check_obj(st0->hidden), Child, LLabel1(tr("Hidden")), Child, HSpace(0), End,
      Child, TextObject, MUIA_Text_Contents, (ULONG)tr("\33cLocking cells or hiding formulas has no effect until you protect the sheet\n(Review, Protect Sheet)."), End,
      Child, VSpace(0),
    End,
  End;
  w = dialog_window("Format Cells", reg, NULL);
  if (!w) return;
  set(reg, MUIA_Group_ActivePage, page);
  DoMethod(fc.cat, MUIM_Notify, MUIA_Cycle_Active, MUIV_EveryTime, (ULONG)app, 2, MUIM_Application_ReturnID, ID_DLG1);
  DoMethod(fc.dec, MUIM_Notify, MUIA_String_Contents, MUIV_EveryTime, (ULONG)app, 2, MUIM_Application_ReturnID, ID_DLG1);
  DoMethod(fc.code, MUIM_Notify, MUIA_String_Contents, MUIV_EveryTime, (ULONG)app, 2, MUIM_Application_ReturnID, ID_DLG2);
  DoMethod(fc.color, MUIM_Notify, MUIA_Cycle_Active, MUIV_EveryTime, (ULONG)app, 2, MUIM_Application_ReturnID, ID_DLG3);
  DoMethod(fc.fill, MUIM_Notify, MUIA_Cycle_Active, MUIV_EveryTime, (ULONG)app, 2, MUIM_Application_ReturnID, ID_DLG4);
  fc_sample();
  if (run_modal(w, ends, fc_each) == ID_DLGOK) {
    char u[300];
    int ci, fi;
    ULONG c;
    /* only what was changed is applied, so a mixed selection keeps the rest of its looks */
    latin1_utf8(sget(fc.code), u, sizeof u);
    if (strcmp(u, book_fmt_code(book, st0->numfmt))) { st.numfmt = (u16)book_fmt(book, u); mask |= SM_NUMFMT; }
    st.halign = (u8)xget(fc.ha, MUIA_Cycle_Active); if (st.halign != st0->halign) mask |= SM_HALIGN;
    st.valign = (u8)xget(fc.va, MUIA_Cycle_Active); if (st.valign != st0->valign) mask |= SM_VALIGN;
    st.wrap = xget(fc.wrap, MUIA_Selected) ? 1 : 0; if (st.wrap != st0->wrap) mask |= SM_WRAP;
    st.indent = (u8)atoi(sget(fc.indent)); if (st.indent != st0->indent) mask |= SM_INDENT;
    latin1_utf8(sget(fc.font), f.name, sizeof f.name);
    f.size = (u16)(atof(sget(fc.size)) * 10 + 0.5);
    if (f.size < 10) f.size = f0->size;
    f.bold = xget(fc.b, MUIA_Selected) ? 1 : 0; f.italic = xget(fc.i, MUIA_Selected) ? 1 : 0;
    f.underline = xget(fc.u, MUIA_Selected) ? (f0->underline ? f0->underline : 1) : 0; f.strike = xget(fc.s, MUIA_Selected) ? 1 : 0;
    ci = (int)xget(fc.color, MUIA_Cycle_Active);
    c = fc_colorvals[ci];
    f.color = c == COLOR_MORE ? fc.custcolor : c;
    if (memcmp(&f, f0, sizeof f)) mask |= SM_FONT;
    for (k = 0; k < 4; k++) {
      static const int side[4] = { B_LEFT, B_RIGHT, B_TOP, B_BOTTOM };
      st.border[side[k]] = (u8)xget(fc.line[k], MUIA_Cycle_Active);
      if (st.border[side[k]] != st0->border[side[k]]) { mask |= SM_BORDER; st.bcolor[side[k]] = COLOR_AUTO; }
    }
    fi = (int)xget(fc.fill, MUIA_Cycle_Active);
    st.fill = fi == 0 ? 0 : FILL_ON | (fc_fillvals[fi] == COLOR_MORE ? fc.custfill : fc_fillvals[fi]);
    if (st.fill != st0->fill) mask |= SM_FILL;
    st.locked = checked_(fc.locked); st.hidden = checked_(fc.hidden);
    if (st.locked != st0->locked || st.hidden != st0->hidden) mask |= SM_PROT;
    if (mask) apply_style(&st, &f, mask);
  }
  MUI_DisposeObject(w);
}

/* ------------------------------------------------------------------ custom number format */
static const char *const nf_examples[] = { "General", "0", "0.00", "#,##0", "#,##0.00", "#,##0;[Red]-#,##0", "$#,##0.00", "0%", "0.00%",
  "0.00E+00", "# ?/?", "d/mm/yyyy", "m/d/yyyy", "d-mmm-yy", "d mmmm yyyy", "dddd", "mmm-yy", "h:mm", "h:mm AM/PM", "h:mm:ss", "[h]:mm",
  "d/mm/yyyy h:mm", "@", "\"Total: \"0.00", "0;-0;\"-\"", NULL };
static Object *nf_list, *nf_str;
static void nf_each(ULONG id)
{
  if (id == ID_DLG1) {
    LONG a = (LONG)xget(nf_list, MUIA_List_Active);
    if (a >= 0 && nf_examples[a]) set(nf_str, MUIA_String_Contents, (ULONG)nf_examples[a]);
  }
}
int dlg_number_format(char *code, int cap)
{
  static const ULONG ends[] = { ID_DLGOK, 0 };
  Object *w;
  char l1[200];
  int ok;
  utf8_to_latin1(code, (u32)strlen(code), l1, sizeof l1);
  w = dialog_window("Number Format", VGroup,
        Child, HGroup, Child, Label1(tr("Code:")), Child, nf_str = str_obj(l1, 150), End,
        Child, ListviewObject, MUIA_Listview_List, nf_list = ListObject, InputListFrame, MUIA_List_SourceArray, (ULONG)nf_examples, End,
                 MUIA_CycleChain, 1, End,
      End, NULL);
  if (!w) return 0;
  DoMethod(nf_list, MUIM_Notify, MUIA_List_Active, MUIV_EveryTime, (ULONG)app, 2, MUIM_Application_ReturnID, ID_DLG1);
  DoMethod(nf_str, MUIM_Notify, MUIA_String_Acknowledge, MUIV_EveryTime, (ULONG)app, 2, MUIM_Application_ReturnID, ID_DLGOK);
  ok = run_modal(w, ends, nf_each) == ID_DLGOK;
  if (ok) latin1_utf8(sget(nf_str), code, (size_t)cap);
  MUI_DisposeObject(w);
  return ok;
}

/* ------------------------------------------------------------------ Insert Function */
/* each function's arguments, what it does and its category: core/funcdesc.c. The list shows the
   category's functions (All: every one in func_table), or those whose name or description has the
   text typed in Search */
#define FN_MAXSHOWN 900
static int strstr_nocase(const char *s, const char *f)
{
  size_t n = strlen(f);
  for (; *s; s++) { size_t i; for (i = 0; i < n && s[i] && ToUpper((UBYTE)s[i]) == ToUpper((UBYTE)f[i]); i++) ; if (i == n) return 1; }
  return 0;
}
/* text broken into lines of at most w characters at its spaces (in place) */
static void fn_wrap(char *t, int w)
{
  char *line = t, *sp = NULL, *p;
  for (p = t; *p; p++) {
    if (*p == '\n') { line = p + 1; sp = NULL; continue; }
    if (*p == ' ') sp = p;
    if (p - line >= w && sp) { *sp = '\n'; line = sp + 1; sp = NULL; }
  }
}
static const char *fn_shown[FN_MAXSHOWN + 1]; static int fn_nshown;
static Object *fn_cat, *fn_list, *fn_text, *fn_find;
static int fn_matches(const char *name, const char *find)
{
  const FuncDesc *fd;
  if (!*find) return 1;
  if (strstr_nocase(name, find)) return 1;
  fd = func_desc(name);
  return fd && strstr_nocase(fd->what, find);
}
static void fn_fill(void)
{
  int cat = (int)xget(fn_cat, MUIA_Cycle_Active), i;
  const char *find = fn_find ? sget(fn_find) : "";
  fn_nshown = 0;
  if (cat == FC_ALL) { func_init(); for (i = 0; i < func_count && fn_nshown < FN_MAXSHOWN; i++) if (fn_matches(func_table[i].name, find)) fn_shown[fn_nshown++] = func_table[i].name; }
  else for (i = 0; i < func_ndescs && fn_nshown < FN_MAXSHOWN; i++) if (func_descs[i].cat == cat && fn_matches(func_descs[i].name, find)) fn_shown[fn_nshown++] = func_descs[i].name;
  fn_shown[fn_nshown] = NULL;
  set(fn_list, MUIA_List_Quiet, TRUE);
  DoMethod(fn_list, MUIM_List_Clear);
  DoMethod(fn_list, MUIM_List_Insert, (ULONG)fn_shown, fn_nshown, MUIV_List_Insert_Bottom);
  set(fn_list, MUIA_List_Quiet, FALSE);
  set(fn_list, MUIA_List_Active, 0);
  if (!fn_nshown) set(fn_text, MUIA_Text_Contents, (ULONG)tr("No function matches."));
}
static void fn_each(ULONG id)
{
  if (id == ID_FUNCCAT || id == ID_FUNCGO) fn_fill();
  else if (id == ID_FUNCPICK) {
    LONG a = (LONG)xget(fn_list, MUIA_List_Active);
    static char t[400];
    char u[360];
    const FuncDesc *fd;
    if (a < 0 || a >= fn_nshown) return;
    if ((fd = func_desc(fn_shown[a]))) {
      char l1[300];
      snprintf(u, sizeof u, "%s(%s)", fd->name, fd->args);
      utf8_to_latin1(u, (u32)strlen(u), l1, sizeof l1);
      snprintf(t, sizeof t, "\33b%s\33n\n", l1);
      utf8_to_latin1(tr(fd->what), (u32)strlen(tr(fd->what)), l1, sizeof l1);
      strncat(t, l1, sizeof t - strlen(t) - 1);
      fn_wrap(t, 58);
    } else snprintf(t, sizeof t, "\33b%s\33n", fn_shown[a]);
    set(fn_text, MUIA_Text_Contents, (ULONG)t);
  }
}
void dlg_insert_function(void)
{
  static const ULONG ends[] = { ID_DLGOK, 0 };
  static const char *cats[FC_N + 1], *names[FC_N + 1];
  Object *w;
  int i;
  for (i = 0; i < FC_N; i++) names[i] = func_cat_names[i];
  names[FC_N] = NULL;
  trl(names, cats);
  w = dialog_window("Insert Function", VGroup,
        Child, HGroup, Child, Label1(tr("Search:")), Child, fn_find = str_obj("", 40),
                       Child, Label1(tr("Category:")), Child, fn_cat = cycle_obj(cats, 0), End,
        Child, ListviewObject, MUIA_Listview_List, fn_list = ListObject, InputListFrame, End, MUIA_CycleChain, 1, MUIA_Listview_DoubleClick, TRUE, End,
        Child, fn_text = TextObject, TextFrame, MUIA_Text_SetMin, FALSE, MUIA_FixHeightTxt, (ULONG)"Wg\nWg\nWg\nWg", End,
      End, "Insert");
  if (!w) return;
  set(w, MUIA_Window_Height, MUIV_Window_Height_Visible(70));
  set(w, MUIA_Window_Width, MUIV_Window_Width_Visible(66));
  DoMethod(fn_find, MUIM_Notify, MUIA_String_Contents, MUIV_EveryTime, (ULONG)app, 2, MUIM_Application_ReturnID, ID_FUNCGO);
  DoMethod(fn_cat, MUIM_Notify, MUIA_Cycle_Active, MUIV_EveryTime, (ULONG)app, 2, MUIM_Application_ReturnID, ID_FUNCCAT);
  DoMethod(fn_list, MUIM_Notify, MUIA_List_Active, MUIV_EveryTime, (ULONG)app, 2, MUIM_Application_ReturnID, ID_FUNCPICK);
  DoMethod(fn_list, MUIM_Notify, MUIA_Listview_DoubleClick, TRUE, (ULONG)app, 2, MUIM_Application_ReturnID, ID_DLGOK);
  fn_fill();
  if (run_modal(w, ends, fn_each) == ID_DLGOK) {
    LONG a = (LONG)xget(fn_list, MUIA_List_Active);
    if (a >= 0 && a < fn_nshown) {
      char t[64];
      if (xget(grid, MUIA_Grid_Editing)) { snprintf(t, sizeof t, "%s()", fn_shown[a]); DoMethod(grid, MUIM_Grid_Insert, (ULONG)t, 1); }
      else { snprintf(t, sizeof t, "=%s()", fn_shown[a]); DoMethod(grid, MUIM_Grid_Edit, (ULONG)t, 1); DoMethod(grid, MUIM_Grid_Insert, (ULONG)"", 1); }
    }
  }
  MUI_DisposeObject(w);
}

/* ------------------------------------------------------------------ names */
static char nm_lines[200][160]; static const char *nm_ptrs[201]; static int nm_n;
static Object *nm_list;
static void nm_fill(void)
{
  int i;
  nm_n = 0;
  for (i = 0; i < book->nnames && nm_n < 200; i++) {
    char a[80], b[200];
    utf8_to_latin1(book->names[i].name, (u32)strlen(book->names[i].name), a, sizeof a);
    utf8_to_latin1(book->names[i].formula_text ? book->names[i].formula_text : "", book->names[i].formula_text ? (u32)strlen(book->names[i].formula_text) : 0, b, sizeof b);
    snprintf(nm_lines[nm_n], sizeof nm_lines[0], "%s  =  %s%s", a, b, book->names[i].sheet != SHEET_SELF ? tr("  (one sheet)") : "");
    nm_ptrs[nm_n] = nm_lines[nm_n]; nm_n++;
  }
  nm_ptrs[nm_n] = NULL;
  DoMethod(nm_list, MUIM_List_Clear);
  DoMethod(nm_list, MUIM_List_Insert, (ULONG)nm_ptrs, nm_n, MUIV_List_Insert_Bottom);
}
static void nm_each(ULONG id)
{
  LONG a = (LONG)xget(nm_list, MUIA_List_Active);
  if (id == ID_NAMEADD) { dlg_define_name(); nm_fill(); }
  else if (id == ID_NAMEDEL && a >= 0 && a < book->nnames) { book_delete_name(book, (int)a); nm_fill(); refresh_all(); }
}
void dlg_names(void)
{
  static const ULONG ends[] = { ID_DLGOK, 0 };
  Object *w, *add, *del;
  w = dialog_window("Name Manager", VGroup,
        Child, ListviewObject, MUIA_Listview_List, nm_list = ListObject, InputListFrame, End, MUIA_CycleChain, 1, End,
        Child, HGroup, Child, add = QButton(tr("New...")), Child, del = QButton(tr("Delete")), Child, HSpace(0), End,
      End, "Close");
  if (!w) return;
  set(w, MUIA_Window_Width, MUIV_Window_Width_Visible(50));
  DoMethod(add, MUIM_Notify, MUIA_Pressed, FALSE, (ULONG)app, 2, MUIM_Application_ReturnID, ID_NAMEADD);
  DoMethod(del, MUIM_Notify, MUIA_Pressed, FALSE, (ULONG)app, 2, MUIM_Application_ReturnID, ID_NAMEDEL);
  nm_fill();
  run_modal(w, ends, nm_each);
  MUI_DisposeObject(w);
}
void dlg_define_name(void)
{
  static const ULONG ends[] = { ID_DLGOK, 0 };
  Object *w, *name, *ref;
  char r[300], ref1[24], ref2[24], l1[300];
  Rect a = cur_sel();
  ref_text(a.r1, a.c1, 1, 1, ref1);
  if (a.r1 != a.r2 || a.c1 != a.c2) { ref_text(a.r2, a.c2, 1, 1, ref2); snprintf(r, sizeof r, "'%s'!%s:%s", cur_sheet()->name, ref1, ref2); }
  else snprintf(r, sizeof r, "'%s'!%s", cur_sheet()->name, ref1);
  utf8_to_latin1(r, (u32)strlen(r), l1, sizeof l1);
  w = dialog_window("Define Name", ColGroup(2),
        Child, Label1(tr("Name:")), Child, name = str_obj("", 60),
        Child, Label1(tr("Refers to:")), Child, ref = str_obj(l1, 290),
      End, NULL);
  if (!w) return;
  set(w, MUIA_Window_ActiveObject, (ULONG)name);
  for (;;) {
    char un[200], ur[900];
    if (run_modal(w, ends, NULL) != ID_DLGOK) break;
    latin1_utf8(sget(name), un, sizeof un); latin1_utf8(sget(ref), ur, sizeof ur);
    if (book_define_name(book, un, ur, SHEET_SELF)) break;
    ask("OK", "%s", "A name starts with a letter and holds letters, digits, _ and . only,\nisn't a cell address, and refers to a cell, a range or a formula.");
  }
  MUI_DisposeObject(w);
  refresh_all();
}

/* ------------------------------------------------------------------ Sort */
void dlg_sort(void)
{
  static const ULONG ends[] = { ID_DLGOK, 0 };
  static const char *orders_en[] = { "A to Z (smallest first)", "Z to A (largest first)", NULL };
  static const char *cols[3][66], *ord[3][4];
  static char colname[64][48];
  Object *w, *key[3], *dir[3], *hdr;
  Rect a = cur_sel();
  Sheet *s = cur_sheet();
  int i, k, n, header;
  if (a.r1 == a.r2 && a.c1 == a.c2) {
    /* one cell: the block of data round it */
    a.r1 = 0; a.r2 = sheet_last_row(s); a.c1 = 0; a.c2 = sheet_last_col(s);
    if (a.r2 < 0) return;
  }
  if (a.r2 > sheet_last_row(s)) a.r2 = sheet_last_row(s);
  if (a.c2 > sheet_last_col(s)) a.c2 = sheet_last_col(s);
  n = a.c2 - a.c1 + 1 > 64 ? 64 : a.c2 - a.c1 + 1;
  /* headings: text over a row that has a number under some of it */
  header = 0;
  if (a.r2 > a.r1)
    for (i = 0; i < n; i++) {
      const Value *v = cell_value(book, cur_index(), a.r1, a.c1 + i), *u = cell_value(book, cur_index(), a.r1 + 1, a.c1 + i);
      if (v->t == V_NUM) { header = 0; break; }
      if (v->t == V_STR && u->t == V_NUM) header = 1;
    }
  for (i = 0; i < n; i++) {
    char c[8], h[64];
    col_name(a.c1 + i, c);
    grid_cell_text(book, cur_index(), a.r1, a.c1 + i, 0, h, sizeof h, (u32 *)&k);
    snprintf(colname[i], sizeof colname[i], header && h[0] ? "%s (%s)" : "%s %s", header && h[0] ? h : tr("Column"), c);
  }
  for (k = 0; k < 3; k++) {
    int j = 0;
    if (k) cols[k][j++] = tr("(none)");
    for (i = 0; i < n; i++) cols[k][j++] = colname[i];
    cols[k][j] = NULL;
    trl(orders_en, ord[k]);
  }
  w = dialog_window("Sort", VGroup,
        Child, ColGroup(3),
          Child, Label1(tr("Sort by:")), Child, key[0] = cycle_obj(cols[0], s->cur_c >= a.c1 && s->cur_c <= a.c2 ? s->cur_c - a.c1 : 0), Child, dir[0] = cycle_obj(ord[0], 0),
          Child, Label1(tr("Then by:")), Child, key[1] = cycle_obj(cols[1], 0), Child, dir[1] = cycle_obj(ord[1], 0),
          Child, Label1(tr("Then by:")), Child, key[2] = cycle_obj(cols[2], 0), Child, dir[2] = cycle_obj(ord[2], 0),
        End,
        Child, HGroup, Child, hdr = check_obj(header), Child, LLabel1(tr("My data has headers")), Child, HSpace(0), End,
      End, "Sort");
  if (!w) return;
  if (run_modal(w, ends, NULL) == ID_DLGOK) {
    SortKey keys[3];
    int nk = 0;
    for (k = 0; k < 3; k++) {
      int v = (int)xget(key[k], MUIA_Cycle_Active) - (k ? 1 : 0);
      if (v < 0) continue;
      keys[nk].col = a.c1 + v; keys[nk].desc = (int)xget(dir[k], MUIA_Cycle_Active); nk++;
    }
    if (nk) book_sort(book, cur_index(), a, keys, nk, xget(hdr, MUIA_Selected) != 0);
    refresh_all();
  }
  MUI_DisposeObject(w);
}

/* ------------------------------------------------------------------ conditional formatting */
static const char *const cf_presets[] = { "Light Red Fill with Dark Red Text", "Yellow Fill with Dark Yellow Text", "Green Fill with Dark Green Text",
                                          "Light Red Fill", "Red Text", "Bold Text", "Custom Format...", NULL };
static const ULONG cf_pfill[] = { 0xFFC7CE, 0xFFEB9C, 0xC6EFCE, 0xFFC7CE, 0, 0 };
static const ULONG cf_pcolor[] = { 0x9C0006, 0x9C5700, 0x006100, COLOR_AUTO, 0xFF0000, COLOR_AUTO };
static const char *const cf_bars[] = { "Blue", "Green", "Red", "Orange", "Purple", NULL };
static const ULONG cf_barvals[] = { 0x638EC6, 0x63C384, 0xFF555A, 0xFFB628, 0xD6007B };
static const char *const cf_scales[] = { "Green - Yellow - Red", "Red - Yellow - Green", "Green - White - Red", "Blue - White - Red", "White - Red", "Green - White", NULL };
static const ULONG cf_scalevals[][3] = { { 0x63BE7B, 0xFFEB84, 0xF8696B }, { 0xF8696B, 0xFFEB84, 0x63BE7B }, { 0x63BE7B, 0xFCFCFF, 0xF8696B },
                                         { 0x5A8AC6, 0xFCFCFF, 0xF8696B }, { 0xFCFCFF, 0xFCFCFF, 0xF8696B }, { 0x63BE7B, 0xFCFCFF, 0xFCFCFF } };
static const u8 cf_scale3[] = { 1, 1, 1, 1, 0, 0 };
/* a look from the presets (not Custom Format) */
static void cf_preset(CondRule *r, int k)
{
  if (k < 0 || k > 5) return;
  r->fill = cf_pfill[k] ? FILL_ON | cf_pfill[k] : 0; r->color = cf_pcolor[k];
  r->bold = k == 5; r->italic = r->underline = r->strike = 0;
}
/* which preset a look is (6: none of them) */
static int cf_which(const CondRule *r)
{
  int k;
  for (k = 0; k < 6; k++)
    if (r->fill == (cf_pfill[k] ? FILL_ON | cf_pfill[k] : 0) && r->color == cf_pcolor[k] && r->bold == (k == 5) && !r->italic && !r->underline && !r->strike) return k;
  return 6;
}
static void cf_bar_set(CondRule *r, int k) { r->type = CF_BAR; r->bar[0] = cf_barvals[k]; }
static void cf_scale_set(CondRule *r, int k)
{
  r->type = CF_SCALE; r->three = cf_scale3[k];
  r->bar[0] = cf_scalevals[k][0]; r->bar[1] = cf_scalevals[k][1]; r->bar[2] = cf_scalevals[k][2];
}
/* Custom Format: the fill, the font's colour and styles; 1 if OK */
static struct { Object *fill, *color; ULONG custfill, custcolor; } cfc;
static void cfc_each(ULONG id)
{
  if (id == ID_DLG3 && xget(cfc.color, MUIA_Cycle_Active) == 17) { LONG c = ask_color(cfc.custcolor); if (c >= 0) cfc.custcolor = (ULONG)c; }
  if (id == ID_DLG4 && xget(cfc.fill, MUIA_Cycle_Active) == 14) { LONG c = ask_color(cfc.custfill); if (c >= 0) cfc.custfill = (ULONG)c; }
}
static int cf_custom(CondRule *r)
{
  static const ULONG ends[] = { ID_DLGOK, 0 };
  static const char *colors[20], *fills[18];
  Object *w, *b, *i, *u, *st;
  int ok;
  trl(fc_colors, colors); trl(fc_fills, fills);
  cfc.custcolor = r->color == COLOR_AUTO ? 0 : r->color; cfc.custfill = r->fill & 0xFFFFFF;
  w = dialog_window("Format Cells", VGroup,
        Child, ColGroup(2),
          Child, Label1(tr("Fill:")), Child, cfc.fill = cycle_obj(fills, r->fill ? color_index(r->fill & 0xFFFFFF, fc_fillvals, 15) : 0),
          Child, Label1(tr("Font colour:")), Child, cfc.color = cycle_obj(colors, color_index(r->color, fc_colorvals, 18)),
        End,
        Child, ColGroup(4),
          Child, b = check_obj(r->bold), Child, LLabel1(tr("Bold")), Child, i = check_obj(r->italic), Child, LLabel1(tr("Italic")),
          Child, u = check_obj(r->underline), Child, LLabel1(tr("Underline")), Child, st = check_obj(r->strike), Child, LLabel1(tr("Strikethrough")),
        End,
      End, "OK");
  if (!w) return 0;
  DoMethod(cfc.color, MUIM_Notify, MUIA_Cycle_Active, MUIV_EveryTime, (ULONG)app, 2, MUIM_Application_ReturnID, ID_DLG3);
  DoMethod(cfc.fill, MUIM_Notify, MUIA_Cycle_Active, MUIV_EveryTime, (ULONG)app, 2, MUIM_Application_ReturnID, ID_DLG4);
  ok = run_modal(w, ends, cfc_each) == ID_DLGOK;
  if (ok) {
    int f = (int)xget(cfc.fill, MUIA_Cycle_Active), c = (int)xget(cfc.color, MUIA_Cycle_Active);
    ULONG fv = f == 14 ? cfc.custfill : fc_fillvals[f], cv = c == 17 ? cfc.custcolor : fc_colorvals[c];
    r->fill = f == 0 ? 0 : FILL_ON | (fv & 0xFFFFFF);
    r->color = cv;
    r->bold = xget(b, MUIA_Selected) != 0; r->italic = xget(i, MUIA_Selected) != 0;
    r->underline = xget(u, MUIA_Selected) != 0; r->strike = xget(st, MUIA_Selected) != 0;
  }
  MUI_DisposeObject(w);
  return ok;
}
/* a value typed (Latin-1) as a rule's formula; NULL (and a message) if it won't do */
static Formula *cf_value(const char *l1)
{
  char u[600];
  Formula *f;
  latin1_utf8(l1, u, sizeof u);
  if (!(f = cond_value(book, cur_index(), u))) ask("OK", "%s", tr("That value or formula isn't right."));
  return f;
}
/* the rule's cells: the selection, whole rows and columns cut to what's used */
static void cf_areas(CondRule *r)
{
  Rect a = cur_sel();
  free(r->areas);
  if ((r->areas = (Rect *)malloc(sizeof(Rect)))) { r->areas[0] = a; r->nareas = 1; }
}

/* the quick ones: Greater Than... to Below Average... (the list's entries 0..11) */
static Object *cfq_look;
static CondRule cfq_rule;
static void cfq_each(ULONG id)
{
  if (id == ID_DLG1 && xget(cfq_look, MUIA_Cycle_Active) == 6) cf_custom(&cfq_rule);
}
static void cf_quick(int kind)
{
  static const ULONG ends[] = { ID_DLGOK, 0 };
  static const char *const titles[] = { "Greater Than", "Less Than", "Between", "Equal To", "Text that Contains", "Duplicate Values",
                                        "Top 10 Items", "Top 10%", "Bottom 10 Items", "Bottom 10%", "Above Average", "Below Average" };
  static const char *const prompts[] = { "Format cells that are GREATER THAN:", "Format cells that are LESS THAN:", "Format cells that are BETWEEN:",
                                         "Format cells that are EQUAL TO:", "Format cells that contain the text:", "Format cells that contain:",
                                         "Format cells that rank in the TOP:", "Format cells that rank in the TOP:", "Format cells that rank in the BOTTOM:",
                                         "Format cells that rank in the BOTTOM:", "Format cells that are ABOVE AVERAGE", "Format cells that are BELOW AVERAGE" };
  static const char *dupl_en[] = { "Duplicate", "Unique", NULL };
  static const char *presets[10], *dupl[4];
  Object *w, *v1 = NULL, *v2 = NULL, *dup = NULL, *row;
  char init[200] = "";
  CondRule *r = &cfq_rule;
  int ok;
  trl(cf_presets, presets); trl(dupl_en, dupl);
  cond_rule_init(r);
  cf_preset(r, 0);
  if (kind <= 4) grid_edit_text(book, cur_index(), cur_sheet()->cur_r, cur_sheet()->cur_c, init, sizeof init);
  if (init[0] == '=') init[0] = 0;
  if (kind >= 6 && kind <= 9) strcpy(init, "10");
  if (kind == 2) row = HGroup, Child, v1 = str_obj(init, 190), Child, Label1(tr("and")), Child, v2 = str_obj("", 190), End;
  else if (kind == 5) row = HGroup, Child, dup = cycle_obj(dupl, 0), Child, LLabel1(tr("values")), Child, HSpace(0), End;
  else if (kind >= 10) row = HSpace(0);
  else if (kind == 7 || kind == 9) row = HGroup, Child, v1 = str_obj(init, 10), Child, LLabel1("%"), Child, HSpace(0), End;
  else row = HGroup, Child, v1 = str_obj(init, 190), End;
  w = dialog_window(titles[kind], VGroup,
        Child, LLabel1(tr(prompts[kind])),
        Child, row,
        Child, HGroup, Child, Label1(tr("with")), Child, cfq_look = cycle_obj(presets, 0), End,
      End, "OK");
  if (!w) return;
  set(w, MUIA_Window_Width, MUIV_Window_Width_Visible(40));
  DoMethod(cfq_look, MUIM_Notify, MUIA_Cycle_Active, MUIV_EveryTime, (ULONG)app, 2, MUIM_Application_ReturnID, ID_DLG1);
  if (v1) set(w, MUIA_Window_ActiveObject, (ULONG)v1);
  ok = run_modal(w, ends, cfq_each) == ID_DLGOK;
  if (ok) {
    int look = (int)xget(cfq_look, MUIA_Cycle_Active);
    if (look < 6) cf_preset(r, look);
    cf_areas(r);
    switch (kind) {
    case 0: case 1: case 3:
      r->type = CF_CELL; r->op = kind == 0 ? CO_GT : kind == 1 ? CO_LT : CO_EQ;
      ok = (r->f1 = cf_value(sget(v1))) != NULL;
      break;
    case 2:
      r->type = CF_CELL; r->op = CO_BETWEEN;
      ok = (r->f1 = cf_value(sget(v1))) != NULL && (r->f2 = cf_value(sget(v2))) != NULL;
      break;
    case 4: { char u[600]; latin1_utf8(sget(v1), u, sizeof u); r->type = CF_TEXT; r->text = str_dup(u); break; }
    case 5: r->type = xget(dup, MUIA_Cycle_Active) ? CF_UNIQUE : CF_DUP; break;
    case 6: case 7: case 8: case 9:
      r->type = CF_TOP; r->bottom = kind >= 8; r->percent = kind == 7 || kind == 9;
      r->rank = atoi(sget(v1));
      if (r->rank < 1) r->rank = 1;
      if (r->percent && r->rank > 100) r->rank = 100;
      break;
    default: r->type = CF_AVERAGE; r->below = kind == 11; break;
    }
    if (ok) book_cond_add(book, cur_index(), r, 0);
    refresh_all();
  }
  cond_rule_free(r);
  MUI_DisposeObject(w);
}

/* New Formatting Rule / Edit Formatting Rule: every kind of rule */
static const char *const cft_names[] = { "Cell value is", "Text that contains", "Text that doesn't contain", "Text that begins with", "Text that ends with",
                                         "Duplicate values", "Unique values", "Top", "Bottom", "Above average", "Below average", "Blank cells",
                                         "Cells that aren't blank", "Errors", "No errors", "Formula is", "Data bar", "Colour scale", NULL };
static int cft_of(const CondRule *r)
{
  switch (r->type) {
  case CF_CELL: return 0; case CF_TEXT: return 1; case CF_NOTTEXT: return 2; case CF_BEGINS: return 3; case CF_ENDS: return 4;
  case CF_DUP: return 5; case CF_UNIQUE: return 6; case CF_TOP: return r->bottom ? 8 : 7; case CF_AVERAGE: return r->below ? 10 : 9;
  case CF_BLANKS: return 11; case CF_NOBLANKS: return 12; case CF_ERRORS: return 13; case CF_NOERRORS: return 14; case CF_EXPR: return 15;
  case CF_BAR: return 16; default: return 17;
  }
}
static struct { Object *type, *op, *v1, *v2, *pct, *look, *bar, *scale, *areas, *stop; CondRule r; } cfr;
/* the parts that don't apply to the kind of rule chosen greyed out */
static void cfr_greys(void)
{
  int t = (int)xget(cfr.type, MUIA_Cycle_Active), op = (int)xget(cfr.op, MUIA_Cycle_Active);
  int v1 = t <= 4 || t == 7 || t == 8 || t == 15, v2 = t == 0 && op <= 1;
  set(cfr.op, MUIA_Disabled, t != 0);
  set(cfr.v1, MUIA_Disabled, !v1);
  set(cfr.v2, MUIA_Disabled, !v2);
  set(cfr.pct, MUIA_Disabled, t != 7 && t != 8);
  set(cfr.look, MUIA_Disabled, t >= 16);
  set(cfr.bar, MUIA_Disabled, t != 16);
  set(cfr.scale, MUIA_Disabled, t != 17);
}
static void cfr_each(ULONG id)
{
  if (id == ID_DLG1) cfr_greys();
  if (id == ID_DLG2 && xget(cfr.look, MUIA_Cycle_Active) == 6) cf_custom(&cfr.r);
}
/* rule i of the sheet edited, or (i < 0) a new one for the selection; 1 if changed */
static int cf_rule(int i)
{
  static const ULONG ends[] = { ID_DLGOK, 0 };
  static const char *const ops_en[] = { "between", "not between", "equal to", "not equal to", "greater than", "less than",
                                        "greater than or equal to", "less than or equal to", NULL };
  static const char *types[20], *ops[10], *presets[10], *bars[8], *scales[8];
  Sheet *s = cur_sheet();
  Object *w;
  char v1[600] = "", v2[600] = "", ar[600] = "", u[600];
  int t, ok, bar = 0, scale = 0, k;
  CondRule *r = &cfr.r;
  trl(cft_names, types); trl(ops_en, ops); trl(cf_presets, presets); trl(cf_bars, bars); trl(cf_scales, scales);
  if (i >= 0 && i < s->ncfs) cond_rule_copy(r, &s->cfs[i]);
  else { cond_rule_init(r); cf_preset(r, 0); r->op = CO_GT; cf_areas(r); }
  t = cft_of(r);
  /* what the boxes show to begin with */
  if (r->type == CF_CELL || r->type == CF_EXPR) { cond_value_text(book, cur_index(), r->f1, u, sizeof u); utf8_to_latin1(u, (u32)strlen(u), v1, sizeof v1); }
  if (r->type == CF_EXPR && v1[0] != '=') { memmove(v1 + 1, v1, strlen(v1) + 1); v1[0] = '='; }
  if (r->type == CF_CELL) { cond_value_text(book, cur_index(), r->f2, u, sizeof u); utf8_to_latin1(u, (u32)strlen(u), v2, sizeof v2); }
  if (r->text) utf8_to_latin1(r->text, (u32)strlen(r->text), v1, sizeof v1);
  if (r->type == CF_TOP) sprintf(v1, "%ld", (long)r->rank);
  for (k = 0; k < 5; k++) if (r->bar[0] == cf_barvals[k]) bar = k;
  for (k = 0; k < 6; k++) if (r->bar[0] == cf_scalevals[k][0] && r->bar[2] == cf_scalevals[k][2] && r->three == cf_scale3[k]) scale = k;
  cond_areas_text(r, ar, sizeof ar);
  w = dialog_window(i >= 0 ? "Edit Formatting Rule" : "New Formatting Rule", VGroup,
        Child, ColGroup(2),
          Child, Label1(tr("Format cells:")), Child, cfr.type = cycle_obj(types, t),
          Child, Label1(tr("Comparison:")), Child, cfr.op = cycle_obj(ops, r->op < CO_N ? r->op : CO_GT),
          Child, Label1(tr("Value:")), Child, HGroup, Child, cfr.v1 = str_obj(v1, 590), Child, cfr.pct = check_obj(r->percent), Child, LLabel1(tr("%")), End,
          Child, Label1(tr("And:")), Child, cfr.v2 = str_obj(v2, 590),
          Child, Label1(tr("Look:")), Child, cfr.look = cycle_obj(presets, cf_which(r)),
          Child, Label1(tr("Bar colour:")), Child, cfr.bar = cycle_obj(bars, bar),
          Child, Label1(tr("Colour scale:")), Child, cfr.scale = cycle_obj(scales, scale),
          Child, Label1(tr("Applies to:")), Child, cfr.areas = str_obj(ar, 590),
        End,
        Child, HGroup, Child, cfr.stop = check_obj(r->stop), Child, LLabel1(tr("Stop if true (later rules don't apply)")), Child, HSpace(0), End,
      End, "OK");
  if (!w) { cond_rule_free(r); return 0; }
  set(w, MUIA_Window_Width, MUIV_Window_Width_Visible(45));
  DoMethod(cfr.type, MUIM_Notify, MUIA_Cycle_Active, MUIV_EveryTime, (ULONG)app, 2, MUIM_Application_ReturnID, ID_DLG1);
  DoMethod(cfr.op, MUIM_Notify, MUIA_Cycle_Active, MUIV_EveryTime, (ULONG)app, 2, MUIM_Application_ReturnID, ID_DLG1);
  DoMethod(cfr.look, MUIM_Notify, MUIA_Cycle_Active, MUIV_EveryTime, (ULONG)app, 2, MUIM_Application_ReturnID, ID_DLG2);
  cfr_greys();
  ok = run_modal(w, ends, cfr_each) == ID_DLGOK;
  if (ok) {
    static const u8 tmap[] = { CF_CELL, CF_TEXT, CF_NOTTEXT, CF_BEGINS, CF_ENDS, CF_DUP, CF_UNIQUE, CF_TOP, CF_TOP, CF_AVERAGE, CF_AVERAGE,
                               CF_BLANKS, CF_NOBLANKS, CF_ERRORS, CF_NOERRORS, CF_EXPR, CF_BAR, CF_SCALE };
    int look = (int)xget(cfr.look, MUIA_Cycle_Active);
    t = (int)xget(cfr.type, MUIA_Cycle_Active);
    r->type = tmap[t];
    r->op = (u8)xget(cfr.op, MUIA_Cycle_Active);
    r->stop = xget(cfr.stop, MUIA_Selected) != 0;
    if (look < 6) cf_preset(r, look);
    formula_free(r->f1); formula_free(r->f2); r->f1 = r->f2 = NULL;
    free(r->text); r->text = NULL;
    latin1_utf8(sget(cfr.areas), u, sizeof u);
    if (!cond_parse_areas(book, u, r)) { ask("OK", "%s", tr("The cells it applies to aren't right (A1:B10, or several separated by spaces).")); ok = 0; }
    else switch (r->type) {
    case CF_CELL:
      ok = (r->f1 = cf_value(sget(cfr.v1))) != NULL;
      if (ok && r->op <= CO_NOTBETWEEN) ok = (r->f2 = cf_value(sget(cfr.v2))) != NULL;
      break;
    case CF_EXPR: {
      char f[602];
      const char *e = sget(cfr.v1);
      snprintf(f, sizeof f, "%s%s", e[0] == '=' ? "" : "=", e);
      ok = (r->f1 = cf_value(f)) != NULL;
      break; }
    case CF_TEXT: case CF_NOTTEXT: case CF_BEGINS: case CF_ENDS: latin1_utf8(sget(cfr.v1), u, sizeof u); r->text = str_dup(u); break;
    case CF_TOP:
      r->bottom = t == 8; r->percent = xget(cfr.pct, MUIA_Selected) != 0;
      r->rank = atoi(sget(cfr.v1));
      if (r->rank < 1) r->rank = 1;
      break;
    case CF_AVERAGE: r->below = t == 10; break;
    case CF_BAR: cf_bar_set(r, (int)xget(cfr.bar, MUIA_Cycle_Active)); break;
    case CF_SCALE: cf_scale_set(r, (int)xget(cfr.scale, MUIA_Cycle_Active)); break;
    }
    if (ok) { if (i >= 0) book_cond_replace(book, cur_index(), i, r); else book_cond_add(book, cur_index(), r, 0); }
    refresh_all();
  }
  cond_rule_free(r);
  MUI_DisposeObject(w);
  return ok;
}

/* Manage Rules: the sheet's rules, first first, to edit, delete or put in order */
static Object *cfm_list;
static char cfm_lines[100][160];
static const char *cfm_ptrs[101];
static void cfm_fill(int active)
{
  Sheet *s = cur_sheet();
  int i, n = 0;
  for (i = 0; i < s->ncfs && n < 100; i++) {
    char d[200], a[200], l1[160];
    cond_describe(book, cur_index(), &s->cfs[i], d, sizeof d);
    cond_areas_text(&s->cfs[i], a, sizeof a);
    snprintf(cfm_lines[n], sizeof cfm_lines[n], "%s   [%s]", d, a);
    utf8_to_latin1(cfm_lines[n], (u32)strlen(cfm_lines[n]), l1, sizeof l1);
    strcpy(cfm_lines[n], l1);
    cfm_ptrs[n] = cfm_lines[n]; n++;
  }
  cfm_ptrs[n] = NULL;
  DoMethod(cfm_list, MUIM_List_Clear);
  DoMethod(cfm_list, MUIM_List_Insert, (ULONG)cfm_ptrs, n, MUIV_List_Insert_Bottom);
  if (n) set(cfm_list, MUIA_List_Active, active < 0 ? 0 : active >= n ? n - 1 : active);
}
static void cfm_each(ULONG id)
{
  LONG a = (LONG)xget(cfm_list, MUIA_List_Active);
  int n = cur_sheet()->ncfs;
  if (a < 0 || a >= n) a = -1;
  switch (id) {
  case ID_DLG1: if (cf_rule(-1)) a = 0; break;
  case ID_DLG2: if (a >= 0) cf_rule((int)a); break;
  case ID_DLG3: if (a >= 0) { book_cond_delete(book, cur_index(), (int)a); refresh_all(); } break;
  case ID_DLG4: if (a > 0) { book_cond_move(book, cur_index(), (int)a, (int)a - 1); a--; refresh_all(); } break;
  case ID_DLG5: if (a >= 0 && a + 1 < n) { book_cond_move(book, cur_index(), (int)a, (int)a + 1); a++; refresh_all(); } break;
  default: return;
  }
  cfm_fill((int)a);
}
static void cf_manage(void)
{
  static const ULONG ends[] = { ID_DLGOK, 0 };
  Object *w, *nw, *ed, *del, *up, *dn;
  w = dialog_window("Conditional Formatting Rules", VGroup,
        Child, LLabel1(tr("Rules on this sheet (the first one wins where they disagree):")),
        Child, ListviewObject, MUIA_Listview_List, cfm_list = ListObject, InputListFrame, End, MUIA_CycleChain, 1, MUIA_Listview_DoubleClick, TRUE, End,
        Child, HGroup, Child, nw = QButton(tr("New Rule...")), Child, ed = QButton(tr("Edit Rule...")), Child, del = QButton(tr("Delete Rule")),
          Child, up = QButton(tr("Move Up")), Child, dn = QButton(tr("Move Down")), End,
      End, "Close");
  if (!w) return;
  set(w, MUIA_Window_Width, MUIV_Window_Width_Visible(55));
  DoMethod(nw, MUIM_Notify, MUIA_Pressed, FALSE, (ULONG)app, 2, MUIM_Application_ReturnID, ID_DLG1);
  DoMethod(ed, MUIM_Notify, MUIA_Pressed, FALSE, (ULONG)app, 2, MUIM_Application_ReturnID, ID_DLG2);
  DoMethod(cfm_list, MUIM_Notify, MUIA_Listview_DoubleClick, TRUE, (ULONG)app, 2, MUIM_Application_ReturnID, ID_DLG2);
  DoMethod(del, MUIM_Notify, MUIA_Pressed, FALSE, (ULONG)app, 2, MUIM_Application_ReturnID, ID_DLG3);
  DoMethod(up, MUIM_Notify, MUIA_Pressed, FALSE, (ULONG)app, 2, MUIM_Application_ReturnID, ID_DLG4);
  DoMethod(dn, MUIM_Notify, MUIA_Pressed, FALSE, (ULONG)app, 2, MUIM_Application_ReturnID, ID_DLG5);
  cfm_fill(0);
  run_modal(w, ends, cfm_each);
  MUI_DisposeObject(w);
}
/* (app.h) an entry of the Conditional Formatting list */
void cond_choice(int v)
{
  CondRule r;
  if (v >= 0 && v <= 11) { cf_quick(v); return; }
  if (v >= 12 && v <= 22) {
    /* a data bar or colour scale straight on the selection */
    cond_rule_init(&r);
    cf_areas(&r);
    if (v <= 16) cf_bar_set(&r, v - 12); else cf_scale_set(&r, v - 17);
    book_cond_add(book, cur_index(), &r, 0);
    cond_rule_free(&r);
    refresh_all();
    return;
  }
  if (v == 23) { cf_rule(-1); return; }
  if (v == 24) { Rect a = cur_sel(); if (!book_cond_clear(book, cur_index(), &a)) ask("OK", "%s", tr("No rules cover the selected cells.")); refresh_all(); return; }
  if (v == 25) { book_cond_clear(book, cur_index(), NULL); refresh_all(); return; }
  if (v == 26) cf_manage();
}

/* ------------------------------------------------------------------ a note on the active cell */
void dlg_note(void)
{
  static const ULONG ends[] = { ID_DLGOK, 0 };
  Sheet *s = cur_sheet();
  const Note *n = sheet_note(s, s->cur_r, s->cur_c);
  char text[1000] = "", who[120] = "", ref[16], title[160];
  Object *w, *str;
  if (n) {
    utf8_to_latin1(n->text ? n->text : "", n->text ? (u32)strlen(n->text) : 0, text, sizeof text);
    utf8_to_latin1(n->author ? n->author : "", n->author ? (u32)strlen(n->author) : 0, who, sizeof who);
  }
  ref_text(s->cur_r, s->cur_c, 0, 0, ref);
  snprintf(title, sizeof title, n && who[0] ? tr("Note on %s (by %s)") : tr("Note on %s"), ref, who);
  w = dialog_window(n ? "Edit Note" : "New Note", VGroup,
        Child, LLabel1(title),
        Child, str = str_obj(text, 990),
      End, "OK");
  if (!w) return;
  set(w, MUIA_Window_Width, MUIV_Window_Width_Visible(40));
  DoMethod(str, MUIM_Notify, MUIA_String_Acknowledge, MUIV_EveryTime, (ULONG)app, 2, MUIM_Application_ReturnID, ID_DLGOK);
  set(w, MUIA_Window_ActiveObject, (ULONG)str);
  if (run_modal(w, ends, NULL) == ID_DLGOK) {
    char u[3000], a[200];
    latin1_utf8(sget(str), u, sizeof u);
    latin1_utf8(username, a, sizeof a);
    book_set_note(book, cur_index(), s->cur_r, s->cur_c, u, a);
    refresh_all();
  }
  MUI_DisposeObject(w);
}

/* ------------------------------------------------------------------ a link on the active cell */
void dlg_link(void)
{
  static const ULONG ends[] = { ID_DLGOK, ID_DLG1, 0 };
  static const char *sheets[MAXSHEETS + 2];
  static char sheetl1[MAXSHEETS][40];
  Sheet *s = cur_sheet();
  const Link *l = sheet_link(s, s->cur_r, s->cur_c);
  char shown[600] = "", addr[600] = "", tip[300] = "", cellref[64] = "A1";
  int i, place = 0, ns = 0;
  Object *w, *o_text, *o_addr, *o_sheet, *o_cell, *o_tip, *o_rem;
  ULONG id;
  grid_cell_text(book, cur_index(), s->cur_r, s->cur_c, 0, shown, sizeof shown, (u32 *)&i);
  sheets[ns++] = tr("(a web page or mail address)");
  for (i = 0; i < book->nsh && ns < MAXSHEETS + 1; i++) {
    utf8_to_latin1(book->sh[i]->name, (u32)strlen(book->sh[i]->name), sheetl1[i], sizeof sheetl1[i]);
    sheets[ns++] = sheetl1[i];
  }
  sheets[ns] = NULL;
  if (l && l->target) {
    if (l->target[0] == '#') {
      /* a place: its sheet and cell */
      u16 sh = SHEET_SELF; Rect r; u8 fl;
      if (parse_ref(book, l->target + 1, &sh, &r, &fl)) { place = (sh == SHEET_SELF ? cur_index() : sh) + 1; rect_text(r, cellref); }
    } else utf8_to_latin1(l->target, (u32)strlen(l->target), addr, sizeof addr);
    utf8_to_latin1(l->tip ? l->tip : "", l->tip ? (u32)strlen(l->tip) : 0, tip, sizeof tip);
  }
  w = dialog_window(l ? "Edit Link" : "Insert Link", VGroup,
        Child, ColGroup(2),
          Child, Label1(tr("Text to show:")), Child, o_text = str_obj(shown, 590),
          Child, Label1(tr("Address:")), Child, o_addr = str_obj(addr, 590),
          Child, Label1(tr("Or a place:")), Child, HGroup, Child, o_sheet = cycle_obj(sheets, place), Child, Label1(tr("cell")), Child, o_cell = str_obj(cellref, 60), End,
          Child, Label1(tr("Tip:")), Child, o_tip = str_obj(tip, 290),
        End,
        Child, l ? HGroup, Child, o_rem = QButton(tr("Remove Link")), Child, HSpace(0), End : (o_rem = NULL, HSpace(0)),
      End, "OK");
  if (!w) return;
  set(w, MUIA_Window_Width, MUIV_Window_Width_Visible(45));
  if (o_rem) DoMethod(o_rem, MUIM_Notify, MUIA_Pressed, FALSE, (ULONG)app, 2, MUIM_Application_ReturnID, ID_DLG1);
  set(w, MUIA_Window_ActiveObject, (ULONG)o_addr);
  id = run_modal(w, ends, NULL);
  if (id == ID_DLG1) { book_set_link(book, cur_index(), s->cur_r, s->cur_c, NULL, NULL); refresh_all(); }
  else if (id == ID_DLGOK) {
    char target[700], u[700], ut[400], ux[700];
    int sh = (int)xget(o_sheet, MUIA_Cycle_Active);
    const char *a = sget(o_addr);
    target[0] = 0;
    if (sh > 0) {
      /* a place: 'Sheet name'!A1 */
      char nm[80];
      latin1_utf8(sget(o_cell), u, sizeof u);
      snprintf(nm, sizeof nm, "%s", book->sh[sh - 1]->name);
      snprintf(target, sizeof target, "#'%s'!%s", nm, u[0] ? u : "A1");
    } else if (a[0]) {
      /* a bare address gets its kind: mail for name@place, the web otherwise */
      char full[700];
      if (strstr(a, "://") || !strncmp(a, "mailto:", 7)) snprintf(full, sizeof full, "%s", a);
      else if (strchr(a, '@')) snprintf(full, sizeof full, "mailto:%s", a);
      else snprintf(full, sizeof full, "http://%s", a);
      latin1_utf8(full, target, sizeof target);
    }
    if (target[0]) {
      latin1_utf8(sget(o_tip), ut, sizeof ut);
      book_begin(book);
      /* the text shown, if it was changed (or the cell was empty: the address) */
      if (strcmp(sget(o_text), shown) || !shown[0]) {
        const char *tx = sget(o_text);
        latin1_utf8(tx[0] ? tx : (sh > 0 ? sget(o_cell) : a), ux, sizeof ux);
        book_input(book, cur_index(), s->cur_r, s->cur_c, ux, &i);
      }
      book_set_link(book, cur_index(), s->cur_r, s->cur_c, target, ut);
      book_end(book);
      /* the look of a link: blue and underlined */
      apply_format(FX_COLOR, 0x0563C1, NULL);
      apply_format(FX_UNDERLINE, 1, NULL);
    }
    refresh_all();
  }
  MUI_DisposeObject(w);
}

/* ------------------------------------------------------------------ an AutoFilter button's list */
/* the column's values each with a tick box (ticklist.c), Sort A to Z / Z to A, Select All */
#define FMAX 2000
static Object *fl_tick;
static void fl_each(ULONG id)
{
  if (id == ID_DLG3 || id == ID_DLG4) set(fl_tick, MUIA_Tick_All, id == ID_DLG3);
}
void dlg_filter(i32 col)
{
  static const ULONG ends[] = { ID_DLGOK, ID_DLG1, ID_DLG2, 0 };
  Sheet *s = cur_sheet();
  const FilterCol *fc0 = sheet_filter_col(s, col);
  char **vals, title[80], c[8], h[64];
  char (*l1)[64];
  const char **lab;
  UBYTE *on;
  int n, nv, blanks, i, k, total;
  u32 colour;
  Object *w, *az, *za, *all, *none, *sc;
  ULONG id;
  if (s->filter.r1 < 0) return;
  nv = n = book_filter_values(book, cur_index(), col, &vals, &blanks);
  if (n > FMAX) n = FMAX;
  total = n + (blanks ? 1 : 0);
  l1 = calloc((size_t)total + 1, sizeof *l1);
  lab = (const char **)calloc((size_t)total + 1, sizeof *lab);
  on = (UBYTE *)calloc((size_t)total + 1, 1);
  if (!l1 || !lab || !on) goto out0;
  /* ticked: everything when the column isn't filtered, else what its filter lets through */
  for (i = 0; i < total; i++) {
    if (i < n) utf8_to_latin1(vals[i], (u32)strlen(vals[i]), l1[i], sizeof l1[i]);
    else strcpy(l1[i], tr("(Blanks)"));
    lab[i] = l1[i];
    on[i] = 1;
    if (fc0) {
      on[i] = 0;
      if (i == n) on[i] = fc0->blanks;
      else for (k = 0; k < fc0->n && !on[i]; k++) on[i] = str_ieq(fc0->vals[k], vals[i]) != 0;
    }
  }
  col_name(col, c);
  grid_cell_text(book, cur_index(), s->filter.r1, col, 0, h, sizeof h, &colour);
  snprintf(title, sizeof title, "%s (%s)", h[0] ? h : tr("Column"), c);
  sc = NewObject(flatscroll_class()->mcc_Class, NULL, TAG_DONE);
  w = dialog_window("Filter", VGroup,
        Child, TextObject, MUIA_Text_Contents, (ULONG)title, MUIA_Text_PreParse, (ULONG)"\33b", End,
        Child, HGroup, Child, az = QButton(tr("Sort A to Z")), Child, za = QButton(tr("Sort Z to A")), End,
        Child, LLabel1(tr("Show rows whose value is ticked:")),
        Child, HGroup, MUIA_Group_Spacing, 0,
          Child, fl_tick = NewObject(ticklist_class()->mcc_Class, NULL, MUIA_Tick_Labels, (ULONG)lab, MUIA_Tick_States, (ULONG)on,
                                     MUIA_Tick_Count, total, MUIA_Tick_Prop, (ULONG)sc, TAG_DONE),
          Child, sc,
        End,
        Child, HGroup, Child, all = QButton(tr("Select All")), Child, none = QButton(tr("Clear")), Child, HSpace(0), End,
      End, "OK");
  if (!w) goto out0;
  DoMethod(sc, MUIM_Notify, MUIA_Prop_First, MUIV_EveryTime, (ULONG)fl_tick, 3, MUIM_Set, MUIA_Tick_First, MUIV_TriggerValue);
  DoMethod(az, MUIM_Notify, MUIA_Pressed, FALSE, (ULONG)app, 2, MUIM_Application_ReturnID, ID_DLG1);
  DoMethod(za, MUIM_Notify, MUIA_Pressed, FALSE, (ULONG)app, 2, MUIM_Application_ReturnID, ID_DLG2);
  DoMethod(all, MUIM_Notify, MUIA_Pressed, FALSE, (ULONG)app, 2, MUIM_Application_ReturnID, ID_DLG3);
  DoMethod(none, MUIM_Notify, MUIA_Pressed, FALSE, (ULONG)app, 2, MUIM_Application_ReturnID, ID_DLG4);
  id = run_modal(w, ends, fl_each);
  if (id == ID_DLG1 || id == ID_DLG2) {
    SortKey key;
    key.col = col; key.desc = id == ID_DLG2;
    book_begin(book);
    book_sort(book, cur_index(), s->filter, &key, 1, 1);
    book_filter_reapply(book, cur_index());
    book_end(book);
    refresh_all();
  } else if (id == ID_DLGOK) {
    /* every value ticked (and none beyond those listed): no filter on the column */
    const char **pick = (const char **)calloc((size_t)n + 1, sizeof *pick);
    int np = 0, bl = 0, all_on = nv == n;
    for (i = 0; i < total; i++) {
      if (!on[i]) { all_on = 0; continue; }
      if (i == n) bl = 1; else if (pick) pick[np++] = vals[i];
    }
    if (all_on) book_filter_column(book, cur_index(), col, NULL, -1, 0);
    else if (pick) book_filter_column(book, cur_index(), col, pick, np, bl);
    free(pick);
    refresh_all();
  }
  MUI_DisposeObject(w);
out0:
  free(l1); free(lab); free(on);
  for (i = 0; i < nv; i++) free(vals[i]);
  free(vals);
  fl_tick = NULL;
}

/* ------------------------------------------------------------------ Find and Replace */
static char find_what[200], find_with[200];
static int find_flags;
static Object *fd_what, *fd_with, *fd_case, *fd_whole, *fd_book, *fd_formulas, *fd_msg;
static void fd_read(void)
{
  strncpy(find_what, sget(fd_what), sizeof find_what - 1);
  if (fd_with) strncpy(find_with, sget(fd_with), sizeof find_with - 1);
  find_flags = (xget(fd_case, MUIA_Selected) ? FIND_CASE : 0) | (xget(fd_whole, MUIA_Selected) ? FIND_WHOLE : 0) |
               (xget(fd_book, MUIA_Selected) ? FIND_BOOK : 0) | (xget(fd_formulas, MUIA_Selected) ? FIND_FORMULAS : 0);
}
/* the next match from the active cell, selected; 0 (and a message) if there is none */
static int find_next(int back)
{
  char u[600];
  int sh = cur_index();
  i32 r = cur_sheet()->cur_r, c = cur_sheet()->cur_c;
  if (!find_what[0]) return 0;
  latin1_utf8(find_what, u, sizeof u);
  if (!book_find(book, &sh, &r, &c, u, find_flags | (back ? FIND_BACK : 0))) {
    if (fd_msg) set(fd_msg, MUIA_Text_Contents, (ULONG)tr("Not found."));
    else ask("OK", "Ledger couldn't find \"%s\".", find_what);
    return 0;
  }
  if (sh != cur_index()) { book->active = sh; set(grid, MUIA_Grid_Book, (ULONG)book); }
  DoMethod(grid, MUIM_Grid_Select, r, c, r, c, r, c);
  refresh_all();
  if (fd_msg) set(fd_msg, MUIA_Text_Contents, (ULONG)"");
  return 1;
}
static void fd_each(ULONG id)
{
  char u1[600], u2[600];
  fd_read();
  latin1_utf8(find_what, u1, sizeof u1); latin1_utf8(find_with, u2, sizeof u2);
  switch (id) {
  case ID_FINDGO: find_next(0); break;
  case ID_FINDPREV: find_next(1); break;
  case ID_REPLACE1:
    /* the selected match replaced, then on to the next */
    if (book_replace_cell(book, cur_index(), cur_sheet()->cur_r, cur_sheet()->cur_c, u1, u2, find_flags)) refresh_all();
    find_next(0);
    break;
  case ID_REPLACEALL: {
    static char msg[80];
    int n = book_replace_all(book, cur_index(), u1, u2, find_flags);
    refresh_all();
    snprintf(msg, sizeof msg, tr(n == 1 ? "%d cell changed." : "%d cells changed."), n);
    set(fd_msg, MUIA_Text_Contents, (ULONG)msg);
    break; }
  }
}
void dlg_find(int mode)
{
  static const ULONG ends[] = { ID_DLGOK, 0 };
  Object *w, *next, *prev, *rep = NULL, *repall = NULL, *row2, *bt;
  if (mode == 2) { if (find_what[0]) { fd_msg = NULL; find_next(0); } else dlg_find(0); return; }
  fd_with = NULL;
  row2 = mode == 1 ? HGroup, Child, Label1(tr("Replace with:")), Child, fd_with = str_obj(find_with, 190), End : HSpace(0);
  bt = mode == 1 ? HGroup, Child, prev = QButton(tr("Previous")), Child, next = QButton(tr("Find Next")), Child, rep = QButton(tr("Replace")), Child, repall = QButton(tr("Replace All")), End
                 : HGroup, Child, prev = QButton(tr("Previous")), Child, next = QButton(tr("Find Next")), Child, HSpace(0), End;
  w = dialog_window(mode == 1 ? "Replace" : "Find", VGroup,
        Child, HGroup, Child, Label1(tr("Find what:")), Child, fd_what = str_obj(find_what, 190), End,
        Child, row2,
        Child, ColGroup(4),
          Child, fd_case = check_obj(find_flags & FIND_CASE), Child, LLabel1(tr("Match case")),
          Child, fd_whole = check_obj(find_flags & FIND_WHOLE), Child, LLabel1(tr("Match entire cell")),
          Child, fd_book = check_obj(find_flags & FIND_BOOK), Child, LLabel1(tr("Every sheet")),
          Child, fd_formulas = check_obj(find_flags & FIND_FORMULAS), Child, LLabel1(tr("Look in formulas")),
        End,
        Child, bt,
        Child, fd_msg = TextObject, MUIA_Text_Contents, (ULONG)"", End,
      End, "Close");
  if (!w) return;
  DoMethod(next, MUIM_Notify, MUIA_Pressed, FALSE, (ULONG)app, 2, MUIM_Application_ReturnID, ID_FINDGO);
  DoMethod(prev, MUIM_Notify, MUIA_Pressed, FALSE, (ULONG)app, 2, MUIM_Application_ReturnID, ID_FINDPREV);
  DoMethod(fd_what, MUIM_Notify, MUIA_String_Acknowledge, MUIV_EveryTime, (ULONG)app, 2, MUIM_Application_ReturnID, ID_FINDGO);
  if (rep) DoMethod(rep, MUIM_Notify, MUIA_Pressed, FALSE, (ULONG)app, 2, MUIM_Application_ReturnID, ID_REPLACE1);
  if (repall) DoMethod(repall, MUIM_Notify, MUIA_Pressed, FALSE, (ULONG)app, 2, MUIM_Application_ReturnID, ID_REPLACEALL);
  set(w, MUIA_Window_ActiveObject, (ULONG)fd_what);
  run_modal(w, ends, fd_each);
  fd_read();
  MUI_DisposeObject(w);
  fd_msg = NULL; fd_with = NULL;
}

/* ------------------------------------------------------------------ Go To */
void dlg_goto(void)
{
  char t[80] = "";
  char u[200];
  u16 sh; Rect r; u8 fl;
  int n, i;
  if (!ask_string("Go To", "Cell, range or name:", t, sizeof t)) return;
  latin1_utf8(t, u, sizeof u);
  n = parse_ref(book, u, &sh, &r, &fl);
  if (!n || n != (int)strlen(u))
    for (i = 0, n = 0; i < book->nnames; i++)
      if (str_ieq(book->names[i].name, u) && book->names[i].formula_text) { const char *ft = book->names[i].formula_text; n = parse_ref(book, ft[0] == '=' ? ft + 1 : ft, &sh, &r, &fl); break; }
  if (!n) { ask("OK", "\"%s\" isn't a cell address or a name in this workbook.", t); return; }
  if (sh != SHEET_SELF && sh != cur_index() && sh < book->nsh) { book->active = sh; set(grid, MUIA_Grid_Book, (ULONG)book); }
  if (fl & RF_COLS) { r.r1 = 0; r.r2 = MAXROW - 1; }
  if (fl & RF_ROWS) { r.c1 = 0; r.c2 = MAXCOL - 1; }
  DoMethod(grid, MUIM_Grid_Select, r.r1, r.c1, r.r2, r.c2, r.r1, r.c1);
  refresh_all();
}

/* ------------------------------------------------------------------ Series */
void dlg_series(void)
{
  static const ULONG ends[] = { ID_DLGOK, 0 };
  static const char *dirs_en[] = { "Down the columns", "Across the rows", NULL }, *dirs[4];
  Object *w, *dir, *step, *stop;
  Rect a = cur_sel();
  trl(dirs_en, dirs);
  if (a.r1 == a.r2 && a.c1 == a.c2) { ask("OK", "%s", "Select the cells to fill first: the first one holds the starting number."); return; }
  w = dialog_window("Series", ColGroup(2),
        Child, Label1(tr("Series in:")), Child, dir = cycle_obj(dirs, a.r1 == a.r2 ? 1 : 0),
        Child, Label1(tr("Step value:")), Child, step = str_obj("1", 20),
        Child, Label1(tr("Stop value:")), Child, stop = str_obj("", 20),
      End, NULL);
  if (!w) return;
  if (run_modal(w, ends, NULL) == ID_DLGOK) {
    const char *sv = sget(stop);
    if (!book_series(book, cur_index(), a, (int)xget(dir, MUIA_Cycle_Active), atof(sget(step)), sv[0] ? atof(sv) : NAN))
      ask("OK", "%s", "The first selected cell must hold a number to start the series from.");
    refresh_all();
  }
  MUI_DisposeObject(w);
}

/* ------------------------------------------------------------------ Insert and Delete Cells */
void dlg_insert_cells(int del)
{
  int r = del ? ask("Shift cells up|Shift cells left|Entire row|Entire column|Cancel", "%s", "Delete the selected cells, and move the others how?")
              : ask("Shift cells down|Shift cells right|Entire row|Entire column|Cancel", "%s", "Insert cells here, and move the others how?");
  Rect a = cur_sel();
  switch (r) {
  case 1: if (del) book_delete_cells(book, cur_index(), a, 1); else book_insert_cells(book, cur_index(), a, 1); break;
  case 2: if (del) book_delete_cells(book, cur_index(), a, 0); else book_insert_cells(book, cur_index(), a, 0); break;
  case 3: if (del) book_delete_rows(book, cur_index(), a.r1, a.r2 - a.r1 + 1); else book_insert_rows(book, cur_index(), a.r1, a.r2 - a.r1 + 1); break;
  case 4: if (del) book_delete_cols(book, cur_index(), a.c1, a.c2 - a.c1 + 1); else book_insert_cols(book, cur_index(), a.c1, a.c2 - a.c1 + 1); break;
  default: return;
  }
  refresh_all();
}

/* ------------------------------------------------------------------ Unhide a sheet */
void dlg_unhide_sheet(void)
{
  static const ULONG ends[] = { ID_DLGOK, 0 };
  static char names[64][40]; static const char *ptrs[65]; static int idx[64];
  Object *w, *list;
  int i, n = 0;
  for (i = 0; i < book->nsh && n < 64; i++) if (book->sh[i]->hidden) { utf8_to_latin1(book->sh[i]->name, (u32)strlen(book->sh[i]->name), names[n], sizeof names[n]); ptrs[n] = names[n]; idx[n++] = i; }
  ptrs[n] = NULL;
  if (!n) { ask("OK", "%s", "No sheets are hidden."); return; }
  w = dialog_window("Unhide", VGroup, Child, TextObject, MUIA_Text_Contents, (ULONG)tr("Unhide sheet:"), End,
        Child, ListviewObject, MUIA_Listview_List, list = ListObject, InputListFrame, MUIA_List_SourceArray, (ULONG)ptrs, MUIA_List_Active, 0, End, MUIA_CycleChain, 1, End,
      End, NULL);
  if (!w) return;
  if (run_modal(w, ends, NULL) == ID_DLGOK) {
    LONG a = (LONG)xget(list, MUIA_List_Active);
    if (a >= 0 && a < n) { book->sh[idx[a]]->hidden = 0; book->active = idx[a]; book->dirty = 1; set(grid, MUIA_Grid_Book, (ULONG)book); refresh_all(); }
  }
  MUI_DisposeObject(w);
}

/* ------------------------------------------------------------------ Workbook Statistics */
void dlg_stats(void)
{
  static char msg[700];
  long cells = 0, formulas = 0, numbers = 0, texts = 0, errors = 0;
  int s, i;
  Sheet *cs = cur_sheet();
  char last[16] = "-";
  for (s = 0; s < book->nsh; s++) {
    Sheet *sh = book->sh[s];
    i32 r;
    for (r = 0; r < sh->nrows; r++) if (sh->rows[r]) for (i = 0; i < sh->rows[r]->n; i++) {
      Cell *c = &sh->rows[r]->c[i];
      const Value *v;
      if (!c->f && c->v.t == V_EMPTY) continue;
      cells++;
      if (c->f) formulas++;
      v = cell_value(book, s, r, c->col);
      if (v->t == V_NUM) numbers++; else if (v->t == V_STR) texts++; else if (v->t == V_ERR) errors++;
    }
  }
  if (sheet_last_row(cs) >= 0) ref_text(sheet_last_row(cs), sheet_last_col(cs), 0, 0, last);
  snprintf(msg, sizeof msg, "%s\n\n%s %d\n%s %ld\n%s %ld\n%s %ld\n%s %ld\n%s %ld\n%s %d\n%s %s",
           tr("Workbook statistics"), tr("Sheets:"), book->nsh, tr("Cells with data:"), cells, tr("Formulas:"), formulas,
           tr("Numbers:"), numbers, tr("Text:"), texts, tr("Errors:"), errors, tr("Defined names:"), book->nnames,
           tr("This sheet's last cell:"), last);
  ask("OK", "%s", msg);
}
