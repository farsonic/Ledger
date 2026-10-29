/*
 * grid.c - the sheet itself: a MUI custom class (a subclass of Area) that draws the active sheet's
 * cells with their fonts, fills, borders and number formats, the row and column headings, the
 * selection with its fill handle, frozen panes and the copied cells' marching outline, and takes
 * the keyboard and mouse: moving and selecting, typing into cells (with point mode for formulas),
 * resizing rows and columns, dragging the fill handle.
 *
 * The grid works on the workbook straight through the core (book.h, ops.h); each change is an undo
 * step there. It tells main.c what happened through attributes others can notify on:
 * MUIA_Grid_Moved (the active cell or selection moved: the formula bar, name box, ribbon and status
 * bar follow), MUIA_Grid_Changed (the workbook changed) and MUIA_Grid_EditText (typing).
 *
 * Drawing goes to an off-screen buffer the size of the grid, then to the window in one blit, so
 * nothing flickers. Cell fonts are Ledger's own screen fonts (made from the TrueType fonts it ships,
 * Carlito for Calibri and so on) or, for an installed TrueType font, rendered at the exact size.
 */
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <exec/memory.h>
#include <devices/inputevent.h>
#include <graphics/gfxmacros.h>
#include <libraries/gadtools.h>
#include <proto/exec.h>
#include <proto/intuition.h>
#include <proto/graphics.h>
#include <proto/layers.h>
#include <proto/muimaster.h>
#include <proto/utility.h>
#include <proto/keymap.h>
#include <clib/alib_protos.h>
#include "ledger.h"
#include "theme.h"
#include "lang.h"
#include "ids.h"
#include "layout.h"
#include "../core/calc.h"
#include "../core/ops.h"
#include "../core/cond.h"

#ifndef MUIM_GoActive
#define MUIM_GoActive   0x8042491aUL
#define MUIM_GoInactive 0x80422c0cUL
#endif

#define NCOLS 64                 /* colours cached as pens */
#define NFONTS 20                /* cell fonts cached */
#define MAXVR 300                /* rows and columns on screen at most */
#define MAXVC 200
#define EMAX 2000                /* the editor's text, bytes */

/* where the mouse is dragging */
enum { DR_NONE, DR_CELLS, DR_COLW, DR_ROWH, DR_COLS, DR_ROWS, DR_FILL, DR_POINT, DR_LAYOUT };

struct Data {
  Object *obj;
  Book *book;
  FontSet *fs;
  Object *vprop, *hprop, *status;
  int shown, handlers, formulas, brush;
  /* pens */
  int pens;
  LONG pen_cell, pen_grid, pen_head, pen_headsel, pen_headtext, pen_headline, pen_text, pen_sel, pen_accent, pen_frozen, pen_white, pen_ref[8];
  struct { ULONG rgb; LONG pen; } col[NCOLS]; int ncol;
  /* cell fonts */
  struct { char name[48]; UWORD ys; struct TextFont *tf; } fc[NFONTS]; int nfc;
  /* the back buffer */
  struct BitMap *bm; struct Layer_Info *li; struct Layer *ly; LONG bw, bh;
  /* the layout of the last drawing: headings' size, the rows and columns shown and their pixels
     (from the grid's top left); the first nfr rows and nfcol columns are the frozen ones */
  LONG hw, hh, w, h;
  int nvr, nvc, nfr, nfcol;
  i32 vr[MAXVR]; LONG vy[MAXVR + 1];
  i32 vc[MAXVC]; LONG vx[MAXVC + 1];
  /* the selection: from the active cell (the sheet's cur_r, cur_c) to this corner */
  i32 end_r, end_c;
  /* editing */
  int editing;                   /* 0 no, 1 entering (arrows finish), 2 editing (arrows move the caret) */
  char ebuf[EMAX + 1]; int elen, ecaret, caret_on;
  int pt_on, pt_pos, pt_len;     /* point mode: the reference typed at pt_pos, pt_len long */
  i32 pt_r, pt_c, pt_r2, pt_c2;  /* its cell, and the range's other corner */
  /* the mouse */
  int drag; i32 drag_idx; LONG drag_px, drag_size;
  ULONG secs, micros; i32 click_r, click_c; int click_region;
  i32 fill_r, fill_c;            /* where the fill handle is dragged to */
  /* the copied cells' outline */
  int copied; Rect copy; int ant;
  /* notification counters */
  ULONG moved, changed, edited;
  ULONG ctx_pick;
  i32 filter_col;                /* the column whose filter button was clicked */
  i32 hover_r, hover_c;          /* the cell with a note or link the mouse rests on (-1 none) */
  i32 hot_col, hot_row;          /* (0.1.1) the heading edge under the mouse, which a drag resizes (-1 none) */
  int resize_ptr;                /* the resize pointer is showing: 1 columns, 2 rows */
  int show_notes;                /* every note shown, not just the one under the mouse */
  Object *ctxstrip;
  struct MUI_EventHandlerNode ehn;
  struct MUI_InputHandlerNode ihn;
};
static struct MUI_CustomClass *cls;

static Sheet *SH(struct Data *d) { return d->book->sh[d->book->active]; }
static int SI(struct Data *d) { return d->book->active; }
static LONG zoom(struct Data *d) { LONG z = SH(d)->zoom; return z < 10 ? 100 : z > 400 ? 400 : z; }
static int fh(void) { return theme_font()->tf_YSize; }
static i32 imin(i32 a, i32 b) { return a < b ? a : b; }
static i32 imax(i32 a, i32 b) { return a > b ? a : b; }

/* ------------------------------------------------------------------ text helpers (ledger.h) */
void grid_cell_text(Book *b, int sheet, i32 r, i32 c, int formulas, char *out, size_t cap, u32 *color)
{
  char u[1024];
  *color = COLOR_AUTO;
  if (formulas) {
    Cell *cell = sheet_cell(b->sh[sheet], r, c);
    if (cell && cell->f) { book_edit_text(b, sheet, r, c, u, sizeof u); utf8_to_latin1(u, (u32)strlen(u), out, cap); return; }
  }
  book_shown_text(b, sheet, r, c, u, sizeof u, color);
  utf8_to_latin1(u, (u32)strlen(u), out, cap);
}
void grid_edit_text(Book *b, int sheet, i32 r, i32 c, char *out, size_t cap)
{
  char u[EMAX + 1];
  book_edit_text(b, sheet, r, c, u, sizeof u);
  utf8_to_latin1(u, (u32)strlen(u), out, cap);
}
/* Latin-1 to UTF-8 (the editor types Latin-1; the workbook keeps UTF-8) */
static void latin1_utf8(const char *s, char *u, size_t cap)
{
  size_t o = 0;
  for (; *s && o + 3 < cap; s++) o += (size_t)utf8_put(u + o, (u8)*s);
  u[o] = 0;
}

/* ------------------------------------------------------------------ pens */
static LONG best(Object *obj, ULONG rgb)
{
  return ObtainBestPen(_screen(obj)->ViewPort.ColorMap, ((rgb >> 16) & 255) * 0x01010101UL, ((rgb >> 8) & 255) * 0x01010101UL, (rgb & 255) * 0x01010101UL,
                       OBP_Precision, PRECISION_IMAGE, TAG_DONE);
}
/* the grid's own pens, for the light or dark theme */
static void get_pens(struct Data *d, Object *obj)
{
  int dk = theme_is_dark();
  d->pen_cell = best(obj, dk ? 0x1F1F1F : 0xFFFFFF);
  d->pen_grid = best(obj, dk ? 0x3A3A3A : 0xE1E1E1);
  d->pen_head = best(obj, dk ? 0x2B2B2B : 0xF5F5F5);
  d->pen_headsel = best(obj, dk ? 0x3D4A5E : 0xDCE6F4);
  d->pen_headtext = best(obj, dk ? 0xC8C8C8 : 0x444444);
  d->pen_headline = best(obj, dk ? 0x4A4A4A : 0xC6C6C6);
  d->pen_text = best(obj, dk ? 0xF0F0F0 : 0x000000);
  d->pen_sel = best(obj, dk ? 0x2E3A4C : 0xD5E3F7);
  d->pen_frozen = best(obj, dk ? 0x777777 : 0x9A9A9A);
  d->pen_white = best(obj, 0xFFFFFF);
  d->pen_accent = tpen(T_ACCENT);
  {
    /* the colours of the references in a formula being typed, as Excel's */
    static const ULONG refcol[8] = { 0x2F75D6, 0xC0392B, 0x7D3C98, 0x1E8449, 0xD35400, 0x16A085, 0xC2185B, 0x8D6E63 };
    int k;
    for (k = 0; k < 8; k++) d->pen_ref[k] = best(obj, refcol[k]);
  }
  d->ncol = 0;
  d->pens = 1;
}
static void free_pens(struct Data *d, Object *obj)
{
  struct ColorMap *cm = _screen(obj)->ViewPort.ColorMap;
  int i;
  if (!d->pens) return;
  ReleasePen(cm, d->pen_cell); ReleasePen(cm, d->pen_grid); ReleasePen(cm, d->pen_head); ReleasePen(cm, d->pen_headsel);
  ReleasePen(cm, d->pen_headtext); ReleasePen(cm, d->pen_headline); ReleasePen(cm, d->pen_text); ReleasePen(cm, d->pen_sel);
  ReleasePen(cm, d->pen_frozen); ReleasePen(cm, d->pen_white);
  for (i = 0; i < 8; i++) ReleasePen(cm, d->pen_ref[i]);
  for (i = 0; i < d->ncol; i++) ReleasePen(cm, d->col[i].pen);
  d->ncol = 0;
  d->pens = 0;
}
/* a pen for an 0xRRGGBB colour, cached (NCOLS of them; then the nearest of the grid's own) */
static LONG pen_for(struct Data *d, ULONG rgb)
{
  int i;
  rgb &= 0xFFFFFF;
  for (i = 0; i < d->ncol; i++) if (d->col[i].rgb == rgb) return d->col[i].pen;
  if (d->ncol >= NCOLS) return ((rgb >> 16) + ((rgb >> 8) & 255) + (rgb & 255)) > 384 ? d->pen_white : d->pen_text;
  d->col[d->ncol].rgb = rgb;
  d->col[d->ncol].pen = best(d->obj, rgb);
  return d->col[d->ncol++].pen;
}
/* a font colour's pen: automatic is the text pen (white on the dark theme's cells unless filled) */
static LONG ink(struct Data *d, ULONG rgb, ULONG fill)
{
  if (rgb == COLOR_AUTO || (rgb & 0xFF000000UL)) {
    if (fill && theme_is_dark()) {
      ULONG f = fill & 0xFFFFFF;
      return ((f >> 16) + ((f >> 8) & 255) + (f & 255)) > 384 ? pen_for(d, 0) : d->pen_white;
    }
    return d->pen_text;
  }
  return pen_for(d, rgb);
}

/* ------------------------------------------------------------------ cell fonts */
/* Ledger's screen font family for a font name (substituted as layout does: Calibri is Carlito...)
   and the TrueType family it was made from (for its metrics) */
static const char *amiga_family(struct Data *d, const char *name, const char **srcfam)
{
  const char *f = d->fs ? fonts_substitute(d->fs, name) : name;
  if (strstr(f, "Carlito") || strstr(name, "Calibri")) { *srcfam = "Carlito"; return "QuillSansC"; }
  if (strstr(f, "Caladea") || strstr(name, "Cambria")) { *srcfam = "Caladea"; return "QuillSerifC"; }
  if (strstr(f, "Mono") || strstr(f, "Courier")) { *srcfam = "Liberation Mono"; return "QuillMono"; }
  if ((strstr(f, "Serif") && !strstr(f, "Sans")) || strstr(f, "Times") || strstr(f, "Georgia")) { *srcfam = "Liberation Serif"; return "QuillSerif"; }
  *srcfam = "Liberation Sans";
  return "QuillSans";
}
static void close_fonts(struct Data *d)
{
  int i;
  for (i = 0; i < d->nfc; i++) if (d->fc[i].tf) theme_close_font(d->fc[i].tf);
  d->nfc = 0;
}
/* the Amiga font for a cell font at the zoom (cached; NULL: the interface font is used) */
static struct TextFont *cell_font(struct Data *d, const Font *f)
{
  const char *src, *fam = amiga_family(d, f->name, &src);
  Face *face = d->fs ? fonts_face(d->fs, f->name, f->bold, f->italic) : NULL;
  LONG em = (LONG)f->size * zoom(d) * 4 / 3000;      /* pixels to the em: points * 96/72 * zoom */
  char name[48];
  UWORD ys;
  int i;
  FaceSize *sz;
  if (em < 5) em = 5;
  if (face && face->installed) {
    LONG ppem64 = (LONG)f->size * zoom(d) * 64 * 4 / 3000;
    sprintf(name, "@%lx", (unsigned long)face);
    ys = (UWORD)((ppem64 + 32) / 64);
    for (i = 0; i < d->nfc; i++) if (d->fc[i].ys == ys && !strcmp(d->fc[i].name, name)) return d->fc[i].tf;
    if (d->nfc >= NFONTS) close_fonts(d);
    i = d->nfc++;
    strcpy(d->fc[i].name, name); d->fc[i].ys = ys;
    d->fc[i].tf = theme_open_face(face, ppem64);
    return d->fc[i].tf;
  }
  /* Ledger's bitmap fonts: their height is their line box (as mkamigafonts.py made them) */
  sz = d->fs ? fonts_size(d->fs, src, f->bold, f->italic, 24) : NULL;
  if (!strcmp(src, "Caladea")) ys = (UWORD)((em * 1300 + 500) / 1000);
  else if (sz) ys = (UWORD)((em * (sz->face->ascent + sz->face->descent) + sz->face->upm / 2) / sz->face->upm);
  else ys = (UWORD)(em + em / 8);
  sprintf(name, "%s%s.font", fam, f->bold ? (f->italic ? "BI" : "B") : f->italic ? "I" : "");
  for (i = 0; i < d->nfc; i++) if (d->fc[i].ys == ys && !strcmp(d->fc[i].name, name)) return d->fc[i].tf;
  if (d->nfc >= NFONTS) close_fonts(d);
  i = d->nfc++;
  strcpy(d->fc[i].name, name); d->fc[i].ys = ys;
  d->fc[i].tf = theme_open_font(name, ys);
  return d->fc[i].tf;
}
static struct TextFont *font_of(struct Data *d, int style)
{
  struct TextFont *tf = cell_font(d, &d->book->fonts[d->book->styles[style].font]);
  return tf ? tf : theme_font();
}

/* ------------------------------------------------------------------ sizes */
/* a row's height in pixels at 100%: its own, or grown to fit its biggest font as Excel does
   (rows nobody sized); 0 when hidden */
static LONG row_px100(struct Data *d, i32 r)
{
  Sheet *s = SH(d);
  LONG h = sheet_row_height(s, r);
  Row *row;
  int i;
  if (!h) return 0;
  if (r < s->nrows && (row = s->rows[r]) && !row->custom) {
    for (i = 0; i < row->n; i++) {
      const Font *f = &d->book->fonts[d->book->styles[row->c[i].style].font];
      LONG need = ((LONG)f->size * 544 + 1500) / 3000;
      if (need > h) h = need;
    }
  }
  return h;
}
static LONG colw(struct Data *d, i32 c)
{
  if (d->drag == DR_COLW && c == d->drag_idx) return d->drag_size * zoom(d) / 100;
  return (LONG)sheet_col_width(SH(d), c) * zoom(d) / 100;
}
static LONG rowh(struct Data *d, i32 r)
{
  if (d->drag == DR_ROWH && r == d->drag_idx) return d->drag_size * zoom(d) / 100;
  return row_px100(d, r) * zoom(d) / 100;
}

