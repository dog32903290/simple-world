// runtime/t3_import_texcompute_cb — CB-scalar trace for the SRV-tex compute collapse (split from
// t3_import_texcompute.cpp for the ≤400-line ratchet, ARCHITECTURE.md rule 4). Baking a b0 CB entry =
// tracing its FloatsToBuffer.Params wire (in order == the cbuffer layout) back to a boundary scalar
// through the standard 1-hop value ops. collapseTextureComputeStageSrv now WIRES the direct-boundary
// scalars LIVE and only BAKES the value-op-mediated ones through resolveCbScalar (fork
// computestagetex-cb-defaults-baked); its Pass-1 classifier recognizes a CB-source child via the guids.
#pragma once

#include <map>
#include <string>

namespace crude_json { struct value; }

namespace sw {

// The CB-source value-op guids collapseTextureComputeStageSrv's Pass-1 classifier recognizes (a child
// with one of these SymbolIds is a CB source → skip, not an "unknown" child). Defs in the .cpp.
extern const char* const kCbIntToFloatSym;
extern const char* const kCbBoolToFloatSym;
extern const char* const kCbVec2ComponentsSym;

// Resolve ONE FloatsToBuffer.Params source (srcGuid,srcSlot) to a BAKED CB float, tracing the standard
// scalar value ops one hop to a boundary Input default:
//   • boundary          → the boundary scalar default
//   • IntToFloat        → (float) its IntValue source's boundary int
//   • BoolToFloat       → its BoolValue source's boundary bool ? ForTrue : ForFalse
//   • Vector2Components → its Value source's boundary vec2 [X|Y] (by which output slot fed Params)
float resolveCbScalar(const crude_json::value& root,
                      const std::map<std::string, std::string>& childSym,
                      const std::string& srcGuid, const std::string& srcSlot);

}  // namespace sw
