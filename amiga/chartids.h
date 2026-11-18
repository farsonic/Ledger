/*
 * chartids.h - the commands, icons, lists and ribbon states of charts, pictures, shapes, ink and
 * sparklines (charttab.c, chartview.c, chartrib.h). Kept apart from ids.h and theme.h: the ids
 * are 3500-3999 (main.c's loop sends them to chart_command), the icons start at ICX_BASE (theme.c
 * asks chart_icon_desc for them), the lists at LX_BASE (ribbon.c finds them in xlists[]).
 */
#ifndef LEDGER_CHARTIDS_H
#define LEDGER_CHARTIDS_H

enum {
  ID_X_FIRST = 3500,
  /* Insert */
  ID_X_PICTURE = ID_X_FIRST, ID_X_SHAPEDD, ID_X_ICONDD, ID_X_RECCHART, ID_X_ALLCHARTS, ID_X_COLBARDD, ID_X_LINEAREADD, ID_X_PIEDD,
  ID_X_SCATTERDD, ID_X_STATDD, ID_X_MOREDD, ID_X_SPARKLINE, ID_X_SPARKCOL, ID_X_SPARKWIN, ID_X_TEXTBOX,
  /* Draw */
  ID_X_LASSO, ID_X_PENBLUE, ID_X_PENBLACK, ID_X_PENRED, ID_X_HIGHLIGHT, ID_X_ERASER, ID_X_INKSHAPE,
  /* Chart Design */
  ID_X_ADDELEMDD, ID_X_LAYOUTDD, ID_X_COLORSDD, ID_X_STYLEDD, ID_X_SWITCHRC, ID_X_SELECTDATA, ID_X_TYPEDD, ID_X_MOVECHART,
  ID_X_CHARTTITLE, ID_X_DELOBJ,
  /* Format */
  ID_X_ELEMDD, ID_X_FORMATSEL, ID_X_FILLDD, ID_X_OUTLINEDD, ID_X_FORWARD, ID_X_BACKWARD, ID_X_EDITTEXT,
  /* Sparkline */
  ID_X_SPTLINE, ID_X_SPTCOL, ID_X_SPTWIN, ID_X_SPHIGH, ID_X_SPLOW, ID_X_SPNEG, ID_X_SPFIRST, ID_X_SPLAST, ID_X_SPMARK,
  ID_X_SPCOLORDD, ID_X_SPMARKDD, ID_X_SPAXISDD, ID_X_SPCLEARDD, ID_X_SPEDIT,
  /* menu-only forms of list entries */
  ID_X_SHAPE0, ID_X_SHAPE6 = ID_X_SHAPE0 + 6,
  ID_X_KIND0, ID_X_KINDN = ID_X_KIND0 + 25,         /* insert a chart of kind k */
  ID_X_LAYOUT1, ID_X_LAYOUT6 = ID_X_LAYOUT1 + 5,
  ID_X_LEGENDNONE, ID_X_LEGENDB, ID_X_LEGENDT, ID_X_LEGENDL, ID_X_LEGENDR, ID_X_LABELS, ID_X_GRIDLINES, ID_X_AXISTITLES,
  ID_X_COLOR0, ID_X_COLOR7 = ID_X_COLOR0 + 7,
  ID_X_STYLE1, ID_X_STYLE8 = ID_X_STYLE1 + 7,
  ID_X_TYPE0, ID_X_TYPEN = ID_X_TYPE0 + 25,         /* change the chart to kind k */
  ID_X_BACKGROUND, ID_X_SELPANE, ID_X_ALIGNDD, ID_X_ROTATE, ID_X_ALIGN0, ID_X_ALIGN5 = ID_X_ALIGN0 + 5,     /* Page Layout: Background, Arrange */
  ID_X_SPCLEAR,                                     /* (the menu) the selected sparklines cleared */
  ID_X_OBJSEL,                                      /* (the grid: an object was selected) */
  ID_X_WORDARTDD, ID_X_WORDART0, ID_X_WORDART9 = ID_X_WORDART0 + 9,   /* Insert > WordArt: a style's WordArt */
  ID_X_LAST
};

/* the ribbon's extra states (MUIP_Ribbon_State.ext): Item.st/en values 96 + RBX_ */
enum {
  RBX_CHART, RBX_SHAPE, RBX_PICTURE, RBX_SPARK, RBX_OBJECT,
  RBX_LASSO, RBX_PENBLUE, RBX_PENBLACK, RBX_PENRED, RBX_HIGHLIGHT, RBX_ERASER,
  RBX_SPLINE, RBX_SPCOL, RBX_SPWIN, RBX_SPHIGH, RBX_SPLOW, RBX_SPNEG, RBX_SPFIRST, RBX_SPLAST, RBX_SPMARK, RBX_INK
};
#define RBX(b) (96 + (b))      /* (above layout.h's LXB_ bits) */

/* icons (theme_icon draws them from chart_icon_desc's descriptions) */
#define ICX_BASE 512
enum {
  ICX_COL = ICX_BASE, ICX_COLSTACK, ICX_COL100, ICX_BAR, ICX_BARSTACK, ICX_BAR100, ICX_LINE, ICX_LINEMARK, ICX_LINESTACK,
  ICX_PIE, ICX_DOUGHNUT, ICX_AREA, ICX_AREASTACK, ICX_AREA100, ICX_SCATTER, ICX_SCATTERLINE, ICX_BUBBLE, ICX_RADAR,
  ICX_HISTOGRAM, ICX_WATERFALL, ICX_FUNNEL, ICX_TREEMAP, ICX_SUNBURST, ICX_BOX, ICX_STOCK, ICX_COMBO,
  ICX_RECOMMENDED, ICX_ALLCHARTS, ICX_SPARKLINE, ICX_SPARKCOL, ICX_WINLOSS, ICX_PEN, ICX_PENBLACK, ICX_PENRED, ICX_HIGHLIGHTER,
  ICX_ERASER, ICX_LASSO, ICX_INKSHAPE, ICX_ICONS, ICX_ADDELEM, ICX_QUICKLAYOUT, ICX_STYLES, ICX_SWITCHRC, ICX_SELECTDATA,
  ICX_CHANGETYPE, ICX_MOVECHART, ICX_FORMATSEL, ICX_AXIS, ICX_CLEARSPARK, ICX_SPCOLOR, ICX_MARKCOLOR, ICX_EDITSPARK, ICX_DELCHART,
  ICX_WORDART, ICX_N
};
const char *chart_icon_desc(int icon);     /* (charticons.c) NULL if not one of these */

/* lists (ribbon.c's Item.list) */
#define LX_BASE 200
enum { LX_SHAPES = LX_BASE, LX_ICONS, LX_COLBAR, LX_LINEAREA, LX_PIE, LX_SCATTER, LX_STAT, LX_MORE, LX_ADDELEM, LX_LAYOUTS,
       LX_COLORS, LX_STYLES, LX_TYPES, LX_ELEMS, LX_SPAXIS, LX_SPCLEAR, LX_ALIGN, LX_WORDART, LX_END };

#endif
