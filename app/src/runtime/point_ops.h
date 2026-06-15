// runtime/point_ops — the built-in point operators (cook fns) for the lane-A point graph.
// Each is a faithful port of its TiXL counterpart (external/tixl .../point/**): crawl the
// .cs/.hlsl/.help, port the kernel + a cook fn, prove it with a golden selftest against
// TiXL's own formula. They register into point_graph's table via registerBuiltinPointOps()
// (declared in point_graph.h) — called once at app startup and by each cook selftest.
// Adding an operator = one cook fn + one registerPointOp line + its .metal + its golden.
#pragma once
#include "runtime/point_graph.h"  // RenderResolution (RenderTarget op contract) + ctx structs
namespace sw {
struct Node;

// --- Per-family point-op registrars (point_ops_register_<family>.cpp) -------------------
// registerBuiltinPointOps() (point_ops.cpp) calls these in family order. Each registrar owns
// one family's registration lines; adding an op to a family edits only its registrar, never
// the central builder. Family split mirrors node_registry_<family>.cpp (avoids the shared
// collision点 so family lanes produce ops in parallel without merge conflicts).
void registerGeneratorPointOps();     // RadialPoints, LinePoints, GridPoints, SpherePoints, HexGridPoints
void registerPointModifyPointOps();   // TransformPoints, OrientPoints, RandomizePoints, SetPointAttributes,
                                      // AddNoise, FilterPoints, PolarTransform, Wrap, Bound,
                                      // TransformSomePoints, WrapPointPosition, SnapToGrid
void registerPointCombinePointOps();  // CombineBuffers
void registerParticlePointOps();      // ParticleSystem
void registerDrawPointOps();          // DrawPoints, DrawLines, DrawBillboards, RenderTarget
// (Image-filter ops self-register via ImageFilterOp registrars in their leaf .cpp — no family
//  registrar, no registerImageFilterPointOps(). See image_filter_op_registry.h.)

// Inline cook fns defined in point_ops.cpp (the ops whose kernels live in the central file:
// RadialPoints/ParticleSystem/DrawPoints). Declared here so their family registrars can wire
// them. simStateNew/simStateFree are ParticleSystem's per-node state lifecycle hooks.
void cookRadialPoints(PointCookCtx& c);
void cookParticleSim(PointCookCtx& c);
RenderCommand cookDrawPoints(CmdCookCtx& c);
void* simStateNew(MTL::Device* dev, MTL::Library* lib, uint32_t count);
void simStateFree(void* p);
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
// VelocityForce golden (批次24): the op rescales each particle's SPEED along its existing
// direction; Accelerate>0 + Amount>0 -> the pool accelerates outward (RMS radius > baseline).
// injectBug (Amount=0) -> no acceleration -> RMS matches baseline -> FAIL.
int runVelocityForceSelfTest(bool injectBug);
// AxisStepForce golden (批次24): SelectRatio=1 + Strength large + ApplyTrigger=1 kicks every
// particle along a random axis -> RMS radius > baseline. injectBug (ApplyTrigger=0) -> no kick.
int runAxisStepForceSelfTest(bool injectBug);
// SnapToAnglesForce golden (批次24): AngleCount=360 (one allowed angle) + Amount=1 snaps every
// planar velocity to the same direction -> strong collinear drift (|mean xy|>0.1). injectBug
// (Amount=0) -> no snap -> isotropic -> mean ~0 -> FAIL. Mode=1 (WorldXY, camera-free path).
int runSnapToAnglesForceSelfTest(bool injectBug);
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
// RepetitionPoints generator golden (point_ops_repetitionpoints.cpp). injectBug = u=i not i+1.
int runRepetitionPointsSelfTest(bool injectBug);
// CommonPointSets GENERATOR golden (point_ops_commonpointsets.cpp, batch 37): CPU-fill fork — a
// Set enum picks one of 7 hard-coded vertex tables (Cross/CrossXY/Cube/Quad/ArrowX/ArrowY/ArrowZ).
// injectBug = assert the WRONG Cross row-0 coordinate -> faithful fill mismatches -> FAIL.
void registerCommonPointSetsOp();
int runCommonPointSetsSelfTest(bool injectBug);
// BoundingBoxPoints GENERATOR golden (point_ops_boundingboxpoints.cpp, batch 38): CPU-readback fork
// of external/tixl .../point/generate/BoundingBoxPoints. Reads a Points input bag, computes its AABB
// (skipping NaN-position points per .hlsl:61), and emits ONE point: Position=center=(min+max)/2,
// Scale=box size=max-min (TiXL Stretch@48==Scale@48), FX2=1 (Selected@60==FX2@60), FX1=1 (W@12).
// injectBug = assert the WRONG center law (center==max) -> faithful (min+max)/2 mismatches -> FAIL.
void registerBoundingBoxPointsOp();
int runBoundingBoxPointsSelfTest(bool injectBug);
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

// WrapPointPosition MODIFIER golden (point_ops_wrappointposition.cpp): cube-fold box wrap.
// injectBug = box size 20 (> input extent) -> no fold -> points stay outside unit box -> FAIL.
int runWrapPointPositionSelfTest(bool injectBug);
// SnapPointsToGrid MODIFIER golden (point_ops_snaptogrid.cpp): lerp points to grid centers.
// injectBug = Amount=0 (no snap) -> radial positions not integer -> FAIL.
int runSnapToGridSelfTest(bool injectBug);
// HexGridPoints GENERATOR golden (point_ops_hexgridpoints.cpp): hex tiling grid.
// injectBug = Size=0 -> all points collapse to center -> no distinct X values -> FAIL.
int runHexGridPointsSelfTest(bool injectBug);
// DoyleSpiralPoints2 GENERATOR golden (point_ops_doylespiralpoints.cpp): Doyle circle-packing
// spiral (CPU Newton-Raphson A/B/R -> GPU kernel). injectBug = ScaleBias(Bias2)=0 -> mag constant.
int runDoyleSpiralPointsSelfTest(bool injectBug);
// ClearSomePoints MODIFIER golden (point_ops_clearsomepoints.cpp): per-point hash kill.
// injectBug flips the Ratio=0 assertion to expect at least 1 kill -> correct shader FAILS -> RED.
int runClearSomePointsSelfTest(bool injectBug);
// ReorientLinePoints MODIFIER golden (point_ops_reorientlinepoints.cpp): align Rotation to the
// line tangent via qSlerp(Amount). injectBug flips the align test (expect forward AWAY) -> RED.
int runReorientLinePointsSelfTest(bool injectBug);
// SelectPoints MODIFIER golden (point_ops_selectpoints.cpp): volume (sphere/box/plane/zebra/noise)
// selection written into FX1/FX2. injectBug zeroes the inside-volume strength -> selection FAILS.
int runSelectPointsSelfTest(bool injectBug);
// SoftTransformPoints MODIFIER golden (point_ops_softtransformpoints.cpp): volume-falloff weighted
// soft transform of Position/Rotation/FX1. injectBug disables the falloff -> wrong weight -> FAIL.
int runSoftTransformPointsSelfTest(bool injectBug);
// OffsetPoints MODIFIER golden (point_ops_offsetpoints.cpp, batch 24): hand-built probe drives the
// REAL offsetpoints kernel over points with distinct non-identity Rotation; recomputes the TiXL
// .hlsl formula (Position + qRotateVec3(Direction*Distance, Rotation)) in C++ and compares per point.
// injectBug = expected reference DROPS the rotation (un-rotated offset) -> mismatches GPU -> FAIL.
void registerOffsetPointsOp();
int runOffsetPointsSelfTest(bool injectBug);
// PointAttributeFromNoise MODIFIER golden (point_ops_pointattributefromnoise.cpp, batch 24): route
// 3D simplex noise into position X/Y/Z; assert displacement off a clean sphere + per-point variation
// + determinism. injectBug = Amount=0 -> c*=0 -> no displacement -> FAIL. RemapNoise port not wired (FORK).
void registerPointAttributeFromNoiseOp();
int runPointAttributeFromNoiseSelfTest(bool injectBug);
// ResampleLinePoints MODIFIER golden (point_ops_resamplelinepoints.cpp, batch 36): count-changing
// arc-PARAMETER resample of a line into Count points (TiXL point/modify/ResampleLinePoints .hlsl).
// Separator (NaN-Scale) points break the line into segments. injectBug asserts the no-scaling
// resample law (x == source index, not sourceF*(SourceCount-1)) -> mismatch -> FAIL.
void registerResampleLinePointsOp();
int runResampleLinePointsSelfTest(bool injectBug);
// SubdivideLinePoints (point_ops_subdividelinepoints.cpp, lane point_modify): per-segment subdivide,
// InsertCount interpolated points inserted per segment (count-change = sourceCount*(InsertCount+1));
// ClosedShape adds a closing segment, separators carve closed segments (TiXL .../SubdivideLinePoints).
// injectBug = wrong subdivision law (sub-points sit on segment start, no f-interpolation) -> FAIL.
void registerSubdivideLinePointsOp();
int runSubdivideLinePointsSelfTest(bool injectBug);
// ChannelMixer image filter (point_ops_channelmixer.cpp, lane image_filter): 4x4 channel matrix
// mix (TiXL image/color/ChannelMixer / MixChannels.hlsl). injectBug = identity -> no swap -> FAIL.
int runChannelMixerSelfTest(bool injectBug);

// ToneMapping image filter (point_ops_tonemapping.cpp, lane image_filter): per-mode tone curve
// (TiXL image/color/ToneMapping / ToneMap.hlsl). Supports Aces/Reinhard/Filmic/Uncharted2/AgX/
// AgX_Punchy(unreachable TiXL bug)/None. injectBug = Mode=None (no compression) -> HDR output
// stays >0.95 -> FAIL. Green path = Reinhard compresses 4.0 HDR -> 0.8 -> PASS.
int runToneMappingSelfTest(bool injectBug);
// ConvertColors image filter (point_ops_convertcolors.cpp, lane image_filter): RGB<->OkLab /
// RGB<->LCh color-space converter (TiXL image/color/ConvertColors / img-fx-ConvertColors.hlsl +
// color-functions.hlsl). Mode enum (0 RgbToOKLab/1 OKLabToRgb/2 RgbToLCh/3 LChToRgb) dispatched by
// float thresholds. injectBug = flip the CPU-expected RgbToLCh matrix mul direction -> Test A
// (hand-computed forward LCh) FAILS. Test B = Mode0->Mode1 round-trip back to original.
int runConvertColorsSelfTest(bool injectBug);
// SnapToPoints COMBINE op (point_ops_snaptopoints.cpp, batch 21): index-paired lerp of Points1
// toward Points2 using distance-based smoothstep * MaxAmount. TiXL SnapToPoints.hlsl port.
// injectBug = MaxAmount=0 -> no snap -> Points1 positions unchanged -> near-P2 assertion FAILS.
void registerSnapToPointsOp();
int runSnapToPointsSelfTest(bool injectBug);
// PairPointsForLines COMBINE op (point_ops_pairpointsforlines.cpp, batch 24): pairs GPoints[i]
// with GTargets[i] (cyclic modulo), emits 3 output points per pair [A, B, NaN divider] for use
// with DrawLines. Output count = max(CountA, CountB) * 3. SetWTo01 sets FX1=0/1 on A/B.
// injectBug = asserts B slot has GPoints range (wrong) -> real shader writes GTargets -> FAIL.
void registerPairPointsForLinesOp();
int runPairPointsForLinesSelfTest(bool injectBug);
// PickPointList COMBINE op (point_ops_pickpointlist.cpp, batch 24): selects one buffer from a
// multi-input list by Index (modulo count). GPU blit of selected input into output buffer.
// Output count = selected input count. TiXL PickPointList.cs pure C# logic port.
// injectBug = asserts wrong input range (index 0 range) -> real cook selects index 1 -> FAIL.
void registerPickPointListOp();
int runPickPointListSelfTest(bool injectBug);

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
// (runBlurSelfTest / runBlurChainSelfTest are declared in point_graph.h next to the other goldens.)

// --- Displace image filter texture op (point_ops_displace.cpp, lane D2) ---
// The SECOND image filter and the FIRST op with TWO Texture2D inputs (Image + DisplaceMap): a TiXL
// image warp (image/fx/distort/Displace). Reads TexCookCtx::inputTextures[0..1] (the multi-input
// gather承重線). Register into the texture stream (texReg).
// (runDisplaceSelfTest / runDisplaceChainSelfTest are declared in point_graph.h next to the goldens.)

// --- Tint image filter texture op (point_ops_tint.cpp, lane F3-1) ---
// Single-pass color tint/remap (TiXL image/color/Tint). Reads upstream Texture2D, remaps
// luminance via ChannelWeights->GainAndBias->lerp(MapBlackTo,MapWhiteTo), blends by Amount.
// Register into the texture stream (texReg).
// MATH golden: solid grey -> red ramp tint (Amount=1, MapWhite=red); center pixel R>64 & G<96.
// injectBug Amount=0 (passthrough) -> grey out -> FAIL.
int runTintSelfTest(bool injectBug);
// CHAIN golden: RadialPoints->DrawPoints->RenderTarget->Tint chain through cook (flat+resident);
// assert Tint is the terminal and the texture is non-black. injectBug drops RT->Tint wire -> FAIL.
int runTintChainSelfTest(bool injectBug);

// --- ChromaticAbberation image filter texture op (point_ops_chromab.cpp, lane F3-2) ---
// Single-pass radial chromatic fringe (TiXL image/fx/stylize/ChromaticAbberation). Splits R/B
// in opposite radial directions with barrel distortion. Register into the texture stream (texReg).
// MATH golden: white center stripe; R and B channels differ between left-of-center and
// right-of-center pixels inside the stripe (radial fringe asymmetry). injectBug Size=0 -> FAIL.
int runChromaBAShiftSelfTest(bool injectBug);

// --- AdjustColors image filter texture op (point_ops_adjustcolors.cpp, lane F3-3) ---
// Single-pass comprehensive color grading (TiXL image/color/AdjustColors): HSB ops, vignette,
// colorize, contrast S-curve, brightness, background composite.
// Register into the texture stream (texReg).
// MATH golden: solid red -> Saturation=0 -> greyscale (R≈G≈B within 30). injectBug Sat=1 ->
// red stays red (R>>G) -> equality FAILS.
int runAdjustColorsSelfTest(bool injectBug);

// --- Pixelate image filter texture op (point_ops_pixelate.cpp, lane image_filter) ---
// Single-pass tile quantizer (TiXL image/fx/stylize/Pixelate). Snaps UV to a tile grid (Divisor>
// 0.5 -> floor(res/(Divisor*2)) tiles, else TileAmount), point-samples the tile center, multiplies
// by Color. FORK (named): TiXL's per-cell Shape texture omitted (default Shape=white = no-op).
// Register into the texture stream (texReg).
// MATH golden: high-frequency stripe source; with a coarse Divisor two same-tile pixels become
// EQUAL (block quantization) though they differ in the source; Color=(1,0,0,1) zeroes G/B.
// injectBug Divisor=0 -> sub-pixel TileAmount tiles -> no merge -> FAIL.
int runPixelateSelfTest(bool injectBug);

// --- Sharpen image filter texture op (point_ops_sharpen.cpp, lane image_filter) ---
// Single-pass 3x3 desaturated-Laplacian unsharp mask (TiXL image/fx/blur/Sharpen):
// final = col + col*Strength*(8*L(center) - 8 neighbour luminances), optional Clamping saturate.
// Register into the texture stream (texReg).
// MATH golden: vertical step edge; the bright-side edge pixel OVERSHOOTS above source (ringing)
// while a flat interior pixel stays unchanged (Laplacian=0). injectBug Strength=0 -> passthrough
// -> no overshoot -> FAIL.
int runSharpenSelfTest(bool injectBug);

// --- DetectEdges image filter texture op (point_ops_detectedges.cpp, lane image_filter) ---
// Single-pass 4-neighbour absolute-difference edge detector (TiXL image/fx/stylize/DetectEdges):
// average = sum_rgb(|x1-m|+|x2-m|+|y1-m|+|y2-m|) * Strength + Contrast, tinted by Color, lerp to
// original by MixOriginal. Register into the texture stream (texReg).
// MATH golden: white/black horizontal border; the border row is BRIGHT (edge magnitude) while a
// flat interior row stays DARK. injectBug Strength=0 -> edge magnitude 0 -> border not bright -> FAIL.
int runDetectEdgesSelfTest(bool injectBug);

// --- ChromaticDistortion image filter texture op (point_ops_chromaticdistortion.cpp, image_filter) ---
// Single-pass radial bulge + N-sample chromatic radial blur (TiXL image/fx/distort/Chromatic-
// Distortion): chromaShift() splits R/B from opposite ends of the radial sample line, lerp
// blurred<->chromarized by Colorize. Register into the texture stream (texReg).
// MATH golden: white/black vertical edge; with Colorize=1 the smeared border separates R from B
// while a deep interior stays grey. injectBug Colorize=0 -> pure blur -> R==B -> FAIL.
int runChromaticDistortionSelfTest(bool injectBug);

// --- VoronoiCells image filter texture op (point_ops_voronoicells.cpp, lane image_filter) ---
// Single-pass iq Voronoi cell mosaic with correct border distances (TiXL image/fx/stylize/
// VoronoiCells): input texture = feature-point + cell-colour field, edges tinted by EdgeColor.
// Register into the texture stream (texReg).
// MATH golden: gradient source -> red-edged white-cell mosaic; the frame contains many red edge
// pixels (R>150,G/B<80). injectBug Scale=1 -> single huge cell -> ~0 borders -> FAIL.
int runVoronoiCellsSelfTest(bool injectBug);

// --- Dither image filter texture op (point_ops_dither.cpp, lane image_filter) ---
// Single-pass Bayer/hash ordered-dither quantizer (TiXL image/fx/stylize/Dither): resample on a
// Scale/Offset grid, gain/bias to grayscale, Bayer64 threshold -> binary, lerp ShadowColor<->
// HighlightColor, optional BlendColors over the source. Register into the texture stream (texReg).
// MATH golden: bright-top/dark-bottom source; dither density tracks brightness so mean luminance
// (top) > (bottom). injectBug Scale=0 -> grid collapse -> source-independent -> means converge -> FAIL.
int runDitherSelfTest(bool injectBug);

// --- NormalMap image filter texture op (point_ops_normalmap.cpp, lane image_filter) ---
// Single-pass finite-difference gradient -> normal encoder (TiXL image/fx/NormalMap, no .cs):
// ±SampleRadius neighbour gradient d -> angle+Twist -> normalize((len*dir*Impact,1)) per Mode.
// Register into the texture stream (texReg).
// MATH golden: vertical step edge; the seam pixel's encoded R departs from 128 (X tilt) while a
// flat interior pixel stays R≈128. injectBug Impact=0 -> normal=(0,0,1) -> seam R≈128 -> FAIL.
int runNormalMapSelfTest(bool injectBug);

// --- ChromaKey image filter texture op (point_ops_chromakey.cpp, lane image_filter) ---
// Single-pass HSB-distance chroma keyer (TiXL image/fx/ChromaKey, no .cs): center+4 (±ChokeRadius)
// neighbours -> rgb2hsb -> weighted distance to KeyColor -> min (choke) -> composite per Mode.
// Register into the texture stream (texReg).
// MATH golden: green(key)/red(keep) split; Mode=0 alpha keying -> green alpha LOW, red alpha HIGH.
// injectBug Amplify=10 -> distance saturates to 0 everywhere -> red alpha collapses -> FAIL.
int runChromaKeySelfTest(bool injectBug);

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
