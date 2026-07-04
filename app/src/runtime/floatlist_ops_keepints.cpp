// KeepInts floatlist op (floatlist self-registration seam leaf — a cross-frame STATE consumer on the
// FLOATLIST rail, the INT twin of KeepFloatValues). TiXL authority:
// external/tixl/Operators/Lib/numbers/int/process/KeepInts.cs (verbatim Update() below; cited inline).
//
//   KeepInts.cs Update():
//     var addValueToList = AddValueToList.GetValue(context);                 // :18
//     var length         = BufferLength.GetValue(context).Clamp(1, 100000);  // :19
//     var newValue       = Value.GetValue(context);                          // :20
//     var reset          = Reset.GetValue(context);                          // :22
//     if (reset) _list.Clear();                                              // :24-25
//     if (_list.Count != length) while (_list.Count < length) _list.Add(0);  // :29-35  GROW only (pad with 0)
//     if (addValueToList) _list.Insert(0, newValue);                         // :37-38  FRONT insert
//     if (_list.Count > length) _list.RemoveRange(length, _list.Count-length);// :40-43  TRIM tail
//     Result.Value = _list;                                                  // :45
//   Field: _list — a PERSISTENT per-node accumulator (cs:54). This op is a ring/shift buffer: each frame a
//   new int pushes to the front and the oldest falls off the tail once length is reached.
//
// DIFF vs KeepFloatValues (the ONLY parity-relevant differences — otherwise character-identical):
//   - KeepInts pads GROW with the LITERAL 0 (cs:33 `_list.Add(0)`), NOT a DefaultValue param. KeepInts has
//     NO DefaultValue input (its ctor list is Value/AddValueToList/BufferLength/Reset only, cs:56-66). So
//     this leaf has no DefaultValue param; the pad is always 0.
//   - The int-ness: TiXL Value/Result are int (List<int>). On this rail every list element is a float; a
//     whole int round-trips exactly through float over any realistic count/index range (|v| < 2^24). See
//     fork-keepints-int-via-floatrail below. The GROW pad 0 and the front-insert value are stored as float.
//   - The try/catch (cs:27-50) wraps only the List ops; it never changes the RESULT for valid inputs
//     (Clamp already bounds length to [1,100000], so RemoveRange/Insert cannot throw). It is a defensive
//     log, not a value branch — dropped (no exception path is reachable for the clamped inputs).
//
// THE SEAM THIS LEAF PROVES: _list SURVIVES across frames (identical to KeepFloatValues). With
// AddValueToList=true (the .t3 default) the list accumulates: frame N pushes newValue to index 0, shifting
// every prior value back one — reading index k after N frames returns the value pushed N-1-k frames ago.
// That history needs the cross-frame FloatListState slot (FloatListState::keepList). The driver owns +
// threads it (flat: Impl::floatListState[flatKey(id)]; resident: residentFloatListState()) — identical
// wiring to KeepFloatValues.
//
// ★.t3 DEFAULTS (KeepInts.t3 — OVERRIDE the C# ctor defaults):
//   BufferLength = 100 ; Value = 0 ; AddValueToList = TRUE ; Reset = false. (No DefaultValue — pad is 0.)
//
// FORK (named, load-bearing): fork-keepints-grow-only. cs ONLY grows the list toward `length` (the while-
//   loop pads with 0; the count!=length branch never shrinks — only the trailing RemoveRange after a front-
//   insert trims). So if BufferLength DROPS frame→frame the list is trimmed lazily by the post-insert
//   RemoveRange, not by the pad branch. VERBATIM transcription — the asymmetric grow/trim is the contract.
//
// FORK (named): fork-keepints-int-via-floatrail. Value (int), AddValueToList/Reset (bool), BufferLength
//   (int) dissolve to pinless Float params on this rail (no Int/Bool wire). Value stores as float
//   (whole-int exact); BufferLength rounds toward zero (std::floor over the non-negative slider range);
//   the bools read `!= 0`. Value/BufferLength are scalar Float PARAMS (NOT FloatList inputs) — KeepInts has
//   NO FloatList INPUT wire; it is a host-scalar→FloatList ACCUMULATOR (only the output produces currency).
#include <cmath>  // std::floor (BufferLength int dissolve)

