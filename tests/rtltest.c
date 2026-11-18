/* rtltest.c - the grid's geometry (core/gridgeo.c): columns and rows laid out with frozen panes and
   split bars, positions, hits and heading edges, and right-to-left sheets mirrored (included by
   coretest.c) */
#include "../core/gridgeo.h"

/* columns 64 pixels wide, C hidden, E 100 */
static long rt_w(void *ctx, i32 i) { (void)ctx; return i == 2 ? 0 : i == 4 ? 100 : 64; }
static long rt_gap(void *ctx, i32 i) { (void)ctx; return i == 6 ? 10 : 0; }
static void rt_axis(GgAxis *a)
{
  a->head = 40; a->extent = 400; a->frozen = 0; a->first = 0; a->split = 0; a->sfirst = 0; a->before_active = 0;
  a->barw = 5; a->max = 16384; a->size = rt_w; a->gap = NULL; a->ctx = NULL;
}

static void rtl_layout(void)
{
  GgAxis a;
  i32 idx[64];
  long pos[65], bar;
  int n, nfix;
  rt_axis(&a);
  n = gg_layout(&a, idx, pos, 64, &nfix, &bar);
  ok("geometry: columns from the headings, a hidden one left out", n == 6 && nfix == 0 && !bar && idx[0] == 0 && pos[0] == 40 && idx[2] == 3 && pos[2] == 168 &&
     idx[3] == 4 && pos[4] == 332 && pos[6] == 460);
  ok("geometry: a column's place shown or not", gg_pos(&a, 0, 0) == 40 && gg_pos(&a, 0, 3) == 168 && gg_pos(&a, 0, 5) == 332);
  a.first = 3;
  ok("geometry: scrolled, a column before the first goes off to the left", gg_pos(&a, 0, 3) == 40 && gg_pos(&a, 0, 1) == -24 && gg_pos(&a, 0, 0) == -88);
  /* frozen panes: A and B stay, the rest scrolls from E */
  a.frozen = 2; a.first = 4;
  n = gg_layout(&a, idx, pos, 64, &nfix, &bar);
  ok("geometry: frozen columns first, then the scrolled ones", nfix == 2 && idx[0] == 0 && idx[1] == 1 && idx[2] == 4 && pos[2] == 168 && idx[3] == 5 && pos[3] == 268);
  ok("geometry: frozen pane positions", gg_pos(&a, 0, 1) == 104 && gg_pos(&a, 0, 4) == 168 && gg_pos(&a, 0, 3) == 104);
  ok("geometry: hit in the frozen and the scrolled part", gg_hit(idx, pos, n, nfix, 40, 50) == 0 && gg_hit(idx, pos, n, nfix, 40, 167) == 1 &&
     gg_hit(idx, pos, n, nfix, 40, 168) == 4 && gg_hit(idx, pos, n, nfix, 40, 10) == 4 && gg_hit(idx, pos, n, nfix, 40, 5000) == idx[n - 1]);
  ok("geometry: heading edges", gg_edge(idx, pos, n, 104, 5, 4) == 0 && gg_edge(idx, pos, n, 99, 5, 4) == 0 && gg_edge(idx, pos, n, 98, 5, 4) == -1 &&
     gg_edge(idx, pos, n, 270, 5, 4) == 4 && gg_shown(idx, n, 5) == 3 && gg_shown(idx, n, 3) == -1);
  /* View > Split: the left pane from B up to the bar at 100 pixels, the right one from G (with a gap before it) */
  a.frozen = 0; a.split = 100; a.sfirst = 1; a.first = 6; a.gap = rt_gap; a.before_active = 1;
  n = gg_layout(&a, idx, pos, 64, &nfix, &bar);
  ok("geometry: split, the left pane then the bar", bar == 140 && nfix == 2 && idx[0] == 1 && idx[1] == 3 && idx[2] == 6 && pos[2] == 155);
  ok("geometry: split, a column in the left pane when it is the active one", gg_in_first_pane(&a, bar, 1) && gg_in_first_pane(&a, bar, 3) && !gg_in_first_pane(&a, bar, 5) &&
     gg_pos(&a, bar, 3) == 104 && gg_pos(&a, bar, 6) == 145 && gg_pos(&a, bar, 7) == 209);
  a.split = 1000;
  gg_layout(&a, idx, pos, 64, &nfix, &bar);
  ok("geometry: the bar kept inside the grid", bar == 400 - 5 - 2);
}

