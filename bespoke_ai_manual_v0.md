# BespokeSynth AI-readable Manual v0
> Purpose: reduce hallucination when answering BespokeSynth architecture/module questions. This is a v0 evidence map, not a full behavioral reimplementation manual.
## Source policy
- **HIGH**: directly supported by inspected source code or official docs.
- **MEDIUM**: supported by official docs excerpt but not cross-checked against class source.
- **LOW**: module appears in official index/source registry; detailed behavior not yet extracted.
- Rule: if an entry is LOW, do not invent wiring or parameter behavior.
## Repository / build facts
- Repo: `BespokeSynth/BespokeSynth`
- License: GPL v3.
- README features: live-patchable environment; VST/VST3/LV2 hosting; Python livecoding; MIDI & OSC mapping; Windows/Mac/Linux.
- README build: `git submodule update --init --recursive`; `cmake -Bignore/build -DCMAKE_BUILD_TYPE=Release`; `cmake --build ignore/build --parallel 4 --config Release`.
## Architecture summary
```yaml
core_classes:
  IDrawableModule:
    role: Base visual/canvas module.
    responsibilities: rendering, UI controls, patch cable sources, save/load, category, enabled/minimized/pinned state.
  ModuleFactory:
    role: Native module registry and spawnable module provider.
    registers: name -> creator, category, hidden/experimental flag, accepts audio/notes/pulses.
  IAudioSource:
    role: audio-producing interface.
    key_method: Process(double time)
  IAudioReceiver:
    role: audio-receiving interface.
    key_data: ChannelBuffer input buffer
  IAudioProcessor:
    role: audio effect/processor; combines source + receiver.
  INoteReceiver:
    role: receives NoteMessage, CC, pressure, MIDI.
  INoteSource:
    role: sends notes through NoteOutput.
  IPulseReceiver:
    role: receives OnPulse(time, velocity, flags).
  IPulseSource:
    role: dispatches pulses.
  VSTPlugin:
    role: wraps external plugins via JUCE AudioProcessor as Bespoke modules.
module_categories:
  - Note
  - Synth
  - Audio
  - Instrument
  - Processor
  - Modulator
  - Pulse
  - Other
spawn_methods:
  - Module
  - EffectChain
  - Prefab
  - Plugin
  - MidiController
  - Preset
ai_strategy:
  v0: AI generates external plugin modules; Bespoke hosts them.
  avoid: AI directly edits ModuleFactory or arbitrary native module internals.
```
## Official module registry v0
Columns: `id`, `category`, `confidence`, `usage_note`.

### instruments
| id | confidence | usage_note |
|---|---:|---|
| `circlesequencer` | medium | polyrhythmic sequencer that displays a loop as a circle |
| `dotsequencer` | medium | polyphonic note sequencer with adjustable note length and velocity |
| `drumsequencer` | medium | step sequencer intended for drums |
| `euclideansequencer` | medium | euclidean sequencer displays a loop as a circle; notes are calculated with the euclidean rhythm algorithm |
| `fouronthefloor` | medium | sends note 0 every beat, to trigger a kick drum |
| `gridkeyboard` | medium | grid-based keyboard intended primarily for 64-pad grid controllers |
| `keyboarddisplay` | medium | displays input notes on a keyboard and allows clicking the keyboard to create notes |
| `m185sequencer` | medium | sequencer using the M185 / Intellijel Metropolis paradigm |
| `midicontroller` | medium | gets MIDI input from external devices and maps MIDI / grid controller data to Bespoke controls |
| `notecanvas` | medium | looping note roll |
| `notechain` | medium | trigger a note followed by a pulse to trigger another note after a delay |
| `notecounter` | medium | advance through pitches sequentially; useful for driving notesequencer or drumsequencer |
| `notecreator` | medium | create a one-off note |
| `notelooper` | medium | note loop recorder with overdubbing and replacement functionality |
| `notesequencer` | medium | looping sequence of notes at an interval |
| `notesinger` | medium | output a note based on a detected pitch |
| `playsequencer` | medium | drum sequencer for punch-in record / overdub steps, inspired by Pulsar-23 |
| `polyrhythms` | medium | looping sequence with lines on different divisions |
| `randomnote` | medium | play a note at a given interval with a random chance |
| `slidersequencer` | medium | trigger notes along a continuous timeline |

