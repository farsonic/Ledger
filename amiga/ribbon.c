/*
 * ribbon.c - the ribbon, as GridCraft has it: a strip of tabs, and under it the tab's groups of
 * tools with their captions, all drawn by Ledger (a MUI custom class) in the theme's colours and
 * a sans-serif font. Lists (fonts, sizes, colours, number formats, functions...) open in a small
 * borderless window (PopList). It is Quill's ribbon with a spreadsheet's tools.
 *
 * The tools are data: tabs of groups of items. A group lays its items out in columns (a large
 * button, or small ones three to a column) or in rows (the Font, Alignment and Number groups).
 */
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/intuition.h>
#include <proto/graphics.h>
#include <graphics/gfxmacros.h>
#include <proto/muimaster.h>
#include <proto/utility.h>
#include <clib/alib_protos.h>
#include "ledger.h"
#include "theme.h"
#include "lang.h"
#include "ids.h"
#include "tabletab.h"
#include "datatab.h"
#include "anatab.h"
#include "layout.h"
#include "uitab.h"
#include "../core/funcdesc.h"

/* ------------------------------------------------------------------ */
/* the lists */
enum { PL_TEXT, PL_COLOR, PL_FONTS, PL_CELLSTYLE, PL_BORDER, PL_TSTYLE };
/* a drop-down list: its entries (Latin-1), how many, how they are drawn (PL_), and for PL_COLOR
   the 0xRRGGBB beside each (0xFF000000 is COLOR_AUTO: automatic, or none) */
typedef struct { const char *const *labels; int n; int kind; const ULONG *colors; } List;
/* the usual families, which every font list starts with */
static const char *const usual_fonts[] = { "Calibri", "Arial", "Times New Roman", "Courier New", "Cambria", "Liberation Sans", "Liberation Serif",
                                           "Liberation Mono", "Carlito", "Caladea", NULL };
#define MAXFONTNAMES 200
static const char *fontnames[MAXFONTNAMES + 1];
static int nfontnames;
/* the font list, made on the first call and kept to the end: the usual families, then each
   installed family not already there (NULL-terminated). The installed names point into fs */
const char *const *ribbon_font_names(FontSet *fs)
{
  const char *fam[MAXFONTNAMES];
  int i, k, n;
  if (nfontnames) return fontnames;
  for (i = 0; usual_fonts[i] && nfontnames < MAXFONTNAMES; i++) fontnames[nfontnames++] = usual_fonts[i];
  n = fs ? fonts_families(fs, fam, MAXFONTNAMES) : 0;
  for (i = 0; i < n && nfontnames < MAXFONTNAMES; i++) {
    for (k = 0; k < nfontnames && Stricmp((STRPTR)fontnames[k], (STRPTR)fam[i]); k++) ;
    if (k == nfontnames) fontnames[nfontnames++] = fam[i];
  }
  fontnames[nfontnames] = NULL;
  return fontnames;
}
const char *ribbon_font_name(int i) { return i >= 0 && i < nfontnames ? fontnames[i] : i >= 0 && i < 10 ? usual_fonts[i] : NULL; }
#define FONTPX 13                /* the font list's previews: pixels to the em */
/* the entries of the lists. A list's choice reaches main.c as the entry's number in
   MUIA_Ribbon_Value (a colour list gives the colour, the sizes list tenths of a point), so main.c's
   handling depends on this order */
static const char *const sizes[] = { "8", "9", "10", "11", "12", "14", "16", "18", "20", "22", "24", "26", "28", "36", "48", "72" };
static const char *const colornames[] = { "Automatic", "Black", "Dark Red", "Red", "Orange", "Gold", "Green", "Teal", "Blue", "Dark Blue",
                                          "Purple", "Grey", "Slate", "Steel Blue", "Sky Blue", "Burnt Orange", "Sunflower", "Leaf Green", "White",
                                          "More Colours..." };
static const ULONG colorvals[] = { 0xFF000000, 0x000000, 0xC00000, 0xFF0000, 0xFFA500, 0xBF9000, 0x00B050, 0x008080, 0x0070C0, 0x002060,
                                   0x7030A0, 0x808080, 0x44546A, 0x4472C4, 0x5B9BD5, 0xED7D31, 0xFFC000, 0x70AD47, 0xFFFFFF, COLOR_MORE };
static const char *const fillnames[] = { "No Fill", "Yellow", "Light Yellow", "Light Green", "Green", "Light Blue", "Blue", "Light Red", "Orange",
                                         "Light Grey", "Grey", "Lavender", "Dark Blue", "Black", "More Colours..." };
static const ULONG fillvals[] = { 0xFF000000, 0xFFFF00, 0xFFF2CC, 0xE2EFDA, 0x70AD47, 0xDDEBF7, 0x4472C4, 0xFCE4D6, 0xF4B084,
                                  0xF2F2F2, 0xBFBFBF, 0xE4DFEC, 0x203764, 0x000000, COLOR_MORE };
static const char *const borders[] = { "Bottom Border", "Top Border", "Left Border", "Right Border", "No Border", "All Borders", "Outside Borders",
                                       "Thick Outside Borders", "Bottom Double Border", "Thick Bottom Border", "Top and Bottom Border",
                                       "Draw Border", "Draw Border Grid", "Erase Border" };
static const char *const merges[] = { "Merge & Centre", "Merge Across", "Merge Cells", "Unmerge Cells" };
static const char *const numfmts[] = { "General", "Number", "Currency", "Accounting", "Short Date", "Long Date", "Time", "Percentage",
                                       "Fraction", "Scientific", "Text", "More Number Formats..." };
static const char *const inserts[] = { "Insert Cells...", "Insert Sheet Rows", "Insert Sheet Columns", "Insert Sheet" };
static const char *const deletes[] = { "Delete Cells...", "Delete Sheet Rows", "Delete Sheet Columns", "Delete Sheet" };
static const char *const formats[] = { "Row Height...", "AutoFit Row Height", "Column Width...", "AutoFit Column Width", "Default Width...",
                                       "Hide Rows", "Hide Columns", "Unhide Rows", "Unhide Columns", "Rename Sheet", "Tab Colour...", "Format Cells...",
                                       "Hide Sheet", "Unhide Sheet...", "Move or Copy Sheet...", "Protect Sheet...", "Lock Cell" };
static const char *const autosums[] = { "Sum", "Average", "Count Numbers", "Max", "Min", "More Functions..." };
static const char *const fills[] = { "Down", "Right", "Up", "Left", "Series...", "Justify" };
static const char *const clears[] = { "Clear All", "Clear Formats", "Clear Contents", "Clear Notes", "Clear Hyperlinks" };
static const char *const sorts[] = { "Sort A to Z", "Sort Z to A", "Custom Sort...", "Filter", "Clear Filter", "Reapply" };
static const char *const finds[] = { "Find...", "Replace...", "Go To...", "Go To Special..." };
static const char *const pastes[] = { "Paste", "Paste Values", "Paste Formulas", "Paste Formatting", "Transpose", "Paste Special..." };
static const char *const symbols[] = { "\251  Copyright", "\256  Registered", "\247  Section", "\266  Pilcrow", "\243  Pound", "\244  Currency",
                                       "\245  Yen", "\260  Degree", "\261  Plus or minus", "\327  Times", "\367  Divide", "\275  One half",
                                       "\274  One quarter", "\276  Three quarters", "\265  Micro", "\267  Middle dot" };
static const char *const tabcolors[] = { "No Colour", "Red", "Orange", "Yellow", "Green", "Teal", "Blue", "Purple", "Grey", "Black" };
static const ULONG tabcolorvals[] = { 0xFF000000, 0xC00000, 0xED7D31, 0xFFC000, 0x70AD47, 0x00B0A0, 0x4472C4, 0x7030A0, 0x808080, 0x000000 };
static const char *const calcs[] = { "Automatic", "Manual" };
static const char *const zooms[] = { "50%", "75%", "100%", "125%", "150%", "200%", "Zoom to Selection" };
static const char *const freezes[] = { "Freeze Panes", "Freeze Top Row", "Freeze First Column", "Unfreeze Panes" };
/* the function lists (each a category; what is chosen is typed as =NAME( into the cell) */
/* (filled from core/funcdesc.c by fn_lists_init: each category's functions in name order) */
#define FN_MAXPER 200
static const char *fnl[FC_N][FN_MAXPER];
/* More Functions: GridCraft's and Excel's sub-lists, each opening its category's functions */
static const char *const fn_more[] = { "Statistical", "Engineering", "Cube", "Information", "Compatibility", "Web" };
static const u8 fn_more_cat[] = { FC_STAT, FC_ENG, FC_CUBE, FC_INFO, FC_COMPAT, FC_WEB };
static const char *const uigroups[] = { "Group", "Regroup", "Ungroup" };
/* conditional formatting (main.c's cond_choice goes by these numbers) */
static const char *const condfmts[] = { "Greater Than...", "Less Than...", "Between...", "Equal To...", "Text that Contains...", "Duplicate Values...",
                                        "Top 10 Items...", "Top 10%...", "Bottom 10 Items...", "Bottom 10%...", "Above Average...", "Below Average...",
                                        "Data Bar: Blue", "Data Bar: Green", "Data Bar: Red", "Data Bar: Orange", "Data Bar: Purple",
                                        "Colour Scale: Green - Yellow - Red", "Colour Scale: Red - Yellow - Green", "Colour Scale: Green - White - Red",
                                        "Colour Scale: Blue - White - Red", "Colour Scale: White - Red", "Colour Scale: Green - White",
                                        "New Rule...", "Clear Rules from Selected Cells", "Clear Rules from Entire Sheet", "Manage Rules..." };
/* the Data tab's lists (datatab.c goes by these numbers) */
static const char *const dvs[] = { "Data Validation...", "Circle Invalid Data", "Clear Validation Circles", "Clear Validation" };
static const char *const whatifs[] = { "Scenario Manager...", "Goal Seek...", "Data Table..." };
static const char *const groups[] = { "Group...", "Ungroup...", "Clear Outline" };
/* (ledger.h) the cell styles: their names here, their looks in cell_style_look below */
static const char *const cellstyles[] = { "Normal", "Good", "Bad", "Neutral", "Title", "Heading 1", "Heading 2", "Heading 3", "Total",
                                          "Input", "Output", "Calculation", "Note", "Accent 1", "Accent 2", "Accent 3", "Accent 4" };
#define TT_LISTS
#include "tabletabs.h"
/* the lists by number (Item.list), in the order of lists[] below: the two must be kept in step */
enum { L_NONE, L_FONTS, L_SIZES, L_COLORS, L_FILLS, L_BORDERS, L_MERGES, L_NUMFMTS, L_INSERTS, L_DELETES, L_FORMATS, L_AUTOSUMS,
       L_FILL, L_CLEARS, L_SORTS, L_FINDS, L_PASTES, L_SYMBOLS, L_TABCOLORS, L_CALCS, L_ZOOMS, L_FREEZES,
       L_FNFIN, L_FNLOGIC, L_FNTEXT, L_FNDATE, L_FNLOOKUP, L_FNMATH, L_FNMORE, L_CELLSTYLES, L_CONDFMTS,
       L_TSTYLES, L_TOTFN, L_PVSUBTOT, L_PVGRAND, L_PVLAYOUT, L_PVBLANK, L_PVMOVE, L_DVS, L_WHATIFS, L_GROUPS,
       L_FNSTAT, L_FNENG, L_FNCUBE, L_FNINFO, L_FNCOMPAT, L_FNWEB, L_UIGROUPS };
#define N(a) ((int)(sizeof(a) / sizeof(a[0])))
static List lists[] = {
  { NULL, 0, PL_TEXT, NULL }, { usual_fonts, 10, PL_FONTS, NULL }, { sizes, N(sizes), PL_TEXT, NULL },
  { colornames, N(colornames), PL_COLOR, colorvals }, { fillnames, N(fillnames), PL_COLOR, fillvals }, { borders, N(borders), PL_BORDER, NULL },
  { merges, N(merges), PL_TEXT, NULL }, { numfmts, N(numfmts), PL_TEXT, NULL }, { inserts, N(inserts), PL_TEXT, NULL },
  { deletes, N(deletes), PL_TEXT, NULL }, { formats, N(formats), PL_TEXT, NULL }, { autosums, N(autosums), PL_TEXT, NULL },
  { fills, N(fills), PL_TEXT, NULL }, { clears, N(clears), PL_TEXT, NULL }, { sorts, N(sorts), PL_TEXT, NULL },
  { finds, N(finds), PL_TEXT, NULL }, { pastes, N(pastes), PL_TEXT, NULL }, { symbols, N(symbols), PL_TEXT, NULL },
  { tabcolors, N(tabcolors), PL_COLOR, tabcolorvals }, { calcs, N(calcs), PL_TEXT, NULL }, { zooms, N(zooms), PL_TEXT, NULL },
  { freezes, N(freezes), PL_TEXT, NULL },
  { fnl[FC_FIN], 0, PL_TEXT, NULL }, { fnl[FC_LOGIC], 0, PL_TEXT, NULL }, { fnl[FC_TEXT], 0, PL_TEXT, NULL },
  { fnl[FC_DATE], 0, PL_TEXT, NULL }, { fnl[FC_LOOKUP], 0, PL_TEXT, NULL }, { fnl[FC_MATH], 0, PL_TEXT, NULL },
  { fn_more, N(fn_more), PL_TEXT, NULL }, { cellstyles, N(cellstyles), PL_CELLSTYLE, NULL }, { condfmts, N(condfmts), PL_TEXT, NULL },
  { tstyles, N(tstyles), PL_TSTYLE, NULL }, { totfns, N(totfns), PL_TEXT, NULL }, { pvsubtots, N(pvsubtots), PL_TEXT, NULL },
  { pvgrands, N(pvgrands), PL_TEXT, NULL }, { pvlayouts, N(pvlayouts), PL_TEXT, NULL }, { pvblanks, N(pvblanks), PL_TEXT, NULL },
  { pvmoves, N(pvmoves), PL_TEXT, NULL },
  { dvs, N(dvs), PL_TEXT, NULL }, { whatifs, N(whatifs), PL_TEXT, NULL }, { groups, N(groups), PL_TEXT, NULL },
  { fnl[FC_STAT], 0, PL_TEXT, NULL }, { fnl[FC_ENG], 0, PL_TEXT, NULL }, { fnl[FC_CUBE], 0, PL_TEXT, NULL },
  { fnl[FC_INFO], 0, PL_TEXT, NULL }, { fnl[FC_COMPAT], 0, PL_TEXT, NULL }, { fnl[FC_WEB], 0, PL_TEXT, NULL },
  { uigroups, N(uigroups), PL_TEXT, NULL },
};
/* the function lists' entries, from the descriptions' categories */
static void fn_lists_init(void)
{
  static const struct { u8 list, cat; } m[] = { { L_FNFIN, FC_FIN }, { L_FNLOGIC, FC_LOGIC }, { L_FNTEXT, FC_TEXT }, { L_FNDATE, FC_DATE },
    { L_FNLOOKUP, FC_LOOKUP }, { L_FNMATH, FC_MATH }, { L_FNSTAT, FC_STAT }, { L_FNENG, FC_ENG }, { L_FNCUBE, FC_CUBE },
    { L_FNINFO, FC_INFO }, { L_FNCOMPAT, FC_COMPAT }, { L_FNWEB, FC_WEB } };
  static int done;
  int k;
  if (done) return;
  done = 1;
  for (k = 0; k < (int)(sizeof m / sizeof m[0]); k++) lists[m[k].list].n = func_descs_in(m[k].cat, fnl[m[k].cat], FN_MAXPER);
}
/* a list by number: Ledger's own here, or (from LXL_FIRST) layout.h's */
static const List *list_of(int k) { fn_lists_init(); return k >= LXL_FIRST && k < LXL_LAST ? (const List *)(const void *)&lx_lists[k - LXL_FIRST] : &lists[k]; }
/* each cell style's look: fill, font colour, bold, a line under it (0 none, 1 thin, 2 thick, 3 double
   over a thin top), its size in points (0 as it is) */
