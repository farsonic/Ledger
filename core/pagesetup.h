/*
 * pagesetup.h - a sheet's page setup (paper, orientation, margins, scaling, print area and titles,
 * page breaks, headers and footers, sheet options) and the pages it gives: what Page Break Preview,
 * the Page Layout view and PDF export draw. Also the workbook's extras kept alongside: document
 * properties beyond title and author, the theme (colours and fonts) and calculation options.
 *
 * Ledger doesn't print, but everything here is saved in .xlsx as Excel keeps it (pageMargins,
 * pageSetup, printOptions, headerFooter, rowBreaks/colBreaks, _xlnm.Print_Area and
 * _xlnm.Print_Titles), so a workbook prints from Excel as it was set up in Ledger.
 *
 * Lengths are in points (1/72 inch) unless said otherwise; margins are kept in inches as in the file.
 */
#ifndef LEDGER_PAGESETUP_H
#define LEDGER_PAGESETUP_H

#include "book.h"
#include "xml.h"

/* paper sizes: index into page_papers[] (each with its .xlsx paperSize code) */
typedef struct { const char *name; int code; double w, h; } Paper;   /* w, h in points, portrait */
extern const Paper page_papers[];
extern const int page_npapers;
int page_paper_by_code(int code);      /* index; 0 (Letter) if the code is unknown */

enum { HF_HEADER, HF_FOOTER, HF_FIRSTH, HF_FIRSTF, HF_EVENH, HF_EVENF, HF_N };
enum { PV_NORMAL, PV_BREAKS, PV_LAYOUT };      /* the sheet's view (sheetView view=...) */
enum { M_LEFT, M_RIGHT, M_TOP, M_BOTTOM, M_HEADER, M_FOOTER };

typedef struct PageSetup {
  u8 landscape;
  u8 paper;                       /* page_papers index */
  double margin[6];               /* inches, M_ order */
  u16 scale;                      /* per cent, 10..400 */
  u8 fit;                         /* fit to fitw pages wide by fith tall (0 there: as many as needed) */
  u16 fitw, fith;
  u8 gridlines, headings;         /* printed (the screen's are Sheet.gridlines/headings) */
  u8 centerh, centerv;
  u8 blackwhite, draft;
  u8 overthendown;                /* page order: across then down (else down then across) */
  u16 firstpage;                  /* the first page's number (0: automatic, 1) */
  u8 difffirst, diffoddeven;      /* different first page / odd and even pages */
  u8 scalehf, alignhf;            /* headers scale with the document / align with the margins */
  char *hf[HF_N];                 /* header and footer codes (UTF-8, &L&C&R...), NULL none */
  Rect area;                      /* the print area (r1 -1: the used range) */
  i32 trow1, trow2;               /* rows to repeat at the top (-1: none) */
  i32 tcol1, tcol2;               /* columns to repeat at the left (-1: none) */
  i32 *rbrk; int nrbrk;           /* manual page breaks: before these rows, sorted */
  i32 *cbrk; int ncbrk;           /* and before these columns */
  u8 view;                        /* PV_: the view the sheet is shown in */
  u8 rdbrk;                       /* (loading: reading rowBreaks 1 or colBreaks 2) */
  int rdhf;                       /* (loading: the header/footer element being read, -1 none) */
} PageSetup;

void       page_defaults(PageSetup *p);
PageSetup *page_get(Sheet *s);            /* the sheet's, made with the defaults if it has none (NULL out of memory) */
const PageSetup *page_of(Sheet *s);       /* the sheet's, or the defaults (never NULL) */
void       page_free(PageSetup *p);
PageSetup *page_copy(const PageSetup *p);  /* NULL for NULL */
void       page_set_hf(PageSetup *p, int which, const char *text);
/* rows or columns inserted (n > 0) or deleted (n < 0) at at: breaks, print area and titles follow (book.c) */
void       page_shift(Sheet *s, int rows, i32 at, i32 n);
/* page breaks: one added before row (rows) or column, or taken away; 1 if something changed */
int        page_set_break(Sheet *s, int rows, i32 at, int on);
int        page_has_break(const Sheet *s, int rows, i32 at);
void       page_reset_breaks(Sheet *s);
/* margins presets: 0 Normal, 1 Wide, 2 Narrow (Excel's) */
void       page_margin_preset(PageSetup *p, int k);

/* ------------------------------------------------------------------ the pages */
typedef struct { i32 a, b; } Band;     /* rows or columns a..b */
typedef struct {
  Rect area;                      /* what is printed */
  double scale;                   /* 1.0 is 100% (fit to page works it out) */
  double pw, ph;                  /* the paper, points, after the orientation */
  double availw, availh;          /* room for cells on a page, points at 100%: the paper less margins and titles */
  Band *rows; int nrows;          /* the page bands down and across */
  Band *cols; int ncols;
  int npages;
  i32 trow1, trow2, tcol1, tcol2; /* titles printed on every page (-1 none) */
  u8 overthendown;                /* the page order */
} Pages;
/* sheet's pages from its page setup; 0 out of memory. The sheet's px sizes (96 dpi) become points
   (x 0.75). An empty sheet has one empty page */
