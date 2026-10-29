/*
 * slicer.h - slicers (Insert > Slicer, Table Design and PivotTable Analyze > Insert Slicer): a box
 * floating over the sheet (an OB_SLICER object of the drawing, drawing.h) listing the items of one
 * field of a table or a pivot table as buttons. Clicking a button filters: a table through its
 * AutoFilter (the column shows the items selected), a pivot table through a filter of its own on
 * that field (LPivot.sfilters). As in GridCraft a click selects that item alone (clicking the one
 * selected item again clears the filter); with Multi-Select on (or a held Shift), clicks add and
 * take away items. Clear Filter selects every item again.
 */
#ifndef LEDGER_SLICER_H
#define LEDGER_SLICER_H

#include "drawing.h"

enum { SS_ASC, SS_DESC, SS_SOURCE };     /* item order: A to Z, Z to A, as in the data */
typedef struct Slicer {
  char name[48];                   /* the slicer's name, its cache's "Slicer_<name>" */
  char caption[128];               /* the header (UTF-8) */
  char source[256];                /* the table's or pivot table's name */
  u8 onpivot;                      /* filters a pivot table (else a table) */
  char field[256];                 /* the column or field */
  u8 cols;                         /* button columns (1..20) */
  u8 sort;                         /* SS_ */
  u8 header;                       /* the header shown */
  u8 multi;                        /* Multi-Select on */
  char style[32];                  /* "SlicerStyleLight1" */
  char **sel; int nsel;            /* the items selected (labels) */
  u8 all;                          /* every item selected: no filter */
} Slicer;

Slicer *slicer_new(void);
void    slicer_free(Slicer *s);
Slicer *slicer_copy(const Slicer *s);
/* the slicer's items in its order, and which are selected (malloc'd; free with pivot_strings_free
   and free); how many */
int     slicer_items(Book *b, const Slicer *s, char ***items, u8 **on);
/* a slicer of a table's column (onpivot 0) or of a pivot table's field, with its top left at
   sheet r, c: its object's index, -1 if the source or field isn't there. An undo step */
int     book_slicer_add(Book *b, int sheet, int onpivot, const char *source, const char *field, i32 r, i32 c);
/* item k of slicer object obj clicked (multi: added or taken away, as with Multi-Select); every
   item selected again; the settings replaced by those of ns (then applied). Undo steps */
int     book_slicer_click(Book *b, int sheet, int obj, int k, int multi);
int     book_slicer_clear(Book *b, int sheet, int obj);
int     book_slicer_set(Book *b, int sheet, int obj, const Slicer *ns);
/* the slicer and the filter it made gone */
int     book_slicer_delete(Book *b, int sheet, int obj);
/* the slicer's selection made the filter (after loading, or when the source changed) */
void    slicer_apply(Book *b, Slicer *s);
void    slicers_apply_all(Book *b);
/* a new slicer's name for field ("Region", "Region 2" ...) unique in the book */
void    slicer_unique_name(Book *b, const char *field, char *out, size_t cap);
/* the slicer drawn in w x h pixels (zoom per cent for text) */
int     slicer_render(Book *b, const Slicer *s, int w, int h, int zoom, TextWidthFn tw, void *ctx, PrimList *out);
/* what is at x, y of the slicer drawn w x h: an item (its index), or SH_ */
enum { SH_NONE = -1, SH_HEADER = -2, SH_CLEAR = -3, SH_MULTI = -4 };
int     slicer_hit(Book *b, const Slicer *s, int w, int h, int zoom, i32 x, i32 y);

#endif
