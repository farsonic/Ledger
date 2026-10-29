/*
 * xlsx_draw.c - see xlsx_draw.h. Written for Ledger from ECMA-376 (DrawingML charts and
 * spreadsheet drawings), Microsoft's [MS-XLSX] notes on x14 sparklines and chartex, and what Excel
 * and LibreOffice write.
 */
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>
#include "xlsx_draw.h"
#include "drawing.h"
#include "xml.h"
#include "image.h"
#include "ops.h"
#include "tablexml.h"
#include "slicer.h"

#define EMU 9525L                /* EMUs to a pixel at 96 dpi */
#define FILLRGB(c) (FILL_ON | ((c) & 0xFFFFFF))
static const u32 accents[6] = { 0x4472C4, 0xED7D31, 0xA5A5A5, 0xFFC000, 0x5B9BD5, 0x70AD47 };

static void xesc(Buf *o, const char *s) { xml_escape(o, s ? s : "", s ? strlen(s) : 0); }
char xd_book_name[128] = "";
static int is_ex(int k) { return k == CK_HISTOGRAM || k == CK_WATERFALL || k == CK_FUNNEL || k == CK_TREEMAP || k == CK_SUNBURST || k == CK_BOXWHISKER; }
static int count_kind(Book *b, int si, int ex, int pics)
{
  int i, n = 0;
  if (si >= b->nsh) si = b->nsh;
  for (i = 0; i < si; i++) {
    Sheet *s = b->sh[i];
    int k;
    for (k = 0; k < drw_count(s); k++) {
      DObj *o = drw_obj(s, k);
      if (pics) n += o->kind == OB_PICTURE && o->pic;
      else n += o->kind == OB_CHART && o->chart && is_ex(o->chart->kind) == ex;
    }
  }
  return n;
}
int xd_has_drawing(Book *b, int si) { return si >= 0 && si < b->nsh && drw_count(b->sh[si]) > 0; }
static int has_sparks(Book *b, int si) { Sheet *s = b->sh[si]; return s->drw && s->drw->ng > 0; }
static int has_bg(Book *b, int si) { Sheet *s = b->sh[si]; return s->drw && s->drw->bg; }
int xd_has_rels(Book *b, int si) { return xd_has_drawing(b, si) || has_bg(b, si); }

/* ------------------------------------------------------------------ saving: package bits */
void xd_content_types(Book *b, Buf *o)
{
  int i, k, png = 0, jpg = 0;
  for (i = 0; i < b->nsh; i++) {
    Sheet *s = b->sh[i];
    int nc = 0, nx = 0;
    if (has_bg(b, i)) { if (s->drw->bgtype == PIC_JPEG) jpg = 1; else png = 1; }
    if (!xd_has_drawing(b, i)) continue;
    buf_printf(o, "<Override PartName=\"/xl/drawings/drawing%d.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.drawing+xml\"/>", i + 1);
    for (k = 0; k < drw_count(s); k++) {
      DObj *ob = drw_obj(s, k);
      if (ob->kind == OB_CHART && ob->chart) {
        if (is_ex(ob->chart->kind)) buf_printf(o, "<Override PartName=\"/xl/charts/chartEx%d.xml\" ContentType=\"application/vnd.ms-office.chartex+xml\"/>", count_kind(b, i, 1, 0) + ++nx);
        else buf_printf(o, "<Override PartName=\"/xl/charts/chart%d.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.drawingml.chart+xml\"/>", count_kind(b, i, 0, 0) + ++nc);
      }
      if (ob->kind == OB_PICTURE && ob->pic) { if (ob->pictype == PIC_JPEG) jpg = 1; else png = 1; }
    }
  }
  if (png) buf_adds(o, "<Default Extension=\"png\" ContentType=\"image/png\"/>");
  if (jpg) buf_adds(o, "<Default Extension=\"jpeg\" ContentType=\"image/jpeg\"/>");
}
void xd_sheet_drawing_ref(Book *b, int si, Buf *o) { if (xd_has_drawing(b, si)) buf_adds(o, "<drawing r:id=\"rIdD1\"/>"); }
void xd_sheet_rels(Book *b, int si, Buf *o)
{
  if (has_bg(b, si))
    buf_printf(o, "<Relationship Id=\"rIdB1\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/image\" Target=\"../media/background%d.%s\"/>",
               si + 1, b->sh[si]->drw->bgtype == PIC_JPEG ? "jpeg" : "png");
  if (xd_has_drawing(b, si))
    buf_printf(o, "<Relationship Id=\"rIdD1\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/drawing\" Target=\"../drawings/drawing%d.xml\"/>", si + 1);
}
static void argb(Buf *o, const char *tag, u32 c) { buf_printf(o, "<x14:%s rgb=\"FF%06lX\"/>", tag, (unsigned long)(c & 0xFFFFFF)); }
/* the sheet's background picture (before tableParts and extLst in the sheet's XML) */
void xd_sheet_picture(Book *b, int si, Buf *o) { if (has_bg(b, si)) buf_adds(o, "<picture r:id=\"rIdB1\"/>"); }
void xd_sheet_ext(Book *b, int si, Buf *o)
{
  Sheet *s = b->sh[si];
  int i, k;
  if (!has_sparks(b, si)) {
    if (tablexml_has_sheet_ext(b, si)) { buf_adds(o, "<extLst>"); tablexml_sheet_ext(b, si, o); buf_adds(o, "</extLst>"); }
    return;
  }
  buf_adds(o, "<extLst><ext uri=\"{05C60535-1F16-4fd2-B633-F4F36F0B64E0}\" xmlns:x14=\"http://schemas.microsoft.com/office/spreadsheetml/2009/9/main\">"
              "<x14:sparklineGroups xmlns:xm=\"http://schemas.microsoft.com/office/excel/2006/main\">");
  for (i = 0; i < s->drw->ng; i++) {
    SparkGroup *g = &s->drw->g[i];
    static const char *const axt[3] = { "individual", "group", "custom" };
    buf_adds(o, "<x14:sparklineGroup");
    buf_printf(o, " lineWeight=\"%g\"", g->weight ? g->weight / 100.0 : 0.75);
    if (g->type == SP_COLUMN) buf_adds(o, " type=\"column\"");
    else if (g->type == SP_WINLOSS) buf_adds(o, " type=\"stacked\"");
    if (g->blanks) buf_printf(o, " displayEmptyCellsAs=\"%s\"", g->blanks == 1 ? "zero" : "span");
    if (g->markers) buf_adds(o, " markers=\"1\"");
    if (g->high) buf_adds(o, " high=\"1\"");
    if (g->low) buf_adds(o, " low=\"1\"");
    if (g->first) buf_adds(o, " first=\"1\"");
    if (g->last) buf_adds(o, " last=\"1\"");
    if (g->negative) buf_adds(o, " negative=\"1\"");
    if (g->axis) buf_adds(o, " displayXAxis=\"1\"");
    if (g->minmode == 2) buf_printf(o, " manualMin=\"%.15g\"", g->minv);
    if (g->maxmode == 2) buf_printf(o, " manualMax=\"%.15g\"", g->maxv);
    if (g->minmode) buf_printf(o, " minAxisType=\"%s\"", axt[g->minmode]);
    if (g->maxmode) buf_printf(o, " maxAxisType=\"%s\"", axt[g->maxmode]);
    buf_adds(o, ">");
    argb(o, "colorSeries", g->color); argb(o, "colorNegative", g->negcolor); argb(o, "colorAxis", 0);
    argb(o, "colorMarkers", g->markcolor); argb(o, "colorFirst", g->firstcolor); argb(o, "colorLast", g->lastcolor);
    argb(o, "colorHigh", g->highcolor); argb(o, "colorLow", g->lowcolor);
    buf_adds(o, "<x14:sparklines>");
    for (k = 0; k < g->n; k++) {
      Buf t;
      char ref[16];
      buf_init(&t);
      if (g->sp[k].f) formula_text(b, -1, g->sp[k].f, &t);
      ref_text(g->sp[k].r, g->sp[k].c, 0, 0, ref);
      buf_adds(o, "<x14:sparkline><xm:f>"); xesc(o, t.data); buf_printf(o, "</xm:f><xm:sqref>%s</xm:sqref></x14:sparkline>", ref);
      buf_free(&t);
    }
    buf_adds(o, "</x14:sparklines></x14:sparklineGroup>");
  }
  buf_adds(o, "</x14:sparklineGroups></ext>");
  tablexml_sheet_ext(b, si, o);    /* slicers */
  buf_adds(o, "</extLst>");
}

