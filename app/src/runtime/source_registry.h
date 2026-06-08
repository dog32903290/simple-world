// Source registry + per-parameter resolution state — the L5 "every value source is
// the same kind of citizen" spine. A parameter resolves each frame in priority order:
//   override  (a live source touched it this frame; sticky until re-enable) →
//   binding   (connection | automation | live-source — mutually exclusive) →
//   constant  (stored value / spec default).
//
// audio / hand / MIDI / protocol all become "register one LiveSource" — adding a
// source is one line of data, not a new code path. Mixing is NOT in this layer:
// combining values = an Add node = binding=Connection (the value-spine wire), which
// is already modeled by the graph edge.
//
// runtime leaf: pure state + lookup. No UI, no graph dependency — graph.cpp's
// evalParam reads this; this never reads the graph.
#pragma once
#include <map>
#include <string>
#include <utility>

#include "runtime/eval_context.h"  // EvaluationContext (LiveSource::value signature)

namespace sw {

// Which kind of citizen drives a bound parameter. Constant is the implicit default
// (no binding). Connection is the value-spine wire (a graph edge). Automation is a
// scoreGraph curve sampled at the playhead (filled in S4). LiveSource is an external
// feed (audio / hand / MIDI …) registered by id.
enum class BindingKind { Constant, Connection, Automation, LiveSource };

// A live value source: something with an id that can yield a value in the current
// frame context. Plain function pointer + opaque self (no std::function, no heap) so
// it stays a trivially-copyable runtime leaf. `self` is how a source reaches its
// backing state — e.g. S2 passes the AudioInput* so value() reads the latest sample.
struct LiveSource {
  std::string id;
  float (*value)(void* self, const EvaluationContext& ctx) = nullptr;
  void* self = nullptr;
};

// The binding for one parameter. `sourceId` names the LiveSource (kind==LiveSource)
// or the curve (kind==Automation, S4).
struct ParamBinding {
  BindingKind kind = BindingKind::Constant;
  std::string sourceId;
};

// A live override: a source physically touched this parameter. Sticky — it holds
// until a global re-enable clears it (Ableton's automation-override behavior, L13).
struct ParamOverride {
  bool  active = false;
  float value  = 0.0f;
};

// Per-(node,param) binding/override state + the live-source table, keyed by
// (nodeId, portId). Single source of truth for "what drives this parameter".
class SourceRegistry {
 public:
  // Register a live source (= the platform's "add one source = one line").
  void registerSource(const LiveSource& src);
  // Bind a parameter to a source / connection / automation. Replaces any prior
  // binding — one parameter has exactly one binding.
  void bind(int nodeId, const std::string& portId, const ParamBinding& b);
  // A live source touched this parameter (sticky override; wins over the binding).
  void setOverride(int nodeId, const std::string& portId, float v);
  // Global re-enable: clear every override; parameters fall back to their bindings.
  void reEnableAll();

  const ParamBinding*  binding(int nodeId, const std::string& portId) const;
  const ParamOverride* override_(int nodeId, const std::string& portId) const;
  const LiveSource*    source(const std::string& id) const;

 private:
  using Key = std::pair<int, std::string>;  // (nodeId, portId)
  std::map<Key, ParamBinding>       bindings_;
  std::map<Key, ParamOverride>      overrides_;
  std::map<std::string, LiveSource> sources_;
};

}  // namespace sw
