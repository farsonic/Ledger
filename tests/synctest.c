/*
 * synctest.c - tests of what came over from GridCraft 4c388df (2026-11-18), included by coretest.c:
 * ECMA.CEILING and GETPIVOTDATA, the functions' fixes for Excel's answers, and the file, editing and
 * chart changes of that update.
 */

static int ev_err(Book *b, const char *f, int e) { return evn(b, f) == -1000 - e; }

static void sync_functions(void)
{
  Book *b = book_new();
  ok("ECMA.CEILING", evn(b, "ECMA.CEILING(2.3,1)") == 3 && evn(b, "ECMA.CEILING(-2.3,-1)") == -3 && near(evn(b, "ECMA.CEILING(2.31,0.05)"), 2.35));
  ok("ECMA.CEILING takes two arguments", ev_err(b, "ECMA.CEILING(2.3)", E_VALUE) || evn(b, "ECMA.CEILING(2.3)") < -1000);
  /* distributions at their edges, as Excel */
  ok("HARMEAN without numbers is #N/A", ev_err(b, "HARMEAN({\"a\",\"b\"})", E_NA) && ev_err(b, "HARMEAN(0,1)", E_NUM));
  ok("T.DIST with 0 degrees", ev_err(b, "T.DIST(1,0.5,FALSE)", E_DIV0) && ev_err(b, "T.DIST(1,0.5,TRUE)", E_NUM) && ev_err(b, "T.DIST(1,-1,FALSE)", E_NUM));
  ok("T.INV of 0", ev_err(b, "T.INV(0,5)", E_DIV0) && ev_err(b, "T.INV(-0.1,5)", E_NUM));
  ok("GAMMA.DIST density at 0", ev_err(b, "GAMMA.DIST(0,1,2,FALSE)", E_NUM) && evn(b, "GAMMA.DIST(0,2,2,FALSE)") == 0 && evn(b, "GAMMA.DIST(0,1,2,TRUE)") == 0);
  ok("WEIBULL.DIST at 0", evn(b, "WEIBULL.DIST(0,0.5,1,FALSE)") == 0 && evn(b, "WEIBULL.DIST(0,1,2,FALSE)") == 0 && evn(b, "WEIBULL.DIST(0,1,2,TRUE)") == 0);
  ok("BETA.DIST density at its ends", ev_err(b, "BETA.DIST(0,1,2,FALSE)", E_NUM) && ev_err(b, "BETA.DIST(1,2,1,FALSE)", E_NUM) && evn(b, "BETA.DIST(0,2,2,FALSE)") == 0 && evn(b, "BETA.DIST(1,2,1,TRUE)") == 1);
  ok("BINOM.DIST trial limit", ev_err(b, "BINOM.DIST(1,2147483647,0.5,FALSE)", E_NUM) && ev_err(b, "BINOM.DIST.RANGE(2147483647,0.5,1)", E_NUM) && evn(b, "BINOM.DIST(1,2147483646,0.5,FALSE)") == 0);
  /* Loader's masses, against 40-digit values */
  ok("BINOM.DIST far out", near(evn(b, "BINOM.DIST(5000000,10000000,0.5,FALSE)") / 0.00025231324589418478, 1) && fabs(evn(b, "BINOM.DIST(500,1000,0.5,FALSE)") / 0.025225018178360802 - 1) < 1e-14);
  ok("POISSON.DIST far out", fabs(evn(b, "POISSON.DIST(1000,1000,FALSE)") / 0.0126146113487215 - 1) < 1e-13 && near(evn(b, "POISSON.DIST(3,2.5,FALSE)"), 0.21376301724973645));
  ok("T.DIST density at huge degrees", fabs(evn(b, "T.DIST(0.5,10000000000,FALSE)") / 0.35206532675164713 - 1) < 1e-14 && fabs(evn(b, "T.DIST(0.5,100000000,FALSE)") / 0.35206532549906471 - 1) < 1e-14);
  ok("PERCENTRANK significance", ev_err(b, "PERCENTRANK({1,2,3},2.5,309)", E_DIV0) && evn(b, "PERCENTRANK({1,2,3,7},2.5,20)") == 0.5 && near(evn(b, "PERCENTRANK({1,2,3},2.5,3)"), 0.75));
  ok("F.DIST at 1E10 degrees", evn(b, "F.DIST(1,10000000000,10000000000,TRUE)") > 0.49 && ev_err(b, "F.DIST(1,10000000001,1,TRUE)", E_NUM));
  ok("MOD quotient limit", ev_err(b, "MOD(1125900000000,1)", E_NUM) && evn(b, "MOD(1125800000000,7)") == dmod(1125800000000.0, 7));
  ok("YEARFRAC 30/360 from February's end", near(evn(b, "YEARFRAC(DATE(2021,2,28),DATE(2021,3,31),0)"), 31.0 / 360) && near(evn(b, "YEARFRAC(DATE(2021,3,31),DATE(2021,2,28))"), 31.0 / 360) &&
     evn(b, "DAYS360(DATE(2021,2,28),DATE(2021,3,31))") == 30);
  /* LINEST keeps a predictor next to a large offset; standard errors after centring */
  put(b, "A1", "1"); put(b, "A2", "3"); put(b, "A3", "4.5");
  put(b, "B1", "10000000000000"); put(b, "B2", "10000000000001"); put(b, "B3", "10000000000002");
  put(b, "C1", "1"); put(b, "C2", "2"); put(b, "C3", "4"); put(b, "D1", "0.1"); put(b, "D2", "0.1"); put(b, "D3", "0.1");
  ok("LINEST with a large offset", near(evn(b, "INDEX(LINEST(A1:A3,B1:B3),1)"), 1.75) && near(evn(b, "SLOPE(A1:A3,B1:B3)"), 1.75));
  ok("LINEST drops a constant column", evn(b, "INDEX(LINEST(A1:A3,D1:D3),1)") == 0 && near(evn(b, "INDEX(LINEST(A1:A3,D1:D3),2)"), 8.5 / 3));
  ok("LINEST standard errors", near(evn(b, "INDEX(LINEST(A1:A3,C1:C3,TRUE,TRUE),2,1)"), 0.30929478706587) && near(evn(b, "INDEX(LINEST(A1:A3,C1:C3,TRUE,TRUE),2,2)"), 0.818317088384971) &&
     near(evn(b, "INDEX(LINEST(A1:A3,C1:C3,TRUE,TRUE),1,1)"), 1.10714285714286));
  /* text */
  ok("FIXED with 15 significant digits", evs(b, "FIXED(1.005,20)", "1.00500000000000000000") && evs(b, "FIXED(1/3,20)", "0.33333333333333300000") && evs(b, "FIXED(1.1,16)", "1.1000000000000000"));
  ok("FIXED of a large number", evs(b, "FIXED(123456789012345678,0)", "123,456,789,012,346,000") && evs(b, "TEXT(123456789012345678,\"0\")", "123456789012346000"));
  ok("FIXED and DOLLAR length limits", evn(b, "LEN(FIXED(1E191,0))") == 255 && ev_err(b, "FIXED(1E192,0)", E_VALUE) && evn(b, "LEN(DOLLAR(1E189,0))") == 254 &&
     ev_err(b, "DOLLAR(1E190,0)", E_VALUE) && evn(b, "LEN(FIXED(-1E253,0,TRUE))") == 255 && ev_err(b, "FIXED(-1E191,0)", E_VALUE));
  ok("DOLLAR rounding to 0", evs(b, "DOLLAR(-0.0001,0)", "($0)") && evs(b, "DOLLAR(-0.0001,2)", "($0.00)") && evs(b, "DOLLAR(-0.0001,-2)", "$0"));
  ok("one character to one in case changes", evs(b, "LOWER(\"\xC4\xB0\")", "i") && evs(b, "PROPER(\"\xC4\xB0STANBUL\")", "Istanbul") && evs(b, "UPPER(\"Stra\xC3\x9F" "e\")", "STRA\xC3\x9F" "E") &&
     evs(b, "UPPER(\"\xC5\x82\xC3\xB3" "d\xC5\xBA\")", "\xC5\x81\xC3\x93" "D\xC5\xB9") && evs(b, "LOWER(\"\xC5\xB8\")", "\xC3\xBF"));
  ok("UNICHAR noncharacters", ev_err(b, "UNICHAR(65534)", E_NA) && ev_err(b, "UNICHAR(64976)", E_NA) && ev_err(b, "UNICHAR(131071)", E_NA) && ev_err(b, "UNICHAR(55296)", E_NA) && evs(b, "UNICHAR(65)", "A"));
  /* engineering */
  ok("IMSQRT by Excel's polar route", evs(b, "IMSQRT(\"-4\")", "1.22514845490862E-16+2i") && evs(b, "IMSQRT(\"i\")", "0.707106781186548+0.707106781186547i") &&
     evs(b, "IMSQRT(\"-5+12i\")", "2+3i") && evs(b, "IMSQRT(\"-4-1E-20i\")", "1.22514845490862E-16-2i") && evs(b, "IMSQRT(\"4\")", "2"));
  ok("0 shifted any distance", evn(b, "BITLSHIFT(0,60)") == 0 && evn(b, "BITRSHIFT(0,-60)") == 0 && ev_err(b, "BITLSHIFT(1,60)", E_NUM));
  ok("TRUE as places", ev_err(b, "DEC2BIN(1,TRUE)", E_VALUE) && ev_err(b, "DEC2HEX(-1,TRUE)", E_VALUE) && evs(b, "DEC2BIN(5,4)", "0101"));
  book_free(b);
}

