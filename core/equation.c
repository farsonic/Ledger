/*
 * equation.c - Insert > Equation (GridCraft #471): an equation is a text box (SK_TEXTBOX with
 * DObj.eq) whose text is its LaTeX source, drawn as a laid-out formula and kept in .xlsx as Office
 * Math (OMML) the way Excel and GridCraft write it (xlsx_draw.c: an mc:AlternateContent whose a14
 * choice holds the box with a14:m, the fallback the LaTeX as plain text).
 *
 * The LaTeX Ledger reads is a working subset (Quill's equation editor knows much more): groups { },
 * ^ and _, \frac, \sqrt (with [n]), \left( ... \right), accents (\hat \bar \vec \dot \tilde,
 * \overline), Greek letters and the common operators and relations by name, function names (\sin,
 * \log ...) upright, and \sum \prod \int with limits. Office Math read from a file is turned into it
 * (fractions, scripts, radicals, delimiters, n-ary operators, functions, accents, bars, limits; other
 * structures by their contents) and kept as it was while the source isn't changed. Portable C99.
 */
#include "drawing.h"
#include "xml.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>

/* ------------------------------------------------------------------ the tree */
enum { EK_TEXT, EK_FRAC, EK_SUP, EK_SUB, EK_SUBSUP, EK_SQRT, EK_DELIM, EK_ACC, EK_BAR };
typedef struct EqN EqN;
struct EqN {
  u8 k, up;                        /* EK_; text: upright (a function name, a number) */
  char *s;                         /* EK_TEXT (UTF-8) */
  EqN *a, *b, *c;                  /* rows: FRAC num, den; SUP/SUB base, script; SUBSUP base, sub, sup; SQRT arg, degree; DELIM, ACC, BAR body */
  u32 l, r;                        /* DELIM: the opening and closing characters (0 none); ACC: the accent */
  EqN *next;                       /* the next element of its row */
  int px;                          /* (layout) its size, pixels to the em */
  i32 w, asc, desc;
};
static void eq_free(EqN *n)
{
  while (n) { EqN *nx = n->next; eq_free(n->a); eq_free(n->b); eq_free(n->c); free(n->s); free(n); n = nx; }
}
static EqN *node(int k)
{
  EqN *n = (EqN *)calloc(1, sizeof *n);
  if (n) n->k = (u8)k;
  return n;
}
static EqN *text_node(const char *s, size_t len, int up)
{
  EqN *n = node(EK_TEXT);
  if (n && !(n->s = str_ndup(s, len))) { free(n); return NULL; }
  if (n) n->up = (u8)up;
  return n;
}
static EqN *cp_node(u32 cp, int up)
{
  char t[8];
  int k = utf8_put(t, cp);
  return text_node(t, (size_t)k, up);
}

/* ------------------------------------------------------------------ names */
typedef struct { const char *name; u32 cp; } Sym;
static const Sym syms[] = {
  { "alpha", 0x3B1 }, { "beta", 0x3B2 }, { "gamma", 0x3B3 }, { "delta", 0x3B4 }, { "epsilon", 0x3F5 }, { "varepsilon", 0x3B5 }, { "zeta", 0x3B6 },
  { "eta", 0x3B7 }, { "theta", 0x3B8 }, { "vartheta", 0x3D1 }, { "iota", 0x3B9 }, { "kappa", 0x3BA }, { "lambda", 0x3BB }, { "mu", 0x3BC },
  { "nu", 0x3BD }, { "xi", 0x3BE }, { "pi", 0x3C0 }, { "rho", 0x3C1 }, { "sigma", 0x3C3 }, { "tau", 0x3C4 }, { "upsilon", 0x3C5 },
  { "phi", 0x3D5 }, { "varphi", 0x3C6 }, { "chi", 0x3C7 }, { "psi", 0x3C8 }, { "omega", 0x3C9 },
  { "Gamma", 0x393 }, { "Delta", 0x394 }, { "Theta", 0x398 }, { "Lambda", 0x39B }, { "Xi", 0x39E }, { "Pi", 0x3A0 }, { "Sigma", 0x3A3 },
  { "Upsilon", 0x3A5 }, { "Phi", 0x3A6 }, { "Psi", 0x3A8 }, { "Omega", 0x3A9 },
  { "sum", 0x2211 }, { "prod", 0x220F }, { "int", 0x222B }, { "iint", 0x222C }, { "oint", 0x222E },
  { "pm", 0xB1 }, { "mp", 0x2213 }, { "times", 0xD7 }, { "div", 0xF7 }, { "cdot", 0x22C5 }, { "ast", 0x2217 }, { "circ", 0x2218 },
  { "leq", 0x2264 }, { "le", 0x2264 }, { "geq", 0x2265 }, { "ge", 0x2265 }, { "neq", 0x2260 }, { "ne", 0x2260 }, { "approx", 0x2248 },
  { "equiv", 0x2261 }, { "sim", 0x223C }, { "propto", 0x221D }, { "in", 0x2208 }, { "notin", 0x2209 }, { "subset", 0x2282 },
  { "supset", 0x2283 }, { "cup", 0x222A }, { "cap", 0x2229 }, { "infty", 0x221E }, { "partial", 0x2202 }, { "nabla", 0x2207 },
  { "rightarrow", 0x2192 }, { "to", 0x2192 }, { "leftarrow", 0x2190 }, { "Rightarrow", 0x21D2 }, { "leftrightarrow", 0x2194 },
  { "forall", 0x2200 }, { "exists", 0x2203 }, { "neg", 0xAC }, { "deg", 0xB0 }, { "ldots", 0x2026 }, { "cdots", 0x22EF },
  { "prime", 0x2032 }, { "hbar", 0x210F }, { "ell", 0x2113 }, { NULL, 0 } };
