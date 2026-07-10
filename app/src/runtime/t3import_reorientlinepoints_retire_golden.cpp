// runtime/t3import_reorientlinepoints_retire_golden — 廢棄節點退場 harness
// (--selftest-t3-reorientlinepoints-retire).
//
// Retires the flat ReorientLinePoints point atom: its human-name references are AUTO-TAKEN-OVER by the
// nested .t3 compound (assets/catalog_t3/ReorientLinePoints.t3, guid 5dbe204c…) via the replace-in-place
// seam (graph_bridge.cpp refreshCompoundSpecs name alias + findSpec's dynamicSpecs tail). The compound's
// own embedded name is "ReorientLinePoints" — IDENTICAL to sw's registered atom name — so no
// kLegacyNameAlias entry is needed (unlike TransformFromClipSpace's sw/TiXL name divergence). Four gates,
// each a MEASURED RED→GREEN tooth. Expected values are TiXL constants (the .t3's own Id guid + the
// .t3ui Position numbers) and the mathv-verified ReorientLinePoints oracle — never sw's own output
// (GOLDEN_STANDARD 特徵1 / P5-safe).
//
// ── the four gates (RETIREMENT_BATTLE_SPEC §5, MATH_VERIFY_WORKFLOW §8) ──────────────────────────
//  ① TAKEOVER POLARITY: with the flat atom retired, findSpec("ReorientLinePoints") falls through every
//     atom sink to the compound's NAME alias → the COMPOUND spec (type==guid, evaluate==nullptr) and
//     lib[guid] is a non-atomic compound with children. injectBug pushes a stand-in flat atom into a
//     LIVE point-modify sink → findSpec hits it FIRST → returns the ATOM → BITE.
//  ② PARITY (cook-driven, mathv oracle): import ReorientLinePoints.t3 → buildEvalGraph (骨7 boundary
//     injection feeds the compound's Amount scalar) → cookResident → read back the ExecuteBufferUpdate
//     output vs mathvRefReorientLinePoints (the mathv-verified CPU oracle, R6-passed). This is the
//     retire §8 佈線 focus: proves FloatsToBuffer assembly (9-float tight array, Amount at cb0[3]) +
//     boundary injection put Amount in the right kernel cbuffer slot + the generic ComputeShaderStage
//     dispatches the ported kernel computeshaderstage_reorientlinepoints (with the SAME copy-through +
//     real-OOB-guard NAMED FORKS as the fused reorientlinepoints.metal). injectBug PERTURBS the
//     boundary-injected Amount (output depends on it continuously via qSlerp) while the oracle keeps the
//     clean value → readback diverges → BITE. This proves the injected boundary param actually reaches
//     the kernel cbuffer (dead injection → output unchanged → NO-BITE).
//  ③ REFERENCE REACHABILITY + COOKABILITY: the NAME reference resolves to a spec carrying the compound
//     boundary (Points in + Output out) AND buildEvalGraph flattens to a NON-EMPTY resident graph.
//     injectBug drops the compound registration (setDynamicSpecs({})) → findSpec nullptr → BITE.
//  ④ LAYOUT: import .t3 + sibling .t3ui through the production layout seam → boundary pins land on their
//     .t3ui Position constants (non-zero, distinct). injectBug flips t3LayoutDisable() → every pos 0,0.
//
// A single injectBug bool drives all four teeth. did-not-trip → return 0 (GOLDEN_STANDARD 特徵3 / P1).
//
// ── PARITY CONFIG (fixture + boundary-injection note) ────────────────────────────────────────────
// Only Amount needs 骨7 injection: it is the sole DIRECT-scalar boundary→FloatsToBuffer(MultiInput) wire
// (ReorientLinePoints.t3 Connections, dst=FloatsToBuffer.Params slot 49556d12). Center/UpVector ride
// Vector3Components (root→component input, component outputs→FloatsToBuffer) and WIsWeight/Flip ride
// BoolToFloat — those child→child wires into FloatsToBuffer are ALWAYS present regardless of whether the
// upstream root boundary got an injected value (resident_eval_flatten.cpp inlineSymbol: a boundary→child
// wire with no inBindings entry simply drops that ONE wire, but the downstream component/BoolToFloat
// node's OWN wire into FloatsToBuffer is a separate child→child connection, resolved unconditionally).
// Skipping Amount's injection would drop ITS wire and shift every later float left (same risk flagged in
// t3import_snappointstogrid_retire_golden.cpp's header) — Amount is therefore the one entry ever
// required. Dead slots (Center/UpVector/WIsWeight/Flip) resolve to their component/BoolToFloat node's own
// default (0) regardless — harmless, since computeshaderstage_reorientlinepoints.metal never reads them
// (cb0[3]=Amount only).
//
// Fixture: an N-point BENT/SPIRAL polyline (continuously varying tangent — no two segments collinear,
// satisfying GOLDEN_STANDARD 特徵2's "probe at divergent middle") with ONE dead point (NaN Scale.x)
// planted mid-line to exercise the copy-through fork + its two neighbours' single-sided (backward-only /
// forward-only) tangent branches, and the LAST point to exercise the real-OOB-guard fork (the oracle is
// fed a `count+1`-sized array with a NaN-Scale padding slot at `in[count]`, matching this fork's own
// documented invariant — see mathv_ref_reorientlinepoints.h's OOB-READ QUIRK note and
// reorientlinepoints.metal's file header). All points start at IDENTITY rotation (matches the mathv ref's
// own self-check cases A/C) so every live point's output rotation is driven entirely by its LOCAL tangent
// — a wrong qAlignForward2/qSlerp would diverge cleanly from the oracle.
//
// ZONE: runtime golden (shell tier — binds the importer + refreshCompoundSpecs seam + the mathv oracle).
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>
#include <map>
#include <string>
#include <vector>

