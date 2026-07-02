// runtime/t3import_displacemeshnoise_golden — 骨9 (--selftest-t3-displacemeshnoise): the MESH-family
// MIXED-SLOT MULTIINPUT proof. This is the honest gap 骨8 left open.
//
// ── WHY THIS GOLDEN EXISTS (the 骨9 承重线) ─────────────────────────────────────────────────────────
// 骨7b fixed resident_eval_flatten's inlineSymbol to resolve a compound's wires in a SINGLE pass over
// sym.connections, so a MultiInput slot fed by BOTH child wires AND boundary wires keeps the true wire
// DECLARATION order in its extraConns (the OLD two-pass code grouped [all child…, all boundary…],
// scrambling any interleave). Downstream the marshal cook packs floatInputs = [primary, extraConns…]
// positionally into a GPU constant buffer, so order == correctness. But 骨7b was only proven on a
// SYNTHETIC graph (resident_mixed_multiinput_golden.cpp, IntsToBuffer) and on TransformMesh — whose
// FloatsToBuffer.Params has NO child/boundary interleave (骨8 explicitly noted "no natural collision to
// swap"). So "骨7b holds on real mesh (SwVertex) currency through a genuinely interleaved mixed slot"
// was NEVER shown end-to-end. THIS golden shows it.
//
// ── WHY DisplaceMeshNoise ──────────────────────────────────────────────────────────────────────────
// Its real TiXL .t3 (embedded byte-faithful below) has a FloatsToBuffer.Params MultiInput fed by 13
// wires whose sources INTERLEAVE boundary and child, in the exact HLSL cbuffer Params order:
//   [0]Amount(BND) [1]Frequency(BND) [2]Phase(BND) [3]Variation(BND) [4..6]AmountDistribution(CH×3)
//   [7]RotationLookupDistance(BND) [8]UseWAsWeight(BND) [9]Space(CH) [10]Direction(CH)
//   [11]OffsetDirection(BND) [12]UseVertexSelection(CH)
// (BND = a boundary Input wire; CH = a sibling child's output — Vector3Components / IntToFloat /
// BoolToFloat.) That is a GENUINE mixed-slot interleave: BND,BND,BND,BND, CH,CH,CH, BND,BND, CH,CH,
// BND, CH. If the flatten did NOT preserve it, these floats land in the WRONG cb1 slots → the noise
// displacement's amplitude / frequency / distribution corrupt → the readback Position diverges. The
// same buffer backbone that TransformMesh proved (GetBufferComponents / ComputeShaderStage /
// StructuredBufferWithViews / ExecuteBufferUpdate / _MeshBufferComponents / _AssembleMeshBuffers) carries
// the SwVertex data UNCHANGED; the cb1 payload op changes (FloatsToBuffer, now interleaved), TWO value
// atoms are added (IntToFloat, Vector3Components — 骨9 map rows), and TWO import seams DisplaceMeshNoise
// exercises that TransformMesh did not (both closed in 骨9):
//   (1) the root boundary Params wires are DIRECT BND→FloatsToBuffer.Params (TransformMesh's went via a
//       child BoolToFloat), so they need buildEvalGraph's boundaryFloatInputs injection — see STEP 2.
//   (2) SBV.Count feeds from GetBufferComponents.Length (not GetSRVProperties.ElementCount like
//       TransformMesh); mapping Length → the Buffer view rail lets SBV allocate the UAV (t3_import_maps).
//
// ── THE PROVING KERNEL ───────────────────────────────────────────────────────────────────────────────
// computeshaderstage_displacemeshnoise.metal — a faithful MSL port of the .t3's ComputeShader.Source
// "Lib:shaders/3d/mesh/mesh-LegacyNoiseDisplace.hlsl", reusing sw's already-ported snoiseVec3/hash31.
//
// ── THE ORACLE (independent, non-circular) ──────────────────────────────────────────────────────────
// Config: Space=0 (PointSpace → posInWorld=(TexCoord.xy,0), NO camera matrix), Direction=0 (WorldSpace →
// NO TBN rotation), Variation=0 (variationOffset=0 → NO hash31), UseVertexSelection=0 (selection=1).
// Then per the .hlsl: Position_out = Position_in + offset, where
//   offset = (snoiseVec3((posInWorld*0.91)*Frequency + Phase) + OffsetDirection) * Amount/100 * AmountDist.
// The oracle recomputes snoiseVec3 in FLOAT (this file's snoise3/snoiseVec3d, our own port of the SAME
// Ashima algorithm in noise.metal.h — NOT derived from the import/cook path, so GREEN is not self-
// proving). FLOAT not double DELIBERATELY: 3-D simplex has floor()/step() branch points and snoiseVec3's
// offsets push lookups to |coord|~120, where a double oracle lands on DIFFERENT simplex cells than the
// float GPU → the noise value forks entirely (measured maxPosErr 0.5, not a rounding delta). Matching
// the GPU's float precision is the faithful reference; the final Position add is done in double.
// We parity-check the Position component (the load-bearing displaced output; measured maxPosErr 8e-6)
// AND the recomputed Normal/Tangent/Bitangent frame (each vertex's T/B rebuilt from getNoise at ±tangent
// anchors, N = T×B — the SAME getNoiseOracle the Position leg uses, so a passing TBN means the frame is
// geometrically right, not just the position; closes the "TBN wrong while Position right" hole).
//
// ── THE 骨9 TOOTH (-bug, load-bearing) ──────────────────────────────────────────────────────────────
// The -bug leg REGROUPS the FloatsToBuffer.Params wires in sym.connections into [all child…, all
// boundary…] before buildEvalGraph — exactly what the OLD pre-骨7b two-pass flatten produced (see the
// 承重线 note above). This is the INTERLEAVE-SPECIFIC mutation, not a generic scramble: it keeps all 13
// wires and each group's internal order, and ONLY collapses the child/boundary INTERLEAVE that 骨7b's
// single-pass fix preserves. (A full reverse would also bite, but reverse proves the SUPERSET "any cb1
// positional dependence"; the label here is specifically "mixed-slot interleave preservation", so the
// tooth must isolate exactly the regroup 骨7b prevents.) Faithful interleave → the 13 floats fill
// cb1[0..12] correctly → GREEN. Regrouped → child wires (AmountDistribution×3, Space, Direction,
// UseVertexSelection) migrate to cb1[0..5] and the boundary wires (Amount, Frequency, Phase, Variation,
// RotationLookupDistance, UseWAsWeight, OffsetDirection) to cb1[6..12] → readParams reads Amount from
// AmountDistribution.x(=1), Frequency from AmountDistribution.y, AmountDistribution from (Direction=0,
// UseVertexSelection=0, Amount=25) etc. → the displaced offset lands on wrong axes with wrong noise
// frequency → the readback Positions DIVERGE from the oracle → RED. That RED is the proof that the
// single-pass declaration-order preservation (骨7b) is what keeps the interleaved mixed slot correct on
// real SwVertex mesh currency. (Regrouping exercises the EXACT sym.connections→extraConns→floatInputs
// →cb1 chain the fix guards — it re-creates the pre-fix flatten output in-place, not a synthetic
// scramble outside that path.)
//
// ZONE: runtime golden (shell tier — binds runtime import + resident cook + the independent oracle).
#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <vector>

