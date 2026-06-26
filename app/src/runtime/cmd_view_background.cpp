// runtime/cmd_view_background — storage for the Output-window VIEW BACKGROUND ambient (header doc).
// A single file-scope optional: set/cleared by the UI seam (via the app shell), read by the terminal
// Command executor. Not thread-local (unlike the camera scope) — the cook + the UI run on the same
// thread and there is exactly one Output window; a plain global mirrors the single g_pointGraph it
// shadows. UNSET == nullptr == the executor's opaque-black default == byte-identical to before the seam.
#include "runtime/cmd_view_background.h"

namespace sw {
namespace {
bool g_engaged = false;
float g_bg[4] = {0.0f, 0.0f, 0.0f, 1.0f};
}  // namespace

void setCommandViewBackground(float r, float g, float b, float a) {
  g_bg[0] = r; g_bg[1] = g; g_bg[2] = b; g_bg[3] = a;
  g_engaged = true;
}

void clearCommandViewBackground() { g_engaged = false; }

const float* commandViewBackground() { return g_engaged ? g_bg : nullptr; }

}  // namespace sw
