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
struct RaymarchTransforms;  // field_camera.h

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

// RaymarchField render scalars (TiXL RaymarchSDFFieldTemplate.hlsl ParamConstants @ b1; defaults from
// RaymarchField.t3). Only the march scalars (MaxSteps/StepSize/MinDistance/MaxDistance) drive the LIVE
// TiXL output (the steps/MaxSteps glow grayscale, see template LIVE-OUTPUT FORK); the colors are for
// the parity-deferred Blinn-Phong path. A caller may override before renderField3d for a known golden.
struct RaymarchRenderParams {
  float maxSteps = 100.0f;     // RaymarchField.t3 MaxSteps
  float stepSize = 1.0f;       // StepSize (also the march D seed)
  float minDistance = 0.002f;  // MinDistance
  float maxDistance = 300.0f;  // MaxDistance
  float distToColor = 0.15f;   // DistToColor (color path only)
  float aoDistance = 1.0f;     // AoDistance (color path only)
  // Colors (color path only — do NOT affect the live glow output). Faithful-ish placeholders.
  float specular[4] = {1.0f, 1.0f, 1.0f, 1.0f};
  float glow[4] = {1.0f, 1.0f, 1.0f, 1.0f};
  float ambientOcclusion[4] = {0.0f, 0.0f, 0.0f, 0.002f};  // .a = AmbientOcclusion default (.t3 :64)
  float background[4] = {0.0f, 0.0f, 0.0f, 1.0f};
  float lightPos[3] = {1.0f, 1.0f, 1.0f};
  float spec[2] = {0.5f, 30.0f};
};

// Render the field tree rooted at `root` as a 3D SPHERE-TRACED image (raymarch3D). Parallels
// renderField2d but uses field_raymarch_template.metal: it binds the field param buffer at [[buffer(0)]],
// the raymarch scalars at [[buffer(1)]], and the camera Transforms (row-major matrices) at [[buffer(2)]],
// plus the Seam-A texture bindings, then draws a fullscreen triangle whose fragment unprojects a world
// ray and sphere-traces the field. Output is an RGBA32Float texture (the live TiXL glow grayscale; R=G=B).
// `xf` is the camera (host-built via field_camera.h, e.g. defaultRaymarchTransforms(aspect)). Returns an
// OWNED MTL::Texture* (caller release()s) or nullptr on failure (null root, compile/PSO/alloc failure).
//
// REUSE: assembleFieldMSL / evalField are reused UNCHANGED — only the template (camera + sphere-trace)
// differs from the 2D path, so the 16 existing SDF leaves cook byte-identically into both paths.
MTL::Texture* renderField3d(MTL::Device* dev, MTL::CommandQueue* queue,
                            const std::shared_ptr<FieldNode>& root, const std::string& templateMsl,
                            const RaymarchTransforms& xf, const RaymarchRenderParams& params,
                            uint32_t w, uint32_t h);

// --- RaymarchField TEX op (field_ops_raymarchfield.cpp) seam ----------------------------------------
// Process-global inject-bug toggle for the output golden: when true the RaymarchField cook short-
// circuits to a black clear (the raymarch is skipped) so the silhouette vanishes → the golden's
// center-vs-corner margin collapses → RED. Off in production. (Mirror of meshInjectBug().) The shell
// golden (field_raymarch_output_golden.cpp) flips it; runtime owns it (defined in the leaf).
bool& raymarchFieldInjectBug();

// FieldToImage render-time OP params (field_to_image_template.metal's FieldToImageParams @ buffer(1);
// host mirror — same discipline as RaymarchParamsGpu in field_render.cpp: exact byte layout, ALL plain
// floats so a raw memcpy lands every field at the MSL-expected offset). TiXL authority: FieldToImage.cs
// slots + FieldToImage.t3 defaults (Scale=1, SliceDepth=0, Range=(0,1), GainAndBias=(0.5,0.5) [neutral],
// Mode=0=MapDistanceToColor, Center=(0,0), Rotate=0, PingPong=false, Repeat=false).
struct FieldToImageParams {
  float centerX = 0.0f, centerY = 0.0f;  // FieldToImage.t3 Center
  float scale = 1.0f;                    // Scale
  float rotate = 0.0f;                   // Rotate (degrees, matches TiXL radians(Rotate) in-shader)
  float sliceDepth = 0.0f;               // SliceDepth
  float mode = 0.0f;                     // Mode: 0 = MapDistanceToColor, 1 = UseColor
  float rangeX = 0.0f, rangeY = 1.0f;    // Range
  float gainX = 0.5f, biasY = 0.5f;      // GainAndBias (0.5,0.5) = ApplyGainAndBias identity
  float pingPong = 0.0f, repeat = 0.0f;  // PingPong / Repeat (bool-as-float, >0.5 = true)
  // `aspect` is NOT a .t3 input (TiXL derives aspectRatio = TargetWidth/TargetHeight from the render
  // target's own ResolutionConstBuffer, b3) — the caller passes it precomputed from the output w/h.
};

// Render the field tree rooted at `root` through field_to_image_template.metal (FieldToImage tex op):
// SAME assembleFieldMSL as the 2D/raymarch paths (byte-identical cook for every existing SDF leaf), a
// SEPARATE FieldToImageParams buffer at [[buffer(1)]] carrying the op's own Center/Scale/Rotate/
// SliceDepth/Mode/Range/GainAndBias/PingPong/Repeat, and `gradientRow` (a 1×N RGBA32Float texture, e.g.
// from rasterizeGradientRow) bound at the FIXED [[texture(30)]] slot (out of band from Seam A's dynamic
// field textures). `aspect` = TargetWidth/TargetHeight (the caller derives it from the output w/h — v1
// has no separate resolution pin, mirrors RaymarchField's "output texture drives the size" shortcut).
// Output is an RGBA32Float texture (float for golden readback; the cook op tonemaps+copies to RGBA8).
// Returns an OWNED MTL::Texture* (caller release()s) or nullptr on failure (null root/gradientRow,
// compile/PSO/alloc failure).
MTL::Texture* renderFieldToImage(MTL::Device* dev, MTL::CommandQueue* queue,
                                 const std::shared_ptr<FieldNode>& root, const std::string& templateMsl,
                                 const FieldToImageParams& params, MTL::Texture* gradientRow,
                                 uint32_t w, uint32_t h);

// --- FieldToImage TEX op (field_ops_fieldtoimage.cpp) seam --------------------------------------------
// Process-global inject-bug toggle for the output golden: when true the FieldToImage cook short-circuits
// to a black clear (mirrors raymarchFieldInjectBug() above). Off in production.
bool& fieldToImageInjectBug();

}  // namespace sw