static void rtl_mirror(void)
{
  GgAxis a;
  i32 idx[64];
  long pos[65], bar, x0, x1;
  int n, nfix;
  GgMirror m = { 1, 400, 0 }, off = { 0, 400, 0 }, win = { 1, 400, 30 };
  rt_axis(&a);
  n = gg_layout(&a, idx, pos, 64, &nfix, &bar);
  ok("right to left: x mirrored, the same both ways", gg_mx(&m, 0) == 399 && gg_mx(&m, 399) == 0 && gg_mx(&m, gg_mx(&m, 123)) == 123 && gg_mx(&off, 77) == 77);
  ok("right to left: mirrored in a window at its own offset", gg_mx(&win, 30) == 429 && gg_mx(&win, 429) == 30 && gg_mx(&win, gg_mx(&win, 100)) == 100);
  ok("right to left: column A at the right, next to the row headings", gg_item_span(&m, idx, pos, n, 0, &x0, &x1) && x0 == 296 && x1 == 359);
  ok("right to left: B to the left of A, columns growing leftwards", gg_item_span(&m, idx, pos, n, 1, &x0, &x1) && x0 == 232 && x1 == 295 &&
     gg_item_span(&m, idx, pos, n, 4, &x0, &x1) && x0 == 68 && x1 == 167);
  ok("right to left: left to right unchanged", gg_item_span(&off, idx, pos, n, 1, &x0, &x1) && x0 == 104 && x1 == 167 && !gg_item_span(&m, idx, pos, n, 2, &x0, &x1));
  ok("right to left: a click finds the column under it", gg_hit_screen(&m, idx, pos, n, nfix, 40, 359) == 0 && gg_hit_screen(&m, idx, pos, n, nfix, 40, 296) == 0 &&
     gg_hit_screen(&m, idx, pos, n, nfix, 40, 295) == 1 && gg_hit_screen(&m, idx, pos, n, nfix, 40, 0) == 6 && gg_hit_screen(&off, idx, pos, n, nfix, 40, 295) == 4);
  /* the row headings are on the right: logical x below the headings' width */
  ok("right to left: the row headings on the right", gg_mx(&m, 395) < 40 && gg_mx(&m, 360) == 39 && gg_mx(&m, 359) >= 40);
  /* a column's resize edge is its left one on the screen */
  ok("right to left: a column's edge to drag is on its left", gg_edge(idx, pos, n, gg_mx(&m, 296), 5, 4) == 0 && gg_edge(idx, pos, n, gg_mx(&m, 232), 5, 4) == 1);
  /* frozen panes: the frozen columns at the right, the scrolled ones to their left */
  a.frozen = 1; a.first = 3;
  n = gg_layout(&a, idx, pos, 64, &nfix, &bar);
  ok("right to left: a frozen column at the right, the scrolled ones from its left", gg_item_span(&m, idx, pos, n, 0, &x0, &x1) && x0 == 296 && x1 == 359 &&
     gg_item_span(&m, idx, pos, n, 3, &x0, &x1) && x0 == 232 && x1 == 295 && gg_hit_screen(&m, idx, pos, n, nfix, 40, 380) == 3);
  /* a selection's rectangle (pixels x0..x1 in logical) mirrored */
  x0 = pos[0]; x1 = pos[2] - 1;
  gg_mspan(&m, &x0, &x1);
  ok("right to left: a selection's box mirrored", x0 == 232 && x1 == 359);
  /* the scroll bar runs the other way */
  ok("right to left: the scroll bar's thumb from the right", gg_bar_first(1, 100, 10, 0) == 90 && gg_bar_first(1, 100, 10, 90) == 0 &&
     gg_bar_first(1, 100, 10, gg_bar_first(1, 100, 10, 37)) == 37 && gg_bar_first(0, 100, 10, 37) == 37 && gg_bar_first(1, 5, 10, 0) == 0);
  ok("right to left: arrow keys go the way they point", gg_key_dc(1, 1) == -1 && gg_key_dc(1, -1) == 1 && gg_key_dc(0, 1) == 1);
  /* alignment: General mirrors with the sheet (text right, numbers left); Left and Right stay */
  ok("right to left: General follows the sheet, Left and Right stay on their side", gg_halign(1, HA_GENERAL) == HA_GENERAL && gg_halign(1, HA_LEFT) == HA_RIGHT &&
     gg_halign(1, HA_RIGHT) == HA_LEFT && gg_halign(1, HA_CENTER) == HA_CENTER && gg_halign(0, HA_LEFT) == HA_LEFT);
}

static void rtl_tests(void)
{
  rtl_layout();
  rtl_mirror();
}
