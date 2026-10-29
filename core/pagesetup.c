/*
 * pagesetup.c - see pagesetup.h. Page setup as GridCraft keeps it (its PrintSettings: orientation,
 * paper, six margins, print area, title rows/columns, scale or fit-to, gridlines/headings, centring,
 * headers and footers with different first and odd/even pages, manual breaks), written in C from
 * that description and from the .xlsx schema.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "pagesetup.h"

/* s copied into d (cap bytes), cut short if need be */
static void scopy(char *d, size_t cap, const char *s) { size_t n = strlen(s); if (n >= cap) n = cap - 1; memcpy(d, s, n); d[n] = 0; }

/* ------------------------------------------------------------------ paper */
#define MM(x) ((x) * 72.0 / 25.4)
const Paper page_papers[] = {
  { "Letter", 1, 612, 792 }, { "Legal", 5, 612, 1008 }, { "Tabloid", 3, 792, 1224 }, { "Executive", 7, 522, 756 },
  { "A3", 8, MM(297), MM(420) }, { "A4", 9, MM(210), MM(297) }, { "A5", 11, MM(148), MM(210) }, { "A6", 70, MM(105), MM(148) },
  { "B4 (JIS)", 12, MM(257), MM(364) }, { "B5 (JIS)", 13, MM(182), MM(257) }, { "Envelope #10", 20, 297, 684 }, { "Envelope DL", 27, MM(110), MM(220) },
};
const int page_npapers = (int)(sizeof page_papers / sizeof page_papers[0]);
int page_paper_by_code(int code)
{
  int i;
  for (i = 0; i < page_npapers; i++) if (page_papers[i].code == code) return i;
  return 0;
}

/* ------------------------------------------------------------------ the setup */
void page_defaults(PageSetup *p)
{
  memset(p, 0, sizeof *p);
  p->margin[M_LEFT] = p->margin[M_RIGHT] = 0.7;
  p->margin[M_TOP] = p->margin[M_BOTTOM] = 0.75;
  p->margin[M_HEADER] = p->margin[M_FOOTER] = 0.3;
  p->scale = 100;
  p->fitw = p->fith = 1;
  p->scalehf = p->alignhf = 1;
  p->area.r1 = -1;
  p->trow1 = p->trow2 = p->tcol1 = p->tcol2 = -1;
  p->rdhf = -1;
}
PageSetup *page_get(Sheet *s)
{
  if (!s->page && (s->page = (PageSetup *)malloc(sizeof(PageSetup)))) page_defaults(s->page);
  return s->page;
}
const PageSetup *page_of(Sheet *s)
{
  static PageSetup dflt;
  static int made;
  if (s && s->page) return s->page;
  if (!made) { page_defaults(&dflt); made = 1; }
  return &dflt;
}
void page_free(PageSetup *p)
{
  int i;
  if (!p) return;
  for (i = 0; i < HF_N; i++) free(p->hf[i]);
  free(p->rbrk); free(p->cbrk);
  free(p);
}
PageSetup *page_copy(const PageSetup *p)
{
  PageSetup *c;
  int i;
  if (!p || !(c = (PageSetup *)malloc(sizeof *c))) return NULL;
  *c = *p;
  for (i = 0; i < HF_N; i++) c->hf[i] = p->hf[i] ? str_dup(p->hf[i]) : NULL;
  c->rbrk = NULL; c->cbrk = NULL;
  if (p->nrbrk && (c->rbrk = (i32 *)malloc((size_t)p->nrbrk * sizeof(i32)))) memcpy(c->rbrk, p->rbrk, (size_t)p->nrbrk * sizeof(i32)); else c->nrbrk = 0;
  if (p->ncbrk && (c->cbrk = (i32 *)malloc((size_t)p->ncbrk * sizeof(i32)))) memcpy(c->cbrk, p->cbrk, (size_t)p->ncbrk * sizeof(i32)); else c->ncbrk = 0;
  return c;
}
void page_set_hf(PageSetup *p, int which, const char *text)
{
  if (which < 0 || which >= HF_N) return;
  free(p->hf[which]);
  p->hf[which] = text && text[0] ? str_dup(text) : NULL;
}
void page_margin_preset(PageSetup *p, int k)
{
  static const double m[3][6] = { { 0.7, 0.7, 0.75, 0.75, 0.3, 0.3 }, { 1, 1, 1, 1, 0.5, 0.5 }, { 0.25, 0.25, 0.75, 0.75, 0.3, 0.3 } };
  if (k >= 0 && k < 3) memcpy(p->margin, m[k], sizeof p->margin);
}
/* a position after rows/columns at..at+|n| went in (n > 0) or out; -1 if it went with them */
static i32 shifted(i32 v, i32 at, i32 n)
{
  if (v < at) return v;
  if (n > 0) return v + n;
  if (v < at - n) return -1;
  return v + n;
}
static void shift_list(i32 *l, int *n, i32 at, i32 d)
{
  int i, k;
  for (i = k = 0; i < *n; i++) { i32 v = shifted(l[i], at, d); if (v > 0 && (!k || l[k - 1] != v)) l[k++] = v; }
  *n = k;
}
void page_shift(Sheet *s, int rows, i32 at, i32 n)
{
  PageSetup *p = s->page;
  if (!p) return;
  if (rows) shift_list(p->rbrk, &p->nrbrk, at, n); else shift_list(p->cbrk, &p->ncbrk, at, n);
  if (p->area.r1 >= 0) {
    i32 *a = rows ? &p->area.r1 : &p->area.c1, *b = rows ? &p->area.r2 : &p->area.c2;
    i32 na = shifted(*a, at, n), nb = shifted(*b, at, n);
    if (na < 0) na = at;
    if (nb < 0) nb = at - 1;
    if (nb < na) p->area.r1 = -1; else { *a = na; *b = nb; }
  }
  {
    i32 *a = rows ? &p->trow1 : &p->tcol1, *b = rows ? &p->trow2 : &p->tcol2;
    if (*a >= 0) {
      i32 na = shifted(*a, at, n), nb = shifted(*b, at, n);
      if (na < 0) na = at;
      if (nb < 0) nb = at - 1;
      if (nb < na) *a = *b = -1; else { *a = na; *b = nb; }
    }
  }
}
int page_has_break(const Sheet *s, int rows, i32 at)
{
  const PageSetup *p = s->page;
  int i;
  if (!p) return 0;
  for (i = 0; i < (rows ? p->nrbrk : p->ncbrk); i++) if ((rows ? p->rbrk : p->cbrk)[i] == at) return 1;
  return 0;
}
int page_set_break(Sheet *s, int rows, i32 at, int on)
{
  PageSetup *p;
  i32 **l;
  int *n, i;
  if (at <= 0 || at >= (rows ? MAXROW : MAXCOL)) return 0;
  if (page_has_break(s, rows, at) == !!on) return 0;
  if (!(p = page_get(s))) return 0;
  l = rows ? &p->rbrk : &p->cbrk; n = rows ? &p->nrbrk : &p->ncbrk;
  if (on) {
    i32 *nl = (i32 *)realloc(*l, (size_t)(*n + 1) * sizeof(i32));
    if (!nl) return 0;
    *l = nl;
    for (i = *n; i > 0 && nl[i - 1] > at; i--) nl[i] = nl[i - 1];
    nl[i] = at; (*n)++;
  } else {
    for (i = 0; i < *n && (*l)[i] != at; i++) ;
    if (i == *n) return 0;
    memmove(*l + i, *l + i + 1, (size_t)(*n - i - 1) * sizeof(i32));
    (*n)--;
  }
  return 1;
}
void page_reset_breaks(Sheet *s)
{
  PageSetup *p = s->page;
  if (!p) return;
  free(p->rbrk); free(p->cbrk);
  p->rbrk = p->cbrk = NULL; p->nrbrk = p->ncbrk = 0;
}

