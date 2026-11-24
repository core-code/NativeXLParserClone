# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

# Differential-fuzzing corpus generator.
# usage: python3 gen_fuzz.py <count> <seed>   -> writes inputs.b64 (one base64 UTF-8 formula per line)
import random, base64, json, sys, os
rnd = random.Random(int(sys.argv[2]) if len(sys.argv) > 2 else 1)
N = int(sys.argv[1]) if len(sys.argv) > 1 else 20000

cells = ['A1','$A$1','A$1','$B2','XFD1048576','XFD1048577','AAA1','XFE1','XFC12','ZZZ9','AZZ3','WZZ1','XEZ5','a1','A0','A01','b12','IV65536','Q7','X1','XF1','XFA1']
ranges = ['A1:B2','A:B','1:2','$A:$C','ABC:DEF','A1:A','$1:$3','A:A','XFD:XFD','A1:B2:C3','Z:AA','10:20','A1:$B$2']
names = ['foo','_x','\\abc','Tbl1','TRUEX','FALSE1','A1B','A1BC','R1C1','my.name','x?y','Sales_2024','é','straße','名前','ABCD1','TRUE_','A1_','x€',]
sheets = ['Sheet1!','\'My Sheet\'!',"' Sp'!","'it''s'!",'Sheet1:Sheet2!',"'S 1:S 2'!",'[1]Sheet1!','[Book.xlsx]Sheet1!',"'[Book 1.xlsx]Sheet 1'!",
          'C:\\path\\[f.xlsx]S!',"'C:\\a b\\[x.xls]S'!",'[1]!','#REF!','Sheet.1!','S$1!',"'#$%'!",'\\\\srv\\share\\[a.xlsx]S!',"' '!", "'a'b'!", 'Sh!eet!']
srefs = ['Table1[Col]','Table1[[#Headers],[Col]]','Table1[@Col]','[@Col]','Table1[]','Table1[[Col1]:[Col2]]','[#This Row]','Table1[#All]',
         'T[[#Data],[A]:[B]]','T[[#Headers],[#Data],[A]]','T[[#Headers],[#Data],[A]:[B]]','T[@[Col A]]','[Col]','T[A]:T[B]','T[[A],[B]]','[[A]]','T[x.y]','T[a b]']
funcs = ['SUM','IF','INDEX','CHOOSE','OFFSET','INDIRECT','MyFunc','_xll.Func',"'my.xla'!Fn",'LAMBDA','BETA.DIST','BETAXDIST','R','C','r','c','ROUND','LOG10','IFERROR','sum','Sum','XLOOKUP','A1','_xlfn.CONCAT','F.DIST.RT','N','T','NOW','Q×','√x','¡x']
nums = ['1','1.5','.5','1e5','1E+5','1e-5','1e','1e+','1.2.3','00012','99999999999999999999','1e999','0.0','5.','1.e5','1e5e5','12abc','3_','0x1F','1,5','2147483648','٣']
strs = ['"a"','""','"a""b"','"unterminated','"x y"','"""','"a"b"','"\u2028"','"tab\there"','"\\"']
consts = nums + strs + ['TRUE','FALSE','true','#N/A','#DIV/0!','#NULL!','#NAME?','#VALUE!','#NUM!','#REF!','#SPILL!','#GETTING_DATA','#n/a']
arrays = ['{1,2;3,4}','{-1,+2}','{"a",#N/A}','{#REF!}','{1}','{}','{1;2;3}','{TRUE,FALSE}','{1,,2}','{--1}','{1+2}','{A1}','{=A1}']
binops = ['+','-','*','/','^','&','=','<','>','<>','<=','>=',':',' ',',','  ']
ws = ['', ' ', '  ', '\t', '\n', '\r\n', '\r', '\u00a0', '\v']

def ref(d):
    r = rnd.random()
    if r < 0.3: return rnd.choice(cells)
    if r < 0.45: return rnd.choice(ranges)
    if r < 0.6: return rnd.choice(names)
    if r < 0.75: return rnd.choice(sheets) + rnd.choice(cells + ranges + names)
    if r < 0.9: return rnd.choice(srefs)
    return rnd.choice(['_xlnm.Print_Area', '[1]!\'topic\'', '[1]!Name', '(' + ref(d+1) + ')', '(' + ref(d+1) + ',' + ref(d+1) + ')'])

def expr(d=0):
    r = rnd.random()
    if d > 4 or r < 0.25:
        return rnd.choice([ref(d), rnd.choice(consts), rnd.choice(arrays)])
    if r < 0.45:
        f = rnd.choice(funcs)
        nargs = rnd.choice([0, 1, 1, 2, 3])
        args = []
        for _ in range(nargs):
            args.append('' if rnd.random() < 0.1 else expr(d + 1))
        sep = rnd.choice([',', ',', ', ', ';'])
        tail = '(' + rnd.choice(['1', 'A1', '']) + ')' if rnd.random() < 0.05 else ''
        return f + '(' + sep.join(args) + ')' + tail
    if r < 0.75:
        return expr(d + 1) + rnd.choice(ws[:3]) + rnd.choice(binops) + rnd.choice(ws[:3]) + expr(d + 1)
    if r < 0.85:
        return rnd.choice(['-', '+', '--', '@', '-+']) + expr(d + 1)
    if r < 0.9:
        return expr(d + 1) + '%'
    return '(' + expr(d + 1) + ')'

def wrap(e):
    r = rnd.random()
    if r < 0.15: return '=' + e
    if r < 0.18: return '{=' + e + '}'
    if r < 0.2: return ' ' + e + ' '
    return e

alphabet = list('ABCxyz019$:!,;()[]{}"\'#@+-*/^&=<>%. _\\\t\n?€éß') + ['\u00a0', '\u00ad', '中', '😀', '\u2028', '\x01']

def mutate(s):
    s = list(s)
    for _ in range(rnd.randint(1, 3)):
        op = rnd.random()
        pos = rnd.randint(0, len(s))
        if op < 0.4 and s:
            del s[min(pos, len(s) - 1)]
        elif op < 0.8:
            s.insert(pos, rnd.choice(alphabet))
        elif s:
            s[min(pos, len(s) - 1)] = rnd.choice(alphabet)
    return ''.join(s)

def load_seeds():
    # seed formulas = the inputs of tests/data/results.txt
    path = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..', 'tests', 'data', 'results.txt')
    seeds = []
    for line in open(path, encoding='utf-8'):
        if line.startswith('FORMULAINPUT: '):
            seeds.append(base64.b64decode(line[len('FORMULAINPUT: '):].split(' OUTPUT: ')[0]).decode('utf-8'))
    return seeds
base = load_seeds()
out = []
for i in range(N):
    r = rnd.random()
    if r < 0.55: f = wrap(expr())
    elif r < 0.75: f = mutate(wrap(expr()))
    elif r < 0.9: f = mutate(rnd.choice(base))
    else: f = ''.join(rnd.choice(alphabet) for _ in range(rnd.randint(0, 12)))
    out.append(f)
with open('inputs.b64', 'w') as fh:
    for f in out:
        fh.write(base64.b64encode(f.encode('utf-8', 'surrogatepass')).decode() + '\n')
print(len(out))
