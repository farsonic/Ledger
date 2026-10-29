/*
 * xlsx.c - .xlsx files (a zip of XML parts) and CSV. Everything in a file is hostile input: every
 * index is checked, sizes are capped, and a damaged part ends up as an error, never a crash.
 *
 * Column widths are stored in pixels here and in characters of the default font's widest digit in
 * the file (7 pixels for an 11 pt Calibri): pixels = width * 7, as Excel shows them. Row heights
 * are points in the file and pixels here (4/3 of a point).
 */
#include "xlsx.h"
#include "table.h"
#include "tablexml.h"
#include "calc.h"
#include "cond.h"
#include "zip.h"
#include "xml.h"
#include "pagesetup.h"
#include "comments.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>
#include <ctype.h>

#define MAXSTRINGS 2000000
#define MAXPART (64L * 1024 * 1024)

/* ------------------------------------------------------------------ colours */
/* the classic 64-colour palette ("indexed" colours in files) */
static const u32 indexed[64] = {
  0x000000, 0xFFFFFF, 0xFF0000, 0x00FF00, 0x0000FF, 0xFFFF00, 0xFF00FF, 0x00FFFF, 0x000000, 0xFFFFFF, 0xFF0000, 0x00FF00,
  0x0000FF, 0xFFFF00, 0xFF00FF, 0x00FFFF, 0x800000, 0x008000, 0x000080, 0x808000, 0x800080, 0x008080, 0xC0C0C0, 0x808080,
  0x9999FF, 0x993366, 0xFFFFCC, 0xCCFFFF, 0x660066, 0xFF8080, 0x0066CC, 0xCCCCFF, 0x000080, 0xFF00FF, 0xFFFF00, 0x00FFFF,
  0x800080, 0x800000, 0x008080, 0x0000FF, 0x00CCFF, 0xCCFFFF, 0xCCFFCC, 0xFFFF99, 0x99CCFF, 0xFF99CC, 0xCC99FF, 0xFFCC99,
  0x3366FF, 0x33CCCC, 0x99CC00, 0xFFCC00, 0xFF9900, 0xFF6600, 0x666699, 0x969696, 0x003366, 0x339966, 0x003300, 0x333300,
  0x993300, 0x993366, 0x333399, 0x333333 };
/* theme colour slots (light 1, dark 1, light 2, dark 2, accents 1-6): a plain neutral set used when
   a file names theme colours; the file's own theme part is read when present */
static u32 theme_colors[12] = { 0xFFFFFF, 0x000000, 0xE7E6E6, 0x44546A, 0x4472C4, 0xED7D31, 0xA5A5A5, 0xFFC000, 0x5B9BD5, 0x70AD47, 0x0563C1, 0x954F72 };
static u32 apply_tint(u32 rgb, double tint)
{
  int ch[3], i;
  ch[0] = (int)(rgb >> 16) & 255; ch[1] = (int)(rgb >> 8) & 255; ch[2] = (int)rgb & 255;
  for (i = 0; i < 3; i++) {
    double v = ch[i];
    v = tint < 0 ? v * (1 + tint) : v + (255 - v) * tint;
    ch[i] = v < 0 ? 0 : v > 255 ? 255 : (int)(v + 0.5);
  }
  return (u32)(ch[0] << 16 | ch[1] << 8 | ch[2]);
}
/* a <color rgb="FF112233"/> / theme / indexed element into 0xRRGGBB; dflt if absent or automatic */
static u32 read_color(Xml *x, u32 dflt)
{
  const char *v;
  u32 c = dflt;
  if ((v = xml_attr(x, "rgb")) && strlen(v) >= 6) c = (u32)strtoul(v + strlen(v) - 6, NULL, 16);
  else if ((v = xml_attr(x, "theme"))) {
    int t = atoi(v);
    /* files number the first four slots dark/light swapped relative to the theme part */
    static const int swap[4] = { 1, 0, 3, 2 };
    if (t >= 0 && t < 12) c = theme_colors[t < 4 ? swap[t] : t];
    if (t >= 0 && t < 4) c = theme_colors[t];
  } else if ((v = xml_attr(x, "indexed"))) {
    int i = atoi(v);
    if (i >= 0 && i < 64) c = indexed[i];
    else if (i == 64) c = dflt;           /* system foreground */
  }
  if ((v = xml_attr(x, "tint")) && c != dflt) c = apply_tint(c, strtod(v, NULL));
  if ((v = xml_attr(x, "auto")) && atoi(v)) c = dflt;
  return c;
}

/* ------------------------------------------------------------------ reading */
typedef struct {
  Book *b;
  char **strings; long nstrings;
  /* styles.xml */
  int *xf_style; int nxf;           /* cellXfs index -> book style */
  Font *fonts; int nfonts;
  u32 *fills; int nfills;
  struct { u8 st[4]; u32 col[4]; } *borders; int nborders;
  int *fmtmap; int *fmtids; int nfmtmap;   /* custom numFmtId -> book format id */
  struct Dxf { u32 fill, color; u8 bold, italic, underline, strike; } *dxfs; int ndxfs;   /* conditional formats' looks */
} Rd;

