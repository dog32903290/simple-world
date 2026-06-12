// app/animation_commands — S3 GUI 動畫命令實作 + 隔離自測（--selftest-animgui）。
#include "app/animation_commands.h"

#include <cstdio>
#include <memory>
#include <utility>

#include "app/command.h"
#include "runtime/compound_save.h"      // libToJsonV2 (byte-faithful undo 比對)
#include "runtime/graph.h"              // defaultParticleGraph (selftest seed)
#include "runtime/graph_bridge.h"       // libFromGraph (selftest seed)
#include "runtime/resident_eval_graph.h"  // buildResidentEvalGraph + sampleAutomation (driver 翻轉驗)

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

// =================== Self-test (against the LIB; resident projection驗 driver 翻轉) ===================
namespace {

// Find the first atomic child of the root with a Float input; returns its (childId, slotId, def).
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

// Is (childId, slotId) projected as an Automation driver in the resident graph built from `lib`?
bool residentDriverIsAutomation(SymbolLibrary& lib, int childId, const std::string& slotId) {
  ResidentEvalGraph g = buildEvalGraph(lib, lib.rootId);
  std::string path = std::to_string(childId);  // root scope: prefix "" + childId
  for (const ResidentNode& n : g.nodes) {
    if (n.path != path) continue;
    for (const ResidentInput& in : n.inputs)
      if (in.slotId == slotId)
        return in.driver == ResidentInput::Driver::Automation;
  }
  return false;
}

#define CHK(cond, msg)                                                       \
  do {                                                                       \
    if (!(cond)) { printf("  [animgui] FAIL %s\n", msg); ++failures; }       \
    else printf("  [animgui] ok   %s\n", msg);                              \
  } while (0)

}  // namespace

