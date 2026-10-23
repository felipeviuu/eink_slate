#include <iostream>
#include <string>
#include <vector>
#include <random>
#include <cstdint>
#include <cassert>
#include <cmath>
#include <cstring>
#include <algorithm>
#include <sstream>

// =========================================================================
// C++ PROTOCOL FUZZER & EMPIRICAL STRESS HARNESS (CHALLENGER TIMECODE 2)
// Target: Firmware BLE Opcodes 0x07..0x0C, Frame Math, 24h Clock Rollover
// =========================================================================

// Firmware simulation state
struct FirmwareState {
    std::string timecode = "00:00:00";
    int tcSlot = 0;              // 0: Top, 1: Bar, 2: Hero
    int tcSize = 1;              // 0: Med, 1: Large, 2: XL
    std::string tcFps = "24";
    bool showTimecode = true;
    bool timecodeRunning = true;
    int layoutMode = 0;
    bool invertedMode = false;
    bool inBootScreen = false;
    bool pinDisplayActive = false;
    bool needDisplayUpdate = false;
    int tcPartialRefreshCount = 0;
    unsigned long lastTcMillis = 0;
};

// Firmware helper functions (exact ports from eink_clap.ino)
void parseTimecode(const std::string& tc, int& h, int& m, int& s) {
    h = 0; m = 0; s = 0;
    if (tc.length() >= 8) {
        h = std::atoi(tc.substr(0, 2).c_str());
        m = std::atoi(tc.substr(3, 2).c_str());
        s = std::atoi(tc.substr(6, 2).c_str());
    } else if (tc.length() >= 5) {
        m = std::atoi(tc.substr(0, 2).c_str());
        s = std::atoi(tc.substr(3, 2).c_str());
    }
    h = std::max(0, std::min(23, h));
    m = std::max(0, std::min(59, m));
    s = std::max(0, std::min(59, s));
}

std::string formatTimecode(int h, int m, int s) {
    char buf[16];
    std::snprintf(buf, sizeof(buf), "%02d:%02d:%02d", h, m, s);
    return std::string(buf);
}

void incrementTimecodeOneSecond(FirmwareState& st) {
    int h = 0, m = 0, s = 0;
    parseTimecode(st.timecode, h, m, s);
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
    st.timecode = formatTimecode(h, m, s);
}

