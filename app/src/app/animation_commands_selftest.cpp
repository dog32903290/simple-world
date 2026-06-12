// app/animation_commands_selftest — S3 GUI 動畫命令隔離自測（--selftest-animgui），
// split mechanically out of animation_commands.cpp (ARCHITECTURE rule 4). Tests the command
// layer through its public header only; the resident projection驗 driver 翻轉.
#include "app/animation_commands.h"

#include <cstdio>
#include <memory>
#include <vector>

#include "app/command.h"
#include "runtime/compound_save.h"        // libToJsonV2 (byte-faithful undo 比對)
#include "runtime/graph.h"                // defaultParticleGraph (selftest seed)
#include "runtime/graph_bridge.h"         // libFromGraph (selftest seed)
#include "runtime/resident_eval_graph.h"  // buildResidentEvalGraph + sampleAutomation (driver 翻轉驗)

namespace sw {
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

  // ⑧⑨⑩ S6 legs (group snapshot / interpolation switch / tangent write) live in
  // animation_commands_selftest_s6.cpp (mechanical split, ARCHITECTURE rule 4).
  failures += runAnimGuiS6Legs(injectBug);

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