/* GETPIVOTDATA on the sales data's pivot table: Region in Rows, Product in Columns, Sum of Sales */
static void sync_getpivotdata(void)
{
  Book *b = book_new();
  LPivot np;
  int sh;
  sales_data(b);
  sh = book_add_sheet(b, "Pivot", -1);
  book_pivot_add(b, sh, "Sheet1!$A$1:$D$7", 2, 0);
  pivot_copy1(&np, &b->sh[sh]->pivots[0]);
  pivot_def_add(&np, PA_ROWS, "Region", 0);
  pivot_def_add(&np, PA_COLS, "Product", 0);
  pivot_def_add(&np, PA_VALUES, "Sales", PF_SUM);
  book_pivot_set(b, sh, 0, &np);
  pivot_free1(&np);
  ok("GETPIVOTDATA grand total", evn(b, "GETPIVOTDATA(\"Sales\",Pivot!A3)") == 625 && evn(b, "GETPIVOTDATA(\"Sum of Sales\",Pivot!C5)") == 625);
  ok("GETPIVOTDATA one item", evn(b, "GETPIVOTDATA(\"Sales\",Pivot!A3,\"Region\",\"East\")") == 180 && evn(b, "GETPIVOTDATA(\"Sales\",Pivot!A3,\"product\",\"Apples\")") == 325);
  ok("GETPIVOTDATA where two meet", evn(b, "GETPIVOTDATA(\"Sales\",Pivot!A3,\"Region\",\"West\",\"Product\",\"Pears\")") == 250 &&
     evn(b, "GETPIVOTDATA(\"Sales\",Pivot!A3,\"Product\",\"Apples\",\"Region\",\"East\")") == 130);
  ok("GETPIVOTDATA #REF!", ev_err(b, "GETPIVOTDATA(\"Sales\",Pivot!A3,\"Region\",\"South\")", E_REF) && ev_err(b, "GETPIVOTDATA(\"Units\",Pivot!A3)", E_REF) &&
     ev_err(b, "GETPIVOTDATA(\"Sales\",Sheet1!A1)", E_REF) && ev_err(b, "GETPIVOTDATA(\"Sales\",Pivot!A3,\"Units\",\"3\")", E_REF) &&
     ev_err(b, "GETPIVOTDATA(\"Sales\",Pivot!A3,\"North\",\"Pears\")", E_REF));
  ok("GETPIVOTDATA an item missing where two meet", ev_err(b, "GETPIVOTDATA(\"Sales\",Pivot!A3,\"Region\",\"North\",\"Product\",\"Pears\")", E_REF));
  /* follows a change in the source (volatile) */
  put(b, "Sheet1!C2", "200");
  book_recalc(b);
  ok("GETPIVOTDATA follows the source", evn(b, "GETPIVOTDATA(\"Sales\",Pivot!A3,\"Region\",\"East\")") == 280);
  book_free(b);
}

