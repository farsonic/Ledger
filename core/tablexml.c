/*
 * tablexml.c - tables and pivot tables in .xlsx files (see tablexml.h).
 *
 * Pivot tables are written as GridCraft's xlsx crate writes them: a cache definition with every
 * source column (shared items for the fields on an axis or with text), the records, and the
 * pivot table definition (location, fields, items, data fields, style) marked refreshOnLoad, so
 * Excel and LibreOffice work the report out again themselves; the report's cells are in the
 * sheet as well.
 */
#include "tablexml.h"
#include "table.h"
#include "pivot.h"
#include "xml.h"
#include "ops.h"
#include "slicer.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>
#include <math.h>

#define NS_MAIN "http://schemas.openxmlformats.org/spreadsheetml/2006/main"
#define NS_REL "http://schemas.openxmlformats.org/officeDocument/2006/relationships"
#define CT "application/vnd.openxmlformats-officedocument.spreadsheetml."
#define XMLDECL "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"

static void esc(Buf *o, const char *s) { xml_escape(o, s, strlen(s)); }
/* numbers as files write them: the shortest text that reads back the same */
static void num(Buf *o, double n)
{
  char t[40];
  int p;
  for (p = 1; p <= 17; p++) { sprintf(t, "%.*g", p, n); if (strtod(t, NULL) == n) break; }
  buf_adds(o, t);
}
/* the book's tables and pivot tables numbered: the first number of sheet si's */
static int first_table(Book *b, int si) { int s, n = 1; for (s = 0; s < si; s++) n += b->sh[s]->ntables; return n; }
static int first_pivot(Book *b, int si) { int s, n = 1; for (s = 0; s < si; s++) n += b->sh[s]->npivots; return n; }
static int total_pivots(Book *b) { return first_pivot(b, b->nsh) - 1; }

/* ------------------------------------------------------------------ writing: slicers */
/* Slicers as Excel writes them (GridCraft keeps none in files): each sheet's pivot table slicers in
   one xl/slicers part and its table slicers in another (listed in the worksheet's extLst), a
   slicer cache for each (xl/slicerCaches, listed in the workbook's extLst) holding its source and
   selection, a defined name for each cache, and the slicer's frame in the drawing */
#define NS_X14 "http://schemas.microsoft.com/office/spreadsheetml/2009/9/main"
#define NS_X15 "http://schemas.microsoft.com/office/spreadsheetml/2010/11/main"
static int sheet_slicers(Book *b, int si, int onpivot)
{
  int i, n = 0;
  for (i = 0; i < drw_count(b->sh[si]); i++) { DObj *o = drw_obj(b->sh[si], i); if (o->kind == OB_SLICER && o->slicer && (onpivot < 0 || o->slicer->onpivot == onpivot)) n++; }
  return n;
}
/* the number of the first slicer cache of sheet si (1 on), and of its slicer parts */
static int first_slcache(Book *b, int si) { int s, n = 1; for (s = 0; s < si; s++) n += sheet_slicers(b, s, -1); return n; }
static int slpart(Book *b, int si, int onpivot)
{
  int s, n = 1;
  for (s = 0; s < si; s++) n += (sheet_slicers(b, s, 1) > 0) + (sheet_slicers(b, s, 0) > 0);
  return n + (onpivot ? 0 : sheet_slicers(b, si, 1) > 0);
}
static void slcache_name(const Slicer *s, char *out, size_t cap)
{
  char *p;
  snprintf(out, cap, "Slicer_%s", s->name);
  for (p = out + 7; *p; p++) if (!isalnum((u8)*p) && *p != '_' && (u8)*p < 0x80) *p = '_';
}
static int slicer_uses(Book *b, const char *pivot, const char *field)
{
  int s, i;
  for (s = 0; s < b->nsh; s++)
    for (i = 0; i < drw_count(b->sh[s]); i++) {
      DObj *o = drw_obj(b->sh[s], i);
      if (o->kind == OB_SLICER && o->slicer && o->slicer->onpivot && str_ieq(o->slicer->source, pivot) && str_ieq(o->slicer->field, field)) return 1;
    }
  return 0;
}
static void slicer_parts_ct(Book *b, Buf *o)
{
  int s, i, n = 0;
  for (s = 0; s < b->nsh; s++) {
    if (sheet_slicers(b, s, 1)) buf_printf(o, "<Override PartName=\"/xl/slicers/slicer%d.xml\" ContentType=\"application/vnd.ms-excel.slicer+xml\"/>", slpart(b, s, 1));
    if (sheet_slicers(b, s, 0)) buf_printf(o, "<Override PartName=\"/xl/slicers/slicer%d.xml\" ContentType=\"application/vnd.ms-excel.slicer+xml\"/>", slpart(b, s, 0));
    n += sheet_slicers(b, s, -1);
  }
  for (i = 1; i <= n; i++) buf_printf(o, "<Override PartName=\"/xl/slicerCaches/slicerCache%d.xml\" ContentType=\"application/vnd.ms-excel.slicerCache+xml\"/>", i);
}
int tablexml_has_names(Book *b) { return first_slcache(b, b->nsh) > 1; }
void tablexml_names(Book *b, Buf *o)
{
  int s, i;
  char nm[80];
  for (s = 0; s < b->nsh; s++)
    for (i = 0; i < drw_count(b->sh[s]); i++) {
      DObj *ob = drw_obj(b->sh[s], i);
      if (ob->kind != OB_SLICER || !ob->slicer) continue;
      slcache_name(ob->slicer, nm, sizeof nm);
      buf_adds(o, "<definedName name=\""); esc(o, nm); buf_adds(o, "\">#N/A</definedName>");
    }
}
static void slicer_workbook_ext(Book *b, Buf *o)
{
  int s, i, k, j, any;
  if (!tablexml_has_names(b)) return;
  buf_adds(o, "<extLst>");
  for (k = 1; k >= 0; k--) {
    for (any = 0, s = 0; s < b->nsh; s++) any += sheet_slicers(b, s, k);
    if (!any) continue;
    buf_adds(o, k ? "<ext uri=\"{BBE1A952-AA13-448e-AADC-164F8A28A991}\" xmlns:x14=\"" NS_X14 "\"><x14:slicerCaches>"
                  : "<ext uri=\"{46BE6895-7355-4a93-B00E-2C351335B9C9}\" xmlns:x15=\"" NS_X15 "\"><x15:slicerCaches xmlns:x14=\"" NS_X14 "\">");
    for (s = 0; s < b->nsh; s++)
      for (j = first_slcache(b, s), i = 0; i < drw_count(b->sh[s]); i++) {
        DObj *ob = drw_obj(b->sh[s], i);
        if (ob->kind != OB_SLICER || !ob->slicer) continue;
        if (ob->slicer->onpivot == k) buf_printf(o, "<x14:slicerCache r:id=\"rIdSC%d\"/>", j);
        j++;
      }
    buf_adds(o, k ? "</x14:slicerCaches></ext>" : "</x15:slicerCaches></ext>");
  }
  buf_adds(o, "</extLst>");
}
int tablexml_has_sheet_ext(Book *b, int si) { return sheet_slicers(b, si, -1) > 0; }
void tablexml_sheet_ext(Book *b, int si, Buf *o)
{
  if (sheet_slicers(b, si, 1)) buf_adds(o, "<ext uri=\"{A8765BA9-456A-4dab-B4F3-ACF838C121DE}\" xmlns:x14=\"" NS_X14 "\"><x14:slicerList><x14:slicer r:id=\"rIdSL1\"/></x14:slicerList></ext>");
  if (sheet_slicers(b, si, 0)) buf_adds(o, "<ext uri=\"{3A4CF648-6AED-40f4-86FF-DC5316D8AED3}\" xmlns:x15=\"" NS_X15 "\"><x14:slicerList xmlns:x14=\"" NS_X14 "\"><x14:slicer r:id=\"rIdSL2\"/></x14:slicerList></ext>");
}
static void slicer_sheet_rels(Book *b, int si, Buf *o)
{
  if (sheet_slicers(b, si, 1)) buf_printf(o, "<Relationship Id=\"rIdSL1\" Type=\"http://schemas.microsoft.com/office/2007/relationships/slicer\" Target=\"../slicers/slicer%d.xml\"/>", slpart(b, si, 1));
  if (sheet_slicers(b, si, 0)) buf_printf(o, "<Relationship Id=\"rIdSL2\" Type=\"http://schemas.microsoft.com/office/2007/relationships/slicer\" Target=\"../slicers/slicer%d.xml\"/>", slpart(b, si, 0));
}
static void slicer_parts(Book *b, ZipWriter *w)
{
  int s, i, k, j;
  Buf o;
  char path[64], nm[80];
  buf_init(&o);
  for (s = 0; s < b->nsh; s++) {
    for (k = 1; k >= 0; k--) {
      if (!sheet_slicers(b, s, k)) continue;
      o.len = 0;
      buf_adds(&o, XMLDECL "<slicers xmlns=\"" NS_X14 "\" xmlns:mc=\"http://schemas.openxmlformats.org/markup-compatibility/2006\" mc:Ignorable=\"x\" xmlns:x=\"" NS_MAIN "\">");
      for (i = 0; i < drw_count(b->sh[s]); i++) {
        DObj *ob = drw_obj(b->sh[s], i);
        Slicer *sl = ob->kind == OB_SLICER ? ob->slicer : NULL;
        if (!sl || sl->onpivot != k) continue;
        slcache_name(sl, nm, sizeof nm);
        buf_adds(&o, "<slicer name=\""); esc(&o, sl->name); buf_adds(&o, "\" cache=\""); esc(&o, nm);
        buf_adds(&o, "\" caption=\""); esc(&o, sl->caption[0] ? sl->caption : sl->field); buf_adds(&o, "\"");
        if (sl->cols > 1) buf_printf(&o, " columnCount=\"%d\"", sl->cols);
        if (!sl->header) buf_adds(&o, " showCaption=\"0\"");
        buf_adds(&o, " style=\""); esc(&o, sl->style[0] ? sl->style : "SlicerStyleLight1"); buf_adds(&o, "\" rowHeight=\"241300\"/>");
      }
      buf_adds(&o, "</slicers>");
      sprintf(path, "xl/slicers/slicer%d.xml", slpart(b, s, k));
      zip_add(w, path, o.data, o.len);
    }
    for (j = first_slcache(b, s), i = 0; i < drw_count(b->sh[s]); i++) {
      DObj *ob = drw_obj(b->sh[s], i);
      Slicer *sl = ob->kind == OB_SLICER ? ob->slicer : NULL;
      if (!sl) continue;
      o.len = 0;
      slcache_name(sl, nm, sizeof nm);
      buf_adds(&o, XMLDECL "<slicerCacheDefinition xmlns=\"" NS_X14 "\" xmlns:mc=\"http://schemas.openxmlformats.org/markup-compatibility/2006\" mc:Ignorable=\"x\" xmlns:x=\"" NS_MAIN "\" name=\"");
      esc(&o, nm); buf_adds(&o, "\" sourceName=\""); esc(&o, sl->field); buf_adds(&o, "\">");
      if (sl->onpivot) {
        int ps = -1, pi = -1, q, ss, kk, nf;
        Rect src;
        char **names;
        for (q = 0; q < b->nsh && pi < 0; q++) { int t; for (t = 0; t < b->sh[q]->npivots; t++) if (str_ieq(b->sh[q]->pivots[t].name, sl->source)) { ps = q; pi = t; break; } }
        if (pi >= 0 && pivot_source(b, &b->sh[ps]->pivots[pi], &ss, &src)) {
          LPivot *p = &b->sh[ps]->pivots[pi];
          char **labs = NULL;
          int nl = 0;
          i32 r;
          buf_adds(&o, "<pivotTables><pivotTable tabId=\""); buf_printf(&o, "%d\" name=\"", ps + 1); esc(&o, p->name); buf_adds(&o, "\"/></pivotTables>");
          nf = pivot_fields(b, p, &names);
          for (kk = 0; kk < nf && !str_ieq(names[kk], sl->field); kk++) ;
          /* the cache field's shared items: labels as they first come */
          for (r = src.r1 + 1; kk < nf && r <= src.r2; r++) {
            char lab[256];
            int any = 0, t;
            i32 c;
            for (c = src.c1; c <= src.c2 && !any; c++) if (cell_value(b, ss, r, c)->t != V_EMPTY) any = 1;
            if (!any) continue;
            pivot_item_label(b, cell_value(b, ss, r, src.c1 + kk), lab, sizeof lab);
            for (t = 0; t < nl && !str_ieq(labs[t], lab); t++) ;
            if (t == nl) { char **nn = (char **)realloc(labs, (size_t)(nl + 1) * sizeof(char *)); if (!nn) break; labs = nn; labs[nl++] = str_dup(lab); }
          }
          buf_printf(&o, "<data><tabular pivotCacheId=\"%d\"%s><items count=\"%d\">", first_pivot(b, ps) + pi, sl->sort == SS_DESC ? " sortOrder=\"descending\"" : "", nl);
          for (q = 0; q < nl; q++) {
            int on = sl->all, t;
            for (t = 0; !on && t < sl->nsel; t++) if (str_ieq(sl->sel[t], labs[q])) on = 1;
            buf_printf(&o, "<i x=\"%d\"%s/>", q, on ? " s=\"1\"" : "");
          }
          buf_adds(&o, "</items></tabular></data>");
          pivot_strings_free(labs, nl);
          pivot_strings_free(names, nf);
        }
      } else {
        int ts, t, col;
        LTable *tb = table_find(b, sl->source, &ts);
        if (tb && (col = table_col(tb, sl->field)) >= 0) {
          t = first_table(b, ts) + table_index(b, ts, tb);
          buf_printf(&o, "<extLst><x:ext uri=\"{2F2917AC-EB37-4324-AD4E-5DD8C200BD13}\" xmlns:x15=\"" NS_X15 "\"><x15:tableSlicerCache tableId=\"%d\" column=\"%d\"%s/></x:ext></extLst>",
                     t, col + 1, sl->sort == SS_DESC ? " sortOrder=\"descending\"" : "");
        }
      }
      buf_adds(&o, "</slicerCacheDefinition>");
      sprintf(path, "xl/slicerCaches/slicerCache%d.xml", j++);
      zip_add(w, path, o.data, o.len);
    }
  }
  buf_free(&o);
}

