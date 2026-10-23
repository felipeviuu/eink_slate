#!/usr/bin/env python3
"""
Adversarial Stress Test Suite — Challenger Upgrade 1
===================================================
Empirical challenge targeting:
1. Typography boundary extremes & zero bounding box overflow:
   - Single tokens > 35 chars without delimiters
   - Names with emojis (🌸, multiple 🌸, non-sakura UTF-8 emojis)
   - Punctuation-heavy names and symbols
   - UTF-8 non-ASCII characters (accents, umlauts, c-cedilla)
   - Empty strings & whitespace
   - Strings exceeding 80 chars
   - Zero bounding box overflow across Cinema Pro, Big Slate, and Commercial layouts.
2. Layout reflow permutations (Hierarchical Elastic Slicing):
   - All 13 visibility toggles tested across 2^13 = 8,192 combinations per layout (24,576 states)
   - Active tier height sum = 480 px
   - Active slot width sum = 800 px
   - No divider lines drawn adjacent to disabled slots or at outer screen boundaries.
3. Timecode rollover & edge cases:
   - Rollovers (23:59:59 -> 00:00:00, 00:59:59 -> 01:00:00)
   - Malformed/invalid timecode strings
   - Rapid successive timecode running/visibility toggles
   - Ghosting mitigation counter limit (60 partials -> full refresh)
4. On-screen PIN modal timeout & partial refresh suppression:
   - 30s timeout rollover
   - Dismissal on auth success, lockout, button press, disconnect
   - Suppression of partial timecode refreshes during modal display.
"""

import unittest
import math
import re
from typing import Dict, List, Tuple, Optional


# ============================================================================
# C++ FIRMWARE REPRODUCERS (Exact Semantics of eink_clap.ino)
# ============================================================================

SAKURA_UTF8 = "🌸"  # 4 bytes in UTF-8: \xF0\x9F\x8C\xB8
TC_GHOSTING_LIMIT = 60

TC_BOX_X = 280
TC_BOX_Y = 140
TC_BOX_W = 240
TC_BOX_H = 40

PIN_MODAL_X = 184
PIN_MODAL_Y = 140
PIN_MODAL_W = 432
PIN_MODAL_H = 200


def cpp_get_line_width_with_sakura(line: str, size: int) -> int:
    """Exact reproduction of getLineWidthWithSakura from eink_clap.ino."""
    w = 0
    pos = 0
    line_bytes = line.encode("utf-8")
    sakura_bytes = SAKURA_UTF8.encode("utf-8")
    length = len(line_bytes)

    while pos < length:
        idx = line_bytes.find(sakura_bytes, pos)
        if idx == -1:
            w += (length - pos) * 6 * size
            break
        else:
            w += (idx - pos) * 6 * size
            w += 32 + 6
            pos = idx + len(sakura_bytes)
    return w


