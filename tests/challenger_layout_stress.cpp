#include <iostream>
#include <vector>
#include <string>
#include <cassert>
#include <iomanip>
#include <cstdint>
#include <algorithm>

// =========================================================================
// EMPIRICAL CHALLENGER: EXHAUSTIVE DYNAMIC PARTIAL WINDOW & LAYOUT GEOMETRY
// =========================================================================

struct TierDef {
    int nominalWeight;
    bool active;
    int y;
    int h;
};

struct CellSlot {
    int nominalWeight;
    bool active;
    int x;
    int w;
};

// Exact replica of solveTierHeights from eink_clap.ino
void solveTierHeights(TierDef* tiers, int numTiers, int totalHeight = 480) {
    int totalWeight = 0;
    int maxWeight = 0;
    int maxIdx = -1;

    for (int i = 0; i < numTiers; i++) {
        if (tiers[i].active) {
            totalWeight += tiers[i].nominalWeight;
            if (tiers[i].nominalWeight > maxWeight) {
                maxWeight = tiers[i].nominalWeight;
                maxIdx = i;
            }
        } else {
            tiers[i].h = 0;
            tiers[i].y = 0;
        }
    }

    if (totalWeight <= 0) return;

    int allocatedHeight = 0;
    for (int i = 0; i < numTiers; i++) {
        if (tiers[i].active) {
            tiers[i].h = (tiers[i].nominalWeight * totalHeight) / totalWeight;
            allocatedHeight += tiers[i].h;
        }
    }

    if (maxIdx >= 0) {
        tiers[maxIdx].h += (totalHeight - allocatedHeight);
    }

    int curY = 0;
    for (int i = 0; i < numTiers; i++) {
        if (tiers[i].active) {
            tiers[i].y = curY;
            curY += tiers[i].h;
        }
    }
}

// Exact replica of solveCellWidths from eink_clap.ino
void solveCellWidths(CellSlot* cells, int numCells, int startX = 0, int totalWidth = 800) {
    int totalWeight = 0;
    int maxWeight = 0;
    int maxIdx = -1;

    for (int i = 0; i < numCells; i++) {
        if (cells[i].active) {
            totalWeight += cells[i].nominalWeight;
            if (cells[i].nominalWeight > maxWeight) {
                maxWeight = cells[i].nominalWeight;
                maxIdx = i;
            }
        } else {
            cells[i].w = 0;
            cells[i].x = 0;
        }
    }

    if (totalWeight <= 0) return;

    int allocatedWidth = 0;
    for (int i = 0; i < numCells; i++) {
        if (cells[i].active) {
            cells[i].w = (cells[i].nominalWeight * totalWidth) / totalWeight;
            allocatedWidth += cells[i].w;
        }
    }

    if (maxIdx >= 0) {
        cells[maxIdx].w += (totalWidth - allocatedWidth);
    }

    int curX = startX;
    for (int i = 0; i < numCells; i++) {
        if (cells[i].active) {
            cells[i].x = curX;
            curX += cells[i].w;
        }
    }
}

// Visibility struct corresponding to 14 clapperboard fields
struct Visibility {
    bool roll;
    bool scene;
    bool take;
    bool prod;
    bool dir;
    bool dop;
    bool cam;
    bool sound;
    bool loc;
    bool light;
    bool date;
    bool timecode;
    bool note;
    bool bat;
};

