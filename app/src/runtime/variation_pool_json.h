// runtime/variation_pool_json — on-disk JSON snapshot round-trip for the in-memory VariationPool
// (closes fork-pool-in-memory, variation_pool.h:42). The pool is the AllVariations list TiXL scans;
// this header is its FILE FORMAT — Variation.ToJson / TryLoadVariationFromJson ported to sw vocabulary.
//
// ZONE: runtime (pure computation — no GPU, no platform, no upward deps). Header-only on top of
// crude_json (the SAME serializer the .swproj rail uses, sw-patched to dump/parse raw UTF-8 byte-stable
// so a CJK Title round-trips — compound_save.cpp:92). Runtime leaf: variation_pool.h + crude_json only.
//
// ── TiXL ground-truth (Editor/.../Variation.cs:172-211 ToJson, :62-168 TryLoadVariationFromJson) ──
//   ToJson  -> { Id, IsPreset, ActivationIndex, Title?(omitted when empty, .cs:183),
//                PosOnCanvas, ParameterSetsForChildIds:{ childIdGuid:{ inputIdGuid: <InputValue json> }}}
//              — a childId with an EMPTY value map is skipped (.cs:194); Title is omitted when empty.
//   Load    -> reads Id (required — bail if absent, .cs:70), Title/ActivationIndex/IsPreset (defaulted
//              when absent, .cs:79-81); per child, per input: an input the referenced symbol does NOT
//              define is SKIPPED (`continue`, .cs:135-139 — missing-input tolerance, no crash); an
//              input flagged ExcludedFromPresets is SKIPPED (.cs:147-153); a child whose surviving
//              changeList is empty is not stored (.cs:161-164).
//
// ── NAMED FORKS ──────────────────────────────────────────────────────────────────────────────────
//  fork-pool-numeric-values — sw's VariationValue is a NUMERIC tagged union (float/vec2/vec3/vec4/int,
//    variation_pool.h:59). We serialize the tag + component array verbatim; string/bool "hold" and
//    Quaternion Slerp values are deferred (same line drawn at variation_pool.h:36). A non-numeric value
//    cannot exist in this pool, so the format need not carry one.
//  fork-pool-no-poscanvas — sw's VariationPool has no per-variation canvas position (the UI canvas is a
//    later L1 batch), so PosOnCanvas is omitted from the format. Everything TiXL persists that sw's
//    in-memory model actually HAS round-trips byte-stable.
//  fork-excluded-as-predicate — TiXL reads ExcludedFromPresets off the live SymbolUi registry; sw's
//    runtime pool has no UI registry, so the skip is driven by a caller-supplied predicate
//    `isExcluded(childId, inputId)` (faithful skip SEMANTICS — the excluded input never lands in the
//    loaded pool — without dragging the UI layer into runtime). Default predicate = nothing excluded.
#pragma once

#include <cstdint>
#include <functional>
#include <string>

#include "crude_json.h"
#include "runtime/variation_pool.h"

