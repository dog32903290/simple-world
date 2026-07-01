// runtime/t3_import — the .t3 IMPORTER, forward-ported from the t3-importer spike onto MAIN and
// widened from the 1-atom spike map to the 6 LIVE buffer atoms (keystone首證). Parse one TiXL .t3
// file (JSON) into ONE Symbol inside a SymbolLibrary, preserving routing fidelity. The importer's
// whole job is the THREE-TABLE Guid→sw mapping (childId / slotName / symbolType), done once.
//
// FORWARD-PORT DELTA vs the spike (t3-importer/.../t3_import.cpp):
//   • Spike depended on a BARE step-1 atom (floats_to_buffer.h, its own floatsToBufferSpec provider
//     NOT in findSpec). Main already has FloatsToBuffer + 5 more buffer atoms LIVE (registered via
//     the BufferOp registrars → findSpec). So here every atom row's provider is NULL: resolve the
//     NodeSpec via findSpec, exactly the production registry the resident cook reads. No bare-atom
//     dependency, no step-3-cook fork — the atom Symbol generated into the lib is the SAME spec the
//     driver cooks.
//   • TABLE ③ widened from {FloatsToBuffer} to the 6 buffer atoms TransformPoints.t3 references that
//     sw actually HAS: FloatsToBuffer / IntsToBuffer / GetBufferComponents / GetSRVProperties /
//     ExecuteBufferUpdate / TransformsConstBuffer.
//   • TABLE ② widened with each atom's t3-slot-guid → sw-slot-NAME rows, hand-verified against the
//     TiXL .cs [Input]/[Output] Guid attributes (IntsToBuffer.cs / GetBufferComponents.cs /
//     GetSRVProperties.cs / ExecuteBufferUpdate.cs / FloatsToBuffer.cs / TransformsConstBuffer.cs).
//
// ★HONEST SCOPE (keystone RED note): TransformPoints.t3 has 11 children; only 6 are buffer atoms sw
//   has. The OTHER 5 (ComputeShader / ComputeShaderStage / StructuredBufferWithViews /
//   CalcDispatchCount / TransformMatrix) are the ops that actually DISPATCH the HLSL kernel — sw has
//   NO atom for them (they map to nothing → children skipped, wires touching them dropped). This
//   importer maps what CAN be mapped faithfully; the missing 5 are the exposed seam, not an importer
//   bug. See t3import_transformpoints_golden.cpp for the measured RED.
//
// ZONE: runtime (純計算). Pure CPU: JSON parse (crude_json) + three maps + SymbolLibrary fill.
#pragma once
#include <string>
#include <vector>

#include "runtime/compound_graph.h"  // SymbolLibrary / Symbol / SymbolChild / SymbolConnection

namespace sw {

// Import one .t3 (JSON text) into `lib` as a single Symbol. id == the .t3 top-level Id; inputDefs
// from Inputs[]; children/connections = the mapped subgraph. Every atom a child's SymbolId resolves
// to is also generated into `lib` (atomicSymbolFromSpec via findSpec). Returns false only when the
// JSON has no usable top-level Id. Local problems (unknown SymbolId, unresolvable slot/child guid)
// drop that element with a warning. `outSymbolId` receives the id; `warnings` collects skips.
bool importT3Symbol(const std::string& t3Json, SymbolLibrary& lib,
                    std::string* outSymbolId = nullptr,
                    std::vector<std::string>* warnings = nullptr);

// Test-only injection seam (routing RED case): reverse the connection order of the first MultiInput
// collision (two wires into the same (child,slot)) — corrupts ONLY the order, not the maps. Off in
// production. The keystone golden flips this around importT3Symbol.
bool& t3ImportInjectBug();

// KEYSTONE首證 golden (--selftest-t3-transformpoints): load the REAL TransformPoints.t3, walk the
// PRODUCTION path importT3Symbol → buildEvalGraph → cookResident, read back the point buffer, and
// compare against the焊死 oracle runTransformPointsParityProbe (xfprobe). Lives in
// t3import_transformpoints_golden.cpp. Returns 0 GREEN (parity holds) / 1 RED (seam gap). injectBug
// drives the importer's connection-order RED tooth. See that file for the measured result + the
// exact seam the keystone exposes.
int runT3TransformPointsParity(bool injectBug);

}  // namespace sw
