/*
 * pdfout.h - a workbook exported as PDF (File > Export > PDF): each sheet's pages as its page setup
 * lays them out (pagesetup.h), with headers and footers, gridlines and headings if asked for, and
 * the cells as the grid shows them: fonts, fills, borders, alignment, wrapping, number formats,
 * conditional formatting. The TrueType fonts are embedded (when their licences allow), so the PDF
 * looks the same everywhere.
 *
 * The file writing (objects, streams, fonts, Windows-1252 text) comes from Quill's core/pdf.c.
 */
#ifndef LEDGER_PDFOUT_H
#define LEDGER_PDFOUT_H

#include "book.h"
#include "fonts.h"

enum { PDF_SHEET, PDF_BOOK, PDF_SELECTION };
typedef struct {
  int what;                       /* PDF_: the active sheet, every visible sheet, or the selection */
  const char *file, *path;        /* the workbook's file name and drawer, for &F and &Z (UTF-8) */
  double now;                     /* &D and &T (a date serial) */
  int formulas;                   /* formulas shown instead of their results */
  int ignore_area;                /* the print areas left out */
} PdfOptions;

/* the PDF (malloc'd, *len its size); NULL out of memory or with no fonts at all */
u8  *pdf_book(Book *b, FontSet *fs, const PdfOptions *o, size_t *len);
int  pdf_save_book(Book *b, FontSet *fs, const PdfOptions *o, const char *path);
/* how many pages it would have */
int  pdf_count_pages(Book *b, const PdfOptions *o);

#endif
