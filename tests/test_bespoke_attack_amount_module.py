import unittest
from pathlib import Path


BESPOKE_SOURCE = Path("/tmp/BespokeSynth/Source")


class BespokeAttackAmountModuleTest(unittest.TestCase):
    def test_native_attack_amount_module_is_registered_and_built(self):
        header = BESPOKE_SOURCE / "AttackAmount.h"
        source = BESPOKE_SOURCE / "AttackAmount.cpp"
        factory = BESPOKE_SOURCE / "ModuleFactory.cpp"
        cmake = BESPOKE_SOURCE / "CMakeLists.txt"

        self.assertTrue(header.exists(), "AttackAmount.h should define the native module contract")
        self.assertTrue(source.exists(), "AttackAmount.cpp should implement the native module")

        header_text = header.read_text()
        source_text = source.read_text()
        factory_text = factory.read_text()
        cmake_text = cmake.read_text()

        self.assertIn("class AttackAmount", header_text)
        self.assertIn("public IAudioProcessor", header_text)
        self.assertIn("public IModulator", header_text)
        self.assertIn('GetTypeName() { return "attackamount"; }', header_text)

        for control in ("window", "threshold", "gain", "decay"):
            self.assertIn(f'"{control}"', source_text)

        self.assertIn("REGISTER(AttackAmount, attackamount", factory_text)
        self.assertIn("AttackAmount.cpp", cmake_text)


if __name__ == "__main__":
    unittest.main()
