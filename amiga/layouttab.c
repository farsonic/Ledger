/*
 * layouttab.c - the Page Layout tab (themes, the page setup, scaling, sheet options), the Page
 * Setup dialog, the File page's PDF and web page export, properties and calculation options, the
 * Home tab's remaining commands (Move or Copy Sheet, Paste Special, the extra Clear, Fill and
 * Format choices), and the dispatch of every ID_LX_ command (layout.h).
 */
#include <math.h>
#include "lxpriv.h"
#include "../core/pdfout.h"
#include "../core/htmlout.h"
#include "../core/calc.h"
#include "../core/xlsx.h"

extern int ledger_date_order;
double ledger_now(void);

/* ------------------------------------------------------------------ helpers (lxpriv.h) */
ULONG lx_xget(Object *o, ULONG attr) { ULONG v = 0; if (o) get(o, attr, &v); return v; }
const char *lx_sget(Object *o) { const char *s = (const char *)lx_xget(o, MUIA_String_Contents); return s ? s : ""; }
Object *lx_str(const char *init, int max) { return StringObject, StringFrame, MUIA_String_Contents, (ULONG)init, MUIA_String_MaxLen, max, MUIA_CycleChain, 1, End; }
Object *lx_int(long v, int max)
{
  return StringObject, StringFrame, MUIA_String_Integer, v, MUIA_String_Accept, (ULONG)"0123456789", MUIA_String_MaxLen, max, MUIA_CycleChain, 1, End;
}
Object *lx_check(int on)
{
  return ImageObject, ImageButtonFrame, MUIA_InputMode, MUIV_InputMode_Toggle, MUIA_Image_Spec, MUII_CheckMark,
    MUIA_Image_FreeVert, TRUE, MUIA_Selected, on, MUIA_Background, MUII_ButtonBack, MUIA_ShowSelState, FALSE, MUIA_CycleChain, 1, End;
}
Object *lx_cycle(const char **labels, int active) { return CycleObject, MUIA_Cycle_Entries, (ULONG)labels, MUIA_Cycle_Active, active, MUIA_CycleChain, 1, End; }
const char **lx_trl(const char *const *en, const char **out)
{
  int i;
  for (i = 0; en[i]; i++) out[i] = tr(en[i]);
  out[i] = NULL;
  return out;
}
void lx_latin1_utf8(const char *s, char *u, size_t cap)
{
  size_t o = 0;
  for (; *s && o + 4 < cap; s++) o += (size_t)utf8_put(u + o, (u8)*s);
  u[o] = 0;
}
void lx_utf8_latin1(const char *u, char *s, size_t cap) { utf8_to_latin1(u, (u32)strlen(u), s, cap); }
int lx_file_request(int save, const char *title, char *path, size_t cap, const char *pattern)
{
  struct FileRequester *fr;
  char dir[400], file[108];
  int ok = 0;
  strncpy(dir, path, sizeof dir - 1); dir[sizeof dir - 1] = 0;
  *PathPart((STRPTR)dir) = 0;
  strncpy(file, path[0] ? (const char *)FilePart((STRPTR)path) : "", sizeof file - 1); file[sizeof file - 1] = 0;
  if (!(fr = (struct FileRequester *)MUI_AllocAslRequestTags(ASL_FileRequest, ASLFR_TitleText, (ULONG)tr(title),
        ASLFR_DoSaveMode, save, ASLFR_InitialDrawer, (ULONG)dir, ASLFR_InitialFile, (ULONG)file,
        (ULONG)(pattern ? ASLFR_InitialPattern : TAG_IGNORE), (ULONG)pattern, (ULONG)(pattern ? ASLFR_DoPatterns : TAG_IGNORE), TRUE, TAG_DONE))) return 0;
  if (MUI_AslRequestTags(fr, ASLFR_Window, (ULONG)_window(grid), TAG_DONE)) {
    strncpy(path, (const char *)fr->fr_Drawer, cap - 1); path[cap - 1] = 0;
    AddPart((STRPTR)path, fr->fr_File, (ULONG)cap);
    ok = fr->fr_File && fr->fr_File[0];
  }
  MUI_FreeAslRequest(fr);
  return ok;
}
LONG lx_value(void)
{
  LONG v = (LONG)lx_xget(ribbon, MUIA_Ribbon_Value);
  set(ribbon, MUIA_Ribbon_Value, -1);
  return v;
}
int lx_pick(const char *title, const char *prompt, const char *const *labels, int n, int active)
{
  static const ULONG ends[] = { ID_DLGOK, 0 };
  Object *w, *list;
  int i, r = -1;
  w = dialog_window(title, VGroup,
        Child, TextObject, MUIA_Text_Contents, (ULONG)tr(prompt), End,
        Child, ListviewObject, MUIA_Listview_List, list = ListObject, InputListFrame, MUIA_List_ConstructHook, MUIV_List_ConstructHook_String,
          MUIA_List_DestructHook, MUIV_List_DestructHook_String, End, MUIA_CycleChain, 1, End,
      End, NULL);
  if (!w) return -1;
  for (i = 0; i < n; i++) DoMethod(list, MUIM_List_InsertSingle, (ULONG)labels[i], MUIV_List_Insert_Bottom);
  set(list, MUIA_List_Active, active >= 0 && active < n ? active : 0);
  DoMethod(list, MUIM_Notify, MUIA_Listview_DoubleClick, TRUE, (ULONG)app, 2, MUIM_Application_ReturnID, ID_DLGOK);
  if (run_modal(w, ends, NULL) == ID_DLGOK) r = (int)(LONG)lx_xget(list, MUIA_List_Active);
  MUI_DisposeObject(w);
  return r >= 0 && r < n ? r : -1;
}
int lx_parse_range(const char *latin1, Rect *r, u8 *flags)
{
  char u[200];
  u16 sh;
  int n;
  const char *p = latin1;
  while (*p == ' ' || *p == '=') p++;
  if (!*p) return 0;
  lx_latin1_utf8(p, u, sizeof u);
  n = parse_ref(book, u, &sh, r, flags);
  return n > 0 && (u[n] == 0 || u[n] == ' ');
}
void lx_range_text(Rect r, int rows_only, int cols_only, char *out)
{
  char a[16], b[16];
  if (rows_only) { sprintf(out, "$%ld:$%ld", (long)r.r1 + 1, (long)r.r2 + 1); return; }
  col_name(r.c1, a); col_name(r.c2, b);
  if (cols_only) { sprintf(out, "$%s:$%s", a, b); return; }
  if (r.r1 == r.r2 && r.c1 == r.c2) sprintf(out, "$%s$%ld", a, (long)r.r1 + 1);
  else sprintf(out, "$%s$%ld:$%s$%ld", a, (long)r.r1 + 1, b, (long)r.r2 + 1);
}

/* ------------------------------------------------------------------ the ribbon's lists */
#define N(a) ((int)(sizeof(a) / sizeof(a[0])))
static const char *themelabels[16], *themecolorlabels[16];
static const char *const themefonts[] = { "Office: Calibri Light / Calibri", "Office 2007: Cambria / Calibri", "Arial", "Times New Roman",
                                          "Liberation Serif / Liberation Sans", "Carlito / Caladea", "Courier New", "Customize Fonts..." };
static const char *const margins[] = { "Normal  (0.75\" top/bottom, 0.7\" sides)", "Wide  (1\" all round)", "Narrow  (0.75\", 0.25\" sides)", "Custom Margins..." };
static const char *const orients[] = { "Portrait", "Landscape" };
static const char *paperlabels[16];
static const char *const areas[] = { "Set Print Area", "Clear Print Area", "Add to Print Area" };
static const char *const breaks[] = { "Insert Page Break", "Remove Page Break", "Reset All Page Breaks" };
static const char *const fitw[] = { "Automatic", "1 page", "2 pages", "3 pages", "4 pages", "5 pages", "6 pages", "7 pages", "8 pages", "9 pages", "More Pages..." };
static const char *const scales[] = { "10%", "25%", "50%", "70%", "75%", "85%", "90%", "100%", "110%", "125%", "150%", "200%", "400%", "Custom..." };
static const int scalev[] = { 10, 25, 50, 70, 75, 85, 90, 100, 110, 125, 150, 200, 400 };
static const char *const removes[] = { "Remove Arrows", "Remove Precedent Arrows", "Remove Dependent Arrows" };
static const char *const calcopts[] = { "Automatic", "Manual", "Iterative Calculation..." };
static const char *const textorient[] = { "Angle Counterclockwise", "Angle Clockwise", "Vertical Text", "Rotate Text Up", "Rotate Text Down", "Horizontal", "Format Cell Alignment..." };
static const char *const nothing[] = { "(none)" };
RList lx_lists[LXL_LAST - LXL_FIRST] = {
  { themelabels, 0, 0, NULL }, { themecolorlabels, 0, 0, NULL }, { themefonts, N(themefonts), 0, NULL }, { margins, N(margins), 0, NULL },
  { orients, N(orients), 0, NULL }, { paperlabels, 0, 0, NULL }, { areas, N(areas), 0, NULL }, { breaks, N(breaks), 0, NULL },
  { fitw, N(fitw), 0, NULL }, { fitw, N(fitw), 0, NULL }, { scales, N(scales), 0, NULL }, { nothing, 1, 0, NULL }, { nothing, 1, 0, NULL },
  { removes, N(removes), 0, NULL }, { calcopts, N(calcopts), 0, NULL }, { nothing, 1, 0, NULL }, { textorient, N(textorient), 0, NULL },
};
static void lists_init(void)
{
  int i;
  for (i = 0; i < page_nthemes && i < 15; i++) themelabels[i] = themecolorlabels[i] = page_themes[i].name;
  lx_lists[LXL_THEMES - LXL_FIRST].n = i;
  themecolorlabels[i] = "Customize Colours...";
  lx_lists[LXL_THEMECOLORS - LXL_FIRST].n = i + 1;
  for (i = 0; i < page_npapers && i < 15; i++) paperlabels[i] = page_papers[i].name;
  lx_lists[LXL_PAPERS - LXL_FIRST].n = i;
}