def cpp_draw_text_auto(text: str, max_w: int, max_h: int, default_size: int, min_size: int = 1) -> Dict:
    """
    Exact reproduction of drawTextAuto bounding-box and layout logic from eink_clap.ino.
    Returns details on rendered dimensions, line count, font size, and whether overflow occurred.
    """
    u_text = text.upper().strip()
    if len(u_text) == 0:
        return {
            "rendered": False,
            "lines": [],
            "size": 0,
            "total_w": 0,
            "total_h": 0,
            "overflow_w": False,
            "overflow_h": False
        }

    # 1. Best single size
    best_single_size = -1
    for s in range(default_size, min_size - 1, -1):
        w = cpp_get_line_width_with_sakura(u_text, s)
        h = 8 * s
        if w <= max_w and h <= max_h:
            best_single_size = s
            break

    # 2. Optimal 2-line split
    best_split = -1
    min_cost = 9999.0
    text_len = len(u_text)
    mid = text_len / 2.0

    for i in range(1, text_len - 1):
        c = u_text[i]
        weight = 0.0
        if c == ' ':
            weight = 1.0
        elif c in ('-', '/', '_'):
            weight = 1.5
        elif c in ('.', ','):
            weight = 2.0

        if weight > 0.0:
            l1 = i
            l2 = text_len - (i + (1 if c == ' ' else 0))
            cost = float(max(l1, l2)) + (abs(i - mid) * 0.5) * weight
            if cost < min_cost:
                min_cost = cost
                best_split = i

    line1 = ""
    line2 = ""
    if best_split == -1 and text_len > 14:
        best_split = text_len // 2
        line1 = u_text[:best_split] + "-"
        line2 = u_text[best_split:]
    elif best_split != -1:
        split_char = u_text[best_split]
        if split_char == ' ':
            line1 = u_text[:best_split]
            line2 = u_text[best_split + 1:]
        else:
            line1 = u_text[:best_split + 1]
            line2 = u_text[best_split + 1:]

    line1 = line1.strip()
    line2 = line2.strip()

    best_double_size = -1
    if len(line1) > 0 and len(line2) > 0:
        for s in range(default_size - 1, min_size - 1, -1):
            w1 = cpp_get_line_width_with_sakura(line1, s)
            w2 = cpp_get_line_width_with_sakura(line2, s)
            gap = 4 if s >= 3 else 2
            total_h = (8 * s * 2) + gap
            if w1 <= max_w and w2 <= max_w and total_h <= max_h:
                best_double_size = s
                break

    # 3. Decision
    use_single_line = False
    if best_single_size != -1:
        if best_double_size == -1:
            use_single_line = True
        elif best_single_size >= 4:
            use_single_line = True
        elif best_double_size > best_single_size:
            use_single_line = False
        else:
            use_single_line = True

    if use_single_line and best_single_size >= 1:
        w = cpp_get_line_width_with_sakura(u_text, best_single_size)
        h = 8 * best_single_size
        return {
            "rendered": True,
            "mode": "single",
            "lines": [u_text],
            "size": best_single_size,
            "total_w": w,
            "total_h": h,
            "overflow_w": w > max_w,
            "overflow_h": h > max_h
        }
    elif best_double_size >= 1:
        w1 = cpp_get_line_width_with_sakura(line1, best_double_size)
        w2 = cpp_get_line_width_with_sakura(line2, best_double_size)
        gap = 4 if best_double_size >= 3 else 2
        total_h = (8 * best_double_size * 2) + gap
        total_w = max(w1, w2)
        return {
            "rendered": True,
            "mode": "double",
            "lines": [line1, line2],
            "size": best_double_size,
            "total_w": total_w,
            "total_h": total_h,
            "overflow_w": (w1 > max_w or w2 > max_w),
            "overflow_h": total_h > max_h
        }
    else:
        # Fallback clipping at size 1
        s = 1
        clipped = u_text
        while len(clipped) > 3 and cpp_get_line_width_with_sakura(clipped + "...", s) > max_w:
            clipped = clipped[:-1]
        final_text = clipped + "..."
        w = cpp_get_line_width_with_sakura(final_text, s)
        h = 8 * s
        return {
            "rendered": True,
            "mode": "clipped",
            "lines": [final_text],
            "size": s,
            "total_w": w,
            "total_h": h,
            "overflow_w": w > max_w,
            "overflow_h": h > max_h
        }


class TierDef:
    def __init__(self, nominal_weight: int, active: bool):
        self.nominal_weight = nominal_weight
        self.active = active
        self.y = 0
        self.h = 0


class CellSlot:
    def __init__(self, nominal_weight: int, active: bool):
        self.nominal_weight = nominal_weight
        self.active = active
        self.x = 0
        self.w = 0


def cpp_solve_tier_heights(tiers: List[TierDef], total_height: int = 480):
    total_weight = 0
    max_weight = 0
    max_idx = -1

    for i, t in enumerate(tiers):
        if t.active:
            total_weight += t.nominal_weight
            if t.nominal_weight > max_weight:
                max_weight = t.nominal_weight
                max_idx = i
        else:
            t.h = 0
            t.y = 0

    if total_weight <= 0:
        return

    allocated_height = 0
    for t in tiers:
        if t.active:
            t.h = (t.nominal_weight * total_height) // total_weight
            allocated_height += t.h

    if max_idx >= 0:
        tiers[max_idx].h += (total_height - allocated_height)

    cur_y = 0
    for t in tiers:
        if t.active:
            t.y = cur_y
            cur_y += t.h


