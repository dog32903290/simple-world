// runtime/curve_animator_selftest — S3 automation END-TO-END. Four legs the spec names:
//   ① resident node input on an Automation driver reads the curve @ localTime (playhead 走曲線)
//   ② Constant<->Automation toggle flips the cache LIVE<->STATIC in lockstep (cache-count proof)
//   ③ a definition-layer curve edit changes EVERY instance (reuse broadcast — same as override/S13)
//   ④ savev2 roundtrip carries the animator bit-stable + a tampered animator entry drops (S15)
// injectBug breaks one expectation -> FAIL (teeth).
#include "runtime/curve_animator.h"

#include <cstdio>
#include <string>

#include "runtime/compound_save.h"
#include "runtime/resident_eval_graph.h"

namespace sw {
namespace {

int g_fail = 0;
void expect(const char* what, bool ok) {
  if (!ok) { ++g_fail; printf("  [animator] FAIL %s\n", what); }
  else printf("  [animator] ok   %s\n", what);
}
void expectNear(const char* what, float got, float want) {
  bool ok = std::abs(got - want) <= 1e-4f;
  if (!ok) { ++g_fail; printf("  [animator] FAIL %s got=%.5f want=%.5f\n", what, got, want); }
  else printf("  [animator] ok   %s = %.5f\n", what, got);
}

Symbol atomic(const char* id, std::vector<SlotDef> ins, std::vector<SlotDef> outs) {
  Symbol s; s.id = id; s.name = id; s.atomic = true;
  s.inputDefs = std::move(ins); s.outputDefs = std::move(outs);
  return s;
}

// Count resident output-cache entries flagged isLiveSource across the whole graph (the LIVE/STATIC
// proof — same mechanism the cache selftest leans on).
int countLive(const ResidentEvalGraph& g) {
  int n = 0;
  for (const ResidentNode& node : g.nodes)
    for (const auto& kv : node.outCache)
      if (kv.second.isLiveSource) ++n;
  return n;
}

// A library: root "S" with TWO Const children (reuse的对照不必，但用 Const#1 当 animated 主角，
// Const#2 当 STATIC 对照) wired into a Multiply -> output. Const{value->out}, Multiply{a,b->out}.
SymbolLibrary makeLib() {
  SymbolLibrary sl;
  sl.symbols["Const"] = atomic("Const", {{"value", "value", "Float", 0.0f}},
                               {{"out", "out", "Float", 0.0f}});
  sl.symbols["Multiply"] = atomic("Multiply", {{"a", "a", "Float", 1.0f}, {"b", "b", "Float", 1.0f}},
                                  {{"out", "out", "Float", 0.0f}});
  Symbol root; root.id = "S"; root.name = "S"; root.atomic = false;
  root.outputDefs = {{"out", "out", "Float", 0.0f}};
  SymbolChild c1; c1.id = 1; c1.symbolId = "Const"; c1.overrides["value"] = 2.0f;
  SymbolChild c2; c2.id = 2; c2.symbolId = "Const"; c2.overrides["value"] = 3.0f;
  SymbolChild cm; cm.id = 3; cm.symbolId = "Multiply";
  root.children = {c1, c2, cm};
  root.connections = {{1, "out", 3, "a"}, {2, "out", 3, "b"}, {3, "out", kSymbolBoundary, "out"}};
  root.nextChildId = 4;
  sl.symbols["S"] = root; sl.rootId = "S";
  return sl;
}

}  // namespace

int runCurveAnimatorSelfTest(bool injectBug) {
  g_fail = 0;
  printf("[selftest] animator (S3 automation end-to-end)\n");

  // ===== leg ②a: STATIC baseline — no animation, no live source (Const+Const+Multiply). =====
  {
    SymbolLibrary sl = makeLib();
    ResidentEvalGraph g = buildEvalGraph(sl, sl.rootId);
    initResidentCache(g);
    expect("STATIC graph has 0 live sources", countLive(g) == 0);
  }

  // ===== leg ①: animate Const#1.value with a ramp curve, sample @ localTime. =====
  // Curve: t=0 -> 0, t=4 -> 8 (linear). @ localTime 1 -> value 2 ; @ 3 -> 6. Multiply by Const#2(3):
  // out @1 -> 2*3 = 6 ; @3 -> 6*3 = 18.
  {
    SymbolLibrary sl = makeLib();
    Symbol& root = sl.symbols["S"];
    Curve ramp;
    VDefinition k0; k0.value = 0.0; k0.inInterpolation = KeyInterpolation::Linear;
    k0.outInterpolation = KeyInterpolation::Linear;
    VDefinition k1; k1.value = 8.0; k1.inInterpolation = KeyInterpolation::Linear;
    k1.outInterpolation = KeyInterpolation::Linear;
    ramp.addOrUpdate(0.0, k0);
    ramp.addOrUpdate(4.0, k1);
    Animator::CurveArray arr; arr.push_back(ramp);
    root.animator.setCurves(1, "value", arr);

    ResidentEvalGraph g = buildEvalGraph(sl, sl.rootId);
    initResidentCache(g);

    // The animated input projects as an Automation driver carrying the curveRef + owning symbol.
    const ResidentNode* c1 = g.node("1");
    const ResidentInput* vi = c1 ? c1->input("value") : nullptr;
    expect("Const#1.value driver == Automation",
           vi && vi->driver == ResidentInput::Driver::Automation);
    expect("Automation curveRef resolves on owning symbol",
           vi && sl.symbols["S"].animator.resolveRef(vi->curveRef) != nullptr);

    ResidentEvalCtx ctx; ctx.lib = &sl;
    auto outP = g.outputs["out"];

    ctx.localTime = 1.0f;
    bumpLiveSources(g);  // playhead moved -> live bump before pulling
    float at1 = pullResidentFloat(g, outP.first, outP.second, ctx);
    expectNear("automated out @localTime=1", at1, 6.0f);

    ctx.localTime = 3.0f;
    bumpLiveSources(g);
    float at3 = pullResidentFloat(g, outP.first, outP.second, ctx);
    expectNear("automated out @localTime=3", at3, 18.0f);

    // leg ②b: an Automation-driven input makes its node a LIVE source (≥1 live). Toggling it back
    // to Constant (un-animate + rebuild) returns to 0 live -> STATIC<->LIVE lockstep.
    expect("Automation graph has >=1 live source", countLive(g) >= 1);

    sl.symbols["S"].animator.remove(1, "value");  // un-animate the definition
    ResidentEvalGraph g2 = buildEvalGraph(sl, sl.rootId);
    initResidentCache(g2);
    const ResidentNode* c1b = g2.node("1");
    const ResidentInput* vib = c1b ? c1b->input("value") : nullptr;
    expect("after un-animate, driver back to Constant",
           vib && vib->driver == ResidentInput::Driver::Constant);
    expect("after un-animate, 0 live sources (STATIC)", countLive(g2) == 0);
  }

  // ===== leg ③: definition-layer reuse broadcast. A compound "Wrap" containing ONE Const child,
  // instantiated TWICE in the root. Animating Wrap's inner Const changes BOTH instances. =====
  {
    SymbolLibrary sl;
    sl.symbols["Const"] = atomic("Const", {{"value", "value", "Float", 0.0f}},
                                 {{"out", "out", "Float", 0.0f}});
    // Wrap: inner Const#1 -> Wrap.out. (Const#1.value animated on Wrap's animator.)
    Symbol wrap; wrap.id = "Wrap"; wrap.name = "Wrap"; wrap.atomic = false;
    wrap.outputDefs = {{"out", "out", "Float", 0.0f}};
    SymbolChild wc; wc.id = 1; wc.symbolId = "Const"; wc.overrides["value"] = 1.0f;
    wrap.children = {wc};
    wrap.connections = {{1, "out", kSymbolBoundary, "out"}};
    wrap.nextChildId = 2;
    // animate inner Const#1.value: constant curve at value 9 (one key).
    Curve flat; VDefinition k; k.value = 9.0;
    k.inInterpolation = KeyInterpolation::Linear; k.outInterpolation = KeyInterpolation::Linear;
    flat.addOrUpdate(0.0, k);
    Animator::CurveArray arr; arr.push_back(flat);
    wrap.animator.setCurves(1, "value", arr);
    sl.symbols["Wrap"] = wrap;
    // root: two Wrap instances (reuse) -> outputs (just view each instance's out).
    Symbol root; root.id = "R"; root.name = "R"; root.atomic = false;
    root.outputDefs = {{"a", "a", "Float", 0.0f}, {"b", "b", "Float", 0.0f}};
    SymbolChild w1; w1.id = 1; w1.symbolId = "Wrap";
    SymbolChild w2; w2.id = 2; w2.symbolId = "Wrap";
    root.children = {w1, w2};
    root.connections = {{1, "out", kSymbolBoundary, "a"}, {2, "out", kSymbolBoundary, "b"}};
    root.nextChildId = 3;
    sl.symbols["R"] = root; sl.rootId = "R";

    ResidentEvalGraph g = buildEvalGraph(sl, sl.rootId);
    initResidentCache(g);
    ResidentEvalCtx ctx; ctx.lib = &sl; ctx.localTime = 0.0f;
    bumpLiveSources(g);
    auto pa = g.outputs["a"]; auto pb = g.outputs["b"];
    float va = pullResidentFloat(g, pa.first, pa.second, ctx);
    float vb = pullResidentFloat(g, pb.first, pb.second, ctx);
    expectNear("reuse instance A reads animated 9", va, 9.0f);
    expectNear("reuse instance B reads animated 9 (same def)", vb, 9.0f);
  }

  // ===== leg ④: savev2 roundtrip carries the animator; reload evaluates identically. =====
  {
    SymbolLibrary sl = makeLib();
    Curve ramp;
    VDefinition k0; k0.value = 0.0; k0.inInterpolation = KeyInterpolation::Linear;
    k0.outInterpolation = KeyInterpolation::Linear;
    VDefinition k1; k1.value = 8.0; k1.inInterpolation = KeyInterpolation::Smooth;
    k1.outInterpolation = KeyInterpolation::Smooth; k1.tensionIn = 1.5f;  // exercise non-default fields
    ramp.addOrUpdate(0.0, k0);
    ramp.addOrUpdate(4.0, k1);
    ramp.postCurveMapping = OutsideBehavior::Cycle;
    Animator::CurveArray arr; arr.push_back(ramp);
    sl.symbols["S"].animator.setCurves(1, "value", arr);

    std::string json1 = libToJsonV2(sl);
    SymbolLibrary rl;
    std::vector<std::string> warns;
    bool loaded = libFromJsonAny(json1, rl, &warns);
    expect("savev2 reload ok", loaded);
    expect("reload has the animator entry", rl.symbols["S"].animator.isAnimated(1, "value"));
    std::string json2 = libToJsonV2(rl);
    expect("savev2 animator roundtrip bit-stable", json1 == json2);

    // evaluation-identical: sample @ localTime 2 pre and post reload through the resident graph.
    auto sampleOut = [](SymbolLibrary& lib, float t) -> float {
      ResidentEvalGraph g = buildEvalGraph(lib, lib.rootId);
      initResidentCache(g);
      ResidentEvalCtx ctx; ctx.lib = &lib; ctx.localTime = t;
      bumpLiveSources(g);
      auto outP = g.outputs["out"];
      return pullResidentFloat(g, outP.first, outP.second, ctx);
    };
    float pre = sampleOut(sl, 2.0f);
    float post = sampleOut(rl, 2.0f);
    expectNear("eval identical pre/post reload", post, pre);

    // S15: a tampered animator entry (animator on a missing child) drops locally, file still loads.
    // Inject by hand-editing the JSON: add an animator entry for child 99 (nonexistent).
    // crude_json dump(2) puts the array '[' on its OWN line after "animator": — find the key, then
    // the next '[', and insert a bogus entry right after that bracket (well-formed JSON, S15 input).
    std::string bad = json1;
    auto keyPos = bad.find("\"animator\":");
    auto pos = keyPos == std::string::npos ? std::string::npos : bad.find('[', keyPos);
    if (pos != std::string::npos) {
      std::string bogus =
          "\n        { \"childId\": 99, \"inputId\": \"value\","
          " \"curve\": { \"preCurve\": 0, \"postCurve\": 0, \"keys\": [] } },";
      bad.insert(pos + 1, bogus);
    }
    SymbolLibrary bl; std::vector<std::string> bwarns;
    bool bok = libFromJsonAny(bad, bl, &bwarns);
    bool droppedWithWarn = false;
    for (const std::string& w : bwarns)
      if (w.find("animator on missing child 99") != std::string::npos) droppedWithWarn = true;
    expect("S15: tampered animator (missing child) loads", bok);
    expect("S15: bogus animator entry dropped + warned", droppedWithWarn);
    expect("S15: the GOOD animator entry survived the tamper",
           bl.symbols["S"].animator.isAnimated(1, "value"));
  }

  // teeth: a deliberately wrong expectation must FAIL under -bug.
  if (injectBug) {
    SymbolLibrary sl = makeLib();
    Curve ramp;
    VDefinition k0; k0.value = 0.0; VDefinition k1; k1.value = 8.0;
    k0.inInterpolation = k0.outInterpolation = KeyInterpolation::Linear;
    k1.inInterpolation = k1.outInterpolation = KeyInterpolation::Linear;
    ramp.addOrUpdate(0.0, k0); ramp.addOrUpdate(4.0, k1);
    Animator::CurveArray arr; arr.push_back(ramp);
    sl.symbols["S"].animator.setCurves(1, "value", arr);
    ResidentEvalGraph g = buildEvalGraph(sl, sl.rootId);
    initResidentCache(g);
    ResidentEvalCtx ctx; ctx.lib = &sl; ctx.localTime = 1.0f;
    bumpLiveSources(g);
    auto outP = g.outputs["out"];
    float v = pullResidentFloat(g, outP.first, outP.second, ctx);
    expectNear("BUG: out @1 should be 6 but asserting 99", v, 99.0f);  // must FAIL
  }

  printf("[selftest] animator -> %s (%d failures)\n", g_fail == 0 ? "PASS" : "FAIL", g_fail);
  return g_fail == 0 ? 0 : 1;
}

}  // namespace sw