// Exact replica of getSlotBoundsForCurrentLayout from eink_clap.ino
void getSlotBoundsForCurrentLayout(int layoutMode, int tcSlot, int tcSize, const Visibility& v,
                                  int &slotX, int &slotY, int &slotW, int &slotH) {
    int tcTierWeight = (tcSize == 2) ? 80 : ((tcSize == 1) ? 56 : 40);

    if (layoutMode == 1) {
        // Big Slate
        TierDef tiers[5] = {
            {tcTierWeight, tcSlot == 0, 0, 0},
            {260, v.scene || v.take, 0, 0},
            {tcTierWeight, tcSlot == 2, 0, 0},
            {110, v.roll || v.prod || v.cam, 0, 0},
            {110, v.dir || v.dop || (tcSlot == 1) || v.date || v.sound || v.loc || v.light || v.bat, 0, 0}
        };
        solveTierHeights(tiers, 5, 480);
        if (tcSlot == 0) {
            slotX = 0; slotY = tiers[0].y; slotW = 800; slotH = tiers[0].h;
        } else if (tcSlot == 2) {
            slotX = 0; slotY = tiers[2].y; slotW = 800; slotH = tiers[2].h;
        } else { // tcSlot == 1
            CellSlot cells[3] = {
                {300, v.dir || v.dop, 0, 0},
                {220, true, 0, 0},
                {280, v.date || v.sound || v.loc || v.light || v.bat, 0, 0}
            };
            solveCellWidths(cells, 3, 0, 800);
            slotX = cells[1].x; slotY = tiers[4].y; slotW = cells[1].w; slotH = tiers[4].h;
        }
    } else if (layoutMode == 2) {
        // Commercial
        TierDef tiers[5] = {
            {tcTierWeight, tcSlot == 0, 0, 0},
            {110, v.prod, 0, 0},
            {tcTierWeight, tcSlot == 2, 0, 0},
            {200, v.roll || v.scene || v.take, 0, 0},
            {170, v.dir || v.dop || v.note || (tcSlot == 1) || v.cam || v.date || v.sound || v.loc || v.light || v.bat, 0, 0}
        };
        solveTierHeights(tiers, 5, 480);
        if (tcSlot == 0) {
            slotX = 0; slotY = tiers[0].y; slotW = 800; slotH = tiers[0].h;
        } else if (tcSlot == 2) {
            slotX = 0; slotY = tiers[2].y; slotW = 800; slotH = tiers[2].h;
        } else { // tcSlot == 1
            CellSlot cols[4] = {
                {280, v.dir || v.dop || v.note, 0, 0},
                {220, true, 0, 0},
                {140, v.cam || v.date, 0, 0},
                {160, v.sound || v.loc || v.light || v.bat, 0, 0}
            };
            solveCellWidths(cols, 4, 0, 800);
            slotX = cols[1].x; slotY = tiers[4].y; slotW = cols[1].w; slotH = tiers[4].h;
        }
    } else {
        // Cinema Pro (layoutMode == 0)
        TierDef tiers[6] = {
            {tcTierWeight, tcSlot == 0, 0, 0},
            {180, v.roll || v.scene || v.take, 0, 0},
            {tcTierWeight, tcSlot == 2, 0, 0},
            {95,  v.prod, 0, 0},
            {95,  v.dir || v.dop, 0, 0},
            {110, v.date || v.note || (tcSlot == 1) || v.cam || v.sound || v.loc || v.light || v.bat, 0, 0}
        };
        solveTierHeights(tiers, 6, 480);
        if (tcSlot == 0) {
            slotX = 0; slotY = tiers[0].y; slotW = 800; slotH = tiers[0].h;
        } else if (tcSlot == 2) {
            slotX = 0; slotY = tiers[2].y; slotW = 800; slotH = tiers[2].h;
        } else { // tcSlot == 1
            CellSlot techCells[4] = {
                {280, v.date || v.note, 0, 0},
                {220, true, 0, 0},
                {100, v.cam, 0, 0},
                {200, v.sound || v.loc || v.light || v.bat, 0, 0}
            };
            solveCellWidths(techCells, 4, 0, 800);
            slotX = techCells[1].x; slotY = tiers[5].y; slotW = techCells[1].w; slotH = tiers[5].h;
        }
    }
}

