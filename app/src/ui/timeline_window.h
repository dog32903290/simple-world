// ui/timeline_window — the timeline (S3 first cut, S6 成熟). A standalone floating imgui window
// (NOT inside the node canvas): one lane per animated (child,input) of the CURRENT composition
// symbol, a draggable playhead, and key gestures. S6 unlocked: cursor-anchored zoom + pan,
// rubber-band multi-select + shift/cmd add/remove, multi-key drag, interpolation switch (context
// menu), and a second Curves view (value axis + 2-axis key drag + in/out tangent handles).
// Every mutation goes through an undoable command (app/animation_commands); the playhead drag
// goes through the SAME transport scrub surface the toolbar uses (frame_cook transportScrub).
//
// = TiXL Editor/Gui/Windows/TimeLine/ (TimeLineCanvas Modes + DopeSheetArea + TimelineCurveEditor
// + CurvePoint). Still locked (具名): TimeClip/Layer/loop range/變速/snap handlers/weighted
// tension drag/keyframe copy-paste. Implementation split across timeline_{window,dopesheet,
// curve_editor,edit}.cpp — see timeline_internal.h for the contract.
// Zone: ui. Depends on app(document/command/frame_cook/animation_commands) + runtime. Never reverse.
#pragma once

namespace sw::ui {

// Draw the Timeline window. Call once per frame alongside drawToolbar/drawInspector/etc.
void drawTimelineWindow();

}  // namespace sw::ui
