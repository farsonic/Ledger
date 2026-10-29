/*
 * drawtest.c - (included by coretest.c) charts, pictures, shapes and sparklines: the model, the
 * layout every chart kind gets, editing with undo, and the round trip through .xlsx.
 */
#include "../core/drawing.h"
#include "../core/image.h"
#include "../core/zip.h"

static void dput(Book *b, int sh, const char *ref, const char *text)
{
  u16 s; Rect r; u8 f;
  parse_ref(b, ref, &s, &r, &f);
  book_input(b, sh, r.r1, r.c1, text, NULL);
}
/* a small table: months down A2:A7, three series B..D with a heading row */
static Book *sales_book(void)
{
  Book *b = book_new();
  static const char *const m[6] = { "Jan", "Feb", "Mar", "Apr", "May", "Jun" };
  int i;
  char ref[16], v[16];
  dput(b, 0, "B1", "North"); dput(b, 0, "C1", "South"); dput(b, 0, "D1", "West");
  for (i = 0; i < 6; i++) {
    sprintf(ref, "A%d", i + 2); dput(b, 0, ref, m[i]);
    sprintf(ref, "B%d", i + 2); sprintf(v, "%d", 10 + i * 3); dput(b, 0, ref, v);
    sprintf(ref, "C%d", i + 2); sprintf(v, "%d", 20 - i * 2); dput(b, 0, ref, v);
    sprintf(ref, "D%d", i + 2); sprintf(v, "%d", 5 + (i % 3) * 4); dput(b, 0, ref, v);
  }
  book_forget_undo(b);
  return b;
}
static Rect R4(i32 r1, i32 c1, i32 r2, i32 c2) { Rect r; r.r1 = r1; r.c1 = c1; r.r2 = r2; r.c2 = c2; return r; }
static int ftext_is(Book *b, const Formula *f, const char *want)
{
  Buf t;
  int r;
  buf_init(&t);
  if (f) formula_text(b, -1, f, &t);
  r = !strcmp(t.data ? t.data : "", want);
  if (!r) printf("   formula \"%s\" (want \"%s\")\n", t.data ? t.data : "", want);
  buf_free(&t);
  return r;
}
static DObj *add_chart(Book *b, int sheet, int kind, Rect src)
{
  Chart *c = chart_from_range(b, sheet, src, kind, -1);
  DObj *o;
  if (!c) return NULL;
  drw_begin(b, sheet);
  o = drw_new(b->sh[sheet], OB_CHART);
  if (!o) { chart_free(c); drw_end(b); return NULL; }
  o->chart = c;
  o->from.r = 8; o->from.c = 1; o->to.r = 22; o->to.c = 8;
  drw_end(b);
  return o;
}
/* a 4x3 PNG (red, green, blue, white stripes) made with zip.c's deflate */
static u8 *make_png(u32 *len)
{
  static const u8 sig[8] = { 137, 80, 78, 71, 13, 10, 26, 10 };
  u8 raw[3 * (1 + 4 * 3)], *z, *out;
  size_t zl;
  u32 a = 1, bsum = 0, crc, n = 0;
  int y, x, i;
  Buf o;
  for (y = 0; y < 3; y++) {
    raw[y * 13] = 0;
    for (x = 0; x < 4; x++) {
      static const u8 cols[4][3] = { { 255, 0, 0 }, { 0, 255, 0 }, { 0, 0, 255 }, { 255, 255, 255 } };
      memcpy(raw + y * 13 + 1 + x * 3, cols[x], 3);
    }
  }
  for (i = 0; i < (int)sizeof raw; i++) { a = (a + raw[i]) % 65521; bsum = (bsum + a) % 65521; }
  z = zdeflate(raw, sizeof raw, &zl);
  buf_init(&o);
  buf_add(&o, sig, 8);
  {
    u8 ih[25] = { 0, 0, 0, 13, 'I', 'H', 'D', 'R', 0, 0, 0, 4, 0, 0, 0, 3, 8, 2, 0, 0, 0 };
    crc = crc32_update(0, ih + 4, 17);
    ih[21] = (u8)(crc >> 24); ih[22] = (u8)(crc >> 16); ih[23] = (u8)(crc >> 8); ih[24] = (u8)crc;
    buf_add(&o, ih, 25);
  }
  {
    u32 dl = (u32)zl + 6;
    u8 h[8], *chunk = (u8 *)malloc(dl + 4);
    h[0] = (u8)(dl >> 24); h[1] = (u8)(dl >> 16); h[2] = (u8)(dl >> 8); h[3] = (u8)dl;
    memcpy(chunk, "IDAT", 4); chunk[4] = 0x78; chunk[5] = 0x01;
    memcpy(chunk + 6, z, zl);
    chunk[6 + zl] = (u8)(bsum >> 8); chunk[7 + zl] = (u8)bsum; chunk[8 + zl] = (u8)(a >> 8); chunk[9 + zl] = (u8)a;
    buf_add(&o, h, 4); buf_add(&o, chunk, dl + 4);
    crc = crc32_update(0, chunk, dl + 4);
    h[4] = (u8)(crc >> 24); h[5] = (u8)(crc >> 16); h[6] = (u8)(crc >> 8); h[7] = (u8)crc;
    buf_add(&o, h + 4, 4);
    free(chunk);
  }
  {
    u8 ie[12] = { 0, 0, 0, 0, 'I', 'E', 'N', 'D' };
    crc = crc32_update(0, ie + 4, 4);
    ie[8] = (u8)(crc >> 24); ie[9] = (u8)(crc >> 16); ie[10] = (u8)(crc >> 8); ie[11] = (u8)crc;
    buf_add(&o, ie, 12);
  }
  free(z);
  n = (u32)o.len;
  out = (u8 *)buf_take(&o);
  *len = n;
  return out;
}
static int count_series_prims(const PrimList *l, int k)
{
  int i, n = 0;
  for (i = 0; i < l->n; i++) if (l->p[i].elem == CE_SERIES + k) n++;
  return n;
}