static const struct { ULONG fill, color; u8 bold, line, size; } stylelook[] = {
  { 0, 0xFF000000, 0, 0, 0 }, { 0xC6EFCE, 0x006100, 0, 0, 0 }, { 0xFFC7CE, 0x9C0006, 0, 0, 0 }, { 0xFFEB9C, 0x9C5700, 0, 0, 0 },
  { 0, 0x44546A, 1, 0, 18 }, { 0, 0x44546A, 1, 2, 15 }, { 0, 0x44546A, 1, 2, 13 }, { 0, 0x44546A, 1, 1, 11 }, { 0, 0xFF000000, 1, 3, 0 },
  { 0xFFCC99, 0x3F3F76, 0, 1, 0 }, { 0xF2F2F2, 0x3F3F3F, 1, 1, 0 }, { 0xF2F2F2, 0xFA7D00, 1, 1, 0 }, { 0xFFFFCC, 0xFF000000, 0, 1, 0 },
  { 0x4472C4, 0xFFFFFF, 0, 0, 0 }, { 0xED7D31, 0xFFFFFF, 0, 0, 0 }, { 0xA5A5A5, 0xFFFFFF, 0, 0, 0 }, { 0xFFC000, 0xFFFFFF, 0, 0, 0 } };
/* (for main.c) cell style k's look; 0 if there is none */
int ribbon_cell_style(int k, ULONG *fill, ULONG *color, int *bold, int *line, int *size)
{
  if (k < 0 || k >= N(stylelook)) return 0;
  *fill = stylelook[k].fill; *color = stylelook[k].color; *bold = stylelook[k].bold; *line = stylelook[k].line; *size = stylelook[k].size;
  return 1;
}

/* ------------------------------------------------------------------ */
/* the tools */
/* item kinds: a button (a ReturnID of its id when clicked); a button with an arrow that opens its
   list; a field showing a value (font, size, number format) that opens its list; a caption.
   Sizes: a small button (16-pixel icon and label), a large one (24-pixel icon over its label, the
   group's full height), an icon alone */
enum { K_BUTTON, K_DROP, K_FIELD, K_LABEL };
enum { S_SMALL, S_LARGE, S_ICON };
#define F_ROW1 1                 /* rows mode: on the second row */
#define F_NEWCOL 2               /* columns mode: start a new column */
#define F_CHECK 4                /* a check box (ticked while its state is on) instead of an icon */
#define F_SPLIT 8                /* a split button: its icon and label do split_id()'s command, the arrow opens the list */
/* a tool: its kind and size (K_, S_), its icon (IC_, theme.h), its list (L_), its label (through
   tr(); NULL for an icon alone), the command it returns (ID_), the RB_ states shown as checked and
   needed to be usable, F_ flags, a field's width in pixels, and its help */
typedef struct {
  u8 kind, size; UWORD icon; u8 list;   /* (icons past 255 since the tables' and layout's) */
  const char *label;
  UWORD id;
  u8 st, en;
  u8 flags;
  UWORD fieldw;
  const char *help;
} Item;
enum { G_COLS, G_ROWS };
typedef struct { const char *name; u8 mode; const Item *items; int n; } Group;
typedef struct { const char *name; const Group *groups; int n; } Tab;
#define TT_TOOLS
#include "tabletabs.h"

/* Save, Undo and Redo: the quick access buttons at the right end of the tab strip */
#define NQA 3
static const Item g_undo[NQA] = {
  { K_BUTTON, S_SMALL, IC_SAVE, 0, "Save", ID_SAVE, 0, 0, 0, 0, "Save (Amiga-S)" },
  { K_BUTTON, S_SMALL, IC_UNDO, 0, "Undo", ID_UNDO, 0, RB_CANUNDO, 0, 0, "Undo (Amiga-Z)" },
  { K_BUTTON, S_SMALL, IC_REDO, 0, "Redo", ID_REDO, 0, RB_CANREDO, 0, 0, "Redo (Amiga-Y)" },
};
/* Home */
static const Item h_clip[] = {
  { K_DROP, S_LARGE, IC_PASTE, L_PASTES, "Paste", ID_PASTEDD, 0, 0, F_SPLIT, 0, "Paste (Amiga-V)" },
  { K_BUTTON, S_SMALL, IC_CUT, 0, "Cut", ID_CUT, 0, 0, 0, 0, "Cut (Amiga-X)" },
  { K_BUTTON, S_SMALL, IC_COPY, 0, "Copy", ID_COPY, 0, 0, 0, 0, "Copy (Amiga-C)" },
  { K_BUTTON, S_SMALL, IC_BRUSH, 0, "Format Painter", ID_BRUSH, RB_BRUSH, 0, 0, 0, "Copy the selected cell's look to the next cells you select" },
};
static const Item h_font[] = {
  { K_FIELD, S_SMALL, 0, L_FONTS, NULL, ID_FONTDD, 0, 0, 0, 92, "Font" },
  { K_FIELD, S_SMALL, 0, L_SIZES, NULL, ID_SIZEDD, 0, 0, 0, 34, "Font size" },
  { K_BUTTON, S_ICON, IC_GROW, 0, NULL, ID_GROW, 0, 0, 0, 0, "Increase font size" },
  { K_BUTTON, S_ICON, IC_SHRINK, 0, NULL, ID_SHRINK, 0, 0, 0, 0, "Decrease font size" },
  { K_BUTTON, S_ICON, IC_SUPER, 0, NULL, ID_LX_SUPER, 0, 0, 0, 0, "Superscript" },
  { K_BUTTON, S_ICON, IC_SUB, 0, NULL, ID_LX_SUB, 0, 0, 0, 0, "Subscript" },
  { K_BUTTON, S_ICON, IC_BOLD, 0, NULL, ID_BOLD, RB_BOLD, 0, F_ROW1, 0, "Bold (Amiga-B)" },
  { K_BUTTON, S_ICON, IC_ITALIC, 0, NULL, ID_ITALIC, RB_ITALIC, 0, F_ROW1, 0, "Italic (Amiga-I)" },
  { K_BUTTON, S_ICON, IC_UNDERLINE, 0, NULL, ID_UNDERLINE, RB_UNDERLINE, 0, F_ROW1, 0, "Underline (Amiga-U)" },
  { K_BUTTON, S_ICON, IC_STRIKE, 0, NULL, ID_STRIKE, RB_STRIKE, 0, F_ROW1, 0, "Strikethrough" },
  { K_BUTTON, S_ICON, IC_DBLUNDER, 0, NULL, ID_LX_DBLUNDER, 0, 0, F_ROW1, 0, "Double underline" },
  { K_DROP, S_ICON, IC_TBORDERS, L_BORDERS, NULL, ID_BORDERDD, 0, 0, F_ROW1, 0, "Borders" },
  { K_DROP, S_ICON, IC_FILLCOLOR, L_FILLS, NULL, ID_FILLCOLOR, 0, 0, F_ROW1, 0, "Fill colour" },
  { K_DROP, S_ICON, IC_FONTCOLOR, L_COLORS, NULL, ID_FONTCOLOR, 0, 0, F_ROW1, 0, "Font colour" },
};
static const Item h_align[] = {
  { K_BUTTON, S_ICON, IC_ALIGNTOP, 0, NULL, ID_ATOP, RB_ATOP, 0, 0, 0, "Top align" },
  { K_BUTTON, S_ICON, IC_ALIGNMID, 0, NULL, ID_AMIDDLE, RB_AMIDDLE, 0, 0, 0, "Middle align" },
  { K_BUTTON, S_ICON, IC_ALIGNBOT, 0, NULL, ID_ABOTTOM, RB_ABOTTOM, 0, 0, 0, "Bottom align" },
  { K_BUTTON, S_ICON, IC_WRAPTEXT, 0, NULL, ID_WRAP, RB_WRAP, 0, 0, 0, "Wrap text" },
  { K_DROP, S_ICON, IC_TEXTORIENT, LXL_TEXTORIENT, NULL, ID_LX_TEXTORIENTDD, 0, 0, 0, 0, "Orientation: text at an angle or upright" },
  { K_BUTTON, S_ICON, IC_LEFT, 0, NULL, ID_LEFT, RB_ALEFT, 0, F_ROW1, 0, "Align left" },
  { K_BUTTON, S_ICON, IC_CENTER, 0, NULL, ID_CENTER, RB_ACENTER, 0, F_ROW1, 0, "Centre" },
  { K_BUTTON, S_ICON, IC_RIGHT, 0, NULL, ID_RIGHT, RB_ARIGHT, 0, F_ROW1, 0, "Align right" },
  { K_BUTTON, S_ICON, IC_OUTDENT, 0, NULL, ID_OUTDENT, 0, 0, F_ROW1, 0, "Decrease indent" },
  { K_BUTTON, S_ICON, IC_INDENT, 0, NULL, ID_INDENT, 0, 0, F_ROW1, 0, "Increase indent" },
  { K_DROP, S_ICON, IC_MERGECELLS, L_MERGES, NULL, ID_MERGEDD, RB_MERGED, 0, F_ROW1 | F_SPLIT, 0, "Merge & Centre" },
};
static const Item h_num[] = {
  { K_FIELD, S_SMALL, 0, L_NUMFMTS, NULL, ID_NUMFMTDD, 0, 0, 0, 104, "Number format" },
  { K_BUTTON, S_ICON, IC_CURRENCY, 0, NULL, ID_CURRENCY, 0, 0, F_ROW1, 0, "Currency format" },
  { K_BUTTON, S_ICON, IC_ACCOUNTING, 0, NULL, ID_LX_ACCOUNTING, 0, 0, F_ROW1, 0, "Accounting number format" },
  { K_BUTTON, S_ICON, IC_PERCENT, 0, NULL, ID_PERCENT, 0, 0, F_ROW1, 0, "Percent style" },
  { K_BUTTON, S_ICON, IC_COMMA, 0, NULL, ID_COMMA, 0, 0, F_ROW1, 0, "Comma style: thousands separators" },
  { K_BUTTON, S_ICON, IC_INCDEC, 0, NULL, ID_INCDEC, 0, 0, F_ROW1, 0, "Increase decimal" },
  { K_BUTTON, S_ICON, IC_DECDEC, 0, NULL, ID_DECDEC, 0, 0, F_ROW1, 0, "Decrease decimal" },
};
static const Item h_styles[] = {
  { K_DROP, S_LARGE, IC_CONDFMT, L_CONDFMTS, "Conditional Formatting", ID_CONDDD, 0, 0, 0, 0, "Conditional formatting" },
  { K_DROP, S_LARGE, IC_FMTTABLE, L_TSTYLES, "Format as Table", ID_FMTTABLEDD, 0, 0, 0, 0, "Make the cells a table in the style chosen" },
  { K_DROP, S_LARGE, IC_CELLSTYLES, L_CELLSTYLES, "Cell Styles", ID_CELLSTYLEDD, 0, 0, 0, 0, "Cell styles" },
  { K_DROP, S_SMALL, IC_NEWSTYLE, LXL_MYSTYLES, "My Styles", ID_LX_MYSTYLEDD, 0, 0, 0, 0, "Your own cell styles, and New Cell Style: the active cell's look under a name" },
};
static const Item h_cells[] = {
  { K_DROP, S_SMALL, IC_INSERTCELLS, L_INSERTS, "Insert", ID_INSERTDD, 0, 0, 0, 0, "Insert cells, rows, columns or a sheet" },
  { K_DROP, S_SMALL, IC_DELETECELLS, L_DELETES, "Delete", ID_DELETEDD, 0, 0, 0, 0, "Delete cells, rows, columns or a sheet" },
  { K_DROP, S_SMALL, IC_FORMATCELLS, L_FORMATS, "Format", ID_FORMATDD, 0, 0, 0, 0, "Row height, column width, hiding, the sheet" },
};
static const Item h_edit[] = {
  { K_DROP, S_SMALL, IC_AUTOSUM, L_AUTOSUMS, "AutoSum", ID_AUTOSUMDD, 0, 0, F_SPLIT, 0, "AutoSum (Alt-=)" },
  { K_DROP, S_SMALL, IC_FILLDOWN, L_FILL, "Fill", ID_FILLDD, 0, 0, 0, 0, "Fill" },
  { K_DROP, S_SMALL, IC_CLEAR, L_CLEARS, "Clear", ID_CLEARDD, 0, 0, 0, 0, "Clear" },
  { K_DROP, S_LARGE, IC_SORTAZ, L_SORTS, "Sort", ID_SORTDD, 0, 0, 0, 0, "Sort" },
  { K_DROP, S_LARGE, IC_FIND, L_FINDS, "Find & Select", ID_FINDDD, 0, 0, 0, 0, "Find & Select" },
};
static const Group home[] = { { "Clipboard", G_COLS, h_clip, N(h_clip) }, { "Font", G_ROWS, h_font, N(h_font) },
                              { "Alignment", G_ROWS, h_align, N(h_align) }, { "Number", G_ROWS, h_num, N(h_num) },
                              { "Styles", G_COLS, h_styles, N(h_styles) }, { "Cells", G_COLS, h_cells, N(h_cells) },
                              { "Editing", G_COLS, h_edit, N(h_edit) } };
/* Insert */
static const Item i_sheets[] = {
  { K_BUTTON, S_LARGE, IC_SHEET, 0, "New Sheet", ID_INSSHEET, 0, 0, 0, 0, "A new sheet after this one (Shift-F11)" },
  { K_BUTTON, S_SMALL, IC_ROWS, 0, "Rows", ID_INSROWS, 0, 0, 0, 0, "Insert rows above the selection" },
  { K_BUTTON, S_SMALL, IC_COLS, 0, "Columns", ID_INSCOLS, 0, 0, 0, 0, "Insert columns left of the selection" },
  { K_BUTTON, S_SMALL, IC_INSERTCELLS, 0, "Cells...", ID_INSCELLS, 0, 0, 0, 0, "Insert cells, moving others down or right" },
};
static const Item i_fn[] = {
  { K_BUTTON, S_LARGE, IC_FX, 0, "Function", ID_FUNCTION, 0, 0, 0, 0, "Insert a function (Shift-F3)" },
  { K_BUTTON, S_SMALL, IC_DATE, 0, "Date", ID_DATE, 0, 0, F_NEWCOL, 0, "Today's date (Amiga-;)" },
  { K_BUTTON, S_SMALL, IC_TIME, 0, "Time", ID_TIME, 0, 0, 0, 0, "The time now (Amiga-:)" },
  { K_DROP, S_SMALL, IC_SYMBOL, L_SYMBOLS, "Symbol", ID_SYMBOLDD, 0, 0, 0, 0, "A symbol" },
  { K_BUTTON, S_SMALL, IC_CHECKBOX, 0, "Checkbox", ID_AN_CHECKBOX, 0, 0, F_NEWCOL, 0, "Cell controls: checkboxes in the selected cells (TRUE ticked, FALSE not; click or Space to tick)" },
};
static const Item i_names[] = {
  { K_BUTTON, S_LARGE, IC_DEFNAME, 0, "Name", ID_DEFNAME, 0, 0, 0, 0, "Give the selection a name to use in formulas" },
};
static const Item i_links[] = {
  { K_BUTTON, S_LARGE, IC_LINK, 0, "Link", ID_LINK, 0, 0, 0, 0, "A link to a web page, a mail address or a place in the workbook" },
  { K_BUTTON, S_LARGE, IC_COMMENT, 0, "Note", ID_NOTENEW, 0, 0, 0, 0, "A note on the active cell" },
  { K_BUTTON, S_LARGE, IC_SHOWCOMMENTS, 0, "Comment", ID_LX_COMMENTNEW, 0, 0, 0, 0, "A comment on the active cell: a conversation others can reply to" },
};
static const Item i_text[] = {
  { K_BUTTON, S_LARGE, IC_HEADER, 0, "Header & Footer", ID_LX_HEADERFOOTER, 0, 0, 0, 0, "Headers and footers for the pages: Page Layout view and its Page Setup" },
};
#include "chartrib.h"            /* charts, pictures, shapes, ink, sparklines (charttab.c does their commands) */
static const Item i_textx[] = {
  { K_BUTTON, S_LARGE, IC_TEXTBOX, 0, "Text Box", ID_X_TEXTBOX, 0, 0, 0, 0, "A box of text over the cells: drag on the sheet to draw it" },
  { K_DROP, S_LARGE, ICX_WORDART, LX_WORDART, "WordArt", ID_X_WORDARTDD, 0, 0, 0, 0, "Large decorative text over the cells, in one of ten styles" },
  { K_BUTTON, S_LARGE, IC_HEADER, 0, "Header & Footer", ID_LX_HEADERFOOTER, 0, 0, 0, 0, "Headers and footers for the pages: Page Layout view and its Page Setup" },
};
static const Group insert[] = { { "Tables", G_COLS, i_tables, N(i_tables) }, { "Sheets & Cells", G_COLS, i_sheets, N(i_sheets) }, { "Illustrations", G_COLS, ix_illus, N(ix_illus) },
                                { "Charts", G_COLS, ix_charts, N(ix_charts) }, { "Sparklines", G_COLS, ix_spark, N(ix_spark) }, { "Filters", G_COLS, i_filters, N(i_filters) },
                                { "Functions", G_COLS, i_fn, N(i_fn) }, { "Links", G_COLS, i_links, N(i_links) }, { "Text", G_COLS, i_textx, N(i_textx) },
                                { "Names", G_COLS, i_names, N(i_names) } };
