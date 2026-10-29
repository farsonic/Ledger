/*
 * xls.h - older and other spreadsheets read: Excel 97-2003 workbooks (.xls, BIFF8 records in a
 * compound document, xls.c) and OpenDocument spreadsheets (.ods, ods.c). Read only: Ledger saves
 * .xlsx.
 */
#ifndef LEDGER_XLS_H
#define LEDGER_XLS_H

#include "book.h"

/* a workbook from .xls bytes; NULL with a reason in err if it isn't one or can't be read */
Book *xls_load(const u8 *data, size_t len, char *err, size_t errcap);
/* a workbook from .ods bytes (a zip holding content.xml); NULL with a reason in err */
Book *ods_load(const u8 *data, size_t len, char *err, size_t errcap);
/* whether .ods bytes (a zip whose mimetype is an OpenDocument spreadsheet) */
int   ods_is(const u8 *data, size_t len);

#endif