static void charts(void)
{
  Book *b = sales_book(), *d;
  DObj *o;
  Chart *c;
  CData cd;
  PrimList pl;
  int k, kinds[8], n, bad;
  u8 *x; size_t len; char err[120];
  /* made from a range as Excel does it */
  c = chart_from_range(b, 0, R4(0, 0, 6, 3), CK_COL, -1);
  ok("chart: three series from the columns", c && c->ns == 3 && !c->byrows);
  ok("chart: series refs", c && ftext_is(b, c->s[0].name, "Sheet1!$B$1") && ftext_is(b, c->s[0].cat, "Sheet1!$A$2:$A$7") && ftext_is(b, c->s[2].val, "Sheet1!$D$2:$D$7"));
  ok("chart: resolved", c && chart_resolve(b, 0, c, &cd) && cd.ns == 3 && cd.ncat == 6 && !strcmp(cd.cat[1], "Feb") && !strcmp(cd.s[1].name, "South") && cd.s[0].v[5] == 25);
  cdata_free(&cd);
  ok("chart: switch row/column", c && chart_set_source(b, c, 0, c->src, 1) && c->ns == 6 && ftext_is(b, c->s[0].cat, "Sheet1!$B$1:$D$1") && ftext_is(b, c->s[0].name, "Sheet1!$A$2"));
  chart_free(c);
  ok("chart: no numbers, no chart", chart_from_range(b, 0, R4(0, 0, 0, 3), CK_COL, -1) == NULL);
  n = chart_recommend(b, 0, R4(0, 0, 6, 3), kinds, 8);
  ok("chart: recommendations", n >= 3 && kinds[0] == CK_COL);
  n = chart_recommend(b, 0, R4(0, 0, 6, 1), kinds, 8);
  ok("chart: one series may be a pie", n >= 3 && (kinds[2] == CK_PIE || kinds[3] == CK_PIE));
  /* every kind lays out, with its series' parts tagged for selecting */
  bad = 0;
  for (k = 0; k < CK_N; k++) {
    Chart *ck = chart_from_range(b, 0, R4(0, 0, 6, 3), k, -1);
    if (!ck) { bad++; printf("   no chart of kind %s\n", chart_kind_name(k)); continue; }
    ck->labels = 1;
    prims_init(&pl);
    if (!chart_render(b, 0, ck, 400, 240, 100, NULL, NULL, &pl) || pl.n < 6 || count_series_prims(&pl, 0) < 1) { bad++; printf("   %s: %d prims, %d of series 1\n", chart_kind_name(k), pl.n, count_series_prims(&pl, 0)); }
    prims_free(&pl);
    chart_free(ck);
  }
  ok("chart: every kind lays out", bad == 0);
  c = chart_from_range(b, 0, R4(0, 0, 6, 3), CK_COL, -1);
  prims_init(&pl);
  chart_render(b, 0, c, 400, 240, 100, NULL, NULL, &pl);
  {
    /* the first column of the first series: hit test finds it */
    int i, hitok = 0;
    for (i = 0; i < pl.n; i++) if (pl.p[i].elem == CE_SERIES && pl.p[i].t == PR_RECT) { hitok = prims_hit(&pl, pl.p[i].x + pl.p[i].w / 2, pl.p[i].y + pl.p[i].h / 2) == CE_SERIES; break; }
    ok("chart: hit test finds a column", hitok);
    ok("chart: hit test finds the title", prims_hit(&pl, 200, 14) == CE_TITLE);
  }
  prims_free(&pl);
  chart_quick_layout(c, 5);
  ok("chart: quick layout 5", c->legend == LG_NONE && c->xtitle && c->ytitle && c->hgrid);
  chart_colors(c, 1);
  ok("chart: monochrome palette", chart_series_rgb(c, 0, 3) != chart_series_rgb(c, 2, 3) && c->palette == 1);
  chart_free(c);
  {
    double mn, mx, mj;
    chart_nice_scale(3, 97, 5, &mn, &mx, &mj);
    ok("chart: nice scale", mn == 0 && mx == 100 && mj == 20);
  }
  /* on the sheet: undo, rows inserted */
  o = add_chart(b, 0, CK_LINEMARK, R4(0, 0, 6, 3));
  ok("chart: put on the sheet", o && drw_count(b->sh[0]) == 1 && !strcmp(o->name, "Chart 1") && book_can_undo(b));
  book_undo(b, NULL, NULL);
  ok("chart: undone", drw_count(b->sh[0]) == 0);
  book_redo(b, NULL, NULL);
  ok("chart: redone", drw_count(b->sh[0]) == 1);
  book_insert_rows(b, 0, 0, 2);
  o = drw_obj(b->sh[0], 0);
  ok("chart: follows inserted rows", o && o->from.r == 10 && ftext_is(b, o->chart->s[0].val, "Sheet1!$B$4:$B$9"));
  book_undo(b, NULL, NULL);
  o = drw_obj(b->sh[0], 0);
  ok("chart: insert undone", o && o->from.r == 8 && ftext_is(b, o->chart->s[0].val, "Sheet1!$B$2:$B$7"));
  {
    i32 x0, y0, x1, y1;
    dobj_box(b->sh[0], o, &x0, &y0, &x1, &y1);
    ok("chart: its box", x0 == 64 && y0 == 160 && x1 == 512 && y1 == 440);
    dobj_place(b->sh[0], o, 100, 30, 300, 230);
    ok("chart: placed by pixels", o->from.c == 1 && o->from.dx == 36 && o->from.r == 1 && o->from.dy == 10 && o->to.c == 4 && o->to.dx == 44);
  }
  {
    /* the data deleted under it: the chart still draws (its references are #REF!) */
    Book *e = sales_book();
    DObj *eo = add_chart(e, 0, CK_COL, R4(0, 0, 6, 3));
    int k2, okall = 1;
    book_delete_cols(e, 0, 0, 5);
    for (k2 = 0; k2 < CK_N && eo; k2++) {
      eo = drw_obj(e->sh[0], 0);
      eo->chart->kind = (u8)k2;
      prims_init(&pl);
      if (!chart_render(e, 0, eo->chart, 300, 200, 100, NULL, NULL, &pl)) okall = 0;
      prims_free(&pl);
    }
    {
      Chart *empty = chart_new(CK_PIE);
      prims_init(&pl);
      if (!chart_render(e, 0, empty, 50, 30, 100, NULL, NULL, &pl)) okall = 0;
      prims_free(&pl);
      chart_free(empty);
    }
    ok("chart: draws with its data gone", eo && okall);
    book_free(e);
  }
  book_duplicate_sheet(b, 0);
  ok("chart: copied with its sheet", b->nsh == 2 && drw_count(b->sh[1]) == 1 && drw_obj(b->sh[1], 0)->chart != drw_obj(b->sh[0], 0)->chart);
  book_delete_sheet(b, 1);
  /* through a file and back, for every kind */
  bad = 0;
  for (k = 0; k < CK_N; k++) {
    Book *e = sales_book();
    DObj *eo = add_chart(e, 0, k, R4(0, 0, 6, 3));
    if (!eo) { bad++; book_free(e); continue; }
    if (k == CK_COMBO) eo->chart->s[2].secondary = 1;
    x = xlsx_save(e, &len);
    d = x ? xlsx_load(x, len, err, sizeof err) : NULL;
    o = d ? drw_obj(d->sh[0], 0) : NULL;
    if (!o || !o->chart || o->chart->kind != k || o->from.r != 8 || o->to.c != 8 || o->chart->ns < 1) {
      bad++;
      printf("   %s came back as %s\n", chart_kind_name(k), o && o->chart ? chart_kind_name(o->chart->kind) : "nothing");
    } else if (!ftext_is(d, o->chart->s[0].val, "Sheet1!$B$2:$B$7")) bad++;
    if (d) book_free(d);
    free(x);
    book_free(e);
  }
  ok("chart: every kind through a file", bad == 0);
  /* the chart's settings through a file */
  o = drw_obj(b->sh[0], 0);
  c = o->chart;
  c->kind = CK_BAR;
  free(c->title); c->title = str_dup("Sales & Costs");
  c->legend = LG_RIGHT; c->labels = 1;
  c->xtitle = str_dup("Units"); c->ytitle = str_dup("Month");
  c->va.minset = 1; c->va.min = 0; c->va.maxset = 1; c->va.max = 40; c->va.majset = 1; c->va.major = 10;
  c->s[1].color = FILL_ON | 0x123456;
  c->efill[CE_PLOT] = FILL_ON | 0xF0F0F0;
  x = xlsx_save(b, &len);
  d = x ? xlsx_load(x, len, err, sizeof err) : NULL;
  o = d ? drw_obj(d->sh[0], 0) : NULL;
  c = o ? o->chart : NULL;
  ok("chart: settings through a file", c && c->kind == CK_BAR && c->title && !strcmp(c->title, "Sales & Costs") && c->legend == LG_RIGHT && c->labels &&
     c->xtitle && !strcmp(c->xtitle, "Units") && c->ytitle && !strcmp(c->ytitle, "Month") && c->va.maxset && c->va.max == 40 && c->va.majset && c->va.major == 10);
  ok("chart: colours through a file", c && c->s[1].color == (FILL_ON | 0x123456) && c->s[0].color == 0 && c->efill[CE_PLOT] == (FILL_ON | 0xF0F0F0));
  ok("chart: name through a file", o && !strcmp(o->name, "Chart 1"));
  if (d) book_free(d);
  free(x);
  {
    /* the parts LibreOffice and Excel look for */
    ZipReader z;
    x = xlsx_save(b, &len);
    ok("chart: package parts", x && zip_open(&z, x, len) && zip_find(&z, "xl/charts/chart1.xml") >= 0 && zip_find(&z, "xl/drawings/drawing1.xml") >= 0 &&
       zip_find(&z, "xl/drawings/_rels/drawing1.xml.rels") >= 0 && zip_find(&z, "xl/worksheets/_rels/sheet1.xml.rels") >= 0);
    if (x) {
      size_t cl;
      int ci = zip_find(&z, "[Content_Types].xml");
      u8 *ct = ci >= 0 ? zip_read(&z, ci, &cl) : NULL;
      ok("chart: content types", ct && strstr((char *)ct, "/xl/charts/chart1.xml") && strstr((char *)ct, "drawing+xml"));
      free(ct);
      zip_close(&z);
    }
    free(x);
  }
  book_free(b);
}

