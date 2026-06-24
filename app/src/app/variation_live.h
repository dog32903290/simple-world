// app/variation_live — Lane L1 (Variation / Snapshot): the LIVE PERFORMANCE PIPE (P1, the thinnest
// vertical slice). This is the first end-to-end "crossfader → graph" water pipe and the TEMPLATE the
// rest of the performance loop (P2-P5) copies.
//
// ZONE: app (product behaviour — owns the live crossfader state, drives g_lib through the
// document-override bridge). app -> runtime (VariationCrossfader spring, variation_apply command).
// No UI, no platform. One responsibility: hold the P1 crossfader and advance/apply it per frame.
//
// ── What this CLOSES ───────────────────────────────────────────────────────────────────────────────
// Before P1 the engine was complete but DARK: the crossfader (variation_crossfader.h) and the
// document bridge (variation_apply.h) both existed and were selftested, but NOBODY ticked the
// crossfader per frame and its blended value never reached the graph override. This module is the
// missing driver: frame_cook calls tick() once per frame; the toolbar fader calls updateFader().
//
// ── TiXL ground-truth (BlendActions.cs / SmoothVariationBlending) ───────────────────────────────────
//   UpdateBlend() runs once per frame: spring-damp _dampedWeight toward _targetWeight with a FIXED
//   frameDuration = 1/60 (BlendActions.cs:244 — "Fixme: Playback.LastFrameDuration", i.e. TiXL itself
//   hard-codes 1/60 and does NOT consume the real dt). Each frame it applies the blend at the damped
//   weight (BeginBlendTowardsSnapshot); when |velocity| < 0.0005 it commits (ApplyCurrentBlend) +
//   flips active. The VariationCrossfader (runtime) already hard-codes kTimeStep = 1/60 internally, so
//   the per-frame tick NEVER feeds the real wall dt into the spring — dt is only a loop heartbeat.
#pragma once
#include <string>

namespace sw {
struct SymbolLibrary;
}

namespace sw::varlive {

// Seed the P1 slice: pick ONE target (childId, slotId) of the root composition and arm the crossfader
// with two endpoints — A (fader 0) = `valueA`, B (fader 127) = `valueB`. This is the narrowed
// single-parameter pipe (P1 proves ONE param end-to-end; the full pool is P2). `compositionSymbolId`
// is the composition that owns the child (usually g_lib.rootId). Returns false if the target slot
// isn't a Float input of the child (nothing to blend). Idempotent: re-seeding resets the spring.
bool seedSliceTarget(SymbolLibrary& lib, const std::string& compositionSymbolId, int childId,
                     const std::string& slotId, float valueA, float valueB);

// Is the P1 slice armed (seedSliceTarget succeeded and not cleared)?
bool armed();

// The crossfader's raw fader (TiXL UpdateBlendingTowardsProgress). `midiValue` in [0,127]: 0 = A,
// 127 = B. Sets the spring target; the actual blend lands over the next frames as tick() damps to it.
void updateFader(float midiValue);

// ONE frame of the live pipe (called by frame_cook once per frame, AFTER the resident projection is
// up to date). Advances the spring by the FIXED 1/60 step (NOT the real dt), applies the damped blend
// to the graph override through buildBlendTowardsVariationCommand, and on settle commits one undoable
// entry. No-op when not armed. `lib` is the live document (doc::g_lib).
void tick(SymbolLibrary& lib);

// The damped weight the spring is currently at (0 = A, 1 = B). For UI readout / the golden.
float dampedWeight();
// The raw target weight the last updateFader set (for the golden / UI).
float targetWeight();
// The current graph-override value of the slice target (read back through effectiveInput) — the
// side-effect the eye-hand verification reads. -999 if not armed / target missing.
float sliceOverrideValue(SymbolLibrary& lib);

// Reset the P1 slice (clears the armed target + spring). Called on New/Open (document swap).
void reset();

// Headless RED->GREEN proof of the live pipe (--selftest-variation-live):
//   (1) the spring advances by the FIXED 1/60 step — feeding a 10x dt does NOT change the per-tick
//       delta (parity landmine: real dt must never reach the springDamp step);
//   (2) repeated ticks SETTLE (|vel| < 0.0005) and the settled override == the B endpoint (writeback
//       reached the graph);
//   (3) the override actually CHANGES the target's effectiveInput (the pipe is live, not a side map).
// injectBug feeds the real dt into the step (and skips the writeback) -> the fixed wants bite. 0=PASS.
int runVariationLiveSelfTest(bool injectBug);

}  // namespace sw::varlive