#include <Foundation/Foundation.hpp>
#include <Metal/Metal.hpp>

#include "runtime/buffer_op_registry.h"     // BufferOp / BufferCookCtx (the fixture producer)
#include "runtime/compound_graph.h"         // SymbolLibrary / Symbol / SymbolChild / SymbolConnection
#include "runtime/eval_context.h"           // EvaluationContext
#include "runtime/graph.h"                  // findSpec / registerBuiltinPointOps
#include "runtime/graph_bridge.h"           // atomicSymbolFromSpec
#include "runtime/point_graph.h"            // PointGraph / residentSwBufferFor
#include "runtime/resident_eval_graph.h"    // ResidentEvalGraph / buildEvalGraph / initResidentCache / ctx
#include "runtime/sw_buffer.h"              // SwBuffer
#include "runtime/sw_mesh.h"                // SwVertex (80B)
#include "runtime/t3_import.h"              // importT3Symbol
#include "runtime/t3import_displacemeshnoise_oracle.h"  // the independent float snoise oracle + getNoiseOracle

namespace sw {

void registerBuiltinPointOps();

namespace {
using namespace dmn_oracle;  // F / V3 / snoiseVec3d / getNoiseOracle / vec helpers (the oracle-math half)

static const char* kDisplaceMeshNoiseT3 =
#include "runtime/displacemeshnoise_t3_embed.inc"
;

// Boundary Input def GUIDs (from DisplaceMeshNoise.t3 Inputs[]) — the golden overrides these scalar
// defaults so the config is closed-form (Space=0/Direction=0/Variation=0/UseVertexSelection=0).
struct BParam { const char* guid; float value; };

// ── Test-fixture Buffer producer: a fixed N-vertex SwVertex bag as a SwBuffer (stride 80). ──────────
std::vector<SwVertex>* g_fixtureVerts = nullptr;

void cookInputVertsFixture(BufferCookCtx& c) {
  if (!c.output || !c.requestBytes || !g_fixtureVerts) return;
  const uint32_t n = (uint32_t)g_fixtureVerts->size();
  if (n == 0) return;
  const uint32_t bytes = n * (uint32_t)sizeof(SwVertex);
  void* dst = c.requestBytes(bytes);
  if (!dst) return;
  std::memcpy(dst, g_fixtureVerts->data(), bytes);
  c.output->elementStride = (uint32_t)sizeof(SwVertex);  // 80
  c.output->elementCount = n;
  c.output->elementFormat = 0;
}

NodeSpec fixtureSpec() {
  NodeSpec s;
  s.type = "t3dmn_input_verts";
  s.title = "t3dmn_input_verts";
  s.category = "test";
  s.ports = {{"Buffer", "Buffer", "Buffer", false}};  // a pure producer (no inputs)
  s.evaluate = nullptr;
  return s;
}
const BufferOp _reg_t3dmn_input_verts(fixtureSpec(), cookInputVertsFixture);

// The independent float snoise oracle (Ashima 3-D simplex port + getNoiseOracle + V3 helpers) lives in
// runtime/t3import_displacemeshnoise_oracle.h — see that header for the FLOAT-not-double rationale.

int childIdOfType(const Symbol& s, const std::string& type) {
  for (const SymbolChild& c : s.children) if (c.symbolId == type) return c.id;
  return 0;
}

}  // namespace

int runT3DisplaceMeshNoiseParity(bool injectBug) {
  NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();
  registerBuiltinPointOps();

  // Closed-form config (see header): Space=0, Direction=0, Variation=0, UseVertexSelection=0.
  const float AMOUNT = 25.0f, FREQ = 1.3f, PHASE = 0.4f, OFFDIR = 0.2f;
  const float RLD = 0.5f;  // RotationLookupDistance — only affects the TBN recompute (Position ignores it)
  const V3 AMTDIST = {1.0, 0.7, 1.4};

  // ---- Input bag: N vertices with distinct Position AND TexCoord (posInWorld=(TexCoord.xy,0)). ----
  const uint32_t N = 16;
  std::vector<SwVertex> in(N);
  for (uint32_t i = 0; i < N; ++i) {
    double a = (double)i / (double)N;
    in[i] = SwVertex{};
    in[i].Position  = SW_MESH_PACKED3{ (float)(std::cos(a*6.2831853)*1.3), (float)(std::sin(a*6.2831853)*0.8), (float)((a-0.5)*1.5) };
    in[i].Normal    = SW_MESH_PACKED3{ 0.0f, 0.0f, 1.0f };
    in[i].Tangent   = SW_MESH_PACKED3{ 1.0f, 0.0f, 0.0f };
    in[i].Bitangent = SW_MESH_PACKED3{ 0.0f, 1.0f, 0.0f };
    in[i].Texcoord  = SW_MESH_FLOAT2{ (float)(a*2.0), (float)((1.0-a)*1.5) };
    in[i].Selection = 1.0f;
    in[i].ColorRgb  = SW_MESH_PACKED3{ 0.5f, 0.6f, 0.7f };
  }
  g_fixtureVerts = &in;

  // ---- STEP 1: import the real .t3 via the PRODUCTION importer ----
  SymbolLibrary lib;
  std::string rootId;
  std::vector<std::string> warnings;
  if (!importT3Symbol(kDisplaceMeshNoiseT3, lib, &rootId, &warnings)) {
    printf("[t3-displacemeshnoise] FAIL: importT3Symbol returned false\n"); pool->release(); return 1;
  }
  Symbol* sym = const_cast<Symbol*>(lib.find(rootId));
  if (!sym) { printf("[t3-displacemeshnoise] FAIL: root symbol missing\n"); pool->release(); return 1; }
  { std::map<std::string, int> byType;
    for (const SymbolChild& c : sym->children) byType[c.symbolId]++;
    printf("[t3-displacemeshnoise] import: rootId=%s children=%d conns=%d warnings=%zu\n",
           rootId.c_str(), (int)sym->children.size(), (int)sym->connections.size(), warnings.size());
    printf("[t3-displacemeshnoise]   mapped atom types:");
    for (const auto& kv : byType) printf(" %s×%d", kv.first.c_str(), kv.second);
    printf("\n"); }

  const int mbcId = childIdOfType(*sym, "_MeshBufferComponents");
  const int asmId = childIdOfType(*sym, "_AssembleMeshBuffers");
  const int f2bId = childIdOfType(*sym, "FloatsToBuffer");
  const int v3cId = childIdOfType(*sym, "Vector3Components");
  const int strideConstId = childIdOfType(*sym, "Const");
  if (!mbcId || !asmId || !f2bId || !v3cId || !strideConstId) {
    printf("[t3-displacemeshnoise] FAIL: expected atom missing (mbc=%d asm=%d f2b=%d v3c=%d strideConst=%d)\n",
           mbcId, asmId, f2bId, v3cId, strideConstId);
    pool->release(); return 1;
  }

  // (a) Boundary scalar values → the top-level boundary-injection map (buildEvalGraph's boundaryFloatInputs).
  //     The .t3 wires these boundary Inputs DIRECTLY into FloatsToBuffer.Params (the interleaved mixed
  //     slot) and into IntToFloat.IntValue (Space/Direction), so they MUST be injected as root Const
  //     producers — inputDef defaults only cover the child-default fallback, NOT boundary MultiInput wires.
  const BParam bp[] = {
      {"357ed675-212f-4b2c-b93b-c8460867a9ae", 0.0f},   // Space = PointSpace (posInWorld=(uv,0))
      {"f108f6f7-5e6f-43c8-9d0b-c2e7bf5adf9c", 0.0f},   // Direction = WorldSpace (no TBN rotation)
      {"e5213b00-6302-45e1-a172-5a11bd91892e", 0.0f},   // Variation = 0 (no hash31)
      {"83cb775f-c600-41c9-9435-604f77a426bd", 0.0f},   // UseVertexSelection = false (selection=1)
      {"b7559321-2dbe-4fe0-ab86-52532d008980", AMOUNT}, // Amount
      {"4b1a66a4-b5e4-4bc3-97f5-bd3cda668893", FREQ},   // Frequency
      {"b89b6730-de46-4d56-b2a8-b7d6f6876620", PHASE},  // Phase
      {"093a468c-c208-4caf-be4f-d5d7d9ceddeb", OFFDIR}, // OffsetDirection
      {"08e2222f-6de1-46a8-bbdc-da83251f424e", RLD},    // RotationLookupDistance (only affects the TBN recompute)
      {"9a82f2a6-390e-4073-bc80-e5fd3b1c0bfe", 0.0f},   // UseWAsWeight (unused in Position math)
  };
  std::map<std::string, std::vector<float>> boundary;
  for (const BParam& b : bp) {
    boundary[b.guid] = {b.value};
    for (SlotDef& d : sym->inputDefs) if (d.id == b.guid) { d.def = b.value; break; }
  }

  // (b) AmountDistribution: the single boundary wire lands on Vector3Components.Value.x head
  //     (fork-t3-vec3-wire-lands-on-head). Author the full vec3 on the imported child.
  for (SymbolChild& c : sym->children) if (c.id == v3cId) {
    c.overrides["Value.x"] = (float)AMTDIST.x;
    c.overrides["Value.y"] = (float)AMTDIST.y;
    c.overrides["Value.z"] = (float)AMTDIST.z;
    break;
  }

  // (c) FORK pbrvertex-stride-64-to-80-swvertex: the imported IntValue(PBRVertex.Stride)→Const carries
  //     64 (TiXL PbrVertex); sw's SwVertex is 80B — override on the imported child.
  for (SymbolChild& c : sym->children) if (c.id == strideConstId) { c.overrides["value"] = 80.0f; break; }

  // (d) Supply the TEST mesh input at _MeshBufferComponents.MeshBuffers (the .t3's InputMesh boundary
  //     has no producer). Same seam TransformMesh's golden feeds.
  if (!lib.symbols.count("t3dmn_input_verts"))
    if (const NodeSpec* fs = findSpec("t3dmn_input_verts"))
      lib.symbols["t3dmn_input_verts"] = atomicSymbolFromSpec(*fs);
  const int fixtureId = sym->nextChildId++;
  { SymbolChild p; p.id = fixtureId; p.symbolId = "t3dmn_input_verts"; sym->children.push_back(p); }
  { SymbolConnection w; w.srcChild = fixtureId; w.srcSlot = "Buffer"; w.dstChild = mbcId; w.dstSlot = "MeshBuffers";
    sym->connections.push_back(w); }

  // ---- 骨9 TOOTH (-bug): REGROUP the FloatsToBuffer.Params wires in sym.connections into
  //     [all child…, all boundary…] — the OLD pre-骨7b two-pass flatten output, reproduced in-place.
  //     This is the INTERLEAVE-SPECIFIC scramble: it keeps all 13 wires and each group's internal order,
  //     and ONLY collapses the child/boundary INTERLEAVE that 骨7b's single-pass fix preserves (a full
  //     reverse would also bite, but reverse proves the SUPERSET "any cb1 positional dependence"; this
  //     tooth isolates exactly the regroup the fix prevents, matching the golden's label). Regrouped →
  //     child wires migrate to cb1[0..5], boundary wires to cb1[6..12] → readParams mis-reads Amount from
  //     AmountDistribution.x etc. → offset lands on wrong axes with wrong noise frequency → RED. ----
  if (injectBug) {
    std::vector<size_t> paramWireIdx;
    for (size_t i = 0; i < sym->connections.size(); ++i) {
      const SymbolConnection& w = sym->connections[i];
      if (w.dstChild == f2bId && w.dstSlot == "Params") paramWireIdx.push_back(i);
    }
    // Partition the Params wires (preserving each group's original relative order), then write them back
    // into the same slots as [child…, boundary…] — exactly the two-pass grouping 骨7b replaced.
    std::vector<SymbolConnection> childWires, boundaryWires;
    for (size_t idx : paramWireIdx) {
      const SymbolConnection& w = sym->connections[idx];
      if (sourceIsSymbolInput(w)) boundaryWires.push_back(w); else childWires.push_back(w);
    }
    std::vector<SymbolConnection> regrouped;
    regrouped.reserve(paramWireIdx.size());
    for (const SymbolConnection& w : childWires)    regrouped.push_back(w);
    for (const SymbolConnection& w : boundaryWires) regrouped.push_back(w);
    for (size_t k = 0; k < paramWireIdx.size(); ++k) sym->connections[paramWireIdx[k]] = regrouped[k];
    printf("[t3-displacemeshnoise] -bug: two-pass REGROUP of %zu FloatsToBuffer.Params wires "
           "(%zu child-first, %zu boundary-last) — collapses mixed-slot interleave (pre-骨7b order)\n",
           paramWireIdx.size(), childWires.size(), boundaryWires.size());
  }

  // ---- STEP 2: build the eval graph (production flattener) + inject the root boundary values ----
  ResidentEvalGraph g = buildEvalGraph(lib, rootId, boundary);
  initResidentCache(g);
  printf("[t3-displacemeshnoise] buildEvalGraph: resident nodes=%zu\n", g.nodes.size());

  const std::string termPath = std::to_string(asmId);

  // ---- STEP 3: cook the resident graph; read back the _AssembleMeshBuffers output as SwVertex[] ----
  MTL::Device* dev = MTL::CreateSystemDefaultDevice();
  MTL::CommandQueue* q = dev->newCommandQueue();
  NS::Error* err = nullptr;
  MTL::Library* mlib = dev->newLibrary(NS::String::string(SW_SHADER_METALLIB, NS::UTF8StringEncoding), &err);
  if (!mlib) { printf("[t3-displacemeshnoise] FAIL: no metallib\n"); q->release(); dev->release(); pool->release(); return 1; }

  PointGraph pg(dev, mlib, q, 64, 64);
  EvaluationContext ctx{}; ctx.frameIndex = 0; ctx.time = 0.0f; ctx.deltaTime = 1.0f/60.0f;
  pg.cookResident(g, ctx, nullptr, termPath);
  const SwBuffer* outBuf = pg.residentSwBufferFor(termPath);

  bool haveOut = outBuf && outBuf->bytes && outBuf->elementCount == N && outBuf->elementStride == sizeof(SwVertex);
  std::vector<SwVertex> got(N);
  if (haveOut) std::memcpy(got.data(), const_cast<MTL::Buffer*>(outBuf->bytes)->contents(), N * sizeof(SwVertex));

  // ---- Oracle: Position AND TBN (Space=0/Dir=0/Var=0/sel=1 ⇒ weight=1, no TBN rotation of offset).
  //   Position_out = Position_in + offset.
  //   T/B recompute (verbatim kernel): with lud = RotationLookupDistance/Frequency and newPos =
  //   posInWorld + offset, tangent from ± anchors along the input Tangent, bitangent along Bitangent,
  //   Normal = cross(T,B). Same getNoiseOracle the Position leg uses, so TBN can only pass if the SAME
  //   noise the Position green already validated also lands the recomputed frame — closing the
  //   "TBN geometrically wrong while Position right" hole. ----
  double maxPos = 0.0; int worstI = -1; double wExp[3]{}, wGot[3]{};
  double maxTbn = 0.0; int worstJ = -1; char worstFrame = '?'; double tExp[3]{}, tGot[3]{};
  if (haveOut)
    for (uint32_t i = 0; i < N; ++i) {
      V3 posInWorld = { in[i].Texcoord.x, in[i].Texcoord.y, 0.0f };
      V3 offset = getNoiseOracle(posInWorld, FREQ, PHASE, OFFDIR, AMOUNT, AMTDIST);
      double ep[3] = { in[i].Position.x + offset.x, in[i].Position.y + offset.y, in[i].Position.z + offset.z };
      double gp[3] = { got[i].Position.x, got[i].Position.y, got[i].Position.z };
      double dp = std::sqrt((ep[0]-gp[0])*(ep[0]-gp[0])+(ep[1]-gp[1])*(ep[1]-gp[1])+(ep[2]-gp[2])*(ep[2]-gp[2]));
      if (dp > maxPos) { maxPos = dp; worstI = (int)i; for (int k=0;k<3;k++){wExp[k]=ep[k];wGot[k]=gp[k];} }

      // TBN recompute (Direction=0 ⇒ offset NOT rotated; weight=selection=1).
      const F lud = RLD / FREQ;
      V3 newPos = add3(posInWorld, offset);
      V3 T = { in[i].Tangent.x, in[i].Tangent.y, in[i].Tangent.z };
      V3 B = { in[i].Bitangent.x, in[i].Bitangent.y, in[i].Bitangent.z };
      V3 tA  = add3(posInWorld, scl3(T, lud));
      V3 tA2 = sub3(posInWorld, scl3(T, lud));
      V3 nT  = nrm3(sub3(add3(tA,  getNoiseOracle(tA,  FREQ, PHASE, OFFDIR, AMOUNT, AMTDIST)), newPos));
      V3 nT2 = scl3(nrm3(sub3(add3(tA2, getNoiseOracle(tA2, FREQ, PHASE, OFFDIR, AMOUNT, AMTDIST)), newPos)), -1.0f);
      V3 expT = mix3(nT, nT2, 0.5f);
      V3 bA  = add3(posInWorld, scl3(B, lud));
      V3 bA2 = sub3(posInWorld, scl3(B, lud));
      V3 nB  = nrm3(sub3(add3(bA,  getNoiseOracle(bA,  FREQ, PHASE, OFFDIR, AMOUNT, AMTDIST)), newPos));
      V3 nB2 = scl3(nrm3(sub3(add3(bA2, getNoiseOracle(bA2, FREQ, PHASE, OFFDIR, AMOUNT, AMTDIST)), newPos)), -1.0f);
      V3 expB = mix3(nB, nB2, 0.5f);
      V3 expN = crs3(expT, expB);
      struct { char tag; V3 exp; V3 got; } frames[3] = {
        {'T', expT, {got[i].Tangent.x,   got[i].Tangent.y,   got[i].Tangent.z}},
        {'B', expB, {got[i].Bitangent.x, got[i].Bitangent.y, got[i].Bitangent.z}},
        {'N', expN, {got[i].Normal.x,    got[i].Normal.y,    got[i].Normal.z}},
      };
      for (const auto& fr : frames) {
        double d = std::sqrt((double)dot3(sub3(fr.exp, fr.got), sub3(fr.exp, fr.got)));
        if (d > maxTbn) { maxTbn = d; worstJ = (int)i; worstFrame = fr.tag;
          tExp[0]=fr.exp.x; tExp[1]=fr.exp.y; tExp[2]=fr.exp.z;
          tGot[0]=fr.got.x; tGot[1]=fr.got.y; tGot[2]=fr.got.z; }
      }
    }

  // TBN rides normalize()/cross() on top of the SAME float snoise as Position; the divisions could amplify
  // the simplex floor()-branch rounding, but in this fixed config the measured TBN residual is ~1e-5 (same
  // order as the Position 8e-6), so it holds the SAME 2e-3 gate — well above rounding, far below the ~0.5
  // divergence a wrong cb1 layout produces (the -bug leg measures 0.55). A geometric TBN error can't hide.
  const double kTbnThresh = 2e-3;
  printf("[t3-displacemeshnoise] replay-vs-oracle: haveOut=%d maxPosErr=%.6f(need<2e-3) worstI=%d "
         "exp=(%.4f,%.4f,%.4f) got=(%.4f,%.4f,%.4f)\n",
         haveOut ? 1 : 0, maxPos, worstI, wExp[0], wExp[1], wExp[2], wGot[0], wGot[1], wGot[2]);
  printf("[t3-displacemeshnoise] replay-vs-oracle TBN: maxTbnErr=%.6f(need<%.0e) worst=%c[%d] "
         "exp=(%.4f,%.4f,%.4f) got=(%.4f,%.4f,%.4f)\n",
         maxTbn, kTbnThresh, worstFrame, worstJ, tExp[0], tExp[1], tExp[2], tGot[0], tGot[1], tGot[2]);

  // Threshold 2e-3: float-GPU snoise vs double-oracle snoise carries a few 1e-4 rounding deltas at the
  // simplex floor() branch points; the displaced offset (~Amount/100 scale) stays well under 2e-3.
  const bool parityGreen = haveOut && (maxPos < 2e-3) && (maxTbn < kTbnThresh);
  printf("[t3-displacemeshnoise] PARITY VERDICT: %s\n",
         parityGreen ? "GREEN (mesh mixed-slot replay reproduces the noise displacement + TBN frame)"
                     : "RED (mesh mixed-slot replay seam gap / cb1 order scrambled / TBN diverged)");

  mlib->release(); q->release(); dev->release();
  g_fixtureVerts = nullptr;

  if (!injectBug) {
    if (!parityGreen) { printf("[t3-displacemeshnoise] FAIL\n"); pool->release(); return 1; }
    printf("[t3-displacemeshnoise] PASS: DisplaceMeshNoise.t3 replays to parity through the INTERLEAVED "
           "FloatsToBuffer.Params mixed slot (骨7b order fix holds on real SwVertex mesh currency)\n");
    pool->release(); return 0;
  }

  // injectBug leg: the Params wires were regrouped [child…, boundary…] → cb1 interleave collapsed → RED.
  // Tooth bites iff NOT green.
  const bool bites = !parityGreen;
  printf("[t3-displacemeshnoise] -bug: cb1-interleave tooth %s (parity green under bug == %s)\n",
         bites ? "BITES" : "TOOTHLESS", parityGreen ? "true" : "false");
  pool->release();
  return bites ? 1 : 2;
}

}  // namespace sw
