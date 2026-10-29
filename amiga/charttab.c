/*
 * charttab.c - the commands of the Insert tab's Illustrations, Charts, Sparklines and Text groups,
 * the Draw tab, and the Chart Design, Format and Sparkline tabs (and the Objects menu): inserting
 * pictures, shapes, icons, charts and sparklines, drawing with ink, and changing what is selected.
 * The dialogs: Insert Chart (all charts or the recommended ones, with a preview of the selected
 * data), Select Data, Move Chart, Format Selection, Sparklines. Each change is an undo step.
 */
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/intuition.h>
#include <proto/graphics.h>
#include <proto/muimaster.h>
#include <proto/utility.h>
#include <libraries/asl.h>
#include <clib/alib_protos.h>
#include "app.h"
#include "theme.h"
#include "lang.h"
#include "ids.h"
#include "charttab.h"
#include "chartview.h"
#include "../core/drawing.h"
#include "../core/image.h"

#undef Label1
#define Label1(l) QLabel(l, MUIO_Label_SingleFrame)
static ULONG xget(Object *o, ULONG attr) { ULONG v = 0; get(o, attr, &v); return v; }
static const char *sget(Object *o) { const char *s = (const char *)xget(o, MUIA_String_Contents); return s ? s : ""; }
static Object *str_obj(const char *init, int max) { return StringObject, StringFrame, MUIA_String_Contents, (ULONG)init, MUIA_String_MaxLen, max, MUIA_CycleChain, 1, End; }
static Object *cycle_obj(const char **labels, int active) { return CycleObject, MUIA_Cycle_Entries, (ULONG)labels, MUIA_Cycle_Active, active, MUIA_CycleChain, 1, End; }
static Object *check_obj(int on)
{
  return ImageObject, ImageButtonFrame, MUIA_InputMode, MUIV_InputMode_Toggle, MUIA_Image_Spec, MUII_CheckMark,
         MUIA_Image_FreeVert, TRUE, MUIA_Selected, on, MUIA_Background, MUII_ButtonBack, MUIA_ShowSelState, FALSE, MUIA_CycleChain, 1, End;
}
static void latin1_utf8(const char *s, char *u, size_t cap)
{
  size_t o = 0;
  for (; *s && o + 4 < cap; s++) o += (size_t)utf8_put(u + o, (u8)*s);
  u[o] = 0;
}
static void utf8_latin(const char *u, char *out, size_t cap) { utf8_to_latin1(u ? u : "", u ? (u32)strlen(u) : 0, out, cap); }

/* ------------------------------------------------------------------ what is selected */
static ObjView *OV(void) { return (ObjView *)xget(grid, MUIA_Grid_ObjView); }
static DObj *sel_obj(void) { return drw_obj(cur_sheet(), ov_selected(OV())); }
static Chart *sel_chart(void) { DObj *o = sel_obj(); return o && o->kind == OB_CHART ? o->chart : NULL; }
static void begin(void) { drw_begin(book, cur_index()); }
static void end(void) { drw_end(book); refresh_all(); }
/* the data a chart or sparkline is made of: the selection, or the table round the active cell */
static Rect data_region(void)
{
  Sheet *s = cur_sheet();
  Rect a = cur_sel();
  int grew = 1;
  if (a.r1 != a.r2 || a.c1 != a.c2) {
    if (a.r2 >= MAXROW - 1) a.r2 = sheet_last_row(s) > a.r1 ? sheet_last_row(s) : a.r1;
    if (a.c2 >= MAXCOL - 1) a.c2 = sheet_last_col(s) > a.c1 ? sheet_last_col(s) : a.c1;
    return a;
  }
  while (grew) {
    i32 r, c;
    grew = 0;
    if (a.r1 > 0) for (c = a.c1 > 0 ? a.c1 - 1 : 0; c <= a.c2 + 1 && !grew; c++) if (sheet_cell(s, a.r1 - 1, c)) { a.r1--; grew = 1; }
    if (!grew) for (c = a.c1 > 0 ? a.c1 - 1 : 0; c <= a.c2 + 1 && !grew; c++) if (sheet_cell(s, a.r2 + 1, c)) { a.r2++; grew = 1; }
    if (!grew && a.c1 > 0) for (r = a.r1 > 0 ? a.r1 - 1 : 0; r <= a.r2 + 1 && !grew; r++) if (sheet_cell(s, r, a.c1 - 1)) { a.c1--; grew = 1; }
    if (!grew) for (r = a.r1 > 0 ? a.r1 - 1 : 0; r <= a.r2 + 1 && !grew; r++) if (sheet_cell(s, r, a.c2 + 1)) { a.c2++; grew = 1; }
  }
  return a;
}
/* a new object's place: w x h pixels (at 100%) from the active cell, kept in sight */
static void place_new(DObj *o, i32 w, i32 h)
{
  Sheet *s = cur_sheet();
  i32 x = sheet_col_px(s, s->cur_c), y = sheet_row_px(s, s->cur_r);
  dobj_place(s, o, x, y, x + w, y + h);
}
static void select_new(void)
{
  ov_select(OV(), drw_count(cur_sheet()) - 1);
}
static void sheet_text(Rect a, char *out, size_t cap)
{
  /* 'Sheet 1'!$A$1:$D$7 as the dialogs show it (Latin-1) */
  char n[40], r1[16], r2[16];
  utf8_latin(cur_sheet()->name, n, sizeof n);
  ref_text(a.r1, a.c1, 1, 1, r1); ref_text(a.r2, a.c2, 1, 1, r2);
  if (a.r1 == a.r2 && a.c1 == a.c2) snprintf(out, cap, "%s", r1);
  else snprintf(out, cap, "%s:%s", r1, r2);
  (void)n;
}
/* a range typed into a dialog (with or without its sheet); 0 if it isn't one */
static int read_range(const char *t, Rect *a, int *sheet)
{
  char u[300];
  u16 sh; u8 fl;
  int n;
  latin1_utf8(t, u, sizeof u);
  n = parse_ref(book, u[0] == '=' ? u + 1 : u, &sh, a, &fl);
  if (!n) return 0;
  *sheet = sh == SHEET_SELF ? cur_index() : sh;
  if (fl & RF_COLS) { a->r1 = 0; a->r2 = sheet_last_row(book->sh[*sheet]); }
  if (fl & RF_ROWS) { a->c1 = 0; a->c2 = sheet_last_col(book->sh[*sheet]); }
  return 1;
}

