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
#include "runtime/compound_graph.h"
#include "runtime/graph.h"  // findSpec (a compound child resolves like an atomic, N1)
#include "verify/eye/eye.h"  // one-line hook: record the Pin button rect for the hand

// The shell renders the live preview into a texture and exposes it through this STABLE
// accessor (defined in main.cpp). nullptr until the first frame has rendered.
namespace MTL { class Texture; }
namespace sw { MTL::Texture* previewTexture(); }

namespace sw::ui {

namespace {
// A child's primary output type ("Points" | "ParticleForce" | "Float"). Empty if it
// has no output port (a draw node like DrawPoints) or no spec — exactly TiXL's typed
// OutputUi lookup: no OutputUi for the type -> nothing to show.
std::string outputTypeOf(const sw::SymbolChild* c) {
  const sw::NodeSpec* s = c ? sw::findSpec(c->symbolId) : nullptr;
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

  // Drop a stale pin (the pinned node was deleted) -> resume following selection.
  const sw::Symbol* cur = sw::doc::currentSymbolConst();
  if (g_pinnedNode != 0 && (!cur || !sw::childById(*cur, g_pinnedNode))) g_pinnedNode = 0;
  const bool pinned = g_pinnedNode != 0;
  const sw::SymbolChild* pinnedNode = pinned ? sw::childById(*cur, g_pinnedNode) : nullptr;

  // --- toolbar: Pin / switch / Unpin, on the active op (TiXL Icon.Pin + PinSelectionToView).
  // Unpinned, the viewport FOLLOWS the selected node (TiXL); Pin LOCKS it so it stops
  // following (and clicking other nodes no longer changes the view). One button:
  //   - a node is selected that isn't the pinned one -> "Pin selected" locks / switches to it
  //   - otherwise, if pinned -> "Unpin" resumes following selection ---
  const bool canPinSelection = g_selectedNode != 0 && g_selectedNode != g_pinnedNode;
  if (ImGui::Button(canPinSelection ? "Pin selected" : (pinned ? "Unpin" : "Pin selected"))) {
    if (canPinSelection)
      g_pinnedNode = g_selectedNode;                 // lock / switch to the active op
    else if (pinned)
      g_pinnedNode = 0;                              // resume following selection
  }
  sw::eye::recordItem("output_pin_btn");             // eye: hand off this button's screen rect
  ImGui::SameLine();

  // What the viewport is actually showing (mirror the shell's cook-target priority in
  // main.cpp): the pinned node wins; else the selected node (follow); else the terminal.
  const sw::SymbolChild* viewNode = pinnedNode;
  if (!viewNode && g_selectedNode != 0 && cur) viewNode = sw::childById(*cur, g_selectedNode);
  const sw::NodeSpec* vs = viewNode ? sw::findSpec(viewNode->symbolId) : nullptr;
  if (pinned)
    ImGui::TextDisabled("%s (pinned)", vs ? vs->title.c_str() : "?");
  else if (viewNode)
    ImGui::TextDisabled("%s (selected)", vs ? vs->title.c_str() : "?");
  else
    ImGui::TextDisabled("Terminal (select a node to preview it)");

  // --- type honesty (§5): v1 only visualizes Points. A draw node (DrawPoints) is always
  // drawable (renders its own input). Nothing pinned/selected = the terminal draw node. ---
  bool drawable;
  std::string outType;
  if (!viewNode)
    drawable = true;                                 // terminal draw node -> today's picture
  else if (viewNode->symbolId == "DrawPoints")
    drawable = true;                                 // a draw/command node -> renders its input
  else {
    outType = outputTypeOf(viewNode);
    // Points -> reuse DrawPoints preview; Texture2D (RenderTarget) -> show the texture directly.
    drawable = (outType == "Points" || outType == "Texture2D");
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
