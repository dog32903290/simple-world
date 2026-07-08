// runtime/t3import_layout_golden — the .t3ui CANVAS-POSITION keystone (--selftest-t3-layout).
//
// 柏為 07-08 observed bug: dive into an imported compound node and every atom child sits stacked on
// (0,0). Root cause (scout-confirmed, see docs/agent/COMPOUND_RECURSION_SEAM_SPEC.md "★後續 lane"):
// canvas positions live in a SIBLING "<Name>.t3ui" file (SymbolChildUis[]/InputUis[]/OutputUis[]
// Position, keyed by guid — TiXL Editor/UiModel/SymbolUiJson.cs), NOT the .t3 the importer already
// read. Fix = the T3LayoutResolver seam (t3_import.h) + applyT3uiPositions (t3_import_layout.cpp),
// wired through importT3Symbol's new overload.
//
// Load the REAL TransformPoints.t3 + its REAL sibling TransformPoints.t3ui through the PRODUCTION path
// importT3Symbol(…, resolve, layoutResolve) and assert every one of its 10 mapped children + both
// boundary pins (Points input / Output output) land EXACTLY on the .t3ui file's own Position constants
// (quoted below with the .t3ui line each came from — never sw's own output, GOLDEN_STANDARD 特徵1).
// TransformPoints.t3's Children[] order (verified against the raw .t3, ComputeShader folds onto
// ComputeShaderStage's KernelName and emits NO child — t3_import.h "computeshader-source-folded-onto-
// stage") assigns childId 1..10 to: IntsToBuffer, GetBufferComponents×2 (a REUSE proof — two distinct
// instances of the SAME type, keyed by DIFFERENT t3 guids, must land on DIFFERENT positions),
// StructuredBufferWithViews, CalcDispatchCount, ComputeShaderStage, GetSRVProperties, TransformMatrix,
// FloatsToBuffer, ExecuteBufferUpdate.
//
// injectBug flips t3LayoutDisable() → applyT3uiPositions no-ops on the SAME real cook seam → every
// position reverts to the pre-seam default 0,0 (GOLDEN_STANDARD 特徵3: real seam, not a want-flip — the
// EXPECTED constants below never change; only what the importer actually WROTE changes under the bug).
//
// ZONE: runtime golden (shell tier — binds the importer + the .t3ui text fixtures).
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "runtime/compound_graph.h"  // SymbolLibrary / Symbol / SymbolChild / SlotDef
#include "runtime/t3_import.h"       // importT3Symbol / T3LayoutResolver / t3LayoutDisable / symbolIdOfT3

