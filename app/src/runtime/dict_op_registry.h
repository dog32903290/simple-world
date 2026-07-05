// runtime/dict_op_registry — self-registration seam for DICT<float> PRODUCER ops + the host-side
// Dict<float> value currency (the dict-currency seam). This is the HOST value-currency parallel of
// FloatList (floatlist_op_registry.h) / ColorList / String, widened to a string->float MAP.
//
// TiXL authority: external/tixl/Core/DataTypes/Dict.cs — `Dict<T> : Dictionary<string, T>`. A
// `Dict<float>` (Slot<Dict<float>>) rides between ops as a string-keyed float map. Its PRODUCERS are the
// device-io family (OscInput.Contents / Gamepad.State / MidiClip.Values / PosiStageInput.TrackersAsDict
// / GetBeatTimingDetails.Values / WebSocketServer / WebSocketClient / FreeDInput — all `Slot<Dict<float>>`
// outputs, see external/tixl/Operators/Lib/io/**). Its CONSUMERS are the SelectFloatFromDict /
// SelectVec2FromDict / SelectVec3FromDict / SelectBoolFromFloatDict family (numbers/data/utils/**), which
// take a `Dict<float>` input + a string key and produce a scalar/vector/bool — those ride the HOST-SCALAR
// rail (host_scalar_op_registry.h), NOT this producer rail.
//
// THE CURRENCY (SwFloatDict): TiXL's `Dict<float>` is a `Dictionary<string,float>` — but the vector
// consumers (SelectVec2/Vec3) iterate `_dict.Keys` in a DETERMINISTIC ORDER (SelectVec2 FindKeyForY uses
// `_dict.Keys.OrderBy(x=>x)` = ordinal sort; SelectVec3 TryUpdateVectorKeysRelatedToX sorts keys ordinal
// too — StringUtils.cs:219). So the currency must PRESERVE every key (no dedup collapse) and the consumer
// SORTS on read. We store an ORDERED vector<pair<string,float>> (insertion order = the OSC producer's
// emit order); the consumers do their own ordinal sort where TiXL does. `tryGet` is TiXL's
// `Dict.TryGetValue` (last write wins on a duplicate key, mirroring Dictionary indexer assignment).
//
// SCOPE (minimal, single-wire): a Dict input port is gathered by cookFlatDict / cookResidentDict on the
// host-scalar rail as a SINGLE wire (producer -> consumer). No MultiInput Dict-merge (TiXL has none).
//
// Pattern cloned from floatlist_op_registry.h: adding a Dict producer = ONE leaf .cpp ending with a
// file-scope `DictOp` registrar, feeding two sinks: dictSpecSink() (NodeSpec -> Add menu / findSpec) +
// dictCookFns() (type-name -> DictCookFn, the producer cook the host-scalar Dict gather dispatches).
//
// Init-order safety (identical to the floatlist / host-scalar sinks): every registrar is a namespace-
// scope static, finishing its dynamic-init ctor before main and before any LIVE sink read.
#pragma once

#include <map>
#include <string>
#include <utility>
#include <vector>

#include "runtime/graph.h"  // NodeSpec

namespace MTL {
class Device;
class Library;
class CommandQueue;
}  // namespace MTL

struct EvaluationContext;  // runtime/eval_context.h

namespace sw {

// The Dict<float> host currency: an ORDERED string->float map (insertion order preserved; consumers that
// need a canonical order sort on read, mirroring TiXL's OrderBy(x=>x) / ordinal key sort). A vector of
// pairs (NOT std::map) because TiXL's producers emit keys in a MEANINGFUL order and the consumers depend
// on that iteration order (SelectVec2/Vec3), while a std::map would silently re-order to key-sorted.
struct SwFloatDict {
  std::vector<std::pair<std::string, float>> entries;

  // TiXL Dict.TryGetValue (Dictionary<string,float>): return true + write `out` if `key` present.
  // Last matching entry wins (Dictionary indexer assignment overwrites — a duplicate key keeps the last).
  bool tryGet(const std::string& key, float& out) const {
    bool found = false;
    for (const auto& kv : entries)
      if (kv.first == key) { out = kv.second; found = true; }  // last write wins
    return found;
  }
  bool has(const std::string& key) const { float t; return tryGet(key, t); }
  size_t count() const { return entries.size(); }
};

// Everything a Dict PRODUCER op gets to cook one node this frame. Mirrors FloatListCookCtx but the
// currency is a HOST SwFloatDict (no GPU buffer, no pre-sizing — the op writes entries). A pure producer
// (BuildFloatDict) reads its resolved string/float inputs and fills *output.
//
//   inputStrings : resolved upstream String inputs of THIS node (spec port order, wire-OR-const) — a
//                  producer that builds a dict from string keys reads these (BuildFloatDict.Keys).
//   inputLists   : cooked upstream FloatList inputs (spec port order) — BuildFloatDict.Values.
//   params       : resolved Float params of THIS node.
//   output       : THIS node's dict. The op writes output->entries; the driver owns + stores it.
struct DictCookCtx {
  MTL::Device* dev = nullptr;
  MTL::Library* lib = nullptr;
  MTL::CommandQueue* queue = nullptr;
  const EvaluationContext* ctx = nullptr;
  int nodeId = 0;
  const std::vector<std::string>* inputStrings = nullptr;
  const std::vector<std::vector<float>>* inputLists = nullptr;
  const std::map<std::string, float>* params = nullptr;
  SwFloatDict* output = nullptr;
};

// A Dict producer op: read inputs -> write *output (a SwFloatDict). ONE fn (a host map self-sizes).
using DictCookFn = void (*)(DictCookCtx&);

// --- the two sinks every Dict-producer leaf registrar feeds ---
std::vector<NodeSpec>& dictSpecSink();               // NodeSpecs (node_registry reads live)
std::map<std::string, DictCookFn>& dictCookFns();     // type-name -> producer cook fn

// Lookup the producer cook fn for a type (nullptr if not a Dict producer). Used by the Dict gather.
const DictCookFn* findDictOp(const std::string& type);

// Read a Float param from a DictCookCtx's RESOLVED map (mirror of floatListParam); `def` when no map.
float dictParam(const std::map<std::string, float>* params, const char* id, float def);

// Test-only injection seam (goldens): when set, a Dict producer's cook corrupts its REAL output (e.g.
// drops the last entry) so a golden's RED case fires on the actual PRODUCE path (NOT by flipping the
// expected value). Off in production. A leaf reads it at the end of its cook.
bool& dictInjectBug();

// RAII registrar: declare one file-scope static of this type at the end of each Dict-producer leaf.
struct DictOp {
  DictOp(NodeSpec spec, DictCookFn cook);
};

}  // namespace sw
