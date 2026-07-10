// field_ops_fieldtoimage — the FieldToImage TEX op: a SECOND OUTPUT-window bridge for the field-SDF
// island (the RaymarchField precedent — field_ops_raymarchfield.cpp — proved the "Field input, Texture2D
// output, registerTexOp" seam; this op RIDES it, not builds it). Unlike RaymarchField (3D sphere-trace),
// FieldToImage renders a flat 2D SLICE of the field (Center/Scale/Rotate/SliceDepth placement) and maps
// the signed distance through a Gradient LUT (Range/GainAndBias/PingPong/Repeat), or (Mode=UseColor)
// returns the field's own f.rgb color channel directly.
//
// TiXL authority: external/tixl/Operators/Lib/image/fx/distort/FieldToImage.cs (inputs: Field +
// Center/SliceDepth/Scale/Rotate/Mode/Range/GainAndBias/PingPong/Repeat/Gradient + Resolution/
// OutputFormat/GenerateMips) and its .t3 defaults (Scale=1, SliceDepth=0, Range=(0,1),
// GainAndBias=(0.5,0.5), Mode=0, Center=(0,0), Rotate=0, PingPong=false, Repeat=false, Gradient=
// black(0)->white(1) linear) — mirrored in field_render.h's FieldToImageParams (the host param struct).
//
// GATHER (ZERO extra wiring, same claim RaymarchField's header makes): the NodeSpec below declares a
// "Field" input port AND a "Gradient" input port. point_graph_{tex,resident_tex}_cook.cpp ALREADY gather
// ANY tex op's wired Field input into tc.inputFieldTree and ANY wired Gradient input into
// tc.inputGradients (the SAME rails BoxGradient/LinearGradient already ride for Gradient — see
// point_ops_boxgradient.cpp) — this op only CONSUMES both, unchanged dispatch chains.
//
// RESOLUTION (v1, named fork): FieldToImage.cs has its own Resolution/OutputFormat/GenerateMips inputs
// (a RenderTarget child sized independently of the terminal). This op instead renders straight into
// `c.output` (the driver's resolution-pinned ensureTex texture) — the SAME "output texture drives the
// size" shortcut RaymarchField's header documents ("No camera connection (v1)"). A real Resolution pin
// is a parity-deferred follow-up.
//
// OUTPUT FORMAT: renderFieldToImage returns an RGBA32Float scratch (float for golden readback); this op
// clamps+quantizes it into the driver-owned RGBA8 `tc.output`, identical to cookRaymarchField's
// TONEMAP+COPY (field_ops_raymarchfield.cpp) — needs NO output_window.cpp change.
//
// ZONE: runtime leaf. Crosses runtime→runtime only (renderFieldToImage, rasterizeGradientRow,
// field_graph_builder). The PSO compile goes through the SAME dormant setFieldSourceCompiler fn-ptr seam
// RaymarchField uses — no platform include here.
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

#include "runtime/field_render.h"               // renderFieldToImage, FieldToImageParams
#include "runtime/gradient_raster.h"             // rasterizeGradientRow, kGradientRowN
#include "runtime/image_filter_op_registry.h"    // ImageFilterOp self-registration (texReg + spec + selftest sinks)
#include "runtime/point_graph.h"                 // TexCookCtx, cookParam, cookVecN
#include "runtime/sw_gradient.h"                 // SwGradient

