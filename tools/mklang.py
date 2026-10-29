#!/usr/bin/env python3
# mklang.py - makes amiga/lang_table.h, the words of Ledger's interface in its languages, from
# lang/strings.txt (see that file's head for its form).
#
#   python3 tools/mklang.py           write amiga/lang_table.h
#   python3 tools/mklang.py --check   only say whether amiga/lang_table.h is as strings.txt makes it
#
# Each row of strings.txt is the English exactly as the C source writes it, then German, French,
# Spanish and Italian, all UTF-8 with C escapes (\n \t \" \\ and octal) allowed. The translations
# must fit in Latin-1, the Amiga's character set: the generator stops with the line's number if one
# doesn't. tr() (amiga/lang.c) finds a row by its English. tools/langcheck.py checks the C source
# against the rows.
import os, sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
STRINGS = os.path.join(ROOT, 'lang', 'strings.txt')
TABLE = os.path.join(ROOT, 'amiga', 'lang_table.h')
NLANG = 5
HEAD = '/* lang_table.h - made by tools/mklang.py: do not edit (English, German, French, Spanish, Italian) */\n'

def unescape(s):
    """the text a C-escaped string stands for (a str: \\n, \\t, \\", \\\\, octal)"""
    out, i = [], 0
    while i < len(s):
        c = s[i]
        if c != '\\': out.append(c); i += 1; continue
        i += 1
        c = s[i] if i < len(s) else '\\'
        if c in '01234567':
            j = i
            while j < len(s) and j < i + 3 and s[j] in '01234567': j += 1
            out.append(chr(int(s[i:j], 8))); i = j; continue
        out.append({'n': '\n', 't': '\t', '"': '"', '\\': '\\', 'r': '\r', 'a': '\a', 'e': '\x1b'}.get(c, c)); i += 1
    return ''.join(out)

def c_literal(t):
    """text (str, Latin-1) as a C string literal: ASCII as it is, the rest in octal"""
    out, prev_oct = '', False
    for ch in t:
        x = ord(ch)
        if x == 10: out += '\\n'; prev_oct = False
        elif x == 9: out += '\\t'; prev_oct = False
        elif ch in '"\\': out += '\\' + ch; prev_oct = False
        elif 32 <= x < 127:
            if prev_oct and ch in '01234567': out += '""'      # an octal escape ends here
            out += ch; prev_oct = False
        else: out += '\\%03o' % x; prev_oct = True
    return '"' + out + '"'

def read_rows(path=STRINGS):
    """strings.txt as a list of ('row', lineno, [5 texts]) and ('section', lineno, title); problems
    as a list of messages"""
    items, bad, seen = [], [], {}
    with open(path, encoding='utf-8') as f:
        for n, line in enumerate(f, 1):
            line = line.rstrip('\n')
            if not line.strip() or line.startswith('# '): continue
            if line.startswith('## '): items.append(('section', n, line[3:].strip())); continue
            if line == '#': continue
            cols = line.split('\t')
            if len(cols) != NLANG: bad.append('%d: %d columns, not %d: %s' % (n, len(cols), NLANG, line[:60])); continue
            texts = [unescape(c) for c in cols]
            if texts[0] in seen: bad.append('%d: "%s" is already on line %d' % (n, cols[0], seen[texts[0]])); continue
            seen[texts[0]] = n
            for k, t in enumerate(texts):
                try: t.encode('latin-1')
                except UnicodeEncodeError: bad.append('%d: not Latin-1 (column %d): %s' % (n, k + 1, cols[k]))
            items.append(('row', n, texts))
    return items, bad

def table_text(items):
    lines = [HEAD]
    for kind, n, v in items:
        if kind == 'section': lines.append('  /* %s */\n' % v)
        else: lines.append('  { { ' + ', '.join(c_literal(t) for t in v) + ' } },\n')
    return ''.join(lines)

def main():
    items, bad = read_rows()
    for b in bad: print('lang/strings.txt:' + b)
    if bad: return 1
    text = table_text(items)
    old = open(TABLE, encoding='latin-1').read() if os.path.exists(TABLE) else ''
    rows = sum(1 for i in items if i[0] == 'row')
    if '--check' in sys.argv:
        if old != text: print('amiga/lang_table.h is out of date: run tools/mklang.py'); return 1
        print('amiga/lang_table.h is up to date (%d rows)' % rows); return 0
    if old != text:
        with open(TABLE, 'w', encoding='latin-1', newline='\n') as f: f.write(text)
    print('amiga/lang_table.h: %d rows%s' % (rows, '' if old != text else ' (unchanged)'))
    return 0

if __name__ == '__main__':
    sys.exit(main())
