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
set(SW_SHADERS radial_emit particle_sim turbulence_force draw_points)
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

add_executable(simple_world
  src/main.cpp
  src/app/document.cpp
  src/ui/editor_ui.cpp
  src/metal_impl.cpp
  src/verify/eye/eye.mm
  src/platform/dialogs.mm
  src/runtime/particle_system.cpp
  src/runtime/graph.cpp
  src/runtime/dispatch.cpp
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
)

if(SW_SANITIZE)
  target_compile_options(simple_world PRIVATE
    -fsanitize=address,undefined -fno-omit-frame-pointer)
  target_link_options(simple_world PRIVATE -fsanitize=address,undefined)
endif()
