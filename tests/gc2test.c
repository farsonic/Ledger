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

static void gc2_tests(void)
{
  gc2_sylk();
  gc2_find();
  gc2_csv_dates();
}
