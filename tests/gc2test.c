/*
 * gc2test.c - tests of the second round of GridCraft 4c388df gaps closed (2026-11-18), included by
 * coretest.c: Find and Replace within a selection, CSV date orders, SYLK, Solver models, form
 * controls, Python cells, equations, web tables and right-to-left sheets.
 */

static void gc2_find(void)
{
  Book *b = book_new();
  Rect a = R("B2:C3");
  int sh = 0;
  i32 r = 0, c = 0;
  put(b, "A1", "cat"); put(b, "B2", "cat"); put(b, "C3", "catalogue"); put(b, "D4", "cat");
  book_add_sheet(b, "Two", -1);
  book_input(b, 1, 2, 2, "cat", NULL);
  ok("find in selection: first inside", book_find_in(b, &sh, &r, &c, "cat", FIND_BOOK, &a) && sh == 0 && r == 1 && c == 1);
  ok("find in selection: next inside", book_find_in(b, &sh, &r, &c, "cat", FIND_BOOK, &a) && r == 2 && c == 2);
  ok("find in selection: wraps inside", book_find_in(b, &sh, &r, &c, "cat", FIND_BOOK, &a) && sh == 0 && r == 1 && c == 1);
  ok("find in selection: backwards", book_find_in(b, &sh, &r, &c, "cat", FIND_BACK, &a) && r == 2 && c == 2);
  ok("find in selection: none", !book_find_in(b, &sh, &r, &c, "dog", 0, &a));
  ok("replace in selection", book_replace_all_in(b, 0, "cat", "dog", FIND_BOOK, &a) == 2 && text_at(b, "B2", "dog") && text_at(b, "C3", "dogalogue") &&
     text_at(b, "A1", "cat") && text_at(b, "D4", "cat") && !strcmp(cell_value(b, 1, 2, 2)->s, "cat"));
  book_free(b);
}

extern int ledger_date_order;
static void gc2_csv_dates(void)
{
  const char *t = "When,US,Either,Mixed,ISO\n31/05/2026,12/31/2026,03/04/2026,31/05/2026,2026-05-31\n03/04/2026,03/04/2026 10:30,12/01/2026,12/31/2026,x\n";
  int either, both;
  Book *b = csv_load_report(t, strlen(t), 0, &either, &both);
  ok("csv dates: a day-first column", b && num_at(b, "A2") == date_serial(2026, 5, 31) && num_at(b, "A3") == date_serial(2026, 4, 3));
  ok("csv dates: a month-first column", b && num_at(b, "B2") == date_serial(2026, 12, 31) && near(num_at(b, "B3"), date_serial(2026, 3, 4) + 10.5 / 24));
  ok("csv dates: either way follows Ledger's order", b && num_at(b, "C2") == date_serial(2026, ledger_date_order ? 3 : 4, ledger_date_order ? 4 : 3) && either == 2);
  ok("csv dates: both kinds stay text", b && text_at(b, "D2", "31/05/2026") && text_at(b, "D3", "12/31/2026") && both == 3);
  ok("csv dates: ISO dates as ever", b && num_at(b, "E2") == date_serial(2026, 5, 31));
  if (b) book_free(b);
}

