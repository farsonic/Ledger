# Ledger against GridCraft: what is missing

GridCraft (github.com/storytold/sheetcraft, the ArtCraft spreadsheet) is the program Ledger follows. This list was made on 2026-10-29 from GridCraft's own source (its function registry) and its ribbon command catalogue (docs/gridcraft/parity-checklist.md, copied here with function-parity.md and gaps.md). It is the work list for closing the gaps; FEATURES.md is ticked off as features land.

Left out on purpose, as in Quill: printing (PDF export is in) and ARexx/MCP scripting.

## Functions

GridCraft has 513 worksheet functions (509 in its registry and special forms, plus RAND, RANDBETWEEN, NOW and TODAY); Ledger has 187. Missing: 326.

- **Statistical (79):** BETA.DIST, BETA.INV, BINOM.DIST, BINOM.DIST.RANGE, BINOM.INV, CHISQ.DIST, CHISQ.DIST.RT, CHISQ.INV, CHISQ.INV.RT, CHISQ.TEST, CONFIDENCE.NORM, CONFIDENCE.T, COVARIANCE.P, COVARIANCE.S, DEVSQ, EXPON.DIST, F.DIST, F.DIST.RT, F.INV, F.INV.RT, F.TEST, FISHER, FISHERINV, FORECAST.ETS, FORECAST.ETS.CONFINT, FORECAST.ETS.SEASONALITY, FORECAST.ETS.STAT, FORECAST.LINEAR, FREQUENCY, GAMMA, GAMMA.DIST, GAMMA.INV, GAMMALN, GAMMALN.PRECISE, GAUSS, GEOMEAN, GROWTH, HARMEAN, HYPGEOM.DIST, INTERCEPT, KURT, LINEST, LOGEST, LOGNORM.DIST, LOGNORM.INV, MODE.MULT, NEGBINOM.DIST, NORM.DIST, NORM.INV, NORM.S.DIST, NORM.S.INV, PEARSON, PERCENTRANK.EXC, PERCENTRANK.INC, PERMUTATIONA, PHI, POISSON.DIST, PROB, QUARTILE.EXC, RANK.AVG, RSQ, SKEW, SKEW.P, SLOPE, STANDARDIZE, STDEVPA, STEYX, T.DIST, T.DIST.2T, T.DIST.RT, T.INV, T.INV.2T, T.TEST, TREND, TRIMMEAN, VARA, VARPA, WEIBULL.DIST, Z.TEST
- **Engineering (54):** BESSELI, BESSELJ, BESSELK, BESSELY, BIN2DEC, BIN2HEX, BIN2OCT, BITAND, BITLSHIFT, BITOR, BITRSHIFT, BITXOR, COMPLEX, CONVERT, DEC2BIN, DEC2HEX, DEC2OCT, DELTA, ERF, ERF.PRECISE, ERFC, ERFC.PRECISE, GESTEP, HEX2BIN, HEX2DEC, HEX2OCT, IMABS, IMAGINARY, IMARGUMENT, IMCONJUGATE, IMCOS, IMCOSH, IMCOT, IMCSC, IMCSCH, IMDIV, IMEXP, IMLN, IMLOG10, IMLOG2, IMPOWER, IMPRODUCT, IMREAL, IMSEC, IMSECH, IMSIN, IMSINH, IMSQRT, IMSUB, IMSUM, IMTAN, OCT2BIN, OCT2DEC, OCT2HEX
- **Financial (45):** ACCRINT, ACCRINTM, AMORDEGRC, AMORLINC, COUPDAYBS, COUPDAYS, COUPDAYSNC, COUPNCD, COUPNUM, COUPPCD, CUMIPMT, CUMPRINC, DB, DDB, DISC, DOLLARDE, DOLLARFR, DURATION, EFFECT, FVSCHEDULE, INTRATE, ISPMT, MDURATION, MIRR, NOMINAL, ODDFPRICE, ODDFYIELD, ODDLPRICE, ODDLYIELD, PDURATION, PRICE, PRICEDISC, PRICEMAT, RECEIVED, RRI, SYD, TBILLEQ, TBILLPRICE, TBILLYIELD, VDB, XIRR, XNPV, YIELD, YIELDDISC, YIELDMAT
- **MathTrig (32):** ACOSH, ACOT, ACOTH, ARABIC, ASINH, ATANH, BASE, CEILING.PRECISE, COMBINA, COT, COTH, CSC, CSCH, DECIMAL, FACTDOUBLE, FLOOR.PRECISE, ISO.CEILING, MDETERM, MINVERSE, MMULT, MULTINOMIAL, MUNIT, PERCENTOF, ROMAN, SEC, SECH, SEQUENCE, SERIESSUM, SQRTPI, SUMX2MY2, SUMX2PY2, SUMXMY2
- **Compatibility (32):** BETADIST, BETAINV, BINOMDIST, CHIDIST, CHIINV, CHITEST, CONFIDENCE, COVAR, CRITBINOM, EXPONDIST, FDIST, FINV, FORECAST, FTEST, GAMMADIST, GAMMAINV, HYPGEOMDIST, LOGINV, LOGNORMDIST, NEGBINOMDIST, NORMDIST, NORMINV, NORMSDIST, NORMSINV, PERCENTRANK, POISSON, TDIST, TINV, TTEST, USDOLLAR, WEIBULL, ZTEST
- **Text (21):** ARRAYTOTEXT, ASC, BAHTTEXT, DBCS, FINDB, JIS, LEFTB, LENB, MIDB, PHONETIC, REGEXEXTRACT, REGEXREPLACE, REGEXTEST, REPLACEB, RIGHTB, SEARCHB, TEXTAFTER, TEXTBEFORE, TEXTSPLIT, UNICODE, VALUETOTEXT
- **Lookup (18):** CHOOSECOLS, CHOOSEROWS, DROP, EXPAND, FILTER, GROUPBY, HSTACK, PIVOTBY, SORT, SORTBY, TAKE, TOCOL, TOROW, UNIQUE, VSTACK, WRAPCOLS, WRAPROWS, XMATCH
- **Special form (17):** AGGREGATE, AREAS, BYCOL, BYROW, CELL, FORMULATEXT, ISOMITTED, LAMBDA, LET, MAKEARRAY, MAP, REDUCE, SCAN, SHEET, SHEETS, SUBTOTAL, TRIMRANGE
- **Database (12):** DAVERAGE, DCOUNT, DCOUNTA, DGET, DMAX, DMIN, DPRODUCT, DSTDEV, DSTDEVP, DSUM, DVAR, DVARP
- **Cube (7):** CUBEKPIMEMBER, CUBEMEMBER, CUBEMEMBERPROPERTY, CUBERANKEDMEMBER, CUBESET, CUBESETCOUNT, CUBEVALUE
- **DateTime (4):** DAYS360, ISOWEEKNUM, NETWORKDAYS.INTL, WORKDAY.INTL
- **Web (3):** ENCODEURL, FILTERXML, WEBSERVICE
- **AddIn (1):** EUROCONVERT
- **Information (1):** INFO

