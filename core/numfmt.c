/*
 * numfmt.c - Excel's number format language: "#,##0.00", "0%", "d-mmm-yy", "[Red](#,##0)",
 * "_($* #,##0.00_)" and so on. Up to four sections (positive; negative; zero; text), conditions
 * ([>=1000]), colours, quoted literals, \ escapes, _ spacing and * fill, digit placeholders 0 # ?,
 * thousands separators and scaling commas, percent, scientific E+00, fractions # ?/?, dates and
 * times with AM/PM, elapsed [h]:mm, and @ for text.
 */
#include "calc.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>
#include <ctype.h>

#define MAXSECT 4

typedef struct { const char *s; size_t n; int cond; double condv; int hascond; u32 color; } Sect;

static const char *const day_names[7] = { "Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday" };
static const char *const month_names[12] = { "January", "February", "March", "April", "May", "June", "July", "August",
                                              "September", "October", "November", "December" };

/* split code into sections at ; (not inside quotes or brackets) */
static int split(const char *code, Sect *sec)
{
  int n = 0, q = 0, br = 0;
  const char *p = code, *start = code;
  for (;; p++) {
    if (*p == '\\' && p[1] && !q) { p++; continue; }
    if (*p == '"') q = !q;
    else if (!q && *p == '[') br = 1;
    else if (!q && *p == ']') br = 0;
    if (!*p || (*p == ';' && !q && !br)) {
      if (n < MAXSECT) { sec[n].s = start; sec[n].n = (size_t)(p - start); sec[n].hascond = 0; sec[n].color = COLOR_AUTO; n++; }
      if (!*p) break;
      start = p + 1;
    }
  }
  return n;
}
/* [Red], [Color10], [>=100], [$€-409], [h] at the start of a section: colours and conditions are
   taken out (the others stay for the formatter) */
static void sect_prefix(Sect *s)
{
  static const struct { const char *name; u32 rgb; } colors[] = {
    { "black", 0x000000 }, { "blue", 0x0000FF }, { "cyan", 0x00FFFF }, { "green", 0x00FF00 }, { "magenta", 0xFF00FF },
    { "red", 0xFF0000 }, { "white", 0xFFFFFF }, { "yellow", 0xFFFF00 } };
  const char *p = s->s, *end = s->s + s->n;
  for (;;) {
    const char *close;
    size_t len;
    int k;
    while (p < end && *p == ' ') p++;
    if (p >= end || *p != '[') break;
    close = memchr(p, ']', (size_t)(end - p));
    if (!close) break;
    len = (size_t)(close - p - 1);
    {
      char in[32];
      if (len >= sizeof in) break;
      memcpy(in, p + 1, len); in[len] = 0;
      for (k = 0; k < 8; k++) if (str_ieq(in, colors[k].name)) { s->color = colors[k].rgb; break; }
      if (k < 8) { p = close + 1; continue; }
      if (!strncasecmp(in, "color", 5)) { s->color = 0x000000; p = close + 1; continue; }
      if (in[0] == '<' || in[0] == '>' || in[0] == '=') {
        const char *q = in;
        int op;
        if (q[0] == '<' && q[1] == '=') { op = 3; q += 2; } else if (q[0] == '>' && q[1] == '=') { op = 4; q += 2; }
        else if (q[0] == '<' && q[1] == '>') { op = 5; q += 2; } else if (q[0] == '<') { op = 1; q++; }
        else if (q[0] == '>') { op = 2; q++; } else { op = 0; q++; }
        s->hascond = 1; s->cond = op; s->condv = strtod(q, NULL);
        p = close + 1;
        continue;
      }
    }
    break;                                    /* [h], [$...]: left in place */
  }
  s->n -= (size_t)(p - s->s);
  s->s = p;
}
static int cond_true(const Sect *s, double v)
{
  switch (s->cond) {
  case 0: return v == s->condv; case 1: return v < s->condv; case 2: return v > s->condv;
  case 3: return v <= s->condv; case 4: return v >= s->condv; default: return v != s->condv;
  }
}