// Exact replica of getTimecodeBounds from eink_clap.ino
void getTimecodeBounds(int layoutMode, int tcSlot, int tcSize, const Visibility& v,
                       uint16_t &x, uint16_t &y, uint16_t &w, uint16_t &h,
                       int &outSlotX, int &outSlotY, int &outSlotW, int &outSlotH) {
    int slotX = 0, slotY = 0, slotW = 800, slotH = 480;
    getSlotBoundsForCurrentLayout(layoutMode, tcSlot, tcSize, v, slotX, slotY, slotW, slotH);

    outSlotX = slotX;
    outSlotY = slotY;
    outSlotW = slotW;
    outSlotH = slotH;

    int boxW = 288;
    int boxH = 52;
    if (tcSize == 0) {
        boxW = 192;
        boxH = 36;
    } else if (tcSize == 2) {
        boxW = 432;
        boxH = 76;
    } else {
        boxW = 288;
        boxH = 52;
    }

    if (boxW > slotW - 8 && slotW > 0) {
        if (slotW >= 200) {
            boxW = 192;
            boxH = 36;
        } else {
            boxW = 144;
            boxH = 28;
        }
    }

    if (boxH > slotH - 4 && slotH > 8) {
        boxH = slotH - 4;
    }

    int rawX = slotX + (slotW - boxW) / 2;
    int rawY = slotY + (slotH - boxH) / 2;
    if (rawX < 0) rawX = 0;
    if (rawY < 0) rawY = 0;

    uint16_t alignedX = (rawX / 8) * 8;
    uint16_t alignedW = ((boxW + (rawX - alignedX) + 7) / 8) * 8;

    if (alignedX + alignedW > 800) {
        alignedW = 800 - alignedX;
    }
    if (rawY + boxH > 480) {
        boxH = 480 - rawY;
    }

    x = alignedX;
    y = (uint16_t)rawY;
    w = alignedW;
    h = (uint16_t)boxH;
}

// Text Centering & Bounding Box Check from drawTimecodeBox in eink_clap.ino
struct TextFitResult {
    int finalSize;
    int strW;
    int strH;
    int textX;
    int textY;
    bool overflowLeft;
    bool overflowRight;
    bool overflowTop;
    bool overflowBottom;
    bool borderCollision;
};

TextFitResult checkTextFit(uint16_t x, uint16_t y, uint16_t w, uint16_t h, int tcSize) {
    int tSize = 5;
    if (tcSize == 0) tSize = 3;
    else if (tcSize == 2) tSize = 8;
    else tSize = 5;

    int strW = 48 * tSize;
    int strH = 8 * tSize;
    while (tSize > 1 && (strW > (int)w - 6 || strH > (int)h - 4)) {
        tSize--;
        strW = 48 * tSize;
        strH = 8 * tSize;
    }

    int textX = (int)x + ((int)w - strW) / 2;
    int textY = (int)y + ((int)h - strH) / 2;

    TextFitResult r;
    r.finalSize = tSize;
    r.strW = strW;
    r.strH = strH;
    r.textX = textX;
    r.textY = textY;
    r.overflowLeft = (textX < (int)x);
    r.overflowRight = (textX + strW > (int)x + (int)w);
    r.overflowTop = (textY < (int)y);
    r.overflowBottom = (textY + strH > (int)y + (int)h);
    // Double border occupies 2 pixels on each edge: x, x+1, x+w-2, x+w-1
    r.borderCollision = (textX < (int)x + 2 || textX + strW > (int)x + (int)w - 2 ||
                         textY < (int)y + 2 || textY + strH > (int)y + (int)h - 2);
    return r;
}

