// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// XLParserClone - native C++ re-implementation of the XLParser Excel formula parser.
//
// Produces the same JSON parse-tree output as the XLParser web demo (Parse.json endpoint)
// for XLParser v1.3.9 running on Irony 1.2.0.
//
// Dependencies: C++17 standard library only.

#ifndef XLPARSER_CLONE_H
#define XLPARSER_CLONE_H

#include <string>

namespace XLParserClone {

// Parses an Excel formula and returns a JSON document describing its parse tree.
//
//  * input:  the formula text, UTF-8 encoded (with or without a leading '=').
//  * return: UTF-8 encoded JSON.
//      - On success: an indented tree of {"name": ..., "children": [...]} objects.
//      - On a parse error: {"error":"Parse error","formula":...,"message":{"level":"Error",
//        "line":L,"column":C,"msg":"..."}}
//
// The function is thread-safe and never throws.
std::string parseFormulaToJSON(const std::string& input);

}  // namespace XLParserClone

#endif  // XLPARSER_CLONE_H
