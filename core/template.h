/*
 * template.h - the workbooks the File page offers to start from: a blank one and a few ready to
 * fill in, each a working sheet with formulas, number formats and styles.
 */
#ifndef LEDGER_TEMPLATE_H
#define LEDGER_TEMPLATE_H

#include "book.h"

enum { TPL_BLANK, TPL_BUDGET, TPL_INVOICE, TPL_EXPENSES, TPL_GRADES, TPL_N };
/* a new workbook made from template t (nothing to undo); NULL out of memory */
Book *book_template(int t);

#endif
