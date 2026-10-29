/*
 * tabletab.h - Ledger's tables and pivot tables on the Amiga side: the commands of Insert > Table
 * and PivotTable, Home > Format as Table, and the contextual tabs Table Design, PivotTable Analyze
 * and PivotTable Design (shown while the active cell is in a table or a pivot table), with their
 * menu items (tabletab.c), and the PivotTable Fields pane and its dialogs (pivotpane.c).
 *
 * Their command ids are a range of their own (main.c hands the range to tabletab_command).
 */
#ifndef LEDGER_TABLETAB_H
#define LEDGER_TABLETAB_H

#include "ledger.h"

enum {
  ID_TBL_FIRST = 2400,
  /* Insert, Home */
  ID_TBLNEW = ID_TBL_FIRST, ID_PIVOTNEW, ID_FMTTABLEDD,
  /* Table Design */
  ID_TBLNAME, ID_TBLRESIZE, ID_TBLPIVOT, ID_TBLDEDUPE, ID_TBLCONVERT,
  ID_TBLHEADER, ID_TBLTOTALS, ID_TBLBANDROWS, ID_TBLBANDCOLS, ID_TBLFIRSTCOL, ID_TBLLASTCOL, ID_TBLFILTERBTN,
  ID_TBLSTYLEDD, ID_TBLTOTALDD,
  /* PivotTable Analyze */
  ID_PVNAME, ID_PVFIELDS, ID_PVREFRESH, ID_PVSOURCE, ID_PVADDFIELD, ID_PVREMOVEFIELD, ID_PVMOVEDD, ID_PVVALUESET,
  ID_PVFILTER, ID_PVSORTAZ, ID_PVSORTZA, ID_PVCLEAR, ID_PVDELETE,
  /* PivotTable Design */
  ID_PVSUBTOTDD, ID_PVGRANDDD, ID_PVLAYOUTDD, ID_PVBLANKDD, ID_PVROWHEAD, ID_PVCOLHEAD, ID_PVBANDROWS, ID_PVBANDCOLS, ID_PVSTYLEDD,
  /* the menu's own forms of the lists */
  ID_TBLSTYLEMENU, ID_PVSUBTOTMENU, ID_PVGRANDMENU, ID_PVLAYOUTMENU, ID_PVBLANKMENU,
  /* the Fields pane and dialogs (internal) */
  ID_PV_FLIST, ID_PV_TOFILTERS, ID_PV_TOCOLS, ID_PV_TOROWS, ID_PV_TOVALUES, ID_PV_AREA0, ID_PV_AREA1, ID_PV_AREA2, ID_PV_AREA3,
  ID_PV_UP, ID_PV_DOWN, ID_PV_REMOVE, ID_PV_SETTINGS, ID_PV_ITEMS, ID_PV_CLOSE,
  ID_TBL_LAST
};

/* the Table menu (main.c's menus[]) */
#define TABLE_MENUS \
  M("Table"), \
  I("Insert Table...", "T", ID_TBLNEW), I("Format as Table...", 0, ID_TBLSTYLEMENU), I("Insert PivotTable...", 0, ID_PIVOTNEW), BAR, \
  I("Table Name...", 0, ID_TBLNAME), I("Resize Table...", 0, ID_TBLRESIZE), I("Summarize with PivotTable...", 0, ID_TBLPIVOT), \
  I("Remove Duplicates in Table", 0, ID_TBLDEDUPE), I("Convert to Range", 0, ID_TBLCONVERT), BAR, \
  T("Header Row", 0, ID_TBLHEADER), T("Total Row", 0, ID_TBLTOTALS), T("Banded Rows", 0, ID_TBLBANDROWS), T("Banded Columns", 0, ID_TBLBANDCOLS), \
  T("First Column", 0, ID_TBLFIRSTCOL), T("Last Column", 0, ID_TBLLASTCOL), T("Filter Button", 0, ID_TBLFILTERBTN), BAR, \
  I("PivotTable Name...", 0, ID_PVNAME), I("PivotTable Field List...", 0, ID_PVFIELDS), I("Refresh PivotTable", 0, ID_PVREFRESH), \
  I("Change Data Source...", 0, ID_PVSOURCE), I("Add Field...", 0, ID_PVADDFIELD), I("Remove Field", 0, ID_PVREMOVEFIELD), \
  I("Value Field Settings...", 0, ID_PVVALUESET), I("Filter Field...", 0, ID_PVFILTER), I("Sort Field A to Z", 0, ID_PVSORTAZ), \
  I("Sort Field Z to A", 0, ID_PVSORTZA), I("Clear PivotTable", 0, ID_PVCLEAR), I("Delete PivotTable", 0, ID_PVDELETE), BAR, \
  I("Subtotals...", 0, ID_PVSUBTOTMENU), I("Grand Totals...", 0, ID_PVGRANDMENU), I("Report Layout...", 0, ID_PVLAYOUTMENU), \
  I("Blank Rows...", 0, ID_PVBLANKMENU), T("Row Headers", 0, ID_PVROWHEAD), T("Column Headers", 0, ID_PVCOLHEAD), \
  T("Banded PivotTable Rows", 0, ID_PVBANDROWS), T("Banded PivotTable Columns", 0, ID_PVBANDCOLS)

/* (tabletab.c) a command of the range; v is MUIA_Ribbon_Value for a list's choice */
void tabletab_command(ULONG id, LONG v);
/* the ribbon's contextual states and name for the active cell (main.c's update_controls), and the
   menu items' ticks */
void tabletab_state(ULONG *ctx, const char **name);
/* (ribbon.c) a table style's small picture in a gallery list: style k of the gallery (tables or,
   with pivot, pivot tables) in the box x, y, w x h */
void tabletab_preview(Object *obj, struct RastPort *rp, int k, LONG x, LONG y, LONG w, LONG h);
/* a gallery entry's name ("TableStyleMedium2", "PivotStyleLight16") */
void tabletab_style_name(int k, int pivot, char *out);

/* (pivotpane.c) */
void pivot_dialog(const char *source);    /* Insert PivotTable: source, where; then the Fields pane */
void pivot_fields_pane(void);             /* the pane for the pivot table at the active cell */
int  pivot_value_settings(int sheet, int pi, int vi);   /* Value Field Settings; 1 if changed */
int  pivot_field_items(int sheet, int pi, int area, int fi);   /* the items a field shows; 1 if changed */
struct LPivot *pivot_here(int *sheet, int *pi);         /* the pivot table at the active cell, or NULL */
/* the field under the active cell: its area (PA_) and index; 0 if none */
int  pivot_field_here(int *area, int *fi);

#endif