/* does the section show a date or time? (d, m, y, h, s outside quotes, no digit placeholders before) */
static int sect_is_date(const Sect *s)
{
  const char *p = s->s, *end = s->s + s->n;
  int q = 0;
  for (; p < end; p++) {
    char c = (char)tolower((u8)*p);
    if (*p == '\\' || *p == '_' || *p == '*') { p++; continue; }
    if (*p == '"') { q = !q; continue; }
    if (q) continue;
    if (*p == '[') {
      const char *close = memchr(p, ']', (size_t)(end - p));
      if (close && (tolower((u8)p[1]) == 'h' || tolower((u8)p[1]) == 'm' || tolower((u8)p[1]) == 's')) return 1;
      if (close) { p = close; continue; }
    }
    if (c == 'd' || c == 'y' || c == 'h' || c == 's' || c == 'm') return 1;
    if (c == '0' || c == '#' || c == '?') return 0;
  }
  return 0;
}
int fmt_is_date(const char *code)
{
  Sect sec[MAXSECT];
  int n = split(code, sec);
  if (!n) return 0;
  sect_prefix(&sec[0]);
  return sect_is_date(&sec[0]);
}

/* ------------------------------------------------------------------ output helpers */
typedef struct { char *o; size_t cap, n; } Out;
static void put(Out *o, const char *s, size_t n) { while (n-- && o->n + 1 < o->cap) o->o[o->n++] = *s++; o->o[o->n] = 0; }
static void putc_(Out *o, char c) { put(o, &c, 1); }
static void puts_(Out *o, const char *s) { put(o, s, strlen(s)); }
/* [$€-409]: the currency text (before the -) */
static const char *currency(const char *p, const char *end, Out *o)
{
  const char *close = memchr(p, ']', (size_t)(end - p)), *q;
  if (!close) return end;
  for (q = p + 2; q < close && *q != '-'; q++) putc_(o, *q);
  return close + 1;
}

