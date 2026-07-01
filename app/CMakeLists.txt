cmake_minimum_required(VERSION 3.21)
project(simple_world LANGUAGES CXX OBJCXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Debug)
endif()

# metal-cpp-discipline Rule 4: develop with sanitizers ON. Toggle off only with
# -DSW_SANITIZE=OFF. The Metal validation layer is enabled at *runtime* via env
# (MTL_DEBUG_LAYER / MTL_SHADER_VALIDATION), see run-dev.sh.
option(SW_SANITIZE "Build with Address+UB sanitizers" ON)

set(TP "${CMAKE_CURRENT_SOURCE_DIR}/third_party")
set(METAL_CPP_DIR "${TP}/metal-cpp")
set(METAL_CPP_EXT_DIR "${TP}/metal-cpp-extensions")
set(IMGUI_DIR "${TP}/imgui")
set(NODE_EDITOR_DIR "${TP}/imgui-node-editor")
set(NFD_DIR "${TP}/nativefiledialog-extended")
add_subdirectory(${NFD_DIR})

# ---- Dear ImGui core + Metal(C++) / OSX backends -----------------------------
# imgui_impl_metal.mm / imgui_impl_osx.mm are written for manual reference
# counting (explicit [x release]/autorelease) -> compile WITHOUT ARC. The
# __bridge_retained/_transfer casts they use are valid under MRC too.
set(IMGUI_OBJCXX
  ${IMGUI_DIR}/backends/imgui_impl_metal.mm
  ${IMGUI_DIR}/backends/imgui_impl_osx.mm
)
set_source_files_properties(${IMGUI_OBJCXX} PROPERTIES COMPILE_OPTIONS -fno-objc-arc)

