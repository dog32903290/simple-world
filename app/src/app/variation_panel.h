// app/variation_panel — Lane L1 (Variation / Snapshot): the FULL VARIATION PANEL state + wiring (P2).
// P1 (variation_live.{h,cpp}) shipped the thinnest vertical slice — ONE crossfader fading ONE Float
// param A→B through the spring + the document bridge. P2 is the whole performance surface around it:
//
//   1. SNAPSHOT POOL — grab (capture the live composition as a snapshot at a slot), activate (apply a
//      filled slot), delete. The pool is the document-vocabulary snapshot store (DocVariation per slot)
//      riding ON the runtime VariationPool + VariationCrossfader + variation_snapshot_actions.h state
//      machine (the same engines P1 used as the value-agnostic spring driver).
//   2. N-WAY WEIGHTED MIX — per-snapshot weight sliders → buildNWayMixCommand → a blended override map
//      applied to the live graph (the N-way cousin of P1's 2-way writeback, undo-able).
//   3. FULL 2-WAY CROSSFADER — pick a left snapshot + a right snapshot, drag the 0..127 fader; the
//      live tick (already in frame_cook, shared with P1) damps the spring (FIXED 1/60) and writes the
//      blended override every frame, committing one undoable entry on settle.
//
// ZONE: app (product behaviour — owns the panel's snapshot pool + crossfader state, drives g_lib
// through the document-override bridge). app -> runtime (VariationPool / VariationCrossfader /
// snapshot_actions / variation_mix) + app (variation_apply command builders). NO imgui, NO platform.
// The imgui WINDOW that paints this state lives in ui/variation_panel.{cpp,h} (a standalone window
// like Inspector / Output, deliberately OUT of the contended node_draw / editor_ui wire paths).
//
// ── TiXL ground-truth ────────────────────────────────────────────────────────────────────────────
//  Pool grid          VariationBaseCanvas.cs / SnapshotCanvas.cs / VariationThumbnail.cs — a 3-column
//                     grid of 160×90 thumbnails; each cell shows Title (bottom-left) + ActivationIndex
//                     (bottom-right, "00"), an active-dot, a selection border. Bare click = activate-
//                     or-create (SnapshotActions.ActivateOrCreateSnapshotAtIndex); Del = remove.
//  Snapshot capture   VariationHandling.cs:106-164 — capture ONLY EnabledForSnapshots children → a
//                     Variation @ index. sw: capture every child's Float-slot effective value (the
//                     document's float-per-slot rail) into a DocVariation.
//  N-way Mix          ExplorationVariation.cs:66-191 — Σ(neighbourValue·weight)/Σweight per param, with
//                     missing-neighbour = current-value fallback. The Exploration window's surface is a
//                     2D bilinear pad; sw exposes the underlying explicit per-snapshot WEIGHT here (the
//                     faithful N-way control — fork-mix-weights-as-sliders, named below).
//  Crossfader         BlendActions.cs:63-152, 215-262 — left @ fader 0 / right @ fader 127, spring
//                     (k=20, 1/60, settle 0.0005). TiXL drives it from a physical MIDI fader; sw draws
//                     an on-screen 0..127 slider (the same control-surface fork P1 established).
//
// ── NAMED FORKS ──────────────────────────────────────────────────────────────────────────────────
//  fork-pool-docvocab     — TiXL captures a Variation in runtime InputValue vocab; sw captures a
//    DocVariation (parameterSets[childId][slotId]=float) because sw's value rail is float-per-slot and
//    the document-override bridge (variation_apply.h) speaks exactly that. The runtime VariationPool is
//    still driven (one trivial scalar per slot) purely as the snapshot_actions state machine + the
//    crossfader's value-agnostic spring — identical to how P1 used it.
//  fork-mix-weights-as-sliders — TiXL's N-way Mix surface is a 2D bilinear pad (4-neighbour barycentric
//    weights). sw exposes the SAME ExplorationVariation.Mix math through explicit per-snapshot weight
//    sliders — the faithful underlying control (every weight is independent), not the pad gesture. The
//    blend math (Σv·w/Σw, missing=current) is byte-identical (buildNWayMixCommand → runtime mixFloat).
//  fork-crossfader-on-screen-slider — inherited from P1: the 0..127 fader is an on-screen ImGui slider,
//    not a MIDI device (TiXL BlendActions is MIDI-only). Same spring, same constants.
#pragma once
#include <string>
#include <vector>

namespace sw {
struct SymbolLibrary;
}

