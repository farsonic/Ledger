/*
 * ledger.h - declarations shared by Ledger's Amiga front end (MUI): the grid, the ribbon, the
 * status bar and sheet tabs, the File page, the dialogs' look, the clipboard.
 *
 * For newcomers to MUI: each custom class here is a BOOPSI class made with MUI_CreateCustomClass.
 * Its attributes (MUIA_) are tags read with get() and written with set(), and "notify" means
 * other objects can watch the attribute with MUIM_Notify. Its methods (MUIM_) are messages sent
 * with DoMethod(obj, MUIM_..., ...), the struct MUIP_ beside each one being the message's layout.
 * All of Ledger's tags sit in the TAG_USER range at QTAG, away from MUI's own.
 */
#ifndef LEDGER_AMIGA_H
#define LEDGER_AMIGA_H

#include <exec/types.h>
#include <intuition/intuition.h>
#include <intuition/classusr.h>
#include <libraries/mui.h>
#include "../core/book.h"
#include "../core/fonts.h"

extern struct Library *MUIMasterBase;

#define QTAG (TAG_USER | 0x4C470000UL)
/* a colour list's "More Colours..." entry (main.c opens the colour window instead) */
#define COLOR_MORE 0xFE000000UL

/* ------------------------------------------------------------------ the grid (grid.c) */
#define MUIA_Grid_Book      (QTAG + 1)     /* Book *: the workbook shown (the grid doesn't own it) */
#define MUIA_Grid_Fonts     (QTAG + 2)     /* FontSet *: for drawing the cells' fonts */
#define MUIA_Grid_Zoom      (QTAG + 3)     /* percent (the active sheet's Sheet.zoom) */
#define MUIA_Grid_Changed   (QTAG + 4)     /* (notify) the workbook was changed by the grid (typing, a drag) */
#define MUIA_Grid_Moved     (QTAG + 5)     /* (notify) the active cell or the selection moved */
#define MUIA_Grid_Editing   (QTAG + 6)     /* (get, notify) 0 not editing, 1 entering (typing started on a cell), 2 editing (F2) */
#define MUIA_Grid_EditText  (QTAG + 7)     /* (get) the text being typed (Latin-1); (notify) it changed */
#define MUIA_Grid_Formulas  (QTAG + 8)     /* show formulas instead of their results */
#define MUIA_Grid_VProp     (QTAG + 9)     /* the scroll bars the grid keeps up to date (flatscroll objects) */
#define MUIA_Grid_HProp     (QTAG + 10)
#define MUIA_Grid_Top       (QTAG + 11)    /* (set) the first scrolling row shown (from the scroll bar) */
#define MUIA_Grid_Left      (QTAG + 12)    /* (set) the first scrolling column shown */
#define MUIA_Grid_CtxPick   (QTAG + 13)    /* (get) the right-button menu's last choice: an ID_ */
#define MUIA_Grid_Brush     (QTAG + 14)    /* the format painter is armed: the next selection gets the copied look */
#define MUIA_Grid_Copied    (QTAG + 15)    /* (set) Rect * of the cells copied (marching outline), NULL none */
#define MUIA_Grid_Status    (QTAG + 16)    /* the status bar the grid tells about point mode */
#define MUIA_Grid_ShowNotes (QTAG + 18)    /* every note shown beside its cell (else only the one under the mouse) */
#define MUIA_Grid_FilterCol (QTAG + 17)    /* (get) the column whose AutoFilter button was clicked (after ID_FILTERCOL) */
#define MUIA_Grid_ObjView   (QTAG + 40)    /* (get) the grid's charts and drawings view (chartview.h) */

