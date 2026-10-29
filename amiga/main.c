/*
 * main.c - Ledger: a spreadsheet for the Amiga (68040+, MUI). The application: windows, menus, the
 * ribbon's and menus' commands, files, preferences. The sheet itself is grid.c; workbooks,
 * formulas and file formats are the portable core in ../core.
 *
 * How it hangs together, for those new to MUI: everything the user does arrives as an ID (ids.h).
 * Menu items carry theirs in menus[] below; buttons and other gadgets are given theirs with a
 * MUIM_Notify that does MUIM_Application_ReturnID. main()'s loop calls MUIM_Application_NewInput,
 * which deals with the Intuition events and returns the next ID (0: none), then Wait()s for more;
 * the ID goes to command() or ribbon_choice(). Most commands change the workbook through the core
 * and then call refresh_all(). Each workbook has a window of its own (see DocWin, near the end).
 */
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <math.h>
#include <exec/memory.h>
#include <dos/dos.h>
#include <dos/dosextens.h>
#include <workbench/startup.h>
#include <libraries/asl.h>
#include <libraries/gadtools.h>
#include <graphics/gfxbase.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/intuition.h>
#include <proto/graphics.h>
#include <proto/muimaster.h>
#include <proto/utility.h>
#include <proto/icon.h>
#include <clib/alib_protos.h>
#include "app.h"
#include "theme.h"
#include "lang.h"
#include "ids.h"
#include "splash.h"
#include "sysfonts.h"
#include "tabletab.h"
#include "../core/calc.h"
#include "../core/xlsx.h"
#include "../core/template.h"

#ifndef MUIA_Window_DisableKeys
#define MUIA_Window_DisableKeys 0x80424c36UL   /* in MUI 3.8, undocumented: MUIKEYF_ keys the window ignores */
#endif
#ifndef MUIKEYF_WINDOW_CLOSE
#define MUIKEYF_WINDOW_CLOSE (1UL << MUIKEY_WINDOW_CLOSE)
#endif

const char verstag[] = "$VER: Ledger 0.2.2 (28.10.2026) a spreadsheet for the Amiga (MUI)";

struct IntuitionBase *IntuitionBase;
struct GfxBase *GfxBase;
struct Library *MUIMasterBase, *LayersBase, *DiskfontBase, *KeymapBase, *UtilityBase;
extern struct WBStartup *_WBenchMsg;
extern int ledger_date_order;

FILE *qlog;
char pub_screen[64];
Object *app, *win, *grid, *ribbon, *statusbar, *tabs, *backstage, *pages, *vscroll, *hscroll, *namebox, *fxbar, *fxgroup;
Book *book;
char bookpath[512];
FontSet fonts;
char username[64];
int pref_dark, pref_large, pref_fxbar = 1;
static char title[600];
static int back_shown;
static LONG win_box[4];
static int fonts_assigned;

/* (core) NOW() and TODAY(): the Amiga's clock as a serial number (the clock keeps local time) */
double ledger_now(void)
{
  struct DateStamp ds;
  DateStamp(&ds);
  /* 1 January 1978 (the Amiga's day 0) is serial 28491 */
  return 28491.0 + ds.ds_Days + ds.ds_Minute / 1440.0 + ds.ds_Tick / (50.0 * 86400.0);
}

/* ------------------------------------------------------------------ the menus */
#define M(title) { NM_TITLE, (STRPTR)title, 0, 0, 0, 0 }
#define I(label, key, id) { NM_ITEM, (STRPTR)label, (STRPTR)key, 0, 0, (APTR)id }
#define T(label, key, id) { NM_ITEM, (STRPTR)label, (STRPTR)key, CHECKIT | MENUTOGGLE, 0, (APTR)id }
#define BAR { NM_ITEM, NM_BARLABEL, 0, 0, 0, 0 }
static struct NewMenu menus[] = {
  M("Project"),
  I("New", "N", ID_NEW), I("Open...", "O", ID_OPEN), I("Import CSV...", 0, ID_IMPORTCSV), BAR,
  I("Save", "S", ID_SAVE), I("Save As...", "A", ID_SAVEAS), I("Save Sheet as CSV...", 0, ID_SAVECSV), BAR,
  I("Close", "W", ID_CLOSE), I("Next Window", "`", ID_NEXTWIN), BAR, I("About...", "?", ID_ABOUT), I("Quit", "Q", ID_QUIT),
  M("Edit"),
  I("Undo", "Z", ID_UNDO), I("Redo", "Y", ID_REDO), BAR,
  I("Cut", "X", ID_CUT), I("Copy", "C", ID_COPY), I("Paste", "V", ID_PASTE), I("Paste Values", 0, ID_PASTEVALUES),
  I("Paste Formulas", 0, ID_PASTEFORMULAS), I("Paste Formatting", 0, ID_PASTEFORMATS), I("Paste Transposed", 0, ID_PASTETRANSPOSE), BAR,
  I("Select All", "L", ID_SELALL), I("Edit Cell", 0, ID_EDITCELL), I("Clear Contents", 0, ID_CLEARCONTENTS), I("Clear Formats", 0, ID_CLEARFORMATS),
  I("Fill Down", "D", ID_FILLDOWN), I("Fill Right", "R", ID_FILLRIGHT), I("Fill Series...", 0, ID_FILLSERIES), BAR,
  I("Find...", "F", ID_FIND), I("Find Next", "K", ID_FINDNEXT), I("Replace...", "H", ID_REPLACE), I("Go To...", "G", ID_GOTO),
  M("Insert"),
  I("Rows", 0, ID_INSROWS), I("Columns", 0, ID_INSCOLS), I("Cells...", 0, ID_INSCELLS), I("Sheet", 0, ID_INSSHEET), BAR,
  I("Function...", 0, ID_FUNCTION), I("AutoSum", "=", ID_AUTOSUM), I("Today's Date", ";", ID_DATE), I("Time Now", ":", ID_TIME), BAR,
  I("Link...", 0, ID_LINK), I("Note...", 0, ID_NOTENEW), BAR,
  I("Define Name...", 0, ID_DEFNAME), I("Name Manager...", 0, ID_NAMES),
  M("Format"),
  I("Cells...", "1", ID_FORMATCELLS), I("Bold", "B", ID_BOLD), I("Italic", "I", ID_ITALIC), I("Underline", "U", ID_UNDERLINE), BAR,
  I("New or Edit Note...", 0, ID_NOTENEW), I("Delete Note", 0, ID_NOTEDEL), BAR,
  I("New Conditional Rule...", 0, ID_CONDNEW), I("Manage Conditional Rules...", 0, ID_CONDMANAGE), I("Clear Conditional Rules", 0, ID_CONDCLEAR), BAR,
  I("Wrap Text", 0, ID_WRAP), I("Merge & Centre", 0, ID_MERGECENTER), I("Increase Decimal", 0, ID_INCDEC), I("Decrease Decimal", 0, ID_DECDEC),
  I("Currency", "$", ID_CURRENCY), I("Percent", "%", ID_PERCENT), BAR,
  I("Row Height...", 0, ID_ROWHEIGHT), I("AutoFit Row Height", 0, ID_AUTOFITROW), I("Column Width...", 0, ID_COLWIDTH), I("AutoFit Column Width", 0, ID_AUTOFITCOL),
  I("Hide Rows", 0, ID_HIDEROWS), I("Hide Columns", 0, ID_HIDECOLS), I("Unhide Rows", 0, ID_UNHIDEROWS), I("Unhide Columns", 0, ID_UNHIDECOLS), BAR,
  I("Rename Sheet...", 0, ID_RENAMESHEET), I("Duplicate Sheet", 0, ID_DUPSHEET), I("Delete Sheet", 0, ID_DELSHEET),
  M("Data"),
  I("Sort A to Z", 0, ID_SORTAZ), I("Sort Z to A", 0, ID_SORTZA), I("Sort...", 0, ID_CUSTOMSORT), BAR,
  T("Filter", 0, ID_FILTER), I("Clear Filter", 0, ID_CLEARFILTER), I("Reapply Filter", 0, ID_REAPPLY), BAR, I("Remove Duplicates", 0, ID_REMOVEDUP), BAR,
  I("Calculate Now", 0, ID_CALCNOW), T("Manual Calculation", 0, ID_MANUALCALC), BAR, I("Workbook Statistics...", 0, ID_STATS),
  TABLE_MENUS,
  M("View"),
  T("Show All Notes", 0, ID_NOTESHOW), T("Formula Bar", 0, ID_FORMULABAR), T("Gridlines", 0, ID_GRIDLINES), T("Headings", 0, ID_HEADINGS), T("Show Formulas", 0, ID_SHOWFORMULAS), BAR,
  I("Zoom In", "+", ID_ZOOMIN), I("Zoom Out", "-", ID_ZOOMOUT), I("Zoom 100%", 0, ID_ZOOM100), I("Zoom to Selection", 0, ID_ZOOMSEL), BAR,
  I("Freeze Panes", 0, ID_FREEZE), I("Unfreeze Panes", 0, ID_UNFREEZE), BAR, T("Dark Mode", 0, ID_DARKUI),
  { NM_END, 0, 0, 0, 0, 0 }
};

/* ------------------------------------------------------------------ small helpers */
Sheet *cur_sheet(void) { return book->sh[book->active]; }
int cur_index(void) { return book->active; }
Rect cur_sel(void) { return cur_sheet()->sel; }
int ask(const char *gadgets, const char *fmt, const char *arg)
{
  return (int)MUI_Request(app, win, 0, (STRPTR)"Ledger", (STRPTR)tr(gadgets), (STRPTR)tr(fmt), arg);
}
static void latin1_utf8(const char *s, char *u, size_t cap)
{
  size_t o = 0;
  for (; *s && o + 4 < cap; s++) o += (size_t)utf8_put(u + o, (u8)*s);
  u[o] = 0;
}
static int xget_open(Object *w) { ULONG o = 0; if (w) get(w, MUIA_Window_Open, &o); return o != 0; }
static ULONG xget(Object *o, ULONG attr) { ULONG v = 0; get(o, attr, &v); return v; }
static void menu_set(ULONG id, ULONG attr, ULONG val)
{
  Object *strip = (Object *)xget(win, MUIA_Window_Menustrip), *it;
  if (strip && (it = (Object *)DoMethod(strip, MUIM_FindUData, id))) set(it, attr, val);
}
/* the ASL file requester, starting in path's drawer with its file name; path is replaced by the
   choice. 1 if a file was chosen */
static int file_request(int save, const char *titletext, char *path, size_t cap, const char *pattern)
{
  struct FileRequester *fr;
  char dir[400], file[108];
  int ok = 0;
  strncpy(dir, path, sizeof dir - 1); dir[sizeof dir - 1] = 0;
  *PathPart((STRPTR)dir) = 0;
  strncpy(file, path[0] ? (const char *)FilePart((STRPTR)path) : "", sizeof file - 1); file[sizeof file - 1] = 0;
  if (!(fr = (struct FileRequester *)MUI_AllocAslRequestTags(ASL_FileRequest, ASLFR_TitleText, (ULONG)tr(titletext),
        ASLFR_DoSaveMode, save, ASLFR_InitialDrawer, (ULONG)dir, ASLFR_InitialFile, (ULONG)file,
        (ULONG)(pattern ? ASLFR_InitialPattern : TAG_IGNORE), (ULONG)pattern, (ULONG)(pattern ? ASLFR_DoPatterns : TAG_IGNORE), TRUE, TAG_DONE))) return 0;
  if (MUI_AslRequestTags(fr, ASLFR_Window, (ULONG)_window(grid), TAG_DONE)) {
    strncpy(path, (const char *)fr->fr_Drawer, cap - 1); path[cap - 1] = 0;
    ok = AddPart((STRPTR)path, fr->fr_File, cap) && fr->fr_File[0];
  }
  MUI_FreeAslRequest(fr);
  return ok;
}

/* ------------------------------------------------------------------ modal dialogs */
ULONG run_modal(Object *w, const ULONG *ends, void (*each)(ULONG id))
{
  ULONG sigs = 0, id = 0;
  int i, done = 0;
  if (!w) return ID_DLGCANCEL;
  DoMethod(app, OM_ADDMEMBER, (ULONG)w);
  DoMethod(w, MUIM_Notify, MUIA_Window_CloseRequest, TRUE, (ULONG)app, 2, MUIM_Application_ReturnID, ID_DLGCANCEL);
  set(win, MUIA_Window_Sleep, TRUE);
  set(w, MUIA_Window_Open, TRUE);
  while (!done) {
    id = DoMethod(app, MUIM_Application_NewInput, (ULONG)&sigs);
    if (id == ID_DLGCANCEL || id == MUIV_Application_ReturnID_Quit) { id = ID_DLGCANCEL; break; }
    for (i = 0; ends[i]; i++) if (id == ends[i]) done = 1;
    if (done) break;
    if (id && each) each(id);
    if (sigs) sigs = Wait(sigs | SIGBREAKF_CTRL_C);
  }
  set(w, MUIA_Window_Open, FALSE);
  set(win, MUIA_Window_Sleep, FALSE);
  DoMethod(app, OM_REMMEMBER, (ULONG)w);
  return id;
}
Object *dialog_window(const char *t, Object *contents, const char *oklabel)
{
  Object *ok, *cancel, *w;
  w = QWindowObject, MUIA_Window_Title, (ULONG)tr(t), MUIA_Window_RefWindow, (ULONG)win, MUIA_Window_LeftEdge, MUIV_Window_LeftEdge_Centered,
    MUIA_Window_TopEdge, MUIV_Window_TopEdge_Centered,
    WindowContents, VGroup, QDLG,
      Child, contents,
      Child, HGroup, Child, HSpace(0), Child, ok = QButton(tr(oklabel ? oklabel : "OK")), Child, cancel = QButton(tr("Cancel")), End,
    End,
  End;
  if (!w) { if (contents) MUI_DisposeObject(contents); return NULL; }
  DoMethod(ok, MUIM_Notify, MUIA_Pressed, FALSE, (ULONG)app, 2, MUIM_Application_ReturnID, ID_DLGOK);
  DoMethod(cancel, MUIM_Notify, MUIA_Pressed, FALSE, (ULONG)app, 2, MUIM_Application_ReturnID, ID_DLGCANCEL);
  return w;
}
int ask_string(const char *t, const char *label, char *buf, int cap)
{
  Object *str, *w;
  static const ULONG ends[] = { ID_DLGOK, 0 };
  int ok;
  w = dialog_window(t, HGroup, Child, QLabel(tr(label), MUIO_Label_SingleFrame),
                    Child, str = StringObject, StringFrame, MUIA_String_Contents, (ULONG)buf, MUIA_String_MaxLen, cap, MUIA_CycleChain, 1, End, End, NULL);
  if (!w) return 0;
  DoMethod(str, MUIM_Notify, MUIA_String_Acknowledge, MUIV_EveryTime, (ULONG)app, 2, MUIM_Application_ReturnID, ID_DLGOK);
  set(w, MUIA_Window_ActiveObject, (ULONG)str);
  ok = run_modal(w, ends, NULL) == ID_DLGOK;
  if (ok) { strncpy(buf, (const char *)xget(str, MUIA_String_Contents), cap - 1); buf[cap - 1] = 0; }
  MUI_DisposeObject(w);
  return ok;
}
LONG ask_color(ULONG start)
{
  Object *r, *g, *b, *w;
  static const ULONG ends[] = { ID_DLGOK, 0 };
  LONG c = -1;
  w = dialog_window("More Colours", ColGroup(2),
                    Child, QLabel(tr("Red"), 0), Child, r = SliderObject, MUIA_Numeric_Min, 0, MUIA_Numeric_Max, 255, MUIA_Numeric_Value, (start >> 16) & 255, MUIA_CycleChain, 1, End,
                    Child, QLabel(tr("Green"), 0), Child, g = SliderObject, MUIA_Numeric_Min, 0, MUIA_Numeric_Max, 255, MUIA_Numeric_Value, (start >> 8) & 255, MUIA_CycleChain, 1, End,
                    Child, QLabel(tr("Blue"), 0), Child, b = SliderObject, MUIA_Numeric_Min, 0, MUIA_Numeric_Max, 255, MUIA_Numeric_Value, start & 255, MUIA_CycleChain, 1, End,
                    End, NULL);
  if (!w) return -1;
  if (run_modal(w, ends, NULL) == ID_DLGOK) c = (LONG)((xget(r, MUIA_Numeric_Value) << 16) | (xget(g, MUIA_Numeric_Value) << 8) | xget(b, MUIA_Numeric_Value));
  MUI_DisposeObject(w);
  return c;
}