/* ------------------------------------------------------------------ layout */
/* the rows and columns that fit in w x h, frozen ones first, from the sheet's scroll position */
static void layout(struct Data *d, LONG w, LONG h)
{
  Sheet *s = SH(d);
  LONG x, y, digits;
  i32 r, c;
  struct RastPort rp;
  char t[12];
  d->w = w; d->h = h;
  if (s->top < s->freezer) s->top = s->freezer;
  if (s->left < s->freezec) s->left = s->freezec;
  d->hh = s->headings ? fh() + 7 : 0;
  /* the row headings wide enough for the biggest row number likely to show */
  sprintf(t, "%ld", (long)(s->top + h / 12 + 1));
  digits = (LONG)strlen(t);
  if (digits < 2) digits = 2;
  InitRastPort(&rp);
  SetFont(&rp, theme_font());
  d->hw = s->headings ? TextLength(&rp, (STRPTR)"0000000", (ULONG)digits) + 12 : 0;
  /* rows */
  y = d->hh; d->nvr = 0; d->nfr = 0;
  for (r = 0; r < s->freezer && d->nvr < MAXVR; r++) {
    LONG rh = rowh(d, r);
    if (!rh) continue;
    d->vr[d->nvr] = r; d->vy[d->nvr++] = y; y += rh;
  }
  d->nfr = d->nvr;
  for (r = s->top; r < MAXROW && d->nvr < MAXVR && y < h; r++) {
    LONG rh = rowh(d, r);
    if (!rh) continue;
    y += lx_gap_before(d->book, SI(d), 1, r, zoom(d));
    d->vr[d->nvr] = r; d->vy[d->nvr++] = y; y += rh;
  }
  d->vy[d->nvr] = y;
  /* columns */
  x = d->hw; d->nvc = 0;
  for (c = 0; c < s->freezec && d->nvc < MAXVC; c++) {
    LONG cw = colw(d, c);
    if (!cw) continue;
    d->vc[d->nvc] = c; d->vx[d->nvc++] = x; x += cw;
  }
  d->nfcol = d->nvc;
  for (c = s->left; c < MAXCOL && d->nvc < MAXVC && x < w; c++) {
    LONG cw = colw(d, c);
    if (!cw) continue;
    x += lx_gap_before(d->book, SI(d), 0, c, zoom(d));
    d->vc[d->nvc] = c; d->vx[d->nvc++] = x; x += cw;
  }
  d->vx[d->nvc] = x;
}
/* the screen x of column c's left edge (from the grid's left), visible or not */
static LONG col_x(struct Data *d, i32 c)
{
  Sheet *s = SH(d);
  LONG x = d->hw;
  i32 k;
  if (c < s->freezec) { for (k = 0; k < c; k++) x += colw(d, k); return x; }
  for (k = 0; k < s->freezec; k++) x += colw(d, k);
  if (c >= s->left) { for (k = s->left; k < c; k++) x += colw(d, k); }
  else { for (k = c; k < s->left; k++) x -= colw(d, k); }
  return x;
}
static LONG row_y(struct Data *d, i32 r)
{
  Sheet *s = SH(d);
  LONG y = d->hh;
  i32 k;
  if (r < s->freezer) { for (k = 0; k < r; k++) y += rowh(d, k); return y; }
  for (k = 0; k < s->freezer; k++) y += rowh(d, k);
  if (r >= s->top) { for (k = s->top; k < r && y < 30000; k++) y += rowh(d, k); }
  else { for (k = r; k < s->top && y > -30000; k++) y -= rowh(d, k); }
  return y;
}
/* index of a visible row or column (-1 if not shown) */
static int vis_row(struct Data *d, i32 r) { int i; for (i = 0; i < d->nvr; i++) if (d->vr[i] == r) return i; return -1; }
static int vis_col(struct Data *d, i32 c) { int i; for (i = 0; i < d->nvc; i++) if (d->vc[i] == c) return i; return -1; }
/* what is at grid point x, y: 0 a cell (*r, *c), 1 a column heading (*c), 2 a row heading (*r),
   3 the corner. Beyond the last row or column shown, the next one */
static int hit(struct Data *d, LONG x, LONG y, i32 *r, i32 *c)
{
  int i;
  *r = d->nvr ? d->vr[d->nvr - 1] : 0; *c = d->nvc ? d->vc[d->nvc - 1] : 0;
  for (i = 0; i < d->nvr; i++) if (y < d->vy[i + 1]) { *r = d->vr[i]; break; }
  if (y < d->hh && d->nvr) *r = d->vr[d->nfr < d->nvr ? d->nfr : 0];
  for (i = 0; i < d->nvc; i++) if (x < d->vx[i + 1]) { *c = d->vc[i]; break; }
  if (x < d->hw && d->nvc) *c = d->vc[d->nfcol < d->nvc ? d->nfcol : 0];
  if (y < d->hh && x < d->hw) return 3;
  if (y < d->hh) return 1;
  if (x < d->hw) return 2;
  return 0;
}

/* ------------------------------------------------------------------ the selection */
static Rect sel_rect(struct Data *d)
{
  Sheet *s = SH(d);
  Rect a;
  a.r1 = imin(s->cur_r, d->end_r); a.r2 = imax(s->cur_r, d->end_r);
  a.c1 = imin(s->cur_c, d->end_c); a.c2 = imax(s->cur_c, d->end_c);
  /* a merged area is selected whole */
  if (s->nmerges) {
    int i, grew = 1;
    while (grew) {
      grew = 0;
      for (i = 0; i < s->nmerges; i++) {
        Rect *m = &s->merges[i];
        if (m->r1 <= a.r2 && m->r2 >= a.r1 && m->c1 <= a.c2 && m->c2 >= a.c1 &&
            (m->r1 < a.r1 || m->r2 > a.r2 || m->c1 < a.c1 || m->c2 > a.c2)) {
          a.r1 = imin(a.r1, m->r1); a.r2 = imax(a.r2, m->r2); a.c1 = imin(a.c1, m->c1); a.c2 = imax(a.c2, m->c2); grew = 1;
        }
      }
    }
  }
  return a;
}
/* the sheet's sel kept in step with the grid's (main.c reads it from the sheet) */
static void sel_store(struct Data *d) { SH(d)->sel = sel_rect(d); }
static void moved(struct Data *d) { sel_store(d); SetAttrs(d->obj, MUIA_Grid_Moved, ++d->moved, TAG_DONE); }
static void changed(struct Data *d) { SetAttrs(d->obj, MUIA_Grid_Changed, ++d->changed, TAG_DONE); }
static int in_rect(const Rect *a, i32 r, i32 c) { return r >= a->r1 && r <= a->r2 && c >= a->c1 && c <= a->c2; }

/* ------------------------------------------------------------------ the scroll bars */
static LONG scroll_rows_fit(struct Data *d) { LONG n = d->nvr - d->nfr; return n > 1 ? n - 1 : 1; }
static LONG scroll_cols_fit(struct Data *d) { LONG n = d->nvc - d->nfcol; return n > 1 ? n - 1 : 1; }
static void update_props(struct Data *d)
{
  Sheet *s = SH(d);
  LONG vis, ent;
  if (d->vprop) {
    vis = scroll_rows_fit(d);
    ent = imax(imax(sheet_last_row(s) + 1, s->cur_r + 1), s->top + vis) + vis - s->freezer;
    if (ent > MAXROW) ent = MAXROW;
    SetAttrs(d->vprop, MUIA_NoNotify, TRUE, MUIA_Prop_Entries, ent, MUIA_Prop_Visible, vis, MUIA_Prop_First, s->top - s->freezer, TAG_DONE);
  }
  if (d->hprop) {
    vis = scroll_cols_fit(d);
    ent = imax(imax(sheet_last_col(s) + 1, s->cur_c + 1), s->left + vis) + vis - s->freezec;
    if (ent > MAXCOL) ent = MAXCOL;
    SetAttrs(d->hprop, MUIA_NoNotify, TRUE, MUIA_Prop_Entries, ent, MUIA_Prop_Visible, vis, MUIA_Prop_First, s->left - s->freezec, TAG_DONE);
  }
}

/* ------------------------------------------------------------------ the back buffer */
static void back_free(struct Data *d)
{
  if (d->ly) { DeleteLayer(0, d->ly); d->ly = NULL; }
  if (d->li) { DisposeLayerInfo(d->li); d->li = NULL; }
  if (d->bm) { WaitBlit(); FreeBitMap(d->bm); d->bm = NULL; }
  d->bw = d->bh = 0;
}
/* a RastPort w x h off screen (with a layer, so drawing past its edges is clipped); NULL without memory */
static struct RastPort *back_rp(struct Data *d, Object *obj, LONG w, LONG h)
{
  struct BitMap *wbm = _rp(obj)->BitMap;
  if (d->ly && d->bw == w && d->bh == h) return d->ly->rp;
  back_free(d);
  if (w <= 0 || h <= 0) return NULL;
  if (!(d->bm = AllocBitMap((ULONG)w, (ULONG)h, GetBitMapAttr(wbm, BMA_DEPTH), 0, wbm))) return NULL;
  if (!(d->li = NewLayerInfo()) || !(d->ly = CreateUpfrontLayer(d->li, d->bm, 0, 0, w - 1, h - 1, LAYERSIMPLE, NULL))) { back_free(d); return NULL; }
  if (theme_aatext()) d->ly->Window = (APTR)_window(obj);
  d->bw = w; d->bh = h;
  return d->ly->rp;
}

/* ------------------------------------------------------------------ drawing */
static void fill(struct RastPort *rp, LONG x0, LONG y0, LONG x1, LONG y1, LONG pen)
{
  if (x1 < x0 || y1 < y0) return;
  SetAPen(rp, pen);
  RectFill(rp, x0, y0, x1, y1);
}
static void hline(struct RastPort *rp, LONG x0, LONG x1, LONG y, LONG pen) { if (x1 < x0) return; SetAPen(rp, pen); Move(rp, x0, y); Draw(rp, x1, y); }
static void vline(struct RastPort *rp, LONG x, LONG y0, LONG y1, LONG pen) { if (y1 < y0) return; SetAPen(rp, pen); Move(rp, x, y0); Draw(rp, x, y1); }
/* whether a cell shows nothing (text can run over it) */
static int empty_cell(struct Data *d, i32 r, i32 c)
{
  Cell *cell = sheet_cell(SH(d), r, c);
  return !cell || (!cell->f && cell->v.t == V_EMPTY);
}
/* one cell's text drawn in the box x0..x1 (its own cell, perhaps widened by overflow or a merge),
   y0..y1, as its style says: font, colour, alignment, wrap, indent, underline, strikethrough */
static void draw_text(struct Data *d, struct RastPort *rp, i32 r, i32 c, LONG cx0, LONG cx1, LONG x0, LONG x1, LONG y0, LONG y1)
{
  Book *b = d->book;
  Sheet *s = SH(d);
  Cell *cell = sheet_cell(s, r, c);
  const Value *v;
  int st = sheet_cell_style(s, r, c);
  const Style *sty = &b->styles[st];
  const Font *fnt = &b->fonts[sty->font];
  Font cfnt, vfnt;
  LONG vsh = 0;
  CondLook lk;
  struct TextFont *tf;
  char t[1100];
  u32 col;
  int ha, isnum, lines = 1, li;
  LONG tw, avail = x1 - x0 - 3, ind = sty->indent * 9 * zoom(d) / 100, lh;
  struct TextExtent te;
  ULONG n;
  const char *starts[64]; int lens[64];
  if (!cell || (!cell->f && cell->v.t == V_EMPTY)) return;
  v = cell_value(b, SI(d), r, c);
  /* conditional formatting: its colour and font styles over the cell's own */
  if (s->ncfs && cond_look(b, SI(d), r, c, &lk) && (lk.bold || lk.italic || lk.underline || lk.strike)) {
    cfnt = *fnt;
    cfnt.bold |= lk.bold; cfnt.italic |= lk.italic; cfnt.underline |= lk.underline; cfnt.strike |= lk.strike;
    fnt = &cfnt;
  } else if (!s->ncfs) { lk.color = COLOR_AUTO; lk.fill = 0; }
  if (d->formulas && cell->f) { grid_cell_text(b, SI(d), r, c, 1, t, sizeof t, &col); isnum = 0; }
  else {
    if (v->t == V_NUM && sty->numfmt == 0) {
      /* General shows as many digits as the column has room for */
      char u[64];
      LONG cw = TextLength(rp, (STRPTR)"0", 1);
      int chars;
      tf = cell_font(d, fnt);
      if (!tf) tf = theme_font();
      SetFont(rp, tf);
      cw = TextLength(rp, (STRPTR)"0", 1);
      chars = (int)((cx1 - cx0 - 3) / (cw > 0 ? cw : 7));
      if (chars < 1) chars = 1;
      if (chars > 11) chars = 11;
      fmt_value(b, "General", v, u, sizeof u, &col, chars);
      utf8_to_latin1(u, (u32)strlen(u), t, sizeof t);
    } else grid_cell_text(b, SI(d), r, c, 0, t, sizeof t, &col);
    isnum = v->t == V_NUM;
    if (v->t == V_NUM && v->n == 0 && !s->showzeros) return;
  }
  /* superscript and subscript: two thirds the size, raised or lowered */
  if (fnt->vert) {
    vfnt = *fnt; vfnt.size = (u16)(fnt->size * 2 / 3);
    vsh = fnt->vert == 1 ? -(LONG)fnt->size * zoom(d) / 2400 : (LONG)fnt->size * zoom(d) / 6000;
    fnt = &vfnt;
  }
  tf = cell_font(d, fnt);
  if (!tf) tf = theme_font();
  SetFont(rp, tf);
  SetSoftStyle(rp, (fnt->underline ? FSF_UNDERLINED : 0), FSF_BOLD | FSF_ITALIC | FSF_UNDERLINED);
  SetDrMd(rp, JAM1);
  SetAPen(rp, ink(d, lk.color != COLOR_AUTO ? lk.color : col != COLOR_AUTO ? col : fnt->color, lk.fill ? lk.fill : sty->fill));
  ha = sty->halign;
  if (ha == HA_GENERAL) ha = isnum || v->t == V_ERR ? HA_RIGHT : v->t == V_BOOL ? HA_CENTER : HA_LEFT;
  if (ha == HA_CENTERSEL || ha == HA_JUSTIFY) ha = HA_CENTER;
  lh = tf->tf_YSize;
  /* wrapped text (or text with line breaks): lines broken at spaces to the cell's width */
  if (sty->wrap || strchr(t, '\n')) {
    const char *p = t;
    lines = 0;
    while (*p && lines < 64) {
      const char *e = p, *brk = NULL;
      while (*e && *e != '\n') {
        LONG w = TextLength(rp, (STRPTR)p, (ULONG)(e - p + 1));
        if (w > avail - ind && e > p && sty->wrap) break;
        if (*e == ' ') brk = e;
        e++;
      }
      if (*e && *e != '\n' && brk && sty->wrap) e = brk;
      starts[lines] = p; lens[lines] = (int)(e - p); lines++;
      p = e;
      while (*p == ' ') p++;
      if (*p == '\n') p++;
    }
    if (!lines) return;
  } else { starts[0] = t; lens[0] = (int)strlen(t); }
  for (li = 0; li < lines; li++) {
    LONG ty, tx;
    const char *ls = starts[li];
    int ll = lens[li];
    char hashes[64];
    tw = TextLength(rp, (STRPTR)ls, (ULONG)ll);
    if (isnum && lines == 1 && tw > avail) {
      /* a number too wide for its column shows as ##### */
      LONG hw = TextLength(rp, (STRPTR)"#", 1);
      int k = (int)(avail / (hw > 0 ? hw : 6));
      if (k < 1) k = 1;
      if (k > 63) k = 63;
      memset(hashes, '#', (size_t)k); hashes[k] = 0;
      ls = hashes; ll = k; tw = TextLength(rp, (STRPTR)ls, (ULONG)ll);
      x0 = cx0; x1 = cx1;
    }
    n = TextFit(rp, (STRPTR)ls, (ULONG)ll, &te, NULL, 1, (ULONG)(x1 - x0 - 2 > 0 ? x1 - x0 - 2 : 0), (ULONG)(y1 - y0 + 1 + 100));
    if (ha == HA_RIGHT) tx = x1 - 2 - te.te_Width - ind;
    else if (ha == HA_CENTER) tx = x0 + (x1 - x0 + 1 - te.te_Width) / 2;
    else tx = x0 + 2 + ind;
    if (tx < x0 + 1) tx = x0 + 1;
    if (sty->valign == VA_TOP) ty = y0 + 1 + li * lh;
    else if (sty->valign == VA_CENTER) ty = y0 + (y1 - y0 + 1 - lines * lh) / 2 + li * lh;
    else ty = y1 - 1 - (lines - li) * lh;
    if (ty < y0 && lines == 1) ty = y0;
    Move(rp, tx, ty + tf->tf_Baseline + vsh);
    Text(rp, (STRPTR)ls, n);
    if (fnt->strike) { Move(rp, tx, ty + tf->tf_Baseline - tf->tf_Baseline / 3); Draw(rp, tx + te.te_Width - 1, ty + tf->tf_Baseline - tf->tf_Baseline / 3); }
    if (fnt->underline == 2) { Move(rp, tx, ty + tf->tf_Baseline + 3); Draw(rp, tx + te.te_Width - 1, ty + tf->tf_Baseline + 3); }
  }
  SetSoftStyle(rp, 0, FSF_UNDERLINED);
}
/* a border line on one side of a cell: thickness and pattern from its kind (BS_) */
static void border_line(struct Data *d, struct RastPort *rp, int kind, ULONG color, LONG x0, LONG y0, LONG x1, LONG y1, int horiz)
{
  LONG pen = color == COLOR_AUTO || (color & 0xFF000000UL) || !color ? (theme_is_dark() ? d->pen_white : pen_for(d, 0)) : pen_for(d, color);
  int k, w = kind == BS_MEDIUM ? 2 : kind == BS_THICK ? 3 : 1;
  SetAPen(rp, pen);
  if (kind == BS_DOTTED || kind == BS_HAIR) SetDrPt(rp, 0xAAAA);
  else if (kind == BS_DASHED) SetDrPt(rp, 0xF0F0);
  for (k = 0; k < w; k++) {
    if (horiz) { Move(rp, x0, y0 + k - w / 2); Draw(rp, x1, y1 + k - w / 2); }
    else { Move(rp, x0 + k - w / 2, y0); Draw(rp, x1 + k - w / 2, y1); }
  }
  if (kind == BS_DOUBLE) {
    if (horiz) { Move(rp, x0, y0 + 2); Draw(rp, x1, y1 + 2); }
    else { Move(rp, x0 + 2, y0); Draw(rp, x1 + 2, y1); }
  }
  SetDrPt(rp, 0xFFFF);
}
/* the editor over the active cell: the text typed so far in a white box as wide as it needs, with
   the caret; dx, dy offset everything (the window's position when drawn straight into it) */