/* ------------------------------------------------------------------ a chart preview (the Insert Chart dialog) */
#define MUIA_ChartPreview_Chart (QTAG + 0x7C0)   /* Chart *: shown (and freed by the preview) */
struct PVData { Chart *c; struct { ULONG rgb; LONG pen; } pens[40]; int npens; struct ColorMap *cm; };
static struct MUI_CustomClass *pvcls;
static LONG pv_pen(void *ctx, ULONG rgb)
{
  struct PVData *d = (struct PVData *)ctx;
  int i;
  rgb &= 0xFFFFFF;
  for (i = 0; i < d->npens; i++) if (d->pens[i].rgb == rgb) return d->pens[i].pen;
  if (d->npens >= 40 || !d->cm) return 1;
  d->pens[d->npens].rgb = rgb;
  d->pens[d->npens].pen = ObtainBestPen(d->cm, ((rgb >> 16) & 255) * 0x01010101UL, ((rgb >> 8) & 255) * 0x01010101UL, (rgb & 255) * 0x01010101UL, OBP_Precision, PRECISION_IMAGE, TAG_DONE);
  return d->pens[d->npens++].pen;
}
static ULONG pv_dispatch(__reg("a0") struct IClass *cl, __reg("a2") Object *obj, __reg("a1") Msg msg)
{
  struct PVData *d;
  switch (msg->MethodID) {
  case OM_NEW:
    if (!(obj = (Object *)DoSuperMethodA(cl, obj, msg))) return 0;
    d = INST_DATA(cl, obj);
    memset(d, 0, sizeof *d);
    return (ULONG)obj;
  case OM_DISPOSE:
    d = INST_DATA(cl, obj);
    chart_free(d->c); d->c = NULL;
    break;
  case OM_SET: {
    struct TagItem *tags = ((struct opSet *)msg)->ops_AttrList, *t;
    d = INST_DATA(cl, obj);
    while ((t = NextTagItem(&tags)))
      if (t->ti_Tag == MUIA_ChartPreview_Chart) { chart_free(d->c); d->c = (Chart *)t->ti_Data; MUI_Redraw(obj, MADF_DRAWOBJECT); }
    break;
  }
  case MUIM_AskMinMax: {
    struct MUIP_AskMinMax *m = (struct MUIP_AskMinMax *)msg;
    DoSuperMethodA(cl, obj, msg);
    m->MinMaxInfo->MinWidth += 240; m->MinMaxInfo->DefWidth += 320; m->MinMaxInfo->MaxWidth += 420;
    m->MinMaxInfo->MinHeight += 150; m->MinMaxInfo->DefHeight += 190; m->MinMaxInfo->MaxHeight += 260;
    return 0;
  }
  case MUIM_Setup:
    if (!DoSuperMethodA(cl, obj, msg)) return FALSE;
    d = INST_DATA(cl, obj);
    d->cm = _screen(obj)->ViewPort.ColorMap;
    return TRUE;
  case MUIM_Cleanup: {
    int i;
    d = INST_DATA(cl, obj);
    for (i = 0; i < d->npens; i++) ReleasePen(d->cm, d->pens[i].pen);
    d->npens = 0;
    break;
  }
  case MUIM_Draw:
    DoSuperMethodA(cl, obj, msg);
    d = INST_DATA(cl, obj);
    if (((struct MUIP_Draw *)msg)->flags & (MADF_DRAWOBJECT | MADF_DRAWUPDATE)) {
      struct RastPort *rp = _rp(obj);
      SetAPen(rp, pv_pen(d, 0xFFFFFF));
      RectFill(rp, _mleft(obj), _mtop(obj), _mright(obj), _mbottom(obj));
      if (d->c) {
        PrimList pl;
        prims_init(&pl);
        chart_render(book, cur_index(), d->c, _mwidth(obj), _mheight(obj), 85, ov_text_width, NULL, &pl);
        ov_paint(rp, &pl, _mleft(obj), _mtop(obj), pv_pen, d);
        prims_free(&pl);
      } else {
        const char *t = tr("Select a range with numbers first.");
        theme_text(rp, theme_font(), _mleft(obj) + 8, _mtop(obj) + 8, t, pv_pen(d, 0x595959));
      }
    }
    return 0;
  }
  return DoSuperMethodA(cl, obj, msg);
}
static Object *preview_obj(void)
{
  if (!pvcls) pvcls = MUI_CreateCustomClass(NULL, (STRPTR)MUIC_Area, NULL, sizeof(struct PVData), (APTR)pv_dispatch);
  return pvcls ? NewObject(pvcls->mcc_Class, NULL, TextFrame, MUIA_FillArea, FALSE, TAG_DONE) : NULL;
}

/* ------------------------------------------------------------------ inserting */
static void insert_chart_obj(Chart *c)
{
  DObj *o;
  Sheet *s = cur_sheet();
  i32 x, y;
  if (!c) { ask("OK", "%s", tr("There are no numbers there to make a chart of.\nSelect the cells with the data (and its headings) first.")); return; }
  begin();
  if (!(o = drw_new(s, OB_CHART))) { chart_free(c); end(); return; }
  o->chart = c;
  /* beside the data, in sight */
  x = sheet_col_px(s, s->left + 1); y = sheet_row_px(s, s->top + 1);
  if (c->hassrc && c->srcsheet == cur_index() && sheet_col_px(s, c->src.c2 + 2) < sheet_col_px(s, s->left) + 240) x = sheet_col_px(s, c->src.c2 + 2);
  dobj_place(s, o, x, y, x + 384, y + 228);
  select_new();
  end();
}
static void insert_chart(int kind)
{
  Rect a = data_region();
  insert_chart_obj(chart_from_range(book, cur_index(), a, kind, -1));
}
static Object *ic_preview, *ic_list;
static int ic_kinds[CK_N + 8], ic_n;
static Rect ic_area;
static void ic_show(void)
{
  LONG a = (LONG)xget(ic_list, MUIA_List_Active);
  if (a >= 0 && a < ic_n) set(ic_preview, MUIA_ChartPreview_Chart, (ULONG)chart_from_range(book, cur_index(), ic_area, ic_kinds[a], -1));
}
static void ic_each(ULONG id) { if (id == ID_DLG1) ic_show(); }
/* Insert Chart: every kind (or the recommended ones first), the selected data previewed */
static void dlg_insert_chart(int recommended)
{
  static const ULONG ends[] = { ID_DLGOK, 0 };
  static char labels[CK_N + 8][48];
  Object *w;
  int i, rec[8], nrec = 0;
  ic_area = data_region();
  ic_n = 0;
  if (recommended) nrec = chart_recommend(book, cur_index(), ic_area, rec, 8);
  for (i = 0; i < nrec; i++) { ic_kinds[ic_n] = rec[i]; snprintf(labels[ic_n], sizeof labels[0], "%s %s", "\267", tr(chart_kind_name(rec[i]))); ic_n++; }
  for (i = 0; i < CK_N; i++) { ic_kinds[ic_n] = i; snprintf(labels[ic_n], sizeof labels[0], "%s", tr(chart_kind_name(i))); ic_n++; }
  w = dialog_window(recommended ? "Recommended Charts" : "Insert Chart", HGroup,
        Child, VGroup, MUIA_HorizWeight, 40,
          Child, QLabel(tr(recommended && nrec ? "Recommended first (\267), then every kind:" : "Every kind of chart:"), MUIO_Label_LeftAligned),
          Child, ListviewObject, MUIA_Listview_List, ic_list = ListObject, InputListFrame, End, MUIA_CycleChain, 1, MUIA_Listview_DoubleClick, TRUE, End,
        End,
        Child, ic_preview = preview_obj(),
      End, "Insert");
  if (!w) return;
  for (i = 0; i < ic_n; i++) DoMethod(ic_list, MUIM_List_InsertSingle, (ULONG)labels[i], MUIV_List_Insert_Bottom);
  DoMethod(ic_list, MUIM_Notify, MUIA_List_Active, MUIV_EveryTime, (ULONG)app, 2, MUIM_Application_ReturnID, ID_DLG1);
  DoMethod(ic_list, MUIM_Notify, MUIA_Listview_DoubleClick, TRUE, (ULONG)app, 2, MUIM_Application_ReturnID, ID_DLGOK);
  set(ic_list, MUIA_List_Active, 0);
  ic_show();
  if (run_modal(w, ends, ic_each) == ID_DLGOK) {
    LONG a = (LONG)xget(ic_list, MUIA_List_Active);
    if (a >= 0 && a < ic_n) insert_chart_obj(chart_from_range(book, cur_index(), ic_area, ic_kinds[a], -1));
  }
  MUI_DisposeObject(w);
  if (pvcls) { MUI_DeleteCustomClass(pvcls); pvcls = NULL; }
}
static char picdir[300];
/* a PNG or JPEG file chosen and read: its bytes (malloc'd), size and kind; NULL if none */
static u8 *choose_picture(const char *title, size_t *lenp, int *tp, u32 *w0, u32 *h0, char *path, size_t cap)
{
  struct FileRequester *fr;
  u8 *data;
  int t;
  path[0] = 0;
  if (!(fr = (struct FileRequester *)MUI_AllocAslRequestTags(ASL_FileRequest, ASLFR_TitleText, (ULONG)tr(title), ASLFR_InitialDrawer, (ULONG)picdir,
        ASLFR_InitialPattern, (ULONG)"#?.(png|jpg|jpeg)", ASLFR_DoPatterns, TRUE, TAG_DONE))) return NULL;
  if (MUI_AslRequestTags(fr, ASLFR_Window, (ULONG)_window(grid), TAG_DONE) && fr->fr_File[0]) {
    strncpy(path, (const char *)fr->fr_Drawer, cap - 1); path[cap - 1] = 0;
    strncpy(picdir, (const char *)fr->fr_Drawer, sizeof picdir - 1);
    AddPart((STRPTR)path, fr->fr_File, (ULONG)cap);
  }
  MUI_FreeAslRequest(fr);
  if (!path[0]) return NULL;
  if (!(data = file_read(path, lenp)) || ((t = img_info(data, (u32)*lenp, w0, h0)) != IMG_PNG && t != IMG_JPEG)) {
    free(data);
    ask("OK", "%s", tr("Ledger can't read that picture: it shows PNG and JPEG pictures."));
    return NULL;
  }
  *tp = t;
  return data;
}
static void background(void)
{
  Sheet *s = cur_sheet();
  char path[400];
  size_t len;
  int t;
  u32 w0, h0;
  u8 *data;
  if (s->drw && s->drw->bg) {
    if (ask(tr("Delete|Cancel"), "%s", tr("Take the sheet's background picture off?")) == 1) { begin(); blob_free(s->drw->bg); s->drw->bg = NULL; end(); }
    return;
  }
  if (!(data = choose_picture("Sheet Background", &len, &t, &w0, &h0, path, sizeof path))) return;
  begin();
  if (drw_of(cur_sheet())) { cur_sheet()->drw->bg = blob_new(data, (u32)len); cur_sheet()->drw->bgtype = (u8)(t == IMG_PNG ? PIC_PNG : PIC_JPEG); }
  end();
  free(data);
}
static void insert_picture(void)
{
  char path[400] = "";
  u8 *data = NULL;
  size_t len = 0;
  u32 w0, h0;
  int t;
  if (!(data = choose_picture("Insert Picture", &len, &t, &w0, &h0, path, sizeof path))) return;
  {
    DObj *o;
    i32 w = (i32)w0, h = (i32)h0;
    /* at its own size, made smaller to fit about half the window */
    while (w > 400 || h > 280) { w = w * 3 / 4; h = h * 3 / 4; }
    if (w < 8) w = 8;
    if (h < 8) h = 8;
    begin();
    if ((o = drw_new(cur_sheet(), OB_PICTURE))) {
      o->pic = blob_new(data, (u32)len); o->pictype = (u8)(t == IMG_PNG ? PIC_PNG : PIC_JPEG);
      o->anchor = AN_ONECELL;
      place_new(o, w, h);
      { char u[120]; latin1_utf8((const char *)FilePart((STRPTR)path), u, sizeof u); o->descr = str_dup(u); }
      select_new();
    }
    end();
  }
  free(data);
}
static void insert_icon(int k)
{
  DObj *o;
  if (k < 0 || k >= DRW_NICONS) return;
  begin();
  if ((o = drw_new(cur_sheet(), OB_SHAPE))) {
    o->shape = SK_ICON;
    { char t[8]; sprintf(t, "%d", k); o->text = str_dup(t); }
    o->line = FILL_ON | 0x4472C4;
    snprintf(o->name, sizeof o->name, "Icon: %s %lu", drw_icon_names[k], (unsigned long)o->id);
    o->anchor = AN_ONECELL;
    place_new(o, 64, 64);
    select_new();
  }
  end();
}
static const u8 shape_of[7] = { SK_RECT, SK_ROUNDRECT, SK_ELLIPSE, SK_TRIANGLE, SK_LINE, SK_ARROW, SK_TEXTBOX };
static void place_shape(int k)
{
  ov_tool(OV(), TOOL_PLACE, 0, 0, shape_of[k % 7]);
  refresh_all();
}