/* ------------------------------------------------------------------ number formats */
/* (the Number list's order: General, Number, Currency, Accounting, Short Date, Long Date, Time,
   Percentage, Fraction, Scientific, Text) */
const char *numfmt_code(int cat, int dec, char *out, size_t cap)
{
  char z[12] = "";
  int i;
  if (dec > 0) { z[0] = '.'; for (i = 0; i < dec && i < 10; i++) z[i + 1] = '0'; z[i + 1] = 0; }
  switch (cat) {
  case 1: snprintf(out, cap, "#,##0%s", z); break;
  case 2: snprintf(out, cap, "$#,##0%s;-$#,##0%s", z, z); break;
  case 3: snprintf(out, cap, "_($* #,##0%s_);_($* (#,##0%s);_($* \"-\"??_);_(@_)", z, z); break;
  case 4: snprintf(out, cap, "%s", ledger_date_order ? "m/d/yyyy" : "d/mm/yyyy"); break;
  case 5: snprintf(out, cap, "d mmmm yyyy"); break;
  case 6: snprintf(out, cap, "h:mm:ss"); break;
  case 7: snprintf(out, cap, "0%s%%", z); break;
  case 8: snprintf(out, cap, "# ?/?"); break;
  case 9: snprintf(out, cap, "0%sE+00", dec > 0 ? z : ".00"); break;
  case 10: snprintf(out, cap, "@"); break;
  default: snprintf(out, cap, "General"); break;
  }
  return out;
}
const char *numfmt_name(const char *code)
{
  if (!code || !strcmp(code, "General")) return "General";
  if (!strcmp(code, "@")) return "Text";
  if (strstr(code, "_(") && strchr(code, '*')) return "Accounting";
  if (fmt_is_date(code)) {
    if (strchr(code, 'h') || strchr(code, 's')) return strchr(code, 'd') || strchr(code, 'y') ? "Date" : "Time";
    return strstr(code, "mmmm") ? "Long Date" : "Short Date";
  }
  if (strchr(code, '%')) return "Percentage";
  if (strchr(code, '?') && strchr(code, '/')) return "Fraction";
  if (strstr(code, "E+") || strstr(code, "e+")) return "Scientific";
  if (strchr(code, '$') || strstr(code, "\243") || strstr(code, "[$")) return "Currency";
  if (strchr(code, '0') || strchr(code, '#')) return "Number";
  return "Custom";
}
/* one more (dir 1) or one fewer (-1) decimal place in a format code's number sections */
static void step_decimals(const char *code, int dir, double v, char *out, size_t cap)
{
  size_t o = 0;
  const char *p = code;
  int q = 0, inbr = 0, done_sec = 0;
  if (!strcmp(code, "General")) {
    /* General: as many decimals as the value shows, plus or minus one */
    char g[40], *dot;
    int dec;
    num_general(v, g, 11);
    dot = strchr(g, '.');
    dec = dot && !strchr(g, 'E') ? (int)strlen(dot + 1) : 0;
    dec += dir;
    if (dec < 0) dec = 0;
    numfmt_code(1, dec, out, cap);
    if (out[0] == '#') memmove(out, out + 6, strlen(out + 6) + 1);     /* "0.00", without thousands */
    if (out[0] == 0 || out[0] == '.') { memmove(out + 1, out, strlen(out) + 1); out[0] = '0'; }
    return;
  }
  while (*p && o + 3 < cap) {
    char ch = *p;
    if (ch == '"') q = !q;
    if (!q && ch == '[') inbr = 1;
    if (!q && ch == ']') inbr = 0;
    if (!q && !inbr && ch == ';') done_sec = 0;
    if (!q && !inbr && !done_sec && (ch == '0' || ch == '#') && (p[1] != '0' && p[1] != '#' && p[1] != ',' && p[1] != '.')) {
      /* the last digit of the section's number */
      out[o++] = ch;
      if (dir > 0) { if (p > code && strchr(code, '.') && strrchr(code, '.') < p) out[o++] = '0'; else { out[o++] = '.'; out[o++] = '0'; } }
      else if (o >= 2 && out[o - 2] == '.') { o -= 2; out[o++] = ch == '.' ? '0' : out[o]; o--; o = o; out[o++] = '0'; o--; }
      done_sec = 1;
      p++;
      continue;
    }
    out[o++] = ch;
    p++;
  }
  out[o] = 0;
  if (dir < 0) {
    /* fewer: the last 0 after each point taken out (and the point if none are left) */
    char *s = out;
    while ((s = strchr(s, '.'))) {
      char *e = s + 1;
      while (*e == '0' || *e == '#') e++;
      if (e > s + 1) { memmove(e - 1, e, strlen(e) + 1); if (e - 1 == s + 1) memmove(s, s + 1, strlen(s + 1) + 1); }
      s++;
    }
  }
}

/* ------------------------------------------------------------------ formatting */
static void fmt_edit(Style *st, Font *f, const void *arg)
{
  const FmtEdit *e = (const FmtEdit *)arg;
  switch (e->what) {
  case FX_BOLD: f->bold = (u8)e->value; break;
  case FX_ITALIC: f->italic = (u8)e->value; break;
  case FX_UNDERLINE: f->underline = (u8)e->value; break;
  case FX_STRIKE: f->strike = (u8)e->value; break;
  case FX_FONT: latin1_utf8(e->text, f->name, sizeof f->name); break;
  case FX_SIZE: f->size = (u16)e->value; break;
  case FX_GROW: {
    static const u16 steps[] = { 80, 90, 100, 110, 120, 140, 160, 180, 200, 220, 240, 260, 280, 360, 480, 720 };
    int i;
    if (e->value > 0) { for (i = 0; i < 16 && steps[i] <= f->size; i++) ; if (i < 16) f->size = steps[i]; }
    else { for (i = 15; i >= 0 && steps[i] >= f->size; i--) ; if (i >= 0) f->size = steps[i]; }
    break; }
  case FX_COLOR: f->color = (u32)e->value; break;
  case FX_FILL: st->fill = e->value == (long)COLOR_AUTO ? 0 : FILL_ON | ((u32)e->value & 0xFFFFFF); break;
  case FX_HALIGN: st->halign = (u8)e->value; break;
  case FX_VALIGN: st->valign = (u8)e->value; break;
  case FX_WRAP: st->wrap = (u8)e->value; break;
  case FX_INDENT: { int n = st->indent + (int)e->value; st->indent = (u8)(n < 0 ? 0 : n > 15 ? 15 : n); if (n > 0 && st->halign == HA_GENERAL) st->halign = HA_LEFT; break; }
  case FX_NUMFMT: st->numfmt = (u16)book_fmt(book, e->text); break;
  case FX_STYLE: {
    unsigned m = (unsigned)e->value;
    if (m & SM_NUMFMT) st->numfmt = e->st->numfmt;
    if (m & SM_HALIGN) st->halign = e->st->halign;
    if (m & SM_VALIGN) st->valign = e->st->valign;
    if (m & SM_WRAP) st->wrap = e->st->wrap;
    if (m & SM_INDENT) st->indent = e->st->indent;
    if (m & SM_FONT) *f = *e->font;
    if (m & SM_BORDER) { memcpy(st->border, e->st->border, sizeof st->border); memcpy(st->bcolor, e->st->bcolor, sizeof st->bcolor); }
    if (m & SM_FILL) st->fill = e->st->fill;
    break; }
  }
}
void apply_format(int what, long value, const char *text)
{
  FmtEdit e;
  memset(&e, 0, sizeof e);
  e.what = what; e.value = value; e.text = text;
  book_restyle(book, cur_index(), cur_sel(), fmt_edit, &e);
  refresh_all();
}
void apply_style(const Style *st, const Font *f, unsigned mask)
{
  FmtEdit e;
  memset(&e, 0, sizeof e);
  e.what = FX_STYLE; e.value = (long)mask; e.st = st; e.font = f;
  book_restyle(book, cur_index(), cur_sel(), fmt_edit, &e);
  refresh_all();
}
/* the active cell's style and font */
static const Style *cur_style(void) { return &book->styles[sheet_cell_style(cur_sheet(), cur_sheet()->cur_r, cur_sheet()->cur_c)]; }
static const Font *cur_font(void) { return &book->fonts[cur_style()->font]; }
/* borders: the presets of the Borders list on the selection */
typedef struct { int preset; Rect a; } BorderEdit;
static BorderEdit bedit;
static void border_one(Style *st, Font *f, const void *arg) { (void)f; memcpy(st->border, ((const Style *)arg)->border, sizeof st->border); }
static void set_borders(Rect a, int left, int right, int top, int bottom)
{
  Style s;
  memset(&s, 0, sizeof s);
  s.border[B_LEFT] = (u8)left; s.border[B_RIGHT] = (u8)right; s.border[B_TOP] = (u8)top; s.border[B_BOTTOM] = (u8)bottom;
  book_restyle(book, cur_index(), a, border_one, &s);
}
typedef struct { int side; int kind; } SideEdit;
static void side_one(Style *st, Font *f, const void *arg) { const SideEdit *e = (const SideEdit *)arg; (void)f; st->border[e->side] = (u8)e->kind; st->bcolor[e->side] = COLOR_AUTO; }
static void border_side(Rect a, int side, int kind) { SideEdit e; e.side = side; e.kind = kind; book_restyle(book, cur_index(), a, side_one, &e); }
static void border_preset(int k)
{
  Rect a = cur_sel(), e;
  book_begin(book);
  switch (k) {
  case 0: e = a; e.r1 = a.r2; border_side(e, B_BOTTOM, BS_THIN); break;
  case 1: e = a; e.r2 = a.r1; border_side(e, B_TOP, BS_THIN); break;
  case 2: e = a; e.c2 = a.c1; border_side(e, B_LEFT, BS_THIN); break;
  case 3: e = a; e.c1 = a.c2; border_side(e, B_RIGHT, BS_THIN); break;
  case 4: set_borders(a, 0, 0, 0, 0); break;
  case 5: set_borders(a, BS_THIN, BS_THIN, BS_THIN, BS_THIN); break;
  case 6: case 7: {
    int kd = k == 7 ? BS_THICK : BS_THIN;
    e = a; e.r2 = a.r1; border_side(e, B_TOP, kd);
    e = a; e.r1 = a.r2; border_side(e, B_BOTTOM, kd);
    e = a; e.c2 = a.c1; border_side(e, B_LEFT, kd);
    e = a; e.c1 = a.c2; border_side(e, B_RIGHT, kd);
    break; }
  case 8: e = a; e.r1 = a.r2; border_side(e, B_BOTTOM, BS_DOUBLE); break;
  case 9: e = a; e.r1 = a.r2; border_side(e, B_BOTTOM, BS_THICK); break;
  case 10: e = a; e.r2 = a.r1; border_side(e, B_TOP, BS_THIN); e = a; e.r1 = a.r2; border_side(e, B_BOTTOM, BS_THIN); break;
  }
  book_end(book);
  (void)bedit;
  refresh_all();
}
/* a cell style from the Cell Styles list */
static void cell_style(int k)
{
  ULONG fill, color;
  int bold, line, size;
  Style st;
  Font f = *cur_font();
  Rect a = cur_sel(), e;
  if (!ribbon_cell_style(k, &fill, &color, &bold, &line, &size)) return;
  memset(&st, 0, sizeof st);
  st.fill = fill ? FILL_ON | fill : 0;
  f.color = color; f.bold = (u8)bold;
  if (size) f.size = (u16)(size * 10);
  else if (k == 0) f.size = 110;
  book_begin(book);
  apply_style(&st, &f, SM_FILL | SM_FONT);
  set_borders(a, 0, 0, 0, 0);
  if (line) {
    e = a; e.r1 = a.r2;
    border_side(e, B_BOTTOM, line == 2 ? BS_THICK : line == 3 ? BS_DOUBLE : BS_THIN);
    if (line == 3) { e = a; e.r2 = a.r1; border_side(e, B_TOP, BS_THIN); }
  }
  book_end(book);
  refresh_all();
}