/* ------------------------------------------------------------------ dates */
static void fmt_date(const Sect *s, double v, Out *o)
{
  const char *p = s->s, *end = s->s + s->n;
  int y, mo, d, h, mi, sec, ampm = 0, wd;
  double days = floor(v), frac = v - days, secs;
  int hasampm = 0;
  long totsecs;
  int subdigits = 0;
  double subsec;
  /* is there an AM/PM? (12-hour clock) */
  {
    const char *q;
    int inq = 0;
    for (q = p; q < end; q++) {
      if (*q == '"') inq = !inq;
      if (!inq && (!strncasecmp(q, "AM/PM", 5) || !strncasecmp(q, "A/P", 3))) hasampm = 1;
    }
    /* fractional seconds: ss.0, ss.00 */
    for (q = p, inq = 0; q < end; q++) {
      if (*q == '"') inq = !inq;
      if (!inq && tolower((u8)*q) == 's' && q + 1 < end && q[1] == '.') {
        const char *z = q + 2;
        while (z < end && *z == '0' && subdigits < 3) { subdigits++; z++; }
      }
    }
  }
  secs = frac * 86400;
  {
    double scale = pow(10.0, subdigits);
    double r = floor(secs * scale + 0.5) / scale;
    if (r >= 86400) { r -= 86400; days += 1; }
    secs = r;
  }
  totsecs = (long)floor(secs);
  subsec = secs - totsecs;
  serial_ymd(days, &y, &mo, &d);
  wd = serial_weekday(days);
  h = (int)(totsecs / 3600); mi = (int)(totsecs % 3600 / 60); sec = (int)(totsecs % 60);
  if (hasampm) { ampm = h >= 12; h %= 12; if (h == 0) h = 12; }
  {
    int q = 0;
    const char *last_hour = NULL;          /* "m" after h means minutes */
    while (p < end) {
      char c = (char)tolower((u8)*p);
      int k = 1;
      if (q) { if (*p == '"') q = 0; else putc_(o, *p); p++; continue; }
      if (*p == '"') { q = 1; p++; continue; }
      if (*p == '\\' && p + 1 < end) { putc_(o, p[1]); p += 2; continue; }
      if (*p == '_' && p + 1 < end) { putc_(o, ' '); p += 2; continue; }
      if (*p == '*' && p + 1 < end) { p += 2; continue; }
      if (*p == '[') {
        const char *close = memchr(p, ']', (size_t)(end - p));
        if (close && p[1] == '$') { p = currency(p, end, o); continue; }
        if (close) {
          /* elapsed time: [h], [mm], [ss] */
          char t = (char)tolower((u8)p[1]);
          char buf[32];
          int w = (int)(close - p - 1);
          long total = (long)floor(v * 86400 + 0.5);
          if (t == 'h') sprintf(buf, "%0*ld", w, total / 3600);
          else if (t == 'm') sprintf(buf, "%0*ld", w, total / 60);
          else sprintf(buf, "%0*ld", w, total);
          puts_(o, buf);
          if (t == 'h') { last_hour = close; mi = (int)(total % 3600 / 60); sec = (int)(total % 60); }
          else if (t == 'm') sec = (int)(total % 60);
          p = close + 1;
          continue;
        }
      }
      while (p + k < end && tolower((u8)p[k]) == c && (c == 'd' || c == 'm' || c == 'y' || c == 'h' || c == 's')) k++;
      if (c == 'y') {
        char buf[16];
        if (k <= 2) sprintf(buf, "%02d", y % 100); else sprintf(buf, "%04d", y);
        puts_(o, buf);
      } else if (c == 'd') {
        char buf[16];
        if (k == 1) sprintf(buf, "%d", d);
        else if (k == 2) sprintf(buf, "%02d", d);
        else if (k == 3) sprintf(buf, "%.3s", day_names[wd]);
        else strcpy(buf, day_names[wd]);
        puts_(o, buf);
      } else if (c == 'h') {
        char buf[16];
        sprintf(buf, k == 1 ? "%d" : "%02d", h);
        puts_(o, buf);
        last_hour = p;
      } else if (c == 's') {
        char buf[16];
        sprintf(buf, k == 1 ? "%d" : "%02d", sec);
        puts_(o, buf);
        if (subdigits && p + k < end && p[k] == '.') {
          char sb[16];
          sprintf(sb, "%.*f", subdigits, subsec);
          puts_(o, sb + 1);                        /* ".5" */
          p += k + 1 + subdigits;
          continue;
        }
      } else if (c == 'm') {
        /* minutes if just after an hour or just before seconds, else the month */
        const char *nx = p + k;
        int minutes = 0;
        while (nx < end && (*nx == ':' || *nx == ' ' || *nx == '.')) nx++;
        if (last_hour) minutes = 1;
        if (nx < end && tolower((u8)*nx) == 's') minutes = 1;
        if (k > 2) minutes = 0;
        if (minutes) {
          char buf[16];
          sprintf(buf, k == 1 ? "%d" : "%02d", mi);
          puts_(o, buf);
          last_hour = NULL;
        } else {
          char buf[16];
          if (k == 1) sprintf(buf, "%d", mo);
          else if (k == 2) sprintf(buf, "%02d", mo);
          else if (k == 3) sprintf(buf, "%.3s", month_names[mo - 1]);
          else if (k == 5) sprintf(buf, "%.1s", month_names[mo - 1]);
          else strcpy(buf, month_names[mo - 1]);
          puts_(o, buf);
        }
      } else if (!strncasecmp(p, "AM/PM", 5)) { puts_(o, ampm ? "PM" : "AM"); p += 5; continue; }
      else if (!strncasecmp(p, "A/P", 3)) { putc_(o, ampm ? (p[0] == 'a' ? 'p' : 'P') : p[0]); p += 3; continue; }
      else if (c == '.' && p + 1 < end && p[1] == '0') { k = 1; while (p + k < end && p[k] == '0') k++; }
      else putc_(o, *p);
      p += k;
    }
  }
}

