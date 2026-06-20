// valuestotexture_golden — --selftest-valuestotexture. CHAIN golden for the FloatList→Texture
// rail-crossing (Slice B): build two FloatsToList producers (lists [1,2,3] and [4,5]) wired into
// ValuesToTexture's Values MultiInput, cook ValuesToTexture as the terminal, read its OP-OWNED
// R32Float texture back via PointGraph::target()->getBytes, and assert each texel equals the
// closed-form TiXL transform pow((list[row][i] + offset) * gain, pow).
//
// What this proves end-to-end:
//   (1) the FloatList host lists flow FloatsToList → ValuesToTexture across the rail (the tex-walker
//       gathers the Values MultiInput into TexCookCtx::inputLists in wire-declaration order);
//   (2) the op allocates its OWN R32Float, DATA-SIZED texture (3×2) — the tex-output fork — and the
//       driver uploads + displays it (NOT the RGBA8 resolution-pinned ensureTex one);
//   (3) the transform/dims/Horizontal fill order match ValuesToTexture.cs byte-for-byte.
//
// Derived golden (Horizontal, Offset=1, Gain=2, Pow=2):
//   sampleCount = max(3,2) = 3, listCount = 2 → width=3, height=2.
//   row0 ([1,2,3]): pow((1+1)*2,2)=16, pow((2+1)*2,2)=36, pow((3+1)*2,2)=64
//   row1 ([4,5]):   pow((4+1)*2,2)=100, pow((5+1)*2,2)=144, short-cell(row1,col2)=0
//
// injectBug routes through valuesToTextureInjectBug() → the REAL cook writes 0 into cell[0]
// (texel(0,0), expected 16) → readback 0 ≠ 16 → RED (teeth on the actual cook path).
#include <cmath>
#include <cstdio>
#include <vector>

#include <Foundation/Foundation.hpp>
#include <Metal/Metal.hpp>

#include "runtime/eval_context.h"  // EvaluationContext
#include "runtime/graph.h"         // Graph/Node/Connection/pinId
#include "runtime/point_graph.h"   // PointGraph::cook + target()

namespace sw {

bool& valuesToTextureInjectBug();  // point_ops_valuestotexture.cpp

namespace {

bool nearf(float a, float b) { return std::fabs(a - b) < 1e-3f; }

}  // namespace

int runValuesToTextureSelfTest(bool injectBug) {
  NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();

  MTL::Device* dev = MTL::CreateSystemDefaultDevice();
  MTL::CommandQueue* q = dev->newCommandQueue();
  PointGraph pg(dev, /*lib=*/nullptr, q, 64, 64);

  // --- Build the flat graph -------------------------------------------------------------------
  // Node 10 = ValuesToTexture (Values is port 0, the FloatList MultiInput).
  // Node 1 = FloatsToList([1,2,3]) (row0); Node 2 = FloatsToList([4,5]) (row1).
  // Const nodes feed each FloatsToList's scalar Float MultiInput (port 0).
  Graph g;
  Node vt; vt.id = 10; vt.type = "ValuesToTexture";
  vt.params["Offset"] = 1.0f; vt.params["Gain"] = 2.0f; vt.params["Pow"] = 2.0f;
  vt.params["Direction"] = 0.0f;  // Horizontal
  g.nodes.push_back(vt);
  const int vtValuesPin = pinId(10, /*Values port*/ 0);

  auto addFloatsToList = [&](int ftlId, const std::vector<float>& vals, int constBase) {
    Node ftl; ftl.id = ftlId; ftl.type = "FloatsToList";
    g.nodes.push_back(ftl);
    const int ftlInputPin = pinId(ftlId, /*Input port*/ 0);
    int cid = constBase;
    for (float v : vals) {
      Node c; c.id = cid; c.type = "Const"; c.params["value"] = v;
      g.nodes.push_back(c);
      // Const "out" is port 1 (port 0 = "value" input). Wire each into FloatsToList.Input in order.
      g.connections.push_back({1000 + cid, pinId(cid, /*out*/ 1), ftlInputPin});
      ++cid;
    }
    // Wire FloatsToList "out" (port 1: port 0 = Input MultiInput, port 1 = out) → ValuesToTexture.Values.
    g.connections.push_back({2000 + ftlId, pinId(ftlId, /*out*/ 1), vtValuesPin});
  };
  // Wire-declaration order = row order: FloatsToList #1 (row0) before #2 (row1).
  addFloatsToList(1, {1.0f, 2.0f, 3.0f}, /*constBase=*/100);
  addFloatsToList(2, {4.0f, 5.0f}, /*constBase=*/200);

  EvaluationContext ctx{};
  ctx.frameIndex = 0; ctx.time = 0.0f; ctx.deltaTime = 1.0f / 60.0f;

  valuesToTextureInjectBug() = injectBug;
  pg.cook(g, ctx, nullptr, /*targetNodeId=*/10);
  valuesToTextureInjectBug() = false;

  // --- Read back the R32Float op-owned texture ------------------------------------------------
  bool ok = true;
  MTL::Texture* tex = pg.target();
  const uint32_t wantW = 3, wantH = 2;
  uint32_t w = tex ? (uint32_t)tex->width() : 0;
  uint32_t h = tex ? (uint32_t)tex->height() : 0;
  if (!tex || w != wantW || h != wantH) {
    std::printf("[selftest-valuestotexture] FAIL: dims=%ux%u want %ux%u\n", w, h, wantW, wantH);
    ok = false;
  } else {
    // R32Float: one float per texel, rowPitch = w * sizeof(float).
    std::vector<float> px((size_t)w * h, -1.0f);
    tex->getBytes(px.data(), w * sizeof(float), MTL::Region::Make2D(0, 0, w, h), 0);
    // Expected: row0=[16,36,64], row1=[100,144,0]. (px is row-major: row*w + col.)
    const float want[2][3] = {{16.0f, 36.0f, 64.0f}, {100.0f, 144.0f, 0.0f}};
    for (uint32_t row = 0; row < h && ok; ++row)
      for (uint32_t col = 0; col < w; ++col) {
        float got = px[(size_t)row * w + col];
        if (!nearf(got, want[row][col])) {
          std::printf("[selftest-valuestotexture] texel(%u,%u)=%.3f want %.3f -> FAIL\n", col, row,
                      got, want[row][col]);
          ok = false;
        }
      }
    if (ok)
      std::printf("[selftest-valuestotexture] 3x2 R32Float row0=[16,36,64] row1=[100,144,0] match\n");
  }

  q->release();
  dev->release();
  pool->release();

  std::printf("[selftest-valuestotexture] %s\n", ok ? "PASS" : "FAIL");
  return ok ? 0 : 1;
}

}  // namespace sw
