/*
 * drawing.h - what floats over a sheet's cells: charts, pictures and shapes (text boxes, lines,
 * arrows, icons, ink), and the sparklines drawn inside cells. Each sheet keeps them in its
 * Drawing (Sheet.drw, NULL while it has none).
 *
 * An object is anchored to the grid as Excel anchors it: its top-left and bottom-right corners are
 * each a cell and an offset into it (pixels at 100%), so it moves and stretches with its rows and
 * columns. A chart keeps formulas (references such as Sheet1!$B$2:$B$7) for each series' name,
 * categories and values, recomputed from the cells whenever it is drawn; they follow rows and
 * columns inserted or deleted like any other formula.
 *
 * Charts and sparklines are drawn (chart.c, spark.c) as a list of simple primitives - rectangles,
 * polylines, polygons, ellipses and text - which the Amiga front end paints with graphics.library
 * and the native tests can inspect. Portable C99.
 */
#ifndef LEDGER_DRAWING_H
#define LEDGER_DRAWING_H

#include "book.h"

/* ------------------------------------------------------------------ objects */
typedef struct { i32 r, c; i32 dy, dx; } DCorner;     /* a cell, and pixels down and across into it (at 100%) */
enum { AN_TWOCELL, AN_ONECELL, AN_ABSOLUTE };          /* move and size with cells, move only, neither */
enum { OB_CHART, OB_PICTURE, OB_SHAPE, OB_SLICER, OB_CONTROL };
enum { SK_RECT, SK_ROUNDRECT, SK_ELLIPSE, SK_TRIANGLE, SK_LINE, SK_ARROW, SK_TEXTBOX, SK_ICON, SK_INK, SK_N };
enum { PIC_NONE, PIC_PNG, PIC_JPEG };

/* a picture's file bytes, shared by the copies undo keeps (counted) */
typedef struct { int refs; u32 len; u8 *data; } Blob;
Blob *blob_new(const u8 *data, u32 len);   /* the bytes copied; NULL out of memory */
Blob *blob_ref(Blob *b);
void  blob_free(Blob *b);

typedef struct Chart Chart;
struct Slicer;                     /* slicer.h */

typedef struct {
  u8 kind, shape, anchor;          /* OB_, SK_ (shapes), AN_ */
  u8 flipx, flipy;                 /* lines and arrows: from the bottom (flipy) or right (flipx) corner */
  u32 id;                          /* the drawing's object number (cNvPr id) */
  char name[48];                   /* "Chart 1", "Picture 2" (UTF-8) */
  char *descr;                     /* alt text (UTF-8), or NULL */
  DCorner from, to;
  u32 fill, line;                  /* FILL_ON | rgb, or 0 none */
  u16 linew;                       /* outline width, hundredths of a point (0: the default, 0.75pt) */
  char *text;                      /* a shape's text (UTF-8); SK_ICON: its icon's number; SK_INK: "x,y;x,y;..." pixels (at 100%) from the top left */
  u16 fsize;                       /* text size, tenths of a point (0: 11pt) */
  u8 bold, italic, halign, valign; /* text: halign 0 left 1 centre 2 right; valign 0 top 1 middle 2 bottom */
  u32 tcolor;                      /* text colour, FILL_ON | rgb (0: automatic) */
  u8 wordart;                      /* SK_TEXTBOX: WordArt (Insert > WordArt): large decorative text, no box of its own */
  u32 toutline, tshadow;           /* text outline and shadow colours, FILL_ON | rgb (0 none) */
  u16 toutw;                       /* the text outline's width, hundredths of a point */
  Blob *pic; u8 pictype;           /* OB_PICTURE: the file (PNG or JPEG) */
  Chart *chart;                    /* OB_CHART */
  struct Slicer *slicer;           /* OB_SLICER (slicer.h) */
  u32 group;                       /* the innermost group (DGroup.id) it is in, 0 none */
  /* OB_CONTROL (formctl.c): a form control, its value 0..30000 as Excel's */
  u8 ctl;                          /* CT_ */
  i32 cval, cmin, cmax, cinc, cpage;
  Formula *link;                   /* the cell it sets (a reference), or NULL */
} DObj;

