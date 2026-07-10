// runtime/t3import_srvtexfold_golden — --selftest-t3-srvtexfold (TEXTURE-COMPUTE SEAM STAGE 3 importer:
// the BUFFER-RAIL SrvFromTexture2d fold, TEXTURE_COMPUTE_SEAM_SPEC.md §5 row 3 "留白" closed).
//
// Stage 3 (t3import_getimagebrightness_golden.cpp) proved the COOK side reads a texture SRV on the buffer
// rail (cross-currency gather + setTexture + default sampler + 2D dispatch), but it built the compound
// PROGRAMMATICALLY — the .t3 IMPORTER couldn't fold SrvFromTexture2d onto the stage's ShaderResourceTextures
// port (SrvFromTexture2d was "unmapped skipped", the texture-SRV wire lost). appendSrvFromTexFold
// (t3_import_srvtexfold.cpp) closes that: SrvFromTexture2d.out → Stage.ShaderResources is re-anchored to
// <texture source> → Stage.ShaderResourceTextures (DX11 packs buffer-SRV + texture-SRV into one t-space;
// sw splits them onto separate Metal spaces).
//
// STRUCTURAL golden (no cook — this seals the JSON COLLAPSE, the cook is sealed by stage 3). A minimal
// synthetic buffer-rail compute .t3 (boundary Texture → SrvFromTexture2d → Stage.ShaderResources +
// boundary GPoints → Stage.ShaderResources [buffer-SRV, stays] + ComputeShader.Source folded). The real
// target SamplePointColorAttributes.t3 additionally routes its texture through UseFallbackTexture (an
// unmapped fallback-glue op) whose elision is a later refinement, and PointsFromMeshData.t3 wires no
// texture at all — hence a synthetic .t3 exercises the fold in isolation (per §5 row 3's fallback).
//
// TWO gates, each a MEASURED RED→GREEN tooth (GOLDEN_STANDARD.md 三特徵):
//  ① TEX FOLD: import → the ComputeShaderStage child gains a ShaderResourceTextures wire whose SOURCE is
//     the boundary Texture (the SrvFromTexture2d texture source, re-anchored). injectBug =
//     t3SrvTexFoldDisable() → SrvFromTexture2d stays unmapped, the wire is lost → BITE.
//  ② BUFFER-SRV UNTOUCHED: the buffer-SRV wire (boundary GPoints → Stage.ShaderResources) is unchanged by
//     the fold (the texture-SRV rides ShaderResourceTextures, the buffer-SRV keeps ShaderResources —
//     separate Metal spaces). Same injectBug: with the fold off, ONLY the texture wire is missing; the
//     buffer wire is always present, so ② stays GREEN under -bug (it is NOT a tooth for this injectBug —
//     it is a REACHABILITY invariant, asserted both ways to prove the fold does not disturb the buffer rail).
#include <cstdio>
#include <string>
#include <vector>

#include "runtime/compound_graph.h"  // Symbol / SymbolLibrary / SymbolConnection / kSymbolBoundary
#include "runtime/graph.h"
#include "runtime/t3_import.h"

namespace sw {

void registerBuiltinPointOps();

namespace {

// A minimal buffer-rail compute compound that reads a Texture2D SRV. Boundary Texture → SrvFromTexture2d →
// Stage.ShaderResources (the fold's input); boundary GPoints → Stage.ShaderResources (a buffer-SRV, stays);
// ComputeShader.Source → Stage.KernelName (folded); Stage.Output → boundary Out. Real glue guids
// (ComputeShaderStage 8bef116d / ComputeShader a256d70f / SrvFromTexture2d c2078514 + their slots).
const char* const kT3 = R"JSON({
  "Id": "0f01dab0-0000-4000-8000-000000000001",
  "Inputs": [
    { "Id": "0f01dab0-0000-4000-8000-0000000000b1", "DefaultValue": null },
    { "Id": "0f01dab0-0000-4000-8000-0000000000b2", "DefaultValue": null }
  ],
  "Children": [
    { "Id": "0f01dab0-0000-4000-8000-0000000000a1", "SymbolId": "8bef116d-7d1c-4c1b-b902-25c1d5e925a9", "InputValues": [] },
    { "Id": "0f01dab0-0000-4000-8000-0000000000a2", "SymbolId": "a256d70f-adb3-481d-a926-caf35bd3e64c",
      "InputValues": [ { "Id": "afb69c81-5063-4cb9-9d42-841b994b5ec0", "Type": "System.String",
                         "Value": "Lib:shaders/points/modify/SamplePointColorAttributes.hlsl" } ] },
    { "Id": "0f01dab0-0000-4000-8000-0000000000a3", "SymbolId": "c2078514-cf1d-439c-a732-0d7b31b5084a", "InputValues": [] }
  ],
  "Connections": [
    { "SourceParentOrChildId": "00000000-0000-0000-0000-000000000000", "SourceSlotId": "0f01dab0-0000-4000-8000-0000000000b1",
      "TargetParentOrChildId": "0f01dab0-0000-4000-8000-0000000000a3", "TargetSlotId": "d5afa102-2f88-431e-9cd4-af91e41f88f6" },
    { "SourceParentOrChildId": "0f01dab0-0000-4000-8000-0000000000a3", "SourceSlotId": "dc71f39f-3fba-4fc6-b8ef-ce57c82bf78e",
      "TargetParentOrChildId": "0f01dab0-0000-4000-8000-0000000000a1", "TargetSlotId": "88938b09-d5a7-437c-b6e1-48a5b375d756" },
    { "SourceParentOrChildId": "00000000-0000-0000-0000-000000000000", "SourceSlotId": "0f01dab0-0000-4000-8000-0000000000b2",
      "TargetParentOrChildId": "0f01dab0-0000-4000-8000-0000000000a1", "TargetSlotId": "88938b09-d5a7-437c-b6e1-48a5b375d756" },
    { "SourceParentOrChildId": "0f01dab0-0000-4000-8000-0000000000a2", "SourceSlotId": "6c118567-8827-4422-86cc-4d4d00762d87",
      "TargetParentOrChildId": "0f01dab0-0000-4000-8000-0000000000a1", "TargetSlotId": "5c0e9c96-9aba-4757-ae1f-cc50fb6173f1" },
    { "SourceParentOrChildId": "0f01dab0-0000-4000-8000-0000000000a1", "SourceSlotId": "c382284f-7e37-4eb0-b284-bc735247f26b",
      "TargetParentOrChildId": "00000000-0000-0000-0000-000000000000", "TargetSlotId": "0f01dab0-0000-4000-8000-0000000000c1" }
  ]
})JSON";

