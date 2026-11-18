/*
 * app.h - what main.c and dialogs.c share: the window being worked in (its objects and workbook),
 * the font set and preferences, and the helpers both use.
 *
 * As in Quill, each workbook has a window of its own, and these globals always belong to the window
 * being worked in (main.c swaps them when another window is activated).
 */
#ifndef LEDGER_APP_H
#define LEDGER_APP_H

#include "ledger.h"
#include "../core/ops.h"

extern Object *app, *win, *grid, *ribbon, *statusbar, *tabs, *backstage, *pages, *vscroll, *hscroll, *namebox, *fxbar, *fxgroup;
extern Book *book;
extern char bookpath[512];
extern FontSet fonts;
extern char username[64];
extern int pref_dark, pref_large, pref_fxbar;

/* the active sheet, its index, the selection */
Sheet *cur_sheet(void);
int    cur_index(void);
Rect   cur_sel(void);
/* after a change to the workbook: the grid, tabs, ribbon, formula bar and status bar brought up to date */
void   refresh_all(void);
void   update_controls(void);

/* a requester (gadgets "OK|Cancel"; returns the button, 1 the first, 0 the last) */
int    ask(const char *gadgets, const char *fmt, const char *arg);
/* after a CSV import (csv_load_report): a note about a column whose dates read either way, or
   that has both kinds (kept as text); nothing if both are -1 */
void   csv_dates_note(int either_col, int both_col);
/* a modal dialog: the window opened over the workbook's (which sleeps meanwhile) until one of the
   ids in ends (0-terminated) or its close gadget (ID_DLGCANCEL); each other id goes to each(), if
   given. Returns the id that ended it. The window is added to the application and removed again;
   the caller disposes of it */
ULONG  run_modal(Object *w, const ULONG *ends, void (*each)(ULONG id));
/* a window in Ledger's dialog look: title, contents, and OK/Cancel at the bottom (ok label) */
Object *dialog_window(const char *title, Object *contents, const char *oklabel);
/* a line of text asked for: label, the starting text in buf (Latin-1); 1 if OK */
int    ask_string(const char *title, const char *label, char *buf, int cap);
/* a colour chosen (More Colours...): 0xRRGGBB, or -1 if cancelled */
LONG   ask_color(ULONG start);

/* dialogs.c */
void   dlg_format_cells(int page);       /* 0 Number, 1 Alignment, 2 Font, 3 Border, 4 Fill */
void   dlg_insert_function(void);
void   dlg_names(void);
void   dlg_define_name(void);
void   dlg_sort(void);
void   cond_choice(int v);                 /* the Conditional Formatting list's entry v (ribbon.c's condfmts order) */
void   dlg_note(void);
void   dlg_link(void);                     /* Insert Link: the active cell made a link (or its link changed) */                     /* the active cell's note written or changed */
void   dlg_filter(i32 col);               /* an AutoFilter button's list: sort, pick the values shown */
void   dlg_find(int replace);
void   dlg_goto(void);
void   dlg_series(void);
void   dlg_stats(void);
void   dlg_unhide_sheet(void);
void   dlg_insert_cells(int del);
int    dlg_number_format(char *code, int cap);   /* More Number Formats: a custom code; 1 if OK */

/* formatting through book_restyle (main.c): what the edit does to each style */
enum { FX_BOLD, FX_ITALIC, FX_UNDERLINE, FX_STRIKE, FX_FONT, FX_SIZE, FX_COLOR, FX_FILL, FX_HALIGN, FX_VALIGN, FX_WRAP,
       FX_INDENT, FX_NUMFMT, FX_BORDER, FX_STYLE, FX_GROW };
typedef struct { int what; long value; const char *text; const Style *st; const Font *font; } FmtEdit;
void   apply_format(int what, long value, const char *text);
void   apply_style(const Style *st, const Font *f, unsigned mask);   /* (Format Cells: the masked parts of st and f) */
enum { SM_NUMFMT = 1, SM_HALIGN = 2, SM_VALIGN = 4, SM_WRAP = 8, SM_INDENT = 16, SM_FONT = 32, SM_BORDER = 64, SM_FILL = 128, SM_PROT = 256 };
/* the number format of a category of the Number list (ribbon order), with decimals places */
const char *numfmt_code(int category, int decimals, char *out, size_t cap);
/* the category a format code belongs to, for the Number field (Latin-1 name) */
const char *numfmt_name(const char *code);

#endif