static u8 *part(ZipReader *z, const char *name, size_t *len)
{
  int i = zip_find(z, name);
  if (i < 0) return NULL;
  if (z->e[i].usize > MAXPART) return NULL;
  return zip_read(z, i, len);
}
/* text of the current element up to its END, joined (for <t> inside <si> and rich runs) */
static void collect_text(Xml *x, Buf *out, int want_t)
{
  int depth = x->depth, in_t = 0, tdepth = 0;
  while (xml_next(x) != XT_EOF) {
    if (x->type == XT_END && x->depth < depth) break;
    if (x->type == XT_START && !strcmp(x->name, "t")) { in_t = 1; tdepth = x->depth; }
    else if (x->type == XT_START && (!strcmp(x->name, "rPh") || !strcmp(x->name, "phoneticPr"))) xml_skip(x);   /* phonetic guides */
    else if (x->type == XT_END && in_t && x->depth < tdepth) in_t = 0;
    else if (x->type == XT_TEXT && (in_t || !want_t)) buf_add(out, x->text.data, x->text.len);
  }
}
static void read_shared_strings(Rd *rd, ZipReader *z)
{
  size_t len;
  u8 *d = part(z, "xl/sharedStrings.xml", &len);
  Xml x;
  long cap = 0;
  if (!d) return;
  xml_init(&x, (const char *)d, len);
  while (xml_next(&x) != XT_EOF) {
    if (x.type == XT_START && !strcmp(x.name, "si")) {
      Buf t;
      buf_init(&t);
      collect_text(&x, &t, 1);
      if (rd->nstrings >= MAXSTRINGS) { buf_free(&t); break; }
      if (rd->nstrings == cap) {
        long nc = cap ? cap * 2 : 256;
        char **ns = (char **)realloc(rd->strings, (size_t)nc * sizeof(char *));
        if (!ns) { buf_free(&t); break; }
        rd->strings = ns; cap = nc;
      }
      rd->strings[rd->nstrings++] = t.data ? buf_take(&t) : str_dup("");
    }
  }
  xml_free(&x);
  free(d);
}
static void read_theme(ZipReader *z)
{
  size_t len;
  u8 *d = part(z, "xl/theme/theme1.xml", &len);
  Xml x;
  int slot = -1;
  static const char *const names[12] = { "lt1", "dk1", "lt2", "dk2", "accent1", "accent2", "accent3", "accent4", "accent5", "accent6", "hlink", "folHlink" };
  if (!d) return;
  xml_init(&x, (const char *)d, len);
  while (xml_next(&x) != XT_EOF) {
    if (x.type != XT_START) continue;
    {
      int k;
      for (k = 0; k < 12; k++) if (!strcmp(x.name, names[k])) { slot = k; break; }
      if (k < 12) continue;
    }
    if (slot >= 0 && !strcmp(x.name, "srgbClr")) { const char *v = xml_attr(&x, "val"); if (v) theme_colors[slot] = (u32)strtoul(v, NULL, 16); slot = -1; }
    else if (slot >= 0 && !strcmp(x.name, "sysClr")) { const char *v = xml_attr(&x, "lastClr"); if (v) theme_colors[slot] = (u32)strtoul(v, NULL, 16); slot = -1; }
    else if (!strcmp(x.name, "fontScheme")) break;
  }
  xml_free(&x);
  free(d);
}
static int border_style(const char *s)
{
  if (!s) return BS_NONE;
  if (!strcmp(s, "thin")) return BS_THIN;
  if (!strcmp(s, "medium") || !strcmp(s, "mediumDashed") || !strcmp(s, "mediumDashDot") || !strcmp(s, "mediumDashDotDot") || !strcmp(s, "slantDashDot")) return BS_MEDIUM;
  if (!strcmp(s, "thick")) return BS_THICK;
  if (!strcmp(s, "double")) return BS_DOUBLE;
  if (!strcmp(s, "dotted")) return BS_DOTTED;
  if (!strcmp(s, "dashed") || !strcmp(s, "dashDot") || !strcmp(s, "dashDotDot")) return BS_DASHED;
  if (!strcmp(s, "hair")) return BS_HAIR;
  return BS_NONE;
}
static void read_styles(Rd *rd, ZipReader *z)
{
  size_t len;
  u8 *d = part(z, "xl/styles.xml", &len);
  Xml x;
  int section = 0;                  /* 1 fonts, 2 fills, 3 borders, 4 cellXfs, 5 cellStyleXfs, 6 numFmts, 7 dxfs */
  Font *cf = NULL;
  int side = -1;
  Style *cs = NULL;
  if (!d) return;
  xml_init(&x, (const char *)d, len);
  while (xml_next(&x) != XT_EOF) {
    const char *n = x.name;
    if (x.type == XT_END) {
      if (!strcmp(n, "fonts") || !strcmp(n, "fills") || !strcmp(n, "borders") || !strcmp(n, "cellXfs") || !strcmp(n, "cellStyleXfs") ||
          !strcmp(n, "numFmts") || !strcmp(n, "dxfs")) section = 0;
      if (!strcmp(n, "xf") && cs && section == 4) {
        int si = book_style(rd->b, cs);
        rd->xf_style[rd->nxf - 1] = si < 0 ? 0 : si;
        free(cs); cs = NULL;
      }
      continue;
    }
    if (x.type != XT_START) continue;
    if (!strcmp(n, "fonts")) { section = 1; continue; }
    if (!strcmp(n, "fills")) { section = 2; continue; }
    if (!strcmp(n, "borders")) { section = 3; continue; }
    if (!strcmp(n, "cellXfs")) { section = 4; continue; }
    if (!strcmp(n, "cellStyleXfs")) { section = 5; continue; }
    if (!strcmp(n, "numFmts")) { section = 6; continue; }
    if (!strcmp(n, "dxfs")) { section = 7; continue; }
    if (section == 7) {
      /* a conditional format's look: its font's colour and styles, and its fill (bgColor for a solid one) */
      struct Dxf *dx;
      if (!strcmp(n, "dxf")) {
        struct Dxf *nd = (struct Dxf *)realloc(rd->dxfs, (size_t)(rd->ndxfs + 1) * sizeof *nd);
        if (!nd || rd->ndxfs > 60000) continue;
        rd->dxfs = nd;
        memset(&nd[rd->ndxfs], 0, sizeof *nd);
        nd[rd->ndxfs++].color = COLOR_AUTO;
        side = 0;
        continue;
      }
      if (!rd->ndxfs) continue;
      dx = &rd->dxfs[rd->ndxfs - 1];
      if (!strcmp(n, "font")) side = 1;
      else if (!strcmp(n, "fill")) side = 2;
      else if (!strcmp(n, "border")) side = 3;
      else if (side == 1 && !strcmp(n, "b")) { const char *v = xml_attr(&x, "val"); dx->bold = !v || (strcmp(v, "0") && strcmp(v, "false")); }
      else if (side == 1 && !strcmp(n, "i")) { const char *v = xml_attr(&x, "val"); dx->italic = !v || (strcmp(v, "0") && strcmp(v, "false")); }
      else if (side == 1 && !strcmp(n, "strike")) { const char *v = xml_attr(&x, "val"); dx->strike = !v || (strcmp(v, "0") && strcmp(v, "false")); }
      else if (side == 1 && !strcmp(n, "u")) { const char *v = xml_attr(&x, "val"); dx->underline = !v || strcmp(v, "none"); }
      else if (side == 1 && !strcmp(n, "color")) dx->color = read_color(&x, COLOR_AUTO);
      else if (side == 2 && (!strcmp(n, "bgColor") || (!strcmp(n, "fgColor") && !dx->fill))) dx->fill = FILL_ON | read_color(&x, 0xFFFFFF);
      continue;
    }
    if (section == 5) continue;
    if (section == 6 && !strcmp(n, "numFmt")) {
      const char *id = xml_attr(&x, "numFmtId"), *code = xml_attr(&x, "formatCode");
      if (id && code && rd->nfmtmap < 4000) {
        int *nm = (int *)realloc(rd->fmtmap, (size_t)(rd->nfmtmap + 1) * sizeof(int));
        int *ni;
        if (!nm) continue;
        rd->fmtmap = nm;
        if (!(ni = (int *)realloc(rd->fmtids, (size_t)(rd->nfmtmap + 1) * sizeof(int)))) continue;
        rd->fmtids = ni;
        rd->fmtids[rd->nfmtmap] = atoi(id);
        rd->fmtmap[rd->nfmtmap] = book_fmt(rd->b, code);
        rd->nfmtmap++;
      }
      continue;
    }
    if (section == 1) {
      if (!strcmp(n, "font")) {
        Font *nf = (Font *)realloc(rd->fonts, (size_t)(rd->nfonts + 1) * sizeof(Font));
        if (!nf || rd->nfonts > 60000) { cf = NULL; continue; }
        rd->fonts = nf;
        cf = &rd->fonts[rd->nfonts++];
        memset(cf, 0, sizeof *cf);
        strcpy(cf->name, "Calibri"); cf->size = 110; cf->color = COLOR_AUTO;
        continue;
      }
      if (!cf) continue;
      if (!strcmp(n, "b")) { const char *v = xml_attr(&x, "val"); cf->bold = !v || (strcmp(v, "0") && strcmp(v, "false")); }
      else if (!strcmp(n, "i")) { const char *v = xml_attr(&x, "val"); cf->italic = !v || (strcmp(v, "0") && strcmp(v, "false")); }
      else if (!strcmp(n, "strike")) { const char *v = xml_attr(&x, "val"); cf->strike = !v || (strcmp(v, "0") && strcmp(v, "false")); }
      else if (!strcmp(n, "u")) { const char *v = xml_attr(&x, "val"); cf->underline = !v || !strcmp(v, "single") || !strcmp(v, "singleAccounting") ? 1 : !strcmp(v, "none") ? 0 : 2; }
      else if (!strcmp(n, "sz")) { const char *v = xml_attr(&x, "val"); if (v) { double s = strtod(v, NULL); if (s > 0 && s < 410) cf->size = (u16)(s * 10 + 0.5); } }
      else if (!strcmp(n, "vertAlign")) { const char *v = xml_attr(&x, "val"); cf->vert = (u8)(v && !strcmp(v, "superscript") ? 1 : v && !strcmp(v, "subscript") ? 2 : 0); }
      else if (!strcmp(n, "name")) { const char *v = xml_attr(&x, "val"); if (v) { strncpy(cf->name, v, sizeof cf->name - 1); cf->name[sizeof cf->name - 1] = 0; } }
      else if (!strcmp(n, "color")) cf->color = read_color(&x, COLOR_AUTO);
      continue;
    }
    if (section == 2) {
      if (!strcmp(n, "fill")) {
        u32 *nf = (u32 *)realloc(rd->fills, (size_t)(rd->nfills + 1) * sizeof(u32));
        if (!nf || rd->nfills > 60000) continue;
        rd->fills = nf;
        rd->fills[rd->nfills++] = 0;
      } else if (!strcmp(n, "patternFill") && rd->nfills) {
        const char *pt = xml_attr(&x, "patternType");
        if (!pt || !strcmp(pt, "none") || !strcmp(pt, "gray125")) rd->fills[rd->nfills - 1] = 0;
        else rd->fills[rd->nfills - 1] = FILL_ON | 0xFFFFFF;     /* the colour follows */
      } else if (!strcmp(n, "fgColor") && rd->nfills && rd->fills[rd->nfills - 1]) {
        rd->fills[rd->nfills - 1] = FILL_ON | read_color(&x, 0xFFFFFF);
      } else if (!strcmp(n, "stop") && rd->nfills) {
        rd->fills[rd->nfills - 1] = FILL_ON | 0xFFFFFF;     /* gradients: their first colour (below) */
      } else if (!strcmp(n, "color") && rd->nfills && rd->fills[rd->nfills - 1] == (FILL_ON | 0xFFFFFF)) {
        rd->fills[rd->nfills - 1] = FILL_ON | read_color(&x, 0xFFFFFF);
      }
      continue;
    }
    if (section == 3) {
      if (!strcmp(n, "border")) {
        void *nb = realloc(rd->borders, (size_t)(rd->nborders + 1) * sizeof(*rd->borders));
        if (!nb || rd->nborders > 60000) continue;
        rd->borders = nb;
        memset(&rd->borders[rd->nborders], 0, sizeof(*rd->borders));
        rd->nborders++;
        side = -1;
        continue;
      }
      if (!rd->nborders) continue;
      if (!strcmp(n, "left") || !strcmp(n, "start")) side = B_LEFT;
      else if (!strcmp(n, "right") || !strcmp(n, "end")) side = B_RIGHT;
      else if (!strcmp(n, "top")) side = B_TOP;
      else if (!strcmp(n, "bottom")) side = B_BOTTOM;
      else if (!strcmp(n, "diagonal")) side = -1;
      else if (!strcmp(n, "color") && side >= 0) { rd->borders[rd->nborders - 1].col[side] = read_color(&x, COLOR_AUTO); continue; }
      else continue;
      if (side >= 0) {
        rd->borders[rd->nborders - 1].st[side] = (u8)border_style(xml_attr(&x, "style"));
        rd->borders[rd->nborders - 1].col[side] = COLOR_AUTO;
      }
      continue;
    }
    if (section == 4) {
      if (!strcmp(n, "xf")) {
        const char *v;
        int *nx = (int *)realloc(rd->xf_style, (size_t)(rd->nxf + 1) * sizeof(int));
        if (!nx || rd->nxf > 60000) continue;
        rd->xf_style = nx;
        rd->xf_style[rd->nxf++] = 0;
        free(cs);
        if (!(cs = (Style *)calloc(1, sizeof(Style)))) continue;
        cs->locked = 1;
        if ((v = xml_attr(&x, "fontId"))) {
          int fi = atoi(v);
          if (fi >= 0 && fi < rd->nfonts) { int bf = book_font(rd->b, &rd->fonts[fi]); cs->font = (u16)(bf < 0 ? 0 : bf); }
        }
        if ((v = xml_attr(&x, "fillId"))) { int fi = atoi(v); if (fi >= 0 && fi < rd->nfills) cs->fill = rd->fills[fi]; }
        if ((v = xml_attr(&x, "borderId"))) {
          int bi = atoi(v), k;
          if (bi >= 0 && bi < rd->nborders) for (k = 0; k < 4; k++) { cs->border[k] = rd->borders[bi].st[k]; cs->bcolor[k] = cs->border[k] ? rd->borders[bi].col[k] : 0; }
        }
        if ((v = xml_attr(&x, "numFmtId"))) {
          int id = atoi(v), k;
          if (id < 164) cs->numfmt = (u16)(id >= 0 ? id : 0);
          for (k = 0; k < rd->nfmtmap; k++) if (rd->fmtids[k] == id) { cs->numfmt = (u16)rd->fmtmap[k]; break; }
          if (id >= 164 && k == rd->nfmtmap) cs->numfmt = 0;
          /* ids Excel shows with locale formats but files don't define */
          if (id >= 50 && id < 164 && k == rd->nfmtmap) cs->numfmt = 0;
        }
        if (xml_attr(&x, "applyAlignment") || 1) {
          /* the alignment child follows */
        }
        if (x.pending_end) {
          int si = book_style(rd->b, cs);
          rd->xf_style[rd->nxf - 1] = si < 0 ? 0 : si;
          free(cs); cs = NULL;
        }
      } else if (!strcmp(n, "alignment") && cs) {
        const char *v;
        if ((v = xml_attr(&x, "horizontal"))) {
          cs->halign = !strcmp(v, "left") ? HA_LEFT : !strcmp(v, "center") ? HA_CENTER : !strcmp(v, "right") ? HA_RIGHT :
                       !strcmp(v, "fill") ? HA_FILL : !strcmp(v, "justify") || !strcmp(v, "distributed") ? HA_JUSTIFY :
                       !strcmp(v, "centerContinuous") ? HA_CENTERSEL : HA_GENERAL;
        }
        if ((v = xml_attr(&x, "vertical"))) cs->valign = !strcmp(v, "top") ? VA_TOP : !strcmp(v, "center") ? VA_CENTER : VA_BOTTOM;
        if ((v = xml_attr(&x, "wrapText"))) cs->wrap = !strcmp(v, "1") || !strcmp(v, "true");
        if ((v = xml_attr(&x, "shrinkToFit"))) cs->shrink = !strcmp(v, "1") || !strcmp(v, "true");
        if ((v = xml_attr(&x, "indent"))) { int k = atoi(v); cs->indent = (u8)(k < 0 ? 0 : k > 250 ? 250 : k); }
        if ((v = xml_attr(&x, "textRotation"))) { int k = atoi(v); cs->rotation = (i16)(k > 90 && k <= 180 ? 90 - k : k > 180 ? 0 : k); }
      } else if (!strcmp(n, "protection") && cs) {
        const char *v;
        if ((v = xml_attr(&x, "locked"))) cs->locked = strcmp(v, "0") && strcmp(v, "false");
        if ((v = xml_attr(&x, "hidden"))) cs->hidden = !strcmp(v, "1") || !strcmp(v, "true");
      }
    }
  }
  free(cs);
  xml_free(&x);
  free(d);
}
/* "B12" into row, column */
static int cell_ref(const char *s, i32 *r, i32 *c)
{
  int n = parse_col(s, c);
  long v = 0;
  if (!n) return 0;
  s += n;
  if (!isdigit((u8)*s)) return 0;
  while (isdigit((u8)*s)) { v = v * 10 + (*s - '0'); if (v > MAXROW) return 0; s++; }
  if (v < 1) return 0;
  *r = (i32)(v - 1);
  return 1;
}
static int rect_ref(const char *s, Rect *rc)
{
  const char *colon = strchr(s, ':');
  if (!cell_ref(s, &rc->r1, &rc->c1)) return 0;
  if (colon) { if (!cell_ref(colon + 1, &rc->r2, &rc->c2)) return 0; }
  else { rc->r2 = rc->r1; rc->c2 = rc->c1; }
  if (rc->r1 > rc->r2) { i32 t = rc->r1; rc->r1 = rc->r2; rc->r2 = t; }
  if (rc->c1 > rc->c2) { i32 t = rc->c1; rc->c1 = rc->c2; rc->c2 = t; }
  return 1;
}
typedef struct { int si; i32 r, c; Formula *f; } Shared;
static void read_sheet(Rd *rd, ZipReader *z, const char *path, int sheet)
{
  size_t len;
  u8 *d = part(z, path, &len);
  Xml x;
  Sheet *s = rd->b->sh[sheet];
  Shared *sh = NULL;
  int nsh = 0, k;
  i32 cur_row = -1, next_col = 0;
  int curf = -1;                                  /* the AutoFilter column being read */
  /* conditional formatting: the rules being read (with Excel's priorities, sorted at the end) */
  CondRule *cr = NULL; int *cprio = NULL; int ncr = 0, curc = -1, nformula = 0, ncfvo = 0, ncolor = 0;
  Rect *careas = NULL; int ncareas = 0;
  if (!d) return;
  page_xml_sheet_in(rd->b, sheet);
  xml_init(&x, (const char *)d, len);
  while (xml_next(&x) != XT_EOF) {
    const char *n = x.name;
    if (x.type != XT_START) continue;
    if (page_xml_sheet(s, &x)) continue;
    if (!strcmp(n, "sheetFormatPr")) {
      const char *v;
      if ((v = xml_attr(&x, "defaultRowHeight"))) { double h = strtod(v, NULL); if (h > 0 && h < 410) s->defheight = (u16)(h * 4 / 3 + 0.5); }
      if ((v = xml_attr(&x, "defaultColWidth"))) { double w = strtod(v, NULL); if (w > 0 && w < 256) s->defwidth = (u16)(w * 7 + 0.5); }
      else if ((v = xml_attr(&x, "baseColWidth"))) { double w = strtod(v, NULL); if (w > 0 && w < 256) s->defwidth = (u16)((w + 0.71) * 7 + 0.5); }
    } else if (!strcmp(n, "sheetView")) {
      const char *v;
      if ((v = xml_attr(&x, "showGridLines"))) s->gridlines = strcmp(v, "0") && strcmp(v, "false");
      if ((v = xml_attr(&x, "showRowColHeaders"))) s->headings = strcmp(v, "0") && strcmp(v, "false");
      if ((v = xml_attr(&x, "showZeros"))) s->showzeros = strcmp(v, "0") && strcmp(v, "false");
      if ((v = xml_attr(&x, "zoomScale"))) { int zz = atoi(v); if (zz >= 10 && zz <= 400) s->zoom = (u16)zz; }
      if ((v = xml_attr(&x, "tabSelected")) && atoi(v)) rd->b->active = sheet;
    } else if (!strcmp(n, "pane")) {
      const char *st = xml_attr(&x, "state"), *v;
      if (st && (!strcmp(st, "frozen") || !strcmp(st, "frozenSplit"))) {
        if ((v = xml_attr(&x, "xSplit"))) { long c = atol(v); if (c > 0 && c < MAXCOL) s->freezec = (i32)c; }
        if ((v = xml_attr(&x, "ySplit"))) { long r = atol(v); if (r > 0 && r < MAXROW) s->freezer = (i32)r; }
      }
    } else if (!strcmp(n, "selection")) {
      const char *v = xml_attr(&x, "activeCell");
      i32 r, c;
      if (v && cell_ref(v, &r, &c)) { s->cur_r = r; s->cur_c = c; s->sel.r1 = s->sel.r2 = r; s->sel.c1 = s->sel.c2 = c; }
    } else if (!strcmp(n, "tabColor")) {
      s->tabcolor = FILL_ON | read_color(&x, 0);
    } else if (!strcmp(n, "col")) {
      const char *mi = xml_attr(&x, "min"), *ma = xml_attr(&x, "max"), *w = xml_attr(&x, "width"), *hid = xml_attr(&x, "hidden"), *st = xml_attr(&x, "style");
      long c1 = mi ? atol(mi) : 0, c2 = ma ? atol(ma) : 0, c;
      if (c1 < 1 || c2 < c1 || c1 > MAXCOL) continue;
      if (c2 > MAXCOL) c2 = MAXCOL;
      if (c2 - c1 > 256 && !st && !hid) {
        /* a width for "all the rest": the sheet's default */
        if (w) { double wd = strtod(w, NULL); if (wd > 0 && wd < 256) s->defwidth = (u16)(wd * 7 + 0.5); }
        continue;
      }
      for (c = c1; c <= c2; c++) {
        ColInfo *ci;
        if (c - 1 >= s->ncols) {
          i32 nn = (i32)c + 15 > MAXCOL ? MAXCOL : (i32)c + 15;
          ColInfo *nc = (ColInfo *)realloc(s->cols, (size_t)nn * sizeof(ColInfo));
          if (!nc) break;
          memset(nc + s->ncols, 0, (size_t)(nn - s->ncols) * sizeof(ColInfo));
          s->cols = nc; s->ncols = nn;
        }
        ci = &s->cols[c - 1];
        if (w) { double wd = strtod(w, NULL); if (wd >= 0 && wd < 256) { ci->width = (u16)(wd * 7 + 0.5); ci->custom = 1; } }
        if (hid && (!strcmp(hid, "1") || !strcmp(hid, "true"))) ci->hidden = 1;
        if (st) { int xi = atoi(st); if (xi >= 0 && xi < rd->nxf) ci->style = (u16)rd->xf_style[xi]; }
      }
    } else if (!strcmp(n, "row")) {
      const char *v = xml_attr(&x, "r");
      long r = v ? atol(v) - 1 : cur_row + 1;
      if (r < 0 || r >= MAXROW) { cur_row = -1; continue; }
      cur_row = (i32)r;
      next_col = 0;
      {
        const char *ht = xml_attr(&x, "ht"), *hid = xml_attr(&x, "hidden"), *st = xml_attr(&x, "s"), *cf = xml_attr(&x, "customFormat"), *ch = xml_attr(&x, "customHeight");
        if (ht || (hid && atoi(hid)) || (st && cf && atoi(cf))) {
          Row *row;
          Cell *dummy = NULL;
          (void)dummy;
          if (!sheet_cell_make(s, cur_row, 0)) continue;
          row = s->rows[cur_row];
          sheet_cell_clear(s, cur_row, 0);
          if (ht && (ch ? atoi(ch) : 1)) { double h = strtod(ht, NULL); if (h >= 0 && h < 410) { row->height = (u16)(h * 4 / 3 + 0.5); row->custom = 1; } }
          if (hid && atoi(hid)) row->hidden = 1;
          if (st && cf && atoi(cf)) { int xi = atoi(st); if (xi >= 0 && xi < rd->nxf) row->style = (u16)rd->xf_style[xi]; }
        }
      }
    } else if (!strcmp(n, "c") && cur_row >= 0) {
      const char *ref = xml_attr(&x, "r"), *t = xml_attr(&x, "t"), *st = xml_attr(&x, "s");
      i32 r = cur_row, c = next_col;
      int depth = x.depth;
      Buf vtxt, ftxt;
      int has_v = 0, has_f = 0, shared_si = -1, is_master = 0, arr_f = 0, has_cm = xml_attr(&x, "cm") != NULL;
      Rect master_ref, arr_ref;
      char type[16];
      Cell *cell;
      int style = 0;
      if (ref && !cell_ref(ref, &r, &c)) continue;
      next_col = c + 1;
      strncpy(type, t ? t : "n", sizeof type - 1); type[sizeof type - 1] = 0;
      if (st) { int xi = atoi(st); if (xi >= 0 && xi < rd->nxf) style = rd->xf_style[xi]; }
      buf_init(&vtxt); buf_init(&ftxt);
      if (!x.pending_end) {
        while (xml_next(&x) != XT_EOF) {
          if (x.type == XT_END && x.depth < depth) break;
          if (x.type != XT_START) continue;
          if (!strcmp(x.name, "v")) {
            has_v = 1;
            while (xml_next(&x) == XT_TEXT) buf_add(&vtxt, x.text.data, x.text.len);
            if (x.type == XT_END && x.depth < depth) break;
          } else if (!strcmp(x.name, "f")) {
            const char *ft = xml_attr(&x, "t"), *si = xml_attr(&x, "si"), *fr = xml_attr(&x, "ref");
            has_f = 1;
            if (ft && !strcmp(ft, "array") && fr && rect_ref(fr, &arr_ref)) arr_f = 1;   /* a dynamic array (cm) or {=...} */
            if (ft && !strcmp(ft, "shared") && si) {
              shared_si = atoi(si);
              if (fr && rect_ref(fr, &master_ref)) is_master = 1;
            }
            if (!x.pending_end) {
              while (xml_next(&x) == XT_TEXT) buf_add(&ftxt, x.text.data, x.text.len);
              if (x.type == XT_END && x.depth < depth) break;
            }
          } else if (!strcmp(x.name, "is")) {
            has_v = 1;
            collect_text(&x, &vtxt, 1);
            strcpy(type, "inlineStr");
          }
        }
      }
      if (!(cell = sheet_cell_make(s, r, c))) { buf_free(&vtxt); buf_free(&ftxt); continue; }
      cell->style = (u16)style;
      /* the value */
      if (has_v) {
        const char *vs = vtxt.data ? vtxt.data : "";
        if (!strcmp(type, "s")) {
          long i = atol(vs);
          cell->v = i >= 0 && i < rd->nstrings ? val_str(rd->strings[i]) : val_str("");
        } else if (!strcmp(type, "str") || !strcmp(type, "inlineStr")) cell->v = val_str(vs);
        else if (!strcmp(type, "b")) cell->v = val_bool(atoi(vs) != 0);
        else if (!strcmp(type, "e")) { int e = err_parse(vs, NULL); cell->v = val_err(e ? e : E_VALUE); }
        else if (!strcmp(type, "d")) { double dd; int k2; cell->v = parse_number(vs, &dd, &k2) ? val_num(dd) : val_str(vs); }
        else { char *end; double dd = strtod(vs, &end); cell->v = end != vs ? val_num(dd) : val_empty(); }
      }
      /* the formula */
      if (has_f) {
        Formula *f = NULL;
        int pos;
        if (ftxt.len) {
          f = formula_parse(rd->b, sheet, ftxt.data, &pos);
          if (f && shared_si >= 0 && is_master && nsh < 100000) {
            Shared *ns = (Shared *)realloc(sh, (size_t)(nsh + 1) * sizeof(Shared));
            if (ns) { sh = ns; sh[nsh].si = shared_si; sh[nsh].r = r; sh[nsh].c = c; sh[nsh].f = formula_copy(f); nsh++; }
          }
        } else if (shared_si >= 0) {
          for (k = 0; k < nsh; k++) if (sh[k].si == shared_si && sh[k].f) {
            f = formula_copy(sh[k].f);
            if (f) formula_offset(f, r - sh[k].r, c - sh[k].c);
            break;
          }
        }
        cell = sheet_cell(s, r, c);
        if (f) {
          /* without the dynamic-array mark a formula is from before dynamic arrays */
          f->legacy = !arr_f;
          if (arr_f) { f->sr = arr_ref.r2 - arr_ref.r1 + 1; f->sc = arr_ref.c2 - arr_ref.c1 + 1; }
          if (arr_f && !has_cm) { f->cse = 1; f->arows = f->sr; f->acols = f->sc; }
        }
        if (cell && f) { formula_free(cell->f); cell->f = f; }
        else formula_free(f);
      }
      buf_free(&vtxt); buf_free(&ftxt);
    } else if (!strcmp(n, "mergeCell")) {
      const char *ref = xml_attr(&x, "ref");
      Rect m;
      if (ref && rect_ref(ref, &m) && s->nmerges < 100000) {
        Rect *nm = (Rect *)realloc(s->merges, (size_t)(s->nmerges + 1) * sizeof(Rect));
        if (nm) { s->merges = nm; s->merges[s->nmerges++] = m; }
      }
    } else if (!strcmp(n, "autoFilter")) {
      const char *ref = xml_attr(&x, "ref");
      Rect m;
      if (ref && rect_ref(ref, &m)) s->filter = m;
    } else if (!strcmp(n, "filterColumn") && s->filter.r1 >= 0) {
      const char *id = xml_attr(&x, "colId");
      FilterCol *nf = (FilterCol *)realloc(s->fcols, (size_t)(s->nfcols + 1) * sizeof *nf);
      curf = -1;
      if (nf && id) {
        s->fcols = nf;
        curf = s->nfcols++;
        memset(&nf[curf], 0, sizeof *nf);
        nf[curf].col = s->filter.c1 + atoi(id);
      }
    } else if (!strcmp(n, "filters") && curf >= 0) {
      const char *bl = xml_attr(&x, "blank");
      s->fcols[curf].blanks = bl && (!strcmp(bl, "1") || !strcmp(bl, "true"));
    } else if (!strcmp(n, "filter") && curf >= 0) {
      const char *v = xml_attr(&x, "val");
      FilterCol *f = &s->fcols[curf];
      char **nv = v ? (char **)realloc(f->vals, (size_t)(f->n + 1) * sizeof(char *)) : NULL;
      if (nv) { f->vals = nv; f->vals[f->n++] = str_dup(v); }
    } else if (!strcmp(n, "hyperlink")) {
      /* a link: to a place in the workbook (location), or out through the sheet's relationships (r:id, read after) */
      const char *ref = xml_attr(&x, "ref"), *rid = xml_attr(&x, "id"), *loc = xml_attr(&x, "location"), *tip = xml_attr(&x, "tooltip");
      Rect m;
      if (ref && rect_ref(ref, &m) && s->nlinks < 100000 && (rid || loc)) {
        Link *nl = (Link *)realloc(s->links, (size_t)(s->nlinks + 1) * sizeof *nl);
        char t[600];
        if (rid) snprintf(t, sizeof t, "\001%s", rid);          /* (resolved by read_sheet_rels) */
        else snprintf(t, sizeof t, "#%s", loc);
        if (nl) { s->links = nl; nl[s->nlinks].r = m.r1; nl[s->nlinks].c = m.c1; nl[s->nlinks].target = str_dup(t); nl[s->nlinks].tip = str_dup(tip ? tip : ""); s->nlinks++; }
      }
    } else if (!strcmp(n, "conditionalFormatting")) {
      /* its cells: "A1:A10 C1:C5" */
      const char *sq = xml_attr(&x, "sqref");
      CondRule tmp;
      cond_rule_init(&tmp);
      free(careas); careas = NULL; ncareas = 0;
      if (sq && cond_parse_areas(rd->b, sq, &tmp)) { careas = tmp.areas; ncareas = tmp.nareas; }
      curc = -1;
    } else if (!strcmp(n, "cfRule") && ncareas && ncr < 10000) {
      static const char *const types[] = { "cellIs", "containsText", "notContainsText", "beginsWith", "endsWith", "duplicateValues", "uniqueValues",
                                           "top10", "aboveAverage", "containsBlanks", "notContainsBlanks", "containsErrors", "notContainsErrors",
                                           "expression", "dataBar", "colorScale", NULL };
      static const char *const opsn[] = { "between", "notBetween", "equal", "notEqual", "greaterThan", "lessThan", "greaterThanOrEqual", "lessThanOrEqual", NULL };
      const char *t = xml_attr(&x, "type"), *v;
      CondRule *nr = (CondRule *)realloc(cr, (size_t)(ncr + 1) * sizeof *nr);
      int *np, ty;
      curc = -1;
      if (!nr) continue;
      cr = nr;
      if (!(np = (int *)realloc(cprio, (size_t)(ncr + 1) * sizeof(int)))) continue;
      cprio = np;
      for (ty = 0; types[ty] && (!t || strcmp(types[ty], t)); ty++) ;
      if (!types[ty]) continue;                      /* (time periods and the like aren't kept) */
      curc = ncr++;
      nr = &cr[curc];
      cond_rule_init(nr);
      nr->type = (u8)ty;
      if ((nr->areas = (Rect *)malloc((size_t)ncareas * sizeof(Rect)))) { memcpy(nr->areas, careas, (size_t)ncareas * sizeof(Rect)); nr->nareas = ncareas; }
      cprio[curc] = (v = xml_attr(&x, "priority")) ? atoi(v) : 1000000 + curc;
      if ((v = xml_attr(&x, "operator"))) { int k2; for (k2 = 0; opsn[k2] && strcmp(opsn[k2], v); k2++) ; nr->op = (u8)(opsn[k2] ? k2 : CO_EQ); }
      if ((v = xml_attr(&x, "text"))) nr->text = str_dup(v);
      if ((v = xml_attr(&x, "rank"))) nr->rank = atoi(v);
      if ((v = xml_attr(&x, "percent"))) nr->percent = !strcmp(v, "1") || !strcmp(v, "true");
      if ((v = xml_attr(&x, "bottom"))) nr->bottom = !strcmp(v, "1") || !strcmp(v, "true");
      if ((v = xml_attr(&x, "aboveAverage"))) nr->below = !strcmp(v, "0") || !strcmp(v, "false");
      if ((v = xml_attr(&x, "equalAverage"))) nr->equal = !strcmp(v, "1") || !strcmp(v, "true");
      if ((v = xml_attr(&x, "stopIfTrue"))) nr->stop = !strcmp(v, "1") || !strcmp(v, "true");
      if ((v = xml_attr(&x, "dxfId"))) {
        int di = atoi(v);
        if (di >= 0 && di < rd->ndxfs) {
          const struct Dxf *dx = &rd->dxfs[di];
          nr->fill = dx->fill; nr->color = dx->color; nr->bold = dx->bold; nr->italic = dx->italic; nr->underline = dx->underline; nr->strike = dx->strike;
        }
      }
      nformula = ncfvo = ncolor = 0;
    } else if (!strcmp(n, "formula") && curc >= 0) {
      Buf ft;
      int pos;
      buf_init(&ft);
      if (!x.pending_end) while (xml_next(&x) == XT_TEXT) buf_add(&ft, x.text.data, x.text.len);
      buf_addc(&ft, 0);
      /* cellIs keeps its value(s); the other kinds keep only an expression's formula */
      if (ft.data && ft.data[0] && (cr[curc].type == CF_CELL || (cr[curc].type == CF_EXPR && !nformula))) {
        Formula *f = formula_parse(rd->b, sheet, ft.data, &pos);
        if (nformula == 0) cr[curc].f1 = f; else if (nformula == 1) cr[curc].f2 = f; else formula_free(f);
      }
      nformula++;
      buf_free(&ft);
    } else if (!strcmp(n, "colorScale") && curc >= 0) {
      ncfvo = ncolor = 0;
    } else if (!strcmp(n, "cfvo") && curc >= 0) {
      ncfvo++;
      if (cr[curc].type == CF_SCALE) cr[curc].three = ncfvo >= 3;
    } else if (!strcmp(n, "color") && curc >= 0 && (cr[curc].type == CF_BAR || cr[curc].type == CF_SCALE)) {
      u32 col = read_color(&x, 0x638EC6);
      if (cr[curc].type == CF_BAR) { if (!ncolor) cr[curc].bar[0] = col; }
      else if (ncolor < 3) cr[curc].bar[ncolor == 0 ? 0 : ncolor == 1 ? (ncfvo >= 3 ? 1 : 2) : 2] = col;
      ncolor++;
    } else if (!strcmp(n, "sheetData") && x.pending_end) {
      /* empty */
    }
  }
  /* rows Excel hid for the AutoFilter are the filter's, not hidden by hand */
  if (s->filter.r1 >= 0 && s->nfcols) {
    i32 r;
    for (r = s->filter.r1 + 1; r <= s->filter.r2 && r < s->nrows; r++)
      if (s->rows[r] && s->rows[r]->hidden) { s->rows[r]->hidden = 0; s->rows[r]->filtered = 1; }
  }
  /* the rules in priority order (insertion sort: there are few) */
  if (ncr) {
    int i, j;
    for (i = 1; i < ncr; i++)
      for (j = i; j > 0 && cprio[j - 1] > cprio[j]; j--) {
        CondRule t = cr[j]; int p = cprio[j];
        cr[j] = cr[j - 1]; cprio[j] = cprio[j - 1]; cr[j - 1] = t; cprio[j - 1] = p;
      }
    /* a cell rule without its value can't be tested */
    for (i = j = 0; i < ncr; i++) {
      if ((cr[i].type == CF_CELL && (!cr[i].f1 || ((cr[i].op == CO_BETWEEN || cr[i].op == CO_NOTBETWEEN) && !cr[i].f2))) || (cr[i].type == CF_EXPR && !cr[i].f1)) { cond_rule_free(&cr[i]); continue; }
      cr[j++] = cr[i];
    }
    s->cfs = cr; s->ncfs = j;
    if (!j) { free(cr); s->cfs = NULL; }
    cr = NULL;
  }
  free(cr); free(cprio); free(careas);
  for (k = 0; k < nsh; k++) formula_free(sh[k].f);
  free(sh);
  xml_free(&x);
  free(d);
}
/* relationship targets from a .rels part: id -> path, made absolute against base ("xl/") */
typedef struct { char id[32]; char target[200]; char type[32]; } Rel;   /* type: the last part of the Type URL ("comments") */
/* a path with its "dir/../" parts taken out, in place */
static void tidy_path(char *p)
{
  char *d;
  while ((d = strstr(p, "/../"))) {
    char *s = d;
    while (s > p && s[-1] != '/') s--;
    memmove(s, d + 4, strlen(d + 4) + 1);
  }
}
static int read_rels(ZipReader *z, const char *path, const char *base, Rel **out)
{
  size_t len;
  u8 *d = part(z, path, &len);
  Xml x;
  int n = 0;
  *out = NULL;
  if (!d) return 0;
  xml_init(&x, (const char *)d, len);
  while (xml_next(&x) != XT_EOF) {
    if (x.type == XT_START && !strcmp(x.name, "Relationship")) {
      const char *id = xml_attr(&x, "Id"), *t = xml_attr(&x, "Target"), *ty = xml_attr(&x, "Type"), *mode = xml_attr(&x, "TargetMode");
      Rel *nr;
      if (!id || !t || n > 10000 || !(nr = (Rel *)realloc(*out, (size_t)(n + 1) * sizeof(Rel)))) continue;
      *out = nr;
      strncpy(nr[n].id, id, sizeof nr[n].id - 1); nr[n].id[sizeof nr[n].id - 1] = 0;
      if (mode && !strcmp(mode, "External")) snprintf(nr[n].target, sizeof nr[n].target, "%s", t);   /* (a web address: as it is) */
      else if (t[0] == '/') snprintf(nr[n].target, sizeof nr[n].target, "%s", t + 1);
      else { snprintf(nr[n].target, sizeof nr[n].target, "%s%s", base, t); tidy_path(nr[n].target); }
      nr[n].type[0] = 0;
      if (ty) { const char *sl = strrchr(ty, '/'); snprintf(nr[n].type, sizeof nr[n].type, "%s", sl ? sl + 1 : ty); }
      n++;
    }
  }
  xml_free(&x);
  free(d);
  return n;
}
/* a sheet's notes from its comments part */
static void read_comments(Rd *rd, ZipReader *z, const char *path, int sheet)
{
  size_t len;
  u8 *d = part(z, path, &len);
  Xml x;
  char **authors = NULL;
  int na = 0, i;
  if (!d) return;
  xml_init(&x, (const char *)d, len);
  while (xml_next(&x) != XT_EOF) {
    if (x.type != XT_START) continue;
    if (!strcmp(x.name, "author")) {
      Buf a;
      char **nn = (char **)realloc(authors, (size_t)(na + 1) * sizeof(char *));
      if (!nn) continue;
      authors = nn;
      buf_init(&a);
      if (!x.pending_end) while (xml_next(&x) == XT_TEXT) buf_add(&a, x.text.data, x.text.len);
      buf_addc(&a, 0);
      authors[na++] = buf_take(&a);
    } else if (!strcmp(x.name, "comment")) {
      const char *ref = xml_attr(&x, "ref"), *au = xml_attr(&x, "authorId");
      i32 r, c;
      int ai = au ? atoi(au) : -1;
      char who[200] = "";
      Buf t;
      if (!ref || !cell_ref(ref, &r, &c)) { xml_skip(&x); continue; }
      if (ai >= 0 && ai < na && authors[ai]) snprintf(who, sizeof who, "%s", authors[ai]);
      buf_init(&t);
      collect_text(&x, &t, 1);
      buf_addc(&t, 0);
      {
        /* Excel starts the text with "Author:" in bold and a line break: that's the author again */
        const char *tx = t.data ? t.data : "";
        size_t lw = strlen(who);
        if (lw && !strncmp(tx, who, lw) && tx[lw] == ':') { tx += lw + 1; while (*tx == '\n' || *tx == '\r') tx++; }
        if (*tx) {
          Sheet *s = rd->b->sh[sheet];
          Note *nn = (Note *)realloc(s->notes, (size_t)(s->nnotes + 1) * sizeof *nn);
          if (nn) { s->notes = nn; memset(&nn[s->nnotes], 0, sizeof *nn); nn[s->nnotes].r = r; nn[s->nnotes].c = c; nn[s->nnotes].text = str_dup(tx); nn[s->nnotes].author = str_dup(who); s->nnotes++; }
        }
      }
      buf_free(&t);
    }
  }
  for (i = 0; i < na; i++) free(authors[i]);
  free(authors);
  xml_free(&x);
  free(d);
}
/* what a sheet's relationships point at: its notes */
static void read_sheet_rels(Rd *rd, ZipReader *z, const char *sheetpath, int sheet)
{
  char rp[512], base[256];
  const char *sl = strrchr(sheetpath, '/');
  size_t dl = sl ? (size_t)(sl - sheetpath + 1) : 0;
  Rel *rels;
  int n, k;
  if (dl >= sizeof base) return;
  memcpy(base, sheetpath, dl); base[dl] = 0;
  snprintf(rp, sizeof rp, "%s_rels/%s.rels", base, sl ? sl + 1 : sheetpath);
  n = read_rels(z, rp, base, &rels);
  for (k = 0; k < n; k++) if (!strcmp(rels[k].type, "comments")) read_comments(rd, z, rels[k].target, sheet);
  for (k = 0; k < n; k++) tablexml_read_rel(rd->b, z, rels[k].type, rels[k].target, sheet);   /* tables, pivot tables */
  for (k = 0; k < n; k++) if (!strcmp(rels[k].type, "threadedComment")) cm_read(rd->b, z, rels[k].target, sheet);
  {
    /* links out: their addresses from the relationships (an external link is written as it is) */
    Sheet *s = rd->b->sh[sheet];
    int i, j;
    for (i = j = 0; i < s->nlinks; i++) {
      Link *l = &s->links[i];
      if (l->target && l->target[0] == '\001') {
        char *t = NULL;
        for (k = 0; k < n; k++) if (!strcmp(rels[k].id, l->target + 1)) t = str_dup(rels[k].target);
        free(l->target); l->target = t;
        if (!t) { free(l->tip); continue; }
      }
      s->links[j++] = *l;
    }
    s->nlinks = j;
  }
  free(rels);
}
Book *xlsx_load(const u8 *data, size_t len, char *err, size_t errcap)
{
  ZipReader z;
  Rd rd;
  Book *b;
  size_t wl;
  u8 *wb;
  Xml x;
  Rel *rels = NULL;
  int nrels, i;
  typedef struct { char name[64]; char rid[32]; int hidden; } SheetEnt;
  SheetEnt *ents = NULL;
  int nents = 0;
  typedef struct { char name[64]; int local; char *text; } NameEnt;
  NameEnt *names = NULL;
  int nnames = 0;
  if (err && errcap) err[0] = 0;
  if (!zip_open(&z, data, len)) { if (err) snprintf(err, errcap, "not an .xlsx file (no zip archive)"); return NULL; }
  if (!(wb = part(&z, "xl/workbook.xml", &wl))) { if (err) snprintf(err, errcap, "no workbook inside (xl/workbook.xml)"); zip_close(&z); return NULL; }
  memset(&rd, 0, sizeof rd);
  if (!(b = book_new())) { free(wb); zip_close(&z); return NULL; }
  rd.b = b;
  /* the sheet list and names */
  xml_init(&x, (const char *)wb, wl);
  while (xml_next(&x) != XT_EOF) {
    if (x.type != XT_START) continue;
    if (!strcmp(x.name, "sheet")) {
      const char *nm = xml_attr(&x, "name"), *rid = xml_attr(&x, "id"), *st = xml_attr(&x, "state");
      SheetEnt *ne;
      if (!nm || !rid || nents >= MAXSHEETS || !(ne = (SheetEnt *)realloc(ents, (size_t)(nents + 1) * sizeof(SheetEnt)))) continue;
      ents = ne;
      strncpy(ents[nents].name, nm, 63); ents[nents].name[63] = 0;
      strncpy(ents[nents].rid, rid, 31); ents[nents].rid[31] = 0;
      ents[nents].hidden = st && strcmp(st, "visible");
      nents++;
    } else if (!strcmp(x.name, "customWorkbookView")) {
      page_xml_book_view(b, &x);
    } else if (!strcmp(x.name, "workbookPr")) {
      const char *v = xml_attr(&x, "date1904");
      if (v && (!strcmp(v, "1") || !strcmp(v, "true"))) b->date1904 = 1;
    } else if (!strcmp(x.name, "calcPr")) {
      const char *v = xml_attr(&x, "calcMode");
      if (v && !strcmp(v, "manual")) b->manualcalc = 0;      /* Ledger recalculates on load anyway */
      page_xml_calcpr(b, &x);
    } else if (!strcmp(x.name, "definedName")) {
      const char *nm = xml_attr(&x, "name"), *ls = xml_attr(&x, "localSheetId");
      Buf t;
      NameEnt *nn;
      buf_init(&t);
      while (xml_next(&x) == XT_TEXT) buf_add(&t, x.text.data, x.text.len);
      if (!nm || nnames > 10000 || !(nn = (NameEnt *)realloc(names, (size_t)(nnames + 1) * sizeof(NameEnt)))) { buf_free(&t); continue; }
      names = nn;
      strncpy(names[nnames].name, nm, 63); names[nnames].name[63] = 0;
      names[nnames].local = ls ? atoi(ls) : -1;
      names[nnames].text = buf_take(&t);
      nnames++;
    }
  }
  xml_free(&x);
  free(wb);
  if (!nents) { if (err) snprintf(err, errcap, "the workbook has no sheets"); book_free(b); free(ents); zip_close(&z); return NULL; }
  read_theme(&z);
  { size_t tl; u8 *t = part(&z, "xl/theme/theme1.xml", &tl); if (t) { page_read_theme(b, (const char *)t, tl); free(t); } }
  read_shared_strings(&rd, &z);
  read_styles(&rd, &z);
  nrels = read_rels(&z, "xl/_rels/workbook.xml.rels", "xl/", &rels);
  /* make the sheets first, so formulas can refer to any of them */
  {
    char first[32];
    strncpy(first, ents[0].name, 31); first[31] = 0;
    for (i = 0; i < nents; i++) {
      char nm[32];
      strncpy(nm, ents[i].name, 31); nm[31] = 0;
      if (i == 0) { if (!book_rename_sheet(b, 0, nm)) { char u[32]; book_unique_sheet_name(b, nm, u); book_rename_sheet(b, 0, u); } }
      else if (book_add_sheet(b, nm, -1) < 0) { char u[32]; book_unique_sheet_name(b, nm, u); book_add_sheet(b, u, -1); }
      if (i < b->nsh) b->sh[i]->hidden = (u8)ents[i].hidden;
    }
    (void)first;
  }
  /* names before the cells (formulas may use them) */
  for (i = 0; i < nnames; i++) {
    Name *nn;
    if (page_xml_name(b, names[i].name, names[i].local, names[i].text)) { free(names[i].text); continue; }
    if (!strncmp(names[i].name, "_xlnm.", 6) || !(nn = (Name *)realloc(b->names, (size_t)(b->nnames + 1) * sizeof(Name)))) { free(names[i].text); continue; }
    b->names = nn;
    memset(&b->names[b->nnames], 0, sizeof(Name));
    strncpy(b->names[b->nnames].name, names[i].name, 63);
    b->names[b->nnames].sheet = (u16)(names[i].local >= 0 && names[i].local < b->nsh ? names[i].local : SHEET_SELF);
    b->names[b->nnames].formula_text = names[i].text;
    b->nnames++;
  }
  for (i = 0; i < b->nnames; i++) {
    int pos;
    const char *t = b->names[i].formula_text;
    if (t) b->names[i].f = formula_parse(b, b->names[i].sheet == SHEET_SELF ? 0 : b->names[i].sheet, t[0] == '=' ? t + 1 : t, &pos);
    if (b->names[i].f && strstr(t, "_xl")) {
      /* shown without the file's prefixes (_xlfn.LAMBDA(_xlpm.x,...) as LAMBDA(x,...)) */
      Buf nt;
      buf_init(&nt);
      formula_text(b, b->names[i].sheet == SHEET_SELF ? 0 : b->names[i].sheet, b->names[i].f, &nt);
      if (nt.data) { free(b->names[i].formula_text); b->names[i].formula_text = buf_take(&nt); }
      else buf_free(&nt);
    }
  }
  free(names);
  for (i = 0; i < nents && i < b->nsh; i++) {
    int k;
    for (k = 0; k < nrels; k++) if (!strcmp(rels[k].id, ents[i].rid)) { read_sheet(&rd, &z, rels[k].target, i); read_sheet_rels(&rd, &z, rels[k].target, i); break; }
  }
  /* doc properties */
  {
    size_t cl;
    u8 *core = part(&z, "docProps/core.xml", &cl);
    if (core) {
      xml_init(&x, (const char *)core, cl);
      while (xml_next(&x) != XT_EOF) {
        if (x.type == XT_START && (!strcmp(x.name, "title") || !strcmp(x.name, "creator"))) {
          int title = !strcmp(x.name, "title");
          Buf t;
          buf_init(&t);
          while (xml_next(&x) == XT_TEXT) buf_add(&t, x.text.data, x.text.len);
          if (t.data) {
            if (title) { strncpy(b->title, t.data, sizeof b->title - 1); }
            else { strncpy(b->author, t.data, sizeof b->author - 1); }
          }
          buf_free(&t);
        } else if (x.type == XT_START) page_xml_core(b, &x);
      }
      xml_free(&x);
      free(core);
    }
    if ((core = part(&z, "docProps/app.xml", &cl))) {
      xml_init(&x, (const char *)core, cl);
      while (xml_next(&x) != XT_EOF) if (x.type == XT_START) page_xml_core(b, &x);
      xml_free(&x);
      free(core);
    }
  }
  for (i = 0; i < rd.nstrings; i++) free(rd.strings[i]);
  free(rd.strings);
  free(rd.xf_style); free(rd.fonts); free(rd.fills); free(rd.dxfs); free(rd.borders); free(rd.fmtmap); free(rd.fmtids);
  free(rels);
  free(ents);
  zip_close(&z);
  tablexml_loaded(b);
  /* loading isn't an edit */
  {
    extern void book_forget_undo(Book *b);
    book_forget_undo(b);
  }
  if (b->active >= b->nsh) b->active = 0;
  book_adopt_spills(b);
  book_recalc(b);
  b->dirty = 0;
  return b;
}

