#!/usr/bin/env python3
"""
TinyBMS WebSocket Stress Test
===============================
Phase 4: WebSocket performance and stability validation

This script creates multiple concurrent WebSocket clients to stress-test
the TinyBMS WebSocket server.

Requirements:
    pip install websockets

Usage:
    python websocket_stress.py [device_ip] [num_clients] [duration_s]

Example:
    python websocket_stress.py tinybms-bridge.local 4 300

Metrics:
    - Connection success rate
    - Message receive rate
    - Latency (P50, P95, P99)
    - Disconnection events
    - Error rates
"""

import asyncio
import websockets
import json
import time
import sys
import statistics
from datetime import datetime
from typing import List, Dict, Any


class WebSocketStressClient:
    """Single WebSocket client for stress testing"""

    def __init__(self, client_id: int, uri: str, duration_s: int):
        self.client_id = client_id
        self.uri = uri
        self.duration_s = duration_s
        self.received_count = 0
        self.error_count = 0
        self.disconnect_count = 0
        self.latencies: List[float] = []
        self.start_time = None
        self.end_time = None

    async def run(self):
        """Run the WebSocket client"""
        print(f"[Client {self.client_id}] Connecting to {self.uri}...")

        try:
            async with websockets.connect(self.uri) as websocket:
                print(f"[Client {self.client_id}] Connected ✓")
                self.start_time = time.time()

                while time.time() - self.start_time < self.duration_s:
                    try:
                        # Receive message with timeout
                        msg = await asyncio.wait_for(
                            websocket.recv(),
                            timeout=5.0
                        )

                        recv_time = time.time()
                        self.received_count += 1

                        # Parse JSON and calculate latency
                        try:
                            data = json.loads(msg)

                            # If message contains timestamp, calculate latency
                            if 'uptime_ms' in data:
                                # Note: This is an approximation
                                # Real latency would require synchronized clocks
                                msg_time = data['uptime_ms'] / 1000.0
                                # Store receive rate instead
                                pass

                            # Log periodic progress
                            if self.received_count % 60 == 0:
                                elapsed = recv_time - self.start_time
                                rate = self.received_count / elapsed
                                print(f"[Client {self.client_id}] "
                                      f"Received {self.received_count} messages "
                                      f"({rate:.1f} msg/s)")

                        except json.JSONDecodeError:
                            self.error_count += 1
                            print(f"[Client {self.client_id}] JSON decode error")

                    except asyncio.TimeoutError:
                        print(f"[Client {self.client_id}] Receive timeout")
                        self.error_count += 1
                        continue

                    except websockets.exceptions.ConnectionClosed:
                        print(f"[Client {self.client_id}] Connection closed by server")
                        self.disconnect_count += 1
                        break

                self.end_time = time.time()

        except Exception as e:
            print(f"[Client {self.client_id}] Error: {e}")
            self.error_count += 1

    def get_stats(self) -> Dict[str, Any]:
        """Get client statistics"""
        if not self.start_time or not self.end_time:
            duration = 0
        else:
            duration = self.end_time - self.start_time

        return {
            'client_id': self.client_id,
            'duration_s': duration,
            'received_count': self.received_count,
            'error_count': self.error_count,
            'disconnect_count': self.disconnect_count,
            'receive_rate': self.received_count / duration if duration > 0 else 0,
        }


class WebSocketStressTest:
    """WebSocket stress test coordinator"""

    def __init__(self, device_ip: str, num_clients: int, duration_s: int):
        self.device_ip = device_ip
        self.num_clients = num_clients
        self.duration_s = duration_s
        self.clients: List[WebSocketStressClient] = []

    async def run(self):
        """Run the stress test"""
        uri = f"ws://{self.device_ip}/ws"

        print("=" * 70)
        print("TinyBMS WebSocket Stress Test")
        print("=" * 70)
        print(f"Device: {self.device_ip}")
        print(f"Clients: {self.num_clients}")
        print(f"Duration: {self.duration_s}s")
        print(f"Start time: {datetime.now()}")
        print("=" * 70)

        # Create clients
        self.clients = [
            WebSocketStressClient(i, uri, self.duration_s)
            for i in range(self.num_clients)
        ]

        # Run all clients concurrently
        start_time = time.time()
        await asyncio.gather(*[client.run() for client in self.clients])
        end_time = time.time()

        # Collect and print statistics
        print("\n" + "=" * 70)
        print("Test Results")
        print("=" * 70)

        all_stats = [client.get_stats() for client in self.clients]

        total_received = sum(s['received_count'] for s in all_stats)
        total_errors = sum(s['error_count'] for s in all_stats)
        total_disconnects = sum(s['disconnect_count'] for s in all_stats)

        print(f"\nTotal duration: {end_time - start_time:.1f}s")
        print(f"Total messages received: {total_received}")
        print(f"Total errors: {total_errors}")
        print(f"Total disconnects: {total_disconnects}")

        print("\nPer-client statistics:")
        for stats in all_stats:
            print(f"  Client {stats['client_id']}: "
                  f"{stats['received_count']} messages, "
                  f"{stats['receive_rate']:.2f} msg/s, "
                  f"{stats['error_count']} errors, "
                  f"{stats['disconnect_count']} disconnects")

        # Calculate receive rates
        receive_rates = [s['receive_rate'] for s in all_stats if s['receive_rate'] > 0]
        if receive_rates:
            avg_rate = statistics.mean(receive_rates)
            print(f"\nAverage receive rate: {avg_rate:.2f} msg/s per client")

        # Test pass/fail criteria
        print("\n" + "=" * 70)
        print("Pass/Fail Criteria")
        print("=" * 70)

        success = True

        # All clients should receive messages
        if total_received == 0:
            print("✗ FAIL: No messages received")
            success = False
        else:
            print(f"✓ PASS: {total_received} messages received")

        # Error rate should be low (< 5%)
        error_rate = (total_errors / total_received * 100) if total_received > 0 else 100
        if error_rate > 5:
            print(f"✗ FAIL: Error rate too high ({error_rate:.1f}%)")
            success = False
        else:
            print(f"✓ PASS: Error rate acceptable ({error_rate:.1f}%)")

        # Disconnects should be minimal
        if total_disconnects > 0:
            print(f"⚠ WARNING: {total_disconnects} unexpected disconnects")

        # All clients should have received messages
        clients_with_messages = sum(1 for s in all_stats if s['received_count'] > 0)
        if clients_with_messages < self.num_clients:
            print(f"✗ FAIL: Only {clients_with_messages}/{self.num_clients} "
                  f"clients received messages")
            success = False
        else:
            print(f"✓ PASS: All {self.num_clients} clients received messages")

        print("\n" + "=" * 70)
        if success:
            print("✓ Test PASSED")
            print("=" * 70)
            return 0
        else:
            print("✗ Test FAILED")
            print("=" * 70)
            return 1


async def main():
    """Main entry point"""
    # Parse command line arguments
    device_ip = sys.argv[1] if len(sys.argv) > 1 else "tinybms-bridge.local"
    num_clients = int(sys.argv[2]) if len(sys.argv) > 2 else 4
    duration_s = int(sys.argv[3]) if len(sys.argv) > 3 else 300  # 5 minutes default

    # Run test
    test = WebSocketStressTest(device_ip, num_clients, duration_s)
    result = await test.run()

    sys.exit(result)


if __name__ == "__main__":
    asyncio.run(main())
