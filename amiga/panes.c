/*
 * panes.c - the strips round Ledger's grid, drawn by Ledger (MUI custom classes) in the theme's
 * colours: the status bar, the sheet tabs and the flat scroll bars.
 *
 * The status bar: what the grid is doing (Ready, Enter, Edit, Point) on the left; the average,
 * count and sum of the selected cells, then the zoom slider, on the right.
 *
 * The sheet tabs: a tab for each sheet (not the hidden ones) with its name and colour, the active
 * one white and underlined; arrows scroll them when there are too many to show, and the + after the
 * last adds a sheet. A double-click renames, the right button has the rest.
 */
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <devices/inputevent.h>
#include <proto/exec.h>
#include <proto/intuition.h>
#include <proto/graphics.h>
#include <proto/muimaster.h>
#include <proto/utility.h>
#include <clib/alib_protos.h>
#include "ledger.h"
#include "theme.h"
#include "lang.h"
#include "ids.h"

/* the interface font's height in pixels: rows and boxes are sized from it */
static int fh(void) { return theme_font()->tf_YSize; }
/* s (Latin-1) at x, y (y the top of the text, not the baseline) in plain tf and pen, cut short
   to fit maxw pixels; returns the width drawn */
static LONG clipped_text(struct RastPort *rp, struct TextFont *tf, LONG x, LONG y, const char *s, LONG maxw, LONG pen)
{
  struct TextExtent te;
  ULONG n;
  SetFont(rp, tf);
  SetSoftStyle(rp, 0, FSF_BOLD | FSF_ITALIC | FSF_UNDERLINED);
  n = TextFit(rp, (STRPTR)s, (ULONG)strlen(s), &te, NULL, 1, (ULONG)(maxw > 0 ? maxw : 0), 100);
  SetAPen(rp, pen);
  SetDrMd(rp, JAM1);
  Move(rp, x, y + tf->tf_Baseline);
  Text(rp, (STRPTR)s, n);
  return te.te_Width;
}
typedef struct { WORD x0, y0, x1, y1; } NBox;
static int in_nbox(NBox *b, WORD x, WORD y) { return x >= b->x0 && x <= b->x1 && y >= b->y0 && y <= b->y1; }
/* (ledger.h) an object typing goes to instead of the grid, or NULL */
Object *key_grab;
static struct MUI_CustomClass *scls, *tcls;

/* ================================================================== */
/* the status bar */
struct SData {
  int shown;
  char mode[16], stats[160];
  LONG zoom;
  int manual;
  NBox stat, calc, minus, plus, slider, pct;
  int hot, drag;
  LONG zoomset;                  /* the zoom chosen on the bar (MUIA_Status_Zoom) */
  struct MUI_EventHandlerNode ehn;
};
/* the slider: 10% at the left, 100% in the middle, 400% at the right (two linear halves).
   zoom_x: the window x of zoom z */
