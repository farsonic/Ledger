/*
 * uitest.c - (included by coretest.c after drawtest.c and reload) the ui work: every function has
 * a description and category, groups of drawn objects, split panes in .xlsx, legacy formulas'
 * implicit intersection, CELL("filename"), GROUPBY/PIVOTBY subtotals.
 */
#include "../core/funcdesc.h"

static void ui_funcdesc(void)
{
  int i, missing = 0, sorted = 1, n[FC_N];
  const char *list[700];
  func_init();
  for (i = 0; i < func_count; i++) {
    const FuncDesc *fd = func_desc(func_table[i].name);
    if (!fd || !fd->what[0] || fd->cat == FC_ALL || fd->cat >= FC_N) { printf("   no description for %s\n", func_table[i].name); missing++; }
  }
  ok("every function in func_table has a description and a category", missing == 0);
  for (i = 1; i < func_ndescs; i++) if (strcmp(func_descs[i - 1].name, func_descs[i].name) >= 0) { sorted = 0; printf("   %s before %s\n", func_descs[i - 1].name, func_descs[i].name); }
  ok("descriptions in name order", sorted);
  for (i = 0; i < FC_N; i++) n[i] = func_descs_in(i, list, 700);
  ok("categories as Excel names them", !strcmp(func_cat_names[FC_ENG], "Engineering") && !strcmp(func_cat_names[FC_COMPAT], "Compatibility") && !strcmp(func_cat_names[FC_CUBE], "Cube"));
  ok("each category has functions", n[FC_STAT] > 90 && n[FC_ENG] > 40 && n[FC_DB] == 12 && n[FC_CUBE] == 7 && n[FC_WEB] == 3 && n[FC_INFO] > 15 && n[FC_COMPAT] > 30);
  ok("All lists them all", n[FC_ALL] == func_ndescs && func_ndescs >= func_count);
  ok("a description", func_desc("NORM.S.DIST") && func_desc("NORM.S.DIST")->cat == FC_STAT && func_desc("BESSELJ")->cat == FC_ENG && func_desc("XLOOKUP")->cat == FC_LOOKUP && !func_desc("NOSUCH"));
}