/* ------------------------------------------------------------------ pages */
double page_rows_pt(Sheet *s, i32 a, i32 b)
{
  double t = 0;
  i32 r;
  for (r = a; r <= b; r++) t += sheet_row_height(s, r) * 0.75;
  return t;
}
double page_cols_pt(Sheet *s, i32 a, i32 b)
{
  double t = 0;
  i32 c;
  for (c = a; c <= b; c++) t += sheet_col_width(s, c) * 0.75;
  return t;
}
/* the used range: the first and last rows and columns with cells (A1 if none) */
static Rect used_range(Sheet *s)
{
  Rect u;
  i32 r;
  u.r1 = -1; u.c1 = MAXCOL; u.r2 = 0; u.c2 = 0;
  for (r = 0; r < s->nrows; r++) {
    Row *row = s->rows[r];
    int i;
    if (!row) continue;
    for (i = 0; i < row->n; i++) {
      const Cell *c = &row->c[i];
      if (!c->f && c->v.t == V_EMPTY && !c->style) continue;
      if (u.r1 < 0) u.r1 = r;
      u.r2 = r;
      if (c->col < u.c1) u.c1 = c->col;
      if (c->col > u.c2) u.c2 = c->col;
    }
  }
  {
    int i;
    for (i = 0; i < s->nmerges; i++) {
      if (u.r1 < 0) { u.r1 = s->merges[i].r1; u.c1 = s->merges[i].c1; }
      if (s->merges[i].r2 > u.r2) u.r2 = s->merges[i].r2;
      if (s->merges[i].c2 > u.c2) u.c2 = s->merges[i].c2;
      if (s->merges[i].r1 < u.r1) u.r1 = s->merges[i].r1;
      if (s->merges[i].c1 < u.c1) u.c1 = s->merges[i].c1;
    }
  }
  if (u.r1 < 0) { u.r1 = u.r2 = 0; u.c1 = u.c2 = 0; }
  return u;
}
static int add_band(Band **l, int *n, int *cap, i32 a, i32 b)
{
  if (*n == *cap) {
    int nc = *cap ? *cap * 2 : 16;
    Band *nl = (Band *)realloc(*l, (size_t)nc * sizeof(Band));
    if (!nl) return 0;
    *l = nl; *cap = nc;
  }
  (*l)[*n].a = a; (*l)[*n].b = b; (*n)++;
  return 1;
}
/* the bands of rows (rows) or columns from a to b: a band ends where the next one would no longer
   fit in avail points (at scale), or at a manual break. Titles take their room off every band
   that doesn't start among them */
