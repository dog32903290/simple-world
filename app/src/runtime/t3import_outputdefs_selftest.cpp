// runtime/t3import_outputdefs_selftest (--selftest-t3-outputdefs) — METADATA承重 proof for the
// catalog-node path (imported-compound-needs-outputdefs-to-be-draggable-child +
// imported-compound-shows-guid-not-name). The gradient/parity goldens VIEW the collapsed atom child
// directly (residentTexFor(atomId)), so they never exercised the COMPOUND's own external metadata:
//   (1) sym.outputDefs — without it a dragged compound child has NO output PIN (specFromSymbol emits
//       output ports from outputDefs) AND viewProducerPath bails on `s->outputDefs.empty()` → black cook.
//   (2) sym.name — TiXL hides the readable name in the top-level Id's inline /*Name*/ comment, which
//       stripT3Comments deletes → the Cmd+F palette would show a raw GUID.
// This asserts BOTH on the REAL RadialGradient.t3 through the PRODUCTION importer (collapse path).
// -bug: clear the just-filled outputDefs → the pin/view承重 is gone → the assertion BITES.
//
// ZONE: runtime selftest (pure CPU — import only, no Metal, no cook).
#include <cstdio>
#include <string>
#include <vector>

#include "runtime/compound_graph.h"
#include "runtime/graph.h"
#include "runtime/t3_import.h"

namespace sw {
namespace {
static const char* kRadialGradientT3 =
#include "runtime/radialgradient_t3_embed.inc"
;
}  // namespace

int runT3ImportOutputDefsSelfTest(bool injectBug) {
  SymbolLibrary lib;
  std::string rootId;
  std::vector<std::string> warnings;
  const bool imported = importT3Symbol(kRadialGradientT3, lib, &rootId, &warnings);
  const Symbol* sym = imported ? lib.find(rootId) : nullptr;
  if (!sym) { printf("[t3-outputdefs] FAIL: import returned no symbol\n"); return 1; }

  // -bug: erase the metadata the importer just filled → simulate the pre-fix dead-child state.
  Symbol scratch = *sym;
  if (injectBug) { scratch.outputDefs.clear(); scratch.name = scratch.id; }

  // (1) outputDefs非空 且 型別==終端 atom output. The terminal atom is the "RadialGradient" child; its
  //     OUTPUT PortSpec dataType is what the compound's output def must carry (Texture2D for image-fx).
  std::string atomType;
  for (const SymbolChild& c : scratch.children)
    if (c.symbolId == "RadialGradient") { atomType = c.symbolId; break; }
  std::string atomOutType;
  if (const NodeSpec* fs = findSpec(atomType))
    for (const PortSpec& p : fs->ports)
      if (!p.isInput) { atomOutType = p.dataType; break; }

  const bool hasOut = !scratch.outputDefs.empty();
  const std::string outType = hasOut ? scratch.outputDefs[0].dataType : std::string();
  const bool typeMatch = hasOut && !atomOutType.empty() && outType == atomOutType;

  // (2) name是真名非 GUID (與 sym.id 不同、且是 "RadialGradient").
  const bool nameOk = scratch.name == "RadialGradient" && scratch.name != scratch.id;

  printf("[t3-outputdefs] name=\"%s\" (id=%s) outputDefs=%zu out[0].type=%s atom(%s).out=%s\n",
         scratch.name.c_str(), scratch.id.c_str(), scratch.outputDefs.size(),
         outType.c_str(), atomType.c_str(), atomOutType.c_str());

  const bool pass = hasOut && typeMatch && nameOk;
  if (!injectBug) {
    printf("[t3-outputdefs] VERDICT: %s\n",
           pass ? "GREEN (imported compound carries an output def of the terminal atom's type + its real name "
                  "→ draggable catalog node with an output PIN)"
                : "RED (missing outputDefs / type mismatch / name is a GUID → dead node in the palette)");
    return pass ? 0 : 1;
  }
  const bool bites = !pass;  // cleared metadata → the pin/view/name承重 is gone → assertion must fail
  printf("[t3-outputdefs] -bug: outputDefs/name tooth %s\n", bites ? "BITES" : "TOOTHLESS");
  return bites ? 1 : 2;
}

}  // namespace sw
