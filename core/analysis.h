/*
 * analysis.h - the Data tab's Analysis group and its neighbours: Solver (solver.c), the Analysis
 * ToolPak's Data Analysis tools (toolpak.c), Forecast Sheet (forecast.c), and the cell work in
 * cellctl.c: checkbox cell controls (Insert > Checkbox), Lock Cell (Home > Format) and Allow Edit
 * Ranges (Review > Protect).
 *
 * The behaviour follows GridCraft's (crates/engine/src/cmd/solver*, analysis.rs, forecast.rs,
 * extra/cells.rs and its xlsx crate's feature property bags), which follows Excel's; the C here is
 * written from that documented behaviour (GridCraft is MIT licensed, see Licenses.txt).
 */
#ifndef LEDGER_ANALYSIS_H
#define LEDGER_ANALYSIS_H

#include "book.h"
#include "xml.h"

/* ------------------------------------------------------------------ Solver (solver.c) */
enum { SG_MAX, SG_MIN, SG_VALUE };                   /* what the objective cell should do */
enum { SM_GRG, SM_SIMPLEX, SM_EVO };                 /* GRG Nonlinear, Simplex LP, Evolutionary */
enum { SR_LE = 1, SR_EQ, SR_GE, SR_INT, SR_BIN, SR_DIF };   /* constraint relations, numbered as Excel's solver_relN */
/* a constraint as typed: cells (lhs, "B2:B4"), the relation, and the right side ("10", "C2:C4";
   empty for int, bin and dif) */
typedef struct { char lhs[128]; u8 rel; char rhs[128]; } SolverCons;
typedef struct {
  int sheet;                       /* the sheet the model lives on (its cells are all there) */
  char objective[64];              /* "B10" */
  u8 goal; double value;           /* SG_; the value for SG_VALUE */
  char vars[256];                  /* the variable cells, "B2:B4,D1" */
  SolverCons *cons; int ncons;
  u8 nonneg, method;               /* unconstrained variables made non-negative; SM_ */
  /* options */
  long maxevals;                   /* the evaluation budget (0: 20,000) */
  double intgap;                   /* integer optimality, per cent (0: the exact optimum) */
  u8 multistart;                   /* GRG: start from several points too and keep the best */
  int population;                  /* multistart starts and the Evolutionary population (0: the default) */
  u32 seed;                        /* random seed (multistart, Evolutionary); 0 a fixed one */
} SolverModel;
void solver_model_init(SolverModel *m, int sheet);  /* max, non-negative, GRG, no constraints */
void solver_model_free(SolverModel *m);
int  solver_model_copy(SolverModel *dst, const SolverModel *src);
int  solver_add(SolverModel *m, const char *lhs, int rel, const char *rhs);   /* 0 out of memory */
void solver_remove(SolverModel *m, int i);
void solver_cons_text(const SolverCons *c, char *out, size_t cap);           /* "B5 <= 10", "B2 int" */
/* a constraint typed as one text ("B5 <= 10", "B2:B4 int"); 0 if it isn't one */
int  solver_parse_cons(const char *text, SolverCons *c);
/* the sheet's saved model (its hidden solver_ names), 0 if it has none; and saved */
int  solver_load(Book *b, int sheet, SolverModel *m);
int  solver_save(Book *b, const SolverModel *m);

/* what a solve came to */
enum { SS_OPTIMAL, SS_CONVERGED, SS_CANNOTIMPROVE, SS_LIMITFEAS, SS_LIMIT, SS_INFEASIBLE, SS_UNBOUNDED, SS_NOTLINEAR, SS_BADMODEL };
typedef struct {
  int status;                      /* SS_ */
  char message[200];               /* Solver Results' sentence (or why the model is no good) */
  int n; i32 *vr, *vc;             /* the variable cells */
  double *orig, *sol;              /* their values before, and the solution (sol NULL: none) */
  double objective, objorig;       /* the objective's value at the solution and before */
  long evals;
} SolverResult;
void solver_result_free(SolverResult *r);
/* works the model out without changing the workbook (the cells are put back). Returns the status */
int  solver_solve(Book *b, const SolverModel *m, SolverResult *r);
/* Solver Results' OK: the solution kept in the variable cells (keep; else the original values
   stay) and the model saved, one undo step */
