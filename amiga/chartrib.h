/*
 * chartrib.h - the ribbon's tools for charts, pictures, shapes, ink and sparklines: the Insert
 * tab's Illustrations, Charts, Sparklines and Text groups, the Draw tab, and the tabs that show
 * while a chart (Chart Design), an object (Format) or a sparkline (Sparkline) is selected.
 * Included by ribbon.c once (it uses ribbon.c's Item, Group, Tab and List); the commands are
 * carried out by charttab.c.
 */
#include "chartids.h"
#include "../core/drawing.h"

/* ------------------------------------------------------------------ the lists (LX_ order) */
/* Insert > WordArt: GridCraft's ten styles (drawing.c's drw_wordart_styles, in that order) */
static const char *const x_wordart[] = { "Classic", "Midnight", "Sunrise", "Lagoon", "Ember", "Mint Outline", "Ink Stamp", "Orchid", "Graphite", "Lime Pop" };
static const char *const x_shapes[] = { "Rectangle", "Rounded Rectangle", "Oval", "Triangle", "Line", "Arrow", "Text Box" };
static const char *const x_colbar[] = { "Clustered Column", "Stacked Column", "100% Stacked Column", "Clustered Bar", "Stacked Bar", "100% Stacked Bar" };
static const char *const x_linearea[] = { "Line", "Line with Markers", "Stacked Line", "Area", "Stacked Area", "100% Stacked Area" };
static const char *const x_pie[] = { "Pie", "Doughnut" };
static const char *const x_scatter[] = { "Scatter", "Scatter with Lines", "Bubble" };
static const char *const x_stat[] = { "Histogram", "Box & Whisker" };
static const char *const x_more[] = { "Treemap", "Sunburst", "Waterfall", "Funnel", "Radar", "Stock", "Combo" };
static const char *const x_addelem[] = { "Chart Title", "No Chart Title", "Axis Titles", "No Axis Titles", "Legend: Right", "Legend: Top",
                                         "Legend: Left", "Legend: Bottom", "No Legend", "Data Labels", "No Data Labels", "Gridlines",
                                         "No Gridlines", "Vertical Gridlines" };
static const char *const x_layouts[] = { "Layout 1: legend right", "Layout 2: legend top, data labels", "Layout 3: legend below",
                                         "Layout 4: legend below, data labels", "Layout 5: axis titles, no legend", "Layout 6: axis titles, legend right" };
static const char *const x_colors[] = { "Colourful", "Monochromatic", "Accent 1 Shades", "Accent 2 Shades", "Accent 3 Shades", "Accent 4 Shades",
                                        "Accent 5 Shades", "Accent 6 Shades" };
static const char *const x_styles[] = { "Style 1: plain", "Style 2: outlined", "Style 3: bold lines", "Style 4: shaded plot area", "Style 5: dark" };
static const char *const x_types[CK_N] = {
  "Clustered Column", "Stacked Column", "100% Stacked Column", "Clustered Bar", "Stacked Bar", "100% Stacked Bar",
  "Line", "Line with Markers", "Stacked Line", "Pie", "Doughnut", "Area", "Stacked Area", "100% Stacked Area",
  "Scatter", "Scatter with Lines", "Bubble", "Radar", "Histogram", "Waterfall", "Funnel", "Treemap", "Sunburst",
  "Box & Whisker", "Stock", "Combo" };
static const char *const x_spaxis[] = { "Show Axis", "Same Minimum for All", "Same Maximum for All", "Each Its Own Minimum and Maximum",
                                        "Custom Minimum...", "Custom Maximum..." };
