// GetAttributeFromJsonString string op (MULTI-OUTPUT seam consumer — Sub-seam B). Parses a JSON STRING
// INPUT (array-of-flat-objects) into a table and reads one cell. Despite TiXL's io/json/ path this op does
// NO file/device IO — the JSON is a STRING input; it rides the EXISTING flat String cook flow. The only new
// bit is a small hand-rolled JSON parser (pure CPU, runtime zone — NO platform/native dep).
//
// TiXL authority: Operators/Lib/io/json/GetAttributeFromJsonString.cs (ported below).
//
//   GetAttributeFromJsonString.cs Update():
//     var columnName = ColumnName.GetValue(context);
//     var rowIndex   = RowIndex.GetValue(context);
//     var json       = JsonString.GetValue(context);
//     try {
//       var dt = JsonConvert.DeserializeObject<DataTable>(json);   // JSON = ARRAY of FLAT OBJECTS:
//                                                                  //   keys = column names, elements = rows
//       var columns = new List<string>();
//       RowCount.Value = dt.Rows.Count;
//       var matchingColumnIndex = -1; var index = 0;
//       foreach (DataColumn c in dt.Columns) {
//         if (c.ColumnName.Equals(columnName, InvariantCultureIgnoreCase)) matchingColumnIndex = index;
//         index++; columns.Add(c.ColumnName);
//       }
//       if (matchingColumnIndex == -1) { /* "Can't find column" — Result LEFT UNCHANGED */ }
//       else if (dt.Rows.Count < rowIndex || rowIndex < 0) { /* "Row index exceeds" — Result UNCHANGED */ }
//       else { Result.Value = dt.Rows[rowIndex][matchingColumnIndex].ToString(); }
//       Columns.Value = columns;     // ALWAYS emitted (even when column/row missing)
//     } catch (Exception) { Log.Warning(...); /* dt null/parse-fail → NOTHING written */ }
//
//   Ports: JsonString=InputSlot<string>; ColumnName=InputSlot<string>; RowIndex=InputSlot<int>.
//          Outputs: Result=Slot<string>; Columns=Slot<List<string>>; RowCount=Slot<int>.
//
// EVAL-SIDE LAYOUT: a String PRODUCER + a List<string> output + an Int scalar (multi-output). The TWO String
// inputs (JsonString, ColumnName) ride inputStrings[0]/[1] in spec port order (wired upstream string, or the
// strDef const "" when unwired). RowIndex is an Int param dissolved to Float (the value spine;
// fork-int-bool-dissolve-to-float). Outputs fan: Result → *output (port 0); Columns → extraStrOutputs[1]
// (joined string, see fork below); RowCount → scalarOutputs[2] (Int dissolved to Float).
//
// FORKS (named):
//   - fork-jsonattr-minimal-parser: TiXL uses Newtonsoft JsonConvert.DeserializeObject<DataTable> (a full
//     JSON deserializer). We hand-roll a MINIMAL parser for the ONE shape this op consumes: a top-level
//     ARRAY of FLAT OBJECTS  `[ {"k1":"v1","k2": 3}, {...} ]`. Supported value kinds: string, number,
//     bool, null. NESTED objects / nested arrays as values are NOT supported (a faithful DataTable would
//     stringify them as System.Object) — named NOT-PORTED; the columns are the UNION of keys discovered
//     in row order (DataTable adds a column the first time a key appears), rows are the array elements.
//     A parse failure (malformed JSON / not an array-of-objects) → like the .cs catch: NOTHING is written
//     (Result/Columns/RowCount all stay at their default empties), faithful to the swallowed exception.
//   - fork-jsonattr-number-stringification: a DataTable cell `.ToString()` on the deserialized value. We
//     stringify a JSON number by echoing its SOURCE TOKEN verbatim (e.g. "255", "3.14", "-0", "1e3") — we
//     do NOT renormalize through a double (so no "255" → "255.0" drift). bool → "true"/"false" (lowercase,
//     the JSON literal). null → "" (an empty cell). A string value → its decoded contents (escapes handled).
//   - fork-jsonattr-columns-joined-string: TiXL's Columns output is a List<string>. The sw String rail's
//     MULTI-OUTPUT fan (StringCookCtx) supports EXTRA single-string outputs (extraStrOutputs) + scalar
//     outputs (scalarOutputs) — it does NOT carry a List<string> EXTRA output (the dedicated StringList
//     PRODUCER rides its own StringListCookCtx, a different cook flow that cannot also be a String producer
//     on the same node). So we emit Columns as a single String = the column names joined by ", " (e.g.
//     "name, r"). This DIVERGES from TiXL's List<string> SHAPE (a downstream StringList consumer cannot
//     wire to it) — named. The column-name CONTENT and ORDER are faithful; only the container type differs.
//   - fork-jsonattr-rowindex-off-by-one-faithful: the .cs range guard is `dt.Rows.Count < rowIndex` (a
//     strict `<`, NOT `<=`). So rowIndex == Rows.Count PASSES the guard, then dt.Rows[rowIndex] throws
//     IndexOutOfRange → caught → Result left unchanged. We reproduce the OBSERVABLE result faithfully: an
//     out-of-range row (rowIndex >= rowCount OR rowIndex < 0) leaves Result EMPTY (we do not write it). The
//     boundary rowIndex == rowCount is therefore "out of range" here exactly as it is unreachable in TiXL.
//   - fork-jsonattr-column-case-insensitive: column match is InvariantCultureIgnoreCase (.cs:44). We port a
//     simple ASCII case-fold compare (lowercasing both sides byte-wise). A non-ASCII culture-specific fold
//     is NOT-PORTED (the JSON keys this op consumes are ASCII identifiers).
//   - fork-jsonattr-flat-only-no-resident: this op rides ONLY the flat String cook flow (PointGraph::cook /
//     cookStringNode), like every multi-output String op — it does NOT enter resident_eval_graph. The golden
//     asserts the FLAT leg; a resident-scope wiring is out of current scope (same as the sibling rail).
//   - fork-int-bool-dissolve-to-float / fork-string-host-not-gpu: as every ported string op.
#include <cctype>
#include <cstddef>
#include <string>
#include <vector>