static LONG zoom_x(struct SData *d, LONG z)
{
  LONG w = d->slider.x1 - d->slider.x0, mid = d->slider.x0 + w / 2;
  if (z <= 100) return d->slider.x0 + (z - 10) * (w / 2) / 90;
  return mid + (z - 100) * (w / 2) / 300;
}
/* and the other way: the zoom at window x, 10 to 400, snapping to 100% near the middle */
static LONG x_zoom(struct SData *d, LONG x)
{
  LONG w = d->slider.x1 - d->slider.x0, mid = d->slider.x0 + w / 2, z;
  if (w <= 0) return 100;
  if (x <= mid) z = 10 + (x - d->slider.x0) * 90 / (w / 2 ? w / 2 : 1);
  else z = 100 + (x - mid) * 300 / (w / 2 ? w / 2 : 1);
  if (z > 92 && z < 108) z = 100;            /* sticks at 100% */
  return z < 10 ? 10 : z > 400 ? 400 : z;
}
/* draws the bar and, as it goes, records where each part is (the boxes status_hit tests) */
static void status_draw(struct SData *d, Object *obj)
{
  struct RastPort *rp = _rp(obj);
  LONG l = _mleft(obj), t = _mtop(obj), r = _mright(obj), b = _mbottom(obj), y = t + (b - t + 1 - fh()) / 2 + 1, w, rx, pw;
  char s[24];
  theme_fill(rp, l, t, r, b, tpen(T_STATUS));
  SetAPen(rp, tpen(T_BORDER)); Move(rp, l, t); Draw(rp, r, t);
  /* the left: the mode, and Calculate when calculation is manual */
  theme_text(rp, theme_font(), l + 8, y, d->mode, tpen(T_DIM));
  w = theme_textlen(rp, theme_font(), d->mode);
  d->calc.x0 = d->calc.x1 = -1;
  if (d->manual) {
    LONG cx = l + 8 + w + 18, cw = theme_textlen(rp, theme_font(), "Calculate");
    d->calc.x0 = (WORD)(cx - 4); d->calc.x1 = (WORD)(cx + cw + 3); d->calc.y0 = (WORD)(t + 1); d->calc.y1 = (WORD)b;
    if (d->hot == 5) theme_fill(rp, d->calc.x0, d->calc.y0, d->calc.x1, d->calc.y1, tpen(T_HOVER));
    theme_text(rp, theme_font(), cx, y, "Calculate", tpen(T_ACCENT));
  }
  /* the right, from the right edge: percent, plus, slider, minus, then the statistics */
  rx = r - 6;
  sprintf(s, "%ld%%", (long)d->zoom);
  pw = theme_textlen(rp, theme_font(), "400%");
  d->pct.x1 = (WORD)rx; d->pct.x0 = (WORD)(rx - pw); d->pct.y0 = (WORD)(t + 1); d->pct.y1 = (WORD)b;
  if (d->hot == 3) theme_fill(rp, d->pct.x0 - 2, d->pct.y0, d->pct.x1, d->pct.y1, tpen(T_HOVER));
  theme_text(rp, theme_font(), rx - theme_textlen(rp, theme_font(), s), y, s, tpen(T_DIM));
  rx -= pw + 6;
  d->plus.x1 = (WORD)rx; d->plus.x0 = (WORD)(rx - 10); d->plus.y0 = (WORD)(t + 2); d->plus.y1 = (WORD)(b - 1);
  SetAPen(rp, tpen(T_ICON));
  Move(rp, d->plus.x0 + 1, (t + b) / 2); Draw(rp, d->plus.x1 - 1, (t + b) / 2);
  Move(rp, (d->plus.x0 + d->plus.x1) / 2, (t + b) / 2 - 4); Draw(rp, (d->plus.x0 + d->plus.x1) / 2, (t + b) / 2 + 4);
  rx -= 14;
  d->slider.x1 = (WORD)rx; d->slider.x0 = (WORD)(rx - (r - l > 500 ? 100 : 70)); d->slider.y0 = (WORD)(t + 2); d->slider.y1 = (WORD)(b - 1);
  SetAPen(rp, tpen(T_DIM));
  Move(rp, d->slider.x0, (t + b) / 2); Draw(rp, d->slider.x1, (t + b) / 2);
  { LONG mx = (d->slider.x0 + d->slider.x1) / 2; Move(rp, mx, (t + b) / 2 - 3); Draw(rp, mx, (t + b) / 2 + 3); }
  {
    LONG kx = zoom_x(d, d->zoom < 10 ? 10 : d->zoom > 400 ? 400 : d->zoom);
    theme_fill(rp, kx - 2, (t + b) / 2 - 5, kx + 2, (t + b) / 2 + 5, tpen(d->hot == 4 || d->drag ? T_ACCENT : T_ICON));
  }
  rx = d->slider.x0 - 4;
  d->minus.x1 = (WORD)rx; d->minus.x0 = (WORD)(rx - 10); d->minus.y0 = (WORD)(t + 2); d->minus.y1 = (WORD)(b - 1);
  SetAPen(rp, tpen(T_ICON));
  Move(rp, d->minus.x0 + 1, (t + b) / 2); Draw(rp, d->minus.x1 - 1, (t + b) / 2);
  rx = d->minus.x0 - 16;
  /* the statistics, right-aligned before the zoom, as much as fits after the mode */
  d->stat.x0 = d->stat.x1 = -1;
  if (d->stats[0]) {
    LONG left = (d->calc.x1 > 0 ? d->calc.x1 : l + 8 + w) + 16, sw = theme_textlen(rp, theme_font(), d->stats);
    LONG sx = rx - sw;
    if (sx < left) sx = left;
    clipped_text(rp, theme_font(), sx, y, d->stats, rx - sx, tpen(T_TEXT));
    d->stat.x0 = (WORD)sx; d->stat.x1 = (WORD)rx; d->stat.y0 = (WORD)(t + 1); d->stat.y1 = (WORD)b;
  }
}
/* the part of the bar at x, y: 1 statistics, 3 percent, 4 slider, 5 Calculate, 6 minus, 7 plus;
   0 none. d->hot holds the same numbers */
static int status_hit(struct SData *d, WORD x, WORD y)
{
  if (d->stat.x0 >= 0 && in_nbox(&d->stat, x, y)) return 1;
  if (in_nbox(&d->pct, x, y)) return 3;
  if (in_nbox(&d->slider, x, y)) return 4;
  if (d->calc.x0 >= 0 && in_nbox(&d->calc, x, y)) return 5;
  if (in_nbox(&d->minus, x, y)) return 6;
  if (in_nbox(&d->plus, x, y)) return 7;
  return 0;
}
/* a zoom chosen on the bar: kept for MUIA_Status_Zoom and main.c told (ID_ZOOMSET) */
static void status_zoom(struct SData *d, Object *obj, LONG z)
{
  d->zoomset = z;
  DoMethod(_app(obj), MUIM_Application_ReturnID, ID_ZOOMSET);
}
/* the bar's MUIM_HandleEvent: hover, clicks and dragging the zoom slider (the zoom applied when
   the button is let go) */
