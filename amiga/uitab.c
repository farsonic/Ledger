/*
 * uitab.c - see uitab.h: Group / Regroup / Ungroup for the selected drawn objects (the groups
 * themselves are core/drwgroup.c's), View > Split (the panes are drawn and dragged by grid.c, kept
 * by core/splitpane.c), and the greyed formula of a spilled cell for the formula bar.
 */
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <proto/exec.h>
#include <proto/intuition.h>
#include <proto/muimaster.h>
#include <clib/alib_protos.h>
#include <proto/graphics.h>
#include "app.h"
#include "theme.h"
#include "lang.h"
#include "ids.h"
#include "chartview.h"
#include "uitab.h"
#include "../core/drawing.h"
#include "../core/splitpane.h"

static ULONG xget(Object *o, ULONG attr) { ULONG v = 0; get(o, attr, &v); return v; }
static ObjView *OV(void) { return (ObjView *)xget(grid, MUIA_Grid_ObjView); }

/* ------------------------------------------------------------------ groups */
/* the objects of the last group taken apart (their ids), for Regroup */
static struct { Book *b; int sheet; u32 ids[64]; int n; char name[48]; } last;

static void select_group(Sheet *s, u32 g)
{
  int mem[64], n = drw_group_members(s, g, mem, 64);
  ov_select_list(OV(), mem, n);
}
static void group_objects(void)
{
  Sheet *s = cur_sheet();
  int sel[64], n = ov_selection(OV(), sel, 64);
  u32 g;
  if (n < 2) { ask("OK", "%s", tr("Select two or more pictures, shapes or charts first (hold Shift and click them, or drag round them with Select Objects).")); return; }
  drw_begin(book, cur_index());
  g = drw_group(s, sel, n);
  drw_end(book);
  if (!g) { ask("OK", "%s", tr("These are one group already: select another object or group as well.")); return; }
  select_group(s, g);
  refresh_all();
}
static void ungroup_objects(void)
{
  Sheet *s = cur_sheet();
  int sel[64], n = ov_selection(OV(), sel, 64), k, mem[64], nm;
  u32 g = n ? drw_top_group(s, sel[0]) : 0;
  DGroup *dg = drw_group_find(s, g);
  if (!dg) { ask("OK", "%s", tr("Select a group first: Ungroup takes it apart into its objects (or the groups inside it).")); return; }
  nm = drw_group_members(s, g, mem, 64);
  last.b = book; last.sheet = cur_index(); last.n = 0;
  snprintf(last.name, sizeof last.name, "%s", dg->name);
  for (k = 0; k < nm; k++) last.ids[last.n++] = drw_obj(s, mem[k])->id;
  drw_begin(book, cur_index());
  drw_ungroup(s, g);
  drw_end(book);
  ov_select_list(OV(), mem, nm);
  refresh_all();
}
static void regroup_objects(void)
{
  Sheet *s = cur_sheet();
  int sel[64], n = 0, i, k;
  u32 g;
  if (last.b != book || last.sheet != cur_index() || last.n < 2) { ask("OK", "%s", tr("There is no group taken apart on this sheet to put back together.")); return; }
  for (i = 0; i < drw_count(s) && n < 64; i++) for (k = 0; k < last.n; k++) if (drw_obj(s, i)->id == last.ids[k]) { sel[n++] = i; break; }
  if (n < 2) { last.n = 0; return; }
  drw_begin(book, cur_index());
  g = drw_group(s, sel, n);
  if (g && drw_group_find(s, g)) snprintf(drw_group_find(s, g)->name, sizeof last.name, "%s", last.name);
  drw_end(book);
  if (g) select_group(s, g);
  last.n = 0;
  refresh_all();
}

/* ------------------------------------------------------------------ View > Split */
void ui_split_toggle(void)
{
  Sheet *s = cur_sheet();
  i32 x, y, top = s->top, left = s->left, ntop, nleft, off;
  if (sheet_is_split(s)) { sheet_unsplit(s); refresh_all(); return; }
  if (s->freezer || s->freezec) book_freeze(book, cur_index(), 0, 0);
  top = s->top; left = s->left;
  /* at the active cell (above and left of it); from the top left cell shown, in the middle */
  y = s->cur_r > top ? sheet_row_px(s, s->cur_r) - sheet_row_px(s, top) : 0;
  x = s->cur_c > left ? sheet_col_px(s, s->cur_c) - sheet_col_px(s, left) : 0;
  ntop = y ? s->cur_r : top; nleft = x ? s->cur_c : left;
  if (!x && !y) {
    LONG z = s->zoom >= 10 ? s->zoom : 100, w = (LONG)_mwidth(grid) - 40, h = (LONG)_mheight(grid) - 24;
    i32 py = (i32)(h / 2 * 100 / z), px = (i32)(w / 2 * 100 / z);
    /* the middle, moved down (right) to the next row's (column's) edge */
    ntop = sheet_row_at(s, sheet_row_px(s, top) + py, &off); if (off) ntop++;
    nleft = sheet_col_at(s, sheet_col_px(s, left) + px, &off); if (off) nleft++;
    y = sheet_row_px(s, ntop) - sheet_row_px(s, top);
    x = sheet_col_px(s, nleft) - sheet_col_px(s, left);
    if (y < 8) y = 0;
    if (x < 8) x = 0;
  }
  sheet_split(s, x, y, top, left, ntop, nleft);
  book->dirty = 1;
  refresh_all();
}