# ---- Metal shaders -> shaders.metallib --------------------------------------
# Precompiled (not runtime-compiled) so each .metal can #include the shared
# headers (tixl_point.h, particle_params.h, shared/*.metal.h) and the compiler
# proves host<->GPU layout (discipline Rule 1). TiXL-ported particle pipeline.
set(SW_METALLIB "${CMAKE_CURRENT_BINARY_DIR}/shaders.metallib")
# Glob every shaders/*.metal (excludes shaders/shared/*.metal.h — those are #include-only
# headers, not top-level .metal). Adding an op's .metal needs no CMake edit; reconfigure picks
# it up. SW_SHADERS holds basenames (NAME_WE) for the per-file compile foreach below.
file(GLOB SW_SHADER_FILES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/shaders/*.metal")
set(SW_SHADERS "")
foreach(_mf ${SW_SHADER_FILES})
  get_filename_component(_mn ${_mf} NAME_WE)
  list(APPEND SW_SHADERS ${_mn})
endforeach()
set(SW_AIRS "")
set(SW_SHADER_DEPS
  src/runtime/tixl_point.h src/runtime/particle_params.h src/runtime/blur_params.h src/runtime/displace_params.h src/runtime/draw_params.h
  src/runtime/addnoise_params.h src/runtime/filterpoints_params.h
  src/runtime/polartransformpoints_params.h src/runtime/wrappoints_params.h src/runtime/boundpoints_params.h src/runtime/transformsomepoints_params.h
  src/runtime/wrappointposition_params.h src/runtime/snaptogrid_params.h src/runtime/hexgridpoints_params.h
  src/runtime/clearsomepoints_params.h src/runtime/doylespiralpoints_params.h src/runtime/snaptopoints_params.h
  src/runtime/tint_params.h src/runtime/chromab_params.h src/runtime/adjustcolors_params.h src/runtime/channelmixer_params.h src/runtime/tonemapping_params.h
  src/runtime/pixelate_params.h src/runtime/sharpen_params.h
  src/runtime/reorientlinepoints_params.h src/runtime/selectpoints_params.h src/runtime/softtransformpoints_params.h
  src/runtime/detectedges_params.h src/runtime/chromaticdistortion_params.h src/runtime/voronoicells_params.h
  src/runtime/dither_params.h src/runtime/normalmap_params.h src/runtime/chromakey_params.h
  src/runtime/grain_params.h
  src/runtime/polarcoordinates_params.h src/runtime/edgerepeat_params.h
  src/runtime/offsetpoints_params.h src/runtime/pointattributefromnoise_params.h
  src/runtime/mesh_draw_params.h src/runtime/sw_mesh.h src/runtime/meshverticestopoints_params.h
  src/runtime/pointsonmesh_params.h src/runtime/findclosestpointsonmesh_params.h
  src/runtime/samplepointattributes_params.h src/runtime/displacepoints2d_params.h src/runtime/transformwithimage_params.h
  src/runtime/computeshaderstage_params.h
  shaders/shared/noise.metal.h shaders/shared/quat.metal.h shaders/shared/hash.metal.h)
set(SW_COMPILE_CMDS "")
foreach(s ${SW_SHADERS})
  set(_air ${CMAKE_CURRENT_BINARY_DIR}/${s}.air)
  list(APPEND SW_AIRS ${_air})
  list(APPEND SW_SHADER_DEPS shaders/${s}.metal)
  set(SW_COMPILE_CMDS ${SW_COMPILE_CMDS}
    COMMAND xcrun -sdk macosx metal -c ${CMAKE_CURRENT_SOURCE_DIR}/shaders/${s}.metal
            -I ${CMAKE_CURRENT_SOURCE_DIR}/src/runtime
            -I ${CMAKE_CURRENT_SOURCE_DIR}/shaders
            -o ${_air})
endforeach()
add_custom_command(
  OUTPUT ${SW_METALLIB}
  ${SW_COMPILE_CMDS}
  COMMAND xcrun -sdk macosx metallib ${SW_AIRS} -o ${SW_METALLIB}
  DEPENDS ${SW_SHADER_DEPS}
  COMMENT "Compiling Metal shaders -> shaders.metallib"
  VERBATIM)
add_custom_target(sw_shaders DEPENDS ${SW_METALLIB})

# eye.mm (ObjC++): PNG encode + AppKit window geometry for the agent's eye.
# Manual ref-counting alongside metal-cpp pointers -> compile WITHOUT ARC, like
# the imgui backends.
set_source_files_properties(src/verify/eye/eye.mm PROPERTIES COMPILE_OPTIONS -fno-objc-arc)
set_source_files_properties(src/verify/eye/eye_selftest.mm PROPERTIES COMPILE_OPTIONS -fno-objc-arc)
set_source_files_properties(src/platform/dialogs.mm PROPERTIES COMPILE_OPTIONS -fno-objc-arc)
# image_decode{,_selftest}.mm: ImageIO/CoreGraphics CFTypeRefs (manual CFRelease) alongside
# metal-cpp pointers (manual release) -> compile WITHOUT ARC, like eye.mm / dialogs.mm.
set_source_files_properties(src/platform/image_decode.mm PROPERTIES COMPILE_OPTIONS -fno-objc-arc)
set_source_files_properties(src/platform/image_decode_selftest.mm PROPERTIES COMPILE_OPTIONS -fno-objc-arc)
# image_save.mm: ImageIO/CoreGraphics CFTypeRefs (manual CFRelease) + metal-cpp getBytes, same
# manual-lifetime convention as image_decode.mm / eye.mm -> compile WITHOUT ARC.
set_source_files_properties(src/platform/image_save.mm PROPERTIES COMPILE_OPTIONS -fno-objc-arc)
# metal_compile{,_selftest}.mm: the one newLibrary(source) site + its proof. Manual metal-cpp
# lifetime (NS::String/CompileOptions/Library/Function) alongside no ARC, like the .mm above.
set_source_files_properties(src/platform/metal_compile.mm PROPERTIES COMPILE_OPTIONS -fno-objc-arc)
set_source_files_properties(src/platform/metal_compile_selftest.mm PROPERTIES COMPILE_OPTIONS -fno-objc-arc)
# audio_capture.mm / audio_playback.mm use AVFoundation — ARC keeps the ObjC lifetime simple
# (they touch no metal-cpp pointers, so the MRC convention of the other .mm files doesn't apply).
set_source_files_properties(src/platform/audio_capture.mm PROPERTIES COMPILE_OPTIONS -fobjc-arc)
set_source_files_properties(src/platform/audio_playback.mm PROPERTIES COMPILE_OPTIONS -fobjc-arc)
# midi_loopback.mm (L5 IO lane): pure CoreMIDI C API (no ObjC objects) -> compile WITHOUT ARC, like
# the other manual-lifetime .mm files. osc_loopback.cpp is plain C++ (POSIX sockets) — no .mm prop.
set_source_files_properties(src/platform/midi_loopback.mm PROPERTIES COMPILE_OPTIONS -fno-objc-arc)
# window_mode.mm: NSWindow toggleFullScreen: (AppKit) — manual retain like dialogs.mm.
set_source_files_properties(src/platform/window_mode.mm PROPERTIES COMPILE_OPTIONS -fno-objc-arc)

# Point-op leaf sources — globbed so a family lane adding point_ops_<name>.cpp (or a new
# point_ops_register_<family>.cpp) needs no CMake edit (just reconfigure). Covers the central
# point_ops.cpp, point_ops_selftest.cpp, every per-op leaf, and the per-family registrars.
file(GLOB SW_POINT_OP_SRCS CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/runtime/point_ops*.cpp")

# Value-op leaf sources — globbed so a value-op lane adding value_op_<name>.cpp needs no CMake edit
# (just reconfigure). Covers value_op_registry.cpp (the two sinks + ValueOp ctor) and every
# stateless value-op leaf (value_op_sin.cpp / value_op_pickfloat.cpp / …). Mirror of the
# SW_POINT_OP_SRCS glob (image-filter self-registration precedent, line above).
file(GLOB SW_VALUE_OP_SRCS CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/runtime/value_op*.cpp")

# Stateful value-op leaf sources — globbed so adding stateful_value_ops_<family>.cpp (a Damp/Spring/
# Ease/... leaf) needs no CMake edit (just reconfigure). Covers stateful_value_op_registry.cpp (the
# self-registration sink + the public cook/predicate API) + every per-subfamily leaf + the selftest.
# Mirror of the SW_VALUE_OP_SRCS glob above (replaced the old explicit stateful_value_ops.cpp line).
file(GLOB SW_STATEFUL_OP_SRCS CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/runtime/stateful_value_op*.cpp")

# Point-modify leaf sources — globbed so a point-modify lane adding
# node_registry_point_modify_<subfamily>.cpp needs no CMake edit (just reconfigure). Covers the
# self-registration registry (point_modify_op_registry.cpp = the sink + PointModifyOp ctor) and every
# per-subfamily NodeSpec leaf (node_registry_point_modify_transform.cpp / _noise / …). Mirror of the
# SW_VALUE_OP_SRCS glob (image-filter / value-op self-registration precedent).
file(GLOB SW_POINT_MODIFY_SRCS CONFIGURE_DEPENDS
  "${CMAKE_CURRENT_SOURCE_DIR}/src/runtime/point_modify_op_registry.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/src/runtime/node_registry_point_modify_*.cpp")

# Math/value leaf sources — globbed so a math lane adding node_registry_math_<subfamily>.cpp needs no
# CMake edit (just reconfigure). Covers the self-registration registry (math_op_registry.cpp = the sink
# + MathOp ctor) and every per-subfamily NodeSpec leaf (node_registry_math_arithmetic.cpp / _vector /
# _logic / …). Mirror of the SW_POINT_MODIFY_SRCS glob (point-modify / value-op self-registration
# precedent — replaced the old explicit node_registry_math.cpp line).
file(GLOB SW_MATH_SRCS CONFIGURE_DEPENDS
  "${CMAKE_CURRENT_SOURCE_DIR}/src/runtime/math_op_registry.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/src/runtime/node_registry_math_*.cpp")

# Selftest router + its per-AREA manifest leaves — globbed so adding selftests_<area>.cpp (or a row to
# an existing one) needs no CMake edit (just reconfigure). Covers the thin router (src/selftests.cpp)
# and every area manifest (src/selftests_core.cpp / _field / _point / …) that self-registers its rows
# into selftestRegistry(). selftest_registry.cpp (the sink + registerSelftests() ctor) is added with
# the runtime registry globs below. Mirror of the SW_MATH_SRCS / SW_POINT_MODIFY_SRCS self-registration
# globs (replaced the old explicit src/selftests.cpp line in add_executable).
file(GLOB SW_SELFTEST_SRCS CONFIGURE_DEPENDS
  "${CMAKE_CURRENT_SOURCE_DIR}/src/selftests*.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/src/runtime/selftest_registry.cpp")

add_executable(simple_world
  src/main.cpp
  src/app_delegate.cpp
  ${SW_SELFTEST_SRCS}  # globbed: selftests.cpp router + selftests_<area>.cpp manifests + selftest_registry.cpp
  src/app/document.cpp
  src/app/document_navigation.cpp  # composition-path nav + combine + resident paths (split from document.cpp)
  src/app/document_io.cpp          # new/open/save lifecycle + dirty tracking + window title + selftest (split from document.cpp)
  src/app/snapshot.cpp  # out-snapshot-png: product Output→PNG action (path policy + TiXL timestamp)
  src/app/audio_settings.cpp
  src/app/keymap_prefs.cpp           # #11: user keymap JSON override store (cross-session rebinding)
  src/app/keymap_prefs_selftest.cpp  # #11: set→save→reload round-trip gate (--selftest-keymap-persist)
  src/app/user_settings.cpp          # #12: editor user-settings store (recent-files MRU, cross-session memory)
  src/app/user_settings_selftest.cpp # #12: push→save→reload round-trip gate (--selftest-user-settings)
  src/app/output_window_state.cpp           # out-window-persistence: Output view-state store (pin/res/bg) + per-project sidecar
  src/app/output_window_state_selftest.cpp  # out-window-persistence: save→reload round-trip gate (--selftest-output-window-state)
  src/app/theme_registry.cpp                # color-theme-system: named-theme registry + per-theme JSON persistence (~/.simple_world/Themes)
  src/app/theme_registry_selftest.cpp       # color-theme-system: construct→save→reload bit-for-bit gate (--selftest-theme-roundtrip)
  src/app/audio_monitor.cpp
  src/app/command.cpp
  src/app/graph_commands.cpp
  src/app/graph_commands_selftest.cpp
  src/app/variation_apply.cpp  # Lane L1 (Variation): document-override bridge + --selftest-variation-apply
  src/app/variation_live.cpp   # Lane L1 (Variation): P1 LIVE PIPE — crossfader frame tick + writeback + --selftest-variation-live
  src/app/variation_panel.cpp  # Lane L1 (Variation): P2 PANEL — snapshot pool + N-way mix + full crossfader
  src/app/variation_panel_selftest.cpp  # L1 (Variation): P2 panel-wiring golden (--selftest-variation-panel)
  src/app/variation_mix_selftest.cpp        # L1 (Variation): N-way per-type Mix golden (--selftest-variation-mix)
  src/app/variation_pool_json_selftest.cpp  # L1 (Variation): JSON pool round-trip golden (--selftest-variation-pool-json)
  src/app/midi_bind.cpp        # Lane L5->cook (P3): live MIDI/OSC binding registerIoLiveSources + cook-side tick + MIDI learn
  src/app/midi_bind_selftest.cpp  # L5 (P3): live-control pipe loopback golden (--selftest-midi-bind)
  src/app/animation_commands.cpp
  src/app/animation_commands_selftest.cpp
  src/app/animation_commands_selftest_s6.cpp
  src/app/rename_commands.cpp
  src/app/child_state_commands.cpp
  src/app/annotation_commands.cpp
  src/app/annotation_selftest.cpp
  src/app/def_removal_selftest.cpp
  src/app/copy_paste_selftest.cpp
  src/app/rename_selftest.cpp
  src/app/child_state_selftest.cpp
  src/app/auto_backup.cpp           # L6 維運: periodic project backup
  src/app/auto_backup_selftest.cpp  # L6 維運: round-trip golden (--selftest-auto-backup)
  src/app/auto_backup_restore.cpp   # L6 維運: retention thinning + crash-recovery restore
  src/app/auto_backup_restore_selftest.cpp  # L6 維運: retention+restore golden (--selftest-backup-restore)
  src/app/asset_ref_roundtrip_selftest.cpp  # L3 檔案: image-asset-reference round-trip golden (--selftest-asset-ref)
  src/app/asset_index.cpp           # L3 檔案: asset-dependency index (deduped referenced keys + missing predicate)
  src/app/asset_index_selftest.cpp  # L3 檔案: asset-index golden (--selftest-asset-index)
  src/app/asset_relink.cpp          # L3 檔案: project-wide asset relink mutation (rewrite key→key, count refs)
  src/app/asset_relink_selftest.cpp # L3 檔案: asset-relink golden (--selftest-asset-relink)
  src/app/asset_library.cpp          # L3 檔案: available-asset enumeration + click-to-create LoadImage payload (EXP #2)
  src/app/asset_library_selftest.cpp # L3 檔案: asset-library golden (--selftest-asset-library, enum + undoable create-op)
  src/app/menu.cpp
  src/app/view_menu_actions.cpp  # View-menu leaf-inversion seam storage (native NSMenu ↔ UI toggles)
  src/ui/cjk_font.cpp
  src/ui/canvas_ids.cpp
  src/ui/fence_preview.cpp
  src/ui/editor_ui.cpp
  src/ui/editor_ui_grid.cpp         # canvas background grid (split from editor_ui.cpp)
  src/ui/editor_ui_canvas_edit.cpp  # keyboard + delete macro (split from editor_ui.cpp)
  src/ui/editor_ui_layout.cpp       # canvas position sync + drag (split from editor_ui.cpp)
  src/ui/toolbar.cpp
  src/ui/view_menu_register.cpp  # UI side of the native View-menu seam (registers toggles at startup)
  src/ui/view_menu_state_selftest.cpp  # View-menu live check-mark data path gate (--selftest-view-menu-state)
  src/ui/combine_dialog.cpp
  src/ui/connection_ops.cpp        # shared wire-edit (drag + connect/disconnect verbs) + verb hooks
  src/ui/connect_verb_selftest.cpp # connect/disconnect verb golden (--selftest-hand-connect, headless)
  src/ui/graph_dump.cpp            # req_graph -> graph.json serializer of the current compound (eye hook)
  src/ui/graph_dump_selftest.cpp   # graphdump golden (--selftest-graphdump, headless)
  src/ui/node_menu_actions.cpp
  src/ui/copy_paste_ui.cpp
  src/ui/keymap.cpp
  src/ui/keymap_binding.cpp  # #11: user-override-aware chord test (rebinding seam, keymap.cpp at cap)
  src/ui/view_modes.cpp  # P6 演出/Focus modes (F12 / Shift+Esc toggles + handlers)
  src/ui/quick_add.cpp
  src/ui/quick_add_tree.cpp
  src/ui/quick_add_selftest.cpp
  src/ui/inspector.cpp
  src/ui/slider_ladder.cpp  # SliderLadder precision value-edit overlay (port of TiXL SliderLadder.cs)
  src/ui/perf_overlay.cpp  # B-track gap perf-observability: FrameTime P99 grader + mini rolling graph (F10)
  src/ui/gradient_widget.cpp  # gradient color-band editor widget (port of TiXL GradientEditor.cs)
  src/ui/asset_browser.cpp  # EXP #2: AssetLibrary window (browse Lib: assets + click-to-create LoadImage)
  src/ui/inspector_param_menu.cpp
  src/ui/timeline_window.cpp
  src/ui/timeline_dopesheet.cpp
  src/ui/timeline_curve_editor.cpp
  src/ui/timeline_select.cpp
  src/ui/timeline_edit.cpp
  src/ui/timeline_canvas.cpp
  src/ui/timeline_raster.cpp
  src/ui/timeline_selftest.cpp
  src/ui/timeline_selftest_b9.cpp
  src/ui/node_draw.cpp
  src/ui/node_draw_selftest.cpp  # --selftest-nodeval: body value-string format + zoom-gating thresholds
  src/ui/node_faces.cpp
  src/ui/annotation_draw.cpp
  src/ui/annotation_interact.cpp
  src/ui/node_style.cpp
  src/ui/theme.cpp
  src/ui/theme_editor.cpp     # color-theme-system: Color Theme Editor window (dropdown + name/author + per-field color edits)
  src/ui/output_window.cpp
  src/ui/output_window_canvas.cpp      # aspect-correct image canvas (split from output_window.cpp)
  src/ui/output_window_resolution.cpp  # Output resolution selector (split from output_window.cpp)
  src/ui/output_window_persist.cpp     # out-window-persistence: capture/restore view state <-> app store
  src/ui/variation_panel.cpp  # Lane L1 (Variation): P2 Variation window (snapshot grid + N-way mix + crossfader)
  src/metal_impl.cpp
  src/verify/eye/eye.mm
  src/verify/eye/eye_selftest.mm
  src/verify/hand/hand.cpp
  src/verify/hand/hand_selftest.cpp
  src/platform/dialogs.mm
  src/platform/audio_capture.mm
  src/platform/audio_playback.mm
  src/platform/audio_devices.mm
  src/platform/image_decode.mm
  src/platform/image_decode_selftest.mm
  src/platform/image_save.mm  # out-snapshot-png: product texture→PNG writer (ImageIO, byte-exact)
  src/platform/metal_compile.mm
  src/platform/metal_compile_selftest.mm
  src/platform/osc_loopback.cpp
  src/platform/midi_loopback.mm
  src/platform/window_mode.mm
  src/platform/menu_appkit_ext.cpp  # tracked AppKit NSMenu extension (live check marks + menuNeedsUpdate: delegate) — replaces the gitignored vendored metal-cpp edits
  src/platform/live_binding.cpp
  src/runtime/beat_timing.cpp        # tap BPM engine (port of TiXL BeatTiming.cs)
  src/runtime/cmd_view_background.cpp  # Output-window view background ambient (TiXL EvaluationContext.BackgroundColor)
  src/runtime/particle_system.cpp
  src/runtime/particle_decay_selftest.cpp
  src/runtime/field_graph.cpp
  src/runtime/field_graph_selftest.cpp
  src/runtime/field_camera.cpp
  src/runtime/field_camera_selftest.cpp
  src/runtime/field_render.cpp
  src/runtime/field_node_registry.cpp
  src/runtime/field_graph_builder.cpp
  src/runtime/field_ops_raymarchfield.cpp
  src/runtime/field_ops_spheresdf.cpp
  src/runtime/field_ops_boxsdf.cpp
  src/runtime/field_ops_boxframesdf.cpp
  src/runtime/field_ops_octahedronsdf.cpp
  src/runtime/field_ops_capsulelinesdf.cpp
  src/runtime/field_ops_chainlinksdf.cpp
  src/runtime/field_ops_torussdf.cpp
  src/runtime/field_ops_cylindersdf.cpp
  src/runtime/field_ops_planesdf.cpp
  src/runtime/field_ops_cappedtorussdf.cpp
  src/runtime/field_ops_prismsdf.cpp
  src/runtime/field_ops_pyramidsdf.cpp
  src/runtime/field_ops_rotatedplanesdf.cpp
  src/runtime/field_ops_combinesdf.cpp
  src/runtime/field_ops_combinesdf_slots.cpp
  src/runtime/field_ops_invertsdf.cpp
  src/runtime/field_ops_absolutesdf.cpp
  src/runtime/field_ops_translate.cpp
  src/runtime/field_ops_repeatfield3.cpp
  src/runtime/field_ops_repeataxis.cpp
  src/runtime/field_ops_reflectfield.cpp
  src/runtime/field_ops_bendfield.cpp
  src/runtime/field_ops_combinefieldcolor.cpp
  src/runtime/field_ops_raster3dfield.cpp
  src/runtime/field_ops_rotateaxis.cpp
  src/runtime/field_ops_rotatefield.cpp
  src/runtime/field_ops_twistfield.cpp
  src/runtime/field_ops_repeatfieldlimit.cpp
  src/runtime/field_ops_fractalsdf.cpp
  src/runtime/field_ops_customsdf.cpp
  src/runtime/field_ops_image2dsdf.cpp
  src/runtime/field_ops_repeatpolar.cpp
  src/runtime/field_ops_translateuv.cpp
  src/runtime/field_ops_staircombinesdf.cpp
  src/runtime/field_ops_staircombinesdf_slots.cpp
  src/runtime/field_ops_noisedisplacesdf.cpp
  src/runtime/field_ops_spatialdisplacesdf.cpp
  src/runtime/field_ops_transformfield.cpp
  src/runtime/field_ops_pushpullsdf.cpp
  src/runtime/field_ops_blendsdfwithsdf.cpp
  src/runtime/field_ops_toroidalvortexfield.cpp
  src/runtime/field_ops_setsdfmaterial.cpp
  src/field_render_golden.cpp
  src/field_raymarch_golden.cpp
  src/field_raymarch_output_golden.cpp
  src/connect_cooks_golden.cpp  # connect VERB → production cook → sphere silhouette (--selftest-connect-cooks)
  src/particlefield_probe_golden.cpp
  src/vectorfieldforce_field_golden.cpp
  src/fielddistanceforce_field_golden.cpp
  src/randomjumpforce_field_golden.cpp
  src/fieldvolumeforce_field_golden.cpp
  src/fieldtree_builder_golden.cpp
  src/movepointstosdf_golden.cpp  # SDF point-modify: GridPoints -> MoveToSDF(Field=SphereSDF) -> on-sphere readback (--selftest-movepointstosdf)
  src/sdfreflectionlinepoints_golden.cpp  # SDF point-modify + count-multiply: LinePoints -> SdfReflectionLinePoints(Field=SphereSDF) -> per-line polyline readback (--selftest-sdfreflectionlinepoints)
  src/raymarchpoints_golden.cpp  # SDF point-modify + count-multiply (TWO modes): LinePoints -> RaymarchPoints(Field=SphereSDF) -> MODE0 line[1] / MODE1 last-step on-sphere readback (--selftest-raymarchpoints)
  src/pointcolorwithfield_golden.cpp  # direct-Field gather LEAF: GridPoints -> SetPointAttributes(seed) -> PointColorWithField(Field=SphereSDF) -> recolored-white readback (--selftest-pointcolorwithfield)
  src/selectpointswithsdf_golden.cpp  # direct-Field gather LEAF: GridPoints -> SelectPointsWithSDF(Field=SphereSDF) -> per-point FX1=max(0,1-2*dist) readback (--selftest-selectpointswithsdf)
  src/radial_points_parity_golden.cpp  # ★PARITY-GATE Stage-1 pilot: RadialPoints production defaults vs TiXL .t3 Count=100/Radius=1 (--selftest-radial-parity, RED-first template)
  src/grid_points_parity_golden.cpp    # ★PARITY-GATE param-completion fan-out #2: GridPoints attribute knobs (Color/F1/F2/Orientation/Scale) wired vs TiXL .t3/.hlsl (--selftest-grid-parity, RED-first)
  src/hexgrid_points_parity_golden.cpp # ★PARITY-GATE param-completion fan-out: HexGridPoints 11th input Scale (Size·Scale via .t3 ScaleVector3) cook-through vs TiXL .hlsl closed-form X (--selftest-hexgrid-parity, RED-first)
  src/line_points_parity_golden.cpp    # ★PARITY-GATE param-completion fan-out #3: LinePoints knobs (ColorA/ColorB dual-color lerp/F1/F2/Orientation/Twist/AddSeparator) wired vs TiXL .t3/.hlsl (--selftest-line-parity, RED-first)
  src/turbulence_parity_golden.cpp     # ★PARITY-GATE Stage-1: TurbulenceForce 15× amplitude tooth + Phase=wall-clock determinism probe (--selftest-turbulence-parity, RED-first)
  src/directional_force_parity_golden.cpp  # ★PARITY-GATE Stage-3 Force-class first: DirectionalForce NodeSpec-default vs TiXL .t3 Amount=0.007 cook-through + closed-form kernel-vel (--selftest-directionalforce-parity, 补验证闸 already-faithful)
  src/axisstep_force_parity_golden.cpp  # Stage-3 Force: AxisStepForce CREATE-vel cook-through 守 NodeSpec SelectRatio=0.1 (--selftest-axisstep-parity)
  src/vectorfield_force_parity_golden.cpp  # Stage-3 Force: VectorFieldForce CREATE-vel no-field baked cook-through (--selftest-vectorfieldforce-parity)
  src/snaptoangles_force_parity_golden.cpp  # Stage-3 Force: SnapToAnglesForce TRANSFORM cook no-op + direct-kernel NodeSpec discrim (--selftest-snaptoanglesforce-parity)
  src/fielddistance_force_parity_golden.cpp  # Stage-3 Force: FieldDistanceForce TRANSFORM baked no-field no-op 契約 (--selftest-fielddistanceforce-parity)
  src/fieldvolume_force_parity_golden.cpp  # Stage-3 Force: FieldVolumeForce TRANSFORM baked no-field no-op 契約 (--selftest-fieldvolumeforce-parity)
  src/drawpoints_parity_golden.cpp  # ★PARITY-GATE 修偏差: DrawPoints v1 退化4px死點 → DrawKind::Points2 quad-sprite + PointSize/Color cook-through (--selftest-drawpoints-parity, RED-first PointSize+tint 雙牙)
  src/field_paramapply_golden.cpp
  src/field_paramapply_cases.cpp
  src/field_paramapply_enumcases.cpp
  src/field_ops_boxsdf_golden.cpp
  src/field_ops_boxframesdf_golden.cpp
  src/field_ops_octahedronsdf_golden.cpp
  src/field_ops_capsulelinesdf_golden.cpp
  src/field_ops_chainlinksdf_golden.cpp
  src/field_ops_torussdf_golden.cpp
  src/field_ops_cylindersdf_golden.cpp
  src/field_ops_planesdf_golden.cpp
  src/field_ops_cappedtorussdf_golden.cpp
  src/field_ops_prismsdf_golden.cpp
  src/field_ops_pyramidsdf_golden.cpp
  src/field_ops_rotatedplanesdf_golden.cpp
  src/field_ops_combinesdf_golden.cpp
  src/field_ops_invertsdf_golden.cpp
  src/field_ops_absolutesdf_golden.cpp
  src/field_ops_translate_golden.cpp
  src/field_ops_repeatfield3_golden.cpp
  src/field_ops_repeataxis_golden.cpp
  src/field_ops_reflectfield_golden.cpp
  src/field_ops_bendfield_golden.cpp
  src/field_ops_combinefieldcolor_golden.cpp
  src/field_ops_rotateaxis_golden.cpp
  src/field_ops_rotatefield_golden.cpp
  src/field_ops_twistfield_golden.cpp
  src/field_ops_repeatfieldlimit_golden.cpp
  src/field_ops_fractalsdf_golden.cpp
  src/field_ops_customsdf_golden.cpp
  src/field_ops_image2dsdf_golden.cpp
  src/field_ops_repeatpolar_golden.cpp
  src/field_ops_translateuv_golden.cpp
  src/field_ops_staircombinesdf_golden.cpp
  src/field_ops_noisedisplacesdf_golden.cpp
  src/field_ops_spatialdisplacesdf_golden.cpp
  src/field_ops_transformfield_golden.cpp
  src/field_ops_pushpullsdf_golden.cpp
  src/field_ops_blendsdfwithsdf_golden.cpp
  src/field_ops_toroidalvortexfield_golden.cpp
  src/field_ops_setsdfmaterial_golden.cpp
  src/field_ops_raster3dfield_golden.cpp
  src/runtime/mesh_op_registry.cpp
  src/runtime/mesh_ops_ngonmesh.cpp
  src/runtime/mesh_ops_quadmesh.cpp
  src/runtime/mesh_ops_transformmesh.cpp
  src/runtime/mesh_ops_combinemeshes.cpp
  src/runtime/mesh_ops_flipnormals.cpp
  src/runtime/mesh_ops_recomputenormals.cpp
  src/runtime/mesh_ops_transformmeshuvs.cpp
  src/runtime/mesh_ops_splitmeshvertices.cpp
  src/runtime/mesh_ops_selectvertices.cpp
  src/runtime/mesh_ops_deformmesh.cpp
  src/runtime/mesh_ops_collapsevertices.cpp
  src/runtime/mesh_ops_meshprojectuv.cpp
  src/runtime/mesh_ops_spheremesh.cpp
  src/runtime/mesh_ops_torusmesh.cpp
  src/runtime/mesh_ops_cylindermesh.cpp
  src/runtime/mesh_ops_cubemesh.cpp
  src/runtime/mesh_ops_icosahedronmesh.cpp
  src/runtime/mesh_ops_blendmeshvertices.cpp
  src/runtime/mesh_ops_pickmeshbuffer.cpp
  src/mesh_golden.cpp
  src/mesh_input_golden.cpp
  src/mesh_modify_golden.cpp
  src/mesh_modify2_golden.cpp
  src/mesh_blendpick_golden.cpp
  src/mesh_sphere_golden.cpp
  src/mesh_torus_golden.cpp
  src/mesh_cylinder_golden.cpp
  src/mesh_cube_golden.cpp
  src/mesh_cube_uv_golden.cpp
  src/mesh_icosahedron_golden.cpp
  src/mesh_icosahedron_uv_golden.cpp
  src/runtime/floatlist_op_registry.cpp
  src/runtime/floatlist_ops_floatstolist.cpp
  src/runtime/floatlist_ops_combinefloatlists.cpp
  src/runtime/floatlist_ops_intstolist.cpp
  src/runtime/floatlist_ops_setfloatlistvalue.cpp
  src/runtime/floatlist_ops_setintlistvalue.cpp
  src/runtime/floatlist_ops_remapfloatlist.cpp
  src/runtime/floatlist_ops_smoothvalues.cpp
  src/runtime/floatlist_ops_animfloatlist.cpp
  src/runtime/floatlist_ops_floatlisttointlist.cpp
  src/runtime/floatlist_ops_intlisttofloatlist.cpp
  src/runtime/floatlist_ops_amplifyvalues.cpp   # AmplifyValues (FIRST cross-frame STATEFUL FloatList op: damped-average follower)
  src/runtime/floatlist_ops_dampfloatlist.cpp   # DampFloatList (cross-frame STATEFUL: per-index damped follower + dt-gate, Method 0)
  src/runtime/floatlist_ops_keepfloatvalues.cpp # KeepFloatValues (cross-frame STATEFUL: front-insert ring-buffer accumulator)
  src/runtime/floatlist_ops_pickfloatlist.cpp   # PickFloatList (MultiInput list picker, T3 floor-Mod index; numbers/floats/logic/PickFloatList.cs)
  src/runtime/floatlist_ops_mergefloatlists.cpp # MergeFloatLists (Append/Htp/Average; StartIndices + Ltp/FailOver deferred)
  src/runtime/floatlist_ops_mergeintlists.cpp   # MergeIntLists (int twin; integer-Average; shares cookMergeListsShared)
  src/merge_lists_golden.cpp                     # --selftest-mergelists (Merge*Lists + PickFloatList chain-through-evalFloat)
  src/floatlist_golden.cpp
  src/floatlist_producers_golden.cpp
  src/floatlist_smoothvalues_golden.cpp
  src/floatlist_animfloatlist_golden.cpp
  src/floatlist_amplifyvalues_golden.cpp        # --selftest-amplifyvalues (cross-frame FloatList STATE: damp toward input over frames, flat + R-2 resident)
  src/floatlist_dampfloatlist_golden.cpp        # --selftest-dampfloatlist (cross-frame STATE: damp + dt-gate, flat + resident)
  src/floatlist_keepfloatvalues_golden.cpp      # --selftest-keepfloatvalues (cross-frame STATE: front-insert history ring, flat + resident)
  src/floatlist_conversion_golden.cpp
  src/runtime/colorlist_op_registry.cpp
  src/runtime/colorlist_ops_colorstolist.cpp
  src/runtime/colorlist_ops_colorlist.cpp
  src/runtime/colorlist_ops_combinecolorlists.cpp
  src/runtime/colorlist_ops_readpointcolors.cpp
  src/runtime/colorlist_ops_keepcolors.cpp
  src/colorlist_golden.cpp
  src/setbpm_golden.cpp             # --selftest-setbpm ([SetBpm] VJ op triggered-pull golden)
  src/setplayback_golden.cpp        # --selftest-setplayback ([SetPlaybackTime]/[SetPlaybackSpeed] op→PlaybackProvider→transport)
  src/bpm_transport_golden.cpp      # --selftest-bpmtransport (end-to-end DetectBpm auto → SetBpm → transport.bpm)
  src/colorlist_fanout_golden.cpp
  src/keepcolors_golden.cpp
  src/runtime/host_scalar_op_registry.cpp
  src/runtime/host_scalar_ops_floatlistlength.cpp
  src/runtime/host_scalar_ops_pickfloatfromlist.cpp
  src/runtime/host_scalar_ops_sumrange.cpp            # FloatList+Lower+Upper -> windowed sum (list fan-out wave-1)
  src/runtime/host_scalar_ops_intlistlength.cpp       # IntList (dissolved) -> count (list fan-out wave-1)
  src/runtime/host_scalar_ops_pickintfromlist.cpp     # IntList (dissolved)+Index -> picked element (list fan-out wave-1)
  src/runtime/host_scalar_ops_comparefloatlists.cpp   # ListA+ListB+Threshold -> differing-ratio (list fan-out wave-1)
  src/runtime/host_scalar_ops_tryparse.cpp            # String -> float (parse-or-Default; numbers/float/logic/TryParse.cs)
  src/runtime/host_scalar_ops_tryparseint.cpp         # String -> int (dissolved Float; numbers/int/process/TryParseInt.cs)
  src/tryparse_golden.cpp                              # --selftest-tryparse (String->Float/Int bridge: flat + resident)
  src/list_routing_golden.cpp
  src/list_routing_wave1_golden.cpp                    # --selftest-listroutingwave1 (wave-1 host-scalar consumers bridge golden)
  src/runtime/string_op_registry.cpp
  src/runtime/string_ops_stringlength.cpp
  src/runtime/string_ops_floattostring.cpp
  src/runtime/string_ops_datetimetostring.cpp  # DateTimeToString (route-B epoch Float + .NET format → string)
  src/runtime/string_ops_combinestrings.cpp
  src/runtime/string_ops_inttostring.cpp
  src/runtime/string_ops_vec3tostring.cpp
  src/runtime/string_ops_indexof.cpp
  src/runtime/string_ops_substring.cpp
  src/runtime/string_ops_changecase.cpp
  src/runtime/string_ops_stringrepeat.cpp
  src/runtime/string_ops_stringinsert.cpp
  src/runtime/string_ops_mockstrings.cpp
  src/runtime/string_ops_pickstring.cpp
  src/runtime/string_ops_searchandreplace.cpp
  src/runtime/string_ops_pickstringpart.cpp
  src/runtime/string_ops_filepathparts.cpp
  src/runtime/string_ops_floatlisttostring.cpp
  src/runtime/string_ops_joinstringlist.cpp
  src/runtime/string_ops_pickstringfromlist.cpp
  src/runtime/string_ops_stringbuildertostring.cpp
  src/runtime/string_ops_stringbuilder.cpp
  src/runtime/string_ops_blendstrings.cpp
  src/runtime/string_ops_hasstringchanged.cpp
  src/runtime/string_ops_buildrandomstring.cpp
  src/runtime/string_ops_buildrandomstring_wrap.cpp
  src/runtime/string_ops_wrapstring.cpp
  src/runtime/string_ops_stringctxvar.cpp
  src/runtime/string_ops_readfile.cpp
  src/runtime/string_ops_getattributefromjson.cpp
  src/runtime/stringlist_op_registry.cpp
  src/runtime/stringlist_ops_splitstring.cpp
  src/runtime/stringlist_ops_zipstringlist.cpp
  src/runtime/stringlist_ops_filesinfolder.cpp
  src/string_rail_golden.cpp
  src/string_rail_golden_subseama.cpp
  src/string_ops_getattributefromjson_golden.cpp
  src/string_builder_golden.cpp
  src/string_ops_wrapstring_golden.cpp
  src/string_ops_blendstrings_golden.cpp
  src/hasstringchanged_golden.cpp
  src/buildrandomstring_golden.cpp
  src/stringctxvar_golden.cpp
  src/runtime/variation_selftest.cpp  # Lane L1 (Variation/Snapshot) harness-first golden: Mix + SpringDamp math
  src/runtime/variation_snapshot_actions_selftest.cpp  # L1 (Variation): snapshot-slot state machine golden (--selftest-variation-snapshot-actions)
  src/runtime/pointlist_op_registry.cpp
  src/runtime/pointlist_ops_radialpointscpu.cpp
  src/runtime/pointlist_ops_transformcpupoint.cpp
  src/runtime/pointlist_ops_linepointscpu.cpp
  src/runtime/pointlist_ops_linearpointscpu.cpp
  src/runtime/pointlist_ops_repeatatpointscpu.cpp
  src/runtime/pointlist_ops_samplecpupoints.cpp
  src/runtime/pointlist_ops_joinlists.cpp
  src/runtime/pointlist_ops_listtobuffer.cpp
  src/runtime/pointlist_ops_pointstocpu.cpp
  src/runtime/obj_parse.cpp                    # device-IO OBJ sub-lane: pure-CPU .obj text parser (ObjMesh.cs)
  src/runtime/obj_parse_distinct.cpp           # distinct-vertex dedup + TBN (for the later LoadObj; split per rule 4)
  src/runtime/pointlist_ops_loadobjaspoints.cpp # LoadObjAsPoints leaf (first obj_parse consumer + String-path channel)
  src/runtime/pointlist_ops_loadobjedges.cpp   # LoadObjEdges leaf (2nd obj_parse consumer: unique face edges → from/to/sep triples)
  src/runtime/gizmo_geometry.cpp
  src/runtime/pointlist_ops_conegizmo.cpp
  src/runtime/pointlist_ops_drawlinegrid.cpp
  src/runtime/pointlist_ops_drawboxgizmo.cpp
  src/runtime/pointlist_ops_drawspheregizmo.cpp
  src/runtime/pointlist_ops_locator.cpp
  src/pointlist_golden.cpp
  src/conegizmo_golden.cpp
  src/drawlinegrid_golden.cpp
  src/drawboxgizmo_golden.cpp
  src/drawspheregizmo_golden.cpp
  src/locator_golden.cpp
  src/pointstocpu_golden.cpp
  src/samplecpupoints_golden.cpp
  src/joinlists_golden.cpp
  src/valuestotexture_golden.cpp
  # Seam-1 "Buffer" currency (TiXL BufferWithViews → one MTL::Buffer): the registry + marshal-op leaves.
  src/runtime/buffer_op_registry.cpp             # Buffer-op self-registration sinks + bufferParam accessor
  src/runtime/buffer_ops_floatstobuffer.cpp      # FloatsToBuffer (keystone: host floats → GPU buffer, matrix-then-float fill)
  src/runtime/buffer_ops_getbuffercomponents.cpp # GetBufferComponents (metadata read-out + Buffer passthrough)
  src/runtime/buffer_ops_executebufferupdate.cpp # ExecuteBufferUpdate (run Command, forward Buffer unchanged)
  src/runtime/buffer_ops_intstobuffer.cpp        # IntsToBuffer (host ints → GPU buffer, 16-byte-padded int32 slices)
  src/runtime/buffer_ops_getsrvproperties.cpp    # GetSRVProperties (Buffer passthrough + element-count read-out, thin GetBufferComponents sibling)
  src/runtime/buffer_ops_transformsconstbuffer.cpp # TransformsConstBuffer (640-byte 10-matrix camera const buffer, HLSL row-major transposed bytes)
  src/runtime/buffer_ops_computeshaderstage.cpp  # ComputeShaderStage (GENERIC compute stage: bind CB/SRV/UAV + dispatch a named MSL kernel)
  src/runtime/buffer_ops_structuredbufferwithviews.cpp # StructuredBufferWithViews (allocate an empty Stride*Count GPU buffer = the UAV write target)
  src/runtime/gradient_op_registry.cpp
  src/runtime/gradient_ops_definegradient.cpp
  src/runtime/gradient_ops_pickgradient.cpp
  src/runtime/gradient_ops_blendgradients.cpp
  src/runtime/gradient_ops_defineiqgradient.cpp
  src/gradient_golden.cpp
  src/keeppreviousframe_golden.cpp
  src/swaptextures_golden.cpp
  src/afterglow_golden.cpp
  src/afterglow2_golden.cpp
  src/advancedfeedback_golden.cpp
  src/runtime/graph.cpp
  src/runtime/node_registry.cpp
  src/runtime/node_registry_generators.cpp
  src/runtime/node_registry_generators_extra.cpp  # second half of the generator table (rule-4 split, param-completion fan-out)
  src/runtime/node_registry_attr_cluster.cpp  # shared per-point attribute NodeSpec cluster (param-completion fan-out; peeled for the ratchet)
  src/runtime/node_registry_point_combine.cpp
  src/runtime/node_registry_particle.cpp
  src/runtime/node_registry_draw.cpp
  src/runtime/node_registry_draw_transform.cpp  # render-island transform-context specs (peeled for the ratchet)
  src/runtime/node_registry_draw_renderstate.cpp  # Seam 2 render-state specs (Rasterizer/OutputMerger, peeled for the ratchet)
  ${SW_MATH_SRCS}  # globbed: math_op_registry.cpp + every per-subfamily math leaf
  src/runtime/value_eval_ops.cpp
  src/runtime/graph_selftest.cpp
  src/runtime/point_graph.cpp
  src/runtime/point_graph_command_cook.cpp
  src/runtime/point_graph_resident_command_cook.cpp
  src/runtime/point_graph_mesh_cook.cpp
  src/runtime/point_graph_tex_cook.cpp
  src/runtime/point_graph_hostvalue_cook.cpp
  src/runtime/point_graph_buffer_cook.cpp        # Seam-1: the flat "Buffer" cook (extracted from point_graph.cpp)
  src/runtime/point_graph_string_cook.cpp
  src/runtime/point_graph_hostscalar_cook.cpp
  src/runtime/point_graph_registry.cpp
  src/runtime/point_graph_params.cpp
  src/runtime/point_graph_debug.cpp
  src/runtime/point_graph_resident.cpp
  src/runtime/point_graph_resident_buffer.cpp    # Seam-1 WO-E: the resident "Buffer" cook (production leg; extracted from point_graph_resident.cpp)
  src/runtime/point_graph_resident_tex_cook.cpp
  src/runtime/point_graph_selftest.cpp
  src/runtime/resident_cook_selftest.cpp
  src/runtime/resident_cook_parity_selftest.cpp
  src/runtime/resident_crop_selftest.cpp
  src/runtime/resident_fastblur_selftest.cpp
  src/runtime/resident_rgbtv_selftest.cpp
  src/runtime/resident_distortandshade_selftest.cpp
  src/runtime/resident_combine3images_selftest.cpp
  src/runtime/resident_picktexture_selftest.cpp
  src/runtime/resident_firstvalidtexture_selftest.cpp
  src/runtime/resident_usefallbacktexture_selftest.cpp
  src/runtime/resident_combinematerialchannels2_selftest.cpp
  src/runtime/resident_combinematerialchannels_selftest.cpp
  src/runtime/resident_hse_selftest.cpp
  src/runtime/resident_mosiactiling_selftest.cpp
  src/runtime/bypass_selftest_shared.cpp
  src/runtime/bypass_cook_selftest.cpp
  src/app/bypass_compound_selftest.cpp
  src/runtime/transport.cpp
  src/runtime/transport_selftest.cpp
  src/runtime/curve.cpp
  src/runtime/curve_selftest.cpp
  src/runtime/curve_json.cpp
  src/runtime/curve_animator.cpp
  src/runtime/curve_animator_selftest.cpp
  src/runtime/curve_animator_selftest_vec.cpp
  src/runtime/compound_graph.cpp
  src/runtime/compound_graph_selftest.cpp
  src/runtime/t3_import.cpp                        # .t3 IMPORTER (keystone首證: forward-ported spike, 6-atom buffer map)
  src/runtime/t3import_transformpoints_golden.cpp  # KEYSTONE parity: TransformPoints.t3 import→buildEvalGraph→cookResident vs xfprobe
  src/runtime/output_slot_view_selftest.cpp  # output-slot picker golden (viewProducerPath startSlot)
  src/runtime/compound_save.cpp
  src/runtime/compound_load.cpp
  src/runtime/compound_folder.cpp          # ADDITIVE folder-package (.swpkg): one .t3 per symbol + metadata.json (reuses symbolToJsonObject + libFromJsonAny)
  src/runtime/compound_folder_selftest.cpp # --selftest-folder-package: round-trip + cross-format (.swproj==.swpkg) invariant + RED leg
  src/runtime/compound_save_selftest.cpp
  src/runtime/testproj_selftest.cpp
  src/runtime/combine.cpp
  src/runtime/combine_selftest.cpp
  src/runtime/copy_paste.cpp
  src/app/frame_cook.cpp
  src/app/cook_host_values.cpp
  src/app/frame_cook_selftest.cpp
  src/app/frame_cook_contextvar_selftest.cpp
  src/app/frame_cook_setboolvar_selftest.cpp
  src/app/frame_cook_animvalue_selftest.cpp
  src/app/frame_cook_animint_selftest.cpp
  src/app/frame_cook_animboolean_selftest.cpp
  src/app/frame_cook_wastrigger_selftest.cpp
  src/app/soundtrack.cpp
  src/app/soundtrack_selftest.cpp
  src/app/soundtrack_selftest_d4.cpp
  src/runtime/graph_bridge.cpp
  src/runtime/graph_bridge_selftest.cpp
  src/runtime/compound_spec_selftest.cpp
  src/runtime/state_count_selftest.cpp
  src/runtime/resident_eval_graph.cpp
  src/runtime/resident_eval_flatten.cpp
  src/runtime/resident_host_scalar_cook.cpp
  src/runtime/resident_value_output_cook.cpp
  src/runtime/resident_matrix_output_cook.cpp
  src/runtime/resident_point_value_output_cook.cpp
  src/runtime/resident_colorlist_cook.cpp
  src/runtime/resident_string_cook.cpp
  src/runtime/resident_stringlist_cook.cpp
  src/runtime/resident_mesh_cook.cpp
  src/runtime/resident_eval_graph_selftest.cpp
  src/runtime/resident_eval_cache.cpp
  src/runtime/resident_eval_cache_selftest.cpp
  src/runtime/resident_eval_idlefade_selftest.cpp
  src/runtime/resident_eval_patch.cpp
  src/runtime/resident_eval_patch_selftest.cpp
  src/runtime/resident_eval_patch_lib.cpp
  src/runtime/resident_eval_patch_lib_selftest.cpp
  src/runtime/math_ops_selftest.cpp
  src/runtime/math_ops_selftest_extra.cpp  # vec2 + logic + misc-vec teeth split off (line-cap)
  ${SW_POINT_OP_SRCS}  # globbed: point_ops.cpp + _selftest + every per-op leaf + per-family registrars
  ${SW_VALUE_OP_SRCS}  # globbed: value_op_registry.cpp + every stateless value-op leaf (Sin/PickFloat/…)
  ${SW_STATEFUL_OP_SRCS}  # globbed: stateful_value_op_registry.cpp + every per-subfamily leaf + selftest
  ${SW_POINT_MODIFY_SRCS}  # globbed: point_modify_op_registry.cpp + every per-subfamily point-modify leaf
  src/runtime/tex_op_cache.cpp
  src/runtime/source_registry.cpp
  src/runtime/attack_detector.cpp
  src/runtime/audio_analyzer.cpp
  src/runtime/spectrum_analyzer.cpp
  src/runtime/beat_synchronizer.cpp  # audio-locked BPM/bar tracker (port of TiXL Core/Audio/BeatSynchronizer.cs)
  src/runtime/beat_lock_selftest.cpp # G2 SlidingAverage<10> de-jitter + G3 beat_timing audio-lock orphan接通
  src/runtime/bpm_detection.cpp
  src/runtime/bpm_provider.cpp       # [SetBpm] triggered-pull singleton (= TiXL Core/IO/BpmProvider.cs)
  src/runtime/playback_provider.cpp  # [SetPlaybackTime/Speed] write mailbox (= SetPlaybackTime/Speed.cs Playback.Current write)
  src/runtime/detect_bpm.cpp
  src/runtime/audio_reaction.cpp
  src/runtime/dispatch.cpp
  src/runtime/audio_ingest.cpp
  src/runtime/audio_ingest_replay.cpp
  ${IMGUI_DIR}/imgui.cpp
  ${IMGUI_DIR}/imgui_draw.cpp
  ${IMGUI_DIR}/imgui_tables.cpp
  ${IMGUI_DIR}/imgui_widgets.cpp
  ${IMGUI_DIR}/imgui_demo.cpp
  ${IMGUI_OBJCXX}
  # imgui-node-editor (backend-agnostic, pure ImDrawList)
  ${NODE_EDITOR_DIR}/imgui_node_editor.cpp
  ${NODE_EDITOR_DIR}/imgui_node_editor_api.cpp
  ${NODE_EDITOR_DIR}/imgui_canvas.cpp
  ${NODE_EDITOR_DIR}/crude_json.cpp
)

target_include_directories(simple_world PRIVATE
  src
  ${METAL_CPP_DIR}
  ${METAL_CPP_EXT_DIR}
  ${IMGUI_DIR}
  ${IMGUI_DIR}/backends
  ${NODE_EDITOR_DIR}
  ${NFD_DIR}/src/include
)

# IMGUI_IMPL_METAL_CPP        -> backend public API uses MTL::* (pure C++ caller)
# IMGUI_IMPL_METAL_CPP_EXTENSIONS -> exposes the void* osx overload for C++ callers
target_compile_definitions(simple_world PRIVATE
  IMGUI_IMPL_METAL_CPP
  IMGUI_IMPL_METAL_CPP_EXTENSIONS
  SW_SHADER_METALLIB="${SW_METALLIB}"
  SW_EYE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/.eye"
  SW_TESTDATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/testdata"
  # Repo assets dir (copied parity assets, e.g. images/basic/perlin-noise-rgb.png from TiXL Lib).
  # ../assets because CMakeLists lives in app/ but assets/ is at the repo root.
  SW_ASSETS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../assets"
  # Field shader-graph render TEMPLATE (a string asset, NOT precompiled — see shaders/templates/).
  # assembleFieldMSL reads this and fills /*{GLOBALS}*/ /*{FLOAT_PARAMS}*/ /*{FIELD_CALL}*/ at runtime.
  SW_FIELD_TEMPLATE="${CMAKE_CURRENT_SOURCE_DIR}/shaders/templates/field_render_template.metal"
  # PF-a field-into-FORCE compute TEMPLATE (same string-asset rule; the COMPUTE sibling of the render
  # template above). assembleFieldMSL fills the SAME hooks; cookParticleSim compiles the result into a
  # compute PSO for the VectorFieldForce field-into-force bridge.
  SW_VFF_TEMPLATE="${CMAKE_CURRENT_SOURCE_DIR}/shaders/templates/vector_field_force_template.metal"
  # PF field-into-FORCE compute TEMPLATE for FieldDistanceForce (the SDF-distance attract/repel sibling of
  # the VFF template above). Same string-asset rule; assembleFieldMSL fills the SAME hooks; cookParticleSim
  # compiles the result into a compute PSO keyed on srcHash.
  SW_FIELD_DISTANCE_TEMPLATE="${CMAKE_CURRENT_SOURCE_DIR}/shaders/templates/field_distance_force_template.metal"
  # PF field-into-FORCE compute TEMPLATE for RandomJumpForce (curlNoise jump modulated by field color; the
  # POSITION-write sibling of the VFF/FieldDistance templates above). Same string-asset rule; assembleFieldMSL
  # fills the SAME hooks; cookParticleSim compiles the result into a compute PSO keyed on srcHash.
  SW_RANDOM_JUMP_TEMPLATE="${CMAKE_CURRENT_SOURCE_DIR}/shaders/templates/random_jump_force_template.metal"
  # PF field-into-FORCE compute TEMPLATE for FieldVolumeForce (SDF-surface bounce/attract/repel; the velocity-
  # reflect sibling of the VFF/FieldDistance/RandomJump templates above). Same string-asset rule; assembleFieldMSL
  # fills the SAME hooks; cookParticleSim compiles the result into a compute PSO keyed on srcHash.
  SW_FIELD_VOLUME_TEMPLATE="${CMAKE_CURRENT_SOURCE_DIR}/shaders/templates/field_volume_force_template.metal"
  # 3D sphere-trace render TEMPLATE (the renderField3d counterpart to the 2D SW_FIELD_TEMPLATE;
  # the camera matrices + ray-march loop; same /*{GLOBALS}*/ /*{FLOAT_PARAMS}*/ /*{FIELD_CALL}*/ hooks).
  SW_FIELD_RAYMARCH_TEMPLATE="${CMAKE_CURRENT_SOURCE_DIR}/shaders/templates/field_raymarch_template.metal"
  # SDF point-MODIFY compute TEMPLATE for MovePointsToSDF (raymarch each point of a bag to the field surface;
  # SourcePoints→ResultPoints, the points-modify sibling of the field-into-FORCE templates above). Same
  # string-asset rule; assembleFieldMSL fills the SAME hooks; the cook compiles the result via cachedSourceComputePSO.
  SW_MOVE_POINTS_TO_SDF_TEMPLATE="${CMAKE_CURRENT_SOURCE_DIR}/shaders/templates/move_points_to_sdf_template.metal"
  # SDF point-MODIFY + COUNT-MULTIPLY compute TEMPLATE for SdfReflectionLinePoints (raymarch+reflect each
  # source point off a wired SDF, emit a polyline; the count-multiplying sibling of MoveToSDF). Same
  # string-asset rule; assembleFieldMSL fills the SAME hooks; the cook compiles via cachedSourceComputePSO.
  SW_SDF_REFLECTION_LINE_POINTS_TEMPLATE="${CMAKE_CURRENT_SOURCE_DIR}/shaders/templates/sdf_reflection_line_points_template.metal"
  # SDF point-MODIFY + COUNT-MULTIPLY compute TEMPLATE for RaymarchPoints (two-mode raymarch each source
  # point to/through a wired SDF; the two-mode sibling of SdfReflectionLinePoints). Same string-asset rule;
  # assembleFieldMSL fills the SAME hooks; the cook compiles the result via cachedSourceComputePSO.
  SW_RAYMARCH_POINTS_TEMPLATE="${CMAKE_CURRENT_SOURCE_DIR}/shaders/templates/raymarch_points_template.metal"
  # Direct-Field gather LEAF (cloned from MoveToSDF) — PointColorWithField: evaluate the wired SDF field's
  # COLOR branch (w=1) at each point position and lerp the point Color toward it. Same string-asset rule;
  # assembleFieldMSL fills the SAME hooks; the cook compiles the result via cachedSourceComputePSO.
  SW_POINT_COLOR_WITH_FIELD_TEMPLATE="${CMAKE_CURRENT_SOURCE_DIR}/shaders/templates/pointcolorwithfield_template.metal"
  # Direct-Field gather LEAF (cloned from MoveToSDF) — SelectPointsWithSDF: read the SDF distance (w=0) at
  # each point, map it through Mode/Mapping/Range/Offset/GainAndBias and write the selection scalar to
  # FX1/FX2. Same string-asset rule; assembleFieldMSL fills the SAME hooks; the cook compiles via cachedSourceComputePSO.
  SW_SELECT_POINTS_WITH_SDF_TEMPLATE="${CMAKE_CURRENT_SOURCE_DIR}/shaders/templates/selectpointswithsdf_template.metal"
)