/* a saved workbook replaces the old file only once it is written in full (#3178) */
static void sync_safe_save(void)
{
  const char *path = "ledger_safe_save_test.xlsx";
  Book *b = book_new(), *c;
  u8 *x, *rd;
  size_t n, rn;
  FILE *fp;
  put(b, "A1", "42");
  x = xlsx_save(b, &n);
  ok("safe save: a new file", x && file_write_safe(path, x, n));
  put(b, "A1", "43");
  free(x);
  x = xlsx_save(b, &n);
  ok("safe save: the old one replaced", x && file_write_safe(path, x, n));
  rd = file_read(path, &rn);
  c = rd ? xlsx_load(rd, rn, NULL, 0) : NULL;
  ok("safe save: the new contents", rd && rn == n && c && num_at(c, "A1") == 43);
  ok("safe save: no temporary file left", !(fp = fopen("Ledger.tmp", "rb")));
  if (fp) fclose(fp);
  ok("safe save: a drawer that isn't there fails", !file_write_safe("no_such_drawer_here/x.xlsx", x, n));
  remove(path);
  free(rd); free(x);
  if (c) book_free(c);
  book_free(b);
}

/* a package part holds a text */
static int part_has(const u8 *x, size_t len, const char *part, const char *text)
{
  ZipReader z;
  int i, r = 0;
  size_t n;
  u8 *d;
  if (!zip_open(&z, x, len)) return 0;
  if ((i = zip_find(&z, part)) >= 0 && (d = zip_read(&z, i, &n))) {
    size_t k, tl = strlen(text);
    for (k = 0; k + tl <= n && !r; k++) if (!memcmp(d + k, text, tl)) r = 1;
    free(d);
  }
  zip_close(&z);
  return r;
}
/* Format Chart's Chart type (GridCraft #2605): a column chart switched to the stacked and 100%
   stacked forms of column, bar and area, and stacked line, keeps its series, draws, and is saved
   with Excel's grouping */
