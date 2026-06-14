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
  src/runtime/tint_params.h src/runtime/chromab_params.h src/runtime/adjustcolors_params.h src/runtime/channelmixer_params.h
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
set_source_files_properties(src/platform/dialogs.mm PROPERTIES COMPILE_OPTIONS -fno-objc-arc)
# audio_capture.mm / audio_playback.mm use AVFoundation — ARC keeps the ObjC lifetime simple
# (they touch no metal-cpp pointers, so the MRC convention of the other .mm files doesn't apply).
set_source_files_properties(src/platform/audio_capture.mm PROPERTIES COMPILE_OPTIONS -fobjc-arc)
set_source_files_properties(src/platform/audio_playback.mm PROPERTIES COMPILE_OPTIONS -fobjc-arc)

# Point-op leaf sources — globbed so a family lane adding point_ops_<name>.cpp (or a new
# point_ops_register_<family>.cpp) needs no CMake edit (just reconfigure). Covers the central
# point_ops.cpp, point_ops_selftest.cpp, every per-op leaf, and the per-family registrars.
file(GLOB SW_POINT_OP_SRCS CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/runtime/point_ops*.cpp")

add_executable(simple_world
  src/main.cpp
  src/app_delegate.cpp
  src/selftests.cpp
  src/app/document.cpp
  src/app/audio_settings.cpp
  src/app/audio_monitor.cpp
  src/app/command.cpp
  src/app/graph_commands.cpp
  src/app/graph_commands_selftest.cpp
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
  src/app/menu.cpp
  src/ui/cjk_font.cpp
  src/ui/canvas_ids.cpp
  src/ui/fence_preview.cpp
  src/ui/editor_ui.cpp
  src/ui/toolbar.cpp
  src/ui/combine_dialog.cpp
  src/ui/copy_paste_ui.cpp
  src/ui/keymap.cpp
  src/ui/quick_add.cpp
  src/ui/inspector.cpp
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
  src/ui/node_faces.cpp
  src/ui/annotation_draw.cpp
  src/ui/annotation_interact.cpp
  src/ui/node_style.cpp
  src/ui/output_window.cpp
  src/metal_impl.cpp
  src/verify/eye/eye.mm
  src/verify/hand/hand.cpp
  src/verify/hand/hand_selftest.cpp
  src/platform/dialogs.mm
  src/platform/audio_capture.mm
  src/platform/audio_playback.mm
  src/platform/audio_devices.mm
  src/runtime/particle_system.cpp
  src/runtime/particle_decay_selftest.cpp
  src/runtime/graph.cpp
  src/runtime/node_registry.cpp
  src/runtime/node_registry_generators.cpp
  src/runtime/node_registry_point_modify.cpp
  src/runtime/node_registry_point_combine.cpp
  src/runtime/node_registry_particle.cpp
  src/runtime/node_registry_draw.cpp
  src/runtime/node_registry_image_filter.cpp
  src/runtime/node_registry_math.cpp
  src/runtime/value_eval_ops.cpp
  src/runtime/graph_selftest.cpp
  src/runtime/point_graph.cpp
  src/runtime/point_graph_resident.cpp
  src/runtime/point_graph_selftest.cpp
  src/runtime/resident_cook_selftest.cpp
  src/runtime/resident_cook_parity_selftest.cpp
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
  src/runtime/compound_save.cpp
  src/runtime/compound_load.cpp
  src/runtime/compound_save_selftest.cpp
  src/runtime/testproj_selftest.cpp
  src/runtime/combine.cpp
  src/runtime/combine_selftest.cpp
  src/runtime/copy_paste.cpp
  src/app/frame_cook.cpp
  src/app/frame_cook_selftest.cpp
  src/app/soundtrack.cpp
  src/app/soundtrack_selftest.cpp
  src/app/soundtrack_selftest_d4.cpp
  src/runtime/graph_bridge.cpp
  src/runtime/graph_bridge_selftest.cpp
  src/runtime/compound_spec_selftest.cpp
  src/runtime/state_count_selftest.cpp
  src/runtime/resident_eval_graph.cpp
  src/runtime/resident_eval_flatten.cpp
  src/runtime/resident_eval_graph_selftest.cpp
  src/runtime/resident_eval_cache.cpp
  src/runtime/resident_eval_cache_selftest.cpp
  src/runtime/resident_eval_idlefade_selftest.cpp
  src/runtime/resident_eval_patch.cpp
  src/runtime/resident_eval_patch_selftest.cpp
  src/runtime/resident_eval_patch_lib.cpp
  src/runtime/resident_eval_patch_lib_selftest.cpp
  src/runtime/math_ops_selftest.cpp
  ${SW_POINT_OP_SRCS}  # globbed: point_ops.cpp + _selftest + every per-op leaf + per-family registrars
  src/runtime/tex_op_cache.cpp
  src/runtime/source_registry.cpp
  src/runtime/attack_detector.cpp
  src/runtime/audio_analyzer.cpp
  src/runtime/spectrum_analyzer.cpp
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
  "-framework QuartzCore"
  "-framework Cocoa"
  "-framework Carbon"
  "-framework GameController"
  "-framework AVFoundation"
  "-framework CoreAudio"
  "-framework AudioToolbox"
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
  COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/../tools/check_arch.sh ${CMAKE_CURRENT_SOURCE_DIR}/src
  COMMENT "Checking zone dependency direction (ARCHITECTURE.md)"
  VERBATIM)
add_dependencies(simple_world check-arch)

if(SW_SANITIZE)
  target_compile_options(simple_world PRIVATE
    -fsanitize=address,undefined -fno-omit-frame-pointer)
  target_link_options(simple_world PRIVATE -fsanitize=address,undefined)
endif()