/* ------------------------------------------------------------------ sparklines */
static void dlg_sparklines(int type, SparkGroup *edit)
{
  static const ULONG ends[] = { ID_DLGOK, 0 };
  Object *w, *o_data, *o_loc;
  char dtext[100] = "", ltext[100] = "";
  Rect a = cur_sel(), d, l;
  int ds, ls, k;
  SparkGroup keep;
  if (edit) {
    /* the group's cells and data, as rectangles round them */
    Rect dd, ll;
    dd.r1 = dd.c1 = ll.r1 = ll.c1 = 0x7FFFFFF; dd.r2 = dd.c2 = ll.r2 = ll.c2 = -1;
    for (k = 0; k < edit->n; k++) {
      Spark *sp = &edit->sp[k];
      if (sp->r < ll.r1) ll.r1 = sp->r;
      if (sp->r > ll.r2) ll.r2 = sp->r;
      if (sp->c < ll.c1) ll.c1 = sp->c;
      if (sp->c > ll.c2) ll.c2 = sp->c;
      if (sp->f && sp->f->n && (sp->f->t[0].op == T_AREA || sp->f->t[0].op == T_REF)) {
        const Tok *t = &sp->f->t[0];
        i32 r2 = t->op == T_REF ? t->r1 : t->r2, c2 = t->op == T_REF ? t->c1 : t->c2;
        if (t->r1 < dd.r1) dd.r1 = t->r1;
        if (r2 > dd.r2) dd.r2 = r2;
        if (t->c1 < dd.c1) dd.c1 = t->c1;
        if (c2 > dd.c2) dd.c2 = c2;
      }
    }
    if (dd.r2 >= 0) sheet_text(dd, dtext, sizeof dtext);
    sheet_text(ll, ltext, sizeof ltext);
    type = edit->type;
  } else if (a.r1 != a.r2 || a.c1 != a.c2) {
    /* the selection is the data: the sparklines go in the column after it (one a row) */
    Rect loc;
    if (a.r2 >= MAXROW - 1) a.r2 = sheet_last_row(cur_sheet());
    if (a.c2 >= MAXCOL - 1) a.c2 = sheet_last_col(cur_sheet());
    sheet_text(a, dtext, sizeof dtext);
    loc.r1 = a.r1; loc.r2 = a.r2; loc.c1 = loc.c2 = a.c2 + 1;
    sheet_text(loc, ltext, sizeof ltext);
  } else sheet_text(a, ltext, sizeof ltext);
  w = dialog_window(edit ? "Edit Sparklines" : "Create Sparklines", VGroup,
        Child, QLabel(tr("Choose the data you want:"), MUIO_Label_LeftAligned),
        Child, ColGroup(2), Child, Label1(tr("Data Range:")), Child, o_data = str_obj(dtext, 90), End,
        Child, QLabel(tr("Choose where to place the sparklines:"), MUIO_Label_LeftAligned),
        Child, ColGroup(2), Child, Label1(tr("Location Range:")), Child, o_loc = str_obj(ltext, 90), End,
      End, "OK");
  if (!w) return;
  set(w, MUIA_Window_ActiveObject, (ULONG)(dtext[0] ? o_loc : o_data));
  if (run_modal(w, ends, NULL) != ID_DLGOK) { MUI_DisposeObject(w); return; }
  if (!read_range(sget(o_data), &d, &ds) || !read_range(sget(o_loc), &l, &ls) || ls != cur_index()) {
    MUI_DisposeObject(w);
    ask("OK", "%s", tr("The data range or the location range isn't a range of cells on this sheet."));
    return;
  }
  MUI_DisposeObject(w);
  memset(&keep, 0, sizeof keep);
  if (edit) { keep = *edit; keep.sp = NULL; keep.n = 0; }
  book_begin(book);
  if (edit) { Rect all; all.r1 = 0; all.c1 = 0; all.r2 = MAXROW - 1; all.c2 = MAXCOL - 1; (void)all; book_clear_sparklines(book, cur_index(), l, 1); }
  if (ds != cur_index() || !book_add_sparklines(book, cur_index(), type, d, l)) {
    book_end(book);
    ask("OK", "%s", tr("The location range must be one row or one column, with a cell for each row or column of the data."));
    refresh_all();
    return;
  }
  if (edit) {
    /* the options carried over to the new group */
    Drawing *dr = cur_sheet()->drw;
    SparkGroup *g = &dr->g[dr->ng - 1];
    Spark *sp = g->sp; int n = g->n;
    *g = keep; g->sp = sp; g->n = n;
  }
  book_end(book);
  refresh_all();
}

