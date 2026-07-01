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
// ★SCOPE (compute-stage keystone — now GREEN): TransformPoints.t3's 11 children ALL map now. The 5
//   formerly-unmapped ones gained sw atoms: ComputeShaderStage (generic bind CB/SRV/UAV + dispatch),
//   StructuredBufferWithViews (allocate the UAV target), TransformMatrix (existing SRT math atom),
//   CalcDispatchCount (existing value op; dispatch folded into the stage), and ComputeShader — which has
//   NO standalone atom: its Source string FOLDS onto the ComputeShaderStage's KernelName (fork
//   computeshader-source-folded-onto-stage, the post-pass in importT3Symbol). A handful of wires still
//   drop honestly (Dispatch/DispatchCount/ThreadGroupSize/CS-handle — folded or derived, no sw slot).
//   The replay reproduces the transform to parity — see t3import_transformpoints_golden.cpp (GREEN).
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
