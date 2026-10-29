/*
 * ods.c - OpenDocument spreadsheets (.ods) read (see xls.h): content.xml's tables, their cells
 * (numbers, percentages, currency, dates, times, booleans, text), formulas turned from OpenFormula
 * ("of:=SUM([.A1:.B2];[$Sheet2.C3])") into Excel's syntax, the automatic styles (fonts, fills,
 * borders, alignment and number formats), column widths and row heights, merged cells, hidden rows
 * and columns, and row and column groups as the outline.
 */
#include "xls.h"
#include "data.h"
#include "zip.h"
#include "xml.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>
#include <ctype.h>

int ods_is(const u8 *data, size_t len)
{
  /* the mimetype part comes first, stored, at a known place */
  static const char mt[] = "application/vnd.oasis.opendocument.spreadsheet";
  return len > 38 + sizeof mt && data[0] == 'P' && data[1] == 'K' && !memcmp(data + 30, "mimetype", 8) && !memcmp(data + 38, mt, sizeof mt - 1);
}

/* ------------------------------------------------------------------ styles */
typedef struct { char name[48]; char data[48]; Style st; Font f; u8 hasfont, hasfill; } CStyle;   /* a cell style */
typedef struct { char name[48]; char *code; } DStyle;                                         /* a number format */
typedef struct { char name[48]; u16 px; u8 optimal; u8 hidden; } LStyle;                      /* a column, row or table style */
typedef struct {
  Book *b;
  CStyle *cs; int ncs;
  DStyle *ds; int nds;
  LStyle *ls; int nls;
} Od;
/* a length ("2.2in", "1.5cm", "12pt", "25mm", "3pc") in pixels at 96 per inch */
static double px_of(const char *v)
{
  char *e;
  double d;
  if (!v) return 0;
  d = strtod(v, &e);
  if (!strncmp(e, "in", 2)) return d * 96;
  if (!strncmp(e, "cm", 2)) return d * 96 / 2.54;
  if (!strncmp(e, "mm", 2)) return d * 96 / 25.4;
  if (!strncmp(e, "pt", 2)) return d * 96 / 72;
  if (!strncmp(e, "pc", 2)) return d * 16;
  return d;
}
static u32 hexcolor(const char *v, u32 dflt)
{
  if (!v || v[0] != '#' || strlen(v) < 7) return dflt;
  return (u32)strtoul(v + 1, NULL, 16) & 0xFFFFFF;
}
/* "0.06pt solid #000000" as a border style */
static void border_of(const char *v, u8 *style, u32 *color)
{
  double w;
  if (!v || !strcmp(v, "none")) return;
  w = px_of(v);
  *style = strstr(v, "double") ? BS_DOUBLE : strstr(v, "dotted") ? BS_DOTTED : strstr(v, "dash") ? BS_DASHED : w > 2.5 ? BS_THICK : w > 1.2 ? BS_MEDIUM : BS_THIN;
  { const char *h = strchr(v, '#'); *color = h ? hexcolor(h, COLOR_AUTO) : COLOR_AUTO; }
}
static CStyle *cstyle(Od *o, const char *name)
{
  int i;
  if (!name) return NULL;
  for (i = 0; i < o->ncs; i++) if (!strcmp(o->cs[i].name, name)) return &o->cs[i];
  return NULL;
}
static LStyle *lstyle(Od *o, const char *name)
{
  int i;
  if (!name) return NULL;
  for (i = 0; i < o->nls; i++) if (!strcmp(o->ls[i].name, name)) return &o->ls[i];
  return NULL;
}
/* a number style's children into an Excel format code */
static void read_numstyle(Od *o, Xml *x, const char *kind)
{
  const char *nm0 = xml_attr(x, "name");
  char nm[48];
  int depth = x->depth;
  Buf code;
  DStyle *nd;
  int intext = 0;
  if (!nm0 || o->nds >= 2000) { xml_skip(x); return; }
  strncpy(nm, nm0, sizeof nm - 1); nm[sizeof nm - 1] = 0;
  buf_init(&code);
  if (x->pending_end) goto done;
  while (xml_next(x) != XT_EOF) {
    if (x->type == XT_END && x->depth < depth) break;
    if (x->type == XT_END && (!strcmp(x->name, "text") || !strcmp(x->name, "currency-symbol"))) { intext = 0; continue; }
    if (x->type == XT_TEXT && intext) {
      /* literal text: quoted unless it is a plain separator */
      size_t i;
      int plain = 1;
      for (i = 0; i < x->text.len; i++) if (!strchr(" -/:.,%()$", x->text.data[i])) plain = 0;
      if (intext == 2) { buf_addc(&code, '"'); buf_add(&code, x->text.data, x->text.len); buf_addc(&code, '"'); }
      else if (plain) buf_add(&code, x->text.data, x->text.len);
      else { buf_addc(&code, '"'); buf_add(&code, x->text.data, x->text.len); buf_addc(&code, '"'); }
      continue;
    }
    if (x->type != XT_START) continue;
    {
      const char *n = x->name, *st = xml_attr(x, "style");
      int lng = st && !strcmp(st, "long");
      if (!strcmp(n, "text")) intext = 1;
      else if (!strcmp(n, "currency-symbol")) intext = 2;
      else if (!strcmp(n, "number") || !strcmp(n, "scientific-number")) {
        const char *dp = xml_attr(x, "decimal-places"), *mi = xml_attr(x, "min-integer-digits"), *gr = xml_attr(x, "grouping");
        int d = dp ? atoi(dp) : 0, m = mi ? atoi(mi) : 1, k;
        if (gr && !strcmp(gr, "true")) buf_adds(&code, m ? "#,##0" : "#,###");
        else { if (!m) buf_addc(&code, '#'); for (k = 0; k < m && k < 12; k++) buf_addc(&code, '0'); }
        if (d > 0) { buf_addc(&code, '.'); for (k = 0; k < d && k < 15; k++) buf_addc(&code, '0'); }
        if (!strcmp(n, "scientific-number")) buf_adds(&code, "E+00");
      } else if (!strcmp(n, "fraction")) buf_adds(&code, "# ?/?");
      else if (!strcmp(n, "year")) buf_adds(&code, lng ? "yyyy" : "yy");
      else if (!strcmp(n, "month")) { const char *tx = xml_attr(x, "textual"); buf_adds(&code, tx && !strcmp(tx, "true") ? (lng ? "mmmm" : "mmm") : lng ? "mm" : "m"); }
      else if (!strcmp(n, "day")) buf_adds(&code, lng ? "dd" : "d");
      else if (!strcmp(n, "day-of-week")) buf_adds(&code, lng ? "dddd" : "ddd");
      else if (!strcmp(n, "hours")) buf_adds(&code, lng ? "hh" : "h");
      else if (!strcmp(n, "minutes")) buf_adds(&code, lng ? "mm" : "m");
      else if (!strcmp(n, "seconds")) { const char *dp = xml_attr(x, "decimal-places"); buf_adds(&code, lng ? "ss" : "s"); if (dp && atoi(dp) > 0) buf_adds(&code, ".00"); }
      else if (!strcmp(n, "am-pm")) buf_adds(&code, "AM/PM");
      else if (!strcmp(n, "boolean")) buf_adds(&code, "\"TRUE\";\"TRUE\";\"FALSE\"");
      else if (!strcmp(n, "text-content")) buf_addc(&code, '@');
    }
  }
done:
  if (code.data && code.len && (nd = (DStyle *)realloc(o->ds, (size_t)(o->nds + 1) * sizeof(DStyle)))) {
    o->ds = nd;
    strncpy(nd[o->nds].name, nm, sizeof nd[0].name - 1); nd[o->nds].name[sizeof nd[0].name - 1] = 0;
    if (!strcmp(kind, "boolean-style")) { buf_free(&code); buf_init(&code); buf_adds(&code, "General"); }
    nd[o->nds].code = buf_take(&code);
    if (nd[o->nds].code) o->nds++;
  }
  buf_free(&code);
}
/* a style:style of the automatic styles */
static void read_style(Od *o, Xml *x)
{
  const char *nm = xml_attr(x, "name"), *fam = xml_attr(x, "family"), *dsn = xml_attr(x, "data-style-name");
  int depth = x->depth;
  CStyle c;
  LStyle l;
  int cell = fam && !strcmp(fam, "table-cell");
  if (!nm || !fam) { xml_skip(x); return; }
  memset(&c, 0, sizeof c); memset(&l, 0, sizeof l);
  strncpy(c.name, nm, sizeof c.name - 1); strncpy(l.name, nm, sizeof l.name - 1);
  if (dsn) strncpy(c.data, dsn, sizeof c.data - 1);
  c.st.locked = 1;
  c.f = o->b->fonts[0];
  if (!x->pending_end)
    while (xml_next(x) != XT_EOF) {
      const char *v;
      if (x->type == XT_END && x->depth < depth) break;
      if (x->type != XT_START) continue;
      if (!strcmp(x->name, "text-properties")) {
        if ((v = xml_attr(x, "font-name")) || (v = xml_attr(x, "font-family"))) { strncpy(c.f.name, v[0] == '\'' ? v + 1 : v, sizeof c.f.name - 1); if (strchr(c.f.name, '\'')) *strchr(c.f.name, '\'') = 0; c.hasfont = 1; }
        if ((v = xml_attr(x, "font-size")) && strstr(v, "pt")) { c.f.size = (u16)(strtod(v, NULL) * 10 + 0.5); c.hasfont = 1; }
        if ((v = xml_attr(x, "font-weight"))) { c.f.bold = !strcmp(v, "bold") || atoi(v) >= 600; c.hasfont = 1; }
        if ((v = xml_attr(x, "font-style"))) { c.f.italic = !strcmp(v, "italic") || !strcmp(v, "oblique"); c.hasfont = 1; }
        if ((v = xml_attr(x, "text-underline-style"))) { c.f.underline = strcmp(v, "none") ? 1 : 0; c.hasfont = 1; }
        if ((v = xml_attr(x, "text-underline-type")) && !strcmp(v, "double") && c.f.underline) c.f.underline = 2;
        if ((v = xml_attr(x, "text-line-through-style"))) { c.f.strike = strcmp(v, "none") ? 1 : 0; c.hasfont = 1; }
        if ((v = xml_attr(x, "color"))) { c.f.color = hexcolor(v, COLOR_AUTO); if (c.f.color == 0) c.f.color = COLOR_AUTO; c.hasfont = 1; }
      } else if (!strcmp(x->name, "table-cell-properties")) {
        if ((v = xml_attr(x, "background-color")) && strcmp(v, "transparent")) c.st.fill = FILL_ON | hexcolor(v, 0xFFFFFF);
        if ((v = xml_attr(x, "border"))) { int k; for (k = 0; k < 4; k++) border_of(v, &c.st.border[k], &c.st.bcolor[k]); }
        border_of(xml_attr(x, "border-left"), &c.st.border[B_LEFT], &c.st.bcolor[B_LEFT]);
        border_of(xml_attr(x, "border-right"), &c.st.border[B_RIGHT], &c.st.bcolor[B_RIGHT]);
        border_of(xml_attr(x, "border-top"), &c.st.border[B_TOP], &c.st.bcolor[B_TOP]);
        border_of(xml_attr(x, "border-bottom"), &c.st.border[B_BOTTOM], &c.st.bcolor[B_BOTTOM]);
        if ((v = xml_attr(x, "wrap-option"))) c.st.wrap = !strcmp(v, "wrap");
        if ((v = xml_attr(x, "vertical-align"))) c.st.valign = !strcmp(v, "top") ? VA_TOP : !strcmp(v, "middle") ? VA_CENTER : VA_BOTTOM;
        if ((v = xml_attr(x, "shrink-to-fit"))) c.st.shrink = !strcmp(v, "true");
        if ((v = xml_attr(x, "rotation-angle"))) { int a = atoi(v) % 360; c.st.rotation = (i16)(a <= 90 ? a : a >= 270 ? a - 360 : 0); }
        if ((v = xml_attr(x, "cell-protect"))) { c.st.locked = strstr(v, "protected") != NULL; c.st.hidden = strstr(v, "formula-hidden") != NULL || strstr(v, "hidden-and-protected") != NULL; }
      } else if (!strcmp(x->name, "paragraph-properties")) {
        if ((v = xml_attr(x, "text-align"))) c.st.halign = !strcmp(v, "center") ? HA_CENTER : (!strcmp(v, "end") || !strcmp(v, "right")) ? HA_RIGHT : !strcmp(v, "justify") ? HA_JUSTIFY : HA_LEFT;
        if ((v = xml_attr(x, "margin-left")) && px_of(v) > 4) c.st.indent = (u8)(px_of(v) / 9 > 15 ? 15 : px_of(v) / 9);
      } else if (!strcmp(x->name, "table-column-properties")) {
        if ((v = xml_attr(x, "column-width"))) l.px = (u16)(px_of(v) + 0.5);
      } else if (!strcmp(x->name, "table-row-properties")) {
        if ((v = xml_attr(x, "row-height"))) l.px = (u16)(px_of(v) + 0.5);
        if ((v = xml_attr(x, "use-optimal-row-height"))) l.optimal = !strcmp(v, "true");
      } else if (!strcmp(x->name, "table-properties")) {
        if ((v = xml_attr(x, "display"))) l.hidden = !strcmp(v, "false");
      }
    }
  if (cell) {
    CStyle *nc = (CStyle *)realloc(o->cs, (size_t)(o->ncs + 1) * sizeof(CStyle));
    if (nc) { o->cs = nc; nc[o->ncs++] = c; }
  } else {
    LStyle *nl = (LStyle *)realloc(o->ls, (size_t)(o->nls + 1) * sizeof(LStyle));
    if (nl) { o->ls = nl; nl[o->nls++] = l; }
  }
}
/* a cell style as Ledger's style index (made on first use) */
static int style_index(Od *o, const char *name, int *kind)
{
  CStyle *c = cstyle(o, name);
  Style st;
  int i;
  if (kind) *kind = 0;
  if (!c) return 0;
  st = c->st;
  if (c->hasfont) { int fi = book_font(o->b, &c->f); st.font = (u16)(fi < 0 ? 0 : fi); }
  if (c->data[0])
    for (i = 0; i < o->nds; i++) if (!strcmp(o->ds[i].name, c->data)) { st.numfmt = (u16)book_fmt(o->b, o->ds[i].code); break; }
  i = book_style(o->b, &st);
  return i < 0 ? 0 : i;
}

