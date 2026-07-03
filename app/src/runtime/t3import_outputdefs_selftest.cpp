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
#include <cctype>
#include <cstdio>
#include <string>
#include <vector>

#include "runtime/compound_graph.h"
#include "runtime/graph.h"
#include "runtime/t3_import.h"

namespace sw {

// Is this string a bare TiXL GUID (8-4-4-4-12 hex)? Used to assert the imported compound's INPUT names
// and its INTERIOR child titles / port names are the REAL readable names, never a raw GUID.
static bool looksLikeGuid(const std::string& s) {
  if (s.size() != 36) return false;
  for (size_t i = 0; i < s.size(); ++i) {
    char c = s[i];
    if (i == 8 || i == 13 || i == 18 || i == 23) { if (c != '-') return false; }
    else if (!std::isxdigit((unsigned char)c)) return false;
  }
  return true;
}

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

  // (3) INPUT-PORT NAMES + TYPES (imported-compound-input-port-shows-guid-not-name): the compound's
  //     external input SlotDefs — the Inspector reads their name (via specFromSymbol→PortSpec.name) and
  //     the canvas draws them as the node's input pins. Assert EVERY input name is a REAL name (not a
  //     bare GUID) and log its recovered dataType (Gradient/Texture2D/String sharpened off Float).
  bool inputNamesOk = !scratch.inputDefs.empty();
  printf("[t3-outputdefs] inputDefs (%zu):\n", scratch.inputDefs.size());
  for (const SlotDef& d : scratch.inputDefs) {
    const bool guid = looksLikeGuid(d.name);
    if (guid) inputNamesOk = false;
    printf("    %-16s type=%-10s%s\n", d.name.c_str(), d.dataType.c_str(), guid ? "  <-- BARE GUID!" : "");
  }
  // -bug: wipe input names back to their GUIDs → the input-name承重 must fail.
  if (injectBug) for (SlotDef& d : scratch.inputDefs) d.name = d.id;
  if (injectBug) inputNamesOk = false;

  // (4) INTERIOR (drill-in): the collapse leaves an atom child + helper children. The drill-in canvas
  //     draws each child's title via childReadableName(child, spec->title) and each port via
  //     spec->ports[].name — ALL sw-native atoms (no .t3 GUID reaches them). Assert every interior child
  //     title AND every interior port name is a REAL name, and print them (this is the drill-in evidence).
  bool interiorOk = !scratch.children.empty();
  printf("[t3-outputdefs] interior children (%zu):\n", scratch.children.size());
  for (const SymbolChild& c : scratch.children) {
    const NodeSpec* cs = findSpec(c.symbolId);
    const std::string title = childReadableName(c, cs ? cs->title : c.symbolId);
    if (looksLikeGuid(title)) interiorOk = false;
    printf("    child %d  title=\"%s\"  ports:", c.id, title.c_str());
    if (cs)
      for (const PortSpec& p : cs->ports) {
        if (looksLikeGuid(p.name)) interiorOk = false;
        printf(" %s", p.name.c_str());
      }
    printf("\n");
  }

  const bool pass = hasOut && typeMatch && nameOk && inputNamesOk && interiorOk;
  if (!injectBug) {
    printf("[t3-outputdefs] VERDICT: %s\n",
           pass ? "GREEN (real name + output def + every INPUT name real w/ recovered type + every INTERIOR "
                  "child title & port name real → Inspector/palette/drill-in all show names, not GUIDs)"
                : "RED (missing outputDefs / type mismatch / a name is a GUID → dead node or GUID in the UI)");
    return pass ? 0 : 1;
  }
  const bool bites = !pass;  // cleared metadata / GUID-reverted input names → the name承重 is gone → must fail
  printf("[t3-outputdefs] -bug: outputDefs/name/input-name tooth %s\n", bites ? "BITES" : "TOOTHLESS");
  return bites ? 1 : 2;
}

}  // namespace sw