static int str_is(char *got, const char *want) { int k = got && !strcmp(got, want); if (!k) printf("  got %s\n", got ? got : "(null)"); free(got); return k; }
static void gc2_sylk(void)
{
  /* as Excel writes it (made by hand): formats, a width, values, R1C1 formulas, a shared formula */
  const char *t = "ID;PWXL;N;E\r\nP;PGeneral\r\nP;P0.00\r\nF;W2 2 20\r\nB;Y4;X3;D0 0 3 2\r\n"
                  "C;Y1;X1;K\"Item;;s\"\r\nC;X2;K12.5\r\nF;P1;Y2;X2\r\nC;Y2;X1;KTRUE\r\nC;X2;K25;ER[-1]C*2\r\n"
                  "C;Y3;X2;K50;ER[-1]C*2\r\nC;Y4;X2;S;R3;C2\r\nC;Y4;X3;K#N/A;ENA()\r\nC;Y1;X3;ESUM(R1C2:R[3]C[-1])+ROUND(C2:C2,0)\r\nE\r\n";
  int bad;
  Book *b = sylk_load(t, strlen(t), &bad), *d;
  char *out;
  ok("sylk: is SYLK", sylk_is(t, strlen(t)) && !sylk_is("a,b\n", 4));
  ok("sylk: values", b && text_at(b, "A1", "Item;s") && num_at(b, "B1") == 12.5 && cell_value(b, 0, 1, 0)->t == V_BOOL && bad == 0);
  ok("sylk: formulas in A1", b && text_at(b, "B2", "=B1*2") && num_at(b, "B2") == 25 && text_at(b, "B4", "=B3*2") && num_at(b, "B4") == 100);
  ok("sylk: absolute and whole columns", b && text_at(b, "C1", "=SUM($B$1:B4)+ROUND($B:$B,0)"));
  ok("sylk: number format and width", b && book_fmt_code(b, b->styles[sheet_cell_style(b->sh[0], 1, 1)].numfmt)[0] == '0' && sheet_col_width(b->sh[0], 1) == 140);
  ok("sylk: R1C1 to A1", str_is(sylk_r1c1_to_a1("R[-1]C+RC[-1]+R1C1+R3+C2+'R1C1'!R2C2&\"RC\"+RATE(1,2,3)+R[1]C:R[2]C", 4, 4), "E4+D5+$A$1+$3:$3+$B:$B+'R1C1'!$B$2&\"RC\"+RATE(1,2,3)+E6:E7"));
  ok("sylk: A1 to R1C1", str_is(sylk_a1_to_r1c1("E4+D5+$A$1+$3:$3+$B:$B+Sheet2!$B$2&\"A1\"+LOG10(2)+Table1[Col1]+A:C", 4, 4), "R[-1]C+RC[-1]+R1C1+R3:R3+C2:C2+Sheet2!R2C2&\"A1\"+LOG10(2)+Table1[Col1]+C[-4]:C[-2]"));
  ok("sylk: off the sheet", str_is(sylk_r1c1_to_a1("R[-5]C", 0, 0), "#REF!"));
  out = b ? sylk_save(b, 0) : NULL;
  ok("sylk: saved", out && strstr(out, "ID;P") && strstr(out, "C;Y1;X1;K\"Item;;s\"") && strstr(out, ";ER[-1]C*2") && strstr(out, "P;P0.00") && strstr(out, "\r\nE\r\n"));
  d = out ? sylk_load(out, strlen(out), &bad) : NULL;
  ok("sylk: read back", d && bad == 0 && text_at(d, "C1", "=SUM($B$1:B4)+ROUND($B:$B,0)") && num_at(d, "B4") == 100 && text_at(d, "A1", "Item;s") && sheet_col_width(d->sh[0], 1) == 140);
  free(out);
  if (d) book_free(d);
  if (b) book_free(b);
  /* Latin-1 bytes */
  { const char *l1 = "ID;P\nC;Y1;X1;K\"Gr\xfc\xdf" "e\"\nE\n"; b = sylk_load(l1, strlen(l1), NULL); }
  ok("sylk: Latin-1 text", b && text_at(b, "A1", "Gr\xc3\xbc\xc3\x9f" "e"));
  if (b) book_free(b);
}

