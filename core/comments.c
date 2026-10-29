/*
 * comments.c - see comments.h. Replies are kept in one string per comment: each reply is
 * "author\037text\036" (unit and record separators), so a note copies and frees as one block.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "comments.h"

static Note *note_at(Sheet *s, i32 r, i32 c)
{
  int i;
  for (i = 0; i < s->nnotes; i++) if (s->notes[i].r == r && s->notes[i].c == c) return &s->notes[i];
  return NULL;
}
int book_set_comment(Book *b, int sheet, i32 r, i32 c, const char *text, const char *author)
{
  Note *n;
  if (sheet < 0 || sheet >= b->nsh) return 0;
  book_begin(b);
  book_note_sheet(b, sheet);
  if (!book_set_note(b, sheet, r, c, text, author)) { book_end(b); return 0; }
  if ((n = note_at(b->sh[sheet], r, c))) n->threaded = 1;
  book_end(b);
  return 1;
}
int book_add_reply(Book *b, int sheet, i32 r, i32 c, const char *text, const char *author)
{
  Note *n;
  size_t ol, al, tl;
  char *nr;
  if (sheet < 0 || sheet >= b->nsh || !text || !*text) return 0;
  if (!(n = note_at(b->sh[sheet], r, c)) || !n->threaded) return 0;
  book_begin(b);
  book_note_sheet(b, sheet);
  n = note_at(b->sh[sheet], r, c);
  ol = n->replies ? strlen(n->replies) : 0; al = author ? strlen(author) : 0; tl = strlen(text);
  if ((nr = (char *)realloc(n->replies, ol + al + tl + 3))) {
    memcpy(nr + ol, author ? author : "", al); nr[ol + al] = '\037';
    memcpy(nr + ol + al + 1, text, tl); nr[ol + al + 1 + tl] = '\036'; nr[ol + al + tl + 2] = 0;
    n->replies = nr;
  }
  book_end(b);
  b->dirty = 1;
  return nr != NULL;
}
int book_resolve_comment(Book *b, int sheet, i32 r, i32 c, int resolved)
{
  Note *n;
  if (sheet < 0 || sheet >= b->nsh || !(n = note_at(b->sh[sheet], r, c)) || !n->threaded) return 0;
  book_begin(b);
  book_note_sheet(b, sheet);
  note_at(b->sh[sheet], r, c)->resolved = (u8)(resolved != 0);
  book_end(b);
  b->dirty = 1;
  return 1;
}
int book_convert_notes(Book *b, int sheet)
{
  int si, i, n = 0;
  book_begin(b);
  for (si = 0; si < b->nsh; si++) {
    Sheet *s = b->sh[si];
    int any = 0;
    if (sheet >= 0 && si != sheet) continue;
    for (i = 0; i < s->nnotes; i++) if (!s->notes[i].threaded) any = 1;
    if (!any) continue;
    book_note_sheet(b, si);
    for (i = 0; i < s->nnotes; i++) if (!s->notes[i].threaded) { s->notes[i].threaded = 1; n++; }
  }
  book_end(b);
  if (n) b->dirty = 1;
  return n;
}
int comment_nreplies(const Note *n)
{
  const char *p;
  int k = 0;
  if (!n || !n->replies) return 0;
  for (p = n->replies; *p; p++) if (*p == '\036') k++;
  return k;
}
static void part(const char *s, size_t len, char *out, size_t cap)
{
  if (!cap) return;
  if (len >= cap) len = cap - 1;
  memcpy(out, s, len); out[len] = 0;
}
int comment_reply(const Note *n, int k, char *author, size_t acap, char *text, size_t tcap)
{
  const char *p = n && n->replies ? n->replies : "";
  while (*p && k > 0) { p = strchr(p, '\036'); if (!p) return 0; p++; k--; }
  if (!*p) return 0;
  {
    const char *us = strchr(p, '\037'), *rs = strchr(p, '\036');
    if (!us || !rs || us > rs) return 0;
    part(p, (size_t)(us - p), author, acap);
    part(us + 1, (size_t)(rs - us - 1), text, tcap);
  }
  return 1;
}
int comment_step(Book *b, int threaded, int forward, int *sheet, i32 *r, i32 *c)
{
  int best = -1, bs = 0, si, i, wrap;
  i32 br = 0, bc = 0;
  /* the nearest after (or before) the position; else, wrapping, the first (or last) of all */
  for (wrap = 0; wrap < 2 && best < 0; wrap++)
    for (si = 0; si < b->nsh; si++) {
      Sheet *s = b->sh[si];
      for (i = 0; i < s->nnotes; i++) {
        const Note *n = &s->notes[i];
        if ((n->threaded != 0) != (threaded != 0)) continue;
        /* compare (sheet, row, col) */
        {
          int after = si > *sheet || (si == *sheet && (n->r > *r || (n->r == *r && n->c > *c)));
          int before = si < *sheet || (si == *sheet && (n->r < *r || (n->r == *r && n->c < *c)));
          int better;
          if (!wrap && !(forward ? after : before)) continue;
          if (best < 0) better = 1;
          else if (forward) better = si < bs || (si == bs && (n->r < br || (n->r == br && n->c < bc)));
          else better = si > bs || (si == bs && (n->r > br || (n->r == br && n->c > bc)));
          if (better) { best = i; bs = si; br = n->r; bc = n->c; }
        }
      }
    }
  if (best < 0) return 0;
  *sheet = bs; *r = br; *c = bc;
  return 1;
}

