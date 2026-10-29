/*
 * tabletabs.h - the ribbon's tools for tables and pivot tables (included by ribbon.c twice: first
 * with TT_LISTS for the lists' entries, then with TT_TOOLS for the tools and the contextual tabs'
 * groups). Kept apart so ribbon.c only gains a few lines.
 */
#ifdef TT_LISTS
/* the table style gallery: 21 Light, 28 Medium, 11 Dark (tabletab_preview draws each) */
static const char *const tstyles[] = {
  "Light 1", "Light 2", "Light 3", "Light 4", "Light 5", "Light 6", "Light 7", "Light 8", "Light 9", "Light 10", "Light 11", "Light 12",
  "Light 13", "Light 14", "Light 15", "Light 16", "Light 17", "Light 18", "Light 19", "Light 20", "Light 21",
  "Medium 1", "Medium 2", "Medium 3", "Medium 4", "Medium 5", "Medium 6", "Medium 7", "Medium 8", "Medium 9", "Medium 10", "Medium 11",
  "Medium 12", "Medium 13", "Medium 14", "Medium 15", "Medium 16", "Medium 17", "Medium 18", "Medium 19", "Medium 20", "Medium 21",
  "Medium 22", "Medium 23", "Medium 24", "Medium 25", "Medium 26", "Medium 27", "Medium 28",
  "Dark 1", "Dark 2", "Dark 3", "Dark 4", "Dark 5", "Dark 6", "Dark 7", "Dark 8", "Dark 9", "Dark 10", "Dark 11" };
/* the totals row's function under the active cell's column (TF_ order) */
static const char *const totfns[] = { "None", "Sum", "Average", "Count", "Count Numbers", "Max", "Min", "StdDev", "Var" };
static const char *const pvsubtots[] = { "Do Not Show Subtotals", "Show All Subtotals at Bottom of Group", "Show All Subtotals at Top of Group" };
static const char *const pvgrands[] = { "Off for Rows and Columns", "On for Rows and Columns", "On for Rows Only", "On for Columns Only" };
static const char *const pvlayouts[] = { "Show in Compact Form", "Show in Outline Form", "Show in Tabular Form" };
static const char *const pvblanks[] = { "Insert Blank Line after Each Item", "Remove Blank Line after Each Item" };
static const char *const pvmoves[] = { "Move Up", "Move Down", "Move to Report Filter", "Move to Row Labels", "Move to Column Labels", "Move to Values" };
#undef TT_LISTS
#endif

