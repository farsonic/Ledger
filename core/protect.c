/*
 * protect.c - sheet and workbook protection (see data.h). A protected sheet refuses changes to its
 * locked cells (every cell is locked unless Format Cells' Protection says otherwise) and hides the
 * formulas of cells marked hidden; what users may still do is a set of PA_ allowances. Passwords
 * are kept only as a hash: Excel's legacy 16-bit one (written to files), or the iterated SHA-512
 * newer files carry, which is checked but never made.
 */
#include "data.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* ------------------------------------------------------------------ hashes */
u16 prot_hash(const char *pw)
{
  u16 bytes[256], h = 0;
  int n = 0, i, used;
  size_t len;
  if (!pw || !*pw) return 0;
  len = strlen(pw);
  /* each character's UTF-16 low byte (its high byte when the low one is 0) */
  for (i = 0; (size_t)i < len && n < 255; i += used) {
    u32 cp = utf8_get(pw + i, len - (size_t)i, &used);
    u16 u = (u16)(cp > 0xFFFF ? 0xFFFD : cp);
    bytes[n++] = (u16)((u & 0xFF) ? (u & 0xFF) : (u >> 8));
  }
#define ROT(v) ((u16)((((v) >> 14) & 1) | (((v) << 1) & 0x7FFF)))
  for (i = n - 1; i >= 0; i--) h = (u16)(ROT(h) ^ bytes[i]);
  h = (u16)(ROT(h) ^ (u16)n);
#undef ROT
  return (u16)(h ^ 0xCE4B);
}

/* SHA-512, for the newer files' password check */
typedef unsigned long long u64;
static const u64 K512[80] = {
  0x428a2f98d728ae22ULL, 0x7137449123ef65cdULL, 0xb5c0fbcfec4d3b2fULL, 0xe9b5dba58189dbbcULL, 0x3956c25bf348b538ULL, 0x59f111f1b605d019ULL,
  0x923f82a4af194f9bULL, 0xab1c5ed5da6d8118ULL, 0xd807aa98a3030242ULL, 0x12835b0145706fbeULL, 0x243185be4ee4b28cULL, 0x550c7dc3d5ffb4e2ULL,
  0x72be5d74f27b896fULL, 0x80deb1fe3b1696b1ULL, 0x9bdc06a725c71235ULL, 0xc19bf174cf692694ULL, 0xe49b69c19ef14ad2ULL, 0xefbe4786384f25e3ULL,
  0x0fc19dc68b8cd5b5ULL, 0x240ca1cc77ac9c65ULL, 0x2de92c6f592b0275ULL, 0x4a7484aa6ea6e483ULL, 0x5cb0a9dcbd41fbd4ULL, 0x76f988da831153b5ULL,
  0x983e5152ee66dfabULL, 0xa831c66d2db43210ULL, 0xb00327c898fb213fULL, 0xbf597fc7beef0ee4ULL, 0xc6e00bf33da88fc2ULL, 0xd5a79147930aa725ULL,
  0x06ca6351e003826fULL, 0x142929670a0e6e70ULL, 0x27b70a8546d22ffcULL, 0x2e1b21385c26c926ULL, 0x4d2c6dfc5ac42aedULL, 0x53380d139d95b3dfULL,
  0x650a73548baf63deULL, 0x766a0abb3c77b2a8ULL, 0x81c2c92e47edaee6ULL, 0x92722c851482353bULL, 0xa2bfe8a14cf10364ULL, 0xa81a664bbc423001ULL,
  0xc24b8b70d0f89791ULL, 0xc76c51a30654be30ULL, 0xd192e819d6ef5218ULL, 0xd69906245565a910ULL, 0xf40e35855771202aULL, 0x106aa07032bbd1b8ULL,
  0x19a4c116b8d2d0c8ULL, 0x1e376c085141ab53ULL, 0x2748774cdf8eeb99ULL, 0x34b0bcb5e19b48a8ULL, 0x391c0cb3c5c95a63ULL, 0x4ed8aa4ae3418acbULL,
  0x5b9cca4f7763e373ULL, 0x682e6ff3d6b2b8a3ULL, 0x748f82ee5defb2fcULL, 0x78a5636f43172f60ULL, 0x84c87814a1f0ab72ULL, 0x8cc702081a6439ecULL,
  0x90befffa23631e28ULL, 0xa4506cebde82bde9ULL, 0xbef9a3f7b2c67915ULL, 0xc67178f2e372532bULL, 0xca273eceea26619cULL, 0xd186b8c721c0c207ULL,
  0xeada7dd6cde0eb1eULL, 0xf57d4f7fee6ed178ULL, 0x06f067aa72176fbaULL, 0x0a637dc5a2c898a6ULL, 0x113f9804bef90daeULL, 0x1b710b35131c471bULL,
  0x28db77f523047d84ULL, 0x32caab7b40c72493ULL, 0x3c9ebe0a15c9bebcULL, 0x431d67c49c100d4cULL, 0x4cc5d4becb3e42b6ULL, 0x597f299cfc657e2aULL,
  0x5fcb6fab3ad6faecULL, 0x6c44198c4a475817ULL };