/* ------------------------------------------------------------------ .xlsx */
/* ids as GUIDs: the person's from its number, a comment's from its sheet, note and reply */
static void person_id(int k, char *out) { sprintf(out, "{4C454447-4552-4000-8000-%012X}", (unsigned)k); }
static void comment_id(int si, int i, int reply, char *out) { sprintf(out, "{4C454447-%04X-4000-8%03X-%012X}", (unsigned)(si & 0xFFFF), (unsigned)(reply & 0xFFF), (unsigned)i); }
int cm_sheet_has(Sheet *s)
{
  int i;
  for (i = 0; i < s->nnotes; i++) if (s->notes[i].threaded) return 1;
  return 0;
}
static void esc(Buf *o, const char *s) { xml_escape(o, s, strlen(s)); }
void cm_legacy(const Note *n, int si, int i, Buf *o, char *author, size_t cap)
{
  char id[48], a[200], t[2000];
  int k, nr = comment_nreplies(n);
  comment_id(si, i, 0, id);
  snprintf(author, cap, "tc=%s", id);
  buf_adds(o, "<r><t xml:space=\"preserve\">[Threaded comment]&#10;&#10;Your version of Excel allows you to read this threaded comment; however, any edits to it will get removed if the file is opened in a newer version of Excel. Learn more: https://go.microsoft.com/fwlink/?linkid=870924&#10;&#10;Comment:&#10;    ");
  esc(o, n->text ? n->text : "");
  for (k = 0; k < nr; k++) if (comment_reply(n, k, a, sizeof a, t, sizeof t)) { buf_adds(o, "&#10;Reply:&#10;    "); esc(o, t); }
  buf_adds(o, "</t></r>");
}
void cm_types(Book *b, Buf *o)
{
  int i, any = 0;
  for (i = 0; i < b->nsh; i++)
    if (cm_sheet_has(b->sh[i])) {
      any = 1;
      buf_printf(o, "<Override PartName=\"/xl/threadedComments/threadedComment%d.xml\" ContentType=\"application/vnd.ms-excel.threadedcomments+xml\"/>", i + 1);
    }
  if (any) buf_adds(o, "<Override PartName=\"/xl/persons/person.xml\" ContentType=\"application/vnd.ms-excel.person+xml\"/>");
}
void cm_book_rels(Book *b, Buf *o, int id)
{
  int i;
  for (i = 0; i < b->nsh; i++)
    if (cm_sheet_has(b->sh[i])) { buf_printf(o, "<Relationship Id=\"rId%d\" Type=\"http://schemas.microsoft.com/office/2017/10/relationships/person\" Target=\"persons/person.xml\"/>", id); return; }
}
void cm_sheet_rels(Sheet *s, int si, Buf *o)
{
  if (cm_sheet_has(s)) buf_printf(o, "<Relationship Id=\"rIdT1\" Type=\"http://schemas.microsoft.com/office/2017/10/relationships/threadedComment\" Target=\"../threadedComments/threadedComment%d.xml\"/>", si + 1);
}
/* the people who wrote comments, each once */
typedef struct { char **names; int n; } People;
static int person(People *p, const char *name)
{
  int i;
  char **nn;
  for (i = 0; i < p->n; i++) if (!strcmp(p->names[i], name)) return i;
  if (!(nn = (char **)realloc(p->names, (size_t)(p->n + 1) * sizeof(char *)))) return 0;
  p->names = nn;
  p->names[p->n] = str_dup(name);
  return p->n++;
}
void cm_parts(Book *b, ZipWriter *w)
{
  People pp;
  Buf o;
  int si, i, k, any = 0;
  char path[64], id[48], pid[48], pid2[48], a[200], t[2000];
  memset(&pp, 0, sizeof pp);
  buf_init(&o);
  for (si = 0; si < b->nsh; si++) {
    Sheet *s = b->sh[si];
    if (!cm_sheet_has(s)) continue;
    any = 1;
    o.len = 0;
    buf_adds(&o, "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n<ThreadedComments xmlns=\"http://schemas.microsoft.com/office/spreadsheetml/2018/threadedcomments\" "
                 "xmlns:x=\"http://schemas.openxmlformats.org/spreadsheetml/2006/main\">");
    for (i = 0; i < s->nnotes; i++) {
      const Note *n = &s->notes[i];
      char ref[16];
      if (!n->threaded) continue;
      ref_text(n->r, n->c, 0, 0, ref);
      comment_id(si, i, 0, id);
      person_id(person(&pp, n->author ? n->author : ""), pid);
      buf_printf(&o, "<threadedComment ref=\"%s\" dT=\"2026-01-01T00:00:00.00\" personId=\"%s\" id=\"%s\"%s><text>", ref, pid, id, n->resolved ? " done=\"1\"" : "");
      esc(&o, n->text ? n->text : "");
      buf_adds(&o, "</text></threadedComment>");
      for (k = 0; comment_reply(n, k, a, sizeof a, t, sizeof t); k++) {
        comment_id(si, i, k + 1, pid2);
        person_id(person(&pp, a), pid);
        buf_printf(&o, "<threadedComment ref=\"%s\" dT=\"2026-01-01T00:00:00.00\" personId=\"%s\" id=\"%s\" parentId=\"%s\"><text>", ref, pid, pid2, id);
        esc(&o, t);
        buf_adds(&o, "</text></threadedComment>");
      }
    }
    buf_adds(&o, "</ThreadedComments>");
    sprintf(path, "xl/threadedComments/threadedComment%d.xml", si + 1);
    zip_add(w, path, o.data, o.len);
  }
  if (any) {
    o.len = 0;
    buf_adds(&o, "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n<personList xmlns=\"http://schemas.microsoft.com/office/spreadsheetml/2018/threadedcomments\" "
                 "xmlns:x=\"http://schemas.openxmlformats.org/spreadsheetml/2006/main\">");
    for (i = 0; i < pp.n; i++) {
      person_id(i, pid);
      buf_adds(&o, "<person displayName=\""); esc(&o, pp.names[i]); buf_printf(&o, "\" id=\"%s\" userId=\"", pid); esc(&o, pp.names[i]); buf_adds(&o, "\" providerId=\"None\"/>");
    }
    buf_adds(&o, "</personList>");
    zip_add(w, "xl/persons/person.xml", o.data, o.len);
  }
  for (i = 0; i < pp.n; i++) free(pp.names[i]);
  free(pp.names);
  buf_free(&o);
}
/* the text of the element just started, to its end */
static char *elem_text(Xml *x)
{
  Buf t;
  int depth = x->depth;
  buf_init(&t);
  if (x->pending_end) { buf_addc(&t, 0); return buf_take(&t); }
  while (xml_next(x) != XT_EOF) {
    if (x->type == XT_TEXT) buf_add(&t, x->text.data, x->text.len);
    else if (x->type == XT_END && x->depth < depth) break;
  }
  buf_addc(&t, 0);
  return buf_take(&t);
}
void cm_read(Book *b, ZipReader *z, const char *path, int sheet)
{
  int pi = zip_find(z, "xl/persons/person.xml"), ti = zip_find(z, path), np = 0, i;
  size_t len;
  u8 *pd = pi >= 0 ? zip_read(z, pi, &len) : NULL, *td;
  char (*pids)[48] = NULL, **pnames = NULL;
  /* the comments' ids, so replies find their thread */
  char (*cids)[48] = NULL; i32 *cr = NULL, *cc = NULL; int nc = 0;
  Xml x;
  Sheet *s = b->sh[sheet];
  if (pd) {
    xml_init(&x, (const char *)pd, len);
    while (xml_next(&x) != XT_EOF) {
      const char *id, *nm;
      if (x.type != XT_START || strcmp(x.name, "person") || !(id = xml_attr(&x, "id")) || !(nm = xml_attr(&x, "displayName")) || np > 5000) continue;
      {
        char (*np1)[48] = realloc(pids, (size_t)(np + 1) * sizeof *pids);
        char **np2 = (char **)realloc(pnames, (size_t)(np + 1) * sizeof(char *));
        if (np1) pids = np1;
        if (np2) pnames = np2;
        if (!np1 || !np2) continue;
        snprintf(pids[np], sizeof pids[np], "%s", id);
        pnames[np] = str_dup(nm);
        np++;
      }
    }
    xml_free(&x);
    free(pd);
  }
  if (ti < 0 || !(td = zip_read(z, ti, &len))) goto done;
  xml_init(&x, (const char *)td, len);
  while (xml_next(&x) != XT_EOF) {
    const char *ref, *pid, *id, *parent, *done;
    char idc[48] = "", parc[48] = "", who[200] = "", *text;
    i32 r, c;
    u16 shn; Rect rc; u8 fl;
    if (x.type != XT_START || strcmp(x.name, "threadedComment") || !(ref = xml_attr(&x, "ref"))) continue;
    if (!parse_ref(b, ref, &shn, &rc, &fl)) continue;
    r = rc.r1; c = rc.c1;
    pid = xml_attr(&x, "personId"); id = xml_attr(&x, "id"); parent = xml_attr(&x, "parentId"); done = xml_attr(&x, "done");
    if (id) snprintf(idc, sizeof idc, "%s", id);
    if (parent) snprintf(parc, sizeof parc, "%s", parent);
    for (i = 0; pid && i < np; i++) if (!strcmp(pids[i], pid)) snprintf(who, sizeof who, "%s", pnames[i] ? pnames[i] : "");
    {
      /* to the <text> element inside */
      int depth = x.depth;
      text = NULL;
      while (xml_next(&x) != XT_EOF) {
        if (x.type == XT_START && !strcmp(x.name, "text")) { text = elem_text(&x); }
        else if (x.type == XT_END && x.depth < depth) break;
      }
    }
    if (!text) continue;
    if (parc[0]) {
      /* a reply: under its thread */
      for (i = 0; i < nc && strcmp(cids[i], parc); i++) ;
      if (i < nc) {
        Note *n = NULL;
        int k;
        for (k = 0; k < s->nnotes; k++) if (s->notes[k].r == cr[i] && s->notes[k].c == cc[i]) n = &s->notes[k];
        if (n) {
          size_t ol = n->replies ? strlen(n->replies) : 0, al = strlen(who), tl = strlen(text);
          char *nr = (char *)realloc(n->replies, ol + al + tl + 3);
          if (nr) { memcpy(nr + ol, who, al); nr[ol + al] = '\037'; memcpy(nr + ol + al + 1, text, tl); nr[ol + al + 1 + tl] = '\036'; nr[ol + al + tl + 2] = 0; n->replies = nr; }
        }
      }
    } else {
      Note *n = NULL;
      int k;
      for (k = 0; k < s->nnotes; k++) if (s->notes[k].r == r && s->notes[k].c == c) n = &s->notes[k];
      if (!n) {
        Note *nn = (Note *)realloc(s->notes, (size_t)(s->nnotes + 1) * sizeof *nn);
        if (nn) { s->notes = nn; n = &nn[s->nnotes++]; memset(n, 0, sizeof *n); n->r = r; n->c = c; }
      }
      if (n) {
        /* the thread replaces the legacy note's stand-in text */
        free(n->text); free(n->author); free(n->replies);
        n->text = str_dup(text); n->author = str_dup(who); n->replies = NULL;
        n->threaded = 1; n->resolved = (u8)(done && (!strcmp(done, "1") || !strcmp(done, "true")));
        {
          char (*n1)[48] = realloc(cids, (size_t)(nc + 1) * sizeof *cids);
          i32 *n2 = (i32 *)realloc(cr, (size_t)(nc + 1) * sizeof(i32)), *n3;
          if (n1) cids = n1;
          if (n2) cr = n2;
          n3 = (i32 *)realloc(cc, (size_t)(nc + 1) * sizeof(i32));
          if (n3) cc = n3;
          if (n1 && n2 && n3 && nc < 100000) { snprintf(cids[nc], sizeof cids[nc], "%s", idc); cr[nc] = r; cc[nc] = c; nc++; }
        }
      }
    }
    free(text);
  }
  xml_free(&x);
  free(td);
done:
  for (i = 0; i < np; i++) free(pnames[i]);
  free(pnames); free(pids); free(cids); free(cr); free(cc);
}