/* a group of objects (Page Layout > Arrange > Group; a grpSp in the file): its members are the
   objects whose group is it or one of the groups inside it, kept together in the stacking order.
   Groups nest through parent (0: a top-level group) */
typedef struct { u32 id, parent; char name[48]; } DGroup;

/* Ledger's own icons for Insert > Icons (SK_ICON shapes): drawn on a 16 x 16 grid by commands
   L x0 y0 x1 y1 (line), R (box), F (filled box), O cx cy r (circle), E (disc), P n x y... (lines
   joining n points), Q n x y... (a filled polygon) */
#define DRW_NICONS 16
extern const char *const drw_icon_names[DRW_NICONS];
const char *drw_icon_desc(int k);         /* NULL if k isn't one */
int   drw_icon_by_name(const char *name);  /* -1 if none */

/* ------------------------------------------------------------------ form controls (formctl.c) */
/* Excel's spin button and scroll bar (Developer > Insert, Form Controls): floating objects that set a
   number in their linked cell; up or across from min to max by inc (a scroll bar's track by page).
   Its value is the linked cell's while that holds a number in range. Saved in .xlsx as Excel writes
   them (a ctrlProp part, the sheet's <controls> and a VML shape); buttons, which run macros, are left
   out with scripting */
enum { CT_SPIN, CT_SCROLL };
#define CTL_MAX 30000
enum { CP_NONE, CP_DOWN, CP_UP, CP_PAGEDOWN, CP_PAGEUP, CP_THUMB };   /* the parts: value down or up */
/* a control of kind ctl at cell r, c (Excel's size), 0..100 by 1 (page 10); its index or -1 (a
   protected sheet), an undo step */
int   book_control_add(Book *b, int sheet, int ctl, i32 r, i32 c);
/* Format Control: link ("" none, else a cell such as $B$1 or Sheet2!B1), value and limits; the linked
   cell gets the value. 0 with *why when something is out of range (nothing changes) */
int   book_control_set(Book *b, int sheet, int i, const char *link, i32 val, i32 min, i32 max, i32 inc, i32 page, const char **why);
i32   control_value(Book *b, int sheet, const DObj *o);
void  control_link_text(Book *b, int sheet, const DObj *o, char *out, size_t cap);   /* "" none */
int   control_horizontal(const DObj *o, i32 w, i32 h);   /* laid out across (wider than high) */
/* the part at x, y of a control drawn w x h; clicking it (book_control_step) changes the value by its
   step or page; 1 if the value changed (an undo step) */
int   control_part(const DObj *o, i32 w, i32 h, i32 x, i32 y, i32 val);
int   book_control_step(Book *b, int sheet, int i, int part);

/* ------------------------------------------------------------------ WordArt (Insert > WordArt) */
/* GridCraft's ten WordArt styles (its own palette): text colour, outline (and its width, hundredths
   of a point), shadow (FILL_ON | rgb, 0 none), bold, italic and size (tenths of a point) */
typedef struct { const char *name; u32 fill, outline; u16 outw; u32 shadow; u8 bold, italic; u16 size; } WordArtStyle;
#define DRW_NWORDART 10
extern const WordArtStyle drw_wordart_styles[DRW_NWORDART];
#define DRW_WORDART_TEXT "Your text here"
/* a style's look given to a WordArt (its text and place kept) */
void  drw_wordart_style(DObj *o, int style);
/* a WordArt of style with text (NULL: "Your text here") at sheet pixel x, y, sized to its text; its
   index, or -1 when the sheet is protected without Edit objects allowed (an undo step) */
int   book_wordart_add(Book *b, int sheet, int style, const char *text, i32 x, i32 y);

/* ------------------------------------------------------------------ charts */
enum {
  CK_COL, CK_COLSTACK, CK_COL100, CK_BAR, CK_BARSTACK, CK_BAR100, CK_LINE, CK_LINEMARK, CK_LINESTACK,
  CK_PIE, CK_DOUGHNUT, CK_AREA, CK_AREASTACK, CK_AREA100, CK_SCATTER, CK_SCATTERLINE, CK_BUBBLE, CK_RADAR,
  CK_HISTOGRAM, CK_WATERFALL, CK_FUNNEL, CK_TREEMAP, CK_SUNBURST, CK_BOXWHISKER, CK_STOCK, CK_COMBO, CK_N
};
#define CK_SAME 0xFF               /* a series drawn as its chart is */
enum { LG_NONE, LG_BOTTOM, LG_TOP, LG_LEFT, LG_RIGHT };
/* the parts of a chart, for selecting one (the Format tab) and hit testing */
enum { CE_CHART, CE_PLOT, CE_TITLE, CE_LEGEND, CE_XAXIS, CE_YAXIS, CE_N, CE_SERIES = 8 };
#define CE_NONE 0xFFFF

