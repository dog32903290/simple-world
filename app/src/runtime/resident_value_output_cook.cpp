// runtime/resident_value_output_cook — cookValueOutputNodes: the PRODUCTION (resident-path) cook-emit
// pass for the VALUE-OUTPUT rail (value-output-rail Phase 1). The frame-level twin of
// cookHostScalarNodes / cookColorListNodes, for ops whose value comes from the COOK CONTEXT (not their
// inputs) and is therefore impossible to compute in a pure evaluate() fn.
//
// WHY THIS FILE EXISTS:
//   A handful of TiXL ops read EvaluationContext fields the host evaluate() rail never sees — e.g.
//   RequestedResolution reads context.RequestedResolution (TiXL RequestedResolution.cs:29-37). There is
//   no Float-input it can decompose; the value is purely a function of the cook context. So — exactly
//   like DetectBpm / AudioReaction (which read the live FFT) — these ops carry evaluate==nullptr and are
//   cooked once per frame by a host pass that writes ResidentNode::extOut[outputPortIndex]; the EXACT
//   channel evalResidentFloat already reads for a !evaluate node (resident_eval_graph.cpp). A downstream
//   Float wire to one of these output ports resolves to its extOut slot by port-id match — no new wire
//   type, no new resolver. This file is the missing per-frame writer for the CONTEXT-reading family.
//
// FORK (named) — fork-vec-output-as-n-scalar-ports:
//   TiXL wires ONE Slot<Int2> (Size) / Slot<int> (Width/Height) / Slot<float> (AspectRatio). This runtime
//   exposes the Int2 as TWO scalar Float output ports (Size.Width / Size.Height); Width/Height/AspectRatio
//   are already scalar. Faithful in VALUE (same numbers), forked in wire-CARDINALITY. EXTENDS the shipped
//   input-side scalar-pack fork (a vector is N scalars wearing one widget — graph.h). A .t3 round-trip of a
//   single Int2 wire won't byte-match topologically; the equivalence is semantic, the same bargain sw made
//   on the input side (AddVec3 / MakeResolution).
//
// SCOPE — Phase 1 = RequestedResolution only (reads ctx.requestedWidth/Height). CalcDispatchCount is PURE
//   (a function of its inputs) so it rides the pure-evaluate ValueOp leaf seam instead, NOT this pass.
//   GetTextureSize (reads a Texture2D description) is DEFERRED: the resident graph carries no texture
//   inputs, and the texture cook lives entirely in the PointGraph cook-core recursion (which Phase 1 must
//   not touch) — surfacing a texture description to this frame-level pass is a later seam.
//
// PLACEMENT: runtime leaf (pure CPU; depends only on resident_eval_graph.h + graph.h). Called from
//   app/cook_host_values.cpp once per frame, same slot family as cookHostScalarNodes / cookColorListNodes
//   (ARCHITECTURE: app owns the per-frame orchestration; the compute body lives in runtime).
#include "runtime/resident_eval_graph.h"

#include <cmath>
#include <cstdio>

#include "runtime/graph.h"              // NodeSpec / PortSpec / findSpec
#include "runtime/value_op_registry.h"  // valueOpSelfTests() (the golden registrar, zero shared-file edit)