/* ------------------------------------------------------------------ saving: charts */
static void solid(Buf *o, u32 rgb) { buf_printf(o, "<a:solidFill><a:srgbClr val=\"%06lX\"/></a:solidFill>", (unsigned long)(rgb & 0xFFFFFF)); }
/* <c:spPr> with a fill and an outline (0: none written, so the default) */
static void sppr(Buf *o, const char *ns, u32 fill, u32 line, int lw)
{
  if (!fill && !line) return;
  buf_printf(o, "<%s:spPr>", ns);
  if (fill) solid(o, fill);
  if (line) { buf_printf(o, "<a:ln w=\"%d\">", lw > 0 ? lw : 9525); solid(o, line); buf_adds(o, "</a:ln>"); }
  buf_printf(o, "</%s:spPr>", ns);
}
static void rich(Buf *o, const char *s, int size, int bold)
{
  buf_adds(o, "<c:tx><c:rich><a:bodyPr/><a:lstStyle/><a:p><a:pPr><a:defRPr");
  if (size) buf_printf(o, " sz=\"%d\"", size);
  buf_printf(o, " b=\"%d\"/></a:pPr><a:r><a:rPr lang=\"en-GB\"", bold);
  if (size) buf_printf(o, " sz=\"%d\"", size);
  buf_printf(o, " b=\"%d\"/><a:t>", bold);
  xesc(o, s);
  buf_adds(o, "</a:t></a:r></a:p></c:rich></c:tx>");
}
static void ftext(Book *b, const Formula *f, Buf *o)
{
  Buf t;
  buf_init(&t);
  formula_text(b, -1, f, &t);
  xesc(o, t.data);
  buf_free(&t);
}
static void num_cache(Buf *o, const double *v, int n, const char *fmt)
{
  int i;
  buf_adds(o, "<c:numCache><c:formatCode>"); xesc(o, fmt && fmt[0] ? fmt : "General");
  buf_printf(o, "</c:formatCode><c:ptCount val=\"%d\"/>", n);
  for (i = 0; i < n; i++) if (!isnan(v[i])) buf_printf(o, "<c:pt idx=\"%d\"><c:v>%.15g</c:v></c:pt>", i, v[i]);
  buf_adds(o, "</c:numCache>");
}
static void str_cache(Buf *o, char **s, int n)
{
  int i;
  buf_printf(o, "<c:strCache><c:ptCount val=\"%d\"/>", n);
  for (i = 0; i < n; i++) { buf_printf(o, "<c:pt idx=\"%d\"><c:v>", i); xesc(o, s[i]); buf_adds(o, "</c:v></c:pt>"); }
  buf_adds(o, "</c:strCache>");
}
static int family(int k)
{
  if (k == CK_COL || k == CK_COLSTACK || k == CK_COL100 || k == CK_BAR || k == CK_BARSTACK || k == CK_BAR100 || k == CK_HISTOGRAM || k == CK_WATERFALL) return 1;
  if (k == CK_LINE || k == CK_LINEMARK || k == CK_LINESTACK) return 2;
  if (k == CK_AREA || k == CK_AREASTACK || k == CK_AREA100) return 3;
  return 0;
}
static int sk_of(const Chart *c, int k)
{
  int s = c->s[k].kind;
  if (c->kind != CK_COMBO) return c->kind;
  if (s != CK_SAME && s < CK_N) return s;
  return k == 0 || c->ns == 1 ? CK_COL : CK_LINE;
}
static void dlbls(Buf *o, int on, int pct)
{
  buf_printf(o, "<c:dLbls><c:showLegendKey val=\"0\"/><c:showVal val=\"%d\"/><c:showCatName val=\"0\"/><c:showSerName val=\"0\"/><c:showPercent val=\"%d\"/><c:showBubbleSize val=\"0\"/></c:dLbls>", on && !pct, pct);
}
/* one series of a chart group of kind k */
static void w_series(Book *b, const Chart *c, const CData *d, int k, int kind, Buf *o)
{
  const CSeries *s = &c->s[k];
  const CDSeries *ds = k < d->ns ? &d->s[k] : NULL;
  int fam = family(kind), i;
  u32 rgb = ds ? ds->rgb : accents[k % 6];
  buf_printf(o, "<c:ser><c:idx val=\"%d\"/><c:order val=\"%d\"/>", k, k);
  if (s->name) { buf_adds(o, "<c:tx><c:strRef><c:f>"); ftext(b, s->name, o); buf_adds(o, "</c:f>"); if (ds) str_cache(o, (char **)&ds->name, 1); buf_adds(o, "</c:strRef></c:tx>"); }
  else if (s->lit) { buf_adds(o, "<c:tx><c:v>"); xesc(o, s->lit); buf_adds(o, "</c:v></c:tx>"); }
  /* its look: written out, so other programs show Ledger's colours */
  if (fam == 2 || kind == CK_RADAR || kind == CK_SCATTERLINE || kind == CK_SCATTER) {
    buf_adds(o, "<c:spPr>");
    if (kind == CK_SCATTER) buf_adds(o, "<a:ln w=\"25400\" cap=\"rnd\"><a:noFill/><a:round/></a:ln>");
    else { buf_printf(o, "<a:ln w=\"%d\" cap=\"rnd\">", c->style == 3 ? 38100 : 28575); solid(o, s->line ? s->line : rgb); buf_adds(o, "<a:round/></a:ln>"); }
    buf_adds(o, "</c:spPr>");
    if (kind == CK_LINEMARK || kind == CK_SCATTER || kind == CK_SCATTERLINE || kind == CK_RADAR) {
      static const char *const sym[4] = { "diamond", "square", "triangle", "circle" };
      if (s->nomarker) buf_adds(o, "<c:marker><c:symbol val=\"none\"/></c:marker>");
      else { buf_printf(o, "<c:marker><c:symbol val=\"%s\"/><c:size val=\"5\"/><c:spPr>", sym[k % 4]); solid(o, rgb); buf_adds(o, "<a:ln w=\"9525\">"); solid(o, rgb); buf_adds(o, "</a:ln></c:spPr></c:marker>"); }
    } else if (fam == 2 || kind == CK_STOCK) buf_adds(o, "<c:marker><c:symbol val=\"none\"/></c:marker>");
  } else if (kind == CK_STOCK) {
    buf_adds(o, "<c:spPr><a:ln w=\"19050\"><a:noFill/></a:ln></c:spPr><c:marker><c:symbol val=\"none\"/></c:marker>");
  } else if (kind == CK_PIE || kind == CK_DOUGHNUT) {
    buf_adds(o, "<c:spPr><a:ln w=\"19050\">"); solid(o, 0xFFFFFF); buf_adds(o, "</a:ln></c:spPr>");
  } else {
    buf_adds(o, "<c:spPr>"); solid(o, rgb);
    if (s->line) { buf_adds(o, "<a:ln>"); solid(o, s->line); buf_adds(o, "</a:ln>"); }
    buf_adds(o, "</c:spPr>");
    if (fam == 1 || kind == CK_BUBBLE) buf_adds(o, "<c:invertIfNegative val=\"0\"/>");
  }
  if ((kind == CK_PIE || kind == CK_DOUGHNUT) && ds) {
    /* each slice's colour */
    for (i = 0; i < ds->n; i++) {
      Chart t = *c;
      t.ns = 0;                  /* (the point colours come from the palette, as Ledger draws them) */
      buf_printf(o, "<c:dPt><c:idx val=\"%d\"/><c:bubble3D val=\"0\"/><c:spPr>", i);
      solid(o, chart_series_rgb(&t, i, ds->n));
      buf_adds(o, "<a:ln w=\"19050\">"); solid(o, 0xFFFFFF); buf_adds(o, "</a:ln></c:spPr></c:dPt>");
    }
  }
  if (kind == CK_SCATTER || kind == CK_SCATTERLINE || kind == CK_BUBBLE) {
    if (s->cat) { buf_adds(o, "<c:xVal><c:numRef><c:f>"); ftext(b, s->cat, o); buf_adds(o, "</c:f>"); if (ds && ds->x) num_cache(o, ds->x, ds->n, NULL); buf_adds(o, "</c:numRef></c:xVal>"); }
    if (s->val) { buf_adds(o, "<c:yVal><c:numRef><c:f>"); ftext(b, s->val, o); buf_adds(o, "</c:f>"); if (ds) num_cache(o, ds->v, ds->n, d->fmt); buf_adds(o, "</c:numRef></c:yVal>"); }
    if (kind == CK_BUBBLE) {
      if (s->size || s->val) { buf_adds(o, "<c:bubbleSize><c:numRef><c:f>"); ftext(b, s->size ? s->size : s->val, o); buf_adds(o, "</c:f>"); if (ds && ds->size) num_cache(o, ds->size, ds->n, NULL); buf_adds(o, "</c:numRef></c:bubbleSize>"); }
      buf_adds(o, "<c:bubble3D val=\"0\"/>");
    } else buf_printf(o, "<c:smooth val=\"%d\"/>", s->smooth);
  } else {
    if (s->cat) { buf_adds(o, "<c:cat><c:strRef><c:f>"); ftext(b, s->cat, o); buf_adds(o, "</c:f>"); if (d->ncat) str_cache(o, d->cat, d->ncat < (ds ? ds->n : 0) ? d->ncat : (ds ? ds->n : 0)); buf_adds(o, "</c:strRef></c:cat>"); }
    if (s->val) {
      buf_adds(o, "<c:val>");
      if (s->val->n == 1 && s->val->t[0].op == T_ARRAY) {
        /* values given in the file as numbers rather than cells */
        buf_adds(o, "<c:numLit><c:formatCode>General</c:formatCode>");
        if (ds) { buf_printf(o, "<c:ptCount val=\"%d\"/>", ds->n); for (i = 0; i < ds->n; i++) if (!isnan(ds->v[i])) buf_printf(o, "<c:pt idx=\"%d\"><c:v>%.15g</c:v></c:pt>", i, ds->v[i]); }
        buf_adds(o, "</c:numLit>");
      } else { buf_adds(o, "<c:numRef><c:f>"); ftext(b, s->val, o); buf_adds(o, "</c:f>"); if (ds) num_cache(o, ds->v, ds->n, d->fmt); buf_adds(o, "</c:numRef>"); }
      buf_adds(o, "</c:val>");
    }
    if (fam == 2) buf_printf(o, "<c:smooth val=\"%d\"/>", s->smooth);
  }
  buf_adds(o, "</c:ser>");
}
static void w_axis(Buf *o, int cat, int id, int cross, const char *pos, int grid, const char *title, const CAxis *ax, int del, int crossmax)
{
  buf_printf(o, "<c:%s><c:axId val=\"%d\"/><c:scaling><c:orientation val=\"minMax\"/>", cat ? "catAx" : "valAx", id);
  if (ax && ax->maxset) buf_printf(o, "<c:max val=\"%.15g\"/>", ax->max);
  if (ax && ax->minset) buf_printf(o, "<c:min val=\"%.15g\"/>", ax->min);
  buf_printf(o, "</c:scaling><c:delete val=\"%d\"/><c:axPos val=\"%s\"/>", del, pos);
  if (grid) buf_adds(o, "<c:majorGridlines><c:spPr><a:ln w=\"9525\"><a:solidFill><a:srgbClr val=\"D9D9D9\"/></a:solidFill></a:ln></c:spPr></c:majorGridlines>");
  if (title && *title) { buf_adds(o, "<c:title>"); rich(o, title, 1000, 1); buf_adds(o, "<c:overlay val=\"0\"/></c:title>"); }
  buf_adds(o, "<c:numFmt formatCode=\"General\" sourceLinked=\"1\"/><c:majorTickMark val=\"none\"/><c:minorTickMark val=\"none\"/><c:tickLblPos val=\"nextTo\"/>");
  buf_adds(o, "<c:spPr><a:ln w=\"9525\"><a:solidFill><a:srgbClr val=\"BFBFBF\"/></a:solidFill></a:ln></c:spPr>");
  buf_printf(o, "<c:crossAx val=\"%d\"/><c:crosses val=\"%s\"/>", cross, crossmax ? "max" : "autoZero");
  if (cat) buf_adds(o, "<c:auto val=\"1\"/><c:lblAlgn val=\"ctr\"/><c:lblOffset val=\"100\"/><c:noMultiLvlLbl val=\"0\"/>");
  else { buf_adds(o, "<c:crossBetween val=\"between\"/>"); if (ax && ax->majset) buf_printf(o, "<c:majorUnit val=\"%.15g\"/>", ax->major); }
  buf_printf(o, "</c:%s>", cat ? "catAx" : "valAx");
}
static void w_chart(Book *b, int si, const Chart *c, Buf *o)
{
  CData d;
  int k, g, kind = c->kind, horiz = kind == CK_BAR || kind == CK_BARSTACK || kind == CK_BAR100, any2 = 0;
  static const char *const lgpos[5] = { "", "b", "t", "l", "r" };
  chart_resolve(b, si, c, &d);
  buf_adds(o, "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
              "<c:chartSpace xmlns:c=\"http://schemas.openxmlformats.org/drawingml/2006/chart\" xmlns:a=\"http://schemas.openxmlformats.org/drawingml/2006/main\" "
              "xmlns:r=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships\"><c:roundedCorners val=\"0\"/>");
  buf_printf(o, "<c:style val=\"%d\"/>", c->style >= 1 && c->style <= 48 ? c->style + 1 : 2);
  if (c->pivot) {
    /* a PivotChart: Excel binds it to the pivot table when the link names the workbook */
    buf_printf(o, "<c:pivotSource><c:name>[%s]", xd_book_name[0] ? xd_book_name : "Book1.xlsx");
    xesc(o, c->pivot);
    buf_adds(o, "</c:name><c:fmtId val=\"0\"/></c:pivotSource>");
  }
  buf_adds(o, "<c:chart>");
  if (c->has_title) {
    buf_adds(o, "<c:title>");
    if (c->title && *c->title) rich(o, c->title, 1400, 0);
    buf_adds(o, "<c:overlay val=\"0\"/>");
    sppr(o, "c", c->efill[CE_TITLE], c->eline[CE_TITLE], 0);
    buf_adds(o, "</c:title><c:autoTitleDeleted val=\"0\"/>");
  } else buf_adds(o, "<c:autoTitleDeleted val=\"1\"/>");
  buf_adds(o, "<c:plotArea><c:layout/>");
  /* one chart group a kind (combo charts: columns, lines, areas, on either axis) */
  for (g = 0; g < 6; g++) {
    int fam = g % 3 + 1, sec = g >= 3, gk = -1, first = 1;
    if (kind != CK_COMBO) { if (g) break; gk = kind; }
    else {
      for (k = 0; k < c->ns; k++) if (family(sk_of(c, k)) == fam && (c->s[k].secondary != 0) == sec) { gk = sk_of(c, k); break; }
      if (gk < 0) continue;
      if (sec) any2 = 1;
    }
    switch (gk) {
    case CK_COL: case CK_COLSTACK: case CK_COL100: case CK_BAR: case CK_BARSTACK: case CK_BAR100: case CK_HISTOGRAM: case CK_WATERFALL:
      buf_printf(o, "<c:barChart><c:barDir val=\"%s\"/><c:grouping val=\"%s\"/><c:varyColors val=\"0\"/>", horiz && kind != CK_COMBO ? "bar" : "col",
                 gk == CK_COLSTACK || gk == CK_BARSTACK ? "stacked" : gk == CK_COL100 || gk == CK_BAR100 ? "percentStacked" : "clustered");
      break;
    case CK_LINE: case CK_LINEMARK: case CK_LINESTACK:
      buf_printf(o, "<c:lineChart><c:grouping val=\"%s\"/><c:varyColors val=\"0\"/>", gk == CK_LINESTACK ? "stacked" : "standard"); break;
    case CK_STOCK: buf_adds(o, "<c:stockChart>"); break;
    case CK_AREA: case CK_AREASTACK: case CK_AREA100:
      buf_printf(o, "<c:areaChart><c:grouping val=\"%s\"/><c:varyColors val=\"0\"/>", gk == CK_AREA ? "standard" : gk == CK_AREASTACK ? "stacked" : "percentStacked"); break;
    case CK_PIE: buf_adds(o, "<c:pieChart><c:varyColors val=\"1\"/>"); break;
    case CK_DOUGHNUT: buf_adds(o, "<c:doughnutChart><c:varyColors val=\"1\"/>"); break;
    case CK_SCATTER: case CK_SCATTERLINE: buf_adds(o, "<c:scatterChart><c:scatterStyle val=\"lineMarker\"/><c:varyColors val=\"0\"/>"); break;
    case CK_BUBBLE: buf_adds(o, "<c:bubbleChart><c:varyColors val=\"0\"/>"); break;
    case CK_RADAR: buf_adds(o, "<c:radarChart><c:radarStyle val=\"marker\"/><c:varyColors val=\"0\"/>"); break;
    default: buf_printf(o, "<c:barChart><c:barDir val=\"col\"/><c:grouping val=\"clustered\"/><c:varyColors val=\"0\"/>"); gk = CK_COL; break;
    }
    for (k = 0; k < c->ns; k++) {
      int s_k = kind == CK_COMBO ? sk_of(c, k) : gk;
      if (kind == CK_COMBO && (family(s_k) != fam || (c->s[k].secondary != 0) != sec)) continue;
      if ((gk == CK_PIE || gk == CK_DOUGHNUT) && !first) break;    /* one series in a pie */
      w_series(b, c, &d, k, s_k, o);
      first = 0;
    }
    dlbls(o, c->labels, 0);
    switch (gk) {
    case CK_COL: case CK_COLSTACK: case CK_COL100: case CK_BAR: case CK_BARSTACK: case CK_BAR100:
      buf_printf(o, "<c:gapWidth val=\"150\"/>%s<c:axId val=\"%d\"/><c:axId val=\"%d\"/></c:barChart>", gk == CK_COL || gk == CK_BAR ? "" : "<c:overlap val=\"100\"/>", sec ? 500003 : 500001, sec ? 500004 : 500002); break;
    case CK_LINE: case CK_LINEMARK: case CK_LINESTACK:
      buf_printf(o, "<c:marker val=\"1\"/><c:axId val=\"%d\"/><c:axId val=\"%d\"/></c:lineChart>", sec ? 500003 : 500001, sec ? 500004 : 500002); break;
    case CK_STOCK:
      buf_adds(o, "<c:hiLowLines><c:spPr><a:ln w=\"9525\"><a:solidFill><a:srgbClr val=\"404040\"/></a:solidFill></a:ln></c:spPr></c:hiLowLines>");
      if (c->ns >= 4) buf_adds(o, "<c:upDownBars><c:gapWidth val=\"150\"/><c:upBars><c:spPr><a:solidFill><a:srgbClr val=\"FFFFFF\"/></a:solidFill></c:spPr></c:upBars><c:downBars><c:spPr><a:solidFill><a:srgbClr val=\"404040\"/></a:solidFill></c:spPr></c:downBars></c:upDownBars>");
      buf_adds(o, "<c:axId val=\"500001\"/><c:axId val=\"500002\"/></c:stockChart>"); break;
    case CK_AREA: case CK_AREASTACK: case CK_AREA100:
      buf_printf(o, "<c:axId val=\"%d\"/><c:axId val=\"%d\"/></c:areaChart>", sec ? 500003 : 500001, sec ? 500004 : 500002); break;
    case CK_PIE: buf_adds(o, "<c:firstSliceAng val=\"0\"/></c:pieChart>"); break;
    case CK_DOUGHNUT: buf_adds(o, "<c:firstSliceAng val=\"0\"/><c:holeSize val=\"50\"/></c:doughnutChart>"); break;
    case CK_SCATTER: case CK_SCATTERLINE: buf_adds(o, "<c:axId val=\"500001\"/><c:axId val=\"500002\"/></c:scatterChart>"); break;
    case CK_BUBBLE: buf_adds(o, "<c:bubbleScale val=\"100\"/><c:showNegBubbles val=\"0\"/><c:axId val=\"500001\"/><c:axId val=\"500002\"/></c:bubbleChart>"); break;
    case CK_RADAR: buf_adds(o, "<c:axId val=\"500001\"/><c:axId val=\"500002\"/></c:radarChart>"); break;
    }
  }
  /* the axes */
  if (kind == CK_SCATTER || kind == CK_SCATTERLINE || kind == CK_BUBBLE) {
    w_axis(o, 0, 500001, 500002, "b", c->vgrid, c->xtitle, &c->ha, 0, 0);
    w_axis(o, 0, 500002, 500001, "l", c->hgrid, c->ytitle, &c->va, 0, 0);
  } else if (kind != CK_PIE && kind != CK_DOUGHNUT) {
    w_axis(o, 1, 500001, 500002, horiz ? "l" : "b", c->vgrid, horiz ? c->ytitle : c->xtitle, NULL, 0, 0);
    w_axis(o, 0, 500002, 500001, horiz ? "b" : "l", c->hgrid, horiz ? c->xtitle : c->ytitle, &c->va, 0, 0);
    if (any2) { w_axis(o, 1, 500003, 500004, "b", 0, NULL, NULL, 1, 0); w_axis(o, 0, 500004, 500003, "r", 0, NULL, NULL, 0, 1); }
  }
  sppr(o, "c", c->efill[CE_PLOT], c->eline[CE_PLOT], 0);
  buf_adds(o, "</c:plotArea>");
  if (c->legend != LG_NONE && c->legend <= LG_RIGHT) {
    buf_printf(o, "<c:legend><c:legendPos val=\"%s\"/><c:overlay val=\"0\"/>", lgpos[c->legend]);
    sppr(o, "c", c->efill[CE_LEGEND], c->eline[CE_LEGEND], 0);
    buf_adds(o, "</c:legend>");
  }
  buf_adds(o, "<c:plotVisOnly val=\"1\"/><c:dispBlanksAs val=\"gap\"/></c:chart>");
  sppr(o, "c", c->efill[CE_CHART] ? c->efill[CE_CHART] : FILLRGB(0xFFFFFF), c->eline[CE_CHART] ? c->eline[CE_CHART] : FILLRGB(0xD9D9D9), 9525);
  buf_adds(o, "</c:chartSpace>");
  cdata_free(&d);
}
/* Excel 2016's kinds: a chartex part */
static void w_chartex(Book *b, int si, const Chart *c, Buf *o)
{
  CData d;
  int k, i;
  static const char *const layout[] = { "clusteredColumn", "waterfall", "funnel", "treemap", "sunburst", "boxWhisker" };
  int li = c->kind == CK_HISTOGRAM ? 0 : c->kind == CK_WATERFALL ? 1 : c->kind == CK_FUNNEL ? 2 : c->kind == CK_TREEMAP ? 3 : c->kind == CK_SUNBURST ? 4 : 5;
  static const char *const lgpos[5] = { "", "b", "t", "l", "r" };
  chart_resolve(b, si, c, &d);
  buf_adds(o, "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
              "<cx:chartSpace xmlns:a=\"http://schemas.openxmlformats.org/drawingml/2006/main\" xmlns:r=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships\" "
              "xmlns:cx=\"http://schemas.microsoft.com/office/drawing/2014/chartex\"><cx:chartData>");
  for (k = 0; k < c->ns; k++) {
    const CSeries *s = &c->s[k];
    buf_printf(o, "<cx:data id=\"%d\">", k);
    if (s->cat && c->kind != CK_HISTOGRAM && c->kind != CK_BOXWHISKER) {
      buf_adds(o, "<cx:strDim type=\"cat\"><cx:f>"); ftext(b, s->cat, o);
      buf_printf(o, "</cx:f><cx:lvl ptCount=\"%d\">", d.ncat);
      for (i = 0; i < d.ncat; i++) { buf_printf(o, "<cx:pt idx=\"%d\">", i); xesc(o, d.cat[i]); buf_adds(o, "</cx:pt>"); }
      buf_adds(o, "</cx:lvl></cx:strDim>");
    }
    if (s->val) {
      buf_printf(o, "<cx:numDim type=\"%s\"><cx:f>", c->kind == CK_TREEMAP || c->kind == CK_SUNBURST ? "size" : "val");
      ftext(b, s->val, o);
      buf_printf(o, "</cx:f><cx:lvl ptCount=\"%d\" formatCode=\"General\">", k < d.ns ? d.s[k].n : 0);
      for (i = 0; k < d.ns && i < d.s[k].n; i++) if (!isnan(d.s[k].v[i])) buf_printf(o, "<cx:pt idx=\"%d\">%.15g</cx:pt>", i, d.s[k].v[i]);
      buf_adds(o, "</cx:lvl></cx:numDim>");
    }
    buf_adds(o, "</cx:data>");
  }
  buf_adds(o, "</cx:chartData><cx:chart>");
  if (c->has_title) {
    const char *t = c->title && *c->title ? c->title : (d.ns == 1 && d.s[0].name ? d.s[0].name : "Chart Title");
    buf_adds(o, "<cx:title pos=\"t\" align=\"ctr\" overlay=\"0\"><cx:tx><cx:txData><cx:v>"); xesc(o, t); buf_adds(o, "</cx:v></cx:txData></cx:tx></cx:title>");
  }
  buf_adds(o, "<cx:plotArea><cx:plotAreaRegion>");
  for (k = 0; k < c->ns; k++) {
    const CSeries *s = &c->s[k];
    buf_printf(o, "<cx:series layoutId=\"%s\" uniqueId=\"{%08X-0000-4000-8000-%012X}\">", layout[li], 0x4C440000u + (unsigned)k, (unsigned)(si * 1000 + k));
    if (s->name) { buf_adds(o, "<cx:tx><cx:txData><cx:f>"); ftext(b, s->name, o); buf_adds(o, "</cx:f><cx:v>"); xesc(o, k < d.ns ? d.s[k].name : ""); buf_adds(o, "</cx:v></cx:txData></cx:tx>"); }
    else if (s->lit) { buf_adds(o, "<cx:tx><cx:txData><cx:v>"); xesc(o, s->lit); buf_adds(o, "</cx:v></cx:txData></cx:tx>"); }
    if (s->color) { buf_adds(o, "<cx:spPr>"); solid(o, s->color); buf_adds(o, "</cx:spPr>"); }
    if (c->labels || c->kind == CK_WATERFALL || c->kind == CK_FUNNEL || c->kind == CK_TREEMAP || c->kind == CK_SUNBURST)
      buf_adds(o, "<cx:dataLabels pos=\"outEnd\"><cx:visibility seriesName=\"0\" categoryName=\"0\" value=\"1\"/></cx:dataLabels>");
    buf_printf(o, "<cx:dataId val=\"%d\"/>", k);
    if (c->kind == CK_HISTOGRAM) buf_adds(o, "<cx:layoutPr><cx:binning intervalClosed=\"r\"/></cx:layoutPr>");
    else if (c->kind == CK_BOXWHISKER) buf_adds(o, "<cx:layoutPr><cx:visibility meanLine=\"0\" meanMarker=\"1\" nonoutliers=\"0\" outliers=\"1\"/><cx:statistics quartileMethod=\"exclusive\"/></cx:layoutPr>");
    else if (c->kind == CK_TREEMAP) buf_adds(o, "<cx:layoutPr><cx:parentLabelLayout val=\"overlapping\"/></cx:layoutPr>");
    else if (c->kind == CK_WATERFALL) buf_adds(o, "<cx:layoutPr><cx:subtotals/></cx:layoutPr>");
    buf_adds(o, "</cx:series>");
    if (c->kind != CK_BOXWHISKER) break;          /* (one series, but box & whisker shows them all) */
  }
  buf_adds(o, "</cx:plotAreaRegion>");
  if (c->kind == CK_FUNNEL) buf_adds(o, "<cx:axis id=\"0\"><cx:catScaling gapWidth=\"0.06\"/><cx:tickLabels/></cx:axis>");
  else if (c->kind != CK_TREEMAP && c->kind != CK_SUNBURST) {
    buf_printf(o, "<cx:axis id=\"0\"><cx:catScaling gapWidth=\"%s\"/><cx:tickLabels/></cx:axis>", c->kind == CK_HISTOGRAM ? "0" : "0.5");
    buf_printf(o, "<cx:axis id=\"1\"><cx:valScaling/>%s<cx:tickLabels/></cx:axis>", c->hgrid ? "<cx:majorGridlines/>" : "");
  }
  buf_adds(o, "</cx:plotArea>");
  if (c->legend != LG_NONE && c->legend <= LG_RIGHT) buf_printf(o, "<cx:legend pos=\"%s\" align=\"ctr\" overlay=\"0\"/>", lgpos[c->legend]);
  buf_adds(o, "</cx:chart></cx:chartSpace>");
  cdata_free(&d);
}