typedef struct { u8 minset, maxset, majset; double min, max, major; } CAxis;
typedef struct {
  Formula *name;                   /* the series' name from a cell, or NULL (then lit) */
  char *lit;                       /* a name given as text (UTF-8), or NULL */
  Formula *cat;                    /* category labels (scatter and bubble: the X values), or NULL */
  Formula *val;                    /* the values (scatter: Y) */
  Formula *size;                   /* bubble sizes */
  u32 color, line;                 /* fill and line colour, FILL_ON | rgb (0: automatic) */
  u8 kind;                         /* a combo chart's series: CK_COL, CK_LINE... or CK_SAME */
  u8 secondary, smooth, nomarker;
} CSeries;
struct Chart {
  u8 kind;
  u8 has_title;                    /* a title shown: title, or (NULL/empty) the series name or "Chart Title" */
  char *title;
  u8 legend;                       /* LG_ */
  u8 labels;                       /* data labels (the values) */
  u8 hgrid, vgrid;                 /* major gridlines across (value axis) and up (category axis) */
  u8 style;                        /* chart style 1..8 (the look of bars and lines) */
  u8 palette;                      /* 0 colourful, 1 monochrome, 2..7 one accent's shades */
  u8 byrows, hassrc;               /* series are rows of src (else columns) */
  u16 srcsheet; Rect src;          /* the range the chart was made from (Switch Row/Column, Select Data) */
  char *xtitle, *ytitle;           /* axis titles, NULL none */
  CAxis va, ha;                    /* the value axis, and scatter's X axis */
  u32 efill[CE_N], eline[CE_N];    /* Format tab: fill and outline of chart area, plot area, title, legend (0 automatic) */
  CSeries *s; int ns;
  char *pivot;                     /* a PivotChart: its pivot table ("Sheet2!PivotTable1"), whose report gives the series; NULL none */
};

/* ------------------------------------------------------------------ sparklines */
enum { SP_LINE, SP_COLUMN, SP_WINLOSS };
typedef struct { i32 r, c; Formula *f; } Spark;   /* in cell r, c, the data f */
typedef struct {
  u8 type;
  u8 markers, high, low, first, last, negative, axis;
  u8 blanks;                       /* empty cells: 0 gaps, 1 zero, 2 joined */
  u8 minmode, maxmode;             /* the vertical axis' ends: 0 each its own, 1 the same for the group, 2 custom */
  double minv, maxv;
  u16 weight;                      /* line weight, hundredths of a point (0: 0.75) */
  u32 color, negcolor, markcolor, highcolor, lowcolor, firstcolor, lastcolor;   /* FILL_ON | rgb */
  Spark *sp; int n;
} SparkGroup;

typedef struct Drawing {
  DObj *o; int n;                  /* the objects, bottom first */
  SparkGroup *g; int ng;
  u32 nextid;
  Blob *bg; u8 bgtype;             /* Page Layout > Background: a picture tiled behind the cells (PIC_), or NULL */
  DGroup *grp; int ngrp;           /* (drwgroup.c) the groups */
} Drawing;