#include "runtime/graph.h"               // NodeSpec, PortSpec, Widget
#include "runtime/string_op_registry.h"  // StringOp / StringCookCtx / stringInjectBug / stringFloatParam

namespace sw {

namespace {

// ── Minimal JSON parser (fork-jsonattr-minimal-parser) ──────────────────────────────────────────────
// Pure C++ (no platform/native dep). Parses ONLY the shape this op consumes: a top-level array of flat
// objects. Returns false on any malformed input (caller then writes NOTHING, faithful to the .cs catch).

struct Row {
  // (key, stringified-value) pairs in source order. Lookup is by key (case-insensitive).
  std::vector<std::pair<std::string, std::string>> cells;
};

// ASCII lowercase one char (fork-jsonattr-column-case-insensitive).
char asciiLower(char ch) {
  return (ch >= 'A' && ch <= 'Z') ? static_cast<char>(ch - 'A' + 'a') : ch;
}
bool asciiEqualsIgnoreCase(const std::string& a, const std::string& b) {
  if (a.size() != b.size()) return false;
  for (std::size_t i = 0; i < a.size(); ++i)
    if (asciiLower(a[i]) != asciiLower(b[i])) return false;
  return true;
}

// A tiny cursor-based recursive parser. `i` is the current read position into `s`.
struct JsonParser {
  const std::string& s;
  std::size_t i = 0;
  explicit JsonParser(const std::string& src) : s(src) {}

  void skipWs() {
    while (i < s.size() &&
           (s[i] == ' ' || s[i] == '\t' || s[i] == '\n' || s[i] == '\r'))
      ++i;
  }
  bool atEnd() { skipWs(); return i >= s.size(); }
  char peek() { return i < s.size() ? s[i] : '\0'; }

