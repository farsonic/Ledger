/*
 * viewtab.c - the View tab: Normal, Page Break Preview and Page Layout views (drawn over and
 * between the grid's cells), the ruler, custom views, zoom, and the workbook windows (New Window,
 * Arrange All, Hide/Unhide, View Side by Side with synchronous scrolling, Switch Windows).
 *
 * Page Break Preview: the print area's pages edged in blue (solid for manual breaks and the area's
 * edges, dashed for the breaks Ledger works out), "Page N" over each, the rest of the sheet greyed;
 * the blue lines can be dragged to move a break or the print area's edge. Page Layout: the grid
 * opens gaps between the pages (lx_gap_before), drawn as the pages' margins with their headers and
 * footers, and grey between pages.
 */
#include <graphics/gfxmacros.h>
#include "lxpriv.h"

double ledger_now(void);

static int ruler_on = 1;
static GridGeom lastg;                   /* the grid's last drawing (for dragging the breaks) */
static int sync_on, side_on, side_other = -1;

/* ------------------------------------------------------------------ the pages, cached between draws */
static struct { Book *b; int sheet; ULONG gen; Pages pg; int ok; } pc;
static ULONG vt_gen = 1;
static const Pages *pages_of(Book *b, int sheet)
{
  if (pc.ok && pc.b == b && pc.sheet == sheet && pc.gen == vt_gen) return &pc.pg;
  if (pc.ok) pages_free(&pc.pg);
  pc.ok = page_layout(b, sheet, &pc.pg);
  pc.b = b; pc.sheet = sheet; pc.gen = vt_gen;
  return pc.ok ? &pc.pg : NULL;
}
static int view_of(Book *b, int sheet) { return b && sheet >= 0 && sheet < b->nsh ? page_of(b->sh[sheet])->view : PV_NORMAL; }
/* the band (of rows or columns) that starts at idx, or -1 */
static int band_starting(const Pages *p, int rows, i32 idx)
{
  int i, n = rows ? p->nrows : p->ncols;
  const Band *bd = rows ? p->rows : p->cols;
  for (i = 0; i < n; i++) if (bd[i].a == idx) return i;
  return -1;
}
LONG lx_gap_before(Book *b, int sheet, int rows, i32 idx, LONG zoom)
{
  const Pages *p;
  const PageSetup *ps;
  int k;
  if (view_of(b, sheet) != PV_LAYOUT || !(p = pages_of(b, sheet))) return 0;
  if ((k = band_starting(p, rows, idx)) < 0) return 0;
  ps = page_of(b->sh[sheet]);
  /* the margins (inches at 96 pixels) before the band, and the previous page's after it */
  {
    double in = rows ? ps->margin[M_TOP] + (k ? ps->margin[M_BOTTOM] : 0) : ps->margin[M_LEFT] + (k ? ps->margin[M_RIGHT] : 0);
    return (LONG)(in * 96 * zoom / 100) + 12;
  }
}

/* ------------------------------------------------------------------ drawing */
static void hline(struct RastPort *rp, LONG x0, LONG x1, LONG y, LONG pen, int dashed, int thick)
{
  int k;
  SetAPen(rp, pen);
  SetDrPt(rp, dashed ? 0xF0F0 : 0xFFFF);
  for (k = 0; k < thick; k++) { Move(rp, x0, y + k); Draw(rp, x1, y + k); }
  SetDrPt(rp, 0xFFFF);
}
static void vline(struct RastPort *rp, LONG x, LONG y0, LONG y1, LONG pen, int dashed, int thick)
{
  int k;
  SetAPen(rp, pen);
  SetDrPt(rp, dashed ? 0xF0F0 : 0xFFFF);
  for (k = 0; k < thick; k++) { Move(rp, x + k, y0); Draw(rp, x + k, y1); }
  SetDrPt(rp, 0xFFFF);
}
/* the pixel of the top (left) of a visible row (column) idx, or -1 */
static LONG pos_of(const GridGeom *g, int rows, i32 idx)
{
  int i, n = rows ? g->nvr : g->nvc;
  const i32 *v = rows ? g->vr : g->vc;
  const LONG *p = rows ? g->vy : g->vx;
  for (i = 0; i < n; i++) if (v[i] == idx) return p[i];
  if (n && idx == v[n - 1] + 1) return p[n];
  return -1;
}
/* the pixel span of rows a..b on screen, clipped to what is shown; 0 if none of it is */
static int span_of(const GridGeom *g, int rows, i32 a, i32 b, LONG *p0, LONG *p1)
{
  int i, n = rows ? g->nvr : g->nvc, any = 0;
  const i32 *v = rows ? g->vr : g->vc;
  const LONG *p = rows ? g->vy : g->vx;
  for (i = 0; i < n; i++) {
    if (v[i] < a || v[i] > b) continue;
    if (!any) *p0 = p[i];
    *p1 = p[i + 1] - 1;
    any = 1;
  }
  return any;
}
/* (the lines Page Break Preview drew, for dragging): rows or columns, the index the line is
   before, its pixel, and what it is (0 a break, 1 the area's first edge, 2 its last) */
