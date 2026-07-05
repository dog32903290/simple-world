// BuildFloatDict — the FIRST Dict<float> PRODUCER leaf (dict-currency seam). A minimal utility/test
// producer that pairs a comma-separated list of String keys with a MultiInput of Float values into a
// SwFloatDict (string->float), so the Select*FromDict consumers have a real Dict wire to gather. It is
// ALSO the reference producer the device-io family (OscInput.Contents / Gamepad.State / MidiClip.Values /
// PosiStageInput.TrackersAsDict / GetBeatTimingDetails.Values / ...) will copy: each is a
// `Slot<Dict<float>>` output (external/tixl/Operators/Lib/io/**) that builds a dict from device
// names+values — the same key/value pairing, just fed by a device instead of wires.
//
// KEY DESIGN — keys are a STRING CONSTANT, values are FLOAT WIRES (the resident-parity choice):
//   The resident flatten DROPS String Connection wires (String inputs become resolved-constant strInputs;
//   resident_eval_flatten String ports carry no driver) — the same wall that makes resident StringLength
//   un-gatherable (list_routing_golden's StringLength note). So a Dict producer whose KEYS came from a
//   String MULTIINPUT wire would be flat-only, breaking the resident leg. Instead keys ride ONE String
//   input "Keys" (comma-separated, wire-OR-const — resolves identically on BOTH legs), and values ride a
//   Float MultiInput "Values" (wire-driven, resolvable on both legs via evalFloat / evalResidentFloat).
//   This is faithful to how a device producer works too: channel NAMES are fixed strings, VALUES stream.
//
// TiXL has no literal "BuildFloatDict" (dicts are device-produced), so there is no .cs authority for the
// PAIRING; the semantics are the obvious zip (entry[i] = (key[i], value[i]) for i in [0, min(#k,#v))).
// What IS TiXL-authoritative is the CONTAINER (Dict<float> = Dictionary<string,float>, Core/DataTypes/
// Dict.cs) and the ORDER-PRESERVING requirement (SelectVec2/Vec3 iterate keys) — the pairing preserves
// key declaration order, matching an OSC producer's emit order.
//
// GATHER: Keys = inputStrings[0] split on ',' (trimmed); Values = inputLists[0] (the Float MultiInput,
// wire-declaration order). The Dict cook driver (cookFlatDict / cookResidentDict) fills DictCookCtx
// before calling this.
#include "runtime/dict_op_registry.h"  // DictOp / DictCookCtx / dictInjectBug
#include "runtime/graph.h"             // NodeSpec, PortSpec, Widget

#include <string>
#include <vector>

namespace sw {

int runSelectFromDictSelfTest(bool injectBug);  // golden lives in select_from_dict_golden.cpp

namespace {

// Split "a, b ,c" -> ["a","b","c"] (comma-separated, each token trimmed of surrounding spaces). Empty
// input -> no keys. A trailing comma yields a trailing empty token dropped by the min() zip if no value.
std::vector<std::string> splitKeys(const std::string& csv) {
  std::vector<std::string> out;
  size_t i = 0;
  while (i <= csv.size()) {
    size_t comma = csv.find(',', i);
    size_t end = (comma == std::string::npos) ? csv.size() : comma;
    size_t a = i, b = end;
    while (a < b && (csv[a] == ' ' || csv[a] == '\t')) ++a;
    while (b > a && (csv[b - 1] == ' ' || csv[b - 1] == '\t')) --b;
    if (comma == std::string::npos && a == b && out.empty()) break;  // wholly empty input
    if (!(comma == std::string::npos && a == b)) out.push_back(csv.substr(a, b - a));
    if (comma == std::string::npos) break;
    i = comma + 1;
  }
  return out;
}

// Pair Keys (inputStrings[0], comma-split) with Values (inputLists[0]) into output->entries, in order.
void cookBuildFloatDict(DictCookCtx& c) {
  if (!c.output) return;
  c.output->entries.clear();
  std::string keyCsv =
      (c.inputStrings && !c.inputStrings->empty()) ? (*c.inputStrings)[0] : std::string{};
  std::vector<std::string> keys = splitKeys(keyCsv);
  const std::vector<float>* vals =
      (c.inputLists && !c.inputLists->empty()) ? &(*c.inputLists)[0] : nullptr;
  if (vals) {
    const size_t n = keys.size() < vals->size() ? keys.size() : vals->size();
    for (size_t i = 0; i < n; ++i) c.output->entries.push_back({keys[i], (*vals)[i]});
  }
  // Test-only: corrupt the REAL produced dict (drop the last entry) on the actual produce path so a
  // golden's RED bites here (the consumer then sees a missing key), NOT by flipping the expected value.
  if (dictInjectBug() && !c.output->entries.empty()) c.output->entries.pop_back();
}

}  // namespace

// Self-registration. File-scope static DictOp — independent leaf .cpp.
//   Ports: "Keys"   = String (comma-separated dict keys, wire-OR-const, resident-safe);
//          "Values" = Float MultiInput (the dict values, paired 1:1 with Keys in declaration order);
//          "out"    = the Dict<float> output (the new host-map channel's currency).
static const DictOp _reg_buildfloatdict{
    {"BuildFloatDict", "BuildFloatDict",
     {{"Keys", "Keys", "String", true},
      {"Values", "Values", "Float", true, 0.0f, -100000.0f, 100000.0f, Widget::Slider, {}, false, 1,
       /*multiInput=*/true},
      {"out", "out", "Dict", false}},
     /*evaluate=*/nullptr},  // Dict output cannot ride NodeSpec::evaluate (returns a host map)
    cookBuildFloatDict};

}  // namespace sw
