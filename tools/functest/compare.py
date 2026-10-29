import openpyxl, json, sys, math
d = sys.argv[1]
meta = json.load(open(d + '/meta.json'))
lowb = openpyxl.load_workbook(d + '/lo/ft.xlsx', data_only=True)
lo = lowb['T']
los = lowb['S'] if 'S' in lowb.sheetnames else None
lowf = openpyxl.load_workbook(d + '/lo/ft.xlsx')
import re as _re
def lo_lacks(sheet, row, col, name):
    # LibreOffice writes a function it doesn't know in lower case
    v = lowf[sheet].cell(row, col).value
    t = getattr(v, 'text', None) or (v if isinstance(v, str) else '')
    return _re.search(r'(^|[^A-Za-z.])(_xlfn\.)?' + _re.escape(name.lower()) + r'\(', t or '') is not None
led = {}; leds = {}
for line in open(d + '/ledger.tsv'):
    p = line.rstrip('\n').split('\t')
    if p[0].startswith('S'): leds[int(p[0][1:])] = p[1:]
    else: led[int(p[0])] = p[1:]
import datetime
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
        x, y = float(a[2:]), float(b[2:])
        return abs(x - y) <= 1e-9 * max(1, abs(x), abs(y))
    return False
VOLATILE = {'RAND', 'RANDBETWEEN', 'NOW'}
known = {}
import os
for line in open(os.path.join(os.path.dirname(os.path.abspath(__file__)), 'excel.txt')):
    if line.strip() and not line.startswith('#'):
        k, why = line.rstrip('\n').split('\t', 1); known[k] = why
bad = []; n = 0; explained = 0; byexcel = 0; lolacks = 0
def want(x):
    # an expected value written in a case (Excel's documented result)
    if isinstance(x, bool): return 'B:' + ('TRUE' if x else 'FALSE')
    if isinstance(x, (int, float)): return 'N:%.15g' % x
    if x is None: return 'N:0'
    if isinstance(x, str) and x.startswith('#'): return 'E:' + x
    return 'S:' + x
def grid(x):
    if not isinstance(x, list): return [[want(x)]]
    return [[want(v) for v in (r if isinstance(r, list) else [r])] for r in x]
def spill_cells(s):
    # 'RxC|v|v|...' from the harness
    p = s.split('|'); r, c = p[0].split('x'); r, c = int(r), int(c)
    return [p[1 + i * c:1 + (i + 1) * c] for i in range(r)]
BH, BW = 10, 9
def lo_block(row, col):
    return [[lo_val(los.cell(row + i, col + j).value) for j in range(BW)] for i in range(BH)]
def lo_agrees(ours, blk):
    # LibreOffice saw a {=...} over the block: our spill in its corner, the rest #N/A (or a single
    # row/column repeated, as {=...} does)
    R, Cc = len(ours), len(ours[0])
    for i in range(BH):
        for j in range(BW):
            o = blk[i][j]
            if i < R and j < Cc:
                if not same(ours[i][j], o): return False
            elif o != 'E:#N/A':
                si, sj = (0 if R == 1 else i), (0 if Cc == 1 else j)
                if si >= R or sj >= Cc or not same(ours[si][sj], o): return False
    return True
def eq_grid(a, b):
    return len(a) == len(b) and all(len(x) == len(y) and all(same(p, q) for p, q in zip(x, y)) for x, y in zip(a, b))
