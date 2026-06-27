// runtime/resident_point_value_output_cook — cookPointValueOutputNodes: the PRODUCTION (resident-path)
// POINT-INTO-FRAME value-emit pass (value-output-rail Phase 4). The pass that lifts PointToMatrix off the
// deferral named in resident_matrix_output_cook.cpp (defer-pointtomatrix-needs-point-into-frame-pass) and
// wires GetPointDataFromList — both read ONE point from a cooked Shared point buffer host-side.
//
// WHY THIS FILE EXISTS / THE WALL IT CROSSES:
//   The value-emit family (cookValueOutputNodes / cookMatrixOutputNodes) runs in cook_host_values, which
//   frame_cook calls BEFORE pg.cookResident — so at that point this frame's point buffers don't exist.
//   PointToMatrix/GetPointDataFromList both consume a POINT (StructuredList<Point>), so their emit MUST run
//   AFTER the point cook. This pass is therefore a SEPARATE frame-level pass, called AFTER pg.cookResident
//   (frame_cook.cpp), reading the finished Shared point buffers through a PointAccessor.
//
// NOT A READBACK PROBLEM: the point buffers are ResourceStorageModeShared (PointGraph::ensureOut), so
//   PointAccessor → contents() reinterprets to a `const SwPoint*` host-readable with ZERO blit. This is the
//   exact sw analog of TiXL's host-side pointList.TypedElements[i] (PointToMatrix.cs:27 /
//   GetPointDataFromList.cs:40). The pass is ADDITIVE: it only READS finished buffers + WRITES the two ops'
//   own (previously-unemitted) extOut/extColorOut — it touches NO existing emit pass and NO cook-core
//   recursion (the accessor is built from PointGraph's const residentCookedPoints — a borrowed read).
//
// SCOPE — point-into-frame value-out ONLY: PointToMatrix (matrix-of-point[0]) + GetPointDataFromList
//   (one point's Position/W/Orientation). GetTextureSize (a texture-source sibling, different accessor) is
//   a separate follow-up. The remaining value-out-from-point family for a later fan-out: SamplePointAtList
//   /point-attribute readers (each the same shape — resolve a Points input, index host-side, emit).
#include "runtime/resident_eval_graph.h"  // ResidentEvalGraph / ResidentNode / resolveResidentFloatInputs
                                          // + (tail include) resident_value_cooks.h: PointAccessor /
                                          // cookPointValueOutputNodes / pointToMatrixRows decls

#include <array>
#include <cmath>
#include <cstdio>
#include <map>
#include <string>
#include <vector>

#include <simd/simd.h>

#include "runtime/graph.h"              // NodeSpec / PortSpec / findSpec
#include "runtime/tixl_point.h"         // SwPoint (64B; Position@0 / FX1@12 / Rotation@16 / Scale@48)
#include "runtime/value_op_registry.h"  // valueOpSelfTests() (the golden registrar, zero shared-file edit)

