#!/usr/bin/env python3
"""
E-Slate BLE Protocol — Empirical Adversarial Stress & Vulnerability Test Suite
=============================================================================
Author: challenger_m1_1 (Empirical Challenger)
Target: docs/BLE_SPEC.md and tests/test_ble_protocol.py

Tests adversarial failure modes:
1. Malformed chunks (short headers, length mismatches, total=0 zombie state, oversized slices).
2. Out-of-order packets & duplicate frames.
3. Guard timer expiration & new-transmission collision on timeout.
4. Cryptographic attacks (tampered nonces, wrong keys, replay attacks, lockout persistence).
5. Schema edge cases & type coercion crashes (integer fields in JSON, alphanumeric roll suffixes).
6. Telemetry packer boundaries (ADC overflow, invalid opcodes).
"""

import hashlib
import hmac
import json
import os
import struct
import sys
import time
import unittest

# Ensure repository root is on sys.path
REPO_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
if REPO_ROOT not in sys.path:
    sys.path.insert(0, REPO_ROOT)

from tests.test_ble_protocol import (
    BatteryTelemetryPacker,
    BLEPacketChunker,
    BLEReassemblyEngine,
    ButtonTelemetryPacker,
    ClapperboardState,
    CommandProcessor,
    ConnectionLockManager,
    SecurityAuthManager,
)


class TestBLEAdversarialMalformedChunks(unittest.TestCase):
    """Stress tests for malformed, truncated, and corrupt chunk headers and payloads."""

    def setUp(self):
        self.engine = BLEReassemblyEngine()

    def test_chunk_header_too_short(self):
        """Headers shorter than 3 bytes must be rejected cleanly without unhandled exception."""
        for short_data in [b"", b"\x00", b"\x00\x01"]:
            status, result = self.engine.feed_chunk(short_data)
            self.assertEqual(status, "ERROR_HEADER_TOO_SHORT")
            self.assertIsNone(result)
            self.assertFalse(self.engine.in_progress)

    def test_chunk_length_mismatch_too_short(self):
        """Header claims 10 bytes payload, but only 5 provided."""
        corrupt_chunk = bytes([0, 1, 10]) + b"12345"
        status, result = self.engine.feed_chunk(corrupt_chunk)
        self.assertEqual(status, "ERROR_LENGTH_MISMATCH")
        self.assertIsNone(result)
        self.assertFalse(self.engine.in_progress)

    def test_chunk_length_mismatch_too_long(self):
        """Header claims 5 bytes payload, but 10 provided (trailing garbage)."""
        corrupt_chunk = bytes([0, 1, 5]) + b"1234567890"
        status, result = self.engine.feed_chunk(corrupt_chunk)
        self.assertEqual(status, "ERROR_LENGTH_MISMATCH")
        self.assertIsNone(result)
        self.assertFalse(self.engine.in_progress)

    def test_chunk_total_zero_rejection(self):
        """
        Total Chunks = 0 (violating BLE_SPEC N >= 1) must be rejected with ERROR_INVALID_HEADER
        without entering unrecoverable zombie state.
        """
        status, result = self.engine.feed_chunk(bytes([0, 0, 0]))
        self.assertEqual(status, "ERROR_INVALID_HEADER")
        self.assertIsNone(result)
        self.assertFalse(self.engine.in_progress)

    def test_empty_payload_handling(self):
        """Valid empty payload chunk [0, 1, 0] produces empty byte result."""
        empty_chunk = bytes([0, 1, 0])
        status, result = self.engine.feed_chunk(empty_chunk)
        self.assertEqual(status, "COMPLETE")
        self.assertEqual(result, b"")

    def test_clapperboard_state_empty_payload_json_decode_failure(self):
        """ClapperboardState.from_utf8_bytes(b'') raises JSONDecodeError on empty reassembled payload."""
        with self.assertRaises(json.decoder.JSONDecodeError):
            ClapperboardState.from_utf8_bytes(b"")

    def test_clapperboard_state_integer_type_coercion(self):
        """
        If JSON payload contains integer values for text fields (e.g. {'take': 2, 'roll': 101}),
        normalize_text cleanly coerces them to uppercase strings without AttributeError.
        """
        state = ClapperboardState.from_dict({"take": 2, "roll": 101})
        self.assertEqual(state.take, "2")
        self.assertEqual(state.roll, "101")


