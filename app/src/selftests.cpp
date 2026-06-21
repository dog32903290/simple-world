// CLI self-test router (see selftests.h). Shell-tier file (lives at src/ root like
// main.cpp / metal_impl.cpp): may include any zone, because it only ROUTES to each
// subsystem's isolated --selftest entry. Adding a self-test = adding one row to kTable.
#include "selftests.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include <Foundation/Foundation.hpp>
#include <Metal/Metal.hpp>

#include "app/annotation_commands.h"  // runAnnotationSelfTest (Annotation 批A 資料/存讀/命令)
#include "app/audio_monitor.h"
#include "app/command.h"
#include "app/document.h"  // runNavigationSelfTest (composition-path semantics)
#include "app/graph_commands.h"  // runDefRemovalSelfTest (S13 boundary-def removal)
#include "app/animation_commands.h"  // runAnimGuiSelfTest (S3 GUI 動畫命令層)
#include "app/frame_cook.h"  // framecook::runArClockSelfTest (AR 時鐘域 pin 牙)
#include "app/soundtrack.h"  // runSoundtrackSelfTest (soundtrack<->transport follow rule)
#include "platform/audio_capture.h"
#include "platform/audio_devices.h"
#include "platform/image_decode.h"  // platform::runImageDecodeSelfTest (native PNG decode proof)
#include "platform/metal_compile.h"  // platform::runMetalCompileSelfTest (newLibrary(source) proof)
#include "runtime/attack_detector.h"
#include "runtime/compound_graph.h"
#include "runtime/combine.h"
#include "runtime/compound_save.h"
#include "runtime/curve.h"
#include "runtime/curve_animator.h"
#include "runtime/graph_bridge.h"
#include "runtime/resident_eval_graph.h"
#include "runtime/audio_analyzer.h"
#include "runtime/audio_ingest.h"
#include "runtime/audio_reaction.h"
#include "runtime/stateful_value_ops.h"  // runStatefulValueSelfTest (Damp/Spring value-graph sims)
#include "runtime/dispatch.h"
#include "runtime/field_graph.h"  // runFieldCodegenSelfTest (shader-graph codegen, pure string)
#include "runtime/field_camera.h"  // runFieldCameraSelfTest (pure-math camera matrices; Layer2d seam)
namespace sw { int runFieldRenderSelfTest(bool);  // field_render_golden.cpp (shell-tier GPU golden)
}  // ^ forward-declared (no header): the GPU field golden lives at shell tier (binds runtime+platform)
// Per-op SDF GPU goldens (Phase C fan-out) — same shell tier, same no-header forward-decl pattern.
namespace sw { int runFieldBoxSdfGoldenSelfTest(bool); }          // field_ops_boxsdf_golden.cpp
namespace sw { int runFieldBoxFrameSdfGoldenSelfTest(bool); }     // field_ops_boxframesdf_golden.cpp
namespace sw { int runFieldOctahedronSdfGoldenSelfTest(bool); }   // field_ops_octahedronsdf_golden.cpp
namespace sw { int runFieldCapsuleLineSdfGoldenSelfTest(bool); }  // field_ops_capsulelinesdf_golden.cpp
namespace sw { int runFieldChainLinkSdfGoldenSelfTest(bool); }    // field_ops_chainlinksdf_golden.cpp
namespace sw { int runFieldTorusSdfGoldenSelfTest(bool); }        // field_ops_torussdf_golden.cpp (axis-enum)
namespace sw { int runFieldCylinderSdfGoldenSelfTest(bool); }     // field_ops_cylindersdf_golden.cpp (axis-enum)
namespace sw { int runFieldPlaneSdfGoldenSelfTest(bool); }        // field_ops_planesdf_golden.cpp (axis-enum)
namespace sw { int runFieldCappedTorusSdfGoldenSelfTest(bool); }  // field_ops_cappedtorussdf_golden.cpp (axis-enum)
namespace sw { int runFieldPrismSdfGoldenSelfTest(bool); }        // field_ops_prismsdf_golden.cpp (axis+sides enum)
namespace sw { int runFieldPyramidSdfGoldenSelfTest(bool); }      // field_ops_pyramidsdf_golden.cpp (axis-enum, two-vec3)
namespace sw { int runFieldRotatedPlaneSdfGoldenSelfTest(bool); } // field_ops_rotatedplanesdf_golden.cpp (normal-vector, two-vec3)
namespace sw { int runFieldCombineSdfGoldenSelfTest(bool); }      // field_ops_combinesdf_golden.cpp (FIRST combiner: 2-input fold)
namespace sw { int runFieldInvertSdfGoldenSelfTest(bool); }       // field_ops_invertsdf_golden.cpp (single-input post-wrap modifier)
namespace sw { int runFieldAbsoluteSdfGoldenSelfTest(bool); }     // field_ops_absolutesdf_golden.cpp (single-input post-wrap modifier)
namespace sw { int runFieldTranslateGoldenSelfTest(bool); }       // field_ops_translate_golden.cpp (single-input PRE-wrap modifier; vec3 param)
namespace sw { int runFieldRepeatField3GoldenSelfTest(bool); }   // field_ops_repeatfield3_golden.cpp (single-input PRE-wrap; pMod3 fold, vec3 Size)
namespace sw { int runFieldRepeatAxisGoldenSelfTest(bool); }      // field_ops_repeataxis_golden.cpp (single-input PRE-wrap; pMod1/pModMirror1, axis+mirror enums)
namespace sw { int runFieldReflectFieldGoldenSelfTest(bool); }    // field_ops_reflectfield_golden.cpp (single-input PRE-wrap; pReflect, vec3 normal + offset)
namespace sw { int runFieldBendFieldGoldenSelfTest(bool); }       // field_ops_bendfield_golden.cpp (single-input PRE+POST wrap; opBend, axis enum)
namespace sw { int runFieldCombineFieldColorGoldenSelfTest(bool); } // field_ops_combinefieldcolor_golden.cpp (two-input color combiner)
namespace sw { int runFieldRotateAxisGoldenSelfTest(bool); }      // field_ops_rotateaxis_golden.cpp (single-input PRE-wrap; pRotateAxis, axis enum)
namespace sw { int runFieldRotateFieldGoldenSelfTest(bool); }     // field_ops_rotatefield_golden.cpp (single-input PRE-wrap; pRotateAxis ×3, vec3 RotateRad; shares pRotateAxis globals key)
namespace sw { int runFieldTwistFieldGoldenSelfTest(bool); }      // field_ops_twistfield_golden.cpp (single-input PRE-wrap; twist about axis)
namespace sw { int runFieldRepeatFieldLimitGoldenSelfTest(bool); } // field_ops_repeatfieldlimit_golden.cpp (single-input PRE-wrap; pModInterval1, limited repeat)
namespace sw { int runFieldFractalSdfGoldenSelfTest(bool); }      // field_ops_fractalsdf_golden.cpp (Mandelbulb fold; iterations=compile-time selector)
namespace sw { int runFieldCustomSdfGoldenSelfTest(bool); }       // field_ops_customsdf_golden.cpp (verbatim user DistanceFunction inject)
namespace sw { int runFieldImage2dSdfGoldenSelfTest(bool); }      // field_ops_image2dsdf_golden.cpp (FIRST texture-binding leaf; Seam A)
namespace sw { int runFieldRepeatPolarGoldenSelfTest(bool); }     // field_ops_repeatpolar_golden.cpp (single-input PRE-wrap; pModPolar/pModPolarMirror swizzle by-value, axis+mirror enums)
namespace sw { int runFieldTranslateUvGoldenSelfTest(bool); }     // field_ops_translateuv_golden.cpp (single-input POST-wrap; f.xyz shift via readback wrapper)
namespace sw { int runFieldStairCombineSdfGoldenSelfTest(bool); } // field_ops_staircombinesdf_golden.cpp (multi-input combiner; stairs/columns joinery, by-value pMod1 compile)
namespace sw { int runFieldNoiseDisplaceSdfGoldenSelfTest(bool); }// field_ops_noisedisplacesdf_golden.cpp (single-input PRE+POST; simplex distance displace, shared fSimplexNoiseDisplace key)
namespace sw { int runFieldSpatialDisplaceSdfGoldenSelfTest(bool); }// field_ops_spatialdisplacesdf_golden.cpp (single-input PRE-wrap; vNoise position warp, two globals favourable order)
namespace sw { int runFieldTransformFieldGoldenSelfTest(bool); }  // field_ops_transformfield_golden.cpp (single-input PRE+POST; float4x4 point xform mul(v,M)->M*v, UniformScale)
namespace sw { int runFieldPushPullSdfGoldenSelfTest(bool); }     // field_ops_pushpullsdf_golden.cpp (custom-collect adjust; SdfField parent-context + optional AmountField subcontext)
namespace sw { int runFieldBlendSdfWithSdfGoldenSelfTest(bool); } // field_ops_blendsdfwithsdf_golden.cpp (3-input custom-collect; sdfBlendByMask helper + f.xyz mix, shared Common key)
namespace sw { int runMeshNGonGoldenSelfTest(bool); }            // mesh_golden.cpp (4th cook flow: NGonMesh)
namespace sw { int runMeshQuadGoldenSelfTest(bool); }            // mesh_golden.cpp (4th cook flow: QuadMesh)
namespace sw { int runMeshTransformGoldenSelfTest(bool); }       // mesh_input_golden.cpp (mesh-input seam: TransformMesh consumer)
namespace sw { int runMeshCombineGoldenSelfTest(bool); }         // mesh_input_golden.cpp (mesh-input seam: CombineMeshes MultiInput)
namespace sw { int runMeshInputProductionGoldenSelfTest(bool); } // mesh_input_golden.cpp (★R-2 production cookResident pixel + DrawMeshUnlit hole fix)
namespace sw { int runMeshFlipNormalsGoldenSelfTest(bool); }     // mesh_modify_golden.cpp (mesh modify: FlipNormals, flat + R-2 resident)
namespace sw { int runMeshRecomputeNormalsGoldenSelfTest(bool); }// mesh_modify_golden.cpp (mesh modify: RecomputeNormals face-cross, flat + R-2 resident)
namespace sw { int runMeshTransformUvsGoldenSelfTest(bool); }    // mesh_modify_golden.cpp (mesh modify: TransformMeshUVs matrix·uv, flat + R-2 resident)
namespace sw { int runFloatListSelfTest(bool); }                 // floatlist_golden.cpp (5th cook flow: FloatsToList host list)
namespace sw { int runStringRailSelfTest(bool); }                // string_rail_golden.cpp (6th cook flow: String value rail)
namespace sw { int runListRoutingSelfTest(bool); }               // list_routing_golden.cpp (FloatList→Float bridge: downstream evalFloat)
namespace sw { int runPointListSelfTest(bool); }                 // pointlist_golden.cpp (7th cook flow: CPU point list + ListToBuffer bridge)
namespace sw { int runGradientSelfTest(bool); }                  // gradient_golden.cpp (8th cook flow: SwGradient::sample byte-vs-TiXL)
namespace sw { int runPickGradientSelfTest(bool); }              // gradient_ops_pickgradient.cpp (MultiInput Gradient select)
namespace sw { int runBlendGradientsSelfTest(bool); }            // gradient_ops_blendgradients.cpp (2-gradient cross-merge)
#include "runtime/graph.h"
#include "runtime/image_filter_op_registry.h"  // imageFilterSelfTests() self-registered sink
#include "runtime/value_op_registry.h"          // valueOpSelfTests() self-registered sink
#include "runtime/particle_system.h"
#include "runtime/point_graph.h"
#include "runtime/point_ops.h"
#include "runtime/spectrum_analyzer.h"
#include "runtime/transport.h"
#include "ui/cjk_font.h"
#include "ui/annotation_draw.h"  // runAnnotationDrawSelfTest (annotation draw/interaction geometry)
#include "ui/canvas_ids.h"  // runCanvasIdsSelfTest (ed pin/node id bands)
#include "ui/fence_preview.h"  // runFenceSelfTest (rubber-band overlap predicate)
#include "ui/keymap.h"      // runKeymapSelfTest (K0 table completeness)
#include "ui/quick_add.h"   // runQuickAddSelfTest (palette filter + eye hook naming)
#include "ui/node_style.h"
#include "ui/timeline_window.h"  // runTimelineSelfTest (S6 timeline gesture core)
#include "verify/eye/eye.h"
#include "verify/hand/hand.h"