/* ------------------------------------------------------------------ a sheet's drawing */
Drawing *drw_of(Sheet *s);                 /* made if the sheet had none; NULL out of memory */
int   drw_count(const Sheet *s);
DObj *drw_obj(Sheet *s, int i);
/* a new object on top of the others, with a fresh id and name ("Chart 3"); NULL out of memory */
DObj *drw_new(Sheet *s, int kind);
void  drw_remove(Sheet *s, int i);
void  drw_move_z(Sheet *s, int i, int to); /* object i to place to in the stacking order (0 bottom) */
void  dobj_clear(DObj *o);                 /* what it owns given back */
int   dobj_copy(DObj *dst, const DObj *src);   /* a deep copy (pictures shared); 0 out of memory */
void  drw_free(Drawing *d);
Drawing *drw_copy(const Drawing *d);       /* NULL if d is NULL or out of memory */
/* (book.c) rows or columns inserted (n > 0) or deleted at at: anchors and sparklines follow */
void  drw_shift(Sheet *s, int rows, i32 at, i32 n);
/* (book.c) every formula the drawing keeps, as book.c's each_formula walks the cells' */
void  drw_each_formula(Sheet *s, int home, void (*fn)(Formula *f, int home, void *arg), void *arg);
/* the left edge of column c and the top of row r, pixels at 100% from A1's corner */
i32   sheet_col_px(Sheet *s, i32 c);
i32   sheet_row_px(Sheet *s, i32 r);
/* the cell holding pixel x (or y) at 100%, and the offset into it */
i32   sheet_col_at(Sheet *s, i32 x, i32 *off);
i32   sheet_row_at(Sheet *s, i32 y, i32 *off);
void  dobj_box(Sheet *s, const DObj *o, i32 *x0, i32 *y0, i32 *x1, i32 *y1);   /* at 100% */
void  dobj_place(Sheet *s, DObj *o, i32 x0, i32 y0, i32 x1, i32 y1);          /* anchored to this box */
/* undo for a change to objects or sparklines: the sheet noted before, a step closed after */
void  drw_begin(Book *b, int sheet);
void  drw_end(Book *b);
void  book_note_all(Book *b);              /* (book.c, inside a step) every sheet noted for undo: objects moved between sheets */

/* ------------------------------------------------------------------ groups (drwgroup.c) */
DGroup *drw_group_find(Sheet *s, u32 id);            /* NULL if there is none */
u32   drw_top_group(Sheet *s, int i);               /* the outermost group object i is in, 0 none */
int   drw_in_group(Sheet *s, int i, u32 gid);       /* object i is in group gid (at any depth) */
int   drw_group_depth(Sheet *s, u32 gid);           /* 1 a top-level group, 2 one inside it... */
/* the objects in group gid (at any depth), bottom first, at most max; how many */
int   drw_group_members(Sheet *s, u32 gid, int *out, int max);
/* what selecting object i selects: its outermost group's members, or i alone; how many */
int   drw_unit(Sheet *s, int i, int *out, int max);
/* the objects (and the groups they are in) of sel made one new group, kept together in the stacking
   order where the topmost of them was; its id, 0 if they are fewer than two objects or groups */
u32   drw_group(Sheet *s, const int *sel, int n);
/* group gid taken apart one level: its objects and groups belong to its parent; 0 if no such group */
int   drw_ungroup(Sheet *s, u32 gid);
void  drw_group_box(Sheet *s, u32 gid, i32 *x0, i32 *y0, i32 *x1, i32 *y1);   /* round its members, at 100% */
/* group gid's members moved and stretched so that its box is this one */
void  drw_group_place(Sheet *s, u32 gid, i32 x0, i32 y0, i32 x1, i32 y1);
void  drw_group_prune(Sheet *s);                    /* groups left without members taken away */
DGroup *drw_group_add(Sheet *s, u32 parent);       /* (loading) an empty new group; NULL out of memory */
int   drw_groups_copy(Drawing *dst, const Drawing *src);   /* 0 out of memory */

/* ------------------------------------------------------------------ drawing primitives */
enum { PR_RECT, PR_LINE, PR_POLY, PR_ELLIPSE, PR_TEXT };
typedef struct {
  u8 t;                            /* PR_ */
  u8 dash, bold, align, valign, vert;   /* text: align 0 left 1 centre 2 right of x; valign 0 top 1 middle 2 bottom of y; vert: turned to read upwards */
  u8 size;                         /* text: pixels to the em */
  u8 width;                        /* lines: pixels */
  u16 elem;                        /* CE_ (or CE_SERIES + series) it belongs to, CE_NONE */
  u32 fill, line;                  /* FILL_ON | rgb, 0 none (text: line is its colour) */
  i32 x, y, w, h;                  /* rectangles and ellipses (ellipses: the box), text's anchor */
  int np; i32 *pts;                /* polylines and polygons: x, y pairs */
  char *text;                      /* UTF-8 */
} Prim;
typedef struct { Prim *p; int n, cap; int oom; } PrimList;
void  prims_init(PrimList *l);
void  prims_free(PrimList *l);
Prim *prim_add(PrimList *l, int t, u16 elem);   /* zeroed; NULL out of memory */
/* the topmost part of a chart at x, y (CE_, CE_SERIES + k) or CE_NONE */
int   prims_hit(const PrimList *l, i32 x, i32 y);
/* text widths for laying out: px pixels to the em */
typedef i32 (*TextWidthFn)(void *ctx, const char *utf8, int px, int bold);

