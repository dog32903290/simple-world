// runtime/resident_eval_patch — batch 1b / slice-3 first cut: incremental patch of the resident
// eval graph (承重決策 3 的結構面 + spec 健檢二補 patch version 規則組). Edits the graph in place
// and preserves cache on untouched nodes, so a patched graph evaluates identically to one rebuilt
// with the edit baked in (patch == rebuild). Split from resident_eval_graph.cpp so the flatten/eval
// engine stays one job (ARCHITECTURE rule 4). runtime leaf: resident_eval_graph.h only.
//
// The RESIDENT-level edits live here (S1 set-constant, S11① add / ② remove connection — the per-slot
// surgery, = TiXL Slot.AddConnection/RemoveConnection). The DEFINITION-level broadcast edits
// (add/remove child, change default, IO change) live in resident_eval_patch_lib.cpp.
//
// ⚠ Authority pairing duty (contract C3): these functions edit the resident PROJECTION only — the
// command layer must apply the matching SymbolLibrary edit alongside, or a later structural
// patchLib* (which re-derives the projection from the lib) will silently discard the resident-only
// edit. The goldens simulate the pairing; production commands own it.
#include "runtime/resident_eval_graph.h"

#include <cstdint>

namespace sw {

void patchSetConstant(ResidentEvalGraph& g, const std::string& path, const std::string& slotId,
                      float value) {
  auto it = g.byPath.find(path);
  if (it == g.byPath.end()) return;
  ResidentNode& n = g.nodes[it->second];
  bool consumed = false;
  for (ResidentInput& in : n.inputs)
    if (in.slotId == slotId) {
      // Write the value regardless of the driver (= TiXL SetTypedInputValue, InputSlot.cs:58-64:
      // the typed value is stored even while wired; a later disconnect falls back to it — refuter
      // A-3: filtering on Constant silently dropped the value and disconnect restored a stale one).
      in.constant = value;
      consumed = consumed || in.driver == ResidentInput::Driver::Constant;
    }
  // S1 value edit = edit-time push, ONLY when the value is actually consumed (Constant-driven).
  // A wired input stores the fallback without invalidating (nothing it feeds changed); a missing
  // slot bumps nothing (the old unconditional bump was a per-edit false-dirty). baseVersion is
  // never overwritten by the upstream sum, so this survives on derived nodes (refuter A4).
  if (!consumed) return;
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

void patchRemoveConnection(ResidentEvalGraph& g, const std::string& dstPath,
                           const std::string& dstSlot) {
  auto it = g.byPath.find(dstPath);
  if (it == g.byPath.end()) return;
  ResidentNode& n = g.nodes[it->second];
  uint64_t dropped = 0;
  bool changed = false;
  for (ResidentInput& in : n.inputs)
    if (in.slotId == dstSlot && in.driver == ResidentInput::Driver::Connection) {
      // The contribution this upstream was making to our pull-time sum (dangling counts as the
      // fixed 1, mirroring pullResidentFloat's D1 rule) — absorbed below so sourceVersion can't
      // decrease across the disconnect (monotonicity; see header 🪤).
      dropped = 1;
      if (const ResidentNode* up = g.node(in.srcNodePath)) {
        auto uc = up->outCache.find(in.srcSlotId);
        if (uc != up->outCache.end()) dropped = uc->second.sourceVersion;
      }
      // S11② restore the pre-connection driver (Slot.cs:233-245 RestoreUpdateAction): fall back to
      // Constant with the KEPT value — in.constant survived under the Connection (= TiXL keeps the
      // input's typed Value while wired; disconnect falls back to it).
      in.driver = ResidentInput::Driver::Constant;
      in.srcNodePath.clear();
      in.srcSlotId.clear();
      changed = true;
    }
  if (!changed) return;
  for (auto& kv : n.outCache) {
    kv.second.baseVersion += dropped + 1;  // absorb dropped contribution, then ForceInvalidate (++)
    kv.second.valueVersion = UINT64_MAX;   // belt & suspenders: never false-clean on the next pull
  }
}

}  // namespace sw