/* ------------------------------------------------------------------ the controls follow the selection */
static char fx_shown[2100];
static void status_update(void)
{
  Sheet *s = cur_sheet();
  Rect a = cur_sel();
  struct MUIP_Status_Set m;
  char stats[160] = "";
  ULONG editing = xget(grid, MUIA_Grid_Editing);
  LONG zoom = s->zoom ? s->zoom : 100;
  if (a.r1 != a.r2 || a.c1 != a.c2) {
    /* the selection's numbers: average, count and sum (the used part only) */
    i32 r, c, r2 = a.r2 > sheet_last_row(s) ? sheet_last_row(s) : a.r2, c2 = a.c2 > sheet_last_col(s) ? sheet_last_col(s) : a.c2;
    long count = 0, nums = 0, seen = 0;
    double sum = 0;
    for (r = a.r1; r <= r2 && seen < 200000L; r++) {
      Row *row = r < s->nrows ? s->rows[r] : NULL;
      int i;
      if (!row || row->filtered || row->hidden) continue;     /* (hidden and filtered rows don't count) */
      for (i = 0; i < row->n; i++) {
        Cell *cell = &row->c[i];
        const Value *v;
        seen++;
        if (cell->col < a.c1 || cell->col > c2) continue;
        if (!cell->f && cell->v.t == V_EMPTY) continue;
        v = cell_value(book, cur_index(), r, cell->col);
        if (v->t == V_EMPTY) continue;
        count++;
        if (v->t == V_NUM) { nums++; sum += v->n; }
      }
    }
    (void)c;
    if (nums) {
      char g1[32], g2[32];
      num_general(sum / nums, g1, 11); num_general(sum, g2, 11);
      snprintf(stats, sizeof stats, "%s: %s    %s: %ld    %s: %s", tr("Average"), g1, tr("Count"), count, tr("Sum"), g2);
    } else if (count > 1) snprintf(stats, sizeof stats, "%s: %ld", tr("Count"), count);
  }
  memset(&m, 0, sizeof m);
  m.MethodID = MUIM_Status_Set;
  m.mode = editing == 1 ? "Enter" : editing == 2 ? "Edit" : "Ready";
  m.stats = stats;
  m.zoom = zoom;
  m.manual = book->manualcalc;
  DoMethodA(statusbar, (Msg)&m);
}
void update_controls(void)
{
  Sheet *s = cur_sheet();
  const Style *st = cur_style();
  const Font *f = &book->fonts[st->font];
  struct MUIP_Ribbon_State rs;
  Rect a = cur_sel(), m;
  char ref[40], t[2100];
  ULONG on = 0;
  /* the name box: the active cell, the selection's size while it is dragged, or its name */
  {
    int i;
    ref_text(s->cur_r, s->cur_c, 0, 0, ref);
    for (i = 0; i < book->nnames; i++) {
      u16 sh; Rect nr; u8 fl;
      const char *ft = book->names[i].formula_text;
      if (ft && parse_ref(book, ft[0] == '=' ? ft + 1 : ft, &sh, &nr, &fl) && (int)strlen(ft[0] == '=' ? ft + 1 : ft) == parse_ref(book, ft[0] == '=' ? ft + 1 : ft, &sh, &nr, &fl) &&
          (sh == SHEET_SELF || sh == cur_index()) && nr.r1 == a.r1 && nr.r2 == a.r2 && nr.c1 == a.c1 && nr.c2 == a.c2) {
        utf8_to_latin1(book->names[i].name, (u32)strlen(book->names[i].name), ref, sizeof ref);
        break;
      }
    }
    if (strcmp((const char *)xget(namebox, MUIA_String_Contents), ref)) SetAttrs(namebox, MUIA_NoNotify, TRUE, MUIA_String_Contents, (ULONG)ref, TAG_DONE);
  }
  /* the formula bar: what is being typed, or the cell's entry */
  if (xget(grid, MUIA_Grid_Editing)) { strncpy(t, (const char *)xget(grid, MUIA_Grid_EditText), sizeof t - 1); t[sizeof t - 1] = 0; }
  else grid_edit_text(book, cur_index(), s->cur_r, s->cur_c, t, sizeof t);
  if (strcmp(t, fx_shown)) { strcpy(fx_shown, t); SetAttrs(fxbar, MUIA_NoNotify, TRUE, MUIA_String_Contents, (ULONG)t, TAG_DONE); }
  /* the ribbon */
  if (f->bold) on |= RBIT(RB_BOLD);
  if (f->italic) on |= RBIT(RB_ITALIC);
  if (f->underline) on |= RBIT(RB_UNDERLINE);
  if (f->strike) on |= RBIT(RB_STRIKE);
  if (book_can_undo(book)) on |= RBIT(RB_CANUNDO);
  if (book_can_redo(book)) on |= RBIT(RB_CANREDO);
  if (st->wrap) on |= RBIT(RB_WRAP);
  if (sheet_merge_at(s, s->cur_r, s->cur_c, &m)) on |= RBIT(RB_MERGED);
  if (st->halign == HA_LEFT) on |= RBIT(RB_ALEFT);
  if (st->halign == HA_CENTER || st->halign == HA_CENTERSEL) on |= RBIT(RB_ACENTER);
  if (st->halign == HA_RIGHT) on |= RBIT(RB_ARIGHT);
  on |= RBIT(st->valign == VA_TOP ? RB_ATOP : st->valign == VA_CENTER ? RB_AMIDDLE : RB_ABOTTOM);
  if (s->gridlines) on |= RBIT(RB_GRIDLINES);
  if (s->headings) on |= RBIT(RB_HEADINGS);
  if (s->showzeros) on |= RBIT(RB_ZEROS);
  if (pref_fxbar) on |= RBIT(RB_FORMULABAR);
  if (xget(grid, MUIA_Grid_Formulas)) on |= RBIT(RB_FORMULAS);
  if (book->manualcalc) on |= RBIT(RB_MANUAL);
  if (xget(grid, MUIA_Grid_Brush)) on |= RBIT(RB_BRUSH);
  if (pref_dark) on |= RBIT(RB_DARK);
  if (s->freezer || s->freezec) on |= RBIT(RB_FROZEN);
  if (s->filter.r1 >= 0) on |= RBIT(RB_FILTER);
  if (xget(grid, MUIA_Grid_ShowNotes)) on |= RBIT(RB_NOTES);
  if (sheet_note(s, s->cur_r, s->cur_c)) on |= RBIT(RB_HASNOTE);
  if (a.r1 != a.r2 || a.c1 != a.c2) on |= RBIT(RB_MULTI);
  memset(&rs, 0, sizeof rs);
  rs.MethodID = MUIM_Ribbon_State;
  rs.on = on; rs.font = f->name; rs.size = f->size; rs.numfmt = numfmt_name(book_fmt_code(book, st->numfmt)); rs.fs = &fonts;
  tabletab_state(&rs.ctx, &rs.ctxname);     /* (Table Design, PivotTable Analyze and Design) */
  DoMethodA(ribbon, (Msg)&rs);
  /* the menus' ticks */
  menu_set(ID_GRIDLINES, MUIA_Menuitem_Checked, s->gridlines);
  menu_set(ID_FILTER, MUIA_Menuitem_Checked, s->filter.r1 >= 0);
  menu_set(ID_NOTESHOW, MUIA_Menuitem_Checked, xget(grid, MUIA_Grid_ShowNotes) != 0);
  menu_set(ID_HEADINGS, MUIA_Menuitem_Checked, s->headings);
  menu_set(ID_FORMULABAR, MUIA_Menuitem_Checked, pref_fxbar);
  menu_set(ID_SHOWFORMULAS, MUIA_Menuitem_Checked, xget(grid, MUIA_Grid_Formulas));
  menu_set(ID_MANUALCALC, MUIA_Menuitem_Checked, book->manualcalc);
  menu_set(ID_DARKUI, MUIA_Menuitem_Checked, pref_dark);
  status_update();
}
static void set_title(void)
{
  char name[256];
  strncpy(name, bookpath[0] ? (const char *)FilePart((STRPTR)bookpath) : tr("Book1"), sizeof name - 1); name[sizeof name - 1] = 0;
  snprintf(title, sizeof title, "%s%s - Ledger", book->dirty ? "* " : "", name);
  set(win, MUIA_Window_Title, (ULONG)title);
}
void refresh_all(void)
{
  DoMethod(grid, MUIM_Grid_Refresh);
  DoMethod(tabs, MUIM_Tabs_Refresh);
  update_controls();
  set_title();
}

/* ------------------------------------------------------------------ recent files and preferences */
#define MAXRECENT 8
static char recent[MAXRECENT][256];
static int nrecent;
static void load_recent(void)
{
  FILE *f = fopen("PROGDIR:Ledger.recent", "r");
  char line[300];
  nrecent = 0;
  if (!f) return;
  while (nrecent < MAXRECENT && fgets(line, sizeof line, f)) {
    line[strcspn(line, "\r\n")] = 0;
    if (line[0]) { strncpy(recent[nrecent], line, sizeof recent[0] - 1); recent[nrecent][sizeof recent[0] - 1] = 0; nrecent++; }
  }
  fclose(f);
}
static void add_recent(const char *path)
{
  int i, k;
  FILE *f;
  for (i = 0; i < nrecent && Stricmp((STRPTR)recent[i], (STRPTR)path); i++) ;
  if (i == nrecent) { if (nrecent < MAXRECENT) nrecent++; i = nrecent - 1; }
  for (k = i; k > 0; k--) strcpy(recent[k], recent[k - 1]);
  strncpy(recent[0], path, sizeof recent[0] - 1); recent[0][sizeof recent[0] - 1] = 0;
  if ((f = fopen("PROGDIR:Ledger.recent", "w"))) { for (i = 0; i < nrecent; i++) fprintf(f, "%s\n", recent[i]); fclose(f); }
}
static void load_prefs(void)
{
  FILE *f = fopen("PROGDIR:Ledger.prefs", "r");
  char line[200];
  if (!f) return;
  while (fgets(line, sizeof line, f)) {
    line[strcspn(line, "\r\n")] = 0;
    if (!strncmp(line, "theme=", 6)) pref_dark = !strcmp(line + 6, "dark");
    else if (!strncmp(line, "textsize=", 9)) pref_large = !strcmp(line + 9, "large");
    else if (!strncmp(line, "dates=", 6)) ledger_date_order = !strcmp(line + 6, "mdy");
    else if (!strncmp(line, "formulabar=", 11)) pref_fxbar = atoi(line + 11);
    else if (!strncmp(line, "name=", 5)) { strncpy(username, line + 5, sizeof username - 1); username[sizeof username - 1] = 0; }
    else if (!strcmp(line, "aatext=off")) theme_no_aatext();
    else if (!strcmp(line, "hinting=off")) theme_no_hinting();
    else if (!strncmp(line, "screen=", 7)) { strncpy(pub_screen, line + 7, sizeof pub_screen - 1); pub_screen[sizeof pub_screen - 1] = 0; }
    else if (!strncmp(line, "windowbox=", 10)) {
      long b[4] = { 0, 0, 0, 0 };
      if (sscanf(line + 10, "%ld,%ld,%ld,%ld", &b[0], &b[1], &b[2], &b[3]) == 4 && b[2] >= 200 && b[3] >= 150)
        { win_box[0] = b[0]; win_box[1] = b[1]; win_box[2] = b[2]; win_box[3] = b[3]; }
    }
  }
  fclose(f);
}
static void save_prefs(void)
{
  FILE *f = fopen("PROGDIR:Ledger.prefs", "w");
  if (!f) return;
  fprintf(f, "theme=%s\ntextsize=%s\ndates=%s\nformulabar=%d\n", pref_dark ? "dark" : "light", pref_large ? "large" : "normal",
          ledger_date_order ? "mdy" : "dmy", pref_fxbar);
  if (username[0]) fprintf(f, "name=%s\n", username);
  if (pub_screen[0]) fprintf(f, "screen=%s\n", pub_screen);
  if (win_box[2] > 0) fprintf(f, "windowbox=%ld,%ld,%ld,%ld\n", (long)win_box[0], (long)win_box[1], (long)win_box[2], (long)win_box[3]);
  fclose(f);
}

/* ------------------------------------------------------------------ files */
static int win_new(Book *b, const char *path);
static int win_find(const char *path);
static int book_untouched(void) { return !book->dirty && !bookpath[0] && book->nsh == 1 && sheet_last_row(book->sh[0]) < 0; }
/* the workbook in path read (.xlsx, or CSV/text) into a new Book; NULL with a message if it can't be */
static Book *load_file(const char *path)
{
  size_t len;
  u8 *data = file_read(path, &len);
  Book *b = NULL;
  char err[200] = "";
  if (!data) { ask("OK", "Ledger couldn't read\n%s", path); return NULL; }
  set(win, MUIA_Window_Sleep, TRUE);
  if (len >= 4 && data[0] == 'P' && data[1] == 'K') b = xlsx_load(data, len, err, sizeof err);
  else if (len >= 8 && data[0] == 0xD0 && data[1] == 0xCF) strcpy(err, "This is an old Excel file (.xls). Save it as .xlsx in Excel or GridCraft first.");
  else b = csv_load((const char *)data, len, 0);
  set(win, MUIA_Window_Sleep, FALSE);
  free(data);
  if (!b) {
    static char msg[400];
    snprintf(msg, sizeof msg, "%s\n\n%s", tr("Ledger couldn't open this file:"), err[0] ? err : tr("it isn't a workbook Ledger can read."));
    ask("OK", "%s", msg);
  }
  return b;
}
static void show_book(Book *b, const char *path)
{
  Book *old = book;
  book = b;
  strncpy(bookpath, path ? path : "", sizeof bookpath - 1); bookpath[sizeof bookpath - 1] = 0;
  set(grid, MUIA_Grid_Book, (ULONG)book);
  set(tabs, MUIA_Tabs_Book, (ULONG)book);
  fx_shown[0] = 1; fx_shown[1] = 0;
  if (old && old != b) book_free(old);
  refresh_all();
}
static void open_path(const char *path)
{
  Book *b;
  if (win_find(path)) return;
  if (!(b = load_file(path))) return;
  if (book_untouched()) show_book(b, path);
  else win_new(b, path);
  add_recent(path);
}
static int save_to(const char *path)
{
  size_t n = strlen(path);
  int ok = 0, csv = n > 4 && !Stricmp((STRPTR)path + n - 4, (STRPTR)".csv");
  set(win, MUIA_Window_Sleep, TRUE);
  if (csv) {
    char *t = csv_save(book, cur_index(), ',');
    ok = t && file_write(path, t, strlen(t));
    free(t);
  } else {
    size_t len;
    u8 *data;
    if (username[0] && !book->author[0]) latin1_utf8(username, book->author, sizeof book->author);
    data = xlsx_save(book, &len);
    ok = data && file_write(path, data, len);
    free(data);
  }
  set(win, MUIA_Window_Sleep, FALSE);
  if (!ok) { ask("OK", "Ledger couldn't save\n%s\n(Is the disk full or write-protected?)", path); return 0; }
  if (!csv) { book->dirty = 0; strncpy(bookpath, path, sizeof bookpath - 1); bookpath[sizeof bookpath - 1] = 0; }
  add_recent(path);
  set_title();
  return 1;
}
static int save_as(int csv)
{
  char path[512];
  strcpy(path, bookpath[0] ? bookpath : "Book1.xlsx");
  if (csv) { char *dot = strrchr(path, '.'); if (dot && dot > (char *)FilePart((STRPTR)path)) strcpy(dot, ".csv"); else strncat(path, ".csv", sizeof path - strlen(path) - 1); }
  if (!file_request(1, csv ? "Save Sheet as CSV" : "Save Workbook As", path, sizeof path, csv ? "#?.csv" : "#?.xlsx")) return 0;
  if (!csv && !strchr((const char *)FilePart((STRPTR)path), '.')) strncat(path, ".xlsx", sizeof path - strlen(path) - 1);
  return save_to(path);
}
static int save(void) { return bookpath[0] ? save_to(bookpath) : save_as(0); }
static int may_discard(void)
{
  int r;
  if (xget(grid, MUIA_Grid_Editing)) DoMethod(grid, MUIM_Grid_Commit, 0);
  if (!book->dirty) return 1;
  r = ask("Save|Don't Save|Cancel", "Save the changes to %s?", bookpath[0] ? (const char *)FilePart((STRPTR)bookpath) : tr("Book1"));
  if (r == 1) return save();
  return r == 2;
}