/* Page Layout: themes, the page setup (Ledger doesn't print, but PDF export, Page Break Preview and
   Excel use it), scaling, the sheet options, and Ledger's sheet tools */
static const Item p_themes[] = {
  { K_DROP, S_LARGE, IC_THEMES, LXL_THEMES, "Themes", ID_LX_THEMEDD, 0, 0, 0, 0, "Themes: a set of colours and fonts for the whole workbook" },
  { K_DROP, S_SMALL, IC_COLORSETS, LXL_THEMECOLORS, "Colours", ID_LX_COLORSDD, 0, 0, 0, 0, "Theme colours: the palette the colour lists offer" },
  { K_DROP, S_SMALL, IC_THEMEFONTS, LXL_THEMEFONTS, "Fonts", ID_LX_FONTSDD, 0, 0, 0, 0, "Theme fonts: the headings' and the cells' font" },
};
static const Item p_setup[] = {
  { K_DROP, S_LARGE, IC_MARGINS, LXL_MARGINS, "Margins", ID_LX_MARGINSDD, 0, 0, 0, 0, "Margins: the space round the cells on each page" },
  { K_DROP, S_LARGE, IC_ORIENT, LXL_ORIENT, "Orientation", ID_LX_ORIENTDD, 0, 0, 0, 0, "Pages upright (portrait) or on their side (landscape)" },
  { K_DROP, S_LARGE, IC_SIZE, LXL_PAPERS, "Size", ID_LX_SIZEDD, 0, 0, 0, 0, "Paper size" },
  { K_DROP, S_LARGE, IC_PRINTAREA, LXL_AREA, "Print Area", ID_LX_AREADD, 0, 0, 0, 0, "The cells that make the pages" },
  { K_DROP, S_LARGE, IC_BREAKS, LXL_BREAKS, "Breaks", ID_LX_BREAKSDD, 0, 0, 0, 0, "Page breaks above and left of the active cell" },
  { K_BUTTON, S_LARGE, IC_PAGECOLOR, 0, "Background", ID_X_BACKGROUND, 0, 0, 0, 0, "A picture behind the cells, repeated across the sheet (again: take it off)" },
  { K_BUTTON, S_LARGE, IC_PRINTTITLES, 0, "Print Titles", ID_LX_TITLES, 0, 0, 0, 0, "Rows and columns repeated on every page" },
  { K_BUTTON, S_LARGE, IC_PAGESETUPDLG, 0, "Page Setup", ID_LX_PAGESETUP, 0, 0, 0, 0, "Page Setup: page, margins, header and footer, sheet" },
};
static const Item p_fit[] = {
  { K_DROP, S_SMALL, IC_FITWIDTH, LXL_FITW, "Width", ID_LX_FITWDD, 0, 0, 0, 0, "Fit the pages to this many pages wide" },
  { K_DROP, S_SMALL, IC_FITHEIGHT, LXL_FITH, "Height", ID_LX_FITHDD, 0, 0, 0, 0, "Fit the pages to this many pages tall" },
  { K_DROP, S_SMALL, IC_ZOOM, LXL_SCALE, "Scale", ID_LX_SCALEDD, 0, 0, 0, 0, "Make the pages' contents bigger or smaller" },
};
static const Item p_opts[] = {
  { K_LABEL, S_SMALL, 0, 0, "Gridlines", 0, 0, 0, 0, 0, NULL },
  { K_BUTTON, S_SMALL, 0, 0, "View", ID_LX_VIEWGRID, LXB_VIEWGRID, 0, F_CHECK, 0, "Show the gridlines between cells" },
  { K_BUTTON, S_SMALL, 0, 0, "Print", ID_LX_PRINTGRID, LXB_PRINTGRID, 0, F_CHECK, 0, "Gridlines on the pages (PDF export)" },
  { K_LABEL, S_SMALL, 0, 0, "Headings", 0, 0, 0, F_NEWCOL, 0, NULL },
  { K_BUTTON, S_SMALL, 0, 0, "View", ID_LX_VIEWHEAD, LXB_VIEWHEAD, 0, F_CHECK, 0, "Show the row numbers and column letters" },
  { K_BUTTON, S_SMALL, 0, 0, "Print", ID_LX_PRINTHEAD, LXB_PRINTHEAD, 0, F_CHECK, 0, "Row numbers and column letters on the pages (PDF export)" },
  { K_BUTTON, S_SMALL, 0, 0, "Zero Values", ID_SHOWZEROS, RB_ZEROS, 0, F_CHECK | F_NEWCOL, 0, "Show zeros (else cells that are 0 look empty)" },
};
static const Item p_sheet[] = {
  { K_BUTTON, S_LARGE, IC_RENAME, 0, "Rename", ID_RENAMESHEET, 0, 0, 0, 0, "Rename this sheet" },
  { K_DROP, S_LARGE, IC_TABCOLOR, L_TABCOLORS, "Tab Colour", ID_TABCOLORDD, 0, 0, 0, 0, "The colour of this sheet's tab" },
  { K_BUTTON, S_SMALL, IC_HIDE, 0, "Hide", ID_HIDESHEET, 0, 0, 0, 0, "Hide this sheet" },
  { K_BUTTON, S_SMALL, IC_UNHIDE, 0, "Unhide", ID_UNHIDESHEET, 0, 0, 0, 0, "Show a hidden sheet" },
  { K_BUTTON, S_SMALL, IC_COPY, 0, "Move or Copy", ID_LX_MOVECOPY, 0, 0, 0, 0, "Move this sheet, or copy it, within the workbook or to another one" },
};
static const Item p_arrange[] = {
  { K_BUTTON, S_SMALL, IC_ARROWLEFT, 0, "Move Left", ID_MOVESHEETL, 0, 0, 0, 0, "Move this sheet one place left" },
  { K_BUTTON, S_SMALL, IC_ARROWRIGHT, 0, "Move Right", ID_MOVESHEETR, 0, 0, 0, 0, "Move this sheet one place right" },
  { K_BUTTON, S_SMALL, IC_DELETECELLS, 0, "Delete", ID_DELSHEET, 0, 0, 0, 0, "Delete this sheet" },
  { K_BUTTON, S_SMALL, IC_FORWARD, 0, "Bring Forward", ID_X_FORWARD, 0, RBX(RBX_OBJECT), F_NEWCOL, 0, "Bring the selected picture, shape or chart one step to the front" },
  { K_BUTTON, S_SMALL, IC_BACKWARD, 0, "Send Backward", ID_X_BACKWARD, 0, RBX(RBX_OBJECT), 0, 0, "Send it one step back" },
  { K_BUTTON, S_SMALL, IC_SELPANE, 0, "Selection Pane", ID_X_SELPANE, 0, 0, 0, 0, "The sheet's pictures, shapes and charts by name: select, rename, reorder" },
  { K_DROP, S_SMALL, IC_ALIGNOBJ, LX_ALIGN, "Align", ID_X_ALIGNDD, 0, RBX(RBX_OBJECT), F_NEWCOL, 0, "Line the selected objects up with the first one selected" },
  { K_DROP, S_SMALL, IC_GROUP, L_UIGROUPS, "Group", ID_UI_GROUPDD, 0, RBX(RBX_OBJECT), 0, 0, "Group the selected objects so they move and size as one; Ungroup takes a group apart, Regroup puts it back" },
  { K_BUTTON, S_SMALL, IC_ROTATE, 0, "Rotate", ID_X_ROTATE, 0, RBX(RBX_SHAPE), 0, 0, "Turn the selected shape a quarter turn" },
};
static const Group playout[] = { { "Themes", G_COLS, p_themes, N(p_themes) }, { "Page Setup", G_COLS, p_setup, N(p_setup) },
                                 { "Scale to Fit", G_COLS, p_fit, N(p_fit) }, { "Sheet Options", G_COLS, p_opts, N(p_opts) },
                                 { "Sheet", G_COLS, p_sheet, N(p_sheet) }, { "Arrange", G_COLS, p_arrange, N(p_arrange) } };
/* Formulas */
static const Item f_lib[] = {
  { K_BUTTON, S_LARGE, IC_FX, 0, "Insert Function", ID_FUNCTION, 0, 0, 0, 0, "Insert a function (Shift-F3)" },
  { K_DROP, S_LARGE, IC_AUTOSUM, L_AUTOSUMS, "AutoSum", ID_AUTOSUMDD, 0, 0, F_SPLIT, 0, "AutoSum (Alt-=)" },
  { K_DROP, S_SMALL, IC_FINANCE, L_FNFIN, "Financial", ID_FN_FIN, 0, 0, 0, 0, "Financial functions" },
  { K_DROP, S_SMALL, IC_LOGIC, L_FNLOGIC, "Logical", ID_FN_LOGIC, 0, 0, 0, 0, "Logical functions" },
  { K_DROP, S_SMALL, IC_TEXTFN, L_FNTEXT, "Text", ID_FN_TEXT, 0, 0, 0, 0, "Text functions" },
  { K_DROP, S_SMALL, IC_DATE, L_FNDATE, "Date & Time", ID_FN_DATE, 0, 0, 0, 0, "Date and time functions" },
  { K_DROP, S_SMALL, IC_LOOKUP, L_FNLOOKUP, "Lookup & Reference", ID_FN_LOOKUP, 0, 0, 0, 0, "Lookup and reference functions" },
  { K_DROP, S_SMALL, IC_MATHFN, L_FNMATH, "Math & Trig", ID_FN_MATH, 0, 0, 0, 0, "Maths and trigonometry functions" },
  { K_DROP, S_SMALL, IC_MOREFN, L_FNMORE, "More Functions", ID_FN_MORE, 0, 0, F_NEWCOL, 0, "Statistical, engineering, cube, information, compatibility and web functions" },
  { K_DROP, S_SMALL, IC_RECENTFN, LXL_RECENT, "Recently Used", ID_LX_RECENTDD, 0, 0, 0, 0, "The functions you used last" },
};
static const Item f_names[] = {
  { K_BUTTON, S_LARGE, IC_NAMES, 0, "Name Manager", ID_NAMES, 0, 0, 0, 0, "The workbook's names: add, change, delete" },
  { K_BUTTON, S_SMALL, IC_DEFNAME, 0, "Define Name", ID_DEFNAME, 0, 0, 0, 0, "Give the selection a name to use in formulas" },
  { K_DROP, S_SMALL, IC_USEINFORMULA, LXL_USENAMES, "Use in Formula", ID_LX_USENAMEDD, 0, 0, 0, 0, "Type one of the workbook's names into the formula" },
  { K_BUTTON, S_SMALL, IC_NAMESFROMSEL, 0, "Create from Selection", ID_LX_CREATENAMES, 0, 0, 0, 0, "Name rows or columns after the labels at their edge" },
};
static const Item f_audit[] = {
  { K_BUTTON, S_SMALL, IC_TRACEPREC, 0, "Trace Precedents", ID_LX_TRACEPREC, 0, 0, 0, 0, "Arrows from the cells the active cell's formula uses" },
  { K_BUTTON, S_SMALL, IC_TRACEDEP, 0, "Trace Dependents", ID_LX_TRACEDEP, 0, 0, 0, 0, "Arrows to the cells whose formulas use the active cell" },
  { K_DROP, S_SMALL, IC_REMOVEARROWS, LXL_REMOVE, "Remove Arrows", ID_LX_REMOVEDD, 0, LXB_ARROWS, F_SPLIT, 0, "Take the tracer arrows away" },
  { K_BUTTON, S_SMALL, IC_SHOWFORMULAS, 0, "Show Formulas", ID_SHOWFORMULAS, RB_FORMULAS, 0, F_NEWCOL, 0, "Show formulas in the cells instead of their results (Ctrl-`)" },
  { K_BUTTON, S_SMALL, IC_ERRCHECK, 0, "Error Checking", ID_LX_ERRCHECK, 0, 0, 0, 0, "Go through the cells showing errors" },
  { K_BUTTON, S_SMALL, IC_EVALUATE, 0, "Evaluate Formula", ID_LX_EVALUATE, 0, LXB_HASFORMULA, 0, 0, "Work out the active cell's formula one step at a time" },
  { K_BUTTON, S_LARGE, IC_WATCH, 0, "Watch Window", ID_LX_WATCH, 0, 0, 0, 0, "Keep an eye on chosen cells' values in a window of their own" },
};
static const Item f_calc[] = {
  { K_DROP, S_LARGE, IC_CALCOPTS, LXL_CALCOPTS, "Calculation Options", ID_LX_CALCOPTDD, 0, 0, 0, 0, "Calculation: automatic, or manual (F9)" },
  { K_BUTTON, S_SMALL, IC_CALCNOW, 0, "Calculate Now", ID_CALCNOW, 0, 0, 0, 0, "Work out every formula again (F9)" },
  { K_BUTTON, S_SMALL, IC_CALCSHEET, 0, "Calculate Sheet", ID_LX_CALCSHEET, 0, 0, 0, 0, "Work out this sheet's formulas again (Shift-F9)" },
};
static const Group formulas[] = { { "Function Library", G_COLS, f_lib, N(f_lib) }, { "Defined Names", G_COLS, f_names, N(f_names) },
                                  { "Formula Auditing", G_COLS, f_audit, N(f_audit) }, { "Calculation", G_COLS, f_calc, N(f_calc) } };
