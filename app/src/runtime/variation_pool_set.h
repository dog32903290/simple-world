// runtime/variation_pool_set — Lane L1 (Variation / Snapshot): the MULTI-POOL model. TiXL keeps ONE
// SymbolVariationPool PER composition symbol — entering a composition switches which pool the panel
// reads (VariationHandling.cs:76 `ActivePoolForSnapshots = GetOrLoadVariations(activeComposition
// Instance.Symbol.Id)`; the per-symbol cache is GetOrLoadVariations, VariationHandling.cs:89), and each
// pool serializes to its own variations/{compositionSymbolId}.var (SymbolVariationPool.cs:148-150).
// This header is sw's shape of that model: a keyed collection of per-composition snapshot pools + its
// JSON round-trip, so the .swproj can carry every non-empty pool ({symbolId: pool} — the single-file
// cousin of TiXL's one-.var-per-symbol folder, same S20 fork compound_save.h documents for symbols).
//
// ZONE: runtime (pure computation — no GPU, no platform, no upward deps). Header-only structs in the
// variation_pool.h style, riding on crude_json (the SAME serializer the .swproj rail uses).
//
// ── NAMED FORKS ──────────────────────────────────────────────────────────────────────────────────
//  fork-poolset-doc-vocab — the snapshots stored here are in the DOCUMENT's float-per-slot vocabulary
//    (parameterSets[childId int][slotId string] = float), i.e. the panel's DocVariation capture
//    (variation_apply.h fork-pool-docvocab), NOT the runtime VariationPool's uint64-keyed
//    VariationValue. The golden-proven variation_pool_json.h format keys inputs by uint64 InputId and
//    cannot carry the document's STRING slot ids losslessly, so the pool-set serializes the same TiXL
//    field shape (Id / Title / ActivationIndex / IsPreset / ParameterSetsForChildIds, Variation.cs:
//    172-211) with string slot keys + plain numbers, under the same tolerant-load discipline
//    (malformed entry → skipped locally, never a crash — TiXL TryLoadVariationFromJson `continue`).
//  fork-poolset-single-file — TiXL writes one .var file per composition symbol; sw's document is a
//    single .swproj, so the pool set nests as ONE {symbolId: pool} object inside it (same fork the
//    symbol library itself took, compound_save.h S20).
#pragma once

#include <map>
#include <string>

#include "crude_json.h"

namespace sw {

// One captured snapshot in the document's float-per-slot vocabulary — TiXL Variation (Variation.cs:
// 18-38) as the panel captures it: parameterSets[childId][slotId] = value. The slot index it sits at
// in its pool is the ActivationIndex (the MIDI pad / number-key identity).
struct DocSnapshot {
  std::string title;
  std::map<int, std::map<std::string, float>> parameterSets;  // [childId][slotId] = captured value

  int paramCount() const {
    int n = 0;
    for (const auto& [childId, values] : parameterSets) n += static_cast<int>(values.size());
    return n;
  }
};

// One composition's pool: activation slot index (1-based) -> snapshot. The map key IS TiXL's
// ActivationIndex (SymbolVariationPool.TryGetSnapshot scans AllVariations for it; a keyed map is the
// same lookup with the overwrite-at-index semantics built in).
using DocSnapshotPool = std::map<int, DocSnapshot>;

// The keyed pool collection — TiXL's per-composition-symbol SymbolVariationPool cache
// (VariationHandling.GetOrLoadVariations, .cs:89). poolFor() is the create-on-demand entry the panel
// uses when ENTERING a composition (switch composition == switch pool).
class VariationPoolSet {
 public:
  DocSnapshotPool& poolFor(const std::string& compositionSymbolId) {
    return pools_[compositionSymbolId];
  }
  const DocSnapshotPool* find(const std::string& compositionSymbolId) const {
    auto it = pools_.find(compositionSymbolId);
    return it == pools_.end() ? nullptr : &it->second;
  }
  const std::map<std::string, DocSnapshotPool>& all() const { return pools_; }
  bool anyNonEmpty() const {
    for (const auto& [id, pool] : pools_)
      if (!pool.empty()) return true;
    return false;
  }
  void clear() { pools_.clear(); }

