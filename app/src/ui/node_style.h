// ui/node_style — TiXL-parity node coloring. A node's "category" = its first output's
// dataType; that maps to a base color (faithful to TiXL Editor/Gui/Styling/UiColors.cs),
// then the TiXL ColorVariations HSV transforms produce the background / outline / label
// tints. Zone: ui. Pure color math + a small dataType→color table; reads NodeSpec only,
// never mutates the graph. editor_ui pushes these per node (ed::PushStyleColor).
#pragma once
#include <string>

#include "imgui.h"

namespace sw { struct NodeSpec; }

namespace sw::ui {

// Raw TiXL base color for a port/link dataType (for pins/links in later 刀). Unknown → gray.
ImU32 typeColor(const std::string& dataType);

// Per-node tints, category = first output's dataType (TiXL ColorVariations factors):
ImU32 nodeBgColor(const sw::NodeSpec& spec);      // OperatorBackground  b0.5  s0.7  a1.0
ImU32 nodeBorderColor(const sw::NodeSpec& spec);  // OperatorOutline     b0.1  s0.7  a0.5
ImU32 nodeLabelColor(const sw::NodeSpec& spec);   // OperatorLabel       b1.3  s0.4  a1.0

// Idle-fade variant (TiXL DrawNode.cs:49-50 + OperatorBackgroundIdle ColorVariation b=0.71,s=1.0,op=0.3):
// idleFadeFactor = RemapAndClamp(framesSince, 0, 60, 1.0, 0.6). When idleFadeFactor==1.0 returns
// nodeBgColor (fully active); when 0.6 lerps toward the idle (dark) variant. Caller computes
// idleFadeFactor from framecook::currentFrameIndex() - framecook::residentNodeLastUpdatePass(path).
ImU32 nodeBgColorIdle(const sw::NodeSpec& spec, float idleFadeFactor);

// Selection / hover outlines (TiXL UiColors.Selection is white, category-independent).
ImU32 nodeSelectedBorderColor();  // bright white — selected node outline
ImU32 nodeHoverBorderColor();     // soft white — hovered node outline

// V2: Connection line color (TiXL DrawConnection.cs:32-42 ConnectionLines variation b1 s1 a0.8).
// selected=true (or hovered) uses OperatorLabel variation instead (slightly brighter).
ImU32 connectionLineColor(const std::string& dataType, bool selected);

// V3: Node corner rounding scaled with zoom (TiXL DrawNode.cs:126 — 5*CanvasScale, 0 if <0.5x).
// tixlScale = 1/GetCurrentZoom() in imgui-node-editor coords.
float nodeRounding(float tixlScale);

// V4: Blink animation value — TiXL MagGraphCanvas.Drawing.cs:459:
//   internal static float Blink => MathF.Sin((float)ImGui.GetTime() * 10) * 0.5f + 0.5f;
// (period = 2pi/10 ≈ 0.628s). Returns [0,1].
float blinkValue();

// Annotation frame colors (= TiXL DrawAnnotation.cs:38-56 + ColorVariations.cs:19-20). The base is the
// annotation's OWN color[4] (NOT a node category). bg = AnnotationBackground(b0.12,s1,op0.7).Fade(0.8);
// outline = AnnotationOutline(b1,s0,op0) when unselected (desaturates to a faint edge) or pure white
// (ForegroundFull) when selected. text = OperatorLabel(b1.3,s0.4,op1) of the color faded by zoom
// (caller supplies `fade`, multiplied into the color's alpha = TiXL annotation.Color.Fade(fade)).
ImU32 annotationBgColor(const float rgba[4]);
ImU32 annotationOutlineColor(const float rgba[4], bool selected);
ImU32 annotationTextColor(const float rgba[4], float fade);

// Isolation test (ARCHITECTURE.md 鐵律 5): type hues + bg-darker-than-label invariant.
int runNodeStyleSelfTest(bool injectBug);

}  // namespace sw::ui
