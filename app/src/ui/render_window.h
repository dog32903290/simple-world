#pragma once
// ui/render_window — the Render-to-file settings window (port of TiXL RenderWindow.cs). A floating
// ImGui tool window (toggled from the toolbar) that puts a UI face on the export engine: time range +
// FPS + resolution preset + codec + output path + Start/Cancel + a live progress bar. Drives an
// app::ExportSession ONE frame per editor frame so the progress bar animates and Cancel is responsive.
// Zone: ui (imgui only). Reaches app(export_session/document/frame_cook) + the shell's Metal-context
// and Fill-baseline seams (defined in main.cpp), never Metal directly.
//
// TiXL blueprint = Editor/Gui/Windows/RenderExport/RenderWindow.cs (the settings form + progress
// controls). NAMED FORKS from TiXL are documented at each site in the .cpp (session-only settings vs
// per-project; text path vs native folder picker; floating vs dockable window; absolute-pixel
// resolution presets vs a resolution-factor). The per-frame pump = RenderProcess.Update() (cs:189-232).
namespace sw::ui {

// Toggle for the Render window (default OFF, like the other tool windows — an always-open panel over
// the canvas eats clicks). The toolbar flips this; drawRenderWindow() no-ops while it is false. bool&
// so the toolbar button can toggle it in place (same idiom as assetBrowserVisible()).
bool& renderWindowVisible();

// Draw the Render window this frame (no-op when renderWindowVisible() is false). Also pumps the active
// ExportSession by ONE frame per call — the per-editor-frame export step (TiXL RenderProcess.Update).
// Called from the shell's editor-chrome draw block (main.cpp), beside drawOutputWindow().
void drawRenderWindow();

// Machine-readable export progress for state.json (the one-line eye hook the shell wires in). Serializes
// the active session's {active, done, cancelled, framesDone, framesTotal} so a .scn can assert an
// export ran to completion. Returns "null" when no export has been started this session.
const char* renderExportStateJson();

}  // namespace sw::ui