static const char *const x_align[] = { "Align Left", "Align Centre", "Align Right", "Align Top", "Align Middle", "Align Bottom" };
static const char *const x_spclear[] = { "Clear Selected Sparklines", "Clear Selected Sparkline Groups" };
/* the parts of the selected chart (charttab.c fills them in through ribbon_set_elems) */
static const char *x_elems[40] = { "Chart Area" };
static List xlists[LX_END - LX_BASE] = {
  { x_shapes, N(x_shapes), PL_TEXT, NULL }, { drw_icon_names, DRW_NICONS, PL_TEXT, NULL }, { x_colbar, N(x_colbar), PL_TEXT, NULL },
  { x_linearea, N(x_linearea), PL_TEXT, NULL }, { x_pie, N(x_pie), PL_TEXT, NULL }, { x_scatter, N(x_scatter), PL_TEXT, NULL },
  { x_stat, N(x_stat), PL_TEXT, NULL }, { x_more, N(x_more), PL_TEXT, NULL }, { x_addelem, N(x_addelem), PL_TEXT, NULL },
  { x_layouts, N(x_layouts), PL_TEXT, NULL }, { x_colors, N(x_colors), PL_TEXT, NULL }, { x_styles, N(x_styles), PL_TEXT, NULL },
  { x_types, CK_N, PL_TEXT, NULL }, { x_elems, 1, PL_TEXT, NULL }, { x_spaxis, N(x_spaxis), PL_TEXT, NULL }, { x_spclear, N(x_spclear), PL_TEXT, NULL },
  { x_align, N(x_align), PL_TEXT, NULL }, { x_wordart, N(x_wordart), PL_TEXT, NULL },
};
/* ribbon.c's list_of() made to know these lists too (inside the macro, list_of is still the function) */
#define list_of(k) ((k) >= LX_BASE && (k) < LX_END ? (const List *)&xlists[(k) - LX_BASE] : list_of(k))
/* (charttab.c) the Format tab's list of the selected chart's parts, and the one shown */
static char x_elemshown[48] = "Chart Area";
void ribbon_set_elems(const char *const *labels, int n, int shown)
{
  int i;
  if (n > 40) n = 40;
  for (i = 0; i < n; i++) x_elems[i] = labels[i];
  xlists[LX_ELEMS - LX_BASE].labels = x_elems;
  xlists[LX_ELEMS - LX_BASE].n = n > 0 ? n : 1;
  strncpy(x_elemshown, shown >= 0 && shown < n ? labels[shown] : "", sizeof x_elemshown - 1);
}
static void chartrib_field(UWORD id, char *out, size_t cap)
{
  if (id == ID_X_ELEMDD) { strncpy(out, tr(x_elemshown), cap - 1); out[cap - 1] = 0; }
}