static const char *const funcs[] = { "sin", "cos", "tan", "cot", "sec", "csc", "sinh", "cosh", "tanh", "arcsin", "arccos", "arctan",
                                     "log", "ln", "lg", "exp", "lim", "max", "min", "sup", "inf", "det", "gcd", "arg", "dim", "ker", NULL };
static const Sym accents[] = { { "hat", 0x302 }, { "tilde", 0x303 }, { "bar", 0x305 }, { "vec", 0x20D7 }, { "dot", 0x307 }, { "ddot", 0x308 }, { NULL, 0 } };
static int is_binop(u32 c) { return c == '+' || c == '-' || c == '=' || c == '<' || c == '>' || c == 0xB1 || c == 0xD7 || c == 0xF7 || c == 0x22C5 || c == 0x2264 || c == 0x2265 || c == 0x2260 || c == 0x2248 || c == 0x2192 || c == 0x2213 || c == 0x2261; }
static int is_nary(u32 c) { return c == 0x2211 || c == 0x220F || c == 0x222B || c == 0x222C || c == 0x222E; }

/* ------------------------------------------------------------------ LaTeX in */
static EqN *tex_row(const char **p, int stop);
static void skip_sp(const char **p) { while (**p == ' ' || **p == '\t' || **p == '\n' || **p == '\r') (*p)++; }
/* one argument: a group in braces, a command, or one character */
static EqN *tex_atom(const char **p);
static EqN *tex_arg(const char **p)
{
  skip_sp(p);
  if (**p == '{') { EqN *r; (*p)++; r = tex_row(p, '}'); if (**p == '}') (*p)++; return r; }
  return tex_atom(p);
}
/* a delimiter after \left or \right: ( [ \{ | . */
static u32 tex_delim(const char **p)
{
  int used;
  u32 c;
  skip_sp(p);
  if (**p == '\\') {
    (*p)++;
    if (**p == '{' || **p == '}' || **p == '|') return (u32)*(*p)++;
    if (!strncmp(*p, "langle", 6)) { *p += 6; return 0x27E8; }
    if (!strncmp(*p, "rangle", 6)) { *p += 6; return 0x27E9; }
    if (!strncmp(*p, "lfloor", 6)) { *p += 6; return 0x230A; }
    if (!strncmp(*p, "rfloor", 6)) { *p += 6; return 0x230B; }
    return 0;
  }
  if (!**p) return 0;
  c = utf8_get(*p, strlen(*p), &used);
  *p += used;
  return c == '.' ? 0 : c;
}
static EqN *tex_atom(const char **p)
{
  const char *s;
  int used;
  u32 c;
  skip_sp(p);
  s = *p;
  if (!*s) return NULL;
  if (*s == '{') return tex_arg(p);
  if (*s == '\\') {
    char nm[24];
    size_t n = 0;
    const Sym *y;
    int k;
    s++;
    if (!isalpha((u8)*s)) {
      /* \{ \} \, \; \! \\ and the like */
      *p = s + (*s ? 1 : 0);
      if (*s == ',' || *s == ';' || *s == ' ' || *s == ':') return text_node(" ", 1, 1);
      if (*s == '!') return text_node("", 0, 1);
      return *s ? text_node(s, 1, 1) : NULL;
    }
    while (isalpha((u8)s[n]) && n < sizeof nm - 1) { nm[n] = s[n]; n++; }
    nm[n] = 0;
    while (isalpha((u8)s[n])) n++;
    *p = s + n;
    if (!strcmp(nm, "frac") || !strcmp(nm, "dfrac") || !strcmp(nm, "tfrac")) { EqN *f = node(EK_FRAC); if (f) { f->a = tex_arg(p); f->b = tex_arg(p); } return f; }
    if (!strcmp(nm, "sqrt")) {
      EqN *f = node(EK_SQRT);
      if (!f) return NULL;
      skip_sp(p);
      if (**p == '[') { (*p)++; f->b = tex_row(p, ']'); if (**p == ']') (*p)++; }
      f->a = tex_arg(p);
      return f;
    }
    if (!strcmp(nm, "left")) {
      EqN *f = node(EK_DELIM);
      if (!f) return NULL;
      f->l = tex_delim(p);
      f->a = tex_row(p, 'R');
      if (!strncmp(*p, "\\right", 6)) { *p += 6; f->r = tex_delim(p); }
      return f;
    }
    if (!strcmp(nm, "overline")) { EqN *f = node(EK_BAR); if (f) f->a = tex_arg(p); return f; }
    if (!strcmp(nm, "mathrm") || !strcmp(nm, "text") || !strcmp(nm, "operatorname")) {
      /* upright text: its characters as they are */
      EqN *f;
      const char *b, *e;
      skip_sp(p);
      if (**p != '{') return tex_atom(p);
      b = ++(*p);
      for (e = b; *e && *e != '}'; e++) ;
      f = text_node(b, (size_t)(e - b), 1);
      *p = *e ? e + 1 : e;
      return f;
    }
    for (k = 0; accents[k].name; k++) if (!strcmp(nm, accents[k].name)) { EqN *f = node(EK_ACC); if (f) { f->l = accents[k].cp; f->a = tex_arg(p); } return f; }
    for (k = 0; funcs[k]; k++) if (!strcmp(nm, funcs[k])) return text_node(nm, strlen(nm), 1);
    for (y = syms; y->name; y++) if (!strcmp(nm, y->name)) return cp_node(y->cp, 1);
    return text_node(nm, strlen(nm), 1);           /* (a name Ledger doesn't know: its letters) */
  }
  if (*s == '}' || *s == ']') return NULL;
  c = utf8_get(s, strlen(s), &used);
  *p = s + used;
  if (isdigit((u8)*s)) {
    /* a number: its digits and point together */
    const char *e = s;
    while (isdigit((u8)*e) || (*e == '.' && isdigit((u8)e[1]))) e++;
    *p = e;
    return text_node(s, (size_t)(e - s), 1);
  }
  return text_node(s, (size_t)used, !(c < 0x80 && isalpha((int)c)));
}
/* a row up to stop ('}', ']', 'R' for \right, 0 the end) */
static EqN *tex_row(const char **p, int stop)
{
  EqN *first = NULL, **tail = &first, *last = NULL;
  int guard = 0;
  for (;;) {
    EqN *n;
    skip_sp(p);
    if (!**p || ++guard > 20000) break;
    if (stop == '}' && **p == '}') break;
    if (stop == ']' && **p == ']') break;
    if (stop == 'R' && !strncmp(*p, "\\right", 6)) break;
    if (**p == '}' || **p == ']') { (*p)++; continue; }   /* (one too many: passed over) */
    if ((**p == '^' || **p == '_') && last) {
      /* scripts on the element before */
      int sup = **p == '^';
      EqN *sc, *base = last, *f;
      (*p)++;
      sc = tex_arg(p);
      if (base->k == EK_SUB && sup) { base->k = EK_SUBSUP; base->c = sc; continue; }
      if (base->k == EK_SUP && !sup) { base->k = EK_SUBSUP; base->c = base->b; base->b = sc; continue; }
      if (!(f = node(sup ? EK_SUP : EK_SUB))) { eq_free(sc); continue; }
      /* the base is taken out of the row and put inside */
      {
        EqN *nx = base->next;
        *f = *base;
        f->next = NULL;
        memset(base, 0, sizeof *base);
        base->k = (u8)(sup ? EK_SUP : EK_SUB);
        base->a = f; base->b = sc; base->next = nx;
      }
      continue;
    }
    if (**p == '^' || **p == '_') { int sup = **p == '^'; (*p)++; n = node(sup ? EK_SUP : EK_SUB); if (n) { n->a = text_node("", 0, 1); n->b = tex_arg(p); } }
    else if (!(n = tex_atom(p))) { if (**p) (*p)++; continue; }
    if (!n) continue;
    *tail = n; last = n;
    while (n->next) n = n->next;
    tail = &n->next;
  }
  return first;
}
static EqN *from_tex(const char *s) { const char *p = s ? s : ""; return tex_row(&p, 0); }

