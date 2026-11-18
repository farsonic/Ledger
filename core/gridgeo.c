/* gridgeo.c - see gridgeo.h */
#include "gridgeo.h"
#include "book.h"

static long sz(const GgAxis *a, i32 i) { return a->size(a->ctx, i); }

int gg_layout(const GgAxis *a, i32 *idx, long *pos, int cap, int *nfix, long *bar)
{
  long x = a->head;
  int n = 0;
  i32 i;
  *bar = 0;
  if (a->split > 0) {
    /* the pane before the bar, from its own first item up to the bar */
    long bx = a->head + a->split;
    if (bx > a->extent - a->barw - 2) bx = a->extent - a->barw - 2;
    if (bx < a->head + 2) bx = a->head + 2;
    for (i = a->sfirst; i < a->max && n < cap && x < bx; i++) {
      long s = sz(a, i);
      if (!s) continue;
      idx[n] = i; pos[n++] = x; x += s;
    }
    *bar = bx; x = bx + a->barw;
  } else
    for (i = 0; i < a->frozen && n < cap; i++) {
      long s = sz(a, i);
      if (!s) continue;
      idx[n] = i; pos[n++] = x; x += s;
    }
  *nfix = n;
  for (i = a->first; i < a->max && n < cap && x < a->extent; i++) {
    long s = sz(a, i);
    if (!s) continue;
    if (a->gap) x += a->gap(a->ctx, i);
    idx[n] = i; pos[n++] = x; x += s;
  }
  pos[n] = x;
  return n;
}

int gg_in_first_pane(const GgAxis *a, long bar, i32 i)
{
  long x = a->head;
  i32 k;
  if (!bar || !a->before_active || i < a->sfirst) return 0;
  for (k = a->sfirst; k < i && x <= bar; k++) x += sz(a, k);
  return x <= bar;
}

long gg_pos(const GgAxis *a, long bar, i32 i)
{
  long x = a->head;
  i32 k;
  if (bar) {
    if (gg_in_first_pane(a, bar, i)) { for (k = a->sfirst; k < i; k++) x += sz(a, k); return x; }
    x = bar + a->barw;
  } else {
    if (i < a->frozen) { for (k = 0; k < i; k++) x += sz(a, k); return x; }
    for (k = 0; k < a->frozen; k++) x += sz(a, k);
  }
  if (i >= a->first) { for (k = a->first; k < i && x < 30000; k++) x += sz(a, k); }
  else { for (k = i; k < a->first && x > -30000; k++) x -= sz(a, k); }
  return x;
}

i32 gg_hit(const i32 *idx, const long *pos, int n, int nfix, long head, long p)
{
  int i;
  i32 r = n ? idx[n - 1] : 0;
  for (i = 0; i < n; i++) if (p < pos[i + 1]) { r = idx[i]; break; }
  if (p < head && n) r = idx[nfix < n ? nfix : 0];
  return r;
}

i32 gg_edge(const i32 *idx, const long *pos, int n, long p, long lo, long hi)
{
  int i;
  for (i = n - 1; i >= 0; i--) if (p >= pos[i + 1] - lo && p <= pos[i + 1] + hi) return idx[i];
  return -1;
}

int gg_shown(const i32 *idx, int n, i32 i)
{
  int k;
  for (k = 0; k < n; k++) if (idx[k] == i) return k;
  return -1;
}

long gg_mx(const GgMirror *m, long x) { return m && m->on ? 2 * m->dx + m->w - 1 - x : x; }

void gg_mspan(const GgMirror *m, long *x0, long *x1)
{
  long a, b;
  if (!m || !m->on) return;
  a = gg_mx(m, *x1); b = gg_mx(m, *x0);
  *x0 = a; *x1 = b;
}

int gg_item_span(const GgMirror *m, const i32 *idx, const long *pos, int n, i32 item, long *x0, long *x1)
{
  int k = gg_shown(idx, n, item);
  if (k < 0) return 0;
  *x0 = pos[k]; *x1 = pos[k + 1] - 1;
  gg_mspan(m, x0, x1);
  return 1;
}

i32 gg_hit_screen(const GgMirror *m, const i32 *idx, const long *pos, int n, int nfix, long head, long sx)
{
  return gg_hit(idx, pos, n, nfix, head, gg_mx(m, sx));
}

long gg_bar_first(int rtl, long entries, long visible, long first)
{
  long f;
  if (!rtl) return first;
  f = entries - visible - first;
  return f < 0 ? 0 : f;
}

int gg_key_dc(int rtl, int dc) { return rtl ? -dc : dc; }

int gg_halign(int rtl, int ha)
{
  if (!rtl) return ha;
  if (ha == HA_LEFT) return HA_RIGHT;
  if (ha == HA_RIGHT) return HA_LEFT;
  return ha;
}
