// platform/midi_loopback.mm — CoreMIDI in-process loopback + raw MIDI → value decode.
//
// Ground truth (external/tixl, read-only):
//   Operators/Lib/io/midi/MidiInput.cs:296-380  ProcessParsedMidiEvent: the raw NAudio MidiEvent →
//     MidiSignal switch. Faithfully mirrored in midiDecodeRaw below:
//       ControlChangeEvent → ControllerId = Controller(data1), ControllerValue = ControllerValue(data2),
//                            EventType = ControllerChanges
//       NoteEvent / NoteOn → ControllerId = NoteNumber(data1), ControllerValue = Velocity(data2),
//                            EventType = Notes
//       NoteEvent / NoteOff→ ControllerValue = 0 (FORCED, regardless of release velocity), Notes
//       PitchWheel         → ControllerId = 10000 + cmd, ControllerValue = Pitch(14-bit), MidiEvent
//       AfterTouch/Patch   → ControllerId = 10000 + cmd, ControllerValue = pressure/patch, MidiEvent
//
// NOTE on NoteOn-vel0: NAudio's MidiEvent.FromRawMessage does NOT remap NoteOn vel0 to NoteOff at the
// CommandCode level — it keeps CommandCode==NoteOn. TiXL's NoteOn branch (MidiInput.cs:313/332) handles
// it by setting both Velocity=0 and ControllerValue=0. We match that: status 0x90 with data2==0 stays
// in the NoteOn branch below and sets controllerValue=0. The VALUE is correct; only the old comment
// was wrong.
//
// TiXL's transport is NAudio (Windows-only); we swap CoreMIDI for the transport and re-implement only
// the decode. The virtual source + input port form an in-process loopback (no external device).
//
// platform leaf: CoreMIDI C API (no ObjC objects → built -fno-objc-arc like the other manual-lifetime
// .mm files); no runtime / app includes.
#include "platform/midi_loopback.h"

#include <CoreMIDI/CoreMIDI.h>

#include <atomic>

namespace sw {

bool midiDecodeRaw(int status, int data1, int data2, MidiEventKind& kind, int& channel,
                   int& controllerId, int& controllerValue) {
  if (status < 0x80) return false;  // not a status byte (data byte / leftover) → no signal
  int cmd = status & 0xF0;
  channel = (status & 0x0F) + 1;  // 1-based (1–16), matching NAudio MidiEvent.Channel
  switch (cmd) {
    case 0x90: {  // NoteOn
      if (data2 == 0) {  // NoteOn vel0 stays NoteOn; TiXL sets controllerValue=0 (see file comment)
        kind = MidiEventKind::Note;
        controllerId = data1;       // NoteNumber
        controllerValue = 0;        // forced (vel=0)
      } else {
        kind = MidiEventKind::Note;
        controllerId = data1;       // NoteNumber
        controllerValue = data2;    // Velocity
      }
      return true;
    }
    case 0x80:  // NoteOff
      kind = MidiEventKind::Note;
      controllerId = data1;         // NoteNumber
      controllerValue = 0;          // forced zero (TiXL NoteOff branch)
      return true;
    case 0xB0:  // ControlChange
      kind = MidiEventKind::ControllerChange;
      controllerId = data1;         // Controller
      controllerValue = data2;      // ControllerValue
      return true;
    case 0xE0:  // PitchWheel (14-bit: data1 = LSB, data2 = MSB)
      kind = MidiEventKind::Other;
      controllerId = 10000 + 0xE0;  // 10000 + (int)CommandCode (PitchWheelChange)
      controllerValue = (data1 & 0x7F) | ((data2 & 0x7F) << 7);
      return true;
    case 0xD0:  // ChannelAfterTouch (1 data byte = pressure)
      kind = MidiEventKind::Other;
      controllerId = 10000 + 0xD0;
      controllerValue = data1;
      return true;
    case 0xC0:  // PatchChange (1 data byte = patch)
      kind = MidiEventKind::Other;
      controllerId = 10000 + 0xC0;
      controllerValue = data1;
      return true;
    default:
      return false;  // system / unsupported → TiXL produces no MidiSignal
  }
}

struct MidiLoopbackDevice::Impl {
  MIDIClientRef   client = 0;
  MIDIPortRef     inputPort = 0;
  MIDIEndpointRef virtualSource = 0;
  EventCallback   cb = nullptr;
  void*           user = nullptr;
  std::atomic<bool> running{false};