## Ribbon commands

GridCraft implements 275 of the 291 Excel ribbon commands in its catalogue. Below, each of GridCraft's commands, tab by tab, with a first guess (by name) whether Ledger has it: ✅ probably has, ⬜ probably missing. Each is to be checked and the missing ones built.

### File (7 of 11 by name)

- ✅ New (File) `file.new`
- ✅ Open (File) `file.open`
- ✅ Save (File) `file.save`
- ✅ Save As (File) `file.saveAs`
- ✅ Close (File) `file.close`
- ⬜ Print (File) `file.print`
- ⬜ Export to PDF (Export) `file.exportPdf`
- ⬜ CSV (Export) `file.exportCsv`
- ⬜ Web Page (Export) `file.exportHtml`
- ✅ Properties (Info) `file.properties`
- ✅ Options (Options) `file.options`

### Home (67 of 94 by name)

- ✅ Paste (Clipboard) `edit.paste`
- ⬜ Paste Special (Clipboard) `edit.pasteSpecial`
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
- ⬜ Double Underline (Font) `home.doubleUnderline`
- ✅ Strikethrough (Font) `home.strikethrough`
- ⬜ Superscript (Font) `home.superscript`
- ⬜ Subscript (Font) `home.subscript`
- ✅ Borders (Font) `home.borders`
- ⬜ Draw Border (Font) `home.drawBorder`
- ⬜ Fill Color (Font) `home.fillColor`
- ⬜ Font Color (Font) `home.fontColor`
- ✅ Top Align (Alignment) `home.alignTop`
- ✅ Middle Align (Alignment) `home.alignMiddle`
- ✅ Bottom Align (Alignment) `home.alignBottom`
- ⬜ Orientation (Alignment) `home.orientation`
- ✅ Align Left (Alignment) `home.alignLeft`
- ⬜ Center (Alignment) `home.alignCenter`
- ✅ Align Right (Alignment) `home.alignRight`
- ✅ Decrease Indent (Alignment) `home.decreaseIndent`
- ✅ Increase Indent (Alignment) `home.increaseIndent`
- ✅ Wrap Text (Alignment) `home.wrapText`
- ⬜ Merge & Center (Alignment) `home.mergeCenter`
- ✅ Merge Across (Alignment) `home.mergeAcross`
- ✅ Merge Cells (Alignment) `home.mergeCells`
- ✅ Unmerge Cells (Alignment) `home.unmergeCells`
- ✅ Number Format (Number) `home.numberFormat`
- ⬜ Accounting Number Format (Number) `home.accounting`
- ✅ Percent Style (Number) `home.percent`
- ⬜ Comma Style (Number) `home.comma`
- ✅ Increase Decimal (Number) `home.increaseDecimal`
- ✅ Decrease Decimal (Number) `home.decreaseDecimal`
- ✅ Conditional Formatting (Styles) `home.conditionalFormat`
- ✅ Clear Rules (Styles) `home.clearRules`
- ✅ Manage Rules (Styles) `home.manageRules`
- ⬜ Format as Table (Styles) `home.formatAsTable`
- ✅ Cell Styles (Styles) `home.cellStyle`
- ⬜ New Cell Style (Styles) `home.newCellStyle`
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
- ⬜ Hide Sheet (Cells) `sheet.hide`
- ⬜ Unhide Sheet (Cells) `sheet.unhide`
- ✅ Rename Sheet (Cells) `sheet.rename`
- ⬜ Move or Copy Sheet (Cells) `sheet.move`
- ⬜ Tab Color (Cells) `sheet.tabColor`
- ⬜ Protect Sheet (Cells) `review.protectSheet`
- ⬜ Lock Cell (Cells) `home.lockCell`
- ✅ Format Cells (Cells) `home.formatCells`
- ✅ AutoSum (Editing) `formulas.autoSum`
- ✅ Fill Down (Editing) `edit.fillDown`
- ✅ Fill Right (Editing) `edit.fillRight`
- ⬜ Fill Up (Editing) `edit.fillUp`
- ⬜ Fill Left (Editing) `edit.fillLeft`
- ✅ Series (Editing) `edit.fillSeries`
- ⬜ Flash Fill (Editing) `edit.flashFill`
- ✅ Clear All (Editing) `edit.clearAll`
- ✅ Clear Formats (Editing) `edit.clearFormats`
- ✅ Clear Contents (Editing) `edit.clearContents`
- ⬜ Clear Comments (Editing) `edit.clearComments`
- ⬜ Clear Hyperlinks (Editing) `edit.clearHyperlinks`
- ✅ Sort A to Z (Editing) `data.sortAscending`
- ✅ Sort Z to A (Editing) `data.sortDescending`
- ✅ Custom Sort (Editing) `data.sort`
- ✅ Filter (Editing) `data.filter`
- ✅ Clear Filter (Editing) `data.clearFilter`
- ✅ Reapply (Editing) `data.reapply`
- ✅ Find (Editing) `edit.find`
- ✅ Replace (Editing) `edit.replace`
- ✅ Go To (Editing) `edit.goTo`
- ⬜ Go To Special (Editing) `edit.goToSpecial`
- ⬜ Add-ins (Add-ins) `home.addIns`

