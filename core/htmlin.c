/*
 * htmlin.c - a web page's table opened as a sheet (GridCraft #3166), from a file on disk: Ledger has
 * no network. Web applications' "Export to Excel" often writes an HTML table and names it .xls; Excel
 * opens the table, and so does Ledger.
 *
 * The first <table> of the page becomes the sheet: each <tr> a row, each <td> or <th> a cell (read as
 * if typed, as CSV fields are; text that starts with = stays text), colspan and rowspan as merged
 * cells, <th> cells bold and centred, <br> a line break, entities and character references decoded,
 * non-breaking spaces kept. The sheet takes the page's <x:Name> (Excel's own HTML), else the caller
 * names it. The page may be UTF-8 or, failing that, Latin-1. A tolerant scanner, not an XML parser:
 * web pages are seldom well formed. Portable C99.
 */
#include "book.h"
#include "xlsx.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>

static int ci_starts(const char *s, const char *e, const char *w)
{
  size_t n = strlen(w);
  return (size_t)(e - s) >= n && !str_ncasecmp(s, w, n);
}
static const char *ci_find(const char *s, const char *e, const char *w)
{
  size_t n = strlen(w);
  for (; s + n <= e; s++) if (!str_ncasecmp(s, w, n)) return s;
  return NULL;
}
int html_is(const char *data, size_t len)
{
  const char *s = data, *e = data + (len > 65536 ? 65536 : len);
  if (len >= 3 && (u8)s[0] == 0xEF && (u8)s[1] == 0xBB && (u8)s[2] == 0xBF) s += 3;
  /* the page starts with markup (after spaces, comments, a doctype) and has a table */
  while (s < e) {
    while (s < e && isspace((u8)*s)) s++;
    if (ci_starts(s, e, "<!--")) { const char *c = ci_find(s + 4, e, "-->"); if (!c) return 0; s = c + 3; continue; }
    break;
  }
  if (s >= e || *s != '<') return 0;
  if (!ci_starts(s, e, "<!doctype html") && !ci_starts(s, e, "<html") && !ci_starts(s, e, "<table") && !ci_starts(s, e, "<head") && !ci_starts(s, e, "<body") && !ci_starts(s, e, "<meta")) return 0;
  return ci_find(data, data + len, "<table") != NULL;
}
static int valid_utf8(const char *s, size_t n)
{
  size_t i = 0;
  while (i < n) {
    u8 c = (u8)s[i];
    int k = c < 0x80 ? 0 : (c & 0xE0) == 0xC0 ? 1 : (c & 0xF0) == 0xE0 ? 2 : (c & 0xF8) == 0xF0 ? 3 : -1;
    if (k < 0 || (k == 1 && c < 0xC2)) return 0;
    for (i++; k > 0; k--, i++) if (i >= n || ((u8)s[i] & 0xC0) != 0x80) return 0;
  }
  return 1;
}
/* an attribute's value in a tag's text (s..e, after the name), "" none */
static void tag_attr(const char *s, const char *e, const char *name, char *out, size_t cap)
{
  size_t n = strlen(name), o = 0;
  out[0] = 0;
  for (; s + n < e; s++) {
    if ((s[-1] == ' ' || s[-1] == '\t' || s[-1] == '\n' || s[-1] == '\r') && !str_ncasecmp(s, name, n)) {
      const char *p = s + n;
      char q = 0;
      while (p < e && *p == ' ') p++;
      if (p >= e || *p != '=') continue;
      p++;
      while (p < e && *p == ' ') p++;
      if (p < e && (*p == '"' || *p == '\'')) q = *p++;
      while (p < e && o + 1 < cap && (q ? *p != q : !isspace((u8)*p) && *p != '>')) out[o++] = *p++;
      out[o] = 0;
      return;
    }
  }
}
static const struct { const char *n; u32 cp; } ents[] = {
  { "amp", '&' }, { "lt", '<' }, { "gt", '>' }, { "quot", '"' }, { "apos", '\'' }, { "nbsp", 0xA0 }, { "copy", 0xA9 }, { "reg", 0xAE },
  { "euro", 0x20AC }, { "pound", 0xA3 }, { "yen", 0xA5 }, { "cent", 0xA2 }, { "deg", 0xB0 }, { "plusmn", 0xB1 }, { "times", 0xD7 },
  { "divide", 0xF7 }, { "ndash", 0x2013 }, { "mdash", 0x2014 }, { "hellip", 0x2026 }, { "laquo", 0xAB }, { "raquo", 0xBB },
  { "auml", 0xE4 }, { "ouml", 0xF6 }, { "uuml", 0xFC }, { "Auml", 0xC4 }, { "Ouml", 0xD6 }, { "Uuml", 0xDC }, { "szlig", 0xDF },
  { "eacute", 0xE9 }, { "egrave", 0xE8 }, { "agrave", 0xE0 }, { "ccedil", 0xE7 }, { "ntilde", 0xF1 }, { "aacute", 0xE1 },
  { "iacute", 0xED }, { "oacute", 0xF3 }, { "uacute", 0xFA }, { "lsquo", 0x2018 }, { "rsquo", 0x2019 }, { "ldquo", 0x201C }, { "rdquo", 0x201D },
  { NULL, 0 } };