/* ------------------------------------------------------------------ page setup changes */
/* an undo step round a change to the active sheet's page setup; NULL out of memory */
static PageSetup *ps_begin(void)
{
  book_begin(book);
  book_note_sheet(book, cur_index());
  return page_get(cur_sheet());
}
static void ps_end(void)
{
  book_end(book);
  book->dirty = 1;
  refresh_all();
}
static void set_area(int how)
{
  PageSetup *p = ps_begin();
  Rect a = cur_sel();
  if (p) {
    if (how == 0 || p->area.r1 < 0) p->area = a;
    else if (how == 1) p->area.r1 = -1;
    else {
      /* (one area only: the two joined) */
      if (a.r1 < p->area.r1) p->area.r1 = a.r1;
      if (a.c1 < p->area.c1) p->area.c1 = a.c1;
      if (a.r2 > p->area.r2) p->area.r2 = a.r2;
      if (a.c2 > p->area.c2) p->area.c2 = a.c2;
    }
  }
  ps_end();
}
/* Insert Page Break: above the active cell's row and left of its column (only one of them in
   the first row or column); Remove takes away those touching the active cell */
static void set_breaks(int how)
{
  Sheet *s = cur_sheet();
  i32 r = s->cur_r, c = s->cur_c;
  book_begin(book);
  book_note_sheet(book, cur_index());
  if (how == 0) {
    if (r > 0) page_set_break(s, 1, r, 1);
    if (c > 0) page_set_break(s, 0, c, 1);
  } else if (how == 1) {
    page_set_break(s, 1, r, 0); page_set_break(s, 0, c, 0);
  } else page_reset_breaks(s);
  ps_end();
}
static void apply_theme_fonts(int k)
{
  static const char *const pairs[][2] = { { "Calibri Light", "Calibri" }, { "Cambria", "Calibri" }, { "Arial", "Arial" }, { "Times New Roman", "Times New Roman" },
                                          { "Liberation Serif", "Liberation Sans" }, { "Carlito", "Caladea" }, { "Courier New", "Courier New" } };
  if (k >= 0 && k < 7) book_set_theme_fonts(book, pairs[k][0], pairs[k][1]);
  else {
    LayoutBook *x = book_lx(book);
    char major[48], minor[48], mu[48], mi[48];
    static const ULONG ends[] = { ID_DLGOK, 0 };
    Object *w, *a, *b;
    if (!x) return;
    lx_utf8_latin1(x->theme.major, major, sizeof major); lx_utf8_latin1(x->theme.minor, minor, sizeof minor);
    w = dialog_window("Customize Fonts", ColGroup(2),
          Child, Label1(tr("Heading font:")), Child, a = lx_str(major, 40),
          Child, Label1(tr("Body font:")), Child, b = lx_str(minor, 40),
        End, NULL);
    if (!w) return;
    if (run_modal(w, ends, NULL) == ID_DLGOK) {
      lx_latin1_utf8(lx_sget(a), mu, sizeof mu); lx_latin1_utf8(lx_sget(b), mi, sizeof mi);
      book_set_theme_fonts(book, mu, mi);
    }
    MUI_DisposeObject(w);
  }
  set(grid, MUIA_Grid_Book, (ULONG)book);
  refresh_all();
}
/* the theme colours dialog: the twelve colours, each a hex number */
static void custom_colours(void)
{
  static const char *const names[12] = { "Text/Background - Light 1", "Text/Background - Dark 1", "Text/Background - Light 2", "Text/Background - Dark 2",
                                         "Accent 1", "Accent 2", "Accent 3", "Accent 4", "Accent 5", "Accent 6", "Hyperlink", "Followed Hyperlink" };
  static const ULONG ends[] = { ID_DLGOK, 0 };
  LayoutBook *x = book_lx(book);
  Object *w, *col, *f[12], *nm;
  char hex[12][8], name[40];
  int i;
  if (!x) return;
  col = ColGroup(4), End;
  for (i = 0; i < 12; i++) {
    sprintf(hex[i], "%06lX", (unsigned long)x->theme.color[i]);
    DoMethod(col, OM_ADDMEMBER, (ULONG)Label1(tr(names[i])));
    DoMethod(col, OM_ADDMEMBER, (ULONG)(f[i] = StringObject, StringFrame, MUIA_String_Contents, (ULONG)hex[i], MUIA_String_Accept, (ULONG)"0123456789ABCDEFabcdef",
                                        MUIA_String_MaxLen, 7, MUIA_FixWidthTxt, (ULONG)"0000000", MUIA_CycleChain, 1, End));
  }
  lx_utf8_latin1(x->theme.name, name, sizeof name);
  w = dialog_window("Customize Colours", VGroup, Child, col, Child, HGroup, Child, Label1(tr("Name:")), Child, nm = lx_str("Custom 1", 30), End, End, NULL);
  if (!w) return;
  if (run_modal(w, ends, NULL) == ID_DLGOK) {
    Theme t = x->theme;
    for (i = 0; i < 12; i++) t.color[i] = (u32)strtoul(lx_sget(f[i]), NULL, 16) & 0xFFFFFF;
    lx_latin1_utf8(lx_sget(nm), t.name, sizeof t.name);
    t.major[0] = t.minor[0] = 0;
    book_set_theme(book, &t);
    refresh_all();
  }
  MUI_DisposeObject(w);
}
static void theme_choice(ULONG id, LONG v)
{
  if (v < 0) {
    /* (from the menu) a dialog with all three */
    static const ULONG ends[] = { ID_DLGOK, 0 };
    static const char *tl[20], *tf[12];
    const char *cur = book->lx ? book->lx->theme.name : "Office";
    Object *w, *t, *f;
    int i, at = 0;
    for (i = 0; i < page_nthemes && i < 18; i++) { tl[i] = page_themes[i].name; if (!strcmp(cur, tl[i])) at = i; }
    tl[i] = NULL;
    for (i = 0; i < 7; i++) tf[i] = themefonts[i];
    tf[i] = NULL;
    w = dialog_window("Themes", ColGroup(2),
          Child, Label1(tr("Theme:")), Child, t = lx_cycle(tl, at),
          Child, Label1(tr("Fonts:")), Child, f = lx_cycle(tf, 0),
          Child, HSpace(0), Child, TextObject, MUIA_Text_Contents, (ULONG)tr("Ledger's own themes keep the fonts you have."), End,
        End, NULL);
    if (!w) return;
    if (run_modal(w, ends, NULL) == ID_DLGOK) {
      book_set_theme(book, &page_themes[lx_xget(t, MUIA_Cycle_Active)]);
      apply_theme_fonts((int)lx_xget(f, MUIA_Cycle_Active));
    }
    MUI_DisposeObject(w);
    return;
  }
  if (id == ID_LX_THEMEDD && v < page_nthemes) book_set_theme(book, &page_themes[v]);
  else if (id == ID_LX_COLORSDD) {
    if (v >= page_nthemes) { custom_colours(); return; }
    else { Theme t = page_themes[v]; t.major[0] = t.minor[0] = 0; book_set_theme(book, &t); }
  } else if (id == ID_LX_FONTSDD) { apply_theme_fonts((int)v); return; }
  set(grid, MUIA_Grid_Book, (ULONG)book);
  refresh_all();
}
/* fit to n pages: n 0 automatic, -1 ask */
static void fit_choice(int height, LONG v)
{
  PageSetup *p;
  long n = v;
  if (v < 0 || v == 10) {
    char t[16];
    sprintf(t, "%d", (int)(height ? page_of(cur_sheet())->fith : page_of(cur_sheet())->fitw));
    if (!ask_string(height ? "Fit to Pages Tall" : "Fit to Pages Wide", "Pages (0: automatic):", t, sizeof t)) return;
    n = atol(t);
    if (n < 0 || n > 999) return;
  }
  if (!(p = ps_begin())) { ps_end(); return; }
  if (height) p->fith = (u16)n; else p->fitw = (u16)n;
  p->fit = p->fitw || p->fith;
  ps_end();
}
static void scale_choice(LONG v)
{
  PageSetup *p;
  long n;
  if (v >= 0 && v < N(scalev)) n = scalev[v];
  else {
    char t[16];
    sprintf(t, "%d", (int)page_of(cur_sheet())->scale);
    if (!ask_string("Scale", "Adjust to (per cent of normal size, 10 to 400):", t, sizeof t)) return;
    n = atol(t);
    if (n < 10 || n > 400) { ask("OK", "%s", tr("The scale goes from 10% to 400%.")); return; }
  }
  if (!(p = ps_begin())) { ps_end(); return; }
  p->scale = (u16)n; p->fit = 0;
  ps_end();
}

