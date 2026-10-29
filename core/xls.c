/*
 * xls.c - Excel 97-2003 workbooks (.xls) read (see xls.h): the compound document (OLE2 / CFB)
 * holding the "Workbook" stream, and its BIFF8 records: sheets, shared strings, cells (numbers, RK,
 * text, booleans, errors), formulas turned from their RPN tokens back into formula text, number
 * formats, fonts, fills, borders and alignment (XF), column widths and row heights, merged cells,
 * frozen panes, the outline, defined names and sheet protection.
 *
 * Every offset and length comes from a hostile file: each read is checked, chains are bounded, and
 * whatever can't be understood is skipped (a formula Ledger can't express keeps its last value).
 */
#include "xls.h"
#include "data.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>

/* ------------------------------------------------------------------ little-endian reads */
static u32 rd16(const u8 *p) { return (u32)p[0] | ((u32)p[1] << 8); }
static u32 rd32(const u8 *p) { return (u32)p[0] | ((u32)p[1] << 8) | ((u32)p[2] << 16) | ((u32)p[3] << 24); }
static double rddbl(const u8 *p)
{
  union { double d; u8 b[8]; } u;
  int i;
  static const union { u16 x; u8 b[2]; } e = { 1 };
  for (i = 0; i < 8; i++) u.b[e.b[0] ? i : 7 - i] = p[i];      /* the file is little-endian; the 68k isn't */
  return u.d;
}

/* ------------------------------------------------------------------ the compound document */
typedef struct { const u8 *d; size_t len; u32 shift, mshift, cutoff; u32 *fat; u32 nfat; u32 *mfat; u32 nmfat; u8 *mini; size_t nmini; } Cfb;
#define ENDCHAIN 0xFFFFFFFEUL
static const u8 *sector(const Cfb *c, u32 n)
{
  size_t off = ((size_t)n + 1) << c->shift;
  if (n >= 0xFFFFFFFAUL || off + ((size_t)1 << c->shift) > c->len) return NULL;
  return c->d + off;
}
/* a chain of big sectors read into a malloc'd buffer of up to max bytes (*got its length) */
static u8 *chain(const Cfb *c, u32 start, size_t max, size_t *got)
{
  size_t ss = (size_t)1 << c->shift, n = 0, cap = 0, steps = 0;
  u8 *out = NULL;
  u32 s = start;
  while (s != ENDCHAIN && n < max && steps++ <= c->len / ss + 1) {
    const u8 *p = sector(c, s);
    if (!p) break;
    if (n + ss > cap) {
      size_t nc = cap ? cap * 2 : ss * 8;
      u8 *no = (u8 *)realloc(out, nc);
      if (!no) break;
      out = no; cap = nc;
    }
    memcpy(out + n, p, ss); n += ss;
    if (s >= c->nfat) break;
    s = c->fat[s];
  }
  *got = n < max ? n : max;
  return out;
}
static int cfb_open(Cfb *c, const u8 *d, size_t len)
{
  static const u8 sig[8] = { 0xD0, 0xCF, 0x11, 0xE0, 0xA1, 0xB1, 0x1A, 0xE1 };
  u32 nfatsec, difat, ndifat, i, k, *secs = NULL, nsecs = 0;
  size_t ss;
  memset(c, 0, sizeof *c);
  if (len < 512 || memcmp(d, sig, 8)) return 0;
  c->d = d; c->len = len;
  c->shift = rd16(d + 30); c->mshift = rd16(d + 32); c->cutoff = rd32(d + 56);
  if (c->shift < 7 || c->shift > 16 || c->mshift < 4 || c->mshift > c->shift) return 0;
  ss = (size_t)1 << c->shift;
  nfatsec = rd32(d + 44); difat = rd32(d + 68); ndifat = rd32(d + 72);
  if (nfatsec > len / ss + 1) return 0;
  /* the FAT's sectors: 109 in the header, the rest in the DIFAT chain */
  if (!(secs = (u32 *)malloc(((size_t)nfatsec + 1) * sizeof(u32)))) return 0;
  for (i = 0; i < 109 && nsecs < nfatsec; i++) secs[nsecs++] = rd32(d + 76 + i * 4);
  for (k = 0; k < ndifat && nsecs < nfatsec && difat < 0xFFFFFFFAUL; k++) {
    const u8 *p = sector(c, difat);
    if (!p) break;
    for (i = 0; i < ss / 4 - 1 && nsecs < nfatsec; i++) secs[nsecs++] = rd32(p + i * 4);
    difat = rd32(p + ss - 4);
  }
  c->nfat = (u32)(nsecs * (ss / 4));
  if (!(c->fat = (u32 *)malloc((size_t)c->nfat * sizeof(u32) + 4))) { free(secs); return 0; }
  for (i = 0; i < nsecs; i++) {
    const u8 *p = sector(c, secs[i]);
    for (k = 0; k < ss / 4; k++) c->fat[i * (ss / 4) + k] = p ? rd32(p + k * 4) : ENDCHAIN;
  }
  free(secs);
  /* the mini FAT */
  {
    size_t got;
    u8 *m = rd32(d + 64) ? chain(c, rd32(d + 60), (size_t)rd32(d + 64) * ss, &got) : NULL;
    if (m) {
      c->nmfat = (u32)(got / 4);
      if ((c->mfat = (u32 *)malloc((size_t)c->nmfat * sizeof(u32) + 4))) for (i = 0; i < c->nmfat; i++) c->mfat[i] = rd32(m + i * 4);
      else c->nmfat = 0;
      free(m);
    }
  }
  return 1;
}
static void cfb_close(Cfb *c) { free(c->fat); free(c->mfat); free(c->mini); memset(c, 0, sizeof *c); }
/* a stream by name (ASCII, compared ignoring case): malloc'd bytes, *n its length; NULL if absent */
static u8 *cfb_stream(Cfb *c, const char *want, size_t *n)
{
  size_t dl, i;
  u8 *dir = chain(c, rd32(c->d + 48), 1u << 22, &dl);
  u8 *out = NULL;
  if (!dir) return NULL;
  for (i = 0; i + 128 <= dl; i += 128) {
    const u8 *e = dir + i;
    char name[32];
    u32 nl = rd16(e + 64), k, start = rd32(e + 116), size = rd32(e + 120);
    int type = e[66];
    if (type == 5 && !c->mini) {
      /* the root: the mini stream */
      c->mini = chain(c, start, size, &c->nmini);
    }
    if (type != 2 || nl < 2 || nl > 64) continue;
    for (k = 0; k < nl / 2 - 1 && k < sizeof name - 1; k++) name[k] = (char)e[k * 2];
    name[k] = 0;
    if (!str_ieq(name, want)) continue;
    if (size < c->cutoff) {
      /* in the mini stream: 64-byte sectors chained through the mini FAT */
      size_t ms = (size_t)1 << c->mshift, got = 0, steps = 0;
      u32 s = start;
      if (!c->mini) {
        /* the root entry comes first, but be safe */
        size_t j;
        for (j = 0; j + 128 <= dl; j += 128) if (dir[j + 66] == 5) { c->mini = chain(c, rd32(dir + j + 116), rd32(dir + j + 120), &c->nmini); break; }
      }
      if (!c->mini || !(out = (u8 *)malloc((size_t)size + 1))) break;
      while (s != ENDCHAIN && got < size && steps++ <= c->nmfat) {
        size_t off = (size_t)s * ms, take = size - got < ms ? size - got : ms;
        if (off + take > c->nmini) break;
        memcpy(out + got, c->mini + off, take); got += take;
        if (s >= c->nmfat) break;
        s = c->mfat[s];
      }
      *n = got;
    } else out = chain(c, start, size, n);
    break;
  }
  free(dir);
  return out;
}

/* ------------------------------------------------------------------ the reader's state */
typedef struct { char name[32]; u32 off; int vis, type; int book; } SheetEnt;
typedef struct { int sup; int first, last; } Xti;
typedef struct { i32 r1, r2, c1, c2; u8 *rgce; u32 cce; u8 *extra; u32 nextra; } Shared;
typedef struct {
  Book *b;
  const u8 *s; size_t len;                /* the Workbook stream */
  char **sst; long nsst;
  SheetEnt *sh; int nsh;
  Xti *xti; int nxti;
  int nsup, selfsup;                       /* SUPBOOK records seen; the one that is this workbook */
  char **extnames; int *extsup; int nextn; /* EXTERNNAMEs (add-in functions), with their SUPBOOK */
  char **names; int nnames;                /* defined names (for ptgName) */
  u16 *xfstyle; int nxf;                   /* XF -> Ledger style */
  int *fontmap; int nfont;                 /* FONT -> Ledger font */
  struct { u16 id; char *code; } *fmts; int nfmt;
  u32 pal[64];
} Rd;

/* a record at offset o: its id and data; 0 at the end */
static int rec(const Rd *rd, size_t o, u32 *id, const u8 **data, u32 *len)
{
  if (o + 4 > rd->len) return 0;
  *id = rd16(rd->s + o); *len = rd16(rd->s + o + 2);
  if (o + 4 + *len > rd->len) return 0;
  *data = rd->s + o + 4;
  return 1;
}