### note_effects
| id | confidence | usage_note |
|---|---:|---|
| `arpeggiator` | medium | arpeggiates held notes |
| `capo` | medium | shifts incoming notes by semitones |
| `chorddisplayer` | medium | display which chord is playing in the context of the current scale |
| `chorder` | medium | takes an incoming pitch and plays additional notes to form chords |
| `chordholder` | medium | keeps notes pressed at the same time sustaining until new notes are pressed |
| `gridnotedisplayer` | medium | use with gridkeyboard to display currently playing notes |
| `midicc` | medium | outputs MIDI control change messages to route to a midioutput module |
| `midioutput` | medium | send MIDI to an external destination |
| `modulationvizualizer` | medium | display MPE modulation values for notes |
| `modwheel` | medium | adds an expression/modwheel value to a note |
| `modwheeltopressure` | medium | swaps expression input to MIDI pressure in the output |
| `modwheeltovibrato` | medium | convert note mod wheel input to rhythmic pitch bend |
| `mpesmoother` | medium | smooth out MPE parameters |
| `mpetweaker` | medium | adjust incoming MPE modulation values |
| `notechance` | medium | randomly allow notes through |
| `notedelayer` | medium | delay input notes by a specified amount |
| `notedisplayer` | medium | show input note info |
| `noteduration` | medium | sets the length a note will play, ignoring the note-off message |
| `noteecho` | medium | output incoming notes at specified delays |
| `noteexpression` | medium | route notes based on evaluated expressions; p=pitch, v=velocity |
| `notefilter` | medium | only allow certain pitches through |
| `noteflusher` | medium | send note-off for all notes |
| `notegate` | medium | allow or disallow notes to pass through |
| `notehocket` | medium | send notes to random destinations |
| `notehumanizer` | medium | add randomness to timing and velocity |
| `notelatch` | medium | use note-on messages to toggle notes on/off |
| `noteoctaver` | medium | transpose a note by octaves |
| `notepanalternator` | medium | set note pan, alternating between two values |
| `notepanner` | medium | set a note's pan |
| `notepanrandom` | medium | set note pan to random values |
| `noterangefilter` | medium | allow notes through within a pitch range |
| `noteratchet` | medium | rapidly repeat an input note over a duration |
| `noterouter` | medium | control where notes are routed using a UI control |
| `notesorter` | medium | separate notes by pitch |
| `notestepper` | medium | output notes through round-robin patch cables |
| `notestream` | medium | view a stream of notes as they are played |
| `notestrummer` | medium | send a chord and move a slider to strum notes |
| `notetable` | medium | map a pitch starting from zero to a scale pitch |
| `notewrap` | medium | wrap an input pitch to stay within a desired range |
| `pitchbender` | medium | add pitch bend to notes |
| `pitchdive` | medium | use pitchbend to settle into input pitch from a starting offset |
| `pitchpanner` | medium | add pan modulation based on input pitch |
| `pitchremap` | medium | remap input pitches to output pitches |
| `pitchsetter` | medium | set an incoming note to a specified pitch |
| `portamento` | medium | monophonic note handling with pitch bend glide |
| `pressure` | medium | add pressure modulation to notes |
| `pressuretomodwheel` | medium | convert MIDI pressure modulation to modwheel modulation |
| `pressuretovibrato` | medium | convert MIDI pressure to vibrato using pitch bend |
| `previousnote` | medium | when receiving note-on, output the prior note-on |
| `quantizer` | medium | delay inputs until next quantization interval |
| `rhythmsequencer` | medium | repeat held input notes to turn sustained notes into rhythm |
| `scaledegree` | medium | transpose input based on current scale |
| `scaledetect` | medium | detect scales that fit entered notes; last played pitch is root |
| `sustainpedal` | medium | keeps input notes sustaining |
| `transposefrom` | medium | transpose input from specified root to the current scale root |
| `unstablemodwheel` | medium | mutate MPE slide with Perlin noise |
| `unstablepitch` | medium | mutate MPE pitchbend with Perlin noise |
| `unstablepressure` | medium | mutate MPE pressure with Perlin noise |
| `velocitycurve` | medium | adjust velocity based on a curve mapping |
| `velocityscaler` | medium | scale note velocity |
| `velocitysetter` | medium | set note velocity |
| `velocitystepsequencer` | medium | adjust velocity of incoming notes based on a sequence |
| `velocitytochance` | medium | use note velocity to determine chance a note plays |
| `vibrato` | medium | add rhythmic oscillating pitch bend |
| `voicesetter` | medium | set a specific voice/channel index for a note |
| `volcabeatscontrol` | medium | outputs MIDI data to control KORG Volca Beats |
| `whitekeys` | medium | remap white keys from C major to the current global scale |