int runAnimGuiSelfTest(bool injectBug) {
  int failures = 0;
  SymbolLibrary lib = libFromGraph(defaultParticleGraph());
  AnimTarget tgt = findFloatTarget(lib);
  if (!tgt.ok) { printf("[selftest-animgui] no Float input target -> FAIL\n"); return 1; }
  const std::string rootId = lib.rootId;
  CommandStack stack;

  // ① Animate: curve生 + 首 key=當前值 + driver 翻 Automation + undo 全還原.
  const float startVal = tgt.def;  // current resolved value (no override -> def)
  {
    auto cmd = std::make_unique<AddAnimationCommand>(lib, rootId, tgt.childId, tgt.slotId,
                                                     0.0, startVal);
    AddAnimationCommand* raw = cmd.get();
    raw->doIt();  // run directly first to check refused() before push (mirrors UI flow)
    CHK(!raw->refused(), "Animate not refused on a fresh Float input");
    Symbol* root = lib.find(rootId);
    CHK(root && root->animator.isAnimated(tgt.childId, tgt.slotId), "① curve created on Animator");
    const Animator::CurveArray* arr = root->animator.curvesFor(tgt.childId, tgt.slotId);
    CHK(arr && arr->size() == 1 && (*arr)[0].count() == 1, "① one channel, one first key");
    CHK(arr && (*arr)[0].count() == 1 &&
            (float)(*arr)[0].sample(0.0) == startVal, "① first key value = current value");
    CHK(residentDriverIsAutomation(lib, tgt.childId, tgt.slotId),
        "① resident driver flipped to Automation (建曲線即翻 driver, 同一處)");
    // Undo restores Constant + no zombie curve.
    raw->undo();
    CHK(!(lib.find(rootId)->animator.isAnimated(tgt.childId, tgt.slotId)),
        "① undo: curve gone (no zombie)");
    CHK(!residentDriverIsAutomation(lib, tgt.childId, tgt.slotId),
        "① undo: resident driver back to Constant");
    // Redo via the real stack (push runs doIt) — confirms byte-faithful re-create.
    stack.push(std::move(cmd));
    CHK(lib.find(rootId)->animator.isAnimated(tgt.childId, tgt.slotId),
        "① redo (push): curve restored byte-faithful");
  }
  // Now animated via the stack.
  const std::string jsonAnimated = libToJsonV2(lib);

  // ② Remove Animation 反向 + undo byte-faithful.
  {
    auto cmd = std::make_unique<RemoveAnimationCommand>(lib, rootId, tgt.childId, tgt.slotId);
    stack.push(std::move(cmd));
    CHK(!lib.find(rootId)->animator.isAnimated(tgt.childId, tgt.slotId),
        "② Remove Animation: curve removed");
    CHK(!residentDriverIsAutomation(lib, tgt.childId, tgt.slotId),
        "② driver back to Constant after remove");
    stack.undo();
    CHK(libToJsonV2(lib) == jsonAnimated, "② undo Remove Animation: byte-faithful restore");
  }

  // ③ add/move/delete key 各自 undo byte-faithful (libToJsonV2 比對).
  {
    const std::string before = libToJsonV2(lib);
    auto add = std::make_unique<AddKeyframeCommand>(lib, rootId, tgt.childId, tgt.slotId, 0, 1.0);
    stack.push(std::move(add));
    CHK(libToJsonV2(lib) != before, "③ AddKeyframe mutated the lib");
    stack.undo();
    CHK(libToJsonV2(lib) == before, "③ AddKeyframe undo byte-faithful");

    // Add two keys so we have something to move, snapshot, then move + undo.
    stack.push(std::make_unique<AddKeyframeCommand>(lib, rootId, tgt.childId, tgt.slotId, 0, 1.0));
    const std::string twoKeys = libToJsonV2(lib);
    stack.push(std::make_unique<MoveKeyframeCommand>(lib, rootId, tgt.childId, tgt.slotId, 0,
                                                     1.0, 1.5, 3.25f));
    {
      const Curve* c = &(*lib.find(rootId)->animator.curvesFor(tgt.childId, tgt.slotId))[0];
      CHK(c->hasKeyAt(Curve::roundTime(1.5)) && !c->hasKeyAt(Curve::roundTime(1.0)),
          "③ MoveKeyframe moved key 1.0 -> 1.5");
    }
    stack.undo();
    CHK(libToJsonV2(lib) == twoKeys, "③ MoveKeyframe undo byte-faithful");

    stack.push(std::make_unique<DeleteKeyframeCommand>(lib, rootId, tgt.childId, tgt.slotId, 0, 1.0));
    {
      const Curve* c = &(*lib.find(rootId)->animator.curvesFor(tgt.childId, tgt.slotId))[0];
      CHK(!c->hasKeyAt(Curve::roundTime(1.0)), "③ DeleteKeyframe removed key@1.0");
    }
    stack.undo();
    CHK(libToJsonV2(lib) == twoKeys, "③ DeleteKeyframe undo byte-faithful");
  }

  // ④ 動已動畫參數 = 播放頭寫 key: transport.position=2.0, write value -> key@2.0 appears/updates.
  {
    const float written = 7.5f;
    stack.push(std::make_unique<WriteKeyAtPlayheadCommand>(lib, rootId, tgt.childId, tgt.slotId,
                                                           0, 2.0, written));
    const Curve* c = &(*lib.find(rootId)->animator.curvesFor(tgt.childId, tgt.slotId))[0];
    CHK(c->hasKeyAt(Curve::roundTime(2.0)), "④ playhead write created key@2.0");
    CHK((float)c->sample(2.0) == written, "④ key@2.0 holds the written value");
    // Write again at the SAME playhead -> updates, not duplicates.
    const size_t n = c->count();
    stack.push(std::make_unique<WriteKeyAtPlayheadCommand>(lib, rootId, tgt.childId, tgt.slotId,
                                                           0, 2.0, 9.0f));
    const Curve* c2 = &(*lib.find(rootId)->animator.curvesFor(tgt.childId, tgt.slotId))[0];
    CHK(c2->count() == n, "④ re-write @2.0 updates (no duplicate key)");
    CHK((float)c2->sample(2.0) == 9.0f, "④ re-write @2.0 updated the value");
    stack.undo();
    CHK((float)(*lib.find(rootId)->animator.curvesFor(tgt.childId, tgt.slotId))[0].sample(2.0)
            == written, "④ undo re-write restores prior value");
  }

  // ⑤ savev2 全鏈: animate -> serialize -> load -> 曲線 + driver 都在.
  {
    SymbolLibrary fresh = libFromGraph(defaultParticleGraph());
    AnimTarget t2 = findFloatTarget(fresh);
    CommandStack st2;
    st2.push(std::make_unique<AddAnimationCommand>(fresh, fresh.rootId, t2.childId, t2.slotId,
                                                   0.0, 4.5f));
    const std::string saved = libToJsonV2(fresh);
    SymbolLibrary loaded;
    bool ok = libFromJsonAny(saved, loaded);
    CHK(ok, "⑤ savev2 -> load roundtrip parsed");
    const Symbol* lroot = loaded.find(loaded.rootId);
    CHK(lroot && lroot->animator.isAnimated(t2.childId, t2.slotId),
        "⑤ curve survived save+load");
    CHK(residentDriverIsAutomation(loaded, t2.childId, t2.slotId),
        "⑤ driver still Automation after load (resident 重投影)");
  }

  // ⑥ BUG-B (timeline deferred mutation): on a MULTI-key curve, moving ONE key must leave the
  //    OTHER keys untouched and undo byte-faithful. The timeline_window loop iterates curve.table()
  //    while choosing the move; if the command ran mid-iteration it would erase the node being walked
  //    (UAF). The data-layer invariant we can assert headless: a move on a 3-key curve preserves the
  //    two neighbours exactly + count stays 3. injectBug corrupts a neighbour (mirrors a botched
  //    in-iteration erase) -> the assertion FAILS (teeth).
  {
    SymbolLibrary lib6 = libFromGraph(defaultParticleGraph());
    AnimTarget t6 = findFloatTarget(lib6);
    const std::string r6 = lib6.rootId;
    CommandStack s6;
    // Build a clean 3-key curve via the command layer (keys @ 1.0 / 2.0 / 3.0).
    s6.push(std::make_unique<AddAnimationCommand>(lib6, r6, t6.childId, t6.slotId, 1.0, 1.0f));
    s6.push(std::make_unique<AddKeyframeCommand>(lib6, r6, t6.childId, t6.slotId, 0, 2.0));
    s6.push(std::make_unique<AddKeyframeCommand>(lib6, r6, t6.childId, t6.slotId, 0, 3.0));
    const Curve* c0 = &(*lib6.find(r6)->animator.curvesFor(t6.childId, t6.slotId))[0];
    CHK(c0->count() == 3, "⑥ seeded 3-key curve (1.0/2.0/3.0)");
    const double v1 = c0->sample(1.0), v3 = c0->sample(3.0);
    const std::string threeKeys = libToJsonV2(lib6);

    // Simulate timeline's gesture: copy the iterated (time) list FIRST, then move the middle key.
    std::vector<double> times;
    for (const auto& [u, vdef] : c0->table()) { (void)vdef; times.push_back(u); }
    CHK(times.size() == 3, "⑥ iterated 3 key times into a copy (deferred-list shape)");
    // Deferred move of the MIDDLE key 2.0 -> 2.5 (executed after the 'iteration' above, like the UI).
    s6.push(std::make_unique<MoveKeyframeCommand>(lib6, r6, t6.childId, t6.slotId, 0, 2.0, 2.5, 5.0f));
    const Curve* c1 = &(*lib6.find(r6)->animator.curvesFor(t6.childId, t6.slotId))[0];
    bool neighboursOk = c1->count() == 3 && c1->hasKeyAt(Curve::roundTime(2.5)) &&
                        !c1->hasKeyAt(Curve::roundTime(2.0)) &&
                        (float)c1->sample(1.0) == (float)v1 && (float)c1->sample(3.0) == (float)v3;
    if (injectBug) {
      // BUG: a botched in-iteration erase drops the 3.0 neighbour while moving the middle key.
      lib6.find(r6)->animator.curvesFor(t6.childId, t6.slotId)->at(0).removeAt(3.0);
      const Curve* cb = &(*lib6.find(r6)->animator.curvesFor(t6.childId, t6.slotId))[0];
      neighboursOk = cb->count() == 3 && cb->hasKeyAt(Curve::roundTime(3.0));
    }
    CHK(neighboursOk, "⑥ deferred move keeps both neighbours intact (this fails under injectBug)");
    s6.undo();
    CHK(libToJsonV2(lib6) == threeKeys, "⑥ deferred move undo byte-faithful (3 keys restored)");
  }

  // ⑦ BUG-A (Inspector live-write 收尾, SetCurveSnapshotCommand): a slider drag on an animated input
  //    writes keys @ the playhead DURING the drag (so sample(playhead) accumulates), then on release
  //    pushes one before/after snapshot command. Headless proof: snapshot before, mutate the curve
  //    like the live-write does (addOrUpdate @ playhead), push SetCurveSnapshot(before, after) ->
  //    sample(playhead) holds the final value + undo restores before byte-faithful. A no-op (before
  //    == after) refuses. injectBug pushes a stale 'after' (== before) and asserts the playhead value
  //    changed anyway -> FAILS (teeth: an empty snapshot must NOT look like a real edit).
  {
    SymbolLibrary lib7 = libFromGraph(defaultParticleGraph());
    AnimTarget t7 = findFloatTarget(lib7);
    const std::string r7 = lib7.rootId;
    CommandStack s7;
    s7.push(std::make_unique<AddAnimationCommand>(lib7, r7, t7.childId, t7.slotId, 0.0, 1.0f));
    const std::string before = libToJsonV2(lib7);
    Animator::CurveArray snapBefore = *lib7.find(r7)->animator.curvesFor(t7.childId, t7.slotId);

    // Live-write: drag at playhead=2.0, the value sweeps 1->...->6 (only the final write survives @2.0).
    const double playhead = 2.0;
    Animator::CurveArray* live = lib7.find(r7)->animator.curvesFor(t7.childId, t7.slotId);
    for (float frame : {3.0f, 4.5f, 6.0f}) {
      VDefinition k;
      k.value = frame; k.u = Curve::roundTime(playhead);
      k.inInterpolation = KeyInterpolation::Linear; k.outInterpolation = KeyInterpolation::Linear;
      k.brokenTangents = true;
      (*live)[0].addOrUpdate(playhead, k);
    }
    Animator::CurveArray snapAfter = *lib7.find(r7)->animator.curvesFor(t7.childId, t7.slotId);
    CHK((float)(*lib7.find(r7)->animator.curvesFor(t7.childId, t7.slotId))[0].sample(playhead) == 6.0f,
        "⑦ live-write accumulated to the final dragged value @ playhead");

    // Reset to before, then drive the WHOLE thing through the command (push runs doIt = re-apply after).
    lib7.find(r7)->animator.setCurves(t7.childId, t7.slotId, snapBefore);
    auto cmd = std::make_unique<SetCurveSnapshotCommand>(lib7, r7, t7.childId, t7.slotId,
                                                         snapBefore, snapAfter);
    CHK(!cmd->refused(), "⑦ SetCurveSnapshot not refused (before != after)");
    s7.push(std::move(cmd));
    CHK((float)(*lib7.find(r7)->animator.curvesFor(t7.childId, t7.slotId))[0].sample(playhead) == 6.0f,
        "⑦ command applied the post-drag snapshot");
    s7.undo();
    CHK(libToJsonV2(lib7) == before, "⑦ SetCurveSnapshot undo byte-faithful (pre-drag restored)");

    // A no-op drag (released at the same value) refuses -> no empty undo step.
    SetCurveSnapshotCommand noop(lib7, r7, t7.childId, t7.slotId, snapBefore, snapBefore);
    bool refusedOk = noop.refused();
    if (injectBug) refusedOk = !noop.refused();  // BUG: treat an empty snapshot as a real edit.
    CHK(refusedOk, "⑦ empty snapshot (before == after) refuses (this fails under injectBug)");
  }

  // teeth: injectBug leaves a zombie curve after the Animate undo.
  if (injectBug) {
    SymbolLibrary lib2 = libFromGraph(defaultParticleGraph());
    AnimTarget t3 = findFloatTarget(lib2);
    AddAnimationCommand cmd(lib2, lib2.rootId, t3.childId, t3.slotId, 0.0, t3.def);
    cmd.doIt();
    // BUG: skip undo's curve removal -> the curve stays (zombie).
    bool zombie = lib2.find(lib2.rootId)->animator.isAnimated(t3.childId, t3.slotId);
    CHK(!zombie, "BUG-leg: a zombie curve must NOT survive (this fails under injectBug)");
  }

  printf("[selftest] animgui -> %s (%d failures)\n", failures ? "FAIL" : "PASS", failures);
  return failures ? 1 : 0;
}

}  // namespace sw
