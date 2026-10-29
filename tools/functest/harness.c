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
static void show_in(Book *b, int sh, int r, int c, const char *sep)
{
  Cell *cell = sheet_cell(b->sh[sh], r, c);
  const Value *v;
  if (!cell) { printf("%s-", sep); return; }
  v = cell_value(b, sh, r, c);
  switch (v->t) {
  case V_NUM: printf("%sN:%.15g", sep, v->n); break;
  case V_STR: printf("%sS:%s", sep, v->s ? v->s : ""); break;
  case V_BOOL: printf("%sB:%s", sep, v->e ? "TRUE" : "FALSE"); break;
  case V_ERR: printf("%sE:%s", sep, v->e < 11 ? errs[v->e] : "?"); break;
  case V_EMPTY: printf("%sN:0", sep); break;
  default: printf("%s?:%d", sep, v->t);
  }
}
static void show(Book *b, int r, int c) { show_in(b, 0, r, c, "\t"); }
/* a spilling case on sheet S: the size of its spill, then its values row by row */
static void show_spill(Book *b, int sh, int r, int c)
{
  Rect a;
  int blocked = 0;
  i32 i, j;
  Cell *cell = sheet_cell(b->sh[sh], r, c);
  if (!cell || !cell->f) { printf("\t-"); return; }
  cell_value(b, sh, r, c);
  if (!book_spill_range(b, sh, r, c, &a, &blocked) || blocked) { a.r1 = a.r2 = r; a.c1 = a.c2 = c; }
  printf("\t%ldx%ld", (long)(a.r2 - a.r1 + 1), (long)(a.c2 - a.c1 + 1));
  for (i = a.r1; i <= a.r2; i++) for (j = a.c1; j <= a.c2; j++) show_in(b, sh, i, j, "|");
}
int main(int argc, char **argv)
{
  FILE *f = fopen(argv[1], "rb"); size_t n; u8 *d; char err[200]; Book *b; i32 r;
  fseek(f, 0, SEEK_END); n = (size_t)ftell(f); rewind(f); d = malloc(n); if (fread(d, 1, n, f) != n) return 1; fclose(f);
  if (!(b = xlsx_load(d, n, err, sizeof err))) { fprintf(stderr, "%s\n", err); return 1; }
  book_recalc(b);
  for (r = 0; r <= sheet_last_row(b->sh[0]); r++) { printf("%ld", (long)r + 1); show(b, r, 1); show(b, r, 2); printf("\n"); }
  {
    int sh = book_find_sheet(b, "S");
    for (r = 0; sh >= 0 && r <= sheet_last_row(b->sh[sh]); r++) {
      Cell *name = sheet_cell(b->sh[sh], r, 0);
      if (!name || name->v.t != V_STR) continue;
      printf("S%ld", (long)r + 1); show_spill(b, sh, r, 1); show_spill(b, sh, r, 11); printf("\n");
    }
  }
  return 0;
}
