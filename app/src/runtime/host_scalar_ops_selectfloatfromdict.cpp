// SelectFloatFromDict — host-scalar op: Dict<float> input + String "Select" key -> Float (dict-currency
// seam). TiXL authority: external/tixl/Operators/Lib/numbers/data/utils/SelectFloatFromDict.cs:18-24.
//
//   SelectFloatFromDict.cs Update():
//     _dict = DictionaryInput.GetValue(context);
//     _selectCommand = Select.GetValue(context);
//     if (_dict != null)
//         _dict.TryGetValue(_selectCommand, out Result.Value);   // miss leaves Result at its prior/0
//
//   Result default = 0f (SelectFloatFromDict.cs:8 `new(0f)`). So: dict null OR key missing -> 0.
//
// Ports: Result (Float out, port 0) | DictionaryInput (Dict in, port 1) | Select (String key param, port 2).
// The host-scalar cook driver gathers the Dict wire into inputDicts[0] and the Select param into
// strParams["Select"]; this leaf reads them + writes the looked-up float into *output (component 0).
#include "runtime/dict_op_registry.h"          // SwFloatDict
#include "runtime/graph.h"                       // NodeSpec, PortSpec, Widget
#include "runtime/host_scalar_op_registry.h"    // HostScalarOp / HostScalarCookCtx / hostScalarInjectBug

#include <string>

namespace sw {

int runSelectFromDictSelfTest(bool injectBug);  // golden lives in select_from_dict_golden.cpp

namespace {

// SelectFloatFromDict.cs:22-23 — TryGetValue(select, out result); miss / null dict -> 0 (Result default).
void cookSelectFloatFromDict(HostScalarCookCtx& c) {
  if (!c.output) return;
  c.components = 1;
  float result = 0.0f;  // SelectFloatFromDict.cs:8 Result default 0f (null dict / missing key)
  const std::string key = hostScalarStrParam(c.strParams, "Select", "");
  const SwFloatDict* dict = (c.inputDicts && !c.inputDicts->empty()) ? (*c.inputDicts)[0] : nullptr;
  if (dict) dict->tryGet(key, result);  // TryGetValue: writes result only on hit; miss keeps 0
  // Test-only: corrupt the REAL looked-up scalar on the actual cook path so the golden's RED bites here
  // (a wrong downstream evalFloat value), NOT by flipping the expected value. Off in production.
  if (hostScalarInjectBug()) result = -999.0f;
  *c.output = result;
}

}  // namespace

// Self-registration (host-scalar rail: consumes Dict, outputs a Float scalar via the outCache bridge).
//   Result (Float out) FIRST (host-scalar output-port-first layout) | DictionaryInput (Dict) | Select (String).
static const HostScalarOp _reg_selectfloatfromdict{
    {"SelectFloatFromDict", "SelectFloatFromDict",
     {{"Result", "Result", "Float", false},
      {"DictionaryInput", "DictionaryInput", "Dict", true},
      {"Select", "Select", "String", true}},
     /*evaluate=*/nullptr},  // host-scalar: no pure evaluate; the cook driver + outCache bridge drive it
    cookSelectFloatFromDict};

}  // namespace sw
