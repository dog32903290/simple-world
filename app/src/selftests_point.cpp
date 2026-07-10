// app/src/selftests_point.cpp — area manifest leaf for the --selftest router: point-op family (force/line/grid/transform/offset/sim/...)
//
// Shell-tier (app/src/ root, like selftests.cpp): may name selftest fns from any zone via
// selftests_decls.h. Self-registers its rows into selftestRegistry() during pre-main dynamic init;
// selftests.cpp reads that sink. Adding a selftest to this area = add ONE row below — selftests.cpp
// is never touched. ORDER_BASE is the global index of the first row (keeps --selftest-list identical
// to the pre-split kTable order; see selftest_registry.h). Rows kept verbatim from the old kTable.
#include "runtime/selftest_registry.h"
#include "selftests_decls.h"

namespace sw {
REGISTER_SELFTESTS(/*orderBase=*/116,
    {"radialop", runRadialOpSelfTest},
    {"radialcenter", runRadialCenterSelfTest},
    {"drawop", runDrawOpSelfTest},
    {"simop", runSimOpSelfTest},
    {"directionalforce", runDirectionalForceSelfTest},
    {"vectorfieldforce", runVectorFieldForceSelfTest},
    {"velocityforce", runVelocityForceSelfTest},
    {"axisstepforce", runAxisStepForceSelfTest},
    {"snaptoanglesforce", runSnapToAnglesForceSelfTest},
    {"forcekindoob", runForceKindOobSelfTest},
    {"linepoints", runLinePointsSelfTest},
    {"gridpoints", runGridPointsSelfTest},
    {"spherepoints", runSpherePointsSelfTest},
    {"repetitionpoints", runRepetitionPointsSelfTest},
    {"commonpointsets", runCommonPointSetsSelfTest},
    {"boundingboxpoints", runBoundingBoxPointsSelfTest},
    {"xfprobe", runTransformPointsParityProbe},
    {"orientpoints", runOrientPointsSelfTest},
    {"randomizepoints", runRandomizePointsSelfTest},
    {"rndrotlock", runRandomizePointsRotationLock},
    {"setpointattributes", runSetPointAttributesSelfTest},
    {"samplepointcolorattributes", runSamplePointColorAttributesSelfTest},
    {"samplepointattributes", runSamplePointAttributesSelfTest},
    {"displacepoints2d", runDisplacePoints2dSelfTest},
    {"transformwithimage", runTransformWithImageSelfTest},
    // (--selftest-meshverticestopoints RETIRED 2026-07-10, 廢棄節點退場 — the .t3 compound provides
    //  MeshVerticesToPoints now; kernel math in --selftest-mathv-meshverticestopoints, takeover in
    //  t3-meshverticestopoints-retire.)
    {"pointsonmesh", runPointsOnMeshSelfTest},
    {"findclosestpointsonmesh", runFindClosestPointsOnMeshSelfTest},
    {"attributesfromimagechannels", runAttributesFromImageChannelsSelfTest},
    {"linearsamplepointattributes", runLinearSamplePointAttributesSelfTest},
    {"mappointattributes", runMapPointAttributesSelfTest},
    {"setattributeswithpointfields", runSetAttributesWithPointFieldsSelfTest},
    {"transformpointsfromclipspace", runTransformPointsFromClipspaceSelfTest},
    {"samplepointsbycameradistance", runSamplePointsByCameraDistanceSelfTest},
    {"sortpoints", runSortPointsSelfTest},
    // (--selftest-combinebuffers RETIRED 2026-07-08, 廢棄節點退場 pilot #2 — the .t3 compound provides
    //  CombineBuffers now; parity lives in t3-combinebuffers, takeover in t3-combinebuffers-retire.)
    // (--selftest-addnoise RETIRED 2026-07-10, 廢棄節點退場 — the .t3 compound provides AddNoise now;
    //  kernel math in --selftest-mathv-addnoise, takeover in t3-addnoise-retire.)
    {"filterpoints", runFilterPointsSelfTest},
    {"polartransform", runPolarTransformPointsSelfTest},
    {"polarprobe", runPolarTransformPointsParityProbe},
    {"wrappoints", runWrapPointsSelfTest},
    {"boundpoints", runBoundPointsSelfTest},
    {"transformsomepoints", runTransformSomePointsSelfTest},
    {"xfsomeprobe", runTransformSomePointsParityProbe},
    // (--selftest-wrappointposition RETIRED 2026-07-10, 廢棄節點退場 — the .t3 compound provides
    //  WrapPointPosition now; kernel math in --selftest-mathv-wrappointposition, takeover in
    //  t3-wrappointposition-retire.)
    // (--selftest-snaptogrid RETIRED 2026-07-10, 廢棄節點退場 — the .t3 compound provides SnapPointsToGrid
    //  now; kernel math in --selftest-mathv-snappointstogrid, takeover in t3-snappointstogrid-retire.)
    {"hexgridpoints", runHexGridPointsSelfTest},
    {"doylespiral", runDoyleSpiralPointsSelfTest},
    // (--selftest-clearsomepoints RETIRED 2026-07-10, 廢棄節點退場 — the .t3 compound provides
    //  ClearSomePoints now; kernel math in --selftest-mathv-clearsomepoints, takeover in t3-clearsomepoints-retire.)
    {"reorientlinepoints", runReorientLinePointsSelfTest},
    {"resamplelinepoints", runResampleLinePointsSelfTest},
    {"subdividelinepoints", runSubdivideLinePointsSelfTest},
    {"selectpoints", runSelectPointsSelfTest},
    {"softtransformpoints", runSoftTransformPointsSelfTest},
    {"offsetpoints", runOffsetPointsSelfTest},
    {"pointattributefromnoise", runPointAttributeFromNoiseSelfTest},
    {"channelmixer", runChannelMixerSelfTest},
    {"tonemapping", runToneMappingSelfTest},
    // (--selftest-snaptopoints RETIRED 2026-07-10, 廢棄節點退場 — the .t3 compound provides SnapToPoints
    //  now; kernel math in --selftest-mathv-snaptopoints, takeover in t3-snaptopoints-retire.)
    {"pairpointsforlines", runPairPointsForLinesSelfTest},
    {"pickpointlist", runPickPointListSelfTest},
    {"pairpointsforsplines", runPairPointsForSplinesSelfTest},
    {"splinepoints", runSplinePointsSelfTest},
    {"pairpointsforgridwalklines", runPairPointsForGridWalkLinesSelfTest},
    // (--selftest-blendpoints RETIRED 2026-07-10, 廢棄節點退場 — the .t3 compound provides BlendPoints
    //  now; kernel math in --selftest-mathv-blendpoints, takeover in t3-blendpoints-retire.)
    {"multiupdatepoints", runMultiUpdatePointsSelfTest},
    {"repeatatpoints", runRepeatAtPointsSelfTest},
    {"repeatatpoints-prod", runRepeatAtPointsProductionSelfTest},
    {"simnoiseoffset", runSimNoiseOffsetSelfTest},
    {"simcentrialoffset", runSimCentricalOffsetSelfTest},
    {"simdirectionaloffset", runSimDirectionalOffsetSelfTest},
    {"simforceoffset", runSimForceOffsetSelfTest},
    {"rendertarget", runRenderTargetSelfTest},
    {"rendertargetwired", runRenderTargetWiredSelfTest},
    {"field-camera", runFieldCameraSelfTest},
    {"view-camera", runViewCameraSelfTest},  // output-camera-orbit: mutable ViewCamera orbit/zoom + default parity floor
    {"view-camera-override", runViewCameraOverrideSelfTest},  // phase-B: output-camera override seam (takes-effect + no-leak, both faces)
    {"layer2d", runLayer2dSelfTest},
    {"camera", runCameraSelfTest},
    {"camera-scope", runCameraScopeSelfTest},        // C1: point-camera hole closed (flat + resident)
    {"camera-resident", runCameraResidentSelfTest},  // C0: Camera→Layer2d through the resident terminal
    {"orthographiccamera", runOrthographicCameraSelfTest},  // C2: OrthographicCamera ortho projection (flat+resident)
    {"shiftcamera", runShiftCameraSelfTest},  // camera-B: ShiftCamera additive CameraToClipSpace nudge (math + resident render flip)
    {"visiblegizmos", runVisibleGizmosSelfTest},  // camera-B: VisibleGizmos MultiInput visibility gate (On/Off/Inherit truth table, resident)
    {"reusecamera", runReuseCameraSelfTest},  // camera-B: ReuseCamera referenced-camera push (Object wire, flat+resident + missing-ref drop)
    {"orbitcamera", runOrbitCameraSelfTest},  // camera-B: OrbitCamera angle->position pipeline (transcription oracle + invariants + resident render)
    {"execute", runExecuteSelfTest},
    {"layercompose", runLayerComposeSelfTest},
    {"group", runGroupSelfTest},
    {"spreadintogrid", runSpreadIntoGridSelfTest},  // render lane: per-WIRE grid translation (SpreadIntoGrid.cs:47-51), flat+resident
    {"spreadlayout", runSpreadLayoutSelfTest},      // render lane: per-WIRE line-spread SRT (SpreadLayout.cs:34/59-60), flat+resident
    {"getscreenpos", runGetScreenPosSelfTest},      // render lane: world→screen projection (GetScreenPos.cs:22-44) + value-rail latch, flat+resident
    {"getposition", runGetPositionSelfTest},        // flow.context: transform-scope read (GetPosition.cs:39-51) + value-rail latch, flat+resident
    {"gpumeasure", runGpuMeasureSelfTest},          // render.analyze: GPU-time measure mechanism (GpuMeasure.cs:65,74) SMOKE — value non-zero after render + monotone smoothing
    {"sliceviewport", runSliceViewPortSelfTest},    // render.transform: cell viewport rect + RepeatView clip scale (SliceViewPort.cs:40-73) closed-form + pixel-in-cell, flat+resident
    {"renderstate-bothleg", runRenderStateBothLegSelfTest},  // ★Seam 2 harness-first: flat+resident stamped tuples byte-identical
    {"rasterizerstate", runRasterizerStateSelfTest},         // Seam 2 closed-form: cull/fill/winding → MTL enum + DX11 defaults
    {"blendstate", runBlendStateSelfTest},                   // Seam 2 closed-form: 7 blend factors + 3 ops + A2C=false
    {"pso-cache", runPsoCacheSelfTest},                      // Seam 2: frozenPSOKey full-tuple key (dynamic depthBias excluded)
    {"s2-guards", runS2GuardsSelfTest},                      // Seam 2 Bucket-C guards: logic-op / dual-source+MRT port-time reject
    {"s2-depthbias", runS2DepthBiasSelfTest},                // Seam 2 Bucket-B: depthBias pass-through (numeric-output DEFERRED)
    {"outputmerger-bothleg", runOutputMergerBothLegSelfTest},        // ★Seam 2 OutputMerger harness-first: flat+resident stamped blend/depth tuples byte-identical
    {"outputmerger-cookthrough", runOutputMergerCookThroughSelfTest},// Seam 2 OutputMerger cook-through: real NodeSpec blend/depth params → cookOutputMerger → stamped tuple == census combo
    {"inputassembler", runInputAssemblerSelfTest},                   // Seam 2 IA closed-form: PrimitiveTopology → MTL::PrimitiveType table
    {"inputassembler-cookthrough", runInputAssemblerCookThroughSelfTest}, // Seam 2 IA cook-through: real NodeSpec topology → cookInputAssembler → stamped frozen.topology == census (both legs identical)
    {"draw-explicit", runDrawExplicitSelfTest},                      // Seam 2 Draw cook-through: real NodeSpec VertexCount/Start → cookDrawExplicit → DrawKind::Explicit item (both legs identical)
    {"draw-explicit-topology", runDrawExplicitTopologySelfTest},     // Seam 2 Draw composition: InputAssembler(LineList) wraps Draw → stamped Explicit item's topology == LineList

    {"transformops", runTransformOpsSelfTest},
    {"drawmeshunlit", runDrawMeshUnlitSelfTest},
    {"mathops", runMathOpsSelfTest},
    {"statefulvalue", runStatefulValueSelfTest},
    {"conegizmo", runConeGizmoSelfTest},  // C3 gizmo Tranche-0: ConeGizmo generator + gizmo_geometry helper
    {"gizmo-box", runGizmoBoxSelfTest},          // C3 gizmo Tranche-1: DrawBoxGizmo (12-edge box) + resident DrawLines pixel
    {"gizmo-sphere", runGizmoSphereSelfTest},    // C3 gizmo Tranche-1: DrawSphereGizmo (lat/long wireframe rings)
    {"gizmo-grid", runGizmoGridSelfTest},        // C3 gizmo Tranche-1: DrawLineGrid (wireframe grid, adjacent lines lit)
    {"gizmo-locator", runGizmoLocatorSelfTest},  // C3 gizmo Tranche-1: Locator (3-axis cross, geometry only)
    {"particlesim-integrate-parity", runParticleSimIntegrateParitySelfTest},  // 骨5: ParticleSystem integrator core cook-through — per-step Δpos == TiXL pow(1-Drag,Speed)·Speed·0.01 closed-form (injectBug=stale linear-drag/missing-0.01 → RED)
    {"particlefield-probe", runParticleFieldProbeSelfTest},  // PF-a TERMINAL probe: field-into-force bridge consumed (anisotropy≠0 both legs)
    {"vectorfieldforce-field", runVectorFieldForceFieldSelfTest},  // PF-a closed-form: 1 particle field-sampled -> Velocity≈(0.5625A,0,0.5625A)
    {"fielddistanceforce-field", runFieldDistanceForceFieldSelfTest},  // PF bridge closed-form: SphereSDF + 1 particle @(1,0,0) -> Velocity≈(-A,0,0)
    {"randomjumpforce-field", runRandomJumpForceFieldSelfTest},  // PF bridge field-gate: SphereSDF -> Position moves & scales with Amount (fork-RandomJump-position-write)
    {"fieldvolumeforce-field", runFieldVolumeForceFieldSelfTest},  // PF bridge closed-form: SphereSDF + 1 particle @(1,0,0), Attraction=1 -> Velocity≈(-0.425A,0,0) (exercises *0.425 Attraction fork)
    {"fieldtree-builder", runFieldTreeBuilderSelfTest},  // PF-0: graph->FieldNode builder (flat+resident both legs)
    {"movepointstosdf", runMoveToSdfSelfTest},  // SDF point-modify seam: MoveToSDF raymarch to SphereSDF surface (on-sphere readback; -bug severs Field -> pass-through)
    {"sdfreflectionlinepoints", runSdfReflectionLinePointsSelfTest},  // SDF point-modify + count-multiply seam: LinePoints -> SdfReflectionLinePoints(Field=SphereSDF) -> count=src*perLine + line[1] on-sphere; -bug severs Field -> pass-through
    {"raymarchpoints", runRaymarchPointsSelfTest},  // SDF point-modify + count-multiply seam (TWO modes): LinePoints -> RaymarchPoints(Field=SphereSDF) -> count=src*(MaxSteps+1)*(clampRefl+1); MODE0 line[1] / MODE1 last-step on-sphere; -bug severs Field -> pass-through
    {"pointcolorwithfield", runPointColorWithFieldSelfTest},  // direct-Field gather LEAF: PointColorWithField recolors from SphereSDF color branch (-bug severs Field -> pass-through seed)
    {"selectpointswithsdf", runSelectPointsWithSdfSelfTest},  // direct-Field gather LEAF: SelectPointsWithSDF writes FX1=max(0,1-2*dist) from SphereSDF (-bug severs Field -> pass-through FX1=0)
    {"field-paramapply", runFieldParamApplySelfTest},  // PF-0c: data-driven field param-apply (graph SphereSDF {Radius:0.8} -> GPU + buffer + enum + slot-id guard)
    {"time-op", runTimeSelfTest},  // Time 5-mode (localFxTime/localTime/playback/runtime/frozen) + GetFrameSpeedFactor (fsf-valid/fallback/interactive)
    {"pointtrailfast", runPointTrailFastSelfTest},  // ★cross-frame trail ring: 2-frame persistent-ring accumulation (posA survives into f1)
    {"pointtrail", runPointTrailSelfTest},          // ★cross-frame 3-pass trail: Collect ring persists across frames, Copy emits newest-first
    {"growstrains", runGrowStrainsSelfTest},        // 2-input cartesian product + GrowthMap texture; per-frame transform vs formula
    {"radial-parity", runRadialPointsParitySelfTest},  // ★PARITY-GATE Stage-1 pilot: RadialPoints production defaults vs TiXL .t3 (RED-first template)
    {"grid-parity", runGridPointsParitySelfTest},  // ★PARITY-GATE param-completion fan-out #2: GridPoints attribute knobs Color/F1/F2/Orientation/Scale (RED-first)
    {"line-parity", runLinePointsParitySelfTest},  // ★PARITY-GATE param-completion fan-out #3: LinePoints knobs ColorA/ColorB(dual-color lerp)/F1/F2/Orientation/Twist/AddSeparator (RED-first)
    {"turbulence-parity", runTurbulenceParitySelfTest},  // ★PARITY-GATE Stage-1: TurbulenceForce 15× amplitude tooth + Phase=wall-clock determinism probe (RED-first)
    {"directionalforce-parity", runDirectionalForceParitySelfTest},  // ★PARITY-GATE Stage-3 Force-class first: DirectionalForce NodeSpec-default cook-through vs TiXL .t3 Amount=0.007 + closed-form kernel-vel (补验证闸, already-faithful)
    {"axisstep-parity", runAxisStepForceParitySelfTest},  // Stage-3 Force: AxisStepForce CREATE-vel T2 cook-through 守 NodeSpec SelectRatio=0.1
    {"vectorfieldforce-parity", runVectorFieldForceParitySelfTest},  // Stage-3 Force: VectorFieldForce CREATE-vel no-field baked, T2 cook-through 守 NodeSpec
    {"snaptoanglesforce-parity", runSnapToAnglesForceParitySelfTest},  // Stage-3 Force: SnapToAnglesForce TRANSFORM; cook no-op 契約 + T1b cook-through NodeSpec discrim (velocity-seed latch; P5 恆真假錨 fixed 2026-07-03)
    {"fielddistanceforce-parity", runFieldDistanceForceParitySelfTest},  // Stage-3 Force: FieldDistanceForce TRANSFORM baked no-field no-op 契約 (wired-SDF DEFERRED)
    {"fieldvolumeforce-parity", runFieldVolumeForceParitySelfTest},  // Stage-3 Force: FieldVolumeForce TRANSFORM baked no-field no-op 契約 (wired-SDF DEFERRED)
    {"drawpoints-parity", runDrawPointsParitySelfTest},  // ★PARITY-GATE 修偏差: DrawPoints v1 退化4px死點 → DrawKind::Points2 quad-sprite + PointSize/Color cook-through (RED-first 雙牙; -bug 復活4px死點)
    {"hexgrid-parity", runHexGridPointsParitySelfTest},  // ★PARITY-GATE param-completion fan-out: HexGridPoints 11th input Scale (Size·Scale via .t3 ScaleVector3) cook-through vs TiXL .hlsl closed-form X (RED-first; -bug re-bakes Scale→1)
    {"t3-transformpoints", runT3TransformPointsParity},  // ★KEYSTONE首證: real TransformPoints.t3 import→buildEvalGraph→cookResident vs xfprobe — MEASURED GREEN (GPU-compute replay LIVE: ComputeShaderStage + StructuredBufferWithViews + TransformMatrix atoms all mapped)
    {"t3-combinebuffers", runT3CombineBuffersParity},  // 187 量產第一波: real CombineBuffers.t3 code-op replay → concat oracle parity (量產配方 verification spike on an arbitrary compound)
    {"t3-nestedcompound", runT3NestedCompoundParity},  // ★COMPOUND-RECURSION keystone: DrawPointsDOF.t3 recurses its lone TransformPoints child into a NESTED compound (resolver-driven) → structural (nested atomic==false + §1.3 cross-layer Points wire) + cross-boundary parity vs host-matrix oracle; -bug disables recursion → nested absent → RED
    {"t3-layout", runT3LayoutGolden},  // ★.t3ui LAYOUT keystone (柏為 07-08 "鑽入複合子節點全擠在原點"): real TransformPoints.t3+.t3ui → every child/boundary-pin x,y matches the .t3ui Position constants; -bug (t3LayoutDisable) reverts every position to 0,0 → RED
    {"t3-transformpoints-retire", runT3TransformPointsRetireGates},  // ★廢棄節點退場 PILOT: retired flat TransformPoints atom → references auto-taken-over by .t3 compound (①takeover polarity ③reference reachability ②parity ④layout, all four teeth under one -bug)
    {"t3-combinebuffers-retire", runT3CombineBuffersRetireGates},  // ★廢棄節點退場 PILOT #2: retired flat CombineBuffers atom → references auto-taken-over by .t3 compound (guid 4dd8a618…) — proves the seam is NOT TransformPoints-specific (①takeover ③reference ②parity ④layout under one -bug)
    {"t3-snappointstogrid-retire", runT3SnapPointsToGridRetireGates},  // ★廢棄節點退場: retired flat SnapPointsToGrid atom → .t3 compound (guid bc88304a…) takeover; ②parity is COOK-DRIVEN vs the mathv oracle (computeshaderstage_snaptogrid kernel) — ①takeover ③reference ②parity ④layout under one -bug
    {"t3-wrappointposition-retire", runT3WrapPointPositionRetireGates},  // ★廢棄節點退場: retired flat WrapPointPosition atom → .t3 compound (guid 0814a593…) takeover; ②parity COOK-DRIVEN through the generic ComputeShaderStage IN-PLACE (u0-only, no SRV) dispatch vs the mathv oracle (computeshaderstage_wrappointposition kernel)
    {"t3-addnoise-retire", runT3AddNoiseRetireGates},  // ★廢棄節點退場: retired flat AddNoise atom → .t3 compound (guid dd586355…) takeover; ②parity COOK-DRIVEN vs the mathv oracle with a TRANSCENDENTAL double fraction gate (simplex-noise displace, computeshaderstage_addnoise kernel)
    {"t3-clearsomepoints-retire", runT3ClearSomePointsRetireGates},  // ★廢棄節點退場: retired flat ClearSomePoints atom → .t3 compound (guid e570b2e6…) takeover; ②parity COOK-DRIVEN vs the mathv oracle (branchy per-block hash kill Scale→NAN, computeshaderstage_clearsomepoints kernel; b0 float Ratio + b1 int Seed/Repeat/Resolution)
    {"t3-snaptopoints-retire", runT3SnapToPointsRetireGates},  // ★廢棄節點退場: retired flat SnapToPoints atom → .t3 compound (guid 5822b0d8…) takeover; ②parity COOK-DRIVEN vs the mathv oracle (DUAL-SRV index-paired snap, Position+FX1(W); computeshaderstage_snaptopoints kernel; b0 [BlendFactor,Distance,MaxAmount]; stride 32→64 fork)
    {"t3-blendpoints-retire", runT3BlendPointsRetireGates},  // ★廢棄節點退場: retired flat BlendPoints atom → .t3 compound (guid 2dc5c9d1…) takeover; ②parity COOK-DRIVEN vs the mathv oracle (DUAL-SRV index-paired blend, Position+FX1; computeshaderstage_blendpoints kernel; b0 5 floats; generic-seam per-SRV countB aux extension)
    {"t3-meshverticestopoints-retire", runT3MeshVerticesToPointsRetireGates},  // ★廢棄節點退場: retired flat MeshVerticesToPoints atom → .t3 compound (guid 2467e1ed…) takeover; ②parity COOK-DRIVEN vs the mathv oracle (Mesh SRV → fresh Point UAV, Stride=64 native; computeshaderstage_meshverticestopoints kernel; b0 [OffsetByTBN.x,W])
    {"t3-brdflookup", runT3BrdfLookupGates},  // ★TEXTURE-COMPUTE SEAM 首證 (stage 1): _ComputeBRDFLookup.t3 → ComputeShaderStageTex collapse → resident cook + RGBA16 readback vs split-sum BRDF oracle (①takeover ③reference ②parity ④layout under one -bug)
    {"t3-hse", runT3HseParity},  // ★IMAGE-fx collapse seam: HSE.t3 (single _multiImageFxSetupStatic wrapper) → sw HSE tex atom → resident cook + readback vs hue-shift oracle (RED→GREEN; -bug drops FxTexture → RED)
    {"t3-blend", runT3BlendParity},  // ★IMAGE-fx collapse GENERALIZES: MULTI-child Blend.t3 (6 helper value ops + fx-setup) through the SAME collapse → Normal-blend oracle; proves not HSE-only
    {"t3-bubblezoom", runT3BubbleZoomParity},  // ★IMAGE-fx collapse GRADIENT-FED: BubbleZoom.t3 with GradientsToTexture ELIDED onto the atom's Gradient port (2×Vector2Components kept) → closed-form gradient oracle; -bug drops the Gradient wire → RED. Unlocks gradient-fed image-fx
    {"t3-radialgradient", runT3RadialGradientParity},  // GRADIENT-FED generator: RadialGradient.t3 (3×V2C+3×B2F+I2F helpers, GTT elided) → closed-form radial oracle; -bug drops Gradient wire → RED
    {"t3-outputdefs", runT3ImportOutputDefsSelfTest},  // CATALOG-NODE metadata承重: imported RadialGradient.t3 outputDefs (type==terminal atom OUTPUT) + real name (not GUID) → draggable node w/ output PIN; -bug clears them → BITE
    {"t3-ngongradient", runT3NGonGradientParity},  // GRADIENT-FED generator: NGonGradient.t3 (2×V2C+2×B2F+I2F, 8 boundary FloatParams, GTT elided) → closed-form NGon oracle; -bug drops Gradient wire → RED
    {"t3-boxgradient", runT3BoxGradientParity},  // GRADIENT-FED generator: BoxGradient.t3 (3×V2C+V4C(CornersRadius)+2×B2F+I2F, GTT elided) → closed-form box-SDF oracle; -bug drops Gradient wire → RED
    {"t3-remapcolor", runT3RemapColorParity},  // GRADIENT-FED COLOR + TRANSFORMIMAGE-PASSTHROUGH: RemapColor.t3 (I2F+B2F+V2C kept; GTT+identity TransformImage+bypassed GenerateMips all elided) → two-region CheckerBoard vs sw ColorRemap rowLinear oracle; -bug drops Gradient wire → gray → RED
    {"t3-lineargradient", runT3LinearGradientParity},  // GRADIENT-FED generator + OFFSET-ROUTING: LinearGradient.t3 (2×V2C+2×B2F+2×I2F+Multiply+PickFloat kept, GTT elided) → closed-form linear-ramp oracle; -bug drops Gradient wire → RED
    {"t3-timeclip-oob", runT3TimeClipOobRegression}  // CRASH regression: parseChildTimeClips heap-OOB on a DirtyFlagTrigger-only Outputs entry (absent OutputData; DrawMesh/FindClosestPointsOnMesh.t3) — ① no-crash+empty ② full-clip values ③ conform; -bug drops the .contains() guard → ASan OOB abort
);
}  // namespace sw
