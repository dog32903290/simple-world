// runtime/variation_crossfader — Lane L1 (Variation / Snapshot): the 2-way CROSSFADER. Blends the
// live parameter set between two snapshots (left @ fader 0, right @ fader 127) using TiXL's
// crossfade formula. Sits on top of variation_pool.h (the snapshots) and variation_mix.h (springDamp).
//
// ZONE: runtime (pure computation). Header-only; one responsibility (the crossfade state machine).
//
// ── TiXL ground-truth (BlendActions.cs:63-152, 215-262; SymbolVariationPool.cs:618) ─────────────
// Crossfader model: _snapshotLeft (pos 0) / _snapshotRight (pos 127) / _activeIsLeft. The blend
// TARGET is always the side OPPOSITE active.
//
//   UpdateBlendingTowardsProgress(index, midiValue):
//     pos = midiValue / 127
//     pos >= 0.99 → request finish-right ; pos <= 0.01 → request finish-left
//     else:  target     = activeIsLeft ? right : left
//            blendAmount = activeIsLeft ? pos  : (1 - pos)
//            SmoothVariationBlending.StartBlendTo(target, blendAmount)
//
//   SmoothVariationBlending.UpdateBlend():  (called once per frame)
//     dampedWeight = SpringDamp(targetWeight, dampedWeight, ref vel, 20, 1/60)
//     if |vel| < 0.0005 → CompleteBlendWhenDampingFinished() (commit + flip activeIsLeft); else
//        BeginBlendTowardsSnapshot(instance, target, dampedWeight) — applies the blend at dampedWeight.
//
//   Per-parameter apply (TryCreateBlendTowardsVariationCommand → BlendMethods → Lerp):
//     result = Lerp(currentLiveValue, targetVariationValue, dampedWeight)   // a + (b-a)*t
//     (when the variation lacks that param: TiXL lerps current→default; here, faithfully, we leave
//      params the target doesn't carry untouched — the "blend towards a sparse snapshot" semantics.)
//
// KEY FAITHFULNESS POINT (what the golden pins): the blend is current→target by the damped weight.
// At weight=0 the live value is unchanged (= A side, the active snapshot already applied); at
// weight=1 every param the target carries equals the target exactly (= B side); at weight=0.5 each
// param is the exact midpoint Lerp(a,b,0.5)=(a+b)/2. This is the byte-faithful crossfade behaviour.
//
// ── NAMED FORK ──────────────────────────────────────────────────────────────────────────────────
//  fork-crossfader-direct-apply — TiXL routes each blended value through a ChangeInputValueCommand /
//    MacroCommand (undoable) onto live Instance inputs. This runtime layer has no command stack /
//    Instance graph yet (that is the document-override L1 batch). So applyBlend() writes the blended
//    values into a plain live-value map (LiveParams) — the SAME numbers TiXL's command would set,
//    minus the undo wrapper. The commit (ApplyCurrentBlend) is therefore "freeze current live as the
//    new baseline + flip active"; the undo wiring lands with the document batch.
#pragma once

#include <cmath>
#include <cstdint>
#include <map>

#include "runtime/variation_mix.h"   // springDamp
#include "runtime/variation_pool.h"  // VariationPool / Variation / VariationValue

namespace sw {

// Live parameter values the crossfader reads from (A side) and writes blended results into.
// LiveParams[childId][inputId] = current value. Mirrors the composition's live input slots.
using LiveParams = std::map<NodeId, std::map<InputId, VariationValue>>;

class VariationCrossfader {
 public:
  static constexpr float kSpringConstant = 20.0f;     // TiXL BlendActions.cs:248
  static constexpr float kTimeStep = 1.0f / 60.0f;    // TiXL BlendActions.cs:244
  static constexpr float kSettleVelocity = 0.0005f;   // TiXL BlendActions.cs:251
  static constexpr float kRightThreshold = 0.99f;     // TiXL BlendActions.cs:81
  static constexpr float kLeftThreshold = 0.01f;      // TiXL BlendActions.cs:88

  explicit VariationCrossfader(const VariationPool& pool) : pool_(pool) {}

  // TiXL SetActiveSnapshot(index): left = index, activeIsLeft = true, right cleared.
  void setActiveSnapshot(int index) {
    snapshotLeft_ = index;
    snapshotRight_ = -1;
    activeIsLeft_ = true;
    dampedWeight_ = 0.0f;
    dampingVelocity_ = 0.0f;
    pendingCompletion_ = Completion::None;
  }