static void gc2_controls(void)
{
  Book *b = book_new(), *d;
  DObj *o;
  const char *why;
  int i, j;
  size_t len;
  u8 *x;
  PrimList pl;
  put(b, "B1", "=A1*2");
  i = book_control_add(b, 0, CT_SPIN, 0, 3);
  j = book_control_add(b, 0, CT_SCROLL, 4, 3);
  ok("controls: added", i == 0 && j == 1 && drw_obj(b->sh[0], i)->kind == OB_CONTROL && drw_obj(b->sh[0], j)->ctl == CT_SCROLL);
  ok("controls: limits checked", !book_control_set(b, 0, i, "$A$1", 5, 10, 2, 1, 10, &why) && !book_control_set(b, 0, i, "A1:A3", 5, 0, 20, 1, 10, &why) &&
     !book_control_set(b, 0, i, "$A$1", 5, 0, 40000, 1, 10, &why));
  ok("controls: format control sets the cell", book_control_set(b, 0, i, "$A$1", 5, 0, 20, 2, 10, &why) && num_at(b, "A1") == 5 && num_at(b, "B1") == 10);
  o = drw_obj(b->sh[0], i);
  ok("controls: spin parts", control_part(o, 16, 32, 8, 4, 5) == CP_UP && control_part(o, 16, 32, 8, 28, 5) == CP_DOWN);
  ok("controls: spin up and down", book_control_step(b, 0, i, CP_UP) && num_at(b, "A1") == 7 && book_control_step(b, 0, i, CP_DOWN) && book_control_step(b, 0, i, CP_DOWN) && num_at(b, "A1") == 3);
  ok("controls: stops at the ends", book_control_step(b, 0, i, CP_DOWN) && num_at(b, "A1") == 1 && book_control_step(b, 0, i, CP_DOWN) && num_at(b, "A1") == 0 && !book_control_step(b, 0, i, CP_DOWN));
  put(b, "A1", "12");
  ok("controls: the value follows the cell", control_value(b, 0, drw_obj(b->sh[0], i)) == 12);
  book_undo(b, NULL, NULL);
  ok("controls: undo", num_at(b, "A1") == 0);
  ok("controls: scroll bar", book_control_set(b, 0, j, "Sheet1!$C$1", 50, 0, 100, 1, 10, &why) && num_at(b, "C1") == 50 &&
     control_part(drw_obj(b->sh[0], j), 16, 100, 8, 2, 50) == CP_DOWN && control_part(drw_obj(b->sh[0], j), 16, 100, 8, 20, 50) == CP_PAGEDOWN &&
     control_part(drw_obj(b->sh[0], j), 16, 100, 8, 98, 50) == CP_UP && book_control_step(b, 0, j, CP_PAGEUP) && num_at(b, "C1") == 60);
  prims_init(&pl);
  ok("controls: drawn", control_render(b, 0, drw_obj(b->sh[0], j), 16, 100, &pl) && pl.n == 6);
  prims_free(&pl);
  book_insert_rows(b, 0, 0, 2);
  ok("controls: the link follows inserted rows", control_value(b, 0, drw_obj(b->sh[0], i)) == 0 && num_at(b, "C3") == 60);
  { char t[40]; control_link_text(b, 0, drw_obj(b->sh[0], j), t, sizeof t); ok("controls: link text", !strcmp(t, "$C$3")); }
  book_set_note(b, 0, 5, 5, "a note", "me");
  x = xlsx_save(b, &len);
  d = x ? xlsx_load(x, len, NULL, 0) : NULL;
  ok("controls: saved and read", d && drw_count(d->sh[0]) == 2 && drw_obj(d->sh[0], 0)->kind == OB_CONTROL && drw_obj(d->sh[0], 0)->ctl == CT_SPIN &&
     drw_obj(d->sh[0], 1)->ctl == CT_SCROLL && drw_obj(d->sh[0], 1)->cpage == 10 && drw_obj(d->sh[0], 0)->cinc == 2 && drw_obj(d->sh[0], 0)->cmax == 20 &&
     control_value(d, 0, drw_obj(d->sh[0], 1)) == 60 && drw_obj(d->sh[0], 1)->from.r == drw_obj(b->sh[0], j)->from.r && d->sh[0]->nnotes == 1);
  {
    /* what Excel needs to find: the parts and the sheet's elements */
    ZipReader z;
    int ok1 = 0;
    if (x && zip_open(&z, x, len)) {
      size_t l1, l2;
      int k1 = zip_find(&z, "xl/ctrlProps/ctrlProp2.xml"), k2 = zip_find(&z, "xl/worksheets/sheet1.xml");
      u8 *p1 = k1 >= 0 ? zip_read(&z, k1, &l1) : NULL, *p2 = k2 >= 0 ? zip_read(&z, k2, &l2) : NULL;
      ok1 = p1 && p2 && strstr((char *)p1, "objectType=\"Scroll\"") && strstr((char *)p1, "fmlaLink=\"$C$3\"") && strstr((char *)p2, "<controls>") && strstr((char *)p2, "legacyDrawing");
      free(p1); free(p2);
      zip_close(&z);
    }
    ok("controls: the file's parts", ok1);
  }
  free(x);
  if (d) book_free(d);
  book_free(b);
}

