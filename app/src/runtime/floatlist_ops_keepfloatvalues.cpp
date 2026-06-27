// KeepFloatValues floatlist op (floatlist self-registration seam leaf — a cross-frame STATE consumer on the
// FLOATLIST rail, sibling of AmplifyValues). TiXL authority:
// external/tixl/Operators/Lib/numbers/floats/process/KeepFloatValues.cs (verbatim Update() below; cited inline).
//
//   KeepFloatValues.cs Update():
//     var addValueToList = AddValueToList.GetValue(context);
//     var length         = BufferLength.GetValue(context).Clamp(1, 100000);
//     var newValue       = Value.GetValue(context);
//     var defaultValue   = DefaultValue.GetValue(context);
//     var reset          = Reset.GetValue(context);
//     if (reset) _list.Clear();
//     if (_list.Count != length) while (_list.Count < length) _list.Add(defaultValue);   // GROW only (pad)
//     if (addValueToList) _list.Insert(0, newValue);                                       // FRONT insert
//     if (_list.Count > length) _list.RemoveRange(length, _list.Count - length);          // TRIM tail
//     Result.Value = _list;
//   Field: _list — a PERSISTENT per-node accumulator (cs:55). This op is a ring/shift buffer: each frame a
//   new value pushes to the front and the oldest falls off the tail once length is reached.
//
// THE SEAM THIS LEAF PROVES: _list SURVIVES across frames. With AddValueToList=true (the .t3 default), the
// list accumulates: frame N pushes newValue to index 0, shifting every prior value back one — so reading
// index k after N frames returns the value pushed N-1-k frames ago. That history is the whole point; it
// needs the cross-frame FloatListState slot (FloatListState::keepList). The driver owns + threads it (flat:
// Impl::floatListState[flatKey(id)]; resident: residentFloatListState()) — identical wiring to AmplifyValues.
//
// ★.t3 DEFAULTS (KeepFloatValues.t3 — OVERRIDE the C# ctor defaults):
//   BufferLength = 100 ; DefaultValue = 0.0 ; Value = 0.0 ; AddValueToList = TRUE ; Reset = false.
//   (AddValueToList defaulting to TRUE means the op accumulates by default — a node fed a changing Value
//   builds a history without the user enabling anything. The pad-with-defaultValue GROW seeds the buffer to
//   `length` zeros on the first cook, then each frame front-inserts and trims back to `length`.)
//
// FORK (named, load-bearing): fork-keepfloatvalues-grow-only. cs ONLY grows the list toward `length` (the
//   while-loop pads; it never shrinks via the count!=length branch — only the trailing RemoveRange after a
//   front-insert trims). So if BufferLength DROPS frame→frame, the list is trimmed lazily by the post-insert
//   RemoveRange, not by the pad branch. This is a VERBATIM transcription — the asymmetric grow/trim is the
//   parity contract. (The Clamp(1,100000) on length is preserved.)
//
// FORK (named): fork-int-bool-dissolve. BufferLength (int), AddValueToList/Reset (bool) dissolve to pinless
//   Float params on this rail (no Int/Bool wire). BufferLength rounds toward zero; the bools read `!= 0`.
//   Value/DefaultValue are scalar Float params (NOT FloatList inputs) — KeepFloatValues has NO FloatList
//   INPUT wire; it is a host-scalar→FloatList ACCUMULATOR (its only currency-producing port is the output).
#include <cmath>  // std::floor (BufferLength int dissolve)

#include "runtime/floatlist_op_registry.h"  // FloatListOp / FloatListCookCtx / FloatListState / injectBug / param
#include "runtime/graph.h"                    // NodeSpec, PortSpec, Widget

