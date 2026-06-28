#include "threatscan.h"
#include "../core/config.h"
#include "../core/sdlog.h"
#include "../gps/gps.h"
#include "../ui/display.h"
#include "../piglet/mood.h"
#include <M5Cardputer.h>

// Radar display constants
static constexpr int RADAR_CX = 60;
static constexpr int RADAR_CY = 55;
static constexpr int RADAR_RADIUS = 50;
static constexpr int THREAT_BLIP_SIZE = 4;

// Color palette
static constexpr uint16_t COL_RADAR_BG = 0x0020;    // Very dark green
static constexpr uint16_t COL_RADAR_GRID = 0x01A0;  // Dim green
static constexpr uint16_t COL_RADAR_SWEEP = 0x07E0; // Bright green
static constexpr uint16_t COL_CRITICAL = 0xF800;    // Red
static constexpr uint16_t COL_HIGH = 0xFD00;         // Orange
static constexpr uint16_t COL_MEDIUM = 0xFFE0;       // Yellow
static constexpr uint16_t COL_LOW = 0x07E0;          // Green
static constexpr uint16_t COL_NONE = 0x8410;         // Gray

bool ThreatScanMode::running = false;
uint8_t ThreatScanMode::filter = 0;
int8_t ThreatScanMode::selectedThreat = -1;
bool ThreatScanMode::detailView = false;
uint32_t ThreatScanMode::lastUpdateMs = 0;

void ThreatScanMode::init() {
    filter = 0;
    selectedThreat = -1;
    detailView = false;
}

void ThreatScanMode::start() {
    running = true;
    selectedThreat = -1;
    detailView = false;
    Mood::setStatusMessage("scanning for threats");
    SDLog::log("THREATSCAN", "Mode started");
}

void ThreatScanMode::stop() {
    running = false;
}

void ThreatScanMode::update() {
    if (!running) return;

    // Check for keyboard input within display
    M5Cardputer.update();
    if (M5Cardputer.Keyboard.isChange()) {
        auto keys = M5Cardputer.Keyboard.keysState();
        for (auto c : keys.word) {
            switch (c) {
                case 'f': case 'F': cycleFilter(); break;
                case KEY_ENTER: toggleDetailView(); break;
                case ';': selectPrevThreat(); break;  // Up arrow
                case '.': selectNextThreat(); break;   // Down arrow
            }
        }
    }

    // ESC handled globally by ap_elim.cpp
    lastUpdateMs = millis();
}

void ThreatScanMode::cycleFilter() {
    filter = (filter + 1) % 3;
    const char* names[] = {"ALL NETWORKS", "HIGH+ ONLY", "SURVEILLANCE"};
    Display::showToast(names[filter], 1500);
}

void ThreatScanMode::selectNextThreat() {
    uint8_t count = ThreatDB::getThreatCount();
    if (count == 0) return;
    if (selectedThreat < 0) selectedThreat = 0;
    else selectedThreat = (selectedThreat + 1) % count;
}

void ThreatScanMode::selectPrevThreat() {
    uint8_t count = ThreatDB::getThreatCount();
    if (count == 0) return;
    if (selectedThreat < 0) selectedThreat = count - 1;
    else selectedThreat = (selectedThreat > 0) ? selectedThreat - 1 : count - 1;
}

void ThreatScanMode::toggleDetailView() {
    if (selectedThreat >= 0) detailView = !detailView;
}

uint16_t ThreatScanMode::threatColor(ThreatLevel level) {
    switch (level) {
        case ThreatLevel::LVL_CRITICAL: return COL_CRITICAL;
        case ThreatLevel::LVL_HIGH:     return COL_HIGH;
        case ThreatLevel::LVL_MEDIUM:   return COL_MEDIUM;
        case ThreatLevel::LVL_LOW:      return COL_LOW;
        default: return COL_NONE;
    }
}

