// app/animation_commands_selftest_s6 — S6 legs of --selftest-animgui (⑧ group snapshot /
// ⑨ interpolation switch / ⑩ tangent write), split mechanically out of
// animation_commands_selftest.cpp (ARCHITECTURE rule 4: one file one responsibility, <400).
// Called by runAnimGuiSelfTest; returns its failure count so the parent aggregates one verdict.
#include "app/animation_commands.h"

#include <cstdio>
#include <memory>
#include <string>
#include <vector>

#include "app/command.h"
#include "runtime/compound_save.h"  // libToJsonV2 (byte-faithful undo 比對)
#include "runtime/graph.h"          // defaultParticleGraph (selftest seed)
#include "runtime/graph_bridge.h"   // libFromGraph (selftest seed)

namespace sw {
namespace {

// Same shape as animation_commands_selftest.cpp's finder (test-local seam, duplicated on purpose:
// the two TUs stay independently compilable).
struct AnimTarget { int childId = 0; std::string slotId; float def = 0.0f; bool ok = false; };
AnimTarget findFloatTarget(SymbolLibrary& lib) {
  AnimTarget t;
  Symbol* root = lib.find(lib.rootId);
  if (!root) return t;
  for (const SymbolChild& c : root->children) {
    const Symbol* d = lib.find(c.symbolId);
    if (!d || !d->atomic) continue;
    for (const SlotDef& s : d->inputDefs) {
      if (s.dataType == "Float") {
        t.childId = c.id; t.slotId = s.id; t.def = s.def; t.ok = true; return t;
      }
    }
  }
  return t;
}

// First TWO distinct (childId, slotId) Float inputs (⑧ multi-group gesture needs two lanes on
// different inputs). May span two children or two inputs of one child — either proves the group.
std::vector<AnimTarget> findTwoFloatTargets(SymbolLibrary& lib) {
  std::vector<AnimTarget> out;
  Symbol* root = lib.find(lib.rootId);
  if (!root) return out;
  for (const SymbolChild& c : root->children) {
    const Symbol* d = lib.find(c.symbolId);
    if (!d || !d->atomic) continue;
    for (const SlotDef& s : d->inputDefs) {
      if (s.dataType != "Float") continue;
      AnimTarget t;
      t.childId = c.id; t.slotId = s.id; t.def = s.def; t.ok = true;
      out.push_back(std::move(t));
      if (out.size() == 2) return out;
    }
  }
  return out;
}

#define CHK(cond, msg)                                                       \
  do {                                                                       \
    if (!(cond)) { printf("  [animgui] FAIL %s\n", msg); ++failures; }       \
    else printf("  [animgui] ok   %s\n", msg);                              \
  } while (0)

}  // namespace

int runAnimGuiS6Legs(bool injectBug) {
  int failures = 0;

  // ⑧ S6 group snapshot: ONE gesture spanning TWO (child,input) groups (multi-select delete).
  //    Undo restores both byte-faithful; redo re-deletes. = TiXL MacroCommand("Delete keyframes").
  {
    SymbolLibrary lib8 = libFromGraph(defaultParticleGraph());
    auto targets = findTwoFloatTargets(lib8);
    if (targets.size() < 2) {
      printf("  [animgui] FAIL ⑧ needs two distinct Float inputs in the seed graph\n");
      ++failures;
    } else {
      const std::string r8 = lib8.rootId;
      CommandStack s8;
      // Two animated inputs, two keys each (1.0 / 2.0).
      for (const AnimTarget& t : targets) {
        s8.push(std::make_unique<AddAnimationCommand>(lib8, r8, t.childId, t.slotId, 1.0, 1.0f));
        s8.push(std::make_unique<AddKeyframeCommand>(lib8, r8, t.childId, t.slotId, 0, 2.0));
      }
      const std::string seeded = libToJsonV2(lib8);
      // Build the group edit on COPIES (UI flow): delete key@2.0 from BOTH inputs in one command.
      std::vector<CurveGroupEdit> edits;
      for (const AnimTarget& t : targets) {
        const Animator::CurveArray* arr = lib8.find(r8)->animator.curvesFor(t.childId, t.slotId);
        Animator::CurveArray after = *arr;
        after[0].removeAt(2.0);
        edits.push_back(CurveGroupEdit{t.childId, t.slotId, *arr, std::move(after)});
      }
      s8.push(std::make_unique<SetCurveGroupSnapshotCommand>(lib8, r8, std::move(edits),
                                                             "Delete Keyframes"));
      bool bothDeleted = true;
      for (const AnimTarget& t : targets) {
        const Curve& c = (*lib8.find(r8)->animator.curvesFor(t.childId, t.slotId))[0];
        bothDeleted = bothDeleted && c.count() == 1 && !c.hasKeyAt(Curve::roundTime(2.0));
      }
      CHK(bothDeleted, "⑧ one group command deleted key@2.0 across BOTH inputs");
      s8.undo();
      CHK(libToJsonV2(lib8) == seeded, "⑧ group undo restored BOTH inputs byte-faithful");
      s8.redo();
      bool redone = true;
      for (const AnimTarget& t : targets)
        redone = redone && (*lib8.find(r8)->animator.curvesFor(t.childId, t.slotId))[0].count() == 1;
      CHK(redone, "⑧ group redo re-applied both deletions");
    }
  }

  // ⑨ S6 interpolation switch (TiXL CurveEditing.OnConstant semantics): Constant-out on key@1.0
  //    of a 2-key ramp -> sample(1.5) holds the first value instead of interpolating; undo
  //    restores byte-faithful; a same-mode re-apply (before==after) refuses.
  {
    SymbolLibrary lib9 = libFromGraph(defaultParticleGraph());
    AnimTarget t9 = findFloatTarget(lib9);
    const std::string r9 = lib9.rootId;
    CommandStack s9;
    s9.push(std::make_unique<AddAnimationCommand>(lib9, r9, t9.childId, t9.slotId, 1.0, 1.0f));
    s9.push(std::make_unique<AddKeyframeCommand>(lib9, r9, t9.childId, t9.slotId, 0, 2.0));
    // Make the ramp real: key@2.0 value 5 (write-at-playhead).
    s9.push(std::make_unique<WriteKeyAtPlayheadCommand>(lib9, r9, t9.childId, t9.slotId, 0, 2.0, 5.0f));
    const Curve* c9 = &(*lib9.find(r9)->animator.curvesFor(t9.childId, t9.slotId))[0];
    CHK((float)c9->sample(1.5) == 3.0f, "⑨ Linear ramp baseline: sample(1.5) = 3.0");
    const std::string ramp = libToJsonV2(lib9);
    // Switch key@1.0 to Constant on a COPY (TiXL OnConstant: broken=true, OUT only).
    {
      const Animator::CurveArray* arr = lib9.find(r9)->animator.curvesFor(t9.childId, t9.slotId);
      Animator::CurveArray after = *arr;
      VDefinition& k = after[0].table().at(Curve::roundTime(1.0));
      k.brokenTangents = true;
      k.outInterpolation = KeyInterpolation::Constant;
      k.tensionIn = 1.0f; k.tensionOut = 1.0f; k.weighted = false;
      after[0].updateTangents();
      s9.push(std::make_unique<SetCurveGroupSnapshotCommand>(
          lib9, r9, std::vector<CurveGroupEdit>{CurveGroupEdit{t9.childId, t9.slotId, *arr, after}},
          "Set Interpolation"));
    }
    const Curve* c9b = &(*lib9.find(r9)->animator.curvesFor(t9.childId, t9.slotId))[0];
    CHK((float)c9b->sample(1.5) == 1.0f, "⑨ Constant-out: sample(1.5) holds 1.0 (no interpolation)");
    // Re-applying the SAME mode = empty gesture -> refused (undo stack stays clean).
    {
      const Animator::CurveArray* arr = lib9.find(r9)->animator.curvesFor(t9.childId, t9.slotId);
      SetCurveGroupSnapshotCommand noop(
          lib9, r9, std::vector<CurveGroupEdit>{CurveGroupEdit{t9.childId, t9.slotId, *arr, *arr}},
          "Set Interpolation");
      CHK(noop.refused(), "⑨ same-mode re-apply refused (before == after)");
    }
    s9.undo();
    CHK(libToJsonV2(lib9) == ramp, "⑨ interpolation switch undo byte-faithful");
  }

  // ⑩ S6 tangent write (TiXL CurvePoint.HandleTangentDrag semantics): authoring out-tangent
  //    angle on key@1.0 (Tangent mode, slope = tan(angle), curve.cpp slopeFromAngle) bends the
  //    segment -> sample(1.5) departs from the linear midpoint; the FULL-field comparator sees a
  //    tangent-only diff as a real edit (injectBug narrows it back -> FAILS); undo byte-faithful.
  {
    SymbolLibrary libA = libFromGraph(defaultParticleGraph());
    AnimTarget tA = findFloatTarget(libA);
    const std::string rA = libA.rootId;
    CommandStack sA;
    sA.push(std::make_unique<AddAnimationCommand>(libA, rA, tA.childId, tA.slotId, 1.0, 1.0f));
    sA.push(std::make_unique<AddKeyframeCommand>(libA, rA, tA.childId, tA.slotId, 0, 2.0));
    sA.push(std::make_unique<WriteKeyAtPlayheadCommand>(libA, rA, tA.childId, tA.slotId, 0, 2.0, 5.0f));
    const std::string preTan = libToJsonV2(libA);
    const Animator::CurveArray* arr = libA.find(rA)->animator.curvesFor(tA.childId, tA.slotId);
    const float linearMid = (float)(*arr)[0].sample(1.5);
    // Author a steep out-tangent on key@1.0 (live-write shape: mutate in place, snapshot around).
    Animator::CurveArray before = *arr;
    {
      Animator::CurveArray* live = libA.find(rA)->animator.curvesFor(tA.childId, tA.slotId);
      VDefinition& k = (*live)[0].table().at(Curve::roundTime(1.0));
      k.outInterpolation = KeyInterpolation::Tangent;
      k.outTangentAngle = -1.4;  // slope tan(-1.4) ≈ -5.8: dips hard below the linear ramp
      k.brokenTangents = true;
      (*live)[0].updateTangents();
    }
    Animator::CurveArray after = *libA.find(rA)->animator.curvesFor(tA.childId, tA.slotId);
    bool tangentRegistered = !curveArraysEqual(before, after);
    if (injectBug) {
      // BUG: a narrow comparator (u/value/interp only) would call this tangent-only edit a no-op.
      Animator::CurveArray stripped = after;
      VDefinition& k = stripped[0].table().at(Curve::roundTime(1.0));
      k.outTangentAngle = before[0].table().at(Curve::roundTime(1.0)).outTangentAngle;
      k.outInterpolation = before[0].table().at(Curve::roundTime(1.0)).outInterpolation;
      k.brokenTangents = before[0].table().at(Curve::roundTime(1.0)).brokenTangents;
      stripped[0].updateTangents();
      tangentRegistered = !curveArraysEqual(before, stripped);
    }
    CHK(tangentRegistered,
        "⑩ tangent-only diff registers as a real edit (this fails under injectBug)");
    const float bentMid = (float)(*libA.find(rA)->animator.curvesFor(tA.childId, tA.slotId))[0].sample(1.5);
    CHK(bentMid < linearMid, "⑩ authored out-tangent bends sample(1.5) below the linear midpoint");
    sA.push(std::make_unique<SetCurveGroupSnapshotCommand>(
        libA, rA,
        std::vector<CurveGroupEdit>{CurveGroupEdit{tA.childId, tA.slotId, std::move(before),
                                                   std::move(after)}},
        "Edit Tangents"));
    sA.undo();
    CHK(libToJsonV2(libA) == preTan, "⑩ tangent edit undo byte-faithful");
    sA.redo();
    CHK((float)(*libA.find(rA)->animator.curvesFor(tA.childId, tA.slotId))[0].sample(1.5) == bentMid,
        "⑩ tangent edit redo re-applies the bend");
  }

  // ⑪ 批次9 修1 — AddKeyframeCommand clones the previous key's style (= TiXL InsertNewKeyframe,
  //    TimelineCurveEditor.cs:339-341 / AnimationOperations.cs:22-24). The old hard-wired Linear
  //    nuked an authored Smooth/Tangent key whenever the (snapped) insert time collided with it.
  //    injectBug re-enacts the hard-Linear shape -> the inheritance/survival CHKs FAIL.
  {
    SymbolLibrary libB = libFromGraph(defaultParticleGraph());
    AnimTarget tB = findFloatTarget(libB);
    const std::string rB = libB.rootId;
    CommandStack sB;
    // Seed keys @1=1 / @3=3 / @5=1 so the collision target (@3) is a MIDDLE key — boundary keys'
    // angles are re-mirrored unconditionally by updateTangents (curve.cpp:316-332), middle keys
    // in Tangent mode keep their authored angles.
    sB.push(std::make_unique<AddAnimationCommand>(libB, rB, tB.childId, tB.slotId, 1.0, 1.0f));
    sB.push(std::make_unique<AddKeyframeCommand>(libB, rB, tB.childId, tB.slotId, 0, 3.0));
    sB.push(std::make_unique<WriteKeyAtPlayheadCommand>(libB, rB, tB.childId, tB.slotId, 0, 3.0, 3.0f));
    sB.push(std::make_unique<AddKeyframeCommand>(libB, rB, tB.childId, tB.slotId, 0, 5.0));
    sB.push(std::make_unique<WriteKeyAtPlayheadCommand>(libB, rB, tB.childId, tB.slotId, 0, 5.0, 1.0f));
    Animator::CurveArray* live = libB.find(rB)->animator.curvesFor(tB.childId, tB.slotId);
    // Author the FIRST key: broken Tangent both sides (the handle-drag end state). Its OUT angle
    // is preserved by updateTangents (Tangent mode); its IN settles to out-π (boundary law).
    {
      VDefinition& k1 = (*live)[0].table().at(Curve::roundTime(1.0));
      k1.inInterpolation = KeyInterpolation::Tangent;
      k1.outInterpolation = KeyInterpolation::Tangent;
      k1.inTangentAngle = -1.234;
      k1.outTangentAngle = 0.777;
      k1.brokenTangents = true;
      (*live)[0].updateTangents();
    }
    const VDefinition authored = (*live)[0].table().at(Curve::roundTime(1.0));  // settled clone source
    auto hardLinear = [&](double u) {  // the 修1 bug shape, re-enacted on the inserted key
      VDefinition& k = (*live)[0].table().at(Curve::roundTime(u));
      k.inInterpolation = KeyInterpolation::Linear;
      k.outInterpolation = KeyInterpolation::Linear;
      k.brokenTangents = true;
      k.inTangentAngle = 0.0; k.outTangentAngle = 0.0;
    };
    // (a) collision: insert exactly ON key@3 (= a snapped double-click landing on it). The clone
    //     of the previous (authored) key keeps the curve authored: style/angles ride in, value
    //     stays the key's own (sample at its own time). Middle key + Tangent mode -> the cloned
    //     angles survive the insert's updateTangents.
    const std::string preCollision = libToJsonV2(libB);
    sB.push(std::make_unique<AddKeyframeCommand>(libB, rB, tB.childId, tB.slotId, 0, 3.0));
    if (injectBug) hardLinear(3.0);
    {
      const VDefinition& k3 = (*live)[0].table().at(Curve::roundTime(3.0));
      CHK(k3.inInterpolation == KeyInterpolation::Tangent &&
              k3.outInterpolation == KeyInterpolation::Tangent && k3.brokenTangents &&
              std::fabs(k3.inTangentAngle - authored.inTangentAngle) < 1e-9 &&
              std::fabs(k3.outTangentAngle - authored.outTangentAngle) < 1e-9,
          "⑪ collision insert: authored tangent style/angles survive (clone-previous)");
      CHK((float)k3.value == 3.0f, "⑪ collision insert: value preserved (sample at own time)");
    }
    sB.undo();
    CHK(libToJsonV2(libB) == preCollision, "⑪ collision insert undo byte-faithful");
    // (b) inheritance: previous key Smooth -> the fresh key inherits Smooth (not Linear).
    {
      VDefinition& k1 = (*live)[0].table().at(Curve::roundTime(1.0));
      k1.inInterpolation = KeyInterpolation::Smooth;
      k1.outInterpolation = KeyInterpolation::Smooth;
      k1.brokenTangents = false;
      (*live)[0].updateTangents();
    }
    sB.push(std::make_unique<AddKeyframeCommand>(libB, rB, tB.childId, tB.slotId, 0, 2.0));
    if (injectBug) hardLinear(2.0);
    {
      const VDefinition& k2 = (*live)[0].table().at(Curve::roundTime(2.0));
      CHK(k2.inInterpolation == KeyInterpolation::Smooth &&
              k2.outInterpolation == KeyInterpolation::Smooth && !k2.brokenTangents,
          "⑪ fresh insert inherits the previous key's Smooth (no hard-wired Linear)");
    }
    // (c) FORK 具名 "clone-next when no previous": inserting in FRONT of the first key inherits
    //     the run's style from the next key (TiXL falls back to plain Linear here).
    sB.push(std::make_unique<AddKeyframeCommand>(libB, rB, tB.childId, tB.slotId, 0, 0.5));
    {
      const VDefinition& k0 = (*live)[0].table().at(Curve::roundTime(0.5));
      CHK(k0.inInterpolation == KeyInterpolation::Smooth &&
              k0.outInterpolation == KeyInterpolation::Smooth,
          "⑪ insert before the first key clones the NEXT key's style (fork 具名)");
    }
  }

  return failures;
}

}  // namespace sw
