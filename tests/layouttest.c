/*
 * layouttest.c - tests of page setup, pages, headers and footers, PDF export, spelling and the
 * formula auditing tools (included by coretest.c, which supplies ok() and the helpers).
 */
#include "../core/pagesetup.h"
#include "../core/pdfout.h"
#include "../core/fonts.h"
#include "../core/spell.h"
#include "../core/audit.h"

static Book *roundtrip(Book *b)
{
  size_t len;
  char err[128];
  u8 *x = xlsx_save(b, &len);
  Book *d = x ? xlsx_load(x, len, err, sizeof err) : NULL;
  free(x);
  return d;
}
static void pct_fmt(Style *st, Font *f, const void *arg) { (void)f; (void)arg; st->numfmt = 10; st->border[B_BOTTOM] = BS_THIN; st->fill = FILL_ON | 0xDDEBF7; }
static void apply_test_format(Book *b, Rect a) { book_restyle(b, 0, a, pct_fmt, NULL); }
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
/* a small formatted workbook as PDF; LEDGER_PDF=path keeps it for looking at */
static void pdf_tests(void)
{
  static FontSet fs;
  Book *b = book_new();
  PdfOptions o;
  PageSetup *p;
  u8 *pdf;
  size_t n = 0;
  i32 r;
  Rect a;
  const char *dir = getenv("LEDGER_FONTS") ? getenv("LEDGER_FONTS") : "/home/user/quill/fonts";
  if (!fonts_init(&fs, dir)) { printf("   (no fonts in %s: PDF tests skipped)\n", dir); book_free(b); return; }
  book_input(b, 0, 0, 0, "Quarterly report", NULL);
  book_input(b, 0, 1, 0, "Item", NULL); book_input(b, 0, 1, 1, "Amount", NULL); book_input(b, 0, 1, 2, "Share", NULL);
  for (r = 2; r < 80; r++) {
    char t[32];
    sprintf(t, "Line %ld", (long)r - 1); book_input(b, 0, r, 0, t, NULL);
    sprintf(t, "%ld.5", (long)(r * 37 % 101)); book_input(b, 0, r, 1, t, NULL);
    sprintf(t, "=B%ld/SUM($B$3:$B$80)", (long)r + 1); book_input(b, 0, r, 2, t, NULL);
  }
  a.r1 = 2; a.r2 = 79; a.c1 = 2; a.c2 = 2;
  apply_test_format(b, a);
  p = page_get(b->sh[0]);
  page_set_hf(p, HF_HEADER, "&L&A&C&\"-,Bold\"Quarterly &P of &N&R&D");
  page_set_hf(p, HF_FOOTER, "&CPage &P");
  p->gridlines = 1; p->headings = 1; p->trow1 = 1; p->trow2 = 1;
  memset(&o, 0, sizeof o);
  o.file = "Report.xlsx"; o.now = 46322.5;
  ok("pdf: page count", pdf_count_pages(b, &o) == 2);
  pdf = pdf_book(b, &fs, &o, &n);
  ok("pdf: made", pdf && n > 1000 && !memcmp(pdf, "%PDF-1.4", 8));
  ok("pdf: two pages", pdf && strstr((char *)pdf, "/Count 2>>"));
  ok("pdf: a font embedded", pdf && strstr((char *)pdf, "/FontFile2"));
  if (pdf && getenv("LEDGER_PDF")) file_write(getenv("LEDGER_PDF"), pdf, n);
  if (getenv("LEDGER_XLSX")) { size_t xl; u8 *x = xlsx_save(b, &xl); if (x) file_write(getenv("LEDGER_XLSX"), x, xl); free(x); }
  free(pdf);
  o.what = PDF_SELECTION;
  b->sh[0]->sel.r1 = 0; b->sh[0]->sel.r2 = 9; b->sh[0]->sel.c1 = 0; b->sh[0]->sel.c2 = 1;
  ok("pdf: selection is one page", pdf_count_pages(b, &o) == 1);
  ok("pdf: the sheet's own setup untouched", p->area.r1 < 0 && b->sh[0]->page == p);
  book_free(b);
  fonts_free(&fs);
}
static void spell_tests(void)
{
  static const char words[] = "apple\nbanana\nreport\nquarterly\nthe\nthis\nspelling\nsales\nsale\nsalesman\nsalesmen\nof\n";
  Spell sp;
  Book *b = book_new();
  int sh = 0, n;
  i32 r = 0, c = 0;
  u32 off = 0, ws, we;
  char *sug[10];
  ok("spell: list loads", spell_load_mem(&sp, words, sizeof words - 1) && sp.n == 12);
  book_input(b, 0, 0, 0, "Quarterly report", NULL);
  book_input(b, 0, 0, 1, "=\"speling\"", NULL);          /* a formula: left alone */
  book_input(b, 0, 2, 0, "the speling of thiss", NULL);
  book_input(b, 0, 3, 0, "42", NULL);
  book_add_sheet(b, "Two", -1);
  book_input(b, 1, 0, 0, "banana apel", NULL);
  ok("spell: first mistake", spell_next_cell(&sp, b, 1, &sh, &r, &c, &off, &ws, &we) && sh == 0 && r == 2 && c == 0 && ws == 4 && we == 11);
  n = spell_suggest(&sp, "speling", 7, sug, 10);
  ok("spell: suggestion", n >= 1 && !strcmp(sug[0], "spelling"));
  while (n > 0) free(sug[--n]);
  ok("spell: next in the cell", spell_next_cell(&sp, b, 1, &sh, &r, &c, &off, &ws, &we) && r == 2 && ws == 15);
  ok("spell: on to the next sheet", spell_next_cell(&sp, b, 1, &sh, &r, &c, &off, &ws, &we) && sh == 1 && ws == 7);
  ok("spell: wraps back round", spell_next_cell(&sp, b, 1, &sh, &r, &c, &off, &ws, &we) && sh == 0 && r == 2 && ws == 4);
  spell_add(&sp, "thiss", 5);
  ok("spell: change all", spell_change_all(b, 1, 0, "speling", "spelling") == 1);
  sh = 0; r = 0; c = 0; off = 0;
  n = spell_next_cell(&sp, b, 1, &sh, &r, &c, &off, &ws, &we);
  if (!(n && sh == 1)) printf("   got %d sheet %d r %ld c %ld ws %lu\n", n, sh, (long)r, (long)c, (unsigned long)ws);
  ok("spell: only the other sheet's left", n && sh == 1);
  sh = 0; r = 0; c = 0; off = 0;
  ok("spell: one sheet only", !spell_next_cell(&sp, b, 0, &sh, &r, &c, &off, &ws, &we));
  ok("spell: change one", spell_change(b, 1, 0, 0, 7, 11, "apple") && !strcmp(cell_value(b, 1, 0, 0)->s, "banana apple"));
  ok("spell: formula untouched", sheet_cell(b->sh[0], 0, 1)->f != NULL);
  n = spell_related(&sp, "sales", sug, 10);
  ok("thesaurus: related words", n >= 2 && !strcmp(sug[0], "sale"));
  while (n > 0) free(sug[--n]);
  spell_free(&sp);
  book_free(b);
}
static void audit_tests(void)
{
  Book *b = book_new();
  AuditRef refs[16];
  AuditCell cells[16];
  EvalSteps e;
  char t[256], part[256];
  Rect a, bound;
  int n;
  book_add_sheet(b, "Two", -1);
  book_input(b, 0, 0, 0, "2", NULL);           /* A1 */
  book_input(b, 0, 1, 0, "3", NULL);           /* A2 */
  book_input(b, 0, 2, 0, "=A1*(A2+1)", NULL);  /* A3 = 8 */
  book_input(b, 0, 3, 0, "=SUM(A1:A3)+Two!B2", NULL);
  book_input(b, 1, 1, 1, "=Sheet1!A1/0", NULL);
  n = audit_precedents(b, 0, 3, 0, refs, 16);
  ok("audit: precedents", n == 2 && refs[0].r.r1 == 0 && refs[0].r.r2 == 2 && refs[1].sheet == 1 && refs[1].r.r1 == 1 && refs[1].r.c1 == 1);
  n = audit_dependents(b, 0, 0, 0, refs, 16);
  ok("audit: dependents", n == 3 && refs[0].r.r1 == 2 && refs[1].r.r1 == 3 && refs[2].sheet == 1);
  ok("eval: begins", eval_begin(&e, b, 0, 2, 0));
  eval_text(&e, t, sizeof t); ok("eval: the formula", !strcmp(t, "=A1*(A2+1)"));
  eval_next(&e, part, sizeof part); ok("eval: A1 first", !strcmp(part, "A1"));
  eval_step(&e); eval_text(&e, t, sizeof t); ok("eval: A1 worked out", !strcmp(t, "=2*(A2+1)"));
  eval_step(&e); eval_text(&e, t, sizeof t); ok("eval: A2", !strcmp(t, "=2*(3+1)"));
  eval_next(&e, part, sizeof part); ok("eval: the sum next", !strcmp(part, "3+1"));
  eval_step(&e); eval_step(&e); eval_text(&e, t, sizeof t); ok("eval: brackets go", !strcmp(t, "=2*4"));
  ok("eval: not done yet", !eval_done(&e));
  eval_step(&e); eval_text(&e, t, sizeof t); ok("eval: result", !strcmp(t, "=8") && eval_done(&e));
  eval_end(&e);
  eval_begin(&e, b, 0, 3, 0);
  eval_next(&e, part, sizeof part); ok("eval: a range stays for SUM", !strcmp(part, "SUM(A1:A3)"));
  while (eval_step(&e)) ;
  eval_text(&e, t, sizeof t); ok("eval: error result", !strcmp(t, "=#DIV/0!"));
  eval_end(&e);
  n = audit_errors(b, cells, 16);
  ok("errors: found", n == 2 && cells[0].sheet == 0 && cells[0].r == 3 && cells[1].sheet == 1 && cells[1].err == E_DIV0);
  a.r1 = a.c1 = 0; a.r2 = a.c2 = 0;
  ok("goto special: formulas", goto_special(b, 0, a, 0, 0, GS_FORMULAS, &bound, cells, 16) == 2 && bound.r1 == 2 && bound.r2 == 3);
  ok("goto special: constants", goto_special(b, 0, a, 0, 0, GS_CONSTANTS, &bound, cells, 16) == 2 && bound.r2 == 1);
  ok("goto special: errors", goto_special(b, 0, a, 0, 0, GS_ERRORS, &bound, cells, 16) == 1 && cells[0].r == 3);
  ok("goto special: last cell", goto_special(b, 0, a, 0, 0, GS_LASTCELL, &bound, cells, 16) == 1 && bound.r1 == 3 && bound.c1 == 0);
  ok("goto special: region", goto_special(b, 0, a, 1, 0, GS_REGION, &bound, cells, 16) == 1 && bound.r1 == 0 && bound.r2 == 3);
  /* names from a table's headings */
  book_input(b, 0, 0, 3, "Unit price", NULL); book_input(b, 0, 0, 4, "Qty", NULL);
  book_input(b, 0, 1, 3, "5", NULL); book_input(b, 0, 1, 4, "7", NULL);
  a.r1 = 0; a.r2 = 2; a.c1 = 3; a.c2 = 4;
  ok("names: from the top row", book_create_names(b, 0, a, 1, 0, 0, 0) == 2);
  ok("names: used", evn(b, "SUM(Unit_price)*SUM(Qty)") == 35);
  n = audit_accessibility(b, t, sizeof t);
  ok("accessibility: default sheet name", n == 1 && strstr(t, "Sheet1\tDefault sheet name"));
  book_free(b);
}
static void iterate_tests(void)
{
  Book *b = book_new();
  LayoutBook *x = book_lx(b);
  book_input(b, 0, 0, 0, "=B1/2+1", NULL);
  book_input(b, 0, 0, 1, "=A1", NULL);
  ok("iterate: off, a circular reference is 0", cell_value(b, 0, 0, 1)->n < 1.5);
  x->iterate = 1; x->maxiter = 100; x->maxchange = 0.0001;
  book_recalc(b);
  ok("iterate: converges", fabs(cell_value(b, 0, 0, 0)->n - 2) < 0.001);
  x->maxiter = 1;
  book_input(b, 0, 1, 0, "=A2+1", NULL);
  ok("iterate: one iteration only", cell_value(b, 0, 1, 0)->n <= 2);
  book_free(b);
}
static void layout_tests(void)
{
  iterate_tests();
  audit_tests();
  spell_tests();
  pagesetup_tests();
  pdf_tests();
}