/* ------------------------------------------------------------------ numbers */
typedef struct {
  int intdig, intzero;            /* integer placeholders; how many are 0 (forced digits) */
  int decdig;                     /* decimal placeholders */
  int thousands;                  /* a comma among the integer placeholders */
  int scale;                      /* trailing commas: divide by 1000 each */
  int percent;
  int sci, expdig, expplus;       /* E+00 */
  int frac, numdig, dendig, denfixed;   /* fractions: # ?/?, # ?/8 */
  int hasdigits;
} Shape;
static void analyse(const Sect *s, Shape *sh)
{
  const char *p = s->s, *end = s->s + s->n;
  int q = 0, afterdot = 0, afterexp = 0, afterslash = 0;
  int lastdigit_was = 0;            /* a digit placeholder came before (for scaling commas) */
  memset(sh, 0, sizeof *sh);
  for (; p < end; p++) {
    char c = *p;
    if (q) { if (c == '"') q = 0; continue; }
    if (c == '"') { q = 1; continue; }
    if (c == '\\' || c == '_' || c == '*') { p++; continue; }
    if (c == '[') { const char *close = memchr(p, ']', (size_t)(end - p)); if (close) p = close; continue; }
    if (c == '0' || c == '#' || c == '?') {
      sh->hasdigits = 1;
      if (afterexp) sh->expdig++;
      else if (afterslash) { sh->dendig++; }
      else if (afterdot) sh->decdig++;
      else { sh->intdig++; if (c == '0') sh->intzero++; }
      lastdigit_was = 1;
      continue;
    }
    if (isdigit((u8)c) && afterslash) {
      /* a fixed denominator: # ?/8 */
      sh->denfixed = sh->denfixed * 10 + (c - '0');
      continue;
    }
    if (c == '.' && !afterexp && !afterslash) { afterdot = 1; continue; }
    if (c == ',') {
      /* between integer placeholders: thousands; after the last placeholder: scaling */
      const char *n = p + 1;
      while (n < end && *n == ',') n++;
      if (lastdigit_was && n < end && (*n == '0' || *n == '#' || *n == '?') && !afterdot) sh->thousands = 1;
      else if (lastdigit_was) sh->scale++;
      continue;
    }
    if (c == '%') { sh->percent++; continue; }
    if ((c == 'E' || c == 'e') && p + 1 < end && (p[1] == '+' || p[1] == '-')) { sh->sci = 1; sh->expplus = p[1] == '+'; afterexp = 1; p++; continue; }
    if (c == '/' && lastdigit_was && !afterexp) {
      /* the numerator was the last group of integer placeholders */
      sh->frac = 1; afterslash = 1;
      continue;
    }
    if (c != ' ' || afterslash) lastdigit_was = c == ' ' ? lastdigit_was : 0;
  }
  if (sh->frac) {
    /* split the integer placeholders into "whole" and "numerator" by the space before the numerator */
    const char *slash = NULL, *q2;
    int inq = 0;
    for (q2 = s->s; q2 < end; q2++) { if (*q2 == '"') inq = !inq; if (!inq && *q2 == '/') { slash = q2; break; } }
    if (slash) {
      int num = 0;
      const char *k = slash - 1;
      while (k >= s->s && (*k == '0' || *k == '#' || *k == '?')) { num++; k--; }
      sh->numdig = num;
      sh->intdig -= num;
      if (sh->intdig < 0) sh->intdig = 0;
    }
  }
}
/* the integer digits of v (v >= 0, already rounded to whole) into buf; "0" for zero */
static void int_digits(double v, char *buf)
{
  if (v < 1e15) sprintf(buf, "%.0f", floor(v + 0.5));
  else sprintf(buf, "%.0f", v);
}
/* more digits than a double holds (v rounded to decdig decimals is 1E15 or more): as Excel, 15
   significant digits and then zeros (1.005 with twenty decimals is 1.00500000000000000000, not the
   binary value's 1.00499999999999983616; 123456789012345678 is 123456789012345000). ib gets the
   integer digits ("0" for none), db decdig decimals; both hold up to 380 characters. */