#include <Foundation/Foundation.hpp>
#include <Metal/Metal.hpp>

#include "runtime/buffer_op_registry.h"     // BufferOp / BufferCookCtx (fixture producer)
#include "runtime/compound_graph.h"         // SymbolLibrary / Symbol / SymbolChild / SymbolConnection
#include "runtime/graph.h"                  // findSpec / NodeSpec / PortSpec / setDynamicSpecs
#include "runtime/graph_bridge.h"           // refreshCompoundSpecs / atomicSymbolFromSpec
#include "runtime/point_modify_op_registry.h"  // pointModifySpecSink (① inject the stand-in atom)
#include "runtime/point_graph.h"            // PointGraph / residentSwBufferFor
#include "runtime/resident_eval_graph.h"    // ResidentEvalGraph / buildEvalGraph / initResidentCache / ctx
#include "runtime/sw_buffer.h"              // SwBuffer
#include "runtime/t3_import.h"              // importT3Symbol / t3ImportInjectBug / t3LayoutDisable / symbolIdOfT3
#include "runtime/tixl_point.h"             // SwPoint (64B)
#include "mathv_ref_reorientlinepoints.h"   // mathvRefReorientLinePoints (mathv-verified CPU oracle)

namespace sw {

void registerBuiltinPointOps();

namespace {

static const char* kT3 =
#include "runtime/reorientlinepoints_t3_embed.inc"
;
static const char* kT3ui =
#include "runtime/reorientlinepoints_t3ui_embed.inc"
;

// Expected constants — the .t3's OWN Id + the .t3ui Position numbers (never sw output).
const char* const kGuid = "5dbe204c-ded0-4f23-bf6a-1de5cca21db6";
const char* const kName = "ReorientLinePoints";
// boundary input def guid (compound Inputs) — the 骨7 injection key (only live param).
const char* const kInAmount = "70c1746a-3cf1-4a20-829e-18fd846b9133";
// .t3ui pins (④): Amount input / Points input / Output — verbatim from ReorientLinePoints.t3ui.
const char* const kPinAmount  = "70c1746a-3cf1-4a20-829e-18fd846b9133";
const char* const kPinPoints  = "d0c4731f-d84d-492c-ad62-7b635ef83407";
const char* const kPinOutput  = "3c051df4-1edc-49dd-88f1-8db923bf0e3e";  // Output pin (.t3ui OutputUis)
constexpr float kAmtX = 53.196f,     kAmtY = 854.5584f;
constexpr float kPtsX = -650.41815f, kPtsY = 592.5717f;
constexpr float kOutX = 812.7836f,   kOutY = 534.6297f;
constexpr float kLayoutEps = 0.01f;
bool nearf(float a, float b, float e = kLayoutEps) { return std::fabs(a - b) < e; }

// PARITY config: clean Amount (kAmount) vs the -bug perturbed value.
constexpr float kAmount = 1.0f;
constexpr uint32_t kN = 16;
constexpr uint32_t kDeadIdx = 7;  // mid-line dead point (NaN Scale.x)

// Deterministic bent/spiral fixture — no two consecutive segments collinear.
std::vector<SwPoint> buildFixture() {
  std::vector<SwPoint> pts(kN);
  for (uint32_t i = 0; i < kN; ++i) {
    const float t = (float)i;
    pts[i] = SwPoint{};
    pts[i].Position = SW_PACKED3{std::cos(t * 0.35f) * 2.0f + t * 0.15f,
                                 std::sin(t * 0.35f) * 1.3f,
                                 t * 0.2f - 0.05f};
    pts[i].Rotation = SW_FLOAT4{0.0f, 0.0f, 0.0f, 1.0f};  // identity (matches mathv ref self-check A/C)
    pts[i].Scale = SW_PACKED3{1.0f, 1.0f, 1.0f};
    pts[i].FX1 = 1.0f;
    pts[i].FX2 = 1.0f;
    pts[i].Color = SW_FLOAT4{1.0f, 1.0f, 1.0f, 1.0f};
  }
  pts[kDeadIdx].Scale.x = std::numeric_limits<float>::quiet_NaN();  // TiXL's own line-break sentinel
  return pts;
}

// ── Test-fixture Buffer producer (emits the fixed N-point bag as the compound's Points input) ────────
std::vector<SwPoint>* g_fixture = nullptr;
void cookFixture(BufferCookCtx& c) {
  if (!c.output || !c.requestBytes || !g_fixture) return;
  const uint32_t n = (uint32_t)g_fixture->size();
  if (n == 0) return;
  void* dst = c.requestBytes(n * (uint32_t)sizeof(SwPoint));
  if (!dst) return;
  std::memcpy(dst, g_fixture->data(), n * sizeof(SwPoint));
  c.output->elementStride = (uint32_t)sizeof(SwPoint);
  c.output->elementCount = n;
  c.output->elementFormat = 0;
}
NodeSpec fixtureSpec() {
  NodeSpec s; s.type = "t3xf_reorient_input"; s.title = "t3xf_reorient_input"; s.category = "test";
  s.ports = {{"Buffer", "Buffer", "Buffer", false}}; s.evaluate = nullptr; return s;
}
const BufferOp _reg_t3xf_reorient_input(fixtureSpec(), cookFixture);

int childIdOfType(const Symbol& s, const std::string& type) {
  for (const SymbolChild& c : s.children) if (c.symbolId == type) return c.id;
  return 0;
}
void countPorts(const NodeSpec& s, int& nIn, int& nOut) {
  nIn = nOut = 0; for (const PortSpec& p : s.ports) (p.isInput ? nIn : nOut)++;
}
NodeSpec standInFlatAtomSpec() {
  NodeSpec s; s.type = kName; s.title = kName; s.category = "point.transform";
  s.ports = {{"points", "points", "Points", true}, {"out", "out", "Points", false}};
  s.evaluate = nullptr; return s;
}

}  // namespace

// ── ② PARITY (cook-driven vs the mathv oracle) ────────────────────────────────────────────────────
int runT3ReorientLinePointsParity(bool injectBug) {
  NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();
  registerBuiltinPointOps();

  std::vector<SwPoint> in = buildFixture();
  g_fixture = &in;

  SymbolLibrary lib;
  std::string rootId;
  std::vector<std::string> warnings;
  bool ok = importT3Symbol(kT3, lib, &rootId, &warnings);
  if (!ok || rootId != std::string(kGuid)) {
    printf("[reorient-retire] ②parity FAIL: import bad (ok=%d id=%s)\n", ok, rootId.c_str());
    g_fixture = nullptr; pool->release(); return 1;
  }
  Symbol* sym = const_cast<Symbol*>(lib.find(rootId));
  if (!sym) { printf("[reorient-retire] ②parity FAIL: no root symbol\n"); g_fixture = nullptr; pool->release(); return 1; }

  // Repoint the Points boundary→GetBufferComponents wire at the fixture producer.
  auto childSym = [&](int id) -> std::string {
    for (const SymbolChild& c : sym->children) if (c.id == id) return c.symbolId;
    return std::string();
  };
  const int gbc = [&]{
    for (const SymbolConnection& w : sym->connections)
      if (w.srcChild == kSymbolBoundary && w.dstSlot == "BufferWithViews" &&
          childSym(w.dstChild) == "GetBufferComponents") return w.dstChild;
    return 0; }();
  if (!gbc) { printf("[reorient-retire] ②parity FAIL: no Points→GetBufferComponents wire\n"); g_fixture = nullptr; pool->release(); return 1; }
  const int fixtureId = sym->nextChildId++;
  { SymbolChild p; p.id = fixtureId; p.symbolId = "t3xf_reorient_input"; sym->children.push_back(p); }
  if (!lib.symbols.count("t3xf_reorient_input"))
    if (const NodeSpec* fs = findSpec("t3xf_reorient_input")) lib.symbols["t3xf_reorient_input"] = atomicSymbolFromSpec(*fs);
  for (SymbolConnection& w : sym->connections)
    if (w.srcChild == kSymbolBoundary && w.dstChild == gbc && w.dstSlot == "BufferWithViews") {
      w.srcChild = fixtureId; w.srcSlot = "Buffer";
    }

  // 骨7 boundary injection: Amount is the ONLY direct-scalar boundary wire into FloatsToBuffer (see file
  // header PARITY CONFIG note). injectBug PERTURBS it (a param the output depends on CONTINUOUSLY via
  // qSlerp) while the oracle keeps the clean kAmount → the readback must DIVERGE.
  const float amtInjected = injectBug ? 0.3f : kAmount;
  std::map<std::string, std::vector<float>> boundaryFloatInputs;
  boundaryFloatInputs[kInAmount] = {amtInjected};  // b0[3]

  ResidentEvalGraph g = buildEvalGraph(lib, rootId, boundaryFloatInputs);
  initResidentCache(g);
  const int ebuId = childIdOfType(*sym, "ExecuteBufferUpdate");
  if (!ebuId) { printf("[reorient-retire] ②parity FAIL: no ExecuteBufferUpdate\n"); g_fixture = nullptr; pool->release(); return 1; }
  const std::string termPath = std::to_string(ebuId);

  MTL::Device* dev = MTL::CreateSystemDefaultDevice();
  MTL::CommandQueue* q = dev->newCommandQueue();
  NS::Error* err = nullptr;
  MTL::Library* mlib = dev->newLibrary(NS::String::string(SW_SHADER_METALLIB, NS::UTF8StringEncoding), &err);
  if (!mlib) { printf("[reorient-retire] ②parity FAIL: no metallib\n"); q->release(); dev->release(); g_fixture = nullptr; pool->release(); return 1; }

  PointGraph pg(dev, mlib, q, 64, 64);
  EvaluationContext ctx{}; ctx.frameIndex = 0; ctx.time = 0.0f; ctx.deltaTime = 1.0f / 60.0f;
  pg.cookResident(g, ctx, nullptr, termPath);
  const SwBuffer* outBuf = pg.residentSwBufferFor(termPath);
  bool haveOut = outBuf && outBuf->bytes && outBuf->elementCount == kN;
  std::vector<SwPoint> got(kN);
  if (haveOut) std::memcpy(got.data(), const_cast<MTL::Buffer*>(outBuf->bytes)->contents(), kN * sizeof(SwPoint));

  // Oracle: the mathv-verified CPU ref, fed a count+1 array (padding slot = NaN-Scale, matching this
  // kernel's real-OOB-guard fork invariant) with `out` pre-seeded per the NO-WRITEBACK QUIRK contract.
  std::vector<SwPoint> inPadded(kN + 1);
  std::memcpy(inPadded.data(), in.data(), kN * sizeof(SwPoint));
  inPadded[kN] = SwPoint{};
  inPadded[kN].Scale.x = std::numeric_limits<float>::quiet_NaN();
  std::vector<SwPoint> exp(kN);
  for (uint32_t i = 0; i < kN; ++i) exp[i] = in[i];  // pre-seed (skip-path "unchanged" contract)
  // Oracle ALWAYS uses the CLEAN kAmount — this is the whole point of the -bug tooth: prove the
  // injected boundary value actually reaches the kernel cbuffer. If it didn't (dead injection), the
  // GPU readback would stay at kAmount's result regardless of amtInjected and still match here.
  mathv_ref::mathvRefReorientLinePoints(inPadded.data(), exp.data(), kN, kAmount);

  double maxRotErr = 0.0, maxPosErr = 0.0; int worstI = -1;
  if (haveOut)
    for (uint32_t i = 0; i < kN; ++i) {
      float dqx = exp[i].Rotation.x - got[i].Rotation.x, dqy = exp[i].Rotation.y - got[i].Rotation.y,
            dqz = exp[i].Rotation.z - got[i].Rotation.z, dqw = exp[i].Rotation.w - got[i].Rotation.w;
      double eRot = std::sqrt((double)dqx*dqx + (double)dqy*dqy + (double)dqz*dqz + (double)dqw*dqw);
      float dpx = exp[i].Position.x - got[i].Position.x, dpy = exp[i].Position.y - got[i].Position.y,
            dpz = exp[i].Position.z - got[i].Position.z;
      double ePos = std::sqrt((double)dpx*dpx + (double)dpy*dpy + (double)dpz*dpz);
      if (eRot > maxRotErr) { maxRotErr = eRot; worstI = (int)i; }
      maxPosErr = std::max(maxPosErr, ePos);
    }
  printf("[reorient-retire] ②parity: haveOut=%d maxRotErr=%.6f(need<1e-3) maxPosErr=%.6f worstI=%d\n",
         haveOut ? 1 : 0, maxRotErr, maxPosErr, worstI);

  // did-not-trip guard: non-bug must see NON-IDENTITY rotation on most live points (else a vacuous
  // passthrough passes hollow).
  double maxRotFromIdentity = 0.0;
  if (haveOut) for (uint32_t i = 0; i < kN; ++i) {
    if (i == kDeadIdx) continue;
    maxRotFromIdentity = std::max(maxRotFromIdentity, (double)std::fabs(1.0f - got[i].Rotation.w));
  }

  mlib->release(); q->release(); dev->release();
  g_fixture = nullptr;

  const bool parityGreen = haveOut && (maxRotErr < 1e-3) && (maxPosErr < 1e-4) && (maxRotFromIdentity > 1e-3);
  if (!injectBug) {
    printf("[reorient-retire] ②parity VERDICT: %s (maxRotFromIdentity=%.4f)\n",
           parityGreen ? "GREEN" : "RED", maxRotFromIdentity);
    pool->release();
    return parityGreen ? 0 : 1;
  }
  // -bug: the perturbed Amount must break parity. dead tooth → 0 (NO-BITE list catches it).
  const bool bites = !parityGreen;
  printf("[reorient-retire] ②parity -bug: %s\n", bites ? "BITES (Amount cbuffer slot load-bearing)" : "TOOTHLESS");
  pool->release();
  return bites ? 1 : 0;
}

// ── ④ LAYOUT (inline — .t3ui Position constants) ──────────────────────────────────────────────────
int runReorientLayoutGate(bool injectBug) {
  std::string id;
  if (!symbolIdOfT3(kT3, &id) || id != std::string(kGuid)) { printf("[reorient-retire] ④layout FAIL: id\n"); return 1; }
  const T3LayoutResolver layoutResolve = [&id](const std::string& guid, std::string& out) -> bool {
    if (guid != id) return false; out = kT3ui; return true; };

  t3LayoutDisable() = injectBug;
  SymbolLibrary lib; std::string rootId; std::vector<std::string> warnings;
  const bool ok = importT3Symbol(kT3, lib, &rootId, &warnings, T3Resolver{}, layoutResolve);
  t3LayoutDisable() = false;
  if (!ok || rootId != std::string(kGuid)) { printf("[reorient-retire] ④layout FAIL: import\n"); return 1; }
  Symbol* s = lib.find(rootId);
  if (!s) { printf("[reorient-retire] ④layout FAIL: no root\n"); return 1; }

  const SlotDef* amt = nullptr; for (const SlotDef& d : s->inputDefs)  if (d.id == kPinAmount) amt = &d;
  const SlotDef* pts = nullptr; for (const SlotDef& d : s->inputDefs)  if (d.id == kPinPoints) pts = &d;
  const SlotDef* out = nullptr; for (const SlotDef& d : s->outputDefs) if (d.id == kPinOutput) out = &d;
  if (!amt || !pts || !out) {
    printf("[reorient-retire] ④layout FAIL: amt=%p pts=%p out=%p\n", (void*)amt, (void*)pts, (void*)out);
    return 1;
  }
  const bool amtOk = injectBug ? (nearf(amt->x, 0) && nearf(amt->y, 0)) : (nearf(amt->x, kAmtX) && nearf(amt->y, kAmtY));
  const bool ptsOk = injectBug ? (nearf(pts->x, 0) && nearf(pts->y, 0)) : (nearf(pts->x, kPtsX) && nearf(pts->y, kPtsY));
  const bool outOk = injectBug ? (nearf(out->x, 0) && nearf(out->y, 0)) : (nearf(out->x, kOutX) && nearf(out->y, kOutY));
  printf("[reorient-retire] ④layout: amt(%.3f,%.3f) pts(%.3f,%.3f) out(%.3f,%.3f)\n",
         amt->x, amt->y, pts->x, pts->y, out->x, out->y);

  const bool distinct = !(nearf(amt->x, pts->x) && nearf(amt->y, pts->y)) &&
                        !(nearf(amt->x, out->x) && nearf(amt->y, out->y)) &&
                        !(nearf(pts->x, out->x) && nearf(pts->y, out->y));
  const bool nonZero = !(nearf(amt->x, 0) && nearf(amt->y, 0));
  if (!injectBug) {
    if (!(nonZero && distinct)) { printf("[reorient-retire] ④layout NO-BITE: seam not exercised\n"); return 0; }
    return (amtOk && ptsOk && outOk) ? 0 : 1;
  }
  const bool bites = (amtOk && ptsOk && outOk);  // under -bug all reverted to 0,0
  return bites ? 1 : 0;
}

int runT3ReorientLinePointsRetireGates(bool injectBug) {
  registerBuiltinPointOps();

  SymbolLibrary lib;
  std::string rootId; std::vector<std::string> warnings;
  if (!importT3Symbol(kT3, lib, &rootId, &warnings) || rootId != std::string(kGuid)) {
    printf("[reorient-retire] FAIL: import\n"); return 1;
  }
  refreshCompoundSpecs(lib);

  // ① TAKEOVER POLARITY
  const NodeSpec* g1spec = findSpec(kName);
  const Symbol* csym = lib.find(kGuid);
  const bool g1green = g1spec && g1spec->type == std::string(kGuid) && g1spec->evaluate == nullptr &&
                       csym && !csym->atomic && !csym->children.empty();
  bool g1bit = false;
  if (injectBug) {
    pointModifySpecSink().push_back(standInFlatAtomSpec());
    const NodeSpec* shadowed = findSpec(kName);
    g1bit = shadowed && shadowed->type == std::string(kName);
    pointModifySpecSink().pop_back();
  }
  printf("[reorient-retire] ①takeover: findSpec(\"%s\")->type=%s atomic=%d children=%d -> %s\n",
         kName, g1spec ? g1spec->type.c_str() : "<null>", csym ? (int)csym->atomic : -1,
         csym ? (int)csym->children.size() : -1,
         injectBug ? (g1bit ? "BITES" : "TOOTHLESS") : (g1green ? "GREEN" : "RED"));

  // ③ REFERENCE REACHABILITY + COOKABILITY
  bool g3green = false; size_t g3nodes = 0; int g3in = 0, g3out = 0;
  if (g1spec) {
    countPorts(*g1spec, g3in, g3out);
    ResidentEvalGraph eg = buildEvalGraph(lib, g1spec->type);
    g3nodes = eg.nodes.size();
    g3green = g3in > 0 && g3out > 0 && g3nodes > 0;
  }
  bool g3bit = false;
  if (injectBug) {
    setDynamicSpecs({});
    g3bit = (findSpec(kName) == nullptr);
    refreshCompoundSpecs(lib);
  }
  printf("[reorient-retire] ③reference: in=%d out=%d nodes=%zu -> %s\n", g3in, g3out, g3nodes,
         injectBug ? (g3bit ? "BITES" : "TOOTHLESS") : (g3green ? "GREEN" : "RED"));

  // ② PARITY (cook-driven)
  const int g2 = runT3ReorientLinePointsParity(injectBug);
  const bool g2green = (g2 == 0), g2bit = (g2 != 0);
  printf("[reorient-retire] ②parity -> %s\n", injectBug ? (g2bit ? "BITES" : "TOOTHLESS") : (g2green ? "GREEN" : "RED"));

  // ④ LAYOUT
  const int g4 = runReorientLayoutGate(injectBug);
  const bool g4green = (g4 == 0), g4bit = (g4 != 0);
  printf("[reorient-retire] ④layout -> %s\n", injectBug ? (g4bit ? "BITES" : "TOOTHLESS") : (g4green ? "GREEN" : "RED"));

  setDynamicSpecs({});

  if (!injectBug) {
    const bool green = g1green && g3green && g2green && g4green;
    printf("[reorient-retire] VERDICT: %s (①%d ③%d ②%d ④%d)\n", green ? "PASS (retirement takeover LIVE)" : "FAIL",
           g1green, g3green, g2green, g4green);
    return green ? 0 : 1;
  }
  const bool allBit = g1bit && g3bit && g2bit && g4bit;
  printf("[reorient-retire] -bug VERDICT: %s (①%d ③%d ②%d ④%d)\n",
         allBit ? "ALL TEETH BITE" : "DEAD TOOTH (NO-BITE)", g1bit, g3bit, g2bit, g4bit);
  return allBit ? 1 : 0;
}

}  // namespace sw
