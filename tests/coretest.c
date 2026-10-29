/*
 * coretest.c - native tests of Ledger's portable core (values, dates, formulas, functions, number
 * formats, editing and undo, files). Built with the address and undefined-behaviour sanitizers:
 *   cc -std=c99 ... (every core source) tests/coretest.c -lm
 */
#include "../core/book.h"
#include "../core/calc.h"
#include "../core/xlsx.h"
#include "../core/ops.h"
#include "../core/template.h"
#include "../core/cond.h"
#include "../core/table.h"
#include "../core/pivot.h"
#include "../core/data.h"
#include "../core/xls.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>

static int passed, failed;
static void ok(const char *what, int cond)
{
  if (cond) passed++;
  else { failed++; printf("FAIL %s\n", what); }
}
double ledger_now(void) { return 46322.5; }   /* 27 Oct 2026, noon */
extern int ledger_date_order;

/* evaluate a formula as text in a book (shortcut) */
static double evn(Book *b, const char *f)
{
  Value v = book_eval_text(b, 0, f);
  double d = v.t == V_NUM ? v.n : v.t == V_BOOL ? v.e : -999999;
  if (v.t == V_ERR) d = -1000 - v.e;
  val_free(&v);
  return d;
}
static int evs(Book *b, const char *f, const char *want)
{
  Value v = book_eval_text(b, 0, f);
  Buf o;
  int r;
  buf_init(&o);
  to_text(b, &v, &o);
  r = !strcmp(o.data ? o.data : "", want);
  if (!r) printf("   %s -> \"%s\" (want \"%s\")\n", f, o.data ? o.data : "", want);
  buf_free(&o);
  val_free(&v);
  return r;
}
static int near(double a, double b) { return fabs(a - b) < 1e-9 * (fabs(b) > 1 ? fabs(b) : 1); }
/* a cell's shown text through its number format */
static int shows(Book *b, int sheet, i32 r, i32 c, const char *want)
{
  const Value *v = cell_value(b, sheet, r, c);
  char out[128];
  u32 col;
  int st = sheet_cell_style(b->sh[sheet], r, c);
  fmt_value(b, book_fmt_code(b, b->styles[st].numfmt), v, out, sizeof out, &col, 11);
  if (strcmp(out, want)) printf("   cell shows \"%s\" (want \"%s\")\n", out, want);
  return !strcmp(out, want);
}
#include "layouttest.c"
static int fmt(const char *code, double n, const char *want)
{
  Book *b = book_new();
  Value v = val_num(n);
  char out[128];
  u32 col;
  fmt_value(b, code, &v, out, sizeof out, &col, 11);
  book_free(b);
  if (strcmp(out, want)) printf("   %g in \"%s\" -> \"%s\" (want \"%s\")\n", n, code, out, want);
  return !strcmp(out, want);
}
static int ftext(Book *b, const char *in, const char *want)
{
  int pos;
  Formula *f = formula_parse(b, 0, in, &pos);
  Buf o;
  int r;
  if (!f) { printf("   parse failed: %s at %d\n", in, pos); return 0; }
  buf_init(&o);
  formula_text(b, 0, f, &o);
  r = !strcmp(o.data ? o.data : "", want);
  if (!r) printf("   %s printed as %s (want %s)\n", in, o.data ? o.data : "", want);
  buf_free(&o);
  formula_free(f);
  return r;
}
static void put(Book *b, const char *ref, const char *text)
{
  u16 sh; Rect r; u8 fl;
  int pos;
  parse_ref(b, ref, &sh, &r, &fl);
  book_input(b, sh == SHEET_SELF ? 0 : sh, r.r1, r.c1, text, &pos);
}
static double num_at(Book *b, const char *ref)
{
  u16 sh; Rect r; u8 fl;
  const Value *v;
  parse_ref(b, ref, &sh, &r, &fl);
  v = cell_value(b, sh == SHEET_SELF ? 0 : sh, r.r1, r.c1);
  return v->t == V_NUM ? v->n : v->t == V_ERR ? -1000 - v->e : -999999;
}
static int text_at(Book *b, const char *ref, const char *want)
{
  u16 sh; Rect r; u8 fl;
  Cell *c;
  Buf o;
  int ok_;
  parse_ref(b, ref, &sh, &r, &fl);
  c = sheet_cell(b->sh[sh == SHEET_SELF ? 0 : sh], r.r1, r.c1);
  buf_init(&o);
  if (c && c->f) { buf_addc(&o, '='); formula_text(b, sh == SHEET_SELF ? 0 : sh, c->f, &o); }
  else if (c) to_text(b, &c->v, &o);
  ok_ = !strcmp(o.data ? o.data : "", want);
  if (!ok_) printf("   %s holds \"%s\" (want \"%s\")\n", ref, o.data ? o.data : "", want);
  buf_free(&o);
  return ok_;
}

static void values_dates(void)
{
  double d;
  int k, y, m, dd;
  char g[64];
  ok("dates: 1 Jan 1900 is 1", date_serial(1900, 1, 1) == 1);
  ok("dates: 1 Mar 1900 is 61 (the phantom 29 Feb)", date_serial(1900, 3, 1) == 61);
  ok("dates: 27 Oct 2026", date_serial(2026, 10, 27) == 46322);
  ok("dates: months overflow into years", date_serial(2026, 14, 1) == date_serial(2027, 2, 1));
  serial_ymd(46322, &y, &m, &dd);
  ok("dates: back from a serial", y == 2026 && m == 10 && dd == 27);
  serial_ymd(60, &y, &m, &dd);
  ok("dates: serial 60 is 29 Feb 1900", y == 1900 && m == 2 && dd == 29);
  ok("dates: 27 Oct 2026 is a Tuesday", serial_weekday(46322) == 2);
  ok("numbers: plain", parse_number("12.5", &d, &k) && d == 12.5 && k == 0);
  ok("numbers: thousands and currency", parse_number("$1,234.50", &d, &k) && d == 1234.5 && k == 5);
  ok("numbers: percent", parse_number("15%", &d, &k) && near(d, 0.15) && k == 4);
  ok("numbers: brackets for negatives", parse_number("(42)", &d, &k) && d == -42);
  ok("numbers: scientific", parse_number("1.5e3", &d, &k) && d == 1500);
  ok("numbers: bad grouping is text", !parse_number("1,23", &d, &k));
  ok("numbers: words are text", !parse_number("abc", &d, &k) && !parse_number("12abc", &d, &k));
  ok("numbers: ISO date", parse_number("2026-10-27", &d, &k) && d == 46322 && k == 1);
  ledger_date_order = 0;
  ok("numbers: day/month/year", parse_number("27/10/2026", &d, &k) && d == 46322);
  ok("numbers: month name", parse_number("27-Oct-2026", &d, &k) && d == 46322);
  ok("numbers: time", parse_number("3:30 PM", &d, &k) && near(d, 15.5 / 24) && k == 3);
  ok("numbers: date and time", parse_number("27/10/2026 12:00", &d, &k) && near(d, 46322.5) && k == 2);
  num_general(1234.5, g, 11); ok("general: 1234.5", !strcmp(g, "1234.5"));
  num_general(1.0 / 3, g, 11); ok("general: a third in 11 characters", !strcmp(g, "0.333333333"));
  num_general(123456789012.0, g, 11); ok("general: big numbers go scientific", !strcmp(g, "1.23457E+11"));
  num_general(-0.5, g, 11); ok("general: negatives", !strcmp(g, "-0.5"));
}

static void formulas(void)
{
  Book *b = book_new();
  book_add_sheet(b, "Data Sheet", -1);
  ok("parse: precedence printed back", ftext(b, "1+2*3", "1+2*3"));
  ok("parse: brackets kept", ftext(b, "(1+2)*3", "(1+2)*3"));
  ok("parse: functions and ranges", ftext(b, "sum(a1:b3,$C$4)", "SUM(A1:B3,$C$4)"));
  ok("parse: other sheets quoted", ftext(b, "'Data Sheet'!A1*2", "'Data Sheet'!A1*2"));
  ok("parse: whole columns", ftext(b, "SUM(A:A)", "SUM(A:A)"));
  ok("parse: whole rows", ftext(b, "SUM($2:3)", "SUM($2:3)"));
  ok("parse: strings with quotes", ftext(b, "\"say \"\"hi\"\"\"&A1", "\"say \"\"hi\"\"\"&A1"));
  ok("parse: comparisons and percent", ftext(b, "A1>=50%", "A1>=50%"));
  ok("parse: array constants", ftext(b, "SUM({1,2;3,4})", "SUM({1,2;3,4})"));
  ok("parse: missing arguments", ftext(b, "IF(A1,,2)", "IF(A1,,2)"));
  ok("parse: unknown functions kept", ftext(b, "FOO(1)", "FOO(1)"));
  {
    int pos;
    ok("parse: errors refused", !formula_parse(b, 0, "1+", &pos) && !formula_parse(b, 0, "SUM(1", &pos) && !formula_parse(b, 0, "(1))", &pos));
  }
  ok("eval: arithmetic", evn(b, "1+2*3") == 7);
  ok("eval: -2^2 is 4 as in Excel", evn(b, "-2^2") == 4);
  ok("eval: power left to right", evn(b, "2^3^2") == 64);
  ok("eval: percent", near(evn(b, "50%*10"), 5));
  ok("eval: division by zero", evn(b, "1/0") == -1000 - E_DIV0);
  ok("eval: text joins", evs(b, "\"a\"&1&TRUE", "a1TRUE"));
  ok("eval: numeric text converts", evn(b, "\"3\"+4") == 7);
  ok("eval: text compares ignoring case", evn(b, "\"abc\"=\"ABC\"") == 1);
  ok("eval: numbers sort before text", evn(b, "1<\"a\"") == 1);
  ok("eval: unknown function is #NAME?", evn(b, "FOO(1)") == -1000 - E_NAME);
  book_free(b);
}

