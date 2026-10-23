#!/usr/bin/env python3
"""
E-Slate BLE Protocol Verification & Automated Test Suite
Exhaustive verification of BLE GATT data serialization, MTU chunking/reassembly,
UTF-8 emoji preservation, HMAC-SHA256 challenge-response security, and hardware telemetry.
"""

import hashlib
import hmac
import json
import math
import os
import struct
import sys
import time
import unittest


# ============================================================================
# 1. CLAPPERBOARD DATA MODEL & STATE NORMALIZATION
# ============================================================================

class ClapperboardState:
    DEFAULT_VISIBILITIES = {
        "roll": True,
        "scene": True,
        "take": True,
        "prod": True,
        "dir": True,
        "dop": True,
        "cam": True,
        "sound": True,
        "loc": True,
        "light": True,
        "date": True,
        "timecode": True,
        "note": True,
        "bat": True,
    }

    def __init__(
        self,
        roll: str = "A001",
        scene: str = "1A",
        take: str = "1",
        prod: str = "NEBRALTA 🌸",
        director: str = "DIRECTOR",
        dop: str = "FELIPELOTAS1",
        note: str = "ISO 120",
        date: str = "25.09.2026",
        timecode: str = "00:00:00",
        tc_slot: int = 0,
        tc_size: int = 1,
        tc_fps: str = "24",
        tc_offset: int = 0,
        tc_running: bool = True,
        cam: str = "A",
        sound: str = "SYNC",
        loc: str = "INT",
        light: str = "DAY",
        layout: int = 0,
        invert: bool = False,
        visibilities: dict | None = None,
    ):
        self.roll = self.normalize_text(roll)
        self.scene = self.normalize_text(scene)
        self.take = self.normalize_text(take)
        self.prod = self.normalize_text(prod)
        self.director = self.normalize_text(director)
        self.dop = self.normalize_text(dop)
        self.note = self.normalize_text(note)
        self.date = self.normalize_text(date)
        self.timecode = self.normalize_text(timecode)
        self.tc_slot = max(0, min(2, int(tc_slot))) if tc_slot is not None else 0
        self.tc_size = max(0, min(2, int(tc_size))) if tc_size is not None else 1
        self.tc_fps = str(tc_fps) if tc_fps is not None else "24"
        self.tc_offset = int(tc_offset) if tc_offset is not None else 0
        self.tc_running = bool(tc_running)
        self.cam = self.normalize_text(cam)
        self.sound = self.normalize_text(sound)
        self.loc = self.normalize_text(loc)
        self.light = self.normalize_text(light)
        self.layout = int(layout) if layout is not None else 0
        self.invert = bool(invert)
        self.visibilities = dict(self.DEFAULT_VISIBILITIES)
        if visibilities:
            self.visibilities.update(visibilities)

    @staticmethod
    def normalize_text(text: object) -> str:
        """Coerce text/numeric values to uppercase string while preserving multi-byte UTF-8 symbols."""
        if text is None:
            return ""
        return str(text).upper()

    def to_dict(self) -> dict:
        return {
            "roll": self.roll,
            "scene": self.scene,
            "take": self.take,
            "prod": self.prod,
            "dir": self.director,
            "dop": self.dop,
            "note": self.note,
            "date": self.date,
            "timecode": self.timecode,
            "tc_slot": self.tc_slot,
            "tc_size": self.tc_size,
            "tc_fps": self.tc_fps,
            "tc_offset": self.tc_offset,
            "tc_running": self.tc_running,
            "cam": self.cam,
            "sound": self.sound,
            "loc": self.loc,
            "light": self.light,
            "layout": self.layout,
            "invert": self.invert,
            "vis": dict(self.visibilities),
        }

    def to_json(self) -> str:
        return json.dumps(self.to_dict(), ensure_ascii=False, separators=(",", ":"))

    def to_utf8_bytes(self) -> bytes:
        return self.to_json().encode("utf-8")

    @classmethod
    def from_dict(cls, data: dict) -> "ClapperboardState":
        return cls(
            roll=data.get("roll", "A001"),
            scene=data.get("scene", "1A"),
            take=data.get("take", "1"),
            prod=data.get("prod", "NEBRALTA 🌸"),
            director=data.get("dir", data.get("director", "DIRECTOR")),
            dop=data.get("dop", "FELIPELOTAS1"),
            note=data.get("note", "ISO 120"),
            date=data.get("date", "25.09.2026"),
            timecode=data.get("timecode", data.get("tc", "00:00:00")),
            tc_slot=data.get("tc_slot", 0),
            tc_size=data.get("tc_size", 1),
            tc_fps=data.get("tc_fps", "24"),
            tc_offset=data.get("tc_offset", 0),
            tc_running=data.get("tc_running", True),
            cam=data.get("cam", "A"),
            sound=data.get("sound", "SYNC"),
            loc=data.get("loc", "INT"),
            light=data.get("light", "DAY"),
            layout=data.get("layout", 0),
            invert=data.get("invert", False),
            visibilities=data.get("vis", None),
        )

    @classmethod
    def from_json(cls, json_str: str) -> "ClapperboardState":
        return cls.from_dict(json.loads(json_str))

    @classmethod
    def from_utf8_bytes(cls, data: bytes) -> "ClapperboardState":
        return cls.from_json(data.decode("utf-8"))

    def __eq__(self, other) -> bool:
        if not isinstance(other, ClapperboardState):
            return False
        return self.to_dict() == other.to_dict()


# ============================================================================
# 2. MTU CHUNKING & REASSEMBLY ENGINE
# ============================================================================

class BLEPacketChunker:
    """Chunks payloads exceeding ATT MTU using a 3-byte framing header: [seq, total, len]."""

    @staticmethod
    def chunk_payload(payload_bytes: bytes, mtu: int = 23) -> list[bytes]:
        max_att_payload = mtu - 3  # Deduct 3-byte ATT opcode/handle
        if max_att_payload < 4:
            raise ValueError(f"MTU {mtu} is too small for BLE transmission")

        # In 3-byte framing header [seq, total, len], len is uint8_t with max 240B ceiling
        max_slice_size = min(max_att_payload - 3, 240)

        # Single-packet transmission (zero fragmentation needed)
        if len(payload_bytes) <= max_slice_size:
            header = bytes([0, 1, len(payload_bytes)])
            return [header + payload_bytes]

        # Multi-packet transmission
        total_chunks = (len(payload_bytes) + max_slice_size - 1) // max_slice_size
        if total_chunks > 255:
            raise ValueError(f"Payload requires {total_chunks} chunks, exceeding 1-byte limit (255)")

        chunks = []
        for seq in range(total_chunks):
            start = seq * max_slice_size
            end = min(start + max_slice_size, len(payload_bytes))
            slice_data = payload_bytes[start:end]
            header = bytes([seq, total_chunks, len(slice_data)])
            chunks.append(header + slice_data)
        return chunks