static void sync_chart_types(void)
{
  static const int kinds[] = { CK_COLSTACK, CK_COL100, CK_BARSTACK, CK_BAR100, CK_LINESTACK, CK_AREASTACK, CK_AREA100 };
  static const char *const grouping[] = { "\"stacked\"", "\"percentStacked\"", "\"stacked\"", "\"percentStacked\"", "\"stacked\"", "\"stacked\"", "\"percentStacked\"" };
  int i, bad = 0;
  for (i = 0; i < 7; i++) {
    Book *e = sales_book(), *d = NULL;
    DObj *eo = add_chart(e, 0, CK_COL, R4(0, 0, 6, 3)), *o;
    PrimList pl;
    u8 *x = NULL; size_t len = 0; char err[100];
    if (!eo) { bad++; book_free(e); continue; }
    eo->chart->kind = (u8)kinds[i];
    prims_init(&pl);
    if (!chart_render(e, 0, eo->chart, 400, 240, 100, NULL, NULL, &pl) || count_series_prims(&pl, 0) < 1) bad++;
    prims_free(&pl);
    x = xlsx_save(e, &len);
    if (!x || !part_has(x, len, "xl/charts/chart1.xml", grouping[i])) bad++;
    d = x ? xlsx_load(x, len, err, sizeof err) : NULL;
    o = d ? drw_obj(d->sh[0], 0) : NULL;
    if (!o || !o->chart || o->chart->kind != kinds[i] || o->chart->ns != eo->chart->ns) { bad++; printf("   %s\n", chart_kind_name(kinds[i])); }
    if (d) book_free(d);
    free(x);
    book_free(e);
  }
  ok("chart type: stacked and 100% stacked forms", bad == 0);
}