static DObj *ushape(Sheet *s, i32 x0, i32 y0, i32 x1, i32 y1)
{
  DObj *o = drw_new(s, OB_SHAPE);
  o->shape = SK_RECT; o->fill = FILL_ON | 0x4472C4;
  dobj_place(s, o, x0, y0, x1, y1);
  return o;
}
static void ui_groups(void)
{
  Book *b = book_new(), *d;
  Sheet *s = b->sh[0];
  int sel[4], mem[8], n;
  u32 g, g2;
  i32 x0, y0, x1, y1;
  ushape(s, 10, 10, 50, 30);      /* 0 */
  ushape(s, 200, 200, 240, 260);  /* 1 (not in a group) */
  ushape(s, 100, 50, 150, 90);    /* 2 */
  sel[0] = 0; sel[1] = 2;
  g = drw_group(s, sel, 2);
  ok("group made", g && drw_group_find(s, g) != NULL);
  /* the members together where the topmost one was */
  ok("members kept together", drw_top_group(s, 1) == g && drw_top_group(s, 2) == g && drw_top_group(s, 0) == 0 && drw_count(s) == 3);
  n = drw_unit(s, 2, mem, 8);
  ok("selecting one selects the group", n == 2 && mem[0] == 1 && mem[1] == 2);
  drw_group_box(s, g, &x0, &y0, &x1, &y1);
  ok("group box", x0 == 10 && y0 == 10 && x1 == 150 && y1 == 90);
  drw_group_place(s, g, 20, 20, 300, 180);       /* moved and twice the size */
  dobj_box(s, drw_obj(s, 1), &x0, &y0, &x1, &y1);
  ok("members stretched with the group", x0 == 20 && y0 == 20 && x1 == 100 && y1 == 60);
  dobj_box(s, drw_obj(s, 2), &x0, &y0, &x1, &y1);
  ok("the other member", x0 == 200 && y0 == 100 && x1 == 300 && y1 == 180);
  /* a nested group: the group and the lone shape */
  sel[0] = 0; sel[1] = 2;
  g2 = drw_group(s, sel, 2);
  ok("nested group", g2 && drw_group_find(s, g)->parent == g2 && drw_top_group(s, 1) == g2 && drw_in_group(s, 1, g) && drw_group_depth(s, g) == 2);
  ok("group of three", drw_group_members(s, g2, mem, 8) == 3);
  /* the round trip: grpSp in the drawing */
  d = reload(b);
  ok("groups saved and loaded", d && drw_count(d->sh[0]) == 3 && d->sh[0]->drw->ngrp == 2);
  if (d) {
    Sheet *t = d->sh[0];
    u32 top = drw_top_group(t, 0);
    int k, same = 1;
    for (k = 0; k < 3; k++) {
      i32 a0, b0, a1, b1;
      dobj_box(s, drw_obj(s, k), &x0, &y0, &x1, &y1);
      dobj_box(t, drw_obj(t, k), &a0, &b0, &a1, &b1);
      if (abs(a0 - x0) > 1 || abs(b0 - y0) > 1 || abs(a1 - x1) > 1 || abs(b1 - y1) > 1) { same = 0; printf("   object %d at %ld,%ld-%ld,%ld (want %ld,%ld-%ld,%ld)\n", k, (long)a0, (long)b0, (long)a1, (long)b1, (long)x0, (long)y0, (long)x1, (long)y1); }
    }
    ok("grouped objects in place after loading", same);
    ok("nesting kept", top && drw_top_group(t, 1) == top && drw_top_group(t, 2) == top && drw_group_depth(t, t->drw->o[1].group) == 2 && drw_group_find(t, top)->name[0]);
    book_free(d);
  }
  /* ungroup one level, then delete a group's members */
  ok("ungroup", drw_ungroup(s, g2) && !drw_group_find(s, g2) && drw_group_find(s, g)->parent == 0 && drw_top_group(s, 0) == 0);
  n = drw_unit(s, 1, mem, 8);
  drw_remove(s, mem[1]); drw_remove(s, mem[0]);
  ok("deleting its members takes the group away", drw_count(s) == 1 && s->drw->ngrp == 0);
  ok("too few to group", drw_group(s, sel, 1) == 0);
  book_free(b);
}


/* a formula as loaded from a file without the dynamic-array mark */
static void legacy_input(Book *b, const char *ref, const char *text)
{
  u16 sh; Rect r; u8 fl;
  Cell *c;
  parse_ref(b, ref, &sh, &r, &fl);
  book_input(b, 0, r.r1, r.c1, text, NULL);
  c = sheet_cell(b->sh[0], r.r1, r.c1);
  if (c && c->f) c->f->legacy = 1;
}
static double cnum(Book *b, const char *ref)
{
  u16 sh; Rect r; u8 fl;
  const Value *v;
  parse_ref(b, ref, &sh, &r, &fl);
  v = cell_value(b, 0, r.r1, r.c1);
  return !v ? -999999 : v->t == V_NUM ? v->n : v->t == V_ERR ? -1000 - v->e : v->t == V_BOOL ? v->e : -999999;
}
static void ui_legacy(void)
{
  Book *b = book_new();
  book_input(b, 0, 0, 1, "4", NULL); book_input(b, 0, 1, 1, "9", NULL); book_input(b, 0, 2, 1, "16", NULL);
  book_input(b, 0, 0, 3, "1", NULL); book_input(b, 0, 0, 4, "2", NULL);
  legacy_input(b, "C2", "=B1:B3*10");
  legacy_input(b, "C5", "=B1:B3*10");
  legacy_input(b, "C3", "=SQRT(B1:B3)");
  legacy_input(b, "C1", "=SUM(B1:B3*10)");
  legacy_input(b, "E4", "=D1:E1+1");
  legacy_input(b, "F2", "=SUM(B1:B3)");
  legacy_input(b, "G3", "=IF(B1:B3>10,1,0)");
  legacy_input(b, "H2", "=SUMPRODUCT(B1:B3*10)");
  legacy_input(b, "H3", "=ABS(-B1:B3)");
  book_input(b, 0, 1, 8, "=B1:B3*10", NULL);   /* I2: a dynamic array spills */
  book_recalc(b);
  ok("legacy: a range in an operator meets the formula's row", cnum(b, "C2") == 90);
  ok("legacy: no row in common is #VALUE!", cnum(b, "C5") == -1000 - E_VALUE);
  ok("legacy: a range for a single-value argument meets the row", near(cnum(b, "C3"), 4));
  ok("legacy: inside SUM too (no CSE)", cnum(b, "C1") == 40);
  ok("legacy: a row range meets the column", cnum(b, "E4") == 3);
  ok("legacy: a range argument stays a range", cnum(b, "F2") == 29);
  ok("legacy: IF's test intersects", cnum(b, "G3") == 1);
  ok("legacy: SUMPRODUCT takes arrays", cnum(b, "H2") == 290);
  ok("legacy: nested operators intersect", cnum(b, "H3") == 16);
  ok("dynamic: the same formula spills", cnum(b, "I2") == 40 && cnum(b, "I4") == 160);
  {
    Rect a; int blocked = 0;
    ok("a spilled cell knows its anchor", book_spill_range(b, 0, 2, 8, &a, &blocked) && a.r1 == 1 && a.c1 == 8 && a.r2 == 3 && !blocked);
    book_input(b, 0, 2, 8, "5", NULL);         /* typed over a spilled cell */
    book_recalc(b);
    ok("typing in a spill range gives #SPILL! on the anchor", cnum(b, "I2") == -1000 - E_SPILL && cnum(b, "I3") == 5);
    book_input(b, 0, 2, 8, "", NULL);
    book_recalc(b);
    ok("cleared, it spills again", cnum(b, "I3") == 90);
  }
  book_free(b);
}

