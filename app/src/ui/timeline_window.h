// ui/timeline_window — the dope-sheet timeline (S3 GUI). A standalone floating imgui window
// (NOT inside the node canvas — far simpler): one lane per animated (child,input) of the CURRENT
// composition symbol, a draggable playhead, and key gestures (single-click select / drag time+value
// / double-click empty = add key / Delete = remove). Every mutation goes through an undoable command
// (app/animation_commands); the playhead drag goes through the SAME transport scrub surface the
// toolbar uses (frame_cook transportScrub), so the two playhead controls can't drift.
//
// = TiXL Editor/Gui/Windows/TimeLine/ (DopeSheetArea + CurrentTimeMarker + TimeLineCanvas). Visual
// is simplified to the first-cut范圍 (報告具名)：dope-sheet only — NO TimeClip/Layer/loop range/變速,
// NO bezier handle drag (key time+value drag is enough; interpolation enum is a later cut).
// Zone: ui. Depends on app(document/command/frame_cook/animation_commands) + runtime. Never reverse.
#pragma once

namespace sw::ui {

// Draw the Timeline window. Call once per frame alongside drawToolbar/drawInspector/etc.
void drawTimelineWindow();

}  // namespace sw::ui
