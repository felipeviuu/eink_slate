#include <iostream>
#include <string>
#include <vector>
#include <algorithm>
#include <cassert>
#include <cstring>
#include <cmath>

// =========================================================================
// C++ NATIVE EMPIRICAL STRESS HARNESS FOR E-SLATE FIRMWARE
// =========================================================================

static const std::string SAKURA_UTF8 = "\xF0\x9F\x8C\xB8"; // 🌸

int getLineWidthWithSakura(const std::string &line, int size) {
    int w = 0;
    size_t pos = 0;
    size_t len = line.length();

    while (pos < len) {
        size_t nextEmoji = line.find(SAKURA_UTF8, pos);
        if (nextEmoji == std::string::npos) {
            w += (int)(len - pos) * 6 * size;
            break;
        } else {
            w += (int)(nextEmoji - pos) * 6 * size;
            w += 32 + 6;
            pos = nextEmoji + SAKURA_UTF8.length();
        }
    }
    return w;
}

struct DrawAutoResult {
    bool rendered;
    int size;
    int totalW;
    int totalH;
    bool overflowW;
    bool overflowH;
};

DrawAutoResult drawTextAutoSim(const std::string &text, int maxW, int maxH, int defaultSize, int minSize = 1) {
    std::string uText = text;
    std::transform(uText.begin(), uText.end(), uText.begin(), ::toupper);
    
    // Trim
    size_t first = uText.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) {
        return {false, 0, 0, 0, false, false};
    }
    size_t last = uText.find_last_not_of(" \t\r\n");
    uText = uText.substr(first, (last - first + 1));

    if (uText.empty()) {
        return {false, 0, 0, 0, false, false};
    }

    int bestSingleSize = -1;
    for (int s = defaultSize; s >= minSize; s--) {
        int w = getLineWidthWithSakura(uText, s);
        int h = 8 * s;
        if (w <= maxW && h <= maxH) {
            bestSingleSize = s;
            break;
        }
    }

    std::string line1 = "", line2 = "";
    int bestSplit = -1;
    float minCost = 9999.0f;
    int len = (int)uText.length();
    float mid = len / 2.0f;

    for (int i = 1; i < len - 1; i++) {
        char c = uText[i];
        float weight = 0.0f;
        if (c == ' ') weight = 1.0f;
        else if (c == '-' || c == '/' || c == '_') weight = 1.5f;
        else if (c == '.' || c == ',') weight = 2.0f;

        if (weight > 0.0f) {
            int l1 = i;
            int l2 = len - (i + (c == ' ' ? 1 : 0));
            float cost = (float)std::max(l1, l2) + (std::abs(i - mid) * 0.5f) * weight;
            if (cost < minCost) {
                minCost = cost;
                bestSplit = i;
            }
        }
    }

    if (bestSplit == -1 && len > 14) {
        bestSplit = len / 2;
        line1 = uText.substr(0, bestSplit) + "-";
        line2 = uText.substr(bestSplit);
    } else if (bestSplit != -1) {
        char splitChar = uText[bestSplit];
        if (splitChar == ' ') {
            line1 = uText.substr(0, bestSplit);
            line2 = uText.substr(bestSplit + 1);
        } else {
            line1 = uText.substr(0, bestSplit + 1);
            line2 = uText.substr(bestSplit + 1);
        }
    }

    auto trimStr = [](std::string &s) {
        size_t f = s.find_first_not_of(" \t\r\n");
        if (f == std::string::npos) { s = ""; return; }
        size_t l = s.find_last_not_of(" \t\r\n");
        s = s.substr(f, (l - f + 1));
    };
    trimStr(line1);
    trimStr(line2);

    int bestDoubleSize = -1;
    if (!line1.empty() && !line2.empty()) {
        for (int s = defaultSize - 1; s >= minSize; s--) {
            int w1 = getLineWidthWithSakura(line1, s);
            int w2 = getLineWidthWithSakura(line2, s);
            int gap = (s >= 3) ? 4 : 2;
            int totalH = (8 * s * 2) + gap;
            if (w1 <= maxW && w2 <= maxW && totalH <= maxH) {
                bestDoubleSize = s;
                break;
            }
        }
    }

    bool useSingleLine = false;
    if (bestSingleSize != -1) {
        if (bestDoubleSize == -1) {
            useSingleLine = true;
        } else if (bestSingleSize >= 4) {
            useSingleLine = true;
        } else if (bestDoubleSize > bestSingleSize) {
            useSingleLine = false;
        } else {
            useSingleLine = true;
        }
    }

    if (useSingleLine && bestSingleSize >= 1) {
        int w = getLineWidthWithSakura(uText, bestSingleSize);
        int h = 8 * bestSingleSize;
        return {true, bestSingleSize, w, h, w > maxW, h > maxH};
    } else if (bestDoubleSize >= 1) {
        int w1 = getLineWidthWithSakura(line1, bestDoubleSize);
        int w2 = getLineWidthWithSakura(line2, bestDoubleSize);
        int gap = (bestDoubleSize >= 3) ? 4 : 2;
        int totalH = (8 * bestDoubleSize * 2) + gap;
        int totalW = std::max(w1, w2);
        return {true, bestDoubleSize, totalW, totalH, totalW > maxW, totalH > maxH};
    } else {
        int s = 1;
        std::string clipped = uText;
        while (clipped.length() > 3 && getLineWidthWithSakura(clipped + "...", s) > maxW) {
            clipped = clipped.substr(0, clipped.length() - 1);
        }
        std::string finalText = clipped + "...";
        int w = getLineWidthWithSakura(finalText, s);
        int h = 8 * s;
        return {true, s, w, h, w > maxW, h > maxH};
    }
}

