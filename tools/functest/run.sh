#!/bin/sh
# Tests every function of Ledger's, each case twice: with its arguments typed in and with them in
# cells. Ledger's core works the workbook out, LibreOffice does too, and the results are compared.
# Needs python3 with openpyxl, LibreOffice (soffice) and a C compiler.
set -e
cd "$(dirname "$0")"
L=../..
W=${TMPDIR:-/tmp}/ledger-functest
rm -rf "$W" && mkdir -p "$W"
CORE="core/util.c core/zip.c core/xml.c core/book.c core/formula.c core/calc.c core/func.c core/func_stat.c core/func_eng.c core/func_fin.c core/func_misc.c core/func_array.c core/numfmt.c core/xlsx.c core/ops.c core/cond.c core/template.c core/fonts.c core/ttraster.c"
(cd $L && cc -std=c99 -D_DEFAULT_SOURCE -O1 -I. $CORE tools/functest/harness.c -o "$W/harness" -lm)
python3 gen.py . "$W/ft.xlsx" "$W/meta.json"
"$W/harness" "$W/ft.xlsx" > "$W/ledger.tsv"
soffice --headless --convert-to xlsx --outdir "$W/lo" "$W/ft.xlsx" > /dev/null 2>&1
python3 compare.py "$W"
