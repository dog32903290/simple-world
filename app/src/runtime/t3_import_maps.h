// runtime/t3_import_maps — the three-table Guid→sw mapping DATA for the .t3 importer, split out of
// t3_import.cpp (ARCHITECTURE rule 4 / rule 7: the mapping tables are a DISTINCT data-driven job — a
// guid→type table + a per-atom guid→slot-name table — so adding an atom = adding a row here, never
// touching the importer's parse/fill logic). t3_import.cpp owns the JSON walk + SymbolLibrary fill; this
// file owns ONLY the lookups. Pure CPU, no dependencies beyond <string>.
#pragma once
#include <string>
#include <vector>

namespace sw {

// guid normalization: .cs writes UPPERCASE guids, .t3 lowercase. Lowercase so the tables (keyed
// lowercase) match either source. Empty guid stays empty.
std::string t3Lc(std::string s);

// TABLE ③: t3 symbol guid → sw op type ("" if unmapped → the child drops with a warning).
std::string swTypeForSymbolGuid(const std::string& guid);

// TABLE ②: (sw op type, t3 slot guid) → sw slot NAME (= PortSpec.id). "" if unknown for that atom.
std::string swSlotNameForGuid(const std::string& swType, const std::string& slotGuid);

// ComputeShader fold-pass guids (computeshader-source-folded-onto-stage): the ComputeShader symbol +
// its Source input / CS output slots + the ComputeShaderStage.ComputeShader input slot. ComputeShader
// never becomes an sw child; its Source folds onto the stage's KernelName.
extern const char* const kComputeShaderGuid;
extern const char* const kComputeShaderSourceSlot;
extern const char* const kComputeShaderCsOutSlot;
extern const char* const kComputeStageCsInSlot;
// 187 量產第一波: _ExecuteCombineBuffers's ComputeShader input slot — a SECOND fold target for the same
// ComputeShader.Source→KernelName post-pass (CombineBuffers.t3 wires its ComputeShader here, not into a
// ComputeShaderStage). The fold accepts either CS-in slot so a code-op compound folds like a stage does.
extern const char* const kCombineBuffersCsInSlot;

// ── IMAGE-FX COLLAPSE SEAM (image-fx-wrapper-collapses-to-tex-atom) ──────────────────────────────────
// A whole family of image .t3 ops (image/color, image/fx) are THIN wrappers: the root's single child is
// one of TiXL's image-fx-setup FRAMEWORK symbols (_multiImageFxSetupStatic / _multiImageFxSetup /
// _trippleImageFxSetup), parameterized by a ShaderPath pixel-shader, with the root's boundary Inputs
// wired INTO that child and the child's Texture2D output wired back OUT. In sw each such .t3 collapses
// to ONE flat tex atom (ports 1:1 with the op's .cs). These helpers drive that collapse in t3_import.cpp.

// TABLE ④a: is this SymbolId one of the image-fx-setup framework wrappers? (the child that gets collapsed
// AWAY — never emitted as an sw child; its inner ports are re-anchored onto the collapsed tex atom).
bool isImageFxSetupGuid(const std::string& guid);

// TABLE ④b: ROOT symbol guid → sw tex op type (e.g. HSE root 3c8003e8 → "HSE"). Non-empty ONLY for a
// root whose .t3 is a known image-fx wrapper we can collapse. "" = not a collapsible image-fx root.
std::string swTexOpForCollapseRootGuid(const std::string& rootGuid);

// TABLE ④c: for a collapsible root (swType), map a FX-SETUP-CHILD inner slot guid → sw tex atom port
// NAME for the FIXED (non-MultiInput) slots: ImageA→Image, ImageB→FxTexture, Output→out. "" if not a
// fixed slot (e.g. the FloatParams MultiInput, handled positionally by swFloatParamOrderForCollapse).
std::string swCollapseSlotNameForGuid(const std::string& swType, const std::string& slotGuid);

// TABLE ④d: the fx-setup FloatParams MultiInput (child slot 2929c4c9) receives the root's scalar boundary
// Inputs IN WIRE ORDER; TiXL binds them positionally to the shader cbuffer. This returns, per collapsed
// swType, the ORDERED list of sw scalar port names the positional wires land on (HSE → [Hue,Saturation,
// Exposure]). A boundary wire N into FloatParams becomes boundary→atom.<order[N]>. Empty if none.
const std::vector<std::string>& swFloatParamOrderForCollapse(const std::string& swType);

// The fx-setup FloatParams MultiInput slot guid (2929c4c9) — the positional scalar rail all wrappers share.
extern const char* const kFxSetupFloatParamsSlot;

// ── REDUNDANT-SUBGRAPH ELISION guids (used by t3_import_collapse.cpp) ─────────────────────────────────
// A whole class of image-fx wrappers feed the fx child's ImageB from a GradientsToTexture that renders the
// root's Gradient boundary → a 1D row; sw's atoms rasterize the Gradient row THEMSELVES from a "Gradient"
// PORT, so the GTT is redundant and ELIDED (gradientstotexture-elided-to-gradient-port): a wire off GTT.out
// re-anchors to the SOURCE feeding its Gradients input.
extern const char* const kGradientsToTextureGuid;         // GradientsToTexture.cs:9
extern const char* const kGradientsToTextureGradientsSlot; // GradientsToTexture.Gradients MultiInput (.cs:134-135)
// RemapColor.t3 interposes a TransformImage (identity copy, GenerateMips=true) between the GTT and the fx
// child's ImageB → ALSO elided (transformimage-identity-passthrough-elided): a wire off its TextureOutput
// re-anchors to its Image source, which chains on to the GTT. A bypassed GenerateMips child hangs off the
// GTT too (dead branch, elided with no source).
// ★FORK (named): the elided TransformImage's GenerateMips=true is NOT a true no-op — it makes TiXL's LUT
//   row texture MIPPED, and ColorRemap.hlsl samples it with .Sample() (auto-LOD), so at high-frequency
//   input-color regions the lookup can pick a COARSER mip. sw's rasterizeGradientRow (gradient_raster.h)
//   builds a NON-mipped row (mipmapped=false), always sampling base level. Dropping GenerateMips thus omits
//   LUT mip generation: sw and TiXL diverge (slight blur) only where adjacent input colors differ sharply.
//   Sub-perceptual on a 256-wide 1D LUT and zero at flat-region golden pins, so it is documented, not fixed.
//   [fork-remapcolor-lut-no-mips]
extern const char* const kTransformImageGuid;        // TransformImage.cs:3
extern const char* const kTransformImageImageSlot;    // TransformImage.Image (.cs:9-10)
extern const char* const kGenerateMipsGuid;           // GenerateMips.cs (dead branch)
// LinearGradient.t3 routes Offset through a PickFloat(OffsetMode,[Offset, Multiply(Width,Offset)]) subgraph.
// sw's atom REIMPLEMENTS that selection internally from its own Offset+OffsetMode ports → the subgraph is
// redundant and ELIDED (offset-routing-subgraph-elided-atom-reimplements): PickFloat.out re-anchors to its
// FIRST FloatValues source (raw Offset), the OffsetMode boundary re-anchors onto the atom's OffsetMode port,
// and the Multiply (fed only PickFloat) is dropped. Keeping it double-routes + the dropped boundary→Multiply
// wires poison it (Multiply reads a=b=1 default → Offset=1 → the ramp saturates).
extern const char* const kPickFloatGuid;         // PickFloat.cs:5
extern const char* const kPickFloatValuesSlot;    // PickFloat.FloatValues MultiInput (.cs:45)
extern const char* const kPickFloatIndexSlot;     // PickFloat.Index (.cs:48)
extern const char* const kMultiplyOffsetGuid;     // Multiply.cs:3 (Width×Offset, elided)

}  // namespace sw
