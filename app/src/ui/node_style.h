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

// Selection / hover outlines (TiXL UiColors.Selection is white, category-independent).
ImU32 nodeSelectedBorderColor();  // bright white — selected node outline
ImU32 nodeHoverBorderColor();     // soft white — hovered node outline

// Isolation test (ARCHITECTURE.md 鐵律 5): type hues + bg-darker-than-label invariant.
int runNodeStyleSelfTest(bool injectBug);

}  // namespace sw::ui