/* ------------------------------------------------------------------ strings */
/* UTF-16 units or Latin-1 bytes appended as UTF-8 */
static void put_chars(Buf *o, const u8 *p, u32 n, int wide)
{
  u32 i;
  char u[8];
  for (i = 0; i < n; i++) {
    u32 cp = wide ? rd16(p + i * 2) : p[i];
    if (wide && cp >= 0xD800 && cp < 0xDC00 && i + 1 < n) { u32 lo = rd16(p + (i + 1) * 2); if (lo >= 0xDC00 && lo < 0xE000) { cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00); i++; } }
    buf_add(o, u, (size_t)utf8_put(u, cp ? cp : 1));      /* (NUL kept as \001: a list's separator) */
  }
}
/* an XLUnicodeString (cch of lenbytes bytes, then flags) at p within a record of n bytes: malloc'd
   UTF-8, *used the bytes taken */
static char *xlstr(const u8 *p, u32 n, int lenbytes, u32 *used)
{
  u32 cch, flags, at;
  Buf o;
  if (n < (u32)lenbytes + 1) { *used = n; return str_dup(""); }
  cch = lenbytes == 1 ? p[0] : rd16(p);
  flags = p[lenbytes];
  at = (u32)lenbytes + 1;
  if (flags & 8) at += 2;
  if (flags & 4) at += 4;
  buf_init(&o);
  if (at + cch * ((flags & 1) ? 2 : 1) > n) cch = (n - (at < n ? at : n)) / ((flags & 1) ? 2 : 1);
  put_chars(&o, p + at, cch, flags & 1);
  *used = at + cch * ((flags & 1) ? 2 : 1);
  if (!o.data) return str_dup("");
  return buf_take(&o);
}
/* the SST: its strings across CONTINUE records (a string's characters may break at a record end,
   the next record then starting with a fresh flags byte) */
typedef struct { const Rd *rd; size_t o; const u8 *d; u32 len, at; int ok; } Seg;
static int seg_next(Seg *g)
{
  u32 id, len;
  const u8 *d;
  size_t o = g->o + 4 + g->len;
  if (!rec(g->rd, o, &id, &d, &len) || id != 0x003C) { g->ok = 0; return 0; }
  g->o = o; g->d = d; g->len = len; g->at = 0;
  return 1;
}
static int seg_byte(Seg *g, u8 *v)
{
  while (g->ok && g->at >= g->len) if (!seg_next(g)) return 0;
  if (!g->ok) return 0;
  *v = g->d[g->at++];
  return 1;
}
static void seg_skip(Seg *g, u32 n)
{
  while (n && g->ok) {
    u32 k;
    if (g->at >= g->len && !seg_next(g)) return;
    k = g->len - g->at < n ? g->len - g->at : n;
    g->at += k; n -= k;
  }
}
static void read_sst(Rd *rd, size_t o, const u8 *d, u32 len)
{
  Seg g;
  long total, i;
  u8 b0, b1;
  if (len < 8) return;
  total = (long)rd32(d + 4);
  if (total < 0 || total > 4000000L) return;
  if (!(rd->sst = (char **)calloc((size_t)total + 1, sizeof(char *)))) return;
  g.rd = rd; g.o = o; g.d = d; g.len = len; g.at = 8; g.ok = 1;
  for (i = 0; i < total && g.ok; i++) {
    u32 cch, runs = 0, ext = 0, k;
    u8 flags, t;
    Buf s;
    if (!seg_byte(&g, &b0) || !seg_byte(&g, &b1) || !seg_byte(&g, &flags)) break;
    cch = (u32)b0 | ((u32)b1 << 8);
    if (flags & 8) { u8 a, c; seg_byte(&g, &a); seg_byte(&g, &c); runs = (u32)a | ((u32)c << 8); }
    if (flags & 4) { u8 q[4]; for (k = 0; k < 4; k++) seg_byte(&g, &q[k]); ext = rd32(q); }
    buf_init(&s);
    for (k = 0; k < cch && g.ok; k++) {
      u32 cp;
      char u[8];
      if (g.at >= g.len) {
        if (!seg_next(&g)) break;
        if (!seg_byte(&g, &t)) break;       /* the new record's flags */
        flags = (u8)((flags & ~1) | (t & 1));
      }
      if (flags & 1) { u8 lo, hi; if (!seg_byte(&g, &lo) || !seg_byte(&g, &hi)) break; cp = (u32)lo | ((u32)hi << 8); }
      else { u8 c1; if (!seg_byte(&g, &c1)) break; cp = c1; }
      buf_add(&s, u, (size_t)utf8_put(u, cp));
    }
    seg_skip(&g, runs * 4 + ext);
    rd->sst[i] = s.data ? buf_take(&s) : str_dup("");
    rd->nsst = i + 1;
  }
}

/* ------------------------------------------------------------------ formulas */
/* BIFF's built-in functions: index -> name and, for those called with a fixed count (ptgFunc), how
   many arguments (-1: only called with a count, ptgFuncVar) */
