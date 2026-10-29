/*
 * tablexml.h - tables and pivot tables in .xlsx files (xlsx.c's hooks): xl/tables/tableN.xml,
 * xl/pivotTables/pivotTableN.xml with its pivotCacheDefinition and pivotCacheRecords, their
 * relationships and content types, written so Excel and LibreOffice open them, and read back
 * from files Excel, LibreOffice or GridCraft wrote.
 *
 * Tables are numbered through the book in sheet order (table1.xml ...), pivot tables likewise,
 * each pivot table with a cache of its own of the same number.
 */
#ifndef LEDGER_TABLEXML_H
#define LEDGER_TABLEXML_H

#include "book.h"
#include "zip.h"

/* the file form of a formula (xlsx.c's: no "=", newer functions prefixed) */
typedef void (*FileFormulaFn)(Book *b, int sheet, const Formula *f, Buf *out);

/* writing */
void tablexml_content_types(Book *b, Buf *o);         /* <Override> for each part */
void tablexml_workbook(Book *b, Buf *o);              /* <pivotCaches> after calcPr */
void tablexml_workbook_rels(Book *b, Buf *o);         /* the caches' relationships */
int  tablexml_has_rels(Book *b, int si);              /* sheet si has table or pivot parts */
void tablexml_sheet_rels(Book *b, int si, Buf *o);    /* its relationships to them */
void tablexml_sheet_parts(Book *b, int si, Buf *o);   /* <tableParts> before </worksheet> */
int  tablexml_filter_in_table(Sheet *s);              /* the sheet's AutoFilter is a table's (written in the table) */
void tablexml_parts(Book *b, ZipWriter *w, FileFormulaFn ff);   /* the parts themselves */

/* slicers: the defined names of their caches (<definedName>s), the worksheet's extLst entries */
int  tablexml_has_names(Book *b);
void tablexml_names(Book *b, Buf *o);
int  tablexml_has_sheet_ext(Book *b, int si);
void tablexml_sheet_ext(Book *b, int si, Buf *o);     /* <ext> elements, inside the sheet's <extLst> */

/* reading: a sheet relationship of type ("table", "pivotTable") to part target */
void tablexml_read_rel(Book *b, ZipReader *z, const char *type, const char *target, int sheet);
/* (xlsx_draw.c) the slicer named name read for sheet, handed over to its frame in the drawing */
struct Slicer *tablexml_slicer_take(int sheet, const char *name);
/* after every sheet was read: structured references tidied, pivot reports worked out */
void tablexml_loaded(Book *b);

#endif