namespace sw {

namespace {

static const char* kTransformPointsT3 =
#include "runtime/transformpoints_t3_embed.inc"
;
static const char* kTransformPointsT3ui =
#include "runtime/transformpoints_t3ui_embed.inc"
;

const char* const kTransformPointsGuid = "7f6c64fe-ca2e-445e-a9b4-c70291ce354e";
const char* const kPointsInputDef = "565ff364-c3d9-4c60-a9a0-79fdd36d3477";
const char* const kOutputDef = "ba17981e-ef9f-46f1-a653-6d50affa8838";

// One expected {name, x, y} row, in TransformPoints.t3's OWN Children[] array order (childId 1..10 —
// ComputeShader is folded, contributes no child). Positions quoted verbatim from TransformPoints.t3ui's
// SymbolChildUis[] (the file, not any sw output).
struct ExpectedChild { const char* type; float x, y; };
const ExpectedChild kExpected[] = {
    {"IntsToBuffer",              107.83078f, 1087.1665f},   // .t3ui:136-141 (2bf12c9a)
    {"GetBufferComponents",       113.58789f,  657.60925f},  // .t3ui:143-148 (3aa1f8d9)
    {"GetBufferComponents",      -238.22786f,  718.185f},    // .t3ui:150-155 (4877c491)
    {"StructuredBufferWithViews", 113.58789f,  622.60925f},  // .t3ui:164-169 (597bfff4)
    {"CalcDispatchCount",         253.58789f,  552.60925f},  // .t3ui:171-176 (633e829a)
    {"ComputeShaderStage",        479.60776f,  575.34845f},  // .t3ui:178-183 (852d7521)
    {"GetSRVProperties",          113.58789f,  587.60925f},  // .t3ui:185-190 (b23f0e8b)
    {"TransformMatrix",           107.50235f,  762.4449f},   // .t3ui:192-197 (c9390e2c)
    {"FloatsToBuffer",            107.50235f,  972.44495f},  // .t3ui:199-204 (d45b52bf)
    {"ExecuteBufferUpdate",       479.6078f,   785.34845f},  // .t3ui:206-211 (d7f0248f)
};
constexpr int kNumExpected = sizeof(kExpected) / sizeof(kExpected[0]);
constexpr float kPointsX = -378.22784f, kPointsY = 718.185f;  // .t3ui InputUis Points (:37-41)
constexpr float kOutX = 479.6078f, kOutY = 855.34845f;        // .t3ui OutputUis Output (:214-217)
constexpr float kEps = 0.01f;

bool near(float a, float b) { return std::fabs(a - b) < kEps; }

}  // namespace

int runT3LayoutGolden(bool injectBug) {
  // ---- STEP 1: layout resolver keyed by TransformPoints' OWN top-level Id (peeked, not hardcoded twice
  // — guards against the embedded .t3/.t3ui pair silently drifting out of sync with kTransformPointsGuid).
  std::string tpId;
  if (!symbolIdOfT3(kTransformPointsT3, &tpId) || tpId != kTransformPointsGuid) {
    printf("[t3-layout] FAIL: symbolIdOfT3(TransformPoints.t3) mismatch (got %s want %s)\n",
           tpId.c_str(), kTransformPointsGuid);
    return 1;
  }
  const T3LayoutResolver layoutResolve = [&tpId](const std::string& guid, std::string& out) -> bool {
    if (guid != tpId) return false;
    out = kTransformPointsT3ui;
    return true;
  };

  t3LayoutDisable() = injectBug;  // -bug: the read no-ops even though layoutResolve has a real hit
  SymbolLibrary lib;
  std::string rootId;
  std::vector<std::string> warnings;
  const bool ok =
      importT3Symbol(kTransformPointsT3, lib, &rootId, &warnings, T3Resolver{}, layoutResolve);
  t3LayoutDisable() = false;
  if (!ok || rootId != kTransformPointsGuid) {
    printf("[t3-layout] FAIL: importT3Symbol(TransformPoints) returned false/wrong id\n");
    return 1;
  }

  Symbol* tp = lib.find(rootId);
  if (!tp || (int)tp->children.size() != kNumExpected) {
    printf("[t3-layout] FAIL: expected %d children, got %d\n", kNumExpected,
           tp ? (int)tp->children.size() : -1);
    return 1;
  }

  // ---- STEP 2: per-child position check, IN Children[] ORDER (childId 1..N) ----
  int mismatches = 0, distinctNonZero = 0;
  for (int i = 0; i < kNumExpected; ++i) {
    const SymbolChild* ch = childById(*tp, i + 1);
    if (!ch || ch->symbolId != kExpected[i].type) {
      printf("[t3-layout] FAIL: child #%d expected type %s, tp child %s\n", i + 1, kExpected[i].type,
             ch ? ch->symbolId.c_str() : "<missing>");
      return 1;
    }
    const bool wantMatch = !injectBug;
    const bool gotMatch = near(ch->x, kExpected[i].x) && near(ch->y, kExpected[i].y);
    const bool gotZero = near(ch->x, 0.0f) && near(ch->y, 0.0f);
    if (wantMatch && !gotMatch) ++mismatches;
    if (injectBug && !gotZero) ++mismatches;  // -bug must revert EVERY position to 0,0
    if (!(ch->x == 0.0f && ch->y == 0.0f)) ++distinctNonZero;
    printf("[t3-layout] child #%d %-24s x=%.4f y=%.4f (want %.4f,%.4f)\n", i + 1, kExpected[i].type,
           ch->x, ch->y, injectBug ? 0.0f : kExpected[i].x, injectBug ? 0.0f : kExpected[i].y);
  }

  // ---- STEP 3: boundary pins (§ .t3ui InputUis/OutputUis → SlotDef.x/y) ----
  const SlotDef* pointsDef = nullptr, *outDef = nullptr;
  for (const SlotDef& d : tp->inputDefs) if (d.id == kPointsInputDef) pointsDef = &d;
  for (const SlotDef& d : tp->outputDefs) if (d.id == kOutputDef) outDef = &d;
  if (!pointsDef || !outDef) {
    printf("[t3-layout] FAIL: Points inputDef / Output outputDef missing\n");
    return 1;
  }
  const bool pointsOk = injectBug ? (near(pointsDef->x, 0) && near(pointsDef->y, 0))
                                  : (near(pointsDef->x, kPointsX) && near(pointsDef->y, kPointsY));
  const bool outOk = injectBug ? (near(outDef->x, 0) && near(outDef->y, 0))
                               : (near(outDef->x, kOutX) && near(outDef->y, kOutY));
  if (!pointsOk) ++mismatches;
  if (!outOk) ++mismatches;
  printf("[t3-layout] Points-pin x=%.4f y=%.4f (want %.4f,%.4f)  Output-pin x=%.4f y=%.4f (want %.4f,%.4f)\n",
         pointsDef->x, pointsDef->y, injectBug ? 0.0f : kPointsX, injectBug ? 0.0f : kPointsY, outDef->x,
         outDef->y, injectBug ? 0.0f : kOutX, injectBug ? 0.0f : kOutY);

  const bool green = (mismatches == 0);
  // did-not-trip guard (GOLDEN_STANDARD 特徵3): the NON-bug run must actually see non-zero, DISTINCT
  // positions (else the whole assertion set is vacuously satisfied by an importer that never wrote
  // anything) — dead tooth exits 0 so --bite's NO-BITE list catches it instead of a false PASS.
  if (!injectBug && distinctNonZero < kNumExpected) {
    printf("[t3-layout] NO-BITE: non-bug run produced %d/%d non-zero children (seam not exercised)\n",
           distinctNonZero, kNumExpected);
    return 0;
  }

  printf("[t3-layout] %s: %d/%d checks %s (mismatches=%d)\n", injectBug ? "-bug" : "PASS",
         kNumExpected + 2, kNumExpected + 2, green ? "GREEN" : "RED", mismatches);

  if (!injectBug) return green ? 0 : 1;
  // -bug leg: the tooth BITES iff positions actually collapsed to 0,0 (green here == bug reproduced the
  // pre-seam behaviour perfectly, which is the EXPECTED-under-bug outcome, not a pass/fail on `mismatches`).
  const bool bites = green;
  printf("[t3-layout] -bug: layout tooth %s\n", bites ? "BITES (all positions reverted to 0,0)" : "TOOTHLESS");
  return bites ? 1 : 0;
}

}  // namespace sw
