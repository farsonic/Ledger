#!/usr/bin/env python3
"""Makes the small .xls and .ods test workbooks (tests/fixtures/import.xls, import.ods) that
coretest.c reads: a workbook written with openpyxl, then converted by LibreOffice headless.
   python3 tests/fixtures/mkfixtures.py  (needs openpyxl and soffice)"""
import os, subprocess, sys, tempfile, shutil
from openpyxl import Workbook
from openpyxl.styles import Font, PatternFill, Alignment
from openpyxl.worksheet.datavalidation import DataValidation

here = os.path.dirname(os.path.abspath(__file__))
wb = Workbook()
s = wb.active
s.title = "Data"
s["A1"] = "Item"; s["B1"] = "Qty"; s["C1"] = "Price"; s["D1"] = "Total"
rows = [("Apple", 3, 0.5), ("Pear", 4, 0.75), ("Plum", 10, 0.2), ("Fig", 1, 2.5)]
for i, (a, b, c) in enumerate(rows, start=2):
    s.cell(i, 1, a); s.cell(i, 2, b); s.cell(i, 3, c)
    s.cell(i, 4, "=B%d*C%d" % (i, i))
s["A6"] = "Sum"; s["D6"] = "=SUM(D2:D5)"
s["A7"] = "Check"; s["B7"] = '=IF(D6>10,"big","small")'
s["C7"] = "=ROUND(AVERAGE(C2:C5),2)"
s["D7"] = "=COUNTIF(B2:B5,\">2\")"
s["A8"] = True; s["B8"] = "=Other!B2*2"; s["C8"] = "=-C2%"; s["D8"] = '=CONCATENATE(A2," & ",A3)'
s["A9"] = "=VLOOKUP(\"Plum\",$A$2:$C$5,3,FALSE)"; s["B9"] = "=1/0"; s["C9"] = "=SUM(B:B)"; s["D9"] = "=MAX(B2:B5)-MIN(B2:B5)"
s["A10"] = 46322; s["A10"].number_format = "yyyy-mm-dd"
s["B10"] = 0.25; s["B10"].number_format = "0.0%"
s["C10"] = 1234.5; s["C10"].number_format = "#,##0.00"
s["D10"] = "=EDATE(A10,1)"; s["D10"].number_format = "yyyy-mm-dd"
s["A1"].font = Font(bold=True, size=14, color="C00000")
s["B1"].font = Font(italic=True, name="Arial")
s["C1"].fill = PatternFill("solid", fgColor="FFFF00")
s["D1"].alignment = Alignment(horizontal="center")
s.column_dimensions["A"].width = 20
s.row_dimensions[3].height = 30
s.merge_cells("A12:C12"); s["A12"] = "Merged title"
s["A13"] = "Café über"
dv = DataValidation(type="list", formula1='"Yes,No,Maybe"', allow_blank=True, promptTitle="Answer", prompt="Pick one")
dv.add("E2:E5"); s.add_data_validation(dv)
dv2 = DataValidation(type="whole", operator="between", formula1="1", formula2="10", errorTitle="Qty", error="1 to 10", errorStyle="warning")
dv2.add("B2:B5"); s.add_data_validation(dv2)
s.row_dimensions.group(2, 5, outline_level=1)
o = wb.create_sheet("Other")
o["A1"] = "x"; o["B2"] = 21; o["A3"] = "=Data!D6+B2"
tmp = tempfile.mkdtemp()
src = os.path.join(tmp, "import.xlsx")
wb.save(src)
for ext, filt in (("xls", "xls:MS Excel 97"), ("ods", "ods")):
    subprocess.run(["soffice", "--headless", "--convert-to", filt, "--outdir", tmp, src], check=True, stdout=subprocess.DEVNULL)
    shutil.copy(os.path.join(tmp, "import." + ext), os.path.join(here, "import." + ext))
print("made", os.listdir(here))