/* ------------------------------------------------------------------ the clipboard */
static Clip *clip;               /* what Ledger copied last (any window) */
static char *clip_text;          /* and its text, to tell whether the clipboard still holds it */
static Book *cut_book; static int cut_sheet; static Rect cut_area; static int cut_pending;
static void copy_cells(int cut)
{
  Rect a = cur_sel();
  char *t, l1[1];
  clip_free(clip);
  clip = clip_copy(book, cur_index(), a);
  t = book_copy_text(book, cur_index(), a);
  free(clip_text); clip_text = NULL;
  if (t) {
    size_t n = strlen(t) + 1;
    if ((clip_text = (char *)malloc(n))) { utf8_to_latin1(t, (u32)strlen(t), clip_text, (u32)n); clip_write(clip_text, (long)strlen(clip_text)); }
    free(t);
  }
  (void)l1;
  cut_pending = cut; cut_book = book; cut_sheet = cur_index(); cut_area = a;
  set(grid, MUIA_Grid_Copied, (ULONG)&a);
}
static void paste(int what, int transpose)
{
  Sheet *s = cur_sheet();
  long len = 0;
  char *t = clip_read(&len);
  Rect out;
  int ours = clip && (!t || (clip_text && !strcmp(t, clip_text)));
  if (ours) {
    book_begin(book);
    book_paste(book, cur_index(), s->cur_r, s->cur_c, clip, what, transpose, &out);
    if (cut_pending && cut_book == book && !transpose) {
      /* a cut: the cells left behind go, except where they were pasted over */
      i32 r, c;
      for (r = cut_area.r1; r <= cut_area.r2; r++)
        for (c = cut_area.c1; c <= cut_area.c2; c++)
          if (!(cut_sheet == cur_index() && r >= out.r1 && r <= out.r2 && c >= out.c1 && c <= out.c2)) {
            Rect one; one.r1 = one.r2 = r; one.c1 = one.c2 = c;
            book_clear(book, cut_sheet, one, CLR_ALL);
          }
      cut_pending = 0;
      set(grid, MUIA_Grid_Copied, 0);
    }
    book_end(book);
  } else if (t) {
    char *u = (char *)malloc((size_t)len * 3 + 4);
    if (u) { latin1_utf8(t, u, (size_t)len * 3 + 4); book_paste_text(book, cur_index(), s->cur_r, s->cur_c, u, &out); free(u); }
  } else { free(t); return; }
  free(t);
  DoMethod(grid, MUIM_Grid_Select, out.r1, out.c1, out.r2, out.c2, out.r1, out.c1);
  refresh_all();
}

/* ------------------------------------------------------------------ the format painter */
static Style brush_st; static Font brush_font; static int brush_pending;
/* the grid has let go of the mouse after a selection with the painter armed */
static void brush_done(void)
{
  if (!brush_pending || xget(grid, MUIA_Grid_Brush)) return;
  brush_pending = 0;
  apply_style(&brush_st, &brush_font, SM_NUMFMT | SM_HALIGN | SM_VALIGN | SM_WRAP | SM_INDENT | SM_FONT | SM_BORDER | SM_FILL);
}

