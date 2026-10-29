/*
 * book.c - values, dates, the workbook, its sheets and cells, styles, editing with undo, and rows
 * and columns going in and out (formulas follow). See book.h.
 */
#include "book.h"
#include "table.h"
#include "pivot.h"
#include "calc.h"
#include "cond.h"
#include "pagesetup.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>
#include <ctype.h>

int ledger_date_order = 0;        /* how 3/4/2026 reads: 0 day/month/year (UK, Australia), 1 month/day/year (US) */

/* ------------------------------------------------------------------ values */
Value val_empty(void) { Value v; memset(&v, 0, sizeof v); return v; }
Value val_num(double n) { Value v = val_empty(); v.t = V_NUM; v.n = n; return v; }
Value val_bool(int b) { Value v = val_empty(); v.t = V_BOOL; v.e = (u8)(b != 0); return v; }
Value val_err(int e) { Value v = val_empty(); v.t = V_ERR; v.e = (u8)e; return v; }
/* a string value owning a copy of n bytes of s (an error value if out of memory) */
Value val_strn(const char *s, size_t n)
{
  Value v = val_empty();
  v.s = str_ndup(s ? s : "", s ? n : 0);
  if (!v.s) return val_err(E_NUM);
  v.t = V_STR;
  return v;
}
Value val_str(const char *s) { return val_strn(s, s ? strlen(s) : 0); }
/* a deep copy, strings and arrays included */
Value val_copy(const Value *v)
{
  Value c = *v;
  if (v->t == V_STR) { c.s = str_dup(v->s ? v->s : ""); if (!c.s) return val_err(E_NUM); }
  else if (v->t == V_ARR && v->a) {
    int i, n = v->a->rows * v->a->cols;
    c.a = arr_new(v->a->rows, v->a->cols);
    if (!c.a) return val_err(E_NUM);
    for (i = 0; i < n; i++) c.a->v[i] = val_copy(&v->a->v[i]);
  }
  else if (v->t == V_LAMBDA) calc_lambda_ref(v->a, 1);
  return c;
}
void val_free(Value *v)
{
  if (v->t == V_STR) free(v->s);
  else if (v->t == V_ARR) arr_free(v->a);
  else if (v->t == V_LAMBDA) calc_lambda_ref(v->a, -1);
  *v = val_empty();
}
/* an array of empties; NULL when larger than a million elements (hostile sizes) or out of memory */
Arr *arr_new(int rows, int cols)
{
  Arr *a;
  if (rows <= 0 || cols <= 0 || (long)rows * cols > 1000000L) return NULL;
  if (!(a = (Arr *)calloc(1, sizeof *a))) return NULL;
  if (!(a->v = (Value *)calloc((size_t)rows * cols, sizeof(Value)))) { free(a); return NULL; }
  a->rows = rows; a->cols = cols;
  return a;
}
void arr_free(Arr *a)
{
  int i;
  if (!a) return;
  for (i = 0; i < a->rows * a->cols; i++) val_free(&a->v[i]);
  free(a->v);
  free(a);
}
static const char *const errnames[E_N] = { "", "#NULL!", "#DIV/0!", "#VALUE!", "#REF!", "#NAME?", "#NUM!", "#N/A",
                                           "#GETTING_DATA", "#SPILL!", "#CALC!" };
const char *err_name(int e) { return e > 0 && e < E_N ? errnames[e] : "#VALUE!"; }
/* an error literal at s (case-insensitive): its E_ and length */
int err_parse(const char *s, int *len)
{
  int e;
  for (e = 1; e < E_N; e++) {
    size_t n = strlen(errnames[e]), i;
    for (i = 0; i < n && s[i] && toupper((u8)s[i]) == errnames[e][i]; i++) ;
    if (i == n) { if (len) *len = (int)n; return e; }
  }
  return 0;
}

/* ------------------------------------------------------------------ dates */
/* days since 1970-01-01 of a proleptic Gregorian date (Howard Hinnant's days_from_civil) */
static long days_from_civil(long y, long m, long d)
{
  long era, yoe, doy, doe;
  y -= m <= 2;
  era = (y >= 0 ? y : y - 399) / 400;
  yoe = y - era * 400;
  doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
  doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return era * 146097 + doe - 719468;
}
#define EPOCH_1899_12_30 (-25569L)       /* days_from_civil(1899, 12, 30) */
/* see book.h: serial numbers count days from 1899-12-30, except that Excel (for Lotus's sake) keeps
   a 29 February 1900, so dates before March 1900 are one less */
double date_serial(int y, int m, int d)
{
  long yy, mm, n;
  if (y >= 0 && y < 1900) y += 1900;               /* DATE(26,1,1) is 1926 */
  /* months beyond 12 (or below 1) move the year */
  yy = y + (m - 1 >= 0 ? (m - 1) / 12 : -((12 - m) / 12));
  mm = ((m - 1) % 12 + 12) % 12 + 1;
  n = days_from_civil(yy, mm, 1) - EPOCH_1899_12_30 + (d - 1);
  if (n < 61) n--;                                 /* before 1 March 1900 (serial 61) */
  return (double)n;
}
void serial_ymd(double serial, int *y, int *m, int *d)
{
  long n = (long)floor(serial), z, era, doe, yoe, doy, mp, yy, mm;
  if (n == 60) { *y = 1900; *m = 2; *d = 29; return; }
  if (n < 1) { *y = 1900; *m = 1; *d = 0; return; }
  if (n < 60) n++;
  z = n + EPOCH_1899_12_30 + 719468;               /* civil_from_days */
  era = (z >= 0 ? z : z - 146096) / 146097;
  doe = z - era * 146097;
  yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
  yy = yoe + era * 400;
  doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
  mp = (5 * doy + 2) / 153;
  *d = (int)(doy - (153 * mp + 2) / 5 + 1);
  mm = mp < 10 ? mp + 3 : mp - 9;
  *m = (int)mm;
  *y = (int)(yy + (mm <= 2));
}
int serial_weekday(double serial)
{
  long n = (long)floor(serial);
  /* WEEKDAY(1) is 1 (Sunday) in Excel, and every serial moves on one day; the phantom 29 Feb 1900
     makes dates before it disagree with the real calendar, as in Excel */
  return (int)(((n - 1) % 7 + 7) % 7);
}

/* ------------------------------------------------------------------ reading numbers */
static const char *const months[12] = { "jan", "feb", "mar", "apr", "may", "jun", "jul", "aug", "sep", "oct", "nov", "dec" };
/* a month name (or its first three letters) at s; 1-12 and *len, 0 if none */
static int month_name(const char *s, int *len)
{
  static const char *const full[12] = { "january", "february", "march", "april", "may", "june", "july", "august",
                                        "september", "october", "november", "december" };
  int m, i;
  for (m = 0; m < 12; m++) {
    for (i = 0; full[m][i] && tolower((u8)s[i]) == full[m][i]; i++) ;
    if (!full[m][i] && !isalpha((u8)s[i])) { *len = i; return m + 1; }
    if (i >= 3 && !isalpha((u8)s[i])) { *len = i; return m + 1; }
    for (i = 0; i < 3 && tolower((u8)s[i]) == months[m][i]; i++) ;
    if (i == 3 && !isalpha((u8)s[3])) { *len = 3; return m + 1; }
  }
  return 0;
}
static int read_int(const char **p, long *v)
{
  const char *s = *p;
  long n = 0;
  int k = 0;
  while (isdigit((u8)*s) && k < 9) { n = n * 10 + (*s - '0'); s++; k++; }
  if (!k || isdigit((u8)*s)) return 0;
  *v = n; *p = s;
  return k;
}
/* a time of day at *p ("3:30", "15:30:05", "3:30 pm", "3 PM"): its fraction of a day */
static int read_time(const char **p, double *frac)
{
  const char *s = *p;
  long h, m = 0;
  double sec = 0;
  int pm = -1;
  if (!read_int(&s, &h)) return 0;
  if (*s == ':') {
    s++;
    if (!read_int(&s, &m) || m > 59) return 0;
    if (*s == ':') {
      long si;
      s++;
      if (!read_int(&s, &si) || si > 59) return 0;
      sec = (double)si;
      if (*s == '.' && isdigit((u8)s[1])) { double f = 0.1; s++; while (isdigit((u8)*s)) { sec += (*s - '0') * f; f /= 10; s++; } }
    }
  } else {
    /* a bare hour needs AM/PM */
    const char *t = s;
    while (*t == ' ') t++;
    if (!(tolower((u8)t[0]) == 'a' || tolower((u8)t[0]) == 'p')) return 0;
  }
  while (*s == ' ') s++;
  if ((tolower((u8)s[0]) == 'a' || tolower((u8)s[0]) == 'p') && (tolower((u8)s[1]) == 'm' || !isalpha((u8)s[1]))) {
    pm = tolower((u8)s[0]) == 'p';
    s += tolower((u8)s[1]) == 'm' ? 2 : 1;
    if (h < 1 || h > 12) return 0;
    if (h == 12) h = 0;
    if (pm) h += 12;
  }
  if (pm < 0 && h > 9999) return 0;
  *frac = (h * 3600.0 + m * 60.0 + sec) / 86400.0;
  *p = s;
  return 1;
}
/* a date at *p: ISO 2026-10-27, 27/10/2026 (or 10/27/2026 in month-first order), 27-Oct-2026,
   27 October 2026, Oct 27, 2026, and short forms without the year (this year) */