class BLEReassemblyEngine:
    """Reassembles fragmented chunks into a unified byte buffer with guard timer & out-of-order check."""
    GUARD_TIMEOUT_SECONDS = 3.0

    def __init__(self):
        self.buffer = bytearray()
        self.expected_seq = 0
        self.total_chunks = 0
        self.start_timestamp = None
        self.in_progress = False

    def reset(self):
        self.buffer = bytearray()
        self.expected_seq = 0
        self.total_chunks = 0
        self.start_timestamp = None
        self.in_progress = False

    def feed_chunk(self, chunk: bytes, current_time: float | None = None) -> tuple[str, bytes | None]:
        if current_time is None:
            current_time = time.monotonic()

        if len(chunk) < 3:
            self.reset()
            return ("ERROR_HEADER_TOO_SHORT", None)

        seq = chunk[0]
        total = chunk[1]
        chunk_len = chunk[2]
        payload = chunk[3:]

        if len(payload) != chunk_len:
            self.reset()
            return ("ERROR_LENGTH_MISMATCH", None)

        if total < 1 or seq >= total:
            self.reset()
            return ("ERROR_INVALID_HEADER", None)

        if self.in_progress and (current_time - self.start_timestamp) > self.GUARD_TIMEOUT_SECONDS:
            self.reset()
            if seq != 0:
                return ("ERROR_TIMEOUT", None)

        if not self.in_progress:
            if seq != 0:
                self.reset()
                return ("ERROR_OUT_OF_ORDER", None)
            self.in_progress = True
            self.expected_seq = 0
            self.total_chunks = total
            self.start_timestamp = current_time
            self.buffer = bytearray()
        else:
            if seq != self.expected_seq or total != self.total_chunks:
                self.reset()
                return ("ERROR_OUT_OF_ORDER", None)

        self.buffer.extend(payload)
        self.expected_seq += 1

        if self.expected_seq == self.total_chunks:
            result = bytes(self.buffer)
            self.reset()
            return ("COMPLETE", result)

        return ("IN_PROGRESS", None)


# ============================================================================
# 3. MULTI-LAYER SECURITY & AUTHENTICATION MANAGER
# ============================================================================

class SecurityAuthManager:
    """Implements HMAC-SHA256 challenge-response, write gating, and rate-limiting."""
    GATT_ERROR_NOT_AUTHORIZED = 0x82

    def __init__(self, shared_secret: str = "123456789P"):
        self.shared_secret = shared_secret
        self.current_nonce = None
        self.session_authenticated = False
        self.failed_attempts = 0
        self.lockout_until = 0.0

    def generate_challenge(self) -> bytes:
        self.current_nonce = os.urandom(16)
        # Tag 0x01: CHALLENGE
        return bytes([0x01]) + self.current_nonce

    def compute_client_response(self, challenge_payload: bytes, secret: str | None = None) -> bytes:
        if secret is None:
            secret = self.shared_secret
        if len(challenge_payload) < 17 or challenge_payload[0] != 0x01:
            raise ValueError("Invalid challenge payload")
        nonce = challenge_payload[1:17]
        signature = hmac.new(secret.encode("utf-8"), nonce, hashlib.sha256).digest()
        # Tag 0x02: RESPONSE
        return bytes([0x02]) + signature

    def verify_response(self, response_payload: bytes, current_time: float | None = None) -> tuple[int, bytes]:
        if current_time is None:
            current_time = time.monotonic()

        if current_time < self.lockout_until:
            return (0x02, bytes([0x03, 0x02]))  # LOCKED_OUT
        elif self.lockout_until > 0.0:
            self.failed_attempts = 0
            self.lockout_until = 0.0

        if len(response_payload) < 33 or response_payload[0] != 0x02:
            self._handle_failed_attempt(current_time)
            return (0x01, bytes([0x03, 0x01]))  # AUTH_FAILED

        if self.current_nonce is None:
            self._handle_failed_attempt(current_time)
            return (0x01, bytes([0x03, 0x01]))  # AUTH_FAILED

        expected_sig = hmac.new(self.shared_secret.encode("utf-8"), self.current_nonce, hashlib.sha256).digest()
        client_sig = response_payload[1:33]

        if hmac.compare_digest(expected_sig, client_sig):
            self.session_authenticated = True
            self.failed_attempts = 0
            session_token = os.urandom(16)
            self.current_nonce = None
            return (0x00, bytes([0x03, 0x00]) + session_token)  # AUTH_SUCCESS
        else:
            self._handle_failed_attempt(current_time)
            return (0x01, bytes([0x03, 0x01]))  # AUTH_FAILED

    def _handle_failed_attempt(self, current_time: float):
        self.failed_attempts += 1
        if self.failed_attempts >= 3:
            self.lockout_until = current_time + 60.0

    def check_write_permission(self, char_uuid_short: int):
        if char_uuid_short in (0xC401, 0xC402):
            if not self.session_authenticated:
                raise PermissionError(self.GATT_ERROR_NOT_AUTHORIZED)
        return True

    def reset_session(self):
        self.session_authenticated = False
        self.current_nonce = None


class ConnectionLockManager:
    """Enforces exclusive single-central connection lock policy."""
    def __init__(self):
        self.is_advertising = True
        self.connected_central_id = None

    def connect(self, central_id: str) -> bool:
        if self.connected_central_id is not None:
            return False  # Refuse secondary connection
        self.connected_central_id = central_id
        self.is_advertising = False
        return True

    def disconnect(self, central_id: str) -> bool:
        if self.connected_central_id == central_id:
            self.connected_central_id = None
            self.is_advertising = True
            return True
        return False


# ============================================================================
# 4. COMMAND PROCESSOR & ALPHANUMERIC INCREMENT LOGIC
# ============================================================================