/* ------------------------------------------------------------------ saving: the drawing */
static const char *const prst[SK_N] = { "rect", "roundRect", "ellipse", "triangle", "line", "straightConnector1", "rect", "rect", "rect" };
static void corner(Buf *o, const char *tag, const DCorner *c)
{
  buf_printf(o, "<xdr:%s><xdr:col>%ld</xdr:col><xdr:colOff>%ld</xdr:colOff><xdr:row>%ld</xdr:row><xdr:rowOff>%ld</xdr:rowOff></xdr:%s>",
             tag, (long)c->c, (long)c->dx * EMU, (long)c->r, (long)c->dy * EMU, tag);
}
static void xfrm(Buf *o, const char *ns, Sheet *s, const DObj *ob)
{
  i32 x0, y0, x1, y1;
  dobj_box(s, ob, &x0, &y0, &x1, &y1);
  buf_printf(o, "<%s:xfrm%s%s><a:off x=\"%ld\" y=\"%ld\"/><a:ext cx=\"%ld\" cy=\"%ld\"/></%s:xfrm>", ns, ob->flipx ? " flipH=\"1\"" : "", ob->flipy ? " flipV=\"1\"" : "",
             (long)x0 * EMU, (long)y0 * EMU, (long)(x1 - x0) * EMU, (long)(y1 - y0) * EMU, ns);
}
static void cnvpr(Buf *o, const DObj *ob)
{
  buf_printf(o, "<xdr:cNvPr id=\"%lu\" name=\"", (unsigned long)ob->id + 1);
  xesc(o, ob->name);
  buf_adds(o, "\"");
  if (ob->descr) { buf_adds(o, " descr=\""); xesc(o, ob->descr); buf_adds(o, "\""); }
  buf_adds(o, "/>");
}
/* a shape's text body: its paragraphs (lines), each one run in the shape's font */
static void txbody(Buf *o, const DObj *ob)
{
  const char *p = ob->text ? ob->text : "";
  static const char *const al[3] = { "l", "ctr", "r" };
  static const char *const an[3] = { "t", "ctr", "b" };
  buf_printf(o, "<xdr:txBody><a:bodyPr vertOverflow=\"clip\" horzOverflow=\"clip\" wrap=\"square\" rtlCol=\"0\" anchor=\"%s\"/><a:lstStyle/>", an[ob->valign % 3]);
  do {
    const char *e = strchr(p, '\n');
    size_t n = e ? (size_t)(e - p) : strlen(p);
    buf_printf(o, "<a:p><a:pPr algn=\"%s\"/>", al[ob->halign % 3]);
    if (n) {
      buf_printf(o, "<a:r><a:rPr lang=\"en-GB\" sz=\"%d\" b=\"%d\" i=\"%d\">", ob->fsize ? ob->fsize * 10 : 1100, ob->bold, ob->italic);
      solid(o, ob->tcolor ? ob->tcolor : (ob->shape == SK_TEXTBOX ? 0 : 0xFFFFFF));
      buf_adds(o, "</a:rPr><a:t>"); xml_escape(o, p, n); buf_adds(o, "</a:t></a:r>");
    }
    buf_adds(o, "</a:p>");
    p = e ? e + 1 : NULL;
  } while (p);
  buf_adds(o, "</xdr:txBody>");
}
static void ink_geom(Buf *o, Sheet *s, const DObj *ob)
{
  i32 x0, y0, x1, y1;
  const char *p = ob->text ? ob->text : "";
  int first = 1;
  dobj_box(s, ob, &x0, &y0, &x1, &y1);
  buf_printf(o, "<a:custGeom><a:avLst/><a:gdLst/><a:ahLst/><a:cxnLst/><a:rect l=\"0\" t=\"0\" r=\"r\" b=\"b\"/><a:pathLst><a:path w=\"%ld\" h=\"%ld\" fill=\"none\">",
             (long)(x1 - x0 > 0 ? x1 - x0 : 1) * EMU, (long)(y1 - y0 > 0 ? y1 - y0 : 1) * EMU);
  while (*p) {
    long x = strtol(p, (char **)&p, 10), y;
    if (*p == ',') p++;
    y = strtol(p, (char **)&p, 10);
    buf_printf(o, "<a:%s><a:pt x=\"%ld\" y=\"%ld\"/></a:%s>", first ? "moveTo" : "lnTo", x * EMU, y * EMU, first ? "moveTo" : "lnTo");
    first = 0;
    while (*p == ';' || *p == ' ') p++;
  }
  buf_adds(o, "</a:path></a:pathLst></a:custGeom>");
}
/* an icon (drawing.c's little language) as DrawingML paths on its 16 x 16 grid */
static long inum(const char **p) { while (**p == ' ') (*p)++; return strtol(*p, (char **)p, 10); }
static void icon_geom(Buf *o, const DObj *ob)
{
  const char *p = drw_icon_desc(ob->text ? atoi(ob->text) : 0);
  buf_adds(o, "<a:custGeom><a:avLst/><a:gdLst/><a:ahLst/><a:cxnLst/><a:rect l=\"0\" t=\"0\" r=\"r\" b=\"b\"/><a:pathLst>");
  while (p && *p) {
    char c = *p++;
    long a, bb, e, f, n, i;
    switch (c) {
    case 'L': case 'R': case 'F':
      a = inum(&p); bb = inum(&p); e = inum(&p); f = inum(&p);
      buf_printf(o, "<a:path w=\"16\" h=\"16\"%s><a:moveTo><a:pt x=\"%ld\" y=\"%ld\"/></a:moveTo>", c == 'F' ? "" : " fill=\"none\"", a, bb);
      if (c == 'L') buf_printf(o, "<a:lnTo><a:pt x=\"%ld\" y=\"%ld\"/></a:lnTo>", e, f);
      else buf_printf(o, "<a:lnTo><a:pt x=\"%ld\" y=\"%ld\"/></a:lnTo><a:lnTo><a:pt x=\"%ld\" y=\"%ld\"/></a:lnTo><a:lnTo><a:pt x=\"%ld\" y=\"%ld\"/></a:lnTo><a:close/>", e, bb, e, f, a, f);
      buf_adds(o, "</a:path>");
      break;
    case 'O': case 'E':
      a = inum(&p); bb = inum(&p); e = inum(&p);
      buf_printf(o, "<a:path w=\"16\" h=\"16\"%s><a:moveTo><a:pt x=\"%ld\" y=\"%ld\"/></a:moveTo><a:arcTo wR=\"%ld\" hR=\"%ld\" stAng=\"0\" swAng=\"21600000\"/><a:close/></a:path>",
                 c == 'E' ? "" : " fill=\"none\"", a + e, bb, e, e);
      break;
    case 'P': case 'Q':
      n = inum(&p);
      buf_printf(o, "<a:path w=\"16\" h=\"16\"%s>", c == 'Q' ? "" : " fill=\"none\"");
      for (i = 0; i < n; i++) { a = inum(&p); bb = inum(&p); buf_printf(o, "<a:%s><a:pt x=\"%ld\" y=\"%ld\"/></a:%s>", i ? "lnTo" : "moveTo", a, bb, i ? "lnTo" : "moveTo"); }
      if (c == 'Q') buf_adds(o, "<a:close/>");
      buf_adds(o, "</a:path>");
      break;
    default: break;
    }
  }
  buf_adds(o, "</a:pathLst></a:custGeom>");
}
/* one object's element (a graphicFrame, pic, sp or cxnSp); in a group its frame's place is given */
static void w_obj(Book *b, int si, DObj *ob, Buf *o, Buf *r, Buf *cbuf, ZipWriter *w, int *rid, int *nc, int *nx, int *ni, int ingroup)
{
  Sheet *s = b->sh[si];
  char path[64];
  if (ob->kind == OB_CHART) {
    int ex = is_ex(ob->chart->kind);
    (*rid)++;
    cbuf->len = 0;
    if (ex) {
      (*nx)++;
      buf_printf(r, "<Relationship Id=\"rId%d\" Type=\"http://schemas.microsoft.com/office/2014/relationships/chartEx\" Target=\"../charts/chartEx%d.xml\"/>", *rid, *nx);
      buf_adds(o, "<mc:AlternateContent xmlns:mc=\"http://schemas.openxmlformats.org/markup-compatibility/2006\">");
      buf_printf(o, "<mc:Choice xmlns:%s=\"%s\" Requires=\"%s\">", ob->chart->kind == CK_FUNNEL ? "cx2" : "cx1",
                 ob->chart->kind == CK_FUNNEL ? "http://schemas.microsoft.com/office/drawing/2015/10/21/chartex" : "http://schemas.microsoft.com/office/drawing/2015/9/8/chartex",
                 ob->chart->kind == CK_FUNNEL ? "cx2" : "cx1");
    } else {
      (*nc)++;
      buf_printf(r, "<Relationship Id=\"rId%d\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/chart\" Target=\"../charts/chart%d.xml\"/>", *rid, *nc);
    }
    buf_adds(o, "<xdr:graphicFrame macro=\"\"><xdr:nvGraphicFramePr>"); cnvpr(o, ob); buf_adds(o, "<xdr:cNvGraphicFramePr/></xdr:nvGraphicFramePr>");
    if (ingroup) { xfrm(o, "xdr", s, ob); buf_adds(o, "<a:graphic>"); }
    else buf_adds(o, "<xdr:xfrm><a:off x=\"0\" y=\"0\"/><a:ext cx=\"0\" cy=\"0\"/></xdr:xfrm><a:graphic>");
    if (ex) buf_printf(o, "<a:graphicData uri=\"http://schemas.microsoft.com/office/drawing/2014/chartex\"><cx:chart xmlns:cx=\"http://schemas.microsoft.com/office/drawing/2014/chartex\" r:id=\"rId%d\"/></a:graphicData>", *rid);
    else buf_printf(o, "<a:graphicData uri=\"http://schemas.openxmlformats.org/drawingml/2006/chart\"><c:chart r:id=\"rId%d\"/></a:graphicData>", *rid);
    buf_adds(o, "</a:graphic></xdr:graphicFrame>");
    if (ex) {
      buf_adds(o, "</mc:Choice><mc:Fallback><xdr:sp macro=\"\" textlink=\"\"><xdr:nvSpPr>"); cnvpr(o, ob);
      buf_adds(o, "<xdr:cNvSpPr><a:spLocks noTextEdit=\"1\"/></xdr:cNvSpPr></xdr:nvSpPr><xdr:spPr>"); xfrm(o, "a", s, ob);
      buf_adds(o, "<a:prstGeom prst=\"rect\"><a:avLst/></a:prstGeom><a:solidFill><a:prstClr val=\"white\"/></a:solidFill><a:ln w=\"1\"><a:solidFill><a:prstClr val=\"green\"/></a:solidFill></a:ln></xdr:spPr>"
                   "<xdr:txBody><a:bodyPr vertOverflow=\"clip\" horzOverflow=\"clip\"/><a:lstStyle/><a:p><a:r><a:rPr lang=\"en-GB\" sz=\"1100\"/><a:t>This chart isn't available in your version of Excel.</a:t></a:r></a:p></xdr:txBody></xdr:sp></mc:Fallback></mc:AlternateContent>");
      w_chartex(b, si, ob->chart, cbuf);
      sprintf(path, "xl/charts/chartEx%d.xml", *nx);
    } else {
      w_chart(b, si, ob->chart, cbuf);
      sprintf(path, "xl/charts/chart%d.xml", *nc);
    }
    zip_add(w, path, cbuf->data, cbuf->len);
  } else if (ob->kind == OB_SLICER) {
    /* Excel 2010's pivot table slicers, Excel 2013's table ones; others see the fallback */
    if (ob->slicer->onpivot) buf_adds(o, "<mc:AlternateContent xmlns:mc=\"http://schemas.openxmlformats.org/markup-compatibility/2006\"><mc:Choice xmlns:a14=\"http://schemas.microsoft.com/office/drawing/2010/main\" Requires=\"a14\">");
    else buf_adds(o, "<mc:AlternateContent xmlns:mc=\"http://schemas.openxmlformats.org/markup-compatibility/2006\"><mc:Choice xmlns:sle15=\"http://schemas.microsoft.com/office/drawing/2012/slicer\" Requires=\"sle15\">");
    buf_adds(o, "<xdr:graphicFrame macro=\"\"><xdr:nvGraphicFramePr>"); cnvpr(o, ob); buf_adds(o, "<xdr:cNvGraphicFramePr/></xdr:nvGraphicFramePr>");
    buf_adds(o, "<xdr:xfrm><a:off x=\"0\" y=\"0\"/><a:ext cx=\"0\" cy=\"0\"/></xdr:xfrm><a:graphic><a:graphicData uri=\"http://schemas.microsoft.com/office/drawing/2010/slicer\">"
                "<sle:slicer xmlns:sle=\"http://schemas.microsoft.com/office/drawing/2010/slicer\" name=\"");
    xesc(o, ob->slicer->name);
    buf_adds(o, "\"/></a:graphicData></a:graphic></xdr:graphicFrame></mc:Choice><mc:Fallback><xdr:sp macro=\"\" textlink=\"\"><xdr:nvSpPr>"); cnvpr(o, ob);
    buf_adds(o, "<xdr:cNvSpPr><a:spLocks noTextEdit=\"1\"/></xdr:cNvSpPr></xdr:nvSpPr><xdr:spPr>"); xfrm(o, "a", s, ob);
    buf_adds(o, "<a:prstGeom prst=\"rect\"><a:avLst/></a:prstGeom><a:solidFill><a:prstClr val=\"white\"/></a:solidFill><a:ln w=\"1\"><a:solidFill><a:prstClr val=\"green\"/></a:solidFill></a:ln></xdr:spPr>"
                "<xdr:txBody><a:bodyPr vertOverflow=\"clip\" horzOverflow=\"clip\"/><a:lstStyle/><a:p><a:r><a:rPr lang=\"en-GB\" sz=\"1100\"/><a:t>This shape represents a slicer. Slicers need Excel 2010 or later.</a:t></a:r></a:p></xdr:txBody></xdr:sp></mc:Fallback></mc:AlternateContent>");
  } else if (ob->kind == OB_PICTURE) {
    (*rid)++; (*ni)++;
    buf_printf(r, "<Relationship Id=\"rId%d\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/image\" Target=\"../media/image%d.%s\"/>", *rid, *ni, ob->pictype == PIC_JPEG ? "jpeg" : "png");
    buf_adds(o, "<xdr:pic><xdr:nvPicPr>"); cnvpr(o, ob); buf_adds(o, "<xdr:cNvPicPr><a:picLocks noChangeAspect=\"1\"/></xdr:cNvPicPr></xdr:nvPicPr>");
    buf_printf(o, "<xdr:blipFill><a:blip r:embed=\"rId%d\"/><a:stretch><a:fillRect/></a:stretch></xdr:blipFill><xdr:spPr>", *rid);
    xfrm(o, "a", s, ob);
    buf_adds(o, "<a:prstGeom prst=\"rect\"><a:avLst/></a:prstGeom>");
    if (ob->line) { buf_printf(o, "<a:ln w=\"%ld\">", ob->linew ? (long)ob->linew * 127 : 9525L); solid(o, ob->line); buf_adds(o, "</a:ln>"); }
    buf_adds(o, "</xdr:spPr></xdr:pic>");
    sprintf(path, "xl/media/image%d.%s", *ni, ob->pictype == PIC_JPEG ? "jpeg" : "png");
    zip_add(w, path, ob->pic->data, ob->pic->len);
  } else {
    int conn = ob->shape == SK_LINE || ob->shape == SK_ARROW;
    buf_adds(o, conn ? "<xdr:cxnSp macro=\"\"><xdr:nvCxnSpPr>" : "<xdr:sp macro=\"\" textlink=\"\"><xdr:nvSpPr>");
    cnvpr(o, ob);
    buf_adds(o, conn ? "<xdr:cNvCxnSpPr/></xdr:nvCxnSpPr>" : ob->shape == SK_TEXTBOX ? "<xdr:cNvSpPr txBox=\"1\"/></xdr:nvSpPr>" : "<xdr:cNvSpPr/></xdr:nvSpPr>");
    buf_adds(o, "<xdr:spPr>");
    xfrm(o, "a", s, ob);
    if (ob->shape == SK_INK) ink_geom(o, s, ob);
    else if (ob->shape == SK_ICON) icon_geom(o, ob);
    else buf_printf(o, "<a:prstGeom prst=\"%s\"><a:avLst/></a:prstGeom>", prst[ob->shape % SK_N]);
    if (ob->shape == SK_ICON) solid(o, ob->line ? ob->line : FILLRGB(0x404040));
    else if (ob->fill && !conn && ob->shape != SK_INK) solid(o, ob->fill); else buf_adds(o, "<a:noFill/>");
    if (ob->line) {
      buf_printf(o, "<a:ln w=\"%ld\"", ob->linew ? (long)ob->linew * 127 : 9525L);
      if (ob->shape == SK_INK) buf_adds(o, " cap=\"rnd\"");
      buf_adds(o, ">");
      solid(o, ob->line);
      if (ob->shape == SK_INK) buf_adds(o, "<a:round/>");
      if (ob->shape == SK_ARROW) buf_adds(o, "<a:tailEnd type=\"triangle\"/>");
      buf_adds(o, "</a:ln>");
    } else buf_adds(o, "<a:ln><a:noFill/></a:ln>");
    buf_adds(o, "</xdr:spPr>");
    if (!conn && ob->shape != SK_INK && ob->shape != SK_ICON) txbody(o, ob);
    buf_adds(o, conn ? "</xdr:cxnSp>" : "</xdr:sp>");
  }
}
/* a group's own element start (its frame: child coordinates the same as the sheet's) */
static void w_group_open(Buf *o, Sheet *s, u32 gid)
{
  DGroup *g = drw_group_find(s, gid);
  i32 x0, y0, x1, y1;
  drw_group_box(s, gid, &x0, &y0, &x1, &y1);
  buf_printf(o, "<xdr:grpSp><xdr:nvGrpSpPr><xdr:cNvPr id=\"%lu\" name=\"", (unsigned long)gid + 1);
  xesc(o, g ? g->name : "Group");
  buf_printf(o, "\"/><xdr:cNvGrpSpPr/></xdr:nvGrpSpPr><xdr:grpSpPr><a:xfrm><a:off x=\"%ld\" y=\"%ld\"/><a:ext cx=\"%ld\" cy=\"%ld\"/>"
             "<a:chOff x=\"%ld\" y=\"%ld\"/><a:chExt cx=\"%ld\" cy=\"%ld\"/></a:xfrm></xdr:grpSpPr>",
             (long)x0 * EMU, (long)y0 * EMU, (long)(x1 - x0) * EMU, (long)(y1 - y0) * EMU, (long)x0 * EMU, (long)y0 * EMU, (long)(x1 - x0) * EMU, (long)(y1 - y0) * EMU);
}
static int writable(const DObj *ob) { return !((ob->kind == OB_CHART && !ob->chart) || (ob->kind == OB_PICTURE && !ob->pic) || (ob->kind == OB_SLICER && !ob->slicer)); }
void xd_write_parts(Book *b, int si, ZipWriter *w)
{
  Sheet *s = b->sh[si];
  Buf o, r, cbuf;
  int k, nc = count_kind(b, si, 0, 0), nx = count_kind(b, si, 1, 0), ni = count_kind(b, si, 0, 1), rid = 0;
  char path[64];
  if (has_bg(b, si)) {
    sprintf(path, "xl/media/background%d.%s", si + 1, s->drw->bgtype == PIC_JPEG ? "jpeg" : "png");
    zip_add(w, path, s->drw->bg->data, s->drw->bg->len);
  }
  if (!xd_has_drawing(b, si)) return;
  buf_init(&o); buf_init(&r); buf_init(&cbuf);
  buf_adds(&o, "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
               "<xdr:wsDr xmlns:xdr=\"http://schemas.openxmlformats.org/drawingml/2006/spreadsheetDrawing\" xmlns:a=\"http://schemas.openxmlformats.org/drawingml/2006/main\" "
               "xmlns:r=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships\" xmlns:c=\"http://schemas.openxmlformats.org/drawingml/2006/chart\">");
  buf_adds(&r, "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">");
  for (k = 0; k < drw_count(s); k++) {
    DObj *ob = drw_obj(s, k);
    static const char *const edit[3] = { "", " editAs=\"oneCell\"", " editAs=\"absolute\"" };
    u32 top = drw_top_group(s, k);
    if (!writable(ob)) continue;
    if (!top) {
      buf_printf(&o, "<xdr:twoCellAnchor%s>", edit[ob->anchor % 3]);
      corner(&o, "from", &ob->from); corner(&o, "to", &ob->to);
      w_obj(b, si, ob, &o, &r, &cbuf, w, &rid, &nc, &nx, &ni, 0);
    } else {
      /* a group: one anchor round it, its groups opened and closed as its members need them */
      u32 open[32], ch[32];
      int nopen = 0, nch, j;
      DObj box;
      i32 x0, y0, x1, y1;
      memset(&box, 0, sizeof box);
      drw_group_box(s, top, &x0, &y0, &x1, &y1);
      dobj_place(s, &box, x0, y0, x1, y1);
      buf_adds(&o, "<xdr:twoCellAnchor>");
      corner(&o, "from", &box.from); corner(&o, "to", &box.to);
      for (; k < drw_count(s) && drw_top_group(s, k) == top; k++) {
        DGroup *g;
        ob = drw_obj(s, k);
        if (!writable(ob)) continue;
        /* its groups, outermost first */
        nch = 0;
        for (g = drw_group_find(s, ob->group); g && nch < 32; g = drw_group_find(s, g->parent)) ch[nch++] = g->id;
        for (j = 0; j < nch / 2; j++) { u32 t = ch[j]; ch[j] = ch[nch - 1 - j]; ch[nch - 1 - j] = t; }
        for (j = 0; j < nopen && j < nch && open[j] == ch[j]; j++) ;
        while (nopen > j) { buf_adds(&o, "</xdr:grpSp>"); nopen--; }
        for (; nopen < nch; nopen++) { open[nopen] = ch[nopen]; w_group_open(&o, s, ch[nopen]); }
        w_obj(b, si, ob, &o, &r, &cbuf, w, &rid, &nc, &nx, &ni, 1);
      }
      k--;
      while (nopen > 0) { buf_adds(&o, "</xdr:grpSp>"); nopen--; }
    }
    buf_adds(&o, "<xdr:clientData/></xdr:twoCellAnchor>");
  }
  buf_adds(&o, "</xdr:wsDr>");
  buf_adds(&r, "</Relationships>");
  sprintf(path, "xl/drawings/drawing%d.xml", si + 1);
  zip_add(w, path, o.data, o.len);
  sprintf(path, "xl/drawings/_rels/drawing%d.xml.rels", si + 1);
  zip_add(w, path, r.data, r.len);
  buf_free(&o); buf_free(&r); buf_free(&cbuf);
}

/* ------------------------------------------------------------------ loading: parts and relationships */
typedef struct { char id[32]; char target[256]; char type[48]; } XRel;
static u8 *part(ZipReader *z, const char *name, size_t *len)
{
  int i = zip_find(z, name);
  if (i < 0 || z->e[i].usize > 64UL * 1024 * 1024) return NULL;
  return zip_read(z, i, len);
}
static void tidy(char *p)
{
  char *d;
  while ((d = strstr(p, "/../"))) {
    char *s = d;
    while (s > p && s[-1] != '/') s--;
    memmove(s, d + 4, strlen(d + 4) + 1);
  }
}
/* path's relationships (in its _rels folder) */
static int rels_of(ZipReader *z, const char *path, XRel **out)
{
  char rp[512], base[200];
  const char *sl = strrchr(path, '/');
  size_t dl = sl ? (size_t)(sl - path + 1) : 0, len;
  u8 *d;
  Xml x;
  int n = 0;
  *out = NULL;
  if (dl >= sizeof base) return 0;
  memcpy(base, path, dl); base[dl] = 0;
  snprintf(rp, sizeof rp, "%s_rels/%s.rels", base, sl ? sl + 1 : path);
  if (!(d = part(z, rp, &len))) return 0;
  xml_init(&x, (const char *)d, len);
  while (xml_next(&x) != XT_EOF) {
    const char *id, *t, *ty;
    XRel *nr;
    if (x.type != XT_START || strcmp(x.name, "Relationship")) continue;
    id = xml_attr(&x, "Id"); t = xml_attr(&x, "Target"); ty = xml_attr(&x, "Type");
    if (!id || !t || n > 5000 || !(nr = (XRel *)realloc(*out, (size_t)(n + 1) * sizeof(XRel)))) continue;
    *out = nr;
    snprintf(nr[n].id, sizeof nr[n].id, "%s", id);
    if (t[0] == '/') snprintf(nr[n].target, sizeof nr[n].target, "%s", t + 1);
    else { snprintf(nr[n].target, sizeof nr[n].target, "%s%s", base, t); tidy(nr[n].target); }
    { const char *s2 = ty ? strrchr(ty, '/') : NULL; snprintf(nr[n].type, sizeof nr[n].type, "%s", s2 ? s2 + 1 : ty ? ty : ""); }
    n++;
  }
  xml_free(&x);
  free(d);
  return n;
}
static const XRel *rel_by_id(const XRel *r, int n, const char *id)
{
  int i;
  for (i = 0; id && i < n; i++) if (!strcmp(r[i].id, id)) return &r[i];
  return NULL;
}

/* ------------------------------------------------------------------ loading: colours */
/* a colour element (srgbClr, schemeClr, sysClr, prstClr) into FILL_ON | rgb; 0 if none */
static u32 clr(Xml *x)
{
  const char *v = xml_attr(x, "val");
  if (!strcmp(x->name, "srgbClr") && v && strlen(v) >= 6) return FILLRGB(strtoul(v, NULL, 16));
  if (!strcmp(x->name, "sysClr")) { const char *l = xml_attr(x, "lastClr"); return FILLRGB(l ? strtoul(l, NULL, 16) : (v && !strcmp(v, "window") ? 0xFFFFFF : 0)); }
  if (!strcmp(x->name, "schemeClr") && v) {
    if (!strncmp(v, "accent", 6) && v[6] >= '1' && v[6] <= '6') return FILLRGB(accents[v[6] - '1']);
    if (!strcmp(v, "bg1") || !strcmp(v, "lt1")) return FILLRGB(0xFFFFFF);
    if (!strcmp(v, "bg2") || !strcmp(v, "lt2")) return FILLRGB(0xE7E6E6);
    if (!strcmp(v, "tx2") || !strcmp(v, "dk2")) return FILLRGB(0x44546A);
    return FILLRGB(0);
  }
  if (!strcmp(x->name, "prstClr") && v) {
    static const struct { const char *n; u32 c; } p[] = { { "black", 0 }, { "white", 0xFFFFFF }, { "red", 0xFF0000 }, { "green", 0x008000 }, { "blue", 0x0000FF }, { "yellow", 0xFFFF00 }, { "gray", 0x808080 } };
    int i;
    for (i = 0; i < (int)(sizeof p / sizeof p[0]); i++) if (!strcmp(p[i].n, v)) return FILLRGB(p[i].c);
    return FILLRGB(0);
  }
  return 0;
}
static int is_clr(const char *n) { return !strcmp(n, "srgbClr") || !strcmp(n, "schemeClr") || !strcmp(n, "sysClr") || !strcmp(n, "prstClr"); }

/* an element stack, for knowing where in the XML a token is */
#define STK 40
typedef struct { char n[STK][32]; int sp; } Stk;
static void stk_push(Stk *s, const char *name) { if (s->sp < STK) { strncpy(s->n[s->sp], name, 31); s->n[s->sp][31] = 0; } s->sp++; }
static void stk_pop(Stk *s) { if (s->sp > 0) s->sp--; }
/* the name k levels up from the current element (0: the current one) */
static const char *up(const Stk *s, int k) { int i = s->sp - 1 - k; return i >= 0 && i < STK ? s->n[i] : ""; }
static int within(const Stk *s, const char *name) { int i; for (i = 0; i < s->sp && i < STK; i++) if (!strcmp(s->n[i], name)) return 1; return 0; }
static int within_from(const Stk *s, const char *name, int from) { int i; for (i = from; i < s->sp && i < STK; i++) if (!strcmp(s->n[i], name)) return 1; return 0; }

/* ------------------------------------------------------------------ loading: charts */
typedef struct { int id, cat, pos, grid, del; char title[200]; CAxis ax; } RAx;
static Formula *parse_f(Book *b, int sheet, const char *t)
{
  int ep;
  while (*t == ' ' || *t == '=') t++;
  if (!*t) return NULL;
  return formula_parse(b, sheet, t, &ep);
}
static void set_str(char **p, const char *s) { free(*p); *p = s ? str_dup(s) : NULL; }
static Chart *read_chart(Book *b, int sheet, const char *data, size_t len)
{
  Xml x;
  Stk st;
  Chart *c = chart_new(CK_COL);
  RAx ax[6];
  int nax = 0, k, i;
  int groups = 0, gkind = CK_COL, gfirst = 0, gbar = 0, gdir_bar = 0;
  int grp_axval[8], grp_first[8], grp_kind[8], grp_n = 0, anymark = 0;
  char grouping[24] = "";
  int has_title_el = 0, autodel = -1;
  Buf tb, nb, lit;
  int titledepth = -1, ser = -1, serdepth = -1, ptidx = -1;
  if (!c) return NULL;
  c->legend = LG_NONE; c->hgrid = 0; c->has_title = 0;
  memset(&st, 0, sizeof st);
  memset(ax, 0, sizeof ax);
  buf_init(&tb); buf_init(&nb); buf_init(&lit);
  xml_init(&x, data, len);
  while (xml_next(&x) != XT_EOF) {
    if (x.type == XT_START) {
      const char *n = x.name, *v = xml_attr(&x, "val");
      stk_push(&st, n);
      /* the chart groups */
      if (!strcmp(n, "barChart") || !strcmp(n, "bar3DChart")) { gkind = CK_COL; gbar = 1; gdir_bar = 0; grouping[0] = 0; gfirst = c->ns; }
      else if (!strcmp(n, "lineChart") || !strcmp(n, "line3DChart")) { gkind = CK_LINE; gbar = 0; grouping[0] = 0; gfirst = c->ns; anymark = 0; }
      else if (!strcmp(n, "pieChart") || !strcmp(n, "pie3DChart") || !strcmp(n, "ofPieChart")) { gkind = CK_PIE; gfirst = c->ns; }
      else if (!strcmp(n, "doughnutChart")) { gkind = CK_DOUGHNUT; gfirst = c->ns; }
      else if (!strcmp(n, "areaChart") || !strcmp(n, "area3DChart")) { gkind = CK_AREA; grouping[0] = 0; gfirst = c->ns; }
      else if (!strcmp(n, "scatterChart")) { gkind = CK_SCATTER; gfirst = c->ns; }
      else if (!strcmp(n, "bubbleChart")) { gkind = CK_BUBBLE; gfirst = c->ns; }
      else if (!strcmp(n, "radarChart")) { gkind = CK_RADAR; gfirst = c->ns; }
      else if (!strcmp(n, "stockChart")) { gkind = CK_STOCK; gfirst = c->ns; }
      else if (!strcmp(n, "surfaceChart") || !strcmp(n, "surface3DChart")) { gkind = CK_LINE; gfirst = c->ns; }
      else if (!strcmp(n, "barDir") && v) gdir_bar = !strcmp(v, "bar");
      else if (!strcmp(n, "grouping") && v) snprintf(grouping, sizeof grouping, "%s", v);
      else if (!strcmp(n, "ser") && strstr(up(&st, 1), "Chart")) {
        CSeries *ns = (CSeries *)realloc(c->s, (size_t)(c->ns + 1) * sizeof(CSeries));
        if (!ns || c->ns >= 255) { xml_skip(&x); stk_pop(&st); continue; }
        c->s = ns;
        memset(&c->s[c->ns], 0, sizeof(CSeries));
        c->s[c->ns].kind = CK_SAME;
        ser = c->ns++; serdepth = st.sp;
      }
      else if (!strcmp(n, "title") && titledepth < 0) {
        titledepth = st.sp; tb.len = 0; if (tb.data) tb.data[0] = 0;
        if (!within(&st, "valAx") && !within(&st, "catAx") && !within(&st, "dateAx") && !within(&st, "serAx")) has_title_el = 1;
      }
      else if (!strcmp(n, "autoTitleDeleted") && v) autodel = atoi(v) || !strcmp(v, "true");
      else if (!strcmp(n, "legendPos") && v) c->legend = !strcmp(v, "t") ? LG_TOP : !strcmp(v, "l") ? LG_LEFT : !strcmp(v, "b") ? LG_BOTTOM : LG_RIGHT;
      else if (!strcmp(n, "legend") && c->legend == LG_NONE) c->legend = LG_RIGHT;
      else if (!strcmp(n, "showVal") && v && (atoi(v) || !strcmp(v, "true")) && within(&st, "dLbls")) c->labels = 1;
      else if (!strcmp(n, "showPercent") && v && (atoi(v) || !strcmp(v, "true")) && within(&st, "dLbls")) c->labels = 1;
      else if ((!strcmp(n, "catAx") || !strcmp(n, "valAx") || !strcmp(n, "dateAx")) && nax < 6) { memset(&ax[nax], 0, sizeof ax[nax]); ax[nax].cat = strcmp(n, "valAx") != 0; ax[nax].id = -1; nax++; }
      else if (nax && st.sp >= 2 && (!strcmp(up(&st, 1), "catAx") || !strcmp(up(&st, 1), "valAx") || !strcmp(up(&st, 1), "dateAx"))) {
        RAx *a = &ax[nax - 1];
        if (!strcmp(n, "axId") && v) a->id = atoi(v);
        else if (!strcmp(n, "axPos") && v) a->pos = v[0];
        else if (!strcmp(n, "majorGridlines")) a->grid = 1;
        else if (!strcmp(n, "delete") && v) a->del = atoi(v) || !strcmp(v, "true");
        else if (!strcmp(n, "majorUnit") && v) { a->ax.majset = 1; a->ax.major = atof(v); }
      }
      else if (nax && (!strcmp(n, "min") || !strcmp(n, "max")) && !strcmp(up(&st, 1), "scaling") && v) {
        RAx *a = &ax[nax - 1];
        if (n[1] == 'i') { a->ax.minset = 1; a->ax.min = atof(v); } else { a->ax.maxset = 1; a->ax.max = atof(v); }
      }
      else if (!strcmp(n, "axId") && v && st.sp >= 2 && strstr(up(&st, 1), "Chart") && grp_n < 8) {
        /* a group's axes: the second is its value axis */
        if (gfirst >= 0) grp_axval[grp_n] = atoi(v);
      }
      else if (ser >= 0 && !strcmp(n, "symbol") && v && !strcmp(up(&st, 1), "marker") && st.sp == serdepth + 2) { if (!strcmp(v, "none")) c->s[ser].nomarker = 1; else anymark = 1; }
      else if (ser >= 0 && !strcmp(n, "smooth") && v && st.sp == serdepth + 1) c->s[ser].smooth = (u8)(atoi(v) || !strcmp(v, "true"));
      else if (ser >= 0 && is_clr(n) && st.sp == serdepth + 3 && !strcmp(up(&st, 1), "solidFill") && !strcmp(up(&st, 2), "spPr")) c->s[ser].color = clr(&x);
      else if (ser >= 0 && is_clr(n) && st.sp == serdepth + 4 && !strcmp(up(&st, 1), "solidFill") && !strcmp(up(&st, 2), "ln") && !strcmp(up(&st, 3), "spPr")) c->s[ser].line = clr(&x);
      else if (ser >= 0 && !strcmp(n, "pt") && within_from(&st, "numLit", serdepth)) { const char *ix = xml_attr(&x, "idx"); ptidx = ix ? atoi(ix) : -1; }
      else if (ser >= 0 && !strcmp(n, "numLit")) { lit.len = 0; buf_adds(&lit, "{"); }
      else if (is_clr(n) && !strcmp(up(&st, 1), "solidFill") && !strcmp(up(&st, 2), "spPr")) {
        /* the chart area, plot area, legend or title's fill (and outline below) */
        const char *owner = up(&st, 3);
        int e = !strcmp(owner, "chartSpace") ? CE_CHART : !strcmp(owner, "plotArea") ? CE_PLOT : !strcmp(owner, "legend") ? CE_LEGEND : !strcmp(owner, "title") && !within(&st, "valAx") && !within(&st, "catAx") ? CE_TITLE : -1;
        if (e >= 0) c->efill[e] = clr(&x);
      }
      else if (is_clr(n) && !strcmp(up(&st, 1), "solidFill") && !strcmp(up(&st, 2), "ln") && !strcmp(up(&st, 3), "spPr")) {
        const char *owner = up(&st, 4);
        int e = !strcmp(owner, "chartSpace") ? CE_CHART : !strcmp(owner, "plotArea") ? CE_PLOT : !strcmp(owner, "legend") ? CE_LEGEND : !strcmp(owner, "title") && !within(&st, "valAx") && !within(&st, "catAx") ? CE_TITLE : -1;
        if (e >= 0) c->eline[e] = clr(&x);
      }
      else if (!strcmp(n, "style") && v && st.sp <= 2) { int sv = atoi(v); c->style = (u8)(sv >= 2 && sv <= 9 ? sv - 1 : 1); }
      continue;
    }
    if (x.type == XT_TEXT) {
      const char *n = up(&st, 0);
      if (titledepth >= 0 && (!strcmp(n, "t") || (!strcmp(n, "v") && within(&st, "strCache")))) buf_add(&tb, x.text.data, x.text.len);
      else if (!strcmp(n, "name") && !strcmp(up(&st, 1), "pivotSource") && x.text.data) {
        const char *t = x.text.data, *e;
        if (*t == '[' && (e = strchr(t, ']'))) t = e + 1;
        free(c->pivot); c->pivot = str_dup(t);
      }
      else if (ser >= 0 && !strcmp(n, "f")) {
        const char *part2 = up(&st, 2);
        Formula *f = parse_f(b, sheet, x.text.data ? x.text.data : "");
        CSeries *s = &c->s[ser];
        if (!strcmp(part2, "tx")) { formula_free(s->name); s->name = f; }
        else if (!strcmp(part2, "cat") || !strcmp(part2, "xVal")) { formula_free(s->cat); s->cat = f; }
        else if (!strcmp(part2, "val") || !strcmp(part2, "yVal")) { formula_free(s->val); s->val = f; }
        else if (!strcmp(part2, "bubbleSize")) { formula_free(s->size); s->size = f; }
        else formula_free(f);
      } else if (ser >= 0 && !strcmp(n, "v") && !strcmp(up(&st, 1), "tx")) {
        buf_init(&nb); buf_add(&nb, x.text.data, x.text.len); set_str(&c->s[ser].lit, nb.data ? nb.data : ""); buf_free(&nb);
      } else if (ser >= 0 && !strcmp(n, "v") && !strcmp(up(&st, 1), "pt") && within_from(&st, "numLit", serdepth) && ptidx >= 0) {
        if (lit.len > 1) buf_adds(&lit, ",");
        buf_add(&lit, x.text.data, x.text.len);
      }
      continue;
    }
    if (x.type == XT_END) {
      const char *n = up(&st, 0);
      if (titledepth >= 0 && st.sp == titledepth) {
        if (has_title_el && !within(&st, "valAx") && !within(&st, "catAx") && !within(&st, "dateAx")) { c->has_title = 1; set_str(&c->title, tb.len ? tb.data : NULL); }
        else if (nax) snprintf(ax[nax - 1].title, sizeof ax[nax - 1].title, "%s", tb.data ? tb.data : "");
        titledepth = -1; has_title_el = 0;
      }
      if (!strcmp(n, "numLit") && ser >= 0 && lit.len > 1) {
        Formula *f;
        buf_adds(&lit, "}");
        if (!strcmp(up(&st, 1), "val") || !strcmp(up(&st, 1), "yVal")) { f = parse_f(b, sheet, lit.data); if (f) { formula_free(c->s[ser].val); c->s[ser].val = f; } }
      }
      if (ser >= 0 && st.sp == serdepth && !strcmp(n, "ser")) { ser = -1; serdepth = -1; }
      if (strstr(n, "Chart") && strcmp(n, "chart") && grp_n < 8 && st.sp >= 2 && !strcmp(up(&st, 1), "plotArea")) {
        int kk = gkind;
        if (gbar) {
          int stk = !strcmp(grouping, "stacked"), pct = !strcmp(grouping, "percentStacked");
          kk = gdir_bar ? (pct ? CK_BAR100 : stk ? CK_BARSTACK : CK_BAR) : (pct ? CK_COL100 : stk ? CK_COLSTACK : CK_COL);
        } else if (gkind == CK_LINE) kk = !strcmp(grouping, "stacked") || !strcmp(grouping, "percentStacked") ? CK_LINESTACK : anymark ? CK_LINEMARK : CK_LINE;
        else if (gkind == CK_AREA) kk = !strcmp(grouping, "stacked") ? CK_AREASTACK : !strcmp(grouping, "percentStacked") ? CK_AREA100 : CK_AREA;
        grp_kind[grp_n] = kk; grp_first[grp_n] = gfirst;
        grp_n++; groups++;
        gbar = 0;
      }
      stk_pop(&st);
    }
  }
  xml_free(&x);
  if (grp_n == 0) { chart_free(c); buf_free(&tb); buf_free(&lit); return NULL; }
  if (grp_n == 1) c->kind = (u8)grp_kind[0];
  else {
    c->kind = CK_COMBO;
    for (k = 0; k < grp_n; k++) {
      int end = k + 1 < grp_n ? grp_first[k + 1] : c->ns;
      for (i = grp_first[k]; i < end; i++) { c->s[i].kind = (u8)grp_kind[k]; c->s[i].secondary = (u8)(grp_axval[k] != grp_axval[0]); }
    }
  }
  /* the axes onto the chart */
  for (k = 0; k < nax; k++) {
    RAx *a = &ax[k];
    int vertical = a->pos == 'l' || a->pos == 'r';
    if (a->del && k >= 2) continue;
    if (!a->cat) {
      if ((c->kind == CK_SCATTER || c->kind == CK_SCATTERLINE || c->kind == CK_BUBBLE) && !vertical) { c->ha = a->ax; c->vgrid = (u8)a->grid; }
      else if (k < 2 || grp_n == 1) { c->va = a->ax; c->hgrid = (u8)a->grid; }
    } else if (k < 2) c->vgrid = (u8)a->grid;
    if (a->title[0] && k < 3) set_str(vertical ? &c->ytitle : &c->xtitle, a->title);
  }
  if (!c->has_title && autodel == 0 && c->ns == 1) c->has_title = 1;
  /* scatter series drawn with lines unless their line is off: Excel writes ln/noFill for markers only */
  if (c->kind == CK_SCATTER) {
    /* re-read cheaply: a scatter series with a line colour has lines */
    for (k = 0; k < c->ns; k++) if (c->s[k].line) { c->kind = CK_SCATTERLINE; break; }
  }
  /* colours that are just Ledger's palette stay automatic */
  for (k = 0; k < c->ns; k++) {
    Chart t = *c;
    u32 autoc;
    t.ns = 0;
    autoc = chart_series_rgb(&t, k, c->ns);
    if (c->s[k].color && (c->s[k].color & 0xFFFFFF) == autoc) c->s[k].color = 0;
    if (c->s[k].line && (c->s[k].line & 0xFFFFFF) == autoc) c->s[k].line = 0;
  }
  if (c->kind == CK_PIE || c->kind == CK_DOUGHNUT) for (k = 0; k < c->ns; k++) c->s[k].color = c->s[k].line = 0;
  buf_free(&tb); buf_free(&lit);
  return c;
}
/* Excel 2016's charts (chartex) */
static Chart *read_chartex(Book *b, int sheet, const char *data, size_t len)
{
  Xml x;
  Stk st;
  Chart *c = chart_new(CK_COL);
  typedef struct { char id[16]; char cat[300]; char val[300]; } Dat;
  Dat dat[16];
  int nd = 0, k, kind = -1, dimcat = 0, intitle = 0;
  Buf tb;
  char sid[16] = "", sname[300] = "", sv[300] = "";
  int inser = 0;
  if (!c) return NULL;
  c->legend = LG_NONE; c->has_title = 0; c->hgrid = 0;
  memset(&st, 0, sizeof st);
  buf_init(&tb);
  xml_init(&x, data, len);
  while (xml_next(&x) != XT_EOF) {
    if (x.type == XT_START) {
      const char *n = x.name;
      stk_push(&st, n);
      if (!strcmp(n, "data") && nd < 16 && within(&st, "chartData")) { const char *id = xml_attr(&x, "id"); memset(&dat[nd], 0, sizeof dat[nd]); snprintf(dat[nd].id, sizeof dat[nd].id, "%s", id ? id : ""); nd++; }
      else if (!strcmp(n, "strDim") || !strcmp(n, "numDim")) { const char *t = xml_attr(&x, "type"); dimcat = t && (!strcmp(t, "cat") || !strcmp(t, "x")); }
      else if (!strcmp(n, "series")) {
        const char *l = xml_attr(&x, "layoutId");
        int kk = !l ? -1 : !strcmp(l, "clusteredColumn") ? CK_HISTOGRAM : !strcmp(l, "waterfall") ? CK_WATERFALL : !strcmp(l, "funnel") ? CK_FUNNEL :
                 !strcmp(l, "treemap") ? CK_TREEMAP : !strcmp(l, "sunburst") ? CK_SUNBURST : !strcmp(l, "boxWhisker") ? CK_BOXWHISKER : -1;
        if (l && !strcmp(l, "paretoLine")) { xml_skip(&x); stk_pop(&st); continue; }
        if (kind < 0) kind = kk;
        inser = 1; sid[0] = 0; sname[0] = 0; sv[0] = 0;
      }
      else if (!strcmp(n, "dataId") && inser) { const char *v = xml_attr(&x, "val"); snprintf(sid, sizeof sid, "%s", v ? v : ""); }
      else if (!strcmp(n, "title") && !within(&st, "axis")) { intitle = 1; tb.len = 0; if (tb.data) tb.data[0] = 0; c->has_title = 1; }
      else if (!strcmp(n, "legend")) { const char *p = xml_attr(&x, "pos"); c->legend = !p ? LG_RIGHT : p[0] == 'b' ? LG_BOTTOM : p[0] == 't' ? LG_TOP : p[0] == 'l' ? LG_LEFT : LG_RIGHT; }
      else if (!strcmp(n, "majorGridlines")) c->hgrid = 1;
      else if (!strcmp(n, "visibility") && within(&st, "dataLabels")) { const char *v = xml_attr(&x, "value"); if (v && atoi(v)) c->labels = 1; }
      continue;
    }
    if (x.type == XT_TEXT) {
      const char *n = up(&st, 0);
      if (intitle && (!strcmp(n, "v") || !strcmp(n, "t"))) buf_add(&tb, x.text.data, x.text.len);
      else if (!strcmp(n, "f") && nd && within(&st, "chartData")) snprintf(dimcat ? dat[nd - 1].cat : dat[nd - 1].val, 300, "%.*s", (int)x.text.len, x.text.data ? x.text.data : "");
      else if (inser && !strcmp(n, "f") && within(&st, "tx")) snprintf(sname, sizeof sname, "%.*s", (int)x.text.len, x.text.data ? x.text.data : "");
      else if (inser && !strcmp(n, "v") && within(&st, "tx")) snprintf(sv, sizeof sv, "%.*s", (int)x.text.len, x.text.data ? x.text.data : "");
      continue;
    }
    if (x.type == XT_END) {
      const char *n = up(&st, 0);
      if (!strcmp(n, "title") && intitle) { intitle = 0; set_str(&c->title, tb.len ? tb.data : NULL); }
      if (!strcmp(n, "series") && inser) {
        CSeries *ns = (CSeries *)realloc(c->s, (size_t)(c->ns + 1) * sizeof(CSeries));
        inser = 0;
        if (ns) {
          CSeries *s;
          c->s = ns; s = &c->s[c->ns++];
          memset(s, 0, sizeof *s);
          s->kind = CK_SAME;
          for (k = 0; k < nd; k++) if (!strcmp(dat[k].id, sid)) { s->cat = dat[k].cat[0] ? parse_f(b, sheet, dat[k].cat) : NULL; s->val = dat[k].val[0] ? parse_f(b, sheet, dat[k].val) : NULL; }
          if (sname[0]) s->name = parse_f(b, sheet, sname);
          else if (sv[0]) s->lit = str_dup(sv);
        }
      }
      stk_pop(&st);
    }
  }
  xml_free(&x);
  buf_free(&tb);
  if (kind < 0 || !c->ns) { chart_free(c); return NULL; }
  c->kind = (u8)kind;
  return c;
}

/* ------------------------------------------------------------------ loading: the drawing */
typedef struct {
  int kind, shape, anchor, have_to, txbox, arrow, nofill, noline, custgeom;
  DCorner from, to;
  long posx, posy, extx, exty;      /* absolute and one-cell anchors (EMU) */
  long gw, gh;                     /* custGeom path's size */
  char name[48]; char *descr;
  char rid[32];
  char slname[48];                 /* a slicer's name */
  u32 fill, line; u16 linew;
  int flipx, flipy;
  Buf text, ink;
  u16 fsize; u8 bold, italic, halign, valign; u32 tcolor;
  int hasx; long xo, yo, xe, ye;    /* (in a group) its frame: a:off and a:ext in its group's child coordinates */
} RObj;
/* the groups open while reading (grpSp): each one's id, its frame (off, ext) and child frame (chOff,
   chExt), and from them where its children go: pixel = p0 + (v - ch0) * k */
typedef struct { u32 id; long off[2], ext[2], choff[2], chext[2]; double p0[2], ch0[2], k[2]; } RGrp;
static void robj_reset(RObj *o)
{
  buf_free(&o->text); buf_free(&o->ink); free(o->descr);
  memset(o, 0, sizeof *o);
  o->kind = -1;
  buf_init(&o->text); buf_init(&o->ink);
}
/* the object read made (its frame box, pixels at 100%, when it is in a group); NULL if it wasn't */
static DObj *finish_obj(Book *b, ZipReader *z, const XRel *rels, int nrels, int sheet, RObj *o, const i32 *box)
{
  Sheet *s = b->sh[sheet];
  DObj *ob = NULL;
  const XRel *r = rel_by_id(rels, nrels, o->rid);
  Chart *ch = NULL;
  Blob *pic = NULL;
  int ptype = PIC_NONE;
  if (o->kind == OB_CHART && r) {
    size_t cl;
    u8 *cd = part(z, r->target, &cl);
    if (cd) { ch = strstr(r->type, "chartEx") || strstr(r->target, "chartEx") ? read_chartex(b, sheet, (const char *)cd, cl) : read_chart(b, sheet, (const char *)cd, cl); free(cd); }
  } else if (o->kind == OB_PICTURE && r) {
    size_t pl;
    u8 *pd = part(z, r->target, &pl);
    u32 w0, h0;
    if (pd) {
      int t = img_info(pd, (u32)pl, &w0, &h0);
      if (t == IMG_PNG || t == IMG_JPEG) { pic = blob_new(pd, (u32)pl); ptype = t == IMG_PNG ? PIC_PNG : PIC_JPEG; }
      free(pd);
    }
  }
  struct Slicer *sl = o->kind == OB_SLICER ? tablexml_slicer_take(sheet, o->slname) : NULL;
  if ((o->kind == OB_CHART && ch) || (o->kind == OB_PICTURE && pic) || o->kind == OB_SHAPE || sl) ob = drw_new(s, o->kind);
  if (ob) {
    ob->anchor = (u8)o->anchor;
    ob->from = o->from;
    if (box) { ob->anchor = AN_TWOCELL; dobj_place(s, ob, box[0], box[1], box[2], box[3]); }
    else if (o->have_to) ob->to = o->to;
    else {
      i32 x0, y0;
      if (o->anchor == AN_ABSOLUTE && !o->have_to) { ob->from.c = sheet_col_at(s, (i32)(o->posx / EMU), &ob->from.dx); ob->from.r = sheet_row_at(s, (i32)(o->posy / EMU), &ob->from.dy); }
      x0 = sheet_col_px(s, ob->from.c) + ob->from.dx; y0 = sheet_row_px(s, ob->from.r) + ob->from.dy;
      dobj_place(s, ob, x0, y0, x0 + (i32)(o->extx / EMU), y0 + (i32)(o->exty / EMU));
    }
    if (o->name[0]) snprintf(ob->name, sizeof ob->name, "%s", o->name);
    ob->descr = o->descr; o->descr = NULL;
    ob->flipx = (u8)o->flipx; ob->flipy = (u8)o->flipy;
    ob->chart = ch; ob->pic = pic; ob->pictype = (u8)ptype;
    ob->slicer = sl; sl = NULL;
    ob->line = o->noline ? 0 : o->line; ob->linew = o->linew;
    if (o->kind == OB_SHAPE) {
      if (o->shape == SK_LINE && o->arrow) o->shape = SK_ARROW;
      if (o->txbox) o->shape = SK_TEXTBOX;
      if (o->custgeom && !strncmp(o->name, "Icon: ", 6) && drw_icon_by_name(o->name + 6) >= 0) {
        char t[8];
        sprintf(t, "%d", drw_icon_by_name(o->name + 6));
        ob->text = str_dup(t);
        o->shape = SK_ICON;
        o->custgeom = 0;
        o->text.len = 0;
      } else if (o->custgeom) {
        /* a freehand path: its points in pixels from the shape's corner */
        Buf pts;
        const char *p = o->ink.data ? o->ink.data : "";
        i32 x0, y0, x1, y1;
        double sx, sy;
        dobj_box(s, ob, &x0, &y0, &x1, &y1);
        sx = o->gw > 0 ? (double)(x1 - x0) / o->gw : 1.0 / EMU; sy = o->gh > 0 ? (double)(y1 - y0) / o->gh : 1.0 / EMU;
        buf_init(&pts);
        while (*p) {
          long px = strtol(p, (char **)&p, 10), py;
          if (*p == ',') p++;
          py = strtol(p, (char **)&p, 10);
          buf_printf(&pts, "%s%ld,%ld", pts.len ? ";" : "", (long)(px * sx + 0.5), (long)(py * sy + 0.5));
          while (*p == ';') p++;
        }
        ob->text = pts.data ? buf_take(&pts) : str_dup("");
        buf_free(&pts);
        o->shape = SK_INK;
      }
      ob->shape = (u8)o->shape;
      ob->fill = o->nofill ? 0 : o->fill;
      if (!o->custgeom && o->text.len) ob->text = str_dup(o->text.data);
      ob->fsize = o->fsize; ob->bold = o->bold; ob->italic = o->italic; ob->halign = o->halign; ob->valign = o->valign; ob->tcolor = o->tcolor;
    }
  } else { chart_free(ch); blob_free(pic); }
  slicer_free(sl);
  return ob;
}
static void read_drawing(Book *b, ZipReader *z, const char *path, int sheet)
{
  size_t len;
  u8 *d = part(z, path, &len);
  XRel *rels = NULL;
  int nrels, para = 0;
  Xml x;
  Stk st;
  RObj o;
  Sheet *s = b->sh[sheet];
  RGrp gs[16];
  int gsp = 0, ngrp_seen = 0;
  if (!d) return;
  nrels = rels_of(z, path, &rels);
  memset(&o, 0, sizeof o);
  robj_reset(&o);
  memset(&st, 0, sizeof st);
  xml_init(&x, (const char *)d, len);
  while (xml_next(&x) != XT_EOF) {
    if (x.type == XT_START) {
      const char *n = x.name;
      stk_push(&st, n);
      if (!strcmp(n, "Fallback") || (!strcmp(n, "grpSp") && gsp >= 16)) { xml_skip(&x); stk_pop(&st); continue; }
      if (!strcmp(n, "grpSp")) {
        /* a group: its children each an object of their own, placed through its frame */
        DGroup *g = drw_group_add(s, gsp ? gs[gsp - 1].id : 0);
        memset(&gs[gsp], 0, sizeof gs[gsp]);
        gs[gsp].id = g ? g->id : 0;
        gs[gsp].k[0] = gs[gsp].k[1] = 1.0 / EMU;
        if (gsp) { gs[gsp].p0[0] = gs[gsp - 1].p0[0]; gs[gsp].p0[1] = gs[gsp - 1].p0[1]; gs[gsp].ch0[0] = gs[gsp - 1].ch0[0]; gs[gsp].ch0[1] = gs[gsp - 1].ch0[1]; gs[gsp].k[0] = gs[gsp - 1].k[0]; gs[gsp].k[1] = gs[gsp - 1].k[1]; }
        gsp++; ngrp_seen = 1;
        continue;
      }
      if (gsp && !strcmp(up(&st, 1), "xfrm") && (!strcmp(n, "off") || !strcmp(n, "ext") || !strcmp(n, "chOff") || !strcmp(n, "chExt"))) {
        const char *a1 = xml_attr(&x, n[0] == 'o' || n[2] == 'O' ? "x" : "cx"), *a2 = xml_attr(&x, n[0] == 'o' || n[2] == 'O' ? "y" : "cy");
        long v1 = a1 ? atol(a1) : 0, v2 = a2 ? atol(a2) : 0;
        if (!strcmp(up(&st, 2), "grpSpPr")) {
          RGrp *G = &gs[gsp - 1];
          long *t = !strcmp(n, "off") ? G->off : !strcmp(n, "ext") ? G->ext : !strcmp(n, "chOff") ? G->choff : G->chext;
          t[0] = v1; t[1] = v2;
        } else if (!strcmp(n, "off")) { o.xo = v1; o.yo = v2; o.hasx = 1; }
        else if (!strcmp(n, "ext")) { o.xe = v1; o.ye = v2; o.hasx = 1; }
        continue;
      }
      if (gsp && !strcmp(n, "cNvPr") && !strcmp(up(&st, 1), "nvGrpSpPr")) {
        DGroup *g = drw_group_find(s, gs[gsp - 1].id);
        const char *nm = xml_attr(&x, "name");
        if (g && nm && *nm) snprintf(g->name, sizeof g->name, "%s", nm);
        continue;
      }
      if (!strcmp(n, "twoCellAnchor") || !strcmp(n, "oneCellAnchor") || !strcmp(n, "absoluteAnchor")) {
        const char *e = xml_attr(&x, "editAs");
        robj_reset(&o);
        gsp = 0; ngrp_seen = 0;
        o.anchor = n[0] == 'o' ? AN_ONECELL : n[0] == 'a' ? AN_ABSOLUTE : (e && !strcmp(e, "oneCell")) ? AN_ONECELL : (e && !strcmp(e, "absolute")) ? AN_ABSOLUTE : AN_TWOCELL;
        o.have_to = n[0] == 't';
        para = 0;
      }
      else if (!strcmp(n, "graphicFrame")) o.kind = OB_CHART;
      else if (!strcmp(n, "pic")) o.kind = OB_PICTURE;
      else if (!strcmp(n, "sp") || !strcmp(n, "cxnSp")) { o.kind = OB_SHAPE; o.shape = n[0] == 'c' ? SK_LINE : SK_RECT; }
      else if (!strcmp(n, "slicer") && o.kind == OB_CHART) { const char *nm = xml_attr(&x, "name"); o.kind = OB_SLICER; snprintf(o.slname, sizeof o.slname, "%s", nm ? nm : ""); }
      else if (!strcmp(n, "chart") && o.kind == OB_CHART) { const char *id = xml_attr(&x, "id"); snprintf(o.rid, sizeof o.rid, "%s", id ? id : ""); }
      else if (!strcmp(n, "blip")) { const char *id = xml_attr(&x, "embed"); snprintf(o.rid, sizeof o.rid, "%s", id ? id : ""); }
      else if (!strcmp(n, "cNvPr")) {
        const char *nm = xml_attr(&x, "name"), *ds = xml_attr(&x, "descr");
        if (nm) snprintf(o.name, sizeof o.name, "%s", nm);
        if (ds && *ds) { free(o.descr); o.descr = str_dup(ds); }
      }
      else if (!strcmp(n, "cNvSpPr")) { const char *t = xml_attr(&x, "txBox"); if (t && atoi(t)) o.txbox = 1; }
      else if (!strcmp(n, "prstGeom")) {
        const char *p = xml_attr(&x, "prst");
        if (p) {
          if (!strcmp(p, "roundRect")) o.shape = SK_ROUNDRECT;
          else if (!strcmp(p, "ellipse")) o.shape = SK_ELLIPSE;
          else if (!strcmp(p, "triangle")) o.shape = SK_TRIANGLE;
          else if (!strcmp(p, "line") || !strncmp(p, "straightConnector", 17)) o.shape = SK_LINE;
          else if (!strcmp(p, "rightArrow")) o.shape = SK_ARROW;
          else if (o.shape != SK_LINE) o.shape = SK_RECT;
        }
      }
      else if (!strcmp(n, "custGeom")) o.custgeom = 1;
      else if (!strcmp(n, "path") && o.custgeom) { const char *w = xml_attr(&x, "w"), *h = xml_attr(&x, "h"); o.gw = w ? atol(w) : 0; o.gh = h ? atol(h) : 0; }
      else if (!strcmp(n, "pt") && o.custgeom) {
        const char *px = xml_attr(&x, "x"), *py = xml_attr(&x, "y");
        if (px && py && o.ink.len < 60000) buf_printf(&o.ink, "%s%ld,%ld", o.ink.len ? ";" : "", atol(px), atol(py));
      }
      else if (!strcmp(n, "xfrm") && within(&st, "spPr")) { const char *fh = xml_attr(&x, "flipH"), *fv = xml_attr(&x, "flipV"); o.flipx = fh && atoi(fh); o.flipy = fv && atoi(fv); }
      else if (!strcmp(n, "ext") && (!strcmp(up(&st, 1), "oneCellAnchor") || !strcmp(up(&st, 1), "absoluteAnchor"))) { const char *cx = xml_attr(&x, "cx"), *cy = xml_attr(&x, "cy"); o.extx = cx ? atol(cx) : 0; o.exty = cy ? atol(cy) : 0; }
      else if (!strcmp(n, "pos") && !strcmp(up(&st, 1), "absoluteAnchor")) { const char *px = xml_attr(&x, "x"), *py = xml_attr(&x, "y"); o.posx = px ? atol(px) : 0; o.posy = py ? atol(py) : 0; }
      else if (!strcmp(n, "noFill") && !strcmp(up(&st, 1), "spPr")) o.nofill = 1;
      else if (!strcmp(n, "noFill") && !strcmp(up(&st, 1), "ln") && !strcmp(up(&st, 2), "spPr")) o.noline = 1;
      else if (is_clr(n) && !strcmp(up(&st, 1), "solidFill") && !strcmp(up(&st, 2), "spPr")) o.fill = clr(&x);
      else if (is_clr(n) && !strcmp(up(&st, 1), "solidFill") && !strcmp(up(&st, 2), "ln") && !strcmp(up(&st, 3), "spPr")) o.line = clr(&x);
      else if (!strcmp(n, "ln") && !strcmp(up(&st, 1), "spPr")) { const char *w = xml_attr(&x, "w"); if (w) o.linew = (u16)(atol(w) / 127); }
      else if ((!strcmp(n, "tailEnd") || !strcmp(n, "headEnd")) && within(&st, "ln")) { const char *t = xml_attr(&x, "type"); if (t && strcmp(t, "none")) o.arrow = 1; }
      else if (!strcmp(n, "bodyPr")) { const char *a = xml_attr(&x, "anchor"); if (a) o.valign = (u8)(!strcmp(a, "ctr") ? 1 : !strcmp(a, "b") ? 2 : 0); }
      else if (!strcmp(n, "pPr") && within(&st, "txBody")) { const char *a = xml_attr(&x, "algn"); if (a) o.halign = (u8)(!strcmp(a, "ctr") ? 1 : !strcmp(a, "r") ? 2 : 0); }
      else if (!strcmp(n, "rPr") && within(&st, "txBody")) {
        const char *sz = xml_attr(&x, "sz"), *bb = xml_attr(&x, "b"), *ii = xml_attr(&x, "i");
        if (sz) o.fsize = (u16)(atoi(sz) / 10);
        if (bb) o.bold = (u8)atoi(bb);
        if (ii) o.italic = (u8)atoi(ii);
      }
      else if (is_clr(n) && !strcmp(up(&st, 1), "solidFill") && !strcmp(up(&st, 2), "rPr")) o.tcolor = clr(&x);
      else if (!strcmp(n, "p") && within(&st, "txBody")) { if (para++) buf_addc(&o.text, '\n'); }
      continue;
    }
    if (x.type == XT_TEXT) {
      const char *n = up(&st, 0), *p1 = up(&st, 1);
      long v = x.text.data ? atol(x.text.data) : 0;
      if (!strcmp(p1, "from") || !strcmp(p1, "to")) {
        DCorner *c = p1[0] == 'f' ? &o.from : &o.to;
        if (!strcmp(n, "col")) c->c = (i32)v;
        else if (!strcmp(n, "row")) c->r = (i32)v;
        else if (!strcmp(n, "colOff")) c->dx = (i32)(v / EMU);
        else if (!strcmp(n, "rowOff")) c->dy = (i32)(v / EMU);
      } else if (!strcmp(n, "t") && within(&st, "txBody")) buf_add(&o.text, x.text.data, x.text.len);
      continue;
    }
    if (x.type == XT_END) {
      const char *n = up(&st, 0);
      if (gsp && !strcmp(n, "xfrm") && !strcmp(up(&st, 1), "grpSpPr")) {
        /* the group's frame known: where its children's coordinates go on the sheet */
        RGrp *G = &gs[gsp - 1];
        double box[4];
        int k;
        if (gsp == 1 && o.have_to) {
          box[0] = sheet_col_px(s, o.from.c) + o.from.dx; box[1] = sheet_row_px(s, o.from.r) + o.from.dy;
          box[2] = sheet_col_px(s, o.to.c) + o.to.dx; box[3] = sheet_row_px(s, o.to.r) + o.to.dy;
        } else {
          RGrp *P = gsp > 1 ? &gs[gsp - 2] : NULL;
          for (k = 0; k < 2; k++) {
            box[k] = P ? P->p0[k] + (G->off[k] - P->ch0[k]) * P->k[k] : (double)G->off[k] / EMU;
            box[k + 2] = box[k] + (P ? G->ext[k] * P->k[k] : (double)G->ext[k] / EMU);
          }
        }
        for (k = 0; k < 2; k++) {
          G->p0[k] = box[k]; G->ch0[k] = (double)G->choff[k];
          G->k[k] = G->chext[k] > 0 ? (box[k + 2] - box[k]) / G->chext[k] : G->ext[k] > 0 ? (box[k + 2] - box[k]) / G->ext[k] : 1.0 / EMU;
        }
      }
      else if (gsp && !strcmp(n, "grpSp")) gsp--;
      else if (gsp && o.kind >= 0 && (!strcmp(n, "sp") || !strcmp(n, "cxnSp") || !strcmp(n, "pic") || !strcmp(n, "graphicFrame")) &&
               (!strcmp(up(&st, 1), "grpSp") || !strcmp(up(&st, 1), "Choice"))) {
        /* a child of a group: made now, then the next child read in the same anchor */
        RGrp *G = &gs[gsp - 1];
        i32 box[4];
        DObj *ob;
        RObj keep = o;
        box[0] = (i32)(G->p0[0] + (o.xo - G->ch0[0]) * G->k[0] + 0.5); box[1] = (i32)(G->p0[1] + (o.yo - G->ch0[1]) * G->k[1] + 0.5);
        box[2] = box[0] + (i32)(o.xe * G->k[0] + 0.5); box[3] = box[1] + (i32)(o.ye * G->k[1] + 0.5);
        if ((ob = finish_obj(b, z, rels, nrels, sheet, &o, box))) ob->group = G->id;
        robj_reset(&o);
        o.anchor = keep.anchor; o.have_to = keep.have_to; o.from = keep.from; o.to = keep.to;
        para = 0;
      }
      if ((!strcmp(n, "twoCellAnchor") || !strcmp(n, "oneCellAnchor") || !strcmp(n, "absoluteAnchor")) && o.kind >= 0 && !ngrp_seen) {
        finish_obj(b, z, rels, nrels, sheet, &o, NULL);
        robj_reset(&o);
      }
      stk_pop(&st);
    }
  }
  robj_reset(&o);
  buf_free(&o.text); buf_free(&o.ink);
  xml_free(&x);
  free(d);
  free(rels);
}

/* ------------------------------------------------------------------ loading: sparklines */
static u32 xcolor(Xml *x)
{
  const char *v = xml_attr(x, "rgb");
  if (v && strlen(v) >= 6) return FILLRGB(strtoul(v + strlen(v) - 6, NULL, 16));
  if ((v = xml_attr(x, "theme"))) { int t = atoi(v); if (t >= 4 && t <= 9) return FILLRGB(accents[t - 4]); return FILLRGB(t == 0 || t == 2 ? 0xFFFFFF : 0); }
  return FILLRGB(0);
}
static void read_background(Book *b, ZipReader *z, const char *path, int sheet, const char *rid)
{
  XRel *rels = NULL;
  int n = rels_of(z, path, &rels);
  const XRel *r = rel_by_id(rels, n, rid);
  size_t pl;
  u8 *pd = r ? part(z, r->target, &pl) : NULL;
  u32 w0, h0;
  int t = pd ? img_info(pd, (u32)pl, &w0, &h0) : IMG_NONE;
  Drawing *d;
  if ((t == IMG_PNG || t == IMG_JPEG) && (d = drw_of(b->sh[sheet]))) { blob_free(d->bg); d->bg = blob_new(pd, (u32)pl); d->bgtype = (u8)(t == IMG_PNG ? PIC_PNG : PIC_JPEG); }
  free(pd);
  free(rels);
}
static void read_sparks(Book *b, ZipReader *z, const char *path, int sheet)
{
  size_t len;
  u8 *d = part(z, path, &len);
  Xml x;
  Stk st;
  SparkGroup g;
  int ing = 0;
  char fbuf[400] = "", rbuf[64] = "";
  Sheet *s = b->sh[sheet];
  if (!d || (!strstr((const char *)d, "sparklineGroup") && !strstr((const char *)d, "<picture"))) { free(d); return; }
  memset(&st, 0, sizeof st);
  memset(&g, 0, sizeof g);
  xml_init(&x, (const char *)d, len);
  while (xml_next(&x) != XT_EOF) {
    if (x.type == XT_START) {
      const char *n = x.name;
      stk_push(&st, n);
      if (!strcmp(n, "sheetData")) { xml_skip(&x); stk_pop(&st); continue; }
      if (!strcmp(n, "picture") && st.sp == 2) { const char *id = xml_attr(&x, "id"); if (id) read_background(b, z, path, sheet, id); }
      if (!strcmp(n, "sparklineGroup")) {
        const char *v;
        spark_group_init(&g, SP_LINE);
        ing = 1;
        if ((v = xml_attr(&x, "type"))) g.type = (u8)(!strcmp(v, "column") ? SP_COLUMN : !strcmp(v, "stacked") ? SP_WINLOSS : SP_LINE);
        if ((v = xml_attr(&x, "displayEmptyCellsAs"))) g.blanks = (u8)(!strcmp(v, "zero") ? 1 : !strcmp(v, "span") ? 2 : 0);
        g.markers = (u8)((v = xml_attr(&x, "markers")) && (atoi(v) || !strcmp(v, "true")));
        g.high = (u8)((v = xml_attr(&x, "high")) && (atoi(v) || !strcmp(v, "true")));
        g.low = (u8)((v = xml_attr(&x, "low")) && (atoi(v) || !strcmp(v, "true")));
        g.first = (u8)((v = xml_attr(&x, "first")) && (atoi(v) || !strcmp(v, "true")));
        g.last = (u8)((v = xml_attr(&x, "last")) && (atoi(v) || !strcmp(v, "true")));
        g.negative = (u8)((v = xml_attr(&x, "negative")) && (atoi(v) || !strcmp(v, "true")));
        g.axis = (u8)((v = xml_attr(&x, "displayXAxis")) && (atoi(v) || !strcmp(v, "true")));
        if ((v = xml_attr(&x, "lineWeight"))) g.weight = (u16)(atof(v) * 100 + 0.5);
        if ((v = xml_attr(&x, "minAxisType"))) g.minmode = (u8)(!strcmp(v, "group") ? 1 : !strcmp(v, "custom") ? 2 : 0);
        if ((v = xml_attr(&x, "maxAxisType"))) g.maxmode = (u8)(!strcmp(v, "group") ? 1 : !strcmp(v, "custom") ? 2 : 0);
        if ((v = xml_attr(&x, "manualMin"))) g.minv = atof(v);
        if ((v = xml_attr(&x, "manualMax"))) g.maxv = atof(v);
      } else if (ing) {
        if (!strcmp(n, "colorSeries")) g.color = xcolor(&x);
        else if (!strcmp(n, "colorNegative")) g.negcolor = xcolor(&x);
        else if (!strcmp(n, "colorMarkers")) g.markcolor = xcolor(&x);
        else if (!strcmp(n, "colorFirst")) g.firstcolor = xcolor(&x);
        else if (!strcmp(n, "colorLast")) g.lastcolor = xcolor(&x);
        else if (!strcmp(n, "colorHigh")) g.highcolor = xcolor(&x);
        else if (!strcmp(n, "colorLow")) g.lowcolor = xcolor(&x);
        else if (!strcmp(n, "sparkline")) { fbuf[0] = 0; rbuf[0] = 0; }
      }
      continue;
    }
    if (x.type == XT_TEXT && ing) {
      const char *n = up(&st, 0);
      if (!strcmp(n, "f") && within(&st, "sparkline")) snprintf(fbuf, sizeof fbuf, "%.*s", (int)x.text.len, x.text.data ? x.text.data : "");
      else if (!strcmp(n, "sqref") && within(&st, "sparkline")) snprintf(rbuf, sizeof rbuf, "%.*s", (int)x.text.len, x.text.data ? x.text.data : "");
      continue;
    }
    if (x.type == XT_END) {
      const char *n = up(&st, 0);
      if (!strcmp(n, "sparkline") && ing && rbuf[0]) {
        u16 sh; Rect r; u8 fl;
        Spark *ns;
        if (parse_ref(b, rbuf, &sh, &r, &fl) && (ns = (Spark *)realloc(g.sp, (size_t)(g.n + 1) * sizeof(Spark)))) {
          g.sp = ns;
          g.sp[g.n].r = r.r1; g.sp[g.n].c = r.c1;
          g.sp[g.n].f = fbuf[0] ? parse_f(b, sheet, fbuf) : NULL;
          g.n++;
        }
      } else if (!strcmp(n, "sparklineGroup") && ing) {
        Drawing *dr = drw_of(s);
        SparkGroup *ng;
        ing = 0;
        if (g.n && dr && (ng = (SparkGroup *)realloc(dr->g, (size_t)(dr->ng + 1) * sizeof(SparkGroup)))) { dr->g = ng; dr->g[dr->ng++] = g; }
        else spark_group_clear(&g);
        memset(&g, 0, sizeof g);
      }
      stk_pop(&st);
    }
  }
  if (ing) spark_group_clear(&g);
  xml_free(&x);
  free(d);
}
void xd_load_sheet(Book *b, ZipReader *z, const char *sheetpath, int sheet)
{
  XRel *rels = NULL;
  int n = rels_of(z, sheetpath, &rels), k;
  for (k = 0; k < n; k++) if (!strcmp(rels[k].type, "drawing")) read_drawing(b, z, rels[k].target, sheet);
  free(rels);
  read_sparks(b, z, sheetpath, sheet);
}