namespace sw::varpanel {

// One pool slot's UI/readout state (for the imgui grid). `filled` == a snapshot was grabbed here.
struct SlotInfo {
  int index = 0;          // 1-based activation index (the slot's grid identity)
  bool filled = false;    // is a snapshot stored at this slot?
  std::string title;      // the snapshot's title ("" → "Untitled" in the grid)
  bool active = false;    // is this the crossfader's active snapshot? (paints the active-dot)
  int paramCount = 0;     // how many (childId,slotId) Float params this snapshot tracks (cell readout)
};

// ── Pool grid ──────────────────────────────────────────────────────────────────────────────────
// Number of grid slots the panel offers (a fixed pad bank — TiXL's pool is unbounded, but the grid
// is paged; sw uses a fixed bank, faithful to the MIDI-pad mental model). 3 columns × kRows rows.
constexpr int kSlotColumns = 3;
constexpr int kSlotRows = 4;
constexpr int kSlotCount = kSlotColumns * kSlotRows;  // slots 1..kSlotCount

// GRAB (= SnapshotActions overwrite/Save): capture the live composition's Float-slot values as a
// snapshot at `slotIndex` (1-based), overwriting any existing. Returns false if there is no current
// composition. `lib` is the live document (doc::g_lib).
bool grabSnapshot(SymbolLibrary& lib, int slotIndex);

// ACTIVATE (= SnapshotActions ActivateOrCreateSnapshotAtIndex on a FILLED slot): apply the snapshot at
// `slotIndex` to the live graph as undo-able overrides AND set it active on the crossfader (left side).
// No-op (returns false) if the slot is empty. `lib` is the live document.
bool activateSnapshot(SymbolLibrary& lib, int slotIndex);

// DELETE (= SnapshotActions RemoveSnapshotAtIndex): drop the snapshot at `slotIndex`. Returns false if
// the slot was already empty (TiXL "No preset to delete" no-op). Does NOT touch the live graph.
bool deleteSnapshot(int slotIndex);

// Read the whole grid for the imgui window (one SlotInfo per slot 1..kSlotCount).
std::vector<SlotInfo> slots();

// ── N-way weighted mix ───────────────────────────────────────────────────────────────────────────
// Set the per-slot mix weight (UI weight slider). `slotIndex` 1-based; `weight` >= 0. A weight of 0
// excludes the slot from the mix. Weights persist until changed (the panel owns them).
void setMixWeight(int slotIndex, float weight);
float mixWeight(int slotIndex);

// APPLY the N-way weighted mix: build buildNWayMixCommand over every FILLED slot with weight>0 (each a
// DocMixNeighbour at its weight) and push it as ONE undoable document edit. No-op (returns false) if no
// slot is weighted (nothing to mix). `lib` is the live document.
bool applyMix(SymbolLibrary& lib);

// ── Full 2-way crossfader ──────────────────────────────────────────────────────────────────────
// Arm the crossfader endpoints: left snapshot @ fader 0, right snapshot @ fader 127. Both must be
// FILLED slots. Returns false if either is empty. Resets the fader to 0 (the left/active side). The
// per-frame tick() (frame_cook, shared with P1) then damps the spring and writes the blended override.
bool armCrossfade(SymbolLibrary& lib, int leftSlot, int rightSlot);

// Is the crossfader armed (armCrossfade succeeded, endpoints still filled)?
bool crossfadeArmed();
int crossfadeLeft();   // the left endpoint slot (fader 0), -1 if not armed
int crossfadeRight();  // the right endpoint slot (fader 127), -1 if not armed

// Move the crossfader fader (TiXL UpdateBlendingTowardsProgress). `midiValue` in [0,127]: 0 = left,
// 127 = right. Sets the spring target; the blend lands over the next frames as tick() damps.
void updateCrossfade(float midiValue);

// ONE frame of the crossfader live pipe (called by frame_cook once per frame, alongside P1's tick).
// Advances the spring by the FIXED 1/60 step, applies the damped left→right blend to the graph as a
// transient override, and commits one undoable entry on settle. No-op when not armed.
void tickCrossfade(SymbolLibrary& lib);

// The damped weight the crossfade spring is at (0 = left, 1 = right). For the UI readout / golden.
float crossfadeWeight();

// Reset ALL panel state (pool, weights, crossfader). Called on New / Open (document swap) — a loaded
// doc has new child ids, so the captured snapshots dangle.
void reset();

// Headless RED→GREEN proof of the panel wiring (--selftest-variation-panel):
//   (1) GRAB captures the live Float values; ACTIVATE applies them as a readable graph override;
//       DELETE empties the slot (pool grab/activate/delete state transitions).
//   (2) N-WAY MIX of two snapshots at weights (1,3) lands the correct Σ(v·w)/Σw weighted blend on the
//       graph (asserted against variation_mix.h math), readable back through effectiveInput, undo-able.
//   (3) the full crossfader settles to the RIGHT endpoint (writeback reached the graph).
// injectBug corrupts one 承重 leg (mix weights flattened to equal) -> the fixed wants bite. 0 = PASS.
int runVariationPanelSelfTest(bool injectBug);

}  // namespace sw::varpanel
