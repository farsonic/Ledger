/*
 * htmlout.c - see htmlout.h. Each sheet becomes a table: columns as wide as on the sheet, merged
 * cells spanning, and each cell's look (font, colours, fill, alignment, borders, wrapping) as an
 * inline style; values as the cells show them.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "htmlout.h"
#include "calc.h"
#include "ops.h"

static void esc(Buf *o, const char *s)
{
  for (; *s; s++) {
    if (*s == '<') buf_adds(o, "&lt;");
    else if (*s == '>') buf_adds(o, "&gt;");
    else if (*s == '&') buf_adds(o, "&amp;");
    else if (*s == '"') buf_adds(o, "&quot;");
    else if (*s == '\n') buf_adds(o, "<br>");
    else buf_addc(o, *s);
  }
}
static const char *border_css(int k)
{
  switch (k) {
  case BS_THIN: return "1px solid"; case BS_MEDIUM: return "2px solid"; case BS_THICK: return "3px solid"; case BS_DOUBLE: return "3px double";
  case BS_DOTTED: return "1px dotted"; case BS_DASHED: return "1px dashed"; case BS_HAIR: return "1px dotted";
  }
  return NULL;
}
static void sheet_table(Book *b, int si, Buf *o)
{
  Sheet *s = b->sh[si];
  i32 lr = sheet_last_row(s), lc = sheet_last_col(s), r, c;
  int i;
  buf_adds(o, "<h2>"); esc(o, s->name); buf_adds(o, s->rtl ? "</h2>\n<table dir=\"rtl\">\n<colgroup>" : "</h2>\n<table>\n<colgroup>");
  for (c = 0; c <= lc; c++) if (sheet_col_width(s, c)) buf_printf(o, "<col style=\"width:%dpx\">", sheet_col_width(s, c));
  buf_adds(o, "</colgroup>\n");
  for (r = 0; r <= lr; r++) {
    if (!sheet_row_height(s, r)) continue;
    buf_printf(o, "<tr style=\"height:%dpx\">", sheet_row_height(s, r));
    for (c = 0; c <= lc; c++) {
      const Style *st = &b->styles[sheet_cell_style(s, r, c)];
      const Font *f = &b->fonts[st->font];
      const Value *v;
      Rect m;
      char t[1024];
      u32 col = COLOR_AUTO;
      int span = 0, ha;
      if (!sheet_col_width(s, c)) continue;
      if (sheet_merge_at(s, r, c, &m)) {
        if (m.r1 != r || m.c1 != c) continue;
        span = 1;
      }
      buf_adds(o, "<td");
      if (span) {
        if (m.c2 > m.c1) buf_printf(o, " colspan=\"%ld\"", (long)(m.c2 - m.c1 + 1));
        if (m.r2 > m.r1) buf_printf(o, " rowspan=\"%ld\"", (long)(m.r2 - m.r1 + 1));
      }
      v = cell_value(b, si, r, c);
      book_shown_text(b, si, r, c, t, sizeof t, &col);
      if (v->t == V_NUM && v->n == 0 && !s->showzeros) t[0] = 0;
      ha = st->halign;
      if (ha == HA_GENERAL) ha = v->t == V_NUM || v->t == V_ERR ? HA_RIGHT : v->t == V_BOOL ? HA_CENTER : HA_LEFT;
      buf_adds(o, " style=\"");
      if (strcmp(f->name, b->fonts[0].name)) { buf_adds(o, "font-family:'"); esc(o, f->name); buf_adds(o, "';"); }
      if (f->size != b->fonts[0].size) buf_printf(o, "font-size:%d.%dpt;", f->size / 10, f->size % 10);
      if (f->bold) buf_adds(o, "font-weight:bold;");
      if (f->italic) buf_adds(o, "font-style:italic;");
      if (f->underline || f->strike) buf_printf(o, "text-decoration:%s%s;", f->underline ? "underline " : "", f->strike ? "line-through" : "");
      if (col == COLOR_AUTO) col = f->color;
      if (col != COLOR_AUTO && !(col & 0xFF000000UL)) buf_printf(o, "color:#%06lX;", (unsigned long)(col & 0xFFFFFF));
      if (st->fill) buf_printf(o, "background:#%06lX;", (unsigned long)(st->fill & 0xFFFFFF));
      if (ha != HA_LEFT) buf_printf(o, "text-align:%s;", ha == HA_RIGHT ? "right" : ha == HA_JUSTIFY ? "justify" : "center");
      buf_printf(o, "vertical-align:%s;", st->valign == VA_TOP ? "top" : st->valign == VA_CENTER ? "middle" : "bottom");
      if (st->wrap) buf_adds(o, "white-space:pre-wrap;");
      if (st->indent) buf_printf(o, "padding-left:%dpx;", st->indent * 9);
      for (i = 0; i < 4; i++) {
        static const char *const side[4] = { "left", "right", "top", "bottom" };
        const char *bc = border_css(st->border[i]);
        if (bc) buf_printf(o, "border-%s:%s #%06lX;", side[i], bc, (unsigned long)(st->bcolor[i] == COLOR_AUTO ? 0 : st->bcolor[i] & 0xFFFFFF));
      }
      buf_adds(o, "\">");
      esc(o, t);
      buf_adds(o, "</td>");
    }
    buf_adds(o, "</tr>\n");
  }
  buf_adds(o, "</table>\n");
}
char *html_book(Book *b, int sheet)
{
  Buf o;
  int i;
  buf_init(&o);
  buf_adds(&o, "<!DOCTYPE html>\n<html><head><meta charset=\"utf-8\"><title>");
  esc(&o, b->title[0] ? b->title : sheet >= 0 ? b->sh[sheet]->name : "Workbook");
  buf_adds(&o, "</title>\n<meta name=\"generator\" content=\"Ledger for the Amiga\">\n<style>\nbody{font-family:'");
  esc(&o, b->fonts[0].name);
  buf_printf(&o, "',sans-serif;font-size:%d.%dpt}\ntable{border-collapse:collapse;table-layout:fixed}\ntd{padding:1px 3px;overflow:hidden;white-space:nowrap}\n</style>\n</head><body>\n", b->fonts[0].size / 10, b->fonts[0].size % 10);
  for (i = 0; i < b->nsh; i++) {
    if (sheet >= 0 && i != sheet) continue;
    if (sheet < 0 && b->sh[i]->hidden) continue;
    sheet_table(b, i, &o);
  }
  buf_adds(&o, "</body></html>\n");
  if (o.oom) { buf_free(&o); return NULL; }
  return buf_take(&o);
}