namespace sw {

namespace {

// KeepFloatValues: persistent front-insert ring buffer, per KeepFloatValues.cs Update() (verbatim).
void cookKeepFloatValues(FloatListCookCtx& c) {
  if (!c.output) return;

  // No state slot (a hand-built ctx) → behave as a single fresh frame on a local empty accumulator.
  FloatListState local;
  FloatListState* st = c.state ? c.state : &local;
  std::vector<float>& list = st->keepList;  // the persistent _list

  // cs:18-23 — read params. BufferLength.Clamp(1,100000); bools read != 0.
  const bool addValueToList = floatListParam(c.params, "AddValueToList", 1.0f) != 0.0f;  // .t3 default TRUE
  const float lengthF = floatListParam(c.params, "BufferLength", 100.0f);                // .t3 default 100
  int length = (int)std::floor(lengthF);  // int dissolve (toward zero for the non-negative slider range)
  if (length < 1) length = 1;             // cs:19 Clamp(1, 100000)
  if (length > 100000) length = 100000;
  const float newValue = floatListParam(c.params, "Value", 0.0f);          // .t3 default 0.0
  const float defaultValue = floatListParam(c.params, "DefaultValue", 0.0f);  // .t3 default 0.0
  const bool reset = floatListParam(c.params, "Reset", 0.0f) != 0.0f;      // .t3 default false

  if (reset) list.clear();  // cs:25-26

  // cs:30-36 — GROW only: pad with defaultValue up to `length` (the count!=length branch never shrinks here).
  if ((int)list.size() != length) {
    while ((int)list.size() < length) list.push_back(defaultValue);
  }

  // injectBug (golden teeth): DISABLE the persistence — front-insert into a FRESH empty scratch instead of the
  // persisted _list, so the accumulated history is lost and the output is just [newValue, pad...] every frame
  // (index>0 reads stale-from-this-frame, not the cross-frame history). Bites the REAL cook: with the bug the
  // history vanishes, without it index k holds the value pushed k frames ago.
  if (floatListInjectBug()) {
    std::vector<float> fresh;
    while ((int)fresh.size() < length) fresh.push_back(defaultValue);
    if (addValueToList) fresh.insert(fresh.begin(), newValue);
    if ((int)fresh.size() > length) fresh.resize(length);
    *c.output = fresh;
    return;
  }

  if (addValueToList) list.insert(list.begin(), newValue);  // cs:38-39 — FRONT insert (push history back)

  // cs:41-44 — TRIM tail back to `length` (RemoveRange(length, count-length)).
  if ((int)list.size() > length) list.resize(length);

  *c.output = list;  // cs:46 — Result.Value = _list (publish the persistent accumulator)
}

}  // namespace

// Self-registration. stateful=true. Ports: "out" (FloatList output); Value/DefaultValue (scalar Float) +
// AddValueToList/Reset (Bool) + BufferLength (int-dissolved Float) params. NO FloatList INPUT wire — this is a
// host-scalar→FloatList accumulator (its only currency-producing port is the output).
//   .t3 defaults baked into PortSpec.def: Value 0, AddValueToList 1 (true), BufferLength 100, Reset 0, DefaultValue 0.
static const FloatListOp _reg_keepfloatvalues{
    {"KeepFloatValues", "KeepFloatValues",
     {{"out", "out", "FloatList", false},
      {"Value", "Value", "Float", true, 0.0f, -1000.0f, 1000.0f, Widget::Slider, {}, /*pinless=*/true},
      {"AddValueToList", "AddValueToList", "Float", true, 1.0f, 0.0f, 1.0f, Widget::Bool, {},
       /*pinless=*/true},
      {"BufferLength", "BufferLength", "Float", true, 100.0f, 1.0f, 100000.0f, Widget::Slider, {},
       /*pinless=*/true},
      {"Reset", "Reset", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Bool, {}, /*pinless=*/true},
      {"DefaultValue", "DefaultValue", "Float", true, 0.0f, -1000.0f, 1000.0f, Widget::Slider, {},
       /*pinless=*/true}},
     /*evaluate=*/nullptr},
    cookKeepFloatValues, /*stateful=*/true};

}  // namespace sw
