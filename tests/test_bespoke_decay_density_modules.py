import unittest
from pathlib import Path


BESPOKE_SOURCE = Path("/tmp/BespokeSynth/Source")


class BespokeDecayDensityModulesTest(unittest.TestCase):
    def assert_native_module(self, class_name, registered_id, controls, accepts_audio):
        header = BESPOKE_SOURCE / f"{class_name}.h"
        source = BESPOKE_SOURCE / f"{class_name}.cpp"
        factory = BESPOKE_SOURCE / "ModuleFactory.cpp"
        cmake = BESPOKE_SOURCE / "CMakeLists.txt"

        self.assertTrue(header.exists(), f"{class_name}.h should define the native module contract")
        self.assertTrue(source.exists(), f"{class_name}.cpp should implement the native module")

        header_text = header.read_text()
        source_text = source.read_text()
        factory_text = factory.read_text()
        cmake_text = cmake.read_text()

        self.assertIn(f"class {class_name}", header_text)
        if accepts_audio:
            self.assertIn("public IAudioProcessor", header_text)
            self.assertIn("AcceptsAudio() { return true; }", header_text)
        else:
            self.assertIn("AcceptsAudio() { return false; }", header_text)
        self.assertIn("public IModulator", header_text)
        self.assertIn(f'GetTypeName() {{ return "{registered_id}"; }}', header_text)

        for control in controls:
            self.assertIn(f'"{control}"', source_text)

        self.assertIn(f"REGISTER({class_name}, {registered_id}", factory_text)
        self.assertIn(f"{class_name}.cpp", cmake_text)

    def test_native_decay_amount_module_is_registered_and_built(self):
        self.assert_native_module(
            "DecayAmount",
            "decayamount",
            ("follow", "window", "threshold", "gain", "decay"),
            accepts_audio=True,
        )

    def test_native_density_amount_module_is_registered_and_built(self):
        self.assert_native_module(
            "DensityAmount",
            "densityamount",
            ("input", "threshold", "window", "full count"),
            accepts_audio=False,
        )


if __name__ == "__main__":
    unittest.main()