static void sig_digits(double v, int decdig, char *ib, char *db)
{
  char t[48], fm[16], dg[20];
  int k = 15, x, nd = 0, i;
  const char *e;
  sprintf(t, "%.14e", v);
  x = atoi(strchr(t, 'e') + 1);
  if (x + 1 + decdig < k) {           /* fewer digits shown than 15: round there */
    k = x + 1 + decdig;
    if (k < 1) k = 1;
    sprintf(fm, "%%.%de", k - 1);    /* (built, not %.*e: vbcc takes a precision of 0 there as none) */
    sprintf(t, fm, v);
    x = atoi(strchr(t, 'e') + 1);
  }
  for (e = t; *e && *e != 'e'; e++) if (*e >= '0' && *e <= '9' && nd < 19) dg[nd++] = *e;
  if (x > 370) x = 370;
  if (x >= 0) {
    for (i = 0; i <= x; i++) ib[i] = i < nd ? dg[i] : '0';
    ib[x + 1] = 0;
  } else strcpy(ib, "0");
  for (i = 0; i < decdig; i++) { int p = x + 1 + i; db[i] = p >= 0 && p < nd ? dg[p] : '0'; }
  db[decdig] = 0;
}
/* the best fraction for x with a denominator of at most dmax (or exactly dfix) */
static void best_frac(double x, int dmax, int dfix, long *num, long *den)
{
  if (dfix > 0) { *den = dfix; *num = (long)floor(x * dfix + 0.5); return; }
  {
    long bn = 0, bd = 1, d;
    double berr = x;
    for (d = 1; d <= dmax; d++) {
      long n = (long)floor(x * d + 0.5);
      double err = fabs(x - (double)n / d);
      if (err < berr - 1e-12) { berr = err; bn = n; bd = d; }
    }
    *num = bn; *den = bd;
  }
}
/* fill the section's template with the digits */
static void fmt_number(Book *b, const Sect *s, double v, int neg_sign, Out *o)
{
  Shape sh;
  const char *p = s->s, *end = s->s + s->n;
  char ib[400], db[400], eb[16];
  int ilen, di = 0, ei = 0, q = 0, exp10 = 0, intleft, afterdot = 0, afterexp = 0, insect_frac = 0;
  long fnum = 0, fden = 1;
  double whole;
  int numleft, denleft;
  char nb[32], denb[32];
  (void)b;
  analyse(s, &sh);
  if (!sh.hasdigits && !sh.frac) {
    /* only literals (e.g. "Zero" or "-"): print them */
    if (neg_sign) putc_(o, '-');
    while (p < end) {
      if (q) { if (*p == '"') q = 0; else putc_(o, *p); p++; continue; }
      if (*p == '"') { q = 1; p++; continue; }
      if (*p == '\\' && p + 1 < end) { putc_(o, p[1]); p += 2; continue; }
      if (*p == '_' && p + 1 < end) { putc_(o, ' '); p += 2; continue; }
      if (*p == '*' && p + 1 < end) { p += 2; continue; }
      if (*p == '[' && p + 1 < end && p[1] == '$') { p = currency(p, end, o); continue; }
      if (*p == '%') { putc_(o, '%'); p++; continue; }
      if (!strncasecmp(p, "General", 7)) { char g[40]; num_general(v, g, 11); puts_(o, g); p += 7; continue; }
      putc_(o, *p++);
    }
    return;
  }
  { int k; for (k = 0; k < sh.percent; k++) v *= 100; }
  { int k; for (k = 0; k < sh.scale; k++) v /= 1000; }
  if (sh.sci) {
    if (v != 0) {
      /* the exponent is a multiple of the integer placeholder count when there are several (##0.0E+0) */
      int step = sh.intdig > 1 ? sh.intdig : 1;
      exp10 = (int)floor(log10(v));
      if (step > 1) exp10 = (int)floor((double)exp10 / step) * step;
      else exp10 -= sh.intdig > 0 ? sh.intdig - 1 : 0;
      v /= pow(10.0, exp10);
      {
        double r = floor(v * pow(10.0, sh.decdig) + 0.5) / pow(10.0, sh.decdig);
        if (r >= pow(10.0, step > 1 ? step : (sh.intdig > 0 ? sh.intdig : 1))) { v /= 10; exp10++; }
      }
    }
  }
  if (sh.frac) {
    double fr;
    whole = sh.intdig ? floor(v) : 0;
    fr = v - whole;
    best_frac(fr, sh.dendig >= 3 ? 999 : sh.dendig == 2 ? 99 : 9, sh.denfixed, &fnum, &fden);
    if (fnum == fden && sh.intdig) { whole += 1; fnum = 0; }
    v = whole;
    sprintf(nb, "%ld", fnum);
    sprintf(denb, "%ld", fden);
    insect_frac = 1;
  }
  /* round to the decimals shown and split into integer and decimal digits */
  {
    double r = floor(v * pow(10.0, sh.decdig) + 0.5 + 1e-9);
    if (r < 1e15 || !(v == v) || sh.decdig > 380) {
      double ip = floor(r / pow(10.0, sh.decdig));
      double dp = r - ip * pow(10.0, sh.decdig);
      int_digits(ip, ib);
      if (sh.decdig && sh.decdig <= 380) { sprintf(db, "%0*.0f", sh.decdig, dp); } else db[0] = 0;
    } else sig_digits(v, sh.decdig, ib, db);
  }
  if (!strcmp(ib, "0") && sh.intzero == 0) ib[0] = 0;   /* "#.00" shows .50 */
  if (insect_frac && !strcmp(ib, "0") && fnum) ib[0] = 0;
  ilen = (int)strlen(ib);
  if (sh.sci) { sprintf(eb, "%d", exp10 < 0 ? -exp10 : exp10); }
  intleft = sh.intdig;                     /* placeholders still to come */
  numleft = sh.numdig; denleft = sh.dendig;
  if (neg_sign) putc_(o, '-');
  while (p < end) {
    char c = *p;
    if (q) { if (c == '"') q = 0; else putc_(o, c); p++; continue; }
    if (c == '"') { q = 1; p++; continue; }
    if (c == '\\' && p + 1 < end) { putc_(o, p[1]); p += 2; continue; }
    if (c == '_' && p + 1 < end) { putc_(o, ' '); p += 2; continue; }
    if (c == '*' && p + 1 < end) { p += 2; continue; }
    if (c == '[') {
      if (p + 1 < end && p[1] == '$') { p = currency(p, end, o); continue; }
      { const char *close = memchr(p, ']', (size_t)(end - p)); p = close ? close + 1 : end; continue; }
    }
    if (c == '0' || c == '#' || c == '?') {
      if (afterexp) {
        /* exponent digits, right-aligned with zeros */
        int need = sh.expdig, have = (int)strlen(eb);
        if (ei == 0) {
          int k;
          for (k = have; k < need; k++) putc_(o, '0');
          puts_(o, eb);
        }
        ei++;
        p++;
        continue;
      }
      if (insect_frac && numleft > 0 && intleft == 0) {
        /* numerator placeholders: print the numerator at the first one */
        if (numleft == sh.numdig) {
          int pad = sh.numdig - (int)strlen(nb), k;
          if (fnum == 0) { for (k = 0; k < sh.numdig; k++) putc_(o, ' '); }
          else { for (k = 0; k < pad; k++) putc_(o, c == '?' ? ' ' : c == '0' ? '0' : ' '); puts_(o, nb); }
        }
        numleft--;
        p++;
        continue;
      }
      if (insect_frac && intleft == 0 && numleft == 0) {
        /* denominator placeholders */
        if (denleft == sh.dendig) {
          int pad = sh.dendig - (int)strlen(denb), k;
          if (fnum == 0) { for (k = 0; k < sh.dendig; k++) putc_(o, ' '); }
          else { puts_(o, denb); for (k = 0; k < pad; k++) putc_(o, ' '); }
        }
        denleft--;
        p++;
        continue;
      }
      if (!afterdot) {
        /* integer placeholder: the first one prints any extra leading digits */
        int pos = intleft;                 /* this placeholder's position from the right (1 = last) */
        if (intleft == sh.intdig) {
          int extra = ilen - sh.intdig, k;
          for (k = 0; k < extra; k++) {
            putc_(o, ib[k]);
            if (sh.thousands && (ilen - k - 1) % 3 == 0 && ilen - k - 1 > 0) putc_(o, ',');
          }
        }
        {
          int idx = ilen - pos;            /* the digit for this placeholder */
          if (idx >= 0) {
            putc_(o, ib[idx]);
            if (sh.thousands && pos > 1 && (pos - 1) % 3 == 0) putc_(o, ',');
          } else if (c == '0') {
            putc_(o, '0');
            if (sh.thousands && pos > 1 && (pos - 1) % 3 == 0) putc_(o, ',');
          } else if (c == '?') putc_(o, ' ');
        }
        intleft--;
        p++;
        continue;
      }
      /* decimal placeholder */
      {
        char d = di < (int)strlen(db) ? db[di] : '0';
        int k, rest_zero = 1;
        for (k = di; k < (int)strlen(db); k++) if (db[k] != '0') rest_zero = 0;
        if (c == '0' || !rest_zero) putc_(o, d);
        else if (c == '?') putc_(o, ' ');
        di++;
      }
      p++;
      continue;
    }
    if (c == '.' && !afterexp) {
      /* the decimal point (dropped if no decimals print and the format has only # after it? Excel keeps it) */
      afterdot = 1;
      putc_(o, '.');
      p++;
      continue;
    }
    if (c == ',') { p++; continue; }            /* separators and scaling are already applied */
    if ((c == 'E' || c == 'e') && p + 1 < end && (p[1] == '+' || p[1] == '-') && sh.sci) {
      putc_(o, 'E');
      putc_(o, exp10 < 0 ? '-' : (sh.expplus ? '+' : 0));
      if (!(exp10 < 0) && !sh.expplus) o->n--, o->o[o->n] = 0;
      afterexp = 1;
      p += 2;
      continue;
    }
    if (c == '/' && insect_frac) {
      if (fnum == 0) putc_(o, ' '); else putc_(o, '/');
      p++;
      continue;
    }
    if (c == ' ' && insect_frac && fnum == 0 && intleft == 0 && numleft > 0) { putc_(o, ' '); p++; continue; }
    putc_(o, c);
    p++;
  }
  (void)ei;
}
/* the text section: @ is the text */
static void fmt_text(const Sect *s, const char *text, Out *o)
{
  const char *p = s->s, *end = s->s + s->n;
  int q = 0;
  while (p < end) {
    if (q) { if (*p == '"') q = 0; else putc_(o, *p); p++; continue; }
    if (*p == '"') { q = 1; p++; continue; }
    if (*p == '\\' && p + 1 < end) { putc_(o, p[1]); p += 2; continue; }
    if (*p == '_' && p + 1 < end) { putc_(o, ' '); p += 2; continue; }
    if (*p == '*' && p + 1 < end) { p += 2; continue; }
    if (*p == '@') { puts_(o, text); p++; continue; }
    if (*p == '[' && p + 1 < end && p[1] == '$') { p = currency(p, end, o); continue; }
    putc_(o, *p++);
  }
}
static int sect_has_at(const Sect *s)
{
  const char *p;
  int q = 0;
  for (p = s->s; p < s->s + s->n; p++) { if (*p == '"') q = !q; else if (!q && *p == '@') return 1; }
  return 0;
}

