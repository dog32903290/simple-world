// ui/node_draw_selftest — isolation tooth for the node-body live value string + zoom gating
// (TiXL MagGraphCanvas.DrawNode.cs:447/484/399 + ValueUtils.GetValueString:488-502). Zone: ui.
// Pure logic, no imgui/graph: asserts the FORMAT of formatInputValue and the THRESHOLDS of
// nodeShow{Label,Value}AtScale — the exact policy drawChild draws by. --selftest-nodeval -bug
// flips one assertion's expectation to a wrong TiXL value so the tooth provably bites.
#include "ui/node_draw.h"

#include <cstdio>
#include <string>

#include "runtime/graph.h"  // PortSpec / Widget

namespace sw::ui {
namespace {

int g_fail = 0;
void check(bool cond, const char* what) {
  if (!cond) { std::printf("[selftest-nodeval] FAIL: %s\n", what); ++g_fail; }
}

sw::PortSpec floatPort(float def, sw::Widget w = sw::Widget::Slider) {
  sw::PortSpec p;
  p.id = "v"; p.name = "V"; p.dataType = "Float"; p.isInput = true; p.def = def; p.widget = w;
  return p;
}

}  // namespace

int runNodeValSelfTest(bool injectBug) {
  g_fail = 0;

  // ── Format (ValueUtils.GetValueString parity) ───────────────────────────────────────────────
  // Float "{:0.000}" — 3 decimals, fixed.
  check(formatInputValue(floatPort(0.0f), 1.0f, "") == "1.000", "float 1.000");
  check(formatInputValue(floatPort(0.0f), 0.02f, "") == "0.020", "float 0.020");
  check(formatInputValue(floatPort(0.0f), -3.14159f, "") == "-3.142", "float -3.142 (round)");

  // Bool widget → "True"/"False" (TiXL bool ToString).
  check(formatInputValue(floatPort(0.0f, sw::Widget::Bool), 1.0f, "") == "True", "bool True");
  check(formatInputValue(floatPort(0.0f, sw::Widget::Bool), 0.0f, "") == "False", "bool False");

  // Enum widget → option label at the rounded index; out-of-range → "".
  {
    sw::PortSpec e = floatPort(0.0f, sw::Widget::Enum);
    e.labels = {"Off", "On", "Auto"};
    check(formatInputValue(e, 0.0f, "") == "Off", "enum[0]=Off");
    check(formatInputValue(e, 2.0f, "") == "Auto", "enum[2]=Auto");
    check(formatInputValue(e, 9.0f, "") == "", "enum out-of-range -> empty");
  }

  // String → truncated (>24 chars gets an ellipsis); non-value dataTypes → "".
  {
    sw::PortSpec s = floatPort(0.0f);
    s.dataType = "String";
    check(formatInputValue(s, 0.0f, "hi") == "hi", "string short verbatim");
    const std::string longText = "abcdefghijklmnopqrstuvwxyz0123456789";  // 36 chars
    const std::string got = formatInputValue(s, 0.0f, longText);
    check(got.size() == 27 && got.substr(0, 24) == longText.substr(0, 24) &&
          got.substr(24) == "...", "string truncates at 24 + ...");
    sw::PortSpec pts = floatPort(0.0f);
    pts.dataType = "Points";
    check(formatInputValue(pts, 1.0f, "") == "", "non-value dataType -> empty");
  }

  // ── Zoom gating thresholds (DrawNode.cs:399 label>0.25, :484 value>0.4, :447 skip primary) ──
  check(!nodeShowLabelAtScale(0.25f), "label gated AT 0.25 (strict >)");
  check(nodeShowLabelAtScale(0.26f), "label shows above 0.25");
  check(!nodeShowLabelAtScale(0.2f), "label hidden well below 0.25");

  // Value: needs scale>0.4 AND a non-primary input (ordinal>0).
  check(!nodeShowValueAtScale(0.4f, 1), "value gated AT 0.4 (strict >)");
  // -bug: claim the value still shows at exactly 0.4 — flips a real TiXL threshold to RED.
  const bool expectShowAt041 = injectBug ? false : true;
  check(nodeShowValueAtScale(0.41f, 1) == expectShowAt041, "value shows above 0.4 for ordinal>0");
  check(!nodeShowValueAtScale(0.9f, 0), "primary input (ordinal 0) NEVER shows value");
  check(!nodeShowValueAtScale(0.3f, 2), "value hidden below 0.4 even for non-primary");

  if (g_fail == 0) std::printf("[selftest-nodeval] PASS (format + zoom gating, TiXL parity)\n");
  return g_fail == 0 ? 0 : 1;
}

}  // namespace sw::ui