#ifdef TT_TOOLS
/* Insert > Tables and Home > Styles' Format as Table */
static const Item i_tables[] = {
  { K_BUTTON, S_LARGE, IC_PIVOT, 0, "PivotTable", ID_PIVOTNEW, 0, 0, 0, 0, "Summarize the data in a PivotTable" },
  { K_BUTTON, S_LARGE, IC_TABLE, 0, "Table", ID_TBLNEW, 0, 0, 0, 0, "Make the data a table, to sort, filter and total it (Amiga-T)" },
};
/* Table Design */
static const Item td_props[] = {
  { K_FIELD, S_SMALL, 0, 0, "Name:", ID_TBLNAME, 0, CB_INTABLE, 0, 70, "Table Name: click to rename the table" },
  { K_BUTTON, S_SMALL, IC_TBLRESIZE, 0, "Resize Table", ID_TBLRESIZE, 0, CB_INTABLE, 0, 0, "Give the table a new range (the header row stays)" },
  { K_DROP, S_SMALL, IC_TOTALROW, L_TOTFN, "Total Function", ID_TBLTOTALDD, 0, CB_INTABLE, 0, 0, "The total row's function under the active cell's column" },
};
static const Item td_tools[] = {
  { K_BUTTON, S_SMALL, IC_PIVOT, 0, "Summarize with PivotTable", ID_TBLPIVOT, 0, CB_INTABLE, 0, 0, "A PivotTable of this table's data" },
  { K_BUTTON, S_SMALL, IC_REMOVEDUP, 0, "Remove Duplicates", ID_TBLDEDUPE, 0, CB_INTABLE, 0, 0, "Delete rows of the table that repeat an earlier one" },
  { K_BUTTON, S_SMALL, IC_TBLCONVERT, 0, "Convert to Range", ID_TBLCONVERT, 0, CB_INTABLE, 0, 0, "Make the table ordinary cells again, keeping its look" },
};
static const Item td_opts[] = {
  { K_BUTTON, S_SMALL, 0, 0, "Header Row", ID_TBLHEADER, CB_HEADER, CB_INTABLE, F_CHECK, 0, "Show the table's header row" },
  { K_BUTTON, S_SMALL, 0, 0, "Total Row", ID_TBLTOTALS, CB_TOTALS, CB_INTABLE, F_CHECK, 0, "A row of totals under the table" },
  { K_BUTTON, S_SMALL, 0, 0, "Banded Rows", ID_TBLBANDROWS, CB_BANDROWS, CB_INTABLE, F_CHECK, 0, "Shade every other row" },
  { K_BUTTON, S_SMALL, 0, 0, "First Column", ID_TBLFIRSTCOL, CB_FIRSTCOL, CB_INTABLE, F_CHECK | F_NEWCOL, 0, "The first column in bold" },
  { K_BUTTON, S_SMALL, 0, 0, "Last Column", ID_TBLLASTCOL, CB_LASTCOL, CB_INTABLE, F_CHECK, 0, "The last column in bold" },
  { K_BUTTON, S_SMALL, 0, 0, "Banded Columns", ID_TBLBANDCOLS, CB_BANDCOLS, CB_INTABLE, F_CHECK, 0, "Shade every other column" },
  { K_BUTTON, S_SMALL, 0, 0, "Filter Button", ID_TBLFILTERBTN, CB_FILTERBTN, CB_INTABLE, F_CHECK | F_NEWCOL, 0, "Filter buttons on the header row" },
};
static const Item td_styles[] = {
  { K_DROP, S_LARGE, IC_TSTYLES, L_TSTYLES, "Table Styles", ID_TBLSTYLEDD, 0, CB_INTABLE, 0, 0, "The table's look: Light, Medium and Dark styles" },
};
static const Group tdesign[] = { { "Properties", G_COLS, td_props, N(td_props) }, { "Tools", G_COLS, td_tools, N(td_tools) },
                                 { "Table Style Options", G_COLS, td_opts, N(td_opts) }, { "Table Styles", G_COLS, td_styles, N(td_styles) } };
/* PivotTable Analyze */
static const Item pa_pivot[] = {
  { K_FIELD, S_SMALL, 0, 0, "Name:", ID_PVNAME, 0, CB_INPIVOT, 0, 70, "PivotTable Name: click to rename it" },
  { K_BUTTON, S_SMALL, IC_PVDELETE, 0, "Delete", ID_PVDELETE, 0, CB_INPIVOT, 0, 0, "Delete the PivotTable and its report" },
  { K_BUTTON, S_SMALL, IC_CLEAR, 0, "Clear All", ID_PVCLEAR, 0, CB_INPIVOT, 0, 0, "Take every field out of the PivotTable" },
};
static const Item pa_field[] = {
  { K_BUTTON, S_LARGE, IC_VALSET, 0, "Field Settings", ID_PVVALUESET, 0, CB_INPIVOT, 0, 0, "Value Field Settings: how the active value field is summarized and shown" },
  { K_BUTTON, S_SMALL, IC_FIELDADD, 0, "Add Field", ID_PVADDFIELD, 0, CB_INPIVOT, 0, 0, "Add a field to the PivotTable" },
  { K_BUTTON, S_SMALL, IC_FIELDREMOVE, 0, "Remove Field", ID_PVREMOVEFIELD, 0, CB_PVFIELD, 0, 0, "Take the active cell's field out" },
  { K_DROP, S_SMALL, IC_FIELDMOVE, L_PVMOVE, "Move Field", ID_PVMOVEDD, 0, CB_PVFIELD, 0, 0, "Move the active cell's field to another area" },
};
static const Item pa_sort[] = {
  { K_BUTTON, S_SMALL, IC_SORTAZ, 0, "Sort A to Z", ID_PVSORTAZ, 0, CB_PVFIELD, 0, 0, "The active field's items smallest first" },
  { K_BUTTON, S_SMALL, IC_SORTZA, 0, "Sort Z to A", ID_PVSORTZA, 0, CB_PVFIELD, 0, 0, "The active field's items largest first" },
  { K_BUTTON, S_SMALL, IC_FILTER, 0, "Filter", ID_PVFILTER, 0, CB_INPIVOT, 0, 0, "Choose the items the active field shows" },
};
static const Item pa_data[] = {
  { K_BUTTON, S_LARGE, IC_PVREFRESH, 0, "Refresh", ID_PVREFRESH, 0, CB_INPIVOT, 0, 0, "Work the PivotTable out again from its source" },
  { K_BUTTON, S_LARGE, IC_PVSOURCE, 0, "Change Data Source", ID_PVSOURCE, 0, CB_INPIVOT, 0, 0, "The range or table the PivotTable summarizes" },
};
static const Item pa_show[] = {
  { K_BUTTON, S_LARGE, IC_PIVOTFIELDS, 0, "Field List", ID_PVFIELDS, 0, CB_INPIVOT, 0, 0, "The PivotTable Fields pane: put fields in Filters, Columns, Rows and Values" },
};
static const Group panalyze[] = { { "PivotTable", G_COLS, pa_pivot, N(pa_pivot) }, { "Active Field", G_COLS, pa_field, N(pa_field) },
                                  { "Sort & Filter", G_COLS, pa_sort, N(pa_sort) }, { "Data", G_COLS, pa_data, N(pa_data) },
                                  { "Show", G_COLS, pa_show, N(pa_show) } };
