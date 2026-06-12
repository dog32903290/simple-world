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

// Headless RED->GREEN teeth for the S6 gesture core (--selftest-timeline): rigid group drag
// clamp, ghost-selection dedupe, delete misroute guard, boundary tangent roundtrip, Linear->
// Tangent promotion, wheel-zoom pins (refuter 批次8 五條 BROKEN 轉正式 leg). Lives in
// timeline_selftest.cpp; injectBug re-introduces each bug's data shape -> the legs must FAIL.
int runTimelineSelfTest(bool injectBug);

}  // namespace sw::ui
