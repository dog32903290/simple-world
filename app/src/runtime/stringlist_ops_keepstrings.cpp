// KeepStrings stringlist op (stringlist self-registration seam leaf — the FIRST STATEFUL StringList
// producer: a persistent string accumulator with four insert modes). TiXL authority:
// Operators/Lib/string/list/KeepStrings.cs:24-148 (Update, ported verbatim below).
//
//   KeepStrings.cs Update():
//     maxCount = MaxCount.Clamp(0, 10000);                                  // :26
//     insertTriggered = InsertTrigger.GetValue(ctx);                        // :27 (LEVEL, not edge!)
//     index = Index.Mod(maxCount);                                          // :28 (positive modulo)
//     if (WasTriggered(ClearTrigger, ref _clear)) {                         // :30 (rising edge)
//       _strings.Clear(); _insertTimes.Clear();
//       ClearTrigger.SetTypedInputValue(false); }                           // :34 (input self-write)
//     onlyOnChanges = OnlyOnChanges.GetValue(ctx);                          // :37
//     hasStringChanged = newStr != _lastString; if (changed) _lastString = newStr;  // :39-43
//     if (insertTriggered) switch (insertMode) {                            // :47
//       Append(:51):   if (changed || !onlyOnChanges) { Add(newStr); Add(LocalFxTime);
//                        while (Count > maxCount && Count > 1) RemoveAt(0);        // trim FRONT
//                        _index = Count - 1; }
//       Insert(:67):   Insert(0, ...); while over: RemoveAt(last); _index = 0;    // trim BACK
//       Overwrite(:82): Count==0 → Add (no _index write); Count<max → Add, _index=Count-1;
//                        else _index=(_index+1)%Count, [_index]=newStr;            // ring cursor
//                        then while over: RemoveAt(last)
//       UseIndex(:114): first trim while over: RemoveAt(last);
//                        Count<=index → Add ; else [index]=newStr }
//     Strings.Value = _strings; InsertTimes.Value = _insertTimes; Count.Value = _strings.Count; // :145-147
//
//   Inputs (KeepStrings.t3 defaults, re-read & confirmed): NewString="", ★InsertTrigger=true (LEVEL —
//   with OnlyOnChanges=true the default behavior is "record every change"), MaxCount=100,
//   ClearTrigger=false, ★OnlyOnChanges=true, InsertMode=0 (Append), Index=0.
//
// EVAL-SIDE LAYOUT: a STATEFUL StringList PRODUCER. NewString is its ONE String input
// (inputStrings[0], wire-OR-const); the six knobs ride resolved Float params; the accumulator lives in
// StringListCookCtx::state (StringListState — the NEW cross-frame slot this leaf introduces, threaded
// by both drivers with the cook-once-per-frame guard; see stringlist_op_registry.h).
//
// FORKS (named):
//   ★fork-keepstrings-outputs-deferred: TiXL has THREE outputs (Strings + InsertTimes + Count). sw's
//     StringList rail carries ONE output channel (the StringList); the InsertTimes (FloatList) and
//     Count (int) ports are DEFERRED (the JoinLists Length-deferred precedent) — their STATE is
//     maintained verbatim (insertTimes bookkeeping below is live and trim-aligned), so landing the
//     ports later is a channel wire, not a state redesign. The ports are NOT declared until they
//     carry a real channel (no dead pins).
//   - fork-keepstrings-cleartrigger-no-input-writeback: TiXL resets the ClearTrigger INPUT value
//     (:34, SetTypedInputValue(false)) after the rising edge; sw's cook cannot write input constants.
//     The WasTriggered latch (_clear ↔ state.clearLatch) already makes the clear fire exactly once
//     per rising edge — observable behavior identical for wired/param-driven triggers; only the
//     inspector's checkbox visually snapping back is dropped (editor affordance).
//   - fork-localfxtime-bars-vs-secs: insertTimes stamps ctx->time (sw LocalFxTime = BARS; TiXL's is
//     seconds) — the family fork named in string_op_registry.h (StringState.lastUpdateTime).
//   - fork-int-dissolve-to-float: MaxCount/InsertMode/Index are TiXL ints on the Float rail.
#include <cmath>    // std::lround
#include <string>
#include <vector>