# The executable needs the metallib built before it runs.
add_dependencies(simple_world sw_shaders)

target_link_libraries(simple_world PRIVATE
  nfd
  "-framework UniformTypeIdentifiers"
  "-framework Metal"
  "-framework MetalKit"
  "-framework AppKit"
  "-framework Foundation"
  "-framework ImageIO"
  "-framework CoreGraphics"
  "-framework QuartzCore"
  "-framework Cocoa"
  "-framework Carbon"
  "-framework GameController"
  "-framework AVFoundation"
  "-framework CoreAudio"
  "-framework AudioToolbox"
  "-framework CoreMIDI"
  "-framework Accelerate"
)

# Embed Info.plist into the bare Mach-O so TCC can read NSMicrophoneUsageDescription
# (required for AVAudioEngine mic capture without a .app bundle).
target_link_options(simple_world PRIVATE
  "-Wl,-sectcreate,__TEXT,__info_plist,${CMAKE_CURRENT_SOURCE_DIR}/Info.plist")

# The linker's auto adhoc signature does NOT bind (seal) the embedded Info.plist —
# `codesign -dvvv` shows "Info.plist=not bound", and TCC then ignores the usage
# description (no microphone prompt). Re-sign after link so the plist is sealed
# ("Info.plist entries=4") and TCC honors NSMicrophoneUsageDescription.
add_custom_command(TARGET simple_world POST_BUILD
  COMMAND codesign --force --sign - --identifier com.baiwei.simpleworld
          $<TARGET_FILE:simple_world>
  COMMENT "Re-sign to bind embedded Info.plist (TCC microphone)")

