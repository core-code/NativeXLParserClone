// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// Reference harness: replicates XLParser.Web Parse.cs (ParseToJson) using the real Irony 1.2.0 sources
// and the XLParser v139 grammar. Input: file with one base64(UTF-8 formula) per line.
// Output: one base64(UTF-8 json) per line.
using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Text;
using Irony.Parsing;
using XLParser.Web.XLParserVersions.v139;

static class Program {
    static string Esc(string s) {
        var sb = new StringBuilder("\"");
        foreach (char c in s) {
            switch (c) {
                case '"': sb.Append("\\\""); break;
                case '\\': sb.Append("\\\\"); break;
                case '\n': sb.Append("\\n"); break;
                case '\r': sb.Append("\\r"); break;
                case '\t': sb.Append("\\t"); break;
                case '\f': sb.Append("\\f"); break;
                case '\b': sb.Append("\\b"); break;
                default:
                    if (c < 0x20 || c == (char)0x85 || c == (char)0x2028 || c == (char)0x2029) sb.Append("\\u" + ((int)c).ToString("x4"));
                    else sb.Append(c);
                    break;
            }
        }
        return sb.Append('"').ToString();
    }
    static string NodeText(ParseTreeNode node) {
        if (node.Term is NonTerminal) return node.Term.Name;
        return node.Term.Name.Length <= 2 ? node.Print() : $"{node.Term.Name}[\"{node.Print()}\"]";
    }
    static void Write(StringBuilder sb, ParseTreeNode node, int indent) {
        string pad = new string(' ', indent + 2);
        sb.Append("{\n").Append(pad).Append("\"name\": ").Append(Esc(NodeText(node)));
        if (node.ChildNodes.Count > 0) {
            sb.Append(",\n").Append(pad).Append("\"children\": [\n");
            for (int i = 0; i < node.ChildNodes.Count; i++) {
                sb.Append(new string(' ', indent + 4));
                Write(sb, node.ChildNodes[i], indent + 4);
                if (i + 1 < node.ChildNodes.Count) sb.Append(",");
                sb.Append("\n");
            }
            sb.Append(pad).Append("]");
        }
        sb.Append("\n").Append(new string(' ', indent)).Append("}");
    }
    static Parser errParser;
    static string ParseToJson(string formula) {
        ParseTreeNode root;
        try {
            root = ExcelFormulaParser.Parse(formula);
        } catch (ArgumentException) {
            if (errParser == null) errParser = new Parser(new ExcelFormulaGrammar());
            ParseTree r = errParser.Parse(formula);
            var m = r.ParserMessages.FirstOrDefault();
            string msg = m == null ? "null" :
                "{\"level\":" + Esc(m.Level.ToString()) + ",\"line\":" + (m.Location.Line + 1) + ",\"column\":" + (m.Location.Column + 1) + ",\"msg\":" + Esc(m.Message) + "}";
            return "{\"error\":\"Parse error\",\"formula\":" + Esc(formula) + ",\"message\":" + msg + "}";
        }
        var sb = new StringBuilder();
        Write(sb, root, 0);
        return sb.ToString();
    }
    static int Main(string[] args) {
        var utf8 = new UTF8Encoding(false);
        using (var outp = new StreamWriter(args[1], false, utf8)) {
            foreach (var line in File.ReadLines(args[0])) {
                string formula = utf8.GetString(Convert.FromBase64String(line.Trim()));
                string res;
                try { res = ParseToJson(formula); }
                catch (Exception e) { res = "EXCEPTION " + e.GetType().Name + ": " + e.Message; }
                outp.WriteLine(Convert.ToBase64String(utf8.GetBytes(res)));
            }
        }
        return 0;
    }
}
