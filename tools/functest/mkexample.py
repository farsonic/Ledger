#!/usr/bin/env python3
# mkexample.py - makes FunctionTests.xlsx, the example workbook that tests every Ledger function.
#
#   python3 tools/functest/mkexample.py out.xlsx [workdir]
#
# Each case of cases.py becomes a row (twice: with its arguments typed in, and with the same
# arguments in cells): the function, the formula as text, the live formula, the expected answer and
# a Check cell that says PASS or FAIL. A summary at the top counts them.
#
# The expected answers are validated before they are written: the workbook is worked out by
# Ledger's core (evalcols.c) and by LibreOffice, and every answer must agree, except the documented
# places where Ledger gives Excel's answer and LibreOffice doesn't (excel.txt); those rows carry a
# note with LibreOffice's answer. The finished workbook is then worked out again by both, and the
# script fails unless Ledger passes every row and LibreOffice passes every row but the noted ones.
# Needs python3 with openpyxl, LibreOffice (soffice) and a C compiler.
import json, os, re, subprocess, sys, datetime
import openpyxl
from openpyxl.styles import Font, PatternFill, Alignment
from openpyxl.formatting.rule import CellIsRule
from openpyxl.worksheet.formula import ArrayFormula

HERE = os.path.dirname(os.path.abspath(__file__))
TOP = os.path.normpath(os.path.join(HERE, '..', '..'))
sys.path.insert(0, HERE)
from cases import C
# (gen.py's two lists, read from it: importing gen.py would run it)
_g = open(os.path.join(HERE, 'gen.py')).read()
RANGE_ONLY = eval(re.search(r'^RANGE_ONLY = (\{.*\})$', _g, re.M).group(1))
XLFN = eval(re.search(r'^XLFN = (\{.*\})$', _g, re.M).group(1))
import cases_stat, cases_eng, cases_fin, cases_misc, cases_array
for _m in (cases_stat, cases_eng, cases_fin, cases_misc, cases_array): XLFN |= set(getattr(_m, 'XLFN', ()))
def call(fn, args): return fn.replace('@', args) if '(' in fn else '%s(%s)' % (fn, args)   # (as gen.py)
# and every function whose table entry in core/func*.c is flagged FF_XLFN (as gen.py)
import glob
for _f in glob.glob(os.path.join(TOP, 'core', 'func*.c')):
    XLFN |= set(re.findall(r'\{ "([A-Z0-9._]+)", \d+, \d+, [^,}]*FF_XLFN', open(_f).read()))

out = sys.argv[1]
work = sys.argv[2] if len(sys.argv) > 2 else os.path.join(os.environ.get('TMPDIR', '/tmp'), 'ledger-mkexample')
os.makedirs(work, exist_ok=True)

# the core's source files, as build.sh lists them
CORE = re.search(r'^CORE="([^"]*)"', open(os.path.join(TOP, "build.sh")).read(), re.M).group(1)
EVAL = os.path.join(work, 'evalcols')
subprocess.check_call('cc -std=c99 -D_DEFAULT_SOURCE -O1 -I. %s tools/functest/evalcols.c -o %s -lm' % (CORE, EVAL), shell=True, cwd=TOP)

FIRST = 7                                  # the first test row
COL_FN, COL_FORM, COL_TEXT, COL_RES, COL_EXP, COL_CHK, COL_NOTE, COL_ARG = 2, 3, 4, 5, 6, 7, 8, 10
num = re.compile(r'^-?\d+(\.\d+)?$')
VOLATILE = {
    # these can't have a fixed answer: each is tested by what must be true of it
    'NOW': ('=AND(ISNUMBER(NOW()),NOW()>45000)', None),
    'TODAY': ('=TODAY()=INT(NOW())', None),
    'RAND': ('=AND(RAND()>=0,RAND()<1)', None),
    'RANDBETWEEN': ('=ISNUMBER(MATCH(RANDBETWEEN(1,6),{1,2,3,4,5,6},0))', '=ISNUMBER(MATCH(RANDBETWEEN(%s,%s),{1,2,3,4,5,6},0))'),
}