/* see calc.h */
void fmt_value(Book *b, const char *code, const Value *v, char *out, size_t cap, u32 *color, int width)
{
  Sect sec[MAXSECT];
  int n, i;
  Out o;
  o.o = out; o.cap = cap; o.n = 0;
  if (cap) out[0] = 0;
  *color = COLOR_AUTO;
  if (!code || !*code || !strcmp(code, "checkbox")) code = "General";   /* (a checkbox cell, analysis.h: General but for TRUE/FALSE) */
  switch (v->t) {
  case V_EMPTY: return;
  case V_BOOL: puts_(&o, v->e ? "TRUE" : "FALSE"); return;
  case V_ERR: puts_(&o, err_name(v->e)); return;
  default: break;
  }
  n = split(code, sec);
  for (i = 0; i < n; i++) sect_prefix(&sec[i]);
  if (v->t == V_STR) {
    /* the fourth section, or a section with @; otherwise the text as it is */
    if (n >= 4) { *color = sec[3].color; fmt_text(&sec[3], v->s, &o); return; }
    for (i = 0; i < n; i++) if (sect_has_at(&sec[i])) { *color = sec[i].color; fmt_text(&sec[i], v->s, &o); return; }
    puts_(&o, v->s);
    return;
  }
  {
    double x = v->n;
    const Sect *s = &sec[0];
    int neg = 0;
    if (n >= 1 && (sec[0].hascond || (n > 1 && sec[1].hascond))) {
      /* conditional sections: the first whose condition holds, else the last */
      int k, chosen = -1;
      for (k = 0; k < n && k < 2; k++) if (sec[k].hascond && cond_true(&sec[k], x)) { chosen = k; break; }
      if (chosen < 0) {
        /* no condition met: the next section without a condition */
        for (k = 0; k < n; k++) if (!sec[k].hascond && !sect_has_at(&sec[k])) { chosen = k; break; }
        if (chosen < 0) chosen = n - 1;
      }
      s = &sec[chosen];
      if (x < 0 && !(s->hascond && (s->cond == 1 || s->cond == 3) && s->condv <= 0)) neg = chosen == 0 ? 1 : 0;
      if (x < 0 && s->hascond && (s->cond == 1 || s->cond == 3)) x = -x, neg = 0;
    } else if (x > 0 || n == 1) {
      s = &sec[0];
      if (x < 0) neg = 1;
    } else if (x < 0) {
      if (n >= 2) { s = &sec[1]; x = -x; } else neg = 1;
    } else {
      s = n >= 3 ? &sec[2] : &sec[0];
    }
    if (x < 0) { x = -x; }
    *color = s->color;
    /* General (possibly with colour or text around it) */
    {
      const char *g = s->s;
      size_t len = s->n;
      while (len && *g == ' ') { g++; len--; }
      if (len >= 7 && !strncasecmp(g, "General", 7) && len == 7) {
        char tmp[64];
        num_general(neg ? -x : x, tmp, width ? width : 11);
        puts_(&o, tmp);
        return;
      }
      if (!len) return;                     /* an empty section hides the value */
    }
    if (sect_is_date(s)) {
      if (v->n < 0 || v->n > 2958465.99999) { size_t k; for (k = 0; k + 1 < cap && k < 11; k++) out[k] = '#'; out[k] = 0; return; }
      fmt_date(s, v->n, &o);
      return;
    }
    /* "-0.00" would be wrong when the rounded value is zero */
    if (neg) {
      Shape sh;
      double r;
      analyse(s, &sh);
      r = x;
      { int k; for (k = 0; k < sh.percent; k++) r *= 100; for (k = 0; k < sh.scale; k++) r /= 1000; }
      if (!sh.sci && !sh.frac && floor(r * pow(10.0, sh.decdig) + 0.5) == 0) neg = 0;
    }
    fmt_number(b, s, x, neg, &o);
  }
}