### synths
| id | confidence | usage_note |
|---|---:|---|
| `beats` | medium | multi-loop player for mixing sample layers |
| `drumplayer` | medium | sample player intended for drum playback |
| `drumsynth` | medium | oscillator plus noise drum synth |
| `fmsynth` | medium | FM synthesizer (official docs list; details need source/doc expansion) |
| `karplusstrong` | medium | Karplus-Strong plucked string synth (official docs list; details need source/doc expansion) |
| `metronome` | medium | metronome synth/source (official docs list; details need source/doc expansion) |
| `oscillator` | medium | signal generator oscillator with note/pulse support and many waveform parameters |
| `samplecanvas` | medium | sample canvas synth module (official docs list; details need source/doc expansion) |
| `sampleplayer` | medium | sample playback synth module (official docs list; details need source/doc expansion) |
| `sampler` | medium | sample-based synth module (official docs list; details need source/doc expansion) |
| `seaofgrain` | medium | granular/sample synth module (official docs list; details need source/doc expansion) |
| `signalgenerator` | medium | synth signal generator (official docs list; details need source/doc expansion) |

### audio_effects
| id | confidence | usage_note |
|---|---:|---|
| `audiometer` | medium | sets a slider to an audio level's volume; useful to map a MIDI display value |
| `audiorouter` | medium | selector for switching where audio is routed |
| `audiosplitter` | medium | send an audio signal to multiple destinations |
| `buffershuffler` | medium | use notes to play slices of a constantly recording live input buffer |
| `dcoffset` | medium | add a constant offset to an audio signal |
| `effectchain` | medium | container holding a serial list of effects |
| `eq` | medium | multi-band equalizer |
| `feedback` | medium | feed delayed audio back earlier in the signal chain |
| `fftvocoder` | medium | FFT-based vocoder |
| `freqdelay` | medium | delay effect with delay length based on input notes |
| `gain` | medium | adjust audio signal volume |
| `input` | medium | get audio from input source, e.g. microphone |
| `inverter` | medium | multiply a signal by -1 |
| `lissajous` | medium | draw input audio as a lissajous curve |
| `looper` | medium | loop input audio; use with looperrecorder for full functionality |
| `looperrecorder` | medium | command center for recording into multiple loopers and retroactive capture |
| `looperrewriter` | medium | rewrite contents of a looper with received input |
| `multitapdelay` | medium | delay with multiple tap points |
| `output` | medium | route audio to output channel / speakers / interface |
| `panner` | medium | pan audio left/right; converts mono input to stereo output |
| `ringmodulator` | medium | modulate signal amplitude at a frequency |
| `samplergrid` | medium | record input onto pads and play back pads |
| `send` | medium | duplicate a signal and send it to a second destination |
| `signalclamp` | medium | clamp audio signal value within a range |
| `spectrum` | medium | display audio signal spectral data |
| `splitter` | medium | split stereo into two mono signals or duplicate mono |
| `stutter` | medium | capture and stutter input |
| `vocoder` | medium | vocoder audio effect (official docs list; details need source/doc expansion) |
| `vocodercarrier` | medium | carrier input for vocoder (official docs list; details need source/doc expansion) |
| `waveformviewer` | medium | waveform display of audio (official docs list; details need source/doc expansion) |
| `waveshaper` | medium | waveshaping audio processor (official docs list; details need source/doc expansion) |