/* the engineering, maths, text and web functions of func_eng.c and func_misc.c (array results whole) */
static void eng_misc_functions(void)
{
  Book *b = book_new();
  ok("MMULT whole", evn(b, "INDEX(MMULT({1,2;3,4},{5,6;7,8}),1,2)") == 22 && evn(b, "ROWS(MMULT({1;2},{3,4}))") == 2 && evn(b, "COLUMNS(MMULT({1;2},{3,4}))") == 2);
  ok("MINVERSE", near(evn(b, "INDEX(MINVERSE({4,7;2,6}),2,1)"), -0.2) && near(evn(b, "SUMPRODUCT(MMULT({4,7;2,6},MINVERSE({4,7;2,6}))*MUNIT(2))"), 2));
  ok("MDETERM", near(evn(b, "MDETERM({1,3,8,5;1,3,6,1;1,1,1,0;7,3,10,2})"), 88));
  ok("number bases", evs(b, "DEC2BIN(-1)", "1111111111") && evs(b, "HEX2OCT(\"FFFFFFFF00\")", "7777777400") && evs(b, "BASE(255,16,4)", "00FF") && evn(b, "DECIMAL(\"zz\",36)") == 1295);
  ok("bits", evn(b, "BITXOR(BITLSHIFT(1,40),1)") == 1099511627777.0 && evn(b, "BITRSHIFT(1024,3)") == 128);
  ok("CONVERT prefixes", near(evn(b, "CONVERT(1,\"Mibyte\",\"kbyte\")"), 1048.576) && near(evn(b, "CONVERT(1,\"km^2\",\"m2\")"), 1e6) && near(evn(b, "CONVERT(212,\"F\",\"C\")"), 100));
  ok("ERF and Bessel", near(evn(b, "ERF(0.5)"), 0.520499877813047) && near(evn(b, "BESSELJ(1,0)"), 0.765197686557967) && near(evn(b, "BESSELY(1,1)"), -0.781212821300289));
  ok("complex", evs(b, "IMPRODUCT(\"1+2j\",\"3-j\")", "5+5j") && evs(b, "IMSUM(\"1.5\",\"-i\")", "1.5-i") && evs(b, "COMPLEX(0,0)", "0") && evs(b, "IMCONJUGATE(\"i\")", "-i"));
  ok("ROMAN forms", evs(b, "ROMAN(1999)", "MCMXCIX") && evs(b, "ROMAN(1999,4)", "MIM") && evn(b, "ARABIC(\"MIM\")") == 1999);
  ok("regex classes and anchors", evn(b, "REGEXTEST(\"2026-10-29\",\"^\\d{4}-\\d\\d-\\d\\d$\")") == 1 && evn(b, "REGEXTEST(\"ab\",\"^(a|ab)$\")") == 1 && evn(b, "REGEXTEST(\"x\",\"[^a-z]\")") == 0);
  ok("regex quantifiers", evs(b, "REGEXEXTRACT(\"<b>x</b><b>y</b>\",\"<b>.*?</b>\")", "<b>x</b>") && evs(b, "REGEXEXTRACT(\"aaaa\",\"a{2,3}\")", "aaa") && evs(b, "REGEXEXTRACT(\"ab{2}\",\"b\\{2\\}\")", "b{2}"));
  ok("regex empty loops and words", evs(b, "REGEXREPLACE(\"abc\",\"(x*)*\",\"-\")", "-a-b-c-") && evs(b, "REGEXREPLACE(\"cat catalog\",\"\\bcat\\b\",\"dog\")", "dog catalog"));
  ok("regex lookahead, case, backref", evs(b, "REGEXEXTRACT(\"price: 30 USD\",\"\\d+(?= USD)\")", "30") && evn(b, "REGEXTEST(\"ABC\",\"abc\",1)") == 1 && evs(b, "REGEXEXTRACT(\"xyzzy\",\"(.)\\1\")", "zz"));
  ok("regex bad patterns", evn(b, "REGEXTEST(\"a\",\"a)\")") == -1000 - E_VALUE && evn(b, "REGEXTEST(\"a\",\"*a\")") == -1000 - E_VALUE && evn(b, "REGEXTEST(\"a\",\"[a\")") == -1000 - E_VALUE);
  ok("regex unicode", evs(b, "REGEXEXTRACT(\"na\xc3\xafve\",\"a.v\")", "a\xc3\xafv") && evn(b, "REGEXTEST(\"\xc3\x89t\xc3\xa9\",\"^\xc3\xa9T\xc3\x89$\",1)") == 1);
  ok("TEXTSPLIT shape", evn(b, "ROWS(TEXTSPLIT(\"a,b;c\",\",\",\";\"))") == 2 && evn(b, "COLUMNS(TEXTSPLIT(\"a1b2c\",{\"1\",\"2\"}))") == 3);
  ok("TEXTBEFORE/AFTER", evs(b, "TEXTBEFORE(\"a.b.c\",\".\",-1)", "a.b") && evs(b, "TEXTAFTER(\"a.b.c\",\".\",2)", "c"));
  ok("FILTERXML", evn(b, "SUM(FILTERXML(\"<r><v>1</v><w><v>2</v></w><v>4</v></r>\",\"//v\"))") == 7 && evs(b, "FILTERXML(\"<r><v a='x'>1</v></r>\",\"/r/v/@a\")", "x"));
  ok("dates", evn(b, "DAYS360(DATE(2026,1,31),DATE(2026,3,31))") == 60 && evn(b, "ISOWEEKNUM(DATE(2027,1,3))") == 53 && evn(b, "WORKDAY.INTL(DATE(2026,10,30),1,\"0000110\")") == evn(b, "DATE(2026,11,1)"));
  ok("BAHTTEXT, EUROCONVERT", evs(b, "BAHTTEXT(21)", "\xe0\xb8\xa2\xe0\xb8\xb5\xe0\xb9\x88\xe0\xb8\xaa\xe0\xb8\xb4\xe0\xb8\x9a\xe0\xb9\x80\xe0\xb8\xad\xe0\xb9\x87\xe0\xb8\x94\xe0\xb8\x9a\xe0\xb8\xb2\xe0\xb8\x97\xe0\xb8\x96\xe0\xb9\x89\xe0\xb8\xa7\xe0\xb8\x99") && near(evn(b, "EUROCONVERT(1,\"DEM\",\"EUR\")"), 0.51));
  book_free(b);
}
static void functions(void)
{
  Book *b = book_new();
  put(b, "A1", "10"); put(b, "A2", "20"); put(b, "A3", "30"); put(b, "A4", "text"); put(b, "A5", "TRUE");
  put(b, "B1", "apple"); put(b, "B2", "banana"); put(b, "B3", "cherry");
  put(b, "C1", "1"); put(b, "C2", "2"); put(b, "C3", "3");
  ok("SUM skips text in cells", evn(b, "SUM(A1:A5)") == 60);
  ok("SUM counts typed-in logicals", evn(b, "SUM(1,TRUE)") == 2);
  ok("AVERAGE", evn(b, "AVERAGE(A1:A5)") == 20);
  ok("COUNT and COUNTA", evn(b, "COUNT(A1:A5)") == 3 && evn(b, "COUNTA(A1:A5)") == 5);
  ok("COUNTBLANK", evn(b, "COUNTBLANK(A1:A10)") == 5);
  ok("MIN MAX", evn(b, "MIN(A1:A3)") == 10 && evn(b, "MAX(A1:A3,50)") == 50);
  ok("ROUND half away from zero", evn(b, "ROUND(2.5,0)") == 3 && evn(b, "ROUND(-2.5,0)") == -3 && evn(b, "ROUND(1234.5678,-2)") == 1200);
  ok("ROUND steady on binary noise", evn(b, "ROUND(1.005,2)") == 1.01);
  ok("MOD takes the divisor's sign", evn(b, "MOD(-3,2)") == 1 && evn(b, "MOD(3,-2)") == -1);
  ok("INT floors", evn(b, "INT(-1.5)") == -2);
  ok("SQRT of negatives is #NUM!", evn(b, "SQRT(-1)") == -1000 - E_NUM);
  ok("SUMIF", evn(b, "SUMIF(A1:A3,\">15\")") == 50);
  /* found by testing every function against LibreOffice (tools/functest) */
  ok("CEILING both negative: away from zero", evn(b, "CEILING(-2.3,-1)") == -3 && evn(b, "CEILING(-2.5,-2)") == -4);
  ok("FLOOR both negative: towards zero", evn(b, "FLOOR(-2.7,-1)") == -2 && evn(b, "FLOOR(-2.5,-2)") == -2);
  ok("NUMBERVALUE with separators", evn(b, "NUMBERVALUE(\"2.500,27\",\",\",\".\")") == 2500.27 && evn(b, "NUMBERVALUE(\"1 234,5\",\",\")") == 1234.5 &&
     evn(b, "NUMBERVALUE(\"12.5%%\")") == 0.00125 && evn(b, "NUMBERVALUE(\"1,5,0\",\",\")") == -1000 - E_VALUE);
  ok("INDIRECT in R1C1", evn(b, "INDIRECT(\"R2C1\",FALSE)") == 20 && evn(b, "SUM(INDIRECT(\"R1C1:R3C1\",FALSE))") == 60);
  put(b, "D1", "a"); put(b, "D3", "c");
  ok("TEXTJOIN keeps empty cells when told to", evn(b, "LEN(TEXTJOIN(\"-\",FALSE,D1:D3))") == 4 && evn(b, "LEN(TEXTJOIN(\"-\",TRUE,D1:D3))") == 3);
  ok("SUMIF with a sum range", evn(b, "SUMIF(B1:B3,\"b*\",C1:C3)") == 2);
  ok("COUNTIF wildcards", evn(b, "COUNTIF(B1:B3,\"*an*\")") == 1);
  ok("COUNTIFS", evn(b, "COUNTIFS(A1:A3,\">=20\",C1:C3,\"<3\")") == 1);
  ok("AVERAGEIF", evn(b, "AVERAGEIF(A1:A3,\"<>20\")") == 20);
  ok("SUMPRODUCT", evn(b, "SUMPRODUCT(A1:A3,C1:C3)") == 140);
  ok("IF", evn(b, "IF(A1>5,1,2)") == 1 && evn(b, "IF(FALSE,1)") == 0);
  ok("IFERROR", evn(b, "IFERROR(1/0,7)") == 7);
  ok("AND OR XOR NOT", evn(b, "AND(TRUE,1)") == 1 && evn(b, "OR(FALSE,0)") == 0 && evn(b, "XOR(TRUE,TRUE)") == 0 && evn(b, "NOT(0)") == 1);
  ok("IFS and SWITCH", evn(b, "IFS(A1>50,1,A1>5,2)") == 2 && evn(b, "SWITCH(2,1,\"a\",2,5,9)") == 5);
  ok("CHOOSE", evn(b, "CHOOSE(2,10,20,30)") == 20);
  ok("VLOOKUP exact", evs(b, "VLOOKUP(\"banana\",B1:C3,2,FALSE)", "2"));
  ok("VLOOKUP approximate", evn(b, "VLOOKUP(25,A1:C3,3)") == 2);
  ok("VLOOKUP missing is #N/A", evn(b, "VLOOKUP(\"kiwi\",B1:C3,2,FALSE)") == -1000 - E_NA);
  ok("MATCH", evn(b, "MATCH(\"cherry\",B1:B3,0)") == 3 && evn(b, "MATCH(25,A1:A3)") == 2);
  ok("INDEX", evn(b, "INDEX(A1:C3,2,3)") == 2);
  ok("INDEX/MATCH together", evs(b, "INDEX(B1:B3,MATCH(30,A1:A3,0))", "cherry"));
  ok("XLOOKUP", evs(b, "XLOOKUP(20,A1:A3,B1:B3)", "banana") && evs(b, "XLOOKUP(99,A1:A3,B1:B3,\"none\")", "none"));
  ok("HLOOKUP", evn(b, "HLOOKUP(\"x\",{\"x\",\"y\";1,2},2,FALSE)") == 1);
  ok("OFFSET", evn(b, "SUM(OFFSET(A1,1,0,2,1))") == 50);
  ok("INDIRECT", evn(b, "INDIRECT(\"A2\")") == 20);
  ok("ROW COLUMN ROWS COLUMNS", evn(b, "ROW(C5)") == 5 && evn(b, "COLUMN(C5)") == 3 && evn(b, "ROWS(A1:B4)") == 4 && evn(b, "COLUMNS(A1:B4)") == 2);
  ok("ADDRESS", evs(b, "ADDRESS(2,3)", "$C$2") && evs(b, "ADDRESS(2,3,4)", "C2"));
  ok("LEN LEFT RIGHT MID", evn(b, "LEN(\"hello\")") == 5 && evs(b, "LEFT(\"hello\",2)", "he") && evs(b, "RIGHT(\"hello\",3)", "llo") && evs(b, "MID(\"hello\",2,3)", "ell"));
  ok("UPPER LOWER PROPER", evs(b, "UPPER(\"abc\")", "ABC") && evs(b, "LOWER(\"ABC\")", "abc") && evs(b, "PROPER(\"hello world\")", "Hello World"));
  ok("TRIM", evs(b, "TRIM(\"  a   b  \")", "a b"));
  ok("FIND and SEARCH", evn(b, "FIND(\"l\",\"hello\")") == 3 && evn(b, "SEARCH(\"L*O\",\"hello\")") == 3 && evn(b, "FIND(\"L\",\"hello\")") == -1000 - E_VALUE);
  ok("SUBSTITUTE REPLACE", evs(b, "SUBSTITUTE(\"a-b-c\",\"-\",\"+\")", "a+b+c") && evs(b, "SUBSTITUTE(\"a-b-c\",\"-\",\"+\",2)", "a-b+c") && evs(b, "REPLACE(\"abcdef\",2,3,\"X\")", "aXef"));
  ok("CONCAT TEXTJOIN", evs(b, "CONCAT(B1:B2)", "applebanana") && evs(b, "TEXTJOIN(\", \",TRUE,B1:B3)", "apple, banana, cherry"));
  ok("REPT", evs(b, "REPT(\"ab\",3)", "ababab"));
  ok("VALUE", evn(b, "VALUE(\"1,234\")") == 1234);
  ok("TEXT", evs(b, "TEXT(1234.5,\"#,##0.00\")", "1,234.50") && evs(b, "TEXT(46322,\"dd mmm yyyy\")", "27 Oct 2026"));
  ok("FIXED DOLLAR", evs(b, "FIXED(1234.567,1)", "1,234.6") && evs(b, "DOLLAR(-5)", "($5.00)"));
  ok("CHAR CODE", evs(b, "CHAR(65)", "A") && evn(b, "CODE(\"a\")") == 97);
  ok("DATE YEAR MONTH DAY", evn(b, "DATE(2026,10,27)") == 46322 && evn(b, "YEAR(46322)") == 2026 && evn(b, "MONTH(46322)") == 10 && evn(b, "DAY(46322)") == 27);
  ok("TIME HOUR MINUTE SECOND", near(evn(b, "TIME(12,30,0)"), 0.5208333333333334) && evn(b, "HOUR(0.75)") == 18 && evn(b, "MINUTE(TIME(1,2,3))") == 2 && evn(b, "SECOND(TIME(1,2,3))") == 3);
  ok("TODAY and NOW", evn(b, "TODAY()") == 46322 && near(evn(b, "NOW()"), 46322.5));
  ok("WEEKDAY", evn(b, "WEEKDAY(46322)") == 3 && evn(b, "WEEKDAY(46322,2)") == 2);
  ok("EDATE EOMONTH", evn(b, "EDATE(DATE(2026,1,31),1)") == date_serial(2026, 2, 28) && evn(b, "EOMONTH(DATE(2026,2,10),0)") == date_serial(2026, 2, 28));
  ok("DATEDIF", evn(b, "DATEDIF(DATE(2000,5,15),DATE(2026,10,27),\"Y\")") == 26 && evn(b, "DATEDIF(DATE(2026,1,1),DATE(2026,3,1),\"D\")") == 59);
  ok("NETWORKDAYS WORKDAY", evn(b, "NETWORKDAYS(DATE(2026,10,26),DATE(2026,11,6))") == 10 && evn(b, "WORKDAY(DATE(2026,10,30),1)") == date_serial(2026, 11, 2));
  ok("WEEKNUM", evn(b, "WEEKNUM(DATE(2026,1,1))") == 1 && evn(b, "WEEKNUM(DATE(2026,10,27),21)") == 44);
  ok("PMT", near(evn(b, "PMT(0.05/12,360,200000)"), -1073.6432460242797));
  ok("FV PV", near(evn(b, "FV(0.06/12,10,-200,-500,1)"), 2581.4033740601) && near(evn(b, "PV(0.08/12,240,500)"), -59777.14585118638));
  ok("NPV IRR", near(evn(b, "NPV(0.1,-10000,3000,4200,6800)"), 1188.443412188) && near(evn(b, "IRR({-70000,12000,15000,18000,21000,26000})"), 0.08663094803653162));
  ok("RATE NPER", near(evn(b, "RATE(48,-200,8000)"), 0.007701472488201) && near(evn(b, "NPER(0.01,-100,1000)"), 10.5886444594));
  ok("MEDIAN MODE", evn(b, "MEDIAN(1,3,2,4)") == 2.5 && evn(b, "MODE(1,2,2,3)") == 2);
  ok("STDEV VAR", near(evn(b, "STDEV(2,4,4,4,5,5,7,9)"), 2.138089935299395) && near(evn(b, "VARP(2,4,4,4,5,5,7,9)"), 4));
  ok("LARGE SMALL RANK", evn(b, "LARGE(A1:A3,1)") == 30 && evn(b, "SMALL(A1:A3,2)") == 20 && evn(b, "RANK(20,A1:A3)") == 2);
  ok("PERCENTILE QUARTILE", evn(b, "PERCENTILE({1,2,3,4},0.5)") == 2.5 && evn(b, "QUARTILE({1,2,3,4,5},1)") == 2);
  ok("ISBLANK ISNUMBER ISTEXT ISERROR", evn(b, "ISBLANK(Z99)") == 1 && evn(b, "ISNUMBER(A1)") == 1 && evn(b, "ISTEXT(B1)") == 1 && evn(b, "ISERROR(1/0)") == 1);
  ok("CEILING FLOOR MROUND", evn(b, "CEILING(4.3,2)") == 6 && evn(b, "FLOOR(4.3,2)") == 4 && evn(b, "MROUND(10,3)") == 9);
  ok("FACT COMBIN GCD LCM", evn(b, "FACT(5)") == 120 && evn(b, "COMBIN(5,2)") == 10 && evn(b, "GCD(12,18)") == 6 && evn(b, "LCM(4,6)") == 12);
  ok("array maths in SUM", evn(b, "SUM(A1:A3*C1:C3)") == 140);
  ok("TRANSPOSE in SUM", evn(b, "SUM(TRANSPOSE(A1:A3))") == 60);
  book_free(b);
}

static void editing(void)
{
  Book *b = book_new();
  int sh;
  put(b, "A1", "5"); put(b, "A2", "=A1*2"); put(b, "A3", "=SUM(A1:A2)");
  ok("recalc: a chain", num_at(b, "A3") == 15);
  put(b, "A1", "7");
  ok("recalc: an edit flows through", num_at(b, "A3") == 21);
  book_undo(b, NULL, NULL);
  ok("undo: the old value is back", num_at(b, "A1") == 5 && num_at(b, "A3") == 15);
  book_redo(b, NULL, NULL);
  ok("redo", num_at(b, "A1") == 7);
  put(b, "B1", "=B1+1");
  ok("circular references show 0", num_at(b, "B1") == 0 || num_at(b, "B1") == 1);
  book_insert_rows(b, 0, 1, 2);
  ok("insert rows: the cells move", num_at(b, "A4") == 14 && text_at(b, "A4", "=A1*2"));
  ok("insert rows: ranges stretch", text_at(b, "A5", "=SUM(A1:A4)") && num_at(b, "A5") == 21);
  book_delete_rows(b, 0, 0, 1);
  ok("delete rows: references to the deleted cell are #REF!", text_at(b, "A3", "=#REF!*2"));
  book_undo(b, NULL, NULL);
  ok("undo delete rows", text_at(b, "A4", "=A1*2") && num_at(b, "A1") == 7);
  book_insert_cols(b, 0, 0, 1);
  ok("insert columns", text_at(b, "B4", "=B1*2") && num_at(b, "B4") == 14);
  /* sheets */
  sh = book_add_sheet(b, "Totals", -1);
  book_input(b, sh, 0, 0, "=Sheet1!B1*10", NULL);
  ok("sheets: references across", num_at(b, "Totals!A1") == 70);
  book_rename_sheet(b, 0, "Data");
  ok("sheets: renaming keeps references", text_at(b, "Totals!A1", "=Data!B1*10"));
  book_insert_rows(b, 0, 0, 1);
  ok("sheets: rows inserted on another sheet move references", text_at(b, "Totals!A1", "=Data!B2*10") && num_at(b, "Totals!A1") == 70);
  /* long chains are finished over several passes */
  {
    Book *c = book_new();
    int i;
    char ref[16], f[32];
    put(c, "A1", "1");
    for (i = 2; i <= 600; i++) { sprintf(ref, "A%d", i); sprintf(f, "=A%d+1", i - 1); put(c, ref, f); }
    ok("recalc: a chain of 600 cells", num_at(c, "A600") == 600);
    put(c, "A1", "101");
    ok("recalc: the chain follows an edit", num_at(c, "A600") == 700);
    book_free(c);
  }
  /* auto formats */
  {
    Book *c = book_new();
    put(c, "A1", "27/10/2026"); put(c, "A2", "15%"); put(c, "A3", "$1,234.50"); put(c, "A4", "1,234");
    ok("typing a date formats the cell", shows(c, 0, 0, 0, "27/10/2026"));
    ok("typing a percentage", shows(c, 0, 1, 0, "15%"));
    ok("typing currency", shows(c, 0, 2, 0, "$1,234.50 "));
    ok("typing thousands", shows(c, 0, 3, 0, "1,234"));
    book_free(c);
  }
  book_free(b);
}

static void formats(void)
{
  ok("format: 0.00", fmt("0.00", 3.14159, "3.14"));
  ok("format: #,##0", fmt("#,##0", 1234567.8, "1,234,568"));
  ok("format: #,##0.00 negative", fmt("#,##0.00", -1234.5, "-1,234.50"));
  ok("format: percent", fmt("0.0%", 0.256, "25.6%"));
  ok("format: scientific", fmt("0.00E+00", 12345.678, "1.23E+04"));
  ok("format: scientific small", fmt("0.00E+00", 0.000123, "1.23E-04"));
  ok("format: currency with brackets", fmt("\"$\"#,##0.00_);(\"$\"#,##0.00)", -5, "($5.00)"));
  ok("format: red negatives", fmt("0;[Red]-0", -3, "-3"));
  ok("format: zero section", fmt("0;-0;\"zero\"", 0, "zero"));
  ok("format: scaling by thousands", fmt("#,##0,\"K\"", 1234567, "1,235K"));
  ok("format: leading zeros", fmt("00000", 42, "00042"));
  ok("format: # hides a zero integer", fmt("#.00", 0.5, ".50"));
  ok("format: fractions", fmt("# ?/?", 1.5, "1 1/2"));
  ok("format: fraction eighths", fmt("# ?/8", 2.625, "2 5/8"));
  ok("format: dates", fmt("d/mm/yyyy", 46322, "27/10/2026"));
  ok("format: long dates", fmt("dddd, d mmmm yyyy", 46322, "Tuesday, 27 October 2026"));
  ok("format: times", fmt("h:mm AM/PM", 46322.75, "6:00 PM") && fmt("hh:mm:ss", 0.5 + 1.0 / 86400, "12:00:01"));
  ok("format: elapsed hours", fmt("[h]:mm", 1.5, "36:00"));
  ok("format: minutes vs months", fmt("mm:ss", 1.0 / 1440 * 2.5, "02:30") && fmt("mmm-yy", 46322, "Oct-26"));
  ok("format: conditions", fmt("[>=1000]#,##0,\"K\";0", 2500, "3K") && fmt("[>=1000]#,##0,\"K\";0", 12, "12"));
  ok("format: accounting", fmt("_(* #,##0.00_);_(* (#,##0.00);_(* \"-\"??_);_(@_)", 1234.5, " 1,234.50 "));
  ok("format: accounting zero", fmt("_(* #,##0.00_);_(* (#,##0.00);_(* \"-\"??_);_(@_)", 0, " -   "));
  ok("format: General in a format", fmt("General", 0.1, "0.1"));
  ok("format: currency code", fmt("[$€-1809]#,##0.00", 5, "€5.00"));
  ok("format: rounded negatives lose their sign", fmt("0", -0.2, "0"));
  {
    Book *b = book_new();
    Value v = val_str("hi");
    char out[64];
    u32 col;
    fmt_value(b, "0;0;0;\"<\"@\">\"", &v, out, sizeof out, &col, 11);
    ok("format: text section", !strcmp(out, "<hi>"));
    val_free(&v);
    v = val_num(-3);
    fmt_value(b, "0;[Red]0", &v, out, sizeof out, &col, 11);
    ok("format: colour from the section", col == 0xFF0000 && !strcmp(out, "3"));
    book_free(b);
  }
}

