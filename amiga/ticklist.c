/*
 * ticklist.c - a list of labels each with a tick box, drawn in Ledger's theme (the AutoFilter's
 * list of values). A click on a line ticks or unticks it; the mouse wheel and a flat scroll bar
 * beside it (MUIA_Tick_Prop) scroll it. The ticks are the caller's array, changed in place.
 */
#include <string.h>
#include <devices/inputevent.h>
#include <proto/exec.h>
#include <proto/graphics.h>
#include <proto/intuition.h>
#include <proto/muimaster.h>
#include <proto/utility.h>
#include <libraries/mui.h>
#include <clib/alib_protos.h>
#include "ledger.h"
#include "theme.h"

struct TKData {
  const char *const *labels; UBYTE *on; LONG n;
  LONG first, visible;
  Object *prop;
  int shown;
  struct MUI_EventHandlerNode ehn;
};
static struct MUI_CustomClass *tkcls;

static LONG line_h(void) { return theme_font()->tf_YSize + 6; }
/* the scroll bar told how much there is and what shows */
static void tk_prop(struct TKData *d)
{
  if (!d->prop) return;
  SetAttrs(d->prop, MUIA_NoNotify, TRUE, MUIA_Prop_Entries, d->n, MUIA_Prop_Visible, d->visible, MUIA_Prop_First, d->first, TAG_DONE);
}
static void tk_first(struct TKData *d, Object *obj, LONG f)
{
  if (f > d->n - d->visible) f = d->n - d->visible;
  if (f < 0) f = 0;
  if (f == d->first) return;
  d->first = f;
  tk_prop(d);
  MUI_Redraw(obj, MADF_DRAWOBJECT);
}
static void tk_draw(struct TKData *d, Object *obj)
{
  struct RastPort *rp = _rp(obj);
  LONG lh = line_h(), x0 = _mleft(obj), y0 = _mtop(obj), x1 = _mright(obj), y1 = _mbottom(obj), i, y, bs;
  d->visible = (y1 - y0 + 1) / lh;
  if (d->visible < 1) d->visible = 1;
  if (d->first > d->n - d->visible) d->first = d->n - d->visible > 0 ? d->n - d->visible : 0;
  tk_prop(d);
  theme_fill(rp, x0, y0, x1, y1, tpen(T_WHITE));
  theme_rect(rp, x0, y0, x1, y1, tpen(T_INPUTBORDER));
  bs = theme_font()->tf_YSize;
  for (i = d->first, y = y0 + 2; i < d->n && y + lh - 1 <= y1; i++, y += lh) {
    LONG bx = x0 + 6, by = y + (lh - bs) / 2;
    if (d->on[i]) {
      LONG k;
      theme_fill(rp, bx, by, bx + bs - 1, by + bs - 1, tpen(T_ACCENT));
      SetAPen(rp, tpen(T_ONACCENT));
      /* the tick: a short stroke down, a long one up, two pixels thick */
      for (k = 0; k < 2; k++) {
        Move(rp, bx + 2, by + bs / 2 + k - 1); Draw(rp, bx + bs * 2 / 5, by + bs - 3 + k - 1);
        Draw(rp, bx + bs - 3, by + 2 + k);
      }
    } else {
      theme_fill(rp, bx, by, bx + bs - 1, by + bs - 1, tpen(T_WHITE));
      theme_rect(rp, bx, by, bx + bs - 1, by + bs - 1, tpen(T_DIM));
    }
    theme_text(rp, theme_font(), bx + bs + 6, y + 3, d->labels[i], tpen(T_TEXT));
  }
}
static ULONG tkdispatch(__reg("a0") struct IClass *cl, __reg("a2") Object *obj, __reg("a1") Msg msg)
{
  struct TKData *d;
  switch (msg->MethodID) {
  case OM_NEW: {
    struct TagItem *tags = ((struct opSet *)msg)->ops_AttrList;
    if (!(obj = (Object *)DoSuperMethodA(cl, obj, msg))) return 0;
    d = INST_DATA(cl, obj);
    memset(d, 0, sizeof *d);
    d->labels = (const char *const *)GetTagData(MUIA_Tick_Labels, 0, tags);
    d->on = (UBYTE *)GetTagData(MUIA_Tick_States, 0, tags);
    d->n = (LONG)GetTagData(MUIA_Tick_Count, 0, tags);
    d->prop = (Object *)GetTagData(MUIA_Tick_Prop, 0, tags);
    if (!d->labels || !d->on) d->n = 0;
    d->ehn.ehn_Object = obj; d->ehn.ehn_Class = cl; d->ehn.ehn_Events = IDCMP_MOUSEBUTTONS | IDCMP_RAWKEY;
    return (ULONG)obj;
  }
  case OM_SET: {
    struct TagItem *tags = ((struct opSet *)msg)->ops_AttrList, *t;
    d = INST_DATA(cl, obj);
    while ((t = NextTagItem(&tags))) {
      if (t->ti_Tag == MUIA_Tick_First && d->shown) tk_first(d, obj, (LONG)t->ti_Data);
      else if (t->ti_Tag == MUIA_Tick_All) {
        LONG i;
        for (i = 0; i < d->n; i++) d->on[i] = t->ti_Data != 0;
        if (d->shown) MUI_Redraw(obj, MADF_DRAWOBJECT);
      }
    }
    break;
  }
  case MUIM_AskMinMax: {
    struct MUIP_AskMinMax *m = (struct MUIP_AskMinMax *)msg;
    LONG lh = line_h(), w = 120, i;
    struct RastPort rp;
    DoSuperMethodA(cl, obj, msg);
    d = INST_DATA(cl, obj);
    InitRastPort(&rp);
    for (i = 0; i < d->n && i < 300; i++) {
      LONG tw = theme_textlen(&rp, theme_font(), d->labels[i]) + theme_font()->tf_YSize + 20;
      if (tw > w) w = tw;
    }
    if (w > 400) w = 400;
    m->MinMaxInfo->MinWidth += w; m->MinMaxInfo->DefWidth += w; m->MinMaxInfo->MaxWidth += MUI_MAXMAX;
    m->MinMaxInfo->MinHeight += lh * 4 + 4; m->MinMaxInfo->DefHeight += lh * 9 + 4; m->MinMaxInfo->MaxHeight += MUI_MAXMAX;
    return 0;
  }
  case MUIM_Setup:
    if (!DoSuperMethodA(cl, obj, msg)) return FALSE;
    theme_setup(obj);
    theme_handler(obj, &((struct TKData *)INST_DATA(cl, obj))->ehn);
    return TRUE;
  case MUIM_Cleanup:
    theme_cleanup(obj);
    break;
  case MUIM_Show: ((struct TKData *)INST_DATA(cl, obj))->shown = 1; break;
  case MUIM_Hide: ((struct TKData *)INST_DATA(cl, obj))->shown = 0; break;
  case MUIM_Draw:
    DoSuperMethodA(cl, obj, msg);
    if (((struct MUIP_Draw *)msg)->flags & (MADF_DRAWOBJECT | MADF_DRAWUPDATE)) tk_draw(INST_DATA(cl, obj), obj);
    return 0;
  case MUIM_HandleEvent: {
    struct IntuiMessage *im = ((struct MUIP_HandleEvent *)msg)->imsg;
    int inside;
    d = INST_DATA(cl, obj);
    if (!im || !d->shown) return 0;
    inside = im->MouseX >= _mleft(obj) && im->MouseX <= _mright(obj) && im->MouseY >= _mtop(obj) && im->MouseY <= _mbottom(obj);
    if (!inside) return 0;
    /* the wheel (NM_WHEEL_UP / DOWN) */
    if (im->Class == IDCMP_RAWKEY && (im->Code == 0x7A || im->Code == 0x7B)) { tk_first(d, obj, d->first + (im->Code == 0x7A ? -3 : 3)); return MUI_EventHandlerRC_Eat; }
    if (im->Class == IDCMP_MOUSEBUTTONS && im->Code == SELECTDOWN) {
      LONG i = d->first + (im->MouseY - _mtop(obj) - 2) / line_h();
      if (i >= d->first && i < d->n && i < d->first + d->visible) { d->on[i] = !d->on[i]; MUI_Redraw(obj, MADF_DRAWOBJECT); }
      return MUI_EventHandlerRC_Eat;
    }
    return 0;
  }
  }
  return DoSuperMethodA(cl, obj, msg);
}
struct MUI_CustomClass *ticklist_class(void)
{
  if (!tkcls) tkcls = MUI_CreateCustomClass(NULL, (STRPTR)MUIC_Area, NULL, sizeof(struct TKData), (APTR)tkdispatch);
  return tkcls;
}
void ticklist_class_free(void)
{
  if (tkcls) { MUI_DeleteCustomClass(tkcls); tkcls = NULL; }
}