class TestBLEAdversarialOutOfOrderAndFuzzing(unittest.TestCase):
    """Stress tests for packet misordering, replay, and recovery."""

    def setUp(self):
        self.engine = BLEReassemblyEngine()
        self.payload = b'{"roll":"A001","scene":"1A","take":"1","prod":"NEBRALTA"}'
        self.chunks = BLEPacketChunker.chunk_payload(self.payload, mtu=23)

    def test_first_chunk_not_seq_zero(self):
        """Feeding chunk 1 when expecting chunk 0 must return ERROR_OUT_OF_ORDER."""
        status, result = self.engine.feed_chunk(self.chunks[1])
        self.assertEqual(status, "ERROR_OUT_OF_ORDER")
        self.assertIsNone(result)
        self.assertFalse(self.engine.in_progress)

    def test_duplicate_chunk_zero(self):
        """Feeding chunk 0 twice in succession must trigger ERROR_OUT_OF_ORDER and reset buffer."""
        status0, _ = self.engine.feed_chunk(self.chunks[0])
        self.assertEqual(status0, "IN_PROGRESS")
        status_dup, result = self.engine.feed_chunk(self.chunks[0])
        self.assertEqual(status_dup, "ERROR_OUT_OF_ORDER")
        self.assertIsNone(result)
        self.assertFalse(self.engine.in_progress)

    def test_skipped_chunk_mid_transmission(self):
        """Chunk 0 followed immediately by Chunk 2 (skipping Chunk 1) must abort with ERROR_OUT_OF_ORDER."""
        self.assertGreater(len(self.chunks), 2)
        self.engine.feed_chunk(self.chunks[0])
        status, result = self.engine.feed_chunk(self.chunks[2])
        self.assertEqual(status, "ERROR_OUT_OF_ORDER")
        self.assertIsNone(result)
        self.assertFalse(self.engine.in_progress)

    def test_recovery_after_rejection(self):
        """After an out-of-order rejection, a subsequent fresh transmission from Chunk 0 must succeed."""
        # Cause rejection
        self.engine.feed_chunk(self.chunks[1])
        self.assertFalse(self.engine.in_progress)

        # Fresh valid transmission
        result = None
        for chunk in self.chunks:
            status, result = self.engine.feed_chunk(chunk)
        self.assertEqual(status, "COMPLETE")
        self.assertEqual(result, self.payload)

    def test_tampered_total_chunks_mid_stream(self):
        """Chunk 0 has total=3, but Chunk 1 tampered to total=4 -> must reject."""
        c0 = self.chunks[0]
        c1_tampered = bytes([1, c0[1] + 1, self.chunks[1][2]]) + self.chunks[1][3:]
        self.engine.feed_chunk(c0)
        status, result = self.engine.feed_chunk(c1_tampered)
        self.assertEqual(status, "ERROR_OUT_OF_ORDER")
        self.assertIsNone(result)


class TestBLEAdversarialGuardTimer(unittest.TestCase):
    """Stress tests on reassembly guard timer and timeout race conditions."""

    def setUp(self):
        self.engine = BLEReassemblyEngine()
        self.payload = b"TESTING_GUARD_TIMER_EXPIRATION_PAYLOAD"
        self.chunks = BLEPacketChunker.chunk_payload(self.payload, mtu=23)

    def test_guard_timer_within_window_succeeds(self):
        """Chunks received at 2.9s interval (under 3.0s timeout) must complete successfully."""
        t0 = 1000.0
        self.engine.feed_chunk(self.chunks[0], current_time=t0)
        status, result = self.engine.feed_chunk(self.chunks[1], current_time=t0 + 2.9)
        self.assertIn(status, ["IN_PROGRESS", "COMPLETE"])

    def test_guard_timer_exceeded_fails(self):
        """Chunk received at 3.1s interval must fail with ERROR_TIMEOUT."""
        t0 = 1000.0
        self.engine.feed_chunk(self.chunks[0], current_time=t0)
        status, result = self.engine.feed_chunk(self.chunks[1], current_time=t0 + 3.1)
        self.assertEqual(status, "ERROR_TIMEOUT")
        self.assertIsNone(result)
        self.assertFalse(self.engine.in_progress)

    def test_new_transmission_after_timeout_starts_cleanly(self):
        """
        If Transmission A sends Chunk 0 at t=0 and halts, then 10 seconds later Transmission B
        sends its Chunk 0, the expired state is wiped and Transmission B starts cleanly.
        """
        # Transmission A: chunk 0 sent at t=0, never finished
        s_a, _ = self.engine.feed_chunk(self.chunks[0], current_time=0.0)
        self.assertEqual(s_a, "IN_PROGRESS")

        # 10 seconds later: Transmission B starts with Chunk 0
        s_b0, r_b0 = self.engine.feed_chunk(self.chunks[0], current_time=10.0)
        self.assertEqual(s_b0, "IN_PROGRESS")
        self.assertIsNone(r_b0)

        # Transmission B continues through remaining chunks
        for i in range(1, len(self.chunks)):
            status, res = self.engine.feed_chunk(self.chunks[i], current_time=10.0 + i * 0.1)
            if i == len(self.chunks) - 1:
                self.assertEqual(status, "COMPLETE")
                self.assertEqual(res, self.payload)
            else:
                self.assertEqual(status, "IN_PROGRESS")