for m in meta:
    if len(m) == 5 and m[4].get('sheet') == 'S':
        row, name, fl, fr, o = m
        lv = leds.get(row, ['-', '-'])
        for k, (form, col) in enumerate(((fl, 2), (fr, 3 + BW))):
            if not form: continue
            n += 1
            ours = spill_cells(lv[k]) if lv[k] != '-' else [['-']]
            blk = lo_block(row, col)
            nolo = blk[0][0] == 'E:#NAME?' or lo_lacks('S', row, col, name)
            ok = True
            if 'shape' in o:
                # random numbers: the size and that each is a number in range
                lo_, hi_ = o.get('range', (0, 1))
                ok = len(ours) == o['shape'][0] and all(len(r) == o['shape'][1] and all(v[:2] == 'N:' and lo_ <= float(v[2:]) <= hi_ for v in r) for r in ours)
                if ok: byexcel += 1
                nolo = True
            elif 'x' in o:
                ok = eq_grid(ours, grid(o['x']))
                if ok and nolo: byexcel += 1; lolacks += 1
            if ok and not nolo and not o.get('volatile') and not lo_agrees(ours, blk):
                if 'x' in o and o.get('lodiff'): explained += 1; continue     # Excel's value; LibreOffice differs (the case says why)
                ok = False
            if not ok and form in known: explained += 1
            elif not ok and nolo and 'x' not in o:
                bad.append((row, name, 'spill (LibreOffice lacks it, no expected value)', form, lv[k], ''))
            elif not ok:
                bad.append((row, name, 'spill ' + ('literal' if k == 0 else 'cells'), form, lv[k], 'excel=' + str(o.get('x')) + ' lo=' + '|'.join(blk[0][:4])))
        if fl and fr and 'shape' not in o and lv[0] != lv[1] and 'LIT:' + name not in known:
            bad.append((row, name, 'literal vs cells', fl + '  |  ' + fr, lv[0], lv[1]))
        continue
    row, name, fl, fr = m[:4]
    o = m[4] if len(m) == 5 else {}
    if 'x' in o:
        # an expected value: checked against it, and against LibreOffice when it has the function
        lv = led.get(row, ['-', '-'])
        for k, (form, col) in enumerate(((fl, 2), (fr, 3))):
            if not form: continue
            n += 1
            ours = lv[k]; theirs = lo_val(lo.cell(row, col).value)
            ok = same(ours, want(o['x']))
            nolo = theirs == 'E:#NAME?' or lo_lacks('T', row, col, name)
            if ok and nolo: byexcel += 1; lolacks += 1
            elif ok and not same(ours, theirs) and not o.get('volatile'):
                if o.get('lodiff'): explained += 1; continue
                ok = False
            if not ok and form in known: explained += 1
            elif not ok: bad.append((row, name, 'literal' if k == 0 else 'cells', form, ours, 'excel=%s lo=%s' % (want(o['x']), theirs)))
        continue
    lv = led.get(row, ['-', '-'])
    for k, (form, col) in enumerate(((fl, 2), (fr, 3))):
        if not form: continue
        n += 1
        ours = lv[k]; theirs = lo_val(lo.cell(row, col).value)
        if name in VOLATILE:
            ok = ours.startswith('N:')
            if name == 'RANDBETWEEN': ok = ours in ('N:%d' % i for i in range(1, 7))
            if name == 'NOW': ok = ok and theirs.startswith('N:') and abs(float(ours[2:]) - float(theirs[2:])) < 0.01
        else: ok = same(ours, theirs)
        if not ok and form in known: explained += 1
        elif not ok: bad.append((row, name, 'literal' if k == 0 else 'cells', form, ours, theirs))
    # the two forms against each other
    if fl and fr and name not in VOLATILE and not same(lv[0], lv[1]) and 'LIT:' + name in known: explained += 1
    elif fl and fr and name not in VOLATILE and not same(lv[0], lv[1]):
        bad.append((row, name, 'literal vs cells', fl + '  |  ' + fr, lv[0], lv[1]))
print('%d functions, %d cases, %d results checked (each case with numbers typed in and with cell references)' % (len({m[1] for m in meta}), len(meta), n))
print('%d differences explained (Ledger follows Excel; see excel.txt), %d unexplained' % (explained, len(bad)))
if byexcel: print('%d results checked against Excel\'s documented values where this LibreOffice lacks the function' % byexcel)
for b in bad: print('%4d %-12s %-16s %-60s ledger=%-22s other=%s' % b)