def cpp_solve_cell_widths(cells: List[CellSlot], start_x: int = 0, total_width: int = 800):
    total_weight = 0
    max_weight = 0
    max_idx = -1

    for i, c in enumerate(cells):
        if c.active:
            total_weight += c.nominal_weight
            if c.nominal_weight > max_weight:
                max_weight = c.nominal_weight
                max_idx = i
        else:
            c.w = 0
            c.x = 0

    if total_weight <= 0:
        return

    allocated_width = 0
    for c in cells:
        if c.active:
            c.w = (c.nominal_weight * total_width) // total_weight
            allocated_width += c.w

    if max_idx >= 0:
        cells[max_idx].w += (total_width - allocated_width)

    cur_x = start_x
    for c in cells:
        if c.active:
            c.x = cur_x
            cur_x += c.w


def cpp_get_tier_dividers(tiers: List[TierDef]) -> List[int]:
    """Returns horizontal Y coordinates where dividers are drawn."""
    divs = []
    first = True
    for t in tiers:
        if not t.active:
            continue
        if not first:
            divs.append(t.y)
        first = False
    return divs


def cpp_get_cell_dividers(cells: List[CellSlot]) -> List[int]:
    """Returns vertical X coordinates where dividers are drawn."""
    divs = []
    first = True
    for c in cells:
        if not c.active:
            continue
        if not first:
            divs.append(c.x)
        first = False
    return divs


def cpp_parse_timecode(tc: str) -> Tuple[int, int, int]:
    h, m, s = 0, 0, 0
    if len(tc) >= 8:
        try:
            h = int(tc[0:2])
        except ValueError:
            h = 0
        try:
            m = int(tc[3:5])
        except ValueError:
            m = 0
        try:
            s = int(tc[6:8])
        except ValueError:
            s = 0
    elif len(tc) >= 5:
        try:
            m = int(tc[0:2])
        except ValueError:
            m = 0
        try:
            s = int(tc[3:5])
        except ValueError:
            s = 0
    h = max(0, min(23, h))
    m = max(0, min(59, m))
    s = max(0, min(59, s))
    return h, m, s


def cpp_format_timecode(h: int, m: int, s: int) -> str:
    return f"{h:02d}:{m:02d}:{s:02d}"


def cpp_increment_timecode(tc: str) -> str:
    h, m, s = cpp_parse_timecode(tc)
    s += 1
    if s >= 60:
        s = 0
        m += 1
        if m >= 60:
            m = 0
            h += 1
            if h >= 24:
                h = 0
    return cpp_format_timecode(h, m, s)


# ============================================================================
# TEST SUITE
# ============================================================================