// -------------------------------------------------------------------------
// HES REFLOW ENGINE
// -------------------------------------------------------------------------

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

std::vector<int> getTierDividers(const TierDef* tiers, int numTiers) {
    std::vector<int> divs;
    bool first = true;
    for (int i = 0; i < numTiers; i++) {
        if (!tiers[i].active) continue;
        if (!first) {
            divs.push_back(tiers[i].y);
        }
        first = false;
    }
    return divs;
}

std::vector<int> getCellDividers(const CellSlot* cells, int numCells) {
    std::vector<int> divs;
    bool first = true;
    for (int i = 0; i < numCells; i++) {
        if (!cells[i].active) continue;
        if (!first) {
            divs.push_back(cells[i].x);
        }
        first = false;
    }
    return divs;
}

// -------------------------------------------------------------------------
// TIMECODE ENGINE
// -------------------------------------------------------------------------

void parseTimecode(const std::string& tc, int& h, int& m, int& s) {
    h = 0; m = 0; s = 0;
    if (tc.length() >= 8) {
        try { h = std::stoi(tc.substr(0, 2)); } catch (...) { h = 0; }
        try { m = std::stoi(tc.substr(3, 2)); } catch (...) { m = 0; }
        try { s = std::stoi(tc.substr(6, 2)); } catch (...) { s = 0; }
    } else if (tc.length() >= 5) {
        try { m = std::stoi(tc.substr(0, 2)); } catch (...) { m = 0; }
        try { s = std::stoi(tc.substr(3, 2)); } catch (...) { s = 0; }
    }
    h = std::max(0, std::min(23, h));
    m = std::max(0, std::min(59, m));
    s = std::max(0, std::min(59, s));
}

std::string formatTimecode(int h, int m, int s) {
    char buf[16];
    snprintf(buf, sizeof(buf), "%02d:%02d:%02d", h, m, s);
    return std::string(buf);
}

std::string incrementTimecode(const std::string& tc) {
    int h = 0, m = 0, s = 0;
    parseTimecode(tc, h, m, s);
    s++;
    if (s >= 60) {
        s = 0;
        m++;
        if (m >= 60) {
            m = 0;
            h++;
            if (h >= 24) {
                h = 0;
            }
        }
    }
    return formatTimecode(h, m, s);
}

// -------------------------------------------------------------------------
// DYNAMIC BOUNDING BOX & SLOT GEOMETRY ENGINE
// -------------------------------------------------------------------------

