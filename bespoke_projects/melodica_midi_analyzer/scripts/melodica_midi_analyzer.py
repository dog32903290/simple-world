CC_LOUDNESS = 20
CC_ATTACK = 21
CC_DENSITY = 22
CC_DECAY = 23

ATTACK_CV_PATH = "melodica_attack_cv~input"
DENSITY_CV_PATH = "melodica_density_cv~input"
DECAY_CV_PATH = "melodica_decay_cv~input"

ATTACK_THRESHOLD = 0.025
ATTACK_RELEASE_MS = 80.0
DENSITY_STEP = 0.125
DENSITY_DECAY_STEP = 0.003
ATTACK_DECAY_STEP = 0.04
DECAY_STEP = 0.04
DECAY_TICK = 0.05

pulse_count = 0
attack_value = 0.0
density_value = 0.0
decay_value = 0.0
tick_active = False


def _cc(value):
    return max(0, min(127, int(round(value * 127))))


def _write_values(attack, density, decay):
    me.set(ATTACK_CV_PATH, attack)
    me.set(DENSITY_CV_PATH, density)
    me.set(DECAY_CV_PATH, decay)
    me.set("a", attack)
    me.set("b", density)
    me.set("c", decay)
    me.set("d", min(1.0, pulse_count / 8.0))


def _send_midi(attack, density, decay):
    me.send_cc(CC_ATTACK, _cc(attack), 0)
    me.send_cc(CC_DENSITY, _cc(density), 0)
    me.send_cc(CC_DECAY, _cc(decay), 0)


def setup():
    me.set_num_note_outputs(1)
    _write_values(0.0, 0.0, 0.0)
    me.output("melodica analyzer ready: a attack, b density, c decay, d pulse debug")


def reset_state():
    global pulse_count
    global attack_value
    global density_value
    global decay_value
    global tick_active
    pulse_count = 0
    attack_value = 0.0
    density_value = 0.0
    decay_value = 0.0
    tick_active = False
    _write_values(0.0, 0.0, 0.0)


def on_pulse():
    global pulse_count
    global attack_value
    global density_value
    global decay_value
    global tick_active
    pulse_count = pulse_count + 1
    attack_value = 1.0
    density_value = density_value + DENSITY_STEP
    if density_value > 1.0:
        density_value = 1.0
    decay_value = 1.0
    _write_values(attack_value, density_value, decay_value)
    _send_midi(attack_value, density_value, decay_value)
    if not tick_active:
        tick_active = True
        me.schedule_call(DECAY_TICK, "decay_tick()")


def decay_tick():
    global attack_value
    global density_value
    global decay_value
    global tick_active
    attack_value = attack_value - ATTACK_DECAY_STEP
    if attack_value < 0.0:
        attack_value = 0.0
    decay_value = decay_value - DECAY_STEP
    if decay_value < 0.0:
        decay_value = 0.0
    density_value = density_value - DENSITY_DECAY_STEP
    if density_value < 0.0:
        density_value = 0.0
    _write_values(attack_value, density_value, decay_value)
    _send_midi(attack_value, density_value, decay_value)
    if attack_value > 0.0 or decay_value > 0.0 or density_value > 0.0:
        me.schedule_call(DECAY_TICK, "decay_tick()")
    else:
        tick_active = False


def _delete_if_exists(module_api, name):
    try:
        existing = module_api.get(name)
        if existing:
            existing.delete()
    except Exception:
        pass


def build_patch():
    import module

    setup()

    for name in ("melodica_input", "melodica_splitter", "melodica_loudness", "melodica_attack_pulse", "midi_loudness_cc", "melodica_midi_out", "melodica_attack_cv", "melodica_density_cv", "melodica_decay_cv"):
        _delete_if_exists(module, name)

    audio_input = module.create("input", 120, 120)
    audio_input.set_name("melodica_input")

    splitter = module.create("audiosplitter", 310, 120)
    splitter.set_name("melodica_splitter")

    loudness = module.create("leveltocv", 500, 70)
    loudness.set_name("melodica_loudness")
    loudness.set("gain", 35)
    loudness.set("min", 0)
    loudness.set("max", 1)

    pulse = module.create("audiotopulse", 500, 230)
    pulse.set_name("melodica_attack_pulse")
    pulse.set("threshold", ATTACK_THRESHOLD)
    pulse.set("release", ATTACK_RELEASE_MS)

    loudness_cc = module.create("midicc", 720, 70)
    loudness_cc.set_name("midi_loudness_cc")
    loudness_cc.set("control", CC_LOUDNESS)
    loudness_cc.set("value", 0)

    attack_cv = module.create("macroslider", 720, 230)
    attack_cv.set_name("melodica_attack_cv")
    attack_cv.set("input", 0)

    density_cv = module.create("macroslider", 720, 360)
    density_cv.set_name("melodica_density_cv")
    density_cv.set("input", 0)

    decay_cv = module.create("macroslider", 720, 490)
    decay_cv.set_name("melodica_decay_cv")
    decay_cv.set("input", 0)

    midi_out = module.create("midioutput", 950, 150)
    midi_out.set_name("melodica_midi_out")

    script_module = me.me()

    audio_input.set_target(splitter)
    splitter.set_target(0, loudness)
    splitter.set_target(1, pulse)
    loudness.set_target("midi_loudness_cc~value")
    loudness_cc.set_target(midi_out)
    pulse.set_target(script_module)
    script_module.set_target(midi_out)

    me.output("built melodica analyzer patch; choose MIDI device on melodica_midi_out")


setup()
