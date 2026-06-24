// app/midi_bind.cpp — P3 live-control pipe: registerIoLiveSources (app hook) + cook-side wire + MIDI
// learn. See midi_bind.h for the contract and the TiXL ground-truth refs. The --selftest-midi-bind
// loopback golden lives at the tail (one file, one responsibility; the seam's own harness).
#include "app/midi_bind.h"

#include <cmath>
#include <cstdio>
#include <mutex>
#include <string>
#include <vector>

#include "app/document.h"            // bumpLibRevision (live writeback must dirty the projection)
#include "platform/live_binding.h"   // LiveBindingTable / LiveBinding / MidiMatchKind / remapLinear
#include "platform/midi_loopback.h"  // MidiLoopbackDevice / MidiEventKind
#include "platform/osc_loopback.h"   // OscLoopbackDevice
#include "runtime/compound_graph.h"  // SymbolLibrary / Symbol / SymbolChild / childById / effectiveInput

namespace sw::midibind {
namespace {

// One bound parameter: the live filter (mirrored so the table can be rebuilt on unbind — the platform
// table has no per-row erase and rows aren't readable) + the graph route (which child/slot it writes).
struct Route {
  LiveBinding  binding;        // the filter + remap (the row we (re)add to the table)
  std::string  compositionId;  // composition that owns the child
  int          childId = -1;
  std::string  slotId;
};

// The single app-level binding table + its routes. Fed by the loopback callbacks (read thread) and the
// test injectors; read by tick() on the cook thread. A mutex guards both (the loopback callbacks fire
// on a CoreMIDI/receive thread, the header notes a real app owner would add exactly this lock).
std::mutex         g_mutex;
LiveBindingTable   g_table;
std::vector<Route> g_routes;

// Learn state (TiXL _teachingActive). When active, the next live event captures its filter into a new
// binding routed to g_pendingLearn. One-shot: cleared on capture (or cancelLearn).
bool        g_learnActive = false;
LearnTarget g_pendingLearn;

// The live transports (owned for the process lifetime once started). MIDI may fail in a restricted env.
MidiLoopbackDevice g_midi;
OscLoopbackDevice  g_osc;
bool g_running = false;

// A stable target name for a (childId, slotId) route — the LiveBinding.target key. Deterministic so a
// re-learn of the same param reuses the same target slot (one binding per param).
std::string targetNameFor(int childId, const std::string& slotId) {
  return "p3:" + std::to_string(childId) + ":" + slotId;
}

MidiMatchKind midiKindFromInt(int kind) {
  switch (kind) {
    case 0:  return MidiMatchKind::Note;
    case 1:  return MidiMatchKind::ControllerChange;
    case 2:  return MidiMatchKind::Other;
    default: return MidiMatchKind::Any;
  }
}
MidiMatchKind midiKindFromEvent(MidiEventKind k) {
  switch (k) {
    case MidiEventKind::Note:             return MidiMatchKind::Note;
    case MidiEventKind::ControllerChange: return MidiMatchKind::ControllerChange;
    case MidiEventKind::Other:            return MidiMatchKind::Other;
  }
  return MidiMatchKind::Any;
}

// Find the route index bound to (childId, slotId), or -1. Caller holds g_mutex.
int routeIndexFor(int childId, const std::string& slotId) {
  for (int i = 0; i < int(g_routes.size()); ++i)
    if (g_routes[i].childId == childId && g_routes[i].slotId == slotId) return i;
  return -1;
}

// Rebuild g_table from g_routes' mirrored bindings (the platform table has no per-row erase). Preserves
// damped/current values across surviving rows? No — a rebuild reseeds each row to its default, which is
// the faithful behaviour after an unbind (a removed binding's value is gone; survivors re-arm from
// default and the next event repopulates them). Caller holds g_mutex.
void rebuildTable_locked() {
  g_table.clear();
  for (const Route& r : g_routes) g_table.addBinding(r.binding);
}

// LEARN CAPTURE (TiXL MidiInput.cs:131-149): the first event during _teachingActive captures its
// concrete channel/controller/kind (or OSC address) into a binding routed to the pending target. One
// binding per param: a re-learn of an already-bound param replaces the old row. Caller holds g_mutex.
void captureLearn_locked(bool isMidi, MidiMatchKind kind, int channel, int controllerId,
                         const std::string& oscAddress) {
  const LearnTarget t = g_pendingLearn;
  const std::string target = targetNameFor(t.childId, t.slotId);

  LiveBinding b;
  if (isMidi) {
    b.source      = LiveSourceKind::Midi;
    b.midiKind    = kind;
    b.midiChannel = channel;      // the CONCRETE observed channel (TiXL signal.Channel)
    b.midiControl = controllerId; // the CONCRETE observed controller (TiXL signal.ControllerId)
    b.inMin = 0.f; b.inMax = 127.f; b.outMin = 0.f; b.outMax = 1.f;  // the MidiInput remap range
  } else {
    b.source     = LiveSourceKind::Osc;
    b.oscAddress = oscAddress;
    b.inMin = 0.f; b.inMax = 1.f; b.outMin = 0.f; b.outMax = 1.f;  // OSC identity (raw, TiXL OscInput)
  }
  b.target = target;

  Route route{b, t.compositionId, t.childId, t.slotId};
  if (int existing = routeIndexFor(t.childId, t.slotId); existing >= 0) {
    g_routes[existing] = route;   // re-learn: replace the row…
    rebuildTable_locked();        // …and rebuild the table from the mirror (no per-row erase)
  } else {
    g_routes.push_back(route);
    g_table.addBinding(b);        // append (table + routes stay in add order)
  }

  g_learnActive = false;          // one-shot (TiXL _teachingActive=false after capture)
  g_pendingLearn = LearnTarget{};
}

}  // namespace

// ── App hook (1) ─────────────────────────────────────────────────────────────────────────────────
bool registerIoLiveSources() {
  if (g_running) return true;
  // MIDI loopback → ingestMidi. The virtual CoreMIDI source means a real controller and our loopback
  // share this exact decode+ingest path (only the event origin differs).
  bool midiUp = g_midi.startListening(
      [](void*, MidiEventKind kind, int channel, int controllerId, int controllerValue) {
        std::lock_guard<std::mutex> lk(g_mutex);
        if (g_learnActive)  // learn beats matching: the first event captured becomes the binding
          captureLearn_locked(/*isMidi=*/true, midiKindFromEvent(kind), channel, controllerId, "");
        g_table.ingestMidi(midiKindFromEvent(kind), channel, controllerId, controllerValue);
      },
      nullptr);
  // OSC loopback (ephemeral port) → ingestOsc.
  bool oscUp = g_osc.startListening(
      0 /*ephemeral*/,
      [](void*, const std::string& address, float value, int /*argIndex*/) {
        std::lock_guard<std::mutex> lk(g_mutex);
        if (g_learnActive)
          captureLearn_locked(/*isMidi=*/false, MidiMatchKind::Any, -1, -1, address);
        g_table.ingestOsc(address, value);
      },
      nullptr);
  g_running = midiUp || oscUp;
  return g_running;
}

void shutdownIoLiveSources() {
  if (g_midi.isListening()) g_midi.stopListening();
  if (g_osc.isListening()) g_osc.stopListening();
  g_running = false;
}

bool ioLiveSourcesRunning() { return g_running; }

// ── Cook-side wire (2): the value reaches the graph parameter HERE ───────────────────────────────────
void tick(SymbolLibrary& lib) {
  std::lock_guard<std::mutex> lk(g_mutex);
  if (g_routes.empty()) return;
  bool changed = false;
  for (const Route& r : g_routes) {
    if (r.childId < 0) continue;
    float v = 0.f;
    if (!g_table.valueForTarget(r.binding.target, v)) continue;  // no value (shouldn't happen)
    Symbol* comp = lib.find(r.compositionId);
    if (!comp) continue;
    SymbolChild* c = childById(*comp, r.childId);
    if (!c) continue;  // child id dangled (document edited) — skip; reset() clears stale routes
    auto it = c->overrides.find(r.slotId);
    if (it == c->overrides.end() || std::fabs(it->second - v) > 1e-7f) {
      c->overrides[r.slotId] = v;  // write the live value onto the graph parameter (same as P1)
      changed = true;
    }
  }
  // Bump the lib revision iff something actually moved, so the resident projection rebuilds and the
  // cook reads the new override (identical contract to variation_live's live preview writeback).
  if (changed) doc::bumpLibRevision();
}

// ── MIDI learn (3) ─────────────────────────────────────────────────────────────────────────────────
void beginLearn(const std::string& compositionId, int childId, const std::string& slotId) {
  std::lock_guard<std::mutex> lk(g_mutex);
  g_pendingLearn = LearnTarget{compositionId, childId, slotId};
  g_learnActive = true;  // TiXL TeachTrigger fired → _teachingActive=true
}
void cancelLearn() {
  std::lock_guard<std::mutex> lk(g_mutex);
  g_learnActive = false;
  g_pendingLearn = LearnTarget{};
}
bool learnActive() {
  std::lock_guard<std::mutex> lk(g_mutex);
  return g_learnActive;
}
LearnTarget pendingLearn() {
  std::lock_guard<std::mutex> lk(g_mutex);
  return g_pendingLearn;
}
int boundParamCount() {
  std::lock_guard<std::mutex> lk(g_mutex);
  int n = 0;
  for (const Route& r : g_routes)
    if (r.childId >= 0) ++n;
  return n;
}
bool isParamBound(int childId, const std::string& slotId) {
  std::lock_guard<std::mutex> lk(g_mutex);
  return routeIndexFor(childId, slotId) >= 0;
}
bool unbindParam(int childId, const std::string& slotId) {
  std::lock_guard<std::mutex> lk(g_mutex);
  int idx = routeIndexFor(childId, slotId);
  if (idx < 0) return false;
  g_routes.erase(g_routes.begin() + idx);
  rebuildTable_locked();  // re-add survivors (no per-row erase on the platform table)
  return true;
}
void reset() {
  std::lock_guard<std::mutex> lk(g_mutex);
  g_table.clear();
  g_routes.clear();
  g_learnActive = false;
  g_pendingLearn = LearnTarget{};
}

// ── Test injection ───────────────────────────────────────────────────────────────────────────────
void injectMidiForTest(int kind, int channel, int controllerId, int controllerValue) {
  std::lock_guard<std::mutex> lk(g_mutex);
  if (g_learnActive)
    captureLearn_locked(/*isMidi=*/true, midiKindFromInt(kind), channel, controllerId, "");
  g_table.ingestMidi(midiKindFromInt(kind), channel, controllerId, controllerValue);
}
void injectOscForTest(const std::string& address, float value) {
  std::lock_guard<std::mutex> lk(g_mutex);
  if (g_learnActive) captureLearn_locked(/*isMidi=*/false, MidiMatchKind::Any, -1, -1, address);
  g_table.ingestOsc(address, value);
}

// ── Eye state surface ────────────────────────────────────────────────────────────────────────────
std::string learnStateJson(const SymbolLibrary& lib) {
  std::lock_guard<std::mutex> lk(g_mutex);
  std::string s = "{";
  s += "\"learnActive\": " + std::string(g_learnActive ? "true" : "false");
  s += ", \"learnTarget\": {\"child\": " + std::to_string(g_pendingLearn.childId) +
       ", \"slot\": \"" + g_pendingLearn.slotId + "\"}";
  int n = 0;
  for (const Route& r : g_routes)
    if (r.childId >= 0) ++n;
  s += ", \"boundCount\": " + std::to_string(n);
  s += ", \"bindings\": [";
  bool first = true;
  for (const Route& r : g_routes) {
    if (r.childId < 0) continue;
    float v = 0.f;
    g_table.valueForTarget(r.binding.target, v);
    if (!first) s += ", ";
    first = false;
    s += "{\"child\": " + std::to_string(r.childId) + ", \"slot\": \"" + r.slotId + "\"";
    s += ", \"value\": " + std::to_string(v) + "}";
  }
  s += "]}";
  (void)lib;
  return s;
}

}  // namespace sw::midibind
