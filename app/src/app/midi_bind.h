// app/midi_bind — Lane L5 → cook (P3): the LIVE CONTROL-INPUT PIPE. The second of the three broken
// live-performance wires (P1/P2 fixed Variation; P3 fixes live MIDI/OSC → graph parameter). This is
// the app-layer driver that:
//   (1) STARTS the live sources (registerIoLiveSources): opens the OSC + virtual-MIDI loopback
//       transports and routes their decoded events into the binding table, so incoming MIDI/OSC
//       ACTUALLY FLOWS into the app (the grep-confirmed-missing app hook).
//   (2) WIRES the bound value to the cook (tick): each frame, every binding that points at a graph
//       parameter writes its current remapped value into that child's override + bumps the lib
//       revision — so the value reaches the graph parameter through the SAME document-override path
//       the resident cook reads (the L5-deferred "node input pull valueForTarget → 未來 L4/cook").
//   (3) drives MIDI LEARN: click a param → enter learn mode → the next live event captures its
//       channel/controller filter and binds that CC to that (childId, slotId).
//
// ZONE: app (product behaviour — owns the live binding state, drives g_lib through the override
// bridge, owns the transport lifetime). app -> platform (live_binding / midi_loopback / osc_loopback)
// + app -> runtime (compound_graph effectiveInput / overrides). NO ui, NO direct graph mutation
// outside the override map. One responsibility: hold the live-control bindings + advance/apply them.
//
// ── TiXL ground-truth (Operators/Lib/io/midi/MidiInput.cs, read-only) ───────────────────────────────
//   Learn flow (MidiInput.cs:113-150): WasTriggered(TeachTrigger) → _teachingActive=true; the NEXT
//   matching signal captures signal.Channel→_trainedChannel, signal.ControllerId→_trainedControllerId,
//   signal.EventType→_trainedEventType, then _teachingActive=false (one-shot). While _teachingActive
//   the match predicate accepts ANY signal (MidiInput.cs:414: `_teachingActive || (matches…)`), so the
//   first event seen becomes the binding. Channel/controller stored as the live value (-1 = "any" only
//   for an unconfigured trained value; learn writes the concrete observed channel/controller).
//   Value path (MidiInput.cs:208): MathUtils.Remap(controllerValue, 0,127, OutputRange) — owned by the
//   platform LiveBindingTable (live_binding.h), which this module feeds + reads.
#pragma once
#include <string>

namespace sw {
struct SymbolLibrary;
}

namespace sw::midibind {

// ── App hook (1): start the live sources ───────────────────────────────────────────────────────────
// Open the OSC (localhost UDP) + virtual-CoreMIDI loopback transports and route every decoded event
// into the app binding table. After this, a real controller / phone-OSC app (or the loopback) feeds
// the table; the bound graph params then move during cook (via tick). Idempotent: a second call is a
// no-op if already started. Returns true if at least one transport came up (MIDI may be unavailable in
// a restricted env — OSC alone still counts). The real-device half (柏為 plugs in a knob box) reuses
// this exact ingest path verbatim; only the event origin changes from the virtual source to hardware.
bool registerIoLiveSources();
// Stop both transports (process teardown / tests). Safe if never started.
void shutdownIoLiveSources();
bool ioLiveSourcesRunning();

// ── Cook-side wire (2): push bound values onto the graph each frame ─────────────────────────────────
// ONE frame of the live-control pipe (called by frame_cook once per frame, next to varlive::tick).
// For every binding routed to a graph parameter (childId, slotId), read its current remapped/damped
// value (LiveBindingTable::valueForTarget) and write it into that child's override; bump the lib
// revision iff any override actually changed (so the projection rebuilds and the resident cook picks
// the new value up — identical to P1's variation_live writeback). No-op when no routed bindings exist.
void tick(SymbolLibrary& lib);

// ── MIDI learn (3) ─────────────────────────────────────────────────────────────────────────────────
// Enter learn mode for a graph parameter (TiXL TeachTrigger). The NEXT live MIDI/OSC event captures
// its filter and creates a binding routed to (childId, slotId). One learn target at a time; a second
// beginLearn replaces the pending target. compositionId = the composition that owns the child.
void beginLearn(const std::string& compositionId, int childId, const std::string& slotId);
// Cancel a pending learn (param re-clicked / Esc) without binding.
void cancelLearn();
// Is a learn pending (waiting for the next event)?
bool learnActive();
// The pending learn target (for the UI label / scenario). Empty slotId / childId<0 when not active.
struct LearnTarget { std::string compositionId; int childId = -1; std::string slotId; };
LearnTarget pendingLearn();

// How many bindings are routed to a graph parameter (the binding-map side-effect count).
int boundParamCount();
// Is (childId, slotId) currently bound to a live source? (for the inspector affordance state.)
bool isParamBound(int childId, const std::string& slotId);
// Remove the binding routed to (childId, slotId), if any. Returns true if one was removed.
bool unbindParam(int childId, const std::string& slotId);

// Reset all bindings + learn state (document swap: New/Open — child ids change, routes dangle).
void reset();

// ── Test / scenario injection (no real device) ─────────────────────────────────────────────────────
// Feed a decoded MIDI event straight into the binding table (bypassing the transport). Used by the
// loopback golden + the .scn scenario to drive learn + the cook-side wire deterministically. kind:
// 0=Note 1=ControllerChange 2=Other (matches platform MidiEventKind / MidiMatchKind ordering).
void injectMidiForTest(int kind, int channel, int controllerId, int controllerValue);
// Feed a decoded OSC arg straight into the binding table.
void injectOscForTest(const std::string& address, float value);

// ── Eye state surface (one-line verify hook on the business side) ────────────────────────────────────
// JSON object for state.json: { "learnActive": bool, "learnTarget": {child,slot}, "boundCount": n,
// "bindings": [ {child, slot, kind, channel, control, value} … ] }. The binding-map side-effect the
// .scn scenario asserts on. Pure read — never mutates.
std::string learnStateJson(const SymbolLibrary& lib);

// ── Self-test (loopback golden, harness-gap-immune) — --selftest-midi-bind ───────────────────────────
// Drives the REAL virtual-CoreMIDI / localhost-OSC loopback through registerIoLiveSources, runs the
// learn flow, and asserts the bound value reaches a GRAPH PARAMETER (effectiveInput) after tick — the
// closed-form CC 64/127 → 0.503937 now landing on the child's override. injectBug breaks the cook-side
// writeback (or the learn capture) → the graph param stays at its default → the tooth bites. 0 = PASS.
int runMidiBindSelfTest(bool injectBug);

}  // namespace sw::midibind
