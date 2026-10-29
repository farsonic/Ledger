/*
 * layouttest.c - tests of page setup, pages, headers and footers, PDF export, spelling and the
 * formula auditing tools (included by coretest.c, which supplies ok() and the helpers).
 */
#include "../core/pagesetup.h"

static Book *roundtrip(Book *b)
{
  size_t len;
  char err[128];
  u8 *x = xlsx_save(b, &len);
  Book *d = x ? xlsx_load(x, len, err, sizeof err) : NULL;
  free(x);
  return d;
}
static void pagesetup_tests(void)
{
  Book *b = book_new(), *d;
  Sheet *s;
  PageSetup *p;
  Pages pg;
  HFSection sec[3];
  HFContext cx;
  char t[256];
  i32 r;
  int rb, cb;
  s = b->sh[0];
  ok("page: defaults without a setup", page_of(s)->scale == 100 && page_of(s)->margin[M_LEFT] == 0.7 && !s->page);
  /* 100 rows of 20 px (15 pt): Letter portrait has 792 - 108 = 684 pt: 45 rows a page */
  for (r = 0; r < 100; r++) { char v[16]; sprintf(v, "%ld", (long)r); book_input(b, 0, r, 0, v, NULL); }
  book_input(b, 0, 0, 20, "wide", NULL);   /* column U: past a page's width (612 - 100.8 pt: 10 columns of 48 pt) */
  ok("page: layout", page_layout(b, 0, &pg));
  ok("page: rows per page", pg.nrows == 3 && pg.rows[0].a == 0 && pg.rows[0].b == 44 && pg.rows[1].a == 45 && pg.rows[2].b == 99);
  ok("page: columns per page", pg.ncols == 3 && pg.cols[0].b == 9 && pg.cols[1].a == 10 && pg.cols[2].b == 20);
  ok("page: page count", pg.npages == 9);
  page_bands(&pg, 4, &rb, &cb);
  ok("page: down then over", rb == 1 && cb == 1);
  page_bands(&pg, 3, &rb, &cb);
  ok("page: down then over (2)", rb == 0 && cb == 1);
  pages_free(&pg);
  /* a manual break and landscape */
  ok("page: break added", page_set_break(s, 1, 10, 1) && page_has_break(s, 1, 10) && !page_set_break(s, 1, 10, 1));
  p = page_get(s);
  p->landscape = 1;
  page_layout(b, 0, &pg);
  ok("page: manual break", pg.rows[0].b == 9 && pg.rows[1].a == 10);
  ok("page: landscape rows", pg.rows[1].b == 10 + (int)((612 - 108) / 15) - 1);
  pages_free(&pg);
  /* fit to one page wide by one tall */
  p->fit = 1; p->fitw = 1; p->fith = 1;
  page_layout(b, 0, &pg);
  ok("page: fit to one page", pg.npages == 1 && pg.scale < 1 && pg.scale > 0.1);
  pages_free(&pg);
  p->fit = 0; p->fith = 0;
  /* print area, titles */
  p->area.r1 = 0; p->area.c1 = 0; p->area.r2 = 59; p->area.c2 = 3;
  p->trow1 = 0; p->trow2 = 1;
  page_layout(b, 0, &pg);
  ok("page: print area", pg.ncols == 1 && pg.rows[pg.nrows - 1].b == 59);
  ok("page: titles take room", pg.rows[1].a == 10 && pg.rows[1].b == 10 + (int)((612 - 108 - 30) / 15) - 1);
  pages_free(&pg);
  /* rows inserted: the break and the area move */
  book_insert_rows(b, 0, 5, 2);
  ok("page: shift with rows", page_has_break(s, 1, 12) && p->area.r2 == 61);
  { int us; Rect ua; book_undo(b, &us, &ua); }
  s = b->sh[0]; p = page_get(s);
  ok("page: undo restores", page_has_break(s, 1, 10) && p->area.r2 == 59);
  /* headers and footers */
  memset(&cx, 0, sizeof cx);
  cx.page = 3; cx.pages = 9; cx.file = "Book.xlsx"; cx.sheet = "Sheet1"; cx.now = 46322.5;
  hf_expand("&LLeft &A&C&\"Arial,Bold\"Page &P of &N&R&D &T &&", &cx, sec);
  hf_plain(&sec[0], t, sizeof t); ok("hf: left", !strcmp(t, "Left Sheet1"));
  hf_plain(&sec[1], t, sizeof t); ok("hf: centre", !strcmp(t, "Page 3 of 9"));
  ok("hf: font and bold", sec[1].n == 1 && sec[1].run[0].bold && !strcmp(sec[1].run[0].font, "Arial"));
  hf_plain(&sec[2], t, sizeof t); ok("hf: right", !strcmp(t, "27/10/2026 12:00 &"));
  hf_expand("Plain &12&Bbig&B small", &cx, sec);
  ok("hf: no section is centre, sizes", sec[1].n == 3 && sec[1].run[1].size == 12 && sec[1].run[1].bold && !strcmp(sec[1].run[1].text, "big") && !sec[1].run[2].bold);
  p->difffirst = 1;
  ok("hf: first page", hf_which(p, 0, 1, 1) == HF_FIRSTH && hf_which(p, 1, 0, 2) == HF_FOOTER);
  page_set_hf(p, HF_HEADER, "&CQuarterly <report> & more");
  page_set_hf(p, HF_FIRSTF, "&RFirst");
  p->margin[M_TOP] = 1.25; p->paper = 5; p->scale = 80; p->gridlines = 1; p->centerh = 1; p->firstpage = 4; p->view = PV_BREAKS;
  page_set_break(s, 0, 3, 1);
  /* the file */
  d = roundtrip(b);
  ok("page: xlsx loads", d != NULL);
  if (d) {
    const PageSetup *q = page_of(d->sh[0]);
    ok("page: xlsx margins, paper, scale", q->margin[M_TOP] == 1.25 && q->paper == 5 && q->scale == 80 && q->landscape);
    ok("page: xlsx options", q->gridlines && q->centerh && !q->headings && q->firstpage == 4 && q->view == PV_BREAKS);
    ok("page: xlsx header", q->hf[HF_HEADER] && !strcmp(q->hf[HF_HEADER], "&CQuarterly <report> & more") && q->hf[HF_FIRSTF] && q->difffirst);
    ok("page: xlsx breaks", q->nrbrk == 1 && q->rbrk[0] == 10 && q->ncbrk == 1 && q->cbrk[0] == 3);
    ok("page: xlsx print area", q->area.r1 == 0 && q->area.c1 == 0 && q->area.r2 == 59 && q->area.c2 == 3);
    ok("page: xlsx print titles", q->trow1 == 0 && q->trow2 == 1 && q->tcol1 < 0);
    ok("page: _xlnm names aren't user names", d->nnames == 0);
    book_free(d);
  }
  page_reset_breaks(s);
  ok("page: reset breaks", !page_has_break(s, 1, 10) && !page_has_break(s, 0, 3));
  /* a sheet name that needs quotes, and fit to page in the file */
  book_rename_sheet(b, 0, "My Data");
  p->fit = 1; p->fitw = 1; p->fith = 0; p->tcol1 = 0; p->tcol2 = 0;
  d = roundtrip(b);
  if (d) {
    const PageSetup *q = page_of(d->sh[0]);
    ok("page: quoted sheet in names", q->area.r2 == 59 && q->tcol1 == 0 && q->tcol2 == 0 && q->trow1 == 0);
    ok("page: fit to page in the file", q->fit && q->fitw == 1 && q->fith == 0);
    book_free(d);
  } else ok("page: quoted sheet roundtrip", 0);
  /* properties and theme */
  {
    LayoutBook *x = book_lx(b);
    strcpy(x->subject, "Accounts"); strcpy(x->keywords, "money, 2026"); strcpy(x->company, "Acme & Co");
    x->iterate = 1; x->maxiter = 50;
    book_set_theme(b, &page_themes[1]);
    ok("theme: default font follows", !strcmp(b->fonts[b->styles[0].font].name, "Calibri"));
    book_set_theme_fonts(b, "Cambria", "Arial");
    ok("theme: body font becomes default", !strcmp(b->fonts[b->styles[0].font].name, "Arial"));
    d = roundtrip(b);
    if (d && d->lx) {
      ok("props: subject, keywords, company", !strcmp(d->lx->subject, "Accounts") && !strcmp(d->lx->keywords, "money, 2026") && !strcmp(d->lx->company, "Acme & Co"));
      ok("calc: iteration kept", d->lx->iterate && d->lx->maxiter == 50);
      ok("theme: kept", !strcmp(d->lx->theme.name, "Office 2007 - 2010") && d->lx->theme.color[4] == 0x4F81BD && !strcmp(d->lx->theme.minor, "Arial"));
    } else ok("props: roundtrip", 0);
    book_free(d);
  }
  book_free(b);
}
static void layout_tests(void)
{
  pagesetup_tests();
}
