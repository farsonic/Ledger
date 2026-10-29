/*
 * flash.c - Flash Fill (see data.h): the column being typed in is filled from the column(s) on its
 * left by the pattern its examples show. A pattern is a sequence of pieces: literal text, a word of
 * the source (counted from the start or the end) in the same, upper, lower or proper case, a word's
 * first letter, or a stretch of the source by position. The first pattern found from the first
 * example that gives every example is used.
 *
 * As GridCraft's Flash Fill (crates/engine/src/fill.rs: tokens, greedy segmentation of the output
 * into the longest matching word with literals between, two counting conventions), with patterns by
 * position added.
 */
#include "data.h"
#include "ops.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>

enum { P_LIT, P_TOK, P_INIT, P_SUB };
enum { C_SAME, C_UPPER, C_LOWER, C_PROPER };
typedef struct { u8 kind, kcase; int idx, len; char lit[64]; } Piece;   /* P_TOK/P_INIT: idx (< 0 from the end); P_SUB: idx start (< 0 from the end), len (-1 to the end) */
#define MAXP 32
#define MAXT 32
typedef struct { Piece p[MAXP]; int n; } Prog;
typedef struct { char t[MAXT][128]; int n; } Toks;

static int wordch(u8 c) { return isalnum(c) || c == '\'' || c >= 0x80; }
static void tokens(const char *s, Toks *t)
{
  t->n = 0;
  while (*s && t->n < MAXT) {
    size_t k = 0;
    while (*s && !wordch((u8)*s)) s++;
    if (!*s) break;
    while (*s && wordch((u8)*s)) { if (k < sizeof t->t[0] - 1) t->t[t->n][k++] = *s; s++; }
    t->t[t->n][k] = 0;
    t->n++;
  }
}
static void with_case(const char *s, int kcase, char *out, size_t cap)
{
  size_t i;
  for (i = 0; s[i] && i + 1 < cap; i++) {
    u8 c = (u8)s[i];
    out[i] = (char)(kcase == C_UPPER ? toupper(c) : kcase == C_LOWER ? tolower(c) : kcase == C_PROPER ? (i ? tolower(c) : toupper(c)) : c);
    if (c >= 0x80) out[i] = (char)c;
  }
  out[i] = 0;
}
static const char *tok_at(const Toks *t, int idx) { int i = idx >= 0 ? idx : t->n + idx; return i >= 0 && i < t->n ? t->t[i] : NULL; }
/* the program on src into out; 0 if a piece has nothing to take */
static int apply(const Prog *p, const char *src, char *out, size_t cap)
{
  Toks t;
  size_t o = 0, sl = strlen(src);
  int i;
  char piece[256];
  tokens(src, &t);
  out[0] = 0;
  for (i = 0; i < p->n; i++) {
    const Piece *q = &p->p[i];
    const char *w;
    piece[0] = 0;
    switch (q->kind) {
    case P_LIT: strcpy(piece, q->lit); break;
    case P_TOK: if (!(w = tok_at(&t, q->idx))) return 0; with_case(w, q->kcase, piece, sizeof piece); break;
    case P_INIT: { char one[2]; if (!(w = tok_at(&t, q->idx)) || !w[0]) return 0; one[0] = w[0]; one[1] = 0; with_case(one, q->kcase, piece, sizeof piece); break; }
    case P_SUB: {
      long st = q->idx >= 0 ? q->idx : (long)sl + q->idx, ln = q->len < 0 ? (long)sl - st : q->len;
      if (st < 0 || ln <= 0 || (size_t)(st + ln) > sl || (size_t)ln >= sizeof piece) return 0;
      memcpy(piece, src + st, (size_t)ln); piece[ln] = 0;
      break; }
    }
    if (o + strlen(piece) + 1 > cap) return 0;
    strcpy(out + o, piece); o += strlen(piece);
  }
  return 1;
}
static void add_lit(Prog *p, char ch)
{
  Piece *q;
  if (p->n && p->p[p->n - 1].kind == P_LIT && strlen(p->p[p->n - 1].lit) < sizeof q->lit - 1) {
    size_t k = strlen(p->p[p->n - 1].lit);
    p->p[p->n - 1].lit[k] = ch; p->p[p->n - 1].lit[k + 1] = 0;
    return;
  }
  if (p->n >= MAXP) return;
  q = &p->p[p->n++];
  memset(q, 0, sizeof *q);
  q->kind = P_LIT; q->lit[0] = ch;
}
/* one example's program by greedy segmentation (neg: words counted from the end) */
static void synthesize(const char *src, const char *dst, int neg, Prog *p)
{
  Toks t;
  const char *rest = dst;
  int guard = 0;
  tokens(src, &t);
  p->n = 0;
  while (*rest && guard++ < 128 && p->n < MAXP) {
    int i, c, bestlen = 0;
    Piece best;
    char cand[256];
    memset(&best, 0, sizeof best);
    for (i = 0; i < t.n; i++)
      for (c = C_SAME; c <= C_PROPER; c++) {
        size_t l;
        with_case(t.t[i], c, cand, sizeof cand);
        l = strlen(cand);
        if (l && !strncmp(rest, cand, l) && (int)l > bestlen) { bestlen = (int)l; best.kind = P_TOK; best.kcase = (u8)c; best.idx = neg ? i - t.n : i; }
      }
    if (bestlen <= 1) {
      /* initials ("J. Smith") */
      for (i = 0; i < t.n && !bestlen; i++)
        for (c = C_SAME; c <= C_LOWER && !bestlen; c++) {
          char one[2];
          one[0] = t.t[i][0]; one[1] = 0;
          with_case(one, c, cand, sizeof cand);
          if (cand[0] && rest[0] == cand[0]) { bestlen = 1; best.kind = P_INIT; best.kcase = (u8)c; best.idx = neg ? i - t.n : i; }
        }
    }
    if (bestlen) { p->p[p->n++] = best; rest += bestlen; }
    else add_lit(p, *rest++);
  }
}
static int fits(const Prog *p, const char *const *src, const char *const *dst, int n)
{
  int i;
  char out[1024];
  for (i = 0; i < n; i++) if (!apply(p, src[i], out, sizeof out) || strcmp(out, dst[i])) return 0;
  return 1;
}
/* the pattern for the examples; 0 if none */
static int learn(const char *const *src, const char *const *dst, int n, Prog *p)
{
  int neg;
  const char *s = src[0], *d = dst[0], *at;
  long sl = (long)strlen(s), dl = (long)strlen(d);
  if (n <= 0 || !*d) return 0;
  for (neg = 0; neg < 2; neg++) { synthesize(s, d, neg, p); if (fits(p, src, dst, n)) return 1; }
  /* by position: the output is a stretch of the source, counted from its start or end, to a length or to the end */
  if (dl <= sl && (at = strstr(s, d))) {
    long st = (long)(at - s);
    int k;
    for (k = 0; k < 4; k++) {
      p->n = 1;
      memset(&p->p[0], 0, sizeof p->p[0]);
      p->p[0].kind = P_SUB;
      p->p[0].idx = k & 1 ? (int)(st - sl) : (int)st;
      p->p[0].len = k & 2 ? (st + dl == sl ? -1 : (int)dl) : (int)dl;
      if (fits(p, src, dst, n)) return 1;
    }
  }
  return 0;
}
char *flash_guess(const char *const *src, const char *const *dst, int n, const char *s)
{
  Prog p;
  char out[1024];
  if (!learn(src, dst, n, &p) || !apply(&p, s, out, sizeof out)) return NULL;
  return str_dup(out);
}

