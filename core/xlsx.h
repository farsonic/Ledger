/*
 * xlsx.h - workbooks in and out: Excel's .xlsx (Office Open XML) and CSV.
 */
#ifndef LEDGER_XLSX_H
#define LEDGER_XLSX_H

#include "book.h"

/* a workbook from .xlsx bytes; NULL with a reason in err if it isn't one or is damaged */
Book *xlsx_load(const u8 *data, size_t len, char *err, size_t errcap);
/* the workbook as .xlsx bytes (malloc'd, *len its size); NULL out of memory */
u8   *xlsx_save(Book *b, size_t *len);
/* CSV: delim 0 guesses (comma, semicolon or tab). Fields are read as if typed (numbers, dates) */
Book *csv_load(const char *data, size_t len, char delim);
/* one sheet as CSV text (malloc'd): numbers in full, dates as shown, text quoted when needed */
char *csv_save(Book *b, int sheet, char delim);

#endif
