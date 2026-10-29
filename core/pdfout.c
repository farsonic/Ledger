/*
 * pdfout.c - see pdfout.h. The PDF objects, deflated streams, Windows-1252 text and embedded
 * TrueType fonts are written as Quill's core/pdf.c writes them (Quill, the word processor Ledger
 * shares its core with); the pages are Ledger's own: cells laid out as the page setup says.
 *
 * Each page's content stream works in points from the page's top left (a flipped matrix), and the
 * cells inside another matrix that applies the print scale; text sets its own matrix the right
 * way up.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "pdfout.h"
#include "pagesetup.h"
#include "calc.h"
#include "cond.h"
#include "ops.h"
#include "zip.h"

/* ------------------------------------------------------------------ from Quill's pdf.c */
/* Windows-1252's 0x80-0x9F (0: not used) */
static const u16 cp1252_hi[32] = {
  0x20AC, 0, 0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021, 0x02C6, 0x2030, 0x0160, 0x2039, 0x0152, 0, 0x017D, 0,
  0, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014, 0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, 0, 0x017E, 0x0178 };
/* the code point of a Windows-1252 byte; 0 for the five unused codes */
static u32 code_cp(int c) { return c >= 0x80 && c < 0xA0 ? cp1252_hi[c - 0x80] : (u32)c; }
/* the Windows-1252 byte for a code point (the fonts' /WinAnsiEncoding), with a few look-alikes
   for characters it lacks; '?' for anything else */
static int cp_code(u32 cp)
{
  int k;
  if (cp < 0x80 || (cp >= 0xA0 && cp < 0x100)) return (int)cp;
  for (k = 0; k < 32; k++) if (cp1252_hi[k] && cp1252_hi[k] == cp) return 0x80 + k;
  if (cp == 0x2011) return '-';
  if (cp == 0x25AA || cp == 0x25BA) return 0x95;
  return '?';
}
#define MAXPFONTS 64
typedef struct {
  Book *b;
  FontSet *fs;
  const PdfOptions *opt;
  Buf o;
  u32 *off; u32 objcap;
  Face *fonts[MAXPFONTS]; int nfonts;
} Pdf;
static int font_index(Pdf *p, Face *f)
{
  int i;
  for (i = 0; i < p->nfonts; i++) if (p->fonts[i] == f) return i;
  if (p->nfonts >= MAXPFONTS) return 0;
  p->fonts[p->nfonts] = f;
  return p->nfonts++;
}
static void obj_start(Pdf *p, u32 n)
{
  if (n < p->objcap) p->off[n] = (u32)p->o.len;
  buf_printf(&p->o, "%lu 0 obj\n", (unsigned long)n);
}
/* a stream object, deflated (zdeflate makes raw deflate; FlateDecode wants the zlib wrapper) */
static void stream(Pdf *p, u32 n, const char *dict, const u8 *data, size_t len)
{
  size_t clen = 0;
  u8 *c = zdeflate(data, len, &clen);
  obj_start(p, n);
  if (c) {
    u32 a = 1, b = 0;
    size_t i;
    for (i = 0; i < len; i++) { a = (a + data[i]) % 65521; b = (b + a) % 65521; }
    buf_printf(&p->o, "<<%s /Filter /FlateDecode /Length %lu>>\nstream\n", dict, (unsigned long)(clen + 6));
    buf_addc(&p->o, 0x78); buf_addc(&p->o, 0x01);
    buf_add(&p->o, c, clen);
    buf_addc(&p->o, (char)(b >> 8)); buf_addc(&p->o, (char)b); buf_addc(&p->o, (char)(a >> 8)); buf_addc(&p->o, (char)a);
    free(c);
  } else {
    buf_printf(&p->o, "<<%s /Length %lu>>\nstream\n", dict, (unsigned long)len);
    buf_add(&p->o, data, len);
  }
  buf_adds(&p->o, "\nendstream\nendobj\n");
}
/* a PDF literal string of bytes (already Windows-1252) */
static void pdf_bytes(Buf *b, const char *s, size_t n)
{
  size_t i;
  buf_addc(b, '(');
  for (i = 0; i < n; i++) {
    u8 c = (u8)s[i];
    if (c == '(' || c == ')' || c == '\\') { buf_addc(b, '\\'); buf_addc(b, (char)c); }
    else if (c < 32 || c > 126) buf_printf(b, "\\%03o", c);
    else buf_addc(b, (char)c);
  }
  buf_addc(b, ')');
}
static void pdf_string(Buf *b, const char *s) { pdf_bytes(b, s, strlen(s)); }
/* UTF-8 to Windows-1252 bytes; returns the length */
static size_t to_1252(const char *s, size_t n, char *out, size_t cap)
{
  size_t i = 0, k = 0;
  while (i < n && k + 1 < cap) {
    int used;
    u32 cp = utf8_get(s + i, n - i, &used);
    if (cp == '\t' || cp == '\n' || cp == '\r') cp = ' ';
    out[k++] = (char)cp_code(cp);
    i += (size_t)used;
  }
  out[k] = 0;
  return k;
}
/* a PostScript name for the face; o needs room for 52 bytes */
static void ps_name(char *o, const Face *f)
{
  const char *s;
  size_t n = 0;
  for (s = f->family; *s && n < 40; s++) if (*s != ' ' && *s > 32 && *s < 127 && !strchr("()<>[]{}/%#", *s)) o[n++] = *s;
  o[n] = 0;
  if (f->bold || f->italic) strcat(o, f->bold && f->italic ? "-BoldItalic" : f->bold ? "-Bold" : "-Italic");
}

