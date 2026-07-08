// runtime/t3_import_clips — the .t3 per-child TimeClip parse (DataSet-timeline seam), split from
// t3_import.cpp's Children loop (ARCHITECTURE.md rule 4: t3_import.cpp is at the ≤400 line ratchet and the
// clip parse is a distinct, self-contained job). Pure CPU. Declared in t3_import_internal.h.
//
// TiXL parity: a SymbolChild whose output slot is a TimeClipSlot<T> (TimeClip / MidiClip / LoadDataClip /
// ImageSequenceClip / VideoClip / PlayVideoClip) serializes its per-instance timeline placement as
// Outputs[].OutputData (SymbolJson.cs:112-131), Type "T3.Core.Animation.TimeClip", read by TimeClip.cs:106-137
// ReadFromJson. This is AUTHORED DATA on the parent .t3's child, NOT an input value and NOT on the definition.
#include "runtime/t3_import_internal.h"

#include <string>

#include "runtime/t3_import_maps.h"  // swSlotNameForGuid (output GUID → sw slot name)

namespace sw {

// Test-only seam (mirrors t3LayoutDisable): the t3-timeclip-oob regression golden flips this true on
// its -bug leg so optField() below reverts to the bare, unguarded const operator[] — reproducing the
// heap-buffer-overflow this fix removes. false in every production path (one predictable branch/field).
bool& t3ClipOobGuardDisable() { static bool v = false; return v; }

namespace {
// crude_json's CONST operator[](key) does NOT null-default an absent key — it dereferences
// map::end()->second (its CRUDE_ASSERT is compiled out under NDEBUG, which the ASan/release build IS)
// → heap-buffer-overflow read the first time the returned dangling ref is touched (e.g. .is_object()
// reads the 4-byte m_Type 8 bytes before a freed node). Every OPTIONAL-key read here MUST go through
// these guards, never bare `v[key]`. Repro that forced this: DrawMesh.t3 / FindClosestPointsOnMesh.t3
// carry a non-empty Outputs entry (a DirtyFlagTrigger-only slot) with NO "OutputData" — the old
// `ov["OutputData"]` read 8 bytes OOB and aborted (ASan) / was UB (release).
const crude_json::value& optField(const crude_json::value& v, const char* key) {
  static const crude_json::value kNull;  // type_t::null: is_object/number/string/array all false
  if (t3ClipOobGuardDisable()) return v[key];  // -bug: bare const op[] → OOB read on an absent key
  return (v.is_object() && v.contains(key)) ? v[key] : kNull;
}
std::string optStr(const crude_json::value& v, const char* key) {
  const crude_json::value& f = optField(v, key);
  return f.is_string() ? f.get<crude_json::string>() : std::string();
}
}  // namespace

// Read a child's Outputs[].OutputData TimeClip blocks into child.clips, keyed by the sw output slot NAME.
// Shape:
//   "Outputs":[ { "Id":"<outGuid>", "OutputData":{ "Type":"T3.Core.Animation.TimeClip",
//                 "TimeClip":{ "TimeRange":{"Start":s,"End":e}, "SourceRange":{"Start":ss,"End":se},
//                              "LayerIndex":n } } } ]
// Only carrying ops emit an OutputData block; DirtyFlagTrigger/IsDisabled-only Outputs entries have no
// OutputData → skipped. Absent SourceRange defaults to TimeRange (TimeClip.cs:62-73 MakeConform).
void parseChildTimeClips(const crude_json::value& cv, const std::string& swType, SymbolChild& child,
                         const std::function<void(const std::string&)>& warn) {
  const crude_json::value& outs = optField(cv, "Outputs");
  if (!outs.is_array()) return;
  for (const crude_json::value& ov : outs.get<crude_json::array>()) {
    if (!ov.is_object()) continue;
    const crude_json::value& od = optField(ov, "OutputData");
    if (!od.is_object()) continue;  // DirtyFlagTrigger/IsDisabled-only entry → no clip data
    // Guard on Type so a future OutputData kind never mis-parses as a clip (only TimeClip today).
    if (optStr(od, "Type") != "T3.Core.Animation.TimeClip") continue;
    const crude_json::value& tc = optField(od, "TimeClip");
    if (!tc.is_object()) continue;
    const std::string outSlot = swSlotNameForGuid(swType, t3i::lc(optStr(ov, "Id")));
    if (outSlot.empty()) {
      warn("t3: TimeClip on unknown output slot for " + swType + ", skipped");
      continue;
    }
    ClipTimeData clip;
    const crude_json::value& trg = optField(tc, "TimeRange");
    if (trg.is_object()) {
      const crude_json::value& s = optField(trg, "Start");
      const crude_json::value& e = optField(trg, "End");
      if (s.is_number()) clip.timeStart = (float)s.get<crude_json::number>();
      if (e.is_number()) clip.timeEnd   = (float)e.get<crude_json::number>();
    }
    const crude_json::value& srg = optField(tc, "SourceRange");
    if (srg.is_object()) {
      const crude_json::value& s = optField(srg, "Start");
      const crude_json::value& e = optField(srg, "End");
      if (s.is_number()) clip.sourceStart = (float)s.get<crude_json::number>();
      if (e.is_number()) clip.sourceEnd   = (float)e.get<crude_json::number>();
    } else {
      clip.sourceStart = clip.timeStart;  // conform: absent SourceRange = TimeRange (TimeClip.cs:62-73)
      clip.sourceEnd   = clip.timeEnd;
    }
    const crude_json::value& li = optField(tc, "LayerIndex");
    if (li.is_number()) clip.layerIndex = (int)li.get<crude_json::number>();
    child.clips[outSlot] = clip;
  }
}

}  // namespace sw
