/*
 * formctl.c - form controls (GridCraft #481): Excel's spin button and scroll bar, floating objects
 * (drawing.h, OB_CONTROL) that set a number in their linked cell. Their behaviour, drawing, and how
 * .xlsx keeps them, as Excel writes them and GridCraft reads them:
 *
 *   - a control properties part xl/ctrlProps/ctrlPropN.xml: <formControlPr objectType="Spin"
 *     fmlaLink="$B$1" min max inc page val/>, through the sheet's ctrlProp relationship;
 *   - a <control shapeId r:id name> in the sheet's <controls> (inside mc:AlternateContent, x14), with
 *     its <controlPr><anchor> from and to;
 *   - a VML shape with x:ClientData ObjectType="Spin" in the sheet's VML drawing (shared with notes),
 *     for older readers.
 *
 * Reading takes the properties part and the anchor; Excel's own mirror of a control in the drawing
 * part (an sp with a14:compatExt) is passed over there (xlsx_draw.c). Buttons are left out: they run
 * macros, and Ledger has no scripting. Portable C99.
 */
#include "drawing.h"
#include "data.h"
#include "xml.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

static i32 clampi(i32 v, i32 lo, i32 hi) { return v < lo ? lo : v > hi ? hi : v; }

int book_control_add(Book *b, int sheet, int ctl, i32 r, i32 c)
{
  Sheet *s;
  DObj *o;
  i32 x, y;
  int idx = -1;
  if (sheet < 0 || sheet >= b->nsh || (ctl != CT_SPIN && ctl != CT_SCROLL)) return -1;
  s = b->sh[sheet];
  if (s->protect && !(s->protallow & PA_OBJECTS)) return -1;
  drw_begin(b, sheet);
  if ((o = drw_new(s, OB_CONTROL))) {
    o->ctl = (u8)ctl;
    o->cmin = 0; o->cmax = 100; o->cinc = 1; o->cpage = 10; o->cval = 0;
    o->anchor = AN_ONECELL;
    snprintf(o->name, sizeof o->name, "%s %lu", ctl == CT_SPIN ? "Spinner" : "Scroll Bar", (unsigned long)o->id);
    x = sheet_col_px(s, c); y = sheet_row_px(s, r);
    /* Excel's: a spin button 1 x 2 rows' worth (about 16 x 32 pixels), a scroll bar 16 x 100 */
    if (ctl == CT_SPIN) dobj_place(s, o, x + 2, y + 2, x + 20, y + 40);
    else dobj_place(s, o, x + 2, y + 2, x + 20, y + 102);
    idx = drw_count(s) - 1;
  }
  drw_end(b);
  return idx;
}

