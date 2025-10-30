"""Integration checks for the TinyBMS data pipeline.

These tests operate on recorded fixtures to validate that the UART → EventBus →
CAN → Web/UI chain remains stable over time.  The goal is to catch regressions
in counter updates, keep-alive handling and alarm propagation without requiring
hardware in the CI environment.
"""

from __future__ import annotations

import json
from pathlib import Path
import statistics
import unittest


FIXTURE_DIR = Path(__file__).resolve().parent.parent / "fixtures"


class EndToEndFlowTest(unittest.TestCase):
    """Validate the recorded end-to-end session."""

    @classmethod
    def setUpClass(cls) -> None:  # noqa: D401 - setUpClass contract
        cls.events = []
        session_path = FIXTURE_DIR / "e2e_session.jsonl"
        with session_path.open("r", encoding="utf-8") as handle:
            for line in handle:
                line = line.strip()
                if not line:
                    continue
                cls.events.append(json.loads(line))

        status_path = FIXTURE_DIR / "status_snapshot.json"
        with status_path.open("r", encoding="utf-8") as handle:
            cls.status_snapshot = json.load(handle)

    def test_stage_progression_is_ordered(self) -> None:
        """Ensure that all stages appear in order and timestamps are monotonic."""
        required = ["uart", "event_bus", "can", "web_ui"]
        seen = {stage: False for stage in required}
        last_ts = -1

        for event in self.events:
            timestamp = event["timestamp_ms"]
            self.assertGreaterEqual(
                timestamp,
                last_ts,
                msg=f"Timestamps must be monotonic: {timestamp} < {last_ts}",
            )
            last_ts = timestamp

            stage = event["stage"]
            self.assertIn(stage, required)
            seen[stage] = True

            idx = required.index(stage)
            if idx > 0:
                self.assertTrue(
                    seen[required[idx - 1]],
                    msg=f"Stage {stage} occurred before {required[idx - 1]}",
                )

        for stage, present in seen.items():
            self.assertTrue(present, msg=f"Stage {stage} never appeared in trace")

    def test_can_counters_monotonic(self) -> None:
        """Verify that CAN TX counters never go backwards in the capture."""
        tx_counts = []
        for event in self.events:
            if event["stage"] == "can" and event.get("metrics"):
                metrics = event["metrics"]
                if "can_tx_count" in metrics:
                    tx_counts.append(metrics["can_tx_count"])

        self.assertTrue(tx_counts, msg="No CAN counters found in trace")
        for older, newer in zip(tx_counts, tx_counts[1:]):
            self.assertGreaterEqual(
                newer,
                older,
                msg=f"CAN TX counter regressed from {older} to {newer}",
            )

    def test_keepalive_interval_stability(self) -> None:
        """Keep-alive receptions should stay within a tight interval window."""
        keepalive_times = [
            event["timestamp_ms"]
            for event in self.events
            if event["stage"] == "can" and event["event"] == "keepalive_rx"
        ]
        self.assertGreaterEqual(len(keepalive_times), 3, "Need >=3 keepalive events")

        intervals = [b - a for a, b in zip(keepalive_times, keepalive_times[1:])]
        spread = max(intervals) - min(intervals)
        self.assertLessEqual(
            spread,
            100,
            msg=f"Keepalive interval spread too large: {intervals}",
        )
        self.assertAlmostEqual(
            statistics.mean(intervals),
            998,
            delta=100,
            msg=f"Keepalive interval mean drifted: {intervals}",
        )

    def test_status_snapshot_contains_new_sections(self) -> None:
        """The status JSON must expose detailed counters for diagnostics."""
        stats = self.status_snapshot["stats"]

        self.assertIn("can", stats)
        self.assertIn("uart", stats)
        self.assertIn("keepalive", stats)
        self.assertIn("event_bus", stats)
        self.assertIn("mqtt", stats)

        mqtt_stats = stats["mqtt"]
        for key in ("enabled", "configured", "connected", "publish_count"):
            self.assertIn(key, mqtt_stats)

        can_stats = stats["can"]
        self.assertIn("tx_success", can_stats)
        self.assertIn("rx_success", can_stats)
        self.assertIn("tx_errors", can_stats)
        self.assertIn("rx_dropped", can_stats)

        keepalive = stats["keepalive"]
        for key in ("ok", "last_tx_ms", "last_rx_ms", "since_last_rx_ms"):
            self.assertIn(key, keepalive)

        live = self.status_snapshot["live_data"]
        self.assertIn("pack_power_w", live)

        victron = self.status_snapshot.get("victron", {})
        self.assertIn("system_state_code", victron)
        self.assertIn("system_state_name", victron)
        self.assertIn("pack_power_w", victron)
        self.assertIn("system_state_raw", victron)

    def test_alarm_snapshot_includes_metadata(self) -> None:
        """Alarms array must include severity information for UI consumption."""
        alarms = self.status_snapshot.get("alarms", [])
        self.assertGreaterEqual(len(alarms), 1, "Alarms array empty in snapshot")

        for alarm in alarms:
            self.assertIn("event", alarm)
            self.assertIn("severity", alarm)
            self.assertIn("severity_name", alarm)
            self.assertIn("active", alarm)
            self.assertIn("timestamp_ms", alarm)
            self.assertIn("victron_bit", alarm)
            self.assertIn("victron_level", alarm)
            self.assertIn("victron_path", alarm)

        # alarms_active should reflect whether the latest alarm is still active
        active_flags = [alarm["active"] for alarm in alarms if alarm["event"] == "raised"]
        if active_flags:
            self.assertEqual(
                self.status_snapshot.get("alarms_active"),
                active_flags[-1],
                msg="alarms_active flag inconsistent with raised alarm state",
            )


if __name__ == "__main__":  # pragma: no cover - convenience entry point
    unittest.main()
