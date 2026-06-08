# Audio Ingest Contract

AudioIngest answers:

```text
how does externally-produced sound enter this visual runtime, without the
runtime ever owning a realtime audio engine?
```

This is the runtime law for the decision: **sound comes from outside**. The
instrument / synthesis / DSP lives in an external host (e.g. BespokeSynth). My
World stays a single-domain visual `frame` runtime and never grows an internal
low-latency audio domain.

## Boundary

```text
AudioIngest      := the single boundary where external sound becomes frame-domain input
ExternalAudioHost:= the realtime audio owner we do not control (BespokeSynth, DAW, etc.)
SemanticPath     := OSC / MIDI messages carrying meaning (note, velocity, param, pulse)
SignalPath       := raw audio over a virtual device (e.g. BlackHole), analyzed on our side
AudioInput       := the one canonical internal value/event a visual node consumes
```

Hard non-ownership:

- the runtime owns **no** audio callback, **no** sample clock, **no** DSP graph
- `domain: audio` is **not** an internal cook domain; it is an external source
  that publishes into the `frame` and `event` domains
- there is **no shared sample clock** across this boundary; see Clock Crossing

## Rule 1 — One Canonical Representation

Visual nodes consume `AudioInput` and **must not know** whether a value arrived
via `SemanticPath` or `SignalPath`. The ingest boundary collapses both sources
into the same internal shape:

```text
AudioInput.value   : Float / Vec[n]   (continuous, latest-wins)
AudioInput.event   : Event            (discrete trigger, e.g. note-on, pulse)
AudioInput.buffer  : PointBuffer/array (optional small immutable slice, e.g. FFT / waveform)
AudioInput.trackId : stable id        (which external track/bus this came from)
```

Source path is metadata, never branching logic inside a visual node.

## Rule 2 — Clock Crossing Is Owned Here (thin leaf)

All clock-domain reconciliation lives **inside** this boundary and nowhere else
(constitution rule 3). Visual nodes never see jitter, sample rate, or transport
detail.

- external values are sampled into the current frame context at frame rate
  (`frameIndex / time / deltaTime` from `FrameScheduler`)
- continuous values are **latest-wins** and **smoothed** at the boundary to avoid
  step artifacts when a downstream node animates from them
- discrete events are **state-based / idempotent** where possible (a dropped OSC
  packet must not strand a stuck note); events may carry an external timestamp
  for sub-frame ordering, but the runtime still publishes them at a frame boundary
- known limit, stated not hidden: visuals are **frame-quantized** against sound.
  Sample-accurate audio→visual sync is **out of scope by construction** (the two
  transports have no common clock).

## Rule 3 — Source-Absent Is a Defined State

Because external sound is a **necessary** line, its absence is a runtime state,
not a crash.

| condition | policy |
|---|---|
| host not running / never connected | `AudioInput` reports `disconnected`; values hold default; no events |
| source disconnects mid-session | hold last value (configurable: hold \| zero \| markInvalid); stop emitting events |
| malformed / out-of-range message | drop message + diagnostic; do not mutate downstream |
| track id unknown to current graph | drop + diagnostic; never auto-create nodes |

A visual graph must remain cookable and non-black while `disconnected`.

## Non-Goals

```text
- internal synthesis / DSP / audio output        (lives in ExternalAudioHost)
- sample-accurate audio→visual sync              (no shared clock; see Rule 2)
- visual → audio control as a core path          (optional OSC round-trip only,
                                                  latency-bound, never realtime)
- cross-platform signal routing guarantee        (SignalPath via BlackHole is
                                                  macOS-first; other OSes need a
                                                  different virtual device)
```

## Semantic vs Signal — When To Use Which

```text
visual follows the music's MEANING (this track played C4, kicked) -> SemanticPath
visual follows the sound's TEXTURE (timbre, noise, spectral shape) -> SignalPath
```

Prefer `SemanticPath` (we already have `tools/bespoke_cli` + the BespokeSynth OSC
bridge). Add `SignalPath` only when a value cannot be reconstructed from meaning.

## External Coupling (named exposure)

This contract makes the product **incomplete standalone, by design**. It depends
on an external host's OSC schema and `.bsk` format; their changes can break the
bridge. This coupling is accepted, not hidden.

## Proof

Status: **not yet built (proof gap)**. Per `CONTRACT_GAPS.md`, before
implementation this contract requires:

```text
fixture : a recorded SemanticPath message log + a graph that consumes AudioInput
test    : assert source-absent stays non-black; assert latest-wins + smoothing;
          assert a dropped discrete event does not strand state
runner  : an ingest shell that replays the message log into frame-sampled AudioInput
```

No visual node may be promoted to consume audio until the fixture + test exist.
