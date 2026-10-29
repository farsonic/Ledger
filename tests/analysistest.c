/*
 * analysistest.c - tests of analysis.h (included by coretest.c): Solver on known LP, integer and
 * nonlinear problems, the Data Analysis tools against known values and the worksheet functions,
 * Forecast Sheet, checkboxes, Lock Cell and Allow Edit Ranges, and their .xlsx round trips.
 */
#include "../core/analysis.h"
#include "../core/drawing.h"

static int close_to(double a, double b, double tol) { return fabs(a - b) <= tol * (fabs(b) > 1 ? fabs(b) : 1); }
static double sol_of(const SolverResult *r, int j) { return r->sol && j < r->n ? r->sol[j] : -999999; }

static void solver_tests(void)
{
  Book *b = book_new();
  SolverModel m;
  SolverResult r;
  /* an LP: max 3x + 5y, x <= 4, 2y <= 12, 3x + 2y <= 18 (x = 2, y = 6, 36) */
  put(b, "B1", "0"); put(b, "B2", "0"); put(b, "B4", "=3*B1+5*B2"); put(b, "C2", "=2*B2"); put(b, "C3", "=3*B1+2*B2");
  solver_model_init(&m, 0);
  strcpy(m.objective, "B4"); strcpy(m.vars, "B1:B2"); m.method = SM_SIMPLEX;
  solver_add(&m, "B1", SR_LE, "4"); solver_add(&m, "C2", SR_LE, "12"); solver_add(&m, "C3", SR_LE, "18");
  ok("solver: simplex optimum", solver_solve(b, &m, &r) == SS_OPTIMAL && close_to(sol_of(&r, 0), 2, 1e-9) && close_to(sol_of(&r, 1), 6, 1e-9) && close_to(r.objective, 36, 1e-9));
  ok("solver: cells put back", num_at(b, "B1") == 0 && num_at(b, "B4") == 0);
  solver_finish(b, &m, &r, 1, 1);
  ok("solver: solution kept", num_at(b, "B1") == 2 && num_at(b, "B2") == 6 && num_at(b, "B4") == 36);
  ok("solver: one undo step", book_undo(b, NULL, NULL) && num_at(b, "B1") == 0 && num_at(b, "B2") == 0);
  solver_result_free(&r);
  /* the same with GRG Nonlinear */
  m.method = SM_GRG;
  ok("solver: GRG on the LP", solver_solve(b, &m, &r) == SS_CONVERGED && close_to(sol_of(&r, 0), 2, 1e-4) && close_to(r.objective, 36, 1e-5));
  solver_result_free(&r);
  /* the saved model: hidden names, and back */
  {
    SolverModel l;
    char t[100];
    solver_save(b, &m);
    ok("solver: saved as names", solver_load(b, 0, &l) && !strcmp(l.objective, "B4") && !strcmp(l.vars, "B1:B2") && l.ncons == 3 &&
       l.cons[1].rel == SR_LE && !strcmp(l.cons[1].rhs, "12") && l.method == SM_GRG && l.nonneg);
    solver_cons_text(&l.cons[2], t, sizeof t);
    ok("solver: constraint text", !strcmp(t, "C3 <= 18"));
    solver_model_free(&l);
  }
  /* integers: max 5x + 4y, 6x + 4y <= 24, x + 2y <= 6 (the LP's 3, 1.5; whole numbers 4, 0) */
  put(b, "E1", "0"); put(b, "E2", "0"); put(b, "E4", "=5*E1+4*E2"); put(b, "F1", "=6*E1+4*E2"); put(b, "F2", "=E1+2*E2");
  solver_model_free(&m);
  solver_model_init(&m, 0);
  strcpy(m.objective, "E4"); strcpy(m.vars, "E1:E2"); m.method = SM_SIMPLEX;
  solver_add(&m, "F1", SR_LE, "24"); solver_add(&m, "F2", SR_LE, "6");
  ok("solver: LP relaxation", solver_solve(b, &m, &r) == SS_OPTIMAL && close_to(sol_of(&r, 0), 3, 1e-9) && close_to(sol_of(&r, 1), 1.5, 1e-9));
  solver_result_free(&r);
  solver_add(&m, "E1:E2", SR_INT, "");
  ok("solver: branch and bound", solver_solve(b, &m, &r) == SS_OPTIMAL && sol_of(&r, 0) == 4 && sol_of(&r, 1) == 0 && close_to(r.objective, 20, 1e-9));
  solver_result_free(&r);
  /* a binary knapsack: values 10 13 7 8, weights 5 6 3 4, capacity 10 (13 + 7 = 20 from weight 9? 10+13 too heavy: 11) */
  put(b, "H1", "0"); put(b, "H2", "0"); put(b, "H3", "0"); put(b, "H4", "0");
  put(b, "H6", "=10*H1+13*H2+7*H3+8*H4"); put(b, "H7", "=5*H1+6*H2+3*H3+4*H4");
  solver_model_free(&m);
  solver_model_init(&m, 0);
  strcpy(m.objective, "H6"); strcpy(m.vars, "H1:H4"); m.method = SM_SIMPLEX;
  solver_add(&m, "H7", SR_LE, "10"); solver_add(&m, "H1:H4", SR_BIN, "");
  ok("solver: binary knapsack", solver_solve(b, &m, &r) == SS_OPTIMAL && close_to(r.objective, 21, 1e-9) && sol_of(&r, 1) == 1 && sol_of(&r, 3) == 1);
  solver_result_free(&r);
  /* all different: minimise B + 10 C + 100 D over a permutation of 1..3 (3, 2, 1: 123) */
  put(b, "J1", "1"); put(b, "J2", "1"); put(b, "J3", "1"); put(b, "J5", "=J1+10*J2+100*J3");
  solver_model_free(&m);
  solver_model_init(&m, 0);
  strcpy(m.objective, "J5"); strcpy(m.vars, "J1:J3"); m.method = SM_SIMPLEX; m.goal = SG_MIN;
  solver_add(&m, "J1:J3", SR_DIF, "");
  ok("solver: all different", solver_solve(b, &m, &r) == SS_OPTIMAL && sol_of(&r, 0) == 3 && sol_of(&r, 1) == 2 && sol_of(&r, 2) == 1 && r.objective == 123);
  solver_result_free(&r);
  /* not linear: Simplex refuses */
  put(b, "L1", "1"); put(b, "L2", "1"); put(b, "L4", "=(L1-3)^2+(L2+1)^2");
  solver_model_free(&m);
  solver_model_init(&m, 0);
  strcpy(m.objective, "L4"); strcpy(m.vars, "L1:L2"); m.method = SM_SIMPLEX; m.goal = SG_MIN;
  ok("solver: not linear", solver_solve(b, &m, &r) == SS_NOTLINEAR && !r.sol);
  solver_result_free(&r);
  /* nonlinear: the unconstrained minimum (3, -1), and with non-negative variables (3, 0) */
  m.method = SM_GRG; m.nonneg = 0;
  ok("solver: GRG minimum", solver_solve(b, &m, &r) == SS_CONVERGED && close_to(sol_of(&r, 0), 3, 1e-4) && close_to(sol_of(&r, 1), -1, 1e-4));
  solver_result_free(&r);
  m.nonneg = 1;
  ok("solver: GRG with bounds", solver_solve(b, &m, &r) == SS_CONVERGED && close_to(sol_of(&r, 0), 3, 1e-4) && fabs(sol_of(&r, 1)) < 1e-6 && close_to(r.objective, 1, 1e-6));
  solver_result_free(&r);
  /* a constrained one: min x^2 + y^2 with x + y >= 1 (0.5, 0.5) */
  put(b, "N1", "2"); put(b, "N2", "0"); put(b, "N4", "=N1^2+N2^2"); put(b, "N5", "=N1+N2");
  solver_model_free(&m);
  solver_model_init(&m, 0);
  strcpy(m.objective, "N4"); strcpy(m.vars, "N1:N2"); m.goal = SG_MIN;
  solver_add(&m, "N5", SR_GE, "1");
  ok("solver: GRG constrained", solver_solve(b, &m, &r) == SS_CONVERGED && close_to(sol_of(&r, 0), 0.5, 1e-4) && close_to(sol_of(&r, 1), 0.5, 1e-4) && close_to(r.objective, 0.5, 1e-5));
  solver_result_free(&r);
  /* Value Of: x^2 = 9 from 1 */
  put(b, "P1", "1"); put(b, "P2", "=P1^2");
  solver_model_free(&m);
  solver_model_init(&m, 0);
  strcpy(m.objective, "P2"); strcpy(m.vars, "P1"); m.goal = SG_VALUE; m.value = 9;
  ok("solver: value of", solver_solve(b, &m, &r) <= SS_CONVERGED && close_to(sol_of(&r, 0), 3, 1e-4));
  solver_result_free(&r);
  /* multistart finds the global minimum of (x^2-4)^2 + x on -5..5 (near -2.03; from 3 GRG stops near 1.97) */
  put(b, "R1", "3"); put(b, "R2", "=(R1^2-4)^2+R1");
  solver_model_free(&m);
  solver_model_init(&m, 0);
  strcpy(m.objective, "R2"); strcpy(m.vars, "R1"); m.goal = SG_MIN; m.nonneg = 0;
  solver_add(&m, "R1", SR_GE, "-5"); solver_add(&m, "R1", SR_LE, "5");
  ok("solver: local minimum", solver_solve(b, &m, &r) == SS_CONVERGED && sol_of(&r, 0) > 1.9 && sol_of(&r, 0) < 2);
  solver_result_free(&r);
  m.multistart = 1;
  ok("solver: multistart", solver_solve(b, &m, &r) <= SS_CONVERGED && sol_of(&r, 0) < -2 && sol_of(&r, 0) > -2.1);
  solver_result_free(&r);
  /* Evolutionary on a model that isn't smooth: ABS(x-2.5) + ABS(y-1) within 0..10 */
  put(b, "T1", "9"); put(b, "T2", "9"); put(b, "T3", "=ABS(T1-2.5)+ABS(T2-1)");
  solver_model_free(&m);
  solver_model_init(&m, 0);
  strcpy(m.objective, "T3"); strcpy(m.vars, "T1:T2"); m.goal = SG_MIN; m.method = SM_EVO;
  ok("solver: evolutionary needs bounds", solver_solve(b, &m, &r) == SS_BADMODEL);
  solver_result_free(&r);
  solver_add(&m, "T1:T2", SR_LE, "10");
  ok("solver: evolutionary", solver_solve(b, &m, &r) <= SS_LIMITFEAS && close_to(sol_of(&r, 0), 2.5, 1e-3) && close_to(sol_of(&r, 1), 1, 1e-3));
  solver_result_free(&r);
  /* infeasible and unbounded */
  solver_model_free(&m);
  solver_model_init(&m, 0);
  strcpy(m.objective, "B4"); strcpy(m.vars, "B1:B2"); m.method = SM_SIMPLEX;
  ok("solver: unbounded", solver_solve(b, &m, &r) == SS_UNBOUNDED);
  solver_result_free(&r);
  solver_add(&m, "C3", SR_GE, "20"); solver_add(&m, "C3", SR_LE, "10");
  ok("solver: infeasible", solver_solve(b, &m, &r) == SS_INFEASIBLE);
  solver_result_free(&r);
  /* bad models */
  strcpy(m.objective, "B1");
  ok("solver: objective needs a formula", solver_solve(b, &m, &r) == SS_BADMODEL);
  solver_result_free(&r);
  strcpy(m.objective, "B4"); strcpy(m.vars, "B4");
  ok("solver: variables are constants", solver_solve(b, &m, &r) == SS_BADMODEL);
  solver_result_free(&r);
  solver_model_free(&m);
  /* the reports */
  {
    int at;
    const char *why;
    solver_model_init(&m, 0);
    strcpy(m.objective, "B4"); strcpy(m.vars, "B1:B2"); m.method = SM_SIMPLEX;
    solver_add(&m, "B1", SR_LE, "4"); solver_add(&m, "C2", SR_LE, "12"); solver_add(&m, "C3", SR_LE, "18");
    solver_solve(b, &m, &r);
    at = solver_report(b, &m, &r, 0, &why);
    ok("solver: answer report", at >= 0 && !strcmp(b->sh[at]->name, "Answer Report 1") && text_at(b, "'Answer Report 1'!A1", "Ledger Answer Report") &&
       num_at(b, "'Answer Report 1'!E9") == 36 && text_at(b, "'Answer Report 1'!F20", "Binding") &&
       text_at(b, "'Answer Report 1'!F23", "Not Binding"));
    at = solver_report(b, &m, &r, 1, &why);
    ok("solver: limits report", at >= 0 && num_at(b, "'Limits Report 1'!D9") == 2 && fabs(num_at(b, "'Limits Report 1'!E9")) < 1e-9 &&
       close_to(num_at(b, "'Limits Report 1'!F9"), 30, 1e-6) && close_to(num_at(b, "'Limits Report 1'!G9"), 2, 1e-6) && close_to(num_at(b, "'Limits Report 1'!H9"), 36, 1e-6));
    solver_result_free(&r);
    solver_model_free(&m);
  }
  /* a constraint typed as text */
  {
    SolverCons c;
    ok("solver: parse constraints", solver_parse_cons("B5 <= 10", &c) && c.rel == SR_LE && !strcmp(c.lhs, "B5") && !strcmp(c.rhs, "10") &&
       solver_parse_cons("A1:A3 int", &c) && c.rel == SR_INT && !strcmp(c.lhs, "A1:A3") && solver_parse_cons("C1 = D1", &c) && c.rel == SR_EQ &&
       solver_parse_cons("C1>=$D$1", &c) && c.rel == SR_GE && solver_parse_cons("B1:B4 dif", &c) && c.rel == SR_DIF && !solver_parse_cons("B1", &c));
  }
  /* the simplex alone: min x + y with x + 2y >= 4, 3x + y >= 6 (1.6, 1.2) */
  {
    double c[2] = { 1, 1 }, a[4] = { 1, 2, 3, 1 }, rhs[2] = { 4, 6 }, lo[2] = { 0, 0 }, hi[2] = { HUGE_VAL, HUGE_VAL }, x[2], obj;
    u8 sense[2] = { SR_GE, SR_GE };
    ok("solver: dense simplex", solver_lp(2, c, 2, a, sense, rhs, lo, hi, NULL, x, &obj) == 0 && close_to(x[0], 1.6, 1e-9) && close_to(x[1], 1.2, 1e-9) && close_to(obj, 2.8, 1e-9));
  }
  book_free(b);
}

