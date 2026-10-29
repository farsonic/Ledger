/*
 * splitpane.h - View > Split (Sheet.splitx, splity, stop, sleft, apane): the panes' bars kept in
 * .xlsx as Excel keeps them, a pane element with state="split" (its xSplit and ySplit in twentieths
 * of a point from the window's edge, the row and column headings included), the bottom right pane's
 * top left cell, the active pane; the top left pane's first cell is the sheet view's topLeftCell.
 */
#ifndef LEDGER_SPLITPANE_H
#define LEDGER_SPLITPANE_H

#include "book.h"
#include "xml.h"

#define SPLIT_HEADW 25            /* the headings' size Excel counts in xSplit, ySplit (pixels) */
#define SPLIT_HEADH 20

int  sheet_is_split(const Sheet *s);            /* split (and not frozen) */
void sheet_unsplit(Sheet *s);
/* bars at x, y (pixels at 100% from the cells' top left; 0 none); the panes below and right start
   at top, left, the top and left ones at stop, sleft */
void sheet_split(Sheet *s, i32 x, i32 y, i32 stop, i32 sleft, i32 top, i32 left);
const char *split_pane_name(const Sheet *s);   /* "bottomRight"... for the active pane; NULL not split */
void xlsx_split_pane(Sheet *s, Xml *x);          /* (loading) a <pane> that isn't frozen */
void xlsx_split_write(Sheet *s, Buf *o);         /* (saving) its <pane> */

#endif