/* Data */
static const Item d_get[] = {
  { K_BUTTON, S_LARGE, IC_IMPORT, 0, "Get Data", ID_GETDATA, 0, 0, 0, 0, "Bring in a CSV or text file: pick its separator and see it first" },
};
static const Item d_sort[] = {
  { K_BUTTON, S_SMALL, IC_SORTAZ, 0, "Sort A to Z", ID_SORTAZ, 0, 0, 0, 0, "Sort the rows by the active cell's column, smallest first" },
  { K_BUTTON, S_SMALL, IC_SORTZA, 0, "Sort Z to A", ID_SORTZA, 0, 0, 0, 0, "Sort the rows by the active cell's column, largest first" },
  { K_BUTTON, S_LARGE, IC_CUSTOMSORT, 0, "Sort", ID_CUSTOMSORT, 0, 0, 0, 0, "Sort by up to three columns" },
  { K_BUTTON, S_LARGE, IC_FILTER, 0, "Filter", ID_FILTER, RB_FILTER, 0, 0, 0, "Filter buttons on the heading row of the table around the active cell" },
  { K_BUTTON, S_SMALL, IC_CLEARFILTER, 0, "Clear", ID_CLEARFILTER, 0, 0, 0, 0, "Show every row again" },
  { K_BUTTON, S_SMALL, IC_REAPPLY, 0, "Reapply", ID_REAPPLY, 0, 0, 0, 0, "Filter again, after the values changed" },
  { K_BUTTON, S_SMALL, IC_ADVFILTER, 0, "Advanced", ID_ADVFILTER, 0, 0, 0, 0, "Filter by a range of conditions, or copy the matching rows elsewhere" },
};
static const Item d_tools[] = {
  { K_BUTTON, S_LARGE, IC_TEXTCOLS, 0, "Text to Columns", ID_TEXTCOLS, 0, 0, 0, 0, "Split each cell's text into columns at commas, spaces or set widths" },
  { K_BUTTON, S_SMALL, IC_FLASHFILL, 0, "Flash Fill", ID_FLASHFILL, 0, 0, 0, 0, "Fill the column from the examples typed in it (Ctrl-E)" },
  { K_BUTTON, S_SMALL, IC_REMOVEDUP, 0, "Remove Duplicates", ID_REMOVEDUP, 0, 0, 0, 0, "Delete repeated rows from the selection" },
  { K_DROP, S_SMALL, IC_VALIDATE, L_DVS, "Data Validation", ID_DVDD, 0, 0, F_SPLIT, 0, "Rules for what cells may hold, drop-down lists, invalid data circled" },
  { K_BUTTON, S_SMALL, IC_CONSOLIDATE, 0, "Consolidate", ID_CONSOLIDATE, 0, 0, F_NEWCOL, 0, "Combine ranges from several places into one summary" },
};
static const Item d_forecast[] = {
  { K_DROP, S_LARGE, IC_WHATIF, L_WHATIFS, "What-If Analysis", ID_WHATIFDD, 0, 0, 0, 0, "Scenarios, Goal Seek and Data Tables" },
  { K_BUTTON, S_LARGE, IC_FORECAST, 0, "Forecast Sheet", ID_AN_FORECAST, 0, 0, 0, 0, "A new sheet forecasting the selected timeline's values, with a chart" },
};
static const Item d_analysis[] = {
  { K_BUTTON, S_SMALL, IC_SOLVER, 0, "Solver", ID_AN_SOLVER, 0, 0, 0, 0, "Find the variable cells' values that make a formula largest, smallest or a value" },
  { K_BUTTON, S_SMALL, IC_TOOLPAK, 0, "Data Analysis", ID_AN_TOOLPAK, 0, 0, 0, 0, "The Analysis ToolPak: regression, Anova, t-tests, histograms and more" },
};
static const Item d_outline[] = {
  { K_DROP, S_SMALL, IC_GROUPROWS, L_GROUPS, "Group", ID_GROUPDD, 0, 0, F_SPLIT, 0, "Group the selected rows or columns (Alt-Shift-Right)" },
  { K_BUTTON, S_SMALL, IC_UNGROUPROWS, 0, "Ungroup", ID_UNGROUP, 0, 0, 0, 0, "Ungroup the selected rows or columns (Alt-Shift-Left)" },
  { K_BUTTON, S_SMALL, IC_SUBTOTAL, 0, "Subtotal", ID_SUBTOTAL, 0, 0, 0, 0, "Totals at each change in a column, with an outline" },
  { K_BUTTON, S_SMALL, IC_SHOWDETAIL, 0, "Show Detail", ID_SHOWDETAIL, 0, 0, F_NEWCOL, 0, "Show the rows or columns of the group" },
  { K_BUTTON, S_SMALL, IC_HIDEDETAIL, 0, "Hide Detail", ID_HIDEDETAIL, 0, 0, 0, 0, "Hide the rows or columns of the group" },
  { K_BUTTON, S_SMALL, IC_OUTLINESET, 0, "Settings", ID_OUTLINESET, 0, 0, 0, 0, "Where summary rows and columns sit" },
};
static const Group data[] = { { "Get Data", G_COLS, d_get, N(d_get) }, { "Sort & Filter", G_COLS, d_sort, N(d_sort) },
                              { "Data Tools", G_COLS, d_tools, N(d_tools) }, { "Forecast", G_COLS, d_forecast, N(d_forecast) },
                              { "Outline", G_COLS, d_outline, N(d_outline) }, { "Analysis", G_COLS, d_analysis, N(d_analysis) } };
/* Review */
static const Item r_proof[] = {
  { K_BUTTON, S_LARGE, IC_SPELLING, 0, "Spelling", ID_LX_SPELLING, 0, 0, 0, 0, "Check the spelling of the text in the cells (F7)" },
  { K_BUTTON, S_SMALL, IC_THESAURUS, 0, "Thesaurus", ID_LX_THESAURUS, 0, 0, 0, 0, "Words related to the one in the active cell" },
  { K_BUTTON, S_SMALL, IC_STATS, 0, "Workbook Statistics", ID_STATS, 0, 0, 0, 0, "Counts of sheets, cells, formulas and names" },
};
static const Item r_access[] = {
  { K_BUTTON, S_LARGE, IC_ACCESS, 0, "Check Accessibility", ID_LX_ACCESS, 0, 0, 0, 0, "Things that make the workbook hard to read for people with disabilities" },
};
static const Item r_find[] = {
  { K_BUTTON, S_LARGE, IC_FIND, 0, "Find", ID_FIND, 0, 0, 0, 0, "Find (Amiga-F)" },
  { K_BUTTON, S_SMALL, IC_REPLACE, 0, "Replace", ID_REPLACE, 0, 0, 0, 0, "Replace (Amiga-H)" },
  { K_BUTTON, S_SMALL, IC_GOTO, 0, "Go To", ID_GOTO, 0, 0, 0, 0, "Go To a cell or name (Amiga-G)" },
};
static const Item r_notes[] = {
  { K_BUTTON, S_LARGE, IC_COMMENT, 0, "New Note", ID_NOTENEW, 0, 0, 0, 0, "Write a note on the active cell, or change its note" },
  { K_BUTTON, S_SMALL, IC_DELETE, 0, "Delete", ID_NOTEDEL, 0, RB_HASNOTE, 0, 0, "Take the active cell's note off" },
  { K_BUTTON, S_SMALL, IC_PREV, 0, "Previous", ID_NOTEPREV, 0, 0, 0, 0, "Go to the note before this one" },
  { K_BUTTON, S_SMALL, IC_NEXT, 0, "Next", ID_NOTENEXT, 0, 0, 0, 0, "Go to the next note" },
  { K_BUTTON, S_SMALL, IC_SHOWNOTE, 0, "Show/Hide Note", ID_LX_NOTESHOWHIDE, LXB_NOTESHOWN, RB_HASNOTE, F_NEWCOL, 0, "Keep the active cell's note shown, or hide it again" },
  { K_BUTTON, S_SMALL, IC_SHOWCOMMENTS, 0, "Show All Notes", ID_NOTESHOW, RB_NOTES, 0, 0, 0, "Show every note beside its cell" },
  { K_BUTTON, S_SMALL, IC_COMMENT, 0, "Convert to Comments", ID_LX_CONVERTNOTES, 0, 0, 0, 0, "Turn the workbook's notes into comments that can have replies" },
};
static const Item r_protect[] = {
  { K_BUTTON, S_LARGE, IC_PROTECT, 0, "Protect Sheet", ID_PROTECTSHEET, RB_PROTECTED, 0, 0, 0, "Stop changes to the sheet's locked cells (press again to unprotect)" },
  { K_BUTTON, S_LARGE, IC_PROTECTBOOK, 0, "Protect Workbook", ID_PROTECTBOOK, RB_BOOKLOCK, 0, 0, 0, "Stop sheets being added, moved, renamed, hidden or deleted" },
  { K_BUTTON, S_SMALL, IC_EDITRANGES, 0, "Allow Edit Ranges", ID_AN_EDITRANGES, 0, 0, 0, 0, "Ranges that stay editable on a protected sheet, with or without a password" },
};
static const Item r_comments[] = {
  { K_BUTTON, S_LARGE, IC_COMMENT, 0, "New Comment", ID_LX_COMMENTNEW, 0, 0, 0, 0, "Start a conversation on the active cell, or reply to its comment" },
  { K_BUTTON, S_SMALL, IC_DELETE, 0, "Delete", ID_LX_COMMENTDEL, 0, LXB_COMMENTS, 0, 0, "Delete the active cell's comment and its replies" },
  { K_BUTTON, S_SMALL, IC_PREV, 0, "Previous", ID_LX_COMMENTPREV, 0, 0, 0, 0, "Go to the comment before this one" },
  { K_BUTTON, S_SMALL, IC_NEXT, 0, "Next", ID_LX_COMMENTNEXT, 0, 0, 0, 0, "Go to the next comment" },
  { K_BUTTON, S_LARGE, IC_SHOWCOMMENTS, 0, "Show Comments", ID_LX_COMMENTSHOW, 0, 0, 0, 0, "Every comment of the workbook with its replies" },
};
static const Group review[] = { { "Proofing", G_COLS, r_proof, N(r_proof) }, { "Accessibility", G_COLS, r_access, N(r_access) },
                                { "Comments", G_COLS, r_comments, N(r_comments) }, { "Notes", G_COLS, r_notes, N(r_notes) },
                                { "Protect", G_COLS, r_protect, N(r_protect) }, { "Find", G_COLS, r_find, N(r_find) } };
/* View */
static const Item v_views[] = {
  { K_BUTTON, S_LARGE, IC_NORMALVIEW, 0, "Normal", ID_LX_NORMAL, LXB_NORMAL, 0, 0, 0, "The sheet as cells, without pages" },
  { K_BUTTON, S_LARGE, IC_PBPVIEW, 0, "Page Break Preview", ID_LX_PBP, LXB_PBP, 0, 0, 0, "Where the pages break: drag the blue lines to move the breaks" },
  { K_BUTTON, S_LARGE, IC_PRINTLAYOUT, 0, "Page Layout", ID_LX_PAGELAYOUT, LXB_PAGELAYOUT, 0, 0, 0, "The sheet as pages, with their margins, headers and footers" },
  { K_BUTTON, S_LARGE, IC_CUSTOMVIEWS, 0, "Custom Views", ID_LX_CUSTOMVIEWS, 0, 0, 0, 0, "Keep the way the workbook is shown under a name, to come back to" },
};
static const Item v_show[] = {
  { K_BUTTON, S_SMALL, 0, 0, "Ruler", ID_LX_RULER, LXB_RULER, 0, F_CHECK, 0, "Show the rulers in Page Layout view" },
  { K_BUTTON, S_SMALL, 0, 0, "Formula Bar", ID_FORMULABAR, RB_FORMULABAR, 0, F_CHECK, 0, "Show the formula bar" },
  { K_BUTTON, S_SMALL, 0, 0, "Gridlines", ID_GRIDLINES, RB_GRIDLINES, 0, F_CHECK, 0, "Show the gridlines between cells" },
  { K_BUTTON, S_SMALL, 0, 0, "Headings", ID_HEADINGS, RB_HEADINGS, 0, F_CHECK | F_NEWCOL, 0, "Show the row numbers and column letters" },
};
static const Item v_zoom[] = {
  { K_DROP, S_LARGE, IC_ZOOM, L_ZOOMS, "Zoom", ID_ZOOMDD, 0, 0, F_SPLIT, 0, "Zoom" },
  { K_BUTTON, S_LARGE, IC_ZOOM100, 0, "100%", ID_ZOOM100, 0, 0, 0, 0, "Back to 100%" },
  { K_BUTTON, S_LARGE, IC_ZOOMSEL, 0, "Zoom to Selection", ID_ZOOMSEL, 0, 0, 0, 0, "Zoom so the selection fills the window" },
};
static const Item v_window[] = {
  { K_BUTTON, S_SMALL, IC_NEWWINDOW, 0, "New Window", ID_LX_NEWWIN, 0, 0, 0, 0, "Another window on this workbook" },
  { K_BUTTON, S_SMALL, IC_ARRANGE, 0, "Arrange All", ID_LX_ARRANGE, 0, 0, 0, 0, "Put the workbook windows side by side, one above another, or tiled" },
  { K_DROP, S_SMALL, IC_FREEZE, L_FREEZES, "Freeze Panes", ID_FREEZEDD, RB_FROZEN, 0, 0, 0, "Keep rows and columns in sight as the rest scrolls" },
  { K_BUTTON, S_SMALL, IC_SPLIT, 0, "Split", ID_LX_SPLIT, LXB_SPLIT, 0, F_NEWCOL, 0, "Split the window at the active cell into panes" },
  { K_BUTTON, S_SMALL, IC_HIDE, 0, "Hide", ID_LX_HIDEWIN, 0, 0, 0, 0, "Hide this workbook window" },
  { K_BUTTON, S_SMALL, IC_UNHIDE, 0, "Unhide", ID_LX_UNHIDEWIN, 0, 0, 0, 0, "Show a hidden workbook window" },
  { K_BUTTON, S_SMALL, IC_SIDEBYSIDE, 0, "View Side by Side", ID_LX_SIDEBYSIDE, LXB_SIDEBYSIDE, LXB_MANYWIN, F_NEWCOL, 0, "Two workbook windows next to each other, to compare them" },
  { K_BUTTON, S_SMALL, IC_SYNCSCROLL, 0, "Synchronous Scrolling", ID_LX_SYNCSCROLL, LXB_SYNC, LXB_SIDEBYSIDE, 0, 0, "Scroll both windows together" },
  { K_BUTTON, S_SMALL, IC_RESETWIN, 0, "Reset Window Position", ID_LX_RESETWIN, 0, LXB_SIDEBYSIDE, 0, 0, "Put the windows side by side again" },
  { K_DROP, S_LARGE, IC_SWITCHWIN, LXL_SWITCH, "Switch Windows", ID_LX_SWITCHDD, 0, 0, 0, 0, "Go to another workbook window (Amiga-`)" },
};
static const Item v_theme[] = {
  { K_BUTTON, S_LARGE, IC_DARK, 0, "Dark Mode", ID_DARKUI, RB_DARK, 0, 0, 0, "Ledger in dark colours, easier on the eyes at night" },
};
static const Group view[] = { { "Workbook Views", G_COLS, v_views, N(v_views) }, { "Show", G_COLS, v_show, N(v_show) },
                              { "Zoom", G_COLS, v_zoom, N(v_zoom) }, { "Window", G_COLS, v_window, N(v_window) }, { "Theme", G_COLS, v_theme, N(v_theme) } };

