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
    {"transformpoints", runTransformPointsSelfTest},
    {"xfprobe", runTransformPointsParityProbe},
    {"orientpoints", runOrientPointsSelfTest},
    {"randomizepoints", runRandomizePointsSelfTest},
    {"rndrotlock", runRandomizePointsRotationLock},
    {"setpointattributes", runSetPointAttributesSelfTest},
    {"samplepointcolorattributes", runSamplePointColorAttributesSelfTest},
    {"meshverticestopoints", runMeshVerticesToPointsSelfTest},
    {"pointsonmesh", runPointsOnMeshSelfTest},
    {"attributesfromimagechannels", runAttributesFromImageChannelsSelfTest},
    {"linearsamplepointattributes", runLinearSamplePointAttributesSelfTest},
    {"mappointattributes", runMapPointAttributesSelfTest},
    {"transformpointsfromclipspace", runTransformPointsFromClipspaceSelfTest},
    {"samplepointsbycameradistance", runSamplePointsByCameraDistanceSelfTest},
    {"combinebuffers", runCombineBuffersSelfTest},
    {"addnoise", runAddNoiseSelfTest},
    {"filterpoints", runFilterPointsSelfTest},
    {"polartransform", runPolarTransformPointsSelfTest},
    {"polarprobe", runPolarTransformPointsParityProbe},
    {"wrappoints", runWrapPointsSelfTest},
    {"boundpoints", runBoundPointsSelfTest},
    {"transformsomepoints", runTransformSomePointsSelfTest},
    {"xfsomeprobe", runTransformSomePointsParityProbe},
    {"wrappointposition", runWrapPointPositionSelfTest},
    {"snaptogrid", runSnapToGridSelfTest},
    {"hexgridpoints", runHexGridPointsSelfTest},
    {"doylespiral", runDoyleSpiralPointsSelfTest},
    {"clearsomepoints", runClearSomePointsSelfTest},
    {"reorientlinepoints", runReorientLinePointsSelfTest},
    {"resamplelinepoints", runResampleLinePointsSelfTest},
    {"subdividelinepoints", runSubdivideLinePointsSelfTest},
    {"selectpoints", runSelectPointsSelfTest},
    {"softtransformpoints", runSoftTransformPointsSelfTest},
    {"offsetpoints", runOffsetPointsSelfTest},
    {"pointattributefromnoise", runPointAttributeFromNoiseSelfTest},
    {"channelmixer", runChannelMixerSelfTest},
    {"tonemapping", runToneMappingSelfTest},
    {"snaptopoints", runSnapToPointsSelfTest},
    {"pairpointsforlines", runPairPointsForLinesSelfTest},
    {"pickpointlist", runPickPointListSelfTest},
    {"pairpointsforsplines", runPairPointsForSplinesSelfTest},
    {"pairpointsforgridwalklines", runPairPointsForGridWalkLinesSelfTest},
    {"blendpoints", runBlendPointsSelfTest},
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
    {"layer2d", runLayer2dSelfTest},
    {"camera", runCameraSelfTest},
    {"camera-scope", runCameraScopeSelfTest},        // C1: point-camera hole closed (flat + resident)
    {"camera-resident", runCameraResidentSelfTest},  // C0: Camera→Layer2d through the resident terminal
    {"orthographiccamera", runOrthographicCameraSelfTest},  // C2: OrthographicCamera ortho projection (flat+resident)
    {"execute", runExecuteSelfTest},
    {"layercompose", runLayerComposeSelfTest},
    {"group", runGroupSelfTest},
    {"transformops", runTransformOpsSelfTest},
    {"drawmeshunlit", runDrawMeshUnlitSelfTest},
    {"mathops", runMathOpsSelfTest},
    {"statefulvalue", runStatefulValueSelfTest},
    {"conegizmo", runConeGizmoSelfTest},  // C3 gizmo Tranche-0: ConeGizmo generator + gizmo_geometry helper
    {"gizmo-box", runGizmoBoxSelfTest},          // C3 gizmo Tranche-1: DrawBoxGizmo (12-edge box) + resident DrawLines pixel
    {"gizmo-sphere", runGizmoSphereSelfTest},    // C3 gizmo Tranche-1: DrawSphereGizmo (lat/long wireframe rings)
    {"gizmo-grid", runGizmoGridSelfTest},        // C3 gizmo Tranche-1: DrawLineGrid (wireframe grid, adjacent lines lit)
    {"gizmo-gridplane", runGizmoGridPlaneSelfTest},  // C3 gizmo Tranche-1: GridPlane (wireframe grid, shader fork)
    {"gizmo-locator", runGizmoLocatorSelfTest},  // C3 gizmo Tranche-1: Locator (3-axis cross, geometry only)
    {"particlefield-probe", runParticleFieldProbeSelfTest},  // PF-a FLAG probe: field-into-force bridge absent (RED evidence)
);
}  // namespace sw
