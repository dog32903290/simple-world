// runtime/point_graph_selftests — the RED→GREEN selftest/golden DECLARATIONS for the point-cook
// subsystem, split out of point_graph.h to keep that header under the ≤400 / ratchet line law (it had
// grown to the 608-line cap purely from these declarations). PURE behavior-preserving extraction: every
// `int run*SelfTest(bool)` below was moved here VERBATIM from point_graph.h (no signature, comment, or
// ordering change). point_graph.h #includes this file, so every existing consumer (selftests_decls.h,
// the op .cpp goldens) sees the identical declarations through the same include path.
//
// ZONE: runtime (pure-computation leaf). Same deps as point_graph.h (none beyond it). Adding a new
// point-op golden = add its declaration here next to its siblings; the cap pressure now lives on this
// leaf, not on the core header.
#pragma once

namespace sw {

// Headless RED→GREEN proof of the COOK MACHINERY (not any real kernel): registers
// CPU-fill stub ops under real type names, builds RadialPoints→ParticleSystem→
// DrawPoints, cooks, and asserts the generated bag threaded through the middle op to
// the draw. injectBug makes the middle op ignore its input so the assertion FAILS.
int runPointGraphSelfTest(bool injectBug);

// Headless RED→GREEN proof that cookResident (resident-graph walk) yields the SAME point bag as
// cook (flat-graph walk) for an equivalent graph. injectBug makes the resident walk drop a driver
// so the bags diverge. (resident_cook_selftest.cpp)
int runResidentCookSelfTest(bool injectBug);

// Golden for the ensureState grow rule (state_count_selftest.cpp, refuter-2b promoted repro):
// a stateful op's persistent state must be re-created when the node's count grows past the
// capacity it was born with (else: GPU OOB write over the undersized state buffer), on BOTH
// cook paths. injectBug = the op's stateNew under-allocates -> the overrun detector fires.
int runStateCountSelfTest(bool injectBug);

// Headless RED→GREEN proof of S2 bypass on the GPU cook flows (bypass_cook_selftest.cpp, 修B):
// a bypassed node's MAIN output passes its MAIN input's upstream value through on the Points
// (buffer), Command (chain) and Texture2D (terminal) flows of cookResident — the executor half
// of the honest whitelist (compoundBypassableType). injectBug emulates a cook that ignores the
// flag so the Points passthrough assertion FAILS (teeth).
int runBypassCookSelfTest(bool injectBug);

// Headless RED→GREEN proof of slice-2b parity (resident_cook_parity_selftest.cpp): for each of
// (a) driver-resolved params (stored/override AND wire-driven), (b) stateful op state persisting
// across cooks per path, (c) force params resolved via the WIRED input (not by-type), (d) the
// tex terminal (RenderTarget executor + displayTex), (e) the preview terminal (synthesized
// 1-item chain) — resident cook == flat cook == hand-computed. injectBug makes the stateful stub
// ignore its persistent state -> the across-cooks assertion FAILS (teeth).
int runResidentCookParitySelfTest(bool injectBug);

// Headless RED→GREEN proof that the Cut 50 compute-shader cook seam works in the RESIDENT
// (production) cook path (resident_crop_selftest.cpp): cook a TexSource->Crop resident graph
// through cookResident and assert Crop's displayTex output is SIZED via imageFilterSizeFns() from
// the cooked input (input - margins, not the Resolution pin) AND fully written via its RWTexture2D
// (proving imageFilterComputeTypes()/needsWrite gave the output ShaderWrite). The pre-seam resident
// cook (ensureTex with no needsWrite/sizeFn) FAILS both. injectBug paints the marker off the kept
// rect so the shift probe fails (teeth on the readback).
int runResidentCropSelfTest(bool injectBug);

// FastBlur resident golden (resident_fastblur_selftest.cpp): the FIRST MULTI-PASS COMPUTE leaf driven
// through the RESIDENT (production) cook path. RenderTarget paints a white square on black ->
// FastBlur terminal; cookResident -> cookTexNode -> leaf N down + N up dispatches over per-level
// scratch (cachedScratchTex shaderWrite seam) -> displayTex. Asserts energy conservation (total ~=
// the square's input energy, DC gain 1) + edge softening + full ShaderWrite coverage. injectBug
// paints a SOLID field (no edge) so the edge-softened + square-energy-band probes can't fire -> RED.
int runResidentFastBlurSelfTest(bool injectBug);

// RgbTV resident golden (resident_rgbtv_selftest.cpp): the (E)-seam phase-2 leaf driven through the
// RESIDENT (production) cook path. RenderTarget paints a uniform gray field -> RgbTV terminal;
// cookResident -> cookTexNode (resolves the registered asset key, calls the registered decoder, binds
// the noise texture @t1) -> leaf generates input mips + dispatches the CRT kernel -> displayTex.
// Golden region: GlitchAmount override 0 (kills both noise sources -> deterministic), pins the
// center-row pixels to the flat-cooked GREEN reference + asserts the asset-decode seam fired.
// injectBug drops the RGB stripe (PatternAmount 0) so the pinned pattern pixels diverge -> RED.
int runResidentRgbTvSelfTest(bool injectBug);

// DistortAndShade resident golden (resident_distortandshade_selftest.cpp): the multi-image seam's
// second resident consumer (Displace was first). Two RenderTarget sources (ramp + uniform) -> the op's
// two Texture2D inputs via cookResident -> cookTexNode (recurses BOTH inputs into inputTextures[0/1]) ->
// leaf samples ImageA at the ImageB-driven uv2 -> displayTex. Golden: hand-derived ramp pins on the
// center row. injectBug OMITS the ImageB wire so the multi-image gather loses its 2nd input (ramp
// self-displaces) -> the pins diverge -> RED.
int runResidentDistortAndShadeSelfTest(bool injectBug);

// Combine3Images golden (point_ops_combine3images.cpp): the multi-image seam's FIRST 3-input consumer
// (Displace/DistortAndShade = 2 inputs). Three FLAT solids with distinct channel values -> the op packs
// out.R<-ImageA.r / out.G<-ImageB.g / out.B<-ImageC.b / out.A<-1 (closed-form d=0 plateau). injectBug
// drops ImageC so out.B reads ImageA.b (the fork) -> the B pin diverges -> RED (exercises the 3rd
// Texture2D port). The resident variant drives the same pack through cookResident -> cookTexNode (all
// three inputs into inputTextures[0/1/2]) -> displayTex; injectBug OMITS the ImageC wire.
int runCombine3ImagesSelfTest(bool injectBug);
int runResidentCombine3ImagesSelfTest(bool injectBug);

// PickTexture golden (point_ops_picktexture.cpp + resident_picktexture_selftest.cpp): the FIRST op with
// a variable-N MultiInputSlot<Texture2D> port. The flat golden drives the leaf with 3 inputs (Index=2 ->
// inputTextures[2]); the resident variant DRIVES the variable-N gather (3 wires into ONE multiInput Input
// port -> primary + 2 extraConns -> inputTextures[0..2]). injectBug OMITS the 3rd wire -> Index(2) mod 2
// = 0 -> input[0] -> RED. Proves cookTexNode's MultiInput Texture2D branch threads ALL N wires.
int runResidentPickTextureSelfTest(bool injectBug);

// FirstValidTexture golden (point_ops_firstvalidtexture.cpp + resident_firstvalidtexture_selftest.cpp):
// a SECOND consumer of the variable-N MultiInputSlot<Texture2D> gather (proven by PickTexture). TiXL
// rule = forward the FIRST NON-NULL gathered input. The flat golden nulls slot 0 -> selects input[1];
// the resident variant wires 2 sources into the ONE multiInput Input port (primary + extraConn) and
// asserts the first wire's color. injectBug OMITS the 1st wire -> first non-null shifts to input[1] -> RED.
int runResidentFirstValidTextureSelfTest(bool injectBug);

// UseFallbackTexture golden (point_ops_usefallbacktexture.cpp + resident_usefallbacktexture_selftest.cpp):
// two FIXED Texture2D ports (TextureA slot 0, Fallback slot 1). TiXL rule = TextureA ?? Fallback. The
// flat golden asserts TextureA wins over a present Fallback; the resident variant routes both ports
// through cookResident. injectBug OMITS the TextureA wire -> slot 0 null -> forwards Fallback -> RED.
int runResidentUseFallbackTextureSelfTest(bool injectBug);

// CombineMaterialChannels2 golden (point_ops_combinematerialchannels2.cpp): the PBR twin of
// Combine3Images — SAME kernel (img-combine-3.hlsl), SAME 3-image gather. A PBR-flavored solid set
// (roughness/metallic/ao) packs to (A.r, B.g, C.b, 1); injectBug drops ImageC -> B reads ImageA.b -> RED.
// The resident variant drives it through cookResident with the ImageC wire omitted on injectBug.
int runCombineMaterialChannels2SelfTest(bool injectBug);
int runResidentCombineMaterialChannels2SelfTest(bool injectBug);

// CombineMaterialChannels golden (point_ops_combinematerialchannels.cpp): the FIXED-port PBR packer
// (own shader CombineMaterialChannels.hlsl + roughness remap Curve). The flat golden injects a
// non-identity curve to prove the remap; the resident variant drives the 3-texture gather + the LUT
// through cookResident with the embedded DEFAULT identity curve (no Curve producer yet) -> remap is a
// passthrough, injectBug OMITS the Occlusion wire -> out.B = 1.0 (255) -> RED.
int runResidentCombineMaterialChannelsSelfTest(bool injectBug);

// HSE / MosiacTiling goldens (Fx-modulation 2-input image leaves on the built multi-input gather):
// Image(t0) + FxTexture/FxImage(t1); the resident variants drive both inputs through cookResident,
// injectBug drops the 2nd input -> golden diverges -> RED (proves the FxTexture .g is load-bearing).
int runResidentHseSelfTest(bool injectBug);
int runResidentMosiacTilingSelfTest(bool injectBug);

// Blur image-filter golden (point_ops_blur.cpp, lane I): the FIRST image filter (Texture2D in ->
// Texture2D out). (a) BLUR MATH: fill a source texture with a hard 1px-wide vertical white line on
// black, run Blur, assert the line SPREADS horizontally (neighbouring columns lit) — a no-op /
// passthrough leaves them black. (b) GATHER DIRECT-THROUGH: build RadialPoints->DrawPoints->
// RenderTarget->Blur through PointGraph::cook and assert the terminal texture is non-empty (the
// RenderTarget's Texture2D output really reached the Blur input). injectBug makes the blur write
// the center tap only (Size 0) so the spread assertion FAILS (teeth).
int runBlurSelfTest(bool injectBug);
int runBlurChainSelfTest(bool injectBug);

// Displace image-filter golden (point_ops_displace.cpp, lane D2): the SECOND image filter and the
// FIRST op with TWO Texture2D inputs (Image + DisplaceMap). (a) DISPLACE MATH: Image = a vertical
// edge, DisplaceMap = a horizontal ramp; with Displacement!=0 the edge MOVES vs a no-warp baseline
// (passthrough leaves it put). (b) MULTI-INPUT GATHER: build two RenderTarget legs feeding Displace's
// Image + DisplaceMap and cook through PointGraph::cook (flat + resident); assert the terminal texture
// is sized + non-empty (both RenderTargets threaded into Displace's two inputs). injectBug zeroes
// Displacement (math) / drops the Image wire (chain) so the assertion FAILS (teeth).
int runDisplaceSelfTest(bool injectBug);
int runDisplaceChainSelfTest(bool injectBug);

// Blend / BlendWithMask image-filter goldens (point_ops_blend.cpp / point_ops_blendwithmask.cpp, lane
// multi-image, image/use): the THIRD/FOURTH multi-image-seam consumers (Displace/DistortAndShade first).
// Blend composites ImageA+ImageB (two Texture2D inputs); BlendWithMask is the FIRST op with THREE
// Texture2D inputs (ImageA+ImageB+Mask). The *SelfTest fns (flat closed-form blend math on solid inputs)
// self-register via the imageFilterSelfTests() sink ("blend"/"blendwithmask"); the *ChainSelfTest fns
// (flat-chain + RESIDENT production golden, the multi-Texture2D gather承重線) are kTable rows below.
// injectBug drops the ImageB wire so the multi-image gather loses its 2nd input -> the mixed-color pins
// collapse -> RED. (runBlend*SelfTest declared here next to the goldens, same precedent as Displace.)
int runBlendSelfTest(bool injectBug);
int runBlendChainSelfTest(bool injectBug);
int runBlendWithMaskSelfTest(bool injectBug);
int runBlendWithMaskChainSelfTest(bool injectBug);

// Tint image-filter golden (point_ops_tint.cpp, lane F3-1): (a) TINT MATH: solid grey ->
// red-ramp tint (Amount=1, MapWhite=(1,0,0,1)); center R>64 & G<96. injectBug Amount=0 ->
// passthrough grey -> FAIL. (b) TINT CHAIN: RadialPoints->DrawPoints->RenderTarget->Tint through
// cook (flat + resident); Tint is terminal, texture non-black. injectBug drops RT->Tint wire -> FAIL.
int runTintSelfTest(bool injectBug);
int runTintChainSelfTest(bool injectBug);

// ChromaticAbberation image-filter golden (point_ops_chromab.cpp, lane F3-2): (a) SHIFT MATH:
// white center stripe; R and B channels inside the stripe become asymmetric (left vs right)
// due to the radial fringe offset. injectBug Size=0 (no fringe) -> symmetric -> FAIL.
int runChromaBAShiftSelfTest(bool injectBug);

// AdjustColors image-filter golden (point_ops_adjustcolors.cpp, lane F3-3): (a) HSB MATH:
// solid red input; Saturation=0 -> greyscale (R≈G≈B within 30, all >60). injectBug Sat=1 ->
// red stays red (R>>G) -> FAIL.
int runAdjustColorsSelfTest(bool injectBug);

// SamplePointColorAttributes golden (point_ops_samplepointcolorattributes.cpp): the FIRST Points op
// with a Texture2D INPUT — the proving op for the texture-into-points seam (PointCookCtx::inputTextures).
// A point's Color is BlendColors(p.Color, sample*BaseColor, Mode). Closed-form (BOTH cook paths, R-2):
// input bag Color=(0,0,0,0), a UNIFORM texture=(1,0,0,1), BaseColor=(1,1,1,1), Mode=0 Normal ->
// sample c=(1,0,0,1) at EVERY uv (uniform texture is coordinate-independent => identity-transform fork
// holds) -> BlendColors Normal: a=1, rgb=(1,0,0) -> every point Color=(1,0,0,1). The FLAT leg reads the
// cooked Points buffer back byte-for-byte (debugCookedBuffer); the RESIDENT leg drives the seam through
// cookResident -> cookNode's Texture2D gather -> the op samples -> DrawPoints->RenderTarget and reads the
// rendered RED pixels (production path; no resident Points-buffer accessor). injectBug: DROP the texture
// bind (inputTextureCount=0 / OMIT the wire) -> sample=(0,0,0,0) -> Color stays (0,0,0,0); want FIXED at
// (1,0,0,1) -> RED.
int runSamplePointColorAttributesSelfTest(bool injectBug);

// SamplePointAttributes_v1 (point_ops_samplepointattributes.cpp): texture-into-points seam consumer that
// samples the Texture2D per point (via transformSampleSpace) and ROUTES L/R/G/B channels (each via an
// Attributes routing enum + Factor/Offset) into Position xyz / W(FX1) / Rotation / Stretch(Scale). Golden
// (2 analytic legs over a UNIFORM ~0.5 texture): POS-route (L=For_X,Factor=1 -> newPos.x = pos.x+gray) +
// W-route (Blue=For_W,Factor=2 -> FX1 = FX1+c.b*2). injectBug drops the texture bind -> passthrough -> RED.
int runSamplePointAttributesSelfTest(bool injectBug);

// DisplacePoints2d (point_ops_displacepoints2d.cpp): texture-into-points seam consumer. Samples a
// DisplaceMap, takes the central-difference GRADIENT of the gray map at ±SampleRadius, and displaces each
// point by direction*DisplaceAmount/100 along the gradient angle (atan2(d.x,d.y)+Twist). WorldToObject =
// inverse of the op-local TRS (Center/TextureRotate/TextureScale) — NO camera (fork-worldtoobject-op-local).
// Golden: a CONSTANT-GRADIENT ramp (gray = u) -> the X-gradient is constant, Y-gradient 0 -> every point
// displaces along a KNOWN direction by a KNOWN magnitude. injectBug drops the texture -> no displace -> RED.
int runDisplacePoints2dSelfTest(bool injectBug);

// TransformWithImage (point_ops_transformwithimage.cpp): texture-into-points seam consumer (TiXL op for
// TranslateWithImage.hlsl). Samples an Image, derives a per-point strength = Strength·(gray+StrengthOffset)·
// (StrengthFactor channel), then applies a host-composed TRS TransformMatrix (Translate/Scale/ScaleUniform/
// Rotate) lerp-blended by strength (transformSampleSpace for the uv; NO camera). Golden: a UNIFORM image
// (gray=0.5) + a pure-Translate TransformMatrix -> every point moves by Translate·strength (analytic).
// injectBug drops the texture -> gray=0 -> strength collapses -> no move -> RED.
int runTransformWithImageSelfTest(bool injectBug);

// AttributesFromImageChannels — texture-into-points seam consumer that ROUTES sampled channels into
// point attributes (position/F1/F2/rotate/scale) via per-channel Factor/Offset gains. Golden: ROUTING
// direct-cook leg (R->Position_X / G->Position_Y with non-identity gains -> want pos=(0.50,0.80,0)) +
// FLAT-DRIVER gather leg (PointGraph::cook + debugCookedBuffer, Red->Scale_Uniform) + RESIDENT leg
// (cookResident, grown sprites). injectBug drops the texture bind -> passthrough -> RED.
int runAttributesFromImageChannelsSelfTest(bool injectBug);

// LinearSamplePointAttributes — texture-into-points seam consumer that samples the texture along the
// point INDEX (uv = (i/pointCount, 0.5) — a 1D LINEAR strip, NO position-derived uv) and ROUTES the
// sampled channels into point attributes (position/F1/rotate/stretch/F2) via per-channel Factor/Offset
// gains. Golden (2 legs, R-2): FLAT direct-cook leg (uniform red, Red->For_X, RedFactor=1 -> every
// point's X shifts +1, closed-form byte-read) + RESIDENT leg (cookResident, the lit ring centroid
// shifts RIGHT vs the no-texture baseline). injectBug drops the texture bind -> passthrough -> RED.
int runLinearSamplePointAttributesSelfTest(bool injectBug);

// MapPointAttributes — the bake-into-point seam consumer (PointCookCtx::inputCurves/inputGradients). A
// count-preserving MODIFIER that BAKES its host Curve (→ R32_Float CurveImage) + Gradient (→ RGBA32
// GradientImage, .t3 resolution 512) into two scratch textures during cook, then per point samples both
// at (f,0.5) — f = the InputMode→MappingMode-remapped coordinate — to write a curve value into FX1/FX2/
// Scale (WriteTo) + a gradient color into Color (WriteColor, default Multiply). Faithful to
// external/tixl .../point/modify/MapPointAttributes.{cs,hlsl,t3} (the .t3 compound bakes the host inputs
// via CurvesToTexture/GradientsToTexture + FirstValidTexture, not straight wires — read the .hlsl cbuffer
// directly). Golden (4 legs, R-2): a hand-built ctx injects a known 2-key curve + 2-step gradient and
// byte-reads FX1 (== curve.r at f) AND Color (== gradient at f, multiplied) — hand-computed; one leg
// through PointGraph::cook (flat) + one through cookResident (production, embedded defaults baked).
// injectBug omits the gradient scratch bind → Color = white passthrough (≠ the injected gradient color);
// want FIXED at the true values.
int runMapPointAttributesSelfTest(bool injectBug);
// SetAttributesWithPointFields golden (point_ops_setattributeswithpointfields.cpp): 2nd-Points (inputs[1]
// = FieldPoints) + bake-into-point seam; injectBug severs FieldPoints → passthrough → pos/color RED.
int runSetAttributesWithPointFieldsSelfTest(bool injectBug);

// TransformPointsFromClipspace golden (point_ops_transformpointsfromclipspace.cpp): the FIRST Points op
// to consume the camera-matrix-into-points seam (PointCookCtx::cameraToWorld). A count-preserving
// MODIFIER: unproject each point through CameraToWorld (mul(float4(pos,1),CameraToWorld) /w) → Position,
// and post-multiply its Rotation by qFromMatrix3Precise(transpose(CameraToWorld 3×3)). v1 fork: default
// camera + identity ObjectToWorld (CameraToWorld = inverse(WorldToCamera)). Golden (4 legs, R-2): direct-
// cook closed-form (host mat4TransformPointDivW == GPU Position) + flat-driver (PointGraph::cook +
// debugCookedBuffer) + resident (cookResident production) + a Rotation leg. injectBug binds an IDENTITY
// CameraToWorld → passthrough → diverges from the unproject expectation → RED.
int runTransformPointsFromClipspaceSelfTest(bool injectBug);

// SamplePointsByCameraDistance golden (point_ops_samplepointsbycameradistance.cpp): the SECOND camera-
// matrix-into-points seam consumer (PointCookCtx::objectToCamera) AND a rider of the bake-into-point seam
// (PointCookCtx::inputCurves — the WForDistance Curve baked to a 256×1 R32 scratch, .t3 default linear
// 0→1). A count-preserving MODIFIER: d = mul(float4(pos,1),ObjectToCamera).z; normalized = (-d-NearRange)/
// (FarRange-NearRange); p.W (== SwPoint.FX2) *= curve.Sample(normalized). Golden (4 legs, R-2): direct-
// cook closed-form (default camera → pos=(0,0,0) camera-space z = -DefaultCameraDistance → normalized →
// linear curve value → W scaled) + a second-position leg + flat-driver + resident. injectBug binds an
// IDENTITY ObjectToCamera → d=0 for every point → all W equal → the depth-spread assertion FAILS → RED.
int runSamplePointsByCameraDistanceSelfTest(bool injectBug);

// SortPoints golden (point_ops_sortpoints.cpp): the THIRD camera-matrix-into-points seam consumer
// (PointCookCtx::cameraToWorld). A count-preserving REORDER: sort the Points bag by each point's distance
// to the camera WORLD position (CameraToWorld[3].xyz). v1 fork-sortpoints-converged-not-incremental: a
// single-cook full STABLE sort == the converged endpoint of TiXL's frame-incremental bitonic network
// (SortingSpeed read-but-ignored; the persistent IndexBuffer is a feedback seam SW lacks). Key (1:1 .hlsl
// c2k): k=length(pos - camWorldPos); isnan(Scale.x)→-1 (sinks last); Ascending flips the sign; result is
// sorted DESCENDING by k → Ascending=false = farthest-first (painter's), Ascending=true = nearest-first.
// Golden (3 legs, R-2): direct-cook descending order (host camera-distance closed-form) + Ascending flip
// with a NaN-scale sink + resident (cookResident production, lit+count-preserved). injectBug binds an
// IDENTITY CameraToWorld → camera at origin → distances become |z| → the z=0 point becomes nearest not
// middle → the order changes → RED.
int runSortPointsSelfTest(bool injectBug);

}  // namespace sw