/* a Data Analysis tool on sheet 0, its report on a new sheet named name */
static int tp(Book *b, TPParams *p, const char *name)
{
  Rect w;
  const char *why;
  int r;
  strcpy(p->newsheet, name);
  r = toolpak_run(b, book_find_sheet(b, "Sheet1"), p, &w, &why);
  if (r < 0) printf("   toolpak %s: %s\n", name, why ? why : "-2");
  if (r >= 0 && getenv("TPDUMP") && strstr(getenv("TPDUMP"), name)) {
    i32 y, x;
    for (y = w.r1; y <= w.r2; y++) { printf("%s %ld:", name, (long)y + 1); for (x = w.c1; x <= w.c2; x++) { Buf o; buf_init(&o); to_text(b, cell_value(b, r, y, x), &o); printf(" | %s", o.data ? o.data : ""); buf_free(&o); } printf("\n"); }
  }
  return r;
}
static void toolpak_tests(void)
{
  Book *b = book_new();
  TPParams p;
  Rect w;
  const char *why;
  int i, S1;
  char t[64];
  /* data: A1:A5 2 3 4 5 5, B1:B5 1 3 2 5 4 (x), C1:C4 6 7 8 9 */
  static const char *const av[] = { "2", "3", "4", "5", "5" }, *const bv[] = { "1", "3", "2", "5", "4" }, *const cv[] = { "6", "7", "8", "9" };
  for (i = 0; i < 5; i++) { sprintf(t, "Sheet1!A%d", i + 1); put(b, t, av[i]); sprintf(t, "Sheet1!B%d", i + 1); put(b, t, bv[i]); }
  for (i = 0; i < 4; i++) { sprintf(t, "Sheet1!C%d", i + 1); put(b, t, cv[i]); }
  /* Descriptive Statistics against the worksheet functions */
  toolpak_defaults(&p, TP_DESCR); strcpy(p.in1, "A1:A5"); p.kl = 2; p.ks = 1;
  ok("toolpak: descriptive", tp(b, &p, "D") >= 0 && text_at(b, "D!A1", "Column1") && text_at(b, "D!A3", "Mean") && near(num_at(b, "D!B3"), 3.8) &&
     near(num_at(b, "D!B4"), evn(b, "STDEV(Sheet1!A1:A5)/SQRT(5)")) && num_at(b, "D!B5") == 4 && num_at(b, "D!B6") == 5 &&
     near(num_at(b, "D!B7"), evn(b, "STDEV(Sheet1!A1:A5)")) && near(num_at(b, "D!B9"), evn(b, "KURT(Sheet1!A1:A5)")) &&
     near(num_at(b, "D!B10"), evn(b, "SKEW(Sheet1!A1:A5)")) && num_at(b, "D!B11") == 3 && num_at(b, "D!B14") == 19 && num_at(b, "D!B15") == 5 &&
     text_at(b, "D!A16", "Largest(2)") && num_at(b, "D!B16") == 5 && num_at(b, "D!B17") == 2 && text_at(b, "D!A18", "Confidence Level(95.0%)") &&
     near(num_at(b, "D!B18"), evn(b, "CONFIDENCE.T(0.05,STDEV(Sheet1!A1:A5),5)")));
  /* Anova: Single Factor, by rows of a two-column block: groups 2 3 4 5 and 6 7 8 9 */
  put(b, "Sheet1!E1", "2"); put(b, "Sheet1!E2", "3"); put(b, "Sheet1!E3", "4"); put(b, "Sheet1!E4", "5"); put(b, "Sheet1!F1", "6"); put(b, "Sheet1!F2", "7"); put(b, "Sheet1!F3", "8"); put(b, "Sheet1!F4", "9");
  toolpak_defaults(&p, TP_ANOVA1); strcpy(p.in1, "E1:F4");
  ok("toolpak: anova single factor", tp(b, &p, "AN1") >= 0 && text_at(b, "AN1!A5", "Column 1") && num_at(b, "AN1!D6") == 7.5 && num_at(b, "AN1!B11") == 32 &&
     num_at(b, "AN1!B12") == 10 && near(num_at(b, "AN1!E11"), 19.2) && near(num_at(b, "AN1!F11"), evn(b, "F.DIST.RT(19.2,1,6)")) &&
     near(num_at(b, "AN1!G11"), evn(b, "F.INV.RT(0.05,1,6)")) && num_at(b, "AN1!B14") == 42 && num_at(b, "AN1!C14") == 7);
  /* two-factor without replication: rows 1 2 3 / 4 5 6 / 7 8 10 */
  put(b, "Sheet1!H1", "1"); put(b, "Sheet1!I1", "2"); put(b, "Sheet1!J1", "3"); put(b, "Sheet1!H2", "4"); put(b, "Sheet1!I2", "5"); put(b, "Sheet1!J2", "6"); put(b, "Sheet1!H3", "7"); put(b, "Sheet1!I3", "8"); put(b, "Sheet1!J3", "10");
  toolpak_defaults(&p, TP_ANOVA2); strcpy(p.in1, "H1:J3");
  ok("toolpak: two-factor without replication", tp(b, &p, "AN2") >= 0 && text_at(b, "AN2!A4", "Row 1") && text_at(b, "AN2!A8", "Column 1") &&
     near(num_at(b, "AN2!B15"), 60.22222222222223) && near(num_at(b, "AN2!B16"), 8.22222222222222) && close_to(num_at(b, "AN2!B17"), 0.4444444444444, 1e-9) &&
     num_at(b, "AN2!C17") == 4 && near(num_at(b, "AN2!B19"), 68.88888888888889) &&
     close_to(num_at(b, "AN2!E15"), 60.22222222222223 / 2 / (0.4444444444444444 / 4), 1e-9));
  /* two-factor with replication: samples s1, s2 of two rows, columns C1, C2 */
  put(b, "Sheet1!L2", "s1"); put(b, "Sheet1!L4", "s2"); put(b, "Sheet1!M1", "C1"); put(b, "Sheet1!N1", "C2");
  put(b, "Sheet1!M2", "1"); put(b, "Sheet1!M3", "2"); put(b, "Sheet1!N2", "3"); put(b, "Sheet1!N3", "5"); put(b, "Sheet1!M4", "4"); put(b, "Sheet1!M5", "6"); put(b, "Sheet1!N4", "8"); put(b, "Sheet1!N5", "9");
  toolpak_defaults(&p, TP_ANOVA2R); strcpy(p.in1, "L1:N5"); p.rowsper = 2;
  ok("toolpak: two-factor with replication", tp(b, &p, "AN3") >= 0 && text_at(b, "AN3!A4", "s1") && text_at(b, "AN3!B3", "C1") && num_at(b, "AN3!D5") == 4 &&
     num_at(b, "AN3!B7") == 1.5 && text_at(b, "AN3!A16", "Total") && num_at(b, "AN3!B25") == 32 && num_at(b, "AN3!B26") == 18 &&
     near(num_at(b, "AN3!B27"), 0.5) && num_at(b, "AN3!B28") == 5 && num_at(b, "AN3!C28") == 4 && near(num_at(b, "AN3!E25"), 32 / 1.25) && num_at(b, "AN3!C30") == 7);
  /* correlation and covariance */
  toolpak_defaults(&p, TP_CORREL); strcpy(p.in1, "A1:B5");
  ok("toolpak: correlation", tp(b, &p, "COR") >= 0 && num_at(b, "COR!B2") == 1 && near(num_at(b, "COR!B3"), evn(b, "CORREL(Sheet1!A1:A5,Sheet1!B1:B5)")) &&
     text_at(b, "COR!C1", "Column 2") && cell_value(b, book_find_sheet(b, "COR"), 1, 2)->t == V_EMPTY);
  toolpak_defaults(&p, TP_COVAR); strcpy(p.in1, "A1:B5");
  ok("toolpak: covariance", tp(b, &p, "COV") >= 0 && near(num_at(b, "COV!B3"), evn(b, "COVARIANCE.P(Sheet1!A1:A5,Sheet1!B1:B5)")) &&
     near(num_at(b, "COV!B2"), evn(b, "VAR.P(Sheet1!A1:A5)")));
  /* regression of A on B against LINEST */
  toolpak_defaults(&p, TP_REGRESSION); strcpy(p.in1, "A1:A5"); strcpy(p.in2, "B1:B5"); p.residuals = 1; p.stdres = 1; p.normprob = 1;
  ok("toolpak: regression", tp(b, &p, "REG") >= 0 && text_at(b, "REG!A1", "SUMMARY OUTPUT") && near(num_at(b, "REG!B5"), evn(b, "RSQ(Sheet1!A1:A5,Sheet1!B1:B5)")) &&
     near(num_at(b, "REG!B7"), evn(b, "STEYX(Sheet1!A1:A5,Sheet1!B1:B5)")) && num_at(b, "REG!B8") == 5 &&
     near(num_at(b, "REG!B17"), evn(b, "INTERCEPT(Sheet1!A1:A5,Sheet1!B1:B5)")) && near(num_at(b, "REG!B18"), evn(b, "SLOPE(Sheet1!A1:A5,Sheet1!B1:B5)")) &&
     near(num_at(b, "REG!C18"), evn(b, "INDEX(LINEST(Sheet1!A1:A5,Sheet1!B1:B5,TRUE,TRUE),2,1)")) &&
     near(num_at(b, "REG!C17"), evn(b, "INDEX(LINEST(Sheet1!A1:A5,Sheet1!B1:B5,TRUE,TRUE),2,2)")) &&
     near(num_at(b, "REG!E12"), evn(b, "INDEX(LINEST(Sheet1!A1:A5,Sheet1!B1:B5,TRUE,TRUE),4,1)")) &&
     near(num_at(b, "REG!F12"), evn(b, "F.DIST.RT(INDEX(LINEST(Sheet1!A1:A5,Sheet1!B1:B5,TRUE,TRUE),4,1),1,3)")) &&
     text_at(b, "REG!A22", "RESIDUAL OUTPUT") && text_at(b, "REG!B24", "Predicted Y") && near(num_at(b, "REG!C25"), -0.4) &&
     text_at(b, "REG!F22", "PROBABILITY OUTPUT") && num_at(b, "REG!F25") == 10 && num_at(b, "REG!G25") == 2);
  /* t-tests and the F-test against T.TEST and F.TEST */
  toolpak_defaults(&p, TP_TPAIRED); strcpy(p.in1, "A1:A5"); strcpy(p.in2, "B1:B5");
  ok("toolpak: paired t-test", tp(b, &p, "TP") >= 0 && near(num_at(b, "TP!B13"), evn(b, "T.TEST(Sheet1!A1:A5,Sheet1!B1:B5,2,1)")) && num_at(b, "TP!B9") == 4 &&
     near(num_at(b, "TP!B14"), evn(b, "T.INV.2T(0.05,4)")) && near(num_at(b, "TP!B7"), evn(b, "CORREL(Sheet1!A1:A5,Sheet1!B1:B5)")));
  toolpak_defaults(&p, TP_TEQUAL); strcpy(p.in1, "A1:A5"); strcpy(p.in2, "C1:C4");
  ok("toolpak: equal variances", tp(b, &p, "TE") >= 0 && near(num_at(b, "TE!B13"), evn(b, "T.TEST(Sheet1!A1:A5,Sheet1!C1:C4,2,2)")) && num_at(b, "TE!B9") == 7 &&
     near(num_at(b, "TE!B11"), evn(b, "T.TEST(Sheet1!A1:A5,Sheet1!C1:C4,1,2)")));
  toolpak_defaults(&p, TP_TUNEQUAL); strcpy(p.in1, "A1:A5"); strcpy(p.in2, "C1:C4");
  ok("toolpak: unequal variances", tp(b, &p, "TU") >= 0 && num_at(b, "TU!B8") == 7 && near(num_at(b, "TU!B12"), evn(b, "T.DIST.2T(ABS(TU!B9),7)")));
  toolpak_defaults(&p, TP_FTEST); strcpy(p.in1, "A1:A5"); strcpy(p.in2, "C1:C4");
  ok("toolpak: F-test", tp(b, &p, "FT") >= 0 && near(num_at(b, "FT!B9"), evn(b, "F.DIST.RT(1.02,4,3)")) && near(num_at(b, "FT!B10"), evn(b, "F.INV.RT(0.05,4,3)")) && num_at(b, "FT!C7") == 3);
  /* z-test with known variances 1 and 2 */
  toolpak_defaults(&p, TP_ZTEST); strcpy(p.in1, "A1:A5"); strcpy(p.in2, "C1:C4"); p.var1 = 1; p.var2 = 2;
  ok("toolpak: z-test", tp(b, &p, "ZT") >= 0 && near(num_at(b, "ZT!B8"), (3.8 - 7.5) / sqrt(1 / 5.0 + 2 / 4.0)) && near(num_at(b, "ZT!B5"), 1) &&
     near(num_at(b, "ZT!B11"), 2 * evn(b, "1-NORM.S.DIST(ABS(ZT!B8),TRUE)")) && near(num_at(b, "ZT!B12"), evn(b, "NORM.S.INV(0.975)")));
  /* histogram with bins 2 and 4, cumulative and Pareto */
  put(b, "Sheet1!P1", "2"); put(b, "Sheet1!P2", "4");
  toolpak_defaults(&p, TP_HISTOGRAM); strcpy(p.in1, "A1:A5"); strcpy(p.in2, "P1:P2"); p.cumulative = 1; p.pareto = 1;
  ok("toolpak: histogram", tp(b, &p, "HI") >= 0 && num_at(b, "HI!B2") == 1 && num_at(b, "HI!B3") == 2 && text_at(b, "HI!A4", "More") && num_at(b, "HI!B4") == 2 &&
     near(num_at(b, "HI!C3"), 0.6) && shows(b, book_find_sheet(b, "HI"), 2, 2, "60.00%") && num_at(b, "HI!E2") == 2 && num_at(b, "HI!F4") == 1);
  /* moving average, exponential smoothing */
  toolpak_defaults(&p, TP_MOVAVG); strcpy(p.in1, "A1:A5"); p.stderrs = 1;
  ok("toolpak: moving average", tp(b, &p, "MA") >= 0 && num_at(b, "MA!A1") == -1000 - E_NA && num_at(b, "MA!A3") == 3 && near(num_at(b, "MA!A5"), 14 / 3.0) &&
     num_at(b, "MA!B4") == -1000 - E_NA && near(num_at(b, "MA!B5"), sqrt((1 + 1 + 1 / 9.0) / 3)));
  toolpak_defaults(&p, TP_EXPSMOOTH); strcpy(p.in1, "A1:A5"); p.damping = 0.3;
  ok("toolpak: exponential smoothing", tp(b, &p, "ES") >= 0 && num_at(b, "ES!A1") == -1000 - E_NA && num_at(b, "ES!A2") == 2 && near(num_at(b, "ES!A3"), 0.7 * 3 + 0.3 * 2));
  /* rank and percentile */
  toolpak_defaults(&p, TP_RANK); strcpy(p.in1, "A1:A5");
  ok("toolpak: rank and percentile", tp(b, &p, "RK") >= 0 && num_at(b, "RK!A2") == 4 && num_at(b, "RK!B2") == 5 && num_at(b, "RK!C2") == 1 && num_at(b, "RK!C3") == 1 &&
     num_at(b, "RK!A6") == 1 && num_at(b, "RK!C6") == 5 && near(num_at(b, "RK!D2"), evn(b, "PERCENTRANK.INC(Sheet1!A1:A5,5)")) && near(num_at(b, "RK!D4"), 0.5));
  /* Fourier: 1 2 3 4 -> 10, -2+2i, -2, -2-2i; and back */
  put(b, "Sheet1!R1", "1"); put(b, "Sheet1!R2", "2"); put(b, "Sheet1!R3", "3"); put(b, "Sheet1!R4", "4");
  toolpak_defaults(&p, TP_FOURIER); strcpy(p.in1, "R1:R4");
  ok("toolpak: fourier", tp(b, &p, "FO") >= 0 && text_at(b, "FO!A1", "10") && text_at(b, "FO!A2", "-2+2i") && text_at(b, "FO!A3", "-2") && text_at(b, "FO!A4", "-2-2i"));
  toolpak_defaults(&p, TP_FOURIER); strcpy(p.in1, "FO!A1:A4"); p.inverse = 1;
  ok("toolpak: inverse fourier", tp(b, &p, "FI") >= 0 && text_at(b, "FI!A1", "1") && text_at(b, "FI!A2", "2") && text_at(b, "FI!A4", "4"));
  /* random numbers (a fixed seed repeats) and sampling */
  toolpak_defaults(&p, TP_RANDOM); p.nvars = 2; p.nnums = 50; p.dist = TPD_UNIFORM; p.p1 = 10; p.p2 = 20; p.seed = 42;
  ok("toolpak: random numbers", tp(b, &p, "RN") >= 0 && evn(b, "COUNT(RN!A1:B50)") == 100 && evn(b, "MIN(RN!A1:B50)") >= 10 && evn(b, "MAX(RN!A1:B50)") < 20 &&
     cell_value(b, book_find_sheet(b, "RN"), 50, 0)->t == V_EMPTY);
  p.dist = TPD_PATTERNED; p.p1 = 1; p.p2 = 3; p.p3 = 1; p.p4 = 2; p.p5 = 2; p.nvars = 1;
  ok("toolpak: patterned", tp(b, &p, "PA") >= 0 && num_at(b, "PA!A1") == 1 && num_at(b, "PA!A2") == 1 && num_at(b, "PA!A3") == 2 && num_at(b, "PA!A7") == 1 && evn(b, "COUNT(PA!A1:A20)") == 12);
  p.dist = TPD_BERNOULLI; p.p1 = 0.5; p.nnums = 200;
  ok("toolpak: bernoulli", tp(b, &p, "BE") >= 0 && evn(b, "SUM(BE!A1:A200)") > 60 && evn(b, "SUM(BE!A1:A200)") < 140 && evn(b, "COUNTIF(BE!A1:A200,\">1\")") == 0);
  toolpak_defaults(&p, TP_SAMPLING); strcpy(p.in1, "A1:A5"); p.periodic = 1; p.nsample = 2;
  ok("toolpak: periodic sample", tp(b, &p, "SP") >= 0 && num_at(b, "SP!A1") == 3 && num_at(b, "SP!A2") == 5 && cell_value(b, book_find_sheet(b, "SP"), 2, 0)->t == V_EMPTY);
  /* an output range: refused over data until overwrite */
  S1 = book_find_sheet(b, "Sheet1");
  toolpak_defaults(&p, TP_CORREL); strcpy(p.in1, "A1:B5"); p.outnew = 0; strcpy(p.out, "A20");
  ok("toolpak: output range", toolpak_run(b, S1, &p, &w, &why) == S1 && num_at(b, "Sheet1!B21") == 1 && w.r1 == 19 && w.c2 == 2);
  ok("toolpak: undo", book_undo(b, NULL, NULL) && cell_value(b, S1, 20, 1)->t == V_EMPTY);
  strcpy(p.out, "A1");
  ok("toolpak: asks before replacing", toolpak_run(b, S1, &p, &w, &why) == -2 && num_at(b, "Sheet1!A1") == 2);
  p.overwrite = 1;
  ok("toolpak: replaces", toolpak_run(b, S1, &p, &w, &why) == S1 && num_at(b, "Sheet1!B2") == 1);
  book_undo(b, NULL, NULL);
  toolpak_defaults(&p, TP_DESCR); strcpy(p.in1, "L1:N5");
  ok("toolpak: non-numeric input", toolpak_run(b, S1, &p, &w, &why) == -1 && why);
  book_free(b);
}

