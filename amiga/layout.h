/*
 * layout.h - the Page Layout, Formulas, Review and View tabs' newer commands, the File page's
 * export and properties, and the Home tab's remaining ones (layouttab.c, viewtab.c, formtab.c),
 * with the small hooks main.c, ribbon.c and grid.c call.
 *
 * Their command ids live in a range of their own (ID_LX_FIRST..ID_LX_LAST), between the grid's
 * notifications (1000) and the windows' (3000); main.c hands each to lx_command(). Their ribbon
 * lists are numbered from LXL_FIRST (ribbon.c looks them up in lx_lists[]) and their check states
 * from LXB_FIRST (ribbon.c asks lx_bit()).
 */
#ifndef LEDGER_LAYOUT_H
#define LEDGER_LAYOUT_H

#include <exec/types.h>
#include <graphics/rastport.h>
#include "../core/book.h"

enum {
  ID_LX_FIRST = 2000,
  /* Page Layout */
  ID_LX_THEMEDD = ID_LX_FIRST, ID_LX_COLORSDD, ID_LX_FONTSDD, ID_LX_MARGINSDD, ID_LX_ORIENTDD, ID_LX_SIZEDD, ID_LX_AREADD, ID_LX_BREAKSDD,
  ID_LX_TITLES, ID_LX_PAGESETUP, ID_LX_HEADERFOOTER, ID_LX_FITWDD, ID_LX_FITHDD, ID_LX_SCALEDD, ID_LX_PRINTGRID, ID_LX_PRINTHEAD, ID_LX_VIEWGRID, ID_LX_VIEWHEAD,
  /* File */
  ID_LX_EXPORTPDF, ID_LX_EXPORTHTML, ID_LX_PROPS, ID_LX_CALCOPTS, ID_LX_OPTIONS, ID_LX_SAVEPDF,
  /* View */
  ID_LX_NORMAL, ID_LX_PBP, ID_LX_PAGELAYOUT, ID_LX_CUSTOMVIEWS, ID_LX_RULER, ID_LX_NEWWIN, ID_LX_ARRANGE, ID_LX_SPLIT, ID_LX_HIDEWIN,
  ID_LX_UNHIDEWIN, ID_LX_SIDEBYSIDE, ID_LX_SYNCSCROLL, ID_LX_RESETWIN, ID_LX_SWITCHDD, ID_LX_ZOOMDLG,
  /* Formulas */
  ID_LX_RECENTDD, ID_LX_USENAMEDD, ID_LX_CREATENAMES, ID_LX_TRACEPREC, ID_LX_TRACEDEP, ID_LX_REMOVEDD, ID_LX_REMOVEARROWS, ID_LX_ERRCHECK,
  ID_LX_EVALUATE, ID_LX_WATCH, ID_LX_CALCSHEET, ID_LX_CALCOPTDD,
  /* Review */
  ID_LX_SPELLING, ID_LX_THESAURUS, ID_LX_ACCESS, ID_LX_NOTESHOWHIDE, ID_LX_COMMENTNEW, ID_LX_COMMENTDEL, ID_LX_COMMENTPREV, ID_LX_COMMENTNEXT,
  ID_LX_COMMENTSHOW, ID_LX_CONVERTNOTES,
  /* Home */
  ID_LX_GOTOSPECIAL, ID_LX_MOVECOPY, ID_LX_PASTESPECIAL, ID_LX_CLEARNOTES, ID_LX_CLEARLINKS, ID_LX_JUSTIFY, ID_LX_ACCOUNTING, ID_LX_DBLUNDER,
  ID_LX_TEXTORIENTDD, ID_LX_NEWCELLSTYLE, ID_LX_SUPER, ID_LX_SUB,
  /* windows and buttons of the modeless windows (spelling, watch window, error checking) */
  ID_LX_REFRESH, ID_LX_SPCHANGE, ID_LX_SPCHANGEALL, ID_LX_SPIGNORE, ID_LX_SPIGNOREALL, ID_LX_SPADD, ID_LX_SPPICK, ID_LX_SPCLOSE,
  ID_LX_WATCHADD, ID_LX_WATCHDEL, ID_LX_WATCHPICK, ID_LX_ERRNEXT, ID_LX_ERRPREV, ID_LX_ERRHELP, ID_LX_ERREDIT, ID_LX_ERRIGNORE, ID_LX_ERREVAL,
  ID_LX_SCROLLED,
  ID_LX_LAST
};