/* ------------------------------------------------------------------ Insert */
static const Item ix_illus[] = {
  { K_BUTTON, S_LARGE, IC_PICTURE, 0, "Pictures", ID_X_PICTURE, 0, 0, 0, 0, "A PNG or JPEG picture over the cells" },
  { K_DROP, S_LARGE, IC_SHAPES, LX_SHAPES, "Shapes", ID_X_SHAPEDD, 0, 0, 0, 0, "A rectangle, oval, line, arrow or text box: drag on the sheet to draw it" },
  { K_DROP, S_LARGE, ICX_ICONS, LX_ICONS, "Icons", ID_X_ICONDD, 0, 0, 0, 0, "A small picture from Ledger's set of icons" },
};
static const Item ix_charts[] = {
  { K_BUTTON, S_LARGE, ICX_RECOMMENDED, 0, "Recommended Charts", ID_X_RECCHART, 0, 0, 0, 0, "Charts that suit the selected data, shown with it" },
  { K_DROP, S_ICON, ICX_COL, LX_COLBAR, NULL, ID_X_COLBARDD, 0, 0, 0, 0, "Column or bar chart" },
  { K_DROP, S_ICON, ICX_LINE, LX_LINEAREA, NULL, ID_X_LINEAREADD, 0, 0, 0, 0, "Line or area chart" },
  { K_DROP, S_ICON, ICX_PIE, LX_PIE, NULL, ID_X_PIEDD, 0, 0, 0, 0, "Pie or doughnut chart" },
  { K_DROP, S_ICON, ICX_SCATTER, LX_SCATTER, NULL, ID_X_SCATTERDD, 0, 0, F_NEWCOL, 0, "Scatter (X, Y) or bubble chart" },
  { K_DROP, S_ICON, ICX_HISTOGRAM, LX_STAT, NULL, ID_X_STATDD, 0, 0, 0, 0, "Statistic chart: histogram, box and whisker" },
  { K_DROP, S_ICON, ICX_TREEMAP, LX_MORE, NULL, ID_X_MOREDD, 0, 0, 0, 0, "Treemap, sunburst, waterfall, funnel, radar, stock or combo chart" },
  { K_BUTTON, S_LARGE, ICX_ALLCHARTS, 0, "All Charts", ID_X_ALLCHARTS, 0, 0, 0, 0, "Every kind of chart, with a preview of the selected data" },
  { K_BUTTON, S_LARGE, ICP_PIVOTCHART, 0, "PivotChart", ID_P2_PIVOTCHART, 0, 0, 0, 0, "A chart of the PivotTable at the active cell, which follows its report" },  /* (pivot2.c) */
};
static const Item ix_spark[] = {
  { K_BUTTON, S_SMALL, ICX_SPARKLINE, 0, "Line", ID_X_SPARKLINE, 0, 0, 0, 0, "A line sparkline: a small chart inside a cell" },
  { K_BUTTON, S_SMALL, ICX_SPARKCOL, 0, "Column", ID_X_SPARKCOL, 0, 0, 0, 0, "A column sparkline inside a cell" },
  { K_BUTTON, S_SMALL, ICX_WINLOSS, 0, "Win/Loss", ID_X_SPARKWIN, 0, 0, 0, 0, "A win/loss sparkline: up for gains, down for losses" },
};
static const Item ix_text[] = {
  { K_BUTTON, S_LARGE, IC_TEXTBOX, 0, "Text Box", ID_X_TEXTBOX, 0, 0, 0, 0, "A box of text over the cells: drag on the sheet to draw it" },
  { K_DROP, S_LARGE, ICX_WORDART, LX_WORDART, "WordArt", ID_X_WORDARTDD, 0, 0, 0, 0, "Large decorative text over the cells, in one of ten styles" },
};
/* ------------------------------------------------------------------ Draw */
static const Item dw_tools[] = {
  { K_BUTTON, S_LARGE, ICX_LASSO, 0, "Select Objects", ID_X_LASSO, RBX(RBX_LASSO), 0, 0, 0, "Select ink and shapes: click them, or drag round them" },
  { K_BUTTON, S_LARGE, ICX_PEN, 0, "Pen Blue", ID_X_PENBLUE, RBX(RBX_PENBLUE), 0, 0, 0, "Draw on the sheet in blue ink" },
  { K_BUTTON, S_LARGE, ICX_PENBLACK, 0, "Pen Black", ID_X_PENBLACK, RBX(RBX_PENBLACK), 0, 0, 0, "Draw on the sheet in black ink" },
  { K_BUTTON, S_LARGE, ICX_PENRED, 0, "Pen Red", ID_X_PENRED, RBX(RBX_PENRED), 0, 0, 0, "Draw on the sheet in red ink" },
  { K_BUTTON, S_LARGE, ICX_HIGHLIGHTER, 0, "Highlighter", ID_X_HIGHLIGHT, RBX(RBX_HIGHLIGHT), 0, 0, 0, "Draw with a broad yellow highlighter" },
  { K_BUTTON, S_LARGE, ICX_ERASER, 0, "Eraser", ID_X_ERASER, RBX(RBX_ERASER), 0, 0, 0, "Rub out ink strokes: click or drag over them" },
};
static const Item dw_convert[] = {
  { K_BUTTON, S_LARGE, ICX_INKSHAPE, 0, "Ink to Shape", ID_X_INKSHAPE, 0, 0, 0, 0, "Turn the selected (or last) ink stroke into the shape it looks like" },
};
/* ------------------------------------------------------------------ Chart Design */
static const Item cd_layouts[] = {
  { K_DROP, S_LARGE, ICX_ADDELEM, LX_ADDELEM, "Add Chart Element", ID_X_ADDELEMDD, 0, 0, 0, 0, "Titles, legend, data labels and gridlines" },
  { K_DROP, S_LARGE, ICX_QUICKLAYOUT, LX_LAYOUTS, "Quick Layout", ID_X_LAYOUTDD, 0, 0, 0, 0, "A ready-made arrangement of the chart's parts" },
};
static const Item cd_styles[] = {
  { K_DROP, S_LARGE, IC_COLORSETS, LX_COLORS, "Change Colours", ID_X_COLORSDD, 0, 0, 0, 0, "The series' colours" },
  { K_DROP, S_LARGE, ICX_STYLES, LX_STYLES, "Chart Styles", ID_X_STYLEDD, 0, 0, 0, 0, "The look of the chart's bars, lines and background" },
};
static const Item cd_data[] = {
  { K_BUTTON, S_LARGE, ICX_SWITCHRC, 0, "Switch Row/Column", ID_X_SWITCHRC, 0, 0, 0, 0, "Series along the rows instead of the columns, or back" },
  { K_BUTTON, S_LARGE, ICX_SELECTDATA, 0, "Select Data", ID_X_SELECTDATA, 0, 0, 0, 0, "The cells the chart shows: its range, series and labels" },
};
static const Item cd_type[] = {
  { K_DROP, S_LARGE, ICX_CHANGETYPE, LX_TYPES, "Change Chart Type", ID_X_TYPEDD, 0, 0, 0, 0, "Another kind of chart for the same data" },
};
static const Item cd_loc[] = {
  { K_BUTTON, S_LARGE, ICX_MOVECHART, 0, "Move Chart", ID_X_MOVECHART, 0, 0, 0, 0, "Move the chart to another sheet" },
  { K_BUTTON, S_SMALL, IC_TEXTBOX, 0, "Title...", ID_X_CHARTTITLE, 0, 0, F_NEWCOL, 0, "Type the chart's title" },
  { K_BUTTON, S_SMALL, ICX_DELCHART, 0, "Delete", ID_X_DELOBJ, 0, 0, 0, 0, "Delete the chart (Del)" },
};
/* ------------------------------------------------------------------ Format (a chart, shape or picture) */
static const Item fm_cur[] = {
  { K_FIELD, S_SMALL, 0, LX_ELEMS, NULL, ID_X_ELEMDD, 0, RBX(RBX_CHART), 0, 112, "Chart elements: the part of the chart to format" },
  { K_BUTTON, S_SMALL, ICX_FORMATSEL, 0, "Format Selection", ID_X_FORMATSEL, 0, 0, F_ROW1, 0, "Colours, size, text and more for the selected object or chart part" },
};
static const Item fm_styles[] = {
  { K_DROP, S_SMALL, IC_SHAPEFILL, L_FILLS, "Shape Fill", ID_X_FILLDD, 0, 0, 0, 0, "The colour inside the selected object or chart part" },
  { K_DROP, S_SMALL, IC_SHAPEOUTLINE, L_COLORS, "Shape Outline", ID_X_OUTLINEDD, 0, 0, 0, 0, "The colour of its outline (of a line, its colour)" },
  { K_BUTTON, S_SMALL, IC_EDITTEXT, 0, "Edit Text", ID_X_EDITTEXT, 0, RBX(RBX_SHAPE), 0, 0, "The text inside the selected shape" },
};
static const Item fm_arrange[] = {
  { K_BUTTON, S_SMALL, IC_FORWARD, 0, "Bring Forward", ID_X_FORWARD, 0, 0, 0, 0, "Bring the object one step to the front" },
  { K_BUTTON, S_SMALL, IC_BACKWARD, 0, "Send Backward", ID_X_BACKWARD, 0, 0, 0, 0, "Send the object one step back" },
  { K_BUTTON, S_SMALL, IC_DELETE, 0, "Delete", ID_X_DELOBJ, 0, 0, 0, 0, "Delete the object (Del)" },
};
/* ------------------------------------------------------------------ Sparkline */
static const Item sp_spark[] = {
  { K_BUTTON, S_LARGE, ICX_EDITSPARK, 0, "Edit Data", ID_X_SPEDIT, 0, 0, 0, 0, "The cells the sparklines show and where they are" },
};
static const Item sp_type[] = {
  { K_BUTTON, S_SMALL, ICX_SPARKLINE, 0, "Line", ID_X_SPTLINE, RBX(RBX_SPLINE), 0, 0, 0, "Show the sparklines as lines" },
  { K_BUTTON, S_SMALL, ICX_SPARKCOL, 0, "Column", ID_X_SPTCOL, RBX(RBX_SPCOL), 0, 0, 0, "Show the sparklines as columns" },
  { K_BUTTON, S_SMALL, ICX_WINLOSS, 0, "Win/Loss", ID_X_SPTWIN, RBX(RBX_SPWIN), 0, 0, 0, "Show the sparklines as wins and losses" },
};
static const Item sp_show[] = {
  { K_BUTTON, S_SMALL, 0, 0, "High Point", ID_X_SPHIGH, RBX(RBX_SPHIGH), 0, F_CHECK, 0, "Mark the highest point" },
  { K_BUTTON, S_SMALL, 0, 0, "Low Point", ID_X_SPLOW, RBX(RBX_SPLOW), 0, F_CHECK, 0, "Mark the lowest point" },
  { K_BUTTON, S_SMALL, 0, 0, "Negative Points", ID_X_SPNEG, RBX(RBX_SPNEG), 0, F_CHECK, 0, "Mark the points below zero" },
  { K_BUTTON, S_SMALL, 0, 0, "First Point", ID_X_SPFIRST, RBX(RBX_SPFIRST), 0, F_CHECK, 0, "Mark the first point" },
  { K_BUTTON, S_SMALL, 0, 0, "Last Point", ID_X_SPLAST, RBX(RBX_SPLAST), 0, F_CHECK, 0, "Mark the last point" },
  { K_BUTTON, S_SMALL, 0, 0, "Markers", ID_X_SPMARK, RBX(RBX_SPMARK), RBX(RBX_SPLINE), F_CHECK, 0, "Mark every point of a line" },
};
static const Item sp_style[] = {
  { K_DROP, S_SMALL, ICX_SPCOLOR, L_COLORS, "Sparkline Colour", ID_X_SPCOLORDD, 0, 0, 0, 0, "The sparklines' colour" },
  { K_DROP, S_SMALL, ICX_MARKCOLOR, L_COLORS, "Marker Colour", ID_X_SPMARKDD, 0, 0, 0, 0, "The colour of the marked points" },
};
static const Item sp_group[] = {
  { K_DROP, S_SMALL, ICX_AXIS, LX_SPAXIS, "Axis", ID_X_SPAXISDD, 0, 0, 0, 0, "The axis and the range of the sparklines' values" },
  { K_DROP, S_SMALL, ICX_CLEARSPARK, LX_SPCLEAR, "Clear", ID_X_SPCLEARDD, 0, 0, 0, 0, "Take the sparklines out" },
};
static const Group x_insert[] = { { "Illustrations", G_COLS, ix_illus, N(ix_illus) }, { "Charts", G_COLS, ix_charts, N(ix_charts) },
                                  { "Sparklines", G_COLS, ix_spark, N(ix_spark) }, { "Text", G_COLS, ix_text, N(ix_text) } };