/* ------------------------------------------------------------------ writing */
/* a formula files mark as a dynamic array (cm="1" and Excel's XLDAPR cell metadata): it spilled or
   worked on arrays */
static int xlsx_dynamic(const Formula *f) { return f && !f->legacy && !f->cse && ((long)f->sr * f->sc > 1 || f->arrayeval); }
static int book_has_dynamic(Book *b)
{
  int si;
  for (si = 0; si < b->nsh; si++) {
    Sheet *s = b->sh[si];
    i32 r;
    for (r = 0; r < s->nrows; r++) {
      int i;
      if (s->rows[r]) for (i = 0; i < s->rows[r]->n; i++) if (xlsx_dynamic(s->rows[r]->c[i].f)) return 1;
    }
  }
  return 0;
}
static void xesc(Buf *o, const char *s) { xml_escape(o, s, strlen(s)); }
/* functions that files write with the _xlfn. prefix (added to Excel after 2007) */
static const char *const xlfn[] = { "AGGREGATE", "CEILING.MATH", "CONCAT", "DAYS", "FLOOR.MATH", "IFNA", "IFS", "ISFORMULA", "MAXIFS", "MINIFS",
  "MODE.SNGL", "NUMBERVALUE", "PERCENTILE.EXC", "PERCENTILE.INC", "QUARTILE.INC", "RANK.EQ", "STDEV.P", "STDEV.S", "SWITCH",
  "TEXTJOIN", "UNICHAR", "VAR.P", "VAR.S", "XLOOKUP", "XOR", NULL };
