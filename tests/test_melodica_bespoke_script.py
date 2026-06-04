import runpy
import unittest
from pathlib import Path


SCRIPT = Path(__file__).resolve().parents[1] / "bespoke_projects" / "melodica_midi_analyzer" / "scripts" / "melodica_midi_analyzer.py"


class FakeMe:
    def __init__(self):
        self.cc = []
        self.scheduled = []
        self.sets = []
        self.outputs = None

    def set_num_note_outputs(self, count):
        self.outputs = count

    def send_cc(self, control, value, output_index=0):
        self.cc.append((control, value, output_index))

    def set(self, path, value):
        self.sets.append((path, value))

    def schedule_call(self, delay, method):
        self.scheduled.append((delay, method))

    def output(self, value):
        pass


class MelodicaBespokeScriptTest(unittest.TestCase):
    def load_script(self):
        fake = FakeMe()
        namespace = runpy.run_path(str(SCRIPT), init_globals={"me": fake})
        return fake, namespace

    def test_setup_declares_one_note_output_for_midioutput(self):
        fake, _ = self.load_script()
        self.assertEqual(fake.outputs, 1)

    def test_pulse_sends_attack_density_and_decay_cc(self):
        fake, namespace = self.load_script()

        namespace["on_pulse"]()

        self.assertIn((21, 127, 0), fake.cc)
        self.assertIn((22, 16, 0), fake.cc)
        self.assertIn((23, 127, 0), fake.cc)
        self.assertIn(("melodica_attack_cv~input", 1.0), fake.sets)
        self.assertIn(("melodica_density_cv~input", 0.125), fake.sets)
        self.assertIn(("melodica_decay_cv~input", 1.0), fake.sets)
        self.assertIn((0.05, "decay_tick()"), fake.scheduled)

    def test_density_rises_when_pulses_cluster(self):
        fake, namespace = self.load_script()

        namespace["on_pulse"]()
        first_density = [value for path, value in fake.sets if path == "melodica_density_cv~input"][-1]
        namespace["on_pulse"]()
        second_density = [value for path, value in fake.sets if path == "melodica_density_cv~input"][-1]

        self.assertGreater(second_density, first_density)

    def test_decay_tick_lowers_decay_cc_after_pulse(self):
        fake, namespace = self.load_script()

        namespace["on_pulse"]()
        namespace["decay_tick"]()

        decay_values = [value for control, value, _ in fake.cc if control == 23]
        self.assertLess(decay_values[-1], 127)
        self.assertGreater(decay_values[-1], 0)
        decay_cv_values = [value for path, value in fake.sets if path == "melodica_decay_cv~input"]
        self.assertLess(decay_cv_values[-1], 1.0)
        self.assertGreater(decay_cv_values[-1], 0)


if __name__ == "__main__":
    unittest.main()