/* ------------------------------------------------------------------ LaTeX out */
static void tex_out_row(Buf *o, const EqN *n);
static void tex_group(Buf *o, const EqN *n) { buf_addc(o, '{'); tex_out_row(o, n); buf_addc(o, '}'); }
static void tex_char(Buf *o, u32 c)
{
  const Sym *y;
  char t[8];
  for (y = syms; y->name; y++) if (y->cp == c && c >= 0x80) { buf_printf(o, "\\%s ", y->name); return; }
  if (c == '{' || c == '}' || c == '#' || c == '%' || c == '&' || c == '$') { buf_addc(o, '\\'); buf_addc(o, (int)c); return; }
  t[utf8_put(t, c)] = 0;
  buf_adds(o, t);
}
static void tex_delim_out(Buf *o, u32 c)
{
  if (!c) buf_addc(o, '.');
  else if (c == '{' || c == '}') { buf_addc(o, '\\'); buf_addc(o, (int)c); }
  else if (c == 0x27E8) buf_adds(o, "\\langle ");
  else if (c == 0x27E9) buf_adds(o, "\\rangle ");
  else tex_char(o, c);
}
static void tex_out_row(Buf *o, const EqN *n)
{
  for (; n; n = n->next) {
    switch (n->k) {
    case EK_TEXT: {
      const char *s = n->s ? n->s : "";
      int k;
      for (k = 0; funcs[k]; k++) if (!strcmp(s, funcs[k])) break;
      if (funcs[k]) { buf_printf(o, "\\%s ", s); break; }
      if (n->up && strlen(s) > 1 && isalpha((u8)s[0])) { buf_adds(o, "\\mathrm{"); buf_adds(o, s); buf_addc(o, '}'); break; }
      while (*s) { int used; u32 c = utf8_get(s, strlen(s), &used); tex_char(o, c); s += used; }
      break; }
    case EK_FRAC: buf_adds(o, "\\frac"); tex_group(o, n->a); tex_group(o, n->b); break;
    case EK_SUP: case EK_SUB: case EK_SUBSUP:
      if (n->a && !n->a->next && n->a->k == EK_TEXT) tex_out_row(o, n->a); else tex_group(o, n->a);
      if (n->k != EK_SUP) { buf_addc(o, '_'); tex_group(o, n->b); }
      if (n->k == EK_SUP) { buf_addc(o, '^'); tex_group(o, n->b); }
      if (n->k == EK_SUBSUP) { buf_addc(o, '^'); tex_group(o, n->c); }
      break;
    case EK_SQRT: buf_adds(o, "\\sqrt"); if (n->b) { buf_addc(o, '['); tex_out_row(o, n->b); buf_addc(o, ']'); } tex_group(o, n->a); break;
    case EK_DELIM: buf_adds(o, "\\left"); tex_delim_out(o, n->l); tex_out_row(o, n->a); buf_adds(o, "\\right"); tex_delim_out(o, n->r); break;
    case EK_BAR: buf_adds(o, "\\overline"); tex_group(o, n->a); break;
    case EK_ACC: {
      int k;
      for (k = 0; accents[k].name && accents[k].cp != n->l; k++) ;
      buf_printf(o, "\\%s", accents[k].name ? accents[k].name : "hat");
      tex_group(o, n->a);
      break; }
    }
  }
}
static char *to_tex(const EqN *n)
{
  Buf o;
  buf_init(&o);
  tex_out_row(&o, n);
  buf_addc(&o, 0);
  /* a trailing space after a command name is not needed at the end */
  while (o.len > 1 && o.data[o.len - 2] == ' ') { o.data[o.len - 2] = 0; o.len--; }
  return buf_take(&o);
}