/* ------------------------------------------------------------------ chart dialogs */
static Object *sd_list;
static Chart *sd_c;
static char sd_names[64][60];
static void sd_fill(void)
{
  CData d;
  int k;
  DoMethod(sd_list, MUIM_List_Clear);
  if (!chart_resolve(book, cur_index(), sd_c, &d)) return;
  for (k = 0; k < d.ns && k < 64; k++) { utf8_latin(d.s[k].name, sd_names[k], sizeof sd_names[k]); DoMethod(sd_list, MUIM_List_InsertSingle, (ULONG)sd_names[k], MUIV_List_Insert_Bottom); }
  cdata_free(&d);
}
static Object *sd_range, *sd_rows;
static void sd_each(ULONG id)
{
  if (id == ID_DLG1) {
    /* Remove: the chosen series out */
    LONG a = (LONG)xget(sd_list, MUIA_List_Active);
    if (a >= 0 && a < sd_c->ns && sd_c->ns > 1) {
      CSeries *s = &sd_c->s[a];
      formula_free(s->name); formula_free(s->cat); formula_free(s->val); formula_free(s->size); free(s->lit);
      memmove(&sd_c->s[a], &sd_c->s[a + 1], (size_t)(sd_c->ns - a - 1) * sizeof(CSeries));
      sd_c->ns--;
      sd_c->hassrc = 0;
      sd_fill();
    }
  } else if (id == ID_DLG2 || id == ID_DLG3) {
    /* the range again, or rows and columns switched */
    Rect a; int sh;
    if (read_range(sget(sd_range), &a, &sh) && chart_set_source(book, sd_c, sh, a, (int)xget(sd_rows, MUIA_Cycle_Active))) sd_fill();
  }
}
static void dlg_select_data(void)
{
  static const ULONG ends[] = { ID_DLGOK, 0 };
  static const char *rows_l[3];
  Chart *c = sel_chart();
  DObj *o = sel_obj();
  Object *w, *rem, *apply;
  char rt[100] = "";
  if (!c) return;
  rows_l[0] = tr("Columns"); rows_l[1] = tr("Rows"); rows_l[2] = NULL;
  if (!(sd_c = chart_copy(c))) return;
  if (c->hassrc) sheet_text(c->src, rt, sizeof rt);
  w = dialog_window("Select Data Source", VGroup,
        Child, HGroup, Child, Label1(tr("Chart data range:")), Child, sd_range = str_obj(rt, 90), Child, apply = QButton(tr("Use")), End,
        Child, HGroup, Child, Label1(tr("Series in:")), Child, sd_rows = cycle_obj(rows_l, c->byrows), Child, HSpace(0), End,
        Child, QLabel(tr("Legend entries (series):"), MUIO_Label_LeftAligned),
        Child, ListviewObject, MUIA_Listview_List, sd_list = ListObject, InputListFrame, End, MUIA_CycleChain, 1, End,
        Child, HGroup, Child, rem = QButton(tr("Remove")), Child, HSpace(0), End,
      End, "OK");
  if (!w) { chart_free(sd_c); return; }
  set(w, MUIA_Window_Width, MUIV_Window_Width_Visible(45));
  DoMethod(rem, MUIM_Notify, MUIA_Pressed, FALSE, (ULONG)app, 2, MUIM_Application_ReturnID, ID_DLG1);
  DoMethod(apply, MUIM_Notify, MUIA_Pressed, FALSE, (ULONG)app, 2, MUIM_Application_ReturnID, ID_DLG2);
  DoMethod(sd_range, MUIM_Notify, MUIA_String_Acknowledge, MUIV_EveryTime, (ULONG)app, 2, MUIM_Application_ReturnID, ID_DLG2);
  DoMethod(sd_rows, MUIM_Notify, MUIA_Cycle_Active, MUIV_EveryTime, (ULONG)app, 2, MUIM_Application_ReturnID, ID_DLG3);
  sd_fill();
  if (run_modal(w, ends, sd_each) == ID_DLGOK && o) {
    begin();
    chart_free(o->chart); o->chart = sd_c; sd_c = NULL;
    end();
  }
  chart_free(sd_c); sd_c = NULL;
  MUI_DisposeObject(w);
}
static void dlg_move_chart(void)
{
  static const ULONG ends[] = { ID_DLGOK, 0 };
  static const char *sheets[MAXSHEETS + 1];
  static char names[MAXSHEETS][40];
  Object *w, *cyc;
  int i, n = 0, from = cur_index(), to, k = ov_selected(OV());
  DObj *o = sel_obj();
  if (!o) return;
  for (i = 0; i < book->nsh && n < MAXSHEETS; i++) { utf8_latin(book->sh[i]->name, names[i], sizeof names[i]); sheets[n++] = names[i]; }
  sheets[n] = NULL;
  w = dialog_window("Move Chart", HGroup, Child, Label1(tr("Object in:")), Child, cyc = cycle_obj(sheets, from), End, "OK");
  if (!w) return;
  if (run_modal(w, ends, NULL) == ID_DLGOK && (to = (int)xget(cyc, MUIA_Cycle_Active)) != from && to >= 0 && to < book->nsh) {
    DObj copy, *no;
    if (dobj_copy(&copy, o)) {
      book_begin(book);
      book_note_all(book);
      if ((no = drw_new(book->sh[to], o->kind))) {
        u32 id = no->id;
        *no = copy;
        no->id = id;
        /* references without a sheet name keep meaning the first sheet's cells: the series name them */
        drw_remove(book->sh[from], k);
      } else dobj_clear(&copy);
      book_end(book);
      book->active = to;
      set(grid, MUIA_Grid_Book, (ULONG)book);
      ov_select(OV(), drw_count(book->sh[to]) - 1);
      refresh_all();
    }
  }
  MUI_DisposeObject(w);
}
/* the colours the Format dialog offers (0: keep as it is) */
static const char *const fcol_names[] = { "(as it is)", "Automatic", "None", "Black", "White", "Grey", "Light Grey", "Blue", "Light Blue", "Dark Blue",
                                          "Orange", "Gold", "Yellow", "Green", "Light Green", "Red", "Dark Red", "Purple", "More Colours...", NULL };
static const ULONG fcol_vals[] = { 0, 1, 2, 0x000000, 0xFFFFFF, 0x808080, 0xD9D9D9, 0x4472C4, 0x9DC3E6, 0x203864, 0xED7D31, 0xBF9000, 0xFFFF00,
                                   0x70AD47, 0xC5E0B4, 0xFF0000, 0xC00000, 0x7030A0, 3 };
