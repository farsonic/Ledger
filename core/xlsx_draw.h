/*
 * xlsx_draw.h - charts, pictures, shapes and sparklines in .xlsx files (xlsx.c calls these):
 * each sheet with objects gets a DrawingML drawing part (xl/drawings/drawingN.xml, objects
 * anchored by twoCellAnchor), charts as xl/charts/chartN.xml (c:chartSpace; Excel 2016's
 * histogram, box & whisker, waterfall, funnel, treemap and sunburst as chartExN.xml), pictures in
 * xl/media; sparklines go in the worksheet's extLst (x14:sparklineGroups).
 */
#ifndef LEDGER_XLSX_DRAW_H
#define LEDGER_XLSX_DRAW_H

#include "book.h"
#include "zip.h"

/* the workbook's file name ("Book1.xlsx") a PivotChart's link names; "" for Book1.xlsx */
extern char xd_book_name[128];
/* saving */
void xd_content_types(Book *b, Buf *o);          /* the parts' content types */
int  xd_has_drawing(Book *b, int si);            /* sheet si has objects (a drawing part) */
int  xd_has_rels(Book *b, int si);               /* it needs relationships of its own (a drawing, a background) */
void xd_sheet_drawing_ref(Book *b, int si, Buf *o);   /* <drawing r:id="rIdD1"/>, if it has one */
void xd_sheet_picture(Book *b, int si, Buf *o);
void xd_sheet_ext(Book *b, int si, Buf *o);      /* the worksheet's extLst (sparklines), if any */
void xd_sheet_rels(Book *b, int si, Buf *o);     /* the sheet's relationship to its drawing */
void xd_write_parts(Book *b, int si, ZipWriter *w);   /* the drawing, its charts and pictures */
/* loading: sheet's drawing (through its relationships) and its sparklines */
void xd_load_sheet(Book *b, ZipReader *z, const char *sheetpath, int sheet);

/* form controls (formctl.c): spin buttons and scroll bars */
#include "xml.h"
int   fc_count(Sheet *s);
void  fc_xlsx_types(Book *b, Buf *o);
void  fc_xlsx_rels(Book *b, int si, Buf *o);             /* rIdC1... to the ctrlProp parts */
void  fc_xlsx_controls(Book *b, int si, Buf *o);         /* the sheet's <controls> */
void  fc_xlsx_vml(Book *b, int si, Buf *o);              /* their shapes in the VML drawing */
char *fc_xlsx_prop(Book *b, int si, int k, int *number); /* control k's ctrlProp part (malloc'd), NULL past the last */
void  fc_xlsx_read_controls(Book *b, int sheet, Xml *x);
void  fc_xlsx_read_prop(Book *b, int sheet, const char *rid, const char *data, size_t len);
void  fc_xlsx_read_done(Book *b, int sheet);
#endif
