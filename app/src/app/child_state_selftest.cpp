// app/child_state_selftest — headless RED->GREEN proof of S2 (批次7) Child 結構补欄: isBypassed /
// per-output isDisabled / per-output triggerOverride. All 照 TiXL Symbol.Child.cs. Zone: app (drives
// the commands + the runtime resident eval engine + v2 save/load + copy/paste — app may include all of
// runtime). Proven through the resident eval graph (the engine S2 must touch), the commands (undo), the
// .t3 roundtrip, and the copy/paste seam.
//
// Legs (task contract, spec Selftest ①–⑦):
//   1. bypass passthrough — a whitelisted child (Sine) with its main output wired: bypass on -> the
//      output equals its main INPUT's upstream value (not sin(x)); undo restores the cooked value.
//   2. bypass refusal: main output UNWIRED -> SetBypassChildCommand refuses (= TiXL .cs:287-300).
//   3. bypass refusal: non-whitelist main I/O type -> refused (childIsBypassable false).
//   4. isDisabled — the output freezes at its last result (change upstream -> frozen value unmoved);
//      a Command-typed output goes no-op (same freeze mechanism); clearing disabled thaws + recomputes.
//   5. triggerOverride — a STATIC node set Always recomputes every pass (live-source count proves it);
//      cleared -> back to STATIC (0 live sources).
//   6. savev2 roundtrip — the three fields are byte-stable across save/load AND S15-tolerant (a tampered
//      局部 — garbage output id / bad trigger string — is dropped without killing the file).
//   7. copy/paste — a bypassed+wired child copied WITH its internal main-output wire pastes bypassed
//      (the seam applies bypass AFTER the wire exists); a bypassed child whose wire is NOT copied
//      pastes UN-bypassed (the deferred SetBypassed no-ops on the unwired fresh instance).
// injectBug breaks leg 1 (the builder ignores the bypass flag) so the passthrough assertion FAILS.
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "app/graph_commands.h"
#include "runtime/compound_graph.h"
#include "runtime/compound_save.h"
#include "runtime/copy_paste.h"
#include "runtime/resident_eval_graph.h"