static char *zip_part(const u8 *x, size_t len, const char *name)
{
  ZipReader z;
  char *t = NULL;
  if (zip_open(&z, x, len)) {
    size_t l;
    int k = zip_find(&z, name);
    if (k >= 0) t = (char *)zip_read(&z, k, &l);
    zip_close(&z);
  }
  return t;
}
static void gc2_python(void)
{
  Book *b = book_new(), *d;
  Cell *c;
  u8 *x;
  size_t len;
  char *p1, *p2;
  put(b, "A1", "=PY(\"df = xl(\"\"B1:B2\"\")\",0)");
  ok("python: a new cell has no result", cell_value(b, 0, 0, 0)->t == V_ERR && cell_value(b, 0, 0, 0)->e == E_GETTING && text_at(b, "A1", "=PY(\"df = xl(\"\"B1:B2\"\")\",0)"));
  /* as if read from a file: a result of two rows */
  c = sheet_cell(b->sh[0], 0, 0);
  c->f->pycache = arr_new(2, 1);
  c->f->pycache->v[0] = val_num(3); c->f->pycache->v[1] = val_str("four");
  book_recalc(b);
  ok("python: the kept result spills", num_at(b, "A1") == 3 && text_at(b, "A2", "four"));
  put(b, "C1", "=A1*2");
  ok("python: other cells use it, it isn't run", num_at(b, "C1") == 6 && num_at(b, "A1") == 3);
  x = xlsx_save(b, &len);
  p1 = x ? zip_part(x, len, "xl/python.xml") : NULL;
  p2 = x ? zip_part(x, len, "xl/worksheets/sheet1.xml") : NULL;
  ok("python: saved as Excel's", p1 && strstr(p1, "<pythonScript><code>df = xl(&quot;B1:B2&quot;)</code></pythonScript>") && p2 && strstr(p2, "_xlfn._xlws.PY(0,0)"));
  free(p1); free(p2);
  d = x ? xlsx_load(x, len, NULL, 0) : NULL;
  ok("python: read back with its code and result", d && text_at(d, "A1", "=PY(\"df = xl(\"\"B1:B2\"\")\",0)") && num_at(d, "A1") == 3 && text_at(d, "A2", "four") && num_at(d, "C1") == 6);
  if (d) book_free(d);
  free(x);
  book_free(b);
  /* the index form with arguments */
  py_scripts_read("<python><pythonScripts><pythonScript><code>1 + 1</code></pythonScript><pythonScript><code>xl(\"%P2%\") + %P3%_x000D_</code></pythonScript></pythonScripts></python>", 161);
  p1 = py_from_file("_xlfn._xlws.PY(1,1,A1:B3,Sheet2!$C$4)");
  ok("python: %Px% arguments", p1 && !strcmp(p1, "PY(\"xl(\"\"A1:B3\"\") + Sheet2!$C$4\r\",1)"));
  free(p1);
  ok("python: no such script", !py_from_file("_xlfn._xlws.PY(7,0)") && !py_from_file("SUM(1,2)"));
  py_scripts_free();
}

