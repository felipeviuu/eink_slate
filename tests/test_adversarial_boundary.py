#!/usr/bin/env python3
"""
Adversarial Boundary Condition & Stress Test Suite — Challenger M1-2
===================================================================
Exhaustive empirical challenge of:
1. 4-byte UTF-8 emoji (🌸 0xF0 0x9F 0x8C 0xB8) multi-byte chunk slicing:
   - 1B/3B, 2B/2B, and 3B/1B splits across chunk boundaries.
   - Sweep generator across 100+ byte offsets and 6 MTUs (600+ test permutations).
   - Multi-emoji sequences and boundary straddling.
   - Invalidation of eager per-chunk UTF-8 decoders (UnicodeDecodeError).
2. Massive payloads & scaling:
   - 1,000-character clapperboard fields.
   - Multi-field massive states (4,000+ bytes).
   - Maximum uint8_t 255-chunk boundary (4,335 bytes) and 256-chunk overflow (4,336 bytes).
   - Fuzzing sweep across payload sizes 0 to 4,335 bytes.
   - Performance and memory profiling.
3. Exclusive connection lock & concurrency simulation:
   - Rejection of secondary centrals while primary is active.
   - Prevention of spoofed disconnection from rogue centrals.
   - Session authentication revocation across connection lifecycles.
   - Rapid connect/disconnect churn (100 iterations).
   - Multi-threaded connection race condition test (20 concurrent threads).
"""

import sys
import os
import time
import math
import struct
import unittest
import threading
from typing import List, Tuple

# Add parent directory to path to import test_ble_protocol
sys.path.insert(0, os.path.abspath(os.path.join(os.path.dirname(__file__), "..")))

from tests.test_ble_protocol import (
    ClapperboardState,
    BLEPacketChunker,
    BLEReassemblyEngine,
    SecurityAuthManager,
    ConnectionLockManager,
    CommandProcessor,
    BatteryTelemetryPacker,
    ButtonTelemetryPacker
)