typedef struct { u8 rows, kind; i32 idx; LONG px; } BLine;
static BLine blines[64]; static int nblines;
static struct { int on; BLine l; LONG px; } drag;
static void add_line(int rows, int kind, i32 idx, LONG px)
{
  if (nblines >= 64 || px < 0) return;
  blines[nblines].rows = (u8)rows; blines[nblines].kind = (u8)kind; blines[nblines].idx = idx; blines[nblines].px = px;
  nblines++;
}
static void draw_breaks(struct RastPort *rp, const GridGeom *g, const Pages *p)
{
  Sheet *s = g->book->sh[g->sheet];
  LONG blue = g->pen(g->pd, 0x2F5597), grey = g->pen(g->pd, 0xA6A6A6), ax0, ax1, ay0, ay1;
  int i, j, k;
  static UWORD dither[2] = { 0xAAAA, 0x5555 };
  nblines = 0;
  /* outside the print area: greyed */
  SetAPen(rp, grey);
  SetAfPt(rp, dither, 1);
  for (i = 0; i < g->nvr; i++) {
    LONG y0 = g->vy[i], y1 = g->vy[i + 1] - 1;
    int inrow = g->vr[i] >= p->area.r1 && g->vr[i] <= p->area.r2;
    for (j = 0; j < g->nvc; j++) {
      if (inrow && g->vc[j] >= p->area.c1 && g->vc[j] <= p->area.c2) continue;
      RectFill(rp, g->vx[j], y0, g->vx[j + 1] - 1, y1);
    }
  }
  SetAfPt(rp, NULL, 0);
  if (!span_of(g, 1, p->area.r1, p->area.r2, &ay0, &ay1) || !span_of(g, 0, p->area.c1, p->area.c2, &ax0, &ax1)) return;
  /* the pages: their numbers, then their edges */
  for (k = 0; k < p->npages; k++) {
    int rb, cb;
    LONG x0, x1, y0, y1;
    page_bands(p, k, &rb, &cb);
    if (!span_of(g, 1, p->rows[rb].a, p->rows[rb].b, &y0, &y1) || !span_of(g, 0, p->cols[cb].a, p->cols[cb].b, &x0, &x1)) continue;
    {
      char t[32];
      struct TextFont *tf = theme_big();
      LONG w;
      sprintf(t, "%s %d", tr("Page"), k + 1);
      SetFont(rp, tf);
      w = TextLength(rp, (STRPTR)t, (ULONG)strlen(t));
      if (x1 - x0 > w + 4 && y1 - y0 > tf->tf_YSize + 4) {
        SetAPen(rp, grey); SetDrMd(rp, JAM1);
        Move(rp, (x0 + x1 - w) / 2, (y0 + y1 - tf->tf_YSize) / 2 + tf->tf_Baseline);
        Text(rp, (STRPTR)t, (ULONG)strlen(t));
      }
    }
  }
  for (i = 1; i < p->nrows; i++) {
    LONG y = pos_of(g, 1, p->rows[i].a);
    if (y < 0) continue;
    hline(rp, ax0, ax1, y - 1, blue, !page_has_break(s, 1, p->rows[i].a), 2);
    add_line(1, 0, p->rows[i].a, y);
  }
  for (j = 1; j < p->ncols; j++) {
    LONG x = pos_of(g, 0, p->cols[j].a);
    if (x < 0) continue;
    vline(rp, x - 1, ay0, ay1, blue, !page_has_break(s, 0, p->cols[j].a), 2);
    add_line(0, 0, p->cols[j].a, x);
  }
  /* the print area's edge, thick */
  if (pos_of(g, 1, p->area.r1) >= 0) { hline(rp, ax0, ax1, ay0, blue, 0, 3); add_line(1, 1, p->area.r1, ay0); }
  if (pos_of(g, 1, p->area.r2 + 1) >= 0) { hline(rp, ax0, ax1, ay1 - 2, blue, 0, 3); add_line(1, 2, p->area.r2 + 1, ay1 + 1); }
  if (pos_of(g, 0, p->area.c1) >= 0) { vline(rp, ax0, ay0, ay1, blue, 0, 3); add_line(0, 1, p->area.c1, ax0); }
  if (pos_of(g, 0, p->area.c2 + 1) >= 0) { vline(rp, ax1 - 2, ay0, ay1, blue, 0, 3); add_line(0, 2, p->area.c2 + 1, ax1 + 1); }
  if (drag.on) {
    if (drag.l.rows) hline(rp, g->hw, g->w - 1, drag.px, g->pen(g->pd, 0xC00000), 0, 2);
    else vline(rp, drag.px, g->hh, g->h - 1, g->pen(g->pd, 0xC00000), 0, 2);
  }
}
/* one header or footer's sections in a band of the screen */
static void hf_line(struct RastPort *rp, const GridGeom *g, const char *code, int page, int pages, LONG x0, LONG x1, LONG y, LONG pen)
{
  HFSection sec[3];
  HFContext cx;
  int k;
  if (!code || !code[0] || x1 - x0 < 30) return;
  memset(&cx, 0, sizeof cx);
  cx.page = page; cx.pages = pages; cx.sheet = g->book->sh[g->sheet]->name; cx.now = ledger_now();
  cx.file = bookpath[0] ? (const char *)FilePart((STRPTR)bookpath) : "Book1";
  hf_expand(code, &cx, sec);
  SetFont(rp, theme_font());
  SetAPen(rp, pen); SetDrMd(rp, JAM1);
  for (k = 0; k < 3; k++) {
    char u[300], l[300];
    LONG w, x;
    struct TextExtent te;
    ULONG fit;
    hf_plain(&sec[k], u, sizeof u);
    lx_utf8_latin1(u, l, sizeof l);
    if (!l[0]) continue;
    fit = TextFit(rp, (STRPTR)l, (ULONG)strlen(l), &te, NULL, 1, (ULONG)((x1 - x0) / 3), 100);
    w = te.te_Width;
    x = k == 0 ? x0 + 4 : k == 1 ? (x0 + x1 - w) / 2 : x1 - 4 - w;
    Move(rp, x, y + theme_font()->tf_Baseline);
    Text(rp, (STRPTR)l, fit);
  }
}
static void draw_layout(struct RastPort *rp, const GridGeom *g, const Pages *p)
{
  Sheet *s = g->book->sh[g->sheet];
  const PageSetup *ps = page_of(s);
  LONG grey = g->pen(g->pd, 0xC8C8C8), white = g->pen(g->pd, 0xFFFFFF), dim = g->pen(g->pd, 0x7F7F7F), edge = g->pen(g->pd, 0x8E8E8E);
  int i, j;
  LONG right = g->vx[g->nvc], bottom = g->vy[g->nvr];
  /* the gaps before each band: the previous page's bottom margin, grey, then this page's top margin */
  for (i = 0; i < g->nvr; i++) {
    int k = band_starting(p, 1, g->vr[i]);
    LONG gap, ytop, bm, tm;
    if (k < 0) continue;
    gap = lx_gap_before(g->book, g->sheet, 1, g->vr[i], g->zoom);
    ytop = g->vy[i] - gap;
    tm = (LONG)(ps->margin[M_TOP] * 96 * g->zoom / 100);
    bm = k ? (LONG)(ps->margin[M_BOTTOM] * 96 * g->zoom / 100) : 0;
    if (ytop < g->hh) ytop = g->hh;
    if (bm) theme_fill(rp, g->hw, ytop, right - 1, ytop + bm - 1, white);
    theme_fill(rp, g->hw, ytop + bm, right - 1, ytop + bm + 11, grey);
    theme_fill(rp, g->hw, ytop + bm + 12, right - 1, g->vy[i] - 1, white);
    hline(rp, g->hw, right - 1, ytop + bm + 11, edge, 0, 1);
    hline(rp, g->hw, right - 1, ytop + bm, edge, 0, 1);
    /* the header in this page's top margin, the previous page's footer in its bottom one */
    for (j = 0; j < g->nvc; j++) {
      int c = band_starting(p, 0, g->vc[j]), n;
      LONG x0, x1;
      if (c < 0) continue;
      x0 = g->vx[j];
      { int e = j + 1; while (e < g->nvc && band_starting(p, 0, g->vc[e]) < 0) e++; x1 = g->vx[e] - 1; }
      n = p->overthendown ? k * p->ncols + c + 1 : c * p->nrows + k + 1;
      if (tm > theme_font()->tf_YSize + 2) hf_line(rp, g, ps->hf[hf_which(ps, 0, n == 1, n)], n, p->npages, x0, x1, ytop + bm + 12 + (LONG)(ps->margin[M_HEADER] * 96 * g->zoom / 100) - 2, dim);
      if (k && bm > theme_font()->tf_YSize + 2) {
        int pn = p->overthendown ? (k - 1) * p->ncols + c + 1 : c * p->nrows + k;
        hf_line(rp, g, ps->hf[hf_which(ps, 1, pn == 1, pn)], pn, p->npages, x0, x1, ytop + bm - (LONG)(ps->margin[M_FOOTER] * 96 * g->zoom / 100) - theme_font()->tf_YSize, dim);
      }
    }
  }
  /* the gaps left of each column band */
  for (j = 0; j < g->nvc; j++) {
    int k = band_starting(p, 0, g->vc[j]);
    LONG gap, x0, rm;
    if (k < 0) continue;
    gap = lx_gap_before(g->book, g->sheet, 0, g->vc[j], g->zoom);
    x0 = g->vx[j] - gap;
    if (x0 < g->hw) x0 = g->hw;
    rm = k ? (LONG)(ps->margin[M_RIGHT] * 96 * g->zoom / 100) : 0;
    if (rm) theme_fill(rp, x0, g->hh, x0 + rm - 1, bottom - 1, white);
    theme_fill(rp, x0 + rm, g->hh, x0 + rm + 11, bottom - 1, grey);
    theme_fill(rp, x0 + rm + 12, g->hh, g->vx[j] - 1, bottom - 1, white);
    vline(rp, x0 + rm, g->hh, bottom - 1, edge, 0, 1);
    vline(rp, x0 + rm + 11, g->hh, bottom - 1, edge, 0, 1);
  }
  /* the ruler: inches from the page's left edge along the top of the column headings */
  if (ruler_on && g->hh > 6) {
    LONG x0 = -1, px;
    for (j = 0; j < g->nvc && x0 < 0; j++) if (band_starting(p, 0, g->vc[j]) >= 0) x0 = g->vx[j] - (LONG)(ps->margin[M_LEFT] * 96 * g->zoom / 100);
    if (x0 >= 0) {
      LONG step = 96 * g->zoom / 100 / 4;
      int t;
      if (step < 3) step = 3;
      for (t = 0, px = x0; px < g->w; t++, px += step) {
        if (px < g->hw) continue;
        vline(rp, px, 0, t % 4 == 0 ? 5 : 2, dim, 0, 1);
      }
    }
  }
}
void vt_draw(struct RastPort *rp, const GridGeom *g)
{
  int v = view_of(g->book, g->sheet);
  lastg = *g;
  const Pages *p;
  if (v == PV_NORMAL || !(p = pages_of(g->book, g->sheet))) { nblines = 0; return; }
  if (v == PV_BREAKS) draw_breaks(rp, g, p);
  else draw_layout(rp, g, p);
}
/* the row (or column) edge nearest a pixel, from the grid's last drawing */
static i32 nearest_edge(int rows, LONG px)
{
  int i, n = rows ? lastg.nvr : lastg.nvc, best = 0;
  const LONG *p = rows ? lastg.vy : lastg.vx;
  const i32 *v = rows ? lastg.vr : lastg.vc;
  LONG d = 0x7FFFFFFF;
  for (i = 0; i <= n; i++) { LONG e = p[i] - px; if (e < 0) e = -e; if (e < d) { d = e; best = i; } }
  return best < n ? v[best] : (n ? v[n - 1] + 1 : 0);
}
int lx_grid_mouse(Object *obj, int what, LONG x, LONG y)
{
  int i;
  if (what == 0) {
    if (!book || view_of(book, cur_index()) != PV_BREAKS) return 0;
    for (i = 0; i < nblines; i++) {
      LONG d = blines[i].rows ? y - blines[i].px : x - blines[i].px;
      if (d >= -3 && d <= 3) { drag.on = 1; drag.l = blines[i]; drag.px = blines[i].rows ? y : x; return 1; }
    }
    return 0;
  }
  if (!drag.on) return 0;
  drag.px = drag.l.rows ? y : x;
  if (what == 1) { MUI_Redraw(obj, MADF_DRAWOBJECT); return 1; }
  drag.on = 0;
  {
    Sheet *s = cur_sheet();
    PageSetup *p;
    const Pages *pg = pages_of(book, cur_index());
    i32 to = nearest_edge(drag.l.rows, drag.px);
    book_begin(book);
    book_note_sheet(book, cur_index());
    p = page_get(s);
    if (p && pg) {
      if (drag.l.kind == 0) {
        /* a break moved (an automatic one becomes manual) */
        page_set_break(s, drag.l.rows, drag.l.idx, 0);
        if (to > (drag.l.rows ? pg->area.r1 : pg->area.c1) && to <= (drag.l.rows ? pg->area.r2 : pg->area.c2)) page_set_break(s, drag.l.rows, to, 1);
      } else {
        /* the print area's edge */
        if (p->area.r1 < 0) p->area = pg->area;
        if (drag.l.rows && drag.l.kind == 1 && to <= p->area.r2) p->area.r1 = to;
        if (drag.l.rows && drag.l.kind == 2 && to - 1 >= p->area.r1) p->area.r2 = to - 1;
        if (!drag.l.rows && drag.l.kind == 1 && to <= p->area.c2) p->area.c1 = to;
        if (!drag.l.rows && drag.l.kind == 2 && to - 1 >= p->area.c1) p->area.c2 = to - 1;
      }
    }
    book_end(book);
    book->dirty = 1;
  }
  DoMethod(_app(obj), MUIM_Application_ReturnID, ID_LX_REFRESH);
  return 1;
}