static void draw_editor(struct Data *d, struct RastPort *rp, LONG dx, LONG dy)
{
  Sheet *s = SH(d);
  int vr = vis_row(d, s->cur_r), vc = vis_col(d, s->cur_c), st;
  LONG x0, y0, x1, y1, tw, cx;
  struct TextFont *tf;
  Rect m;
  if (!d->editing || vr < 0 || vc < 0) return;
  x0 = d->vx[vc]; y0 = d->vy[vr]; x1 = d->vx[vc + 1] - 1; y1 = d->vy[vr + 1] - 1;
  if (sheet_merge_at(s, s->cur_r, s->cur_c, &m)) { x1 = col_x(d, m.c2 + 1) - 1; y1 = row_y(d, m.r2 + 1) - 1; }
  st = sheet_cell_style(s, s->cur_r, s->cur_c);
  tf = font_of(d, st);
  SetFont(rp, tf);
  SetSoftStyle(rp, 0, FSF_BOLD | FSF_ITALIC | FSF_UNDERLINED);
  tw = TextLength(rp, (STRPTR)d->ebuf, (ULONG)d->elen) + 10;
  if (x0 + tw > x1) x1 = x0 + tw;
  if (x1 > d->w - 2) {
    /* no room to the right: the box grows to the left instead */
    x1 = d->w - 2;
    if (x1 - x0 < tw) { x0 = x1 - tw; if (x0 < d->hw + 1) x0 = d->hw + 1; }
  }
  if (y1 - y0 < tf->tf_YSize + 2) y1 = y0 + tf->tf_YSize + 2;
  fill(rp, x0 + dx, y0 + dy, x1 + dx, y1 + dy, theme_is_dark() ? d->pen_cell : d->pen_white);
  SetAPen(rp, d->pen_accent);
  Move(rp, x0 + dx - 1, y0 + dy - 1); Draw(rp, x1 + dx + 1, y0 + dy - 1); Draw(rp, x1 + dx + 1, y1 + dy + 1); Draw(rp, x0 + dx - 1, y1 + dy + 1); Draw(rp, x0 + dx - 1, y0 + dy - 1);
  SetAPen(rp, d->pen_text); SetDrMd(rp, JAM1);
  {
    /* the text from where the caret can be seen */
    int first = 0;
    LONG room = x1 - x0 - 6;
    while (first < d->ecaret && TextLength(rp, (STRPTR)d->ebuf + first, (ULONG)(d->ecaret - first)) > room) first++;
    {
      struct TextExtent te;
      ULONG n = TextFit(rp, (STRPTR)d->ebuf + first, (ULONG)(d->elen - first), &te, NULL, 1, (ULONG)(room > 0 ? room : 0), 200);
      LONG ty = y1 - 1 - tf->tf_YSize;
      if (ty < y0) ty = y0;
      RefSpan sp[24];
      int ns = formula_ref_spans(d->book, d->ebuf, sp, 24), at = first, k;
      Move(rp, x0 + dx + 3, ty + dy + tf->tf_Baseline);
      /* the text in pieces: each reference in its colour, the rest in the text pen */
      while (at < first + (int)n) {
        int end = first + (int)n, pen = -1;
        for (k = 0; k < ns; k++) {
          if (at >= sp[k].pos && at < sp[k].pos + sp[k].len) { end = sp[k].pos + sp[k].len; pen = sp[k].colour; break; }
          if (sp[k].pos > at && sp[k].pos < end) end = sp[k].pos;
        }
        if (end > first + (int)n) end = first + (int)n;
        SetAPen(rp, pen >= 0 ? d->pen_ref[pen & 7] : d->pen_text);
        Text(rp, (STRPTR)d->ebuf + at, (ULONG)(end - at));
        at = end;
      }
      cx = x0 + 3 + TextLength(rp, (STRPTR)d->ebuf + first, (ULONG)(d->ecaret - first));
      if (d->caret_on) { SetAPen(rp, d->pen_text); RectFill(rp, cx + dx, ty + dy, cx + dx, ty + dy + tf->tf_YSize - 1); }
    }
  }
}
/* the rectangle of cells a..b on screen (clipped to the scrolling and frozen parts as they show) */
static int rect_px(struct Data *d, const Rect *a, LONG *x0, LONG *y0, LONG *x1, LONG *y1)
{
  *x0 = col_x(d, a->c1); *x1 = col_x(d, a->c2 + 1) - 1;
  *y0 = row_y(d, a->r1); *y1 = row_y(d, a->r2 + 1) - 1;
  /* a rectangle that starts in the frozen part and runs under the scrolled one */
  if (a->c1 < SH(d)->freezec && a->c2 >= SH(d)->freezec && a->c2 < SH(d)->left) *x1 = col_x(d, SH(d)->freezec) - 1;
  if (a->r1 < SH(d)->freezer && a->r2 >= SH(d)->freezer && a->r2 < SH(d)->top) *y1 = row_y(d, SH(d)->freezer) - 1;
  if (*x1 > d->w + 4) *x1 = d->w + 4;
  if (*y1 > d->h + 4) *y1 = d->h + 4;
  if (*x0 < d->hw - 4) *x0 = d->hw - 4;
  if (*y0 < d->hh - 4) *y0 = d->hh - 4;
  return *x1 >= *x0 && *y1 >= *y0;
}
/* the cells part: fills, gridlines, text, borders, merged areas */
static void draw_cells(struct Data *d, struct RastPort *rp)
{
  Book *b = d->book;
  Sheet *s = SH(d);
  Rect sel = sel_rect(d);
  int i, j, k;
  LONG x1 = d->vx[d->nvc], y1 = d->vy[d->nvr];
  fill(rp, d->hw, d->hh, d->w - 1, d->h - 1, d->pen_cell);
  /* gridlines on each cell's right and bottom edge */
  if (s->gridlines) {
    for (j = 0; j < d->nvc; j++) vline(rp, d->vx[j + 1] - 1, d->hh, d->h - 1, d->pen_grid);
    for (i = 0; i < d->nvr; i++) hline(rp, d->hw, d->w - 1, d->vy[i + 1] - 1, d->pen_grid);
  }
  (void)x1; (void)y1;
  /* fills (and the selection's tint) */
  for (i = 0; i < d->nvr; i++) {
    i32 r = d->vr[i];
    for (j = 0; j < d->nvc; j++) {
      i32 c = d->vc[j];
      int st = sheet_cell_style(s, r, c);
      ULONG f = b->styles[st].fill;
      int insel = in_rect(&sel, r, c) && !(r == s->cur_r && c == s->cur_c) && (sel.r1 != sel.r2 || sel.c1 != sel.c2);
      CondLook lk;
      lk.bar = -1;
      if (s->ncfs && cond_look(b, SI(d), r, c, &lk) && lk.fill) f = lk.fill;
      if (f) fill(rp, d->vx[j], d->vy[i], d->vx[j + 1] - 1, d->vy[i + 1] - 1, pen_for(d, f));
      if (lk.bar >= 0) {
        /* a data bar, as long as the value is big: its colour fading towards white in four steps, edged in the colour */
        LONG bx0 = d->vx[j] + 1, bx1 = bx0 + (d->vx[j + 1] - 3 - bx0) * lk.bar / 1000, by0 = d->vy[i] + 2, by1 = d->vy[i + 1] - 4, q;
        ULONG c1 = lk.barcolor & 0xFFFFFF;
        if (bx1 > bx0 && by1 > by0) {
          for (q = 0; q < 4; q++) {
            LONG k = 30 + q * 55, xa = bx0 + (bx1 - bx0) * q / 4, xb = bx0 + (bx1 - bx0) * (q + 1) / 4;
            ULONG m = ((((c1 >> 16) & 255) * (256 - k) + 255 * k) >> 8) << 16 | ((((c1 >> 8) & 255) * (256 - k) + 255 * k) >> 8) << 8 | (((c1 & 255) * (256 - k) + 255 * k) >> 8);
            if (xb >= xa) fill(rp, xa, by0, xb, by1, pen_for(d, m));
          }
          SetAPen(rp, pen_for(d, c1));
          Move(rp, bx0, by0); Draw(rp, bx1, by0); Draw(rp, bx1, by1); Draw(rp, bx0, by1); Draw(rp, bx0, by0);
        }
      }
    }
  }
  /* text: each occupied cell in the rows shown, text running over empty neighbours */
  for (i = 0; i < d->nvr; i++) {
    i32 r = d->vr[i];
    Row *row = r < s->nrows ? s->rows[r] : NULL;
    if (!row) continue;
    for (j = 0; j < d->nvc; j++) {
      i32 c = d->vc[j];
      LONG tx0 = d->vx[j], tx1 = d->vx[j + 1] - 1;
      Cell *cell = sheet_cell(s, r, c);
      int st, ha;
      Rect m;
      if (!cell || (!cell->f && cell->v.t == V_EMPTY)) continue;
      if (s->nmerges && sheet_merge_at(s, r, c, &m)) continue;      /* (drawn with the merges) */
      st = sheet_cell_style(s, r, c);
      ha = b->styles[st].halign;
      if (cell_value(b, SI(d), r, c)->t == V_STR && !b->styles[st].wrap && !d->formulas) {
        /* text runs on over empty cells: right for left-aligned, left for right-aligned, both ways centred */
        if (ha == HA_GENERAL || ha == HA_LEFT || ha == HA_CENTER || ha == HA_FILL)
          for (k = j + 1; k < d->nvc && (k != d->nfcol || SH(d)->left == SH(d)->freezec) && empty_cell(d, r, d->vc[k]) && !(s->nmerges && sheet_merge_at(s, r, d->vc[k], NULL)); k++) tx1 = d->vx[k + 1] - 1;
        if (ha == HA_RIGHT || ha == HA_CENTER)
          for (k = j - 1; k >= 0 && (k != d->nfcol - 1 || SH(d)->left == SH(d)->freezec) && empty_cell(d, r, d->vc[k]) && !(s->nmerges && sheet_merge_at(s, r, d->vc[k], NULL)); k--) tx0 = d->vx[k];
        if (ha == HA_CENTER) {
          /* centred: as much room each side */
          LONG l = d->vx[j] - tx0, rr = tx1 - (d->vx[j + 1] - 1), e = l < rr ? l : rr;
          tx0 = d->vx[j] - e; tx1 = d->vx[j + 1] - 1 + e;
        }
      }
      draw_text(d, rp, r, c, d->vx[j], d->vx[j + 1] - 1, tx0, tx1, d->vy[i], d->vy[i + 1] - 1);
    }
  }
  /* merged areas: one fill and the first cell's text over all of them */
  for (k = 0; k < s->nmerges; k++) {
    Rect *m = &s->merges[k];
    LONG mx0, my0, mx1, my1;
    int st = sheet_cell_style(s, m->r1, m->c1);
    ULONG f = b->styles[st].fill;
    int insel = in_rect(&sel, m->r1, m->c1) && !in_rect(m, s->cur_r, s->cur_c) && (sel.r1 != sel.r2 || sel.c1 != sel.c2);
    if (m->r2 < (d->nvr ? d->vr[0] : 0) && m->r2 < s->freezer) continue;
    if (!rect_px(d, m, &mx0, &my0, &mx1, &my1)) continue;
    if (mx0 >= d->w || my0 >= d->h) continue;
    fill(rp, mx0, my0, mx1 - 1, my1 - 1, f ? pen_for(d, f) : insel ? d->pen_sel : d->pen_cell);
    if (s->gridlines) { vline(rp, mx1, my0, my1, d->pen_grid); hline(rp, mx0, mx1, my1, d->pen_grid); }
    draw_text(d, rp, m->r1, m->c1, mx0, mx1, mx0, mx1, my0, my1);
  }
  /* borders */
  for (i = 0; i < d->nvr; i++) {
    i32 r = d->vr[i];
    for (j = 0; j < d->nvc; j++) {
      i32 c = d->vc[j];
      int st = sheet_cell_style(s, r, c);
      const Style *sy = &b->styles[st];
      LONG bx0 = d->vx[j] - 1, bx1 = d->vx[j + 1] - 1, by0 = d->vy[i] - 1, by1 = d->vy[i + 1] - 1;
      if (!(sy->border[0] | sy->border[1] | sy->border[2] | sy->border[3])) continue;
      if (sy->border[B_TOP]) border_line(d, rp, sy->border[B_TOP], sy->bcolor[B_TOP], bx0, by0, bx1, by0, 1);
      if (sy->border[B_BOTTOM]) border_line(d, rp, sy->border[B_BOTTOM], sy->bcolor[B_BOTTOM], bx0, by1, bx1, by1, 1);
      if (sy->border[B_LEFT]) border_line(d, rp, sy->border[B_LEFT], sy->bcolor[B_LEFT], bx0, by0, bx0, by1, 0);
      if (sy->border[B_RIGHT]) border_line(d, rp, sy->border[B_RIGHT], sy->bcolor[B_RIGHT], bx1, by0, bx1, by1, 0);
    }
  }
  /* notes: a small red triangle in the cell's top right corner */
  for (k = 0; k < s->nnotes; k++) {
    int vi = vis_row(d, s->notes[k].r), vj = vis_col(d, s->notes[k].c), t;
    LONG cx, cy;
    if (vi < 0 || vj < 0) continue;
    cx = d->vx[vj + 1] - 2; cy = d->vy[vi];
    SetAPen(rp, pen_for(d, 0xE00000));
    for (t = 0; t < 6; t++) { Move(rp, cx - 5 + t, cy + t); Draw(rp, cx, cy + t); }
  }
}
/* the headings: column letters and row numbers, those in the selection marked */
static void draw_headings(struct Data *d, struct RastPort *rp)
{
  Sheet *s = SH(d);
  Rect sel = sel_rect(d);
  int i;
  char t[16];
  if (!s->headings) return;
  fill(rp, 0, 0, d->w - 1, d->hh - 1, d->pen_head);
  fill(rp, 0, 0, d->hw - 1, d->h - 1, d->pen_head);
  SetFont(rp, theme_font());
  SetSoftStyle(rp, 0, FSF_BOLD | FSF_ITALIC | FSF_UNDERLINED);
  SetDrMd(rp, JAM1);
  for (i = 0; i < d->nvc; i++) {
    i32 c = d->vc[i];
    LONG x0 = d->vx[i], x1 = d->vx[i + 1] - 1, tw;
    int on = c >= sel.c1 && c <= sel.c2, whole = on && sel.r1 == 0 && sel.r2 >= MAXROW - 1;
    col_name(c, t);
    if (on) { fill(rp, x0, 0, x1, d->hh - 1, whole ? d->pen_sel : d->pen_headsel); fill(rp, x0, d->hh - 2, x1, d->hh - 1, d->pen_accent); }
    vline(rp, x1, 0, d->hh - 1, d->pen_headline);
    /* (0.1.1) the edge a drag would resize, or is resizing, lit */
    if (c == d->hot_col || (d->drag == DR_COLW && c == d->drag_idx)) fill(rp, x1 - 1, 0, x1 + 1 < d->w ? x1 + 1 : x1, d->hh - 1, d->pen_accent);
    tw = TextLength(rp, (STRPTR)t, (ULONG)strlen(t));
    SetAPen(rp, on ? d->pen_accent : d->pen_headtext);
    Move(rp, x0 + (x1 - x0 + 1 - tw) / 2, (d->hh - fh()) / 2 + theme_font()->tf_Baseline);
    if (x1 - x0 > tw) Text(rp, (STRPTR)t, (ULONG)strlen(t));
  }
  if (!(sel.r1 == 0 && sel.r2 >= MAXROW - 1)) {}
  hline(rp, 0, d->w - 1, d->hh - 1, d->pen_headline);
  for (i = 0; i < d->nvr; i++) {
    i32 r = d->vr[i];
    LONG y0 = d->vy[i], y1 = d->vy[i + 1] - 1, tw;
    int on = r >= sel.r1 && r <= sel.r2, whole = on && sel.c1 == 0 && sel.c2 >= MAXCOL - 1;
    sprintf(t, "%ld", (long)r + 1);
    if (on) { fill(rp, 0, y0, d->hw - 1, y1, whole ? d->pen_sel : d->pen_headsel); fill(rp, d->hw - 2, y0, d->hw - 1, y1, d->pen_accent); }
    hline(rp, 0, d->hw - 1, y1, d->pen_headline);
    if (r == d->hot_row || (d->drag == DR_ROWH && r == d->drag_idx)) fill(rp, 0, y1 - 1, d->hw - 1, y1 + 1 < d->h ? y1 + 1 : y1, d->pen_accent);
    tw = TextLength(rp, (STRPTR)t, (ULONG)strlen(t));
    /* the rows of a filtered table numbered in the accent colour (some rows are hidden) */
    SetAPen(rp, on || (s->nfcols && r > s->filter.r1 && r <= s->filter.r2) ? d->pen_accent : d->pen_headtext);
    if (y1 - y0 + 1 >= fh() - 2) { Move(rp, d->hw - 6 - tw, y0 + (y1 - y0 + 1 - fh()) / 2 + theme_font()->tf_Baseline); Text(rp, (STRPTR)t, (ULONG)strlen(t)); }
  }
  vline(rp, d->hw - 1, 0, d->h - 1, d->pen_headline);
  /* the corner: a triangle pointing at the cells (select all) */
  fill(rp, 0, 0, d->hw - 2, d->hh - 2, d->pen_head);
  {
    LONG cx = d->hw - 5, cy = d->hh - 5, k;
    SetAPen(rp, d->pen_headline);
    for (k = 0; k < 8 && k < d->hh - 6; k++) { Move(rp, cx - k, cy - k); Draw(rp, cx, cy - k); }
  }
}
/* the AutoFilter's buttons: a small box at the right of each cell of the filter's heading row,
   with a drop-down arrow, or a funnel when that column is filtered */