def put(ws, cell, frag):
    """an argument fragment as a cell's content (a number, TRUE/FALSE, text, or a formula)"""
    if frag == '': return
    if isinstance(frag, (int, float)): ws[cell] = frag; return
    if num.match(frag): ws[cell] = float(frag) if '.' in frag else int(frag)
    elif frag in ('TRUE', 'FALSE'): ws[cell] = frag == 'TRUE'
    elif re.match(r'^"([^"]|"")*"$', frag):
        ws[cell] = frag[1:-1].replace('""', '"')
        if not ws[cell].value.startswith('='): ws[cell].data_type = 's'   # (text like ="=Apple" is a formula there, as gen.py writes it)
    else: ws[cell] = '=' + frag
def lit(v):
    if isinstance(v, (int, float)): return str(v)
    return v if v != '' else '""'
def text(ws, cell, s):
    ws[cell] = s; ws[cell].data_type = 's'

def build(expected=None, notes=None):
    """the workbook; expected: {row: 'N:..'/'S:..'/'B:..'/'E:..'} fills the Expected column"""
    wb = openpyxl.Workbook()
    T = wb.active; T.title = 'Tests'
    D = wb.create_sheet('Data')
    D['A1'] = 'The ranges the Tests sheet\'s formulas read (each case\'s arrays, in cells).'
    drow = 3
    rows = []                               # (row, case index, form, display formula)
    r = FIRST
    for i, case in enumerate(C):
        opts = {}
        if isinstance(case[-1], dict): opts = case[-1]; case = case[:-1]
        if len(case) == 3: name, _, args = case; refonly = True
        else: name, args = case; refonly = False
        refonly = refonly or name in RANGE_ONLY
        la, ra, argcells = [], [], []
        col = COL_ARG
        for a in args:
            if isinstance(a, list):
                m = [x if isinstance(x, list) else [x] for x in a]
                la.append('{' + ';'.join(','.join(lit(v) for v in row) for row in m) + '}')
                for y, row in enumerate(m):
                    for x, v in enumerate(row): put(D, D.cell(drow + y, 1 + x).coordinate, v)
                ra.append('Data!%s:%s' % (D.cell(drow, 1).coordinate, D.cell(drow + len(m) - 1, len(m[0])).coordinate))
                drow += len(m) + 1
            else:
                la.append(a); argcells.append((col, a)); ra.append('%s%%d' % openpyxl.utils.get_column_letter(col)); col += 1
        fn = ('_xlfn.' if name in XLFN else '') + name
        forms = []
        if not refonly: forms.append(('typed', '=' + call(fn, ','.join(la))))
        if args and not opts.get('nocells'): forms.append(('cells', '=' + call(fn, ','.join(ra))))
        if opts.get('spill') and isinstance(opts.get('x'), list) and any(isinstance(v, str) and v.startswith('#') for row in opts['x'] for v in (row if isinstance(row, list) else [row])):
            continue          # (an array with errors in it: tools/functest's suite compares those cell by cell)
        if opts.get('spill'):
            # a dynamic array: in one cell as its shape and its values joined (a spill would run over the
            # next columns), or for RANDARRAY what must be true of its shape and range
            def wrap(f):
                g = f[1:]
                if 'shape' in opts:
                    return '=AND(ROWS(%s)=%d,COLUMNS(%s)=%d,MIN(%s)>=%s,MAX(%s)<=%s)' % (g, opts['shape'][0], g, opts['shape'][1], g, opts['range'][0], g, opts['range'][1])
                return '=ROWS(%s)&"x"&COLUMNS(%s)&": "&_xlfn.TEXTJOIN(", ",FALSE,%s)' % (g, g, g)
            forms = [(fm, wrap(f)) for fm, f in forms]
        for form, f in forms:
            if form == 'cells':
                f = f.replace('%d', str(r))
                for c, a in argcells: put(T, T.cell(r, c).coordinate, a)
            if name in VOLATILE:
                lf, cf = VOLATILE[name]
                f = lf if form == 'typed' else (cf % tuple('%s%d' % (openpyxl.utils.get_column_letter(c), r) for c, _ in argcells))
            T.cell(r, 1, len(rows) + 1)
            T.cell(r, COL_FN, name)
            T.cell(r, COL_FORM, form)
            text(T, T.cell(r, COL_TEXT).coordinate, f.replace('_xlfn.', '').replace('_xlpm.', ''))
            if opts.get('spill'): T[T.cell(r, COL_RES).coordinate] = ArrayFormula(T.cell(r, COL_RES).coordinate, f)
            else: T.cell(r, COL_RES, f)
            # (not TYPE(): LibreOffice gives 8, "formula", for a cell with a formula in it)
            if name.startswith('IM') or name == 'COMPLEX':
                # complex numbers as text: compared as numbers (the last of 15 digits may differ with the maths library)
                T.cell(r, COL_CHK, ('=IF(ISERROR(F{0}),IF(ISERROR(E{0}),IF(ERROR.TYPE(E{0})=ERROR.TYPE(F{0}),"PASS","FAIL"),"FAIL"),IF(ISERROR(E{0}),"FAIL",'
                                    'IF(ISERROR(IMABS(IMSUB(E{0},F{0}))),IF(E{0}=F{0},"PASS","FAIL"),IF(IMABS(IMSUB(E{0},F{0}))<=1E-9*MAX(1,IMABS(F{0})),"PASS","FAIL"))))').format(r))
            else: T.cell(r, COL_CHK, ('=IF(ISERROR(F{0}),IF(ISERROR(E{0}),IF(ERROR.TYPE(E{0})=ERROR.TYPE(F{0}),"PASS","FAIL"),"FAIL"),IF(ISERROR(E{0}),"FAIL",'
                                'IF(ISNUMBER(F{0}),IF(ISNUMBER(E{0}),IF(ABS(E{0}-F{0})<=1E-9*MAX(1,ABS(F{0})),"PASS","FAIL"),"FAIL"),'
                                'IF(ISLOGICAL(F{0}),IF(ISLOGICAL(E{0}),IF(E{0}=F{0},"PASS","FAIL"),"FAIL"),'
                                'IF(ISTEXT(E{0}),IF(EXACT(E{0},F{0}),"PASS","FAIL"),"FAIL")))))').format(r))
            if expected is not None:
                v = expected[r]
                cell = T.cell(r, COL_EXP)
                if v.startswith('N:'): cell.value = float(v[2:]) if ('.' in v or 'e' in v) else int(v[2:])
                elif v.startswith('B:'): cell.value = v[2:] == 'TRUE'
                elif v.startswith('E:'): cell.value = v[2:]; cell.data_type = 'e'
                elif v == 'S:': cell.value = '=""'
                else: cell.value = v[2:]; cell.data_type = 's'
            if notes and r in notes: text(T, T.cell(r, COL_NOTE).coordinate, notes[r])
            rows.append((r, i, form, f, opts))
            r += 1
    last = r - 1
    # the top: title, what it is, the summary
    T['A1'] = 'Ledger function tests'; T['A1'].font = Font(size=16, bold=True)
    text(T, 'A2', 'Every function Ledger has, each case worked out twice: with its arguments typed in ("typed") and with them in cells '
                  '("cells", to the right of the Note). Check says PASS when the Result is the Expected answer (Excel\'s). Change an argument in the cells to try your own.')
    T['B3'] = 'Passed'; T['C3'] = '=COUNTIF(G%d:G%d,"PASS")' % (FIRST, last)
    T['B4'] = 'Failed'; T['C4'] = '=COUNTIF(G%d:G%d,"FAIL")' % (FIRST, last)
    T['B5'] = 'Tests'; T['C5'] = '=COUNTA(G%d:G%d)' % (FIRST, last)
    text(T, 'D3', '%d functions tested' % len({c[0] for c in C}))
    T['B3'].font = T['B4'].font = T['B5'].font = T['C3'].font = T['C4'].font = Font(bold=True)
    for k, h in enumerate(['#', 'Function', 'Form', 'Formula', 'Result', 'Expected', 'Check', 'Note', '', 'Arguments (cells form)']):
        if h: c = T.cell(FIRST - 1, k + 1, h); c.font = Font(bold=True, color='FFFFFF'); c.fill = PatternFill('solid', fgColor='2F5597')
    # (A to G fit a 640 pixel wide screen)
    for col, w in zip('ABCDEFGHIJKL', [5, 13, 7, 30, 12, 12, 7, 40, 2, 12, 12, 12]): T.column_dimensions[col].width = w
    T.freeze_panes = 'C%d' % FIRST
    rng = 'G%d:G%d' % (FIRST, last)
    T.conditional_formatting.add(rng, CellIsRule(operator='equal', formula=['"PASS"'], fill=PatternFill('solid', fgColor='C6EFCE'), font=Font(color='006100')))
    T.conditional_formatting.add(rng, CellIsRule(operator='equal', formula=['"FAIL"'], fill=PatternFill('solid', fgColor='FFC7CE'), font=Font(color='9C0006')))
    return wb, rows, last