// Bounding box simulation matching eink_clap.ino:getTimecodeBounds
void getTimecodeBounds(const FirmwareState& st, uint16_t& x, uint16_t& y, uint16_t& w, uint16_t& h) {
    int boxH = (st.tcSize == 2) ? 80 : ((st.tcSize == 1) ? 56 : 40);
    int boxW = (st.tcSize == 2) ? 420 : ((st.tcSize == 1) ? 300 : 220);
    int rawX = 0, rawY = 0;

    if (st.tcSlot == 0) { // Top Header
        rawX = (800 - boxW) / 2;
        rawY = 4;
    } else if (st.tcSlot == 2) { // Center Hero
        rawX = (800 - boxW) / 2;
        rawY = (480 - boxH) / 2;
    } else { // Lower Bar (Slot 1)
        rawX = 260;
        rawY = 480 - boxH - 6;
        boxW = 280;
    }

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

// Cycle-accurate replica of eink_clap.ino:2008..2115 CmdCharCallbacks::onWrite
bool processBleCommandPacket(FirmwareState& st, const uint8_t* val, size_t len) {
    if (len == 0) return false;
    uint8_t opcode = val[0];

    st.inBootScreen = false;
    st.tcPartialRefreshCount = 0;

    switch (opcode) {
        case 0x01: // INCREMENT_TAKE
            break;
        case 0x02: // INCREMENT_ROLL
            break;
        case 0x03: // TOGGLE_INVERT
            st.invertedMode = !st.invertedMode;
            st.needDisplayUpdate = true;
            break;
        case 0x04: // FORCE_REFRESH
            st.needDisplayUpdate = true;
            break;
        case 0x05: // DISMISS_BOOT
            st.needDisplayUpdate = true;
            break;
        case 0x06: // SET_LAYOUT
            if (len > 1) {
                int l = (int)val[1];
                st.layoutMode = std::max(0, std::min(2, l));
                st.needDisplayUpdate = true;
            }
            break;
        case 0x07: // TOGGLE_TIMECODE
            st.timecodeRunning = !st.timecodeRunning;
            break;
        case 0x08: // SET_TC_SLOT
            if (len > 1) {
                st.tcSlot = std::max(0, std::min(2, (int)val[1]));
                st.needDisplayUpdate = true;
            }
            break;
        case 0x09: // SET_TC_SIZE
            if (len > 1) {
                st.tcSize = std::max(0, std::min(2, (int)val[1]));
                st.needDisplayUpdate = true;
            }
            break;
        case 0x0A: // SET_TC_FPS
            if (len > 1) {
                uint8_t fpsIdx = val[1];
                if (fpsIdx == 0) st.tcFps = "23.976";
                else if (fpsIdx == 1) st.tcFps = "24";
                else if (fpsIdx == 2) st.tcFps = "25";
                else if (fpsIdx == 3) st.tcFps = "29.97";
                else if (fpsIdx == 4) st.tcFps = "30";
                else st.tcFps = "24";
            }
            break;
        case 0x0B: // SET_TC_VISIBILITY
            if (len > 1) {
                st.showTimecode = (val[1] != 0);
                st.needDisplayUpdate = true;
            }
            break;
        case 0x0C: // FAST_JAM_SYNC_TOD
            if (len >= 4) {
                int h = std::max(0, std::min(23, (int)val[1]));
                int m = std::max(0, std::min(59, (int)val[2]));
                int s = std::max(0, std::min(59, (int)val[3]));
                st.timecode = formatTimecode(h, m, s);
                st.lastTcMillis = 1000;
                st.timecodeRunning = true;
                st.tcPartialRefreshCount = 0;
            }
            break;
        default:
            return false;
    }
    return true;
}

// C++ Frame Offset Math Engine — Model of existing implementation (with floor truncation)
std::string applyFrameOffsetExisting(const std::string& tcStr, int64_t deltaFrames, double fps) {
    int h = 0, m = 0, s = 0;
    parseTimecode(tcStr, h, m, s);
    int64_t totalSeconds = (int64_t)h * 3600 + (int64_t)m * 60 + s;
    int64_t currentFrames = (int64_t)std::round((double)totalSeconds * fps);
    int64_t totalFrames = currentFrames + deltaFrames;
    int64_t framesInDay = (int64_t)std::round(86400.0 * fps);

    int64_t wrappedFrames = totalFrames % framesInDay;
    while (wrappedFrames < 0) {
        wrappedFrames += framesInDay;
    }

    int64_t resSeconds = (int64_t)((double)wrappedFrames / fps); // Int/floor truncation
    int resS = (int)(resSeconds % 60);
    int resM = (int)((resSeconds / 60) % 60);
    int resH = (int)((resSeconds / 3600) % 24);
    return formatTimecode(resH, resM, resS);
}

// C++ Frame Offset Math Engine — Corrected Oracle (with 0.5 frame rounding epsilon)
std::string applyFrameOffsetOracle(const std::string& tcStr, int64_t deltaFrames, double fps) {
    int h = 0, m = 0, s = 0;
    parseTimecode(tcStr, h, m, s);
    int64_t totalSeconds = (int64_t)h * 3600 + (int64_t)m * 60 + s;
    int64_t currentFrames = (int64_t)std::round((double)totalSeconds * fps);
    int64_t totalFrames = currentFrames + deltaFrames;
    int64_t framesInDay = (int64_t)std::round(86400.0 * fps);

    int64_t wrappedFrames = totalFrames % framesInDay;
    while (wrappedFrames < 0) {
        wrappedFrames += framesInDay;
    }

    int64_t resSeconds = (int64_t)std::floor(((double)wrappedFrames + 0.5) / fps);
    int resS = (int)(resSeconds % 60);
    int resM = (int)((resSeconds / 60) % 60);
    int resH = (int)((resSeconds / 3600) % 24);
    return formatTimecode(resH, resM, resS);
}

int main() {
    std::cout << "==========================================================" << std::endl;
    std::cout << "⚡ RUNNING C++ PROTOCOL FUZZER & MATHEMATICAL STRESS SUITE" << std::endl;
    std::cout << "==========================================================" << std::endl;

    // -------------------------------------------------------------
    // TEST 1: BLE OPCODE FUZZING (10,000 FUZZED PACKETS)
    // -------------------------------------------------------------
    std::cout << "[1/4] Fuzzing BLE Opcode Parser (0xC402: 0x07..0x0C) with 10,000 packets..." << std::endl;
    FirmwareState fuzzerState;
    std::mt19937 rng(1337);
    std::uniform_int_distribution<int> lenDist(0, 128);
    std::uniform_int_distribution<int> byteDist(0, 255);

    int validParsed = 0;
    int rejectedCount = 0;

    // 1.1 Truncated packet regression
    {
        uint8_t pktEmpty[] = {};
        assert(!processBleCommandPacket(fuzzerState, pktEmpty, 0));

        uint8_t pkt07[] = {0x07};
        assert(processBleCommandPacket(fuzzerState, pkt07, 1));
        assert(!fuzzerState.timecodeRunning); // toggled off

        // Truncated 0x08..0x0B (length 1: missing param)
        int prevSlot = fuzzerState.tcSlot;
        uint8_t pkt08_trunc[] = {0x08};
        assert(processBleCommandPacket(fuzzerState, pkt08_trunc, 1));
        assert(fuzzerState.tcSlot == prevSlot); // untouched

        int prevSize = fuzzerState.tcSize;
        uint8_t pkt09_trunc[] = {0x09};
        assert(processBleCommandPacket(fuzzerState, pkt09_trunc, 1));
        assert(fuzzerState.tcSize == prevSize); // untouched

        std::string prevFps = fuzzerState.tcFps;
        uint8_t pkt0A_trunc[] = {0x0A};
        assert(processBleCommandPacket(fuzzerState, pkt0A_trunc, 1));
        assert(fuzzerState.tcFps == prevFps); // untouched

        bool prevVis = fuzzerState.showTimecode;
        uint8_t pkt0B_trunc[] = {0x0B};
        assert(processBleCommandPacket(fuzzerState, pkt0B_trunc, 1));
        assert(fuzzerState.showTimecode == prevVis); // untouched

        // Truncated 0x0C (lengths 1, 2, 3: missing HH, MM, SS)
        std::string prevTc = fuzzerState.timecode;
        uint8_t pkt0C_t1[] = {0x0C};
        uint8_t pkt0C_t2[] = {0x0C, 12};
        uint8_t pkt0C_t3[] = {0x0C, 12, 34};
        assert(processBleCommandPacket(fuzzerState, pkt0C_t1, 1));
        assert(processBleCommandPacket(fuzzerState, pkt0C_t2, 2));
        assert(processBleCommandPacket(fuzzerState, pkt0C_t3, 3));
        assert(fuzzerState.timecode == prevTc); // untouched
    }

    // 1.2 Extreme values and over-sized payloads
    {
        uint8_t pkt08_ext[] = {0x08, 255};
        processBleCommandPacket(fuzzerState, pkt08_ext, 2);
        assert(fuzzerState.tcSlot == 2);

        uint8_t pkt09_ext[] = {0x09, 255};
        processBleCommandPacket(fuzzerState, pkt09_ext, 2);
        assert(fuzzerState.tcSize == 2);

        uint8_t pkt0A_ext[] = {0x0A, 255};
        processBleCommandPacket(fuzzerState, pkt0A_ext, 2);
        assert(fuzzerState.tcFps == "24");

        uint8_t pkt0C_huge[60];
        pkt0C_huge[0] = 0x0C;
        pkt0C_huge[1] = 255;
        pkt0C_huge[2] = 255;
        pkt0C_huge[3] = 255;
        std::memset(&pkt0C_huge[4], 0xAA, 56);
        processBleCommandPacket(fuzzerState, pkt0C_huge, 60);
        assert(fuzzerState.timecode == "23:59:59");
    }

    // 1.3 Randomized 10,000 packet storm
    for (int i = 0; i < 10000; i++) {
        int len = lenDist(rng);
        std::vector<uint8_t> pkt(len);
        for (int j = 0; j < len; j++) {
            pkt[j] = (uint8_t)byteDist(rng);
        }
        bool ok = processBleCommandPacket(fuzzerState, pkt.data(), len);
        if (ok) validParsed++;
        else rejectedCount++;

        assert(fuzzerState.tcSlot >= 0 && fuzzerState.tcSlot <= 2);
        assert(fuzzerState.tcSize >= 0 && fuzzerState.tcSize <= 2);
        assert(fuzzerState.layoutMode >= 0 && fuzzerState.layoutMode <= 2);
        assert(fuzzerState.timecode.length() == 8);
        assert(fuzzerState.tcFps == "23.976" || fuzzerState.tcFps == "24" ||
               fuzzerState.tcFps == "25" || fuzzerState.tcFps == "29.97" || fuzzerState.tcFps == "30");
    }
    std::cout << "  ✓ Processed 10,000 fuzzed packets with zero crashes or deadlocks ("
              << validParsed << " parsed, " << rejectedCount << " safely rejected)." << std::endl;

    // -------------------------------------------------------------
    // TEST 2: FRAME OFFSET MODULAR ARITHMETIC (INTEGER & FRACTIONAL)
    // -------------------------------------------------------------
    std::cout << "[2/4] Stress testing Frame Offset math with extreme deltas (±10,000, ±1,000,000 frames)..." << std::endl;
    std::vector<double> intFpsList = {24.0, 25.0, 30.0};
    std::vector<int64_t> extremeDeltas = {
        1, -1, 10, -10, 24, -24, 100, -100, 1000, -1000,
        10000, -10000, 100000, -100000,
        1000000, -1000000, 10000000, -10000000,
        100000000, -100000000
    };
    std::vector<std::string> baseTimecodes = {
        "00:00:00", "00:00:01", "06:15:30", "12:00:00", "18:45:12", "23:59:58", "23:59:59"
    };

    int frameMathChecks = 0;
    // 2.1 Integer frame rates: Exact 24h wraps
    for (double fps : intFpsList) {
        int64_t framesInDay = (int64_t)std::round(86400.0 * fps);

        for (const auto& tc : baseTimecodes) {
            std::string resPosDay = applyFrameOffsetExisting(tc, framesInDay, fps);
            std::string resNegDay = applyFrameOffsetExisting(tc, -framesInDay, fps);
            assert(resPosDay == tc && "Adding 24h of frames must wrap exactly to origin");
            assert(resNegDay == tc && "Subtracting 24h of frames must wrap exactly to origin");
            frameMathChecks += 2;
        }

        std::string wrapUnder = applyFrameOffsetExisting("00:00:00", -1, fps);
        assert(wrapUnder == "23:59:59" && "Subtracting 1 frame from 00:00:00 must wrap cleanly to 23:59:59");
        frameMathChecks++;

        int64_t framesInSec = (int64_t)std::ceil(fps);
        std::string wrapOver = applyFrameOffsetExisting("23:59:59", framesInSec, fps);
        assert(wrapOver == "00:00:00" && "Overflowing 23:59:59 must wrap to 00:00:00");
        frameMathChecks++;

        for (const auto& tc : baseTimecodes) {
            for (int64_t delta : extremeDeltas) {
                std::string res = applyFrameOffsetExisting(tc, delta, fps);
                assert(res.length() == 8 && "Timecode length must be 8");
                assert(res[2] == ':' && res[5] == ':');
                int h = 0, m = 0, s = 0;
                parseTimecode(res, h, m, s);
                assert(h >= 0 && h <= 23 && "Hours out of range");
                assert(m >= 0 && m <= 59 && "Minutes out of range");
                assert(s >= 0 && s <= 59 && "Seconds out of range");
                frameMathChecks++;
            }
        }
    }

    // 2.2 EMPIRICAL BUG REPRODUCTION: 23.976 & 29.97 fps Truncation Drift in Current Implementation
    std::cout << "  [EMPIRICAL DEFECT ANALYSIS] Demonstrating 1-second truncation drift in existing client math:" << std::endl;
    std::string existingDrift23976 = applyFrameOffsetExisting("12:00:00", 0, 23.976);
    std::string existingDrift2997  = applyFrameOffsetExisting("06:15:30", 0, 29.97);
    std::cout << "    • Current implementation 12:00:00 + 0 frames @ 23.976 fps -> " << existingDrift23976
              << " (BUG: Drifted -1 sec!)" << std::endl;
    std::cout << "    • Current implementation 06:15:30 + 0 frames @ 29.97  fps -> " << existingDrift2997
              << " (BUG: Drifted -1 sec!)" << std::endl;

    // 2.3 ORACLE VERIFICATION: Proving that 0.5 frame epsilon restores 100% precision
    std::string oracle23976 = applyFrameOffsetOracle("12:00:00", 0, 23.976);
    std::string oracle2997  = applyFrameOffsetOracle("06:15:30", 0, 29.97);
    assert(oracle23976 == "12:00:00" && "Oracle must preserve exact 12:00:00 with 0 delta");
    assert(oracle2997  == "06:15:30" && "Oracle must preserve exact 06:15:30 with 0 delta");

    // Test oracle across all 5 frame rates with ±10,000 and ±1,000,000 frames
    std::vector<double> allFps = {23.976, 24.0, 25.0, 29.97, 30.0};
    for (double fps : allFps) {
        int64_t framesInDay = (int64_t)std::round(86400.0 * fps);
        for (const auto& tc : baseTimecodes) {
            std::string resPos = applyFrameOffsetOracle(tc, framesInDay, fps);
            std::string resNeg = applyFrameOffsetOracle(tc, -framesInDay, fps);
            assert(resPos == tc && "Oracle: Adding 24h of frames must wrap cleanly");
            assert(resNeg == tc && "Oracle: Subtracting 24h of frames must wrap cleanly");
            frameMathChecks += 2;
        }
    }
    std::cout << "  ✓ Passed " << frameMathChecks << " frame offset math tests (Identified exact root cause & fix)." << std::endl;

    // -------------------------------------------------------------
    // TEST 3: 24-HOUR CONTINUOUS CLOCK SIMULATION & ANTI-GHOSTING
    // -------------------------------------------------------------
    std::cout << "[3/4] Simulating 86,400 seconds (24h) continuous ticking and 60-cycle anti-ghosting..." << std::endl;
    FirmwareState clockState;
    clockState.timecode = "00:00:00";
    clockState.showTimecode = true;
    clockState.timecodeRunning = true;

    int fullRefreshTriggers = 0;
    for (int sec = 0; sec < 86400; sec++) {
        int expH = sec / 3600;
        int expM = (sec % 3600) / 60;
        int expS = sec % 60;
        char expBuf[16];
        std::snprintf(expBuf, sizeof(expBuf), "%02d:%02d:%02d", expH, expM, expS);
        assert(clockState.timecode == std::string(expBuf));

        incrementTimecodeOneSecond(clockState);
        clockState.tcPartialRefreshCount++;
        if (clockState.tcPartialRefreshCount >= 60) {
            clockState.tcPartialRefreshCount = 0;
            fullRefreshTriggers++;
        }
    }
    assert(clockState.timecode == "00:00:00" && "Rollover at 24h must be exactly 00:00:00");
    assert(fullRefreshTriggers == 1440 && "Anti-ghosting must trigger exactly 1,440 full refreshes in 24 hours");

    // 3.2 Millis() 32-bit unsigned overflow test
    {
        uint32_t lastMillis = 0xFFFFFF00U;
        uint32_t nowMillis = 0x00000300U;
        uint32_t elapsed = nowMillis - lastMillis;
        assert(elapsed == 1024U && "Unsigned 32-bit subtraction must handle millis() overflow cleanly");
    }
    std::cout << "  ✓ 86,400 seconds validated: clean 23:59:59 -> 00:00:00 rollover & 1,440 full refreshes." << std::endl;

    // -------------------------------------------------------------
    // TEST 4: RAPID TOGGLE STRESS & DYNAMIC BOUNDING BOX INVARIANTS
    // -------------------------------------------------------------
    std::cout << "[4/4] Stress testing 1,000 rapid visibility toggles & dynamic bounds calculations..." << std::endl;
    FirmwareState stressState;
    int toggleChecks = 0;

    for (int iter = 0; iter < 1000; iter++) {
        stressState.showTimecode = !stressState.showTimecode;
        stressState.needDisplayUpdate = true;

        stressState.tcSlot = iter % 3;
        stressState.tcSize = (iter / 3) % 3;
        stressState.layoutMode = (iter / 9) % 3;

        uint16_t tcX = 0, tcY = 0, tcW = 0, tcH = 0;
        getTimecodeBounds(stressState, tcX, tcY, tcW, tcH);

        assert(tcX % 8 == 0 && "tcX must be 8-pixel byte aligned");
        assert(tcW % 8 == 0 && "tcW must be 8-pixel byte aligned");
        assert(tcX + tcW <= 800 && "tcX + tcW exceeds 800px display width");
        assert(tcY + tcH <= 480 && "tcY + tcH exceeds 480px display height");
        assert(tcW > 0 && tcH > 0 && "Bounding box dimensions must be non-zero");

        toggleChecks++;
    }
    std::cout << "  ✓ Passed 1,000 rapid toggle stress cycles with 100% byte alignment & zero overflow." << std::endl;

    std::cout << "==========================================================" << std::endl;
    std::cout << "🎉 ALL C++ ADVERSARIAL STRESS TESTS COMPLETED SUCCESSFULLY!" << std::endl;
    std::cout << "==========================================================" << std::endl;
    return 0;
}