/* ------------------------------------------------------------------ writing: the small parts */
void tablexml_content_types(Book *b, Buf *o)
{
  int i, n;
  n = first_table(b, b->nsh) - 1;
  for (i = 1; i <= n; i++) buf_printf(o, "<Override PartName=\"/xl/tables/table%d.xml\" ContentType=\"" CT "table+xml\"/>", i);
  n = total_pivots(b);
  for (i = 1; i <= n; i++)
    buf_printf(o, "<Override PartName=\"/xl/pivotTables/pivotTable%d.xml\" ContentType=\"" CT "pivotTable+xml\"/>"
                  "<Override PartName=\"/xl/pivotCache/pivotCacheDefinition%d.xml\" ContentType=\"" CT "pivotCacheDefinition+xml\"/>"
                  "<Override PartName=\"/xl/pivotCache/pivotCacheRecords%d.xml\" ContentType=\"" CT "pivotCacheRecords+xml\"/>", i, i, i);
  slicer_parts_ct(b, o);
}
void tablexml_workbook(Book *b, Buf *o)
{
  int i, n = total_pivots(b);
  if (n) {
    buf_adds(o, "<pivotCaches>");
    for (i = 1; i <= n; i++) buf_printf(o, "<pivotCache cacheId=\"%d\" r:id=\"rIdC%d\"/>", i, i);
    buf_adds(o, "</pivotCaches>");
  }
  slicer_workbook_ext(b, o);
}
void tablexml_workbook_rels(Book *b, Buf *o)
{
  int i, n = total_pivots(b);
  for (i = 1; i <= n; i++)
    buf_printf(o, "<Relationship Id=\"rIdC%d\" Type=\"" NS_REL "/pivotCacheDefinition\" Target=\"pivotCache/pivotCacheDefinition%d.xml\"/>", i, i);
  n = first_slcache(b, b->nsh) - 1;
  for (i = 1; i <= n; i++)
    buf_printf(o, "<Relationship Id=\"rIdSC%d\" Type=\"http://schemas.microsoft.com/office/2007/relationships/slicerCache\" Target=\"slicerCaches/slicerCache%d.xml\"/>", i, i);
}
int tablexml_has_rels(Book *b, int si) { return b->sh[si]->ntables || b->sh[si]->npivots || sheet_slicers(b, si, -1); }
void tablexml_sheet_rels(Book *b, int si, Buf *o)
{
  Sheet *s = b->sh[si];
  int i, t0 = first_table(b, si), p0 = first_pivot(b, si);
  for (i = 0; i < s->ntables; i++)
    buf_printf(o, "<Relationship Id=\"rIdT%d\" Type=\"" NS_REL "/table\" Target=\"../tables/table%d.xml\"/>", i + 1, t0 + i);
  for (i = 0; i < s->npivots; i++)
    buf_printf(o, "<Relationship Id=\"rIdP%d\" Type=\"" NS_REL "/pivotTable\" Target=\"../pivotTables/pivotTable%d.xml\"/>", i + 1, p0 + i);
  slicer_sheet_rels(b, si, o);
}
void tablexml_sheet_parts(Book *b, int si, Buf *o)
{
  Sheet *s = b->sh[si];
  int i;
  if (!s->ntables) return;
  buf_printf(o, "<tableParts count=\"%d\">", s->ntables);
  for (i = 0; i < s->ntables; i++) buf_printf(o, "<tablePart r:id=\"rIdT%d\"/>", i + 1);
  buf_adds(o, "</tableParts>");
}
/* the sheet's AutoFilter is on table t (its rows above the totals row) */
static int filter_on(Sheet *s, const LTable *t)
{
  return s->filter.r1 >= 0 && t->header && s->filter.r1 == t->r.r1 && s->filter.c1 == t->r.c1 && s->filter.c2 == t->r.c2;
}
int tablexml_filter_in_table(Sheet *s)
{
  int i;
  for (i = 0; i < s->ntables; i++) if (filter_on(s, &s->tables[i])) return 1;
  return 0;
}

/* ------------------------------------------------------------------ writing: tables */
static const char *const totnames[TF_N] = { "none", "sum", "average", "count", "countNums", "max", "min", "stdDev", "var", "custom" };
static void write_table(Book *b, int si, LTable *t, int id, FileFormulaFn ff, Buf *o)
{
  Sheet *s = b->sh[si];
  char ref[40];
  int k, pos;
  rect_text(t->r, ref);
  buf_printf(o, XMLDECL "<table xmlns=\"" NS_MAIN "\" id=\"%d\" name=\"", id);
  esc(o, t->name); buf_adds(o, "\" displayName=\""); esc(o, t->name);
  buf_printf(o, "\" ref=\"%s\"", ref);
  if (!t->header) buf_adds(o, " headerRowCount=\"0\"");
  if (t->totals) buf_adds(o, " totalsRowCount=\"1\""); else buf_adds(o, " totalsRowShown=\"0\"");
  buf_adds(o, ">");
  if (t->header && t->filterbtn) {
    Rect f = t->r;
    f.r2 -= t->totals;
    rect_text(f, ref);
    buf_printf(o, "<autoFilter ref=\"%s\"", ref);
    if (filter_on(s, t) && s->nfcols) {
      int i;
      buf_adds(o, ">");
      for (i = 0; i < s->nfcols; i++) {
        const FilterCol *fc = &s->fcols[i];
        int j;
        buf_printf(o, "<filterColumn colId=\"%ld\"><filters%s>", (long)(fc->col - t->r.c1), fc->blanks ? " blank=\"1\"" : "");
        for (j = 0; j < fc->n; j++) { buf_adds(o, "<filter val=\""); esc(o, fc->vals[j]); buf_adds(o, "\"/>"); }
        buf_adds(o, "</filters></filterColumn>");
      }
      buf_adds(o, "</autoFilter>");
    } else buf_adds(o, "/>");
  }
  buf_printf(o, "<tableColumns count=\"%d\">", t->ncols);
  for (k = 0; k < t->ncols; k++) {
    TableCol *c = &t->cols[k];
    int body = 0;
    buf_printf(o, "<tableColumn id=\"%d\" name=\"", k + 1);
    esc(o, c->name ? c->name : "");
    buf_adds(o, "\"");
    if (t->totals) {
      if (c->totfn != TF_NONE) buf_printf(o, " totalsRowFunction=\"%s\"", totnames[c->totfn]);
      else if (c->totlabel) { buf_adds(o, " totalsRowLabel=\""); esc(o, c->totlabel); buf_adds(o, "\""); }
    }
    for (pos = 0; pos < 2; pos++) {
      const char *txt = pos ? (t->totals && c->totfn == TF_CUSTOM ? c->totformula : NULL) : c->calcformula;
      Formula *f;
      int e;
      if (!txt || !(f = formula_parse(b, si, txt, &e))) continue;
      if (!body) { buf_adds(o, ">"); body = 1; }
      table_home_r = pos ? t->r.r2 : t->r.r1 + t->header; table_home_c = t->r.c1 + k;
      buf_adds(o, pos ? "<totalsRowFormula>" : "<calculatedColumnFormula>");
      {
        Buf ft;
        buf_init(&ft);
        ff(b, si, f, &ft);
        xml_escape(o, ft.data ? ft.data : "", ft.len);
        buf_free(&ft);
      }
      buf_adds(o, pos ? "</totalsRowFormula>" : "</calculatedColumnFormula>");
      formula_free(f);
    }
    buf_adds(o, body ? "</tableColumn>" : "/>");
  }
  buf_adds(o, "</tableColumns><tableStyleInfo name=\"");
  esc(o, t->style[0] ? t->style : "TableStyleMedium2");
  buf_printf(o, "\" showFirstColumn=\"%d\" showLastColumn=\"%d\" showRowStripes=\"%d\" showColumnStripes=\"%d\"/></table>",
             t->firstcol, t->lastcol, t->bandrows, t->bandcols);
}

/* ------------------------------------------------------------------ writing: pivot tables */
/* a cache field: a source column's shared items (when itemized) and what kinds of value it has; or
   a grouping field (dates' coarser levels, items grouped by hand) or a calculated field. As in
   GridCraft's writer, a date column's finest level groups the column's own field and the coarser
   levels are fields of their own chained through par */