### Insert (9 of 17 by name)

- ⬜ PivotTable (Tables) `insert.pivotTable`
- ⬜ Recommended PivotTables (Tables) `insert.recommendedPivotTables`
- ⬜ Table (Tables) `insert.table`
- ✅ Pictures (Illustrations) `insert.picture`
- ✅ Shapes (Illustrations) `insert.shape`
- ✅ Icons (Illustrations) `insert.icons`
- ✅ Recommended Charts (Charts) `insert.recommendedCharts`
- ✅ Charts (Charts) `insert.chart`
- ⬜ PivotChart (Charts) `insert.pivotChart`
- ✅ Sparklines (Sparklines) `insert.sparkline`
- ⬜ Slicer (Filters) `insert.slicer`
- ✅ Link (Links) `insert.link`
- ⬜ Comment (Comments) `review.newComment`
- ✅ Text Box (Text) `insert.textBox`
- ⬜ Header & Footer (Text) `pageLayout.headerFooter`
- ✅ Symbol (Symbols) `insert.symbol`
- ⬜ Checkbox (Cell Controls) `insert.checkbox`

### Draw (4 of 4 by name)

- ✅ Pen (Tools) `draw.pen`
- ✅ Eraser (Tools) `draw.eraser`
- ✅ Lasso Select (Tools) `draw.lasso`
- ✅ Ink to Shape (Convert) `draw.inkToShape`

### Page Layout (1 of 23 by name)