static ULONG status_events(struct IClass *cl, Object *obj, struct MUIP_HandleEvent *m)
{
  struct SData *d = INST_DATA(cl, obj);
  struct IntuiMessage *im = m->imsg;
  int inside, h;
  if (!im || !d->shown) return 0;
  inside = im->MouseX >= _mleft(obj) && im->MouseX <= _mright(obj) && im->MouseY >= _mtop(obj) && im->MouseY <= _mbottom(obj);
  if (im->Class == IDCMP_MOUSEMOVE) {
    if (d->drag) { d->zoom = x_zoom(d, im->MouseX); MUI_Redraw(obj, MADF_DRAWOBJECT); return MUI_EventHandlerRC_Eat; }
    h = inside ? status_hit(d, im->MouseX, im->MouseY) : 0;
    if (h != d->hot) { d->hot = h; MUI_Redraw(obj, MADF_DRAWOBJECT); }
    return 0;
  }
  if (im->Class != IDCMP_MOUSEBUTTONS) return 0;
  if (im->Code == SELECTUP && d->drag) { d->drag = 0; status_zoom(d, obj, d->zoom); return MUI_EventHandlerRC_Eat; }
  if (im->Code != SELECTDOWN || !inside) return 0;
  switch (status_hit(d, im->MouseX, im->MouseY)) {
  case 1: DoMethod(_app(obj), MUIM_Application_ReturnID, ID_STATS); break;
  case 3: status_zoom(d, obj, 100); break;
  case 4: d->drag = 1; d->zoom = x_zoom(d, im->MouseX); MUI_Redraw(obj, MADF_DRAWOBJECT); break;
  case 5: DoMethod(_app(obj), MUIM_Application_ReturnID, ID_CALCNOW); break;
  case 6: status_zoom(d, obj, d->zoom > 20 ? (d->zoom - 10) / 10 * 10 : 10); break;
  case 7: status_zoom(d, obj, d->zoom < 390 ? (d->zoom + 10) / 10 * 10 : 400); break;
  default: return 0;
  }
  return MUI_EventHandlerRC_Eat;
}
/* the status bar's bubbles: what each part shows, and what a click on it does */
static const char *status_help(struct SData *d, WORD x, WORD y)
{
  switch (status_hit(d, x, y)) {
  case 1: return theme_bubble(tr("Selection"), tr("The average, count and sum of the numbers in the selected cells. Click for the workbook's statistics."));
  case 3: return theme_bubble(tr("Zoom"), tr("How big the cells are shown. Click to go back to 100%."));
  case 4: return theme_bubble(tr("Zoom"), tr("Drag to zoom in or out. The middle is 100%."));
  case 5: return theme_bubble(tr("Calculate"), tr("Calculation is manual: formulas haven't been worked out again since the last change. Click to calculate now (F9)."));
  case 6: return theme_bubble(tr("Zoom Out"), tr("Shows the cells 10% smaller."));
  case 7: return theme_bubble(tr("Zoom In"), tr("Shows the cells 10% bigger."));
  }
  return NULL;
}
/* the status bar class's dispatcher. MUIM_Status_Set redraws only when something shown changed,
   and is ignored while the slider is dragged so the knob doesn't jump */
static ULONG sdispatch(__reg("a0") struct IClass *cl, __reg("a2") Object *obj, __reg("a1") Msg msg)
{
  struct SData *d;
  switch (msg->MethodID) {
  case OM_NEW:
    if (!(obj = (Object *)DoSuperMethodA(cl, obj, msg))) return 0;
    d = INST_DATA(cl, obj);
    memset(d, 0, sizeof *d);
    d->zoom = 100; strcpy(d->mode, "Ready");
    d->ehn.ehn_Object = obj; d->ehn.ehn_Class = cl; d->ehn.ehn_Events = IDCMP_MOUSEBUTTONS | IDCMP_MOUSEMOVE;
    return (ULONG)obj;
  case OM_GET: {
    struct opGet *g = (struct opGet *)msg;
    if (g->opg_AttrID == MUIA_Status_Zoom) { *g->opg_Storage = (ULONG)((struct SData *)INST_DATA(cl, obj))->zoomset; return TRUE; }
    break;
  }
  case MUIM_AskMinMax: {
    struct MUIP_AskMinMax *m = (struct MUIP_AskMinMax *)msg;
    LONG h = fh() + 9;
    DoSuperMethodA(cl, obj, msg);
    m->MinMaxInfo->MinWidth += 200; m->MinMaxInfo->DefWidth += 640; m->MinMaxInfo->MaxWidth += MUI_MAXMAX;
    m->MinMaxInfo->MinHeight += h; m->MinMaxInfo->DefHeight += h; m->MinMaxInfo->MaxHeight += h;
    return 0;
  }
  case MUIM_Setup:
    if (!DoSuperMethodA(cl, obj, msg)) return FALSE;
    theme_setup(obj);
    theme_handler(obj, &((struct SData *)INST_DATA(cl, obj))->ehn);
    return TRUE;
  case MUIM_Cleanup:
    theme_cleanup(obj);
    break;
  case MUIM_Draw:
    DoSuperMethodA(cl, obj, msg);
    if (((struct MUIP_Draw *)msg)->flags & (MADF_DRAWOBJECT | MADF_DRAWUPDATE)) status_draw(INST_DATA(cl, obj), obj);
    return 0;
  case MUIM_Show: ((struct SData *)INST_DATA(cl, obj))->shown = 1; break;
  case MUIM_Hide: ((struct SData *)INST_DATA(cl, obj))->shown = 0; break;
  case MUIM_HandleEvent:
    return status_events(cl, obj, (struct MUIP_HandleEvent *)msg);
  case MUIM_CreateShortHelp: {
    struct MUIP_CreateShortHelp *m = (struct MUIP_CreateShortHelp *)msg;
    return (ULONG)status_help(INST_DATA(cl, obj), (WORD)m->mx, (WORD)m->my);
  }
  case MUIM_DeleteShortHelp: return 0;
  case MUIM_Status_Set: {
    struct MUIP_Status_Set *m = (struct MUIP_Status_Set *)msg;
    const char *mode = m->mode ? m->mode : "Ready", *st = m->stats ? m->stats : "";
    d = INST_DATA(cl, obj);
    if (d->drag) return 0;
    if (!strcmp(d->mode, mode) && !strcmp(d->stats, st) && d->zoom == m->zoom && d->manual == m->manual) return 0;
    strncpy(d->mode, mode, sizeof d->mode - 1); d->mode[sizeof d->mode - 1] = 0;
    strncpy(d->stats, st, sizeof d->stats - 1); d->stats[sizeof d->stats - 1] = 0;
    d->zoom = m->zoom; d->manual = m->manual;
    if (_win(obj) && d->shown) MUI_Redraw(obj, MADF_DRAWOBJECT);
    return 0;
  }
  }
  return DoSuperMethodA(cl, obj, msg);
}