static int bands(Sheet *s, int rows, i32 a, i32 b, double avail, double title, i32 t2, const i32 *brk, int nbrk, Band **out, int *nout)
{
  int cap = 0, k = 0;
  i32 i, start = a;
  double used = 0;
  *out = NULL; *nout = 0;
  for (i = a; i <= b; i++) {
    double sz = (rows ? sheet_row_height(s, i) : sheet_col_width(s, i)) * 0.75;
    double room = avail - (start > t2 ? title : 0);
    int manual = 0;
    while (k < nbrk && brk[k] < i) k++;
    if (k < nbrk && brk[k] == i && i > start) manual = 1;
    if (i > start && (manual || used + sz > room + 0.01)) {
      if (!add_band(out, nout, &cap, start, i - 1)) return 0;
      start = i; used = 0;
    }
    used += sz;
    if (*nout > 20000) break;
  }
  return add_band(out, nout, &cap, start, b);
}
int page_layout(Book *b, int sheet, Pages *pg)
{
  Sheet *s = b->sh[sheet];
  const PageSetup *p = page_of(s);
  const Paper *pp = &page_papers[p->paper < page_npapers ? p->paper : 0];
  double w0, h0, tw = 0, th = 0, scale;
  int tries;
  memset(pg, 0, sizeof *pg);
  pg->pw = p->landscape ? pp->h : pp->w;
  pg->ph = p->landscape ? pp->w : pp->h;
  pg->area = p->area.r1 >= 0 ? p->area : used_range(s);
  if (pg->area.r2 >= MAXROW) pg->area.r2 = MAXROW - 1;
  if (pg->area.c2 >= MAXCOL) pg->area.c2 = MAXCOL - 1;
  pg->overthendown = p->overthendown;
  pg->trow1 = p->trow1; pg->trow2 = p->trow2; pg->tcol1 = p->tcol1; pg->tcol2 = p->tcol2;
  if (pg->trow1 >= 0) th = page_rows_pt(s, pg->trow1, pg->trow2);
  if (pg->tcol1 >= 0) tw = page_cols_pt(s, pg->tcol1, pg->tcol2);
  w0 = pg->pw - (p->margin[M_LEFT] + p->margin[M_RIGHT]) * 72;
  h0 = pg->ph - (p->margin[M_TOP] + p->margin[M_BOTTOM]) * 72;
  if (w0 < 36) w0 = 36;
  if (h0 < 36) h0 = 36;
  if (p->headings) { w0 -= 30; h0 -= 14; }
  scale = p->fit ? 1.0 : (p->scale >= 10 && p->scale <= 400 ? p->scale / 100.0 : 1.0);
  if (p->fit) {
    double W = page_cols_pt(s, pg->area.c1, pg->area.c2), H = page_rows_pt(s, pg->area.r1, pg->area.r2);
    if (p->fitw && W > 0 && w0 * p->fitw / (W + tw * (p->fitw - 1)) < scale) scale = w0 * p->fitw / (W + tw * (p->fitw - 1));
    if (p->fith && H > 0 && h0 * p->fith / (H + th * (p->fith - 1)) < scale) scale = h0 * p->fith / (H + th * (p->fith - 1));
    if (scale < 0.1) scale = 0.1;
  }
  for (tries = 0; tries < 80; tries++) {
    pg->availw = w0 / scale; pg->availh = h0 / scale;
    free(pg->rows); free(pg->cols);
    if (!bands(s, 1, pg->area.r1, pg->area.r2, pg->availh, th, pg->trow1 >= 0 ? pg->trow2 : -1, p->fit ? NULL : p->rbrk, p->fit ? 0 : p->nrbrk, &pg->rows, &pg->nrows) ||
        !bands(s, 0, pg->area.c1, pg->area.c2, pg->availw, tw, pg->tcol1 >= 0 ? pg->tcol2 : -1, p->fit ? NULL : p->cbrk, p->fit ? 0 : p->ncbrk, &pg->cols, &pg->ncols)) {
      pages_free(pg);
      return 0;
    }
    /* fit to pages: whole rows and columns may still need one more; a little smaller until they fit */
    if (!p->fit || scale <= 0.1 || ((!p->fitw || pg->ncols <= p->fitw) && (!p->fith || pg->nrows <= p->fith))) break;
    scale *= 0.97;
    if (scale < 0.1) scale = 0.1;
  }
  pg->scale = scale;
  pg->npages = pg->nrows * pg->ncols;
  return 1;
}
void pages_free(Pages *p)
{
  free(p->rows); free(p->cols);
  p->rows = p->cols = NULL; p->nrows = p->ncols = p->npages = 0;
}
void page_bands(const Pages *p, int k, int *rb, int *cb)
{
  *rb = *cb = 0;
  if (!p->nrows || !p->ncols) return;
  if (p->overthendown) { *cb = k % p->ncols; *rb = k / p->ncols; }
  else { *rb = k % p->nrows; *cb = k / p->nrows; }
}

/* ------------------------------------------------------------------ headers and footers */
int hf_which(const PageSetup *p, int footer, int first, int number)
{
  if (p->difffirst && first) return footer ? HF_FIRSTF : HF_FIRSTH;
  if (p->diffoddeven && number % 2 == 0) return footer ? HF_EVENF : HF_EVENH;
  return footer ? HF_FOOTER : HF_HEADER;
}
static const char *const presets[] = {
  "", "&CPage &P", "&CPage &P of &N", "&C&A", "&C&F", "&C&A&RPage &P", "&L&A&CConfidential&RPage &P", "&C&D", "&LPrepared by &\"-,Bold\"Ledger&R&D",
  "&L&F&C&A&RPage &P of &N", "&L&D&R&T", "&C&Z&F",
};
int hf_preset_count(void) { return (int)(sizeof presets / sizeof presets[0]); }
const char *hf_preset(int k) { return k >= 0 && k < hf_preset_count() ? presets[k] : ""; }
static void run_add(HFSection *sec, const HFRun *look, const char *t, size_t n)
{
  HFRun *r;
  size_t l;
  if (!n) return;
  if (sec->n && !memcmp(&sec->run[sec->n - 1].bold, &look->bold, sizeof *look - sizeof look->text)) r = &sec->run[sec->n - 1];
  else {
    if (sec->n >= 16) return;
    r = &sec->run[sec->n++];
    *r = *look;
    r->text[0] = 0;
  }
  l = strlen(r->text);
  if (l + n >= sizeof r->text) n = sizeof r->text - 1 - l;
  memcpy(r->text + l, t, n);
  r->text[l + n] = 0;
}
void hf_expand(const char *code, const HFContext *cx, HFSection out[3])
{
  HFRun look;
  int sec = 1;
  const char *p = code ? code : "";
  memset(out, 0, 3 * sizeof *out);
  memset(&look, 0, sizeof look);
  look.color = COLOR_AUTO;
  while (*p) {
    const char *e = p;
    char t[64];
    while (*e && *e != '&') e++;
    run_add(&out[sec], &look, p, (size_t)(e - p));
    p = e;
    if (!*p) break;
    p++;
    switch (*p) {
    case 0: break;
    case 'L': sec = 0; memset(&look, 0, sizeof look); look.color = COLOR_AUTO; p++; break;
    case 'C': sec = 1; memset(&look, 0, sizeof look); look.color = COLOR_AUTO; p++; break;
    case 'R': sec = 2; memset(&look, 0, sizeof look); look.color = COLOR_AUTO; p++; break;
    case '&': run_add(&out[sec], &look, "&", 1); p++; break;
    case 'P': case 'N': {
      long v = *p == 'P' ? cx->page : cx->pages;
      p++;
      /* &P+n, &P-n */
      if (*p == '+' || *p == '-') { int neg = *p == '-'; long d = strtol(p + 1, (char **)&p, 10); v += neg ? -d : d; }
      sprintf(t, "%ld", v);
      run_add(&out[sec], &look, t, strlen(t));
      break; }
    case 'D': case 'T': {
      int y, m, d;
      double fr = cx->now - (double)(long)cx->now;
      if (*p == 'D') { serial_ymd(cx->now, &y, &m, &d); sprintf(t, "%02d/%02d/%04d", d, m, y); }
      else { long mins = (long)(fr * 1440 + 0.5); sprintf(t, "%02ld:%02ld", mins / 60 % 24, mins % 60); }
      run_add(&out[sec], &look, t, strlen(t));
      p++;
      break; }
    case 'F': if (cx->file) run_add(&out[sec], &look, cx->file, strlen(cx->file)); p++; break;
    case 'Z': if (cx->path) run_add(&out[sec], &look, cx->path, strlen(cx->path)); p++; break;
    case 'A': if (cx->sheet) run_add(&out[sec], &look, cx->sheet, strlen(cx->sheet)); p++; break;
    case 'B': look.bold = !look.bold; p++; break;
    case 'I': look.italic = !look.italic; p++; break;
    case 'U': look.underline = look.underline == 1 ? 0 : 1; p++; break;
    case 'E': look.underline = look.underline == 2 ? 0 : 2; p++; break;
    case 'S': look.strike = !look.strike; p++; break;
    case 'X': case 'Y': case 'G': case 'H': case 'O': p++; break;
    case 'K':
      p++;
      if (strlen(p) >= 6) { char hex[7]; memcpy(hex, p, 6); hex[6] = 0; look.color = (u32)strtoul(hex, NULL, 16); p += 6; }
      break;
    case '"': {
      /* &"font,style": "-" keeps the font; the style may say Bold and/or Italic */
      const char *q = ++p, *comma;
      while (*p && *p != '"') p++;
      comma = memchr(q, ',', (size_t)(p - q));
      if (comma) {
        size_t fl = (size_t)(comma - q);
        if (!(fl == 1 && *q == '-')) { if (fl >= sizeof look.font) fl = sizeof look.font - 1; memcpy(look.font, q, fl); look.font[fl] = 0; }
        look.bold = strstr(comma, "Bold") && strstr(comma, "Bold") < p;
        look.italic = (strstr(comma, "Italic") && strstr(comma, "Italic") < p) || (strstr(comma, "Oblique") && strstr(comma, "Oblique") < p);
      }
      if (*p) p++;
      break; }
    default:
      if (*p >= '0' && *p <= '9') { look.size = (u16)strtol(p, (char **)&p, 10); if (look.size > 400) look.size = 400; }
      else p++;
    }
  }
}
void hf_plain(const HFSection *s, char *out, size_t cap)
{
  int i;
  size_t n = 0;
  out[0] = 0;
  for (i = 0; i < s->n; i++) {
    size_t l = strlen(s->run[i].text);
    if (n + l >= cap) l = cap - 1 - n;
    memcpy(out + n, s->run[i].text, l); n += l; out[n] = 0;
  }
}

