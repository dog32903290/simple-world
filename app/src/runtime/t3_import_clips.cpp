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

// Read a child's Outputs[].OutputData TimeClip blocks into child.clips, keyed by the sw output slot NAME.
// Shape:
//   "Outputs":[ { "Id":"<outGuid>", "OutputData":{ "Type":"T3.Core.Animation.TimeClip",
//                 "TimeClip":{ "TimeRange":{"Start":s,"End":e}, "SourceRange":{"Start":ss,"End":se},
//                              "LayerIndex":n } } } ]
// Only carrying ops emit an OutputData block; DirtyFlagTrigger/IsDisabled-only Outputs entries have no
// OutputData → skipped. Absent SourceRange defaults to TimeRange (TimeClip.cs:62-73 MakeConform).
void parseChildTimeClips(const crude_json::value& cv, const std::string& swType, SymbolChild& child,
                         const std::function<void(const std::string&)>& warn) {
  if (!cv["Outputs"].is_array()) return;
  for (const crude_json::value& ov : cv["Outputs"].get<crude_json::array>()) {
    if (!ov.is_object()) continue;
    const crude_json::value& od = ov["OutputData"];
    if (!od.is_object()) continue;  // DirtyFlagTrigger/IsDisabled-only entry → no clip data
    // Guard on Type so a future OutputData kind never mis-parses as a clip (only TimeClip today).
    if (t3i::asStr(od, "Type") != "T3.Core.Animation.TimeClip") continue;
    const crude_json::value& tc = od["TimeClip"];
    if (!tc.is_object()) continue;
    const std::string outSlot = swSlotNameForGuid(swType, t3i::lc(t3i::asStr(ov, "Id")));
    if (outSlot.empty()) {
      warn("t3: TimeClip on unknown output slot for " + swType + ", skipped");
      continue;
    }
    ClipTimeData clip;
    const crude_json::value& trg = tc["TimeRange"];
    if (trg.is_object()) {
      if (trg["Start"].is_number()) clip.timeStart = (float)trg["Start"].get<crude_json::number>();
      if (trg["End"].is_number())   clip.timeEnd   = (float)trg["End"].get<crude_json::number>();
    }
    const crude_json::value& srg = tc["SourceRange"];
    if (srg.is_object()) {
      if (srg["Start"].is_number()) clip.sourceStart = (float)srg["Start"].get<crude_json::number>();
      if (srg["End"].is_number())   clip.sourceEnd   = (float)srg["End"].get<crude_json::number>();
    } else {
      clip.sourceStart = clip.timeStart;  // conform: absent SourceRange = TimeRange (TimeClip.cs:62-73)
      clip.sourceEnd   = clip.timeEnd;
    }
    if (tc["LayerIndex"].is_number()) clip.layerIndex = (int)tc["LayerIndex"].get<crude_json::number>();
    child.clips[outSlot] = clip;
  }
}

}  // namespace sw