/* ================================================================== */
/* the sheet tabs */
#define MAXTABS 64
struct TData {
  int shown;
  Book *book;
  int first;                     /* the first sheet shown (scrolled by the arrows) */
  NBox left, right, plus, tab[MAXTABS]; int tabsheet[MAXTABS], ntab;
  int hot, value, more;          /* more: some sheets are cut off at the right */
  ULONG secs, micros; int lastclick;
  Object *strip;                 /* the right-button menu */
  struct MUI_EventHandlerNode ehn;
};
/* a sheet's name in Latin-1 */
static void sheet_latin1(Sheet *s, char *out, size_t cap) { utf8_to_latin1(s->name, (u32)strlen(s->name), out, cap); }
/* the nearest pen to an 0xRRGGBB (ReleasePen it) */
static LONG rgb_pen(Object *obj, ULONG c)
{
  return ObtainBestPen(_screen(obj)->ViewPort.ColorMap, ((c >> 16) & 255) * 0x01010101UL, ((c >> 8) & 255) * 0x01010101UL, (c & 255) * 0x01010101UL,
                       OBP_Precision, PRECISION_IMAGE, TAG_DONE);
}
/* the strip: arrows, the tabs from d->first (each recorded for tabs_hit), the + */
static void tabs_draw(struct TData *d, Object *obj)
{
  struct RastPort *rp = _rp(obj);
  LONG l = _mleft(obj), t = _mtop(obj), r = _mright(obj), b = _mbottom(obj), x, ty = t + (b - t + 1 - fh()) / 2;
  int i;
  theme_fill(rp, l, t, r, b, tpen(T_TABSTRIP));
  SetAPen(rp, tpen(T_BORDER)); Move(rp, l, t); Draw(rp, r, t);
  d->left.x0 = (WORD)(l + 2); d->left.x1 = (WORD)(l + 17); d->right.x0 = (WORD)(l + 19); d->right.x1 = (WORD)(l + 34);
  d->left.y0 = d->right.y0 = (WORD)(t + 1); d->left.y1 = d->right.y1 = (WORD)b;
  if (d->hot == 1) theme_fill(rp, d->left.x0, d->left.y0, d->left.x1, d->left.y1, tpen(T_HOVER));
  if (d->hot == 2) theme_fill(rp, d->right.x0, d->right.y0, d->right.x1, d->right.y1, tpen(T_HOVER));
  theme_icon(rp, IC_ARROWLEFT, d->left.x0 + 3, ty, 10, d->first == 0);
  x = l + 40;
  d->ntab = 0; d->more = 0;
  d->plus.x0 = d->plus.x1 = -1;
  if (d->book) {
    for (i = d->first; i < d->book->nsh && d->ntab < MAXTABS; i++) {
      Sheet *s = d->book->sh[i];
      char name[40];
      LONG w;
      int act = i == d->book->active;
      if (s->hidden) continue;
      sheet_latin1(s, name, sizeof name);
      w = theme_textlen(rp, act ? theme_bold() : theme_font(), name) + 20;
      if (x + w > r - 24) { d->more = 1; break; }
      d->tab[d->ntab].x0 = (WORD)x; d->tab[d->ntab].x1 = (WORD)(x + w - 1); d->tab[d->ntab].y0 = (WORD)t; d->tab[d->ntab].y1 = (WORD)b;
      d->tabsheet[d->ntab] = i;
      if (act) theme_fill(rp, x, t, x + w - 1, b, tpen(T_WHITE));
      else if (d->hot == 10 + d->ntab) theme_fill(rp, x, t + 1, x + w - 1, b, tpen(T_HOVER));
      if (s->tabcolor) {
        LONG pen = rgb_pen(obj, s->tabcolor & 0xFFFFFF);
        theme_fill(rp, x + 2, b - 2, x + w - 3, b, pen);
        ReleasePen(_screen(obj)->ViewPort.ColorMap, pen);
      }
      SetFont(rp, act ? theme_bold() : theme_font());
      SetSoftStyle(rp, 0, FSF_BOLD | FSF_ITALIC | FSF_UNDERLINED);
      SetAPen(rp, tpen(act ? T_ACCENT : T_TEXT)); SetDrMd(rp, JAM1);
      Move(rp, x + 10, ty + (act ? theme_bold() : theme_font())->tf_Baseline);
      Text(rp, (STRPTR)name, (ULONG)strlen(name));
      if (act) theme_fill(rp, x + 6, b - 2, x + w - 7, b - 1, tpen(T_ACCENT));
      SetAPen(rp, tpen(T_BORDER)); Move(rp, x + w - 1, t + 3); Draw(rp, x + w - 1, b - 3);
      x += w;
      d->ntab++;
    }
    if (!d->more) {
      d->plus.x0 = (WORD)(x + 2); d->plus.x1 = (WORD)(x + 19); d->plus.y0 = (WORD)(t + 1); d->plus.y1 = (WORD)b;
      if (d->hot == 3) theme_fill(rp, d->plus.x0, d->plus.y0, d->plus.x1, d->plus.y1, tpen(T_HOVER));
      SetAPen(rp, tpen(T_ICON));
      Move(rp, d->plus.x0 + 4, (t + b) / 2); Draw(rp, d->plus.x1 - 4, (t + b) / 2);
      Move(rp, (d->plus.x0 + d->plus.x1) / 2, (t + b) / 2 - 5); Draw(rp, (d->plus.x0 + d->plus.x1) / 2, (t + b) / 2 + 5);
    }
  }
  theme_icon(rp, IC_ARROWRIGHT, d->right.x0 + 3, ty, 10, !d->more);
}
/* what is at x, y: 1 the left arrow, 2 the right, 3 the +, 10 + n tab n; 0 nothing */
static int tabs_hit(struct TData *d, WORD x, WORD y)
{
  int i;
  if (in_nbox(&d->left, x, y)) return 1;
  if (in_nbox(&d->right, x, y)) return 2;
  if (d->plus.x0 >= 0 && in_nbox(&d->plus, x, y)) return 3;
  for (i = 0; i < d->ntab; i++) if (in_nbox(&d->tab[i], x, y)) return 10 + i;
  return 0;
}
/* the tabs' MUIM_HandleEvent: hover, clicks on a tab (double: rename), the arrows and the + */
static ULONG tabs_events(struct IClass *cl, Object *obj, struct MUIP_HandleEvent *m)
{
  struct TData *d = INST_DATA(cl, obj);
  struct IntuiMessage *im = m->imsg;
  int inside, h;
  if (!im || !d->shown) return 0;
  inside = im->MouseX >= _mleft(obj) && im->MouseX <= _mright(obj) && im->MouseY >= _mtop(obj) && im->MouseY <= _mbottom(obj);
  h = inside ? tabs_hit(d, im->MouseX, im->MouseY) : 0;
  if (im->Class == IDCMP_MOUSEMOVE) {
    if (h != d->hot) { d->hot = h; MUI_Redraw(obj, MADF_DRAWOBJECT); }
    return 0;
  }
  if (im->Class != IDCMP_MOUSEBUTTONS || im->Code != SELECTDOWN || !inside) return 0;
  if (h == 1) { if (d->first > 0) { d->first--; MUI_Redraw(obj, MADF_DRAWOBJECT); } }
  else if (h == 2) { if (d->more) { d->first++; MUI_Redraw(obj, MADF_DRAWOBJECT); } }
  else if (h == 3) DoMethod(_app(obj), MUIM_Application_ReturnID, ID_NEWSHEET);
  else if (h >= 10) {
    int s = d->tabsheet[h - 10];
    int dbl = d->lastclick == s && DoubleClick(d->secs, d->micros, im->Seconds, im->Micros);
    d->secs = im->Seconds; d->micros = im->Micros; d->lastclick = s;
    d->value = s;
    DoMethod(_app(obj), MUIM_Application_ReturnID, dbl ? ID_RENAMESHEET : ID_SHEETTAB);
  }
  else return 0;
  return MUI_EventHandlerRC_Eat;
}
/* the right-button menu's entries and the commands they send */
static const struct { const char *label; ULONG id; } tabmenu[] = {
  { "Insert Sheet", ID_NEWSHEET }, { "Delete", ID_DELSHEET }, { "Rename", ID_RENAMESHEET }, { "Duplicate", ID_DUPSHEET },
  { "Move Left", ID_MOVESHEETL }, { "Move Right", ID_MOVESHEETR }, { "Hide", ID_HIDESHEET }, { "Unhide...", ID_UNHIDESHEET } };