/* ------------------------------------------------------------------ custom views (saved with the workbook) */
static void custom_views(void)
{
  static const ULONG ends[] = { ID_DLGOK, ID_DLG1, ID_DLG2, 0 };
  static char labels[MAXVIEWS][44];
  LayoutBook *x = book_lx(book);
  Object *w, *list, *add, *del;
  int i;
  ULONG r;
  if (!x) return;
  for (;;) {
    w = dialog_window("Custom Views", VGroup,
          Child, TextObject, MUIA_Text_Contents, (ULONG)tr("Views of this workbook:"), End,
          Child, ListviewObject, MUIA_Listview_List, list = ListObject, InputListFrame, MUIA_List_ConstructHook, MUIV_List_ConstructHook_String,
            MUIA_List_DestructHook, MUIV_List_DestructHook_String, End, MUIA_CycleChain, 1, End,
          Child, HGroup, Child, add = QButton(tr("Add...")), Child, del = QButton(tr("Delete")), Child, HSpace(0), End,
        End, "Show");
    if (!w) return;
    for (i = 0; i < x->nviews; i++) { lx_utf8_latin1(x->views[i].name, labels[i], sizeof labels[i]); DoMethod(list, MUIM_List_InsertSingle, (ULONG)labels[i], MUIV_List_Insert_Bottom); }
    if (x->nviews) set(list, MUIA_List_Active, 0);
    DoMethod(list, MUIM_Notify, MUIA_Listview_DoubleClick, TRUE, (ULONG)app, 2, MUIM_Application_ReturnID, ID_DLGOK);
    DoMethod(add, MUIM_Notify, MUIA_Pressed, FALSE, (ULONG)app, 2, MUIM_Application_ReturnID, ID_DLG1);
    DoMethod(del, MUIM_Notify, MUIA_Pressed, FALSE, (ULONG)app, 2, MUIM_Application_ReturnID, ID_DLG2);
    r = run_modal(w, ends, NULL);
    {
      LONG a = (LONG)lx_xget(list, MUIA_List_Active);
      MUI_DisposeObject(w);
      if (r == ID_DLG1) {
        char nm[40] = "";
        Sheet *s = cur_sheet();
        CustomView *v;
        if (!ask_string("Add View", "Name:", nm, sizeof nm) || !nm[0]) continue;
        if (x->nviews >= MAXVIEWS) { ask("OK", "%s", tr("A workbook keeps up to 24 views.")); continue; }
        v = &x->views[x->nviews++];
        memset(v, 0, sizeof *v);
        lx_latin1_utf8(nm, v->name, sizeof v->name);
        v->sheet = book->active; v->cur_r = s->cur_r; v->cur_c = s->cur_c; v->top = s->top; v->left = s->left;
        v->sel = s->sel; v->zoom = s->zoom ? s->zoom : 100; v->view = page_of(s)->view; v->grid = s->gridlines; v->head = s->headings;
        book->dirty = 1;
        continue;
      }
      if (r == ID_DLG2) {
        if (a >= 0 && a < x->nviews) { memmove(&x->views[a], &x->views[a + 1], (size_t)(x->nviews - a - 1) * sizeof(CustomView)); x->nviews--; book->dirty = 1; }
        continue;
      }
      if (r == ID_DLGOK && a >= 0 && a < x->nviews) {
        CustomView *v = &x->views[a];
        if (v->sheet < book->nsh) {
          Sheet *s;
          book->active = v->sheet;
          s = cur_sheet();
          s->cur_r = v->cur_r; s->cur_c = v->cur_c; s->top = v->top; s->left = v->left; s->sel = v->sel;
          s->gridlines = v->grid; s->headings = v->head; s->zoom = v->zoom;
          if (v->view || s->page) { PageSetup *p = page_get(s); if (p) p->view = v->view; }
          set(grid, MUIA_Grid_Book, (ULONG)book);
          vt_gen++;
          refresh_all();
        }
      }
      return;
    }
  }
}