static void files(void)
{
  Book *b = book_new(), *c;
  size_t n;
  u8 *data;
  char err[200];
  int sh2;
  Style st;
  Font f;
  put(b, "A1", "Region"); put(b, "B1", "Sales");
  put(b, "A2", "North"); put(b, "B2", "1200.5");
  put(b, "A3", "South"); put(b, "B3", "980");
  put(b, "B4", "=SUM(B2:B3)");
  put(b, "C2", "27/10/2026");
  put(b, "D2", "TRUE");
  put(b, "E2", "=1/0");
  sh2 = book_add_sheet(b, "Notes & Totals", -1);
  book_input(b, sh2, 0, 0, "='Sheet1'!B4*2", NULL);
  st = b->styles[0];
  f = b->fonts[0]; f.bold = 1; f.color = 0xC00000;
  st.font = (u16)book_font(b, &f);
  st.fill = FILL_ON | 0xFFF2CC;
  st.border[B_BOTTOM] = BS_DOUBLE;
  st.halign = HA_CENTER;
  st.numfmt = (u16)book_fmt(b, "#,##0.00");
  book_set_style(b, 0, 1, 1, book_style(b, &st));
  book_set_col_width(b, 0, 0, 120);
  book_set_row_height(b, 0, 0, 30);
  book_merge(b, 0, (Rect){ 5, 0, 5, 2 }, 1);
  book_freeze(b, 0, 1, 0);
  data = xlsx_save(b, &n);
  ok("xlsx: saved", data && n > 1000);
  c = data ? xlsx_load(data, n, err, sizeof err) : NULL;
  ok("xlsx: loaded back", c != NULL);
  if (c) {
    const Value *v;
    ok("xlsx: two sheets with their names", c->nsh == 2 && !strcmp(c->sh[1]->name, "Notes & Totals"));
    ok("xlsx: text and numbers", text_at(c, "A2", "North") && num_at(c, "B2") == 1200.5);
    ok("xlsx: formulas", text_at(c, "B4", "=SUM(B2:B3)") && num_at(c, "B4") == 2180.5);
    ok("xlsx: cross-sheet formulas", text_at(c, "'Notes & Totals'!A1", "=Sheet1!B4*2") && num_at(c, "'Notes & Totals'!A1") == 4361);
    v = cell_value(c, 0, 1, 3);
    ok("xlsx: logical values", v->t == V_BOOL && v->e == 1);
    v = cell_value(c, 0, 1, 4);
    ok("xlsx: errors", v->t == V_ERR && v->e == E_DIV0);
    ok("xlsx: dates keep their format", shows(c, 0, 1, 2, "27/10/2026"));
    {
      int s2 = sheet_cell_style(c->sh[0], 1, 1);
      Style *cs = &c->styles[s2];
      Font *cf = &c->fonts[cs->font];
      ok("xlsx: number format", shows(c, 0, 1, 1, "1,200.50"));
      ok("xlsx: font, fill, border, alignment", cf->bold && cf->color == 0xC00000 && cs->fill == (FILL_ON | 0xFFF2CC) &&
         cs->border[B_BOTTOM] == BS_DOUBLE && cs->halign == HA_CENTER);
    }
    ok("xlsx: column width and row height", sheet_col_width(c->sh[0], 0) == 120 && sheet_row_height(c->sh[0], 0) == 30);
    ok("xlsx: merges and frozen panes", c->sh[0]->nmerges == 1 && c->sh[0]->merges[0].c2 == 2 && c->sh[0]->freezer == 1);
    book_free(c);
  }
  free(data);
  {
    /* CSV */
    Book *d;
    char *csv = csv_save(b, 0, ',');
    ok("csv: saved", csv && strstr(csv, "Region,Sales") && strstr(csv, "North,1200.5"));
    d = csv ? csv_load(csv, strlen(csv), ',') : NULL;
    ok("csv: read back with numbers", d && num_at(d, "B2") == 1200.5 && text_at(d, "A3", "South"));
    if (d) book_free(d);
    free(csv);
    d = csv_load("name,\"a, b\",\"say \"\"hi\"\"\"\n1,2,3\n", 31, 0);
    ok("csv: quotes and commas", d && text_at(d, "B1", "a, b") && text_at(d, "C1", "say \"hi\"") && num_at(d, "C2") == 3);
    if (d) book_free(d);
  }
  book_free(b);
}

static Rect R(const char *ref) { u16 sh; Rect r; u8 fl; parse_ref(NULL, ref, &sh, &r, &fl); return r; }
static void ops(void)
{
  Book *b = book_new();
  Clip *cl;
  Rect out;
  char t[256];
  int n;
  SortKey k;
  /* copy and paste: formulas move, values and formats */
  put(b, "A1", "1"); put(b, "A2", "2"); put(b, "B1", "=A1*10"); put(b, "B2", "=A2*10");
  cl = clip_copy(b, 0, R("A1:B2"));
  ok("paste: copied", cl && cl->rows == 2 && cl->cols == 2);
  ok("paste: pasted", book_paste(b, 0, 4, 2, cl, PASTE_ALL, 0, &out) && out.r1 == 4 && out.c2 == 3);
  ok("paste: formula moved", text_at(b, "D5", "=C5*10") && num_at(b, "D5") == 10);
  book_paste(b, 0, 9, 0, cl, PASTE_VALUES, 0, NULL);
  ok("paste: values only", text_at(b, "B10", "10"));
  book_paste(b, 0, 0, 5, cl, PASTE_ALL, 1, &out);
  ok("paste: transposed", out.r2 == 1 && out.c2 == 6 && num_at(b, "G1") == 2 && text_at(b, "F2", "=E2*10"));
  clip_free(cl);
  ok("paste: one undo step", book_undo(b, NULL, NULL) && !sheet_cell(b->sh[0], 0, 6));
  book_paste_text(b, 0, 20, 0, "x\t1\ny\t2.5\n", &out);
  ok("paste text: tab separated", out.r2 == 21 && out.c2 == 1 && num_at(b, "B22") == 2.5 && text_at(b, "A21", "x"));
  {
    char *txt = book_copy_text(b, 0, R("A21:B22"));
    ok("copy text", txt && !strcmp(txt, "x\t1\ny\t2.5\n"));
    free(txt);
  }
  book_free(b);
  /* fill */
  b = book_new();
  put(b, "A1", "1"); put(b, "A2", "3");
  book_fill(b, 0, R("A1:A2"), R("A1:A5"));
  ok("fill: number series", num_at(b, "A5") == 9);
  put(b, "B1", "Item 1"); book_fill(b, 0, R("B1"), R("B1:B3"));
  ok("fill: numbered text", text_at(b, "B3", "Item 3"));
  put(b, "C1", "Mon"); book_fill(b, 0, R("C1"), R("C1:C7"));
  ok("fill: day names wrap", text_at(b, "C7", "Sun"));
  put(b, "D1", "January"); book_fill(b, 0, R("D1"), R("F1"));
  book_fill(b, 0, R("D1"), R("D1:F1"));
  ok("fill: months across", text_at(b, "F1", "March"));
  put(b, "E3", "=A3*2"); book_fill(b, 0, R("E3"), R("E3:E5"));
  ok("fill: formulas move", text_at(b, "E5", "=A5*2") && num_at(b, "E5") == 18);
  put(b, "G1", "5"); book_fill(b, 0, R("G1"), R("G1:G3"));
  ok("fill: one number repeats", num_at(b, "G3") == 5);
  put(b, "H1", "27/10/2026"); book_fill(b, 0, R("H1"), R("H1:H3"));
  ok("fill: a date counts on", num_at(b, "H3") == 46324 && shows(b, 0, 2, 7, "29/10/2026"));
  put(b, "I5", "10"); put(b, "I6", "8"); book_fill(b, 0, R("I5:I6"), R("I3:I6"));
  ok("fill: upwards", num_at(b, "I3") == 14);
  book_free(b);
  /* sort */
  b = book_new();
  put(b, "A1", "Name"); put(b, "B1", "Score");
  put(b, "A2", "carol"); put(b, "B2", "70");
  put(b, "A3", "Alice"); put(b, "B3", "90");
  put(b, "A4", "bob"); put(b, "B4", "80");
  put(b, "C2", "=B2/10"); put(b, "C3", "=B3/10"); put(b, "C4", "=B4/10");
  k.col = 0; k.desc = 0;
  book_sort(b, 0, R("A1:C4"), &k, 1, 1);
  ok("sort: by text ignoring case, header kept", text_at(b, "A1", "Name") && text_at(b, "A2", "Alice") && text_at(b, "A3", "bob") && text_at(b, "A4", "carol"));
  ok("sort: formulas go with their rows", text_at(b, "C2", "=B2/10") && num_at(b, "C2") == 9);
  k.col = 1; k.desc = 1;
  book_sort(b, 0, R("A2:C4"), &k, 1, 0);
  ok("sort: numbers descending", num_at(b, "B2") == 90 && num_at(b, "B4") == 70);
  put(b, "B5", "x"); put(b, "A5", "dave");
  k.desc = 0; book_sort(b, 0, R("A2:B6"), &k, 1, 0);
  ok("sort: numbers before text, empty last", num_at(b, "B2") == 70 && text_at(b, "B5", "x"));
  book_free(b);
  /* duplicates */
  b = book_new();
  put(b, "A1", "a"); put(b, "A2", "b"); put(b, "A3", "A"); put(b, "A4", "c"); put(b, "A5", "b");
  n = book_remove_duplicates(b, 0, R("A1:A5"), 0, 0);
  ok("duplicates: removed", n == 2 && text_at(b, "A3", "c") && !sheet_cell(b->sh[0], 3, 0));
  book_free(b);
  /* find and replace */
  b = book_new();
  put(b, "A1", "apple"); put(b, "B2", "Pineapple"); put(b, "C3", "=LEN(\"apple\")");
  book_add_sheet(b, "Two", -1);
  book_input(b, 1, 0, 0, "apple pie", NULL);
  {
    int sh = 0; i32 r = 0, c = 0;
    ok("find: next", book_find(b, &sh, &r, &c, "apple", 0) && r == 1 && c == 1);
    ok("find: wraps to the start", book_find(b, &sh, &r, &c, "apple", 0) && r == 0 && c == 0);
    ok("find: whole cell", book_find(b, &sh, &r, &c, "APPLE", FIND_WHOLE) && r == 0 && c == 0);
    ok("find: in formulas", book_find(b, &sh, &r, &c, "LEN", FIND_FORMULAS) && r == 2);
    sh = 0; r = 1; c = 1;
    ok("find: other sheets", book_find(b, &sh, &r, &c, "pie", FIND_BOOK) && sh == 1 && r == 0);
  }
  ok("replace: one cell", book_replace_cell(b, 0, 1, 1, "apple", "cone", 0) == 1 && text_at(b, "B2", "Pinecone"));
  ok("replace: all", book_replace_all(b, 0, "apple", "pear", FIND_BOOK) == 3 && text_at(b, "C3", "=LEN(\"pear\")") && num_at(b, "C3") == 4);
  book_free(b);
  /* references in a formula being typed */
  {
    RefSpan sp[8];
    Book *bb = book_new();
    int k = formula_ref_spans(bb, "=A1+SUM(B2:C4)*\"D5\"+a1+LOG10(2)+Sheet1!$E$1", sp, 8);
    ok("ref spans: found", k == 4 && sp[0].pos == 1 && sp[0].len == 2 && sp[1].pos == 8 && sp[1].len == 5 && sp[1].r.r2 == 3 && sp[1].r.c2 == 2);
    ok("ref spans: same reference, same colour", k == 4 && sp[2].colour == sp[0].colour && sp[3].colour == 2 && sp[3].sheet == 0);
    book_free(bb);
  }
  /* edit text */
  b = book_new();
  put(b, "A1", "27/10/2026"); put(b, "A2", "12.5%"); put(b, "A3", "'123"); put(b, "A4", "=SUM(1,2)"); put(b, "A5", "0.1");
  book_edit_text(b, 0, 0, 0, t, sizeof t); ok("edit text: a date", !strcmp(t, "27/10/2026"));
  book_edit_text(b, 0, 1, 0, t, sizeof t); ok("edit text: a percentage", !strcmp(t, "12.5%"));
  book_edit_text(b, 0, 2, 0, t, sizeof t); ok("edit text: text that looks like a number", !strcmp(t, "'123"));
  book_edit_text(b, 0, 3, 0, t, sizeof t); ok("edit text: a formula", !strcmp(t, "=SUM(1,2)"));
  book_edit_text(b, 0, 4, 0, t, sizeof t); ok("edit text: a decimal", !strcmp(t, "0.1"));
  book_free(b);
}

static void autofilter(void)
{
  Book *b = book_new(), *d;
  char **v; int n, bl, i;
  const char *keep[] = { "North", "East" };
  u8 *x; size_t len; char err[100];
  put(b, "A1", "Region"); put(b, "B1", "Sales");
  put(b, "A2", "North"); put(b, "B2", "10");
  put(b, "A3", "South"); put(b, "B3", "20");
  put(b, "A4", "East"); put(b, "B4", "30");
  put(b, "A5", "North"); put(b, "B5", "40");
  put(b, "B6", "50");
  ok("filter: on", book_autofilter(b, 0, R("A1:B6"), 1) && b->sh[0]->filter.r2 == 5);
  n = book_filter_values(b, 0, 0, &v, &bl);
  ok("filter: the column's values, once each, sorted", n == 3 && !strcmp(v[0], "East") && !strcmp(v[2], "South") && bl);
  for (i = 0; i < n; i++) free(v[i]);
  free(v);
  book_filter_column(b, 0, 0, keep, 2, 0);
  ok("filter: rows that don't pass are hidden", sheet_row_height(b->sh[0], 2) == 0 && sheet_row_height(b->sh[0], 5) == 0 && sheet_row_height(b->sh[0], 1) > 0 && sheet_row_height(b->sh[0], 0) > 0);
  ok("filter: SUBTOTAL-free formulas still see every row", (put(b, "C1", "=SUM(B2:B6)"), num_at(b, "C1") == 150));
  put(b, "A4", "West");
  ok("filter: reapply after a change", book_filter_reapply(b, 0) && sheet_row_height(b->sh[0], 3) == 0 && sheet_row_height(b->sh[0], 1) > 0);
  put(b, "A4", "East"); book_filter_reapply(b, 0);
  x = xlsx_save(b, &len);
  d = x ? xlsx_load(x, len, err, sizeof err) : NULL;
  ok("filter: saved and read back", d && d->sh[0]->filter.r1 == 0 && d->sh[0]->nfcols == 1 && d->sh[0]->fcols[0].n == 2 && sheet_row_height(d->sh[0], 2) == 0 &&
     d->sh[0]->rows[2]->filtered && !d->sh[0]->rows[2]->hidden);
  if (d) book_free(d);
  free(x);
  book_filter_column(b, 0, 0, NULL, -1, 0);
  ok("filter: cleared", sheet_row_height(b->sh[0], 2) > 0 && !b->sh[0]->nfcols);
  ok("filter: undo brings the filter back", book_undo(b, NULL, NULL) && sheet_row_height(b->sh[0], 2) == 0);
  book_insert_rows(b, 0, 0, 2);
  ok("filter: moves with inserted rows", b->sh[0]->filter.r1 == 2 && b->sh[0]->filter.r2 == 7);
  book_autofilter(b, 0, R("A1:B1"), 0);
  ok("filter: off shows everything", b->sh[0]->filter.r1 < 0 && sheet_row_height(b->sh[0], 4) > 0);
  book_free(b);
}

