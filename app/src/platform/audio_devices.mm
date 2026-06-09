#include "platform/audio_devices.h"

#include <CoreAudio/CoreAudio.h>
#include <CoreFoundation/CoreFoundation.h>

#include <cstdio>
#include <vector>

namespace sw {
namespace {

std::string cfToString(CFStringRef s) {
  if (s == nullptr) return {};
  char buf[512];
  if (CFStringGetCString(s, buf, sizeof(buf), kCFStringEncodingUTF8)) return std::string(buf);
  return {};
}

// Number of input channels a device exposes (0 => it's output-only, skip it).
int inputChannelCount(AudioDeviceID dev) {
  AudioObjectPropertyAddress addr = {kAudioDevicePropertyStreamConfiguration,
                                     kAudioObjectPropertyScopeInput,
                                     kAudioObjectPropertyElementMain};
  UInt32 size = 0;
  if (AudioObjectGetPropertyDataSize(dev, &addr, 0, nullptr, &size) != noErr || size == 0) return 0;
  std::vector<unsigned char> buf(size);
  AudioBufferList* list = reinterpret_cast<AudioBufferList*>(buf.data());
  if (AudioObjectGetPropertyData(dev, &addr, 0, nullptr, &size, list) != noErr) return 0;
  int channels = 0;
  for (UInt32 i = 0; i < list->mNumberBuffers; ++i)
    channels += (int)list->mBuffers[i].mNumberChannels;
  return channels;
}

std::string deviceString(AudioDeviceID dev, AudioObjectPropertySelector sel) {
  AudioObjectPropertyAddress addr = {sel, kAudioObjectPropertyScopeGlobal,
                                     kAudioObjectPropertyElementMain};
  CFStringRef cf = nullptr;
  UInt32 size = sizeof(cf);
  if (AudioObjectGetPropertyData(dev, &addr, 0, nullptr, &size, &cf) != noErr) return {};
  std::string out = cfToString(cf);
  if (cf) CFRelease(cf);
  return out;
}

AudioDeviceID defaultInputDevice() {
  AudioObjectPropertyAddress addr = {kAudioHardwarePropertyDefaultInputDevice,
                                     kAudioObjectPropertyScopeGlobal,
                                     kAudioObjectPropertyElementMain};
  AudioDeviceID dev = kAudioObjectUnknown;
  UInt32 size = sizeof(dev);
  AudioObjectGetPropertyData(kAudioObjectSystemObject, &addr, 0, nullptr, &size, &dev);
  return dev;
}

}  // namespace

std::vector<AudioDevice> listInputDevices() {
  std::vector<AudioDevice> out;

  AudioObjectPropertyAddress addr = {kAudioHardwarePropertyDevices,
                                     kAudioObjectPropertyScopeGlobal,
                                     kAudioObjectPropertyElementMain};
  UInt32 size = 0;
  if (AudioObjectGetPropertyDataSize(kAudioObjectSystemObject, &addr, 0, nullptr, &size) != noErr)
    return out;
  const int count = (int)(size / sizeof(AudioDeviceID));
  std::vector<AudioDeviceID> ids(count);
  if (AudioObjectGetPropertyData(kAudioObjectSystemObject, &addr, 0, nullptr, &size, ids.data()) !=
      noErr)
    return out;

  const AudioDeviceID def = defaultInputDevice();
  for (AudioDeviceID id : ids) {
    const int inputs = inputChannelCount(id);
    if (inputs <= 0) continue;  // output-only device, not selectable as a source
    AudioDevice d;
    d.coreAudioId = (unsigned int)id;
    d.name = deviceString(id, kAudioObjectPropertyName);
    d.uid = deviceString(id, kAudioDevicePropertyDeviceUID);
    d.inputChannels = inputs;
    d.isDefault = (id == def);
    if (d.name.empty()) d.name = d.uid.empty() ? "(unnamed input)" : d.uid;
    out.push_back(d);
  }
  return out;
}

int runListAudioDevices() {
  const std::vector<AudioDevice> devices = listInputDevices();
  printf("[audio-devices] %zu input device(s):\n", devices.size());
  for (const AudioDevice& d : devices)
    printf("  %s %-32s  ch=%-2d  uid=%s\n", d.isDefault ? "*" : " ", d.name.c_str(),
           d.inputChannels, d.uid.c_str());
  printf("  (* = system default input)\n");
  return devices.empty() ? 1 : 0;
}

}  // namespace sw
