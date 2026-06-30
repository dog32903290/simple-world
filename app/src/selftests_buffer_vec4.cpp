// selftests_buffer_vec4 — the VEC4-CURRENCY bridge GATE (--selftest-buffer-vec4 + -bug).
//
// Closes the fork `floatstobuffer-vec4-from-nodeparams`: a WIRED matrix/Vector4[] producer feeding
// FloatsToBuffer.Vec4Params must flow its 16 floats (4 rows .X.Y.Z.W, matrices-first per FloatsToBuffer.cs)
// into the GPU buffer on BOTH cook legs. The matrix currency in sw is the ColorList channel (4 float4 rows =
// fork-matrix-as-4-vec4-on-extColorOut); this gate drives Vec4Params from a REAL ColorList wire.
//
// G1 vec4-currency (BOTH legs, the gather-symmetry gate): ColorsToList(4 distinct rows) -> FloatsToBuffer.
//    Vec4Params, + Const scalars -> Params. Cook flat (direct) AND resident (cookColorListNodes settles
//    ColorsToList.extColorOut, mirroring cook_host_values.cpp's producer-before-cookResident order), assert
//    the resident SwBuffer is byte-identical to the flat one AND the bytes ARE [16 matrix floats | scalars].
//    RED before the bridge: flat falls back to Node::params (no matrix), resident gathers none → no block.
// G2 transformmatrix-to-buffer (RESIDENT, the production keystone): TransformMatrix(known SRT) ->
//    FloatsToBuffer.Vec4Params. cookMatrixOutputNodes settles the 4 transposed SRT rows; cookResident gathers
//    them; assert bytes[0..15] == the hand-derived closed-form rows (the SAME values --selftest-transformmatrix
//    TiXL-verifies → resident==TiXL, non-circular). This is the actual 17-compound production wire.
//
// -bug (bufferInjectBug): FloatsToBuffer drops the LAST scalar float → count short → the count asserts FAIL on
//    the real cook path on both legs (not a flipped expected value). run_all_selftests.sh --bite scans it.
#include <array>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include <Foundation/Foundation.hpp>
#include <Metal/Metal.hpp>
#include <simd/simd.h>

#include "runtime/buffer_op_registry.h"   // bufferInjectBug
#include "runtime/eval_context.h"         // EvaluationContext
#include "runtime/graph.h"                // Graph/Node/Connection/pinId
#include "runtime/graph_bridge.h"         // libFromGraph (flat Graph -> SymbolLibrary, paths == ids)
#include "runtime/point_graph.h"          // PointGraph + debugCookedSwBuffer / residentSwBufferFor
#include "runtime/resident_eval_graph.h"  // buildEvalGraph, ResidentEvalCtx
#include "runtime/resident_value_cooks.h" // cookColorListNodes / cookMatrixOutputNodes (producer passes)
#include "runtime/selftest_registry.h"    // REGISTER_SELFTESTS
#include "runtime/sw_buffer.h"            // SwBuffer