/* ------------------------------------------------------------------ the Page Setup dialog */
static struct {
  Object *orient, *scalemode, *scale, *fitw, *fith, *paper, *first;
  Object *m[6], *ch, *cv;
  Object *hpre, *fpre, *hcode, *fcode, *difffirst, *diffoe, *scalehf, *alignhf, *hsample, *fsample;
  Object *area, *trows, *tcols, *grid, *head, *bw, *draft, *order;
} pd;
static void hf_sample(Object *code, Object *sample)
{
  HFSection sec[3];
  HFContext cx;
  char u[400], l[3][120], out[400];
  int k;
  memset(&cx, 0, sizeof cx);
  cx.page = 1; cx.pages = 1; cx.sheet = cur_sheet()->name; cx.file = bookpath[0] ? (const char *)FilePart((STRPTR)bookpath) : "Book1"; cx.now = ledger_now();
  lx_latin1_utf8(lx_sget(code), u, sizeof u);
  hf_expand(u, &cx, sec);
  for (k = 0; k < 3; k++) { char p[300]; hf_plain(&sec[k], p, sizeof p); lx_utf8_latin1(p, l[k], sizeof l[k]); }
  snprintf(out, sizeof out, "%s  |  %s  |  %s", l[0][0] ? l[0] : "-", l[1][0] ? l[1] : "-", l[2][0] ? l[2] : "-");
  set(sample, MUIA_Text_Contents, (ULONG)out);
}
static void ps_each(ULONG id)
{
  if (id == ID_DLG1 || id == ID_DLG2) {
    Object *pre = id == ID_DLG1 ? pd.hpre : pd.fpre, *code = id == ID_DLG1 ? pd.hcode : pd.fcode;
    LONG k = (LONG)lx_xget(pre, MUIA_Cycle_Active);
    char l[200];
    if (k > 0) { lx_utf8_latin1(hf_preset(k), l, sizeof l); set(code, MUIA_String_Contents, (ULONG)l); }
  } else if (id == ID_DLG3) hf_sample(pd.hcode, pd.hsample);
  else if (id == ID_DLG4) hf_sample(pd.fcode, pd.fsample);
  else if (id == ID_DLG5) { Rect a = cur_sel(); char t[40]; lx_range_text(a, 0, 0, t); set(pd.area, MUIA_String_Contents, (ULONG)t); }
  else if (id == ID_DLG6) { Rect a = cur_sel(); char t[40]; lx_range_text(a, 1, 0, t); set(pd.trows, MUIA_String_Contents, (ULONG)t); }
  else if (id == ID_DLG7) { Rect a = cur_sel(); char t[40]; lx_range_text(a, 0, 1, t); set(pd.tcols, MUIA_String_Contents, (ULONG)t); }
}
static Object *mstr(double v)
{
  char t[16];
  sprintf(t, "%.2f", v);
  return StringObject, StringFrame, MUIA_String_Contents, (ULONG)t, MUIA_String_Accept, (ULONG)"0123456789.", MUIA_String_MaxLen, 6, MUIA_FixWidthTxt, (ULONG)"00000", MUIA_CycleChain, 1, End;
}
void lt_page_setup(int page)
{
  static const char *pages_en[] = { "Page", "Margins", "Header/Footer", "Sheet", NULL }, *pl[6];
  static const char *ol[4], *sm[4], *papers[20], *hp[20], *fp[20], *orders[4];
  static const char *const sm_en[] = { "Adjust to % of normal size", "Fit to pages wide by tall", NULL };
  static const char *const ord_en[] = { "Down, then over", "Over, then down", NULL };
  static const char *const or_en[] = { "Portrait", "Landscape", NULL };
  static const ULONG ends[] = { ID_DLGOK, 0 };
  const PageSetup *p0 = page_of(cur_sheet());
  char hcode[300], fcode[300], area[40] = "", trows[40] = "", tcols[40] = "", first[12];
  Object *w, *reg, *selarea, *selrows, *selcols;
  int i;
  lx_trl(pages_en, pl); lx_trl(or_en, ol); lx_trl(sm_en, sm); lx_trl(ord_en, orders);
  for (i = 0; i < page_npapers && i < 19; i++) papers[i] = page_papers[i].name;
  papers[i] = NULL;
  for (i = 0; i < hf_preset_count() && i < 19; i++) {
    static char pre[20][80];
    if (i == 0) strcpy(pre[i], tr("(custom)"));
    else {
      HFSection sec[3]; HFContext cx; char t[3][60];
      int k;
      memset(&cx, 0, sizeof cx); cx.page = 1; cx.pages = 1; cx.sheet = "Sheet1"; cx.file = "Book1.xlsx"; cx.path = "Work:"; cx.now = ledger_now();
      hf_expand(hf_preset(i), &cx, sec);
      for (k = 0; k < 3; k++) { char u[60]; hf_plain(&sec[k], u, sizeof u); lx_utf8_latin1(u, t[k], sizeof t[k]); }
      snprintf(pre[i], sizeof pre[i], "%s%s%s%s%s", t[0], t[0][0] && (t[1][0] || t[2][0]) ? ", " : "", t[1], t[1][0] && t[2][0] ? ", " : "", t[2]);
    }
    hp[i] = fp[i] = pre[i];
  }
  hp[i] = fp[i] = NULL;
  lx_utf8_latin1(p0->hf[HF_HEADER] ? p0->hf[HF_HEADER] : "", hcode, sizeof hcode);
  lx_utf8_latin1(p0->hf[HF_FOOTER] ? p0->hf[HF_FOOTER] : "", fcode, sizeof fcode);
  if (p0->area.r1 >= 0) lx_range_text(p0->area, 0, 0, area);
  if (p0->trow1 >= 0) { Rect r; r.r1 = p0->trow1; r.r2 = p0->trow2; r.c1 = r.c2 = 0; lx_range_text(r, 1, 0, trows); }
  if (p0->tcol1 >= 0) { Rect r; r.c1 = p0->tcol1; r.c2 = p0->tcol2; r.r1 = r.r2 = 0; lx_range_text(r, 0, 1, tcols); }
  if (p0->firstpage) sprintf(first, "%d", (int)p0->firstpage); else strcpy(first, tr("Auto"));
  reg = RegisterGroup(pl), MUIA_Register_Frame, TRUE, MUIA_CycleChain, 1,
    /* Page */
    Child, VGroup,
      Child, ColGroup(2),
        Child, Label1(tr("Orientation:")), Child, pd.orient = lx_cycle(ol, p0->landscape),
        Child, Label1(tr("Scaling:")), Child, pd.scalemode = lx_cycle(sm, p0->fit),
        Child, Label1(tr("Adjust to (%):")), Child, pd.scale = lx_int(p0->scale, 4),
        Child, Label1(tr("Fit to:")), Child, HGroup, Child, pd.fitw = lx_int(p0->fitw, 4), Child, Label1(tr("wide by")), Child, pd.fith = lx_int(p0->fith, 4), Child, Label1(tr("tall")), End,
        Child, Label1(tr("Paper size:")), Child, pd.paper = lx_cycle(papers, p0->paper),
        Child, Label1(tr("First page number:")), Child, pd.first = lx_str(first, 8),
      End,
      Child, TextObject, MUIA_Text_Contents, (ULONG)tr("\33cFit to: 0 pages tall means as many as it takes."), End,
      Child, VSpace(0),
    End,
    /* Margins */
    Child, VGroup,
      Child, ColGroup(4),
        Child, Label1(tr("Top:")), Child, pd.m[M_TOP] = mstr(p0->margin[M_TOP]),
        Child, Label1(tr("Header:")), Child, pd.m[M_HEADER] = mstr(p0->margin[M_HEADER]),
        Child, Label1(tr("Left:")), Child, pd.m[M_LEFT] = mstr(p0->margin[M_LEFT]),
        Child, Label1(tr("Right:")), Child, pd.m[M_RIGHT] = mstr(p0->margin[M_RIGHT]),
        Child, Label1(tr("Bottom:")), Child, pd.m[M_BOTTOM] = mstr(p0->margin[M_BOTTOM]),
        Child, Label1(tr("Footer:")), Child, pd.m[M_FOOTER] = mstr(p0->margin[M_FOOTER]),
      End,
      Child, TextObject, MUIA_Text_Contents, (ULONG)tr("\33cMargins in inches."), End,
      Child, HGroup, Child, Label1(tr("Centre on page:")), Child, pd.ch = lx_check(p0->centerh), Child, LLabel1(tr("Horizontally")),
        Child, pd.cv = lx_check(p0->centerv), Child, LLabel1(tr("Vertically")), Child, HSpace(0), End,
      Child, VSpace(0),
    End,
    /* Header/Footer */
    Child, VGroup,
      Child, ColGroup(2),
        Child, Label1(tr("Header:")), Child, pd.hpre = lx_cycle(hp, 0),
        Child, Label1(tr("Code:")), Child, pd.hcode = lx_str(hcode, 250),
        Child, HSpace(0), Child, pd.hsample = TextObject, TextFrame, MUIA_Text_Contents, (ULONG)"", End,
        Child, Label1(tr("Footer:")), Child, pd.fpre = lx_cycle(fp, 0),
        Child, Label1(tr("Code:")), Child, pd.fcode = lx_str(fcode, 250),
        Child, HSpace(0), Child, pd.fsample = TextObject, TextFrame, MUIA_Text_Contents, (ULONG)"", End,
      End,
      Child, TextObject, MUIA_Text_Contents, (ULONG)tr("\33c&L &C &R left, centre, right;  &P page, &N pages, &D date, &T time,\n&F file, &A sheet, &Z path;  &B bold, &I italic, &U underline, &12 size"), End,
      Child, HGroup,
        Child, pd.difffirst = lx_check(p0->difffirst), Child, LLabel1(tr("Different first page")),
        Child, pd.diffoe = lx_check(p0->diffoddeven), Child, LLabel1(tr("Different odd and even")), Child, HSpace(0),
      End,
      Child, HGroup,
        Child, pd.scalehf = lx_check(p0->scalehf), Child, LLabel1(tr("Scale with document")),
        Child, pd.alignhf = lx_check(p0->alignhf), Child, LLabel1(tr("Align with page margins")), Child, HSpace(0),
      End,
      Child, VSpace(0),
    End,
    /* Sheet */
    Child, VGroup,
      Child, ColGroup(3),
        Child, Label1(tr("Print area:")), Child, pd.area = lx_str(area, 38), Child, selarea = QButton(tr("Selection")),
        Child, Label1(tr("Rows to repeat at top:")), Child, pd.trows = lx_str(trows, 38), Child, selrows = QButton(tr("Selection")),
        Child, Label1(tr("Columns to repeat at left:")), Child, pd.tcols = lx_str(tcols, 38), Child, selcols = QButton(tr("Selection")),
      End,
      Child, HGroup,
        Child, pd.grid = lx_check(p0->gridlines), Child, LLabel1(tr("Gridlines")),
        Child, pd.head = lx_check(p0->headings), Child, LLabel1(tr("Row and column headings")), Child, HSpace(0),
      End,
      Child, HGroup,
        Child, pd.bw = lx_check(p0->blackwhite), Child, LLabel1(tr("Black and white")),
        Child, pd.draft = lx_check(p0->draft), Child, LLabel1(tr("Draft quality")), Child, HSpace(0),
      End,
      Child, ColGroup(2), Child, Label1(tr("Page order:")), Child, pd.order = lx_cycle(orders, p0->overthendown), End,
      Child, VSpace(0),
    End,
  End;
  w = dialog_window("Page Setup", reg, NULL);
  if (!w) return;
  set(reg, MUIA_Group_ActivePage, page);
  DoMethod(pd.hpre, MUIM_Notify, MUIA_Cycle_Active, MUIV_EveryTime, (ULONG)app, 2, MUIM_Application_ReturnID, ID_DLG1);
  DoMethod(pd.fpre, MUIM_Notify, MUIA_Cycle_Active, MUIV_EveryTime, (ULONG)app, 2, MUIM_Application_ReturnID, ID_DLG2);
  DoMethod(pd.hcode, MUIM_Notify, MUIA_String_Contents, MUIV_EveryTime, (ULONG)app, 2, MUIM_Application_ReturnID, ID_DLG3);
  DoMethod(pd.fcode, MUIM_Notify, MUIA_String_Contents, MUIV_EveryTime, (ULONG)app, 2, MUIM_Application_ReturnID, ID_DLG4);
  DoMethod(selarea, MUIM_Notify, MUIA_Pressed, FALSE, (ULONG)app, 2, MUIM_Application_ReturnID, ID_DLG5);
  DoMethod(selrows, MUIM_Notify, MUIA_Pressed, FALSE, (ULONG)app, 2, MUIM_Application_ReturnID, ID_DLG6);
  DoMethod(selcols, MUIM_Notify, MUIA_Pressed, FALSE, (ULONG)app, 2, MUIM_Application_ReturnID, ID_DLG7);
  hf_sample(pd.hcode, pd.hsample); hf_sample(pd.fcode, pd.fsample);
  if (run_modal(w, ends, ps_each) == ID_DLGOK) {
    PageSetup *p = ps_begin();
    if (p) {
      Rect r;
      u8 fl;
      char u[300];
      long n;
      p->landscape = (u8)lx_xget(pd.orient, MUIA_Cycle_Active);
      p->fit = (u8)lx_xget(pd.scalemode, MUIA_Cycle_Active);
      n = atol(lx_sget(pd.scale)); if (n >= 10 && n <= 400) p->scale = (u16)n;
      n = atol(lx_sget(pd.fitw)); if (n >= 0 && n < 1000) p->fitw = (u16)n;
      n = atol(lx_sget(pd.fith)); if (n >= 0 && n < 1000) p->fith = (u16)n;
      p->paper = (u8)lx_xget(pd.paper, MUIA_Cycle_Active);
      n = atol(lx_sget(pd.first)); p->firstpage = (u16)(n > 0 && n < 32768 ? n : 0);
      for (i = 0; i < 6; i++) { double m = atof(lx_sget(pd.m[i])); if (m >= 0 && m < 20) p->margin[i] = m; }
      p->centerh = lx_xget(pd.ch, MUIA_Selected) != 0; p->centerv = lx_xget(pd.cv, MUIA_Selected) != 0;
      lx_latin1_utf8(lx_sget(pd.hcode), u, sizeof u); page_set_hf(p, HF_HEADER, u);
      lx_latin1_utf8(lx_sget(pd.fcode), u, sizeof u); page_set_hf(p, HF_FOOTER, u);
      p->difffirst = lx_xget(pd.difffirst, MUIA_Selected) != 0; p->diffoddeven = lx_xget(pd.diffoe, MUIA_Selected) != 0;
      p->scalehf = lx_xget(pd.scalehf, MUIA_Selected) != 0; p->alignhf = lx_xget(pd.alignhf, MUIA_Selected) != 0;
      if (lx_parse_range(lx_sget(pd.area), &r, &fl)) p->area = r; else if (!lx_sget(pd.area)[0]) p->area.r1 = -1;
      if (lx_parse_range(lx_sget(pd.trows), &r, &fl)) { p->trow1 = r.r1; p->trow2 = r.r2; } else if (!lx_sget(pd.trows)[0]) p->trow1 = p->trow2 = -1;
      if (lx_parse_range(lx_sget(pd.tcols), &r, &fl)) { p->tcol1 = r.c1; p->tcol2 = r.c2; } else if (!lx_sget(pd.tcols)[0]) p->tcol1 = p->tcol2 = -1;
      p->gridlines = lx_xget(pd.grid, MUIA_Selected) != 0; p->headings = lx_xget(pd.head, MUIA_Selected) != 0;
      p->blackwhite = lx_xget(pd.bw, MUIA_Selected) != 0; p->draft = lx_xget(pd.draft, MUIA_Selected) != 0;
      p->overthendown = (u8)lx_xget(pd.order, MUIA_Cycle_Active);
    }
    ps_end();
  }
  MUI_DisposeObject(w);
}