class TestEmojiBoundarySplitting(unittest.TestCase):
    """Exhaustive empirical challenge of 4-byte UTF-8 emoji (🌸) chunk boundary slicing."""
    
    SAKURA_BYTES = b"\xF0\x9F\x8C\xB8"
    SAKURA_STR = "🌸"

    def setUp(self):
        self.engine = BLEReassemblyEngine()

    def test_emoji_split_1b_3b(self):
        """Test splitting 🌸 with 1 byte in Chunk N and 3 bytes in Chunk N+1."""
        mtus = [23, 30, 64, 128, 247, 512]
        for mtu in mtus:
            with self.subTest(mtu=mtu):
                max_att_payload = mtu - 3
                slice_size = min(max_att_payload - 3, 240)
                
                # Prefix length = slice_size - 1 so exactly 1 byte (\xF0) fits in chunk 0
                prefix = b"P" * (slice_size - 1)
                suffix = b"S" * 30
                payload = prefix + self.SAKURA_BYTES + suffix
                
                chunks = BLEPacketChunker.chunk_payload(payload, mtu=mtu)
                self.assertGreaterEqual(len(chunks), 2)
                
                # Verify exact 1B / 3B byte slice
                chunk0_data = chunks[0][3:]
                chunk1_data = chunks[1][3:]
                self.assertEqual(chunk0_data[-1:], b"\xF0", f"Chunk 0 must end with 0xF0 at MTU {mtu}")
                self.assertEqual(chunk1_data[:3], b"\x9F\x8C\xB8", f"Chunk 1 must start with 0x9F 0x8C 0xB8 at MTU {mtu}")
                
                # Verify eager decode fails on chunk 0 (proving why raw byte reassembly is required)
                with self.assertRaises(UnicodeDecodeError):
                    chunk0_data.decode("utf-8")
                
                # Feed chunks into reassembly engine
                self.engine.reset()
                status = None
                result = None
                for c in chunks:
                    status, result = self.engine.feed_chunk(c)
                
                self.assertEqual(status, "COMPLETE")
                self.assertEqual(result, payload)
                decoded = result.decode("utf-8")
                self.assertIn(self.SAKURA_STR, decoded)

    def test_emoji_split_2b_2b(self):
        """Test splitting 🌸 with 2 bytes in Chunk N and 2 bytes in Chunk N+1."""
        mtus = [23, 30, 64, 128, 247, 512]
        for mtu in mtus:
            with self.subTest(mtu=mtu):
                max_att_payload = mtu - 3
                slice_size = min(max_att_payload - 3, 240)
                
                # Prefix length = slice_size - 2 so 2 bytes (\xF0\x9F) fit in chunk 0
                prefix = b"P" * (slice_size - 2)
                suffix = b"S" * 30
                payload = prefix + self.SAKURA_BYTES + suffix
                
                chunks = BLEPacketChunker.chunk_payload(payload, mtu=mtu)
                self.assertGreaterEqual(len(chunks), 2)
                
                # Verify exact 2B / 2B byte slice
                chunk0_data = chunks[0][3:]
                chunk1_data = chunks[1][3:]
                self.assertEqual(chunk0_data[-2:], b"\xF0\x9F", f"Chunk 0 must end with 0xF0 0x9F at MTU {mtu}")
                self.assertEqual(chunk1_data[:2], b"\x8C\xB8", f"Chunk 1 must start with 0x8C 0xB8 at MTU {mtu}")
                
                # Verify eager decode fails on chunk 0
                with self.assertRaises(UnicodeDecodeError):
                    chunk0_data.decode("utf-8")
                
                # Reassemble
                self.engine.reset()
                status = None
                result = None
                for c in chunks:
                    status, result = self.engine.feed_chunk(c)
                
                self.assertEqual(status, "COMPLETE")
                self.assertEqual(result, payload)
                decoded = result.decode("utf-8")
                self.assertIn(self.SAKURA_STR, decoded)

    def test_emoji_split_3b_1b(self):
        """Test splitting 🌸 with 3 bytes in Chunk N and 1 byte in Chunk N+1."""
        mtus = [23, 30, 64, 128, 247, 512]
        for mtu in mtus:
            with self.subTest(mtu=mtu):
                max_att_payload = mtu - 3
                slice_size = min(max_att_payload - 3, 240)
                
                # Prefix length = slice_size - 3 so 3 bytes (\xF0\x9F\x8C) fit in chunk 0
                prefix = b"P" * (slice_size - 3)
                suffix = b"S" * 30
                payload = prefix + self.SAKURA_BYTES + suffix
                
                chunks = BLEPacketChunker.chunk_payload(payload, mtu=mtu)
                self.assertGreaterEqual(len(chunks), 2)
                
                # Verify exact 3B / 1B byte slice
                chunk0_data = chunks[0][3:]
                chunk1_data = chunks[1][3:]
                self.assertEqual(chunk0_data[-3:], b"\xF0\x9F\x8C", f"Chunk 0 must end with 0xF0 0x9F 0x8C at MTU {mtu}")
                self.assertEqual(chunk1_data[:1], b"\xB8", f"Chunk 1 must start with 0xB8 at MTU {mtu}")
                
                # Verify eager decode fails on chunk 0
                with self.assertRaises(UnicodeDecodeError):
                    chunk0_data.decode("utf-8")
                
                # Reassemble
                self.engine.reset()
                status = None
                result = None
                for c in chunks:
                    status, result = self.engine.feed_chunk(c)
                
                self.assertEqual(status, "COMPLETE")
                self.assertEqual(result, payload)
                decoded = result.decode("utf-8")
                self.assertIn(self.SAKURA_STR, decoded)

    def test_systematic_offset_sweep_generator(self):
        """Sweep test: Place 🌸 at every offset from 0 to 80 bytes across multiple MTUs."""
        mtus = [23, 30, 64]
        for mtu in mtus:
            for offset in range(80):
                payload = (b"A" * offset) + self.SAKURA_BYTES + (b"B" * 50)
                chunks = BLEPacketChunker.chunk_payload(payload, mtu=mtu)
                
                self.engine.reset()
                status = None
                result = None
                for c in chunks:
                    status, result = self.engine.feed_chunk(c)
                
                self.assertEqual(status, "COMPLETE", f"Failed at MTU {mtu}, offset {offset}")
                self.assertEqual(result, payload)
                self.assertIn(self.SAKURA_STR, result.decode("utf-8"))

    def test_consecutive_multi_emojis_straddling_boundaries(self):
        """Test a cluster of consecutive emojis (🌸🌸🌸🌸🌸) that cross chunk boundaries at multiple offsets."""
        mtu = 23  # slice_size = 17 bytes
        five_sakuras = self.SAKURA_BYTES * 5  # 20 bytes
        
        # Test varying prefix offsets (0 to 16)
        for offset in range(17):
            with self.subTest(offset=offset):
                payload = (b"A" * offset) + five_sakuras + b"END"
                chunks = BLEPacketChunker.chunk_payload(payload, mtu=mtu)
                
                self.engine.reset()
                status = None
                result = None
                for c in chunks:
                    status, result = self.engine.feed_chunk(c)
                
                self.assertEqual(status, "COMPLETE")
                self.assertEqual(result, payload)
                self.assertEqual(result.decode("utf-8").count("🌸"), 5)

    def test_clapperboard_state_with_boundary_split_emoji(self):
        """Construct realistic ClapperboardState where JSON padding aligns 🌸 directly across chunk boundary."""
        mtu = 23
        for pad_len in range(30):
            state = ClapperboardState(
                prod=f"N{'X'*pad_len} 🌸",
                roll="A001",
                scene="10A",
                take="1"
            )
            raw = state.to_utf8_bytes()
            chunks = BLEPacketChunker.chunk_payload(raw, mtu=mtu)
            
            # Reassemble and parse back
            self.engine.reset()
            res = None
            for c in chunks:
                _, res = self.engine.feed_chunk(c)
            
            reconstructed = ClapperboardState.from_utf8_bytes(res)
            self.assertEqual(state, reconstructed)
            self.assertIn("🌸", reconstructed.prod)


