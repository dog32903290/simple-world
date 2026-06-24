#pragma once
// ui/asset_browser — the AssetLibrary resource browser (EXPERIENCE_SCOPE_GAPS #2). A standalone
// floating window (like Inspector / Output / Variation), deliberately OUT of the contended
// node_draw / editor_ui wire paths. It lists the available `Lib:` assets on disk (from
// app/asset_library), flags any the LOADED PROJECT references-but-can't-resolve (from app/asset_index),
// and on click CREATES a LoadImage op wired to that asset key through the undoable AddChildCommand —
// matching TiXL's drop-to-create behavior (AssetLib DropHandling: the image asset-type's primary
// operator is LoadImage, pointed at the asset). All data + command-payload logic lives in app/; this
// file is the imgui surface + the g_commands push only.
// Zone: ui (reads app::assetlib/assetidx/document, pushes app::g_commands; never touches the graph
// directly or the cook core).
namespace sw::ui {

// Draw the floating Asset Library window. Call once per frame alongside drawToolbar / drawInspector.
void drawAssetBrowser();

}  // namespace sw::ui