/* ------------------------------------------------------------------ Office Math out */
static void om_row(Buf *o, const EqN *n);
static void om_val(Buf *o, const char *tag, u32 c)
{
  char t[8];
  t[utf8_put(t, c)] = 0;
  buf_printf(o, "<m:%s m:val=\"", tag); xml_escape(o, t, strlen(t)); buf_adds(o, "\"/>");
}
static void om_wrap(Buf *o, const char *tag, const EqN *n) { buf_printf(o, "<m:%s>", tag); om_row(o, n); buf_printf(o, "</m:%s>", tag); }
static void om_row(Buf *o, const EqN *n)
{
  for (; n; n = n->next) {
    switch (n->k) {
    case EK_TEXT:
      buf_adds(o, "<m:r>");
      if (n->up && n->s && isalpha((u8)n->s[0])) buf_adds(o, "<m:rPr><m:sty m:val=\"p\"/></m:rPr>");
      buf_adds(o, "<m:t>"); xml_escape(o, n->s ? n->s : "", n->s ? strlen(n->s) : 0); buf_adds(o, "</m:t></m:r>");
      break;
    case EK_FRAC: buf_adds(o, "<m:f>"); om_wrap(o, "num", n->a); om_wrap(o, "den", n->b); buf_adds(o, "</m:f>"); break;
    case EK_SUP: case EK_SUB: case EK_SUBSUP: {
      if (n->a && !n->a->next && n->a->k == EK_TEXT && n->a->s) {
        int used;
        u32 c = utf8_get(n->a->s, strlen(n->a->s), &used);
        if (is_nary(c) && !n->a->s[used]) {
          /* an n-ary operator with its limits */
          buf_adds(o, "<m:nary><m:naryPr>"); om_val(o, "chr", c);
          if (n->k == EK_SUP) buf_adds(o, "<m:subHide m:val=\"1\"/>");
          if (n->k == EK_SUB) buf_adds(o, "<m:supHide m:val=\"1\"/>");
          buf_adds(o, "</m:naryPr>");
          om_wrap(o, "sub", n->k == EK_SUP ? NULL : n->b);
          om_wrap(o, "sup", n->k == EK_SUP ? n->b : n->k == EK_SUBSUP ? n->c : NULL);
          buf_adds(o, "<m:e/></m:nary>");
          break;
        }
      }
      if (n->k == EK_SUP) { buf_adds(o, "<m:sSup>"); om_wrap(o, "e", n->a); om_wrap(o, "sup", n->b); buf_adds(o, "</m:sSup>"); }
      else if (n->k == EK_SUB) { buf_adds(o, "<m:sSub>"); om_wrap(o, "e", n->a); om_wrap(o, "sub", n->b); buf_adds(o, "</m:sSub>"); }
      else { buf_adds(o, "<m:sSubSup>"); om_wrap(o, "e", n->a); om_wrap(o, "sub", n->b); om_wrap(o, "sup", n->c); buf_adds(o, "</m:sSubSup>"); }
      break; }
    case EK_SQRT:
      buf_adds(o, "<m:rad>");
      if (!n->b) buf_adds(o, "<m:radPr><m:degHide m:val=\"1\"/></m:radPr><m:deg/>"); else om_wrap(o, "deg", n->b);
      om_wrap(o, "e", n->a);
      buf_adds(o, "</m:rad>");
      break;
    case EK_DELIM:
      buf_adds(o, "<m:d><m:dPr>");
      if (n->l != '(') om_val(o, "begChr", n->l);
      if (n->r != ')') om_val(o, "endChr", n->r);
      buf_adds(o, "</m:dPr>");
      om_wrap(o, "e", n->a);
      buf_adds(o, "</m:d>");
      break;
    case EK_BAR: buf_adds(o, "<m:bar><m:barPr><m:pos m:val=\"top\"/></m:barPr>"); om_wrap(o, "e", n->a); buf_adds(o, "</m:bar>"); break;
    case EK_ACC: buf_adds(o, "<m:acc><m:accPr>"); om_val(o, "chr", n->l); buf_adds(o, "</m:accPr>"); om_wrap(o, "e", n->a); buf_adds(o, "</m:acc>"); break;
    }
  }
}