/* ------------------------------------------------------------------ File: export, properties, options */
static void export_pdf(void)
{
  static const char *const what_en[] = { "Active sheet", "Entire workbook", "Selection", NULL };
  static const char *what[4];
  static const ULONG ends[] = { ID_DLGOK, 0 };
  static int last_what;
  char path[512], file[200], dir[400], u1[200], u2[400], msg[600];
  Object *w, *wh, *ign;
  PdfOptions o;
  int ok, pages;
  lx_trl(what_en, what);
  w = dialog_window("Export to PDF", VGroup,
        Child, ColGroup(2),
          Child, Label1(tr("Publish:")), Child, wh = lx_cycle(what, last_what),
          Child, Label1(tr("Ignore print areas:")), Child, HGroup, Child, ign = lx_check(0), Child, HSpace(0), End,
        End,
        Child, TextObject, MUIA_Text_Contents, (ULONG)tr("\33cThe pages are laid out as Page Setup says, with\ntheir headers and footers, and the fonts embedded."), End,
      End, "Export");
  if (!w) return;
  if (run_modal(w, ends, NULL) != ID_DLGOK) { MUI_DisposeObject(w); return; }
  memset(&o, 0, sizeof o);
  o.what = last_what = (int)lx_xget(wh, MUIA_Cycle_Active);
  o.ignore_area = lx_xget(ign, MUIA_Selected) != 0;
  MUI_DisposeObject(w);
  strcpy(path, bookpath[0] ? bookpath : "Book1.pdf");
  { char *dot = strrchr(path, '.'); if (dot && dot > (char *)FilePart((STRPTR)path)) strcpy(dot, ".pdf"); else strncat(path, ".pdf", sizeof path - strlen(path) - 1); }
  if (!lx_file_request(1, "Export to PDF", path, sizeof path, "#?.pdf")) return;
  if (!strchr((const char *)FilePart((STRPTR)path), '.')) strncat(path, ".pdf", sizeof path - strlen(path) - 1);
  strncpy(file, bookpath[0] ? (const char *)FilePart((STRPTR)bookpath) : "Book1", sizeof file - 1); file[sizeof file - 1] = 0;
  strncpy(dir, bookpath, sizeof dir - 1); dir[sizeof dir - 1] = 0; *PathPart((STRPTR)dir) = 0;
  lx_latin1_utf8(file, u1, sizeof u1); lx_latin1_utf8(dir, u2, sizeof u2);
  o.file = u1; o.path = u2; o.now = ledger_now();
  o.formulas = (int)lx_xget(grid, MUIA_Grid_Formulas);
  set(win, MUIA_Window_Sleep, TRUE);
  pages = pdf_count_pages(book, &o);
  ok = pdf_save_book(book, &fonts, &o, path);
  set(win, MUIA_Window_Sleep, FALSE);
  if (!ok) { ask("OK", "Ledger couldn't write the PDF\n%s\n(Is the disk full or write-protected?)", path); return; }
  snprintf(msg, sizeof msg, tr("%d pages written to %s"), pages, (const char *)FilePart((STRPTR)path));
  ask("OK", "%s", msg);
}
static void export_html(void)
{
  char path[512], *h;
  int all = ask("Entire Workbook|Active Sheet|Cancel", "%s", tr("Save the whole workbook as a web page, or just this sheet?"));
  if (!all) return;
  strcpy(path, bookpath[0] ? bookpath : "Book1.html");
  { char *dot = strrchr(path, '.'); if (dot && dot > (char *)FilePart((STRPTR)path)) strcpy(dot, ".html"); else strncat(path, ".html", sizeof path - strlen(path) - 1); }
  if (!lx_file_request(1, "Export as Web Page", path, sizeof path, "#?.(html|htm)")) return;
  set(win, MUIA_Window_Sleep, TRUE);
  h = html_book(book, all == 1 ? -1 : cur_index());
  set(win, MUIA_Window_Sleep, FALSE);
  if (!h || !file_write(path, h, strlen(h))) ask("OK", "Ledger couldn't write\n%s", path);
  free(h);
}
static void properties(void)
{
  static const ULONG ends[] = { ID_DLGOK, 0 };
  LayoutBook *x = book_lx(book);
  Object *w, *f[9];
  char l[9][260];
  static const char *const labels[9] = { "Title:", "Subject:", "Author:", "Manager:", "Company:", "Category:", "Keywords:", "Comments:", "Status:" };
  char *dst[9];
  size_t cap[9];
  int i;
  if (!x) return;
  dst[0] = book->title; cap[0] = sizeof book->title; dst[1] = x->subject; cap[1] = sizeof x->subject; dst[2] = book->author; cap[2] = sizeof book->author;
  dst[3] = x->manager; cap[3] = sizeof x->manager; dst[4] = x->company; cap[4] = sizeof x->company; dst[5] = x->category; cap[5] = sizeof x->category;
  dst[6] = x->keywords; cap[6] = sizeof x->keywords; dst[7] = x->description; cap[7] = sizeof x->description; dst[8] = x->status; cap[8] = sizeof x->status;
  for (i = 0; i < 9; i++) lx_utf8_latin1(dst[i], l[i], sizeof l[i]);
  {
    Object *col = ColGroup(2), End;
    for (i = 0; i < 9; i++) { DoMethod(col, OM_ADDMEMBER, (ULONG)Label1(tr(labels[i]))); DoMethod(col, OM_ADDMEMBER, (ULONG)(f[i] = lx_str(l[i], (int)cap[i] / 2))); }
    w = dialog_window("Properties", col, NULL);
  }
  if (!w) return;
  if (run_modal(w, ends, NULL) == ID_DLGOK) {
    for (i = 0; i < 9; i++) lx_latin1_utf8(lx_sget(f[i]), dst[i], cap[i]);
    book->dirty = 1;
    refresh_all();
  }
  MUI_DisposeObject(w);
}
/* the options kept in Ledger.prefs: a new workbook's font and size and how many sheets it has */
static char opt_font[48] = "Calibri";
static int opt_size = 110, opt_sheets = 1;
int lx_pref_line(const char *line)
{
  if (!strncmp(line, "font=", 5)) { strncpy(opt_font, line + 5, sizeof opt_font - 1); opt_font[sizeof opt_font - 1] = 0; return 1; }
  if (!strncmp(line, "fontsize=", 9)) { int n = atoi(line + 9); if (n >= 60 && n <= 720) opt_size = n; return 1; }
  if (!strncmp(line, "sheets=", 7)) { int n = atoi(line + 7); if (n >= 1 && n <= 255) opt_sheets = n; return 1; }
  return 0;
}
void lx_pref_save(FILE *f)
{
  if (strcmp(opt_font, "Calibri")) fprintf(f, "font=%s\n", opt_font);
  if (opt_size != 110) fprintf(f, "fontsize=%d\n", opt_size);
  if (opt_sheets != 1) fprintf(f, "sheets=%d\n", opt_sheets);
}
Book *lx_new_book(void)
{
  Book *b = book_new();
  int i;
  if (!b) return NULL;
  if (strcmp(opt_font, "Calibri") || opt_size != 110) {
    lx_latin1_utf8(opt_font, b->fonts[0].name, sizeof b->fonts[0].name);
    b->fonts[0].size = (u16)opt_size;
  }
  for (i = 1; i < opt_sheets; i++) { char nm[32]; book_unique_sheet_name(b, "Sheet1", nm); book_add_sheet(b, nm, -1); }
  book_forget_undo(b);
  b->dirty = 0;
  return b;
}
static void opt_each(ULONG id) { if (id == ID_DLG1) { book_recalc(book); refresh_all(); } }
/* Ledger Options: General (a new workbook's font, size, sheets) and Formulas (calculation) */
static void calc_options(int page)
{
  static const char *const modes_en[] = { "Automatic", "Manual", NULL };
  static const char *const pages_en[] = { "General", "Formulas", NULL };
  static const char *modes[4], *pl[4];
  static const ULONG ends[] = { ID_DLGOK, 0 };
  LayoutBook *x = book_lx(book);
  Object *w, *reg, *mode, *it, *maxi, *maxc, *font, *size, *sheets, *r1c1, *calcnow;
  char mc[24], sz[12];
  if (!x) return;
  lx_trl(modes_en, modes); lx_trl(pages_en, pl);
  sprintf(mc, "%g", x->maxchange);
  sprintf(sz, "%d", opt_size / 10);
  reg = RegisterGroup(pl), MUIA_Register_Frame, TRUE, MUIA_CycleChain, 1,
    Child, VGroup,
      Child, ColGroup(2),
        Child, Label1(tr("Default font for new workbooks:")), Child, font = lx_str(opt_font, 40),
        Child, Label1(tr("Font size:")), Child, size = StringObject, StringFrame, MUIA_String_Contents, (ULONG)sz, MUIA_String_Accept, (ULONG)"0123456789.", MUIA_String_MaxLen, 5, MUIA_CycleChain, 1, End,
        Child, Label1(tr("Sheets in a new workbook:")), Child, sheets = lx_int(opt_sheets, 3),
      End,
      Child, VSpace(0),
    End,
    Child, VGroup,
      Child, ColGroup(2),
        Child, Label1(tr("Workbook calculation:")), Child, mode = lx_cycle(modes, book->manualcalc),
        Child, Label1(tr("Enable iterative calculation:")), Child, HGroup, Child, it = lx_check(x->iterate), Child, HSpace(0), End,
        Child, Label1(tr("Maximum iterations:")), Child, maxi = lx_int(x->maxiter, 5),
        Child, Label1(tr("Maximum change:")), Child, maxc = StringObject, StringFrame, MUIA_String_Contents, (ULONG)mc, MUIA_String_Accept, (ULONG)"0123456789.eE-", MUIA_String_MaxLen, 20, MUIA_CycleChain, 1, End,
        Child, Label1(tr("R1C1 reference style:")), Child, HGroup, Child, r1c1 = lx_check(x->r1c1), Child, LLabel1(tr("(kept in the file; Ledger shows A1)")), Child, HSpace(0), End,
      End,
      Child, TextObject, MUIA_Text_Contents, (ULONG)tr("\33cIterative calculation works circular formulas out again and again\nuntil they change less than the maximum change."), End,
      Child, HGroup, Child, HSpace(0), Child, calcnow = QButton(tr("Calculate Now (F9)")), Child, HSpace(0), End,
    End,
  End;
  w = dialog_window("Ledger Options", reg, NULL);
  if (!w) return;
  set(reg, MUIA_Group_ActivePage, page);
  DoMethod(calcnow, MUIM_Notify, MUIA_Pressed, FALSE, (ULONG)app, 2, MUIM_Application_ReturnID, ID_DLG1);
  if (run_modal(w, ends, opt_each) == ID_DLGOK) {
    long n = atol(lx_sget(maxi));
    double d = atof(lx_sget(maxc));
    book->manualcalc = (int)lx_xget(mode, MUIA_Cycle_Active);
    x->iterate = lx_xget(it, MUIA_Selected) != 0;
    x->r1c1 = lx_xget(r1c1, MUIA_Selected) != 0;
    if (n >= 1 && n <= 32767) x->maxiter = (u16)n;
    if (d > 0) x->maxchange = d;
    if (lx_sget(font)[0]) { strncpy(opt_font, lx_sget(font), sizeof opt_font - 1); opt_font[sizeof opt_font - 1] = 0; }
    n = (long)(atof(lx_sget(size)) * 10 + 0.5); if (n >= 60 && n <= 720) opt_size = (int)n;
    n = atol(lx_sget(sheets)); if (n >= 1 && n <= 255) opt_sheets = (int)n;
    lx_save_prefs();
    book->dirty = 1;
    if (!book->manualcalc) book_recalc(book);
    refresh_all();
  }
  MUI_DisposeObject(w);
}