typedef struct { u16 i; const char *name; signed char argc; } Ftab;
static const Ftab ftab[] = {
  { 0, "COUNT", -1 }, { 1, "IF", -1 }, { 2, "ISNA", 1 }, { 3, "ISERROR", 1 }, { 4, "SUM", -1 }, { 5, "AVERAGE", -1 }, { 6, "MIN", -1 },
  { 7, "MAX", -1 }, { 8, "ROW", -1 }, { 9, "COLUMN", -1 }, { 10, "NA", 0 }, { 11, "NPV", -1 }, { 12, "STDEV", -1 }, { 13, "DOLLAR", -1 },
  { 14, "FIXED", -1 }, { 15, "SIN", 1 }, { 16, "COS", 1 }, { 17, "TAN", 1 }, { 18, "ATAN", 1 }, { 19, "PI", 0 }, { 20, "SQRT", 1 },
  { 21, "EXP", 1 }, { 22, "LN", 1 }, { 23, "LOG10", 1 }, { 24, "ABS", 1 }, { 25, "INT", 1 }, { 26, "SIGN", 1 }, { 27, "ROUND", 2 },
  { 28, "LOOKUP", -1 }, { 29, "INDEX", -1 }, { 30, "REPT", 2 }, { 31, "MID", 3 }, { 32, "LEN", 1 }, { 33, "VALUE", 1 }, { 34, "TRUE", 0 },
  { 35, "FALSE", 0 }, { 36, "AND", -1 }, { 37, "OR", -1 }, { 38, "NOT", 1 }, { 39, "MOD", 2 }, { 40, "DCOUNT", 3 }, { 41, "DSUM", 3 },
  { 42, "DAVERAGE", 3 }, { 43, "DMIN", 3 }, { 44, "DMAX", 3 }, { 45, "DSTDEV", 3 }, { 46, "VAR", -1 }, { 47, "DVAR", 3 }, { 48, "TEXT", 2 },
  { 49, "LINEST", -1 }, { 50, "TREND", -1 }, { 51, "LOGEST", -1 }, { 52, "GROWTH", -1 }, { 56, "PV", -1 }, { 57, "FV", -1 }, { 58, "NPER", -1 },
  { 59, "PMT", -1 }, { 60, "RATE", -1 }, { 61, "MIRR", 3 }, { 62, "IRR", -1 }, { 63, "RAND", 0 }, { 64, "MATCH", -1 }, { 65, "DATE", 3 },
  { 66, "TIME", 3 }, { 67, "DAY", 1 }, { 68, "MONTH", 1 }, { 69, "YEAR", 1 }, { 70, "WEEKDAY", -1 }, { 71, "HOUR", 1 }, { 72, "MINUTE", 1 },
  { 73, "SECOND", 1 }, { 74, "NOW", 0 }, { 75, "AREAS", 1 }, { 76, "ROWS", 1 }, { 77, "COLUMNS", 1 }, { 78, "OFFSET", -1 }, { 82, "SEARCH", -1 },
  { 83, "TRANSPOSE", 1 }, { 86, "TYPE", 1 }, { 97, "ATAN2", 2 }, { 98, "ASIN", 1 }, { 99, "ACOS", 1 }, { 100, "CHOOSE", -1 },
  { 101, "HLOOKUP", -1 }, { 102, "VLOOKUP", -1 }, { 105, "ISREF", 1 }, { 109, "LOG", -1 }, { 111, "CHAR", 1 }, { 112, "LOWER", 1 },
  { 113, "UPPER", 1 }, { 114, "PROPER", 1 }, { 115, "LEFT", -1 }, { 116, "RIGHT", -1 }, { 117, "EXACT", 2 }, { 118, "TRIM", 1 },
  { 119, "REPLACE", 4 }, { 120, "SUBSTITUTE", -1 }, { 121, "CODE", 1 }, { 124, "FIND", -1 }, { 125, "CELL", -1 }, { 126, "ISERR", 1 },
  { 127, "ISTEXT", 1 }, { 128, "ISNUMBER", 1 }, { 129, "ISBLANK", 1 }, { 130, "T", 1 }, { 131, "N", 1 }, { 140, "DATEVALUE", 1 },
  { 141, "TIMEVALUE", 1 }, { 142, "SLN", 3 }, { 143, "SYD", 4 }, { 144, "DDB", -1 }, { 148, "INDIRECT", -1 }, { 162, "CLEAN", 1 },
  { 163, "MDETERM", 1 }, { 164, "MINVERSE", 1 }, { 165, "MMULT", 2 }, { 167, "IPMT", -1 }, { 168, "PPMT", -1 }, { 169, "COUNTA", -1 },
  { 183, "PRODUCT", -1 }, { 184, "FACT", 1 }, { 189, "DPRODUCT", 3 }, { 190, "ISNONTEXT", 1 }, { 193, "STDEVP", -1 }, { 194, "VARP", -1 },
  { 195, "DSTDEVP", 3 }, { 196, "DVARP", 3 }, { 197, "TRUNC", -1 }, { 198, "ISLOGICAL", 1 }, { 199, "DCOUNTA", 3 }, { 204, "USDOLLAR", -1 },
  { 205, "FINDB", -1 }, { 206, "SEARCHB", -1 }, { 207, "REPLACEB", 4 }, { 208, "LEFTB", -1 }, { 209, "RIGHTB", -1 }, { 210, "MIDB", 3 },
  { 211, "LENB", 1 }, { 212, "ROUNDUP", 2 }, { 213, "ROUNDDOWN", 2 }, { 214, "ASC", 1 }, { 215, "DBCS", 1 }, { 216, "RANK", -1 },
  { 219, "ADDRESS", -1 }, { 220, "DAYS360", -1 }, { 221, "TODAY", 0 }, { 222, "VDB", -1 }, { 227, "MEDIAN", -1 }, { 228, "SUMPRODUCT", -1 },
  { 229, "SINH", 1 }, { 230, "COSH", 1 }, { 231, "TANH", 1 }, { 232, "ASINH", 1 }, { 233, "ACOSH", 1 }, { 234, "ATANH", 1 },
  { 235, "DGET", 3 }, { 244, "INFO", 1 }, { 247, "DB", -1 }, { 252, "FREQUENCY", 2 }, { 261, "ERROR.TYPE", 1 }, { 269, "AVEDEV", -1 },
  { 270, "BETADIST", -1 }, { 271, "GAMMALN", 1 }, { 272, "BETAINV", -1 }, { 273, "BINOMDIST", 4 }, { 274, "CHIDIST", 2 },
  { 275, "CHIINV", 2 }, { 276, "COMBIN", 2 }, { 277, "CONFIDENCE", 3 }, { 278, "CRITBINOM", 3 }, { 279, "EVEN", 1 }, { 280, "EXPONDIST", 3 },
  { 281, "FDIST", 3 }, { 282, "FINV", 3 }, { 283, "FISHER", 1 }, { 284, "FISHERINV", 1 }, { 285, "FLOOR", 2 }, { 286, "GAMMADIST", 4 },
  { 287, "GAMMAINV", 3 }, { 288, "CEILING", 2 }, { 289, "HYPGEOMDIST", 4 }, { 290, "LOGNORMDIST", 3 }, { 291, "LOGINV", 3 },
  { 292, "NEGBINOMDIST", 3 }, { 293, "NORMDIST", 4 }, { 294, "NORMSDIST", 1 }, { 295, "NORMINV", 3 }, { 296, "NORMSINV", 1 },
  { 297, "STANDARDIZE", 3 }, { 298, "ODD", 1 }, { 299, "PERMUT", 2 }, { 300, "POISSON", 3 }, { 301, "TDIST", 3 }, { 302, "WEIBULL", 4 },
  { 303, "SUMXMY2", 2 }, { 304, "SUMX2MY2", 2 }, { 305, "SUMX2PY2", 2 }, { 306, "CHITEST", 2 }, { 307, "CORREL", 2 }, { 308, "COVAR", 2 },
  { 309, "FORECAST", 3 }, { 310, "FTEST", 2 }, { 311, "INTERCEPT", 2 }, { 312, "PEARSON", 2 }, { 313, "RSQ", 2 }, { 314, "STEYX", 2 },
  { 315, "SLOPE", 2 }, { 316, "TTEST", 4 }, { 317, "PROB", -1 }, { 318, "DEVSQ", -1 }, { 319, "GEOMEAN", -1 }, { 320, "HARMEAN", -1 },
  { 321, "SUMSQ", -1 }, { 322, "KURT", -1 }, { 323, "SKEW", -1 }, { 324, "ZTEST", -1 }, { 325, "LARGE", 2 }, { 326, "SMALL", 2 },
  { 327, "QUARTILE", 2 }, { 328, "PERCENTILE", 2 }, { 329, "PERCENTRANK", -1 }, { 330, "MODE", -1 }, { 331, "TRIMMEAN", 2 }, { 332, "TINV", 2 },
  { 336, "CONCATENATE", -1 }, { 337, "POWER", 2 }, { 342, "RADIANS", 1 }, { 343, "DEGREES", 1 }, { 344, "SUBTOTAL", -1 }, { 345, "SUMIF", -1 },
  { 346, "COUNTIF", 2 }, { 347, "COUNTBLANK", 1 }, { 350, "ISPMT", 4 }, { 351, "DATEDIF", 3 }, { 354, "ROMAN", -1 }, { 358, "GETPIVOTDATA", -1 },
  { 359, "HYPERLINK", -1 }, { 360, "PHONETIC", 1 }, { 361, "AVERAGEA", -1 }, { 362, "MAXA", -1 }, { 363, "MINA", -1 }, { 364, "STDEVPA", -1 },
  { 365, "VARPA", -1 }, { 366, "STDEVA", -1 }, { 367, "VARA", -1 }, { 368, "BAHTTEXT", 1 } };
static const Ftab *ffind(u32 i)
{
  int lo = 0, hi = (int)(sizeof ftab / sizeof ftab[0]) - 1;
  while (lo <= hi) { int m = (lo + hi) / 2; if (ftab[m].i == i) return &ftab[m]; if (ftab[m].i < i) lo = m + 1; else hi = m - 1; }
  return NULL;
}
static const char *errtext(u32 e)
{
  switch (e) { case 0x00: return "#NULL!"; case 0x07: return "#DIV/0!"; case 0x0F: return "#VALUE!"; case 0x17: return "#REF!"; case 0x1D: return "#NAME?"; case 0x24: return "#NUM!"; case 0x2A: return "#N/A"; }
  return "#N/A";
}
static int errcode(u32 e)
{
  switch (e) { case 0x00: return E_NULL; case 0x07: return E_DIV0; case 0x0F: return E_VALUE; case 0x17: return E_REF; case 0x1D: return E_NAME; case 0x24: return E_NUM; }
  return E_NA;
}
/* the expression stack: strings, each with its precedence (to bracket only where needed) */
#define MAXSTK 64
typedef struct { char *s[MAXSTK]; int prec[MAXSTK]; int n; int bad; } Stk;
static void spush(Stk *k, char *s, int prec)
{
  if (!s) { k->bad = 1; return; }
  if (k->n >= MAXSTK) { free(s); k->bad = 1; return; }
  k->s[k->n] = s; k->prec[k->n] = prec; k->n++;
}
static char *spop(Stk *k) { if (k->n <= 0) { k->bad = 1; return NULL; } return k->s[--k->n]; }
static char *cat3(const char *a, const char *b, const char *c)
{
  size_t n = strlen(a) + strlen(b) + strlen(c) + 1;
  char *s = (char *)malloc(n);
  if (s) { strcpy(s, a); strcat(s, b); strcat(s, c); }
  return s;
}
/* a cell or column reference's text (ra, ca: $ wanted) */
static void cellref(char *out, i32 r, i32 c, int rrel, int crel)
{
  char cn[8];
  col_name(c, cn);
  sprintf(out, "%s%s%s%ld", crel ? "" : "$", cn, rrel ? "" : "$", (long)r + 1);
}
static void sheet_prefix(const Rd *rd, int ixti, char *out, size_t cap)
{
  out[0] = 0;
  if (ixti < 0 || ixti >= rd->nxti) { snprintf(out, cap, "#REF!"); return; }
  {
    const Xti *x = &rd->xti[ixti];
    if (x->sup != rd->selfsup || x->first < 0 || x->first >= rd->nsh) { snprintf(out, cap, "#REF!"); return; }
    if (x->last != x->first) { snprintf(out, cap, "#REF!"); return; }        /* (3-D ranges aren't Ledger's) */
    {
      const char *n = rd->sh[x->first].name, *p;
      int quote = 0;
      for (p = n; *p; p++) if (!((*p >= 'A' && *p <= 'Z') || (*p >= 'a' && *p <= 'z') || (*p >= '0' && *p <= '9') || *p == '_' || (u8)*p >= 0x80)) quote = 1;
      if (n[0] >= '0' && n[0] <= '9') quote = 1;
      if (quote) {
        size_t o = 0;
        out[o++] = '\'';
        for (p = n; *p && o + 4 < cap; p++) { if (*p == '\'') out[o++] = '\''; out[o++] = *p; }
        out[o++] = '\''; out[o++] = '!'; out[o] = 0;
      } else snprintf(out, cap, "%s!", n);
    }
  }
}
/* a reference's row and column from the file: absolute, or (shared formulas: RefN, AreaN) offsets
   from the cell r0, c0 */
