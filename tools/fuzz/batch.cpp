// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// Batch driver for differential testing: base64 formulas in (one per line) -> base64 JSON out.
// usage: batch inputs.b64 cpp_out.b64

#include <fstream>
#include <iostream>
#include <string>
#include <cstring>
#include "xlparser_clone.h"
static const char* kB = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
static std::string Dec(const std::string& in) {
  std::string out; int val = 0, bits = -8;
  for (unsigned char c : in) { const char* p = c ? strchr(kB, c) : nullptr; if (!p) break; val = (val << 6) | int(p - kB); bits += 6; if (bits >= 0) { out.push_back(char((val >> bits) & 0xFF)); bits -= 8; } }
  return out;
}
static std::string Enc(const std::string& in) {
  std::string out; int val = 0, bits = -6;
  for (unsigned char c : in) { val = (val << 8) + c; bits += 8; while (bits >= 0) { out.push_back(kB[(val >> bits) & 0x3F]); bits -= 6; } }
  if (bits > -6) out.push_back(kB[((val << 8) >> (bits + 8)) & 0x3F]);
  while (out.size() % 4) out.push_back('=');
  return out;
}
int main(int argc, char** argv) {
  std::ifstream in(argv[1]); std::ofstream out(argv[2]); std::string line;
  while (std::getline(in, line)) out << Enc(XLParserClone::parseFormulaToJSON(Dec(line))) << "\n";
}