namespace sw {

// GetForegroundColor cook-seam tooth (the --selftest-foregroundcolor -bug): when true the emit IGNORES
// ctx.foregroundColor and writes the DEFAULT Vector4.One — the exact degeneracy a broken/dropped context
// read would produce (the op no longer reflecting the ambient state). It corrupts the REAL cook path
// (the ctx→extOut read), NOT the expected value (GOLDEN_STANDARD P3). OFF in production; the golden flips
// it around one cook then resets. CPU op flag (constitution rule: no shader branch).
bool& getForegroundColorForceDefaultForTest() {
  static bool f = false;
  return f;
}

void cookValueOutputNodes(ResidentEvalGraph& g, const ResidentEvalCtx& ctx) {
  for (ResidentNode& rn : g.nodes) {
    // RequestedResolution (TiXL Lib.render.utils.RequestedResolution): emit the cook-context resolution.
    //   TiXL RequestedResolution.cs:29-37:
    //     Size.Value        = context.RequestedResolution;           // Int2
    //     Width.Value       = context.RequestedResolution.Width;     // int
    //     Height.Value      = context.RequestedResolution.Height;    // int
    //     AspectRatio.Value = width / (float)height;                 // float (no zero guard → +Inf at h=0)
    // Output-port order in the NodeSpec (= extOut index order):
    //   [0] Size.Width  [1] Size.Height  [2] Width  [3] Height  [4] AspectRatio
    // Size.Width/Height duplicate Width/Height (TiXL's Size is the Int2 packing of the same two values).
    if (rn.opType == "RequestedResolution") {
      const float w = (float)ctx.requestedWidth;
      const float h = (float)ctx.requestedHeight;
      rn.extOut[0] = w;            // Size.Width
      rn.extOut[1] = h;            // Size.Height
      rn.extOut[2] = w;            // Width
      rn.extOut[3] = h;            // Height
      rn.extOut[4] = w / h;        // AspectRatio (fork-requestedresolution-aspect-zero-guard: h==0 → +Inf,
                                   // matching TiXL's unguarded width/(float)height; 0/0 → NaN)
      continue;
    }
    // GridPosition (TiXL Lib.numbers.vec2.GridPosition): a per-cell grid layout position + size. Unlike
    // RequestedResolution (context-only), it reads BOTH its resolved Float INPUTS (Index, RasterSize.x/y)
    // AND the cook-context aspect (ctx.requestedWidth/Height) — so it lives on this rail (its value cannot
    // come from a pure evaluate() fn: the aspect is context, not an input). Output-port order (= extOut):
    //   [0] Position.x  [1] Position.y  [2] Size.x  [3] Size.y
    //   TiXL GridPosition.cs:19-37 (verbatim):
    //     var index   = Index.GetValue(context);                                          // cs:21 (int)
    //     var size    = RasterSize.GetValue(context);                                      // cs:22 (Int2)
    //     var columns = size.Width.Clamp(1, 10000);                                        // cs:23
    //     var rows    = size.Height.Clamp(1, 10000);                                       // cs:24
    //     var row     = index / columns;                                                   // cs:26 (INT div)
    //     var column  = index - (row * columns);                                           // cs:27
    //     var aspectRatio = (float)context.RequestedResolution.Width / .Height;            // cs:29
    //     var sizeValue   = new Vector2(1f/columns, 1f/rows);                               // cs:30-31
    //     var x = ((float)column/columns - 0.5f)*aspectRatio*2 + sizeValue.X*aspectRatio;  // cs:33
    //     var y = (((float)(rows-row-1)/rows) - 0.5f)*2 + sizeValue.Y;                      // cs:34
    //     Size.Value = sizeValue;  Position.Value = new Vector2(x, y);                      // cs:36-37
    //   The `A` input (Vector2, cs:41) is UNUSED in TiXL's Update — a dead input; sw omits it (no wire
    //   consumes it, no value depends on it → a faithful drop, named fork-gridposition-drop-dead-A).
    //   FORK fork-gridposition-int-via-floatrail: Index/RasterSize are int/Int2 in TiXL; on this rail they
    //   ride Float (truncated toward zero via (int)), so row/column integer division matches C# int math.
    if (rn.opType == "GridPosition") {
      const std::map<std::string, float> in = resolveResidentFloatInputs(g, rn, ctx);
      auto getIn = [&](const char* id, float def) -> float {
        auto it = in.find(id);
        return it != in.end() ? it->second : def;
      };
      const int index = (int)getIn("Index", 0.0f);           // cs:21 (int, .t3 default 0)
      int columns = (int)getIn("RasterSize.x", 0.0f);        // cs:23 size.Width.Clamp(1,10000)
      int rows    = (int)getIn("RasterSize.y", 0.0f);        // cs:24 size.Height.Clamp(1,10000)
      if (columns < 1) columns = 1;
      if (columns > 10000) columns = 10000;
      if (rows < 1) rows = 1;
      if (rows > 10000) rows = 10000;
      const int row    = index / columns;                    // cs:26 (int division)
      const int column = index - (row * columns);            // cs:27
      const float aspect = (float)ctx.requestedWidth / (float)ctx.requestedHeight;  // cs:29 (no zero guard)
      const float sizeX = 1.0f / (float)columns;             // cs:30
      const float sizeY = 1.0f / (float)rows;                // cs:31
      const float x = ((float)column / (float)columns - 0.5f) * aspect * 2.0f + sizeX * aspect;  // cs:33
      const float y = (((float)(rows - row - 1) / (float)rows) - 0.5f) * 2.0f + sizeY;           // cs:34
      rn.extOut[0] = x;      // Position.x
      rn.extOut[1] = y;      // Position.y
      rn.extOut[2] = sizeX;  // Size.x
      rn.extOut[3] = sizeY;  // Size.y
      continue;
    }
    // GetForegroundColor (TiXL Lib.flow.context.GetForegroundColor.cs:14-17): emit the ambient
    // ForegroundColor host state. The op's ENTIRE body is `Result.Value = context.ForegroundColor;`
    // (a pure Vector4 context read — no math, no Command). sw carries ForegroundColor on ctx.foregroundColor
    // (resident_eval_graph.h; default Vector4.One = EvaluationContext.cs:150). Output-port order in the
    // NodeSpec (= extOut index): [0..3] = Result.x/y/z/w. fork-foregroundcolor-vec4-as-4-scalar-ports (the
    // same input-side scalar-pack bargain as RequestedResolution's Int2). No enclosing-Group walk is needed
    // for v1: sw has no ForegroundColor writer op, so the ambient value is always the default (or whatever a
    // future push planted on ctx) — TiXL's own no-writer case returns exactly this default too.
    if (rn.opType == "GetForegroundColor") {
      const bool forceDefault = getForegroundColorForceDefaultForTest();  // -bug: drop the ctx read
      rn.extOut[0] = forceDefault ? 1.0f : ctx.foregroundColor[0];  // Result.x (R)
      rn.extOut[1] = forceDefault ? 1.0f : ctx.foregroundColor[1];  // Result.y (G)
      rn.extOut[2] = forceDefault ? 1.0f : ctx.foregroundColor[2];  // Result.z (B)
      rn.extOut[3] = forceDefault ? 1.0f : ctx.foregroundColor[3];  // Result.w (A)
      continue;
    }
    // (future context-reading cook-emit ops fan out here — same extOut[outputPortIndex] contract.)
  }
}

// --- RequestedResolution cook-emit GOLDEN (3-leg, value-output-rail Phase 1) ----------------------
// Model on detect_bpm.cpp:227-283 (the no-evaluate extOut readback golden). Three legs:
//   (1) cook the node through cookValueOutputNodes with a KNOWN ctx resolution → read extOut[0..4];
//   (2) cross-check evalResidentFloat(<each output port>) returns the SAME extOut (the no-evaluate
//       readback path — evalResidentFloat returns extOut[outIdx] for an evaluate==nullptr node);
//   (3) assert against the HAND-DERIVED expected (1920×1080 → Width=1920, Height=1080, AR=1920/1080),
//       independent of the impl.
// -bug = emit the DEFAULT/0 resolution (the pre-rail behaviour) instead of the ctx value → Width/Height
//        collapse to 0 → RED. (Mirror of detect_bpm's injectBug = wrong recovered value.)
namespace {

int runRequestedResolutionGolden(bool injectBug) {
  const float eps = 1e-4f;
  bool ok = true;

  // 1-node resident graph: a single RequestedResolution (no inputs — RequestedResolution.t3 Inputs == []).
  ResidentEvalGraph g;
  ResidentNode rn;
  rn.path = "1";
  rn.opType = "RequestedResolution";
  g.nodes.push_back(rn);
  g.byPath["1"] = 0;  // the evalResidentFloat lookup index (flatten populates this in production)

  // LEG 1 — cook through the production emit pass with a KNOWN resolution. injectBug feeds 0×0 (the
  // unseeded / pre-rail resolution) so the emitted Width/Height collapse → RED against the 1920×1080
  // expected. This is a TEST-INPUT tooth (mirror of detect_bpm), not an impl flip.
  ResidentEvalCtx ctx;
  ctx.requestedWidth  = injectBug ? 0u : 1920u;
  ctx.requestedHeight = injectBug ? 0u : 1080u;
  cookValueOutputNodes(g, ctx);

  const float exW = 1920.0f, exH = 1080.0f, exAR = 1920.0f / 1080.0f;

  // LEG 1 readback (extOut): [0]Size.Width [1]Size.Height [2]Width [3]Height [4]AspectRatio.
  const float oSizeW = g.nodes[0].extOut[0];
  const float oSizeH = g.nodes[0].extOut[1];
  const float oW     = g.nodes[0].extOut[2];
  const float oH     = g.nodes[0].extOut[3];
  const float oAR    = g.nodes[0].extOut[4];

  // LEG 2 — evalResidentFloat must AGREE with extOut for each output port (the no-evaluate readback).
  const float eSizeW = evalResidentFloat(g, "1", "Size.Width",  ctx);
  const float eSizeH = evalResidentFloat(g, "1", "Size.Height", ctx);
  const float eW     = evalResidentFloat(g, "1", "Width",       ctx);
  const float eH     = evalResidentFloat(g, "1", "Height",      ctx);
  const float eAR    = evalResidentFloat(g, "1", "AspectRatio", ctx);
  const bool evalAgrees =
      std::fabs(eSizeW - oSizeW) < eps && std::fabs(eSizeH - oSizeH) < eps &&
      std::fabs(eW - oW) < eps && std::fabs(eH - oH) < eps && std::fabs(eAR - oAR) < eps;

  // LEG 3 — assert against the hand-derived expected (independent of the impl).
  const bool valuesMatch =
      std::fabs(oW - exW) < eps && std::fabs(oH - exH) < eps && std::fabs(oAR - exAR) < eps &&
      std::fabs(oSizeW - exW) < eps && std::fabs(oSizeH - exH) < eps;

  ok = ok && evalAgrees && valuesMatch;
  std::printf("[selftest-requestedresolutionvalue] cook→extOut Size=(%.0f,%.0f) W=%.0f H=%.0f AR=%.4f "
              "want=(1920,1080) 1920 1080 %.4f | evalResidentFloat agrees=%d%s -> %s\n",
              oSizeW, oSizeH, oW, oH, oAR, exAR, evalAgrees ? 1 : 0,
              injectBug ? " (injectBug→ctx 0×0)" : "", ok ? "PASS" : "FAIL");

  // A second resolution to prove it's not hard-coded: 1280×720 → AR=16/9. (Skip under injectBug — the
  // first leg already carries the RED; re-cooking with a fresh value would mask it.)
  if (!injectBug) {
    ResidentEvalGraph g2;
    ResidentNode rn2; rn2.path = "1"; rn2.opType = "RequestedResolution";
    g2.nodes.push_back(rn2); g2.byPath["1"] = 0;
    ResidentEvalCtx c2; c2.requestedWidth = 1280u; c2.requestedHeight = 720u;
    cookValueOutputNodes(g2, c2);
    const float w2 = g2.nodes[0].extOut[2], h2 = g2.nodes[0].extOut[3], ar2 = g2.nodes[0].extOut[4];
    bool pass2 = std::fabs(w2 - 1280.0f) < eps && std::fabs(h2 - 720.0f) < eps &&
                 std::fabs(ar2 - (1280.0f / 720.0f)) < 1e-3f;
    ok = ok && pass2;
    std::printf("[selftest-requestedresolutionvalue] 1280×720: W=%.0f H=%.0f AR=%.4f want=1280,720,%.4f -> %s\n",
                w2, h2, ar2, 1280.0f / 720.0f, pass2 ? "PASS" : "FAIL");
  }

  return ok ? 0 : 1;
}

// --- GetForegroundColor cook-emit GOLDEN (3-leg, host-state read) --------------------------------
// TiXL GetForegroundColor.cs:14-17 (Update body = `Result.Value = context.ForegroundColor;`). Model on
// runRequestedResolutionGolden. The op is a PURE CONTEXT READ, so the golden proves the emit REFLECTS
// ctx.foregroundColor (not a hard-coded default): it drives the ctx field to a NON-DEFAULT color, cooks,
// and asserts extOut == that color AND evalResidentFloat agrees.
//   Probe color = (0.2, 0.4, 0.6, 0.8) — off the {1,1,1,1} default on EVERY channel (so a dropped read of
//   ANY component is caught; GOLDEN_STANDARD "sample off the identity/saturation platform").
//   -bug (getForegroundColorForceDefaultForTest): the emit ignores ctx.foregroundColor and writes the
//   default {1,1,1,1} → all four channels diverge from the probe → RED. Corrupts the REAL cook read (P3-safe:
//   it flips the IMPL behaviour, not the expected value).
int runGetForegroundColorGolden(bool injectBug) {
  const float eps = 1e-5f;
  bool ok = true;

  // 1-node resident graph: a single GetForegroundColor (no inputs — GetForegroundColor.t3 Inputs == []).
  ResidentEvalGraph g;
  ResidentNode rn;
  rn.path = "1";
  rn.opType = "GetForegroundColor";
  g.nodes.push_back(rn);
  g.byPath["1"] = 0;

  // A KNOWN non-default ForegroundColor pushed onto the cook context (the seam a Group/SetMaterial writer
  // would drive). The op must reflect exactly this.
  ResidentEvalCtx ctx;
  ctx.foregroundColor[0] = 0.2f;
  ctx.foregroundColor[1] = 0.4f;
  ctx.foregroundColor[2] = 0.6f;
  ctx.foregroundColor[3] = 0.8f;

  getForegroundColorForceDefaultForTest() = injectBug;  // -bug: emit ignores ctx → default {1,1,1,1}
  cookValueOutputNodes(g, ctx);
  getForegroundColorForceDefaultForTest() = false;      // reset (never leak into later tests)

  // LEG 1 (extOut readback): [0..3] = Result.x/y/z/w.
  const float oR = g.nodes[0].extOut[0], oG = g.nodes[0].extOut[1];
  const float oB = g.nodes[0].extOut[2], oA = g.nodes[0].extOut[3];

  // LEG 2 — evalResidentFloat must AGREE with extOut for each output port (the no-evaluate readback).
  const float eR = evalResidentFloat(g, "1", "Result.x", ctx);
  const float eG = evalResidentFloat(g, "1", "Result.y", ctx);
  const float eB = evalResidentFloat(g, "1", "Result.z", ctx);
  const float eA = evalResidentFloat(g, "1", "Result.w", ctx);
  const bool evalAgrees = std::fabs(eR - oR) < eps && std::fabs(eG - oG) < eps &&
                          std::fabs(eB - oB) < eps && std::fabs(eA - oA) < eps;

  // LEG 3 — assert against the hand-derived expected (the pushed ctx color, independent of the impl).
  const bool valuesMatch = std::fabs(oR - 0.2f) < eps && std::fabs(oG - 0.4f) < eps &&
                           std::fabs(oB - 0.6f) < eps && std::fabs(oA - 0.8f) < eps;

  ok = ok && evalAgrees && valuesMatch;
  std::printf("[selftest-foregroundcolor] cook→extOut RGBA=(%.2f,%.2f,%.2f,%.2f) want=(0.20,0.40,0.60,0.80) "
              "| evalResidentFloat agrees=%d%s -> %s\n", oR, oG, oB, oA, evalAgrees ? 1 : 0,
              injectBug ? " (injectBug→default {1,1,1,1})" : "", ok ? "PASS" : "FAIL");
  return ok ? 0 : 1;
}

struct GetForegroundColorGoldenRegistrar {
  GetForegroundColorGoldenRegistrar() {
    valueOpSelfTests().push_back({"foregroundcolor", runGetForegroundColorGolden});
  }
};
static const GetForegroundColorGoldenRegistrar _reg_foregroundcolor_golden;

// Golden registrar: DIRECT push into the live-consumed value-op selftest sink (no shared-file edit —
// selftests.cpp iterates valueOpSelfTests() for --selftest-<name> / -bug). Mirror of
// gradient_ops_defineiqgradient.cpp's IqGoldenRegistrar.
struct RequestedResolutionGoldenRegistrar {
  RequestedResolutionGoldenRegistrar() {
    // NOTE the name is requestedresolutionvalue (NOT requestedresolution) — the latter is already taken
    // by point_ops_setrequestedresolution.cpp's SetRequestedResolution RenderTarget VISUAL test (a
    // different op). The shared-name dispatcher matches that one first, so our value-output golden needs
    // its own token. --selftest-requestedresolutionvalue / -bug.
    valueOpSelfTests().push_back({"requestedresolutionvalue", runRequestedResolutionGolden});
  }
};
static const RequestedResolutionGoldenRegistrar _reg_requestedresolution_golden;

}  // namespace

}  // namespace sw
