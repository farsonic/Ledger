/* splitpane.c - see splitpane.h */
#include <stdlib.h>
#include <string.h>
#include "splitpane.h"

int sheet_is_split(const Sheet *s) { return !s->freezer && !s->freezec && (s->splitx > 0 || s->splity > 0); }
void sheet_unsplit(Sheet *s)
{
  if (s->splity > 0 && s->stop < s->top) s->top = s->stop;
  if (s->splitx > 0 && s->sleft < s->left) s->left = s->sleft;
  s->splitx = s->splity = 0; s->stop = s->sleft = 0; s->apane = 0;
}
void sheet_split(Sheet *s, i32 x, i32 y, i32 stop, i32 sleft, i32 top, i32 left)
{
  s->splitx = x > 0 ? x : 0; s->splity = y > 0 ? y : 0;
  s->stop = stop < 0 ? 0 : stop; s->sleft = sleft < 0 ? 0 : sleft;
  s->top = top < 0 ? 0 : top; s->left = left < 0 ? 0 : left;
  if (!s->splity) s->stop = 0;
  if (!s->splitx) s->sleft = 0;
  s->apane = (u8)((s->splity ? 1 : 0) | (s->splitx ? 2 : 0));
}
const char *split_pane_name(const Sheet *s)
{
  static const char *const nm[4] = { "topLeft", "bottomLeft", "topRight", "bottomRight" };
  int p = s->apane & 3;
  if (!sheet_is_split(s)) return NULL;
  if (!s->splity) p &= 2;
  if (!s->splitx) p &= 1;
  return nm[p];
}
static int a1_ref(const char *s, i32 *r, i32 *c)
{
  int n = parse_col(s, c);
  long v = 0;
  if (!n) return 0;
  for (s += n; *s >= '0' && *s <= '9'; s++) { v = v * 10 + (*s - '0'); if (v > MAXROW) return 0; }
  if (v < 1 || *s) return 0;
  *r = (i32)(v - 1);
  return 1;
}
void xlsx_split_pane(Sheet *s, Xml *x)
{
  const char *v;
  long xs = 0, ys = 0;
  i32 r, c;
  if ((v = xml_attr(x, "xSplit"))) xs = atol(v);
  if ((v = xml_attr(x, "ySplit"))) ys = atol(v);
  s->splitx = xs > 0 ? (i32)(xs / 15 - SPLIT_HEADW) : 0;
  s->splity = ys > 0 ? (i32)(ys / 15 - SPLIT_HEADH) : 0;
  if (xs > 0 && s->splitx < 8) s->splitx = 8;
  if (ys > 0 && s->splity < 8) s->splity = 8;
  if (!s->splitx && !s->splity) return;
  /* the bottom right pane's first cell; the sheet view's (read before) is the top left pane's */
  if ((v = xml_attr(x, "topLeftCell")) && a1_ref(v, &r, &c)) { s->top = r; s->left = c; }
  if (!s->splity) s->stop = 0;
  if (!s->splitx) s->sleft = 0;
  s->apane = 3;
  if ((v = xml_attr(x, "activePane"))) s->apane = (u8)((strstr(v, "bottom") ? 1 : 0) | (strstr(v, "Right") ? 2 : 0));
}
void xlsx_split_write(Sheet *s, Buf *o)
{
  char ref[24];
  ref_text(s->top, s->left, 0, 0, ref);
  buf_adds(o, "<pane");
  if (s->splitx) buf_printf(o, " xSplit=\"%ld\"", (long)(s->splitx + SPLIT_HEADW) * 15);
  if (s->splity) buf_printf(o, " ySplit=\"%ld\"", (long)(s->splity + SPLIT_HEADH) * 15);
  buf_printf(o, " topLeftCell=\"%s\" activePane=\"%s\" state=\"split\"/>", ref, split_pane_name(s));
}