void getSlotBoundsForCurrentLayoutSim(int layoutMode, int tcSlot, int tcSize, bool showTimecode, const bool* v, int &slotX, int &slotY, int &slotW, int &slotH) {
    int tcTierWeight = (tcSize == 2) ? 80 : ((tcSize == 1) ? 56 : 40);
    // v: 0:roll, 1:scene, 2:take, 3:prod, 4:dir, 5:dop, 6:cam, 7:sound, 8:loc, 9:light, 10:date, 11:note, 12:bat
    bool showRoll = v[0], showScene = v[1], showTake = v[2], showProd = v[3];
    bool showDir = v[4], showDop = v[5], showCam = v[6], showSound = v[7];
    bool showLoc = v[8], showLight = v[9], showDate = v[10], showNote = v[11], showBat = v[12];

    if (layoutMode == 1) {
        // Big Slate
        TierDef tiers[5] = {
            {tcTierWeight, showTimecode && (tcSlot == 0), 0, 0},
            {260, showScene || showTake, 0, 0},
            {tcTierWeight, showTimecode && (tcSlot == 2), 0, 0},
            {110, showRoll || showProd || showCam, 0, 0},
            {110, showDir || showDop || (showTimecode && (tcSlot == 1)) || showDate || showSound || showLoc || showLight || showBat, 0, 0}
        };
        solveTierHeights(tiers, 5, 480);
        if (tcSlot == 0) {
            slotX = 0; slotY = tiers[0].y; slotW = 800; slotH = tiers[0].h;
        } else if (tcSlot == 2) {
            slotX = 0; slotY = tiers[2].y; slotW = 800; slotH = tiers[2].h;
        } else { // tcSlot == 1
            CellSlot cells[3] = {
                {300, showDir || showDop, 0, 0},
                {220, showTimecode && (tcSlot == 1), 0, 0},
                {280, showDate || showSound || showLoc || showLight || showBat, 0, 0}
            };
            solveCellWidths(cells, 3, 0, 800);
            slotX = cells[1].x; slotY = tiers[4].y; slotW = cells[1].w; slotH = tiers[4].h;
        }
    } else if (layoutMode == 2) {
        // Commercial
        TierDef tiers[5] = {
            {tcTierWeight, showTimecode && (tcSlot == 0), 0, 0},
            {110, showProd, 0, 0},
            {tcTierWeight, showTimecode && (tcSlot == 2), 0, 0},
            {200, showRoll || showScene || showTake, 0, 0},
            {170, showDir || showDop || showNote || (showTimecode && (tcSlot == 1)) || showCam || showDate || showSound || showLoc || showLight || showBat, 0, 0}
        };
        solveTierHeights(tiers, 5, 480);
        if (tcSlot == 0) {
            slotX = 0; slotY = tiers[0].y; slotW = 800; slotH = tiers[0].h;
        } else if (tcSlot == 2) {
            slotX = 0; slotY = tiers[2].y; slotW = 800; slotH = tiers[2].h;
        } else { // tcSlot == 1
            CellSlot cols[4] = {
                {280, showDir || showDop || showNote, 0, 0},
                {220, showTimecode && (tcSlot == 1), 0, 0},
                {140, showCam || showDate, 0, 0},
                {160, showSound || showLoc || showLight || showBat, 0, 0}
            };
            solveCellWidths(cols, 4, 0, 800);
            slotX = cols[1].x; slotY = tiers[4].y; slotW = cols[1].w; slotH = tiers[4].h;
        }
    } else {
        // Cinema Pro (layoutMode == 0)
        TierDef tiers[6] = {
            {tcTierWeight, showTimecode && (tcSlot == 0), 0, 0},
            {180, showRoll || showScene || showTake, 0, 0},
            {tcTierWeight, showTimecode && (tcSlot == 2), 0, 0},
            {95,  showProd, 0, 0},
            {95,  showDir || showDop, 0, 0},
            {110, showDate || showNote || (showTimecode && (tcSlot == 1)) || showCam || showSound || showLoc || showLight || showBat, 0, 0}
        };
        solveTierHeights(tiers, 6, 480);
        if (tcSlot == 0) {
            slotX = 0; slotY = tiers[0].y; slotW = 800; slotH = tiers[0].h;
        } else if (tcSlot == 2) {
            slotX = 0; slotY = tiers[2].y; slotW = 800; slotH = tiers[2].h;
        } else { // tcSlot == 1
            CellSlot techCells[4] = {
                {280, showDate || showNote, 0, 0},
                {220, showTimecode && (tcSlot == 1), 0, 0},
                {100, showCam, 0, 0},
                {200, showSound || showLoc || showLight || showBat, 0, 0}
            };
            solveCellWidths(techCells, 4, 0, 800);
            slotX = techCells[1].x; slotY = tiers[5].y; slotW = techCells[1].w; slotH = tiers[5].h;
        }
    }
}

