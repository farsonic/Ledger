/*
 * func_eng.c - the engineering functions (number bases, bits, CONVERT, ERF, Bessel, complex
 * numbers) and the maths & trigonometry functions added after the core set (see func.c for the
 * conventions). Its table, func_eng_defs, is in name order and merged with the others by
 * func_init. Behaviour follows Excel's documentation; docs/functions-eng.txt describes each one.
 */
#include "calc.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>
#include <ctype.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif
#define SQRTPI_ 1.77245385090551602729
#define EULER_ 0.57721566490153286061

/* an optional argument: given and not left out */
#define HAS(i) (n > (i) && !IS_MISSING(&a[i]))

/* ------------------------------------------------------------------ number bases */
/* the text of a number argument for BIN2DEC and the like: numbers become their digits, logical
   values are #VALUE!. 0 or an E_ error; out has room for 64 bytes */
static int base_text(Ctx *cx, const Value *v, char *out)
{
  Value s;
  int e = 0;
  if (IS_MISSING(v)) { strcpy(out, "0"); return 0; }
  s = scalar(cx, v);
  if (s.t == V_ERR) e = s.e;
  else if (s.t == V_BOOL) e = E_VALUE;
  else if (s.t == V_NUM) {
    if (s.n < 0 || s.n >= 1e15 || s.n != floor(s.n)) e = E_NUM;
    else sprintf(out, "%.0f", s.n);
  } else if (s.t == V_STR) {
    if (strlen(s.s) > 63) e = E_NUM; else strcpy(out, s.s);
  } else strcpy(out, "");
  val_free(&s);
  return e;
}
/* digits in base (2, 8, 16), at most 10, the 10th digit's top bit making it negative (two's
   complement over 10 digits) */
static int base_parse(const char *s, int base, double *out)
{
  double v = 0, full = ipow((double)base, 10);
  int k, len = (int)strlen(s);
  if (len > 10) return E_NUM;
  for (k = 0; k < len; k++) {
    int c = toupper((unsigned char)s[k]), d;
    if (c >= '0' && c <= '9') d = c - '0';
    else if (c >= 'A' && c <= 'F') d = c - 'A' + 10;
    else return E_NUM;
    if (d >= base) return E_NUM;
    v = v * base + d;
  }
  if (len == 10 && v >= full / 2) v -= full;
  *out = v;
  return 0;
}
/* the places argument: 0 when not given, else 1..10 */
static int base_places(Ctx *cx, Value *a, int n, int i, int *places)
{
  double p;
  int e;
  *places = 0;
  if (!HAS(i)) return 0;
  if ((e = arg_num(cx, &a[i], &p))) return e;
  p = floor(p);
  if (p < 1 || p > 10) return E_NUM;
  *places = (int)p;
  return 0;
}
/* v (an integer) written in base, padded to places; negatives as ten digits of two's complement */
static Value base_out(double v, int base, int places)
{
  double full = ipow((double)base, 10), lim = full / 2;
  char tmp[16], out[16];
  int k = 0, j;
  if (v < -lim || v >= lim) return val_err(E_NUM);
  if (v < 0) { v += full; places = 10; }
  do { double d = dmod(v, base); tmp[k++] = "0123456789ABCDEF"[(int)d]; v = floor(v / base); } while (v > 0 && k < 15);
  if (places && k > places) return val_err(E_NUM);
  for (j = 0; j < places - k; j++) out[j] = '0';
  for (j = 0; j < k; j++) out[(places > k ? places - k : 0) + j] = tmp[k - 1 - j];
  out[places > k ? places : k] = 0;
  return val_str(out);
}
static Value conv_from(Ctx *cx, Value *a, int n, int from, int to)
{
  char s[64];
  double v;
  int e, places;
  if ((e = base_text(cx, &a[0], s))) return val_err(e);
  if ((e = base_parse(s, from, &v))) return val_err(e);
  if (to == 10) return val_num(v);
  if ((e = base_places(cx, a, n, 1, &places))) return val_err(e);
  return base_out(v, to, places);
}
static Value conv_dec(Ctx *cx, Value *a, int n, int to)
{
  double v;
  int e, places;
  if ((e = arg_num(cx, &a[0], &v))) return val_err(e);
  if ((e = base_places(cx, a, n, 1, &places))) return val_err(e);
  v = v < 0 ? ceil(v) : floor(v);
  return base_out(v, to, places);
}
static Value f_bin2dec(Ctx *cx, Value *a, int n) { return conv_from(cx, a, n, 2, 10); }
static Value f_bin2hex(Ctx *cx, Value *a, int n) { return conv_from(cx, a, n, 2, 16); }
static Value f_bin2oct(Ctx *cx, Value *a, int n) { return conv_from(cx, a, n, 2, 8); }
static Value f_oct2bin(Ctx *cx, Value *a, int n) { return conv_from(cx, a, n, 8, 2); }
static Value f_oct2dec(Ctx *cx, Value *a, int n) { return conv_from(cx, a, n, 8, 10); }
static Value f_oct2hex(Ctx *cx, Value *a, int n) { return conv_from(cx, a, n, 8, 16); }
static Value f_hex2bin(Ctx *cx, Value *a, int n) { return conv_from(cx, a, n, 16, 2); }
static Value f_hex2dec(Ctx *cx, Value *a, int n) { return conv_from(cx, a, n, 16, 10); }
static Value f_hex2oct(Ctx *cx, Value *a, int n) { return conv_from(cx, a, n, 16, 8); }
static Value f_dec2bin(Ctx *cx, Value *a, int n) { return conv_dec(cx, a, n, 2); }
static Value f_dec2hex(Ctx *cx, Value *a, int n) { return conv_dec(cx, a, n, 16); }
static Value f_dec2oct(Ctx *cx, Value *a, int n) { return conv_dec(cx, a, n, 8); }