/* ------------------------------------------------------------------ formulas */
static int is_ident(char ch) { return isalnum((u8)ch) || ch == '_' || ch == '.' || (u8)ch >= 0x80; }
/* an OpenFormula reference's inside ("$Sheet1.A1:.B2", "'My sheet'.A1", ".A:.A") as Excel's text */
static void ref_text_of(const char *s, size_t n, Buf *o)
{
  char part[2][300];
  size_t k, i = 0;
  int np = 0;
  const char *sheet = NULL;
  size_t sheetlen = 0;
  /* split at ':' outside quotes */
  while (i <= n && np < 2) {
    size_t st = i, len;
    int q = 0;
    while (i < n && (q || s[i] != ':')) { if (s[i] == '\'') q = !q; i++; }
    len = i - st < sizeof part[0] - 1 ? i - st : sizeof part[0] - 1;
    memcpy(part[np], s + st, len); part[np][len] = 0;
    np++;
    i++;
  }
  for (k = 0; k < (size_t)np; k++) {
    char *p = part[k], *dot = NULL;
    int q = 0;
    char *c;
    if (*p == '$') p++;
    for (c = p; *c; c++) { if (*c == '\'') q = !q; else if (*c == '.' && !q) dot = c; }
    if (k) buf_addc(o, ':');
    if (dot) {
      if (dot > p && k == 0) { sheet = p; sheetlen = (size_t)(dot - p); }
      if (k == 0 && sheet) {
        if (sheet[0] == '\'') buf_add(o, sheet, sheetlen);
        else {
          /* quoted when the name needs it */
          size_t j;
          int needq = 0;
          for (j = 0; j < sheetlen; j++) if (!isalnum((u8)sheet[j]) && sheet[j] != '_' && (u8)sheet[j] < 0x80) needq = 1;
          if (needq) buf_addc(o, '\'');
          buf_add(o, sheet, sheetlen);
          if (needq) buf_addc(o, '\'');
        }
        buf_addc(o, '!');
      }
      buf_adds(o, dot + 1);
    } else buf_adds(o, p);
  }
}
/* "of:=SUM([.A1:.A3];2)" as "SUM(A1:A3,2)" (malloc'd); NULL if not OpenFormula */
static char *of_formula(const char *f)
{
  Buf o;
  const char *p;
  if (!strncmp(f, "of:", 3)) f += 3;
  else if (!strncmp(f, "oooc:", 5)) f += 5;
  else if (strchr(f, ':') && strchr(f, ':') < strchr(f, '=') && !isalpha((u8)f[0])) return NULL;
  if (*f == '=') f++;
  buf_init(&o);
  for (p = f; *p; ) {
    if (*p == '"') {
      /* a string, its "" kept */
      buf_addc(&o, *p++);
      while (*p) { if (*p == '"' && p[1] == '"') { buf_adds(&o, "\"\""); p += 2; continue; } if (*p == '"') break; buf_addc(&o, *p++); }
      if (*p) buf_addc(&o, *p++);
      continue;
    }
    if (*p == '[') {
      const char *e = p + 1;
      int q = 0;
      while (*e && (q || *e != ']')) { if (*e == '\'') q = !q; e++; }
      ref_text_of(p + 1, (size_t)(e - p - 1), &o);
      p = *e ? e + 1 : e;
      continue;
    }
    if (*p == ';') { buf_addc(&o, ','); p++; continue; }
    if (*p == '~') { buf_addc(&o, ','); p++; continue; }
    if (*p == '!') { buf_addc(&o, ' '); p++; continue; }
    if (isalpha((u8)*p) || *p == '_') {
      /* a name or function: OpenFormula's prefixes dropped */
      const char *e = p;
      static const char *const pre[] = { "COM.MICROSOFT.", "ORG.OPENOFFICE.", "ORG.LIBREOFFICE.", "_XLFN.", "LEGACY.", NULL };
      int k;
      while (is_ident(*e)) e++;
      for (k = 0; pre[k]; k++) if ((size_t)(e - p) > strlen(pre[k]) && !strncasecmp(p, pre[k], strlen(pre[k]))) { p += strlen(pre[k]); break; }
      buf_add(&o, p, (size_t)(e - p));
      /* TRUE() and FALSE() as the constants */
      if (e[0] == '(' && e[1] == ')' && ((e - p == 4 && !strncasecmp(p, "TRUE", 4)) || (e - p == 5 && !strncasecmp(p, "FALSE", 5)))) e += 2;
      p = e;
      continue;
    }
    buf_addc(&o, *p++);
  }
  if (!o.data) return str_dup("");
  return buf_take(&o);
}