static i32 eq_tw(void *ctx, const char *s, int px, int bold) { (void)ctx; (void)bold; return (i32)strlen(s) * px / 2; }
static void gc2_equation(void)
{
  Book *b = book_new(), *d;
  char *om, *t;
  int i;
  u8 *x;
  size_t len;
  PrimList pl;
  const char *q = "x = \\frac{-b \\pm \\sqrt{b^{2} - 4ac}}{2a}";
  om = eq_omml_from_latex(q);
  ok("equation: LaTeX to Office Math", om && strstr(om, "<m:f><m:num>") && strstr(om, "<m:rad><m:radPr><m:degHide m:val=\"1\"/>") && strstr(om, "<m:sSup><m:e><m:r><m:t>b</m:t>") &&
     strstr(om, "<m:t>\xc2\xb1</m:t>"));
  t = om ? eq_latex_from_omml(om, strlen(om)) : NULL;
  ok("equation: and back", t && !strcmp(t, "x=\\frac{-b\\pm \\sqrt{b^{2}-4ac}}{2a}"));
  free(t); free(om);
  {
    const char *xm = "<m:oMath xmlns:m=\"x\"><m:nary><m:naryPr><m:chr m:val=\"\xe2\x88\x91\"/></m:naryPr><m:sub><m:r><m:t>i=1</m:t></m:r></m:sub><m:sup><m:r><m:t>n</m:t></m:r></m:sup><m:e><m:sSub><m:e><m:r><m:t>x</m:t></m:r></m:e><m:sub><m:r><m:t>i</m:t></m:r></m:sub></m:sSub></m:e></m:nary>"
                         "<m:d><m:e><m:r><m:t>a</m:t></m:r></m:e><m:e><m:r><m:t>b</m:t></m:r></m:e></m:d><m:func><m:fName><m:r><m:t>sin</m:t></m:r></m:fName><m:e><m:r><m:t>\xce\xb8</m:t></m:r></m:e></m:func></m:oMath>";
    t = eq_latex_from_omml(xm, strlen(xm));
  }
  ok("equation: Excel's structures read", t && !strcmp(t, "\\sum _{i=1}^{n}x_{i}\\left(a|b\\right)\\sin \\theta"));
  free(t);
  i = book_equation_add(b, 0, q, 10, 10);
  ok("equation: inserted", i == 0 && drw_obj(b->sh[0], 0)->eq && drw_obj(b->sh[0], 0)->shape == SK_TEXTBOX);
  prims_init(&pl);
  ok("equation: drawn", eq_render(q, 200, 60, 15, FILL_ON, eq_tw, NULL, &pl) && pl.n > 10);
  {
    int k, lines = 0;
    for (k = 0; k < pl.n; k++) lines += pl.p[k].t == PR_LINE;
    ok("equation: fraction bar and radical", lines >= 5);
  }
  prims_free(&pl);
  x = xlsx_save(b, &len);
  t = x ? zip_part(x, len, "xl/drawings/drawing1.xml") : NULL;
  ok("equation: saved as Office Math with a fallback", t && strstr(t, "Requires=\"a14\"") && strstr(t, "<a14:m ") && strstr(t, "<m:oMathPara") && strstr(t, "<mc:Fallback>") && strstr(t, "\\frac{-b"));
  free(t);
  d = x ? xlsx_load(x, len, NULL, 0) : NULL;
  ok("equation: read back", d && drw_count(d->sh[0]) == 1 && drw_obj(d->sh[0], 0)->eq && drw_obj(d->sh[0], 0)->omml &&
     !strcmp(drw_obj(d->sh[0], 0)->text, "x=\\frac{-b\\pm \\sqrt{b^{2}-4ac}}{2a}"));
  free(x);
  if (d) {
    /* changed: the Office Math made again */
    DObj *o = drw_obj(d->sh[0], 0);
    free(o->text); o->text = str_dup("E = mc^2");
    x = xlsx_save(d, &len);
    t = x ? zip_part(x, len, "xl/drawings/drawing1.xml") : NULL;
    ok("equation: an edited equation's Office Math", t && strstr(t, "<m:sSup><m:e><m:r><m:t>c</m:t></m:r></m:e><m:sup><m:r><m:rPr><m:sty m:val=\"p\"/></m:rPr>") == NULL && strstr(t, "<m:t>c</m:t></m:r></m:e><m:sup><m:r><m:t>2</m:t>") && !strstr(t, "frac"));
    free(t); free(x);
    book_free(d);
  }
  book_free(b);
}