static int filter_btn(struct Data *d, int i, int j, LONG *x0, LONG *y0, LONG *x1, LONG *y1)
{
  Sheet *s = SH(d);
  LONG h, bs;
  if (s->filter.r1 < 0 || d->vr[i] != s->filter.r1 || d->vc[j] < s->filter.c1 || d->vc[j] > s->filter.c2) return 0;
  h = d->vy[i + 1] - d->vy[i];
  bs = fh() + 2;
  if (bs > h - 2) bs = h - 2;
  if (bs < 8 || d->vx[j + 1] - d->vx[j] < bs + 4) return 0;
  *x1 = d->vx[j + 1] - 3; *x0 = *x1 - bs + 1;
  *y1 = d->vy[i + 1] - 3; *y0 = *y1 - bs + 1;
  return 1;
}
static void draw_filters(struct Data *d, struct RastPort *rp)
{
  Sheet *s = SH(d);
  int i, j, k;
  LONG x0, y0, x1, y1, cx, cy;
  if (s->filter.r1 < 0) return;
  for (i = 0; i < d->nvr; i++) {
    if (d->vr[i] != s->filter.r1) continue;
    for (j = 0; j < d->nvc; j++) {
      int on;
      if (!filter_btn(d, i, j, &x0, &y0, &x1, &y1)) continue;
      if (x1 >= d->w || y1 >= d->h) continue;
      on = sheet_filter_col(s, d->vc[j]) != NULL;
      fill(rp, x0, y0, x1, y1, d->pen_head);
      SetAPen(rp, d->pen_headline);
      Move(rp, x0, y0); Draw(rp, x1, y0); Draw(rp, x1, y1); Draw(rp, x0, y1); Draw(rp, x0, y0);
      cx = (x0 + x1) / 2; cy = (y0 + y1) / 2;
      if (on) {
        /* a funnel, then a small arrow beside it */
        SetAPen(rp, d->pen_accent);
        for (k = 0; k < 4; k++) { Move(rp, cx - 4 + k, cy - 3 + k); Draw(rp, cx + 1 - k, cy - 3 + k); }
        fill(rp, cx - 2, cy + 1, cx - 1, cy + 4, d->pen_accent);
        SetAPen(rp, d->pen_headtext);
        for (k = 0; k < 3; k++) { Move(rp, cx + 2 + k, cy + 1 + k); Draw(rp, cx + 6 - k, cy + 1 + k); }
      } else {
        SetAPen(rp, d->pen_headtext);
        for (k = 0; k < 4; k++) { Move(rp, cx - 3 + k, cy - 1 + k); Draw(rp, cx + 3 - k, cy - 1 + k); }
      }
    }
  }
}
/* the filter button at grid point x, y: its column, or -1 */
static i32 filter_hit(struct Data *d, LONG x, LONG y)
{
  int i, j;
  LONG x0, y0, x1, y1;
  for (i = 0; i < d->nvr; i++)
    for (j = 0; j < d->nvc; j++)
      if (filter_btn(d, i, j, &x0, &y0, &x1, &y1) && x >= x0 && x <= x1 && y >= y0 && y <= y1) return d->vc[j];
  return -1;
}
/* the selection's outline and fill handle, the copied cells' marching outline, frozen pane lines,
   and point mode's reference */
static void draw_marks(struct Data *d, struct RastPort *rp)
{
  Sheet *s = SH(d);
  Rect sel = sel_rect(d);
  LONG x0, y0, x1, y1;
  /* frozen panes: a darker line after the frozen rows and columns */
  if (s->freezer && d->nfr) hline(rp, d->hw, d->w - 1, d->vy[d->nfr] - 1, d->pen_frozen);
  if (s->freezec && d->nfcol) vline(rp, d->vx[d->nfcol] - 1, d->hh, d->h - 1, d->pen_frozen);
  if (rect_px(d, &sel, &x0, &y0, &x1, &y1) && x0 < d->w && y0 < d->h) {
    Rect act;
    LONG ax0, ay0, ax1, ay1;
    SetAPen(rp, d->pen_accent);
    Move(rp, x0 - 1, y0 - 1); Draw(rp, x1, y0 - 1); Draw(rp, x1, y1); Draw(rp, x0 - 1, y1); Draw(rp, x0 - 1, y0 - 1);
    Move(rp, x0, y0); Draw(rp, x1 - 1, y0); Draw(rp, x1 - 1, y1 - 1); Draw(rp, x0, y1 - 1); Draw(rp, x0, y0);
    /* the active cell in a bigger selection: a thin box inside */
    act.r1 = act.r2 = s->cur_r; act.c1 = act.c2 = s->cur_c;
    sheet_merge_at(s, s->cur_r, s->cur_c, &act);
    if ((sel.r1 != act.r1 || sel.r2 != act.r2 || sel.c1 != act.c1 || sel.c2 != act.c2) && rect_px(d, &act, &ax0, &ay0, &ax1, &ay1)) {
      SetAPen(rp, d->pen_accent);
      Move(rp, ax0 + 1, ay0 + 1); Draw(rp, ax1 - 2, ay0 + 1); Draw(rp, ax1 - 2, ay1 - 2); Draw(rp, ax0 + 1, ay1 - 2); Draw(rp, ax0 + 1, ay0 + 1);
    }
    /* the fill handle */
    if (!d->editing && x1 < d->w && y1 < d->h) { fill(rp, x1 - 3, y1 - 3, x1 + 3, y1 + 3, d->pen_white); fill(rp, x1 - 2, y1 - 2, x1 + 2, y1 + 2, d->pen_accent); }
  }
  /* the fill handle being dragged: the area it will fill */
  if (d->drag == DR_FILL) {
    Rect f = sel;
    if (d->fill_r > sel.r2) f.r2 = d->fill_r; else if (d->fill_r < sel.r1) f.r1 = d->fill_r;
    else if (d->fill_c > sel.c2) f.c2 = d->fill_c; else if (d->fill_c < sel.c1) f.c1 = d->fill_c;
    if (rect_px(d, &f, &x0, &y0, &x1, &y1)) {
      SetAPen(rp, d->pen_frozen); SetDrPt(rp, 0xCCCC);
      Move(rp, x0, y0); Draw(rp, x1 - 1, y0); Draw(rp, x1 - 1, y1 - 1); Draw(rp, x0, y1 - 1); Draw(rp, x0, y0);
      SetDrPt(rp, 0xFFFF);
    }
  }
  /* the copied cells */
  if (d->copied && rect_px(d, &d->copy, &x0, &y0, &x1, &y1)) {
    UWORD pat = (UWORD)(0xF0F0 >> (d->ant & 7) | 0xF0F0 << (16 - (d->ant & 7)));
    SetAPen(rp, d->pen_accent); SetBPen(rp, d->pen_white); SetDrMd(rp, JAM2);
    SetDrPt(rp, pat);
    Move(rp, x0, y0); Draw(rp, x1 - 1, y0); Draw(rp, x1 - 1, y1 - 1); Draw(rp, x0, y1 - 1); Draw(rp, x0, y0);
    Move(rp, x0 + 1, y0 + 1); Draw(rp, x1 - 2, y0 + 1); Draw(rp, x1 - 2, y1 - 2); Draw(rp, x0 + 1, y1 - 2); Draw(rp, x0 + 1, y0 + 1);
    SetDrPt(rp, 0xFFFF); SetDrMd(rp, JAM1);
  }
  /* a formula being typed: each reference's cells boxed in its colour (point mode's moving one dashed) */
  if (d->editing && d->ebuf[0] == '=') {
    RefSpan sp[24];
    int ns = formula_ref_spans(d->book, d->ebuf, sp, 24), k;
    for (k = 0; k < ns; k++) {
      Rect p = sp[k].r;
      if (sp[k].sheet != SHEET_SELF && sp[k].sheet != SI(d)) continue;
      if (!rect_px(d, &p, &x0, &y0, &x1, &y1)) continue;
      SetAPen(rp, d->pen_ref[sp[k].colour & 7]);
      if (d->pt_on && sp[k].pos == d->pt_pos) SetDrPt(rp, 0xF0F0);
      Move(rp, x0, y0); Draw(rp, x1 - 1, y0); Draw(rp, x1 - 1, y1 - 1); Draw(rp, x0, y1 - 1); Draw(rp, x0, y0);
      Move(rp, x0 + 1, y0 + 1); Draw(rp, x1 - 2, y0 + 1); Draw(rp, x1 - 2, y1 - 2); Draw(rp, x0 + 1, y1 - 2); Draw(rp, x0 + 1, y0 + 1);
      SetDrPt(rp, 0xFFFF);
    }
  }
}
/* a box beside a cell (a note, or where a link goes): a pale yellow card with a first line in bold
   (the note's author) and the text wrapped, and a line from the cell's corner to it */
