// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// Test runner: reads a results file in the format
//   FORMULAINPUT: <base64 formula> OUTPUT: <base64 expected json>
// (one case per line), runs XLParserClone::parseFormulaToJSON on each formula and compares.
// Base64 handling lives only here, the library itself never sees base64.
//
// Usage: run_tests <results.txt> [--show N] [--dump-failures <dir>]

#include <cstdio>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "xlparser_clone.h"

static int B64Val(unsigned char c) {
  if (c >= 'A' && c <= 'Z') return c - 'A';
  if (c >= 'a' && c <= 'z') return c - 'a' + 26;
  if (c >= '0' && c <= '9') return c - '0' + 52;
  if (c == '+') return 62;
  if (c == '/') return 63;
  return -1;
}

// Decodes base64; stops at the first non-base64 character. Sets 'truncated' when the input
// contained a non-base64 marker (the source data truncates some long outputs with an ellipsis).
static std::string B64Decode(const std::string& in, bool& truncated) {
  std::string out;
  unsigned val = 0;
  int bits = -8;
  truncated = false;
  for (unsigned char c : in) {
    if (c == '=') break;
    int d = B64Val(c);
    if (d < 0) { truncated = true; break; }
    val = ((val << 6) | static_cast<unsigned>(d)) & 0xFFFFFFu;
    bits += 6;
    if (bits >= 0) {
      out.push_back(static_cast<char>((val >> bits) & 0xFF));
      bits -= 8;
    }
  }
  return out;
}

int main(int argc, char** argv) {
  if (argc < 2) {
    std::cerr << "usage: run_tests <results.txt> [--show N] [--dump-failures dir]\n";
    return 2;
  }
  int show = 5;
  std::string dumpDir;
  for (int i = 2; i < argc; ++i) {
    std::string a = argv[i];
    if (a == "--show" && i + 1 < argc) show = std::stoi(argv[++i]);
    else if (a == "--dump-failures" && i + 1 < argc) dumpDir = argv[++i];
  }
  std::ifstream f(argv[1], std::ios::binary);
  if (!f) { std::cerr << "cannot open " << argv[1] << "\n"; return 2; }
  std::string line;
  int total = 0, pass = 0, passPrefix = 0, fail = 0, shown = 0;
  while (std::getline(f, line)) {
    if (!line.empty() && line.back() == '\r') line.pop_back();
    if (line.empty()) continue;
    const std::string k1 = "FORMULAINPUT: ", k2 = " OUTPUT: ";
    if (line.compare(0, k1.size(), k1) != 0) continue;
    size_t p = line.find(k2);
    if (p == std::string::npos) continue;
    bool t1, truncated;
    std::string formula = B64Decode(line.substr(k1.size(), p - k1.size()), t1);
    std::string expected = B64Decode(line.substr(p + k2.size()), truncated);
    ++total;
    std::string actual = XLParserClone::parseFormulaToJSON(formula);
    bool ok;
    if (truncated) {
      // Reference output was truncated in the source data: compare the available prefix only.
      size_t n = expected.size();
      while (n > 0 && (static_cast<unsigned char>(expected[n - 1]) & 0xC0) == 0x80) --n;  // drop partial UTF-8
      if (n > 0 && static_cast<unsigned char>(expected[n - 1]) >= 0xC0) --n;
      ok = actual.compare(0, n, expected, 0, n) == 0;
      if (ok) ++passPrefix;
    } else {
      ok = actual == expected;
    }
    if (ok) { ++pass; continue; }
    ++fail;
    if (shown < show) {
      ++shown;
      size_t d = 0;
      while (d < actual.size() && d < expected.size() && actual[d] == expected[d]) ++d;
      std::cout << "FAIL #" << total << " input: " << formula << "\n";
      size_t from = d > 200 ? d - 200 : 0;
      std::cout << "  expected: ..." << expected.substr(from, 400) << "\n";
      std::cout << "  actual:   ..." << actual.substr(from, 400) << "\n";
    }
    if (!dumpDir.empty()) {
      std::ofstream(dumpDir + "/" + std::to_string(total) + ".expected.json") << expected;
      std::ofstream(dumpDir + "/" + std::to_string(total) + ".actual.json") << actual;
      std::ofstream(dumpDir + "/" + std::to_string(total) + ".input.txt") << formula;
    }
  }
  std::cout << "total " << total << ", passed " << pass << " (of which " << passPrefix
            << " compared by prefix because the reference was truncated), failed " << fail << "\n";
  return fail == 0 ? 0 : 1;
}