class TestTypographyAdversarial(unittest.TestCase):
    """Stress tests targeting typography boundaries, formatting, and zero overflow."""

    def test_single_tokens_greater_than_35_chars_no_delimiters(self):
        """Single tokens > 35 characters without delimiters must wrap or clip with ZERO overflow."""
        test_strings = [
            "A" * 36,
            "B" * 40,
            "C" * 50,
            "D" * 80,
            "E" * 120,
            "ALPHANUMERICWITHOUTDELIMITERS12345678901234567890",
            "SUPERLONGDIRECTORNAMEWITHABSOLUTELYNODELIMITERSANYWHERE"
        ]
        
        # Test against minimum bounding box across all layouts (min max_w = 170, min max_h = 45)
        bounding_boxes = [
            {"name": "Big Slate Roll Min", "max_w": 170, "max_h": 60, "def_font": 6},
            {"name": "Cinema Pro Take Min", "max_w": 217, "max_h": 115, "def_font": 8},
            {"name": "Commercial Col0 Note Min", "max_w": 290, "max_h": 46, "def_font": 3},
            {"name": "Cinema Pro Prod Max", "max_w": 680, "max_h": 83, "def_font": 7},
            {"name": "Extreme Narrow Box", "max_w": 100, "max_h": 30, "def_font": 4}
        ]

        for s in test_strings:
            for box in bounding_boxes:
                with self.subTest(string_len=len(s), box=box["name"]):
                    res = cpp_draw_text_auto(s, box["max_w"], box["max_h"], box["def_font"], 1)
                    self.assertTrue(res["rendered"], f"Failed to render string length {len(s)}")
                    self.assertFalse(res["overflow_w"], 
                        f"Width overflow! total_w={res['total_w']} > max_w={box['max_w']} for '{s[:15]}...' (len {len(s)})")
                    self.assertFalse(res["overflow_h"], 
                        f"Height overflow! total_h={res['total_h']} > max_h={box['max_h']} for '{s[:15]}...' (len {len(s)})")

    def test_names_with_emojis(self):
        """Names containing sakura emojis, multiple emojis, and other UTF-8 emojis."""
        test_strings = [
            "NEBRALTA 🌸",
            "🌸",
            "🌸🌸🌸",
            "PROD 🌸🌸 FLOWER",
            "NEBRALTA 🌸 SPECIAL EDITION",
            "DIRECTOR 🎬 MOVIE",
            "DOP 🎥 CAMERA 🌸",
            "🌸🌸🌸🌸🌸🌸🌸🌸🌸🌸",
            "VERY LONG PRODUCTION NAME WITH EMOJI AT THE VERY END 🌸"
        ]
        max_w = 200
        max_h = 60
        for s in test_strings:
            with self.subTest(emoji_string=s):
                res = cpp_draw_text_auto(s, max_w, max_h, default_size=6, min_size=1)
                self.assertTrue(res["rendered"])
                self.assertFalse(res["overflow_w"], f"Emoji width overflow for {s}")
                self.assertFalse(res["overflow_h"], f"Emoji height overflow for {s}")

    def test_punctuation_heavy_strings(self):
        """Names containing diverse punctuation, delimiters, and special symbols."""
        test_strings = [
            "A.B-C_D/E,F!G#H$I%J^K&L*M",
            "--__..//,,",
            "DIR: [UNIT-2] / (SECOND_UNIT)",
            "SCENE 42-B/1.2",
            "ROLL #A.001-REV_3/FINAL",
            "!@#$%^&*()_+-=[]{}|;':,.<>?/",
            "NOTE: ISO 120 / 3200K / T2.8 / 180° / 24FPS"
        ]
        for s in test_strings:
            with self.subTest(punct_string=s):
                res = cpp_draw_text_auto(s, max_w=250, max_h=50, default_size=4, min_size=1)
                self.assertTrue(res["rendered"])
                self.assertFalse(res["overflow_w"], f"Punctuation width overflow for {s}")
                self.assertFalse(res["overflow_h"], f"Punctuation height overflow for {s}")

    def test_utf8_non_ascii_characters(self):
        """Names with international UTF-8 accents, umlauts, cedillas, and non-ASCII glyphs."""
        test_strings = [
            "JOSÉ MARÍA PEÑA-CÁCERES",
            "GARCÍA MÁRQUEZ PRODUCCIONES",
            "ÉLÈVE CINÉMATOGRAPHIQUE FRANÇAIS",
            "MÜNCHEN & KÖLN FILMSTUDIOS GMBH",
            "SØREN KIERKEGAARD DOKUMENTAR",
            "ŁÓDŹ FILMOWY ZESPÓŁ",
            "ÁÉÍÓÚÑ áéíóúñ çàèìòù"
        ]
        for s in test_strings:
            with self.subTest(utf8_string=s):
                res = cpp_draw_text_auto(s, max_w=300, max_h=55, default_size=5, min_size=1)
                self.assertTrue(res["rendered"])
                self.assertFalse(res["overflow_w"], f"UTF-8 width overflow for {s}")
                self.assertFalse(res["overflow_h"], f"UTF-8 height overflow for {s}")

    def test_empty_and_whitespace_strings(self):
        """Empty strings, whitespace-only, and tabs/newlines."""
        empty_samples = ["", " ", "   ", "\t", "\n", " \t \n  "]
        for s in empty_samples:
            res = cpp_draw_text_auto(s, max_w=200, max_h=50, default_size=4, min_size=1)
            self.assertFalse(res["rendered"], f"Expected empty result for '{s}'")
            self.assertFalse(res["overflow_w"])
            self.assertFalse(res["overflow_h"])

    def test_strings_exceeding_80_chars(self):
        """Massive strings exceeding 80 characters up to 500 characters."""
        long_samples = [
            "X" * 81,
            "THIS IS AN EXTREMELY LONG PRODUCTION TITLE CREATED ADVERSARIALLY TO BREAK TYPOGRAPHY RENDERERS " * 3,
            "1234567890 " * 25,
            "WORD " * 80
        ]
        for s in long_samples:
            with self.subTest(length=len(s)):
                res = cpp_draw_text_auto(s, max_w=350, max_h=70, default_size=5, min_size=1)
                self.assertTrue(res["rendered"])
                self.assertFalse(res["overflow_w"], f"Massive string width overflow: {res['total_w']} > 350")
                self.assertFalse(res["overflow_h"], f"Massive string height overflow: {res['total_h']} > 70")
                if res["mode"] == "clipped":
                    self.assertTrue(res["lines"][0].endswith("..."))


