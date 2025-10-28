"""Integration checks for recorded WebSocket notification behaviour."""

from __future__ import annotations

import json
from pathlib import Path
import unittest


FIXTURE_DIR = Path(__file__).resolve().parent.parent / "fixtures"


class WebSocketNotificationTest(unittest.TestCase):
    """Validate that WebSocket updates in the capture behave as expected."""

    @classmethod
    def setUpClass(cls) -> None:  # noqa: D401 - unittest contract
        session_path = FIXTURE_DIR / "e2e_session.jsonl"
        cls.websocket_events = []
        with session_path.open("r", encoding="utf-8") as handle:
            for line in handle:
                line = line.strip()
                if not line:
                    continue
                event = json.loads(line)
                if event.get("stage") == "web_ui":
                    cls.websocket_events.append(event)

    def test_status_push_events_present(self) -> None:
        """Captured session must contain WebSocket status pushes."""
        self.assertGreaterEqual(
            len(self.websocket_events),
            1,
            msg="No WebSocket events found in capture",
        )
        first = self.websocket_events[0]
        fields = first.get("fields", [])
        self.assertIn("live_data", fields)
        self.assertIn("stats", fields)
        self.assertEqual(first.get("event"), "status_push")

    def test_status_sequence_monotonic(self) -> None:
        """status_seq should increase monotonically for WebSocket pushes."""
        sequences = [event.get("status_seq") for event in self.websocket_events if "status_seq" in event]
        self.assertTrue(sequences, msg="WebSocket events missing status_seq")
        for older, newer in zip(sequences, sequences[1:]):
            self.assertLess(
                older,
                newer,
                msg=f"status_seq regressed from {older} to {newer}",
            )

    def test_update_interval_matches_configuration(self) -> None:
        """Successive WebSocket pushes should respect the ~1s update interval."""
        timestamps = [event["timestamp_ms"] for event in self.websocket_events]
        self.assertGreaterEqual(len(timestamps), 2, "Need >=2 WebSocket updates to validate interval")

        deltas = [b - a for a, b in zip(timestamps, timestamps[1:])]
        for delta in deltas:
            self.assertGreaterEqual(delta, 800, msg=f"WebSocket interval too short: {delta}ms")
            self.assertLessEqual(delta, 1200, msg=f"WebSocket interval too long: {delta}ms")


if __name__ == "__main__":  # pragma: no cover - convenience entry point
    unittest.main()