#include "runtime/eval_context.h"            // EvaluationContext (full def — the LocalFxTime stamp)
#include "runtime/graph.h"                   // NodeSpec, PortSpec, Widget
#include "runtime/stringlist_op_registry.h"  // StringListOp / StringListCookCtx / StringListState

namespace sw {
namespace {

// MathUtils.Mod (MathUtils.cs:273) — POSITIVE modulo; repeat==0 → 0. (The AnimInt posMod twin.)
int posMod(int val, int repeat) {
  if (repeat == 0) return 0;
  int x = val % repeat;
  if (x < 0) x = repeat + x;
  return x;
}

// MathUtils.WasTriggered(triggered, ref latch): rising edge — true once when `triggered` goes
// false→true; the latch remembers. (The KeepColors/KeepStrings clear-trigger shape.)
bool wasTriggered(bool triggered, bool& latch) {
  const bool fired = triggered && !latch;
  latch = triggered;
  return fired;
}

// cookKeepStrings: verbatim port of KeepStrings.cs:24-148 (see header trace).
void cookKeepStrings(StringListCookCtx& c) {
  if (!c.output) return;
  c.output->clear();
  if (!c.state) return;  // no driver state (a mis-wired hand ctx) → nothing to accumulate against
  StringListState& st = *c.state;

  int maxCount = (int)std::lround(stringListParam(c.params, "MaxCount", 100.0f));
  maxCount = maxCount < 0 ? 0 : (maxCount > 10000 ? 10000 : maxCount);  // :26 Clamp(0,10000)
  const bool insertTriggered = stringListParam(c.params, "InsertTrigger", 1.0f) > 0.5f;  // :27 LEVEL
  const int index = posMod((int)std::lround(stringListParam(c.params, "Index", 0.0f)), maxCount);  // :28

  if (wasTriggered(stringListParam(c.params, "ClearTrigger", 0.0f) > 0.5f, st.clearLatch)) {  // :30
    st.strings.clear();
    st.insertTimes.clear();
    // :34 input write-back dropped — fork-keepstrings-cleartrigger-no-input-writeback (header).
  }

  const bool onlyOnChanges = stringListParam(c.params, "OnlyOnChanges", 1.0f) > 0.5f;  // :37
  const std::string newStr = (c.inputStrings && !c.inputStrings->empty()) ? (*c.inputStrings)[0]
                                                                          : std::string{};
  const bool hasStringChanged = newStr != st.lastString;  // :39
  if (hasStringChanged) st.lastString = newStr;           // :40-43

  const int insertMode = (int)std::lround(stringListParam(c.params, "InsertMode", 0.0f));  // :45
  const float fxTime = c.ctx ? c.ctx->time : 0.0f;  // context.LocalFxTime (bars — family fork)

  if (insertTriggered) {  // :47
    switch (insertMode) {
      case 0:  // Append (:51-66)
        if (hasStringChanged || !onlyOnChanges) {
          st.strings.push_back(newStr);
          st.insertTimes.push_back(fxTime);
          while ((int)st.strings.size() > maxCount && st.strings.size() > 1) {  // trim FRONT
            st.strings.erase(st.strings.begin());
            st.insertTimes.erase(st.insertTimes.begin());
          }
          st.index = (int)st.strings.size() - 1;
        }
        break;
      case 1:  // Insert (:67-81)
        if (hasStringChanged || !onlyOnChanges) {
          st.strings.insert(st.strings.begin(), newStr);
          st.insertTimes.insert(st.insertTimes.begin(), fxTime);
          while ((int)st.strings.size() > maxCount && st.strings.size() > 1) {  // trim BACK
            st.strings.pop_back();
            st.insertTimes.pop_back();
          }
          st.index = 0;
        }
        break;
      case 2:  // Overwrite (:82-112) — the ring cursor
        if (hasStringChanged || !onlyOnChanges) {
          if (maxCount > 0) {
            if (st.strings.empty()) {  // :87-91 Add WITHOUT an _index write (verbatim quirk)
              st.strings.push_back(newStr);
              st.insertTimes.push_back(fxTime);
            } else if ((int)st.strings.size() < maxCount) {  // :92-97
              st.strings.push_back(newStr);
              st.insertTimes.push_back(fxTime);
              st.index = (int)st.strings.size() - 1;
            } else {  // :98-103 the ring overwrite
              st.index = (st.index + 1) % (int)st.strings.size();
              st.strings[(size_t)st.index] = newStr;
              st.insertTimes[(size_t)st.index] = fxTime;
            }
          }
          while ((int)st.strings.size() > maxCount && st.strings.size() > 1) {  // :106-110 trim BACK
            st.strings.pop_back();
            st.insertTimes.pop_back();
          }
        }
        break;
      case 3:  // UseIndex (:114-141) — trim FIRST, then write-or-append at `index`
        while ((int)st.strings.size() > maxCount && st.strings.size() > 1) {  // :117-121
          st.strings.pop_back();
          st.insertTimes.pop_back();
        }
        if (hasStringChanged || !onlyOnChanges) {
          if ((int)st.strings.size() <= index) {  // :125-129
            st.strings.push_back(newStr);
            st.insertTimes.push_back(fxTime);
          } else {  // :130-134
            st.strings[(size_t)index] = newStr;
            st.insertTimes[(size_t)index] = fxTime;
          }
        }
        break;
      default:
        break;
    }
  }

  *c.output = st.strings;  // Strings.Value = _strings (:145). InsertTimes/Count outputs deferred (fork).

  // Test-only: corrupt the REAL published output (drop the last element) so the golden's RED case
  // fires on the actual cook path (NOT by flipping the expected value). State stays intact — the
  // corruption is on the publish, mirroring the string-family drop-last tooth.
  if (stringListInjectBug() && !c.output->empty()) c.output->pop_back();
}

}  // namespace

// Self-registration. File-scope static StringListOp, stateful=true (threads StringListState + the
// cook-once-per-frame driver guard — the AmplifyValues pattern).
//   Ports: "Strings" = the StringList output (the accumulator); "NewString" = the ONE String input;
//   six Float knobs (.t3 defaults above). InsertTimes/Count output ports DEFERRED (named fork).
static const StringListOp _reg_keepstrings{
    {"KeepStrings", "KeepStrings",
     {{"Strings", "Strings", "StringList", false},
      {"NewString", "NewString", "String", true, 0.0f, 0.0f, 1.0f, Widget::Slider, {}, false, 1,
       false, ""},
      {"InsertTrigger", "InsertTrigger", "Float", true, 1.0f, 0.0f, 1.0f, Widget::Bool, {}, true},
      {"MaxCount", "MaxCount", "Float", true, 100.0f, 0.0f, 10000.0f},
      {"ClearTrigger", "ClearTrigger", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Bool, {}, true},
      {"OnlyOnChanges", "OnlyOnChanges", "Float", true, 1.0f, 0.0f, 1.0f, Widget::Bool, {}, true},
      {"InsertMode", "InsertMode", "Float", true, 0.0f, 0.0f, 3.0f, Widget::Enum,
       {"Append", "Insert", "Overwrite", "UseIndex"}},
      {"Index", "Index", "Float", true, 0.0f, 0.0f, 10000.0f}},
     /*evaluate=*/nullptr,
     "string.list"},
    cookKeepStrings, /*stateful=*/true};

}  // namespace sw
