"""Regression and functional validation for MQTT topic stability."""

from __future__ import annotations

import json
from pathlib import Path
import unittest


FIXTURE_DIR = Path(__file__).resolve().parent.parent / "fixtures"
TOPIC_SNAPSHOT_PATH = FIXTURE_DIR / "mqtt_topics_snapshot.json"
VENUS_VALIDATION_PATH = FIXTURE_DIR / "venus_os_validation.json"


class MqttTopicRegressionTest(unittest.TestCase):
    """Ensure the recorded MQTT topic catalogue keeps backward compatibility."""

    @classmethod
    def setUpClass(cls) -> None:  # noqa: D401 - unittest contract
        with TOPIC_SNAPSHOT_PATH.open("r", encoding="utf-8") as handle:
            cls.snapshot = json.load(handle)

    def test_root_topic_is_stable(self) -> None:
        """The MQTT root topic must remain unchanged between releases."""
        before = self.snapshot["before"]["root_topic"]
        after = self.snapshot["after"]["root_topic"]
        self.assertEqual(before, after, "Root topic changed and would break subscribers")

    def test_legacy_topics_are_preserved(self) -> None:
        """All historical topics must still be published after the update."""
        before_topics = set(self.snapshot["before"]["topics"])
        after_topics = set(self.snapshot["after"]["topics"])

        missing = sorted(before_topics - after_topics)
        self.assertFalse(
            missing,
            msg=f"Legacy topics disappeared: {missing}",
        )

    def test_new_topics_are_explicitly_tracked(self) -> None:
        """The delta between snapshots must match the declared new topics."""
        before_topics = set(self.snapshot["before"]["topics"])
        after_topics = set(self.snapshot["after"]["topics"])
        declared_new = set(self.snapshot["after"].get("new_topics", []))

        actual_new = after_topics - before_topics
        self.assertEqual(
            actual_new,
            declared_new,
            msg=f"Unexpected topic delta: expected {declared_new}, found {actual_new}",
        )


class VenusOsFunctionalSnapshotTest(unittest.TestCase):
    """Validate the recorded Venus OS / ESS functional run."""

    @classmethod
    def setUpClass(cls) -> None:  # noqa: D401 - unittest contract
        with VENUS_VALIDATION_PATH.open("r", encoding="utf-8") as handle:
            cls.venus = json.load(handle)
        with TOPIC_SNAPSHOT_PATH.open("r", encoding="utf-8") as handle:
            cls.snapshot = json.load(handle)

    def test_functional_run_passed(self) -> None:
        """The recorded Venus OS session must have passed without regressions."""
        self.assertEqual(
            self.venus["result"],
            "pass",
            msg="Venus OS validation reported a failure",
        )

    def test_root_topic_matches_snapshot(self) -> None:
        """Functional run must use the same root topic as the regression snapshot."""
        expected_root = self.snapshot["after"]["root_topic"]
        self.assertEqual(self.venus["root_topic"], expected_root)

    def test_legacy_topics_seen_on_venus(self) -> None:
        """Functional capture should confirm all legacy topics were observed."""
        before_topics = set(self.snapshot["before"]["topics"])
        validated = set(self.venus.get("legacy_topics_verified", []))
        missing = sorted(before_topics - validated)
        self.assertFalse(
            missing,
            msg=f"Venus run missed legacy topics: {missing}",
        )

    def test_new_topics_have_expected_dbus_paths(self) -> None:
        """Pack power and system state should map to the expected DBus hierarchy."""
        matrix = self.venus.get("functional_matrix", {})
        expectations = {
            "pack_power_w": "/Dc/0/Power",
            "system_state": "/System/0/State",
        }

        for topic, expected_path in expectations.items():
            self.assertIn(topic, matrix, msg=f"Missing matrix entry for {topic}")
            entry = matrix[topic]
            self.assertEqual(entry.get("status"), "ok", f"Functional test failed for {topic}")
            self.assertEqual(entry.get("dbus_path"), expected_path)
            samples = entry.get("payload_samples", [])
            self.assertGreaterEqual(
                len(samples),
                2,
                msg=f"Not enough samples captured for {topic}",
            )


if __name__ == "__main__":  # pragma: no cover - convenience entry point
    unittest.main()