static Book *an_roundtrip(Book *b)
{
  size_t n;
  char err[200];
  u8 *data = xlsx_save(b, &n);
  Book *c = data ? xlsx_load(data, n, err, sizeof err) : NULL;
  free(data);
  return c;
}
static void forecast_tests(void)
{
  Book *b = book_new();
  ForecastOpts o;
  const char *why;
  int at, i;
  char t[32], v[32];
  double last, step;
  int np;
  /* y = 10 + 2t with a four-step season, t = 1..16 (GridCraft's test: t = 17 forecasts 47) */
  static const double season[4] = { 3, -1, 4, -6 };
  put(b, "A1", "Month"); put(b, "B1", "Sales");
  for (i = 1; i <= 16; i++) { sprintf(t, "A%d", i + 1); sprintf(v, "%d", i); put(b, t, v); sprintf(t, "B%d", i + 1); sprintf(v, "%g", 10 + 2.0 * i + season[(i - 1) % 4]); put(b, t, v); }
  ok("forecast: timeline", forecast_timeline(b, 0, "A1:A17", &last, &step, &np) && last == 16 && step == 1 && np == 16);
  forecast_defaults(&o);
  strcpy(o.timeline, "A1:A17"); strcpy(o.values, "B1:B17"); o.end = 20; o.stats = 1;
  at = forecast_sheet(b, 0, &o, &why);
  ok("forecast: sheet", at == 0 && b->nsh == 2 && b->active == 0 && text_at(b, "A1", "Month") && text_at(b, "C1", "Forecast(Sales)") &&
     text_at(b, "D1", "Lower Confidence Bound(Sales)") && text_at(b, "E1", "Upper Confidence Bound(Sales)") && num_at(b, "A18") == 17 && num_at(b, "A21") == 20 &&
     cell_value(b, 0, 21, 0)->t == V_EMPTY);
  ok("forecast: formulas", text_at(b, "C17", "=B17") && text_at(b, "C18", "=FORECAST.ETS(A18,$B$2:$B$17,$A$2:$A$17,1,1)") &&
     text_at(b, "D18", "=C18-FORECAST.ETS.CONFINT(A18,$B$2:$B$17,$A$2:$A$17,0.95,1,1)") && text_at(b, "H2", "=FORECAST.ETS.STAT($B$2:$B$17,$A$2:$A$17,1,1,1)"));
  ok("forecast: values", close_to(num_at(b, "C18"), 47, 1e-6) && num_at(b, "D19") <= num_at(b, "E19") && num_at(b, "D17") == num_at(b, "B17"));
  ok("forecast: table and chart", b->sh[0]->ntables == 1 && drw_count(b->sh[0]) == 1 && drw_obj(b->sh[0], 0)->chart->ns == 4 && drw_obj(b->sh[0], 0)->chart->kind == CK_LINE);
  ok("forecast: one undo step", book_undo(b, NULL, NULL) && b->nsh == 1 && !strcmp(b->sh[0]->name, "Sheet1"));
  /* options: no confidence, seasonality 4, zeros, sum, a column chart */
  forecast_defaults(&o);
  strcpy(o.timeline, "A2:A17"); strcpy(o.values, "B2:B17"); o.conf = 0; o.seasonality = 4; o.fill = 0; o.aggregate = 7; o.column = 1;
  at = forecast_sheet(b, 0, &o, &why);
  ok("forecast: options", at == 0 && text_at(b, "B1", "Values") && text_at(b, "C18", "=FORECAST.ETS(A18,$B$2:$B$17,$A$2:$A$17,4,0,7)") &&
     cell_value(b, 0, 0, 3)->t == V_EMPTY && num_at(b, "A23") == 22 && drw_obj(b->sh[0], 0)->chart->kind == CK_COMBO);
  b->active = 1;
  o.end = 3;
  ok("forecast: end before the data", forecast_sheet(b, 1, &o, &why) < 0 && why);
  o.end = 0;
  put(b, "Sheet1!A5", "4.5");
  ok("forecast: uneven timeline", forecast_sheet(b, 1, &o, &why) < 0 && why);
  book_free(b);
}