/* PivotTable Design */
static const Item pd_layout[] = {
  { K_DROP, S_LARGE, IC_SUBTOTALS, L_PVSUBTOT, "Subtotals", ID_PVSUBTOTDD, 0, CB_INPIVOT, 0, 0, "Subtotals: none, at the bottom or at the top of each group" },
  { K_DROP, S_LARGE, IC_GRANDTOTALS, L_PVGRAND, "Grand Totals", ID_PVGRANDDD, 0, CB_INPIVOT, 0, 0, "The Grand Total row and column" },
  { K_DROP, S_LARGE, IC_RPTLAYOUT, L_PVLAYOUT, "Report Layout", ID_PVLAYOUTDD, 0, CB_INPIVOT, 0, 0, "Compact, outline or tabular form" },
  { K_DROP, S_LARGE, IC_BLANKROWS, L_PVBLANK, "Blank Rows", ID_PVBLANKDD, 0, CB_INPIVOT, 0, 0, "A blank line after each item of the outer field" },
};
static const Item pd_opts[] = {
  { K_BUTTON, S_SMALL, 0, 0, "Row Headers", ID_PVROWHEAD, CB_PVROWHEAD, CB_INPIVOT, F_CHECK, 0, "The row labels in bold" },
  { K_BUTTON, S_SMALL, 0, 0, "Column Headers", ID_PVCOLHEAD, CB_PVCOLHEAD, CB_INPIVOT, F_CHECK, 0, "The header rows shaded" },
  { K_BUTTON, S_SMALL, 0, 0, "Banded Rows", ID_PVBANDROWS, CB_PVBANDROWS, CB_INPIVOT, F_CHECK | F_NEWCOL, 0, "Shade every other row" },
  { K_BUTTON, S_SMALL, 0, 0, "Banded Columns", ID_PVBANDCOLS, CB_PVBANDCOLS, CB_INPIVOT, F_CHECK, 0, "Shade every other column" },
};
static const Item pd_styles[] = {
  { K_DROP, S_LARGE, IC_TSTYLES, L_TSTYLES, "PivotTable Styles", ID_PVSTYLEDD, 0, CB_INPIVOT, 0, 0, "The PivotTable's look" },
};
static const Group pdesign[] = { { "Layout", G_COLS, pd_layout, N(pd_layout) }, { "PivotTable Style Options", G_COLS, pd_opts, N(pd_opts) },
                                 { "PivotTable Styles", G_COLS, pd_styles, N(pd_styles) } };
#define TT_CONTEXT_FIRST 8        /* tabs[] from here are contextual */
#undef TT_TOOLS
#endif