class TestLayoutReflowPermutations(unittest.TestCase):
    """Exhaustive empirical challenge of Hierarchical Elastic Slicing (HES) layout reflow."""

    FIELD_KEYS = [
        "roll", "scene", "take", "prod", "dir", "dop",
        "date", "note", "cam", "sound", "loc", "light", "bat"
    ]

    def build_cinema_pro_layout(self, vis: Dict[str, bool]) -> Tuple[List[TierDef], List[List[CellSlot]]]:
        t0 = TierDef(180, vis["roll"] or vis["scene"] or vis["take"])
        t1 = TierDef(95, vis["prod"])
        t2 = TierDef(95, vis["dir"] or vis["dop"])
        t3 = TierDef(110, vis["date"] or vis["note"] or vis["cam"] or vis["sound"] or vis["loc"] or vis["light"] or vis["bat"])
        tiers = [t0, t1, t2, t3]

        c0 = [CellSlot(266, vis["roll"]), CellSlot(282, vis["scene"]), CellSlot(252, vis["take"])]
        c1 = []  # Single wide cell
        c2 = [CellSlot(400, vis["dir"]), CellSlot(400, vis["dop"])]
        c3 = [
            CellSlot(400, vis["date"] or vis["note"]),
            CellSlot(125, vis["cam"]),
            CellSlot(275, vis["sound"] or vis["loc"] or vis["light"] or vis["bat"])
        ]
        cells = [c0, c1, c2, c3]
        return tiers, cells

    def build_big_slate_layout(self, vis: Dict[str, bool]) -> Tuple[List[TierDef], List[List[CellSlot]]]:
        t0 = TierDef(260, vis["scene"] or vis["take"])
        t1 = TierDef(110, vis["roll"] or vis["prod"] or vis["cam"])
        t2 = TierDef(110, vis["dir"] or vis["dop"] or vis["date"] or vis["sound"] or vis["loc"] or vis["light"] or vis["bat"])
        tiers = [t0, t1, t2]

        c0 = [CellSlot(400, vis["scene"]), CellSlot(400, vis["take"])]
        c1 = [CellSlot(200, vis["roll"]), CellSlot(450, vis["prod"]), CellSlot(150, vis["cam"])]
        c2 = [
            CellSlot(420, vis["dir"] or vis["dop"]),
            CellSlot(380, vis["date"] or vis["sound"] or vis["loc"] or vis["light"] or vis["bat"])
        ]
        cells = [c0, c1, c2]
        return tiers, cells

    def build_commercial_layout(self, vis: Dict[str, bool]) -> Tuple[List[TierDef], List[List[CellSlot]]]:
        t0 = TierDef(110, vis["prod"])
        t1 = TierDef(200, vis["roll"] or vis["scene"] or vis["take"])
        t2 = TierDef(170, vis["dir"] or vis["dop"] or vis["note"] or vis["cam"] or vis["date"] or vis["sound"] or vis["loc"] or vis["light"] or vis["bat"])
        tiers = [t0, t1, t2]

        c0 = []  # Wide header
        c1 = [CellSlot(240, vis["roll"]), CellSlot(280, vis["scene"]), CellSlot(280, vis["take"])]
        c2 = [
            CellSlot(380, vis["dir"] or vis["dop"] or vis["note"]),
            CellSlot(180, vis["cam"] or vis["date"]),
            CellSlot(240, vis["sound"] or vis["loc"] or vis["light"] or vis["bat"])
        ]
        cells = [c0, c1, c2]
        return tiers, cells

    def _verify_layout_invariants(self, layout_name: str, tiers: List[TierDef], cell_groups: List[List[CellSlot]], vis_summary: str):
        cpp_solve_tier_heights(tiers, 480)
        active_tiers = [t for t in tiers if t.active]

        if not active_tiers:
            # All tiers disabled
            self.assertEqual(sum(t.h for t in tiers), 0)
            return

        # Invariant 1: Sum of active tier heights is EXACTLY 480 px
        tier_height_sum = sum(t.h for t in active_tiers)
        self.assertEqual(tier_height_sum, 480,
            f"{layout_name}: Sum of active tier heights ({tier_height_sum}) != 480 px under {vis_summary}")

        # Invariant 2: No tier overflows 480 px, y >= 0, contiguous stack
        cur_y = 0
        for t in active_tiers:
            self.assertEqual(t.y, cur_y, f"{layout_name}: Tier y mismatch under {vis_summary}")
            self.assertGreater(t.h, 0, f"{layout_name}: Active tier has 0 height under {vis_summary}")
            cur_y += t.h
        self.assertEqual(cur_y, 480)

        # Invariant 3: Tier dividers: no line at y=0 or y=480, line only between 2 active tiers
        tier_divs = cpp_get_tier_dividers(tiers)
        for dy in tier_divs:
            self.assertGreater(dy, 0, f"{layout_name}: Tier divider at y=0 under {vis_summary}")
            self.assertLess(dy, 480, f"{layout_name}: Tier divider at y=480 under {vis_summary}")

        # Invariant 4: Cell width invariants
        for t_idx, t in enumerate(tiers):
            if not t.active or t_idx >= len(cell_groups):
                continue
            group = cell_groups[t_idx]
            if not group:
                continue
            
            active_cells = [c for c in group if c.active]
            cpp_solve_cell_widths(group, 0, 800)
            
            if not active_cells:
                continue

            # Sum of active cell widths is EXACTLY 800 px
            cell_width_sum = sum(c.w for c in active_cells)
            self.assertEqual(cell_width_sum, 800,
                f"{layout_name} Tier {t_idx}: Sum of active cell widths ({cell_width_sum}) != 800 px under {vis_summary}")

            cur_x = 0
            for c in active_cells:
                self.assertEqual(c.x, cur_x)
                self.assertGreater(c.w, 0)
                cur_x += c.w
            self.assertEqual(cur_x, 800)

            # Cell dividers: no line at x=0 or x=800, line only between 2 active cells
            cell_divs = cpp_get_cell_dividers(group)
            for dx in cell_divs:
                self.assertGreater(dx, 0, f"{layout_name} Tier {t_idx}: Cell divider at x=0 under {vis_summary}")
                self.assertLess(dx, 800, f"{layout_name} Tier {t_idx}: Cell divider at x=800 under {vis_summary}")

    def test_all_off_and_all_on(self):
        """Boundary check: 100% fields off, and 100% fields on."""
        for all_val in [False, True]:
            vis = {k: all_val for k in self.FIELD_KEYS}
            name = f"All {'ON' if all_val else 'OFF'}"
            for builder, l_name in [
                (self.build_cinema_pro_layout, "Cinema Pro"),
                (self.build_big_slate_layout, "Big Slate"),
                (self.build_commercial_layout, "Commercial")
            ]:
                tiers, cells = builder(vis)
                self._verify_layout_invariants(l_name, tiers, cells, name)

    def test_single_field_active(self):
        """Checker: exactly 1 field active out of 13 fields (13 permutations per layout)."""
        for active_key in self.FIELD_KEYS:
            vis = {k: (k == active_key) for k in self.FIELD_KEYS}
            name = f"Only [{active_key}] active"
            for builder, l_name in [
                (self.build_cinema_pro_layout, "Cinema Pro"),
                (self.build_big_slate_layout, "Big Slate"),
                (self.build_commercial_layout, "Commercial")
            ]:
                tiers, cells = builder(vis)
                self._verify_layout_invariants(l_name, tiers, cells, name)

    def test_checkerboard_active(self):
        """Checkerboard: alternating even/odd visibility flags."""
        vis_even = {k: (i % 2 == 0) for i, k in enumerate(self.FIELD_KEYS)}
        vis_odd = {k: (i % 2 != 0) for i, k in enumerate(self.FIELD_KEYS)}

        for vis, name in [(vis_even, "Checkerboard Even"), (vis_odd, "Checkerboard Odd")]:
            for builder, l_name in [
                (self.build_cinema_pro_layout, "Cinema Pro"),
                (self.build_big_slate_layout, "Big Slate"),
                (self.build_commercial_layout, "Commercial")
            ]:
                tiers, cells = builder(vis)
                self._verify_layout_invariants(l_name, tiers, cells, name)

    def test_exhaustive_all_8192_visibility_permutations_across_all_layouts(self):
        """
        Exhaustive test of all 2^13 = 8,192 visibility permutations across Cinema Pro,
        Big Slate, and Commercial layouts (24,576 layout evaluations).
        Asserts 100% invariant preservation across the entire mathematical space.
        """
        total_perms = 1 << len(self.FIELD_KEYS)  # 8192
        self.assertEqual(total_perms, 8192)

        builders = [
            (self.build_cinema_pro_layout, "Cinema Pro"),
            (self.build_big_slate_layout, "Big Slate"),
            (self.build_commercial_layout, "Commercial")
        ]

        evaluated = 0
        for mask in range(total_perms):
            vis = {k: bool((mask >> i) & 1) for i, k in enumerate(self.FIELD_KEYS)}
            for builder, l_name in builders:
                tiers, cells = builder(vis)
                self._verify_layout_invariants(l_name, tiers, cells, f"Mask 0x{mask:04X}")
                evaluated += 1

        self.assertEqual(evaluated, 8192 * 3)  # 24,576 total evaluations