#define ROR(x, n) (((x) >> (n)) | ((x) << (64 - (n))))
static void sha512_block(u64 *h, const u8 *p)
{
  u64 w[80], a, b, c, d, e, f, g, hh, t1, t2;
  int i;
  for (i = 0; i < 16; i++) {
    int k;
    w[i] = 0;
    for (k = 0; k < 8; k++) w[i] = (w[i] << 8) | p[i * 8 + k];
  }
  for (i = 16; i < 80; i++) {
    u64 s0 = ROR(w[i - 15], 1) ^ ROR(w[i - 15], 8) ^ (w[i - 15] >> 7), s1 = ROR(w[i - 2], 19) ^ ROR(w[i - 2], 61) ^ (w[i - 2] >> 6);
    w[i] = w[i - 16] + s0 + w[i - 7] + s1;
  }
  a = h[0]; b = h[1]; c = h[2]; d = h[3]; e = h[4]; f = h[5]; g = h[6]; hh = h[7];
  for (i = 0; i < 80; i++) {
    t1 = hh + (ROR(e, 14) ^ ROR(e, 18) ^ ROR(e, 41)) + ((e & f) ^ (~e & g)) + K512[i] + w[i];
    t2 = (ROR(a, 28) ^ ROR(a, 34) ^ ROR(a, 39)) + ((a & b) ^ (a & c) ^ (b & c));
    hh = g; g = f; f = e; e = d + t1; d = c; c = b; b = a; a = t1 + t2;
  }
  h[0] += a; h[1] += b; h[2] += c; h[3] += d; h[4] += e; h[5] += f; h[6] += g; h[7] += hh;
}
/* the SHA-512 of n bytes (at most a few hundred: the cases here) */
static void sha512(const u8 *msg, size_t n, u8 out[64])
{
  static const u64 init[8] = { 0x6a09e667f3bcc908ULL, 0xbb67ae8584caa73bULL, 0x3c6ef372fe94f82bULL, 0xa54ff53a5f1d36f1ULL,
                               0x510e527fade682d1ULL, 0x9b05688c2b3e6c1fULL, 0x1f83d9abfb41bd6bULL, 0x5be0cd19137e2179ULL };
  u64 h[8];
  u8 block[128];
  size_t done = 0;
  int i;
  memcpy(h, init, sizeof h);
  while (n - done >= 128) { sha512_block(h, msg + done); done += 128; }
  memset(block, 0, sizeof block);
  memcpy(block, msg + done, n - done);
  block[n - done] = 0x80;
  if (n - done >= 112) { sha512_block(h, block); memset(block, 0, sizeof block); }
  for (i = 0; i < 8; i++) block[127 - i] = (u8)((u64)(n * 8) >> (8 * i));
  sha512_block(h, block);
  for (i = 0; i < 64; i++) out[i] = (u8)(h[i / 8] >> (56 - 8 * (i % 8)));
}
static int b64v(int c) { return c >= 'A' && c <= 'Z' ? c - 'A' : c >= 'a' && c <= 'z' ? c - 'a' + 26 : c >= '0' && c <= '9' ? c - '0' + 52 : c == '+' ? 62 : c == '/' ? 63 : -1; }
static int b64dec(const char *s, u8 *out, int cap)
{
  int n = 0, bits = 0, v;
  u32 acc = 0;
  for (; *s && *s != ':'; s++) {
    if ((v = b64v((u8)*s)) < 0) continue;
    acc = (acc << 6) | (u32)v; bits += 6;
    if (bits >= 8) { bits -= 8; if (n < cap) out[n++] = (u8)(acc >> bits); }
  }
  return n;
}
/* the password against a key "SHA-512:spin:salt:hash" */
static int key_ok(const char *key, const char *pw)
{
  u8 salt[64], want[64], buf[64 + 600], h[64];
  int ns, nw, k, used;
  long spin, i;
  size_t len, n = 0;
  const char *p = key;
  if (strncmp(p, "SHA-512:", 8)) return 0;
  p += 8;
  spin = atol(p);
  if (spin < 0 || spin > 10000000L || !(p = strchr(p, ':'))) return 0;
  ns = b64dec(++p, salt, sizeof salt);
  if (!(p = strchr(p, ':'))) return 0;
  nw = b64dec(++p, want, sizeof want);
  if (nw != 64) return 0;
  memcpy(buf, salt, (size_t)ns); n = (size_t)ns;
  len = pw ? strlen(pw) : 0;
  for (k = 0; (size_t)k < len && n + 2 <= sizeof buf; k += used) {
    u32 cp = utf8_get(pw + k, len - (size_t)k, &used);
    if (cp > 0xFFFF) cp = 0xFFFD;
    buf[n++] = (u8)cp; buf[n++] = (u8)(cp >> 8);
  }
  sha512(buf, n, h);
  for (i = 0; i < spin; i++) {
    memcpy(buf, h, 64);
    buf[64] = (u8)i; buf[65] = (u8)(i >> 8); buf[66] = (u8)(i >> 16); buf[67] = (u8)(i >> 24);
    sha512(buf, 68, h);
  }
  return !memcmp(h, want, 64);
}
static int password_ok(u16 hash, const char *key, const char *pw)
{
  if (!hash && !key[0]) return 1;
  if (hash) return prot_hash(pw) == hash;
  return key_ok(key, pw);
}