def ledger_eval(path, cols):
    res = {}
    for line in subprocess.check_output([EVAL, path] + [str(c) for c in cols], text=True).splitlines():
        p = line.split('\t'); res[int(p[0])] = p[1:]
    return res
def lo_eval(path):
    lo = os.path.join(work, 'lo')
    os.makedirs(lo, exist_ok=True)
    subprocess.run(['soffice', '--headless', '--convert-to', 'xlsx', '--outdir', lo, path], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, timeout=600, cwd=work)
    return openpyxl.load_workbook(os.path.join(lo, os.path.basename(path)), data_only=True)['Tests']
def lo_val(v):
    if v is None: return 'NONE'
    if isinstance(v, datetime.datetime): v = (v - datetime.datetime(1899, 12, 30)).total_seconds() / 86400
    if isinstance(v, datetime.time): v = (v.hour * 3600 + v.minute * 60 + v.second + v.microsecond / 1e6) / 86400
    if isinstance(v, bool): return 'B:' + ('TRUE' if v else 'FALSE')
    if isinstance(v, (int, float)): return 'N:%.15g' % v
    if isinstance(v, str) and v.startswith('#'): return 'E:' + v
    return 'S:' + str(v)
def same(a, b):
    if a == b: return True
    if 'NONE' in (a, b): return (a if b == 'NONE' else b) in ('S:', 'N:0')
    if a[:2] == 'N:' and b[:2] == 'N:':
        x, y = float(a[2:]), float(b[2:]); return abs(x - y) <= 1e-9 * max(1, abs(x), abs(y))
    return False
