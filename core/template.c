/*
 * template.c - see template.h. Each template is typed in as a person would (book_input), then
 * given its look, so the formulas are ordinary ones the user can change.
 */
#include <string.h>
#include <stdio.h>
#include "template.h"
#include "ops.h"

/* what a template's cells are: "A1", what is typed */
typedef struct { const char *ref, *text; } Entry;
/* a look for a range: bold, size (tenths of a point, 0 as it is), colours, a line under, a format */
typedef struct { const char *range; u8 bold; u16 size; u32 color, fill; u8 bottom, top; const char *fmt; u8 halign; } Look;

static void put_all(Book *b, int sheet, const Entry *e)
{
  for (; e->ref; e++) {
    u16 sh; Rect r; u8 fl;
    if (parse_ref(b, e->ref, &sh, &r, &fl)) book_input(b, sheet, r.r1, r.c1, e->text, NULL);
  }
}
/* the edit book_restyle makes for one Look */
static void look_edit(Style *st, Font *f, const void *arg)
{
  const Look *k = (const Look *)arg;
  if (k->bold) f->bold = 1;
  if (k->size) f->size = k->size;
  if (k->color) f->color = k->color;
  if (k->fill) st->fill = FILL_ON | k->fill;
  if (k->bottom) st->border[B_BOTTOM] = k->bottom;
  if (k->top) st->border[B_TOP] = k->top;
  if (k->halign) st->halign = k->halign;
}
static Book *fmt_book;   /* (look_fmt needs the book for the format's id) */
static void look_fmt(Style *st, Font *f, const void *arg) { (void)f; st->numfmt = (u16)book_fmt(fmt_book, (const char *)arg); }
static void style_all(Book *b, int sheet, const Look *k)
{
  for (; k->range; k++) {
    u16 sh; Rect r; u8 fl;
    if (!parse_ref(b, k->range, &sh, &r, &fl)) continue;
    book_restyle(b, sheet, r, look_edit, k);
    if (k->fmt) { fmt_book = b; book_restyle(b, sheet, r, look_fmt, k->fmt); }
  }
}
static void widths(Book *b, int sheet, const u16 *w, int n) { int i; for (i = 0; i < n; i++) book_set_col_width(b, sheet, i, w[i]); }

static const Entry budget[] = {
  { "A1", "Monthly Budget" }, { "A3", "Category" }, { "B3", "Budgeted" }, { "C3", "Actual" }, { "D3", "Difference" },
  { "A4", "Rent" }, { "B4", "950" }, { "C4", "950" },
  { "A5", "Food" }, { "B5", "320" }, { "C5", "355.40" },
  { "A6", "Transport" }, { "B6", "120" }, { "C6", "96.50" },
  { "A7", "Utilities" }, { "B7", "150" }, { "C7", "162.25" },
  { "A8", "Fun" }, { "B8", "100" }, { "C8", "80" },
  { "A9", "Savings" }, { "B9", "250" }, { "C9", "250" },
  { "D4", "=B4-C4" }, { "D5", "=B5-C5" }, { "D6", "=B6-C6" }, { "D7", "=B7-C7" }, { "D8", "=B8-C8" }, { "D9", "=B9-C9" },
  { "A10", "Total" }, { "B10", "=SUM(B4:B9)" }, { "C10", "=SUM(C4:C9)" }, { "D10", "=SUM(D4:D9)" },
  { "A12", "Income" }, { "B12", "2000" }, { "A13", "Left over" }, { "B13", "=B12-C10" }, { NULL, NULL } };
static const Look budget_look[] = {
  { "A1", 1, 180, 0x44546A, 0, 0, 0, NULL, 0 }, { "A3:D3", 1, 0, 0xFFFFFF, 0x4472C4, 0, 0, NULL, 0 },
  { "B4:D10", 0, 0, 0, 0, 0, 0, "#,##0.00", 0 }, { "A10:D10", 1, 0, 0, 0, BS_DOUBLE, BS_THIN, NULL, 0 },
  { "B12:B13", 0, 0, 0, 0, 0, 0, "#,##0.00", 0 }, { "A13:B13", 1, 0, 0, 0xE2EFDA, 0, 0, NULL, 0 }, { NULL, 0, 0, 0, 0, 0, 0, NULL, 0 } };