/* ------------------------------------------------------------------ cells */
static double iso_date(const char *v)
{
  int y = 0, m = 1, d = 1, hh = 0, mm = 0;
  double ss = 0;
  if (sscanf(v, "%d-%d-%d", &y, &m, &d) < 3) return NAN;
  if (strchr(v, 'T')) sscanf(strchr(v, 'T') + 1, "%d:%d:%lf", &hh, &mm, &ss);
  return date_serial(y, m, d) + (hh * 3600 + mm * 60 + ss) / 86400.0;
}
static double iso_time(const char *v)
{
  /* PT12H30M15S (or P1DT2H...) */
  double days = 0, h = 0, m = 0, s = 0, n;
  const char *p = v;
  int neg = 0;
  if (*p == '-') { neg = 1; p++; }
  if (*p == 'P') p++;
  while (*p) {
    char *e;
    if (*p == 'T') { p++; continue; }
    n = strtod(p, &e);
    if (e == p) break;
    if (*e == 'D') days = n; else if (*e == 'H') h = n; else if (*e == 'M') m = n; else if (*e == 'S') s = n;
    p = *e ? e + 1 : e;
  }
  n = days + (h * 3600 + m * 60 + s) / 86400.0;
  return neg ? -n : n;
}
/* the text of a cell's paragraphs (text:p, with text:s spaces, tabs and line breaks) */
static void cell_text(Xml *x, Buf *out)
{
  int depth = x->depth, paras = 0;
  if (x->pending_end) return;
  while (xml_next(x) != XT_EOF) {
    if (x->type == XT_END && x->depth < depth) break;
    if (x->type == XT_START) {
      if (!strcmp(x->name, "p")) { if (paras++) buf_addc(out, '\n'); }
      else if (!strcmp(x->name, "s")) { const char *c = xml_attr(x, "c"); int k, n = c ? atoi(c) : 1; for (k = 0; k < n && k < 1000; k++) buf_addc(out, ' '); }
      else if (!strcmp(x->name, "tab")) buf_addc(out, '\t');
      else if (!strcmp(x->name, "line-break")) buf_addc(out, '\n');
      else if (!strcmp(x->name, "annotation")) xml_skip(x);
    } else if (x->type == XT_TEXT && out->len < 32768) buf_add(out, x->text.data, x->text.len);
  }
}

