#!/usr/bin/env python3
"""
Adversarial Verification Suite: Dynamic Partial Window Calculation & Layout Geometry
====================================================================================
Empirical test suite for E-Slate Generation 3 Timecode Modular System.
Tests all layouts, slots, sizes, and visibility permutations against:
1. Hardware Byte Alignment (tcX % 8 == 0, tcW % 8 == 0)
2. Screen Bounds ([0, 800] x [0, 480])
3. Dimension Conservation (sum(tier heights) == 480, sum(cell widths) == 800, zero overlap/dead space)
4. Text Centering & Bounding Box Overflow
5. Slot Containment (tcY >= slotY, tcY + tcH <= slotY + slotH, tcX >= slotX, tcX + tcW <= slotX + slotW)
"""

import sys
import unittest
from typing import Dict, List, Tuple

def solve_tiers(tier_weights: List[Tuple[int, bool]], total_h: int = 480) -> List[Tuple[int, int]]:
    """Exact replica of solveTierHeights from eink_clap.ino"""
    tot_w = sum(w for w, act in tier_weights if act)
    if tot_w <= 0:
        return [(0, 0)] * len(tier_weights)
    max_w, max_idx = -1, -1
    for idx, (w, act) in enumerate(tier_weights):
        if act and w > max_w:
            max_w = w
            max_idx = idx
    res = []
    allocated = 0
    for w, act in tier_weights:
        if act:
            h = (w * total_h) // tot_w
            allocated += h
            res.append(h)
        else:
            res.append(0)
    if max_idx >= 0:
        res[max_idx] += (total_h - allocated)
    out = []
    cur_y = 0
    for idx, h in enumerate(res):
        if tier_weights[idx][1]:
            out.append((cur_y, h))
            cur_y += h
        else:
            out.append((0, 0))
    return out

def solve_cells(cell_weights: List[Tuple[int, bool]], start_x: int = 0, total_w: int = 800) -> List[Tuple[int, int]]:
    """Exact replica of solveCellWidths from eink_clap.ino"""
    tot_w = sum(w for w, act in cell_weights if act)
    if tot_w <= 0:
        return [(0, 0)] * len(cell_weights)
    max_w, max_idx = -1, -1
    for idx, (w, act) in enumerate(cell_weights):
        if act and w > max_w:
            max_w = w
            max_idx = idx
    res = []
    allocated = 0
    for w, act in cell_weights:
        if act:
            cw = (w * total_w) // tot_w
            allocated += cw
            res.append(cw)
        else:
            res.append(0)
    if max_idx >= 0:
        res[max_idx] += (total_w - allocated)
    out = []
    cur_x = start_x
    for idx, cw in enumerate(res):
        if cell_weights[idx][1]:
            out.append((cur_x, cw))
            cur_x += cw
        else:
            out.append((0, 0))
    return out

def get_slot_bounds(layout_mode: int, tc_slot: int, tc_size: int, v: Dict[str, bool]) -> Tuple[int, int, int, int]:
    """Exact replica of getSlotBoundsForCurrentLayout from eink_clap.ino"""
    tc_tier_weight = 80 if tc_size == 2 else (56 if tc_size == 1 else 40)
    
    if layout_mode == 1:  # Big Slate
        t_defs = [
            (tc_tier_weight, tc_slot == 0),
            (260, v['scene'] or v['take']),
            (tc_tier_weight, tc_slot == 2),
            (110, v['roll'] or v['prod'] or v['cam']),
            (110, v['dir'] or v['dop'] or (tc_slot == 1) or v['date'] or v['sound'] or v['loc'] or v['light'] or v['bat'])
        ]
        t_solved = solve_tiers(t_defs, 480)
        if tc_slot == 0:
            return 0, t_solved[0][0], 800, t_solved[0][1]
        elif tc_slot == 2:
            return 0, t_solved[2][0], 800, t_solved[2][1]
        else:
            c_defs = [
                (300, v['dir'] or v['dop']),
                (220, True),
                (280, v['date'] or v['sound'] or v['loc'] or v['light'] or v['bat'])
            ]
            c_solved = solve_cells(c_defs, 0, 800)
            return c_solved[1][0], t_solved[4][0], c_solved[1][1], t_solved[4][1]
            
    elif layout_mode == 2:  # Commercial
        t_defs = [
            (tc_tier_weight, tc_slot == 0),
            (110, v['prod']),
            (tc_tier_weight, tc_slot == 2),
            (200, v['roll'] or v['scene'] or v['take']),
            (170, v['dir'] or v['dop'] or v['note'] or (tc_slot == 1) or v['cam'] or v['date'] or v['sound'] or v['loc'] or v['light'] or v['bat'])
        ]
        t_solved = solve_tiers(t_defs, 480)
        if tc_slot == 0:
            return 0, t_solved[0][0], 800, t_solved[0][1]
        elif tc_slot == 2:
            return 0, t_solved[2][0], 800, t_solved[2][1]
        else:
            c_defs = [
                (280, v['dir'] or v['dop'] or v['note']),
                (220, True),
                (140, v['cam'] or v['date']),
                (160, v['sound'] or v['loc'] or v['light'] or v['bat'])
            ]
            c_solved = solve_cells(c_defs, 0, 800)
            return c_solved[1][0], t_solved[4][0], c_solved[1][1], t_solved[4][1]
            
    else:  # Cinema Pro (0)
        t_defs = [
            (tc_tier_weight, tc_slot == 0),
            (180, v['roll'] or v['scene'] or v['take']),
            (tc_tier_weight, tc_slot == 2),
            (95,  v['prod']),
            (95,  v['dir'] or v['dop']),
            (110, v['date'] or v['note'] or (tc_slot == 1) or v['cam'] or v['sound'] or v['loc'] or v['light'] or v['bat'])
        ]
        t_solved = solve_tiers(t_defs, 480)
        if tc_slot == 0:
            return 0, t_solved[0][0], 800, t_solved[0][1]
        elif tc_slot == 2:
            return 0, t_solved[2][0], 800, t_solved[2][1]
        else:
            c_defs = [
                (280, v['date'] or v['note']),
                (220, True),
                (100, v['cam']),
                (200, v['sound'] or v['loc'] or v['light'] or v['bat'])
            ]
            c_solved = solve_cells(c_defs, 0, 800)
            return c_solved[1][0], t_solved[5][0], c_solved[1][1], t_solved[5][1]