/* the ribbon lists (Item.list), indexes into lx_lists[] from LXL_FIRST */
enum {
  LXL_FIRST = 100,
  LXL_THEMES = LXL_FIRST, LXL_THEMECOLORS, LXL_THEMEFONTS, LXL_MARGINS, LXL_ORIENT, LXL_PAPERS, LXL_AREA, LXL_BREAKS, LXL_FITW, LXL_FITH,
  LXL_SCALE, LXL_RECENT, LXL_USENAMES, LXL_REMOVE, LXL_CALCOPTS, LXL_SWITCH, LXL_TEXTORIENT,
  LXL_LAST
};
/* a list as ribbon.c draws it: labels (Latin-1), how many, kind (0 text, 1 colours with colors[]) */
typedef struct { const char *const *labels; int n; int kind; const ULONG *colors; } RList;
extern RList lx_lists[LXL_LAST - LXL_FIRST];

/* check states (Item.st) from LXB_FIRST: lx_bit() says whether each is on */
enum {
  LXB_FIRST = 32,
  LXB_PRINTGRID = LXB_FIRST, LXB_PRINTHEAD, LXB_NORMAL, LXB_PBP, LXB_PAGELAYOUT, LXB_RULER, LXB_SPLIT, LXB_SIDEBYSIDE, LXB_SYNC,
  LXB_ARROWS, LXB_HASFORMULA, LXB_LANDSCAPE, LXB_NOTESHOWN, LXB_MANYWIN, LXB_VIEWGRID, LXB_VIEWHEAD, LXB_COMMENTS,
  LXB_LAST
};
int   lx_bit(int b);
ULONG lx_bits_hash(void);                 /* changes whenever one of the bits does (ribbon.c redraws) */

/* main.c's hooks */
void  lx_command(ULONG id);                /* an ID_LX_ command */
int   lx_ribbon_choice(ULONG id, LONG v);  /* a choice from one of the Home tab's lists that Ledger's own code doesn't know (1: done) */
void  lx_update(void);                     /* after any change: watch window, error checking, ribbon lists */
void  lx_init(void);                       /* at start: the dictionary, the ribbon lists */
void  lx_cleanup(void);                    /* at the end */

/* grid.c's hooks: the grid's last layout, for drawing on top of the cells and in Page Layout view */
typedef struct {
  Object *obj;
  Book *book; int sheet;
  LONG zoom;                               /* per cent */
  LONG hw, hh, w, h;                       /* headings' size, the grid's size */
  int nvr, nvc;
  const i32 *vr; const LONG *vy;           /* the rows shown and their tops (nvr + 1 entries in vy) */
  const i32 *vc; const LONG *vx;
  LONG (*pen)(void *pd, ULONG rgb); void *pd;   /* a pen for a colour */
} GridGeom;
void  lx_grid_draw(struct RastPort *rp, const GridGeom *g);   /* page breaks, page numbers, trace arrows, marked cells */
/* the mouse in the grid (x, y from its top left; what: 0 down, 1 move, 2 up): 1 if it was taken
   (dragging a page break in Page Break Preview) */
int   lx_grid_mouse(Object *obj, int what, LONG x, LONG y);
/* Page Layout view: the extra pixels before row (rows) or column idx of sheet: the gap between pages */
LONG  lx_gap_before(Book *b, int sheet, int rows, i32 idx, LONG zoom);
void  lx_selection_moved(void);           /* (Go To Special's marks go when the selection moves) */

/* File > Options: Ledger's own lines in Ledger.prefs, and a new workbook as the options say */
#include <stdio.h>
int   lx_pref_line(const char *line);      /* 1 if the line was one of these options */
void  lx_pref_save(FILE *f);
void  lx_save_prefs(void);                 /* (main.c) Ledger.prefs written again */
Book *lx_new_book(void);

/* main.c's workbook windows (View > Window) */
int   win_slots(void);
int   win_current(void);
int   win_info(int i, Object **w, Book **b, const char **path, int *hidden);   /* 0 if slot i is free */
void  win_activate(int i);                /* brought forward (and shown again if hidden) */
int   win_new_shared(void);               /* New Window: another window on the same workbook */
void  win_hide(void);
#include "../core/ops.h"
Clip *app_clip(void);              /* what was copied last */

/* the menus (main.c's menus[], with its M, I, T and BAR) */
#define LX_PROJECT_ITEMS \
  I("Export as PDF...", "P", ID_LX_EXPORTPDF), I("Export as Web Page...", 0, ID_LX_EXPORTHTML), I("Properties...", 0, ID_LX_PROPS), \
  I("Calculation Options...", 0, ID_LX_CALCOPTS), I("Ledger Options...", 0, ID_LX_OPTIONS),