/* text between tags into the cell's text: entities decoded, runs of white space one space */
static void add_text(Buf *t, const char *s, const char *e, int *space)
{
  while (s < e) {
    if (isspace((u8)*s)) { *space = 1; s++; continue; }
    if (*space && t->len && t->data[t->len - 1] != '\n') buf_addc(t, ' ');
    *space = 0;
    if (*s == '&') {
      const char *q = s + 1;
      u32 cp = 0;
      int k, ok = 0;
      char u[8];
      if (q < e && *q == '#') {
        q++;
        if (q < e && (*q == 'x' || *q == 'X')) { q++; while (q < e && isxdigit((u8)*q) && cp < 0x110000) { cp = cp * 16 + (u32)(isdigit((u8)*q) ? *q - '0' : (tolower((u8)*q) - 'a' + 10)); q++; } }
        else while (q < e && isdigit((u8)*q) && cp < 0x110000) cp = cp * 10 + (u32)(*q++ - '0');
        ok = cp > 0 && cp < 0x110000 && q < e && *q == ';';
      } else {
        for (k = 0; ents[k].n; k++) { size_t n = strlen(ents[k].n); if ((size_t)(e - q) > n && !strncmp(q, ents[k].n, n) && q[n] == ';') { cp = ents[k].cp; q += n; ok = 1; break; } }
      }
      if (ok) { buf_add(t, u, (size_t)utf8_put(u, cp)); s = q + 1; continue; }
    }
    buf_addc(t, *s++);
  }
}
typedef struct { i32 r, c; } Pos;
Book *html_load(const char *data, size_t len, char *sheetname, size_t cap)
{
  Book *b;
  char *conv = NULL;
  const char *s, *e, *t0, *t1;
  i32 r = -1, c = 0;
  Buf text;
  int in_cell = 0, space = 0, header = 0, depth = 0, saved, cs = 1, rs = 1, bold_style = -1;
  u8 *taken = NULL;                 /* cells covered by a rowspan from above: 64 rows x 256 columns, rolling */
  if (sheetname && cap) sheetname[0] = 0;
  if (!html_is(data, len)) return NULL;
  if (!valid_utf8(data, len)) {
    Buf u;
    size_t i;
    buf_init(&u);
    for (i = 0; i < len; i++) { char x[4]; buf_add(&u, x, (size_t)utf8_put(x, (u8)data[i])); }
    if (u.oom) { buf_free(&u); return NULL; }
    len = u.len; conv = buf_take(&u);
    data = conv;
  }
  e = data + len;
  /* Excel's own HTML names its sheet: <x:Name>Sheet1</x:Name> */
  if (sheetname && (t0 = ci_find(data, e, "<x:Name>")) && (t1 = ci_find(t0, e, "</x:Name>")) && t1 - t0 - 8 > 0 && (size_t)(t1 - t0 - 8) < cap && t1 - t0 - 8 < 32) {
    memcpy(sheetname, t0 + 8, (size_t)(t1 - t0 - 8)); sheetname[t1 - t0 - 8] = 0;
  }
  if (!(t0 = ci_find(data, e, "<table")) || !(b = book_new())) { free(conv); return NULL; }
  saved = b->manualcalc; b->manualcalc = 1;
  taken = (u8 *)calloc(64 * 256, 1);
  buf_init(&text);
  for (s = t0; s < e; ) {
    const char *gt, *nm;
    int close, k;
    if (*s != '<') {
      const char *lt = memchr(s, '<', (size_t)(e - s));
      if (!lt) lt = e;
      if (in_cell) add_text(&text, s, lt, &space);
      s = lt;
      continue;
    }
    if (ci_starts(s, e, "<!--")) { const char *q = ci_find(s + 4, e, "-->"); s = q ? q + 3 : e; continue; }
    if (!(gt = memchr(s, '>', (size_t)(e - s)))) break;
    close = s[1] == '/';
    nm = s + 1 + close;
    for (k = 0; nm + k < gt && isalnum((u8)nm[k]); k++) ;
    if ((k == 6 && !str_ncasecmp(nm, "script", 6)) || (k == 5 && !str_ncasecmp(nm, "style", 5))) {
      if (!close) { const char *q = ci_find(gt, e, k == 6 ? "</script" : "</style"); s = q ? q : e; continue; }
    }
    /* a cell ends where the next begins, its row ends, or the table */
    if (in_cell && ((depth == 1 && k == 2 && (!str_ncasecmp(nm, "td", 2) || !str_ncasecmp(nm, "th", 2) || !str_ncasecmp(nm, "tr", 2))) || (k == 5 && close && !str_ncasecmp(nm, "table", 5) && depth == 1))) {
      while (text.len && (text.data[text.len - 1] == ' ' || text.data[text.len - 1] == '\n')) text.len--;
      buf_addc(&text, 0); text.len--;
      if (r >= 0 && r < MAXROW && c < MAXCOL && text.len) {
        Value v;
        if (text.data[0] == '=' || text.data[0] == '\'') { v = val_str(text.data); book_set_value(b, 0, r, c, &v); val_free(&v); }
        else book_input(b, 0, r, c, text.data, NULL);
      }
      if (header && r >= 0 && r < MAXROW && c < MAXCOL) {
        if (bold_style < 0) {
          Style st = b->styles[0];
          Font f = b->fonts[st.font];
          f.bold = 1; st.font = (u16)book_font(b, &f); st.halign = HA_CENTER;
          bold_style = book_style(b, &st);
        }
        if (bold_style >= 0) { i32 cc; for (cc = c; cc < c + cs && cc < MAXCOL; cc++) book_set_style(b, 0, r, cc, bold_style); }
      }
      if (r >= 0 && (cs > 1 || rs > 1)) {
        Rect m;
        i32 rr, cc;
        m.r1 = r; m.c1 = c; m.r2 = r + rs - 1; m.c2 = c + cs - 1;
        if (m.r2 >= MAXROW) m.r2 = MAXROW - 1;
        if (m.c2 >= MAXCOL) m.c2 = MAXCOL - 1;
        book_merge(b, 0, m, 1);
        for (rr = r + 1; rr <= m.r2 && rr < r + 64; rr++) for (cc = c; cc <= m.c2 && cc < 256; cc++) if (taken) taken[(rr % 64) * 256 + cc] = 1;
      }
      c += cs;
      in_cell = 0;
    }
    if (k == 5 && !str_ncasecmp(nm, "table", 5)) {
      depth += close ? -1 : 1;
      if (depth <= 0) break;
    } else if (depth == 1 && k == 2 && !close && !str_ncasecmp(nm, "tr", 2)) {
      if (taken && r >= 0) memset(taken + (r % 64) * 256, 0, 256);
      r++; c = 0;
      if (r >= MAXROW) break;
    } else if (depth == 1 && k == 2 && !close && (!str_ncasecmp(nm, "td", 2) || !str_ncasecmp(nm, "th", 2))) {
      char a[16];
      if (r < 0) r = 0;
      while (taken && c < 256 && taken[(r % 64) * 256 + c]) c++;
      header = tolower((u8)nm[1]) == 'h';
      tag_attr(nm + 2, gt, "colspan", a, sizeof a); cs = atoi(a); if (cs < 1 || cs > 1000) cs = 1;
      tag_attr(nm + 2, gt, "rowspan", a, sizeof a); rs = atoi(a); if (rs < 1 || rs > 1000) rs = 1;
      text.len = 0; space = 0;
      in_cell = 1;
    } else if (in_cell && k == 2 && !str_ncasecmp(nm, "br", 2)) {
      buf_addc(&text, '\n'); space = 0;
    } else if (in_cell && k == 1 && close && (tolower((u8)nm[0]) == 'p')) {
      buf_addc(&text, '\n'); space = 0;
    }
    s = gt + 1;
  }
  buf_free(&text);
  free(taken);
  free(conv);
  b->manualcalc = saved;
  book_forget_undo(b);
  book_recalc(b);
  b->dirty = 0;
  return b;
}