  // TiXL StartBlendingTowardsSnapshot(index): keep active where it is, place target on the opposite
  // side. dampedWeight resets to 0 (start from the active snapshot).
  void startBlendingTowards(int index) {
    int currentActive = activeIsLeft_ ? snapshotLeft_ : snapshotRight_;
    if (currentActive == -1) {
      snapshotLeft_ = index;
      snapshotRight_ = index;
      activeIsLeft_ = true;
    } else if (activeIsLeft_) {
      snapshotRight_ = index;
    } else {
      snapshotLeft_ = index;
    }
    dampedWeight_ = 0.0f;
    dampingVelocity_ = 0.0f;
    pendingCompletion_ = Completion::None;
  }

  // TiXL UpdateBlendingTowardsProgress(midiValue): map raw fader → targetWeight (0..1). Returns the
  // raw blendAmount it set as the spring target (for tests / UI readout). 0..127 fader convention.
  float updateFader(float midiValue) {
    if (snapshotLeft_ == -1 || snapshotRight_ == -1) return targetWeight_;
    const float pos = midiValue / 127.0f;
    if (pos >= kRightThreshold) { pendingCompletion_ = Completion::Right; return targetWeight_; }
    if (pos <= kLeftThreshold)  { pendingCompletion_ = Completion::Left;  return targetWeight_; }
    targetWeight_ = activeIsLeft_ ? pos : (1.0f - pos);  // blend toward the opposite (target) side
    return targetWeight_;
  }

  // One frame of damping (TiXL SmoothVariationBlending.UpdateBlend). Smooths dampedWeight toward
  // targetWeight via springDamp; applies the blend at the damped weight into `live`. When |vel| settles,
  // commits any pending endpoint completion (flips active). Returns the dampedWeight applied this frame.
  float tick(LiveParams& live) {
    if (snapshotLeft_ == -1 || snapshotRight_ == -1) return dampedWeight_;
    dampedWeight_ = springDamp(targetWeight_, dampedWeight_, dampingVelocity_, kSpringConstant, kTimeStep);

    if (std::fabs(dampingVelocity_) < kSettleVelocity) {
      completePending();
      return dampedWeight_;
    }
    applyBlend(live, dampedWeight_);
    return dampedWeight_;
  }

  // Apply the 2-way blend at an explicit weight (current→target Lerp). Public so the golden can pin
  // exact values at weight 0 / 0.5 / 1 without driving the spring. `weight` in [0,1].
  void applyBlend(LiveParams& live, float weight) const {
    const int targetIndex = activeIsLeft_ ? snapshotRight_ : snapshotLeft_;
    const Variation* target = pool_.tryGetSnapshot(targetIndex);
    if (!target) return;
    for (const auto& [childId, inputs] : target->parameterSets) {
      for (const auto& [inputId, targetValue] : inputs) {
        auto& liveChild = live[childId];
        auto lit = liveChild.find(inputId);
        // a = current live value (or, if none yet, the target itself so weight has something to lerp
        // from — degenerate; on a real composition the live value always exists).
        const VariationValue a = (lit != liveChild.end()) ? lit->second : targetValue;
        liveChild[inputId] = a.blendTo(targetValue, weight);  // Lerp(a, b, weight) (verbatim)
      }
    }
  }

  // Accessors (UI / tests).
  int snapshotLeft() const { return snapshotLeft_; }
  int snapshotRight() const { return snapshotRight_; }
  bool activeIsLeft() const { return activeIsLeft_; }
  float dampedWeight() const { return dampedWeight_; }
  float targetWeight() const { return targetWeight_; }
  // TiXL ActiveSnapshotIndex / BlendTowardsIndex.
  int activeSnapshotIndex() const {
    if (snapshotLeft_ == -1 && snapshotRight_ == -1) return -1;
    return activeIsLeft_ ? snapshotLeft_ : snapshotRight_;
  }
  int blendTowardsIndex() const {
    if (snapshotLeft_ == -1 || snapshotRight_ == -1) return -1;
    return activeIsLeft_ ? snapshotRight_ : snapshotLeft_;
  }

 private:
  enum class Completion { None, Left, Right };

  // TiXL CompleteBlendWhenDampingFinished: commit + flip active toward the completing side.
  void completePending() {
    if (pendingCompletion_ == Completion::None) return;
    const Completion side = pendingCompletion_;
    pendingCompletion_ = Completion::None;
    if (side == Completion::Right) activeIsLeft_ = false;
    else                           activeIsLeft_ = true;
    // (fork-crossfader-direct-apply) ApplyCurrentBlend's undo-add is a later batch; the live values
    // already hold the committed blend, and the active flip is the durable state change here.
    dampedWeight_ = 0.0f;
    dampingVelocity_ = 0.0f;
    targetWeight_ = 0.0f;
  }

  const VariationPool& pool_;
  int snapshotLeft_ = -1;
  int snapshotRight_ = -1;
  bool activeIsLeft_ = true;
  float targetWeight_ = 0.0f;
  float dampedWeight_ = 0.0f;
  float dampingVelocity_ = 0.0f;
  Completion pendingCompletion_ = Completion::None;
};

}  // namespace sw