### modulators
| id | confidence | usage_note |
|---|---:|---|
| `accum` | medium | modulator accumulator (official docs list; details need source/doc expansion) |
| `add` | medium | outputs result of value1 + value2; intended as modulation patch targets |
| `addcentered` | medium | centered add modulator (official docs list; details need source/doc expansion) |
| `audiotocv` | medium | audio to modulation/CV converter (official docs list; details need source/doc expansion) |
| `controlrecorder` | medium | record control changes (official docs list; details need source/doc expansion) |
| `controlsequencer` | medium | sequence control values (official docs list; details need source/doc expansion) |
| `curve` | medium | modulator curve mapping (official docs list; details need source/doc expansion) |
| `curvelooper` | medium | loop control/modulation curve (official docs list; details need source/doc expansion) |
| `dataprovider` | medium | modulator data provider (official docs list; details need source/doc expansion) |
| `envelope` | medium | envelope modulator (official docs list; details need source/doc expansion) |
| `expression` | medium | expression-based modulator (official docs list; details need source/doc expansion) |
| `fubble` | medium | modulator module registered by source; details need source expansion |
| `gravity` | medium | gravity-style modulator (official docs list; details need source/doc expansion) |
| `gridsliders` | medium | grid-driven sliders (official docs list; details need source/doc expansion) |
| `leveltocv` | medium | convert audio level to modulation/CV |
| `lfo` | medium | low-frequency oscillator control/modulation source |
| `macroslider` | medium | take one value and send scaled versions to multiple destinations |
| `modwheeltocv` | medium | take note modwheel and convert to modulation value |
| `mult` | medium | multiply value1 by value2 |
| `notetofreq` | medium | convert note pitch to frequency in Hz |
| `notetoms` | medium | convert note pitch frequency to period in milliseconds |
| `pitchtocv` | medium | convert note pitch to modulation value |
| `pitchtospeed` | medium | convert input pitch to speed ratio |
| `pitchtovalue` | medium | output MIDI pitch value |
| `pressuretocv` | medium | convert note pressure to modulation value |
| `ramper` | medium | blend a control to a target value over time |
| `smoother` | medium | output a smoothed value of the input |
| `subtract` | medium | subtract value2 from value1 |
| `valuesetter` | medium | set a specified value on a targeted control |
| `velocitytocv` | medium | convert note velocity to modulation value |
| `vinylcontrol` | medium | output speed value from control vinyl input audio |

### pulse
| id | confidence | usage_note |
|---|---:|---|
| `audiotopulse` | medium | send pulse when audio level exceeds threshold |
| `boundstopulse` | medium | send pulse when input slider hits max/min bound |
| `notetopulse` | medium | trigger a pulse when a note is received |
| `pulsebutton` | medium | trigger a pulse with a button |
| `pulsechance` | medium | randomly allow pulses through |
| `pulsedelayer` | medium | delay pulses |
| `pulsedisplayer` | medium | see flags on pulses |
| `pulseflag` | medium | set properties/flags on pulses |
| `pulsegate` | medium | allow or disallow pulses |
| `pulsehocket` | medium | send pulses to randomized destinations |
| `pulselimit` | medium | limit number of pulses allowed through |
| `pulser` | medium | send pulse messages at an interval |
| `pulserouter` | medium | control where pulses are routed using UI control |
| `pulsesequence` | medium | define a looping sequence of pulses |
| `pulsetrain` | medium | define a list of pulses to execute once pulsed |