/* the formula as a file writes it (no =, newer functions prefixed) */
static void file_formula(Book *b, int sheet, const Formula *f, Buf *o)
{
  Buf t;
  const char *p;
  int q = 0;
  buf_init(&t);
  formula_file_mode = 1;
  formula_text(b, sheet, f, &t);
  formula_file_mode = 0;
  for (p = t.data ? t.data : ""; *p; ) {
    if (*p == '"') { q = !q; buf_addc(o, *p++); continue; }
    if (!q && (isalpha((u8)*p) || *p == '_') && (p == t.data || (!isalnum((u8)p[-1]) && p[-1] != '.' && p[-1] != '_' && p[-1] != '\''))) {
      const char *e = p;
      size_t n;
      int k;
      while (isalnum((u8)*e) || *e == '.' || *e == '_') e++;
      n = (size_t)(e - p);
      if (*e == '(') {
        char nm[64];
        int id;
        for (k = 0; xlfn[k]; k++) if (strlen(xlfn[k]) == n && !strncasecmp(xlfn[k], p, n)) { buf_adds(o, "_xlfn."); break; }
        if (!xlfn[k] && n < sizeof nm) {
          memcpy(nm, p, n); nm[n] = 0;
          if ((id = func_lookup(nm)) >= 0 && (func_table[id].flags & FF_XLFN)) buf_adds(o, "_xlfn.");
        }
      }
      buf_add(o, p, n);
      p = e;
      continue;
    }
    buf_addc(o, *p++);
  }
  buf_free(&t);
}
static void color_attr(Buf *o, const char *tag, u32 c)
{
  if (c == COLOR_AUTO) buf_printf(o, "<%s auto=\"1\"/>", tag);
  else buf_printf(o, "<%s rgb=\"FF%06lX\"/>", tag, (unsigned long)(c & 0xFFFFFF));
}
static const char *border_name(int s)
{
  static const char *const n[] = { NULL, "thin", "medium", "thick", "double", "dotted", "dashed", "hair" };
  return s > 0 && s < 8 ? n[s] : NULL;
}
/* conditional formats' looks: one dxf for each rule that has one, sheet by sheet (write_cond counts the same way) */
static int has_dxf(const CondRule *r) { return r->type != CF_BAR && r->type != CF_SCALE; }
static void write_dxfs(Book *b, Buf *o)
{
  int si, i, n = 0;
  for (si = 0; si < b->nsh; si++) for (i = 0; i < b->sh[si]->ncfs; i++) n += has_dxf(&b->sh[si]->cfs[i]);
  if (!n) return;
  buf_printf(o, "<dxfs count=\"%d\">", n);
  for (si = 0; si < b->nsh; si++)
    for (i = 0; i < b->sh[si]->ncfs; i++) {
      const CondRule *r = &b->sh[si]->cfs[i];
      if (!has_dxf(r)) continue;
      buf_adds(o, "<dxf>");
      if (r->bold || r->italic || r->underline || r->strike || r->color != COLOR_AUTO) {
        buf_adds(o, "<font>");
        if (r->bold) buf_adds(o, "<b/>");
        if (r->italic) buf_adds(o, "<i/>");
        if (r->strike) buf_adds(o, "<strike/>");
        if (r->underline) buf_adds(o, "<u/>");
        if (r->color != COLOR_AUTO) color_attr(o, "color", r->color);
        buf_adds(o, "</font>");
      }
      if (r->fill) { buf_adds(o, "<fill><patternFill>"); color_attr(o, "bgColor", r->fill & 0xFFFFFF); buf_adds(o, "</patternFill></fill>"); }
      buf_adds(o, "</dxf>");
    }
  buf_adds(o, "</dxfs>");
}
/* a sheet's conditional formatting; dxf is the first dxf number its rules use */
static void write_cond(Book *b, int si, int dxf, Buf *o)
{
  static const char *const types[] = { "cellIs", "containsText", "notContainsText", "beginsWith", "endsWith", "duplicateValues", "uniqueValues",
                                       "top10", "aboveAverage", "containsBlanks", "notContainsBlanks", "containsErrors", "notContainsErrors",
                                       "expression", "dataBar", "colorScale" };
  static const char *const opsn[] = { "between", "notBetween", "equal", "notEqual", "greaterThan", "lessThan", "greaterThanOrEqual", "lessThanOrEqual" };
  Sheet *s = b->sh[si];
  int i;
  for (i = 0; i < s->ncfs; i++) {
    const CondRule *r = &s->cfs[i];
    char sq[1024], tl[16], q[600];
    const char *t = r->text ? r->text : "";
    size_t k = 0;
    if (r->type >= CF_N || !r->nareas) continue;
    cond_areas_text(r, sq, sizeof sq);
    ref_text(r->areas[0].r1, r->areas[0].c1, 0, 0, tl);
    /* the text with its quotes doubled, for the formulas Excel wants beside text rules */
    for (; *t && k + 2 < sizeof q; t++) { if (*t == '"') q[k++] = '"'; q[k++] = *t; }
    q[k] = 0;
    buf_printf(o, "<conditionalFormatting sqref=\"%s\"><cfRule type=\"%s\"", sq, types[r->type]);
    if (has_dxf(r)) buf_printf(o, " dxfId=\"%d\"", dxf++);
    buf_printf(o, " priority=\"%d\"", i + 1);
    if (r->stop) buf_adds(o, " stopIfTrue=\"1\"");
    if (r->type == CF_CELL) buf_printf(o, " operator=\"%s\"", opsn[r->op < CO_N ? r->op : CO_EQ]);
    if (r->type == CF_TEXT || r->type == CF_NOTTEXT || r->type == CF_BEGINS || r->type == CF_ENDS) {
      buf_printf(o, " operator=\"%s\" text=\"", r->type == CF_TEXT ? "containsText" : r->type == CF_NOTTEXT ? "notContains" : r->type == CF_BEGINS ? "beginsWith" : "endsWith");
      xesc(o, r->text ? r->text : "");
      buf_adds(o, "\"");
    }
    if (r->type == CF_TOP) buf_printf(o, " rank=\"%ld\"%s%s", (long)r->rank, r->percent ? " percent=\"1\"" : "", r->bottom ? " bottom=\"1\"" : "");
    if (r->type == CF_AVERAGE) buf_printf(o, "%s%s", r->below ? " aboveAverage=\"0\"" : "", r->equal ? " equalAverage=\"1\"" : "");
    buf_adds(o, ">");
    switch (r->type) {
    case CF_CELL: case CF_EXPR:
      if (r->f1) { Buf f; buf_init(&f); file_formula(b, si, r->f1, &f); buf_adds(o, "<formula>"); xml_escape(o, f.data ? f.data : "", f.len); buf_adds(o, "</formula>"); buf_free(&f); }
      if (r->type == CF_CELL && r->f2 && (r->op == CO_BETWEEN || r->op == CO_NOTBETWEEN)) {
        Buf f; buf_init(&f); file_formula(b, si, r->f2, &f); buf_adds(o, "<formula>"); xml_escape(o, f.data ? f.data : "", f.len); buf_adds(o, "</formula>"); buf_free(&f);
      }
      break;
    case CF_TEXT: buf_adds(o, "<formula>NOT(ISERROR(SEARCH(&quot;"); xesc(o, q); buf_printf(o, "&quot;,%s)))</formula>", tl); break;
    case CF_NOTTEXT: buf_adds(o, "<formula>ISERROR(SEARCH(&quot;"); xesc(o, q); buf_printf(o, "&quot;,%s))</formula>", tl); break;
    case CF_BEGINS: buf_printf(o, "<formula>LEFT(%s,LEN(&quot;", tl); xesc(o, q); buf_adds(o, "&quot;))=&quot;"); xesc(o, q); buf_adds(o, "&quot;</formula>"); break;
    case CF_ENDS: buf_printf(o, "<formula>RIGHT(%s,LEN(&quot;", tl); xesc(o, q); buf_adds(o, "&quot;))=&quot;"); xesc(o, q); buf_adds(o, "&quot;</formula>"); break;
    case CF_BLANKS: buf_printf(o, "<formula>LEN(TRIM(%s))=0</formula>", tl); break;
    case CF_NOBLANKS: buf_printf(o, "<formula>LEN(TRIM(%s))&gt;0</formula>", tl); break;
    case CF_ERRORS: buf_printf(o, "<formula>ISERROR(%s)</formula>", tl); break;
    case CF_NOERRORS: buf_printf(o, "<formula>NOT(ISERROR(%s))</formula>", tl); break;
    case CF_BAR:
      buf_adds(o, "<dataBar><cfvo type=\"min\"/><cfvo type=\"max\"/>"); color_attr(o, "color", r->bar[0]); buf_adds(o, "</dataBar>");
      break;
    case CF_SCALE:
      buf_adds(o, "<colorScale><cfvo type=\"min\"/>");
      if (r->three) buf_adds(o, "<cfvo type=\"percentile\" val=\"50\"/>");
      buf_adds(o, "<cfvo type=\"max\"/>");
      color_attr(o, "color", r->bar[0]);
      if (r->three) color_attr(o, "color", r->bar[1]);
      color_attr(o, "color", r->bar[2]);
      buf_adds(o, "</colorScale>");
      break;
    }
    buf_adds(o, "</cfRule></conditionalFormatting>");
  }
}
static void write_styles(Book *b, Buf *o, int **fillmap_out, int **bordermap_out)
{
  int i, nfills = 2, nborders = 1;
  u32 *fills = (u32 *)calloc((size_t)b->nstyles + 2, sizeof(u32));
  int *fillmap = (int *)calloc((size_t)b->nstyles + 1, sizeof(int)), *bordermap = (int *)calloc((size_t)b->nstyles + 1, sizeof(int));
  int *bsrc = (int *)calloc((size_t)b->nstyles + 1, sizeof(int));
  buf_adds(o, "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
              "<styleSheet xmlns=\"http://schemas.openxmlformats.org/spreadsheetml/2006/main\">");
  if (b->nfmts) {
    buf_printf(o, "<numFmts count=\"%d\">", b->nfmts);
    for (i = 0; i < b->nfmts; i++) { buf_printf(o, "<numFmt numFmtId=\"%d\" formatCode=\"", 164 + i); xesc(o, b->fmts[i]); buf_adds(o, "\"/>"); }
    buf_adds(o, "</numFmts>");
  }
  buf_printf(o, "<fonts count=\"%d\">", b->nfonts);
  for (i = 0; i < b->nfonts; i++) {
    Font *f = &b->fonts[i];
    buf_adds(o, "<font>");
    if (f->bold) buf_adds(o, "<b/>");
    if (f->italic) buf_adds(o, "<i/>");
    if (f->strike) buf_adds(o, "<strike/>");
    if (f->underline) buf_adds(o, f->underline == 2 ? "<u val=\"double\"/>" : "<u/>");
    if (f->vert) buf_adds(o, f->vert == 1 ? "<vertAlign val=\"superscript\"/>" : "<vertAlign val=\"subscript\"/>");
    buf_printf(o, "<sz val=\"%g\"/>", f->size / 10.0);
    if (f->color != COLOR_AUTO) color_attr(o, "color", f->color);
    buf_adds(o, "<name val=\""); xesc(o, f->name); buf_adds(o, "\"/>");
    buf_adds(o, "</font>");
  }
  buf_adds(o, "</fonts>");
  /* fills: the two Excel requires, then one per distinct colour */
  if (fills && fillmap) {
    for (i = 0; i < b->nstyles; i++) {
      int k;
      u32 f = b->styles[i].fill;
      if (!f) { fillmap[i] = 0; continue; }
      for (k = 2; k < nfills; k++) if (fills[k] == f) break;
      if (k == nfills) fills[nfills++] = f;
      fillmap[i] = k;
    }
  }
  buf_printf(o, "<fills count=\"%d\"><fill><patternFill patternType=\"none\"/></fill><fill><patternFill patternType=\"gray125\"/></fill>", nfills);
  for (i = 2; i < nfills; i++) buf_printf(o, "<fill><patternFill patternType=\"solid\"><fgColor rgb=\"FF%06lX\"/><bgColor indexed=\"64\"/></patternFill></fill>", (unsigned long)(fills[i] & 0xFFFFFF));
  buf_adds(o, "</fills>");
  /* borders: one per style that has any (deduplicated by comparing with earlier ones) */
  if (bordermap && bsrc) {
    for (i = 0; i < b->nstyles; i++) {
      Style *s = &b->styles[i];
      int k, any = s->border[0] || s->border[1] || s->border[2] || s->border[3];
      if (!any) { bordermap[i] = 0; continue; }
      for (k = 1; k < nborders; k++) {
        Style *t = &b->styles[bsrc[k]];
        if (!memcmp(t->border, s->border, sizeof s->border) && !memcmp(t->bcolor, s->bcolor, sizeof s->bcolor)) break;
      }
      if (k == nborders) bsrc[nborders++] = i;
      bordermap[i] = k;
    }
  }
  buf_printf(o, "<borders count=\"%d\"><border><left/><right/><top/><bottom/><diagonal/></border>", nborders);
  for (i = 1; i < nborders; i++) {
    Style *s = &b->styles[bsrc[i]];
    static const char *const sides[4] = { "left", "right", "top", "bottom" };
    int k;
    buf_adds(o, "<border>");
    for (k = 0; k < 4; k++) {
      const char *bn = border_name(s->border[k]);
      if (!bn) { buf_printf(o, "<%s/>", sides[k]); continue; }
      buf_printf(o, "<%s style=\"%s\">", sides[k], bn);
      color_attr(o, "color", s->bcolor[k] ? s->bcolor[k] : COLOR_AUTO);
      buf_printf(o, "</%s>", sides[k]);
    }
    buf_adds(o, "<diagonal/></border>");
  }
  buf_adds(o, "</borders>");
  buf_adds(o, "<cellStyleXfs count=\"1\"><xf numFmtId=\"0\" fontId=\"0\" fillId=\"0\" borderId=\"0\"/></cellStyleXfs>");
  buf_printf(o, "<cellXfs count=\"%d\">", b->nstyles);
  for (i = 0; i < b->nstyles; i++) {
    Style *s = &b->styles[i];
    static const char *const ha[] = { NULL, "left", "center", "right", "fill", "justify", "centerContinuous" };
    static const char *const va[] = { NULL, "center", "top" };
    int align = s->halign || s->valign || s->wrap || s->indent || s->shrink || s->rotation;
    int prot = !s->locked || s->hidden;
    buf_printf(o, "<xf numFmtId=\"%d\" fontId=\"%d\" fillId=\"%d\" borderId=\"%d\" xfId=\"0\"", s->numfmt, s->font < b->nfonts ? s->font : 0,
               fillmap ? fillmap[i] : 0, bordermap ? bordermap[i] : 0);
    if (s->numfmt) buf_adds(o, " applyNumberFormat=\"1\"");
    if (s->font) buf_adds(o, " applyFont=\"1\"");
    if (s->fill) buf_adds(o, " applyFill=\"1\"");
    if (bordermap && bordermap[i]) buf_adds(o, " applyBorder=\"1\"");
    if (align) buf_adds(o, " applyAlignment=\"1\"");
    if (prot) buf_adds(o, " applyProtection=\"1\"");
    if (!align && !prot) { buf_adds(o, "/>"); continue; }
    buf_adds(o, ">");
    if (align) {
      buf_adds(o, "<alignment");
      if (s->halign && s->halign < 7) buf_printf(o, " horizontal=\"%s\"", ha[s->halign]);
      if (s->valign && s->valign < 3) buf_printf(o, " vertical=\"%s\"", va[s->valign]);
      if (s->wrap) buf_adds(o, " wrapText=\"1\"");
      if (s->shrink) buf_adds(o, " shrinkToFit=\"1\"");
      if (s->indent) buf_printf(o, " indent=\"%d\"", s->indent);
      if (s->rotation) buf_printf(o, " textRotation=\"%d\"", s->rotation > 0 ? s->rotation : 90 - s->rotation);
      buf_adds(o, "/>");
    }
    if (prot) buf_printf(o, "<protection locked=\"%d\" hidden=\"%d\"/>", s->locked ? 1 : 0, s->hidden ? 1 : 0);
    buf_adds(o, "</xf>");
  }
  buf_adds(o, "</cellXfs><cellStyles count=\"1\"><cellStyle name=\"Normal\" xfId=\"0\" builtinId=\"0\"/></cellStyles>");
  write_dxfs(b, o);
  buf_adds(o, "</styleSheet>");
  free(fills); free(bsrc);
  *fillmap_out = fillmap; *bordermap_out = bordermap;
}
/* shared strings: a simple hash to deduplicate */
typedef struct { char **s; long n, cap; long *hash; long hcap; } SST;
static unsigned long str_hash(const char *s) { unsigned long h = 5381; while (*s) h = h * 33 + (u8)*s++; return h; }
static long sst_add(SST *t, const char *s)
{
  unsigned long h;
  long i;
  if (t->n * 2 >= t->hcap) {
    long nc = t->hcap ? t->hcap * 2 : 1024, k;
    long *nh = (long *)malloc((size_t)nc * sizeof(long));
    if (!nh) return -1;
    for (k = 0; k < nc; k++) nh[k] = -1;
    for (k = 0; k < t->n; k++) { unsigned long hh = str_hash(t->s[k]) % (unsigned long)nc; while (nh[hh] >= 0) hh = (hh + 1) % (unsigned long)nc; nh[hh] = k; }
    free(t->hash); t->hash = nh; t->hcap = nc;
  }
  h = str_hash(s) % (unsigned long)t->hcap;
  while ((i = t->hash[h]) >= 0) { if (!strcmp(t->s[i], s)) return i; h = (h + 1) % (unsigned long)t->hcap; }
  if (t->n == t->cap) {
    long nc = t->cap ? t->cap * 2 : 256;
    char **ns = (char **)realloc(t->s, (size_t)nc * sizeof(char *));
    if (!ns) return -1;
    t->s = ns; t->cap = nc;
  }
  t->s[t->n] = (char *)s;              /* borrowed: the cells outlive the save */
  t->hash[h] = t->n;
  return t->n++;
}
static void write_sheet(Book *b, int si, SST *sst, Buf *o)
{
  Sheet *s = b->sh[si];
  i32 r, lr = sheet_last_row(s), lc = sheet_last_col(s);
  char ref[32];
  int i;
  buf_adds(o, "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
              "<worksheet xmlns=\"http://schemas.openxmlformats.org/spreadsheetml/2006/main\" "
              "xmlns:r=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships\">");
  if (s->tabcolor || page_needs_sheetpr(s)) {
    buf_adds(o, "<sheetPr>");
    if (s->tabcolor) buf_printf(o, "<tabColor rgb=\"FF%06lX\"/>", (unsigned long)(s->tabcolor & 0xFFFFFF));
    page_write_sheetpr(s, o);
    buf_adds(o, "</sheetPr>");
  }
  if (lr >= 0 && lc >= 0) {
    Rect d; d.r1 = 0; d.c1 = 0; d.r2 = lr; d.c2 = lc;
    rect_text(d, ref);
    buf_printf(o, "<dimension ref=\"%s\"/>", ref);
  }
  buf_printf(o, "<sheetViews><sheetView workbookViewId=\"0\"%s%s%s%s", si == b->active ? " tabSelected=\"1\"" : "",
             s->gridlines ? "" : " showGridLines=\"0\"", s->headings ? "" : " showRowColHeaders=\"0\"", s->showzeros ? "" : " showZeros=\"0\"");
  if (s->zoom && s->zoom != 100) buf_printf(o, " zoomScale=\"%d\"", s->zoom);
  page_write_view(s, o);
  buf_adds(o, ">");
  if (s->freezer || s->freezec) {
    ref_text(s->freezer, s->freezec, 0, 0, ref);
    buf_adds(o, "<pane");
    if (s->freezec) buf_printf(o, " xSplit=\"%ld\"", (long)s->freezec);
    if (s->freezer) buf_printf(o, " ySplit=\"%ld\"", (long)s->freezer);
    buf_printf(o, " topLeftCell=\"%s\" activePane=\"%s\" state=\"frozen\"/>", ref,
               s->freezer && s->freezec ? "bottomRight" : s->freezer ? "bottomLeft" : "topRight");
  }
  ref_text(s->cur_r, s->cur_c, 0, 0, ref);
  buf_printf(o, "<selection activeCell=\"%s\" sqref=\"%s\"/></sheetView></sheetViews>", ref, ref);
  buf_printf(o, "<sheetFormatPr defaultRowHeight=\"%g\"", s->defheight * 0.75);
  if (s->defwidth != 64) buf_printf(o, " defaultColWidth=\"%g\"", floor(s->defwidth / 7.0 * 256) / 256);
  buf_adds(o, "/>");
  /* columns: runs of equal settings */
  {
    i32 c = 0;
    int open = 0;
    while (c < s->ncols) {
      ColInfo *ci = &s->cols[c];
      i32 e = c;
      if (!ci->custom && !ci->hidden && !ci->style) { c++; continue; }
      while (e + 1 < s->ncols && !memcmp(&s->cols[e + 1], ci, sizeof *ci)) e++;
      if (!open) { buf_adds(o, "<cols>"); open = 1; }
      buf_printf(o, "<col min=\"%ld\" max=\"%ld\" width=\"%g\"%s%s", (long)c + 1, (long)e + 1,
                 floor((ci->width ? ci->width : s->defwidth) / 7.0 * 256 + 0.5) / 256, ci->custom ? " customWidth=\"1\"" : "", ci->hidden ? " hidden=\"1\"" : "");
      if (ci->style) buf_printf(o, " style=\"%d\"", ci->style);
      buf_adds(o, "/>");
      c = e + 1;
    }
    if (open) buf_adds(o, "</cols>");
  }
  buf_adds(o, "<sheetData>");
  for (r = 0; r < s->nrows; r++) {
    Row *row = s->rows[r];
    if (!row || (!row->n && !row->custom && !row->hidden && !row->filtered && !row->style)) continue;
    buf_printf(o, "<row r=\"%ld\"", (long)r + 1);
    if (row->custom && row->height) buf_printf(o, " ht=\"%g\" customHeight=\"1\"", row->height * 0.75);
    if (row->hidden || row->filtered) buf_adds(o, " hidden=\"1\"");
    if (row->style) buf_printf(o, " s=\"%d\" customFormat=\"1\"", row->style);
    buf_adds(o, ">");
    for (i = 0; i < row->n; i++) {
      Cell *c = &row->c[i];
      const Value *v = &c->v;
      ref_text(r, c->col, 0, 0, ref);
      buf_printf(o, "<c r=\"%s\"", ref);
      if (c->style) buf_printf(o, " s=\"%d\"", c->style);
      if (c->f) {
        if (v->t == V_STR) buf_adds(o, " t=\"str\"");
        else if (v->t == V_BOOL) buf_adds(o, " t=\"b\"");
        else if (v->t == V_ERR) buf_adds(o, " t=\"e\"");
        if (xlsx_dynamic(c->f)) buf_adds(o, " cm=\"1\"");
        if (xlsx_dynamic(c->f) || c->f->cse) {
          /* a dynamic array or {=...}: its range (the cells of a spill keep their values) */
          Rect ar;
          ar.r1 = r; ar.c1 = c->col;
          ar.r2 = r + (c->f->cse ? c->f->arows : c->f->sr) - 1; ar.c2 = c->col + (c->f->cse ? c->f->acols : c->f->sc) - 1;
          if (ar.r2 < r || c->f->spillblocked) ar.r2 = r;
          if (ar.c2 < c->col || c->f->spillblocked) ar.c2 = c->col;
          rect_text(ar, ref);
          buf_printf(o, "><f t=\"array\" ref=\"%s\">", ref);
        } else
        buf_adds(o, "><f>");
        {
          Buf ft;
          buf_init(&ft);
          table_home_r = r; table_home_c = c->col;    /* (structured references name their table) */
          file_formula(b, si, c->f, &ft);
          xml_escape(o, ft.data ? ft.data : "", ft.len);
          buf_free(&ft);
        }
        buf_adds(o, "</f>");
        if (v->t == V_NUM) buf_printf(o, "<v>%.17g</v>", v->n);
        else if (v->t == V_STR) { buf_adds(o, "<v>"); xesc(o, v->s); buf_adds(o, "</v>"); }
        else if (v->t == V_BOOL) buf_printf(o, "<v>%d</v>", v->e);
        else if (v->t == V_ERR) buf_printf(o, "<v>%s</v>", err_name(v->e));
        buf_adds(o, "</c>");
        continue;
      }
      switch (v->t) {
      case V_NUM: { char nb[40]; sprintf(nb, "%.17g", v->n); if (strtod(nb, NULL) != v->n) sprintf(nb, "%.17g", v->n); {
        char sh[40]; int p; for (p = 15; p <= 17; p++) { sprintf(sh, "%.*g", p, v->n); if (strtod(sh, NULL) == v->n) { strcpy(nb, sh); break; } } }
        buf_printf(o, "><v>%s</v></c>", nb); break; }
      case V_STR: { long k = sst_add(sst, v->s); if (k < 0) { buf_adds(o, " t=\"inlineStr\"><is><t>"); xesc(o, v->s); buf_adds(o, "</t></is></c>"); } else buf_printf(o, " t=\"s\"><v>%ld</v></c>", k); break; }
      case V_BOOL: buf_printf(o, " t=\"b\"><v>%d</v></c>", v->e); break;
      case V_ERR: buf_printf(o, " t=\"e\"><v>%s</v></c>", err_name(v->e)); break;
      default: buf_adds(o, "/>"); break;
      }
    }
    buf_adds(o, "</row>");
  }
  buf_adds(o, "</sheetData>");
  if (s->filter.r1 >= 0 && !tablexml_filter_in_table(s)) {
    /* the AutoFilter: its range and the values each filtered column lets through */
    rect_text(s->filter, ref);
    buf_printf(o, "<autoFilter ref=\"%s\"", ref);
    if (!s->nfcols) buf_adds(o, "/>");
    else {
      buf_adds(o, ">");
      for (i = 0; i < s->nfcols; i++) {
        const FilterCol *f = &s->fcols[i];
        int k;
        buf_printf(o, "<filterColumn colId=\"%ld\"><filters%s>", (long)(f->col - s->filter.c1), f->blanks ? " blank=\"1\"" : "");
        for (k = 0; k < f->n; k++) { buf_adds(o, "<filter val=\""); xesc(o, f->vals[k]); buf_adds(o, "\"/>"); }
        buf_adds(o, "</filters></filterColumn>");
      }
      buf_adds(o, "</autoFilter>");
    }
  }
  page_write_sheet_views(b, si, o);
  if (s->nmerges) {
    buf_printf(o, "<mergeCells count=\"%d\">", s->nmerges);
    for (i = 0; i < s->nmerges; i++) { rect_text(s->merges[i], ref); buf_printf(o, "<mergeCell ref=\"%s\"/>", ref); }
    buf_adds(o, "</mergeCells>");
  }
  {
    int dxf = 0, k;
    for (k = 0; k < si; k++) for (i = 0; i < b->sh[k]->ncfs; i++) dxf += has_dxf(&b->sh[k]->cfs[i]);
    write_cond(b, si, dxf, o);
  }
  if (s->nlinks) {
    /* links: out through the sheet's relationships (rIdL1...), or to a place in the workbook */
    buf_adds(o, "<hyperlinks>");
    for (i = 0; i < s->nlinks; i++) {
      const Link *l = &s->links[i];
      ref_text(l->r, l->c, 0, 0, ref);
      buf_printf(o, "<hyperlink ref=\"%s\"", ref);
      if (l->target[0] == '#') { buf_adds(o, " location=\""); xesc(o, l->target + 1); buf_adds(o, "\""); }
      else buf_printf(o, " r:id=\"rIdL%d\"", i + 1);
      if (l->tip && l->tip[0]) { buf_adds(o, " tooltip=\""); xesc(o, l->tip); buf_adds(o, "\""); }
      buf_adds(o, "/>");
    }
    buf_adds(o, "</hyperlinks>");
  }
  page_write_sheet(s, o);
  if (s->nnotes) buf_adds(o, "<legacyDrawing r:id=\"rIdN2\"/>");   /* (the notes' boxes, for Excel) */
  tablexml_sheet_parts(b, si, o);
  buf_adds(o, "</worksheet>");
}
/* a sheet's notes: the comments part, and the VML drawing of their boxes Excel needs to show them */
static void write_comments(Book *b, int si, Buf *o)
{
  Sheet *s = b->sh[si];
  char **au = (char **)calloc((size_t)s->nnotes + 1, sizeof(char *));
  int na = 0, i, k;
  buf_adds(o, "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
              "<comments xmlns=\"http://schemas.openxmlformats.org/spreadsheetml/2006/main\"><authors>");
  char (*tc)[64] = (char (*)[64])calloc((size_t)s->nnotes + 1, 64);
  Buf lt;
  buf_init(&lt);
  /* threaded comments: their author is the thread's id, their text a summary (comments.c) */
  for (i = 0; i < s->nnotes && tc; i++) if (s->notes[i].threaded) { lt.len = 0; cm_legacy(&s->notes[i], si, i, &lt, tc[i], 64); }
  for (i = 0; i < s->nnotes && au; i++) {
    const char *a = tc && tc[i][0] ? tc[i] : s->notes[i].author ? s->notes[i].author : "";
    for (k = 0; k < na && strcmp(au[k], a); k++) ;
    if (k == na) { au[na++] = (char *)a; buf_adds(o, "<author>"); xesc(o, a); buf_adds(o, "</author>"); }
  }
  buf_adds(o, "</authors><commentList>");
  for (i = 0; i < s->nnotes; i++) {
    const Note *n = &s->notes[i];
    const char *a = tc && tc[i][0] ? tc[i] : n->author ? n->author : "";
    char ref[16];
    for (k = 0; au && k < na && strcmp(au[k], a); k++) ;
    ref_text(n->r, n->c, 0, 0, ref);
    buf_printf(o, "<comment ref=\"%s\" authorId=\"%d\"><text>", ref, au ? k : 0);
    if (tc && tc[i][0]) { lt.len = 0; cm_legacy(n, si, i, &lt, tc[i], 64); buf_add(o, lt.data, lt.len); buf_adds(o, "</text></comment>"); continue; }
    /* as Excel writes them: the author in bold, then the text on the next line */
    if (a[0]) { buf_adds(o, "<r><rPr><b/><sz val=\"9\"/><rFont val=\"Tahoma\"/></rPr><t>"); xesc(o, a); buf_adds(o, ":</t></r>"); }
    buf_adds(o, "<r><rPr><sz val=\"9\"/><rFont val=\"Tahoma\"/></rPr><t xml:space=\"preserve\">");
    if (a[0]) buf_adds(o, "&#10;");
    xesc(o, n->text ? n->text : "");
    buf_adds(o, "</t></r></text></comment>");
  }
  buf_adds(o, "</commentList></comments>");
  free(au); free(tc);
  buf_free(&lt);
}
static void write_vml(Book *b, int si, Buf *o)
{
  Sheet *s = b->sh[si];
  int i;
  buf_printf(o, "<xml xmlns:v=\"urn:schemas-microsoft-com:vml\" xmlns:o=\"urn:schemas-microsoft-com:office:office\" xmlns:x=\"urn:schemas-microsoft-com:office:excel\">"
                "<o:shapelayout v:ext=\"edit\"><o:idmap v:ext=\"edit\" data=\"%d\"/></o:shapelayout>"
                "<v:shapetype id=\"_x0000_t202\" coordsize=\"21600,21600\" o:spt=\"202\" path=\"m,l,21600r21600,l21600,xe\">"
                "<v:stroke joinstyle=\"miter\"/><v:path gradientshapeok=\"t\" o:connecttype=\"rect\"/></v:shapetype>", si + 1);
  for (i = 0; i < s->nnotes; i++) {
    const Note *n = &s->notes[i];
    buf_printf(o, "<v:shape id=\"_x0000_s%d\" type=\"#_x0000_t202\" style=\"position:absolute;margin-left:59.25pt;margin-top:1.5pt;width:108pt;height:59.25pt;z-index:%d;visibility:hidden\" "
                  "fillcolor=\"#ffffe1\" o:insetmode=\"auto\"><v:fill color2=\"#ffffe1\"/><v:shadow on=\"t\" color=\"black\" obscured=\"t\"/>"
                  "<v:path o:connecttype=\"none\"/><v:textbox style=\"mso-direction-alt:auto\"><div style=\"text-align:left\"></div></v:textbox>"
                  "<x:ClientData ObjectType=\"Note\"><x:MoveWithCells/><x:SizeWithCells/><x:Anchor>%ld, 15, %ld, 2, %ld, 15, %ld, 16</x:Anchor>"
                  "<x:AutoFill>False</x:AutoFill><x:Row>%ld</x:Row><x:Column>%ld</x:Column></x:ClientData></v:shape>",
               1024 * (si + 1) + i + 1, i + 1, (long)n->c + 1, (long)n->r, (long)n->c + 3, (long)n->r + 4, (long)n->r, (long)n->c);
  }
  buf_adds(o, "</xml>");
}
/* a sheet's relationships (its notes), or nothing if it has none */
static int write_sheet_rels(Book *b, int si, Buf *o)
{
  Sheet *s = b->sh[si];
  int i, out = 0;
  for (i = 0; i < s->nlinks; i++) if (s->links[i].target[0] != '#') out = 1;
  if (!s->nnotes && !out && !tablexml_has_rels(b, si)) return 0;
  buf_adds(o, "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
              "<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">");
  if (s->nnotes)
    buf_printf(o, "<Relationship Id=\"rIdN1\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/comments\" Target=\"../comments%d.xml\"/>"
                  "<Relationship Id=\"rIdN2\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/vmlDrawing\" Target=\"../drawings/vmlDrawing%d.vml\"/>", si + 1, si + 1);
  cm_sheet_rels(s, si, o);
  for (i = 0; i < s->nlinks; i++) {
    if (s->links[i].target[0] == '#') continue;
    buf_printf(o, "<Relationship Id=\"rIdL%d\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/hyperlink\" Target=\"", i + 1);
    xesc(o, s->links[i].target);
    buf_adds(o, "\" TargetMode=\"External\"/>");
  }
  tablexml_sheet_rels(b, si, o);
  buf_adds(o, "</Relationships>");
  return 1;
}
u8 *xlsx_save(Book *b, size_t *len)
{
  ZipWriter w;
  Buf o;
  SST sst;
  int i, *fillmap = NULL, *bordermap = NULL, dynamic = book_has_dynamic(b);
  char path[64];
  memset(&sst, 0, sizeof sst);
  table_file_mode = 1;     /* structured references in their file form */
  table_home_r = table_home_c = -1;
  zip_begin(&w);
  buf_init(&o);
  /* content types */
  buf_adds(&o, "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
               "<Types xmlns=\"http://schemas.openxmlformats.org/package/2006/content-types\">"
               "<Default Extension=\"rels\" ContentType=\"application/vnd.openxmlformats-package.relationships+xml\"/>"
               "<Default Extension=\"xml\" ContentType=\"application/xml\"/>"
               "<Override PartName=\"/xl/workbook.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.spreadsheetml.sheet.main+xml\"/>"
               "<Override PartName=\"/xl/styles.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.spreadsheetml.styles+xml\"/>"
               "<Override PartName=\"/xl/sharedStrings.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.spreadsheetml.sharedStrings+xml\"/>"
               "<Override PartName=\"/docProps/core.xml\" ContentType=\"application/vnd.openxmlformats-package.core-properties+xml\"/>"
               "<Override PartName=\"/docProps/app.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.extended-properties+xml\"/>"
               "<Override PartName=\"/xl/theme/theme1.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.theme+xml\"/>");
  for (i = 0; i < b->nsh; i++) {
    buf_printf(&o, "<Override PartName=\"/xl/worksheets/sheet%d.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.spreadsheetml.worksheet+xml\"/>", i + 1);
    if (b->sh[i]->nnotes) buf_printf(&o, "<Override PartName=\"/xl/comments%d.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.spreadsheetml.comments+xml\"/>", i + 1);
  }
  for (i = 0; i < b->nsh && !b->sh[i]->nnotes; i++) ;
  if (i < b->nsh) buf_adds(&o, "<Default Extension=\"vml\" ContentType=\"application/vnd.openxmlformats-officedocument.vmlDrawing\"/>");
  tablexml_content_types(b, &o);
  cm_types(b, &o);
  if (dynamic) buf_adds(&o, "<Override PartName=\"/xl/metadata.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.spreadsheetml.sheetMetadata+xml\"/>");
  buf_adds(&o, "</Types>");
  zip_add(&w, "[Content_Types].xml", o.data, o.len);
  o.len = 0;
  buf_adds(&o, "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
               "<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">"
               "<Relationship Id=\"rId1\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/officeDocument\" Target=\"xl/workbook.xml\"/>"
               "<Relationship Id=\"rId2\" Type=\"http://schemas.openxmlformats.org/package/2006/relationships/metadata/core-properties\" Target=\"docProps/core.xml\"/>"
               "<Relationship Id=\"rId3\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/extended-properties\" Target=\"docProps/app.xml\"/>"
               "</Relationships>");
  zip_add(&w, "_rels/.rels", o.data, o.len);
  o.len = 0;
  buf_adds(&o, "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
               "<cp:coreProperties xmlns:cp=\"http://schemas.openxmlformats.org/package/2006/metadata/core-properties\" "
               "xmlns:dc=\"http://purl.org/dc/elements/1.1/\" xmlns:dcterms=\"http://purl.org/dc/terms/\" "
               "xmlns:xsi=\"http://www.w3.org/2001/XMLSchema-instance\">");
  if (b->title[0]) { buf_adds(&o, "<dc:title>"); xesc(&o, b->title); buf_adds(&o, "</dc:title>"); }
  if (b->author[0]) { buf_adds(&o, "<dc:creator>"); xesc(&o, b->author); buf_adds(&o, "</dc:creator>"); }
  page_write_core(b, &o);
  buf_adds(&o, "</cp:coreProperties>");
  zip_add(&w, "docProps/core.xml", o.data, o.len);
  o.len = 0;
  buf_adds(&o, "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
               "<Properties xmlns=\"http://schemas.openxmlformats.org/officeDocument/2006/extended-properties\"><Application>Ledger</Application>");
  if (b->lx && b->lx->manager[0]) { buf_adds(&o, "<Manager>"); xesc(&o, b->lx->manager); buf_adds(&o, "</Manager>"); }
  if (b->lx && b->lx->company[0]) { buf_adds(&o, "<Company>"); xesc(&o, b->lx->company); buf_adds(&o, "</Company>"); }
  buf_adds(&o, "</Properties>");
  zip_add(&w, "docProps/app.xml", o.data, o.len);
  /* workbook */
  o.len = 0;
  buf_adds(&o, "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
               "<workbook xmlns=\"http://schemas.openxmlformats.org/spreadsheetml/2006/main\" "
               "xmlns:r=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships\">");
  if (b->date1904) buf_adds(&o, "<workbookPr date1904=\"1\"/>");
  buf_printf(&o, "<bookViews><workbookView activeTab=\"%d\"/></bookViews><sheets>", b->active);
  for (i = 0; i < b->nsh; i++) {
    buf_adds(&o, "<sheet name=\""); xesc(&o, b->sh[i]->name);
    buf_printf(&o, "\" sheetId=\"%d\"%s r:id=\"rId%d\"/>", i + 1, b->sh[i]->hidden ? " state=\"hidden\"" : "", i + 1);
  }
  buf_adds(&o, "</sheets>");
  if (b->nnames || page_has_names(b)) {
    buf_adds(&o, "<definedNames>");
    page_write_names(b, &o);
    for (i = 0; i < b->nnames; i++) {
      Name *n = &b->names[i];
      Buf ft;
      buf_adds(&o, "<definedName name=\""); xesc(&o, n->name); buf_adds(&o, "\"");
      if (n->sheet != SHEET_SELF) buf_printf(&o, " localSheetId=\"%d\"", n->sheet);
      buf_adds(&o, ">");
      buf_init(&ft);
      if (n->f) file_formula(b, n->sheet == SHEET_SELF ? -1 : n->sheet, n->f, &ft);
      else if (n->formula_text) buf_adds(&ft, n->formula_text);
      xml_escape(&o, ft.data ? ft.data : "", ft.len);
      buf_free(&ft);
      buf_adds(&o, "</definedName>");
    }
    buf_adds(&o, "</definedNames>");
  }
  buf_adds(&o, "<calcPr calcId=\"191029\"/>");
  tablexml_workbook(b, &o);
  buf_adds(&o, "<calcPr calcId=\"191029\"");
  page_write_calcpr(b, &o);
  buf_adds(&o, "/>");
  page_write_book_views(b, &o);
  buf_adds(&o, "</workbook>");
  zip_add(&w, "xl/workbook.xml", o.data, o.len);
  o.len = 0;
  buf_adds(&o, "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
               "<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">");
  for (i = 0; i < b->nsh; i++)
    buf_printf(&o, "<Relationship Id=\"rId%d\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/worksheet\" Target=\"worksheets/sheet%d.xml\"/>", i + 1, i + 1);
  buf_printf(&o, "<Relationship Id=\"rId%d\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/styles\" Target=\"styles.xml\"/>", b->nsh + 1);
  buf_printf(&o, "<Relationship Id=\"rId%d\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/sharedStrings\" Target=\"sharedStrings.xml\"/>", b->nsh + 2);
  tablexml_workbook_rels(b, &o);
  buf_printf(&o, "<Relationship Id=\"rId%d\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/theme\" Target=\"theme/theme1.xml\"/>", b->nsh + 3);
  cm_book_rels(b, &o, b->nsh + 4);
  if (dynamic) buf_adds(&o, "<Relationship Id=\"rIdMeta1\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/sheetMetadata\" Target=\"metadata.xml\"/>");
  buf_adds(&o, "</Relationships>");
  zip_add(&w, "xl/_rels/workbook.xml.rels", o.data, o.len);
  /* sheets (they fill the string table) */
  for (i = 0; i < b->nsh; i++) {
    o.len = 0;
    write_sheet(b, i, &sst, &o);
    sprintf(path, "xl/worksheets/sheet%d.xml", i + 1);
    zip_add(&w, path, o.data, o.len);
    o.len = 0;
    if (write_sheet_rels(b, i, &o)) { sprintf(path, "xl/worksheets/_rels/sheet%d.xml.rels", i + 1); zip_add(&w, path, o.data, o.len); }
    if (b->sh[i]->nnotes) {
      o.len = 0; write_comments(b, i, &o); sprintf(path, "xl/comments%d.xml", i + 1); zip_add(&w, path, o.data, o.len);
      o.len = 0; write_vml(b, i, &o); sprintf(path, "xl/drawings/vmlDrawing%d.vml", i + 1); zip_add(&w, path, o.data, o.len);
    }
  }
  tablexml_parts(b, &w, file_formula);
  table_home_r = table_home_c = -1;
  cm_parts(b, &w);
  o.len = 0;
  page_write_theme(b, &o);
  zip_add(&w, "xl/theme/theme1.xml", o.data, o.len);
  o.len = 0;
  write_styles(b, &o, &fillmap, &bordermap);
  zip_add(&w, "xl/styles.xml", o.data, o.len);
  o.len = 0;
  buf_printf(&o, "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
                 "<sst xmlns=\"http://schemas.openxmlformats.org/spreadsheetml/2006/main\" count=\"%ld\" uniqueCount=\"%ld\">", sst.n, sst.n);
  for (i = 0; i < sst.n; i++) {
    const char *s = sst.s[i];
    int keep = s[0] == ' ' || (s[0] && s[strlen(s) - 1] == ' ') || strchr(s, '\n');
    buf_adds(&o, keep ? "<si><t xml:space=\"preserve\">" : "<si><t>");
    xesc(&o, s);
    buf_adds(&o, "</t></si>");
  }
  buf_adds(&o, "</sst>");
  zip_add(&w, "xl/sharedStrings.xml", o.data, o.len);
  if (dynamic) {
    /* cm="1" points here: Excel's dynamic array properties */
    o.len = 0;
    buf_adds(&o, "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
                 "<metadata xmlns=\"http://schemas.openxmlformats.org/spreadsheetml/2006/main\" xmlns:xda=\"http://schemas.microsoft.com/office/spreadsheetml/2017/dynamicarray\">"
                 "<metadataTypes count=\"1\"><metadataType name=\"XLDAPR\" minSupportedVersion=\"120000\" copy=\"1\" pasteAll=\"1\" pasteValues=\"1\" merge=\"1\" "
                 "splitFirst=\"1\" rowColShift=\"1\" clearFormats=\"1\" clearComments=\"1\" assign=\"1\" coerce=\"1\" cellMeta=\"1\"/></metadataTypes>"
                 "<futureMetadata name=\"XLDAPR\" count=\"1\"><bk><extLst><ext uri=\"{bdbb8cdc-fa1e-496e-a857-3c3f30c029c3}\">"
                 "<xda:dynamicArrayProperties fDynamic=\"1\" fCollapsed=\"0\"/></ext></extLst></bk></futureMetadata>"
                 "<cellMetadata count=\"1\"><bk><rc t=\"1\" v=\"0\"/></bk></cellMetadata></metadata>");
    zip_add(&w, "xl/metadata.xml", o.data, o.len);
  }
  free(fillmap); free(bordermap);
  free(sst.s); free(sst.hash);
  table_file_mode = 0;
  if (o.oom || !zip_finish(&w)) { buf_free(&o); zip_free(&w); return NULL; }
  buf_free(&o);
  *len = w.out.len;
  {
    u8 *res = (u8 *)buf_take(&w.out);
    zip_free(&w);
    return res;
  }
}

/* ------------------------------------------------------------------ CSV */
Book *csv_load(const char *data, size_t len, char delim)
{
  Book *b = book_new();
  size_t i = 0;
  i32 r = 0, c = 0;
  Buf f;
  int saved_manual;
  if (!b) return NULL;
  if (len >= 3 && (u8)data[0] == 0xEF && (u8)data[1] == 0xBB && (u8)data[2] == 0xBF) i = 3;      /* UTF-8 mark */
  if (!delim) {
    /* the most frequent of , ; tab on the first line, outside quotes */
    int counts[3] = { 0, 0, 0 }, q = 0;
    size_t k;
    for (k = i; k < len && (q || (data[k] != '\n' && data[k] != '\r')); k++) {
      if (data[k] == '"') q = !q;
      else if (!q) counts[data[k] == ',' ? 0 : data[k] == ';' ? 1 : data[k] == '\t' ? 2 : 0] += data[k] == ',' || data[k] == ';' || data[k] == '\t';
    }
    delim = counts[1] > counts[0] && counts[1] >= counts[2] ? ';' : counts[2] > counts[0] ? '\t' : ',';
  }
  saved_manual = b->manualcalc;
  b->manualcalc = 1;
  buf_init(&f);
  while (i <= len) {
    int quoted = 0, end_line = 0;
    f.len = 0;
    if (f.data) f.data[0] = 0;
    if (i < len && data[i] == '"') {
      quoted = 1;
      i++;
      while (i < len) {
        if (data[i] == '"') { if (i + 1 < len && data[i + 1] == '"') { buf_addc(&f, '"'); i += 2; continue; } i++; break; }
        buf_addc(&f, data[i++]);
      }
    }
    while (i < len && data[i] != delim && data[i] != '\n' && data[i] != '\r') buf_addc(&f, data[i++]);
    if (i >= len) end_line = 2;
    else if (data[i] == delim) i++;
    else { if (data[i] == '\r' && i + 1 < len && data[i + 1] == '\n') i++; i++; end_line = 1; }
    if (f.len || quoted) {
      const char *t = f.data ? f.data : "";
      if (quoted) {
        /* quoted fields are text unless they read as numbers */
        double d;
        int k;
        if (parse_number(t, &d, &k)) book_input(b, 0, r, c, t, NULL);
        else { Value v = val_str(t); book_set_value(b, 0, r, c, &v); val_free(&v); }
      } else book_input(b, 0, r, c, t[0] == '=' ? t : t, NULL);
    }
    c++;
    if (end_line) { r++; c = 0; if (r >= MAXROW) break; }
    if (end_line == 2) break;
  }
  buf_free(&f);
  b->manualcalc = saved_manual;
  {
    extern void book_forget_undo(Book *b);
    book_forget_undo(b);
  }
  book_recalc(b);
  b->dirty = 0;
  return b;
}
char *csv_save(Book *b, int sheet, char delim)
{
  Sheet *s;
  Buf o;
  i32 r, lr, lc, c;
  if (sheet < 0 || sheet >= b->nsh) return NULL;
  s = b->sh[sheet];
  lr = sheet_last_row(s); lc = sheet_last_col(s);
  if (!delim) delim = ',';
  buf_init(&o);
  for (r = 0; r <= lr; r++) {
    for (c = 0; c <= lc; c++) {
      const Value *v = cell_value(b, sheet, r, c);
      Buf t;
      const char *p;
      int quote = 0;
      if (c) buf_addc(&o, delim);
      buf_init(&t);
      if (v->t == V_NUM) {
        int st = sheet_cell_style(s, r, c);
        const char *code = book_fmt_code(b, b->styles[st].numfmt);
        if (fmt_is_date(code)) { char out[128]; u32 col; fmt_value(b, code, v, out, sizeof out, &col, 0); buf_adds(&t, out); }
        else to_text(b, v, &t);
      } else to_text(b, v, &t);
      for (p = t.data ? t.data : ""; *p; p++) if (*p == delim || *p == '"' || *p == '\n' || *p == '\r') quote = 1;
      if (quote) {
        buf_addc(&o, '"');
        for (p = t.data; *p; p++) { if (*p == '"') buf_addc(&o, '"'); buf_addc(&o, *p); }
        buf_addc(&o, '"');
      } else if (t.data) buf_adds(&o, t.data);
      buf_free(&t);
    }
    buf_addc(&o, '\n');
  }
  return buf_take(&o);
}
