#pragma once
// app/snapshot — PRODUCT "Snapshot" action: save the current Output render to a PNG the
// user keeps (zone: app). Faithful port of TiXL RenderProcess.TryRenderScreenShot
// (Editor/Gui/Windows/RenderExport/RenderProcess.cs:47-63), surfaced by the Output-window
// toolbar Snapshot button (TiXL OutputWindow.cs:332 Icon.Snapshot).
//
// TiXL writes <projectFolder>/Screenshots/<yyyy_MM_dd-HH_mm_ss_fff>.png DIRECTLY — no save
// dialog (its screenshot path has none). We match that: no NFD modal (which the hand could
// not drive anyway), a deterministic timestamped name in a Screenshots dir. This keeps the
// affordance absent-safe and hand/scenario-drivable.
//
// Direction ui → app → platform: the Output window calls saveSnapshot(); this owns the
// path policy + the texture readback handoff to platform::saveTextureToPng.
#include <string>

namespace MTL { class Texture; }

namespace sw {

// The directory product snapshots are written into:
//   - $SW_SNAPSHOT_DIR if set (deterministic TEST seam — the scenario points it at a temp
//     dir so the assertion can find the PNG without an NFD dialog),
//   - else <project folder>/Screenshots when a document has been saved (TiXL's home),
//   - else <assets>/Screenshots as the unsaved-document fallback (a stable user location).
std::string snapshotDir();

// Faithful TiXL filename stamp: "yyyy_MM_dd-HH_mm_ss_fff" (local time, millisecond field).
std::string snapshotStamp();

// Read back `tex` and write it as snapshotDir()/<stamp>.png. On success returns the written
// path; on failure (null/empty texture, unwritable dir, encode error) returns "". `tex` is
// the live Output preview texture (sw::previewTexture()); the caller passes it so this stays
// out of the Metal-owning shell. `outPath`, if non-null, always receives the intended path
// (even on failure) so the caller can report it.
std::string saveSnapshot(MTL::Texture* tex, std::string* outPath = nullptr);

}  // namespace sw
