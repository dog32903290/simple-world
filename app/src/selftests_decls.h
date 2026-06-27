// app/src/selftests_decls.h — shared declarations for the --selftest router + its area manifests.
//
// Shell-tier header (lives at app/src/ root like selftests.cpp / metal_impl.cpp): it may include any
// zone, because it only declares the per-subsystem --selftest entry points that the router ROUTES to.
// It carries (1) every header that declares a selftest fn, and (2) the no-header forward-decls for
// the shell-tier goldens (field_*/mesh_*/list/gradient .cpp at src root that have no header).
//
// selftests.cpp (the thin reader) and every selftests_<area>.cpp manifest leaf include this single
// header, so each leaf can name any selftest fn without tracking which header declares it. Adding a
// selftest = add a row to the relevant area leaf's REGISTER_SELFTESTS block; only add a line HERE if
// the new selftest is a NEW no-header shell-tier golden (mirrors the old inline fwd-decls).
#pragma once

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
#include "runtime/compound_folder.h"  // runFolderPackageSelfTest (folder-package .swpkg round-trip)
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
#include "runtime/graph.h"
#include "runtime/image_filter_op_registry.h"  // imageFilterSelfTests() self-registered sink
#include "runtime/value_op_registry.h"          // valueOpSelfTests() self-registered sink
#include "runtime/particle_system.h"
#include "runtime/point_graph.h"
#include "runtime/point_ops.h"
#include "runtime/point_ops_camera_scope.h"  // runCameraScopeSelfTest (C1) + runCameraResidentSelfTest (C0)
#include "runtime/point_ops_orthographiccamera.h"  // runOrthographicCameraSelfTest (camera3d C2)
#include "runtime/spectrum_analyzer.h"
#include "runtime/bpm_detection.h"  // runBpmDetectionSelfTest (L6 BPM auto-detect, TiXL parity)
#include "runtime/detect_bpm.h"     // runDetectBpmSelfTest (TiXL DetectBpm operator parity, node-level)
#include "runtime/transport.h"
#include "ui/cjk_font.h"
#include "ui/annotation_draw.h"  // runAnnotationDrawSelfTest (annotation draw/interaction geometry)
#include "ui/canvas_ids.h"  // runCanvasIdsSelfTest (ed pin/node id bands)
#include "ui/node_draw.h"   // runNodeValSelfTest (body value-string format + zoom gating)
#include "ui/fence_preview.h"  // runFenceSelfTest (rubber-band overlap predicate)
#include "ui/keymap.h"      // runKeymapSelfTest (K0 table completeness)
#include "ui/quick_add.h"   // runQuickAddSelfTest (palette filter + eye hook naming)
#include "ui/node_style.h"
#include "ui/theme.h"            // runThemeSelfTest (default theme table == TiXL UiColors constants)
#include "ui/timeline_window.h"  // runTimelineSelfTest (S6 timeline gesture core)
#include "ui/graph_dump.h"   // runGraphDumpSelfTest (req_graph -> graph.json of current compound)
#include "verify/eye/eye.h"
#include "verify/hand/hand.h"

