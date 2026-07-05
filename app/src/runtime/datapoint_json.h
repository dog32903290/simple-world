// runtime/datapoint_json — minimal JSON reader shared by the point/io data ops
// (DataPointImportExport / DataPointConverter). Pure CPU, zero platform deps (runtime leaf).
//
// SCOPE (deliberately small, rule 7 one-table-one-builder does not apply — this is a parser):
// exactly the JSON shapes the two TiXL ops consume — a top-level ARRAY of OBJECTS whose values are
// numbers, strings, or ONE level of nested objects (Position/Orientation/Scale sub-objects with
// numeric/string leaves). It parses general JSON structure (any nesting), but consumers only walk
// the shapes above.
//
// TiXL authority for the VALUE semantics (both ops share them):
//   DataPointImportExport.cs:311-346 GetFloat/GetNestedFloat/ParseFloatString and
//   DataPointConverter.cs:330-358 GetFloatFromJson/ParseFloatString:
//   - a JSON number → its float value;
//   - a JSON string → ParseFloatString: strip every 'm' and '°', trim, invariant float parse;
//   - missing key / null / unparsable → the caller's default.
//   Dictionary key lookups in the .cs are CASE-SENSITIVE (Dictionary<string,JsonElement> default
//   comparer; PropertyNameCaseInsensitive only affects POCO property binding, not dictionary keys),
//   so jsonFind here is exact-match on purpose.
#pragma once

#include <string>
#include <vector>

namespace sw {

// One parsed JSON value. Small tagged struct (no variant — keeps the header trivial).
struct JsonVal {
  enum Kind { Null, Bool, Num, Str, Arr, Obj } kind = Null;
  double num = 0.0;
  bool boolean = false;
  std::string str;                                    // Str payload
  std::vector<JsonVal> arr;                           // Arr payload
  std::vector<std::pair<std::string, JsonVal>> obj;   // Obj payload (insertion order)
};

// Parse `text` into `out`. Returns false on malformed JSON (consumers then treat the whole file as
// failed — mirroring the .cs try/catch around Deserialize, which aborts the import on any parse error).
bool jsonParse(const std::string& text, JsonVal& out);

// Exact-match key lookup in an Obj (nullptr when missing / v is not an Obj). Case-SENSITIVE (see header).
const JsonVal* jsonFind(const JsonVal& v, const std::string& key);

// ParseFloatString (DataPointImportExport.cs:339-346): strip every 'm' and the degree sign, trim,
// invariant float parse. Returns `def` when the cleaned string is empty or unparsable.
float parseFloatString(const std::string& raw, float def);

// GetFloat semantics on a JsonVal leaf: Num → (float)num; Str → parseFloatString; anything else → def.
float jsonToFloat(const JsonVal* v, float def);

}  // namespace sw