static void pictures_shapes(void)
{
  Book *b = book_new(), *d;
  u32 plen, w, h;
  u8 *png = make_png(&plen), *rgb, *x;
  DObj *o;
  size_t len;
  char err[120];
  ok("picture: the test PNG reads", img_info(png, plen, &w, &h) == IMG_PNG && w == 4 && h == 3);
  rgb = img_decode(png, plen, 4, 3);
  ok("picture: decoded", rgb && rgb[0] == 255 && rgb[1] == 0 && rgb[3 + 1] == 255 && rgb[6 + 2] == 255);
  free(rgb);
  drw_begin(b, 0);
  o = drw_new(b->sh[0], OB_PICTURE);
  o->pic = blob_new(png, plen); o->pictype = PIC_PNG;
  o->descr = str_dup("Stripes");
  dobj_place(b->sh[0], o, 10, 10, 138, 106);
  o = drw_new(b->sh[0], OB_SHAPE);
  o->shape = SK_ROUNDRECT; o->fill = FILL_ON | 0x4472C4; o->line = FILL_ON | 0x203864;
  o->text = str_dup("Hello\nWorld"); o->halign = 1; o->valign = 1; o->bold = 1; o->fsize = 140;
  dobj_place(b->sh[0], o, 200, 20, 330, 90);
  o = drw_new(b->sh[0], OB_SHAPE);
  o->shape = SK_ARROW; o->line = FILL_ON | 0xC00000; o->linew = 200;
  dobj_place(b->sh[0], o, 20, 200, 200, 260);
  o = drw_new(b->sh[0], OB_SHAPE);
  o->shape = SK_INK; o->line = FILL_ON | 0x1F5FC9; o->linew = 150;
  o->text = str_dup("0,0;10,20;40,25;60,5");
  dobj_place(b->sh[0], o, 300, 200, 360, 225);
  o = drw_new(b->sh[0], OB_SHAPE);
  o->shape = SK_TEXTBOX; o->text = str_dup("A note"); o->line = FILL_ON | 0;
  dobj_place(b->sh[0], o, 400, 40, 500, 80);
  drw_end(b);
  ok("shapes: five objects", drw_count(b->sh[0]) == 5 && !strcmp(drw_obj(b->sh[0], 0)->name, "Picture 1") && !strcmp(drw_obj(b->sh[0], 2)->name, "Shape 2"));
  x = xlsx_save(b, &len);
  d = x ? xlsx_load(x, len, err, sizeof err) : NULL;
  ok("shapes: read back", d && drw_count(d->sh[0]) == 5);
  if (d && drw_count(d->sh[0]) == 5) {
    DObj *p = drw_obj(d->sh[0], 0), *r = drw_obj(d->sh[0], 1), *a = drw_obj(d->sh[0], 2), *k = drw_obj(d->sh[0], 3), *t = drw_obj(d->sh[0], 4);
    i32 x0, y0, x1, y1;
    dobj_box(d->sh[0], p, &x0, &y0, &x1, &y1);
    ok("picture: through a file", p->kind == OB_PICTURE && p->pic && p->pic->len == plen && !memcmp(p->pic->data, png, plen) && p->descr && !strcmp(p->descr, "Stripes") &&
       x0 == 10 && y0 == 10 && x1 == 138 && y1 == 106);
    ok("shape: through a file", r->kind == OB_SHAPE && r->shape == SK_ROUNDRECT && r->fill == (FILL_ON | 0x4472C4) && r->line == (FILL_ON | 0x203864) &&
       r->text && !strcmp(r->text, "Hello\nWorld") && r->halign == 1 && r->valign == 1 && r->bold && r->fsize == 140);
    ok("arrow: through a file", a->shape == SK_ARROW && a->line == (FILL_ON | 0xC00000) && a->linew == 200);
    ok("ink: through a file", k->shape == SK_INK && k->text && !strcmp(k->text, "0,0;10,20;40,25;60,5") && k->line == (FILL_ON | 0x1F5FC9));
    ok("text box: through a file", t->shape == SK_TEXTBOX && t->text && !strcmp(t->text, "A note"));
  }
  if (d) book_free(d);
  free(x);
  {
    /* an icon from Ledger's set */
    Book *e = book_new(), *f;
    DObj *io;
    drw_begin(e, 0);
    io = drw_new(e->sh[0], OB_SHAPE);
    io->shape = SK_ICON; io->text = str_dup("13"); io->line = FILL_ON | 0x70AD47;
    snprintf(io->name, sizeof io->name, "Icon: %s 1", drw_icon_names[13]);
    dobj_place(e->sh[0], io, 50, 50, 98, 98);
    drw_end(e);
    x = xlsx_save(e, &len);
    f = x ? xlsx_load(x, len, err, sizeof err) : NULL;
    io = f ? drw_obj(f->sh[0], 0) : NULL;
    ok("icon: through a file", io && io->shape == SK_ICON && io->text && !strcmp(io->text, "13") && drw_icon_desc(13));
    if (f) book_free(f);
    free(x);
    book_free(e);
  }
  {
    /* a background picture */
    Book *e = book_new(), *f;
    Drawing *dr = drw_of(e->sh[0]);
    dr->bg = blob_new(png, plen); dr->bgtype = PIC_PNG;
    x = xlsx_save(e, &len);
    f = x ? xlsx_load(x, len, err, sizeof err) : NULL;
    ok("background: through a file", f && f->sh[0]->drw && f->sh[0]->drw->bg && f->sh[0]->drw->bg->len == plen && f->sh[0]->drw->bgtype == PIC_PNG);
    if (f) book_free(f);
    free(x);
    book_free(e);
  }
  /* objects follow columns deleted */
  book_delete_cols(b, 0, 0, 2);
  o = drw_obj(b->sh[0], 1);
  ok("shapes: follow deleted columns", o && o->from.c == 1);
  book_free(b);
  free(png);
}

