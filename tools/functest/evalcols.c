/* evalcols.c - loads a workbook into Ledger's core, works it out, and prints the values of the given
   columns of its first sheet, a line a row: the row number, then each value (N: number, S: text,
   B: TRUE/FALSE, E: error, - no cell). Used by mkexample.py to fill in and to check the function
   test workbook.

     evalcols book.xlsx col [col...]       (columns as numbers, 1 = A) */
#include "core/book.h"
#include "core/calc.h"
#include "core/xlsx.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#ifdef __VBCC__
/* (built for the Amiga too, to check the 68040 build gives the same answers: the clock as Ledger reads it) */
#include <dos/dos.h>
#include <proto/dos.h>
double ledger_now(void)
{
  struct DateStamp ds;
  DateStamp(&ds);
  return 28491.0 + ds.ds_Days + ds.ds_Minute / 1440.0 + ds.ds_Tick / (50.0 * 86400.0);
}
#else
double ledger_now(void)
{
  time_t t = time(NULL); struct tm *l = localtime(&t);
  return 25569.0 + (double)(t + l->tm_gmtoff) / 86400.0;
}
#endif
static const char *errs[] = { "", "#NULL!", "#DIV/0!", "#VALUE!", "#REF!", "#NAME?", "#NUM!", "#N/A", "#GETTING_DATA", "#SPILL!", "#CALC!" };
static void show(Book *b, int r, int c)
{
  Cell *cell = sheet_cell(b->sh[0], r, c);
  const Value *v;
  if (!cell) { printf("\t-"); return; }
  v = cell_value(b, 0, r, c);
  switch (v->t) {
  case V_NUM: printf("\tN:%.15g", v->n); break;
  case V_STR: printf("\tS:%s", v->s ? v->s : ""); break;
  case V_BOOL: printf("\tB:%s", v->e ? "TRUE" : "FALSE"); break;
  case V_ERR: printf("\tE:%s", v->e < 11 ? errs[v->e] : "?"); break;
  case V_EMPTY: printf("\tN:0"); break;
  default: printf("\t?:%d", v->t);
  }
}
int main(int argc, char **argv)
{
  FILE *f; size_t n; u8 *d; char err[200]; Book *b; i32 r; int k;
  if (argc < 3 || !(f = fopen(argv[1], "rb"))) { fprintf(stderr, "usage: evalcols book.xlsx col [col...]\n"); return 2; }
  fseek(f, 0, SEEK_END); n = (size_t)ftell(f); rewind(f); d = malloc(n); if (fread(d, 1, n, f) != n) return 1; fclose(f);
  if (!(b = xlsx_load(d, n, err, sizeof err))) { fprintf(stderr, "%s\n", err); return 1; }
  book_recalc(b);
  for (r = 0; r <= sheet_last_row(b->sh[0]); r++) {
    printf("%ld", (long)r + 1);
    for (k = 2; k < argc; k++) show(b, r, atoi(argv[k]) - 1);
    printf("\n");
  }
  return 0;
}