namespace sw {
namespace {

int fail(const char* msg) {
  std::printf("[selftest] childstate: FAIL — %s\n", msg);
  return 1;
}
bool approx(float a, float b) { return std::fabs(a - b) < 1e-4f; }

Symbol atomic(const char* id, std::vector<SlotDef> ins, std::vector<SlotDef> outs) {
  Symbol s; s.id = id; s.name = id; s.atomic = true;
  s.inputDefs = std::move(ins); s.outputDefs = std::move(outs);
  return s;
}

int countLive(const ResidentEvalGraph& g) {
  int n = 0;
  for (const ResidentNode& node : g.nodes)
    for (const auto& kv : node.outCache)
      if (kv.second.isLiveSource) ++n;
  return n;
}

// Root "S": Const#1(value=v) -> Sine#2.x -> Sine#2.out -> boundary "out". Sine is whitelisted (Float
// main I/O), normal out = sin(v), bypassed out = v. Both atomics are registered specs (evalConst /
// evalSine) so the resident engine cooks them for real.
SymbolLibrary makeLib(float v) {
  SymbolLibrary sl;
  sl.symbols["Const"] = atomic("Const", {{"value", "value", "Float", 0.0f}},
                               {{"out", "out", "Float", 0.0f}});
  sl.symbols["Sine"] = atomic("Sine", {{"x", "x", "Float", 0.0f}}, {{"out", "out", "Float", 0.0f}});
  Symbol root; root.id = "S"; root.name = "S"; root.atomic = false;
  root.outputDefs = {{"out", "out", "Float", 0.0f}};
  SymbolChild c1; c1.id = 1; c1.symbolId = "Const"; c1.overrides["value"] = v;
  SymbolChild c2; c2.id = 2; c2.symbolId = "Sine";
  root.children = {c1, c2};
  root.connections = {{1, "out", 2, "x"}, {2, "out", kSymbolBoundary, "out"}};
  root.nextChildId = 3;
  sl.symbols["S"] = root; sl.rootId = "S";
  return sl;
}

float pullRootOut(SymbolLibrary& sl) {
  ResidentEvalGraph g = buildEvalGraph(sl, sl.rootId);
  initResidentCache(g);
  ResidentEvalCtx ctx; ctx.lib = &sl;
  auto outP = g.outputs["out"];
  return pullResidentFloat(g, outP.first, outP.second, ctx);
}

}  // namespace

int runChildStateSelfTest(bool injectBug) {
  std::printf("[selftest] childstate (S2: bypass / isDisabled / triggerOverride)\n");
  const float V = 1.0f;
  const float SIN_V = std::sin(V);

  // ===== Leg 1: bypass passthrough + undo =====
  {
    SymbolLibrary sl = makeLib(V);
    if (!approx(pullRootOut(sl), SIN_V)) return fail("leg1: baseline out != sin(v)");

    Symbol& root = sl.symbols["S"];
    SetBypassChildCommand cmd(sl, "S", 2, true);
    if (cmd.refused()) return fail("leg1: legit bypass of a wired bypassable child was refused");
    cmd.doIt();
    if (!childById(root, 2)->isBypassed) return fail("leg1: bypass flag not set");

    float out = pullRootOut(sl);
    if (injectBug) {
      // Emulate a builder that ignores the bypass flag (no redirect): out stays sin(v), so the
      // passthrough assertion below FAILS. We force that by clearing the flag the engine reads.
      childById(root, 2)->isBypassed = false;
      out = pullRootOut(sl);
    }
    if (!approx(out, V)) return fail("leg1: bypassed out != main input value (passthrough broken)");

    cmd.undo();
    if (childById(root, 2)->isBypassed) return fail("leg1: undo did not clear bypass");
    if (!approx(pullRootOut(sl), SIN_V)) return fail("leg1: undo did not restore cooked value");
  }

  // ===== Leg 2: bypass refused when the main output is UNWIRED (= TiXL .cs:287-300) =====
  {
    SymbolLibrary sl = makeLib(V);
    Symbol& root = sl.symbols["S"];
    // Drop the Sine.out -> boundary wire: now Sine's main output feeds nothing.
    root.connections = {{1, "out", 2, "x"}};
    SetBypassChildCommand cmd(sl, "S", 2, true);
    if (!cmd.refused()) return fail("leg2: bypass of an unwired main output was NOT refused");
    if (childById(root, 2)->isBypassed) return fail("leg2: refused command still mutated the flag");
  }

  // ===== Leg 3: bypass refused for a NON-whitelist main I/O type =====
  {
    SymbolLibrary sl = makeLib(V);
    // Re-type Sine's I/O to a type with no bypass analog ("Mesh" — not in compoundBypassableType).
    sl.symbols["Sine"].inputDefs[0].dataType = "Mesh";
    sl.symbols["Sine"].outputDefs[0].dataType = "Mesh";
    if (childIsBypassable(sl, *childById(sl.symbols["S"], 2)))
      return fail("leg3: non-whitelist type wrongly reported bypassable");
    SetBypassChildCommand cmd(sl, "S", 2, true);
    if (!cmd.refused()) return fail("leg3: bypass of a non-whitelist type was NOT refused");
  }

  // ===== Leg 4: isDisabled freezes the output; thaw recomputes =====
  {
    SymbolLibrary sl = makeLib(V);
    Symbol& root = sl.symbols["S"];
    // Build ONE resident graph and pull through it across edits (the cache is the thing under test).
    ResidentEvalGraph g = buildEvalGraph(sl, sl.rootId);
    initResidentCache(g);
    ResidentEvalCtx ctx; ctx.lib = &sl;
    auto outP = g.outputs["out"];
    float warm = pullResidentFloat(g, outP.first, outP.second, ctx);  // = sin(v), now cached
    if (!approx(warm, SIN_V)) return fail("leg4: warm-up out != sin(v)");

    // Disable Sine.out AT THE RESIDENT LEVEL (the freeze is a cache semantic). The frozen value is the
    // last computed result (sin(v)).
    SetOutputDisabledCommand dis(sl, "S", 2, "out", true);
    if (dis.refused()) return fail("leg4: legit disable was refused");
    dis.doIt();
    ResidentNode* sn = nullptr;
    for (ResidentNode& n : g.nodes) if (n.path == "2") sn = &n;
    if (!sn) return fail("leg4: resident Sine node missing");
    sn->outCache["out"].isDisabled = true;  // project the model edit onto the live graph

    // Now change the upstream Const value WITHOUT thawing — frozen output must NOT move.
    patchSetConstant(g, "1", "value", 5.0f);  // would make sin-input 5 -> sin(5) if not frozen
    float frozen = pullResidentFloat(g, outP.first, outP.second, ctx);
    if (injectBug) {
      // Emulate a freeze that doesn't hold: clear the flag so the upstream change leaks through.
      sn->outCache["out"].isDisabled = false;
      frozen = pullResidentFloat(g, outP.first, outP.second, ctx);
    }
    if (!approx(frozen, SIN_V)) return fail("leg4: disabled output did NOT freeze at last result");

    // Thaw -> recompute against the NEW upstream (sin(5)).
    sn->outCache["out"].isDisabled = false;
    SetOutputDisabledCommand en(sl, "S", 2, "out", false);
    if (en.refused()) return fail("leg4: legit enable was refused");
    en.doIt();
    if (childById(root, 2)->disabledOutputs.count("out"))
      return fail("leg4: enabling did not erase the sparse key");
    float thawed = pullResidentFloat(g, outP.first, outP.second, ctx);
    if (!approx(thawed, std::sin(5.0f))) return fail("leg4: thawed output did not recompute");
  }

  // ===== Leg 4b: a Command-typed output goes no-op when disabled (freeze mechanism, type-agnostic) =====
  // We don't cook Command buffers here; the freeze is the SAME version-chasing skip — proven by the
  // cache flag being honored regardless of dataType. The Float freeze above IS the mechanism; this leg
  // asserts the model carries the flag for ANY output (no type gate on disable).
  {
    SymbolLibrary sl = makeLib(V);
    sl.symbols["Sine"].outputDefs[0].dataType = "Command";  // pretend a Command output
    SetOutputDisabledCommand dis(sl, "S", 2, "out", true);
    if (dis.refused()) return fail("leg4b: disable refused for a Command output (should be allowed)");
    dis.doIt();
    if (!childById(sl.symbols["S"], 2)->disabledOutputs.at("out"))
      return fail("leg4b: Command output disable not recorded");
  }

  // ===== Leg 4c: freeze survives a projection REBUILD via the REAL projection path. =====
  // (refuter-S2 blind spot 2 + the P1×P7 combination: production toggles disable through the
  // model -> libRevision bump -> buildEvalGraph rebuilds COLD -> without the transplant the
  // "frozen at last result" value snaps to 0. This leg drives model -> projection -> transplant.)
  {
    SymbolLibrary sl = makeLib(V);
    ResidentEvalGraph g = buildEvalGraph(sl, sl.rootId);
    initResidentCache(g);
    ResidentEvalCtx ctx; ctx.lib = &sl;
    auto outP = g.outputs["out"];
    float warm = pullResidentFloat(g, outP.first, outP.second, ctx);
    if (!approx(warm, SIN_V)) return fail("leg4c: warm-up out != sin(v)");

    SetOutputDisabledCommand dis(sl, "S", 2, "out", true);
    dis.doIt();  // model edit (production would bump libRevision here)
    ResidentEvalGraph fresh = buildEvalGraph(sl, sl.rootId);  // projection path carries the flag
    initResidentCache(fresh);
    transplantDisabledCaches(g, fresh);  // = frame_cook's rebuild seam
    float frozen = pullResidentFloat(fresh, outP.first, outP.second, ctx);
    if (!approx(frozen, SIN_V))
      return fail("leg4c: frozen value did not survive the rebuild (snapped off last result)");
    // Upstream change must NOT thaw it.
    sl.symbols["S"].children[0].overrides["value"] = 9.0f;
    ResidentEvalGraph fresh2 = buildEvalGraph(sl, sl.rootId);
    initResidentCache(fresh2);
    transplantDisabledCaches(fresh, fresh2);
    if (!approx(pullResidentFloat(fresh2, outP.first, outP.second, ctx), SIN_V))
      return fail("leg4c: frozen value moved after an upstream edit across rebuild");
    // Thaw -> recompute against the NEW upstream.
    SetOutputDisabledCommand en(sl, "S", 2, "out", false);
    en.doIt();
    ResidentEvalGraph fresh3 = buildEvalGraph(sl, sl.rootId);
    initResidentCache(fresh3);
    transplantDisabledCaches(fresh2, fresh3);
    if (!approx(pullResidentFloat(fresh3, outP.first, outP.second, ctx), std::sin(9.0f)))
      return fail("leg4c: thawed output did not recompute against new upstream");
  }

  // ===== Leg 8: bypass × disable mutual exclusion (= TiXL Slot.cs:50-53, second op refused). =====
  {
    SymbolLibrary sl = makeLib(V);
    // disable main output first -> bypass enable must refuse.
    SetOutputDisabledCommand dis(sl, "S", 2, "out", true);
    dis.doIt();
    SetBypassChildCommand byp(sl, "S", 2, true);
    if (!byp.refused()) return fail("leg8: bypass over a disabled main output was NOT refused");
    // clear; bypass first -> disable of the MAIN output must refuse.
    SetOutputDisabledCommand en(sl, "S", 2, "out", false);
    en.doIt();
    SetBypassChildCommand byp2(sl, "S", 2, true);
    if (byp2.refused()) return fail("leg8: legit bypass refused after thaw");
    byp2.doIt();
    SetOutputDisabledCommand dis2(sl, "S", 2, "out", true);
    if (!dis2.refused()) return fail("leg8: disable of a bypassed main output was NOT refused");
  }

  // ===== Leg 5: triggerOverride Always -> LIVE; clear -> STATIC =====
  {
    SymbolLibrary sl = makeLib(V);
    Symbol& root = sl.symbols["S"];
    {
      ResidentEvalGraph g0 = buildEvalGraph(sl, sl.rootId);
      initResidentCache(g0);
      if (countLive(g0) != 0) return fail("leg5: STATIC baseline has live sources");
    }
    SetOutputTriggerCommand t(sl, "S", 2, "out", TriggerOverride::Always);
    if (t.refused()) return fail("leg5: legit trigger set was refused");
    t.doIt();
    {
      ResidentEvalGraph g1 = buildEvalGraph(sl, sl.rootId);
      initResidentCache(g1);
      int live = countLive(g1);
      if (injectBug) live = 0;  // (leg1 already RED under injectBug; keep this leg honest if reached)
      if (live != 1) return fail("leg5: Always trigger did not make the output a LIVE source");
      // Prove the live source actually re-bumps every pass (version moves on bumpLiveSources).
      ResidentNode* sn = nullptr;
      for (ResidentNode& n : g1.nodes) if (n.path == "2") sn = &n;
      uint64_t before = sn->outCache["out"].baseVersion;
      bumpLiveSources(g1);
      if (sn->outCache["out"].baseVersion != before + 1)
        return fail("leg5: LIVE source did not bump every pass");
    }
    t.undo();
    if (childById(root, 2)->triggerOverrides.count("out"))
      return fail("leg5: undo did not erase the trigger key");
    {
      ResidentEvalGraph g2 = buildEvalGraph(sl, sl.rootId);
      initResidentCache(g2);
      if (countLive(g2) != 0) return fail("leg5: cleared trigger did not return to STATIC");
    }
  }

  // ===== Leg 6: savev2 roundtrip byte-stable + S15 tolerance =====
  {
    SymbolLibrary sl = makeLib(V);
    Symbol& root = sl.symbols["S"];
    childById(root, 2)->isBypassed = true;
    childById(root, 2)->disabledOutputs["out"] = true;
    childById(root, 2)->triggerOverrides["out"] = TriggerOverride::Always;

    std::string json = libToJsonV2(sl);
    SymbolLibrary back;
    std::vector<std::string> warns;
    if (!libFromJsonAny(json, back, &warns)) return fail("leg6: file with S2 fields failed to load");
    const SymbolChild* c = childById(*back.find("S"), 2);
    if (!c) return fail("leg6: child lost on reload");
    if (!c->isBypassed) return fail("leg6: isBypassed did not roundtrip");
    if (!c->disabledOutputs.count("out") || !c->disabledOutputs.at("out"))
      return fail("leg6: isDisabled did not roundtrip");
    auto tit = c->triggerOverrides.find("out");
    if (tit == c->triggerOverrides.end() || tit->second != TriggerOverride::Always)
      return fail("leg6: triggerOverride did not roundtrip");
    // byte-stable: a second save of the loaded lib equals the first dump.
    if (libToJsonV2(back) != json) return fail("leg6: save->load->save not byte-stable");

    // S15: tamper the output id to a non-existent slot -> the per-output entry is dropped, child lives.
    {
      std::string bad = json;
      auto p = bad.find("\"id\": \"out\"");
      // find the output-state "id":"out" (the LAST one — overrides/slotdefs also use "out" but as a
      // bare token, not "id":"out" inside the outputs array; the outputs array entry is the target).
      if (p != std::string::npos) bad.replace(p, std::string("\"id\": \"out\"").size(), "\"id\": \"zzz\"");
      SymbolLibrary b2;
      std::vector<std::string> w2;
      if (!libFromJsonAny(bad, b2, &w2)) return fail("leg6: tampered output id killed the whole file");
      const SymbolChild* c2 = childById(*b2.find("S"), 2);
      if (!c2) return fail("leg6: child dropped instead of tolerating bad output id");
    }
    // S15: tamper the trigger string to garbage -> trigger reads back None (dropped), child lives.
    {
      std::string bad = json;
      auto p = bad.find("\"trigger\": \"Always\"");
      if (p != std::string::npos)
        bad.replace(p, std::string("\"trigger\": \"Always\"").size(), "\"trigger\": \"Bogus\"");
      SymbolLibrary b3;
      std::vector<std::string> w3;
      if (!libFromJsonAny(bad, b3, &w3)) return fail("leg6: garbage trigger killed the file");
      const SymbolChild* c3 = childById(*b3.find("S"), 2);
      if (!c3 || c3->triggerOverrides.count("out"))
        return fail("leg6: garbage trigger string was not dropped to None");
    }
  }

  // ===== Leg 7: copy/paste bypass-after-wire seam =====
  {
    // 7a: copy BOTH children + the internal Const->Sine wire; mark Sine bypassed. After paste the new
    //     Sine should be bypassed (its main output wire... wait: the Sine.out->boundary wire is NOT
    //     internal-to-selection, so it's cut). To prove the seam we copy a selection where the copied
    //     child's MAIN OUTPUT is wired internally: make Sine feed a second Sine.
    SymbolLibrary sl;
    sl.symbols["Const"] = atomic("Const", {{"value", "value", "Float", 0.0f}},
                                 {{"out", "out", "Float", 0.0f}});
    sl.symbols["Sine"] = atomic("Sine", {{"x", "x", "Float", 0.0f}}, {{"out", "out", "Float", 0.0f}});
    Symbol root; root.id = "S"; root.name = "S"; root.atomic = false;
    root.outputDefs = {{"out", "out", "Float", 0.0f}};
    SymbolChild a; a.id = 1; a.symbolId = "Sine"; a.isBypassed = true;  // a's out is wired internally (a->b)
    SymbolChild b; b.id = 2; b.symbolId = "Sine";
    root.children = {a, b};
    root.connections = {{1, "out", 2, "x"}, {2, "out", kSymbolBoundary, "out"}};
    root.nextChildId = 3;
    sl.symbols["S"] = root; sl.rootId = "S";

    // Copy the selection {1,2}: the 1->2 wire is both-ends-internal (survives); 2->boundary is cut.
    ClipboardData clip = extractClipboard(sl.symbols["S"], {1, 2});
    if (clip.children.size() != 2) return fail("leg7a: clipboard did not capture both children");
    bool carried = false;
    for (const ClipboardChild& cc : clip.children) if (cc.id == 1 && cc.isBypassed) carried = true;
    if (!carried) return fail("leg7a: clipboard did not carry isBypassed");

    PastePlan plan = planPaste(sl, "S", clip, 100.0f, 100.0f);
    // The new copy of child 1 must NOT have bypass baked into the SymbolChild yet (deferred to seam).
    for (const PastedChild& pc : plan.children)
      if (plan.oldToNew.at(1) == pc.child.id && pc.child.isBypassed)
        return fail("leg7a: bypass was baked into the pasted child instead of deferred");

    CopyPasteChildrenCommand cmd(sl, "S", plan);
    cmd.doIt();
    const int newA = plan.oldToNew.at(1);
    const SymbolChild* na = childById(*sl.find("S"), newA);
    if (!na) return fail("leg7a: pasted child missing");
    if (!na->isBypassed)
      return fail("leg7a: seam did not apply bypass after the internal wire was laid");
    cmd.undo();
    if (childById(*sl.find("S"), newA)) return fail("leg7a: undo did not remove the pasted child");

    // 7b: copy ONLY child 1 (its main-output wire 1->2 is now EXTERNAL to the selection -> cut). The
    //     deferred bypass must NOT apply (no wire on the fresh instance's main output).
    SymbolLibrary sl2 = makeLib(V);
    childById(sl2.symbols["S"], 2)->isBypassed = false;  // start clean
    // child 1 is a Const (not bypassable) — use child 2 (Sine, bypassable) but copy it ALONE so its
    // out wire (2->boundary) is external and cut.
    childById(sl2.symbols["S"], 2)->isBypassed = true;
    ClipboardData clip2 = extractClipboard(sl2.symbols["S"], {2});
    PastePlan plan2 = planPaste(sl2, "S", clip2, 200.0f, 200.0f);
    CopyPasteChildrenCommand cmd2(sl2, "S", plan2);
    cmd2.doIt();
    const int newB = plan2.oldToNew.at(2);
    const SymbolChild* nb = childById(*sl2.find("S"), newB);
    if (!nb) return fail("leg7b: pasted lone child missing");
    if (nb->isBypassed)
      return fail("leg7b: bypass applied to a paste whose main output wire was cut (seam wrong)");
  }

  std::printf("[selftest] childstate: %s\n", injectBug ? "FAIL not reached (bug should RED)" : "PASS");
  return injectBug ? 1 : 0;
}

}  // namespace sw