### effect_chain
| id | confidence | usage_note |
|---|---:|---|
| `basiceq` | medium | simple multiband EQ |
| `biquad` | medium | filter using biquad formula |
| `bitcrush` | medium | reduce sample resolution and sample rate |
| `butterworth` | medium | filter using Butterworth formula |
| `compressor` | medium | dynamic compressor; lookahead introduces delay |
| `dcremover` | medium | 10 Hz high-pass filter to remove DC offset |
| `delay` | medium | echoing delay |
| `distortion` | medium | waveshaping distortion |
| `freeverb` | medium | reverb using the Freeverb algorithm |
| `gainstage` | medium | control volume inside an effectchain |
| `gate` | medium | only allow signal through above threshold |
| `granulator` | medium | granulate live input |
| `muter` | medium | mute an incoming signal |
| `noisify` | medium | multiply input by white noise |
| `pitchshift` | medium | shift signal pitch |
| `pumper` | medium | rhythmic volume dip / sidechain-like pumping |
| `tremolo` | medium | rhythmic volume modulation |

### other
| id | confidence | usage_note |
|---|---:|---|
| `abletonlink` | medium | synchronize transport with Ableton Link devices/software |
| `clockin` | medium | read clock pulses from external MIDI devices to control transport |
| `clockout` | medium | send clock pulses to external MIDI devices |
| `comment` | medium | box to display explanatory text |
| `eventcanvas` | medium | schedule values over time |
| `globalcontrols` | medium | interface controls for MIDI/navigation/background/canvas parameters |
| `grid` | medium | generic grid used by script module and grid MIDI controllers |
| `groupcontrol` | medium | connect to several checkboxes and control them together |
| `label` | medium | display a label in a large patch |
| `loopergranulator` | medium | use with looper to play loop contents with granular synthesis |
| `multitrackrecorder` | medium | record synchronized audio tracks to disk |
| `notetoggle` | medium | turn a control on/off depending on whether input notes are held |
| `oscoutput` | medium | send OSC messages when sliders change or notes are received |
| `prefab` | medium | collection/container of modules that can be loaded from prefabs menu |
| `push2control` | medium | use Ableton Push 2 to control Bespoke interface |
| `radiosequencer` | medium | sequence so only one value is enabled at a time |
| `samplebrowser` | medium | browse samples and drag them to modules |
| `savestateloader` | medium | load other savestate files |
| `scale` | medium | control global scale/tuning |
| `script` | medium | Python scripting for livecoding notes and module control |
| `scriptstatus` | medium | show current Python scope for debugging |
| `selector` | medium | radio button control to only enable one value at a time |
| `snapshots` | medium | save and restore sets of control values |
| `songbuilder` | medium | large-scale scene/song mode organizer |
| `timelinecontrol` | medium | control global transport position |
| `timerdisplay` | medium | display a timer |
| `transport` | medium | controls tempo/current time/play-pause/reset |
| `valuestream` | medium | display a control value over time |

## Native module warning
Bespoke native modules are centrally registered in `Source/ModuleFactory.cpp`. A native module normally touches class files, module include, `REGISTER(...)`, category, capability flags, UI controls, DSP/event logic, save/load and build integration. Do not let AI modify these without a specific proof task.

## Next extraction tasks
1. Parse full official docs into structured `controls[]` and `accepts[]` for every module.
2. Cross-check each official module against `Source/ModuleFactory.cpp` or EffectFactory.
3. For priority modules, inspect `.h/.cpp` to derive exact class inheritance and process method.
4. Create an answer-time rule: when asked about a module with confidence < HIGH, say what is known and what remains unverified.