/* the colour chosen: 0 keep, 1 automatic, else FILL_ON | rgb (None: FILL_NONE) */
#define FILL_NONE 0xFE000001UL
static ULONG fcol_get(Object *cyc, ULONG now)
{
  LONG a = (LONG)xget(cyc, MUIA_Cycle_Active);
  if (a <= 0) return now;
  if (fcol_vals[a] == 1) return 0;
  if (fcol_vals[a] == 2) return FILL_NONE;
  if (fcol_vals[a] == 3) { LONG c = ask_color(now & 0xFFFFFF); return c < 0 ? now : FILL_ON | (ULONG)c; }
  return FILL_ON | fcol_vals[a];
}
static const char **trn(const char *const *en, const char **out) { int i; for (i = 0; en[i]; i++) out[i] = tr(en[i]); out[i] = NULL; return out; }
static int elem_index(int e) { return e == CE_PLOT ? 1 : e == CE_TITLE ? 2 : e == CE_LEGEND ? 3 : e >= CE_SERIES ? 4 + e - CE_SERIES : 0; }
static int elem_of(int i) { return i == 1 ? CE_PLOT : i == 2 ? CE_TITLE : i == 3 ? CE_LEGEND : i >= 4 ? CE_SERIES + i - 4 : CE_CHART; }
static char elab[44][56];
static const char *eptr[45];
/* the selected chart's parts, by name (the Format tab's list and dialog) */
static int chart_elems(const Chart *c)
{
  static const char *const fixed[4] = { "Chart Area", "Plot Area", "Chart Title", "Legend" };
  CData d;
  int n = 0, k;
  for (k = 0; k < 4; k++) { strcpy(elab[n], fixed[k]); eptr[n] = elab[n]; n++; }
  if (c && chart_resolve(book, cur_index(), c, &d)) {
    for (k = 0; k < d.ns && n < 44; k++) { char t[40]; utf8_latin(d.s[k].name, t, sizeof t); snprintf(elab[n], sizeof elab[n], "Series \"%s\"", t); eptr[n] = elab[n]; n++; }
    cdata_free(&d);
  }
  eptr[n] = NULL;
  return n;
}
static void apply_colour(Chart *c, DObj *o, int e, ULONG v, int outline)
{
  ULONG col = v == FILL_NONE ? 0 : v;
  if (c) {
    if (e >= CE_SERIES && e - CE_SERIES < c->ns) { if (outline) c->s[e - CE_SERIES].line = col; else c->s[e - CE_SERIES].color = col; }
    else if (e < CE_N) { if (outline) c->eline[e] = col; else c->efill[e] = col; }
  } else if (o) { if (outline) o->line = v == FILL_NONE ? 0 : col; else o->fill = v == FILL_NONE ? 0 : col; }
}
static void num_text(const CAxis *a, int which, char *out)
{
  int set = which == 0 ? a->minset : which == 1 ? a->maxset : a->majset;
  double v = which == 0 ? a->min : which == 1 ? a->max : a->major;
  if (set) num_general(v, out, 11); else out[0] = 0;
}
static void num_read(Object *s, u8 *set, double *v)
{
  const char *t = sget(s);
  if (!*t) { *set = 0; return; }
  *set = 1; *v = atof(t);
}
static void dlg_format(void)
{
  static const ULONG ends[] = { ID_DLGOK, 0 };
  static const char *cols[20], *legs[6], *aligns[4], *valigns[4];
  static const char *const leg_en[] = { "None", "Bottom", "Top", "Left", "Right", NULL }, *al_en[] = { "Left", "Centre", "Right", NULL }, *va_en[] = { "Top", "Middle", "Bottom", NULL };
  DObj *o = sel_obj();
  Chart *c = sel_chart();
  Object *w, *o_el = NULL, *o_fill = NULL, *o_line, *o_title = NULL, *o_show = NULL, *o_xt = NULL, *o_yt = NULL, *o_min = NULL, *o_max = NULL, *o_maj = NULL;
  Object *o_leg = NULL, *o_lab = NULL, *o_grid = NULL, *o_text = NULL, *o_size = NULL, *o_bold = NULL, *o_ital = NULL, *o_al = NULL, *o_va = NULL, *o_lw = NULL, *o_alt = NULL, *o_w, *o_h;
  char t1[200], t2[100], t3[100], mn[32], mx[32], mj[32], sz[16], lw[16], wt[16], ht[16], alt[200];
  i32 x0, y0, x1, y1;
  if (!o) return;
  trn(fcol_names, cols); trn(leg_en, legs); trn(al_en, aligns); trn(va_en, valigns);
  dobj_box(cur_sheet(), o, &x0, &y0, &x1, &y1);
  sprintf(wt, "%ld", (long)(x1 - x0)); sprintf(ht, "%ld", (long)(y1 - y0));
  sprintf(lw, "%g", (o->linew ? o->linew : 75) / 100.0);
  if (c) {
    chart_elems(c);
    utf8_latin(c->title ? c->title : "", t1, sizeof t1); utf8_latin(c->xtitle ? c->xtitle : "", t2, sizeof t2); utf8_latin(c->ytitle ? c->ytitle : "", t3, sizeof t3);
    num_text(&c->va, 0, mn); num_text(&c->va, 1, mx); num_text(&c->va, 2, mj);
    w = dialog_window("Format Chart", VGroup,
          Child, ColGroup(4),
            Child, Label1(tr("Part:")), Child, o_el = cycle_obj(eptr, elem_index(ov_elem(OV()))), Child, Label1(tr("Legend:")), Child, o_leg = cycle_obj(legs, c->legend),
            Child, Label1(tr("Fill:")), Child, o_fill = cycle_obj(cols, 0), Child, Label1(tr("Outline:")), Child, o_line = cycle_obj(cols, 0),
          End,
          Child, ColGroup(2),
            Child, Label1(tr("Title:")), Child, HGroup, Child, o_show = check_obj(c->has_title), Child, o_title = str_obj(t1, 190), End,
            Child, Label1(tr("Horizontal axis title:")), Child, o_xt = str_obj(t2, 90),
            Child, Label1(tr("Vertical axis title:")), Child, o_yt = str_obj(t3, 90),
          End,
          Child, HGroup, Child, Label1(tr("Value axis from")), Child, o_min = str_obj(mn, 30), Child, Label1(tr("to")), Child, o_max = str_obj(mx, 30),
            Child, Label1(tr("step")), Child, o_maj = str_obj(mj, 30), End,
          Child, HGroup, Child, o_lab = check_obj(c->labels), Child, Label1(tr("Data labels")), Child, o_grid = check_obj(c->hgrid), Child, Label1(tr("Gridlines")), Child, HSpace(0), End,
          Child, HGroup, Child, Label1(tr("Size: width")), Child, o_w = str_obj(wt, 8), Child, Label1(tr("height")), Child, o_h = str_obj(ht, 8), Child, Label1(tr("pixels")), Child, HSpace(0), End,
        End, "OK");
  } else if (o->kind == OB_PICTURE) {
    utf8_latin(o->descr ? o->descr : "", alt, sizeof alt);
    w = dialog_window("Format Picture", ColGroup(2),
          Child, Label1(tr("Outline:")), Child, o_line = cycle_obj(cols, 0),
          Child, Label1(tr("Outline width (pt):")), Child, o_lw = str_obj(lw, 10),
          Child, Label1(tr("Alt text:")), Child, o_alt = str_obj(alt, 190),
          Child, Label1(tr("Width (pixels):")), Child, o_w = str_obj(wt, 8),
          Child, Label1(tr("Height (pixels):")), Child, o_h = str_obj(ht, 8),
        End, "OK");
  } else {
    utf8_latin(o->text ? o->text : "", t1, sizeof t1);
    sprintf(sz, "%g", (o->fsize ? o->fsize : 110) / 10.0);
    w = dialog_window("Format Shape", ColGroup(2),
          Child, Label1(tr("Fill:")), Child, o_fill = cycle_obj(cols, 0),
          Child, Label1(tr("Outline:")), Child, o_line = cycle_obj(cols, 0),
          Child, Label1(tr("Outline width (pt):")), Child, o_lw = str_obj(lw, 10),
          Child, Label1(tr("Text:")), Child, o_text = str_obj(t1, 190),
          Child, Label1(tr("Text size (pt):")), Child, HGroup, Child, o_size = str_obj(sz, 10), Child, o_bold = check_obj(o->bold), Child, Label1(tr("Bold")),
            Child, o_ital = check_obj(o->italic), Child, Label1(tr("Italic")), End,
          Child, Label1(tr("Text place:")), Child, HGroup, Child, o_al = cycle_obj(aligns, o->halign % 3), Child, o_va = cycle_obj(valigns, o->valign % 3), End,
          Child, Label1(tr("Width (pixels):")), Child, o_w = str_obj(wt, 8),
          Child, Label1(tr("Height (pixels):")), Child, o_h = str_obj(ht, 8),
        End, "OK");
  }
  if (!w) return;
  if (run_modal(w, ends, NULL) == ID_DLGOK) {
    char u[400];
    i32 nw = atol(sget(o_w)), nh = atol(sget(o_h));
    begin();
    if (c) {
      int e = elem_of((int)xget(o_el, MUIA_Cycle_Active));
      ULONG f = fcol_get(o_fill, 0), l = fcol_get(o_line, 0);
      if (xget(o_fill, MUIA_Cycle_Active)) apply_colour(c, NULL, e, f, 0);
      if (xget(o_line, MUIA_Cycle_Active)) apply_colour(c, NULL, e, l, 1);
      ov_set_elem(OV(), e);
      c->has_title = (u8)(xget(o_show, MUIA_Selected) != 0);
      latin1_utf8(sget(o_title), u, sizeof u); free(c->title); c->title = u[0] ? str_dup(u) : NULL;
      latin1_utf8(sget(o_xt), u, sizeof u); free(c->xtitle); c->xtitle = u[0] ? str_dup(u) : NULL;
      latin1_utf8(sget(o_yt), u, sizeof u); free(c->ytitle); c->ytitle = u[0] ? str_dup(u) : NULL;
      num_read(o_min, &c->va.minset, &c->va.min); num_read(o_max, &c->va.maxset, &c->va.max); num_read(o_maj, &c->va.majset, &c->va.major);
      if (c->va.majset && c->va.major <= 0) c->va.majset = 0;
      c->legend = (u8)xget(o_leg, MUIA_Cycle_Active);
      c->labels = (u8)(xget(o_lab, MUIA_Selected) != 0);
      c->hgrid = (u8)(xget(o_grid, MUIA_Selected) != 0);
    } else {
      if (o_fill && xget(o_fill, MUIA_Cycle_Active)) apply_colour(NULL, o, 0, fcol_get(o_fill, o->fill), 0);
      if (xget(o_line, MUIA_Cycle_Active)) apply_colour(NULL, o, 0, fcol_get(o_line, o->line), 1);
      if (o_lw) { double v = atof(sget(o_lw)); if (v > 0 && v < 100) o->linew = (u16)(v * 100 + 0.5); }
      if (o_alt) { latin1_utf8(sget(o_alt), u, sizeof u); free(o->descr); o->descr = u[0] ? str_dup(u) : NULL; }
      if (o_text) { latin1_utf8(sget(o_text), u, sizeof u); free(o->text); o->text = u[0] ? str_dup(u) : NULL; }
      if (o_size) { double v = atof(sget(o_size)); if (v >= 4 && v <= 200) o->fsize = (u16)(v * 10 + 0.5); }
      if (o_bold) o->bold = (u8)(xget(o_bold, MUIA_Selected) != 0);
      if (o_ital) o->italic = (u8)(xget(o_ital, MUIA_Selected) != 0);
      if (o_al) o->halign = (u8)xget(o_al, MUIA_Cycle_Active);
      if (o_va) o->valign = (u8)xget(o_va, MUIA_Cycle_Active);
    }
    if (nw > 2 && nh > 2 && nw < 10000 && nh < 10000 && (nw != x1 - x0 || nh != y1 - y0)) dobj_place(cur_sheet(), o, x0, y0, x0 + nw, y0 + nh);
    end();
  }
  MUI_DisposeObject(w);
}
static void edit_text(void)
{
  DObj *o = sel_obj();
  char t[400], u[800];
  if (!o || o->kind != OB_SHAPE || o->shape == SK_LINE || o->shape == SK_ARROW || o->shape == SK_INK || o->shape == SK_ICON) return;
  utf8_latin(o->text ? o->text : "", t, sizeof t);
  if (!ask_string(o->shape == SK_TEXTBOX ? "Text Box" : "Shape Text", "Text:", t, sizeof t)) return;
  latin1_utf8(t, u, sizeof u);
  begin();
  free(o->text); o->text = u[0] ? str_dup(u) : NULL;
  end();
}
static void chart_title(void)
{
  Chart *c = sel_chart();
  char t[200], u[400];
  if (!c) return;
  utf8_latin(c->title ? c->title : "", t, sizeof t);
  if (!ask_string("Chart Title", "Title:", t, sizeof t)) return;
  latin1_utf8(t, u, sizeof u);
  begin();
  free(c->title); c->title = u[0] ? str_dup(u) : NULL;
  c->has_title = 1;
  end();
}
static void add_element(int v)
{
  Chart *c = sel_chart();
  if (!c) return;
  begin();
  switch (v) {
  case 0: c->has_title = 1; break;
  case 1: c->has_title = 0; break;
  case 2: if (!c->xtitle) c->xtitle = str_dup("Axis Title"); if (!c->ytitle) c->ytitle = str_dup("Axis Title"); break;
  case 3: free(c->xtitle); free(c->ytitle); c->xtitle = c->ytitle = NULL; break;
  case 4: c->legend = LG_RIGHT; break;
  case 5: c->legend = LG_TOP; break;
  case 6: c->legend = LG_LEFT; break;
  case 7: c->legend = LG_BOTTOM; break;
  case 8: c->legend = LG_NONE; break;
  case 9: c->labels = 1; break;
  case 10: c->labels = 0; break;
  case 11: c->hgrid = 1; break;
  case 12: c->hgrid = 0; c->vgrid = 0; break;
  case 13: c->vgrid = !c->vgrid; break;
  }
  end();
}
static void change_type(int k)
{
  Chart *c = sel_chart();
  if (!c || k < 0 || k >= CK_N) return;
  begin();
  c->kind = (u8)k;
  /* a pie's legend names the slices; bubbles need sizes (each value its own) */
  if (chart_is_pielike(k)) { c->hgrid = 0; if (c->legend == LG_NONE && k != CK_FUNNEL) c->legend = LG_BOTTOM; }
  end();
}

