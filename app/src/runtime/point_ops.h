// runtime/point_ops — the built-in point operators (cook fns) for the lane-A point graph.
// Each is a faithful port of its TiXL counterpart (external/tixl .../point/**): crawl the
// .cs/.hlsl/.help, port the kernel + a cook fn, prove it with a golden selftest against
// TiXL's own formula. They register into point_graph's table via registerBuiltinPointOps()
// (declared in point_graph.h) — called once at app startup and by each cook selftest.
// Adding an operator = one cook fn + one registerPointOp line + its .metal + its golden.
#pragma once
#include "runtime/point_graph.h"  // RenderResolution (RenderTarget op contract)
namespace sw {
struct Node;
// Headless golden proof of the RadialPoints cook op THROUGH the point-graph: cook
// RadialPoints -> a capture draw, assert the bag lies on a circle of the requested radius
// and is spread around it. injectBug sets Cycles=0 so all points collapse to one angle
// (spread -> 0) and the test FAILS — real degenerate, not a flipped assertion.
int runRadialOpSelfTest(bool injectBug);
// Vector-param contract golden: RadialPoints with Center=(5,0,0) translates the whole ring
// (mean x ~= 5, ring preserved around the new center). Proves NodeSpec Vec ports -> readVecN
// -> RadialParams -> shader end to end. injectBug omits Center so the assertion FAILS.
int runRadialCenterSelfTest(bool injectBug);
// Golden proof of the DrawPoints draw op: cook RadialPoints -> DrawPoints (real renderer),
// assert a lit ring + black center in the target texture. injectBug (0 points) -> all black.
int runDrawOpSelfTest(bool injectBug);
// Golden proof of the ParticleSystem sim op: cook RadialPoints->ParticleSystem(sim)->capture,
// step N frames, assert turbulence pushed points off the ring. injectBug (Amount=0) -> no flow.
int runSimOpSelfTest(bool injectBug);
// LinePoints generator golden (point_ops_linepoints.cpp). injectBug = real degeneracy.
int runLinePointsSelfTest(bool injectBug);
// GridPoints generator golden (point_ops_gridpoints.cpp). injectBug = real degeneracy.
int runGridPointsSelfTest(bool injectBug);
// SpherePoints generator golden (point_ops_spherepoints.cpp). injectBug = real degeneracy.
int runSpherePointsSelfTest(bool injectBug);
// TransformPoints MODIFIER golden (point_ops_transformpoints.cpp): ring -> scale+translate.
// injectBug = Strength 0 -> identity passthrough -> ring unchanged. First modifier (in->out bag).
int runTransformPointsSelfTest(bool injectBug);
// OrientPoints MODIFIER golden (point_ops_orientpoints.cpp). injectBug = real degeneracy.
int runOrientPointsSelfTest(bool injectBug);
// RandomizePoints MODIFIER golden (point_ops_randomizepoints.cpp). injectBug = real degeneracy.
int runRandomizePointsSelfTest(bool injectBug);
// SetPointAttributes MODIFIER golden (point_ops_setpointattributes.cpp). injectBug = real degeneracy.
int runSetPointAttributesSelfTest(bool injectBug);
// CombineBuffers COMBINE golden (point_ops_combinebuffers.cpp): concat N bags, count = sum.
// injectBug = drop one input -> count != sum. First combine op (multi-input -> one bag).
int runCombineBuffersSelfTest(bool injectBug);

// --- RenderTarget texture op (point_ops_rendertarget.cpp, render-target pivot) ---
// Resolve a RenderTarget node's output resolution: WindowFollow -> windowSize, else a
// fixed/custom size. The resolution PIN. Used by the cook driver (batch 3) + its golden.
// (resolveRenderResolution is declared in point_graph.h, next to RenderResolution.)
// Register the RenderTarget op into the texture stream (texReg). Wired into the live cook
// (cook() tex terminal) since batch 2; registerBuiltinPointOps registers it for production.
void registerRenderTargetOp();
// The RenderCommand executor (rasterizes a draw chain into TexCookCtx::output). The texReg entry
// for "RenderTarget" AND the shared rasterizer for every draw-op leaf selftest (DrawPoints/
// DrawLines/DrawBillboards drive a CPU-built chain straight through it).
struct TexCookCtx;
void cookRenderTarget(TexCookCtx& c);
// RenderTarget golden: a CPU point bag -> 1-item RenderCommand -> RenderTarget texture,
// assert lit (non-black) + the resolution contract (HD1080->1920x1080, WindowFollow->win).
// injectBug = 0 points -> all black -> FAIL.
int runRenderTargetSelfTest(bool injectBug);
// RenderTarget WIRED golden (batch 3): RadialPoints->DrawPoints(Command)->RenderTarget(Custom
// 256x256) cooked through PointGraph as the terminal — proves tex-terminal selection, the
// resolution pin sizing its own texture, and the Command wire. injectBug drops the wire -> FAIL.
int runRenderTargetWiredSelfTest(bool injectBug);

// --- Blur image filter texture op (point_ops_blur.cpp, lane I) ---
// The FIRST image filter (Texture2D in -> Texture2D out): a 2-pass directional Gaussian blur
// (TiXL image/fx/blur/Blur). Register into the texture stream (texReg). Wired into the cook tex
// terminal AND mid-walk via cookTexNode's Texture2D gather (TexCookCtx::inputTexture).
void registerBlurOp();
// (runBlurSelfTest / runBlurChainSelfTest are declared in point_graph.h next to the other goldens.)

// --- Displace image filter texture op (point_ops_displace.cpp, lane D2) ---
// The SECOND image filter and the FIRST op with TWO Texture2D inputs (Image + DisplaceMap): a TiXL
// image warp (image/fx/distort/Displace). Reads TexCookCtx::inputTextures[0..1] (the multi-input
// gather承重線). Register into the texture stream (texReg).
void registerDisplaceOp();
// (runDisplaceSelfTest / runDisplaceChainSelfTest are declared in point_graph.h next to the goldens.)

// --- DrawLines / DrawBillboards command ops (point_ops_drawlines.cpp / point_ops_drawbillboards.cpp,
// lane L). Both Points→Command producers (DrawKind::Lines / ::Billboards); the executor
// cookRenderTarget rasterizes them. Register into the command stream (cmdReg). ---
void registerDrawLinesOp();
void registerDrawBillboardsOp();
// DrawLines golden: a row of points with a W=NaN break → assert the segment body is lit between
// the kept endpoints AND the broken half stays dark. injectBug = LineWidth 0 → no band → FAIL.
int runDrawLinesSelfTest(bool injectBug);
// DrawBillboards golden: a single point → assert the sprite quad covers an AREA (>1px).
// injectBug = Scale 0 → zero-area quad → ~no lit pixels → FAIL.
int runDrawBillboardsSelfTest(bool injectBug);
}  // namespace sw
