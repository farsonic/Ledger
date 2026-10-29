import openpyxl, json, sys, math
d = sys.argv[1]
meta = json.load(open(d + '/meta.json'))
lo = openpyxl.load_workbook(d + '/lo/ft.xlsx', data_only=True)['T']
led = {}
for line in open(d + '/ledger.tsv'):
    p = line.rstrip('\n').split('\t')
    led[int(p[0])] = p[1:]
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
bad = []; n = 0; explained = 0
for row, name, fl, fr in meta:
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
        if not ok and (form in known or (k == 1 and fl in known)): explained += 1   # (the cells form of a known literal one)
        elif not ok: bad.append((row, name, 'literal' if k == 0 else 'cells', form, ours, theirs))
    # the two forms against each other
    if fl and name not in VOLATILE and not same(lv[0], lv[1]) and 'LIT:' + name in known: explained += 1
    elif fl and name not in VOLATILE and not same(lv[0], lv[1]):
        bad.append((row, name, 'literal vs cells', fl + '  |  ' + fr, lv[0], lv[1]))
print('%d functions, %d cases, %d results checked (each case with numbers typed in and with cell references)' % (len({m[1] for m in meta}), len(meta), n))
print('%d differences explained (Ledger follows Excel; see excel.txt), %d unexplained' % (explained, len(bad)))
for b in bad: print('%4d %-12s %-16s %-60s ledger=%-22s other=%s' % b)