namespace sw {

namespace {

// Resolve a resident node's Points-input srcNodePath. PointToMatrix's CamPointBuffer / GetPointData's
// DataList is a single-cardinality Points input → the primary Connection's srcNodePath. Returns "" when
// the input is unwired (Constant/absent — no point source), so the caller emits identity.
std::string pointsSrcPath(const ResidentNode& rn, const NodeSpec& s) {
  for (const PortSpec& port : s.ports) {
    if (!port.isInput || port.dataType != "Points") continue;
    const ResidentInput* ri = rn.input(port.id);
    if (ri && ri->driver == ResidentInput::Driver::Connection) return ri->srcNodePath;
    return "";  // the (one) Points input is unwired
  }
  return "";
}

// The ColorList-typed output port index of a spec (the matrix/vec4 channel; mirror of
// cookMatrixOutputNodes). -1 if none.
int colorListOutPort(const NodeSpec& s) {
  for (size_t i = 0; i < s.ports.size(); ++i)
    if (!s.ports[i].isInput && s.ports[i].dataType == "ColorList") return (int)i;
  return -1;
}

// Identity matrix rows (the faithful PointToMatrix.cs:24 NumElements==0 → return: the Slot keeps its
// prior value; with no prior, identity is the neutral matrix a downstream consumer reads as no-op).
std::array<simd::float4, 4> identityRows() {
  return {simd::make_float4(1, 0, 0, 0), simd::make_float4(0, 1, 0, 0), simd::make_float4(0, 0, 1, 0),
          simd::make_float4(0, 0, 0, 1)};
}

}  // namespace

void cookPointValueOutputNodes(ResidentEvalGraph& g, const ResidentEvalCtx& ctx,
                               const PointAccessor& acc) {
  for (ResidentNode& rn : g.nodes) {
    const bool isP2M = rn.opType == "PointToMatrix";
    const bool isGPD = rn.opType == "GetPointDataFromList";
    if (!isP2M && !isGPD) continue;
    const NodeSpec* s = findSpec(rn.opType);
    if (!s) continue;

    // Resolve the cooked point buffer feeding this node's Points input (host-readable, zero blit).
    uint32_t count = 0;
    const SwPoint* pts = nullptr;
    const std::string src = pointsSrcPath(rn, *s);
    if (!src.empty() && acc) pts = acc(src, count);

    if (isP2M) {
      // PointToMatrix.cs: build the SRT matrix of point[0] (Position/Orientation/Scale) and emit the 4
      // transposed rows onto the ColorList channel — IDENTICAL math + channel as cookMatrixOutputNodes's
      // TransformMatrix path. Empty/unwired → identity (faithful NumElements==0 passthrough).
      const int outPort = colorListOutPort(*s);
      if (outPort < 0) continue;
      std::array<simd::float4, 4> rows;
      if (pts && count > 0) {
        const SwPoint& p = pts[0];
        simd::float3 pos = simd::make_float3(p.Position.x, p.Position.y, p.Position.z);
        float oq[4] = {p.Rotation.x, p.Rotation.y, p.Rotation.z, p.Rotation.w};
        simd::float3 scl = simd::make_float3(p.Scale.x, p.Scale.y, p.Scale.z);
        rows = pointToMatrixRows(pos, oq, scl);
      } else {
        rows = identityRows();
      }
      rn.extColorOut[outPort] = {rows[0], rows[1], rows[2], rows[3]};
      continue;
    }

    // GetPointDataFromList.cs:40 — point[ItemIndex.Mod(N)]. Emit Position→extOut[0..2], F1(W)→extOut[3],
    // Orientation→extOut[4..7]. FORK fork-getpointdata-vec-as-scalar-ports: TiXL has Position(Vec3),
    // W(float), Orientation(Vec4) as 3 typed Slots; sw fans them onto the SCALAR extOut[] rail (8 slots =
    // 3+1+4 exactly), the EXACT same fork RequestedResolution uses (Size as 2 Float ports). Faithful in
    // VALUE, forked only in wire-cardinality (the established value-output scalar-pack fork). Empty/unwired
    // → leave extOut at 0 (faithful NumElements==0 → return: the Slot keeps its default 0).
    if (!pts || count == 0) continue;
    // index.Mod(N): C# Mod is a EUCLIDEAN modulo (always non-negative). ItemIndex default 0.
    int idx = (int)std::lround(resolveResidentFloatInputs(g, rn, ctx)["ItemIndex"]);
    int n = (int)count;
    int m = ((idx % n) + n) % n;  // Euclidean mod (matches T3 Utilities int.Mod)
    const SwPoint& p = pts[(uint32_t)m];
    rn.extOut[0] = p.Position.x;
    rn.extOut[1] = p.Position.y;
    rn.extOut[2] = p.Position.z;
    rn.extOut[3] = p.FX1;  // W = point.F1 (SwPoint.FX1 @12 == TiXL Point.F1)
    rn.extOut[4] = p.Rotation.x;
    rn.extOut[5] = p.Rotation.y;
    rn.extOut[6] = p.Rotation.z;
    rn.extOut[7] = p.Rotation.w;
  }
}

// --- POINT-INTO-FRAME emit GOLDENS (value-output-rail Phase 4) ------------------------------------
// Both mirror the 3-leg shape of resident_matrix_output_cook.cpp's runTransformMatrixGolden: cook through
// the production pass with a STUB PointAccessor (a known SwPoint — no real point cook needed), read the
// emitted slots, assert against a HAND-DERIVED closed form. -bug asserts the PRE-transform / wrong-mapping
// expectation so the CORRECT emit FAILS (a TEST-INPUT tooth, not an impl flip).
namespace {

bool f4Near(simd::float4 a, simd::float4 b, float eps) {
  return std::fabs(a.x - b.x) < eps && std::fabs(a.y - b.y) < eps && std::fabs(a.z - b.z) < eps &&
         std::fabs(a.w - b.w) < eps;
}

// A stub accessor returning ONE known point. Built per-golden over a captured SwPoint.
PointAccessor stubAccessor(const SwPoint* one, uint32_t n) {
  return [one, n](const std::string& /*src*/, uint32_t& outCount) -> const SwPoint* {
    outCount = n;
    return one;
  };
}

int runPointToMatrixEmitGolden(bool injectBug) {
  const float eps = 1e-4f;
  bool ok = true;

  // Resident PointToMatrix node with a WIRED CamPointBuffer (Connection) so pointsSrcPath resolves.
  ResidentEvalGraph g;
  ResidentNode rn;
  rn.path = "1";
  rn.opType = "PointToMatrix";
  ResidentInput ri;
  ri.slotId = "CamPointBuffer";
  ri.driver = ResidentInput::Driver::Connection;
  ri.srcNodePath = "src";  // the stub accessor ignores the path
  rn.inputs.push_back(ri);
  g.nodes.push_back(rn);
  g.byPath["1"] = 0;

  // KNOWN point: Position=(1,2,3), Orientation=Identity quat, Scale=(1,1,1). Closed-form (transposed,
  // identity rotation+scale): translation in the W column → rows (1,0,0,1)(0,1,0,2)(0,0,1,3)(0,0,0,1).
  // SAME closed form the matrix golden bakes for its PointToMatrix MATH leg (resident_matrix_output_cook
  // .cpp:343-356) — this golden proves the SAME math now rides the PRODUCTION point-into-frame channel.
  SwPoint p{};
  p.Position = {1.0f, 2.0f, 3.0f};
  p.Rotation = simd::make_float4(0, 0, 0, 1);
  p.Scale = {1.0f, 1.0f, 1.0f};
  cookPointValueOutputNodes(g, ResidentEvalCtx{}, stubAccessor(&p, 1));

  // Read the 4 rows off extColorOut (the production channel; the ColorList output port).
  std::vector<simd::float4> got;
  {
    const NodeSpec* s = findSpec("PointToMatrix");
    int outPort = -1;
    for (size_t i = 0; s && i < s->ports.size(); ++i)
      if (!s->ports[i].isInput && s->ports[i].dataType == "ColorList") { outPort = (int)i; break; }
    auto it = g.nodes[0].extColorOut.find(outPort);
    if (it != g.nodes[0].extColorOut.end()) got = it->second;
  }
  bool haveFour = got.size() == 4;

  simd::float4 exR1 = simd::make_float4(1, 0, 0, 1);
  simd::float4 exR2 = simd::make_float4(0, 1, 0, 2);
  simd::float4 exR3 = simd::make_float4(0, 0, 1, 3);
  simd::float4 exR4 = simd::make_float4(0, 0, 0, 1);
  // injectBug: the PRE-transpose expectation (translation in Row4, W-column zero) — the SAME tooth shape
  // the matrix golden uses: the correct TRANSPOSED emit then FAILS against it.
  if (injectBug) {
    exR1 = simd::make_float4(1, 0, 0, 0);
    exR2 = simd::make_float4(0, 1, 0, 0);
    exR3 = simd::make_float4(0, 0, 1, 0);
    exR4 = simd::make_float4(1, 2, 3, 1);
  }
  bool match = haveFour && f4Near(got[0], exR1, eps) && f4Near(got[1], exR2, eps) &&
               f4Near(got[2], exR3, eps) && f4Near(got[3], exR4, eps);
  ok = ok && match;

  // EMPTY-LIST leg (only on the clean run): a wired-but-empty buffer → identity (faithful passthrough).
  if (!injectBug) {
    ResidentEvalGraph g2;
    ResidentNode rn2 = rn;
    g2.nodes.push_back(rn2);
    g2.byPath["1"] = 0;
    SwPoint dummy{};
    cookPointValueOutputNodes(g2, ResidentEvalCtx{}, stubAccessor(&dummy, 0));  // count 0 → identity
    const NodeSpec* s = findSpec("PointToMatrix");
    int outPort = -1;
    for (size_t i = 0; s && i < s->ports.size(); ++i)
      if (!s->ports[i].isInput && s->ports[i].dataType == "ColorList") { outPort = (int)i; break; }
    auto it = g2.nodes[0].extColorOut.find(outPort);
    bool ident = it != g2.nodes[0].extColorOut.end() && it->second.size() == 4 &&
                 f4Near(it->second[0], simd::make_float4(1, 0, 0, 0), eps) &&
                 f4Near(it->second[3], simd::make_float4(0, 0, 0, 1), eps);
    ok = ok && ident;
    std::printf("[selftest-pointtomatrixemit] emptyList->identity=%d\n", ident ? 1 : 0);
  }

  if (haveFour)
    std::printf("[selftest-pointtomatrixemit] row0=(%.1f,%.1f,%.1f,%.1f) row3=(%.1f,%.1f,%.1f,%.1f)%s -> %s\n",
                got[0].x, got[0].y, got[0].z, got[0].w, got[3].x, got[3].y, got[3].z, got[3].w,
                injectBug ? " (injectBug->pre-transpose)" : "", ok ? "PASS" : "FAIL");
  else
    std::printf("[selftest-pointtomatrixemit] extColorOut produced %zu rows (want 4) -> FAIL\n", got.size());
  return ok ? 0 : 1;
}

int runGetPointDataFromListGolden(bool injectBug) {
  const float eps = 1e-4f;
  bool ok = true;

  // Resident GetPointDataFromList with a WIRED DataList + ItemIndex=4 (Constant). A 3-point list; index
  // 4 mod 3 == 1 → point[1]. Proves the modulo wrap (GetPointDataFromList.cs:40 index.Mod(N)).
  ResidentEvalGraph g;
  ResidentNode rn;
  rn.path = "1";
  rn.opType = "GetPointDataFromList";
  {
    ResidentInput ri;
    ri.slotId = "DataList";
    ri.driver = ResidentInput::Driver::Connection;
    ri.srcNodePath = "src";
    rn.inputs.push_back(ri);
  }
  {
    ResidentInput ri;
    ri.slotId = "ItemIndex";
    ri.driver = ResidentInput::Driver::Constant;
    ri.constant = 4.0f;  // 4 mod 3 == 1
    rn.inputs.push_back(ri);
  }
  g.nodes.push_back(rn);
  g.byPath["1"] = 0;

  // 3 known points; point[1] = Position=(10,20,30), FX1(W)=7, Orientation=(0.1,0.2,0.3,0.4).
  SwPoint pts[3]{};
  pts[0].Position = {0, 0, 0};
  pts[1].Position = {10.0f, 20.0f, 30.0f};
  pts[1].FX1 = 7.0f;
  pts[1].Rotation = simd::make_float4(0.1f, 0.2f, 0.3f, 0.4f);
  pts[2].Position = {99, 99, 99};
  cookPointValueOutputNodes(g, ResidentEvalCtx{}, stubAccessor(pts, 3));

  const float* o = g.nodes[0].extOut;
  // HAND-DERIVED: Position→extOut[0..2]=(10,20,30), W→extOut[3]=7, Orientation→extOut[4..7]=(.1,.2,.3,.4).
  float exPos[3] = {10.0f, 20.0f, 30.0f};
  float exW = 7.0f;
  float exOri[4] = {0.1f, 0.2f, 0.3f, 0.4f};
  // injectBug: assert index 4 → point[4 mod 3 BUT computed as raw 4 clamped to 2] = point[2]=(99,99,99).
  // i.e. the WRONG (non-modulo / wrong-wrap) expectation; the CORRECT modulo emit (point[1]) FAILS it.
  if (injectBug) {
    exPos[0] = 99.0f; exPos[1] = 99.0f; exPos[2] = 99.0f;
  }
  bool posOk = std::fabs(o[0] - exPos[0]) < eps && std::fabs(o[1] - exPos[1]) < eps &&
               std::fabs(o[2] - exPos[2]) < eps;
  bool wOk = std::fabs(o[3] - exW) < eps;
  bool oriOk = std::fabs(o[4] - exOri[0]) < eps && std::fabs(o[5] - exOri[1]) < eps &&
               std::fabs(o[6] - exOri[2]) < eps && std::fabs(o[7] - exOri[3]) < eps;
  // On injectBug the W/Orientation legs would still pass for point[1]; force the bite onto Position only
  // (the modulo-wrap mechanism), so the wrong-index expectation reliably RED-s the correct emit.
  ok = injectBug ? (ok && posOk) : (ok && posOk && wOk && oriOk);

  std::printf("[selftest-getpointdatafromlist] pos=(%.1f,%.1f,%.1f) W=%.1f ori=(%.1f,%.1f,%.1f,%.1f)%s -> %s\n",
              o[0], o[1], o[2], o[3], o[4], o[5], o[6], o[7],
              injectBug ? " (injectBug->wrong-index)" : "", ok ? "PASS" : "FAIL");
  return ok ? 0 : 1;
}

// Golden registrars — DIRECT push into the value-op selftest sink (zero shared-file edit; selftests.cpp
// iterates valueOpSelfTests() for --selftest-<name> / -bug + --bite enumeration). Mirror of
// resident_matrix_output_cook.cpp's TransformMatrixGoldenRegistrar.
struct PointToMatrixEmitGoldenRegistrar {
  PointToMatrixEmitGoldenRegistrar() {
    valueOpSelfTests().push_back({"pointtomatrixemit", runPointToMatrixEmitGolden});
    valueOpSelfTests().push_back({"getpointdatafromlist", runGetPointDataFromListGolden});
  }
};
static const PointToMatrixEmitGoldenRegistrar _reg_pointtomatrixemit_golden;

}  // namespace

}  // namespace sw
