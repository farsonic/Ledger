/* loads the test workbook into Ledger's core, works it out, and prints each case's two results */
#include "core/book.h"
#include "core/calc.h"
#include "core/xlsx.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
double ledger_now(void)
{
  time_t t = time(NULL); struct tm *l = localtime(&t);
  return 25569.0 + (double)(t + l->tm_gmtoff) / 86400.0;
}
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
  FILE *f = fopen(argv[1], "rb"); size_t n; u8 *d; char err[200]; Book *b; i32 r;
  fseek(f, 0, SEEK_END); n = (size_t)ftell(f); rewind(f); d = malloc(n); if (fread(d, 1, n, f) != n) return 1; fclose(f);
  if (!(b = xlsx_load(d, n, err, sizeof err))) { fprintf(stderr, "%s\n", err); return 1; }
  book_recalc(b);
  for (r = 0; r <= sheet_last_row(b->sh[0]); r++) { printf("%ld", (long)r + 1); show(b, r, 1); show(b, r, 2); printf("\n"); }
  return 0;
}
