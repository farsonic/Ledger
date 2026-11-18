# Ledger against GridCraft: what is missing

GridCraft (github.com/storytold/sheetcraft, the ArtCraft spreadsheet) is the program Ledger follows. This list was first made on 2026-10-29 from GridCraft's own source (its function registry) and its ribbon command catalogue (docs/gridcraft/parity-checklist.md, copied here with function-parity.md and gaps.md), and brought up to date on 2026-11-18 against GridCraft commit **4c388df** (the copies in docs/gridcraft/ are from that commit). It is the work list for closing the gaps; FEATURES.md is ticked off as features land.

Left out on purpose, as in Quill: printing (PDF export is in) and ARexx/MCP scripting (with them GridCraft's macros, Record Macro, Python code that runs, and buttons that run macros).

## Functions

GridCraft 4c388df has 516 worksheet functions (511 in its registry and evaluator special forms, listed in docs/gridcraft/functions.txt, plus RAND, RANDARRAY, RANDBETWEEN, NOW and TODAY). Ledger has every one of them: the two GridCraft added since 2026-10-29 are in (2026-11-18):

- ✅ ECMA.CEILING (Compatibility): CEILING by its ECMA-376 name, `_xlfn.` in files; not on Microsoft's list, as USDOLLAR.
- ✅ GETPIVOTDATA (Lookup): the value a pivot table shows for a data field where the named Rows and Columns items meet, from the pivot's definition and source (volatile); `#REF!` for anything the report doesn't show. (GridCraft's "Generate GetPivotData", typing it by clicking a report cell, is not in Ledger either.)
- ⬜ PY: GridCraft keeps Python cells (Excel's `_xlfn._xlws.PY`) with their cached results without running the code; Ledger opens such a cell as a formula of an unknown function showing its cached value.

GridCraft's function fixes in this period, each checked in Ledger first and made where Ledger differed (with FunctionTests rows, LibreOffice's differing answers noted in tools/functest/excel.txt): HARMEAN with no numbers `#N/A`; T.DIST with 0 degrees `#DIV/0!` (density), T.INV(0) `#DIV/0!`; GAMMA.DIST, WEIBULL.DIST and BETA.DIST densities at 0 and at the ends; BINOM.DIST's 2,147,483,646-trial limit; BINOM.DIST and POISSON.DIST masses by Loader's saddle point (1E-8 relative error at 1E7 trials before); the t density at huge degrees; PERCENTRANK's significance 309 and 15-digit rounding; F.DIST/F.INV at 1E10 degrees; MOD's 1.1259E12 quotient limit; YEARFRAC basis 0 from February's end; LINEST keeping a predictor that varies little next to a large offset (Ledger centres the variables; it already solved by orthogonalisation, not the normal equations); FIXED and DOLLAR's 255/254-character limits and digits past the fifteenth (Ledger now shows 15 significant digits and then zeros everywhere, as Excel); single-character case changes (LOWER of the dotted capital I is i, Latin Extended-A letters); UNICHAR's noncharacters; IMSQRT by Excel's polar route; BITLSHIFT/BITRSHIFT of 0; TRUE as DEC2BIN's places. Already as Excel in Ledger: GCD/LCM with negative fractions, BIN2*/HEX2*/OCT2* with spaces, UPPER keeping ß, DOLLAR of a negative amount rounding to 0, HYPERLINK without a friendly name, AutoSum stopping at a blank.

## Ribbon commands

GridCraft 4c388df implements 282 of the 296 Excel ribbon commands in its catalogue (275 of 291 on 2026-10-29: since then Insert's WordArt, called Decorative Text there, and Equation, View's Record Macro and a Developer tab of form controls). Below, each of GridCraft's commands, tab by tab, with a first guess (by name) whether Ledger has it: ✅ probably has, ⬜ probably missing. Each is to be checked and the missing ones built. The File, Home (but for the commands other parts of the work own: Format as Table, Flash Fill, protection), Page Layout, Formulas, Review and View tabs have been checked one by one and their marks are real: ✅ done, ⬜ not done (see the note under the tab), ➖ left out on purpose.

### File (7 of 11 by name)

- ✅ New (File) `file.new`
- ✅ Open (File) `file.open`
- ✅ Save (File) `file.save`
- ✅ Save As (File) `file.saveAs`
- ✅ Close (File) `file.close`
- ➖ Print (File) `file.print`
- ✅ Export to PDF (Export) `file.exportPdf`
- ✅ CSV (Export) `file.exportCsv`
- ✅ Web Page (Export) `file.exportHtml`
- ✅ Properties (Info) `file.properties`
- ✅ Options (Options) `file.options`

### Home (68 of 94 by name)

- ✅ Paste (Clipboard) `edit.paste`
- ✅ Paste Special (Clipboard) `edit.pasteSpecial`
- ✅ Cut (Clipboard) `edit.cut`
- ✅ Copy (Clipboard) `edit.copy`
- ✅ Format Painter (Clipboard) `edit.formatPainter`
- ✅ Font (Font) `home.fontName`
- ✅ Font Size (Font) `home.fontSize`
- ✅ Increase Font Size (Font) `home.increaseFontSize`
- ✅ Decrease Font Size (Font) `home.decreaseFontSize`
- ✅ Bold (Font) `home.bold`
- ✅ Italic (Font) `home.italic`
- ✅ Underline (Font) `home.underline`
- ✅ Double Underline (Font) `home.doubleUnderline`
- ✅ Strikethrough (Font) `home.strikethrough`
- ✅ Superscript (Font) `home.superscript`
- ✅ Subscript (Font) `home.subscript`
- ✅ Borders (Font) `home.borders`
- ✅ Draw Border (Font) `home.drawBorder`
- ✅ Fill Color (Font) `home.fillColor`
- ✅ Font Color (Font) `home.fontColor`
- ✅ Top Align (Alignment) `home.alignTop`
- ✅ Middle Align (Alignment) `home.alignMiddle`
- ✅ Bottom Align (Alignment) `home.alignBottom`
- ✅ Orientation (Alignment) `home.orientation`
- ✅ Align Left (Alignment) `home.alignLeft`
- ✅ Center (Alignment) `home.alignCenter`
- ✅ Align Right (Alignment) `home.alignRight`
- ✅ Decrease Indent (Alignment) `home.decreaseIndent`
- ✅ Increase Indent (Alignment) `home.increaseIndent`
- ✅ Wrap Text (Alignment) `home.wrapText`
- ✅ Merge & Center (Alignment) `home.mergeCenter`
- ✅ Merge Across (Alignment) `home.mergeAcross`
- ✅ Merge Cells (Alignment) `home.mergeCells`
- ✅ Unmerge Cells (Alignment) `home.unmergeCells`
- ✅ Number Format (Number) `home.numberFormat`
- ✅ Accounting Number Format (Number) `home.accounting`
- ✅ Percent Style (Number) `home.percent`
- ✅ Comma Style (Number) `home.comma`
- ✅ Increase Decimal (Number) `home.increaseDecimal`
- ✅ Decrease Decimal (Number) `home.decreaseDecimal`
- ✅ Conditional Formatting (Styles) `home.conditionalFormat`
- ✅ Clear Rules (Styles) `home.clearRules`
- ✅ Manage Rules (Styles) `home.manageRules`
- ✅ Format as Table (Styles) `home.formatAsTable`
- ✅ Cell Styles (Styles) `home.cellStyle`
- ✅ New Cell Style (Styles) `home.newCellStyle`
- ✅ Insert Cells (Cells) `home.insertCells`
- ✅ Insert Sheet Rows (Cells) `home.insertRows`
- ✅ Insert Sheet Columns (Cells) `home.insertColumns`
- ✅ Insert Sheet (Cells) `home.insertSheet`
- ✅ Delete Cells (Cells) `home.deleteCells`
- ✅ Delete Sheet Rows (Cells) `home.deleteRows`
- ✅ Delete Sheet Columns (Cells) `home.deleteColumns`
- ✅ Delete Sheet (Cells) `home.deleteSheet`
- ✅ Row Height (Cells) `home.rowHeight`
- ✅ AutoFit Row Height (Cells) `home.autofitRowHeight`
- ✅ Column Width (Cells) `home.columnWidth`
- ✅ AutoFit Column Width (Cells) `home.autofitColumnWidth`
- ✅ Default Width (Cells) `home.defaultWidth`
- ✅ Hide Rows (Cells) `home.hideRows`
- ✅ Hide Columns (Cells) `home.hideColumns`
- ✅ Unhide Rows (Cells) `home.unhideRows`
- ✅ Unhide Columns (Cells) `home.unhideColumns`
- ✅ Hide Sheet (Cells) `sheet.hide`
- ✅ Unhide Sheet (Cells) `sheet.unhide`
- ✅ Rename Sheet (Cells) `sheet.rename`
- ✅ Move or Copy Sheet (Cells) `sheet.move`
- ✅ Tab Color (Cells) `sheet.tabColor`
- ✅ Protect Sheet (Cells) `review.protectSheet`
- ✅ Lock Cell (Cells) `home.lockCell`
- ✅ Format Cells (Cells) `home.formatCells`
- ✅ AutoSum (Editing) `formulas.autoSum`
- ✅ Fill Down (Editing) `edit.fillDown`
- ✅ Fill Right (Editing) `edit.fillRight`
- ✅ Fill Up (Editing) `edit.fillUp`
- ✅ Fill Left (Editing) `edit.fillLeft`
- ✅ Series (Editing) `edit.fillSeries`
- ✅ Flash Fill (Editing) `edit.flashFill`
- ✅ Clear All (Editing) `edit.clearAll`
- ✅ Clear Formats (Editing) `edit.clearFormats`
- ✅ Clear Contents (Editing) `edit.clearContents`
- ✅ Clear Comments (Editing) `edit.clearComments`
- ✅ Clear Hyperlinks (Editing) `edit.clearHyperlinks`
- ✅ Sort A to Z (Editing) `data.sortAscending`
- ✅ Sort Z to A (Editing) `data.sortDescending`
- ✅ Custom Sort (Editing) `data.sort`
- ✅ Filter (Editing) `data.filter`
- ✅ Clear Filter (Editing) `data.clearFilter`
- ✅ Reapply (Editing) `data.reapply`
- ✅ Find (Editing) `edit.find`
- ✅ Replace (Editing) `edit.replace`
- ✅ Go To (Editing) `edit.goTo`
- ✅ Go To Special (Editing) `edit.goToSpecial`
- ➖ Add-ins (Add-ins) `home.addIns`

New Cell Style keeps the active cell's look under a name in Home > My Styles while Ledger runs (not saved in the file). Format as Table, Flash Fill and Protect Sheet are the table and data work's. Lock Cell (in the Format list and the Format menu) locks the selected cells, or unlocks them when the active cell is locked; a protected sheet refuses it unless Format cells is allowed. Orientation sets and saves the angle; PDF export draws the text at its angle, the screen shows it upright. Draw Border (in the Borders list) draws on the next cells dragged over, one stroke at a time. Add-ins: Ledger has none.

### Insert (10 of 17 by name)

- ✅ PivotTable (Tables) `insert.pivotTable`
- ✅ Recommended PivotTables (Tables) `insert.recommendedPivotTables`
- ✅ Table (Tables) `insert.table`
- ✅ Pictures (Illustrations) `insert.picture`
- ✅ Shapes (Illustrations) `insert.shape`
- ✅ Icons (Illustrations) `insert.icons`
- ✅ Recommended Charts (Charts) `insert.recommendedCharts`
- ✅ Charts (Charts) `insert.chart`
- ✅ PivotChart (Charts) `insert.pivotChart`
- ✅ Sparklines (Sparklines) `insert.sparkline`
- ✅ Slicer (Filters) `insert.slicer`
- ✅ Link (Links) `insert.link`
- ✅ Comment (Comments) `review.newComment`
- ✅ Text Box (Text) `insert.textBox`
- ✅ WordArt (Text) `insert.wordArt` (new in GridCraft, there "Decorative Text"; 2026-11-18)
- ✅ Header & Footer (Text) `pageLayout.headerFooter`
- ⬜ Equation (Symbols) `insert.equation` (new in GridCraft: a LaTeX editor, Office Math in .xlsx; Ledger shows an equation's fallback text only)
- ✅ Symbol (Symbols) `insert.symbol`
- ✅ Checkbox (Cell Controls) `insert.checkbox`

WordArt (Insert > Text > WordArt, and the Insert menu) puts large decorative text at the active cell in one of GridCraft's ten styles (its own palette: text colour, outline, shadow, bold or italic, size); its text is then typed in and edited as a text box's. It is not inserted on a protected sheet unless objects may be edited. It is saved as GridCraft saves it, a text box whose text body has the plain text warp textNoShape, with the run's outline (a:ln) and shadow (outerShdw), and read back as WordArt. GridCraft's Format Chart Area pane got the stacked and 100% stacked chart types (#2605); Ledger's Format Chart dialog now has a Chart type list with every kind, the chart's own shown.

Checkbox makes the selected cells checkboxes, as GridCraft and Excel do: the cell holds TRUE or FALSE and shows a box (empty cells start FALSE; a formula's TRUE/FALSE shows too); a click on the cell or Space ticks it. It is kept as the cell format "checkbox" (GridCraft's way) and saved in .xlsx as Excel saves it: General plus the xf's feature property bag extension (featurePropertyBag.xml, [MS-XLSX] 2.1.22), read back from Excel's files.

### Draw (4 of 4 by name)

- ✅ Pen (Tools) `draw.pen`
- ✅ Eraser (Tools) `draw.eraser`
- ✅ Lasso Select (Tools) `draw.lasso`
- ✅ Ink to Shape (Convert) `draw.inkToShape`

### Page Layout (21 of 23 by name)

- ✅ Themes (Themes) `pageLayout.theme`
- ✅ Colors (Themes) `pageLayout.themeColors`
- ✅ Fonts (Themes) `pageLayout.themeFonts`
- ➖ Effects (Themes) `pageLayout.themeEffects`
- ✅ Margins (Page Setup) `pageLayout.margins`
- ✅ Orientation (Page Setup) `pageLayout.orientation`
- ✅ Size (Page Setup) `pageLayout.size`
- ✅ Print Area (Page Setup) `pageLayout.printArea`
- ✅ Breaks (Page Setup) `pageLayout.breaks`
- ✅ Background (Page Setup) `pageLayout.background`
- ✅ Print Titles (Page Setup) `pageLayout.printTitles`
- ✅ Page Setup (Page Setup) `pageLayout.pageSetup`
- ✅ Scale to Fit (Scale to Fit) `pageLayout.scaleToFit`
- ✅ View Gridlines (Sheet Options) `view.gridlines`
- ✅ Print Gridlines (Sheet Options) `pageLayout.printGridlines`
- ✅ View Headings (Sheet Options) `view.headings`
- ✅ Print Headings (Sheet Options) `pageLayout.printHeadings`
- ✅ Bring Forward (Arrange) `arrange.bringForward`
- ✅ Send Backward (Arrange) `arrange.sendBackward`
- ✅ Selection Pane (Arrange) `arrange.selectionPane`
- ✅ Align (Arrange) `arrange.align`
- ✅ Group (Arrange) `arrange.group`
- ✅ Rotate (Arrange) `arrange.rotate`

Not done on Page Layout: Effects only matter to charts and shapes (GridCraft keeps just the name), so they are left out here. Group (with Regroup and Ungroup, also in the Insert menu's Group Objects) makes real group shapes: the objects move, stretch and are deleted together, groups nest, and they are saved and loaded as xdr:grpSp.

### Formulas (13 of 23 by name)

- ✅ Insert Function (Function Library) `formulas.insertFunction`
- ✅ Recently Used (Function Library) `formulas.recentlyUsed`
- ✅ Financial (Function Library) `formulas.financial`
- ✅ Logical (Function Library) `formulas.logical`
- ✅ Text (Function Library) `formulas.text`
- ✅ Date & Time (Function Library) `formulas.dateTime`
- ✅ Lookup & Reference (Function Library) `formulas.lookupReference`
- ✅ Math & Trig (Function Library) `formulas.mathTrig`
- ✅ More Functions (Function Library) `formulas.moreFunctions`
- ✅ Name Manager (Defined Names) `formulas.nameManager`
- ✅ Define Name (Defined Names) `formulas.defineName`
- ✅ Use in Formula (Defined Names) `formulas.useInFormula`
- ✅ Create from Selection (Defined Names) `formulas.createFromSelection`
- ✅ Trace Precedents (Formula Auditing) `formulas.tracePrecedents`
- ✅ Trace Dependents (Formula Auditing) `formulas.traceDependents`
- ✅ Remove Arrows (Formula Auditing) `formulas.removeArrows`
- ✅ Show Formulas (Formula Auditing) `formulas.showFormulas`
- ✅ Error Checking (Formula Auditing) `formulas.errorChecking`
- ✅ Evaluate Formula (Formula Auditing) `formulas.evaluateFormula`
- ✅ Watch Window (Formula Auditing) `formulas.watchWindow`
- ✅ Calculation Options (Calculation) `formulas.calculationOptions`
- ✅ Calculate Now (Calculation) `formulas.calculateNow`
- ✅ Calculate Sheet (Calculation) `formulas.calculateSheet`

### Data (20 of 23 by name; 3 not applicable)

- ✅ Get Data (Get & Transform Data) `data.getData`
- ✅ From Text/CSV (Get & Transform Data) `data.fromTextCsv`
- ➖ From Table/Range (Get & Transform Data) `data.fromTableRange` (Ledger has no tables, queries or pivot tables to refresh)
- ➖ Refresh All (Queries & Connections) `data.refreshAll` (Ledger has no tables, queries or pivot tables to refresh)
- ➖ Queries & Connections (Queries & Connections) `data.queriesConnections` (Ledger has no tables, queries or pivot tables to refresh)
- ✅ Advanced (Sort & Filter) `data.advancedFilter`
- ✅ Text to Columns (Data Tools) `data.textToColumns`
- ✅ Flash Fill (Data Tools) `data.flashFill`
- ✅ Remove Duplicates (Data Tools) `data.removeDuplicates`
- ✅ Data Validation (Data Tools) `data.validation`
- ✅ Circle Invalid Data (Data Tools) `data.circleInvalid`
- ✅ Consolidate (Data Tools) `data.consolidate`
- ✅ Scenario Manager (Forecast) `data.scenarioManager`
- ✅ Goal Seek (Forecast) `data.goalSeek`
- ✅ Data Table (Forecast) `data.dataTable`
- ✅ Forecast Sheet (Forecast) `data.forecastSheet`
- ✅ Group (Outline) `data.group`
- ✅ Ungroup (Outline) `data.ungroup`
- ✅ Subtotal (Outline) `data.subtotal`
- ✅ Show Detail (Outline) `data.showDetail`
- ✅ Hide Detail (Outline) `data.hideDetail`
- ✅ Solver (Analysis) `data.solver`
- ✅ Data Analysis (Analysis) `data.analysisToolPak`

Forecast Sheet makes a new sheet before the data's: the timeline and values (sorted), FORECAST.ETS and the FORECAST.ETS.CONFINT bounds as live formulas, optional FORECAST.ETS.STAT statistics, a table and a line (or column) chart; the dialog sets the end, seasonality (automatic, none, a length), the confidence interval, how missing points are filled and duplicates aggregated. Solver has GridCraft's three methods: Simplex LP (linearised and checked, a two-phase simplex, branch and bound for int, bin and dif), GRG Nonlinear (an augmented Lagrangian with projected gradient steps, and Multistart from random points) and Evolutionary; Solver Results keeps or restores the solution and writes Answer and Limits reports; the model is saved in the sheet's hidden solver_ names as Excel saves it. Data Analysis has all nineteen of Excel's tools (GridCraft's thirteen, plus two-factor Anova with and without replication, Fourier Analysis, Random Number Generation, Sampling and the z-Test), writing values to an output range (asking before replacing data) or a new sheet.

### Review (15 of 15 by name)

- ✅ Spelling (Proofing) `review.spelling`
- ✅ Thesaurus (Proofing) `review.thesaurus`
- ✅ Workbook Statistics (Proofing) `review.workbookStatistics`
- ✅ Check Accessibility (Accessibility) `review.checkAccessibility`
- ✅ Delete Comment (Comments) `review.deleteComment`
- ✅ Previous Comment (Comments) `review.previousComment`
- ✅ Next Comment (Comments) `review.nextComment`
- ✅ Show Comments (Comments) `review.showComments`
- ✅ New Note (Notes) `review.newNote`
- ✅ Show/Hide Note (Notes) `review.showNote`
- ✅ Show All Notes (Notes) `review.showAllNotes`
- ✅ Convert to Comments (Notes) `review.convertToComments`
- ✅ Unprotect Sheet (Protect) `review.unprotectSheet`
- ✅ Protect Workbook (Protect) `review.protectWorkbook`
- ✅ Allow Edit Ranges (Protect) `review.allowEditRanges`

The Protect group's Unprotect Sheet and Protect Workbook are the data work's. Allow Edit Ranges keeps ranges (with a password or none) that stay editable on a protected sheet; a password range asks for its password once; they are saved as <protectedRanges> in .xlsx. Thesaurus offers words sharing the word's stem in the dictionary, as GridCraft's does; Check Accessibility makes GridCraft's checks (merged cells, default sheet names).

### View (13 of 20 by name)

- ✅ Normal (Workbook Views) `view.normal`
- ✅ Page Break Preview (Workbook Views) `view.pageBreakPreview`
- ✅ Page Layout (Workbook Views) `view.pageLayout`
- ✅ Custom Views (Workbook Views) `view.customViews`
- ✅ Ruler (Show) `view.ruler`
- ✅ Formula Bar (Show) `view.formulaBar`
- ✅ Zoom (Zoom) `view.zoom`
- ✅ 100% (Zoom) `view.zoom100`
- ✅ Zoom to Selection (Zoom) `view.zoomToSelection`
- ✅ New Window (Window) `view.newWindow`
- ✅ Arrange All (Window) `view.arrangeAll`
- ✅ Freeze Panes (Window) `view.freezePanes`
- ✅ Freeze Top Row (Window) `view.freezeTopRow`
- ✅ Freeze First Column (Window) `view.freezeFirstColumn`
- ✅ Unfreeze Panes (Window) `view.unfreezePanes`
- ✅ Split (Window) `view.split`
- ✅ Hide (Window) `view.hideWindow`
- ✅ Unhide (Window) `view.unhideWindow`
- ✅ Switch Windows (Window) `view.switchWindows`
- ➖ Macros (Macros) `view.macros`
- ➖ Record Macro (Macros) `macro.record` (new in GridCraft; scripting is left out)

Split makes two or four panes that scroll on their own, at the active cell (or the middle); the bars can be dragged, and go when double-clicked, dragged to an edge or Split is pressed again; saved and loaded as Excel does (pane state="split"). Custom Views are saved in the file (customWorkbookViews / customSheetViews). Macros: left out, as is scripting.

### Automate (0 of 4 by name)

- ➖ New Script (Scripting Tools) (scripting left out on purpose, as for Quill) `automate.newScript`
- ➖ All Scripts (Scripting Tools) (scripting left out on purpose, as for Quill) `automate.allScripts`
- ➖ Record Actions (Scripting Tools) (scripting left out on purpose, as for Quill) `automate.recordActions`
- ➖ Trust Scripts (Scripting Tools) (scripting left out on purpose, as for Quill) `automate.trustScripts`

### Developer (0 of 4; new in GridCraft)

- ➖ Button (Form Control) (Controls) `insert.button` (it runs a macro or command: scripting is left out)
- ⬜ Spin Button (Form Control) (Controls) `insert.spinButton`
- ⬜ Scroll Bar (Form Control) (Controls) `insert.slider`
- ⬜ Properties (Controls) `control.format`

GridCraft's form controls are floating objects linked to a cell (a spin button or scroll bar sets its number); Ledger's checkbox cells are the cell form of the same idea. The spin button and scroll bar could come without scripting.

### Table Design (2 of 13 by name)

- ✅ Table Name (Properties) `table.rename`
- ✅ Resize Table (Properties) `table.resize`
- ✅ Summarize with PivotTable (Tools) `table.summarizePivot`
- ✅ Remove Duplicates (Tools) `table.removeDuplicates`
- ✅ Convert to Range (Tools) `table.convertToRange`
- ✅ Insert Slicer (Tools) `table.insertSlicer`
- ✅ Header Row (Table Style Options) `table.headerRow`
- ✅ Total Row (Table Style Options) `table.totalRow`
- ✅ Banded Rows (Table Style Options) `table.bandedRows`
- ✅ First Column (Table Style Options) `table.firstColumn`
- ✅ Last Column (Table Style Options) `table.lastColumn`
- ✅ Banded Columns (Table Style Options) `table.bandedColumns`
- ✅ Table Styles (Table Styles) `table.style`

### Chart Design (8 of 8 by name)

- ✅ Add Chart Element (Chart Layouts) `chart.addElement`
- ✅ Quick Layout (Chart Layouts) `chart.quickLayout`
- ✅ Change Colors (Chart Styles) `chart.changeColors`
- ✅ Chart Styles (Chart Styles) `chart.set`
- ✅ Switch Row/Column (Data) `chart.switchRowColumn`
- ✅ Select Data (Data) `chart.selectData`
- ✅ Change Chart Type (Type) `chart.changeType`
- ✅ Move Chart (Location) `chart.move`

### Format (3 of 3 by name)

- ✅ Format Selection (Current Selection) `chart.formatSelection`
- ✅ Shape Fill (Shape Styles) `chart.shapeFill`
- ✅ Shape Outline (Shape Styles) `chart.shapeOutline`

### PivotTable Analyze (3 of 13 by name)

- ✅ Field List (Show) `pivot.fieldList`
- ✅ Refresh (Data) `pivot.refresh`
- ✅ Change Data Source (Data) `pivot.changeSource`
- ✅ Fields, Items & Sets (Calculations) `pivot.calculatedField`
- ✅ Add Field (Fields) `pivot.addField`
- ✅ Remove Field (Fields) `pivot.removeField`
- ✅ Move Field (Fields) `pivot.moveField`
- ✅ Value Field Settings (Active Field) `pivot.valueSettings`
- ✅ Filter (Filter) `pivot.filter`
- ✅ Sort (Sort) `pivot.sort`
- ✅ Group Field (Group) `pivot.group`
- ✅ Collapse Field (Active Field) `pivot.collapse`
- ✅ Clear All (Actions) `pivot.delete`

Recommended PivotTables offers GridCraft's suggestions (a category by a number, two categories, a date by quarters, two numbers, a count, an average) with a preview of each report, and puts the one chosen on a new sheet. A PivotChart is a chart bound to its pivot table: its series are the report's inner (or collapsed) items, made again whenever the report is, and saved with Excel's pivotSource. Slicers float over the sheet listing a field's items as buttons: a click filters a table through its AutoFilter or a pivot table through a filter of its own; Multi-Select and Clear Filter sit in the header; Slicer Settings sets the name, caption, columns, order and header; they are saved as Excel's slicer and slicerCache parts. Fields, Items & Sets makes calculated fields (formulas over the other fields' sums); Group Field groups dates by years, quarters, months and days, numbers by intervals, and items by hand (Ungroup takes it away); Expand and Collapse Field and the +/- buttons on outer items hide or show their detail; Show Values As has running total and rank as GridCraft has them. All of it is saved in .xlsx as Excel writes it.

### PivotTable Design (0 of 4 by name)

- ✅ Grand Totals (Layout) `pivot.grandTotals`
- ✅ Subtotals (Layout) `pivot.subtotals`
- ✅ Report Layout (Layout) `pivot.reportLayout`
- ✅ PivotTable Styles (PivotTable Styles) `pivot.style`


## GridCraft's changes from 2026-10-27 to 4c388df (2026-11-18)

123 merged pull requests (the sync report): 3 new features, 79 fixes, the rest other work. What applies to Ledger, and what became of it:

- ✅ Insert > WordArt (#784), Format Chart Area's stacked and 100% stacked types (#2605), GETPIVOTDATA (#284), ECMA.CEILING, the function fixes listed under Functions.
- ✅ Flush a saved workbook to disk before replacing the old one (#3178): Ledger wrote over the old file in place; workbooks, CSV and PDF now go to Ledger.tmp in the same drawer first and are renamed over the old file once written and closed.
- ✅ Set precision as displayed (#1366): Ledger Options, Formulas; calcPr fullPrecision="0".
- ✅ A hidden Total Row keeps its calculations (#4383): Ledger dropped them on saving.
- ✅ Google Sheets' DUMMYFUNCTION wrapper opened as the formula (#2871).
- ✅ Fill Series keeps its stop value despite float drift.
- ✅ (found while checking the above) a typed number is the nearest double, as a formula's literal: 0.3 typed in a cell was an ulp off, so MATCH(0.3,A1,0) failed.
- Already as GridCraft now has it: Page Setup's first page number (#4382), R1C1 reference style kept (#2809), AutoSum stopping at a blank (#3291), Subtotal's outline with every row under the grand total (#3945), HYPERLINK without a friendly name.
- ⬜ Not yet: Equations (Insert > Equation, #471), Python cells kept with their results (#2637, no code runs), form controls (#481; buttons are scripting), SYLK (.slk) files (#1481), a web page's table opened as a sheet (#3166), Find and Replace within the selection (#3224), CSV date order detected per column (#2765), custom-list sort (Ledger has no custom lists), Arabic right-to-left cells (#947), Solver's Load/Save Model, Options and Sensitivity report (#3086, #3087, #501).
- ➖ Not for Ledger: the installers, Windows/Linux/macOS/Flatpak packaging and window decorations, the web host and page API, update checks, telemetry, the MCP/control server, Python code running, macros and Record Macro, the command palette and its shortcuts, egui interface work, Rust lint and CI work, performance work on GridCraft's own engine (Ledger's calculation is its own), the shared Craft libraries' cutovers.