uint16_t ThreatScanMode::threatClassColor(ThreatClass tc) {
    switch (tc) {
        case ThreatClass::AXON_BODY_CAM: return COL_CRITICAL;
        case ThreatClass::STINGRAY:      return COL_CRITICAL;
        case ThreatClass::TAIL_DEVICE:   return TFT_PURPLE;
        case ThreatClass::EVIL_TWIN:     return COL_HIGH;
        case ThreatClass::ROGUE_AP:      return COL_HIGH;
        case ThreatClass::GHOST_NET:     return COL_MEDIUM;
        case ThreatClass::SURVEILLANCE:  return COL_MEDIUM;
        case ThreatClass::KNOWN_SAFE:    return COL_LOW;
        default: return COL_NONE;
    }
}

const char* ThreatScanMode::threatClassName(ThreatClass tc) {
    switch (tc) {
        case ThreatClass::AXON_BODY_CAM: return "AXON CAM";
        case ThreatClass::STINGRAY:      return "STINGRAY";
        case ThreatClass::ROGUE_AP:      return "ROGUE AP";
        case ThreatClass::GHOST_NET:     return "GHOST NET";
        case ThreatClass::EVIL_TWIN:     return "EVIL TWIN";
        case ThreatClass::SURVEILLANCE:  return "SURVEILLANCE";
        case ThreatClass::TAIL_DEVICE:   return "TAIL DEVICE";
        case ThreatClass::KNOWN_SAFE:    return "SAFE";
        default: return "UNKNOWN";
    }
}

const char* ThreatScanMode::threatLevelName(ThreatLevel level) {
    switch (level) {
        case ThreatLevel::LVL_CRITICAL: return "CRITICAL";
        case ThreatLevel::LVL_HIGH:     return "HIGH";
        case ThreatLevel::LVL_MEDIUM:   return "MEDIUM";
        case ThreatLevel::LVL_LOW:      return "LOW";
        default: return "NONE";
    }
}

// ========= DRAWING =========

void ThreatScanMode::draw(M5Canvas& canvas) {
    if (!running) return;

    // Clear
    canvas.fillSprite(COL_RADAR_BG);

    // Draw radar display (left side)
    drawRadar(canvas);

    // Draw threat list or detail (right side)
    if (detailView && selectedThreat >= 0) {
        uint8_t count;
        const ThreatRecord* threats = ThreatDB::getThreats(count);
        if (selectedThreat < (int8_t)count) {
            drawDetailPanel(canvas, threats[selectedThreat]);
        }
    } else {
        drawThreatList(canvas);
    }

    // Bottom stats
    drawStats(canvas);
}

void ThreatScanMode::drawRadar(M5Canvas& canvas) {
    // Radar rings
    for (int r = RADAR_RADIUS; r > 10; r -= 12) {
        canvas.drawCircle(RADAR_CX, RADAR_CY, r, COL_RADAR_GRID);
    }

    // Crosshairs
    canvas.drawLine(RADAR_CX - RADAR_RADIUS, RADAR_CY, RADAR_CX + RADAR_RADIUS, RADAR_CY, COL_RADAR_GRID);
    canvas.drawLine(RADAR_CX, RADAR_CY - RADAR_RADIUS, RADAR_CX, RADAR_CY + RADAR_RADIUS, COL_RADAR_GRID);

    // Sweep animation (rotating line)
    drawRadarSweep(canvas, RADAR_CX, RADAR_CY, RADAR_RADIUS);

    // Threat blips
    uint8_t count;
    const ThreatRecord* threats = ThreatDB::getThreats(count);
    for (uint8_t i = 0; i < count; i++) {
        drawThreatBlip(canvas, RADAR_CX, RADAR_CY, RADAR_RADIUS, threats[i]);
    }

    // Center dot (us)
    canvas.fillCircle(RADAR_CX, RADAR_CY, 3, COL_RADAR_SWEEP);
}

void ThreatScanMode::drawRadarSweep(M5Canvas& canvas, int cx, int cy, int radius) {
    // Animated sweep line
    float angle = (millis() % 3000) * (TWO_PI / 3000.0f);
    int ex = cx + (int)(radius * cosf(angle));
    int ey = cy + (int)(radius * sinf(angle));
    canvas.drawLine(cx, cy, ex, ey, COL_RADAR_SWEEP);
}