/* the sheet tabs' dispatcher */
static ULONG tdispatch(__reg("a0") struct IClass *cl, __reg("a2") Object *obj, __reg("a1") Msg msg)
{
  struct TData *d;
  switch (msg->MethodID) {
  case OM_NEW: {
    Object *menu;
    int i;
    if (!(obj = (Object *)DoSuperMethodA(cl, obj, msg))) return 0;
    d = INST_DATA(cl, obj);
    memset(d, 0, sizeof *d);
    d->lastclick = -1;
    d->book = (Book *)GetTagData(MUIA_Tabs_Book, 0, ((struct opSet *)msg)->ops_AttrList);
    d->ehn.ehn_Object = obj; d->ehn.ehn_Class = cl; d->ehn.ehn_Events = IDCMP_MOUSEBUTTONS | IDCMP_MOUSEMOVE;
    d->strip = MenustripObject, MUIA_Family_Child, menu = MenuObject, MUIA_Menu_Title, (ULONG)tr("Sheet"), End, End;
    if (d->strip && menu) {
      for (i = 0; i < (int)(sizeof tabmenu / sizeof tabmenu[0]); i++) {
        Object *it = MenuitemObject, MUIA_Menuitem_Title, (ULONG)tr(tabmenu[i].label), MUIA_UserData, tabmenu[i].id, End;
        if (it) DoMethod(menu, MUIM_Family_AddTail, (ULONG)it);
      }
      set(obj, MUIA_ContextMenu, (ULONG)d->strip);
    }
    return (ULONG)obj;
  }
  case OM_DISPOSE:
    d = INST_DATA(cl, obj);
    if (d->strip) { set(obj, MUIA_ContextMenu, 0); MUI_DisposeObject(d->strip); d->strip = NULL; }
    break;
  case OM_SET: {
    struct TagItem *tags = ((struct opSet *)msg)->ops_AttrList, *t;
    d = INST_DATA(cl, obj);
    while ((t = NextTagItem(&tags)))
      if (t->ti_Tag == MUIA_Tabs_Book) { d->book = (Book *)t->ti_Data; d->first = 0; if (_win(obj) && d->shown) MUI_Redraw(obj, MADF_DRAWOBJECT); }
    break;
  }
  case OM_GET: {
    struct opGet *g = (struct opGet *)msg;
    if (g->opg_AttrID == MUIA_Tabs_Value) { *g->opg_Storage = (ULONG)((struct TData *)INST_DATA(cl, obj))->value; return TRUE; }
    break;
  }
  case MUIM_AskMinMax: {
    struct MUIP_AskMinMax *m = (struct MUIP_AskMinMax *)msg;
    LONG h = fh() + 9;
    DoSuperMethodA(cl, obj, msg);
    m->MinMaxInfo->MinWidth += 120; m->MinMaxInfo->DefWidth += 400; m->MinMaxInfo->MaxWidth += MUI_MAXMAX;
    m->MinMaxInfo->MinHeight += h; m->MinMaxInfo->DefHeight += h; m->MinMaxInfo->MaxHeight += h;
    return 0;
  }
  case MUIM_Setup:
    if (!DoSuperMethodA(cl, obj, msg)) return FALSE;
    theme_setup(obj);
    theme_handler(obj, &((struct TData *)INST_DATA(cl, obj))->ehn);
    return TRUE;
  case MUIM_Cleanup:
    theme_cleanup(obj);
    break;
  case MUIM_Draw:
    DoSuperMethodA(cl, obj, msg);
    if (((struct MUIP_Draw *)msg)->flags & (MADF_DRAWOBJECT | MADF_DRAWUPDATE)) tabs_draw(INST_DATA(cl, obj), obj);
    return 0;
  case MUIM_Show: ((struct TData *)INST_DATA(cl, obj))->shown = 1; break;
  case MUIM_Hide: ((struct TData *)INST_DATA(cl, obj))->shown = 0; break;
  case MUIM_HandleEvent:
    return tabs_events(cl, obj, (struct MUIP_HandleEvent *)msg);
  case MUIM_ContextMenuBuild: {
    /* the right button on a tab: that sheet first becomes the active one */
    struct MUIP_ContextMenuBuild *m = (struct MUIP_ContextMenuBuild *)msg;
    int h;
    d = INST_DATA(cl, obj);
    h = tabs_hit(d, (WORD)m->mx, (WORD)m->my);
    if (h >= 10 && d->tabsheet[h - 10] != (d->book ? d->book->active : -1)) {
      d->value = d->tabsheet[h - 10];
      DoMethod(_app(obj), MUIM_Application_ReturnID, ID_SHEETTAB);
    }
    return (ULONG)d->strip;
  }
  case MUIM_ContextMenuChoice: {
    Object *item = ((struct MUIP_ContextMenuChoice *)msg)->item;
    ULONG id = 0;
    if (item) get(item, MUIA_UserData, &id);
    if (id) DoMethod(_app(obj), MUIM_Application_ReturnID, id);
    return 0;
  }
  case MUIM_CreateShortHelp: {
    struct MUIP_CreateShortHelp *m = (struct MUIP_CreateShortHelp *)msg;
    int h;
    d = INST_DATA(cl, obj);
    h = tabs_hit(d, (WORD)m->mx, (WORD)m->my);
    if (h == 1 || h == 2) return (ULONG)theme_bubble(tr("Scroll Sheets"), tr("Shows the sheet tabs further left or right, when there are more than fit."));
    if (h == 3) return (ULONG)theme_bubble(tr("New Sheet"), tr("Adds a sheet after the last one (Shift-F11)."));
    if (h >= 10) return (ULONG)theme_bubble(tr("Sheet"), tr("Click to show this sheet; double-click to rename it. The right mouse button can insert, delete, move, hide or colour sheets."));
    return 0;
  }
  case MUIM_DeleteShortHelp: return 0;
  case MUIM_Tabs_Refresh:
    d = INST_DATA(cl, obj);
    if (d->book && d->first > d->book->active) d->first = d->book->active;
    if (_win(obj) && d->shown) {
      MUI_Redraw(obj, MADF_DRAWOBJECT);
      /* the active one cut off at the right: scrolled until it shows */
      while (d->book && d->more && d->ntab && d->tabsheet[d->ntab - 1] < d->book->active && d->first < d->book->active) { d->first++; MUI_Redraw(obj, MADF_DRAWOBJECT); }
    }
    return 0;
  }
  return DoSuperMethodA(cl, obj, msg);
}

