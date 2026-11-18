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

static void gc2_tests(void)
{
  gc2_find();
}
