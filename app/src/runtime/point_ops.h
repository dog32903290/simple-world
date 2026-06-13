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
// DirectionalForce golden (particle-force lane): a constant Direction=(0,-1,0) push drags the
// pool's center-of-mass down (meanY<0). injectBug (Amount=0) -> symmetric ring -> FAIL.
int runDirectionalForceSelfTest(bool injectBug);
// VectorFieldForce golden (particle-force lane, fork-VFF): no field bound -> constant (1,1,1)
// push -> the pool drifts diagonally (every mean component >0 + isotropic). injectBug Amount=0 -> FAIL.
int runVectorFieldForceSelfTest(bool injectBug);
// REFUTER-F probe (assertion 2 cook side): drive an OUT-OF-RANGE _ForceKind (99 / -5, as a
// corrupted .swproj would) into a DirectionalForce node and cook — proves no crash / no OOB
// kernel; an unrecognized kind falls to the turbulence else (bounded misroute). injectBug makes
// kind=99 the working directional value -> the "fell to turbulence" assertion FAILS (teeth).
int runForceKindOobSelfTest(bool injectBug);
// LinePoints generator golden (point_ops_linepoints.cpp). injectBug = real degeneracy.
int runLinePointsSelfTest(bool injectBug);
// GridPoints generator golden (point_ops_gridpoints.cpp). injectBug = real degeneracy.
int runGridPointsSelfTest(bool injectBug);
// SpherePoints generator golden (point_ops_spherepoints.cpp). injectBug = real degeneracy.
int runSpherePointsSelfTest(bool injectBug);
// TransformPoints MODIFIER golden (point_ops_transformpoints.cpp): ring -> scale+translate, PLUS a
// multi-axis rotation tooth (a known point under Rot!=0 lands where the Y·X·Z order predicts).
// injectBug = Strength 0 -> identity passthrough -> ring unchanged. First modifier (in->out bag).
int runTransformPointsSelfTest(bool injectBug);
// refuter-T GPU adversarial probe (batch 17, point_ops_transformpoints.cpp): drives the REAL
// transformpoints kernel directly over a hand-built bag (non-identity Position AND Rotation) with
// MULTI-AXIS non-equal Rotation + non-uniform Scale, captures GPU Position+Rotation, compares each
// against the TiXL host TransformMatrix path (Scale*CreateFromQuaternion(CreateFromYawPitchRoll(
// yaw=Y,pitch=X,roll=Z))*Translation, render/_/TransformMatrix.cs) recomputed in C++. Gates PointSpace
// AND ObjectSpace at Pivot=0; reports Pivot!=0 as a non-gated diagnostic. injectBug=true uses the old
// Z·Y·X reference -> mismatches the fixed Y·X·Z shader -> FAIL (bite). Permanent bite tooth.
int runTransformPointsParityProbe(bool injectBug);
// OrientPoints MODIFIER golden (point_ops_orientpoints.cpp). injectBug = real degeneracy.
int runOrientPointsSelfTest(bool injectBug);
// RandomizePoints MODIFIER golden (point_ops_randomizepoints.cpp). injectBug = real degeneracy.
int runRandomizePointsSelfTest(bool injectBug);
// RandomizePoints rotation-ORDER regression lock (point_ops_randomizepoints.cpp, batch 17): drives
// the REAL randomizepoints kernel with identity origin points, recovers each point's biasedA.xyz
// from the (raw) position offset, then replays the INCREMENTAL X→Y→Z qMul(rot,axis) composition
// (RandomizePoints.hlsl:124-128, byte-identical to our shader) in C++ and compares to the captured
// Rotation. Pins the incremental shape: a reorder or collapse-to-combined turns it RED. injectBug
// replays a COMBINED single quaternion -> mismatches the faithful shader -> FAIL. Permanent tooth.
int runRandomizePointsRotationLock(bool injectBug);
// SetPointAttributes MODIFIER golden (point_ops_setpointattributes.cpp). injectBug = real degeneracy.
int runSetPointAttributesSelfTest(bool injectBug);
// CombineBuffers COMBINE golden (point_ops_combinebuffers.cpp): concat N bags, count = sum.
// injectBug = drop one input -> count != sum. First combine op (multi-input -> one bag).
int runCombineBuffersSelfTest(bool injectBug);
// AddNoise MODIFIER golden (point_ops_addnoise.cpp): simplex noise displaces sphere points.
// injectBug = Strength=0 -> identity passthrough -> no displacement -> FAIL.
int runAddNoiseSelfTest(bool injectBug);
// FilterPoints op golden (point_ops_filterpoints.cpp): re-samples input bag to Count points.
// injectBug = flips sphere-membership predicate sense -> FAIL.
int runFilterPointsSelfTest(bool injectBug);
// PolarTransformPoints MODIFIER golden (point_ops_polartransformpoints.cpp): TRS pre-transform +
// cartesian->cylindrical warp maps a line to a circle of radius R in XZ.
// injectBug = Translation.z=0 -> warp collapses every point to the origin (radius 0) -> FAIL.
int runPolarTransformPointsSelfTest(bool injectBug);
// refuter-P GPU adversarial probe (batch 16, point_ops_polartransformpoints.cpp): runs the REAL
// polartransformpoints kernel with Rotation!=0 + non-uniform Scale + Translation!=0, captures GPU
// positions, and compares each against the TiXL host TransformMatrix path (Scale *
// CreateFromQuaternion(CreateFromYawPitchRoll(yaw=Y,pitch=X,roll=Z)) * Translation) recomputed in
// C++. Prints maxPosErr. injectBug=true swaps yaw/roll in the C++ reference to simulate the old
// Z·Y·X bug, forcing a mismatch against the correct Y·X·Z shader -> FAIL (bite tooth).
// Permanent bite tooth (refuter-P verified, batch 16).
int runPolarTransformPointsParityProbe(bool injectBug);
// WrapPoints MODIFIER golden (point_ops_wrappoints.cpp): floored-mod box-wrap of positions.
// injectBug = box larger than the input line -> no point wraps inside the unit box -> FAIL.
int runWrapPointsSelfTest(bool injectBug);
// BoundPoints MODIFIER golden (point_ops_boundpoints.cpp): clamp positions into an AABB.
// injectBug = box larger than the input line -> no clamping -> points stay outside -> FAIL.
int runBoundPointsSelfTest(bool injectBug);
// TransformSomePoints MODIFIER golden (point_ops_transformsomepoints.cpp): TRS transform weighted
// by point W channel (selection). Three teeth: (1) graph golden ring shift, (2) multi-axis rotation
// order lock (Rot=37/53/71°, catches Z·Y·X regression), (3) WIsWeight lerp (W=0.5 -> movement halved).
// injectBug = Strength=0 -> identity passthrough -> all three assertions FAIL.
int runTransformSomePointsSelfTest(bool injectBug);
// refuter-XfSome GPU adversarial probe (batch 18, point_ops_transformsomepoints.cpp): drives the REAL
// transformsomepoints kernel with Rotation=(37,53,71)° + non-uniform Scale + Translation + non-identity
// per-point orgRot, captures GPU Position+Rotation, compares against the TiXL C++ reference path
// (Scale*CreateFromQuaternion(CreateFromYawPitchRoll(yaw=Y,pitch=X,roll=Z))*Translation). Gates
// PointSpace AND ObjectSpace; also gates WIsWeight=true with W=0.5 (lerp sub-probe).
// injectBug=true uses the old Z·Y·X reference -> mismatches the Y·X·Z shader -> FAIL (bite).
int runTransformSomePointsParityProbe(bool injectBug);

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

