/*
 * htmlout.h - File > Export > Web Page: the workbook (or one sheet) as an HTML page of tables,
 * with the cells' looks as inline styles (GridCraft's file.exportHtml).
 */
#ifndef LEDGER_HTMLOUT_H
#define LEDGER_HTMLOUT_H

#include "book.h"

/* every visible sheet (sheet -1) or one sheet, as a page (UTF-8, malloc'd); NULL out of memory */
char *html_book(Book *b, int sheet);

#endif