namespace sw {
namespace {

// The app's "eye" color proof. Offscreen-render the SAME background color into a texture
// we own, read the center pixel back, assert it matches (codex-eyes: provenance is
// structural — we read what THIS process drew). injectBug clears a wrong color so the eye
// can FAIL (RED) before we trust a PASS (GREEN). Moved verbatim out of main's shell.
int runColorSelfTest(bool injectBug) {
  NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();
  const int W = 64, H = 64;

  const int ex = (int)std::lround(kBgR * 255.0);
  const int ey = (int)std::lround(kBgG * 255.0);
  const int ez = (int)std::lround(kBgB * 255.0);

  MTL::ClearColor clear = injectBug ? MTL::ClearColor::Make(0.78, 0.12, 0.12, 1.0)
                                    : MTL::ClearColor::Make(kBgR, kBgG, kBgB, 1.0);

  MTL::Device* dev = MTL::CreateSystemDefaultDevice();
  MTL::CommandQueue* q = dev->newCommandQueue();

  MTL::TextureDescriptor* td =
      MTL::TextureDescriptor::texture2DDescriptor(MTL::PixelFormatRGBA8Unorm, W, H, false);
  td->setUsage(MTL::TextureUsageRenderTarget | MTL::TextureUsageShaderRead);
  td->setStorageMode(MTL::StorageModeShared);
  MTL::Texture* tex = dev->newTexture(td);

  MTL::RenderPassDescriptor* rpd = MTL::RenderPassDescriptor::renderPassDescriptor();
  auto* ca = rpd->colorAttachments()->object(0);
  ca->setTexture(tex);
  ca->setLoadAction(MTL::LoadActionClear);
  ca->setClearColor(clear);
  ca->setStoreAction(MTL::StoreActionStore);

  MTL::CommandBuffer* cmd = q->commandBuffer();
  cmd->renderCommandEncoder(rpd)->endEncoding();  // clear only
  cmd->commit();
  cmd->waitUntilCompleted();

  uint8_t px[4] = {0, 0, 0, 0};
  tex->getBytes(px, W * 4, MTL::Region::Make2D(W / 2, H / 2, 1, 1), 0);

  bool pass = std::abs(px[0] - ex) <= 2 && std::abs(px[1] - ey) <= 2 && std::abs(px[2] - ez) <= 2;
  printf("[selftest] center=(%d,%d,%d) expect=(%d,%d,%d) -> %s\n", px[0], px[1], px[2], ex, ey, ez,
         pass ? "PASS" : "FAIL");

  tex->release();
  q->release();
  dev->release();
  pool->release();
  return pass ? 0 : 1;
}

// Data-driven table of the uniform `--selftest[-<name>]` / `--selftest[-<name>]-bug`
// pairs. fn(bool injectBug) -> process exit code. Add a self-test = add a row.
struct SelfTest {
  const char* name;       // "" -> "--selftest"; "graph" -> "--selftest-graph"
  int (*fn)(bool);
};
const SelfTest kTable[] = {
    {"", runColorSelfTest},
    {"graph", runGraphRoundtripSelfTest},
    {"save", runSaveLoadSelfTest},
    {"command", runCommandSelfTest},
    {"defremoval", runDefRemovalSelfTest},
    {"copypaste", runCopyPasteSelfTest},
    {"rename", runRenameSelfTest},
    {"childstate", runChildStateSelfTest},
    {"annotation", runAnnotationSelfTest},
    {"navigation", doc::runNavigationSelfTest},
    {"valuecook", runValueCookSelfTest},
    {"resolve", runResolveSelfTest},
    {"audionode", runAudioNodeSelfTest},
    {"attack", runAttackSelfTest},
    {"analyzer", runAudioAnalyzerSelfTest},
    {"spectrum", runSpectrumSelfTest},
    {"audioreaction", runAudioReactionSelfTest},
    {"audiomonitor", audio_monitor::runAudioMonitorSelfTest},
    {"flow", runParticleFlowSelfTest},
    {"draw", runDrawPointsSelfTest},
    {"decay", runParticleDecaySelfTest},
    {"pointgraph", runPointGraphSelfTest},
    {"residentcook", runResidentCookSelfTest},
    {"residentparity", runResidentCookParitySelfTest},
    {"imagedecode", platform::runImageDecodeSelfTest},
    {"metal-compile", platform::runMetalCompileSelfTest},
    {"field-codegen", runFieldCodegenSelfTest},
    {"field-render", runFieldRenderSelfTest},
    {"field-boxsdf", runFieldBoxSdfGoldenSelfTest},
    {"field-boxframesdf", runFieldBoxFrameSdfGoldenSelfTest},
    {"field-octahedronsdf", runFieldOctahedronSdfGoldenSelfTest},
    {"field-capsulelinesdf", runFieldCapsuleLineSdfGoldenSelfTest},
    {"field-chainlinksdf", runFieldChainLinkSdfGoldenSelfTest},
    {"field-torussdf", runFieldTorusSdfGoldenSelfTest},
    {"field-cylindersdf", runFieldCylinderSdfGoldenSelfTest},
    {"field-planesdf", runFieldPlaneSdfGoldenSelfTest},
    {"field-cappedtorussdf", runFieldCappedTorusSdfGoldenSelfTest},
    {"field-prismsdf", runFieldPrismSdfGoldenSelfTest},
    {"field-pyramidsdf", runFieldPyramidSdfGoldenSelfTest},
    {"field-rotatedplanesdf", runFieldRotatedPlaneSdfGoldenSelfTest},
    {"field-combinesdf", runFieldCombineSdfGoldenSelfTest},
    {"field-invertsdf", runFieldInvertSdfGoldenSelfTest},
    {"field-absolutesdf", runFieldAbsoluteSdfGoldenSelfTest},
    {"field-translate", runFieldTranslateGoldenSelfTest},
    {"field-repeatfield3", runFieldRepeatField3GoldenSelfTest},
    {"field-repeataxis", runFieldRepeatAxisGoldenSelfTest},
    {"field-reflectfield", runFieldReflectFieldGoldenSelfTest},
    {"field-bendfield", runFieldBendFieldGoldenSelfTest},
    {"field-combinefieldcolor", runFieldCombineFieldColorGoldenSelfTest},
    {"field-rotateaxis", runFieldRotateAxisGoldenSelfTest},
    {"field-rotatefield", runFieldRotateFieldGoldenSelfTest},
    {"field-twistfield", runFieldTwistFieldGoldenSelfTest},
    {"field-repeatfieldlimit", runFieldRepeatFieldLimitGoldenSelfTest},
    {"field-fractalsdf", runFieldFractalSdfGoldenSelfTest},
    {"field-customsdf", runFieldCustomSdfGoldenSelfTest},
    {"field-image2dsdf", runFieldImage2dSdfGoldenSelfTest},
    {"field-repeatpolar", runFieldRepeatPolarGoldenSelfTest},
    {"field-translateuv", runFieldTranslateUvGoldenSelfTest},
    {"field-staircombinesdf", runFieldStairCombineSdfGoldenSelfTest},
    {"field-noisedisplacesdf", runFieldNoiseDisplaceSdfGoldenSelfTest},
    {"field-spatialdisplacesdf", runFieldSpatialDisplaceSdfGoldenSelfTest},
    {"field-transformfield", runFieldTransformFieldGoldenSelfTest},
    {"field-pushpullsdf", runFieldPushPullSdfGoldenSelfTest},
    {"field-blendsdfwithsdf", runFieldBlendSdfWithSdfGoldenSelfTest},
    {"mesh-ngon", runMeshNGonGoldenSelfTest},
    {"mesh-quad", runMeshQuadGoldenSelfTest},
    {"mesh-transform", runMeshTransformGoldenSelfTest},
    {"mesh-combine", runMeshCombineGoldenSelfTest},
    {"mesh-production", runMeshInputProductionGoldenSelfTest},
    {"mesh-flipnormals", runMeshFlipNormalsGoldenSelfTest},
    {"mesh-recomputenormals", runMeshRecomputeNormalsGoldenSelfTest},
    {"mesh-transformuvs", runMeshTransformUvsGoldenSelfTest},
    {"floatlist", runFloatListSelfTest},
    {"stringrail", runStringRailSelfTest},
    {"listrouting", runListRoutingSelfTest},
    {"pointlist", runPointListSelfTest},
    {"gradient", runGradientSelfTest},
    {"pickgradient", runPickGradientSelfTest},
    {"blendgradients", runBlendGradientsSelfTest},
    {"cropresident", runResidentCropSelfTest},
    {"fastblurresident", runResidentFastBlurSelfTest},
    {"rgbtvresident", runResidentRgbTvSelfTest},
    {"distortandshaderesident", runResidentDistortAndShadeSelfTest},
    {"combine3imagesresident", runResidentCombine3ImagesSelfTest},
    {"combinematerialchannels2resident", runResidentCombineMaterialChannels2SelfTest},
    {"bypasscook", runBypassCookSelfTest},
    {"bypasscompound", runBypassCompoundSelfTest},
    {"graphbridge", runGraphBridgeSelfTest},
    {"statecount", runStateCountSelfTest},
    {"savev2", runSaveV2SelfTest},
    {"testproj", runTestProjSelfTest},
    {"forcekindcorrupt", runForceKindCorruptProbe},
    {"combine", runCombineSelfTest},
    {"compoundspec", runCompoundSpecSelfTest},
    {"compoundmodel", runCompoundModelSelfTest},
    {"cycleguard", runCycleGuardSelfTest},
    {"transport", runTransportSelfTest},
    {"arclock", framecook::runArClockSelfTest},
    {"contextvar", framecook::runContextVarSelfTest},  // context-var YELLOW seam (block #1)
    {"animvalue", framecook::runAnimValueSelfTest},    // Anim* foundation: AnimValue on the prod cook
    {"animint", framecook::runAnimIntSelfTest},        // Anim* integer sibling: AnimInt on the prod cook
    {"animboolean", framecook::runAnimBooleanSelfTest},// Anim* edge-only: AnimBoolean on the prod cook
    {"curve", runCurveSelfTest},
    {"animator", runCurveAnimatorSelfTest},
    {"animgui", runAnimGuiSelfTest},
    {"residenteval", runResidentEvalSelfTest},
    {"multiinput", runMultiInputSelfTest},
    {"residentcache", runResidentCacheSelfTest},
    {"idlefade", runIdleFadeSelfTest},
    {"residentpatch", runResidentPatchSelfTest},
    {"residentlibpatch", runResidentLibPatchSelfTest},
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
    {"attributesfromimagechannels", runAttributesFromImageChannelsSelfTest},
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
    {"drawmeshunlit", runDrawMeshUnlitSelfTest},
    {"mathops", runMathOpsSelfTest},
    {"statefulvalue", runStatefulValueSelfTest},
    {"blur", runBlurSelfTest},
    {"blurchain", runBlurChainSelfTest},
    {"displace", runDisplaceSelfTest},
    {"displacechain", runDisplaceChainSelfTest},
    {"blendchain", runBlendChainSelfTest},                  // multi-image seam (3rd consumer): resident gather
    {"blendwithmaskchain", runBlendWithMaskChainSelfTest},  // multi-image seam (1st THREE-input consumer)
    {"tint", runTintSelfTest},
    {"tintchain", runTintChainSelfTest},
    {"chromab", runChromaBAShiftSelfTest},
    {"adjustcolors", runAdjustColorsSelfTest},
    {"pixelate", runPixelateSelfTest},
    {"sharpen", runSharpenSelfTest},
    {"detectedges", runDetectEdgesSelfTest},
    {"chromaticdistortion", runChromaticDistortionSelfTest},
    {"voronoicells", runVoronoiCellsSelfTest},
    {"dither", runDitherSelfTest},
    {"normalmap", runNormalMapSelfTest},
    {"chromakey", runChromaKeySelfTest},
    {"convertcolors", runConvertColorsSelfTest},
    {"drawlines", runDrawLinesSelfTest},
    {"drawclosedlines", runDrawClosedLinesSelfTest},
    {"drawpoints2", runDrawPoints2SelfTest},
    {"drawlinesbuildup", runDrawLinesBuildupSelfTest},
    {"drawbillboards", runDrawBillboardsSelfTest},
    {"drawscreenquad", runDrawScreenQuadSelfTest},
    {"drawscreenquadclamp", runDrawScreenQuadClampSelfTest},
    {"drawscreenquadfilter", runDrawScreenQuadFilterSelfTest},
    {"drawscreenquadblend", runDrawScreenQuadBlendSelfTest},
    {"drawscreenquadwired", runDrawScreenQuadWiredSelfTest},
    {"clearrendertarget", runClearRenderTargetSelfTest},
    {"timeline", ui::runTimelineSelfTest},
    {"nodestyle", ui::runNodeStyleSelfTest},
    {"anndraw",   ui::runAnnotationDrawSelfTest},
    {"canvasids", ui::runCanvasIdsSelfTest},
    {"cjkfont", ui::runCjkFontSelfTest},
    {"eye", eye::runSelfTest},
    {"map", eye::runMapSelfTest},
    {"hand", hand::runSelfTest},
    {"audioingest", runAudioIngestSelfTest},
    {"soundtrack", soundtrack::runSoundtrackSelfTest},
    {"keymap",    ui::km::runKeymapSelfTest},
    {"quickadd",  ui::runQuickAddSelfTest},
    {"fencepreview", ui::runFenceSelfTest},
};

}  // namespace

int runSelftestFromArgs(int argc, char** argv) {
  for (int i = 1; i < argc; ++i) {
    const char* a = argv[i];

    // --selftest-list: emit one selftest name per line (kTable rows + the self-registered
    // imageFilterSelfTests() sink), so run_all_selftests.sh discovers EVERY tooth — including
    // sink-registered image-filter leaves that never appear as a kTable grep row. (The harness
    // prefers this list and only falls back to grepping kTable when this flag is absent; without
    // it, a sink-only op like TransformImage would silently never run in the bite suite.)
    if (std::strcmp(a, "--selftest-list") == 0) {
      for (const SelfTest& e : kTable) std::printf("%s\n", e.name);  // "" = base color selftest
      // Sink names, skipping any already present in kTable (e.g. convertcolors is in both during the
      // self-registration transition) so the harness never double-runs the same tooth.
      for (const auto& s : imageFilterSelfTests()) {
        bool inTable = false;
        for (const SelfTest& e : kTable)
          if (std::strcmp(e.name, s.first) == 0) { inTable = true; break; }
        if (!inTable) std::printf("%s\n", s.first);
      }
      // Value-op sink names (mirror of the image-filter loop above), skipping any already in
      // kTable so the harness never double-runs the same tooth.
      for (const auto& s : valueOpSelfTests()) {
        bool inTable = false;
        for (const SelfTest& e : kTable)
          if (std::strcmp(e.name, s.first) == 0) { inTable = true; break; }
        if (!inTable) std::printf("%s\n", s.first);
      }
      return 0;
    }

    // Uniform --selftest[-name] / -bug pairs (the table).
    for (const SelfTest& e : kTable) {
      std::string base = std::string("--selftest") + (e.name[0] ? std::string("-") + e.name : "");
      if (base == a) return e.fn(false);
      if (base + "-bug" == a) return e.fn(true);
    }

    // Self-registered IMAGE FILTER selftests (the imageFilterSelfTests() sink that each
    // point_ops_<name>.cpp ImageFilterOp registrar feeds). This activates the dormant half of the
    // self-registration design documented in image_filter_op_registry.h: NodeSpecs were already
    // read from imageFilterSpecSink() by node_registry, but the matching --selftest dispatch was
    // never wired — so new image-filter leaves had to also touch kTable. Reading the sink here makes
    // an image-filter leaf a TRUE zero-shared-file-edit add. (kTable is matched FIRST above, so the
    // pre-existing hardcoded image-filter rows like convertcolors win and do not double-run.)
    for (const auto& e : imageFilterSelfTests()) {
      std::string base = std::string("--selftest-") + e.first;
      if (base == a) return e.second(false);
      if (base + "-bug" == a) return e.second(true);
    }

    // Self-registered VALUE-OP selftests (the valueOpSelfTests() sink that each value_op_<name>.cpp
    // ValueOp registrar feeds). Same dormant-half activation as the image-filter loop above — a
    // value-op leaf becomes a TRUE zero-shared-file-edit add. (kTable matched FIRST, so any name
    // also present there wins and does not double-run.)
    for (const auto& e : valueOpSelfTests()) {
      std::string base = std::string("--selftest-") + e.first;
      if (base == a) return e.second(false);
      if (base + "-bug" == a) return e.second(true);
    }

    // Non-uniform entries (no bug variant / take arguments).
    if (std::strcmp(a, "--selftest-dispatch") == 0) return runDispatchSelfTest();
    if (std::strcmp(a, "--audio-ingest-replay") == 0)
      return runAudioIngestReplay(i + 1 < argc ? argv[i + 1] : "");
    if (std::strcmp(a, "--audio-capture-smoke") == 0)
      return runAudioCaptureSmoke(i + 1 < argc ? atof(argv[i + 1]) : 4.0,
                                  i + 2 < argc ? argv[i + 2] : "");
    if (std::strcmp(a, "--list-audio-devices") == 0) return runListAudioDevices();
    if (std::strcmp(a, "--audio-permission-status") == 0) return runAudioPermissionStatus();

    // 順手債: an UNKNOWN --selftest* flag used to fall through and launch the GUI — which reads as
    // a hung headless run (the binary opens a window instead of printing/exiting). Reject it loudly
    // with a nonzero exit so a typo'd selftest name fails fast instead of looking dead.
    if (std::strncmp(a, "--selftest", 10) == 0) {
      std::fprintf(stderr, "unknown self-test flag: %s\n", a);
      return 2;
    }
  }
  return -1;  // no self-test flag -> launch the GUI
}

}  // namespace sw