/* the longer explanations in the help bubbles; tools not listed show their Item help */
static const struct { UWORD id; const char *tip; } tips[] = {
  { ID_SAVE, "Saves the workbook under its name (the first time, asks for one)." },
  { ID_UNDO, "Takes back your last change. Press it again to go further back, one step at a time." },
  { ID_REDO, "Puts back a change you undid." },
  { ID_PASTE, "Puts what you cut or copied at the selected cell. The arrow offers other ways: values only, formulas, formatting, or turned on its side." },
  { ID_CUT, "Removes the selected cells' contents and keeps them on the clipboard, ready to paste somewhere else." },
  { ID_COPY, "Copies the selected cells to the clipboard. Formulas pasted elsewhere point at the same cells relative to themselves." },
  { ID_BRUSH, "Picks up the selected cell's look: font, colours, borders, number format. Then select other cells to give them the same look." },
  { ID_FONTDD, "The typeface of the selected cells. Each name in the list is shown in its own design." },
  { ID_SIZEDD, "The size of the selected cells' text, in points." },
  { ID_BORDERDD, "Lines round or between the selected cells." },
  { ID_FILLCOLOR, "A colour behind the selected cells." },
  { ID_FONTCOLOR, "The colour of the selected cells' text. Automatic is black." },
  { ID_WRAP, "Shows long text on several lines inside its cell instead of running over the next ones." },
  { ID_MERGEDD, "Joins the selected cells into one and centres its text, for a title over several columns. The arrow can merge each row on its own, or split merged cells again." },
  { ID_NUMFMTDD, "How numbers in the selected cells are shown: as currency, dates, percentages and so on. The value itself doesn't change." },
  { ID_CURRENCY, "Shows the numbers as money, with two decimal places and thousands separators." },
  { ID_PERCENT, "Shows the numbers as percentages: 0.25 shows as 25%." },
  { ID_COMMA, "Thousands separators and two decimal places." },
  { ID_INCDEC, "Shows one more decimal place." },
  { ID_DECDEC, "Shows one fewer decimal place (the value is rounded only for showing)." },
  { ID_CELLSTYLEDD, "Gives the selected cells a ready-made look: headings, totals, good and bad, inputs and outputs." },
  { ID_FORMATCELLS, "Opens Format Cells: number formats with every option, alignment, font, borders and fill at once." },
  { ID_AUTOSUM, "Adds up the numbers above (or to the left of) the selected cell with a SUM formula. The arrow has average, count, maximum and minimum." },
  { ID_FILLDD, "Copies the top cell down the selection (or the left one across). Series continues 1, 2, 3 or dates by a step you choose. Dragging the small square at the selection's corner does the same." },
  { ID_CLEARDD, "Takes out the selected cells' contents, their formatting, or both." },
  { ID_SORTDD, "Puts rows in order by the active cell's column, or by several columns." },
  { ID_FINDDD, "Finds text or numbers in the sheet, replaces them, or goes to a cell by its address or name." },
  { ID_FUNCTION, "Lists the functions by category with what each does; the one chosen is typed into the cell, ready for its arguments." },
  { ID_SHOWFORMULAS, "Shows each cell's formula instead of its result, for checking how a sheet works." },
  { ID_CALCDD, "Automatic works out formulas after every change. Manual waits until you press F9 or Calculate Now, which helps with very big workbooks." },
  { ID_FREEZEDD, "Keeps the rows above and the columns left of the active cell in sight while the rest of the sheet scrolls." },
  { ID_REMOVEDUP, "Deletes rows of the selection that repeat an earlier row exactly, and moves the rest up." },
  { ID_CONDDD, "Changes the look of cells by what they hold: bigger than a number, containing some text, repeated, among the largest, above average. Data bars and colour scales show the values at a glance." },
  { ID_LINK, "Makes the cell a link: to a web page, a mail address, or a cell on one of the workbook's sheets. Hold Ctrl and click the cell to follow it." },
  { ID_NOTENEW, "A note is a few words on a cell for whoever reads the sheet. Cells with a note have a red corner; rest the mouse on one to read it." },
  { ID_FILTER, "Puts a button on each heading of the table round the active cell. A button lists the column's values: untick some to hide their rows, or sort the table by that column." },
  { ID_CLEARFILTER, "Shows every row the filter hid. The filter buttons stay." },
  { ID_REAPPLY, "Tests each row again after values have changed, hiding the ones that no longer match." },
  { ID_IMPORTCSV, "Reads a CSV or tab-separated text file into a new sheet of this workbook." },
  { ID_GETDATA, "Reads a CSV or text file into a new sheet or at the active cell. You choose the separator and see how the columns will come out first." },
  { ID_TEXTCOLS, "Splits the text of each selected cell into several columns: at commas, tabs, semicolons, spaces or a character of your own, or at set widths." },
  { ID_FLASHFILL, "Type an example or two of what you want beside your data (say the first names out of full names), then Flash Fill finishes the column the same way." },
  { ID_DVDD, "Rules for what the selected cells may hold: whole numbers or decimals within limits, dates, times, text of a length, or a choice from a list shown as a drop-down on the cell. Circle Invalid Data rings the cells that break their rules." },
  { ID_CONSOLIDATE, "Adds up (or averages, counts...) ranges from several sheets into one table, matching them by their position or by the labels in their top row and left column." },
  { ID_AN_FORECAST, "Makes a new sheet from a timeline and its values: the history, a forecast by exponential smoothing (FORECAST.ETS) with its confidence bounds, and a chart." },
  { ID_AN_SOLVER, "Solver changes the variable cells to make the objective cell as large or small as it can be, or a set value, keeping to the constraints. Simplex LP for linear models, GRG Nonlinear for smooth ones, Evolutionary for others." },
  { ID_AN_TOOLPAK, "The Analysis ToolPak's tools: Anova, correlation, covariance, descriptive statistics, smoothing, F, t and z tests, Fourier analysis, histograms, random numbers, rank and percentile, regression and sampling." },
  { ID_AN_EDITRANGES, "Ranges of cells that can still be changed when the sheet is protected, each with a password or none." },
  { ID_AN_CHECKBOX, "Makes the selected cells checkboxes: a ticked box is TRUE, an empty one FALSE, so formulas can use them. Click a box or press Space to tick it." },
  { ID_WHATIFDD, "Goal Seek finds the input that gives a formula the result you want. Data Table works a formula out for a range of inputs. Scenarios keep sets of input values to switch between." },
  { ID_GROUPDD, "Groups the selected rows or columns: a bracket with a minus button appears beside the headings, and the level buttons show or hide whole levels at a time." },
  { ID_SUBTOTAL, "Inserts a total row at each change in a column of a sorted list (SUBTOTAL formulas), a grand total, and an outline to show just the totals." },
  { ID_ADVFILTER, "Filters a list by conditions written in cells: a heading row naming columns, conditions under it (on one row they must all hold; any row will do). Can copy the matching rows elsewhere, each one once." },
  { ID_PROTECTSHEET, "Protects the sheet: its locked cells (every cell, unless Format Cells' Protection unlocked it) can't be changed until it is unprotected, with the password if one was given." },
  { ID_PROTECTBOOK, "Protects the workbook's structure: sheets can't be added, deleted, renamed, moved or hidden until it is unprotected." },
  { ID_DARKUI, "Shows the ribbon, the sheet and everything else in dark colours. The workbook itself doesn't change." },
};
static ULONG split_id(UWORD id);
static const char *tip_for(UWORD id)
{
  int i;
  for (i = 0; i < (int)N(tips); i++) if (tips[i].id == id) return tips[i].tip;
  if (split_id(id) != id) return tip_for((UWORD)split_id(id));
  return NULL;
}
/* the tab strip, left to right. File isn't a tab of tools: a click on it opens the File page */
static const Tab tabs[] = {
  { "File", NULL, 0 }, { "Home", home, N(home) }, { "Insert", insert, N(insert) }, { "Draw", x_draw, N(x_draw) }, { "Page Layout", playout, N(playout) },
  { "Formulas", formulas, N(formulas) }, { "Data", data, N(data) }, { "Review", review, N(review) }, { "View", view, N(view) },
  /* contextual (TT_CONTEXT_FIRST on): shown while the active cell is in a table or a pivot table */
  { "Table Design", tdesign, N(tdesign) }, { "PivotTable Analyze", panalyze, N(panalyze) }, { "PivotTable Design", pdesign, N(pdesign) },
  { "Chart Design", x_design, N(x_design) }, { "Format", x_format, N(x_format) }, { "Sparkline", x_spark, N(x_spark) },
};
#define NTABS N(tabs)
/* (ledger.h) the label of entry v of the list of the item that returns id */
/* More Functions' categories: tools of no group, whose lists open from its list */
static const Item fn_sub[] = {
  { K_DROP, S_SMALL, 0, L_FNSTAT, "Statistical", ID_FN_STAT, 0, 0, 0, 0, NULL }, { K_DROP, S_SMALL, 0, L_FNENG, "Engineering", ID_FN_ENG, 0, 0, 0, 0, NULL },
  { K_DROP, S_SMALL, 0, L_FNCUBE, "Cube", ID_FN_CUBE, 0, 0, 0, 0, NULL }, { K_DROP, S_SMALL, 0, L_FNINFO, "Information", ID_FN_INFO, 0, 0, 0, 0, NULL },
  { K_DROP, S_SMALL, 0, L_FNCOMPAT, "Compatibility", ID_FN_COMPAT, 0, 0, 0, 0, NULL }, { K_DROP, S_SMALL, 0, L_FNWEB, "Web", ID_FN_WEB, 0, 0, 0, 0, NULL } };
const char *ribbon_entry(ULONG id, LONG v)
{
  int t, g, i;
  fn_lists_init();
  for (i = 0; i < N(fn_sub); i++) if (fn_sub[i].id == id) { const List *l = list_of(fn_sub[i].list); return v >= 0 && v < l->n ? l->labels[v] : NULL; }
  for (t = 0; t < NTABS; t++)
    for (g = 0; g < tabs[t].n; g++)
      for (i = 0; i < tabs[t].groups[g].n; i++) {
        const Item *it = &tabs[t].groups[g].items[i];
        if (it->id == id && it->list) {
          const List *l = list_of(it->list);
          if (l->kind == PL_FONTS) return ribbon_font_name((int)v);
          return v >= 0 && v < l->n ? l->labels[v] : NULL;
        }
      }
  return NULL;
}

/* ------------------------------------------------------------------ */
/* the instance */
#define MAXI 56
typedef struct { WORD x0, y0, x1, y1; } RBox;
struct RData {
  int shown;
  int tab;
  struct MUIP_Ribbon_State st;
  char font[48], numfmt[40], ctxname[64];
  int shortctx;                  /* the contextual tabs under short names (no room for the long ones) */
  RBox tabbox[NTABS];
  RBox qa[NQA];
  RBox item[MAXI]; int nitem;
  RBox group[12]; int ngroup;
  int hot, pressed;              /* item under the pointer / pressed (-1 none); tabs are 100 + n, quick access 200 + n */
  LONG value;
  struct MUI_EventHandlerNode ehn;
  Object *popwin, *poplist;
  int popitem;
  /* the shown tab's placed items: each one's tool (NULL: a collapsed group's button) and group */
  const Item *kitem[MAXI]; int kgroup[MAXI];
  int collapsed[12];             /* groups shown as one button for lack of room */
  int popgroup;                  /* the collapsed group whose list is open (-1 none) */
  List glist; const char *glabels[33]; char gbuf[32][48]; int gidx[32];
  const Item *popdirect;         /* the tool a list is open for when it isn't a placed item */
  const Item *defer;             /* a tool's list to open once the group's list has closed */
  ULONG lxhash;                  /* layout.h's check states when last drawn */
};
#define MUIM_Ribbon_Open (QTAG + 0x311)   /* (internal) the deferred list opened */
static struct MUI_CustomClass *rcls, *pcls;

static int fh(void) { return theme_font()->tf_YSize; }
static int tabh(void) { return fh() + 10; }
static int rowh(void) { return fh() + 7; }
static int bodyh(void) { return 3 * rowh() + 4; }
static int caph(void) { return fh() + 3; }

static int bit_on(struct RData *d, int b)
{
  if (b >= CB_INTABLE) return (d->st.ctx & CBIT(b)) != 0;
  if (b >= RBX(0)) return (d->st.ext & (1UL << (b - RBX(0)))) != 0;
  return b >= LXB_FIRST ? lx_bit(b) : (d->st.on & RBIT(b)) != 0;
}
/* a contextual tab is shown while the active cell is in a table (Table Design) or a pivot table,
   or while a chart, an object or a sparkline is selected (charttab.c) */
static int tab_shown(struct RData *d, int t)
{
  if (x_tab_hidden(tabs[t].groups, d->st.ext)) return 0;
  if (t < TT_CONTEXT_FIRST || t >= TT_CONTEXT_FIRST + 3) return 1;
  return bit_on(d, t == TT_CONTEXT_FIRST ? CB_INTABLE : CB_INPIVOT);
}
static const char *tab_name(struct RData *d, int t)
{
  static const char *const shortnames[3] = { "Design", "Analyze", "Design" };
  return t >= TT_CONTEXT_FIRST && t < TT_CONTEXT_FIRST + 3 && d->shortctx ? shortnames[t - TT_CONTEXT_FIRST] : tabs[t].name;
}
static int item_on(struct RData *d, const Item *it) { return it->st && bit_on(d, it->st); }
static int item_enabled(struct RData *d, const Item *it) { return !it->en || bit_on(d, it->en); }

/* too little room: 1 small buttons show their icons alone; 2 large ones become small icons too */
static int squeeze;
static int large(const Item *it) { return it->size == S_LARGE && squeeze < 2; }
static int bare(const Item *it) { return squeeze && (it->size == S_SMALL || (it->size == S_LARGE && squeeze >= 2)) && it->icon && (it->kind == K_BUTTON || it->kind == K_DROP); }
static int split_at(struct RastPort *rp, const char *s, LONG *w)
{
  int n = (int)strlen(s), best = 0, i;
  LONG bw;
  SetFont(rp, theme_font());
  bw = TextLength(rp, (STRPTR)s, (ULONG)n);
  for (i = 1; i < n - 1; i++)
    if (s[i] == ' ') {
      LONG a = TextLength(rp, (STRPTR)s, (ULONG)i), b = TextLength(rp, (STRPTR)s + i + 1, (ULONG)(n - i - 1));
      if ((a > b ? a : b) < bw) { bw = a > b ? a : b; best = i; }
    }
  *w = bw;
  return best;
}
static LONG item_w(struct RastPort *rp, const Item *it)
{
  LONG lw = it->label ? theme_textlen(rp, theme_font(), it->label) : 0;
  if (bare(it)) return 22 + (it->kind == K_DROP ? 9 : 0);
  if (large(it) && it->label) { LONG w2; if (split_at(rp, tr(it->label), &w2) && (squeeze || lw > 64)) lw = w2; }
  if (it->kind == K_LABEL) return lw + 6;
  if (it->kind == K_FIELD) return it->fieldw + (it->label ? lw + 4 : 0);
  if (it->flags & F_CHECK) return lw + 26;
  if (large(it)) return (lw > 30 ? lw : 30) + 10;
  if (it->size == S_ICON) return (squeeze ? 20 : 22) + (it->kind == K_DROP ? 9 : 0);
  return 16 + 6 + lw + 8 + (it->kind == K_DROP ? 9 : 0);
}
static LONG group_widths(struct RastPort *rp, const Tab *tab, LONG *gw)
{
  LONG total = 0;
  int g, i;
  for (g = 0; g < tab->n && g < 12; g++) {
    const Group *gr = &tab->groups[g];
    LONG w = 0, cap = theme_textlen(rp, theme_font(), gr->name) + 12;
    if (gr->mode == G_ROWS) {
      LONG r0 = 0, r1 = 0;
      for (i = 0; i < gr->n; i++) { if (gr->items[i].flags & F_ROW1) r1 += item_w(rp, &gr->items[i]) + 1; else r0 += item_w(rp, &gr->items[i]) + 1; }
      w = (r0 > r1 ? r0 : r1) + 8;
    } else {
      LONG colw = 0;
      int row = 0;
      for (i = 0; i < gr->n; i++) {
        const Item *it = &gr->items[i];
        if (large(it)) { if (colw) { w += colw; colw = 0; row = 0; } w += item_w(rp, it) + 2; continue; }
        if (row == 3 || (it->flags & F_NEWCOL)) { w += colw + 2; colw = 0; row = 0; }
        if (item_w(rp, it) > colw) colw = item_w(rp, it);
        row++;
      }
      w += colw + 8;
    }
    if (w < cap && !squeeze) w = cap;
    gw[g] = w; total += w;
  }
  return total;
}
/* every box of the shown tab in window pixels: the tabs, the quick access buttons, then the groups
   left to right with their items numbered in table order. If the groups don't fit it lays out again
   squeezed. Done on every draw, so it follows the window's width */
