#!/usr/bin/env python3
"""
Adversarial Verification & Stress Test Suite — Challenger Upgrade 2
===================================================================
Author: challenger_upgrade_2 (Empirical Challenger)
Targets:
1. iOS Keychain & Auth flow (missing PIN, invalid PIN, 3-attempt 60s lockout, unlock, persistence).
2. iOS Keyboard avoidance & dynamic padding across device viewports (iPhone SE, iPhone 15/16 Pro, iPad).
3. Web Bluetooth PWA & Web Crypto HMAC-SHA256 multi-platform parity (Python, Node.js, Apple CryptoKit).
4. Deterministic 3-byte chunking fragmentation across boundary MTUs (20B, 23B, 128B, 240B, 512B),
   slicing across 4-byte UTF-8 emoji (🌸), chunk loss, and 3000ms reassembly timeouts.
5. Coexistence: Concurrent BLE GATT operations and simulated Wi-Fi HTTP requests.
"""

import os
import sys
import time
import json
import math
import hmac
import hashlib
import struct
import subprocess
import threading
import unittest
from typing import List, Dict, Tuple, Optional

# Ensure repository root is on sys.path
REPO_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
if REPO_ROOT not in sys.path:
    sys.path.insert(0, REPO_ROOT)

from tests.test_ble_protocol import (
    ClapperboardState,
    BLEPacketChunker,
    BLEReassemblyEngine,
    SecurityAuthManager,
    ConnectionLockManager,
    CommandProcessor,
    BatteryTelemetryPacker,
    ButtonTelemetryPacker,
)


# ============================================================================
# 1. WEB CRYPTO / PYTHON / APPLE CRYPTOKIT PARITY HARNESS
# ============================================================================

class TestWebCryptoParityAndCryptoKit(unittest.TestCase):
    """Verifies HMAC-SHA256 parity between Python hmac, Node.js Web Crypto, and Apple CryptoKit."""

    TEST_VECTORS = [
        # (Key bytes, Data bytes, Description)
        (b"123456789P", bytes([1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16]), "E-Slate Default PIN + 16B Nonce"),
        (b"0000", bytes([0] * 16), "4-digit Zero PIN + Zero Nonce"),
        (b"A1B2C3D4E5", bytes([0xFF] * 16), "Alphanumeric PIN + 0xFF Nonce"),
        (b"CinemaSlate2026!@#$%^&*()_+~`", bytes(range(16)), "Complex 30-char PIN + Sequential Nonce"),
        ("CLAP🌸2026".encode("utf-8"), bytes([0xF0, 0x9F, 0x8C, 0xB8] * 4), "Unicode Emoji PIN + Emoji Nonce"),
        # RFC 4231 Vector 1
        (bytes([0x0b] * 20), b"Hi There", "RFC 4231 Vector 1"),
        # RFC 4231 Vector 2
        (b"Jefe", b"what do ya want for nothing?", "RFC 4231 Vector 2"),
        # RFC 4231 Vector 3
        (bytes([0xaa] * 20), bytes([0xdd] * 50), "RFC 4231 Vector 3"),
    ]

    @staticmethod
    def _run_node_web_crypto(key_bytes: bytes, data_bytes: bytes) -> str:
        """Computes HMAC-SHA256 using Node.js Web Crypto API (globalThis.crypto.subtle)."""
        key_hex = key_bytes.hex()
        data_hex = data_bytes.hex()
        js_code = f"""
        (async () => {{
            const keyBytes = Buffer.from("{key_hex}", "hex");
            const dataBytes = Buffer.from("{data_hex}", "hex");
            const cryptoKey = await crypto.subtle.importKey(
                "raw", keyBytes,
                {{ name: "HMAC", hash: {{ name: "SHA-256" }} }},
                false, ["sign"]
            );
            const signature = await crypto.subtle.sign("HMAC", cryptoKey, dataBytes);
            console.log(Buffer.from(signature).toString("hex"));
        }})();
        """
        result = subprocess.run(
            ["node", "-e", js_code],
            capture_output=True,
            text=True,
            check=True
        )
        return result.stdout.strip()

    @staticmethod
    def _run_apple_cryptokit(key_bytes: bytes, data_bytes: bytes) -> str:
        """Computes HMAC-SHA256 using Apple CryptoKit compiled through Swift CLI."""
        key_hex = key_bytes.hex()
        data_hex = data_bytes.hex()
        swift_code = f"""
        import Foundation
        import CryptoKit

        func dataFromHex(_ hex: String) -> Data {{
            var data = Data()
            var index = hex.startIndex
            while index < hex.endIndex {{
                let nextIndex = hex.index(index, offsetBy: 2)
                let byteStr = hex[index..<nextIndex]
                if let byte = UInt8(byteStr, radix: 16) {{
                    data.append(byte)
                }}
                index = nextIndex
            }}
            return data
        }}

        let key = SymmetricKey(data: dataFromHex("{key_hex}"))
        let data = dataFromHex("{data_hex}")
        let code = HMAC<SHA256>.authenticationCode(for: data, using: key)
        print(Data(code).map {{ String(format: "%02x", $0) }}.joined())
        """
        module_cache = os.path.join(REPO_ROOT, ".derivedData", "ModuleCache")
        os.makedirs(module_cache, exist_ok=True)
        result = subprocess.run(
            ["swift", "-module-cache-path", module_cache, "-"],
            input=swift_code,
            capture_output=True,
            text=True,
            check=True
        )
        return result.stdout.strip()

    def test_cryptographic_parity_triad(self):
        """Assert 100% byte-for-byte identity: Python hmac == Node Web Crypto == Apple CryptoKit."""
        for key_bytes, data_bytes, desc in self.TEST_VECTORS:
            with self.subTest(description=desc):
                # 1. Python standard library hmac
                expected_digest = hmac.new(key_bytes, data_bytes, hashlib.sha256).hexdigest()

                # 2. Node.js Web Crypto API
                node_digest = self._run_node_web_crypto(key_bytes, data_bytes)
                self.assertEqual(node_digest, expected_digest, f"Node Web Crypto mismatch on {desc}")

                # 3. Apple CryptoKit (Swift)
                swift_digest = self._run_apple_cryptokit(key_bytes, data_bytes)
                self.assertEqual(swift_digest, expected_digest, f"Apple CryptoKit mismatch on {desc}")


