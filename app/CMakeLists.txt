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
set(SW_SHADERS radial_emit radial_points particle_sim turbulence_force draw_points linepoints gridpoints spherepoints transformpoints orientpoints randomizepoints setpointattributes)
set(SW_AIRS "")
set(SW_SHADER_DEPS
  src/runtime/tixl_point.h src/runtime/particle_params.h
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
# audio_capture.mm uses AVAudioEngine — ARC keeps the ObjC lifetime simple (it touches
# no metal-cpp pointers, so the MRC convention of the other .mm files doesn't apply).
set_source_files_properties(src/platform/audio_capture.mm PROPERTIES COMPILE_OPTIONS -fobjc-arc)

add_executable(simple_world
  src/main.cpp
  src/selftests.cpp
  src/app/document.cpp
  src/app/audio_settings.cpp
  src/app/audio_monitor.cpp
  src/app/command.cpp
  src/app/graph_commands.cpp
  src/app/menu.cpp
  src/ui/editor_ui.cpp
  src/ui/node_draw.cpp
  src/ui/node_faces.cpp
  src/ui/node_style.cpp
  src/ui/output_window.cpp
  src/metal_impl.cpp
  src/verify/eye/eye.mm
  src/verify/hand/hand.cpp
  src/platform/dialogs.mm
  src/platform/audio_capture.mm
  src/platform/audio_devices.mm
  src/runtime/particle_system.cpp
  src/runtime/graph.cpp
  src/runtime/node_registry.cpp
  src/runtime/graph_selftest.cpp
  src/runtime/point_graph.cpp
  src/runtime/point_graph_resident.cpp
  src/runtime/point_graph_selftest.cpp
  src/runtime/resident_cook_selftest.cpp
  src/runtime/resident_cook_parity_selftest.cpp
  src/runtime/compound_graph.cpp
  src/runtime/compound_graph_selftest.cpp
  src/runtime/compound_save.cpp
  src/runtime/compound_save_selftest.cpp
  src/app/frame_cook.cpp
  src/runtime/graph_bridge.cpp
  src/runtime/graph_bridge_selftest.cpp
  src/runtime/state_count_selftest.cpp
  src/runtime/resident_eval_graph.cpp
  src/runtime/resident_eval_graph_selftest.cpp
  src/runtime/resident_eval_cache.cpp
  src/runtime/resident_eval_cache_selftest.cpp
  src/runtime/resident_eval_patch.cpp
  src/runtime/resident_eval_patch_selftest.cpp
  src/runtime/resident_eval_patch_lib.cpp
  src/runtime/resident_eval_patch_lib_selftest.cpp
  src/runtime/point_ops.cpp
  src/runtime/point_ops_selftest.cpp
  src/runtime/point_ops_linepoints.cpp
  src/runtime/point_ops_gridpoints.cpp
  src/runtime/point_ops_spherepoints.cpp
  src/runtime/point_ops_transformpoints.cpp
  src/runtime/point_ops_orientpoints.cpp
  src/runtime/point_ops_randomizepoints.cpp
  src/runtime/point_ops_setpointattributes.cpp
  src/runtime/point_ops_combinebuffers.cpp
  src/runtime/point_ops_rendertarget.cpp
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
