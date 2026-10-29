/*
 * cellctl.c - cell controls and protection's finer points (see analysis.h): checkbox cells
 * (Insert > Checkbox), Lock Cell (Home > Format) and Allow Edit Ranges (Review > Protect).
 *
 * A checkbox is a cell format, as GridCraft keeps it (the "checkbox" number format; its
 * crates/engine/src/cmd/extra/cells.rs and crates/xlsx/src/feature_bags.rs, MIT licensed): the
 * cell holds TRUE or FALSE and shows a box. Excel files mark such a format with a feature
 * property bag chain ([MS-XLSX] 2.1.22): the xf's extLst names an XFComplement whose XFControls
 * bag names a Checkbox cell control. Allow Edit Ranges are <protectedRange>s: cells that stay
 * editable on a protected sheet, with or without a password.
 */
#include "analysis.h"
#include "data.h"
#include "calc.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* ------------------------------------------------------------------ checkboxes */
int cell_is_checkbox(Book *b, int sheet, i32 r, i32 c)
{
  int st;
  if (sheet < 0 || sheet >= b->nsh) return 0;
  st = sheet_cell_style(b->sh[sheet], r, c);
  return b->styles[st].numfmt >= 164 && !strcmp(book_fmt_code(b, b->styles[st].numfmt), CHECKBOX_FMT);
}
static void checkbox_edit(Style *st, Font *f, const void *arg) { (void)f; st->numfmt = (u16)*(const int *)arg; st->halign = HA_CENTER; }
int book_checkbox(Book *b, int sheet, Rect a)
{
  int fmt = book_fmt(b, CHECKBOX_FMT);
  i32 r, c;
  Value no = val_bool(0);
  if (sheet < 0 || sheet >= b->nsh) return 0;
  if ((long)(a.r2 - a.r1 + 1) * (a.c2 - a.c1 + 1) > 100000L) return 0;
  book_begin(b);
  book_restyle(b, sheet, a, checkbox_edit, &fmt);
  /* values and formulas already there stay (only TRUE and FALSE draw as a box); empty cells start unticked */
  for (r = a.r1; r <= a.r2; r++)
    for (c = a.c1; c <= a.c2; c++) {
      Cell *cell = sheet_cell(b->sh[sheet], r, c);
      if (!cell || (!cell->f && cell->v.t == V_EMPTY)) book_set_value(b, sheet, r, c, &no);
    }
  book_end(b);
  b->dirty = 1;
  return 1;
}
int book_checkbox_toggle(Book *b, int sheet, i32 r, i32 c)
{
  Cell *cell;
  Value v;
  if (!cell_is_checkbox(b, sheet, r, c)) return 0;
  cell = sheet_cell(b->sh[sheet], r, c);
  if (cell && cell->f) return 0;
  if (cell && cell->v.t != V_BOOL && cell->v.t != V_EMPTY) return 0;
  v = val_bool(!(cell && cell->v.t == V_BOOL && cell->v.e));
  return book_set_value(b, sheet, r, c, &v);
}

/* ------------------------------------------------------------------ Lock Cell */
static void lock_edit(Style *st, Font *f, const void *arg) { (void)f; st->locked = (u8)*(const int *)arg; }
int book_lock_cells(Book *b, int sheet, Rect a, int on)
{
  if (sheet < 0 || sheet >= b->nsh) return 0;
  return book_restyle(b, sheet, a, lock_edit, &on);
}
int cell_locked(Book *b, int sheet, i32 r, i32 c)
{
  if (sheet < 0 || sheet >= b->nsh) return 1;
  return b->styles[sheet_cell_style(b->sh[sheet], r, c)].locked != 0;
}