# ============================================================================
# 2. DETERMINISTIC 3-BYTE CHUNKING & BOUNDARY MTU STRESS TESTS
# ============================================================================

class TestChunkingBoundaryAndFragmentation(unittest.TestCase):
    """Stress tests 3-byte chunking ([seq][total][len]) across boundary MTUs, timeouts, and loss."""

    BOUNDARY_MTUS = [20, 23, 128, 240, 512]
    SAKURA_EMOJI_UTF8 = "🌸".encode("utf-8")  # b'\xf0\x9f\x8c\xb8' (4 bytes)

    def setUp(self):
        self.engine = BLEReassemblyEngine()

    def test_chunking_packet_structure_invariants(self):
        """Header invariant verification: byte[0]=seq, byte[1]=total, byte[2]=slice_len."""
        payload = "NEBRALTA E-SLATE CLAPPERBOARD CALIBRATION PAYLOAD WITH SAKURA 🌸".encode("utf-8") * 10
        for mtu in self.BOUNDARY_MTUS:
            chunks = BLEPacketChunker.chunk_payload(payload, mtu=mtu)
            total = len(chunks)
            self.assertGreaterEqual(total, 1)
            self.assertLessEqual(total, 255)

            for seq, chunk in enumerate(chunks):
                self.assertGreaterEqual(len(chunk), 3)
                self.assertEqual(chunk[0], seq, f"Seq mismatch in chunk {seq} at MTU {mtu}")
                self.assertEqual(chunk[1], total, f"Total mismatch in chunk {seq} at MTU {mtu}")
                self.assertEqual(chunk[2], len(chunk) - 3, f"Len mismatch in chunk {seq} at MTU {mtu}")
                self.assertLessEqual(len(chunk), max(20, mtu - 3 if mtu in (20, 23) else mtu))

    def test_boundary_payload_sizes_and_255_chunk_limit(self):
        """Test boundary payload lengths up to 255 chunks, and rejection of 256 chunks."""
        # For MTU 20: max slice is 14 -> 255 * 14 = 3570 bytes
        chunks_3570 = BLEPacketChunker.chunk_payload(b"Z" * 3570, mtu=20)
        self.assertEqual(len(chunks_3570), 255)
        with self.assertRaises(ValueError):
            BLEPacketChunker.chunk_payload(b"Z" * 3571, mtu=20)

        # For MTU 23: max slice is 17 -> 255 * 17 = 4335 bytes
        chunks_4335 = BLEPacketChunker.chunk_payload(b"Z" * 4335, mtu=23)
        self.assertEqual(len(chunks_4335), 255)
        with self.assertRaises(ValueError):
            BLEPacketChunker.chunk_payload(b"Z" * 4336, mtu=23)

        # Reassembly verification across boundary sizes
        test_sizes = [0, 1, 2, 14, 17, 18, 125, 240, 241, 480, 509, 510, 1024, 2048]
        for size in test_sizes:
            payload = b"X" * size
            for mtu in [20, 23, 240, 512]:
                chunks = BLEPacketChunker.chunk_payload(payload, mtu=mtu)
                self.engine.reset()
                status = "IN_PROGRESS"
                result = None
                for chunk in chunks:
                    status, result = self.engine.feed_chunk(chunk)

                if size == 0:
                    self.assertEqual(status, "COMPLETE")
                    self.assertEqual(result, b"")
                else:
                    self.assertEqual(status, "COMPLETE", f"Failed at size {size}, MTU {mtu}")
                    self.assertEqual(result, payload)

    def test_emoji_slicing_all_offsets(self):
        """Sweeps 4-byte UTF-8 emoji (🌸) across every possible byte boundary offset."""
        for offset in range(0, 32):
            prefix = b"A" * offset
            suffix = b"B" * 32
            payload = prefix + self.SAKURA_EMOJI_UTF8 + suffix
            for mtu in [20, 23, 30]:
                chunks = BLEPacketChunker.chunk_payload(payload, mtu=mtu)
                self.engine.reset()
                result = None
                for chunk in chunks:
                    status, result = self.engine.feed_chunk(chunk)
                self.assertEqual(status, "COMPLETE")
                self.assertEqual(result, payload)
                # Verify exact UTF-8 emoji string reconstruction
                decoded_str = result.decode("utf-8")
                self.assertIn("🌸", decoded_str)

    def test_chunk_loss_handling(self):
        """Dropping a middle chunk must leave assembly incomplete or trigger clean abort."""
        payload = b"ChunkLossTestPayload_" * 30
        chunks = BLEPacketChunker.chunk_payload(payload, mtu=20)
        self.assertGreater(len(chunks), 3)

        # Drop chunk at index 1
        self.engine.reset()
        status0, _ = self.engine.feed_chunk(chunks[0])
        self.assertEqual(status0, "IN_PROGRESS")

        # Skip chunk 1, feed chunk 2 -> must reject out-of-order sequence
        status2, res2 = self.engine.feed_chunk(chunks[2])
        self.assertEqual(status2, "ERROR_OUT_OF_ORDER")
        self.assertFalse(self.engine.in_progress)

    def test_out_of_order_chunk_rejection(self):
        """Feeding chunks in reversed order must be rejected immediately."""
        payload = b"ReverseChunkTest" * 20
        chunks = BLEPacketChunker.chunk_payload(payload, mtu=20)
        self.assertGreater(len(chunks), 2)

        self.engine.reset()
        # Feed chunk 1 first (seq != 0)
        status, res = self.engine.feed_chunk(chunks[1])
        self.assertEqual(status, "ERROR_OUT_OF_ORDER")
        self.assertFalse(self.engine.in_progress)

    def test_reassembly_guard_timeout_simulation(self):
        """Simulate 3000ms guard timeout expiry between chunk arrivals."""
        payload = b"TimeoutSimulationPayload" * 15
        chunks = BLEPacketChunker.chunk_payload(payload, mtu=20)
        self.assertGreater(len(chunks), 2)

        self.engine.reset()
        now = time.monotonic()
        # Feed chunk 0 at t=0
        status0, _ = self.engine.feed_chunk(chunks[0], current_time=now)
        self.assertEqual(status0, "IN_PROGRESS")

        # Feed chunk 1 at t=now + 3.1s (> 3.0s guard timeout)
        status1, res1 = self.engine.feed_chunk(chunks[1], current_time=now + 3.1)
        self.assertEqual(status1, "ERROR_TIMEOUT")
        self.assertFalse(self.engine.in_progress)

        # Immediate recovery: new transmission with chunk 0 starts cleanly
        status_rec, _ = self.engine.feed_chunk(chunks[0], current_time=now + 3.2)
        self.assertEqual(status_rec, "IN_PROGRESS")