int  solver_finish(Book *b, const SolverModel *m, const SolverResult *r, int keep, int save);
/* an Answer (kind 0) or Limits (kind 1) report on a new sheet before the model's; its index, or
   -1 (*why says why) */
int  solver_report(Book *b, const SolverModel *m, const SolverResult *r, int kind, const char **why);
const char *solver_method_name(int method);

/* the dense simplex and branch and bound underneath, for the tests: minimise c.x subject to
   rows (a[i] . x  sense[i]  rhs[i], sense SR_LE/SR_EQ/SR_GE) and lo <= x <= hi, integer[j] for
   whole numbers. 0 solved (x, *obj), 1 infeasible, 2 unbounded, 3 out of work */
int  solver_lp(int n, const double *c, int m, const double *a, const u8 *sense, const double *rhs,
               const double *lo, const double *hi, const u8 *integer, double *x, double *obj);

/* shared helpers: a range typed (a reference or a name; whole columns cut to what is used) on
   defsheet or the sheet it names (toolpak.c); a new sheet named base ("Sheet4" when NULL) at index
   at, or at the end when sheet-scoped names would lose their sheet (solver.c) */
int  an_range(Book *b, int defsheet, const char *text, int *sheet, Rect *r);
int  an_add_sheet(Book *b, const char *base, int at);

/* ------------------------------------------------------------------ Data Analysis (toolpak.c) */
enum {
  TP_ANOVA1, TP_ANOVA2R, TP_ANOVA2, TP_CORREL, TP_COVAR, TP_DESCR, TP_EXPSMOOTH, TP_FTEST, TP_FOURIER, TP_HISTOGRAM,
  TP_MOVAVG, TP_RANDOM, TP_RANK, TP_REGRESSION, TP_SAMPLING, TP_TPAIRED, TP_TEQUAL, TP_TUNEQUAL, TP_ZTEST, TP_N
};
extern const char *const toolpak_names[TP_N];        /* "Anova: Single Factor" ... in Excel's order */
enum { TPD_UNIFORM, TPD_NORMAL, TPD_BERNOULLI, TPD_BINOMIAL, TPD_POISSON, TPD_PATTERNED, TPD_DISCRETE, TPD_N };
typedef struct {
  int tool;                        /* TP_ */
  char in1[160], in2[160];         /* input range (Variable 1, Input Y); second (Variable 2, Input X, Bin Range, value and probability range) */
  u8 byrows, labels;               /* grouped by rows; labels in the first row (column) */
  double alpha;                    /* 0.05 */
  double conf;                     /* confidence level, per cent (descriptive: 0 none; regression 95) */
  double hmd;                      /* hypothesized mean difference */
  double var1, var2;               /* z-test: the known variances */
  double damping;                  /* exponential smoothing: 0.3 */
  int interval;                    /* moving average: 3 */
  int rowsper;                     /* two-factor with replication: rows per sample */
  int kl, ks;                      /* descriptive: Kth largest and smallest (0 none) */
  u8 summary, pareto, cumulative, stderrs, constzero, residuals, stdres, normprob, inverse, periodic;
  int nvars, nnums, dist;          /* random numbers: variables, numbers of each, TPD_ */
  double p1, p2, p3, p4, p5;       /* the distribution's parameters (uniform a b; normal mean sd; Bernoulli p; binomial p n;
                                      Poisson lambda; patterned from to step repeat-number repeat-sequence) */
  u32 seed;                        /* random numbers and sampling: 0 a different run each time */
  int nsample;                     /* sampling: the period or the number of samples */
  u8 outnew;                       /* output to a new sheet (else at out) */
  char out[64], newsheet[32];      /* output range's top left cell; the new sheet's name ("" a default) */
  u8 overwrite;                    /* replace what the output range holds */
} TPParams;
void toolpak_defaults(TPParams *p, int tool);
/* runs the tool: the report written as values (one undo step). Returns the report's sheet, or -1
   with *why; -2 when the output range holds data and overwrite isn't set */
int  toolpak_run(Book *b, int sheet, const TPParams *p, Rect *where, const char **why);