static void layout_ribbon(struct RData *d, Object *obj)
{
  struct RastPort *rp = _rp(obj);
  LONG x = _mleft(obj), y0 = _mtop(obj) + tabh() + 2, total;
  int t, g, i, n = 0;
  const Tab *tab;
  LONG gw[12];
  if (!tab_shown(d, d->tab)) d->tab = 1;
  {
    /* the padding shrinks, then the table tabs take short names, when they would run off the strip */
    LONG all = 0, pad = 16;
    int shown = 0;
    for (t = 0; t < NTABS; t++) if (tab_shown(d, t)) { all += theme_textlen(rp, theme_font(), tabs[t].name); shown++; }
    while (pad > 4 && all + pad * shown > _mwidth(obj) - NQA * 24 - 8) pad -= 2;
    d->shortctx = all + pad * shown > _mwidth(obj) - NQA * 24 - 8;
    for (t = 0; t < NTABS; t++) d->tabbox[t].x0 = (WORD)(tab_shown(d, t) ? pad : -1);
  }
  for (t = 0; t < NTABS; t++) {
    LONG w = theme_textlen(rp, theme_font(), tab_name(d, t)) + d->tabbox[t].x0;
    if (d->tabbox[t].x0 < 0) { d->tabbox[t].x1 = -1; continue; }
    d->tabbox[t].x0 = (WORD)x; d->tabbox[t].x1 = (WORD)(x + w - 1);
    d->tabbox[t].y0 = (WORD)_mtop(obj); d->tabbox[t].y1 = (WORD)(_mtop(obj) + tabh() - 1);
    x += w;
  }
  for (t = 0; t < NQA; t++) {
    LONG qx1 = _mright(obj) - 4 - (NQA - 1 - t) * 24;
    if (qx1 - 21 <= x) { d->qa[t].x0 = d->qa[t].x1 = -1; continue; }
    d->qa[t].x0 = (WORD)(qx1 - 21); d->qa[t].x1 = (WORD)qx1;
    d->qa[t].y0 = (WORD)(_mtop(obj) + 1); d->qa[t].y1 = (WORD)(_mtop(obj) + tabh() - 1);
  }
  tab = &tabs[d->tab];
  d->nitem = 0; d->ngroup = 0;
  if (!tab->groups) return;
  squeeze = 0;
  total = group_widths(rp, tab, gw);
  if (total > _mwidth(obj) + 8) { squeeze = 1; total = group_widths(rp, tab, gw); }
  /* still too wide: groups from the right become one button each, their tools in its list */
  for (g = 0; g < 12; g++) d->collapsed[g] = 0;
  for (g = tab->n - 1; g >= 0 && total > _mwidth(obj) + 8; g--) {
    LONG cw = theme_textlen(rp, theme_font(), tab->groups[g].name) + 16;
    if (cw < 52) cw = 52;
    total += cw - gw[g]; gw[g] = cw; d->collapsed[g] = 1;
  }
  if (total > _mwidth(obj) + 8) {
    /* even so: large buttons shrink to icons in the groups left open */
    LONG keep[12];
    for (g = 0; g < tab->n && g < 12; g++) keep[g] = gw[g];
    squeeze = 2; total = group_widths(rp, tab, gw);
    for (g = 0; g < tab->n && g < 12; g++) if (d->collapsed[g]) { total += keep[g] - gw[g]; gw[g] = keep[g]; }
  }
  x = _mleft(obj) + 2;
  for (g = 0; g < tab->n && g < 12; g++) {
    const Group *gr = &tab->groups[g];
    LONG w = gw[g], cx, top = y0 + 2;
    if (x + w > _mright(obj) + 6) break;          /* (no room even so) */
    if (d->collapsed[g] && n < MAXI) {
      d->item[n].x0 = (WORD)(x + 3); d->item[n].x1 = (WORD)(x + w - 4); d->item[n].y0 = (WORD)top; d->item[n].y1 = (WORD)(top + 3 * rowh() - 1);
      d->kitem[n] = NULL; d->kgroup[n] = g; n++;
      goto placed;
    }
    if (gr->mode == G_ROWS) {
      LONG rx[2];
      rx[0] = rx[1] = x + 4;
      for (i = 0; i < gr->n && n < MAXI; i++) {
        const Item *it = &gr->items[i];
        int r = (it->flags & F_ROW1) ? 1 : 0;
        LONG iw = item_w(rp, it), iy = top + 4 + r * (rowh() + 6);
        d->item[n].x0 = (WORD)rx[r]; d->item[n].x1 = (WORD)(rx[r] + iw - 1); d->item[n].y0 = (WORD)iy; d->item[n].y1 = (WORD)(iy + rowh() - 1);
        d->kitem[n] = it; d->kgroup[n] = g;
        rx[r] += iw + 1; n++;
      }
    } else {
      LONG colw = 0;
      int row = 0, colstart = n;
      cx = x + 4;
      for (i = 0; i < gr->n && n < MAXI; i++) {
        const Item *it = &gr->items[i];
        LONG iw = item_w(rp, it);
        if (large(it)) {
          int k;
          if (colw) { for (k = colstart; k < n; k++) d->item[k].x1 = (WORD)(d->item[k].x0 + colw - 1); cx += colw + 2; colw = 0; row = 0; }
          d->item[n].x0 = (WORD)cx; d->item[n].x1 = (WORD)(cx + iw - 1); d->item[n].y0 = (WORD)top; d->item[n].y1 = (WORD)(top + 3 * rowh() - 1);
          d->kitem[n] = it; d->kgroup[n] = g;
          cx += iw + 2; n++; colstart = n;
          continue;
        }
        if (row == 3 || (it->flags & F_NEWCOL)) {
          int k;
          for (k = colstart; k < n; k++) d->item[k].x1 = (WORD)(d->item[k].x0 + colw - 1);
          cx += colw + 2; colw = 0; row = 0; colstart = n;
        }
        d->item[n].x0 = (WORD)cx; d->item[n].x1 = (WORD)(cx + iw - 1);
        d->item[n].y0 = (WORD)(top + row * rowh()); d->item[n].y1 = (WORD)(top + (row + 1) * rowh() - 1);
        d->kitem[n] = it; d->kgroup[n] = g;
        if (iw > colw) colw = iw;
        row++; n++;
      }
      { int k; for (k = colstart; k < n; k++) if (d->item[k].x1 < d->item[k].x0 + colw - 1 && tab->groups[g].items[gr->n - (n - k)].kind != K_FIELD) d->item[k].x1 = (WORD)(d->item[k].x0 + colw - 1); }
    }
  placed:
    d->group[d->ngroup].x0 = (WORD)x; d->group[d->ngroup].x1 = (WORD)(x + w - 1);
    d->group[d->ngroup].y0 = (WORD)y0; d->group[d->ngroup].y1 = (WORD)(y0 + bodyh() + caph());
    d->ngroup++;
    x += w;
  }
  d->nitem = n;
}
/* placed item k's tool (NULL for a collapsed group's button) and its group */
static const Item *nth_item(struct RData *d, int k, int *group)
{
  if (k < 0 || k >= d->nitem) return NULL;
  if (group) *group = d->kgroup[k];
  return d->kitem[k];
}
static int is_group_button(struct RData *d, int k) { return k >= 0 && k < d->nitem && !d->kitem[k]; }

/* ------------------------------------------------------------------ */
/* drawing */
static void field_text(struct RData *d, const Item *it, char *out, size_t cap)
{
  out[0] = 0;
  switch (it->id) {
  case ID_FONTDD: utf8_to_latin1(d->st.font ? d->st.font : "", d->st.font ? (u32)strlen(d->st.font) : 0, out, cap); break;
  case ID_SIZEDD: sprintf(out, "%d%s", d->st.size / 10, d->st.size % 10 ? ".5" : ""); break;
  case ID_NUMFMTDD: strncpy(out, tr(d->st.numfmt ? d->st.numfmt : "General"), cap - 1); out[cap - 1] = 0; break;
  case ID_TBLNAME: case ID_PVNAME: strncpy(out, d->ctxname, cap - 1); out[cap - 1] = 0; break;
  default: chartrib_field(it->id, out, cap); break;
  }
}
static void draw_item(struct RData *d, Object *obj, int k)
{
  struct RastPort *rp = _rp(obj);
  int g;
  const Item *it = nth_item(d, k, &g);
  RBox *b = &d->item[k];
  int en, on, hot, pr;
  LONG bg;
  if (is_group_button(d, k)) {
    /* a collapsed group: its first tool's icon, its name and an arrow */
    const Group *gr = &tabs[d->tab].groups[d->kgroup[k]];
    int i, icon = 0;
    LONG lw = theme_textlen(rp, theme_font(), gr->name);
    for (i = 0; i < gr->n && !icon; i++) icon = gr->items[i].icon;
    theme_fill(rp, b->x0, b->y0, b->x1, b->y1, d->pressed == k ? tpen(T_PRESSED) : d->hot == k ? tpen(T_HOVER) : tpen(T_RIBBON));
    theme_icon(rp, icon, b->x0 + (b->x1 - b->x0 + 1 - 24) / 2, b->y0 + 4, 24, 0);
    theme_text(rp, theme_font(), b->x0 + (b->x1 - b->x0 + 1 - lw) / 2, b->y0 + 31, gr->name, tpen(T_TEXT));
    theme_icon(rp, IC_ARROWDOWN, b->x0 + (b->x1 - b->x0 + 1 - 8) / 2, b->y0 + 31 + fh(), 8, 0);
    return;
  }
  if (!it || k >= d->nitem) return;
  en = item_enabled(d, it); on = item_on(d, it); hot = d->hot == k && en; pr = d->pressed == k && en;
  if (it->flags & F_CHECK) {
    LONG bx = b->x0 + 4, by = b->y0 + (rowh() - 11) / 2;
    theme_fill(rp, b->x0, b->y0, b->x1, b->y1, pr ? tpen(T_PRESSED) : hot ? tpen(T_HOVER) : tpen(T_RIBBON));
    theme_fill(rp, bx, by, bx + 10, by + 10, tpen(T_WHITE));
    theme_rect(rp, bx, by, bx + 10, by + 10, tpen(en ? T_INPUTBORDER : T_DISABLED));
    if (on) {
      SetAPen(rp, tpen(en ? T_ACCENT : T_DISABLED));
      Move(rp, bx + 2, by + 5); Draw(rp, bx + 4, by + 7); Draw(rp, bx + 8, by + 3);
      Move(rp, bx + 2, by + 6); Draw(rp, bx + 4, by + 8); Draw(rp, bx + 8, by + 4);
    }
    if (it->label) theme_text(rp, theme_font(), b->x0 + 22, b->y0 + (rowh() - fh()) / 2, it->label, tpen(en ? T_TEXT : T_DISABLED));
    return;
  }
  bg = pr ? tpen(T_PRESSED) : on ? tpen(T_CHECKED) : hot ? tpen(T_HOVER) : tpen(T_RIBBON);
  theme_fill(rp, b->x0, b->y0, b->x1, b->y1, tpen(T_RIBBON));
  if (it->kind == K_LABEL) { theme_text(rp, theme_font(), b->x0 + 2, b->y0 + (rowh() - fh()) / 2, it->label, tpen(T_DIM)); return; }
  if (it->kind == K_FIELD) {
    char t[64];
    LONG fx = b->x0, lw = 0;
    if (it->label) { lw = theme_textlen(rp, theme_font(), it->label) + 4; theme_text(rp, theme_font(), fx, b->y0 + (rowh() - fh()) / 2, it->label, tpen(T_TEXT)); }
    fx += lw;
    theme_fill(rp, fx, b->y0 + 1, b->x1, b->y1 - 1, tpen(T_WHITE));
    theme_rect(rp, fx, b->y0 + 1, b->x1, b->y1 - 1, hot ? tpen(T_ACCENT) : tpen(T_INPUTBORDER));
    field_text(d, it, t, sizeof t);
    {
      struct TextExtent te;
      ULONG fit;
      SetFont(rp, theme_font());
      fit = TextFit(rp, (STRPTR)t, (ULONG)strlen(t), &te, NULL, 1, (ULONG)(b->x1 - fx - 14), 100);
      SetAPen(rp, tpen(T_TEXT));
      Move(rp, fx + 3, b->y0 + (rowh() - fh()) / 2 + theme_font()->tf_Baseline);
      Text(rp, (STRPTR)t, fit);
    }
    theme_icon(rp, IC_ARROWDOWN, b->x1 - 11, b->y0 + (rowh() - 10) / 2, 10, 0);
    return;
  }
  if (bg != tpen(T_RIBBON)) theme_fill(rp, b->x0, b->y0, b->x1, b->y1, bg);
  if (on) theme_rect(rp, b->x0, b->y0, b->x1, b->y1, tpen(T_SELECTION));
  if (large(it)) {
    LONG lw = it->label ? theme_textlen(rp, theme_font(), it->label) : 0, ix = b->x0 + (b->x1 - b->x0 + 1 - 24) / 2, w2;
    const char *lab = it->label ? tr(it->label) : NULL;
    int sp;
    theme_icon(rp, it->icon, ix, b->y0 + 4, 24, !en);
    sp = lab && (squeeze || lw > 64) ? split_at(rp, lab, &w2) : 0;
    if (sp) {
      LONG a = TextLength(rp, (STRPTR)lab, (ULONG)sp), c = TextLength(rp, (STRPTR)lab + sp + 1, (ULONG)strlen(lab + sp + 1));
      LONG cx = b->x0 + (b->x1 - b->x0 + 1 - c - (it->kind == K_DROP ? 10 : 0)) / 2;
      SetAPen(rp, tpen(en ? T_TEXT : T_DISABLED)); SetDrMd(rp, JAM1);
      Move(rp, b->x0 + (b->x1 - b->x0 + 1 - a) / 2, b->y0 + 30 + theme_font()->tf_Baseline); Text(rp, (STRPTR)lab, (ULONG)sp);
      Move(rp, cx, b->y0 + 30 + fh() + theme_font()->tf_Baseline); Text(rp, (STRPTR)lab + sp + 1, (ULONG)strlen(lab + sp + 1));
      if (it->kind == K_DROP) theme_icon(rp, IC_ARROWDOWN, cx + c + 2, b->y0 + 30 + fh() + (fh() - 8) / 2, 8, !en);
    } else {
      if (lab) theme_text(rp, theme_font(), b->x0 + (b->x1 - b->x0 + 1 - lw) / 2, b->y0 + 31, it->label, tpen(en ? T_TEXT : T_DISABLED));
      if (it->kind == K_DROP) theme_icon(rp, IC_ARROWDOWN, b->x0 + (b->x1 - b->x0 + 1 - 8) / 2, b->y0 + 31 + fh(), 8, !en);
    }
  } else {
    LONG iy = b->y0 + (rowh() - 16) / 2;
    theme_icon(rp, it->icon, b->x0 + 3, iy, 16, !en);
    if (it->size == S_SMALL && it->label && !bare(it)) theme_text(rp, theme_font(), b->x0 + 22, b->y0 + (rowh() - fh()) / 2, it->label, tpen(en ? T_TEXT : T_DISABLED));
    if (it->kind == K_DROP) theme_icon(rp, IC_ARROWDOWN, b->x1 - 9, b->y0 + (rowh() - 8) / 2, 8, !en);
  }
  if ((it->flags & F_SPLIT) && (hot || pr)) {
    SetAPen(rp, tpen(T_BORDER2));
    if (large(it)) { Move(rp, b->x0 + 2, b->y0 + 28); Draw(rp, b->x1 - 2, b->y0 + 28); }
    else { Move(rp, b->x1 - 12, b->y0 + 2); Draw(rp, b->x1 - 12, b->y1 - 2); }
  }
}
static void draw_tabs(struct RData *d, Object *obj)
{
  struct RastPort *rp = _rp(obj);
  int t;
  theme_fill(rp, _mleft(obj), _mtop(obj), _mright(obj), _mtop(obj) + tabh() + 1, tpen(T_TABSTRIP));
  for (t = 0; t < NTABS; t++) {
    RBox *b = &d->tabbox[t];
    LONG w;
    int act = t == d->tab;
    if (b->x0 < 0) continue;
    if (d->hot == 100 + t && !act) theme_fill(rp, b->x0 + 2, b->y0 + 2, b->x1 - 2, b->y1 - 1, tpen(T_HOVER));
    w = theme_textlen(rp, theme_font(), tab_name(d, t));
    if (t >= TT_CONTEXT_FIRST) theme_fill(rp, b->x0 + 2, b->y0, b->x1 - 2, b->y0 + 1, tpen(t == TT_CONTEXT_FIRST ? T_GREEN : T_ORANGE));   /* (contextual) */
    theme_text(rp, theme_font(), b->x0 + (b->x1 - b->x0 + 1 - w) / 2, b->y0 + (tabh() - fh()) / 2, tab_name(d, t), tpen(act || t >= TT_CONTEXT_FIRST ? T_ACCENT : T_TEXT));
    if (act) theme_fill(rp, b->x0 + (b->x1 - b->x0 + 1 - w) / 2, b->y1 - 1, b->x0 + (b->x1 - b->x0 + 1 + w) / 2, b->y1, tpen(T_ACCENT));
  }
  for (t = 0; t < NQA; t++) {
    RBox *b = &d->qa[t];
    int en = item_enabled(d, &g_undo[t]);
    if (b->x0 < 0) continue;
    if (d->hot == 200 + t && en) theme_fill(rp, b->x0, b->y0 + 1, b->x1, b->y1 - 1, tpen(T_HOVER));
    theme_icon(rp, g_undo[t].icon, b->x0 + 3, b->y0 + (b->y1 - b->y0 + 1 - 16) / 2, 16, !en);
  }
}
static void draw_all(struct RData *d, Object *obj)
{
  struct RastPort *rp = _rp(obj);
  LONG y0 = _mtop(obj) + tabh() + 2;
  int g, k;
  const Tab *tab;
  layout_ribbon(d, obj);
  draw_tabs(d, obj);
  theme_fill(rp, _mleft(obj), y0, _mright(obj), _mbottom(obj), tpen(T_RIBBON));
  SetAPen(rp, tpen(T_BORDER));
  Move(rp, _mleft(obj), _mbottom(obj)); Draw(rp, _mright(obj), _mbottom(obj));
  tab = &tabs[d->tab];
  if (!tab->groups) return;
  for (k = 0; k < d->nitem; k++) draw_item(d, obj, k);
  for (g = 0; g < d->ngroup && g < tab->n; g++) {
    RBox *b = &d->group[g];
    const char *name = tr(tab->groups[g].name);
    struct TextExtent te;
    ULONG fit;
    SetFont(rp, theme_font());
    fit = TextFit(rp, (STRPTR)name, (ULONG)strlen(name), &te, NULL, 1, (ULONG)(b->x1 - b->x0 - 3 > 0 ? b->x1 - b->x0 - 3 : 0), 100);
    SetAPen(rp, tpen(T_GROUPLABEL)); SetDrMd(rp, JAM1);
    Move(rp, b->x0 + (b->x1 - b->x0 + 1 - te.te_Width) / 2, y0 + bodyh() + theme_font()->tf_Baseline);
    Text(rp, (STRPTR)name, fit);
    SetAPen(rp, tpen(T_BORDER));
    Move(rp, b->x1, y0 + 6); Draw(rp, b->x1, _mbottom(obj) - 5);
  }
}