/* ------------------------------------------------------------------ windows */
static void box_of(Object *w, LONG *b)
{
  struct Window *iw = w ? (struct Window *)lx_xget(w, MUIA_Window_Window) : NULL;
  b[0] = b[1] = b[2] = b[3] = 0;
  if (iw) { b[0] = iw->LeftEdge; b[1] = iw->TopEdge; b[2] = iw->Width; b[3] = iw->Height; }
}
static void place(Object *w, LONG x, LONG y, LONG wd, LONG ht)
{
  struct Window *iw = w ? (struct Window *)lx_xget(w, MUIA_Window_Window) : NULL;
  if (iw) ChangeWindowBox(iw, x, y, wd, ht);
}
/* the shown windows' slots (current first) */
static int shown_windows(int *out)
{
  int i, n = 0, cur = win_current(), hid;
  if (cur >= 0) out[n++] = cur;
  for (i = 0; i < win_slots(); i++) if (i != cur && win_info(i, NULL, NULL, NULL, &hid) && !hid) out[n++] = i;
  return n;
}
static void arrange(int how)
{
  int sl[16], n = shown_windows(sl), i, cols, rows;
  struct Window *iw = (struct Window *)lx_xget(win, MUIA_Window_Window);
  struct Screen *scr = iw ? iw->WScreen : NULL;
  LONG sw, sh, top;
  if (!scr || n < 1) return;
  top = scr->BarHeight + 1;
  sw = scr->Width; sh = scr->Height - top;
  for (i = 0; i < n; i++) {
    Object *w;
    LONG x, y, wd, ht;
    win_info(sl[i], &w, NULL, NULL, NULL);
    switch (how) {
    case 1: wd = sw; ht = sh / n; x = 0; y = top + i * ht; break;              /* horizontal: one above another */
    case 2: wd = sw / n; ht = sh; x = i * wd; y = top; break;                  /* vertical: side by side */
    case 3: wd = sw * 3 / 4; ht = sh * 3 / 4; x = i * 20; y = top + i * 16; break;   /* cascade */
    default:                                                                     /* tiled */
      cols = 1; while (cols * cols < n) cols++;
      rows = (n + cols - 1) / cols;
      wd = sw / cols; ht = sh / rows; x = (i % cols) * wd; y = top + (i / cols) * ht;
    }
    place(w, x, y, wd, ht);
  }
}
static void side_by_side(int on)
{
  int sl[16], n = shown_windows(sl);
  side_on = on;
  if (!on) { sync_on = 0; side_other = -1; return; }
  if (n < 2) { side_on = 0; ask("OK", "%s", tr("View Side by Side needs two workbook windows: open another (View > New Window).")); return; }
  if (n > 2) {
    static char names[16][60];
    static const char *ptrs[16];
    int k, m = 0, map[16];
    for (k = 1; k < n; k++) { const char *pth; win_info(sl[k], NULL, NULL, &pth, NULL); sprintf(names[m], "%d: %s", m + 1, pth && pth[0] ? (const char *)FilePart((STRPTR)pth) : tr("Book1")); ptrs[m] = names[m]; map[m++] = sl[k]; }
    k = lx_pick("Compare Side by Side", "Compare side by side with:", ptrs, m, 0);
    if (k < 0) { side_on = 0; return; }
    side_other = map[k];
  } else side_other = sl[1];
  sync_on = 1;
  {
    Object *a, *b;
    struct Window *iw = (struct Window *)lx_xget(win, MUIA_Window_Window);
    struct Screen *scr = iw ? iw->WScreen : NULL;
    LONG top, sw, sh;
    if (!scr) return;
    top = scr->BarHeight + 1; sw = scr->Width; sh = scr->Height - top;
    win_info(win_current(), &a, NULL, NULL, NULL); win_info(side_other, &b, NULL, NULL, NULL);
    /* (one above the other on a narrow screen, else side by side) */
    if (sw < 800) { place(a, 0, top, sw, sh / 2); place(b, 0, top + sh / 2, sw, sh - sh / 2); }
    else { place(a, 0, top, sw / 2, sh); place(b, sw / 2, top, sw - sw / 2, sh); }
  }
}
/* the other window of the pair follows this one's scrolling */
static void scrolled(void)
{
  Book *ob;
  Object *ow;
  int cur = win_current();
  Sheet *s = cur_sheet();
  if (!side_on || !sync_on || side_other < 0 || side_other == cur || !win_info(side_other, &ow, &ob, NULL, NULL)) return;
  if (ob && ob != book && ob->active < ob->nsh) {
    Sheet *o = ob->sh[ob->active];
    if (o->top != s->top || o->left != s->left) {
      o->top = s->top; o->left = s->left;
      {
        /* (the other window's grid: its first child group's grid, found by asking the window to redraw) */
        Object *root = (Object *)lx_xget(ow, MUIA_Window_RootObject);
        if (root) MUI_Redraw(root, MADF_DRAWOBJECT);
      }
    }
  }
}
static void unhide_window(void)
{
  static char names[16][60];
  static const char *ptrs[16];
  int i, n = 0, map[16], hid, k;
  for (i = 0; i < win_slots() && n < 16; i++) {
    const char *pth;
    if (!win_info(i, NULL, NULL, &pth, &hid) || !hid) continue;
    strncpy(names[n], pth && pth[0] ? (const char *)FilePart((STRPTR)pth) : tr("Book1"), 59); names[n][59] = 0;
    ptrs[n] = names[n]; map[n++] = i;
  }
  if (!n) { ask("OK", "%s", tr("No workbook windows are hidden.")); return; }
  k = lx_pick("Unhide", "Unhide workbook:", ptrs, n, 0);
  if (k >= 0) win_activate(map[k]);
}
static char swnames[16][64];
static const char *swptrs[17];
static int swmap[16];
void vt_switch_list(void)
{
  int i, n = 0, hid;
  for (i = 0; i < win_slots() && n < 16; i++) {
    const char *pth;
    Book *b;
    if (!win_info(i, NULL, &b, &pth, &hid)) continue;
    sprintf(swnames[n], "%d %s%s%s", n + 1, pth && pth[0] ? (const char *)FilePart((STRPTR)pth) : tr("Book1"), i == win_current() ? "  *" : "", hid ? tr("  (hidden)") : "");
    swptrs[n] = swnames[n]; swmap[n++] = i;
  }
  swptrs[n] = NULL;
  lx_lists[LXL_SWITCH - LXL_FIRST].labels = swptrs;
  lx_lists[LXL_SWITCH - LXL_FIRST].n = n;
}
static void zoom_dialog(void)
{
  static const char *const z_en[] = { "200%", "100%", "75%", "50%", "25%", "Fit selection", "Custom", NULL };
  static const char *zl[10];
  static const int zv[] = { 200, 100, 75, 50, 25 };
  static const ULONG ends[] = { ID_DLGOK, 0 };
  Object *w, *cy, *custom;
  int at = 6, i;
  Sheet *s = cur_sheet();
  lx_trl(z_en, zl);
  for (i = 0; i < 5; i++) if (s->zoom == zv[i]) at = i;
  w = dialog_window("Zoom", ColGroup(2),
        Child, Label1(tr("Magnification:")), Child, cy = lx_cycle(zl, at),
        Child, Label1(tr("Custom (%):")), Child, custom = lx_int(s->zoom ? s->zoom : 100, 4),
      End, NULL);
  if (!w) return;
  if (run_modal(w, ends, NULL) == ID_DLGOK) {
    int k = (int)lx_xget(cy, MUIA_Cycle_Active);
    long z = k < 5 ? zv[k] : atol(lx_sget(custom));
    if (k == 5) DoMethod(app, MUIM_Application_ReturnID, ID_ZOOMSEL);
    else if (z >= 10 && z <= 400) { set(grid, MUIA_Grid_Zoom, z); refresh_all(); }
  }
  MUI_DisposeObject(w);
}