/* ------------------------------------------------------------------ Home: the rest */
static void move_copy_sheet(void)
{
  static const ULONG ends[] = { ID_DLGOK, 0 };
  static char names[MAXSHEETS + 1][40];
  static const char *ptrs[MAXSHEETS + 2], *books[10];
  static char booknames[10][60];
  Object *w, *tob, *list, *copy;
  int i, n = 0, nb = 0, bi[10], cur = 0;
  for (i = 0; i < win_slots() && nb < 9; i++) {
    Book *b; const char *pth;
    if (!win_info(i, NULL, &b, &pth, NULL)) continue;
    {
      int k, dup = 0;
      for (k = 0; k < nb; k++) { Book *o; win_info(bi[k], NULL, &o, NULL, NULL); if (o == b) dup = 1; }
      if (dup) continue;
    }
    if (b == book) cur = nb;
    strncpy(booknames[nb], pth && pth[0] ? (const char *)FilePart((STRPTR)pth) : tr("(new workbook)"), 59); booknames[nb][59] = 0;
    books[nb] = booknames[nb]; bi[nb++] = i;
  }
  books[nb] = NULL;
  for (i = 0; i < book->nsh && n < MAXSHEETS; i++) { lx_utf8_latin1(book->sh[i]->name, names[n], sizeof names[n]); ptrs[n] = names[n]; n++; }
  strcpy(names[n], tr("(move to end)")); ptrs[n] = names[n]; n++;
  ptrs[n] = NULL;
  w = dialog_window("Move or Copy", VGroup,
        Child, ColGroup(2), Child, Label1(tr("To book:")), Child, tob = lx_cycle(books, cur), End,
        Child, TextObject, MUIA_Text_Contents, (ULONG)tr("Before sheet:"), End,
        Child, ListviewObject, MUIA_Listview_List, list = ListObject, InputListFrame, MUIA_List_SourceArray, (ULONG)ptrs, MUIA_List_Active, book->active, End, MUIA_CycleChain, 1, End,
        Child, HGroup, Child, copy = lx_check(0), Child, LLabel1(tr("Create a copy")), Child, HSpace(0), End,
      End, NULL);
  if (!w) return;
  if (run_modal(w, ends, NULL) == ID_DLGOK) {
    LONG at = (LONG)lx_xget(list, MUIA_List_Active), tb = (LONG)lx_xget(tob, MUIA_Cycle_Active);
    int docopy = lx_xget(copy, MUIA_Selected) != 0, src = book->active;
    Book *dst = book;
    if (tb >= 0 && tb < nb) win_info(bi[tb], NULL, &dst, NULL, NULL);
    if (at < 0) at = book->nsh;
    if (dst == book) {
      if (docopy) {
        int c = book_duplicate_sheet(book, src);
        if (c >= 0) { book_move_sheet(book, c, at > c ? (int)at - 1 : (int)at); }
      } else book_move_sheet(book, src, at > src ? (int)at - 1 : (int)at);
      set(grid, MUIA_Grid_Book, (ULONG)book);
    } else {
      /* to another workbook: through the clipboard format, which carries values, formulas and looks */
      Sheet *s = book->sh[src];
      char nm[32];
      int k;
      i32 lr = sheet_last_row(s), lc = sheet_last_col(s);
      Clip *cl;
      Rect a;
      book_unique_sheet_name(dst, s->name, nm);
      k = book_add_sheet(dst, nm, -1);
      a.r1 = a.c1 = 0; a.r2 = lr < 0 ? 0 : lr; a.c2 = lc < 0 ? 0 : lc;
      if (k >= 0 && (cl = clip_copy(book, src, a))) {
        Rect out;
        i32 c;
        book_paste(dst, k, 0, 0, cl, PASTE_ALL, 0, &out);
        clip_free(cl);
        for (c = 0; c <= a.c2; c++) book_set_col_width(dst, k, c, sheet_col_width(s, c) ? sheet_col_width(s, c) : s->defwidth);
        dst->sh[k]->page = page_copy(s->page);
        dst->dirty = 1;
        if (!docopy) book_delete_sheet(book, src);
      }
      set(grid, MUIA_Grid_Book, (ULONG)book);
    }
    book->dirty = 1;
    refresh_all();
  }
  MUI_DisposeObject(w);
}
/* Paste Special: what to paste, an operation with the cells pasted on, skip blanks, transpose */
static void paste_special(void)
{
  static const char *const what_en[] = { "All", "Formulas", "Values", "Formats", "All except borders", NULL };
  static const char *const op_en[] = { "None", "Add", "Subtract", "Multiply", "Divide", NULL };
  static const char *wh[8], *ops[8];
  static const ULONG ends[] = { ID_DLGOK, 0 };
  Clip *cl = app_clip();
  Object *w, *what, *op, *skip, *tr_;
  if (!cl) { ask("OK", "%s", tr("Copy some cells first: Paste Special pastes what Ledger copied.")); return; }
  lx_trl(what_en, wh); lx_trl(op_en, ops);
  w = dialog_window("Paste Special", VGroup,
        Child, ColGroup(2),
          Child, Label1(tr("Paste:")), Child, what = lx_cycle(wh, 0),
          Child, Label1(tr("Operation:")), Child, op = lx_cycle(ops, 0),
        End,
        Child, HGroup,
          Child, skip = lx_check(0), Child, LLabel1(tr("Skip blanks")),
          Child, tr_ = lx_check(0), Child, LLabel1(tr("Transpose")), Child, HSpace(0),
        End,
      End, NULL);
  if (!w) return;
  if (run_modal(w, ends, NULL) == ID_DLGOK) {
    static const int map[] = { PASTE_ALL, PASTE_FORMULAS, PASTE_VALUES, PASTE_FORMATS, PASTE_ALL };
    int k = (int)lx_xget(what, MUIA_Cycle_Active), o = (int)lx_xget(op, MUIA_Cycle_Active), sk = lx_xget(skip, MUIA_Selected) != 0, tp = lx_xget(tr_, MUIA_Selected) != 0;
    Sheet *s = cur_sheet();
    i32 r0 = s->cur_r, c0 = s->cur_c, rows = tp ? cl->cols : cl->rows, cols = tp ? cl->rows : cl->cols, r, c;
    double *old = NULL;
    u8 *had = NULL;
    Rect out;
    if ((o || sk || k == 4) && (old = (double *)calloc((size_t)rows * cols, sizeof(double))) && (had = (u8 *)calloc((size_t)rows * cols, 1))) {
      for (r = 0; r < rows; r++) for (c = 0; c < cols; c++) {
        const Value *v = cell_value(book, cur_index(), r0 + r, c0 + c);
        Cell *ce = sheet_cell(s, r0 + r, c0 + c);
        old[r * cols + c] = v->t == V_NUM ? v->n : 0;
        had[r * cols + c] = ce && (ce->f || ce->v.t != V_EMPTY) ? (v->t == V_NUM ? 2 : 1) : 0;
      }
    }
    book_begin(book);
    {
      /* the styles of the target, to put the borders back (All except borders) */
      int *st = NULL;
      if (k == 4 && (st = (int *)malloc((size_t)rows * cols * sizeof(int)))) for (r = 0; r < rows; r++) for (c = 0; c < cols; c++) st[r * cols + c] = sheet_cell_style(s, r0 + r, c0 + c);
      book_paste(book, cur_index(), r0, c0, cl, o ? PASTE_VALUES : map[k], tp, &out);
      if (st) {
        for (r = 0; r < rows; r++) for (c = 0; c < cols; c++) {
          Style ns = book->styles[sheet_cell_style(s, r0 + r, c0 + c)];
          const Style *os = &book->styles[st[r * cols + c]];
          memcpy(ns.border, os->border, sizeof ns.border); memcpy(ns.bcolor, os->bcolor, sizeof ns.bcolor);
          book_set_style(book, cur_index(), r0 + r, c0 + c, book_style(book, &ns));
        }
        free(st);
      }
    }
    if (old && had)
      for (r = 0; r < rows; r++) for (c = 0; c < cols; c++) {
        const ClipCell *cc = &cl->cells[tp ? c * cl->cols + r : r * cl->cols + c];
        const Value *nv = cell_value(book, cur_index(), r0 + r, c0 + c);
        if (sk && !cc->has) {
          /* a blank in the copy leaves the cell as it was */
          if (had[r * cols + c] == 2) { Value v = val_num(old[r * cols + c]); book_set_value(book, cur_index(), r0 + r, c0 + c, &v); }
          continue;
        }
        if (o && nv->t == V_NUM && had[r * cols + c] != 1) {
          double a = old[r * cols + c], bv = nv->n, res = o == 1 ? a + bv : o == 2 ? a - bv : o == 3 ? a * bv : (bv != 0 ? a / bv : NAN);
          Value v = isnan(res) ? val_err(E_DIV0) : val_num(res);
          book_set_value(book, cur_index(), r0 + r, c0 + c, &v);
        }
      }
    book_end(book);
    free(old); free(had);
    DoMethod(grid, MUIM_Grid_Select, out.r1, out.c1, out.r2, out.c2, out.r1, out.c1);
    refresh_all();
  }
  MUI_DisposeObject(w);
}
static void clear_notes_links(int links)
{
  Rect a = cur_sel();
  Sheet *s = cur_sheet();
  int i;
  book_begin(book);
  if (links) {
    for (i = s->nlinks - 1; i >= 0; i--) {
      const Link *l = &s->links[i];
      if (l->r >= a.r1 && l->r <= a.r2 && l->c >= a.c1 && l->c <= a.c2) book_set_link(book, cur_index(), l->r, l->c, NULL, NULL);
    }
  } else
    for (i = s->nnotes - 1; i >= 0; i--) {
      const Note *n = &s->notes[i];
      if (n->r >= a.r1 && n->r <= a.r2 && n->c >= a.c1 && n->c <= a.c2) book_set_note(book, cur_index(), n->r, n->c, NULL, NULL);
    }
  book_end(book);
  refresh_all();
}
/* Fill > Justify: the text of the selection's first column joined and broken again into lines
   as wide as the selection (by characters of the column width) */
