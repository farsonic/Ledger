/*
 * xlsxtool.c - a native helper for checking files against other programs: loads an .xlsx (or
 * .csv), prints every cell (address, formula, what it shows) and optionally saves it again.
 *   xlsxtool in.xlsx [out.xlsx]
 */
#include "../core/book.h"
#include "../core/calc.h"
#include "../core/xlsx.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

double ledger_now(void) { return 46322.5; }

int main(int argc, char **argv)
{
  FILE *fp;
  u8 *data;
  long len;
  char err[200] = "";
  Book *b;
  int s;
  if (argc < 2) { fprintf(stderr, "usage: xlsxtool in.xlsx [out.xlsx]\n"); return 2; }
  fp = fopen(argv[1], "rb");
  if (!fp) { perror(argv[1]); return 1; }
  fseek(fp, 0, SEEK_END); len = ftell(fp); fseek(fp, 0, SEEK_SET);
  data = malloc(len + 1);
  if (fread(data, 1, len, fp) != (size_t)len) { fclose(fp); return 1; }
  fclose(fp);
  if (strstr(argv[1], ".csv")) b = csv_load((char *)data, len, 0);
  else b = xlsx_load(data, len, err, sizeof err);
  free(data);
  if (!b) { fprintf(stderr, "load failed: %s\n", err); return 1; }
  for (s = 0; s < b->nsh; s++) {
    Sheet *sh = b->sh[s];
    i32 r;
    printf("== sheet %s (freeze %d,%d, merges %d)\n", sh->name, sh->freezer, sh->freezec, sh->nmerges);
    for (r = 0; r < sh->nrows; r++) {
      Row *row = sh->rows[r];
      int i;
      if (!row) continue;
      for (i = 0; i < row->n; i++) {
        Cell *c = &row->c[i];
        char ref[16], out[256];
        u32 col;
        const Value *v = cell_value(b, s, r, c->col);
        Style *st = &b->styles[c->style];
        ref_text(r, c->col, 0, 0, ref);
        fmt_value(b, book_fmt_code(b, st->numfmt), v, out, sizeof out, &col, 11);
        printf("%s", ref);
        if (c->f) { Buf t; buf_init(&t); formula_text(b, s, c->f, &t); printf(" =%s", t.data ? t.data : ""); buf_free(&t); }
        printf(" -> [%s]", out);
        if (st->font || st->fill) printf(" font=%s%s fill=%06lX", b->fonts[st->font].name, b->fonts[st->font].bold ? " bold" : "", (unsigned long)(st->fill & 0xFFFFFF));
        printf("\n");
      }
    }
  }
  for (s = 0; s < b->nnames; s++) printf("name %s = %s\n", b->names[s].name, b->names[s].formula_text ? b->names[s].formula_text : "?");
  if (argc > 2) {
    size_t n;
    u8 *out = xlsx_save(b, &n);
    fp = out ? fopen(argv[2], "wb") : NULL;
    if (!fp || fwrite(out, 1, n, fp) != n) { fprintf(stderr, "save failed\n"); return 1; }
    fclose(fp);
    free(out);
  }
  book_free(b);
  return 0;
}
