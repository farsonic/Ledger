# cases for the functions of core/func_misc.c (same form as cases.py; see cases_eng.py for the
# _xlfn. names and the INDEX checks of array results; LibreOffice reads NETWORKDAYS.INTL and
# WORKDAY.INTL only without the prefix)
C = []
def x(name): return '_xlfn.' + name
H = [[46325], [46327]]
C += [
 ('DAYS360', ['DATE(2011,1,30)', 'DATE(2011,12,31)']), ('DAYS360', ['DATE(2011,1,1)', 'DATE(2011,1,31)']), ('DAYS360', ['DATE(2011,2,28)', 'DATE(2011,3,31)']),
 ('DAYS360', ['DATE(2011,1,30)', 'DATE(2011,12,31)', 'TRUE']), ('DAYS360', ['DATE(2012,2,29)', 'DATE(2012,3,31)']), ('DAYS360', ['DATE(2012,3,31)', 'DATE(2012,2,29)']),
 ('DAYS360', ['DATE(2011,12,31)', 'DATE(2011,1,30)']), ('DAYS360', ['"2024-01-31"', '"2024-03-31"', 'FALSE']), ('DAYS360', ['46322', '46400']), ('DAYS360', ['DATE(2024,2,29)', 'DATE(2024,2,28)']),
 (x('ISOWEEKNUM'), ['DATE(2012,3,9)']), (x('ISOWEEKNUM'), ['DATE(2021,1,1)']), (x('ISOWEEKNUM'), ['DATE(2020,12,31)']), (x('ISOWEEKNUM'), ['DATE(2026,12,31)']),
 (x('ISOWEEKNUM'), ['DATE(2024,12,30)']), (x('ISOWEEKNUM'), ['-1']), (x('ISOWEEKNUM'), ['46322']),
 ('NETWORKDAYS.INTL', ['DATE(2006,1,1)', 'DATE(2006,1,31)']), ('NETWORKDAYS.INTL', ['DATE(2006,2,28)', 'DATE(2006,1,31)']),
 ('NETWORKDAYS.INTL', ['DATE(2006,1,1)', 'DATE(2006,2,1)', '7', [[38719], [38733]]]),
 ('NETWORKDAYS.INTL', ['DATE(2006,1,1)', 'DATE(2006,2,1)', '"0010001"']), ('NETWORKDAYS.INTL', ['46322', '46340', '11']), ('NETWORKDAYS.INTL', ['46322', '46340', '17', H]),
 ('NETWORKDAYS.INTL', ['46322', '46340', '8']), ('NETWORKDAYS.INTL', ['46322', '46340', '"1111111"']), ('NETWORKDAYS.INTL', ['46322', '46340', '"11"']),
 ('WORKDAY.INTL', ['DATE(2012,1,1)', '30', '0']), ('WORKDAY.INTL', ['DATE(2012,1,1)', '90', '11']), ('WORKDAY.INTL', ['DATE(2012,1,1)', '30', '17']),
 ('WORKDAY.INTL', ['46322', '10']), ('WORKDAY.INTL', ['46322', '-7', '2']), ('WORKDAY.INTL', ['46322', '5', '"0000011"', H]), ('WORKDAY.INTL', ['46322', '5', '"1111111"']),
 ('WORKDAY.INTL', ['46322', '0']), ('WORKDAY.INTL', ['46322', '3', '1', H]),
]
# text
C += [
 ('ASC', ['"ＡＢＣ１２３"']), ('ASC', ['"abc"']), (x('DBCS'), ['"abc 1"']), ('JIS', ['"Ab!"']),
 (x('BAHTTEXT'), ['1234']), (x('BAHTTEXT'), ['0']), (x('BAHTTEXT'), ['21']), (x('BAHTTEXT'), ['11']), (x('BAHTTEXT'), ['101']), (x('BAHTTEXT'), ['1000001']),
 (x('BAHTTEXT'), ['0.5']), (x('BAHTTEXT'), ['-12.25']), (x('BAHTTEXT'), ['2000000.01']), (x('BAHTTEXT'), ['123456789.99']), (x('BAHTTEXT'), ['1']),
 ('FINDB', ['"b"', '"abcb"', '3']), ('SEARCHB', ['"C"', '"abc"']), ('LEFTB', ['"Amiga"', '2']), ('RIGHTB', ['"Amiga"', '3']), ('MIDB', ['"Amiga"', '2', '3']),
 ('LENB', ['"Amiga"']), ('REPLACEB', ['"Amiga"', '2', '3', '"xx"']), ('LEFTB', ['"Amiga"']),
 ('PHONETIC', ['"text"']),
 (x('UNICODE'), ['"A"']), (x('UNICODE'), ['"€uro"']), (x('UNICODE'), ['""']), (x('UNICODE'), ['" "']),
 (x('TEXTBEFORE'), ['"Red riding hood"', '" "']), (x('TEXTBEFORE'), ['"Red riding hood"', '" "', '2']), (x('TEXTBEFORE'), ['"Red riding hood"', '" "', '-1']),
 (x('TEXTBEFORE'), ['"Red riding hood"', '"x"']), (x('TEXTBEFORE'), ['"Red riding hood"', '"x"', '1', '0', '0', '"none"']), (x('TEXTBEFORE'), ['"aXbxc"', '"x"', '1', '1']),
 (x('TEXTBEFORE'), ['"a-b"', '"-"', '2', '0', '1']), (x('TEXTBEFORE'), ['"abc"', '""']), (x('TEXTBEFORE'), ['"abc"', '"b"', '0']), (x('TEXTBEFORE'), ['"abc"', '"b"', '5']),
 (x('TEXTBEFORE'), ['"a,b;c"', [['","'], ['";"']], '2']),
 (x('TEXTAFTER'), ['"Red riding hood"', '" "']), (x('TEXTAFTER'), ['"Red riding hood"', '" "', '-1']), (x('TEXTAFTER'), ['"Red riding hood"', '"RIDING"', '1', '1']),
 (x('TEXTAFTER'), ['"abc"', '""']), (x('TEXTAFTER'), ['"abc"', '""', '-1']), (x('TEXTAFTER'), ['"a-b"', '"-"', '-2', '0', '1']), (x('TEXTAFTER'), ['"abc"', '"z"']),
 (x('VALUETOTEXT'), ['"abc"']), (x('VALUETOTEXT'), ['"abc"', '1']), (x('VALUETOTEXT'), ['1.5']), (x('VALUETOTEXT'), ['TRUE', '1']), (x('VALUETOTEXT'), ['1/0']), (x('VALUETOTEXT'), ['1', '2']),
 (x('ARRAYTOTEXT'), [[[1, '"a"'], ['TRUE', 2.5]]]), (x('ARRAYTOTEXT'), [[[1, '"a"'], ['TRUE', 2.5]], '1']), (x('ARRAYTOTEXT'), ['"x"']),
 ('T', ['INDEX(_xlfn.TEXTSPLIT("a,b;c,d",",",";"),2,1)']), ('T', ['INDEX(_xlfn.TEXTSPLIT("a,b;c,d",",",";"),1,2)']),
 ('N', ['ROWS(_xlfn.TEXTSPLIT("a,b;c",",",";"))']), ('N', ['COLUMNS(_xlfn.TEXTSPLIT("a,b;c",",",";"))']), ('ISNA', ['INDEX(_xlfn.TEXTSPLIT("a,b;c",",",";"),2,2)']),
 ('T', ['INDEX(_xlfn.TEXTSPLIT("a,b;c",",",";",FALSE,0,"-"),2,2)']), ('N', ['COLUMNS(_xlfn.TEXTSPLIT("a,,b",","))']), ('N', ['COLUMNS(_xlfn.TEXTSPLIT("a,,b",",",,TRUE))']),
 ('T', ['INDEX(_xlfn.TEXTSPLIT("aXbxc","x",,FALSE,1),1,2)']), ('T', ['INDEX(_xlfn.TEXTSPLIT("a b-c",{" ","-"}),1,3)']), ('N', ['ROWS(_xlfn.TEXTSPLIT("1;2;3",,";"))']),
 ('T', ['_xlfn.TEXTSPLIT("abc","")']),
 (x('REGEXTEST'), ['"abc123"', '"[0-9]+"']), (x('REGEXTEST'), ['"abc"', '"^B"']), (x('REGEXTEST'), ['"abc"', '"^B"', '1']), (x('REGEXTEST'), ['"abc"', '"("']),
 (x('REGEXTEST'), ['"a@b.com"', '"^[\\w.]+@\\w+\\.\\w{2,3}$"']), (x('REGEXTEST'), ['"foobar"', '"foo(?!bar)"']), (x('REGEXTEST'), ['"foobaz"', '"foo(?!bar)"']),
 (x('REGEXEXTRACT'), ['"Tel 555-1234 or 555-9876"', '"\\d{3}-\\d{4}"']), (x('REGEXEXTRACT'), ['"abc"', '"\\d"']), (x('REGEXEXTRACT'), ['"aaa"', '"a+?"']),
 (x('REGEXEXTRACT'), ['"key=value"', '"(\\w+)=(\\w+)"', '0']), ('T', ['INDEX(_xlfn.REGEXEXTRACT("key=value","(\\w+)=(\\w+)",2),1,2)']),
 ('T', ['INDEX(_xlfn.REGEXEXTRACT("a1b22c333","[0-9]+",1),3,1)']), ('N', ['ROWS(_xlfn.REGEXEXTRACT("a1b22c333","[0-9]+",1))']),
 (x('REGEXEXTRACT'), ['"Hello World"', '"WORLD"', '0', '1']), (x('REGEXEXTRACT'), ['"x(y)z"', '"\\((.)\\)"']), (x('REGEXEXTRACT'), ['"2026-10-29"', '"(\\d+)-(\\d+)"', '3']),
 (x('REGEXREPLACE'), ['"a1b22c"', '"[0-9]+"', '"#"']), (x('REGEXREPLACE'), ['"2026-10-22"', '"(\\d+)-(\\d+)-(\\d+)"', '"$3/$2/$1"']),
 (x('REGEXREPLACE'), ['"abc"', '"x*"', '"-"']), (x('REGEXREPLACE'), ['"a1b2"', '"[0-9]"', '"#"', '2']), (x('REGEXREPLACE'), ['"a1b2c3"', '"[0-9]"', '"#"', '-1']),
 (x('REGEXREPLACE'), ['"Hello"', '"l"', '"L"', '0', '1']), (x('REGEXREPLACE'), ['"aAa"', '"a"', '"x"', '0', '1']), (x('REGEXREPLACE'), ['"John Smith"', '"(\\w+) (\\w+)"', '"${2}, $1"']),
 (x('REGEXREPLACE'), ['"a.b.c"', '"\\."', '"$$"']), (x('REGEXREPLACE'), ['"tel: 0123"', '"(?i)TEL"', '"Phone"']), (x('REGEXREPLACE'), ['"aaa"', '"(a)\\1"', '"b"']),
]
# information, web, cube, add-in
C += [
 ('INFO', ['"recalc"']), ('INFO', ['"bogus"']), ('INFO', ['"numfile"']),
 (x('ENCODEURL'), ['"http://contoso.sharepoint.com/Finance/Profit and Loss Statement.xlsx"']), (x('ENCODEURL'), ['"a b&c/é"']), (x('ENCODEURL'), ['"AZaz09-_.~"']),
 (x('WEBSERVICE'), ['"notaurl"']),
 (x('FILTERXML'), ['"<a><b>1</b><b>x</b></a>"', '"//b[2]"']), (x('FILTERXML'), ['"<a><b>1</b><b>2</b></a>"', '"/a/b[1]"']), ('N', ['SUM(_xlfn.FILTERXML("<a><b>1</b><b>2</b></a>","//b"))']),
 (x('FILTERXML'), ['"<r><i n=\'p\'>7</i><i n=\'q\'>8</i></r>"', '"//i[@n=\'q\']"']), (x('FILTERXML'), ['"<r><i n=\'p\'>7</i></r>"', '"//i/@n"']), (x('FILTERXML'), ['"<a><b>1</b></a>"', '"//c"']),
 (x('FILTERXML'), ['"not xml"', '"//a"']), ('T', ['INDEX(_xlfn.FILTERXML("<a><b>p</b><c><b>q</b></c></a>","//b"),2,1)']), (x('FILTERXML'), ['"<a><b>x</b><b>y</b></a>"', '"//b[last()]"']),
 ('CUBEVALUE', ['"conn"']), ('CUBEMEMBER', ['"conn"', '"[x]"']), ('CUBESET', ['"conn"', '"[x]"']), ('CUBESETCOUNT', ['"set"']), ('CUBERANKEDMEMBER', ['"conn"', '"set"', '1']),
 ('CUBEMEMBERPROPERTY', ['"conn"', '"m"', '"p"']), ('CUBEKPIMEMBER', ['"conn"', '"k"', '1']),
 ('EUROCONVERT', ['1.2', '"DEM"', '"EUR"']), ('EUROCONVERT', ['1', '"FRF"', '"DEM"', 'TRUE', '3']), ('EUROCONVERT', ['1', '"FRF"', '"DEM"', 'FALSE', '3']),
 ('EUROCONVERT', ['1', '"FRF"', '"DEM"', 'TRUE']), ('EUROCONVERT', ['100', '"EUR"', '"BEF"']), ('EUROCONVERT', ['100', '"EUR"', '"PTE"']), ('EUROCONVERT', ['1', '"XYZ"', '"EUR"']),
 ('EUROCONVERT', ['1', '"DEM"', '"FRF"', 'FALSE', '2']), ('EUROCONVERT', ['5', '"eur"', '"eur"']), ('EUROCONVERT', ['100', '"ITL"', '"EUR"']),
]
# GridCraft 4c388df's fixes (Excel's answers): FIXED and DOLLAR digits and length limits, single-character
# casing, UNICHAR's noncharacters, MOD's quotient limit, YEARFRAC's 30/360 end-of-February
C += [
 ('FIXED', ['1.005', '20']), ('FIXED', ['1.1', '16']), ('LEN(FIXED(@))', ['1E191', '0']), ('FIXED', ['1E192', '0']),
 ('LEN(DOLLAR(@))', ['1E189', '0']), ('DOLLAR', ['1E190', '0']), ('DOLLAR', ['-0.0001', '0']), ('FIXED', ['123456789012345678', '0']),
 ('LOWER', ['"İ"']), ('PROPER', ['"İSTANBUL"']), ('UPPER', ['"Straße"']), ('UPPER', ['"łódź"']), ('LOWER', ['"ŁÓDŹ"']),
 ('UNICHAR', ['65534']), ('UNICHAR', ['64976']), ('UNICHAR', ['131071']),
 ('MOD', ['1125900000000', '1']), ('MOD', ['1125800000000', '7']),
 ('YEARFRAC', ['DATE(2021,2,28)', 'DATE(2021,3,31)', '0']), ('YEARFRAC', ['DATE(2021,3,31)', 'DATE(2021,2,28)']),
]
# GETPIVOTDATA (GridCraft #284): outside a pivot table it is #REF! (tests/synctest.c checks it on one)
C += [('GETPIVOTDATA', ['"Sales"', 'A1'])]