typedef struct { int style; u16 px; u8 hidden, level; } ColDef;
static void read_table(Od *o, Xml *x, int si)
{
  Sheet *s = o->b->sh[si];
  int depth = x->depth, rgroup = 0, cgroup = 0, k;
  i32 row = 0, col = 0, ncdef = 0;
  ColDef *cdef = NULL;
  int rowstyle = 0;
  const char *v;
  if ((v = xml_attr(x, "protected")) && !strcmp(v, "true")) { s->protect = 1; s->protallow = PA_DEFAULT; }
  if (x->pending_end) return;
  while (xml_next(x) != XT_EOF) {
    const char *n = x->name;
    if (x->type == XT_END && x->depth < depth) break;
    if (x->type == XT_END) {
      if (!strcmp(n, "table-row-group") && rgroup > 0) rgroup--;
      else if (!strcmp(n, "table-column-group") && cgroup > 0) cgroup--;
      continue;
    }
    if (x->type != XT_START) continue;
    if (!strcmp(n, "table-row-group")) { rgroup++; continue; }
    if (!strcmp(n, "table-column-group")) { cgroup++; continue; }
    if (!strcmp(n, "table-column")) {
      const char *rep = xml_attr(x, "number-columns-repeated"), *st = xml_attr(x, "style-name"), *dc = xml_attr(x, "default-cell-style-name"), *vis = xml_attr(x, "visibility");
      long r = rep ? atol(rep) : 1, i;
      LStyle *ls = lstyle(o, st);
      int sty = dc && strcmp(dc, "Default") ? style_index(o, dc, NULL) : 0;
      if (r < 1) r = 1;
      for (i = 0; i < r && ncdef < MAXCOL && i < 1024; i++) {
        ColDef *nc = (ColDef *)realloc(cdef, (size_t)(ncdef + 1) * sizeof(ColDef));
        ColInfo *ci;
        if (!nc) break;
        cdef = nc;
        cdef[ncdef].style = sty; cdef[ncdef].px = ls ? ls->px : 0; cdef[ncdef].hidden = vis && strcmp(vis, "visible"); cdef[ncdef].level = (u8)(cgroup > OUTLINE_MAX ? OUTLINE_MAX : cgroup);
        if ((cdef[ncdef].px && abs((int)cdef[ncdef].px - s->defwidth) > 1) || cdef[ncdef].hidden || cdef[ncdef].level || sty) {
          if ((ci = sheet_col(s, ncdef, 1))) {
            if (cdef[ncdef].px) { ci->width = cdef[ncdef].px; ci->custom = 1; }
            ci->hidden = cdef[ncdef].hidden; ci->outline = cdef[ncdef].level;
            if (sty) ci->style = (u16)sty;
          }
        }
        ncdef++;
      }
      continue;
    }
    if (!strcmp(n, "table-row")) {
      const char *rep = xml_attr(x, "number-rows-repeated"), *st = xml_attr(x, "style-name"), *vis = xml_attr(x, "visibility"), *dc = xml_attr(x, "default-cell-style-name");
      long r = rep ? atol(rep) : 1, i;
      LStyle *ls = lstyle(o, st);
      int rdepth = x->depth;
      i32 r0 = row;
      Buf txt;
      if (r < 1) r = 1;
      rowstyle = dc && strcmp(dc, "Default") ? style_index(o, dc, NULL) : 0;
      /* the row's properties (for each repeat that still has a row to give them) */
      for (i = 0; i < r && row + i < MAXROW && i < 1024; i++) {
        int hid = vis && strcmp(vis, "visible");
        if ((ls && !ls->optimal && ls->px) || hid || rgroup) {
          Row *rw = sheet_row(s, row + (i32)i, 1);
          if (!rw) break;
          if (ls && !ls->optimal && ls->px) { rw->height = ls->px; rw->custom = 1; }
          if (hid) rw->hidden = 1;
          rw->outline = (u8)(rgroup > OUTLINE_MAX ? OUTLINE_MAX : rgroup);
        }
      }
      /* its cells */
      col = 0;
      buf_init(&txt);
      if (!x->pending_end)
        while (xml_next(x) != XT_EOF) {
          if (x->type == XT_END && x->depth < rdepth) break;
          if (x->type != XT_START) continue;
          if (!strcmp(x->name, "table-cell") || !strcmp(x->name, "covered-table-cell")) {
            const char *crep = xml_attr(x, "number-columns-repeated"), *cst = xml_attr(x, "style-name"), *vt = xml_attr(x, "value-type");
            const char *val = xml_attr(x, "value"), *dv = xml_attr(x, "date-value"), *tv = xml_attr(x, "time-value"), *bv = xml_attr(x, "boolean-value");
            const char *sv = xml_attr(x, "string-value"), *fm = xml_attr(x, "formula"), *cs = xml_attr(x, "number-columns-spanned"), *rs = xml_attr(x, "number-rows-spanned");
            long cr = crep ? atol(crep) : 1, j;
            int style, covered = !strcmp(x->name, "covered-table-cell");
            char *ftext = fm ? of_formula(fm) : NULL;
            char *vtype = vt ? str_dup(vt) : NULL, *vval = val ? str_dup(val) : NULL, *dval = dv ? str_dup(dv) : NULL, *tval = tv ? str_dup(tv) : NULL;
            char *bval = bv ? str_dup(bv) : NULL, *sval = sv ? str_dup(sv) : NULL;
            long span_c = cs ? atol(cs) : 1, span_r = rs ? atol(rs) : 1;
            if (cr < 1) cr = 1;
            style = cst ? style_index(o, cst, NULL) : col < ncdef ? cdef[col].style : rowstyle;
            txt.len = 0; if (txt.data) txt.data[0] = 0;
            cell_text(x, &txt);
            if (!covered && (span_c > 1 || span_r > 1) && s->nmerges < 100000 && row < MAXROW && col < MAXCOL) {
              Rect *nm = (Rect *)realloc(s->merges, (size_t)(s->nmerges + 1) * sizeof(Rect));
              if (nm) { s->merges = nm; nm[s->nmerges].r1 = row; nm[s->nmerges].c1 = col; nm[s->nmerges].r2 = row + (i32)span_r - 1; nm[s->nmerges].c2 = col + (i32)span_c - 1; s->nmerges++; }
            }
            for (j = 0; j < cr && col < MAXCOL; j++, col++) {
              int has = vtype || ftext || txt.len;
              Value vv = val_empty();
              if (!has && (!style || cr > 256 || r > 256)) continue;
              if (vtype && (!strcmp(vtype, "float") || !strcmp(vtype, "percentage") || !strcmp(vtype, "currency"))) vv = val_num(vval ? strtod(vval, NULL) : 0);
              else if (vtype && !strcmp(vtype, "date")) { double dd = dval ? iso_date(dval) : NAN; vv = dd == dd ? val_num(dd) : val_str(txt.data ? txt.data : ""); }
              else if (vtype && !strcmp(vtype, "time")) vv = val_num(tval ? iso_time(tval) : 0);
              else if (vtype && !strcmp(vtype, "boolean")) vv = val_bool(bval && (!strcmp(bval, "true") || !strcmp(bval, "1")));
              else if (vtype && !strcmp(vtype, "string")) vv = val_str(sval ? sval : txt.data ? txt.data : "");
              else if (txt.len) vv = val_str(txt.data);
              for (i = 0; i < r && row + i < MAXROW && i < 1024; i++) {
                Cell *cell = sheet_cell_make(s, row + (i32)i, col);
                if (!cell) break;
                val_free(&cell->v);
                cell->v = val_copy(&vv);
                cell->style = (u16)style;
                if (vtype && !strcmp(vtype, "percentage") && !style) {
                  Style st = o->b->styles[0];
                  int si2;
                  st.numfmt = (u16)book_fmt(o->b, "0%");
                  if ((si2 = book_style(o->b, &st)) > 0) cell->style = (u16)si2;
                }
                if (ftext) {
                  int pos;
                  Formula *f = formula_parse(o->b, si, ftext, &pos);
                  cell = sheet_cell(s, row + (i32)i, col);
                  if (f && cell) { formula_free(cell->f); cell->f = f; } else formula_free(f);
                }
              }
              val_free(&vv);
            }
            free(ftext); free(vtype); free(vval); free(dval); free(tval); free(bval); free(sval);
          }
        }
      buf_free(&txt);
      row = r0 + (i32)(r > MAXROW ? MAXROW : r);
      if (row > MAXROW) row = MAXROW;
      continue;
    }
    if (!strcmp(n, "shapes") || !strcmp(n, "forms") || !strcmp(n, "named-expressions")) xml_skip(x);
  }
  (void)k;
  free(cdef);
}
/* defined names: <table:named-range table:name= table:cell-range-address=> and named expressions */
static void read_names(Od *o, Xml *x)
{
  const char *nm = xml_attr(x, "name"), *ad = xml_attr(x, "cell-range-address"), *ex = xml_attr(x, "expression");
  char *t = NULL;
  if (!nm) return;
  if (ad) {
    /* "$Sheet1.$A$1:.$B$2" : as a bracketed reference */
    Buf b;
    buf_init(&b);
    ref_text_of(ad, strlen(ad), &b);
    t = b.data ? buf_take(&b) : NULL;
  } else if (ex) t = of_formula(ex);
  if (t) { book_define_name(o->b, nm, t, SHEET_SELF); free(t); }
}