static void fill_justify(void)
{
  Rect a = cur_sel();
  Sheet *s = cur_sheet();
  Buf all;
  i32 r, width = 0, c;
  const char *p;
  int ok = 1;
  for (c = a.c1; c <= a.c2; c++) width += sheet_col_width(s, c);
  width = width / 7;
  if (width < 4) width = 4;
  buf_init(&all);
  for (r = a.r1; r <= a.r2 && r <= sheet_last_row(s); r++) {
    Cell *ce = sheet_cell(s, r, a.c1);
    if (!ce) continue;
    if (ce->f || (ce->v.t != V_STR && ce->v.t != V_EMPTY)) { ok = 0; break; }
    if (ce->v.t == V_STR) { if (all.len) buf_addc(&all, ' '); buf_adds(&all, ce->v.s); }
  }
  if (!ok) { buf_free(&all); ask("OK", "%s", tr("Justify works on text only: the selection holds numbers or formulas.")); return; }
  book_begin(book);
  for (r = a.r1; r <= a.r2 && r <= sheet_last_row(s); r++) { Rect one; one.r1 = one.r2 = r; one.c1 = one.c2 = a.c1; book_clear(book, cur_index(), one, CLR_CONTENTS); }
  p = all.data ? all.data : "";
  r = a.r1;
  while (*p) {
    const char *e = p, *brk = NULL;
    Value v;
    long n = 0;
    while (*e && n < width) { if (*e == ' ') brk = e; e += utf8_next(e, strlen(e), 0); n++; }
    if (*e && brk && brk > p) e = brk;
    v = val_strn(p, (size_t)(e - p));
    book_set_value(book, cur_index(), r++, a.c1, &v);
    val_free(&v);
    p = e;
    while (*p == ' ') p++;
    if (r >= MAXROW) break;
  }
  book_end(book);
  buf_free(&all);
  refresh_all();
}
typedef struct { int what; int v; } FxArg;
static void fx_one(Style *st, Font *f, const void *arg)
{
  const FxArg *a = (const FxArg *)arg;
  if (a->what == 0) f->underline = f->underline == 2 ? 0 : 2;
  else if (a->what == 1) st->rotation = (i16)a->v;
  else if (a->what == 2) f->vert = (u8)(a->v < 0 ? 0 : a->v);
}
static void text_orient(LONG v)
{
  FxArg a;
  static const int rot[] = { 45, -45, 90, 90, -90, 0 };
  if (v < 0 || v >= 6) { dlg_format_cells(1); return; }
  a.what = 1; a.v = rot[v];
  book_restyle(book, cur_index(), cur_sel(), fx_one, &a);
  refresh_all();
}
/* New Cell Style: the active cell's look added to the Cell Styles list under a name (kept while Ledger runs) */
static void new_cell_style(void)
{
  ask("OK", "%s", tr("Ledger's cell styles are a fixed set (Home > Cell Styles).\nTo reuse a look, pick it up with the Format Painter."));
}