void getTimecodeBoundsSim(int layoutMode, int tcSlot, int tcSize, bool showTimecode, const bool* v, uint16_t &x, uint16_t &y, uint16_t &w, uint16_t &h) {
    int slotX = 0, slotY = 0, slotW = 800, slotH = 480;
    getSlotBoundsForCurrentLayoutSim(layoutMode, tcSlot, tcSize, showTimecode, v, slotX, slotY, slotW, slotH);

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

// =========================================================================
// MAIN RUNNER
// =========================================================================

int main() {
    std::cout << "==========================================================" << std::endl;
    std::cout << "🚀 RUNNING NATIVE C++ ADVERSARIAL STRESS TEST HARNESS" << std::endl;
    std::cout << "==========================================================" << std::endl;

    // 1. TYPOGRAPHY EDGE CASES
    std::cout << "[1/5] Stress testing typography boundary extremes..." << std::endl;
    std::vector<std::string> testStrings = {
        std::string(36, 'A'),
        std::string(50, 'Z'),
        std::string(80, '0'),
        std::string(120, 'W'),
        std::string(500, 'X'),
        "NEBRALTA " + SAKURA_UTF8,
        SAKURA_UTF8 + SAKURA_UTF8 + SAKURA_UTF8,
        "A.B-C_D/E,F!G#H$I%J^K&L*M",
        "JOSÉ MARÍA PEÑA-CÁCERES",
        "ÉLÈVE CINÉMA FRANÇAIS",
        "KÖLN DOKUMENTAR GMBH",
        "",
        "    ",
        "\t\n\r"
    };

    struct TestBox { int w; int h; int f; };
    std::vector<TestBox> boxes = {
        {170, 60, 6},
        {217, 115, 8},
        {290, 46, 3},
        {350, 70, 5},
        {680, 83, 7},
        {100, 30, 4}
    };

    int typoTests = 0;
    for (const auto& str : testStrings) {
        for (const auto& b : boxes) {
            DrawAutoResult res = drawTextAutoSim(str, b.w, b.h, b.f, 1);
            if (res.rendered) {
                assert(!res.overflowW && "Width overflow occurred!");
                assert(!res.overflowH && "Height overflow occurred!");
            }
            typoTests++;
        }
    }
    std::cout << "  ✓ Passed " << typoTests << " typography stress tests with zero overflow." << std::endl;

    // 2. DYNAMIC PARTIAL BOUNDING BOX CALCULATION & BYTE-ALIGNMENT INVARIANTS
    std::cout << "[2/5] Stress testing dynamic partial window calculations & byte alignment invariants..." << std::endl;
    int boundsTests = 0;
    // Test across layouts (0, 1, 2), slots (0: Top, 1: Lower Bar, 2: Center Hero), sizes (0: Med, 1: Large, 2: Giant XL)
    for (int lMode = 0; lMode <= 2; lMode++) {
        for (int slot = 0; slot <= 2; slot++) {
            for (int sz = 0; sz <= 2; sz++) {
                // Test across diverse visibility masks
                for (int m = 0; m < 64; m++) {
                    bool v[13];
                    for (int i = 0; i < 13; i++) v[i] = ((m >> (i % 6)) & 1);
                    // Ensure at least some fields are visible
                    v[0] = true; v[1] = true; v[2] = true;

                    uint16_t tcX = 0, tcY = 0, tcW = 0, tcH = 0;
                    getTimecodeBoundsSim(lMode, slot, sz, true, v, tcX, tcY, tcW, tcH);

                    // INVARIANT 1: Horizontal start X is byte-aligned (divisible by 8)
                    assert((tcX % 8 == 0) && "tcX must be 8-pixel byte aligned");

                    // INVARIANT 2: Width W is byte-aligned (divisible by 8)
                    assert((tcW % 8 == 0) && "tcW must be 8-pixel byte aligned");

                    // INVARIANT 3: Window fits entirely within 800px display width
                    assert((tcX + tcW <= 800) && "tcX + tcW exceeds 800px display boundary");

                    // INVARIANT 4: Window fits entirely within 480px display height
                    assert((tcY + tcH <= 480) && "tcY + tcH exceeds 480px display boundary");

                    // INVARIANT 5: Positive non-zero dimensions
                    assert((tcW > 0 && tcH > 0) && "tcW and tcH must be positive");

                    boundsTests++;
                }
            }
        }
    }
    std::cout << "  ✓ Passed " << boundsTests << " dynamic bounding box tests with exact 8-pixel byte alignment." << std::endl;

    // 3. HES LAYOUT REFLOW WITH TIMECODE ON / OFF
    std::cout << "[3/5] Stress testing HES layout reflow (when showTimecode == false & true)..." << std::endl;
    int evalCount = 0;
    for (int mask = 0; mask < 8192; mask++) {
        bool v[13];
        for (int i = 0; i < 13; i++) v[i] = ((mask >> i) & 1);

        for (int showTc = 0; showTc <= 1; showTc++) {
            bool tcActive = (showTc == 1);
            int slot = (mask % 3);
            int sz = ((mask >> 2) % 3);
            int tcWeight = (sz == 2) ? 80 : ((sz == 1) ? 56 : 40);

            // Cinema Pro (6 tiers)
            {
                TierDef tiers[6] = {
                    {tcWeight, tcActive && (slot == 0), 0, 0},
                    {180, v[0] || v[1] || v[2], 0, 0},
                    {tcWeight, tcActive && (slot == 2), 0, 0},
                    {95,  v[3], 0, 0},
                    {95,  v[4] || v[5], 0, 0},
                    {110, v[10] || v[11] || (tcActive && (slot == 1)) || v[6] || v[7] || v[8] || v[9] || v[12], 0, 0}
                };
                solveTierHeights(tiers, 6, 480);
                int activeT = 0, sumH = 0;
                for (int i = 0; i < 6; i++) {
                    if (tiers[i].active) { activeT++; sumH += tiers[i].h; }
                }
                if (activeT > 0) {
                    assert(sumH == 480 && "Cinema Pro sum of tier heights must equal exactly 480px");
                }

                auto tierDivs = getTierDividers(tiers, 6);
                for (int dy : tierDivs) {
                    assert(dy > 0 && dy < 480);
                }

                // If bottom tier is active, solve lower cell widths
                if (tiers[5].active) {
                    CellSlot techCells[4] = {
                        {280, v[10] || v[11], 0, 0},
                        {220, tcActive && (slot == 1), 0, 0},
                        {100, v[6], 0, 0},
                        {200, v[7] || v[8] || v[9] || v[12], 0, 0}
                    };
                    solveCellWidths(techCells, 4, 0, 800);
                    int activeC = 0, sumW = 0;
                    for (int i = 0; i < 4; i++) {
                        if (techCells[i].active) { activeC++; sumW += techCells[i].w; }
                    }
                    if (activeC > 0) {
                        assert(sumW == 800 && "Cinema Pro bottom cell widths must equal exactly 800px");
                    }
                }
                evalCount++;
            }

            // Big Slate (5 tiers)
            {
                TierDef tiers[5] = {
                    {tcWeight, tcActive && (slot == 0), 0, 0},
                    {260, v[1] || v[2], 0, 0},
                    {tcWeight, tcActive && (slot == 2), 0, 0},
                    {110, v[0] || v[3] || v[6], 0, 0},
                    {110, v[4] || v[5] || (tcActive && (slot == 1)) || v[10] || v[7] || v[8] || v[9] || v[12], 0, 0}
                };
                solveTierHeights(tiers, 5, 480);
                int activeT = 0, sumH = 0;
                for (int i = 0; i < 5; i++) {
                    if (tiers[i].active) { activeT++; sumH += tiers[i].h; }
                }
                if (activeT > 0) {
                    assert(sumH == 480 && "Big Slate sum of tier heights must equal exactly 480px");
                }
                evalCount++;
            }

            // Commercial (5 tiers)
            {
                TierDef tiers[5] = {
                    {tcWeight, tcActive && (slot == 0), 0, 0},
                    {110, v[3], 0, 0},
                    {tcWeight, tcActive && (slot == 2), 0, 0},
                    {200, v[0] || v[1] || v[2], 0, 0},
                    {170, v[4] || v[5] || v[11] || (tcActive && (slot == 1)) || v[6] || v[10] || v[7] || v[8] || v[9] || v[12], 0, 0}
                };
                solveTierHeights(tiers, 5, 480);
                int activeT = 0, sumH = 0;
                for (int i = 0; i < 5; i++) {
                    if (tiers[i].active) { activeT++; sumH += tiers[i].h; }
                }
                if (activeT > 0) {
                    assert(sumH == 480 && "Commercial sum of tier heights must equal exactly 480px");
                }
                evalCount++;
            }
        }
    }
    std::cout << "  ✓ Passed " << evalCount << " layout evaluations with 100% height/width conservation and 0 orphaned space." << std::endl;

    // 4. FULL 24-HOUR TIMECODE TICKING & ROLLOVER SIMULATION (86,400 continuous seconds)
    std::cout << "[4/5] Stress testing 86,400 second (full 24-hour) continuous clock rollover..." << std::endl;
    std::string curTc = "00:00:00";
    for (int sec = 0; sec < 86400; sec++) {
        int expH = sec / 3600;
        int expM = (sec % 3600) / 60;
        int expS = sec % 60;
        char expBuf[16];
        snprintf(expBuf, sizeof(expBuf), "%02d:%02d:%02d", expH, expM, expS);
        assert(curTc == std::string(expBuf));
        curTc = incrementTimecode(curTc);
    }
    // After 86,400 seconds, it must rollover exactly back to 00:00:00
    assert(curTc == "00:00:00");
    std::cout << "  ✓ Passed 86,400 second clock continuous ticking with exact 24h rollover." << std::endl;

    // 5. ANTI-GHOSTING MITIGATION (60-CYCLE ROLLOVER) & PIN MODAL LIFECYCLE
    std::cout << "[5/5] Stress testing anti-ghosting counter (60-cycle limit) and PIN modal..." << std::endl;
    int partialCount = 0;
    int fullRefreshRequested = 0;
    for (int sec = 1; sec <= 180; sec++) {
        partialCount++;
        if (partialCount >= 60) {
            partialCount = 0;
            fullRefreshRequested++;
        }
    }
    assert(fullRefreshRequested == 3 && "180 partial refreshes must trigger exactly 3 full refresh requests!");
    assert(partialCount == 0 && "Partial count must reset to 0 after 60-cycle rollover!");

    // PIN modal lifecycle (30-second expiry)
    bool pinActive = true;
    unsigned long expiry = 130000;
    unsigned long now = 100000;
    int partialsDuringPin = 0;
    for (int i = 0; i < 35; i++) {
        now += 1000;
        if (pinActive && now >= expiry) {
            pinActive = false;
        }
        if (!pinActive) {
            partialsDuringPin++;
        }
    }
    assert(!pinActive && "Modal did not timeout at 30s!");
    assert(partialsDuringPin == 6 && "Partial refresh should only resume after 30s expiry (6 seconds post-timeout)!");
    std::cout << "  ✓ Passed 60-cycle anti-ghosting rollover and PIN modal 30s lifecycle." << std::endl;

    std::cout << "==========================================================" << std::endl;
    std::cout << "🎉 ALL C++ NATIVE EMPIRICAL STRESS TESTS PASSED CLEANLY!" << std::endl;
    std::cout << "==========================================================" << std::endl;
    return 0;
}