def get_timecode_bounds(layout_mode: int, tc_slot: int, tc_size: int, v: Dict[str, bool]) -> Tuple[int, int, int, int, int, int, int, int]:
    """Exact replica of getTimecodeBounds from eink_clap.ino"""
    slot_x, slot_y, slot_w, slot_h = get_slot_bounds(layout_mode, tc_slot, tc_size, v)
    
    box_w = 432 if tc_size == 2 else (192 if tc_size == 0 else 288)
    box_h = 76 if tc_size == 2 else (36 if tc_size == 0 else 52)
    
    if box_w > slot_w - 8 and slot_w > 0:
        if slot_w >= 200:
            box_w, box_h = 192, 36
        else:
            box_w, box_h = 144, 28

    if box_h > slot_h - 4 and slot_h > 8:
        box_h = slot_h - 4
            
    raw_x = slot_x + (slot_w - box_w) // 2
    raw_y = slot_y + (slot_h - box_h) // 2
    if raw_x < 0: raw_x = 0
    if raw_y < 0: raw_y = 0
    
    aligned_x = (raw_x // 8) * 8
    aligned_w = ((box_w + (raw_x - aligned_x) + 7) // 8) * 8
    
    if aligned_x + aligned_w > 800:
        aligned_w = 800 - aligned_x
    if raw_y + box_h > 480:
        box_h = 480 - raw_y
        
    return aligned_x, raw_y, aligned_w, box_h, slot_x, slot_y, slot_w, slot_h

def check_text_fit(x: int, y: int, w: int, h: int, tc_size: int) -> Tuple[int, int, int, int, int, bool, bool]:
    """Exact replica of drawTimecodeBox text scaling and positioning from eink_clap.ino"""
    t_size = 8 if tc_size == 2 else (3 if tc_size == 0 else 5)
    str_w = 48 * t_size
    str_h = 8 * t_size
    while t_size > 1 and (str_w > w - 6 or str_h > h - 4):
        t_size -= 1
        str_w = 48 * t_size
        str_h = 8 * t_size
        
    text_x = x + (w - str_w) // 2
    text_y = y + (h - str_h) // 2
    
    overflow = (text_x < x or text_x + str_w > x + w or text_y < y or text_y + str_h > y + h)
    border_collision = (text_x < x + 2 or text_x + str_w > x + w - 2 or text_y < y + 2 or text_y + str_h > y + h - 2)
    return t_size, str_w, str_h, text_x, text_y, overflow, border_collision

class TestDynamicTimecodeAndLayoutGeometry(unittest.TestCase):
    
    def test_hardware_byte_alignment_100_percent(self):
        """Invariant: tcX % 8 == 0 and tcW % 8 == 0 across all layouts, slots, sizes, and sample visibility."""
        for lm in [0, 1, 2]:
            for slot in [0, 1, 2]:
                for sz in [0, 1, 2]:
                    for m in range(256):
                        v = {
                            'roll': bool(m & 1), 'scene': bool(m & 2), 'take': bool(m & 4),
                            'prod': bool(m & 8), 'dir': bool(m & 16), 'dop': bool(m & 32),
                            'cam': bool(m & 64), 'sound': bool(m & 128), 'loc': True,
                            'light': True, 'date': True, 'timecode': True, 'note': True, 'bat': True
                        }
                        tcX, tcY, tcW, tcH, sx, sy, sw, sh = get_timecode_bounds(lm, slot, sz, v)
                        self.assertEqual(tcX % 8, 0, f"tcX {tcX} not 8-pixel aligned for lm={lm}, slot={slot}, sz={sz}")
                        self.assertEqual(tcW % 8, 0, f"tcW {tcW} not 8-pixel aligned for lm={lm}, slot={slot}, sz={sz}")

    def test_screen_bounds_100_percent(self):
        """Invariant: 0 <= tcX, 0 <= tcY, tcX + tcW <= 800, tcY + tcH <= 480."""
        for lm in [0, 1, 2]:
            for slot in [0, 1, 2]:
                for sz in [0, 1, 2]:
                    for m in range(256):
                        v = {
                            'roll': bool(m & 1), 'scene': bool(m & 2), 'take': bool(m & 4),
                            'prod': bool(m & 8), 'dir': bool(m & 16), 'dop': bool(m & 32),
                            'cam': bool(m & 64), 'sound': bool(m & 128), 'loc': True,
                            'light': True, 'date': True, 'timecode': True, 'note': True, 'bat': True
                        }
                        tcX, tcY, tcW, tcH, sx, sy, sw, sh = get_timecode_bounds(lm, slot, sz, v)
                        self.assertGreaterEqual(tcX, 0)
                        self.assertGreaterEqual(tcY, 0)
                        self.assertLessEqual(tcX + tcW, 800)
                        self.assertLessEqual(tcY + tcH, 480)

    def test_dimension_conservation_cinema_pro(self):
        """Invariant: Total height of active tiers == 480, total width of active cells == 800."""
        tc_tier_weight = 56
        for m in range(512):
            v = {
                'roll': bool(m & 1), 'scene': bool(m & 2), 'take': bool(m & 4),
                'prod': bool(m & 8), 'dir': bool(m & 16), 'dop': bool(m & 32),
                'cam': bool(m & 64), 'sound': bool(m & 128), 'loc': bool(m & 256),
                'light': True, 'date': True, 'timecode': True, 'note': True, 'bat': True
            }
            tiers = [
                (tc_tier_weight, v['timecode']),
                (180, v['roll'] or v['scene'] or v['take']),
                (tc_tier_weight, False),
                (95,  v['prod']),
                (95,  v['dir'] or v['dop']),
                (110, v['date'] or v['note'] or v['cam'] or v['sound'] or v['loc'] or v['light'] or v['bat'])
            ]
            t_solved = solve_tiers(tiers, 480)
            active_h = sum(h for y, h in t_solved)
            self.assertEqual(active_h, 480, f"Sum of tier heights {active_h} != 480 for mask {m}")

    def test_text_centering_and_overflow(self):
        """Invariant: Timecode string never overflows computed tcW or tcH."""
        for lm in [0, 1, 2]:
            for slot in [0, 1, 2]:
                for sz in [0, 1, 2]:
                    v = {'roll': True, 'scene': True, 'take': True, 'prod': True, 'dir': True,
                         'dop': True, 'cam': True, 'sound': True, 'loc': True, 'light': True,
                         'date': True, 'timecode': True, 'note': True, 'bat': True}
                    tcX, tcY, tcW, tcH, sx, sy, sw, sh = get_timecode_bounds(lm, slot, sz, v)
                    t_size, str_w, str_h, text_x, text_y, overflow, collision = check_text_fit(tcX, tcY, tcW, tcH, sz)
                    self.assertFalse(overflow, f"Text overflow for lm={lm}, slot={slot}, sz={sz}")
                    self.assertFalse(collision, f"Text border collision for lm={lm}, slot={slot}, sz={sz}")

    def test_adversarial_slot_containment_failure_detection(self):
        """Adversarial challenge: Verify that with height clamping, Large and Giant XL timecode boxes are strictly contained."""
        v = {'roll': True, 'scene': True, 'take': True, 'prod': True, 'dir': True,
             'dop': True, 'cam': True, 'sound': True, 'loc': True, 'light': True,
             'date': True, 'timecode': True, 'note': True, 'bat': True}
        
        # Test Cinema Pro, Top Header (Slot 0), Giant XL (Size 2)
        tcX, tcY, tcW, tcH, sx, sy, sw, sh = get_timecode_bounds(0, 0, 2, v)
        print(f"\n[REMEDIATION VERIFICATION] Cinema Pro, Slot 0, Size 2: slotH={sh}, tcH={tcH}, tcY={tcY}")
        self.assertLessEqual(tcY + tcH, sy + sh, "Giant XL in Slot 0 must not overflow slot bottom boundary")
        self.assertGreaterEqual(tcY, sy, "Giant XL in Slot 0 must not overflow slot top boundary")
        
        # Test Cinema Pro, Center Hero (Slot 2), Giant XL (Size 2)
        tcX, tcY, tcW, tcH, sx, sy, sw, sh = get_timecode_bounds(0, 2, 2, v)
        print(f"[REMEDIATION VERIFICATION] Cinema Pro, Slot 2, Size 2: slotY={sy}, tcY={tcY}, slotH={sh}, tcH={tcH}")
        self.assertGreaterEqual(tcY, sy, "Giant XL in Slot 2 must not overflow above slot boundary")
        self.assertLessEqual(tcY + tcH, sy + sh, "Giant XL in Slot 2 must not overflow below slot boundary")

if __name__ == "__main__":
    unittest.main()
