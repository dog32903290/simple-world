// runtime/colorlist_op_registry — self-registration seam for COLORLIST ops (the vec4-list cook flow).
//
// The ColorList channel is a HOST-side value currency — TiXL's Slot<List<Vector4>> (a CPU list of
// colors that rides between ops), NOT a GPU buffer. It is the vec4 parallel of the FloatList rail
// (floatlist_op_registry.h): a producer port (dataType=="ColorList" output) hands a
// std::vector<simd::float4> to a consumer port (dataType=="ColorList" input). The list lives in host
// memory the whole way; it never touches the 16-byte GPU EvaluationContext.
//
// VERBATIM clone of floatlist_op_registry.h with the currency widened float -> simd::float4. The ONLY
// real differences are the element type (simd::float4) and the gather shape of a Vector4 MultiInput
// (see fork-colorstolist-vec4-as-4-parallel-multiinputs in colorlist_ops_colorstolist.cpp): a
// Vector4 is the established vecN-as-N-floats fork, so a MultiInput<Vector4> becomes 4 PARALLEL Float
// MultiInput component ports the driver zips per-index into one host float4 list. The driver therefore
// also hands a `inputColorScalars` channel (4 parallel scalar lists) alongside `inputLists`.
//
// Init-order safety / intra-family ORDER fork: identical to floatlist_op_registry (every registrar is a
// namespace-scope static; ORDER in the sink follows cross-TU dynamic-init order, cosmetic only).
#pragma once

#include <array>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include <simd/simd.h>  // simd::float4 (the ColorList element type)

#include "runtime/graph.h"  // NodeSpec

namespace MTL {
class Device;
class Library;
class CommandQueue;
class Buffer;
}  // namespace MTL

struct EvaluationContext;  // runtime/eval_context.h

namespace sw {

// Everything a colorlist op gets to cook one node this frame. Mirrors FloatListCookCtx but the currency
// is a HOST std::vector<simd::float4>, not std::vector<float>. NO pre-sizing (a vector self-sizes) and
// NO Metal allocation. dev/lib/queue ride along for symmetry (a future GPU-bound colorlist consumer
// could need them; a pure producer like ColorsToList ignores them).
//
//   inputLists         : already-cooked upstream ColorList inputs (spec port order, MultiInput-expanded
//                        into wire-declaration order). A pure producer (ColorsToList) has none → empty.
//   inputColorScalars  : the 4 PARALLEL scalar Float MultiInput component channels (Colors.x/.y/.z/.w),
//                        each a list of wired scalars in wire-declaration order. ColorsToList ZIPS them
//                        per-index into output colors. Empty for a colorlist op with no Vec4 MultiInput.
//   output             : THIS node's host color list. Driver-owned (Impl::colorListBuf, keyed by
//                        flatKey(id)); the op WRITES into *output (clear + fill) — never allocates it.
//   params             : RESOLVED Float params of THIS node (same value spine as FloatListCookCtx).
struct ColorListCookCtx {
  MTL::Device* dev = nullptr;
  MTL::Library* lib = nullptr;
  MTL::CommandQueue* queue = nullptr;
  const EvaluationContext* ctx = nullptr;  // time / frameIndex / deltaTime
  int nodeId = 0;
  // Cooked upstream ColorList inputs (one entry per WIRED ColorList source, spec port order with
  // MultiInput ports expanded into wire-declaration order). Borrowed (driver-owned); never retained.
  const std::vector<std::vector<simd::float4>>* inputLists = nullptr;
  // The 4 parallel scalar Float MultiInput component channels (x,y,z,w), each in wire-declaration
  // order. The vec4-as-4-floats MultiInput fork: ColorsToList zips index i across the 4 to one float4.
  const std::array<std::vector<float>, 4>* inputColorScalars = nullptr;
  // POINTS-bag input (the point-readback rail-crossing): the already-cooked upstream Points buffer wired
  // to a colorlist op's "Points" input port, + its point count. A ColorList op that READS a GPU point bag
  // (ReadPointColors) gathers the .Color field (byte offset 32 of SwPoint) per point into the host color
  // list. The bag is StorageModeShared and the upstream point op committed+waited during its own cook, so
  // contents() is valid CPU-side here (same posture as point_ops_boundingboxpoints.cpp's CPU readback).
  // null/0 for every colorlist op with no Points input (ColorsToList/ColorList/CombineColorLists).
  const MTL::Buffer* inputPointsBag = nullptr;
  uint32_t inputPointsCount = 0;
  // Driver-owned output list. The op writes via output->clear()/push_back; never allocates/frees it.
  std::vector<simd::float4>* output = nullptr;
  // RESOLVED Float params of THIS node (mirror of FloatListCookCtx::params); read via colorListParam.
  const std::map<std::string, float>* params = nullptr;
};

// A colorlist op: read inputs (+ resolved Float params) → write *output. ONE fn (a host vector self-
// sizes — the driver never pre-allocates it). Mirror of FloatListCookFn.
using ColorListCookFn = void (*)(ColorListCookCtx&);

// Read a Float param from a ColorListCookCtx's RESOLVED map (mirror of floatListParam); `def` when the
// driver supplied no map (ops invoked outside a cook driver, e.g. a hand-built ctx in a golden).
float colorListParam(const std::map<std::string, float>* params, const char* id, float def);

// --- the two sinks every colorlist-op leaf registrar feeds ---
std::vector<NodeSpec>& colorListSpecSink();                   // NodeSpecs (node_registry reads live)
std::map<std::string, ColorListCookFn>& colorListCookFns();  // type-name -> cook fn

// Lookup the cook fn for a type (nullptr if not a colorlist op). Used by the cook driver's dispatch.
const ColorListCookFn* findColorListOp(const std::string& type);

// Test-only injection seam (goldens): when set, a colorlist op's cook corrupts its REAL output (e.g.
// drops the last element) so the golden's RED case fires on the actual cook path (NOT by flipping the
// expected value). Off in production. A leaf reads it at the end of its cook.
bool& colorListInjectBug();

// RAII registrar: declare one file-scope static of this type at the end of each colorlist-op leaf.
//   ColorListOp(spec, cookFn);  // pushes spec into colorListSpecSink() and cook into colorListCookFns()
struct ColorListOp {
  ColorListOp(NodeSpec spec, ColorListCookFn cook);
};

}  // namespace sw
