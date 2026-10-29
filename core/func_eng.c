/*
 * func_eng.c - worksheet functions (see func.c for the conventions). Its table, func_eng_defs, is in
 * name order and merged with the others by func_init.
 */
#include "calc.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>

static Value f_none(Ctx *cx, Value *a, int n) { (void)cx; (void)a; (void)n; return val_err(E_NAME); }
const FuncDef func_eng_defs[] = {
  { "", 0, 0, 0, f_none },
};
const int func_eng_count = 0;