/* the cell a link points at: 0 if none */
static int link_cell(Book *b, int sheet, const DObj *o, u16 *ls, i32 *r, i32 *c)
{
  char t[300];
  Buf f;
  u16 s2; Rect a; u8 fl;
  int n;
  if (!o->link) return 0;
  buf_init(&f);
  formula_text(b, sheet, o->link, &f);
  snprintf(t, sizeof t, "%s", f.data ? f.data : "");
  buf_free(&f);
  n = parse_ref(b, t, &s2, &a, &fl);
  if (!n || t[n]) return 0;
  *ls = s2 == SHEET_SELF ? (u16)sheet : s2;
  if (*ls >= b->nsh) return 0;
  *r = a.r1; *c = a.c1;
  return 1;
}
void control_link_text(Book *b, int sheet, const DObj *o, char *out, size_t cap)
{
  Buf f;
  out[0] = 0;
  if (!o->link) return;
  buf_init(&f);
  formula_text(b, sheet, o->link, &f);
  snprintf(out, cap, "%s", f.data ? f.data : "");
  buf_free(&f);
  {
    /* its own sheet's name left out, as Excel writes the link ("$B$1") */
    char *bang = strrchr(out, '!'), nm[80];
    size_t n;
    if (!bang) return;
    n = (size_t)(bang - out);
    if (!n) return;
    if (n >= 2 && out[0] == '\'' && out[n - 1] == '\'') { size_t k, o2 = 0; for (k = 1; k + 1 < n && o2 + 1 < sizeof nm; k++) { if (out[k] == '\'' && out[k + 1] == '\'') k++; nm[o2++] = out[k]; } nm[o2] = 0; }
    else snprintf(nm, sizeof nm, "%.*s", (int)(n < 79 ? n : 79), out);
    if (str_ieq(nm, b->sh[sheet]->name)) memmove(out, bang + 1, strlen(bang + 1) + 1);
  }
}
i32 control_value(Book *b, int sheet, const DObj *o)
{
  u16 ls; i32 r, c;
  if (link_cell(b, sheet, o, &ls, &r, &c)) {
    const Value *v = cell_value(b, ls, r, c);
    if (v->t == V_NUM && v->n >= o->cmin && v->n <= o->cmax) return (i32)(v->n + 0.5);
  }
  return clampi(o->cval, o->cmin, o->cmax);
}
/* the linked cell given the value */
static void put_link(Book *b, int sheet, DObj *o)
{
  u16 ls; i32 r, c;
  if (link_cell(b, sheet, o, &ls, &r, &c)) { Value v = val_num(o->cval); book_set_value(b, ls, r, c, &v); }
}
int book_control_set(Book *b, int sheet, int i, const char *link, i32 val, i32 min, i32 max, i32 inc, i32 page, const char **why)
{
  DObj *o;
  Formula *f = NULL;
  *why = NULL;
  if (sheet < 0 || sheet >= b->nsh || !(o = drw_obj(b->sh[sheet], i)) || o->kind != OB_CONTROL) { *why = "That isn't a form control."; return 0; }
  if (min < 0 || max > CTL_MAX || min > CTL_MAX || max < 0) { *why = "Enter values between 0 and 30000."; return 0; }
  if (min > max) { *why = "The minimum value must be less than or equal to the maximum."; return 0; }
  if (inc < 1 || inc > CTL_MAX || page < 1 || page > CTL_MAX) { *why = "The incremental and page changes must be between 1 and 30000."; return 0; }
  if (link && *link) {
    u16 s2; Rect a; u8 fl;
    const char *t = link[0] == '=' ? link + 1 : link;
    int n = parse_ref(b, t, &s2, &a, &fl);
    if (!n || t[n] || a.r1 != a.r2 || a.c1 != a.c2 || !(f = formula_parse(b, sheet, t, NULL))) { *why = "The cell link must be one cell, as $B$1."; return 0; }
  }
  drw_begin(b, sheet);
  o = drw_obj(b->sh[sheet], i);
  formula_free(o->link);
  o->link = f;
  o->cmin = min; o->cmax = max; o->cinc = inc; o->cpage = page;
  o->cval = clampi(val, min, max);
  put_link(b, sheet, o);
  drw_end(b);
  return 1;
}
int control_horizontal(const DObj *o, i32 w, i32 h) { (void)o; return w > h; }
int control_part(const DObj *o, i32 w, i32 h, i32 x, i32 y, i32 val)
{
  int across = control_horizontal(o, w, h);
  i32 len = across ? w : h, thick = across ? h : w, at = across ? x : y, t0, t1, room;
  if (x < 0 || y < 0 || x >= w || y >= h || len < 4) return CP_NONE;
  if (o->ctl == CT_SPIN) return across ? (at < len / 2 ? CP_DOWN : CP_UP) : (at < len / 2 ? CP_UP : CP_DOWN);
  /* a scroll bar: arrows at the ends (square), the track between with its thumb */
  if (thick * 2 + 4 > len) thick = (len - 4) / 2;
  if (at < thick) return CP_DOWN;
  if (at >= len - thick) return CP_UP;
  room = len - 3 * thick;
  t0 = thick + (o->cmax > o->cmin && room > 0 ? (i32)((long)room * (val - o->cmin) / (o->cmax - o->cmin)) : 0);
  t1 = t0 + thick;
  return at < t0 ? CP_PAGEDOWN : at >= t1 ? CP_PAGEUP : CP_THUMB;
}
int book_control_step(Book *b, int sheet, int i, int part)
{
  DObj *o;
  i32 v, nv;
  if (sheet < 0 || sheet >= b->nsh || !(o = drw_obj(b->sh[sheet], i)) || o->kind != OB_CONTROL) return 0;
  v = control_value(b, sheet, o);
  switch (part) {
  case CP_DOWN: nv = v - o->cinc; break;
  case CP_UP: nv = v + o->cinc; break;
  case CP_PAGEDOWN: nv = v - (o->ctl == CT_SCROLL ? o->cpage : o->cinc); break;
  case CP_PAGEUP: nv = v + (o->ctl == CT_SCROLL ? o->cpage : o->cinc); break;
  default: return 0;
  }
  nv = clampi(nv, o->cmin, o->cmax);
  if (nv == v && nv == o->cval) return 0;
  drw_begin(b, sheet);
  o = drw_obj(b->sh[sheet], i);
  o->cval = nv;
  put_link(b, sheet, o);
  drw_end(b);
  return 1;
}