static void draw_box(struct Data *d, struct RastPort *rp, i32 r, i32 c, const char *bold_u, const char *text_u)
{
  int vi = vis_row(d, r), vj = vis_col(d, c), lines = 0, k;
  LONG bw = 190, lh = fh(), x0, y0, x1, y1, cx, cy;
  char t[1100], au[200];
  const char *starts[24]; int lens[24];
  const char *p;
  if (vi < 0 || vj < 0) return;
  utf8_to_latin1(text_u ? text_u : "", text_u ? (u32)strlen(text_u) : 0, t, sizeof t);
  utf8_to_latin1(bold_u ? bold_u : "", bold_u ? (u32)strlen(bold_u) : 0, au, sizeof au);
  SetFont(rp, theme_font());
  SetSoftStyle(rp, 0, FSF_BOLD | FSF_ITALIC | FSF_UNDERLINED);
  /* the text broken into lines that fit */
  for (p = t; *p && lines < 24; ) {
    const char *e = p, *brk = NULL;
    while (*e && *e != '\n' && TextLength(rp, (STRPTR)p, (ULONG)(e - p + 1)) <= bw - 10) { if (*e == ' ' || *e == '/') brk = e; e++; }
    if (*e && *e != '\n' && brk) e = brk + 1;
    if (e == p && *e && *e != '\n') e++;
    starts[lines] = p; lens[lines] = (int)(e - p); lines++;
    p = e;
    if (*p == ' ' || *p == '\n') p++;
  }
  cx = d->vx[vj + 1] - 1; cy = d->vy[vi];
  x0 = cx + 12; y0 = cy - 6;
  x1 = x0 + bw; y1 = y0 + 6 + (lines + (au[0] ? 1 : 0)) * lh + 4;
  /* kept in the grid: to the cell's left if there's no room on the right */
  if (x1 >= d->w) { x1 = d->vx[vj] - 12; x0 = x1 - bw; }
  if (y0 < d->hh) { y1 += d->hh - y0; y0 = d->hh; }
  if (y1 >= d->h) { y0 -= y1 - d->h + 1; y1 = d->h - 1; }
  SetAPen(rp, d->pen_headtext);
  Move(rp, cx, cy); Draw(rp, x0 < cx ? x1 : x0, y0 + 6);
  fill(rp, x0 + 3, y0 + 3, x1 + 3, y1 + 3, pen_for(d, 0x9A9A9A));        /* a shadow */
  fill(rp, x0, y0, x1, y1, pen_for(d, 0xFFFFE1));
  SetAPen(rp, pen_for(d, 0x6D6D6D));
  Move(rp, x0, y0); Draw(rp, x1, y0); Draw(rp, x1, y1); Draw(rp, x0, y1); Draw(rp, x0, y0);
  SetDrMd(rp, JAM1);
  SetAPen(rp, pen_for(d, 0x000000));
  y1 = y0 + 4;
  if (au[0]) {
    SetFont(rp, theme_bold());
    Move(rp, x0 + 5, y1 + theme_bold()->tf_Baseline); Text(rp, (STRPTR)au, (ULONG)strlen(au));
    SetFont(rp, theme_font());
    y1 += lh;
  }
  for (k = 0; k < lines; k++, y1 += lh) { Move(rp, x0 + 5, y1 + theme_font()->tf_Baseline); Text(rp, (STRPTR)starts[k], (ULONG)lens[k]); }
}
static void draw_note(struct Data *d, struct RastPort *rp, const Note *n) { draw_box(d, rp, n->r, n->c, n->author, n->text); }
static void draw_notes(struct Data *d, struct RastPort *rp)
{
  Sheet *s = SH(d);
  int k;
  const Note *n;
  char ln[600];
  if (d->show_notes) for (k = 0; k < s->nnotes; k++) draw_note(d, rp, &s->notes[k]);
  if (d->hover_r < 0 || d->editing) return;
  if (!d->show_notes && (n = sheet_note(s, d->hover_r, d->hover_c))) draw_note(d, rp, n);
  else if (book_cell_link(d->book, SI(d), d->hover_r, d->hover_c, ln, sizeof ln)) {
    /* a link: where it goes (its tip, if it has one), and how to follow it */
    const Link *l = sheet_link(s, d->hover_r, d->hover_c);
    char t[800];
    snprintf(t, sizeof t, "%s\n%s", l && l->tip && l->tip[0] ? l->tip : ln[0] == '#' ? ln + 1 : ln, tr("Ctrl-click to follow the link."));
    draw_box(d, rp, d->hover_r, d->hover_c, NULL, t);
  }
}
static LONG pen_cb(void *pd, ULONG rgb) { return pen_for((struct Data *)pd, rgb); }
static void draw_all(struct Data *d, struct RastPort *rp, LONG w, LONG h)
{
  GridGeom g;
  layout(d, w, h);
  draw_cells(d, rp);
  draw_headings(d, rp);
  draw_filters(d, rp);
  /* (layout.h) page breaks, page numbers and tracer arrows over the cells */
  g.obj = d->obj; g.book = d->book; g.sheet = SI(d); g.zoom = zoom(d); g.hw = d->hw; g.hh = d->hh; g.w = d->w; g.h = d->h;
  g.nvr = d->nvr; g.nvc = d->nvc; g.vr = d->vr; g.vy = d->vy; g.vc = d->vc; g.vx = d->vx; g.pen = pen_cb; g.pd = d;
  lx_grid_draw(rp, &g);
  draw_marks(d, rp);
  draw_notes(d, rp);
  draw_editor(d, rp, 0, 0);
}
/* the whole grid drawn afresh */
static void redraw(struct Data *d) { if (d->shown && _win(d->obj)) MUI_Redraw(d->obj, MADF_DRAWOBJECT); }

/* ------------------------------------------------------------------ scrolling */
/* scrolled so the active cell (or r, c) shows */
static void show_cell(struct Data *d, i32 r, i32 c)
{
  Sheet *s = SH(d);
  LONG h = d->h > 0 ? d->h : _mheight(d->obj), w = d->w > 0 ? d->w : _mwidth(d->obj), y, x;
  i32 k;
  if (r >= s->freezer) {
    if (r < s->top) s->top = r;
    else {
      for (;;) {
        y = d->hh;
        for (k = 0; k < s->freezer; k++) y += rowh(d, k);
        for (k = s->top; k <= r && y <= h; k++) y += rowh(d, k);
        if (y <= h || s->top >= r) break;
        s->top++;
      }
    }
  }
  if (c >= s->freezec) {
    if (c < s->left) s->left = c;
    else {
      for (;;) {
        x = d->hw;
        for (k = 0; k < s->freezec; k++) x += colw(d, k);
        for (k = s->left; k <= c && x <= w; k++) x += colw(d, k);
        if (x <= w || s->left >= c) break;
        s->left++;
      }
    }
  }
}
static void scroll_rows(struct Data *d, LONG n)
{
  Sheet *s = SH(d);
  LONG t = s->top + n;
  if (t < s->freezer) t = s->freezer;
  if (t > MAXROW - 1) t = MAXROW - 1;
  if (t == s->top) return;
  s->top = (i32)t;
  redraw(d);
  update_props(d);
}

/* ------------------------------------------------------------------ moving */
/* the active cell moved to r, c (extend: the selection's corner moves instead) and shown */
static void go(struct Data *d, i32 r, i32 c, int extend)
{
  Sheet *s = SH(d);
  Rect m;
  if (r < 0) r = 0;
  if (r >= MAXROW) r = MAXROW - 1;
  if (c < 0) c = 0;
  if (c >= MAXCOL) c = MAXCOL - 1;
  if (extend) { d->end_r = r; d->end_c = c; }
  else {
    if (s->nmerges && sheet_merge_at(s, r, c, &m)) { r = m.r1; c = m.c1; }
    s->cur_r = r; s->cur_c = c; d->end_r = r; d->end_c = c;
  }
  show_cell(d, r, c);
  redraw(d);
  update_props(d);
  moved(d);
}
/* the next row or column from i in direction dir, past hidden ones and the far side of a merge */
static i32 step_row(struct Data *d, i32 r, i32 c, int dir)
{
  Sheet *s = SH(d);
  Rect m;
  if (dir > 0 && s->nmerges && sheet_merge_at(s, r, c, &m)) r = m.r2;
  if (dir < 0 && s->nmerges && sheet_merge_at(s, r, c, &m)) r = m.r1;
  do r += dir; while (r > 0 && r < MAXROW - 1 && !sheet_row_height(s, r));
  return r;
}
static i32 step_col(struct Data *d, i32 r, i32 c, int dir)
{
  Sheet *s = SH(d);
  Rect m;
  if (dir > 0 && s->nmerges && sheet_merge_at(s, r, c, &m)) c = m.c2;
  if (dir < 0 && s->nmerges && sheet_merge_at(s, r, c, &m)) c = m.c1;
  do c += dir; while (c > 0 && c < MAXCOL - 1 && !sheet_col_width(s, c));
  return c;
}
/* Ctrl and an arrow: to the edge of the block of filled cells (or the next one, or the sheet's edge) */
static void data_edge(struct Data *d, i32 *r, i32 *c, int dr, int dc)
{
  i32 rr = *r, cc = *c, lastr = sheet_last_row(SH(d)), lastc = sheet_last_col(SH(d));
  int here = !empty_cell(d, rr, cc), next;
  if (rr + dr < 0 || cc + dc < 0) return;
  next = !empty_cell(d, rr + dr, cc + dc);
  if (here && next) {
    while (rr + dr >= 0 && cc + dc >= 0 && rr + dr < MAXROW && cc + dc < MAXCOL && !empty_cell(d, rr + dr, cc + dc)) { rr += dr; cc += dc; }
  } else {
    rr += dr; cc += dc;
    while (rr >= 0 && cc >= 0 && rr <= (dr > 0 ? lastr : MAXROW - 1) && cc <= (dc > 0 ? lastc : MAXCOL - 1) && empty_cell(d, rr, cc)) { rr += dr; cc += dc; }
    if (rr < 0 || cc < 0 || rr > (dr > 0 ? lastr : MAXROW - 1) || cc > (dc > 0 ? lastc : MAXCOL - 1)) {
      rr = dr > 0 ? MAXROW - 1 : dr < 0 ? 0 : rr - dr; cc = dc > 0 ? MAXCOL - 1 : dc < 0 ? 0 : cc - dc;
      if (dr) cc = *c;
      if (dc) rr = *r;
    }
  }
  *r = rr; *c = cc;
}

/* ------------------------------------------------------------------ editing */
static void edit_notify(struct Data *d) { SetAttrs(d->obj, MUIA_Grid_EditText, ++d->edited, TAG_DONE); }
static void edit_begin(struct Data *d, const char *text, int mode)
{
  Sheet *s = SH(d);
  if (text) { strncpy(d->ebuf, text, EMAX); d->ebuf[EMAX] = 0; }
  else grid_edit_text(d->book, SI(d), s->cur_r, s->cur_c, d->ebuf, sizeof d->ebuf);
  d->elen = (int)strlen(d->ebuf);
  d->ecaret = d->elen;
  d->editing = mode;
  d->caret_on = 1;
  d->pt_on = 0;
  d->copied = 0;
  show_cell(d, s->cur_r, s->cur_c);
  SetAttrs(d->obj, MUIA_Grid_Editing, mode, TAG_DONE);
  edit_notify(d);
  redraw(d);
}
static void edit_end(struct Data *d)
{
  d->editing = 0;
  d->pt_on = 0;
  SetAttrs(d->obj, MUIA_Grid_Editing, 0, TAG_DONE);
}
/* the editor's text into the cell (after a move: 0 stay, 1 down, 2 right, 3 up, 4 left, 5 every
   cell of the selection). 0 if the formula doesn't parse: still editing, the caret at the error */
static int commit(struct Data *d, int move)
{
  Sheet *s = SH(d);
  char u[EMAX * 3 + 4];
  int errpos = -1;
  Rect sel = sel_rect(d);
  if (!d->editing) return 1;
  latin1_utf8(d->ebuf, u, sizeof u);
  /* a formula without its closing brackets gets them, as Excel does */
  if (u[0] == '=') {
    int depth = 0, q = 0;
    char *p;
    for (p = u; *p; p++) { if (*p == '"') q = !q; else if (!q && *p == '(') depth++; else if (!q && *p == ')') depth--; }
    while (depth-- > 0 && strlen(u) + 1 < sizeof u) strcat(u, ")");
  }
  if (move == 5) {
    i32 r, c;
    int ok = 1;
    book_begin(d->book);
    for (r = sel.r1; r <= sel.r2 && ok; r++)
      for (c = sel.c1; c <= sel.c2 && ok; c++) {
        if (r == sel.r1 && c == sel.c1) ok = book_input(d->book, SI(d), r, c, u, &errpos);
        else {
          /* the formula as it would be copied there */
          Cell *first = sheet_cell(s, sel.r1, sel.c1);
          if (first && first->f) { Formula *f = formula_copy(first->f); if (f) formula_offset(f, r - sel.r1, c - sel.c1); book_set_formula(d->book, SI(d), r, c, f); }
          else book_input(d->book, SI(d), r, c, u, NULL);
        }
      }
    book_end(d->book);
    if (!ok) goto bad;
    edit_end(d);
    changed(d);
    redraw(d);
    moved(d);
    return 1;
  }
  if (!book_input(d->book, SI(d), s->cur_r, s->cur_c, u, &errpos)) goto bad;
  edit_end(d);
  changed(d);
  if (move) {
    /* within a bigger selection, Return and Tab move round it */
    int multi = sel.r1 != sel.r2 || sel.c1 != sel.c2;
    i32 r = s->cur_r, c = s->cur_c;
    if (move == 1) r = step_row(d, r, c, 1);
    else if (move == 3) r = step_row(d, r, c, -1);
    else if (move == 2) c = step_col(d, r, c, 1);
    else if (move == 4) c = step_col(d, r, c, -1);
    if (multi) {
      if (r > sel.r2) { r = sel.r1; c = c + 1 > sel.c2 ? sel.c1 : c + 1; }
      if (c > sel.c2) { c = sel.c1; r = r + 1 > sel.r2 ? sel.r1 : r + 1; }
      if (r < sel.r1) r = sel.r2;
      if (c < sel.c1) c = sel.c2;
      s->cur_r = r; s->cur_c = c;
      show_cell(d, r, c);
      redraw(d);
      update_props(d);
      SetAttrs(d->obj, MUIA_Grid_Moved, ++d->moved, TAG_DONE);
      return 1;
    }
    go(d, r, c, 0);
  } else { redraw(d); moved(d); }
  return 1;
bad:
  /* the caret near the problem (errpos counts UTF-8 bytes of the formula after "=") */
  if (errpos >= 0) { d->ecaret = errpos + 1 > d->elen ? d->elen : errpos + 1; }
  d->editing = 2;
  redraw(d);
  return 0;
}
static void edit_insert(struct Data *d, const char *t, int n)
{
  if (d->elen + n > EMAX) n = EMAX - d->elen;
  if (n <= 0) return;
  memmove(d->ebuf + d->ecaret + n, d->ebuf + d->ecaret, (size_t)(d->elen - d->ecaret + 1));
  memcpy(d->ebuf + d->ecaret, t, (size_t)n);
  d->elen += n; d->ecaret += n;
}
static void edit_delete(struct Data *d, int at, int n)
{
  if (at < 0) { n += at; at = 0; }
  if (n <= 0 || at >= d->elen) return;
  if (at + n > d->elen) n = d->elen - at;
  memmove(d->ebuf + at, d->ebuf + at + n, (size_t)(d->elen - at - n + 1));
  d->elen -= n;
  if (d->ecaret > at + n) d->ecaret -= n; else if (d->ecaret > at) d->ecaret = at;
}
/* point mode: a reference can be typed at the caret (a formula, after an operator, a bracket or a comma) */
static int pointable(struct Data *d)
{
  char ch;
  if (d->editing != 1 || d->elen == 0 || d->ebuf[0] != '=') return 0;
  if (d->pt_on && d->ecaret == d->pt_pos + d->pt_len) return 1;
  if (d->ecaret != d->elen) return 0;
  ch = d->ebuf[d->ecaret - 1];
  return strchr("=+-*/^&(,;<>:%", ch) != NULL;
}
/* the reference pointed at written into the formula (replacing the one pointed at before) */
static void point_write(struct Data *d)
{
  char t[40], a[16], b[16];
  if (!d->pt_on) { d->pt_pos = d->ecaret; d->pt_len = 0; d->pt_on = 1; }
  ref_text(imin(d->pt_r, d->pt_r2), imin(d->pt_c, d->pt_c2), 0, 0, a);
  if (d->pt_r != d->pt_r2 || d->pt_c != d->pt_c2) { ref_text(imax(d->pt_r, d->pt_r2), imax(d->pt_c, d->pt_c2), 0, 0, b); sprintf(t, "%s:%s", a, b); }
  else strcpy(t, a);
  d->ecaret = d->pt_pos + d->pt_len;
  edit_delete(d, d->pt_pos, d->pt_len);
  d->ecaret = d->pt_pos;
  edit_insert(d, t, (int)strlen(t));
  d->pt_len = (int)strlen(t);
  edit_notify(d);
}
static void point_move(struct Data *d, int dr, int dc, int extend)
{
  Sheet *s = SH(d);
  if (!d->pt_on) { d->pt_r = d->pt_r2 = s->cur_r; d->pt_c = d->pt_c2 = s->cur_c; }
  if (extend) { d->pt_r2 = imax(0, imin(MAXROW - 1, d->pt_r2 + dr)); d->pt_c2 = imax(0, imin(MAXCOL - 1, d->pt_c2 + dc)); }
  else { d->pt_r = d->pt_r2 = imax(0, imin(MAXROW - 1, (d->pt_on ? d->pt_r : s->cur_r) + dr)); d->pt_c = d->pt_c2 = imax(0, imin(MAXCOL - 1, (d->pt_on ? d->pt_c : s->cur_c) + dc)); }
  point_write(d);
  show_cell(d, d->pt_r2, d->pt_c2);
  redraw(d);
  update_props(d);
}

