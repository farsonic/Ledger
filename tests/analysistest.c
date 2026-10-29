/*
 * analysistest.c - tests of analysis.h (included by coretest.c): Solver on known LP, integer and
 * nonlinear problems, the Data Analysis tools against known values and the worksheet functions,
 * Forecast Sheet, checkboxes, Lock Cell and Allow Edit Ranges, and their .xlsx round trips.
 */
#include "../core/analysis.h"

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

static void analysis_tests(void)
{
  solver_tests();
}