static void ui_cellinfo(void)
{
  Book *b = book_new();
  ColInfo *ci;
  ok("CELL filename of a new book is empty", evs(b, "CELL(\"filename\")", ""));
  strcpy(b->path, "DH0:Work/Accounts/Budget.xlsx");
  ok("CELL filename as Excel writes it", evs(b, "CELL(\"filename\",A1)", "DH0:Work/Accounts/[Budget.xlsx]Sheet1"));
  strcpy(b->path, "Budget.xlsx");
  ok("CELL filename without a drawer", evs(b, "CELL(\"filename\")", "[Budget.xlsx]Sheet1"));
  ok("CELL width: characters", evn(b, "INDEX(CELL(\"width\",A1),1)") == 8);
  ok("CELL width: default", evn(b, "INDEX(CELL(\"width\",A1),2)") == 1);
  ci = sheet_col(b->sh[0], 1, 1); ci->width = 145;
  ok("CELL width of a sized column", evn(b, "INDEX(CELL(\"width\",B1),1)") == 20 && evn(b, "INDEX(CELL(\"width\",B1),2)") == 0);
  book_free(b);
}

static void ui_groupby(void)
{
  Book *b = book_new();
  static const char *const d[] = { "Region", "Product", "Sales", "East", "Pear", "5", "West", "Apple", "7", "East", "Apple", "10",
                                   "West", "Pear", "1", "East", "Pear", "2", "North", "Fig", "4" };
  int i;
  for (i = 0; i < 21; i++) book_input(b, 0, i / 3, i % 3, d[i], NULL);
  /* total_depth 2: a subtotal under each region, the grand total last */
  ok("GROUPBY subtotals", evs(b, "ARRAYTOTEXT(GROUPBY(A2:B7,C2:C7,SUM,0,2),1)",
     "{\"East\",\"Apple\",10;\"East\",\"Pear\",7;\"East\",,17;\"North\",\"Fig\",4;\"North\",,4;\"West\",\"Apple\",7;\"West\",\"Pear\",1;\"West\",,8;\"Total\",,29}"));
  ok("GROUPBY subtotals above (-2)", evs(b, "ARRAYTOTEXT(TAKE(GROUPBY(A2:B7,C2:C7,SUM,0,-2),3),1)", "{\"Total\",,29;\"East\",,17;\"East\",\"Apple\",10}"));
  ok("GROUPBY hierarchy sorts the second field within the first", evs(b, "ARRAYTOTEXT(TAKE(GROUPBY(A2:B7,C2:C7,SUM,0,0,-2),3),1)", "{\"East\",\"Pear\",7;\"East\",\"Apple\",10;\"North\",\"Fig\",4}"));
  ok("GROUPBY table relationship sorts the field on its own", evs(b, "ARRAYTOTEXT(TAKE(GROUPBY(A2:B7,C2:C7,SUM,0,0,-2,,1),3),1)", "{\"East\",\"Pear\",7;\"West\",\"Pear\",1;\"North\",\"Fig\",4}"));
  ok("GROUPBY sorted by a value within the hierarchy", evs(b, "ARRAYTOTEXT(TAKE(GROUPBY(A2:B7,C2:C7,SUM,0,0,-3),3),1)", "{\"East\",\"Apple\",10;\"East\",\"Pear\",7;\"West\",\"Apple\",7}"));
  ok("GROUPBY bad relationship", evn(b, "ROWS(GROUPBY(A2:B7,C2:C7,SUM,0,0,1,,2))") == -1000 - E_VALUE);
  ok("GROUPBY one field as before", evs(b, "ARRAYTOTEXT(GROUPBY(A2:A7,C2:C7,SUM),1)", "{\"East\",17;\"North\",4;\"West\",8;\"Total\",29}"));
  /* PIVOTBY: regions down with subtotals of nothing (one field), products across with a total */
  ok("PIVOTBY", evs(b, "ARRAYTOTEXT(PIVOTBY(A2:A7,B2:B7,C2:C7,SUM),1)",
     "{,\"Apple\",\"Fig\",\"Pear\",\"Total\";\"East\",10,,7,17;\"North\",,4,,4;\"West\",7,,1,8;\"Total\",17,4,8,29}"));
  ok("PIVOTBY row subtotals", evs(b, "ARRAYTOTEXT(INDEX(PIVOTBY(A2:B7,A2:A7,C2:C7,SUM,0,2,1,0),4,0),1)", "{\"East\",,17,,}"));
  ok("PIVOTBY column subtotals", evs(b, "ARRAYTOTEXT(INDEX(PIVOTBY(A2:A7,A2:B7,C2:C7,SUM,0,0,1,2),1,0),1)", "{,\"East\",\"East\",\"East\",\"North\",\"North\",\"West\",\"West\",\"West\",\"Total\"}"));
  book_free(b);
}