/* ------------------------------------------------------------------ drawing */
#define FACE (FILL_ON | 0xE4E4E4)
#define EDGE (FILL_ON | 0x7A7A7A)
#define MARK (FILL_ON | 0x303030)
#define TRACK (FILL_ON | 0xF4F4F4)
static void box(PrimList *l, i32 x, i32 y, i32 w, i32 h, u32 fill)
{
  Prim *p = prim_add(l, PR_RECT, CE_NONE);
  if (p) { p->x = x; p->y = y; p->w = w; p->h = h; p->fill = fill; p->line = EDGE; }
}
/* a small triangle pointing dir (0 up, 1 down, 2 left, 3 right) in the box */
static void arrow(PrimList *l, i32 x, i32 y, i32 w, i32 h, int dir)
{
  Prim *p = prim_add(l, PR_POLY, CE_NONE);
  i32 cx = x + w / 2, cy = y + h / 2, r = (w < h ? w : h) / 4;
  if (!p) return;
  if (r < 2) r = 2;
  if (!(p->pts = (i32 *)malloc(6 * sizeof(i32)))) { l->oom = 1; return; }
  p->np = 3; p->fill = MARK; p->line = MARK;
  switch (dir) {
  case 0: p->pts[0] = cx - r; p->pts[1] = cy + r / 2; p->pts[2] = cx + r; p->pts[3] = cy + r / 2; p->pts[4] = cx; p->pts[5] = cy - r / 2 - 1; break;
  case 1: p->pts[0] = cx - r; p->pts[1] = cy - r / 2; p->pts[2] = cx + r; p->pts[3] = cy - r / 2; p->pts[4] = cx; p->pts[5] = cy + r / 2 + 1; break;
  case 2: p->pts[0] = cx + r / 2; p->pts[1] = cy - r; p->pts[2] = cx + r / 2; p->pts[3] = cy + r; p->pts[4] = cx - r / 2 - 1; p->pts[5] = cy; break;
  default: p->pts[0] = cx - r / 2; p->pts[1] = cy - r; p->pts[2] = cx - r / 2; p->pts[3] = cy + r; p->pts[4] = cx + r / 2 + 1; p->pts[5] = cy; break;
  }
}
int control_render(Book *b, int sheet, const DObj *o, i32 w, i32 h, PrimList *out)
{
  int across = control_horizontal(o, w, h);
  i32 val = control_value(b, sheet, o);
  if (w < 4 || h < 4) return 0;
  if (o->ctl == CT_SPIN) {
    if (across) { box(out, 0, 0, w / 2, h, FACE); box(out, w / 2, 0, w - w / 2, h, FACE); arrow(out, 0, 0, w / 2, h, 2); arrow(out, w / 2, 0, w - w / 2, h, 3); }
    else { box(out, 0, 0, w, h / 2, FACE); box(out, 0, h / 2, w, h - h / 2, FACE); arrow(out, 0, 0, w, h / 2, 0); arrow(out, 0, h / 2, w, h - h / 2, 1); }
  } else {
    i32 len = across ? w : h, thick = across ? h : w, room, t0;
    if (thick * 2 + 4 > len) thick = (len - 4) / 2;
    room = len - 3 * thick;
    t0 = thick + (o->cmax > o->cmin && room > 0 ? (i32)((long)room * (val - o->cmin) / (o->cmax - o->cmin)) : 0);
    box(out, 0, 0, w, h, TRACK);
    if (across) {
      box(out, 0, 0, thick, h, FACE); arrow(out, 0, 0, thick, h, 2);
      box(out, w - thick, 0, thick, h, FACE); arrow(out, w - thick, 0, thick, h, 3);
      box(out, t0, 0, thick, h, FACE);
    } else {
      box(out, 0, 0, w, thick, FACE); arrow(out, 0, 0, w, thick, 0);
      box(out, 0, h - thick, w, thick, FACE); arrow(out, 0, h - thick, w, thick, 1);
      box(out, 0, t0, w, thick, FACE);
    }
  }
  return !out->oom;
}

