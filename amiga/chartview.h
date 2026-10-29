/*
 * chartview.h - charts, pictures, shapes, ink and sparklines on the sheet (chartview.c): drawn by
 * the grid over its cells into its back buffer, selected, moved and resized with the mouse, drawn
 * with the pen. The grid hands over what it knows about its layout in a GridView.
 */
#ifndef LEDGER_CHARTVIEW_H
#define LEDGER_CHARTVIEW_H

#include "ledger.h"
#include "../core/drawing.h"

typedef struct GridView {
  Object *obj;
  struct RastPort *rp;           /* where to draw (the back buffer, or the window) */
  LONG ox, oy;                   /* where the grid's corner is in rp */
  Book *book; int sheet;
  LONG zoom;                     /* per cent */
  LONG hw, hh, w, h;             /* the headings' size, the grid's */
  void *g;                       /* the grid's own data, for these: */
  LONG (*colx)(void *g, i32 c);  /* column c's left edge and row r's top, from the grid's corner */
  LONG (*rowy)(void *g, i32 r);
  int  (*cell_at)(void *g, LONG x, LONG y, i32 *r, i32 *c);   /* 0: a cell */
  LONG (*pen)(void *g, ULONG rgb);
  void (*notify)(void *g, int changed);   /* the selection changed (0) or the workbook (1) */
  void (*redraw)(void *g);
} GridView;

enum { TOOL_SELECT, TOOL_LASSO, TOOL_PEN, TOOL_ERASER, TOOL_PLACE };
typedef struct ObjView ObjView;
ObjView *ov_new(void);
void ov_free(ObjView *ov);
void ov_cleanup(ObjView *ov, Object *obj);      /* (MUIM_Cleanup) pens and pictures given back */
void ov_fonts_free(void);                       /* (at the end) the chart text fonts */
void ov_sparks(ObjView *ov, GridView *gv);      /* sparklines in their cells (after the cells) */
void ov_draw(ObjView *ov, GridView *gv);        /* the objects over the cells, the selection's handles */
int  ov_mouse(ObjView *ov, GridView *gv, struct IntuiMessage *im, LONG x, LONG y);   /* 1: taken */
int  ov_key(ObjView *ov, GridView *gv, UWORD code, UWORD qual);                       /* 1: taken */
int  ov_busy(ObjView *ov);                      /* an object selected or a tool in use (the grid leaves keys to us) */

/* (charttab.c) the selection and the tools */
int  ov_selected(ObjView *ov);                  /* the object selected (index), -1 none */
int  ov_selection(ObjView *ov, int *list, int max);
void ov_select(ObjView *ov, int i);             /* -1: none */
int  ov_elem(ObjView *ov);                      /* the selected chart's part (CE_..., CE_SERIES + k) */
void ov_set_elem(ObjView *ov, int e);
void ov_tool(ObjView *ov, int tool, ULONG color, int width, int shape);
int  ov_tool_now(ObjView *ov, ULONG *color, int *width);
int  ov_last_ink(ObjView *ov);                  /* the last ink stroke drawn (index), -1 */

/* primitives painted into a rastport at x, y (also the chart previews' way of drawing) */
typedef LONG (*PenFn)(void *ctx, ULONG rgb);
void ov_paint(struct RastPort *rp, const PrimList *pl, LONG x, LONG y, PenFn pen, void *ctx);
i32  ov_text_width(void *ctx, const char *utf8, int px, int bold);
/* a shape drawn into the box (the Insert dialogs' and the grid's) */
void ov_shape(struct RastPort *rp, const DObj *o, LONG x0, LONG y0, LONG x1, LONG y1, LONG zoom, PenFn pen, void *ctx);

#endif