- ⬜ Themes (Themes) `pageLayout.theme`
- ⬜ Colors (Themes) `pageLayout.themeColors`
- ✅ Fonts (Themes) `pageLayout.themeFonts`
- ⬜ Effects (Themes) `pageLayout.themeEffects`
- ⬜ Margins (Page Setup) `pageLayout.margins`
- ⬜ Orientation (Page Setup) `pageLayout.orientation`
- ⬜ Size (Page Setup) `pageLayout.size`
- ⬜ Print Area (Page Setup) `pageLayout.printArea`
- ⬜ Breaks (Page Setup) `pageLayout.breaks`
- ⬜ Background (Page Setup) `pageLayout.background`
- ⬜ Print Titles (Page Setup) `pageLayout.printTitles`
- ⬜ Page Setup (Page Setup) `pageLayout.pageSetup`
- ⬜ Scale to Fit (Scale to Fit) `pageLayout.scaleToFit`
- ⬜ View Gridlines (Sheet Options) `view.gridlines`
- ⬜ Print Gridlines (Sheet Options) `pageLayout.printGridlines`
- ⬜ View Headings (Sheet Options) `view.headings`
- ⬜ Print Headings (Sheet Options) `pageLayout.printHeadings`
- ⬜ Bring Forward (Arrange) `arrange.bringForward`
- ⬜ Send Backward (Arrange) `arrange.sendBackward`
- ⬜ Selection Pane (Arrange) `arrange.selectionPane`
- ⬜ Align (Arrange) `arrange.align`
- ⬜ Group (Arrange) `arrange.group`
- ⬜ Rotate (Arrange) `arrange.rotate`

### Formulas (13 of 23 by name)

- ✅ Insert Function (Function Library) `formulas.insertFunction`
- ⬜ Recently Used (Function Library) `formulas.recentlyUsed`
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
- ⬜ Create from Selection (Defined Names) `formulas.createFromSelection`
- ⬜ Trace Precedents (Formula Auditing) `formulas.tracePrecedents`
- ⬜ Trace Dependents (Formula Auditing) `formulas.traceDependents`
- ⬜ Remove Arrows (Formula Auditing) `formulas.removeArrows`
- ✅ Show Formulas (Formula Auditing) `formulas.showFormulas`
- ⬜ Error Checking (Formula Auditing) `formulas.errorChecking`
- ⬜ Evaluate Formula (Formula Auditing) `formulas.evaluateFormula`
- ⬜ Watch Window (Formula Auditing) `formulas.watchWindow`
- ⬜ Calculation Options (Calculation) `formulas.calculationOptions`
- ✅ Calculate Now (Calculation) `formulas.calculateNow`
- ⬜ Calculate Sheet (Calculation) `formulas.calculateSheet`

### Data (3 of 23 by name)

- ✅ Get Data (Get & Transform Data) `data.getData`
- ✅ From Text/CSV (Get & Transform Data) `data.fromTextCsv`
- ⬜ From Table/Range (Get & Transform Data) `data.fromTableRange`
- ⬜ Refresh All (Queries & Connections) `data.refreshAll`
- ⬜ Queries & Connections (Queries & Connections) `data.queriesConnections`
- ⬜ Advanced (Sort & Filter) `data.advancedFilter`
- ⬜ Text to Columns (Data Tools) `data.textToColumns`
- ⬜ Flash Fill (Data Tools) `data.flashFill`
- ✅ Remove Duplicates (Data Tools) `data.removeDuplicates`
- ⬜ Data Validation (Data Tools) `data.validation`
- ⬜ Circle Invalid Data (Data Tools) `data.circleInvalid`
- ⬜ Consolidate (Data Tools) `data.consolidate`
- ⬜ Scenario Manager (Forecast) `data.scenarioManager`
- ⬜ Goal Seek (Forecast) `data.goalSeek`
- ⬜ Data Table (Forecast) `data.dataTable`
- ⬜ Forecast Sheet (Forecast) `data.forecastSheet`
- ⬜ Group (Outline) `data.group`
- ⬜ Ungroup (Outline) `data.ungroup`
- ⬜ Subtotal (Outline) `data.subtotal`
- ⬜ Show Detail (Outline) `data.showDetail`
- ⬜ Hide Detail (Outline) `data.hideDetail`
- ⬜ Solver (Analysis) `data.solver`
- ⬜ Data Analysis (Analysis) `data.analysisToolPak`

### Review (3 of 15 by name)