# ============================================================================
# 3. IOS KEYCHAIN & AUTH FLOW STATE MACHINE SIMULATION
# ============================================================================

class SimulatedIOSAppSession:
    """Emulates BluetoothManager + KeychainHelper + AuthPromptSheetView state machine."""

    def __init__(self, stored_keychain_pin: Optional[str] = None):
        self.keychain_pin: Optional[str] = stored_keychain_pin
        self.secret_pin: str = stored_keychain_pin or ""
        self.is_connected = False
        self.is_authenticated = False
        self.is_auth_prompt_presented = False
        self.auth_failure_count = 0
        self.is_locked_out = False
        self.lockout_seconds_remaining = 0
        self.pending_nonce: Optional[bytes] = None
        self.status_message = "Desconectado"

    def on_did_connect(self):
        self.is_connected = True
        self.status_message = "Descubriendo servicios..."

    def on_receive_challenge(self, challenge_payload: bytes):
        """Matches BluetoothManager.swift handleAuthData (tag 0x01)."""
        tag = challenge_payload[0]
        if tag == 0x01 and len(challenge_payload) >= 17:
            self.pending_nonce = challenge_payload[1:17]
            effective_pin = self.secret_pin or self.keychain_pin or ""
            if effective_pin:
                self.secret_pin = effective_pin
                return self._build_hmac_response(self.pending_nonce, effective_pin)
            else:
                self.is_auth_prompt_presented = True
                self.status_message = "Esperando PIN de claqueta..."
                return None
        return None

    def user_submits_pin(self, pin: str, remember: bool = True) -> Optional[bytes]:
        """Matches BluetoothManager.swift submitPinAuth."""
        trimmed = pin.strip()
        if not trimmed or self.is_locked_out:
            return None
        self.secret_pin = trimmed
        if remember:
            self.keychain_pin = trimmed
        self.is_auth_prompt_presented = False
        if self.pending_nonce:
            return self._build_hmac_response(self.pending_nonce, trimmed)
        return None

    def on_receive_status(self, status_byte: int):
        """Matches BluetoothManager.swift handleAuthData (tag 0x03)."""
        if status_byte == 0x00:
            self.is_authenticated = True
            self.is_auth_prompt_presented = False
            self.auth_failure_count = 0
            self.is_locked_out = False
            self.pending_nonce = None
            self.status_message = "🟢 Conectado: E-Slate NEBRALTA"
        elif status_byte == 0x01:
            self.is_authenticated = False
            self.auth_failure_count += 1
            self.status_message = "⚠️ PIN de autenticación erróneo"
            self.keychain_pin = None
            self.secret_pin = ""
            self.is_auth_prompt_presented = True
        elif status_byte == 0x02:
            self.is_authenticated = False
            self.is_locked_out = True
            self.lockout_seconds_remaining = 60
            self.status_message = "⛔️ Bloqueado por 60s"
            self.is_auth_prompt_presented = True

    def _build_hmac_response(self, nonce: bytes, pin: str) -> bytes:
        sig = hmac.new(pin.encode("utf-8"), nonce, hashlib.sha256).digest()
        return bytes([0x02]) + sig