/* ------------------------------------------------------------------ the keyboard */
/* a key while editing: 1 if used */
static int edit_key(struct Data *d, UWORD code, UWORD q, const char *typed, int ntyped)
{
  int shift = (q & (IEQUALIFIER_LSHIFT | IEQUALIFIER_RSHIFT)) != 0, alt = (q & (IEQUALIFIER_LALT | IEQUALIFIER_RALT)) != 0;
  int ctrl = (q & IEQUALIFIER_CONTROL) != 0;
  switch (code) {
  case 0x45: edit_end(d); redraw(d); moved(d); return 1;                         /* Esc */
  case 0x44: case 0x43:                                                            /* Return */
    if (alt) { edit_insert(d, "\n", 1); d->pt_on = 0; edit_notify(d); redraw(d); return 1; }
    commit(d, ctrl ? 5 : shift ? 3 : 1); return 1;
  case 0x42: commit(d, shift ? 4 : 2); return 1;                                   /* Tab */
  case 0x4C: case 0x4D: case 0x4E: case 0x4F: {                                    /* arrows */
    int dr = code == 0x4C ? -1 : code == 0x4D ? 1 : 0, dc = code == 0x4E ? 1 : code == 0x4F ? -1 : 0;
    if (pointable(d)) { point_move(d, dr, dc, shift); return 1; }
    if (d->editing == 1) { commit(d, code == 0x4C ? 3 : code == 0x4D ? 1 : code == 0x4E ? 2 : 4); return 1; }
    if (dc < 0 && d->ecaret > 0) d->ecaret--;
    if (dc > 0 && d->ecaret < d->elen) d->ecaret++;
    if (dr < 0) d->ecaret = 0;
    if (dr > 0) d->ecaret = d->elen;
    d->pt_on = 0; d->caret_on = 1;
    redraw(d);
    return 1; }
  case 0x70: d->ecaret = 0; d->pt_on = 0; redraw(d); return 1;                     /* Home */
  case 0x71: d->ecaret = d->elen; d->pt_on = 0; redraw(d); return 1;               /* End */
  case 0x41:                                                                       /* Backspace */
    if (d->ecaret > 0) { edit_delete(d, d->ecaret - 1, 1); }
    if (d->pt_on && d->ecaret < d->pt_pos + d->pt_len) d->pt_on = 0;
    edit_notify(d); redraw(d); return 1;
  case 0x46: edit_delete(d, d->ecaret, 1); d->pt_on = 0; edit_notify(d); redraw(d); return 1;   /* Del */
  case 0x51: d->editing = d->editing == 1 ? 2 : 1; d->pt_on = 0; SetAttrs(d->obj, MUIA_Grid_Editing, d->editing, TAG_DONE); return 1;   /* F2: enter/edit */
  }
  if (ntyped > 0) {
    edit_insert(d, typed, ntyped);
    d->pt_on = 0;
    d->caret_on = 1;
    edit_notify(d);
    redraw(d);
    return 1;
  }
  return 0;
}
/* the text a key types (Latin-1, printable), from the keymap; its length */
static int key_text(struct IntuiMessage *im, char *out, int cap)
{
  struct InputEvent ie;
  char buf[8];
  LONG n, i;
  int o = 0;
  memset(&ie, 0, sizeof ie);
  ie.ie_Class = IECLASS_RAWKEY; ie.ie_Code = im->Code; ie.ie_Qualifier = im->Qualifier & ~IEQUALIFIER_CONTROL;
  ie.ie_EventAddress = (APTR *)*((ULONG *)im->IAddress);
  n = MapRawKey(&ie, (STRPTR)buf, sizeof buf, NULL);
  if (n > 0 && (u8)buf[0] == 0x9B) n = 0;
  for (i = 0; i < n && o < cap - 1; i++) if ((u8)buf[i] >= 32 && (u8)buf[i] != 127 && !((u8)buf[i] >= 0x80 && (u8)buf[i] < 0xA0)) out[o++] = buf[i];
  out[o] = 0;
  return o;
}
static int key(struct Data *d, Object *obj, struct IntuiMessage *im)
{
  Sheet *s = SH(d);
  UWORD code = im->Code, q = im->Qualifier;
  int shift = (q & (IEQUALIFIER_LSHIFT | IEQUALIFIER_RSHIFT)) != 0, alt = (q & (IEQUALIFIER_LALT | IEQUALIFIER_RALT)) != 0;
  int ctrl = (q & IEQUALIFIER_CONTROL) != 0;
  char typed[8];
  int n;
  i32 r = d->end_r, c = d->end_c;
  if (code & IECODE_UP_PREFIX) return 0;
  if (q & (IEQUALIFIER_LCOMMAND | IEQUALIFIER_RCOMMAND)) return 0;      /* menu shortcuts */
  n = (code >= 0x50 && code <= 0x59) || code == 0x5F ? 0 : key_text(im, typed, sizeof typed);
  if (d->editing) return edit_key(d, code, q, typed, n);
  if (!shift) { r = s->cur_r; c = s->cur_c; }
  switch (code) {
  case 0x4C: case 0x4D: case 0x4E: case 0x4F: {
    int dr = code == 0x4C ? -1 : code == 0x4D ? 1 : 0, dc = code == 0x4E ? 1 : code == 0x4F ? -1 : 0;
    if (ctrl || alt) data_edge(d, &r, &c, dr, dc);
    else if (dr) r = step_row(d, r, c, dr);
    else c = step_col(d, r, c, dc);
    go(d, r, c, shift);
    return 1; }
  case 0x48: case 0x49: {                                       /* Page Up, Page Down (Ctrl: the sheet before or after) */
    LONG k = scroll_rows_fit(d);
    if (ctrl) { DoMethod(_app(obj), MUIM_Application_ReturnID, code == 0x48 ? ID_PREVSHEET : ID_NEXTSHEET); return 1; }
    s->top = (i32)imax(s->freezer, s->top + (code == 0x48 ? -k : k));
    go(d, imax(0, r + (code == 0x48 ? -k : k)), c, shift);
    return 1; }
  case 0x70: go(d, ctrl ? 0 : r, 0, shift); return 1;                                  /* Home */
  case 0x71: if (ctrl) go(d, imax(0, sheet_last_row(s)), imax(0, sheet_last_col(s)), shift); else go(d, r, imax(0, sheet_last_col(s)), shift); return 1;   /* End */
  case 0x44: case 0x43: {                                       /* Return: down (within a selection, round it) */
    Rect sel = sel_rect(d);
    if (sel.r1 != sel.r2 || sel.c1 != sel.c2) { d->editing = 1; strcpy(d->ebuf, ""); commit(d, shift ? 3 : 1); d->editing = 0; }
    go(d, step_row(d, s->cur_r, s->cur_c, shift ? -1 : 1), s->cur_c, 0);
    return 1; }
  case 0x42: go(d, s->cur_r, step_col(d, s->cur_r, s->cur_c, shift ? -1 : 1), 0); return 1;   /* Tab */
  case 0x46: {                                                  /* Del: the selection's contents go */
    Rect sel = sel_rect(d);
    book_clear(d->book, SI(d), sel, CLR_CONTENTS);
    changed(d); redraw(d); moved(d);
    return 1; }
  case 0x41: edit_begin(d, "", 1); return 1;                    /* Backspace: the cell cleared and typed into */
  case 0x51: edit_begin(d, NULL, 2); return 1;                  /* F2 */
  case 0x45: if (d->copied) { d->copied = 0; redraw(d); } return 1;   /* Esc: the copied outline goes */
  case 0x58: DoMethod(_app(obj), MUIM_Application_ReturnID, shift ? ID_LX_CALCSHEET : ID_CALCNOW); return 1;   /* F9, Shift-F9 */
  case 0x56: DoMethod(_app(obj), MUIM_Application_ReturnID, ID_LX_SPELLING); return 1;   /* F7 */
  case 0x7A: case 0x7B: scroll_rows(d, code == 0x7A ? -3 : 3); return 1;             /* the mouse wheel */
  }
  /* Shift-F3 Insert Function, Shift-F11 a new sheet, Alt-= AutoSum, Ctrl-` show formulas */
  if (code == 0x52 && shift) { DoMethod(_app(obj), MUIM_Application_ReturnID, ID_FUNCTION); return 1; }
  if ((code == 0x4B || code == 0x5A) && shift) { DoMethod(_app(obj), MUIM_Application_ReturnID, ID_NEWSHEET); return 1; }
  if (code == 0x0C && alt) { DoMethod(_app(obj), MUIM_Application_ReturnID, ID_AUTOSUM); return 1; }
  if (code == 0x00 && ctrl) { DoMethod(_app(obj), MUIM_Application_ReturnID, ID_SHOWFORMULAS); return 1; }
  if (n > 0 && !ctrl) { edit_begin(d, typed, 1); return 1; }
  return 0;
}

/* ------------------------------------------------------------------ the mouse */
/* the event handler's IDCMP flags changed (moves and ticks only while dragging) */
static void events_set(struct Data *d, Object *obj, ULONG ev)
{
  if (d->ehn.ehn_Events == ev) return;
  DoMethod(_win(obj), MUIM_Window_RemEventHandler, (ULONG)&d->ehn);
  d->ehn.ehn_Events = ev;
  DoMethod(_win(obj), MUIM_Window_AddEventHandler, (ULONG)&d->ehn);
}
/* (ticks: a note under the mouse; 0.1.1: moves, for the resize pointer over the headings' edges) */
#define EV_IDLE (IDCMP_RAWKEY | IDCMP_MOUSEBUTTONS | IDCMP_MOUSEMOVE | IDCMP_INTUITICKS)
#define EV_DRAG (IDCMP_RAWKEY | IDCMP_MOUSEBUTTONS | IDCMP_MOUSEMOVE | IDCMP_INTUITICKS)
/* the column whose right edge is near x in the headings (-1 none) */
static i32 col_edge(struct Data *d, LONG x)
{
  int i;
  /* (0.1.1: five pixels either side of the line, so it is easy to catch) */
  for (i = d->nvc - 1; i >= 0; i--) if (x >= d->vx[i + 1] - 5 && x <= d->vx[i + 1] + 4) return d->vc[i];
  return -1;
}
static i32 row_edge(struct Data *d, LONG y)
{
  int i;
  for (i = d->nvr - 1; i >= 0; i--) if (y >= d->vy[i + 1] - 4 && y <= d->vy[i + 1] + 3) return d->vr[i];
  return -1;
}
/* (0.1.1) the resize pointers: a bar with arrows either side, across for columns and up and down
   for rows (sprite data: two planes a line; colour 19 inside, 18 round it), copied to chip memory
   on first use as sprites must be */
static const UWORD ptr_colsize_data[] = {
  0x0000, 0x0000,
  0x0180, 0x03C0,
  0x0180, 0x1FF8,
  0x0990, 0x3FFC,
  0x1188, 0x7FFE,
  0x2184, 0xFFFF,
  0x7FFE, 0xFFFF,
  0x2184, 0xFFFF,
  0x1188, 0x7FFE,
  0x0990, 0x3FFC,
  0x0180, 0x1FF8,
  0x0180, 0x03C0,
  0x0000, 0x0000
};
static const UWORD ptr_rowsize_data[] = {
  0x0000, 0x0000,
  0x0000, 0x0E00,
  0x0400, 0x1F00,
  0x0E00, 0x3F80,
  0x1500, 0x7FC0,
  0x2480, 0x7FC0,
  0x0400, 0x7FC0,
  0x0400, 0xFFF0,
  0xFFE0, 0xFFF0,
  0xFFE0, 0xFFF0,
  0x0400, 0xFFF0,
  0x0400, 0x7FC0,
  0x2480, 0x7FC0,
  0x1500, 0x7FC0,
  0x0E00, 0x3F80,
  0x0400, 0x1F00,
  0x0000, 0x0E00,
  0x0000, 0x0000
};

static UWORD *ptr_chip[2];
static UWORD *ptr_get(int rows)
{
  const UWORD *src = rows ? ptr_rowsize_data : ptr_colsize_data;
  ULONG n = rows ? sizeof ptr_rowsize_data : sizeof ptr_colsize_data;
  if (!ptr_chip[rows] && (ptr_chip[rows] = (UWORD *)AllocVec(n, MEMF_CHIP))) CopyMem((APTR)src, ptr_chip[rows], n);
  return ptr_chip[rows];
}
/* the window's pointer: the resize pointer for columns (1) or rows (2), or the usual one (0) */
static void resize_pointer(struct Data *d, Object *obj, int which)
{
  struct Window *w = _window(obj);
  UWORD *p;
  if (which == d->resize_ptr || !w) return;
  d->resize_ptr = which;
  if (!which) { ClearPointer(w); return; }
  if (!(p = ptr_get(which == 2))) return;
  if (which == 1) SetPointer(w, p, 11, 16, -8, -5);
  else SetPointer(w, p, 16, 16, -6, -8);
}
/* the heading edge under the mouse (x, y from the grid's corner) lit, and the pointer to match */
static void hover_edge(struct Data *d, Object *obj, LONG x, LONG y, int inside)
{
  i32 hc = -1, hr = -1;
  if (inside && SH(d)->headings) {
    if (y < d->hh && x >= d->hw) hc = col_edge(d, x);
    else if (x < d->hw && y >= d->hh) hr = row_edge(d, y);
  }
  resize_pointer(d, obj, hc >= 0 ? 1 : hr >= 0 ? 2 : 0);
  if (hc != d->hot_col || hr != d->hot_row) { d->hot_col = hc; d->hot_row = hr; redraw(d); }
}
/* whether x, y is on the fill handle */
static int on_fill_handle(struct Data *d, LONG x, LONG y)
{
  Rect sel = sel_rect(d);
  LONG x0, y0, x1, y1;
  if (!rect_px(d, &sel, &x0, &y0, &x1, &y1)) return 0;
  return x >= x1 - 4 && x <= x1 + 4 && y >= y1 - 4 && y <= y1 + 4;
}
/* the mouse while dragging: the selection, a row or column size, the fill handle or a pointed range;
   past the edges the grid scrolls (ticks keep it going while the mouse is still) */
