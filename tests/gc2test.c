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

static void gc2_tests(void)
{
  gc2_find();
  gc2_csv_dates();
}
