// SelectVec2FromDict — host-scalar op: Dict<float> input + String "SelectX" key -> Vector2 (dict-currency
// seam). TiXL authority: external/tixl/Operators/Lib/numbers/data/utils/SelectVec2FromDict.cs:16-85.
//
//   SelectVec2FromDict.cs Update():
//     _dict = DictionaryInput.GetValue(context);
//     _selectCommand = SelectX.GetValue(context);
//     if (_dict == null) return;
//     if (_yKey == null) _yKey = FindKeyForY(_selectCommand);
//     if (_yKey != null && _dict.TryGetValue(_selectCommand, out var x) && _dict.TryGetValue(_yKey, out var y))
//         Result.Value = new Vector2(x, y);
//
//   FindKeyForY(xKey) (SelectVec2FromDict.cs:64-85): iterate _dict.Keys.OrderBy(x => x) (ORDINAL default
//   string sort in .NET for OrderBy over strings); on reaching xKey, return the NEXT key. So Y = value of
//   the key that ORDINALLY FOLLOWS selectX. Both keys must be present -> Result=(x,y); else Result KEEPS its
//   prior value. Result default = (0,0) (SelectVec2FromDict.cs:8 `new()`).
//
// NOTE (impl-independence): sw does NOT cache _yKey across cooks (per-cook host-scalar). We recompute
// FindKeyForY every cook — byte-identical result to TiXL's cache-after-first for a stable dict (the cache
// is a perf memo, not a semantic; a changed dict in TiXL keeps a STALE _yKey until _yKey==null, an edge we
// don't reproduce because sw has no cross-cook _yKey — recomputing is the faithful steady-state value).
//
// Ports: Result.x/.y (Float out, ports 0-1) | DictionaryInput (Dict in, port 2) | SelectX (String, port 3).
#include "runtime/dict_op_registry.h"          // SwFloatDict
#include "runtime/graph.h"                       // NodeSpec, PortSpec, Widget
#include "runtime/host_scalar_op_registry.h"    // HostScalarOp / HostScalarCookCtx / hostScalarInjectBug

#include <algorithm>
#include <string>
#include <vector>

namespace sw {

int runSelectFromDictSelfTest(bool injectBug);  // shared golden

namespace {

// FindKeyForY (SelectVec2FromDict.cs:64-85): the key ORDINALLY following xKey in the sorted key set. "" if
// xKey is absent or is the last key. (OrderBy over strings in .NET = ordinal; std::string < is byte/ordinal.)
std::string findKeyForY(const SwFloatDict& dict, const std::string& xKey) {
  std::vector<std::string> keys;
  keys.reserve(dict.entries.size());
  for (const auto& kv : dict.entries) keys.push_back(kv.first);
  std::sort(keys.begin(), keys.end());  // ordinal (byte) sort == .NET OrderBy(x=>x) over strings
  bool justFoundX = false;
  for (const std::string& k : keys) {
    if (k == xKey) { justFoundX = true; continue; }
    if (justFoundX) return k;  // the NEXT key after xKey
  }
  return "";
}

void cookSelectVec2FromDict(HostScalarCookCtx& c) {
  if (!c.output) return;
  c.components = 2;
  float rx = 0.0f, ry = 0.0f;  // SelectVec2FromDict.cs:8 Result default (0,0)
  const std::string keyX = hostScalarStrParam(c.strParams, "SelectX", "");
  const SwFloatDict* dict = (c.inputDicts && !c.inputDicts->empty()) ? (*c.inputDicts)[0] : nullptr;
  if (dict) {
    const std::string keyY = findKeyForY(*dict, keyX);
    float x = 0.0f, y = 0.0f;
    // SelectVec2FromDict.cs:27 — need yKey non-empty AND both keys present, else Result keeps default.
    if (!keyY.empty() && dict->tryGet(keyX, x) && dict->tryGet(keyY, y)) { rx = x; ry = y; }
  }
  // Test-only: corrupt the REAL result on the actual cook path so the golden's RED bites here.
  if (hostScalarInjectBug()) { rx = -999.0f; ry = -999.0f; }
  *c.output = rx;
  if (c.outY) *c.outY = ry;
}

}  // namespace

static const HostScalarOp _reg_selectvec2fromdict{
    {"SelectVec2FromDict", "SelectVec2FromDict",
     {{"Result.x", "Result.x", "Float", false},
      {"Result.y", "Result.y", "Float", false},
      {"DictionaryInput", "DictionaryInput", "Dict", true},
      {"SelectX", "SelectX", "String", true}},
     /*evaluate=*/nullptr},
    cookSelectVec2FromDict};

}  // namespace sw