void ThreatScanMode::drawThreatBlip(M5Canvas& canvas, int cx, int cy, int radius,
                                      const ThreatRecord& threat) {
    // Position on radar based on distance and a hash of BSSID for angle
    float dist = threat.estimatedDistanceM;
    if (dist < 0) dist = 50;

    // Map distance to radar radius (closer = more outward on radar)
    float distNorm = 1.0f - (dist / 100.0f);
    if (distNorm < 0.1f) distNorm = 0.1f;
    if (distNorm > 0.9f) distNorm = 0.9f;

    // Angle from BSSID hash (pseudo-random but consistent per device)
    uint8_t hash = threat.bssid[0] ^ threat.bssid[5] ^ threat.bssid[2];
    float angle = (hash / 255.0f) * TWO_PI;

    int bx = cx + (int)(radius * distNorm * cosf(angle));
    int by = cy + (int)(radius * distNorm * sinf(angle));

    // Blip size based on threat level
    int size = THREAT_BLIP_SIZE;
    if (threat.level >= ThreatLevel::LVL_HIGH) size += 2;
    if (threat.level >= ThreatLevel::LVL_CRITICAL) size += 2;

    // Pulsing effect for HIGH/CRITICAL
    if (threat.level >= ThreatLevel::LVL_HIGH) {
        float pulse = sinf(millis() / 500.0f) * 0.3f + 0.7f;
        int pulseSize = size + (int)(2 * pulse);
        canvas.fillCircle(bx, by, pulseSize, threatColor(threat.level));
    }

    canvas.fillCircle(bx, by, size, threatColor(threat.level));
    // Border
    canvas.drawCircle(bx, by, size + 1, TFT_WHITE);
}

void ThreatScanMode::drawThreatList(M5Canvas& canvas) {
    uint8_t count;
    const ThreatRecord* threats = ThreatDB::getThreats(count);

    int y = 8;
    uint8_t maxDisplay = 5;
    uint8_t startIdx = 0;

    // Scroll if selected beyond visible
    if (selectedThreat >= (int8_t)maxDisplay) {
        startIdx = selectedThreat - maxDisplay + 1;
    }

    canvas.setTextSize(1);
    for (uint8_t i = startIdx; i < count && i < startIdx + maxDisplay; i++) {
        uint16_t color = threatClassColor(threats[i].classification);
        if ((int8_t)i == selectedThreat) {
            canvas.fillRect(120, y - 1, 115, 10, 0x3186);  // Highlight bar
        }

        // Icon
        canvas.setTextColor(color);
        switch (threats[i].classification) {
            case ThreatClass::AXON_BODY_CAM: canvas.drawChar('!', 122, y, 1); break;
            case ThreatClass::STINGRAY:      canvas.drawChar('S', 122, y, 1); break;
            case ThreatClass::TAIL_DEVICE:   canvas.drawChar('T', 122, y, 1); break;
            default:                         canvas.drawChar('?', 122, y, 1); break;
        }

        // Name
        canvas.setTextColor(TFT_WHITE);
        canvas.drawString(threats[i].ssid[0] ? threats[i].ssid : "(hidden)", 132, y, 1);

        // Distance
        if (threats[i].estimatedDistanceM > 0) {
            char distStr[16];
            if (threats[i].estimatedDistanceM < 10)
                snprintf(distStr, sizeof(distStr), "%.1fm", threats[i].estimatedDistanceM);
            else
                snprintf(distStr, sizeof(distStr), "%dm", (int)threats[i].estimatedDistanceM);
            canvas.setTextColor(threatColor(threats[i].level));
            canvas.drawRightString(distStr, 235, y, 1);
        }

        // Approaching indicator
        if (threats[i].rssiSlope > 1) {
            canvas.setTextColor(COL_CRITICAL);
            canvas.drawChar('^', 122, y + 1, 1);
        }

        y += 11;
    }
}