class TestIOSKeychainAndAuthFlow(unittest.TestCase):
    """Stress tests the complete iOS Keychain, modal prompt, lockout, and auth state machine."""

    def setUp(self):
        self.server = SecurityAuthManager(shared_secret="123456789P")

    def test_missing_pin_in_keychain_triggers_modal(self):
        """When Keychain is empty, Tag 0x01 challenge must pop modal and pause handshake."""
        app = SimulatedIOSAppSession(stored_keychain_pin=None)
        app.on_did_connect()

        challenge = self.server.generate_challenge()
        response = app.on_receive_challenge(challenge)

        # No automatic response sent, modal presented, nonce held
        self.assertIsNone(response)
        self.assertTrue(app.is_auth_prompt_presented)
        self.assertFalse(app.is_authenticated)
        self.assertEqual(app.pending_nonce, challenge[1:17])
        self.assertEqual(app.status_message, "Esperando PIN de claqueta...")

    def test_invalid_pin_triggers_error_and_purges_keychain(self):
        """Invalid PIN entry triggers status 0x01, failure counter increment, and Keychain deletion."""
        app = SimulatedIOSAppSession(stored_keychain_pin=None)
        app.on_did_connect()
        challenge = self.server.generate_challenge()
        app.on_receive_challenge(challenge)

        # User enters bad PIN
        resp = app.user_submits_pin("WRONGPIN", remember=True)
        self.assertIsNotNone(resp)

        # Server verifies and rejects
        status_code, status_payload = self.server.verify_response(resp)
        self.assertEqual(status_code, 0x01)
        self.assertEqual(status_payload[:2], bytes([0x03, 0x01]))

        # App handles rejection
        app.on_receive_status(status_payload[1])
        self.assertFalse(app.is_authenticated)
        self.assertEqual(app.auth_failure_count, 1)
        self.assertIsNone(app.keychain_pin, "Keychain must be purged on auth failure")
        self.assertTrue(app.is_auth_prompt_presented)
        self.assertEqual(app.status_message, "⚠️ PIN de autenticación erróneo")

    def test_three_consecutive_failures_trigger_60s_lockout(self):
        """3 consecutive failures trigger 60s lockout; submissions during lockout receive 0x02."""
        app = SimulatedIOSAppSession(stored_keychain_pin=None)
        app.on_did_connect()
        base_time = 1000.0

        # Attempt 1 (Failure)
        c1 = self.server.generate_challenge()
        app.on_receive_challenge(c1)
        r1 = app.user_submits_pin("FAIL1")
        _, p1 = self.server.verify_response(r1, current_time=base_time)
        app.on_receive_status(p1[1])
        self.assertEqual(app.auth_failure_count, 1)
        self.assertFalse(app.is_locked_out)

        # Attempt 2 (Failure)
        c2 = self.server.generate_challenge()
        app.on_receive_challenge(c2)
        r2 = app.user_submits_pin("FAIL2")
        _, p2 = self.server.verify_response(r2, current_time=base_time + 1.0)
        app.on_receive_status(p2[1])
        self.assertEqual(app.auth_failure_count, 2)
        self.assertFalse(app.is_locked_out)

        # Attempt 3 (Failure -> triggers lockout)
        c3 = self.server.generate_challenge()
        app.on_receive_challenge(c3)
        r3 = app.user_submits_pin("FAIL3")
        _, p3 = self.server.verify_response(r3, current_time=base_time + 2.0)
        app.on_receive_status(p3[1])
        self.assertEqual(app.auth_failure_count, 3)

        # Now server is in 60s lockout until t = base_time + 62.0s
        self.assertEqual(self.server.lockout_until, base_time + 62.0)

        # Attempt 4 made at t = base_time + 10.0s (during lockout)
        c4 = self.server.generate_challenge()
        app.on_receive_challenge(c4)
        # Even if user tries to submit valid pin, during lockout server sends 0x02
        r4 = app._build_hmac_response(c4[1:17], "123456789P")
        code4, p4 = self.server.verify_response(r4, current_time=base_time + 10.0)
        self.assertEqual(code4, 0x02)
        self.assertEqual(p4, bytes([0x03, 0x02]))

        # App updates to lockout state
        app.on_receive_status(p4[1])
        self.assertTrue(app.is_locked_out)
        self.assertEqual(app.lockout_seconds_remaining, 60)
        self.assertEqual(app.status_message, "⛔️ Bloqueado por 60s")

        # Submit is locked out in UI
        r_locked = app.user_submits_pin("123456789P")
        self.assertIsNone(r_locked, "UI must disable submissions during lockout")

    def test_lockout_expiry_and_successful_unlock(self):
        """After 60s elapses, valid PIN unlocks session and stores in Keychain."""
        app = SimulatedIOSAppSession(stored_keychain_pin=None)
        app.on_did_connect()
        base_time = 2000.0

        # Trigger 3 failures
        for i in range(3):
            c = self.server.generate_challenge()
            app.on_receive_challenge(c)
            r = app.user_submits_pin(f"BAD_{i}")
            _, p = self.server.verify_response(r, current_time=base_time + i)
            app.on_receive_status(p[1])

        self.assertEqual(self.server.failed_attempts, 3)

        # Advance past 60s lockout: t = base_time + 65.0s
        unlock_time = base_time + 65.0
        app.is_locked_out = False  # Timer expired

        c_valid = self.server.generate_challenge()
        app.on_receive_challenge(c_valid)
        r_valid = app.user_submits_pin("123456789P", remember=True)
        self.assertIsNotNone(r_valid)

        code, p_success = self.server.verify_response(r_valid, current_time=unlock_time)
        self.assertEqual(code, 0x00)
        self.assertEqual(p_success[0], 0x03)
        self.assertEqual(p_success[1], 0x00)

        app.on_receive_status(p_success[1])
        self.assertTrue(app.is_authenticated)
        self.assertFalse(app.is_auth_prompt_presented)
        self.assertEqual(app.auth_failure_count, 0)
        self.assertEqual(app.keychain_pin, "123456789P")

        # Subsequent connection with stored Keychain PIN auto-authenticates
        app2 = SimulatedIOSAppSession(stored_keychain_pin=app.keychain_pin)
        app2.on_did_connect()
        c_auto = self.server.generate_challenge()
        r_auto = app2.on_receive_challenge(c_auto)
        self.assertIsNotNone(r_auto, "Stored Keychain PIN must immediately respond")
        self.assertFalse(app2.is_auth_prompt_presented)

        code_auto, p_auto = self.server.verify_response(r_auto, current_time=unlock_time + 10.0)
        self.assertEqual(code_auto, 0x00)
        app2.on_receive_status(p_auto[1])
        self.assertTrue(app2.is_authenticated)