/* ------------------------------------------------------------------ commands on the selection */
/* the block of filled cells round the active cell (what Sort and the like work on when one cell is selected) */
static Rect current_region(void)
{
  Sheet *s = cur_sheet();
  Rect a = cur_sel();
  int grew = 1;
  if (a.r1 != a.r2 || a.c1 != a.c2) {
    if (a.r2 >= MAXROW - 1) a.r2 = sheet_last_row(s) > a.r1 ? sheet_last_row(s) : a.r1;
    if (a.c2 >= MAXCOL - 1) a.c2 = sheet_last_col(s) > a.c1 ? sheet_last_col(s) : a.c1;
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
/* whether the area's first row looks like headings: text over a row holding numbers */
static int has_header(Rect a)
{
  i32 c;
  int text = 0, nums = 0;
  if (a.r2 <= a.r1) return 0;
  for (c = a.c1; c <= a.c2; c++) {
    const Value *v = cell_value(book, cur_index(), a.r1, c), *w = cell_value(book, cur_index(), a.r1 + 1, c);
    if (v->t == V_STR) text++;
    if (w->t == V_NUM) nums++;
    if (v->t == V_NUM) return 0;
  }
  return text > 0 && (nums > 0 || text == a.c2 - a.c1 + 1);
}
static void sort_quick(int desc)
{
  Rect a = current_region();
  SortKey k;
  k.col = cur_sheet()->cur_c; k.desc = desc;
  book_sort(book, cur_index(), a, &k, 1, has_header(a));
  refresh_all();
}
/* the active cell's link followed: a place in the workbook is gone to; anything else goes to the
   web browser (or mail program) through openurl.library, which most Amiga browsers install, and
   without it the address is shown */
static ULONG URL_OpenA_(__reg("a6") struct Library *base, __reg("a0") STRPTR url, __reg("a1") struct TagItem *tags) = "\tjsr\t-30(a6)";
static void link_open(void)
{
  Sheet *s = cur_sheet();
  char u[600], lat[600];
  struct Library *ou;
  if (!book_cell_link(book, cur_index(), s->cur_r, s->cur_c, u, sizeof u)) { DisplayBeep(NULL); return; }
  utf8_to_latin1(u, (u32)strlen(u), lat, sizeof lat);
  if (u[0] == '#') {
    u16 sh = SHEET_SELF; Rect r; u8 fl;
    int n = parse_ref(book, u + 1, &sh, &r, &fl);
    if (!n) {
      /* a defined name */
      int i;
      for (i = 0; i < book->nnames && !str_ieq(book->names[i].name, u + 1); i++) ;
      if (i < book->nnames && book->names[i].formula_text) n = parse_ref(book, book->names[i].formula_text, &sh, &r, &fl);
    }
    if (!n) { ask("OK", "This link goes to \"%s\", which isn't a cell in this workbook.", lat + 1); return; }
    if (sh != SHEET_SELF && sh < book->nsh && sh != cur_index()) { book->active = sh; set(grid, MUIA_Grid_Book, (ULONG)book); }
    DoMethod(grid, MUIM_Grid_Select, r.r1, r.c1, r.r2, r.c2, r.r1, r.c1);
    refresh_all();
    return;
  }
  if ((ou = OpenLibrary((STRPTR)"openurl.library", 1))) {
    ULONG ok = URL_OpenA_(ou, (STRPTR)lat, NULL);
    CloseLibrary(ou);
    if (ok) return;
  }
  ask("OK", "Ledger couldn't open the link, which goes to\n\n%s\n\n(openurl.library and a web browser open links.)", lat);
}
/* the next (or previous) note, by rows then columns, selected and shown */
static void note_step(int fwd)
{
  Sheet *s = cur_sheet();
  int i, best = -1;
  long here = (long)s->cur_r * MAXCOL + s->cur_c, bk = 0;
  if (!s->nnotes) { ask("OK", "%s", tr("There are no notes on this sheet.")); return; }
  for (i = 0; i < s->nnotes; i++) {
    long k = (long)s->notes[i].r * MAXCOL + s->notes[i].c;
    if (fwd ? k > here && (best < 0 || k < bk) : k < here && (best < 0 || k > bk)) { best = i; bk = k; }
  }
  if (best < 0)   /* round to the first (or last) */
    for (i = 0; i < s->nnotes; i++) {
      long k = (long)s->notes[i].r * MAXCOL + s->notes[i].c;
      if (best < 0 || (fwd ? k < bk : k > bk)) { best = i; bk = k; }
    }
  DoMethod(grid, MUIM_Grid_Select, s->notes[best].r, s->notes[best].c, s->notes[best].r, s->notes[best].c, s->notes[best].r, s->notes[best].c);
  refresh_all();
}
/* AutoFilter on (buttons on the heading row of the block round the active cell) or off */
static void filter_toggle(void)
{
  Sheet *s = cur_sheet();
  if (s->filter.r1 >= 0) book_autofilter(book, cur_index(), s->filter, 0);
  else {
    Rect a = current_region();
    if (a.r1 == a.r2 && !sheet_cell(s, a.r1, a.c1) && a.c1 == a.c2) { ask("OK", "%s", tr("Select a cell in a table of data first.")); return; }
    book_autofilter(book, cur_index(), a, 1);
  }
  refresh_all();
}
static void filter_clear(void)
{
  Sheet *s = cur_sheet();
  while (s->nfcols) book_filter_column(book, cur_index(), s->fcols[0].col, NULL, -1, 0);
  refresh_all();
}
static void remove_dups(void)
{
  Rect a = current_region();
  int hdr = has_header(a), n;
  static char msg[200];
  n = book_remove_duplicates(book, cur_index(), a, 0, hdr);
  refresh_all();
  snprintf(msg, sizeof msg, n ? tr("%d duplicate rows were found and removed.") : tr("No duplicate rows were found."), n);
  ask("OK", "%s", msg);
}
/* AutoSum and the like: a function over the numbers above (or to the left of) the active cell,
   typed into it ready to be checked; with several cells selected, a total under each column */
static void autosum(const char *fn)
{
  Sheet *s = cur_sheet();
  Rect a = cur_sel();
  char t[80], r1[16], r2[16];
  if (a.r1 != a.r2 || a.c1 != a.c2) {
    i32 c, below = a.r2 + 1;
    book_begin(book);
    for (c = a.c1; c <= a.c2; c++) {
      ref_text(a.r1, c, 0, 0, r1); ref_text(a.r2, c, 0, 0, r2);
      snprintf(t, sizeof t, "=%s(%s:%s)", fn, r1, r2);
      book_input(book, cur_index(), below, c, t, NULL);
    }
    book_end(book);
    refresh_all();
    return;
  }
  {
    i32 r = s->cur_r, c = s->cur_c, k;
    if (r > 0 && cell_value(book, cur_index(), r - 1, c)->t == V_NUM) {
      for (k = r - 1; k > 0 && cell_value(book, cur_index(), k - 1, c)->t == V_NUM; k--) ;
      ref_text(k, c, 0, 0, r1); ref_text(r - 1, c, 0, 0, r2);
    } else if (c > 0 && cell_value(book, cur_index(), r, c - 1)->t == V_NUM) {
      for (k = c - 1; k > 0 && cell_value(book, cur_index(), r, k - 1)->t == V_NUM; k--) ;
      ref_text(r, k, 0, 0, r1); ref_text(r, c - 1, 0, 0, r2);
    } else { snprintf(t, sizeof t, "=%s()", fn); DoMethod(grid, MUIM_Grid_Edit, (ULONG)t, 1); DoMethod(grid, MUIM_Grid_Insert, (ULONG)"", 1); return; }
    snprintf(t, sizeof t, "=%s(%s:%s)", fn, r1, r2);
    DoMethod(grid, MUIM_Grid_Edit, (ULONG)t, 1);
  }
}
/* a function chosen from a list: typed at the caret if a formula is being written, else a new one */
static void insert_function(const char *name)
{
  char t[64];
  if (!name) return;
  if (xget(grid, MUIA_Grid_Editing)) { snprintf(t, sizeof t, "%s()", name); DoMethod(grid, MUIM_Grid_Insert, (ULONG)t, 1); }
  else { snprintf(t, sizeof t, "=%s()", name); DoMethod(grid, MUIM_Grid_Edit, (ULONG)t, 1); DoMethod(grid, MUIM_Grid_Insert, (ULONG)"", 1); }
  set(win, MUIA_Window_ActiveObject, (ULONG)grid);
}
static void fill(int dir)
{
  Rect a = cur_sel(), src = a;
  if (dir == 0) { if (a.r1 == a.r2 && a.r1 > 0) { src.r1 = src.r2 = a.r1 - 1; a.r1--; } else src.r2 = a.r1; }
  else if (dir == 1) { if (a.c1 == a.c2 && a.c1 > 0) { src.c1 = src.c2 = a.c1 - 1; a.c1--; } else src.c2 = a.c1; }
  else if (dir == 2) { src.r1 = a.r2; }
  else { src.c1 = a.c2; }
  /* "Down" copies: the one cell repeated, as Excel's Fill Down does (not a series) */
  {
    Clip *cl = clip_copy(book, cur_index(), src);
    i32 r, c;
    if (!cl) return;
    book_begin(book);
    if (dir == 0 || dir == 2) for (r = a.r1; r <= a.r2; r++) { if (r >= src.r1 && r <= src.r2) continue; book_paste(book, cur_index(), r, a.c1, cl, PASTE_ALL, 0, NULL); }
    else for (c = a.c1; c <= a.c2; c++) { if (c >= src.c1 && c <= src.c2) continue; book_paste(book, cur_index(), a.r1, c, cl, PASTE_ALL, 0, NULL); }
    book_end(book);
    clip_free(cl);
  }
  refresh_all();
}
static void merge(int how)
{
  Rect a = cur_sel(), m;
  Sheet *s = cur_sheet();
  if (how == 3 || (how == 0 && sheet_merge_at(s, s->cur_r, s->cur_c, &m) && a.r1 == m.r1 && a.c1 == m.c1 && a.r2 == m.r2 && a.c2 == m.c2)) {
    /* unmerge (Merge & Centre on a merged cell takes it apart again) */
    book_merge(book, cur_index(), a, 0);
    refresh_all();
    return;
  }
  if (a.r1 == a.r2 && a.c1 == a.c2) return;
  if (how == 1) {
    i32 r;
    book_begin(book);
    for (r = a.r1; r <= a.r2; r++) { Rect row = a; row.r1 = row.r2 = r; book_merge(book, cur_index(), row, 1); }
    book_end(book);
  } else {
    book_begin(book);
    book_merge(book, cur_index(), a, 1);
    if (how == 0) { FmtEdit e; memset(&e, 0, sizeof e); e.what = FX_HALIGN; e.value = HA_CENTER; book_restyle(book, cur_index(), a, fmt_edit, &e); }
    book_end(book);
  }
  refresh_all();
}
static void rows_cols(ULONG id)
{
  Rect a = cur_sel();
  int sh = cur_index();
  switch (id) {
  case ID_INSROWS: book_insert_rows(book, sh, a.r1, a.r2 - a.r1 + 1 > 5000 ? 1 : a.r2 - a.r1 + 1); break;
  case ID_INSCOLS: book_insert_cols(book, sh, a.c1, a.c2 - a.c1 + 1 > 500 ? 1 : a.c2 - a.c1 + 1); break;
  case ID_DELROWS: book_delete_rows(book, sh, a.r1, a.r2 - a.r1 + 1); break;
  case ID_DELCOLS: book_delete_cols(book, sh, a.c1, a.c2 - a.c1 + 1); break;
  case ID_HIDEROWS: book_hide_rows(book, sh, a.r1, a.r2, 1); break;
  case ID_HIDECOLS: book_hide_cols(book, sh, a.c1, a.c2, 1); break;
  case ID_UNHIDEROWS: book_hide_rows(book, sh, a.r1 > 0 && a.r1 == a.r2 ? a.r1 - 1 : a.r1, a.r2 + (a.r1 == a.r2), 0); break;
  case ID_UNHIDECOLS: book_hide_cols(book, sh, a.c1 > 0 && a.c1 == a.c2 ? a.c1 - 1 : a.c1, a.c2 + (a.c1 == a.c2), 0); break;
  }
  refresh_all();
}
static void size_dialog(int rows)
{
  Sheet *s = cur_sheet();
  Rect a = cur_sel();
  char t[16];
  i32 k;
  /* in points for rows and characters for columns, as Excel asks */
  if (rows) sprintf(t, "%ld", (long)(sheet_row_height(s, s->cur_r) * 3 / 4));
  else sprintf(t, "%ld", (long)((sheet_col_width(s, s->cur_c) + 3) / 7));
  if (!ask_string(rows ? "Row Height" : "Column Width", rows ? "Row height (points):" : "Column width (characters):", t, sizeof t)) return;
  book_begin(book);
  if (rows) { u16 px = (u16)(atof(t) * 4 / 3 + 0.5); for (k = a.r1; k <= a.r2 && k < a.r1 + 10000; k++) book_set_row_height(book, cur_index(), k, px); }
  else { u16 px = (u16)(atof(t) * 7 + 5 + 0.5); for (k = a.c1; k <= a.c2 && k < a.c1 + 2000; k++) book_set_col_width(book, cur_index(), k, px); }
  book_end(book);
  refresh_all();
}
static void zoom_to(LONG z)
{
  if (z < 10) z = 10;
  if (z > 400) z = 400;
  set(grid, MUIA_Grid_Zoom, z);
  update_controls();
}
static void zoom_selection(void)
{
  Sheet *s = cur_sheet();
  Rect a = cur_sel();
  LONG w = 0, h = 0, gw = _mwidth(grid) - 60, gh = _mheight(grid) - 30, z;
  i32 k;
  if (a.r2 - a.r1 > 2000 || a.c2 - a.c1 > 500) return;
  for (k = a.c1; k <= a.c2; k++) w += sheet_col_width(s, k);
  for (k = a.r1; k <= a.r2; k++) h += sheet_row_height(s, k);
  if (w <= 0 || h <= 0) return;
  z = gw * 100 / w;
  if (gh * 100 / h < z) z = gh * 100 / h;
  s->top = a.r1; s->left = a.c1;
  zoom_to(z);
}
static void freeze(int how)
{
  Sheet *s = cur_sheet();
  switch (how) {
  case 0: book_freeze(book, cur_index(), s->cur_r, s->cur_c); break;
  case 1: book_freeze(book, cur_index(), 1, 0); break;
  case 2: book_freeze(book, cur_index(), 0, 1); break;
  default: book_freeze(book, cur_index(), 0, 0); break;
  }
  s->top = s->freezer; s->left = s->freezec;
  refresh_all();
}
/* ------------------------------------------------------------------ sheets */
static void select_sheet(int i)
{
  if (i < 0 || i >= book->nsh || i == book->active) return;
  if (xget(grid, MUIA_Grid_Editing) && !DoMethod(grid, MUIM_Grid_Commit, 0)) return;
  book->active = i;
  set(grid, MUIA_Grid_Book, (ULONG)book);
  refresh_all();
}
static void new_sheet(void)
{
  int at = book_add_sheet(book, NULL, book->active + 1);
  if (at >= 0) { book->active = at; set(grid, MUIA_Grid_Book, (ULONG)book); }
  refresh_all();
}
static void rename_sheet(void)
{
  char t[64];
  utf8_to_latin1(cur_sheet()->name, (u32)strlen(cur_sheet()->name), t, sizeof t);
  while (ask_string("Rename Sheet", "Name:", t, 32)) {
    char u[96];
    latin1_utf8(t, u, sizeof u);
    if (book_rename_sheet(book, cur_index(), u)) break;
    ask("OK", "%s", "A sheet's name can't be empty, used by another sheet,\nlonger than 31 characters or hold : \\ / ? * [ ]");
  }
  refresh_all();
}
static void delete_sheet(void)
{
  if (book->nsh <= 1) { ask("OK", "%s", "A workbook must keep at least one sheet."); return; }
  if (sheet_last_row(cur_sheet()) >= 0 && ask("Delete|Cancel", "Delete the sheet \"%s\" and everything on it?", cur_sheet()->name) != 1) return;
  book_delete_sheet(book, cur_index());
  set(grid, MUIA_Grid_Book, (ULONG)book);
  refresh_all();
}
static void move_sheet(int dir)
{
  int to = book->active + dir;
  if (to < 0 || to >= book->nsh) return;
  if (book_move_sheet(book, book->active, to)) book->active = to;
  set(grid, MUIA_Grid_Book, (ULONG)book);
  refresh_all();
}
static void hide_sheet(void)
{
  int i, visible = 0;
  for (i = 0; i < book->nsh; i++) visible += !book->sh[i]->hidden;
  if (visible <= 1) { ask("OK", "%s", "A workbook must have at least one sheet showing."); return; }
  cur_sheet()->hidden = 1;
  book->dirty = 1;
  for (i = 0; i < book->nsh; i++) if (!book->sh[(book->active + i) % book->nsh]->hidden) { book->active = (book->active + i) % book->nsh; break; }
  set(grid, MUIA_Grid_Book, (ULONG)book);
  refresh_all();
}

/* ------------------------------------------------------------------ the File page */
static void backstage_show(int on, int page)
{
  if (on) {
    static char info[1024];
    static const char *recentp[MAXRECENT];
    struct MUIP_Back_Set bs;
    char name[256], loc[300], ttl[130], auth[70];
    long cells = 0, formulas = 0;
    int i, s;
    for (s = 0; s < book->nsh; s++) {
      Sheet *sh = book->sh[s];
      i32 r;
      for (r = 0; r < sh->nrows; r++) if (sh->rows[r]) for (i = 0; i < sh->rows[r]->n; i++) { cells++; if (sh->rows[r]->c[i].f) formulas++; }
    }
    strncpy(name, bookpath[0] ? (const char *)FilePart((STRPTR)bookpath) : tr("Book1"), sizeof name - 1); name[sizeof name - 1] = 0;
    strncpy(loc, bookpath[0] ? bookpath : tr("Not saved yet"), sizeof loc - 1); loc[sizeof loc - 1] = 0;
    if (bookpath[0]) *((char *)PathPart((STRPTR)loc)) = 0;
    utf8_to_latin1(book->title, (u32)strlen(book->title), ttl, sizeof ttl);
    utf8_to_latin1(book->author, (u32)strlen(book->author), auth, sizeof auth);
    sprintf(info, "Name\t%.100s\nLocation\t%.250s\nSheets\t%d\nCells\t%ld\nFormulas\t%ld\nNames\t%d\nCalculation\t%s\nTitle\t%.100s\nAuthor\t%.60s",
            name, loc[0] ? loc : tr("(current drawer)"), book->nsh, cells, formulas, book->nnames, tr(book->manualcalc ? "Manual" : "Automatic"),
            ttl[0] ? ttl : "-", auth[0] ? auth : "-");
    for (i = 0; i < nrecent; i++) recentp[i] = recent[i];
    memset(&bs, 0, sizeof bs);
    bs.MethodID = MUIM_Back_Set;
    bs.recent = recentp; bs.nrecent = nrecent; bs.info = info; bs.username = username;
    bs.metric = !ledger_date_order; bs.dark = pref_dark; bs.lang = 0; bs.large = pref_large;
    DoMethodA(backstage, (Msg)&bs);
    set(backstage, MUIA_Back_Page, page);
  }
  back_shown = on;
  set(pages, MUIA_Group_ActivePage, on ? 1 : 0);
  if (!on) set(win, MUIA_Window_ActiveObject, (ULONG)grid);
}
static void win_reopen_all(void);
static void backstage_command(ULONG id)
{
  LONG v = 0;
  get(backstage, MUIA_Back_Value, &v);
  switch (id) {
  case ID_FILETAB: backstage_show(1, 0); break;
  case ID_BACKSTAGE: backstage_show(0, 0); break;
  case ID_TEMPLATE: {
    Book *b = book_template((int)v);
    backstage_show(0, 0);
    if (b && book_untouched()) show_book(b, ""); else if (b) win_new(b, "");
    break; }
  case ID_RECENT:
    backstage_show(0, 0);
    if (v >= 0 && v < nrecent) { char path[256]; strcpy(path, recent[v]); open_path(path); }
    break;
  case ID_OPTIONS: {
    ULONG m = 1, dk = 0, lgt = 0;
    char *n = NULL;
    get(backstage, MUIA_Back_Metric, &m); get(backstage, MUIA_Back_UserName, &n);
    ledger_date_order = m ? 0 : 1;
    if (n) { strncpy(username, n, sizeof username - 1); username[sizeof username - 1] = 0; }
    get(backstage, MUIA_Back_Dark, &dk); get(backstage, MUIA_Back_Large, &lgt);
    if ((int)dk != pref_dark || (int)lgt != pref_large) {
      pref_dark = (int)dk; pref_large = (int)lgt;
      win_reopen_all();
    }
    save_prefs();
    break; }
  }
}

/* ------------------------------------------------------------------ the ribbon's lists */
static void ribbon_choice(ULONG id)
{
  LONG v = (LONG)xget(ribbon, MUIA_Ribbon_Value);
  switch (id) {
  case ID_FONTDD: { const char *n = ribbon_font_name((int)v); char l[64]; if (n) { utf8_to_latin1(n, (u32)strlen(n), l, sizeof l); apply_format(FX_FONT, 0, l); } break; }
  case ID_SIZEDD: apply_format(FX_SIZE, v, NULL); break;
  case ID_FONTCOLOR: case ID_FILLCOLOR:
    if ((ULONG)v == COLOR_MORE) { LONG c = ask_color(0x4472C4); if (c < 0) break; v = c; }
    apply_format(id == ID_FONTCOLOR ? FX_COLOR : FX_FILL, v, NULL);
    break;
  case ID_BORDERDD: border_preset((int)v); break;
  case ID_MERGEDD: merge(v == 0 ? 0 : v == 1 ? 1 : v == 2 ? 2 : 3); break;
  case ID_NUMFMTDD:
    if (v == 11) { char code[128]; strcpy(code, book_fmt_code(book, cur_style()->numfmt)); if (dlg_number_format(code, sizeof code)) apply_format(FX_NUMFMT, 0, code); }
    else { char code[128]; numfmt_code((int)v, v == 1 || v == 2 || v == 3 || v == 7 ? 2 : 0, code, sizeof code); apply_format(FX_NUMFMT, 0, code); }
    break;
  case ID_CELLSTYLEDD: cell_style((int)v); break;
  case ID_CONDDD: cond_choice((int)v); break;
  case ID_INSERTDD: if (v == 0) dlg_insert_cells(0); else if (v == 1) rows_cols(ID_INSROWS); else if (v == 2) rows_cols(ID_INSCOLS); else new_sheet(); break;
  case ID_DELETEDD: if (v == 0) dlg_insert_cells(1); else if (v == 1) rows_cols(ID_DELROWS); else if (v == 2) rows_cols(ID_DELCOLS); else delete_sheet(); break;
  case ID_FORMATDD:
    switch (v) {
    case 0: size_dialog(1); break;
    case 1: DoMethod(grid, MUIM_Grid_Autofit, 1, cur_sel().r1, cur_sel().r2 < cur_sel().r1 + 2000 ? cur_sel().r2 : cur_sel().r1 + 2000); update_controls(); break;
    case 2: size_dialog(0); break;
    case 3: DoMethod(grid, MUIM_Grid_Autofit, 0, cur_sel().c1, cur_sel().c2 < cur_sel().c1 + 500 ? cur_sel().c2 : cur_sel().c1 + 500); update_controls(); break;
    case 4: {
      char t[16];
      sprintf(t, "%ld", (long)((cur_sheet()->defwidth + 3) / 7));
      if (ask_string("Default Width", "Standard column width (characters):", t, sizeof t)) { cur_sheet()->defwidth = (u16)(atof(t) * 7 + 5); book->dirty = 1; refresh_all(); }
      break; }
    case 5: rows_cols(ID_HIDEROWS); break;
    case 6: rows_cols(ID_HIDECOLS); break;
    case 7: rows_cols(ID_UNHIDEROWS); break;
    case 8: rows_cols(ID_UNHIDECOLS); break;
    case 9: rename_sheet(); break;
    case 10: { LONG c = ask_color(cur_sheet()->tabcolor & 0xFFFFFF); if (c >= 0) { cur_sheet()->tabcolor = FILL_ON | (ULONG)c; book->dirty = 1; refresh_all(); } break; }
    case 11: dlg_format_cells(0); break;
    }
    break;
  case ID_AUTOSUMDD: {
    static const char *const fns[] = { "SUM", "AVERAGE", "COUNT", "MAX", "MIN" };
    if (v >= 0 && v < 5) autosum(fns[v]); else dlg_insert_function();
    break; }
  case ID_FILLDD: if (v < 4) fill((int)v); else dlg_series(); break;
  case ID_CLEARDD: book_clear(book, cur_index(), cur_sel(), v == 0 ? CLR_ALL : v == 1 ? CLR_FORMATS : CLR_CONTENTS); refresh_all(); break;
  case ID_SORTDD:
    if (v == 0) sort_quick(0); else if (v == 1) sort_quick(1); else if (v == 2) dlg_sort();
    else if (v == 3) filter_toggle(); else if (v == 4) filter_clear(); else { book_filter_reapply(book, cur_index()); refresh_all(); }
    break;
  case ID_FINDDD: if (v == 0) dlg_find(0); else if (v == 1) dlg_find(1); else dlg_goto(); break;
  case ID_PASTEDD: paste(v == 1 ? PASTE_VALUES : v == 2 ? PASTE_FORMULAS : v == 3 ? PASTE_FORMATS : PASTE_ALL, v == 4); break;
  case ID_SYMBOLDD: { const char *e = ribbon_entry(id, v); char t[2]; if (e) { t[0] = e[0]; t[1] = 0; DoMethod(grid, MUIM_Grid_Insert, (ULONG)t, 0); } break; }
  case ID_TABCOLORDD:
    if ((ULONG)v == COLOR_MORE) { LONG c = ask_color(0x4472C4); if (c < 0) break; v = c; }
    cur_sheet()->tabcolor = (ULONG)v == COLOR_AUTO ? 0 : FILL_ON | ((ULONG)v & 0xFFFFFF);
    book->dirty = 1;
    refresh_all();
    break;
  case ID_CALCDD: book->manualcalc = v == 1; if (!book->manualcalc) book_recalc(book); refresh_all(); break;
  case ID_ZOOMDD: { static const LONG z[] = { 50, 75, 100, 125, 150, 200 }; if (v < 6) zoom_to(z[v]); else zoom_selection(); break; }
  case ID_FREEZEDD: freeze((int)v); break;
  default:
    if (id >= ID_FN_FIN && id <= ID_FN_MORE) insert_function(ribbon_entry(id, v));
    break;
  }
}

/* ------------------------------------------------------------------ the commands */
static int toggled(int now) { return !now; }
static void command(ULONG id)
{
  Sheet *s = cur_sheet();
  switch (id) {
  case ID_NEW: win_new(book_new(), ""); break;
  case ID_OPEN: {
    char path[512];
    strcpy(path, bookpath);
    if (file_request(0, "Open Workbook", path, sizeof path, "#?.(xlsx|csv|txt|tsv)")) open_path(path);
    break; }
  case ID_IMPORTCSV: {
    char path[512];
    Book *b;
    strcpy(path, bookpath);
    if (!file_request(0, "Import CSV", path, sizeof path, "#?.(csv|txt|tsv)") || !(b = load_file(path))) break;
    {
      /* its first sheet copied into a new sheet of this workbook */
      int at = book_add_sheet(book, NULL, book->nsh);
      Clip *cl;
      Rect all;
      char base[64], nm[32];
      if (at < 0) { book_free(b); break; }
      strncpy(base, (const char *)FilePart((STRPTR)path), sizeof base - 1); base[sizeof base - 1] = 0;
      if (strrchr(base, '.')) *strrchr(base, '.') = 0;
      base[31] = 0;
      book_unique_sheet_name(book, base, nm);
      book_rename_sheet(book, at, nm);
      all.r1 = 0; all.c1 = 0; all.r2 = sheet_last_row(b->sh[0]); all.c2 = sheet_last_col(b->sh[0]);
      if (all.r2 >= 0 && (cl = clip_copy(b, 0, all))) { book_paste(book, at, 0, 0, cl, PASTE_ALL, 0, NULL); clip_free(cl); }
      book_free(b);
      book->active = at;
      set(grid, MUIA_Grid_Book, (ULONG)book);
      refresh_all();
    }
    break; }
  case ID_SAVE: save(); break;
  case ID_SAVEAS: save_as(0); break;
  case ID_SAVECSV: save_as(1); break;
  case ID_ABOUT:
    ask("OK", "%s", "Ledger 0.2.2\nA spreadsheet for the Amiga (68040 or better, MUI)\n\nReads and writes Excel workbooks (.xlsx) and CSV.\nIts look follows GridCraft's; its icons are its own.");
    break;
  case ID_NEXTWIN: break;          /* (main's loop) */
  case ID_UNDO: case ID_REDO: {
    int sh;
    Rect a;
    if (xget(grid, MUIA_Grid_Editing)) DoMethod(grid, MUIM_Grid_Cancel);
    if (id == ID_UNDO ? book_undo(book, &sh, &a) : book_redo(book, &sh, &a)) {
      if (sh >= 0 && sh < book->nsh && sh != book->active) { book->active = sh; set(grid, MUIA_Grid_Book, (ULONG)book); }
      if (a.r1 >= 0 && a.r1 < MAXROW && a.c1 >= 0 && a.c1 < MAXCOL && a.r2 >= a.r1 && a.c2 >= a.c1)
        DoMethod(grid, MUIM_Grid_Select, a.r1, a.c1, a.r2, a.c2, a.r1, a.c1);
    }
    refresh_all();
    break; }
  case ID_CUT: copy_cells(1); break;
  case ID_COPY: copy_cells(0); break;
  case ID_PASTE: paste(PASTE_ALL, 0); break;
  case ID_PASTEVALUES: paste(PASTE_VALUES, 0); break;
  case ID_PASTEFORMULAS: paste(PASTE_FORMULAS, 0); break;
  case ID_PASTEFORMATS: paste(PASTE_FORMATS, 0); break;
  case ID_PASTETRANSPOSE: paste(PASTE_ALL, 1); break;
  case ID_SELALL: DoMethod(grid, MUIM_Grid_Select, 0, 0, MAXROW - 1, MAXCOL - 1, s->cur_r, s->cur_c); break;
  case ID_EDITCELL: DoMethod(grid, MUIM_Grid_Edit, 0, 2); break;
  case ID_CLEARCONTENTS: book_clear(book, cur_index(), cur_sel(), CLR_CONTENTS); refresh_all(); break;
  case ID_CLEARFORMATS: book_clear(book, cur_index(), cur_sel(), CLR_FORMATS); refresh_all(); break;
  case ID_CLEARALL: book_clear(book, cur_index(), cur_sel(), CLR_ALL); refresh_all(); break;
  case ID_FILLDOWN: fill(0); break;
  case ID_FILLRIGHT: fill(1); break;
  case ID_FILLUP: fill(2); break;
  case ID_FILLLEFT: fill(3); break;
  case ID_FILLSERIES: dlg_series(); break;
  case ID_FIND: dlg_find(0); break;
  case ID_FINDNEXT: dlg_find(2); break;
  case ID_REPLACE: dlg_find(1); break;
  case ID_GOTO: dlg_goto(); break;
  case ID_BRUSH:
    /* the format painter: the active cell's look given to the next cells selected (brush_done) */
    if (brush_pending) { brush_pending = 0; set(grid, MUIA_Grid_Brush, 0); update_controls(); break; }
    brush_st = *cur_style(); brush_font = *cur_font();
    brush_pending = 1;
    set(grid, MUIA_Grid_Brush, 1);
    update_controls();
    break;
  case ID_GROW: apply_format(FX_GROW, 1, NULL); break;
  case ID_SHRINK: apply_format(FX_GROW, -1, NULL); break;
  case ID_BOLD: apply_format(FX_BOLD, toggled(cur_font()->bold), NULL); break;
  case ID_ITALIC: apply_format(FX_ITALIC, toggled(cur_font()->italic), NULL); break;
  case ID_UNDERLINE: apply_format(FX_UNDERLINE, cur_font()->underline ? 0 : 1, NULL); break;
  case ID_STRIKE: apply_format(FX_STRIKE, toggled(cur_font()->strike), NULL); break;
  case ID_ATOP: apply_format(FX_VALIGN, VA_TOP, NULL); break;
  case ID_AMIDDLE: apply_format(FX_VALIGN, VA_CENTER, NULL); break;
  case ID_ABOTTOM: apply_format(FX_VALIGN, VA_BOTTOM, NULL); break;
  case ID_LEFT: apply_format(FX_HALIGN, cur_style()->halign == HA_LEFT ? HA_GENERAL : HA_LEFT, NULL); break;
  case ID_CENTER: apply_format(FX_HALIGN, cur_style()->halign == HA_CENTER ? HA_GENERAL : HA_CENTER, NULL); break;
  case ID_RIGHT: apply_format(FX_HALIGN, cur_style()->halign == HA_RIGHT ? HA_GENERAL : HA_RIGHT, NULL); break;
  case ID_WRAP: apply_format(FX_WRAP, toggled(cur_style()->wrap), NULL); break;
  case ID_INDENT: apply_format(FX_INDENT, 1, NULL); break;
  case ID_OUTDENT: apply_format(FX_INDENT, -1, NULL); break;
  case ID_MERGECENTER: merge(0); break;
  case ID_CURRENCY: { char c[128]; apply_format(FX_NUMFMT, 0, numfmt_code(2, 2, c, sizeof c)); break; }
  case ID_PERCENT: apply_format(FX_NUMFMT, 0, "0%"); break;
  case ID_COMMA: { char c[128]; apply_format(FX_NUMFMT, 0, numfmt_code(1, 2, c, sizeof c)); break; }
  case ID_INCDEC: case ID_DECDEC: {
    char c[160];
    const Value *v = cell_value(book, cur_index(), s->cur_r, s->cur_c);
    step_decimals(book_fmt_code(book, cur_style()->numfmt), id == ID_INCDEC ? 1 : -1, v->t == V_NUM ? v->n : 0, c, sizeof c);
    apply_format(FX_NUMFMT, 0, c);
    break; }
  case ID_FORMATCELLS: dlg_format_cells(0); break;
  case ID_ROWHEIGHT: size_dialog(1); break;
  case ID_COLWIDTH: size_dialog(0); break;
  case ID_AUTOFITROW: DoMethod(grid, MUIM_Grid_Autofit, 1, cur_sel().r1, cur_sel().r2 < cur_sel().r1 + 2000 ? cur_sel().r2 : cur_sel().r1 + 2000); update_controls(); break;
  case ID_AUTOFITCOL: DoMethod(grid, MUIM_Grid_Autofit, 0, cur_sel().c1, cur_sel().c2 < cur_sel().c1 + 500 ? cur_sel().c2 : cur_sel().c1 + 500); update_controls(); break;
  case ID_INSROWS: case ID_INSCOLS: case ID_DELROWS: case ID_DELCOLS: case ID_HIDEROWS: case ID_HIDECOLS: case ID_UNHIDEROWS: case ID_UNHIDECOLS: rows_cols(id); break;
  case ID_INSCELLS: dlg_insert_cells(0); break;
  case ID_DELCELLS: dlg_insert_cells(1); break;
  case ID_AUTOSUM: autosum("SUM"); break;
  case ID_SORTAZ: sort_quick(0); break;
  case ID_SORTZA: sort_quick(1); break;
  case ID_CUSTOMSORT: dlg_sort(); break;
  case ID_REMOVEDUP: remove_dups(); break;
  case ID_FILTER: filter_toggle(); break;
  case ID_NOTENEW: dlg_note(); break;
  case ID_LINK: dlg_link(); break;
  case ID_LINKOPEN: link_open(); break;
  case ID_LINKDEL: book_set_link(book, cur_index(), cur_sheet()->cur_r, cur_sheet()->cur_c, NULL, NULL); refresh_all(); break;
  case ID_NOTEDEL: book_set_note(book, cur_index(), cur_sheet()->cur_r, cur_sheet()->cur_c, NULL, NULL); refresh_all(); break;
  case ID_NOTEPREV: case ID_NOTENEXT: note_step(id == ID_NOTENEXT); break;
  case ID_NOTESHOW: set(grid, MUIA_Grid_ShowNotes, !xget(grid, MUIA_Grid_ShowNotes)); update_controls(); break;
  case ID_CONDNEW: cond_choice(23); break;
  case ID_CONDCLEAR: cond_choice(24); break;
  case ID_CONDCLEARALL: cond_choice(25); break;
  case ID_CONDMANAGE: cond_choice(26); break;
  case ID_CLEARFILTER: filter_clear(); break;
  case ID_REAPPLY: book_filter_reapply(book, cur_index()); refresh_all(); break;
  case ID_FILTERCOL: dlg_filter((i32)xget(grid, MUIA_Grid_FilterCol)); break;
  case ID_INSSHEET: case ID_NEWSHEET: new_sheet(); break;
  case ID_FUNCTION: case ID_FXBUTTON: dlg_insert_function(); break;
  case ID_DATE: case ID_TIME: {
    double n = ledger_now();
    char t[40];
    int y, m, d;
    if (id == ID_DATE) { serial_ymd(floor(n), &y, &m, &d); sprintf(t, "%d/%d/%d", ledger_date_order ? m : d, ledger_date_order ? d : m, y); }
    else { long sec = (long)((n - floor(n)) * 86400.0); sprintf(t, "%ld:%02ld:%02ld", sec / 3600, sec / 60 % 60, sec % 60); }
    if (xget(grid, MUIA_Grid_Editing)) DoMethod(grid, MUIM_Grid_Insert, (ULONG)t, 0);
    else { char u[64]; latin1_utf8(t, u, sizeof u); book_input(book, cur_index(), s->cur_r, s->cur_c, u, NULL); refresh_all(); }
    break; }
  case ID_NAMES: dlg_names(); break;
  case ID_DEFNAME: dlg_define_name(); break;
  case ID_SHOWFORMULAS: set(grid, MUIA_Grid_Formulas, !xget(grid, MUIA_Grid_Formulas)); update_controls(); break;
  case ID_CALCNOW: book_recalc(book); refresh_all(); break;
  case ID_MANUALCALC: book->manualcalc = !book->manualcalc; if (!book->manualcalc) book_recalc(book); refresh_all(); break;
  case ID_STATS: dlg_stats(); break;
  case ID_GRIDLINES: s->gridlines = !s->gridlines; book->dirty = 1; refresh_all(); break;
  case ID_HEADINGS: s->headings = !s->headings; book->dirty = 1; refresh_all(); break;
  case ID_SHOWZEROS: s->showzeros = !s->showzeros; book->dirty = 1; refresh_all(); break;
  case ID_FORMULABAR: pref_fxbar = !pref_fxbar; set(fxgroup, MUIA_ShowMe, pref_fxbar); save_prefs(); update_controls(); break;
  case ID_ZOOM100: zoom_to(100); break;
  case ID_ZOOMIN: zoom_to((s->zoom ? s->zoom : 100) + 10); break;
  case ID_ZOOMOUT: zoom_to((s->zoom ? s->zoom : 100) - 10); break;
  case ID_ZOOMSEL: zoom_selection(); break;
  case ID_ZOOMSET: zoom_to((LONG)xget(statusbar, MUIA_Status_Zoom)); break;
  case ID_FREEZE: freeze(0); break;
  case ID_UNFREEZE: freeze(3); break;
  case ID_DARKUI: pref_dark = !pref_dark; save_prefs(); win_reopen_all(); break;
  case ID_SHEETTAB: select_sheet((int)xget(tabs, MUIA_Tabs_Value)); break;
  case ID_PREVSHEET: { int i = book->active; do i--; while (i >= 0 && book->sh[i]->hidden); select_sheet(i); break; }
  case ID_NEXTSHEET: { int i = book->active; do i++; while (i < book->nsh && book->sh[i]->hidden); select_sheet(i); break; }
  case ID_RENAMESHEET: rename_sheet(); break;
  case ID_DELSHEET: delete_sheet(); break;
  case ID_DUPSHEET: { int at = book_duplicate_sheet(book, book->active); if (at >= 0) { book->active = at; set(grid, MUIA_Grid_Book, (ULONG)book); } refresh_all(); break; }
  case ID_MOVESHEETL: move_sheet(-1); break;
  case ID_MOVESHEETR: move_sheet(1); break;
  case ID_HIDESHEET: hide_sheet(); break;
  case ID_UNHIDESHEET: dlg_unhide_sheet(); break;
  case ID_NAMEBOX: {
    /* a cell, a range or a name typed into the name box: selected */
    char t[64], u[200];
    u16 sh; Rect r; u8 fl;
    int i, n;
    strncpy(t, (const char *)xget(namebox, MUIA_String_Contents), sizeof t - 1); t[sizeof t - 1] = 0;
    latin1_utf8(t, u, sizeof u);
    n = parse_ref(book, u, &sh, &r, &fl);
    if (!n || n != (int)strlen(u)) {
      for (i = 0; i < book->nnames; i++) if (str_ieq(book->names[i].name, u) && book->names[i].formula_text) {
        const char *ft = book->names[i].formula_text;
        n = parse_ref(book, ft[0] == '=' ? ft + 1 : ft, &sh, &r, &fl);
        break;
      }
    }
    if (n) {
      if (sh != SHEET_SELF && sh != book->active) select_sheet(sh);
      if (fl & RF_COLS) { r.r1 = 0; r.r2 = MAXROW - 1; }
      if (fl & RF_ROWS) { r.c1 = 0; r.c2 = MAXCOL - 1; }
      DoMethod(grid, MUIM_Grid_Select, r.r1, r.c1, r.r2, r.c2, r.r1, r.c1);
    } else if (u[0]) {
      /* a new name for the selection */
      char ref[40], txt[300];
      Rect a = cur_sel();
      ref_text(a.r1, a.c1, 1, 1, ref);
      if (a.r1 != a.r2 || a.c1 != a.c2) { strcat(ref, ":"); ref_text(a.r2, a.c2, 1, 1, ref + strlen(ref)); }
      snprintf(txt, sizeof txt, "'%s'!%s", cur_sheet()->name, ref);
      if (!book_define_name(book, u, txt, SHEET_SELF)) ask("OK", "%s", "That isn't a cell address, and it can't be a name:\na name starts with a letter and holds letters, digits, _ and . only.");
    }
    set(win, MUIA_Window_ActiveObject, (ULONG)grid);
    update_controls();
    break; }
  case ID_FXTYPED:
    /* typing in the formula bar shows in the cell as it goes */
    strncpy(fx_shown, (const char *)xget(fxbar, MUIA_String_Contents), sizeof fx_shown - 1);
    DoMethod(grid, MUIM_Grid_SetText, (ULONG)fx_shown, 0);
    status_update();
    break;
  case ID_FXENTER:
    DoMethod(grid, MUIM_Grid_SetText, (ULONG)xget(fxbar, MUIA_String_Contents), 0);
    if (DoMethod(grid, MUIM_Grid_Commit, 1)) set(win, MUIA_Window_ActiveObject, (ULONG)grid);
    else ask("OK", "%s", "There's a problem with this formula.\nCheck its brackets, commas and quotes.");
    update_controls();
    break;
  case ID_FXCANCEL: DoMethod(grid, MUIM_Grid_Cancel); fx_shown[0] = 1; update_controls(); set(win, MUIA_Window_ActiveObject, (ULONG)grid); break;
  }
}

/* ------------------------------------------------------------------ several workbooks at once */
#define MAXWIN 8
typedef struct {
  int used;
  Object *win, *grid, *ribbon, *statusbar, *tabs, *backstage, *pages, *vscroll, *hscroll, *namebox, *fxbar, *fxgroup;
  Book *book;
  char path[512];
  int back_shown;
} DocWin;
static DocWin wins[MAXWIN];
static int curwin = -1;
static void win_save(DocWin *w)
{
  w->win = win; w->grid = grid; w->ribbon = ribbon; w->statusbar = statusbar; w->tabs = tabs; w->backstage = backstage; w->pages = pages;
  w->vscroll = vscroll; w->hscroll = hscroll; w->namebox = namebox; w->fxbar = fxbar; w->fxgroup = fxgroup;
  w->book = book; memcpy(w->path, bookpath, sizeof bookpath); w->back_shown = back_shown;
}
static void win_load(DocWin *w)
{
  win = w->win; grid = w->grid; ribbon = w->ribbon; statusbar = w->statusbar; tabs = w->tabs; backstage = w->backstage; pages = w->pages;
  vscroll = w->vscroll; hscroll = w->hscroll; namebox = w->namebox; fxbar = w->fxbar; fxgroup = w->fxgroup;
  book = w->book; memcpy(bookpath, w->path, sizeof bookpath); back_shown = w->back_shown;
  fx_shown[0] = 1; fx_shown[1] = 0;
}
static void win_switch(int i)
{
  if (i == curwin || i < 0 || i >= MAXWIN || !wins[i].used) return;
  if (curwin >= 0 && wins[curwin].used) win_save(&wins[curwin]);
  curwin = i;
  win_load(&wins[i]);
}
static int win_count(void) { int i, n = 0; for (i = 0; i < MAXWIN; i++) n += wins[i].used; return n; }
static int win_now(LONG *box)
{
  struct Window *w = win ? (struct Window *)xget(win, MUIA_Window_Window) : NULL;
  if (!w) return 0;
  box[0] = w->LeftEdge; box[1] = w->TopEdge; box[2] = w->Width; box[3] = w->Height;
  return 1;
}
/* a new workbook window showing b (path: where it came from), which becomes the current one */
static int win_new(Book *b, const char *path)
{
  int i, slot = -1, has_box = 0;
  LONG box[4];
  Object *w, *strip, *fxb, *fxok, *fxno;
  if (!b) return 0;
  for (i = 0; i < MAXWIN && slot < 0; i++) if (!wins[i].used) slot = i;
  if (slot < 0) { book_free(b); ask("OK", "Ledger can have %s workbooks open at once.\nClose one to open another.", "8"); return 0; }
  if (curwin >= 0 && win_now(box)) { box[0] += 24; box[1] += 20; has_box = 1; }
  else if (win_box[2] > 0) { memcpy(box, win_box, sizeof box); has_box = 1; }
  if (curwin >= 0) win_save(&wins[curwin]);
  strip = MUI_MakeObject(MUIO_MenustripNM, (ULONG)menus, 0);
  w = QWindowObject,
    MUIA_Window_Title, (ULONG)"Ledger",
    MUIA_Window_ID, slot ? 0 : MAKE_ID('L','E','D','G'),
    MUIA_Window_DisableKeys, MUIKEYF_WINDOW_CLOSE,
    MUIA_Window_Menustrip, (ULONG)strip,
    MUIA_Window_Width, has_box ? box[2] : MUIV_Window_Width_Visible(98),
    MUIA_Window_Height, has_box ? box[3] : MUIV_Window_Height_Visible(98),
    (ULONG)(has_box ? MUIA_Window_LeftEdge : TAG_IGNORE), box[0],
    (ULONG)(has_box ? MUIA_Window_TopEdge : TAG_IGNORE), box[1],
    WindowContents, pages = VGroup, InnerSpacing(0, 0), MUIA_Group_PageMode, TRUE,
     Child, VGroup, InnerSpacing(0, 0), MUIA_Group_Spacing, 0,
      Child, ribbon = NewObject(ribbon_class()->mcc_Class, NULL, MUIA_ShortHelp, (ULONG)" ", TAG_DONE),
      Child, fxgroup = HGroup, InnerSpacing(4, 2), MUIA_Group_Spacing, 4, MUIA_ShowMe, pref_fxbar, QDLG,
        Child, namebox = StringObject, StringFrame, MUIA_String_MaxLen, 60, MUIA_HorizWeight, 0, MUIA_FixWidthTxt, (ULONG)"WWWWWWWWW",
                         MUIA_ShortHelp, (ULONG)tr("Name Box: the active cell's address. Type an address or a name and press Return to go there; type a new name to name the selection."), End,
        Child, fxno = QButton("\327"),
        Child, fxok = QButton("OK"),
        Child, fxb = QButton("fx"),
        Child, fxbar = StringObject, StringFrame, MUIA_String_MaxLen, 2000, MUIA_ShortHelp, (ULONG)tr("Formula bar: the active cell's entry. Click here to change it; Return puts it in the cell."), End,
      End,
      Child, HGroup, MUIA_Group_Spacing, 0,
        Child, grid = NewObject(grid_class()->mcc_Class, NULL, MUIA_Grid_Fonts, (ULONG)&fonts, MUIA_FillArea, FALSE, MUIA_CycleChain, 1, TAG_DONE),
        Child, vscroll = NewObject(flatscroll_class()->mcc_Class, NULL, TAG_DONE),
      End,
      Child, HGroup, MUIA_Group_Spacing, 0,
        Child, tabs = NewObject(tabs_class()->mcc_Class, NULL, MUIA_HorizWeight, 60, TAG_DONE),
        Child, hscroll = NewObject(flatscroll_class()->mcc_Class, NULL, MUIA_FlatScroll_Horiz, TRUE, MUIA_HorizWeight, 40, TAG_DONE),
      End,
      Child, statusbar = NewObject(status_class()->mcc_Class, NULL, MUIA_ShortHelp, (ULONG)" ", TAG_DONE),
     End,
     Child, backstage = NewObject(backstage_class()->mcc_Class, NULL, TAG_DONE),
    End,
  End;
  if (!w) {
    if (strip) MUI_DisposeObject(strip);
    book_free(b);
    if (curwin >= 0) { win_load(&wins[curwin]); ask("OK", "%s", "There isn't enough memory for another window."); }
    return 0;
  }
  DoMethod(app, OM_ADDMEMBER, (ULONG)w);
  win = w;
  SetAttrs(fxno, MUIA_HorizWeight, 0, TAG_DONE); SetAttrs(fxok, MUIA_HorizWeight, 0, TAG_DONE); SetAttrs(fxb, MUIA_HorizWeight, 0, TAG_DONE);
  set(fxno, MUIA_ShortHelp, (ULONG)tr("Cancel the entry (Esc)"));
  set(fxok, MUIA_ShortHelp, (ULONG)tr("Enter: put it in the cell (Return)"));
  set(fxb, MUIA_ShortHelp, (ULONG)tr("Insert a function"));
  SetAttrs(grid, MUIA_Grid_VProp, (ULONG)vscroll, MUIA_Grid_HProp, (ULONG)hscroll, MUIA_Grid_Status, (ULONG)statusbar, TAG_DONE);
  DoMethod(win, MUIM_Notify, MUIA_Window_CloseRequest, TRUE, (ULONG)app, 2, MUIM_Application_ReturnID, ID_WINCLOSE + slot);
  DoMethod(win, MUIM_Notify, MUIA_Window_Activate, TRUE, (ULONG)app, 2, MUIM_Application_ReturnID, ID_WINACT + slot);
  DoMethod(vscroll, MUIM_Notify, MUIA_Prop_First, MUIV_EveryTime, (ULONG)grid, 3, MUIM_Set, MUIA_Grid_Top, MUIV_TriggerValue);
  DoMethod(hscroll, MUIM_Notify, MUIA_Prop_First, MUIV_EveryTime, (ULONG)grid, 3, MUIM_Set, MUIA_Grid_Left, MUIV_TriggerValue);
  DoMethod(grid, MUIM_Notify, MUIA_Grid_Moved, MUIV_EveryTime, (ULONG)app, 2, MUIM_Application_ReturnID, ID_SELCHG);
  DoMethod(grid, MUIM_Notify, MUIA_Grid_Changed, MUIV_EveryTime, (ULONG)app, 2, MUIM_Application_ReturnID, ID_CHANGED);
  DoMethod(grid, MUIM_Notify, MUIA_Grid_EditText, MUIV_EveryTime, (ULONG)app, 2, MUIM_Application_ReturnID, ID_EDITCHG);
  DoMethod(grid, MUIM_Notify, MUIA_Grid_Editing, MUIV_EveryTime, (ULONG)app, 2, MUIM_Application_ReturnID, ID_EDITDONE);
  DoMethod(grid, MUIM_Notify, MUIA_Grid_Brush, FALSE, (ULONG)app, 2, MUIM_Application_ReturnID, ID_BRUSHDONE);
  DoMethod(namebox, MUIM_Notify, MUIA_String_Acknowledge, MUIV_EveryTime, (ULONG)app, 2, MUIM_Application_ReturnID, ID_NAMEBOX);
  DoMethod(fxbar, MUIM_Notify, MUIA_String_Acknowledge, MUIV_EveryTime, (ULONG)app, 2, MUIM_Application_ReturnID, ID_FXENTER);
  DoMethod(fxbar, MUIM_Notify, MUIA_String_Contents, MUIV_EveryTime, (ULONG)app, 2, MUIM_Application_ReturnID, ID_FXTYPED);
  DoMethod(fxok, MUIM_Notify, MUIA_Pressed, FALSE, (ULONG)app, 2, MUIM_Application_ReturnID, ID_FXENTER);
  DoMethod(fxno, MUIM_Notify, MUIA_Pressed, FALSE, (ULONG)app, 2, MUIM_Application_ReturnID, ID_FXCANCEL);
  DoMethod(fxb, MUIM_Notify, MUIA_Pressed, FALSE, (ULONG)app, 2, MUIM_Application_ReturnID, ID_FXBUTTON);
  set(win, MUIA_Window_DefaultObject, (ULONG)grid);
  wins[slot].used = 1;
  curwin = slot;
  book = NULL;
  back_shown = 0;
  set(win, MUIA_Window_Open, TRUE);
  show_book(b, path);
  set(win, MUIA_Window_ActiveObject, (ULONG)grid);
  win_save(&wins[slot]);
  return 1;
}
static int win_close(void)
{
  int i = curwin;
  Object *w = win;
  if (i < 0) return 1;
  if (!may_discard()) return 0;
  if (i == 0 && win_now(win_box)) save_prefs();
  if (back_shown) backstage_show(0, 0);
  set(w, MUIA_Window_Open, FALSE);
  DoMethod(app, OM_REMMEMBER, (ULONG)w);
  MUI_DisposeObject(w);
  if (cut_book == book) { cut_book = NULL; cut_pending = 0; }
  book_free(book);
  book = NULL;
  memset(&wins[i], 0, sizeof wins[i]);
  curwin = -1;
  for (i = 0; i < MAXWIN; i++) if (wins[i].used) { win_switch(i); set(win, MUIA_Window_Activate, TRUE); break; }
  return 1;
}
static int win_find(const char *path)
{
  int i;
  if (curwin >= 0) win_save(&wins[curwin]);
  for (i = 0; i < MAXWIN; i++)
    if (wins[i].used && wins[i].path[0] && !Stricmp((STRPTR)wins[i].path, (STRPTR)path)) {
      win_switch(i);
      DoMethod(win, MUIM_Window_ToFront);
      set(win, MUIA_Window_Activate, TRUE);
      return 1;
    }
  return 0;
}
/* a new look or text size: every window closed and opened again, MUI building them afresh */
static void win_reopen_all(void)
{
  int i, cur = curwin;
  if (curwin >= 0) win_save(&wins[curwin]);
  for (i = 0; i < MAXWIN; i++) if (wins[i].used) set(wins[i].win, MUIA_Window_Open, FALSE);
  theme_set_dark(pref_dark); theme_set_large(pref_large);
  for (i = 0; i < MAXWIN; i++) if (wins[i].used && i != cur) set(wins[i].win, MUIA_Window_Open, TRUE);
  if (cur >= 0) set(wins[cur].win, MUIA_Window_Open, TRUE);
  if (cur >= 0) { win_load(&wins[cur]); refresh_all(); }
}
static void win_next(void)
{
  int i, k;
  for (k = 1; k < MAXWIN; k++) {
    i = (curwin + k) % MAXWIN;
    if (wins[i].used) { win_switch(i); DoMethod(win, MUIM_Window_ToFront); set(win, MUIA_Window_Activate, TRUE); return; }
  }
}
static int win_close_all(void)
{
  int i;
  for (i = 0; i < MAXWIN; i++) if (wins[i].used) { win_switch(i); set(win, MUIA_Window_Activate, TRUE); if (!win_close()) return 0; }
  return 1;
}

/* ------------------------------------------------------------------ starting up */
static BPTR home_lock, home_old;
static void scan_step(int done, int total)
{
  static char what[80];
  snprintf(what, sizeof what, tr("Looking for installed fonts (%d of %d)"), done, total);
  splash_step(10 + (total ? 30 * done / total : 0), what);
}
static void wb_tooltypes(char *screen, int size)
{
  struct Library *IconBase;
  struct WBArg *wa = &_WBenchMsg->sm_ArgList[0];
  struct DiskObject *dob;
  BPTR old;
  STRPTR v;
  if (!(IconBase = OpenLibrary((STRPTR)"icon.library", 37))) return;
  old = CurrentDir(wa->wa_Lock);
  if ((dob = GetDiskObject((STRPTR)wa->wa_Name))) {
    if ((v = FindToolType((STRPTR *)dob->do_ToolTypes, (STRPTR)"PUBSCREEN")) && !screen[0]) { strncpy(screen, (char *)v, size - 1); screen[size - 1] = 0; }
    FreeDiskObject(dob);
  }
  CurrentDir(old);
  CloseLibrary(IconBase);
}
static int home_has_fonts(BPTR dir)
{
  BPTR old = CurrentDir(dir), l = Lock((STRPTR)"Fonts/QuillSans.font", ACCESS_READ);
  CurrentDir(old);
  if (l) UnLock(l);
  return l != 0;
}
/* PROGDIR: made right if a shell started Ledger without it (it holds Fonts and TrueType) */
static void find_home(const char *argv0)
{
  char name[256];
  const char *tries[2];
  int i;
  struct Process *me = (struct Process *)FindTask(NULL);
  APTR pw = me->pr_WindowPtr;
  BPTR l;
  me->pr_WindowPtr = (APTR)-1;
  l = GetProgramDir() ? Lock((STRPTR)"PROGDIR:Fonts/QuillSans.font", ACCESS_READ) : 0;
  if (l) { UnLock(l); me->pr_WindowPtr = pw; return; }
  tries[0] = argv0;
  tries[1] = GetProgramName((STRPTR)name, sizeof name) ? name : NULL;
  for (i = 0; i < 2 && !home_lock; i++) {
    BPTR f, d;
    if (!tries[i] || !tries[i][0] || !(f = Lock((STRPTR)tries[i], ACCESS_READ))) continue;
    d = ParentDir(f);
    UnLock(f);
    if (d && home_has_fonts(d)) home_lock = d; else if (d) UnLock(d);
  }
  if (!home_lock) { BPTR d = Lock((STRPTR)"", ACCESS_READ); if (d && home_has_fonts(d)) home_lock = d; else if (d) UnLock(d); }
  if (home_lock) home_old = SetProgramDir(home_lock);
  me->pr_WindowPtr = pw;
}
static void free_home(void)
{
  if (!home_lock) return;
  SetProgramDir(home_old);
  UnLock(home_lock);
  home_lock = 0;
}
/* Ledger's Fonts drawer added to FONTS: while it runs (diskfont.library finds its fonts there) */
static void add_fonts(void)
{
  BPTR l = Lock((STRPTR)"PROGDIR:Fonts", ACCESS_READ), sys;
  struct Process *me = (struct Process *)FindTask(NULL);
  APTR pw = me->pr_WindowPtr;
  if (!l) return;
  me->pr_WindowPtr = (APTR)-1;
  if ((sys = Lock((STRPTR)"FONTS:", ACCESS_READ))) {
    LONG same = SameLock(l, sys);
    UnLock(sys);
    if (same == LOCK_SAME) { UnLock(l); me->pr_WindowPtr = pw; return; }
  }
  me->pr_WindowPtr = pw;
  if (AssignAdd((STRPTR)"FONTS", l)) fonts_assigned = 1; else UnLock(l);
}
static void remove_fonts(void)
{
  BPTR l;
  if (!fonts_assigned) return;
  if ((l = Lock((STRPTR)"PROGDIR:Fonts", ACCESS_READ))) { RemAssignList((STRPTR)"FONTS", l); UnLock(l); }
  fonts_assigned = 0;
}
/* the keyboard back to the grid after a command, except those that left a window or field wanting it */
static int keeps_focus(ULONG id) { return id == ID_FXTYPED || id == ID_EDITCHG || id == ID_SELCHG || id == ID_CHANGED || id == ID_EDITDONE || id == ID_BRUSHDONE; }

int main(int argc, char **argv)
{
  ULONG sigs = 0;
  int rc = 20, i;
  char start[512] = "", arg_screen[64] = "", missing_screen[64] = "";
  if (!(IntuitionBase = (struct IntuitionBase *)OpenLibrary((STRPTR)"intuition.library", 39)) ||
      !(GfxBase = (struct GfxBase *)OpenLibrary((STRPTR)"graphics.library", 39)) ||
      !(LayersBase = OpenLibrary((STRPTR)"layers.library", 39)) ||
      !(UtilityBase = OpenLibrary((STRPTR)"utility.library", 39)) ||
      !(KeymapBase = OpenLibrary((STRPTR)"keymap.library", 37))) goto done;
  if (!(MUIMasterBase = OpenLibrary((STRPTR)MUIMASTER_NAME, 19))) { printf("Ledger needs MUI (muimaster.library 19 or later).\n"); goto done; }
  find_home(argc > 0 ? argv[0] : NULL);
  for (i = 1; i < argc; i++) {
    if (!strncmp(argv[i], "DEBUG=", 6)) qlog = fopen(argv[i] + 6, "w");
    else if (!Strnicmp((STRPTR)argv[i], (STRPTR)"PUBSCREEN=", 10)) { strncpy(arg_screen, argv[i] + 10, sizeof arg_screen - 1); arg_screen[sizeof arg_screen - 1] = 0; }
    else if (!start[0]) strncpy(start, argv[i], sizeof start - 1);
  }
  if (argc == 0 && _WBenchMsg && _WBenchMsg->sm_NumArgs > 1) {
    struct WBArg *wa = &_WBenchMsg->sm_ArgList[1];
    if (NameFromLock(wa->wa_Lock, (STRPTR)start, sizeof start)) AddPart((STRPTR)start, wa->wa_Name, sizeof start);
  }
  if (argc == 0 && _WBenchMsg) wb_tooltypes(arg_screen, sizeof arg_screen);
  load_prefs();
  if (arg_screen[0]) strcpy(pub_screen, arg_screen);
  if (pub_screen[0]) {
    struct Screen *sc = LockPubScreen((STRPTR)pub_screen);
    if (sc) UnlockPubScreen(NULL, sc); else { strcpy(missing_screen, pub_screen); pub_screen[0] = 0; }
  }
  lang_set(LANG_EN);
  theme_set_dark(pref_dark);
  theme_set_large(pref_large);
  splash_open(pub_screen, "Ledger 0.2.2");
  splash_step(3, tr("Adding Ledger's fonts drawer"));
  DiskfontBase = OpenLibrary((STRPTR)"diskfont.library", 39);
  add_fonts();
  splash_step(6, tr("Reading the TrueType fonts"));
  if (!fonts_init(&fonts, "PROGDIR:TrueType")) printf("Ledger: no fonts in PROGDIR:TrueType (fonts.list).\n");
  splash_step(10, tr("Looking for installed fonts"));
  sysfonts_scan(&fonts, scan_step);
  splash_step(45, tr("Preparing"));
  if (!grid_class() || !ribbon_class() || !status_class() || !tabs_class() || !flatscroll_class() || !ticklist_class() || !backstage_class()) goto done;
  load_recent();
  splash_step(60, tr("Building the window"));
  app = ApplicationObject,
    MUIA_Application_Title, (ULONG)"Ledger",
    MUIA_Application_Version, (ULONG)verstag,
    MUIA_Application_Description, (ULONG)"A spreadsheet",
    MUIA_Application_Base, (ULONG)"LEDGER",
  End;
  if (!app) { printf("Ledger: the application could not be made (MUI classes missing?)\n"); goto done; }
  splash_step(75, tr("Opening the window and loading its fonts"));
  if (!win_new(book_new(), "")) { printf("Ledger: the window could not be made\n"); goto done; }
  if (start[0]) {
    static char what[300];
    snprintf(what, sizeof what, tr("Opening %s"), (const char *)FilePart((STRPTR)start));
    splash_front();
    splash_step(85, what);
    open_path(start);
  }
  splash_close();
  if (!theme_fonts_ok()) {
    static char fmsg[640];
    snprintf(fmsg, sizeof fmsg, "%s\n\n%s", tr("Ledger couldn't open its own screen fonts, so text is drawn in the\nsystem font and won't look as it should.\n\nKeep the Fonts drawer next to Ledger."), theme_fonts_why());
    ask("OK", "%s", fmsg);
  }
  if (missing_screen[0]) {
    static char smsg[200];
    snprintf(smsg, sizeof smsg, tr("The public screen \"%s\" isn't open, so Ledger opened on the default one."), missing_screen);
    ask("OK", "%s", smsg);
  }
  for (;;) {
    ULONG id = DoMethod(app, MUIM_Application_NewInput, (ULONG)&sigs);
    if (qlog && id) { fprintf(qlog, "return id %lu\n", (unsigned long)id); fflush(qlog); }
    if (id >= ID_WINACT && id < ID_WINACT + MAXWIN) { win_switch((int)(id - ID_WINACT)); id = 0; }
    else if (id >= ID_WINCLOSE && id < ID_WINCLOSE + MAXWIN) {
      win_switch((int)(id - ID_WINCLOSE));
      win_close();
      if (!win_count()) break;
      id = 0;
    }
    if (id == MUIV_Application_ReturnID_Quit || id == ID_QUIT) { if (win_close_all()) break; }
    else if (id == ID_CLOSE) { win_close(); if (!win_count()) break; }
    else if (id == ID_NEXTWIN) win_next();
    else if (id == ID_SELCHG) update_controls();
    else if (id == ID_BRUSHDONE) brush_done();
    else if (id == ID_CHANGED) { update_controls(); set_title(); DoMethod(tabs, MUIM_Tabs_Refresh); }
    else if (id == ID_EDITCHG || id == ID_EDITDONE) update_controls();
    else if (id == ID_FILETAB || id == ID_BACKSTAGE || id == ID_TEMPLATE || id == ID_RECENT || id == ID_OPTIONS) backstage_command(id);
    else if ((id >= ID_FONTDD && id <= ID_FN_MORE)) ribbon_choice(id);
    else if (id >= ID_TBL_FIRST && id < ID_TBL_LAST) tabletab_command(id, (LONG)xget(ribbon, MUIA_Ribbon_Value));
    else if (id) {
      if (back_shown && (id == ID_OPEN || id == ID_SAVE || id == ID_SAVEAS || id == ID_SAVECSV || id == ID_NEW)) backstage_show(0, 0);
      command(id);
    }
    if (id && !keeps_focus(id) && id != ID_NAMEBOX && id != ID_FXTYPED && win && !back_shown && xget(win, MUIA_Window_ActiveObject) != (ULONG)fxbar)
      set(win, MUIA_Window_ActiveObject, (ULONG)grid);
    if (sigs) {
      sigs = Wait(sigs | SIGBREAKF_CTRL_C);
      if (sigs & SIGBREAKF_CTRL_C) break;
    }
  }
  rc = 0;
done:
  if (curwin >= 0 && wins[curwin].used) win_save(&wins[curwin]);
  for (i = 0; i < MAXWIN; i++) if (wins[i].used && wins[i].book) { book_free(wins[i].book); wins[i].book = NULL; }
  if (curwin < 0 && book) book_free(book);
  book = NULL;
  clip_free(clip); clip = NULL;
  free(clip_text);
  if (app) MUI_DisposeObject(app);
  grid_class_free();
  ribbon_class_free();
  panes_free();
  ticklist_class_free();
  backstage_class_free();
  qbutton_class_free();
  fonts_free(&fonts);
  theme_free_fonts();
  remove_fonts();
  free_home();
  splash_close();
  if (qlog) { fclose(qlog); qlog = NULL; }
  if (DiskfontBase) CloseLibrary(DiskfontBase);
  if (MUIMasterBase) CloseLibrary(MUIMasterBase);
  if (KeymapBase) CloseLibrary(KeymapBase);
  if (UtilityBase) CloseLibrary(UtilityBase);
  if (LayersBase) CloseLibrary(LayersBase);
  if (GfxBase) CloseLibrary((struct Library *)GfxBase);
  if (IntuitionBase) CloseLibrary((struct Library *)IntuitionBase);
  return rc;
}