namespace sw {

// ── value <-> json ───────────────────────────────────────────────────────────────────────────────
// VariationValue -> { "type": <int tag>, "v": [components...] }. componentCount() drives how many of
// v[] are written, so a float carries one number and a vec4 carries four — no padding noise in the file.
inline crude_json::value variationValueToJson(const VariationValue& val) {
  crude_json::value o(crude_json::type_t::object);
  o["type"] = static_cast<crude_json::number>(static_cast<int>(val.type));
  crude_json::value arr(crude_json::type_t::array);
  auto& a = arr.get<crude_json::array>();
  const int n = val.componentCount();
  for (int i = 0; i < n; ++i) a.push_back(crude_json::value(static_cast<crude_json::number>(val.v[i])));
  o["v"] = arr;
  return o;
}

// json -> VariationValue. Tolerant: a malformed entry (missing/!object, bad tag, missing v[]) yields
// `ok=false` so the loader can SKIP it (missing-input tolerance — no crash, faithful to TiXL's
// `continue`). On success `out` carries the tag + components (extra/short v[] are clamped to the tag's
// component count so a truncated file can't read out of bounds).
inline bool variationValueFromJson(const crude_json::value& v, VariationValue& out) {
  if (!v.is_object() || !v.contains("type") || !v.contains("v")) return false;
  const crude_json::value& tv = v["type"];
  const crude_json::value& av = v["v"];
  if (!tv.is_number() || !av.is_array()) return false;
  const int tag = static_cast<int>(tv.get<crude_json::number>());
  if (tag < 0 || tag > static_cast<int>(VariationValue::Type::Int)) return false;
  out = VariationValue();
  out.type = static_cast<VariationValue::Type>(tag);
  const auto& a = av.get<crude_json::array>();
  const int n = out.componentCount();
  for (int i = 0; i < n && i < static_cast<int>(a.size()); ++i)
    if (a[i].is_number()) out.v[i] = static_cast<float>(a[i].get<crude_json::number>());
  return true;
}

// ── one Variation <-> json (TiXL Variation.ToJson / TryLoadVariationFromJson) ────────────────────
inline crude_json::value variationToJson(const Variation& var) {
  crude_json::value o(crude_json::type_t::object);
  o["Id"] = static_cast<crude_json::number>(static_cast<double>(var.id));
  o["IsPreset"] = static_cast<crude_json::boolean>(var.isPreset);
  o["ActivationIndex"] = static_cast<crude_json::number>(var.activationIndex);
  if (!var.title.empty()) o["Title"] = var.title;  // omitted when empty (TiXL .cs:183)

  crude_json::value sets(crude_json::type_t::object);
  for (const auto& [childId, values] : var.parameterSets) {
    if (values.empty()) continue;  // a child with no surviving values is skipped (TiXL .cs:194)
    crude_json::value childObj(crude_json::type_t::object);
    for (const auto& [inputId, value] : values)
      childObj[std::to_string(inputId)] = variationValueToJson(value);
    sets[std::to_string(childId)] = childObj;
  }
  o["ParameterSetsForChildIds"] = sets;
  return o;
}

// Predicate: is (childId, inputId) excluded from presets? (fork-excluded-as-predicate). Default = none.
using ExcludedPredicate = std::function<bool(NodeId, InputId)>;

// json -> Variation. Faithful to TiXL TryLoadVariationFromJson: Id required (return false if absent),
// Title/ActivationIndex/IsPreset defaulted when missing; per input — excluded inputs and malformed
// values are SKIPPED (missing-input tolerance); a child whose changeList ends up empty is not stored.
inline bool variationFromJson(const crude_json::value& v, Variation& out,
                              const ExcludedPredicate& isExcluded = {}) {
  if (!v.is_object() || !v.contains("Id") || !v["Id"].is_number()) return false;  // Id required
  out = Variation();
  out.id = static_cast<uint64_t>(v["Id"].get<crude_json::number>());
  if (v.contains("Title") && v["Title"].is_string()) out.title = v["Title"].get<crude_json::string>();
  if (v.contains("ActivationIndex") && v["ActivationIndex"].is_number())
    out.activationIndex = static_cast<int>(v["ActivationIndex"].get<crude_json::number>());
  if (v.contains("IsPreset") && v["IsPreset"].is_boolean())
    out.isPreset = v["IsPreset"].get<crude_json::boolean>();

  if (!v.contains("ParameterSetsForChildIds") || !v["ParameterSetsForChildIds"].is_object())
    return true;  // no parameter sets is a valid (empty) variation — not a parse failure
  const auto& sets = v["ParameterSetsForChildIds"].get<crude_json::object>();
  for (const auto& [childStr, childVal] : sets) {
    if (!childVal.is_object()) continue;
    const NodeId childId = static_cast<NodeId>(std::stoull(childStr));
    std::map<InputId, VariationValue> changeList;
    for (const auto& [inputStr, valueVal] : childVal.get<crude_json::object>()) {
      const InputId inputId = static_cast<InputId>(std::stoull(inputStr));
      if (isExcluded && isExcluded(childId, inputId)) continue;  // ExcludedFromPresets skip
      VariationValue parsed;
      if (!variationValueFromJson(valueVal, parsed)) continue;  // malformed → skip (tolerant)
      changeList[inputId] = parsed;
    }
    if (!changeList.empty()) out.parameterSets[childId] = changeList;  // empty child not stored
  }
  return true;
}

// ── whole pool <-> json string ───────────────────────────────────────────────────────────────────
// The pool serializes to { "Variations": [ <variation>... ] } in pool order (the AllVariations list).
// dump() of crude_json is deterministic (object keys are std::map-sorted), so the bytes are stable.
inline std::string variationPoolToJson(const VariationPool& pool) {
  crude_json::value root(crude_json::type_t::object);
  crude_json::value arr(crude_json::type_t::array);
  auto& a = arr.get<crude_json::array>();
  for (const Variation& var : pool.allVariations()) a.push_back(variationToJson(var));
  root["Variations"] = arr;
  return root.dump();
}

// Parse a pool JSON string back into `out` (cleared first). Tolerant: a malformed array element is
// skipped; a variation missing its Id is skipped (faithful — TryLoadVariationFromJson returns false and
// the caller drops it). Returns false only if the top-level JSON is not the expected object/array shape.
inline bool variationPoolFromJson(const std::string& json, VariationPool& out,
                                  const ExcludedPredicate& isExcluded = {}) {
  out = VariationPool();
  crude_json::value root = crude_json::value::parse(json);
  if (!root.is_object() || !root.contains("Variations") || !root["Variations"].is_array())
    return false;
  for (const crude_json::value& el : root["Variations"].get<crude_json::array>()) {
    Variation var;
    if (variationFromJson(el, var, isExcluded)) out.adopt(std::move(var));
  }
  return true;
}

}  // namespace sw
