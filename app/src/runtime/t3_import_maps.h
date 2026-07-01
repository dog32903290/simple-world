// runtime/t3_import_maps — the three-table Guid→sw mapping DATA for the .t3 importer, split out of
// t3_import.cpp (ARCHITECTURE rule 4 / rule 7: the mapping tables are a DISTINCT data-driven job — a
// guid→type table + a per-atom guid→slot-name table — so adding an atom = adding a row here, never
// touching the importer's parse/fill logic). t3_import.cpp owns the JSON walk + SymbolLibrary fill; this
// file owns ONLY the lookups. Pure CPU, no dependencies beyond <string>.
#pragma once
#include <string>

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

}  // namespace sw