 private:
  std::map<std::string, DocSnapshotPool> pools_;  // compositionSymbolId -> that composition's pool
};

// ── pool set <-> json ────────────────────────────────────────────────────────────────────────────
// Shape (TiXL Variation.ToJson field names, Variation.cs:172-211, in doc vocab — fork-poolset-doc-vocab):
//   { "<compositionSymbolId>": { "Variations": [
//       { "Id": <slot>, "Title": <string, omitted when empty>, "ActivationIndex": <slot>,
//         "IsPreset": false,
//         "ParameterSetsForChildIds": { "<childId>": { "<slotId>": <number> } } } ] }, ... }
// Only NON-EMPTY pools are written (an empty pool carries nothing worth a file entry — TiXL only has
// a .var once something was saved into it). crude_json dump is std::map-sorted → bytes are stable.
inline crude_json::value variationPoolSetToJsonValue(const VariationPoolSet& set) {
  crude_json::value root(crude_json::type_t::object);
  for (const auto& [symbolId, pool] : set.all()) {
    if (pool.empty()) continue;  // 非空池 only
    crude_json::value arr(crude_json::type_t::array);
    auto& a = arr.get<crude_json::array>();
    for (const auto& [slotIndex, snap] : pool) {
      crude_json::value o(crude_json::type_t::object);
      o["Id"] = static_cast<crude_json::number>(slotIndex);  // slot identity doubles as Id (no Guids in sw)
      if (!snap.title.empty()) o["Title"] = snap.title;      // omitted when empty (TiXL .cs:183)
      o["ActivationIndex"] = static_cast<crude_json::number>(slotIndex);
      o["IsPreset"] = static_cast<crude_json::boolean>(false);  // snapshots only (Preset 子系統=下一棒)
      crude_json::value sets(crude_json::type_t::object);
      for (const auto& [childId, values] : snap.parameterSets) {
        if (values.empty()) continue;  // empty child skipped (TiXL .cs:194)
        crude_json::value childObj(crude_json::type_t::object);
        for (const auto& [slotId, value] : values)
          childObj[slotId] = static_cast<crude_json::number>(value);
        sets[std::to_string(childId)] = childObj;
      }
      o["ParameterSetsForChildIds"] = sets;
      a.push_back(o);
    }
    crude_json::value poolObj(crude_json::type_t::object);
    poolObj["Variations"] = arr;
    root[symbolId] = poolObj;
  }
  return root;
}

// json -> pool set (cleared first). Tolerant (TiXL TryLoadVariationFromJson discipline): a malformed
// pool / variation / child / value is SKIPPED locally — never a crash, never a whole-file failure.
// ActivationIndex is the slot identity and is required per entry (TiXL requires Id, .cs:70 — same
// "no identity, no entry" rule). Returns false only when `v` is not an object at all.
inline bool variationPoolSetFromJson(const crude_json::value& v, VariationPoolSet& out) {
  out.clear();
  if (!v.is_object()) return false;
  for (const auto& [symbolId, poolVal] : v.get<crude_json::object>()) {
    if (!poolVal.is_object() || !poolVal.contains("Variations") ||
        !poolVal["Variations"].is_array())
      continue;  // malformed pool dropped locally
    DocSnapshotPool& pool = out.poolFor(symbolId);
    for (const crude_json::value& el : poolVal["Variations"].get<crude_json::array>()) {
      if (!el.is_object() || !el.contains("ActivationIndex") || !el["ActivationIndex"].is_number())
        continue;  // no slot identity -> entry dropped (TiXL Id-required rule)
      const int slotIndex = static_cast<int>(el["ActivationIndex"].get<crude_json::number>());
      DocSnapshot snap;
      if (el.contains("Title") && el["Title"].is_string())
        snap.title = el["Title"].get<crude_json::string>();
      if (el.contains("ParameterSetsForChildIds") && el["ParameterSetsForChildIds"].is_object()) {
        for (const auto& [childStr, childVal] : el["ParameterSetsForChildIds"].get<crude_json::object>()) {
          if (!childVal.is_object()) continue;
          int childId = 0;
          try {
            childId = std::stoi(childStr);
          } catch (...) {
            continue;  // non-numeric child key dropped (doc child ids are ints)
          }
          std::map<std::string, float> values;
          for (const auto& [slotId, valueVal] : childVal.get<crude_json::object>()) {
            if (!valueVal.is_number()) continue;  // malformed value skipped (tolerant)
            values[slotId] = static_cast<float>(valueVal.get<crude_json::number>());
          }
          if (!values.empty()) snap.parameterSets[childId] = std::move(values);  // empty child not stored
        }
      }
      pool[slotIndex] = std::move(snap);
    }
    // A pool whose every entry was dropped stays as an empty entry — harmless (filtered on write).
  }
  return true;
}

}  // namespace sw
