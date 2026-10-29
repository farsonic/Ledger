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
}
void tablexml_workbook(Book *b, Buf *o)
{
  int i, n = total_pivots(b);
  if (!n) return;
  buf_adds(o, "<pivotCaches>");
  for (i = 1; i <= n; i++) buf_printf(o, "<pivotCache cacheId=\"%d\" r:id=\"rIdC%d\"/>", i, i);
  buf_adds(o, "</pivotCaches>");
}
void tablexml_workbook_rels(Book *b, Buf *o)
{
  int i, n = total_pivots(b);
  for (i = 1; i <= n; i++)
    buf_printf(o, "<Relationship Id=\"rIdC%d\" Type=\"" NS_REL "/pivotCacheDefinition\" Target=\"pivotCache/pivotCacheDefinition%d.xml\"/>", i, i);
}
int tablexml_has_rels(Book *b, int si) { return b->sh[si]->ntables || b->sh[si]->npivots; }
void tablexml_sheet_rels(Book *b, int si, Buf *o)
{
  Sheet *s = b->sh[si];
  int i, t0 = first_table(b, si), p0 = first_pivot(b, si);
  for (i = 0; i < s->ntables; i++)
    buf_printf(o, "<Relationship Id=\"rIdT%d\" Type=\"" NS_REL "/table\" Target=\"../tables/table%d.xml\"/>", i + 1, t0 + i);
  for (i = 0; i < s->npivots; i++)
    buf_printf(o, "<Relationship Id=\"rIdP%d\" Type=\"" NS_REL "/pivotTable\" Target=\"../pivotTables/pivotTable%d.xml\"/>", i + 1, p0 + i);
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
/* a cache field: its source column's shared items (when itemized) and what kinds of value it has */
typedef struct {
  char *name;
  int itemized;
  char **lab; Value *val; int n, cap;
  int blank, text, number, nonint, boolean, error;
  double mn, mx;
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
/* a sheet name as a worksheetSource writes it (31 characters at most) */
static void write_pivot(Book *b, int si, LPivot *p, int id, ZipWriter *w)
{
  int ss, nf, k, i, nrec = 0;
  Rect src;
  char **names = NULL, path[96], ref[40];
  CField *cf;
  Buf o, rec;
  i32 r;
  LTable *tb;
  if (!pivot_source(b, p, &ss, &src)) return;
  nf = pivot_fields(b, p, &names);
  if (!nf || !(cf = (CField *)calloc((size_t)nf, sizeof(CField)))) { pivot_strings_free(names, nf); return; }
  for (k = 0; k < nf; k++) {
    cf[k].name = names[k];
    cf[k].itemized = in_area(p->rows, p->nrows, names[k]) >= 0 || in_area(p->cols, p->ncols, names[k]) >= 0 || in_area(p->filters, p->nfilters, names[k]) >= 0;
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
      } else item_xml(&rec, v);
    }
    buf_adds(&rec, "</r>");
    nrec++;
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
  buf_printf(&o, "</cacheSource><cacheFields count=\"%d\">", nf);
  for (k = 0; k < nf; k++) {
    CField *f = &cf[k];
    int strings = f->text || f->boolean || f->error;
    buf_adds(&o, "<cacheField name=\""); esc(&o, f->name); buf_adds(&o, "\" numFmtId=\"0\"><sharedItems");
    if (!strings && !f->blank) buf_adds(&o, " containsSemiMixedTypes=\"0\"");
    if (!strings) buf_adds(&o, " containsString=\"0\"");
    if (f->blank) buf_adds(&o, " containsBlank=\"1\"");
    if (strings && f->number) buf_adds(&o, " containsMixedTypes=\"1\"");
    if (f->number) {
      buf_adds(&o, " containsNumber=\"1\"");
      if (!f->nonint) buf_adds(&o, " containsInteger=\"1\"");
      buf_adds(&o, " minValue=\""); num(&o, f->mn); buf_adds(&o, "\" maxValue=\""); num(&o, f->mx); buf_adds(&o, "\"");
    }
    if (f->itemized) {
      buf_printf(&o, " count=\"%d\">", f->n);
      for (i = 0; i < f->n; i++) item_xml(&o, &f->val[i]);
      buf_adds(&o, "</sharedItems>");
    } else buf_adds(&o, "/>");
    buf_adds(&o, "</cacheField>");
  }
  buf_adds(&o, "</cacheFields></pivotCacheDefinition>");
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
    int R = 0, C = 0, V = 0, compact = p->layout == PL_COMPACT, fdr, fdc, *rowf, *colf, *pagef, np = 0;
    Rect loc;
    rowf = (int *)malloc(((size_t)p->nrows + 1) * sizeof(int));
    colf = (int *)malloc(((size_t)p->ncols + 1) * sizeof(int));
    pagef = (int *)malloc(((size_t)p->nfilters + 1) * sizeof(int));
    if (!rowf || !colf || !pagef) { free(rowf); free(colf); free(pagef); goto done; }
    for (i = 0; i < p->nrows; i++) { int x = -1; for (k = 0; k < nf; k++) if (str_ieq(names[k], p->rows[i].name)) x = k; if (x >= 0) rowf[R++] = x; }
    for (i = 0; i < p->ncols; i++) { int x = -1; for (k = 0; k < nf; k++) if (str_ieq(names[k], p->cols[i].name)) x = k; if (x >= 0) colf[C++] = x; }
    for (i = 0; i < p->nfilters; i++) { int x = -1; for (k = 0; k < nf; k++) if (str_ieq(names[k], p->filters[i].name)) x = k; if (x >= 0) pagef[np++] = x; }
    for (i = 0; i < p->nvals; i++) for (k = 0; k < nf; k++) if (str_ieq(names[k], p->vals[i].name)) { V++; break; }
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
    buf_printf(&o, "/><pivotFields count=\"%d\">", nf);
    for (k = 0; k < nf; k++) {
      CField *f = &cf[k];
      const PivotField *af = NULL;
      const char *axis = NULL;
      int isdata = 0, *ord = NULL;
      for (i = 0; i < p->nrows; i++) if (str_ieq(p->rows[i].name, f->name)) { axis = "axisRow"; af = &p->rows[i]; }
      for (i = 0; i < p->ncols; i++) if (str_ieq(p->cols[i].name, f->name)) { axis = "axisCol"; af = &p->cols[i]; }
      for (i = 0; i < p->nfilters; i++) if (str_ieq(p->filters[i].name, f->name)) { axis = "axisPage"; af = &p->filters[i]; }
      for (i = 0; i < p->nvals; i++) if (str_ieq(p->vals[i].name, f->name)) isdata = 1;
      buf_adds(&o, "<pivotField");
      if (axis) buf_printf(&o, " axis=\"%s\"", axis);
      if (isdata) buf_adds(&o, " dataField=\"1\"");
      if (p->layout == PL_OUTLINE) buf_adds(&o, " compact=\"0\"");
      else if (p->layout == PL_TABULAR) buf_adds(&o, " compact=\"0\" outline=\"0\"");
      if (axis && af && af->nhidden && !strcmp(axis, "axisPage")) buf_adds(&o, " multipleItemSelectionAllowed=\"1\"");
      buf_adds(&o, " showAll=\"0\"");
      if (af && strcmp(axis, "axisPage")) buf_adds(&o, af->sort == PO_DESC ? " sortType=\"descending\"" : af->sort == PO_ASC ? " sortType=\"ascending\"" : "");
      if (!p->subtotals) buf_adds(&o, " defaultSubtotal=\"0\"");
      if (p->subtotals && !p->subtop) buf_adds(&o, " subtotalTop=\"0\"");
      if (p->blankrows && axis && !strcmp(axis, "axisRow")) buf_adds(&o, " insertBlankRow=\"1\"");
      if (!axis) { buf_adds(&o, "/>"); continue; }
      /* the items in report order, those filtered out hidden */
      ord = f->n ? (int *)malloc((size_t)f->n * sizeof(int)) : NULL;
      buf_printf(&o, "><items count=\"%d\">", f->n + (p->subtotals ? 1 : 0));
      if (ord) {
        char **items;
        int ni = pivot_items(b, p, f->name, &items), j, shown = 0;
        for (i = 0; i < f->n; i++) ord[i] = i;
        /* pivot_items is in report order: follow it */
        {
          int m = 0;
          for (j = 0; j < ni; j++) { int x = cf_index(f, items[j]); if (x >= 0) ord[m++] = x; }
          for (i = 0; i < f->n && m < f->n; i++) { int seen = 0; for (j = 0; j < m; j++) if (ord[j] == i) seen = 1; if (!seen) ord[m++] = i; }
        }
        for (i = 0; i < f->n; i++) if (!(af && hidden_of(af, f->lab[ord[i]]))) shown++;
        for (i = 0; i < f->n; i++) {
          buf_printf(&o, "<item x=\"%d\"", ord[i]);
          if (shown && af && hidden_of(af, f->lab[ord[i]])) buf_adds(&o, " h=\"1\"");
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
        int j, shown = 0, last = -1;
        for (j = 0; j < f->n; j++) if (!hidden_of(&p->filters[i], f->lab[j])) { shown++; last = j; }
        buf_printf(&o, "<pageField fld=\"%d\"", pagef[i]);
        if (p->filters[i].nhidden && shown == 1) buf_printf(&o, " item=\"%d\"", last);
        buf_adds(&o, " hier=\"-1\"/>");
      }
      buf_adds(&o, "</pageFields>");
    }
    if (V) {
      static const char *const sub[PF_N] = { "sum", "count", "average", "max", "min", "product", "countNums", "stdDev", "stdDevp", "var", "varp" };
      static const char *const showas[PS_N] = { "", "percentOfTotal", "percentOfCol", "percentOfRow" };
      buf_printf(&o, "<dataFields count=\"%d\">", V);
      for (i = 0; i < p->nvals; i++) {
        int x = -1;
        char cap[300];
        for (k = 0; k < nf; k++) if (str_ieq(names[k], p->vals[i].name)) x = k;
        if (x < 0) continue;
        pivot_caption(&p->vals[i], cap, sizeof cap);
        buf_adds(&o, "<dataField name=\""); esc(&o, cap); buf_printf(&o, "\" fld=\"%d\"", x);
        if (p->vals[i].func != PF_SUM && p->vals[i].func < PF_N) buf_printf(&o, " subtotal=\"%s\"", sub[p->vals[i].func]);
        if (p->vals[i].showas > PS_NORMAL && p->vals[i].showas < PS_N) buf_printf(&o, " showDataAs=\"%s\"", showas[p->vals[i].showas]);
        buf_adds(&o, " baseField=\"0\" baseItem=\"0\"");
        if (p->vals[i].showas != PS_NORMAL) buf_adds(&o, " numFmtId=\"10\"");
        buf_adds(&o, "/>");
      }
      buf_adds(&o, "</dataFields>");
    }
    buf_adds(&o, "<pivotTableStyleInfo");
    if (p->style[0]) { buf_adds(&o, " name=\""); esc(&o, p->style); buf_adds(&o, "\""); }
    buf_printf(&o, " showRowHeaders=\"%d\" showColHeaders=\"%d\" showRowStripes=\"%d\" showColStripes=\"%d\" showLastColumn=\"1\"/></pivotTableDefinition>",
               p->rowheaders, p->colheaders, p->bandrows, p->bandcols);
    free(rowf); free(colf); free(pagef);
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
  for (k = 0; k < nf; k++) {
    for (i = 0; i < cf[k].n; i++) { free(cf[k].lab[i]); val_free(&cf[k].val[i]); }
    free(cf[k].lab); free(cf[k].val);
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
/* a pivot cache: the source and its fields' names and shared items' labels */
typedef struct { char source[256]; char **names; int nf; char ***items; int *nitems; } Cache;
static void cache_free(Cache *c)
{
  int k;
  for (k = 0; k < c->nf; k++) { pivot_strings_free(c->items[k], c->nitems[k]); free(c->names[k]); }
  free(c->names); free(c->items); free(c->nitems);
}
static int read_cache(Book *b, ZipReader *z, const char *path, Cache *c)
{
  size_t len;
  u8 *d = part(z, path, &len);
  Xml x;
  int cur = -1, insh = 0;
  char sheet[64] = "", ref[64] = "";
  memset(c, 0, sizeof *c);
  if (!d) return 0;
  xml_init(&x, (const char *)d, len);
  while (xml_next(&x) != XT_EOF) {
    if (x.type == XT_END && !strcmp(x.name, "sharedItems")) { insh = 0; continue; }
    if (x.type != XT_START) continue;
    if (!strcmp(x.name, "worksheetSource")) {
      const char *nm = xml_attr(&x, "name"), *r = xml_attr(&x, "ref"), *sh = xml_attr(&x, "sheet");
      if (nm) snprintf(c->source, sizeof c->source, "%s", nm);
      if (r) snprintf(ref, sizeof ref, "%s", r);
      if (sh) snprintf(sheet, sizeof sheet, "%s", sh);
    } else if (!strcmp(x.name, "cacheField")) {
      const char *nm = xml_attr(&x, "name");
      char **nn = (char **)realloc(c->names, (size_t)(c->nf + 1) * sizeof(char *));
      char ***ni;
      int *nc;
      if (!nn) continue;
      c->names = nn;
      if (!(ni = (char ***)realloc(c->items, (size_t)(c->nf + 1) * sizeof(char **)))) continue;
      c->items = ni;
      if (!(nc = (int *)realloc(c->nitems, (size_t)(c->nf + 1) * sizeof(int)))) continue;
      c->nitems = nc;
      cur = c->nf++;
      c->names[cur] = str_dup(nm ? nm : "");
      c->items[cur] = NULL; c->nitems[cur] = 0;
    } else if (!strcmp(x.name, "sharedItems")) insh = !x.pending_end;
    else if (insh && cur >= 0 && (!strcmp(x.name, "s") || !strcmp(x.name, "n") || !strcmp(x.name, "b") || !strcmp(x.name, "e") || !strcmp(x.name, "m") || !strcmp(x.name, "d"))) {
      const char *v = xml_attr(&x, "v");
      char lab[256];
      char **nl = (char **)realloc(c->items[cur], (size_t)(c->nitems[cur] + 1) * sizeof(char *));
      Value val = val_empty();
      if (!nl) continue;
      c->items[cur] = nl;
      if (x.name[0] == 's') val = val_str(v ? v : "");
      else if (x.name[0] == 'n') val = val_num(v ? strtod(v, NULL) : 0);
      else if (x.name[0] == 'b') val = val_bool(v && (!strcmp(v, "1") || !strcmp(v, "true")));
      else if (x.name[0] == 'e') val = val_err(v ? err_parse(v, NULL) : E_NA);
      else if (x.name[0] == 'd' && v) { int y = 0, m = 0, dd = 0; sscanf(v, "%d-%d-%d", &y, &m, &dd); val = val_num(date_serial(y, m, dd)); }
      pivot_item_label(b, &val, lab, sizeof lab);
      val_free(&val);
      nl[c->nitems[cur]++] = str_dup(lab);
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
static void add_hidden(PivotField *f, const char *lab)
{
  char **nh = (char **)realloc(f->hidden, (size_t)(f->nhidden + 1) * sizeof(char *));
  if (!nh) return;
  f->hidden = nh;
  f->hidden[f->nhidden++] = str_dup(lab);
}
static void read_pivot(Book *b, ZipReader *z, const char *path, int sheet)
{
  size_t len;
  u8 *d;
  Xml x;
  char cpath[300];
  Cache c;
  LPivot p, *np;
  int fi = -1, area = 0, i, *ax = NULL, nax = 0, subdef = 1, subtopdef = 1, blank = 0;
  char ***hid = NULL;      /* (per cache field: hidden item labels, gathered then given to the fields) */
  int *nhid = NULL, *sortv = NULL;
  Sheet *s = b->sh[sheet];
  if (!rel_target(z, path, "pivotCacheDefinition", cpath, sizeof cpath) || !read_cache(b, z, cpath, &c)) return;
  if (!(d = part(z, path, &len))) { cache_free(&c); return; }
  memset(&p, 0, sizeof p);
  p.out.r1 = -1;
  p.layout = PL_COMPACT; p.subtotals = 1; p.subtop = 1; p.grandrows = 1; p.grandcols = 1; p.rowheaders = 1; p.colheaders = 1;
  ax = (int *)calloc((size_t)c.nf + 1, sizeof(int));
  hid = (char ***)calloc((size_t)c.nf + 1, sizeof(char **));
  nhid = (int *)calloc((size_t)c.nf + 1, sizeof(int));
  sortv = (int *)calloc((size_t)c.nf + 1, sizeof(int));
  if (!ax || !hid || !nhid || !sortv) goto out;
  snprintf(p.source, sizeof p.source, "%s", c.source);
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
      sortv[fi] = st && !strcmp(st, "descending") ? PO_DESC : st && !strcmp(st, "ascending") ? PO_ASC : PO_ASC;
      if (ax[fi] && ax[fi] != 3) {
        if (!attr1(&x, "defaultSubtotal", 1)) subdef = 0;
        if (!attr1(&x, "subtotalTop", 1)) subtopdef = 0;
        if (attr1(&x, "insertBlankRow", 0)) blank = 1;
      }
    } else if (!strcmp(x.name, "item") && fi >= 0 && fi < c.nf) {
      const char *xi = xml_attr(&x, "x");
      int k = xi ? atoi(xi) : -1;
      if (attr1(&x, "h", 0) && k >= 0 && k < c.nitems[fi]) {
        char **nh = (char **)realloc(hid[fi], (size_t)(nhid[fi] + 1) * sizeof(char *));
        if (nh) { hid[fi] = nh; hid[fi][nhid[fi]++] = c.items[fi][k]; }
      }
    } else if (!strcmp(x.name, "rowFields")) area = 1;
    else if (!strcmp(x.name, "colFields")) area = 2;
    else if (!strcmp(x.name, "field") && (area == 1 || area == 2)) {
      const char *xi = xml_attr(&x, "x");
      int k = xi ? atoi(xi) : -1;
      if (k >= 0 && k < c.nf) {
        pivot_def_add(&p, area == 1 ? PA_ROWS : PA_COLS, c.names[k], 0);
        nax++;
      }
    } else if (!strcmp(x.name, "pageField")) {
      const char *f = xml_attr(&x, "fld"), *it = xml_attr(&x, "item");
      int k = f ? atoi(f) : -1;
      if (k >= 0 && k < c.nf && pivot_def_add(&p, PA_FILTERS, c.names[k], 0) && it) {
        /* one item chosen: the others hidden */
        int one = atoi(it), j;
        PivotField *pf = &p.filters[p.nfilters - 1];
        for (j = 0; j < c.nitems[k]; j++) if (j != one) add_hidden(pf, c.items[k][j]);
      }
    } else if (!strcmp(x.name, "dataField")) {
      const char *f = xml_attr(&x, "fld"), *nm = xml_attr(&x, "name"), *sub = xml_attr(&x, "subtotal"), *sa = xml_attr(&x, "showDataAs");
      static const char *const subs[PF_N] = { "sum", "count", "average", "max", "min", "product", "countNums", "stdDev", "stdDevp", "var", "varp" };
      int k = f ? atoi(f) : -1, fn = PF_SUM;
      if (k < 0 || k >= c.nf) continue;
      if (sub) for (i = 0; i < PF_N; i++) if (!strcmp(sub, subs[i])) fn = i;
      if (pivot_def_add(&p, PA_VALUES, c.names[k], fn)) {
        PivotValue *v = &p.vals[p.nvals - 1];
        char def[300];
        if (sa) v->showas = (u8)(!strcmp(sa, "percentOfTotal") ? PS_PCT_GRAND : !strcmp(sa, "percentOfCol") ? PS_PCT_COL : !strcmp(sa, "percentOfRow") ? PS_PCT_ROW : PS_NORMAL);
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
  /* hidden items and sorting to the fields they belong to */
  for (i = 0; i < c.nf; i++) {
    int k, j;
    PivotField *lists[3] = { p.rows, p.cols, p.filters };
    int counts[3] = { p.nrows, p.ncols, p.nfilters }, a;
    for (a = 0; a < 3; a++)
      for (k = 0; k < counts[a]; k++)
        if (str_ieq(lists[a][k].name, c.names[i])) {
          lists[a][k].sort = (u8)sortv[i];
          if (lists[a][k].nhidden) continue;     /* (a page field's one item already says) */
          for (j = 0; j < nhid[i]; j++) add_hidden(&lists[a][k], hid[i][j]);
        }
  }
  if (p.nfilters && p.r >= p.nfilters + 1) { /* the filters sit above the location already */ }
  if ((np = (LPivot *)realloc(s->pivots, (size_t)(s->npivots + 1) * sizeof(LPivot)))) {
    s->pivots = np;
    s->pivots[s->npivots++] = p;
    memset(&p, 0, sizeof p);
  }
out:
  pivot_free1(&p);
  for (i = 0; hid && i < c.nf; i++) free(hid[i]);
  free(hid); free(nhid); free(ax); free(sortv);
  free(d);
  cache_free(&c);
  (void)nax;
}
void tablexml_read_rel(Book *b, ZipReader *z, const char *type, const char *target, int sheet)
{
  if (!strcmp(type, "table")) read_table(b, z, target, sheet);
  else if (!strcmp(type, "pivotTable")) read_pivot(b, z, target, sheet);
}
void tablexml_loaded(Book *b)
{
  tables_tidy_loaded(b);
  book_pivots_refresh_all(b);
}