/* ------------------------------------------------------------------ cells */
int prot_locked(Book *b, int sheet, i32 r, i32 c)
{
  Sheet *s;
  if (sheet < 0 || sheet >= b->nsh || !(s = b->sh[sheet])->protect) return 0;
  return b->styles[sheet_cell_style(s, r, c)].locked != 0;
}
int prot_hidden(Book *b, int sheet, i32 r, i32 c)
{
  Sheet *s;
  if (sheet < 0 || sheet >= b->nsh || !(s = b->sh[sheet])->protect) return 0;
  return b->styles[sheet_cell_style(s, r, c)].hidden != 0;
}
int prot_area_locked(Book *b, int sheet, Rect a)
{
  Sheet *s;
  i32 r, c, last;
  int i;
  if (sheet < 0 || sheet >= b->nsh || !(s = b->sh[sheet])->protect) return 0;
  /* the cells there are; then the rows' and columns' own styles cover the empty ones */
  last = a.r2 < s->nrows - 1 ? a.r2 : s->nrows - 1;
  for (r = a.r1; r <= last; r++) {
    Row *row = s->rows[r];
    if (!row) continue;
    for (i = 0; i < row->n; i++) if (row->c[i].col >= a.c1 && row->c[i].col <= a.c2 && b->styles[row->c[i].style].locked) return 1;
    if (row->style && b->styles[row->style].locked && row->n < a.c2 - a.c1 + 1) return 1;
  }
  for (c = a.c1; c <= a.c2 && c < a.c1 + MAXCOL; c++) {
    int st = c < s->ncols ? s->cols[c].style : 0;
    if (b->styles[st].locked) {
      /* an empty cell takes its column's style unless its row has one */
      for (r = a.r1; r <= a.r2; r++) {
        Row *row = r < s->nrows ? s->rows[r] : NULL;
        if (!row) return 1;
        if (row->style) continue;
        if (!sheet_cell(s, r, c)) return 1;
        if (r > a.r1 + 200000) return 1;
      }
    }
  }
  return 0;
}