  // Parse a JSON string literal (i must point at the opening '"'). Decodes the common escapes. Writes the
  // decoded contents into `out`; returns false on a malformed/unterminated string.
  bool parseString(std::string& out) {
    if (peek() != '"') return false;
    ++i;  // consume opening quote
    out.clear();
    while (i < s.size()) {
      char ch = s[i++];
      if (ch == '"') return true;  // closing quote
      if (ch == '\\') {
        if (i >= s.size()) return false;
        char e = s[i++];
        switch (e) {
          case '"':  out.push_back('"');  break;
          case '\\': out.push_back('\\'); break;
          case '/':  out.push_back('/');  break;
          case 'b':  out.push_back('\b'); break;
          case 'f':  out.push_back('\f'); break;
          case 'n':  out.push_back('\n'); break;
          case 'r':  out.push_back('\r'); break;
          case 't':  out.push_back('\t'); break;
          case 'u': {  // \uXXXX → ASCII passthrough for the BMP-basic case (named-minimal: we emit the raw
                       // codepoint only when it is < 128; otherwise the escape is dropped — full UTF-8
                       // synthesis is NOT-PORTED, the keys/values this op consumes are ASCII).
            if (i + 4 > s.size()) return false;
            int cp = 0;
            for (int k = 0; k < 4; ++k) {
              char h = s[i++];
              int v;
              if (h >= '0' && h <= '9') v = h - '0';
              else if (h >= 'a' && h <= 'f') v = h - 'a' + 10;
              else if (h >= 'A' && h <= 'F') v = h - 'A' + 10;
              else return false;
              cp = (cp << 4) | v;
            }
            if (cp < 128) out.push_back(static_cast<char>(cp));
            break;
          }
          default: return false;  // unknown escape → malformed
        }
      } else {
        out.push_back(ch);
      }
    }
    return false;  // unterminated
  }

  // Parse a JSON value as its STRINGIFIED cell content (fork-jsonattr-number-stringification). On entry the
  // cursor is at the start of the value (whitespace already skipped). NESTED object/array values are
  // rejected (return false) — fork-jsonattr-minimal-parser (flat objects only).
  bool parseValueAsCell(std::string& out) {
    skipWs();
    char c = peek();
    if (c == '"') return parseString(out);
    if (c == '{' || c == '[') return false;  // nested object/array value → NOT-PORTED (flat only)
    if (c == 't') {  // true
      if (s.compare(i, 4, "true") == 0) { i += 4; out = "true"; return true; }
      return false;
    }
    if (c == 'f') {  // false
      if (s.compare(i, 5, "false") == 0) { i += 5; out = "false"; return true; }
      return false;
    }
    if (c == 'n') {  // null → empty cell
      if (s.compare(i, 4, "null") == 0) { i += 4; out = ""; return true; }
      return false;
    }
    // number: echo the SOURCE TOKEN verbatim (no double round-trip).
    if (c == '-' || c == '+' || (c >= '0' && c <= '9')) {
      std::size_t start = i;
      while (i < s.size()) {
        char n = s[i];
        if ((n >= '0' && n <= '9') || n == '+' || n == '-' || n == '.' || n == 'e' || n == 'E')
          ++i;
        else
          break;
      }
      if (i == start) return false;
      out = s.substr(start, i - start);
      return true;
    }
    return false;  // unrecognized token
  }

  // Parse one flat object `{ "k": value, ... }` into `row`. Cursor must be at '{'.
  bool parseObject(Row& row) {
    if (peek() != '{') return false;
    ++i;  // consume '{'
    skipWs();
    if (peek() == '}') { ++i; return true; }  // empty object
    while (true) {
      skipWs();
      std::string key;
      if (!parseString(key)) return false;
      skipWs();
      if (peek() != ':') return false;
      ++i;  // consume ':'
      std::string val;
      if (!parseValueAsCell(val)) return false;
      row.cells.emplace_back(std::move(key), std::move(val));
      skipWs();
      char d = peek();
      if (d == ',') { ++i; continue; }
      if (d == '}') { ++i; return true; }
      return false;  // malformed
    }
  }

