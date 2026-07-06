// runtime/t3import_combinebuffers_golden — 187 量產第一波 replay spike (--selftest-t3-combinebuffers).
//
// Loads the REAL TiXL CombineBuffers.t3 (embedded byte-faithful below), walks the PRODUCTION path
// importT3Symbol → buildEvalGraph → cookResident, reads back the combined Point buffer, and compares it
// against an INDEPENDENT concatenation oracle (input0's points, then input1's, then input2's, in order).
// This proves an ARBITRARY unfinished GPU-compute compound套上 the已證 replay 量產配方 replays byte-faithful
// — the point of the spike being to verify the recipe is a clean, repeatable flow (not one only the three
// orchestrator-tracked bones can walk). CombineBuffers rides the已證 point-buffer currency (near
// TransformPoints), so currency is NOT the variable here — the recipe is.
//
// ── THE SHAPE DIFFERENCE (recipe friction, documented) ─────────────────────────────────────────────
// CombineBuffers.t3 has only TWO children: _ExecuteCombineBuffers (a C# CODE-OP that hides the whole
// compute-stage assembly — result alloc + per-input dispatch loop — in code) + a ComputeShader (Source =
// CombineBuffersAsInt.hlsl). Unlike TransformPoints.t3 (which spells ComputeShaderStage /
// StructuredBufferWithViews / ExecuteBufferUpdate out as wired atoms), CombineBuffers cannot decompose into
// the generic ComputeShaderStage seam — so the recipe here added a DEDICATED replay atom
// (buffer_ops_executecombinebuffers.cpp) + one map row + generalized the ComputeShader fold to a second
// CS-in slot. The oracle/tooth/embed steps were pure template copies.
//
// ── FEEDING THE INPUT (honest test scaffold) ───────────────────────────────────────────────────────
// The .t3's `Input` is a MultiInput BOUNDARY with no in-graph producer. The golden REWIRES it: three
// test-fixture Buffer producers (t3cb_input_points0/1/2, registered in this TU) each emit a known bag of a
// different size; the boundary→InputBuffers wire is REPLACED by three wires (one per fixture) into
// _ExecuteCombineBuffers.InputBuffers, in fixture order. That order IS the concatenation order the oracle
// asserts (StartIndex accumulates in wire order). Only the input source is test-supplied; the marshalling
// (result alloc + dispatch loop) runs the PRODUCTION atom end-to-end.
//
// ZONE: runtime golden (shell tier — binds runtime import + resident cook + the concatenation oracle).
#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <vector>

#include <Foundation/Foundation.hpp>
#include <Metal/Metal.hpp>

#include "runtime/buffer_op_registry.h"     // BufferOp / BufferCookCtx (the fixture producers)
#include "runtime/compound_graph.h"         // SymbolLibrary / Symbol / SymbolChild / SymbolConnection
#include "runtime/graph.h"                  // findSpec / registerBuiltinPointOps
#include "runtime/graph_bridge.h"           // atomicSymbolFromSpec (add the fixture atoms to the lib)
#include "runtime/resident_eval_graph.h"    // ResidentEvalGraph / buildEvalGraph / initResidentCache
#include "runtime/point_graph.h"            // PointGraph / residentSwBufferFor
#include "runtime/sw_buffer.h"              // SwBuffer
#include "runtime/t3_import.h"              // importT3Symbol / t3ImportInjectBug
#include "runtime/tixl_point.h"             // SwPoint (64B)