/* ------------------------------------------------------------------ protecting */
int book_protect_sheet(Book *b, int sheet, const char *pw, u16 allow)
{
  Sheet *s;
  if (sheet < 0 || sheet >= b->nsh) return 0;
  s = b->sh[sheet];
  book_begin(b);
  book_note_sheet(b, sheet);
  s->protect = 1; s->pwhash = prot_hash(pw); s->protallow = allow; s->protkey[0] = 0;
  book_end(b);
  b->dirty = 1; b->changes++;
  return 1;
}
int book_unprotect_sheet(Book *b, int sheet, const char *pw)
{
  Sheet *s;
  if (sheet < 0 || sheet >= b->nsh) return 0;
  s = b->sh[sheet];
  if (!s->protect) return 1;
  if (!password_ok(s->pwhash, s->protkey, pw)) return 0;
  book_begin(b);
  book_note_sheet(b, sheet);
  s->protect = 0; s->pwhash = 0; s->protkey[0] = 0;
  book_end(b);
  b->dirty = 1; b->changes++;
  return 1;
}
int book_protect_book(Book *b, const char *pw)
{
  b->lockstruct = 1; b->bookpwhash = prot_hash(pw); b->bookkey[0] = 0;
  b->dirty = 1; b->changes++;
  return 1;
}
int book_unprotect_book(Book *b, const char *pw)
{
  if (!b->lockstruct) return 1;
  if (!password_ok(b->bookpwhash, b->bookkey, pw)) return 0;
  b->lockstruct = 0; b->bookpwhash = 0; b->bookkey[0] = 0;
  b->dirty = 1; b->changes++;
  return 1;
}

/* ------------------------------------------------------------------ .xlsx */
/* sheetProtection's attributes: those that say a thing is protected default to 1 (allowed: "0");
   the two selection ones default to 0 (refused: "1") */
static const struct { const char *name; u16 bit; } pattrs[] = {
  { "formatCells", PA_FMTCELLS }, { "formatColumns", PA_FMTCOLS }, { "formatRows", PA_FMTROWS }, { "insertColumns", PA_INSCOLS },
  { "insertRows", PA_INSROWS }, { "insertHyperlinks", PA_INSLINKS }, { "deleteColumns", PA_DELCOLS }, { "deleteRows", PA_DELROWS },
  { "sort", PA_SORT }, { "autoFilter", PA_FILTER }, { "pivotTables", PA_PIVOT } };