static void loc(u32 rw, u32 col, int shared, i32 r0, i32 c0, i32 *r, i32 *c, int *rrel, int *crel)
{
  *rrel = (col & 0x8000) != 0; *crel = (col & 0x4000) != 0;
  *r = (i32)rw; *c = (i32)(col & 0x3FFF);
  if (shared) {
    if (*rrel) *r = (i32)((r0 + (i32)(i16)(u16)rw) & 0xFFFF);
    if (*crel) *c = (i32)((c0 + (i32)(signed char)(u8)(col & 0xFF)) & 0xFF);
  }
}
/* RPN tokens to formula text (without "="), as seen from cell r0, c0; NULL if Ledger can't say it.
   extra: the data after the tokens (array constants) */
static char *rpn(const Rd *rd, const u8 *p, u32 n, const u8 *extra, u32 nextra, int shared, i32 r0, i32 c0)
{
  Stk k;
  u32 i = 0, ei = 0;
  char t[600], pre[100];
  memset(&k, 0, sizeof k);
#define NEED(x) do { if (i + (x) > n) { k.bad = 1; goto out; } } while (0)
  while (i < n && !k.bad) {
    u32 ptg = p[i++], base = ptg < 0x20 ? ptg : (ptg & 0x1F) | 0x20;
    switch (base) {
    case 0x03: case 0x04: case 0x05: case 0x06: case 0x07: case 0x08: case 0x09: case 0x0A: case 0x0B: case 0x0C: case 0x0D: case 0x0E:
    case 0x0F: case 0x10: case 0x11: {
      static const char *const ops[] = { "+", "-", "*", "/", "^", "&", "<", "<=", "=", ">=", ">", "<>", " ", ",", ":" };
      static const int prec[] = { 4, 4, 5, 5, 6, 3, 2, 2, 2, 2, 2, 2, 9, 9, 9 };
      char *b = spop(&k), *a = spop(&k), *s, *aa, *bb;
      int pb = k.prec[k.n + 1], pa = k.prec[k.n], pr = prec[base - 3];
      if (!a || !b) { free(a); free(b); break; }
      aa = pa < pr ? cat3("(", a, ")") : str_dup(a);
      bb = pb <= pr && base != 0x0F && base != 0x10 && base != 0x11 ? cat3("(", b, ")") : str_dup(b);
      if (base == 0x10) { s = cat3(aa ? aa : "", ",", bb ? bb : ""); spush(&k, s, 1); }   /* (a union stays in the list it is) */
      else { s = aa && bb ? cat3(aa, ops[base - 3], bb) : NULL; spush(&k, s, pr); }
      free(a); free(b); free(aa); free(bb);
      break; }
    case 0x12: case 0x13: { char *a = spop(&k); int pa = k.prec[k.n]; if (a) { char *x = pa < 9 ? cat3("(", a, ")") : str_dup(a); spush(&k, x ? cat3(base == 0x12 ? "+" : "-", x, "") : NULL, 9); free(x); free(a); } break; }
    case 0x14: { char *a = spop(&k); int pa = k.prec[k.n]; if (a) { char *x = pa < 8 ? cat3("(", a, ")") : str_dup(a); spush(&k, x ? cat3(x, "%", "") : NULL, 8); free(x); free(a); } break; }
    case 0x15: { char *a = spop(&k); if (a) { spush(&k, cat3("(", a, ")"), 10); free(a); } break; }
    case 0x16: spush(&k, str_dup(""), 10); break;
    case 0x17: {
      u32 used;
      char *s;
      Buf q;
      const char *c;
      NEED(2);
      s = xlstr(p + i, n - i, 1, &used);
      i += used;
      buf_init(&q);
      buf_addc(&q, '"');
      for (c = s ? s : ""; *c; c++) { if (*c == '"') buf_addc(&q, '"'); buf_addc(&q, *c); }
      buf_addc(&q, '"');
      free(s);
      spush(&k, q.data ? buf_take(&q) : NULL, 10);
      break; }
    case 0x18: k.bad = 1; break;                        /* (elf and the like: not in worksheets Ledger reads) */
    case 0x19: {
      u32 g;
      NEED(3);
      g = p[i];
      if (g & 0x04) { u32 cnt = rd16(p + i + 1); i += 3 + (cnt + 1) * 2; }    /* choose's jump table */
      else if (g & 0x10) {                               /* SUM of one argument */
        char *a = spop(&k);
        i += 3;
        if (a) { spush(&k, cat3("SUM(", a, ")"), 10); free(a); }
      } else i += 3;
      break; }
    case 0x1C: NEED(1); spush(&k, str_dup(errtext(p[i])), 10); i++; break;
    case 0x1D: NEED(1); spush(&k, str_dup(p[i] ? "TRUE" : "FALSE"), 10); i++; break;
    case 0x1E: NEED(2); sprintf(t, "%lu", (unsigned long)rd16(p + i)); spush(&k, str_dup(t), 10); i += 2; break;
    case 0x1F: { double d; NEED(8); d = rddbl(p + i); i += 8; if (d != d) d = 0; sprintf(t, "%.15g", d); spush(&k, str_dup(t), 10); break; }
    case 0x20: {
      /* an array constant: its values in extra */
      u32 cols, rows, a, b2;
      Buf q;
      NEED(7);
      i += 7;
      if (!extra || ei + 3 > nextra) { k.bad = 1; break; }
      cols = (u32)extra[ei] + 1; rows = rd16(extra + ei + 1) + 1; ei += 3;
      buf_init(&q);
      buf_addc(&q, '{');
      for (a = 0; a < rows && !k.bad; a++)
        for (b2 = 0; b2 < cols && !k.bad; b2++) {
          u32 ty;
          if (b2) buf_addc(&q, ','); else if (a) buf_addc(&q, ';');
          if (ei >= nextra) { k.bad = 1; break; }
          ty = extra[ei++];
          if (ty == 1 && ei + 8 <= nextra) { sprintf(t, "%.15g", rddbl(extra + ei)); buf_adds(&q, t); ei += 8; }
          else if (ty == 2 && ei + 3 <= nextra) {
            u32 used; char *s = xlstr(extra + ei, nextra - ei, 2, &used); const char *c;
            buf_addc(&q, '"'); for (c = s ? s : ""; *c; c++) { if (*c == '"') buf_addc(&q, '"'); buf_addc(&q, *c); } buf_addc(&q, '"');
            free(s); ei += used;
          } else if (ty == 4 && ei + 8 <= nextra) { buf_adds(&q, extra[ei] ? "TRUE" : "FALSE"); ei += 8; }
          else if (ty == 16 && ei + 8 <= nextra) { buf_adds(&q, errtext(extra[ei])); ei += 8; }
          else if (ty == 0 && ei + 8 <= nextra) { buf_adds(&q, "0"); ei += 8; }
          else k.bad = 1;
        }
      buf_addc(&q, '}');
      spush(&k, q.data ? buf_take(&q) : NULL, 10);
      break; }
    case 0x21: case 0x22: {
      u32 fi, argc, a;
      const Ftab *f;
      Buf q;
      char *args[64];
      const char *fname;
      char *own = NULL;
      if (base == 0x21) { NEED(2); fi = rd16(p + i); i += 2; f = ffind(fi); if (!f || f->argc < 0) { k.bad = 1; break; } argc = (u32)f->argc; }
      else { NEED(3); argc = p[i] & 0x7F; fi = rd16(p + i + 1) & 0x7FFF; i += 3; f = ffind(fi); }
      if (argc > 64 || (int)argc > k.n) { k.bad = 1; break; }
      for (a = argc; a > 0; a--) args[a - 1] = spop(&k);
      if (fi == 255) {
        /* an add-in or newer function: its name is the first argument */
        if (!argc || !args[0]) { k.bad = 1; for (a = 0; a < argc; a++) free(args[a]); break; }
        own = args[0];
        fname = own;
        if (!strncmp(fname, "_xlfn.", 6)) fname += 6;
      } else if (!f) { k.bad = 1; for (a = 0; a < argc; a++) free(args[a]); break; }
      else fname = f->name;
      buf_init(&q);
      buf_adds(&q, fname);
      buf_addc(&q, '(');
      for (a = fi == 255 ? 1 : 0; a < argc; a++) { if (a > (fi == 255 ? 1u : 0u)) buf_addc(&q, ','); buf_adds(&q, args[a] ? args[a] : ""); }
      buf_addc(&q, ')');
      for (a = 0; a < argc; a++) free(args[a]);
      spush(&k, q.data ? buf_take(&q) : NULL, 10);
      break; }
    case 0x23: {
      u32 ni;
      NEED(4);
      ni = rd16(p + i); i += 4;
      if (ni < 1 || (int)ni > rd->nnames || !rd->names[ni - 1]) { k.bad = 1; break; }
      spush(&k, str_dup(rd->names[ni - 1]), 10);
      break; }
    case 0x39: {
      /* an external name: an add-in function's (taken by the call after it) or another book's */
      u32 ixti, ni;
      NEED(6);
      ixti = rd16(p + i); ni = rd16(p + i + 2); i += 6;
      (void)ixti;
      if (ni < 1 || (int)ni > rd->nextn || !rd->extnames[ni - 1]) { k.bad = 1; break; }
      spush(&k, str_dup(rd->extnames[ni - 1]), 10);
      break; }
    case 0x24: case 0x2C: case 0x3A: {
      i32 r, c;
      int rr, cr, ixti = -1;
      if (base == 0x3A) { NEED(6); ixti = (int)rd16(p + i); i += 2; } else NEED(4);
      loc(rd16(p + i), rd16(p + i + 2), base == 0x2C, r0, c0, &r, &c, &rr, &cr);
      i += 4;
      if (ixti >= 0) sheet_prefix(rd, ixti, pre, sizeof pre); else pre[0] = 0;
      if (!strcmp(pre, "#REF!")) { spush(&k, str_dup("#REF!"), 10); break; }
      cellref(t, r, c, rr, cr);
      spush(&k, cat3(pre, t, ""), 10);
      break; }
    case 0x25: case 0x2D: case 0x3B: {
      i32 ra, ca, rb, cb;
      int rra, cra, rrb, crb, ixti = -1;
      char a1[24], a2[24], cn1[8], cn2[8];
      if (base == 0x3B) { NEED(10); ixti = (int)rd16(p + i); i += 2; } else NEED(8);
      loc(rd16(p + i), rd16(p + i + 4), base == 0x2D, r0, c0, &ra, &ca, &rra, &cra);
      loc(rd16(p + i + 2), rd16(p + i + 6), base == 0x2D, r0, c0, &rb, &cb, &rrb, &crb);
      i += 8;
      if (ixti >= 0) sheet_prefix(rd, ixti, pre, sizeof pre); else pre[0] = 0;
      if (!strcmp(pre, "#REF!")) { spush(&k, str_dup("#REF!"), 10); break; }
      if (ra == 0 && rb >= 65535 && !shared) {
        /* whole columns */
        col_name(ca, cn1); col_name(cb, cn2);
        snprintf(t, sizeof t, "%s%s%s:%s%s", pre, cra ? "" : "$", cn1, crb ? "" : "$", cn2);
      } else if (ca == 0 && cb >= 255 && !shared) {
        snprintf(t, sizeof t, "%s%s%ld:%s%ld", pre, rra ? "" : "$", (long)ra + 1, rrb ? "" : "$", (long)rb + 1);
      } else {
        cellref(a1, ra, ca, rra, cra); cellref(a2, rb, cb, rrb, crb);
        snprintf(t, sizeof t, "%s%s:%s", pre, a1, a2);
      }
      spush(&k, str_dup(t), 10);
      break; }
    case 0x26: case 0x27: case 0x28: NEED(6); i += 6; break;        /* (mem areas: the expression inside follows) */
    case 0x29: NEED(2); i += 2; break;
    case 0x2A: NEED(4); i += 4; spush(&k, str_dup("#REF!"), 10); break;
    case 0x2B: NEED(8); i += 8; spush(&k, str_dup("#REF!"), 10); break;
    case 0x3C: NEED(6); i += 6; spush(&k, str_dup("#REF!"), 10); break;
    case 0x3D: NEED(10); i += 10; spush(&k, str_dup("#REF!"), 10); break;
    default: k.bad = 1; break;
    }
  }
out:
#undef NEED
  if (k.bad || k.n != 1) { while (k.n) free(k.s[--k.n]); return NULL; }
  return k.s[0];
}