static const Entry invoice[] = {
  { "A1", "INVOICE" }, { "A3", "Bill to:" }, { "B3", "Customer name" }, { "D3", "Invoice no." }, { "E3", "1001" },
  { "D4", "Date" }, { "E4", "=TODAY()" }, { "B4", "Address" },
  { "A6", "Item" }, { "B6", "Description" }, { "C6", "Quantity" }, { "D6", "Unit price" }, { "E6", "Amount" },
  { "A7", "1" }, { "B7", "Consulting (hours)" }, { "C7", "10" }, { "D7", "45" },
  { "A8", "2" }, { "B8", "Floppy disks (box)" }, { "C8", "3" }, { "D8", "12.5" },
  { "A9", "3" }, { "B9", "Delivery" }, { "C9", "1" }, { "D9", "8" },
  { "E7", "=C7*D7" }, { "E8", "=C8*D8" }, { "E9", "=C9*D9" }, { "E10", "=C10*D10" }, { "E11", "=C11*D11" },
  { "D13", "Subtotal" }, { "E13", "=SUM(E7:E11)" }, { "D14", "Tax rate" }, { "E14", "20%" }, { "D15", "Tax" }, { "E15", "=E13*E14" },
  { "D16", "Total" }, { "E16", "=E13+E15" }, { "A18", "Thank you for your business!" }, { NULL, NULL } };
static const Look invoice_look[] = {
  { "A1", 1, 240, 0x2F5597, 0, 0, 0, NULL, 0 }, { "A6:E6", 1, 0, 0, 0xDDEBF7, BS_THIN, 0, NULL, 0 }, { "E4", 0, 0, 0, 0, 0, 0, "d mmmm yyyy", 0 },
  { "D7:E13", 0, 0, 0, 0, 0, 0, "#,##0.00", 0 }, { "E15:E16", 0, 0, 0, 0, 0, 0, "#,##0.00", 0 }, { "A11:E11", 0, 0, 0, 0, BS_THIN, 0, NULL, 0 },
  { "D16:E16", 1, 120, 0, 0, BS_DOUBLE, BS_THIN, NULL, 0 }, { "A18", 0, 0, 0x808080, 0, 0, 0, NULL, 0 }, { NULL, 0, 0, 0, 0, 0, 0, NULL, 0 } };
static const Entry expenses[] = {
  { "A1", "Date" }, { "B1", "Description" }, { "C1", "Category" }, { "D1", "Amount" },
  { "A2", "=TODAY()-6" }, { "B2", "Train to the show" }, { "C2", "Travel" }, { "D2", "23.40" },
  { "A3", "=TODAY()-5" }, { "B3", "Lunch with a client" }, { "C3", "Meals" }, { "D3", "31.75" },
  { "A4", "=TODAY()-5" }, { "B4", "Hotel" }, { "C4", "Lodging" }, { "D4", "89" },
  { "A5", "=TODAY()-3" }, { "B5", "Taxi" }, { "C5", "Travel" }, { "D5", "14.20" },
  { "A6", "=TODAY()-1" }, { "B6", "Printer paper" }, { "C6", "Supplies" }, { "D6", "6.99" },
  { "C8", "Total" }, { "D8", "=SUM(D2:D7)" },
  { "F1", "Category" }, { "G1", "Spent" },
  { "F2", "Travel" }, { "G2", "=SUMIF($C$2:$C$7,F2,$D$2:$D$7)" }, { "F3", "Meals" }, { "G3", "=SUMIF($C$2:$C$7,F3,$D$2:$D$7)" },
  { "F4", "Lodging" }, { "G4", "=SUMIF($C$2:$C$7,F4,$D$2:$D$7)" }, { "F5", "Supplies" }, { "G5", "=SUMIF($C$2:$C$7,F5,$D$2:$D$7)" }, { NULL, NULL } };
static const Look expenses_look[] = {
  { "A1:D1", 1, 0, 0xFFFFFF, 0x548235, 0, 0, NULL, 0 }, { "F1:G1", 1, 0, 0xFFFFFF, 0x548235, 0, 0, NULL, 0 },
  { "A2:A7", 0, 0, 0, 0, 0, 0, "d-mmm-yy", 0 }, { "D2:D8", 0, 0, 0, 0, 0, 0, "#,##0.00", 0 }, { "G2:G5", 0, 0, 0, 0, 0, 0, "#,##0.00", 0 },
  { "C8:D8", 1, 0, 0, 0xE2EFDA, 0, BS_THIN, NULL, 0 }, { NULL, 0, 0, 0, 0, 0, 0, NULL, 0 } };