#define LX_EDIT_ITEMS \
  I("Go To Special...", 0, ID_LX_GOTOSPECIAL), I("Paste Special...", 0, ID_LX_PASTESPECIAL), I("Clear Notes", 0, ID_LX_CLEARNOTES), \
  I("Clear Hyperlinks", 0, ID_LX_CLEARLINKS), I("Fill Justify", 0, ID_LX_JUSTIFY),
#define LX_FORMAT_ITEMS \
  I("Move or Copy Sheet...", 0, ID_LX_MOVECOPY), I("Accounting Format", 0, ID_LX_ACCOUNTING), I("Double Underline", 0, ID_LX_DBLUNDER), \
  I("Superscript", 0, ID_LX_SUPER), I("Subscript", 0, ID_LX_SUB), I("Text Orientation...", 0, ID_LX_TEXTORIENTDD), I("New Cell Style...", 0, ID_LX_NEWCELLSTYLE),
#define LX_MENUS \
  M("Page"), \
  I("Page Setup...", 0, ID_LX_PAGESETUP), I("Themes...", 0, ID_LX_THEMEDD), I("Margins...", 0, ID_LX_MARGINSDD), I("Orientation...", 0, ID_LX_ORIENTDD), \
  I("Paper Size...", 0, ID_LX_SIZEDD), I("Print Area...", 0, ID_LX_AREADD), I("Page Breaks...", 0, ID_LX_BREAKSDD), I("Print Titles...", 0, ID_LX_TITLES), I("Header & Footer...", 0, ID_LX_HEADERFOOTER), \
  I("Scale to Fit...", 0, ID_LX_SCALEDD), BAR, \
  T("Print Gridlines", 0, ID_LX_PRINTGRID), T("Print Headings", 0, ID_LX_PRINTHEAD), BAR, \
  T("Normal View", 0, ID_LX_NORMAL), T("Page Break Preview", 0, ID_LX_PBP), T("Page Layout View", 0, ID_LX_PAGELAYOUT), T("Ruler", 0, ID_LX_RULER), \
  I("Custom Views...", 0, ID_LX_CUSTOMVIEWS), \
  M("Formulas"), \
  I("Recently Used Functions...", 0, ID_LX_RECENTDD), I("Use Name in Formula...", 0, ID_LX_USENAMEDD), I("Create Names from Selection...", 0, ID_LX_CREATENAMES), BAR, \
  I("Trace Precedents", 0, ID_LX_TRACEPREC), I("Trace Dependents", 0, ID_LX_TRACEDEP), I("Remove Arrows", 0, ID_LX_REMOVEARROWS), \
  I("Error Checking...", 0, ID_LX_ERRCHECK), I("Evaluate Formula...", 0, ID_LX_EVALUATE), I("Watch Window", 0, ID_LX_WATCH), BAR, \
  I("Calculate Sheet", 0, ID_LX_CALCSHEET), \
  M("Review"), \
  I("Spelling...", 0, ID_LX_SPELLING), I("Thesaurus...", 0, ID_LX_THESAURUS), I("Check Accessibility...", 0, ID_LX_ACCESS), BAR, \
  T("Show/Hide Note", 0, ID_LX_NOTESHOWHIDE), I("Convert Notes to Comments", 0, ID_LX_CONVERTNOTES), BAR, \
  I("New Comment...", 0, ID_LX_COMMENTNEW), I("Delete Comment", 0, ID_LX_COMMENTDEL), I("Previous Comment", 0, ID_LX_COMMENTPREV), \
  I("Next Comment", 0, ID_LX_COMMENTNEXT), I("Show Comments...", 0, ID_LX_COMMENTSHOW), \
  M("Window"), \
  I("New Window", 0, ID_LX_NEWWIN), I("Arrange All...", 0, ID_LX_ARRANGE), I("Switch Windows...", 0, ID_LX_SWITCHDD), BAR, \
  I("Hide", 0, ID_LX_HIDEWIN), I("Unhide...", 0, ID_LX_UNHIDEWIN), BAR, \
  T("View Side by Side", 0, ID_LX_SIDEBYSIDE), T("Synchronous Scrolling", 0, ID_LX_SYNCSCROLL), I("Reset Window Position", 0, ID_LX_RESETWIN), BAR, \
  T("Split", 0, ID_LX_SPLIT), I("Zoom...", 0, ID_LX_ZOOMDLG),

#endif
