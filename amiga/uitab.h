/*
 * uitab.h - the ui work's commands (ids 4400-4599): the Formulas tab's More Functions lists
 * (Statistical, Engineering, Cube, Information, Compatibility, Web), Group / Regroup / Ungroup for
 * drawn objects (Page Layout > Arrange, the Insert menu's objects), View > Split, and the formula
 * bar's greyed formula for a cell a dynamic array spilled into.
 */
#ifndef LEDGER_UITAB_H
#define LEDGER_UITAB_H

#include "../core/book.h"

enum {
  ID_UI_FIRST = 4400,
  ID_FN_STAT = ID_UI_FIRST, ID_FN_ENG, ID_FN_CUBE, ID_FN_INFO, ID_FN_COMPAT, ID_FN_WEB,   /* (a function picked: main.c types it) */
  ID_UI_GROUPDD, ID_UI_GROUP, ID_UI_REGROUP, ID_UI_UNGROUP,
  ID_UI_LAST
};
#define ID_FN_SUBLAST ID_FN_WEB

void ui_command(unsigned long id, long v);
void ui_split_toggle(void);                    /* View > Split (viewtab.c) */
/* the formula of the dynamic array the cell r, c was spilled into from (Latin-1), for the formula
   bar to show greyed; 0 if the cell isn't a spilled one */
int  ui_spill_formula(Book *b, int sheet, i32 r, i32 c, char *out, size_t cap);
/* the formula bar's other page: text in the disabled colour (a spilled cell's formula) */
APTR ui_fxghost(void);
void ui_fx_ghost(APTR fxbar, int on, const char *text);    /* shown (the page with it) or not */
void ui_class_free(void);

#endif