// --- no-header shell-tier golden forward-decls (verbatim from the old selftests.cpp top) ---
namespace sw { int runFieldRenderSelfTest(bool);  // field_render_golden.cpp (shell-tier GPU golden)
int runFxaaSelfTest(bool);  // point_ops_fxaa.cpp — NVIDIA FXAA 3.11 AA (no point_ops.h line: linecount ratchet at cap)
int runRaymarchFieldOutputSelfTest(bool);  // field_raymarch_output_golden.cpp (production-path field→output golden)
int runConnectCooksSelfTest(bool);         // connect_cooks_golden.cpp (connect VERB → cook → sphere silhouette)
int runHandConnectSelfTest(bool);          // ui/connect_verb_selftest.cpp (connect/disconnect verbs → wire edit)
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
namespace sw { int runFieldToroidalVortexFieldGoldenSelfTest(bool); } // field_ops_toroidalvortexfield_golden.cpp (vec3 VECTOR-field generator; decay-channel GPU golden + velocity-text assertion, axis-enum)
namespace sw { int runFieldSetSDFMaterialGoldenSelfTest(bool); }    // field_ops_setsdfmaterial_golden.cpp (custom-collect adjust; p.w gate (0.5,1.5) + Color float4; p.w-setter + Readback leaf pattern)
namespace sw { int runFieldRaymarchSelfTest(bool); }               // field_raymarch_golden.cpp (3D sphere-trace path: silhouette+symmetry+depth/hit teeth; camera from defaultRaymarchTransforms)
namespace sw { int runParticleFieldProbeSelfTest(bool); }         // particlefield_probe_golden.cpp (PF-a TERMINAL probe: field-into-force bridge consumed on both legs — anisotropy≠0)
namespace sw { int runVectorFieldForceFieldSelfTest(bool); }      // particlefield_probe_golden.cpp (PF-a closed-form: one particle @field-space(0.25,0,0) -> Velocity≈(0.5625A,0,0.5625A))
namespace sw { int runFieldDistanceForceFieldSelfTest(bool); }    // fielddistanceforce_field_golden.cpp (PF bridge closed-form: SphereSDF + particle @(1,0,0) -> Velocity≈(-A,0,0))
namespace sw { int runRandomJumpForceFieldSelfTest(bool); }       // randomjumpforce_field_golden.cpp (PF bridge field-gate: SphereSDF -> Position moves & scales linearly with Amount; fork-RandomJump-position-write)
namespace sw { int runFieldVolumeForceFieldSelfTest(bool); }      // fieldvolumeforce_field_golden.cpp (PF bridge closed-form: SphereSDF + particle @(1,0,0), Attraction=1 -> Velocity≈(-0.425*A,0,0), exercises the .t3 *0.425 Attraction fork)
namespace sw { int runFieldTreeBuilderSelfTest(bool); }          // fieldtree_builder_golden.cpp (PF-0 graph->FieldNode builder: flat+resident both build ToroidalVortexField tree + project wired Radius)
namespace sw { int runMoveToSdfSelfTest(bool); }                // movepointstosdf_golden.cpp (SDF point-modify seam: GridPoints -> MoveToSDF(Field=SphereSDF) raymarch -> on-sphere readback; -bug severs Field wire -> pass-through unmoved grid)
namespace sw { int runSdfReflectionLinePointsSelfTest(bool); }  // sdfreflectionlinepoints_golden.cpp (SDF point-modify + count-multiply seam: LinePoints -> SdfReflectionLinePoints(Field=SphereSDF) -> count=src*perLine + line[1] on-sphere; -bug severs Field -> pass-through, no hit)
namespace sw { int runFieldParamApplySelfTest(bool); }          // field_paramapply_golden.cpp (PF-0c data-driven param-apply: graph SphereSDF {Radius:0.8} -> GPU f.w + buffer slot + CombineSDF enum text + slot-id==port-id guard)
namespace sw { int runMeshNGonGoldenSelfTest(bool); }            // mesh_golden.cpp (4th cook flow: NGonMesh)
namespace sw { int runMeshQuadGoldenSelfTest(bool); }            // mesh_golden.cpp (4th cook flow: QuadMesh)
namespace sw { int runMeshTransformGoldenSelfTest(bool); }       // mesh_input_golden.cpp (mesh-input seam: TransformMesh consumer)
namespace sw { int runMeshCombineGoldenSelfTest(bool); }         // mesh_input_golden.cpp (mesh-input seam: CombineMeshes MultiInput)
namespace sw { int runMeshInputProductionGoldenSelfTest(bool); } // mesh_input_golden.cpp (★R-2 production cookResident pixel + DrawMeshUnlit hole fix)
namespace sw { int runMeshFlipNormalsGoldenSelfTest(bool); }     // mesh_modify_golden.cpp (mesh modify: FlipNormals, flat + R-2 resident)
namespace sw { int runMeshVerticesToPointsSelfTest(bool); }      // point_ops_meshverticestopoints.cpp (★mesh-into-points seam: Mesh→Points, R-2 flat+resident)
namespace sw { int runPointsOnMeshSelfTest(bool); }              // point_ops_pointsonmesh_golden.cpp (★area-weighted surface scatter; consumes meshIdx + ColorMap, R-2 + area-CDF leg)
namespace sw { int runMeshRecomputeNormalsGoldenSelfTest(bool); }// mesh_modify_golden.cpp (mesh modify: RecomputeNormals face-cross, flat + R-2 resident)
namespace sw { int runMeshTransformUvsGoldenSelfTest(bool); }    // mesh_modify_golden.cpp (mesh modify: TransformMeshUVs matrix·uv, flat + R-2 resident)
namespace sw { int runMeshSplitVerticesGoldenSelfTest(bool); }   // mesh_modify2_golden.cpp (mesh modify: SplitMeshVertices un-weld 3×face, flat + R-2 resident)
namespace sw { int runMeshSelectVerticesGoldenSelfTest(bool); }  // mesh_modify2_golden.cpp (mesh modify: SelectVertices volume field → Selection, flat + R-2 resident)
namespace sw { int runMeshDeformGoldenSelfTest(bool); }          // mesh_modify2_golden.cpp (mesh modify: DeformMesh Spherize/Taper/Twist position, flat + R-2 resident)
namespace sw { int runMeshCollapseGoldenSelfTest(bool); }        // mesh_modify2_golden.cpp (mesh modify: CollapseVertices grid-snap by field, flat + R-2 resident)
namespace sw { int runMeshProjectUvGoldenSelfTest(bool); }       // mesh_modify2_golden.cpp (mesh modify: MeshProjectUV planar uv=pos·M+1, flat + R-2 resident)
namespace sw { int runMeshSphereGoldenSelfTest(bool); }          // mesh_sphere_golden.cpp (mesh generate: SphereMesh UV-sphere, closed-form poles+equator)
namespace sw { int runMeshTorusGoldenSelfTest(bool); }           // mesh_torus_golden.cpp (mesh generate: TorusMesh tube×radius rings)
namespace sw { int runMeshCylinderGoldenSelfTest(bool); }        // mesh_cylinder_golden.cpp (mesh generate: CylinderMesh hull, closed-form)
namespace sw { int runMeshCubeGoldenSelfTest(bool); }            // mesh_cube_golden.cpp (mesh generate: CubeMesh 6-side, front-face closed-form)
namespace sw { int runMeshIcosahedronGoldenSelfTest(bool); }     // mesh_icosahedron_golden.cpp (mesh generate: IcosahedronMesh golden-ratio base, unit-sphere invariant)
namespace sw { int runFloatListSelfTest(bool); }                 // floatlist_golden.cpp (5th cook flow: FloatsToList host list)
namespace sw { int runColorsToListSelfTest(bool); }              // colorlist_golden.cpp (vec4-list cook flow: ColorsToList host color list, flat + R-2 resident)
namespace sw { int runColorListSelfTest(bool); }                // colorlist_fanout_golden.cpp (ColorList identity passthrough, flat + R-2 resident)
namespace sw { int runSetBpmSelfTest(bool); }                   // setbpm_golden.cpp ([SetBpm] VJ op: triggered-pull SetBpm edge → BpmProvider → comp.bpm)
namespace sw { int runBpmTransportSelfTest(bool); }             // bpm_transport_golden.cpp (end-to-end: DetectBpm auto → SetBpm edge → BpmProvider → transport.bpm)
namespace sw::runtime { int runBeatSyncSelfTest(bool); }        // beat_synchronizer.cpp (G1: audio-locked BPM/bar P-controller, port of BeatSynchronizer.cs)
namespace sw { int runBeatLockSelfTest(bool); }                 // beat_lock_selftest.cpp (G2 SlidingAverage<10> de-jitter + G3 beat_timing audio-lock orphan接通)
namespace sw { int runCombineColorListsSelfTest(bool); }        // colorlist_fanout_golden.cpp (CombineColorLists MultiInput concat, flat + R-2 resident)
namespace sw { int runReadPointColorsSelfTest(bool); }          // colorlist_fanout_golden.cpp (ReadPointColors: Points bag .Color -> ColorList, flat)
namespace sw { int runKeepColorsSelfTest(bool); }               // keepcolors_golden.cpp (per-node cross-frame colorlist STATE: KeepColors accumulate/cap/reset, flat + R-2 resident)
namespace sw { int runStringRailSelfTest(bool); }                // string_rail_golden.cpp (6th cook flow: String value rail)
namespace sw { int runHasStringChangedSelfTest(bool); }          // hasstringchanged_golden.cpp (per-node cross-frame STRING state: HasStringChanged delta, flat + R-2 resident)
namespace sw { int runListRoutingSelfTest(bool); }               // list_routing_golden.cpp (FloatList→Float bridge: downstream evalFloat)
namespace sw { int runFloatListProducersSelfTest(bool); }        // floatlist_producers_golden.cpp (wave-2 FloatList→FloatList producers: Combine/IntsToList/SetFloat/SetInt/Remap, chain-through-evalFloat)
namespace sw { int runSmoothValuesSelfTest(bool); }              // floatlist_smoothvalues_golden.cpp (SmoothValues forward-window box average, STATELESS FloatList→FloatList, chain-through-evalFloat)
namespace sw { int runAnimFloatListSelfTest(bool); }             // floatlist_animfloatlist_golden.cpp (AnimFloatList animator PRODUCER: AnimMath shapes → List<float> on LocalFxTime, flat+resident chain-through)
namespace sw { int runFloatListConversionSelfTest(bool); }       // floatlist_conversion_golden.cpp (FloatListToIntList trunc-toward-zero + IntListToFloatList widening, chain-through-evalFloat)
namespace sw { int runListRoutingWave1SelfTest(bool); }          // list_routing_wave1_golden.cpp (list fan-out wave-1: SumRange/IntListLength/PickIntFromList/CompareFloatLists bridge)
namespace sw { int runPointListSelfTest(bool); }                 // pointlist_golden.cpp (7th cook flow: CPU point list + ListToBuffer bridge)
namespace sw { int runConeGizmoSelfTest(bool); }                 // conegizmo_golden.cpp (C3 gizmo Tranche-0: ConeGizmo generator via pointlist seam, closed-form cone geometry)
namespace sw { int runGizmoBoxSelfTest(bool); }                  // drawboxgizmo_golden.cpp (C3 gizmo Tranche-1: DrawBoxGizmo 12-edge box, transport + resident DrawLines pixel)
namespace sw { int runGizmoSphereSelfTest(bool); }               // drawspheregizmo_golden.cpp (C3 gizmo Tranche-1: DrawSphereGizmo lat/long rings)
namespace sw { int runGizmoGridSelfTest(bool); }                 // drawlinegrid_golden.cpp (C3 gizmo Tranche-1: DrawLineGrid wireframe grid, adjacent-line pixel)
namespace sw { int runGizmoLocatorSelfTest(bool); }              // locator_golden.cpp (C3 gizmo Tranche-1: Locator 3-axis cross [fork-gizmo-screen-constant], geometry only)
namespace sw { int runPointsToCpuSelfTest(bool); }               // pointstocpu_golden.cpp (PointsToCPU: GPU Points bag -> host List<Point>, flat)
namespace sw { int runGradientSelfTest(bool); }                  // gradient_golden.cpp (8th cook flow: SwGradient::sample byte-vs-TiXL)
namespace sw { int runPickGradientSelfTest(bool); }              // gradient_ops_pickgradient.cpp (MultiInput Gradient select)
namespace sw { int runBlendGradientsSelfTest(bool); }            // gradient_ops_blendgradients.cpp (2-gradient cross-merge)
namespace sw { int runLayerComposeSelfTest(bool); }              // point_ops_layercompose.cpp (★S2c: layer-compose end-to-end — 2 Layer2d → Execute → RenderTarget, blend order=wire order, flat + resident)
namespace sw { int runGroupSelfTest(bool); }                     // point_ops_group.cpp (★S2b: Group SRT transform-context push — Layer2d→Group(translate/scale)→RenderTarget, child quad moves on flat + resident)
namespace sw { int runTransformOpsSelfTest(bool); }              // point_ops_transform_golden.cpp (★S2 island: RotateAroundAxis/Shear/Transform transform-context push over Group — child quad rotates/shears/translates on flat + resident)
namespace sw { int runKeymapPersistSelfTest(bool); }             // keymap_prefs_selftest.cpp (★#11: user keymap JSON overrides factory — set→save→reload round-trip + no-file=factory)
namespace sw { int runUserSettingsSelfTest(bool); }              // user_settings_selftest.cpp (★#12: recent-files MRU — push→save→reload round-trip + dedup/cap/no-file=empty)
namespace sw { int runOutputWindowStateSelfTest(bool); }         // output_window_state_selftest.cpp (out-window-persistence: Output view state pin/res/bg — save→reload round-trip + no-file=Defaults)
namespace sw { int runThemeRegistrySelfTest(bool); }             // theme_registry_selftest.cpp (color-theme-system: construct non-default theme → save → fresh-registry load → bit-for-bit survival)
namespace sw { int runMeshBlendMeshVerticesGoldenSelfTest(bool); }  // mesh_blendpick_golden.cpp (mesh modify: BlendMeshVertices two-mesh lerp, flat + R-2 resident)
namespace sw { int runMeshPickMeshBufferGoldenSelfTest(bool); }     // mesh_blendpick_golden.cpp (mesh modify: PickMeshBuffer MultiInput modular selector, flat + R-2 resident)
namespace sw { int runTimeSelfTest(bool); }                          // stateful_value_ops_selftest_time.cpp (Time 5-mode + GetFrameSpeedFactor fsf-valid/fallback)
