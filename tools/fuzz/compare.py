# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

# Compares cpp_out.b64 (XLParserClone) with ref_out.b64 (C# reference) for inputs.b64.
# usage: python3 compare.py [number_of_mismatches_to_show]
import base64, sys, collections
inp = [base64.b64decode(l.strip()).decode('utf-8', 'replace') for l in open('inputs.b64')]
a = [base64.b64decode(l.strip()).decode('utf-8', 'replace') for l in open('cpp_out.b64')]
b = [base64.b64decode(l.strip()).decode('utf-8', 'replace') for l in open('ref_out.b64')]
assert len(inp) == len(a) == len(b), (len(inp), len(a), len(b))
bad = [(i, x, y, z) for i, (x, y, z) in enumerate(zip(inp, a, b)) if y != z]
ok_err = sum(1 for z in b if z.startswith('{"error"'))
exc = sum(1 for z in b if z.startswith('EXCEPTION'))
print('cases', len(inp), 'mismatches', len(bad), '| ref errors', ok_err, 'ref exceptions', exc)
for i, x, y, z in bad[:int(sys.argv[1]) if len(sys.argv) > 1 else 8]:
    print('---', i, repr(x))
    k = 0
    while k < min(len(y), len(z)) and y[k] == z[k]: k += 1
    print(' cpp:', repr(y[max(0, k - 150):k + 150]))
    print(' ref:', repr(z[max(0, k - 150):k + 150]))