/* ------------------------------------------------------------------ the Page Layout tab's commands */
void lt_command(ULONG id, LONG v)
{
  PageSetup *p;
  switch (id) {
  case ID_LX_THEMEDD: case ID_LX_COLORSDD: case ID_LX_FONTSDD: theme_choice(id, v); break;
  case ID_LX_MARGINSDD:
    if (v >= 0 && v < 3) { if ((p = ps_begin())) page_margin_preset(p, (int)v); ps_end(); }
    else lt_page_setup(1);
    break;
  case ID_LX_ORIENTDD:
    if (v < 0) v = lx_pick("Orientation", "The pages:", orients, 2, page_of(cur_sheet())->landscape);
    if (v >= 0 && (p = ps_begin())) p->landscape = v == 1;
    if (v >= 0) ps_end();
    break;
  case ID_LX_SIZEDD:
    if (v < 0) v = lx_pick("Size", "Paper size:", paperlabels, page_npapers < 15 ? page_npapers : 15, page_of(cur_sheet())->paper);
    if (v >= 0 && (p = ps_begin())) p->paper = (u8)v;
    if (v >= 0) ps_end();
    break;
  case ID_LX_AREADD:
    if (v < 0) v = lx_pick("Print Area", "The print area:", areas, 3, 0);
    if (v >= 0) set_area((int)v);
    break;
  case ID_LX_BREAKSDD:
    if (v < 0) v = lx_pick("Breaks", "Page breaks at the active cell:", breaks, 3, 0);
    if (v >= 0) set_breaks((int)v);
    break;
  case ID_LX_TITLES: lt_page_setup(3); break;
  case ID_LX_HEADERFOOTER:
    /* as Excel does: Page Layout view, then the header and footer to write */
    if (page_of(cur_sheet())->view != PV_LAYOUT) vt_command(ID_LX_PAGELAYOUT, -1);
    lt_page_setup(2);
    break;
  case ID_LX_PAGESETUP: lt_page_setup(0); break;
  case ID_LX_FITWDD: case ID_LX_FITHDD: fit_choice(id == ID_LX_FITHDD, v); break;
  case ID_LX_SCALEDD:
    if (v < 0) lt_page_setup(0); else scale_choice(v);
    break;
  case ID_LX_PRINTGRID: case ID_LX_PRINTHEAD:
    if ((p = ps_begin())) { if (id == ID_LX_PRINTGRID) p->gridlines = !p->gridlines; else p->headings = !p->headings; }
    ps_end();
    break;
  case ID_LX_VIEWGRID: DoMethod(app, MUIM_Application_ReturnID, ID_GRIDLINES); break;
  case ID_LX_VIEWHEAD: DoMethod(app, MUIM_Application_ReturnID, ID_HEADINGS); break;
  case ID_LX_EXPORTPDF: case ID_LX_SAVEPDF: export_pdf(); break;
  case ID_LX_EXPORTHTML: export_html(); break;
  case ID_LX_PROPS: properties(); break;
  case ID_LX_CALCOPTS: calc_options(v == -2 ? 0 : 1); break;
  case ID_LX_OPTIONS: calc_options(0); break;
  case ID_LX_MOVECOPY: move_copy_sheet(); break;
  case ID_LX_PASTESPECIAL: paste_special(); break;
  case ID_LX_CLEARNOTES: clear_notes_links(0); break;
  case ID_LX_CLEARLINKS: clear_notes_links(1); break;
  case ID_LX_JUSTIFY: fill_justify(); break;
  case ID_LX_ACCOUNTING: {
    char code[128];
    apply_format(FX_NUMFMT, 0, numfmt_code(3, 2, code, sizeof code));
    break; }
  case ID_LX_DBLUNDER: { FxArg a; a.what = 0; a.v = 0; book_restyle(book, cur_index(), cur_sel(), fx_one, &a); refresh_all(); break; }
  case ID_LX_TEXTORIENTDD:
    if (v < 0) v = lx_pick("Orientation", "Text in the selected cells:", textorient, N(textorient), 5);
    if (v >= 0) text_orient(v);
    break;
  case ID_LX_NEWCELLSTYLE: new_cell_style(); break;
  case ID_LX_SUPER: case ID_LX_SUB: {
    FxArg a;
    const Style *st = &book->styles[sheet_cell_style(cur_sheet(), cur_sheet()->cur_r, cur_sheet()->cur_c)];
    a.what = 2; a.v = id == ID_LX_SUPER ? 1 : 2;
    /* (all the selection to the active cell's new state) */
    if (book->fonts[st->font].vert == a.v) a.v = -a.v;
    book_restyle(book, cur_index(), cur_sel(), fx_one, &a);
    refresh_all();
    break; }
  }
}

