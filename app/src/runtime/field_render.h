// runtime/field_render — sw's Render2dField: drive an assembled field graph onto a GPU texture.
//
// ZONE: runtime. This is the GPU-dispatch half of the shader-graph island (Build-2). It takes a
// FieldNode tree (from field_graph), assembles its MSL via assembleFieldMSL, gets a render PSO from
// the source-PSO cache (cachedSourcePSO, keyed on srcHash — zero per-frame recompile), and draws a
// fullscreen triangle whose fragment evaluates the field and writes the signed distance f.w into the
// RED channel of an output texture. (Color-mapping parity is deferred — see field template.)
//
// PARITY: TiXL Render2dField + GenerateShaderGraphCode. The coordinate mapping (texCoord -> field
// space p.xy = (texCoord.x*2-1, (1-texCoord.y)*2-1), p.z = p.w = 0) is the FieldToImageTemplate
// default regula, backward-traced and pinned in the template + the golden.
//
// runtime never includes platform: the MSL-source compilation crosses the runtime->platform boundary
// only through the dormant fn-ptr seam (sw::fieldSourceCompiler, wired by app to
// platform::compileLibraryFromSource). This file names MTL types (it issues the draw) but compiles
// the source-string ONLY via that seam (inside cachedSourcePSO), so no platform include appears here.
#pragma once

#include <cstdint>
#include <memory>
#include <string>

namespace MTL {
class Device;
class CommandQueue;
class Texture;
}  // namespace MTL

namespace sw {

struct FieldNode;  // field_graph.h

// Render the field tree rooted at `root` into a freshly-allocated R32Float texture of (w,h),
// using `templateMsl` (the field render template contents). The output's RED channel carries the
// signed distance f.w at each texel (per the template). Returns an OWNED MTL::Texture* (caller
// release()s / wraps in NS::TransferPtr) on success, or nullptr if root is null, the source fails to
// compile, the PSO is unavailable (no field source compiler registered), or allocation fails.
//
// The PSO is cached by the assembled source's srcHash (cachedSourcePSO): the FIRST render of a given
// field topology compiles; subsequent renders of the same topology reuse the PSO with no recompile.
// The float-param buffer is rebuilt each call (cheap memcpy) so param edits take effect without
// recompiling — exactly TiXL's split (code cached by ChangedFlags.Code, params re-uploaded freely).
//
// R32Float (not RGBA8Unorm) is REQUIRED: the distance f.w is a signed float that ranges outside
// [0,1] (interior is negative; far points exceed 1). An 8-bit unorm target would clamp/quantize it
// and the golden could not read the true distance back. Single-channel R32 makes the CPU readback a
// plain float-per-texel (no half-float decode). (TiXL renders to a float field target too.)
MTL::Texture* renderField2d(MTL::Device* dev, MTL::CommandQueue* queue,
                            const std::shared_ptr<FieldNode>& root, const std::string& templateMsl,
                            uint32_t w, uint32_t h);

}  // namespace sw
