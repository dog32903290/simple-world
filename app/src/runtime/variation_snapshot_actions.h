// runtime/variation_snapshot_actions — Lane L1 (Variation / Snapshot): the SNAPSHOT-SLOT state
// machine. The pure action layer behind the MIDI pad / number-key grid: given a slot index and a
// mode (activate-or-create / overwrite-save / remove), drive the VariationPool (store/delete) and the
// VariationCrossfader (set-active) exactly as TiXL's SnapshotActions does. Pure CPU state machine —
// no GPU, no UI, no MIDI; machine-verifiable.
//
// ZONE: runtime (pure computation — no GPU, no platform, no upward deps). Header-only free functions
// over the existing VariationPool + VariationCrossfader substrate (variation_pool.h /
// variation_crossfader.h). Sits ON those leaves; adds no new state of its own.
//
// ── TiXL ground-truth (READ-ONLY external/tixl, byte-faithful) ──────────────────────────────────
// Editor/Gui/Interaction/Variations/SnapshotActions.cs — three static actions, each bound to a MIDI
// pad InputMode (Default / Save / Delete) by the device layer (ApcMini/Launchpad/NanoControl/…):
//
//   ActivateOrCreateSnapshotAtIndex(index)  [InputModes.Default — a bare pad press]:
//     if TryGetSnapshot(index, out existing):              // slot is FILLED
//         pool.Apply(instance, existing)                   //   → apply it (set live params to it)
//         BlendActions.SetActiveSnapshot(index)            //   → crossfader left=index, clear right
//         return
//     CreateOrUpdateSnapshotVariation(index)               // slot is EMPTY → CAPTURE current
//     pool.UpdateActiveStateForVariation(index)            //   mark it Active in the pool
//     BlendActions.SetActiveSnapshot(index)                //   crossfader set-active
//
//   SaveSnapshotAtIndex(index)  [InputModes.Save — the OVERWRITE modifier held]:
//     CreateOrUpdateSnapshotVariation(index)               // ALWAYS capture (delete-then-add) →
//     pool.UpdateActiveStateForVariation(index)            //   OVERWRITES even a filled slot
//     BlendActions.SetActiveSnapshot(index)
//     // NOTE: NO TryGetSnapshot branch — Save unconditionally overwrites. That is the ONLY difference
//     // between Save and ActivateOrCreate: ActivateOrCreate APPLIES a filled slot, Save REPLACES it.
//
//   RemoveSnapshotAtIndex(index)  [InputModes.Delete — the delete modifier held]:
//     if TryGetSnapshot(index, out snapshot):  pool.DeleteVariation(snapshot)   // remove the slot
//     else:                                    warn "No preset to delete"       // empty → no-op
//
//   BlendActions.SetActiveSnapshot(index) [BlendActions.cs:167]:
//     _snapshotLeft = index; _activeIsLeft = true; _snapshotRight = -1;   // left=index, clear right.
//   This is EXACTLY VariationCrossfader::setActiveSnapshot(index) (variation_crossfader.h) — left side
//   = index, active = left, right cleared (the "opposite side" the next fader move will target).
//
//   CreateOrUpdateSnapshotVariation(index) [VariationHandling.cs:106-136]: delete any existing snapshot
//   at index, capture ONLY EnabledForSnapshots children → fresh Variation @ index. This is EXACTLY
//   VariationPool::createOrUpdateSnapshot(index, children) (variation_pool.h) — delete-then-capture
//   with the EnabledForSnapshots filter already faithful in the pool leaf.
//
// ── NAMED FORK ──────────────────────────────────────────────────────────────────────────────────
//  fork-snapshot-action-keybind-out — TiXL binds these three actions to physical MIDI pads via
//    InputModes (Default→ActivateOrCreate, Save→Save, Delete→Remove; see ApcMini.cs / Launchpad.cs /
//    NanoControl8.cs). That keybind→action mapping (which pad, which modifier, which device) is the
//    UI/input layer — 柏為's domain — and is DELIBERATELY OUT of this runtime leaf. Here we model only
//    the PURE actions the keybinds resolve to:
//      activateOrCreate(pool, crossfader, slotIndex, overwrite, children)
//        overwrite=false → ActivateOrCreateSnapshotAtIndex (apply-if-filled, capture-if-empty)
//        overwrite=true  → SaveSnapshotAtIndex            (always capture = OVERWRITE)
//      removeSlot(pool, slotIndex) → RemoveSnapshotAtIndex
//    The modifier→overwrite / modifier→remove decision is made by the keybind layer and handed to us
//    as the `overwrite` bool / the choice of function — we never read a keyboard or a MIDI device.
//
//  fork-snapshot-apply-via-setactive — TiXL's pool.Apply runs an undoable command that writes the
//    snapshot's stored values onto the live Instance inputs, THEN SetActiveSnapshot wires the crossfader.
//    This runtime layer has no command stack / Instance graph (that is the document-override batch,
//    app/variation_apply.{h,cpp}). So applying a FILLED slot here = crossfader.setActiveSnapshot(index)
//    only — the durable state change SnapshotActions makes (left=index, active=left, right cleared).
//    The live-value write is the document batch's job; the SET-ACTIVE state machine is this leaf's job.
#pragma once