/* ------------------------------------------------------------------ ink to shape */
static void ink_to_shape(void)
{
  ObjView *ov = OV();
  Sheet *s = cur_sheet();
  int k = ov_selected(ov);
  DObj *o = drw_obj(s, k);
  const char *p;
  double xs[4000], ys[4000], area = 0, mnx = 1e9, mny = 1e9, mxx = -1e9, mxy = -1e9, diag, gap, fill;
  int n = 0, i, shape;
  if (!o || o->kind != OB_SHAPE || o->shape != SK_INK) { k = ov_last_ink(ov); o = drw_obj(s, k); }
  if (!o || o->kind != OB_SHAPE || o->shape != SK_INK) { ask("OK", "%s", tr("Draw an ink stroke (or select one) first.")); return; }
  for (p = o->text ? o->text : ""; *p && n < 4000; ) {
    xs[n] = strtol(p, (char **)&p, 10); if (*p == ',') p++;
    ys[n] = strtol(p, (char **)&p, 10);
    if (xs[n] < mnx) mnx = xs[n];
    if (xs[n] > mxx) mxx = xs[n];
    if (ys[n] < mny) mny = ys[n];
    if (ys[n] > mxy) mxy = ys[n];
    n++;
    while (*p == ';') p++;
  }
  if (n < 2) return;
  diag = sqrt((mxx - mnx) * (mxx - mnx) + (mxy - mny) * (mxy - mny));
  gap = sqrt((xs[0] - xs[n - 1]) * (xs[0] - xs[n - 1]) + (ys[0] - ys[n - 1]) * (ys[0] - ys[n - 1]));
  for (i = 0; i < n; i++) area += xs[i] * ys[(i + 1) % n] - xs[(i + 1) % n] * ys[i];
  fill = (mxx - mnx) * (mxy - mny) > 0 ? fabs(area) / 2 / ((mxx - mnx) * (mxy - mny)) : 0;
  /* open: a line (or an arrow, drawn with a hook at its end); closed: by how much of its box it fills */
  if (diag < 1 || gap > diag * 0.3) shape = SK_LINE;
  else if (fill > 0.86) shape = SK_RECT;
  else if (fill > 0.62) shape = SK_ELLIPSE;
  else shape = SK_TRIANGLE;
  begin();
  if (shape == SK_LINE) {
    o->flipx = (u8)(xs[n - 1] < xs[0]); o->flipy = (u8)(ys[n - 1] < ys[0]);
    if (fabs(ys[n - 1] - ys[0]) < diag * 0.12) { o->flipy = 0; }
  }
  o->shape = (u8)shape;
  free(o->text); o->text = NULL;
  o->fill = 0;
  if (o->linew > 400) o->linew = 200;
  snprintf(o->name, sizeof o->name, "%s %lu", shape == SK_LINE ? "Straight Connector" : shape == SK_RECT ? "Rectangle" : shape == SK_ELLIPSE ? "Oval" : "Triangle", (unsigned long)o->id);
  if (shape == SK_LINE && fabs(ys[n - 1] - ys[0]) < diag * 0.12) {
    /* a level line: its box flattened */
    i32 x0, y0, x1, y1;
    dobj_box(s, o, &x0, &y0, &x1, &y1);
    dobj_place(s, o, x0, (y0 + y1) / 2, x1, (y0 + y1) / 2 + 1);
  }
  ov_select(ov, k);
  end();
}

/* ------------------------------------------------------------------ sparkline options */
static SparkGroup *cur_spark(void)
{
  SparkGroup *g = NULL;
  Sheet *s = cur_sheet();
  return spark_at(s, s->cur_r, s->cur_c, &g) ? g : NULL;
}
static void spark_set(ULONG id)
{
  SparkGroup *g = cur_spark();
  if (!g) return;
  begin();
  g = cur_spark();          /* (the same group in the sheet as it is now) */
  if (g) switch (id) {
  case ID_X_SPTLINE: g->type = SP_LINE; break;
  case ID_X_SPTCOL: g->type = SP_COLUMN; break;
  case ID_X_SPTWIN: g->type = SP_WINLOSS; break;
  case ID_X_SPHIGH: g->high = !g->high; break;
  case ID_X_SPLOW: g->low = !g->low; break;
  case ID_X_SPNEG: g->negative = !g->negative; break;
  case ID_X_SPFIRST: g->first = !g->first; break;
  case ID_X_SPLAST: g->last = !g->last; break;
  case ID_X_SPMARK: g->markers = !g->markers; break;
  }
  end();
}

