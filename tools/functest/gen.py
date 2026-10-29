import openpyxl, re, json, sys, os
sys.path.insert(0, sys.argv[1])
from cases import C, R
# functions Excel only takes ranges for (it won't accept array constants there)
RANGE_ONLY = {'ROW','COLUMN','COUNTIF','COUNTIFS','SUMIF','SUMIFS','AVERAGEIF','AVERAGEIFS','MAXIFS','MINIFS','OFFSET','COUNTBLANK'}
XLFN = {'CEILING.MATH','CONCAT','DAYS','FLOOR.MATH','IFNA','IFS','ISFORMULA','MAXIFS','MINIFS','MODE.SNGL','NUMBERVALUE','PERCENTILE.EXC','PERCENTILE.INC','QUARTILE.INC','RANK.EQ','STDEV.P','STDEV.S','SWITCH','TEXTJOIN','UNICHAR','VAR.P','VAR.S','XLOOKUP','XOR'}
# cases_array.py: its functions' _xlfn. prefixes, and the sheet of spilling cases
try:
    from cases_array import XLFN as _xa
    XLFN = XLFN | _xa
except ImportError:
    pass
from openpyxl.worksheet.formula import ArrayFormula
BH, BW = 10, 9         # a spilling case's block on sheet S: rows, columns for each of its two forms
wb = openpyxl.Workbook(); T = wb.active; T.title = 'T'; D = wb.create_sheet('Data'); S = None; srow = 1
num = re.compile(r'^-?\d+(\.\d+)?$')
def put(ws, cell, frag):
    if frag == '': return
    if isinstance(frag, (int, float)): ws[cell] = frag; return
    if num.match(frag): ws[cell] = float(frag) if '.' in frag else int(frag)
    elif frag in ('TRUE', 'FALSE'): ws[cell] = frag == 'TRUE'
    elif re.match(r'^"([^"]|"")*"$', frag): ws[cell] = frag[1:-1].replace('""', '"')
    else: ws[cell] = '=' + frag
def lit(v):
    if isinstance(v, (int, float)): return str(v)
    return v if v != '' else '""'
drow = 1
meta = []
row = 1
for case in C:
    opts = {}
    if isinstance(case[-1], dict): opts = case[-1]; case = case[:-1]
    if len(case) == 3: name, _, args = case; refonly = True
    else: name, args = case; refonly = False
    refonly = refonly or name in RANGE_ONLY
    la, ra = [], []
    col = 5
    if opts.get('spill'):
        # a spilling case: the two forms as dynamic arrays (cm="1", added below) in a block of sheet S;
        # LibreOffice sees each as a {=...} formula over the whole block
        if S is None: S = wb.create_sheet('S')
        scol = 22
        for a in args:
            if isinstance(a, list):
                rows = [x if isinstance(x, list) else [x] for x in a]
                la.append('{' + ';'.join(','.join(lit(v) for v in r) for r in rows) + '}')
                for i, r in enumerate(rows):
                    for j, v in enumerate(r): put(D, D.cell(drow + i, 1 + j).coordinate, v)
                ra.append('Data!%s:%s' % (D.cell(drow, 1).coordinate, D.cell(drow + len(rows) - 1, len(rows[0])).coordinate))
                drow += len(rows) + 1
            else:
                la.append(a)
                c = S.cell(srow, scol).coordinate
                put(S, c, a)
                ra.append(c)
                scol += 1
        fn = ('_xlfn.' if name in XLFN else '') + name
        S.cell(srow, 1, name)
        lf = '=%s(%s)' % (fn, ','.join(la)) if not refonly else None
        rf = '=%s(%s)' % (fn, ','.join(ra)) if not opts.get('nocells') else None
        for f, c0 in ((lf, 2), (rf, 3 + BW)):
            if f:
                a1 = S.cell(srow, c0).coordinate
                S[a1] = ArrayFormula('%s:%s' % (a1, S.cell(srow + BH - 1, c0 + BW - 1).coordinate), f)
        o = dict(opts); o['sheet'] = 'S'
        meta.append((srow, name, lf, rf, o))
        srow += BH + 1
        continue
    for a in args:
        if isinstance(a, list):
            rows = [x if isinstance(x, list) else [x] for x in a]
            la.append('{' + ';'.join(','.join(lit(v) for v in r) for r in rows) + '}')
            for i, r in enumerate(rows):
                for j, v in enumerate(r): put(D, D.cell(drow + i, 1 + j).coordinate, v)
            ra.append('Data!%s:%s' % (D.cell(drow, 1).coordinate, D.cell(drow + len(rows) - 1, len(rows[0])).coordinate))
            drow += len(rows) + 1
        else:
            la.append(a)
            c = T.cell(row, col).coordinate
            put(T, c, a)
            ra.append(c if a != '' else c)
            col += 1
    T.cell(row, 1, name)
    fn = ('_xlfn.' if name in XLFN else '') + name
    if not refonly: T.cell(row, 2, '=%s(%s)' % (fn, ','.join(la)))
    if not opts.get('nocells'): T.cell(row, 3, '=%s(%s)' % (fn, ','.join(ra)))
    meta.append((row, name, T.cell(row, 2).value, T.cell(row, 3).value, opts) if opts else (row, name, T.cell(row, 2).value, T.cell(row, 3).value))
    row += 1