/* ------------------------------------------------------------------ */
/* the list window */
struct PData { struct RData *r; Object *ribbon; const List *l; int n, hot, cols; LONG w; const char *const *labels; };
static ULONG popdispatch(__reg("a0") struct IClass *cl, __reg("a2") Object *obj, __reg("a1") Msg msg);
static LONG pop_rowh(struct PData *p) { return p->l && p->l->kind == PL_FONTS ? FONTPX + 8 : p->l && (p->l->kind == PL_CELLSTYLE || p->l->kind == PL_TSTYLE) ? fh() + 9 : fh() + 5; }
static int pop_cols(struct PData *p) { return p->cols > 0 ? p->cols : 1; }
static int pop_rows(struct PData *p) { int c = pop_cols(p); return (p->n + c - 1) / c; }
/* opens item k's list under it, in a borderless window of its own (made on first use and kept).
   Long lists (fonts, functions) go into as many columns as keep them on the screen */
static void open_list_l(struct RData *d, Object *obj, int k, const List *l);
static void open_list(struct RData *d, Object *obj, int k)
{
  const Item *it = nth_item(d, k, NULL);
  if (!it) return;
  d->popgroup = -1; d->popdirect = NULL;
  open_list_l(d, obj, k, list_of(it->list));
}
/* a collapsed group's list: its tools by name */
static void open_group(struct RData *d, Object *obj, int k)
{
  const Group *gr = &tabs[d->tab].groups[d->kgroup[k]];
  int i, n = 0;
  for (i = 0; i < gr->n && n < 32; i++) {
    const Item *it = &gr->items[i];
    const char *h;
    size_t len;
    if (it->kind == K_LABEL) continue;
    if (it->label) { strncpy(d->gbuf[n], tr(it->label), sizeof d->gbuf[n] - 1); d->gbuf[n][sizeof d->gbuf[n] - 1] = 0; }
    else {
      h = it->help ? tr(it->help) : "";
      len = strcspn(h, "(:");
      while (len && h[len - 1] == ' ') len--;
      if (len >= sizeof d->gbuf[n]) len = sizeof d->gbuf[n] - 1;
      memcpy(d->gbuf[n], h, len); d->gbuf[n][len] = 0;
    }
    if (it->kind == K_DROP || it->kind == K_FIELD) strncat(d->gbuf[n], "...", sizeof d->gbuf[n] - 1 - strlen(d->gbuf[n]));
    d->glabels[n] = d->gbuf[n]; d->gidx[n] = i; n++;
  }
  d->glabels[n] = NULL;
  d->glist.labels = d->glabels; d->glist.n = n; d->glist.kind = PL_TEXT; d->glist.colors = NULL;
  d->popgroup = d->kgroup[k]; d->popdirect = NULL;
  open_list_l(d, obj, k, &d->glist);
}
static void open_list_l(struct RData *d, Object *obj, int k, const List *l)
{
  int i, rows;
  struct PData *pd;
  LONG x, y, w = 150, cw = 0, room;
  struct Window *win = _window(obj);
  if (!win || k < 0 || k >= d->nitem) return;
  if (!d->popwin) {
    d->poplist = NewObject(pcls->mcc_Class, NULL, MUIA_FillArea, FALSE, TAG_DONE);
    d->popwin = QWindowObject, MUIA_Window_Borderless, TRUE, MUIA_Window_CloseGadget, FALSE, MUIA_Window_DepthGadget, FALSE,
      MUIA_Window_SizeGadget, FALSE, MUIA_Window_DragBar, FALSE, MUIA_Window_Activate, TRUE,
      WindowContents, VGroup, InnerSpacing(0, 0), MUIA_Group_Spacing, 0, Child, d->poplist, End, End;
    if (!d->popwin) { d->poplist = NULL; return; }
    DoMethod(_app(obj), OM_ADDMEMBER, (ULONG)d->popwin);
    DoMethod(d->popwin, MUIM_Notify, MUIA_Window_Activate, FALSE, (ULONG)d->popwin, 3, MUIM_Set, MUIA_Window_Open, FALSE);
  }
  set(d->popwin, MUIA_Window_Open, FALSE);
  pd = INST_DATA(pcls->mcc_Class, d->poplist);
  pd->r = d; pd->ribbon = obj; pd->l = l; pd->hot = -1;
  if (l->kind == PL_FONTS) { pd->labels = ribbon_font_names(d->st.fs); for (pd->n = 0; pd->labels[pd->n]; pd->n++) ; }
  else { pd->n = l->n; pd->labels = l->labels; }
  d->popitem = k;
  room = _screen(obj)->Height - (win->TopEdge + d->item[k].y1 + 1) - 8;
  if (l->kind == PL_FONTS) set(_win(obj), MUIA_Window_Sleep, TRUE);
  for (i = 0; i < pd->n; i++) {
    LONG tw = 0;
    char t[64];
    if (l->kind == PL_FONTS) {
      Face *f = d->st.fs ? fonts_face(d->st.fs, pd->labels[i], 0, 0) : NULL;
      utf8_to_latin1(pd->labels[i], (u32)strlen(pd->labels[i]), t, sizeof t);
      tw = f ? theme_preview_width(f, t, FONTPX * 64) : 0;
      if (!tw) tw = theme_textlen(_rp(obj), theme_font(), t);
    } else tw = theme_textlen(_rp(obj), theme_font(), pd->labels[i]) + (l->kind == PL_COLOR ? 20 : l->kind == PL_BORDER ? 24 : l->kind == PL_TSTYLE ? 40 : 0);
    if (tw + 16 > cw) cw = tw + 16;
  }
  if (l->kind == PL_FONTS) set(_win(obj), MUIA_Window_Sleep, FALSE);
  rows = (int)(room / pop_rowh(pd));
  if (rows < 4) rows = 4;
  pd->cols = (pd->n + rows - 1) / rows;
  if (pd->cols < 1) pd->cols = 1;
  if (cw * pd->cols > _screen(obj)->Width) cw = _screen(obj)->Width / pd->cols;
  w = cw * pd->cols;
  if (w < 120) w = 120;
  if (d->item[k].x1 - d->item[k].x0 + 1 > w) w = d->item[k].x1 - d->item[k].x0 + 1;
  pd->w = w;
  x = win->LeftEdge + d->item[k].x0; y = win->TopEdge + d->item[k].y1 + 1;
  if (x + w > _screen(obj)->Width) x = _screen(obj)->Width - w;
  if (x < 0) x = 0;
  SetAttrs(d->popwin, MUIA_Window_LeftEdge, x, MUIA_Window_TopEdge, y, TAG_DONE);
  DoMethod(d->popwin, MUIM_Window_ScreenToFront);
  set(d->popwin, MUIA_Window_Open, TRUE);
}
static ULONG split_id(UWORD id);
static void chosen(Object *obj, struct RData *d, int k, LONG value)
{
  const Item *it = d->popdirect ? d->popdirect : nth_item(d, k, NULL);
  if (d->popgroup >= 0) {
    /* a tool from a collapsed group's list: a button does its command, a list opens (once this one has gone) */
    const Group *gr = &tabs[d->tab].groups[d->popgroup];
    d->popgroup = -1;
    if (value < 0 || value >= d->glist.n) return;
    it = &gr->items[d->gidx[value]];
    if (it->kind == K_DROP || it->kind == K_FIELD) { d->defer = it; DoMethod(_app(obj), MUIM_Application_PushMethod, (ULONG)obj, 2, MUIM_Ribbon_Open, k); }
    else DoMethod(_app(obj), MUIM_Application_ReturnID, (ULONG)it->id);
    return;
  }
  d->popdirect = NULL;
  if (!it) return;
  if (it->id == ID_FN_MORE && value >= 0 && value < N(fn_sub)) {
    /* (uitab) a category of More Functions: its functions' list next */
    d->defer = &fn_sub[value];
    DoMethod(_app(obj), MUIM_Application_PushMethod, (ULONG)obj, 2, MUIM_Ribbon_Open, k);
    return;
  }
  d->value = value;
  DoMethod(_app(obj), MUIM_Application_ReturnID, (ULONG)it->id);
}
/* the nearest pen to an 0xRRGGBB (ReleasePen it) */
static LONG rgbpen(Object *obj, ULONG c)
{
  return ObtainBestPen(_screen(obj)->ViewPort.ColorMap, ((c >> 16) & 255) * 0x01010101UL, ((c >> 8) & 255) * 0x01010101UL, (c & 255) * 0x01010101UL,
                       OBP_Precision, PRECISION_IMAGE, TAG_DONE);
}
/* a small picture of a border choice: a cell with the lines it puts on */
static void border_picture(struct RastPort *rp, int i, LONG x, LONG y, LONG s)
{
  LONG x1 = x + s, y1 = y + s;
  SetAPen(rp, tpen(T_BORDER2)); SetDrPt(rp, 0xAAAA);
  Move(rp, x, y); Draw(rp, x1, y); Draw(rp, x1, y1); Draw(rp, x, y1); Draw(rp, x, y);
  Move(rp, (x + x1) / 2, y); Draw(rp, (x + x1) / 2, y1); Move(rp, x, (y + y1) / 2); Draw(rp, x1, (y + y1) / 2);
  SetDrPt(rp, 0xFFFF);
  SetAPen(rp, tpen(T_TEXT));
  switch (i) {
  case 0: Move(rp, x, y1); Draw(rp, x1, y1); break;
  case 1: Move(rp, x, y); Draw(rp, x1, y); break;
  case 2: Move(rp, x, y); Draw(rp, x, y1); break;
  case 3: Move(rp, x1, y); Draw(rp, x1, y1); break;
  case 4: break;
  case 5: Move(rp, (x + x1) / 2, y); Draw(rp, (x + x1) / 2, y1); Move(rp, x, (y + y1) / 2); Draw(rp, x1, (y + y1) / 2); /* fall through */
  case 6: Move(rp, x, y); Draw(rp, x1, y); Draw(rp, x1, y1); Draw(rp, x, y1); Draw(rp, x, y); break;
  case 7: RectFill(rp, x, y, x1, y + 1); RectFill(rp, x, y1 - 1, x1, y1); RectFill(rp, x, y, x + 1, y1); RectFill(rp, x1 - 1, y, x1, y1); break;
  case 8: Move(rp, x, y1 - 2); Draw(rp, x1, y1 - 2); Move(rp, x, y1); Draw(rp, x1, y1); break;
  case 9: RectFill(rp, x, y1 - 1, x1, y1); break;
  case 10: Move(rp, x, y); Draw(rp, x1, y); Move(rp, x, y1); Draw(rp, x1, y1); break;
  }
}
static ULONG popdispatch(__reg("a0") struct IClass *cl, __reg("a2") Object *obj, __reg("a1") Msg msg)
{
  struct PData *p;
  switch (msg->MethodID) {
  case OM_NEW:
    if (!(obj = (Object *)DoSuperMethodA(cl, obj, msg))) return 0;
    p = INST_DATA(cl, obj);
    memset(p, 0, sizeof *p);
    p->hot = -1;
    return (ULONG)obj;
  case MUIM_Setup:
    if (!DoSuperMethodA(cl, obj, msg)) return FALSE;
    MUI_RequestIDCMP(obj, IDCMP_MOUSEBUTTONS | IDCMP_MOUSEMOVE);
    return TRUE;
  case MUIM_Cleanup:
    MUI_RejectIDCMP(obj, IDCMP_MOUSEBUTTONS | IDCMP_MOUSEMOVE);
    break;
  case MUIM_AskMinMax: {
    struct MUIP_AskMinMax *m = (struct MUIP_AskMinMax *)msg;
    LONG h;
    p = INST_DATA(cl, obj);
    DoSuperMethodA(cl, obj, msg);
    h = (p->n ? pop_rows(p) : 1) * pop_rowh(p) + 4;
    m->MinMaxInfo->MinWidth += p->w ? p->w : 150; m->MinMaxInfo->DefWidth += p->w ? p->w : 150; m->MinMaxInfo->MaxWidth += p->w ? p->w : 150;
    m->MinMaxInfo->MinHeight += h; m->MinMaxInfo->DefHeight += h; m->MinMaxInfo->MaxHeight += h;
    return 0;
  }
  case MUIM_Draw: {
    struct RastPort *rp;
    int i;
    DoSuperMethodA(cl, obj, msg);
    p = INST_DATA(cl, obj);
    rp = _rp(obj);
    theme_fill(rp, _mleft(obj), _mtop(obj), _mright(obj), _mbottom(obj), tpen(T_WHITE));
    theme_rect(rp, _mleft(obj), _mtop(obj), _mright(obj), _mbottom(obj), tpen(T_BORDER2));
    for (i = 0; i < p->n; i++) {
      LONG cw = (_mwidth(obj) - 2) / pop_cols(p), y = _mtop(obj) + 2 + (i % pop_rows(p)) * pop_rowh(p), x0 = _mleft(obj) + 1 + (i / pop_rows(p)) * cw, x = x0 + 5;
      char t[64];
      if (i == p->hot) theme_fill(rp, x0, y, x0 + cw - 1, y + pop_rowh(p) - 1, tpen(T_HOVER));
      if (p->l->kind == PL_FONTS) {
        Face *f = p->r->st.fs ? fonts_face(p->r->st.fs, p->labels[i], 0, 0) : NULL;
        utf8_to_latin1(p->labels[i], (u32)strlen(p->labels[i]), t, sizeof t);
        if (!f || !theme_preview(rp, f, t, FONTPX * 64, x, y + 4 + FONTPX * 4 / 5, cw - 10, tpen(T_TEXT)))
          theme_text(rp, theme_font(), x, y + (pop_rowh(p) - fh()) / 2, t, tpen(T_TEXT));
        continue;
      }
      if (p->l->kind == PL_CELLSTYLE) {
        /* the style's look: its fill behind its name in its colour */
        ULONG fl, co;
        int bold, line, size;
        LONG fp = -1, cp = -1, tw;
        struct ColorMap *cm = _screen(obj)->ViewPort.ColorMap;
        ribbon_cell_style(i, &fl, &co, &bold, &line, &size);
        if (fl) { fp = rgbpen(obj, fl); theme_fill(rp, x - 2, y + 1, x0 + cw - 4, y + pop_rowh(p) - 2, fp); }
        if (!(co & 0xFF000000UL)) cp = rgbpen(obj, co);
        tw = theme_textlen(rp, bold ? theme_bold() : theme_font(), p->labels[i]);
        theme_text(rp, bold ? theme_bold() : theme_font(), x, y + (pop_rowh(p) - fh()) / 2, p->labels[i], cp >= 0 ? cp : tpen(T_TEXT));
        if (line) theme_fill(rp, x, y + pop_rowh(p) - 3, x + tw, y + pop_rowh(p) - 3 + (line == 2 ? 1 : 0), cp >= 0 ? cp : tpen(T_TEXT));
        if (fp >= 0) ReleasePen(cm, fp);
        if (cp >= 0) ReleasePen(cm, cp);
        continue;
      }
      strncpy(t, p->labels[i], sizeof t - 1); t[sizeof t - 1] = 0;
      if (p->l->kind == PL_TSTYLE) { tabletab_preview(obj, rp, i, x - 2, y + 2, 34, pop_rowh(p) - 5); x += 36; }
      if (p->l->kind == PL_BORDER) { border_picture(rp, i, x, y + 3, pop_rowh(p) - 7); x += pop_rowh(p) + 2; }
      if (p->l->kind == PL_COLOR) {
        ULONG c = p->l->colors[i];
        if (c == COLOR_AUTO) theme_rect(rp, x, y + 2, x + 13, y + fh() + 1, tpen(T_DIM));
        else if (c == COLOR_MORE) {
          LONG my = y + 2 + (fh() - 1) / 2;
          theme_fill(rp, x, y + 2, x + 6, my, tpen(T_RED)); theme_fill(rp, x + 7, y + 2, x + 13, my, tpen(T_GREEN));
          theme_fill(rp, x, my + 1, x + 6, y + fh() + 1, tpen(T_ACCENT)); theme_fill(rp, x + 7, my + 1, x + 13, y + fh() + 1, tpen(T_YELLOW));
        } else {
          LONG pen = rgbpen(obj, c);
          theme_fill(rp, x, y + 2, x + 13, y + fh() + 1, pen);
          theme_rect(rp, x, y + 2, x + 13, y + fh() + 1, tpen(T_BORDER2));
          ReleasePen(_screen(obj)->ViewPort.ColorMap, pen);
        }
        x += 20;
      }
      theme_text(rp, theme_font(), x, y + 2, t, tpen(T_TEXT));
    }
    return 0;
  }
  case MUIM_HandleInput: {
    struct MUIP_HandleInput *m = (struct MUIP_HandleInput *)msg;
    p = INST_DATA(cl, obj);
    if (m->imsg && (m->imsg->Class == IDCMP_MOUSEMOVE || m->imsg->Class == IDCMP_MOUSEBUTTONS)) {
      WORD mx = m->imsg->MouseX, my = m->imsg->MouseY;
      int i = -1;
      if (mx >= _mleft(obj) && mx <= _mright(obj) && my >= _mtop(obj) + 2) {
        LONG cw = (_mwidth(obj) - 2) / pop_cols(p);
        int col = (int)((mx - _mleft(obj) - 1) / (cw > 0 ? cw : 1)), row = (int)((my - _mtop(obj) - 2) / pop_rowh(p));
        if (col < pop_cols(p) && row < pop_rows(p)) i = col * pop_rows(p) + row;
      }
      if (i >= p->n) i = -1;
      if (m->imsg->Class == IDCMP_MOUSEMOVE && i != p->hot) { p->hot = i; MUI_Redraw(obj, MADF_DRAWOBJECT); }
      if (m->imsg->Class == IDCMP_MOUSEBUTTONS && m->imsg->Code == SELECTUP && i >= 0) {
        Object *win = _win(obj);
        /* the value: a colour, a size in tenths of a point, else the entry's number */
        LONG value = p->l->kind == PL_COLOR ? (LONG)p->l->colors[i] : i;
        if (p->l == &lists[L_SIZES]) value = (LONG)(atof(p->labels[i]) * 10);
        DoMethod(_app(obj), MUIM_Application_PushMethod, (ULONG)win, 3, MUIM_Set, MUIA_Window_Open, FALSE);
        chosen(p->ribbon, p->r, p->r->popitem, value);
      }
    }
    return 0;
  }
  }
  return DoSuperMethodA(cl, obj, msg);
}