/* ------------------------------------------------------------------ the workbook's extras */
const Theme page_themes[] = {
  { "Office", { 0xFFFFFF, 0x000000, 0xE7E6E6, 0x44546A, 0x4472C4, 0xED7D31, 0xA5A5A5, 0xFFC000, 0x5B9BD5, 0x70AD47, 0x0563C1, 0x954F72 }, "Calibri Light", "Calibri" },
  { "Office 2007 - 2010", { 0xFFFFFF, 0x000000, 0xEEECE1, 0x1F497D, 0x4F81BD, 0xC0504D, 0x9BBB59, 0x8064A2, 0x4BACC6, 0xF79646, 0x0000FF, 0x800080 }, "Cambria", "Calibri" },
  { "Grayscale", { 0xFFFFFF, 0x000000, 0xF8F8F8, 0x000000, 0xDDDDDD, 0xB2B2B2, 0x969696, 0x808080, 0x5F5F5F, 0x4D4D4D, 0x5F5F5F, 0x919191 }, "Arial", "Arial" },
  { "Paper", { 0xFFFFFF, 0x000000, 0xFEFAC9, 0x444D26, 0xA5B592, 0xF3A447, 0xE7BC29, 0xD092A7, 0x9C85C0, 0x809EC2, 0x8E58B6, 0x7F6F6F }, "Times New Roman", "Times New Roman" },
  /* GridCraft's own palettes (crates/engine/src/cmd/view.rs, MIT, (c) the GridCraft authors) */
  { "Craft", { 0xFFFFFF, 0x000000, 0xE8E8E8, 0x0E2841, 0x156082, 0xE97132, 0x196B24, 0x0F9ED5, 0xA02B93, 0x4EA72E, 0x467886, 0x96607D }, "", "" },
  { "Slate", { 0xFFFFFF, 0x1F2328, 0xEEF1F4, 0x3B4652, 0x4F6D8A, 0x8AA1B8, 0xC27C4C, 0x6F9A72, 0x9D6C9B, 0xD3B25A, 0x2F6FB0, 0x7A5C8F }, "", "" },
  { "Meadow", { 0xFFFFFF, 0x16241A, 0xEEF4EA, 0x2E4A33, 0x4D8B3F, 0x9BC53D, 0xE4B33A, 0x2E8C83, 0xC4643C, 0x7B6A9E, 0x2B7A57, 0x6E7F3A }, "", "" },
  { "Ember", { 0xFFFFFF, 0x2A1A14, 0xF6EEE8, 0x5A2E1E, 0xD9532B, 0xF2994A, 0xF2C94C, 0x8C3B2E, 0x4A6FA5, 0x6F8F4E, 0xB5472A, 0x8C6A5A }, "", "" },
  { "Ocean", { 0xFFFFFF, 0x0B1E2D, 0xE8F1F7, 0x123A5A, 0x1F78B4, 0x33A6CC, 0x52C7B8, 0x3B5BA5, 0xF2A65A, 0x8E6CB8, 0x1A6FA8, 0x6A7FB0 }, "", "" },
  { "Orchid", { 0xFFFFFF, 0x24152B, 0xF3EDF6, 0x4B2C5E, 0x8E44AD, 0xC56CF0, 0xF78FB3, 0x3DC1D3, 0xF5CD79, 0x63CDDA, 0x7E3FA0, 0xB066B0 }, "", "" },
  { "Graphite", { 0xFFFFFF, 0x111111, 0xEDEDED, 0x333333, 0x595959, 0x7F7F7F, 0xA5A5A5, 0x3C78D8, 0xCC4125, 0x6AA84F, 0x3C78D8, 0x8E7CC3 }, "", "" },
};
const int page_nthemes = (int)(sizeof page_themes / sizeof page_themes[0]);
LayoutBook *book_lx(Book *b)
{
  if (!b->lx && (b->lx = (LayoutBook *)calloc(1, sizeof(LayoutBook)))) {
    b->lx->theme = page_themes[0];
    b->lx->maxiter = 100;
    b->lx->maxchange = 0.001;
  }
  return b->lx;
}
void lx_free(LayoutBook *x) { free(x); }
/* every font of the book named from (and style 0's) changed to to */
static void rename_fonts(Book *b, const char *from, const char *to)
{
  int i;
  if (!to[0]) return;
  for (i = 0; i < b->nfonts; i++)
    if (str_ieq(b->fonts[i].name, from) || (b->nstyles && b->styles[0].font == i)) scopy(b->fonts[i].name, sizeof b->fonts[i].name, to);
}
void book_set_theme_fonts(Book *b, const char *major, const char *minor)
{
  LayoutBook *x = book_lx(b);
  char old[48];
  if (!x) return;
  strcpy(old, x->theme.minor[0] ? x->theme.minor : "Calibri");
  if (major && major[0]) scopy(x->theme.major, sizeof x->theme.major, major);
  if (minor && minor[0]) {
    scopy(x->theme.minor, sizeof x->theme.minor, minor);
    rename_fonts(b, old, x->theme.minor);
  }
  b->dirty = 1; b->changes++;
}
void book_set_theme(Book *b, const Theme *t)
{
  LayoutBook *x = book_lx(b);
  int i, k;
  u32 oldc[12];
  if (!x) return;
  memcpy(oldc, x->theme.color, sizeof oldc);
  scopy(x->theme.name, sizeof x->theme.name, t->name);
  memcpy(x->theme.color, t->color, sizeof t->color);
  /* colours that were the old theme's accents (cells that used theme colours) take the new ones */
  for (i = 0; i < b->nstyles; i++)
    for (k = 4; k < 10; k++) if (b->styles[i].fill == (FILL_ON | oldc[k]) && oldc[k] != t->color[k]) { b->styles[i].fill = FILL_ON | t->color[k]; break; }
  for (i = 0; i < b->nfonts; i++)
    for (k = 2; k < 10; k++) if (k != 2 && b->fonts[i].color == oldc[k] && oldc[k] != t->color[k]) { b->fonts[i].color = t->color[k]; break; }
  if (t->minor[0]) book_set_theme_fonts(b, t->major, t->minor);
  b->dirty = 1; b->changes++;
}

