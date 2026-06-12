// app/animation_commands — S3 GUI 動畫命令實作。隔離自測（--selftest-animgui）lives in
// animation_commands_selftest.cpp (mechanical split, ARCHITECTURE rule 4).
#include "app/animation_commands.h"

#include <utility>

namespace sw {
namespace {

Symbol* sym(SymbolLibrary& lib, const std::string& id) { return lib.find(id); }

// The curve array for (childId, slotId) on `symbolId`'s definition Animator, or nullptr.
Animator::CurveArray* curvesArr(SymbolLibrary& lib, const std::string& symbolId, int childId,
                                const std::string& slotId) {
  Symbol* s = sym(lib, symbolId);
  return s ? s->animator.curvesFor(childId, slotId) : nullptr;
}

// One curve at `index` of (childId, slotId), or nullptr (no animation / index out of range).
Curve* curveAt(SymbolLibrary& lib, const std::string& symbolId, int childId,
               const std::string& slotId, int index) {
  Animator::CurveArray* arr = curvesArr(lib, symbolId, childId, slotId);
  if (!arr || index < 0 || index >= (int)arr->size()) return nullptr;
  return &(*arr)[index];
}

// Snapshot the VDefinition at (rounded) time `t` in `c`, returning whether one existed.
bool keyAt(const Curve& c, double t, VDefinition& out) {
  double rt = Curve::roundTime(t);
  auto it = c.table().find(rt);
  if (it == c.table().end()) return false;
  out = it->second;
  return true;
}

}  // namespace

// --- AddAnimationCommand ---
// refused decided in the ctor (pure read) so the caller can check it BEFORE push (= TiXL
// RenameSymbol precedent): missing symbol or already-animated input.
AddAnimationCommand::AddAnimationCommand(SymbolLibrary& lib, std::string symbolId, int childId,
                                         std::string slotId, double time, float currentValue)
    : lib_(lib), symbolId_(std::move(symbolId)), childId_(childId), slotId_(std::move(slotId)),
      time_(time), value_(currentValue) {
  Symbol* s = sym(lib_, symbolId_);
  if (!s || s->animator.isAnimated(childId_, slotId_)) refused_ = true;
}
void AddAnimationCommand::doIt() {
  if (refused_) return;
  Symbol* s = sym(lib_, symbolId_);
  if (!s) { refused_ = true; return; }
  if (kept_.empty()) {
    s->animator.animateFloat(childId_, slotId_, time_, value_);
  } else {
    // Redo: restore the exact curve array we created (byte-faithful, = TiXL _keepCurves).
    s->animator.setCurves(childId_, slotId_, kept_);
  }
  // Snapshot for redo (only needs to happen once; the array is identical after restore).
  if (const Animator::CurveArray* arr = s->animator.curvesFor(childId_, slotId_)) kept_ = *arr;
}
void AddAnimationCommand::undo() {
  if (refused_) return;
  if (Symbol* s = sym(lib_, symbolId_)) s->animator.remove(childId_, slotId_);
}

// --- RemoveAnimationCommand ---
RemoveAnimationCommand::RemoveAnimationCommand(SymbolLibrary& lib, std::string symbolId, int childId,
                                               std::string slotId)
    : lib_(lib), symbolId_(std::move(symbolId)), childId_(childId), slotId_(std::move(slotId)) {
  Symbol* s = sym(lib_, symbolId_);
  if (!s || !s->animator.isAnimated(childId_, slotId_)) refused_ = true;
}
void RemoveAnimationCommand::doIt() {
  if (refused_) return;
  Symbol* s = sym(lib_, symbolId_);
  if (!s) { refused_ = true; return; }
  if (const Animator::CurveArray* arr = s->animator.curvesFor(childId_, slotId_)) kept_ = *arr;
  s->animator.remove(childId_, slotId_);
}
void RemoveAnimationCommand::undo() {
  if (refused_) return;
  if (Symbol* s = sym(lib_, symbolId_)) s->animator.setCurves(childId_, slotId_, kept_);
}

// --- AddKeyframeCommand ---
Curve* AddKeyframeCommand::resolve() {
  return curveAt(lib_, symbolId_, childId_, slotId_, index_);
}
void AddKeyframeCommand::doIt() {
  Curve* c = resolve();
  if (!c) { refused_ = true; return; }
  before_ = *c;  // whole-curve snapshot -> byte-faithful undo (see header rationale)
  // = TiXL InsertKeyframeToCurves: the new key lands ON the curve (sampled value), Linear.
  VDefinition k;
  k.value = c->sample(time_);
  k.u = Curve::roundTime(time_);
  k.inInterpolation = KeyInterpolation::Linear;
  k.outInterpolation = KeyInterpolation::Linear;
  k.brokenTangents = true;
  c->addOrUpdate(time_, k);
}
void AddKeyframeCommand::undo() {
  if (refused_) return;
  if (Curve* c = resolve()) *c = before_;
}

// --- MoveKeyframeCommand ---
Curve* MoveKeyframeCommand::resolve() {
  return curveAt(lib_, symbolId_, childId_, slotId_, index_);
}
void MoveKeyframeCommand::doIt() {
  Curve* c = resolve();
  if (!c) { refused_ = true; return; }
  VDefinition moved;
  if (!keyAt(*c, oldTime_, moved)) { refused_ = true; return; }  // no key to move
  before_ = *c;
  double rNew = Curve::roundTime(newTime_), rOld = Curve::roundTime(oldTime_);
  if (rNew != rOld) c->removeAt(oldTime_);  // moving away from the old slot (clobber-on-collision)
  moved.value = newValue_;
  moved.u = rNew;
  c->addOrUpdate(newTime_, moved);  // AddOrUpdateV: overwrites any key already at newTime
}
void MoveKeyframeCommand::undo() {
  if (refused_) return;
  if (Curve* c = resolve()) *c = before_;
}

// --- DeleteKeyframeCommand ---
Curve* DeleteKeyframeCommand::resolve() {
  return curveAt(lib_, symbolId_, childId_, slotId_, index_);
}
void DeleteKeyframeCommand::doIt() {
  Curve* c = resolve();
  if (!c || !c->hasKeyAt(Curve::roundTime(time_))) { refused_ = true; return; }
  before_ = *c;
  c->removeAt(time_);
}
void DeleteKeyframeCommand::undo() {
  if (refused_) return;
  if (Curve* c = resolve()) *c = before_;
}

// --- WriteKeyAtPlayheadCommand (P1) ---
Curve* WriteKeyAtPlayheadCommand::resolve() {
  return curveAt(lib_, symbolId_, childId_, slotId_, index_);
}
void WriteKeyAtPlayheadCommand::doIt() {
  Curve* c = resolve();
  if (!c) { refused_ = true; return; }  // input not animated -> P1 doesn't apply
  before_ = *c;
  VDefinition oldKey;
  VDefinition k;
  if (keyAt(*c, time_, oldKey)) {
    k = oldKey;           // update the existing key's value, keep its interpolation/tangents
    k.value = value_;
  } else {
    k.value = value_;     // insert a fresh Linear key at the playhead
    k.u = Curve::roundTime(time_);
    k.inInterpolation = KeyInterpolation::Linear;
    k.outInterpolation = KeyInterpolation::Linear;
    k.brokenTangents = true;
  }
  c->addOrUpdate(time_, k);
}
void WriteKeyAtPlayheadCommand::undo() {
  if (refused_) return;
  if (Curve* c = resolve()) *c = before_;
}

// --- SetCurveSnapshotCommand (P1 live-write 收尾) ---
namespace {
// Two curve arrays are equal for undo purposes iff same shape + every key's (u, value, interp) match.
// Tangent angles are derived from those, so this is a faithful "did the drag change anything?" test.
bool curveArraysEqual(const Animator::CurveArray& a, const Animator::CurveArray& b) {
  if (a.size() != b.size()) return false;
  for (size_t i = 0; i < a.size(); ++i) {
    const std::map<double, VDefinition>& ta = a[i].table();
    const std::map<double, VDefinition>& tb = b[i].table();
    if (ta.size() != tb.size()) return false;
    auto ia = ta.begin(), ib = tb.begin();
    for (; ia != ta.end(); ++ia, ++ib) {
      if (ia->first != ib->first) return false;
      const VDefinition& va = ia->second;
      const VDefinition& vb = ib->second;
      if (va.value != vb.value || va.inInterpolation != vb.inInterpolation ||
          va.outInterpolation != vb.outInterpolation)
        return false;
    }
  }
  return true;
}
}  // namespace
SetCurveSnapshotCommand::SetCurveSnapshotCommand(SymbolLibrary& lib, std::string symbolId,
                                                 int childId, std::string slotId,
                                                 Animator::CurveArray before,
                                                 Animator::CurveArray after)
    : lib_(lib), symbolId_(std::move(symbolId)), childId_(childId), slotId_(std::move(slotId)),
      before_(std::move(before)), after_(std::move(after)) {
  // No-op drag (click without moving the value): the slot is unchanged -> refuse so the caller
  // doesn't pollute the undo stack with an empty step (= the free-slider erase-residue precedent).
  if (curveArraysEqual(before_, after_)) refused_ = true;
}
// doIt/undo install whichever array. The whole-array setCurves keeps the resident driver's curveRef
// resolution intact (same store), and bumpLibRevision rides on the CommandStack push.
void SetCurveSnapshotCommand::doIt() {
  if (refused_) return;
  Symbol* s = sym(lib_, symbolId_);
  if (!s) { refused_ = true; return; }
  s->animator.setCurves(childId_, slotId_, after_);
}
void SetCurveSnapshotCommand::undo() {
  if (refused_) return;
  if (Symbol* s = sym(lib_, symbolId_)) s->animator.setCurves(childId_, slotId_, before_);
}

}  // namespace sw