  // Parse the top-level ARRAY of flat objects into `rows`. Returns false on any malformation.
  bool parseTopArray(std::vector<Row>& rows) {
    skipWs();
    if (peek() != '[') return false;
    ++i;  // consume '['
    skipWs();
    if (peek() == ']') { ++i; return true; }  // empty array → 0 rows
    while (true) {
      skipWs();
      Row row;
      if (!parseObject(row)) return false;
      rows.push_back(std::move(row));
      skipWs();
      char d = peek();
      if (d == ',') { ++i; continue; }
      if (d == ']') { ++i; return true; }
      return false;  // malformed
    }
  }
};

// Parse `json` into rows. Returns false on parse failure (→ caller writes nothing, faithful to .cs catch).
bool parseJsonTable(const std::string& json, std::vector<Row>& rows) {
  JsonParser p(json);
  if (!p.parseTopArray(rows)) return false;
  // Trailing garbage after the closing ']' → malformed (JsonConvert would also reject it).
  if (!p.atEnd()) return false;
  return true;
}

// Build the ordered UNION of column names across all rows (DataTable adds a column the first time a key is
// seen, in row/key encounter order; a key repeated in a later row does not duplicate the column).
std::vector<std::string> collectColumns(const std::vector<Row>& rows) {
  std::vector<std::string> cols;
  for (const Row& r : rows) {
    for (const auto& kv : r.cells) {
      bool seen = false;
      for (const std::string& c : cols)
        if (c == kv.first) { seen = true; break; }
      if (!seen) cols.push_back(kv.first);
    }
  }
  return cols;
}

// Cell lookup: the value for `columnName` (case-insensitive) in `row`; "" when the row lacks that key
// (a DataTable cell that was never set is DBNull → .ToString() == "", faithful).
std::string cellValue(const Row& row, const std::string& columnName) {
  for (const auto& kv : row.cells)
    if (asciiEqualsIgnoreCase(kv.first, columnName)) return kv.second;
  return "";
}

void cookGetAttributeFromJson(StringCookCtx& c) {
  if (!c.output) return;

  // Two String inputs in spec port order: [0]=JsonString, [1]=ColumnName (wired upstream, or strDef "").
  const std::string json =
      (c.inputStrings && c.inputStrings->size() > 0) ? (*c.inputStrings)[0] : std::string{};
  const std::string columnName =
      (c.inputStrings && c.inputStrings->size() > 1) ? (*c.inputStrings)[1] : std::string{};
  const int rowIndex = static_cast<int>(stringFloatParam(c.params, "RowIndex", 0.0f));

  std::vector<Row> rows;
  if (!parseJsonTable(json, rows)) {
    // .cs catch: dt null / parse fail → NOTHING written. Leave Result/Columns/RowCount at their defaults
    // (the driver-owned buffers are empty / unset). We explicitly write nothing here (faithful no-op).
    return;
  }

  const std::vector<std::string> columns = collectColumns(rows);
  const int rowCount = static_cast<int>(rows.size());

  // Find the matching column (case-insensitive). matchingColumnIndex == -1 when absent.
  int matchingColumnIndex = -1;
  for (std::size_t idx = 0; idx < columns.size(); ++idx)
    if (asciiEqualsIgnoreCase(columns[idx], columnName)) {
      matchingColumnIndex = static_cast<int>(idx);
      // .cs keeps scanning (last match wins) — but column names in a DataTable are unique, so the first
      // (and only) match is final. We break for clarity; behavior is identical to the .cs loop.
      break;
    }

  // Result: written ONLY when the column exists AND the row is in range (fork-jsonattr-rowindex-off-by-one-
  // faithful: rowIndex >= rowCount OR rowIndex < 0 → out of range → Result left empty / unwritten).
  std::string result;  // default "" (the unwritten / out-of-range / missing-column case)
  if (matchingColumnIndex != -1 && rowIndex >= 0 && rowIndex < rowCount) {
    result = cellValue(rows[static_cast<std::size_t>(rowIndex)], columns[static_cast<std::size_t>(matchingColumnIndex)]);
  }
  *c.output = result;  // port 0 (Result → MAIN String output)

  // Columns (port 1) → extraStrOutputs[1] as a ", "-joined string (fork-jsonattr-columns-joined-string).
  if (c.extraStrOutputs) {
    std::string joined;
    for (std::size_t idx = 0; idx < columns.size(); ++idx) {
      if (idx > 0) joined += ", ";
      joined += columns[idx];
    }
    (*c.extraStrOutputs)[1] = joined;
  }

  // RowCount (port 2) → scalarOutputs[2] (Int dissolved to Float; fork-int-bool-dissolve-to-float).
  if (c.scalarOutputs) (*c.scalarOutputs)[2] = static_cast<float>(rowCount);

  // Test-only: corrupt the REAL cook so the golden's RED bites on the actual parse/index path. Knock the
  // ROW INDEX off by one (re-read the cell at rowIndex+1) — a real perturbation of the lookup, NOT a
  // want-flip. Also drop the last char of the joined Columns + knock RowCount off, so every output goes RED.
  if (stringInjectBug()) {
    int badRow = rowIndex + 1;  // off-by-one row index → wrong cell (or empty if now out of range)
    std::string badResult;
    if (matchingColumnIndex != -1 && badRow >= 0 && badRow < rowCount)
      badResult = cellValue(rows[static_cast<std::size_t>(badRow)],
                            columns[static_cast<std::size_t>(matchingColumnIndex)]);
    *c.output = badResult;
    if (c.extraStrOutputs) {
      auto& cols = (*c.extraStrOutputs)[1];
      if (!cols.empty()) cols.pop_back();
    }
    if (c.scalarOutputs) (*c.scalarOutputs)[2] = -999.0f;  // sentinel ≠ any valid count
  }
}

}  // namespace

// Self-registration. File-scope static StringOp — independent leaf .cpp (no shared edit point in code; the
// CMake explicit string-op list still needs the row — flagged to the orchestrator).
//   Output ports (the MULTI-OUTPUT layout — port index is the extraStrOutputs/scalarOutputs key):
//     [0] "Result"   = String output  (MAIN → ctx.output → extStrOut[0] / stringBuf[id])
//     [1] "Columns"  = String output  (extraStrOutputs[1]; ", "-joined names, fork-jsonattr-columns-joined)
//     [2] "RowCount" = Float output   (Int dissolved → scalarOutputs[2] → extOut[2] / outCache[2])
//   Input ports (gathered after the outputs):
//     [3] "JsonString" = String input (wire-OR-const; strDef "") → inputStrings[0]
//     [4] "ColumnName" = String input (wire-OR-const; strDef "") → inputStrings[1]
//     [5] "RowIndex"   = Float/Int — rides params
//   PortSpec positional: {id, name, dataType, isInput, def, minV, maxV, widget, labels, pinless,
//                         vecArity, multiInput, strDef}.
static const StringOp _reg_getattributefromjson{
    {"GetAttributeFromJsonString", "GetAttributeFromJsonString",
     {{"Result",   "Result",   "String", false},
      {"Columns",  "Columns",  "String", false},
      {"RowCount", "RowCount", "Float",  false},
      {"JsonString", "JsonString", "String", true, 0.0f, 0.0f, 1.0f, Widget::Slider, {}, false, 1,
       false, ""},
      {"ColumnName", "ColumnName", "String", true, 0.0f, 0.0f, 1.0f, Widget::Slider, {}, false, 1,
       false, ""},
      {"RowIndex", "RowIndex", "Float", true, 0.0f, -1000000.0f, 1000000.0f, Widget::Slider}},
     /*evaluate=*/nullptr},  // String output cannot ride NodeSpec::evaluate (returns ONE float)
    cookGetAttributeFromJson};

}  // namespace sw