- ⬜ Spelling (Proofing) `review.spelling`
- ⬜ Thesaurus (Proofing) `review.thesaurus`
- ✅ Workbook Statistics (Proofing) `review.workbookStatistics`
- ⬜ Check Accessibility (Accessibility) `review.checkAccessibility`
- ⬜ Delete Comment (Comments) `review.deleteComment`
- ⬜ Previous Comment (Comments) `review.previousComment`
- ⬜ Next Comment (Comments) `review.nextComment`
- ⬜ Show Comments (Comments) `review.showComments`
- ✅ New Note (Notes) `review.newNote`
- ⬜ Show/Hide Note (Notes) `review.showNote`
- ✅ Show All Notes (Notes) `review.showAllNotes`
- ⬜ Convert to Comments (Notes) `review.convertToComments`
- ⬜ Unprotect Sheet (Protect) `review.unprotectSheet`
- ⬜ Protect Workbook (Protect) `review.protectWorkbook`
- ⬜ Allow Edit Ranges (Protect) `review.allowEditRanges`

### View (13 of 20 by name)

- ✅ Normal (Workbook Views) `view.normal`
- ⬜ Page Break Preview (Workbook Views) `view.pageBreakPreview`
- ✅ Page Layout (Workbook Views) `view.pageLayout`
- ⬜ Custom Views (Workbook Views) `view.customViews`
- ⬜ Ruler (Show) `view.ruler`
- ✅ Formula Bar (Show) `view.formulaBar`
- ✅ Zoom (Zoom) `view.zoom`
- ⬜ 100% (Zoom) `view.zoom100`
- ✅ Zoom to Selection (Zoom) `view.zoomToSelection`
- ✅ New Window (Window) `view.newWindow`
- ⬜ Arrange All (Window) `view.arrangeAll`
- ✅ Freeze Panes (Window) `view.freezePanes`
- ✅ Freeze Top Row (Window) `view.freezeTopRow`
- ✅ Freeze First Column (Window) `view.freezeFirstColumn`
- ✅ Unfreeze Panes (Window) `view.unfreezePanes`
- ⬜ Split (Window) `view.split`
- ✅ Hide (Window) `view.hideWindow`
- ✅ Unhide (Window) `view.unhideWindow`
- ✅ Switch Windows (Window) `view.switchWindows`
- ⬜ Macros (Macros) `view.macros`

### Automate (0 of 4 by name)

- ⬜ New Script (Scripting Tools) `automate.newScript`
- ⬜ All Scripts (Scripting Tools) `automate.allScripts`
- ⬜ Record Actions (Scripting Tools) `automate.recordActions`
- ⬜ Trust Scripts (Scripting Tools) `automate.trustScripts`

### Table Design (2 of 13 by name)

- ⬜ Table Name (Properties) `table.rename`
- ⬜ Resize Table (Properties) `table.resize`
- ⬜ Summarize with PivotTable (Tools) `table.summarizePivot`
- ✅ Remove Duplicates (Tools) `table.removeDuplicates`
- ⬜ Convert to Range (Tools) `table.convertToRange`
- ⬜ Insert Slicer (Tools) `table.insertSlicer`
- ⬜ Header Row (Table Style Options) `table.headerRow`
- ⬜ Total Row (Table Style Options) `table.totalRow`
- ⬜ Banded Rows (Table Style Options) `table.bandedRows`
- ✅ First Column (Table Style Options) `table.firstColumn`
- ⬜ Last Column (Table Style Options) `table.lastColumn`
- ⬜ Banded Columns (Table Style Options) `table.bandedColumns`
- ⬜ Table Styles (Table Styles) `table.style`

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

- ⬜ Field List (Show) `pivot.fieldList`
- ⬜ Refresh (Data) `pivot.refresh`
- ⬜ Change Data Source (Data) `pivot.changeSource`
- ⬜ Fields, Items & Sets (Calculations) `pivot.calculatedField`
- ⬜ Add Field (Fields) `pivot.addField`
- ⬜ Remove Field (Fields) `pivot.removeField`
- ⬜ Move Field (Fields) `pivot.moveField`
- ⬜ Value Field Settings (Active Field) `pivot.valueSettings`
- ✅ Filter (Filter) `pivot.filter`
- ✅ Sort (Sort) `pivot.sort`
- ⬜ Group Field (Group) `pivot.group`
- ⬜ Collapse Field (Active Field) `pivot.collapse`
- ✅ Clear All (Actions) `pivot.delete`

### PivotTable Design (0 of 4 by name)

- ⬜ Grand Totals (Layout) `pivot.grandTotals`
- ⬜ Subtotals (Layout) `pivot.subtotals`
- ⬜ Report Layout (Layout) `pivot.reportLayout`
- ⬜ PivotTable Styles (PivotTable Styles) `pivot.style`