/* ------------------------------------------------------------------ .xlsx */
static int truthy(const char *v) { return v && (!strcmp(v, "1") || !strcmp(v, "true")); }
static double dattr(Xml *x, const char *n, double d) { const char *v = xml_attr(x, n); return v ? strtod(v, NULL) : d; }
int page_xml_sheet(Sheet *s, Xml *x)
{
  const char *n = x->name, *v;
  PageSetup *p;
  if (!strcmp(n, "sheetView")) {
    if ((v = xml_attr(x, "view")) && strcmp(v, "normal") && (p = page_get(s))) p->view = !strcmp(v, "pageLayout") ? PV_LAYOUT : PV_BREAKS;
    return 0;                       /* (xlsx.c reads the rest) */
  }
  if (!strcmp(n, "pageSetUpPr")) { if (truthy(xml_attr(x, "fitToPage")) && (p = page_get(s))) p->fit = 1; return 1; }
  if (strcmp(n, "pageMargins") && strcmp(n, "pageSetup") && strcmp(n, "printOptions") && strcmp(n, "headerFooter") && strcmp(n, "rowBreaks") &&
      strcmp(n, "colBreaks") && strcmp(n, "brk")) return 0;
  if (!(p = page_get(s))) return 1;
  if (!strcmp(n, "pageMargins")) {
    static const char *const mn[6] = { "left", "right", "top", "bottom", "header", "footer" };
    int i;
    for (i = 0; i < 6; i++) { double m = dattr(x, mn[i], p->margin[i]); if (m >= 0 && m < 50) p->margin[i] = m; }
  } else if (!strcmp(n, "pageSetup")) {
    long l;
    if ((v = xml_attr(x, "paperSize"))) p->paper = (u8)page_paper_by_code(atoi(v));
    if ((v = xml_attr(x, "orientation"))) p->landscape = !strcmp(v, "landscape");
    if ((v = xml_attr(x, "scale"))) { l = atol(v); if (l >= 10 && l <= 400) p->scale = (u16)l; }
    if ((v = xml_attr(x, "fitToWidth"))) { l = atol(v); if (l >= 0 && l < 32768) p->fitw = (u16)l; }
    if ((v = xml_attr(x, "fitToHeight"))) { l = atol(v); if (l >= 0 && l < 32768) p->fith = (u16)l; }
    if ((v = xml_attr(x, "pageOrder"))) p->overthendown = !strcmp(v, "overThenDown");
    p->blackwhite = (u8)truthy(xml_attr(x, "blackAndWhite"));
    p->draft = (u8)truthy(xml_attr(x, "draft"));
    if (truthy(xml_attr(x, "useFirstPageNumber")) && (v = xml_attr(x, "firstPageNumber"))) { l = atol(v); if (l > 0 && l < 32768) p->firstpage = (u16)l; }
  } else if (!strcmp(n, "printOptions")) {
    p->gridlines = (u8)truthy(xml_attr(x, "gridLines"));
    p->headings = (u8)truthy(xml_attr(x, "headings"));
    p->centerh = (u8)truthy(xml_attr(x, "horizontalCentered"));
    p->centerv = (u8)truthy(xml_attr(x, "verticalCentered"));
  } else if (!strcmp(n, "headerFooter")) {
    static const char *const hn[HF_N] = { "oddHeader", "oddFooter", "firstHeader", "firstFooter", "evenHeader", "evenFooter" };
    int depth = x->depth;
    p->difffirst = (u8)truthy(xml_attr(x, "differentFirst"));
    p->diffoddeven = (u8)truthy(xml_attr(x, "differentOddEven"));
    if ((v = xml_attr(x, "scaleWithDoc"))) p->scalehf = (u8)truthy(v);
    if ((v = xml_attr(x, "alignWithMargins"))) p->alignhf = (u8)truthy(v);
    if (x->pending_end) return 1;
    /* its children, to the end of the element */
    while (xml_next(x) != XT_EOF && !(x->type == XT_END && x->depth < depth)) {
      int k;
      if (x->type != XT_START) continue;
      for (k = 0; k < HF_N && strcmp(x->name, hn[k]); k++) ;
      if (k < HF_N) {
        Buf t;
        buf_init(&t);
        while (xml_next(x) == XT_TEXT) buf_add(&t, x->text.data, x->text.len);
        page_set_hf(p, k, t.data ? t.data : "");
        buf_free(&t);
        if (x->type == XT_END && x->depth < depth) break;
      }
    }
  } else if (!strcmp(n, "rowBreaks")) p->rdbrk = 1;
  else if (!strcmp(n, "colBreaks")) p->rdbrk = 2;
  else if (!strcmp(n, "brk") && p->rdbrk && (v = xml_attr(x, "id"))) {
    long id = atol(v);
    const char *man = xml_attr(x, "man");
    if (id > 0 && (!man || truthy(man))) page_set_break(s, p->rdbrk == 1, (i32)id, 1);
  }
  return 1;
}
/* a sheet's name as a formula writes it: quoted if it needs to be */
static void sheet_ref(Buf *o, const char *name)
{
  const char *c;
  int q = !name[0] || (name[0] >= '0' && name[0] <= '9');
  for (c = name; *c; c++) if (!((*c >= 'A' && *c <= 'Z') || (*c >= 'a' && *c <= 'z') || (*c >= '0' && *c <= '9') || *c == '_' || *c == '.' || (u8)*c >= 0x80)) q = 1;
  if (q) buf_addc(o, '\'');
  for (c = name; *c; c++) { if (*c == '\'') buf_addc(o, '\''); buf_addc(o, *c); }
  if (q) buf_addc(o, '\'');
  buf_addc(o, '!');
}
int page_xml_name(Book *b, const char *name, int local, const char *text)
{
  int area = !strcmp(name, "_xlnm.Print_Area"), titles = !strcmp(name, "_xlnm.Print_Titles");
  const char *t = text;
  PageSetup *p;
  if (!area && !titles) return 0;
  if (local < 0 || local >= b->nsh || !t || !(p = page_get(b->sh[local]))) return 1;
  if (*t == '=') t++;
  while (*t) {
    u16 sh; Rect r; u8 fl;
    int n = parse_ref(b, t, &sh, &r, &fl);
    if (!n) break;
    if (area && p->area.r1 < 0 && !(fl & (RF_ROWS | RF_COLS))) p->area = r;
    else if (area && p->area.r1 < 0) {
      p->area = r;
      if (fl & RF_COLS) { p->area.r1 = 0; p->area.r2 = MAXROW - 1; }
      if (fl & RF_ROWS) { p->area.c1 = 0; p->area.c2 = MAXCOL - 1; }
    } else if (titles && (fl & RF_ROWS)) { p->trow1 = r.r1; p->trow2 = r.r2; }
    else if (titles && (fl & RF_COLS)) { p->tcol1 = r.c1; p->tcol2 = r.c2; }
    t += n;
    if (*t != ',') break;
    t++;
  }
  return 1;
}
int page_needs_sheetpr(Sheet *s) { return s->page && s->page->fit; }
void page_write_sheetpr(Sheet *s, Buf *o) { if (page_needs_sheetpr(s)) buf_adds(o, "<pageSetUpPr fitToPage=\"1\"/>"); }
void page_write_view(Sheet *s, Buf *o)
{
  if (s->page && s->page->view == PV_BREAKS) buf_adds(o, " view=\"pageBreakPreview\"");
  else if (s->page && s->page->view == PV_LAYOUT) buf_adds(o, " view=\"pageLayout\"");
}
static void num(Buf *o, double v)
{
  char t[32];
  int n;
  sprintf(t, "%.4f", v);
  n = (int)strlen(t);
  while (n > 1 && t[n - 1] == '0') t[--n] = 0;
  if (n && t[n - 1] == '.') t[--n] = 0;
  buf_adds(o, t);
}
void page_write_sheet(Sheet *s, Buf *o)
{
  const PageSetup *p = page_of(s);
  static const char *const mn[6] = { "left", "right", "top", "bottom", "header", "footer" };
  static const char *const hn[HF_N] = { "oddHeader", "oddFooter", "firstHeader", "firstFooter", "evenHeader", "evenFooter" };
  static const int order[HF_N] = { HF_HEADER, HF_FOOTER, HF_EVENH, HF_EVENF, HF_FIRSTH, HF_FIRSTF };
  int i, any;
  if (p->gridlines || p->headings || p->centerh || p->centerv)
    buf_printf(o, "<printOptions%s%s%s%s/>", p->centerh ? " horizontalCentered=\"1\"" : "", p->centerv ? " verticalCentered=\"1\"" : "",
               p->headings ? " headings=\"1\"" : "", p->gridlines ? " gridLines=\"1\"" : "");
  buf_adds(o, "<pageMargins");
  for (i = 0; i < 6; i++) { buf_printf(o, " %s=\"", mn[i]); num(o, p->margin[i]); buf_adds(o, "\""); }
  buf_adds(o, "/>");
  if (s->page) {
    buf_printf(o, "<pageSetup paperSize=\"%d\"", page_papers[p->paper < page_npapers ? p->paper : 0].code);
    if (p->scale != 100) buf_printf(o, " scale=\"%d\"", p->scale);
    if (p->firstpage) buf_printf(o, " firstPageNumber=\"%d\"", p->firstpage);
    if (p->fit) buf_printf(o, " fitToWidth=\"%d\" fitToHeight=\"%d\"", p->fitw, p->fith);
    if (p->overthendown) buf_adds(o, " pageOrder=\"overThenDown\"");
    buf_printf(o, " orientation=\"%s\"", p->landscape ? "landscape" : "portrait");
    if (p->blackwhite) buf_adds(o, " blackAndWhite=\"1\"");
    if (p->draft) buf_adds(o, " draft=\"1\"");
    if (p->firstpage) buf_adds(o, " useFirstPageNumber=\"1\"");
    buf_adds(o, "/>");
  }
  for (i = any = 0; i < HF_N; i++) if (p->hf[i]) any = 1;
  if (any || p->difffirst || p->diffoddeven || !p->scalehf || !p->alignhf) {
    buf_printf(o, "<headerFooter%s%s%s%s>", p->diffoddeven ? " differentOddEven=\"1\"" : "", p->difffirst ? " differentFirst=\"1\"" : "",
               p->scalehf ? "" : " scaleWithDoc=\"0\"", p->alignhf ? "" : " alignWithMargins=\"0\"");
    for (i = 0; i < HF_N; i++) {
      int k = order[i];
      if (!p->hf[k]) continue;
      buf_printf(o, "<%s>", hn[k]); xml_escape(o, p->hf[k], strlen(p->hf[k])); buf_printf(o, "</%s>", hn[k]);
    }
    buf_adds(o, "</headerFooter>");
  }
  if (p->nrbrk) {
    buf_printf(o, "<rowBreaks count=\"%d\" manualBreakCount=\"%d\">", p->nrbrk, p->nrbrk);
    for (i = 0; i < p->nrbrk; i++) buf_printf(o, "<brk id=\"%ld\" max=\"16383\" man=\"1\"/>", (long)p->rbrk[i]);
    buf_adds(o, "</rowBreaks>");
  }
  if (p->ncbrk) {
    buf_printf(o, "<colBreaks count=\"%d\" manualBreakCount=\"%d\">", p->ncbrk, p->ncbrk);
    for (i = 0; i < p->ncbrk; i++) buf_printf(o, "<brk id=\"%ld\" max=\"1048575\" man=\"1\"/>", (long)p->cbrk[i]);
    buf_adds(o, "</colBreaks>");
  }
}
int page_has_names(Book *b)
{
  int i;
  for (i = 0; i < b->nsh; i++) if (b->sh[i]->page && (b->sh[i]->page->area.r1 >= 0 || b->sh[i]->page->trow1 >= 0 || b->sh[i]->page->tcol1 >= 0)) return 1;
  return 0;
}
static void abs_rect(Buf *o, const char *sheet, Rect r)
{
  char a[16], c[16];
  sheet_ref(o, sheet);
  col_name(r.c1, a); col_name(r.c2, c);
  if (r.r1 == 0 && r.r2 >= MAXROW - 1) buf_printf(o, "$%s:$%s", a, c);
  else if (r.c1 == 0 && r.c2 >= MAXCOL - 1) buf_printf(o, "$%ld:$%ld", (long)r.r1 + 1, (long)r.r2 + 1);
  else if (r.r1 == r.r2 && r.c1 == r.c2) buf_printf(o, "$%s$%ld", a, (long)r.r1 + 1);
  else buf_printf(o, "$%s$%ld:$%s$%ld", a, (long)r.r1 + 1, c, (long)r.r2 + 1);
}
/* (Excel wants definedNames sorted by name; the _xlnm ones sort first and are written first) */
void page_write_names(Book *b, Buf *o)
{
  int i;
  Buf t;
  buf_init(&t);
  for (i = 0; i < b->nsh; i++) {
    const PageSetup *p = b->sh[i]->page;
    if (!p || p->area.r1 < 0) continue;
    t.len = 0;
    abs_rect(&t, b->sh[i]->name, p->area);
    buf_printf(o, "<definedName name=\"_xlnm.Print_Area\" localSheetId=\"%d\">", i);
    xml_escape(o, t.data, t.len);
    buf_adds(o, "</definedName>");
  }
  for (i = 0; i < b->nsh; i++) {
    const PageSetup *p = b->sh[i]->page;
    Rect r;
    if (!p || (p->trow1 < 0 && p->tcol1 < 0)) continue;
    t.len = 0;
    if (p->tcol1 >= 0) { r.r1 = 0; r.r2 = MAXROW - 1; r.c1 = p->tcol1; r.c2 = p->tcol2; abs_rect(&t, b->sh[i]->name, r); }
    if (p->trow1 >= 0) { if (t.len) buf_addc(&t, ','); r.c1 = 0; r.c2 = MAXCOL - 1; r.r1 = p->trow1; r.r2 = p->trow2; abs_rect(&t, b->sh[i]->name, r); }
    buf_printf(o, "<definedName name=\"_xlnm.Print_Titles\" localSheetId=\"%d\">", i);
    xml_escape(o, t.data, t.len);
    buf_adds(o, "</definedName>");
  }
  buf_free(&t);
}
static void elem(Buf *o, const char *tag, const char *v)
{
  if (!v[0]) return;
  buf_printf(o, "<%s>", tag); xml_escape(o, v, strlen(v)); buf_printf(o, "</%s>", tag);
}
void page_write_core(Book *b, Buf *o)
{
  LayoutBook *x = b->lx;
  if (!x) return;
  elem(o, "dc:subject", x->subject);
  elem(o, "cp:keywords", x->keywords);
  elem(o, "dc:description", x->description);
  elem(o, "cp:category", x->category);
  elem(o, "cp:contentStatus", x->status);
}
int page_xml_core(Book *b, Xml *x)
{
  static const char *const tags[] = { "subject", "keywords", "description", "category", "contentStatus", "Company", "Manager" };
  LayoutBook *l;
  char *dst[7];
  size_t cap[7];
  int k;
  Buf t;
  for (k = 0; k < 7 && strcmp(x->name, tags[k]); k++) ;
  if (k == 7 || !(l = book_lx(b))) return 0;
  dst[0] = l->subject; cap[0] = sizeof l->subject; dst[1] = l->keywords; cap[1] = sizeof l->keywords;
  dst[2] = l->description; cap[2] = sizeof l->description; dst[3] = l->category; cap[3] = sizeof l->category;
  dst[4] = l->status; cap[4] = sizeof l->status; dst[5] = l->company; cap[5] = sizeof l->company; dst[6] = l->manager; cap[6] = sizeof l->manager;
  buf_init(&t);
  while (xml_next(x) == XT_TEXT) buf_add(&t, x->text.data, x->text.len);
  if (t.data) { strncpy(dst[k], t.data, cap[k] - 1); dst[k][cap[k] - 1] = 0; }
  buf_free(&t);
  return 1;
}
void page_write_calcpr(Book *b, Buf *o)
{
  if (b->manualcalc) buf_adds(o, " calcMode=\"manual\"");
  if (b->lx && b->lx->iterate) {
    buf_printf(o, " iterate=\"1\" iterateCount=\"%d\" iterateDelta=\"", b->lx->maxiter);
    num(o, b->lx->maxchange);
    buf_adds(o, "\"");
  }
  if (b->lx && b->lx->r1c1) buf_adds(o, " refMode=\"R1C1\"");
}
void page_xml_calcpr(Book *b, Xml *x)
{
  const char *v;
  LayoutBook *l;
  if (!truthy(xml_attr(x, "iterate")) && !((v = xml_attr(x, "refMode")) && !strcmp(v, "R1C1"))) return;
  if (!(l = book_lx(b))) return;
  l->iterate = (u8)truthy(xml_attr(x, "iterate"));
  if ((v = xml_attr(x, "iterateCount"))) { long n = atol(v); if (n >= 1 && n <= 32767) l->maxiter = (u16)n; }
  if ((v = xml_attr(x, "iterateDelta"))) { double d = strtod(v, NULL); if (d > 0 && d < 1e6) l->maxchange = d; }
  l->r1c1 = (v = xml_attr(x, "refMode")) && !strcmp(v, "R1C1");
}
void page_read_theme(Book *b, const char *xml, size_t len)
{
  static const char *const names[12] = { "lt1", "dk1", "lt2", "dk2", "accent1", "accent2", "accent3", "accent4", "accent5", "accent6", "hlink", "folHlink" };
  Xml x;
  LayoutBook *l = book_lx(b);
  int slot = -1, font = 0;
  if (!l) return;
  xml_init(&x, xml, len);
  while (xml_next(&x) != XT_EOF) {
    int k;
    const char *v;
    if (x.type != XT_START) continue;
    if (!strcmp(x.name, "clrScheme") && (v = xml_attr(&x, "name"))) { strncpy(l->theme.name, v, sizeof l->theme.name - 1); continue; }
    for (k = 0; k < 12; k++) if (!strcmp(x.name, names[k])) { slot = k; break; }
    if (k < 12) continue;
    if (slot >= 0 && !strcmp(x.name, "srgbClr")) { if ((v = xml_attr(&x, "val"))) l->theme.color[slot] = (u32)strtoul(v, NULL, 16); slot = -1; }
    else if (slot >= 0 && !strcmp(x.name, "sysClr")) { if ((v = xml_attr(&x, "lastClr"))) l->theme.color[slot] = (u32)strtoul(v, NULL, 16); slot = -1; }
    else if (!strcmp(x.name, "majorFont")) font = 1;
    else if (!strcmp(x.name, "minorFont")) font = 2;
    else if (font && !strcmp(x.name, "latin") && (v = xml_attr(&x, "typeface"))) {
      char *d = font == 1 ? l->theme.major : l->theme.minor;
      strncpy(d, v, 47); d[47] = 0;
      font = 0;
    }
  }
  xml_free(&x);
}
void page_write_theme(Book *b, Buf *o)
{
  static const char *const names[12] = { "lt1", "dk1", "lt2", "dk2", "accent1", "accent2", "accent3", "accent4", "accent5", "accent6", "hlink", "folHlink" };
  const Theme *t = b->lx ? &b->lx->theme : &page_themes[0];
  int i;
  buf_adds(o, "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n<a:theme xmlns:a=\"http://schemas.openxmlformats.org/drawingml/2006/main\" name=\"");
  xml_escape(o, t->name, strlen(t->name));
  buf_adds(o, "\"><a:themeElements><a:clrScheme name=\"");
  xml_escape(o, t->name, strlen(t->name));
  buf_adds(o, "\">");
  buf_printf(o, "<a:dk1><a:srgbClr val=\"%06lX\"/></a:dk1>", (unsigned long)t->color[1]);
  buf_printf(o, "<a:lt1><a:srgbClr val=\"%06lX\"/></a:lt1>", (unsigned long)t->color[0]);
  buf_printf(o, "<a:dk2><a:srgbClr val=\"%06lX\"/></a:dk2>", (unsigned long)t->color[3]);
  buf_printf(o, "<a:lt2><a:srgbClr val=\"%06lX\"/></a:lt2>", (unsigned long)t->color[2]);
  for (i = 4; i < 12; i++) buf_printf(o, "<a:%s><a:srgbClr val=\"%06lX\"/></a:%s>", names[i], (unsigned long)t->color[i], names[i]);
  buf_adds(o, "</a:clrScheme><a:fontScheme name=\"");
  xml_escape(o, t->name, strlen(t->name));
  buf_adds(o, "\"><a:majorFont><a:latin typeface=\"");
  xml_escape(o, t->major[0] ? t->major : "Calibri Light", strlen(t->major[0] ? t->major : "Calibri Light"));
  buf_adds(o, "\"/><a:ea typeface=\"\"/><a:cs typeface=\"\"/></a:majorFont><a:minorFont><a:latin typeface=\"");
  xml_escape(o, t->minor[0] ? t->minor : "Calibri", strlen(t->minor[0] ? t->minor : "Calibri"));
  buf_adds(o, "\"/><a:ea typeface=\"\"/><a:cs typeface=\"\"/></a:minorFont></a:fontScheme>"
              "<a:fmtScheme name=\"Office\"><a:fillStyleLst><a:solidFill><a:schemeClr val=\"phClr\"/></a:solidFill><a:solidFill><a:schemeClr val=\"phClr\"/></a:solidFill>"
              "<a:solidFill><a:schemeClr val=\"phClr\"/></a:solidFill></a:fillStyleLst><a:lnStyleLst><a:ln w=\"6350\"><a:solidFill><a:schemeClr val=\"phClr\"/></a:solidFill></a:ln>"
              "<a:ln w=\"12700\"><a:solidFill><a:schemeClr val=\"phClr\"/></a:solidFill></a:ln><a:ln w=\"19050\"><a:solidFill><a:schemeClr val=\"phClr\"/></a:solidFill></a:ln></a:lnStyleLst>"
              "<a:effectStyleLst><a:effectStyle><a:effectLst/></a:effectStyle><a:effectStyle><a:effectLst/></a:effectStyle><a:effectStyle><a:effectLst/></a:effectStyle></a:effectStyleLst>"
              "<a:bgFillStyleLst><a:solidFill><a:schemeClr val=\"phClr\"/></a:solidFill><a:solidFill><a:schemeClr val=\"phClr\"/></a:solidFill><a:solidFill><a:schemeClr val=\"phClr\"/></a:solidFill></a:bgFillStyleLst>"
              "</a:fmtScheme></a:themeElements></a:theme>");
}