/* ------------------------------------------------------------------ the commands */
static void set_view(int v)
{
  Sheet *s = cur_sheet();
  PageSetup *p = v == PV_NORMAL && !s->page ? NULL : page_get(s);
  if (p) {
    if (v == PV_BREAKS && p->view != PV_BREAKS && (!s->zoom || s->zoom == 100)) set(grid, MUIA_Grid_Zoom, 60);
    if (v != PV_BREAKS && p->view == PV_BREAKS && s->zoom == 60) set(grid, MUIA_Grid_Zoom, 100);
    if (v == PV_LAYOUT && (s->freezer || s->freezec)) book_freeze(book, cur_index(), 0, 0);   /* (Excel leaves frozen panes out of Page Layout view) */
    p->view = (u8)v;
  }
  book->dirty = 1;
  vt_gen++;
  refresh_all();
}
void vt_command(ULONG id, LONG v)
{
  switch (id) {
  case ID_LX_NORMAL: set_view(PV_NORMAL); break;
  case ID_LX_PBP: set_view(PV_BREAKS); break;
  case ID_LX_PAGELAYOUT: set_view(PV_LAYOUT); break;
  case ID_LX_CUSTOMVIEWS: custom_views(); break;
  case ID_LX_RULER: ruler_on = !ruler_on; vt_gen++; refresh_all(); break;
  case ID_LX_NEWWIN: win_new_shared(); break;
  case ID_LX_ARRANGE: {
    static const char *const how[] = { "Tiled", "Horizontal", "Vertical", "Cascade" };
    int k = lx_pick("Arrange Windows", "Arrange the workbook windows:", how, 4, 0);
    if (k >= 0) arrange(k);
    break; }
  case ID_LX_SPLIT:
    ask("OK", "%s", tr("Ledger can't split a window into panes that scroll on their own yet.\nFreeze Panes keeps rows and columns in sight; New Window shows two parts at once."));
    break;
  case ID_LX_HIDEWIN: win_hide(); break;
  case ID_LX_UNHIDEWIN: unhide_window(); break;
  case ID_LX_SIDEBYSIDE: side_by_side(!side_on); refresh_all(); break;
  case ID_LX_SYNCSCROLL: sync_on = !sync_on; refresh_all(); break;
  case ID_LX_RESETWIN: if (side_on) { side_on = 0; side_by_side(1); } break;
  case ID_LX_SWITCHDD:
    vt_switch_list();
    if (v < 0) v = lx_pick("Switch Windows", "Go to:", swptrs, lx_lists[LXL_SWITCH - LXL_FIRST].n, 0);
    if (v >= 0 && v < lx_lists[LXL_SWITCH - LXL_FIRST].n) win_activate(swmap[v]);
    break;
  case ID_LX_ZOOMDLG: zoom_dialog(); break;
  case ID_LX_SCROLLED: scrolled(); break;
  case ID_LX_REFRESH: vt_gen++; refresh_all(); break;
  }
}
int vt_bit(int b)
{
  int v = view_of(book, cur_index());
  switch (b) {
  case LXB_NORMAL: return v == PV_NORMAL;
  case LXB_PBP: return v == PV_BREAKS;
  case LXB_PAGELAYOUT: return v == PV_LAYOUT;
  case LXB_RULER: return ruler_on;
  case LXB_SPLIT: return 0;
  case LXB_SIDEBYSIDE: return side_on;
  case LXB_SYNC: return side_on && sync_on;
  case LXB_MANYWIN: { int sl[16]; return shown_windows(sl) > 1; }
  }
  return 0;
}
void vt_update(void)
{
  Object *strip = win ? (Object *)lx_xget(win, MUIA_Window_Menustrip) : NULL;
  static const ULONG ids[] = { ID_LX_NORMAL, ID_LX_PBP, ID_LX_PAGELAYOUT, ID_LX_RULER, ID_LX_SIDEBYSIDE, ID_LX_SYNCSCROLL, ID_LX_PRINTGRID, ID_LX_PRINTHEAD };
  static const int bits[] = { LXB_NORMAL, LXB_PBP, LXB_PAGELAYOUT, LXB_RULER, LXB_SIDEBYSIDE, LXB_SYNC, LXB_PRINTGRID, LXB_PRINTHEAD };
  int i;
  vt_gen++;
  vt_switch_list();
  if (side_on && side_other >= 0 && !win_info(side_other, NULL, NULL, NULL, NULL)) { side_on = 0; side_other = -1; }
  for (i = 0; strip && i < (int)(sizeof ids / sizeof ids[0]); i++) {
    Object *it = (Object *)DoMethod(strip, MUIM_FindUData, ids[i]);
    if (it) set(it, MUIA_Menuitem_Checked, lx_bit(bits[i]));
  }
}