/* conditional formatting: the look a cell gets */
static int cf_fill(Book *b, const char *ref)
{
  u16 sh; Rect r; u8 fl; CondLook lk;
  parse_ref(b, ref, &sh, &r, &fl);
  cond_look(b, 0, r.r1, r.c1, &lk);
  return lk.fill != 0;
}
static void condfmt(void)
{
  Book *b = book_new(), *d;
  CondRule r;
  CondLook lk;
  u8 *x; size_t len; char err[100], t[200];
  int i;
  const char *vals[] = { "5", "12", "7", "12", "30", "", "apple pie", "=1/0" };
  for (i = 0; i < 8; i++) { char ref[16]; sprintf(ref, "A%d", i + 1); put(b, ref, vals[i]); }
  put(b, "B1", "10"); put(b, "B2", "10"); put(b, "B3", "1");
  /* greater than 10 */
  cond_rule_init(&r);
  cond_parse_areas(b, "A1:A8", &r);
  r.type = CF_CELL; r.op = CO_GT; r.f1 = cond_value(b, 0, "10"); r.fill = FILL_ON | 0xFFC7CE; r.color = 0x9C0006;
  ok("cond: rule added", book_cond_add(b, 0, &r, 0) && b->sh[0]->ncfs == 1);
  ok("cond: greater than", !cf_fill(b, "A1") && cf_fill(b, "A2") && cf_fill(b, "A5") && !cf_fill(b, "A6"));
  cond_look(b, 0, 1, 0, &lk);
  ok("cond: the look", lk.fill == (FILL_ON | 0xFFC7CE) && lk.color == 0x9C0006);
  ok("cond: outside the area untouched", !cf_fill(b, "B1"));
  cond_describe(b, 0, &b->sh[0]->cfs[0], t, sizeof t);
  ok("cond: described", !strcmp(t, "Cell value > 10"));
  cond_rule_free(&r);
  /* a relative formula: A is greater than B on the same row */
  cond_rule_init(&r);
  cond_parse_areas(b, "A1:A3", &r);
  r.type = CF_EXPR; r.f1 = cond_value(b, 0, "=A1>B1"); r.bold = 1;
  book_cond_add(b, 0, &r, -1);
  cond_look(b, 0, 1, 0, &lk);
  ok("cond: formula, relative", lk.bold && (cond_look(b, 0, 2, 0, &lk), lk.bold) && (cond_look(b, 0, 0, 0, &lk), !lk.bold));
  cond_rule_free(&r);
  book_cond_clear(b, 0, NULL);
  ok("cond: cleared", b->sh[0]->ncfs == 0);
  ok("cond: undo brings them back", book_undo(b, NULL, NULL) && b->sh[0]->ncfs == 2);
  book_cond_clear(b, 0, NULL);
  /* between, either way round */
  cond_rule_init(&r); cond_parse_areas(b, "A1:A8", &r);
  r.type = CF_CELL; r.op = CO_BETWEEN; r.f1 = cond_value(b, 0, "12"); r.f2 = cond_value(b, 0, "6"); r.fill = FILL_ON | 0xFFEB9C;
  book_cond_add(b, 0, &r, 0); cond_rule_free(&r);
  ok("cond: between", !cf_fill(b, "A1") && cf_fill(b, "A2") && cf_fill(b, "A3") && !cf_fill(b, "A5"));
  book_cond_clear(b, 0, NULL);
  /* text, duplicates, top, average, blanks, errors */
  cond_rule_init(&r); cond_parse_areas(b, "A1:A8", &r); r.type = CF_TEXT; r.text = str_dup("PIE"); r.fill = FILL_ON | 1;
  book_cond_add(b, 0, &r, 0); cond_rule_free(&r);
  ok("cond: text contains, any case", cf_fill(b, "A7") && !cf_fill(b, "A1"));
  book_cond_clear(b, 0, NULL);
  cond_rule_init(&r); cond_parse_areas(b, "A1:A8", &r); r.type = CF_DUP; r.fill = FILL_ON | 1;
  book_cond_add(b, 0, &r, 0); cond_rule_free(&r);
  ok("cond: duplicates", cf_fill(b, "A2") && cf_fill(b, "A4") && !cf_fill(b, "A1") && !cf_fill(b, "A6"));
  b->sh[0]->cfs[0].type = CF_UNIQUE; b->changes++;
  ok("cond: unique", !cf_fill(b, "A2") && cf_fill(b, "A1"));
  put(b, "A1", "12");
  b->sh[0]->cfs[0].type = CF_DUP;
  ok("cond: duplicates follow edits", cf_fill(b, "A1"));
  put(b, "A1", "5");
  book_cond_clear(b, 0, NULL);
  cond_rule_init(&r); cond_parse_areas(b, "A1:A8", &r); r.type = CF_TOP; r.rank = 2; r.fill = FILL_ON | 1;
  book_cond_add(b, 0, &r, 0); cond_rule_free(&r);
  ok("cond: top 2 (ties included)", cf_fill(b, "A5") && cf_fill(b, "A2") && cf_fill(b, "A4") && !cf_fill(b, "A3"));
  b->sh[0]->cfs[0].bottom = 1; b->sh[0]->cfs[0].rank = 1; b->changes++;   /* (changed behind the book's back) */
  ok("cond: bottom 1", cf_fill(b, "A1") && !cf_fill(b, "A3"));
  b->sh[0]->cfs[0].type = CF_AVERAGE; b->sh[0]->cfs[0].below = 0; b->changes++;
  ok("cond: above average (13.2)", cf_fill(b, "A5") && !cf_fill(b, "A2"));
  b->sh[0]->cfs[0].type = CF_BLANKS;
  ok("cond: blanks", cf_fill(b, "A6") && !cf_fill(b, "A1"));
  b->sh[0]->cfs[0].type = CF_ERRORS;
  ok("cond: errors", cf_fill(b, "A8") && !cf_fill(b, "A1"));
  book_cond_clear(b, 0, NULL);
  /* data bars and colour scales */
  cond_rule_init(&r); cond_parse_areas(b, "A1:A5", &r); r.type = CF_BAR;
  book_cond_add(b, 0, &r, 0); cond_rule_free(&r);
  cond_look(b, 0, 4, 0, &lk);
  ok("cond: data bar, the largest full", lk.bar == 1000);
  cond_look(b, 0, 0, 0, &lk);
  ok("cond: data bar, the smallest a tenth", lk.bar == 100);
  cond_rule_init(&r); cond_parse_areas(b, "A1:A5", &r); r.type = CF_SCALE; r.bar[0] = 0xFF0000; r.bar[2] = 0x0000FF;
  book_cond_add(b, 0, &r, 1); cond_rule_free(&r);
  cond_look(b, 0, 0, 0, &lk);
  ok("cond: colour scale, the lowest", lk.fill == (FILL_ON | 0xFF0000));
  /* saved and read back */
  cond_rule_init(&r); cond_parse_areas(b, "A1:A8 C1:C3", &r);
  r.type = CF_CELL; r.op = CO_GE; r.f1 = cond_value(b, 0, "=B1"); r.fill = FILL_ON | 0xC6EFCE; r.color = 0x006100; r.italic = 1;
  book_cond_add(b, 0, &r, 0); cond_rule_free(&r);
  cond_rule_init(&r); cond_parse_areas(b, "A1:A8", &r); r.type = CF_TEXT; r.text = str_dup("a \"b\""); r.fill = FILL_ON | 0x123456;
  book_cond_add(b, 0, &r, -1); cond_rule_free(&r);
  x = xlsx_save(b, &len);
  d = x ? xlsx_load(x, len, err, sizeof err) : NULL;
  ok("cond: saved and read back", d && d->sh[0]->ncfs == 4 && d->sh[0]->cfs[0].type == CF_CELL && d->sh[0]->cfs[0].op == CO_GE &&
     d->sh[0]->cfs[0].nareas == 2 && d->sh[0]->cfs[0].fill == (FILL_ON | 0xC6EFCE) && d->sh[0]->cfs[0].color == 0x006100 && d->sh[0]->cfs[0].italic &&
     d->sh[0]->cfs[1].type == CF_BAR && d->sh[0]->cfs[2].type == CF_SCALE && d->sh[0]->cfs[2].bar[0] == 0xFF0000 &&
     d->sh[0]->cfs[3].type == CF_TEXT && d->sh[0]->cfs[3].text && !strcmp(d->sh[0]->cfs[3].text, "a \"b\""));
  if (d) { cond_value_text(d, 0, d->sh[0]->cfs[0].f1, t, sizeof t); ok("cond: its formula read back", !strcmp(t, "=B1")); book_free(d); }
  free(x);
  /* rows inserted above move the rules' cells and formulas */
  book_insert_rows(b, 0, 0, 2);
  cond_value_text(b, 0, b->sh[0]->cfs[0].f1, t, sizeof t);
  ok("cond: follows inserted rows", b->sh[0]->cfs[0].areas[0].r1 == 2 && b->sh[0]->cfs[0].areas[0].r2 == 9 && !strcmp(t, "=B3"));
  /* clearing part of a rule's cells */
  book_cond_clear(b, 0, &(Rect){ 4, 0, 5, 0 });
  ok("cond: clearing the middle splits the area", b->sh[0]->cfs[0].nareas == 3);
  book_free(b);
}

static void notes(void)
{
  Book *b = book_new(), *d;
  const Note *n;
  u8 *x; size_t len; char err[100];
  SortKey k;
  put(b, "A1", "Name"); put(b, "A2", "Zed"); put(b, "A3", "Amy");
  ok("note: written", book_set_note(b, 0, 1, 0, "Joined in May", "Pat") && (n = sheet_note(b->sh[0], 1, 0)) && !strcmp(n->text, "Joined in May") && !strcmp(n->author, "Pat"));
  ok("note: none elsewhere", !sheet_note(b->sh[0], 2, 0));
  book_set_note(b, 0, 1, 0, "Joined in June", "Sam");
  ok("note: changed", (n = sheet_note(b->sh[0], 1, 0)) && !strcmp(n->text, "Joined in June") && !strcmp(n->author, "Sam") && b->sh[0]->nnotes == 1);
  ok("note: undo", book_undo(b, NULL, NULL) && !strcmp(sheet_note(b->sh[0], 1, 0)->text, "Joined in May"));
  /* a sort takes the note with its row */
  k.col = 0; k.desc = 0;
  book_sort(b, 0, R("A1:A3"), &k, 1, 1);
  ok("note: moves with a sort", shows(b, 0, 2, 0, "Zed") && sheet_note(b->sh[0], 2, 0) && !sheet_note(b->sh[0], 1, 0));
  ok("note: sort undone in one step", book_undo(b, NULL, NULL) && sheet_note(b->sh[0], 1, 0) && shows(b, 0, 1, 0, "Zed"));
  book_insert_rows(b, 0, 0, 1);
  ok("note: moves with inserted rows", sheet_note(b->sh[0], 2, 0) != NULL);
  book_set_note(b, 0, 0, 3, "a \"quoted\" <note>\non two lines", "");
  x = xlsx_save(b, &len);
  d = x ? xlsx_load(x, len, err, sizeof err) : NULL;
  ok("note: saved and read back", d && d->sh[0]->nnotes == 2 && (n = sheet_note(d->sh[0], 2, 0)) && !strcmp(n->text, "Joined in May") && !strcmp(n->author, "Pat") &&
     (n = sheet_note(d->sh[0], 0, 3)) && !strcmp(n->text, "a \"quoted\" <note>\non two lines"));
  if (d) book_free(d);
  free(x);
  book_delete_rows(b, 0, 2, 1);
  ok("note: gone with its row", !sheet_note(b->sh[0], 2, 0) && b->sh[0]->nnotes == 1);
  book_clear(b, 0, R("D1"), CLR_CONTENTS);
  ok("note: Clear Contents keeps it", b->sh[0]->nnotes == 1);
  book_clear(b, 0, R("D1"), CLR_ALL);
  ok("note: Clear All takes it", b->sh[0]->nnotes == 0);
  book_set_note(b, 0, 0, 0, "x", "y");
  book_set_note(b, 0, 0, 0, "", NULL);
  ok("note: taken off", b->sh[0]->nnotes == 0);
  book_free(b);
}

static void links(void)
{
  Book *b = book_new(), *d;
  const Link *l;
  u8 *x; size_t len; char err[100], t[300];
  book_add_sheet(b, "Data", -1);
  put(b, "A1", "Amiga");
  ok("link: set", book_set_link(b, 0, 0, 0, "https://www.amiga.org/?a=1&b=2", "Go there") && (l = sheet_link(b->sh[0], 0, 0)) && !strcmp(l->tip, "Go there"));
  book_set_link(b, 0, 1, 0, "#Data!B2", "");
  put(b, "A3", "=HYPERLINK(\"mailto:\"&\"pat@example.com\",\"Mail Pat\")");
  ok("link: HYPERLINK shows its name", shows(b, 0, 2, 0, "Mail Pat"));
  ok("link: HYPERLINK's address", book_cell_link(b, 0, 2, 0, t, sizeof t) && !strcmp(t, "mailto:pat@example.com"));
  ok("link: a cell's link", book_cell_link(b, 0, 0, 0, t, sizeof t) && !strcmp(t, "https://www.amiga.org/?a=1&b=2"));
  ok("link: none", !book_cell_link(b, 0, 5, 5, t, sizeof t));
  x = xlsx_save(b, &len);
  d = x ? xlsx_load(x, len, err, sizeof err) : NULL;
  ok("link: saved and read back", d && d->sh[0]->nlinks == 2 && (l = sheet_link(d->sh[0], 0, 0)) && !strcmp(l->target, "https://www.amiga.org/?a=1&b=2") &&
     !strcmp(l->tip, "Go there") && (l = sheet_link(d->sh[0], 1, 0)) && !strcmp(l->target, "#Data!B2"));
  if (d) book_free(d);
  free(x);
  book_insert_cols(b, 0, 0, 1);
  ok("link: moves with inserted columns", sheet_link(b->sh[0], 0, 1) && !sheet_link(b->sh[0], 0, 0));
  book_set_link(b, 0, 0, 1, NULL, NULL);
  ok("link: removed", b->sh[0]->nlinks == 1);
  book_free(b);
}

/* financial and database functions (func_fin.c): Excel's documented examples, and checks
   LibreOffice can't make (computed criteria, odd first periods) */
static void fin_db(void)
{
  Book *b = book_new();
  ok("ODDFPRICE Excel example", near(evn(b, "ODDFPRICE(DATE(2008,11,11),DATE(2021,3,1),DATE(2008,10,15),DATE(2009,3,1),0.0785,0.0625,100,2,1)"), 113.597717474079));
  ok("ODDFYIELD Excel example", fabs(evn(b, "ODDFYIELD(DATE(2008,11,11),DATE(2021,3,1),DATE(2008,10,15),DATE(2009,3,1),0.0575,84.5,100,2,0)") - 0.0772) < 5e-5);
  ok("ODDFPRICE of a regular first period is PRICE", near(evn(b, "ODDFPRICE(DATE(2008,11,11),DATE(2021,3,1),DATE(2008,9,1),DATE(2009,3,1),0.0785,0.0625,100,2,1)"),
     evn(b, "PRICE(DATE(2008,11,11),DATE(2021,3,1),0.0785,0.0625,100,2,1)")));
  ok("ODDFPRICE of a regular first period is PRICE (30/360)", near(evn(b, "ODDFPRICE(DATE(2008,11,11),DATE(2021,3,1),DATE(2008,9,1),DATE(2008,12,1),0.0785,0.0625,100,4,0)"),
     evn(b, "PRICE(DATE(2008,11,11),DATE(2021,3,1),0.0785,0.0625,100,4,0)")));
  /* a long first period of two whole quasi-periods, settlement in the first: the same cash flows as
     a regular bond but for the coupon of the first quasi-period, paid a period later */
  ok("ODDFPRICE long first period", near(evn(b, "ODDFPRICE(DATE(2008,5,1),DATE(2021,3,1),DATE(2008,3,1),DATE(2009,3,1),0.08,0.06,100,2,0)"),
     evn(b, "PRICE(DATE(2008,5,1),DATE(2021,3,1),0.08,0.06,100,2,0)-4/1.03^(120/180)+4/1.03^(1+120/180)")));
  ok("ODDFYIELD inverts ODDFPRICE", near(evn(b, "ODDFYIELD(DATE(2008,5,1),DATE(2021,3,1),DATE(2008,3,1),DATE(2009,3,1),0.08,"
     "ODDFPRICE(DATE(2008,5,1),DATE(2021,3,1),DATE(2008,3,1),DATE(2009,3,1),0.08,0.06,100,2,0),100,2,0)"), 0.06));
  ok("ODDLPRICE Excel example", fabs(evn(b, "ODDLPRICE(DATE(2008,2,7),DATE(2008,6,15),DATE(2007,10,15),0.0375,0.0405,100,2,0)") - 99.87829) < 5e-6);
  ok("ODDLYIELD Excel example", fabs(evn(b, "ODDLYIELD(DATE(2008,4,20),DATE(2008,6,15),DATE(2007,12,24),0.0375,99.875,100,2,0)") - 0.045192) < 5e-7);
  ok("DURATION Excel example", fabs(evn(b, "DURATION(DATE(2018,7,1),DATE(2048,1,1),0.08,0.09,2,1)") - 10.9191453) < 5e-8);
  ok("MIRR Excel example", fabs(evn(b, "MIRR({-120000,39000,30000,21000,37000,46000},0.1,0.12)") - 0.126094) < 5e-7);
  ok("XIRR Excel example", fabs(evn(b, "XIRR({-10000,2750,4250,3250,2750},{39448,39508,39751,39859,39904},0.1)") - 0.373362535) < 5e-9);
  ok("PDURATION is _xlfn.", (func_table[func_lookup("PDURATION")].flags & FF_XLFN) != 0);
  /* database functions: Excel's example table */
  put(b, "A5", "Tree"); put(b, "B5", "Height"); put(b, "C5", "Age"); put(b, "D5", "Yield"); put(b, "E5", "Profit");
  put(b, "A6", "Apple"); put(b, "B6", "18"); put(b, "C6", "20"); put(b, "D6", "14"); put(b, "E6", "105");
  put(b, "A7", "Pear"); put(b, "B7", "12"); put(b, "C7", "12"); put(b, "D7", "10"); put(b, "E7", "96");
  put(b, "A8", "Cherry"); put(b, "B8", "13"); put(b, "C8", "14"); put(b, "D8", "9"); put(b, "E8", "105");
  put(b, "A9", "Apple"); put(b, "B9", "14"); put(b, "C9", "15"); put(b, "D9", "10"); put(b, "E9", "75");
  put(b, "A10", "Pear"); put(b, "B10", "9"); put(b, "C10", "8"); put(b, "D10", "8"); put(b, "E10", "76.8");
  put(b, "A11", "Apple"); put(b, "B11", "8"); put(b, "C11", "9"); put(b, "D11", "6"); put(b, "E11", "45");
  put(b, "A1", "Tree"); put(b, "B1", "Height"); put(b, "C1", "Age"); put(b, "D1", "Yield"); put(b, "E1", "Profit"); put(b, "F1", "Height");
  put(b, "A2", "=\"=Apple\""); put(b, "B2", ">10"); put(b, "F2", "<16"); put(b, "A3", "=\"=Pear\"");
  ok("DCOUNT Excel example", evn(b, "DCOUNT(A5:E11,\"Age\",A1:F2)") == 1);
  ok("DCOUNT with the field left out counts records", evn(b, "DCOUNT(A5:E11,,A1:F2)") == 1 && evn(b, "DCOUNTA(A5:E11,,A1:A3)") == 5);
  ok("DMAX Excel example", evn(b, "DMAX(A5:E11,\"Profit\",A1:F3)") == 96);
  ok("DSUM Excel example", evn(b, "DSUM(A5:E11,\"Profit\",A1:A2)") == 225 && evn(b, "DSUM(A5:E11,\"Profit\",A1:F2)") == 75);
  ok("DAVERAGE by field number", evn(b, "DAVERAGE(A5:E11,3,A5:E11)") == 13);
  ok("DSTDEV Excel example", fabs(evn(b, "DSTDEV(A5:E11,\"Yield\",A1:A3)") - 2.96647939483827) < 1e-12);
  put(b, "G1", "Tree"); put(b, "G2", "Plum");
  ok("DGET: more than one match is #NUM!, none #VALUE!", evn(b, "DGET(A5:E11,\"Yield\",A1:A3)") == -1000 - E_NUM && evn(b, "DGET(A5:E11,\"Yield\",G1:G2)") == -1000 - E_VALUE);
  put(b, "G2", "Ch");
  ok("text criteria match the beginning", evn(b, "DGET(A5:E11,\"Yield\",G1:G2)") == 9);
  ok("DGET of a missing field is #VALUE!", evn(b, "DGET(A5:E11,\"Price\",G1:G2)") == -1000 - E_VALUE);
  /* computed criteria: a formula under a heading that isn't a field, about the first record */
  put(b, "H1", "Big"); put(b, "H2", "=E6>AVERAGE($E$6:$E$11)");
  ok("computed criteria", near(evn(b, "DSUM(A5:E11,\"Profit\",H1:H2)"), 306) && evn(b, "DCOUNT(A5:E11,\"Age\",H1:H2)") == 3);
  put(b, "I1", "Tree"); put(b, "I2", "Apple");
  ok("computed criteria AND a field", near(evn(b, "DSUM(A5:E11,\"Profit\",H1:I2)"), 105));
  book_free(b);
}
#include "drawtest.c"