#define NPATTRS ((int)(sizeof pattrs / sizeof pattrs[0]))
/* a key "SHA-512:spin:salt:hash" written back as the file's four attributes, prefix before each name */
static void key_attrs(const char *key, const char *prefix, Buf *o)
{
  char algo[32], spin[16], salt[100], hash[120];
  if (sscanf(key, "%31[^:]:%15[^:]:%99[^:]:%119s", algo, spin, salt, hash) != 4) return;
  buf_printf(o, " %salgorithmName=\"%s\" %shashValue=\"%s\" %ssaltValue=\"%s\" %sspinCount=\"%s\"", prefix, algo, prefix, hash, prefix, salt, prefix, spin);
}
static void key_read(Xml *x, const char *prefix, char *key, size_t cap)
{
  char n[4][48];
  const char *a, *h, *s, *sp;
  snprintf(n[0], 48, "%salgorithmName", prefix); snprintf(n[1], 48, "%shashValue", prefix);
  snprintf(n[2], 48, "%ssaltValue", prefix); snprintf(n[3], 48, "%sspinCount", prefix);
  a = xml_attr(x, n[0]); h = xml_attr(x, n[1]); s = xml_attr(x, n[2]); sp = xml_attr(x, n[3]);
  key[0] = 0;
  if (a && h && s && strlen(a) + strlen(h) + strlen(s) + 20 < cap) snprintf(key, cap, "%s:%s:%s:%s", a, sp ? sp : "0", s, h);
}
void prot_xlsx_write_sheet(const Sheet *s, Buf *o)
{
  int i;
  if (!s->protect) return;
  buf_adds(o, "<sheetProtection");
  if (s->pwhash) buf_printf(o, " password=\"%04X\"", s->pwhash);
  else if (s->protkey[0]) key_attrs(s->protkey, "", o);
  buf_adds(o, " sheet=\"1\"");
  if (!(s->protallow & PA_OBJECTS)) buf_adds(o, " objects=\"1\"");
  if (!(s->protallow & PA_SCENARIOS)) buf_adds(o, " scenarios=\"1\"");
  for (i = 0; i < NPATTRS; i++) if (s->protallow & pattrs[i].bit) buf_printf(o, " %s=\"0\"", pattrs[i].name);
  if (!(s->protallow & PA_SELLOCKED)) buf_adds(o, " selectLockedCells=\"1\"");
  if (!(s->protallow & PA_SELUNLOCKED)) buf_adds(o, " selectUnlockedCells=\"1\"");
  buf_adds(o, "/>");
}
void prot_xlsx_write_book(const Book *b, Buf *o)
{
  if (!b->lockstruct) return;
  buf_adds(o, "<workbookProtection");
  if (b->bookpwhash) buf_printf(o, " workbookPassword=\"%04X\"", b->bookpwhash);
  else if (b->bookkey[0]) key_attrs(b->bookkey, "workbook", o);
  buf_adds(o, " lockStructure=\"1\"/>");
}
static int on(const char *v) { return v && (!strcmp(v, "1") || !strcmp(v, "true")); }
static int off(const char *v) { return v && (!strcmp(v, "0") || !strcmp(v, "false")); }
void prot_xlsx_read_sheet(Sheet *s, Xml *x)
{
  const char *v;
  int i;
  if (!on(xml_attr(x, "sheet"))) return;
  s->protect = 1;
  s->protallow = 0;
  for (i = 0; i < NPATTRS; i++) if (off(xml_attr(x, pattrs[i].name))) s->protallow |= pattrs[i].bit;
  if (!on(xml_attr(x, "selectLockedCells"))) s->protallow |= PA_SELLOCKED;
  if (!on(xml_attr(x, "selectUnlockedCells"))) s->protallow |= PA_SELUNLOCKED;
  if (!on(xml_attr(x, "objects"))) s->protallow |= PA_OBJECTS;
  if (!on(xml_attr(x, "scenarios"))) s->protallow |= PA_SCENARIOS;
  if ((v = xml_attr(x, "password"))) s->pwhash = (u16)strtoul(v, NULL, 16);
  else key_read(x, "", s->protkey, sizeof s->protkey);
}
void prot_xlsx_read_book(Book *b, Xml *x)
{
  const char *v;
  if (!on(xml_attr(x, "lockStructure"))) return;
  b->lockstruct = 1;
  if ((v = xml_attr(x, "workbookPassword"))) b->bookpwhash = (u16)strtoul(v, NULL, 16);
  else key_read(x, "workbook", b->bookkey, sizeof b->bookkey);
}