/* ------------------------------------------------------------------ drawing helpers */
static void num(Buf *o, double v)
{
  char t[32];
  int n;
  if (v > -0.0005 && v < 0.0005) v = 0;
  sprintf(t, "%.3f", v);
  n = (int)strlen(t);
  while (n > 1 && t[n - 1] == '0') t[--n] = 0;
  if (n && t[n - 1] == '.') t[--n] = 0;
  buf_adds(o, t);
  buf_addc(o, ' ');
}
static void colour(Buf *b, u32 c, const char *op)
{
  if (c == COLOR_AUTO || (c & 0xFF000000UL)) c = 0;
  num(b, ((c >> 16) & 255) / 255.0); num(b, ((c >> 8) & 255) / 255.0); num(b, (c & 255) / 255.0);
  buf_adds(b, op); buf_addc(b, '\n');
}
static void rect_fill(Buf *b, double x, double y, double w, double h, u32 c)
{
  colour(b, c, "rg");
  num(b, x); num(b, y); num(b, w); num(b, h); buf_adds(b, "re f\n");
}
static void line(Buf *b, double x0, double y0, double x1, double y1)
{
  num(b, x0); num(b, y0); buf_adds(b, "m "); num(b, x1); num(b, y1); buf_adds(b, "l S\n");
}
/* a font and the face it uses (NULL if there are no fonts) */
typedef struct { FaceSize *z; double pt; int fi; } PF;
static int pfont(Pdf *p, const char *family, int bold, int italic, int tenths, PF *out)
{
  int hp = tenths / 5;
  if (hp < 2) hp = 2;
  out->z = fonts_size(p->fs, family && family[0] ? family : "Calibri", bold, italic, (u16)hp);
  if (!out->z) return 0;
  out->pt = hp / 2.0;
  out->fi = font_index(p, out->z->face);
  return 1;
}
static double text_w(const PF *f, const char *s, size_t n) { return fonts_text(f->z, s, (u32)n) / (QFINE * 20.0); }
/* text (UTF-8) at x, baseline y (in the flipped space), with synthetic bold/italic for faces that lack them */
static void text_at(Pdf *p, Buf *b, const PF *f, int bold, int italic, const char *s, size_t n, double x, double y, u32 color)
{
  char enc[1100];
  size_t k = to_1252(s, n, enc, sizeof enc);
  int sb = bold && !f->z->face->bold, si = italic && !f->z->face->italic;
  if (!k) return;
  colour(b, color, "rg");
  if (sb) { colour(b, color, "RG"); buf_adds(b, "q "); num(b, f->pt / 30); buf_adds(b, "w "); }
  buf_printf(b, "BT /F%d ", f->fi); num(b, f->pt); buf_adds(b, "Tf ");
  if (sb) buf_adds(b, "2 Tr ");
  buf_adds(b, si ? "1 0 0.21 -1 " : "1 0 0 -1 "); num(b, x); num(b, y); buf_adds(b, "Tm ");
  pdf_bytes(b, enc, k);
  buf_adds(b, " Tj ET\n");
  if (sb) buf_adds(b, "Q\n");
  (void)p;
}
static void decorate(Buf *b, const PF *f, double x, double y, double w, int underline, int strike, u32 color)
{
  double t = f->pt / 18 + 0.1;
  colour(b, color, "rg");
  if (underline) { num(b, x); num(b, y + f->pt / 9); num(b, w); num(b, t); buf_adds(b, "re f\n"); }
  if (underline == 2) { num(b, x); num(b, y + f->pt / 9 + t * 2); num(b, w); num(b, t); buf_adds(b, "re f\n"); }
  if (strike) { num(b, x); num(b, y - f->pt / 3.5); num(b, w); num(b, t); buf_adds(b, "re f\n"); }
}