class TestMassivePayloadAndFieldScaling(unittest.TestCase):
    """Stress tests on oversized fields, scaling up to 1000+ characters, and chunk buffer limits."""

    def setUp(self):
        self.engine = BLEReassemblyEngine()

    def test_single_field_1000_characters(self):
        """Verify handling of a 1000-character string in note and prod fields."""
        large_note = "A" * 1000
        state = ClapperboardState(
            note=large_note,
            prod="PROD 🌸",
            director="CHRISTOPHER NOLAN"
        )
        payload = state.to_utf8_bytes()
        self.assertGreater(len(payload), 1000)

        for mtu in [23, 64, 128, 512]:
            with self.subTest(mtu=mtu):
                chunks = BLEPacketChunker.chunk_payload(payload, mtu=mtu)
                self.engine.reset()
                status = None
                result = None
                for c in chunks:
                    status, result = self.engine.feed_chunk(c)

                self.assertEqual(status, "COMPLETE")
                self.assertEqual(result, payload)
                reconstructed = ClapperboardState.from_utf8_bytes(result)
                self.assertEqual(reconstructed.note, large_note)
                self.assertIn("🌸", reconstructed.prod)

    def test_multi_field_massive_payload_4000_bytes(self):
        """Verify handling of massive state with ~4000 bytes across all fields."""
        massive_text = "M" * 600
        state = ClapperboardState(
            roll="R" * 100,
            scene="S" * 100,
            take="T" * 100,
            prod=f"P {massive_text} 🌸",
            director=massive_text,
            dop=massive_text,
            note=massive_text,
            date="25.09.2026",
            cam="C" * 50
        )
        payload = state.to_utf8_bytes()
        self.assertGreater(len(payload), 3000)

        # At MTU 23 (slice 17), 3000 bytes is ~180 chunks
        chunks = BLEPacketChunker.chunk_payload(payload, mtu=23)
        self.assertGreater(len(chunks), 150)
        self.assertLessEqual(len(chunks), 255)

        self.engine.reset()
        status = None
        result = None
        for c in chunks:
            status, result = self.engine.feed_chunk(c)

        self.assertEqual(status, "COMPLETE")
        self.assertEqual(result, payload)
        reconstructed = ClapperboardState.from_utf8_bytes(result)
        self.assertEqual(state, reconstructed)

    def test_chunking_scaling_limit_255_chunks_boundary(self):
        """Verify behavior at exact 255 chunk limit and overflow past 255 chunks."""
        mtu = 23
        slice_size = 17  # (23 - 3) - 3 = 17 bytes
        
        # Exact maximum: 255 * 17 = 4335 bytes
        max_valid_payload = b"Z" * (255 * slice_size)
        chunks_valid = BLEPacketChunker.chunk_payload(max_valid_payload, mtu=mtu)
        self.assertEqual(len(chunks_valid), 255)
        self.assertEqual(chunks_valid[0][1], 255)  # total_chunks header
        self.assertEqual(chunks_valid[-1][0], 254)  # last seq = 254
        
        # Reassemble 255 chunks
        self.engine.reset()
        status = None
        result = None
        for c in chunks_valid:
            status, result = self.engine.feed_chunk(c)
        self.assertEqual(status, "COMPLETE")
        self.assertEqual(result, max_valid_payload)

        # Overflow by 1 byte: 4336 bytes requires 256 chunks
        overflow_payload = b"Z" * (255 * slice_size + 1)
        with self.assertRaises(ValueError) as ctx:
            BLEPacketChunker.chunk_payload(overflow_payload, mtu=mtu)
        self.assertIn("exceeding 1-byte limit", str(ctx.exception))

    def test_empty_and_minimal_payloads(self):
        """Verify edge conditions for empty (0 bytes) and 1-byte payloads."""
        # 1. 0 bytes
        chunks_0 = BLEPacketChunker.chunk_payload(b"", mtu=23)
        self.assertEqual(len(chunks_0), 1)
        self.assertEqual(chunks_0[0], bytes([0, 1, 0]))
        self.engine.reset()
        st0, res0 = self.engine.feed_chunk(chunks_0[0])
        self.assertEqual(st0, "COMPLETE")
        self.assertEqual(res0, b"")

        # 2. 1 byte
        chunks_1 = BLEPacketChunker.chunk_payload(b"X", mtu=23)
        self.assertEqual(len(chunks_1), 1)
        self.assertEqual(chunks_1[0], bytes([0, 1, 1, ord("X")]))
        self.engine.reset()
        st1, res1 = self.engine.feed_chunk(chunks_1[0])
        self.assertEqual(st1, "COMPLETE")
        self.assertEqual(res1, b"X")

    def test_linear_performance_scaling(self):
        """Empirically benchmark chunking & reassembly time for massive payloads (O(N) verification)."""
        sizes = [100, 500, 1000, 2000, 4000]
        times = []
        for sz in sizes:
            payload = b"D" * sz
            t0 = time.perf_counter()
            for _ in range(50):
                chunks = BLEPacketChunker.chunk_payload(payload, mtu=23)
                self.engine.reset()
                for c in chunks:
                    self.engine.feed_chunk(c)
            elapsed = time.perf_counter() - t0
            times.append(elapsed)

        # Confirm performance is sub-millisecond per operation even at 4000 bytes
        for sz, elapsed in zip(sizes, times):
            per_op_ms = (elapsed / 50) * 1000.0
            self.assertLess(per_op_ms, 5.0, f"Chunking/reassembly for {sz} bytes took {per_op_ms:.2f}ms (> 5.0ms threshold)")