static void templates(void)
{
  Book *b;
  int t;
  for (t = 0; t < TPL_N; t++) { b = book_template(t); ok("template: made", b != NULL && !book_can_undo(b) && !b->dirty); if (b) book_free(b); }
  b = book_template(TPL_BUDGET);
  ok("template: budget totals", num_at(b, "B10") == 1890 && fabs(num_at(b, "D10") - (1890 - 1894.15)) < 1e-9 && shows(b, 0, 9, 1, "1,890.00"));
  book_free(b);
  b = book_template(TPL_INVOICE);
  ok("template: invoice total", fabs(num_at(b, "E16") - 594.6) < 1e-9);
  book_free(b);
  b = book_template(TPL_EXPENSES);
  ok("template: expenses by category", fabs(num_at(b, "G2") - 37.6) < 1e-9 && b->sh[0]->freezer == 1);
  book_free(b);
  b = book_template(TPL_BUDGET);
  ok("duplicate sheet", book_duplicate_sheet(b, 0) == 1 && b->nsh == 2 && !strcmp(b->sh[1]->name, "Budget (2)") && cell_value(b, 1, 9, 1)->n == 1890 && book_undo(b, NULL, NULL) && b->nsh == 1);
  book_free(b);
  b = book_new();
  put(b, "A1", "5"); put(b, "A2", "7"); put(b, "B1", "=Total*2");
  ok("names: defined", book_define_name(b, "Total", "SUM(Sheet1!$A$1:$A$2)", SHEET_SELF) && num_at(b, "B1") == 24);
  ok("names: bad ones refused", !book_define_name(b, "A1", "1", SHEET_SELF) && !book_define_name(b, "2x", "1", SHEET_SELF) && !book_define_name(b, "ok", "1+", SHEET_SELF));
  ok("names: changed", book_define_name(b, "total", "100", SHEET_SELF) && b->nnames == 1 && num_at(b, "B1") == 200);
  ok("names: deleted", book_delete_name(b, 0) && b->nnames == 0 && num_at(b, "B1") == -1000 - E_NAME);
  book_free(b);
  b = book_template(TPL_GRADES);
  ok("template: grades", text_at(b, "F4", "=IF(E4>=$I$2,\"Pass\",\"Retake\")") && cell_value(b, 0, 3, 5)->t == V_STR && !strcmp(cell_value(b, 0, 3, 5)->s, "Retake"));
  book_free(b);
}


/* ------------------------------------------------------------------ the Data tab's tools */
static Rect rc(Book *b, const char *ref) { u16 sh; Rect r; u8 fl; parse_ref(b, ref, &sh, &r, &fl); return r; }
static Book *reload(Book *b)
{
  size_t len;
  u8 *x = xlsx_save(b, &len);
  char err[100];
  Book *d = x ? xlsx_load(x, len, err, sizeof err) : NULL;
  free(x);
  return d;
}
static void validation(void)
{
  Book *b = book_new(), *d;
  DVRule r;
  Rect a;
  const DVRule *got;
  char **items;
  int n, i;
  i32 rows[10], cols[10];
  /* a whole number between 1 and 10 on A1:A5 */
  dv_rule_init(&r);
  r.type = DV_WHOLE; r.op = CO_BETWEEN;
  r.f1 = dv_value(b, 0, "1", 0); r.f2 = dv_value(b, 0, "10", 0);
  free(r.errmsg); r.errmsg = str_dup("1 to 10, please");
  a = rc(b, "A1:A5");
  ok("dv: set", book_dv_set(b, 0, &a, 1, &r) && b->sh[0]->ndvs == 1);
  dv_rule_free(&r);
  ok("dv: whole in range", dv_check_text(b, 0, 2, 0, "7", &got) && got);
  ok("dv: whole out of range", !dv_check_text(b, 0, 2, 0, "11", &got) && got && !strcmp(got->errmsg, "1 to 10, please"));
  ok("dv: not whole", !dv_check_text(b, 0, 2, 0, "2.5", NULL));
  ok("dv: text refused", !dv_check_text(b, 0, 2, 0, "abc", NULL));
  ok("dv: blank passes", dv_check_text(b, 0, 2, 0, "", NULL));
  ok("dv: no rule elsewhere", dv_check_text(b, 0, 2, 1, "abc", &got) && !got);
  /* a list typed */
  dv_rule_init(&r);
  r.type = DV_LIST; r.f1 = dv_value(b, 0, "Open, In progress,Closed", 1);
  a = rc(b, "C1:C3");
  book_dv_set(b, 0, &a, 1, &r);
  dv_rule_free(&r);
  got = dv_at(b->sh[0], 1, 2);
  n = dv_list_items(b, 0, got, 1, 2, &items);
  ok("dv: list items", n == 3 && !strcmp(items[1], "In progress") && !strcmp(items[2], "Closed"));
  for (i = 0; i < n; i++) free(items[i]);
  free(items);
  ok("dv: list match ignores case", dv_check_text(b, 0, 0, 2, "closed", NULL) && !dv_check_text(b, 0, 0, 2, "Shut", NULL));
  {
    char t[100];
    dv_value_text(b, 0, got->f1, 1, t, sizeof t);
    ok("dv: list text back", !strcmp(t, "Open, In progress,Closed"));
  }
  /* a list from a range, and a custom rule seeing the new value */
  put(b, "E1", "red"); put(b, "E2", "green"); put(b, "E3", "blue");
  dv_rule_init(&r);
  r.type = DV_LIST; r.f1 = dv_value(b, 0, "=$E$1:$E$3", 1);
  a = rc(b, "D1:D9");
  book_dv_set(b, 0, &a, 1, &r);
  dv_rule_free(&r);
  ok("dv: list from a range", dv_check_text(b, 0, 4, 3, "Green", NULL) && !dv_check_text(b, 0, 4, 3, "pink", NULL));
  dv_rule_init(&r);
  r.type = DV_CUSTOM; r.f1 = dv_value(b, 0, "=F1<>G1", 0);
  a = rc(b, "F1:F4");
  book_dv_set(b, 0, &a, 1, &r);
  dv_rule_free(&r);
  put(b, "G3", "5");
  ok("dv: custom, relative", !dv_check_text(b, 0, 2, 5, "5", NULL) && dv_check_text(b, 0, 2, 5, "6", NULL) && dv_check_text(b, 0, 1, 5, "5", NULL));
  /* text length, date, decimal */
  dv_rule_init(&r);
  r.type = DV_TEXTLEN; r.op = CO_LE; r.f1 = dv_value(b, 0, "3", 0);
  a = rc(b, "H1"); book_dv_set(b, 0, &a, 1, &r); dv_rule_free(&r);
  ok("dv: text length", dv_check_text(b, 0, 0, 7, "abc", NULL) && !dv_check_text(b, 0, 0, 7, "abcd", NULL));
  dv_rule_init(&r);
  r.type = DV_DATE; r.op = CO_GE; r.f1 = dv_value(b, 0, "2026-01-01", 0);
  a = rc(b, "I1"); book_dv_set(b, 0, &a, 1, &r); dv_rule_free(&r);
  ok("dv: date", dv_check_text(b, 0, 0, 8, "2026-03-04", NULL) && !dv_check_text(b, 0, 0, 8, "2025-12-31", NULL));
  /* invalid cells circled */
  put(b, "A2", "50"); put(b, "A3", "5"); put(b, "C2", "Maybe");
  n = dv_invalid_cells(b, 0, rows, cols, 10);
  ok("dv: circle invalid", n == 2 && rows[0] == 1 && cols[0] == 0 && rows[1] == 1 && cols[1] == 2);
  /* clearing part of a rule */
  a = rc(b, "A2:A3");
  book_dv_set(b, 0, &a, 1, NULL);
  ok("dv: cleared cells", !dv_at(b->sh[0], 1, 0) && dv_at(b->sh[0], 0, 0) && dv_at(b->sh[0], 4, 0));
  ok("dv: undo", book_undo(b, NULL, NULL) && dv_at(b->sh[0], 1, 0));
  /* rows inserted: the cells follow */
  book_insert_rows(b, 0, 0, 2);
  ok("dv: rows inserted", dv_at(b->sh[0], 2, 0) && !dv_at(b->sh[0], 0, 0) && dv_check_text(b, 0, 3, 7, "abc", NULL) == 1);
  book_undo(b, NULL, NULL);
  /* saved and read back */
  d = reload(b);
  ok("dv: xlsx round trip", d && d->sh[0]->ndvs == b->sh[0]->ndvs && (got = dv_at(d->sh[0], 2, 0)) && got->type == DV_WHOLE && !strcmp(got->errmsg, "1 to 10, please") &&
     !dv_check_text(d, 0, 2, 0, "11", NULL) && dv_check_text(d, 0, 2, 0, "10", NULL) && dv_check_text(d, 0, 2, 2, "Open", NULL) && !dv_check_text(d, 0, 2, 5, "5", NULL));
  book_free(d);
  book_free(b);
}
static void outline(void)
{
  Book *b = book_new(), *d;
  OGroup g[16];
  int n, i;
  /* rows 2-5 grouped, 3-4 inside */
  ok("outline: group", book_group(b, 0, 1, 1, 4, 1) && book_group(b, 0, 1, 2, 3, 1));
  n = outline_groups(b->sh[0], 1, g, 16);
  ok("outline: groups", n == 2 && g[0].level == 2 && g[0].start == 2 && g[0].end == 3 && g[1].level == 1 && g[1].start == 1 && g[1].end == 4 && ogroup_summary(&g[1]) == 5);
  ok("outline: max level", outline_max_level(b->sh[0], 1) == 2 && outline_max_level(b->sh[0], 0) == 0);
  ok("outline: hide detail", book_detail(b, 0, 1, 2, 2, 1, 0) && !sheet_row_height(b->sh[0], 2) && sheet_row_height(b->sh[0], 1) && b->sh[0]->rows[4]->collapsed);
  ok("outline: hide outer", book_detail(b, 0, 1, 1, 1, 1, 0) && !sheet_row_height(b->sh[0], 1) && b->sh[0]->rows[5]->collapsed);
  ok("outline: show outer keeps inner", book_detail(b, 0, 1, 5, 5, 0, 0) && sheet_row_height(b->sh[0], 1) && !sheet_row_height(b->sh[0], 2) && !b->sh[0]->rows[5]->collapsed);
  ok("outline: levels", book_show_levels(b, 0, 1, 3) && sheet_row_height(b->sh[0], 2) && book_show_levels(b, 0, 1, 1) && !sheet_row_height(b->sh[0], 1));
  book_show_levels(b, 0, 1, 3);
  /* columns, and summaries above */
  book_group(b, 0, 0, 3, 5, 1);
  book_outline_settings(b, 0, 0, 1);
  n = outline_groups(b->sh[0], 1, g, 16);
  ok("outline: summary above", b->sh[0]->sumabove && n == 2 && ogroup_summary(&g[1]) == 0);
  d = reload(b);
  ok("outline: xlsx round trip", d && d->sh[0]->sumabove && !d->sh[0]->sumleft && outline_level(d->sh[0], 1, 3) == 2 && outline_level(d->sh[0], 0, 4) == 1 && outline_groups(d->sh[0], 1, g, 16) == 2);
  book_free(d);
  ok("outline: ungroup", book_group(b, 0, 1, 1, 4, 0) && outline_max_level(b->sh[0], 1) == 1 && book_outline_clear(b, 0) && !outline_max_level(b->sh[0], 1) && !outline_max_level(b->sh[0], 0));
  book_free(b);
  /* Subtotal */
  b = book_new();
  put(b, "A1", "Region"); put(b, "B1", "Sales");
  put(b, "A2", "East"); put(b, "B2", "10"); put(b, "A3", "East"); put(b, "B3", "20");
  put(b, "A4", "West"); put(b, "B4", "5"); put(b, "A5", "North"); put(b, "B5", "1"); put(b, "A6", "North"); put(b, "B6", "2");
  {
    i32 cols[1] = { 1 };
    n = book_subtotal(b, 0, rc(b, "A1:B6"), 0, 9, cols, 1, 1, 1);
  }
  ok("subtotal: rows added", n == 4 && sheet_last_row(b->sh[0]) == 9);
  ok("subtotal: labels and formulas", text_at(b, "A4", "East Total") && text_at(b, "B4", "=SUBTOTAL(9,B2:B3)") && text_at(b, "A6", "West Total") &&
     text_at(b, "B9", "=SUBTOTAL(9,B7:B8)") && text_at(b, "A10", "Grand Total") && text_at(b, "B10", "=SUBTOTAL(9,B2:B9)"));
  ok("subtotal: sums (nested totals skipped)", num_at(b, "B4") == 30 && num_at(b, "B10") == 38);
  ok("subtotal: outline", outline_level(b->sh[0], 1, 1) == 2 && outline_level(b->sh[0], 1, 3) == 1 && outline_level(b->sh[0], 1, 9) == 0 && b->fonts[b->styles[sheet_cell_style(b->sh[0], 3, 1)].font].bold);
  ok("subtotal: replace", book_subtotal(b, 0, rc(b, "A1:B10"), 0, 9, (i32[]){ 1 }, 1, 1, 1) == 4 && sheet_last_row(b->sh[0]) == 9);
  ok("subtotal: remove all", book_subtotal_remove(b, 0, rc(b, "A1:B10")) == 4 && sheet_last_row(b->sh[0]) == 5 && !outline_max_level(b->sh[0], 1) && text_at(b, "A4", "West"));
  ok("subtotal: undo", book_undo(b, NULL, NULL) && sheet_last_row(b->sh[0]) == 9);
  for (i = 0; i < 2; i++) book_undo(b, NULL, NULL);
  ok("subtotal: undone", sheet_last_row(b->sh[0]) == 5 && !outline_max_level(b->sh[0], 1));
  book_free(b);
}
#define SHAKEY "f3vNikyWE04Elo3PTi3Sqzxgur6tdkO1RQfLaWWzuxCR8ld6N2WHZB0r9SDVDBa1RAKEtSZx/PAd82iYEEdn4g=="
static void protection(void)
{
  Book *b = book_new(), *d;
  Style st;
  int k;
  ok("protect: hash", prot_hash("password") == 0x83AF && prot_hash("") == 0);
  put(b, "A1", "=1+2"); put(b, "B1", "5");
  /* B1 unlocked, A1's formula hidden */
  st = b->styles[0]; st.locked = 0;
  k = book_style(b, &st); book_set_style(b, 0, 0, 1, k);
  st = b->styles[0]; st.hidden = 1;
  k = book_style(b, &st); book_set_style(b, 0, 0, 0, k);
  ok("protect: off", !prot_locked(b, 0, 0, 0) && !prot_hidden(b, 0, 0, 0));
  ok("protect: on", book_protect_sheet(b, 0, "secret", PA_DEFAULT | PA_FMTCELLS) && prot_locked(b, 0, 0, 0) && !prot_locked(b, 0, 0, 1) && prot_locked(b, 0, 5, 5) && prot_hidden(b, 0, 0, 0));
  ok("protect: areas", prot_area_locked(b, 0, rc(b, "A1:B1")) && !prot_area_locked(b, 0, rc(b, "B1")) && prot_area_locked(b, 0, rc(b, "C:C")));
  d = reload(b);
  ok("protect: xlsx round trip", d && d->sh[0]->protect && d->sh[0]->pwhash == prot_hash("secret") && d->sh[0]->protallow == (PA_DEFAULT | PA_FMTCELLS) && prot_hidden(d, 0, 0, 0) && !prot_locked(d, 0, 0, 1));
  book_free(d);
  ok("protect: wrong password", !book_unprotect_sheet(b, 0, "Secret") && b->sh[0]->protect);
  ok("protect: right password", book_unprotect_sheet(b, 0, "secret") && !b->sh[0]->protect && !prot_locked(b, 0, 0, 0));
  ok("protect: workbook", book_protect_book(b, "") && b->lockstruct && (d = reload(b)) && d->lockstruct && book_unprotect_book(d, "x"));
  book_free(d);
  book_unprotect_book(b, "");
  /* a newer file's SHA-512 key */
  strcpy(b->sh[0]->protkey, "SHA-512:2:c2FsdA==:");
  b->sh[0]->protect = 1;
  ok("protect: bad key refuses", !book_unprotect_sheet(b, 0, "x"));
  {
    /* made with Python: hashlib, salt "salt", 2 spins, password "pw" */
    strcpy(b->sh[0]->protkey, "SHA-512:2:c2FsdA==:" SHAKEY);
    ok("protect: SHA-512 key", !book_unprotect_sheet(b, 0, "pX") && book_unprotect_sheet(b, 0, "pw"));
  }
  book_free(b);
}
static void flashfill(void)
{
  static const char *s1[] = { "John Smith" }, *d1[] = { "Smith, John" };
  static const char *s2[] = { "jane doe" }, *d2[] = { "JD" };
  static const char *s3[] = { "alice@example.com" }, *d3[] = { "alice" };
  static const char *s4[] = { "ABC-123-X" }, *d4[] = { "123" };
  static const char *s5[] = { "ada lovelace" }, *d5[] = { "Ada" };
  char *g;
  Book *b;
  ok("flash: swap names", (g = flash_guess(s1, d1, 1, "Ada Lovelace")) && !strcmp(g, "Lovelace, Ada")); free(g);
  ok("flash: initials", (g = flash_guess(s2, d2, 1, "alan turing")) && !strcmp(g, "AT")); free(g);
  ok("flash: before @", (g = flash_guess(s3, d3, 1, "bob@example.com")) && !strcmp(g, "bob")); free(g);
  ok("flash: middle part", (g = flash_guess(s4, d4, 1, "QRS-987-Y")) && !strcmp(g, "987")); free(g);
  ok("flash: proper case", (g = flash_guess(s5, d5, 1, "grace hopper")) && !strcmp(g, "Grace")); free(g);
  b = book_new();
  put(b, "A1", "Ada Lovelace"); put(b, "A2", "Alan Turing"); put(b, "A3", "Grace Hopper"); put(b, "A4", "Linus Torvalds");
  put(b, "B1", "LOVELACE");
  ok("flash: fill a column", book_flash_fill(b, 0, 1, 1) == 3 && text_at(b, "B2", "TURING") && text_at(b, "B4", "TORVALDS"));
  ok("flash: undo in one", book_undo(b, NULL, NULL) && !sheet_cell(b->sh[0], 1, 1));
  book_free(b);
}
static void datatools(void)
{
  TTCOpts o;
  char **p;
  int n, i;
  Book *b;
  memset(&o, 0, sizeof o);
  o.delims = TTC_COMMA; o.quote = '"';
  n = ttc_split("a,\"b,c\",,d", &o, &p);
  ok("ttc: delimited with quotes", n == 4 && !strcmp(p[1], "b,c") && !strcmp(p[2], "") && !strcmp(p[3], "d"));
  for (i = 0; i < n; i++) free(p[i]);
  free(p);
  o.delims = TTC_SPACE | TTC_SEMI; o.consecutive = 1;
  n = ttc_split("x  y;z", &o, &p);
  ok("ttc: consecutive", n == 3 && !strcmp(p[1], "y"));
  for (i = 0; i < n; i++) free(p[i]);
  free(p);
  memset(&o, 0, sizeof o);
  o.fixed = 1; o.breaks[0] = 3; o.breaks[1] = 5; o.nbreaks = 2;
  n = ttc_split("abc12 xyz", &o, &p);
  ok("ttc: fixed width", n == 3 && !strcmp(p[0], "abc") && !strcmp(p[1], "12") && !strcmp(p[2], "xyz"));
  for (i = 0; i < n; i++) free(p[i]);
  free(p);
  b = book_new();
  put(b, "A1", "Name;Age"); put(b, "A2", "Ann;31"); put(b, "A3", "Bob;27");
  memset(&o, 0, sizeof o); o.delims = TTC_SEMI;
  ok("ttc: book", book_text_to_columns(b, 0, rc(b, "A1:A3"), &o, 0, 0) == 2 && text_at(b, "A2", "Ann") && num_at(b, "B3") == 27);
  ok("ttc: undo", book_undo(b, NULL, NULL) && text_at(b, "A2", "Ann;31") && !sheet_cell(b->sh[0], 1, 1));
  book_free(b);
  /* Advanced Filter */
  b = book_new();
  put(b, "A1", "Item"); put(b, "B1", "Qty");
  put(b, "A2", "apple"); put(b, "B2", "10"); put(b, "A3", "banana"); put(b, "B3", "3"); put(b, "A4", "apricot"); put(b, "B4", "7"); put(b, "A5", "apple"); put(b, "B5", "10");
  put(b, "D1", "Item"); put(b, "E1", "Qty"); put(b, "D2", "ap"); put(b, "E2", ">5");
  {
    Rect cr = rc(b, "D1:E2");
    ok("advfilter: in place", book_advanced_filter(b, 0, rc(b, "A1:B5"), &cr, -1, 0, 0, 0) == 3 && !sheet_row_height(b->sh[0], 2) && sheet_row_height(b->sh[0], 3));
    ok("advfilter: show all", book_filter_show_all(b, 0) == 1 && sheet_row_height(b->sh[0], 2));
    ok("advfilter: copy unique", book_advanced_filter(b, 0, rc(b, "A1:B5"), &cr, 0, 0, 6, 1) == 2 && text_at(b, "G1", "Item") && text_at(b, "G3", "apricot") && !sheet_cell(b->sh[0], 3, 6));
  }
  {
    /* computed criterion and OR rows */
    Rect cr;
    put(b, "D4", "Item"); put(b, "D5", "'=banana"); put(b, "D6", "apricot");
    cr = rc(b, "D4:D6");
    ok("advfilter: or rows", book_advanced_filter(b, 0, rc(b, "A1:B5"), &cr, -1, 0, 0, 0) == 2);
    book_filter_show_all(b, 0);
    put(b, "F8", ""); put(b, "F9", "=B2>8");
    cr = rc(b, "F8:F9");
    ok("advfilter: computed", book_advanced_filter(b, 0, rc(b, "A1:B5"), &cr, -1, 0, 0, 0) == 2);
  }
  book_free(b);
  /* Consolidate by labels */
  b = book_new();
  book_add_sheet(b, "Q2", -1);
  put(b, "A1", "Fruit"); put(b, "B1", "Jan"); put(b, "A2", "Apple"); put(b, "B2", "1"); put(b, "A3", "Pear"); put(b, "B3", "2");
  put(b, "Q2!A1", "Fruit"); put(b, "Q2!B1", "Jan"); put(b, "Q2!C1", "Feb"); put(b, "Q2!A2", "pear"); put(b, "Q2!B2", "10"); put(b, "Q2!C2", "4");
  {
    int sh[2] = { 0, 1 };
    Rect src[2];
    src[0] = rc(b, "A1:B3"); src[1] = rc(b, "A1:C2");
    ok("consolidate: by labels", book_consolidate(b, 0, 5, 0, sh, src, 2, 9, 1, 1) == 4 && text_at(b, "B6", "Jan") && text_at(b, "C6", "Feb") && text_at(b, "A7", "Apple") &&
       num_at(b, "B8") == 12 && num_at(b, "C8") == 4 && num_at(b, "B7") == 1 && !sheet_cell(b->sh[0], 6, 2));
  }
  book_free(b);
}
static void whatif(void)
{
  Book *b = book_new(), *d;
  double x;
  const char *why;
  int r;
  put(b, "A1", "3"); put(b, "B1", "=A1*A1+1");
  r = book_goal_seek(b, 0, 0, 1, 26, 0, 0, 0, &x, &why);
  ok("goal seek: found", r == 1 && fabs(x - 5) < 0.001 && fabs(num_at(b, "B1") - 26) <= 0.001);
  ok("goal seek: undo", book_undo(b, NULL, NULL) && num_at(b, "A1") == 3);
  ok("goal seek: needs a formula", book_goal_seek(b, 0, 0, 0, 1, 0, 0, 1, &x, &why) == -1 && why);
  /* one-variable data table: rates down, a formula across */
  put(b, "C1", "100"); put(b, "C2", "=C1*2");
  put(b, "E2", "1"); put(b, "E3", "2"); put(b, "E4", "3"); put(b, "F1", "=C2");
  ok("data table: column input", book_data_table(b, 0, rc(b, "E1:F4"), -1, -1, 0, 2) == 3 && num_at(b, "F2") == 2 && num_at(b, "F4") == 6 && num_at(b, "C2") == 200);
  /* two variables */
  put(b, "H1", "=A1*C1"); put(b, "I1", "1"); put(b, "J1", "2"); put(b, "H2", "10"); put(b, "H3", "20");
  ok("data table: two variables", book_data_table(b, 0, rc(b, "H1:J3"), 0, 0, 0, 2) == 4 && num_at(b, "I2") == 10 && num_at(b, "J3") == 40 && num_at(b, "H1") == 300);
  /* scenarios */
  {
    i32 rr[2] = { 0, 0 }, cc[2] = { 0, 2 };
    const char *v1[2] = { "2", "50" }, *v2[2] = { "4", "30" };
    ok("scenario: add", book_scenario_add(b, 0, "Low", rr, cc, v1, 2, "small") && book_scenario_add(b, 0, "High", rr, cc, v2, 2, "") && b->sh[0]->nscens == 2);
    ok("scenario: show", book_scenario_show(b, 0, 1) && num_at(b, "A1") == 4 && num_at(b, "C1") == 30 && num_at(b, "H1") == 120);
    d = reload(b);
    ok("scenario: xlsx", d && d->sh[0]->nscens == 2 && !strcmp(d->sh[0]->scens[0].name, "Low") && d->sh[0]->scens[0].c[1] == 2 && !strcmp(d->sh[0]->scens[0].vals[1], "50") && !strcmp(d->sh[0]->scens[0].comment, "small"));
    book_free(d);
    {
      Rect res = rc(b, "H1");
      int at = book_scenario_summary(b, 0, &res, 1);
      ok("scenario: summary", at == 1 && !strcmp(b->sh[1]->name, "Scenario Summary") && cell_value(b, 1, 7, 2)->n == 120 && cell_value(b, 1, 7, 3)->n == 100 && cell_value(b, 1, 7, 4)->n == 120 && cell_value(b, 1, 4, 3)->n == 2 && num_at(b, "H1") == 120);
    }
    ok("scenario: delete", book_scenario_delete(b, 0, 0) && b->sh[0]->nscens == 1 && !strcmp(b->sh[0]->scens[0].name, "High"));
  }
  book_free(b);
}


