# Audio Ingest Contract

AudioIngest answers:

```text
how does sound — external (live) or loaded (played) — become a value/event this
interaction graph can react to, without the runtime owning a synthesis/DSP engine?
```

This is the runtime law for the decision: **external-first, two worlds** (L3, L11). The
instrument / synthesis / DSP lives elsewhere; this runtime never grows an internal synth.
But it **may own a media playback transport + sample clock** (World 2 below), and it must
keep live external sound (World 1) inside a stated latency budget.

## Boundary

```text
AudioIngest      := the single boundary where external sound becomes frame-domain input
ExternalAudioHost:= the realtime audio owner we do not control (BespokeSynth, DAW, etc.)
SemanticPath     := OSC / MIDI messages carrying meaning (note, velocity, param, pulse)
SignalPath       := raw audio over a virtual device (e.g. BlackHole), analyzed on our side
AudioInput       := the one canonical internal value/event a visual node consumes
```

Hard non-ownership (scoped — L3/L11):

- the runtime owns **no** synthesis / DSP / instrument / audio-output engine. This stays.
- **World 1 (live external — Bespoke / BlackHole):** no audio callback, no shared sample
  clock; `domain: audio` is an external source publishing into `frame` / `event`. (the
  proven path today — see Proof)
- **World 2 (loaded playback):** the runtime **may own a media playback transport + sample
  clock** — playing a loaded file is not an instrument. Owning that clock means audio,
  automation, and visual share one playhead (tight sync). This is the near-term core.

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
- known limit (**World 1 only**): against *live external* sound the two transports have no
  common clock, so visuals are **frame-quantized** and sample-accurate sync is out of scope.
  The achievable target is **perceptual** simultaneity within a latency budget (below).
  World 2 (owned playback clock) shares one playhead and is **not** subject to this limit.

## Latency Budget (World 1 — live external, L14)

Perceptual simultaneity, not sample-accuracy, is the target. Human audio-visual fusion sits
~20–40 ms; the contract sets:

```text
target  : sound-in → photon-out ≤ 25 ms   (broadband transient features, e.g. kick)
ceiling : ≤ 40 ms                          (beyond = fails "feels simultaneous")
```

- a **measurable gate**: a build that cannot hit the ceiling is a *named* failure, not a
  silent shrug. Numbers are provisional until measured on real hardware.
- dominant cost is the **render→present path** (compositor latency), not analysis —
  low-latency present (high fps, near-exclusive) is a precondition.
- **FFT / spectral / pitch features are exempt** but must declare their window latency (a
  1024-sample window alone is ~21 ms); broadband transients are the budget-friendly case.
- World 1 is reactive (no lookahead); World 2 (owned clock) can be scheduled and is not
  bound by this budget.

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

## Recording From BespokeSynth

BespokeSynth exposes named scalar controls per module (`module.control -> value`)
over its OSC bridge. A SemanticPath recording is a time series of those values:

```text
poll bespoke_cli -> snapshots.json -> tools/bespoke_to_ingest.py -> AUDIO_INGEST fixture
                                                                  -> --audio-ingest-replay / engine
```

`tools/bespoke_to_ingest.py` diffs consecutive control snapshots into the message
log (`setValue` on change, `connect`/`disconnect` on bridge availability), with a
stable integer `trackId` per module (kept in `trackMap`). The converter is pure
and host-independent; only the live poll that produces `snapshots.json` needs a
running BespokeSynth.

`tools/bespoke_live_poll.py` is that live poll. The OSC bridge's `live
list-controls` returns *structured* records whose path uses `~`
(`transport~tempo`), not the flat `{module.control: value}` snapshot schema this
contract documents. `scalar_map()` is the bridge between the two: it collapses
live records into the flat map, keeping only continuous scalars (drops buttons,
text entries, stepped/enum controls). That mapping is pure and host-independent,
so it is unit-tested without a running Bespoke (`map-records` mode); only
`record()` needs the live host. Recording is read-only by default; `--sweep`
injects motion on one control and `--restore` returns it to its original value.

## External Coupling (named exposure)

This contract makes the product **incomplete standalone, by design**. It depends
on an external host's OSC schema and `.bsk` format; their changes can break the
bridge. This coupling is accepted, not hidden.

## Proof

Status: **built** (runtime, not a proof-host shell).

```text
engine  : app/src/runtime/audio_ingest.{h,cpp}   (pure, zero-dep, zero-Metal)
fixture : docs/runtime/fixtures/audio_ingest_semantic_log.json
selftest: simple_world --selftest-audioingest   (and --selftest-audioingest-bug)
replay  : simple_world --audio-ingest-replay docs/runtime/fixtures/audio_ingest_semantic_log.json
bespoke : tools/bespoke_to_ingest.py  (control snapshots -> fixture; tests/bespoke_to_ingest.test.js)
livepoll: tools/bespoke_live_poll.py  (OSC bridge records -> snapshot schema; tests/bespoke_live_poll.test.js)
gate    : tests/audio_ingest_contract.test.js    (contract + fixture shape)
```

### macOS closure (2026-06-08)

The whole boundary is closed on macOS, not only on the Linux sanitizer host:

- Full Metal app builds with both `audio_ingest.cpp` and `audio_ingest_replay.cpp`
  linked (`cd app && cmake -S . -B build && cmake --build build -j`).
- `--selftest-audioingest` PASS / exit 0; `--selftest-audioingest-bug` FAIL / exit 1.
- Replay of the semantic-log fixture traces frame-exact (noteOn 60 @f1, single
  pulse @f2, noteOff @f5, `connected=0 cookable=1` from f6).
- Live proof against a running BespokeSynth: `bespoke_live_poll.py record`
  captured 24 real scalar controls/frame, a `transport~tempo` sweep
  (147->160->175->190->...) flowed through the converter and replay into the
  per-frame `AudioInput` value stream, and the swept control was restored.
- JS gate 8/8 (`audio_ingest_contract` + `bespoke_to_ingest` + `bespoke_live_poll`).

Observed coupling fact: the live bridge occasionally drops a `--wait` response
under rapid calls; `bespoke_live_poll._run_cli` retries once so a sweep cannot
leave the live patch dirty.

`--selftest-audioingest` is hermetic and isolated (constitution Rule 5): it
replays a synthetic SemanticPath log over an 8-frame 60fps sweep and asserts all
three properties; the `-bug` variant flips the engine into degenerate modes
(stepped values, edge-based notes, absent-garbage) so every assertion must FAIL.

Promotion rule still holds: no visual node may consume `AudioInput` until its own
fixture + test extend this proof to the consuming graph.