namespace sw {
namespace {

// The FieldToImage render TEMPLATE (string asset, NOT precompiled). Read ONCE per process into a
// function-static; empty if the define is unset/unreadable (cook then clears — no crash). Mirrors
// field_ops_raymarchfield.cpp's raymarchTemplate() loader exactly.
const std::string& fieldToImageTemplate() {
  static const std::string tmpl = []() -> std::string {
#ifdef SW_FIELD_TO_IMAGE_TEMPLATE
    std::ifstream f(SW_FIELD_TO_IMAGE_TEMPLATE);
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

// Clear `out` to transparent black (the no-field / no-gradient / no-template / inject-bug fallback).
// Mirrors field_ops_raymarchfield.cpp's clearOut().
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

// Unwired-Gradient fallback: FieldToImage.t3's OWN Gradient slot default (black@0 -> white@1, Linear).
SwGradient defaultFieldToImageGradient() {
  SwGradient g;
  g.interpolation = kGradientLinear;
  g.steps.push_back({0.0f, simd::make_float4(0.0f, 0.0f, 0.0f, 1.0f)});
  g.steps.push_back({1.0f, simd::make_float4(1.0f, 1.0f, 1.0f, 1.0f)});
  return g;
}

void cookFieldToImage(TexCookCtx& c) {
  if (!c.dev || !c.queue || !c.output) return;
  const uint32_t w = (uint32_t)c.output->width();
  const uint32_t h = (uint32_t)c.output->height();

  if (fieldToImageInjectBug() || !c.inputFieldTree || fieldToImageTemplate().empty() || w == 0 || h == 0) {
    clearOut(c);
    return;
  }

  FieldToImageParams params{};
  float center[2] = {0.0f, 0.0f};
  cookVecN(c, "Center", center, 2, center);
  params.centerX = center[0]; params.centerY = center[1];
  params.scale = cookParam(c, "Scale", params.scale);
  params.rotate = cookParam(c, "Rotate", params.rotate);
  params.sliceDepth = cookParam(c, "SliceDepth", params.sliceDepth);
  params.mode = cookParam(c, "Mode", params.mode);
  float range[2] = {0.0f, 1.0f};
  cookVecN(c, "Range", range, 2, range);
  params.rangeX = range[0]; params.rangeY = range[1];
  float gainBias[2] = {0.5f, 0.5f};
  cookVecN(c, "GainAndBias", gainBias, 2, gainBias);
  params.gainX = gainBias[0]; params.biasY = gainBias[1];
  params.pingPong = cookParam(c, "PingPong", 0.0f);
  params.repeat = cookParam(c, "Repeat", 0.0f);

  const SwGradient& g = (c.inputGradients && !c.inputGradients->empty())
                            ? (*c.inputGradients)[0]
                            : defaultFieldToImageGradient();
  MTL::Texture* gradTex = rasterizeGradientRow(c.dev, g, kGradientRowN);
  if (!gradTex) { clearOut(c); return; }

  MTL::Texture* rendered =
      renderFieldToImage(c.dev, c.queue, c.inputFieldTree, fieldToImageTemplate(), params, gradTex, w, h);
  gradTex->release();  // consumed by the draw; not needed past renderFieldToImage
  if (!rendered) { clearOut(c); return; }

  // Clamp+quantize the RGBA32Float scratch into the driver-owned RGBA8 `tc.output` — identical
  // TONEMAP+COPY shape to cookRaymarchField (field_ops_raymarchfield.cpp), full RGBA (not glow-only).
  std::vector<float> hf((size_t)w * h * 4, 0.0f);
  rendered->getBytes(hf.data(), (NS::UInteger)w * 4 * sizeof(float), MTL::Region::Make2D(0, 0, w, h), 0);
  rendered->release();

  std::vector<uint8_t> u8((size_t)w * h * 4, 0);
  for (size_t i = 0; i < (size_t)w * h; ++i) {
    for (int ch = 0; ch < 4; ++ch) {
      float v = std::max(0.0f, std::min(1.0f, hf[i * 4 + ch]));
      u8[i * 4 + ch] = (uint8_t)std::lround(v * 255.0f);
    }
  }
  c.output->replaceRegion(MTL::Region::Make2D(0, 0, w, h), 0, u8.data(), (NS::UInteger)w * 4);
}

// FieldToImage NodeSpec (the Add-menu / findSpec entry). Ports = FieldToImage.cs Field input (a single
// "Field" port) + the placement/remap scalar slots (their .t3 defaults) + the Gradient input. No
// Resolution/OutputFormat/GenerateMips port (v1 fork — see header).
NodeSpec fieldToImageSpec() {
  NodeSpec s;
  s.type = "FieldToImage";
  s.title = "Field To Image";
  PortSpec field; field.id = "Field"; field.name = "Field"; field.dataType = "Field"; field.isInput = true;
  PortSpec out; out.id = "out"; out.name = "out"; out.dataType = "Texture2D"; out.isInput = false;
  PortSpec cx; cx.id = "Center.x"; cx.name = "Center"; cx.dataType = "Float"; cx.isInput = true;
  cx.def = 0.0f; cx.minV = -2.0f; cx.maxV = 2.0f; cx.widget = Widget::Vec; cx.vecArity = 2;
  PortSpec cy; cy.id = "Center.y"; cy.name = "Center.y"; cy.dataType = "Float"; cy.isInput = true;
  cy.def = 0.0f; cy.minV = -2.0f; cy.maxV = 2.0f;
  PortSpec sliceDepth; sliceDepth.id = "SliceDepth"; sliceDepth.name = "SliceDepth"; sliceDepth.dataType = "Float";
  sliceDepth.isInput = true; sliceDepth.def = 0.0f; sliceDepth.minV = -2.0f; sliceDepth.maxV = 2.0f;
  PortSpec scale; scale.id = "Scale"; scale.name = "Scale"; scale.dataType = "Float";
  scale.isInput = true; scale.def = 1.0f; scale.minV = 0.001f; scale.maxV = 10.0f;
  PortSpec rotate; rotate.id = "Rotate"; rotate.name = "Rotate"; rotate.dataType = "Float";
  rotate.isInput = true; rotate.def = 0.0f; rotate.minV = -180.0f; rotate.maxV = 180.0f;
  PortSpec mode; mode.id = "Mode"; mode.name = "Mode"; mode.dataType = "Float";
  mode.isInput = true; mode.def = 0.0f; mode.minV = 0.0f; mode.maxV = 1.0f; mode.widget = Widget::Enum;
  mode.labels = {"MapDistanceToColor", "UseColor"};
  PortSpec rx; rx.id = "Range.x"; rx.name = "Range"; rx.dataType = "Float"; rx.isInput = true;
  rx.def = 0.0f; rx.minV = -10.0f; rx.maxV = 10.0f; rx.widget = Widget::Vec; rx.vecArity = 2;
  PortSpec ry; ry.id = "Range.y"; ry.name = "Range.y"; ry.dataType = "Float"; ry.isInput = true;
  ry.def = 1.0f; ry.minV = -10.0f; ry.maxV = 10.0f;
  PortSpec gx; gx.id = "GainAndBias.x"; gx.name = "GainAndBias"; gx.dataType = "Float"; gx.isInput = true;
  gx.def = 0.5f; gx.minV = 0.0f; gx.maxV = 1.0f; gx.widget = Widget::Vec; gx.vecArity = 2;
  PortSpec gy; gy.id = "GainAndBias.y"; gy.name = "GainAndBias.y"; gy.dataType = "Float"; gy.isInput = true;
  gy.def = 0.5f; gy.minV = 0.0f; gy.maxV = 1.0f;
  PortSpec pingPong; pingPong.id = "PingPong"; pingPong.name = "PingPong"; pingPong.dataType = "Float";
  pingPong.isInput = true; pingPong.def = 0.0f; pingPong.minV = 0.0f; pingPong.maxV = 1.0f;
  pingPong.widget = Widget::Bool;
  PortSpec repeat; repeat.id = "Repeat"; repeat.name = "Repeat"; repeat.dataType = "Float";
  repeat.isInput = true; repeat.def = 0.0f; repeat.minV = 0.0f; repeat.maxV = 1.0f;
  repeat.widget = Widget::Bool;
  PortSpec gradient; gradient.id = "Gradient"; gradient.name = "Gradient"; gradient.dataType = "Gradient";
  gradient.isInput = true;
  s.ports = {field, out, cx, cy, sliceDepth, scale, rotate, mode, rx, ry, gx, gy, pingPong, repeat, gradient};
  return s;
}

}  // namespace

// Process-global inject-bug toggle for the output golden (mirror of raymarchFieldInjectBug()): the
// golden flips it true so the cook short-circuits to clearOut(black) — production never calls it.
bool& fieldToImageInjectBug() {
  static bool b = false;
  return b;
}

// Self-registration: registerTexOp("FieldToImage", cook) + push the spec into imageFilterSpecSink().
// NO selftest passed here — the output golden is SHELL tier (crosses runtime+platform via the field
// source compiler seam), so it registers in selftests_field.cpp, same posture as RaymarchField.
static const ImageFilterOp _reg_fieldtoimage{fieldToImageSpec(), "FieldToImage", cookFieldToImage};

}  // namespace sw