/* ------------------------------------------------------------------ .xlsx */
int fc_count(Sheet *s)
{
  int i, n = 0;
  for (i = 0; i < drw_count(s); i++) n += drw_obj(s, i)->kind == OB_CONTROL;
  return n;
}
/* the number of the first ctrlProp part of sheet si (they are numbered through the book) */
static int first_prop(Book *b, int si) { int i, n = 0; for (i = 0; i < si && i < b->nsh; i++) n += fc_count(b->sh[i]); return n + 1; }
static void xesc(Buf *o, const char *s) { xml_escape(o, s ? s : "", s ? strlen(s) : 0); }
static void link_attr(Book *b, int si, const DObj *o, Buf *out, const char *pre, const char *post)
{
  char t[300];
  control_link_text(b, si, o, t, sizeof t);
  if (!t[0]) return;
  buf_adds(out, pre); xesc(out, t); buf_adds(out, post);
}
void fc_xlsx_types(Book *b, Buf *o)
{
  int i, k, n = 0;
  for (i = 0; i < b->nsh; i++) for (k = fc_count(b->sh[i]); k > 0; k--)
    buf_printf(o, "<Override PartName=\"/xl/ctrlProps/ctrlProp%d.xml\" ContentType=\"application/vnd.ms-excel.controlproperties+xml\"/>", ++n);
}
void fc_xlsx_rels(Book *b, int si, Buf *o)
{
  int k, n = fc_count(b->sh[si]), p = first_prop(b, si);
  for (k = 0; k < n; k++)
    buf_printf(o, "<Relationship Id=\"rIdC%d\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/ctrlProp\" Target=\"../ctrlProps/ctrlProp%d.xml\"/>", k + 1, p + k);
}
static void marker(Buf *o, const char *tag, const DCorner *c)
{
  buf_printf(o, "<%s><xdr:col>%ld</xdr:col><xdr:colOff>%ld</xdr:colOff><xdr:row>%ld</xdr:row><xdr:rowOff>%ld</xdr:rowOff></%s>",
             tag, (long)c->c, (long)c->dx * 9525L, (long)c->r, (long)c->dy * 9525L, tag);
}
/* shape ids: after the notes' (xlsx.c numbers those 1024 * (sheet + 1) + note + 1) */
static long shape_id(Sheet *s, int si, int k) { return 1024L * (si + 1) + s->nnotes + k + 1; }
void fc_xlsx_controls(Book *b, int si, Buf *o)
{
  Sheet *s = b->sh[si];
  int i, k = 0;
  if (!fc_count(s)) return;
  buf_adds(o, "<mc:AlternateContent xmlns:mc=\"http://schemas.openxmlformats.org/markup-compatibility/2006\"><mc:Choice xmlns:x14=\"http://schemas.microsoft.com/office/spreadsheetml/2009/9/main\" Requires=\"x14\"><controls>");
  for (i = 0; i < drw_count(s); i++) {
    DObj *ob = drw_obj(s, i);
    long id;
    if (ob->kind != OB_CONTROL) continue;
    id = shape_id(s, si, k);
    buf_adds(o, "<mc:AlternateContent xmlns:mc=\"http://schemas.openxmlformats.org/markup-compatibility/2006\"><mc:Choice xmlns:x14=\"http://schemas.microsoft.com/office/spreadsheetml/2009/9/main\" Requires=\"x14\">");
    buf_printf(o, "<control shapeId=\"%ld\" r:id=\"rIdC%d\" name=\"", id, k + 1); xesc(o, ob->name);
    buf_printf(o, "\"><controlPr defaultSize=\"0\" autoPict=\"0\"><anchor%s xmlns:xdr=\"http://schemas.openxmlformats.org/drawingml/2006/spreadsheetDrawing\">",
               ob->anchor == AN_TWOCELL ? " moveWithCells=\"1\" sizeWithCells=\"1\"" : ob->anchor == AN_ONECELL ? " moveWithCells=\"1\"" : "");
    marker(o, "from", &ob->from); marker(o, "to", &ob->to);
    buf_printf(o, "</anchor></controlPr></control></mc:Choice><mc:Fallback><control shapeId=\"%ld\" r:id=\"rIdC%d\" name=\"", id, k + 1); xesc(o, ob->name);
    buf_adds(o, "\"/></mc:Fallback></mc:AlternateContent>");
    k++;
  }
  buf_adds(o, "</controls></mc:Choice></mc:AlternateContent>");
}
void fc_xlsx_vml(Book *b, int si, Buf *o)
{
  Sheet *s = b->sh[si];
  int i, k = 0;
  if (!fc_count(s)) return;
  buf_adds(o, "<v:shapetype id=\"_x0000_t201\" coordsize=\"21600,21600\" o:spt=\"201\" path=\"m,l,21600r21600,l21600,xe\"><v:stroke joinstyle=\"miter\"/>"
              "<v:path shadowok=\"f\" o:extrusionok=\"f\" strokeok=\"f\" fillok=\"f\" o:connecttype=\"rect\"/><o:lock v:ext=\"edit\" shapetype=\"t\"/></v:shapetype>");
  for (i = 0; i < drw_count(s); i++) {
    DObj *ob = drw_obj(s, i);
    i32 x0, y0, x1, y1;
    if (ob->kind != OB_CONTROL) continue;
    dobj_box(s, ob, &x0, &y0, &x1, &y1);
    buf_printf(o, "<v:shape id=\"_x0000_s%ld\" type=\"#_x0000_t201\" style=\"position:absolute;margin-left:%.2fpt;margin-top:%.2fpt;width:%.2fpt;height:%.2fpt;z-index:%d\" "
                  "stroked=\"f\" filled=\"f\" o:insetmode=\"auto\"><o:lock v:ext=\"edit\" rotation=\"t\"/><x:ClientData ObjectType=\"%s\">",
               shape_id(s, si, k), x0 * 0.75, y0 * 0.75, (x1 - x0) * 0.75, (y1 - y0) * 0.75, k + 1, ob->ctl == CT_SPIN ? "Spin" : "Scroll");
    if (ob->anchor == AN_ABSOLUTE) buf_adds(o, "<x:MoveWithCells/>");
    if (ob->anchor != AN_TWOCELL) buf_adds(o, "<x:SizeWithCells/>");
    buf_printf(o, "<x:Anchor>%ld, %ld, %ld, %ld, %ld, %ld, %ld, %ld</x:Anchor><x:AutoFill>False</x:AutoFill>",
               (long)ob->from.c, (long)ob->from.dx, (long)ob->from.r, (long)ob->from.dy, (long)ob->to.c, (long)ob->to.dx, (long)ob->to.r, (long)ob->to.dy);
    link_attr(b, si, ob, o, "<x:FmlaLink>", "</x:FmlaLink>");
    buf_printf(o, "<x:Val>%ld</x:Val><x:Min>%ld</x:Min><x:Max>%ld</x:Max><x:Inc>%ld</x:Inc>", (long)control_value(b, si, ob), (long)ob->cmin, (long)ob->cmax, (long)ob->cinc);
    if (ob->ctl == CT_SCROLL) {
      buf_printf(o, "<x:Page>%ld</x:Page>", (long)ob->cpage);
      if (control_horizontal(ob, x1 - x0, y1 - y0)) buf_adds(o, "<x:Horiz/>");
    }
    buf_adds(o, "<x:Dx>22</x:Dx></x:ClientData></v:shape>");
    k++;
  }
}
/* the properties part of control k of sheet si (k counts controls only); NULL past the last */
char *fc_xlsx_prop(Book *b, int si, int k, int *number)
{
  Sheet *s = b->sh[si];
  int i, j = 0;
  for (i = 0; i < drw_count(s); i++) {
    DObj *ob = drw_obj(s, i);
    i32 x0, y0, x1, y1;
    Buf o;
    if (ob->kind != OB_CONTROL || j++ != k) continue;
    dobj_box(s, ob, &x0, &y0, &x1, &y1);
    buf_init(&o);
    buf_printf(&o, "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n<formControlPr xmlns=\"http://schemas.microsoft.com/office/spreadsheetml/2009/9/main\" objectType=\"%s\" dx=\"22\"",
               ob->ctl == CT_SPIN ? "Spin" : "Scroll");
    link_attr(b, si, ob, &o, " fmlaLink=\"", "\"");
    if (ob->ctl == CT_SCROLL && control_horizontal(ob, x1 - x0, y1 - y0)) buf_adds(&o, " horiz=\"1\"");
    buf_printf(&o, " inc=\"%ld\" max=\"%ld\" min=\"%ld\"", (long)ob->cinc, (long)ob->cmax, (long)ob->cmin);
    if (ob->ctl == CT_SCROLL) buf_printf(&o, " page=\"%ld\"", (long)ob->cpage);
    buf_printf(&o, " val=\"%ld\"/>", (long)control_value(b, si, ob));
    *number = first_prop(b, si) + k;
    return buf_take(&o);
  }
  return NULL;
}

