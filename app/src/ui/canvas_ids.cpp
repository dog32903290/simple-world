// ui/canvas_ids — implementation of the canvas pin/link/boundary id scheme (see canvas_ids.h).
// Moved verbatim out of editor_ui.cpp's anonymous namespace (mechanical split, rule 4).
#include "ui/canvas_ids.h"

#include <cstdio>

#include "app/document.h"
#include "runtime/graph.h"  // findSpec / pinId / pinNode (the int pin scheme rides on child ids)

namespace sw::ui {

int childPinId(int childId, int portIndex) { return kChildPinBase + sw::pinId(childId, portIndex); }

int pinChildId(int pin) {
  return pinIsBoundary(pin) ? sw::kSymbolBoundary : sw::pinNode(pin - kChildPinBase);
}
int pinPortIndex(int pin) {
  return pinIsBoundary(pin) ? (pin - kBoundaryPinBase) : (pin - kChildPinBase - 1) % 100;
}

PinInfo pinInfoOf(int pin) {
  PinInfo r;
  const sw::Symbol* cur = sw::doc::currentSymbolConst();
  if (!cur) return r;
  const int childId = pinChildId(pin);
  const int idx = pinPortIndex(pin);
  if (childId == sw::kSymbolBoundary) {
    const int nIn = (int)cur->inputDefs.size();
    if (idx >= 0 && idx < nIn) {
      r = {true, /*isInput=*/false, cur->inputDefs[idx].id, cur->inputDefs[idx].dataType};
    } else if (idx >= nIn && idx < nIn + (int)cur->outputDefs.size()) {
      const sw::SlotDef& d = cur->outputDefs[idx - nIn];
      r = {true, /*isInput=*/true, d.id, d.dataType};
    }
    return r;
  }
  const sw::SymbolChild* c = sw::childById(*cur, childId);
  const sw::NodeSpec* s = c ? sw::findSpec(c->symbolId) : nullptr;
  if (!s || idx < 0 || idx >= (int)s->ports.size()) return r;
  const sw::PortSpec& p = s->ports[idx];
  return {true, p.isInput, p.id, p.dataType};
}

bool pinIsInput(int pin) {
  PinInfo p = pinInfoOf(pin);
  return p.valid && p.isInput;
}

int pinOfSlot(int childId, const std::string& slotId, bool isInput) {
  const sw::Symbol* cur = sw::doc::currentSymbolConst();
  if (!cur) return -1;
  if (childId == sw::kSymbolBoundary) {
    const int nIn = (int)cur->inputDefs.size();
    if (!isInput) {  // a wire SOURCE at the boundary = one of the symbol's inputDefs
      for (int i = 0; i < nIn; ++i)
        if (cur->inputDefs[i].id == slotId) return boundaryPinId(i);
    } else {  // a wire SINK at the boundary = one of the symbol's outputDefs
      for (size_t j = 0; j < cur->outputDefs.size(); ++j)
        if (cur->outputDefs[j].id == slotId) return boundaryPinId(nIn + (int)j);
    }
    return -1;
  }
  const sw::SymbolChild* c = sw::childById(*cur, childId);
  const sw::NodeSpec* s = c ? sw::findSpec(c->symbolId) : nullptr;
  if (!s) return -1;
  for (size_t i = 0; i < s->ports.size(); ++i)
    if (s->ports[i].isInput == isInput && s->ports[i].id == slotId) return childPinId(childId, (int)i);
  return -1;
}

bool wireOfLink(uint64_t id, sw::SymbolConnection& out) {
  PinInfo sp = pinInfoOf(linkSrcPin(id));
  PinInfo dp = pinInfoOf(linkDstPin(id));
  if (!sp.valid || !dp.valid) return false;
  out.srcChild = pinChildId(linkSrcPin(id));
  out.srcSlot = sp.slotId;
  out.dstChild = pinChildId(linkDstPin(id));
  out.dstSlot = dp.slotId;
  return true;
}

std::string boundaryDefSlot(const sw::Symbol& cur, int edId, bool& isInput) {
  if (edId <= -1001) {  // output def j = -id - 1001
    isInput = false;
    int j = -edId - 1001;
    if (j >= 0 && j < (int)cur.outputDefs.size()) return cur.outputDefs[j].id;
  } else if (edId < 0) {  // input def i = -id - 1
    isInput = true;
    int i = -edId - 1;
    if (i >= 0 && i < (int)cur.inputDefs.size()) return cur.inputDefs[i].id;
  }
  return "";
}

int runCanvasIdsSelfTest(bool injectBug) {
  // 1) Child-pin ed ids live strictly inside [kChildPinBase, kBoundaryPinBase) — they can
  //    never equal a child NODE id (small positive ints) or a boundary pin. Sweep past the
  //    regression point (child ids >= 101, the 批次9 D4 freeze: node 109 == raw pin 109).
  bool banded = true;
  for (int c = 1; c <= 200 && banded; ++c)
    for (int k = 0; k < 12 && banded; ++k) {
      const int pin = injectBug ? sw::pinId(c, k) : childPinId(c, k);  // bug = the OLD raw scheme
      banded = pin >= kChildPinBase && pin < kBoundaryPinBase && pin != c;
    }
  // 2) Decode inverts encode for child pins…
  bool inverts = true;
  for (int c = 1; c <= 200 && inverts; ++c)
    for (int k = 0; k < 12 && inverts; ++k)
      inverts = pinChildId(childPinId(c, k)) == c && pinPortIndex(childPinId(c, k)) == k;
  // 3) …and the boundary band still decodes to the sentinel + combined index.
  const bool boundary = pinChildId(boundaryPinId(3)) == sw::kSymbolBoundary &&
                        pinPortIndex(boundaryPinId(3)) == 3 && pinIsBoundary(boundaryPinId(0)) &&
                        !pinIsBoundary(childPinId(200, 11));
  const bool ok = banded && inverts && boundary;
  std::printf("[selftest-canvasids] banded=%d inverts=%d boundary=%d -> %s\n", banded, inverts,
              boundary, ok ? "PASS" : "FAIL");
  return ok ? 0 : 1;
}

}  // namespace sw::ui
