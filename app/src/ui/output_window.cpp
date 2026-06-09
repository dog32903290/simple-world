// ui/output_window — TiXL OutputWindow + ViewSelectionPinning, faithful + minimal.
// Zone: ui. Depends on app(document) + runtime(graph) + the shell's previewTexture seam.
// Never mutates the graph; the pin (g_pinnedNode) is session-only state, never serialized.
//
// view ⊥ graph: "what I'm building" (the graph) and "what I'm looking at" (the pin) are
// two different things. The cook target is decided by the pin in the shell (main.cpp); this
// window only drives the pin and shows the result. OUTPUT_PIN_VIEWER_CONTRACT §4-A / §5.
#include "ui/output_window.h"

#include <string>

#include "imgui.h"

#include "app/document.h"
#include "ui/editor_ui.h"  // g_pinnedNode (the session pin) + g_selectedNode (what Pin grabs)
#include "runtime/graph.h"
#include "verify/eye/eye.h"  // one-line hook: record the Pin button rect for the hand

// The shell renders the live preview into a texture and exposes it through this STABLE
// accessor (defined in main.cpp). nullptr until the first frame has rendered.
namespace MTL { class Texture; }
namespace sw { MTL::Texture* previewTexture(); }

namespace sw::ui {

namespace {
// A node's primary output type ("Points" | "ParticleForce" | "Float"). Empty if the node
// has no output port (a draw node like DrawPoints) or no spec — exactly TiXL's typed
// OutputUi lookup: no OutputUi for the type -> nothing to show.
std::string outputTypeOf(const sw::Node* n) {
  const sw::NodeSpec* s = n ? sw::findSpec(n->type) : nullptr;
  if (!s) return "";
  for (const sw::PortSpec& p : s->ports)
    if (!p.isInput) return p.dataType;
  return "";
}
}  // namespace

void drawOutputWindow() {
  const ImGuiViewport* vp = ImGui::GetMainViewport();
  // Default spot: lower-right, clear of the toolbar (top-left), the Inspector (top-right)
  // and the default graph's nodes. 柏為 drags it wherever he wants — it's his viewport.
  ImGui::SetNextWindowSize(ImVec2(380.0f, 420.0f), ImGuiCond_FirstUseEver);
  ImGui::SetNextWindowPos(ImVec2(vp->WorkPos.x + vp->WorkSize.x - 404.0f,
                                 vp->WorkPos.y + vp->WorkSize.y - 444.0f),
                          ImGuiCond_FirstUseEver);
  ImGui::Begin("Output");

  // Drop a stale pin (the pinned node was deleted) -> fall back to the terminal.
  if (g_pinnedNode != 0 && !sw::doc::g_graph.node(g_pinnedNode)) g_pinnedNode = 0;
  const bool pinned = g_pinnedNode != 0;
  const sw::Node* pinnedNode = pinned ? sw::doc::g_graph.node(g_pinnedNode) : nullptr;

  // --- toolbar: Pin / switch / Unpin, on the active op (TiXL Icon.Pin + PinSelectionToView).
  // One button, three states so switching what you watch is a SINGLE click (§6.5):
  //   - a node is selected that isn't the pinned one -> "Pin selected" pins / switches to it
  //   - otherwise, if pinned -> "Unpin" drops back to the graph terminal ---
  const bool canPinSelection = g_selectedNode != 0 && g_selectedNode != g_pinnedNode;
  if (ImGui::Button(canPinSelection ? "Pin selected" : (pinned ? "Unpin" : "Pin selected"))) {
    if (canPinSelection)
      g_pinnedNode = g_selectedNode;                 // pin or switch to the active op
    else if (pinned)
      g_pinnedNode = 0;                              // back to the graph terminal
  }
  sw::eye::recordItem("output_pin_btn");             // eye: hand off this button's screen rect
  ImGui::SameLine();
  if (pinned) {
    const sw::NodeSpec* ps = pinnedNode ? sw::findSpec(pinnedNode->type) : nullptr;
    ImGui::TextDisabled("%s (pinned)", ps ? ps->title.c_str() : "?");
  } else {
    ImGui::TextDisabled("Terminal (select a node, then Pin)");
  }

  // --- type honesty (§5): v1 only visualizes Points. A draw node (DrawPoints) is always
  // drawable (it renders its own input). Unpinned = the terminal draw node = drawable. ---
  bool drawable;
  std::string outType;
  if (!pinned)
    drawable = true;                                 // terminal draw node -> today's picture
  else if (pinnedNode && pinnedNode->type == "DrawPoints")
    drawable = true;                                 // pinned a draw node -> renders its input
  else {
    outType = outputTypeOf(pinnedNode);
    drawable = (outType == "Points");                // the one cell v1 fills
  }
  if (!drawable)
    ImGui::TextDisabled("no preview for output type \"%s\" yet",
                        outType.empty() ? "?" : outType.c_str());

  // --- the viewport: the shell's preview texture, cooked for the pinned/terminal node.
  // For an unsupported type the cook already cleared it to black (point_graph.cpp), so the
  // image area is black + the notice above = the honest "no preview" state (§6.4). ---
  ImVec2 avail = ImGui::GetContentRegionAvail();
  if (MTL::Texture* tex = sw::previewTexture(); tex && avail.x > 1.0f && avail.y > 1.0f)
    ImGui::Image(reinterpret_cast<ImTextureID>(tex), avail);

  ImGui::End();
}

}  // namespace sw::ui
