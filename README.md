# XLParserClone

A native C++17 replacement for the XLParser Excel-formula parser. It has no dependencies beyond the
standard library and exposes a single public function:

```cpp
#include "xlparser_clone.h"

std::string json = XLParserClone::parseFormulaToJSON("SUM(H4:H5)");
```

* **Input:** the formula as UTF-8 text. A leading `=` is optional, as in XLParser.
* **Output:** UTF-8 JSON.
  * When the formula parses, the result is an indented tree of `{"name": …, "children": […]}` nodes.
  * When it doesn't, the result is a one-line object:
    `{"error":"Parse error","formula":…,"message":{"level":"Error","line":L,"column":C,"msg":…}}`
* The function is thread-safe and never throws.

## What exactly is being reproduced

The reference outputs (`tests/data/results.txt`) were produced by the **XLParser web demo**
(`app/XLParser.Web/Parse.cs`, the `Parse.json` endpoint) using its **`v139`** library snapshot. That
snapshot is the XLParser 1.3.9 grammar and function list, running on **Irony 1.2.0** under .NET
Framework 4.7.2, with Newtonsoft.Json for serialization. The data itself shows this:

* Node names such as `StructuredReferenceElement`, `StructuredReferenceTable` and
  `EnclosedInBracketsToken` only existed between v1.3.0 and v1.4.1.
* `XLOOKUP` and `XMATCH` are reported as UDFs, which matches the v1.3.5–1.3.9 function list.
* The output format (`Name["text"]` for terminals, bare text for names of ≤ 2 characters, the
  error-object layout) is exactly what `Parse.cs` produces.

Many outputs depend on Irony internals: syntax-error messages list the "expected" terminals in the
order Irony's LALR builder inserted them, operator conflicts are resolved at parse time from
precedence, and the scanner filters candidate tokens by the current parser state. So the library is
**a faithful port of the parts of Irony 1.2.0 that XLParser uses**, rather than a hand-written
parser:

* grammar data
* the LALR(1) table builder, including hints, precedence actions and .NET `HashSet` ordering
* the scanner, with terminal priorities and .NET's `List.Sort` (introsort)
* the parser runtime and error reporting

The XLParser v139 grammar is written on top of that port. The .NET regular expressions are
reproduced with a small backtracking engine that has .NET's leftmost-first semantics and
case-insensitive behaviour.

## Building

```sh
# CMake
cmake -S . -B build && cmake --build build && (cd build && ctest)

# or plain make
make && make test
```

Only `include/xlparser_clone.h` and the `src/` folder are needed to embed the library in another
project. The `.inc` files in `src/` are generated data, included by `src/xlparser_clone.cpp`.

## Verification

| Check | Result |
|---|---|
| `tests/data/results.txt` (1464 cases) | **1464 / 1464 identical.** 4 of the reference outputs were truncated in the source data, at an Excel cell's length limit, and are compared on the available prefix. |
| Differential fuzzing against the *real* C# code (Irony 1.2.0 + XLParser v139 sources compiled with Mono; see `tools/`) | **> 500,000** generated, mutated and random formulas, all identical. That includes 11.7k valid formulas built from the test inputs with the CSV quote-doubling removed, plus sheet, table and file-qualified variants. The only differences ever observed were with U+212A and U+0130 (see below). |
| AddressSanitizer + UndefinedBehaviorSanitizer, ThreadSanitizer (8 concurrent threads) | clean |
| Compilers | GCC and Clang with `-Wall -Wextra -pedantic`: no warnings |
| Performance | ~3 ms one-time grammar/LALR construction, then ~25 µs per typical formula |

To re-run the differential test, run `tools/reference_harness/build_and_run.sh`, which needs Mono
and git. Then:

```sh
python3 tools/fuzz/gen_fuzz.py 40000 1
batch inputs.b64 cpp_out.b64
mono xlref.exe inputs.b64 ref_out.b64
python3 tools/fuzz/compare.py
```

## Deviations from the old library

**On the provided test data: none.** Every one of the 1464 outputs is reproduced byte for byte, so
none of them needed to be classified as a "bug".

The library is deliberately bug-compatible, so the old behaviour is kept even where it is arguably
wrong. Examples, all confirmed against the real C# code:

| Input | Old (and this library's) result |
|---|---|
| `A1B` | cell `A1` *intersected* with name `B`. The NamedRangeCombination regex needs ≥ 2 trailing characters in this version. |
| `ABC:DEF` | `VRangeToken["ABC:DE"]` ∩ `NameToken["F"]`. This comes from regex backtracking in the column pattern. |
| `BETAXDIST(1)` | accepted as a built-in function. The function names are joined into a regex unescaped, so `.` matches any character. |
| `XLOOKUP(…)`, `LET(…)`, `LAMBDA(…)` | UDF calls, because the v139 function list predates them |
| `.5`, `1e999` | parse errors (`1e999`: "Invalid number." — .NET Framework's `double.TryParse` fails on overflow) |
| `' S'!A1` | quoted sheet token text keeps the leading space (XLParser's `FixQuotedSheetNodeForWhitespace`) |

These can be turned into deliberate fixes later if wanted; each would be a small, local change.

The remaining edge cases sit outside the test data:

* **Case folding of U+212A (Kelvin sign) and U+0130 (İ).** The library follows .NET Framework on
  Windows, where these lowercase to `k` and `i`, so `E` + U+212A + `1` is a cell. Mono, used for the
  differential testing, doesn't fold them; those cases were the only fuzzing differences.
* **Unicode data.** Character categories come from Unicode 15.1, while .NET Framework 4.7.2 used an
  older Unicode version. Letters assigned since then may be classified differently.
* **Invalid UTF-8 input.** Malformed bytes are replaced with U+FFFD (maximal-subpart rule) before
  parsing, as a .NET string decoder would do.
* **Line endings.** The JSON uses `\n` line breaks, like the provided reference data.

## License

XLParserClone is licensed under the **Mozilla Public License 2.0** (`LICENSE`), because it contains
material derived from XLParser, which is MPL 2.0. MPL 2.0 applies file by file: you can use the
library in software under any license, but if you distribute it (as source or compiled), the source
of these files, including your changes to them, must stay available under the MPL.

The library also contains translations of MIT-licensed code (Irony 1.2.0 and Microsoft's .NET
Framework reference source) and data derived from the Unicode Character Database. Their notices are
embedded in `src/xlparser_clone.cpp` and collected in `THIRD_PARTY_NOTICES.md`; keep both with any
copy you distribute.

## Layout

```
LICENSE                         Mozilla Public License 2.0
THIRD_PARTY_NOTICES.md          XLParser, Irony, Microsoft reference source and Unicode notices
include/xlparser_clone.h        public API (one function)
src/xlparser_clone.cpp          Irony port + XLParser v139 grammar + JSON output
src/unicode_tables.inc          generated: Unicode general categories / lowercase map (tools/gen_unicode.py)
src/excel_functions.inc         generated: v139 built-in function list (tools/gen_function_list.py)
tests/run_tests.cpp             runs tests/data/results.txt (base64 handling lives only here)
tests/thread_test.cpp           concurrency check
tools/reference_harness/        C# oracle: real Irony 1.2.0 + XLParser v139, replicating Parse.cs
tools/fuzz/                     corpus generator, batch driver and comparison script
```