class CommandProcessor:
    """Executes atomic commands matching firmware logic in eink_clap.ino."""

    @staticmethod
    def increment_take(take_str: str) -> str:
        """Port of incrementTake() in eink_clap.ino:798-815."""
        try:
            val = int(take_str)
            if val >= 0:
                return str(val + 1)
        except ValueError:
            pass

        i = 0
        while i < len(take_str) and take_str[i].isdigit():
            i += 1
        if i > 0:
            num = int(take_str[:i]) + 1
            return f"{num}{take_str[i:]}"
        return "1"

    @staticmethod
    def increment_roll(roll_str: str) -> str:
        """Port of incrementRoll() in eink_clap.ino:818-850."""
        if not roll_str:
            return "A001"
        roll_str = str(roll_str)
        num_start = -1
        for i, c in enumerate(roll_str):
            if c.isdigit():
                num_start = i
                break

        if num_start != -1:
            num_end = num_start
            while num_end < len(roll_str) and roll_str[num_end].isdigit():
                num_end += 1
            prefix = roll_str[:num_start]
            num_str = roll_str[num_start:num_end]
            suffix = roll_str[num_end:]
            num_len = len(num_str)
            next_num = int(num_str) + 1
            return f"{prefix}{next_num:0{num_len}d}{suffix}"
        elif len(roll_str) == 1 and roll_str.isalpha():
            upper_c = roll_str.upper()
            if upper_c == "Z":
                return "A"
            return chr(ord(upper_c) + 1)
        else:
            return "A001"

    @classmethod
    def execute_opcode(cls, state: ClapperboardState, opcode_bytes: bytes) -> tuple[bool, str]:
        if not opcode_bytes:
            return (False, "Empty opcode")
        op = opcode_bytes[0]
        if op == 0x01:  # INCREMENT_TAKE
            state.take = cls.increment_take(state.take)
            return (True, "TAKE_INCREMENTED")
        elif op == 0x02:  # INCREMENT_ROLL
            state.roll = cls.increment_roll(state.roll)
            return (True, "ROLL_INCREMENTED")
        elif op == 0x03:  # TOGGLE_INVERT
            state.invert = not state.invert
            return (True, "INVERT_TOGGLED")
        elif op == 0x04:  # FORCE_REFRESH
            return (True, "FORCE_REFRESH_FLAGGED")
        elif op == 0x05:  # DISMISS_BOOT
            return (True, "BOOT_DISMISSED")
        elif op == 0x06:  # SET_LAYOUT
            param = opcode_bytes[1] if len(opcode_bytes) > 1 else 0
            state.layout = max(0, min(2, param))
            return (True, "LAYOUT_SET")
        elif op == 0x07:  # TOGGLE_TIMECODE_RUNNING
            state.tc_running = not state.tc_running
            return (True, "TIMECODE_RUNNING_TOGGLED")
        elif op == 0x08:  # SET_TIMECODE_SLOT
            slot = opcode_bytes[1] if len(opcode_bytes) > 1 else 0
            state.tc_slot = max(0, min(2, int(slot)))
            return (True, "TIMECODE_SLOT_SET")
        elif op == 0x09:  # SET_TIMECODE_SIZE
            size = opcode_bytes[1] if len(opcode_bytes) > 1 else 1
            state.tc_size = max(0, min(2, int(size)))
            return (True, "TIMECODE_SIZE_SET")
        elif op == 0x0A:  # SET_TIMECODE_FPS
            fps_idx = opcode_bytes[1] if len(opcode_bytes) > 1 else 1
            fps_map = {0: "23.976", 1: "24", 2: "25", 3: "29.97", 4: "30"}
            state.tc_fps = fps_map.get(fps_idx, "24")
            return (True, "TIMECODE_FPS_SET")
        elif op == 0x0B:  # SET_TIMECODE_VISIBILITY
            vis = (opcode_bytes[1] != 0) if len(opcode_bytes) > 1 else True
            state.visibilities["timecode"] = vis
            return (True, "TIMECODE_VISIBILITY_SET")
        elif op == 0x0C:  # FAST_JAM_SYNC_TOD
            if len(opcode_bytes) >= 4:
                h = max(0, min(23, int(opcode_bytes[1])))
                m = max(0, min(59, int(opcode_bytes[2])))
                s = max(0, min(59, int(opcode_bytes[3])))
                state.timecode = f"{h:02d}:{m:02d}:{s:02d}"
                state.tc_running = True
                return (True, "TIMECODE_JAM_SYNC_SET")
            return (False, "INVALID_JAM_SYNC_PAYLOAD")
        return (False, f"UNKNOWN_OPCODE_{op}")


# ============================================================================
# 5. HARDWARE TELEMETRY PACKERS
# ============================================================================

class ButtonTelemetryPacker:
    """3-byte button event: [EventCode: 1B][Counter: 2B Big-Endian]."""
    @staticmethod
    def pack(event_code: int, counter: int) -> bytes:
        clamped_code = max(0, min(255, int(event_code)))
        return struct.pack(">BH", clamped_code, int(counter) & 0xFFFF)

    @staticmethod
    def unpack(data: bytes) -> tuple[int, int]:
        return struct.unpack(">BH", data)


class BatteryTelemetryPacker:
    """4-byte battery telemetry: [mV: 2B Big-Endian][Percent: 1B][Flags: 1B]."""
    @staticmethod
    def calculate_percentage(voltage_v: float) -> int:
        if voltage_v >= 4.15:
            return 100
        if voltage_v <= 3.40:
            return 0
        pct = int(round((voltage_v - 3.40) / (4.15 - 3.40) * 100.0))
        return max(0, min(100, pct))

    @classmethod
    def pack(cls, voltage_mv: int) -> bytes:
        clamped_mv = max(0, min(65535, int(voltage_mv)))
        voltage_v = clamped_mv / 1000.0
        pct = cls.calculate_percentage(voltage_v)
        flags = 0
        if clamped_mv >= 4150:
            flags |= 0x01  # FLAG_USB_CONNECTED_OR_FULL
        if clamped_mv < 3550:
            flags |= 0x02  # FLAG_BATTERY_LOW
        return struct.pack(">HBB", clamped_mv, pct, flags)

    @staticmethod
    def unpack(data: bytes) -> tuple[int, int, int]:
        return struct.unpack(">HBB", data)


# ============================================================================
# 6. AUTOMATED UNIT & INTEGRATION TEST CASES
# ============================================================================

