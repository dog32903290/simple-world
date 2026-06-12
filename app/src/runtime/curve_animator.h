// runtime/curve_animator — the definition-layer Animator (S3, contract C3/P2). = TiXL
// Core/Operator/Animator.cs: a per-Symbol container of animation curves keyed by
// [childId][inputId] -> Curve[]. It is the AUTHORITY for Automation drivers (S4 scoreGraph 作廢,
// 拍板 P2): every Symbol definition owns one Animator; editing a curve on the definition broadcasts
// to all instances (reuse semantics, same as overrides / S13). The resident input's Automation
// driver carries a `curveRef` that resolves INTO this store at sample time.
//
// TiXL fidelity: the dictionary shape (_curvesByChildAndInput, Animator.cs:26), CopyAnimationsTo
// (Animator.cs:28-55, used by copy/paste — child-id remap), IsAnimated, the Write/Read JSON segment
// (Animator.cs:371-444). FORK from TiXL: childId is our int instance id (not a Guid) and inputId is
// our string slotId (= SlotDef.id), matching the rest of the compound model (compound_graph.h).
//
// curveRef format: "<childId>:<inputId>[#<index>]" — the Automation driver's projection key. The
// Animator resolves it to a Curve* (index defaults to 0 = the first/only channel for a Float input).
#pragma once
#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "runtime/curve.h"

namespace sw {

// = TiXL Animator (per-Symbol). Lives ON the Symbol definition (compound_graph.h adds a member).
class Animator {
 public:
  // Per (childId, inputId): the array of curves (one per scalar channel; Float = 1, VecN = N).
  // = TiXL Dictionary<Guid, Dictionary<Guid, Curve[]>> _curvesByChildAndInput.
  using CurveArray = std::vector<Curve>;

  // Is (childId, inputId) animated? = Animator.IsAnimated (Animator.cs:321-325).
  bool isAnimated(int childId, const std::string& inputId) const;
  bool isInstanceAnimated(int childId) const;  // = IsInstanceAnimated (cs:327-330)

  // Fetch the curve array for (childId, inputId), or nullptr. The Automation driver samples [0].
  const CurveArray* curvesFor(int childId, const std::string& inputId) const;
  CurveArray* curvesFor(int childId, const std::string& inputId);

  // Install/replace the curve array for (childId, inputId) = SetCurveArray (cs:467-475). Adding an
  // animation = this + the resident input's driver flipping to Automation (the two are one act, the
  // driver decides BOTH the resolved value and LIVE/STATIC — 拍板 driver enum).
  void setCurves(int childId, const std::string& inputId, CurveArray curves);
  // Animate an input with `n` channels (Float n=1, Vec2/3/4 n=2..4): one curve PER channel, each
  // with one live key at `time` valued values[k] (= Animator.AddCurvesForFloatVector cs:97-126:
  // Linear in/out, brokenTangents). Returns the channel-0 curveRef string.
  std::string animateFloatVector(int childId, const std::string& inputId, double time,
                                 const float* values, int n);
  // Convenience: the scalar (n=1) entry — kept as the existing callers' name.
  std::string animateFloat(int childId, const std::string& inputId, double time, float value);

  // Remove the animation on (childId, inputId) = RemoveAnimationFrom (cs:278-291). Drops the empty
  // child bucket. The caller flips the resident driver back to Constant separately.
  void remove(int childId, const std::string& inputId);
  // Drop EVERY animation on a child (= RemoveAnimationsFromInstances cs:57-61, used by remove-child).
  void removeChild(int childId);

  // = CopyAnimationsTo (Animator.cs:28-55): clone the animations of `childrenToCopyFrom` into
  // `target`, remapping child ids through oldToNew (copy/paste). Curves are deep-cloned.
  void copyAnimationsTo(Animator& target, const std::vector<int>& childrenToCopyFrom,
                        const std::map<int, int>& oldToNew) const;

  // Resolve a curveRef ("<childId>:<inputId>[#index]") to a sampleable Curve, or nullptr. THIS is
  // what the Automation driver calls each sample (S3 接通). index out of range -> nullptr.
  const Curve* resolveRef(const std::string& curveRef) const;

  // Build the curveRef string for (childId, inputId, index). index 0 omits the "#0" suffix.
  static std::string makeRef(int childId, const std::string& inputId, int index = 0);
  // Parse a curveRef back to its parts. Returns false on malformed input.
  static bool parseRef(const std::string& ref, int& childId, std::string& inputId, int& index);

  bool empty() const { return curves_.empty(); }
  // Iteration for the savev2 writer (Animator.Write cs:371-408): childId -> inputId -> CurveArray.
  const std::map<int, std::map<std::string, CurveArray>>& all() const { return curves_; }

 private:
  std::map<int, std::map<std::string, CurveArray>> curves_;  // childId -> inputId -> curves
};

// Headless RED->GREEN proof of S3 automation END-TO-END (the resident接通 + savev2): ① a resident
// node input with an Automation driver reads the curve @ localTime (playhead走曲線) ② Constant<->
// Automation toggle flips the cache LIVE<->STATIC in lockstep (cache-count proof, the 拍板 selftest
// leg) ③ a definition-layer curve edit changes EVERY instance (reuse broadcast) ④ savev2 roundtrip
// carries the animator bit-stable + a tampered animator entry is dropped locally (S15). injectBug
// breaks one expectation -> the assertion FAILS (teeth).
int runCurveAnimatorSelfTest(bool injectBug);
// ⑤vec leg (批次8 Vec multi-channel: build/projection golden/savev2 index roundtrip/remove),
// mechanical TU split (curve_animator_selftest_vec.cpp, ARCHITECTURE rule 4). Called by
// runCurveAnimatorSelfTest; returns the failure count.
int runCurveAnimatorVecLeg(bool injectBug);

}  // namespace sw
