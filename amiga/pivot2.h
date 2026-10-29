/*
 * pivot2.h - Recommended PivotTables, PivotCharts, slicers, and the pivot tables' calculated
 * fields, grouping and expand/collapse on the Amiga side (pivot2.c). Their command ids are the
 * range 4000-4199 (main.c hands it to pivot2_command); their icons start at ICP_BASE (theme.c asks
 * pivot2_icon_desc for them).
 */
#ifndef LEDGER_PIVOT2_H
#define LEDGER_PIVOT2_H

enum {
  ID_P2_FIRST = 4000,
  ID_P2_RECPIVOT = ID_P2_FIRST, ID_P2_PIVOTCHART, ID_P2_SLICER, ID_P2_SLICERSET, ID_P2_SLICERCLEAR,
  ID_P2_CALCFIELD, ID_P2_GROUP, ID_P2_UNGROUP, ID_P2_EXPAND, ID_P2_COLLAPSE,
  ID_P2_LAST
};
#define ICP_BASE 700
enum { ICP_RECPIVOT = ICP_BASE, ICP_PIVOTCHART, ICP_SLICER, ICP_SLICERSET, ICP_CALCFIELD, ICP_GROUPFIELD, ICP_UNGROUPFIELD, ICP_EXPAND, ICP_COLLAPSE, ICP_N };
const char *pivot2_icon_desc(int icon);   /* NULL if not one of these */

/* the Insert menu's item and the Table menu's (main.c's menus[]) */
#define PIVOT2_INSERT_MENU \
  { NM_ITEM, (STRPTR)"PivotTable & Slicer", 0, 0, 0, 0 }, \
  { NM_SUB, (STRPTR)"Recommended PivotTables...", 0, 0, 0, (APTR)ID_P2_RECPIVOT }, { NM_SUB, (STRPTR)"PivotChart...", 0, 0, 0, (APTR)ID_P2_PIVOTCHART }, \
  { NM_SUB, (STRPTR)"Slicer...", 0, 0, 0, (APTR)ID_P2_SLICER }
#define PIVOT2_TABLE_MENU \
  { NM_ITEM, NM_BARLABEL, 0, 0, 0, 0 }, \
  { NM_ITEM, (STRPTR)"PivotTable Tools", 0, 0, 0, 0 }, \
  { NM_SUB, (STRPTR)"Recommended PivotTables...", 0, 0, 0, (APTR)ID_P2_RECPIVOT }, { NM_SUB, (STRPTR)"PivotChart...", 0, 0, 0, (APTR)ID_P2_PIVOTCHART }, \
  { NM_SUB, (STRPTR)"Insert Slicer...", 0, 0, 0, (APTR)ID_P2_SLICER }, { NM_SUB, (STRPTR)"Slicer Settings...", 0, 0, 0, (APTR)ID_P2_SLICERSET }, \
  { NM_SUB, (STRPTR)"Clear Slicer Filter", 0, 0, 0, (APTR)ID_P2_SLICERCLEAR }, { NM_SUB, NM_BARLABEL, 0, 0, 0, 0 }, \
  { NM_SUB, (STRPTR)"Calculated Field...", 0, 0, 0, (APTR)ID_P2_CALCFIELD }, { NM_SUB, (STRPTR)"Group Field...", 0, 0, 0, (APTR)ID_P2_GROUP }, \
  { NM_SUB, (STRPTR)"Ungroup", 0, 0, 0, (APTR)ID_P2_UNGROUP }, { NM_SUB, (STRPTR)"Expand Field", 0, 0, 0, (APTR)ID_P2_EXPAND }, \
  { NM_SUB, (STRPTR)"Collapse Field", 0, 0, 0, (APTR)ID_P2_COLLAPSE }

void pivot2_command(unsigned long id);

/* (grid.c) a pivot table's +/- buttons over its items, and a click on one (1: the report changed) */
struct RastPort;
struct GridGeo_;
void pivot2_draw_buttons(struct RastPort *rp, const void *geo);
int  pivot2_click(const void *geo, long x, long y);

/* (chartview.c) a slicer drawn in its box; a click at x, y in it (from its corner): 1 taken (an
   item, Clear Filter or Multi-Select), with *changed set when the workbook changed */
struct GridView;
void pivot2_draw_slicer(struct GridView *gv, int obj, long x0, long y0, long x1, long y1);
int  pivot2_slicer_click(struct GridView *gv, int obj, long x, long y, long w, long h, int shift);

#endif
