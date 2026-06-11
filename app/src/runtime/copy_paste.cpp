// runtime/copy_paste — copy/paste data semantics (契約 4 第三刀). See copy_paste.h.
#include "runtime/copy_paste.h"

#include <algorithm>
#include <cfloat>

#include "crude_json.h"

namespace sw {
namespace {

bool inSet(const std::vector<int>& ids, int id) {
  return std::find(ids.begin(), ids.end(), id) != ids.end();
}

}  // namespace

ClipboardData extractClipboard(const Symbol& src, const std::vector<int>& childIds) {
  ClipboardData clip;

  // 1) Gather the selected children (in the symbol's child order so paste numbering is stable),
  //    and compute the selection's upper-left corner for relative positioning (TiXL .cs:77-95).
  std::vector<int> picked;
  float ulX = FLT_MAX, ulY = FLT_MAX;
  for (const SymbolChild& c : src.children) {
    if (c.id == kSymbolBoundary || !inSet(childIds, c.id)) continue;
    picked.push_back(c.id);
    ulX = std::min(ulX, c.x);
    ulY = std::min(ulY, c.y);
  }
  if (picked.empty()) return clip;  // nothing selected -> empty clipboard

  for (const SymbolChild& c : src.children) {
    if (!inSet(picked, c.id)) continue;
    ClipboardChild cc;
    cc.id = c.id;
    cc.symbolId = c.symbolId;
    cc.overrides = c.overrides;     // FULL per-instance state our model carries (see header FORK)
    cc.relX = c.x - ulX;
    cc.relY = c.y - ulY;
    clip.children.push_back(std::move(cc));
  }

  // 2) Only wires whose BOTH endpoints are selected children survive — external wires are cut
  //    (TiXL .cs:100-109: the double `where con.Source/Target in selection`). A boundary-sentinel
  //    side (childId 0) is never in `picked`, so boundary-crossing wires drop automatically.
  //    Source order preserved == multi-input order on this end (paste reverses to restore it).
  for (const SymbolConnection& w : src.connections)
    if (inSet(picked, w.srcChild) && inSet(picked, w.dstChild))
      clip.wires.push_back(w);

  return clip;
}

std::string clipboardToJson(const ClipboardData& clip) {
  crude_json::object root;
  root["clipboardVersion"] = (crude_json::number)1;

  crude_json::array children;
  for (const ClipboardChild& c : clip.children) {
    crude_json::object co;
    co["id"] = (crude_json::number)c.id;
    co["symbolId"] = c.symbolId;
    crude_json::object ov;
    for (const auto& kv : c.overrides) ov[kv.first] = (crude_json::number)kv.second;
    co["overrides"] = crude_json::value(ov);
    co["relX"] = (crude_json::number)c.relX;
    co["relY"] = (crude_json::number)c.relY;
    children.push_back(crude_json::value(co));
  }
  root["children"] = crude_json::value(children);

  crude_json::array wires;
  for (const SymbolConnection& w : clip.wires) {
    crude_json::object wo;
    wo["srcChild"] = (crude_json::number)w.srcChild;
    wo["srcSlot"] = w.srcSlot;
    wo["dstChild"] = (crude_json::number)w.dstChild;
    wo["dstSlot"] = w.dstSlot;
    wires.push_back(crude_json::value(wo));
  }
  root["wires"] = crude_json::value(wires);

  return crude_json::value(root).dump(2);
}

bool clipboardFromJson(const std::string& json, ClipboardData& out) {
  out = ClipboardData{};
  if (json.empty()) return false;
  crude_json::value v = crude_json::value::parse(json);
  if (!v.is_object()) return false;
  if (!v["clipboardVersion"].is_number()) return false;  // not our clipboard -> clean no-op

  if (v["children"].is_array()) {
    for (auto& cv : v["children"].get<crude_json::array>()) {
      ClipboardChild c;
      c.id = cv["id"].is_number() ? (int)cv["id"].get<crude_json::number>() : 0;
      if (c.id <= 0) continue;  // boundary sentinel / garbage id
      c.symbolId = cv["symbolId"].is_string() ? cv["symbolId"].get<crude_json::string>() : "";
      if (c.symbolId.empty()) continue;
      if (cv["overrides"].is_object())
        for (auto& kv : cv["overrides"].get<crude_json::object>())
          if (kv.second.is_number()) c.overrides[kv.first] = (float)kv.second.get<crude_json::number>();
      if (cv["relX"].is_number()) c.relX = (float)cv["relX"].get<crude_json::number>();
      if (cv["relY"].is_number()) c.relY = (float)cv["relY"].get<crude_json::number>();
      out.children.push_back(std::move(c));
    }
  }
  if (v["wires"].is_array()) {
    for (auto& wv : v["wires"].get<crude_json::array>()) {
      SymbolConnection w;
      w.srcChild = wv["srcChild"].is_number() ? (int)wv["srcChild"].get<crude_json::number>() : -1;
      w.dstChild = wv["dstChild"].is_number() ? (int)wv["dstChild"].get<crude_json::number>() : -1;
      if (wv["srcSlot"].is_string()) w.srcSlot = wv["srcSlot"].get<crude_json::string>();
      if (wv["dstSlot"].is_string()) w.dstSlot = wv["dstSlot"].get<crude_json::string>();
      out.wires.push_back(w);
    }
  }
  return !out.children.empty();
}

PastePlan planPaste(const SymbolLibrary& lib, const std::string& targetId,
                    const ClipboardData& clip, float pasteX, float pasteY) {
  PastePlan plan;
  const Symbol* target = lib.find(targetId);
  if (!target) return plan;

  // ── 曲线 / S3 SEAM ──────────────────────────────────────────────────────────────────────
  // TiXL copies animations HERE (CopySymbolChildrenCommand.cs:196-199), BEFORE creating the
  // child instances, keyed by the oldToNew map below, so new instances bind copied curves at
  // construction. Our model has no Animator yet (S3 Curve/Animator is a parallel worktree).
  // When it lands: build oldToNew first (it is already built per-child just below), then call
  // the curve-copy here passing oldToNew, before the caller applies plan.children. Leaving the
  // seam at this exact point keeps TiXL's ordering invariant intact.

  // Allocate new ids from the target's monotonic floor — strictly increasing, never colliding
  // with existing OR freed-but-burned ids (TiXL: Guid.NewGuid()). We can't mutate the real
  // Symbol here (planning is const), so we track a local floor seeded from nextFreeChildId and
  // bump it per accepted child.
  int floor = nextFreeChildId(*target);

  for (const ClipboardChild& cc : clip.children) {
    // Cycle gate: pasting a compound into one of its own ancestors (or itself) would self-nest;
    // the resident builder then silently skips it (a hole on canvas). Reuse the SSOT predicate —
    // drop the child (and, below, its wires) rather than smuggle a cycle in (TiXL filters such
    // adds in the UI; we filter at paste time, the stronger transitive check).
    if (addChildWouldCycle(lib, targetId, cc.symbolId)) continue;

    const int newId = floor++;
    plan.oldToNew[cc.id] = newId;

    SymbolChild nc;
    nc.id = newId;
    nc.symbolId = cc.symbolId;
    nc.overrides = cc.overrides;          // full per-instance state carried (header FORK)
    nc.x = pasteX + cc.relX;
    nc.y = pasteY + cc.relY;
    plan.children.push_back({std::move(nc)});
  }

  // Remap wires onto the new ids. A wire survives only if BOTH endpoints were accepted (a child
  // dropped by the cycle gate takes its wires with it).
  //
  // 多-input 保序 — FORK from TiXL .cs:110 (named): TiXL reverses its gathered connection list
  // because Symbol.AddConnection INSERTS a multi-input at the slot's front, so a reverse makes the
  // final insertion order match the source. OUR connection model APPENDS (push_back, AddWireCommand
  // tail-append = the order contract, compound_save array-order == multi-input order). With append,
  // preserving source order requires NO reverse — extractClipboard already captured wires in the
  // source symbol's connection order, so straight remap-and-append reproduces it. (combine.cpp uses
  // the identical no-reverse append招 for the same reason.)
  for (const SymbolConnection& w : clip.wires) {
    auto s = plan.oldToNew.find(w.srcChild);
    auto d = plan.oldToNew.find(w.dstChild);
    if (s == plan.oldToNew.end() || d == plan.oldToNew.end()) continue;
    SymbolConnection nw;
    nw.srcChild = s->second;
    nw.srcSlot = w.srcSlot;
    nw.dstChild = d->second;
    nw.dstSlot = w.dstSlot;
    plan.wires.push_back(nw);
  }

  return plan;
}

}  // namespace sw
