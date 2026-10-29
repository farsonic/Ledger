/*
 * drwgroup.c - groups of drawn objects (see drawing.h): an object names the innermost group it is
 * in, a group its parent, so a group's members are found by walking up from each object. Members
 * are kept next to each other in the stacking order, so a group is drawn, saved (as a grpSp in the
 * drawing part) and moved as one. Portable C99.
 */
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "drawing.h"

#define MAXDEPTH 32

DGroup *drw_group_find(Sheet *s, u32 id)
{
  int i;
  if (!id || !s->drw) return NULL;
  for (i = 0; i < s->drw->ngrp; i++) if (s->drw->grp[i].id == id) return &s->drw->grp[i];
  return NULL;
}
/* the groups object i is in, innermost first; how many */
static int chain(Sheet *s, int i, u32 *out)
{
  DObj *o = drw_obj(s, i);
  DGroup *g;
  int n = 0;
  if (!o) return 0;
  for (g = drw_group_find(s, o->group); g && n < MAXDEPTH; g = drw_group_find(s, g->parent)) out[n++] = g->id;
  return n;
}
u32 drw_top_group(Sheet *s, int i)
{
  u32 c[MAXDEPTH];
  int n = chain(s, i, c);
  return n ? c[n - 1] : 0;
}
int drw_in_group(Sheet *s, int i, u32 gid)
{
  u32 c[MAXDEPTH];
  int n = chain(s, i, c), k;
  for (k = 0; k < n; k++) if (c[k] == gid) return 1;
  return 0;
}
int drw_group_depth(Sheet *s, u32 gid)
{
  DGroup *g;
  int n = 0;
  for (g = drw_group_find(s, gid); g && n < MAXDEPTH; g = drw_group_find(s, g->parent)) n++;
  return n;
}
int drw_group_members(Sheet *s, u32 gid, int *out, int max)
{
  int i, n = 0;
  for (i = 0; i < drw_count(s) && n < max; i++) if (drw_in_group(s, i, gid)) out[n++] = i;
  return n;
}
int drw_unit(Sheet *s, int i, int *out, int max)
{
  u32 g = drw_top_group(s, i);
  if (!drw_obj(s, i) || max < 1) return 0;
  if (!g) { out[0] = i; return 1; }
  return drw_group_members(s, g, out, max);
}
static u32 new_id(Drawing *d)
{
  int i;
  for (i = 0; i < d->n; i++) if (d->o[i].id >= d->nextid) d->nextid = d->o[i].id + 1;
  for (i = 0; i < d->ngrp; i++) if (d->grp[i].id >= d->nextid) d->nextid = d->grp[i].id + 1;
  return d->nextid++;
}
/* a new group (parent 0) named "Group N"; NULL out of memory */
static DGroup *group_new(Drawing *d)
{
  DGroup *n = (DGroup *)realloc(d->grp, (size_t)(d->ngrp + 1) * sizeof *n), *g;
  int i, k = 0;
  if (!n) return NULL;
  d->grp = n;
  for (i = 0; i < d->ngrp; i++) if (!strncmp(d->grp[i].name, "Group ", 6)) { int v = atoi(d->grp[i].name + 6); if (v > k) k = v; }
  g = &d->grp[d->ngrp++];
  memset(g, 0, sizeof *g);
  g->id = new_id(d);
  snprintf(g->name, sizeof g->name, "Group %d", k + 1);
  return g;
}
DGroup *drw_group_add(Sheet *s, u32 parent)
{
  Drawing *d = drw_of(s);
  DGroup *g = d ? group_new(d) : NULL;
  if (g) g->parent = parent;
  return g;
}
u32 drw_group(Sheet *s, const int *sel, int n)
{
  Drawing *d = s->drw;
  u32 units[256], gid;
  int nu = 0, i, k, top = -1, *mark, cnt, j;
  DObj *tmp;
  DGroup *g;
  if (!d || n < 1) return 0;
  /* the outermost groups (or lone objects, as -1 - index) the selection takes in */
  for (k = 0; k < n; k++) {
    u32 t = drw_top_group(s, sel[k]), key = t ? t : 0x80000000UL | (u32)sel[k];
    if (!drw_obj(s, sel[k])) continue;
    for (i = 0; i < nu && units[i] != key; i++) ;
    if (i == nu && nu < 256) units[nu++] = key;
  }
  if (nu < 2) return 0;
  if (!(mark = (int *)calloc((size_t)d->n, sizeof(int)))) return 0;
  for (i = 0; i < d->n; i++) {
    u32 t = drw_top_group(s, i), key = t ? t : 0x80000000UL | (u32)i;
    for (k = 0; k < nu; k++) if (units[k] == key) { mark[i] = 1; if (i > top) top = i; break; }
  }
  if (!(g = group_new(d))) { free(mark); return 0; }
  gid = g->id;
  /* the units' tops become the new group's children */
  for (k = 0; k < nu; k++) {
    if (units[k] & 0x80000000UL) d->o[units[k] & 0x7FFFFFFFUL].group = gid;
    else { DGroup *u = drw_group_find(s, units[k]); if (u) u->parent = gid; }
  }
  /* the members together, where the topmost was: those below it that aren't members keep their order below */
  if (!(tmp = (DObj *)malloc((size_t)d->n * sizeof(DObj)))) { free(mark); return gid; }
  cnt = 0;
  for (i = 0; i <= top; i++) if (!mark[i]) tmp[cnt++] = d->o[i];
  for (i = 0; i <= top; i++) if (mark[i]) tmp[cnt++] = d->o[i];
  for (j = top + 1; j < d->n; j++) tmp[cnt++] = d->o[j];
  memcpy(d->o, tmp, (size_t)d->n * sizeof(DObj));
  free(tmp);
  free(mark);
  return gid;
}
int drw_ungroup(Sheet *s, u32 gid)
{
  Drawing *d = s->drw;
  DGroup *g = drw_group_find(s, gid);
  u32 parent;
  int i;
  if (!g) return 0;
  parent = g->parent;
  for (i = 0; i < d->n; i++) if (d->o[i].group == gid) d->o[i].group = parent;
  for (i = 0; i < d->ngrp; i++) if (d->grp[i].parent == gid) d->grp[i].parent = parent;
  i = (int)(g - d->grp);
  memmove(&d->grp[i], &d->grp[i + 1], (size_t)(d->ngrp - i - 1) * sizeof(DGroup));
  d->ngrp--;
  return 1;
}
void drw_group_box(Sheet *s, u32 gid, i32 *x0, i32 *y0, i32 *x1, i32 *y1)
{
  int i, any = 0;
  *x0 = *y0 = *x1 = *y1 = 0;
  for (i = 0; i < drw_count(s); i++) {
    i32 a0, b0, a1, b1;
    if (!drw_in_group(s, i, gid)) continue;
    dobj_box(s, drw_obj(s, i), &a0, &b0, &a1, &b1);
    if (!any || a0 < *x0) *x0 = a0;
    if (!any || b0 < *y0) *y0 = b0;
    if (!any || a1 > *x1) *x1 = a1;
    if (!any || b1 > *y1) *y1 = b1;
    any = 1;
  }
}
/* v from the span a0..a1 to b0..b1 */
static i32 remap(i32 v, i32 a0, i32 a1, i32 b0, i32 b1)
{
  if (a1 <= a0) return b0 + (v - a0);
  return b0 + (i32)(((double)(v - a0) * (b1 - b0)) / (a1 - a0) + (v >= a0 ? 0.5 : -0.5));
}
void drw_group_place(Sheet *s, u32 gid, i32 x0, i32 y0, i32 x1, i32 y1)
{
  i32 g0, h0, g1, h1;
  int i;
  drw_group_box(s, gid, &g0, &h0, &g1, &h1);
  if (x0 < 0) { x1 -= x0; x0 = 0; }
  if (y0 < 0) { y1 -= y0; y0 = 0; }
  for (i = 0; i < drw_count(s); i++) {
    DObj *o = drw_obj(s, i);
    i32 a0, b0, a1, b1, na0, nb0, na1, nb1;
    if (!drw_in_group(s, i, gid)) continue;
    dobj_box(s, o, &a0, &b0, &a1, &b1);
    na0 = remap(a0, g0, g1, x0, x1); na1 = remap(a1, g0, g1, x0, x1);
    nb0 = remap(b0, h0, h1, y0, y1); nb1 = remap(b1, h0, h1, y0, y1);
    if (o->kind == OB_SHAPE && o->shape == SK_INK && a1 > a0 && b1 > b0) {
      /* a stroke: its points stretched too */
      Buf nb;
      const char *p = o->text ? o->text : "";
      buf_init(&nb);
      while (*p) {
        long a = strtol(p, (char **)&p, 10), b;
        if (*p == ',') p++;
        b = strtol(p, (char **)&p, 10);
        buf_printf(&nb, "%s%ld,%ld", nb.len ? ";" : "", a * (long)(na1 - na0) / (long)(a1 - a0), b * (long)(nb1 - nb0) / (long)(b1 - b0));
        while (*p == ';') p++;
      }
      if (nb.data) { free(o->text); o->text = buf_take(&nb); }
      buf_free(&nb);
    }
    dobj_place(s, o, na0, nb0, na1, nb1);
  }
}
void drw_group_prune(Sheet *s)
{
  Drawing *d = s->drw;
  int again = 1;
  if (!d) return;
  while (again) {
    int k;
    again = 0;
    for (k = 0; k < d->ngrp; k++) {
      u32 id = d->grp[k].id;
      int i, used = 0;
      for (i = 0; i < d->n && !used; i++) if (d->o[i].group == id) used = 1;
      for (i = 0; i < d->ngrp && !used; i++) if (d->grp[i].parent == id) used = 1;
      if (!used) { memmove(&d->grp[k], &d->grp[k + 1], (size_t)(d->ngrp - k - 1) * sizeof(DGroup)); d->ngrp--; again = 1; break; }
    }
  }
}
int drw_groups_copy(Drawing *dst, const Drawing *src)
{
  dst->grp = NULL; dst->ngrp = 0;
  if (!src->ngrp) return 1;
  if (!(dst->grp = (DGroup *)malloc((size_t)src->ngrp * sizeof(DGroup)))) return 0;
  memcpy(dst->grp, src->grp, (size_t)src->ngrp * sizeof(DGroup));
  dst->ngrp = src->ngrp;
  return 1;
}
