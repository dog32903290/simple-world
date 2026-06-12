// app/frame_cook — the per-frame PRODUCTION cook (product behaviour, out of the main shell).
// Zone: app. Depends on runtime only (never ui — the caller resolves viewTarget from ui
// session state and passes the plain child id).
//
// One call = one frame: advance the app clock, rebuild the resident projection if the lib
// revision changed (document.h contract), cook every AudioReaction instance from the live
// spectrum (resident extOut), then cook the RESIDENT eval graph into pg's target.
#pragma once
#include <cstdint>
#include <map>
#include <string>

#include "runtime/audio_reaction.h"  // AudioReactionState (per-instance memory, keyed by path)

namespace sw {
class PointGraph;
struct ResidentEvalGraph;
struct SpectrumSnapshot;
struct SymbolLibrary;
struct Transport;
}

namespace sw::framecook {

// Cook one production frame into `pg`. `targetPath` = the RESIDENT path the viewport shows
// (pin/selection/terminal — resolved by the shell, composition-aware: usually
// doc::residentPathFor(childId), but the terminal fallback may live at the root while the
// canvas is deep inside a compound).
void run(PointGraph& pg, const std::string& targetPath);

// dt 分流 seam (refuter-C 修2): the per-frame wall dt is measured ONCE, then split — the
// TRANSPORT eats it raw (TiXL Playback advances from a Stopwatch, unclamped; clamping it made
// the playhead lag a stalled frame while the audio free-ran, and the follow rule seeked
// BACKWARDS audibly), while the Metal sim's ctx.deltaTime gets this clamped copy (≤ 0.25s —
// an integration-stability ceiling, never a playhead rule). Exposed so the seam itself is
// headless-bitable (--selftest-arclock leg ③).
double simDeltaFromWall(double dtSecs);

// Read a resident node's externally-cooked outputs (AudioReaction level/hit/count) by
// resident path — the UI's window into live values (node faces). Returns nullptr when the
// path isn't resident (e.g. just deleted). Pointer is valid this frame only (rebuild-on-edit).
const float* residentOut(const char* path);

// Cook every AudioReaction instance in `g` for this frame (TiXL parity): resolve its params
// through the resident drivers, run the stateful algorithm on `spec`, write the 3 outputs onto
// the node's extOut. ★時鐘域 SEAM (refuter-S5 BROKEN-A): the time AudioReaction eats is derived
// HERE from the transport — t.fxTime, which is BARS (TiXL LocalFxTime IS Playback.FxTimeInBars,
// EvaluationContext.cs:49; MinTimeBetweenHits debounces in the SAME domain). run() calls this
// with the app transport; --selftest-arclock calls it with its own and BITES anyone who feeds
// seconds back in. `state` keys off the resident path (survives rebuilds, per-instance).
void cookAudioReactionNodes(ResidentEvalGraph& g, const SpectrumSnapshot& spec,
                            const Transport& t, uint32_t frameIndex, const SymbolLibrary* lib,
                            std::map<std::string, AudioReactionState>& state);

// AR clock-domain pin (refuter-S5 盲區 3, --selftest-arclock): proves through the REAL cook
// seam above that AudioReaction receives BARS — hit timestamps == transport.fxTime (bars, not
// seconds), and halving the BPM halves the wall-clock hits a fixed-debounce pulse train yields
// (the debounce window scales ∝ 240/BPM in wall time). injectBug expects the seconds-domain
// numbers instead -> FAILS (teeth: reverting bars→secs gets bitten).
int runArClockSelfTest(bool injectBug);

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
// Playback speed (= TiXL Playback.PlaybackSpeed). Gate lives in Transport::setRate (NaN
// refused, clamp ±16 = TimeControls.cs:92/106). NOT persisted — TiXL never saves PlaybackSpeed
// either (runtime playback state, not a composition setting). BPM and speed are two independent
// knobs that multiply in advance(); neither setter writes the other.
double transportRate();
void   transportSetRate(double rate);
// Play backwards toggle (= TiXL J key path, Transport::playBackwards() semantics):
//   playing-backwards -> stop; else (stopped or forward) -> rate=-1, playing.
// Named fork vs TiXL ×2 ladder: see keymap.cpp and transport.h.
void   transportPlayBackwards();

}  // namespace sw::framecook