wb.save(sys.argv[2])
if S is not None:
    # mark sheet S's array formulas as dynamic arrays, the way Excel writes them
    import zipfile, shutil
    src = sys.argv[2] + '.tmp'
    shutil.move(sys.argv[2], src)
    zi = zipfile.ZipFile(src); zo = zipfile.ZipFile(sys.argv[2], 'w', zipfile.ZIP_DEFLATED)
    sheet_s = 'xl/worksheets/sheet%d.xml' % (wb.sheetnames.index('S') + 1)
    md = ('<?xml version="1.0" encoding="UTF-8" standalone="yes"?>\n<metadata xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main" '
          'xmlns:xda="http://schemas.microsoft.com/office/spreadsheetml/2017/dynamicarray"><metadataTypes count="1"><metadataType name="XLDAPR" '
          'minSupportedVersion="120000" copy="1" pasteAll="1" pasteValues="1" merge="1" splitFirst="1" rowColShift="1" clearFormats="1" '
          'clearComments="1" assign="1" coerce="1" cellMeta="1"/></metadataTypes><futureMetadata name="XLDAPR" count="1"><bk><extLst>'
          '<ext uri="{bdbb8cdc-fa1e-496e-a857-3c3f30c029c3}"><xda:dynamicArrayProperties fDynamic="1" fCollapsed="0"/></ext></extLst></bk>'
          '</futureMetadata><cellMetadata count="1"><bk><rc t="1" v="0"/></bk></cellMetadata></metadata>')
    for it in zi.infolist():
        d = zi.read(it.filename)
        if it.filename == sheet_s:
            d = re.sub(rb'<c r="([A-Z]+[0-9]+)"([^>]*)><f t="array"', rb'<c r="\1"\2 cm="1"><f t="array"', d)
        elif it.filename == '[Content_Types].xml':
            d = d.replace(b'</Types>', b'<Override PartName="/xl/metadata.xml" ContentType="application/vnd.openxmlformats-officedocument.spreadsheetml.sheetMetadata+xml"/></Types>')
        elif it.filename == 'xl/_rels/workbook.xml.rels':
            d = d.replace(b'</Relationships>', b'<Relationship Id="rIdMd1" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/sheetMetadata" Target="metadata.xml"/></Relationships>')
        zo.writestr(it, d)
    zo.writestr('xl/metadata.xml', md)
    zo.close()
json.dump(meta, open(sys.argv[3], 'w'))
print(len(meta), 'cases,', len({m[1] for m in meta}), 'functions')