/* ------------------------------------------------------------------ dispatch (layout.h) */
void lx_command(ULONG id)
{
  LONG v = -1;
  if (id == ID_LX_THEMEDD || id == ID_LX_COLORSDD || id == ID_LX_FONTSDD || id == ID_LX_MARGINSDD || id == ID_LX_ORIENTDD || id == ID_LX_SIZEDD ||
      id == ID_LX_AREADD || id == ID_LX_BREAKSDD || id == ID_LX_FITWDD || id == ID_LX_FITHDD || id == ID_LX_SCALEDD || id == ID_LX_RECENTDD ||
      id == ID_LX_USENAMEDD || id == ID_LX_REMOVEDD || id == ID_LX_CALCOPTDD || id == ID_LX_SWITCHDD || id == ID_LX_TEXTORIENTDD) v = lx_value();
  if (id <= ID_LX_SAVEPDF || (id >= ID_LX_GOTOSPECIAL && id <= ID_LX_SUB && id != ID_LX_GOTOSPECIAL)) lt_command(id, v);
  else if (id >= ID_LX_NORMAL && id <= ID_LX_ZOOMDLG) vt_command(id, v);
  else if (id == ID_LX_SCROLLED || id == ID_LX_REFRESH) vt_command(id, v);
  else ft_command(id, v);
}
/* Draw Border: the next cells dragged over get an outline (or every line, or none) */
static int draw_mode = -1;
static void edge_one(Style *st, Font *f, const void *arg)
{
  const int *e = (const int *)arg;           /* side, kind */
  (void)f;
  if (e[0] < 0) { int k; for (k = 0; k < 4; k++) { st->border[k] = (u8)e[1]; st->bcolor[k] = COLOR_AUTO; } }
  else { st->border[e[0]] = (u8)e[1]; st->bcolor[e[0]] = COLOR_AUTO; }
}
int lx_brush_done(void)
{
  Rect a, e;
  int arg[2];
  if (draw_mode < 0 || lx_xget(grid, MUIA_Grid_Brush)) return 0;
  a = cur_sel();
  book_begin(book);
  if (draw_mode == 0) {
    arg[1] = BS_THIN;
    e = a; e.r2 = a.r1; arg[0] = B_TOP; book_restyle(book, cur_index(), e, edge_one, arg);
    e = a; e.r1 = a.r2; arg[0] = B_BOTTOM; book_restyle(book, cur_index(), e, edge_one, arg);
    e = a; e.c2 = a.c1; arg[0] = B_LEFT; book_restyle(book, cur_index(), e, edge_one, arg);
    e = a; e.c1 = a.c2; arg[0] = B_RIGHT; book_restyle(book, cur_index(), e, edge_one, arg);
  } else { arg[0] = -1; arg[1] = draw_mode == 1 ? BS_THIN : BS_NONE; book_restyle(book, cur_index(), a, edge_one, arg); }
  book_end(book);
  draw_mode = -1;
  refresh_all();
  return 1;
}
int lx_ribbon_choice(ULONG id, LONG v)
{
  switch (id) {
  case ID_BORDERDD:
    if (v >= 11 && v <= 13) {
      draw_mode = (int)v - 11;
      set(grid, MUIA_Grid_Brush, 1);
      update_controls();
      return 1;
    }
    break;
  case ID_FILLDD: if (v == 5) { fill_justify(); return 1; } break;
  case ID_CLEARDD: if (v == 3 || v == 4) { clear_notes_links(v == 4); return 1; } break;
  case ID_FINDDD: if (v == 3) { ft_command(ID_LX_GOTOSPECIAL, -1); return 1; } break;
  case ID_PASTEDD: if (v == 5) { paste_special(); return 1; } break;
  case ID_FORMATDD:
    if (v == 12) { DoMethod(app, MUIM_Application_ReturnID, ID_HIDESHEET); return 1; }
    if (v == 13) { DoMethod(app, MUIM_Application_ReturnID, ID_UNHIDESHEET); return 1; }
    if (v == 14) { move_copy_sheet(); return 1; }
    break;
  case ID_CALCDD: case ID_FN_FIN: case ID_FN_LOGIC: case ID_FN_TEXT: case ID_FN_DATE: case ID_FN_LOOKUP: case ID_FN_MATH: case ID_FN_MORE:
    if (id != ID_CALCDD) { const char *e = ribbon_entry(id, v); if (e) ft_used_function(e); }
    break;
  }
  return 0;
}
int lx_bit(int b)
{
  Sheet *s;
  if (!book) return 0;                       /* (a window drawn before its workbook is in) */
  s = cur_sheet();
  switch (b) {
  case LXB_PRINTGRID: return page_of(s)->gridlines;
  case LXB_PRINTHEAD: return page_of(s)->headings;
  case LXB_LANDSCAPE: return page_of(s)->landscape;
  case LXB_VIEWGRID: return s->gridlines;
  case LXB_VIEWHEAD: return s->headings;
  }
  return vt_bit(b) || ft_bit(b);
}
ULONG lx_bits_hash(void)
{
  ULONG h = 0;
  int b;
  if (!book) return 0;
  for (b = LXB_FIRST; b < LXB_LAST; b++) if (lx_bit(b)) h |= 1UL << (b - LXB_FIRST);
  return h;
}
void lx_update(void)
{
  if (!book) return;
  vt_update();
  ft_update();
}
void lx_init(void)
{
  lists_init();
  ft_init();
}
void lx_cleanup(void) { ft_cleanup(); }
void lx_grid_draw(struct RastPort *rp, const GridGeom *g)
{
  vt_draw(rp, g);
  ft_draw(rp, g);
}