  // CoreMIDI read callback (read thread). Walks the packet list, decodes each 3-byte channel-voice
  // message, and forwards via cb.
  static void readProc(const MIDIPacketList* pktList, void* readProcRefCon, void* /*srcRefCon*/) {
    Impl* self = static_cast<Impl*>(readProcRefCon);
    if (!self || !self->cb) return;
    const MIDIPacket* pkt = &pktList->packet[0];
    for (UInt32 i = 0; i < pktList->numPackets; ++i) {
      UInt16 n = pkt->length;
      UInt16 j = 0;
      while (j < n) {
        int status = pkt->data[j];
        if (status < 0x80) { j++; continue; }  // skip stray data bytes
        int cmd = status & 0xF0;
        int dataLen = (cmd == 0xC0 || cmd == 0xD0) ? 1 : 2;  // 1-data-byte commands
        if (j + dataLen >= n) break;  // not enough bytes for status + data → stop
        int d1 = pkt->data[j + 1];
        int d2 = (dataLen == 2) ? pkt->data[j + 2] : 0;
        MidiEventKind kind;
        int channel, controllerId, controllerValue;
        if (midiDecodeRaw(status, d1, d2, kind, channel, controllerId, controllerValue)) {
          self->cb(self->user, kind, channel, controllerId, controllerValue);
        }
        j += 1 + dataLen;
      }
      pkt = MIDIPacketNext(pkt);
    }
  }
};

MidiLoopbackDevice::MidiLoopbackDevice() : impl_(new Impl()) {}
MidiLoopbackDevice::~MidiLoopbackDevice() {
  stopListening();
  delete impl_;
}

bool MidiLoopbackDevice::startListening(EventCallback cb, void* user) {
  stopListening();
  impl_->cb = cb;
  impl_->user = user;

  OSStatus st = MIDIClientCreate(CFSTR("SW Loopback Client"), nullptr, nullptr, &impl_->client);
  if (st != noErr || impl_->client == 0) return false;

  st = MIDIInputPortCreate(impl_->client, CFSTR("SW Loopback In"), &Impl::readProc, impl_,
                           &impl_->inputPort);
  if (st != noErr || impl_->inputPort == 0) { stopListening(); return false; }

  // Virtual source we both publish and read from (in-process loopback).
  st = MIDISourceCreate(impl_->client, CFSTR("SW Loopback"), &impl_->virtualSource);
  if (st != noErr || impl_->virtualSource == 0) { stopListening(); return false; }

  // Connect our input port to our own virtual source so MIDIReceived() on the source is delivered to
  // readProc. refCon (3rd arg) is per-source; we pass nullptr (we key off readProcRefCon = impl_).
  st = MIDIPortConnectSource(impl_->inputPort, impl_->virtualSource, nullptr);
  if (st != noErr) { stopListening(); return false; }

  impl_->running.store(true);
  return true;
}

void MidiLoopbackDevice::stopListening() {
  if (impl_->inputPort && impl_->virtualSource) {
    MIDIPortDisconnectSource(impl_->inputPort, impl_->virtualSource);
  }
  if (impl_->virtualSource) { MIDIEndpointDispose(impl_->virtualSource); impl_->virtualSource = 0; }
  if (impl_->inputPort)     { MIDIPortDispose(impl_->inputPort);         impl_->inputPort = 0; }
  if (impl_->client)        { MIDIClientDispose(impl_->client);          impl_->client = 0; }
  impl_->running.store(false);
}

bool MidiLoopbackDevice::sendRawMidi(int status, int data1, int data2) {
  if (!isListening()) return false;
  int cmd = status & 0xF0;
  int len = (cmd == 0xC0 || cmd == 0xD0) ? 2 : 3;  // status + 1 or 2 data bytes
  Byte bytes[3] = {Byte(status & 0xFF), Byte(data1 & 0x7F), Byte(data2 & 0x7F)};

  Byte packetBuffer[64];
  MIDIPacketList* pktList = reinterpret_cast<MIDIPacketList*>(packetBuffer);
  MIDIPacket* pkt = MIDIPacketListInit(pktList);
  pkt = MIDIPacketListAdd(pktList, sizeof(packetBuffer), pkt, 0, len, bytes);
  if (!pkt) return false;
  OSStatus st = MIDIReceived(impl_->virtualSource, pktList);
  return st == noErr;
}

bool MidiLoopbackDevice::isListening() const { return impl_->running.load(); }

}  // namespace sw