/* ------------------------------------------------------------------ arranging */
static void align_objects(int how)
{
  int sel[32], n = ov_selection(OV(), sel, 32), k;
  Sheet *s = cur_sheet();
  i32 ax0, ay0, ax1, ay1;
  if (n < 2) { ask("OK", "%s", tr("Select two or more objects first (hold Shift and click them, or drag round them with Select Objects).")); return; }
  dobj_box(s, drw_obj(s, sel[0]), &ax0, &ay0, &ax1, &ay1);
  begin();
  for (k = 1; k < n; k++) {
    DObj *o = drw_obj(s, sel[k]);
    i32 x0, y0, x1, y1, dx = 0, dy = 0;
    if (!o) continue;
    dobj_box(s, o, &x0, &y0, &x1, &y1);
    switch (how) {
    case 0: dx = ax0 - x0; break;
    case 1: dx = (ax0 + ax1) / 2 - (x0 + x1) / 2; break;
    case 2: dx = ax1 - x1; break;
    case 3: dy = ay0 - y0; break;
    case 4: dy = (ay0 + ay1) / 2 - (y0 + y1) / 2; break;
    default: dy = ay1 - y1; break;
    }
    if (x0 + dx < 0) dx = -x0;
    if (y0 + dy < 0) dy = -y0;
    dobj_place(s, o, x0 + dx, y0 + dy, x1 + dx, y1 + dy);
  }
  end();
}
static void rotate_shape(void)
{
  DObj *o = sel_obj();
  Sheet *s = cur_sheet();
  i32 x0, y0, x1, y1, cx, cy, w, h;
  if (!o || o->kind != OB_SHAPE) return;
  dobj_box(s, o, &x0, &y0, &x1, &y1);
  cx = (x0 + x1) / 2; cy = (y0 + y1) / 2; w = x1 - x0; h = y1 - y0;
  begin();
  /* a quarter turn about its middle: its width and height change places (a line turns too) */
  if (o->shape == SK_LINE || o->shape == SK_ARROW) { u8 fx = o->flipx; o->flipx = (u8)!o->flipy; o->flipy = fx; }
  dobj_place(s, o, cx - h / 2 > 0 ? cx - h / 2 : 0, cy - w / 2 > 0 ? cy - w / 2 : 0, (cx - h / 2 > 0 ? cx - h / 2 : 0) + h, (cy - w / 2 > 0 ? cy - w / 2 : 0) + w);
  end();
}
/* the Selection Pane: the objects by name, topmost first */
static Object *sp_list;
static char sp_names[200][64];
static int sp_n;
static void sp_fill(void)
{
  Sheet *s = cur_sheet();
  int i;
  static const char *const kinds[3] = { "chart", "picture", "shape" };
  DoMethod(sp_list, MUIM_List_Clear);
  sp_n = drw_count(s) < 200 ? drw_count(s) : 200;
  for (i = 0; i < sp_n; i++) {
    DObj *o = drw_obj(s, sp_n - 1 - i);
    char nm[48];
    utf8_latin(o->name, nm, sizeof nm);
    snprintf(sp_names[i], sizeof sp_names[i], "%s  (%s)", nm, tr(kinds[o->kind % 3]));
    DoMethod(sp_list, MUIM_List_InsertSingle, (ULONG)sp_names[i], MUIV_List_Insert_Bottom);
  }
}
static void sp_each(ULONG id)
{
  LONG a = (LONG)xget(sp_list, MUIA_List_Active);
  Sheet *s = cur_sheet();
  int k = a >= 0 && a < sp_n ? sp_n - 1 - (int)a : -1;
  if (k < 0) return;
  if (id == ID_DLG1) { ov_select(OV(), k); refresh_all(); }
  else if (id == ID_DLG2 || id == ID_DLG3) {
    int to = id == ID_DLG2 ? k + 1 : k - 1;
    if (to < 0 || to >= drw_count(s)) return;
    begin(); drw_move_z(s, k, to); ov_select(OV(), to); end();
    sp_fill(); set(sp_list, MUIA_List_Active, sp_n - 1 - to);
  } else if (id == ID_DLG4) {
    char t[48], u[96];
    DObj *o = drw_obj(s, k);
    utf8_latin(o->name, t, sizeof t);
    if (!ask_string("Rename", "Name:", t, sizeof t) || !t[0]) return;
    latin1_utf8(t, u, sizeof u);
    begin(); o = drw_obj(s, k); snprintf(o->name, sizeof o->name, "%s", u); end();
    sp_fill(); set(sp_list, MUIA_List_Active, a);
  } else if (id == ID_DLG5) {
    begin(); drw_remove(s, k); ov_select(OV(), -1); end();
    sp_fill();
  }
}
static void dlg_selection_pane(void)
{
  static const ULONG ends[] = { ID_DLGOK, 0 };
  Object *w, *b1, *b2, *b3, *b4, *b5;
  w = dialog_window("Selection Pane", VGroup,
        Child, QLabel(tr("Objects on this sheet, the front one first:"), MUIO_Label_LeftAligned),
        Child, ListviewObject, MUIA_Listview_List, sp_list = ListObject, InputListFrame, End, MUIA_CycleChain, 1, MUIA_Listview_DoubleClick, TRUE, End,
        Child, HGroup, Child, b1 = QButton(tr("Select")), Child, b2 = QButton(tr("Forward")), Child, b3 = QButton(tr("Backward")),
          Child, b4 = QButton(tr("Rename")), Child, b5 = QButton(tr("Delete")), End,
      End, "Close");
  if (!w) return;
  set(w, MUIA_Window_Height, MUIV_Window_Height_Visible(50));
  DoMethod(b1, MUIM_Notify, MUIA_Pressed, FALSE, (ULONG)app, 2, MUIM_Application_ReturnID, ID_DLG1);
  DoMethod(sp_list, MUIM_Notify, MUIA_Listview_DoubleClick, TRUE, (ULONG)app, 2, MUIM_Application_ReturnID, ID_DLG1);
  DoMethod(b2, MUIM_Notify, MUIA_Pressed, FALSE, (ULONG)app, 2, MUIM_Application_ReturnID, ID_DLG2);
  DoMethod(b3, MUIM_Notify, MUIA_Pressed, FALSE, (ULONG)app, 2, MUIM_Application_ReturnID, ID_DLG3);
  DoMethod(b4, MUIM_Notify, MUIA_Pressed, FALSE, (ULONG)app, 2, MUIM_Application_ReturnID, ID_DLG4);
  DoMethod(b5, MUIM_Notify, MUIA_Pressed, FALSE, (ULONG)app, 2, MUIM_Application_ReturnID, ID_DLG5);
  sp_fill();
  run_modal(w, ends, sp_each);
  MUI_DisposeObject(w);
}

/* ------------------------------------------------------------------ the ribbon's states */
ULONG chart_ribbon_state(void)
{
  ULONG ext = 0, col;
  ObjView *ov = OV();
  DObj *o = sel_obj();
  SparkGroup *g = cur_spark();
  int tool = ov_tool_now(ov, &col, NULL);
  if (o) {
    ext |= 1UL << RBX_OBJECT;
    if (o->kind == OB_CHART) ext |= 1UL << RBX_CHART;
    else if (o->kind == OB_PICTURE) ext |= 1UL << RBX_PICTURE;
    else { ext |= 1UL << RBX_SHAPE; if (o->shape == SK_INK) ext |= 1UL << RBX_INK; }
  }
  {
    /* the Format tab's list of the chart's parts */
    int n = chart_elems(o && o->kind == OB_CHART ? o->chart : NULL);
    ribbon_set_elems(eptr, o && o->kind == OB_CHART ? n : 0, o && o->kind == OB_CHART ? elem_index(ov_elem(ov)) : -1);
  }
  if (tool == TOOL_LASSO) ext |= 1UL << RBX_LASSO;
  if (tool == TOOL_ERASER) ext |= 1UL << RBX_ERASER;
  if (tool == TOOL_PEN) ext |= 1UL << (col == 0xF2E33A ? RBX_HIGHLIGHT : col == 0x000000 ? RBX_PENBLACK : col == 0xD13B2F ? RBX_PENRED : RBX_PENBLUE);
  if (g) {
    ext |= 1UL << RBX_SPARK;
    ext |= 1UL << (g->type == SP_COLUMN ? RBX_SPCOL : g->type == SP_WINLOSS ? RBX_SPWIN : RBX_SPLINE);
    if (g->high) ext |= 1UL << RBX_SPHIGH;
    if (g->low) ext |= 1UL << RBX_SPLOW;
    if (g->negative) ext |= 1UL << RBX_SPNEG;
    if (g->first) ext |= 1UL << RBX_SPFIRST;
    if (g->last) ext |= 1UL << RBX_SPLAST;
    if (g->markers) ext |= 1UL << RBX_SPMARK;
  }
  return ext;
}