/* ------------------------------------------------------------------ the formula bar */
int ui_spill_formula(Book *b, int sheet, i32 r, i32 c, char *out, size_t cap)
{
  Rect a;
  int blocked = 0;
  Cell *cell = sheet_cell(b->sh[sheet], r, c);
  char u[2048];
  if (!cell || !cell->spill || cell->f) return 0;
  if (!book_spill_range(b, sheet, r, c, &a, &blocked) || blocked || (a.r1 == r && a.c1 == c)) return 0;
  book_edit_text(b, sheet, a.r1, a.c1, u, sizeof u);
  utf8_to_latin1(u, (u32)strlen(u), out, cap);
  return 1;
}

/* the greyed text: an Area in a string's frame drawing ghost_text in the disabled colour */
static char ghost_text[1024];
static struct MUI_CustomClass *gcls;
static ULONG gdispatch(__reg("a0") struct IClass *cl, __reg("a2") Object *obj, __reg("a1") Msg msg)
{
  switch (msg->MethodID) {
  case MUIM_AskMinMax: {
    struct MUI_MinMax *mm;
    DoSuperMethodA(cl, obj, msg);
    mm = ((struct MUIP_AskMinMax *)msg)->MinMaxInfo;
    mm->MinWidth += 20; mm->DefWidth += 200; mm->MaxWidth += MUI_MAXMAX;
    mm->MinHeight += theme_font()->tf_YSize + 2; mm->DefHeight += theme_font()->tf_YSize + 2; mm->MaxHeight += theme_font()->tf_YSize + 2;
    return 0; }
  case MUIM_Draw: {
    struct RastPort *rp = _rp(obj);
    struct TextExtent te;
    ULONG fit;
    DoSuperMethodA(cl, obj, msg);
    if (!(((struct MUIP_Draw *)msg)->flags & MADF_DRAWOBJECT)) return 0;
    SetAPen(rp, tpen(T_WHITE));
    RectFill(rp, _mleft(obj), _mtop(obj), _mright(obj), _mbottom(obj));
    SetFont(rp, theme_font());
    fit = TextFit(rp, (STRPTR)ghost_text, (ULONG)strlen(ghost_text), &te, NULL, 1, (ULONG)(_mwidth(obj) > 4 ? _mwidth(obj) - 4 : 0), (ULONG)_mheight(obj) + 4);
    SetAPen(rp, tpen(T_DISABLED)); SetDrMd(rp, JAM1);
    Move(rp, _mleft(obj) + 2, _mtop(obj) + (_mheight(obj) - theme_font()->tf_YSize) / 2 + theme_font()->tf_Baseline);
    Text(rp, (STRPTR)ghost_text, fit);
    return 0; }
  }
  return DoSuperMethodA(cl, obj, msg);
}
APTR ui_fxghost(void)
{
  if (!gcls && !(gcls = MUI_CreateCustomClass(NULL, MUIC_Area, NULL, 4, (APTR)gdispatch))) return NULL;
  return NewObject(gcls->mcc_Class, NULL, MUIA_Frame, MUIV_Frame_String, MUIA_ShortHelp, (ULONG)tr("A cell a dynamic array spilled into: its anchor's formula, greyed. Type in the cell to replace the value (the formula then shows #SPILL!)."), TAG_DONE);
}
void ui_fx_ghost(APTR fxbar, int on, const char *text)
{
  Object *pg = (Object *)xget((Object *)fxbar, MUIA_Parent), *gh;
  if (!pg) return;
  if (on && strcmp(ghost_text, text)) { strncpy(ghost_text, text, sizeof ghost_text - 1); ghost_text[sizeof ghost_text - 1] = 0; on = 2; }
  if ((LONG)xget(pg, MUIA_Group_ActivePage) != (on ? 1 : 0)) set(pg, MUIA_Group_ActivePage, on ? 1 : 0);
  else if (on == 2) {
    struct List *l = (struct List *)xget(pg, MUIA_Group_ChildList);
    APTR state = l ? (APTR)l->lh_Head : NULL;
    if (state && NextObject(&state) && (gh = (Object *)NextObject(&state))) MUI_Redraw(gh, MADF_DRAWOBJECT);  /* (the string, then the ghost) */
  }
}
void ui_class_free(void) { if (gcls) { MUI_DeleteCustomClass(gcls); gcls = NULL; } }

/* ------------------------------------------------------------------ the workbook's file */
void ui_book_path(Book *b, const char *path)
{
  size_t o = 0;
  if (!b) return;
  for (; path && *path && o + 4 < sizeof b->path; path++) o += (size_t)utf8_put(b->path + o, (u8)*path);
  b->path[o] = 0;
}

/* ------------------------------------------------------------------ the commands */
void ui_command(unsigned long id, long v)
{
  switch (id) {
  case ID_UI_GROUPDD: if (v == 0) group_objects(); else if (v == 1) regroup_objects(); else ungroup_objects(); break;
  case ID_UI_GROUP: group_objects(); break;
  case ID_UI_REGROUP: regroup_objects(); break;
  case ID_UI_UNGROUP: ungroup_objects(); break;
  }
}