#define MUIM_Grid_Refresh   (QTAG + 0x100)   /* everything drawn afresh (after main.c changed the workbook) */
#define MUIM_Grid_Edit      (QTAG + 0x101)   /* struct MUIP_Grid_Edit: start editing the active cell */
struct MUIP_Grid_Edit { ULONG MethodID; const char *text; LONG mode; };   /* text: Latin-1 to start with (NULL: the cell's own); mode as MUIA_Grid_Editing */
#define MUIM_Grid_Commit    (QTAG + 0x102)   /* struct MUIP_Grid_Commit: the edit put into the cell; 1 done, 0 a formula error (still editing) */
struct MUIP_Grid_Commit { ULONG MethodID; LONG move; };   /* move: 0 stay, 1 down, 2 right, 3 up, 4 left; or 5 the whole selection (Ctrl-Return) */
#define MUIM_Grid_Cancel    (QTAG + 0x103)   /* the edit dropped */
#define MUIM_Grid_Show      (QTAG + 0x104)   /* scrolled to show the active cell */
#define MUIM_Grid_Insert    (QTAG + 0x105)   /* struct MUIP_Grid_Insert: text typed at the editor's caret (editing starts if need be) */
struct MUIP_Grid_Insert { ULONG MethodID; const char *text; LONG back; };   /* back: then the caret moved back this many characters */
#define MUIM_Grid_Blink     (QTAG + 0x106)   /* the grid's timer: the caret blinks, the copied outline marches */
#define MUIM_Grid_Select    (QTAG + 0x107)   /* struct MUIP_Grid_Select: the selection set and shown */
struct MUIP_Grid_Select { ULONG MethodID; LONG r1, c1, r2, c2; LONG cur_r, cur_c; };
#define MUIM_Grid_SetText   (QTAG + 0x108)   /* struct MUIP_Grid_Insert: the editor's text replaced (the formula bar typed in) */
#define MUIM_Grid_Autofit   (QTAG + 0x109)   /* struct MUIP_Grid_Autofit: columns or rows sized to their contents */
struct MUIP_Grid_Autofit { ULONG MethodID; LONG rows; LONG from, to; };
#define MUIM_Grid_TextWidth (QTAG + 0x10A)   /* (internal use by Autofit) */

struct MUI_CustomClass *grid_class(void);
void grid_class_free(void);
/* a cell's text as shown (formatted, Latin-1) and as edited (its formula with "=", or its value) */
void grid_cell_text(Book *b, int sheet, i32 r, i32 c, int formulas, char *out, size_t cap, u32 *color);
void grid_edit_text(Book *b, int sheet, i32 r, i32 c, char *out, size_t cap);

/* ------------------------------------------------------------------ the ribbon (ribbon.c) */
/* GridCraft's tabs and groups, drawn by Ledger. A button's click is a MUIM_Application_ReturnID of
   its ID_ (ids.h); a choice from a list also sets MUIA_Ribbon_Value to the entry chosen */
#define MUIA_Ribbon_Value  (QTAG + 0x300)
#define MUIA_Ribbon_Tab    (QTAG + 0x301)     /* the tab shown */
#define MUIM_Ribbon_State  (QTAG + 0x310)
enum {
  RB_BOLD = 1, RB_ITALIC, RB_UNDERLINE, RB_STRIKE, RB_CANUNDO, RB_CANREDO, RB_WRAP, RB_MERGED,
  RB_ALEFT, RB_ACENTER, RB_ARIGHT, RB_ATOP, RB_AMIDDLE, RB_ABOTTOM, RB_GRIDLINES, RB_HEADINGS, RB_FORMULABAR,
  RB_FORMULAS, RB_MANUAL, RB_BRUSH, RB_DARK, RB_ZEROS, RB_MULTI, RB_EDITING, RB_FROZEN, RB_FILTER, RB_NOTES, RB_HASNOTE
};
#define RBIT(b) (1UL << (b))
struct MUIP_Ribbon_State {
  ULONG MethodID;
  ULONG on;                 /* RBIT(RB_...) */
  const char *font;         /* the active cell's font (UTF-8) and its size in tenths of a point */
  int size;
  const char *numfmt;       /* the number format's name as the Number field shows it (Latin-1) */
  FontSet *fs;
  ULONG ext;                /* more states, the Item.st/en values 32-63 (chartids.h's RBX_) */
};
struct MUI_CustomClass *ribbon_class(void);
/* (chartrib.h) the Format tab's list of the selected chart's parts (labels kept by the caller), the one shown */
void ribbon_set_elems(const char *const *labels, int n, int shown);
void ribbon_class_free(void);
/* the font list: the usual families, then the installed ones (made once); one by number */
const char *const *ribbon_font_names(FontSet *fs);
const char *ribbon_font_name(int i);
/* the label of entry v of a list item's list (the item that returns id), or NULL (English) */
const char *ribbon_entry(ULONG id, LONG v);
/* the look of cell style k from the Cell Styles list: fill (0 none), font colour, bold, a line under
   it (1 thin, 2 thick, 3 double with a thin one over), size in points (0 unchanged); 0 if no such style */
int ribbon_cell_style(int k, ULONG *fill, ULONG *color, int *bold, int *line, int *size);