/* Insert > WordArt (GridCraft #784): a styled text shape, saved with GridCraft's textNoShape marker */
static void sync_wordart(void)
{
  Book *b = book_new(), *d;
  int i, j;
  DObj *o;
  u8 *x;
  size_t len;
  char err[100];
  i = book_wordart_add(b, 0, 2, NULL, 64, 40);          /* Sunrise: orange, a brown outline */
  o = drw_obj(b->sh[0], i);
  ok("WordArt: made", i == 0 && o && o->kind == OB_SHAPE && o->shape == SK_TEXTBOX && o->wordart && !strcmp(o->text, "Your text here") &&
     o->tcolor == (FILL_ON | 0xF2A33A) && o->toutline == (FILL_ON | 0xB5451B) && o->toutw == 150 && o->fsize == 400 && o->bold && !o->fill && !o->line);
  j = book_wordart_add(b, 0, 9, "Lime", 64, 200);       /* Lime Pop: outline and shadow, italic */
  ok("WordArt: a second with its text", j == 1 && drw_obj(b->sh[0], 1)->tshadow == (FILL_ON | 0x6E8B2C) && drw_obj(b->sh[0], 1)->italic);
  o = drw_new(b->sh[0], OB_SHAPE);                      /* an ordinary rectangle stays one */
  o->shape = SK_RECT; o->fill = FILL_ON | 0x4472C4; o->text = str_dup("box");
  x = xlsx_save(b, &len);
  ok("WordArt: the marker saved", x && part_has(x, len, "xl/drawings/drawing1.xml", "prstTxWarp prst=\"textNoShape\"") && part_has(x, len, "xl/drawings/drawing1.xml", "outerShdw"));
  d = x ? xlsx_load(x, len, err, sizeof err) : NULL;
  ok("WordArt: back from the file", d && drw_count(d->sh[0]) == 3 && drw_obj(d->sh[0], 0)->wordart && drw_obj(d->sh[0], 0)->toutline == (FILL_ON | 0xB5451B) &&
     drw_obj(d->sh[0], 0)->toutw == 150 && drw_obj(d->sh[0], 1)->tshadow == (FILL_ON | 0x6E8B2C) && !strcmp(drw_obj(d->sh[0], 1)->text, "Lime") &&
     drw_obj(d->sh[0], 1)->fsize == 400 && !drw_obj(d->sh[0], 2)->wordart && drw_obj(d->sh[0], 2)->shape == SK_RECT);
  if (d) book_free(d);
  free(x);
  ok("WordArt: undone", book_undo(b, NULL, NULL) && drw_count(b->sh[0]) == 1);   /* (the second one; the rectangle came without an undo step) */
  book_protect_sheet(b, 0, "", PA_DEFAULT);
  ok("WordArt: not on a protected sheet", book_wordart_add(b, 0, 0, "x", 0, 0) < 0 && drw_count(b->sh[0]) == 1);
  book_protect_sheet(b, 0, "", PA_DEFAULT | PA_OBJECTS);
  ok("WordArt: unless objects may be edited", book_wordart_add(b, 0, 0, "x", 0, 0) == 1);
  book_free(b);
}

/* a hidden Total Row keeps its calculations through a file (GridCraft #4383) */
static void sync_hidden_totals(void)
{
  Book *b = book_new(), *d;
  int ti;
  u8 *x;
  size_t len;
  char err[100];
  sales_data(b);
  ti = book_table_add(b, 0, rc4(0, 0, 6, 3), 1, "");
  book_table_option(b, 0, ti, TO_TOTALS, 1);
  book_table_total(b, 0, ti, 2, TF_AVERAGE);
  book_table_option(b, 0, ti, TO_TOTALS, 0);
  x = xlsx_save(b, &len);
  d = x ? xlsx_load(x, len, err, sizeof err) : NULL;
  ok("hidden Total Row: its functions kept", d && d->sh[0]->ntables == 1 && !d->sh[0]->tables[0].totals && d->sh[0]->tables[0].cols[3].totfn == TF_SUM &&
     d->sh[0]->tables[0].cols[2].totfn == TF_AVERAGE && d->sh[0]->tables[0].cols[0].totlabel && !strcmp(d->sh[0]->tables[0].cols[0].totlabel, "Total"));
  if (d) {
    book_table_option(d, 0, 0, TO_TOTALS, 1);
    ok("hidden Total Row: shown again with them", num_at(d, "D8") == 57 && num_at(d, "C8") == 625.0 / 6 && text_at(d, "A8", "Total"));
    book_free(d);
  }
  free(x);
  book_free(b);
}