/* ------------------------------------------------------------------ Allow Edit Ranges */
static void er_clear(EditRange *e) { free(e->areas); e->areas = NULL; e->nareas = 0; }
int edit_range_has_pw(const EditRange *e) { return e->pwhash || e->key[0]; }
int edit_range_unlock(EditRange *e, const char *pw)
{
  if (!edit_range_has_pw(e)) return 1;
  if (!prot_password_ok(e->pwhash, e->key, pw)) return 0;
  e->unlocked = 1;
  return 1;
}
int book_edit_range_set(Book *b, int sheet, int i, const char *title, const Rect *areas, int n, const char *pw, int keep_pw)
{
  Sheet *s;
  EditRange e, *ne;
  if (sheet < 0 || sheet >= b->nsh || n <= 0 || n > 64) return 0;
  s = b->sh[sheet];
  if (i >= s->neranges) return 0;
  memset(&e, 0, sizeof e);
  snprintf(e.title, sizeof e.title, "%s", title && *title ? title : "Range1");
  if (!(e.areas = (Rect *)malloc((size_t)n * sizeof(Rect)))) return 0;
  memcpy(e.areas, areas, (size_t)n * sizeof(Rect));
  e.nareas = n;
  if (keep_pw && i >= 0) { e.pwhash = s->eranges[i].pwhash; strcpy(e.key, s->eranges[i].key); e.unlocked = s->eranges[i].unlocked; }
  else e.pwhash = prot_hash(pw);
  book_begin(b);
  book_note_sheet(b, sheet);
  if (i >= 0) { er_clear(&s->eranges[i]); s->eranges[i] = e; }
  else {
    if (!(ne = (EditRange *)realloc(s->eranges, (size_t)(s->neranges + 1) * sizeof(EditRange)))) { book_end(b); free(e.areas); return 0; }
    s->eranges = ne;
    s->eranges[s->neranges++] = e;
  }
  book_end(b);
  b->dirty = 1; b->changes++;
  return 1;
}
int book_edit_range_delete(Book *b, int sheet, int i)
{
  Sheet *s;
  if (sheet < 0 || sheet >= b->nsh || i < 0 || i >= (s = b->sh[sheet])->neranges) return 0;
  book_begin(b);
  book_note_sheet(b, sheet);
  er_clear(&s->eranges[i]);
  memmove(&s->eranges[i], &s->eranges[i + 1], (size_t)(s->neranges - i - 1) * sizeof(EditRange));
  s->neranges--;
  book_end(b);
  b->dirty = 1; b->changes++;
  return 1;
}
static int in_rect(const Rect *a, i32 r, i32 c) { return r >= a->r1 && r <= a->r2 && c >= a->c1 && c <= a->c2; }
EditRange *edit_range_at(Sheet *s, i32 r, i32 c)
{
  int i, k;
  for (i = 0; i < s->neranges; i++) for (k = 0; k < s->eranges[i].nareas; k++) if (in_rect(&s->eranges[i].areas[k], r, c)) return &s->eranges[i];
  return NULL;
}
/* whether cell r, c is in a range that is open (no password, or given) */
static int open_at(Sheet *s, i32 r, i32 c)
{
  int i, k;
  for (i = 0; i < s->neranges; i++) {
    EditRange *e = &s->eranges[i];
    if (edit_range_has_pw(e) && !e->unlocked) continue;
    for (k = 0; k < e->nareas; k++) if (in_rect(&e->areas[k], r, c)) return 1;
  }
  return 0;
}
int edit_ranges_open(Sheet *s, Rect a)
{
  int i, k;
  i32 r, c;
  if (!s->neranges) return 0;
  /* inside one open area: the quick answer */
  for (i = 0; i < s->neranges; i++) {
    EditRange *e = &s->eranges[i];
    if (edit_range_has_pw(e) && !e->unlocked) continue;
    for (k = 0; k < e->nareas; k++) {
      const Rect *x = &e->areas[k];
      if (a.r1 >= x->r1 && a.r2 <= x->r2 && a.c1 >= x->c1 && a.c2 <= x->c2) return 1;
    }
  }
  if ((long)(a.r2 - a.r1 + 1) * (a.c2 - a.c1 + 1) > 20000L) return 0;
  for (r = a.r1; r <= a.r2; r++) for (c = a.c1; c <= a.c2; c++) if (!open_at(s, r, c)) return 0;
  return 1;
}
EditRange *edit_range_shut(Sheet *s, Rect a)
{
  int i, k;
  for (i = 0; i < s->neranges; i++) {
    EditRange *e = &s->eranges[i];
    if (!edit_range_has_pw(e) || e->unlocked) continue;
    for (k = 0; k < e->nareas; k++) {
      const Rect *x = &e->areas[k];
      if (a.r1 <= x->r2 && a.r2 >= x->r1 && a.c1 <= x->c2 && a.c2 >= x->c1) return e;
    }
  }
  return NULL;
}
void edit_ranges_free(Sheet *s)
{
  int i;
  for (i = 0; i < s->neranges; i++) er_clear(&s->eranges[i]);
  free(s->eranges);
  s->eranges = NULL; s->neranges = 0;
}
void edit_ranges_clone(Sheet *dst, const Sheet *src)
{
  int i;
  dst->eranges = NULL; dst->neranges = 0;
  if (!src->neranges || !(dst->eranges = (EditRange *)calloc((size_t)src->neranges, sizeof(EditRange)))) return;
  for (i = 0; i < src->neranges; i++) {
    EditRange *d = &dst->eranges[dst->neranges];
    *d = src->eranges[i];
    if (!(d->areas = (Rect *)malloc((size_t)(d->nareas + 1) * sizeof(Rect)))) continue;
    memcpy(d->areas, src->eranges[i].areas, (size_t)d->nareas * sizeof(Rect));
    dst->neranges++;
  }
}
void edit_ranges_shift(Sheet *s, int rows, i32 at, i32 n)
{
  int i, j, k;
  i32 lim = rows ? MAXROW : MAXCOL;
  for (i = 0; i < s->neranges; i++) {
    EditRange *e = &s->eranges[i];
    for (j = k = 0; j < e->nareas; j++) {
      Rect a = e->areas[j];
      i32 *a1 = rows ? &a.r1 : &a.c1, *a2 = rows ? &a.r2 : &a.c2;
      if (n > 0) {
        if (*a1 >= at) *a1 += n;
        if (*a2 >= at && *a2 < lim - 1) *a2 += n;
        if (*a1 >= lim) continue;
        if (*a2 >= lim) *a2 = lim - 1;
      } else {
        i32 d = -n;
        if (*a1 >= at && *a2 < at + d) continue;
        if (*a1 >= at + d) *a1 -= d; else if (*a1 >= at) *a1 = at;
        if (*a2 >= lim - 1) {} else if (*a2 >= at + d) *a2 -= d; else if (*a2 >= at) *a2 = at - 1;
      }
      e->areas[k++] = a;
    }
    e->nareas = k;
  }
  for (i = k = 0; i < s->neranges; i++) { if (s->eranges[i].nareas) s->eranges[k++] = s->eranges[i]; else er_clear(&s->eranges[i]); }
  s->neranges = k;
}
static void xesc(Buf *o, const char *s) { xml_escape(o, s, strlen(s)); }
void edit_ranges_xlsx_write(const Sheet *s, Buf *o)
{
  int i, k;
  char ref[40];
  if (!s->neranges) return;
  buf_adds(o, "<protectedRanges>");
  for (i = 0; i < s->neranges; i++) {
    const EditRange *e = &s->eranges[i];
    buf_adds(o, "<protectedRange");
    if (e->pwhash) buf_printf(o, " password=\"%04X\"", e->pwhash);
    else if (e->key[0]) {
      char algo[32], spin[16], salt[100], hash[120];
      if (sscanf(e->key, "%31[^:]:%15[^:]:%99[^:]:%119s", algo, spin, salt, hash) == 4)
        buf_printf(o, " algorithmName=\"%s\" hashValue=\"%s\" saltValue=\"%s\" spinCount=\"%s\"", algo, hash, salt, spin);
    }
    buf_adds(o, " sqref=\"");
    for (k = 0; k < e->nareas; k++) { rect_text(e->areas[k], ref); buf_printf(o, "%s%s", k ? " " : "", ref); }
    buf_adds(o, "\" name=\""); xesc(o, e->title); buf_adds(o, "\"/>");
  }
  buf_adds(o, "</protectedRanges>");
}
void edit_ranges_xlsx_read(Sheet *s, Xml *x)
{
  const char *sq = xml_attr(x, "sqref"), *nm = xml_attr(x, "name"), *pw = xml_attr(x, "password"), *a, *h, *sa, *sp;
  EditRange e, *ne;
  char buf[400], *tok;
  if (!sq || s->neranges >= 1000) return;
  memset(&e, 0, sizeof e);
  snprintf(e.title, sizeof e.title, "%s", nm ? nm : "Range1");
  snprintf(buf, sizeof buf, "%s", sq);
  if (!(e.areas = (Rect *)malloc(64 * sizeof(Rect)))) return;
  for (tok = strtok(buf, " "); tok && e.nareas < 64; tok = strtok(NULL, " ")) {
    Rect r; u16 sh; u8 fl;
    int used = parse_ref(NULL, tok, &sh, &r, &fl);
    if (used && !tok[used]) e.areas[e.nareas++] = r;
  }
  if (!e.nareas) { free(e.areas); return; }
  if (pw) e.pwhash = (u16)strtoul(pw, NULL, 16);
  else if ((a = xml_attr(x, "algorithmName")) && (h = xml_attr(x, "hashValue")) && (sa = xml_attr(x, "saltValue"))) {
    sp = xml_attr(x, "spinCount");
    /* a key Ledger can't check (or a damaged one) never matches, so the range stays shut */
    if (strlen(a) + strlen(h) + strlen(sa) + 20 < sizeof e.key) snprintf(e.key, sizeof e.key, "%s:%s:%s:%s", a, sp ? sp : "0", sa, h);
    else strcpy(e.key, "?");
  }
  if (!(ne = (EditRange *)realloc(s->eranges, (size_t)(s->neranges + 1) * sizeof(EditRange)))) { free(e.areas); return; }
  s->eranges = ne;
  s->eranges[s->neranges++] = e;
}

