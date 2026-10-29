#!/usr/bin/env python3
# langcheck.py - checks that every interface string in Ledger's Amiga source has its translations.
#
#   python3 tools/langcheck.py            list what is missing (exit 1 if anything is)
#   python3 tools/langcheck.py --rows     the same, printed as rows to paste into lang/strings.txt
#                                         (English in each column: translate the four after it)
#   python3 tools/langcheck.py --unused   also list the rows of strings.txt the source no longer has
#
# Every string literal in amiga/*.c and amiga/*.h (adjacent literals joined, as C does), and the
# category names and descriptions in core/funcdesc.c, must be one of:
#   - a row of lang/strings.txt (the English column), so tr() finds its translations;
#   - in lang/ignore.txt (words that are not the interface's: file names, format codes, font names,
#     log lines ...);
#   - plainly not words (see not_words below: icon drawings, numbers, format specifiers alone,
#     ALL-CAPS names such as function names, file and device names, the arguments of printf, strcmp
#     and other calls that never show their text, gadget sizing samples (MUIA_FixWidthTxt ...)).
# It also checks each row's translations keep the English's printf conversions (%s %d ...) in order,
# its '|' requester buttons, its leading MUI escape (\33c) and its '_' shortcut marker, and it
# flags a literal handed straight to a gadget (Label1("..."), QButton("..."), MUIA_ShortHelp ...)
# without tr() (unless its line says "lang: as is"). After adding strings, run this, translate, then tools/mklang.py.
import glob, os, re, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import mklang

ROOT = mklang.ROOT
IGNORE = os.path.join(ROOT, 'lang', 'ignore.txt')

# calls whose string arguments are never shown as words
QUIET_CALLS = {'printf', 'fprintf', 'sscanf', 'strcmp', 'strncmp', 'strstr', 'strchr', 'strrchr', 'strcspn', 'strspn',
               'Stricmp', 'Strnicmp', 'stricmp', 'strnicmp', 'fopen', 'Open', 'Lock', 'OpenLibrary', 'OpenDevice', 'FindPort',
               'MAKE_ID', 'qlogf', 'logf', 'dbg', 'OpenDiskFont', 'GetVar', 'SetVar', 'DeleteFile', 'Rename', 'CreateDir',
               'MatchPattern', 'ParsePattern', 'ParsePatternNoCase', 'MatchFirst', 'AddPart', 'strcasecmp', 'strncasecmp',
               'name_is', 'ext_is', 'has_ext', 'eq', 'starts', 'ends_with', 'fputs', 'qlog_line', 'splash_log'}
# gadget-making calls and tags that show their text as it is: a literal there needs tr()
SHOWN = re.compile(r'(?:\b(?:Label1|Label2|LLabel1|LLabel2|Label|LLabel|QLabel|QButton|SimpleButton|KeyButton|theme_bubble|CLabel)\s*\(\s*'
                   r'|MUIA_(?:ShortHelp|Text_Contents|Window_Title|Frame_Title|FrameTitle)\s*,\s*\(ULONG\)\s*)"([^"]+)"')
FMT = re.compile(r'%(?:-?[0-9]*(?:\.[0-9]+)?|\.\*|-?\*)(?:hh|h|ll|l)?[diouxXeEfgGcs%]')

def literals(path):
    """the string literals of a C file: (line, start offset, raw text), adjacent ones joined"""
    src = open(path, encoding='latin-1').read()
    out, i, n, line, cur = [], 0, len(src), 1, None
    while i < n:
        c = src[i]
        if c == '\n': line += 1; i += 1; continue
        if src.startswith('/*', i):
            j = src.find('*/', i + 2); line += src.count('\n', i, j); i = j + 2; continue
        if src.startswith('//', i): i = src.find('\n', i); continue
        if c == "'":
            j = i + 1
            while src[j] != "'": j += 2 if src[j] == '\\' else 1
            i = j + 1
            if cur: out.append(cur); cur = None
            continue
        if c == '"':
            j = i + 1
            while src[j] != '"': j += 2 if src[j] == '\\' else 1
            if cur: cur = (cur[0], cur[1], cur[2] + src[i + 1:j])
            else: cur = (line, i, src[i + 1:j])
            i = j + 1; continue
        if c in ' \t\r': i += 1; continue
        if c == '\\' and src.startswith('\\\n', i): i += 1; continue
        if cur: out.append(cur); cur = None
        if src.startswith('#include', i): i = src.find('\n', i); continue
        i += 1
    if cur: out.append(cur)
    return src, out

def enclosing_call(src, pos):
    """the name of the call whose parentheses hold position pos ('' if none on the way back)"""
    depth, i = 0, pos - 1
    lim = max(0, pos - 3000)
    while i > lim:
        c = src[i]
        if c == ')': depth += 1
        elif c == '(':
            if depth == 0:
                j = i - 1
                while j >= 0 and src[j] in ' \t': j -= 1
                k = j
                while k >= 0 and (src[k].isalnum() or src[k] == '_'): k -= 1
                return src[k + 1:j + 1]
            depth -= 1
        elif c in ';{}' and depth == 0: return ''
        i -= 1
    return ''