static void numfmt_edit(Style *st, Font *f, const void *arg) { (void)f; st->numfmt = *(const u16 *)arg; }
static void book_restyle_fmt(Book *b, const char *ref, const char *code)
{
  u16 id = (u16)book_fmt(b, code), sh; Rect r; u8 fl;
  parse_ref(b, ref, &sh, &r, &fl);
  book_restyle(b, 0, r, numfmt_edit, &id);
}
/* Set precision as displayed (GridCraft #1366): numbers kept as their formats show them */
static void sync_precision(void)
{
  Book *b = book_new(), *d;
  u8 *x;
  size_t len;
  char err[100];
  int pct;
  put(b, "A1", "1.236"); put(b, "A2", "=A1*10"); put(b, "B1", "0.12345"); put(b, "C1", "=1/3"); put(b, "D1", "2.5");
  book_restyle_fmt(b, "A1:A2", "0.00"); book_restyle_fmt(b, "B1", "0.0%"); book_restyle_fmt(b, "C1", "0.000");
  book_recalc(b);
  ok("precision as displayed: off by default", num_at(b, "A1") == 1.236 && near(num_at(b, "A2"), 12.36) && !book_precdisp(b));
  ok("precision as displayed: turned on", book_set_precdisp(b, 1) && num_at(b, "A1") == 1.24 && near(num_at(b, "A2"), 12.4) && num_at(b, "B1") == 0.123 && num_at(b, "C1") == 0.333 && num_at(b, "D1") == 2.5);
  put(b, "A3", "3.14159"); book_restyle_fmt(b, "A3", "#,##0.0");
  put(b, "A4", "2.71828");
  ok("precision as displayed: typed and reformatted", num_at(b, "A3") == 3.1 && num_at(b, "A4") == 2.71828);
  pct = num_at(b, "A2") == 12.4;
  x = xlsx_save(b, &len);
  ok("precision as displayed: saved as fullPrecision=0", pct && x && part_has(x, len, "xl/workbook.xml", "fullPrecision=\"0\""));
  d = x ? xlsx_load(x, len, err, sizeof err) : NULL;
  ok("precision as displayed: read back", d && book_precdisp(d));
  if (d) book_free(d);
  free(x);
  ok("precision as displayed: undone, step by step", book_undo(b, NULL, NULL) && book_undo(b, NULL, NULL) && book_undo(b, NULL, NULL) && book_undo(b, NULL, NULL) && num_at(b, "A1") == 1.236);
  book_set_precdisp(b, 0);
  put(b, "A5", "1.23456"); book_restyle_fmt(b, "A5", "0.0");
  ok("precision as displayed: off again", num_at(b, "A5") == 1.23456);
  book_free(b);
}

/* a typed number is the nearest double, as a formula's literal (it was added up digit by digit) */
static void sync_typed_numbers(void)
{
  Book *b = book_new();
  put(b, "A1", "0.3"); put(b, "A2", "1.23456"); put(b, "A3", "1,234.5e2"); put(b, "A4", "12%"); put(b, "A5", "($1,000.25)");
  ok("typed numbers: the nearest double", num_at(b, "A1") == 0.3 && num_at(b, "A2") == 1.23456 && num_at(b, "A3") == 123450 && num_at(b, "A4") == 0.12 && num_at(b, "A5") == -1000.25);
  ok("typed numbers: MATCH finds them", evn(b, "MATCH(1.23456,A2,0)") == 1 && evn(b, "A1-0.3") == 0);
  book_free(b);
}

/* Fill Series: the stop value itself at the end, without the steps' drift */
static void sync_series(void)
{
  Book *b = book_new();
  put(b, "A1", "0");
  book_series(b, 0, rc4(0, 0, 9, 0), 0, 0.1, 0.3);
  ok("Fill Series ends on its stop", num_at(b, "A4") == 0.3 && cell_value(b, 0, 4, 0)->t == V_EMPTY && near(num_at(b, "A3"), 0.2));
  book_free(b);
}

static void sync_tests(void)
{
  sync_series();
  sync_typed_numbers();
  sync_precision();
  sync_hidden_totals();
  sync_wordart();
  sync_chart_types();
  sync_safe_save();
  sync_functions();
  sync_getpivotdata();
}