static void drag_to(struct Data *d, Object *obj, LONG x, LONG y)
{
  Sheet *s = SH(d);
  i32 r, c;
  if (d->drag == DR_COLW) { d->drag_size = imax(0, d->drag_size + (x - d->drag_px) * 100 / zoom(d)); d->drag_px = x; redraw(d); return; }
  if (d->drag == DR_ROWH) { d->drag_size = imax(0, d->drag_size + (y - d->drag_px) * 100 / zoom(d)); d->drag_px = y; redraw(d); return; }
  if (y >= d->h && d->drag != DR_COLS) { s->top++; update_props(d); }
  else if (y < d->hh && s->top > s->freezer && d->drag != DR_COLS) { s->top--; update_props(d); }
  if (x >= d->w && d->drag != DR_ROWS) { s->left++; update_props(d); }
  else if (x < d->hw && s->left > s->freezec && d->drag != DR_ROWS) { s->left--; update_props(d); }
  layout(d, d->w, d->h);
  hit(d, x, y, &r, &c);
  switch (d->drag) {
  case DR_CELLS: if (r != d->end_r || c != d->end_c) { d->end_r = r; d->end_c = c; redraw(d); moved(d); } break;
  case DR_COLS: if (c != d->end_c) { d->end_c = c; redraw(d); moved(d); } break;
  case DR_ROWS: if (r != d->end_r) { d->end_r = r; redraw(d); moved(d); } break;
  case DR_FILL: {
    Rect sel = sel_rect(d);
    /* along whichever way the pointer has gone further out of the selection */
    LONG out_r = r > sel.r2 ? r - sel.r2 : r < sel.r1 ? sel.r1 - r : 0, out_c = c > sel.c2 ? c - sel.c2 : c < sel.c1 ? sel.c1 - c : 0;
    i32 fr = out_r >= out_c ? r : sel.r2, fc = out_r >= out_c ? sel.c2 : c;
    if (fr != d->fill_r || fc != d->fill_c) { d->fill_r = fr; d->fill_c = fc; redraw(d); }
    break; }
  case DR_POINT:
    if (r != d->pt_r2 || c != d->pt_c2) { d->pt_r2 = r; d->pt_c2 = c; point_write(d); redraw(d); }
    break;
  }
}
static void drag_end(struct Data *d, Object *obj)
{
  int k = d->drag;
  d->drag = DR_NONE;
  events_set(d, obj, EV_IDLE);
  if (k == DR_COLW) {
    Rect sel = sel_rect(d);
    i32 c;
    /* a column inside a selection of whole columns: they all take the width */
    if (sel.r1 == 0 && sel.r2 >= MAXROW - 1 && d->drag_idx >= sel.c1 && d->drag_idx <= sel.c2) {
      book_begin(d->book);
      for (c = sel.c1; c <= sel.c2; c++) book_set_col_width(d->book, SI(d), c, (u16)d->drag_size);
      book_end(d->book);
    } else book_set_col_width(d->book, SI(d), d->drag_idx, (u16)d->drag_size);
    if (!d->drag_size) book_hide_cols(d->book, SI(d), d->drag_idx, d->drag_idx, 1);
    changed(d); redraw(d); update_props(d);
  } else if (k == DR_ROWH) {
    Rect sel = sel_rect(d);
    i32 r;
    if (sel.c1 == 0 && sel.c2 >= MAXCOL - 1 && d->drag_idx >= sel.r1 && d->drag_idx <= sel.r2) {
      book_begin(d->book);
      for (r = sel.r1; r <= sel.r2 && r < sel.r1 + 5000; r++) book_set_row_height(d->book, SI(d), r, (u16)d->drag_size);
      book_end(d->book);
    } else book_set_row_height(d->book, SI(d), d->drag_idx, (u16)d->drag_size);
    changed(d); redraw(d); update_props(d);
  } else if (k == DR_FILL) {
    Rect sel = sel_rect(d), dst = sel;
    if (d->fill_r > sel.r2) dst.r2 = d->fill_r; else if (d->fill_r < sel.r1) dst.r1 = d->fill_r;
    else if (d->fill_c > sel.c2) dst.c2 = d->fill_c; else if (d->fill_c < sel.c1) dst.c1 = d->fill_c;
    if (dst.r1 != sel.r1 || dst.r2 != sel.r2 || dst.c1 != sel.c1 || dst.c2 != sel.c2) {
      book_fill(d->book, SI(d), sel, dst);
      SH(d)->cur_r = dst.r1; SH(d)->cur_c = dst.c1; d->end_r = dst.r2; d->end_c = dst.c2;
      changed(d); moved(d);
    }
    redraw(d);
  } else {
    if (k == DR_CELLS && d->brush) { d->brush = 0; SetAttrs(obj, MUIA_Grid_Brush, 0, TAG_DONE); }
    redraw(d);
  }
}
static ULONG handle_event(struct IClass *cl, Object *obj, struct MUIP_HandleEvent *m)
{
  struct Data *d = INST_DATA(cl, obj);
  struct IntuiMessage *im = m->imsg;
  LONG x, y;
  int inside;
  if (!im || !d->book || !d->shown) return 0;
  if (im->Class == IDCMP_RAWKEY) {
    Object *ao = NULL;
    if (key_grab) return 0;
    get(_win(obj), MUIA_Window_ActiveObject, &ao);
    if (ao && ao != obj) return 0;
    return key(d, obj, im) ? MUI_EventHandlerRC_Eat : 0;
  }
  x = im->MouseX - _mleft(obj); y = im->MouseY - _mtop(obj);
  inside = x >= 0 && y >= 0 && x < _mwidth(obj) && y < _mheight(obj);
  if (im->Class == IDCMP_MOUSEMOVE || im->Class == IDCMP_INTUITICKS) {
    if (d->drag == DR_LAYOUT) { lx_grid_mouse(obj, 1, x, y); return MUI_EventHandlerRC_Eat; }
    if (d->drag) { drag_to(d, obj, x, y); return MUI_EventHandlerRC_Eat; }
    if (im->Class == IDCMP_MOUSEMOVE) { hover_edge(d, obj, x, y, inside); return 0; }
    if (im->Class == IDCMP_INTUITICKS) {
      /* the mouse resting on a cell with a note (or a link) shows it */
      i32 r = -1, c = -1;
      char ln[8];
      if (inside && hit(d, x, y, &r, &c) == 0 && !sheet_note(SH(d), r, c) && !book_cell_link(d->book, SI(d), r, c, ln, sizeof ln)) r = c = -1;
      if (!inside) r = c = -1;
      if (r != d->hover_r || c != d->hover_c) { d->hover_r = r; d->hover_c = c; redraw(d); }
    }
    return 0;
  }
  if (im->Class != IDCMP_MOUSEBUTTONS) return 0;
  if (im->Code == SELECTUP && d->drag == DR_LAYOUT) { d->drag = DR_NONE; events_set(d, obj, EV_IDLE); lx_grid_mouse(obj, 2, x, y); return MUI_EventHandlerRC_Eat; }
  if (im->Code == SELECTUP) { if (d->drag) { drag_end(d, obj); return MUI_EventHandlerRC_Eat; } return 0; }
  if (im->Code != SELECTDOWN || !inside) return 0;
  if (!d->editing && lx_grid_mouse(obj, 0, x, y)) { d->drag = DR_LAYOUT; events_set(d, obj, EV_DRAG); return MUI_EventHandlerRC_Eat; }
  {
    Sheet *s = SH(d);
    i32 r, c, e;
    int region = hit(d, x, y, &r, &c), shift = (im->Qualifier & (IEQUALIFIER_LSHIFT | IEQUALIFIER_RSHIFT)) != 0;
    int dbl = DoubleClick(d->secs, d->micros, im->Seconds, im->Micros) && r == d->click_r && c == d->click_c && region == d->click_region;
    d->secs = im->Seconds; d->micros = im->Micros; d->click_r = r; d->click_c = c; d->click_region = region;
    set(_win(obj), MUIA_Window_ActiveObject, (ULONG)obj);
    key_grab = NULL;
    /* while typing a formula, a click points at a cell for it */
    if (region == 0 && d->editing && pointable(d)) {
      d->pt_on = 0;
      d->pt_r = d->pt_r2 = r; d->pt_c = d->pt_c2 = c;
      point_write(d);
      d->drag = DR_POINT;
      events_set(d, obj, EV_DRAG);
      redraw(d);
      return MUI_EventHandlerRC_Eat;
    }
    if (d->editing && !commit(d, 0)) return MUI_EventHandlerRC_Eat;
    if (region == 1 && (e = col_edge(d, x)) >= 0) {
      if (dbl) { DoMethod(obj, MUIM_Grid_Autofit, 0, e, e); return MUI_EventHandlerRC_Eat; }
      d->drag = DR_COLW; d->drag_idx = e; d->drag_px = x; d->drag_size = sheet_col_width(s, e);
      events_set(d, obj, EV_DRAG);
      return MUI_EventHandlerRC_Eat;
    }
    if (region == 2 && (e = row_edge(d, y)) >= 0) {
      if (dbl) { DoMethod(obj, MUIM_Grid_Autofit, 1, e, e); return MUI_EventHandlerRC_Eat; }
      d->drag = DR_ROWH; d->drag_idx = e; d->drag_px = y; d->drag_size = row_px100(d, e);
      events_set(d, obj, EV_DRAG);
      return MUI_EventHandlerRC_Eat;
    }
    if (region == 3) { s->cur_r = 0; s->cur_c = 0; d->end_r = MAXROW - 1; d->end_c = MAXCOL - 1; redraw(d); moved(d); return MUI_EventHandlerRC_Eat; }
    if (region == 1) {
      /* a column heading: the whole column (Shift: the columns from the active one) */
      if (!shift) { s->cur_r = 0; s->cur_c = c; }
      else s->cur_r = 0;
      d->end_r = MAXROW - 1; d->end_c = c;
      d->drag = DR_COLS; events_set(d, obj, EV_DRAG);
      redraw(d); moved(d);
      return MUI_EventHandlerRC_Eat;
    }
    if (region == 2) {
      if (!shift) { s->cur_c = 0; s->cur_r = r; }
      else s->cur_c = 0;
      d->end_c = MAXCOL - 1; d->end_r = r;
      d->drag = DR_ROWS; events_set(d, obj, EV_DRAG);
      redraw(d); moved(d);
      return MUI_EventHandlerRC_Eat;
    }
    /* Ctrl-click on a link follows it (main.c) */
    if (region == 0 && (im->Qualifier & IEQUALIFIER_CONTROL)) {
      char ln[8];
      if (book_cell_link(d->book, SI(d), r, c, ln, sizeof ln)) {
        /* (the cell made the active one: main.c follows the active cell's link) */
        s->cur_r = r; s->cur_c = c; d->end_r = r; d->end_c = c;
        redraw(d); moved(d);
        DoMethod(_app(obj), MUIM_Application_ReturnID, ID_LINKOPEN);
        return MUI_EventHandlerRC_Eat;
      }
    }
    /* the cells: a filter button opens its list (main.c) */
    if (region == 0 && (e = filter_hit(d, x, y)) >= 0) {
      d->filter_col = e;
      DoMethod(_app(obj), MUIM_Application_ReturnID, ID_FILTERCOL);
      return MUI_EventHandlerRC_Eat;
    }
    if (on_fill_handle(d, x, y)) {
      Rect sel = sel_rect(d);
      d->drag = DR_FILL; d->fill_r = sel.r2; d->fill_c = sel.c2;
      events_set(d, obj, EV_DRAG);
      return MUI_EventHandlerRC_Eat;
    }
    if (dbl && !shift) { edit_begin(d, NULL, 2); return MUI_EventHandlerRC_Eat; }
    if (shift) { d->end_r = r; d->end_c = c; }
    else {
      Rect mm;
      if (s->nmerges && sheet_merge_at(s, r, c, &mm)) { r = mm.r1; c = mm.c1; }
      s->cur_r = r; s->cur_c = c; d->end_r = r; d->end_c = c;
    }
    d->drag = DR_CELLS;
    events_set(d, obj, EV_DRAG);
    redraw(d);
    moved(d);
    return MUI_EventHandlerRC_Eat;
  }
}

/* ------------------------------------------------------------------ autofit */
/* columns from..to as wide as their widest entry, or rows as high as their biggest font */
static void autofit(struct Data *d, Object *obj, int rows, i32 from, i32 to)
{
  Sheet *s = SH(d);
  struct RastPort rp;
  i32 k, r;
  InitRastPort(&rp);
  book_begin(d->book);
  for (k = from; k <= to; k++) {
    if (rows) {
      Row *row = k < s->nrows ? s->rows[k] : NULL;
      LONG h = s->defheight;
      int i;
      if (row) for (i = 0; i < row->n; i++) {
        const Font *f = &d->book->fonts[d->book->styles[row->c[i].style].font];
        LONG need = ((LONG)f->size * 544 + 1500) / 3000;
        if (need > h) h = need;
      }
      book_set_row_height(d->book, SI(d), k, (u16)h);
      if (row) row->custom = 0;
      continue;
    }
    {
      LONG w = 0;
      for (r = 0; r < s->nrows; r++) {
        Cell *cell = s->rows[r] ? sheet_cell(s, r, k) : NULL;
        char t[512];
        u32 col;
        LONG tw;
        if (!cell || (!cell->f && cell->v.t == V_EMPTY) || (s->nmerges && sheet_merge_at(s, r, k, NULL))) continue;
        grid_cell_text(d->book, SI(d), r, k, d->formulas, t, sizeof t, &col);
        SetFont(&rp, font_of(d, cell->style));
        tw = TextLength(&rp, (STRPTR)t, (ULONG)strlen(t)) * 100 / zoom(d) + 9;
        if (tw > w) w = tw;
      }
      if (w > 0) book_set_col_width(d->book, SI(d), k, (u16)(w > 1500 ? 1500 : w));
    }
  }
  book_end(d->book);
  changed(d);
  redraw(d);
  update_props(d);
}

