// t3import_getimagebrightness_golden.cpp — --selftest-t3-getimagebrightness (TEXTURE-COMPUTE SEAM STAGE 3:
// the buffer-rail ComputeShaderStage reads a Texture2D SRV → writes a buffer UAV).
//
// WHAT THIS SEALS (the stage-3 承重線): a BUFFER-out ComputeShaderStage that CONSUMES a Texture2D SRV —
// the CROSS-CURRENCY gather (the buffer cook reaching into the tex rail via cookTexNode, both legs, the
// [中]-risk in point_graph_buffer_cook.cpp) + the stage's enc->setTexture + the default linear-clamp
// sampler + the per-kernel 2D-from-SRV-texture dispatch. It drives the GetImageBrightness `main` kernel
// (computeshaderstage_getimagebrightness.metal, ported from external/tixl cs-GetImageBrightness.hlsl)
// through the REAL resident cook and compares the readback to the R-authored CPU oracle
// (mathv_ref_getimagebrightness.h).
//
// ── WHY A STAGE-COOK GOLDEN, NOT A FULL 4-GATE .t3 IMPORT (honest scope, TEXTURE_COMPUTE_SEAM §5 row 3) ──
// The two blueprint-suggested seal nodes are BOTH structurally unsuitable for a faithful 4-gate .t3 import:
//   • PointsFromMeshData.t3 wires NO texture at all (inputs Count/Data(=FaceBuffer SRV-buf)/Seed); its
//     inputTexture:t1 + SamplerState:s0 are HLSL-declared but the .Sample is COMMENTED OUT and unwired →
//     sealing it exercises zero texture path.
//   • GetImageBrightness.t3's compound OUTPUT is a `float Brightness` reached through a GPU→CPU int readback
//     (_ReadIntFromGpuBuffer) + IntDiv/Div — a host-scalar readback currency the sw buffer rail lacks, so a
//     faithful whole-compound import is stage-4+ work.
// So this golden builds the stage-3 mechanism DIRECTLY (a programmatic compound: texture fixture → buffer
// ComputeShaderStage → zero-init uint UAV, KernelName=computeshaderstage_getimagebrightness), cooks it
// through buildEvalGraph→cookResident targeting the stage, and reads back the UAV — the same COOK-DRIVEN
// ②parity shape the depth golden uses (t3import_depthtolinear_golden.cpp), but on the buffer rail. The
// kernel math itself is proven bit-exact + 逐格 by --selftest-mathv-getimagebrightness.
//
// GATES (GOLDEN_STANDARD.md 三特徵):
//   ① PARITY (cook-driven, MEASURED): cross-currency gather + stage cook (setTexture + 2D dispatch) →
//      readback ResultBuffer[0] == the CPU luminance-sum oracle. injectBug = computeShaderStageSkipTexture()
//      → the texture-SRV bind dropped (gather off) → the reduction reads an unbound texture → adds nothing →
//      the UAV stays at its zero-init (0) → diverges from the non-trivial oracle → BITE.
//   ② MECHANISM REACHABILITY (invariant, no-bug leg): the registered ComputeShaderStage spec carries the
//      "ShaderResourceTextures" Texture2D input port — the seam's stage-3 delta is present, not stubbed.
// did-not-trip → return 0 (GOLDEN_STANDARD 特徵3 / P1). The oracle is the TiXL HLSL (never sw's own output).
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#include <Foundation/Foundation.hpp>
#include <Metal/Metal.hpp>

#include "mathv_ref_getimagebrightness.h"
#include "runtime/buffer_op_registry.h"         // BufferCookCtx / BufferOp
#include "runtime/compound_graph.h"             // Symbol / SymbolChild / SymbolConnection / SlotDef
#include "runtime/eval_context.h"               // EvaluationContext
#include "runtime/graph.h"                       // NodeSpec / PortSpec / findSpec
#include "runtime/graph_bridge.h"                // atomicSymbolFromSpec
#include "runtime/image_filter_op_registry.h"    // ImageFilterOp (texture fixture)
#include "runtime/point_graph.h"                 // TexCookCtx / PointGraph
#include "runtime/resident_eval_graph.h"         // buildEvalGraph / initResidentCache
#include "runtime/sw_buffer.h"                    // SwBuffer

