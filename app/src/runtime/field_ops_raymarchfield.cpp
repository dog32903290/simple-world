// field_ops_raymarchfield — the RaymarchField TEX op: the OUTPUT-window bridge for the field-SDF island.
//
// THE SEAM IT CLOSES: SDF leaves (SphereSDF …) output a port dataType="Field". When such a leaf (or any
// field op) is the cook terminal, the terminal dispatch (point_graph_resident.cpp / point_graph.cpp)
// has no "Field" branch → it falls into clearTarget() and the OutputWindow draws black (39 SDF ops were
// authored-but-invisible). RaymarchField is the bridge tex op: it takes ONE "Field" input + the march/
// color scalars and renders the assembled FieldNode tree as a 3D sphere-traced image into a Texture2D.
// Because it registers in texReg(), the EXISTING tex terminal branch (cookTexNode → displayTex) picks it
// up with ZERO change to the dispatch chains — the field becomes visible the moment a RaymarchField sits
// at the terminal. The Field gather (graph "Field" wire → FieldNode tree → tc.inputFieldTree) is done by
// the cook drivers' tex leaves (gatherTex{,Resident}FieldTree); this op only CONSUMES tc.inputFieldTree.
//
// TiXL authority: external/tixl/Operators/Lib/field/render/RaymarchField.cs (inputs: SDFField +
// MaxSteps/StepSize/MinDistance/MaxDistance/DistToColor/AoDistance + the color slots) and its .t3
// defaults — mirrored in field_render.h's RaymarchRenderParams (the host param struct the render uses).
//
// OUTPUT FORMAT (the displayTex contract): renderField3d returns an RGBA32Float texture (the live TiXL
// glow grayscale R=G=B, float for golden readback). But displayTex / pg.target() flows into ImGui::Image
// AND eye::dumpTextureRGBA — both assume 4-byte RGBA8 (eye.mm getBytes uses w*4). So this op renders into
// a scratch RGBA32Float and TONEMAPS+COPIES it into the driver-owned RGBA8 `tc.output` (the resolution-
// pinned ensureTex texture). This keeps the present/eye/displayTex path byte-format-compatible and needs
// NO output_window.cpp change. The grayscale glow maps straight to RGB (g→(g,g,g,1)); a true HDR tonemap
// is a parity-deferred follow-up (the live TiXL output is already in [0,1], steps/MaxSteps).
//
// ZONE: runtime leaf. Crosses runtime→runtime only (renderField3d, field_camera, field_graph_builder).
// The PSO compile inside renderField3d goes through the dormant setFieldSourceCompiler fn-ptr seam (the
// app wires it; the golden wires it directly) — no platform include here.
#include "runtime/point_ops.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include <Foundation/Foundation.hpp>
#include <Metal/Metal.hpp>

#include "runtime/field_camera.h"               // RaymarchTransforms, defaultRaymarchTransforms
#include "runtime/field_render.h"               // renderField3d, RaymarchRenderParams
#include "runtime/image_filter_op_registry.h"   // ImageFilterOp self-registration (texReg + spec + selftest sinks)
#include "runtime/point_graph.h"                // TexCookCtx, cookParam, cookVecN