/* ------------------------------------------------------------------ styles */
static u32 color_of(const Rd *rd, u32 icv, u32 dflt) { return icv < 64 ? rd->pal[icv] : dflt; }
static const u32 defpal[64] = {
  0x000000, 0xFFFFFF, 0xFF0000, 0x00FF00, 0x0000FF, 0xFFFF00, 0xFF00FF, 0x00FFFF, 0x000000, 0xFFFFFF, 0xFF0000, 0x00FF00,
  0x0000FF, 0xFFFF00, 0xFF00FF, 0x00FFFF, 0x800000, 0x008000, 0x000080, 0x808000, 0x800080, 0x008080, 0xC0C0C0, 0x808080,
  0x9999FF, 0x993366, 0xFFFFCC, 0xCCFFFF, 0x660066, 0xFF8080, 0x0066CC, 0xCCCCFF, 0x000080, 0xFF00FF, 0xFFFF00, 0x00FFFF,
  0x800080, 0x800000, 0x008080, 0x0000FF, 0x00CCFF, 0xCCFFFF, 0xCCFFCC, 0xFFFF99, 0x99CCFF, 0xFF99CC, 0xCC99FF, 0xFFCC99,
  0x3366FF, 0x33CCCC, 0x99CC00, 0xFFCC00, 0xFF9900, 0xFF6600, 0x666699, 0x969696, 0x003366, 0x339966, 0x003300, 0x333300,
  0x993300, 0x993366, 0x333399, 0x333333 };
static int bstyle(u32 dg)
{
  static const u8 m[14] = { BS_NONE, BS_THIN, BS_MEDIUM, BS_DASHED, BS_DOTTED, BS_THICK, BS_DOUBLE, BS_HAIR, BS_MEDIUM, BS_DASHED, BS_MEDIUM, BS_DASHED, BS_MEDIUM, BS_MEDIUM };
  return dg < 14 ? m[dg] : BS_THIN;
}
static int fmt_id(Rd *rd, u32 ifmt)
{
  int i;
  for (i = 0; i < rd->nfmt; i++) if (rd->fmts[i].id == ifmt) return book_fmt(rd->b, rd->fmts[i].code);
  return ifmt < 164 ? (int)ifmt : 0;
}
static void read_xf(Rd *rd, const u8 *d, u32 len)
{
  Style st;
  u32 ifnt, ifmt, f, al, b1, b2, ic, rot;
  int k;
  u16 *nx;
  if (len < 20 || rd->nxf >= 4000) return;
  if (!(nx = (u16 *)realloc(rd->xfstyle, (size_t)(rd->nxf + 1) * sizeof(u16)))) return;
  rd->xfstyle = nx;
  memset(&st, 0, sizeof st);
  ifnt = rd16(d); ifmt = rd16(d + 2); f = rd16(d + 4); al = d[6]; rot = d[7]; b1 = rd32(d + 10); b2 = rd32(d + 14); ic = rd16(d + 18);
  if (ifnt >= 4) ifnt--;                                 /* (font 4 doesn't exist) */
  st.font = (u16)(ifnt < (u32)rd->nfont ? rd->fontmap[ifnt] : 0);
  st.numfmt = (u16)fmt_id(rd, ifmt);
  st.locked = (u8)(f & 1); st.hidden = (u8)((f >> 1) & 1);
  st.halign = (u8)((al & 7) <= HA_CENTERSEL ? (al & 7) : HA_GENERAL);
  st.wrap = (u8)((al >> 3) & 1);
  st.valign = (u8)(((al >> 4) & 7) == 0 ? VA_TOP : ((al >> 4) & 7) == 1 ? VA_CENTER : VA_BOTTOM);
  st.rotation = (i16)(rot <= 90 ? (int)rot : rot <= 180 ? -(int)(rot - 90) : 0);
  st.indent = (u8)(d[8] & 15);
  st.shrink = (u8)((d[8] >> 4) & 1);
  st.border[B_LEFT] = (u8)bstyle(b1 & 15); st.border[B_RIGHT] = (u8)bstyle((b1 >> 4) & 15);
  st.border[B_TOP] = (u8)bstyle((b1 >> 8) & 15); st.border[B_BOTTOM] = (u8)bstyle((b1 >> 12) & 15);
  st.bcolor[B_LEFT] = color_of(rd, (b1 >> 16) & 0x7F, COLOR_AUTO); st.bcolor[B_RIGHT] = color_of(rd, (b1 >> 23) & 0x7F, COLOR_AUTO);
  st.bcolor[B_TOP] = color_of(rd, b2 & 0x7F, COLOR_AUTO); st.bcolor[B_BOTTOM] = color_of(rd, (b2 >> 7) & 0x7F, COLOR_AUTO);
  for (k = 0; k < 4; k++) if (!st.border[k]) st.bcolor[k] = 0;
  if ((b2 >> 26) & 0x3F) { u32 fg = ic & 0x7F; if (fg < 64) st.fill = FILL_ON | rd->pal[fg]; }
  k = book_style(rd->b, &st);
  rd->xfstyle[rd->nxf++] = (u16)(k < 0 ? 0 : k);
}
static void read_font(Rd *rd, const u8 *d, u32 len)
{
  Font f;
  u32 used;
  char *nm;
  int *nf, k;
  if (len < 15 || rd->nfont >= 1000) return;
  if (!(nf = (int *)realloc(rd->fontmap, (size_t)(rd->nfont + 1) * sizeof(int)))) return;
  rd->fontmap = nf;
  memset(&f, 0, sizeof f);
  f.size = (u16)(rd16(d) / 2);
  f.italic = (u8)((rd16(d + 2) >> 1) & 1); f.strike = (u8)((rd16(d + 2) >> 3) & 1);
  f.color = rd16(d + 4) < 64 ? rd->pal[rd16(d + 4)] : COLOR_AUTO;
  if (f.color == 0x000000) f.color = COLOR_AUTO;          /* (black text: automatic, so it shows on dark backgrounds too) */
  f.bold = (u8)(rd16(d + 6) >= 700);
  f.underline = (u8)(d[10] == 1 || d[10] == 0x21 ? 1 : d[10] == 2 || d[10] == 0x22 ? 2 : 0);
  nm = xlstr(d + 14, len - 14, 1, &used);
  strncpy(f.name, nm && nm[0] ? nm : "Arial", sizeof f.name - 1);
  free(nm);
  if (!f.size) f.size = 100;
  k = book_font(rd->b, &f);
  rd->fontmap[rd->nfont++] = k < 0 ? 0 : k;
}

