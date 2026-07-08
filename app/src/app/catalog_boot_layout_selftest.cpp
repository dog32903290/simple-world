// app/catalog_boot_layout_selftest — REAL end-to-end proof of the .t3ui layout seam through the
// PRODUCTION boot path (loadCatalogFromFolder(SW_CATALOG_T3_DIR)), not an in-memory fixture. The
// t3import_layout_golden.cpp keystone proves the importer MECHANISM (TransformPoints.t3 + embedded
// .t3ui); this proves the ACTUAL shipped assets/catalog_t3/ folder (8 .t3 + their 8 sibling .t3ui, all
// committed 2026-07-08) produce non-origin positions when boot-loaded exactly like a real app launch —
// the literal repro path for 柏為's "dive into a compound, every child sits on (0,0)" report, since
// image-fx COLLAPSE compounds (Blend/BoxGradient/…) keep their decompose-helper children as real
// SymbolChildren (atomic==false, dive-in-able) even after collapsing the fx-setup wrapper to one atom.
//
// Target: Blend.t3 (9f43f769…) — 6 kept helper children (2×Vector4Components, 3×IntToFloat,
// 1×BoolToFloat) + the COLLAPSED atom (which inherits the fx-setup wrapper child's OWN .t3ui Position,
// t3_import_collapse.cpp's childGuidToId[fxChildGuid]=atomId). Children[] walk order (verified against
// the raw Blend.t3, fx-setup wrapper skipped/collapsed) assigns childId 1..6 to the 6 helpers in the
// SAME order .t3ui lists them; atomId=7. Expected positions quoted verbatim from Blend.t3ui's
// SymbolChildUis[] (the file, never sw's own output — GOLDEN_STANDARD 特徵1).
//
// injectBug flips t3LayoutDisable() (the SAME production seam the layout keystone golden flips) →
// loadCatalogFromFolder still imports all 8 .t3 fine, but every position reverts to 0,0.
//
// ZONE: app (product boot behaviour) golden — calls the real doc::loadCatalogFromFolder + inspects
// doc::g_lib(), the same globals main.cpp wires at real startup.
#include "app/document.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>

#include "runtime/compound_graph.h"
#include "runtime/selftest_registry.h"  // REGISTER_SELFTESTS
#include "runtime/t3_import.h"          // t3LayoutDisable

namespace sw {

namespace {
const char* const kBlendGuid = "9f43f769-d32a-4f49-92ac-e0be3ba250cf";
struct ExpectedChild { const char* type; float x, y; };
const ExpectedChild kExpected[] = {
    {"Vector4Components", 122.33575f, 154.58716f},  // Blend.t3ui:100-105 (42f85198)
    {"Vector4Components", 122.33575f,  98.58719f},  // Blend.t3ui:107-112 (46965dc5)
    {"IntToFloat",         122.33575f, 210.58716f},  // Blend.t3ui:114-119 (8127b1df)
    {"BoolToFloat",        120.221344f, 310.80862f}, // Blend.t3ui:121-126 (882f89a2)
    {"IntToFloat",         119.894775f, 417.96936f}, // Blend.t3ui:128-133 (b2c29ae7)
    {"IntToFloat",         122.33575f, 266.5871f},   // Blend.t3ui:135-140 (b7edf6bf)
    {"Blend",              683.5539f,   78.61325f},  // Blend.t3ui:93-98 (30ce4abc, the collapsed fx-setup wrapper)
};
constexpr int kNumExpected = sizeof(kExpected) / sizeof(kExpected[0]);
constexpr float kEps = 0.01f;
bool near(float a, float b) { return std::fabs(a - b) < kEps; }
}  // namespace

int runCatalogBootLayoutSelfTest(bool injectBug) {
#ifndef SW_CATALOG_T3_DIR
  printf("[catalog-layout] FAIL: SW_CATALOG_T3_DIR not defined (build config gap)\n");
  return 1;
#else
  t3LayoutDisable() = injectBug;
  const int imported = doc::loadCatalogFromFolder(SW_CATALOG_T3_DIR, /*quiet=*/true);
  t3LayoutDisable() = false;
  if (imported < 8) {
    printf("[catalog-layout] FAIL: expected >=8 catalog symbols imported, got %d\n", imported);
    return 1;
  }

  const Symbol* blend = doc::g_lib().find(kBlendGuid);
  if (!blend || (int)blend->children.size() != kNumExpected) {
    printf("[catalog-layout] FAIL: Blend expected %d children, got %d\n", kNumExpected,
           blend ? (int)blend->children.size() : -1);
    return 1;
  }

  int mismatches = 0, nonZero = 0;
  for (int i = 0; i < kNumExpected; ++i) {
    const SymbolChild* ch = childById(*blend, i + 1);
    if (!ch || ch->symbolId != kExpected[i].type) {
      printf("[catalog-layout] FAIL: child #%d expected type %s, got %s\n", i + 1, kExpected[i].type,
             ch ? ch->symbolId.c_str() : "<missing>");
      return 1;
    }
    const bool wantMatch = near(ch->x, injectBug ? 0.0f : kExpected[i].x) &&
                           near(ch->y, injectBug ? 0.0f : kExpected[i].y);
    if (!wantMatch) ++mismatches;
    if (!(ch->x == 0.0f && ch->y == 0.0f)) ++nonZero;
    printf("[catalog-layout] Blend child #%d %-20s x=%.4f y=%.4f\n", i + 1, kExpected[i].type, ch->x, ch->y);
  }

  // HARD-FAIL, not NO-BITE (柏為 07-08 gate hole): the shipped assets/catalog_t3/*.t3ui provably carry
  // non-zero Positions (committed beside each .t3). A base run producing FEWER than all-children
  // non-zero means the PRODUCTION boot path (loadCatalogFromFolder → collapse/normal →
  // applyT3uiPositions) failed to seed them — that IS 柏為's "every child stacked on (0,0)" bug and it
  // must FAIL the gate. A base-leg `return 0` here would be invisible: run_all_selftests.sh --bite only
  // inspects the -bug leg's exit code for NO-BITE, so a base return-0 lets a real layout regression pass
  // GREEN (P1 閘空轉 — the exact blind spot this selftest exists to close). The GOLDEN_STANDARD did-not-
  // trip→return-0 rule is about the -BUG injection not tripping; a base run that can't fail is not that.
  if (!injectBug && nonZero < kNumExpected) {
    printf("[catalog-layout] FAIL: base run seeded only %d/%d non-zero children — .t3ui layout NOT "
           "applied on the real boot path (柏為's 0,0 bug reproduced)\n", nonZero, kNumExpected);
    return 1;
  }
  printf("[catalog-layout] %s: mismatches=%d\n", injectBug ? "-bug" : "PASS", mismatches);
  if (!injectBug) return mismatches == 0 ? 0 : 1;
  const bool bites = (mismatches == 0);  // -bug expects EVERY position to have reverted to 0,0
  printf("[catalog-layout] -bug: layout tooth %s\n", bites ? "BITES" : "TOOTHLESS");
  return bites ? 1 : 0;
#endif
}

REGISTER_SELFTESTS(/*orderBase=*/970, {"catalog-layout", runCatalogBootLayoutSelfTest});

}  // namespace sw