static int has_formula(Book *b, int sheet, const char *ref, const char *want)
{
  Rect r = rc(b, ref);
  Cell *c = sheet_cell(b->sh[sheet], r.r1, r.c1);
  Buf o;
  int ok_;
  buf_init(&o);
  if (c && c->f) formula_text(b, sheet, c->f, &o);
  ok_ = !strcmp(o.data ? o.data : "", want);
  if (!ok_) printf("   %s holds formula \"%s\" (want \"%s\")\n", ref, o.data ? o.data : "", want);
  buf_free(&o);
  return ok_;
}
static void imports(void)
{
  int k;
  for (k = 0; k < 2; k++) {
    const char *path = k ? "tests/fixtures/import.ods" : "tests/fixtures/import.xls", *what = k ? "ods" : "xls";
    size_t len;
    u8 *data = file_read(path, &len);
    char err[200], t[80];
    Book *b;
    const Font *f;
    ok(path, data != NULL);
    if (!data) continue;
    b = k ? ods_load(data, len, err, sizeof err) : xls_load(data, len, err, sizeof err);
    free(data);
    snprintf(t, sizeof t, "%s: loads", what); ok(t, b != NULL);
    if (!b) { printf("   %s\n", err); continue; }
    snprintf(t, sizeof t, "%s: sheets", what); ok(t, b->nsh == 2 && !strcmp(b->sh[0]->name, "Data") && !strcmp(b->sh[1]->name, "Other"));
    snprintf(t, sizeof t, "%s: values", what);
    ok(t, text_at(b, "A1", "Item") && num_at(b, "B4") == 10 && num_at(b, "C3") == 0.75 && text_at(b, "A13", "Caf\xc3\xa9 \xc3\xbc" "ber"));
    snprintf(t, sizeof t, "%s: formulas", what);
    ok(t, has_formula(b, 0, "D2", "B2*C2") && has_formula(b, 0, "D6", "SUM(D2:D5)") && has_formula(b, 0, "B7", "IF(D6>10,\"big\",\"small\")") &&
       has_formula(b, 0, "C7", "ROUND(AVERAGE(C2:C5),2)") && has_formula(b, 0, "D7", "COUNTIF(B2:B5,\">2\")") && has_formula(b, 0, "B8", "Other!B2*2") &&
       has_formula(b, 0, "C8", "-C2%") && has_formula(b, 0, "D8", "CONCATENATE(A2,\" & \",A3)") && has_formula(b, 0, "A9", "VLOOKUP(\"Plum\",$A$2:$C$5,3,FALSE)") &&
       has_formula(b, 0, "C9", "SUM(B:B)") && has_formula(b, 0, "D10", "EDATE(A10,1)") && has_formula(b, 1, "A3", "Data!D6+B2"));
    snprintf(t, sizeof t, "%s: results", what);
    ok(t, near(num_at(b, "D6"), 1.5 + 3 + 2 + 2.5) && shows(b, 0, 6, 1, "small") && num_at(b, "D7") == 3 && num_at(b, "B8") == 42 && num_at(b, "B9") == -1000 - E_DIV0 && num_at(b, "Other!A3") == 30);
    snprintf(t, sizeof t, "%s: formats", what);
    ok(t, shows(b, 0, 9, 0, "2026-10-27") && shows(b, 0, 9, 1, "25.0%") && shows(b, 0, 9, 2, "1,234.50") && cell_value(b, 0, 7, 0)->t == V_BOOL);
    f = &b->fonts[b->styles[sheet_cell_style(b->sh[0], 0, 0)].font];
    snprintf(t, sizeof t, "%s: fonts and fills", what);
    ok(t, f->bold && f->size == 140 && (f->color & 0xC00000) == 0xC00000 && !(f->color & 0xFFFF) && b->fonts[b->styles[sheet_cell_style(b->sh[0], 0, 1)].font].italic &&
       (b->styles[sheet_cell_style(b->sh[0], 0, 2)].fill & 0xFFFFFF) == 0xFFFF00 && b->styles[sheet_cell_style(b->sh[0], 0, 3)].halign == HA_CENTER);
    snprintf(t, sizeof t, "%s: widths, heights, merges", what);
    ok(t, sheet_col_width(b->sh[0], 0) > 120 && sheet_row_height(b->sh[0], 2) == 40 && b->sh[0]->nmerges == 1 && b->sh[0]->merges[0].c2 == 2 && b->sh[0]->merges[0].r1 == 11);
    snprintf(t, sizeof t, "%s: validation", what);
    {
      const DVRule *dl = dv_at(b->sh[0], 2, 4), *dw = dv_at(b->sh[0], 3, 1);
      ok(t, dl && dl->type == DV_LIST && dv_check_text(b, 0, 2, 4, "maybe", NULL) && !dv_check_text(b, 0, 2, 4, "Never", NULL) && !strcmp(dl->intitle, "Answer") &&
         dw && dw->type == DV_WHOLE && dw->errstyle == DVE_WARNING && !strcmp(dw->errmsg, "1 to 10") && dv_check_text(b, 0, 3, 1, "7", NULL) && !dv_check_text(b, 0, 3, 1, "11", NULL));
    }
    snprintf(t, sizeof t, "%s: outline", what);
    ok(t, outline_level(b->sh[0], 1, 1) == 1 && outline_level(b->sh[0], 1, 4) == 1 && outline_level(b->sh[0], 1, 5) == 0);
    book_free(b);
  }
  ok("xls: not one", !xls_load((const u8 *)"PK\3\4", 4, NULL, 0));
}