const char* const kGuid = "0f01dab0-0000-4000-8000-000000000001";
const char* const kTextureInputId = "0f01dab0-0000-4000-8000-0000000000b1";  // boundary Texture → SrvFromTexture2d
const char* const kBufferInputId = "0f01dab0-0000-4000-8000-0000000000b2";   // boundary GPoints (buffer-SRV)

int childIdOfType(const Symbol& s, const std::string& type) {
  for (const SymbolChild& c : s.children) if (c.symbolId == type) return c.id;
  return 0;
}
bool hasWire(const Symbol& s, int srcChild, const std::string& srcSlot, int dstChild, const std::string& dstSlot) {
  for (const SymbolConnection& c : s.connections)
    if (c.srcChild == srcChild && c.srcSlot == srcSlot && c.dstChild == dstChild && c.dstSlot == dstSlot)
      return true;
  return false;
}

}  // namespace

int runT3SrvTexFoldGates(bool injectBug) {
  registerBuiltinPointOps();
  t3SrvTexFoldDisable() = injectBug;  // -bug: fold off → SrvFromTexture2d stays unmapped, texture wire lost

  SymbolLibrary lib; std::string rootId; std::vector<std::string> warnings;
  const bool imported = importT3Symbol(kT3, lib, &rootId, &warnings) && rootId == std::string(kGuid);
  t3SrvTexFoldDisable() = false;

  const Symbol* sym = lib.find(kGuid);
  const int stageId = sym ? childIdOfType(*sym, "ComputeShaderStage") : 0;
  if (!imported || !sym || !stageId) {
    printf("[srvtexfold] FAIL: import/stage (imported=%d stage=%d)\n", imported ? 1 : 0, stageId);
    return injectBug ? 0 : 1;  // a broken import is not a clean tooth
  }

  // ① TEX FOLD: boundary Texture → stage.ShaderResourceTextures (the re-anchored texture-SRV).
  const bool texFolded = hasWire(*sym, kSymbolBoundary, kTextureInputId, stageId, "ShaderResourceTextures");
  // ② BUFFER-SRV UNTOUCHED: boundary GPoints → stage.ShaderResources (the buffer-SRV, always present).
  const bool bufKept = hasWire(*sym, kSymbolBoundary, kBufferInputId, stageId, "ShaderResources");
  // Bonus: the ComputeShader.Source folded onto the stage's KernelName.
  bool kernelFolded = false;
  for (const SymbolChild& c : sym->children)
    if (c.id == stageId) { auto it = c.strOverrides.find("KernelName"); kernelFolded = it != c.strOverrides.end() && !it->second.empty(); }

  printf("[srvtexfold] ①texFolded=%d ②bufKept=%d kernelFolded=%d -> %s\n",
         texFolded ? 1 : 0, bufKept ? 1 : 0, kernelFolded ? 1 : 0,
         injectBug ? (texFolded ? "TOOTHLESS" : "BITES") : ((texFolded && bufKept && kernelFolded) ? "GREEN" : "RED"));

  if (!injectBug) {
    const bool green = texFolded && bufKept && kernelFolded;
    printf("[srvtexfold] VERDICT: %s\n",
           green ? "PASS (buffer-rail SrvFromTexture2d folds onto ShaderResourceTextures)" : "FAIL");
    return green ? 0 : 1;
  }
  // -bug: the fold is off → the texture wire must be GONE (①bites); the buffer-SRV must still be present.
  const bool bites = !texFolded && bufKept;
  printf("[srvtexfold] -bug VERDICT: %s\n", bites ? "TOOTH BITES (texture wire lost, buffer wire intact)" : "DEAD TOOTH");
  return bites ? 1 : 0;
}

}  // namespace sw