static void cellctl_tests(void)
{
  Book *b = book_new(), *c;
  Rect a;
  int ok1;
  /* checkboxes */
  put(b, "A2", "TRUE"); put(b, "A3", "=1>2");
  a.r1 = 0; a.r2 = 3; a.c1 = 0; a.c2 = 0;
  ok("checkbox: made", book_checkbox(b, 0, a) && cell_is_checkbox(b, 0, 0, 0) && cell_value(b, 0, 0, 0)->t == V_BOOL && !cell_value(b, 0, 0, 0)->e &&
     cell_value(b, 0, 1, 0)->e == 1 && sheet_cell(b->sh[0], 2, 0)->f && !cell_is_checkbox(b, 0, 0, 1) &&
     b->styles[sheet_cell_style(b->sh[0], 0, 0)].halign == HA_CENTER);
  ok("checkbox: toggles", book_checkbox_toggle(b, 0, 0, 0) && cell_value(b, 0, 0, 0)->e == 1 && book_checkbox_toggle(b, 0, 0, 0) && cell_value(b, 0, 0, 0)->e == 0 &&
     !book_checkbox_toggle(b, 0, 2, 0) && !book_checkbox_toggle(b, 0, 0, 1));
  ok("checkbox: shows as General otherwise", (put(b, "A4", "12.5"), shows(b, 0, 3, 0, "12.5")));
  ok("checkbox: undo", book_undo(b, NULL, NULL) && book_undo(b, NULL, NULL) && book_undo(b, NULL, NULL) && book_undo(b, NULL, NULL) && !cell_is_checkbox(b, 0, 0, 0));
  book_redo(b, NULL, NULL);
  c = an_roundtrip(b);
  ok("checkbox: .xlsx round trip", c && cell_is_checkbox(c, 0, 0, 0) && cell_is_checkbox(c, 0, 1, 0) && cell_value(c, 0, 1, 0)->e == 1 && !cell_is_checkbox(c, 0, 5, 0));
  if (c) {
    size_t n;
    u8 *data = xlsx_save(c, &n);
    /* the part and the extension Excel looks for (stored uncompressed or not, the names show in the zip directory) */
    ok1 = data && n > 0;
    {
      size_t i;
      int part = 0;
      for (i = 0; data && i + 40 < n; i++) if (!memcmp(data + i, "xl/featurePropertyBag/featurePropertyBag.xml", 44)) part = 1;
      ok("checkbox: feature property bag part", ok1 && part);
    }
    free(data);
    book_free(c);
  }
  /* the bag chain read from another order of bags */
  {
    static const char x[] = "<FeaturePropertyBags xmlns=\"x\"><bag type=\"XFComplements\"><a k=\"MappedFeaturePropertyBags\"><bagId>3</bagId><bagId>9</bagId></a></bag>"
                            "<bag type=\"Checkbox\"/><bag type=\"XFControls\"><bagId k=\"CellControl\">1</bagId></bag><bag type=\"XFComplement\"><bagId k=\"XFControls\">2</bagId></bag></FeaturePropertyBags>";
    u8 fl[4];
    ok("checkbox: bag chain", checkbox_complements(x, strlen(x), fl, 4) == 2 && fl[0] == 1 && fl[1] == 0);
  }
  /* Lock Cell */
  a.r1 = 4; a.r2 = 5; a.c1 = 1; a.c2 = 1;
  ok("lock cell", cell_locked(b, 0, 4, 1) && book_lock_cells(b, 0, a, 0) && !cell_locked(b, 0, 4, 1) && !cell_locked(b, 0, 5, 1) && cell_locked(b, 0, 6, 1));
  book_protect_sheet(b, 0, "", PA_DEFAULT);
  ok("lock cell: protection", !prot_locked(b, 0, 4, 1) && prot_locked(b, 0, 6, 1));
  /* Allow Edit Ranges: D1:D3 open, F1:F2 behind a password */
  {
    Rect d, f, x;
    d.r1 = 0; d.r2 = 2; d.c1 = d.c2 = 3; f.r1 = 0; f.r2 = 1; f.c1 = f.c2 = 5;
    ok("edit ranges: added", book_edit_range_set(b, 0, -1, "Open", &d, 1, "", 0) && book_edit_range_set(b, 0, -1, "Secret", &f, 1, "pw", 0) && b->sh[0]->neranges == 2);
    x = d; x.r2 = 1;
    ok("edit ranges: open on a protected sheet", !prot_locked(b, 0, 1, 3) && !prot_area_locked(b, 0, x) && prot_locked(b, 0, 3, 3) && prot_locked(b, 0, 0, 5) &&
       edit_range_shut(b->sh[0], f) == &b->sh[0]->eranges[1]);
    c = an_roundtrip(b);
    ok("edit ranges: .xlsx round trip", c && c->sh[0]->neranges == 2 && !strcmp(c->sh[0]->eranges[1].title, "Secret") && c->sh[0]->eranges[1].pwhash == prot_hash("pw") &&
       c->sh[0]->eranges[0].areas[0].r2 == 2 && !prot_locked(c, 0, 2, 3) && prot_locked(c, 0, 0, 5));
    if (c) book_free(c);
    ok("edit ranges: password", !edit_range_unlock(&b->sh[0]->eranges[1], "no") && edit_range_unlock(&b->sh[0]->eranges[1], "pw") && !prot_locked(b, 0, 1, 5));
    book_insert_rows(b, 0, 0, 2);
    ok("edit ranges: follow inserted rows", b->sh[0]->eranges[0].areas[0].r1 == 2 && b->sh[0]->eranges[0].areas[0].r2 == 4);
    ok("edit ranges: deleted", book_edit_range_delete(b, 0, 0) && b->sh[0]->neranges == 1 && prot_locked(b, 0, 3, 3));
  }
  book_free(b);
}
static void solver_file_tests(void)
{
  Book *b = book_new(), *c;
  SolverModel m, l;
  put(b, "B1", "0"); put(b, "B2", "0"); put(b, "B4", "=3*B1+5*B2"); put(b, "C3", "=3*B1+2*B2");
  solver_model_init(&m, 0);
  strcpy(m.objective, "B4"); strcpy(m.vars, "B1:B2"); m.method = SM_SIMPLEX; m.goal = SG_MIN; m.multistart = 1; m.population = 7;
  solver_add(&m, "B1", SR_LE, "4"); solver_add(&m, "C3", SR_GE, "C1"); solver_add(&m, "B1:B2", SR_INT, "");
  solver_save(b, &m);
  c = an_roundtrip(b);
  ok("solver: model in .xlsx", c && solver_load(c, 0, &l) && !strcmp(l.objective, "B4") && l.goal == SG_MIN && l.method == SM_SIMPLEX && l.ncons == 3 &&
     !strcmp(l.cons[1].rhs, "C1") && l.cons[1].rel == SR_GE && l.cons[2].rel == SR_INT && l.multistart && l.population == 7);
  solver_model_free(&l);
  if (c) book_free(c);
  solver_model_free(&m);
  book_free(b);
}

static void analysis_tests(void)
{
  forecast_tests();
  cellctl_tests();
  solver_file_tests();
  solver_tests();
  toolpak_tests();
}