ICON = re.compile(r'^(?:[A-Za-z]{1,2}|-?\d+(?:\.\d+)?)(?: (?:[A-Za-z]{1,2}|-?\d+(?:\.\d+)?))*$')
def not_words(t):
    """true for text that isn't words for people to read"""
    s = FMT.sub(' ', t)
    if not re.search(r'[A-Za-z\300-\377]{2}', s): return True             # numbers, signs, conversions
    if ICON.match(t) and re.search(r'\d', t): return True                  # an icon's drawing (theme.c)
    if re.fullmatch(r'[A-Z][A-Z0-9_.]*[A-Z0-9]?(\(\)?)?', t) and len(t) >= 3: return True   # FUNCTION names, IDs
    if ' ' not in t and re.fullmatch(r'[A-Za-z0-9_]+:\S+', t): return True           # PROGDIR:file, FONTS:...
    if ' ' not in t and re.search(r'\.[a-z]{2,9}$', t) and not t.endswith('...'): return True  # file names
    if ' ' not in t and re.fullmatch(r'[a-z0-9_/.\-]+', t) and ('_' in t or '/' in t): return True   # paths, ids
    return False

def read_ignore():
    ign = set()
    if os.path.exists(IGNORE):
        for line in open(IGNORE, encoding='utf-8'):
            line = line.rstrip('\n')
            if not line or line.startswith('# ') or line == '#': continue
            ign.add(mklang.unescape(line))
    return ign

def sources():
    files = sorted(glob.glob(os.path.join(ROOT, 'amiga', '*.c')) + glob.glob(os.path.join(ROOT, 'amiga', '*.h')))
    return [f for f in files if not f.endswith('lang_table.h')]

def funcdesc_strings():
    """core/funcdesc.c's category names and descriptions (the third field of each entry)"""
    path = os.path.join(ROOT, 'core', 'funcdesc.c')
    if not os.path.exists(path): return []
    src = open(path, encoding='utf-8').read()
    out = []
    m = re.search(r'func_cat_names\[[^]]*\]\s*=\s*\{([^}]*)\}', src)
    if m:
        for s in re.findall(r'"((?:[^"\\]|\\.)*)"', m.group(1)): out.append(('core/funcdesc.c', 0, mklang.unescape(s)))
    for lno, line in enumerate(src.split('\n'), 1):
        mm = re.match(r'\s*\{\s*"[^"]*",\s*"(?:[^"\\]|\\.)*",\s*"((?:[^"\\]|\\.)*)"', line)
        if mm: out.append(('core/funcdesc.c', lno, mklang.unescape(mm.group(1)).encode('utf-8').decode('utf-8')))
    return out

def check_row(texts):
    """problems with one row's translations"""
    en, probs = texts[0], []
    fe = FMT.findall(en)
    for k, t in enumerate(texts[1:], 1):
        lang = 'de fr es it'.split()[k - 1]
        if FMT.findall(t) != fe: probs.append('%s: conversions %s, not %s' % (lang, FMT.findall(t), fe))
        if en.count('|') != t.count('|'): probs.append("%s: %d '|', not %d" % (lang, t.count('|'), en.count('|')))
        if en.startswith('\x1b') and not t.startswith(en[:2]): probs.append('%s: should start with the same \\33 escape' % lang)
        if ('_' in en) != ('_' in t) and re.search(r'(^|\|)_', en): probs.append("%s: '_' shortcut marker" % lang)
        if not t.strip(): probs.append('%s: empty' % lang)
    return probs

def main():
    items, bad = mklang.read_rows()
    for b in bad: print('lang/strings.txt:' + b)
    rows = {v[0]: (n, v) for k, n, v in items if k == 'row'}
    ign = read_ignore()
    missing, used, unwrapped = {}, set(), []
    for path in sources():
        rel = os.path.relpath(path, ROOT)
        src, lits = literals(path)
        for line, pos, raw in lits:
            t = mklang.unescape(raw)
            if t in rows: used.add(t); continue
            if t in ign or not_words(t): continue
            if enclosing_call(src, pos) in QUIET_CALLS: continue
            if re.search(r'MUIA_(?:FixWidthTxt|FixHeightTxt|String_Accept|String_Reject|List_Format)\s*,\s*(?:\(ULONG\)\s*)?$', src[max(0, pos - 60):pos]): continue
            missing.setdefault(t, []).append('%s:%d' % (rel, line))
        for m in SHOWN.finditer(src):
            t = mklang.unescape(m.group(1))
            if 'lang: as is' in src[m.start():src.find('\n', m.start())]: continue
            if t in rows or (t not in ign and not not_words(t)):
                unwrapped.append('%s:%d: "%s" is shown without tr()' % (rel, src.count('\n', 0, m.start()) + 1, m.group(1)))
    for rel, line, t in funcdesc_strings():
        if t in rows: used.add(t); continue
        if t in ign: continue
        missing.setdefault(t, []).append('%s:%d' % (rel, line))
    probs = []
    for en, (n, v) in rows.items():
        for p in check_row(v): probs.append('lang/strings.txt:%d: %s' % (n, p))
    if '--rows' in sys.argv:
        for t in missing: print('\t'.join([t.replace('\\', '\\\\').replace('\n', '\\n').replace('\t', '\\t')] * 5))
        return 1 if missing else 0
    for t, locs in sorted(missing.items(), key=lambda x: x[1][0]):
        print('%s: not translated: "%s"' % (locs[0], t.replace('\n', '\\n')))
    for u in unwrapped: print(u)
    for p in probs: print(p)
    if '--unused' in sys.argv:
        for en, (n, v) in sorted(rows.items(), key=lambda x: x[1][0]):
            if en not in used: print('lang/strings.txt:%d: unused: "%s"' % (n, en.replace('\n', '\\n')))
    print('langcheck: %d rows, %d used, %d strings missing, %d shown without tr(), %d row problems'
          % (len(rows), len(used), len(missing), len(unwrapped), len(probs)))
    return 1 if missing or unwrapped or probs or bad else 0

if __name__ == '__main__':
    sys.exit(main())