static const Entry grades[] = {
  { "A1", "Student" }, { "B1", "Test 1" }, { "C1", "Test 2" }, { "D1", "Project" }, { "E1", "Average" }, { "F1", "Result" },
  { "A2", "Ada" }, { "B2", "92" }, { "C2", "88" }, { "D2", "95" },
  { "A3", "Brian" }, { "B3", "71" }, { "C3", "64" }, { "D3", "80" },
  { "A4", "Chloe" }, { "B4", "55" }, { "C4", "48" }, { "D4", "62" },
  { "A5", "Dev" }, { "B5", "83" }, { "C5", "90" }, { "D5", "78" },
  { "A6", "Erin" }, { "B6", "67" }, { "C6", "73" }, { "D6", "70" },
  { "E2", "=AVERAGE(B2:D2)" }, { "E3", "=AVERAGE(B3:D3)" }, { "E4", "=AVERAGE(B4:D4)" }, { "E5", "=AVERAGE(B5:D5)" }, { "E6", "=AVERAGE(B6:D6)" },
  { "F2", "=IF(E2>=$I$2,\"Pass\",\"Retake\")" }, { "F3", "=IF(E3>=$I$2,\"Pass\",\"Retake\")" }, { "F4", "=IF(E4>=$I$2,\"Pass\",\"Retake\")" },
  { "F5", "=IF(E5>=$I$2,\"Pass\",\"Retake\")" }, { "F6", "=IF(E6>=$I$2,\"Pass\",\"Retake\")" },
  { "A8", "Class average" }, { "E8", "=AVERAGE(E2:E6)" }, { "A9", "Highest" }, { "E9", "=MAX(E2:E6)" },
  { "H2", "Pass mark" }, { "I2", "60" }, { NULL, NULL } };
static const Look grades_look[] = {
  { "A1:F1", 1, 0, 0xFFFFFF, 0xC55A11, 0, 0, NULL, 3 }, { "E2:E9", 0, 0, 0, 0, 0, 0, "0.0", 0 }, { "A8:E9", 1, 0, 0, 0, 0, 0, NULL, 0 },
  { "A8:F8", 0, 0, 0, 0, 0, BS_THIN, NULL, 0 }, { "H2:I2", 1, 0, 0, 0xFCE4D6, 0, 0, NULL, 0 }, { NULL, 0, 0, 0, 0, 0, 0, NULL, 0 } };

Book *book_template(int t)
{
  Book *b = book_new();
  static const u16 w_budget[] = { 140, 90, 90, 90 }, w_invoice[] = { 60, 180, 70, 80, 90 }, w_exp[] = { 80, 160, 90, 80, 30, 90, 80 },
                   w_grades[] = { 110, 64, 64, 64, 70, 64, 30, 80, 50 };
  if (!b || t <= TPL_BLANK || t >= TPL_N) return b;
  b->manualcalc = 1;
  switch (t) {
  case TPL_BUDGET: book_rename_sheet(b, 0, "Budget"); put_all(b, 0, budget); style_all(b, 0, budget_look); widths(b, 0, w_budget, 4); break;
  case TPL_INVOICE: book_rename_sheet(b, 0, "Invoice"); put_all(b, 0, invoice); style_all(b, 0, invoice_look); widths(b, 0, w_invoice, 5); break;
  case TPL_EXPENSES: book_rename_sheet(b, 0, "Expenses"); put_all(b, 0, expenses); style_all(b, 0, expenses_look); widths(b, 0, w_exp, 7); book_freeze(b, 0, 1, 0); break;
  case TPL_GRADES: book_rename_sheet(b, 0, "Grades"); put_all(b, 0, grades); style_all(b, 0, grades_look); widths(b, 0, w_grades, 9); book_freeze(b, 0, 1, 1); break;
  }
  b->manualcalc = 0;
  book_forget_undo(b);
  book_recalc(b);
  b->dirty = 0;
  return b;
}
