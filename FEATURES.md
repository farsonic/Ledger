# Ledger: features missing against GridCraft

Worked through top to bottom. ✅ done (built, tested natively and in the emulator) · ⬜ to do · ➖ left out on purpose.

| # | Feature | GridCraft has | Status |
|---|---------|---------------|--------|
| 1 | Coloured references while typing a formula (each reference and its cells in its own colour) | yes | ✅ |
| 2 | AutoFilter (filter buttons on a heading row; filter by values, sort from the button) | yes | ✅ |
| 3 | Conditional formatting: highlight rules (greater/less/between/equal/text/duplicates/top 10) | yes | ✅ |
| 4 | Conditional formatting: data bars and colour scales | yes | ✅ |
| 5 | Comments / notes on cells (red corner, shown on hover, saved in .xlsx) | yes | ✅ |
| 6 | Hyperlinks in cells (HYPERLINK function and Insert Link, saved in .xlsx) | yes | ✅ |
| 7 | Data validation (drop-down lists, number/date limits, warnings) | yes | ✅ |
| 8 | Charts: column, bar, line, pie (drawn on the sheet, saved in .xlsx) | yes | ✅ |
| 9 | Charts: area, scatter, doughnut; chart title, legend, axis options | yes | ✅ |
| 10 | Format as Table (banded rows, header row, totals row) | yes | ✅ |
| 11 | Dynamic array formulas that spill (SEQUENCE, SORT, FILTER, UNIQUE...) | yes | ✅ |
| 12 | LET and LAMBDA | yes | ✅ |
| 13 | More functions (statistics, engineering, text, information: towards GridCraft's 500+): Ledger has every one of GridCraft's (516 at 4c388df, ECMA.CEILING and GETPIVOTDATA the latest), each with its description and Excel category in Insert Function | 500+ | ✅ |
| 14 | Text to Columns | yes | ✅ |
| 15 | Group and outline rows/columns; Subtotals | yes | ✅ |
| 16 | Protect sheet (locked cells can't be changed) | yes | ✅ |
| 17 | Sparklines (line, column) | yes | ✅ |
| 18 | Pivot tables (calculated fields, grouping by dates, intervals and by hand, expand/collapse, running total and rank, Recommended PivotTables, PivotCharts, slicers on tables and pivot tables) | yes | ✅ |
| 19 | Old Excel .xls files (values, formulas, number formats) | yes | ✅ |
| 20 | OpenDocument .ods import | yes | ✅ |
| 21 | Pictures on the sheet (PNG/JPEG) | yes | ✅ |
| 22 | Spelling check | yes | ✅ |
| 23 | Interface languages (German, French, Spanish, Italian, as Quill) | yes | ✅ |
| 24 | PDF export | yes | ✅ |
| 25 | Flash Fill | yes | ✅ |
| 26 | Printing, page setup, Page Break Preview | yes | ✅ page setup, Page Break Preview, Page Layout view (printing itself ➖ left out, as for Quill) |
| 27 | ARexx / scripting control channel | MCP | ➖ (left out, as for Quill) |
| 28 | Solver (Simplex LP with branch and bound, GRG Nonlinear with Multistart, Evolutionary; reports; model saved in hidden names) | yes | ✅ |
| 29 | Analysis ToolPak (Data Analysis: Anova, regression, t/F/z tests, histogram, descriptive statistics and the rest of Excel's 19 tools) | yes | ✅ |
| 30 | Forecast Sheet (FORECAST.ETS with confidence bounds, table and chart on a new sheet) | yes | ✅ |
| 31 | Checkbox cells, Lock Cell, Allow Edit Ranges (saved in .xlsx as Excel saves them) | yes | ✅ |
| 32 | WordArt (Insert > WordArt, GridCraft's ten styles, saved with its textNoShape marker) | yes (4c388df) | ✅ |
| 33 | Set precision as displayed | yes (4c388df) | ✅ |
| 34 | Safe saving (the new file written in full before it replaces the old one) | yes (4c388df) | ✅ |
| 35 | Equations (Insert > Equation, LaTeX, Office Math in .xlsx) | yes (4c388df) | ✅ LaTeX subset, drawn by Ledger, Office Math kept and written |
| 36 | Python in cells (stored with cached results, no code run) | yes (4c388df) | ✅ kept with their results (running code ➖) |
| 37 | Form controls (spin button, scroll bar, button) | yes (4c388df) | ✅ spin button and scroll bar; buttons ➖ (they run macros) |
| 38 | SYLK (.slk) files | yes (4c388df) | ✅ opened and saved |
| 39 | A web page's table opened as a sheet (a file on disk; no network) | yes (4c388df) | ✅ |
| 40 | Solver Load/Save Model, Options, Sensitivity report | yes (4c388df) | ✅ |
| 41 | CSV import with each column's date order | yes (4c388df) | ✅ |
| 42 | Right-to-left sheets (mirrored grid, Arabic text) | yes (4c388df) | ✅ the grid drawn mirrored as Excel draws it (column A at the right, row headings on the right, the horizontal scroll bar reversed, clicks, drags, fill handle, resizing, frozen panes and split bars mirrored, General text right and numbers left), Page Layout > Sheet Right-to-Left (and the Page menu) per sheet, kept in .xlsx; Arabic text itself isn't shaped (the Amiga's fonts have no Arabic) |

Rows 35-41 (round 2 of the 4c388df sync, 2026-11-18) are tested natively and build for the Amiga; their check in the emulator is still to do.