/* ------------------------------------------------------------------ a sheet */
static int style_of(const Rd *rd, u32 xf) { return xf < (u32)rd->nxf ? rd->xfstyle[xf] : 0; }
static double rk(u32 v)
{
  double d;
  if (v & 2) d = (double)((i32)v >> 2);
  else { union { double d; u8 b[8]; } u; u32 hi = v & 0xFFFFFFFCUL; u8 le[8]; int i; static const union { u16 x; u8 b[2]; } e = { 1 };
    memset(le, 0, 8); le[4] = (u8)hi; le[5] = (u8)(hi >> 8); le[6] = (u8)(hi >> 16); le[7] = (u8)(hi >> 24);
    for (i = 0; i < 8; i++) u.b[e.b[0] ? i : 7 - i] = le[i];
    d = u.d; }
  return (v & 1) ? d / 100 : d;
}
static Cell *put_cell(Rd *rd, int si, u32 r, u32 c, u32 xf)
{
  Cell *cell;
  if (r >= MAXROW || c >= MAXCOL || !(cell = sheet_cell_make(rd->b->sh[si], (i32)r, (i32)c))) return NULL;
  val_free(&cell->v); formula_free(cell->f); cell->f = NULL;
  cell->style = (u16)style_of(rd, xf);
  return cell;
}
/* a DV record: one data validation rule (its formulas seen from the first cell of its ranges) */
static char *dvstr(const u8 *p, u32 n, u32 *used)
{
  char *t = xlstr(p, n, 2, used);
  if (t && t[0] == 1 && !t[1]) t[0] = 0;       /* (an empty string is written as one NUL) */
  return t;
}
static void read_dv(Rd *rd, int si, const u8 *d, u32 len)
{
  Sheet *s = rd->b->sh[si];
  DVRule r;
  u32 fl, at = 4, used, k, cce, cref;
  char *fx[2] = { NULL, NULL };
  const u8 *fp[2];
  u32 fc[2];
  if (len < 4 + 4 * 3 || s->ndvs >= 10000) return;
  fl = rd32(d);
  dv_rule_init(&r);
  r.type = (u8)((fl & 15) < DV_N ? (fl & 15) : 0);
  r.errstyle = (u8)(((fl >> 4) & 7) > 2 ? 0 : ((fl >> 4) & 7));
  r.blank = (u8)((fl >> 8) & 1); r.dropdown = (u8)!((fl >> 9) & 1);
  r.showinput = (u8)((fl >> 18) & 1); r.showerror = (u8)((fl >> 19) & 1);
  r.op = (u8)(((fl >> 20) & 15) < 8 ? ((fl >> 20) & 15) : 0);
  free(r.intitle); free(r.errtitle); free(r.inmsg); free(r.errmsg);
  r.intitle = dvstr(d + at, len - at, &used); at += used;
  r.errtitle = at < len ? dvstr(d + at, len - at, &used) : str_dup(""); at += used;
  r.inmsg = at < len ? dvstr(d + at, len - at, &used) : str_dup(""); at += used;
  r.errmsg = at < len ? dvstr(d + at, len - at, &used) : str_dup(""); at += used;
  for (k = 0; k < 2; k++) {
    if (at + 4 > len) { dv_rule_free(&r); return; }
    cce = rd16(d + at); at += 4;
    if (at + cce > len) { dv_rule_free(&r); return; }
    fp[k] = d + at; fc[k] = cce; at += cce;
  }
  if (at + 2 > len) { dv_rule_free(&r); return; }
  cref = rd16(d + at); at += 2;
  for (k = 0; k < cref && at + 8 <= len && k < 1000; k++, at += 8) {
    Rect *na = (Rect *)realloc(r.areas, (size_t)(r.nareas + 1) * sizeof(Rect));
    if (!na) break;
    r.areas = na;
    na[r.nareas].r1 = (i32)rd16(d + at); na[r.nareas].r2 = (i32)rd16(d + at + 2); na[r.nareas].c1 = (i32)rd16(d + at + 4); na[r.nareas].c2 = (i32)rd16(d + at + 6);
    if (na[r.nareas].r2 >= 65535) na[r.nareas].r2 = MAXROW - 1;
    r.nareas++;
  }
  if (!r.nareas) { dv_rule_free(&r); return; }
  for (k = 0; k < 2; k++) if (fc[k]) fx[k] = rpn(rd, fp[k], fc[k], NULL, 0, 1, r.areas[0].r1, r.areas[0].c1);
  if (fx[0]) { char *p; for (p = fx[0]; *p; p++) if (*p == 1) *p = ','; }      /* (a typed list's items: NUL-separated) */
  for (k = 0; k < 2; k++) if (fx[k]) {
    int pos;
    Formula *f = formula_parse(rd->b, si, fx[k], &pos);
    if (k == 0) r.f1 = f; else r.f2 = f;
    free(fx[k]);
  }
  if (r.type != DV_ANY && !r.f1) { dv_rule_free(&r); return; }
  {
    DVRule *nd = (DVRule *)realloc(s->dvs, (size_t)(s->ndvs + 1) * sizeof(DVRule));
    if (!nd) { dv_rule_free(&r); return; }
    s->dvs = nd; nd[s->ndvs++] = r;
  }
}
static void read_sheet(Rd *rd, int si, size_t o)
{
  u32 id, len;
  const u8 *d;
  Sheet *s = rd->b->sh[si];
  Shared *shr = NULL;
  int nshr = 0, pass, k;
  size_t start = o;
  /* two passes: the shared and array formulas first (they come after the cells using them) */
  for (pass = 0; pass < 2; pass++) {
    size_t q = start;
    Cell *lastf = NULL;
    i32 lastr = -1, lastc = -1;
    int nest = 0;
    while (rec(rd, q, &id, &d, &len)) {
      size_t next = q + 4 + len;
      /* (an embedded chart's substream skipped) */
      if (id == 0x0809) { nest++; q = next; continue; }
      if (id == 0x000A) { if (!nest) break; nest--; q = next; continue; }
      if (nest) { q = next; continue; }
      if (pass == 0) {
        if ((id == 0x04BC || id == 0x0221) && len >= 12 && nshr < 2000) {
          Shared *ns = (Shared *)realloc(shr, (size_t)(nshr + 1) * sizeof(Shared));
          u32 off = id == 0x04BC ? 8 : 12, cce;
          if (!ns) break;
          shr = ns;
          memset(&shr[nshr], 0, sizeof *shr);
          shr[nshr].r1 = (i32)rd16(d); shr[nshr].r2 = (i32)rd16(d + 2); shr[nshr].c1 = d[4]; shr[nshr].c2 = d[5];
          if (off + 2 <= len) {
            cce = rd16(d + off);
            if (off + 2 + cce <= len) {
              shr[nshr].rgce = (u8 *)malloc(cce + 1); shr[nshr].cce = cce;
              if (shr[nshr].rgce) memcpy(shr[nshr].rgce, d + off + 2, cce);
              shr[nshr].nextra = len - off - 2 - cce;
              if (shr[nshr].nextra && (shr[nshr].extra = (u8 *)malloc(shr[nshr].nextra))) memcpy(shr[nshr].extra, d + off + 2 + cce, shr[nshr].nextra);
              else shr[nshr].nextra = 0;
              if (id == 0x0221) shr[nshr].c1 |= 0x10000;      /* (marks an array formula) */
            }
          }
          nshr++;
        }
        q = next;
        continue;
      }
      switch (id) {
      case 0x0203: if (len >= 14) { Cell *c = put_cell(rd, si, rd16(d), rd16(d + 2), rd16(d + 4)); if (c) c->v = val_num(rddbl(d + 6)); } break;
      case 0x027E: if (len >= 10) { Cell *c = put_cell(rd, si, rd16(d), rd16(d + 2), rd16(d + 4)); if (c) c->v = val_num(rk(rd32(d + 6))); } break;
      case 0x00BD: if (len >= 6) {
        u32 r = rd16(d), c = rd16(d + 2), i;
        for (i = 4; i + 6 <= len - 2; i += 6, c++) { Cell *cl = put_cell(rd, si, r, c, rd16(d + i)); if (cl) cl->v = val_num(rk(rd32(d + i + 2))); }
      } break;
      case 0x00FD: if (len >= 10) { long ix = (long)rd32(d + 6); Cell *c = put_cell(rd, si, rd16(d), rd16(d + 2), rd16(d + 4)); if (c) c->v = val_str(ix >= 0 && ix < rd->nsst && rd->sst[ix] ? rd->sst[ix] : ""); } break;
      case 0x0204: case 0x00D6: if (len >= 8) { u32 used; char *t = xlstr(d + 6, len - 6, 2, &used); Cell *c = put_cell(rd, si, rd16(d), rd16(d + 2), rd16(d + 4)); if (c) c->v = val_str(t ? t : ""); free(t); } break;
      case 0x0205: if (len >= 8) { Cell *c = put_cell(rd, si, rd16(d), rd16(d + 2), rd16(d + 4)); if (c) c->v = d[7] ? val_err(errcode(d[6])) : val_bool(d[6] != 0); } break;
      case 0x0201: if (len >= 6) { u32 st = style_of(rd, rd16(d + 4)); if (st) put_cell(rd, si, rd16(d), rd16(d + 2), rd16(d + 4)); } break;
      case 0x00BE: if (len >= 6) {
        u32 r = rd16(d), c = rd16(d + 2), i;
        for (i = 4; i + 2 <= len - 2; i += 2, c++) if (style_of(rd, rd16(d + i))) put_cell(rd, si, r, c, rd16(d + i));
      } break;
      case 0x0006: if (len >= 22) {
        u32 r = rd16(d), c = rd16(d + 2), cce = rd16(d + 20);
        Cell *cell = put_cell(rd, si, r, c, rd16(d + 4));
        char *ft = NULL;
        lastf = NULL;
        if (!cell) break;
        /* the last result */
        if (rd16(d + 12) == 0xFFFF) {
          if (d[6] == 1) cell->v = val_bool(d[8] != 0);
          else if (d[6] == 2) cell->v = val_err(errcode(d[8]));
          else if (d[6] == 3) cell->v = val_str("");
          else { cell->v = val_str(""); lastf = cell; lastr = (i32)r; lastc = (i32)c; }    /* (text: in the STRING record next) */
        } else cell->v = val_num(rddbl(d + 6));
        if (22 + cce <= len) {
          if (cce >= 5 && d[22] == 0x01) {
            /* ptgExp: the shared or array formula anchored at that cell */
            u32 ar = rd16(d + 23), ac = rd16(d + 25);
            for (k = 0; k < nshr; k++)
              if (shr[k].rgce && shr[k].r1 == (i32)ar && (shr[k].c1 & 0xFFFF) == (i32)ac) {
                if (shr[k].c1 & 0x10000) { if (r == ar && c == ac) ft = rpn(rd, shr[k].rgce, shr[k].cce, shr[k].extra, shr[k].nextra, 0, (i32)r, (i32)c); }
                else ft = rpn(rd, shr[k].rgce, shr[k].cce, shr[k].extra, shr[k].nextra, 1, (i32)r, (i32)c);
                break;
              }
          } else ft = rpn(rd, d + 22, cce, d + 22 + cce, len - 22 - cce, 0, (i32)r, (i32)c);
        }
        if (ft) {
          int pos;
          Formula *f = formula_parse(rd->b, si, ft, &pos);
          cell = sheet_cell(s, (i32)r, (i32)c);
          if (f && cell) cell->f = f; else formula_free(f);
          free(ft);
        }
      } break;
      case 0x0207: if (len >= 3 && lastf) {
        u32 used;
        char *t = xlstr(d, len, 2, &used);
        Cell *c = sheet_cell(s, lastr, lastc);
        if (c) { val_free(&c->v); c->v = val_str(t ? t : ""); }
        free(t);
        lastf = NULL;
      } break;
      case 0x0208: if (len >= 16) {
        u32 r = rd16(d), h = rd16(d + 6), g = rd32(d + 12);
        Row *row;
        if (r >= MAXROW) break;
        if (g & 0xF7) {
          if (!(row = sheet_row(s, (i32)r, 1))) break;
          if ((g & 0x40) && !(h & 0x8000)) { row->height = (u16)((h & 0x7FFF) / 20.0 * 4 / 3 + 0.5); row->custom = 1; }
          if (g & 0x20) row->hidden = 1;
          row->outline = (u8)(g & 7); row->collapsed = (u8)((g >> 4) & 1);
          if (g & 0x80) row->style = (u16)style_of(rd, (g >> 16) & 0xFFF);
        }
      } break;
      case 0x007D: if (len >= 10) {
        u32 c1 = rd16(d), c2 = rd16(d + 2), w = rd16(d + 4), xf = rd16(d + 6), g = rd16(d + 8), c;
        if (c2 >= 256) c2 = 255;
        for (c = c1; c <= c2; c++) {
          ColInfo *ci = sheet_col(s, (i32)c, 1);
          if (!ci) break;
          ci->width = (u16)(w / 256.0 * 7 + 0.5); ci->custom = 1;
          if (g & 1) ci->hidden = 1;
          ci->outline = (u8)((g >> 8) & 7); ci->collapsed = (u8)((g >> 12) & 1);
          if (style_of(rd, xf)) ci->style = (u16)style_of(rd, xf);
        }
      } break;
      case 0x0055: if (len >= 2) { u32 w = rd16(d); if (w > 0 && w < 256) s->defwidth = (u16)(w * 7 + 5); } break;
      case 0x0225: if (len >= 4) { u32 h = rd16(d + 2); if (h > 0 && h < 8000) s->defheight = (u16)(h / 20.0 * 4 / 3 + 0.5); } break;
      case 0x00E5: if (len >= 2) {
        u32 n = rd16(d), i;
        for (i = 0; i < n && 2 + i * 8 + 8 <= len && s->nmerges < 100000; i++) {
          const u8 *m = d + 2 + i * 8;
          Rect *nm = (Rect *)realloc(s->merges, (size_t)(s->nmerges + 1) * sizeof(Rect));
          if (!nm) break;
          s->merges = nm;
          nm[s->nmerges].r1 = (i32)rd16(m); nm[s->nmerges].r2 = (i32)rd16(m + 2); nm[s->nmerges].c1 = (i32)rd16(m + 4); nm[s->nmerges].c2 = (i32)rd16(m + 6);
          if (nm[s->nmerges].r2 >= nm[s->nmerges].r1 && nm[s->nmerges].c2 >= nm[s->nmerges].c1) s->nmerges++;
        }
      } break;
      case 0x023E: if (len >= 10) {
        u32 g = rd16(d);
        s->gridlines = (u8)((g >> 1) & 1); s->headings = (u8)((g >> 2) & 1); s->showzeros = (u8)((g >> 4) & 1);
        if (g & 0x200) rd->b->active = si;
        if (len >= 14 && rd16(d + 12) >= 10 && rd16(d + 12) <= 400) s->zoom = (u16)rd16(d + 12);
      } break;
      case 0x00A0: if (len >= 4 && rd16(d + 2)) { u32 z = rd16(d) * 100 / rd16(d + 2); if (z >= 10 && z <= 400) s->zoom = (u16)z; } break;
      case 0x0041: if (len >= 4) { s->freezec = (i32)rd16(d); s->freezer = (i32)rd16(d + 2); if (s->freezec >= MAXCOL) s->freezec = 0; if (s->freezer >= MAXROW) s->freezer = 0; } break;
      case 0x0081: if (len >= 2) { u32 g = rd16(d); s->sumabove = (u8)!((g >> 6) & 1); s->sumleft = (u8)!((g >> 7) & 1); } break;
      case 0x0012: if (len >= 2 && rd16(d)) { s->protect = 1; s->protallow = PA_DEFAULT; } break;
      case 0x0013: if (len >= 2) s->pwhash = (u16)rd16(d); break;
      case 0x01BE: read_dv(rd, si, d, len); break;
      }
      q = next;
    }
  }
  if (!s->protect) s->pwhash = 0;
  for (k = 0; k < nshr; k++) { free(shr[k].rgce); free(shr[k].extra); }
  free(shr);
}

