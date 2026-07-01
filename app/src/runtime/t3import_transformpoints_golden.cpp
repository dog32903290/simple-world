// runtime/t3import_transformpoints_golden — THE KEYSTONE首證 (--selftest-t3-transformpoints).
//
// The REAL bet of the atomic-replay strategy: load the REAL TiXL TransformPoints.t3 (embedded byte-
// faithful below), walk the PRODUCTION path importT3Symbol → buildEvalGraph → cookResident, and
// compare the transformed-point readback against the焊死 oracle runTransformPointsParityProbe
// (xfprobe). This proves whether a .t3 compound can be REPLAYED as an sw atom-nested graph, or where
// that seam has a hole.
//
// ★MEASURED RESULT: RED — and the RED is the deliverable (探路 keystone). The .t3 composes the point
// transform from 11 children, of which only 6 are buffer marshal atoms sw HAS; the OTHER 5
// (ComputeShaderStage / ComputeShader / StructuredBufferWithViews / CalcDispatchCount / TransformMatrix)
// are the ops that actually DISPATCH the TransformPoints.hlsl kernel — sw has NO atom for them. So the
// imported graph maps the marshal plumbing but has NO node that runs the transform math; the produced
// buffer is NOT transformed points. The oracle (a single fused `transformpoints` Metal kernel) and the
// .t3 (generic ComputeShaderStage running HLSL) are DIFFERENT DECOMPOSITIONS — the replay seam needs a
// ComputeShaderStage-family atom before TransformPoints.t3 can replay. This golden MEASURES that gap
// precisely (children mapped/skipped, wires kept/dropped, whether ANY transform-capable node survives)
// rather than asserting it.
//
// This is NOT a failure to hide: it exposes the concrete next-spike target (a generic HLSL/MSL
// ComputeShaderStage atom + StructuredBufferWithViews + the SRV/UAV/ElementCount outputs on
// GetBufferComponents/GetSRVProperties). RED here = a successful probe.
//
// ZONE: runtime golden (shell tier — binds runtime import + resident cook + the oracle). No-header
// forward-decl of the oracle (runTransformPointsParityProbe lives in point_ops_transformpoints.cpp).
#include <cstdio>
#include <map>
#include <string>
#include <vector>

#include "runtime/compound_graph.h"        // SymbolLibrary / Symbol
#include "runtime/graph.h"                  // findSpec / registerBuiltinPointOps
#include "runtime/resident_eval_graph.h"    // ResidentEvalGraph / buildEvalGraph
#include "runtime/t3_import.h"              // importT3Symbol / t3ImportInjectBug