# Architecture tripwire (ARCHITECTURE.md 鐵律 2): fail the build if any zone crosses a
# forbidden dependency boundary (leaf->leaf, leaf->up, ui->platform, app->ui). A rule nobody
# checks gets violated — this catches drift at compile time, not in a late human review.
# Runs before every simple_world build; standalone: cmake --build build --target check-arch.
add_custom_target(check-arch
  # Invoke EXACTLY as the documented command (`bash tools/check_arch.sh` from the repo root):
  # SRC defaults to the RELATIVE app/src and GRANDFATHER to the relative tools/linecount-grandfather.txt.
  # The grandfather file keys files by their repo-relative path (app/src/...), and the script greps
  # ^<path>:, so the scanned paths MUST be repo-relative too — passing an absolute SRC makes every
  # grandfathered file look brand-new (absolute path never matches the relative key) → false FAIL.
  # Hence: run from the repo root, no SRC argument.
  COMMAND bash tools/check_arch.sh
  WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/..
  COMMENT "Checking zone dependency direction + line-count ratchet (ARCHITECTURE.md)"
  VERBATIM)
add_dependencies(simple_world check-arch)

if(SW_SANITIZE)
  target_compile_options(simple_world PRIVATE
    -fsanitize=address,undefined -fno-omit-frame-pointer)
  target_link_options(simple_world PRIVATE -fsanitize=address,undefined)
endif()