class TestBLEAdversarialCryptographicSecurity(unittest.TestCase):
    """Cryptographic attack tests: tampered nonces, wrong keys, replay attacks, brute-force lockout."""

    def setUp(self):
        self.shared_secret = "123456789P"
        self.auth = SecurityAuthManager(self.shared_secret)

    def test_tampered_nonce_in_client_hmac(self):
        """Attacker computes HMAC over a modified nonce -> must reject with AUTH_FAILED."""
        challenge = self.auth.generate_challenge()
        real_nonce = challenge[1:17]
        tampered_nonce = bytearray(real_nonce)
        tampered_nonce[0] ^= 0xFF  # Flip bits in first byte

        tampered_sig = hmac.new(self.shared_secret.encode("utf-8"), bytes(tampered_nonce), hashlib.sha256).digest()
        tampered_response = bytes([0x02]) + tampered_sig

        status_code, notify = self.auth.verify_response(tampered_response)
        self.assertEqual(status_code, 0x01)  # AUTH_FAILED
        self.assertFalse(self.auth.session_authenticated)

    def test_single_bit_flip_in_hmac_signature(self):
        """Attacker modifies 1 bit of genuine signature -> constant-time check must reject."""
        challenge = self.auth.generate_challenge()
        valid_response = bytearray(self.auth.compute_client_response(challenge))
        valid_response[15] ^= 0x01  # Flip 1 bit in signature

        status_code, notify = self.auth.verify_response(bytes(valid_response))
        self.assertEqual(status_code, 0x01)
        self.assertFalse(self.auth.session_authenticated)

    def test_replay_attack_same_session(self):
        """Replaying a previously successful response without generating new challenge must be rejected."""
        challenge = self.auth.generate_challenge()
        response = self.auth.compute_client_response(challenge)

        # First verification succeeds
        code1, _ = self.auth.verify_response(response)
        self.assertEqual(code1, 0x00)
        self.assertTrue(self.auth.session_authenticated)

        # Attacker replays exact same response immediately
        code2, notify2 = self.auth.verify_response(response)
        self.assertEqual(code2, 0x01)  # AUTH_FAILED because current_nonce is None
        self.assertEqual(notify2, bytes([0x03, 0x01]))

    def test_replay_attack_new_challenge(self):
        """Attacker intercepts old response and submits it against a newly issued challenge -> must fail."""
        # Session 1
        c1 = self.auth.generate_challenge()
        r1 = self.auth.compute_client_response(c1)
        self.auth.verify_response(r1)

        # Session reset
        self.auth.reset_session()
        self.assertFalse(self.auth.session_authenticated)

        # Session 2: new challenge issued
        c2 = self.auth.generate_challenge()
        self.assertNotEqual(c1[1:17], c2[1:17], "New nonce must be cryptographically distinct")

        # Attacker replays r1 from Session 1
        code_replay, _ = self.auth.verify_response(r1)
        self.assertEqual(code_replay, 0x01)  # Rejected
        self.assertFalse(self.auth.session_authenticated)

    def test_unsolicited_response_write_without_challenge(self):
        """Writing response payload to 0xC405 when no challenge was ever requested -> must fail."""
        random_response = bytes([0x02]) + os.urandom(32)
        code, notify = self.auth.verify_response(random_response)
        self.assertEqual(code, 0x01)
        self.assertFalse(self.auth.session_authenticated)

    def test_malformed_response_length_under_33_bytes(self):
        """Writing short payload (< 33 bytes) to 0xC405 must be rejected."""
        self.auth.generate_challenge()
        short_resp = bytes([0x02]) + b"\xAA" * 10
        code, notify = self.auth.verify_response(short_resp)
        self.assertEqual(code, 0x01)
        self.assertFalse(self.auth.session_authenticated)

    def test_wrong_tag_in_response(self):
        """Tag 0x01 or 0x03 written to response port instead of 0x02 -> must be rejected."""
        self.auth.generate_challenge()
        bad_tag_resp = bytes([0x01]) + os.urandom(32)
        code, notify = self.auth.verify_response(bad_tag_resp)
        self.assertEqual(code, 0x01)
        self.assertFalse(self.auth.session_authenticated)

    def test_brute_force_lockout_counter_reset_after_expiry(self):
        """
        After 3 failed attempts, system locks out for 60s.
        Once the 60s expires, failed_attempts is reset so a single subsequent typo/failure
        does not immediately re-lock out the user (requires 3 new failures).
        """
        t0 = 1000.0
        for i in range(3):
            c = self.auth.generate_challenge()
            r = self.auth.compute_client_response(c, secret="WRONG_SECRET")
            self.auth.verify_response(r, current_time=t0 + i)

        self.assertEqual(self.auth.lockout_until, t0 + 62.0)

        # Fast forward past 60s lockout window: t = 1070.0
        t_after = 1070.0
        c_after1 = self.auth.generate_challenge()
        r_after1 = self.auth.compute_client_response(c_after1, secret="TYPO_KEY_1")
        code1, notify1 = self.auth.verify_response(r_after1, current_time=t_after)
        self.assertEqual(code1, 0x01)
        self.assertEqual(notify1, bytes([0x03, 0x01]))
        self.assertEqual(self.auth.failed_attempts, 1)
        self.assertEqual(self.auth.lockout_until, 0.0)

        # Second typo at t = 1071.0
        c_after2 = self.auth.generate_challenge()
        r_after2 = self.auth.compute_client_response(c_after2, secret="TYPO_KEY_2")
        code2, _ = self.auth.verify_response(r_after2, current_time=t_after + 1.0)
        self.assertEqual(code2, 0x01)
        self.assertEqual(self.auth.failed_attempts, 2)
        self.assertEqual(self.auth.lockout_until, 0.0)

        # Third typo at t = 1072.0 -> triggers new 60s lockout
        c_after3 = self.auth.generate_challenge()
        r_after3 = self.auth.compute_client_response(c_after3, secret="TYPO_KEY_3")
        code3, _ = self.auth.verify_response(r_after3, current_time=t_after + 2.0)
        self.assertEqual(code3, 0x01)
        self.assertEqual(self.auth.failed_attempts, 3)
        self.assertEqual(self.auth.lockout_until, t_after + 62.0)


