// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
//
// Third-party material in this file (see also THIRD_PARTY_NOTICES.md):
//
// * XLParser v1.3.9 (https://github.com/spreadsheetlab/XLParser), Mozilla Public License 2.0.
//   The grammar (rules, terminal names, priorities and regular expressions), the JSON output format
//   and the quoted-sheet whitespace handling are derived from XLParser as shipped in the XLParser web
//   demo (app/XLParser.Web/XLParserVersions/v139). This file is therefore distributed under the MPL 2.0.
//
// * Irony 1.2.0 (https://github.com/IronyProject/Irony), MIT License.
//   The grammar-data, LALR(1) table construction, scanner, terminal, hint, parser-action and
//   parser-runtime code is a C++ translation of Irony's C# sources. Irony's source files also carry
//   the notice "Copyright (c) Roman Ivantsov". Irony's license:
//
//     MIT License
//
//     Copyright (c) 2019 Irony Project (https://github.com/IronyProject)
//
//     Permission is hereby granted, free of charge, to any person obtaining a copy of this software
//     and associated documentation files (the "Software"), to deal in the Software without restriction,
//     including without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense,
//     and/or sell copies of the Software, and to permit persons to whom the Software is furnished to do so,
//     subject to the following conditions:
//
//     The above copyright notice and this permission notice shall be included in all copies or
//     substantial portions of the Software.
//
//     THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,
//     INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE
//     AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,
//     DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
//     OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
//
// * Microsoft .NET Framework Reference Source (https://github.com/microsoft/referencesource),
//   MIT License. NetIntroSort is a translation of ArraySortHelper<T>.IntrospectiveSort
//   (mscorlib/system/collections/generic/arraysorthelper.cs). Its license:
//
//     The MIT License (MIT)
//
//     Copyright (c) Microsoft Corporation
//
//     Permission is hereby granted, free of charge, to any person obtaining a copy
//     of this software and associated documentation files (the "Software"), to deal
//     in the Software without restriction, including without limitation the rights
//     to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
//     copies of the Software, and to permit persons to whom the Software is
//     furnished to do so, subject to the following conditions:
//
//     The above copyright notice and this permission notice shall be included in all
//     copies or substantial portions of the Software.
//
//     THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
//     IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
//     FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
//     AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
//     LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
//     OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
//     SOFTWARE.
//
// * Unicode Character Database: the tables included from unicode_tables.inc are derived from it
//   (Unicode License v3, Copyright (c) 1991-2023 Unicode, Inc.; full text in THIRD_PARTY_NOTICES.md).
//

// XLParserClone - native C++ re-implementation of XLParser (v1.3.9 grammar, as served by the
// XLParser web demo) on top of a faithful port of the relevant parts of Irony 1.2.0.
//
// Why a port and not a hand-written parser: the reference output contains Irony-specific details
// (LALR conflict resolution through operator precedence and hints, scanner/parser coupling,
// "expected token" lists of syntax errors whose order depends on the LALR construction order).
// Reproducing these exactly requires building the same LALR automaton in the same way.
//
// Only the C++17 standard library is used.

#include "xlparser_clone.h"

