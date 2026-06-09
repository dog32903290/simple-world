// platform/audio_devices — enumerate the machine's audio INPUT devices (built-in mic,
// USB interfaces like the Focusrite 2i2, BlackHole loopback, aggregates). The
// preference UI lists these so 柏為 picks the source — Ableton-style. BlackHole and
// the 2i2 both appear here (they ARE input devices in CoreAudio), so listing inputs
// covers "real interface" and "system audio via BlackHole" alike.
//
// Pure CoreAudio query: listing devices needs NO microphone permission (only
// capturing samples does). platform leaf — no UI, no runtime deps.
#pragma once
#include <string>
#include <vector>

namespace sw {

struct AudioDevice {
  unsigned int coreAudioId = 0;  // AudioDeviceID — transient handle for engine routing
  std::string  uid;              // stable UID — persist THIS across launches/reconnects
  std::string  name;             // human label for the menu (e.g. "Focusrite USB Audio")
  int          inputChannels = 0;
  bool         isDefault = false;  // the current system default input
};

// All devices that have at least one input channel, in CoreAudio order. The system
// default input is flagged isDefault. Empty vector on a query failure.
std::vector<AudioDevice> listInputDevices();

// CLI proof: print the scan (name / uid / channels / default). Confirms enumeration
// works and shows what's selectable on this machine. No mic permission required.
int runListAudioDevices();

}  // namespace sw