/* BASE(number, radix, [min_length]) and DECIMAL(text, radix) */
static Value f_base(Ctx *cx, Value *a, int n)
{
  double v, r, m = 0;
  char tmp[64], out[320];
  int e, k = 0, j, len;
  if ((e = arg_num(cx, &a[0], &v)) || (e = arg_num(cx, &a[1], &r))) return val_err(e);
  if (HAS(2) && (e = arg_num(cx, &a[2], &m))) return val_err(e);
  v = floor(v); r = floor(r); m = floor(m);
  if (v < 0 || v >= 9007199254740992.0 || r < 2 || r > 36 || m < 0 || m > 255) return val_err(E_NUM);
  do { tmp[k++] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ"[(int)dmod(v, r)]; v = floor(v / r); } while (v > 0 && k < 63);
  len = k > (int)m ? k : (int)m;
  for (j = 0; j < len - k; j++) out[j] = '0';
  for (j = 0; j < k; j++) out[len - k + j] = tmp[k - 1 - j];
  out[len] = 0;
  return val_str(out);
}
static Value f_decimal(Ctx *cx, Value *a, int n)
{
  char *s;
  double r, v = 0;
  int e;
  const char *p;
  (void)n;
  if ((e = arg_num(cx, &a[1], &r))) return val_err(e);
  if ((e = arg_str(cx, &a[0], &s))) return val_err(e);
  r = floor(r);
  if (r < 2 || r > 36 || strlen(s) > 255) { free(s); return val_err(E_NUM); }
  for (p = s; *p; p++) {
    int c = toupper((unsigned char)*p), d;
    if (c >= '0' && c <= '9') d = c - '0';
    else if (c >= 'A' && c <= 'Z') d = c - 'A' + 10;
    else { free(s); return val_err(E_NUM); }
    if (d >= r) { free(s); return val_err(E_NUM); }
    v = v * r + d;
  }
  free(s);
  if (v >= 9007199254740992.0) return val_err(E_NUM);
  return val_num(v);
}

/* ------------------------------------------------------------------ bits */
#define BITLIM 281474976710656.0     /* 2^48 */
static int bit_arg(Ctx *cx, const Value *v, double *x)
{
  int e = arg_num(cx, v, x);
  if (e) return e;
  if (*x < 0 || *x >= BITLIM || *x != floor(*x)) return E_NUM;
  return 0;
}
static Value bitop(Ctx *cx, Value *a, int op)
{
  double x, y, r = 0, bit = 1;
  int e;
  if ((e = bit_arg(cx, &a[0], &x)) || (e = bit_arg(cx, &a[1], &y))) return val_err(e);
  /* bit by bit (no 64-bit integers needed) */
  while (x > 0 || y > 0) {
    int bx = dmod(x, 2) != 0, by = dmod(y, 2) != 0, b;
    b = op == 0 ? (bx & by) : op == 1 ? (bx | by) : (bx ^ by);
    if (b) r += bit;
    bit *= 2; x = floor(x / 2); y = floor(y / 2);
  }
  return val_num(r);
}
static Value f_bitand(Ctx *cx, Value *a, int n) { (void)n; return bitop(cx, a, 0); }
static Value f_bitor(Ctx *cx, Value *a, int n) { (void)n; return bitop(cx, a, 1); }
static Value f_bitxor(Ctx *cx, Value *a, int n) { (void)n; return bitop(cx, a, 2); }
static Value bitshift(Ctx *cx, Value *a, int left)
{
  double x, s, r;
  int e;
  if ((e = bit_arg(cx, &a[0], &x)) || (e = arg_num(cx, &a[1], &s))) return val_err(e);
  s = s < 0 ? ceil(s) : floor(s);
  if (fabs(s) > 53) return val_err(E_NUM);
  if (!left) s = -s;
  r = s >= 0 ? x * ipow(2.0, (int)s) : floor(x / ipow(2.0, (int)-s));
  if (r >= BITLIM) return val_err(E_NUM);
  return val_num(r);
}
static Value f_bitlshift(Ctx *cx, Value *a, int n) { (void)n; return bitshift(cx, a, 1); }
static Value f_bitrshift(Ctx *cx, Value *a, int n) { (void)n; return bitshift(cx, a, 0); }

/* ------------------------------------------------------------------ DELTA, GESTEP */
static Value f_delta(Ctx *cx, Value *a, int n)
{
  double x, y = 0;
  int e;
  if ((e = arg_num(cx, &a[0], &x))) return val_err(e);
  if (HAS(1) && (e = arg_num(cx, &a[1], &y))) return val_err(e);
  return val_num(x == y);
}
static Value f_gestep(Ctx *cx, Value *a, int n)
{
  double x, y = 0;
  int e;
  if ((e = arg_num(cx, &a[0], &x))) return val_err(e);
  if (HAS(1) && (e = arg_num(cx, &a[1], &y))) return val_err(e);
  return val_num(x >= y);
}

/* ------------------------------------------------------------------ CONVERT */
/* units as Excel's CONVERT documents them, each with its size in the dimension's base unit
   (gram, metre, second, pascal, newton, joule, watt, tesla, kelvin, cubic metre, square metre,
   bit, metre per second). pre: 0 no prefixes, 1/2/3 metric prefixes (raised to that power: m2
   with "k" is a square kilometre), 4 metric and binary prefixes */
enum { D_MASS, D_LEN, D_TIME, D_PRES, D_FORCE, D_ENERGY, D_POWER, D_MAG, D_TEMP, D_VOL, D_AREA, D_INFO, D_SPEED };
typedef struct { const char *name; u8 dim, pre; double f; } Unit;
#define IN_ 0.0254
#define FT_ 0.3048
#define YD_ 0.9144
#define MI_ 1609.344
#define NMI_ 1852.0
#define LY_ 9460730472580800.0
#define PICA_ (0.0254 / 72.0)
#define GAL_ 0.003785411784
static const Unit units[] = {
  { "g", D_MASS, 1, 1.0 }, { "sg", D_MASS, 0, 14593.9029372064 }, { "lbm", D_MASS, 0, 453.59237 },
  { "u", D_MASS, 1, 1.66053906660e-24 }, { "ozm", D_MASS, 0, 28.349523125 }, { "grain", D_MASS, 0, 0.06479891 },
  { "cwt", D_MASS, 0, 45359.237 }, { "shweight", D_MASS, 0, 45359.237 }, { "uk_cwt", D_MASS, 0, 50802.34544 },
  { "lcwt", D_MASS, 0, 50802.34544 }, { "hweight", D_MASS, 0, 50802.34544 }, { "stone", D_MASS, 0, 6350.29318 },
  { "ton", D_MASS, 0, 907184.74 }, { "uk_ton", D_MASS, 0, 1016046.9088 }, { "LTON", D_MASS, 0, 1016046.9088 },
  { "brton", D_MASS, 0, 1016046.9088 },
  { "m", D_LEN, 1, 1.0 }, { "mi", D_LEN, 0, MI_ }, { "Nmi", D_LEN, 0, NMI_ }, { "in", D_LEN, 0, IN_ },
  { "ft", D_LEN, 0, FT_ }, { "yd", D_LEN, 0, YD_ }, { "ang", D_LEN, 1, 1e-10 }, { "ell", D_LEN, 0, 1.143 },
  { "ly", D_LEN, 1, LY_ }, { "parsec", D_LEN, 1, 30856775814913673.0 }, { "pc", D_LEN, 1, 30856775814913673.0 },
  { "Picapt", D_LEN, 0, PICA_ }, { "Pica", D_LEN, 0, PICA_ }, { "pica", D_LEN, 0, 0.0254 / 6.0 },
  { "survey_mi", D_LEN, 0, 1609.3472186944373 },
  { "yr", D_TIME, 0, 31557600.0 }, { "day", D_TIME, 0, 86400.0 }, { "d", D_TIME, 0, 86400.0 },
  { "hr", D_TIME, 0, 3600.0 }, { "mn", D_TIME, 0, 60.0 }, { "min", D_TIME, 0, 60.0 },
  { "sec", D_TIME, 1, 1.0 }, { "s", D_TIME, 1, 1.0 },
  { "Pa", D_PRES, 1, 1.0 }, { "p", D_PRES, 1, 1.0 }, { "atm", D_PRES, 1, 101325.0 }, { "at", D_PRES, 1, 101325.0 },
  { "mmHg", D_PRES, 1, 133.322387415 }, { "psi", D_PRES, 0, 6894.75729316836 }, { "Torr", D_PRES, 0, 101325.0 / 760.0 },
  { "N", D_FORCE, 1, 1.0 }, { "dyn", D_FORCE, 1, 1e-5 }, { "dy", D_FORCE, 1, 1e-5 },
  { "lbf", D_FORCE, 0, 4.4482216152605 }, { "pond", D_FORCE, 1, 0.00980665 },
  { "J", D_ENERGY, 1, 1.0 }, { "e", D_ENERGY, 1, 1e-7 }, { "c", D_ENERGY, 1, 4.184 }, { "cal", D_ENERGY, 1, 4.1868 },
  { "eV", D_ENERGY, 1, 1.602176634e-19 }, { "ev", D_ENERGY, 1, 1.602176634e-19 },
  { "HPh", D_ENERGY, 0, 2684519.53769617 }, { "hh", D_ENERGY, 0, 2684519.53769617 },
  { "Wh", D_ENERGY, 1, 3600.0 }, { "wh", D_ENERGY, 1, 3600.0 }, { "flb", D_ENERGY, 0, 1.3558179483314 },
  { "BTU", D_ENERGY, 0, 1055.05585262 }, { "btu", D_ENERGY, 0, 1055.05585262 },
  { "W", D_POWER, 1, 1.0 }, { "w", D_POWER, 1, 1.0 }, { "HP", D_POWER, 0, 745.69987158227 },
  { "h", D_POWER, 0, 745.69987158227 }, { "PS", D_POWER, 0, 735.49875 },
  { "T", D_MAG, 1, 1.0 }, { "ga", D_MAG, 1, 1e-4 },
  { "C", D_TEMP, 0, 0 }, { "cel", D_TEMP, 0, 0 }, { "F", D_TEMP, 0, 1 }, { "fah", D_TEMP, 0, 1 },
  { "K", D_TEMP, 1, 2 }, { "kel", D_TEMP, 1, 2 }, { "Rank", D_TEMP, 0, 3 }, { "Reau", D_TEMP, 0, 4 },
  { "l", D_VOL, 1, 0.001 }, { "L", D_VOL, 1, 0.001 }, { "lt", D_VOL, 1, 0.001 },
  { "tsp", D_VOL, 0, GAL_ / 768.0 }, { "tspm", D_VOL, 0, 5e-6 }, { "tbs", D_VOL, 0, GAL_ / 256.0 },
  { "oz", D_VOL, 0, GAL_ / 128.0 }, { "cup", D_VOL, 0, GAL_ / 16.0 }, { "pt", D_VOL, 0, GAL_ / 8.0 },
  { "us_pt", D_VOL, 0, GAL_ / 8.0 }, { "uk_pt", D_VOL, 0, 0.00056826125 }, { "qt", D_VOL, 0, GAL_ / 4.0 },
  { "uk_qt", D_VOL, 0, 0.0011365225 }, { "gal", D_VOL, 0, GAL_ }, { "uk_gal", D_VOL, 0, 0.00454609 },
  { "m3", D_VOL, 3, 1.0 }, { "mi3", D_VOL, 0, MI_ * MI_ * MI_ }, { "Nmi3", D_VOL, 0, NMI_ * NMI_ * NMI_ },
  { "yd3", D_VOL, 0, YD_ * YD_ * YD_ }, { "ft3", D_VOL, 0, FT_ * FT_ * FT_ }, { "in3", D_VOL, 0, IN_ * IN_ * IN_ },
  { "ang3", D_VOL, 3, 1e-30 }, { "ly3", D_VOL, 0, LY_ * LY_ * LY_ }, { "Picapt3", D_VOL, 0, PICA_ * PICA_ * PICA_ },
  { "Pica3", D_VOL, 0, PICA_ * PICA_ * PICA_ }, { "barrel", D_VOL, 0, 0.158987294928 },
  { "bushel", D_VOL, 0, 0.03523907016688 }, { "MTON", D_VOL, 0, 1.13267386368 },
  { "GRT", D_VOL, 0, 2.8316846592 }, { "regton", D_VOL, 0, 2.8316846592 },
  { "m2", D_AREA, 2, 1.0 }, { "ha", D_AREA, 0, 10000.0 }, { "ar", D_AREA, 1, 100.0 },
  { "ft2", D_AREA, 0, FT_ * FT_ }, { "in2", D_AREA, 0, IN_ * IN_ }, { "yd2", D_AREA, 0, YD_ * YD_ },
  { "mi2", D_AREA, 0, MI_ * MI_ }, { "Nmi2", D_AREA, 0, NMI_ * NMI_ }, { "ang2", D_AREA, 2, 1e-20 },
  { "ly2", D_AREA, 0, LY_ * LY_ }, { "Picapt2", D_AREA, 0, PICA_ * PICA_ }, { "Pica2", D_AREA, 0, PICA_ * PICA_ },
  { "uk_acre", D_AREA, 0, 4046.8564224 }, { "us_acre", D_AREA, 0, 4046.872609874252 }, { "Morgen", D_AREA, 0, 2500.0 },
  { "bit", D_INFO, 4, 1.0 }, { "byte", D_INFO, 4, 8.0 },
  { "m/s", D_SPEED, 1, 1.0 }, { "m/sec", D_SPEED, 1, 1.0 }, { "m/h", D_SPEED, 1, 1.0 / 3600.0 },
  { "m/hr", D_SPEED, 1, 1.0 / 3600.0 }, { "mph", D_SPEED, 0, MI_ / 3600.0 }, { "kn", D_SPEED, 0, NMI_ / 3600.0 },
  { "admkn", D_SPEED, 0, 6080.0 * FT_ / 3600.0 },
};
static const struct { const char *p; double f; } si_pre[] = {
  { "da", 1e1 }, { "Y", 1e24 }, { "Z", 1e21 }, { "E", 1e18 }, { "P", 1e15 }, { "T", 1e12 }, { "G", 1e9 }, { "M", 1e6 },
  { "k", 1e3 }, { "h", 1e2 }, { "e", 1e1 }, { "d", 1e-1 }, { "c", 1e-2 }, { "m", 1e-3 }, { "u", 1e-6 }, { "n", 1e-9 },
  { "p", 1e-12 }, { "f", 1e-15 }, { "a", 1e-18 }, { "z", 1e-21 }, { "y", 1e-24 },
}, bin_pre[] = {
  { "ki", 1024.0 }, { "Mi", 1048576.0 }, { "Gi", 1073741824.0 }, { "Ti", 1099511627776.0 },
  { "Pi", 1125899906842624.0 }, { "Ei", 1152921504606846976.0 }, { "Zi", 1180591620717411303424.0 },
  { "Yi", 1208925819614629174706176.0 },
};
static const Unit *unit_find(const char *s)
{
  size_t i;
  for (i = 0; i < sizeof units / sizeof units[0]; i++) if (!strcmp(units[i].name, s)) return &units[i];
  return NULL;
}
/* a unit name, perhaps with a prefix ("km", "kibyte", "cm^2"); NULL if unknown */
static const Unit *unit_resolve(const char *name, double *mul)
{
  char s[64];
  const Unit *u;
  size_t i, j, k = 0;
  for (i = 0; name[i] && k < 62; i++) {          /* "^2" and "^3" as "2" and "3" */
    if (name[i] == '^' && (name[i + 1] == '2' || name[i + 1] == '3')) continue;
    s[k++] = name[i];
  }
  s[k] = 0;
  if (name[i]) return NULL;
  *mul = 1;
  if ((u = unit_find(s))) return u;
  for (j = 0; j < sizeof bin_pre / sizeof bin_pre[0]; j++) {
    size_t l = strlen(bin_pre[j].p);
    if (!strncmp(s, bin_pre[j].p, l) && (u = unit_find(s + l)) && u->pre == 4) { *mul = bin_pre[j].f; return u; }
  }
  for (j = 0; j < sizeof si_pre / sizeof si_pre[0]; j++) {
    size_t l = strlen(si_pre[j].p);
    if (!strncmp(s, si_pre[j].p, l) && (u = unit_find(s + l)) && u->pre) {
      *mul = u->pre == 4 ? si_pre[j].f : pow(si_pre[j].f, u->pre);
      return u;
    }
  }
  return NULL;
}
static double to_kelvin(int kind, double x)
{
  switch (kind) {
  case 0: return x + 273.15;
  case 1: return (x - 32) * 5 / 9 + 273.15;
  case 3: return x * 5 / 9;
  case 4: return x * 1.25 + 273.15;
  default: return x;
  }
}
static double from_kelvin(int kind, double k)
{
  switch (kind) {
  case 0: return k - 273.15;
  case 1: return (k - 273.15) * 9 / 5 + 32;
  case 3: return k * 9 / 5;
  case 4: return (k - 273.15) / 1.25;
  default: return k;
  }
}
static Value f_convert(Ctx *cx, Value *a, int n)
{
  double x, fm, tm;
  char *fs, *ts;
  const Unit *fu, *tu;
  int e;
  (void)n;
  if ((e = arg_num(cx, &a[0], &x))) return val_err(e);
  if ((e = arg_str(cx, &a[1], &fs))) return val_err(e);
  if ((e = arg_str(cx, &a[2], &ts))) { free(fs); return val_err(e); }
  fu = unit_resolve(fs, &fm); tu = unit_resolve(ts, &tm);
  free(fs); free(ts);
  if (!fu || !tu || fu->dim != tu->dim) return val_err(E_NA);
  if (fu->dim == D_TEMP) return num_or_err(from_kelvin((int)tu->f, to_kelvin((int)fu->f, x * fm)) / tm);
  return num_or_err(x * fu->f * fm / (tu->f * tm));
}

/* ------------------------------------------------------------------ ERF */
/* erf and erfc to full double precision: for |x| < 3 the series erf x = 2/sqrt(pi) e^-x^2
   sum 2^k x^(2k+1) / (1*3*...*(2k+1)) (all terms positive), beyond it erfc by its continued
   fraction */
static double erfc_cf(double x)      /* x >= 3 */
{
  /* erfc x = e^-x^2 / sqrt(pi) * 1/(x + (1/2)/(x + 1/(x + (3/2)/(x + 2/(x + ...))))), worked from the tail */
  double f = x;
  int k;
  for (k = 60; k >= 1; k--) f = x + (k / 2.0) / f;
  return exp(-x * x) / SQRTPI_ / f;
}
static double ledger_erf(double x)
{
  double ax = fabs(x), s, t;
  int k;
  if (ax >= 3) { s = 1 - erfc_cf(ax); return x < 0 ? -s : s; }
  s = t = ax;
  for (k = 1; k < 200; k++) {
    t *= 2 * ax * ax / (2 * k + 1);
    s += t;
    if (t < s * 1e-17) break;
  }
  s *= 2 / SQRTPI_ * exp(-ax * ax);
  return x < 0 ? -s : s;
}
static double ledger_erfc(double x)
{
  if (x >= 3) return erfc_cf(x);
  if (x <= -3) return 2 - erfc_cf(-x);
  return 1 - ledger_erf(x);
}
static Value f_erf(Ctx *cx, Value *a, int n)
{
  double lo, hi;
  int e;
  if ((e = arg_num(cx, &a[0], &lo))) return val_err(e);
  if (!HAS(1)) return num_or_err(ledger_erf(lo));
  if ((e = arg_num(cx, &a[1], &hi))) return val_err(e);
  return num_or_err(ledger_erf(hi) - ledger_erf(lo));
}
static Value f_erfprecise(Ctx *cx, Value *a, int n) { double x; int e = arg_num(cx, &a[0], &x); (void)n; return e ? val_err(e) : num_or_err(ledger_erf(x)); }
static Value f_erfc(Ctx *cx, Value *a, int n) { double x; int e = arg_num(cx, &a[0], &x); (void)n; return e ? val_err(e) : num_or_err(ledger_erfc(x)); }

/* ------------------------------------------------------------------ Bessel functions */
/* J_n by its series when x is small or n large, else by the integral (1/pi) int_0^pi
   cos(n t - x sin t) dt, whose trapezoid sums converge geometrically (the integrand is periodic) */
static double bessel_series(double x, int n, int alt)      /* sum (+-1)^k (x/2)^(2k+n) / (k! (k+n)!) */
{
  double t = 1, s, h = x / 2;
  int k;
  for (k = 1; k <= n; k++) t *= h / k;
  s = t;
  for (k = 1; k < 500; k++) {
    t *= h * h / (k * (double)(k + n));
    if (alt) t = -t;
    s += t;
    if (fabs(t) < 1e-17 * fabs(s)) break;
  }
  return s;
}
static double bessel_j(double x, int n)
{
  int m, k;
  double h, s = 0;
  if (fabs(x) <= 8 || n > fabs(x)) return bessel_series(x, n, 1);
  m = (int)(fabs(x) + n) * 2 + 64;
  h = M_PI / m;
  for (k = 0; k <= m; k++) {
    double t = k * h, f = cos(n * t - x * sin(t));
    s += (k == 0 || k == m) ? f / 2 : f;
  }
  return s * h / M_PI;
}
/* K0 and K1 as Excel works them out: the polynomial approximations of Abramowitz & Stegun 9.8.5-9.8.8
   (about eight significant digits; Excel's results, and LibreOffice's, are these), with I0 and I1
   from their series; K_n by the recurrence upwards (stable for K) */
static double bessel_k(double x, int n)
{
  double y, k0, k1, k2;
  int j;
  if (x <= 2) {
    y = x * x / 4;
    k0 = -log(x / 2) * bessel_series(x, 0, 0) + (-0.57721566 + y * (0.42278420 + y * (0.23069756 + y * (0.3488590e-1 + y * (0.262698e-2 + y * (0.10750e-3 + y * 0.74e-5))))));
    k1 = log(x / 2) * bessel_series(x, 1, 0) + (1 / x) * (1 + y * (0.15443144 + y * (-0.67278579 + y * (-0.18156897 + y * (-0.1919402e-1 + y * (-0.110404e-2 + y * (-0.4686e-4)))))));
  } else {
    y = 2 / x;
    k0 = exp(-x) / sqrt(x) * (1.25331414 + y * (-0.7832358e-1 + y * (0.2189568e-1 + y * (-0.1062446e-1 + y * (0.587872e-2 + y * (-0.251540e-2 + y * 0.53208e-3))))));
    k1 = exp(-x) / sqrt(x) * (1.25331414 + y * (0.23498619 + y * (-0.3655620e-1 + y * (0.1504268e-1 + y * (-0.780353e-2 + y * (0.325614e-2 + y * (-0.68245e-3)))))));
  }
  if (n == 0) return k0;
  for (j = 1; j < n; j++) { k2 = k0 + 2 * j / x * k1; k0 = k1; k1 = k2; }
  return k1;
}
/* Y_n: for x <= 17 its series (Abramowitz & Stegun 9.1.11), else Hankel's asymptotic expansion
   for Y0 and Y1 and the recurrence upwards (stable for Y) */
static double digamma_int(int m) { double s = -EULER_; int k; for (k = 1; k < m; k++) s += 1.0 / k; return s; }   /* psi(m), m >= 1 */
static double bessel_y_series(double x, int n)
{
  double h = x / 2, s1 = 0, s2 = 0, t, f;
  int k;
  /* -(1/pi) sum_{k<n} (n-k-1)!/k! (x/2)^(2k-n) */
  for (k = 0; k < n; k++) {
    int j;
    f = 1;
    for (j = 2; j <= n - k - 1; j++) f *= j;
    for (j = 2; j <= k; j++) f /= j;
    s1 += f * pow(h, 2 * k - n);
  }
  /* -(1/pi) sum_k (psi(k+1) + psi(n+k+1)) (-x^2/4)^k / (k! (n+k)!) (x/2)^n */
  t = 1;
  for (k = 1; k <= n; k++) t *= h / k;
  for (k = 0; k < 300; k++) {
    double term;
    if (k > 0) t *= -h * h / (k * (double)(k + n));
    term = (digamma_int(k + 1) + digamma_int(n + k + 1)) * t;
    s2 += term;
    if (k > 5 && fabs(term) < 1e-18 * (fabs(s2) + 1e-300)) break;
  }
  return -s1 / M_PI + 2 / M_PI * log(h) * bessel_series(x, n, 1) - s2 / M_PI;
}
static void hankel(double x, int nu, double *j, double *y)
{
  double mu = 4.0 * nu * nu, p = 0, q = 0, t = 1, last = 1e300, w;
  int k;
  for (k = 0; k < 100; k++) {
    double term;
    if (k > 0) t *= (mu - (2.0 * k - 1) * (2.0 * k - 1)) / (k * 8.0 * x);
    term = fabs(t);
    if (term > last) break;
    last = term;
    switch (k % 4) { case 0: p += t; break; case 1: q += t; break; case 2: p -= t; break; default: q -= t; }
    if (term < 1e-17) break;
  }
  w = x - nu * M_PI / 2 - M_PI / 4;
  t = sqrt(2 / (M_PI * x));
  *j = t * (p * cos(w) - q * sin(w));
  *y = t * (p * sin(w) + q * cos(w));
}
static double bessel_y(double x, int n)
{
  double jj, y0, y1, y2;
  int k;
  if (x <= 17) return bessel_y_series(x, n);
  hankel(x, 0, &jj, &y0);
  if (n == 0) return y0;
  hankel(x, 1, &jj, &y1);
  for (k = 1; k < n; k++) { y2 = 2 * k / x * y1 - y0; y0 = y1; y1 = y2; }
  return y1;
}
static int bessel_args(Ctx *cx, Value *a, double *x, int *nn)
{
  double o;
  int e;
  if ((e = arg_num(cx, &a[0], x)) || (e = arg_num(cx, &a[1], &o))) return e;
  o = o < 0 ? ceil(o) : floor(o);
  if (o < 0 || o > 1000) return E_NUM;
  *nn = (int)o;
  return 0;
}
static Value f_besseli(Ctx *cx, Value *a, int n) { double x; int k, e = bessel_args(cx, a, &x, &k); (void)n; return e ? val_err(e) : num_or_err(bessel_series(x, k, 0)); }
static Value f_besselj(Ctx *cx, Value *a, int n) { double x; int k, e = bessel_args(cx, a, &x, &k); (void)n; return e ? val_err(e) : num_or_err(bessel_j(x, k)); }
static Value f_besselk(Ctx *cx, Value *a, int n)
{
  double x; int k, e = bessel_args(cx, a, &x, &k); (void)n;
  if (e) return val_err(e);
  if (x <= 0) return val_err(E_NUM);
  return num_or_err(bessel_k(x, k));
}
static Value f_bessely(Ctx *cx, Value *a, int n)
{
  double x; int k, e = bessel_args(cx, a, &x, &k); (void)n;
  if (e) return val_err(e);
  if (x <= 0) return val_err(E_NUM);
  return num_or_err(bessel_y(x, k));
}

/* ------------------------------------------------------------------ complex numbers */
typedef struct { double re, im; } Cplx;
/* a real number at s (no sign); its length, 0 if none */
static int real_len(const char *s)
{
  int i = 0, dig = 0;
  while (isdigit((unsigned char)s[i])) { i++; dig = 1; }
  if (s[i] == '.') { i++; while (isdigit((unsigned char)s[i])) { i++; dig = 1; } }
  if (!dig) return 0;
  if (s[i] == 'e' || s[i] == 'E') {
    int j = i + 1;
    if (s[j] == '+' || s[j] == '-') j++;
    if (isdigit((unsigned char)s[j])) { while (isdigit((unsigned char)s[j])) j++; i = j; }
  }
  return i;
}
/* "3+4i", "-2.5j", "i", "7", "1e3-i": 0 and *suffix 'i', 'j' or 0 (none), or E_NUM */
static int cplx_parse(const char *s, Cplx *z, char *suffix)
{
  double part[2];
  int np = 0, i = 0;
  *suffix = 0; z->re = z->im = 0;
  if (!*s) return 0;
  while (s[i]) {
    double sg = 1, v = 1;
    int l;
    if (np == 2) return E_NUM;
    if (s[i] == '+' || s[i] == '-') { sg = s[i] == '-' ? -1 : 1; i++; }
    else if (np) return E_NUM;
    l = real_len(s + i);
    if (l) { char tmp[64]; if (l > 63) return E_NUM; memcpy(tmp, s + i, (size_t)l); tmp[l] = 0; v = atof(tmp); i += l; }
    if (s[i] == 'i' || s[i] == 'j') {
      if (*suffix || s[i + 1]) return E_NUM;
      *suffix = s[i]; i++;
      z->im = sg * v;
    } else {
      if (!l || np) return E_NUM;
      z->re = sg * v;
    }
    part[np++] = v;
  }
  (void)part;
  return 0;
}
/* a complex argument: numbers are real, logical values #VALUE!; *suffix as cplx_parse */
static int cplx_arg(Ctx *cx, const Value *v, Cplx *z, char *suffix)
{
  Value s = scalar(cx, v);
  int e = 0;
  *suffix = 0; z->re = z->im = 0;
  if (s.t == V_ERR) e = s.e;
  else if (s.t == V_BOOL) e = E_VALUE;
  else if (s.t == V_NUM) z->re = s.n;
  else if (s.t == V_STR) e = cplx_parse(s.s, z, suffix);
  val_free(&s);
  return e;
}
/* a part as Excel writes it: up to 15 significant digits, 1E-20 style exponents */
static void num15(double x, char out[40])
{
  char *e, tmp[48];
  if (x == floor(x) && fabs(x) < 1e15) sprintf(tmp, "%.0f", x);
  else sprintf(tmp, "%.15G", x);
  memcpy(out, tmp, 40); out[39] = 0;
  if ((e = strchr(out, 'E'))) {
    /* "1.5E-020" or "1.5E+20" -> "1.5E-20", "1.5E+20" */
    char sign = e[1], *d = e + 2;
    while (*d == '0' && d[1]) d++;
    memmove(e + 2, d, strlen(d) + 1);
    e[1] = sign;
  }
}
static Value cplx_val(Cplx z, char suffix)
{
  char re[40], im[40], out[100];
  if (!(z.re == z.re) || !(z.im == z.im) || fabs(z.re) > 1.7976931348623157e308 || fabs(z.im) > 1.7976931348623157e308) return val_err(E_NUM);
  if (!suffix) suffix = 'i';
  if (z.re == 0) z.re = 0;                    /* no "-0" */
  num15(z.re, re); num15(z.im, im);
  if (z.im == 0) return val_str(re);
  out[0] = 0;
  if (z.re != 0) strcat(out, re);
  if (z.im == 1) strcat(out, z.re != 0 ? "+" : "");
  else if (z.im == -1) strcat(out, "-");
  else { if (z.re != 0 && z.im > 0) strcat(out, "+"); strcat(out, im); }
  { size_t l = strlen(out); out[l] = suffix; out[l + 1] = 0; }
  return val_str(out);
}
static Cplx cx_mul(Cplx a, Cplx b) { Cplx r; r.re = a.re * b.re - a.im * b.im; r.im = a.re * b.im + a.im * b.re; return r; }
static int cx_div(Cplx a, Cplx b, Cplx *r)
{
  double d = b.re * b.re + b.im * b.im;
  if (d == 0) return E_NUM;
  r->re = (a.re * b.re + a.im * b.im) / d; r->im = (a.im * b.re - a.re * b.im) / d;
  return 0;
}
static Cplx cx_exp(Cplx a) { Cplx r; double m = exp(a.re); r.re = m * cos(a.im); r.im = m * sin(a.im); return r; }
static Cplx cx_sin(Cplx a) { Cplx r; r.re = sin(a.re) * cosh(a.im); r.im = cos(a.re) * sinh(a.im); return r; }
static Cplx cx_cos(Cplx a) { Cplx r; r.re = cos(a.re) * cosh(a.im); r.im = -sin(a.re) * sinh(a.im); return r; }
static Cplx cx_sinh(Cplx a) { Cplx r; r.re = sinh(a.re) * cos(a.im); r.im = cosh(a.re) * sin(a.im); return r; }
static Cplx cx_cosh(Cplx a) { Cplx r; r.re = cosh(a.re) * cos(a.im); r.im = sinh(a.re) * sin(a.im); return r; }
static double cx_abs(Cplx a) { return hypot(a.re, a.im); }
/* one-argument complex functions */
enum { CF_SQRT, CF_EXP, CF_LN, CF_LOG10, CF_LOG2, CF_SIN, CF_COS, CF_TAN, CF_COT, CF_SEC, CF_CSC, CF_SINH, CF_COSH,
       CF_SECH, CF_CSCH, CF_CONJ };
static Value cplx1(Ctx *cx, Value *a, int op)
{
  Cplx z, r, one;
  char sf;
  int e;
  if ((e = cplx_arg(cx, &a[0], &z, &sf))) return val_err(e);
  one.re = 1; one.im = 0;
  switch (op) {
  case CF_SQRT: { double m = sqrt(cx_abs(z)), t = atan2(z.im, z.re) / 2; r.re = m * cos(t); r.im = m * sin(t); break; }
  case CF_EXP: r = cx_exp(z); break;
  case CF_LN: case CF_LOG10: case CF_LOG2: {
    double d = op == CF_LN ? 1 : op == CF_LOG10 ? log(10.0) : log(2.0);
    if (z.re == 0 && z.im == 0) return val_err(E_NUM);
    r.re = log(cx_abs(z)) / d; r.im = atan2(z.im, z.re) / d; break; }
  case CF_SIN: r = cx_sin(z); break;
  case CF_COS: r = cx_cos(z); break;
  /* tan, cot and sec by their double-angle forms (Excel's results to the last digit) */
  case CF_TAN: { double d = cos(2 * z.re) + cosh(2 * z.im); if (d == 0) return val_err(E_NUM); r.re = sin(2 * z.re) / d; r.im = sinh(2 * z.im) / d; break; }
  case CF_COT: { double d = cosh(2 * z.im) - cos(2 * z.re); if (d == 0) return val_err(E_NUM); r.re = sin(2 * z.re) / d; r.im = -sinh(2 * z.im) / d; break; }
  case CF_SEC: { double d = cos(2 * z.re) + cosh(2 * z.im); if (d == 0) return val_err(E_NUM); r.re = 2 * cos(z.re) * cosh(z.im) / d; r.im = 2 * sin(z.re) * sinh(z.im) / d; break; }
  case CF_CSC: if (z.re == 0 && z.im == 0) return val_err(E_NUM); if ((e = cx_div(one, cx_sin(z), &r))) return val_err(e); break;
  case CF_SINH: r = cx_sinh(z); break;
  case CF_COSH: r = cx_cosh(z); break;
  case CF_SECH: if ((e = cx_div(one, cx_cosh(z), &r))) return val_err(e); break;
  case CF_CSCH: if (z.re == 0 && z.im == 0) return val_err(E_NUM); if ((e = cx_div(one, cx_sinh(z), &r))) return val_err(e); break;
  default: r.re = z.re; r.im = -z.im; break;
  }
  return cplx_val(r, sf);
}
static Value f_imsqrt(Ctx *cx, Value *a, int n) { (void)n; return cplx1(cx, a, CF_SQRT); }
static Value f_imexp(Ctx *cx, Value *a, int n) { (void)n; return cplx1(cx, a, CF_EXP); }
static Value f_imln(Ctx *cx, Value *a, int n) { (void)n; return cplx1(cx, a, CF_LN); }
static Value f_imlog10(Ctx *cx, Value *a, int n) { (void)n; return cplx1(cx, a, CF_LOG10); }
static Value f_imlog2(Ctx *cx, Value *a, int n) { (void)n; return cplx1(cx, a, CF_LOG2); }
static Value f_imsin(Ctx *cx, Value *a, int n) { (void)n; return cplx1(cx, a, CF_SIN); }
static Value f_imcos(Ctx *cx, Value *a, int n) { (void)n; return cplx1(cx, a, CF_COS); }
static Value f_imtan(Ctx *cx, Value *a, int n) { (void)n; return cplx1(cx, a, CF_TAN); }
static Value f_imcot(Ctx *cx, Value *a, int n) { (void)n; return cplx1(cx, a, CF_COT); }
static Value f_imsec(Ctx *cx, Value *a, int n) { (void)n; return cplx1(cx, a, CF_SEC); }
static Value f_imcsc(Ctx *cx, Value *a, int n) { (void)n; return cplx1(cx, a, CF_CSC); }
static Value f_imsinh(Ctx *cx, Value *a, int n) { (void)n; return cplx1(cx, a, CF_SINH); }
static Value f_imcosh(Ctx *cx, Value *a, int n) { (void)n; return cplx1(cx, a, CF_COSH); }
static Value f_imsech(Ctx *cx, Value *a, int n) { (void)n; return cplx1(cx, a, CF_SECH); }
static Value f_imcsch(Ctx *cx, Value *a, int n) { (void)n; return cplx1(cx, a, CF_CSCH); }
static Value f_imconjugate(Ctx *cx, Value *a, int n) { (void)n; return cplx1(cx, a, CF_CONJ); }
/* the real-valued ones */
static Value cplx_part(Ctx *cx, Value *a, int op)
{
  Cplx z;
  char sf;
  int e;
  if ((e = cplx_arg(cx, &a[0], &z, &sf))) return val_err(e);
  switch (op) {
  case 0: return num_or_err(cx_abs(z));
  case 1: return val_num(z.im);
  case 2: return val_num(z.re);
  default: if (z.re == 0 && z.im == 0) return val_err(E_DIV0); return val_num(atan2(z.im, z.re));
  }
}
static Value f_imabs(Ctx *cx, Value *a, int n) { (void)n; return cplx_part(cx, a, 0); }
static Value f_imaginary(Ctx *cx, Value *a, int n) { (void)n; return cplx_part(cx, a, 1); }
static Value f_imreal(Ctx *cx, Value *a, int n) { (void)n; return cplx_part(cx, a, 2); }
static Value f_imargument(Ctx *cx, Value *a, int n) { (void)n; return cplx_part(cx, a, 3); }
/* suffixes must agree: the first one seen is kept, a different one is #VALUE! */
static int suffix_merge(char *have, char s) { if (!s) return 0; if (*have && *have != s) return E_VALUE; *have = s; return 0; }
static Value f_complex(Ctx *cx, Value *a, int n)
{
  double re, im;
  char sf = 'i';
  int e;
  Cplx z;
  if ((e = arg_num(cx, &a[0], &re)) || (e = arg_num(cx, &a[1], &im))) return val_err(e);
  if (HAS(2)) {
    char *s;
    if ((e = arg_str(cx, &a[2], &s))) return val_err(e);
    if (!strcmp(s, "j")) sf = 'j';
    else if (*s && strcmp(s, "i")) { free(s); return val_err(E_VALUE); }
    free(s);
  }
  z.re = re; z.im = im;
  return cplx_val(z, sf);
}
static Value f_imdiv(Ctx *cx, Value *a, int n)
{
  Cplx x, y, r;
  char s1, s2, sf = 0;
  int e;
  (void)n;
  if ((e = cplx_arg(cx, &a[0], &x, &s1)) || (e = cplx_arg(cx, &a[1], &y, &s2))) return val_err(e);
  if ((e = suffix_merge(&sf, s1)) || (e = suffix_merge(&sf, s2))) return val_err(e);
  if ((e = cx_div(x, y, &r))) return val_err(e);
  return cplx_val(r, sf);
}
static Value f_imsub(Ctx *cx, Value *a, int n)
{
  Cplx x, y;
  char s1, s2, sf = 0;
  int e;
  (void)n;
  if ((e = cplx_arg(cx, &a[0], &x, &s1)) || (e = cplx_arg(cx, &a[1], &y, &s2))) return val_err(e);
  if ((e = suffix_merge(&sf, s1)) || (e = suffix_merge(&sf, s2))) return val_err(e);
  x.re -= y.re; x.im -= y.im;
  return cplx_val(x, sf);
}
static Value f_impower(Ctx *cx, Value *a, int n)
{
  Cplx z, r;
  char sf;
  double p, m, t;
  int e;
  (void)n;
  if ((e = cplx_arg(cx, &a[0], &z, &sf)) || (e = arg_num(cx, &a[1], &p))) return val_err(e);
  if (z.re == 0 && z.im == 0) { if (p <= 0) return val_err(E_NUM); r.re = r.im = 0; return cplx_val(r, sf); }
  m = pow(cx_abs(z), p); t = atan2(z.im, z.re) * p;
  r.re = m * cos(t); r.im = m * sin(t);
  return cplx_val(r, sf);
}
/* IMSUM and IMPRODUCT walk every value of their arguments */
typedef struct { Cplx acc; char sf; int err, prod; } CAcc;
static int cacc_cb(Ctx *cx, const Value *v, int direct, void *arg)
{
  CAcc *c = (CAcc *)arg;
  Cplx z;
  char s;
  (void)direct;
  if (v->t == V_EMPTY) return 0;
  if ((c->err = cplx_arg(cx, v, &z, &s)) || (c->err = suffix_merge(&c->sf, s))) return 1;
  if (c->prod) c->acc = cx_mul(c->acc, z);
  else { c->acc.re += z.re; c->acc.im += z.im; }
  return 0;
}
static Value cacc(Ctx *cx, Value *a, int n, int prod)
{
  CAcc c;
  int i;
  c.acc.re = prod ? 1 : 0; c.acc.im = 0; c.sf = 0; c.err = 0; c.prod = prod;
  for (i = 0; i < n && !c.err; i++) {
    if (a[i].t == V_ERR) return val_err(a[i].e);
    each_value(cx, &a[i], cacc_cb, &c);
  }
  if (c.err) return val_err(c.err);
  return cplx_val(c.acc, c.sf);
}
static Value f_imsum(Ctx *cx, Value *a, int n) { return cacc(cx, a, n, 0); }
static Value f_improduct(Ctx *cx, Value *a, int n) { return cacc(cx, a, n, 1); }

/* ------------------------------------------------------------------ trigonometry */
static double d_asinh(double x)
{
  double ax = fabs(x), r;
  if (ax < 1e-4) r = ax * (1 - ax * ax / 6);
  else r = log(ax + sqrt(ax * ax + 1));
  return x < 0 ? -r : r;
}
static double d_atanh(double x)
{
  double ax = fabs(x), r;
  if (ax < 1e-4) r = ax * (1 + ax * ax / 3);
  else r = 0.5 * log((1 + ax) / (1 - ax));
  return x < 0 ? -r : r;
}
typedef double (*D1)(double);
/* dom: 0 any, 1 x >= 1, 2 |x| < 1, 3 |x| > 1, 4 |x| < 2^27, 5 the same and x != 0 (else #DIV/0!), 6 x != 0 */
static Value m1(Ctx *cx, Value *a, int dom, int op)
{
  double x, r;
  int e;
  if ((e = arg_num(cx, &a[0], &x))) return val_err(e);
  if ((dom == 1 && x < 1) || (dom == 2 && fabs(x) >= 1) || (dom == 3 && fabs(x) <= 1)) return val_err(E_NUM);
  if ((dom == 4 || dom == 5) && fabs(x) >= 134217728.0) return val_err(E_NUM);
  if ((dom == 5 || dom == 6) && x == 0) return val_err(E_DIV0);
  switch (op) {
  case 0: r = log(x + sqrt(x * x - 1)); break;                  /* ACOSH */
  case 1: r = d_asinh(x); break;
  case 2: r = d_atanh(x); break;
  case 3: r = M_PI / 2 - atan(x); break;                         /* ACOT */
  case 4: r = 0.5 * log((x + 1) / (x - 1)); break;               /* ACOTH */
  case 5: r = 1 / tan(x); break;                                 /* COT */
  case 6: r = 1 / tanh(x); break;                                /* COTH */
  case 7: r = 1 / sin(x); break;                                 /* CSC */
  case 8: r = 1 / sinh(x); break;                                /* CSCH */
  case 9: r = 1 / cos(x); break;                                 /* SEC */
  case 10: r = fabs(x) > 710 ? 0 : 1 / cosh(x); break;           /* SECH */
  default: if (x < 0) return val_err(E_NUM); r = sqrt(x * M_PI); break;   /* SQRTPI */
  }
  return num_or_err(r);
}
static Value f_acosh(Ctx *cx, Value *a, int n) { (void)n; return m1(cx, a, 1, 0); }
static Value f_asinh(Ctx *cx, Value *a, int n) { (void)n; return m1(cx, a, 0, 1); }
static Value f_atanh(Ctx *cx, Value *a, int n) { (void)n; return m1(cx, a, 2, 2); }
static Value f_acot(Ctx *cx, Value *a, int n) { (void)n; return m1(cx, a, 0, 3); }
static Value f_acoth(Ctx *cx, Value *a, int n) { (void)n; return m1(cx, a, 3, 4); }
static Value f_cot(Ctx *cx, Value *a, int n) { (void)n; return m1(cx, a, 5, 5); }
static Value f_coth(Ctx *cx, Value *a, int n) { (void)n; return m1(cx, a, 6, 6); }
static Value f_csc(Ctx *cx, Value *a, int n) { (void)n; return m1(cx, a, 5, 7); }
static Value f_csch(Ctx *cx, Value *a, int n) { (void)n; return m1(cx, a, 6, 8); }
static Value f_sec(Ctx *cx, Value *a, int n) { (void)n; return m1(cx, a, 4, 9); }
static Value f_sech(Ctx *cx, Value *a, int n) { (void)n; return m1(cx, a, 0, 10); }
static Value f_sqrtpi(Ctx *cx, Value *a, int n) { (void)n; return m1(cx, a, 0, 11); }

/* ------------------------------------------------------------------ Roman numerals */
static Value f_roman(Ctx *cx, Value *a, int n)
{
  /* the classic form, then the four "concise" forms of Excel: for a digit of 4 or 9 the
     subtraction may reach further left (499 is CDXCIX, LDVLIV, XDIX, VDIV, ID) */
  static const char ch[] = "MDCLXVI";
  static const int val[] = { 1000, 500, 100, 50, 10, 5, 1 };
  double x, f = 0;
  int e, v, mode, i;
  char out[64];
  size_t k = 0;
  if ((e = arg_num(cx, &a[0], &x))) return val_err(e);
  if (HAS(1)) {
    Value s = scalar(cx, &a[1]);
    if (s.t == V_BOOL) f = s.e ? 0 : 4;
    else { e = to_num(&s, &f); if (e) { val_free(&s); return val_err(e); } }
    val_free(&s);
  }
  x = floor(x); f = floor(f);
  if (x < 0 || x > 3999 || f < 0 || f > 4) return val_err(E_VALUE);
  v = (int)x; mode = (int)f;
  for (i = 0; i <= 6; i += 2) {
    int digit = v / val[i];
    if (digit % 5 == 4) {
      int idx = i, idx2 = digit == 4 ? i - 1 : i - 2, steps = 0;
      while (steps < mode && idx < 6) {
        steps++;
        if (val[idx2] - val[idx + 1] <= v) idx++;
        else steps = mode;
      }
      out[k++] = ch[idx]; out[k++] = ch[idx2];
      v += val[idx]; v -= val[idx2];
    } else {
      int pad = digit % 5;
      if (digit > 4) out[k++] = ch[i - 1];
      while (pad-- > 0) out[k++] = ch[i];
      v %= val[i];
    }
  }
  out[k] = 0;
  return val_str(out);
}
static Value f_arabic(Ctx *cx, Value *a, int n)
{
  char *s, *p;
  int e, neg = 0;
  double total = 0;
  (void)n;
  if ((e = arg_str(cx, &a[0], &s))) return val_err(e);
  p = s;
  while (*p == ' ') p++;
  { size_t l = strlen(p); while (l && p[l - 1] == ' ') p[--l] = 0; if (l > 255) { free(s); return val_err(E_VALUE); } }
  if (*p == '-') { neg = 1; p++; }
  {
    int prev = 0, len = (int)strlen(p), i;
    for (i = len - 1; i >= 0; i--) {
      int d;
      switch (toupper((unsigned char)p[i])) {
      case 'I': d = 1; break; case 'V': d = 5; break; case 'X': d = 10; break; case 'L': d = 50; break;
      case 'C': d = 100; break; case 'D': d = 500; break; case 'M': d = 1000; break;
      default: free(s); return val_err(E_VALUE);
      }
      if (d < prev) total -= d; else { total += d; prev = d; }
    }
  }
  free(s);
  return val_num(neg ? -total : total);
}

/* ------------------------------------------------------------------ combinatorics and series */
static Value f_combina(Ctx *cx, Value *a, int n)
{
  double x, k, r = 1, i;
  int e;
  (void)n;
  if ((e = arg_num(cx, &a[0], &x)) || (e = arg_num(cx, &a[1], &k))) return val_err(e);
  x = floor(x); k = floor(k);
  if (x < 0 || k < 0 || (x == 0 && k > 0)) return val_err(x == 0 && k > 0 ? E_NUM : E_NUM);
  /* C(x + k - 1, k) */
  x = x + k - 1;
  if (k > x - k) k = x - k;
  for (i = 1; i <= k; i++) r = r * (x - k + i) / i;
  return num_or_err(floor(r + 0.5));
}
static Value f_permutationa(Ctx *cx, Value *a, int n)
{
  double x, k;
  int e;
  (void)n;
  if ((e = arg_num(cx, &a[0], &x)) || (e = arg_num(cx, &a[1], &k))) return val_err(e);
  x = floor(x); k = floor(k);
  if (x < 0 || k < 0) return val_err(E_NUM);
  return num_or_err(pow(x, k));
}
static Value f_factdouble(Ctx *cx, Value *a, int n)
{
  double x, r = 1;
  int e;
  (void)n;
  if ((e = arg_num(cx, &a[0], &x))) return val_err(e);
  x = floor(x);
  if (x < 0) return val_err(E_NUM);
  for (; x > 1; x -= 2) r *= x;
  return num_or_err(r);
}
static Value f_multinomial(Ctx *cx, Value *a, int n)
{
  Nums ns;
  double sum = 0, r = 1;
  long i;
  if (gather(cx, a, n, &ns, 0)) { free(ns.v); return val_err(ns.err); }
  for (i = 0; i < ns.n; i++) {
    double x = floor(ns.v[i]), k;
    if (x < 0) { free(ns.v); return val_err(E_NUM); }
    for (k = 1; k <= x; k++) r = r * (sum + k) / k;    /* r *= C(sum + x, x) */
    sum += x;
  }
  free(ns.v);
  return num_or_err(floor(r + 0.5));
}
static Value f_seriessum(Ctx *cx, Value *a, int n)
{
  double x, p, m, s = 0;
  Nums ns;
  int e;
  long i;
  (void)n;
  if ((e = arg_num(cx, &a[0], &x)) || (e = arg_num(cx, &a[1], &p)) || (e = arg_num(cx, &a[2], &m))) return val_err(e);
  if (gather(cx, &a[3], 1, &ns, 0)) { free(ns.v); return val_err(ns.err); }
  for (i = 0; i < ns.n; i++) s += ns.v[i] * pow(x, p + i * m);
  free(ns.v);
  return num_or_err(s);
}
/* SUMX2MY2, SUMX2PY2, SUMXMY2: the pairs of two ranges that are both numbers */
static Value sumxy(Ctx *cx, Value *a, int kind)
{
  i32 r1, c1, r2, c2, r, c;
  double s = 0;
  if (a[0].t == V_ERR) return val_err(a[0].e);
  if (a[1].t == V_ERR) return val_err(a[1].e);
  arg_shape(&a[0], &r1, &c1); arg_shape(&a[1], &r2, &c2);
  if ((long)r1 * c1 != (long)r2 * c2) return val_err(E_NA);
  for (r = 0; r < r1; r++) for (c = 0; c < c1; c++) {
    long k = (long)r * c1 + c;
    const Value *x = arg_at(cx, &a[0], r, c), *y;
    double xv, yv;
    if (x->t == V_ERR) return val_err(x->e);
    if (x->t != V_NUM) { y = arg_at(cx, &a[1], (i32)(k / c2), (i32)(k % c2)); if (y->t == V_ERR) return val_err(y->e); continue; }
    xv = x->n;
    y = arg_at(cx, &a[1], (i32)(k / c2), (i32)(k % c2));
    if (y->t == V_ERR) return val_err(y->e);
    if (y->t != V_NUM) continue;
    yv = y->n;
    s += kind == 0 ? xv * xv - yv * yv : kind == 1 ? xv * xv + yv * yv : (xv - yv) * (xv - yv);
  }
  return num_or_err(s);
}
static Value f_sumx2my2(Ctx *cx, Value *a, int n) { (void)n; return sumxy(cx, a, 0); }
static Value f_sumx2py2(Ctx *cx, Value *a, int n) { (void)n; return sumxy(cx, a, 1); }
static Value f_sumxmy2(Ctx *cx, Value *a, int n) { (void)n; return sumxy(cx, a, 2); }

/* ------------------------------------------------------------------ rounding */
/* FLOOR.PRECISE, CEILING.PRECISE, ISO.CEILING: the sign of significance doesn't matter */
static Value precise(Ctx *cx, Value *a, int n, int up)
{
  double x, s = 1, q;
  int e;
  if ((e = arg_num(cx, &a[0], &x))) return val_err(e);
  if (HAS(1) && (e = arg_num(cx, &a[1], &s))) return val_err(e);
  s = fabs(s);
  if (s == 0 || x == 0) return val_num(0);
  q = x / s;
  q = up ? ceil(q - 1e-12) : floor(q + 1e-12);
  return num_or_err(q * s + 0.0);
}
static Value f_floorprecise(Ctx *cx, Value *a, int n) { return precise(cx, a, n, 0); }
static Value f_ceilingprecise(Ctx *cx, Value *a, int n) { return precise(cx, a, n, 1); }

/* ------------------------------------------------------------------ matrices */
/* a numeric matrix from an argument: every element a number, else #VALUE! */
static double *matrix(Ctx *cx, const Value *v, i32 *rows, i32 *cols, int *err)
{
  i32 r, c;
  double *m;
  *err = 0;
  if (v->t == V_ERR) { *err = v->e; return NULL; }
  arg_shape(v, rows, cols);
  if ((long)*rows * *cols > 1000000L) { *err = E_NUM; return NULL; }
  if (!(m = (double *)malloc(sizeof(double) * (size_t)(*rows) * (size_t)(*cols)))) { *err = E_NUM; return NULL; }
  for (r = 0; r < *rows; r++) for (c = 0; c < *cols; c++) {
    const Value *x = arg_at(cx, v, r, c);
    if (x->t == V_ERR) { *err = x->e; free(m); return NULL; }
    if (x->t != V_NUM) { *err = E_VALUE; free(m); return NULL; }
    m[r * *cols + c] = x->n;
  }
  return m;
}
static Value arr_value(Arr *ar) { Value v = val_empty(); v.t = V_ARR; v.a = ar; return v; }
/* LU decomposition with partial pivoting in place; the determinant (0 when singular) */
static double lu(double *m, int nn, int *perm)
{
  double det = 1;
  int i, j, k;
  for (i = 0; i < nn; i++) perm[i] = i;
  for (k = 0; k < nn; k++) {
    int p = k;
    double big = fabs(m[k * nn + k]);
    for (i = k + 1; i < nn; i++) if (fabs(m[i * nn + k]) > big) { big = fabs(m[i * nn + k]); p = i; }
    if (big == 0) return 0;
    if (p != k) {
      for (j = 0; j < nn; j++) { double t = m[k * nn + j]; m[k * nn + j] = m[p * nn + j]; m[p * nn + j] = t; }
      i = perm[k]; perm[k] = perm[p]; perm[p] = i;
      det = -det;
    }
    det *= m[k * nn + k];
    for (i = k + 1; i < nn; i++) {
      double f = m[i * nn + k] /= m[k * nn + k];
      for (j = k + 1; j < nn; j++) m[i * nn + j] -= f * m[k * nn + j];
    }
  }
  return det;
}
static Value f_mdeterm(Ctx *cx, Value *a, int n)
{
  i32 r, c;
  int e, *perm;
  double *m = matrix(cx, &a[0], &r, &c, &e), d;
  (void)n;
  if (!m) return val_err(e);
  if (r != c) { free(m); return val_err(E_VALUE); }
  if (!(perm = (int *)malloc(sizeof(int) * (size_t)r))) { free(m); return val_err(E_NUM); }
  d = lu(m, r, perm);
  free(m); free(perm);
  return num_or_err(d);
}
static Value f_minverse(Ctx *cx, Value *a, int n)
{
  i32 r, c, i, j, k;
  int e, *perm;
  double *m = matrix(cx, &a[0], &r, &c, &e), d;
  Arr *out;
  (void)n;
  if (!m) return val_err(e);
  if (r != c) { free(m); return val_err(E_VALUE); }
  if (!(perm = (int *)malloc(sizeof(int) * (size_t)r))) { free(m); return val_err(E_NUM); }
  d = lu(m, r, perm);
  if (d == 0 || !(out = arr_new(r, r))) { free(m); free(perm); return val_err(E_NUM); }
  /* solve L U x = P e_j column by column */
  for (j = 0; j < r; j++) {
    double *x = (double *)malloc(sizeof(double) * (size_t)r);
    if (!x) { free(m); free(perm); arr_free(out); return val_err(E_NUM); }
    for (i = 0; i < r; i++) {
      double s = perm[i] == j ? 1 : 0;
      for (k = 0; k < i; k++) s -= m[i * r + k] * x[k];
      x[i] = s;
    }
    for (i = r - 1; i >= 0; i--) {
      double s = x[i];
      for (k = i + 1; k < r; k++) s -= m[i * r + k] * x[k];
      x[i] = s / m[i * r + i];
    }
    for (i = 0; i < r; i++) out->v[i * r + j] = val_num(x[i]);
    free(x);
  }
  free(m); free(perm);
  return arr_value(out);
}
static Value f_mmult(Ctx *cx, Value *a, int n)
{
  i32 r1, c1, r2, c2, i, j, k;
  int e;
  double *x, *y;
  Arr *out;
  (void)n;
  if (!(x = matrix(cx, &a[0], &r1, &c1, &e))) return val_err(e);
  if (!(y = matrix(cx, &a[1], &r2, &c2, &e))) { free(x); return val_err(e); }
  if (c1 != r2 || !(out = arr_new(r1, c2))) { free(x); free(y); return val_err(c1 != r2 ? E_VALUE : E_NUM); }
  for (i = 0; i < r1; i++) for (j = 0; j < c2; j++) {
    double s = 0;
    for (k = 0; k < c1; k++) s += x[i * c1 + k] * y[k * c2 + j];
    out->v[i * c2 + j] = val_num(s);
  }
  free(x); free(y);
  return arr_value(out);
}
static Value f_munit(Ctx *cx, Value *a, int n)
{
  double d;
  int e;
  i32 i, j, k;
  Arr *out;
  (void)n;
  if ((e = arg_num(cx, &a[0], &d))) return val_err(e);
  d = floor(d);
  if (d < 1 || d > 1000) return val_err(E_VALUE);
  k = (i32)d;
  if (!(out = arr_new(k, k))) return val_err(E_NUM);
  for (i = 0; i < k; i++) for (j = 0; j < k; j++) out->v[i * k + j] = val_num(i == j);
  return arr_value(out);
}
/* PERCENTOF(subset, all): SUM(subset) / SUM(all) */
static Value f_percentof(Ctx *cx, Value *a, int n)
{
  Nums ns;
  double s1 = 0, s2 = 0;
  long i;
  (void)n;
  if (gather(cx, &a[0], 1, &ns, 0)) { free(ns.v); return val_err(ns.err); }
  for (i = 0; i < ns.n; i++) s1 += ns.v[i];
  free(ns.v);
  if (gather(cx, &a[1], 1, &ns, 0)) { free(ns.v); return val_err(ns.err); }
  for (i = 0; i < ns.n; i++) s2 += ns.v[i];
  free(ns.v);
  if (s2 == 0) return val_err(E_DIV0);
  return num_or_err(s1 / s2);
}

/* ------------------------------------------------------------------ the table (name order) */
#define X FF_XLFN
const FuncDef func_eng_defs[] = {
  { "ACOSH", 1, 1, 0, f_acosh }, { "ACOT", 1, 1, X, f_acot }, { "ACOTH", 1, 1, X, f_acoth },
  { "ARABIC", 1, 1, X, f_arabic }, { "ASINH", 1, 1, 0, f_asinh }, { "ATANH", 1, 1, 0, f_atanh },
  { "BASE", 2, 3, X, f_base }, { "BESSELI", 2, 2, 0, f_besseli }, { "BESSELJ", 2, 2, 0, f_besselj },
  { "BESSELK", 2, 2, 0, f_besselk }, { "BESSELY", 2, 2, 0, f_bessely }, { "BIN2DEC", 1, 1, 0, f_bin2dec },
  { "BIN2HEX", 1, 2, 0, f_bin2hex }, { "BIN2OCT", 1, 2, 0, f_bin2oct }, { "BITAND", 2, 2, X, f_bitand },
  { "BITLSHIFT", 2, 2, X, f_bitlshift }, { "BITOR", 2, 2, X, f_bitor }, { "BITRSHIFT", 2, 2, X, f_bitrshift },
  { "BITXOR", 2, 2, X, f_bitxor }, { "CEILING.PRECISE", 1, 2, X, f_ceilingprecise }, { "COMBINA", 2, 2, X, f_combina },
  { "COMPLEX", 2, 3, 0, f_complex }, { "CONVERT", 3, 3, 0, f_convert }, { "COT", 1, 1, X, f_cot },
  { "COTH", 1, 1, X, f_coth }, { "CSC", 1, 1, X, f_csc }, { "CSCH", 1, 1, X, f_csch },
  { "DEC2BIN", 1, 2, 0, f_dec2bin }, { "DEC2HEX", 1, 2, 0, f_dec2hex }, { "DEC2OCT", 1, 2, 0, f_dec2oct },
  { "DECIMAL", 2, 2, X, f_decimal }, { "DELTA", 1, 2, 0, f_delta }, { "ERF", 1, 2, 0, f_erf },
  { "ERF.PRECISE", 1, 1, X, f_erfprecise }, { "ERFC", 1, 1, 0, f_erfc }, { "ERFC.PRECISE", 1, 1, X, f_erfc },
  { "FACTDOUBLE", 1, 1, 0, f_factdouble }, { "FLOOR.PRECISE", 1, 2, X, f_floorprecise }, { "GESTEP", 1, 2, 0, f_gestep },
  { "HEX2BIN", 1, 2, 0, f_hex2bin }, { "HEX2DEC", 1, 1, 0, f_hex2dec }, { "HEX2OCT", 1, 2, 0, f_hex2oct },
  { "IMABS", 1, 1, 0, f_imabs }, { "IMAGINARY", 1, 1, 0, f_imaginary }, { "IMARGUMENT", 1, 1, 0, f_imargument },
  { "IMCONJUGATE", 1, 1, 0, f_imconjugate }, { "IMCOS", 1, 1, 0, f_imcos }, { "IMCOSH", 1, 1, X, f_imcosh },
  { "IMCOT", 1, 1, X, f_imcot }, { "IMCSC", 1, 1, X, f_imcsc }, { "IMCSCH", 1, 1, X, f_imcsch },
  { "IMDIV", 2, 2, 0, f_imdiv }, { "IMEXP", 1, 1, 0, f_imexp }, { "IMLN", 1, 1, 0, f_imln },
  { "IMLOG10", 1, 1, 0, f_imlog10 }, { "IMLOG2", 1, 1, 0, f_imlog2 }, { "IMPOWER", 2, 2, 0, f_impower },
  { "IMPRODUCT", 1, 255, 0, f_improduct }, { "IMREAL", 1, 1, 0, f_imreal }, { "IMSEC", 1, 1, X, f_imsec },
  { "IMSECH", 1, 1, X, f_imsech }, { "IMSIN", 1, 1, 0, f_imsin }, { "IMSINH", 1, 1, X, f_imsinh },
  { "IMSQRT", 1, 1, 0, f_imsqrt }, { "IMSUB", 2, 2, 0, f_imsub }, { "IMSUM", 1, 255, 0, f_imsum },
  { "IMTAN", 1, 1, X, f_imtan }, { "ISO.CEILING", 1, 2, 0, f_ceilingprecise }, { "MDETERM", 1, 1, 0, f_mdeterm },
  { "MINVERSE", 1, 1, 0, f_minverse }, { "MMULT", 2, 2, 0, f_mmult }, { "MULTINOMIAL", 1, 255, 0, f_multinomial },
  { "MUNIT", 1, 1, X, f_munit }, { "OCT2BIN", 1, 2, 0, f_oct2bin }, { "OCT2DEC", 1, 1, 0, f_oct2dec },
  { "OCT2HEX", 1, 2, 0, f_oct2hex }, { "PERCENTOF", 2, 2, X, f_percentof }, { "PERMUTATIONA", 2, 2, X, f_permutationa },
  { "ROMAN", 1, 2, 0, f_roman }, { "SEC", 1, 1, X, f_sec }, { "SECH", 1, 1, X, f_sech },
  { "SERIESSUM", 4, 4, 0, f_seriessum }, { "SQRTPI", 1, 1, 0, f_sqrtpi }, { "SUMX2MY2", 2, 2, 0, f_sumx2my2 },
  { "SUMX2PY2", 2, 2, 0, f_sumx2py2 }, { "SUMXMY2", 2, 2, 0, f_sumxmy2 },
};
#undef X
const int func_eng_count = (int)(sizeof func_eng_defs / sizeof func_eng_defs[0]);