def shown(v):
    return {'N': lambda s: s, 'S': lambda s: '"%s"' % s, 'B': lambda s: s, 'E': lambda s: s}.get(v[:1], lambda s: v)(v[2:]) if v not in ('NONE',) else 'nothing'

# the documented differences (excel.txt), by case and form: the reference run of gen.py's layout
known = {}
for line in open(os.path.join(HERE, 'excel.txt')):
    if line.strip() and not line.startswith('#'):
        k, why = line.rstrip('\n').split('\t', 1); known[k] = why
# (gen.py's own numbering: case i is on row i + 1 of its sheet; its formulas are the keys of excel.txt)
gen_forms = {}                              # case index -> (typed formula, cells formula, name, FUNC#n) as gen.py writes them
subprocess.check_output([sys.executable, os.path.join(HERE, 'gen.py'), HERE, os.path.join(work, 'ref.xlsx'), os.path.join(work, 'ref.json')], text=True)
_seen = {}
for i, m in enumerate(json.load(open(os.path.join(work, 'ref.json')))):
    _seen[m[1]] = _seen.get(m[1], 0) + 1
    gen_forms[i] = (m[2], m[3], m[1], '%s#%d' % (m[1], _seen[m[1]]))
def reason(i, form):
    fl, fr, name, nth = gen_forms[i]
    f = fl if form == 'typed' else fr
    # (a cells form is explained by its typed twin's line too, as compare.py takes it)
    return known.get(f) or known.get(nth) or known.get('LIT:' + name) or (form == 'cells' and known.get(fl))
def excel_value(x):
    """a case's documented Excel answer ('x') as evalcols writes values; a spill as the wrapper shows it"""
    def one(v):
        if v is None: return ''
        if isinstance(v, bool): return 'TRUE' if v else 'FALSE'
        if isinstance(v, (int, float)): return '%.15g' % v
        return str(v)
    if isinstance(x, list):
        rows = [r if isinstance(r, list) else [r] for r in x]
        return 'S:%dx%d: %s' % (len(rows), len(rows[0]), ', '.join(one(v) for r in rows for v in r))
    if isinstance(x, bool): return 'B:' + one(x)
    if isinstance(x, (int, float)): return 'N:' + one(x)
    if isinstance(x, str) and x.startswith('#'): return 'E:' + x
    return 'S:' + x
