// ui/gradient_widget — interactive gradient color-band preview widget (Inspector Gradient face).
//
// Port of TiXL GradientEditor.cs (Editor/Gui/UiHelpers/GradientEditor.cs).
// DrawGradient (bar rendering) + DrawHandle (stop handle squares) + add-stop + delete-stop (trash
// zone) + color-picker popup (ImGui::ColorEdit4) + interpolation context-menu.
//
// Zone: ui. Depends on runtime/sw_gradient.h (gradient value type). No Metal, no app layer.
//
// Entry point:
//   sw::ui::drawGradientWidget(gradient, width)  — pass the SwGradient to show/edit; returns true if
//                                                  the gradient was modified this frame.
//
// FORK (named vs TiXL GradientEditor.cs):
//   • id-by-index: TiXL uses Guid per stop (step.Id = GradientEditor stop identity for drag). sw drops
//     Guid (sw_gradient.h drop-Guid fork) — stop identity uses array index; re-sorting can remap the
//     selected stop, but sort only happens on drag-release (one frame latency, acceptable).
//   • no presets: TiXL has GradientPresets save/load menu — deferred TODO.
//   • no cloneIfModified: TiXL's ref-param / clone-on-first-edit pattern. sw edits in-place (caller owns).
//   • interpolation sub-menu: shown as a popup context menu; TiXL shows it inside ContextMenuForItem.
#pragma once

namespace sw {
struct SwGradient;
}

namespace sw::ui {

// Draw a gradient color-band preview + interactive editor for `gradient`.
// `width` = widget width in pixels (default 240). Height = kBarHeight + kHandleSize (39px total).
// Returns true if the gradient was modified this frame.
// Caller is responsible for bumpLibRevision() + undo command when this returns true.
bool drawGradientWidget(SwGradient& gradient, float width = 240.0f);

}  // namespace sw::ui
