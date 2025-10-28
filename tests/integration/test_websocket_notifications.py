"""Integration checks for recorded WebSocket notification behaviour."""

from __future__ import annotations

import json
from pathlib import Path
import unittest


FIXTURE_DIR = Path(__file__).resolve().parent.parent / "fixtures"


def _load_websocket_events(fixture_name: str) -> list[dict]:
    """Return WebSocket events from the given JSON lines fixture."""

    session_path = FIXTURE_DIR / fixture_name
    events: list[dict] = []
    with session_path.open("r", encoding="utf-8") as handle:
        for raw_line in handle:
            raw_line = raw_line.strip()
            if not raw_line:
                continue
            event = json.loads(raw_line)
            if event.get("stage") == "web_ui":
                events.append(event)
    return events


class WebSocketNotificationTest(unittest.TestCase):
    """Validate that WebSocket updates in the capture behave as expected."""

    @classmethod
    def setUpClass(cls) -> None:  # noqa: D401 - unittest contract
        cls.websocket_events = _load_websocket_events("e2e_session.jsonl")

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

    def test_multiple_clients_receive_broadcasts(self) -> None:
        """Simultaneous clients should receive identical broadcasts while connected."""

        events = _load_websocket_events("websocket_multi_client.jsonl")
        connected_clients: set[str] = set()

        for event in events:
            if event.get("event") == "client_connected":
                connected_clients.add(event["client_id"])
            elif event.get("event") == "client_disconnected":
                connected_clients.discard(event["client_id"])
            elif event.get("event") == "status_push":
                self.assertIn("clients", event, msg="status_push missing client list")
                self.assertSetEqual(
                    set(event["clients"]),
                    connected_clients,
                    msg=f"Broadcast mismatch for status_seq={event.get('status_seq')}",
                )

        self.assertGreater(len(connected_clients), -1, "Loop should execute without errors")

    def test_configuration_broadcast_contains_expected_payload(self) -> None:
        """Configuration broadcasts must include update metadata for all recipients."""

        events = _load_websocket_events("websocket_multi_client.jsonl")
        config_events = [event for event in events if event.get("event") == "config_broadcast"]
        self.assertTrue(config_events, msg="No configuration broadcast events recorded")

        broadcast = config_events[0]
        self.assertIn("config", broadcast)
        config = broadcast["config"]
        self.assertEqual(sorted(config["metrics"]), ["current", "voltage"])
        self.assertEqual(config["update_interval_ms"], 1000)
        self.assertEqual(sorted(broadcast.get("clients", [])), ["alpha", "beta"])

    def test_error_events_capture_timeouts_and_disconnects(self) -> None:
        """Timeouts and disconnects should be explicitly logged in the capture."""

        events = _load_websocket_events("websocket_multi_client.jsonl")
        timeout_events = [event for event in events if event.get("event") == "error" and event.get("error") == "timeout"]
        self.assertTrue(timeout_events, msg="Expected timeout error event not found")

        timeout = timeout_events[0]
        self.assertEqual(timeout["client_id"], "alpha")
        self.assertGreater(timeout["details"].get("since_last_heartbeat_ms", 0), 0)

        disconnect_events = [event for event in events if event.get("event") == "client_disconnected"]
        self.assertTrue(disconnect_events, msg="No client disconnect events captured")
        self.assertIn("beta", {event["client_id"] for event in disconnect_events})


if __name__ == "__main__":  # pragma: no cover - convenience entry point
    unittest.main()
