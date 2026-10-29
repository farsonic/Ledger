/*
 * funcdesc.h - the functions' descriptions for the Insert Function dialog and the Formulas tab's
 * Function Library lists: arguments, one line on what each does, and its category as Excel names
 * them (FC_, in the order of Excel's Insert Function dialog).
 */
#ifndef LEDGER_FUNCDESC_H
#define LEDGER_FUNCDESC_H

enum { FC_ALL, FC_FIN, FC_DATE, FC_MATH, FC_STAT, FC_LOOKUP, FC_DB, FC_TEXT, FC_LOGIC, FC_INFO, FC_ENG, FC_CUBE, FC_COMPAT, FC_WEB, FC_ADDIN, FC_N };
typedef struct { const char *name, *args, *what; unsigned char cat; } FuncDesc;

extern const char *const func_cat_names[FC_N];   /* "All", "Financial", ... */
extern const FuncDesc func_descs[];             /* in name order */
extern const int func_ndescs;
const FuncDesc *func_desc(const char *name);    /* NULL if it has none */
/* the names in category cat (FC_ALL: every one), in name order, at most max; how many */
int func_descs_in(int cat, const char **out, int max);

#endif