#include <algorithm>
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace XLParserClone {
namespace {

#include "excel_functions.inc"
#include "unicode_tables.inc"

using ustr = std::u16string;

// =====================================================================================
//  Unicode helpers emulating the .NET Framework char APIs used by Irony / XLParser
// =====================================================================================

enum UCat {
  kLu = 0, kLl, kLt, kLm, kLo, kMn, kMc, kMe, kNd, kNl, kNo, kZs, kZl, kZp, kCc, kCf, kCs, kCo,
  kPc, kPd, kPs, kPe, kPi, kPf, kPo, kSm, kSc, kSk, kSo, kCn
};

int GetCategory(char16_t c) {
  size_t lo = 0, hi = sizeof(kCatRuns) / sizeof(kCatRuns[0]);
  while (hi - lo > 1) {
    size_t mid = (lo + hi) / 2;
    if ((kCatRuns[mid] >> 8) <= static_cast<uint32_t>(c)) lo = mid; else hi = mid;
  }
  return static_cast<int>(kCatRuns[lo] & 0xFF);
}

// Culture-style simple lowercase mapping (what TextInfo.ToLower does for a single char).
char16_t ToLowerCulture(char16_t c) {
  if (c < 128) return (c >= 'A' && c <= 'Z') ? static_cast<char16_t>(c + 32) : c;
  size_t n = sizeof(kLowerMap) / sizeof(kLowerMap[0]);
  size_t lo = 0, hi = n;
  while (lo < hi) {
    size_t mid = (lo + hi) / 2;
    if ((kLowerMap[mid] >> 16) < static_cast<uint32_t>(c)) lo = mid + 1; else hi = mid;
  }
  if (lo < n && (kLowerMap[lo] >> 16) == static_cast<uint32_t>(c)) return static_cast<char16_t>(kLowerMap[lo] & 0xFFFF);
  return c;
}

char16_t ToUpperAscii(char16_t c) { return (c >= 'a' && c <= 'z') ? static_cast<char16_t>(c - 32) : c; }

bool NetIsLetter(char16_t c) { return GetCategory(c) <= kLo; }
bool NetIsDigit(char16_t c) { return GetCategory(c) == kNd; }
bool NetIsLetterOrDigit(char16_t c) { int k = GetCategory(c); return k <= kLo || k == kNd; }
bool NetIsWhiteSpace(char16_t c) {
  if (c == ' ' || (c >= 0x09 && c <= 0x0D) || c == 0xA0 || c == 0x85) return true;
  if (c < 256) return false;
  int k = GetCategory(c);
  return k == kZs || k == kZl || k == kZp;
}

// UTF-8 -> UTF-16 (.NET strings are UTF-16; positions/columns are counted in UTF-16 code units).
// Malformed sequences are replaced by U+FFFD using the "maximal subpart" rule, like .NET's decoder.
ustr Utf8ToUtf16(const std::string& s) {
  ustr out;
  out.reserve(s.size());
  const size_t n = s.size();
  size_t i = 0;
  while (i < n) {
    const unsigned char c = static_cast<unsigned char>(s[i]);
    if (c < 0x80) { out.push_back(c); ++i; continue; }
    size_t len;
    uint32_t cp;
    unsigned char lo2 = 0x80, hi2 = 0xBF;  // allowed range of the second byte
    if (c >= 0xC2 && c <= 0xDF) { len = 2; cp = c & 0x1F; }
    else if (c >= 0xE0 && c <= 0xEF) {
      len = 3; cp = c & 0x0F;
      if (c == 0xE0) lo2 = 0xA0;       // no overlongs
      else if (c == 0xED) hi2 = 0x9F;  // no surrogates
    } else if (c >= 0xF0 && c <= 0xF4) {
      len = 4; cp = c & 0x07;
      if (c == 0xF0) lo2 = 0x90;
      else if (c == 0xF4) hi2 = 0x8F;
    } else { out.push_back(0xFFFD); ++i; continue; }
    size_t k = 1;
    for (; k < len && i + k < n; ++k) {
      const unsigned char cc = static_cast<unsigned char>(s[i + k]);
      const unsigned char lo = (k == 1) ? lo2 : 0x80, hi = (k == 1) ? hi2 : 0xBF;
      if (cc < lo || cc > hi) break;
      cp = (cp << 6) | (cc & 0x3F);
    }
    if (k < len) { out.push_back(0xFFFD); i += k; continue; }  // truncated / invalid: one U+FFFD per maximal subpart
    if (cp >= 0x10000) {
      cp -= 0x10000;
      out.push_back(static_cast<char16_t>(0xD800 + (cp >> 10)));
      out.push_back(static_cast<char16_t>(0xDC00 + (cp & 0x3FF)));
    } else {
      out.push_back(static_cast<char16_t>(cp));
    }
    i += len;
  }
  return out;
}

void AppendUtf8(std::string& out, uint32_t cp) {
  if (cp < 0x80) out.push_back(static_cast<char>(cp));
  else if (cp < 0x800) { out.push_back(static_cast<char>(0xC0 | (cp >> 6))); out.push_back(static_cast<char>(0x80 | (cp & 0x3F))); }
  else if (cp < 0x10000) {
    out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
    out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
    out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
  } else {
    out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
    out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
    out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
    out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
  }
}

std::string Utf16ToUtf8(const ustr& s) {
  std::string out;
  out.reserve(s.size());
  for (size_t i = 0; i < s.size(); ++i) {
    uint32_t c = s[i];
    if (c >= 0xD800 && c <= 0xDBFF && i + 1 < s.size() && s[i + 1] >= 0xDC00 && s[i + 1] <= 0xDFFF) {
      uint32_t cp = 0x10000 + ((c - 0xD800) << 10) + (s[i + 1] - 0xDC00);
      AppendUtf8(out, cp);
      ++i;
    } else if (c >= 0xD800 && c <= 0xDFFF) {
      AppendUtf8(out, 0xFFFD);  // lone surrogate cannot be encoded in UTF-8
    } else {
      AppendUtf8(out, c);
    }
  }
  return out;
}

ustr U(const char* ascii) {
  ustr r;
  while (*ascii) r.push_back(static_cast<unsigned char>(*ascii++));
  return r;
}

// =====================================================================================
//  NetSet: an insertion-ordered set emulating System.Collections.Generic.HashSet<T>
//  enumeration order (including slot reuse after Remove), which Irony relies on.
// =====================================================================================

template <class T>
class NetSet {
 public:
  bool Add(const T& v) {
    if (index_.count(v)) return false;
    int i;
    if (!free_.empty()) {
      i = free_.back();
      free_.pop_back();
      slots_[i] = v;
      used_[i] = 1;
    } else {
      i = static_cast<int>(slots_.size());
      slots_.push_back(v);
      used_.push_back(1);
    }
    index_[v] = i;
    ++count_;
    return true;
  }
  bool Remove(const T& v) {
    auto it = index_.find(v);
    if (it == index_.end()) return false;
    int i = it->second;
    index_.erase(it);
    used_[i] = 0;
    --count_;
    if (count_ == 0) {
      slots_.clear();
      used_.clear();
      free_.clear();
    } else {
      free_.push_back(i);
    }
    return true;
  }
  bool Contains(const T& v) const { return index_.count(v) != 0; }
  int Count() const { return count_; }
  void Clear() { slots_.clear(); used_.clear(); free_.clear(); index_.clear(); count_ = 0; }
  std::vector<T> ToList() const {
    std::vector<T> r;
    r.reserve(count_);
    for (size_t i = 0; i < slots_.size(); ++i)
      if (used_[i]) r.push_back(slots_[i]);
    return r;
  }
  void UnionWith(const NetSet& other) {
    for (const T& v : other.ToList()) Add(v);
  }
  T First() const {
    for (size_t i = 0; i < slots_.size(); ++i)
      if (used_[i]) return slots_[i];
    return T();
  }

 private:
  std::vector<T> slots_;
  std::vector<char> used_;
  std::vector<int> free_;
  std::unordered_map<T, int> index_;
  int count_ = 0;
};

// =====================================================================================
//  Small backtracking regex engine for the .NET regex subset used by the grammar.
//  Leftmost-first (priority) semantics like .NET; memoized on (pc, pos) so it is linear.
// =====================================================================================

struct CharClass {
  bool negate = false;
  std::vector<std::pair<char16_t, char16_t>> ranges;
  uint32_t catMask = 0;  // bit per UnicodeCategory
  bool Matches(char16_t c) const {
    bool in = false;
    for (const auto& r : ranges)
      if (c >= r.first && c <= r.second) { in = true; break; }
    if (!in && catMask != 0 && ((catMask >> GetCategory(c)) & 1u)) in = true;
    return negate ? !in : in;
  }
};

class Regex {
 public:
  Regex() = default;
  Regex(const ustr& pattern, bool ignoreCase) : pat_(pattern), ic_(ignoreCase) {
    pos_ = 0;
    Node* root = ParseAlt();
    Emit(root);
    Inst m;
    m.op = kMatch;
    prog_.push_back(m);
    ComputeFirstSet();
  }

  // Returns the match length of the pattern anchored at 'start', or -1 when there is no match.
  int MatchAt(const ustr& text, int start) const {
    const int n = static_cast<int>(text.size());
    if (!canMatchEmpty_) {  // cheap rejection on the first character
      if (start >= n) return -1;
      char16_t c = Fold(text[start]);
      bool possible = std::binary_search(firstChars_.begin(), firstChars_.end(), c);
      for (size_t i = 0; !possible && i < firstClasses_.size(); ++i) possible = classes_[firstClasses_[i]].Matches(c);
      if (!possible) return -1;
    }
    thread_local VisitedSet visited;
    thread_local std::vector<std::pair<int, int>> stack;
    visited.Reset();
    stack.clear();
    stack.emplace_back(0, start);
    while (!stack.empty()) {
      int pc = stack.back().first;
      int pos = stack.back().second;
      stack.pop_back();
      for (;;) {
        uint64_t key = (static_cast<uint64_t>(pc) << 32) | static_cast<uint32_t>(pos);
        if (!visited.Insert(key)) break;
        const Inst& in = prog_[pc];
        if (in.op == kMatch) return pos - start;
        if (in.op == kChar) {
          if (pos < n && Fold(text[pos]) == in.c) { ++pc; ++pos; continue; }
          break;
        }
        if (in.op == kClass) {
          if (pos < n && classes_[in.x].Matches(Fold(text[pos]))) { ++pc; ++pos; continue; }
          break;
        }
        if (in.op == kJmp) { pc = in.x; continue; }
        if (in.op == kSplit) { stack.emplace_back(in.y, pos); pc = in.x; continue; }
        break;
      }
    }
    return -1;
  }

 private:
  // Reusable open-addressing set of (pc, pos) keys; cleared in O(1) through generation stamps.
  class VisitedSet {
   public:
    void Reset() {
      if (++gen_ == 0) {  // stamp wrap-around: really clear
        std::fill(stamps_.begin(), stamps_.end(), 0u);
        gen_ = 1;
      }
      size_ = 0;
    }
    bool Insert(uint64_t key) {
      if (keys_.empty() || (size_ + 1) * 2 > keys_.size()) Grow();
      size_t mask = keys_.size() - 1;
      uint64_t x = key;
      x ^= x >> 33;
      x *= 0xff51afd7ed558ccdull;
      x ^= x >> 33;
      x *= 0xc4ceb9fe1a85ec53ull;
      x ^= x >> 33;
      size_t h = static_cast<size_t>(x) & mask;
      while (stamps_[h] == gen_) {
        if (keys_[h] == key) return false;
        h = (h + 1) & mask;
      }
      stamps_[h] = gen_;
      keys_[h] = key;
      ++size_;
      return true;
    }

   private:
    void Grow() {
      std::vector<uint64_t> oldKeys;
      std::vector<uint32_t> oldStamps;
      oldKeys.swap(keys_);
      oldStamps.swap(stamps_);
      size_t cap = oldKeys.empty() ? 256 : oldKeys.size() * 2;
      keys_.assign(cap, 0);
      stamps_.assign(cap, 0u);
      uint32_t g = gen_;
      size_ = 0;
      for (size_t i = 0; i < oldKeys.size(); ++i)
        if (oldStamps[i] == g) Insert(oldKeys[i]);
    }
    std::vector<uint64_t> keys_;
    std::vector<uint32_t> stamps_;
    uint32_t gen_ = 1;
    size_t size_ = 0;
  };

  // Collects the instructions that can consume the first character (epsilon closure of pc 0).
  void ComputeFirstSet() {
    std::vector<char> seen(prog_.size(), 0);
    std::vector<int> work{0};
    while (!work.empty()) {
      int pc = work.back();
      work.pop_back();
      if (seen[pc]) continue;
      seen[pc] = 1;
      const Inst& in = prog_[pc];
      switch (in.op) {
        case kChar: firstChars_.push_back(in.c); break;
        case kClass: firstClasses_.push_back(in.x); break;
        case kJmp: work.push_back(in.x); break;
        case kSplit: work.push_back(in.x); work.push_back(in.y); break;
        case kMatch: canMatchEmpty_ = true; break;
      }
    }
    std::sort(firstChars_.begin(), firstChars_.end());
    firstChars_.erase(std::unique(firstChars_.begin(), firstChars_.end()), firstChars_.end());
  }

  enum Op { kChar, kClass, kSplit, kJmp, kMatch };
  struct Inst {
    Op op = kMatch;
    char16_t c = 0;
    int x = 0, y = 0;
  };
  enum NodeKind { nChar, nClass, nConcat, nAlt, nRepeat, nEmpty };
  struct Node {
    NodeKind kind = nEmpty;
    char16_t c = 0;
    int cls = -1;
    int min = 0, max = 0;  // max = -1: unbounded
    std::vector<Node*> kids;
  };

  char16_t Fold(char16_t c) const { return ic_ ? ToLowerCulture(c) : c; }

  Node* NewNode(NodeKind k) {
    nodes_.emplace_back(new Node());
    nodes_.back()->kind = k;
    return nodes_.back().get();
  }
  bool More() const { return pos_ < pat_.size(); }
  char16_t Peek() const { return pat_[pos_]; }

  Node* ParseAlt() {
    std::vector<Node*> alts;
    alts.push_back(ParseConcat());
    while (More() && Peek() == '|') {
      ++pos_;
      alts.push_back(ParseConcat());
    }
    if (alts.size() == 1) return alts[0];
    Node* a = NewNode(nAlt);
    a->kids = alts;
    return a;
  }
  Node* ParseConcat() {
    Node* c = NewNode(nConcat);
    while (More() && Peek() != '|' && Peek() != ')') {
      Node* atom = ParseAtom();
      if (atom == nullptr) continue;
      atom = ParseQuant(atom);
      c->kids.push_back(atom);
    }
    return c;
  }
  int ParseInt() {
    int v = 0;
    while (More() && Peek() >= '0' && Peek() <= '9') { v = v * 10 + (Peek() - '0'); ++pos_; }
    return v;
  }
  Node* ParseQuant(Node* atom) {
    for (;;) {
      if (!More()) return atom;
      char16_t q = Peek();
      int mn, mx;
      if (q == '?') { mn = 0; mx = 1; ++pos_; }
      else if (q == '*') { mn = 0; mx = -1; ++pos_; }
      else if (q == '+') { mn = 1; mx = -1; ++pos_; }
      else if (q == '{') {
        ++pos_;
        mn = ParseInt();
        mx = mn;
        if (More() && Peek() == ',') {
          ++pos_;
          if (More() && Peek() == '}') mx = -1; else mx = ParseInt();
        }
        ++pos_;  // '}'
      } else {
        return atom;
      }
      Node* r = NewNode(nRepeat);
      r->min = mn;
      r->max = mx;
      r->kids.push_back(atom);
      atom = r;
    }
  }
  int AddClass(const CharClass& cc) {
    CharClass c = cc;
    if (ic_) {
      // .NET RegexCharClass.AddLowercase: add the lowercase image of every char in the ranges.
      std::vector<std::pair<char16_t, char16_t>> extra;
      for (const auto& r : c.ranges) {
        for (uint32_t ch = r.first; ch <= r.second; ++ch) {
          char16_t l = ToLowerCulture(static_cast<char16_t>(ch));
          if (l != ch) extra.emplace_back(l, l);
        }
      }
      for (const auto& e : extra) c.ranges.push_back(e);
    }
    classes_.push_back(c);
    return static_cast<int>(classes_.size()) - 1;
  }
  uint32_t ParseCategoryName() {
    // after "\p", expects "{Name}"
    ++pos_;  // '{'
    ustr name;
    while (More() && Peek() != '}') name.push_back(pat_[pos_++]);
    ++pos_;  // '}'
    std::string n(name.begin(), name.end());
    if (n == "L") return (1u << kLu) | (1u << kLl) | (1u << kLt) | (1u << kLm) | (1u << kLo);
    if (n == "Lu") return 1u << kLu;
    if (n == "Ll") return 1u << kLl;
    if (n == "Lt") return 1u << kLt;
    if (n == "Lm") return 1u << kLm;
    if (n == "Lo") return 1u << kLo;
    if (n == "Pc") return 1u << kPc;
    if (n == "Nd") return 1u << kNd;
    if (n == "Mn") return 1u << kMn;
    if (n == "N") return (1u << kNd) | (1u << kNl) | (1u << kNo);
    return 0;
  }
  static uint32_t WordMask() {
    return (1u << kLu) | (1u << kLl) | (1u << kLt) | (1u << kLm) | (1u << kLo) | (1u << kMn) | (1u << kNd) | (1u << kPc);
  }
  Node* ParseAtom() {
    char16_t c = pat_[pos_++];
    if (c == '(') {
      if (More() && Peek() == '?') {
        pos_ += 2;  // "?:"
      }
      Node* inner = ParseAlt();
      ++pos_;  // ')'
      return inner;
    }
    if (c == '[') return ParseClass();
    if (c == '.') {
      CharClass cc;
      cc.negate = true;
      cc.ranges.emplace_back(u'\n', u'\n');
      Node* nd = NewNode(nClass);
      nd->cls = AddClass(cc);
      return nd;
    }
    if (c == '\\') {
      char16_t e = pat_[pos_++];
      if (e == 'G') return nullptr;  // start anchor; matching is always anchored at the start position
      if (e == 'w' || e == 'p') {
        CharClass cc;
        cc.catMask = (e == 'w') ? WordMask() : ParseCategoryName();
        Node* nd = NewNode(nClass);
        nd->cls = AddClass(cc);
        return nd;
      }
      if (e == 'd') {
        CharClass cc;
        cc.catMask = 1u << kNd;
        Node* nd = NewNode(nClass);
        nd->cls = AddClass(cc);
        return nd;
      }
      Node* nd = NewNode(nChar);
      nd->c = Fold(e);
      return nd;
    }
    Node* nd = NewNode(nChar);
    nd->c = Fold(c);
    return nd;
  }
  Node* ParseClass() {
    CharClass cc;
    if (More() && Peek() == '^') { cc.negate = true; ++pos_; }
    bool first = true;
    while (More() && (Peek() != ']' || first)) {
      first = false;
      char16_t c = pat_[pos_++];
      bool isLiteral = true;
      if (c == '\\') {
        char16_t e = pat_[pos_++];
        if (e == 'w') { cc.catMask |= WordMask(); isLiteral = false; }
        else if (e == 'p') { cc.catMask |= ParseCategoryName(); isLiteral = false; }
        else if (e == 'd') { cc.catMask |= 1u << kNd; isLiteral = false; }
        else c = e;
      }
      if (!isLiteral) continue;
      if (More() && Peek() == '-' && pos_ + 1 < pat_.size() && pat_[pos_ + 1] != ']') {
        ++pos_;  // '-'
        char16_t hi = pat_[pos_++];
        if (hi == '\\') hi = pat_[pos_++];
        cc.ranges.emplace_back(c, hi);
      } else {
        cc.ranges.emplace_back(c, c);
      }
    }
    ++pos_;  // ']'
    Node* nd = NewNode(nClass);
    nd->cls = AddClass(cc);
    return nd;
  }

  int Pc() const { return static_cast<int>(prog_.size()); }
  int EmitInst(Op op, int x = 0, int y = 0, char16_t c = 0) {
    Inst in;
    in.op = op; in.x = x; in.y = y; in.c = c;
    prog_.push_back(in);
    return Pc() - 1;
  }
  void Emit(Node* nd) {
    switch (nd->kind) {
      case nEmpty: break;
      case nChar: EmitInst(kChar, 0, 0, nd->c); break;
      case nClass: EmitInst(kClass, nd->cls); break;
      case nConcat: for (Node* k : nd->kids) Emit(k); break;
      case nAlt: {
        std::vector<int> jumps;
        for (size_t i = 0; i < nd->kids.size(); ++i) {
          if (i + 1 < nd->kids.size()) {
            int split = EmitInst(kSplit);
            prog_[split].x = Pc();
            Emit(nd->kids[i]);
            jumps.push_back(EmitInst(kJmp));
            prog_[split].y = Pc();
          } else {
            Emit(nd->kids[i]);
          }
        }
        for (int j : jumps) prog_[j].x = Pc();
        break;
      }
      case nRepeat: {
        Node* body = nd->kids[0];
        for (int i = 0; i < nd->min; ++i) Emit(body);
        if (nd->max < 0) {
          int loop = EmitInst(kSplit);
          prog_[loop].x = Pc();
          Emit(body);
          EmitInst(kJmp, loop);
          prog_[loop].y = Pc();
        } else {
          std::vector<int> splits;
          for (int i = nd->min; i < nd->max; ++i) {
            int s = EmitInst(kSplit);
            prog_[s].x = Pc();
            splits.push_back(s);
            Emit(body);
          }
          for (int s : splits) prog_[s].y = Pc();
        }
        break;
      }
    }
  }

  ustr pat_;
  bool ic_ = true;
  size_t pos_ = 0;
  std::vector<std::unique_ptr<Node>> nodes_;
  std::vector<CharClass> classes_;
  std::vector<Inst> prog_;
  std::vector<char16_t> firstChars_;
  std::vector<int> firstClasses_;
  bool canMatchEmpty_ = false;
};

// =====================================================================================
//  Irony object model (port of Irony 1.2.0, only what XLParser needs)
// =====================================================================================

enum TermFlags : uint32_t {
  IsOperator = 0x01,
  IsOpenBrace = 0x02,
  IsCloseBrace = 0x04,
  IsBrace = IsOpenBrace | IsCloseBrace,
  IsLiteral = 0x08,
  IsPunctuation = 0x20,
  IsReservedWord = 0x080,
  IsNonScanner = 0x01000,
  IsNonGrammar = 0x02000,
  IsTransient = 0x04000,
  IsNullable = 0x010000,
  IsKeyword = 0x040000,
  IsMultiline = 0x100000,
  IsList = 0x200000,
  IsListContainer = 0x400000,
  NoAstNode = 0x800000,
};

enum class Assoc { Left, Right, Neutral };
enum TokenCategory { kContent = 0, kOutline = 1, kComment = 2, kDirective = 3, kErrorCat = 4 };

const int kNoPrecedence = 0;
const int kPriorityLow = -1000;
const int kPriorityReservedWords = 900;

struct SourceLocation {
  int position = 0;
  int line = 0;
  int column = 0;
};

struct ParsingContext;
class SourceStream;
struct Token;
struct GrammarData;
struct ImpliedPrecedenceHint;
struct KeyTerm;
struct Production;
struct LRItem;
struct ParserState;
class Language;

struct BnfTerm {
  enum Kind { kNonTerminal, kTerminal, kHint };
  BnfTerm(Kind k, std::string n, std::string ea) : kind(k), name(std::move(n)), errorAlias(std::move(ea)) {}
  virtual ~BnfTerm() = default;
  bool IsSet(uint32_t f) const { return (flags & f) != 0; }
  void SetFlag(uint32_t f) { flags |= f; }

  Kind kind;
  std::string name;
  std::string errorAlias;
  uint32_t flags = 0;
  int precedence = kNoPrecedence;
  Assoc assoc = Assoc::Neutral;
  // Handlers of the BnfTerm.Shifting event (only ImpliedPrecedenceHint subscribes to it).
  std::vector<ImpliedPrecedenceHint*> shiftingHandlers;
};

using BnfTermList = std::vector<BnfTerm*>;

struct NonTerminal : BnfTerm {
  explicit NonTerminal(std::string n) : BnfTerm(kNonTerminal, std::move(n), std::string()) {}
  std::vector<BnfTermList> rule;  // BnfExpression.Data: alternatives of sequences
  std::vector<Production*> productions;
};

struct GrammarHint : BnfTerm {
  GrammarHint() : BnfTerm(kHint, "hint", "hint") {}
  virtual void Apply(Language& lang, LRItem* owner) { (void)lang; (void)owner; }
};

struct Terminal : BnfTerm {
  explicit Terminal(std::string n, int cat = kContent, uint32_t fl = 0) : BnfTerm(kTerminal, n, n) {
    category = cat;
    flags |= fl;
    if (category == kOutline) SetFlag(IsPunctuation);
    outputTerminal = this;
  }
  virtual void Init(GrammarData& gd) { (void)gd; }
  virtual std::vector<ustr> GetFirsts() { return {}; }
  virtual Token* TryMatch(ParsingContext& ctx, SourceStream& src) { (void)ctx; (void)src; return nullptr; }
  virtual void OnValidateToken(ParsingContext& ctx) { (void)ctx; }

  int category = kContent;
  int priority = 0;
  bool isKeyTerm = false;
  Terminal* outputTerminal;
};

struct Token {
  Terminal* terminal = nullptr;
  Terminal* keyTerm = nullptr;  // Token.KeyTerm (set when the token's terminal is a KeyTerm)
  SourceLocation location;
  ustr text;
  std::string errorMessage;  // Token.Value for scanner error tokens
  bool IsError() const { return terminal->category == kErrorCat; }
  int Length() const { return static_cast<int>(text.size()); }
};

struct ParseTreeNode {
  Token* token = nullptr;
  BnfTerm* term = nullptr;
  int precedence = kNoPrecedence;
  Assoc assoc = Assoc::Neutral;
  SourceLocation location;
  std::vector<ParseTreeNode*> children;
  bool isError = false;
  ParserState* state = nullptr;

  bool IsPunctuationOrEmptyTransient() const {
    if (term->IsSet(IsPunctuation)) return true;
    if (term->IsSet(IsTransient) && children.empty()) return true;
    return false;
  }
};

class SourceStream {
 public:
  SourceStream(const ustr& text, ParsingContext* ctx) : text_(text), len_(static_cast<int>(text.size())), ctx_(ctx) {}

  const ustr& Text() const { return text_; }
  int Position() const { return location.position; }
  void SetPosition(int v) {
    if (location.position != v) SetNewPosition(v);
  }
  char16_t PreviewChar() const { return previewPosition >= len_ ? char16_t(0) : text_[previewPosition]; }
  char16_t NextPreviewChar() const { return previewPosition + 1 >= len_ ? char16_t(0) : text_[previewPosition + 1]; }
  bool EOF_() const { return previewPosition >= len_; }

  // string.Compare(text, PreviewPosition, symbol, 0, symbol.Length, InvariantCultureIgnoreCase) == 0
  bool MatchSymbol(const ustr& symbol) const {
    if (previewPosition + static_cast<int>(symbol.size()) > len_) return false;
    for (size_t i = 0; i < symbol.size(); ++i)
      if (ToLowerCulture(text_[previewPosition + i]) != ToLowerCulture(symbol[i])) return false;
    return true;
  }

  Token* CreateToken(Terminal* terminal);
  Token* CreateToken(Terminal* terminal, const std::string& message);

  ustr GetPreviewText() const {
    int until = previewPosition;
    if (until > len_) until = len_;
    int p = location.position;
    return text_.substr(p, until - p);
  }

  SourceLocation location;
  int previewPosition = 0;

 private:
  void SetNewPosition(int newPosition) {
    int p = location.position;
    int col = location.column;
    int line = location.line;
    const int tabWidth = 8;
    while (p < newPosition) {
      if (p >= len_) break;
      char16_t curr = text_[p];
      switch (curr) {
        case '\n': line++; col = 0; break;
        case '\r':
          if (p == len_ - 1 || text_[p + 1] != '\n') { line++; col = 0; }
          break;
        case '\t': col = (col / tabWidth + 1) * tabWidth; break;
        default: col++; break;
      }
      p++;
    }
    location.position = p;
    location.line = line;
    location.column = col;
  }

  const ustr& text_;
  int len_;
  ParsingContext* ctx_;
};

// ---------------------------------- Parser data -----------------------------------

struct LR0Item {
  int id = 0;
  Production* production = nullptr;
  int position = 0;
  BnfTerm* current = nullptr;
  bool tailIsNullable = false;
  std::vector<GrammarHint*> hints;

  LR0Item* ShiftedItem() const;
  bool IsInitial() const { return position == 0; }
  bool IsFinal() const;
};

struct Production {
  NonTerminal* lvalue = nullptr;
  BnfTermList rvalues;
  std::vector<LR0Item*> lr0Items;
  uint32_t flags = 0;
};

LR0Item* LR0Item::ShiftedItem() const {
  if (position >= static_cast<int>(production->lr0Items.size()) - 1) return nullptr;
  return production->lr0Items[position + 1];
}
bool LR0Item::IsFinal() const { return position == static_cast<int>(production->rvalues.size()); }

struct Transition;
struct ParserAction;

struct LRItem {
  ParserState* state = nullptr;
  LR0Item* core = nullptr;
  LRItem* shiftedItem = nullptr;
  Transition* transition = nullptr;
  NetSet<Transition*> lookbacks;
  NetSet<Terminal*> lookaheads;
};

struct ParserStateData;

struct ParserState {
  std::string name;
  std::unordered_map<BnfTerm*, ParserAction*> actions;
  ParserAction* defaultAction = nullptr;
  NetSet<Terminal*> expectedTerminals;
  std::vector<std::string> reportedExpectedSet;  // precomputed ReportedExpectedSet
  ParserStateData* builderData = nullptr;
  int customFlags = 0;

  ParserAction* FindAction(BnfTerm* t) const {
    auto it = actions.find(t);
    return it == actions.end() ? nullptr : it->second;
  }
};

const int kImpliedPrecedenceCustomFlag = 0x01000000;

struct ParserStateData {
  ParserStateData(Language& lang, ParserState* st, const NetSet<LR0Item*>& kernelCores);

  ParserState* state;
  NetSet<LRItem*> allItems, shiftItems, reduceItems, initialItems;
  NetSet<BnfTerm*> shiftTerms;
  NetSet<Terminal*> shiftTerminals;
  NetSet<Terminal*> conflicts;
  bool isInadequate = false;
  NetSet<LR0Item*> allCores;
  std::unordered_map<NonTerminal*, Transition*> transitions;
  std::unique_ptr<NetSet<ParserState*>> readStateSet;

  void AddItem(LR0Item* core);
  NetSet<ParserState*>& ReadStateSet();
  ParserState* GetNextState(BnfTerm* shiftTerm) {
    for (LRItem* it : shiftItems.ToList())
      if (it->core->current == shiftTerm) return it->shiftedItem->state;
    return nullptr;
  }

 private:
  Language& lang_;
};

struct Transition {
  Transition(ParserState* from, NonTerminal* over);
  ParserState* fromState;
  ParserState* toState;
  NonTerminal* overNonTerminal;
  NetSet<LRItem*> items;
  NetSet<Transition*> includes;
  NetSet<Transition*> includedBy;

  void Include(Transition* other) {
    if (other == this) return;
    if (!IncludeTransition(other)) return;
    for (Transition* child : other->includes.ToList()) IncludeTransition(child);
  }
  bool IncludeTransition(Transition* other) {
    if (!includes.Add(other)) return false;
    other->includedBy.Add(this);
    for (Transition* incBy : includedBy.ToList()) incBy->IncludeTransition(other);
    return true;
  }
};

// ------------------------------------ Actions -------------------------------------

struct ParserAction {
  virtual ~ParserAction() = default;
  virtual void Execute(ParsingContext& ctx) = 0;
};

// =====================================================================================
//  Language (GrammarData + ScannerData + ParserData)
// =====================================================================================

struct GrammarData {
  NonTerminal* augmentedRoot = nullptr;
  NetSet<BnfTerm*> allTerms;
  NetSet<Terminal*> terminals;
  NetSet<NonTerminal*> nonTerminals;
  NetSet<Terminal*> noPrefixTerminals;
};

class Language {
 public:
  Language();

  // ----- grammar -----
  template <class T, class... A>
  T* Make(A&&... args) {
    T* p = new T(std::forward<A>(args)...);
    owned_.emplace_back(p);
    return p;
  }
  struct KeyTermEntry {
    ustr text;
    KeyTerm* term;
  };
  KeyTerm* ToTerm(const ustr& text, const std::string& name);

  NonTerminal* root = nullptr;
  Terminal* Empty = nullptr;
  Terminal* Eof = nullptr;
  Terminal* SyntaxError = nullptr;
  std::vector<KeyTermEntry> keyTerms;

  GrammarData grammarData;

  // ----- scanner data -----
  std::unordered_map<char16_t, std::vector<Terminal*>> terminalsLookup;
  std::vector<Terminal*> noPrefixTerminalsSorted;
  std::vector<Terminal*> allTerminalsList;  // GrammarData.Terminals enumeration order

  // ----- parser data -----
  std::vector<ParserState*> states;
  ParserState* initialState = nullptr;
  ParserAction* errorAction = nullptr;

  template <class T, class... A>
  T* MakeObj(A&&... args) {
    T* p = new T(std::forward<A>(args)...);
    objects_.emplace_back(std::unique_ptr<void, void (*)(void*)>(p, [](void* q) { delete static_cast<T*>(q); }));
    return p;
  }

 private:
  void BuildGrammar();
  void BuildGrammarData();
  void BuildScannerData();
  void BuildParserData();

  std::vector<std::unique_ptr<BnfTerm>> owned_;
  std::vector<std::unique_ptr<void, void (*)(void*)>> objects_;
};

ParserStateData::ParserStateData(Language& lang, ParserState* st, const NetSet<LR0Item*>& kernelCores)
    : state(st), lang_(lang) {
  for (LR0Item* core : kernelCores.ToList()) AddItem(core);
  isInadequate = reduceItems.Count() > 1 || (reduceItems.Count() == 1 && shiftItems.Count() > 0);
}

void ParserStateData::AddItem(LR0Item* core) {
  if (!allCores.Add(core)) return;
  LRItem* item = lang_.MakeObj<LRItem>();
  item->state = state;
  item->core = core;
  allItems.Add(item);
  if (core->IsFinal()) reduceItems.Add(item); else shiftItems.Add(item);
  if (core->IsInitial()) initialItems.Add(item);
  if (core->IsFinal()) return;
  if (!shiftTerms.Add(core->current)) return;
  if (core->current->kind == BnfTerm::kTerminal) shiftTerminals.Add(static_cast<Terminal*>(core->current));
  if (core->current->kind != BnfTerm::kNonTerminal) return;
  NonTerminal* nt = static_cast<NonTerminal*>(core->current);
  for (Production* p : nt->productions) AddItem(p->lr0Items[0]);
}

Transition::Transition(ParserState* from, NonTerminal* over) : fromState(from), overNonTerminal(over) {
  toState = fromState->builderData->GetNextState(over);
  fromState->builderData->transitions[over] = this;
  for (LRItem* it : fromState->builderData->shiftItems.ToList())
    if (it->core->current == over) items.Add(it);
  for (LRItem* it : items.ToList()) it->transition = this;
}

// =====================================================================================
//  Parsing context
// =====================================================================================

enum class ParserStatus { Init, Parsing, Previewing, Recovering, Accepted, AcceptedPartial, Error };

struct LogMessage {
  SourceLocation location;
  std::string message;
};

struct ParsingContext {
  explicit ParsingContext(Language& l) : lang(l) {}
  Language& lang;
  std::unique_ptr<SourceStream> source;
  ParserState* currentParserState = nullptr;
  ParseTreeNode* currentParserInput = nullptr;
  Token* currentToken = nullptr;
  std::vector<Terminal*> currentTerminals;
  ParserStatus status = ParserStatus::Init;
  std::vector<ParseTreeNode*> stack;
  std::vector<LogMessage> messages;
  ParseTreeNode* root = nullptr;

  std::vector<std::unique_ptr<Token>> tokenArena;
  std::vector<std::unique_ptr<ParseTreeNode>> nodeArena;

  Token* NewToken(Terminal* t, SourceLocation loc, ustr text) {
    tokenArena.emplace_back(new Token());
    Token* tk = tokenArena.back().get();
    tk->terminal = t;
    tk->keyTerm = nullptr;
    tk->location = loc;
    tk->text = std::move(text);
    return tk;
  }
  ParseTreeNode* NewNode() {
    nodeArena.emplace_back(new ParseTreeNode());
    return nodeArena.back().get();
  }
  ParseTreeNode* NewTokenNode(Token* token) {
    ParseTreeNode* n = NewNode();
    n->token = token;
    n->term = token->terminal;
    n->precedence = n->term->precedence;
    n->assoc = token->terminal->assoc;
    n->location = token->location;
    n->isError = token->IsError();
    return n;
  }
  ParseTreeNode* NewNtNode(NonTerminal* nt) {
    ParseTreeNode* n = NewNode();
    n->term = nt;
    return n;
  }

  Token* CreateErrorToken(const std::string& message) { return source->CreateToken(lang.SyntaxError, message); }

  void AddParserError(const std::string& message) {
    SourceLocation loc = currentParserInput == nullptr ? source->location : currentParserInput->location;
    if (messages.size() >= 20) return;
    messages.push_back(LogMessage{loc, message});
  }

  // Shift-time event (BnfTerm.Shifting): only ImpliedPrecedenceHint handlers exist.
  void FireShifting(BnfTerm* term);
};

Token* SourceStream::CreateToken(Terminal* terminal) {
  Token* t = ctx_->NewToken(terminal, location, GetPreviewText());
  if (terminal->isKeyTerm) t->keyTerm = terminal;
  return t;
}
Token* SourceStream::CreateToken(Terminal* terminal, const std::string& message) {
  Token* t = CreateToken(terminal);
  t->errorMessage = message;
  return t;
}

// =====================================================================================
//  Terminals
// =====================================================================================

bool IsWhitespaceOrDelimiter(char16_t ch) {
  switch (ch) {
    case ' ': case '\t': case '\r': case '\n': case '\v':
    case '(': case ')': case ',': case ';': case '[': case ']': case '{': case '}':
    case 0:
      return true;
    default:
      return false;
  }
}

struct KeyTerm : Terminal {
  KeyTerm(const ustr& t, const std::string& n) : Terminal(n), text(t) {
    isKeyTerm = true;
    errorAlias = n;
    SetFlag(NoAstNode);
  }
  void Init(GrammarData& gd) override {
    (void)gd;
    if (IsSet(IsReservedWord)) priority = kPriorityReservedWords + static_cast<int>(text.size());
    else priority = kPriorityLow + static_cast<int>(text.size());
  }
  std::vector<ustr> GetFirsts() override { return {text}; }
  Token* TryMatch(ParsingContext& ctx, SourceStream& src) override {
    (void)ctx;
    if (!src.MatchSymbol(text)) return nullptr;
    src.previewPosition += static_cast<int>(text.size());
    if (IsSet(IsKeyword)) {
      char16_t pc = src.PreviewChar();
      if (NetIsLetterOrDigit(pc) || pc == '_') return nullptr;
    }
    return src.CreateToken(outputTerminal);
  }
  ustr text;
};

struct ImpliedSymbolTerminal : Terminal {
  explicit ImpliedSymbolTerminal(const std::string& n) : Terminal(n) { priority = kPriorityLow; }
  Token* TryMatch(ParsingContext& ctx, SourceStream& src) override {
    (void)ctx;
    return src.CreateToken(this);
  }
};

struct RegexBasedTerminal : Terminal {
  RegexBasedTerminal(const std::string& n, const ustr& pat) : Terminal(n), pattern(pat) {}
  void Init(GrammarData& gd) override {
    (void)gd;
    regex.reset(new Regex(U("\\G(") + pattern + U(")"), /*ignoreCase*/ true));
  }
  Token* TryMatch(ParsingContext& ctx, SourceStream& src) override {
    (void)ctx;
    int len = regex->MatchAt(src.Text(), src.previewPosition);
    if (len < 0) return nullptr;
    src.previewPosition += len;
    return src.CreateToken(outputTerminal);
  }
  ustr pattern;
  std::unique_ptr<Regex> regex;
};

// The ExcelFunction terminal is a RegexBasedTerminal with the pattern "(NAME1|NAME2|...)\\(" built from the
// built-in function list (names are not escaped, so '.' inside a name matches any char except '\n').
// .NET's backtracking semantics for that pattern are: the first name in list order that matches at the
// position and is followed by '('. This matcher implements exactly that, without the generic engine.
struct FunctionListTerminal : Terminal {
  FunctionListTerminal(const std::string& n, std::vector<ustr> names) : Terminal(n), names_(std::move(names)) {
    for (ustr& nm : names_)
      for (char16_t& c : nm) c = ToLowerCulture(c);
    for (size_t i = 0; i < names_.size(); ++i) {
      if (names_[i].empty()) continue;
      if (names_[i][0] == '.') wildcardFirst_.push_back(static_cast<int>(i));
      else byFirst_[names_[i][0]].push_back(static_cast<int>(i));
    }
  }
  Token* TryMatch(ParsingContext& ctx, SourceStream& src) override {
    (void)ctx;
    const ustr& text = src.Text();
    const int n = static_cast<int>(text.size());
    const int start = src.previewPosition;
    if (start >= n) return nullptr;
    char16_t first = ToLowerCulture(text[start]);
    const std::vector<int>* cands = nullptr;
    auto it = byFirst_.find(first);
    if (it != byFirst_.end()) cands = &it->second;
    // merge both candidate lists in original list order
    size_t a = 0, b = 0;
    const size_t na = cands ? cands->size() : 0, nb = (first != '\n') ? wildcardFirst_.size() : 0;
    while (a < na || b < nb) {
      int idx;
      if (b >= nb || (a < na && (*cands)[a] < wildcardFirst_[b])) idx = (*cands)[a++];
      else idx = wildcardFirst_[b++];
      const ustr& nm = names_[idx];
      int len = static_cast<int>(nm.size());
      if (start + len >= n) continue;  // need room for '('
      bool ok = true;
      for (int k = 0; k < len && ok; ++k) {
        char16_t c = ToLowerCulture(text[start + k]);
        ok = (nm[k] == '.') ? (c != '\n') : (c == nm[k]);
      }
      if (!ok || text[start + len] != '(') continue;
      src.previewPosition += len + 1;
      return src.CreateToken(outputTerminal);
    }
    return nullptr;
  }
  std::vector<ustr> names_;
  std::unordered_map<char16_t, std::vector<int>> byFirst_;
  std::vector<int> wildcardFirst_;
};

// Irony NumberLiteral with NumberOptions.None, DefaultIntTypes {Int32, Int64, BigInt}, float type Double.
struct NumberLiteral : Terminal {
  explicit NumberLiteral(const std::string& n) : Terminal(n) { SetFlag(IsLiteral); }
  std::vector<ustr> GetFirsts() override {
    std::vector<ustr> r;
    for (char16_t c = '0'; c <= '9'; ++c) r.push_back(ustr(1, c));
    return r;
  }
  static bool IsAsciiDigit(char16_t c) { return c >= '0' && c <= '9'; }

  // .NET Framework double.TryParse(NumberStyles.Float, InvariantCulture) for the bodies produced by ReadBody:
  // digits with an optional '.', optional exponent. Fails on malformed exponent and on overflow.
  static bool TryParseDouble(const ustr& body) {
    size_t i = 0, n = body.size();
    std::string mant;
    long dotPos = -1;
    while (i < n && (IsAsciiDigit(body[i]) || body[i] == '.')) {
      if (body[i] == '.') dotPos = static_cast<long>(mant.size());
      else mant.push_back(static_cast<char>(body[i]));
      ++i;
    }
    long long exp = 0;
    bool expNeg = false;
    if (i < n && (body[i] == 'e' || body[i] == 'E')) {
      ++i;
      if (i < n && (body[i] == '+' || body[i] == '-')) { expNeg = body[i] == '-'; ++i; }
      if (i >= n || !IsAsciiDigit(body[i])) return false;
      while (i < n && IsAsciiDigit(body[i])) {
        exp = exp * 10 + (body[i] - '0');
        if (exp > 1000000000LL) exp = 1000000000LL;
        ++i;
      }
    }
    if (i != n || mant.empty()) return false;
    size_t firstNZ = mant.find_first_not_of('0');
    if (firstNZ == std::string::npos) return true;  // zero
    long long intDigits = dotPos < 0 ? static_cast<long long>(mant.size()) : dotPos;
    long long sigExp = (intDigits - 1 - static_cast<long long>(firstNZ)) + (expNeg ? -exp : exp);
    if (sigExp > 308) return false;
    if (sigExp < 308) return true;
    std::string sig = mant.substr(firstNZ);
    sig.resize(20, '0');
    return sig < std::string("17976931348623158079");
  }

  Token* TryMatch(ParsingContext& ctx, SourceStream& src) override {
    // QuickParse
    char16_t current = src.PreviewChar();
    if (NetIsDigit(current)) {
      char16_t next = src.NextPreviewChar();
      if (IsWhitespaceOrDelimiter(next) && next != '.') {
        src.previewPosition++;
        return src.CreateToken(outputTerminal);
      }
    }
    src.previewPosition = src.Position();
    // ReadPrefix: no prefixes. ReadBody:
    int start = src.previewPosition;
    bool foundDigits = false, hasDot = false, hasExp = false, expSet = false;
    while (!src.EOF_()) {
      current = src.PreviewChar();
      if (IsAsciiDigit(current)) {
        src.previewPosition++;
        foundDigits = true;
        continue;
      }
      bool isDot = current == '.';
      if (isDot) {
        if (hasDot || hasExp) break;
        if (!IsAsciiDigit(src.NextPreviewChar())) break;
        hasDot = true;
        src.previewPosition++;
        continue;
      }
      bool isExpSymbol = !expSet && (current == 'e' || current == 'E');
      if (isExpSymbol) {
        char16_t next = src.NextPreviewChar();
        bool nextIsSign = next == '-' || next == '+';
        bool nextIsDigit = IsAsciiDigit(next);
        if (!nextIsSign && !nextIsDigit) break;
        expSet = true;
        hasExp = true;
        src.previewPosition++;
        if (nextIsSign) src.previewPosition++;
        continue;
      }
      break;
    }
    int end = src.previewPosition;
    if (!foundDigits) return nullptr;
    ustr body = src.Text().substr(start, end - start);
    // ConvertValue
    bool ok = true;
    if (hasDot || hasExp) ok = TryParseDouble(body);  // integers always convert (Int32/Int64/BigInteger)
    if (!ok) return ctx.CreateErrorToken("Invalid number.");
    return src.CreateToken(outputTerminal);
  }
  void OnValidateToken(ParsingContext& ctx) override;
};

// Irony StringLiteral with AllowsDoubledQuote | AllowsLineBreak | NoEscapes and a single start/end symbol.
struct StringLiteral : Terminal {
  StringLiteral(const std::string& n, char16_t q) : Terminal(n), quote(q) { SetFlag(IsLiteral); }
  std::vector<ustr> GetFirsts() override { return {ustr(1, quote), ustr(1, quote)}; }
  Token* TryMatch(ParsingContext& ctx, SourceStream& src) override {
    src.previewPosition = src.Position();
    // ReadStartSymbol
    if (src.PreviewChar() != quote) return nullptr;
    if (!src.MatchSymbol(ustr(1, quote))) return nullptr;
    src.previewPosition += 1;
    // CompleteReadBody
    const ustr& text = src.Text();
    for (;;) {
      size_t endPosRaw = text.find(quote, static_cast<size_t>(src.previewPosition));
      int endPos = endPosRaw == ustr::npos ? -1 : static_cast<int>(endPosRaw);
      if (endPos < 0) {
        return ctx.CreateErrorToken("Mal-formed  string literal - cannot find termination symbol.");
      }
      if (src.EOF_()) break;
      src.previewPosition = endPos;
      if (src.MatchSymbol(ustr(2, quote))) {
        src.previewPosition = endPos + 2;
        continue;
      }
      src.previewPosition = endPos + 1;
      break;
    }
    return src.CreateToken(outputTerminal);
  }
  char16_t quote;
};

// =====================================================================================
//  Hints
// =====================================================================================

struct ImpliedPrecedenceHint : GrammarHint {
  ImpliedPrecedenceHint(int p, Assoc a) { precedence = p; assoc = a; }
  void Apply(Language& lang, LRItem* owner) override;
};

struct PreferredActionHint : GrammarHint {
  explicit PreferredActionHint(bool shift) : preferShift(shift) {}
  void Apply(Language& lang, LRItem* owner) override;
  bool preferShift;
};

struct PrecedenceHint : GrammarHint {
  void Apply(Language& lang, LRItem* owner) override;
};

// =====================================================================================
//  Parser actions
// =====================================================================================

struct ShiftParserAction : ParserAction {
  ShiftParserAction(BnfTerm* t, ParserState* ns) : term(t), newState(ns) {}
  void Execute(ParsingContext& ctx) override {
    ParseTreeNode* currInput = ctx.currentParserInput;
    ctx.FireShifting(currInput->term);
    currInput->state = newState;
    ctx.stack.push_back(currInput);
    ctx.currentParserState = newState;
    ctx.currentParserInput = nullptr;
  }
  BnfTerm* term;
  ParserState* newState;
};

struct ReduceParserAction : ParserAction {
  explicit ReduceParserAction(Production* p) : production(p) {}
  static ReduceParserAction* Create(Language& lang, Production* production);

  void Execute(ParsingContext& ctx) override {
    ParseTreeNode* saved = ctx.currentParserInput;
    ctx.currentParserInput = GetResultNode(ctx);
    CompleteReduce(ctx);
    ctx.currentParserInput = saved;
  }
  virtual ParseTreeNode* GetResultNode(ParsingContext& ctx) {
    int childCount = static_cast<int>(production->rvalues.size());
    int firstChildIndex = static_cast<int>(ctx.stack.size()) - childCount;
    ParseTreeNode* newNode = ctx.NewNtNode(production->lvalue);
    for (int i = 0; i < childCount; ++i) {
      ParseTreeNode* child = ctx.stack[firstChildIndex + i];
      if (child->IsPunctuationOrEmptyTransient()) continue;
      newNode->children.push_back(child);
    }
    return newNode;
  }
  void CompleteReduce(ParsingContext& ctx) {
    ParseTreeNode* resultNode = ctx.currentParserInput;
    int childCount = static_cast<int>(production->rvalues.size());
    ctx.stack.resize(ctx.stack.size() - childCount);
    ctx.currentParserState = ctx.stack.back()->state;
    if (resultNode->term != production->lvalue) ctx.FireShifting(production->lvalue);
    ParserAction* shift = ctx.currentParserState->FindAction(production->lvalue);
    shift->Execute(ctx);
  }
  Production* production;
};

struct ReduceTransientParserAction : ReduceParserAction {
  explicit ReduceTransientParserAction(Production* p) : ReduceParserAction(p) {}
  ParseTreeNode* GetResultNode(ParsingContext& ctx) override {
    int topIndex = static_cast<int>(ctx.stack.size()) - 1;
    int childCount = static_cast<int>(production->rvalues.size());
    for (int i = 0; i < childCount; ++i) {
      ParseTreeNode* child = ctx.stack[topIndex - i];
      if (child->IsPunctuationOrEmptyTransient()) continue;
      return child;
    }
    return ctx.NewNtNode(production->lvalue);
  }
};

struct ReduceListBuilderParserAction : ReduceParserAction {
  explicit ReduceListBuilderParserAction(Production* p) : ReduceParserAction(p) {}
  ParseTreeNode* GetResultNode(ParsingContext& ctx) override {
    int childCount = static_cast<int>(production->rvalues.size());
    int firstChildIndex = static_cast<int>(ctx.stack.size()) - childCount;
    ParseTreeNode* listNode = ctx.stack[firstChildIndex];
    ParseTreeNode* listMember = ctx.stack.back();
    if (listMember->IsPunctuationOrEmptyTransient()) return listNode;
    listNode->children.push_back(listMember);
    return listNode;
  }
};

struct ReduceListContainerParserAction : ReduceParserAction {
  explicit ReduceListContainerParserAction(Production* p) : ReduceParserAction(p) {}
  ParseTreeNode* GetResultNode(ParsingContext& ctx) override {
    int childCount = static_cast<int>(production->rvalues.size());
    int firstChildIndex = static_cast<int>(ctx.stack.size()) - childCount;
    ParseTreeNode* newNode = ctx.NewNtNode(production->lvalue);
    if (childCount > 0) {
      ParseTreeNode* listNode = ctx.stack[firstChildIndex];
      for (ParseTreeNode* c : listNode->children) newNode->children.push_back(c);
    }
    return newNode;
  }
};

ReduceParserAction* ReduceParserAction::Create(Language& lang, Production* production) {
  NonTerminal* nonTerm = production->lvalue;
  bool isList = nonTerm->IsSet(IsList);
  bool isListBuilderProduction = isList && !production->rvalues.empty() && production->rvalues[0] == production->lvalue;
  if (isListBuilderProduction) return lang.MakeObj<ReduceListBuilderParserAction>(production);
  if (nonTerm->IsSet(IsListContainer)) return lang.MakeObj<ReduceListContainerParserAction>(production);
  if (nonTerm->IsSet(IsTransient)) return lang.MakeObj<ReduceTransientParserAction>(production);
  return lang.MakeObj<ReduceParserAction>(production);
}

struct AcceptParserAction : ParserAction {
  void Execute(ParsingContext& ctx) override {
    ctx.root = ctx.stack.back();
    ctx.stack.pop_back();
    ctx.status = ParserStatus::Accepted;
  }
};

struct PrecedenceBasedParserAction : ParserAction {
  PrecedenceBasedParserAction(Language& lang, BnfTerm* shiftTerm, ParserState* newShiftState, Production* reduceProduction) {
    reduceAction = lang.MakeObj<ReduceParserAction>(reduceProduction);
    shiftAction = lang.MakeObj<ShiftParserAction>(shiftTerm, newShiftState);
  }
  bool CheckMustReduce(ParsingContext& ctx) {
    ParseTreeNode* input = ctx.currentParserInput;
    int stackCount = static_cast<int>(ctx.stack.size());
    int prodLength = static_cast<int>(reduceAction->production->rvalues.size());
    for (int i = 1; i <= prodLength; ++i) {
      ParseTreeNode* prevNode = ctx.stack[stackCount - i];
      if (prevNode == nullptr) continue;
      if (prevNode->precedence == kNoPrecedence) continue;
      if (prevNode->precedence == input->precedence) return input->assoc == Assoc::Left;
      return prevNode->precedence > input->precedence;
    }
    return false;
  }
  void Execute(ParsingContext& ctx) override {
    if (CheckMustReduce(ctx)) reduceAction->Execute(ctx);
    else shiftAction->Execute(ctx);
  }
  ReduceParserAction* reduceAction;
  ShiftParserAction* shiftAction;
};

struct ErrorRecoveryParserAction : ParserAction {
  void Execute(ParsingContext& ctx) override;
};

void ParsingContext::FireShifting(BnfTerm* term) {
  for (ImpliedPrecedenceHint* h : term->shiftingHandlers) {
    if ((currentParserState->customFlags & kImpliedPrecedenceCustomFlag) == 0) continue;
    currentParserInput->assoc = h->assoc;
    currentParserInput->precedence = h->precedence;
  }
}

void NumberLiteral::OnValidateToken(ParsingContext& ctx) {
  char16_t current = ctx.source->PreviewChar();
  if (NetIsLetter(current) || current == '_') ctx.currentToken = ctx.CreateErrorToken("Number cannot be followed by a letter.");
}

void ImpliedPrecedenceHint::Apply(Language& lang, LRItem* owner) {
  (void)lang;
  BnfTerm* curr = owner->core->current;
  if (curr == nullptr) return;
  owner->state->customFlags |= kImpliedPrecedenceCustomFlag;
  curr->shiftingHandlers.push_back(this);
}

void PreferredActionHint::Apply(Language& lang, LRItem* owner) {
  ParserState* state = owner->state;
  NetSet<Terminal*>& conflicts = state->builderData->conflicts;
  if (conflicts.Count() == 0) return;
  if (preferShift) {
    BnfTerm* cur = owner->core->current;
    if (cur == nullptr || cur->kind != BnfTerm::kTerminal) return;
    Terminal* currTerm = static_cast<Terminal*>(cur);
    if (!conflicts.Contains(currTerm)) return;
    state->actions[currTerm] = lang.MakeObj<ShiftParserAction>(owner->core->current, owner->shiftedItem->state);
    conflicts.Remove(currTerm);
  } else {
    if (!owner->core->IsFinal()) return;
    ReduceParserAction* reduceAction = nullptr;
    for (Terminal* lkhead : owner->lookaheads.ToList()) {
      if (conflicts.Contains(lkhead)) {
        if (reduceAction == nullptr) reduceAction = lang.MakeObj<ReduceParserAction>(owner->core->production);
        state->actions[lkhead] = reduceAction;
        conflicts.Remove(lkhead);
      }
    }
  }
}

void PrecedenceHint::Apply(Language& lang, LRItem* owner) {
  ParserState* state = owner->state;
  NetSet<Terminal*>& allConflicts = state->builderData->conflicts;
  if (allConflicts.Count() == 0) return;
  std::vector<Terminal*> operConflicts;
  for (Terminal* c : allConflicts.ToList())
    if (c->IsSet(IsOperator)) operConflicts.push_back(c);
  for (Terminal* conflict : operConflicts) {
    ParserState* newState = state->builderData->GetNextState(conflict);
    std::vector<LRItem*> reduceItems;
    for (LRItem* it : state->builderData->reduceItems.ToList())
      if (it->lookaheads.Contains(conflict)) reduceItems.push_back(it);
    if (newState == nullptr || reduceItems.size() != 1) continue;
    state->actions[conflict] = lang.MakeObj<PrecedenceBasedParserAction>(lang, conflict, newState, reduceItems[0]->core->production);
    allConflicts.Remove(conflict);
  }
}

NetSet<ParserState*>& ParserStateData::ReadStateSet() {
  if (!readStateSet) {
    readStateSet.reset(new NetSet<ParserState*>());
    for (BnfTerm* shiftTerm : state->builderData->shiftTerms.ToList()) {
      if (shiftTerm->IsSet(IsNullable)) {
        ShiftParserAction* shift = dynamic_cast<ShiftParserAction*>(state->FindAction(shiftTerm));
        ParserState* targetState = shift->newState;
        readStateSet->Add(targetState);
        readStateSet->UnionWith(targetState->builderData->ReadStateSet());
      }
    }
  }
  return *readStateSet;
}

// =====================================================================================
//  .NET Framework 4.x List<T>.Sort(Comparison<T>) == ArraySortHelper<T>.IntrospectiveSort
//  (unstable; the exact algorithm matters for terminals of equal priority)
// =====================================================================================

int CompareByPriorityReverse(Terminal* x, Terminal* y) {
  if (x->priority > y->priority) return -1;
  if (x->priority == y->priority) return 0;
  return 1;
}

struct NetIntroSort {
  using Cmp = int (*)(Terminal*, Terminal*);
  std::vector<Terminal*>& keys;
  Cmp cmp;
  void SwapIfGreater(int a, int b) {
    if (a != b && cmp(keys[a], keys[b]) > 0) std::swap(keys[a], keys[b]);
  }
  void Swap(int i, int j) {
    if (i != j) std::swap(keys[i], keys[j]);
  }
  static int FloorLog2(int n) {
    int result = 0;
    while (n >= 1) { result++; n = n / 2; }
    return result;
  }
  void InsertionSort(int lo, int hi) {
    for (int i = lo; i < hi; i++) {
      int j = i;
      Terminal* t = keys[i + 1];
      while (j >= lo && cmp(t, keys[j]) < 0) { keys[j + 1] = keys[j]; j--; }
      keys[j + 1] = t;
    }
  }
  void DownHeap(int i, int n, int lo) {
    Terminal* d = keys[lo + i - 1];
    while (i <= n / 2) {
      int child = 2 * i;
      if (child < n && cmp(keys[lo + child - 1], keys[lo + child]) < 0) child++;
      if (!(cmp(d, keys[lo + child - 1]) < 0)) break;
      keys[lo + i - 1] = keys[lo + child - 1];
      i = child;
    }
    keys[lo + i - 1] = d;
  }
  void Heapsort(int lo, int hi) {
    int n = hi - lo + 1;
    for (int i = n / 2; i >= 1; i = i - 1) DownHeap(i, n, lo);
    for (int i = n; i > 1; i = i - 1) { Swap(lo, lo + i - 1); DownHeap(1, i - 1, lo); }
  }
  int PickPivotAndPartition(int lo, int hi) {
    int middle = lo + ((hi - lo) >> 1);
    SwapIfGreater(lo, middle);
    SwapIfGreater(lo, hi);
    SwapIfGreater(middle, hi);
    Terminal* pivot = keys[middle];
    Swap(middle, hi - 1);
    int left = lo, right = hi - 1;
    while (left < right) {
      while (cmp(keys[++left], pivot) < 0) {}
      while (cmp(pivot, keys[--right]) < 0) {}
      if (left >= right) break;
      Swap(left, right);
    }
    Swap(left, hi - 1);
    return left;
  }
  void IntroSort(int lo, int hi, int depthLimit) {
    while (hi > lo) {
      int partitionSize = hi - lo + 1;
      if (partitionSize <= 16) {
        if (partitionSize == 1) return;
        if (partitionSize == 2) { SwapIfGreater(lo, hi); return; }
        if (partitionSize == 3) { SwapIfGreater(lo, hi - 1); SwapIfGreater(lo, hi); SwapIfGreater(hi - 1, hi); return; }
        InsertionSort(lo, hi);
        return;
      }
      if (depthLimit == 0) { Heapsort(lo, hi); return; }
      depthLimit--;
      int p = PickPivotAndPartition(lo, hi);
      IntroSort(p + 1, hi, depthLimit);
      hi = p - 1;
    }
  }
  void Sort(int capacity) {
    int length = static_cast<int>(keys.size());
    if (length < 2) return;
    IntroSort(0, length - 1, 2 * FloorLog2(capacity));
  }
};

// Capacity of a .NET List<T> after n sequential Add() calls (0 -> 4 -> 8 -> 16 ...).
int ListCapacityAfterAdds(int n) {
  if (n == 0) return 0;
  int cap = 4;
  while (cap < n) cap *= 2;
  return cap;
}

// =====================================================================================
//  Language construction
// =====================================================================================

KeyTerm* Language::ToTerm(const ustr& text, const std::string& name) {
  for (auto& e : keyTerms) {
    if (e.text.size() != text.size()) continue;
    bool eq = true;
    for (size_t i = 0; i < text.size(); ++i)
      if (ToLowerCulture(e.text[i]) != ToLowerCulture(text[i])) { eq = false; break; }
    if (eq) {
      if (e.term->name.empty() && !name.empty()) e.term->name = name;
      return e.term;
    }
  }
  ustr lower;
  for (char16_t c : text) lower.push_back(ToLowerCulture(c));
  KeyTerm* term = Make<KeyTerm>(lower, name);
  keyTerms.push_back(KeyTermEntry{lower, term});
  return term;
}

Language::Language() {
  BuildGrammar();
  BuildGrammarData();
  BuildScannerData();
  BuildParserData();
}

// ---------------------------- The XLParser v1.3.9 grammar ----------------------------

void Language::BuildGrammar() {
  // Standard Irony terminals
  Empty = Make<Terminal>("EMPTY");
  Eof = Make<Terminal>("EOF", kOutline);
  SyntaxError = Make<Terminal>("SYNTAX_ERROR", kErrorCat, IsNonScanner);

  auto T = [&](const char* s) { return ToTerm(U(s), s); };

  // ---- Symbols and operators
  KeyTerm* at = T("@");
  KeyTerm* comma = T(",");
  KeyTerm* colon = T(":");
  KeyTerm* semicolon = T(";");
  KeyTerm* OpenParen = T("(");
  KeyTerm* CloseParen = T(")");
  KeyTerm* CloseSquareParen = T("]");
  KeyTerm* OpenSquareParen = T("[");
  KeyTerm* exclamationMark = T("!");
  KeyTerm* CloseCurlyParen = T("}");
  KeyTerm* OpenCurlyParen = T("{");
  KeyTerm* QuoteS = T("'");
  KeyTerm* mulop = T("*");
  KeyTerm* plusop = T("+");
  KeyTerm* divop = T("/");
  KeyTerm* minop = T("-");
  KeyTerm* concatop = T("&");
  KeyTerm* expop = T("^");
  Terminal* intersectop = Make<ImpliedSymbolTerminal>("INTERSECT");
  KeyTerm* percentop = T("%");
  KeyTerm* gtop = T(">");
  KeyTerm* eqop = T("=");
  KeyTerm* ltop = T("<");
  KeyTerm* neqop = T("<>");
  KeyTerm* gteop = T(">=");
  KeyTerm* lteop = T("<=");

  auto Rx = [&](const char* name, const ustr& pattern, int priority) {
    RegexBasedTerminal* t = Make<RegexBasedTerminal>(name, pattern);
    t->priority = priority;
    return t;
  };

  // ---- Literals
  Terminal* BoolToken = Rx("BoolToken", U("TRUE|FALSE"), 0);
  Terminal* NumberToken = Make<NumberLiteral>("NumberToken");
  Terminal* TextToken = Make<StringLiteral>("TextToken", u'"');
  Terminal* SingleQuotedStringToken = Make<StringLiteral>("SingleQuotedString", u'\'');
  SingleQuotedStringToken->priority = -100;
  Terminal* ErrorToken = Rx("ErrorToken", U("#NULL!|#DIV/0!|#VALUE!|#NAME\\?|#NUM!|#N/A"), 0);
  KeyTerm* RefErrorToken = ToTerm(U("#REF!"), "RefErrorToken");

  // ---- Functions
  const ustr specialUdfChars =
      u"¡¢£¤¥¦§¨©«¬­®¯°±²³"
      u"´¶·¸¹»¼½¾¿×÷";
  Terminal* UDFToken =
      Rx("UDFToken", ustr(uR"RX(('[^<>"/\|?*]+\.xla'!|_xll\.)?[\w)RX") + specialUdfChars + ustr(uR"RX(\\.]+\()RX"), 1150);
  Terminal* ExcelRefFunctionToken = Rx("ExcelRefFunctionToken", U("(INDEX|OFFSET|INDIRECT)\\("), 1200);
  Terminal* ExcelConditionalRefFunctionToken = Rx("ExcelConditionalRefFunctionToken", U("(IF|CHOOSE)\\("), 1200);
  std::vector<ustr> fnNames;
  for (size_t i = 0; i < sizeof(kExcelFunctions) / sizeof(kExcelFunctions[0]); ++i) fnNames.push_back(U(kExcelFunctions[i]));
  Terminal* ExcelFunction = Make<FunctionListTerminal>("ExcelFunction", fnNames);
  ExcelFunction->priority = 1200;
  Terminal* EmptyArgumentToken = Make<ImpliedSymbolTerminal>("EmptyArgumentToken");

  // ---- References and names
  const ustr col = U("(?:[A-Z]{1,2}|[A-W][A-Z]{1,2}|X[A-E][A-Z]|XF[A-D])");
  Terminal* VRangeToken = Rx("VRangeToken", U("[$]?") + col + U(":[$]?") + col, 0);
  Terminal* HRangeToken = Rx("HRangeToken", U("[$]?[1-9][0-9]*:[$]?[1-9][0-9]*"), 0);
  const ustr cellRegex = U("[$]?") + col + U("[$]?[1-9][0-9]*");
  Terminal* CellToken = Rx("CellToken", cellRegex, 1000);
  const ustr nameStart = U("[\\p{L}\\\\_]");
  const ustr nameValid = U("[\\w\\\\_\\.\\?]");
  Terminal* NameToken = Rx("NameToken", nameStart + nameValid + U("*"), -800);
  const ustr invalidWords = U("((TRUE|FALSE)") + nameValid + U("+)") + U("|(") + cellRegex +
                            U("[\\p{Ll}\\p{Lu}\\p{Lt}\\p{Lo}\\p{Lm}\\p{Pc}\\\\_\\.\\?]") + nameValid + U("*)");
  Terminal* NamedRangeCombinationToken = Rx("NamedRangeCombinationToken", invalidWords + nameValid + U("+"), 1100);
  Terminal* ReservedNameToken = Rx("ReservedNameToken", U("_xlnm\\.[a-zA-Z_]+"), -700);
  Terminal* SRColumnToken = Rx("SRColumn", U("[\\w\\\\.]+"), -900);

  // ---- Prefixes
  const ustr mustBeQuoted = ustr(uR"RX(\(\);{}#"=<>&+\-*/\^%, )RX");
  const ustr notSheetName = ustr(uR"RX('*\[\]\\:/?)RX");
  const ustr normalSheetName = U("[^") + notSheetName + mustBeQuoted + U("]+");
  const ustr quotedSheetName = U("([^") + notSheetName + U("]|'')*");
  Terminal* SheetToken = Rx("SheetNameToken", normalSheetName + U("!"), 1200);
  Terminal* SheetQuotedToken = Rx("SheetNameQuotedToken", quotedSheetName + U("'!"), 1200);
  Terminal* MultipleSheetsToken = Rx("MultipleSheetsToken", normalSheetName + U(":") + normalSheetName + U("!"), 100);
  Terminal* MultipleSheetsQuotedToken =
      Rx("MultipleSheetsQuotedToken", quotedSheetName + U(":") + quotedSheetName + U("'!"), 100);
  Terminal* FileToken = Rx("FileNameNumericToken", U("\\[[0-9]+\\]"), 1200);
  Terminal* EnclosedInBracketsToken = Rx("EnclosedInBracketsToken", U("\\[[^\\[\\]]+\\]"), -500);
  Terminal* FilePathWindowsToken =
      Rx("FilePathWindowsToken", ustr(uR"RX((?:[a-zA-Z]\:|\\\\[\w\.]+\\[\w.$]+)\\(([^<>:"/\|?*\\]| )+\\)*)RX"), 0);

  // ---- Non-terminals
  auto NT = [&](const char* n) { return Make<NonTerminal>(n); };
  NonTerminal* Argument = NT("Argument");
  NonTerminal* Arguments = NT("Arguments");
  NonTerminal* ArrayColumns = NT("ArrayColumns");
  NonTerminal* ArrayConstant = NT("ArrayConstant");
  NonTerminal* ArrayFormula = NT("ArrayFormula");
  NonTerminal* ArrayRows = NT("ArrayRows");
  NonTerminal* Bool = NT("Bool");
  NonTerminal* Cell = NT("Cell");
  NonTerminal* Constant = NT("Constant");
  NonTerminal* ConstantArray = NT("ConstantArray");
  NonTerminal* DynamicDataExchange = NT("DynamicDataExchange");
  NonTerminal* EmptyArgument = NT("EmptyArgument");
  NonTerminal* Error = NT("Error");
  NonTerminal* File = NT("File");
  NonTerminal* Formula = NT("Formula");
  NonTerminal* FormulaWithEq = NT("FormulaWithEq");
  NonTerminal* FunctionCall = NT("FunctionCall");
  NonTerminal* FunctionName = NT("FunctionName");
  NonTerminal* HRange = NT("HRange");
  NonTerminal* InfixOp = NT("InfixOp");
  NonTerminal* NamedRange = NT("NamedRange");
  NonTerminal* Number = NT("Number");
  NonTerminal* PostfixOp = NT("PostfixOp");
  NonTerminal* Prefix = NT("Prefix");
  NonTerminal* PrefixOp = NT("PrefixOp");
  NonTerminal* Reference = NT("Reference");
  NonTerminal* ReferenceItem = NT("ReferenceItem");
  NonTerminal* ReferenceFunctionCall = NT("ReferenceFunctionCall");
  NonTerminal* RefError = NT("RefError");
  NonTerminal* RefFunctionName = NT("RefFunctionName");
  NonTerminal* ReservedName = NT("ReservedName");
  NonTerminal* Start = NT("Start");
  NonTerminal* StructuredReference = NT("StructuredReference");
  NonTerminal* StructuredReferenceElement = NT("StructuredReferenceElement");
  NonTerminal* StructuredReferenceExpression = NT("StructuredReferenceExpression");
  NonTerminal* StructuredReferenceTable = NT("StructuredReferenceTable");
  NonTerminal* Text = NT("Text");
  NonTerminal* UDFName = NT("UDFName");
  NonTerminal* UDFunctionCall = NT("UDFunctionCall");
  NonTerminal* Union = NT("Union");
  NonTerminal* VRange = NT("VRange");

  auto PreferShiftHere = [&]() -> BnfTerm* { return Make<PreferredActionHint>(true); };
  auto ImplyPrecedenceHere = [&](int p) -> BnfTerm* { return Make<ImpliedPrecedenceHint>(p, Assoc::Left); };
  auto MarkPunctuation = [&](std::initializer_list<BnfTerm*> ts) {
    for (BnfTerm* t : ts) t->SetFlag(IsPunctuation | NoAstNode);
  };
  auto MarkTransient = [&](NonTerminal* nt) { nt->flags |= IsTransient | NoAstNode; };
  // Grammar.MakeListRule (PlusList / StarList both carry AddPreferShiftHint)
  auto MakeListRule = [&](NonTerminal* list, BnfTerm* delimiter, BnfTerm* listMember, bool star) {
    NonTerminal* plusList = star ? Make<NonTerminal>(listMember->name + "+") : list;
    plusList->SetFlag(IsList);
    plusList->rule = {BnfTermList{plusList, delimiter, PreferShiftHere(), listMember}, BnfTermList{listMember}};
    if (star) {
      list->rule = {BnfTermList{Empty}, BnfTermList{plusList}};
      plusList->SetFlag(NoAstNode);
      list->SetFlag(IsListContainer);
    }
  };

  const int kComparison = 1, kConcatenation = 2, kAddition = 3, kMultiplication = 4, kExponentiation = 5,
            kUnaryPostFix = 6, kUnaryPreFix = 7, kUnion = 9, kIntersection = 10, kRange = 11;

  MarkPunctuation({OpenParen, CloseParen});
  MarkPunctuation({OpenSquareParen, CloseSquareParen});
  MarkPunctuation({OpenCurlyParen, CloseCurlyParen});

  root = Start;
  Start->rule = {{FormulaWithEq}, {Formula}, {ArrayFormula}};
  MarkTransient(Start);
  ArrayFormula->rule = {{OpenCurlyParen, eqop, Formula, CloseCurlyParen}};
  FormulaWithEq->rule = {{eqop, Formula}};
  Formula->rule = {{Reference}, {Constant}, {FunctionCall}, {ConstantArray}, {OpenParen, Formula, CloseParen}, {ReservedName}};
  ReservedName->rule = {{ReservedNameToken}};
  Constant->rule = {{Number}, {Text}, {Bool}, {Error}};
  Text->rule = {{TextToken}};
  Number->rule = {{NumberToken}};
  Bool->rule = {{BoolToken}};
  Error->rule = {{ErrorToken}};
  RefError->rule = {{RefErrorToken}};

  FunctionCall->rule = {{FunctionName, Arguments, CloseParen}, {PrefixOp, Formula}, {Formula, PostfixOp}, {Formula, InfixOp, Formula}};
  FunctionName->rule = {{ExcelFunction}};
  MakeListRule(Arguments, comma, Argument, /*star*/ true);
  EmptyArgument->rule = {{EmptyArgumentToken}};
  Argument->rule = {{Formula}, {EmptyArgument}};
  PrefixOp->rule = {{ImplyPrecedenceHere(kUnaryPreFix), plusop}, {ImplyPrecedenceHere(kUnaryPreFix), minop}};
  MarkTransient(PrefixOp);
  InfixOp->rule = {{expop}, {mulop}, {divop}, {plusop}, {minop}, {concatop}, {gtop}, {eqop}, {ltop}, {neqop}, {gteop}, {lteop}};
  MarkTransient(InfixOp);
  PostfixOp->rule = {{PreferShiftHere(), percentop}};
  MarkTransient(PostfixOp);

  Reference->rule = {{ReferenceItem},
                     {ReferenceFunctionCall},
                     {OpenParen, Reference, PreferShiftHere(), CloseParen},
                     {Prefix, ReferenceItem},
                     {DynamicDataExchange}};
  ReferenceFunctionCall->rule = {{Reference, colon, Reference},
                                 {Reference, intersectop, Reference},
                                 {OpenParen, Union, CloseParen},
                                 {RefFunctionName, Arguments, CloseParen}};
  RefFunctionName->rule = {{ExcelRefFunctionToken}, {ExcelConditionalRefFunctionToken}};
  MakeListRule(Union, comma, Reference, /*star*/ false);
  ReferenceItem->rule = {{Cell}, {NamedRange}, {VRange}, {HRange}, {RefError}, {UDFunctionCall}, {StructuredReference}};
  MarkTransient(ReferenceItem);
  UDFunctionCall->rule = {{UDFName, Arguments, CloseParen}};
  UDFName->rule = {{UDFToken}};
  VRange->rule = {{VRangeToken}};
  HRange->rule = {{HRangeToken}};
  Cell->rule = {{CellToken}};
  File->rule = {{FileToken}, {EnclosedInBracketsToken}, {FilePathWindowsToken, EnclosedInBracketsToken}};
  DynamicDataExchange->rule = {{File, exclamationMark, SingleQuotedStringToken}};
  NamedRange->rule = {{NameToken}, {NamedRangeCombinationToken}};
  Prefix->rule = {{SheetToken},
                  {QuoteS, SheetQuotedToken},
                  {File, SheetToken},
                  {QuoteS, File, SheetQuotedToken},
                  {File, exclamationMark},
                  {MultipleSheetsToken},
                  {QuoteS, MultipleSheetsQuotedToken},
                  {File, MultipleSheetsToken},
                  {QuoteS, File, MultipleSheetsQuotedToken}};
  NonTerminal* SRE = StructuredReferenceElement;
  SRE->rule = {{OpenSquareParen, SRColumnToken, CloseSquareParen}, {OpenSquareParen, NameToken, CloseSquareParen}, {EnclosedInBracketsToken}};
  StructuredReferenceTable->rule = {{NameToken}};
  StructuredReferenceExpression->rule = {{SRE},
                                         {at, SRE},
                                         {SRE, colon, SRE},
                                         {at, SRE, colon, SRE},
                                         {SRE, comma, SRE},
                                         {SRE, comma, SRE, colon, SRE},
                                         {SRE, comma, SRE, comma, SRE},
                                         {SRE, comma, SRE, comma, SRE, colon, SRE}};
  StructuredReference->rule = {{SRE},
                               {OpenSquareParen, StructuredReferenceExpression, CloseSquareParen},
                               {StructuredReferenceTable, SRE},
                               {StructuredReferenceTable, OpenSquareParen, CloseSquareParen},
                               {StructuredReferenceTable, OpenSquareParen, StructuredReferenceExpression, CloseSquareParen}};
  ConstantArray->rule = {{OpenCurlyParen, ArrayColumns, CloseCurlyParen}};
  MakeListRule(ArrayColumns, semicolon, ArrayRows, false);
  MakeListRule(ArrayRows, comma, ArrayConstant, false);
  ArrayConstant->rule = {{Constant}, {PrefixOp, Number}, {RefError}};

  auto RegisterOperators = [&](int prec, std::initializer_list<BnfTerm*> ts) {
    for (BnfTerm* t : ts) {
      t->SetFlag(IsOperator);
      t->precedence = prec;
      t->assoc = Assoc::Left;
    }
  };
  RegisterOperators(kComparison, {eqop, ltop, gtop, lteop, gteop, neqop});
  RegisterOperators(kConcatenation, {concatop});
  RegisterOperators(kAddition, {plusop, minop});
  RegisterOperators(kMultiplication, {mulop, divop});
  RegisterOperators(kExponentiation, {expop});
  RegisterOperators(kUnaryPostFix, {percentop});
  RegisterOperators(kUnion, {comma});
  RegisterOperators(kIntersection, {intersectop});
  RegisterOperators(kRange, {colon});
}

// ---------------------------- GrammarDataBuilder ----------------------------

void Language::BuildGrammarData() {
  GrammarData& gd = grammarData;
  // CreateAugmentedRoots
  NonTerminal* aug = Make<NonTerminal>(root->name + "'");
  aug->rule = {{root, Eof}};
  aug->SetFlag(NoAstNode);
  gd.augmentedRoot = aug;

  // CollectTermsFromGrammar
  std::vector<BnfTerm*> todo;
  struct Collector {
    GrammarData& gd;
    void Collect(BnfTerm* term) {
      if (gd.allTerms.Contains(term)) return;
      gd.allTerms.Add(term);
      if (term->kind != BnfTerm::kNonTerminal) return;
      NonTerminal* nt = static_cast<NonTerminal*>(term);
      for (BnfTermList& elemList : nt->rule)
        for (size_t i = 0; i < elemList.size(); ++i) Collect(elemList[i]);
    }
  } collector{gd};
  collector.Collect(aug);
  gd.allTerms.Add(SyntaxError);

  // InitTermLists
  for (BnfTerm* term : gd.allTerms.ToList()) {
    if (term->kind == BnfTerm::kNonTerminal) gd.nonTerminals.Add(static_cast<NonTerminal*>(term));
    if (term->kind == BnfTerm::kTerminal && term != Empty) gd.terminals.Add(static_cast<Terminal*>(term));
  }
  for (Terminal* term : gd.terminals.ToList()) {
    KeyTerm* kt = dynamic_cast<KeyTerm*>(term);
    if (kt == nullptr) continue;
    if (!kt->text.empty() && NetIsLetter(kt->text[0])) kt->SetFlag(IsKeyword);
  }
  for (BnfTerm* term : gd.allTerms.ToList()) {
    if (term->kind == BnfTerm::kTerminal) static_cast<Terminal*>(term)->Init(gd);
  }

  // CreateProductions
  int lastItemId = 0;
  for (NonTerminal* nt : gd.nonTerminals.ToList()) {
    nt->productions.clear();
    for (BnfTermList& operands : nt->rule) {
      Production* prod = MakeObj<Production>();
      prod->lvalue = nt;
      std::vector<GrammarHint*> hints;
      bool haveHints = false;
      for (BnfTerm* operand : operands) {
        if (operand == Empty) continue;
        if (operand->kind == BnfTerm::kHint) {
          hints.push_back(static_cast<GrammarHint*>(operand));
          haveHints = true;
          continue;
        }
        prod->rvalues.push_back(operand);
        LR0Item* item = MakeObj<LR0Item>();
        item->id = lastItemId++;
        item->production = prod;
        item->position = static_cast<int>(prod->rvalues.size()) - 1;
        item->current = prod->rvalues[item->position];
        if (haveHints) item->hints = hints;
        prod->lr0Items.push_back(item);
        hints.clear();
        haveHints = false;
      }
      prod->flags = 0;
      for (BnfTerm* rv : prod->rvalues)
        if (rv->kind == BnfTerm::kTerminal) prod->flags |= 0x02;  // HasTerminals
      LR0Item* fin = MakeObj<LR0Item>();
      fin->id = lastItemId++;
      fin->production = prod;
      fin->position = static_cast<int>(prod->rvalues.size());
      fin->current = nullptr;
      if (haveHints) fin->hints = hints;
      prod->lr0Items.push_back(fin);
      nt->productions.push_back(prod);
    }
  }

  // ComputeNonTerminalsNullability
  std::vector<NonTerminal*> undecided = gd.nonTerminals.ToList();
  while (!undecided.empty()) {
    std::vector<NonTerminal*> newUndecided;
    for (NonTerminal* nt : undecided) {
      bool decided = false;
      for (Production* prod : nt->productions) {
        if (prod->rvalues.empty()) { nt->SetFlag(IsNullable); decided = true; break; }
        if (prod->flags & 0x02) continue;
        bool allNullable = true;
        for (BnfTerm* child : prod->rvalues) allNullable &= child->IsSet(IsNullable);
        if (allNullable) { nt->SetFlag(IsNullable); decided = true; break; }
      }
      if (!decided) newUndecided.push_back(nt);
    }
    if (undecided.size() == newUndecided.size()) break;
    undecided = newUndecided;
  }

  // ComputeTailsNullability
  for (NonTerminal* nt : gd.nonTerminals.ToList()) {
    for (Production* prod : nt->productions) {
      int count = static_cast<int>(prod->lr0Items.size());
      for (int i = count - 1; i >= 0; i--) {
        LR0Item* item = prod->lr0Items[i];
        item->tailIsNullable = true;
        if (item->current == nullptr) continue;
        if (!item->current->IsSet(IsNullable)) break;
      }
    }
  }
}

// ---------------------------- ScannerDataBuilder ----------------------------

void Language::BuildScannerData() {
  GrammarData& gd = grammarData;
  allTerminalsList = gd.terminals.ToList();
  std::vector<char16_t> keysInOrder;
  for (Terminal* term : allTerminalsList) {
    if (term->IsSet(IsNonScanner | IsNonGrammar)) continue;
    std::vector<ustr> firsts = term->GetFirsts();
    if (firsts.empty()) {
      gd.noPrefixTerminals.Add(term);
      continue;
    }
    for (const ustr& prefix : firsts) {
      if (prefix.empty()) continue;
      char16_t firstChar = prefix[0];
      char16_t variants[2] = {ToLowerCulture(firstChar), ToUpperAscii(firstChar)};
      for (char16_t ch : variants) {
        auto it = terminalsLookup.find(ch);
        if (it == terminalsLookup.end()) {
          terminalsLookup[ch] = std::vector<Terminal*>();
          it = terminalsLookup.find(ch);
          keysInOrder.push_back(ch);
        }
        if (std::find(it->second.begin(), it->second.end(), term) == it->second.end()) it->second.push_back(term);
      }
    }
  }
  if (gd.noPrefixTerminals.Count() > 0) {
    noPrefixTerminalsSorted = gd.noPrefixTerminals.ToList();
    int n = static_cast<int>(noPrefixTerminalsSorted.size());
    NetIntroSort s{noPrefixTerminalsSorted, &CompareByPriorityReverse};
    s.Sort(n > 4 ? n : 4);  // AddRange(ICollection) on an empty list: capacity = max(4, n)
    for (char16_t key : keysInOrder) {
      std::vector<Terminal*>& list = terminalsLookup[key];
      for (Terminal* ft : noPrefixTerminalsSorted)
        if (std::find(list.begin(), list.end(), ft) == list.end()) list.push_back(ft);
    }
  }
  for (char16_t key : keysInOrder) {
    std::vector<Terminal*>& list = terminalsLookup[key];
    if (list.size() > 1) {
      NetIntroSort s{list, &CompareByPriorityReverse};
      s.Sort(ListCapacityAfterAdds(static_cast<int>(list.size())));
    }
  }
}

// ---------------------------- ParserDataBuilder ----------------------------

class ParserDataBuilder {
 public:
  explicit ParserDataBuilder(Language& l) : lang_(l) {}

  void Build() {
    CreateParserStates();
    NetSet<LRItem*> itemsNeedLookaheads = GetReduceItemsInInadequateState();
    ComputeTransitions(itemsNeedLookaheads);
    ComputeLookaheads(itemsNeedLookaheads);
    ComputeStatesExpectedTerminals();
    ComputeConflicts();
    ApplyHints();
    HandleUnresolvedConflicts();
    CreateRemainingReduceActions();
    lang_.errorAction = lang_.MakeObj<ErrorRecoveryParserAction>();
    ComputeReportedExpectedSets();
  }

 private:
  static std::string ComputeLR0ItemSetKey(const NetSet<LR0Item*>& items) {
    std::vector<LR0Item*> list = items.ToList();
    if (list.empty()) return std::string();
    if (list.size() == 1) return std::to_string(list[0]->id);
    std::vector<int> ids;
    for (LR0Item* i : list) ids.push_back(i->id);
    std::sort(ids.begin(), ids.end());
    std::string key;
    for (int id : ids) { key += std::to_string(id); key += ","; }
    return key;
  }

  ParserState* FindOrCreateState(const NetSet<LR0Item*>& coreItems) {
    std::string key = ComputeLR0ItemSetKey(coreItems);
    auto it = stateHash_.find(key);
    if (it != stateHash_.end()) return it->second;
    ParserState* state = lang_.MakeObj<ParserState>();
    state->name = "S" + std::to_string(lang_.states.size());
    state->builderData = lang_.MakeObj<ParserStateData>(lang_, state, coreItems);
    lang_.states.push_back(state);
    stateHash_[key] = state;
    return state;
  }

  void CreateParserStates() {
    NonTerminal* aug = lang_.grammarData.augmentedRoot;
    NetSet<LR0Item*> ini;
    ini.Add(aug->productions[0]->lr0Items[0]);
    lang_.initialState = FindOrCreateState(ini);
    ExpandParserStateList(0);
    // CreateAcceptAction
    BnfTerm* rootTerm = aug->productions[0]->rvalues[0];
    ShiftParserAction* shiftAction = dynamic_cast<ShiftParserAction*>(lang_.initialState->FindAction(rootTerm));
    shiftAction->newState->actions[lang_.Eof] = lang_.MakeObj<AcceptParserAction>();
  }

  void ExpandParserStateList(int initialIndex) {
    for (size_t index = initialIndex; index < lang_.states.size(); index++) {
      ParserState* state = lang_.states[index];
      for (BnfTerm* term : state->builderData->shiftTerms.ToList()) {
        NetSet<LRItem*> shiftItems;
        for (LRItem* it : state->builderData->shiftItems.ToList())
          if (it->core->current == term) shiftItems.Add(it);
        NetSet<LR0Item*> shiftedCoreItems;
        for (LRItem* it : shiftItems.ToList())
          if (it->core->ShiftedItem() != nullptr) shiftedCoreItems.Add(it->core->ShiftedItem());
        ParserState* newState = FindOrCreateState(shiftedCoreItems);
        state->actions[term] = lang_.MakeObj<ShiftParserAction>(term, newState);
        for (LRItem* shiftItem : shiftItems.ToList()) {
          LR0Item* target = shiftItem->core->ShiftedItem();
          shiftItem->shiftedItem = nullptr;
          for (LRItem* cand : newState->builderData->allItems.ToList())
            if (cand->core == target) { shiftItem->shiftedItem = cand; break; }
        }
      }
    }
  }

  NetSet<LRItem*> GetReduceItemsInInadequateState() {
    NetSet<LRItem*> result;
    for (ParserState* state : lang_.states)
      if (state->builderData->isInadequate) result.UnionWith(state->builderData->reduceItems);
    return result;
  }

  void ComputeTransitions(const NetSet<LRItem*>& forItems) {
    NetSet<LRItem*> newItemsNeedLookbacks = forItems;
    while (newItemsNeedLookbacks.Count() > 0) {
      std::vector<Transition*> newTransitions = CreateLookbackTransitions(newItemsNeedLookbacks);
      newItemsNeedLookbacks = SelectNewItemsThatNeedLookback(newTransitions);
    }
  }

  NetSet<LRItem*> SelectNewItemsThatNeedLookback(const std::vector<Transition*>& transitions) {
    NetSet<LRItem*> items;
    for (Transition* trans : transitions)
      for (LRItem* item : trans->items.ToList())
        if (item->core->tailIsNullable && item->lookbacks.Count() == 0) items.Add(item);
    return items;
  }

  std::vector<Transition*> CreateLookbackTransitions(const NetSet<LRItem*>& sourceItems) {
    std::vector<Transition*> newTransitions;
    NetSet<LR0Item*> iniCores;
    for (LRItem* sourceItem : sourceItems.ToList()) iniCores.Add(sourceItem->core->production->lr0Items[0]);
    for (ParserState* state : lang_.states) {
      for (LRItem* iniItem : state->builderData->initialItems.ToList()) {
        if (!iniCores.Contains(iniItem->core)) continue;
        NonTerminal* iniItemNt = iniItem->core->production->lvalue;
        Transition* lookback = nullptr;
        LRItem* currItem = iniItem;
        while (currItem != nullptr) {
          if (sourceItems.Contains(currItem)) {
            if (lookback == nullptr) {
              auto it = state->builderData->transitions.find(iniItemNt);
              if (it != state->builderData->transitions.end()) {
                lookback = it->second;
              } else {
                lookback = lang_.MakeObj<Transition>(state, iniItemNt);
                newTransitions.push_back(lookback);
              }
            }
            if (currItem->core->IsFinal()) currItem->lookbacks.Add(lookback);
            else currItem->transition->Include(lookback);
          }
          currItem = currItem->shiftedItem;
        }
      }
    }
    return newTransitions;
  }

  void ComputeLookaheads(const NetSet<LRItem*>& forItems) {
    for (LRItem* reduceItem : forItems.ToList()) {
      NetSet<ParserState*> sourceStates;
      for (Transition* lookbackTrans : reduceItem->lookbacks.ToList()) {
        sourceStates.Add(lookbackTrans->toState);
        sourceStates.UnionWith(lookbackTrans->toState->builderData->ReadStateSet());
        for (Transition* includeTrans : lookbackTrans->includes.ToList()) {
          sourceStates.Add(includeTrans->toState);
          sourceStates.UnionWith(includeTrans->toState->builderData->ReadStateSet());
        }
      }
      for (ParserState* state : sourceStates.ToList()) reduceItem->lookaheads.UnionWith(state->builderData->shiftTerminals);
      if (reduceItem->lookaheads.Contains(lang_.SyntaxError)) reduceItem->lookaheads.Remove(lang_.SyntaxError);
    }
  }

  void ComputeStatesExpectedTerminals() {
    for (ParserState* state : lang_.states) {
      state->expectedTerminals.UnionWith(state->builderData->shiftTerminals);
      for (LRItem* reduceItem : state->builderData->reduceItems.ToList()) state->expectedTerminals.UnionWith(reduceItem->lookaheads);
      if (state->expectedTerminals.Contains(lang_.SyntaxError)) state->expectedTerminals.Remove(lang_.SyntaxError);
      if (state->expectedTerminals.Contains(lang_.Eof)) state->expectedTerminals.Remove(lang_.Eof);
    }
  }

  void ComputeConflicts() {
    for (ParserState* state : lang_.states) {
      ParserStateData* sd = state->builderData;
      if (!sd->isInadequate) continue;
      sd->conflicts.Clear();
      NetSet<Terminal*> allLkhds;
      for (LRItem* item : sd->reduceItems.ToList()) {
        for (Terminal* lkh : item->lookaheads.ToList()) {
          if (allLkhds.Contains(lkh)) sd->conflicts.Add(lkh);
          allLkhds.Add(lkh);
        }
      }
      for (Terminal* term : sd->shiftTerminals.ToList())
        if (allLkhds.Contains(term)) sd->conflicts.Add(term);
    }
  }

  void ApplyHints() {
    for (ParserState* state : lang_.states) {
      ParserStateData* sd = state->builderData;
      if (sd->conflicts.Count() > 0) {
        for (Terminal* conflict : sd->conflicts.ToList()) {
          if (conflict->IsSet(IsOperator)) {
            LRItem* reduceItem = nullptr;
            for (LRItem* it : sd->reduceItems.ToList())
              if (it->lookaheads.Contains(conflict)) { reduceItem = it; break; }
            reduceItem->core->hints.push_back(lang_.Make<PrecedenceHint>());
          }
        }
      }
      for (LRItem* item : sd->allItems.ToList()) {
        std::vector<GrammarHint*> hints = item->core->hints;
        for (GrammarHint* hint : hints) hint->Apply(lang_, item);
      }
    }
  }

  void HandleUnresolvedConflicts() {
    for (ParserState* state : lang_.states) {
      ParserStateData* sd = state->builderData;
      if (sd->conflicts.Count() == 0) continue;
      for (Terminal* conflict : sd->conflicts.ToList()) {
        if (sd->shiftTerminals.Contains(conflict)) continue;  // shift-reduce: shift (already present)
        LRItem* first = nullptr;
        for (LRItem* it : sd->reduceItems.ToList())
          if (it->lookaheads.Contains(conflict)) { first = it; break; }
        state->actions[conflict] = lang_.MakeObj<ReduceParserAction>(first->core->production);
      }
    }
  }

  void CreateRemainingReduceActions() {
    for (ParserState* state : lang_.states) {
      if (state->defaultAction != nullptr) continue;
      ParserStateData* sd = state->builderData;
      if (sd->shiftItems.Count() == 0 && sd->reduceItems.Count() == 1) {
        state->defaultAction = ReduceParserAction::Create(lang_, sd->reduceItems.First()->core->production);
        continue;
      }
      for (LRItem* item : sd->reduceItems.ToList()) {
        ReduceParserAction* action = ReduceParserAction::Create(lang_, item->core->production);
        for (Terminal* lkh : item->lookaheads.ToList()) {
          if (state->actions.count(lkh)) continue;
          state->actions[lkh] = action;
        }
      }
    }
  }

  // ParserDataBuilder.ComputeGroupedExpectedSetForState (no report groups in this grammar).
  void ComputeReportedExpectedSets() {
    for (ParserState* state : lang_.states) {
      std::vector<std::string> result;
      std::unordered_set<std::string> seen;
      for (Terminal* t : state->expectedTerminals.ToList()) {
        if (seen.insert(t->errorAlias).second) result.push_back(t->errorAlias);
      }
      state->reportedExpectedSet = result;
    }
  }

  Language& lang_;
  std::unordered_map<std::string, ParserState*> stateHash_;
};

void Language::BuildParserData() {
  ParserDataBuilder b(*this);
  b.Build();
}

// =====================================================================================
//  Scanner + Parser runtime
// =====================================================================================

class Parser {
 public:
  explicit Parser(Language& l) : lang_(l), ctx_(l) {}

  ParsingContext& Parse(const ustr& text) {
    ctx_.currentParserState = lang_.initialState;
    ctx_.currentParserInput = nullptr;
    ctx_.stack.clear();
    ParseTreeNode* ini = ctx_.NewNode();
    ini->state = lang_.initialState;
    ctx_.stack.push_back(ini);
    ctx_.currentTerminals.clear();
    ctx_.currentToken = nullptr;
    ctx_.source.reset(new SourceStream(text, &ctx_));
    ctx_.status = ParserStatus::Parsing;
    while (ctx_.status == ParserStatus::Parsing) ExecuteNextAction();
    return ctx_;
  }

 private:
  // ---------------- Parser ----------------
  void ExecuteNextAction() {
    if (ctx_.currentParserInput == nullptr && ctx_.currentParserState->defaultAction == nullptr) ReadInput();
    if (ctx_.currentParserInput != nullptr && ctx_.currentParserInput->isError) {
      lang_.errorAction->Execute(ctx_);
      return;
    }
    ParserAction* action = GetNextAction();
    if (action == nullptr) {
      lang_.errorAction->Execute(ctx_);
      return;
    }
    action->Execute(ctx_);
  }

  ParserAction* GetNextAction() {
    ParserState* currState = ctx_.currentParserState;
    ParseTreeNode* currInput = ctx_.currentParserInput;
    if (currState->defaultAction != nullptr) return currState->defaultAction;
    Token* inputToken = currInput->token;
    if (inputToken != nullptr && inputToken->keyTerm != nullptr) {
      Terminal* keyTerm = inputToken->keyTerm;
      ParserAction* a = currState->FindAction(keyTerm);
      if (a != nullptr) {
        inputToken->terminal = keyTerm;
        currInput->term = keyTerm;
        currInput->precedence = keyTerm->precedence;
        currInput->assoc = keyTerm->assoc;
        return a;
      }
    }
    return currState->FindAction(currInput->term);
  }

  void ReadInput() {
    Token* token;
    Terminal* term;
    do {
      token = GetToken();
      term = token->terminal;
    } while (term->IsSet(IsNonGrammar) && term != lang_.Eof);
    ctx_.currentParserInput = ctx_.NewTokenNode(token);
  }

  // ---------------- Scanner ----------------
  Token* GetToken() {
    ctx_.currentToken = nullptr;
    NextToken();
    return ctx_.currentToken;
  }

  void SkipWhitespace() {
    SourceStream& s = *ctx_.source;
    while (!s.EOF_()) {
      char16_t c = s.PreviewChar();
      if (c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\v') { s.previewPosition++; continue; }
      return;
    }
  }

  void NextToken() {
    SkipWhitespace();
    SourceStream& s = *ctx_.source;
    s.SetPosition(s.previewPosition);
    if (s.EOF_()) {
      ctx_.currentToken = ctx_.NewToken(lang_.Eof, s.location, ustr());
      return;
    }
    ScanToken();
  }

  void ScanToken() {
    if (!MatchRegularTerminals()) MatchAllTerminals();
    Token* token = ctx_.currentToken;
    SourceStream& src = *ctx_.source;
    if (token != nullptr && !token->IsError()) {
      src.previewPosition = src.Position() + token->Length();
      src.SetPosition(src.previewPosition);
      return;
    }
    if (token == nullptr) {
      char16_t pc = src.PreviewChar();
      std::string msg = "Invalid character: '" + Utf16ToUtf8(ustr(1, pc)) + "'.";
      ctx_.currentToken = ctx_.CreateErrorToken(msg);
    }
    Recover();
  }

  bool Recover() {
    SourceStream& src = *ctx_.source;
    src.previewPosition++;
    while (!src.EOF_()) {
      if (IsWhitespaceOrDelimiter(src.PreviewChar())) {
        src.SetPosition(src.previewPosition);
        return true;
      }
      src.previewPosition++;
    }
    return false;
  }

  bool MatchRegularTerminals() {
    ComputeCurrentTerminals();
    MatchTerminals();
    return ctx_.currentToken != nullptr;
  }

  void MatchAllTerminals() {
    ctx_.currentTerminals = lang_.allTerminalsList;
    MatchTerminals();
  }

  void ComputeCurrentTerminals() {
    ctx_.currentTerminals.clear();
    const std::vector<Terminal*>* termsForCurrentChar;
    auto it = lang_.terminalsLookup.find(ctx_.source->PreviewChar());
    if (it != lang_.terminalsLookup.end()) termsForCurrentChar = &it->second;
    else termsForCurrentChar = &lang_.noPrefixTerminalsSorted;
    ParserState* parserState = ctx_.currentParserState;
    for (Terminal* term : *termsForCurrentChar)
      if (parserState->expectedTerminals.Contains(term->outputTerminal)) ctx_.currentTerminals.push_back(term);
  }

  void MatchTerminals() {
    Token* priorToken = nullptr;
    SourceStream& src = *ctx_.source;
    for (size_t i = 0; i < ctx_.currentTerminals.size(); i++) {
      Terminal* term = ctx_.currentTerminals[i];
      if (priorToken != nullptr && priorToken->terminal->priority > term->priority) return;
      src.previewPosition = src.location.position;
      Token* token = term->TryMatch(ctx_, src);
      if (token == nullptr) continue;
      if (priorToken != nullptr && !priorToken->IsError() && token->Length() < priorToken->Length()) continue;
      ctx_.currentToken = token;
      term->OnValidateToken(ctx_);
      if (ctx_.currentToken != nullptr) priorToken = ctx_.currentToken;
    }
  }

  Language& lang_;
  ParsingContext ctx_;
};

void ErrorRecoveryParserAction::Execute(ParsingContext& ctx) {
  ctx.status = ParserStatus::Error;
  Language& lang = ctx.lang;
  // Grammar.ReportParseError
  std::string error;
  if (ctx.currentParserInput->term == lang.SyntaxError) {
    error = ctx.currentParserInput->token->errorMessage;
  } else {
    const std::vector<std::string>& expected = ctx.currentParserState->reportedExpectedSet;
    if (!expected.empty()) {
      error = "Syntax error, expected: ";
      for (size_t i = 0; i < expected.size(); ++i) {
        if (i) error += ", ";
        error += expected[i];
      }
    } else {
      error = "Syntax error, unexpected input.";
    }
  }
  ctx.AddParserError(error);
  if (ctx.currentParserInput->term == lang.Eof) return;
  // TryRecoverFromError: the grammar has no error productions, so recovery always fails.
  ctx.status = ParserStatus::Recovering;
  while (ctx.stack.size() >= 1) {
    ParserAction* a = ctx.currentParserState->FindAction(lang.SyntaxError);
    if (a != nullptr && dynamic_cast<ShiftParserAction*>(a) != nullptr) break;  // never happens
    if (ctx.stack.size() == 1) break;
    ctx.stack.pop_back();
    ctx.currentParserState = ctx.stack.back()->state;
  }
  ctx.status = ParserStatus::Error;
}

// =====================================================================================
//  Output (XLParser web demo Parse.cs + Newtonsoft.Json formatting)
// =====================================================================================

void AppendJsonString(std::string& out, const ustr& s) {
  static const char* hex = "0123456789abcdef";
  ustr esc;
  esc.reserve(s.size() + 2);
  esc.push_back('"');
  for (char16_t c : s) {
    switch (c) {
      case '"': esc += u"\\\""; break;
      case '\\': esc += u"\\\\"; break;
      case '\n': esc += u"\\n"; break;
      case '\r': esc += u"\\r"; break;
      case '\t': esc += u"\\t"; break;
      case '\f': esc += u"\\f"; break;
      case '\b': esc += u"\\b"; break;
      default:
        if (c < 0x20 || c == 0x85 || c == 0x2028 || c == 0x2029) {
          esc += u"\\u";
          esc.push_back(hex[(c >> 12) & 0xF]);
          esc.push_back(hex[(c >> 8) & 0xF]);
          esc.push_back(hex[(c >> 4) & 0xF]);
          esc.push_back(hex[c & 0xF]);
        } else {
          esc.push_back(c);
        }
    }
  }
  esc.push_back('"');
  out += Utf16ToUtf8(esc);
}

ustr NodeText(ParseTreeNode* node) {
  if (node->term->kind == BnfTerm::kNonTerminal) return U(node->term->name.c_str());
  const ustr& text = node->token->text;  // ExcelFormulaParser.Print for a terminal: token text
  if (node->term->name.size() <= 2) return text;
  return U(node->term->name.c_str()) + u"[\"" + text + u"\"]";
}

// Newtonsoft.Json Formatting.Indented output of the web demo's JsonNode tree ("name" first,
// "children" omitted when empty). Iterative, so arbitrarily deep trees cannot overflow the stack.
void WriteTree(std::string& out, ParseTreeNode* root) {
  struct Frame {
    ParseTreeNode* node;
    size_t next;  // index of next child to write
    int indent;
  };
  auto open = [&out](ParseTreeNode* node, int indent) {
    out += "{\n";
    out.append(static_cast<size_t>(indent + 2), ' ');
    out += "\"name\": ";
    AppendJsonString(out, NodeText(node));
    if (!node->children.empty()) {
      out += ",\n";
      out.append(static_cast<size_t>(indent + 2), ' ');
      out += "\"children\": [\n";
    }
  };
  std::vector<Frame> stack;
  open(root, 0);
  stack.push_back(Frame{root, 0, 0});
  while (!stack.empty()) {
    Frame& f = stack.back();
    ParseTreeNode* node = f.node;
    if (f.next < node->children.size()) {
      if (f.next > 0) out += ",\n";
      ParseTreeNode* child = node->children[f.next++];
      int childIndent = f.indent + 4;
      out.append(static_cast<size_t>(childIndent), ' ');
      open(child, childIndent);
      stack.push_back(Frame{child, 0, childIndent});
      continue;
    }
    if (!node->children.empty()) {
      out += "\n";
      out.append(static_cast<size_t>(f.indent + 2), ' ');
      out += "]";
    }
    out += "\n";
    out.append(static_cast<size_t>(f.indent), ' ');
    out += "}";
    stack.pop_back();
  }
}

// ExcelFormulaParser.ParseToTree post-processing: PrefixInfo.FixQuotedSheetNodeForWhitespace
// (a quoted sheet name keeps the whitespace that Irony skipped after the opening quote).
void FixQuotedSheetNodes(ParseTreeNode* root, const ustr& input) {
  std::vector<ParseTreeNode*> stack{root};
  while (!stack.empty()) {
    ParseTreeNode* node = stack.back();
    stack.pop_back();
    if (node->token != nullptr && node->term->name == "SheetNameQuotedToken") {
      int startIndex = node->location.position;
      while (startIndex > 0) {
        if (!NetIsWhiteSpace(input[startIndex - 1])) break;
        startIndex--;
      }
      if (startIndex != node->location.position) {
        int end = node->location.position + node->token->Length();
        node->token->text = input.substr(startIndex, end - startIndex);
        node->location.position = startIndex;
      }
    }
    for (ParseTreeNode* c : node->children) stack.push_back(c);
  }
}

Language& GetLanguage() {
  static Language* lang = new Language();  // built once (thread-safe static init), never destroyed
  return *lang;
}

std::string ParseToJson(const std::string& inputUtf8) {
  Language& lang = GetLanguage();
  ustr formula = Utf8ToUtf16(inputUtf8);
  Parser parser(lang);
  ParsingContext& ctx = parser.Parse(formula);
  bool hasErrors = !ctx.messages.empty();
  if (hasErrors || ctx.root == nullptr) {
    std::string out = "{\"error\":\"Parse error\",\"formula\":";
    AppendJsonString(out, formula);
    out += ",\"message\":";
    if (ctx.messages.empty()) {
      out += "null";
    } else {
      // ParserMessages.Sort(ByLocation) then FirstOrDefault
      const LogMessage* first = &ctx.messages[0];
      for (const LogMessage& m : ctx.messages)
        if (m.location.position < first->location.position) first = &m;
      out += "{\"level\":\"Error\",\"line\":" + std::to_string(first->location.line + 1) +
             ",\"column\":" + std::to_string(first->location.column + 1) + ",\"msg\":";
      AppendJsonString(out, Utf8ToUtf16(first->message));
      out += "}";
    }
    out += "}";
    return out;
  }
  FixQuotedSheetNodes(ctx.root, formula);
  std::string out;
  WriteTree(out, ctx.root);
  return out;
}

}  // namespace

std::string parseFormulaToJSON(const std::string& input) {
  try {
    return ParseToJson(input);
  } catch (...) {
    return "{\"error\":\"Internal error\"}";
  }
}

}  // namespace XLParserClone
