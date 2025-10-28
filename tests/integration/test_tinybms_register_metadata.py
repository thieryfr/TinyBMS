from __future__ import annotations

import json
from pathlib import Path
import unittest


REPO_ROOT = Path(__file__).resolve().parent.parent.parent
MAPPING_PATH = REPO_ROOT / "data" / "tiny_rw_bms.json"
EDITOR_SOURCE = REPO_ROOT / "src" / "tinybms_config_editor.cpp"


class TinyBmsRegisterMetadataTest(unittest.TestCase):
    """Validate the enriched RW register metadata used by the API."""

    @classmethod
    def setUpClass(cls) -> None:  # noqa: D401 - unittest contract
        with MAPPING_PATH.open("r", encoding="utf-8") as handle:
            cls.mapping = json.load(handle)
        cls.registers: dict[str, dict] = cls.mapping.get("tiny_rw_registers", {})

    def test_mapping_contains_expected_sections(self) -> None:
        self.assertIn("tiny_rw_registers", self.mapping)
        self.assertGreater(len(self.registers), 0, "Register catalog should not be empty")

    def test_keys_are_unique_and_populated(self) -> None:
        seen_keys: set[str] = set()
        for addr, entry in self.registers.items():
            with self.subTest(address=addr):
                self.assertTrue(entry.get("key"), f"Register {addr} missing key")
                self.assertTrue(entry.get("label"), f"Register {addr} missing label")
                key = entry["key"]
                self.assertNotIn(key, seen_keys, f"Duplicate key detected: {key}")
                seen_keys.add(key)

    def test_min_max_and_defaults_are_consistent(self) -> None:
        for addr, entry in self.registers.items():
            with self.subTest(address=addr):
                scale = float(entry.get("scale", 1.0))
                offset = float(entry.get("offset", 0.0))
                if "min" in entry and "max" in entry:
                    min_raw = float(entry["min"])
                    max_raw = float(entry["max"])
                    self.assertLessEqual(min_raw, max_raw, f"min > max for register {addr}")
                    min_user = min_raw * scale + offset
                    max_user = max_raw * scale + offset
                    self.assertLessEqual(min_user, max_user + 1e-6, f"User min > max for register {addr}")
                if "default" in entry and "min" in entry and "max" in entry:
                    default_raw = float(entry["default"])
                    min_raw = float(entry["min"])
                    max_raw = float(entry["max"])
                    self.assertGreaterEqual(default_raw, min_raw - 1e-6,
                                             f"Default below min for register {addr}")
                    self.assertLessEqual(default_raw, max_raw + 1e-6,
                                         f"Default above max for register {addr}")

    def test_enum_registers_have_labels(self) -> None:
        enum_registers = [entry for entry in self.registers.values() if "enum" in entry]
        self.assertTrue(enum_registers, "Expected at least one enum register in mapping")
        for entry in enum_registers:
            with self.subTest(key=entry.get("key")):
                options = entry.get("enum", [])
                self.assertTrue(options, f"Enum register {entry.get('key')} missing options")
                for option in options:
                    self.assertIn("value", option)
                    self.assertTrue(option.get("label"), "Enum option missing label")

    def test_known_register_metadata(self) -> None:
        broadcast = self.registers.get("342")
        self.assertIsNotNone(broadcast, "Register 342 missing")
        self.assertEqual(broadcast["group"], "system")
        self.assertEqual(broadcast["type"], "enum")
        self.assertIn("enum", broadcast)
        self.assertGreaterEqual(len(broadcast["enum"]), 2)

        safety_cutoff = self.registers.get("320")
        self.assertIsNotNone(safety_cutoff, "Register 320 missing")
        self.assertEqual(safety_cutoff["group"], "safety")
        self.assertIn("min", safety_cutoff)
        self.assertIn("max", safety_cutoff)
        self.assertLessEqual(float(safety_cutoff["min"]), float(safety_cutoff["max"]))

    def test_write_log_message_present(self) -> None:
        source = EDITOR_SOURCE.read_text(encoding="utf-8")
        self.assertIn("Write OK → Reg", source,
                      "UART write confirmation log message missing from TinyBMSConfigEditor")


if __name__ == "__main__":  # pragma: no cover - convenience entry point
    unittest.main()