/* ================================================================== */
/* a flat scroll bar (beside the page): takes MUIA_Prop_Entries, _Visible and _First as a Prop does,
   and notifies MUIA_Prop_First as it is dragged */
struct FData { LONG entries, visible, first; int drag, hot, horiz, shown; LONG grab; struct MUI_EventHandlerNode ehn; };
/* along the bar: its start and length, and the pointer's place */
static LONG f0(struct FData *d, Object *obj) { return (d->horiz ? _mleft(obj) : _mtop(obj)) + 2; }
static LONG flen(struct FData *d, Object *obj) { return (d->horiz ? _mwidth(obj) : _mheight(obj)) - 4; }
static struct MUI_CustomClass *fcls;
/* the thumb's start and end along the bar (window pixels), at least 13 long and kept inside */
static void thumb(struct FData *d, Object *obj, LONG *a, LONG *b)
{
  LONG t = f0(d, obj), len = flen(d, obj), e = d->entries > d->visible ? d->entries : d->visible;
  if (e <= 0) e = 1;
  *a = t + len * d->first / e;
  *b = t + len * (d->first + d->visible) / e - 1;
  if (*b < *a + 12) *b = *a + 12;
  if (*b > t + len - 1) { *a -= *b - (t + len - 1); *b = t + len - 1; }
  if (*a < t) *a = t;
}
/* the bar: the canvas colour with a rounded thumb (two overlapping fills), none if everything is
   visible; darker while dragged, lighter under the pointer */