/* reading: the <controls> element (at its START); each control made with its anchor, its rid kept in
   descr ("\001rId3") until its properties part is read (fc_xlsx_read_prop) */
void fc_xlsx_read_controls(Book *b, int sheet, Xml *x)
{
  Sheet *s = b->sh[sheet];
  int d0 = x->depth, part = 0, field = -1;
  long seen[256];
  int nseen = 0, k;
  DObj *cur = NULL;
  DCorner *cn = NULL;
  if (x->pending_end) return;
  while (xml_next(x) != XT_EOF) {
    if (x->type == XT_END) {
      if (x->depth < d0) break;
      if (!strcmp(x->name, "control")) cur = NULL;
      if (!strcmp(x->name, "from") || !strcmp(x->name, "to")) cn = NULL;
      field = -1;
      continue;
    }
    if (x->type == XT_TEXT) {
      if (cn && field >= 0) {
        long v = atol(x->text.data ? x->text.data : "0");
        if (field == 0) cn->c = (i32)(v >= 0 && v < MAXCOL ? v : 0);
        else if (field == 1) cn->dx = (i32)(v / 9525);
        else if (field == 2) cn->r = (i32)(v >= 0 && v < MAXROW ? v : 0);
        else cn->dy = (i32)(v / 9525);
      }
      continue;
    }
    if (x->type != XT_START) continue;
    if (!strcmp(x->name, "control")) {
      const char *id = xml_attr(x, "shapeId"), *rid = xml_attr(x, "id"), *nm = xml_attr(x, "name");
      long sid = id ? atol(id) : -1;
      cur = NULL;
      for (k = 0; k < nseen && seen[k] != sid; k++) ;
      if (k < nseen || !rid || drw_count(s) > 10000) continue;       /* (the Fallback's copy) */
      if (nseen < 256) seen[nseen++] = sid;
      if (!(cur = drw_new(s, OB_CONTROL))) continue;
      cur->ctl = CT_SPIN; cur->cmax = 100; cur->cinc = 1; cur->cpage = 10;
      cur->anchor = AN_ONECELL;
      cur->to.r = cur->from.r + 2; cur->to.c = cur->from.c;
      if (nm && *nm) snprintf(cur->name, sizeof cur->name, "%s", nm);
      { size_t n = strlen(rid); if ((cur->descr = (char *)malloc(n + 2))) { cur->descr[0] = '\001'; memcpy(cur->descr + 1, rid, n + 1); } }
      part++;
    } else if (cur && !strcmp(x->name, "anchor")) {
      const char *mv = xml_attr(x, "moveWithCells"), *sz = xml_attr(x, "sizeWithCells");
      cur->anchor = sz && atoi(sz) ? AN_TWOCELL : mv && atoi(mv) ? AN_ONECELL : AN_ABSOLUTE;
    } else if (cur && (!strcmp(x->name, "from") || !strcmp(x->name, "to"))) {
      cn = x->name[0] == 'f' ? &cur->from : &cur->to;
    } else if (cn) {
      field = !strcmp(x->name, "col") ? 0 : !strcmp(x->name, "colOff") ? 1 : !strcmp(x->name, "row") ? 2 : !strcmp(x->name, "rowOff") ? 3 : -1;
    }
  }
  (void)part;
}
static i32 attr_i(Xml *x, const char *name, i32 dflt)
{
  const char *v = xml_attr(x, name);
  long n;
  if (!v) return dflt;
  n = atol(v);
  return (i32)(n < 0 ? 0 : n > CTL_MAX ? CTL_MAX : n);
}
/* a properties part read for the control whose relationship is rid; a kind Ledger doesn't have
   (a button, a check box, a list) takes the control away */