enum { GK_NONE, GK_DATE, GK_RANGE, GK_DISCRETE };
typedef struct {
  char *name;
  int col;                          /* source column, -1 not a database field */
  int itemized;
  char **lab; Value *val; int n, cap;
  int blank, text, number, nonint, boolean, error;
  double mn, mx;
  u8 gk, level;                     /* GK_, PG_ */
  int base, par;                    /* grouping: the column's field, the next coarser level (-1) */
  double gstart, gend, gstep;
  char **gi; int ngi;               /* group items */
  int *disc;                        /* GK_DISCRETE: each base item's group item */
  const PivotField *pf;             /* the field whose grouping this is */
  char *formula;                    /* a calculated field's */
} CField;
static int cf_index(CField *f, const char *lab)
{
  int i;
  for (i = 0; i < f->n; i++) if (str_ieq(f->lab[i], lab)) return i;
  return -1;
}
static int cf_add(CField *f, const char *lab, const Value *v)
{
  int i = cf_index(f, lab);
  if (i >= 0) return i;
  if (f->n == f->cap) {
    int nc = f->cap ? f->cap * 2 : 16;
    char **nl = (char **)realloc(f->lab, (size_t)nc * sizeof(char *));
    Value *nv;
    if (!nl) return -1;
    f->lab = nl;
    if (!(nv = (Value *)realloc(f->val, (size_t)nc * sizeof(Value)))) return -1;
    f->val = nv;
    f->cap = nc;
  }
  f->lab[f->n] = str_dup(lab);
  f->val[f->n] = val_copy(v);
  return f->n++;
}
static void gi_add(CField *f, const char *lab)
{
  char **n = (char **)realloc(f->gi, (size_t)(f->ngi + 1) * sizeof(char *));
  if (!n) return;
  f->gi = n;
  n[f->ngi++] = str_dup(lab);
}
static void iso_date(double serial, char *out)
{
  int y, m, d;
  serial_ymd(floor(serial), &y, &m, &d);
  sprintf(out, "%04d-%02d-%02dT00:00:00", y, m, d);
}
static void mdy_date(double serial, char *out)
{
  int y, m, d;
  serial_ymd(floor(serial), &y, &m, &d);
  sprintf(out, "%d/%d/%d", m, d, y);
}
static void item_xml(Buf *o, const Value *v)
{
  switch (v->t) {
  case V_NUM: buf_adds(o, "<n v=\""); num(o, v->n); buf_adds(o, "\"/>"); break;
  case V_STR: if (!v->s[0]) { buf_adds(o, "<m/>"); break; } buf_adds(o, "<s v=\""); esc(o, v->s); buf_adds(o, "\"/>"); break;
  case V_BOOL: buf_printf(o, "<b v=\"%d\"/>", v->e); break;
  case V_ERR: buf_adds(o, "<e v=\""); esc(o, err_name(v->e)); buf_adds(o, "\"/>"); break;
  default: buf_adds(o, "<m/>"); break;
  }
}
static int hidden_of(const PivotField *f, const char *lab)
{
  int i;
  for (i = 0; i < f->nhidden; i++) if (str_ieq(f->hidden[i], lab)) return 1;
  return 0;
}
static int in_area(const PivotField *f, int n, const char *name)
{
  int i;
  for (i = 0; i < n; i++) if (str_ieq(f[i].name, name)) return i;
  return -1;
}
static const char *const groupby[PG_N] = { "", "years", "quarters", "months", "days", "range" };
/* a date grouping's items: <start, the levels' items, >end (as Excel lists them) */
static void date_items(CField *f, double start, double end)
{
  char t[64];
  int y, m, d, y1;
  mdy_date(start, t + 1); t[0] = '<'; gi_add(f, t);
  switch (f->level) {
  case PG_YEARS:
    serial_ymd(floor(start), &y, &m, &d); serial_ymd(floor(end) - 1, &y1, &m, &d);
    for (; y <= y1 && f->ngi < 10000; y++) { sprintf(t, "%d", y); gi_add(f, t); }
    break;
  case PG_QUARTERS: for (y = 1; y <= 4; y++) { sprintf(t, "Qtr%d", y); gi_add(f, t); } break;
  case PG_MONTHS: { PivotField pf; Value v; memset(&pf, 0, sizeof pf); pf.group = PG_MONTHS; for (m = 1; m <= 12; m++) { v = val_num(date_serial(2000, m, 1)); pivot_field_label(NULL, &pf, &v, t, sizeof t); gi_add(f, t); } break; }
  default: { PivotField pf; Value v; memset(&pf, 0, sizeof pf); pf.group = PG_DAYS; for (d = 0; d < 366; d++) { v = val_num(date_serial(2000, 1, 1 + d)); pivot_field_label(NULL, &pf, &v, t, sizeof t); gi_add(f, t); } break; }
  }
  mdy_date(end, t + 1); t[0] = '>'; gi_add(f, t);
}
static void range_items(CField *f)
{
  PivotField pf;
  Value v;
  char t[96];
  double x;
  long k = 0;
  memset(&pf, 0, sizeof pf);
  pf.group = PG_RANGE; pf.gstart = f->gstart; pf.gend = f->gend; pf.gstep = f->gstep;
  v = val_num(f->gstart - 1); pivot_field_label(NULL, &pf, &v, t, sizeof t); gi_add(f, t);
  for (x = f->gstart; x <= f->gend && k < 10000; x = f->gstart + (++k) * f->gstep) { v = val_num(x); pivot_field_label(NULL, &pf, &v, t, sizeof t); gi_add(f, t); }
  v = val_num(f->gend + 1); pivot_field_label(NULL, &pf, &v, t, sizeof t); gi_add(f, t);
}
static CField *cf_new(CField **cf, int *n, const char *name)
{
  CField *nc = (CField *)realloc(*cf, (size_t)(*n + 1) * sizeof(CField));
  if (!nc) return NULL;
  *cf = nc;
  memset(&nc[*n], 0, sizeof(CField));
  nc[*n].name = str_dup(name);
  nc[*n].col = -1; nc[*n].base = -1; nc[*n].par = -1;
  return &nc[(*n)++];
}
static int cf_named(CField *cf, int n, const char *name) { int i; for (i = 0; i < n; i++) if (str_ieq(cf[i].name, name)) return i; return -1; }
/* the cache field an area's field is written as */
static int axis_cf(CField *cf, int ncf, int nf, const PivotField *f)
{
  int k, i;
  for (k = 0; k < nf && !str_ieq(cf[k].name, f->name); k++) ;
  if (k == nf) return -1;
  if (f->group >= PG_YEARS && f->group <= PG_DAYS) {
    for (i = 0; i < ncf; i++) if ((i == k || cf[i].base == k) && cf[i].gk == GK_DATE && cf[i].level == f->group) return i;
  } else if (f->nmg && f->group == PG_NONE) {
    for (i = nf; i < ncf; i++) if (cf[i].base == k && cf[i].gk == GK_DISCRETE) return i;
  }
  return k;
}
/* the labels of a cache field's items (what <item x> counts) */
static char **cf_labels(CField *f, int *n) { if (f->gk != GK_NONE) { *n = f->ngi; return f->gi; } *n = f->n; return f->lab; }
static void write_pivot(Book *b, int si, LPivot *p, int id, ZipWriter *w)
{
  int ss, nf, ncf, k, i, nrec = 0;
  Rect src;
  char **names = NULL, path[96], ref[40];
  CField *cf;
  Buf o, rec;
  i32 r;
  LTable *tb;
  if (!pivot_source(b, p, &ss, &src)) return;
  nf = pivot_fields(b, p, &names);
  if (!nf || !(cf = (CField *)calloc((size_t)nf, sizeof(CField)))) { pivot_strings_free(names, nf); return; }
  ncf = nf;
  for (k = 0; k < nf; k++) {
    cf[k].name = str_dup(names[k]); cf[k].col = k; cf[k].base = -1; cf[k].par = -1;
    cf[k].itemized = in_area(p->rows, p->nrows, names[k]) >= 0 || in_area(p->cols, p->ncols, names[k]) >= 0 || in_area(p->filters, p->nfilters, names[k]) >= 0
                     || in_area(p->sfilters, p->nsfilters, names[k]) >= 0 || slicer_uses(b, p->name, names[k]);
  }
  /* kinds of value, and text fields itemized too */
  for (r = src.r1 + 1; r <= src.r2; r++)
    for (k = 0; k < nf; k++) {
      const Value *v = cell_value(b, ss, r, src.c1 + k);
      CField *f = &cf[k];
      if (v->t == V_EMPTY || (v->t == V_STR && !v->s[0])) f->blank = 1;
      else if (v->t == V_STR) f->text = 1;
      else if (v->t == V_BOOL) f->boolean = 1;
      else if (v->t == V_ERR) f->error = 1;
      else if (v->t == V_NUM) {
        if (!f->number) f->mn = f->mx = v->n;
        f->number = 1;
        if (v->n < f->mn) f->mn = v->n;
        if (v->n > f->mx) f->mx = v->n;
        if (v->n != floor(v->n)) f->nonint = 1;
      }
    }
  for (k = 0; k < nf; k++) if (cf[k].text || cf[k].boolean || cf[k].error) cf[k].itemized = 1;
  /* groupings: each column's date levels (finest on its own field), intervals, groups by hand */
  for (k = 0; k < nf; k++) {
    unsigned lv = 0;
    const PivotField *rangef = NULL, *discf = NULL;
    int g, prev;
    for (i = 0; i < p->nrows + p->ncols; i++) {
      const PivotField *f = i < p->nrows ? &p->rows[i] : &p->cols[i - p->nrows];
      if (!str_ieq(f->name, names[k])) continue;
      if (f->group >= PG_YEARS && f->group <= PG_DAYS) lv |= 1u << f->group;
      else if (f->group == PG_RANGE) rangef = f;
      else if (f->nmg) discf = f;
    }
    if (lv && cf[k].number && !cf[k].text) {
      prev = k;
      for (g = PG_DAYS; g >= PG_YEARS; g--) {
        CField *t;
        if (!(lv & (1u << g))) continue;
        if (prev == k && cf[k].gk == GK_NONE) t = &cf[k];
        else {
          char nm[300];
          int s2 = 2;
          static const char *const lvn[] = { "", "Years", "Quarters", "Months", "Days" };
          snprintf(nm, sizeof nm, "%s", lvn[g]);
          while (cf_named(cf, ncf, nm) >= 0) snprintf(nm, sizeof nm, "%s%d", lvn[g], s2++);
          if (!(t = cf_new(&cf, &ncf, nm))) break;
          cf[prev].par = ncf - 1;
          prev = ncf - 1;
        }
        t->gk = GK_DATE; t->level = (u8)g; t->base = k;
        t->gstart = floor(cf[k].mn); t->gend = floor(cf[k].mx) + 1;
        date_items(t, t->gstart, t->gend);
        if (t == &cf[k]) prev = k;
      }
      cf[k].itemized = 0;
    } else if (rangef && cf[k].number && !cf[k].text) {
      cf[k].gk = GK_RANGE; cf[k].base = k; cf[k].gstart = rangef->gstart; cf[k].gend = rangef->gend; cf[k].gstep = rangef->gstep;
      range_items(&cf[k]);
      cf[k].itemized = 0;
    } else if (discf) {
      char nm[300];
      int s2 = 2;
      CField *t;
      snprintf(nm, sizeof nm, "%s%d", names[k], s2++);
      while (cf_named(cf, ncf, nm) >= 0) snprintf(nm, sizeof nm, "%s%d", names[k], s2++);
      if ((t = cf_new(&cf, &ncf, nm))) { t->gk = GK_DISCRETE; t->base = k; t->pf = discf; cf[k].itemized = 1; }
    }
  }
  /* the records (rows not wholly empty), shared items as they come */
  buf_init(&rec);
  for (r = src.r1 + 1; r <= src.r2; r++) {
    int any = 0;
    for (k = 0; k < nf && !any; k++) if (cell_value(b, ss, r, src.c1 + k)->t != V_EMPTY) any = 1;
    if (!any) continue;
    buf_adds(&rec, "<r>");
    for (k = 0; k < nf; k++) {
      const Value *v = cell_value(b, ss, r, src.c1 + k);
      if (cf[k].itemized) {
        char lab[256];
        pivot_item_label(b, v, lab, sizeof lab);
        buf_printf(&rec, "<x v=\"%d\"/>", cf_add(&cf[k], lab, v));
      } else if (cf[k].gk == GK_DATE && v->t == V_NUM) { char t[40]; iso_date(v->n, t); buf_printf(&rec, "<d v=\"%s\"/>", t); }
      else item_xml(&rec, v);
    }
    buf_adds(&rec, "</r>");
    nrec++;
  }
  /* groups by hand: the group names, then the items in no group, each base item's group */
  for (k = nf; k < ncf; k++) {
    CField *t = &cf[k], *bf;
    int j, g;
    if (t->gk != GK_DISCRETE) continue;
    bf = &cf[t->base];
    for (g = 0; g < t->pf->nmg; g++) gi_add(t, t->pf->mg[g].name);
    t->disc = (int *)calloc((size_t)bf->n + 1, sizeof(int));
    for (j = 0; j < bf->n && t->disc; j++) {
      char lab[256];
      int x;
      pivot_field_label(b, t->pf, &bf->val[j], lab, sizeof lab);
      for (x = 0; x < t->ngi && !str_ieq(t->gi[x], lab); x++) ;
      if (x == t->ngi) gi_add(t, lab);
      t->disc[j] = x;
    }
  }
  /* calculated fields last */
  for (i = 0; i < p->ncalcs; i++) {
    CField *t;
    if (cf_named(cf, ncf, p->calcs[i].name) >= 0 || !(t = cf_new(&cf, &ncf, p->calcs[i].name))) continue;
    t->formula = str_dup(p->calcs[i].formula);
  }
  /* the cache definition */
  buf_init(&o);
  buf_printf(&o, XMLDECL "<pivotCacheDefinition xmlns=\"" NS_MAIN "\" xmlns:r=\"" NS_REL "\" r:id=\"rId1\" refreshOnLoad=\"1\" refreshedBy=\"Ledger\" "
                 "createdVersion=\"6\" refreshedVersion=\"6\" minRefreshableVersion=\"3\" recordCount=\"%d\"><cacheSource type=\"worksheet\">", nrec);
  if ((tb = table_find(b, p->source, NULL)) && !strcmp(tb->name, p->source)) { buf_adds(&o, "<worksheetSource name=\""); esc(&o, tb->name); buf_adds(&o, "\"/>"); }
  else {
    char sn[32];
    rect_text(src, ref);
    snprintf(sn, sizeof sn, "%s", b->sh[ss]->name);
    buf_printf(&o, "<worksheetSource ref=\"%s\" sheet=\"", ref);
    esc(&o, sn);
    buf_adds(&o, "\"/>");
  }
  buf_printf(&o, "</cacheSource><cacheFields count=\"%d\">", ncf);
  for (k = 0; k < ncf; k++) {
    CField *f = &cf[k];
    int strings = f->text || f->boolean || f->error;
    buf_adds(&o, "<cacheField name=\""); esc(&o, f->name);
    buf_printf(&o, "\" numFmtId=\"%d\"", f->gk == GK_DATE ? 14 : 0);
    if (f->formula) { buf_adds(&o, " formula=\""); esc(&o, f->formula); buf_adds(&o, "\""); }
    if (f->col < 0) buf_adds(&o, " databaseField=\"0\"");
    buf_adds(&o, ">");
    if (f->col >= 0) {
      buf_adds(&o, "<sharedItems");
      if (f->gk == GK_DATE) {
        char a[40], z[40];
        iso_date(f->mn, a); iso_date(f->mx, z);
        buf_adds(&o, " containsSemiMixedTypes=\"0\" containsNonDate=\"0\" containsDate=\"1\" containsString=\"0\"");
        if (f->blank) buf_adds(&o, " containsBlank=\"1\"");
        buf_printf(&o, " minDate=\"%s\" maxDate=\"%s\"", a, z);
      } else {
        if (!strings && !f->blank) buf_adds(&o, " containsSemiMixedTypes=\"0\"");
        if (!strings) buf_adds(&o, " containsString=\"0\"");
        if (f->blank) buf_adds(&o, " containsBlank=\"1\"");
        if (strings && f->number) buf_adds(&o, " containsMixedTypes=\"1\"");
        if (f->number) {
          buf_adds(&o, " containsNumber=\"1\"");
          if (!f->nonint) buf_adds(&o, " containsInteger=\"1\"");
          buf_adds(&o, " minValue=\""); num(&o, f->mn); buf_adds(&o, "\" maxValue=\""); num(&o, f->mx); buf_adds(&o, "\"");
        }
      }
      if (f->itemized) {
        buf_printf(&o, " count=\"%d\">", f->n);
        for (i = 0; i < f->n; i++) item_xml(&o, &f->val[i]);
        buf_adds(&o, "</sharedItems>");
      } else buf_adds(&o, "/>");
    }
    if (f->gk != GK_NONE) {
      buf_adds(&o, "<fieldGroup");
      if (f->par >= 0) buf_printf(&o, " par=\"%d\"", f->par);
      buf_printf(&o, " base=\"%d\">", f->base);
      if (f->gk == GK_DATE) {
        char a[40], z[40];
        iso_date(f->gstart, a); iso_date(f->gend, z);
        buf_printf(&o, "<rangePr groupBy=\"%s\" startDate=\"%s\" endDate=\"%s\"/>", groupby[f->level], a, z);
      } else if (f->gk == GK_RANGE) {
        buf_adds(&o, "<rangePr autoStart=\"0\" autoEnd=\"0\" startNum=\""); num(&o, f->gstart);
        buf_adds(&o, "\" endNum=\""); num(&o, f->gend); buf_adds(&o, "\" groupInterval=\""); num(&o, f->gstep); buf_adds(&o, "\"/>");
      } else if (f->disc) {
        CField *bf = &cf[f->base];
        buf_printf(&o, "<discretePr count=\"%d\">", bf->n);
        for (i = 0; i < bf->n; i++) buf_printf(&o, "<x v=\"%d\"/>", f->disc[i]);
        buf_adds(&o, "</discretePr>");
      }
      buf_printf(&o, "<groupItems count=\"%d\">", f->ngi);
      for (i = 0; i < f->ngi; i++) { buf_adds(&o, "<s v=\""); esc(&o, f->gi[i]); buf_adds(&o, "\"/>"); }
      buf_adds(&o, "</groupItems></fieldGroup>");
    }
    buf_adds(&o, "</cacheField>");
  }
  buf_adds(&o, "</cacheFields>");
  /* (an id slicers' caches name the cache by) */
  buf_printf(&o, "<extLst><ext uri=\"{725AE2AE-9491-48be-B2B4-4EB974FC3084}\" xmlns:x14=\"http://schemas.microsoft.com/office/spreadsheetml/2009/9/main\">"
                 "<x14:pivotCacheDefinition pivotCacheId=\"%d\"/></ext></extLst>", id);
  buf_adds(&o, "</pivotCacheDefinition>");
  sprintf(path, "xl/pivotCache/pivotCacheDefinition%d.xml", id);
  zip_add(w, path, o.data, o.len);
  o.len = 0;
  buf_printf(&o, XMLDECL "<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">"
                 "<Relationship Id=\"rId1\" Type=\"" NS_REL "/pivotCacheRecords\" Target=\"pivotCacheRecords%d.xml\"/></Relationships>", id);
  sprintf(path, "xl/pivotCache/_rels/pivotCacheDefinition%d.xml.rels", id);
  zip_add(w, path, o.data, o.len);
  o.len = 0;
  buf_printf(&o, XMLDECL "<pivotCacheRecords xmlns=\"" NS_MAIN "\" xmlns:r=\"" NS_REL "\" count=\"%d\">", nrec);
  buf_add(&o, rec.data ? rec.data : "", rec.len);
  buf_adds(&o, "</pivotCacheRecords>");
  sprintf(path, "xl/pivotCache/pivotCacheRecords%d.xml", id);
  zip_add(w, path, o.data, o.len);
  buf_free(&rec);
  /* the pivot table */
  o.len = 0;
  {
    int R = 0, C = 0, V = 0, compact = p->layout == PL_COMPACT, fdr, fdc, *rowf, *colf, *pagef, np = 0, *fieldof;
    const PivotField **afield;      /* each cache field's area field (axis or page), or a slicer's filter */
    u8 *axis;                       /* 1 row 2 column 3 page 4 slicer filter */
    Rect loc;
    rowf = (int *)malloc(((size_t)p->nrows + 1) * sizeof(int));
    colf = (int *)malloc(((size_t)p->ncols + 1) * sizeof(int));
    pagef = (int *)malloc(((size_t)p->nfilters + 1) * sizeof(int));
    fieldof = (int *)malloc(((size_t)p->nvals + 1) * sizeof(int));
    afield = (const PivotField **)calloc((size_t)ncf + 1, sizeof(PivotField *));
    axis = (u8 *)calloc((size_t)ncf + 1, 1);
    if (!rowf || !colf || !pagef || !fieldof || !afield || !axis) { free(rowf); free(colf); free(pagef); free(fieldof); free(afield); free(axis); goto done; }
    for (i = 0; i < p->nrows; i++) { int x = axis_cf(cf, ncf, nf, &p->rows[i]); if (x >= 0 && !axis[x]) { rowf[R++] = x; axis[x] = 1; afield[x] = &p->rows[i]; } }
    for (i = 0; i < p->ncols; i++) { int x = axis_cf(cf, ncf, nf, &p->cols[i]); if (x >= 0 && !axis[x]) { colf[C++] = x; axis[x] = 2; afield[x] = &p->cols[i]; } }
    for (i = 0; i < p->nfilters; i++) { int x = axis_cf(cf, ncf, nf, &p->filters[i]); if (x >= 0 && !axis[x]) { pagef[np++] = x; axis[x] = 3; afield[x] = &p->filters[i]; } }
    for (i = 0; i < p->nsfilters; i++) { int x = cf_named(cf, nf, p->sfilters[i].name); if (x >= 0 && !axis[x] && p->sfilters[i].nhidden) { axis[x] = 4; afield[x] = &p->sfilters[i]; } }
    for (i = 0; i < p->nvals; i++) { fieldof[i] = cf_named(cf, ncf, p->vals[i].name); if (fieldof[i] >= 0 && (cf[fieldof[i]].col >= 0 || cf[fieldof[i]].formula)) V++; else fieldof[i] = -1; }
    buf_adds(&o, XMLDECL "<pivotTableDefinition xmlns=\"" NS_MAIN "\" name=\"");
    esc(&o, p->name);
    buf_printf(&o, "\" cacheId=\"%d\" applyNumberFormats=\"0\" applyBorderFormats=\"0\" applyFontFormats=\"0\" applyPatternFormats=\"0\" applyAlignmentFormats=\"0\" "
                   "applyWidthHeightFormats=\"1\" dataCaption=\"Values\" updatedVersion=\"6\" minRefreshableVersion=\"3\" useAutoFormatting=\"1\" itemPrintTitles=\"1\" "
                   "createdVersion=\"6\" indent=\"0\"", id);
    if (p->layout == PL_COMPACT) buf_adds(&o, " outline=\"1\" outlineData=\"1\"");
    else if (p->layout == PL_OUTLINE) buf_adds(&o, " compact=\"0\" compactData=\"0\" outline=\"1\" outlineData=\"1\"");
    else buf_adds(&o, " compact=\"0\" compactData=\"0\" gridDropZones=\"1\"");
    if (!p->grandcols) buf_adds(&o, " rowGrandTotals=\"0\"");
    if (!p->grandrows) buf_adds(&o, " colGrandTotals=\"0\"");
    buf_adds(&o, " multipleFieldFilters=\"0\">");
    loc.r1 = p->bodyrow; loc.c1 = p->c; loc.r2 = p->out.r1 >= 0 ? p->out.r2 : p->r; loc.c2 = p->out.r1 >= 0 ? p->out.c2 : p->c;
    if (loc.r2 < loc.r1) loc.r2 = loc.r1;
    if (p->out.r1 < 0) loc.r1 = p->r;
    rect_text(loc, ref);
    fdr = C ? 1 + C + (V > 1) : 1;
    fdc = (!R && !C) ? 0 : compact ? 1 : (R ? R : 1);
    buf_printf(&o, "<location ref=\"%s\" firstHeaderRow=\"1\" firstDataRow=\"%d\" firstDataCol=\"%d\"", ref, fdr, fdc);
    if (np) buf_printf(&o, " rowPageCount=\"%d\" colPageCount=\"1\"", np);
    buf_printf(&o, "/><pivotFields count=\"%d\">", ncf);
    for (k = 0; k < ncf; k++) {
      CField *f = &cf[k];
      const PivotField *af = afield[k];
      const char *ax = axis[k] == 1 ? "axisRow" : axis[k] == 2 ? "axisCol" : axis[k] == 3 ? "axisPage" : NULL;
      int isdata = 0, nl, *ord = NULL;
      char **labs = cf_labels(f, &nl);
      for (i = 0; i < p->nvals; i++) if (fieldof[i] == k) isdata = 1;
      buf_adds(&o, "<pivotField");
      if (ax) buf_printf(&o, " axis=\"%s\"", ax);
      if (isdata) buf_adds(&o, " dataField=\"1\"");
      if (f->formula) buf_adds(&o, " dragToRow=\"0\" dragToCol=\"0\" dragToPage=\"0\"");
      if (p->layout == PL_OUTLINE) buf_adds(&o, " compact=\"0\"");
      else if (p->layout == PL_TABULAR) buf_adds(&o, " compact=\"0\" outline=\"0\"");
      if (f->gk == GK_DATE) buf_adds(&o, " numFmtId=\"14\"");
      if (af && af->nhidden && axis[k] == 3) buf_adds(&o, " multipleItemSelectionAllowed=\"1\"");
      buf_adds(&o, " showAll=\"0\"");
      if (af && axis[k] <= 2) buf_adds(&o, af->sort == PO_DESC ? " sortType=\"descending\"" : af->sort == PO_ASC ? " sortType=\"ascending\"" : "");
      if (!p->subtotals || f->formula) buf_adds(&o, " defaultSubtotal=\"0\"");
      if (p->subtotals && !p->subtop) buf_adds(&o, " subtotalTop=\"0\"");
      if (p->blankrows && axis[k] == 1) buf_adds(&o, " insertBlankRow=\"1\"");
      if (!ax && !(axis[k] == 4 && f->itemized)) { buf_adds(&o, "/>"); continue; }
      /* the items in report order, those filtered out hidden, those collapsed sd="0" */
      ord = nl ? (int *)malloc((size_t)nl * sizeof(int)) : NULL;
      buf_printf(&o, "><items count=\"%d\">", nl + (p->subtotals ? 1 : 0));
      if (ord) {
        char **items = NULL;
        int ni = f->gk == GK_NONE && af ? pivot_field_items_of(b, p, af->name, axis[k] == 4 ? NULL : af, &items) : 0, j, shown = 0, m = 0;
        for (j = 0; j < ni; j++) { int x; for (x = 0; x < nl && !str_ieq(labs[x], items[j]); x++) ; if (x < nl) { int seen = 0, q; for (q = 0; q < m; q++) if (ord[q] == x) seen = 1; if (!seen) ord[m++] = x; } }
        for (i = 0; i < nl && m < nl; i++) { int seen = 0; for (j = 0; j < m; j++) if (ord[j] == i) seen = 1; if (!seen) ord[m++] = i; }
        for (i = 0; i < nl; i++) if (!(af && hidden_of(af, labs[ord[i]]))) shown++;
        for (i = 0; i < nl; i++) {
          buf_printf(&o, "<item x=\"%d\"", ord[i]);
          if (shown && af && hidden_of(af, labs[ord[i]])) buf_adds(&o, " h=\"1\"");
          if (af && axis[k] <= 2 && pivot_is_collapsed(af, labs[ord[i]])) buf_adds(&o, " sd=\"0\"");
          buf_adds(&o, "/>");
        }
        pivot_strings_free(items, ni);
        free(ord);
      }
      if (p->subtotals) buf_adds(&o, "<item t=\"default\"/>");
      buf_adds(&o, "</items></pivotField>");
    }
    buf_adds(&o, "</pivotFields>");
    if (R) { buf_printf(&o, "<rowFields count=\"%d\">", R); for (i = 0; i < R; i++) buf_printf(&o, "<field x=\"%d\"/>", rowf[i]); buf_adds(&o, "</rowFields>"); }
    if (C || V > 1) {
      buf_printf(&o, "<colFields count=\"%d\">", C + (V > 1));
      for (i = 0; i < C; i++) buf_printf(&o, "<field x=\"%d\"/>", colf[i]);
      if (V > 1) buf_adds(&o, "<field x=\"-2\"/>");
      buf_adds(&o, "</colFields>");
    }
    if (np) {
      buf_printf(&o, "<pageFields count=\"%d\">", np);
      for (i = 0; i < np; i++) {
        CField *f = &cf[pagef[i]];
        const PivotField *pf = afield[pagef[i]];
        int j, shown = 0, last = -1, nl;
        char **labs = cf_labels(f, &nl);
        for (j = 0; j < nl; j++) if (!hidden_of(pf, labs[j])) { shown++; last = j; }
        buf_printf(&o, "<pageField fld=\"%d\"", pagef[i]);
        if (pf->nhidden && shown == 1) buf_printf(&o, " item=\"%d\"", last);
        buf_adds(&o, " hier=\"-1\"/>");
      }
      buf_adds(&o, "</pageFields>");
    }
    if (V) {
      static const char *const sub[PF_N] = { "sum", "count", "average", "max", "min", "product", "countNums", "stdDev", "stdDevp", "var", "varp" };
      static const char *const showas[PS_N] = { "", "percentOfTotal", "percentOfCol", "percentOfRow", "runTotal", "" };
      int basef = R ? rowf[0] : C ? colf[0] : 0;
      buf_printf(&o, "<dataFields count=\"%d\">", V);
      for (i = 0; i < p->nvals; i++) {
        char cap[300];
        int sa = p->vals[i].showas;
        if (fieldof[i] < 0) continue;
        pivot_caption(&p->vals[i], cap, sizeof cap);
        buf_adds(&o, "<dataField name=\""); esc(&o, cap); buf_printf(&o, "\" fld=\"%d\"", fieldof[i]);
        if (p->vals[i].func != PF_SUM && p->vals[i].func < PF_N) buf_printf(&o, " subtotal=\"%s\"", sub[p->vals[i].func]);
        if (sa > PS_NORMAL && sa < PS_N && showas[sa][0]) buf_printf(&o, " showDataAs=\"%s\"", showas[sa]);
        buf_printf(&o, " baseField=\"%d\" baseItem=\"0\"", sa == PS_RUNTOTAL || sa == PS_RANK ? basef : 0);
        if (sa >= PS_PCT_GRAND && sa <= PS_PCT_ROW) buf_adds(&o, " numFmtId=\"10\"");
        if (sa == PS_RANK) buf_adds(&o, "><extLst><ext uri=\"{E15A36E0-9728-4e99-A89B-3F7291B0FE68}\" xmlns:x14=\"http://schemas.microsoft.com/office/spreadsheetml/2009/9/main\">"
                                        "<x14:dataField pivotShowAs=\"rankDescending\"/></ext></extLst></dataField>");
        else buf_adds(&o, "/>");
      }
      buf_adds(&o, "</dataFields>");
    }
    buf_adds(&o, "<pivotTableStyleInfo");
    if (p->style[0]) { buf_adds(&o, " name=\""); esc(&o, p->style); buf_adds(&o, "\""); }
    buf_printf(&o, " showRowHeaders=\"%d\" showColHeaders=\"%d\" showRowStripes=\"%d\" showColStripes=\"%d\" showLastColumn=\"1\"/></pivotTableDefinition>",
               p->rowheaders, p->colheaders, p->bandrows, p->bandcols);
    free(rowf); free(colf); free(pagef); free(fieldof); free(afield); free(axis);
  }
  sprintf(path, "xl/pivotTables/pivotTable%d.xml", id);
  zip_add(w, path, o.data, o.len);
  o.len = 0;
  buf_printf(&o, XMLDECL "<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">"
                 "<Relationship Id=\"rId1\" Type=\"" NS_REL "/pivotCacheDefinition\" Target=\"../pivotCache/pivotCacheDefinition%d.xml\"/></Relationships>", id);
  sprintf(path, "xl/pivotTables/_rels/pivotTable%d.xml.rels", id);
  zip_add(w, path, o.data, o.len);
done:
  buf_free(&o);
  for (k = 0; k < ncf; k++) {
    for (i = 0; i < cf[k].n; i++) { free(cf[k].lab[i]); val_free(&cf[k].val[i]); }
    for (i = 0; i < cf[k].ngi; i++) free(cf[k].gi[i]);
    free(cf[k].lab); free(cf[k].val); free(cf[k].gi); free(cf[k].disc); free(cf[k].name); free(cf[k].formula);
  }
  free(cf);
  pivot_strings_free(names, nf);
  (void)si;
}
void tablexml_parts(Book *b, ZipWriter *w, FileFormulaFn ff)
{
  int s, i, id = 1;
  Buf o;
  char path[64];
  buf_init(&o);
  for (s = 0; s < b->nsh; s++)
    for (i = 0; i < b->sh[s]->ntables; i++) {
      o.len = 0;
      write_table(b, s, &b->sh[s]->tables[i], id, ff, &o);
      sprintf(path, "xl/tables/table%d.xml", id++);
      zip_add(w, path, o.data, o.len);
    }
  buf_free(&o);
  id = 1;
  for (s = 0; s < b->nsh; s++) for (i = 0; i < b->sh[s]->npivots; i++) write_pivot(b, s, &b->sh[s]->pivots[i], id++, w);
  slicer_parts(b, w);
}