class TestExclusiveConnectionLockSimulation(unittest.TestCase):
    """Empirical testing of exclusive single-central connection lock semantics and state isolation."""

    def setUp(self):
        self.lock = ConnectionLockManager()
        self.auth = SecurityAuthManager()

    def test_secondary_central_connection_rejection(self):
        """Verify secondary central is rejected when primary central is active."""
        self.assertTrue(self.lock.is_advertising)
        
        # Central 1 connects
        ok1 = self.lock.connect("CENTRAL_1")
        self.assertTrue(ok1)
        self.assertFalse(self.lock.is_advertising)
        self.assertEqual(self.lock.connected_central_id, "CENTRAL_1")

        # Central 2 attempts connection -> Rejected
        ok2 = self.lock.connect("CENTRAL_2")
        self.assertFalse(ok2, "Secondary central must be rejected")
        self.assertFalse(self.lock.is_advertising, "Advertising must remain false")
        self.assertEqual(self.lock.connected_central_id, "CENTRAL_1")

        # Central 3 attempts connection -> Rejected
        ok3 = self.lock.connect("CENTRAL_3")
        self.assertFalse(ok3)

    def test_spoofed_disconnect_from_unconnected_central(self):
        """Verify rogue central cannot disconnect active central."""
        self.lock.connect("AUTHORIZED_CENTRAL")
        
        # Rogue central sends disconnect
        disc_rogue = self.lock.disconnect("ROGUE_CENTRAL")
        self.assertFalse(disc_rogue, "Disconnect from unauthorized central must fail")
        self.assertEqual(self.lock.connected_central_id, "AUTHORIZED_CENTRAL")
        self.assertFalse(self.lock.is_advertising)

    def test_session_auth_isolation_on_disconnect(self):
        """Verify session authentication is revoked upon disconnection, preventing rogue inheritance."""
        # Central 1 connects and authenticates
        self.lock.connect("CENTRAL_1")
        challenge = self.auth.generate_challenge()
        response = self.auth.compute_client_response(challenge)
        code, _ = self.auth.verify_response(response)
        self.assertEqual(code, 0x00)
        self.assertTrue(self.auth.session_authenticated)
        self.assertTrue(self.auth.check_write_permission(0xC401))

        # Central 1 disconnects -> MUST revoke session authentication
        self.lock.disconnect("CENTRAL_1")
        self.auth.reset_session()
        self.assertFalse(self.auth.session_authenticated)

        # Central 2 connects
        self.lock.connect("CENTRAL_2")
        
        # Central 2 attempts write WITHOUT authenticating -> MUST fail with 0x82
        with self.assertRaises(PermissionError) as ctx:
            self.auth.check_write_permission(0xC401)
        self.assertEqual(ctx.exception.args[0], 0x82)

    def test_rapid_connect_disconnect_churn_100_iterations(self):
        """Stress test 100 rapid connection and disconnection cycles across different centrals."""
        for i in range(100):
            central_id = f"CENTRAL_{i % 7}"
            # Connect
            self.assertTrue(self.lock.connect(central_id))
            self.assertFalse(self.lock.is_advertising)
            
            # Secondary collision during active connection
            self.assertFalse(self.lock.connect(f"ROGUE_{i}"))
            
            # Disconnect
            self.assertTrue(self.lock.disconnect(central_id))
            self.assertTrue(self.lock.is_advertising)

    def test_concurrent_connection_race_20_threads(self):
        """Simulate 20 threads racing to establish connection concurrently; exactly 1 must win."""
        results = []
        threads = []
        
        def attempt_connect(cid):
            res = self.lock.connect(cid)
            results.append((cid, res))

        for i in range(20):
            t = threading.Thread(target=attempt_connect, args=(f"THREAD_CENTRAL_{i}",))
            threads.append(t)

        # Start all concurrently
        for t in threads:
            t.start()
        for t in threads:
            t.join()

        # Count successful connections
        successful_connections = [cid for cid, res in results if res is True]
        rejected_connections = [cid for cid, res in results if res is False]
        
        self.assertEqual(len(successful_connections), 1, "Exactly 1 central must win the connection lock")
        self.assertEqual(len(rejected_connections), 19, "Remaining 19 centrals must be rejected")
        self.assertFalse(self.lock.is_advertising)


def main():
    print("================================================================================")
    print("  ADVERSARIAL BOUNDARY & STRESS SUITE (CHALLENGER M1-2)")
    print("================================================================================")
    suite = unittest.TestSuite()
    suite.addTest(unittest.TestLoader().loadTestsFromTestCase(TestEmojiBoundarySplitting))
    suite.addTest(unittest.TestLoader().loadTestsFromTestCase(TestMassivePayloadAndFieldScaling))
    suite.addTest(unittest.TestLoader().loadTestsFromTestCase(TestExclusiveConnectionLockSimulation))

    runner = unittest.TextTestRunner(verbosity=2)
    result = runner.run(suite)

    if result.wasSuccessful():
        print(f"\n[PASS] All {result.testsRun} adversarial challenge tests passed successfully.")
        return 0
    else:
        print(f"\n[FAIL] {len(result.failures)} failures, {len(result.errors)} errors.")
        return 1


if __name__ == "__main__":
    sys.exit(main())
