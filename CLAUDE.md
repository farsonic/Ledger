# CLAUDE.md - working on Ledger

Ledger is a spreadsheet for AmigaOS 3.x (68040/68060 with FPU, MUI 3.8, AROS or MorphOS), written in C
and laid out the way GridCraft (an Excel-like program) is: ribbon, formula bar, grid, sheet tabs. It
reads and writes Excel .xlsx (and opens .xls, .ods, SYLK and web pages' tables, writes .csv, SYLK, PDF
and web pages), has 516 worksheet functions (every one of GridCraft's) with dynamic arrays, LET/LAMBDA,
charts, sparklines, pictures, shapes, equations and form controls, tables, pivot tables and slicers,
data validation, the Data tab's tools (Solver, Analysis ToolPak, Goal Seek, scenarios, Forecast Sheet),
protection, page layout and PDF export, in five interface languages.
GPL-2.0-only (see `LICENSE`). This file is for anyone, person or agent, picking the work up. Read it
first, then `README.md` (what Ledger does) and `FEATURES.md` / `docs/gridcraft-gap.md` (what's missing).

## Layout

| Path | What |
|---|---|
| `core/` | Portable C99, no AmigaOS calls: the workbook (`book.c`), formula parser and evaluator (`formula.c`, `calc.c`), functions (`func*.c`, with `funcdesc.c` describing each), number formats, `.xlsx`/`.xls`/`.ods` readers and writers, charts, tables, pivots, slicers, drawings, validation, outline, protection, Flash Fill, data tools, what-if, Solver, the Analysis ToolPak, forecasting, page setup, PDF and HTML output, spelling, fonts and the TrueType rasteriser. |
| `amiga/` | The MUI front end: the window, ribbon, grid, formula bar, panes and dialogs; `lang.c`/`lang_table.h` (generated) for interface strings. |
| `lang/` | `strings.txt`: every interface string with its German, French, Spanish and Italian translations. `tools/mklang.py` makes `amiga/lang_table.h`; `tools/langcheck.py` checks every string is translated and used. |
| `tests/` | `coretest.c` (it includes `respathtest.c`, `analysistest.c`, `drawtest.c`, `layouttest.c`, `uitest.c`, `synctest.c` and `gc2test.c`, the two rounds of the GridCraft 4c388df sync), `xlsxtool.c`, `fixtures/` (made by `mkfixtures.py`). |
| `tools/` | `test.sh` (native tests, what CI runs), `functest/` (makes and checks `FunctionTests.xlsx`, compares with LibreOffice), `Install-Ledger` (Installer script), `mkicons.py`, `vbcc-aos68k-fpu` (the vbcc config for the 68040+FPU build). |
| `docs/` | `gridcraft-gap.md` (every GridCraft ribbon command and Ledger's status), `functions-*.txt`, and `gridcraft/` (copies of GridCraft's own reference documents, MIT, see its README). |
| `FEATURES.md` | GridCraft's big features, one row each, with Ledger's status. |
| `FunctionTests.xlsx` | 3,615 tests of every function, with PASS/FAIL cells; shipped as an example workbook. |

## Build and test

Native tests (need a C compiler and Python 3; CI runs them and they must stay green):

    sh tools/test.sh            # TMP=<dir> to build somewhere other than /tmp

Expected: `1027 tests, 0 failed` (or more), `amiga/lang_table.h is up to date` and langcheck reporting 0
missing strings. When you add or change interface strings, edit `lang/strings.txt` (all five languages),
then `python3 tools/mklang.py` and `python3 tools/langcheck.py`.

The function workbook: `tools/functest/` regenerates and checks `FunctionTests.xlsx` (see its scripts);
every function change should keep all of its rows passing, natively and on a 68040.

Amiga binary: `./build.sh` runs the native tests, then builds `Ledger` with vbcc. It needs, at these
default paths (or set `VBCC`, `NDK`, `MUI`):

- **vbcc** 0.9h with vasm and vlink and the m68k-amigaos target, in `/opt/amiga/vbcc`
  (http://www.compilers.de/vbcc.html), with the 68040 FPU maths library (`-lm040`); Ledger uses the
  config in `tools/vbcc-aos68k-fpu`.
- **NDK 3.2** C headers in `/opt/amiga/ndk/Include_H` (Aminet `dev/misc/NDK3.2.lha`).
- **MUI 3.8 developer headers** in `/opt/amiga/mui/include` (Aminet `dev/mui/mui38dev.lha`, its
  `C/Include`; not in this repository because it may only be redistributed whole).

Release package: the Ledger drawer holds Ledger, README.md, Install-Ledger, FunctionTests.xlsx, the
Dict, Fonts and TrueType drawers (shared with Quill), Source/ and LICENSE, with icons from
`tools/mkicons.py <package-dir>`. Packages are `.lha` and not committed (attach them to GitHub releases).
Before a package ships, search it for certificate or key material and refuse if any is found.

## Rules of the project

- C99 that vbcc compiles. On the 68040, vbcc's `floor`/`ceil` go wrong past 2,147,483,647 (Ledger has its
  own); the Amiga C library formats some numbers differently from glibc (see FIXED/DOLLAR in the README's
  0.2.2 notes). Test number formatting on the Amiga build, not only natively.
- Where Excel and LibreOffice disagree, Ledger follows Excel's documented behaviour; say so in the
  FunctionTests row's note.
- Every interface string is translated (English, German, French, Spanish, Italian, in Excel's words for
  each); the window fits 640x480; menus alongside the ribbon.
- Shared drawers (Fonts, TrueType, Dict) are found through `core/respath.c` (beside the program, then the
  Bureau suite's `Bureau:`, then `FONTS:` for screen fonts; resolved once at start, `theme_find_shared`):
  never write `PROGDIR:Fonts` and the like in `amiga/`. `core/respath.c/.h` are the same text in all five
  Bureau programs' repositories; change them in all or none.
- Printing and GridCraft's scripting tools (Automate tab, ARexx) are left out on purpose.
- A new feature gets a native test, its row in `FEATURES.md` / `docs/gridcraft-gap.md` updated, and a
  README note.
- Don't use `rm -rf` in scripts or by hand on shared machines; move things aside.
- Commit identity: commits so far use `Claude <noreply@anthropic.com>` or `Ledger <ledger@localhost>`.
  Never put a personal email address in commits, code or docs.

## Where work stands

Ledger 0.3 is released: it closed the gap with GridCraft (every function, every ribbon command but the
scripting tools, and the big features; see the README's "What's new in 0.3"). All feature branches are
merged into `master`. `FEATURES.md` and `docs/gridcraft-gap.md` mark anything still to do; beyond them,
likely next steps are speed on large sheets on a real 68040, more file-format fidelity (charts and pivots
written by other programs), and checks on real AmigaOS 3.x with MUI.

## Issues

Bugs and requests come in as GitHub issues (templates in `.github/ISSUE_TEMPLATE/`). To work on them:

1. `gh issue list --repo farsonic/Ledger` and `gh issue view N --repo farsonic/Ledger --comments`.
2. Reproduce first. Formula and file bugs can almost always be reproduced natively: build a workbook in a
   test (or load the attached one with the core, see `tests/xlsxtool.c`), write a failing test in
   `tests/coretest.c` (or a FunctionTests row for a function), then fix it in `core/`. Interface bugs need
   the Amiga build; say what you could and couldn't verify.
3. Fix on a branch named `issue-N-short-name`, keep `tools/test.sh` green, and open a pull request whose
   description says `Fixes #N`, what changed and how it was tested. Ask on the issue when information is
   missing rather than guessing. Don't close issues by hand unless asked; merging the PR closes them.
4. Attached workbooks may be private: never commit them; reproduce with a small workbook of your own.

`.github/workflows/claude.yml` lets people mention `@claude` in an issue or pull request to have Claude
Code look at it (it needs the `ANTHROPIC_API_KEY` repository secret or the Claude GitHub App).