/* ------------------------------------------------------------------ the right-button menu */
static const struct { const char *label; ULONG id; } ctxitems[] = {
  { "Cut", ID_CUT }, { "Copy", ID_COPY }, { "Paste", ID_PASTE }, { "Paste Values", ID_PASTEVALUES }, { NULL, 0 },
  { "Insert Rows", ID_INSROWS }, { "Insert Columns", ID_INSCOLS }, { "Delete Rows", ID_DELROWS }, { "Delete Columns", ID_DELCOLS },
  { "Clear Contents", ID_CLEARCONTENTS }, { NULL, 0 },
  { "New or Edit Note...", ID_NOTENEW }, { "Delete Note", ID_NOTEDEL }, { NULL, 0 },
  { "Link...", ID_LINK }, { "Open Link", ID_LINKOPEN }, { "Remove Link", ID_LINKDEL }, { NULL, 0 },
  { "Sort A to Z", ID_SORTAZ }, { "Sort Z to A", ID_SORTZA }, { NULL, 0 },
  { "Format Cells...", ID_FORMATCELLS }, { "Row Height...", ID_ROWHEIGHT }, { "Column Width...", ID_COLWIDTH },
  { "Hide Rows", ID_HIDEROWS }, { "Hide Columns", ID_HIDECOLS }, { "Unhide Rows", ID_UNHIDEROWS }, { "Unhide Columns", ID_UNHIDECOLS } };
static Object *ctx_make(void)
{
  Object *strip, *menu;
  int i;
  strip = MenustripObject, MUIA_Family_Child, menu = MenuObject, MUIA_Menu_Title, (ULONG)tr("Cells"), End, End;
  if (!strip || !menu) return strip;
  for (i = 0; i < (int)(sizeof ctxitems / sizeof ctxitems[0]); i++) {
    Object *it = ctxitems[i].label ? MenuitemObject, MUIA_Menuitem_Title, (ULONG)tr(ctxitems[i].label), MUIA_UserData, ctxitems[i].id, End
                                   : MenuitemObject, MUIA_Menuitem_Title, (ULONG)NM_BARLABEL, End;
    if (it) DoMethod(menu, MUIM_Family_AddTail, (ULONG)it);
  }
  return strip;
}

/* ------------------------------------------------------------------ attributes */
static void mset(struct Data *d, Object *obj, struct opSet *msg, int init)
{
  struct TagItem *tags = msg->ops_AttrList, *t;
  while ((t = NextTagItem(&tags))) {
    switch (t->ti_Tag) {
    case MUIA_Grid_Book:
      d->book = (Book *)t->ti_Data;
      d->editing = 0; d->copied = 0; d->pt_on = 0;
      if (d->book) { Sheet *s = SH(d); Rect a = s->sel; d->end_r = s->cur_r; d->end_c = s->cur_c;
        if (in_rect(&a, s->cur_r, s->cur_c)) { d->end_r = a.r1 == s->cur_r ? a.r2 : a.r1; d->end_c = a.c1 == s->cur_c ? a.c2 : a.c1; } }
      if (!init) { close_fonts(d); redraw(d); update_props(d); }
      break;
    case MUIA_Grid_Fonts: d->fs = (FontSet *)t->ti_Data; break;
    case MUIA_Grid_VProp: d->vprop = (Object *)t->ti_Data; break;
    case MUIA_Grid_HProp: d->hprop = (Object *)t->ti_Data; break;
    case MUIA_Grid_Status: d->status = (Object *)t->ti_Data; break;
    case MUIA_Grid_Formulas: d->formulas = (int)t->ti_Data; if (!init) redraw(d); break;
    case MUIA_Grid_ShowNotes: d->show_notes = (int)t->ti_Data; if (!init) redraw(d); break;
    case MUIA_Grid_Brush: d->brush = (int)t->ti_Data; break;
    case MUIA_Grid_Zoom:
      if (d->book && (LONG)t->ti_Data != SH(d)->zoom) { SH(d)->zoom = (u16)t->ti_Data; close_fonts(d); if (!init) { redraw(d); update_props(d); } }
      break;
    case MUIA_Grid_Top:
      if (d->book) { i32 v = (i32)t->ti_Data + SH(d)->freezer; if (v != SH(d)->top) { SH(d)->top = v; redraw(d); } }
      break;
    case MUIA_Grid_Left:
      if (d->book) { i32 v = (i32)t->ti_Data + SH(d)->freezec; if (v != SH(d)->left) { SH(d)->left = v; redraw(d); } }
      break;
    case MUIA_Grid_Copied:
      if (t->ti_Data) { d->copy = *(Rect *)t->ti_Data; d->copied = 1; } else d->copied = 0;
      if (!init) redraw(d);
      break;
    }
  }
}

/* ------------------------------------------------------------------ the dispatcher */
static ULONG dispatch(__reg("a0") struct IClass *cl, __reg("a2") Object *obj, __reg("a1") Msg msg)
{
  struct Data *d;
  switch (msg->MethodID) {
  case OM_NEW:
    if (!(obj = (Object *)DoSuperMethodA(cl, obj, msg))) return 0;
    d = INST_DATA(cl, obj);
    memset(d, 0, sizeof *d);
    d->obj = obj;
    d->ehn.ehn_Object = obj; d->ehn.ehn_Class = cl; d->ehn.ehn_Events = EV_IDLE;
    d->hover_r = d->hover_c = -1;
    d->hot_col = d->hot_row = -1;
    d->ihn.ihn_Object = obj; d->ihn.ihn_Flags = MUIIHNF_TIMER; d->ihn.ihn_Method = MUIM_Grid_Blink;
    d->ihn.ihn_stuff.ihn_timer.ihn_millis = 500;
    d->click_r = -1;
    mset(d, obj, (struct opSet *)msg, 1);
    if ((d->ctxstrip = ctx_make())) set(obj, MUIA_ContextMenu, (ULONG)d->ctxstrip);
    return (ULONG)obj;
  case OM_DISPOSE:
    d = INST_DATA(cl, obj);
    if (d->ctxstrip) { set(obj, MUIA_ContextMenu, 0); MUI_DisposeObject(d->ctxstrip); d->ctxstrip = NULL; }
    break;
  case OM_SET:
    mset(INST_DATA(cl, obj), obj, (struct opSet *)msg, 0);
    break;
  case OM_GET: {
    struct opGet *g = (struct opGet *)msg;
    d = INST_DATA(cl, obj);
    switch (g->opg_AttrID) {
    case MUIA_Grid_Editing: *g->opg_Storage = (ULONG)d->editing; return TRUE;
    case MUIA_Grid_EditText: *g->opg_Storage = (ULONG)d->ebuf; return TRUE;
    case MUIA_Grid_Moved: *g->opg_Storage = d->moved; return TRUE;
    case MUIA_Grid_Changed: *g->opg_Storage = d->changed; return TRUE;
    case MUIA_Grid_Formulas: *g->opg_Storage = (ULONG)d->formulas; return TRUE;
    case MUIA_Grid_ShowNotes: *g->opg_Storage = (ULONG)d->show_notes; return TRUE;
    case MUIA_Grid_CtxPick: *g->opg_Storage = d->ctx_pick; return TRUE;
    case MUIA_Grid_FilterCol: *g->opg_Storage = (ULONG)d->filter_col; return TRUE;
    case MUIA_Grid_Zoom: *g->opg_Storage = d->book ? (ULONG)zoom(d) : 100; return TRUE;
    case MUIA_Grid_Brush: *g->opg_Storage = (ULONG)d->brush; return TRUE;
    }
    break;
  }
  case MUIM_AskMinMax: {
    struct MUIP_AskMinMax *m = (struct MUIP_AskMinMax *)msg;
    DoSuperMethodA(cl, obj, msg);
    m->MinMaxInfo->MinWidth += 160; m->MinMaxInfo->DefWidth += 640; m->MinMaxInfo->MaxWidth += MUI_MAXMAX;
    m->MinMaxInfo->MinHeight += 80; m->MinMaxInfo->DefHeight += 400; m->MinMaxInfo->MaxHeight += MUI_MAXMAX;
    return 0;
  }
  case MUIM_Setup:
    if (!DoSuperMethodA(cl, obj, msg)) return FALSE;
    d = INST_DATA(cl, obj);
    if (d->handlers) return TRUE;
    theme_setup(obj);
    get_pens(d, obj);
    theme_handler(obj, &d->ehn);
    DoMethod(_app(obj), MUIM_Application_AddInputHandler, (ULONG)&d->ihn);
    d->handlers = 1;
    return TRUE;
  case MUIM_Cleanup:
    d = INST_DATA(cl, obj);
    resize_pointer(d, obj, 0);
    d->hot_col = d->hot_row = -1;
    if (d->handlers) {
      DoMethod(_app(obj), MUIM_Application_RemInputHandler, (ULONG)&d->ihn);
      close_fonts(d);
      free_pens(d, obj);
      back_free(d);
      theme_cleanup(obj);
      d->ehn.ehn_Events = EV_IDLE;
      d->handlers = 0;
    }
    break;
  case MUIM_Show: ((struct Data *)INST_DATA(cl, obj))->shown = 1; break;
  case MUIM_Hide: d = INST_DATA(cl, obj); d->shown = 0; resize_pointer(d, obj, 0); d->hot_col = d->hot_row = -1; break;
  case MUIM_Draw: {
    struct MUIP_Draw *m = (struct MUIP_Draw *)msg;
    struct RastPort *rp;
    LONG w, h;
    DoSuperMethodA(cl, obj, msg);
    if (!(m->flags & (MADF_DRAWOBJECT | MADF_DRAWUPDATE))) return 0;
    d = INST_DATA(cl, obj);
    w = _mwidth(obj); h = _mheight(obj);
    if (!d->book || !d->pens) return 0;
    if ((rp = back_rp(d, obj, w, h))) {
      draw_all(d, rp, w, h);
      ClipBlit(rp, 0, 0, _rp(obj), _mleft(obj), _mtop(obj), w, h, 0xC0);
    } else {
      APTR clip = (APTR)MUI_AddClipping(muiRenderInfo(obj), _mleft(obj), _mtop(obj), w, h);
      struct Layer *ly = _rp(obj)->Layer;
      (void)ly;
      draw_all(d, _rp(obj), w, h);
      MUI_RemoveClipping(muiRenderInfo(obj), (struct Region *)clip);
    }
    update_props(d);
    return 0;
  }
  case MUIM_GoActive: case MUIM_GoInactive:
    return 0;
  case MUIM_HandleEvent:
    return handle_event(cl, obj, (struct MUIP_HandleEvent *)msg);
  case MUIM_ContextMenuBuild: {
    /* the right button outside the selection selects that cell first */
    struct MUIP_ContextMenuBuild *m = (struct MUIP_ContextMenuBuild *)msg;
    i32 r, c;
    Rect sel;
    d = INST_DATA(cl, obj);
    if (d->book && !d->editing && hit(d, m->mx - _mleft(obj), m->my - _mtop(obj), &r, &c) == 0) {
      sel = sel_rect(d);
      if (!in_rect(&sel, r, c)) { SH(d)->cur_r = r; SH(d)->cur_c = c; d->end_r = r; d->end_c = c; redraw(d); moved(d); }
    }
    return (ULONG)d->ctxstrip;
  }
  case MUIM_ContextMenuChoice: {
    Object *item = ((struct MUIP_ContextMenuChoice *)msg)->item;
    ULONG id = 0;
    d = INST_DATA(cl, obj);
    if (item) get(item, MUIA_UserData, &id);
    d->ctx_pick = id;
    if (id) DoMethod(_app(obj), MUIM_Application_ReturnID, id);
    return 0;
  }
  case MUIM_Grid_Blink:
    d = INST_DATA(cl, obj);
    if (!d->shown || !d->book || !_win(obj)) return 0;
    if (d->editing && !d->drag) {
      d->caret_on = !d->caret_on;
      draw_editor(d, _rp(obj), _mleft(obj), _mtop(obj));
    }
    if (d->copied && !d->drag) { d->ant += 2; redraw(d); }
    return 0;
  case MUIM_Grid_Refresh:
    d = INST_DATA(cl, obj);
    if (d->book) {
      Sheet *s = SH(d);
      if (d->end_r >= MAXROW) d->end_r = MAXROW - 1;
      if (d->end_c >= MAXCOL) d->end_c = MAXCOL - 1;
      sel_store(d);
      (void)s;
    }
    close_fonts(d);
    redraw(d);
    update_props(d);
    return 0;
  case MUIM_Grid_Edit: {
    struct MUIP_Grid_Edit *m = (struct MUIP_Grid_Edit *)msg;
    d = INST_DATA(cl, obj);
    if (!d->book) return 0;
    edit_begin(d, m->text, m->mode ? (int)m->mode : 2);
    return 0;
  }
  case MUIM_Grid_Commit:
    d = INST_DATA(cl, obj);
    return d->book ? (ULONG)commit(d, (int)((struct MUIP_Grid_Commit *)msg)->move) : 1;
  case MUIM_Grid_Cancel:
    d = INST_DATA(cl, obj);
    if (d->editing) { edit_end(d); redraw(d); moved(d); }
    return 0;
  case MUIM_Grid_Show:
    d = INST_DATA(cl, obj);
    if (d->book) { show_cell(d, SH(d)->cur_r, SH(d)->cur_c); redraw(d); update_props(d); }
    return 0;
  case MUIM_Grid_Insert: case MUIM_Grid_SetText: {
    struct MUIP_Grid_Insert *m = (struct MUIP_Grid_Insert *)msg;
    d = INST_DATA(cl, obj);
    if (!d->book) return 0;
    if (!d->editing) edit_begin(d, "", 2);
    if (msg->MethodID == MUIM_Grid_SetText) { d->elen = 0; d->ebuf[0] = 0; d->ecaret = 0; if (d->editing == 1) d->editing = 2; }
    edit_insert(d, m->text, (int)strlen(m->text));
    d->ecaret -= (int)m->back;
    if (d->ecaret < 0) d->ecaret = 0;
    d->pt_on = 0;
    if (msg->MethodID == MUIM_Grid_Insert) edit_notify(d);
    redraw(d);
    return 0;
  }
  case MUIM_Grid_Select: {
    struct MUIP_Grid_Select *m = (struct MUIP_Grid_Select *)msg;
    Sheet *s;
    d = INST_DATA(cl, obj);
    if (!d->book) return 0;
    s = SH(d);
    s->cur_r = (i32)m->cur_r; s->cur_c = (i32)m->cur_c;
    d->end_r = m->r1 == m->cur_r ? (i32)m->r2 : (i32)m->r1; d->end_c = m->c1 == m->cur_c ? (i32)m->c2 : (i32)m->c1;
    show_cell(d, s->cur_r, s->cur_c);
    redraw(d); update_props(d); moved(d);
    return 0;
  }
  case MUIM_Grid_Autofit: {
    struct MUIP_Grid_Autofit *m = (struct MUIP_Grid_Autofit *)msg;
    d = INST_DATA(cl, obj);
    if (d->book) autofit(d, obj, (int)m->rows, (i32)m->from, (i32)m->to);
    return 0;
  }
  }
  return DoSuperMethodA(cl, obj, msg);
}
struct MUI_CustomClass *grid_class(void)
{
  if (!cls) cls = MUI_CreateCustomClass(NULL, (STRPTR)MUIC_Area, NULL, sizeof(struct Data), (APTR)dispatch);
  return cls;
}
void grid_class_free(void)
{
  int i;
  if (cls) { MUI_DeleteCustomClass(cls); cls = NULL; }
  for (i = 0; i < 2; i++) if (ptr_chip[i]) { FreeVec(ptr_chip[i]); ptr_chip[i] = NULL; }
}
