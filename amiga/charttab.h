/*
 * charttab.h - the commands of charts, pictures, shapes, ink and sparklines (charttab.c), and their
 * menu: main.c sends every id from ID_X_FIRST to ID_X_LAST here and puts CHART_MENUS in its menus.
 */
#ifndef LEDGER_CHARTTAB_H
#define LEDGER_CHARTTAB_H

#include "chartids.h"

void chart_command(ULONG id);
/* the ribbon's extra states (contextual tabs, tools, sparkline options) for MUIP_Ribbon_State.ext */
ULONG chart_ribbon_state(void);

#define XM(t) { NM_TITLE, (STRPTR)(t), 0, 0, 0, 0 }
#define XI(t, id) { NM_ITEM, (STRPTR)(t), 0, 0, 0, (APTR)(id) }
#define XP(t) { NM_ITEM, (STRPTR)(t), 0, 0, 0, 0 }
#define XS(t, id) { NM_SUB, (STRPTR)(t), 0, 0, 0, (APTR)(id) }
#define XBAR { NM_ITEM, NM_BARLABEL, 0, 0, 0, 0 }
#define XKINDS(base) XS("Clustered Column", base + 0), XS("Stacked Column", base + 1), XS("100% Stacked Column", base + 2), XS("Clustered Bar", base + 3), \
  XS("Stacked Bar", base + 4), XS("100% Stacked Bar", base + 5), XS("Line", base + 6), XS("Line with Markers", base + 7), XS("Stacked Line", base + 8), \
  XS("Pie", base + 9), XS("Doughnut", base + 10), XS("Area", base + 11), XS("Stacked Area", base + 12), XS("100% Stacked Area", base + 13), \
  XS("Scatter", base + 14), XS("Scatter with Lines", base + 15), XS("Bubble", base + 16), XS("Radar", base + 17), XS("Histogram", base + 18), \
  XS("Waterfall", base + 19), XS("Funnel", base + 20), XS("Treemap", base + 21), XS("Sunburst", base + 22), XS("Box & Whisker", base + 23), \
  XS("Stock", base + 24), XS("Combo", base + 25)
#define CHART_MENUS \
  XM("Objects"), \
  XI("Picture...", ID_X_PICTURE), \
  XP("Shape"), XS("Rectangle", ID_X_SHAPE0), XS("Rounded Rectangle", ID_X_SHAPE0 + 1), XS("Oval", ID_X_SHAPE0 + 2), XS("Triangle", ID_X_SHAPE0 + 3), \
    XS("Line", ID_X_SHAPE0 + 4), XS("Arrow", ID_X_SHAPE0 + 5), XS("Text Box", ID_X_SHAPE0 + 6), \
  XI("Text Box", ID_X_TEXTBOX), \
  XBAR, \
  XI("Recommended Charts...", ID_X_RECCHART), XI("All Charts...", ID_X_ALLCHARTS), XP("Insert Chart"), XKINDS(ID_X_KIND0), \
  XP("Sparklines"), XS("Line...", ID_X_SPARKLINE), XS("Column...", ID_X_SPARKCOL), XS("Win/Loss...", ID_X_SPARKWIN), \
  XBAR, \
  XP("Draw"), XS("Select Objects", ID_X_LASSO), XS("Pen Blue", ID_X_PENBLUE), XS("Pen Black", ID_X_PENBLACK), XS("Pen Red", ID_X_PENRED), \
    XS("Highlighter", ID_X_HIGHLIGHT), XS("Eraser", ID_X_ERASER), XS("Ink to Shape", ID_X_INKSHAPE), \
  XBAR, \
  XP("Chart"), XS("Chart Title...", ID_X_CHARTTITLE), XS("Switch Row/Column", ID_X_SWITCHRC), XS("Select Data...", ID_X_SELECTDATA), \
    XS("Move Chart...", ID_X_MOVECHART), XS("Format Selection...", ID_X_FORMATSEL), \
  XP("Chart Elements"), XS("No Legend", ID_X_LEGENDNONE), XS("Legend Below", ID_X_LEGENDB), XS("Legend Above", ID_X_LEGENDT), \
    XS("Legend Left", ID_X_LEGENDL), XS("Legend Right", ID_X_LEGENDR), XS("Data Labels On or Off", ID_X_LABELS), XS("Gridlines On or Off", ID_X_GRIDLINES), \
    XS("Axis Titles On or Off", ID_X_AXISTITLES), \
  XP("Chart Type"), XKINDS(ID_X_TYPE0), \
  XP("Quick Layout"), XS("Layout 1", ID_X_LAYOUT1), XS("Layout 2", ID_X_LAYOUT1 + 1), XS("Layout 3", ID_X_LAYOUT1 + 2), XS("Layout 4", ID_X_LAYOUT1 + 3), \
    XS("Layout 5", ID_X_LAYOUT1 + 4), XS("Layout 6", ID_X_LAYOUT1 + 5), \
  XP("Chart Colours"), XS("Colourful", ID_X_COLOR0), XS("Monochromatic", ID_X_COLOR0 + 1), XS("Accent 1 Shades", ID_X_COLOR0 + 2), \
    XS("Accent 2 Shades", ID_X_COLOR0 + 3), XS("Accent 3 Shades", ID_X_COLOR0 + 4), XS("Accent 4 Shades", ID_X_COLOR0 + 5), \
    XS("Accent 5 Shades", ID_X_COLOR0 + 6), XS("Accent 6 Shades", ID_X_COLOR0 + 7), \
  XP("Chart Style"), XS("Style 1: plain", ID_X_STYLE1), XS("Style 2: outlined", ID_X_STYLE1 + 1), XS("Style 3: bold lines", ID_X_STYLE1 + 2), \
    XS("Style 4: shaded plot area", ID_X_STYLE1 + 3), XS("Style 5: dark", ID_X_STYLE1 + 4), \
  XBAR, \
  XI("Edit Shape Text...", ID_X_EDITTEXT), XI("Bring Forward", ID_X_FORWARD), XI("Send Backward", ID_X_BACKWARD), XI("Delete Object", ID_X_DELOBJ), \
  XBAR, \
  XP("Sparkline"), XS("Edit Data...", ID_X_SPEDIT), XS("Line", ID_X_SPTLINE), XS("Column", ID_X_SPTCOL), XS("Win/Loss", ID_X_SPTWIN), \
    XS("High Point", ID_X_SPHIGH), XS("Low Point", ID_X_SPLOW), XS("Negative Points", ID_X_SPNEG), XS("First Point", ID_X_SPFIRST), \
    XS("Last Point", ID_X_SPLAST), XS("Markers", ID_X_SPMARK), XS("Clear Sparklines", ID_X_SPCLEAR)

#endif