/* ------------------------------------------------------------------ one page */
typedef struct {
  int sheet;
  const PageSetup *ps;
  PageSetup *own;                 /* a copy made for this export (the selection, or no print area) */
  Pages pg;
} SheetPages;
#define MAXPC 512
typedef struct {
  i32 idx[MAXPC]; double pos[MAXPC + 1]; int n;    /* the rows (or columns) printed, and their tops (lefts), points at 100% */
} Axis;
static void axis_add(Sheet *s, Axis *a, int rows, i32 from, i32 to)
{
  i32 i;
  for (i = from; i <= to && a->n < MAXPC; i++) {
    double sz = (rows ? sheet_row_height(s, i) : sheet_col_width(s, i)) * 0.75;
    if (sz <= 0) continue;
    a->idx[a->n] = i;
    a->pos[a->n + 1] = a->pos[a->n] + sz;
    a->n++;
  }
}
static int empty_at(Sheet *s, i32 r, i32 c)
{
  Cell *cell = sheet_cell(s, r, c);
  Rect m;
  if (sheet_merge_at(s, r, c, &m)) return 0;
  return !cell || (!cell->f && cell->v.t == V_EMPTY);
}
static void border_side(Buf *b, int kind, u32 color, double x0, double y0, double x1, double y1, double k)
{
  double w = kind == BS_MEDIUM ? 1.0 : kind == BS_THICK ? 1.6 : kind == BS_HAIR ? 0.25 : 0.5;
  if (!kind) return;
  colour(b, color == COLOR_AUTO || !color ? 0 : color, "RG");
  num(b, w / k); buf_adds(b, "w ");
  if (kind == BS_DOTTED) { buf_adds(b, "["); num(b, 1 / k); buf_adds(b, "] 0 d "); }
  else if (kind == BS_DASHED) { buf_adds(b, "["); num(b, 3 / k); num(b, 2 / k); buf_adds(b, "] 0 d "); }
  if (kind == BS_DOUBLE) {
    double dx = y0 == y1 ? 0 : 0.8 / k, dy = y0 == y1 ? 0.8 / k : 0;
    line(b, x0 - dx, y0 - dy, x1 - dx, y1 - dy);
    line(b, x0 + dx, y0 + dy, x1 + dx, y1 + dy);
  } else line(b, x0, y0, x1, y1);
  if (kind == BS_DOTTED || kind == BS_DASHED) buf_adds(b, "[] 0 d\n");
}
/* one cell's text in the box x0..x1, y0..y1 (points at 100%), clipped to cx0..cx1 */
static void cell_text(Pdf *p, Buf *b, Sheet *s, int si, i32 r, i32 c, double x0, double y0, double x1, double y1, double cx0, double cx1)
{
  Book *bk = p->b;
  Cell *cell = sheet_cell(s, r, c);
  const Value *v;
  const Style *st;
  Font fnt;
  CondLook lk;
  char t[1024];
  u32 col = COLOR_AUTO;
  PF f;
  int ha, isnum, nl = 0, i;
  double avail, lh, ind;
  size_t starts[64], lens[64];
  if (!cell || (!cell->f && cell->v.t == V_EMPTY)) return;
  v = cell_value(bk, si, r, c);
  st = &bk->styles[sheet_cell_style(s, r, c)];
  fnt = bk->fonts[st->font];
  memset(&lk, 0, sizeof lk); lk.color = COLOR_AUTO;
  if (s->ncfs && cond_look(bk, si, r, c, &lk)) { fnt.bold |= lk.bold; fnt.italic |= lk.italic; fnt.underline |= lk.underline; fnt.strike |= lk.strike; }
  if (v->t == V_NUM && v->n == 0 && !s->showzeros && !(p->opt->formulas && cell->f)) return;
  if (!pfont(p, fnt.name, fnt.bold, fnt.italic, (fnt.size ? fnt.size : 110) * (fnt.vert ? 2 : 3) / 3, &f)) return;
  if (p->opt->formulas && cell->f) { book_edit_text(bk, si, r, c, t, sizeof t); isnum = 0; }
  else if (v->t == V_NUM && st->numfmt == 0) {
    /* General shows as many digits as the column has room for */
    int chars = (int)((x1 - x0 - 3) / (text_w(&f, "0", 1) > 0 ? text_w(&f, "0", 1) : 5));
    if (chars < 1) chars = 1;
    if (chars > 11) chars = 11;
    fmt_value(bk, "General", v, t, sizeof t, &col, chars);
    isnum = 1;
  } else { book_shown_text(bk, si, r, c, t, sizeof t, &col); isnum = v->t == V_NUM; }
  if (lk.color != COLOR_AUTO) col = lk.color;
  else if (col == COLOR_AUTO) col = fnt.color;
  ha = st->halign;
  if (ha == HA_GENERAL) ha = isnum || v->t == V_ERR ? HA_RIGHT : v->t == V_BOOL ? HA_CENTER : HA_LEFT;
  if (ha == HA_CENTERSEL || ha == HA_JUSTIFY || ha == HA_FILL) ha = ha == HA_FILL ? HA_LEFT : HA_CENTER;
  ind = st->indent * 7.5;
  avail = x1 - x0 - 3 - ind;
  lh = f.z->height / 20.0;
  if (lh < f.pt) lh = f.pt * 1.2;
  if (st->wrap || strchr(t, '\n')) {
    size_t at = 0, len = strlen(t);
    while (at < len && nl < 64) {
      size_t e = at, brk = 0;
      int hasbrk = 0;
      while (e < len && t[e] != '\n') {
        size_t nx = utf8_next(t, len, e);
        if (st->wrap && e > at && text_w(&f, t + at, nx - at) > avail) break;
        if (t[e] == ' ') { brk = e; hasbrk = 1; }
        e = nx;
      }
      if (e < len && t[e] != '\n' && hasbrk && st->wrap) e = brk;
      starts[nl] = at; lens[nl] = e - at; nl++;
      at = e;
      while (at < len && t[at] == ' ') at++;
      if (at < len && t[at] == '\n') at++;
    }
  } else { starts[0] = 0; lens[0] = strlen(t); nl = 1; }
  buf_adds(b, "q "); num(b, cx0); num(b, y0); num(b, cx1 - cx0); num(b, y1 - y0); buf_adds(b, "re W n\n");
  if (st->rotation && nl == 1 && !isnum) {
    /* text at an angle (Orientation): its baseline through the cell's middle */
    double ang = st->rotation * 3.14159265358979 / 180, c = cos(ang), sn = sin(ang), w = text_w(&f, t, strlen(t));
    double mx = (x0 + x1) / 2, my = (y0 + y1) / 2, tx = mx - c * w / 2 + sn * f.pt / 3, ty = my + sn * w / 2 + c * f.pt / 3;
    char enc[1100];
    size_t k = to_1252(t, strlen(t), enc, sizeof enc);
    buf_adds(b, "q "); num(b, x0); num(b, y0); num(b, x1 - x0); num(b, y1 - y0); buf_adds(b, "re W n\n");
    colour(b, col, "rg");
    buf_printf(b, "BT /F%d ", f.fi); num(b, f.pt); buf_adds(b, "Tf ");
    num(b, c); num(b, -sn); num(b, -sn); num(b, -c); num(b, tx); num(b, ty); buf_adds(b, "Tm ");
    pdf_bytes(b, enc, k);
    buf_adds(b, " Tj ET Q\n");
    buf_adds(b, "Q\n");
    return;
  }
  for (i = 0; i < nl; i++) {
    const char *ls = t + starts[i];
    size_t ll = lens[i];
    char hashes[64];
    double w = text_w(&f, ls, ll), tx, ty;
    if (isnum && nl == 1 && w > x1 - x0 - 2) {
      /* a number too wide for its column shows as ##### */
      double hw = text_w(&f, "#", 1);
      int k = (int)((x1 - x0 - 2) / (hw > 0 ? hw : 5));
      if (k < 1) k = 1;
      if (k > 63) k = 63;
      memset(hashes, '#', (size_t)k); hashes[k] = 0;
      ls = hashes; ll = (size_t)k; w = text_w(&f, ls, ll);
    }
    if (ha == HA_RIGHT) tx = x1 - 2 - w - ind;
    else if (ha == HA_CENTER) tx = x0 + (x1 - x0 - w) / 2;
    else tx = x0 + 2 + ind;
    if (st->valign == VA_TOP) ty = y0 + 1 + i * lh + f.z->ascent / 20.0;
    else if (st->valign == VA_CENTER) ty = y0 + (y1 - y0 - nl * lh) / 2 + i * lh + f.z->ascent / 20.0;
    else ty = y1 - 1.5 - (nl - 1 - i) * lh - f.z->descent / 20.0;
    if (fnt.vert) ty += fnt.vert == 1 ? -f.pt / 2 : f.pt / 4;
    text_at(p, b, &f, fnt.bold, fnt.italic, ls, ll, tx, ty, col);
    if (fnt.underline || fnt.strike) decorate(b, &f, tx, ty, w, fnt.underline, fnt.strike, col);
  }
  buf_adds(b, "Q\n");
}
/* a header or footer: its three sections on one line at baseline y (points from the page's top) */
static void header_footer(Pdf *p, Buf *b, const char *code, const HFContext *cx, double y, double left, double right, double pw, int footer)
{
  HFSection sec[3];
  int k, i;
  if (!code || !code[0]) return;
  hf_expand(code, cx, sec);
  for (k = 0; k < 3; k++) {
    double w = 0, x, maxpt = 0;
    PF fs[16];
    for (i = 0; i < sec[k].n; i++) {
      const HFRun *r = &sec[k].run[i];
      if (!pfont(p, r->font[0] ? r->font : p->b->fonts[0].name, r->bold, r->italic, r->size ? r->size * 10 : 110, &fs[i])) return;
      w += text_w(&fs[i], r->text, strlen(r->text));
      if (fs[i].pt > maxpt) maxpt = fs[i].pt;
    }
    x = k == 0 ? left : k == 1 ? (pw - w) / 2 : pw - right - w;
    for (i = 0; i < sec[k].n; i++) {
      const HFRun *r = &sec[k].run[i];
      double rw = text_w(&fs[i], r->text, strlen(r->text)), by = footer ? y : y + maxpt;
      text_at(p, b, &fs[i], r->bold, r->italic, r->text, strlen(r->text), x, by, r->color);
      if (r->underline || r->strike) decorate(b, &fs[i], x, by, rw, r->underline, r->strike, r->color);
      x += rw;
    }
  }
}
static void page_content(Pdf *p, const SheetPages *sp, int k, int number, int total, Buf *b)
{
  Book *bk = p->b;
  int si = sp->sheet;
  Sheet *s = bk->sh[si];
  const PageSetup *ps = sp->ps;
  const Pages *pg = &sp->pg;
  int rb, cb, i, j, first = k == 0;
  Axis ax, ay;
  double sc = pg->scale, ox, oy, hx = ps->headings ? 30 : 0, hy = ps->headings ? 14 : 0, cw, ch;
  HFContext cx;
  page_bands(pg, k, &rb, &cb);
  memset(&ax, 0, sizeof ax); memset(&ay, 0, sizeof ay);
  if (pg->tcol1 >= 0 && pg->cols[cb].a > pg->tcol2) axis_add(s, &ax, 0, pg->tcol1, pg->tcol2);
  axis_add(s, &ax, 0, pg->cols[cb].a, pg->cols[cb].b);
  if (pg->trow1 >= 0 && pg->rows[rb].a > pg->trow2) axis_add(s, &ay, 1, pg->trow1, pg->trow2);
  axis_add(s, &ay, 1, pg->rows[rb].a, pg->rows[rb].b);
  cw = ax.pos[ax.n] + hx; ch = ay.pos[ay.n] + hy;
  /* the page: points from its top left; then the cells at the print scale */
  buf_adds(b, "1 0 0 -1 0 "); num(b, pg->ph); buf_adds(b, "cm\n");
  ox = ps->margin[M_LEFT] * 72; oy = ps->margin[M_TOP] * 72;
  if (ps->centerh) { double room = pg->pw - (ps->margin[M_LEFT] + ps->margin[M_RIGHT]) * 72 - cw * sc; if (room > 0) ox += room / 2; }
  if (ps->centerv) { double room = pg->ph - (ps->margin[M_TOP] + ps->margin[M_BOTTOM]) * 72 - ch * sc; if (room > 0) oy += room / 2; }
  /* headers and footers (at their own size: Scale with Document only matters for scaled pages) */
  memset(&cx, 0, sizeof cx);
  cx.page = number; cx.pages = total; cx.file = p->opt->file; cx.path = p->opt->path; cx.sheet = s->name; cx.now = p->opt->now;
  {
    double l = ps->alignhf ? ps->margin[M_LEFT] * 72 : 0.7 * 72, r = ps->alignhf ? ps->margin[M_RIGHT] * 72 : 0.7 * 72;
    header_footer(p, b, ps->hf[hf_which(ps, 0, first, number)], &cx, ps->margin[M_HEADER] * 72, l, r, pg->pw, 0);
    header_footer(p, b, ps->hf[hf_which(ps, 1, first, number)], &cx, pg->ph - ps->margin[M_FOOTER] * 72, l, r, pg->pw, 1);
  }
  buf_adds(b, "q "); num(b, sc); buf_adds(b, "0 0 "); num(b, sc); num(b, ox); num(b, oy); buf_adds(b, "cm\n");
  /* the headings: column letters and row numbers in grey boxes */
  if (ps->headings) {
    PF f;
    if (pfont(p, bk->fonts[0].name, 0, 0, 100, &f)) {
      buf_adds(b, "0.5 w\n");
      for (j = 0; j < ax.n; j++) {
        char t[16];
        double x0 = hx + ax.pos[j], x1 = hx + ax.pos[j + 1];
        col_name(ax.idx[j], t);
        rect_fill(b, x0, 0, x1 - x0, hy, 0xE8E8E8);
        colour(b, 0x808080, "RG"); num(b, x0); buf_adds(b, "0 "); num(b, x1 - x0); num(b, hy); buf_adds(b, "re S\n");
        text_at(p, b, &f, 0, 0, t, strlen(t), (x0 + x1 - text_w(&f, t, strlen(t))) / 2, hy - 3.5, 0);
      }
      for (i = 0; i < ay.n; i++) {
        char t[16];
        double y0 = hy + ay.pos[i], y1 = hy + ay.pos[i + 1];
        sprintf(t, "%ld", (long)ay.idx[i] + 1);
        rect_fill(b, 0, y0, hx, y1 - y0, 0xE8E8E8);
        colour(b, 0x808080, "RG"); buf_adds(b, "0 "); num(b, y0); num(b, hx); num(b, y1 - y0); buf_adds(b, "re S\n");
        text_at(p, b, &f, 0, 0, t, strlen(t), (hx - text_w(&f, t, strlen(t))) / 2, (y0 + y1) / 2 + f.pt / 3, 0);
      }
    }
    buf_adds(b, "1 0 0 1 "); num(b, hx); num(b, hy); buf_adds(b, "cm\n");
  }
  /* fills (and data bars) */
  for (i = 0; i < ay.n; i++)
    for (j = 0; j < ax.n; j++) {
      i32 r = ay.idx[i], c = ax.idx[j];
      const Style *st = &bk->styles[sheet_cell_style(s, r, c)];
      CondLook lk;
      u32 fill = st->fill;
      double x0 = ax.pos[j], y0 = ay.pos[i], w = ax.pos[j + 1] - x0, h = ay.pos[i + 1] - y0;
      Rect m;
      memset(&lk, 0, sizeof lk); lk.bar = -1;
      if (s->ncfs && cond_look(bk, si, r, c, &lk) && lk.fill) fill = lk.fill;
      if (sheet_merge_at(s, r, c, &m) && (m.r1 != r || m.c1 != c)) fill = bk->styles[sheet_cell_style(s, m.r1, m.c1)].fill;
      if (fill) rect_fill(b, x0, y0, w, h, fill & 0xFFFFFF);
      if (lk.bar >= 0) rect_fill(b, x0 + 1, y0 + 2, (w - 2) * lk.bar / 1000.0, h - 4, lk.barcolor & 0xFFFFFF);
    }
  /* gridlines */
  if (ps->gridlines) {
    buf_adds(b, "0.6 0.6 0.6 RG "); num(b, 0.4 / sc); buf_adds(b, "w\n");
    for (i = 0; i <= ay.n; i++) line(b, 0, ay.pos[i], ax.pos[ax.n], ay.pos[i]);
    for (j = 0; j <= ax.n; j++) line(b, ax.pos[j], 0, ax.pos[j], ay.pos[ay.n]);
  }
  /* text: merged areas from their first cell, overflowing text into empty neighbours */
  for (i = 0; i < ay.n; i++)
    for (j = 0; j < ax.n; j++) {
      i32 r = ay.idx[i], c = ax.idx[j];
      Rect m;
      double x0 = ax.pos[j], y0 = ay.pos[i], x1 = ax.pos[j + 1], y1 = ay.pos[i + 1], cx0, cx1;
      if (sheet_merge_at(s, r, c, &m)) {
        int a, e;
        if (m.r1 != r || m.c1 != c) continue;
        for (a = j; a + 1 < ax.n && ax.idx[a + 1] <= m.c2; a++) ;
        for (e = i; e + 1 < ay.n && ay.idx[e + 1] <= m.r2; e++) ;
        x1 = ax.pos[a + 1]; y1 = ay.pos[e + 1];
        cell_text(p, b, s, si, r, c, x0, y0, x1, y1, x0, x1);
        continue;
      }
      if (empty_at(s, r, c)) continue;
      cx0 = x0; cx1 = x1;
      {
        const Style *st = &bk->styles[sheet_cell_style(s, r, c)];
        const Value *v = cell_value(bk, si, r, c);
        if (!st->wrap && v->t == V_STR) {
          int a = j, e = j, ha = st->halign;
          if (ha == HA_GENERAL || ha == HA_LEFT || ha == HA_CENTER) while (e + 1 < ax.n && ax.idx[e + 1] == ax.idx[e] + 1 && empty_at(s, r, ax.idx[e + 1]) && e - j < 40) e++;
          if (ha == HA_RIGHT || ha == HA_CENTER) while (a > 0 && ax.idx[a - 1] == ax.idx[a] - 1 && empty_at(s, r, ax.idx[a - 1]) && j - a < 40) a--;
          cx0 = ax.pos[a]; cx1 = ax.pos[e + 1];
          if (ha == HA_CENTER) { double half = (cx1 - cx0) / 2, mid = (x0 + x1) / 2; x0 = mid - half; x1 = mid + half; }
          else if (ha == HA_RIGHT) x0 = cx0; else x1 = cx1;
        }
      }
      cell_text(p, b, s, si, r, c, x0, y0, x1, y1, cx0, cx1);
    }
  /* borders, over everything */
  for (i = 0; i < ay.n; i++)
    for (j = 0; j < ax.n; j++) {
      i32 r = ay.idx[i], c = ax.idx[j];
      const Style *st = &bk->styles[sheet_cell_style(s, r, c)];
      double x0 = ax.pos[j], y0 = ay.pos[i], x1 = ax.pos[j + 1], y1 = ay.pos[i + 1];
      if (!st->border[0] && !st->border[1] && !st->border[2] && !st->border[3]) continue;
      border_side(b, st->border[B_LEFT], st->bcolor[B_LEFT], x0, y0, x0, y1, sc);
      border_side(b, st->border[B_RIGHT], st->bcolor[B_RIGHT], x1, y0, x1, y1, sc);
      border_side(b, st->border[B_TOP], st->bcolor[B_TOP], x0, y0, x1, y0, sc);
      border_side(b, st->border[B_BOTTOM], st->bcolor[B_BOTTOM], x0, y1, x1, y1, sc);
    }
  buf_adds(b, "Q\n");
}

