// app/variation_multipool_selftest — the MULTI-POOL + persistence teeth (--selftest-variation-multipool).
// TiXL ground-truth: ONE SymbolVariationPool PER composition symbol (VariationHandling.cs:76 switches
// ActivePoolForSnapshots on the active composition; .cs:89 GetOrLoadVariations is the per-symbol cache),
// each pool persisted (variations/{symbolId}.var, SymbolVariationPool.cs:148-150; sw: nested under the
// .swproj "variationPools" key — fork-poolset-single-file, variation_pool_set.h).
//
//   TOOTH 1 (multi-pool isolation)  — two compositions each grab a snapshot: A's snapshot must NOT
//     appear in B's grid, and switching back to A both keeps A's snapshot AND activates A's captured
//     value (not B's). injectBug degenerates the pool collection to a single shared pool
//     (debugForceSinglePoolKeyForTest — a REAL pool-key corruption, not a want-flip) -> A/B 混池 ->
//     B's grid shows A's slot AND A's activate applies B's value -> both fixed assertions bite.
//   TOOTH 2 (.swproj round-trip)    — two pools with one snapshot each -> save (embed) -> REAL
//     doOpenPath reload -> both pools restored and activate applies the persisted values; then the
//     REAL doSave call site persists a newly grabbed snapshot across a second reload. injectBug makes
//     the load skip the variation section (debugSkipPoolAdoptForTest — the "loader forgot the pools"
//     bug) -> every pool reloads empty -> the restored/activate assertions bite.
#include <cmath>
#include <cstdio>
#include <fstream>
#include <string>
#include <vector>

#include "app/document.h"            // doc::g_lib / g_compositionPath / pushComposition / doOpenPath / doSave
#include "app/variation_panel.h"     // panel API + persistence hooks + selftest seams
#include "runtime/compound_graph.h"  // SymbolLibrary / childById / effectiveInput
#include "runtime/compound_save.h"   // libToJsonV2 (the save body the embed rides on)
#include "runtime/graph.h"           // findSpec ("Const" — a REAL registered atomic, survives v2 reload)
#include "runtime/graph_bridge.h"    // atomicSymbolFromSpec (regenerable atomic — same as save selftest)
#include "runtime/selftest_registry.h"  // REGISTER_SELFTESTS