/* ------------------------------------------------------------------ Forecast Sheet (forecast.c) */
typedef struct {
  char timeline[160], values[160];  /* the two ranges (a text first cell is the heading) */
  double end;                       /* the forecast's last point (a timeline value; 0: a third of the history on) */
  int seasonality;                  /* -1 detected automatically, 0 none, else the season's length */
  double conf;                      /* confidence interval (0.95), 0 none */
  int fill;                         /* missing points: 1 interpolated, 0 zeros */
  int aggregate;                    /* duplicate times: 1 average, 2 count, 3 counta, 4 max, 5 median, 6 min, 7 sum */
  u8 stats, column;                 /* the FORECAST.ETS.STAT table; a column chart (else a line) */
} ForecastOpts;
void forecast_defaults(ForecastOpts *o);
/* a new sheet before the source's: the timeline and values, FORECAST.ETS and its confidence bounds,
   a table and a chart. Its index, or -1 (*why) */
int  forecast_sheet(Book *b, int sheet, const ForecastOpts *o, const char **why);
/* the timeline's step and its last point; 0 if it isn't one with a constant step (for the dialog) */
int  forecast_timeline(Book *b, int sheet, const char *timeline, double *last, double *step, int *npoints);

/* ------------------------------------------------------------------ cell controls and protection (cellctl.c) */
#define CHECKBOX_FMT "checkbox"     /* a checkbox cell's number format, as GridCraft keeps it */
int  cell_is_checkbox(Book *b, int sheet, i32 r, i32 c);
/* Insert > Checkbox: the area's cells become checkboxes (centred; empty ones FALSE). An undo step */
int  book_checkbox(Book *b, int sheet, Rect area);
/* a checkbox cell's TRUE and FALSE swapped (empty counts as FALSE); 0 if it isn't one or holds a formula */
int  book_checkbox_toggle(Book *b, int sheet, i32 r, i32 c);
/* Lock Cell: the area's cells locked (on) or unlocked; whether cell r, c is locked */
int  book_lock_cells(Book *b, int sheet, Rect area, int on);
int  cell_locked(Book *b, int sheet, i32 r, i32 c);

/* Allow Edit Ranges: cells that stay editable on a protected sheet, with or without a password */
typedef struct EditRange {
  char title[64];                   /* UTF-8 */
  Rect *areas; int nareas;
  u16 pwhash;                       /* the password's legacy hash (0 none) */
  char key[144];                    /* or a newer file's check, "SHA-512:spin:salt:hash" */
  u8 unlocked;                      /* the password was given (while Ledger runs) */
} EditRange;
int  book_edit_range_set(Book *b, int sheet, int i, const char *title, const Rect *areas, int n, const char *pw, int keep_pw);  /* i < 0 adds */
int  book_edit_range_delete(Book *b, int sheet, int i);
int  edit_range_has_pw(const EditRange *e);
int  edit_range_unlock(EditRange *e, const char *pw);   /* 0 wrong password */
/* the edit range holding r, c (NULL none); whether every cell of a is in ranges that are open
   (no password, or unlocked); and the first password range holding part of a that is still shut */
EditRange *edit_range_at(Sheet *s, i32 r, i32 c);
int  edit_ranges_open(Sheet *s, Rect a);
EditRange *edit_range_shut(Sheet *s, Rect a);
/* (valid.c's hooks) the sheet's ranges freed, copied, moved with inserted and deleted lines */
void edit_ranges_free(Sheet *s);
void edit_ranges_clone(Sheet *dst, const Sheet *src);
void edit_ranges_shift(Sheet *s, int rows, i32 at, i32 n);
/* .xlsx: <protectedRanges> written; a <protectedRange> read */
void edit_ranges_xlsx_write(const Sheet *s, Buf *o);
void edit_ranges_xlsx_read(Sheet *s, Xml *x);
/* .xlsx checkboxes: the feature property bag part and the xf extension Excel uses ([MS-XLSX] 2.1.22) */
#define CHECKBOX_PART "xl/featurePropertyBag/featurePropertyBag.xml"
int  checkbox_used(Book *b);                         /* whether any style is a checkbox's */
void checkbox_part(Buf *o);                          /* the part's XML */
void checkbox_xf_ext(Buf *o);                        /* an xf's <extLst> */
/* which xfComplement indexes are checkboxes, from the part's XML: flags[i] set; how many read */
int  checkbox_complements(const char *xml, size_t len, u8 *flags, int max);

#endif