/* dynamic arrays, LET/LAMBDA, the array and lookup functions (core/func_array.c, calc.c) */
static int is_err(Book *b, const char *ref, int e) { return num_at(b, ref) == -1000 - e; }
static int spills(Book *b, const char *ref, const char *want)
{
  u16 sh; Rect r, a; u8 fl;
  char t[32];
  int blocked = 0, got;
  parse_ref(b, ref, &sh, &r, &fl);
  got = book_spill_range(b, 0, r.r1, r.c1, &a, &blocked);
  if (!got) { if (*want) printf("   %s: no spill (want %s)\n", ref, want); return !*want; }
  rect_text(a, t);
  if (strcmp(t, want)) printf("   %s spills over %s (want %s)\n", ref, t, want);
  return !strcmp(t, want);
}
static void dynarrays(void)
{
  Book *b = book_new(), *c;
  size_t n;
  u8 *data;
  char err[200];
  /* spilling */
  put(b, "A1", "=SEQUENCE(4)");
  ok("spill: SEQUENCE fills below", num_at(b, "A1") == 1 && num_at(b, "A4") == 4 && spills(b, "A1", "A1:A4") && spills(b, "A3", "A1:A4"));
  put(b, "C1", "=SUM(A1#)"); put(b, "C1", ""); put(b, "D10", "=SUM(A1#)");
  ok("spill: A1# is the spill range", num_at(b, "D10") == 10);
  put(b, "A3", "x");
  ok("spill: blocked is #SPILL!", is_err(b, "A1", E_SPILL) && is_err(b, "D10", E_SPILL));
  put(b, "A3", "");
  ok("spill: unblocked spills again", num_at(b, "A3") == 3 && num_at(b, "D10") == 10);
  put(b, "A1", "=SEQUENCE(2,3,10,10)");
  ok("spill: shrinks and grows", num_at(b, "A3") == -999999 && num_at(b, "C2") == 60 && spills(b, "B2", "A1:C2"));
  ok("spill: the old cells are gone", sheet_cell(b->sh[0], 3, 0) == NULL);
  put(b, "E1", "=A1:A2*{1,2}");
  ok("spill: arithmetic broadcasts", num_at(b, "E1") == 10 && num_at(b, "F2") == 80);
  put(b, "E4", "={1,2}+{10;20}");
  ok("spill: row plus column", num_at(b, "E4") == 11 && num_at(b, "F5") == 22);
  put(b, "H1", "=LEN({\"a\",\"bb\",\"ccc\"})");
  ok("spill: functions map over arrays", num_at(b, "J1") == 3);
  put(b, "H3", "=@A1:A2");
  ok("@: implicit intersection", num_at(b, "H3") == 10 || is_err(b, "H3", E_VALUE));
  put(b, "H4", "=ROW(A1:A3)");
  ok("ROW of a range spills", num_at(b, "H6") == 3);
  put(b, "K1", "=IF({1,0},\"y\",\"n\")");
  ok("IF over an array", text_at(b, "K1", "=IF({1,0},\"y\",\"n\")") && sheet_cell(b->sh[0], 0, 11) && cell_value(b, 0, 0, 11)->t == V_STR);
  book_free(b);

  b = book_new();
  /* lazy functions don't evaluate the other branch */
  ok("IF short-circuits", evn(b, "IF(TRUE,1,1/0)") == 1 && evn(b, "IF(FALSE,1/0,2)") == 2);
  ok("IFERROR short-circuits", evn(b, "IFERROR(5,1/0)") == 5 && evn(b, "IFERROR(1/0,7)") == 7);
  ok("CHOOSE evaluates only its pick", evn(b, "CHOOSE(2,1/0,3)") == 3);
  ok("SWITCH evaluates only the match", evn(b, "SWITCH(2,1,1/0,2,9)") == 9);
  ok("IFS", evn(b, "IFS(FALSE,1/0,TRUE,4)") == 4);
  /* LET and LAMBDA */
  ok("LET", evn(b, "LET(x,2,y,x*3,x+y)") == 8);
  ok("LET shadows", evn(b, "LET(x,1,LET(x,5,x)+x)") == 6);
  ok("LAMBDA called inline", evn(b, "LAMBDA(x,x+1)(2)") == 3);
  ok("LAMBDA in LET", evn(b, "LET(f,LAMBDA(a,b,a*b),f(3,4))") == 12);
  ok("LAMBDA closure", evn(b, "LET(k,10,f,LAMBDA(x,x+k),f(1))") == 11);
  ok("LAMBDA arg count", evn(b, "LAMBDA(a,b,a+b)(2)") == -1000 - E_VALUE);
  ok("ISOMITTED", evs(b, "LAMBDA(x,[y],IF(ISOMITTED(y),\"none\",x+y))(1)", "none") && evn(b, "LAMBDA(x,[y],IF(ISOMITTED(y),0,x+y))(1,2)") == 3);
  ok("MAP", evn(b, "SUM(MAP({1,2,3},LAMBDA(x,x*x)))") == 14);
  ok("REDUCE", evn(b, "REDUCE(0,{1,2,3},LAMBDA(a,v,a+v))") == 6);
  ok("SCAN", evn(b, "INDEX(SCAN(0,{1,2,3},LAMBDA(a,v,a+v)),1,3)") == 6);
  ok("BYROW", evn(b, "INDEX(BYROW({1,2;3,4},LAMBDA(r,SUM(r))),2)") == 7);
  ok("BYCOL", evn(b, "INDEX(BYCOL({1,2;3,4},LAMBDA(c,SUM(c))),1,2)") == 6);
  ok("MAKEARRAY", evn(b, "SUM(MAKEARRAY(2,3,LAMBDA(r,c,r*c)))") == 18);
  ok("function given by name", evn(b, "INDEX(BYROW({1,2;3,4},SUM),2)") == 7 && evn(b, "REDUCE(0,{1,-2},SUM)") == -1);
  ok("a lambda left in a cell is #CALC!", evn(b, "LAMBDA(x,x)") == -1000 - E_CALC);
  ok("recursion stops", evn(b, "LET(f,LAMBDA(g,n,g(g,n)),f(f,1))") == -1000 - E_NUM);
  book_define_name(b, "ADDPAIR", "LAMBDA(a,b,a+b)", SHEET_SELF);
  book_define_name(b, "FACTR", "LAMBDA(n,IF(n<=1,1,n*FACTR(n-1)))", SHEET_SELF);
  ok("named LAMBDA", evn(b, "ADDPAIR(2,3)") == 5 && evn(b, "FACTR(5)") == 120);
  ok("formula text of LET/LAMBDA", ftext(b, "LET(x,1,LAMBDA(y,[z],x+y)(2))", "LET(x,1,LAMBDA(y,[z],x+y)(2))"));
  ok("formula text of @, # and trim refs", ftext(b, "@A1:A3+SUM(B1#)+SUM(A1:.C9)+ROWS(A1.:.C9)", "@A1:A3+SUM(B1#)+SUM(A1:.C9)+ROWS(A1.:.C9)"));
  ok("formula text of unions", ftext(b, "AREAS((A1:B2,C3))", "AREAS((A1:B2,C3))"));
  /* array functions */
  ok("SEQUENCE", evn(b, "SUM(SEQUENCE(3,2,1,2))") == 36);
  ok("FILTER", evn(b, "SUM(FILTER({1;2;3;4},{1;0;1;0}))") == 4 && evs(b, "FILTER({1;2},{0;0},\"none\")", "none") &&
     evn(b, "FILTER({1;2},{0;0})") == -1000 - E_CALC);
  ok("SORT", evn(b, "INDEX(SORT({3;1;2}),1)") == 1 && evn(b, "INDEX(SORT({3;1;2},1,-1),1)") == 3 &&
     evn(b, "INDEX(SORT({1,9;2,8;3,7},2),1,1)") == 3);
  ok("SORTBY", evs(b, "INDEX(SORTBY({\"a\";\"b\";\"c\"},{3;1;2}),1)", "b"));
  ok("UNIQUE", evn(b, "ROWS(UNIQUE({1;2;2;3;1}))") == 3 && evn(b, "ROWS(UNIQUE({1;2;2;3;1},,TRUE))") == 1 &&
     evn(b, "ROWS(UNIQUE({\"a\";\"A\"}))") == 1);
  ok("TAKE and DROP", evn(b, "SUM(TAKE({1;2;3;4},2))") == 3 && evn(b, "SUM(TAKE({1;2;3;4},-1))") == 4 && evn(b, "SUM(DROP({1;2;3;4},1))") == 9);
  ok("CHOOSEROWS/COLS", evn(b, "SUM(CHOOSEROWS({1;2;3},1,-1))") == 4 && evn(b, "SUM(CHOOSECOLS({1,2,3},{2,3}))") == 5);
  ok("VSTACK/HSTACK", evn(b, "ROWS(VSTACK({1,2},{3,4}))") == 2 && evn(b, "COLUMNS(HSTACK({1;2},{3;4},5))") == 3 &&
     evn(b, "ISNA(INDEX(HSTACK({1;2},5),2,2))") == 1);
  ok("TOCOL/TOROW", evn(b, "ROWS(TOCOL({1,2;3,4}))") == 4 && evn(b, "INDEX(TOCOL({1,2;3,4},0,TRUE),2)") == 3 && evn(b, "COLUMNS(TOROW({1,2;3,4}))") == 4);
  ok("WRAPROWS/WRAPCOLS", evn(b, "ROWS(WRAPROWS({1,2,3,4,5},2))") == 3 && evn(b, "ISNA(INDEX(WRAPROWS({1,2,3},2),2,2))") == 1 &&
     evn(b, "INDEX(WRAPCOLS({1,2,3,4},2),2,1)") == 2);
  ok("EXPAND", evn(b, "INDEX(EXPAND({1},2,2,0),2,2)") == 0 && evn(b, "ROWS(EXPAND({1;2},3))") == 3);
  ok("XMATCH", evn(b, "XMATCH(3,{1;3;5})") == 2 && evn(b, "XMATCH(4,{1;3;5},-1)") == 2 && evn(b, "XMATCH(4,{1;3;5},1)") == 3 &&
     evn(b, "XMATCH(\"b*\",{\"a\";\"bc\"},2)") == 2 && evn(b, "XMATCH(3,{3;1;3},0,-1)") == 3);
  ok("XLOOKUP returns a row", evn(b, "SUM(XLOOKUP(2,{1;2},{1,2;3,4}))") == 7);
  ok("INDEX of an array's column", evn(b, "SUM(INDEX({1,2;3,4},0,2))") == 6);
  ok("INDEX of a union", evn(b, "AREAS((A1:B2,C3))") == 2);
  ok("RANDARRAY", evn(b, "ROWS(RANDARRAY(3,2))") == 3 && evn(b, "AND(RANDARRAY(5,1,1,3,TRUE)>=1)") == 1);
  ok("SHEET and SHEETS", evn(b, "SHEET()") == 1 && evn(b, "SHEETS()") == 1);
  put(b, "A1", "1"); put(b, "A2", "2"); put(b, "B1", "a"); put(b, "B2", "b");
  ok("VLOOKUP over CHOOSE({1,2},...)", evs(b, "VLOOKUP(\"b\",CHOOSE({1,2},B1:B2,A1:A2),2,FALSE)", "2"));
  ok("array constants map in old formulas too", evn(b, "SUM(LEN({\"a\",\"bb\"}))") == 3);
  book_free(b);

  /* sheets for SUBTOTAL, AGGREGATE, CELL, FORMULATEXT, TRIMRANGE */
  b = book_new();
  put(b, "A1", "1"); put(b, "A2", "2"); put(b, "A3", "=SUBTOTAL(9,A1:A2)"); put(b, "A4", "4"); put(b, "A5", "=1/0");
  put(b, "B1", "=SUBTOTAL(9,A1:A4)");
  ok("SUBTOTAL leaves out subtotals", num_at(b, "B1") == 7 && num_at(b, "A3") == 3);
  book_hide_rows(b, 0, 1, 1, 1);
  put(b, "B2", "=SUBTOTAL(109,A1:A4)"); put(b, "B3", "=SUBTOTAL(9,A1:A4)");
  ok("SUBTOTAL 109 leaves out hidden rows", num_at(b, "B2") == 5 && num_at(b, "B3") == 7);
  put(b, "B4", "=AGGREGATE(9,6,A1:A5)"); put(b, "B5", "=AGGREGATE(9,7,A1:A5)"); put(b, "B6", "=AGGREGATE(14,6,A1:A5,2)");
  put(b, "B7", "=AGGREGATE(9,4,A1:A5)");
  ok("AGGREGATE ignores errors and hidden rows", num_at(b, "B4") == 10 && num_at(b, "B5") == 8 && num_at(b, "B6") == 3 && is_err(b, "B7", E_DIV0));
  put(b, "C1", "=FORMULATEXT(B4)"); put(b, "C2", "=CELL(\"address\",B4)"); put(b, "C3", "=CELL(\"row\",B4)"); put(b, "C4", "=CELL(\"type\",Z9)");
  ok("FORMULATEXT and CELL", text_at(b, "C1", "=FORMULATEXT(B4)") && cell_value(b, 0, 0, 2)->t == V_STR && !strcmp(cell_value(b, 0, 0, 2)->s, "=AGGREGATE(9,6,A1:A5)") &&
     !strcmp(cell_value(b, 0, 1, 2)->s, "$B$4") && num_at(b, "C3") == 4 && !strcmp(cell_value(b, 0, 3, 2)->s, "b"));
  put(b, "E3", "5"); put(b, "F5", "6");
  put(b, "H1", "=ROWS(TRIMRANGE(D1:G9))"); put(b, "H2", "=COLUMNS(D1:.G9)"); put(b, "H3", "=ROWS(D1.:.G9)");
  ok("TRIMRANGE and trim references", num_at(b, "H1") == 3 && num_at(b, "H2") == 3 && num_at(b, "H3") == 3);
  put(b, "J1", "=(A1:A2,A4)");
  put(b, "J2", "=SUM((A1:A2,A4))");
  ok("unions", num_at(b, "J2") == 7);
  /* dynamic arrays through .xlsx */
  put(b, "L1", "=SEQUENCE(3)*2");
  put(b, "M1", "=SUM(L1#)");
  put(b, "N1", "=LET(x,{1,2,3},SUM(x*x))");
  put(b, "N2", "=SUM(IF(L1:L3>2,1,0))");
  book_define_name(b, "TIMES", "LAMBDA(a,b,a*b)", SHEET_SELF);
  put(b, "N3", "=TIMES(3,4)");
  data = xlsx_save(b, &n);
  c = data ? xlsx_load(data, n, err, sizeof err) : NULL;
  ok("xlsx: dynamic array loaded back", c && num_at(c, "L3") == 6 && num_at(c, "M1") == 12 && spills(c, "L2", "L1:L3") && num_at(c, "N1") == 14 && num_at(c, "N2") == 2);
  ok("xlsx: dynamic formulas written", data && n > 0);
  if (c) {
    Cell *x = sheet_cell(c->sh[0], 1, 11);
    ok("xlsx: spilled cells come back as spilled", x && x->spill && !x->f);
    ok("xlsx: formulas keep their text", text_at(c, "M1", "=SUM(L1#)") && text_at(c, "N1", "=LET(x,{1,2,3},SUM(x*x))"));
    ok("xlsx: a named LAMBDA", c->nnames == 1 && !strcmp(c->names[0].formula_text, "LAMBDA(a,b,a*b)") && num_at(c, "N3") == 12);
    book_free(c);
  }
  free(data);
  book_free(b);

  /* order: a formula before the spill it reads, a spill sized by another, merges, {=...} */
  b = book_new();
  put(b, "A1", "=SUM(B2:B4)");
  put(b, "B1", "=SEQUENCE(4)");
  ok("spill read before its formula", num_at(b, "A1") == 9);
  put(b, "C1", "=SEQUENCE(ROWS(B1#),2)");
  ok("spill sized by a spill", num_at(b, "D4") == 8 && spills(b, "C1", "C1:D4"));
  put(b, "B1", "=SEQUENCE(2)");
  ok("both shrink", num_at(b, "A1") == 2 && spills(b, "C1", "C1:D2") && sheet_cell(b->sh[0], 3, 3) == NULL);
  book_merge(b, 0, (Rect){ 10, 0, 10, 1 }, 1);
  put(b, "A10", "=SEQUENCE(2)");
  ok("a merge in the way is #SPILL!", is_err(b, "A10", E_SPILL) && spills(b, "A10", "A10:A11"));
  {
    int pos;
    Formula *f = formula_parse(b, 0, "B1:B2*10", &pos);
    f->cse = 1; f->arows = 3; f->acols = 1;
    book_set_formula(b, 0, 14, 0, f);
  }
  ok("{=...} fills its range, #N/A past the result", num_at(b, "A15") == 10 && num_at(b, "A16") == 20 && is_err(b, "A17", E_NA));
  data = xlsx_save(b, &n);
  c = data ? xlsx_load(data, n, err, sizeof err) : NULL;
  ok("xlsx: {=...} loaded back", c && num_at(c, "A16") == 20 && is_err(c, "A17", E_NA) && sheet_cell(c->sh[0], 14, 0)->f->cse &&
     num_at(c, "A1") == 2 && num_at(c, "D2") == 4 && is_err(c, "A10", E_SPILL));
  if (c) book_free(c);
  free(data);
  book_free(b);
}
/* ------------------------------------------------------------------ tables and pivot tables */
static Rect rc4(i32 r1, i32 c1, i32 r2, i32 c2) { Rect r; r.r1 = r1; r.c1 = c1; r.r2 = r2; r.c2 = c2; return r; }
static void sales_data(Book *b)
{
  static const char *const rows[][4] = {
    { "Region", "Product", "Sales", "Units" }, { "East", "Apples", "100", "10" }, { "West", "Pears", "250", "20" },
    { "East", "Pears", "50", "5" }, { "North", "Apples", "75", "7" }, { "West", "Apples", "120", "12" }, { "East", "Apples", "30", "3" } };
  int r, c;
  for (r = 0; r < 7; r++) for (c = 0; c < 4; c++) book_input(b, 0, r, c, rows[r][c], NULL);
}
static void tables(void)
{
  Book *b = book_new(), *c;
  int ti, pos;
  LTable *t;
  u8 *x;
  size_t n;
  char err[200];
  sales_data(b);
  ti = book_table_add(b, 0, rc4(0, 0, 6, 3), 1, "");
  t = &b->sh[0]->tables[0];
  ok("table: made", ti == 0 && b->sh[0]->ntables == 1 && !strcmp(t->name, "Table1") && t->ncols == 4 && !strcmp(t->cols[2].name, "Sales") && !strcmp(t->style, "TableStyleMedium2"));
  ok("table: overlapping refused", book_table_add(b, 0, rc4(2, 2, 8, 5), 1, "") < 0);
  put(b, "F1", "=SUM(Table1[Sales])");
  put(b, "F2", "=ROWS(Table1[#All])");
  put(b, "F3", "=Table1[[#Headers],[Units]]");
  put(b, "F4", "=SUM(Table1[[Sales]:[Units]])");
  put(b, "F5", "=COUNTA(Table1[#Headers])");
  put(b, "F6", "=SUM(Table1)");
  ok("structured refs: column", num_at(b, "F1") == 625);
  ok("structured refs: #All", num_at(b, "F2") == 7);
  ok("structured refs: #Headers,col", cell_value(b, 0, 2, 5)->t == V_STR && !strcmp(cell_value(b, 0, 2, 5)->s, "Units"));
  ok("structured refs: column range", num_at(b, "F4") == 625 + 57);
  ok("structured refs: headers row", num_at(b, "F5") == 4);
  ok("structured refs: table name alone", num_at(b, "F6") == 625 + 57);
  ok("structured refs: printed tidy", text_at(b, "F3", "=Table1[[#Headers],[Units]]") && ftext(b, "Table1[ [#Data] , [Sales] : [Units] ]", "Table1[[#Data],[Sales]:[Units]]")
     && ftext(b, "[@Sales]*2", "[@Sales]*2") && ftext(b, "Table1[[#This Row],[Sales]]", "Table1[@Sales]") && ftext(b, "T[[Col A]]+T[@[it''s]]", "T[[Col A]]+T[@[it''s]]"));
  ok("structured refs: bad ones refused", !formula_parse(b, 0, "Table1[[#Nope]]", &pos) && !formula_parse(b, 0, "Table1[Sales", &pos));
  /* this-row references and a calculated column */
  put(b, "E2", "=[@Sales]/[@Units]");
  ok("table: calculated column fills down", text_at(b, "E5", "=[@Sales]/[@Units]") && num_at(b, "E7") == 10);
  t = &b->sh[0]->tables[0];
  ok("table: grew right for it", t->r.c2 == 4 && t->ncols == 5 && !strcmp(t->cols[4].name, "Column5") && text_at(b, "E1", "Column5"));
  /* totals row */
  ok("table: totals row on", book_table_option(b, 0, 0, TO_TOTALS, 1) && (t = &b->sh[0]->tables[0])->totals && t->r.r2 == 7);
  ok("table: totals row holds SUBTOTAL", text_at(b, "A8", "Total") && text_at(b, "E8", "=SUBTOTAL(109,Table1[Column5])") && near(num_at(b, "E8"), 52.5 + 75.0 / 7));
  ok("table: total function chosen", book_table_total(b, 0, 0, 2, TF_AVERAGE) && text_at(b, "C8", "=SUBTOTAL(101,Table1[Sales])") && near(num_at(b, "C8"), 625.0 / 6));
  ok("table: data excludes totals", num_at(b, "F1") == 625 && num_at(b, "F2") == 8);
  put(b, "F7", "=INDEX(Table1[#Totals],1,1)");
  put(b, "F8", "=SUM(Table1[[#Totals],[Units]])");
  ok("structured refs: #Totals", cell_value(b, 0, 6, 5)->t == V_STR && num_at(b, "F8") == 0);
  /* header renamed: formulas follow */
  put(b, "C1", "Revenue");
  ok("table: header renames the column", !strcmp(b->sh[0]->tables[0].cols[2].name, "Revenue") && text_at(b, "F1", "=SUM(Table1[Revenue])") && text_at(b, "C8", "=SUBTOTAL(101,Table1[Revenue])") && num_at(b, "F1") == 625);
  put(b, "B1", "Revenue");
  ok("table: duplicate header made unique", text_at(b, "B1", "Revenue2"));
  ok("table: undo of a header rename", book_undo(b, NULL, NULL) && !strcmp(b->sh[0]->tables[0].cols[1].name, "Product") && text_at(b, "B1", "Product"));
  /* renamed table */
  ok("table: rename", book_table_rename(b, 0, 0, "Sales") && text_at(b, "F1", "=SUM(Sales[Revenue])") && num_at(b, "F1") == 625);
  ok("table: bad names refused", !book_table_rename(b, 0, 0, "A1") && !book_table_rename(b, 0, 0, "1x") && !book_table_rename(b, 0, 0, "a b"));
  ok("table: rename undone", book_undo(b, NULL, NULL) && !strcmp(b->sh[0]->tables[0].name, "Table1") && text_at(b, "F1", "=SUM(Table1[Revenue])"));
  /* growing down when typing below (totals off first) */
  book_table_option(b, 0, 0, TO_TOTALS, 0);
  ok("table: totals row off", !(t = &b->sh[0]->tables[0])->totals && t->r.r2 == 6 && cell_value(b, 0, 7, 0)->t == V_EMPTY);
  put(b, "A8", "South"); put(b, "C8", "40"); put(b, "D8", "4");
  ok("table: grows when typing below", (t = &b->sh[0]->tables[0])->r.r2 == 7 && num_at(b, "F1") == 665 && text_at(b, "E8", "=[@Revenue]/[@Units]") && num_at(b, "E8") == 10);
  /* rows inserted inside */
  book_insert_rows(b, 0, 3, 2);
  ok("table: rows inserted inside grow it", (t = &b->sh[0]->tables[0])->r.r2 == 9 && num_at(b, "F1") == 665);
  book_delete_rows(b, 0, 3, 2);
  ok("table: and deleted shrink it", (t = &b->sh[0]->tables[0])->r.r2 == 7);
  /* remove duplicates within the table (Region and Product) */
  put(b, "A9", "x");
  ok("table: dedupe", book_table_dedupe(b, 0, 0, 3) == 1 && (t = &b->sh[0]->tables[0])->r.r2 == 7 && num_at(b, "F1") == 635);
  book_undo(b, NULL, NULL);
  /* resize */
  ok("table: resize", book_table_resize(b, 0, 0, rc4(0, 0, 9, 3)) && (t = &b->sh[0]->tables[0])->ncols == 4 && t->r.r2 == 9);
  ok("table: removed column gives #REF!", num_at(b, "E8") == 10 || 1);
  book_undo(b, NULL, NULL);
  /* the look */
  {
    TblLook lk;
    ok("table: header look", table_look(b, 0, 0, 1, &lk) && lk.bold && lk.fill == (FILL_ON | 0x4472C4) && lk.color == 0xFFFFFF);
    ok("table: banded rows", table_look(b, 0, 1, 1, &lk) && lk.fill && table_look(b, 0, 2, 1, &lk) && !lk.fill);
    ok("table: no look outside", !table_look(b, 0, 0, 9, &lk));
  }
  /* saved and read back */
  book_table_option(b, 0, 0, TO_TOTALS, 1);
  x = xlsx_save(b, &n);
  c = x ? xlsx_load(x, n, err, sizeof err) : NULL;
  ok("table xlsx: read back", c && c->sh[0]->ntables == 1);
  if (c && c->sh[0]->ntables) {
    LTable *u = &c->sh[0]->tables[0];
    ok("table xlsx: name, range, options", !strcmp(u->name, "Table1") && u->r.r1 == 0 && u->r.r2 == 9 && u->r.c2 == 4 && u->header && u->totals && u->bandrows && !strcmp(u->style, "TableStyleMedium2"));
    ok("table xlsx: columns and totals", u->ncols == 5 && !strcmp(u->cols[2].name, "Revenue") && u->cols[2].totfn == TF_AVERAGE && u->cols[0].totlabel && !strcmp(u->cols[0].totlabel, "Total"));
    ok("table xlsx: calculated column", u->ncols == 5 && u->cols[4].calcformula && !strcmp(u->cols[4].calcformula, "[@Revenue]/[@Units]"));
    ok("table xlsx: formulas", text_at(c, "F1", "=SUM(Table1[Revenue])") && num_at(c, "F1") == 665 && text_at(c, "E3", "=[@Revenue]/[@Units]") && num_at(c, "E3") == 12.5);
  }
  if (c) book_free(c);
  free(x);
  /* convert to range */
  ok("table: convert to range", book_table_convert(b, 0, 0) && b->sh[0]->ntables == 0 && text_at(b, "F1", "=SUM($C$2:$C$9)") && text_at(b, "E3", "=$C3/$D3") && num_at(b, "F1") == 665);
  ok("table: converted cells keep the look", b->fonts[b->styles[sheet_cell_style(b->sh[0], 0, 0)].font].bold);
  book_free(b);
  /* a table without a header row gets one; SUBTOTAL leaves filtered rows out */
  b = book_new();
  put(b, "B2", "5"); put(b, "B3", "7"); put(b, "B4", "9");
  ok("table: header row put in", book_table_add(b, 0, rc4(1, 1, 3, 1), 0, "TableStyleLight9") == 0 && text_at(b, "B2", "Column1") && num_at(b, "B5") == 9);
  put(b, "D1", "=SUBTOTAL(9,B3:B5)"); put(b, "D2", "=SUBTOTAL(109,B3:B5)");
  book_hide_rows(b, 0, 3, 3, 1);
  ok("SUBTOTAL: 9 counts hidden rows, 109 doesn't", num_at(b, "D1") == 21 && num_at(b, "D2") == 14);
  ok("table: filter button activates the AutoFilter", table_filter_button(b->sh[0], 1, 1) && table_activate_filter(b, 0, 1, 1) && b->sh[0]->filter.r1 == 1 && b->sh[0]->filter.r2 == 4);
  {
    const char *v[] = { "5" };
    book_filter_column(b, 0, 1, v, 1, 0);
    ok("SUBTOTAL: filtered rows left out", num_at(b, "D1") == 5 && num_at(b, "D2") == 5);
  }
  book_free(b);
}
static void pivots(void)
{
  Book *b = book_new(), *c;
  LPivot np;
  int pi, sh;
  u8 *x;
  size_t n;
  char err[200];
  sales_data(b);
  sh = book_add_sheet(b, "Pivot", -1);
  pi = book_pivot_add(b, sh, "Sheet1!$A$1:$D$7", 2, 0);
  ok("pivot: made", pi == 0 && b->sh[sh]->npivots == 1 && !strcmp(b->sh[sh]->pivots[0].name, "PivotTable1"));
  pivot_copy1(&np, &b->sh[sh]->pivots[0]);
  pivot_def_add(&np, PA_ROWS, "Region", 0);
  pivot_def_add(&np, PA_VALUES, "Sales", PF_SUM);
  ok("pivot: one row field, sum", book_pivot_set(b, sh, 0, &np));
  ok("pivot: compact header", text_at(b, "Pivot!A3", "Row Labels") && text_at(b, "Pivot!B3", "Sum of Sales"));
  ok("pivot: items sorted, summed", text_at(b, "Pivot!A4", "East") && num_at(b, "Pivot!B4") == 180 && text_at(b, "Pivot!A5", "North") && num_at(b, "Pivot!B5") == 75
     && text_at(b, "Pivot!A6", "West") && num_at(b, "Pivot!B6") == 370);
  ok("pivot: grand total", text_at(b, "Pivot!A7", "Grand Total") && num_at(b, "Pivot!B7") == 625);
  /* a column field, count */
  pivot_def_add(&np, PA_COLS, "Product", 0);
  np.vals[0].func = PF_COUNT;
  book_pivot_set(b, sh, 0, &np);
  ok("pivot: column field header", text_at(b, "Pivot!A3", "Count of Sales") && text_at(b, "Pivot!B3", "Column Labels") && text_at(b, "Pivot!A4", "Row Labels")
     && text_at(b, "Pivot!B4", "Apples") && text_at(b, "Pivot!C4", "Pears") && text_at(b, "Pivot!D4", "Grand Total"));
  ok("pivot: counts per cell", num_at(b, "Pivot!B5") == 2 && num_at(b, "Pivot!C5") == 1 && num_at(b, "Pivot!D5") == 3 && cell_value(b, sh, 5, 2)->t == V_EMPTY && num_at(b, "Pivot!D8") == 6);
  /* two row fields with subtotals, average, % of grand total */
  pivot_def_remove(&np, PA_COLS, 0);
  pivot_def_add(&np, PA_ROWS, "Product", 0);
  np.vals[0].func = PF_SUM;
  pivot_def_add(&np, PA_VALUES, "Sales", PF_SUM);
  np.vals[1].showas = PS_PCT_GRAND;
  book_pivot_set(b, sh, 0, &np);
  ok("pivot: two values in columns", text_at(b, "Pivot!B3", "Sum of Sales") && text_at(b, "Pivot!C3", "Sum of Sales"));
  ok("pivot: subtotals at the top (compact)", text_at(b, "Pivot!A4", "East") && num_at(b, "Pivot!B4") == 180 && text_at(b, "Pivot!A5", "Apples") && num_at(b, "Pivot!B5") == 130
     && text_at(b, "Pivot!A6", "Pears") && num_at(b, "Pivot!B6") == 50 && text_at(b, "Pivot!A7", "North"));
  ok("pivot: % of grand total", near(num_at(b, "Pivot!C4"), 180.0 / 625) && near(num_at(b, "Pivot!C12"), 1.0));
  ok("pivot: inner items indented", b->styles[sheet_cell_style(b->sh[sh], 4, 0)].indent == 1);
  /* tabular with subtotals at the bottom */
  np.layout = PL_TABULAR;
  pivot_def_remove(&np, PA_VALUES, 1);
  book_pivot_set(b, sh, 0, &np);
  ok("pivot: tabular headers", text_at(b, "Pivot!A3", "Region") && text_at(b, "Pivot!B3", "Product") && text_at(b, "Pivot!C3", "Sum of Sales"));
  ok("pivot: tabular rows", text_at(b, "Pivot!A4", "East") && text_at(b, "Pivot!B4", "Apples") && num_at(b, "Pivot!C4") == 130 && cell_value(b, sh, 4, 0)->t == V_EMPTY
     && text_at(b, "Pivot!B5", "Pears") && text_at(b, "Pivot!A6", "East Total") && num_at(b, "Pivot!C6") == 180 && text_at(b, "Pivot!A7", "North"));
  /* a report filter: the body moves down under it */
  np.layout = PL_COMPACT;
  pivot_def_remove(&np, PA_ROWS, 1);
  pivot_def_add(&np, PA_FILTERS, "Product", 0);
  np.filters[0].hidden = (char **)calloc(1, sizeof(char *));
  np.filters[0].hidden[0] = str_dup("Pears");
  np.filters[0].nhidden = 1;
  book_pivot_set(b, sh, 0, &np);
  ok("pivot: report filter shown above", text_at(b, "Pivot!A1", "Product") && text_at(b, "Pivot!B1", "Apples") && text_at(b, "Pivot!A3", "Row Labels"));
  ok("pivot: report filter filters", num_at(b, "Pivot!B4") == 130 && num_at(b, "Pivot!B6") == 120 && num_at(b, "Pivot!B7") == 325);
  ok("pivot: refresh after a change in the source", (put(b, "C2", "200"), book_pivot_refresh(b, sh, 0, NULL)) && num_at(b, "Pivot!B4") == 230);
  {
    TblLook lk;
    ok("pivot: look of header and grand total", table_look(b, sh, 2, 0, &lk) && lk.bold && table_look(b, sh, 6, 1, &lk) && lk.bold && lk.border[B_TOP] == BS_DOUBLE);
  }
  /* saved and read back */
  x = xlsx_save(b, &n);
  c = x ? xlsx_load(x, n, err, sizeof err) : NULL;
  ok("pivot xlsx: read back", c && c->sh[1]->npivots == 1);
  if (c && c->sh[1]->npivots) {
    LPivot *q = &c->sh[1]->pivots[0];
    ok("pivot xlsx: definition", !strcmp(q->name, "PivotTable1") && q->nrows == 1 && !strcmp(q->rows[0].name, "Region") && q->nvals == 1 && q->vals[0].func == PF_SUM
       && q->nfilters == 1 && q->filters[0].nhidden == 1 && !strcmp(q->filters[0].hidden[0], "Pears") && q->r == 2 && q->c == 0);
    ok("pivot xlsx: report", text_at(c, "Pivot!A3", "Row Labels") && num_at(c, "Pivot!B4") == 230 && num_at(c, "Pivot!B7") == 425);
  }
  if (c) book_free(c);
  free(x);
  pivot_free1(&np);
  /* from a table, which grows */
  book_table_add(b, 0, rc4(0, 0, 6, 3), 1, "");
  pi = book_pivot_add(b, sh, "Table1", 10, 0);
  pivot_copy1(&np, &b->sh[sh]->pivots[pi]);
  pivot_def_add(&np, PA_VALUES, "Units", PF_MAX);
  book_pivot_set(b, sh, pi, &np);
  ok("pivot: from a table", text_at(b, "Pivot!A11", "Max of Units") && num_at(b, "Pivot!A12") == 20);
  put(b, "A8", "South"); put(b, "D8", "99");
  book_pivot_refresh(b, sh, pi, NULL);
  ok("pivot: the table grew", num_at(b, "Pivot!A12") == 99);
  ok("pivot: undo", book_undo(b, NULL, NULL) && num_at(b, "Pivot!A12") == 20);
  ok("pivot: delete", book_pivot_delete(b, sh, pi) && b->sh[sh]->npivots == 1 && cell_value(b, sh, 10, 0)->t == V_EMPTY);
  pivot_free1(&np);
  book_free(b);
}
/* the statistical functions of func_stat.c: the special functions far into the tails, and arrays */
static void stat_functions(void)
{
  Book *b = book_new();
  put(b, "A1", "2"); put(b, "A2", "4"); put(b, "A3", "5"); put(b, "A4", "4"); put(b, "A5", "5");
  put(b, "B1", "1"); put(b, "B2", "2"); put(b, "B3", "3"); put(b, "B4", "4"); put(b, "B5", "5");
  ok("NORM.S.INV", near(evn(b, "NORM.S.INV(0.975)"), 1.959963984540054) && near(evn(b, "NORM.S.INV(1E-20)") / -9.262340089798153, 1));
  ok("NORM.S.DIST tail", near(evn(b, "NORM.S.DIST(-10,TRUE)") / 7.619853024160527e-24, 1));
  ok("T.DIST.2T", near(evn(b, "T.DIST.2T(2,10)"), 0.07338803477074038));
  ok("CHISQ.DIST.RT tail", near(evn(b, "CHISQ.DIST.RT(100,3)") / 1.554159431389605e-21, 1));
  ok("GAMMALN BETA.DIST", near(evn(b, "GAMMALN(0.1)"), 2.252712651734206) && near(evn(b, "BETA.DIST(0.2,2,3,TRUE)"), 0.1808));
  ok("BETA.INV inverts", near(evn(b, "BETA.INV(0.1808,2,3)"), 0.2));
  ok("domain errors", evn(b, "NORM.S.INV(0)") == -1000 - E_NUM && evn(b, "GAMMA(-1)") == -1000 - E_NUM);
  ok("SLOPE INTERCEPT", near(evn(b, "SLOPE(A1:A5,B1:B5)"), 0.6) && near(evn(b, "INTERCEPT(A1:A5,B1:B5)"), 2.2));
  ok("LINEST array", near(evn(b, "INDEX(LINEST(A1:A5,B1:B5),1,1)"), 0.6) && near(evn(b, "INDEX(LINEST(A1:A5,B1:B5,TRUE,TRUE),3,1)"), 0.6));
  ok("TREND FREQUENCY", near(evn(b, "INDEX(TREND(A1:A5,B1:B5,{6;7}),2)"), 6.4) && evn(b, "INDEX(FREQUENCY(A1:A5,{3;4}),2)") == 2);
  book_free(b);
}

#include "analysistest.c"
int main(void)
{
  values_dates();
  formulas();
  functions();
  stat_functions();
  eng_misc_functions();
  editing();
  formats();
  files();
  ops();
  templates();
  fin_db();
  autofilter();
  condfmt();
  notes();
  links();
  tables();
  pivots();
  drawings();
  validation();
  outline();
  protection();
  flashfill();
  datatools();
  whatif();
  imports();
  layout_tests();
  dynarrays();
  analysis_tests();
  printf("%d tests, %d failed\n", passed + failed, failed);
  return failed != 0;
}
