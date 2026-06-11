// runtime/resident_eval_patch — batch 1b / slice-3 first cut: incremental patch of the resident
// eval graph (承重決策 3 的結構面 + spec 健檢二補 patch version 規則組). Edits the graph in place
// and preserves cache on untouched nodes, so a patched graph evaluates identically to one rebuilt
// with the edit baked in (patch == rebuild). Split from resident_eval_graph.cpp so the flatten/eval
// engine stays one job (ARCHITECTURE rule 4). runtime leaf: resident_eval_graph.h only.
//
// Two edits land here (the rest of the six S11 operations — disconnect, add/remove child, change
// definition default, IO change — are named-deferred to later slice-3 cuts).
#include "runtime/resident_eval_graph.h"

#include <cstdint>

namespace sw {

void patchSetConstant(ResidentEvalGraph& g, const std::string& path, const std::string& slotId,
                      float value) {
  auto it = g.byPath.find(path);
  if (it == g.byPath.end()) return;
  ResidentNode& n = g.nodes[it->second];
  for (ResidentInput& in : n.inputs)
    if (in.slotId == slotId && in.driver == ResidentInput::Driver::Constant) in.constant = value;
  // S1 value edit = edit-time push: bump THIS node's own baseVersion so it (and, via the pull-time
  // upstream sum, its downstream cone) recomputes. baseVersion is never overwritten by the upstream
  // sum, so this survives even when the node is derived (refuter A4). Untouched nodes keep cache.
  for (auto& kv : n.outCache) kv.second.baseVersion++;
}

void patchAddConnection(ResidentEvalGraph& g, const std::string& dstPath, const std::string& dstSlot,
                        const std::string& srcPath, const std::string& srcSlot) {
  auto it = g.byPath.find(dstPath);
  if (it == g.byPath.end()) return;
  ResidentNode& n = g.nodes[it->second];
  for (ResidentInput& in : n.inputs)
    if (in.slotId == dstSlot) {
      in.driver = ResidentInput::Driver::Connection;
      in.srcNodePath = srcPath;
      in.srcSlotId = srcSlot;
    }
  // S11① add connection (spec 健檢二補 ②): force the dst's outputs to first-pull-recompute by
  // setting valueVersion to the never-matching sentinel (= TiXL ValueVersion=-1). NOT a
  // sourceVersion bump — a derived slot's sourceVersion is the pull-time upstream sum, and bumping
  // it here would corrupt that arithmetic. The next pull adopts the real summed version and clears.
  for (auto& kv : n.outCache) kv.second.valueVersion = UINT64_MAX;
}

}  // namespace sw