// --- Tint image filter texture op (point_ops_tint.cpp, lane F3-1) ---
// Single-pass color tint/remap (TiXL image/color/Tint). Reads upstream Texture2D, remaps
// luminance via ChannelWeights->GainAndBias->lerp(MapBlackTo,MapWhiteTo), blends by Amount.
// Register into the texture stream (texReg).
void registerTintOp();
// MATH golden: solid grey -> red ramp tint (Amount=1, MapWhite=red); center pixel R>64 & G<96.
// injectBug Amount=0 (passthrough) -> grey out -> FAIL.
int runTintSelfTest(bool injectBug);
// CHAIN golden: RadialPoints->DrawPoints->RenderTarget->Tint chain through cook (flat+resident);
// assert Tint is the terminal and the texture is non-black. injectBug drops RT->Tint wire -> FAIL.
int runTintChainSelfTest(bool injectBug);

// --- ChromaticAbberation image filter texture op (point_ops_chromab.cpp, lane F3-2) ---
// Single-pass radial chromatic fringe (TiXL image/fx/stylize/ChromaticAbberation). Splits R/B
// in opposite radial directions with barrel distortion. Register into the texture stream (texReg).
void registerChromaBAOp();
// MATH golden: white center stripe; R and B channels differ between left-of-center and
// right-of-center pixels inside the stripe (radial fringe asymmetry). injectBug Size=0 -> FAIL.
int runChromaBAShiftSelfTest(bool injectBug);

// --- AdjustColors image filter texture op (point_ops_adjustcolors.cpp, lane F3-3) ---
// Single-pass comprehensive color grading (TiXL image/color/AdjustColors): HSB ops, vignette,
// colorize, contrast S-curve, brightness, background composite.
// Register into the texture stream (texReg).
void registerAdjustColorsOp();
// MATH golden: solid red -> Saturation=0 -> greyscale (R≈G≈B within 30). injectBug Sat=1 ->
// red stays red (R>>G) -> equality FAILS.
int runAdjustColorsSelfTest(bool injectBug);

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