#include <vector>

#include "runtime/variation_crossfader.h"  // VariationCrossfader
#include "runtime/variation_pool.h"        // VariationPool / SnapshotChildState

namespace sw {

// The result of a snapshot action — what the state machine did this press (for tests / UI readout).
enum class SnapshotActionResult : uint8_t {
  Applied,    // slot was FILLED, activate-mode → applied (set-active), no capture
  Captured,   // slot was EMPTY (or overwrite-mode) → captured current into the slot
  Removed,    // slot was FILLED, remove-mode → snapshot deleted
  NoOp,       // remove-mode on an EMPTY slot → nothing to delete (TiXL warns + returns)
};

// ActivateOrCreate / Save — the bare-pad and overwrite-modifier actions (SnapshotActions.cs:7-44).
//   overwrite == false → ActivateOrCreateSnapshotAtIndex:
//       slot FILLED → APPLY it: crossfader.setActiveSnapshot(slotIndex). Result = Applied.
//       slot EMPTY  → CAPTURE current into the slot + set-active.       Result = Captured.
//   overwrite == true  → SaveSnapshotAtIndex:
//       ALWAYS CAPTURE current (delete-then-add = overwrite) + set-active. Result = Captured.
//
// `children` is the live SnapshotChildState list to capture from (only used when capturing). The
// EnabledForSnapshots filter is applied inside pool.createOrUpdateSnapshot (faithful in the pool leaf).
// Returns Applied (filled+activate) or Captured (empty, or overwrite) — exactly the TiXL branch taken.
inline SnapshotActionResult activateOrCreate(VariationPool& pool,
                                             VariationCrossfader& crossfader,
                                             int slotIndex,
                                             bool overwrite,
                                             const std::vector<SnapshotChildState>& children,
                                             const std::string& title = "") {
  // ActivateOrCreate (NOT overwrite): if the slot is already filled, APPLY it — do NOT capture.
  if (!overwrite && pool.tryGetSnapshot(slotIndex) != nullptr) {
    // TiXL: pool.Apply(instance, existing) [live-value write = doc batch] + SetActiveSnapshot(index).
    crossfader.setActiveSnapshot(slotIndex);  // fork-snapshot-apply-via-setactive (set-active only)
    return SnapshotActionResult::Applied;
  }
  // EMPTY slot (ActivateOrCreate) OR overwrite-mode (Save): CAPTURE current into the slot.
  // createOrUpdateSnapshot deletes any existing at slotIndex first → faithful overwrite (delete-then-add).
  pool.createOrUpdateSnapshot(slotIndex, children, title);
  // TiXL UpdateActiveStateForVariation(index) marks it Active in the pool; here the durable set-active
  // state lives on the crossfader (the pool's per-Variation State flag is a UI-paint fact, not modeled).
  crossfader.setActiveSnapshot(slotIndex);
  return SnapshotActionResult::Captured;
}

// Remove — the delete-modifier action (SnapshotActions.cs:46-62 RemoveSnapshotAtIndex).
//   slot FILLED → delete the snapshot at slotIndex.   Result = Removed.
//   slot EMPTY  → TiXL warns "No preset to delete" + returns; nothing happens. Result = NoOp.
// Does NOT touch the crossfader (TiXL's Remove only deletes from the pool — it does not re-active).
inline SnapshotActionResult removeSlot(VariationPool& pool, int slotIndex) {
  if (pool.tryGetSnapshot(slotIndex) == nullptr) {
    return SnapshotActionResult::NoOp;  // TiXL: Log.Warning("No preset to delete") + return.
  }
  pool.removeSnapshot(slotIndex);  // TiXL: ActivePoolForSnapshots.DeleteVariation(snapshot).
  return SnapshotActionResult::Removed;
}

}  // namespace sw
