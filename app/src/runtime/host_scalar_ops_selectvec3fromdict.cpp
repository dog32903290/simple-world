// SelectVec3FromDict — host-scalar op: Dict<float> input + String "SelectX" key -> Vector3 (dict-currency
// seam). TiXL authority: external/tixl/Operators/Lib/numbers/data/utils/SelectVec3FromDict.cs:20-58 +
// external/tixl/Core/Utils/StringUtils.cs:203-256 (TryUpdateVectorKeysRelatedToX).
//
//   SelectVec3FromDict.cs Update():
//     _dict = DictionaryInput.GetValue(context);
//     if (_dict == null) return;
//     _keysValid = StringUtils.TryUpdateVectorKeysRelatedToX(_dict, SelectX, ref _vectorKeys, 3);
//     if (!_keysValid) return;               // "Can't find vector3 values in OSC dict"
//     if (_dict.TryGetValue(_vectorKeys[0], out var x) && [1]->y && [2]->z)
//         Result.Value = new Vector3(x, y, z);
//   Result default = (0,0,0) (SelectVec3FromDict.cs:13 `new()`).
//
//   TryUpdateVectorKeysRelatedToX(dict, xKey, keys, count=3) (StringUtils.cs:203-256):
//     if dict==null || dict.Count < 3 || xKey empty -> false.
//     Sort dict.Keys ORDINALLY (Array.Sort + StringComparer.Ordinal). Walk sorted keys; once xKey is
//     reached (justFoundX), collect keys while (--count >= 0) AND CountDifferentChars(key, xKey) <= 1
//     (same length, at most one differing char — e.g. "posX","posY","posZ"), else BREAK. Return count < 0
//     (i.e. exactly 3 collected). CountDifferentChars: different length -> int.MaxValue (never matches).
//     NOTE: xKey itself is checked by CountDifferentChars(xKey, xKey)=0<=1, so it is the FIRST collected.
//
// Ports: Result.x/.y/.z (Float out, 0-2) | DictionaryInput (Dict in, 3) | SelectX (String, 4).
#include "runtime/dict_op_registry.h"          // SwFloatDict
#include "runtime/graph.h"                       // NodeSpec, PortSpec, Widget
#include "runtime/host_scalar_op_registry.h"    // HostScalarOp / HostScalarCookCtx / hostScalarInjectBug

#include <algorithm>
#include <climits>
#include <string>
#include <vector>

namespace sw {

int runSelectFromDictSelfTest(bool injectBug);  // shared golden

namespace {

// StringUtils.cs:244-255 CountDifferentChars: MAX if lengths differ, else # of positions that differ.
int countDifferentChars(const std::string& a, const std::string& b) {
  if (a.size() != b.size()) return INT_MAX;
  int diff = 0;
  for (size_t i = 0; i < a.size(); ++i)
    if (a[i] != b[i]) ++diff;
  return diff;
}

// StringUtils.cs:203-256 TryUpdateVectorKeysRelatedToX for count=3. Fills `outKeys` with the 3 vector
// keys (xKey first) and returns true, or returns false (outKeys undefined) if it can't find exactly 3.
bool tryUpdateVectorKeys(const SwFloatDict& dict, const std::string& xKey,
                         std::vector<std::string>& outKeys, int count) {
  if ((int)dict.entries.size() < count || xKey.empty()) return false;  // StringUtils.cs:205
  outKeys.clear();
  std::vector<std::string> keys;
  keys.reserve(dict.entries.size());
  for (const auto& kv : dict.entries) keys.push_back(kv.first);
  std::sort(keys.begin(), keys.end());  // StringUtils.cs:219 Array.Sort ... StringComparer.Ordinal
  bool justFoundX = false;
  for (const std::string& key : keys) {                    // StringUtils.cs:221-239
    if (key == xKey) justFoundX = true;
    if (!justFoundX) continue;
    if (--count >= 0 && countDifferentChars(key, xKey) <= 1)
      outKeys.push_back(key);
    else
      break;
  }
  return count < 0;  // StringUtils.cs:242 — true iff exactly `count` keys collected
}

void cookSelectVec3FromDict(HostScalarCookCtx& c) {
  if (!c.output) return;
  c.components = 3;
  float rx = 0.0f, ry = 0.0f, rz = 0.0f;  // SelectVec3FromDict.cs:13 Result default (0,0,0)
  const std::string keyX = hostScalarStrParam(c.strParams, "SelectX", "");
  const SwFloatDict* dict = (c.inputDicts && !c.inputDicts->empty()) ? (*c.inputDicts)[0] : nullptr;
  if (dict) {
    std::vector<std::string> vk;
    if (tryUpdateVectorKeys(*dict, keyX, vk, 3) && vk.size() == 3) {  // SelectVec3FromDict.cs:33,44
      float x = 0.0f, y = 0.0f, z = 0.0f;
      if (dict->tryGet(vk[0], x) && dict->tryGet(vk[1], y) && dict->tryGet(vk[2], z)) {
        rx = x; ry = y; rz = z;  // SelectVec3FromDict.cs:52-57
      }
    }
  }
  // Test-only: corrupt the REAL result on the actual cook path so the golden's RED bites here.
  if (hostScalarInjectBug()) { rx = -999.0f; ry = -999.0f; rz = -999.0f; }
  *c.output = rx;
  if (c.outY) *c.outY = ry;
  if (c.outZ) *c.outZ = rz;
}

}  // namespace

static const HostScalarOp _reg_selectvec3fromdict{
    {"SelectVec3FromDict", "SelectVec3FromDict",
     {{"Result.x", "Result.x", "Float", false},
      {"Result.y", "Result.y", "Float", false},
      {"Result.z", "Result.z", "Float", false},
      {"DictionaryInput", "DictionaryInput", "Dict", true},
      {"SelectX", "SelectX", "String", true}},
     /*evaluate=*/nullptr},
    cookSelectVec3FromDict};

}  // namespace sw