/* ------------------------------------------------------------------ checkboxes in .xlsx */
#define FPB_NS "http://schemas.microsoft.com/office/spreadsheetml/2022/featurepropertybag"
int checkbox_used(Book *b)
{
  int i;
  for (i = 0; i < b->nstyles; i++) if (b->styles[i].numfmt >= 164 && !strcmp(book_fmt_code(b, b->styles[i].numfmt), CHECKBOX_FMT)) return 1;
  return 0;
}
/* Checkbox (0), XFControls naming it (1), XFComplement naming that (2), XFComplements mapping
   index 0 to it (3): every reference to an earlier bag */
void checkbox_part(Buf *o)
{
  buf_adds(o, "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n<FeaturePropertyBags xmlns=\"" FPB_NS "\" count=\"4\">"
              "<bag type=\"Checkbox\"/><bag type=\"XFControls\"><bagId k=\"CellControl\">0</bagId></bag>"
              "<bag type=\"XFComplement\"><bagId k=\"XFControls\">1</bagId></bag>"
              "<bag type=\"XFComplements\"><a k=\"MappedFeaturePropertyBags\"><bagId>2</bagId></a></bag></FeaturePropertyBags>");
}
void checkbox_xf_ext(Buf *o)
{
  buf_adds(o, "<extLst><ext uri=\"{C7286773-470A-42A8-94C5-96B5CB345126}\" xmlns:xfpb=\"" FPB_NS "\"><xfpb:xfComplement i=\"0\"/></ext></extLst>");
}
/* which xfComplement indexes resolve to a Checkbox: the MappedFeaturePropertyBags entry -> an
   XFComplement bag -> its XFControls bag -> its CellControl, a Checkbox bag */