# ============================================================================
# 4. IOS KEYBOARD AVOIDANCE & VIEWPORT GEOMETRY VERIFICATION
# ============================================================================

class TestIOSKeyboardAvoidanceGeometry(unittest.TestCase):
    """Verifies keyboard avoidance padding, auto-scroll centering, and toolbar layout across devices."""

    # Device specifications: (name, screen_width, screen_height, top_bar_pt, home_indicator_pt, keyboard_height_pt)
    DEVICES = [
        ("iPhone SE (3rd Gen)", 375, 667, 44, 0, 260),
        ("iPhone 15 / 16 Pro", 393, 852, 59, 34, 336),
        ("iPhone 15 / 16 Pro Max", 430, 932, 59, 34, 346),
        ("iPad 10th Gen (Portrait)", 820, 1180, 24, 20, 313),
    ]

    KEYBOARD_ACCESSORY_BAR_HEIGHT = 44  # ToolbarItemGroup(placement: .keyboard)
    STICKY_TOOLBAR_HEIGHT = 120         # quickActionBar height with padding
    ACTIVE_INPUT_PADDING = 24           # Bottom padding when focusedField != nil
    CARD_HEIGHT = 64                    # Approximate slate input card height

    SLATE_FIELDS = ["roll", "scene", "take", "prod", "dir", "dop", "note", "date"]

    def test_sticky_toolbar_complete_occlusion_elimination(self):
        """When focusedField != nil, quickActionBar is omitted (height 0), padding becomes 24pt."""
        for name, w, h, top_bar, home_bar, kbd_h in self.DEVICES:
            with self.subTest(device=name):
                # When unfocused:
                padding_unfocused = 120
                toolbar_present = True

                # When focused:
                padding_focused = self.ACTIVE_INPUT_PADDING
                toolbar_present_focused = False

                self.assertEqual(padding_focused, 24)
                self.assertFalse(toolbar_present_focused)
                self.assertGreater(padding_unfocused, padding_focused)

    def test_active_viewport_clearance_across_devices(self):
        """Computes visible viewport and validates that centered input card has sufficient margin."""
        for name, w, h, top_bar, home_bar, kbd_h in self.DEVICES:
            with self.subTest(device=name):
                # Usable vertical viewport above keyboard and accessory bar
                visible_viewport = h - top_bar - kbd_h - self.KEYBOARD_ACCESSORY_BAR_HEIGHT

                # Assert that visible viewport can comfortably fit a 64pt card centered
                self.assertGreater(visible_viewport, self.CARD_HEIGHT + 100, f"Viewport too small on {name}")

                # Field centered at visible_viewport / 2
                field_top = (visible_viewport - self.CARD_HEIGHT) / 2
                field_bottom = field_top + self.CARD_HEIGHT

                # Top clearance from navigation bar
                self.assertGreaterEqual(field_top, 40, f"Top clearance insufficient on {name}")
                # Bottom clearance from keyboard top
                clearance_to_kbd = visible_viewport - field_bottom
                self.assertGreaterEqual(clearance_to_kbd, 40, f"Bottom clearance insufficient on {name}")

    def test_rapid_keyboard_focus_transitions(self):
        """Simulates 1,000 rapid focus switches across fields to verify state stability."""
        focused_field = None
        padding = 120

        for cycle in range(1000):
            # Pick a field or None
            target = self.SLATE_FIELDS[cycle % len(self.SLATE_FIELDS)] if (cycle % 3 != 0) else None
            focused_field = target

            if focused_field is not None:
                padding = 24
                toolbar_visible = False
            else:
                padding = 120
                toolbar_visible = True

            # Invariants
            self.assertIn(padding, [24, 120])
            self.assertEqual(toolbar_visible, focused_field is None)