namespace sw::varpanel {
namespace {

constexpr float kTol = 1e-4f;

// Root{1:CompA, 2:CompB}; CompA and CompB each hold one snapshot-enabled child (id 1) instancing the
// REAL registered atomic "Const" (one Float input "value", def 0) so the lib survives a v2 save →
// reload (atomics regenerate from the registry). Entering child 1/2 of Root switches the composition —
// and therefore, in the multi-pool model, WHICH pool the panel reads.
SymbolLibrary buildTwoCompLib() {
  SymbolLibrary lib;
  lib.symbols["Const"] = atomicSymbolFromSpec(*findSpec("Const"));

  auto makeComp = [](const std::string& id) {
    Symbol comp;
    comp.id = id; comp.name = id; comp.atomic = false;
    SymbolChild child; child.id = 1; child.symbolId = "Const";
    child.snapshotGroupIndex = 1;  // EnabledForSnapshots (TiXL SymbolUi.Child.cs:50-56); persisted
    comp.children.push_back(child);
    comp.nextChildId = 2;
    return comp;
  };
  lib.symbols["CompA"] = makeComp("CompA");
  lib.symbols["CompB"] = makeComp("CompB");

  Symbol root;
  root.id = "Root"; root.name = "Root"; root.atomic = false;
  root.children.push_back({1, "CompA"});
  root.children.push_back({2, "CompB"});
  root.nextChildId = 3;
  lib.symbols["Root"] = root;
  lib.rootId = "Root";
  return lib;
}

void setAmount(SymbolLibrary& lib, const std::string& compId, float v) {
  Symbol* s = lib.find(compId);
  if (SymbolChild* c = s ? childById(*s, 1) : nullptr) c->overrides["value"] = v;
}
float readAmount(SymbolLibrary& lib, const std::string& compId) {
  Symbol* s = lib.find(compId);
  SymbolChild* c = s ? childById(*s, 1) : nullptr;
  return c ? effectiveInput(lib, *c, "value", -999.0f) : -999.0f;
}

// Enter Root's child `childId` as the current composition (from wherever we are).
bool enterComp(int childId) {
  doc::truncateComposition(0);
  return doc::pushComposition(childId);
}

// ── TOOTH 1 — per-composition pools stay isolated (TiXL VariationHandling.cs:76 pool switch) ─────
bool toothMultiPoolIsolation(bool injectBug) {
  reset();
  debugForceSinglePoolKeyForTest(injectBug);  // bug: every composition shares ONE pool key (混池)
  doc::g_lib() = buildTwoCompLib();
  SymbolLibrary& lib = doc::g_lib();
  doc::g_compositionPath.clear();

  // Composition A grabs slot 1 with amount=5.
  bool ok = enterComp(1) && doc::currentSymbolId() == "CompA";
  setAmount(lib, "CompA", 5.0f);
  ok = ok && grabSnapshot(lib, 1);

  // Switch to composition B: slot 1 must be EMPTY (A's snapshot must not bleed into B's pool).
  ok = ok && enterComp(2) && doc::currentSymbolId() == "CompB";
  const bool bEmpty = !slots()[0].filled;

  // B grabs its own slot 1 with amount=9 (same slot index as A's — the collision the multi-pool
  // model must keep apart).
  setAmount(lib, "CompB", 9.0f);
  ok = ok && grabSnapshot(lib, 1);

  // Back to A: the pool is still there, and ACTIVATE applies A's captured 5 — NOT B's 9.
  ok = ok && enterComp(1);
  const bool aStillFilled = slots()[0].filled;
  setAmount(lib, "CompA", 0.0f);  // move the live value away so the apply is observable
  ok = ok && activateSnapshot(lib, 1);
  const float got = readAmount(lib, "CompA");
  const bool appliedA = std::fabs(got - 5.0f) < kTol;  // fixed TiXL semantics: A's pool, A's value

  const bool pass = ok && bEmpty && aStillFilled && appliedA;
  std::printf("[selftest-variation-multipool] ISOLATION bEmpty=%s aStill=%s applied=%.4f(want 5) -> %s\n",
              bEmpty ? "y" : "n", aStillFilled ? "y" : "n", got, pass ? "PASS" : "FAIL");
  debugForceSinglePoolKeyForTest(false);
  reset();
  return pass;
}

// ── TOOTH 2 — pools survive save -> reload (.swproj "variationPools" round-trip) ─────────────────
bool toothPoolRoundTrip(bool injectBug) {
  reset();
  const std::string path = "/tmp/sw_multipool_selftest.swproj";
  std::remove(path.c_str());

  doc::g_lib() = buildTwoCompLib();
  SymbolLibrary& lib = doc::g_lib();
  doc::g_compositionPath.clear();

  // Pool A: slot 1 captures amount=5. Pool B: slot 2 captures amount=9.
  bool ok = enterComp(1);
  setAmount(lib, "CompA", 5.0f);
  ok = ok && grabSnapshot(lib, 1);
  ok = ok && enterComp(2);
  setAmount(lib, "CompB", 9.0f);
  ok = ok && grabSnapshot(lib, 2);
  doc::truncateComposition(0);

  // SAVE — the doSave body: libToJsonV2 + embedPoolsIntoDocJson, written to the real file.
  {
    const std::string withPools = embedPoolsIntoDocJson(libToJsonV2(lib));
    std::ofstream f(path);
    ok = ok && bool(f);
    f << withPools;
    ok = ok && f.good();
  }

  // RELOAD through the REAL doOpenPath (lib swap + panel reset + pool adopt). The bug leg makes the
  // adopt a no-op — the "loader forgot the variation section" corruption.
  reset();
  debugSkipPoolAdoptForTest(injectBug);
  ok = ok && doc::doOpenPath(path, /*quiet=*/true);
  debugSkipPoolAdoptForTest(false);

  // Pool A restored: slot 1 filled, activate applies the persisted 5.
  bool aBack = false, bBack = false;
  float gotA = -1.0f, gotB = -1.0f;
  if (ok) {
    ok = enterComp(1);
    aBack = slots()[0].filled;
    setAmount(lib, "CompA", 0.0f);
    activateSnapshot(lib, 1);
    gotA = readAmount(lib, "CompA");

    // Pool B restored: slot 2 filled, activate applies the persisted 9.
    ok = ok && enterComp(2);
    bBack = slots()[1].filled;
    setAmount(lib, "CompB", 0.0f);
    activateSnapshot(lib, 2);
    gotB = readAmount(lib, "CompB");
  }
  const bool restored = aBack && bBack && std::fabs(gotA - 5.0f) < kTol && std::fabs(gotB - 9.0f) < kTol;

  // REAL doSave call site: grab a NEW snapshot (B slot 3), save through doSave (doOpenPath set
  // g_documentPath to our file), reload again — the new snapshot must survive too.
  bool grewBack = false;
  if (ok && restored) {
    setAmount(lib, "CompB", 12.0f);
    ok = ok && grabSnapshot(lib, 3);
    doc::truncateComposition(0);
    ok = ok && doc::doSave();
    reset();
    ok = ok && doc::doOpenPath(path, /*quiet=*/true);
    ok = ok && enterComp(2);
    grewBack = slots()[2].filled;
  }

  const bool pass = ok && restored && grewBack;
  std::printf("[selftest-variation-multipool] ROUNDTRIP aBack=%s bBack=%s gotA=%.4f(want 5) "
              "gotB=%.4f(want 9) grewBack=%s -> %s\n",
              aBack ? "y" : "n", bBack ? "y" : "n", gotA, gotB, grewBack ? "y" : "n",
              pass ? "PASS" : "FAIL");
  std::remove(path.c_str());
  reset();
  return pass;
}

}  // namespace

int runVariationMultiPoolSelfTest(bool injectBug) {
  // Preserve / restore the live document (the teeth swap g_lib and drive doOpenPath/doSave, which
  // retarget the document path). initSnapshot() re-anchors the dirty baseline to the restored lib so
  // no stale "dirty" residue survives the test.
  SymbolLibrary saved = doc::g_lib();
  std::vector<int> savedPath = doc::g_compositionPath;
  bool ok = true;
  ok = toothMultiPoolIsolation(injectBug) && ok;
  ok = toothPoolRoundTrip(injectBug) && ok;
  doc::g_lib() = std::move(saved);
  doc::g_compositionPath = std::move(savedPath);
  doc::bumpLibRevision();
  doc::initSnapshot();
  reset();
  std::printf("[selftest-variation-multipool] %s\n", ok ? "PASS" : "FAIL");
  return ok ? 0 : 1;
}

}  // namespace sw::varpanel

namespace sw {
// Self-register into the --selftest router (306 block = the Variation lane, after variation-panel).
REGISTER_SELFTESTS(/*orderBase=*/306,
    {"variation-multipool", sw::varpanel::runVariationMultiPoolSelfTest});
}  // namespace sw
