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
/* the same, each column's dates read in the order its values show (31/05/2026: day first, 12/31/2026:
   month first, as GridCraft and Excel's import): *either_col the first column whose dates all read
   either way (they follow Ledger's date order), *both_col the first with both kinds (its dates kept as
   text); -1 for none. Either pointer may be NULL */
Book *csv_load_report(const char *data, size_t len, char delim, int *either_col, int *both_col);
/* one sheet as CSV text (malloc'd): numbers in full, dates as shown, text quoted when needed */
char *csv_save(Book *b, int sheet, char delim);

/* SYLK (.slk, sylk.c): sylk_is the data starts with an ID record; sylk_load one sheet from it (NULL if
   it isn't SYLK), *skipped the records it couldn't read (may be NULL); sylk_save one sheet as SYLK
   text (malloc'd, Latin-1 as Excel writes it) */
int   sylk_is(const char *data, size_t len);
Book *sylk_load(const char *data, size_t len, int *skipped);
char *sylk_save(Book *b, int sheet);
/* a formula's text (without '=') between R1C1 and A1 references for the cell at row, col (0-based):
   malloc'd, NULL out of memory */
char *sylk_r1c1_to_a1(const char *f, long row, long col);
char *sylk_a1_to_r1c1(const char *f, long row, long col);
/* a web page's first table as a sheet (htmlin.c; a file on disk, no network): html_is the bytes are a
   page with a table; html_load reads it (NULL if not), sheetname the page's <x:Name> or "" */
int   html_is(const char *data, size_t len);
Book *html_load(const char *data, size_t len, char *sheetname, size_t cap);
#endif