void fc_xlsx_read_prop(Book *b, int sheet, const char *rid, const char *data, size_t len)
{
  Sheet *s = b->sh[sheet];
  int i;
  Xml x;
  for (i = 0; i < drw_count(s); i++) {
    DObj *o = drw_obj(s, i);
    if (o->kind != OB_CONTROL || !o->descr || o->descr[0] != '\001' || strcmp(o->descr + 1, rid)) continue;
    free(o->descr); o->descr = NULL;
    xml_init(&x, data, len);
    while (xml_next(&x) != XT_EOF) {
      const char *t, *l;
      if (x.type != XT_START || strcmp(x.name, "formControlPr")) continue;
      t = xml_attr(&x, "objectType");
      if (!t || (str_ncasecmp(t, "Spin", 5) && str_ncasecmp(t, "Scroll", 7))) { o->kind = 0xFF; break; }
      o->ctl = (u8)(str_ncasecmp(t, "Spin", 5) ? CT_SCROLL : CT_SPIN);
      o->cmin = attr_i(&x, "min", 0); o->cmax = attr_i(&x, "max", 100);
      if (o->cmax < o->cmin) o->cmax = o->cmin;
      o->cinc = attr_i(&x, "inc", 1); if (o->cinc < 1) o->cinc = 1;
      o->cpage = attr_i(&x, "page", 10); if (o->cpage < 1) o->cpage = 1;
      o->cval = clampi(attr_i(&x, "val", 0), o->cmin, o->cmax);
      if ((l = xml_attr(&x, "fmlaLink")) && *l) o->link = formula_parse(b, sheet, l[0] == '=' ? l + 1 : l, NULL);
      break;
    }
    xml_free(&x);
    if (o->kind == 0xFF) drw_remove(s, i);
    return;
  }
}
/* after the relationships: controls whose properties never came are taken away */
void fc_xlsx_read_done(Book *b, int sheet)
{
  Sheet *s = b->sh[sheet];
  int i;
  for (i = drw_count(s) - 1; i >= 0; i--) {
    DObj *o = drw_obj(s, i);
    if (o->kind == OB_CONTROL && o->descr && o->descr[0] == '\001') drw_remove(s, i);
  }
}