static void gc2_html(void)
{
  const char *page = "<!-- exported -->\n<html xmlns:x=\"urn:schemas-microsoft-com:office:excel\"><head><style>td{color:red}</style><x:Name>Sales</x:Name></head><body>"
                     "<p>Report</p><table border=1><tr><th>Region</th><th colspan=\"2\">Q1 &amp; Q2</th></tr>"
                     "<tr><td>North</td><td>1,200.50</td><td>27/10/2026</td></tr><tr><td rowspan=2>South &nbsp;x</td><td>=1+1</td><td>a<br>b</td></tr>"
                     "<tr><td>7</td><td><table><tr><td>in</td></tr></table></td></tr></table><table><tr><td>second</td></tr></table></body></html>";
  char nm[40];
  Book *b;
  ok("html: a page with a table", html_is(page, strlen(page)) && !html_is("a,b\n1,2", 7) && !html_is("<html><p>no table</p></html>", 28));
  b = html_load(page, strlen(page), nm, sizeof nm);
  ok("html: the sheet's name", b && !strcmp(nm, "Sales"));
  ok("html: cells read as typed", b && text_at(b, "A1", "Region") && text_at(b, "B1", "Q1 & Q2") && num_at(b, "B2") == 1200.5 && num_at(b, "C2") == date_serial(2026, 10, 27));
  ok("html: headers bold, spans merged", b && b->fonts[b->styles[sheet_cell_style(b->sh[0], 0, 0)].font].bold && b->sh[0]->nmerges == 2);
  ok("html: rowspan moves the next row's cells", b && num_at(b, "B4") == 7 && text_at(b, "C4", "in"));
  ok("html: text kept as text", b && text_at(b, "B3", "=1+1") && text_at(b, "C3", "a\nb") && text_at(b, "A3", "South \xc2\xa0x") && !sheet_cell(b->sh[0], 4, 0));
  if (b) book_free(b);
  /* Latin-1 */
  b = html_load("<table><tr><td>Gr\xfc\xdf" "e</td></tr></table>", 38, NULL, 0);
  ok("html: Latin-1 page", b && text_at(b, "A1", "Gr\xc3\xbc\xc3\x9f" "e"));
  if (b) book_free(b);
}

static void gc2_rtl(void)
{
  Book *b = book_new(), *d;
  u8 *x;
  size_t len;
  char *h;
  put(b, "A1", "1");
  b->sh[0]->rtl = 1;
  x = xlsx_save(b, &len);
  d = x ? xlsx_load(x, len, NULL, 0) : NULL;
  ok("right-to-left sheet kept", d && d->sh[0]->rtl);
  h = html_book(b, 0);
  ok("right-to-left sheet in a web page", h && strstr(h, "<table dir=\"rtl\">"));
  free(h); free(x);
  if (d) book_free(d);
  book_free(b);
}

static void gc2_listsort(void)
{
  Book *b = book_new();
  SortKey k = { 0, 0, SL_MONTHS };
  put(b, "A1", "Mar"); put(b, "A2", "zebra"); put(b, "A3", "january"); put(b, "A4", "Dec"); put(b, "A5", "apple"); put(b, "A6", "February");
  put(b, "B1", "3"); put(b, "B4", "12");
  ok("custom list sort: months in order, others after A to Z", book_sort(b, 0, R("A1:B6"), &k, 1, 0) && text_at(b, "A1", "january") && text_at(b, "A2", "February") &&
     text_at(b, "A3", "Mar") && num_at(b, "B3") == 3 && text_at(b, "A4", "Dec") && text_at(b, "A5", "apple") && text_at(b, "A6", "zebra"));
  k.list = SL_DAYS; k.desc = 1;
  put(b, "C1", "Mon"); put(b, "C2", "Sunday"); put(b, "C3", "Fri"); put(b, "C4", "x");
  k.col = 2;
  ok("custom list sort: days, the list reversed", book_sort(b, 0, R("C1:C4"), &k, 1, 0) && text_at(b, "C1", "Fri") && text_at(b, "C2", "Mon") && text_at(b, "C3", "Sunday") && text_at(b, "C4", "x"));
  book_free(b);
}

static void gc2_tests(void)
{
  gc2_listsort();
  gc2_rtl();
  gc2_html();
  gc2_equation();
  gc2_python();
  gc2_controls();
  gc2_sylk();
  gc2_find();
  gc2_csv_dates();
}