/* ------------------------------------------------------------------ the workbook */
Book *xls_load(const u8 *data, size_t len, char *err, size_t errcap)
{
  Cfb cfb;
  Rd rd;
  u8 *stream;
  size_t n, o = 0;
  u32 id, rl;
  const u8 *d;
  int i, globals = 1, nbof = 0;
  Book *b;
  if (err && errcap) err[0] = 0;
  if (!cfb_open(&cfb, data, len)) { if (err) snprintf(err, errcap, "not an Excel 97-2003 file (no compound document)"); return NULL; }
  stream = cfb_stream(&cfb, "Workbook", &n);
  if (!stream) {
    stream = cfb_stream(&cfb, "Book", &n);
    cfb_close(&cfb);
    free(stream);
    if (err) snprintf(err, errcap, stream ? "an Excel 5/95 workbook: too old for Ledger (save it as .xlsx)" : "no workbook inside this file");
    return NULL;
  }
  cfb_close(&cfb);
  memset(&rd, 0, sizeof rd);
  rd.s = stream; rd.len = n; rd.selfsup = -1;
  memcpy(rd.pal, defpal, sizeof rd.pal);
  if (!rec(&rd, 0, &id, &d, &rl) || id != 0x0809 || rl < 4 || rd16(d) != 0x0600) {
    free(stream);
    if (err) snprintf(err, errcap, "this .xls isn't in Excel 97-2003's format (BIFF8)");
    return NULL;
  }
  if (!(b = book_new())) { free(stream); return NULL; }
  rd.b = b;
  /* the globals: up to their EOF */
  while (globals && rec(&rd, o, &id, &d, &rl)) {
    switch (id) {
    case 0x0809: nbof++; break;
    case 0x000A: globals = 0; break;
    case 0x002F: book_free(b); b = NULL; if (err) snprintf(err, errcap, "the workbook is protected with a password (encrypted)"); globals = 0; break;
    case 0x0022: if (rl >= 2 && rd16(d)) b->date1904 = 1; break;
    case 0x0092: if (rl >= 2) { u32 cnt = rd16(d), k; for (k = 0; k < cnt && k < 56 && 2 + k * 4 + 4 <= rl; k++) rd.pal[8 + k] = ((u32)d[2 + k * 4] << 16) | ((u32)d[3 + k * 4] << 8) | d[4 + k * 4]; } break;
    case 0x0031: read_font(&rd, d, rl); break;
    case 0x041E: if (rl >= 5 && rd.nfmt < 500) {
      void *nf = realloc(rd.fmts, (size_t)(rd.nfmt + 1) * sizeof *rd.fmts);
      u32 used;
      if (!nf) break;
      rd.fmts = nf;
      rd.fmts[rd.nfmt].id = (u16)rd16(d);
      rd.fmts[rd.nfmt].code = xlstr(d + 2, rl - 2, 2, &used);
      if (rd.fmts[rd.nfmt].code) rd.nfmt++;
    } break;
    case 0x00E0: read_xf(&rd, d, rl); break;
    case 0x0085: if (rl >= 8 && rd.nsh < MAXSHEETS) {
      void *ns = realloc(rd.sh, (size_t)(rd.nsh + 1) * sizeof(SheetEnt));
      u32 used;
      char *nm;
      if (!ns) break;
      rd.sh = ns;
      rd.sh[rd.nsh].off = rd32(d); rd.sh[rd.nsh].vis = d[4]; rd.sh[rd.nsh].type = d[5];
      nm = xlstr(d + 6, rl - 6, 1, &used);
      strncpy(rd.sh[rd.nsh].name, nm ? nm : "Sheet", 31); rd.sh[rd.nsh].name[31] = 0;
      free(nm);
      rd.sh[rd.nsh].book = -1;
      rd.nsh++;
    } break;
    case 0x00FC: read_sst(&rd, o, d, rl); break;
    case 0x01AE: if (rl >= 4) { if (rd16(d + 2) == 0x0401) rd.selfsup = rd.nsup; rd.nsup++; } break;
    case 0x0023: if (rl >= 7 && rd.nextn < 5000) {
      void *nn = realloc(rd.extnames, (size_t)(rd.nextn + 1) * sizeof(char *));
      u32 used;
      if (!nn) break;
      rd.extnames = nn;
      rd.extnames[rd.nextn++] = xlstr(d + 6, rl - 6, 1, &used);
    } break;
    case 0x0017: if (rl >= 2) {
      u32 cnt = rd16(d), k;
      free(rd.xti);
      rd.xti = (Xti *)calloc((size_t)cnt + 1, sizeof(Xti));
      for (k = 0; rd.xti && k < cnt && 2 + k * 6 + 6 <= rl; k++) {
        rd.xti[k].sup = (int)rd16(d + 2 + k * 6);
        rd.xti[k].first = (int)(i16)(u16)rd16(d + 4 + k * 6); rd.xti[k].last = (int)(i16)(u16)rd16(d + 6 + k * 6);
        rd.nxti = (int)k + 1;
      }
    } break;
    case 0x0018: if (rl >= 14 && rd.nnames < 10000) {
      /* a defined name (its formula read after the sheets exist) */
      void *nn = realloc(rd.names, (size_t)(rd.nnames + 1) * 2 * sizeof(char *));
      u32 cch = d[3], flags = rd16(d), used = 0;
      Buf nm;
      if (!nn) break;
      rd.names = nn;
      buf_init(&nm);
      if (14 + 1 + cch <= rl) {
        int wide = d[14] & 1;
        if (flags & 0x20) {
          /* built-in names (print areas and such): not kept */
          buf_adds(&nm, "_xlnm.");
          put_chars(&nm, (const u8 *)"?", 1, 0);
        } else put_chars(&nm, d + 15, cch, wide);
        used = 15 + cch * (wide ? 2 : 1);
      }
      rd.names[rd.nnames] = nm.data ? buf_take(&nm) : str_dup("");
      rd.nnames++;
      (void)used;
    } break;
    case 0x0012: if (rl >= 2 && rd16(d)) b->lockstruct = 1; break;
    case 0x0013: if (rl >= 2 && b->lockstruct) b->bookpwhash = (u16)rd16(d); break;
    }
    o += 4 + rl;
  }
  if (!b) goto done;
  /* the sheets */
  {
    int made = 0;
    for (i = 0; i < rd.nsh; i++) {
      char nm[32];
      if (rd.sh[i].type != 0) continue;                    /* worksheets only (not charts or macros) */
      strncpy(nm, rd.sh[i].name, 31); nm[31] = 0;
      if (!made) { if (!book_rename_sheet(b, 0, nm)) { char u[32]; book_unique_sheet_name(b, nm, u); book_rename_sheet(b, 0, u); } rd.sh[i].book = 0; }
      else { int at = book_add_sheet(b, nm, -1); if (at < 0) { char u[32]; book_unique_sheet_name(b, nm, u); at = book_add_sheet(b, u, -1); } rd.sh[i].book = at; }
      if (rd.sh[i].book >= 0) { b->sh[rd.sh[i].book]->hidden = (u8)(rd.sh[i].vis != 0); made++; }
    }
    if (!made) { book_free(b); b = NULL; if (err) snprintf(err, errcap, "the workbook has no worksheets"); goto done; }
  }
  /* (formulas name sheets by their BOUNDSHEET number: keep the names so; Ledger's indexes differ only when
     chart sheets are skipped, and then references by name still resolve through formula_parse) */
  b->active = 0;
  for (i = 0; i < rd.nsh; i++) {
    u32 sid;
    if (rd.sh[i].book < 0 || rd.sh[i].off + 4 > n) continue;
    if (!rec(&rd, rd.sh[i].off, &sid, &d, &rl) || sid != 0x0809) continue;
    read_sheet(&rd, rd.sh[i].book, rd.sh[i].off + 4 + rl);
  }
  /* defined names: their formulas (they may refer to the sheets) */
  {
    size_t q = 0;
    int ni = 0;
    while (rec(&rd, q, &id, &d, &rl)) {
      if (id == 0x000A) break;
      if (id == 0x0018 && rl >= 14 && ni < rd.nnames) {
        u32 flags = rd16(d), cch = d[3], cce = rd16(d + 4), itab = rd16(d + 8);
        int wide = rl > 14 ? d[14] & 1 : 0;
        u32 at = 15 + cch * (wide ? 2 : 1);
        if (!(flags & 0x20) && !(flags & 1) && at + cce <= rl && rd.names[ni][0]) {
          char *ft = rpn(&rd, d + at, cce, d + at + cce, rl - at - cce, 0, 0, 0);
          int sheet = SHEET_SELF;
          if (itab >= 1 && (int)itab <= rd.nsh && rd.sh[itab - 1].book >= 0) sheet = rd.sh[itab - 1].book;
          if (ft) { book_define_name(b, rd.names[ni], ft, sheet); free(ft); }
        }
        ni++;
      }
      q += 4 + rl;
    }
  }
  book_forget_undo(b);
  if (b->active >= b->nsh) b->active = 0;
  book_recalc(b);
  b->dirty = 0;
done:
  for (i = 0; i < rd.nsst; i++) free(rd.sst[i]);
  free(rd.sst);
  for (i = 0; i < rd.nextn; i++) free(rd.extnames[i]);
  free(rd.extnames);
  for (i = 0; i < rd.nnames; i++) free(rd.names[i]);
  free(rd.names);
  for (i = 0; i < rd.nfmt; i++) free(rd.fmts[i].code);
  free(rd.fmts); free(rd.xfstyle); free(rd.fontmap); free(rd.sh); free(rd.xti);
  free(stream);
  return b;
}
