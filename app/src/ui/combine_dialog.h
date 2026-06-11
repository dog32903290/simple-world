// ui/combine_dialog — the canvas context menu + "Combine into new symbol" naming dialog
// (批次 4, 照 TiXL GraphContextMenu "Combine into new type..." + CombineToSymbolDialog).
// Zone: ui. Split from editor_ui (one file one duty); shares only these two entry points.
#pragma once

namespace sw::ui {

// Call BETWEEN ed::Begin/ed::End (uses ed::Suspend/Resume): right-click on a node opens
// the context menu; "Combine..." captures the selection and arms the dialog.
void drawCanvasContextMenu();

// Call AFTER ed::SetCurrentEditor(nullptr), still inside the canvas host window: the modal
// name dialog; Combine calls doc::doCombine (not undoable, 照 TiXL).
void drawCombineDialog();

}  // namespace sw::ui
