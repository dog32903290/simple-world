// app/audio_settings — the app-layer coordinator for "which audio input drives the
// graph". The UI (ui layer) lists devices and picks one here; main applies the pick to
// the platform AudioCapture; this persists the choice by stable UID across launches.
// Keeps the UI off platform headers (ui -> app -> platform).
#pragma once
#include <string>
#include <vector>

namespace sw::audio {

// One selectable input (app's view of platform/audio_devices::AudioDevice, so the UI
// needn't include platform).
struct DeviceInfo {
  unsigned int id = 0;        // CoreAudio id (transient handle)
  std::string  uid;           // stable id (this is what gets persisted)
  std::string  name;          // menu label
  int          channels = 0;
  bool         isDefault = false;
};

// The selectable input devices (proxy to platform). An empty selectedUid() means
// "System Default" (whatever the OS input is).
std::vector<DeviceInfo> inputDevices();

// Pick a device by stable UID ("" = system default). Persists the choice and flags a
// pending change for main to apply on the next frame.
void selectByUid(const std::string& uid);
std::string selectedUid();   // "" = system default
std::string selectedName();  // human label of the current pick (for the menu button)

// main: call loadPrefs() once at startup (it also flags a pending change so the saved
// device is applied on the first frame). Each frame call takePendingChange(): if it
// returns true, (re)start capture on outDeviceId (0 = system default).
void loadPrefs();
bool takePendingChange(unsigned int& outDeviceId);

// Live input level for the toolbar meter: main publishes the captured rms (any sound)
// and attack envelope (transients/hits) each frame; the UI reads them. Single-threaded
// (main writes, UI reads, both on the render thread).
void  publishMonitor(float rms, float envelope);
float monitorRms();
float monitorEnvelope();

}  // namespace sw::audio