/* a form control (formctl.c) drawn in w x h pixels */
int    control_render(Book *b, int sheet, const DObj *o, i32 w, i32 h, PrimList *out);

/* ------------------------------------------------------------------ charts (chart.c) */
Chart *chart_new(int kind);
void   chart_free(Chart *c);
Chart *chart_copy(const Chart *c);
const char *chart_kind_name(int kind);    /* "Clustered Column" */
int    chart_kind_by_name(const char *name);
int    chart_is_cartesian(int kind);      /* has axes */
int    chart_is_pielike(int kind);        /* pie, doughnut, funnel, treemap, sunburst: one colour a point */
/* a chart of kind made from the cells of area: a heading row and a label column found as Excel
   does; series down the columns, or along the rows when there are more columns than rows
   (byrows -1 guesses). NULL if there are no numbers there */
Chart *chart_from_range(Book *b, int sheet, Rect area, int kind, int byrows);
/* the series made again from c->src, along rows or columns */
int    chart_set_source(Book *b, Chart *c, int sheet, Rect area, int byrows);
/* kinds that suit area's data, best first (Recommended Charts); how many */
int    chart_recommend(Book *b, int sheet, Rect area, int *kinds, int max);
void   chart_quick_layout(Chart *c, int n);    /* layouts 1..6 */
void   chart_colors(Chart *c, int palette);
void   chart_style(Chart *c, int style);
/* the series' values: n of them (NAN for an empty cell); names, categories (malloc'd strings) */
typedef struct {
  char *name; double *v, *x, *size; int n; u32 rgb;
} CDSeries;
typedef struct { char **cat; int ncat; CDSeries *s; int ns; char fmt[64]; } CData;
int    chart_resolve(Book *b, int sheet, const Chart *c, CData *d);
void   cdata_free(CData *d);
u32    chart_series_rgb(const Chart *c, int k, int n);   /* series k's colour (of n) */
/* the chart drawn in w x h pixels (zoom: per cent, for text sizes) */
int    chart_render(Book *b, int sheet, const Chart *c, int w, int h, int zoom, TextWidthFn tw, void *ctx, PrimList *out);
/* a nice axis: from lo to hi in steps of major (a few of them) */
void   chart_nice_scale(double lo, double hi, int maxticks, double *min, double *max, double *major);

/* ------------------------------------------------------------------ sparklines (spark.c) */
/* the sparkline in cell r, c and its group, or NULL */
Spark *spark_at(Sheet *s, i32 r, i32 c, SparkGroup **g);
/* sparklines of type in the cells of loc (a row or column of them), one for each row or column of
   data (as many as loc has cells); an undo step. 0 if they don't match */
int    book_add_sparklines(Book *b, int sheet, int type, Rect data, Rect loc);
int    book_clear_sparklines(Book *b, int sheet, Rect area, int whole_groups);   /* how many went */
void   spark_group_init(SparkGroup *g, int type);
void   spark_group_clear(SparkGroup *g);
/* a sparkline's values (NAN for empty cells), at most max; how many */
int    spark_values(Book *b, int sheet, const Spark *sp, double *out, int max);
/* the sparkline drawn into w x h pixels; lo and hi the axis' ends (NAN: its own) */
int    spark_render(const SparkGroup *g, const double *v, int n, int w, int h, double lo, double hi, PrimList *out);
/* the group's common axis ends (when minmode/maxmode ask for them) */
void   spark_group_range(Book *b, int sheet, const SparkGroup *g, double *lo, double *hi);

#endif