/* ------------------------------------------------------------------ the file */
/* the sheets to export with their pages; returns how many (sp malloc'd: free with release) */
static int collect(Book *b, const PdfOptions *o, SheetPages **out)
{
  int i, n = 0;
  SheetPages *sp = (SheetPages *)calloc((size_t)b->nsh, sizeof *sp);
  *out = sp;
  if (!sp) return 0;
  for (i = 0; i < b->nsh; i++) {
    Sheet *s = b->sh[i];
    PageSetup *save = s->page, *t = NULL;
    int ok;
    if (o->what != PDF_BOOK && i != b->active) continue;
    if (o->what == PDF_BOOK && s->hidden) continue;
    if (o->what == PDF_SELECTION || o->ignore_area) {
      /* the selection (or the whole sheet) for this export only */
      t = s->page ? page_copy(s->page) : (PageSetup *)malloc(sizeof(PageSetup));
      if (!t) continue;
      if (!s->page) page_defaults(t);
      if (o->what == PDF_SELECTION) t->area = s->sel; else t->area.r1 = -1;
      if (t->area.r1 >= 0 && (t->area.r2 >= MAXROW - 1 || t->area.c2 >= MAXCOL - 1)) {
        i32 lr = sheet_last_row(s), lc = sheet_last_col(s);
        if (t->area.r2 > lr) t->area.r2 = lr > t->area.r1 ? lr : t->area.r1;
        if (t->area.c2 > lc) t->area.c2 = lc > t->area.c1 ? lc : t->area.c1;
      }
      s->page = t;
    }
    sp[n].sheet = i;
    ok = page_layout(b, i, &sp[n].pg);
    s->page = save;
    if (!ok) { page_free(t); continue; }
    sp[n].own = t;
    sp[n].ps = t ? t : page_of(s);
    n++;
  }
  return n;
}
static void release(SheetPages *sp, int n)
{
  int i;
  for (i = 0; i < n; i++) { pages_free(&sp[i].pg); page_free(sp[i].own); }
  free(sp);
}
int pdf_count_pages(Book *b, const PdfOptions *o)
{
  SheetPages *sp;
  int n = collect(b, o, &sp), i, t = 0;
  for (i = 0; i < n; i++) t += sp[i].pg.npages;
  release(sp, n);
  return t;
}
u8 *pdf_book(Book *b, FontSet *fs, const PdfOptions *o, size_t *len)
{
  Pdf p;
  SheetPages *sp;
  int nsp = collect(b, o, &sp), i, k, total = 0, np = 0;
  u32 pg0, xref, n;
  Buf *pages;
  double *pw, *ph;
  if (!nsp) { release(sp, 0); return NULL; }
  for (i = 0; i < nsp; i++) total += sp[i].pg.npages;
  memset(&p, 0, sizeof p);
  p.b = b; p.fs = fs; p.opt = o;
  buf_init(&p.o);
  pages = (Buf *)calloc((size_t)total, sizeof(Buf));
  pw = (double *)calloc((size_t)total, sizeof(double)); ph = (double *)calloc((size_t)total, sizeof(double));
  if (!pages || !pw || !ph) { free(pages); free(pw); free(ph); release(sp, nsp); return NULL; }
  /* the pages' content first: it tells which fonts are used */
  for (i = 0; i < nsp; i++) {
    int start = sp[i].ps->firstpage ? sp[i].ps->firstpage : 1;
    for (k = 0; k < sp[i].pg.npages; k++) {
      buf_init(&pages[np]);
      page_content(&p, &sp[i], k, (o->what == PDF_BOOK && !sp[i].ps->firstpage ? np : k) + start, o->what == PDF_BOOK ? total : sp[i].pg.npages, &pages[np]);
      pw[np] = sp[i].pg.pw; ph[np] = sp[i].pg.ph;
      np++;
    }
  }
  release(sp, nsp);
  pg0 = 4 + 3 * (u32)p.nfonts;
  p.objcap = pg0 + 2 * (u32)np + 1;
  if (!(p.off = (u32 *)calloc(p.objcap, sizeof(u32)))) { for (k = 0; k < np; k++) buf_free(&pages[k]); free(pages); free(pw); free(ph); return NULL; }
  buf_adds(&p.o, "%PDF-1.4\n%\xE2\xE3\xCF\xD3\n");
  obj_start(&p, 1); buf_adds(&p.o, "<</Type /Catalog /Pages 2 0 R>>\nendobj\n");
  obj_start(&p, 2);
  buf_adds(&p.o, "<</Type /Pages /Kids [");
  for (k = 0; k < np; k++) buf_printf(&p.o, "%lu 0 R ", (unsigned long)(pg0 + 2 * k));
  buf_printf(&p.o, "] /Count %d>>\nendobj\n", np);
  obj_start(&p, 3);
  buf_adds(&p.o, "<</Producer (Ledger for the Amiga)");
  if (b->title[0]) { char t[200]; to_1252(b->title, strlen(b->title), t, sizeof t); buf_adds(&p.o, " /Title "); pdf_string(&p.o, t); }
  if (b->author[0]) { char t[200]; to_1252(b->author, strlen(b->author), t, sizeof t); buf_adds(&p.o, " /Author "); pdf_string(&p.o, t); }
  if (b->lx && b->lx->subject[0]) { char t[200]; to_1252(b->lx->subject, strlen(b->lx->subject), t, sizeof t); buf_adds(&p.o, " /Subject "); pdf_string(&p.o, t); }
  if (b->lx && b->lx->keywords[0]) { char t[200]; to_1252(b->lx->keywords, strlen(b->lx->keywords), t, sizeof t); buf_adds(&p.o, " /Keywords "); pdf_string(&p.o, t); }
  buf_adds(&p.o, ">>\nendobj\n");
  for (n = 0; n < (u32)p.nfonts; n++) {
    Face *f = p.fonts[n];
    char name[64];
    size_t flen = 0;
    u8 *file = fonts_embeddable(f) ? fonts_face_file(f, &flen) : NULL;
    int c;
    i32 upm = f->upm ? f->upm : 1000;
    ps_name(name, f);
    obj_start(&p, 4 + 3 * n);
    buf_printf(&p.o, "<</Type /Font /Subtype /%s /BaseFont /%s /FirstChar 32 /LastChar 255 /Encoding /WinAnsiEncoding /FontDescriptor %lu 0 R /Widths [",
               f->cff ? "Type1" : "TrueType", name, (unsigned long)(5 + 3 * n));
    for (c = 32; c < 256; c++) {
      u32 cp = code_cp(c);
      u16 g = cp ? ttf_glyph(f, cp) : 0;
      u16 adv = f->nmetrics ? f->adv[g < f->nmetrics ? g : f->nmetrics - 1] : 500;
      buf_printf(&p.o, "%ld ", (long)adv * 1000 / upm);
    }
    buf_adds(&p.o, "]>>\nendobj\n");
    obj_start(&p, 5 + 3 * n);
    buf_printf(&p.o, "<</Type /FontDescriptor /FontName /%s /Flags %d /FontBBox [-600 %ld 2000 %ld] /ItalicAngle %d /Ascent %ld /Descent %ld /CapHeight %ld /StemV %d",
               name, 32 | (f->italic ? 64 : 0), (long)(-f->descent * 1000 / upm), (long)(f->ascent * 1000 / upm), f->italic ? -12 : 0,
               (long)(f->ascent * 1000 / upm), (long)(-f->descent * 1000 / upm), (long)(f->ascent * 700 / upm), f->bold ? 140 : 80);
    if (file) buf_printf(&p.o, " /FontFile%s %lu 0 R", f->cff ? "3" : "2", (unsigned long)(6 + 3 * n));
    buf_adds(&p.o, ">>\nendobj\n");
    if (file) {
      char dict[64];
      if (f->cff) strcpy(dict, "/Subtype /OpenType");
      else sprintf(dict, "/Length1 %lu", (unsigned long)flen);
      stream(&p, 6 + 3 * n, dict, file, flen);
      free(file);
    } else { obj_start(&p, 6 + 3 * n); buf_adds(&p.o, "null\nendobj\n"); }
  }
  for (k = 0; k < np; k++) {
    obj_start(&p, pg0 + 2 * k);
    buf_adds(&p.o, "<</Type /Page /Parent 2 0 R /MediaBox [0 0 ");
    num(&p.o, pw[k]); num(&p.o, ph[k]);
    buf_printf(&p.o, "] /Contents %lu 0 R /Resources <</Font <<", (unsigned long)(pg0 + 2 * k + 1));
    for (n = 0; n < (u32)p.nfonts; n++) buf_printf(&p.o, "/F%lu %lu 0 R ", (unsigned long)n, (unsigned long)(4 + 3 * n));
    buf_adds(&p.o, ">> >> >>\nendobj\n");
    stream(&p, pg0 + 2 * k + 1, "", (const u8 *)(pages[k].data ? pages[k].data : ""), pages[k].len);
    buf_free(&pages[k]);
  }
  free(pages); free(pw); free(ph);
  xref = (u32)p.o.len;
  buf_printf(&p.o, "xref\n0 %lu\n0000000000 65535 f \n", (unsigned long)p.objcap);
  for (n = 1; n < p.objcap; n++) buf_printf(&p.o, "%010lu 00000 n \n", (unsigned long)p.off[n]);
  buf_printf(&p.o, "trailer\n<</Size %lu /Root 1 0 R /Info 3 0 R>>\nstartxref\n%lu\n%%%%EOF\n", (unsigned long)p.objcap, (unsigned long)xref);
  free(p.off);
  if (p.o.oom) { buf_free(&p.o); return NULL; }
  *len = p.o.len;
  return (u8 *)buf_take(&p.o);
}
int pdf_save_book(Book *b, FontSet *fs, const PdfOptions *o, const char *path)
{
  size_t n;
  u8 *data = pdf_book(b, fs, o, &n);
  int ok;
  if (!data) return 0;
  ok = file_write(path, data, n);
  free(data);
  return ok;
}
