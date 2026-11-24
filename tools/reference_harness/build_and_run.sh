#!/bin/sh
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

# Builds the C# reference oracle: the real Irony 1.2.0 sources + XLParser's v139 web-demo grammar,
# with a Main that reproduces app/XLParser.Web/Parse.cs (ParseToJson). Requires Mono (mcs) and git.
#   usage: build_and_run.sh <workdir>           -> builds <workdir>/xlref.exe
#          mono <workdir>/xlref.exe in.b64 out.b64   (one base64 UTF-8 formula / JSON per line)
set -e
W=${1:-./refwork}; mkdir -p "$W"; HERE=$(cd "$(dirname "$0")" && pwd); cd "$W"
[ -d Irony ] || git clone -q https://github.com/IronyProject/Irony.git
[ -d XLParser ] || git clone -q https://github.com/spreadsheetlab/XLParser.git
rm -rf irony120 v139 && mkdir irony120 v139
(cd Irony && git archive v1.2.0 Irony) | tar -x -C irony120
for f in ExcelFormulaGrammar.cs ExcelFormulaParser.cs ParserReference.cs PrefixInfo.cs; do
  (cd XLParser && git show HEAD:app/XLParser.Web/XLParserVersions/v139/$f) > v139/$f; done
(cd XLParser && git show HEAD:app/XLParser.Web/Resources/ExcelBuiltinFunctionList-v139.txt) > v139/functions.txt
sed -i "s|var resource = Properties.Resources.ExcelBuiltinFunctionList_v139;|var resource = System.IO.File.ReadAllText(\"$PWD/v139/functions.txt\");|" v139/ExcelFormulaGrammar.cs
python3 - <<'PY'
import xml.etree.ElementTree as ET
t = ET.parse('irony120/Irony/Resources.resx')
out = ['namespace Irony {', '  internal static class Resources {', '    public static System.Globalization.CultureInfo Culture;']
for d in t.getroot().findall('data'):
    v = d.find('value')
    if v is None: continue
    s = (v.text or '').replace('\\', '\\\\').replace('"', '\\"').replace('\r', '\\r').replace('\n', '\\n')
    out.append('    public static string %s { get { return "%s"; } }' % (d.get('name'), s))
open('Resources.cs', 'w').write('\n'.join(out + ['  }', '}']))
PY
SRCS=$(find irony120/Irony -name '*.cs' ! -name 'Resources.Designer.cs' ! -path '*/Properties/*')
mcs -nologo -langversion:7.2 -optimize+ -target:exe -out:xlref.exe -r:System.Numerics.dll -r:System.Core.dll \
    -r:System.Data.dll -r:System.Xml.dll $SRCS Resources.cs v139/*.cs "$HERE/Program.cs" 2>&1 | grep -v warning || true
ls -la xlref.exe