int main() {
    std::cout << "================================================================================" << std::endl;
    std::cout << "EMPIRICAL ADVERSARIAL STRESS TEST: DYNAMIC PARTIAL WINDOW & LAYOUT GEOMETRY" << std::endl;
    std::cout << "================================================================================" << std::endl;

    uint64_t totalTests = 0;
    uint64_t failByteAlignX = 0;
    uint64_t failByteAlignW = 0;
    uint64_t failBoundsX = 0;
    uint64_t failBoundsY = 0;
    uint64_t failBoundsW = 0;
    uint64_t failBoundsH = 0;
    uint64_t failTotalDimHeight = 0;
    uint64_t failTotalDimWidth = 0;
    uint64_t failTierOverlap = 0;
    uint64_t failCellOverlap = 0;
    uint64_t failSlotOverflowTop = 0;
    uint64_t failSlotOverflowBottom = 0;
    uint64_t failSlotOverflowLeft = 0;
    uint64_t failSlotOverflowRight = 0;
    uint64_t failTextOverflow = 0;
    uint64_t failBorderCollision = 0;

    // Track examples of failures
    std::vector<std::string> slotOverflowExamples;
    std::vector<std::string> borderCollisionExamples;

    // Iterate through all 3 layouts, 3 slots, 3 sizes, 2 showTimecode toggles
    for (int layoutMode = 0; layoutMode < 3; layoutMode++) {
        for (int tcSlot = 0; tcSlot < 3; tcSlot++) {
            for (int tcSize = 0; tcSize < 3; tcSize++) {
                // 14 visibility fields: 2^14 = 16384 permutations
                for (int mask = 0; mask < 16384; mask++) {
                    Visibility v;
                    v.roll     = (mask & (1 << 0)) != 0;
                    v.scene    = (mask & (1 << 1)) != 0;
                    v.take     = (mask & (1 << 2)) != 0;
                    v.prod     = (mask & (1 << 3)) != 0;
                    v.dir      = (mask & (1 << 4)) != 0;
                    v.dop      = (mask & (1 << 5)) != 0;
                    v.cam      = (mask & (1 << 6)) != 0;
                    v.sound    = (mask & (1 << 7)) != 0;
                    v.loc      = (mask & (1 << 8)) != 0;
                    v.light    = (mask & (1 << 9)) != 0;
                    v.date     = (mask & (1 << 10)) != 0;
                    v.timecode = (mask & (1 << 11)) != 0;
                    v.note     = (mask & (1 << 12)) != 0;
                    v.bat      = (mask & (1 << 13)) != 0;

                    totalTests++;

                    // 1. PARTIAL WINDOW BOUNDS & HARDWARE BYTE ALIGNMENT
                    uint16_t tcX = 0, tcY = 0, tcW = 0, tcH = 0;
                    int slotX = 0, slotY = 0, slotW = 0, slotH = 0;
                    getTimecodeBounds(layoutMode, tcSlot, tcSize, v, tcX, tcY, tcW, tcH, slotX, slotY, slotW, slotH);

                    // Byte alignment invariants
                    if (tcX % 8 != 0) failByteAlignX++;
                    if (tcW % 8 != 0) failByteAlignW++;

                    // Screen bounds invariants
                    if (tcX < 0 || tcX > 800) failBoundsX++;
                    if (tcY < 0 || tcY > 480) failBoundsY++;
                    if (tcX + tcW > 800) failBoundsW++;
                    if (tcY + tcH > 480) failBoundsH++;

                    // Check slot containment: Does the timecode box fit in its allocated slot?
                    if (tcY < slotY) {
                        failSlotOverflowTop++;
                        if (slotOverflowExamples.size() < 5) {
                            slotOverflowExamples.push_back(
                                "Layout " + std::to_string(layoutMode) + ", Slot " + std::to_string(tcSlot) +
                                ", Size " + std::to_string(tcSize) + ": tcY=" + std::to_string(tcY) +
                                " < slotY=" + std::to_string(slotY) + " (slotH=" + std::to_string(slotH) + ", tcH=" + std::to_string(tcH) + ")"
                            );
                        }
                    }
                    if (tcY + tcH > slotY + slotH) {
                        failSlotOverflowBottom++;
                        if (slotOverflowExamples.size() < 5) {
                            slotOverflowExamples.push_back(
                                "Layout " + std::to_string(layoutMode) + ", Slot " + std::to_string(tcSlot) +
                                ", Size " + std::to_string(tcSize) + ": tcY+tcH=" + std::to_string(tcY + tcH) +
                                " > slotY+slotH=" + std::to_string(slotY + slotH) + " (slotH=" + std::to_string(slotH) + ", tcH=" + std::to_string(tcH) + ")"
                            );
                        }
                    }
                    if (tcX < slotX) failSlotOverflowLeft++;
                    if (tcX + tcW > slotX + slotW) failSlotOverflowRight++;

                    // 2. TEXT CENTERING & OVERFLOW
                    TextFitResult tr = checkTextFit(tcX, tcY, tcW, tcH, tcSize);
                    if (tr.overflowLeft || tr.overflowRight || tr.overflowTop || tr.overflowBottom) {
                        failTextOverflow++;
                    }
                    if (tr.borderCollision) {
                        failBorderCollision++;
                        if (borderCollisionExamples.size() < 5) {
                            borderCollisionExamples.push_back(
                                "Layout " + std::to_string(layoutMode) + ", Slot " + std::to_string(tcSlot) +
                                ", Size " + std::to_string(tcSize) + ": Box=" + std::to_string(tcW) + "x" + std::to_string(tcH) +
                                ", Text=" + std::to_string(tr.strW) + "x" + std::to_string(tr.strH) + " (size " + std::to_string(tr.finalSize) + ")"
                            );
                        }
                    }

                    // 3. DIMENSION CONSERVATION ACROSS TIERS & CELLS IN ACTUAL LAYOUT RENDERING
                    int tcTierWeight = (tcSize == 2) ? 80 : ((tcSize == 1) ? 56 : 40);

                    if (layoutMode == 0) { // Cinema Pro
                        TierDef tiers[6] = {
                            {tcTierWeight, v.timecode && (tcSlot == 0), 0, 0},
                            {180, v.roll || v.scene || v.take, 0, 0},
                            {tcTierWeight, v.timecode && (tcSlot == 2), 0, 0},
                            {95,  v.prod, 0, 0},
                            {95,  v.dir || v.dop, 0, 0},
                            {110, v.date || v.note || (v.timecode && tcSlot == 1) || v.cam || v.sound || v.loc || v.light || v.bat, 0, 0}
                        };
                        solveTierHeights(tiers, 6, 480);
                        int activeT = 0, sumH = 0;
                        int lastY = 0;
                        for (int i = 0; i < 6; i++) {
                            if (tiers[i].active) {
                                if (activeT > 0 && tiers[i].y != lastY) failTierOverlap++;
                                activeT++;
                                sumH += tiers[i].h;
                                lastY = tiers[i].y + tiers[i].h;
                            }
                        }
                        if (activeT > 0 && sumH != 480) failTotalDimHeight++;

                        // Check Cell Conservation in Tier 1
                        if (tiers[1].active) {
                            CellSlot cells[3] = {
                                {266, v.roll, 0, 0},
                                {282, v.scene, 0, 0},
                                {252, v.take, 0, 0}
                            };
                            solveCellWidths(cells, 3, 0, 800);
                            int actC = 0, sumW = 0, lastX = 0;
                            for (int c = 0; c < 3; c++) {
                                if (cells[c].active) {
                                    if (actC > 0 && cells[c].x != lastX) failCellOverlap++;
                                    actC++;
                                    sumW += cells[c].w;
                                    lastX = cells[c].x + cells[c].w;
                                }
                            }
                            if (actC > 0 && sumW != 800) failTotalDimWidth++;
                        }

                        // Check Cell Conservation in Tier 4
                        if (tiers[4].active) {
                            CellSlot cells[2] = {
                                {400, v.dir, 0, 0},
                                {400, v.dop, 0, 0}
                            };
                            solveCellWidths(cells, 2, 0, 800);
                            int actC = 0, sumW = 0, lastX = 0;
                            for (int c = 0; c < 2; c++) {
                                if (cells[c].active) {
                                    if (actC > 0 && cells[c].x != lastX) failCellOverlap++;
                                    actC++;
                                    sumW += cells[c].w;
                                    lastX = cells[c].x + cells[c].w;
                                }
                            }
                            if (actC > 0 && sumW != 800) failTotalDimWidth++;
                        }

                        // Check Cell Conservation in Tier 5
                        if (tiers[5].active) {
                            CellSlot cells[4] = {
                                {280, v.date || v.note, 0, 0},
                                {220, v.timecode && (tcSlot == 1), 0, 0},
                                {100, v.cam, 0, 0},
                                {200, v.sound || v.loc || v.light || v.bat, 0, 0}
                            };
                            solveCellWidths(cells, 4, 0, 800);
                            int actC = 0, sumW = 0, lastX = 0;
                            for (int c = 0; c < 4; c++) {
                                if (cells[c].active) {
                                    if (actC > 0 && cells[c].x != lastX) failCellOverlap++;
                                    actC++;
                                    sumW += cells[c].w;
                                    lastX = cells[c].x + cells[c].w;
                                }
                            }
                            if (actC > 0 && sumW != 800) failTotalDimWidth++;
                        }
                    } else if (layoutMode == 1) { // Big Slate
                        TierDef tiers[5] = {
                            {tcTierWeight, v.timecode && (tcSlot == 0), 0, 0},
                            {260, v.scene || v.take, 0, 0},
                            {tcTierWeight, v.timecode && (tcSlot == 2), 0, 0},
                            {110, v.roll || v.prod || v.cam, 0, 0},
                            {110, v.dir || v.dop || (v.timecode && tcSlot == 1) || v.date || v.sound || v.loc || v.light || v.bat, 0, 0}
                        };
                        solveTierHeights(tiers, 5, 480);
                        int activeT = 0, sumH = 0, lastY = 0;
                        for (int i = 0; i < 5; i++) {
                            if (tiers[i].active) {
                                if (activeT > 0 && tiers[i].y != lastY) failTierOverlap++;
                                activeT++;
                                sumH += tiers[i].h;
                                lastY = tiers[i].y + tiers[i].h;
                            }
                        }
                        if (activeT > 0 && sumH != 480) failTotalDimHeight++;

                        // Cell conservation in Tier 1
                        if (tiers[1].active) {
                            CellSlot cells[2] = {
                                {400, v.scene, 0, 0},
                                {400, v.take, 0, 0}
                            };
                            solveCellWidths(cells, 2, 0, 800);
                            int actC = 0, sumW = 0, lastX = 0;
                            for (int c = 0; c < 2; c++) {
                                if (cells[c].active) {
                                    if (actC > 0 && cells[c].x != lastX) failCellOverlap++;
                                    actC++;
                                    sumW += cells[c].w;
                                    lastX = cells[c].x + cells[c].w;
                                }
                            }
                            if (actC > 0 && sumW != 800) failTotalDimWidth++;
                        }

                        // Cell conservation in Tier 3
                        if (tiers[3].active) {
                            CellSlot cells[3] = {
                                {200, v.roll, 0, 0},
                                {450, v.prod, 0, 0},
                                {150, v.cam, 0, 0}
                            };
                            solveCellWidths(cells, 3, 0, 800);
                            int actC = 0, sumW = 0, lastX = 0;
                            for (int c = 0; c < 3; c++) {
                                if (cells[c].active) {
                                    if (actC > 0 && cells[c].x != lastX) failCellOverlap++;
                                    actC++;
                                    sumW += cells[c].w;
                                    lastX = cells[c].x + cells[c].w;
                                }
                            }
                            if (actC > 0 && sumW != 800) failTotalDimWidth++;
                        }

                        // Cell conservation in Tier 4
                        if (tiers[4].active) {
                            CellSlot cells[3] = {
                                {300, v.dir || v.dop, 0, 0},
                                {220, v.timecode && (tcSlot == 1), 0, 0},
                                {280, v.date || v.sound || v.loc || v.light || v.bat, 0, 0}
                            };
                            solveCellWidths(cells, 3, 0, 800);
                            int actC = 0, sumW = 0, lastX = 0;
                            for (int c = 0; c < 3; c++) {
                                if (cells[c].active) {
                                    if (actC > 0 && cells[c].x != lastX) failCellOverlap++;
                                    actC++;
                                    sumW += cells[c].w;
                                    lastX = cells[c].x + cells[c].w;
                                }
                            }
                            if (actC > 0 && sumW != 800) failTotalDimWidth++;
                        }
                    } else if (layoutMode == 2) { // Commercial
                        TierDef tiers[5] = {
                            {tcTierWeight, v.timecode && (tcSlot == 0), 0, 0},
                            {110, v.prod, 0, 0},
                            {tcTierWeight, v.timecode && (tcSlot == 2), 0, 0},
                            {200, v.roll || v.scene || v.take, 0, 0},
                            {170, v.dir || v.dop || v.note || (v.timecode && tcSlot == 1) || v.cam || v.date || v.sound || v.loc || v.light || v.bat, 0, 0}
                        };
                        solveTierHeights(tiers, 5, 480);
                        int activeT = 0, sumH = 0, lastY = 0;
                        for (int i = 0; i < 5; i++) {
                            if (tiers[i].active) {
                                if (activeT > 0 && tiers[i].y != lastY) failTierOverlap++;
                                activeT++;
                                sumH += tiers[i].h;
                                lastY = tiers[i].y + tiers[i].h;
                            }
                        }
                        if (activeT > 0 && sumH != 480) failTotalDimHeight++;

                        // Cell conservation in Tier 3
                        if (tiers[3].active) {
                            CellSlot cells[3] = {
                                {240, v.roll, 0, 0},
                                {280, v.scene, 0, 0},
                                {280, v.take, 0, 0}
                            };
                            solveCellWidths(cells, 3, 0, 800);
                            int actC = 0, sumW = 0, lastX = 0;
                            for (int c = 0; c < 3; c++) {
                                if (cells[c].active) {
                                    if (actC > 0 && cells[c].x != lastX) failCellOverlap++;
                                    actC++;
                                    sumW += cells[c].w;
                                    lastX = cells[c].x + cells[c].w;
                                }
                            }
                            if (actC > 0 && sumW != 800) failTotalDimWidth++;
                        }

                        // Cell conservation in Tier 4
                        if (tiers[4].active) {
                            CellSlot cells[4] = {
                                {280, v.dir || v.dop || v.note, 0, 0},
                                {220, v.timecode && (tcSlot == 1), 0, 0},
                                {140, v.cam || v.date, 0, 0},
                                {160, v.sound || v.loc || v.light || v.bat, 0, 0}
                            };
                            solveCellWidths(cells, 4, 0, 800);
                            int actC = 0, sumW = 0, lastX = 0;
                            for (int c = 0; c < 4; c++) {
                                if (cells[c].active) {
                                    if (actC > 0 && cells[c].x != lastX) failCellOverlap++;
                                    actC++;
                                    sumW += cells[c].w;
                                    lastX = cells[c].x + cells[c].w;
                                }
                            }
                            if (actC > 0 && sumW != 800) failTotalDimWidth++;
                        }
                    }
                }
            }
        }
    }

    std::cout << "Total permutations evaluated: " << totalTests << std::endl;
    std::cout << "--------------------------------------------------------------------------------" << std::endl;
    std::cout << "1. Hardware Byte Alignment Invariants:" << std::endl;
    std::cout << "   - tcX % 8 == 0 failures: " << failByteAlignX << std::endl;
    std::cout << "   - tcW % 8 == 0 failures: " << failByteAlignW << std::endl;
    std::cout << "2. Screen Bounds Invariants ([0..800] x [0..480]):" << std::endl;
    std::cout << "   - tcX < 0 or > 800: " << failBoundsX << std::endl;
    std::cout << "   - tcY < 0 or > 480: " << failBoundsY << std::endl;
    std::cout << "   - tcX + tcW > 800:  " << failBoundsW << std::endl;
    std::cout << "   - tcY + tcH > 480:  " << failBoundsH << std::endl;
    std::cout << "3. Dimension Conservation & Layout Geometry:" << std::endl;
    std::cout << "   - Total tier height != 480px: " << failTotalDimHeight << std::endl;
    std::cout << "   - Total cell width != 800px:  " << failTotalDimWidth << std::endl;
    std::cout << "   - Tier overlap / dead space:  " << failTierOverlap << std::endl;
    std::cout << "   - Cell overlap / dead space:  " << failCellOverlap << std::endl;
    std::cout << "4. Timecode Slot Containment Invariants:" << std::endl;
    std::cout << "   - tcY < slotY (Top overflow):        " << failSlotOverflowTop << std::endl;
    std::cout << "   - tcY+tcH > slotY+slotH (Bottom ovf): " << failSlotOverflowBottom << std::endl;
    std::cout << "   - tcX < slotX (Left overflow):       " << failSlotOverflowLeft << std::endl;
    std::cout << "   - tcX+tcW > slotX+slotW (Right ovf): " << failSlotOverflowRight << std::endl;
    std::cout << "5. Text Centering & Bounding Box Invariants:" << std::endl;
    std::cout << "   - Text string overflow beyond box: " << failTextOverflow << std::endl;
    std::cout << "   - Text colliding with box border:  " << failBorderCollision << std::endl;
    std::cout << "================================================================================" << std::endl;

    if (!slotOverflowExamples.empty()) {
        std::cout << "CRITICAL FINDINGS - Slot Overflow Examples:" << std::endl;
        for (const auto& ex : slotOverflowExamples) {
            std::cout << "  * " << ex << std::endl;
        }
    }

    if (!borderCollisionExamples.empty()) {
        std::cout << "Text Border Collision Examples:" << std::endl;
        for (const auto& ex : borderCollisionExamples) {
            std::cout << "  * " << ex << std::endl;
        }
    }

    return 0;
}
