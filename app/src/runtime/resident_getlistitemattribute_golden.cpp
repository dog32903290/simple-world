// runtime/resident_getlistitemattribute_golden — GetListItemAttribute MATH golden, split out of
// resident_point_value_output_cook.cpp (ARCHITECTURE rule 4, ≤400-line leaf; the cook file was at cap).
// The cook (cookPointValueOutputNodes' GetListItemAttribute branch) lives in the sibling; this file
// carries only the heavy self-test body + its registrar. Same split precedent as value_op_animvec3_golden
// .cpp (AnimVec3's golden split off its leaf).
//
// GATE: read the F1 / F2 float fields of an indexed point in a StructuredList<Point> — the sw analog of
// TiXL StructuredListUtils.GetValueOfFieldWithType<float> (StructuredListUtils.cs:12-54). Probes the
// DIVERGING MIDDLE (a point whose FX1 != FX2, at ItemIndex mid-list) and selects F2 by INDEX 5 and by
// NAME "F2" (both must recover FX2, not FX1). -bug flips the FieldIndex INPUT (5→1) so the REAL cook reads
// FX1 (a genuinely different cooked value) → the F2 assertion RED-s. A TEST-INPUT tooth on the real
// field-selection path (mirror of getpointdatafromlist's wrong-index tooth), NOT an expected-value flip.
//
// runtime leaf: pure computation, no hardware, no UI.
#include "runtime/resident_eval_graph.h"  // ResidentEvalGraph / ResidentNode / PointAccessor /
                                          // cookPointValueOutputNodes (tail-include resident_value_cooks.h)

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>

#include "runtime/tixl_point.h"         // SwPoint (FX1 @12 == TiXL Point.F1; FX2 @60 == Point.F2)
#include "runtime/value_op_registry.h"  // valueOpSelfTests() (the golden registrar sink)

namespace sw {
namespace {

// A stub accessor returning a known point array (no real point cook needed) — local copy of the cook
// file's stubAccessor (a 4-line closure; not worth a shared header for one helper).
PointAccessor stubAccessor(const SwPoint* pts, uint32_t n) {
  return [pts, n](const std::string& /*src*/, uint32_t& outCount) -> const SwPoint* {
    outCount = n;
    return pts;
  };
}

int runGetListItemAttributeGolden(bool injectBug) {
  const float eps = 1e-4f;
  bool ok = true;

  // A 3-point list; index 7 % 3 == 1 → point[1] (the diverging middle, not endpoint 0/2). point[1] has
  // FX1=3.5 and FX2=9.25 (distinct → selecting the wrong field diverges). FieldIndex 5 = F2 → expect 9.25.
  SwPoint pts[3]{};
  pts[0].FX1 = 1.0f;  pts[0].FX2 = 2.0f;
  pts[1].FX1 = 3.5f;  pts[1].FX2 = 9.25f;   // point[1]: the probed middle; FX1 != FX2
  pts[2].FX1 = 8.0f;  pts[2].FX2 = 8.0f;

  auto cookOne = [&](float fieldIndex, const std::string& fieldName) -> float {
    ResidentEvalGraph g;
    ResidentNode rn;
    rn.path = "1";
    rn.opType = "GetListItemAttribute";
    { ResidentInput ri; ri.slotId = "DataList"; ri.driver = ResidentInput::Driver::Connection;
      ri.srcNodePath = "src"; rn.inputs.push_back(ri); }
    { ResidentInput ri; ri.slotId = "ItemIndex"; ri.driver = ResidentInput::Driver::Constant;
      ri.constant = 7.0f; rn.inputs.push_back(ri); }  // 7 % 3 == 1
    { ResidentInput ri; ri.slotId = "FieldIndex"; ri.driver = ResidentInput::Driver::Constant;
      ri.constant = fieldIndex; rn.inputs.push_back(ri); }
    rn.strInputs["OrFieldName"] = fieldName;
    g.nodes.push_back(rn);
    g.byPath["1"] = 0;
    cookPointValueOutputNodes(g, ResidentEvalCtx{}, stubAccessor(pts, 3));
    return g.nodes[0].extOut[0];
  };

  // 1) By INDEX 5 → F2 → FX2 of point[1] = 9.25. -bug flips the INPUT FieldIndex to 1 → the cook reads FX1
  //    (=3.5), a real different value → this assert RED-s. index-picks-float-field is the mechanism tested.
  const float byIndex = cookOne(injectBug ? 1.0f : 5.0f, "");
  ok = ok && std::fabs(byIndex - 9.25f) < eps;

  // 2) By NAME "F2" (FieldIndex 0 = Position, a NON-float field → the index match fails the `is float`
  //    test, so the NAME "F2" wins) → FX2 = 9.25. Proves the name path AND that a non-float index falls
  //    through to name (StructuredListUtils.cs:32-44). (No -bug branch: this leg's tooth is carried by leg 1;
  //    a clean-only assertion of the name path — always the same expected value.)
  if (!injectBug) {
    const float byName = cookOne(0.0f, "F2");  // index 0 = Position (non-float) → name "F2" wins
    ok = ok && std::fabs(byName - 9.25f) < eps;

    // 3) F1 by index 1 → FX1 of point[1] = 3.5 (the OTHER float field; proves the two are distinguished).
    const float f1 = cookOne(1.0f, "");
    ok = ok && std::fabs(f1 - 3.5f) < eps;

    // 4) A NON-float field (index 4 = Scale) with no name → miss → 0 (the `is float`-fail default, cs:45).
    const float miss = cookOne(4.0f, "");
    ok = ok && std::fabs(miss - 0.0f) < eps;

    std::printf("[selftest-getlistitemattribute] byName=%.3f f1=%.3f miss=%.3f\n", byName, f1, miss);
  }

  std::printf("[selftest-getlistitemattribute] byIndex(F2 of pt[7%%3=1])=%.3f%s -> %s\n", byIndex,
              injectBug ? " (injectBug->FieldIndex 1 reads F1)" : "", ok ? "PASS" : "FAIL");
  return ok ? 0 : 1;
}

struct GetListItemAttributeGoldenRegistrar {
  GetListItemAttributeGoldenRegistrar() {
    valueOpSelfTests().push_back({"getlistitemattribute", runGetListItemAttributeGolden});
  }
};
static const GetListItemAttributeGoldenRegistrar _reg_getlistitemattribute_golden;

}  // namespace
}  // namespace sw