#include "../core/splitpane.h"
static void ui_split(void)
{
  Book *b = book_new(), *d;
  Sheet *s;
  book_add_sheet(b, "Two", -1);
  sheet_split(b->sh[0], 200, 120, 3, 1, 40, 12);
  b->sh[0]->apane = 2;                          /* the top right pane active */
  sheet_split(b->sh[1], 0, 90, 5, 0, 30, 2);    /* two panes, one above the other */
  ok("split", sheet_is_split(b->sh[0]) && !strcmp(split_pane_name(b->sh[0]), "topRight") && !strcmp(split_pane_name(b->sh[1]), "bottomLeft"));
  d = reload(b);
  ok("split panes saved and loaded", d && sheet_is_split(d->sh[0]) && sheet_is_split(d->sh[1]));
  if (d) {
    s = d->sh[0];
    ok("bars' places", s->splitx == 200 && s->splity == 120);
    ok("each pane's first cell", s->stop == 3 && s->sleft == 1 && s->top == 40 && s->left == 12 && s->apane == 2);
    s = d->sh[1];
    ok("a split one way", s->splitx == 0 && s->splity == 90 && s->stop == 5 && s->top == 30 && s->left == 2 && (s->apane & 1));
    book_free(d);
  }
  b->sh[0]->freezer = 2;
  ok("frozen panes come first", !sheet_is_split(b->sh[0]));
  b->sh[0]->freezer = 0;
  sheet_unsplit(b->sh[0]);
  ok("unsplit", !sheet_is_split(b->sh[0]) && b->sh[0]->top == 3 && b->sh[0]->left == 1);
  book_free(b);
}

static void ui_tests(void)
{
  ui_funcdesc();
  ui_groups();
  ui_legacy();
  ui_cellinfo();
  ui_groupby();
  ui_split();
}