/* ------------------------------------------------------------------ Office Math in */
typedef struct { EqN *rows[8]; char names[8][64]; int n; u32 chr, beg, end, sep; u8 deghide, sty, haschr, hasbeg, hasend, top; } OmKids;
static EqN *om_elem(Xml *x);
/* the elements of a row container until its END: their nodes joined */
static EqN *om_children_row(Xml *x)
{
  EqN *first = NULL, **tail = &first;
  int d0 = x->depth;
  if (x->pending_end) { xml_next(x); return NULL; }
  while (xml_next(x) != XT_EOF) {
    if (x->type == XT_END && x->depth < d0) break;
    if (x->type != XT_START) continue;
    if (strlen(x->name) > 2 && !strcmp(x->name + strlen(x->name) - 2, "Pr")) { xml_skip(x); continue; }   /* (ctrlPr, rPr...) */
    {
      EqN *n = om_elem(x);
      if (!n) continue;
      *tail = n;
      while (n->next) n = n->next;
      tail = &n->next;
    }
  }
  return first;
}
static u32 first_cp(const char *s) { int used; return s && *s ? utf8_get(s, strlen(s), &used) : 0; }
/* a structure's parts: its row containers by name, and the properties Ledger uses */
static void om_kids(Xml *x, OmKids *k)
{
  int d0 = x->depth;
  memset(k, 0, sizeof *k);
  if (x->pending_end) { xml_next(x); return; }
  while (xml_next(x) != XT_EOF) {
    const char *nm;
    if (x->type == XT_END && x->depth < d0) break;
    if (x->type != XT_START) continue;
    nm = x->name;
    if (strlen(nm) > 2 && !strcmp(nm + strlen(nm) - 2, "Pr")) {
      /* properties: their children's m:val */
      int d1 = x->depth;
      if (x->pending_end) { xml_next(x); continue; }
      while (xml_next(x) != XT_EOF) {
        const char *v;
        if (x->type == XT_END && x->depth < d1) break;
        if (x->type != XT_START) continue;
        v = xml_attr(x, "val");
        if (!strcmp(x->name, "chr")) { k->chr = first_cp(v); k->haschr = 1; }
        else if (!strcmp(x->name, "begChr")) { k->beg = first_cp(v); k->hasbeg = 1; }
        else if (!strcmp(x->name, "endChr")) { k->end = first_cp(v); k->hasend = 1; }
        else if (!strcmp(x->name, "sepChr")) k->sep = first_cp(v);
        else if (!strcmp(x->name, "degHide")) k->deghide = !v || strcmp(v, "0") != 0;
        else if (!strcmp(x->name, "sty")) k->sty = v && !strcmp(v, "p");
        else if (!strcmp(x->name, "pos")) k->top = !v || !strcmp(v, "top");
      }
      continue;
    }
    if (k->n < 8) {
      snprintf(k->names[k->n], sizeof k->names[0], "%s", nm);
      k->rows[k->n++] = om_children_row(x);
    } else xml_skip(x);
  }
}
static EqN *kid(OmKids *k, const char *name)
{
  int i;
  for (i = 0; i < k->n; i++) if (!strcmp(k->names[i], name)) { EqN *r = k->rows[i]; k->rows[i] = NULL; return r; }
  return NULL;
}
static void kids_free(OmKids *k) { int i; for (i = 0; i < k->n; i++) eq_free(k->rows[i]); }
static EqN *join(EqN *a, EqN *b) { EqN *t = a; if (!a) return b; while (t->next) t = t->next; t->next = b; return a; }
/* one element at its START: its nodes (a row: several when a structure has a body after it) */
static EqN *om_elem(Xml *x)
{
  char nm[64];
  OmKids k;
  EqN *n = NULL;
  snprintf(nm, sizeof nm, "%s", x->name);
  if (!strcmp(nm, "r")) {
    /* a run: its text; m:sty p upright */
    Buf t;
    int d0 = x->depth, sty = 0, in_t = 0;
    buf_init(&t);
    if (!x->pending_end)
      while (xml_next(x) != XT_EOF) {
        if (x->type == XT_END) { if (x->depth < d0) break; if (!strcmp(x->name, "t")) in_t = 0; continue; }
        if (x->type == XT_START) { if (!strcmp(x->name, "t")) in_t = 1; else if (!strcmp(x->name, "sty")) { const char *v = xml_attr(x, "val"); sty = v && !strcmp(v, "p"); } continue; }
        if (x->type == XT_TEXT && in_t) buf_add(&t, x->text.data, x->text.len);
      }
    else xml_next(x);
    n = text_node(t.data ? t.data : "", t.len, sty || (t.len && !isalpha((u8)t.data[0])));
    buf_free(&t);
    return n;
  }
  {
    /* a row of elements (met where a structure was expected, or a matrix's row): its elements */
    static const char *const rows[] = { "oMath", "e", "num", "den", "sub", "sup", "deg", "fName", "lim", "mr", NULL };
    int i;
    for (i = 0; rows[i]; i++) if (!strcmp(nm, rows[i])) {
      EqN *r = om_children_row(x);
      if (!strcmp(nm, "mr")) r = join(r, text_node(" ", 1, 1));
      return r;
    }
  }
  om_kids(x, &k);
  if (!strcmp(nm, "f")) { if ((n = node(EK_FRAC))) { n->a = kid(&k, "num"); n->b = kid(&k, "den"); } }
  else if (!strcmp(nm, "sSup") || !strcmp(nm, "limUpp")) { if ((n = node(EK_SUP))) { n->a = kid(&k, "e"); n->b = kid(&k, nm[0] == 's' ? "sup" : "lim"); } }
  else if (!strcmp(nm, "sSub") || !strcmp(nm, "limLow")) { if ((n = node(EK_SUB))) { n->a = kid(&k, "e"); n->b = kid(&k, nm[0] == 's' ? "sub" : "lim"); } }
  else if (!strcmp(nm, "sSubSup")) { if ((n = node(EK_SUBSUP))) { n->a = kid(&k, "e"); n->b = kid(&k, "sub"); n->c = kid(&k, "sup"); } }
  else if (!strcmp(nm, "sPre")) { if ((n = node(EK_SUBSUP))) { n->a = text_node("", 0, 1); n->b = kid(&k, "sub"); n->c = kid(&k, "sup"); n = join(n, kid(&k, "e")); } }
  else if (!strcmp(nm, "rad")) { if ((n = node(EK_SQRT))) { n->a = kid(&k, "e"); n->b = k.deghide ? NULL : kid(&k, "deg"); } }
  else if (!strcmp(nm, "d")) {
    if ((n = node(EK_DELIM))) {
      int i;
      n->l = k.hasbeg ? k.beg : '('; n->r = k.hasend ? k.end : ')';
      for (i = 0; i < k.n; i++) if (!strcmp(k.names[i], "e")) {
        if (n->a) n->a = join(n->a, cp_node(k.sep ? k.sep : '|', 1));
        n->a = join(n->a, k.rows[i]); k.rows[i] = NULL;
      }
    }
  }
  else if (!strcmp(nm, "nary")) {
    EqN *sub = kid(&k, "sub"), *sup = kid(&k, "sup");
    if ((n = node(sub && sup ? EK_SUBSUP : sup ? EK_SUP : EK_SUB))) {
      n->a = cp_node(k.haschr ? k.chr : 0x222B, 1);
      if (sub && sup) { n->b = sub; n->c = sup; } else n->b = sup ? sup : sub;
      if (!sub && !sup) { EqN *t = n->a; n->a = NULL; eq_free(n); n = t; }
      n = join(n, kid(&k, "e"));
    } else { eq_free(sub); eq_free(sup); }
  }
  else if (!strcmp(nm, "func")) {
    EqN *f = kid(&k, "fName"), *t;
    for (t = f; t; t = t->next) if (t->k == EK_TEXT) t->up = 1;
    n = join(f, kid(&k, "e"));
  }
  else if (!strcmp(nm, "acc")) { if ((n = node(EK_ACC))) { n->l = k.haschr ? k.chr : 0x302; n->a = kid(&k, "e"); } }
  else if (!strcmp(nm, "bar")) { if ((n = node(EK_BAR))) n->a = kid(&k, "e"); }
  else {
    /* box, borderBox, phant, groupChr, eqArr, m, oMath...: the contents in order */
    int i;
    for (i = 0; i < k.n; i++) {
      if (n && (!strcmp(nm, "eqArr") || !strcmp(k.names[i], "mr"))) n = join(n, text_node(" ", 1, 1));
      n = join(n, k.rows[i]); k.rows[i] = NULL;
    }
  }
  kids_free(&k);
  return n;
}
/* an Office Math element (m:oMathPara or m:oMath) as LaTeX (malloc'd; NULL if it holds nothing) */
char *eq_latex_from_omml(const char *xml, size_t len)
{
  Xml x;
  EqN *row = NULL;
  char *t;
  xml_init(&x, xml, len);
  while (xml_next(&x) != XT_EOF) {
    if (x.type != XT_START || (strcmp(x.name, "oMathPara") && strcmp(x.name, "oMath"))) continue;
    row = om_children_row(&x);
    break;
  }
  xml_free(&x);
  if (!row) return NULL;
  t = to_tex(row);
  eq_free(row);
  return t;
}
/* the LaTeX as Office Math: an m:oMathPara with one m:oMath (malloc'd) */
char *eq_omml_from_latex(const char *latex)
{
  EqN *row = from_tex(latex);
  Buf o;
  buf_init(&o);
  buf_adds(&o, "<m:oMathPara xmlns:m=\"http://schemas.openxmlformats.org/officeDocument/2006/math\"><m:oMathParaPr><m:jc m:val=\"centerGroup\"/></m:oMathParaPr><m:oMath>");
  om_row(&o, row);
  buf_adds(&o, "</m:oMath></m:oMathPara>");
  eq_free(row);
  buf_addc(&o, 0);
  return o.oom ? (buf_free(&o), (char *)NULL) : buf_take(&o);
}
/* the source tidied as Ledger writes it (what reading its Office Math back gives) */
char *eq_latex_normal(const char *latex)
{
  EqN *row = from_tex(latex);
  char *t = to_tex(row);
  eq_free(row);
  return t;
}

