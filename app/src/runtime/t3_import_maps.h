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

}  // namespace sw
