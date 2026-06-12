// runtime/compound_load — the READ side of .swproj v2 (libFromJsonAny + loadLibFromFile),
// split mechanically out of compound_save.cpp (ARCHITECTURE rule 4: one file one duty; the
// writer stays in compound_save.cpp). Same contract header: runtime/compound_save.h.
#include "runtime/compound_save.h"

#include <algorithm>
#include <fstream>
#include <iterator>
#include <map>

#include "crude_json.h"
#include "runtime/curve_json.h"    // curveFromJson (S3 animator segment)
#include "runtime/graph.h"         // findSpec (atomic regeneration) + fromJson (legacy)
#include "runtime/graph_bridge.h"  // atomicSymbolFromSpec + libFromGraph (legacy migration)

namespace sw {
namespace {

void appendWarn(std::vector<std::string>* w, const std::string& msg) {
  if (w) w->push_back(msg);
}

SlotDef slotDefFromJson(crude_json::value& v) {
  SlotDef d;
  if (v["id"].is_string()) d.id = v["id"].get<crude_json::string>();
  if (v["name"].is_string()) d.name = v["name"].get<crude_json::string>();
  if (v["dataType"].is_string()) d.dataType = v["dataType"].get<crude_json::string>();
  if (v["def"].is_number()) d.def = (float)v["def"].get<crude_json::number>();
  if (v["x"].is_number()) d.x = (float)v["x"].get<crude_json::number>();
  if (v["y"].is_number()) d.y = (float)v["y"].get<crude_json::number>();
  return d;
}

// S2: read the per-output child-state segment back onto `c`. S15-tolerant — an entry whose `id` is not
// a real outputDef of `refSym` is dropped LOCALLY (next save self-heals; mirrors the zombie-override
// scrub), garbage elements skipped, garbage trigger strings -> None (dropped). `warnSink` appends drops.
void childOutputStateFromJson(crude_json::value& outsv, const Symbol* refSym, SymbolChild& c,
                              const std::string& symId, std::vector<std::string>* warnSink) {
  for (auto& outv : outsv.get<crude_json::array>()) {
    if (!outv.is_object() || !outv["id"].is_string()) continue;  // garbage element -> skip
    const std::string oid = outv["id"].get<crude_json::string>();
    bool known = false;
    if (refSym)
      for (const SlotDef& d : refSym->outputDefs)
        if (d.id == oid) { known = true; break; }
    if (!known) {
      appendWarn(warnSink, "output state for unknown output '" + oid + "' on child " +
                               std::to_string(c.id) + " in '" + symId + "' — dropped");
      continue;
    }
    if (outv["isDisabled"].is_boolean() && outv["isDisabled"].get<bool>())
      c.disabledOutputs[oid] = true;
    if (outv["trigger"].is_string()) {
      TriggerOverride t = triggerOverrideFromName(outv["trigger"].get<crude_json::string>());
      if (t != TriggerOverride::None) c.triggerOverrides[oid] = t;
    }
  }
}

}  // namespace

bool libFromJsonAny(const std::string& json, SymbolLibrary& out,
                    std::vector<std::string>* warnings) {
  crude_json::value v = crude_json::value::parse(json);
  if (!v.is_object()) return false;

  // LEGACY v1 (no formatVersion): a flat editor graph. Migrate through the bridge — the
  // result is a root compound whose children mirror the nodes (auto-upgrades on next save).
  if (!v["formatVersion"].is_number()) {
    Graph g;
    if (!fromJson(json, g)) return false;
    out = libFromGraph(g);
    appendWarn(warnings, "legacy v1 .swproj migrated to v2 (flat graph -> root symbol)");
    return true;
  }

  const int fmt = (int)v["formatVersion"].get<crude_json::number>();
  if (fmt > 2)
    appendWarn(warnings, "file formatVersion " + std::to_string(fmt) +
                             " is newer than this app (2) — loading what is understood");

  out = SymbolLibrary{};

  // Composition settings (S5): tolerant read — absent segment OR a bad/non-positive bpm falls back
  // to the struct default (120 bpm), file still loads (S15, 世界觀級 — bad data dropped locally).
  if (crude_json::value& comp = v["composition"]; comp.is_object()) {
    if (comp["bpm"].is_number()) {
      double b = comp["bpm"].get<crude_json::number>();
      // Sane range, not just >0: a tiny-positive bpm (1e-300, legal JSON) passes a >0 gate but
      // makes secondsFromBars explode to inf downstream (refuter-S5 BROKEN-B). Clamp at load.
      if (b >= 1.0 && b <= 999.0)
        out.composition.bpm = b;
      else
        appendWarn(warnings, "composition bpm outside [1,999] dropped — using default 120");
    }
    if (comp["soundtrackPath"].is_string())
      out.composition.soundtrackPath = comp["soundtrackPath"].get<crude_json::string>();
    if (comp["soundtrackVolume"].is_number())
      out.composition.soundtrackVolume = comp["soundtrackVolume"].get<crude_json::number>();
  }

  // ---- phase 1: register every compound symbol DEFINITION (no reference resolution yet,
  // 兩階段 load 照搬 — children/connections resolve only after every definition exists). ----
  crude_json::value& symbols = v["symbols"];
  if (!symbols.is_array()) return false;
  for (auto& sv : symbols.get<crude_json::array>()) {
    if (!sv["id"].is_string()) { appendWarn(warnings, "symbol without id dropped"); continue; }
    Symbol s;
    s.id = sv["id"].get<crude_json::string>();
    if (out.symbols.count(s.id)) {  // duplicate definition: first wins, deterministic (refuter G)
      appendWarn(warnings, "duplicate symbol '" + s.id + "' — later definition dropped");
      continue;
    }
    s.name = sv["name"].is_string() ? sv["name"].get<crude_json::string>() : s.id;
    s.atomic = false;
    // Absent in pre-N2 files: leave the default floor (1); nextFreeChildId's max+1 covers.
    if (sv["nextChildId"].is_number())
      s.nextChildId = (int)sv["nextChildId"].get<crude_json::number>();
    if (sv["inputDefs"].is_array())
      for (auto& dv : sv["inputDefs"].get<crude_json::array>()) s.inputDefs.push_back(slotDefFromJson(dv));
    if (sv["outputDefs"].is_array())
      for (auto& dv : sv["outputDefs"].get<crude_json::array>()) s.outputDefs.push_back(slotDefFromJson(dv));
    // Boundary-def count cap (99): the kept practical ceiling on a symbol's external ports
    // (mirrors combine.cpp's refuse guard; OURS, not TiXL's — TiXL has no port ceiling). The
    // editor's boundary pin
    // encoding now lives in its own high band so > 99 no longer ALIASES child 1's pins — but we
    // still drop the excess locally (S15 tolerance) so a crafted file can't smuggle in more
    // ports than combine would ever produce. No UI can create defs yet; only a crafted file can.
    while (s.inputDefs.size() + s.outputDefs.size() > 99) {
      appendWarn(warnings, "symbol '" + s.id + "': boundary def limit (99) exceeded — '" +
                               (s.outputDefs.empty() ? s.inputDefs.back().id
                                                     : s.outputDefs.back().id) +
                               "' dropped");
      if (!s.outputDefs.empty()) s.outputDefs.pop_back();
      else s.inputDefs.pop_back();
    }
    out.symbols[s.id] = s;
  }

  // ---- phase 2: resolve children (atomic uuid -> registry regeneration; unknown -> drop)
  // and scrub unresolvable wires (S15: local drop + warn, next save self-heals). ----
  std::vector<std::string> phase2Done;  // first-wins guard must hold here too (refuter G)
  for (auto& sv : symbols.get<crude_json::array>()) {
    if (!sv["id"].is_string()) continue;
    const std::string sid = sv["id"].get<crude_json::string>();
    if (std::find(phase2Done.begin(), phase2Done.end(), sid) != phase2Done.end()) continue;
    phase2Done.push_back(sid);
    Symbol& s = out.symbols[sid];

    if (sv["children"].is_array()) {
      for (auto& cv : sv["children"].get<crude_json::array>()) {
        SymbolChild c;
        c.id = cv["id"].is_number() ? (int)cv["id"].get<crude_json::number>() : 0;
        std::string ref = cv["symbolId"].is_string() ? cv["symbolId"].get<crude_json::string>() : "";
        // COMPOUND ids resolve FIRST: a compound named inside the atomic namespaces (e.g.
        // a literal "sw-type:Const" id) must never be hijacked into an atomic reference
        // (refuter-savev2 finding J). Only unmatched refs fall to the atomic tables.
        if (out.symbols.count(ref) && !out.symbols[ref].atomic) {
          c.symbolId = ref;
        } else if (std::string atomicType = typeForAtomicUuid(ref); !atomicType.empty()) {
          const NodeSpec* spec = findSpec(atomicType);
          if (!spec) {
            appendWarn(warnings, "child " + std::to_string(c.id) + " in '" + s.id +
                                     "' references unknown operator '" + atomicType + "' — dropped");
            continue;
          }
          if (!out.symbols.count(atomicType)) out.symbols[atomicType] = atomicSymbolFromSpec(*spec);
          c.symbolId = atomicType;
        } else {
          appendWarn(warnings, "child " + std::to_string(c.id) + " in '" + s.id +
                                   "' references missing symbol '" + ref + "' — dropped");
          continue;
        }
        if (c.id <= 0) { appendWarn(warnings, "child with invalid id in '" + s.id + "' dropped"); continue; }
        if (cv["overrides"].is_object()) {
          // Scrub ZOMBIE overrides the SAME way phase-2 scrubs dangling wires: an override keys an
          // inputDef on the child's referenced symbol — if that def no longer exists (a def was
          // removed but a stale/hand-edited file still carries the override) it can never matter and
          // would otherwise re-save forever (S15: local drop + warn, next save self-heals). Worse,
          // left in place it可借屍還魂 — if a def of the same id later reappears, effectiveInput
          // returns the dead override instead of the new default. c.symbolId already resolves to a
          // symbol present in `out` (atomic regenerated above, compound from phase 1), so its
          // inputDefs are the authority (refuter probe6).
          const Symbol* refSym = out.find(c.symbolId);
          for (auto& kv : cv["overrides"].get<crude_json::object>()) {
            if (!kv.second.is_number()) continue;
            bool known = false;
            if (refSym)
              for (const SlotDef& d : refSym->inputDefs)
                if (d.id == kv.first) { known = true; break; }
            if (!known) {
              appendWarn(warnings, "obsolete override '" + kv.first + "' on child " +
                                       std::to_string(c.id) + " in '" + s.id +
                                       "' (no such input def) — dropped");
              continue;  // self-heal: gone from the in-memory model, gone on next save
            }
            c.overrides[kv.first] = (float)kv.second.get<crude_json::number>();
          }
        }
        // Custom instance name (rename). S15-tolerant: a non-string/garbage `name` is simply
        // dropped (the child keeps loading, falls back to the def name) — never fails the child.
        if (cv["name"].is_string()) c.name = cv["name"].get<crude_json::string>();
        if (cv["x"].is_number()) c.x = (float)cv["x"].get<crude_json::number>();
        if (cv["y"].is_number()) c.y = (float)cv["y"].get<crude_json::number>();
        // S2 (批次7) structural补欄. isBypassed: a non-bool/garbage value is ignored (stays false).
        // The per-output disabled/trigger segment is read by the helper (S15-tolerant: unknown output
        // ids dropped locally, = the zombie-override scrub; refSym is the slot-existence authority).
        if (cv["isBypassed"].is_boolean()) c.isBypassed = cv["isBypassed"].get<bool>();
        if (cv["outputs"].is_array())
          childOutputStateFromJson(cv["outputs"], out.find(c.symbolId), c, s.id, warnings);
        s.children.push_back(c);
      }
    }

    if (sv["connections"].is_array()) {
      auto childOf = [&](int id) -> const SymbolChild* {
        for (const SymbolChild& c : s.children)
          if (c.id == id) return &c;
        return nullptr;
      };
      auto hasSlot = [&](const std::vector<SlotDef>& defs, const std::string& slot) {
        for (const SlotDef& d : defs)
          if (d.id == slot) return true;
        return false;
      };
      for (auto& wv : sv["connections"].get<crude_json::array>()) {
        SymbolConnection w;
        w.srcChild = wv["srcChild"].is_number() ? (int)wv["srcChild"].get<crude_json::number>() : -1;
        w.dstChild = wv["dstChild"].is_number() ? (int)wv["dstChild"].get<crude_json::number>() : -1;
        if (wv["srcSlot"].is_string()) w.srcSlot = wv["srcSlot"].get<crude_json::string>();
        if (wv["dstSlot"].is_string()) w.dstSlot = wv["dstSlot"].get<crude_json::string>();
        bool ok = true;
        if (w.srcChild == kSymbolBoundary) {
          ok = hasSlot(s.inputDefs, w.srcSlot);  // sentinel: the parent's own input def
        } else if (const SymbolChild* c = childOf(w.srcChild)) {
          const Symbol* ref = out.find(c->symbolId);
          ok = ref && hasSlot(ref->outputDefs, w.srcSlot);
        } else {
          ok = false;
        }
        if (ok) {
          if (w.dstChild == kSymbolBoundary) {
            ok = hasSlot(s.outputDefs, w.dstSlot);  // sentinel: the parent's own output def
          } else if (const SymbolChild* c = childOf(w.dstChild)) {
            const Symbol* ref = out.find(c->symbolId);
            ok = ref && hasSlot(ref->inputDefs, w.dstSlot);
          } else {
            ok = false;
          }
        }
        if (!ok) {
          appendWarn(warnings, "dangling wire " + std::to_string(w.srcChild) + ":" + w.srcSlot +
                                   " -> " + std::to_string(w.dstChild) + ":" + w.dstSlot + " in '" +
                                   s.id + "' — dropped");
          continue;  // self-heal: gone from the in-memory model, gone on next save
        }
        s.connections.push_back(w);
      }
    }

    // ---- S3 animator segment: resolve curves AFTER children exist (so childId/inputId validate).
    // S15 tolerance: an entry pointing at a missing child, or at an input def that no child's
    // referenced symbol owns, is dropped LOCALLY with a warning (next save self-heals) — a malformed
    // animator never fails the whole file. Curves group into arrays by (childId, inputId), index
    // placing each channel (gaps default-fill with empty curves, same as TiXL's curveArray[maxIndex+1]).
    if (sv["animator"].is_array()) {
      auto childOf = [&](int id) -> const SymbolChild* {
        for (const SymbolChild& c : s.children)
          if (c.id == id) return &c;
        return nullptr;
      };
      // Collect by (childId,inputId) -> index -> Curve so multi-channel arrays land in order.
      std::map<std::pair<int, std::string>, std::map<int, Curve>> grouped;
      for (auto& av : sv["animator"].get<crude_json::array>()) {
        if (!av["childId"].is_number() || !av["inputId"].is_string()) {
          appendWarn(warnings, "animator entry without childId/inputId in '" + s.id + "' — dropped");
          continue;
        }
        int childId = (int)av["childId"].get<crude_json::number>();
        std::string inputId = av["inputId"].get<crude_json::string>();
        int index = av["index"].is_number() ? (int)av["index"].get<crude_json::number>() : 0;
        const SymbolChild* c = childOf(childId);
        if (!c) {
          appendWarn(warnings, "animator on missing child " + std::to_string(childId) + " in '" +
                                   s.id + "' — dropped");
          continue;
        }
        const Symbol* ref = out.find(c->symbolId);
        bool known = false;
        if (ref)
          for (const SlotDef& d : ref->inputDefs)
            if (d.id == inputId) { known = true; break; }
        if (!known) {
          appendWarn(warnings, "animator on unknown input '" + inputId + "' of child " +
                                   std::to_string(childId) + " in '" + s.id + "' — dropped");
          continue;
        }
        if (index < 0 || index > 16) {  // sane channel ceiling (S15: a crafted file can't allocate huge)
          appendWarn(warnings, "animator channel index " + std::to_string(index) + " out of range in '" +
                                   s.id + "' — dropped");
          continue;
        }
        if (av["curve"].is_object()) {
          auto& slot = grouped[{childId, inputId}];
          if (slot.count(index))  // duplicate (childId,inputId,index): later wins, but warn (was silent)
            appendWarn(warnings, "duplicate animator channel (child " + std::to_string(childId) +
                                     ", input '" + inputId + "', index " + std::to_string(index) +
                                     ") in '" + s.id + "' — later overwrites earlier");
          int droppedKeys = 0;
          Curve cv = curveFromJson(av["curve"], &droppedKeys);
          if (droppedKeys > 0)  // key-time NaN / malformed key dropped inside the curve (was silent)
            appendWarn(warnings, std::to_string(droppedKeys) + " malformed/NaN key(s) dropped from "
                                     "animator curve (child " + std::to_string(childId) + ", input '" +
                                     inputId + "') in '" + s.id + "'");
          slot[index] = std::move(cv);
        }
      }
      for (auto& [key, byIndex] : grouped) {
        int maxIndex = 0;
        for (auto& [idx, cv] : byIndex) maxIndex = idx > maxIndex ? idx : maxIndex;
        Animator::CurveArray arr(maxIndex + 1);  // gaps default-fill (= TiXL curveArray[maxIndex+1])
        for (auto& [idx, cv] : byIndex) arr[idx] = std::move(cv);
        s.animator.setCurves(key.first, key.second, std::move(arr));
      }
    }
  }

  // Root resolve: missing/unknown root falls back to the first compound (load 不死).
  if (v["rootSymbolId"].is_string()) out.rootId = v["rootSymbolId"].get<crude_json::string>();
  const Symbol* root = out.find(out.rootId);
  if (!root || root->atomic) {
    out.rootId.clear();
    for (const auto& kv : out.symbols)
      if (!kv.second.atomic) { out.rootId = kv.first; break; }
    if (out.rootId.empty()) return false;  // nothing usable
    appendWarn(warnings, "rootSymbolId missing/unknown — fell back to '" + out.rootId + "'");
  }
  return true;
}

bool loadLibFromFile(const std::string& path, SymbolLibrary& out,
                     std::vector<std::string>* warnings) {
  std::ifstream f(path);
  if (!f) return false;
  std::string json((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
  return libFromJsonAny(json, out, warnings);
}

}  // namespace sw
