// runtime/compound_save — the WRITE side of .swproj v2 (libToJsonV2 + saveLibToFile) plus the
// atomic-uuid table both sides key off. The READ side (libFromJsonAny + loadLibFromFile) lives
// in compound_load.cpp (mechanical split, ARCHITECTURE rule 4). Contract: compound_save.h.
#include "runtime/compound_save.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <map>

#include "crude_json.h"
#include "runtime/curve_json.h"  // curveToJson (S3 animator segment)

namespace sw {
namespace {

// 承重決策 4: fixed UUIDs for the shipped atomic operator types. Assigned once, NEVER edited
// or reused — a rename of the C++ type string only touches this table, old files keep loading.
const std::map<std::string, std::string>& atomicUuidTable() {
  static const std::map<std::string, std::string> t = {
      {"RadialPoints", "5d3a9c1e-7b42-4f0e-9a11-c2e84d6f0a01"},
      {"LinePoints", "8f21b6d4-3c5a-4e87-b390-1a7e5cd20b02"},
      {"GridPoints", "2c74e8a9-651f-4b3d-8e26-94b0f3a1cc03"},
      {"SpherePoints", "b19d4f72-08e3-4a56-bc81-7d2ea6450d04"},
      {"TransformPoints", "e6582ab0-94cd-4713-a5f8-30c1b78e9e05"},
      {"OrientPoints", "47cfd913-2ab8-4c60-9374-e8156fab2f06"},
      {"RandomizePoints", "90ab37c5-de14-4982-b6c0-52f47e91a007"},
      {"SetPointAttributes", "3e60c2f8-71a9-4d35-82eb-c49d08361108"},
      {"CombineBuffers", "ad95e041-b37f-4628-95a2-16e83c7d2209"},
      {"TurbulenceForce", "61f8ba2d-c490-47e1-8d53-a02b9e64330a"},
      {"DirectionalForce", "980b71de-9502-44b1-8b93-21c7e6c47bb2"},
      {"VectorFieldForce", "1595572f-adcd-4146-9e39-3778104847e9"},
      {"ParticleSystem", "f04c79e6-25d1-4b98-ae37-68915cfa440b"},
      {"DrawPoints", "7a2e58c1-9f06-4374-b1d9-3ce5a0b2550c"},
      {"RenderTarget", "c8b1f3a7-46e2-4d09-92c5-eb7480d3660d"},
      {"Time", "19d672e8-83ba-4f51-a046-5d29c1ef770e"},
      {"AudioReaction", "642a90fb-1ce7-4583-bd28-09f4a36c880f"},
      {"Const", "d537c4b2-68a0-4e19-8f63-714be0952910"},
      {"Multiply", "0be8a15f-d943-4c27-96da-83f50c167a11"},
      {"Sine", "85f10dc9-3741-4ab6-805e-2c96db48fb12"},
      {"Remap", "4a9be6f0-507c-4d82-b1c4-da3871e90c13"},
  };
  return t;
}

// A non-finite float dumped by crude_json becomes the bare token `nan`/`inf` — INVALID JSON
// that the parser then rejects WHOLESALE, so one bad number would make a file the app wrote
// unreadable by the app (refuter-savev2 finding B/C/N, an S15 contract violation). The writer
// is the gate: never emit a non-finite number.
crude_json::number finiteOr0(float v) { return std::isfinite(v) ? (crude_json::number)v : 0.0; }

crude_json::value slotDefToJson(const SlotDef& d, bool isInput) {
  crude_json::object o;
  o["id"] = d.id;
  o["name"] = d.name;
  o["dataType"] = d.dataType;
  if (isInput) o["def"] = finiteOr0(d.def);
  // String sub-seam: a String input slot's default text (omitted unless present, zero churn).
  if (isInput && !d.strDef.empty()) o["strDef"] = d.strDef;
  // boundary-node canvas position (TiXL keeps it in .t3ui; our single-file inline analog)
  o["x"] = finiteOr0(d.x);
  o["y"] = finiteOr0(d.y);
  return crude_json::value(o);
}

// S2 (批次7): the per-output child-state segment (= TiXL SymbolJson.cs outputs[]). One entry per output
// that has a non-default disabled/trigger, keyed by output slot id, sorted (std::map) for a stable
// file. Returns the array (empty -> caller omits "outputs"). Factored out so the child write loop stays
// readable (compound_save was already over the soft size line — ARCHITECTURE rule 4).
crude_json::array childOutputStateToJson(const SymbolChild& c) {
  std::map<std::string, crude_json::object> perOut;  // sorted by slotId
  for (const auto& kv : c.disabledOutputs)
    if (kv.second) perOut[kv.first]["isDisabled"] = true;
  for (const auto& kv : c.triggerOverrides)
    if (kv.second != TriggerOverride::None)
      perOut[kv.first]["trigger"] = std::string(triggerOverrideName(kv.second));
  crude_json::array outs;
  for (auto& kv : perOut) {
    crude_json::object oo = kv.second;
    oo["id"] = kv.first;
    outs.push_back(crude_json::value(oo));
  }
  return outs;
}

// Annotation 批A (契約2): one annotation -> {id, [title], [label], [collapsed], [color], x,y,w,h}.
// Mirrors TiXL SymbolUiJson.cs:178-203 omission discipline — empty title/label and false collapsed
// are OMITTED (the common case → minimal file/diff). Color: TiXL ALWAYS writes it; we OMIT a default
// gray (named fork-A2, low risk) to match every other "omit at default" knob in this writer — the id
// (always present) keeps the entry identifiable, and load restores gray when the key is absent. pos/
// size always written (a frame without geometry is meaningless; matches GetVec2OrDefault on read).
// title/label may be CJK: crude_json dumps raw UTF-8 byte-stable (sw-patch utf8), so the roundtrip
// holds (golden covers a 中文 title). NaN-guarded via finiteOr0 (the whole-file abort gate).
crude_json::value annotationToJson(const Annotation& a) {
  crude_json::object o;
  o["id"] = a.id;
  if (!a.title.empty()) o["title"] = a.title;
  if (!a.label.empty()) o["label"] = a.label;
  if (a.collapsed) o["collapsed"] = true;
  if (!annotationColorIsDefault(a)) {
    crude_json::array c;
    for (int i = 0; i < 4; ++i) c.push_back(finiteOr0(a.color[i]));
    o["color"] = crude_json::value(c);
  }
  o["x"] = finiteOr0(a.x);
  o["y"] = finiteOr0(a.y);
  o["w"] = finiteOr0(a.w);
  o["h"] = finiteOr0(a.h);
  return crude_json::value(o);
}

}  // namespace

std::string atomicUuidForType(const std::string& type) {
  if (type.empty()) return "";
  auto it = atomicUuidTable().find(type);
  return it != atomicUuidTable().end() ? it->second : "sw-type:" + type;
}

std::string typeForAtomicUuid(const std::string& uuid) {
  if (uuid.rfind("sw-type:", 0) == 0) return uuid.substr(8);
  for (const auto& kv : atomicUuidTable())
    if (kv.second == uuid) return kv.first;
  return "";
}

std::string libToJsonV2(const SymbolLibrary& lib) {
  // A child's on-disk symbolId: atomic -> fixed UUID; compound -> the raw symbol id.
  auto refOf = [&](const std::string& symbolId) -> std::string {
    const Symbol* s = lib.find(symbolId);
    if (s && !s->atomic) return symbolId;
    return atomicUuidForType(symbolId);
  };

  crude_json::object root;
  root["formatVersion"] = (crude_json::number)2;
  root["rootSymbolId"] = lib.rootId;

  // Composition settings segment (S5): BPM + soundtrack live on the composition, not a sidecar
  // file (契約3 健檢修正). ALWAYS written (a fixed-shape object keeps the file deterministic /
  // diffs clean) — an old file lacking it still loads at the defaults (S15, the read side below).
  {
    crude_json::object comp;
    comp["bpm"] = finiteOr0(lib.composition.bpm);
    comp["soundtrackPath"] = lib.composition.soundtrackPath;
    comp["soundtrackVolume"] = finiteOr0(lib.composition.soundtrackVolume);
    root["composition"] = crude_json::value(comp);
  }

  // Only compound symbols, sorted by id (stable file = clean diffs, TiXL OrderBy(Id)).
  std::vector<const Symbol*> compounds;
  for (const auto& kv : lib.symbols)
    if (!kv.second.atomic) compounds.push_back(&kv.second);
  std::sort(compounds.begin(), compounds.end(),
            [](const Symbol* a, const Symbol* b) { return a->id < b->id; });

  crude_json::array symbols;
  for (const Symbol* s : compounds) {
    crude_json::object o;
    o["id"] = s->id;
    o["name"] = s->name;
    // Monotonic child-id floor (compound_graph.h): without it a reload would recompute
    // max+1 and resurrect freed ids (= a new child inheriting dead per-path state).
    o["nextChildId"] = (crude_json::number)s->nextChildId;
    crude_json::array ins, outs;  // array order == definition order (S16)
    for (const SlotDef& d : s->inputDefs) ins.push_back(slotDefToJson(d, /*isInput=*/true));
    for (const SlotDef& d : s->outputDefs) outs.push_back(slotDefToJson(d, /*isInput=*/false));
    o["inputDefs"] = crude_json::value(ins);
    o["outputDefs"] = crude_json::value(outs);
    crude_json::array children;
    for (const SymbolChild& c : s->children) {
      crude_json::object co;
      co["id"] = (crude_json::number)c.id;
      co["symbolId"] = refOf(c.symbolId);
      // Custom instance name (rename, 契約 rename). OMITTED when empty (= the common case, the
      // child falls back to the def name) so the file stays minimal + diffs stay clean — TiXL
      // likewise only persists a non-default Name. May be CJK: crude_json dumps raw UTF-8 bytes
      // (sw-patch utf8) and parses them back verbatim, so the roundtrip is byte-stable.
      if (!c.name.empty()) co["name"] = c.name;
      crude_json::object ov;
      for (const auto& kv : c.overrides) ov[kv.first] = finiteOr0(kv.second);
      co["overrides"] = crude_json::value(ov);
      // String sub-seam: per-instance String overrides (e.g. SetFloatVar.VariableName). OMITTED
      // when empty (the universal case for non-var ops → minimal file/diff). May hold CJK; crude_json
      // dumps raw UTF-8 byte-stable, so the roundtrip holds.
      if (!c.strOverrides.empty()) {
        crude_json::object sov;
        for (const auto& kv : c.strOverrides) sov[kv.first] = kv.second;
        co["strOverrides"] = crude_json::value(sov);
      }
      co["x"] = finiteOr0(c.x);
      co["y"] = finiteOr0(c.y);
      // S2 (批次7) child structural补欄, all 照 TiXL SymbolJson.cs, all OMITTED at default so the
      // file stays minimal + diffs clean (= TiXL only persists non-default state). isBypassed: a
      // child-level bool (.cs:83-85). outputs[]: per-output {id, [isDisabled], [trigger]} — an entry
      // is written ONLY when that output has a non-default disabled/trigger (.cs:117-143), keyed by
      // the output slot id (sorted for a stable file, same intent as the symbols/connections order).
      if (c.isBypassed) co["isBypassed"] = true;
      crude_json::array outState = childOutputStateToJson(c);
      if (!outState.empty()) co["outputs"] = crude_json::value(outState);
      children.push_back(crude_json::value(co));
    }
    o["children"] = crude_json::value(children);
    crude_json::array conns;  // array order == multi-input order (stable)
    for (const SymbolConnection& w : s->connections) {
      crude_json::object wo;
      wo["srcChild"] = (crude_json::number)w.srcChild;
      wo["srcSlot"] = w.srcSlot;
      wo["dstChild"] = (crude_json::number)w.dstChild;
      wo["dstSlot"] = w.dstSlot;
      conns.push_back(crude_json::value(wo));
    }
    o["connections"] = crude_json::value(conns);

    // S3 animator segment (= TiXL Animator.Write cs:371-408). Flatten childId -> inputId -> Curve[]
    // into a sorted array of {childId, inputId, index, curve}. Sorted by (childId, inputId, index)
    // for a stable file (clean diffs, same intent as the symbols/connections ordering). Omitted
    // entirely when the symbol has no animations (= TiXL early-return on empty, cs:373).
    if (!s->animator.empty()) {
      crude_json::array anim;
      for (const auto& [childId, inputDict] : s->animator.all()) {
        for (const auto& [inputId, arr] : inputDict) {
          for (size_t i = 0; i < arr.size(); ++i) {
            crude_json::object ao;
            ao["childId"] = (crude_json::number)childId;
            ao["inputId"] = inputId;
            if (i != 0) ao["index"] = (crude_json::number)(int)i;
            ao["curve"] = curveToJson(arr[i]);
            anim.push_back(crude_json::value(ao));
          }
        }
      }
      o["animator"] = crude_json::value(anim);
    }

    // Annotation 批A (契約2): the optional "annotations" segment, sorted by id (= TiXL OrderBy(Id),
    // SymbolUiJson.cs:178 — a stable file = clean diffs, same intent as the symbols/connections sort).
    // OMITTED entirely when the symbol has none, so an old file with no annotations stays byte-identical
    // and a reader without the segment is the common case (S15: absent = empty, zero warning).
    if (!s->annotations.empty()) {
      std::vector<const Annotation*> sorted;
      for (const Annotation& a : s->annotations) sorted.push_back(&a);
      std::sort(sorted.begin(), sorted.end(),
                [](const Annotation* a, const Annotation* b) { return a->id < b->id; });
      crude_json::array anns;
      for (const Annotation* a : sorted) anns.push_back(annotationToJson(*a));
      o["annotations"] = crude_json::value(anns);
    }
    symbols.push_back(crude_json::value(o));
  }
  root["symbols"] = crude_json::value(symbols);
  return crude_json::value(root).dump(2);
}

bool saveLibToFile(const std::string& path, const SymbolLibrary& lib) {
  std::ofstream f(path);
  if (!f) return false;
  f << libToJsonV2(lib);
  return f.good();
}

}  // namespace sw