static void flat_draw(struct FData *d, Object *obj)
{
  struct RastPort *rp = _rp(obj);
  LONG a, b, l = _mleft(obj), r = _mright(obj);
  theme_fill(rp, l, _mtop(obj), r, _mbottom(obj), tpen(T_CANVAS));
  if (d->entries <= d->visible) return;
  thumb(d, obj, &a, &b);
  if (d->horiz) {
    LONG t = (_mtop(obj) + _mbottom(obj)) / 2 - 6, u = t + 11;
    theme_fill(rp, a + 1, t + 3, b - 1, u - 3, tpen(d->drag ? T_DIM : d->hot ? T_GROUPLABEL : T_BORDER2));
    theme_fill(rp, a, t + 4, b, u - 4, tpen(d->drag ? T_DIM : d->hot ? T_GROUPLABEL : T_BORDER2));
    return;
  }
  theme_fill(rp, l + 3, a + 1, r - 3, b - 1, tpen(d->drag ? T_DIM : d->hot ? T_GROUPLABEL : T_BORDER2));
  theme_fill(rp, l + 4, a, r - 4, b, tpen(d->drag ? T_DIM : d->hot ? T_GROUPLABEL : T_BORDER2));
}
/* MUIA_Prop_First set to f, kept between 0 and entries - visible */
static void flat_set_first(struct FData *d, Object *obj, LONG f)
{
  LONG max = d->entries - d->visible;
  if (f > max) f = max;
  if (f < 0) f = 0;
  if (f == d->first) return;
  SetAttrs(obj, MUIA_Prop_First, f, TAG_DONE);          /* through OM_SET, so notifications go */
}
/* the flat scroll bar's dispatcher: it stands in for MUI's Prop class, keeping the same three
   attributes so main.c can notify on MUIA_Prop_First as it would on a Prop. A click either side of
   the thumb moves nine tenths of a view; d->grab is where on the thumb it was caught */