#include "runtime/floatlist_op_registry.h"  // FloatListOp / FloatListCookCtx / FloatListState / injectBug / param
#include "runtime/graph.h"                    // NodeSpec, PortSpec, Widget

namespace sw {

namespace {

// KeepInts: persistent front-insert ring buffer, per KeepInts.cs Update() (verbatim). Pads GROW with 0.
void cookKeepInts(FloatListCookCtx& c) {
  if (!c.output) return;

  // No state slot (a hand-built ctx) → behave as a single fresh frame on a local empty accumulator.
  FloatListState local;
  FloatListState* st = c.state ? c.state : &local;
  std::vector<float>& list = st->keepList;  // the persistent _list

  // cs:18-22 — read params. BufferLength.Clamp(1,100000); bools read != 0.
  const bool addValueToList = floatListParam(c.params, "AddValueToList", 1.0f) != 0.0f;  // .t3 default TRUE
  const float lengthF = floatListParam(c.params, "BufferLength", 100.0f);                // .t3 default 100
  int length = (int)std::floor(lengthF);  // int dissolve (toward zero for the non-negative slider range)
  if (length < 1) length = 1;             // cs:19 Clamp(1, 100000)
  if (length > 100000) length = 100000;
  const float newValue = floatListParam(c.params, "Value", 0.0f);      // .t3 default 0
  const bool reset = floatListParam(c.params, "Reset", 0.0f) != 0.0f;  // .t3 default false

  if (reset) list.clear();  // cs:24-25

  // cs:29-35 — GROW only: pad with LITERAL 0 up to `length` (KeepInts uses _list.Add(0), no DefaultValue).
  if ((int)list.size() != length) {
    while ((int)list.size() < length) list.push_back(0.0f);
  }

  // injectBug (golden teeth): DISABLE the persistence — front-insert into a FRESH empty scratch (padded 0)
  // instead of the persisted _list, so the accumulated history is lost and the output is just [newValue,
  // 0...] every frame (index>0 reads the pad 0, not the cross-frame history). Bites the REAL cook: with the
  // bug the history vanishes, without it index k holds the value pushed k frames ago.
  if (floatListInjectBug()) {
    std::vector<float> fresh;
    while ((int)fresh.size() < length) fresh.push_back(0.0f);
    if (addValueToList) fresh.insert(fresh.begin(), newValue);
    if ((int)fresh.size() > length) fresh.resize(length);
    *c.output = fresh;
    return;
  }

  if (addValueToList) list.insert(list.begin(), newValue);  // cs:37-38 — FRONT insert (push history back)

  // cs:40-43 — TRIM tail back to `length` (RemoveRange(length, count-length)).
  if ((int)list.size() > length) list.resize(length);

  *c.output = list;  // cs:45 — Result.Value = _list (publish the persistent accumulator)
}

}  // namespace

// Self-registration. stateful=true. Ports: "out" (FloatList output); Value (scalar Float, int-dissolved) +
// AddValueToList/Reset (Bool) + BufferLength (int-dissolved Float) params. NO FloatList INPUT wire — this is
// a host-scalar→FloatList accumulator. NO DefaultValue param (KeepInts pads with literal 0).
//   .t3 defaults baked into PortSpec.def: Value 0, AddValueToList 1 (true), BufferLength 100, Reset 0.
static const FloatListOp _reg_keepints{
    {"KeepInts", "KeepInts",
     {{"out", "out", "FloatList", false},
      {"Value", "Value", "Float", true, 0.0f, -100000.0f, 100000.0f, Widget::Slider, {}, /*pinless=*/true},
      {"AddValueToList", "AddValueToList", "Float", true, 1.0f, 0.0f, 1.0f, Widget::Bool, {},
       /*pinless=*/true},
      {"BufferLength", "BufferLength", "Float", true, 100.0f, 1.0f, 100000.0f, Widget::Slider, {},
       /*pinless=*/true},
      {"Reset", "Reset", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Bool, {}, /*pinless=*/true}},
     /*evaluate=*/nullptr},
    cookKeepInts, /*stateful=*/true};

}  // namespace sw