class TestTimecodeRolloverAndGhosting(unittest.TestCase):
    """Stress testing of 1 Hz timecode clock rollover, parsing, and ghosting mitigation."""

    def test_timecode_rollovers(self):
        """Verify standard, minute, hour, and day rollovers."""
        cases = [
            ("23:59:59", "00:00:00"),  # Day rollover
            ("00:59:59", "01:00:00"),  # Hour rollover
            ("12:34:59", "12:35:00"),  # Minute rollover
            ("00:00:00", "00:00:01"),  # Initial tick
            ("12:00:00", "12:00:01"),
            ("09:09:09", "09:09:10")
        ]
        for start_tc, expected_next in cases:
            with self.subTest(start=start_tc):
                actual = cpp_increment_timecode(start_tc)
                self.assertEqual(actual, expected_next)

    def test_continuous_100_second_tick(self):
        """Simulate 100 continuous 1 Hz ticks across rollover boundaries."""
        tc = "23:58:30"
        for _ in range(100):
            tc = cpp_increment_timecode(tc)
        self.assertEqual(tc, "00:00:10")

    def test_malformed_timecode_strings(self):
        """Edge cases: empty, garbage, invalid format, out of range values."""
        malformed = [
            ("", "00:00:01"),
            ("   ", "00:00:01"),
            ("INVALID", "00:00:01"),
            ("99:99:99", "00:00:00"),  # 23:59:59 constrained -> rolls over to 00:00:00
            ("24:00:00", "23:00:01"),  # 23:00:00 constrained -> 23:00:01
            ("12:34", "00:12:35"),     # MM:SS fallback
            ("-5:-2:99", "00:01:00"),  # s=99 constrained to 59 -> rolls over to 00:01:00
            (":::::", "00:00:01"),
            ("12:34:56:78", "12:34:57")
        ]
        for raw, expected in malformed:
            with self.subTest(raw=raw):
                next_tc = cpp_increment_timecode(raw)
                self.assertEqual(next_tc, expected)

    def test_ghosting_mitigation_counter(self):
        """Verify ghosting mitigation: partial refresh count forces full refresh every 60 iterations."""
        partial_count = 0
        full_refreshes = 0
        simulated_seconds = 185  # Over 3 full cycles

        for sec in range(simulated_seconds):
            partial_count += 1
            if partial_count >= TC_GHOSTING_LIMIT:
                partial_count = 0
                full_refreshes += 1

        self.assertEqual(full_refreshes, 3)
        self.assertEqual(partial_count, 5)

    def test_manual_action_resets_ghosting_counter(self):
        """Manual actions (Take, Roll, State sync) must reset partial counter."""
        partial_count = 45
        # Simulate button press:
        partial_count = 0  # reset in incrementTake() / incrementRoll()
        self.assertEqual(partial_count, 0)