static ULONG fdispatch(__reg("a0") struct IClass *cl, __reg("a2") Object *obj, __reg("a1") Msg msg)
{
  struct FData *d;
  switch (msg->MethodID) {
  case OM_NEW:
    if (!(obj = (Object *)DoSuperMethodA(cl, obj, msg))) return 0;
    d = INST_DATA(cl, obj);
    memset(d, 0, sizeof *d);
    d->entries = d->visible = 1;
    d->horiz = (int)GetTagData(MUIA_FlatScroll_Horiz, FALSE, ((struct opSet *)msg)->ops_AttrList);
    d->ehn.ehn_Object = obj; d->ehn.ehn_Class = cl; d->ehn.ehn_Events = IDCMP_MOUSEBUTTONS | IDCMP_MOUSEMOVE;
    return (ULONG)obj;
  case OM_SET: {
    struct TagItem *tags = ((struct opSet *)msg)->ops_AttrList, *t;
    int redraw = 0;
    d = INST_DATA(cl, obj);
    while ((t = NextTagItem(&tags))) {
      if (t->ti_Tag == MUIA_Prop_Entries && d->entries != (LONG)t->ti_Data) { d->entries = (LONG)t->ti_Data; redraw = 1; }
      else if (t->ti_Tag == MUIA_Prop_Visible && d->visible != (LONG)t->ti_Data) { d->visible = (LONG)t->ti_Data; redraw = 1; }
      else if (t->ti_Tag == MUIA_Prop_First && d->first != (LONG)t->ti_Data) { d->first = (LONG)t->ti_Data; redraw = 1; }
    }
    if (redraw && _win(obj)) MUI_Redraw(obj, MADF_DRAWOBJECT);
    break;
  }
  case OM_GET: {
    struct opGet *g = (struct opGet *)msg;
    d = INST_DATA(cl, obj);
    if (g->opg_AttrID == MUIA_Prop_Entries) { *g->opg_Storage = (ULONG)d->entries; return TRUE; }
    if (g->opg_AttrID == MUIA_Prop_Visible) { *g->opg_Storage = (ULONG)d->visible; return TRUE; }
    if (g->opg_AttrID == MUIA_Prop_First) { *g->opg_Storage = (ULONG)d->first; return TRUE; }
    break;
  }
  case MUIM_AskMinMax: {
    struct MUIP_AskMinMax *m = (struct MUIP_AskMinMax *)msg;
    DoSuperMethodA(cl, obj, msg);
    if (((struct FData *)INST_DATA(cl, obj))->horiz) {
      m->MinMaxInfo->MinHeight += 12; m->MinMaxInfo->DefHeight += 12; m->MinMaxInfo->MaxHeight += MUI_MAXMAX;
      m->MinMaxInfo->MinWidth += 30; m->MinMaxInfo->DefWidth += 200; m->MinMaxInfo->MaxWidth += MUI_MAXMAX;
      return 0;
    }
    m->MinMaxInfo->MinWidth += 12; m->MinMaxInfo->DefWidth += 12; m->MinMaxInfo->MaxWidth += 12;
    m->MinMaxInfo->MinHeight += 30; m->MinMaxInfo->DefHeight += 200; m->MinMaxInfo->MaxHeight += MUI_MAXMAX;
    return 0;
  }
  case MUIM_Setup:
    if (!DoSuperMethodA(cl, obj, msg)) return FALSE;
    theme_setup(obj);
    theme_handler(obj, &((struct FData *)INST_DATA(cl, obj))->ehn);
    return TRUE;
  case MUIM_Cleanup:
    theme_cleanup(obj);
    break;
  case MUIM_Draw:
    DoSuperMethodA(cl, obj, msg);
    if (((struct MUIP_Draw *)msg)->flags & (MADF_DRAWOBJECT | MADF_DRAWUPDATE)) flat_draw(INST_DATA(cl, obj), obj);
    return 0;
  case MUIM_Show: ((struct FData *)INST_DATA(cl, obj))->shown = 1; break;
  case MUIM_Hide: ((struct FData *)INST_DATA(cl, obj))->shown = 0; break;
  case MUIM_HandleEvent: {
    struct IntuiMessage *im = ((struct MUIP_HandleEvent *)msg)->imsg;
    LONG a, b, mp;
    int inside;
    d = INST_DATA(cl, obj);
    if (!im || !d->shown || d->entries <= d->visible) return 0;
    inside = im->MouseX >= _mleft(obj) && im->MouseX <= _mright(obj) && im->MouseY >= _mtop(obj) && im->MouseY <= _mbottom(obj);
    thumb(d, obj, &a, &b);
    mp = d->horiz ? im->MouseX : im->MouseY;
    if (im->Class == IDCMP_MOUSEMOVE) {
      if (d->drag) {
        LONG len = flen(d, obj);
        flat_set_first(d, obj, (mp - d->grab - f0(d, obj)) * d->entries / (len > 0 ? len : 1));
        return MUI_EventHandlerRC_Eat;
      }
      if ((inside && mp >= a && mp <= b) != d->hot) { d->hot = !d->hot; MUI_Redraw(obj, MADF_DRAWOBJECT); }
      return 0;
    }
    if (im->Class != IDCMP_MOUSEBUTTONS) return 0;
    if (im->Code == SELECTUP && d->drag) { d->drag = 0; MUI_Redraw(obj, MADF_DRAWOBJECT); return MUI_EventHandlerRC_Eat; }
    if (im->Code != SELECTDOWN || !inside) return 0;
    if (mp < a) flat_set_first(d, obj, d->first - d->visible * 9 / 10);
    else if (mp > b) flat_set_first(d, obj, d->first + d->visible * 9 / 10);
    else { d->drag = 1; d->grab = mp - a; MUI_Redraw(obj, MADF_DRAWOBJECT); }
    return MUI_EventHandlerRC_Eat;
  }
  }
  return DoSuperMethodA(cl, obj, msg);
}
/* the classes, each a subclass of Area made on first call (NULL if MUI couldn't make it): the
   flat scroll bar, */
struct MUI_CustomClass *flatscroll_class(void)
{
  if (!fcls) fcls = MUI_CreateCustomClass(NULL, (STRPTR)MUIC_Area, NULL, sizeof(struct FData), (APTR)fdispatch);
  return fcls;
}
/* the status bar */
struct MUI_CustomClass *status_class(void)
{
  if (!scls) scls = MUI_CreateCustomClass(NULL, (STRPTR)MUIC_Area, NULL, sizeof(struct SData), (APTR)sdispatch);
  return scls;
}
/* and the sheet tabs */
struct MUI_CustomClass *tabs_class(void)
{
  if (!tcls) tcls = MUI_CreateCustomClass(NULL, (STRPTR)MUIC_Area, NULL, sizeof(struct TData), (APTR)tdispatch);
  return tcls;
}
/* at the end, once the windows and every object of these classes has been disposed of */
void panes_free(void)
{
  if (scls) { MUI_DeleteCustomClass(scls); scls = NULL; }
  if (tcls) { MUI_DeleteCustomClass(tcls); tcls = NULL; }
  if (fcls) { MUI_DeleteCustomClass(fcls); fcls = NULL; }
}