static void sparklines(void)
{
  Book *b = sales_book(), *d;
  Spark *sp;
  SparkGroup *g = NULL;
  double v[16], lo, hi;
  int n;
  PrimList pl;
  u8 *x; size_t len; char err[120];
  ok("sparkline: added", book_add_sparklines(b, 0, SP_LINE, R4(1, 1, 6, 3), R4(7, 1, 7, 3)) && (sp = spark_at(b->sh[0], 7, 2, &g)) && g->n == 3);
  sp = spark_at(b->sh[0], 7, 1, &g);
  n = sp ? spark_values(b, 0, sp, v, 16) : 0;
  ok("sparkline: its values", n == 6 && v[0] == 10 && v[5] == 25);
  prims_init(&pl);
  g->markers = 1; g->high = 1;
  ok("sparkline: drawn", spark_render(g, v, n, 60, 20, NAN, NAN, &pl) && pl.n >= 7 && pl.p[0].t == PR_LINE && pl.p[0].np == 6);
  prims_free(&pl);
  g->type = SP_COLUMN;
  prims_init(&pl);
  ok("sparkline: columns", spark_render(g, v, n, 60, 20, NAN, NAN, &pl) && pl.n == 6);
  prims_free(&pl);
  g->minmode = 1; g->maxmode = 1;
  spark_group_range(b, 0, g, &lo, &hi);
  ok("sparkline: the group's range", lo == 5 && hi == 25);
  g->type = SP_WINLOSS; g->negative = 1;
  ok("sparkline: in one cell from a row", book_add_sparklines(b, 0, SP_COLUMN, R4(1, 1, 1, 3), R4(1, 5, 1, 5)) && (sp = spark_at(b->sh[0], 1, 5, NULL)) && spark_values(b, 0, sp, v, 16) == 3);
  x = xlsx_save(b, &len);
  d = x ? xlsx_load(x, len, err, sizeof err) : NULL;
  sp = d ? spark_at(d->sh[0], 7, 3, &g) : NULL;
  ok("sparkline: through a file", sp && g->type == SP_WINLOSS && g->negative && g->markers && g->high && g->minmode == 1 && g->n == 3 && ftext_is(d, sp->f, "Sheet1!$D$2:$D$7"));
  ok("sparkline: the other group too", d && d->sh[0]->drw && d->sh[0]->drw->ng == 2);
  if (d) book_free(d);
  free(x);
  book_insert_rows(b, 0, 3, 1);
  sp = spark_at(b->sh[0], 8, 1, &g);
  ok("sparkline: moves with rows", sp && !spark_at(b->sh[0], 7, 1, NULL) && ftext_is(b, sp->f, "Sheet1!$B$2:$B$8"));
  ok("sparkline: cleared", book_clear_sparklines(b, 0, R4(8, 1, 8, 1), 0) == 1 && !spark_at(b->sh[0], 8, 1, NULL) && spark_at(b->sh[0], 8, 2, NULL));
  book_undo(b, NULL, NULL);
  ok("sparkline: clearing undone", spark_at(b->sh[0], 8, 1, NULL) != NULL);
  book_free(b);
}

static void drawings(void)
{
  charts();
  pictures_shapes();
  sparklines();
}