def same_x(ours, x):
    """Ledger's answer against Excel's documented one, numbers to 1E-9"""
    want = excel_value(x)
    if same(ours, want): return True
    if not isinstance(x, list) and ours.startswith('S:1x1: '):      # (a spill of one value)
        v = ours[7:]
        return v == want[2:] or same('N:' + v, want) if want[:2] == 'N:' else v == want[2:]
    if ours[:2] == want[:2] == 'S:' and isinstance(x, list):
        a, b = ours[2:].split(', '), want[2:].split(', ')
        if len(a) != len(b): return False
        for u, v in zip(a, b):
            try:
                if abs(float(u.split(': ')[-1]) - float(v.split(': ')[-1])) > 1e-9 * max(1, abs(float(v.split(': ')[-1]))): return False
            except ValueError:
                if u != v: return False
        return True
    return False

# 1: the workbook without its answers, worked out by both
wb, rows, last = build()
p1 = os.path.join(work, 'stage1.xlsx'); wb.save(p1)
led = ledger_eval(p1, [COL_RES])
lo = lo_eval(p1)
expected, notes, problems = {}, {}, []
for r, i, form, f, opts in rows:
    ours = led[r][0]
    theirs = lo_val(lo.cell(r, COL_RES).value)
    expected[r] = ours
    if same(ours, theirs): continue
    why = reason(i, form) or opts.get('lodiff')
    if 'x' in opts and not same_x(ours, opts['x']): problems.append((r, f, ours, 'Excel ' + excel_value(opts['x']))); continue
    if 'shape' in opts and ours != 'B:TRUE': problems.append((r, f, ours, 'B:TRUE')); continue
    if why: notes[r] = 'Excel\'s answer; LibreOffice gives %s (%s)' % (shown(theirs), why)
    elif 'x' in opts or 'shape' in opts:
        notes[r] = 'Excel\'s documented answer; LibreOffice gives %s (it hasn\'t this function, or not in this form)' % shown(theirs)
    else: problems.append((r, f, ours, theirs))
if problems:
    for p in problems: print('UNEXPLAINED row %d %s: Ledger %s, LibreOffice %s' % p)
    sys.exit(1)

# 2: the finished workbook, then checked again by both. A row LibreOffice fails although it shows the
# same error is one where LibreOffice raises an error of its own (Err:502, "invalid argument") and
# only writes it out as #VALUE!: its ERROR.TYPE doesn't know it. Those get a note too, and the
# workbook is made once more
for attempt in range(2):
    wb, rows2, last = build(expected, notes)
    wb.save(out)
    led = ledger_eval(out, [COL_CHK])
    lo = lo_eval(out)
    fails_led = [r for r, *_ in rows2 if led[r][0] != 'S:PASS']
    fails_lo = [r for r, *_ in rows2 if lo.cell(r, COL_CHK).value != 'PASS']
    unexpected_lo = [r for r in fails_lo if r not in notes]
    own = [r for r in unexpected_lo if expected[r].startswith('E:') and lo_val(lo.cell(r, COL_RES).value) == expected[r]]
    empty = [r for r in unexpected_lo if expected[r] == 'N:0' and lo_val(lo.cell(r, COL_RES).value) == 'N:0']
    for r in empty: notes[r] = 'Excel\'s answer; LibreOffice gives the empty cell itself (shown as 0 but no number to ISNUMBER)'
    if not own and not empty: break
    for r in own: notes[r] = 'Excel\'s answer; LibreOffice raises its own error here (Err:502) and shows it as %s, which its ERROR.TYPE doesn\'t take for one' % expected[r][2:]
nfun = len({c[0] for c in C})
report = ['FunctionTests.xlsx: %d tests of %d functions (%d cases, each with arguments typed in and in cells where it takes any)' % (len(rows2), nfun, len(C)),
          'Ledger\'s core: %d PASS, %d FAIL' % (len(rows2) - len(fails_led), len(fails_led)),
          'LibreOffice: %d PASS, %d FAIL (%d of them the rows noted as Excel\'s answer, %d others)' % (len(rows2) - len(fails_lo), len(fails_lo), len(fails_lo) - len(unexpected_lo), len(unexpected_lo))]
print('\n'.join(report))
open(os.path.join(work, 'report.txt'), 'w').write('\n'.join(report) + '\n')
if fails_led or unexpected_lo:
    for r in fails_led: print('Ledger FAIL row', r)
    for r in unexpected_lo: print('LibreOffice FAIL row', r)
    sys.exit(1)