namespace sw {
namespace {

// 4 known distinct rows for the ColorsToList matrix stand-in (NOT identity — distinct values catch a
// row/column swap or a wrong-offset gather). row r = (4r+1, 4r+2, 4r+3, 4r+4).
const float kRows16[16] = {
    1, 2, 3, 4,
    5, 6, 7, 8,
    9, 10, 11, 12,
    13, 14, 15, 16,
};

// Append a Const node (id assigned from *nextId) producing `v`, wired to input pin `toPin`. Const's value
// output is port 1 (mirror selftests_buffer.cpp). Connection ids ascend from *nextConn so per-pin wire order
// == call order.
void wireConst(Graph& g, int* nextId, int* nextConn, float v, int toPin) {
  Node c; c.id = (*nextId)++; c.type = "Const"; c.params["value"] = v;
  g.nodes.push_back(c);
  g.connections.push_back({(*nextConn)++, pinId(c.id, /*out*/ 1), toPin});
}

// Build { ColorsToList(id 10, 4 rows from kRows16) -> FloatsToBuffer(id 1).Vec4Params(port 1);
//         Const(scalars[i]) -> FloatsToBuffer.Params(port 2) }. ColorsToList ports: Colors.x/.y/.z/.w =
// 0..3 (Float MultiInput), out = 4 (ColorList). The i-th color = (x[i],y[i],z[i],w[i]); wire each channel's
// 4 values in color order. Returns via g; terminal = FloatsToBuffer id 1.
void buildColorsToBuffer(Graph& g, const std::vector<float>& scalars) {
  Node f2b; f2b.id = 1; f2b.type = "FloatsToBuffer";
  g.nodes.push_back(f2b);
  Node ctl; ctl.id = 10; ctl.type = "ColorsToList";
  g.nodes.push_back(ctl);

  int nextId = 100, nextConn = 200;
  // 4 component channels (port 0..3); per channel, 4 colors in order → row r component = kRows16[r*4 + ch].
  for (int ch = 0; ch < 4; ++ch) {
    const int chPin = pinId(10, /*portIndex=*/ch);
    for (int r = 0; r < 4; ++r) wireConst(g, &nextId, &nextConn, kRows16[r * 4 + ch], chPin);
  }
  // ColorsToList.out (port 4) → FloatsToBuffer.Vec4Params (port 1).
  g.connections.push_back({300, pinId(10, /*out*/ 4), pinId(1, /*Vec4Params*/ 1)});
  // Const scalars → FloatsToBuffer.Params (port 2), in wire order.
  const int paramsPin = pinId(1, /*Params*/ 2);
  for (float s : scalars) wireConst(g, &nextId, &nextConn, s, paramsPin);
}

// Build { TransformMatrix(id 20, known SRT) -> FloatsToBuffer(id 1).Vec4Params(port 1); Const(scalar) ->
// .Params(port 2) }. TransformMatrix.Result (ColorList) is output port 0. Known SRT: Scale=(2,3,4),
// UniformScale=1, Translation=(5,6,7), rotation=Identity (PitchYawRoll, all 0). Closed-form transposed rows
// (see --selftest-transformmatrix): Row1=(2,0,0,5) Row2=(0,3,0,6) Row3=(0,0,4,7) Row4=(0,0,0,1). The trailing
// scalar gives G2 a real BITE: -bug drops it → count 17→16 → the count assert FAILs (the matrix block is
// matrices-first so its bytes still assert — the production-keystone matrix is checked, not green-only).
void buildTransformMatrixToBuffer(Graph& g, float scalar) {
  Node f2b; f2b.id = 1; f2b.type = "FloatsToBuffer";
  g.nodes.push_back(f2b);
  Node tm; tm.id = 20; tm.type = "TransformMatrix";
  tm.params["Scale.x"] = 2.0f; tm.params["Scale.y"] = 3.0f; tm.params["Scale.z"] = 4.0f;
  tm.params["UniformScale"] = 1.0f;
  tm.params["Translation.x"] = 5.0f; tm.params["Translation.y"] = 6.0f; tm.params["Translation.z"] = 7.0f;
  g.nodes.push_back(tm);
  // TransformMatrix.Result (out port 0) → FloatsToBuffer.Vec4Params (port 1).
  g.connections.push_back({400, pinId(20, /*Result out*/ 0), pinId(1, /*Vec4Params*/ 1)});
  int nextId = 500, nextConn = 600;
  wireConst(g, &nextId, &nextConn, scalar, pinId(1, /*Params*/ 2));  // trailing scalar (the -bug tooth)
}

const float* bufFloats(const SwBuffer* b) {
  return (b && b->bytes) ? (const float*)b->bytes->contents() : nullptr;
}

}  // namespace

int runBufferVec4SelfTest(bool injectBug) {
  NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();
  MTL::Device* dev = MTL::CreateSystemDefaultDevice();
  MTL::CommandQueue* q = dev->newCommandQueue();
  bool ok = true;

  EvaluationContext ctx{}; ctx.frameIndex = 0; ctx.time = 0.0f; ctx.deltaTime = 1.0f / 60.0f;
  ResidentEvalCtx rctx;

  // ---- G1 — vec4-currency BOTH legs (ColorsToList → FloatsToBuffer), flat==resident byte-parity. --------
  {
    const std::vector<float> scalars = {100.0f, 200.0f};
    const uint32_t wantCount = 16u + (uint32_t)scalars.size();  // 16 matrix + 2 scalars = 18

    // FLAT leg.
    Graph gf; buildColorsToBuffer(gf, scalars);
    PointGraph pgFlat(dev, nullptr, q, 64, 64);
    bufferInjectBug() = injectBug;
    pgFlat.cook(gf, ctx, nullptr, /*terminal=*/1);
    bufferInjectBug() = false;
    const SwBuffer* flat = pgFlat.debugCookedSwBuffer(1);

    // RESIDENT leg (production): settle ColorsToList.extColorOut first (cook_host_values order), then cook.
    Graph gr; buildColorsToBuffer(gr, scalars);
    SymbolLibrary slib = libFromGraph(gr);
    ResidentEvalGraph rg = buildEvalGraph(slib, slib.rootId);
    std::map<std::string, std::vector<simd::float4>> colorState;
    cookColorListNodes(rg, rctx, colorState);
    PointGraph pgRes(dev, nullptr, q, 64, 64);
    bufferInjectBug() = injectBug;
    pgRes.cookResident(rg, ctx, nullptr, "1");
    bufferInjectBug() = false;
    const SwBuffer* res = pgRes.residentSwBufferFor("1");

    bool flatOK = flat && flat->bytes && flat->elementCount == wantCount && flat->elementStride == 4u;
    // Flat bytes ARE [16 rows | scalars].
    bool flatBytes = flatOK;
    if (flatBytes) {
      const float* d = bufFloats(flat);
      for (int k = 0; k < 16 && flatBytes; ++k) if (d[k] != kRows16[k]) flatBytes = false;
      for (size_t i = 0; i < scalars.size() && flatBytes; ++i) if (d[16 + i] != scalars[i]) flatBytes = false;
    }
    // Resident byte-identical to flat.
    bool resOK = res && res->bytes && flat && flat->bytes &&
                 res->elementCount == flat->elementCount && res->elementStride == flat->elementStride;
    if (resOK) resOK = std::memcmp(res->bytes->contents(), flat->bytes->contents(),
                                   res->elementCount * res->elementStride) == 0;
    bool pass = flatOK && flatBytes && resOK;
    ok = ok && pass;
    std::printf("[selftest-buffer-vec4] G1 flat{count=%u stride=%u bytes=%d} resident{count=%u} "
                "resOK=%d (want %u) -> %s\n",
                flat ? flat->elementCount : 0, flat ? flat->elementStride : 0, flatBytes ? 1 : 0,
                res ? res->elementCount : 0, resOK ? 1 : 0, wantCount, pass ? "PASS" : "FAIL");
  }

  // ---- G2 — TransformMatrix → FloatsToBuffer (RESIDENT production keystone). --------------------------
  {
    const float kScalar = 99.0f;
    Graph gr; buildTransformMatrixToBuffer(gr, kScalar);
    SymbolLibrary slib = libFromGraph(gr);
    ResidentEvalGraph rg = buildEvalGraph(slib, slib.rootId);
    cookMatrixOutputNodes(rg, rctx);  // settle the 4 transposed SRT rows onto TransformMatrix.extColorOut
    PointGraph pgRes(dev, nullptr, q, 64, 64);
    bufferInjectBug() = injectBug;
    pgRes.cookResident(rg, ctx, nullptr, "1");
    bufferInjectBug() = false;
    const SwBuffer* res = pgRes.residentSwBufferFor("1");

    // Closed-form expected (Scale=(2,3,4), T=(5,6,7), rot=I, transposed): translation in the W column.
    const float kExpect[16] = {
        2, 0, 0, 5,
        0, 3, 0, 6,
        0, 0, 4, 7,
        0, 0, 0, 1,
    };
    const uint32_t wantCount = 16u + 1u;  // 16 matrix + 1 scalar; -bug → 16 → count assert FAILs (the bite)
    bool haveBuf = res && res->bytes && res->elementCount == wantCount && res->elementStride == 4u;
    bool bytesOK = haveBuf;
    if (bytesOK) {
      const float* d = bufFloats(res);
      for (int k = 0; k < 16 && bytesOK; ++k)
        if (std::fabs(d[k] - kExpect[k]) > 1e-4f) bytesOK = false;
      if (bytesOK && std::fabs(d[16] - kScalar) > 1e-4f) bytesOK = false;  // scalar after the matrix block
    }
    bool pass = haveBuf && bytesOK;  // -bug drops the scalar → count 16 ≠ 17 → FAIL (real tooth)
    ok = ok && pass;
    std::printf("[selftest-buffer-vec4] G2 TransformMatrix resident{count=%u(want %u)} bytesOK=%d -> %s\n",
                res ? res->elementCount : 0, wantCount, bytesOK ? 1 : 0, pass ? "PASS" : "FAIL");
  }

  // ---- G3 — UNRESOLVED matrix source is DETECTED, not silent (the gate). -----------------------------
  // Const has no ColorList output / never writes extColorOut, so wiring Const → Vec4Params yields NO matrix
  // rows. The gather must NOTE it (bufferUnresolvedMatrixSources()++ + warn-once) on BOTH legs instead of
  // silently emitting a zero matrix block. Asserts the counter fires; removing the detection → counter 0 → RED.
  // (Independent of injectBug — the gate is structural: it proves the detection exists, not -bug corruption.)
  {
    Graph g;
    Node f2b; f2b.id = 1; f2b.type = "FloatsToBuffer"; g.nodes.push_back(f2b);
    Node cst; cst.id = 30; cst.type = "Const"; cst.params["value"] = 1.0f; g.nodes.push_back(cst);
    g.connections.push_back({700, pinId(30, /*Const out*/ 1), pinId(1, /*Vec4Params*/ 1)});

    bufferUnresolvedMatrixSources() = 0;
    PointGraph pgFlat(dev, nullptr, q, 64, 64);
    pgFlat.cook(g, ctx, nullptr, /*terminal=*/1);
    uint32_t flatNotes = bufferUnresolvedMatrixSources();

    bufferUnresolvedMatrixSources() = 0;
    SymbolLibrary slib = libFromGraph(g);
    ResidentEvalGraph rg = buildEvalGraph(slib, slib.rootId);  // no producer pass: Const writes nothing
    PointGraph pgRes(dev, nullptr, q, 64, 64);
    pgRes.cookResident(rg, ctx, nullptr, "1");
    uint32_t resNotes = bufferUnresolvedMatrixSources();
    bufferUnresolvedMatrixSources() = 0;

    bool pass = flatNotes >= 1 && resNotes >= 1;  // both legs must NOTICE the unresolved source
    ok = ok && pass;
    std::printf("[selftest-buffer-vec4] G3 unresolved-source notes flat=%u resident=%u (want >=1 each) -> %s\n",
                flatNotes, resNotes, pass ? "PASS" : "FAIL");
  }

  q->release(); dev->release(); pool->release();
  std::printf("[selftest-buffer-vec4] %s\n", ok ? "PASS" : "FAIL");
  return ok ? 0 : 1;
}

// Self-register the vec4-currency GATE row (orderBase 624, just above the WO-E resident gate at 622).
REGISTER_SELFTESTS(/*orderBase=*/624,
    {"buffer-vec4", runBufferVec4SelfTest});

}  // namespace sw
