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

int main(void)
{
  values_dates();
  formulas();
  functions();
  editing();
  formats();
  files();
  ops();
  templates();
  autofilter();
  condfmt();
  notes();
  links();
  drawings();
  printf("%d tests, %d failed\n", passed + failed, failed);
  return failed != 0;
}