namespace sw {

void registerBuiltinPointOps();
bool& computeShaderStageSkipTexture();  // buffer_ops_computeshaderstage.cpp (①parity injectBug)

namespace {

constexpr uint32_t kW = 40, kH = 24;      // non-8-multiple W/H → the ceil-div dispatch tail (edge threads add 0)
constexpr uint32_t kScaleFactor = 1000;

MTL::Texture* g_brightnessSrc = nullptr;  // the known RGBA32Float input the fixture redirects to

// The SAME per-texel values the oracle reads (r=g=b=v, a=1 → lum=v exactly; v in 1/16ths keeps lum·scale
// clear of integer boundaries so the sum is EXACT). Mirror of the mathv fixture.
inline void texelRGBA(uint32_t x, uint32_t y, float& r, float& g, float& b, float& a) {
  const uint32_t k = (x * 3u + y * 5u + (x ^ y)) % 13u;
  const float v = (float)(k + 1) / 16.0f;
  r = g = b = v;
  a = 1.0f;
}

// ── FIXTURES ───────────────────────────────────────────────────────────────────────────────────────
// Texture fixture: redirects the InputTexture SRV to g_brightnessSrc (mirror of t3xf_depth_source).
void cookBrightnessSource(TexCookCtx& c) { c.redirectTexture = g_brightnessSrc; }
NodeSpec brightnessSourceSpec() {
  NodeSpec s;
  s.type = "t3xf_brightness_source";
  s.title = "t3xf_brightness_source";
  s.ports = {{"out", "out", "Texture2D", false}};
  s.evaluate = nullptr;
  return s;
}
const ImageFilterOp _reg_brightnesssrc{brightnessSourceSpec(), "t3xf_brightness_source",
                                       cookBrightnessSource};

// UAV fixture: a 1-uint buffer ZERO-INITIALIZED (= the folded GetImageBrightness `clear` pass). The stage
// atomic-adds INTO this buffer, so its output IS this buffer, now = the sum.
void cookZeroUintBuffer(BufferCookCtx& c) {
  uint32_t* p = (uint32_t*)c.requestBytes(sizeof(uint32_t));
  if (!p) return;
  p[0] = 0u;
  c.output->elementStride = sizeof(uint32_t);
  c.output->elementCount = 1;
}
NodeSpec zeroUintBufferSpec() {
  NodeSpec s;
  s.type = "t3xf_zero_uint_buffer";
  s.title = "t3xf_zero_uint_buffer";
  s.ports = {{"Output", "out", "Buffer", false}};
  s.evaluate = nullptr;
  return s;
}
const BufferOp _reg_zerouintbuf{zeroUintBufferSpec(), cookZeroUintBuffer};

// CB fixture: a 1-uint buffer = scaleFactor (b0 ParamConstants.scaleFactor; texW/H come from get_width()).
void cookScaleUintBuffer(BufferCookCtx& c) {
  uint32_t* p = (uint32_t*)c.requestBytes(sizeof(uint32_t));
  if (!p) return;
  p[0] = kScaleFactor;
  c.output->elementStride = sizeof(uint32_t);
  c.output->elementCount = 1;
}
NodeSpec scaleUintBufferSpec() {
  NodeSpec s;
  s.type = "t3xf_scale_uint_buffer";
  s.title = "t3xf_scale_uint_buffer";
  s.ports = {{"Output", "out", "Buffer", false}};
  s.evaluate = nullptr;
  return s;
}
const BufferOp _reg_scaleuintbuf{scaleUintBufferSpec(), cookScaleUintBuffer};

// ── Build the stage-3 compound programmatically: texfix→stage.ShaderResourceTextures, zerofix→stage.Uavs,
//    scalefix→stage.ConstantBuffers, KernelName=getimagebrightness, stage.Output→boundary Buffer output. ──
const char* const kRootGuid = "beac1234-0000-4000-8000-000000000001";
constexpr int kStageId = 1, kTexFixId = 2, kZeroFixId = 3, kScaleFixId = 4;

bool buildCompound(SymbolLibrary& lib) {
  const NodeSpec* stageSpec = findSpec("ComputeShaderStage");
  if (!stageSpec) return false;
  // Register the atomic symbols the children instance.
  auto reg = [&](const char* type) {
    if (!lib.symbols.count(type))
      if (const NodeSpec* sp = findSpec(type)) lib.symbols[type] = atomicSymbolFromSpec(*sp);
  };
  reg("ComputeShaderStage");
  reg("t3xf_brightness_source");
  reg("t3xf_zero_uint_buffer");
  reg("t3xf_scale_uint_buffer");

  Symbol root;
  root.id = kRootGuid;
  root.name = "TestGetImageBrightness";
  root.atomic = false;
  root.outputDefs.push_back({"out", "Result", "Buffer", 0.0f});

  SymbolChild stage;
  stage.id = kStageId;
  stage.symbolId = "ComputeShaderStage";
  stage.strOverrides["KernelName"] = "computeshaderstage_getimagebrightness";
  root.children.push_back(stage);

  SymbolChild texfix; texfix.id = kTexFixId; texfix.symbolId = "t3xf_brightness_source"; root.children.push_back(texfix);
  SymbolChild zerofix; zerofix.id = kZeroFixId; zerofix.symbolId = "t3xf_zero_uint_buffer"; root.children.push_back(zerofix);
  SymbolChild scalefix; scalefix.id = kScaleFixId; scalefix.symbolId = "t3xf_scale_uint_buffer"; root.children.push_back(scalefix);
  root.nextChildId = 5;

  auto wire = [&](int sc, const char* ss, int dc, const char* ds) {
    SymbolConnection c; c.srcChild = sc; c.srcSlot = ss; c.dstChild = dc; c.dstSlot = ds;
    root.connections.push_back(c);
  };
  wire(kTexFixId, "out", kStageId, "ShaderResourceTextures");
  wire(kZeroFixId, "out", kStageId, "Uavs");
  wire(kScaleFixId, "out", kStageId, "ConstantBuffers");
  wire(kStageId, "Output", kSymbolBoundary, "out");

  lib.symbols[kRootGuid] = root;
  return true;
}

// ── ①parity: cook the compound through the resident driver, readback ResultBuffer[0], compare oracle. ──
int runParity(bool injectBug) {
  NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();
  registerBuiltinPointOps();

  SymbolLibrary lib;
  if (!buildCompound(lib)) { printf("[getbrightness-seam] ①parity FAIL: build (no ComputeShaderStage spec)\n"); pool->release(); return 1; }

  ResidentEvalGraph g = buildEvalGraph(lib, kRootGuid);
  initResidentCache(g);

  MTL::Device* dev = MTL::CreateSystemDefaultDevice();
  MTL::CommandQueue* q = dev->newCommandQueue();
  NS::Error* err = nullptr;
  MTL::Library* mlib = dev->newLibrary(NS::String::string(SW_SHADER_METALLIB, NS::UTF8StringEncoding), &err);
  if (!mlib) { printf("[getbrightness-seam] ①parity FAIL: no metallib\n"); q->release(); dev->release(); pool->release(); return 1; }

  // Build the known RGBA32Float input the fixture redirects to (SAME values the oracle reads).
  MTL::TextureDescriptor* td =
      MTL::TextureDescriptor::texture2DDescriptor(MTL::PixelFormatRGBA32Float, kW, kH, false);
  td->setUsage(MTL::TextureUsageShaderRead);
  td->setStorageMode(MTL::StorageModeShared);
  g_brightnessSrc = dev->newTexture(td);
  std::vector<float> rgba((size_t)kW * kH * 4, 0.0f);
  for (uint32_t y = 0; y < kH; ++y)
    for (uint32_t x = 0; x < kW; ++x)
      texelRGBA(x, y, rgba[((size_t)y * kW + x) * 4 + 0], rgba[((size_t)y * kW + x) * 4 + 1],
                rgba[((size_t)y * kW + x) * 4 + 2], rgba[((size_t)y * kW + x) * 4 + 3]);
  g_brightnessSrc->replaceRegion(MTL::Region::Make2D(0, 0, kW, kH), 0, rgba.data(),
                                 (NS::UInteger)kW * 4 * sizeof(float));

  computeShaderStageSkipTexture() = injectBug;  // -bug: drop the texture-SRV bind (gather off) → unbound read
  PointGraph pg(dev, mlib, q, 64, 64);
  EvaluationContext ctx{}; ctx.frameIndex = 0; ctx.time = 0.0f; ctx.deltaTime = 1.0f / 60.0f;
  pg.cookResident(g, ctx, nullptr, std::to_string(kStageId));

  const SwBuffer* outBuf = pg.residentSwBufferFor(std::to_string(kStageId));
  const bool haveOut = outBuf && outBuf->bytes && outBuf->elementCount == 1;
  uint32_t gpu = 0;
  if (haveOut) gpu = *(const uint32_t*)const_cast<MTL::Buffer*>(outBuf->bytes)->contents();
  const uint32_t ref = mathv_ref::getImageBrightnessSum(rgba.data(), kW, kH, kScaleFactor);

  computeShaderStageSkipTexture() = false;
  g_brightnessSrc->release(); g_brightnessSrc = nullptr;
  mlib->release(); q->release(); dev->release();

  const uint32_t diff = gpu > ref ? gpu - ref : ref - gpu;
  const bool nonTrivial = ref > 0;
  const bool green = haveOut && nonTrivial && diff == 0;
  printf("[getbrightness-seam] ①parity: haveOut=%d gpu=%u ref=%u diff=%u -> %s\n", haveOut ? 1 : 0, gpu, ref,
         diff, injectBug ? (green ? "TOOTHLESS" : "BITES") : (green ? "GREEN" : "RED"));
  pool->release();
  return green ? 0 : 1;  // no-bug wants 0 (GREEN); -bug wants 1 (diverged)
}

// ── ②mechanism reachability: the ComputeShaderStage spec carries the stage-3 ShaderResourceTextures port. ──
bool stageHasTexturePort() {
  const NodeSpec* s = findSpec("ComputeShaderStage");
  if (!s) return false;
  for (const PortSpec& p : s->ports)
    if (p.isInput && p.id == "ShaderResourceTextures" && p.dataType == "Texture2D") return true;
  return false;
}

}  // namespace

int runT3GetImageBrightnessGates(bool injectBug) {
  registerBuiltinPointOps();

  const bool g2 = stageHasTexturePort();  // invariant (no-bug leg only)
  const int g1 = runParity(injectBug);
  const bool g1green = (g1 == 0);
  const bool g1bit = !g1green;
  printf("[getbrightness-seam] ②reachability: ShaderResourceTextures(Texture2D) port present=%d\n", g2 ? 1 : 0);

  if (!injectBug) {
    const bool green = g1green && g2;
    printf("[getbrightness-seam] VERDICT: %s (①parity=%d ②reach=%d)\n",
           green ? "PASS (buffer-rail texture-SRV compute seam LIVE)" : "FAIL", g1green, g2);
    return green ? 0 : 1;
  }
  printf("[getbrightness-seam] -bug VERDICT: %s (①parity bites=%d)\n",
         g1bit ? "TOOTH BITES" : "DEAD TOOTH (NO-BITE)", g1bit);
  return g1bit ? 1 : 0;
}

}  // namespace sw
