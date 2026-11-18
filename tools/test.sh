#!/bin/sh
# Ledger's native tests (no Amiga toolchain needed): the core test (which includes the analysis, drawing,
# layout and ui tests) with ASan/UBSan, then the interface-string checks. The same steps build.sh runs
# before the Amiga build. TMP=<dir> to build elsewhere than /tmp.
set -e
cd "$(dirname "$0")/.."
T=${TMP:-/tmp}
CORE="core/util.c core/zip.c core/xml.c core/book.c core/formula.c core/calc.c core/func.c core/func_stat.c core/func_eng.c core/func_fin.c core/func_misc.c core/func_array.c core/funcdesc.c core/numfmt.c core/xlsx.c core/splitpane.c core/ops.c core/cond.c core/template.c core/fonts.c core/ttraster.c core/pagesetup.c core/pdfout.c core/spell.c core/audit.c core/htmlout.c core/comments.c core/table.c core/pivot.c core/slicer.c core/tablexml.c core/drawing.c core/drwgroup.c core/chart.c core/spark.c core/image.c core/xlsx_draw.c core/valid.c core/outline.c core/protect.c core/flash.c core/datatools.c core/whatif.c core/xls.c core/ods.c core/solver.c core/toolpak.c core/forecast.c core/cellctl.c core/sylk.c core/formctl.c core/pycell.c core/equation.c core/htmlin.c core/gridgeo.c"
cc -std=c99 -O1 -g -Wall -Wextra -Wno-unused-parameter -fsanitize=address,undefined $CORE tests/coretest.c -o $T/ledger_coretest -lm
$T/ledger_coretest | tail -3
python3 tools/mklang.py --check
python3 tools/langcheck.py | tail -1