/* ------------------------------------------------------------------ reading */
static u8 *part(ZipReader *z, const char *name, size_t *len)
{
  int i = zip_find(z, name);
  return i < 0 ? NULL : zip_read(z, i, len);
}
static void tidy(char *p)
{
  char *d;
  while ((d = strstr(p, "/../"))) {
    char *s = d;
    while (s > p && s[-1] != '/') s--;
    memmove(s, d + 4, strlen(d + 4) + 1);
  }
}
/* the target of the first relationship of type (the Type URL's last part) in a part's rels */
static int rel_target(ZipReader *z, const char *partpath, const char *type, char *out, size_t cap)
{
  char rp[300], base[256];
  const char *sl = strrchr(partpath, '/');
  size_t dl = sl ? (size_t)(sl - partpath + 1) : 0, len;
  u8 *d;
  Xml x;
  int found = 0;
  if (dl >= sizeof base) return 0;
  memcpy(base, partpath, dl); base[dl] = 0;
  snprintf(rp, sizeof rp, "%s_rels/%s.rels", base, sl ? sl + 1 : partpath);
  if (!(d = part(z, rp, &len))) return 0;
  xml_init(&x, (const char *)d, len);
  while (!found && xml_next(&x) != XT_EOF) {
    const char *ty, *t, *ls;
    if (x.type != XT_START || strcmp(x.name, "Relationship")) continue;
    ty = xml_attr(&x, "Type"); t = xml_attr(&x, "Target");
    if (!ty || !t) continue;
    ls = strrchr(ty, '/');
    if (strcmp(ls ? ls + 1 : ty, type)) continue;
    if (t[0] == '/') snprintf(out, cap, "%s", t + 1);
    else { snprintf(out, cap, "%s%s", base, t); tidy(out); }
    found = 1;
  }
  xml_free(&x);
  free(d);
  return found;
}
static int attr1(Xml *x, const char *name, int dflt)
{
  const char *v = xml_attr(x, name);
  if (!v) return dflt;
  return !strcmp(v, "1") || !strcmp(v, "true");
}
static int a1_rect(const char *s, Rect *r)
{
  u16 sh; u8 fl;
  int n = parse_ref(NULL, s, &sh, r, &fl);
  return n > 0 && !s[n];
}
/* the text of the element just started, up to its end */
static char *elem_text(Xml *x)
{
  Buf t;
  buf_init(&t);
  if (!x->pending_end) while (xml_next(x) == XT_TEXT) buf_add(&t, x->text.data, x->text.len);
  if (!t.data) buf_adds(&t, "");
  return buf_take(&t);
}
/* a formula text from a file, tidied as Ledger keeps them (NULL if it doesn't parse) */
static char *tidy_formula(Book *b, int sheet, const char *txt)
{
  int pos;
  Formula *f = formula_parse(b, sheet, txt[0] == '=' ? txt + 1 : txt, &pos);
  Buf o;
  if (!f) return NULL;
  buf_init(&o);
  formula_text(b, sheet, f, &o);
  formula_free(f);
  return buf_take(&o);
}
static void read_table(Book *b, ZipReader *z, const char *path, int sheet)
{
  size_t len;
  u8 *d = part(z, path, &len);
  Xml x;
  LTable t, *nt;
  Sheet *s = b->sh[sheet];
  int colk = -1, curf = -1, filt = 0, i;
  Rect fr;
  if (!d) return;
  memset(&t, 0, sizeof t);
  t.header = 1; t.filterbtn = 0;
  fr.r1 = -1;
  xml_init(&x, (const char *)d, len);
  while (xml_next(&x) != XT_EOF) {
    if (x.type != XT_START) continue;
    if (!strcmp(x.name, "table")) {
      const char *nm = xml_attr(&x, "displayName"), *ref = xml_attr(&x, "ref"), *h = xml_attr(&x, "headerRowCount"), *tc = xml_attr(&x, "totalsRowCount");
      if (!nm) nm = xml_attr(&x, "name");
      if (nm) snprintf(t.name, sizeof t.name, "%s", nm);
      if (!ref || !a1_rect(ref, &t.r)) break;
      t.header = !h || atoi(h) != 0;
      t.totals = tc && atoi(tc) > 0;
      t.id = (u32)(xml_attr(&x, "id") ? atol(xml_attr(&x, "id")) : 0);
    } else if (!strcmp(x.name, "autoFilter")) {
      const char *ref = xml_attr(&x, "ref");
      t.filterbtn = 1;
      if (ref && a1_rect(ref, &fr)) filt = 1;
    } else if (!strcmp(x.name, "filterColumn") && filt) {
      const char *id = xml_attr(&x, "colId");
      FilterCol *nf;
      curf = -1;
      if (s->filter.r1 < 0 || (s->filter.r1 == fr.r1 && s->filter.c1 == fr.c1)) {
        if (s->filter.r1 < 0) s->filter = fr;
        if (id && (nf = (FilterCol *)realloc(s->fcols, (size_t)(s->nfcols + 1) * sizeof *nf))) {
          s->fcols = nf; curf = s->nfcols++;
          memset(&nf[curf], 0, sizeof *nf);
          nf[curf].col = fr.c1 + atoi(id);
        }
      }
    } else if (!strcmp(x.name, "filters") && curf >= 0) {
      s->fcols[curf].blanks = (u8)attr1(&x, "blank", 0);
    } else if (!strcmp(x.name, "filter") && curf >= 0) {
      const char *v = xml_attr(&x, "val");
      FilterCol *f = &s->fcols[curf];
      char **nv = v ? (char **)realloc(f->vals, (size_t)(f->n + 1) * sizeof(char *)) : NULL;
      if (nv) { f->vals = nv; f->vals[f->n++] = str_dup(v); }
    } else if (!strcmp(x.name, "tableColumn")) {
      const char *nm = xml_attr(&x, "name"), *fn = xml_attr(&x, "totalsRowFunction"), *lab = xml_attr(&x, "totalsRowLabel");
      TableCol *nc = (TableCol *)realloc(t.cols, (size_t)(t.ncols + 1) * sizeof(TableCol));
      if (!nc || t.ncols >= 1000) continue;
      t.cols = nc;
      colk = t.ncols++;
      memset(&nc[colk], 0, sizeof(TableCol));
      nc[colk].name = str_dup(nm ? nm : "");
      if (fn) for (i = 0; i < TF_N; i++) if (!strcmp(fn, totnames[i])) nc[colk].totfn = (u8)i;
      if (lab) nc[colk].totlabel = str_dup(lab);
    } else if ((!strcmp(x.name, "calculatedColumnFormula") || !strcmp(x.name, "totalsRowFormula")) && colk >= 0) {
      int calc = !strcmp(x.name, "calculatedColumnFormula");
      char *txt = elem_text(&x), *f = txt ? tidy_formula(b, sheet, txt) : NULL;
      if (f) { char **dst = calc ? &t.cols[colk].calcformula : &t.cols[colk].totformula; free(*dst); *dst = f; }
      free(txt);
    } else if (!strcmp(x.name, "tableStyleInfo")) {
      const char *nm = xml_attr(&x, "name");
      snprintf(t.style, sizeof t.style, "%s", nm ? nm : "");
      t.firstcol = (u8)attr1(&x, "showFirstColumn", 0);
      t.lastcol = (u8)attr1(&x, "showLastColumn", 0);
      t.bandrows = (u8)attr1(&x, "showRowStripes", 0);
      t.bandcols = (u8)attr1(&x, "showColumnStripes", 0);
    }
  }
  xml_free(&x);
  free(d);
  /* a sound table: a name, a range its columns fit, no clash */
  if (!t.name[0] || t.r.r2 < t.r.r1 || t.ncols != t.r.c2 - t.r.c1 + 1 || table_find(b, t.name, NULL) || table_at(b, sheet, t.r.r1, t.r.c1)) {
    for (i = 0; i < t.ncols; i++) { free(t.cols[i].name); free(t.cols[i].totlabel); free(t.cols[i].totformula); free(t.cols[i].calcformula); }
    free(t.cols);
    return;
  }
  /* rows Excel hid for the table's filter are the filter's */
  if (s->filter.r1 >= 0 && s->nfcols && s->filter.r1 == t.r.r1 && s->filter.c1 == t.r.c1) {
    i32 r;
    for (r = s->filter.r1 + 1; r <= s->filter.r2 && r < s->nrows; r++)
      if (s->rows[r] && s->rows[r]->hidden) { s->rows[r]->hidden = 0; s->rows[r]->filtered = 1; }
  }
  if (!(nt = (LTable *)realloc(s->tables, (size_t)(s->ntables + 1) * sizeof(LTable)))) return;
  s->tables = nt;
  s->tables[s->ntables++] = t;
}
/* a pivot cache: the source and its fields' names, shared items' labels, groupings and formulas */
typedef struct {
  char *formula;                  /* a calculated field's */
  int base, par;                  /* a grouping's column field (-1 none) */
  u8 gk, level;                   /* GK_, PG_ */
  double gstart, gend, gstep;
  char **gi; int ngi;             /* group items */
  int *disc; int ndisc;           /* discretePr */
} CacheF;
typedef struct { char source[256]; char **names; int nf; char ***items; int *nitems; CacheF *cf; int cacheid; } Cache;
static void cache_free(Cache *c)
{
  int k;
  for (k = 0; k < c->nf; k++) {
    pivot_strings_free(c->items[k], c->nitems[k]); free(c->names[k]);
    if (c->cf) { free(c->cf[k].formula); pivot_strings_free(c->cf[k].gi, c->cf[k].ngi); free(c->cf[k].disc); }
  }
  free(c->names); free(c->items); free(c->nitems); free(c->cf);
}
static void strs_push(char ***v, int *n, const char *s)
{
  char **nv = (char **)realloc(*v, (size_t)(*n + 1) * sizeof(char *));
  if (!nv) return;
  *v = nv;
  nv[(*n)++] = str_dup(s);
}
static int level_of(const char *g)
{
  int i;
  for (i = PG_YEARS; i < PG_N; i++) if (!strcmp(g, groupby[i])) return i;
  return PG_NONE;
}
static int read_cache(Book *b, ZipReader *z, const char *path, Cache *c)
{
  size_t len;
  u8 *d = part(z, path, &len);
  Xml x;
  int cur = -1, insh = 0, ingi = 0;
  char sheet[64] = "", ref[64] = "";
  memset(c, 0, sizeof *c);
  if (!d) return 0;
  xml_init(&x, (const char *)d, len);
  while (xml_next(&x) != XT_EOF) {
    if (x.type == XT_END && !strcmp(x.name, "sharedItems")) { insh = 0; continue; }
    if (x.type == XT_END && !strcmp(x.name, "groupItems")) { ingi = 0; continue; }
    if (x.type != XT_START) continue;
    if (!strcmp(x.name, "worksheetSource")) {
      const char *nm = xml_attr(&x, "name"), *r = xml_attr(&x, "ref"), *sh = xml_attr(&x, "sheet");
      if (nm) snprintf(c->source, sizeof c->source, "%s", nm);
      if (r) snprintf(ref, sizeof ref, "%s", r);
      if (sh) snprintf(sheet, sizeof sheet, "%s", sh);
    } else if (!strcmp(x.name, "cacheField")) {
      const char *nm = xml_attr(&x, "name"), *fo = xml_attr(&x, "formula");
      char **nn = (char **)realloc(c->names, (size_t)(c->nf + 1) * sizeof(char *));
      char ***ni;
      int *nc;
      CacheF *ncf;
      if (!nn) continue;
      c->names = nn;
      if (!(ni = (char ***)realloc(c->items, (size_t)(c->nf + 1) * sizeof(char **)))) continue;
      c->items = ni;
      if (!(nc = (int *)realloc(c->nitems, (size_t)(c->nf + 1) * sizeof(int)))) continue;
      c->nitems = nc;
      if (!(ncf = (CacheF *)realloc(c->cf, (size_t)(c->nf + 1) * sizeof(CacheF)))) continue;
      c->cf = ncf;
      cur = c->nf++;
      c->names[cur] = str_dup(nm ? nm : "");
      c->items[cur] = NULL; c->nitems[cur] = 0;
      memset(&c->cf[cur], 0, sizeof(CacheF));
      c->cf[cur].base = c->cf[cur].par = -1;
      if (fo) c->cf[cur].formula = str_dup(fo);
    } else if (!strcmp(x.name, "sharedItems")) insh = !x.pending_end;
    else if (!strcmp(x.name, "groupItems")) ingi = !x.pending_end;
    else if (!strcmp(x.name, "fieldGroup") && cur >= 0) {
      const char *ba = xml_attr(&x, "base"), *pa = xml_attr(&x, "par");
      c->cf[cur].base = ba ? atoi(ba) : cur;
      if (pa) c->cf[cur].par = atoi(pa);
    } else if (!strcmp(x.name, "rangePr") && cur >= 0) {
      const char *g = xml_attr(&x, "groupBy"), *s1 = xml_attr(&x, "startNum"), *e1 = xml_attr(&x, "endNum"), *gi = xml_attr(&x, "groupInterval");
      int lv = g ? level_of(g) : PG_RANGE;
      if (lv >= PG_YEARS && lv <= PG_DAYS) { c->cf[cur].gk = GK_DATE; c->cf[cur].level = (u8)lv; }
      else if (lv == PG_RANGE) {
        c->cf[cur].gk = GK_RANGE; c->cf[cur].level = PG_RANGE;
        c->cf[cur].gstart = s1 ? strtod(s1, NULL) : 0; c->cf[cur].gend = e1 ? strtod(e1, NULL) : 0; c->cf[cur].gstep = gi ? strtod(gi, NULL) : 1;
      }
    } else if (!strcmp(x.name, "discretePr") && cur >= 0) c->cf[cur].gk = GK_DISCRETE;
    else if (!strcmp(x.name, "x") && cur >= 0 && c->cf[cur].gk == GK_DISCRETE && !ingi) {
      const char *v = xml_attr(&x, "v");
      int *nd = (int *)realloc(c->cf[cur].disc, (size_t)(c->cf[cur].ndisc + 1) * sizeof(int));
      if (nd) { c->cf[cur].disc = nd; nd[c->cf[cur].ndisc++] = v ? atoi(v) : 0; }
    } else if ((insh || ingi) && cur >= 0 && (!strcmp(x.name, "s") || !strcmp(x.name, "n") || !strcmp(x.name, "b") || !strcmp(x.name, "e") || !strcmp(x.name, "m") || !strcmp(x.name, "d"))) {
      const char *v = xml_attr(&x, "v");
      char lab[256];
      Value val = val_empty();
      if (x.name[0] == 's') val = val_str(v ? v : "");
      else if (x.name[0] == 'n') val = val_num(v ? strtod(v, NULL) : 0);
      else if (x.name[0] == 'b') val = val_bool(v && (!strcmp(v, "1") || !strcmp(v, "true")));
      else if (x.name[0] == 'e') val = val_err(v ? err_parse(v, NULL) : E_NA);
      else if (x.name[0] == 'd' && v) { int y = 0, m = 0, dd = 0; sscanf(v, "%d-%d-%d", &y, &m, &dd); val = val_num(date_serial(y, m, dd)); }
      pivot_item_label(b, &val, lab, sizeof lab);
      val_free(&val);
      if (ingi) strs_push(&c->cf[cur].gi, &c->cf[cur].ngi, lab);
      else strs_push(&c->items[cur], &c->nitems[cur], lab);
    } else if (!strcmp(x.name, "pivotCacheDefinition")) {
      const char *id = xml_attr(&x, "pivotCacheId");
      if (id) c->cacheid = atoi(id);
    }
  }
  xml_free(&x);
  free(d);
  if (!c->source[0] && ref[0]) {
    /* Sheet!$A$1:$D$7, the name quoted when it must be */
    const char *p;
    int q = !sheet[0] || isdigit((u8)sheet[0]);
    char a[64], *dst;
    Rect r;
    for (p = sheet; *p; p++) if (!isalnum((u8)*p) && *p != '_' && (u8)*p < 0x80) q = 1;
    if (!a1_rect(ref, &r)) { cache_free(c); return 0; }
    ref_text(r.r1, r.c1, 1, 1, a);
    dst = c->source;
    if (sheet[0]) {
      if (q) { size_t o = 0; dst[o++] = '\''; for (p = sheet; *p && o < 200; p++) { if (*p == '\'') dst[o++] = '\''; dst[o++] = *p; } dst[o++] = '\''; dst[o] = 0; }
      else snprintf(dst, 200, "%s", sheet);
      strcat(dst, "!");
    }
    strcat(dst, a);
    if (r.r2 != r.r1 || r.c2 != r.c1) { ref_text(r.r2, r.c2, 1, 1, a); strcat(dst, ":"); strcat(dst, a); }
  }
  return c->source[0] != 0;
}
/* the labels <item x> counts in cache field k */
static char **cache_labels(Cache *c, int k, int *n)
{
  if (c->cf[k].gk != GK_NONE && c->cf[k].ngi) { *n = c->cf[k].ngi; return c->cf[k].gi; }
  *n = c->nitems[k]; return c->items[k];
}
static void add_hidden(PivotField *f, const char *lab)
{
  char **nh = (char **)realloc(f->hidden, (size_t)(f->nhidden + 1) * sizeof(char *));
  if (!nh) return;
  f->hidden = nh;
  f->hidden[f->nhidden++] = str_dup(lab);
}
/* cache field k as a field of area: the column it groups and how */
static int add_cache_field(LPivot *p, Cache *c, int area, int k)
{
  CacheF *f = &c->cf[k];
  int base = f->base >= 0 && f->base < c->nf ? f->base : k, ok;
  PivotField *pf;
  int n;
  if (f->gk == GK_DATE) ok = pivot_def_add_level(p, area, c->names[base], f->level);
  else ok = area == PA_FILTERS ? pivot_def_add(p, area, c->names[base], 0) : pivot_def_add_level(p, area, c->names[base], PG_NONE);
  if (!ok) return 0;
  n = area == PA_ROWS ? p->nrows : area == PA_COLS ? p->ncols : p->nfilters;
  pf = &(area == PA_ROWS ? p->rows : area == PA_COLS ? p->cols : p->filters)[n - 1];
  if (f->gk == GK_RANGE) { pf->group = PG_RANGE; pf->gstart = f->gstart; pf->gend = f->gend; pf->gstep = f->gstep > 0 ? f->gstep : 1; }
  else if (f->gk == GK_DISCRETE && base != k) {
    /* groups by hand: a group item holding base items other than itself */
    int g, j;
    for (g = 0; g < f->ngi; g++) {
      char *its[1];
      for (j = 0; j < f->ndisc && j < c->nitems[base]; j++)
        if (f->disc[j] == g && !str_ieq(c->items[base][j], f->gi[g])) { its[0] = c->items[base][j]; pivot_group_items(p, area, n - 1, f->gi[g], its, 1); }
      for (j = 0; j < f->ndisc && j < c->nitems[base]; j++)
        if (f->disc[j] == g && str_ieq(c->items[base][j], f->gi[g])) {
          /* (an item named as its group, alongside others) */
          int i2, has = 0;
          for (i2 = 0; i2 < pf->nmg; i2++) if (str_ieq(pf->mg[i2].name, f->gi[g])) has = 1;
          if (has) { its[0] = c->items[base][j]; pivot_group_items(p, area, n - 1, f->gi[g], its, 1); }
        }
    }
  }
  return 1;
}
static void read_pivot(Book *b, ZipReader *z, const char *path, int sheet)
{
  size_t len;
  u8 *d;
  Xml x;
  char cpath[300];
  Cache c;
  LPivot p, *np;
  int fi = -1, area = 0, i, *ax = NULL, subdef = 1, subtopdef = 1, blank = 0, lastval = -1;
  char ***hid = NULL, ***col = NULL;   /* (per cache field: hidden and collapsed item labels, given to the fields after) */
  int *nhid = NULL, *ncol = NULL, *sortv = NULL;
  int *rowk = NULL, *colk = NULL, *pagek = NULL;   /* each area field's cache field */
  Sheet *s = b->sh[sheet];
  if (!rel_target(z, path, "pivotCacheDefinition", cpath, sizeof cpath) || !read_cache(b, z, cpath, &c)) return;
  if (!(d = part(z, path, &len))) { cache_free(&c); return; }
  memset(&p, 0, sizeof p);
  p.out.r1 = -1;
  p.layout = PL_COMPACT; p.subtotals = 1; p.subtop = 1; p.grandrows = 1; p.grandcols = 1; p.rowheaders = 1; p.colheaders = 1;
  ax = (int *)calloc((size_t)c.nf + 1, sizeof(int));
  hid = (char ***)calloc((size_t)c.nf + 1, sizeof(char **));
  col = (char ***)calloc((size_t)c.nf + 1, sizeof(char **));
  nhid = (int *)calloc((size_t)c.nf + 1, sizeof(int));
  ncol = (int *)calloc((size_t)c.nf + 1, sizeof(int));
  sortv = (int *)calloc((size_t)c.nf + 1, sizeof(int));
  rowk = (int *)calloc(17, sizeof(int)); colk = (int *)calloc(17, sizeof(int)); pagek = (int *)calloc(17, sizeof(int));
  if (!ax || !hid || !col || !nhid || !ncol || !sortv || !rowk || !colk || !pagek) goto out;
  snprintf(p.source, sizeof p.source, "%s", c.source);
  /* calculated fields */
  for (i = 0; i < c.nf; i++)
    if (c.cf[i].formula && c.cf[i].formula[0]) {
      PivotCalc *nc = (PivotCalc *)realloc(p.calcs, (size_t)(p.ncalcs + 1) * sizeof(PivotCalc));
      if (!nc) continue;
      p.calcs = nc;
      nc[p.ncalcs].name = str_dup(c.names[i]);
      nc[p.ncalcs].formula = str_dup(c.cf[i].formula[0] == '=' ? c.cf[i].formula + 1 : c.cf[i].formula);
      p.ncalcs++;
    }
  xml_init(&x, (const char *)d, len);
  while (xml_next(&x) != XT_EOF) {
    if (x.type != XT_START) continue;
    if (!strcmp(x.name, "pivotTableDefinition")) {
      const char *nm = xml_attr(&x, "name");
      snprintf(p.name, sizeof p.name, "%s", nm ? nm : "PivotTable1");
      if (!attr1(&x, "compact", 1)) p.layout = attr1(&x, "outline", 0) ? PL_OUTLINE : PL_TABULAR;
      p.grandcols = (u8)attr1(&x, "rowGrandTotals", 1);
      p.grandrows = (u8)attr1(&x, "colGrandTotals", 1);
    } else if (!strcmp(x.name, "location")) {
      const char *ref = xml_attr(&x, "ref");
      Rect r;
      if (ref && a1_rect(ref, &r)) { p.r = r.r1; p.c = r.c1; }
    } else if (!strcmp(x.name, "pivotField")) {
      const char *axis = xml_attr(&x, "axis"), *st = xml_attr(&x, "sortType");
      fi++;
      if (fi >= c.nf) continue;
      ax[fi] = !axis ? 0 : !strcmp(axis, "axisRow") ? 1 : !strcmp(axis, "axisCol") ? 2 : !strcmp(axis, "axisPage") ? 3 : 0;
      sortv[fi] = st && !strcmp(st, "descending") ? PO_DESC : PO_ASC;
      if (ax[fi] && ax[fi] != 3) {
        if (!attr1(&x, "defaultSubtotal", 1)) subdef = 0;
        if (!attr1(&x, "subtotalTop", 1)) subtopdef = 0;
        if (attr1(&x, "insertBlankRow", 0)) blank = 1;
      }
    } else if (!strcmp(x.name, "item") && fi >= 0 && fi < c.nf) {
      const char *xi = xml_attr(&x, "x");
      int k = xi ? atoi(xi) : -1, nl;
      char **labs = cache_labels(&c, fi, &nl);
      if (k >= 0 && k < nl) {
        if (attr1(&x, "h", 0)) { char **nh = (char **)realloc(hid[fi], (size_t)(nhid[fi] + 1) * sizeof(char *)); if (nh) { hid[fi] = nh; hid[fi][nhid[fi]++] = labs[k]; } }
        if (xml_attr(&x, "sd") && !attr1(&x, "sd", 1)) { char **nh = (char **)realloc(col[fi], (size_t)(ncol[fi] + 1) * sizeof(char *)); if (nh) { col[fi] = nh; col[fi][ncol[fi]++] = labs[k]; } }
      }
    } else if (!strcmp(x.name, "rowFields")) area = 1;
    else if (!strcmp(x.name, "colFields")) area = 2;
    else if (!strcmp(x.name, "field") && (area == 1 || area == 2)) {
      const char *xi = xml_attr(&x, "x");
      int k = xi ? atoi(xi) : -1;
      if (k >= 0 && k < c.nf && !c.cf[k].formula) {
        int n0 = area == 1 ? p.nrows : p.ncols;
        if (n0 < 16 && add_cache_field(&p, &c, area == 1 ? PA_ROWS : PA_COLS, k)) (area == 1 ? rowk : colk)[n0] = k;
      }
    } else if (!strcmp(x.name, "pageField")) {
      const char *f = xml_attr(&x, "fld"), *it = xml_attr(&x, "item");
      int k = f ? atoi(f) : -1, n0 = p.nfilters;
      if (k >= 0 && k < c.nf && n0 < 16 && add_cache_field(&p, &c, PA_FILTERS, k)) {
        pagek[n0] = k;
        if (it) {
          /* one item chosen: the others hidden */
          int one = atoi(it), j, nl;
          char **labs = cache_labels(&c, k, &nl);
          PivotField *pf = &p.filters[p.nfilters - 1];
          for (j = 0; j < nl; j++) if (j != one) add_hidden(pf, labs[j]);
        }
      }
    } else if (!strcmp(x.name, "dataField")) {
      const char *f = xml_attr(&x, "fld"), *nm = xml_attr(&x, "name"), *sub = xml_attr(&x, "subtotal"), *sa = xml_attr(&x, "showDataAs");
      static const char *const subs[PF_N] = { "sum", "count", "average", "max", "min", "product", "countNums", "stdDev", "stdDevp", "var", "varp" };
      int k = f ? atoi(f) : -1, fn = PF_SUM;
      const char *ps = xml_attr(&x, "pivotShowAs");
      /* (x14:dataField, inside the last one's extLst: names have no prefixes here) */
      if (!f && ps && lastval >= 0) { if (!strncmp(ps, "rank", 4)) p.vals[lastval].showas = PS_RANK; continue; }
      lastval = -1;
      if (k < 0 || k >= c.nf) continue;
      if (sub) for (i = 0; i < PF_N; i++) if (!strcmp(sub, subs[i])) fn = i;
      if (pivot_def_add(&p, PA_VALUES, c.names[k], fn)) {
        PivotValue *v = &p.vals[p.nvals - 1];
        char def[300];
        lastval = p.nvals - 1;
        if (sa) v->showas = (u8)(!strcmp(sa, "percentOfTotal") ? PS_PCT_GRAND : !strcmp(sa, "percentOfCol") ? PS_PCT_COL : !strcmp(sa, "percentOfRow") ? PS_PCT_ROW
                                 : !strcmp(sa, "runTotal") ? PS_RUNTOTAL : PS_NORMAL);
        pivot_caption(v, def, sizeof def);
        if (nm && strcmp(nm, def)) v->caption = str_dup(nm);
      }
    } else if (!strcmp(x.name, "pivotTableStyleInfo")) {
      const char *nm = xml_attr(&x, "name");
      snprintf(p.style, sizeof p.style, "%s", nm ? nm : "");
      p.bandrows = (u8)attr1(&x, "showRowStripes", 0);
      p.bandcols = (u8)attr1(&x, "showColStripes", 0);
      p.rowheaders = (u8)attr1(&x, "showRowHeaders", 1);
      p.colheaders = (u8)attr1(&x, "showColHeaders", 1);
    }
  }
  xml_free(&x);
  p.subtotals = (u8)subdef; p.subtop = (u8)subtopdef; p.blankrows = (u8)blank;
  /* hidden items, collapsed ones and sorting to the fields they came from */
  {
    int a, j;
    for (a = 0; a < 3; a++) {
      PivotField *l = a == 0 ? p.rows : a == 1 ? p.cols : p.filters;
      int n = a == 0 ? p.nrows : a == 1 ? p.ncols : p.nfilters, *kk = a == 0 ? rowk : a == 1 ? colk : pagek;
      for (i = 0; i < n && i < 16; i++) {
        int k = kk[i];
        if (a < 2) l[i].sort = (u8)sortv[k];
        for (j = 0; j < ncol[k] && a < 2; j++) pivot_set_collapsed(&l[i], col[k][j], 1);
        if (l[i].nhidden) continue;     /* (a page field's one item already says) */
        for (j = 0; j < nhid[k]; j++) add_hidden(&l[i], hid[k][j]);
      }
    }
    /* items hidden in a field on no axis: a slicer's filter */
    for (i = 0; i < c.nf; i++)
      if (!ax[i] && nhid[i] && !c.cf[i].formula && c.cf[i].gk == GK_NONE) {
        PivotField *nf = (PivotField *)realloc(p.sfilters, (size_t)(p.nsfilters + 1) * sizeof(PivotField));
        if (!nf) continue;
        p.sfilters = nf;
        memset(&nf[p.nsfilters], 0, sizeof(PivotField));
        nf[p.nsfilters].name = str_dup(c.names[i]);
        for (j = 0; j < nhid[i]; j++) add_hidden(&nf[p.nsfilters], hid[i][j]);
        p.nsfilters++;
      }
  }
  if ((np = (LPivot *)realloc(s->pivots, (size_t)(s->npivots + 1) * sizeof(LPivot)))) {
    s->pivots = np;
    s->pivots[s->npivots++] = p;
    memset(&p, 0, sizeof p);
  }
out:
  pivot_free1(&p);
  for (i = 0; hid && i < c.nf; i++) free(hid[i]);
  for (i = 0; col && i < c.nf; i++) free(col[i]);
  free(hid); free(col); free(nhid); free(ncol); free(ax); free(sortv); free(rowk); free(colk); free(pagek);
  free(d);
  cache_free(&c);
}
/* ------------------------------------------------------------------ reading: slicers */
/* slicers read from a sheet's slicers part wait here for their frame in the drawing */
typedef struct { int sheet; Slicer *s; } PendSl;
static PendSl *pend; static int npend;
/* the relationships' targets of type in the workbook */
static int book_rels(ZipReader *z, const char *type, char ***out)
{
  size_t len;
  u8 *d = part(z, "xl/_rels/workbook.xml.rels", &len);
  Xml x;
  int n = 0;
  *out = NULL;
  if (!d) return 0;
  xml_init(&x, (const char *)d, len);
  while (xml_next(&x) != XT_EOF) {
    const char *ty, *t, *ls;
    char full[300];
    if (x.type != XT_START || strcmp(x.name, "Relationship")) continue;
    ty = xml_attr(&x, "Type"); t = xml_attr(&x, "Target");
    if (!ty || !t) continue;
    ls = strrchr(ty, '/');
    if (strcmp(ls ? ls + 1 : ty, type)) continue;
    if (t[0] == '/') snprintf(full, sizeof full, "%s", t + 1); else snprintf(full, sizeof full, "xl/%s", t);
    tidy(full);
    strs_push(out, &n, full);
  }
  xml_free(&x);
  free(d);
  return n;
}
/* a pivot cache's field's item labels, by the cache's x14 id */
static int cache_items_by_id(Book *b, ZipReader *z, int id, const char *field, char ***out)
{
  char **parts;
  int n = book_rels(z, "pivotCacheDefinition", &parts), i, k, m = 0;
  *out = NULL;
  for (i = 0; i < n && !*out; i++) {
    Cache c;
    if (!read_cache(b, z, parts[i], &c)) continue;
    if (c.cacheid == id || (!c.cacheid && n == 1))
      for (k = 0; k < c.nf; k++) if (str_ieq(c.names[k], field)) { int j; for (j = 0; j < c.nitems[k]; j++) strs_push(out, &m, c.items[k][j]); if (!*out) *out = (char **)calloc(1, sizeof(char *)); break; }
    cache_free(&c);
  }
  pivot_strings_free(parts, n);
  return m;
}
/* the slicer cache named name: its source and selection into s */
static void read_slicer_cache(Book *b, ZipReader *z, const char *name, Slicer *s)
{
  char **parts;
  int n = book_rels(z, "slicerCache", &parts), i, found = 0;
  for (i = 0; i < n && !found; i++) {
    size_t len;
    u8 *d = part(z, parts[i], &len);
    Xml x;
    int cacheid = -1, *xs = NULL, nx = 0, nsel = 0, tableid = -1, column = -1, ok = 0;
    char **items = NULL;
    if (!d) continue;
    xml_init(&x, (const char *)d, len);
    while (xml_next(&x) != XT_EOF) {
      if (x.type != XT_START) continue;
      if (!strcmp(x.name, "slicerCacheDefinition")) {
        const char *nm = xml_attr(&x, "name"), *src = xml_attr(&x, "sourceName");
        if (!nm || !str_ieq(nm, name)) break;
        ok = 1;
        snprintf(s->field, sizeof s->field, "%s", src ? src : "");
      } else if (!strcmp(x.name, "pivotTable")) {
        const char *nm = xml_attr(&x, "name");
        s->onpivot = 1;
        if (nm) snprintf(s->source, sizeof s->source, "%s", nm);
      } else if (!strcmp(x.name, "tabular")) {
        const char *id = xml_attr(&x, "pivotCacheId"), *so = xml_attr(&x, "sortOrder");
        cacheid = id ? atoi(id) : 0;
        if (so && !strcmp(so, "descending")) s->sort = SS_DESC;
      } else if (!strcmp(x.name, "i")) {
        const char *xi = xml_attr(&x, "x");
        int *nn = (int *)realloc(xs, (size_t)(nx + 1) * sizeof(int));
        if (!nn) continue;
        xs = nn;
        xs[nx++] = attr1(&x, "s", 0) ? (xi ? atoi(xi) : 0) : -1;
        if (attr1(&x, "s", 0)) nsel++;
      } else if (!strcmp(x.name, "tableSlicerCache")) {
        const char *t = xml_attr(&x, "tableId"), *c = xml_attr(&x, "column"), *so = xml_attr(&x, "sortOrder");
        tableid = t ? atoi(t) : -1; column = c ? atoi(c) : -1;
        if (so && !strcmp(so, "descending")) s->sort = SS_DESC;
      }
    }
    xml_free(&x);
    free(d);
    if (ok) {
      found = 1;
      if (s->onpivot && cacheid >= 0 && nsel && nsel < nx) {
        int ni = cache_items_by_id(b, z, cacheid, s->field, &items), k;
        s->all = 0;
        for (k = 0; k < nx; k++) if (xs[k] >= 0 && xs[k] < ni) strs_push(&s->sel, &s->nsel, items[xs[k]]);
        if (!s->nsel) s->all = 1;
        pivot_strings_free(items, ni);
      } else if (!s->onpivot && tableid >= 0) {
        /* (the table may be on a sheet not read yet: tablexml_loaded finds it) */
        snprintf(s->field, sizeof s->field, "#%d,%d", tableid, column);
        s->source[0] = 0;
      }
    }
    free(xs);
  }
  pivot_strings_free(parts, n);
}
static void read_slicers(Book *b, ZipReader *z, const char *path, int sheet)
{
  size_t len;
  u8 *d = part(z, path, &len);
  Xml x;
  if (!d) return;
  xml_init(&x, (const char *)d, len);
  while (xml_next(&x) != XT_EOF) {
    const char *nm, *cache, *cap, *cc, *sty;
    Slicer *s;
    PendSl *np;
    if (x.type != XT_START || strcmp(x.name, "slicer")) continue;
    nm = xml_attr(&x, "name"); cache = xml_attr(&x, "cache"); cap = xml_attr(&x, "caption"); cc = xml_attr(&x, "columnCount"); sty = xml_attr(&x, "style");
    if (!nm || !cache || !(s = slicer_new())) continue;
    snprintf(s->name, sizeof s->name, "%s", nm);
    if (cap) snprintf(s->caption, sizeof s->caption, "%s", cap);
    if (cc) s->cols = (u8)(atoi(cc) < 1 ? 1 : atoi(cc) > 20 ? 20 : atoi(cc));
    if (sty) snprintf(s->style, sizeof s->style, "%s", sty);
    s->header = (u8)attr1(&x, "showCaption", 1);
    read_slicer_cache(b, z, cache, s);
    if (!s->field[0] || !(np = (PendSl *)realloc(pend, (size_t)(npend + 1) * sizeof(PendSl)))) { slicer_free(s); continue; }
    pend = np;
    pend[npend].sheet = sheet; pend[npend].s = s;
    npend++;
  }
  xml_free(&x);
  free(d);
}
struct Slicer *tablexml_slicer_take(int sheet, const char *name)
{
  int i;
  for (i = 0; i < npend; i++)
    if (pend[i].sheet == sheet && str_ieq(pend[i].s->name, name)) {
      Slicer *s = pend[i].s;
      pend[i] = pend[--npend];
      return s;
    }
  return NULL;
}
/* after loading: slicers without a frame placed, table slicers' tables found, their filters made */
static void slicers_loaded(Book *b)
{
  int s, i;
  for (i = 0; i < npend; i++) {
    DObj *o;
    if (pend[i].sheet < b->nsh && (o = drw_new(b->sh[pend[i].sheet], OB_SLICER))) { o->slicer = pend[i].s; snprintf(o->name, sizeof o->name, "%s", o->slicer->name); o->anchor = AN_ONECELL; }
    else slicer_free(pend[i].s);
  }
  free(pend); pend = NULL; npend = 0;
  for (s = 0; s < b->nsh; s++)
    for (i = 0; i < drw_count(b->sh[s]); i++) {
      DObj *o = drw_obj(b->sh[s], i);
      Slicer *sl = o->kind == OB_SLICER ? o->slicer : NULL;
      int id, col, q, t;
      if (!sl || sl->onpivot || sl->source[0] || sscanf(sl->field, "#%d,%d", &id, &col) != 2) continue;
      for (q = 0; q < b->nsh; q++)
        for (t = 0; t < b->sh[q]->ntables; t++) {
          LTable *tb = &b->sh[q]->tables[t];
          const FilterCol *fc;
          if ((int)tb->id != id || col < 1 || col > tb->ncols) continue;
          snprintf(sl->source, sizeof sl->source, "%s", tb->name);
          snprintf(sl->field, sizeof sl->field, "%s", tb->cols[col - 1].name ? tb->cols[col - 1].name : "");
          /* the selection is the table's filter on the column */
          if ((fc = sheet_filter_col(b->sh[q], tb->r.c1 + col - 1))) {
            int k;
            sl->all = 0;
            for (k = 0; k < fc->n; k++) strs_push(&sl->sel, &sl->nsel, fc->vals[k]);
            if (fc->blanks) strs_push(&sl->sel, &sl->nsel, "(blank)");
          }
        }
    }
  slicers_apply_all(b);
}

void tablexml_read_rel(Book *b, ZipReader *z, const char *type, const char *target, int sheet)
{
  if (!strcmp(type, "table")) read_table(b, z, target, sheet);
  else if (!strcmp(type, "pivotTable")) read_pivot(b, z, target, sheet);
  else if (!strcmp(type, "slicer")) read_slicers(b, z, target, sheet);
}
void tablexml_loaded(Book *b)
{
  tables_tidy_loaded(b);
  book_pivots_refresh_all(b);
  slicers_loaded(b);
}