class TestBLEAdversarialCommandAndTelemetry(unittest.TestCase):
    """Stress tests on command execution and telemetry boundaries."""

    def test_increment_roll_with_camera_suffix(self):
        """
        Roll with camera suffix like 'A001B' increments digits while preserving suffix ('A002B').
        """
        res = CommandProcessor.increment_roll("A001B")
        self.assertEqual(res, "A002B")
        self.assertEqual(CommandProcessor.increment_roll("B009C"), "B010C")

    def test_battery_voltage_negative_clamped(self):
        """BatteryTelemetryPacker.pack with negative voltage clamps to 0 mV without struct.error."""
        packed = BatteryTelemetryPacker.pack(-500)
        mv, pct, flags = BatteryTelemetryPacker.unpack(packed)
        self.assertEqual(mv, 0)
        self.assertEqual(pct, 0)
        self.assertEqual(flags & 0x02, 0x02)  # FLAG_BATTERY_LOW

    def test_battery_voltage_overflow_clamped(self):
        """BatteryTelemetryPacker.pack with voltage > 65535 mV clamps to 65535 mV without struct.error."""
        packed = BatteryTelemetryPacker.pack(70000)
        mv, pct, flags = BatteryTelemetryPacker.unpack(packed)
        self.assertEqual(mv, 65535)
        self.assertEqual(pct, 100)
        self.assertEqual(flags & 0x01, 0x01)  # FLAG_USB_CONNECTED_OR_FULL

    def test_button_event_code_overflow_clamped(self):
        """ButtonTelemetryPacker.pack with event_code > 255 clamps to 255 without struct.error."""
        packed = ButtonTelemetryPacker.pack(300, 1)
        code, counter = ButtonTelemetryPacker.unpack(packed)
        self.assertEqual(code, 255)
        self.assertEqual(counter, 1)


def run_adversarial_suite():
    loader = unittest.TestLoader()
    suite = unittest.TestSuite()
    suite.addTests(loader.loadTestsFromTestCase(TestBLEAdversarialMalformedChunks))
    suite.addTests(loader.loadTestsFromTestCase(TestBLEAdversarialOutOfOrderAndFuzzing))
    suite.addTests(loader.loadTestsFromTestCase(TestBLEAdversarialGuardTimer))
    suite.addTests(loader.loadTestsFromTestCase(TestBLEAdversarialCryptographicSecurity))
    suite.addTests(loader.loadTestsFromTestCase(TestBLEAdversarialCommandAndTelemetry))
    runner = unittest.TextTestRunner(verbosity=2)
    return runner.run(suite)


if __name__ == "__main__":
    result = run_adversarial_suite()
    sys.exit(0 if result.wasSuccessful() else 1)
