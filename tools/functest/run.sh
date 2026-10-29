#!/bin/sh
# Tests every function of Ledger's, each case twice: with its arguments typed in and with them in
# cells. Ledger's core works the workbook out, LibreOffice does too, and the results are compared.
# Needs python3 with openpyxl, LibreOffice (soffice) and a C compiler.
set -e
cd "$(dirname "$0")"
L=../..
W=${TMPDIR:-/tmp}/ledger-functest
rm -rf "$W" && mkdir -p "$W"
CORE=$(sed -n 's/^CORE="\(.*\)"$/\1/p' $L/build.sh)
(cd $L && cc -std=c99 -D_DEFAULT_SOURCE -O1 -I. $CORE tools/functest/harness.c -o "$W/harness" -lm)
python3 gen.py . "$W/ft.xlsx" "$W/meta.json"
"$W/harness" "$W/ft.xlsx" > "$W/ledger.tsv"
soffice --headless --convert-to xlsx --outdir "$W/lo" "$W/ft.xlsx" > /dev/null 2>&1
python3 compare.py "$W"