int checkbox_complements(const char *xml, size_t len, u8 *flags, int max)
{
  Xml x;
  enum { NB = 4096 };
  char *type = (char *)calloc(NB, 1);
  int *ctl = (int *)malloc(NB * sizeof(int)), *xfc = (int *)malloc(NB * sizeof(int)), *map = (int *)malloc(NB * sizeof(int));
  int nb = 0, nmap = 0, inmap = 0, i, cur = -1;
  char key[32];
  if (!type || !ctl || !xfc || !map) { free(type); free(ctl); free(xfc); free(map); return 0; }
  key[0] = 0;
  xml_init(&x, xml, len);
  while (xml_next(&x) != XT_EOF) {
    if (x.type == XT_START && !strcmp(x.name, "bag") && nb < NB) {
      const char *t = xml_attr(&x, "type");
      cur = nb++;
      type[cur] = !t ? 0 : !strcmp(t, "Checkbox") ? 1 : !strcmp(t, "XFControls") ? 2 : !strcmp(t, "XFComplement") ? 3 : !strcmp(t, "XFComplements") ? 4 : 0;
      ctl[cur] = xfc[cur] = -1;
      if (x.pending_end) cur = -1;
    } else if (x.type == XT_END && !strcmp(x.name, "bag")) cur = -1;
    else if (x.type == XT_START && !strcmp(x.name, "a") && cur >= 0) { const char *k = xml_attr(&x, "k"); inmap = k && !strcmp(k, "MappedFeaturePropertyBags") && type[cur] == 4; }
    else if (x.type == XT_END && !strcmp(x.name, "a")) inmap = 0;
    else if (x.type == XT_START && !strcmp(x.name, "bagId") && cur >= 0) {
      const char *k = xml_attr(&x, "k");
      snprintf(key, sizeof key, "%s", k ? k : "");
      if (x.pending_end) key[0] = 0;
    } else if (x.type == XT_TEXT && cur >= 0) {
      int v = atoi(x.text.data ? x.text.data : "-1");
      if (inmap && !key[0] && nmap < NB) map[nmap++] = v;
      else if (!strcmp(key, "CellControl")) ctl[cur] = v;
      else if (!strcmp(key, "XFControls")) xfc[cur] = v;
    } else if (x.type == XT_END && !strcmp(x.name, "bagId")) key[0] = 0;
  }
  xml_free(&x);
  for (i = 0; i < nmap && i < max; i++) {
    int c = map[i], d = c >= 0 && c < nb && type[c] == 3 ? xfc[c] : -1, e = d >= 0 && d < nb && type[d] == 2 ? ctl[d] : -1;
    flags[i] = (u8)(e >= 0 && e < nb && type[e] == 1);
  }
  free(type); free(ctl); free(xfc); free(map);
  return nmap < max ? nmap : max;
}