/* ------------------------------------------------------------------ the commands */
static void pen(ULONG rgb, int width)
{
  ObjView *ov = OV();
  ULONG now;
  int w;
  /* a second click on the pen in use puts it down */
  if (ov_tool_now(ov, &now, &w) == TOOL_PEN && now == rgb) ov_tool(ov, TOOL_SELECT, rgb, w, 0);
  else ov_tool(ov, TOOL_PEN, rgb, width, 0);
  refresh_all();
}
static ULONG colour_choice(LONG v)
{
  /* a ribbon colour list's choice: COLOR_AUTO automatic (0), More Colours asks */
  if ((ULONG)v == COLOR_MORE) { LONG c = ask_color(0x4472C4); return c < 0 ? 0xFFFFFFFFUL : FILL_ON | (ULONG)c; }
  if ((ULONG)v == COLOR_AUTO) return 0;
  return FILL_ON | ((ULONG)v & 0xFFFFFF);
}
void chart_command(ULONG id)
{
  LONG v = (LONG)xget(ribbon, MUIA_Ribbon_Value);
  DObj *o = sel_obj();
  Chart *c = sel_chart();
  ObjView *ov = OV();
  if (id >= ID_X_SHAPE0 && id <= ID_X_SHAPE6) { place_shape((int)(id - ID_X_SHAPE0)); return; }
  if (id >= ID_X_KIND0 && id <= ID_X_KINDN) { insert_chart((int)(id - ID_X_KIND0)); return; }
  if (id >= ID_X_TYPE0 && id <= ID_X_TYPEN) { change_type((int)(id - ID_X_TYPE0)); return; }
  if (id >= ID_X_LAYOUT1 && id <= ID_X_LAYOUT6) { if (c) { begin(); chart_quick_layout(c, (int)(id - ID_X_LAYOUT1 + 1)); end(); } return; }
  if (id >= ID_X_COLOR0 && id <= ID_X_COLOR7) { if (c) { begin(); chart_colors(c, (int)(id - ID_X_COLOR0)); end(); } return; }
  if (id >= ID_X_ALIGN0 && id <= ID_X_ALIGN5) { align_objects((int)(id - ID_X_ALIGN0)); return; }
  if (id >= ID_X_STYLE1 && id <= ID_X_STYLE8) { if (c) { begin(); chart_style(c, (int)(id - ID_X_STYLE1 + 1)); end(); } return; }
  switch (id) {
  /* Insert */
  case ID_X_PICTURE: insert_picture(); break;
  case ID_X_SHAPEDD: place_shape((int)v); break;
  case ID_X_ICONDD: insert_icon((int)v); break;
  case ID_X_TEXTBOX: place_shape(6); break;
  case ID_X_RECCHART: dlg_insert_chart(1); break;
  case ID_X_ALLCHARTS: dlg_insert_chart(0); break;
  case ID_X_COLBARDD: insert_chart(CK_COL + (int)v); break;
  case ID_X_LINEAREADD: { static const int k[6] = { CK_LINE, CK_LINEMARK, CK_LINESTACK, CK_AREA, CK_AREASTACK, CK_AREA100 }; if (v >= 0 && v < 6) insert_chart(k[v]); break; }
  case ID_X_PIEDD: insert_chart(v ? CK_DOUGHNUT : CK_PIE); break;
  case ID_X_SCATTERDD: insert_chart(v == 2 ? CK_BUBBLE : v == 1 ? CK_SCATTERLINE : CK_SCATTER); break;
  case ID_X_STATDD: insert_chart(v ? CK_BOXWHISKER : CK_HISTOGRAM); break;
  case ID_X_MOREDD: { static const int k[7] = { CK_TREEMAP, CK_SUNBURST, CK_WATERFALL, CK_FUNNEL, CK_RADAR, CK_STOCK, CK_COMBO }; if (v >= 0 && v < 7) insert_chart(k[v]); break; }
  case ID_X_SPARKLINE: dlg_sparklines(SP_LINE, NULL); break;
  case ID_X_SPARKCOL: dlg_sparklines(SP_COLUMN, NULL); break;
  case ID_X_SPARKWIN: dlg_sparklines(SP_WINLOSS, NULL); break;
  /* Draw */
  case ID_X_LASSO: ov_tool(ov, ov_tool_now(ov, NULL, NULL) == TOOL_LASSO ? TOOL_SELECT : TOOL_LASSO, 0, 0, 0); refresh_all(); break;
  case ID_X_PENBLUE: pen(0x1F5FC9, 200); break;
  case ID_X_PENBLACK: pen(0x000000, 200); break;
  case ID_X_PENRED: pen(0xD13B2F, 200); break;
  case ID_X_HIGHLIGHT: pen(0xF2E33A, 1200); break;
  case ID_X_ERASER: ov_tool(ov, ov_tool_now(ov, NULL, NULL) == TOOL_ERASER ? TOOL_SELECT : TOOL_ERASER, 0, 0, 0); refresh_all(); break;
  case ID_X_INKSHAPE: ink_to_shape(); break;
  /* Chart Design */
  case ID_X_ADDELEMDD: add_element((int)v); break;
  case ID_X_LAYOUTDD: if (c) { begin(); chart_quick_layout(c, (int)v + 1); end(); } break;
  case ID_X_COLORSDD: if (c) { begin(); chart_colors(c, (int)v); end(); } break;
  case ID_X_STYLEDD: if (c) { begin(); chart_style(c, (int)v + 1); end(); } break;
  case ID_X_SWITCHRC:
    if (c && c->hassrc && c->srcsheet < book->nsh) { begin(); chart_set_source(book, c, c->srcsheet, c->src, !c->byrows); end(); }
    else if (c) ask("OK", "%s", tr("This chart's series weren't made from one range, so they can't be switched round.\nUse Select Data to give it a range."));
    break;
  case ID_X_SELECTDATA: dlg_select_data(); break;
  case ID_X_TYPEDD: change_type((int)v); break;
  case ID_X_MOVECHART: dlg_move_chart(); break;
  case ID_X_CHARTTITLE: chart_title(); break;
  case ID_X_LEGENDNONE: case ID_X_LEGENDB: case ID_X_LEGENDT: case ID_X_LEGENDL: case ID_X_LEGENDR:
    if (c) { static const u8 lg[5] = { LG_NONE, LG_BOTTOM, LG_TOP, LG_LEFT, LG_RIGHT }; begin(); c->legend = lg[id - ID_X_LEGENDNONE]; end(); }
    break;
  case ID_X_LABELS: if (c) { begin(); c->labels = !c->labels; end(); } break;
  case ID_X_GRIDLINES: if (c) { begin(); c->hgrid = !c->hgrid; end(); } break;
  case ID_X_AXISTITLES: add_element(c && c->xtitle ? 3 : 2); break;
  case ID_X_DELOBJ: {
    int k = ov_selected(ov);
    if (k < 0) break;
    begin();
    drw_remove(cur_sheet(), k);
    ov_select(ov, -1);
    end();
    break; }
  /* Format */
  case ID_X_ELEMDD: ov_set_elem(ov, elem_of((int)v)); refresh_all(); break;
  case ID_X_FORMATSEL: dlg_format(); break;
  case ID_X_FILLDD: case ID_X_OUTLINEDD: {
    ULONG col = colour_choice(v);
    if (!o || col == 0xFFFFFFFFUL) break;
    begin();
    o = sel_obj(); c = sel_chart();
    if (id == ID_X_FILLDD && (ULONG)v == COLOR_AUTO && !c) col = FILL_NONE;      /* (No Fill) */
    apply_colour(c, o, ov_elem(ov), col, id == ID_X_OUTLINEDD);
    end();
    break; }
  case ID_X_FORWARD: case ID_X_BACKWARD: {
    int k = ov_selected(ov), to = id == ID_X_FORWARD ? k + 1 : k - 1;
    if (k < 0 || to < 0 || to >= drw_count(cur_sheet())) break;
    begin();
    drw_move_z(cur_sheet(), k, to);
    ov_select(ov, to);
    end();
    break; }
  case ID_X_EDITTEXT: edit_text(); break;
  case ID_X_BACKGROUND: background(); break;
  case ID_X_SELPANE: dlg_selection_pane(); break;
  case ID_X_ALIGNDD: align_objects((int)v); break;
  case ID_X_ROTATE: rotate_shape(); break;
  /* Sparkline */
  case ID_X_SPTLINE: case ID_X_SPTCOL: case ID_X_SPTWIN: case ID_X_SPHIGH: case ID_X_SPLOW: case ID_X_SPNEG: case ID_X_SPFIRST: case ID_X_SPLAST: case ID_X_SPMARK:
    spark_set(id);
    break;
  case ID_X_SPCOLORDD: case ID_X_SPMARKDD: {
    ULONG col = colour_choice(v);
    SparkGroup *g;
    if (col == 0xFFFFFFFFUL || !cur_spark()) break;
    begin();
    if ((g = cur_spark())) {
      if (id == ID_X_SPCOLORDD) g->color = col ? col : FILL_ON | 0x376092;
      else { ULONG m = col ? col : FILL_ON | 0xD00000; g->markcolor = g->highcolor = g->lowcolor = g->firstcolor = g->lastcolor = g->negcolor = m; }
    }
    end();
    break; }
  case ID_X_SPAXISDD: {
    SparkGroup *g = cur_spark();
    char t[40] = "";
    double val = 0;
    if (!g) break;
    if (v == 4 || v == 5) {
      if (!ask_string(v == 4 ? "Sparkline Axis" : "Sparkline Axis", v == 4 ? "Lowest value shown:" : "Highest value shown:", t, sizeof t)) break;
      val = atof(t);
    }
    begin();
    if ((g = cur_spark())) {
      if (v == 0) g->axis = !g->axis;
      else if (v == 1) g->minmode = 1;
      else if (v == 2) g->maxmode = 1;
      else if (v == 3) g->minmode = g->maxmode = 0;
      else if (v == 4) { g->minmode = 2; g->minv = val; }
      else if (v == 5) { g->maxmode = 2; g->maxv = val; }
    }
    end();
    break; }
  case ID_X_SPCLEARDD: case ID_X_SPCLEAR:
    book_clear_sparklines(book, cur_index(), cur_sel(), id == ID_X_SPCLEARDD && v == 1);
    refresh_all();
    break;
  case ID_X_SPEDIT: { SparkGroup *g = cur_spark(); if (g) dlg_sparklines(g->type, g); break; }
  }
}