/* ------------------------------------------------------------------ drawing */
typedef struct { TextWidthFn tw; void *ctx; u32 color; PrimList *out; } Lay;
static int small(int px) { int s = px * 7 / 10; return s < 6 ? 6 : s; }
static void measure_row(Lay *L, EqN *n, int px, i32 *w, i32 *asc, i32 *desc);
static void measure(Lay *L, EqN *n, int px)
{
  i32 w1, a1, d1, w2, a2, d2, w3, a3, d3;
  n->px = px;
  switch (n->k) {
  case EK_TEXT: {
    const char *s = n->s ? n->s : "";
    int used;
    u32 c = *s ? utf8_get(s, strlen(s), &used) : 0;
    n->w = *s ? L->tw(L->ctx, s, is_nary(c) ? px * 3 / 2 : px, 0) : 0;
    n->asc = px * 3 / 4; n->desc = px / 4;
    if (is_nary(c)) { n->asc = px; n->desc = px / 2; }
    if (is_binop(c) && !s[used]) n->w += px / 3;
    break; }
  case EK_FRAC:
    measure_row(L, n->a, px * 4 / 5 < 6 ? 6 : px * 4 / 5, &w1, &a1, &d1);
    measure_row(L, n->b, px * 4 / 5 < 6 ? 6 : px * 4 / 5, &w2, &a2, &d2);
    n->w = (w1 > w2 ? w1 : w2) + px / 3;
    n->asc = px / 3 + 2 + a1 + d1; n->desc = a2 + d2 + 2 - px / 3;
    break;
  case EK_SUP: case EK_SUB: case EK_SUBSUP:
    measure_row(L, n->a, px, &w1, &a1, &d1);
    measure_row(L, n->b, small(px), &w2, &a2, &d2);
    w3 = a3 = d3 = 0;
    if (n->k == EK_SUBSUP) measure_row(L, n->c, small(px), &w3, &a3, &d3);
    n->w = w1 + (w2 > w3 ? w2 : w3) + 1;
    if (n->k == EK_SUP) { n->asc = a1 / 2 + a2 + d2; if (n->asc < a1) n->asc = a1; n->desc = d1; }
    else if (n->k == EK_SUB) { n->asc = a1; n->desc = d1 / 2 + px / 4 + a2 + d2; }
    else { n->asc = a1 / 2 + a3 + d3; if (n->asc < a1) n->asc = a1; n->desc = d1 / 2 + px / 4 + a2 + d2; }
    break;
  case EK_SQRT:
    measure_row(L, n->a, px, &w1, &a1, &d1);
    w2 = 0;
    if (n->b) measure_row(L, n->b, small(small(px)), &w2, &a2, &d2);
    n->w = w1 + px * 2 / 3 + (w2 > px / 3 ? w2 - px / 3 : 0) + 2;
    n->asc = a1 + 3; n->desc = d1;
    break;
  case EK_DELIM:
    measure_row(L, n->a, px, &w1, &a1, &d1);
    n->w = w1 + (n->l ? px / 2 : 0) + (n->r ? px / 2 : 0);
    n->asc = a1 + 1; n->desc = d1 + 1;
    break;
  case EK_ACC: case EK_BAR:
    measure_row(L, n->a, px, &w1, &a1, &d1);
    n->w = w1; n->asc = a1 + px / 4 + 1; n->desc = d1;
    break;
  }
}
static void measure_row(Lay *L, EqN *n, int px, i32 *w, i32 *asc, i32 *desc)
{
  *w = 0; *asc = px * 3 / 4; *desc = px / 4;
  for (; n; n = n->next) {
    measure(L, n, px);
    *w += n->w;
    if (n->asc > *asc) *asc = n->asc;
    if (n->desc > *desc) *desc = n->desc;
  }
}
static void p_text(Lay *L, const char *s, i32 x, i32 base, int px, int italic)
{
  Prim *p;
  if (!s || !*s || !(p = prim_add(L->out, PR_TEXT, CE_NONE))) return;
  if (!(p->text = str_dup(s))) { L->out->oom = 1; return; }
  p->x = x; p->y = base + px / 4; p->size = (u8)(px > 255 ? 255 : px); p->line = L->color; p->valign = 2; p->align = 0;
  (void)italic;
}
static void p_line(Lay *L, i32 x0, i32 y0, i32 x1, i32 y1)
{
  Prim *p = prim_add(L->out, PR_LINE, CE_NONE);
  if (!p || !(p->pts = (i32 *)malloc(4 * sizeof(i32)))) { if (p) L->out->oom = 1; return; }
  p->np = 2; p->pts[0] = x0; p->pts[1] = y0; p->pts[2] = x1; p->pts[3] = y1; p->line = L->color; p->width = 1;
}
static void place_row(Lay *L, EqN *n, i32 x, i32 base);
static void place(Lay *L, EqN *n, i32 x, i32 base)
{
  int px = n->px;
  switch (n->k) {
  case EK_TEXT: {
    int used;
    u32 c = n->s && *n->s ? utf8_get(n->s, strlen(n->s), &used) : 0;
    int bin = c && is_binop(c) && !n->s[used];
    if (is_nary(c)) p_text(L, n->s, x, base + px / 3, px * 3 / 2, 0);
    else p_text(L, n->s, x + (bin ? px / 6 : 0), base, px, !n->up);
    break; }
  case EK_FRAC: {
    i32 axis = base - px / 3, wa = 0, wb = 0, a, d;
    EqN *t;
    for (t = n->a; t; t = t->next) wa += t->w;
    for (t = n->b; t; t = t->next) wb += t->w;
    p_line(L, x + 1, axis, x + n->w - 2, axis);
    a = d = 0;
    for (t = n->b; t; t = t->next) if (t->asc > a) a = t->asc;
    for (t = n->a; t; t = t->next) if (t->desc > d) d = t->desc;
    place_row(L, n->a, x + (n->w - wa) / 2, axis - 2 - d);
    place_row(L, n->b, x + (n->w - wb) / 2, axis + 2 + (a ? a : small(px) * 3 / 4));
    break; }
  case EK_SUP: case EK_SUB: case EK_SUBSUP: {
    i32 wa = 0, a1 = px * 3 / 4, d1 = px / 4, ds = 0, as = 0;
    EqN *t;
    for (t = n->a; t; t = t->next) { wa += t->w; if (t->asc > a1) a1 = t->asc; if (t->desc > d1) d1 = t->desc; }
    place_row(L, n->a, x, base);
    if (n->k == EK_SUP || n->k == EK_SUBSUP) {
      EqN *sp = n->k == EK_SUP ? n->b : n->c;
      for (t = sp; t; t = t->next) if (t->desc > ds) ds = t->desc;
      place_row(L, sp, x + wa + 1, base - a1 / 2 - ds);
    }
    if (n->k == EK_SUB || n->k == EK_SUBSUP) {
      for (t = n->b; t; t = t->next) if (t->asc > as) as = t->asc;
      place_row(L, n->b, x + wa + 1, base + d1 / 2 + px / 4 + as);
    }
    break; }
  case EK_SQRT: {
    i32 off = px * 2 / 3, wd = 0, top = base - n->asc + 1;
    EqN *t;
    if (n->b) { for (t = n->b; t; t = t->next) wd += t->w; place_row(L, n->b, x, base - px / 2); if (wd > px / 3) off += wd - px / 3; }
    p_line(L, x + off - px * 2 / 3, base - px / 3, x + off - px / 2, base - px / 2);
    p_line(L, x + off - px / 2, base - px / 2, x + off - px / 4, base + n->desc);
    p_line(L, x + off - px / 4, base + n->desc, x + off, top);
    p_line(L, x + off, top, x + n->w - 1, top);
    place_row(L, n->a, x + off + 1, base);
    break; }
  case EK_DELIM: {
    i32 lw = n->l ? px / 2 : 0, h = n->asc + n->desc, sz = h > px * 3 / 2 ? h : px;
    char t[8];
    if (n->l) { t[utf8_put(t, n->l)] = 0; p_text(L, t, x, base + (sz - px) / 3, sz, 0); }
    place_row(L, n->a, x + lw, base);
    if (n->r) { EqN *k; i32 w = 0; for (k = n->a; k; k = k->next) w += k->w; t[utf8_put(t, n->r)] = 0; p_text(L, t, x + lw + w, base + (sz - px) / 3, sz, 0); }
    break; }
  case EK_ACC: case EK_BAR: {
    i32 top = base - n->asc + 1;
    if (n->k == EK_BAR || n->l == 0x305) p_line(L, x, top, x + n->w - 1, top);
    else if (n->l == 0x20D7) { p_line(L, x, top + 1, x + n->w - 1, top + 1); p_line(L, x + n->w - 4, top - 1, x + n->w - 1, top + 1); }
    else if (n->l == 0x307 || n->l == 0x308) p_line(L, x + n->w / 2, top, x + n->w / 2 + 1, top);
    else { p_line(L, x + n->w / 2 - px / 5, top + px / 6, x + n->w / 2, top); p_line(L, x + n->w / 2, top, x + n->w / 2 + px / 5, top + px / 6); }
    place_row(L, n->a, x, base);
    break; }
  }
}
static void place_row(Lay *L, EqN *n, i32 x, i32 base) { for (; n; n = n->next) { place(L, n, x, base); x += n->w; } }
int eq_render(const char *latex, i32 w, i32 h, int px, u32 color, TextWidthFn tw, void *ctx, PrimList *out)
{
  EqN *row = from_tex(latex);
  Lay L;
  i32 rw, ra, rd;
  if (px < 4) px = 4;
  L.tw = tw; L.ctx = ctx; L.color = color ? color : FILL_ON; L.out = out;
  measure_row(&L, row, px, &rw, &ra, &rd);
  place_row(&L, row, (w - rw) / 2, (h - ra - rd) / 2 + ra);
  eq_free(row);
  return !out->oom;
}
/* the size an equation needs at px pixels to the em */
void eq_size(const char *latex, int px, TextWidthFn tw, void *ctx, i32 *w, i32 *h)
{
  EqN *row = from_tex(latex);
  Lay L;
  i32 ra, rd;
  PrimList dummy;
  prims_init(&dummy);
  L.tw = tw; L.ctx = ctx; L.color = 0; L.out = &dummy;
  measure_row(&L, row, px, w, &ra, &rd);
  *h = ra + rd;
  eq_free(row);
}

