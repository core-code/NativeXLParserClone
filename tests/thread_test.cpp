// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// Concurrency check: several threads parse the same corpus at once; results must match a
// single-threaded run.
#include <atomic>
#include <iostream>
#include <string>
#include <thread>
#include <vector>
#include "xlparser_clone.h"
int main() {
  std::vector<std::string> corpus = {"SUM(H4:H5)", "=IF(A1>0,,-1)", "'My Sheet'!A1:B2*2", "Table1[[#Headers],[Col]]",
                                     "{1,2;3,4}", "1e999", "\"unterminated", "A1 B1", "-A1^2%", "INDEX((A1:B5,D1:E5),2,1,2)",
                                     "[1]Sheet1!A1+[Book.xlsx]S!B2", "LAMBDA(x,x*2)(A1)", "", "#REF!", "SUM(", "=\"a\"\"b\"&C3"};
  std::vector<std::string> expected;
  for (auto& f : corpus) expected.push_back(XLParserClone::parseFormulaToJSON(f));
  std::atomic<int> bad{0};
  std::vector<std::thread> ts;
  for (int t = 0; t < 8; ++t)
    ts.emplace_back([&, t] {
      for (int r = 0; r < 200; ++r)
        for (size_t i = 0; i < corpus.size(); ++i) {
          size_t k = (i + t + r) % corpus.size();
          if (XLParserClone::parseFormulaToJSON(corpus[k]) != expected[k]) ++bad;
        }
    });
  for (auto& th : ts) th.join();
  std::cout << "thread test mismatches: " << bad << "\n";
  return bad == 0 ? 0 : 1;
}