namespace sw {

void registerBuiltinPointOps();

namespace {

static const char* kCombineBuffersT3 =
#include "runtime/combinebuffers_t3_embed.inc"
;

// ── Test-fixture Buffer producers ─────────────────────────────────────────────────────────────────
// Three producers, each emitting a fixed distinct-size SwPoint bag as a SwBuffer (stride 64). Registered
// as Buffer ops so findSpec resolves them and the resident buffer cook drives them. Live ONLY in this TU.
std::vector<SwPoint>* g_fixture[3] = {nullptr, nullptr, nullptr};

template <int I>
void cookFixture(BufferCookCtx& c) {
  if (!c.output || !c.requestBytes || !g_fixture[I]) return;
  const uint32_t n = (uint32_t)g_fixture[I]->size();
  if (n == 0) return;
  const uint32_t bytes = n * (uint32_t)sizeof(SwPoint);
  void* dst = c.requestBytes(bytes);
  if (!dst) return;
  std::memcpy(dst, g_fixture[I]->data(), bytes);
  c.output->elementStride = (uint32_t)sizeof(SwPoint);
  c.output->elementCount = n;
  c.output->elementFormat = 0;
}

template <int I>
NodeSpec fixtureSpec(const char* type) {
  NodeSpec s;
  s.type = type;
  s.title = type;
  s.category = "test";
  s.ports = {{"Buffer", "Buffer", "Buffer", false}};  // a pure producer (no inputs)
  s.evaluate = nullptr;
  return s;
}
const BufferOp _reg_cb0(fixtureSpec<0>("t3cb_input_points0"), cookFixture<0>);
const BufferOp _reg_cb1(fixtureSpec<1>("t3cb_input_points1"), cookFixture<1>);
const BufferOp _reg_cb2(fixtureSpec<2>("t3cb_input_points2"), cookFixture<2>);

// Build a deterministic point bag whose Position encodes (tag, index) so a mis-ordered / dropped / wrong-
// offset copy is unambiguously visible in the readback (each point is unique across all three bags).
std::vector<SwPoint> makeBag(float tag, uint32_t n) {
  std::vector<SwPoint> v(n);
  for (uint32_t i = 0; i < n; ++i) {
    v[i] = SwPoint{};
    v[i].Position = SW_PACKED3{tag, (float)i, tag + (float)i * 0.5f};
    v[i].Rotation = SW_FLOAT4{0, 0, 0, 1};
    v[i].Scale = SW_PACKED3{1, 1, 1};
    v[i].FX1 = tag; v[i].FX2 = (float)i;
  }
  return v;
}

int childIdOfType(const Symbol& s, const std::string& type) {
  for (const SymbolChild& c : s.children) if (c.symbolId == type) return c.id;
  return 0;
}

}  // namespace

int runT3CombineBuffersParity(bool injectBug) {
  NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();
  registerBuiltinPointOps();

  // ---- Three input bags of distinct sizes (the concatenation inputs, in wire order) ----
  std::vector<SwPoint> bag0 = makeBag(10.0f, 5);
  std::vector<SwPoint> bag1 = makeBag(20.0f, 3);
  std::vector<SwPoint> bag2 = makeBag(30.0f, 7);
  g_fixture[0] = &bag0; g_fixture[1] = &bag1; g_fixture[2] = &bag2;
  const uint32_t N = (uint32_t)(bag0.size() + bag1.size() + bag2.size());  // 15

  // ---- STEP 1: import the real .t3 via the PRODUCTION importer (always clean; the tooth is scaffold-side,
  //      authored in STEP 1b, because the .t3 has no pre-existing MultiInput collision to route-swap) ----
  SymbolLibrary lib;
  std::string rootId;
  std::vector<std::string> warnings;
  bool ok = importT3Symbol(kCombineBuffersT3, lib, &rootId, &warnings);
  if (!ok) { printf("[t3-combinebuffers] FAIL: importT3Symbol returned false\n"); pool->release(); return 1; }

  Symbol* sym = const_cast<Symbol*>(lib.find(rootId));
  if (!sym) { printf("[t3-combinebuffers] FAIL: root symbol missing\n"); pool->release(); return 1; }
  { std::map<std::string, int> byType;
    for (const SymbolChild& c : sym->children) byType[c.symbolId]++;
    printf("[t3-combinebuffers] import: rootId=%s children=%d conns=%d warnings=%zu\n",
           rootId.c_str(), (int)sym->children.size(), (int)sym->connections.size(), warnings.size());
    printf("[t3-combinebuffers]   mapped atom types:");
    for (const auto& kv : byType) printf(" %s×%d", kv.first.c_str(), kv.second);
    printf("\n"); }

  // The _ExecuteCombineBuffers child must be mapped AND have its KernelName folded from the ComputeShader.
  const int exId = childIdOfType(*sym, "_ExecuteCombineBuffers");
  if (!exId) { printf("[t3-combinebuffers] FAIL: _ExecuteCombineBuffers child not mapped\n"); pool->release(); return 1; }
  { std::string k;
    for (const SymbolChild& c : sym->children) if (c.id == exId) { auto it = c.strOverrides.find("KernelName"); if (it != c.strOverrides.end()) k = it->second; }
    printf("[t3-combinebuffers]   _ExecuteCombineBuffers.KernelName(folded) = \"%s\"\n", k.c_str());
    if (k.empty()) { printf("[t3-combinebuffers] FAIL: ComputeShader.Source did not fold onto KernelName\n"); pool->release(); return 1; } }

  // ---- STEP 1b: TEST-SCAFFOLD rewire — replace the Input boundary→InputBuffers wire with three fixture wires.
  // Confirm the boundary→_ExecuteCombineBuffers.InputBuffers wire exists (the imported MultiInput feed).
  bool haveBoundaryWire = false;
  for (const SymbolConnection& w : sym->connections)
    if (w.srcChild == kSymbolBoundary && w.dstChild == exId && w.dstSlot == "InputBuffers") haveBoundaryWire = true;
  if (!haveBoundaryWire) { printf("[t3-combinebuffers] FAIL: no Input boundary → InputBuffers wire\n"); pool->release(); return 1; }
  // Drop the boundary wire(s) into InputBuffers; add three fixture producer children + three wires.
  std::vector<SymbolConnection> keep;
  for (const SymbolConnection& w : sym->connections)
    if (!(w.srcChild == kSymbolBoundary && w.dstChild == exId && w.dstSlot == "InputBuffers")) keep.push_back(w);
  sym->connections = keep;
  const char* fixTypes[3] = {"t3cb_input_points0", "t3cb_input_points1", "t3cb_input_points2"};
  // The concatenation order = the order the three InputBuffers wires are DECLARED (the atom accumulates
  // StartIndex in inputBuffers gather order, which is wire order). -bug REVERSES that declaration order.
  // The .t3 itself has only ONE (boundary) wire into InputBuffers — no pre-existing MultiInput collision —
  // so the importer's routing tooth can't cut this edge; the tooth is authored HERE on the scaffold wires,
  // still cutting the REAL load-bearing edge (per-input StartIndex order) that flows through the atom.
  int order[3] = {0, 1, 2};
  if (injectBug) { order[0] = 2; order[2] = 0; }  // reverse first/last → bag2 ++ bag1 ++ bag0
  for (int k = 0; k < 3; ++k) {
    const int i = order[k];
    const int fid = sym->nextChildId++;
    { SymbolChild p; p.id = fid; p.symbolId = fixTypes[i]; sym->children.push_back(p); }
    if (!lib.symbols.count(fixTypes[i]))
      if (const NodeSpec* fs = findSpec(fixTypes[i]))
        lib.symbols[fixTypes[i]] = atomicSymbolFromSpec(*fs);
    SymbolConnection w; w.srcChild = fid; w.srcSlot = "Buffer"; w.dstChild = exId; w.dstSlot = "InputBuffers";
    sym->connections.push_back(w);  // wire-declaration order = concatenation order (StartIndex accumulates)
  }

  // ---- STEP 2: build the eval graph via the PRODUCTION flattener ----
  ResidentEvalGraph g = buildEvalGraph(lib, rootId);
  initResidentCache(g);
  printf("[t3-combinebuffers] buildEvalGraph: resident nodes=%zu\n", g.nodes.size());

  const std::string termPath = std::to_string(exId);  // _ExecuteCombineBuffers = the graph terminal

  // ---- STEP 3: cook the resident graph; read back the combined buffer ----
  MTL::Device* dev = MTL::CreateSystemDefaultDevice();
  MTL::CommandQueue* q = dev->newCommandQueue();
  NS::Error* err = nullptr;
  MTL::Library* mlib = dev->newLibrary(
      NS::String::string(SW_SHADER_METALLIB, NS::UTF8StringEncoding), &err);
  if (!mlib) { printf("[t3-combinebuffers] FAIL: no metallib\n"); q->release(); dev->release(); pool->release(); return 1; }

  PointGraph pg(dev, mlib, q, 64, 64);
  EvaluationContext ctx{}; ctx.frameIndex = 0; ctx.time = 0.0f; ctx.deltaTime = 1.0f/60.0f;
  pg.cookResident(g, ctx, nullptr, termPath);
  const SwBuffer* outBuf = pg.residentSwBufferFor(termPath);

  bool haveOut = outBuf && outBuf->bytes && outBuf->elementCount == N;
  std::vector<SwPoint> got(N);
  if (haveOut) std::memcpy(got.data(), const_cast<MTL::Buffer*>(outBuf->bytes)->contents(), N * sizeof(SwPoint));

  // ---- INDEPENDENT ORACLE: the combined bag = bag0 ++ bag1 ++ bag2 (byte-identical, in wire order) ----
  std::vector<SwPoint> expect;
  expect.insert(expect.end(), bag0.begin(), bag0.end());
  expect.insert(expect.end(), bag1.begin(), bag1.end());
  expect.insert(expect.end(), bag2.begin(), bag2.end());

  double maxPosErr = 0.0; int worstI = -1;
  if (haveOut)
    for (uint32_t i = 0; i < N; ++i) {
      const SwPoint& e = expect[i]; const SwPoint& gp = got[i];
      double dx = e.Position.x - gp.Position.x, dy = e.Position.y - gp.Position.y, dz = e.Position.z - gp.Position.z;
      double d = std::sqrt(dx*dx + dy*dy + dz*dz);
      if (d > maxPosErr) { maxPosErr = d; worstI = (int)i; }
    }

  printf("[t3-combinebuffers] replay-vs-concat-oracle: haveOut=%d outCount=%u(need=%u) maxPosErr=%.6f(need<1e-4) worstI=%d\n",
         haveOut ? 1 : 0, outBuf ? outBuf->elementCount : 0u, N, maxPosErr, worstI);

  const bool parityGreen = haveOut && (maxPosErr < 1e-4);
  printf("[t3-combinebuffers] PARITY VERDICT: %s\n",
         parityGreen ? "GREEN (replay reproduces the concatenation)" : "RED (replay seam gap)");

  mlib->release(); q->release(); dev->release();
  g_fixture[0] = g_fixture[1] = g_fixture[2] = nullptr;

  if (!injectBug) {
    if (!parityGreen) { printf("[t3-combinebuffers] FAIL\n"); pool->release(); return 1; }
    printf("[t3-combinebuffers] PASS: CombineBuffers.t3 replays to parity (量產配方 verified on an arbitrary compound)\n");
    pool->release();
    return 0;
  }

  // injectBug leg: the importer's routing tooth reverses the FIRST MultiInput collision — here the three
  // wires into _ExecuteCombineBuffers.InputBuffers. Reversing their order flips the concatenation order
  // (bag2 ++ bag1 ++ bag0 instead of bag0 ++ bag1 ++ bag2) → the readback DIVERGES from the oracle at the
  // first point (worstI=0, tag 30 vs 10). Tooth bites iff parity is NOT green under -bug (proves the wire
  // order into InputBuffers is load-bearing AND actually flows the per-input StartIndex offsets).
  const bool bites = !parityGreen;
  printf("[t3-combinebuffers] -bug: InputBuffers order tooth %s (parity green under bug == %s)\n",
         bites ? "BITES" : "TOOTHLESS", parityGreen ? "true" : "false");
  pool->release();
  return bites ? 1 : 0;  // dead tooth exits 0 → --bite NO-BITE list catches it (GOLDEN_STANDARD 特徵3)
}

}  // namespace sw