static int read_date(const char **p, double *serial)
{
  const char *s = *p;
  long a, b, c = -1;
  int ml, m;
  int y, mo, d;
  if ((m = month_name(s, &ml))) {
    /* "Oct 27, 2026" or "Oct 2026" or "Oct-26" */
    s += ml;
    if (*s == '.') s++;
    while (*s == ' ' || *s == '-') s++;
    if (!read_int(&s, &a)) return 0;
    if (*s == ',') s++;
    while (*s == ' ') s++;
    if (isdigit((u8)*s) && read_int(&s, &b)) { y = (int)b; d = (int)a; }
    else if (a > 31) { y = (int)a; d = 1; }
    else { y = -1; d = (int)a; }
    mo = m;
  } else {
    char sep;
    if (!read_int(&s, &a)) return 0;
    sep = *s;
    if (sep != '/' && sep != '-' && sep != ' ' && sep != '.') return 0;
    s++;
    while (sep == ' ' && *s == ' ') s++;
    if ((m = month_name(s, &ml))) {
      /* 27-Oct-2026, 27 October 2026, 27-Oct */
      s += ml;
      if (*s == '.') s++;
      if (*s == sep || (sep == ' ' && *s == ',')) {
        s++;
        while (*s == ' ') s++;
        if (!read_int(&s, &c)) return 0;
      }
      d = (int)a; mo = m; y = c >= 0 ? (int)c : -1;
    } else {
      if (sep == ' ' || sep == '.') return 0;
      if (!read_int(&s, &b)) return 0;
      if (*s == sep) { s++; if (!read_int(&s, &c)) return 0; }
      if (a > 31) { y = (int)a; mo = (int)b; d = (int)c; if (c < 0) return 0; }      /* ISO */
      else if (ledger_date_order == 1) { mo = (int)a; d = (int)b; y = (int)c; }
      else { d = (int)a; mo = (int)b; y = (int)c; }
      if (c < 0) y = -1;
    }
  }
  if (y < 0) {
    /* this year */
    y = 2026;
    {
      extern int ledger_this_year;
      if (ledger_this_year) y = ledger_this_year;
    }
  }
  else if (y < 30) y += 2000;
  else if (y < 100) y += 1900;
  if (mo < 1 || mo > 12 || d < 1 || d > 31 || y > 9999) return 0;
  {
    static const int mdays[12] = { 31, 29, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
    if (d > mdays[mo - 1]) return 0;
    if (mo == 2 && d == 29 && !((y % 4 == 0 && y % 100 != 0) || y % 400 == 0) && y != 1900) return 0;
  }
  *serial = date_serial(y, mo, d);
  *p = s;
  return 1;
}
int ledger_this_year;

/* see book.h */
int parse_number(const char *s, double *out, int *isdate)
{
  const char *p = s;
  int neg = 0, paren = 0, pct = 0, digits = 0, cur = 0;
  double v = 0;
  if (isdate) *isdate = 0;
  while (*p == ' ') p++;
  if (!*p) return 0;
  /* dates and times first (they also start with digits) */
  {
    const char *q = p;
    double serial, frac;
    if (read_date(&q, &serial)) {
      while (*q == ' ') q++;
      if (!*q) { *out = serial; if (isdate) *isdate = 1; return 1; }
      if (read_time(&q, &frac)) {
        while (*q == ' ') q++;
        if (!*q) { *out = serial + frac; if (isdate) *isdate = 2; return 1; }
      }
    }
    q = p;
    if (read_time(&q, &frac)) {
      while (*q == ' ') q++;
      if (!*q) { *out = frac; if (isdate) *isdate = 3; return 1; }
    }
  }
  if (*p == '(') { paren = 1; p++; }
  if (*p == '-') { neg = 1; p++; } else if (*p == '+') p++;
  if (*p == '$') { cur = 1; p++; }
  else if ((u8)p[0] == 0xC2 && (u8)p[1] == 0xA3) { cur = 1; p += 2; }                         /* £ */
  else if ((u8)p[0] == 0xE2 && (u8)p[1] == 0x82 && (u8)p[2] == 0xAC) { cur = 1; p += 3; }       /* € */
  if (!neg && *p == '-') { neg = 1; p++; }
  /* the integer part, with optional groups of three after commas */
  {
    int group = -1;
    while (isdigit((u8)*p) || (*p == ',' && digits)) {
      if (*p == ',') {
        if (group >= 0 && group != 3) return 0;
        group = 0;
        if (!isdigit((u8)p[1])) return 0;
      } else {
        v = v * 10 + (*p - '0');
        digits++;
        if (group >= 0) group++;
      }
      p++;
    }
    if (group >= 0 && group != 3) return 0;
  }
  if (*p == '.') {
    double f = 0.1;
    p++;
    while (isdigit((u8)*p)) { v += (*p - '0') * f; f /= 10; p++; digits++; }
  }
  if (!digits) return 0;
  if ((*p == 'e' || *p == 'E') && (isdigit((u8)p[1]) || ((p[1] == '+' || p[1] == '-') && isdigit((u8)p[2])))) {
    int eneg = 0, ex = 0;
    p++;
    if (*p == '+') p++; else if (*p == '-') { eneg = 1; p++; }
    while (isdigit((u8)*p)) { if (ex < 400) ex = ex * 10 + (*p - '0'); p++; }
    v *= pow(10.0, eneg ? -ex : ex);
  }
  if (*p == '%') { pct = 1; p++; }
  if (paren) { if (*p != ')' || neg) return 0; p++; neg = 1; }
  while (*p == ' ') p++;
  if (*p) return 0;
  if (pct) v /= 100;
  *out = neg ? -v : v;
  if (isdate) *isdate = pct ? 4 : cur ? 5 : 0;
  return 1;
}

/* see book.h: General format. Up to 11 characters for big and small numbers, as Excel shows them in
   a standard column: integers below 1e11 in full, otherwise 10 significant digits, scientific when
   the number is very large or very small */
void num_general(double n, char *out, int width)
{
  char buf[64];
  double a = fabs(n);
  if (width < 1 || width > 30) width = 11;
  if (n == 0) { strcpy(out, "0"); return; }
  if (!(a == a) || a > 1e308) { strcpy(out, "#NUM!"); return; }
  if (a >= 1e11 || a < 1e-9) {
    /* scientific: 1.23457E+11 */
    int digits = width - 5 - (n < 0);
    char *e, *q;
    if (digits < 1) digits = 1;
    if (digits > 14) digits = 14;
    sprintf(buf, "%.*E", digits - 1, n);
    /* trim zeros in the mantissa and use two-digit exponents */
    e = strchr(buf, 'E');
    if (e) {
      char exp[16];
      int ev = atoi(e + 1);
      *e = 0;
      if (strchr(buf, '.')) { q = buf + strlen(buf) - 1; while (*q == '0') *q-- = 0; if (*q == '.') *q = 0; }
      sprintf(exp, "E%c%02d", ev < 0 ? '-' : '+', ev < 0 ? -ev : ev);
      strcat(buf, exp);
    }
    strcpy(out, buf);
    return;
  }
  {
    /* fixed: as many decimals as fit in width, at most 10 significant digits */
    int intdig = a >= 1 ? (int)floor(log10(a)) + 1 : 1, dec, sig = 10;
    char *q;
    dec = width - intdig - (n < 0) - 1;
    if (a < 1) {
      int lead = (int)floor(-log10(a));                /* zeros after the point before the first digit */
      if (dec > lead + sig) dec = lead + sig;
    } else if (dec > sig - intdig) dec = sig - intdig;
    if (dec < 0) dec = 0;
    sprintf(buf, "%.*f", dec, n);
    if (strchr(buf, '.')) { q = buf + strlen(buf) - 1; while (*q == '0') *q-- = 0; if (*q == '.') *q = 0; }
    if (!strcmp(buf, "-0")) strcpy(buf, "0");
    strcpy(out, buf);
  }
}

/* ------------------------------------------------------------------ addresses */
void col_name(i32 c, char *out)
{
  char tmp[8];
  int n = 0;
  c++;
  while (c > 0 && n < 7) { int r = (int)((c - 1) % 26); tmp[n++] = (char)('A' + r); c = (c - 1) / 26; }
  while (n > 0) *out++ = tmp[--n];
  *out = 0;
}
int parse_col(const char *s, i32 *c)
{
  i32 v = 0;
  int n = 0;
  while (isalpha((u8)s[n]) && n < 4) { v = v * 26 + (toupper((u8)s[n]) - 'A' + 1); n++; }
  if (!n || isalpha((u8)s[n]) || v > MAXCOL) return 0;
  *c = v - 1;
  return n;
}
void ref_text(i32 r, i32 c, u8 absr, u8 absc, char *out)
{
  char col[8];
  col_name(c, col);
  sprintf(out, "%s%s%s%ld", absc ? "$" : "", col, absr ? "$" : "", (long)r + 1);
}
void rect_text(Rect r, char *out)
{
  ref_text(r.r1, r.c1, 0, 0, out);
  if (r.r2 != r.r1 || r.c2 != r.c1) { strcat(out, ":"); ref_text(r.r2, r.c2, 0, 0, out + strlen(out)); }
}
/* one cell address or column/row part at s: $A$1, A1, $A, 3 */
static int parse_cellpart(const char *s, i32 *r, i32 *c, u8 *absr, u8 *absc, int *kind)
{
  const char *p = s;
  i32 col = -1, row = -1;
  int n;
  u8 ac = 0, ar = 0;
  if (*p == '$') { ac = 1; p++; }
  if ((n = parse_col(p, &col))) p += n; else if (ac) { ac = 0; p = s; col = -1; }
  if (*p == '$') { ar = 1; p++; }
  if (isdigit((u8)*p)) {
    long v = 0;
    int k = 0;
    while (isdigit((u8)*p) && k < 8) { v = v * 10 + (*p - '0'); p++; k++; }
    if (isdigit((u8)*p) || v < 1 || v > MAXROW) return 0;
    row = (i32)(v - 1);
  } else if (ar) return 0;
  if (isalnum((u8)*p) || *p == '_' || *p == '(' || *p == '.') return 0;
  if (col < 0 && row < 0) return 0;
  *kind = col >= 0 && row >= 0 ? 0 : col >= 0 ? 1 : 2;     /* 0 cell, 1 column only, 2 row only */
  *r = row; *c = col; *absr = ar; *absc = ac;
  return (int)(p - s);
}
/* see book.h */
int parse_ref(Book *b, const char *s, u16 *sheet, Rect *r, u8 *flags)
{
  const char *p = s;
  i32 r1, c1, r2, c2;
  u8 ar1, ac1, ar2, ac2;
  int k1, k2, n;
  *sheet = SHEET_SELF;
  *flags = 0;
  /* Sheet!  or 'Sheet name'! */
  if (*p == '\'') {
    char name[64];
    int i = 0;
    p++;
    while (*p && i < 63) {
      if (*p == '\'') { if (p[1] == '\'') { name[i++] = '\''; p += 2; continue; } break; }
      name[i++] = *p++;
    }
    name[i] = 0;
    if (*p != '\'' || p[1] != '!') return 0;
    p += 2;
    if (!b || (n = book_find_sheet(b, name)) < 0) return 0;
    *sheet = (u16)n; *flags |= RF_SHEET;
  } else {
    const char *q = p;
    while (*q && (isalnum((u8)*q) || *q == '_' || *q == '.' || (u8)*q >= 0x80)) q++;
    if (*q == '!' && q > p) {
      char name[64];
      size_t len = (size_t)(q - p);
      if (len > 63) return 0;
      memcpy(name, p, len); name[len] = 0;
      if (!b || (n = book_find_sheet(b, name)) < 0) return 0;
      *sheet = (u16)n; *flags |= RF_SHEET;
      p = q + 1;
    }
  }
  if (!(n = parse_cellpart(p, &r1, &c1, &ar1, &ac1, &k1))) return 0;
  p += n;
  r2 = r1; c2 = c1; ar2 = ar1; ac2 = ac1; k2 = k1;
  if (*p == ':') {
    int m = parse_cellpart(p + 1, &r2, &c2, &ar2, &ac2, &k2);
    if (m && k2 == k1) p += 1 + m;
    else if (k1 != 0) return 0;
    else { r2 = r1; c2 = c1; k2 = k1; }
  } else if (k1 != 0) return 0;               /* a lone "A" or "3" isn't a reference */
  if (k1 == 1) { r1 = 0; r2 = MAXROW - 1; *flags |= RF_COLS; }
  if (k1 == 2) { c1 = 0; c2 = MAXCOL - 1; *flags |= RF_ROWS; }
  if (r1 > r2) { i32 t = r1; u8 a = ar1; r1 = r2; r2 = t; ar1 = ar2; ar2 = a; }
  if (c1 > c2) { i32 t = c1; u8 a = ac1; c1 = c2; c2 = t; ac1 = ac2; ac2 = a; }
  r->r1 = r1; r->c1 = c1; r->r2 = r2; r->c2 = c2;
  *flags |= (ar1 ? RF_ABSR1 : 0) | (ac1 ? RF_ABSC1 : 0) | (ar2 ? RF_ABSR2 : 0) | (ac2 ? RF_ABSC2 : 0);
  return (int)(p - s);
}

/* ------------------------------------------------------------------ sheets and cells */
/* a new empty sheet named name (NULL out of memory) */
static Sheet *sheet_new(const char *name)
{
  Sheet *s = (Sheet *)calloc(1, sizeof *s);
  if (!s) return NULL;
  strncpy(s->name, name, sizeof s->name - 1);
  s->defwidth = 64; s->defheight = 20;
  s->gridlines = 1; s->headings = 1; s->showzeros = 1;
  s->zoom = 100;
  s->filter.r1 = -1;
  return s;
}
static void cell_free_contents(Cell *c) { val_free(&c->v); formula_free(c->f); c->f = NULL; c->spill = 0; }
/* AutoFilter columns given back */
static void fcols_free(FilterCol *f, int n)
{
  int i, k;
  for (i = 0; i < n; i++) { for (k = 0; k < f[i].n; k++) free(f[i].vals[k]); free(f[i].vals); }
  free(f);
}
/* and copied (NULL out of memory, or none) */
static FilterCol *fcols_copy(const FilterCol *f, int n)
{
  FilterCol *c;
  int i, k;
  if (!n || !(c = (FilterCol *)calloc((size_t)n, sizeof *c))) return NULL;
  for (i = 0; i < n; i++) {
    c[i] = f[i];
    c[i].vals = f[i].n ? (char **)calloc((size_t)f[i].n, sizeof(char *)) : NULL;
    for (k = 0; k < f[i].n && c[i].vals; k++) c[i].vals[k] = str_dup(f[i].vals[k]);
  }
  return c;
}
/* notes given back, and copied (NULL out of memory, or none) */
static void notes_free(Note *n, int k)
{
  int i;
  for (i = 0; i < k; i++) { free(n[i].author); free(n[i].text); free(n[i].replies); }
  free(n);
}
static Note *notes_copy(const Note *n, int k)
{
  Note *c;
  int i;
  if (!k || !(c = (Note *)calloc((size_t)k, sizeof *c))) return NULL;
  for (i = 0; i < k; i++) { c[i] = n[i]; c[i].author = str_dup(n[i].author ? n[i].author : ""); c[i].text = str_dup(n[i].text ? n[i].text : ""); c[i].replies = n[i].replies ? str_dup(n[i].replies) : NULL; }
  return c;
}
/* links given back, and copied */
static void links_free(Link *n, int k)
{
  int i;
  for (i = 0; i < k; i++) { free(n[i].target); free(n[i].tip); }
  free(n);
}
static Link *links_copy(const Link *n, int k)
{
  Link *c;
  int i;
  if (!k || !(c = (Link *)calloc((size_t)k, sizeof *c))) return NULL;
  for (i = 0; i < k; i++) { c[i] = n[i]; c[i].target = str_dup(n[i].target ? n[i].target : ""); c[i].tip = str_dup(n[i].tip ? n[i].tip : ""); }
  return c;
}
static void sheet_free(Sheet *s)
{
  i32 r;
  int i;
  if (!s) return;
  for (r = 0; r < s->nrows; r++)
    if (s->rows[r]) {
      for (i = 0; i < s->rows[r]->n; i++) cell_free_contents(&s->rows[r]->c[i]);
      free(s->rows[r]->c);
      free(s->rows[r]);
    }
  free(s->rows);
  free(s->cols);
  free(s->merges);
  fcols_free(s->fcols, s->nfcols);
  cond_free_all(s->cfs, s->ncfs);
  notes_free(s->notes, s->nnotes);
  links_free(s->links, s->nlinks);
  tables_free(s->tables, s->ntables);
  pivots_free(s->pivots, s->npivots);
  page_free(s->page);
  free(s);
}
/* a deep copy of a sheet, for undo (NULL out of memory) */
static Sheet *sheet_clone(const Sheet *s)
{
  Sheet *c = (Sheet *)malloc(sizeof *c);
  i32 r;
  int i;
  if (!c) return NULL;
  *c = *s;
  c->rows = NULL; c->cols = NULL; c->merges = NULL; c->nrows = 0;
  c->fcols = fcols_copy(s->fcols, s->nfcols);
  if (!c->fcols) c->nfcols = 0;
  c->cfs = cond_copy_all(s->cfs, s->ncfs);
  if (!c->cfs) c->ncfs = 0;
  c->notes = notes_copy(s->notes, s->nnotes);
  if (!c->notes) c->nnotes = 0;
  c->links = links_copy(s->links, s->nlinks);
  if (!c->links) c->nlinks = 0;
  c->tables = tables_copy(s->tables, s->ntables);
  if (!c->tables) c->ntables = 0;
  c->pivots = pivots_copy(s->pivots, s->npivots);
  if (!c->pivots) c->npivots = 0;
  c->page = page_copy(s->page);
  if (s->nrows && !(c->rows = (Row **)calloc((size_t)s->nrows, sizeof(Row *)))) goto fail;
  c->nrows = s->nrows;
  for (r = 0; r < s->nrows; r++) {
    Row *src = s->rows[r], *dst;
    if (!src) continue;
    if (!(dst = (Row *)malloc(sizeof *dst))) goto fail;
    *dst = *src;
    dst->c = NULL; dst->cap = dst->n = 0;
    c->rows[r] = dst;
    if (src->n) {
      if (!(dst->c = (Cell *)malloc((size_t)src->n * sizeof(Cell)))) goto fail;
      dst->cap = src->n;
      for (i = 0; i < src->n; i++) {
        dst->c[i] = src->c[i];
        dst->c[i].v = val_copy(&src->c[i].v);
        dst->c[i].f = src->c[i].f ? formula_copy(src->c[i].f) : NULL;
        dst->n = i + 1;
      }
    }
  }
  if (s->ncols) {
    if (!(c->cols = (ColInfo *)malloc((size_t)s->ncols * sizeof(ColInfo)))) goto fail;
    memcpy(c->cols, s->cols, (size_t)s->ncols * sizeof(ColInfo));
  }
  if (s->nmerges) {
    if (!(c->merges = (Rect *)malloc((size_t)s->nmerges * sizeof(Rect)))) goto fail;
    memcpy(c->merges, s->merges, (size_t)s->nmerges * sizeof(Rect));
  }
  return c;
fail:
  c->ncols = 0; c->nmerges = 0;
  sheet_free(c);
  return NULL;
}
/* the index in row of column c, or where it would go (*found says which) */
static int row_find(const Row *row, i32 c, int *found)
{
  int lo = 0, hi = row->n;
  while (lo < hi) {
    int mid = (lo + hi) / 2;
    if (row->c[mid].col < c) lo = mid + 1; else hi = mid;
  }
  *found = lo < row->n && row->c[lo].col == c;
  return lo;
}
Cell *sheet_cell(Sheet *s, i32 r, i32 c)
{
  Row *row;
  int i, f;
  if (r < 0 || r >= s->nrows || c < 0 || c >= MAXCOL || !(row = s->rows[r])) return NULL;
  i = row_find(row, c, &f);
  return f ? &row->c[i] : NULL;
}
/* the row r, made if absent (NULL: out of range or out of memory) */
static Row *row_make(Sheet *s, i32 r)
{
  if (r < 0 || r >= MAXROW) return NULL;
  if (r >= s->nrows) {
    i32 n = s->nrows ? s->nrows : 64;
    Row **rows;
    while (n <= r) n = n * 2 > MAXROW ? MAXROW : n * 2;
    if (!(rows = (Row **)realloc(s->rows, (size_t)n * sizeof(Row *)))) return NULL;
    memset(rows + s->nrows, 0, (size_t)(n - s->nrows) * sizeof(Row *));
    s->rows = rows; s->nrows = n;
  }
  if (!s->rows[r] && !(s->rows[r] = (Row *)calloc(1, sizeof(Row)))) return NULL;
  return s->rows[r];
}
Cell *sheet_cell_make(Sheet *s, i32 r, i32 c)
{
  Row *row;
  int i, f;
  if (c < 0 || c >= MAXCOL || !(row = row_make(s, r))) return NULL;
  i = row_find(row, c, &f);
  if (f) return &row->c[i];
  if (row->n == row->cap) {
    int cap = row->cap ? row->cap * 2 : 8;
    Cell *nc = (Cell *)realloc(row->c, (size_t)cap * sizeof(Cell));
    if (!nc) return NULL;
    row->c = nc; row->cap = cap;
  }
  memmove(&row->c[i + 1], &row->c[i], (size_t)(row->n - i) * sizeof(Cell));
  memset(&row->c[i], 0, sizeof(Cell));
  row->c[i].col = (u16)c;
  /* a new cell takes its row's or column's style */
  row->c[i].style = row->style ? row->style : (c < s->ncols ? s->cols[c].style : 0);
  row->n++;
  return &row->c[i];
}
void sheet_cell_clear(Sheet *s, i32 r, i32 c)
{
  Row *row;
  int i, f;
  if (r < 0 || r >= s->nrows || !(row = s->rows[r])) return;
  i = row_find(row, c, &f);
  if (!f) return;
  cell_free_contents(&row->c[i]);
  memmove(&row->c[i], &row->c[i + 1], (size_t)(row->n - i - 1) * sizeof(Cell));
  row->n--;
}
i32 sheet_last_row(Sheet *s)
{
  i32 r;
  for (r = s->nrows - 1; r >= 0; r--) if (s->rows[r] && s->rows[r]->n) return r;
  return -1;
}
i32 sheet_last_col(Sheet *s)
{
  i32 r, best = -1;
  for (r = 0; r < s->nrows; r++)
    if (s->rows[r] && s->rows[r]->n && s->rows[r]->c[s->rows[r]->n - 1].col > best) best = s->rows[r]->c[s->rows[r]->n - 1].col;
  return best;
}
int sheet_cell_style(Sheet *s, i32 r, i32 c)
{
  Cell *cell = sheet_cell(s, r, c);
  if (cell) return cell->style;
  if (r >= 0 && r < s->nrows && s->rows[r] && s->rows[r]->style) return s->rows[r]->style;
  if (c >= 0 && c < s->ncols) return s->cols[c].style;
  return 0;
}
u16 sheet_col_width(Sheet *s, i32 c)
{
  if (c >= 0 && c < s->ncols) { if (s->cols[c].hidden) return 0; if (s->cols[c].width) return s->cols[c].width; }
  return s->defwidth;
}
u16 sheet_row_height(Sheet *s, i32 r)
{
  if (r >= 0 && r < s->nrows && s->rows[r]) { if (s->rows[r]->hidden || s->rows[r]->filtered) return 0; if (s->rows[r]->height) return s->rows[r]->height; }
  return s->defheight;
}
int sheet_merge_at(Sheet *s, i32 r, i32 c, Rect *m)
{
  int i;
  for (i = 0; i < s->nmerges; i++)
    if (r >= s->merges[i].r1 && r <= s->merges[i].r2 && c >= s->merges[i].c1 && c <= s->merges[i].c2) { if (m) *m = s->merges[i]; return 1; }
  return 0;
}
/* column info for c, made if absent */
static ColInfo *col_make(Sheet *s, i32 c)
{
  if (c < 0 || c >= MAXCOL) return NULL;
  if (c >= s->ncols) {
    i32 n = c + 16 > MAXCOL ? MAXCOL : c + 16;
    ColInfo *ci = (ColInfo *)realloc(s->cols, (size_t)n * sizeof(ColInfo));
    if (!ci) return NULL;
    memset(ci + s->ncols, 0, (size_t)(n - s->ncols) * sizeof(ColInfo));
    s->cols = ci; s->ncols = n;
  }
  return &s->cols[c];
}

/* ------------------------------------------------------------------ the book */
Book *book_new(void)
{
  Book *b = (Book *)calloc(1, sizeof *b);
  Font f;
  Style st;
  if (!b) return NULL;
  memset(&f, 0, sizeof f);
  strcpy(f.name, "Calibri"); f.size = 110; f.color = COLOR_AUTO;
  memset(&st, 0, sizeof st);
  st.locked = 1;
  if (book_font(b, &f) < 0 || book_style(b, &st) < 0 || book_add_sheet(b, "Sheet1", 0) < 0) { book_free(b); return NULL; }
  b->calcgen = 1;
  b->dirty = 0;
  return b;
}
static void undo_free_list(UndoStep *u);
void book_free(Book *b)
{
  int i;
  if (!b) return;
  for (i = 0; i < b->nsh; i++) sheet_free(b->sh[i]);
  free(b->sh);
  free(b->fonts);
  free(b->styles);
  for (i = 0; i < b->nfmts; i++) free(b->fmts[i]);
  free(b->fmts);
  for (i = 0; i < b->nnames; i++) { free(b->names[i].formula_text); formula_free(b->names[i].f); }
  free(b->names);
  undo_free_list(b->undo);
  undo_free_list(b->redo);
  undo_free_list(b->open);
  lx_free(b->lx);
  free(b);
}
int book_find_sheet(Book *b, const char *name)
{
  int i;
  for (i = 0; i < b->nsh; i++) if (str_ieq(b->sh[i]->name, name)) return i;
  return -1;
}
/* Excel's rules: 1-31 characters, none of : \ / ? * [ ], not starting or ending with ' */
static int sheet_name_ok(Book *b, const char *name, int except)
{
  size_t n = strlen(name);
  int i;
  if (!n || n > 31 || name[0] == '\'' || name[n - 1] == '\'' || strpbrk(name, ":\\/?*[]")) return 0;
  for (i = 0; i < b->nsh; i++) if (i != except && str_ieq(b->sh[i]->name, name)) return 0;
  return 1;
}
void book_unique_sheet_name(Book *b, const char *base, char *out)
{
  int k;
  if (!base) {
    for (k = b->nsh + 1; k < 10000; k++) { sprintf(out, "Sheet%d", k); if (book_find_sheet(b, out) < 0) return; }
    return;
  }
  if (sheet_name_ok(b, base, -1)) { strcpy(out, base); return; }
  for (k = 2; k < 10000; k++) {
    char tail[16];
    size_t room;
    sprintf(tail, " (%d)", k);
    room = 31 - strlen(tail);
    strncpy(out, base, room); out[room] = 0;
    strcat(out, tail);
    if (book_find_sheet(b, out) < 0) return;
  }
}

/* ------------------------------------------------------------------ undo */
typedef struct { u16 sheet; i32 r, c; u8 present; Cell cell; } CellRec;
enum { U_CELLS, U_SHEET, U_BOOK };
struct UndoStep {
  int kind;
  int sheet;                      /* where it happened, for moving the cursor back */
  Rect area;
  CellRec *rec; int nrec, caprec;
  Sheet *snap;                    /* U_SHEET: the sheet as it was */
  Sheet **book; int nbook, active;   /* U_BOOK: every sheet as it was */
  UndoStep *next;
};
static void undo_free_step(UndoStep *u)
{
  int i;
  for (i = 0; i < u->nrec; i++) if (u->rec[i].present) cell_free_contents(&u->rec[i].cell);
  free(u->rec);
  sheet_free(u->snap);
  for (i = 0; i < u->nbook; i++) sheet_free(u->book[i]);
  free(u->book);
  free(u);
}
static void undo_free_list(UndoStep *u)
{
  while (u) { UndoStep *n = u->next; undo_free_step(u); u = n; }
}
void book_begin(Book *b)
{
  if (b->nesting++ == 0 && !b->open) b->open = (UndoStep *)calloc(1, sizeof(UndoStep));
}
/* finish the open step: kept if it changed anything; redo is forgotten; at most 100 steps kept */
void book_end(Book *b)
{
  UndoStep *u;
  if (b->nesting > 0 && --b->nesting > 0) return;
  u = b->open;
  b->open = NULL;
  b->changes++;
  if (!u) return;
  if (u->kind == U_CELLS && !u->nrec) { undo_free_step(u); return; }
  u->next = b->undo;
  b->undo = u;
  undo_free_list(b->redo);
  b->redo = NULL;
  b->dirty = 1;
  if (++b->undo_depth > 100) {
    UndoStep *p = b->undo;
    int k = 1;
    while (p && k < 100) { p = p->next; k++; }
    if (p) { undo_free_list(p->next); p->next = NULL; }
    b->undo_depth = 100;
  }
}
/* note a cell before it changes (inside the open step; outside one, a step is opened and closed
   around the caller by the editing functions) */
static void undo_cell(Book *b, int sheet, i32 r, i32 c)
{
  UndoStep *u = b->open;
  Cell *cell;
  if (!u || u->kind != U_CELLS) return;
  if (u->nrec == u->caprec) {
    int cap = u->caprec ? u->caprec * 2 : 16;
    CellRec *nr = (CellRec *)realloc(u->rec, (size_t)cap * sizeof(CellRec));
    if (!nr) return;
    u->rec = nr; u->caprec = cap;
  }
  {
    CellRec *rec = &u->rec[u->nrec];
    memset(rec, 0, sizeof *rec);
    rec->sheet = (u16)sheet; rec->r = r; rec->c = c;
    if ((cell = sheet_cell(b->sh[sheet], r, c))) {
      rec->present = 1;
      rec->cell = *cell;
      rec->cell.v = val_copy(&cell->v);
      rec->cell.f = cell->f ? formula_copy(cell->f) : NULL;
    }
    if (!u->nrec) { u->sheet = sheet; u->area.r1 = u->area.r2 = r; u->area.c1 = u->area.c2 = c; }
    else {
      if (r < u->area.r1) u->area.r1 = r;
      if (r > u->area.r2) u->area.r2 = r;
      if (c < u->area.c1) u->area.c1 = c;
      if (c > u->area.c2) u->area.c2 = c;
    }
    u->nrec++;
  }
}
/* note a whole sheet before a structural change (rows, columns, sizes, merges) */
static void undo_sheet(Book *b, int sheet)
{
  UndoStep *u = b->open;
  if (!u) return;
  if (u->kind == U_CELLS && !u->nrec) {
    if ((u->snap = sheet_clone(b->sh[sheet]))) { u->kind = U_SHEET; u->sheet = sheet; }
  } else if (u->kind == U_CELLS || (u->kind == U_SHEET && u->sheet != sheet)) {
    /* mixed changes: fall back to the whole book */
  }
}
void book_note_sheet(Book *b, int sheet) { undo_sheet(b, sheet); }
/* note the whole book before sheets come, go or move */
static void undo_book(Book *b)
{
  UndoStep *u = b->open;
  int i;
  if (!u || u->kind == U_BOOK || u->nrec || u->snap) return;
  if (!(u->book = (Sheet **)calloc((size_t)b->nsh + 1, sizeof(Sheet *)))) return;
  for (i = 0; i < b->nsh; i++) if (!(u->book[i] = sheet_clone(b->sh[i]))) { u->nbook = i; return; }
  u->nbook = b->nsh;
  u->active = b->active;
  u->kind = U_BOOK;
}
void book_note_book(Book *b) { undo_book(b); }
/* apply step u (taking it off from), and push its opposite onto to */
static int undo_apply(Book *b, UndoStep **from, UndoStep **to, int *sheet, Rect *area)
{
  UndoStep *u = *from, *opp;
  int i;
  if (!u) return 0;
  if (!(opp = (UndoStep *)calloc(1, sizeof *opp))) return 0;
  *from = u->next;
  opp->kind = u->kind; opp->sheet = u->sheet; opp->area = u->area;
  if (u->kind == U_CELLS) {
    /* the records newest first, so the oldest record of a cell wins; each one's present state
       goes to the opposite step */
    for (i = u->nrec - 1; i >= 0; i--) {
      CellRec *rec = &u->rec[i];
      Cell *cur;
      if (rec->sheet >= b->nsh) continue;
      if (opp->nrec == opp->caprec) {
        int cap = opp->caprec ? opp->caprec * 2 : 16;
        CellRec *nr = (CellRec *)realloc(opp->rec, (size_t)cap * sizeof(CellRec));
        if (!nr) break;
        opp->rec = nr; opp->caprec = cap;
      }
      memset(&opp->rec[opp->nrec], 0, sizeof(CellRec));
      opp->rec[opp->nrec].sheet = rec->sheet; opp->rec[opp->nrec].r = rec->r; opp->rec[opp->nrec].c = rec->c;
      if ((cur = sheet_cell(b->sh[rec->sheet], rec->r, rec->c))) {
        opp->rec[opp->nrec].present = 1;
        opp->rec[opp->nrec].cell = *cur;
        cur->v = val_empty(); cur->f = NULL;
        sheet_cell_clear(b->sh[rec->sheet], rec->r, rec->c);
      }
      opp->nrec++;
      if (rec->present && (cur = sheet_cell_make(b->sh[rec->sheet], rec->r, rec->c))) {
        *cur = rec->cell;
        rec->present = 0;               /* moved into the sheet */
      }
    }
  } else if (u->kind == U_SHEET) {
    if (u->sheet < b->nsh) {
      opp->snap = b->sh[u->sheet];
      b->sh[u->sheet] = u->snap;
      u->snap = NULL;
    }
  } else {
    opp->book = b->sh; opp->nbook = b->nsh; opp->active = b->active;
    b->sh = u->book; b->nsh = u->nbook; b->cap = u->nbook + 1;
    if (b->active >= b->nsh) b->active = 0;
    b->active = u->active < b->nsh ? u->active : 0;
    u->book = NULL; u->nbook = 0;
  }
  opp->next = *to;
  *to = opp;
  if (sheet) *sheet = u->sheet;
  if (area) *area = u->area;
  undo_free_step(u);
  b->dirty = 1;
  if (!b->manualcalc) book_recalc(b);
  return 1;
}
int book_undo(Book *b, int *sheet, Rect *area) { int ok; b->changes++; ok = undo_apply(b, &b->undo, &b->redo, sheet, area); if (ok && b->undo_depth) b->undo_depth--; return ok; }
int book_redo(Book *b, int *sheet, Rect *area) { int ok; b->changes++; ok = undo_apply(b, &b->redo, &b->undo, sheet, area); if (ok) b->undo_depth++; return ok; }
int book_can_undo(Book *b) { return b->undo != NULL; }
int book_can_redo(Book *b) { return b->redo != NULL; }

/* ------------------------------------------------------------------ sheets in and out */
/* every formula in the book (cells and names) through fn */
static void each_formula(Book *b, void (*fn)(Formula *f, int home, void *arg), void *arg)
{
  int s, i;
  i32 r;
  for (s = 0; s < b->nsh; s++) {
    Sheet *sh = b->sh[s];
    for (r = 0; r < sh->nrows; r++)
      if (sh->rows[r]) for (i = 0; i < sh->rows[r]->n; i++) if (sh->rows[r]->c[i].f) fn(sh->rows[r]->c[i].f, s, arg);
    /* conditional formatting's formulas too */
    for (i = 0; i < sh->ncfs; i++) { if (sh->cfs[i].f1) fn(sh->cfs[i].f1, s, arg); if (sh->cfs[i].f2) fn(sh->cfs[i].f2, s, arg); }
  }
  for (i = 0; i < b->nnames; i++) if (b->names[i].f) fn(b->names[i].f, b->names[i].sheet == SHEET_SELF ? 0 : b->names[i].sheet, arg);
}
/* sheet numbers in references after sheets move: map[old] is the new number, -1 for deleted */
static void remap_sheets(Formula *f, int home, void *arg)
{
  const int *map = (const int *)arg;
  int i;
  (void)home;
  for (i = 0; i < f->n; i++) {
    Tok *t = &f->t[i];
    if ((t->op == T_REF || t->op == T_AREA) && t->a != SHEET_SELF) {
      int m = map[t->a];
      if (m < 0) { t->op = T_ERR; t->a = E_REF; t->flags = 0; }
      else t->a = (u16)m;
    }
  }
}
int book_add_sheet(Book *b, const char *name, int at)
{
  Sheet *s;
  char nm[32];
  int i;
  if (b->nsh >= MAXSHEETS) return -1;
  if (!name) { book_unique_sheet_name(b, NULL, nm); name = nm; }
  if (!sheet_name_ok(b, name, -1)) return -1;
  if (at < 0 || at > b->nsh) at = b->nsh;
  if (b->nsh + 1 > b->cap) {
    int cap = b->cap ? b->cap * 2 : 4;
    Sheet **ns = (Sheet **)realloc(b->sh, (size_t)cap * sizeof(Sheet *));
    if (!ns) return -1;
    b->sh = ns; b->cap = cap;
  }
  if (!(s = sheet_new(name))) return -1;
  if (b->nsh) {
    int map[MAXSHEETS + 1];
    undo_book(b);
    for (i = 0; i < b->nsh; i++) map[i] = i < at ? i : i + 1;
    each_formula(b, remap_sheets, map);
  }
  memmove(&b->sh[at + 1], &b->sh[at], (size_t)(b->nsh - at) * sizeof(Sheet *));
  b->sh[at] = s;
  b->nsh++;
  if (b->active >= at && b->nsh > 1) b->active++;
  b->dirty = 1;
  return at;
}
/* see book.h: a copy of sheet i (cells, sizes, merges, panes) after it, named "Name (2)" */
int book_duplicate_sheet(Book *b, int i)
{
  char nm[32];
  int at;
  Sheet *c;
  if (i < 0 || i >= b->nsh) return -1;
  book_unique_sheet_name(b, b->sh[i]->name, nm);
  book_begin(b);
  at = book_add_sheet(b, nm, i + 1);
  if (at >= 0 && (c = sheet_clone(b->sh[i]))) {
    strncpy(c->name, nm, sizeof c->name - 1); c->name[sizeof c->name - 1] = 0;
    sheet_free(b->sh[at]);
    b->sh[at] = c;
    tables_rename_copies(b, at);
  }
  book_end(b);
  if (!b->manualcalc) book_recalc(b);
  return at;
}
/* see book.h: a defined name made or changed (its formula as typed, without "=") */
int book_define_name(Book *b, const char *name, const char *text, int sheet)
{
  int i, pos, k = -1;
  Formula *f;
  u16 sh; Rect r; u8 fl;
  const char *p;
  if (!name || !name[0] || strlen(name) > 63) return 0;
  if (!isalpha((u8)name[0]) && name[0] != '_' && name[0] != '\\' && (u8)name[0] < 0x80) return 0;
  for (p = name; *p; p++) if (!isalnum((u8)*p) && *p != '_' && *p != '.' && *p != '\\' && (u8)*p < 0x80) return 0;
  if (str_ieq(name, "TRUE") || str_ieq(name, "FALSE") || ((int)strlen(name) == parse_ref(b, name, &sh, &r, &fl))) return 0;
  if (text[0] == '=') text++;
  if (!(f = formula_parse(b, sheet == SHEET_SELF ? 0 : sheet, text, &pos))) return 0;
  for (i = 0; i < b->nnames; i++) if (str_ieq(b->names[i].name, name) && b->names[i].sheet == (u16)sheet) k = i;
  if (k < 0) {
    Name *nn = (Name *)realloc(b->names, (size_t)(b->nnames + 1) * sizeof(Name));
    if (!nn) { formula_free(f); return 0; }
    b->names = nn;
    k = b->nnames++;
    memset(&b->names[k], 0, sizeof(Name));
  } else { free(b->names[k].formula_text); formula_free(b->names[k].f); }
  strncpy(b->names[k].name, name, sizeof b->names[k].name - 1);
  b->names[k].sheet = (u16)sheet;
  b->names[k].formula_text = str_dup(text);
  b->names[k].f = f;
  b->dirty = 1;
  if (!b->manualcalc) book_recalc(b);
  return 1;
}
int book_delete_name(Book *b, int i)
{
  if (i < 0 || i >= b->nnames) return 0;
  free(b->names[i].formula_text); formula_free(b->names[i].f);
  memmove(&b->names[i], &b->names[i + 1], (size_t)(b->nnames - i - 1) * sizeof(Name));
  b->nnames--;
  b->dirty = 1;
  if (!b->manualcalc) book_recalc(b);
  return 1;
}
int book_delete_sheet(Book *b, int i)
{
  int map[MAXSHEETS + 1], k;
  if (i < 0 || i >= b->nsh || b->nsh == 1) return 0;
  book_begin(b);
  undo_book(b);
  for (k = 0; k < b->nsh; k++) map[k] = k < i ? k : k == i ? -1 : k - 1;
  each_formula(b, remap_sheets, map);
  sheet_free(b->sh[i]);
  memmove(&b->sh[i], &b->sh[i + 1], (size_t)(b->nsh - i - 1) * sizeof(Sheet *));
  b->nsh--;
  if (b->active >= b->nsh) b->active = b->nsh - 1;
  book_end(b);
  if (!b->manualcalc) book_recalc(b);
  return 1;
}
int book_rename_sheet(Book *b, int i, const char *name)
{
  if (i < 0 || i >= b->nsh || !sheet_name_ok(b, name, i)) return 0;
  book_begin(b);
  undo_book(b);
  strncpy(b->sh[i]->name, name, sizeof b->sh[i]->name - 1);
  b->sh[i]->name[sizeof b->sh[i]->name - 1] = 0;
  book_end(b);
  return 1;
}
int book_move_sheet(Book *b, int from, int to)
{
  int map[MAXSHEETS + 1], k;
  Sheet *s;
  if (from < 0 || from >= b->nsh || to < 0 || to >= b->nsh || from == to) return 0;
  book_begin(b);
  undo_book(b);
  /* the new order: from taken out and put back at to */
  for (k = 0; k < b->nsh; k++) {
    if (k == from) map[k] = to;
    else if (from < to && k > from && k <= to) map[k] = k - 1;
    else if (from > to && k >= to && k < from) map[k] = k + 1;
    else map[k] = k;
  }
  each_formula(b, remap_sheets, map);
  s = b->sh[from];
  if (from < to) memmove(&b->sh[from], &b->sh[from + 1], (size_t)(to - from) * sizeof(Sheet *));
  else memmove(&b->sh[to + 1], &b->sh[to], (size_t)(from - to) * sizeof(Sheet *));
  b->sh[to] = s;
  b->active = to;
  book_end(b);
  return 1;
}

/* ------------------------------------------------------------------ styles */
int book_font(Book *b, const Font *f)
{
  int i;
  Font *nf;
  for (i = 0; i < b->nfonts; i++) if (!memcmp(&b->fonts[i], f, sizeof *f)) return i;
  if (b->nfonts >= 0xFFFF || !(nf = (Font *)realloc(b->fonts, (size_t)(b->nfonts + 1) * sizeof(Font)))) return -1;
  b->fonts = nf;
  b->fonts[b->nfonts] = *f;
  return b->nfonts++;
}
int book_style(Book *b, const Style *st)
{
  int i;
  Style *ns;
  for (i = 0; i < b->nstyles; i++) if (!memcmp(&b->styles[i], st, sizeof *st)) return i;
  if (b->nstyles >= 0xFFFF || !(ns = (Style *)realloc(b->styles, (size_t)(b->nstyles + 1) * sizeof(Style)))) return -1;
  b->styles = ns;
  b->styles[b->nstyles] = *st;
  return b->nstyles++;
}
/* the built-in number formats Excel numbers 0-49 (those it has) */
static const char *const builtin_fmts[50] = {
  "General", "0", "0.00", "#,##0", "#,##0.00", "\"$\"#,##0_);(\"$\"#,##0)", "\"$\"#,##0_);[Red](\"$\"#,##0)",
  "\"$\"#,##0.00_);(\"$\"#,##0.00)", "\"$\"#,##0.00_);[Red](\"$\"#,##0.00)", "0%", "0.00%", "0.00E+00", "# ?/?", "# ?\?/??",
  "d/mm/yyyy", "d-mmm-yy", "d-mmm", "mmm-yy", "h:mm AM/PM", "h:mm:ss AM/PM", "h:mm", "h:mm:ss", "d/mm/yyyy h:mm",
  NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
  "#,##0_);(#,##0)", "#,##0_);[Red](#,##0)", "#,##0.00_);(#,##0.00)", "#,##0.00_);[Red](#,##0.00)",
  "_(* #,##0_);_(* (#,##0);_(* \"-\"_);_(@_)", "_(\"$\"* #,##0_);_(\"$\"* (#,##0);_(\"$\"* \"-\"_);_(@_)",
  "_(* #,##0.00_);_(* (#,##0.00);_(* \"-\"??_);_(@_)", "_(\"$\"* #,##0.00_);_(\"$\"* (#,##0.00);_(\"$\"* \"-\"??_);_(@_)",
  "mm:ss", "[h]:mm:ss", "mm:ss.0", "##0.0E+0", "@" };
const char *book_fmt_code(Book *b, int id)
{
  if (id >= 0 && id < 50 && builtin_fmts[id]) return builtin_fmts[id];
  if (id >= 164 && id - 164 < b->nfmts) return b->fmts[id - 164];
  return "General";
}
int book_fmt(Book *b, const char *code)
{
  int i;
  char **nf;
  for (i = 0; i < 50; i++) if (builtin_fmts[i] && !strcmp(builtin_fmts[i], code)) return i;
  for (i = 0; i < b->nfmts; i++) if (!strcmp(b->fmts[i], code)) return 164 + i;
  if (b->nfmts > 4000 || !(nf = (char **)realloc(b->fmts, (size_t)(b->nfmts + 1) * sizeof(char *)))) return 0;
  b->fmts = nf;
  if (!(b->fmts[b->nfmts] = str_dup(code))) return 0;
  return 164 + b->nfmts++;
}

/* ------------------------------------------------------------------ editing */
static void after_edit(Book *b) { b->dirty = 1; if (!b->manualcalc) book_recalc(b); }

/* the style a number typed in a General cell gets, by what it looked like */
static int auto_format(Book *b, int style, int kind, const char *text)
{
  Style st;
  const char *code = NULL;
  if (style < 0 || style >= b->nstyles || b->styles[style].numfmt != 0) return style;
  switch (kind) {
  case 1: {
    /* a date: keep the shape typed (with a month name: d-mmm-yy; otherwise d/mm/yyyy) */
    const char *p = text;
    int alpha = 0;
    while (*p) if (isalpha((u8)*p++)) alpha = 1;
    code = alpha ? "d-mmm-yy" : ledger_date_order ? "m/d/yyyy" : "d/mm/yyyy";
    break; }
  case 2: code = ledger_date_order ? "m/d/yyyy h:mm" : "d/mm/yyyy h:mm"; break;
  case 3: code = strpbrk(text, "aApP") ? (strchr(text, ':') && strchr(strchr(text, ':') + 1, ':') ? "h:mm:ss AM/PM" : "h:mm AM/PM")
                                        : (strchr(text, ':') && strchr(strchr(text, ':') + 1, ':') ? "h:mm:ss" : "h:mm"); break;
  case 4: code = strchr(text, '.') ? "0.00%" : "0%"; break;
  case 5: code = strchr(text, '.') ? "\"$\"#,##0.00_);(\"$\"#,##0.00)" : "\"$\"#,##0_);(\"$\"#,##0)"; break;
  default:
    if (strchr(text, ',')) code = strchr(text, '.') ? "#,##0.00" : "#,##0";
    break;
  }
  if (!code) return style;
  st = b->styles[style];
  st.numfmt = (u16)book_fmt(b, code);
  {
    int ns = book_style(b, &st);
    return ns >= 0 ? ns : style;
  }
}
int book_input(Book *b, int sheet, i32 r, i32 c, const char *text, int *errpos)
{
  Sheet *s;
  Cell *cell;
  Value v = val_empty();
  Formula *f = NULL;
  int style, kind = 0, tg;
  double n;
  if (sheet < 0 || sheet >= b->nsh || r < 0 || r >= MAXROW || c < 0 || c >= MAXCOL) return 0;
  s = b->sh[sheet];
  if (text[0] == '=' && text[1]) {
    if (!(f = formula_parse(b, sheet, text + 1, errpos))) return 0;
  } else if (text[0] == '+' && text[1] && !isdigit((u8)text[1]) && text[1] != '.') {
    /* "+A1*2" is a formula as in Lotus and Excel */
    if (!(f = formula_parse(b, sheet, text + 1, errpos))) return 0;
  } else if (text[0] == '\'') v = val_str(text + 1);
  else if (!text[0]) v = val_empty();
  else if (str_ieq(text, "TRUE")) v = val_bool(1);
  else if (str_ieq(text, "FALSE")) v = val_bool(0);
  else if (text[0] == '#' && err_parse(text, NULL) && strlen(text) == strlen(err_name(err_parse(text, NULL)))) v = val_err(err_parse(text, NULL));
  else if (parse_number(text, &n, &kind)) v = val_num(n);
  else v = val_str(text);
  book_begin(b);
  tg = s->ntables ? table_input_pre(b, sheet, r, c, text) : 0;     /* (a table grows, or its header changes) */
  undo_cell(b, sheet, r, c);
  style = sheet_cell_style(s, r, c);
  if (v.t == V_EMPTY && !f) {
    /* emptied: keep the cell only for its format */
    if ((cell = sheet_cell(s, r, c))) {
      if (cell->style) { cell_free_contents(cell); }
      else sheet_cell_clear(s, r, c);
    }
  } else if ((cell = sheet_cell_make(s, r, c))) {
    cell_free_contents(cell);
    cell->v = v;
    cell->f = f;
    cell->calcgen = 0;
    if (v.t == V_NUM) cell->style = (u16)auto_format(b, style, kind, text);
    v = val_empty(); f = NULL;
  }
  val_free(&v);
  formula_free(f);
  if (tg) table_input_post(b, sheet, r, c, tg);
  book_end(b);
  after_edit(b);
  return 1;
}
int book_set_value(Book *b, int sheet, i32 r, i32 c, const Value *v)
{
  Cell *cell;
  if (sheet < 0 || sheet >= b->nsh) return 0;
  book_begin(b);
  undo_cell(b, sheet, r, c);
  if ((cell = sheet_cell_make(b->sh[sheet], r, c))) {
    cell_free_contents(cell);
    cell->v = val_copy(v);
  }
  book_end(b);
  after_edit(b);
  return cell != NULL;
}
/* see book.h: a compiled formula put in a cell (copy and paste, fill, sort), its old contents gone */
int book_set_formula(Book *b, int sheet, i32 r, i32 c, Formula *f)
{
  Cell *cell;
  if (sheet < 0 || sheet >= b->nsh || !f) { formula_free(f); return 0; }
  book_begin(b);
  undo_cell(b, sheet, r, c);
  if ((cell = sheet_cell_make(b->sh[sheet], r, c))) {
    cell_free_contents(cell);
    cell->f = f;
    cell->calcgen = 0;
  } else formula_free(f);
  book_end(b);
  after_edit(b);
  return cell != NULL;
}
int book_set_style(Book *b, int sheet, i32 r, i32 c, int style)
{
  Cell *cell;
  if (sheet < 0 || sheet >= b->nsh || style < 0 || style >= b->nstyles) return 0;
  book_begin(b);
  undo_cell(b, sheet, r, c);
  if ((cell = sheet_cell_make(b->sh[sheet], r, c))) cell->style = (u16)style;
  book_end(b);
  b->dirty = 1;
  return cell != NULL;
}
/* the occupied cells of area on a sheet, each through fn (a snapshot of the coordinates first, as
   fn may add or remove cells). Huge areas (whole columns) only visit cells that exist */
typedef void (*CellFn)(Book *b, int sheet, i32 r, i32 c, const void *arg);
static int each_cell(Book *b, int sheet, Rect a, int all, CellFn fn, const void *arg)
{
  Sheet *s = b->sh[sheet];
  i32 r, rmax = a.r2 < s->nrows - 1 ? a.r2 : s->nrows - 1, *list = NULL;
  long n = 0, cap = 0, k;
  if (all) {
    /* every position (formatting a small area must create cells) */
    for (r = a.r1; r <= a.r2; r++) {
      i32 c;
      for (c = a.c1; c <= a.c2; c++) fn(b, sheet, r, c, arg);
    }
    return 1;
  }
  for (r = a.r1; r <= rmax; r++) {
    int i;
    if (!s->rows[r]) continue;
    for (i = 0; i < s->rows[r]->n; i++) {
      i32 c = s->rows[r]->c[i].col;
      if (c < a.c1 || c > a.c2) continue;
      if (n + 2 > cap) {
        long nc = cap ? cap * 2 : 256;
        i32 *nl = (i32 *)realloc(list, (size_t)nc * sizeof(i32));
        if (!nl) { free(list); return 0; }
        list = nl; cap = nc;
      }
      list[n++] = r; list[n++] = c;
    }
  }
  for (k = 0; k < n; k += 2) fn(b, sheet, list[k], list[k + 1], arg);
  free(list);
  return 1;
}
static void clear_one(Book *b, int sheet, i32 r, i32 c, const void *arg)
{
  int what = *(const int *)arg;
  Cell *cell = sheet_cell(b->sh[sheet], r, c);
  if (!cell) return;
  undo_cell(b, sheet, r, c);
  if (what & CLR_CONTENTS) { cell_free_contents(cell); }
  if (what & CLR_FORMATS) cell->style = 0;
  if (cell->v.t == V_EMPTY && !cell->f && !cell->style) sheet_cell_clear(b->sh[sheet], r, c);
}
int book_clear(Book *b, int sheet, Rect area, int what)
{
  if (sheet < 0 || sheet >= b->nsh) return 0;
  book_begin(b);
  if (what == CLR_ALL) {
    /* Clear All takes the notes too */
    Sheet *s = b->sh[sheet];
    int i, any = 0;
    for (i = 0; i < s->nnotes; i++) if (s->notes[i].r >= area.r1 && s->notes[i].r <= area.r2 && s->notes[i].c >= area.c1 && s->notes[i].c <= area.c2) any = 1;
    if (any) {
      undo_sheet(b, sheet);
      for (i = 0; i < s->nnotes; )
        if (s->notes[i].r >= area.r1 && s->notes[i].r <= area.r2 && s->notes[i].c >= area.c1 && s->notes[i].c <= area.c2) {
          free(s->notes[i].author); free(s->notes[i].text);
          s->notes[i] = s->notes[--s->nnotes];
        } else i++;
    }
    any = 0;
    for (i = 0; i < s->nlinks; i++) if (s->links[i].r >= area.r1 && s->links[i].r <= area.r2 && s->links[i].c >= area.c1 && s->links[i].c <= area.c2) any = 1;
    if (any) {
      undo_sheet(b, sheet);
      for (i = 0; i < s->nlinks; )
        if (s->links[i].r >= area.r1 && s->links[i].r <= area.r2 && s->links[i].c >= area.c1 && s->links[i].c <= area.c2) {
          free(s->links[i].target); free(s->links[i].tip);
          s->links[i] = s->links[--s->nlinks];
        } else i++;
    }
  }
  each_cell(b, sheet, area, 0, clear_one, &what);
  if (what & CLR_FORMATS) {
    /* whole rows or columns lose their formats too */
    Sheet *s = b->sh[sheet];
    if (area.r1 == 0 && area.r2 == MAXROW - 1) {
      i32 c;
      undo_sheet(b, sheet);
      for (c = area.c1; c <= area.c2 && c < s->ncols; c++) s->cols[c].style = 0;
    }
  }
  book_end(b);
  after_edit(b);
  return 1;
}
/* restyle one cell (see book_restyle) */
typedef struct { StyleEdit fn; const void *arg; i32 *map; } Restyle;
static int restyled(Book *b, int old, Restyle *rs)
{
  Style st;
  Font f;
  int fi, si;
  if (old < 0 || old >= b->nstyles) old = 0;
  if (rs->map && rs->map[old] >= 0) return rs->map[old];
  st = b->styles[old];
  f = b->fonts[st.font < b->nfonts ? st.font : 0];
  rs->fn(&st, &f, rs->arg);
  fi = book_font(b, &f);
  if (fi >= 0) st.font = (u16)fi;
  si = book_style(b, &st);
  if (si < 0) si = old;
  if (rs->map) rs->map[old] = si;
  return si;
}
static void restyle_one(Book *b, int sheet, i32 r, i32 c, const void *arg)
{
  Restyle *rs = (Restyle *)arg;
  Sheet *s = b->sh[sheet];
  int old = sheet_cell_style(s, r, c), ns = restyled(b, old, rs);
  Cell *cell;
  if (ns == old && sheet_cell(s, r, c)) return;
  if (ns == old) return;
  undo_cell(b, sheet, r, c);
  if ((cell = sheet_cell_make(s, r, c))) cell->style = (u16)ns;
}
int book_restyle(Book *b, int sheet, Rect area, StyleEdit fn, const void *arg)
{
  Restyle rs;
  Sheet *s;
  long cells;
  int i;
  if (sheet < 0 || sheet >= b->nsh) return 0;
  s = b->sh[sheet];
  rs.fn = fn; rs.arg = arg;
  rs.map = (i32 *)malloc(65536 * sizeof(i32));
  if (rs.map) for (i = 0; i < 65536; i++) rs.map[i] = -1;
  book_begin(b);
  cells = (long)(area.r2 - area.r1 + 1) * (area.c2 - area.c1 + 1);
  if (area.r1 == 0 && area.r2 == MAXROW - 1) {
    /* whole columns: the column formats, and the cells that exist */
    i32 c;
    undo_sheet(b, sheet);
    for (c = area.c1; c <= area.c2; c++) { ColInfo *ci = col_make(s, c); if (ci) ci->style = (u16)restyled(b, ci->style, &rs); }
    each_cell(b, sheet, area, 0, restyle_one, &rs);
  } else if (area.c1 == 0 && area.c2 == MAXCOL - 1) {
    i32 r;
    undo_sheet(b, sheet);
    for (r = area.r1; r <= area.r2; r++) { Row *row = row_make(s, r); if (row) row->style = (u16)restyled(b, row->style, &rs); }
    each_cell(b, sheet, area, 0, restyle_one, &rs);
  } else if (cells <= 200000L) each_cell(b, sheet, area, 1, restyle_one, &rs);
  book_end(b);
  free(rs.map);
  b->dirty = 1;
  return 1;
}
int book_set_col_width(Book *b, int sheet, i32 c, u16 px)
{
  ColInfo *ci;
  if (sheet < 0 || sheet >= b->nsh) return 0;
  book_begin(b);
  undo_sheet(b, sheet);
  if ((ci = col_make(b->sh[sheet], c))) { ci->width = px; ci->custom = 1; ci->hidden = px == 0; }
  book_end(b);
  return ci != NULL;
}
int book_set_row_height(Book *b, int sheet, i32 r, u16 px)
{
  Row *row;
  if (sheet < 0 || sheet >= b->nsh) return 0;
  book_begin(b);
  undo_sheet(b, sheet);
  if ((row = row_make(b->sh[sheet], r))) { row->height = px; row->custom = 1; row->hidden = px == 0; }
  book_end(b);
  return row != NULL;
}
int book_hide_cols(Book *b, int sheet, i32 c1, i32 c2, int hide)
{
  i32 c;
  if (sheet < 0 || sheet >= b->nsh) return 0;
  book_begin(b);
  undo_sheet(b, sheet);
  for (c = c1; c <= c2; c++) {
    ColInfo *ci = col_make(b->sh[sheet], c);
    if (ci) { ci->hidden = (u8)hide; if (!hide && ci->width == 0) ci->width = b->sh[sheet]->defwidth; }
  }
  book_end(b);
  return 1;
}
int book_hide_rows(Book *b, int sheet, i32 r1, i32 r2, int hide)
{
  i32 r;
  if (sheet < 0 || sheet >= b->nsh) return 0;
  book_begin(b);
  undo_sheet(b, sheet);
  for (r = r1; r <= r2 && r < MAXROW; r++) {
    Row *row = hide ? row_make(b->sh[sheet], r) : (r < b->sh[sheet]->nrows ? b->sh[sheet]->rows[r] : NULL);
    if (row) { row->hidden = (u8)hide; if (!hide && row->height == 0) row->height = 0; }
  }
  book_end(b);
  if (!b->manualcalc) book_recalc(b);     /* (SUBTOTAL leaves hidden rows out) */
  return 1;
}
int book_merge(Book *b, int sheet, Rect a, int merge)
{
  Sheet *s;
  int i;
  if (sheet < 0 || sheet >= b->nsh) return 0;
  s = b->sh[sheet];
  book_begin(b);
  undo_sheet(b, sheet);
  /* any merge overlapping the area goes */
  for (i = s->nmerges - 1; i >= 0; i--) {
    Rect m = s->merges[i];
    if (m.r1 <= a.r2 && m.r2 >= a.r1 && m.c1 <= a.c2 && m.c2 >= a.c1) {
      memmove(&s->merges[i], &s->merges[i + 1], (size_t)(s->nmerges - i - 1) * sizeof(Rect));
      s->nmerges--;
    }
  }
  if (merge && (a.r1 != a.r2 || a.c1 != a.c2)) {
    Rect *nm = (Rect *)realloc(s->merges, (size_t)(s->nmerges + 1) * sizeof(Rect));
    if (nm) {
      i32 r;
      s->merges = nm;
      s->merges[s->nmerges++] = a;
      /* only the top-left cell keeps its value, as in Excel */
      for (r = a.r1; r <= a.r2 && r < s->nrows; r++) {
        int k;
        if (!s->rows[r]) continue;
        for (k = s->rows[r]->n - 1; k >= 0; k--) {
          Cell *cell = &s->rows[r]->c[k];
          if (cell->col < a.c1 || cell->col > a.c2 || (r == a.r1 && cell->col == a.c1)) continue;
          cell_free_contents(cell);
        }
      }
    }
  }
  book_end(b);
  after_edit(b);
  return 1;
}
int book_freeze(Book *b, int sheet, i32 rows, i32 cols)
{
  if (sheet < 0 || sheet >= b->nsh) return 0;
  book_begin(b);
  undo_sheet(b, sheet);
  b->sh[sheet]->freezer = rows < 0 ? 0 : rows;
  b->sh[sheet]->freezec = cols < 0 ? 0 : cols;
  book_end(b);
  return 1;
}

/* ------------------------------------------------------------------ rows and columns in and out */
typedef struct { int target, rows; i32 at, n; } Shift;
static void shift_formula(Formula *f, int home, void *arg)
{
  const Shift *sh = (const Shift *)arg;
  formula_shift(f, home, sh->target, sh->rows, sh->at, sh->n);
}
/* move the cells of a sheet: rows from at on by n (n < 0 deletes -n rows at at) */
static void move_rows(Sheet *s, i32 at, i32 n)
{
  i32 r;
  if (n > 0) {
    i32 last = s->nrows - 1;
    while (last >= at && !s->rows[last]) last--;
    if (last < at) return;
    /* the rows pushed past the end are lost (as Excel refuses; we drop them) */
    if (!row_make(s, last + n > MAXROW - 1 ? MAXROW - 1 : last + n)) return;
    for (r = last; r >= at; r--) {
      i32 to = r + n;
      if (to >= MAXROW) {
        Row *row = s->rows[r];
        int i;
        if (row) { for (i = 0; i < row->n; i++) cell_free_contents(&row->c[i]); free(row->c); free(row); }
      } else {
        if (s->rows[to]) { free(s->rows[to]->c); free(s->rows[to]); }
        s->rows[to] = s->rows[r];
      }
      s->rows[r] = NULL;
    }
  } else if (n < 0) {
    i32 del = -n, end = at + del;
    for (r = at; r < end && r < s->nrows; r++) {
      Row *row = s->rows[r];
      int i;
      if (!row) continue;
      for (i = 0; i < row->n; i++) cell_free_contents(&row->c[i]);
      free(row->c); free(row);
      s->rows[r] = NULL;
    }
    for (r = end; r < s->nrows; r++) { s->rows[r - del] = s->rows[r]; s->rows[r] = NULL; }
  }
}
static void move_cols(Sheet *s, i32 at, i32 n)
{
  i32 r;
  for (r = 0; r < s->nrows; r++) {
    Row *row = s->rows[r];
    int i, k;
    if (!row) continue;
    if (n < 0) {
      for (i = k = 0; i < row->n; i++) {
        Cell *c = &row->c[i];
        if (c->col >= at && c->col < at - n) { cell_free_contents(c); continue; }
        if (c->col >= at - n) c->col = (u16)(c->col + n);
        row->c[k++] = *c;
      }
      row->n = k;
    } else {
      for (i = k = 0; i < row->n; i++) {
        Cell *c = &row->c[i];
        if (c->col >= at) {
          if (c->col + n >= MAXCOL) { cell_free_contents(c); continue; }
          c->col = (u16)(c->col + n);
        }
        row->c[k++] = *c;
      }
      row->n = k;
    }
  }
  /* column info */
  if (n > 0 && at < s->ncols) {
    i32 c;
    if (col_make(s, s->ncols - 1 + n < MAXCOL ? s->ncols - 1 + n : MAXCOL - 1)) {
      for (c = s->ncols - 1; c >= at + n; c--) s->cols[c] = s->cols[c - n];
      for (c = at; c < at + n && c < s->ncols; c++) memset(&s->cols[c], 0, sizeof(ColInfo));
    }
  } else if (n < 0 && at < s->ncols) {
    i32 c;
    for (c = at; c < s->ncols; c++) {
      if (c - n < s->ncols) s->cols[c] = s->cols[c - n];
      else memset(&s->cols[c], 0, sizeof(ColInfo));
    }
  }
}
/* merges and frozen panes follow rows/columns (rows: 1 for rows) */
static void move_extras(Sheet *s, int rows, i32 at, i32 n)
{
  int i, k;
  for (i = k = 0; i < s->nmerges; i++) {
    Rect m = s->merges[i];
    i32 *a1 = rows ? &m.r1 : &m.c1, *a2 = rows ? &m.r2 : &m.c2;
    if (n > 0) {
      if (*a1 >= at) *a1 += n;
      if (*a2 >= at) *a2 += n;
    } else {
      i32 d = -n;
      if (*a1 >= at && *a2 < at + d) continue;            /* deleted altogether */
      if (*a1 >= at + d) *a1 -= d; else if (*a1 >= at) *a1 = at;
      if (*a2 >= at + d) *a2 -= d; else if (*a2 >= at) *a2 = at - 1;
      if (*a2 < *a1 || (m.r1 == m.r2 && m.c1 == m.c2)) continue;
    }
    s->merges[k++] = m;
  }
  s->nmerges = k;
  /* notes: moved with their cells, gone with deleted ones */
  for (i = k = 0; i < s->nnotes; i++) {
    Note nt = s->notes[i];
    i32 *a = rows ? &nt.r : &nt.c;
    if (n > 0) { if (*a >= at) *a += n; if (*a >= (rows ? MAXROW : MAXCOL)) { free(nt.author); free(nt.text); free(nt.replies); continue; } }
    else if (*a >= at && *a < at - n) { free(nt.author); free(nt.text); free(nt.replies); continue; }
    else if (*a >= at - n) *a += n;
    s->notes[k++] = nt;
  }
  s->nnotes = k;
  for (i = k = 0; i < s->nlinks; i++) {
    Link ln = s->links[i];
    i32 *a = rows ? &ln.r : &ln.c;
    if (n > 0) { if (*a >= at) *a += n; if (*a >= (rows ? MAXROW : MAXCOL)) { free(ln.target); free(ln.tip); continue; } }
    else if (*a >= at && *a < at - n) { free(ln.target); free(ln.tip); continue; }
    else if (*a >= at - n) *a += n;
    s->links[k++] = ln;
  }
  s->nlinks = k;
  /* the AutoFilter's range and its columns */
  if (s->filter.r1 >= 0) {
    i32 *a1 = rows ? &s->filter.r1 : &s->filter.c1, *a2 = rows ? &s->filter.r2 : &s->filter.c2;
    if (n > 0) { if (*a1 >= at) *a1 += n; if (*a2 >= at) *a2 += n; }
    else {
      i32 d = -n;
      if (*a1 >= at && *a2 < at + d) { s->filter.r1 = -1; fcols_free(s->fcols, s->nfcols); s->fcols = NULL; s->nfcols = 0; return; }
      if (*a1 >= at + d) *a1 -= d; else if (*a1 >= at) *a1 = at;
      if (*a2 >= at + d) *a2 -= d; else if (*a2 >= at) *a2 = at - 1;
    }
    if (!rows)
      for (i = k = 0; i < s->nfcols; i++) {
        FilterCol f = s->fcols[i];
        if (n > 0) { if (f.col >= at) f.col += n; }
        else if (f.col >= at && f.col < at - n) { int j; for (j = 0; j < f.n; j++) free(f.vals[j]); free(f.vals); continue; }
        else if (f.col >= at - n) f.col += n;
        s->fcols[k++] = f;
      }
    if (!rows) s->nfcols = k;
  }
}
static int insdel(Book *b, int sheet, int rows, i32 at, i32 n)
{
  Shift sh;
  Sheet *s;
  if (sheet < 0 || sheet >= b->nsh || n == 0 || at < 0 || at >= (rows ? MAXROW : MAXCOL)) return 0;
  s = b->sh[sheet];
  book_begin(b);
  undo_book(b);
  if (rows) move_rows(s, at, n); else move_cols(s, at, n);
  move_extras(s, rows, at, n);
  cond_shift_areas(s, rows, at, n);
  tables_shift(s, rows, at, n);
  pivots_shift(s, rows, at, n);
  page_shift(s, rows, at, n);
  sh.target = sheet; sh.rows = rows; sh.at = at; sh.n = n;
  each_formula(b, shift_formula, &sh);
  book_end(b);
  after_edit(b);
  return 1;
}
int book_insert_rows(Book *b, int sheet, i32 r, i32 n) { return insdel(b, sheet, 1, r, n); }
int book_delete_rows(Book *b, int sheet, i32 r, i32 n) { return insdel(b, sheet, 1, r, -n); }
int book_insert_cols(Book *b, int sheet, i32 c, i32 n) { return insdel(b, sheet, 0, c, n); }
int book_delete_cols(Book *b, int sheet, i32 c, i32 n) { return insdel(b, sheet, 0, c, -n); }

/* insert/delete cells: the cells in the area's columns (down) or rows (right) move. Formulas pointing
   into the moved block follow only approximately (Excel moves just that strip); here references
   follow when they lie wholly within the strip */
int book_insert_cells(Book *b, int sheet, Rect a, int down)
{
  Sheet *s;
  i32 r, c, n;
  if (sheet < 0 || sheet >= b->nsh) return 0;
  s = b->sh[sheet];
  book_begin(b);
  undo_book(b);
  if (down) {
    n = a.r2 - a.r1 + 1;
    for (c = a.c1; c <= a.c2; c++) {
      i32 last = sheet_last_row(s);
      for (r = last; r >= a.r1; r--) {
        Cell *src = sheet_cell(s, r, c), *dst;
        if (!src) continue;
        if (r + n < MAXROW && (dst = sheet_cell_make(s, r + n, c))) {
          src = sheet_cell(s, r, c);
          cell_free_contents(dst);
          *dst = *src;
          dst->col = (u16)c;
          if (dst->f) formula_offset(dst->f, n, 0);
          src->v = val_empty(); src->f = NULL;
        }
        sheet_cell_clear(s, r, c);
      }
    }
  } else {
    n = a.c2 - a.c1 + 1;
    for (r = a.r1; r <= a.r2 && r < s->nrows; r++) {
      i32 last;
      if (!s->rows[r] || !s->rows[r]->n) continue;
      last = s->rows[r]->c[s->rows[r]->n - 1].col;
      for (c = last; c >= a.c1; c--) {
        Cell *src = sheet_cell(s, r, c), *dst;
        if (!src) continue;
        if (c + n < MAXCOL && (dst = sheet_cell_make(s, r, c + n))) {
          src = sheet_cell(s, r, c);
          cell_free_contents(dst);
          { u16 col = dst->col; *dst = *src; dst->col = col; }
          if (dst->f) formula_offset(dst->f, 0, n);
          src->v = val_empty(); src->f = NULL;
        }
        sheet_cell_clear(s, r, c);
      }
    }
  }
  book_end(b);
  after_edit(b);
  return 1;
}
int book_delete_cells(Book *b, int sheet, Rect a, int up)
{
  Sheet *s;
  i32 r, c, n;
  if (sheet < 0 || sheet >= b->nsh) return 0;
  s = b->sh[sheet];
  book_begin(b);
  undo_book(b);
  if (up) {
    i32 last = sheet_last_row(s);
    n = a.r2 - a.r1 + 1;
    for (c = a.c1; c <= a.c2; c++) {
      for (r = a.r1; r <= a.r2; r++) sheet_cell_clear(s, r, c);
      for (r = a.r2 + 1; r <= last; r++) {
        Cell *src = sheet_cell(s, r, c), *dst;
        if (!src) continue;
        if ((dst = sheet_cell_make(s, r - n, c))) {
          src = sheet_cell(s, r, c);
          *dst = *src;
          if (dst->f) formula_offset(dst->f, -n, 0);
          src->v = val_empty(); src->f = NULL;
        }
        sheet_cell_clear(s, r, c);
      }
    }
  } else {
    n = a.c2 - a.c1 + 1;
    for (r = a.r1; r <= a.r2 && r < s->nrows; r++) {
      i32 last;
      for (c = a.c1; c <= a.c2; c++) sheet_cell_clear(s, r, c);
      if (!s->rows[r] || !s->rows[r]->n) continue;
      last = s->rows[r]->c[s->rows[r]->n - 1].col;
      for (c = a.c2 + 1; c <= last; c++) {
        Cell *src = sheet_cell(s, r, c), *dst;
        if (!src) continue;
        if ((dst = sheet_cell_make(s, r, c - n))) {
          src = sheet_cell(s, r, c);
          { u16 col = dst->col; *dst = *src; dst->col = col; }
          if (dst->f) formula_offset(dst->f, 0, -n);
          src->v = val_empty(); src->f = NULL;
        }
        sheet_cell_clear(s, r, c);
      }
    }
  }
  book_end(b);
  after_edit(b);
  return 1;
}

void book_forget_undo(Book *b) { undo_free_list(b->undo); undo_free_list(b->redo); b->undo = b->redo = NULL; b->undo_depth = 0; }

/* ------------------------------------------------------------------ AutoFilter */
const FilterCol *sheet_filter_col(Sheet *s, i32 col)
{
  int i;
  for (i = 0; i < s->nfcols; i++) if (s->fcols[i].col == col) return &s->fcols[i];
  return NULL;
}
/* the text a filter compares: what the cell shows */
static void filter_text(Book *b, int sheet, i32 r, i32 c, char *out, size_t cap)
{
  Cell *cell = sheet_cell(b->sh[sheet], r, c);
  const Value *v;
  u32 col;
  out[0] = 0;
  if (!cell) return;
  v = cell_value(b, sheet, r, c);
  if (v->t == V_EMPTY) return;
  fmt_value(b, book_fmt_code(b, b->styles[cell->style].numfmt), v, out, cap, &col, 20);
}
/* every row of the filter's range shown or hidden as the columns' choices say */
static void filter_apply(Book *b, int sheet)
{
  Sheet *s = b->sh[sheet];
  i32 r;
  int i, k;
  char t[512];
  if (s->filter.r1 < 0) return;
  for (r = s->filter.r1 + 1; r <= s->filter.r2; r++) {
    int pass = 1;
    Row *row;
    for (i = 0; i < s->nfcols && pass; i++) {
      const FilterCol *f = &s->fcols[i];
      filter_text(b, sheet, r, f->col, t, sizeof t);
      if (!t[0]) pass = f->blanks;
      else { pass = 0; for (k = 0; k < f->n && !pass; k++) if (str_ieq(f->vals[k], t)) pass = 1; }
    }
    if (pass && (r >= s->nrows || !s->rows[r])) continue;
    if (!(row = row_make(s, r))) continue;
    row->filtered = (u8)!pass;
  }
}
/* ------------------------------------------------------------------ notes */
const Note *sheet_note(Sheet *s, i32 r, i32 c)
{
  int i;
  for (i = 0; i < s->nnotes; i++) if (s->notes[i].r == r && s->notes[i].c == c) return &s->notes[i];
  return NULL;
}
int book_set_note(Book *b, int sheet, i32 r, i32 c, const char *text, const char *author)
{
  Sheet *s;
  int i;
  if (sheet < 0 || sheet >= b->nsh || r < 0 || r >= MAXROW || c < 0 || c >= MAXCOL) return 0;
  s = b->sh[sheet];
  for (i = 0; i < s->nnotes && (s->notes[i].r != r || s->notes[i].c != c); i++) ;
  if (i == s->nnotes && (!text || !*text)) return 1;
  book_begin(b);
  undo_sheet(b, sheet);
  if (!text || !*text) {
    free(s->notes[i].author); free(s->notes[i].text); free(s->notes[i].replies);
    s->notes[i] = s->notes[--s->nnotes];
  } else if (i < s->nnotes) {
    char *t = str_dup(text);
    if (t) { free(s->notes[i].text); s->notes[i].text = t; }
    if (author && *author) { char *a = str_dup(author); if (a) { free(s->notes[i].author); s->notes[i].author = a; } }
  } else {
    Note *nn = (Note *)realloc(s->notes, (size_t)(s->nnotes + 1) * sizeof *nn);
    if (nn) {
      s->notes = nn;
      memset(&nn[s->nnotes], 0, sizeof *nn);
      nn[s->nnotes].r = r; nn[s->nnotes].c = c;
      nn[s->nnotes].text = str_dup(text);
      nn[s->nnotes].author = str_dup(author ? author : "");
      if (nn[s->nnotes].text && nn[s->nnotes].author) s->nnotes++;
      else { free(nn[s->nnotes].text); free(nn[s->nnotes].author); }
    }
  }
  book_end(b);
  b->dirty = 1;
  return 1;
}
/* ------------------------------------------------------------------ hyperlinks */
const Link *sheet_link(Sheet *s, i32 r, i32 c)
{
  int i;
  for (i = 0; i < s->nlinks; i++) if (s->links[i].r == r && s->links[i].c == c) return &s->links[i];
  return NULL;
}
int book_set_link(Book *b, int sheet, i32 r, i32 c, const char *target, const char *tip)
{
  Sheet *s;
  int i;
  if (sheet < 0 || sheet >= b->nsh || r < 0 || r >= MAXROW || c < 0 || c >= MAXCOL) return 0;
  s = b->sh[sheet];
  for (i = 0; i < s->nlinks && (s->links[i].r != r || s->links[i].c != c); i++) ;
  if (i == s->nlinks && (!target || !*target)) return 1;
  book_begin(b);
  undo_sheet(b, sheet);
  if (!target || !*target) {
    free(s->links[i].target); free(s->links[i].tip);
    s->links[i] = s->links[--s->nlinks];
  } else {
    char *t = str_dup(target), *p = str_dup(tip ? tip : "");
    if (t && p && i < s->nlinks) { free(s->links[i].target); free(s->links[i].tip); s->links[i].target = t; s->links[i].tip = p; }
    else if (t && p) {
      Link *nl = (Link *)realloc(s->links, (size_t)(s->nlinks + 1) * sizeof *nl);
      if (nl) { s->links = nl; nl[s->nlinks].r = r; nl[s->nlinks].c = c; nl[s->nlinks].target = t; nl[s->nlinks].tip = p; s->nlinks++; t = p = NULL; }
      free(t); free(p);
    } else { free(t); free(p); }
  }
  book_end(b);
  b->dirty = 1;
  return 1;
}
int book_cell_link(Book *b, int sheet, i32 r, i32 c, char *out, size_t cap)
{
  Sheet *s;
  const Link *l;
  Cell *cell;
  out[0] = 0;
  if (sheet < 0 || sheet >= b->nsh) return 0;
  s = b->sh[sheet];
  if ((l = sheet_link(s, r, c)) && l->target && l->target[0]) { snprintf(out, cap, "%s", l->target); return 1; }
  /* =HYPERLINK(link, ...): the link worked out */
  if ((cell = sheet_cell(s, r, c)) && cell->f && cell->f->n && cell->f->t[cell->f->n - 1].op == T_FUNC &&
      cell->f->t[cell->f->n - 1].a == func_lookup("HYPERLINK")) {
    Buf t;
    const char *p, *q;
    int depth = 0, inq = 0;
    buf_init(&t);
    formula_text(b, sheet, cell->f, &t);
    buf_addc(&t, 0);
    p = t.data ? strchr(t.data, '(') : NULL;
    if (p) {
      /* the first argument: up to a comma or the closing bracket at the top level */
      for (q = ++p; *q; q++) {
        if (*q == '"') inq = !inq;
        else if (!inq && *q == '(') depth++;
        else if (!inq && *q == ')') { if (!depth) break; depth--; }
        else if (!inq && *q == ',' && !depth) break;
      }
      {
        char *arg = str_ndup(p, (size_t)(q - p));
        if (arg) {
          Value v = book_eval_text(b, sheet, arg);
          Buf o;
          buf_init(&o);
          if (v.t != V_ERR) to_text(b, &v, &o);
          buf_addc(&o, 0);
          snprintf(out, cap, "%s", o.data ? o.data : "");
          buf_free(&o); val_free(&v); free(arg);
        }
      }
    }
    buf_free(&t);
    return out[0] != 0;
  }
  return 0;
}
int book_autofilter(Book *b, int sheet, Rect a, int on)
{
  Sheet *s;
  i32 r;
  if (sheet < 0 || sheet >= b->nsh) return 0;
  s = b->sh[sheet];
  book_begin(b);
  undo_sheet(b, sheet);
  for (r = 0; r < s->nrows; r++) if (s->rows[r]) s->rows[r]->filtered = 0;
  fcols_free(s->fcols, s->nfcols); s->fcols = NULL; s->nfcols = 0;
  if (on) s->filter = a; else s->filter.r1 = -1;
  book_end(b);
  b->dirty = 1;
  return 1;
}
int book_filter_column(Book *b, int sheet, i32 col, const char *const *vals, int n, int blanks)
{
  Sheet *s;
  int i, k;
  i32 r;
  if (sheet < 0 || sheet >= b->nsh || b->sh[sheet]->filter.r1 < 0) return 0;
  s = b->sh[sheet];
  book_begin(b);
  undo_sheet(b, sheet);
  for (i = 0; i < s->nfcols && s->fcols[i].col != col; i++) ;
  if (i < s->nfcols) {
    for (k = 0; k < s->fcols[i].n; k++) free(s->fcols[i].vals[k]);
    free(s->fcols[i].vals);
    s->fcols[i] = s->fcols[--s->nfcols];
  }
  if (n >= 0) {
    FilterCol *nf = (FilterCol *)realloc(s->fcols, (size_t)(s->nfcols + 1) * sizeof *nf);
    if (nf) {
      s->fcols = nf;
      nf = &s->fcols[s->nfcols++];
      nf->col = col; nf->n = 0; nf->blanks = (u8)blanks;
      nf->vals = n ? (char **)calloc((size_t)n, sizeof(char *)) : NULL;
      for (k = 0; k < n && nf->vals; k++) nf->vals[nf->n++] = str_dup(vals[k]);
    }
  }
  for (r = s->filter.r1 + 1; r <= s->filter.r2 && r < s->nrows; r++) if (s->rows[r]) s->rows[r]->filtered = 0;
  filter_apply(b, sheet);
  book_end(b);
  b->dirty = 1;
  if (!b->manualcalc) book_recalc(b);     /* (SUBTOTAL leaves filtered rows out) */
  return 1;
}
static int cmp_text(const void *a, const void *b)
{
  const char *x = *(const char *const *)a, *y = *(const char *const *)b;
  double dx, dy;
  int kx, ky;
  if (parse_number(x, &dx, &kx) && parse_number(y, &dy, &ky)) return dx < dy ? -1 : dx > dy;
  return str_ncasecmp(x, y, (size_t)-1);
}
int book_filter_reapply(Book *b, int sheet)
{
  Sheet *s;
  i32 r;
  if (sheet < 0 || sheet >= b->nsh || b->sh[sheet]->filter.r1 < 0) return 0;
  s = b->sh[sheet];
  book_begin(b);
  undo_sheet(b, sheet);
  for (r = s->filter.r1 + 1; r <= s->filter.r2 && r < s->nrows; r++) if (s->rows[r]) s->rows[r]->filtered = 0;
  filter_apply(b, sheet);
  book_end(b);
  if (!b->manualcalc) book_recalc(b);
  return 1;
}
int book_filter_values(Book *b, int sheet, i32 col, char ***out, int *blanks)
{
  Sheet *s;
  i32 r;
  int n = 0, cap = 0, i, k;
  char **v = NULL, t[512];
  *out = NULL; *blanks = 0;
  if (sheet < 0 || sheet >= b->nsh || b->sh[sheet]->filter.r1 < 0) return 0;
  s = b->sh[sheet];
  for (r = s->filter.r1 + 1; r <= s->filter.r2; r++) {
    filter_text(b, sheet, r, col, t, sizeof t);
    if (!t[0]) { *blanks = 1; continue; }
    for (i = 0; i < n && !str_ieq(v[i], t); i++) ;
    if (i < n) continue;
    if (n == cap) { char **nv = (char **)realloc(v, (size_t)(cap ? cap * 2 : 16) * sizeof(char *)); if (!nv) break; v = nv; cap = cap ? cap * 2 : 16; }
    v[n++] = str_dup(t);
  }
  if (n > 1) qsort(v, (size_t)n, sizeof(char *), cmp_text);
  (void)k;
  *out = v;
  return n;
}
