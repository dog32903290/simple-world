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

// --- no-header shell-tier golden forward-decls (verbatim from the old selftests.cpp top) ---
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
namespace sw { int runMeshVerticesToPointsSelfTest(bool); }      // point_ops_meshverticestopoints.cpp (★mesh-into-points seam: Mesh→Points, R-2 flat+resident)
namespace sw { int runPointsOnMeshSelfTest(bool); }              // point_ops_pointsonmesh_golden.cpp (★area-weighted surface scatter; consumes meshIdx + ColorMap, R-2 + area-CDF leg)
namespace sw { int runMeshRecomputeNormalsGoldenSelfTest(bool); }// mesh_modify_golden.cpp (mesh modify: RecomputeNormals face-cross, flat + R-2 resident)
namespace sw { int runMeshTransformUvsGoldenSelfTest(bool); }    // mesh_modify_golden.cpp (mesh modify: TransformMeshUVs matrix·uv, flat + R-2 resident)
namespace sw { int runMeshSphereGoldenSelfTest(bool); }          // mesh_sphere_golden.cpp (mesh generate: SphereMesh UV-sphere, closed-form poles+equator)
namespace sw { int runMeshTorusGoldenSelfTest(bool); }           // mesh_torus_golden.cpp (mesh generate: TorusMesh tube×radius rings)
namespace sw { int runMeshCylinderGoldenSelfTest(bool); }        // mesh_cylinder_golden.cpp (mesh generate: CylinderMesh hull, closed-form)
namespace sw { int runMeshCubeGoldenSelfTest(bool); }            // mesh_cube_golden.cpp (mesh generate: CubeMesh 6-side, front-face closed-form)
namespace sw { int runMeshIcosahedronGoldenSelfTest(bool); }     // mesh_icosahedron_golden.cpp (mesh generate: IcosahedronMesh golden-ratio base, unit-sphere invariant)
namespace sw { int runFloatListSelfTest(bool); }                 // floatlist_golden.cpp (5th cook flow: FloatsToList host list)
namespace sw { int runColorsToListSelfTest(bool); }              // colorlist_golden.cpp (vec4-list cook flow: ColorsToList host color list, flat + R-2 resident)
namespace sw { int runColorListSelfTest(bool); }                // colorlist_fanout_golden.cpp (ColorList identity passthrough, flat + R-2 resident)
namespace sw { int runCombineColorListsSelfTest(bool); }        // colorlist_fanout_golden.cpp (CombineColorLists MultiInput concat, flat + R-2 resident)
namespace sw { int runReadPointColorsSelfTest(bool); }          // colorlist_fanout_golden.cpp (ReadPointColors: Points bag .Color -> ColorList, flat)
namespace sw { int runKeepColorsSelfTest(bool); }               // keepcolors_golden.cpp (per-node cross-frame colorlist STATE: KeepColors accumulate/cap/reset, flat + R-2 resident)
namespace sw { int runStringRailSelfTest(bool); }                // string_rail_golden.cpp (6th cook flow: String value rail)
namespace sw { int runHasStringChangedSelfTest(bool); }          // hasstringchanged_golden.cpp (per-node cross-frame STRING state: HasStringChanged delta, flat + R-2 resident)
namespace sw { int runListRoutingSelfTest(bool); }               // list_routing_golden.cpp (FloatList→Float bridge: downstream evalFloat)
namespace sw { int runPointListSelfTest(bool); }                 // pointlist_golden.cpp (7th cook flow: CPU point list + ListToBuffer bridge)
namespace sw { int runPointsToCpuSelfTest(bool); }               // pointstocpu_golden.cpp (PointsToCPU: GPU Points bag -> host List<Point>, flat)
namespace sw { int runGradientSelfTest(bool); }                  // gradient_golden.cpp (8th cook flow: SwGradient::sample byte-vs-TiXL)
namespace sw { int runPickGradientSelfTest(bool); }              // gradient_ops_pickgradient.cpp (MultiInput Gradient select)
namespace sw { int runBlendGradientsSelfTest(bool); }            // gradient_ops_blendgradients.cpp (2-gradient cross-merge)
namespace sw { int runLayerComposeSelfTest(bool); }              // point_ops_layercompose.cpp (★S2c: layer-compose end-to-end — 2 Layer2d → Execute → RenderTarget, blend order=wire order, flat + resident)
namespace sw { int runGroupSelfTest(bool); }                     // point_ops_group.cpp (★S2b: Group SRT transform-context push — Layer2d→Group(translate/scale)→RenderTarget, child quad moves on flat + resident)
namespace sw { int runTransformOpsSelfTest(bool); }              // point_ops_transform_golden.cpp (★S2 island: RotateAroundAxis/Shear/Transform transform-context push over Group — child quad rotates/shears/translates on flat + resident)