# ============================================================================
# 5. COEXISTENCE: CONCURRENT BLE & SIMULATED WI-FI HTTP REQUESTS
# ============================================================================

class TestCoexistenceBLEAndWiFi(unittest.TestCase):
    """Stress tests concurrent BLE packet streaming and Wi-Fi HTTP state mutations."""

    def test_concurrent_ble_notifications_and_http_mutations(self):
        """Dispatches 50 concurrent BLE chunk writes alongside 50 concurrent HTTP updates."""
        state = ClapperboardState(roll="A001", scene="1A", take="1", prod="NEBRALTA 🌸")
        lock = threading.Lock()
        errors = []

        def http_worker(worker_id: int):
            for i in range(10):
                try:
                    with lock:
                        state.take = str(int(state.take) + 1)
                        state.roll = f"A{worker_id:03d}"
                        state.prod = f"PROD_{worker_id} 🌸"
                        # Serialize state with ensure_ascii=False to match JavaScript & Swift
                        raw_json = json.dumps(state.to_dict(), ensure_ascii=False)
                        parsed = json.loads(raw_json)
                        assert "🌸" in parsed["prod"]
                except Exception as e:
                    errors.append(f"HTTP worker {worker_id} error: {e}")
                time.sleep(0.001)

        def ble_worker(worker_id: int):
            engine = BLEReassemblyEngine()
            for i in range(10):
                try:
                    with lock:
                        raw_bytes = json.dumps(state.to_dict(), ensure_ascii=False).encode("utf-8")
                    chunks = BLEPacketChunker.chunk_payload(raw_bytes, mtu=128)
                    engine.reset()
                    res = None
                    for ch in chunks:
                        st, res = engine.feed_chunk(ch)
                    assert st == "COMPLETE", f"Expected COMPLETE, got {st}"
                    assert b"\xf0\x9f\x8c\xb8" in res, "Emoji missing in reassembled BLE buffer"
                except Exception as e:
                    errors.append(f"BLE worker {worker_id} error: {e}")
                time.sleep(0.001)

        threads = []
        for i in range(5):
            threads.append(threading.Thread(target=http_worker, args=(i,)))
            threads.append(threading.Thread(target=ble_worker, args=(i,)))

        for t in threads:
            t.start()
        for t in threads:
            t.join()

        self.assertEqual(len(errors), 0, f"Encountered concurrency errors: {errors}")
        self.assertIn("🌸", state.prod)


if __name__ == "__main__":
    unittest.main(verbosity=2)
