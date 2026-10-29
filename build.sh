#!/bin/sh
# Build Ledger for AmigaOS (vbcc + NDK 3.2 + MUI 3.8 headers, 68040 with its FPU) and run the native
# core tests first.
set -e
export VBCC=${VBCC:-/opt/amiga/vbcc}
export PATH=$VBCC/bin:$PATH
NDK=${NDK:-/opt/amiga/ndk/Include_H}
MUI=${MUI:-/opt/amiga/mui/include}
cd "$(dirname "$0")"
CORE="core/util.c core/zip.c core/xml.c core/book.c core/formula.c core/calc.c core/func.c core/func_stat.c core/func_eng.c core/func_fin.c core/func_misc.c core/func_array.c core/numfmt.c core/xlsx.c core/ops.c core/cond.c core/template.c core/fonts.c core/ttraster.c core/pagesetup.c core/pdfout.c core/spell.c core/audit.c core/htmlout.c"
cc -std=c99 -O1 -g -Wall -Wextra -Wno-unused-parameter -fsanitize=address,undefined $CORE tests/coretest.c -o /tmp/ledger_coretest -lm
/tmp/ledger_coretest | tail -3
vc +tools/vbcc-aos68k-fpu -c99 -O2 -cpu=68040 -fpu=68040 -I. -I"$MUI" -I"$NDK" amiga/*.c $CORE -o Ledger -lamiga -lm040