namespace sw {
namespace {

// The 3D sphere-trace render TEMPLATE (string asset, NOT precompiled). Read ONCE per process into a
// function-static; empty if the define is unset/unreadable (cook then clears — no crash). Mirrors
// point_ops_forcetemplates.cpp's loaders exactly (the field-into-force template precedent).
const std::string& raymarchTemplate() {
  static const std::string tmpl = []() -> std::string {
#ifdef SW_FIELD_RAYMARCH_TEMPLATE
    std::ifstream f(SW_FIELD_RAYMARCH_TEMPLATE);
    if (!f) return std::string();
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
#else
    return std::string();
#endif
  }();
  return tmpl;
}

// Clear `out` to opaque black (the no-field / no-template / inject-bug fallback). Mirrors the image-
// filter no-input contract (point_ops_dither.cpp's clear branch).
void clearOut(TexCookCtx& c) {
  if (!c.output || !c.queue) return;
  MTL::RenderPassDescriptor* pass = MTL::RenderPassDescriptor::renderPassDescriptor();
  auto* ca = pass->colorAttachments()->object(0);
  ca->setTexture(c.output);
  ca->setLoadAction(MTL::LoadActionClear);
  ca->setClearColor(MTL::ClearColor::Make(0.0, 0.0, 0.0, 1.0));
  ca->setStoreAction(MTL::StoreActionStore);
  MTL::CommandBuffer* cmd = c.queue->commandBuffer();
  cmd->renderCommandEncoder(pass)->endEncoding();
  cmd->commit();
  cmd->waitUntilCompleted();
}

void cookRaymarchField(TexCookCtx& c) {
  if (!c.dev || !c.queue || !c.output) return;
  const uint32_t w = (uint32_t)c.output->width();
  const uint32_t h = (uint32_t)c.output->height();

  // No wired field (tc.inputFieldTree null), no template, or the inject-bug short-circuit → black.
  if (raymarchFieldInjectBug() || !c.inputFieldTree || raymarchTemplate().empty() || w == 0 || h == 0) {
    clearOut(c);
    return;
  }

  // March + color scalars from the resolved params (RaymarchField.cs slots; defaults = .t3, carried by
  // RaymarchRenderParams). Only the march scalars drive the LIVE glow; the colors feed the parity-
  // deferred Blinn-Phong path (kept so a future color revival reads real values).
  RaymarchRenderParams rp{};
  rp.maxSteps    = cookParam(c, "MaxSteps", rp.maxSteps);
  rp.stepSize    = cookParam(c, "StepSize", rp.stepSize);
  rp.minDistance = cookParam(c, "MinDistance", rp.minDistance);
  rp.maxDistance = cookParam(c, "MaxDistance", rp.maxDistance);
  rp.distToColor = cookParam(c, "DistToColor", rp.distToColor);
  rp.aoDistance  = cookParam(c, "AoDistance", rp.aoDistance);
  // Color Vec4s read by .r/.g/.b/.a (the port-id convention this spec + Dither use; cookVecN's .x/.y/.z
  // suffixes would miss them). Color path only — these do NOT affect the live glow output below.
  static const char* kRGBA[4] = {".r", ".g", ".b", ".a"};
  auto readColor = [&](const char* base, float* dst) {
    for (int i = 0; i < 4; ++i) dst[i] = cookParam(c, (std::string(base) + kRGBA[i]).c_str(), dst[i]);
  };
  readColor("Specular", rp.specular);
  readColor("Glow", rp.glow);
  readColor("AmbientOcclusion", rp.ambientOcclusion);
  readColor("Background", rp.background);

  // No camera connection (v1): TiXL's DEFAULT camera at the output aspect (the parity target pinned by
  // --selftest-field-camera / --selftest-field-raymarch).
  const float aspect = (float)w / (float)h;
  RaymarchTransforms xf = defaultRaymarchTransforms(aspect);

  // Render the assembled tree into an OWNED RGBA32Float scratch (PSO cached on srcHash inside).
  MTL::Texture* rendered = renderField3d(c.dev, c.queue, c.inputFieldTree, raymarchTemplate(), xf, rp, w, h);
  if (!rendered) {  // compile/PSO/alloc failure (logged upstream) → black, no crash
    clearOut(c);
    return;
  }

  // TONEMAP+COPY the float glow (R=G=B) into the driver-owned RGBA8 `tc.output`. Both are StorageMode
  // Shared (renderField3d + ensureTex), so a CPU read+write is correct and avoids a second GPU PSO. The
  // glow is already in [0,1]; clamp+quantize to 8-bit and splat to RGB (alpha opaque).
  std::vector<float> hf((size_t)w * h * 4, 0.0f);
  rendered->getBytes(hf.data(), (NS::UInteger)w * 4 * sizeof(float), MTL::Region::Make2D(0, 0, w, h), 0);
  rendered->release();  // scratch consumed

  std::vector<uint8_t> u8((size_t)w * h * 4, 0);
  for (size_t i = 0; i < (size_t)w * h; ++i) {
    float g = hf[i * 4];  // glow in R (= G = B from the live template)
    g = std::max(0.0f, std::min(1.0f, g));
    uint8_t v = (uint8_t)std::lround(g * 255.0f);
    u8[i * 4 + 0] = v; u8[i * 4 + 1] = v; u8[i * 4 + 2] = v; u8[i * 4 + 3] = 255;
  }
  c.output->replaceRegion(MTL::Region::Make2D(0, 0, w, h), 0, u8.data(), (NS::UInteger)w * 4);
}

// RaymarchField NodeSpec (the Add-menu / findSpec entry). Ports = RaymarchField.cs SDFField input (a
// single "Field" port — the gather builds the upstream FieldNode tree) + the march/color scalar slots
// with their .t3 defaults (== RaymarchRenderParams). The single "Texture2D" OUTPUT makes it a tex-flow
// terminal so the EXISTING tex dispatch shows it. (No Texture2D INPUT → it is not an image filter; the
// ImageFilterOp registrar is reused PURELY as the self-registration vehicle — registerTexOp + spec sink
// + selftest pair; the registrar does not constrain port types.)
NodeSpec raymarchFieldSpec() {
  NodeSpec s;
  s.type = "RaymarchField";
  s.title = "Raymarch Field";
  PortSpec field; field.id = "SDFField"; field.name = "SDFField"; field.dataType = "Field"; field.isInput = true;
  PortSpec out; out.id = "out"; out.name = "out"; out.dataType = "Texture2D"; out.isInput = false;
  PortSpec maxSteps; maxSteps.id = "MaxSteps"; maxSteps.name = "MaxSteps"; maxSteps.dataType = "Float";
  maxSteps.isInput = true; maxSteps.def = 100.0f; maxSteps.minV = 1.0f; maxSteps.maxV = 512.0f;
  PortSpec stepSize; stepSize.id = "StepSize"; stepSize.name = "StepSize"; stepSize.dataType = "Float";
  stepSize.isInput = true; stepSize.def = 1.0f; stepSize.minV = 0.001f; stepSize.maxV = 4.0f;
  PortSpec minD; minD.id = "MinDistance"; minD.name = "MinDistance"; minD.dataType = "Float";
  minD.isInput = true; minD.def = 0.002f; minD.minV = 0.0f; minD.maxV = 1.0f;
  PortSpec maxD; maxD.id = "MaxDistance"; maxD.name = "MaxDistance"; maxD.dataType = "Float";
  maxD.isInput = true; maxD.def = 300.0f; maxD.minV = 0.0f; maxD.maxV = 1000.0f;
  PortSpec d2c; d2c.id = "DistToColor"; d2c.name = "DistToColor"; d2c.dataType = "Float";
  d2c.isInput = true; d2c.def = 0.15f; d2c.minV = 0.0f; d2c.maxV = 10.0f;
  PortSpec ao; ao.id = "AoDistance"; ao.name = "AoDistance"; ao.dataType = "Float";
  ao.isInput = true; ao.def = 1.0f; ao.minV = 0.0f; ao.maxV = 10.0f;
  // Color slots (Vec4; color path only — kept for parity-revival, do not affect the live glow output).
  auto vec4 = [](const char* base, float r, float g, float b, float a) {
    std::vector<PortSpec> v;
    PortSpec x; x.id = std::string(base) + ".r"; x.name = base; x.dataType = "Float"; x.isInput = true;
    x.def = r; x.minV = 0.0f; x.maxV = 1.0f; x.widget = Widget::Vec; x.vecArity = 4;
    PortSpec y; y.id = std::string(base) + ".g"; y.name = std::string(base) + ".g"; y.dataType = "Float";
    y.isInput = true; y.def = g; y.minV = 0.0f; y.maxV = 1.0f; y.widget = Widget::Vec; y.vecArity = 1;
    PortSpec z; z.id = std::string(base) + ".b"; z.name = std::string(base) + ".b"; z.dataType = "Float";
    z.isInput = true; z.def = b; z.minV = 0.0f; z.maxV = 1.0f; z.widget = Widget::Vec; z.vecArity = 1;
    PortSpec w; w.id = std::string(base) + ".a"; w.name = std::string(base) + ".a"; w.dataType = "Float";
    w.isInput = true; w.def = a; w.minV = 0.0f; w.maxV = 1.0f; w.widget = Widget::Vec; w.vecArity = 1;
    v = {x, y, z, w};
    return v;
  };
  s.ports = {field, out, maxSteps, stepSize, minD, maxD, d2c, ao};
  for (const char* base : {"Specular", "Glow", "Background"}) {
    float d = 1.0f;  // Specular/Glow default white; Background default black (.a=1)
    bool bg = std::string(base) == "Background";
    auto vv = vec4(base, bg ? 0.0f : d, bg ? 0.0f : d, bg ? 0.0f : d, 1.0f);
    s.ports.insert(s.ports.end(), vv.begin(), vv.end());
  }
  {  // AmbientOcclusion default (0,0,0,0.002) — RaymarchRenderParams.ambientOcclusion
    auto vv = vec4("AmbientOcclusion", 0.0f, 0.0f, 0.0f, 0.002f);
    s.ports.insert(s.ports.end(), vv.begin(), vv.end());
  }
  return s;
}

}  // namespace

// Process-global inject-bug toggle (mirror of meshInjectBug()): the golden flips it true so the cook
// short-circuits to clearOut(black) → the raymarch silhouette vanishes → the golden's center-vs-corner
// margin collapses → RED. Off in production (every cook renders the field).
bool& raymarchFieldInjectBug() {
  static bool b = false;
  return b;
}

// Self-registration: registerTexOp("RaymarchField", cook) + push the spec into imageFilterSpecSink()
// (so findSpec / the Add menu / the tex terminal dispatch discover it). The ImageFilterOp registrar is
// the SAME self-registration vehicle point_ops_dither.cpp uses; RaymarchField is NOT an image filter (no
// Texture2D input) but the registrar does not constrain port types. NO selftest passed here: the output
// golden is SHELL tier (it crosses platform via the field source compiler), so it registers in
// selftests_field.cpp ("raymarchfield-output") — a runtime leaf must NOT name a shell-tier golden.
static const ImageFilterOp _reg_raymarchfield{raymarchFieldSpec(), "RaymarchField", cookRaymarchField};

}  // namespace sw