/* a rough text width when no font is at hand (adding an equation in the core) */
static i32 rough_width(void *ctx, const char *s, int px, int bold)
{
  i32 n = 0;
  (void)ctx; (void)bold;
  for (; *s; s++) if (((u8)*s & 0xC0) != 0x80) n++;
  return n * px * 11 / 20 + 1;
}
int book_equation_add(Book *b, int sheet, const char *latex, i32 x, i32 y)
{
  Sheet *s;
  DObj *o;
  i32 w, h;
  int idx = -1;
  if (sheet < 0 || sheet >= b->nsh) return -1;
  s = b->sh[sheet];
  if (s->protect && !(s->protallow & 8192)) return -1;    /* PA_OBJECTS */
  if (!latex || !*latex) latex = "x = \\frac{-b \\pm \\sqrt{b^2 - 4ac}}{2a}";
  drw_begin(b, sheet);
  if ((o = drw_new(s, OB_SHAPE))) {
    o->shape = SK_TEXTBOX;
    o->eq = 1;
    o->text = str_dup(latex);
    o->fsize = 110; o->halign = 1; o->valign = 1;
    snprintf(o->name, sizeof o->name, "Equation %lu", (unsigned long)o->id);
    eq_size(latex, 15, rough_width, NULL, &w, &h);
    dobj_place(s, o, x, y, x + w + 16, y + h + 12);
    idx = drw_count(s) - 1;
  }
  drw_end(b);
  return idx;
}