/* a split button's own command (the list is the rest of what it does) */
static ULONG split_id(UWORD id)
{
  switch (id) {
  case ID_PASTEDD: return ID_PASTE;
  case ID_MERGEDD: return ID_MERGECENTER;
  case ID_AUTOSUMDD: return ID_AUTOSUM;
  case ID_DVDD: return ID_DV;
  case ID_GROUPDD: return ID_GROUP;
  case ID_LX_REMOVEDD: return ID_LX_REMOVEARROWS;
  case ID_ZOOMDD: return ID_LX_ZOOMDLG;
  }
  return id;
}
static int on_arrow(struct RData *d, const Item *it, int k, WORD x, WORD y)
{
  RBox *b = &d->item[k];
  return large(it) ? y >= b->y0 + 29 : x >= b->x1 - 11;
}

/* ------------------------------------------------------------------ */
/* the pointer */
static int hit(struct RData *d, WORD mx, WORD my)
{
  int k, t;
  for (t = 0; t < NQA; t++)
    if (d->qa[t].x0 >= 0 && mx >= d->qa[t].x0 && mx <= d->qa[t].x1 && my >= d->qa[t].y0 && my <= d->qa[t].y1) return 200 + t;
  for (t = 0; t < NTABS; t++)
    if (d->tabbox[t].x0 >= 0 && mx >= d->tabbox[t].x0 && mx <= d->tabbox[t].x1 && my >= d->tabbox[t].y0 && my <= d->tabbox[t].y1) return 100 + t;
  for (k = 0; k < d->nitem; k++)
    if (mx >= d->item[k].x0 && mx <= d->item[k].x1 && my >= d->item[k].y0 && my <= d->item[k].y1) return k;
  return -1;
}
static void redraw_hot(struct RData *d, Object *obj, int old)
{
  if (old >= 100 || d->hot >= 100) draw_tabs(d, obj);
  if (old >= 0 && old < 100) draw_item(d, obj, old);
  if (d->hot >= 0 && d->hot < 100) draw_item(d, obj, d->hot);
}
static ULONG events(struct IClass *cl, Object *obj, struct MUIP_HandleEvent *m)
{
  struct RData *d = INST_DATA(cl, obj);
  struct IntuiMessage *im = m->imsg;
  int h, old;
  if (!im || !d->shown) return 0;
  if (im->Class == IDCMP_MOUSEMOVE) {
    h = (im->MouseX >= _mleft(obj) && im->MouseX <= _mright(obj) && im->MouseY >= _mtop(obj) && im->MouseY <= _mbottom(obj)) ? hit(d, im->MouseX, im->MouseY) : -1;
    if (h != d->hot) { old = d->hot; d->hot = h; redraw_hot(d, obj, old); }
    return 0;
  }
  if (im->Class != IDCMP_MOUSEBUTTONS) return 0;
  if (im->Code == SELECTDOWN) {
    if (im->MouseX < _mleft(obj) || im->MouseX > _mright(obj) || im->MouseY < _mtop(obj) || im->MouseY > _mbottom(obj)) return 0;
    h = hit(d, im->MouseX, im->MouseY);
    if (h >= 200) {
      if (item_enabled(d, &g_undo[h - 200])) DoMethod(_app(obj), MUIM_Application_ReturnID, (ULONG)g_undo[h - 200].id);
      return MUI_EventHandlerRC_Eat;
    }
    if (h >= 100) {
      int t = h - 100;
      if (t == 0) { DoMethod(_app(obj), MUIM_Application_ReturnID, ID_FILETAB); return MUI_EventHandlerRC_Eat; }
      if (t != d->tab) { d->tab = t; d->hot = -1; MUI_Redraw(obj, MADF_DRAWOBJECT); }
      return MUI_EventHandlerRC_Eat;
    }
    if (h >= 0) {
      int g;
      const Item *it = nth_item(d, h, &g);
      if (!is_group_button(d, h) && (!it || !item_enabled(d, it) || it->kind == K_LABEL)) return MUI_EventHandlerRC_Eat;
      d->pressed = h;
      draw_item(d, obj, h);
      return MUI_EventHandlerRC_Eat;
    }
    return MUI_EventHandlerRC_Eat;
  }
  if (im->Code == SELECTUP && d->pressed >= 0) {
    int k = d->pressed, g;
    const Item *it = nth_item(d, k, &g);
    d->pressed = -1;
    draw_item(d, obj, k);
    if (is_group_button(d, k) && hit(d, im->MouseX, im->MouseY) == k) { open_group(d, obj, k); return MUI_EventHandlerRC_Eat; }
    if (it && hit(d, im->MouseX, im->MouseY) == k) {
      if ((it->flags & F_SPLIT) && !on_arrow(d, it, k, im->MouseX, im->MouseY)) DoMethod(_app(obj), MUIM_Application_ReturnID, split_id(it->id));
      else if (it->kind == K_DROP || (it->kind == K_FIELD && it->list)) open_list(d, obj, k);
      else DoMethod(_app(obj), MUIM_Application_ReturnID, (ULONG)it->id);
    }
    return MUI_EventHandlerRC_Eat;
  }
  return 0;
}

/* the ribbon class's dispatcher. MUIM_Ribbon_State is sent as the active cell moves: ignored when
   nothing shown has changed, so the ribbon isn't redrawn on every key */
static ULONG dispatch(__reg("a0") struct IClass *cl, __reg("a2") Object *obj, __reg("a1") Msg msg)
{
  struct RData *d;
  switch (msg->MethodID) {
  case OM_NEW:
    if (!(obj = (Object *)DoSuperMethodA(cl, obj, msg))) return 0;
    d = INST_DATA(cl, obj);
    memset(d, 0, sizeof *d);
    d->tab = 1; d->hot = d->pressed = -1; d->popgroup = -1;
    d->ehn.ehn_Object = obj; d->ehn.ehn_Class = cl; d->ehn.ehn_Events = IDCMP_MOUSEBUTTONS | IDCMP_MOUSEMOVE;
    return (ULONG)obj;
  case OM_GET: {
    struct opGet *g = (struct opGet *)msg;
    d = INST_DATA(cl, obj);
    if (g->opg_AttrID == MUIA_Ribbon_Value) { *g->opg_Storage = (ULONG)d->value; return TRUE; }
    if (g->opg_AttrID == MUIA_Ribbon_Tab) { *g->opg_Storage = (ULONG)d->tab; return TRUE; }
    break;
  }
  case OM_SET: {
    struct TagItem *tags = ((struct opSet *)msg)->ops_AttrList, *t;
    d = INST_DATA(cl, obj);
    while ((t = NextTagItem(&tags)))
      if (t->ti_Tag == MUIA_Ribbon_Tab && t->ti_Data < NTABS) { d->tab = (int)t->ti_Data; if (_win(obj)) MUI_Redraw(obj, MADF_DRAWOBJECT); }
      else if (t->ti_Tag == MUIA_Ribbon_Value) d->value = (LONG)t->ti_Data;
    break;
  }
  case MUIM_AskMinMax: {
    struct MUIP_AskMinMax *m = (struct MUIP_AskMinMax *)msg;
    LONG h = tabh() + 2 + bodyh() + caph() + 3;
    DoSuperMethodA(cl, obj, msg);
    m->MinMaxInfo->MinWidth += 200; m->MinMaxInfo->DefWidth += 640; m->MinMaxInfo->MaxWidth += MUI_MAXMAX;
    m->MinMaxInfo->MinHeight += h; m->MinMaxInfo->DefHeight += h; m->MinMaxInfo->MaxHeight += h;
    return 0;
  }
  case MUIM_Setup:
    if (!DoSuperMethodA(cl, obj, msg)) return FALSE;
    d = INST_DATA(cl, obj);
    theme_setup(obj);
    theme_handler(obj, &d->ehn);
    return TRUE;
  case MUIM_Cleanup:
    theme_cleanup(obj);
    break;
  case MUIM_Draw:
    DoSuperMethodA(cl, obj, msg);
    if (((struct MUIP_Draw *)msg)->flags & (MADF_DRAWOBJECT | MADF_DRAWUPDATE)) draw_all(INST_DATA(cl, obj), obj);
    return 0;
  case MUIM_Show: ((struct RData *)INST_DATA(cl, obj))->shown = 1; break;
  case MUIM_Hide: ((struct RData *)INST_DATA(cl, obj))->shown = 0; break;
  case MUIM_HandleEvent:
    return events(cl, obj, (struct MUIP_HandleEvent *)msg);
  case MUIM_CreateShortHelp: {
    struct MUIP_CreateShortHelp *m = (struct MUIP_CreateShortHelp *)msg;
    static char help[600];
    char name[80], keys[32], body[400];
    const char *h, *open, *tip;
    const Item *it;
    int g, k;
    d = INST_DATA(cl, obj);
    k = hit(d, (WORD)m->mx, (WORD)m->my);
    if (k >= 200 && k < 200 + NQA) it = &g_undo[k - 200];
    else if (is_group_button(d, k)) return (ULONG)theme_bubble(tr(tabs[d->tab].groups[d->kgroup[k]].name), tr("There isn't room for this group's tools: click for a list of them."));
    else if (k < 0 || k >= 100 || !(it = nth_item(d, k, &g)) || !(it->help || it->label)) return 0;
    h = it->help ? tr(it->help) : "";
    keys[0] = 0;
    if ((open = strrchr(h, '(')) && (!strncmp(open, "(Amiga-", 7) || !strncmp(open, "(Shift-", 7) || !strncmp(open, "(Alt-", 5) || !strncmp(open, "(Ctrl-", 6) || !strncmp(open, "(F", 2)) && open[strlen(open) - 1] == ')') {
      strncpy(keys, open, sizeof keys - 1); keys[sizeof keys - 1] = 0;
    } else open = NULL;
    if (it->label) { strncpy(name, tr(it->label), sizeof name - 1); name[sizeof name - 1] = 0; }
    else {
      size_t n = open ? (size_t)(open - h) : strlen(h);
      const char *colon = strchr(h, ':');
      if (colon && (size_t)(colon - h) < n) n = (size_t)(colon - h);
      while (n && h[n - 1] == ' ') n--;
      if (n >= sizeof name) n = sizeof name - 1;
      memcpy(name, h, n); name[n] = 0;
    }
    body[0] = 0;
    if ((tip = tip_for(it->id))) { strncpy(body, tr(tip), sizeof body - 1); body[sizeof body - 1] = 0; }
    else if (it->help) {
      size_t n = open ? (size_t)(open - h) : strlen(h);
      while (n && h[n - 1] == ' ') n--;
      if (n >= sizeof body) n = sizeof body - 1;
      memcpy(body, h, n); body[n] = 0;
      if (!strcmp(body, name)) body[0] = 0;
      else if (!strncmp(body, name, strlen(name)) && body[strlen(name)] == ':') memmove(body, body + strlen(name) + 2, strlen(body + strlen(name) + 2) + 1);
    }
    if (it->kind == K_DROP && !it->label && body[0] == 0) strcpy(body, tr("Click to choose from a list."));
    snprintf(help, sizeof help, "\33b%s\33n%s%s", name, keys[0] ? "  " : "", keys);
    if (body[0]) {
      char *o = help + strlen(help);
      const char *b = body;
      size_t room = sizeof help - (size_t)(o - help) - 2;
      int col = 0;
      *o++ = '\n'; room--;
      while (*b && room > 1) {
        const char *sp = b;
        int wl;
        while (*sp && *sp != ' ') sp++;
        wl = (int)(sp - b);
        if (col && col + 1 + wl > 46) { *o++ = '\n'; room--; col = 0; }
        else if (col) { *o++ = ' '; room--; col++; }
        while (b < sp && room > 1) { *o++ = *b++; room--; col++; }
        while (*b == ' ') b++;
      }
      *o = 0;
    }
    return (ULONG)help;
  }
  case MUIM_DeleteShortHelp: return 0;
  case MUIM_Ribbon_Open:
    d = INST_DATA(cl, obj);
    if (d->defer) { const Item *it = d->defer; d->defer = NULL; d->popgroup = -1; d->popdirect = it; open_list_l(d, obj, (int)((ULONG *)msg)[1], list_of(it->list)); }
    return 0;
  case MUIM_Ribbon_State: {
    struct MUIP_Ribbon_State *m = (struct MUIP_Ribbon_State *)msg;
    d = INST_DATA(cl, obj);
    if (m->on == d->st.on && m->ctx == d->st.ctx && m->ext == d->st.ext && m->size == d->st.size && m->font && !strcmp(m->font, d->font) && m->numfmt && !strcmp(m->numfmt, d->numfmt)
        && !strcmp(m->ctxname ? m->ctxname : "", d->ctxname) && lx_bits_hash() == d->lxhash) return 0;
    d->lxhash = lx_bits_hash();
    d->st = *m;
    strncpy(d->ctxname, m->ctxname ? m->ctxname : "", sizeof d->ctxname - 1); d->ctxname[sizeof d->ctxname - 1] = 0;
    if (m->font) { strncpy(d->font, m->font, sizeof d->font - 1); d->font[sizeof d->font - 1] = 0; }
    if (m->numfmt) { strncpy(d->numfmt, m->numfmt, sizeof d->numfmt - 1); d->numfmt[sizeof d->numfmt - 1] = 0; }
    d->st.font = d->font; d->st.numfmt = d->numfmt;
    if (_win(obj)) MUI_Redraw(obj, MADF_DRAWOBJECT);
    return 0;
  }
  }
  return DoSuperMethodA(cl, obj, msg);
}
struct MUI_CustomClass *ribbon_class(void)
{
  if (!rcls) rcls = MUI_CreateCustomClass(NULL, (STRPTR)MUIC_Area, NULL, sizeof(struct RData), (APTR)dispatch);
  if (!pcls) pcls = MUI_CreateCustomClass(NULL, (STRPTR)MUIC_Area, NULL, sizeof(struct PData), (APTR)popdispatch);
  return rcls && pcls ? rcls : NULL;
}
void ribbon_class_free(void)
{
  if (rcls) { MUI_DeleteCustomClass(rcls); rcls = NULL; }
  if (pcls) { MUI_DeleteCustomClass(pcls); pcls = NULL; }
}