void ThreatScanMode::drawDetailPanel(M5Canvas& canvas, const ThreatRecord& threat) {
    canvas.fillRect(115, 0, 125, 107, 0x1082);  // Dark panel bg

    canvas.setTextSize(1);
    int y = 5;

    // Classification header
    canvas.setTextColor(threatClassColor(threat.classification));
    canvas.drawCentreString(threatClassName(threat.classification), 177, y, 1);
    y += 12;

    // SSID
    canvas.setTextColor(TFT_WHITE);
    canvas.drawString("SSID:", 118, y, 1);
    canvas.drawString(threat.ssid[0] ? threat.ssid : "(hidden)", 155, y, 1);
    y += 10;

    // BSSID
    char macStr[18];
    snprintf(macStr, sizeof(macStr), "%02X:%02X:%02X:%02X:%02X:%02X",
             threat.bssid[0], threat.bssid[1], threat.bssid[2],
             threat.bssid[3], threat.bssid[4], threat.bssid[5]);
    canvas.drawString("MAC:", 118, y, 1);
    canvas.drawString(macStr, 155, y, 1);
    y += 10;

    // Distance
    char distStr[16];
    if (threat.estimatedDistanceM > 0)
        snprintf(distStr, sizeof(distStr), "%.1fm", threat.estimatedDistanceM);
    else
        snprintf(distStr, sizeof(distStr), "N/A");
    canvas.drawString("DIST:", 118, y, 1);
    canvas.setTextColor(threatColor(threat.level));
    canvas.drawString(distStr, 155, y, 1);
    y += 10;

    // Channel / RSSI
    canvas.setTextColor(TFT_WHITE);
    char rssiStr[16];
    snprintf(rssiStr, sizeof(rssiStr), "CH%02d %ddBm", threat.channel, threat.rssi);
    canvas.drawString("RF:", 118, y, 1);
    canvas.drawString(rssiStr, 155, y, 1);
    y += 10;

    // Approach rate
    if (threat.rssiSlope != 0) {
        char slopeStr[16];
        snprintf(slopeStr, sizeof(slopeStr), "%+ddBm/s", threat.rssiSlope);
        canvas.drawString(threat.rssiSlope > 0 ? "APPROACHING" : "LEAVING", 118, y, 1);
        canvas.drawString(slopeStr, 185, y, 1);
        y += 10;
    }

    // Threat level
    canvas.setTextColor(threatColor(threat.level));
    canvas.drawCentreString(threatLevelName(threat.level), 177, y, 1);
    y += 12;

    // Sightings
    canvas.setTextColor(TFT_WHITE);
    char sCount[16];
    snprintf(sCount, sizeof(sCount), "x%u seen", threat.sightingCount);
    canvas.drawCentreString(sCount, 177, y, 1);
    y += 10;

    // GPS
    if (threat.hasGPS) {
        char gpsStr[32];
        snprintf(gpsStr, sizeof(gpsStr), "%.4f,%.4f", threat.latitude, threat.longitude);
        canvas.drawCentreString(gpsStr, 177, y, 1);
    }
}

void ThreatScanMode::drawStats(M5Canvas& canvas) {
    uint8_t count;
    const ThreatRecord* threats = ThreatDB::getThreats(count);

    // Bottom bar stats
    canvas.setTextSize(1);
    char stats[64];

    uint8_t highCount = 0, survCount = 0;
    for (uint8_t i = 0; i < count; i++) {
        if (threats[i].level >= ThreatLevel::LVL_HIGH) highCount++;
        if (threats[i].classification == ThreatClass::SURVEILLANCE ||
            threats[i].classification == ThreatClass::AXON_BODY_CAM ||
            threats[i].classification == ThreatClass::STINGRAY) survCount++;
    }

    snprintf(stats, sizeof(stats), "THREATS: %u HIGH:%u SURV:%u TAILS:%u",
             count, highCount, survCount, ThreatDB::getActiveTailCount());
    canvas.setTextColor(COL_RADAR_SWEEP);
    canvas.drawString(stats, 5, 113, 1);

    // GPS status
    if (Config::gps().enabled) {
        char gpsStr[24];
        GPSData gps = GPS::getData();
        if (gps.fix)
            snprintf(gpsStr, sizeof(gpsStr), "%.4f,%.4f", gps.latitude, gps.longitude);
        else
            snprintf(gpsStr, sizeof(gpsStr), "NO FIX");
        canvas.drawRightString(gpsStr, 235, 113, 1);
    }
}
