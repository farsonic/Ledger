/*
 * charticons.c - the icons of the chart, drawing and sparkline tools, in theme.c's little vector
 * language (16 x 16: c colour, L line, R box, F filled box, O circle, E disc, P polyline). Ledger's
 * own drawings, like the rest of its icons.
 */
#include "chartids.h"

static const char *const icx[ICX_N - ICX_BASE] = {
  /* COL: clustered columns */ "ci L 1 15 15 15 ca F 2 8 4 14 co F 5 4 7 14 ca F 9 6 11 14 co F 12 2 14 14",
  /* COLSTACK */ "ci L 1 15 15 15 ca F 2 9 5 14 F 7 7 10 14 F 12 5 15 14 co F 2 5 5 8 F 7 2 10 6 F 12 1 15 4",
  /* COL100 */ "ci L 1 15 15 15 ca F 2 8 5 14 F 7 10 10 14 F 12 6 15 14 co F 2 1 5 7 F 7 1 10 9 F 12 1 15 5",
  /* BAR: clustered bars */ "ci L 1 1 1 15 ca F 2 1 9 3 co F 2 4 12 6 ca F 2 9 7 11 co F 2 12 14 14",
  /* BARSTACK */ "ci L 1 1 1 15 ca F 2 2 7 5 F 2 7 9 10 F 2 12 5 15 co F 8 2 12 5 F 10 7 15 10 F 6 12 10 15",
  /* BAR100 */ "ci L 1 1 1 15 ca F 2 2 8 5 F 2 7 6 10 F 2 12 10 15 co F 9 2 15 5 F 7 7 15 10 F 11 12 15 15",
  /* LINE */ "ci L 1 1 1 15 L 1 15 15 15 ca P 4 2 12 6 7 10 9 14 3",
  /* LINEMARK */ "ci L 1 1 1 15 L 1 15 15 15 ca P 4 2 12 6 7 10 9 14 3 F 1 11 3 13 F 5 6 7 8 F 9 8 11 10 F 13 2 15 4",
  /* LINESTACK */ "ci L 1 1 1 15 L 1 15 15 15 ca P 4 2 13 6 11 10 12 14 9 co P 4 2 8 6 4 10 6 14 2",
  /* PIE */ "ca E 8 8 6 co L 8 8 8 2 L 8 8 9 2 L 8 8 10 2 L 8 8 11 3 L 8 8 12 3 L 8 8 13 4 L 8 8 13 5 L 8 8 14 6 L 8 8 14 7 L 8 8 14 8 cw L 8 8 8 2 L 8 8 14 8",
  /* DOUGHNUT */ "ca O 8 8 6 O 8 8 5 O 8 8 4 co P 5 8 2 11 3 13 5 14 8 14 8 P 4 8 3 11 4 12 6 13 8 P 3 8 4 10 5 12 8",
  /* AREA */ "ci L 1 15 15 15 ca L 2 10 2 14 L 3 9 3 14 L 4 8 4 14 L 5 8 5 14 L 6 9 6 14 L 7 10 7 14 L 8 9 8 14 L 9 7 9 14 L 10 6 10 14 L 11 5 11 14 L 12 5 12 14 L 13 4 13 14 L 14 4 14 14",
  /* AREASTACK */ "ci L 1 15 15 15 ca L 2 11 2 14 L 4 10 4 14 L 6 11 6 14 L 8 9 8 14 L 10 8 10 14 L 12 9 12 14 L 14 7 14 14 L 3 10 3 14 L 5 10 5 14 L 7 10 7 14 L 9 8 9 14 L 11 8 11 14 L 13 8 13 14 co P 7 2 10 4 6 6 8 8 5 10 3 12 4 14 2",
  /* AREA100 */ "ci L 1 15 15 15 R 2 1 14 14 ca L 3 9 3 13 L 5 8 5 13 L 7 10 7 13 L 9 7 9 13 L 11 6 11 13 L 13 8 13 13 L 4 8 4 13 L 6 9 6 13 L 8 8 8 13 L 10 6 10 13 L 12 7 12 13",
  /* SCATTER */ "ci L 1 1 1 15 L 1 15 15 15 ca E 4 11 1 E 7 6 1 E 11 9 1 co E 13 3 1 E 5 7 1 E 9 12 1",
  /* SCATTERLINE */ "ci L 1 1 1 15 L 1 15 15 15 ca P 4 3 12 6 8 10 9 14 3 F 2 11 4 13 F 5 7 7 9 F 9 8 11 10 F 13 2 15 4",
  /* BUBBLE */ "ci L 1 1 1 15 L 1 15 15 15 ca O 5 10 3 co O 11 5 3 cg O 12 12 1",
  /* RADAR */ "ci P 6 8 1 15 6 12 15 4 15 1 6 8 1 L 8 8 8 1 L 8 8 15 6 L 8 8 12 15 L 8 8 4 15 L 8 8 1 6 ca P 6 8 4 12 7 10 12 6 11 4 7 8 4",
  /* HISTOGRAM */ "ci L 1 15 15 15 ca F 1 10 3 14 F 4 5 6 14 F 7 2 9 14 F 10 6 12 14 F 13 10 15 14",
  /* WATERFALL */ "ci L 1 15 15 15 ca F 1 9 3 14 F 4 5 6 9 co F 7 5 9 8 ca F 10 2 12 8 cd F 13 2 15 14",
  /* FUNNEL */ "ca F 1 2 14 4 F 3 6 12 8 F 5 10 10 12 F 7 13 8 15",
  /* TREEMAP */ "ca F 1 1 8 14 co F 9 1 14 7 cg F 9 8 11 14 cy F 12 8 14 14",
  /* SUNBURST */ "co O 8 8 7 O 8 8 6 ca E 8 8 4 cw L 8 8 8 1 L 8 8 15 8 L 8 8 3 13",
  /* BOX */ "ci L 4 1 4 4 L 4 11 4 14 L 3 1 5 1 L 3 14 5 14 L 11 2 11 6 L 11 10 11 13 L 10 2 12 2 L 10 13 12 13 ca R 2 4 6 11 L 2 7 6 7 co R 9 6 13 10 L 9 8 13 8",
  /* STOCK */ "ci L 3 2 3 12 L 3 7 5 7 L 8 4 8 14 L 8 10 10 10 L 13 1 13 9 L 13 4 15 4 ca L 1 15 15 15",
  /* COMBO */ "ci L 1 15 15 15 ca F 2 8 4 14 F 6 5 8 14 F 10 9 12 14 co P 4 2 6 6 3 10 7 14 2",
  /* RECOMMENDED: a chart and a star */ "ci L 1 15 15 15 ca F 2 9 4 14 F 6 6 8 14 cy P 6 12 1 13 4 15 4 13 6 14 9 12 7 E 12 5 1",
  /* ALLCHARTS: columns and a pie */ "ci L 1 15 8 15 ca F 2 9 3 14 F 5 6 6 14 co E 12 9 3 cw L 12 9 12 6 L 12 9 15 9",
  /* SPARKLINE: a line in a cell */ "ci R 1 4 14 11 ca P 5 2 9 5 6 8 8 11 5 13 7",
  /* SPARKCOL: columns in a cell */ "ci R 1 4 14 11 ca F 3 8 4 10 F 6 6 7 10 F 9 7 10 10 F 12 5 13 10",
  /* WINLOSS: up and down in a cell */ "ci R 1 4 14 11 cd L 2 8 13 8 ca F 3 5 4 7 F 6 5 7 7 cr F 9 9 10 10 ca F 12 5 13 7",
  /* PEN: a blue pen */ "ci P 5 11 2 14 5 6 13 3 10 11 2 L 3 10 6 13 ca P 4 3 10 2 14 6 13 3 10 L 2 14 1 15",
  /* PENBLACK */ "ci P 5 11 2 14 5 6 13 3 10 11 2 L 3 10 6 13 ct P 4 3 10 2 14 6 13 3 10 L 2 14 1 15",
  /* PENRED */ "ci P 5 11 2 14 5 6 13 3 10 11 2 L 3 10 6 13 cr P 4 3 10 2 14 6 13 3 10 L 2 14 1 15",
  /* HIGHLIGHTER: a broad yellow tip */ "ci P 5 9 1 14 6 8 12 3 7 9 1 L 4 8 8 11 cy F 2 10 5 13 F 1 14 10 15",
  /* ERASER */ "ci P 5 9 2 14 7 7 14 2 9 9 2 L 5 6 10 11 cs F 3 10 5 11 F 4 12 6 13 ci L 6 15 14 15",
  /* LASSO: a dotted loop round shapes */ "ci L 2 3 4 3 L 6 3 8 3 L 10 3 12 3 L 13 4 13 6 L 13 8 13 10 L 2 5 2 7 L 2 9 2 11 L 3 12 5 12 L 7 12 9 12 ca F 5 5 8 8 co E 10 9 1 ci L 11 12 14 15",
  /* INKSHAPE: a squiggle becoming a box */ "ca P 5 1 5 3 2 5 6 7 3 8 6 ci L 8 9 9 9 L 9 8 10 9 L 9 10 10 9 ca R 10 6 15 12",
  /* ICONS: a star and a heart */ "ca P 6 5 1 6 4 9 4 7 6 8 9 5 7 P 4 3 7 5 9 2 8 3 7 co E 10 11 2 E 13 11 2 P 4 8 12 11 15 15 12 8 12",
  /* ADDELEM: a chart and a plus */ "ci L 1 15 12 15 ca F 2 9 4 14 F 6 6 8 14 F 10 10 11 14 cg L 13 1 13 7 L 10 4 15 4",
  /* QUICKLAYOUT: a chart's parts */ "ci R 1 1 14 14 ca F 3 3 12 4 cd F 3 6 9 12 co F 10 6 12 12",
  /* STYLES: a chart and a brush */ "ci L 1 15 10 15 ca F 2 9 4 14 F 6 6 8 14 co F 11 1 14 5 ci L 12 6 12 13 L 13 6 13 13",
  /* SWITCHRC: arrows round a table */ "ci R 4 4 11 11 L 4 7 11 7 L 7 4 7 11 ca P 4 1 10 1 2 3 2 L 1 2 3 0 P 3 13 14 13 13 12 13 L 14 13 12 15",
  /* SELECTDATA: a table and a chart */ "ci R 1 1 8 10 L 1 4 8 4 L 4 1 4 10 ca F 10 10 11 14 F 13 7 14 14 ci L 9 15 15 15",
  /* CHANGETYPE: columns becoming a line */ "ca F 1 9 2 14 F 4 6 5 14 F 7 8 8 14 ci L 1 15 8 15 co P 3 10 12 12 7 15 4 ci L 10 15 15 15",
  /* MOVECHART: a chart and an arrow */ "ci R 1 5 10 14 ca F 3 9 4 12 F 6 7 7 12 cg L 9 1 15 1 L 15 1 15 7 L 15 1 10 6",
  /* FORMATSEL: a box with handles */ "ci R 3 3 12 12 ca F 2 2 4 4 F 11 2 13 4 F 2 11 4 13 F 11 11 13 13 cd F 5 5 10 10",
  /* AXIS: an axis across a sparkline */ "ci R 1 4 14 11 ct L 2 8 13 8 ca P 4 3 9 6 6 9 10 12 6",
  /* CLEARSPARK: a sparkline crossed out */ "ci R 1 4 14 11 ca P 4 3 9 6 6 9 10 12 6 cr L 9 1 15 7 L 15 1 9 7",
  /* SPCOLOR: a pen and a colour bar */ "ci P 5 4 2 12 10 10 12 2 4 4 2 cy F 1 13 14 15",
  /* MARKCOLOR: markers and a colour bar */ "ci P 3 2 9 7 4 13 8 cr E 2 9 1 E 7 4 1 E 13 8 1 ca F 1 13 14 15",
  /* EDITSPARK: a sparkline and a pencil */ "ci R 1 6 9 12 ca P 3 2 10 5 8 8 10 co P 4 9 6 14 1 15 2 10 7",
  /* DELCHART: a chart and a red cross */ "ci L 1 15 10 15 ca F 2 9 4 14 F 6 6 8 14 cr L 10 1 15 6 L 15 1 10 6",
  /* WORDART: a large A with its shadow and a swash */ "cs P 3 4 14 8 3 12 14 L 6 10 10 10 ca P 3 2 12 7 1 11 12 L 4 8 10 8 L 3 12 3 11 L 11 12 11 11 co P 4 1 15 6 13 10 15 15 13",
};
const char *chart_icon_desc(int icon)
{
  return icon >= ICX_BASE && icon < ICX_N ? icx[icon - ICX_BASE] : 0;
}
