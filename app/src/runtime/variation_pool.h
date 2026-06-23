// runtime/variation_pool — Lane L1 (Variation / Snapshot — the VJ live-performance core):
// the snapshot POOL (store / retrieve / filter parameter snapshots) + the 2-way CROSSFADER
// (interpolate / blend between two snapshots using TiXL's crossfade formula).
//
// This is the next L1 deliverable after the math harness in variation_mix.h (commit 10e7845,
// springDamp + mixFloat golden). It rides ON those primitives:
//   - the crossfader smooths the raw 0..1 fader with sw::springDamp (variation_mix.h)
//   - the per-parameter blend is TiXL's Lerp (a + (b-a)*t), the 2-way cousin of mixFloat's
//     N-way normalized weighted average.
//
// ZONE: runtime (pure computation — no GPU, no platform, no upward deps). Header-only structs +
// free / member functions in the surrounding runtime style. The pool is plain in-memory state;
// document-override wiring + UI canvas + MIDI/OSC fader input are LATER L1 batches (see end note).
//
// ── TiXL ground-truth (READ-ONLY external/tixl, byte-faithful) ──────────────────────────────────
// Data model        Variation.cs:18-38  — a Variation = {Id/Title/ActivationIndex/IsPreset,
//                                          ParameterSetsForChildIds[childId][inputId] = value},
//                                          childId Guid.Empty = composition itself. IsSnapshot=!IsPreset.
// Capture / filter  VariationHandling.cs:106-164 — CreateOrUpdateSnapshotVariation(index):
//                                          delete existing at index → collect ONLY children with
//                                          EnabledForSnapshots==true → build variation → set index.
// Pool lookup       SymbolVariationPool.cs:858 — TryGetSnapshot(index): linear scan AllVariations
//                                          for the first with matching ActivationIndex.
// 2-way blend       SymbolVariationPool.cs:618 → ValueUtils.cs:21-58 (BlendMethods) →
//                                          MathUtils.cs:305 Lerp(a,b,t)=a+(b-a)*t, PER input type
//                                          (float / Vector2 / Vector3 / Vector4 / int …). a = the
//                                          CURRENT live value, b = the target variation's stored
//                                          value, t = the damped blend weight.
// Crossfader        BlendActions.cs:63-152, 215-262 — pos=midi/127; ≥0.99 → finish-right /
//                                          ≤0.01 → finish-left; mid → blendAmount = activeIsLeft
//                                          ? pos : (1-pos); damp via SpringDamp(target,damped,
//                                          ref vel, 20, 1/60); |vel|<0.0005 → commit (ApplyCurrentBlend
//                                          + flip activeIsLeft).
//
// ── NAMED FORKS (faithful where it matters, simplifications named) ──────────────────────────────
//  fork-pool-numeric-values — TiXL blends every InputValue type via a type→func table (incl.
//    Quaternion Slerp, string/bool "t<=0.5 ? a : b" hold). This batch models the NUMERIC crossfade
//    types that carry VJ morphs (float/vec2/vec3/vec4/int) with a small tagged VariationValue +
//    per-type Lerp. Non-numeric "hold" types (string/bool) and Quaternion Slerp are deferred to a
//    later L1 batch (the crossfader loop itself is type-agnostic — it calls value.blendTo). int uses
//    TiXL's truncating (int)(a+(b-a)*t) verbatim.
//  fork-pool-in-memory — no on-disk per-symbol JSON pool yet (Variation.ToJson / TryLoadVariationFromJson
//    is the file format; document-override wiring is a later batch). The pool is an in-memory vector,
//    exactly the AllVariations list TiXL scans.
//
// Each file <400 lines (ARCHITECTURE rule 4). Pool + crossfader split into two headers so neither
// grows past the limit and each has one responsibility.
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace sw {

// A blendable parameter value. Tagged union over the NUMERIC crossfade types (fork-pool-numeric-values).
// `blendTo(b, t)` is TiXL's per-type Lerp: a + (b-a)*t, componentwise; int truncates like TiXL.
struct VariationValue {
  enum class Type : uint8_t { Float, Vec2, Vec3, Vec4, Int };
  Type type = Type::Float;
  float v[4] = {0, 0, 0, 0};  // float→v[0]; vec2→v[0..1]; vec3→v[0..2]; vec4→v[0..3]; int→v[0]

  static VariationValue makeFloat(float a) { VariationValue r; r.type = Type::Float; r.v[0] = a; return r; }
  static VariationValue makeVec2(float x, float y) {
    VariationValue r; r.type = Type::Vec2; r.v[0] = x; r.v[1] = y; return r;
  }
  static VariationValue makeVec3(float x, float y, float z) {
    VariationValue r; r.type = Type::Vec3; r.v[0] = x; r.v[1] = y; r.v[2] = z; return r;
  }
  static VariationValue makeVec4(float x, float y, float z, float w) {
    VariationValue r; r.type = Type::Vec4; r.v[0] = x; r.v[1] = y; r.v[2] = z; r.v[3] = w; return r;
  }
  static VariationValue makeInt(int a) { VariationValue r; r.type = Type::Int; r.v[0] = (float)a; return r; }

  int componentCount() const {
    switch (type) {
      case Type::Float: return 1;
      case Type::Vec2:  return 2;
      case Type::Vec3:  return 3;
      case Type::Vec4:  return 4;
      case Type::Int:   return 1;
    }
    return 1;
  }

  // TiXL ValueUtils.BlendMethods → MathUtils.Lerp(a,b,t)=a+(b-a)*t, per component. `t` in [0,1];
  // t=0 → exactly *this (A), t=1 → exactly b (B). int truncates after the lerp (TiXL MathUtils.cs:441).
  // Type mismatch is treated as TiXL treats it (BlendMethods returns null on a type mismatch → the
  // input is skipped); here we just keep *this unchanged so a mistyped blend is a no-op, not a crash.
  VariationValue blendTo(const VariationValue& b, float t) const {
    if (b.type != type) return *this;  // faithful: TiXL skips a mismatched-type blend
    VariationValue r;
    r.type = type;
    const int n = componentCount();
    for (int i = 0; i < n; ++i) {
      const float lerped = v[i] + (b.v[i] - v[i]) * t;            // a + (b-a)*t  (verbatim)
      r.v[i] = (type == Type::Int) ? (float)(int)(lerped) : lerped;  // int truncates (verbatim)
    }
    return r;
  }

  bool equals(const VariationValue& o, float tol = 1e-4f) const {
    if (o.type != type) return false;
    const int n = componentCount();
    for (int i = 0; i < n; ++i)
      if (std::fabs(v[i] - o.v[i]) > tol) return false;
    return true;
  }
};

// A parameter snapshot — TiXL Variation (Variation.cs). NodeId / InputId stand in for TiXL's Guids;
// kCompositionNode == TiXL Guid.Empty (the composition-self bucket).
using NodeId = uint64_t;
using InputId = uint64_t;
constexpr NodeId kCompositionNode = 0;

struct Variation {
  uint64_t id = 0;
  std::string title;
  int activationIndex = -1;   // TiXL ActivationIndex — the MIDI pad / number-key slot
  bool isPreset = false;      // IsSnapshot == !isPreset
  // ParameterSetsForChildIds[childId][inputId] = value (TiXL Variation.cs:38).
  std::map<NodeId, std::map<InputId, VariationValue>> parameterSets;

  bool isSnapshot() const { return !isPreset; }

  // Look up one stored parameter (childId/inputId). Returns nullptr if this variation does not carry it.
  const VariationValue* find(NodeId childId, InputId inputId) const {
    auto cit = parameterSets.find(childId);
    if (cit == parameterSets.end()) return nullptr;
    auto iit = cit->second.find(inputId);
    if (iit == cit->second.end()) return nullptr;
    return &iit->second;
  }
};

// A child node's live parameter values + whether it participates in snapshots (TiXL
// SymbolChildUi.EnabledForSnapshots). The composition feeds the pool a list of these to capture from.
struct SnapshotChildState {
  NodeId childId = kCompositionNode;
  bool enabledForSnapshots = true;          // TiXL EnabledForSnapshots filter
  std::map<InputId, VariationValue> values;  // current live values for this child's inputs
};

// The snapshot pool — TiXL SymbolVariationPool's AllVariations list + capture/lookup (fork-pool-in-memory).
class VariationPool {
 public:
  // TiXL CreateOrUpdateSnapshotVariation(index): delete any existing snapshot at `index` first, then
  // capture ONLY EnabledForSnapshots children's values into a fresh Variation at that index. Returns
  // a pointer to the stored variation (owned by the pool).
  const Variation* createOrUpdateSnapshot(int activationIndex,
                                          const std::vector<SnapshotChildState>& children,
                                          const std::string& title = "") {
    removeSnapshot(activationIndex);  // delete previous at this index (TiXL VariationHandling.cs:115-118)
    Variation v;
    v.id = nextId_++;
    v.title = title;
    v.activationIndex = activationIndex;
    v.isPreset = false;  // snapshot
    for (const SnapshotChildState& c : children) {
      if (!c.enabledForSnapshots) continue;  // TiXL EnabledForSnapshots filter (VariationHandling.cs:159)
      if (c.values.empty()) continue;
      v.parameterSets[c.childId] = c.values;
    }
    all_.push_back(std::move(v));
    return &all_.back();
  }

  // TiXL SymbolVariationPool.TryGetSnapshot(index): first variation with matching activationIndex.
  const Variation* tryGetSnapshot(int activationIndex) const {
    for (const Variation& v : all_) {
      if (v.activationIndex == activationIndex) return &v;
    }
    return nullptr;
  }

  // TiXL DeleteVariation by index (used by createOrUpdate's overwrite + RemoveSnapshotAtIndex).
  bool removeSnapshot(int activationIndex) {
    auto it = std::find_if(all_.begin(), all_.end(),
                           [&](const Variation& v) { return v.activationIndex == activationIndex; });
    if (it == all_.end()) return false;
    all_.erase(it);
    return true;
  }

  const std::vector<Variation>& allVariations() const { return all_; }
  size_t size() const { return all_.size(); }

 private:
  std::vector<Variation> all_;
  uint64_t nextId_ = 1;
};

}  // namespace sw