static const Group x_draw[] = { { "Tools", G_COLS, dw_tools, N(dw_tools) }, { "Convert", G_COLS, dw_convert, N(dw_convert) } };
static const Group x_design[] = { { "Chart Layouts", G_COLS, cd_layouts, N(cd_layouts) }, { "Chart Styles", G_COLS, cd_styles, N(cd_styles) },
                                  { "Data", G_COLS, cd_data, N(cd_data) }, { "Type", G_COLS, cd_type, N(cd_type) }, { "Location", G_COLS, cd_loc, N(cd_loc) } };
static const Group x_format[] = { { "Current Selection", G_ROWS, fm_cur, N(fm_cur) }, { "Shape Styles", G_COLS, fm_styles, N(fm_styles) },
                                  { "Arrange", G_COLS, fm_arrange, N(fm_arrange) } };
static const Group x_spark[] = { { "Sparkline", G_COLS, sp_spark, N(sp_spark) }, { "Type", G_COLS, sp_type, N(sp_type) }, { "Show", G_COLS, sp_show, N(sp_show) },
                                 { "Style", G_COLS, sp_style, N(sp_style) }, { "Group", G_COLS, sp_group, N(sp_group) } };
/* the contextual tabs show only while their kind of thing is selected */
static int x_tab_hidden(const Group *g, ULONG ext)
{
  if (g == x_design) return !(ext & (1UL << RBX_CHART));
  if (g == x_format) return !(ext & (1UL << RBX_OBJECT));
  if (g == x_spark) return !(ext & (1UL << RBX_SPARK));
  return 0;
}
