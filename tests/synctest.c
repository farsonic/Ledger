/*
 * synctest.c - tests of what came over from GridCraft 4c388df (2026-11-18), included by coretest.c:
 * ECMA.CEILING and GETPIVOTDATA, the functions' fixes for Excel's answers, and the file, editing and
 * chart changes of that update.
 */

static int ev_err(Book *b, const char *f, int e) { return evn(b, f) == -1000 - e; }

static void sync_functions(void)
{
  Book *b = book_new();
  ok("ECMA.CEILING", evn(b, "ECMA.CEILING(2.3,1)") == 3 && evn(b, "ECMA.CEILING(-2.3,-1)") == -3 && near(evn(b, "ECMA.CEILING(2.31,0.05)"), 2.35));
  ok("ECMA.CEILING takes two arguments", ev_err(b, "ECMA.CEILING(2.3)", E_VALUE) || evn(b, "ECMA.CEILING(2.3)") < -1000);
  /* distributions at their edges, as Excel */
  ok("HARMEAN without numbers is #N/A", ev_err(b, "HARMEAN({\"a\",\"b\"})", E_NA) && ev_err(b, "HARMEAN(0,1)", E_NUM));
  ok("T.DIST with 0 degrees", ev_err(b, "T.DIST(1,0.5,FALSE)", E_DIV0) && ev_err(b, "T.DIST(1,0.5,TRUE)", E_NUM) && ev_err(b, "T.DIST(1,-1,FALSE)", E_NUM));
  ok("T.INV of 0", ev_err(b, "T.INV(0,5)", E_DIV0) && ev_err(b, "T.INV(-0.1,5)", E_NUM));
  ok("GAMMA.DIST density at 0", ev_err(b, "GAMMA.DIST(0,1,2,FALSE)", E_NUM) && evn(b, "GAMMA.DIST(0,2,2,FALSE)") == 0 && evn(b, "GAMMA.DIST(0,1,2,TRUE)") == 0);
  ok("WEIBULL.DIST at 0", evn(b, "WEIBULL.DIST(0,0.5,1,FALSE)") == 0 && evn(b, "WEIBULL.DIST(0,1,2,FALSE)") == 0 && evn(b, "WEIBULL.DIST(0,1,2,TRUE)") == 0);
  ok("BETA.DIST density at its ends", ev_err(b, "BETA.DIST(0,1,2,FALSE)", E_NUM) && ev_err(b, "BETA.DIST(1,2,1,FALSE)", E_NUM) && evn(b, "BETA.DIST(0,2,2,FALSE)") == 0 && evn(b, "BETA.DIST(1,2,1,TRUE)") == 1);
  ok("BINOM.DIST trial limit", ev_err(b, "BINOM.DIST(1,2147483647,0.5,FALSE)", E_NUM) && ev_err(b, "BINOM.DIST.RANGE(2147483647,0.5,1)", E_NUM) && evn(b, "BINOM.DIST(1,2147483646,0.5,FALSE)") == 0);
  /* Loader's masses, against 40-digit values */
  ok("BINOM.DIST far out", near(evn(b, "BINOM.DIST(5000000,10000000,0.5,FALSE)") / 0.00025231324589418478, 1) && fabs(evn(b, "BINOM.DIST(500,1000,0.5,FALSE)") / 0.025225018178360802 - 1) < 1e-14);
  ok("POISSON.DIST far out", fabs(evn(b, "POISSON.DIST(1000,1000,FALSE)") / 0.0126146113487215 - 1) < 1e-13 && near(evn(b, "POISSON.DIST(3,2.5,FALSE)"), 0.21376301724973645));
  ok("T.DIST density at huge degrees", fabs(evn(b, "T.DIST(0.5,10000000000,FALSE)") / 0.35206532675164713 - 1) < 1e-14 && fabs(evn(b, "T.DIST(0.5,100000000,FALSE)") / 0.35206532549906471 - 1) < 1e-14);
  ok("PERCENTRANK significance", ev_err(b, "PERCENTRANK({1,2,3},2.5,309)", E_DIV0) && evn(b, "PERCENTRANK({1,2,3,7},2.5,20)") == 0.5 && near(evn(b, "PERCENTRANK({1,2,3},2.5,3)"), 0.75));
  ok("F.DIST at 1E10 degrees", evn(b, "F.DIST(1,10000000000,10000000000,TRUE)") > 0.49 && ev_err(b, "F.DIST(1,10000000001,1,TRUE)", E_NUM));
  ok("MOD quotient limit", ev_err(b, "MOD(1125900000000,1)", E_NUM) && evn(b, "MOD(1125800000000,7)") == dmod(1125800000000.0, 7));
  ok("YEARFRAC 30/360 from February's end", near(evn(b, "YEARFRAC(DATE(2021,2,28),DATE(2021,3,31),0)"), 31.0 / 360) && near(evn(b, "YEARFRAC(DATE(2021,3,31),DATE(2021,2,28))"), 31.0 / 360) &&
     evn(b, "DAYS360(DATE(2021,2,28),DATE(2021,3,31))") == 30);
  /* LINEST keeps a predictor next to a large offset; standard errors after centring */
  put(b, "A1", "1"); put(b, "A2", "3"); put(b, "A3", "4.5");
  put(b, "B1", "10000000000000"); put(b, "B2", "10000000000001"); put(b, "B3", "10000000000002");
  put(b, "C1", "1"); put(b, "C2", "2"); put(b, "C3", "4"); put(b, "D1", "0.1"); put(b, "D2", "0.1"); put(b, "D3", "0.1");
  ok("LINEST with a large offset", near(evn(b, "INDEX(LINEST(A1:A3,B1:B3),1)"), 1.75) && near(evn(b, "SLOPE(A1:A3,B1:B3)"), 1.75));
  ok("LINEST drops a constant column", evn(b, "INDEX(LINEST(A1:A3,D1:D3),1)") == 0 && near(evn(b, "INDEX(LINEST(A1:A3,D1:D3),2)"), 8.5 / 3));
  ok("LINEST standard errors", near(evn(b, "INDEX(LINEST(A1:A3,C1:C3,TRUE,TRUE),2,1)"), 0.30929478706587) && near(evn(b, "INDEX(LINEST(A1:A3,C1:C3,TRUE,TRUE),2,2)"), 0.818317088384971) &&
     near(evn(b, "INDEX(LINEST(A1:A3,C1:C3,TRUE,TRUE),1,1)"), 1.10714285714286));
  /* text */
  ok("FIXED with 15 significant digits", evs(b, "FIXED(1.005,20)", "1.00500000000000000000") && evs(b, "FIXED(1/3,20)", "0.33333333333333300000") && evs(b, "FIXED(1.1,16)", "1.1000000000000000"));
  ok("FIXED of a large number", evs(b, "FIXED(123456789012345678,0)", "123,456,789,012,346,000") && evs(b, "TEXT(123456789012345678,\"0\")", "123456789012346000"));
  ok("FIXED and DOLLAR length limits", evn(b, "LEN(FIXED(1E191,0))") == 255 && ev_err(b, "FIXED(1E192,0)", E_VALUE) && evn(b, "LEN(DOLLAR(1E189,0))") == 254 &&
     ev_err(b, "DOLLAR(1E190,0)", E_VALUE) && evn(b, "LEN(FIXED(-1E253,0,TRUE))") == 255 && ev_err(b, "FIXED(-1E191,0)", E_VALUE));
  ok("DOLLAR rounding to 0", evs(b, "DOLLAR(-0.0001,0)", "($0)") && evs(b, "DOLLAR(-0.0001,2)", "($0.00)") && evs(b, "DOLLAR(-0.0001,-2)", "$0"));
  ok("one character to one in case changes", evs(b, "LOWER(\"\xC4\xB0\")", "i") && evs(b, "PROPER(\"\xC4\xB0STANBUL\")", "Istanbul") && evs(b, "UPPER(\"Stra\xC3\x9F" "e\")", "STRA\xC3\x9F" "E") &&
     evs(b, "UPPER(\"\xC5\x82\xC3\xB3" "d\xC5\xBA\")", "\xC5\x81\xC3\x93" "D\xC5\xB9") && evs(b, "LOWER(\"\xC5\xB8\")", "\xC3\xBF"));
  ok("UNICHAR noncharacters", ev_err(b, "UNICHAR(65534)", E_NA) && ev_err(b, "UNICHAR(64976)", E_NA) && ev_err(b, "UNICHAR(131071)", E_NA) && ev_err(b, "UNICHAR(55296)", E_NA) && evs(b, "UNICHAR(65)", "A"));
  /* engineering */
  ok("IMSQRT by Excel's polar route", evs(b, "IMSQRT(\"-4\")", "1.22514845490862E-16+2i") && evs(b, "IMSQRT(\"i\")", "0.707106781186548+0.707106781186547i") &&
     evs(b, "IMSQRT(\"-5+12i\")", "2+3i") && evs(b, "IMSQRT(\"-4-1E-20i\")", "1.22514845490862E-16-2i") && evs(b, "IMSQRT(\"4\")", "2"));
  ok("0 shifted any distance", evn(b, "BITLSHIFT(0,60)") == 0 && evn(b, "BITRSHIFT(0,-60)") == 0 && ev_err(b, "BITLSHIFT(1,60)", E_NUM));
  ok("TRUE as places", ev_err(b, "DEC2BIN(1,TRUE)", E_VALUE) && ev_err(b, "DEC2HEX(-1,TRUE)", E_VALUE) && evs(b, "DEC2BIN(5,4)", "0101"));
  book_free(b);
}

static void sync_tests(void)
{
  sync_functions();
}