/* the text of a cell as shown */
static void shown(Book *b, int sheet, i32 r, i32 c, char *out, size_t cap)
{
  u32 col;
  book_shown_text(b, sheet, r, c, out, cap, &col);
}
static int filled(Book *b, int sheet, i32 r, i32 c) { return c >= 0 && cell_value(b, sheet, r, c)->t != V_EMPTY; }
#define MAXEX 64
int book_flash_fill(Book *b, int sheet, i32 r, i32 c)
{
  i32 top = r, bottom = r, row, c0, sc = c - 1;
  int nex = 0, filledn = 0, k;
  char *src[MAXEX], *dst[MAXEX];
  Prog p;
  if (sheet < 0 || sheet >= b->nsh || c <= 0) return 0;
  /* the source: the filled columns just left of c (joined with spaces) */
  c0 = sc;
  while (top > 0 && filled(b, sheet, top - 1, sc)) top--;
  while (bottom + 1 < MAXROW && filled(b, sheet, bottom + 1, sc) && bottom - top < 100000) bottom++;
  if (!filled(b, sheet, r, sc) && top == bottom) return 0;
  while (c0 > 0 && c - c0 < 8 && filled(b, sheet, top, c0 - 1)) c0--;
  /* the examples: rows already typed in */
  for (row = top; row <= bottom && nex < MAXEX; row++) {
    char t[512], s[1024];
    i32 k2;
    if (!filled(b, sheet, row, c)) continue;
    s[0] = 0;
    for (k2 = c0; k2 <= sc; k2++) { shown(b, sheet, row, k2, t, sizeof t); if (k2 > c0) strcat(s, " "); if (strlen(s) + strlen(t) < sizeof s - 2) strcat(s, t); }
    shown(b, sheet, row, c, t, sizeof t);
    src[nex] = str_dup(s); dst[nex] = str_dup(t);
    if (!src[nex] || !dst[nex]) { free(src[nex]); free(dst[nex]); break; }
    nex++;
  }
  if (!nex || !learn((const char *const *)src, (const char *const *)dst, nex, &p)) goto out;
  book_begin(b);
  for (row = top; row <= bottom; row++) {
    char t[512], s[1024], out[1024];
    i32 k2;
    Value v;
    if (filled(b, sheet, row, c) || (sheet_cell(b->sh[sheet], row, c) && sheet_cell(b->sh[sheet], row, c)->f)) continue;
    s[0] = 0;
    for (k2 = c0; k2 <= sc; k2++) { shown(b, sheet, row, k2, t, sizeof t); if (k2 > c0) strcat(s, " "); if (strlen(s) + strlen(t) < sizeof s - 2) strcat(s, t); }
    if (!apply(&p, s, out, sizeof out) || !out[0]) continue;
    v = val_str(out);
    book_set_value(b, sheet, row, c, &v);
    val_free(&v);
    filledn++;
  }
  book_end(b);
out:
  for (k = 0; k < nex; k++) { free(src[k]); free(dst[k]); }
  return filledn;
}
