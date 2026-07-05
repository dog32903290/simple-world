// SelectBoolFromFloatDict — host-scalar op: Dict<float> input + String "Select" key -> Bool (dissolved to
// Float 0/1) (dict-currency seam). TiXL authority:
// external/tixl/Operators/Lib/numbers/data/utils/SelectBoolFromFloatDict.cs:17-31.
//
//   SelectBoolFromFloatDict.cs Update():
//     _dict = DictionaryInput.GetValue(context);
//     _selectCommand = Select.GetValue(context);
//     if (_dict != null && _dict.TryGetValue(_selectCommand, out var floatValue))
//         Result.Value = floatValue > 0.5f;                  // hit -> (v > 0.5)
//     // else: warning; Result KEEPS its prior value. Result default = false (line 9 `new(false)`).
//
//   sw dissolves bool->Float (fork-int-bool-dissolve-to-float, Cut32): true->1.0, false->0.0. On a
//   null-dict / missing-key the value stays at the default false -> 0.0 (TiXL keeps Result; the first
//   cook's default is false, and sw's per-cook host-scalar has no prior frame retention → 0.0 faithful).
//
// Ports: Result (Float out, port 0) | DictionaryInput (Dict in, port 1) | Select (String key param, port 2).
#include "runtime/dict_op_registry.h"          // SwFloatDict
#include "runtime/graph.h"                       // NodeSpec, PortSpec, Widget
#include "runtime/host_scalar_op_registry.h"    // HostScalarOp / HostScalarCookCtx / hostScalarInjectBug

#include <string>

namespace sw {

int runSelectFromDictSelfTest(bool injectBug);  // shared golden

namespace {

// SelectBoolFromFloatDict.cs:22-23 — hit: Result = (v > 0.5f); miss/null: default false. bool->Float 0/1.
void cookSelectBoolFromFloatDict(HostScalarCookCtx& c) {
  if (!c.output) return;
  c.components = 1;
  bool result = false;  // SelectBoolFromFloatDict.cs:9 Result default false (null dict / missing key)
  const std::string key = hostScalarStrParam(c.strParams, "Select", "");
  const SwFloatDict* dict = (c.inputDicts && !c.inputDicts->empty()) ? (*c.inputDicts)[0] : nullptr;
  float v = 0.0f;
  if (dict && dict->tryGet(key, v)) result = v > 0.5f;  // SelectBoolFromFloatDict.cs:22-23
  float out = result ? 1.0f : 0.0f;                     // fork-int-bool-dissolve-to-float
  // Test-only: corrupt the REAL result on the actual cook path so the golden's RED bites here.
  if (hostScalarInjectBug()) out = -999.0f;
  *c.output = out;
}

}  // namespace

static const HostScalarOp _reg_selectboolfromfloatdict{
    {"SelectBoolFromFloatDict", "SelectBoolFromFloatDict",
     {{"Result", "Result", "Float", false},
      {"DictionaryInput", "DictionaryInput", "Dict", true},
      {"Select", "Select", "String", true}},
     /*evaluate=*/nullptr},
    cookSelectBoolFromFloatDict};

}  // namespace sw