int    page_layout(Book *b, int sheet, Pages *p);
void   pages_free(Pages *p);
/* page k (0-based, in the setup's page order) as its row and column band */
void   page_bands(const Pages *p, int k, int *rb, int *cb);
/* points from the area's top (rows) or left: the size of rows/columns a..b */
double page_rows_pt(Sheet *s, i32 a, i32 b);
double page_cols_pt(Sheet *s, i32 a, i32 b);

/* ------------------------------------------------------------------ headers and footers */
/* a piece of header text with its look */
typedef struct { char text[256]; u8 bold, italic, underline, strike; u16 size; char font[48]; u32 color; } HFRun;
typedef struct { HFRun run[16]; int n; } HFSection;
typedef struct {
  int page, pages;                /* &P (already counting firstpage) and &N */
  const char *file, *path, *sheet;   /* &F, &Z, &A (UTF-8) */
  double now;                     /* &D, &T: a date serial */
} HFContext;
/* the code (&L &C &R sections; &P &N &D &T &F &Z &A &&; &B &I &U &E &S, &"font,style", &nn size, &Kcolor)
   into its left, centre and right sections; codes it doesn't know are dropped */
void   hf_expand(const char *code, const HFContext *cx, HFSection out[3]);
/* the plain text of an expanded section (the runs joined), for the screen */
void   hf_plain(const HFSection *s, char *out, size_t cap);
/* which header/footer a page uses (HF_HEADER/HF_FIRSTH/HF_EVENH or the footers): number is &P */
int    hf_which(const PageSetup *p, int footer, int first, int number);
/* Excel's ready-made headers/footers (the Page Setup lists): count and the k-th code */
int    hf_preset_count(void);
const char *hf_preset(int k);

/* ------------------------------------------------------------------ the workbook's extras */
typedef struct {
  char name[32];
  u32 color[12];                  /* lt1, dk1, lt2, dk2, accent1..6, hlink, folHlink */
  char major[48], minor[48];      /* heading and body fonts */
} Theme;
extern const Theme page_themes[];     /* Office's and GridCraft's ready-made ones */
extern const int page_nthemes;
/* a custom view (View > Custom Views): how the workbook was shown, under a name */
typedef struct { char name[40]; int sheet; i32 cur_r, cur_c, top, left; Rect sel; u16 zoom; u8 view, grid, head; } CustomView;
#define MAXVIEWS 24
typedef struct LayoutBook {
  char subject[128], keywords[128], description[256], category[64], status[32], company[64], manager[64];
  Theme theme;
  u8 iterate;                     /* iterative calculation */
  u16 maxiter;                    /* 100 */
  double maxchange;               /* 0.001 */
  u8 r1c1;                        /* R1C1 reference style */
  u8 hasprops;
  CustomView views[MAXVIEWS]; int nviews;
} LayoutBook;
LayoutBook *book_lx(Book *b);         /* made with defaults when first asked for (NULL out of memory) */
void   lx_free(LayoutBook *x);
/* a theme put on the workbook: its colours become the palette and its body font the default font
   (style 0's font, and every font that was the old theme's body font) */
void   book_set_theme(Book *b, const Theme *t);
void   book_set_theme_fonts(Book *b, const char *major, const char *minor);

/* ------------------------------------------------------------------ .xlsx (xlsx.c's hooks) */
/* an element of a worksheet part; 1 if it was page setup's (the parser may have moved on) */
int    page_xml_sheet(Sheet *s, Xml *x);
void   page_xml_sheet_in(Book *b, int sheet);   /* (before a sheet's elements: which book and sheet they are) */
/* the workbook part's _xlnm names (Print_Area, Print_Titles): 1 if taken */
int    page_xml_name(Book *b, const char *name, int local, const char *text);
/* written in their places in a worksheet: inside <sheetPr> (fitToPage), and after the hyperlinks */
void   page_write_sheetpr(Sheet *s, Buf *o);
int    page_needs_sheetpr(Sheet *s);
void   page_write_sheet(Sheet *s, Buf *o);
/* sheetView attributes (view="pageBreakPreview") */
void   page_write_view(Sheet *s, Buf *o);
/* the _xlnm names to add to workbook.xml's definedNames, and whether there are any */
int    page_has_names(Book *b);
void   page_write_names(Book *b, Buf *o);
/* docProps/core.xml and app.xml extras, the theme part */
void   page_write_core(Book *b, Buf *o);
int    page_xml_core(Book *b, Xml *x);   /* an element of core.xml or app.xml; 1 if taken */
void   page_write_theme(Book *b, Buf *o);
void   page_read_theme(Book *b, const char *xml, size_t len);
/* custom views: customWorkbookViews (after calcPr) and each sheet's customSheetViews (before mergeCells) */
void   page_write_book_views(Book *b, Buf *o);
void   page_write_sheet_views(Book *b, int sheet, Buf *o);
void   page_xml_book_view(Book *b, Xml *x);
/* calcPr attributes */
void   page_write_calcpr(Book *b, Buf *o);
void   page_xml_calcpr(Book *b, Xml *x);

#endif
