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

static void ui_tests(void)
{
  ui_funcdesc();
  ui_groups();
}
