// app/frame_cook — the per-frame PRODUCTION cook (product behaviour, out of the main shell).
// Zone: app. Depends on runtime only (never ui — the caller resolves viewTarget from ui
// session state and passes the plain child id).
//
// One call = one frame: advance the app clock, rebuild the resident projection if the lib
// revision changed (document.h contract), cook every AudioReaction instance from the live
// spectrum (resident extOut), then cook the RESIDENT eval graph into pg's target.
#pragma once
#include <string>

namespace sw {
class PointGraph;
}

namespace sw::framecook {

// Cook one production frame into `pg`. `targetPath` = the RESIDENT path the viewport shows
// (pin/selection/terminal — resolved by the shell, composition-aware: usually
// doc::residentPathFor(childId), but the terminal fallback may live at the root while the
// canvas is deep inside a compound).
void run(PointGraph& pg, const std::string& targetPath);

// Read a resident node's externally-cooked outputs (AudioReaction level/hit/count) by
// resident path — the UI's window into live values (node faces). Returns nullptr when the
// path isn't resident (e.g. just deleted). Pointer is valid this frame only (rebuild-on-edit).
const float* residentOut(const char* path);

// --- Transport (S5): the two-clock playback head, owned HERE (app: the per-frame driver hands
// it a real wall-clock deltaTime each run()). The UI/toolbar drives it through these; the cook
// reads position (playhead -> automation) and fxTime (wall clock -> stateful sims) off it. ---
void transportPlay();
void transportPause();
void transportToggle();   // play <-> pause (toolbar button)
void transportScrub(double bars);  // jump the playhead (timeline drag / numeric scrub)
bool transportPlaying();
double transportPosition();  // playhead, bars (the displayed/scrubbed value)
double transportFxTime();    // wall clock, bars (runs while paused)
double transportBpm();
void   transportSetBpm(double bpm);  // also writes lib.composition.bpm (the persistence home)

}  // namespace sw::framecook