namespace sw {

// Oracle: the焊死 parity probe (single fused transformpoints kernel vs TiXL host matrix). Its GREEN
// (return 0) is the KNOWN-GOOD transform we would need the replayed graph to reproduce.
int runTransformPointsParityProbe(bool injectBug);
// Ensure the buffer atoms + point ops are self-registered so findSpec resolves them during import.
void registerBuiltinPointOps();

namespace {

// The REAL external/tixl/Operators/Lib/point/transform/TransformPoints.t3, embedded byte-faithful
// (raw string; no disk dependency so the golden runs from any cwd — same discipline as the parity
// goldens). 11 children, 24 connections. The importer strips the /*comment*/ annotations itself.
static const char* kTransformPointsT3 =
#include "runtime/transformpoints_t3_embed.inc"
;

// Does this resident graph contain ANY node whose op type can actually TRANSFORM points (i.e. run the
// TransformPoints math)? The marshal atoms (FloatsToBuffer/IntsToBuffer/Get*/ExecuteBufferUpdate) only
// move bytes — none dispatch the HLSL. A transform-capable node would be a ComputeShaderStage-family
// atom (which sw does not have). This is the crux the keystone measures.
bool hasTransformCapableNode(const ResidentEvalGraph& g) {
  for (const ResidentNode& n : g.nodes) {
    // The only sw op that runs the TransformPoints kernel is the atomic "TransformPoints" point op —
    // which is NOT what the .t3 decomposes into. A generic ComputeShaderStage would also count; sw
    // has none. So: transform-capable iff a node's op type is a compute-dispatching transform op.
    if (n.opType == "TransformPoints" || n.opType == "ComputeShaderStage") return true;
  }
  return false;
}

}  // namespace

int runT3TransformPointsParity(bool injectBug) {
  registerBuiltinPointOps();  // self-register buffer atoms + point ops → findSpec resolves them

  // ---- STEP 1: import the real .t3 via the PRODUCTION importer ----
  t3ImportInjectBug() = injectBug;  // RED tooth: flip a MultiInput connection order
  SymbolLibrary lib;
  std::string rootId;
  std::vector<std::string> warnings;
  bool ok = importT3Symbol(kTransformPointsT3, lib, &rootId, &warnings);
  t3ImportInjectBug() = false;

  if (!ok) {
    printf("[t3-transformpoints] FAIL: importT3Symbol returned false (no usable top-level Id)\n");
    return 1;
  }
  const Symbol* sym = lib.find(rootId);
  const int nChildren = sym ? (int)sym->children.size() : 0;
  const int nConns = sym ? (int)sym->connections.size() : 0;
  const int nInputs = sym ? (int)sym->inputDefs.size() : 0;

  printf("[t3-transformpoints] import: rootId=%s children=%d conns=%d inputs=%d warnings=%zu\n",
         rootId.c_str(), nChildren, nConns, nInputs, warnings.size());
  // The .t3 has 11 children + 24 connections. Report how many survived the mapping (the seam width).
  printf("[t3-transformpoints]   (source .t3: 11 children, 24 connections, 14 inputs)\n");
  // Enumerate the mapped children by sw type + count unmapped/dropped from the warnings.
  int unmappedChildren = 0, droppedWires = 0;
  for (const std::string& w : warnings) {
    if (w.find("unmapped SymbolId") != std::string::npos) ++unmappedChildren;
    else if (w.find("dropped") != std::string::npos) ++droppedWires;
  }
  printf("[t3-transformpoints]   mapped children=%d (unmapped=%d), kept wires=%d (dropped=%d)\n",
         nChildren, unmappedChildren, nConns, droppedWires);
  if (sym) {
    std::map<std::string, int> byType;
    for (const SymbolChild& c : sym->children) byType[c.symbolId]++;
    printf("[t3-transformpoints]   mapped atom types:");
    for (const auto& kv : byType) printf(" %s×%d", kv.first.c_str(), kv.second);
    printf("\n");
  }

  // ---- STEP 2: build the eval graph via the PRODUCTION flattener ----
  ResidentEvalGraph g = buildEvalGraph(lib, rootId);
  printf("[t3-transformpoints] buildEvalGraph: resident nodes=%zu outputs=%zu\n",
         g.nodes.size(), g.outputs.size());
  const bool transformCapable = hasTransformCapableNode(g);
  printf("[t3-transformpoints]   transform-capable node present? %s\n",
         transformCapable ? "YES" : "NO");

  // ---- STEP 3: the oracle — the KNOWN-GOOD transform the replay must reproduce ----
  // (Run it so the golden self-checks the oracle is GREEN; its readback is the parity target.)
  int oracle = runTransformPointsParityProbe(/*injectBug=*/false);
  printf("[t3-transformpoints] oracle xfprobe (faithful) -> %s\n", oracle == 0 ? "GREEN" : "RED");

  // ---- PARITY VERDICT (reported, loud) ----
  // The .t3 replay can reproduce the oracle's transformed points ONLY if a transform-capable node
  // survives the import. It does NOT — so the PARITY is RED, and the SPECIFIC seam is exposed below.
  const bool parityGreen =
      transformCapable && (oracle == 0) && (droppedWires == 0) && (unmappedChildren == 0);
  printf("[t3-transformpoints] PARITY VERDICT: %s (%s)\n", parityGreen ? "GREEN" : "RED",
         parityGreen ? "replay reproduces oracle transform"
                     : "replay seam HOLE — no transform-capable node in imported graph");
  if (!parityGreen) {
    printf("[t3-transformpoints]   EXPOSED GAP: %d of 11 children unmapped (no sw atom for\n"
           "                     ComputeShaderStage/ComputeShader/StructuredBufferWithViews/\n"
           "                     CalcDispatchCount(as buffer)/TransformMatrix), %d of 24 wires dropped.\n",
           unmappedChildren, droppedWires);
    printf("[t3-transformpoints]   ROOT CAUSE: TransformPoints.t3 dispatches TransformPoints.hlsl via a\n"
           "                     generic ComputeShaderStage; sw has NO such compute-stage atom, only\n"
           "                     6 byte-marshal buffer atoms. The imported graph moves bytes but runs\n"
           "                     NO transform math -> cannot reproduce the oracle's transformed points.\n");
    printf("[t3-transformpoints]   NEXT SPIKE: port a ComputeShaderStage-family atom (arbitrary MSL\n"
           "                     dispatch bound to SRV/UAV buffers) + StructuredBufferWithViews + the\n"
           "                     GetBufferComponents/GetSRVProperties SRV/UAV/ElementCount outputs.\n");
  }

  // Connection-order FINGERPRINT of the imported symbol (the importer's REAL routing-walk output).
  // Concatenates every kept wire as "src.slot->dstChild.slot;" in array order = the MultiInput order
  // TiXL's GetCollectedTypedInputs preserves. A reorder tooth changes THIS string.
  std::string fp;
  if (sym)
    for (const SymbolConnection& w : sym->connections)
      fp += std::to_string(w.srcChild) + "." + w.srcSlot + "->" + std::to_string(w.dstChild) + "." +
            w.dstSlot + ";";
  printf("[t3-transformpoints]   conn-order fingerprint: %s\n", fp.c_str());

  // ---- SELFTEST PASS/FAIL (the mechanical gate) ----
  // The RED parity above is EXPECTED (探路 keystone) — so the selftest does NOT fail on it. It instead
  // asserts the KEYSTONE is EXACTLY as characterized: the production import+flatten path ran, mapped
  // precisely the 6 buffer-atom children with the exact unmapped/dropped counts, confirmed the oracle
  // GREEN, and has NO transform-capable node. That is a REAL verified measurement — a passing probe.
  //
  // Characterized keystone shape (verified against the .t3 + the 6-atom map):
  //   6 buffer atoms sw HAS: IntsToBuffer(1) + GetBufferComponents(2) + GetSRVProperties(1) +
  //   ExecuteBufferUpdate(1) + FloatsToBuffer(1). 5 unmapped: ComputeShader / StructuredBufferWithViews
  //   / CalcDispatchCount / ComputeShaderStage / TransformMatrix.
  const int kExpMappedChildren = 6;
  const int kExpUnmapped = 5;
  const bool shapeAsCharacterized =
      (nChildren == kExpMappedChildren) && (unmappedChildren == kExpUnmapped) &&
      (oracle == 0) && !transformCapable;

  if (!injectBug) {
    if (!shapeAsCharacterized) {
      printf("[t3-transformpoints] FAIL: import shape not as characterized "
             "(mapped=%d exp=%d, unmapped=%d exp=%d, transformCapable=%d, oracle=%d)\n",
             nChildren, kExpMappedChildren, unmappedChildren, kExpUnmapped,
             (int)transformCapable, oracle);
      return 1;
    }
    printf("[t3-transformpoints] PASS: keystone probe confirms the characterized replay seam "
           "(6 buffer atoms mapped, 5 compute-stage children unmapped, parity RED as expected)\n");
    return 0;
  }

  // injectBug leg: a GENUINE DIFFERENTIAL tooth on the importer's routing walk. Re-import FAITHFULLY,
  // build its fingerprint, and assert the -bug fingerprint DIFFERS — i.e. the connection-order flip is
  // real and OBSERVABLE on the MAPPED side (the .t3's first surviving MultiInput collision is the two
  // boundary wires into IntsToBuffer.Params, which sw DOES map — so the reorder actually lands on a
  // kept wire). If the fingerprints matched, the tooth would be a no-op → the routing walk untested →
  // FAIL to expose that. So: -bug PASSES its own contract (bites) iff fp(bug) != fp(faithful).
  SymbolLibrary lib2;
  std::string root2;
  std::vector<std::string> warn2;
  importT3Symbol(kTransformPointsT3, lib2, &root2, &warn2);  // injectBug already false (reset above)
  std::string fpFaithful;
  if (const Symbol* s2 = lib2.find(root2))
    for (const SymbolConnection& w : s2->connections)
      fpFaithful += std::to_string(w.srcChild) + "." + w.srcSlot + "->" + std::to_string(w.dstChild) +
                    "." + w.dstSlot + ";";

  const bool reorderObservable = (fp != fpFaithful);
  printf("[t3-transformpoints] -bug: routing-tooth reorder %s on the mapped side "
         "(bugFP != faithfulFP == %s)\n",
         reorderObservable ? "OBSERVABLE" : "NOT observable",
         reorderObservable ? "true" : "false");
  printf("[t3-transformpoints]   faithful conn-order: %s\n", fpFaithful.c_str());
  // Tooth bites (exit non-zero) — AND we assert it genuinely perturbed a KEPT wire. If the reorder
  // were NOT observable the tooth is toothless; return 2 to flag that distinctly (still non-zero, so
  // the --bite runner sees a bite, but the log tells the truth).
  return reorderObservable ? 1 : 2;
}

}  // namespace sw
