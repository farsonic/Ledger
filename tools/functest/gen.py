import openpyxl, re, json, sys
sys.path.insert(0, sys.argv[1])
from cases import C, R
# functions Excel only takes ranges for (it won't accept array constants there)
RANGE_ONLY = {'ROW','COLUMN','COUNTIF','COUNTIFS','SUMIF','SUMIFS','AVERAGEIF','AVERAGEIFS','MAXIFS','MINIFS','OFFSET','COUNTBLANK'}
XLFN = {'CEILING.MATH','CONCAT','DAYS','FLOOR.MATH','IFNA','IFS','ISFORMULA','MAXIFS','MINIFS','MODE.SNGL','NUMBERVALUE','PERCENTILE.EXC','PERCENTILE.INC','QUARTILE.INC','RANK.EQ','STDEV.P','STDEV.S','SWITCH','TEXTJOIN','UNICHAR','VAR.P','VAR.S','XLOOKUP','XOR'}
# (the per-file case lists can name more: XLFN in cases_<m>.py)
import cases_stat, cases_eng, cases_fin, cases_misc, cases_array
for _m in (cases_stat, cases_eng, cases_fin, cases_misc, cases_array): XLFN |= set(getattr(_m, 'XLFN', ()))
# a case's name with brackets in it is a template: @ stands for the arguments, e.g. 'INDEX(LINEST(@),2,1)'
def call(fn, args): return fn.replace('@', args) if '(' in fn else '%s(%s)' % (fn, args)
# and every function whose table entry in core/func*.c is flagged FF_XLFN
import glob, os
for _f in glob.glob(os.path.join(sys.argv[1], '..', '..', 'core', 'func*.c')):
    XLFN |= set(re.findall(r'\{ "([A-Z0-9._]+)", \d+, \d+, [^,}]*FF_XLFN', open(_f).read()))
wb = openpyxl.Workbook(); T = wb.active; T.title = 'T'; D = wb.create_sheet('Data')
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
    if len(case) == 3: name, _, args = case; refonly = True
    else: name, args = case; refonly = False
    refonly = refonly or name in RANGE_ONLY
    la, ra = [], []
    col = 5
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
    if not refonly: T.cell(row, 2, '=' + call(fn, ','.join(la)))
    T.cell(row, 3, '=' + call(fn, ','.join(ra)))
    meta.append((row, name, T.cell(row, 2).value, T.cell(row, 3).value))
    row += 1
wb.save(sys.argv[2])
json.dump(meta, open(sys.argv[3], 'w'))
print(len(meta), 'cases,', len({m[1] for m in meta}), 'functions')