class TestBLEProtocolSpecification(unittest.TestCase):

    def setUp(self):
        self.sample_state = ClapperboardState(
            roll="A001",
            scene="35A",
            take="1",
            prod="NEBRALTA 🌸",
            director="DIRECTOR",
            dop="FELIPELOTAS1",
            note="ISO 120",
            date="25.09.2026",
            cam="A",
            sound="SYNC",
            loc="INT",
            light="DAY",
            layout=0,
            invert=False,
        )

    # ------------------------------------------------------------------------
    # TC-01: JSON Serialization & Deserialization
    # ------------------------------------------------------------------------
    def test_json_serialization_all_fields_and_visibilities(self):
        json_str = self.sample_state.to_json()
        deserialized = ClapperboardState.from_json(json_str)
        self.assertEqual(self.sample_state, deserialized)
        self.assertEqual(deserialized.roll, "A001")
        self.assertEqual(deserialized.scene, "35A")
        self.assertEqual(deserialized.take, "1")
        self.assertEqual(deserialized.director, "DIRECTOR")
        self.assertEqual(deserialized.dop, "FELIPELOTAS1")
        self.assertEqual(deserialized.note, "ISO 120")
        self.assertEqual(deserialized.date, "25.09.2026")
        self.assertEqual(deserialized.cam, "A")
        self.assertEqual(deserialized.sound, "SYNC")
        self.assertEqual(deserialized.loc, "INT")
        self.assertEqual(deserialized.light, "DAY")
        self.assertEqual(deserialized.layout, 0)
        self.assertFalse(deserialized.invert)
        self.assertTrue(deserialized.visibilities["roll"])
        self.assertTrue(deserialized.visibilities["bat"])

    # ------------------------------------------------------------------------
    # TC-02: Sakura Emoji 🌸 UTF-8 Preservation
    # ------------------------------------------------------------------------
    def test_utf8_emoji_sakura_preservation(self):
        raw_bytes = self.sample_state.to_utf8_bytes()
        sakura_utf8 = b"\xF0\x9F\x8C\xB8"
        self.assertIn(sakura_utf8, raw_bytes, "Raw Sakura UTF-8 byte sequence must be present")
        reconstructed = ClapperboardState.from_utf8_bytes(raw_bytes)
        self.assertEqual(reconstructed.prod, "NEBRALTA 🌸")

    # ------------------------------------------------------------------------
    # TC-03: Forced ALL CAPS Text Normalization
    # ------------------------------------------------------------------------
    def test_all_caps_normalization(self):
        state = ClapperboardState(
            roll="b012",
            scene="scene 4",
            take="take 3b",
            prod="film title 🌸",
            director="jane doe",
            dop="john smith",
            note="anamorphic lens",
            date="25.09.2026",
            cam="b",
            sound="sync",
            loc="ext",
            light="night",
        )
        self.assertEqual(state.roll, "B012")
        self.assertEqual(state.scene, "SCENE 4")
        self.assertEqual(state.take, "TAKE 3B")
        self.assertEqual(state.prod, "FILM TITLE 🌸")
        self.assertEqual(state.director, "JANE DOE")
        self.assertEqual(state.dop, "JOHN SMITH")
        self.assertEqual(state.note, "ANAMORPHIC LENS")
        self.assertEqual(state.cam, "B")
        self.assertEqual(state.sound, "SYNC")
        self.assertEqual(state.loc, "EXT")
        self.assertEqual(state.light, "NIGHT")

    # ------------------------------------------------------------------------
    # TC-04: Chunking & Reassembly over Legacy MTU 23
    # ------------------------------------------------------------------------
    def test_chunking_and_reassembly_mtu_23(self):
        payload = self.sample_state.to_utf8_bytes()
        self.assertGreater(len(payload), 250, "State JSON payload should be > 250 bytes")

        chunks = BLEPacketChunker.chunk_payload(payload, mtu=23)
        self.assertGreater(len(chunks), 15, "MTU 23 should produce multiple chunks")

        # Verify header structure
        total = len(chunks)
        for idx, chunk in enumerate(chunks):
            seq, tot, length = chunk[0], chunk[1], chunk[2]
            self.assertEqual(seq, idx)
            self.assertEqual(tot, total)
            self.assertEqual(length, len(chunk) - 3)
            self.assertLessEqual(len(chunk), 20, "Chunk must not exceed ATT payload (MTU 23 - 3)")

        # Reassemble
        engine = BLEReassemblyEngine()
        result = None
        for chunk in chunks:
            status, result = engine.feed_chunk(chunk)

        self.assertEqual(status, "COMPLETE")
        self.assertIsNotNone(result)
        self.assertEqual(payload, result)
        reconstructed_state = ClapperboardState.from_utf8_bytes(result)
        self.assertEqual(self.sample_state, reconstructed_state)

    # ------------------------------------------------------------------------
    # TC-05: Chunking & Reassembly over MTU 64
    # ------------------------------------------------------------------------
    def test_chunking_and_reassembly_mtu_64(self):
        payload = self.sample_state.to_utf8_bytes()
        chunks = BLEPacketChunker.chunk_payload(payload, mtu=64)
        self.assertGreater(len(chunks), 4)

        engine = BLEReassemblyEngine()
        result = None
        for chunk in chunks:
            status, result = engine.feed_chunk(chunk)
        self.assertEqual(status, "COMPLETE")
        self.assertEqual(payload, result)

    # ------------------------------------------------------------------------
    # TC-06: Chunking & Reassembly over MTU 128
    # ------------------------------------------------------------------------
    def test_chunking_and_reassembly_mtu_128(self):
        payload = self.sample_state.to_utf8_bytes()
        chunks = BLEPacketChunker.chunk_payload(payload, mtu=128)
        self.assertGreaterEqual(len(chunks), 2)

        engine = BLEReassemblyEngine()
        result = None
        for chunk in chunks:
            status, result = engine.feed_chunk(chunk)
        self.assertEqual(status, "COMPLETE")
        self.assertEqual(payload, result)

    # ------------------------------------------------------------------------
    # TC-07: Chunking & Reassembly over High MTU 512
    # ------------------------------------------------------------------------
    def test_chunking_and_reassembly_mtu_512(self):
        # 1. Compact payload (<= 240 bytes) fits in a single unfragmented chunk
        compact_payload = b'{"roll":"A001","scene":"1A","take":"1","prod":"NEBRALTA"}'
        compact_chunks = BLEPacketChunker.chunk_payload(compact_payload, mtu=512)
        self.assertEqual(len(compact_chunks), 1)
        self.assertEqual(compact_chunks[0][0], 0)
        self.assertEqual(compact_chunks[0][1], 1)
        self.assertEqual(compact_chunks[0][2], len(compact_payload))

        engine = BLEReassemblyEngine()
        status, result = engine.feed_chunk(compact_chunks[0])
        self.assertEqual(status, "COMPLETE")
        self.assertEqual(compact_payload, result)

        # 2. Full state payload (> 240 bytes) chunks using 240-byte slices under MTU 512
        full_payload = self.sample_state.to_utf8_bytes()
        self.assertGreater(len(full_payload), 240)
        chunks = BLEPacketChunker.chunk_payload(full_payload, mtu=512)
        expected_chunks = (len(full_payload) + 239) // 240
        self.assertEqual(len(chunks), expected_chunks)

        engine.reset()
        for idx, chunk in enumerate(chunks):
            self.assertEqual(chunk[0], idx)
            self.assertEqual(chunk[1], expected_chunks)
            if idx < expected_chunks - 1:
                self.assertEqual(chunk[2], 240)
            else:
                self.assertEqual(chunk[2], len(full_payload) - (expected_chunks - 1) * 240)
            status, result_full = engine.feed_chunk(chunk)
            if idx == expected_chunks - 1:
                self.assertEqual(status, "COMPLETE")
                self.assertEqual(full_payload, result_full)
            else:
                self.assertEqual(status, "IN_PROGRESS")

    # ------------------------------------------------------------------------
    # TC-08: UTF-8 Multi-Byte Emoji Split Across Chunk Boundary
    # ------------------------------------------------------------------------
    def test_utf8_emoji_split_across_chunk_boundary(self):
        """Construct a payload where the 4-byte 🌸 sequence straddles a chunk boundary."""
        sakura_utf8 = b"\xF0\x9F\x8C\xB8"
        # Create a payload of exactly 15 ASCII bytes + 4 bytes Sakura + trailing ASCII
        # Under MTU 23, slice_size = (23 - 3) - 3 = 17 bytes.
        # Bytes 0..14 (15B), byte 15..16 = first 2 bytes of Sakura, byte 17..18 = last 2 bytes of Sakura in chunk 1.
        filler_prefix = b"A" * 15
        filler_suffix = b"B" * 20
        synthetic_payload = filler_prefix + sakura_utf8 + filler_suffix

        chunks = BLEPacketChunker.chunk_payload(synthetic_payload, mtu=23)
        self.assertGreaterEqual(len(chunks), 2)

        # Confirm byte split: chunk 0 ends with \xF0\x9F, chunk 1 starts with \x8C\xB8
        chunk_0_data = chunks[0][3:]
        chunk_1_data = chunks[1][3:]
        self.assertTrue(chunk_0_data.endswith(b"\xF0\x9F"))
        self.assertTrue(chunk_1_data.startswith(b"\x8C\xB8"))

        # Reassemble
        engine = BLEReassemblyEngine()
        status_0, _ = engine.feed_chunk(chunks[0])
        self.assertEqual(status_0, "IN_PROGRESS")
        status_1, _ = engine.feed_chunk(chunks[1])
        status_final, result = engine.feed_chunk(chunks[2])

        self.assertEqual(status_final, "COMPLETE")
        self.assertEqual(synthetic_payload, result)
        # Verify decoding succeeds without UnicodeDecodeError
        decoded = result.decode("utf-8")
        self.assertIn("🌸", decoded)

    # ------------------------------------------------------------------------
    # TC-09: Out-of-Order Chunk Rejection
    # ------------------------------------------------------------------------
    def test_reassembly_out_of_order_chunk_rejection(self):
        payload = self.sample_state.to_utf8_bytes()
        chunks = BLEPacketChunker.chunk_payload(payload, mtu=23)

        engine = BLEReassemblyEngine()
        # Feed chunk 0
        engine.feed_chunk(chunks[0])
        # Feed chunk 2 instead of chunk 1
        status, result = engine.feed_chunk(chunks[2])
        self.assertEqual(status, "ERROR_OUT_OF_ORDER")
        self.assertIsNone(result)

    # ------------------------------------------------------------------------
    # TC-10: Guard Timer Expiration / Buffer Discard
    # ------------------------------------------------------------------------
    def test_reassembly_timeout_expiration(self):
        payload = self.sample_state.to_utf8_bytes()
        chunks = BLEPacketChunker.chunk_payload(payload, mtu=23)

        engine = BLEReassemblyEngine()
        t0 = 1000.0
        status_0, _ = engine.feed_chunk(chunks[0], current_time=t0)
        self.assertEqual(status_0, "IN_PROGRESS")

        # Fast forward past 3.0 seconds (e.g. 3.5s later)
        t1 = t0 + 3.5
        status_1, result = engine.feed_chunk(chunks[1], current_time=t1)
        self.assertEqual(status_1, "ERROR_TIMEOUT")
        self.assertIsNone(result)

    # ------------------------------------------------------------------------
    # TC-11: HMAC-SHA256 Challenge-Response Handshake Success
    # ------------------------------------------------------------------------
    def test_hmac_sha256_handshake_success(self):
        auth = SecurityAuthManager(shared_secret="123456789P")
        self.assertFalse(auth.session_authenticated)

        # 1. Peripheral emits challenge
        challenge = auth.generate_challenge()
        self.assertEqual(len(challenge), 17)
        self.assertEqual(challenge[0], 0x01)

        # 2. Central computes response
        response = auth.compute_client_response(challenge, secret="123456789P")
        self.assertEqual(len(response), 33)
        self.assertEqual(response[0], 0x02)

        # 3. Peripheral verifies response
        status_code, notify_payload = auth.verify_response(response)
        self.assertEqual(status_code, 0x00)
        self.assertTrue(auth.session_authenticated)
        self.assertEqual(notify_payload[0], 0x03)
        self.assertEqual(notify_payload[1], 0x00)  # SUCCESS
        self.assertEqual(len(notify_payload), 18)  # Tag + Status + 16B Token

    # ------------------------------------------------------------------------
    # TC-12: HMAC-SHA256 Challenge-Response Rejection with Invalid Key
    # ------------------------------------------------------------------------
    def test_hmac_sha256_handshake_invalid_key_rejected(self):
        auth = SecurityAuthManager(shared_secret="123456789P")
        challenge = auth.generate_challenge()

        # Rogue central uses wrong secret PIN
        rogue_response = auth.compute_client_response(challenge, secret="WRONG_SECRET")
        status_code, notify_payload = auth.verify_response(rogue_response)

        self.assertEqual(status_code, 0x01)  # AUTH_FAILED
        self.assertFalse(auth.session_authenticated)
        self.assertEqual(notify_payload[0], 0x03)
        self.assertEqual(notify_payload[1], 0x01)

    # ------------------------------------------------------------------------
    # TC-13: Security Gating & Unauthorized Write Rejection
    # ------------------------------------------------------------------------
    def test_security_gating_unauthorized_write_rejected(self):
        auth = SecurityAuthManager(shared_secret="123456789P")
        self.assertFalse(auth.session_authenticated)

        # Attempt to write to State Sync (0xC401)
        with self.assertRaises(PermissionError) as ctx:
            auth.check_write_permission(0xC401)
        self.assertEqual(ctx.exception.args[0], 0x82)  # GATT Error 0x82

        # Attempt to write to Command (0xC402)
        with self.assertRaises(PermissionError) as ctx:
            auth.check_write_permission(0xC402)
        self.assertEqual(ctx.exception.args[0], 0x82)

        # Authenticate
        challenge = auth.generate_challenge()
        response = auth.compute_client_response(challenge)
        auth.verify_response(response)
        self.assertTrue(auth.session_authenticated)

        # Now write is authorized
        self.assertTrue(auth.check_write_permission(0xC401))
        self.assertTrue(auth.check_write_permission(0xC402))

    # ------------------------------------------------------------------------
    # TC-14: Brute-Force Rate Limiting & 60s Quarantine Lockout
    # ------------------------------------------------------------------------
    def test_brute_force_lockout_after_three_failures(self):
        auth = SecurityAuthManager(shared_secret="123456789P")
        t_now = 5000.0

        # Attempt 1 (Failure)
        c1 = auth.generate_challenge()
        r1 = auth.compute_client_response(c1, secret="BAD_1")
        code1, _ = auth.verify_response(r1, current_time=t_now)
        self.assertEqual(code1, 0x01)

        # Attempt 2 (Failure)
        c2 = auth.generate_challenge()
        r2 = auth.compute_client_response(c2, secret="BAD_2")
        code2, _ = auth.verify_response(r2, current_time=t_now + 1.0)
        self.assertEqual(code2, 0x01)

        # Attempt 3 (Failure -> Triggers Lockout)
        c3 = auth.generate_challenge()
        r3 = auth.compute_client_response(c3, secret="BAD_3")
        code3, _ = auth.verify_response(r3, current_time=t_now + 2.0)
        self.assertEqual(code3, 0x01)
        self.assertEqual(auth.lockout_until, t_now + 62.0)

        # Attempt 4 within lockout window (even with CORRECT secret) -> LOCKED_OUT (0x02)
        c4 = auth.generate_challenge()
        r4 = auth.compute_client_response(c4, secret="123456789P")
        code4, notify4 = auth.verify_response(r4, current_time=t_now + 10.0)
        self.assertEqual(code4, 0x02)  # LOCKED_OUT
        self.assertEqual(notify4[1], 0x02)
        self.assertFalse(auth.session_authenticated)

        # Attempt 5 after lockout expired (e.g. 65s later) -> SUCCESS
        c5 = auth.generate_challenge()
        r5 = auth.compute_client_response(c5, secret="123456789P")
        code5, _ = auth.verify_response(r5, current_time=t_now + 70.0)
        self.assertEqual(code5, 0x00)
        self.assertTrue(auth.session_authenticated)

    # ------------------------------------------------------------------------
    # TC-15: Exclusive Single-Central Connection Lock
    # ------------------------------------------------------------------------
    def test_exclusive_connection_lock(self):
        lock = ConnectionLockManager()
        self.assertTrue(lock.is_advertising)

        # Central 1 connects
        conn1 = lock.connect("CENTRAL_IPHONE_1")
        self.assertTrue(conn1)
        self.assertFalse(lock.is_advertising, "Advertising must halt on active connection")

        # Rogue Central 2 attempts connection
        conn2 = lock.connect("CENTRAL_IPHONE_2")
        self.assertFalse(conn2, "Secondary connection must be rejected")
        self.assertFalse(lock.is_advertising)

        # Central 1 disconnects
        disc1 = lock.disconnect("CENTRAL_IPHONE_1")
        self.assertTrue(disc1)
        self.assertTrue(lock.is_advertising, "Advertising must resume upon disconnection")

    # ------------------------------------------------------------------------
    # TC-16: Command Opcodes & Alphanumeric Increments
    # ------------------------------------------------------------------------
    def test_command_opcodes_and_smart_increments(self):
        state = ClapperboardState(roll="A001", take="1", invert=False, layout=0)

        # Opcode 0x01: TAKE +1 ("1" -> "2")
        ok, msg = CommandProcessor.execute_opcode(state, bytes([0x01]))
        self.assertTrue(ok)
        self.assertEqual(state.take, "2")

        # Alphanumeric TAKE increment ("4B" -> "5B")
        state.take = "4B"
        CommandProcessor.execute_opcode(state, bytes([0x01]))
        self.assertEqual(state.take, "5B")

        # Opcode 0x02: ROLL +1 ("A001" -> "A002")
        ok, msg = CommandProcessor.execute_opcode(state, bytes([0x02]))
        self.assertTrue(ok)
        self.assertEqual(state.roll, "A002")

        # Alphanumeric single-letter ROLL ("A" -> "B", "Z" -> "A")
        state.roll = "A"
        CommandProcessor.execute_opcode(state, bytes([0x02]))
        self.assertEqual(state.roll, "B")
        state.roll = "Z"
        CommandProcessor.execute_opcode(state, bytes([0x02]))
        self.assertEqual(state.roll, "A")

        # Opcode 0x03: Toggle Invert
        self.assertFalse(state.invert)
        CommandProcessor.execute_opcode(state, bytes([0x03]))
        self.assertTrue(state.invert)

        # Opcode 0x06: Set Layout
        CommandProcessor.execute_opcode(state, bytes([0x06, 0x02]))
        self.assertEqual(state.layout, 2)

    # ------------------------------------------------------------------------
    # TC-17: Button Event Telemetry Packing & Unpacking
    # ------------------------------------------------------------------------
    def test_button_event_telemetry_packing(self):
        # KEY1 TAKE (0x01), counter 42
        packed_take = ButtonTelemetryPacker.pack(0x01, 42)
        self.assertEqual(len(packed_take), 3)
        code, counter = ButtonTelemetryPacker.unpack(packed_take)
        self.assertEqual(code, 0x01)
        self.assertEqual(counter, 42)

        # KEY2 ROLL (0x02), counter 65535 (uint16 boundary)
        packed_roll = ButtonTelemetryPacker.pack(0x02, 65535)
        code_r, counter_r = ButtonTelemetryPacker.unpack(packed_roll)
        self.assertEqual(code_r, 0x02)
        self.assertEqual(counter_r, 65535)

    # ------------------------------------------------------------------------
    # TC-18: Battery Telemetry Encoding & Flags
    # ------------------------------------------------------------------------
    def test_battery_telemetry_encoding_and_flags(self):
        # 1. Nominal battery (4000 mV -> ~80%, no flags)
        nominal_data = BatteryTelemetryPacker.pack(4000)
        self.assertEqual(len(nominal_data), 4)
        mv, pct, flags = BatteryTelemetryPacker.unpack(nominal_data)
        self.assertEqual(mv, 4000)
        self.assertEqual(pct, 80)
        self.assertEqual(flags, 0x00)

        # 2. USB charging / full (4200 mV -> 100%, FLAG_USB_CONNECTED_OR_FULL set)
        usb_data = BatteryTelemetryPacker.pack(4200)
        mv_u, pct_u, flags_u = BatteryTelemetryPacker.unpack(usb_data)
        self.assertEqual(mv_u, 4200)
        self.assertEqual(pct_u, 100)
        self.assertEqual(flags_u & 0x01, 0x01)

        # 3. Low battery (3500 mV -> 13%, FLAG_BATTERY_LOW set)
        low_data = BatteryTelemetryPacker.pack(3500)
        mv_l, pct_l, flags_l = BatteryTelemetryPacker.unpack(low_data)
        self.assertEqual(mv_l, 3500)
        self.assertEqual(pct_l, 13)
        self.assertEqual(flags_l & 0x02, 0x02)

        # 4. Critical cutoff (3300 mV -> 0%)
        crit_data = BatteryTelemetryPacker.pack(3300)
        mv_c, pct_c, flags_c = BatteryTelemetryPacker.unpack(crit_data)
        self.assertEqual(pct_c, 0)
        self.assertEqual(flags_c & 0x02, 0x02)

    # ------------------------------------------------------------------------
    # TC-19: Timecode State Serialization & Default Parameters
    # ------------------------------------------------------------------------
    def test_timecode_state_serialization_and_defaults(self):
        # 1. Verify defaults
        state = ClapperboardState()
        self.assertEqual(state.timecode, "00:00:00")
        self.assertEqual(state.tc_slot, 0)
        self.assertEqual(state.tc_size, 1)
        self.assertEqual(state.tc_fps, "24")
        self.assertEqual(state.tc_offset, 0)
        self.assertTrue(state.tc_running)
        self.assertTrue(state.visibilities["timecode"])

        # 2. Verify custom parameters serialize to JSON
        custom_state = ClapperboardState(
            timecode="14:30:15",
            tc_slot=2,
            tc_size=0,
            tc_fps="29.97",
            tc_offset=12,
            tc_running=False,
            visibilities={"timecode": False}
        )
        data = custom_state.to_dict()
        self.assertEqual(data["timecode"], "14:30:15")
        self.assertEqual(data["tc_slot"], 2)
        self.assertEqual(data["tc_size"], 0)
        self.assertEqual(data["tc_fps"], "29.97")
        self.assertEqual(data["tc_offset"], 12)
        self.assertFalse(data["tc_running"])
        self.assertFalse(data["vis"]["timecode"])

        # 3. Round-trip JSON deserialization
        json_str = custom_state.to_json()
        restored = ClapperboardState.from_json(json_str)
        self.assertEqual(custom_state, restored)
        self.assertEqual(restored.timecode, "14:30:15")
        self.assertEqual(restored.tc_slot, 2)
        self.assertEqual(restored.tc_size, 0)
        self.assertEqual(restored.tc_fps, "29.97")
        self.assertEqual(restored.tc_offset, 12)
        self.assertFalse(restored.tc_running)
        self.assertFalse(restored.visibilities["timecode"])

    # ------------------------------------------------------------------------
    # TC-20: BLE Command Opcode 0x07 (Toggle Timecode Running)
    # ------------------------------------------------------------------------
    def test_ble_command_opcode_0x07_toggle_timecode(self):
        state = ClapperboardState(tc_running=True)
        # First toggle: True -> False
        ok, msg = CommandProcessor.execute_opcode(state, bytes([0x07]))
        self.assertTrue(ok)
        self.assertEqual(msg, "TIMECODE_RUNNING_TOGGLED")
        self.assertFalse(state.tc_running)

        # Second toggle: False -> True
        ok, msg = CommandProcessor.execute_opcode(state, bytes([0x07]))
        self.assertTrue(ok)
        self.assertTrue(state.tc_running)

    # ------------------------------------------------------------------------
    # TC-21: BLE Command Opcode 0x08 (Set Timecode Slot)
    # ------------------------------------------------------------------------
    def test_ble_command_opcode_0x08_set_timecode_slot(self):
        state = ClapperboardState(tc_slot=0)
        # Slot 0: Top Header
        ok, msg = CommandProcessor.execute_opcode(state, bytes([0x08, 0x00]))
        self.assertTrue(ok)
        self.assertEqual(state.tc_slot, 0)

        # Slot 1: Lower Bar
        ok, msg = CommandProcessor.execute_opcode(state, bytes([0x08, 0x01]))
        self.assertTrue(ok)
        self.assertEqual(state.tc_slot, 1)

        # Slot 2: Center Hero
        ok, msg = CommandProcessor.execute_opcode(state, bytes([0x08, 0x02]))
        self.assertTrue(ok)
        self.assertEqual(state.tc_slot, 2)

        # Clamping out-of-bounds slot (e.g. 5 -> clamped to 2)
        ok, msg = CommandProcessor.execute_opcode(state, bytes([0x08, 0x05]))
        self.assertTrue(ok)
        self.assertEqual(state.tc_slot, 2)

    # ------------------------------------------------------------------------
    # TC-22: BLE Command Opcode 0x09 (Set Timecode Size)
    # ------------------------------------------------------------------------
    def test_ble_command_opcode_0x09_set_timecode_size(self):
        state = ClapperboardState(tc_size=1)
        # Size 0: Medium (24px / TextSize 3)
        ok, msg = CommandProcessor.execute_opcode(state, bytes([0x09, 0x00]))
        self.assertTrue(ok)
        self.assertEqual(state.tc_size, 0)

        # Size 1: Large (40px / TextSize 5)
        ok, msg = CommandProcessor.execute_opcode(state, bytes([0x09, 0x01]))
        self.assertTrue(ok)
        self.assertEqual(state.tc_size, 1)

        # Size 2: Giant XL (64px / TextSize 8)
        ok, msg = CommandProcessor.execute_opcode(state, bytes([0x09, 0x02]))
        self.assertTrue(ok)
        self.assertEqual(state.tc_size, 2)

        # Clamping out-of-bounds size (e.g. 99 -> clamped to 2)
        ok, msg = CommandProcessor.execute_opcode(state, bytes([0x09, 0x63]))
        self.assertTrue(ok)
        self.assertEqual(state.tc_size, 2)

    # ------------------------------------------------------------------------
    # TC-23: BLE Command Opcode 0x0A (Set Timecode FPS Profile)
    # ------------------------------------------------------------------------
    def test_ble_command_opcode_0x0A_set_timecode_fps(self):
        state = ClapperboardState(tc_fps="24")
        # 0: 23.976 fps
        ok, msg = CommandProcessor.execute_opcode(state, bytes([0x0A, 0x00]))
        self.assertTrue(ok)
        self.assertEqual(state.tc_fps, "23.976")

        # 1: 24 fps
        ok, msg = CommandProcessor.execute_opcode(state, bytes([0x0A, 0x01]))
        self.assertTrue(ok)
        self.assertEqual(state.tc_fps, "24")

        # 2: 25 fps
        ok, msg = CommandProcessor.execute_opcode(state, bytes([0x0A, 0x02]))
        self.assertTrue(ok)
        self.assertEqual(state.tc_fps, "25")

        # 3: 29.97 fps
        ok, msg = CommandProcessor.execute_opcode(state, bytes([0x0A, 0x03]))
        self.assertTrue(ok)
        self.assertEqual(state.tc_fps, "29.97")

        # 4: 30 fps
        ok, msg = CommandProcessor.execute_opcode(state, bytes([0x0A, 0x04]))
        self.assertTrue(ok)
        self.assertEqual(state.tc_fps, "30")

        # Unknown index fallback to "24"
        ok, msg = CommandProcessor.execute_opcode(state, bytes([0x0A, 0x09]))
        self.assertTrue(ok)
        self.assertEqual(state.tc_fps, "24")

    # ------------------------------------------------------------------------
    # TC-24: BLE Command Opcode 0x0B (Set Timecode Visibility)
    # ------------------------------------------------------------------------
    def test_ble_command_opcode_0x0B_set_timecode_visibility(self):
        state = ClapperboardState()
        self.assertTrue(state.visibilities["timecode"])

        # Set Off (0x00)
        ok, msg = CommandProcessor.execute_opcode(state, bytes([0x0B, 0x00]))
        self.assertTrue(ok)
        self.assertEqual(msg, "TIMECODE_VISIBILITY_SET")
        self.assertFalse(state.visibilities["timecode"])

        # Set On (0x01)
        ok, msg = CommandProcessor.execute_opcode(state, bytes([0x0B, 0x01]))
        self.assertTrue(ok)
        self.assertTrue(state.visibilities["timecode"])

    # ------------------------------------------------------------------------
    # TC-25: BLE Command Opcode 0x0C (Fast Jam-Sync TOD)
    # ------------------------------------------------------------------------
    def test_ble_command_opcode_0x0C_fast_jam_sync_tod(self):
        state = ClapperboardState(timecode="00:00:00", tc_running=False)

        # Jam sync to 18:45:30 -> [0x0C, 18, 45, 30]
        packet = bytes([0x0C, 18, 45, 30])
        ok, msg = CommandProcessor.execute_opcode(state, packet)
        self.assertTrue(ok)
        self.assertEqual(msg, "TIMECODE_JAM_SYNC_SET")
        self.assertEqual(state.timecode, "18:45:30")
        self.assertTrue(state.tc_running, "Jam-Sync must automatically activate running state")

        # Boundary midnight: 00:00:00
        ok, _ = CommandProcessor.execute_opcode(state, bytes([0x0C, 0, 0, 0]))
        self.assertTrue(ok)
        self.assertEqual(state.timecode, "00:00:00")

        # Boundary end of day: 23:59:59
        ok, _ = CommandProcessor.execute_opcode(state, bytes([0x0C, 23, 59, 59]))
        self.assertTrue(ok)
        self.assertEqual(state.timecode, "23:59:59")

        # Clamping out-of-range values: 25:99:99 -> clamped to 23:59:59
        ok, _ = CommandProcessor.execute_opcode(state, bytes([0x0C, 25, 99, 99]))
        self.assertTrue(ok)
        self.assertEqual(state.timecode, "23:59:59")

        # Malformed packet (< 4 bytes)
        ok, msg = CommandProcessor.execute_opcode(state, bytes([0x0C, 12]))
        self.assertFalse(ok)
        self.assertEqual(msg, "INVALID_JAM_SYNC_PAYLOAD")

    # ------------------------------------------------------------------------
    # TC-26: State Chunking & Reassembly with Timecode & Sakura Emoji
    # ------------------------------------------------------------------------
    def test_chunking_reassembly_with_timecode_and_sakura_emoji(self):
        state = ClapperboardState(
            roll="B024",
            scene="42B",
            take="5",
            prod="NEBRALTA 🌸 SAKURA SPECIAL",
            director="CHRISTOPHER NOLAN",
            dop="HOYTE VAN HOYTEMA",
            note="IMAX 70MM 15-PERF",
            date="23.10.2026",
            timecode="15:20:45",
            tc_slot=2,
            tc_size=2,
            tc_fps="24",
            tc_offset=0,
            tc_running=True,
            visibilities={"timecode": True, "note": True}
        )
        payload = state.to_utf8_bytes()
        sakura_utf8 = b"\xF0\x9F\x8C\xB8"
        self.assertIn(sakura_utf8, payload)

        for mtu in [23, 64, 128, 512]:
            chunks = BLEPacketChunker.chunk_payload(payload, mtu=mtu)
            engine = BLEReassemblyEngine()
            result = None
            for chunk in chunks:
                status, result = engine.feed_chunk(chunk)
            self.assertEqual(status, "COMPLETE", f"Reassembly failed under MTU {mtu}")
            self.assertEqual(payload, result)
            reconstructed = ClapperboardState.from_utf8_bytes(result)
            self.assertEqual(state, reconstructed)
            self.assertEqual(reconstructed.timecode, "15:20:45")
            self.assertEqual(reconstructed.tc_slot, 2)
            self.assertEqual(reconstructed.tc_size, 2)
            self.assertEqual(reconstructed.tc_fps, "24")
            self.assertIn("🌸", reconstructed.prod)


# ============================================================================
# MAIN RUNNER
# ============================================================================

def main():
    print("================================================================================")
    print("  E-SLATE DIGITAL CLAPPERBOARD — BLE PROTOCOL AUTOMATED VERIFICATION SUITE")
    print("================================================================================")
    suite = unittest.TestLoader().loadTestsFromTestCase(TestBLEProtocolSpecification)
    runner = unittest.TextTestRunner(verbosity=2)
    result = runner.run(suite)
    if result.wasSuccessful():
        print(f"\n[SUCCESS] ALL {result.testsRun} PROTOCOL VERIFICATION TESTS PASSED.")
        sys.exit(0)
    else:
        print(f"\n[FAILURE] {len(result.failures)} failures, {len(result.errors)} errors.")
        sys.exit(1)


if __name__ == "__main__":
    main()