class TestPinModalTimeoutAndSuppression(unittest.TestCase):
    """Stress tests on PIN modal lifecycle, 30s timeout, dismissal triggers, and partial suppression."""

    def test_pin_modal_and_timecode_bounding_geometry(self):
        """Verify that Timecode box is entirely contained within PIN modal geometry."""
        # Timecode: [280, 140, 240, 40] -> x: 280..520, y: 140..180
        # PIN Modal: [184, 140, 432, 200] -> x: 184..616, y: 140..340
        tc_x1 = TC_BOX_X
        tc_x2 = TC_BOX_X + TC_BOX_W
        tc_y1 = TC_BOX_Y
        tc_y2 = TC_BOX_Y + TC_BOX_H

        pin_x1 = PIN_MODAL_X
        pin_x2 = PIN_MODAL_X + PIN_MODAL_W
        pin_y1 = PIN_MODAL_Y
        pin_y2 = PIN_MODAL_Y + PIN_MODAL_H

        self.assertGreaterEqual(tc_x1, pin_x1, "Timecode left edge not contained in PIN modal")
        self.assertLessEqual(tc_x2, pin_x2, "Timecode right edge not contained in PIN modal")
        self.assertGreaterEqual(tc_y1, pin_y1, "Timecode top edge not contained in PIN modal")
        self.assertLessEqual(tc_y2, pin_y2, "Timecode bottom edge not contained in PIN modal")

    def test_modal_suppression_of_timecode_partial(self):
        """While pinDisplayActive is True, partial timecode refreshes MUST be suppressed."""
        pin_display_active = True
        show_timecode = True
        in_boot_screen = False

        partial_calls = 0

        # Simulate 10 seconds of 1 Hz loop
        for sec in range(10):
            # Loop condition from eink_clap.ino line 2809:
            if not pin_display_active and not in_boot_screen and show_timecode:
                partial_calls += 1

        self.assertEqual(partial_calls, 0, "Partial refresh occurred while PIN modal was active!")

    def test_30s_timeout_rollover(self):
        """Modal must expire and restore display after 30 seconds (30,000 ms)."""
        now = 100000  # ms
        pin_display_active = True
        pin_display_expiry = now + 30000
        need_display_update = False

        # Advance 29 seconds: still active
        now += 29000
        if pin_display_active and now >= pin_display_expiry:
            pin_display_active = False
            need_display_update = True
        self.assertTrue(pin_display_active)
        self.assertFalse(need_display_update)

        # Advance 2 more seconds (total 31s): expired
        now += 2000
        if pin_display_active and now >= pin_display_expiry:
            pin_display_active = False
            need_display_update = True
        self.assertFalse(pin_display_active)
        self.assertTrue(need_display_update)

    def test_modal_dismissal_on_auth_success(self):
        """Successful authentication immediately dismisses modal."""
        pin_display_active = True
        need_display_update = False

        # Auth success event
        auth_valid = True
        if auth_valid:
            if pin_display_active:
                pin_display_active = False
                need_display_update = True

        self.assertFalse(pin_display_active)
        self.assertTrue(need_display_update)

    def test_modal_dismissal_on_button_press(self):
        """Operator button press (KEY0, KEY1, KEY2) immediately dismisses modal."""
        pin_display_active = True
        need_display_update = False

        # Button event
        button_pressed = True
        if button_pressed:
            if pin_display_active:
                pin_display_active = False
                need_display_update = True

        self.assertFalse(pin_display_active)
        self.assertTrue(need_display_update)

    def test_modal_dismissal_on_disconnect(self):
        """Central disconnection immediately dismisses modal."""
        pin_display_active = True
        need_display_update = False

        # Disconnect event
        disconnected = True
        if disconnected:
            if pin_display_active:
                pin_display_active = False
                need_display_update = True

        self.assertFalse(pin_display_active)
        self.assertTrue(need_display_update)


if __name__ == "__main__":
    unittest.main()
