#include "app/audio_settings.h"

#include "platform/audio_devices.h"

#include <cstdlib>
#include <fstream>

namespace sw::audio {
namespace {

std::string g_selectedUid;                  // "" = system default
std::string g_selectedName = "System Default";  // cached label (avoid per-frame enum)
bool        g_pending = false;               // a selection (or load) waiting for main to apply

// Persist the picked device by UID in the home dir — it's a machine-level choice
// (his 2i2 / BlackHole), not per-project, so it lives outside the .swproj.
std::string prefsPath() {
  const char* home = std::getenv("HOME");
  return std::string(home ? home : ".") + "/.simple_world_audio_device";
}

void savePrefs() {
  std::ofstream f(prefsPath(), std::ios::trunc);
  if (f) f << g_selectedUid << "\n";
}

unsigned int idForUid(const std::string& uid) {
  if (uid.empty()) return 0;  // system default
  for (const AudioDevice& d : listInputDevices())
    if (d.uid == uid) return d.coreAudioId;
  return 0;  // saved device not currently present -> fall back to default
}

std::string resolveName(const std::string& uid) {
  if (uid.empty()) return "System Default";
  for (const AudioDevice& d : listInputDevices())
    if (d.uid == uid) return d.name;
  return uid + " (not connected)";
}

}  // namespace

std::vector<DeviceInfo> inputDevices() {
  std::vector<DeviceInfo> out;
  for (const AudioDevice& d : listInputDevices())
    out.push_back({d.coreAudioId, d.uid, d.name, d.inputChannels, d.isDefault});
  return out;
}

std::string selectedUid() { return g_selectedUid; }

std::string selectedName() { return g_selectedName; }  // cached at selection/load time

void selectByUid(const std::string& uid) {
  g_selectedUid = uid;
  g_selectedName = resolveName(uid);
  g_pending = true;
  savePrefs();
}

void loadPrefs() {
  std::ifstream f(prefsPath());
  if (f) std::getline(f, g_selectedUid);
  // Strip a trailing newline/space if any.
  while (!g_selectedUid.empty() && (g_selectedUid.back() == '\n' || g_selectedUid.back() == '\r' ||
                                    g_selectedUid.back() == ' '))
    g_selectedUid.pop_back();
  g_selectedName = resolveName(g_selectedUid);
  g_pending = true;  // apply the saved device on the first frame
}

bool takePendingChange(unsigned int& outDeviceId) {
  if (!g_pending) return false;
  g_pending = false;
  outDeviceId = idForUid(g_selectedUid);
  return true;
}

}  // namespace sw::audio
