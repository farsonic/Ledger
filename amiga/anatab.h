/*
 * anatab.h - the Analysis commands (anatab.c): Data > Forecast Sheet, Data > Solver and Data
 * Analysis, Home > Format > Lock Cell, Review > Allow Edit Ranges and Insert > Checkbox, over the
 * core's analysis.h; and the checkbox cells the grid draws and toggles.
 */
#ifndef LEDGER_ANATAB_H
#define LEDGER_ANATAB_H

#include "ledger.h"

/* the commands (wave 3's range for this work: 4200-4399) */
enum {
  ID_AN_FIRST = 4200,
  ID_AN_FORECAST = ID_AN_FIRST, ID_AN_SOLVER, ID_AN_TOOLPAK, ID_AN_LOCK, ID_AN_EDITRANGES, ID_AN_CHECKBOX, ID_AN_TOGGLE,
  ID_AN_LAST
};
/* (main.c) one of the commands above */
void anatab_command(ULONG id);
/* (datatab.c) cells of a protected sheet in an Allow Edit Range behind a password: the password
   asked for; 1 if they may now be changed */
int  anatab_unlock(Book *b, int sheet, Rect a);
/* (grid.c) a checkbox cell's box drawn in x0..x1, y0..y1 (box and tick pens); 1 if it is one */
int  anatab_draw_check(struct RastPort *rp, Book *b, int sheet, i32 r, i32 c, LONG x0, LONG y0, LONG x1, LONG y1, LONG penbox, LONG pentick, LONG penback);

/* the menus' items (in main.c's Data, Format, Insert and Review menus) */
#define AN_DATA_ITEMS \
  BAR, I("Forecast Sheet...", 0, ID_AN_FORECAST), I("Solver...", 0, ID_AN_SOLVER), I("Data Analysis...", 0, ID_AN_TOOLPAK),
#define AN_FORMAT_ITEMS \
  BAR, I("Lock Cell", 0, ID_AN_LOCK),
#define AN_INSERT_ITEMS \
  BAR, I("Checkbox", 0, ID_AN_CHECKBOX),
#define AN_REVIEW_ITEMS \
  BAR, I("Allow Edit Ranges...", 0, ID_AN_EDITRANGES),

#endif