Book *ods_load(const u8 *data, size_t len, char *err, size_t errcap)
{
  ZipReader z;
  size_t cl;
  u8 *content;
  Xml x;
  Od o;
  Book *b;
  int ntables = 0, i;
  if (err && errcap) err[0] = 0;
  if (!zip_open(&z, data, len)) { if (err) snprintf(err, errcap, "not an .ods file (no zip archive)"); return NULL; }
  if ((i = zip_find(&z, "content.xml")) < 0 || !(content = zip_read(&z, i, &cl))) { if (err) snprintf(err, errcap, "no content inside (content.xml)"); zip_close(&z); return NULL; }
  if (!(b = book_new())) { free(content); zip_close(&z); return NULL; }
  memset(&o, 0, sizeof o);
  o.b = b;
  /* the common number formats (styles.xml) */
  {
    size_t sl;
    u8 *st = (i = zip_find(&z, "styles.xml")) >= 0 ? zip_read(&z, i, &sl) : NULL;
    if (st) {
      xml_init(&x, (const char *)st, sl);
      while (xml_next(&x) != XT_EOF)
        if (x.type == XT_START && (!strcmp(x.name, "number-style") || !strcmp(x.name, "date-style") || !strcmp(x.name, "time-style") ||
            !strcmp(x.name, "percentage-style") || !strcmp(x.name, "currency-style"))) {
          char kind[32];
          strncpy(kind, x.name, sizeof kind - 1); kind[sizeof kind - 1] = 0;
          read_numstyle(&o, &x, kind);
        }
      xml_free(&x);
      free(st);
    }
  }
  zip_close(&z);
  /* the sheets first, so formulas can name any of them */
  xml_init(&x, (const char *)content, cl);
  while (xml_next(&x) != XT_EOF) {
    if (x.type != XT_START || strcmp(x.name, "table")) continue;
    {
      const char *nm = xml_attr(&x, "name");
      char name[32];
      int at;
      strncpy(name, nm ? nm : "Sheet", 31); name[31] = 0;
      if (!ntables) { if (!book_rename_sheet(b, 0, name)) { char u[32]; book_unique_sheet_name(b, name, u); book_rename_sheet(b, 0, u); } at = 0; }
      else if ((at = book_add_sheet(b, name, -1)) < 0) { char u[32]; book_unique_sheet_name(b, name, u); at = book_add_sheet(b, u, -1); }
      if (at >= 0) ntables++;
      xml_skip(&x);
    }
  }
  xml_free(&x);
  i = 0;
  xml_init(&x, (const char *)content, cl);
  while (xml_next(&x) != XT_EOF) {
    if (x.type != XT_START) continue;
    if (!strcmp(x.name, "style")) read_style(&o, &x);
    else if (strstr(x.name, "-style") && (!strcmp(x.name, "number-style") || !strcmp(x.name, "date-style") || !strcmp(x.name, "time-style") ||
             !strcmp(x.name, "percentage-style") || !strcmp(x.name, "currency-style") || !strcmp(x.name, "boolean-style") || !strcmp(x.name, "text-style"))) {
      char kind[32];
      strncpy(kind, x.name, sizeof kind - 1); kind[sizeof kind - 1] = 0;
      read_numstyle(&o, &x, kind);
    } else if (!strcmp(x.name, "table")) {
      LStyle *ls = lstyle(&o, xml_attr(&x, "style-name"));
      if (i >= ntables) { xml_skip(&x); continue; }
      if (ls && ls->hidden) b->sh[i]->hidden = 1;
      read_table(&o, &x, i++);
    } else if (!strcmp(x.name, "named-range") || !strcmp(x.name, "named-expression")) read_names(&o, &x);
  }
  xml_free(&x);
  free(content);
  for (i = 0; i < o.nds; i++) free(o.ds[i].code);
  free(o.ds); free(o.cs); free(o.ls);
  if (!ntables) { book_free(b); if (err) snprintf(err, errcap, "the spreadsheet has no tables"); return NULL; }
  {
    int vis = 0;
    for (i = 0; i < b->nsh; i++) vis += !b->sh[i]->hidden;
    if (!vis) b->sh[0]->hidden = 0;
    for (i = 0; i < b->nsh && b->sh[i]->hidden; i++) ;
    b->active = i < b->nsh ? i : 0;
  }
  book_forget_undo(b);
  book_recalc(b);
  b->dirty = 0;
  return b;
}