/* ------------------------------------------------------------------ status bar, sheet tabs, scroll bars (panes.c) */
#define MUIA_Status_Zoom (QTAG + 0x420)     /* (get) the zoom chosen on the slider, after ID_ZOOMSET */
#define MUIM_Status_Set  (QTAG + 0x430)
struct MUIP_Status_Set {
  ULONG MethodID;
  const char *mode;         /* "Ready", "Enter", "Edit", "Point" (English: translated as drawn) */
  const char *stats;        /* "Average: 3  Count: 4  Sum: 12" (Latin-1), "" none; copied */
  LONG zoom;
  int manual;               /* calculation is manual: "Calculate" shown */
};
#define MUIA_Tabs_Book   (QTAG + 0x450)     /* Book *: whose sheets the tabs are */
#define MUIA_Tabs_Value  (QTAG + 0x451)     /* (get) the sheet clicked (ID_SHEETTAB) or chosen in the menu */
#define MUIM_Tabs_Refresh (QTAG + 0x460)    /* the sheets changed (names, order, colours, the active one) */
#define MUIA_FlatScroll_Horiz (QTAG + 0x440)  /* (init) across instead of up and down */
struct MUI_CustomClass *status_class(void);
struct MUI_CustomClass *tabs_class(void);
struct MUI_CustomClass *flatscroll_class(void);   /* a flat scroll bar: MUIA_Prop_Entries, _Visible, _First */
void panes_free(void);

/* ------------------------------------------------------------------ a list of tick boxes (ticklist.c) */
#define MUIA_Tick_Labels (QTAG + 0x600)     /* (init) const char *const *: the lines (Latin-1, kept by the caller) */
#define MUIA_Tick_States (QTAG + 0x601)     /* (init) UBYTE *: each line's tick, changed in place */
#define MUIA_Tick_Count  (QTAG + 0x602)     /* (init) how many lines */
#define MUIA_Tick_Prop   (QTAG + 0x603)     /* (init) the flat scroll bar beside it (kept up to date) */
#define MUIA_Tick_First  (QTAG + 0x604)     /* (set) the first line shown (from the scroll bar) */
#define MUIA_Tick_All    (QTAG + 0x605)     /* (set) every line ticked (TRUE) or none */
struct MUI_CustomClass *ticklist_class(void);
void ticklist_class_free(void);

/* ------------------------------------------------------------------ the File page (backstage.c) */
#define MUIA_Back_Value    (QTAG + 0x500)   /* (get) the template or recent workbook chosen */
#define MUIA_Back_Page     (QTAG + 0x501)   /* (set) the page shown: 0 Home, 1 New, 2 Open, 3 Info, 4 Export, 5 Options, 6 About */
#define MUIA_Back_Metric   (QTAG + 0x502)   /* (get) the date order chosen in Options: day first (else month first) */
#define MUIA_Back_UserName (QTAG + 0x503)   /* (get) the name given in Options (Latin-1) */
#define MUIA_Back_Dark     (QTAG + 0x504)   /* (get) the interface theme chosen: dark (else light) */
#define MUIA_Back_Language (QTAG + 0x505)   /* (get) the interface language chosen (LANG_) */
#define MUIA_Back_Large    (QTAG + 0x506)   /* (get) the interface's text large */
#define MUIM_Back_Set      (QTAG + 0x510)
struct MUIP_Back_Set {
  ULONG MethodID;
  const char *const *recent; int nrecent;   /* paths, the latest first */
  const char *info;                          /* the Info page: "label\tvalue" lines */
  const char *username;
  int metric, dark, lang, large;
};
struct MUI_CustomClass *backstage_class(void);
void backstage_class_free(void);

/* ------------------------------------------------------------------ shared bits */
/* an object typing goes to instead of the grid (the formula bar or name box while active), or NULL */
extern Object *key_grab;
/* the public screen Ledger's windows open on ("": the default one); QWindowObject is a window on it */
extern char pub_screen[64];
/* a dialog's contents in Ledger's font and colours (after a group's opening: VGroup, QDLG, ...) */
#define QDLG (ULONG)(theme_dialog_font() ? MUIA_Font : TAG_IGNORE), (ULONG)theme_dialog_font(), \
             (ULONG)(theme_dialog_back() ? MUIA_Background : TAG_IGNORE), (ULONG)theme_dialog_back()
struct TextFont *theme_dialog_font(void);
const char *theme_dialog_back(void);
/* (qbutton.c) a dialog button, a label and a section title drawn in Ledger's theme */
Object *QButton(const char *label);
Object *QLabel(const char *label, ULONG flags);
Object *QTitleObject(const char *t);
struct MUI_CustomClass *qbutton_class(void);
void qbutton_class_free(void);
#define QWindowObject WindowObject, (ULONG)(pub_screen[0] ? MUIA_Window_PublicScreen : TAG_IGNORE), (ULONG)pub_screen

/* the clipboard (unit 0): FTXT text, Latin-1. clip_read: malloc()ed, 0-terminated, or NULL */
int   clip_write(const char *s, long len);
char *clip_read(long *len);

/* debugging log (DEBUG=file), or NULL */
#include <stdio.h>
extern FILE *qlog;

#endif
