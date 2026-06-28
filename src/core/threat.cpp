#include "threat.h"
#include "config.h"
#include "sdlog.h"
#include "../ui/display.h"
#include "../audio/sfx.h"
#include "../piglet/mood.h"
#include <M5Cardputer.h>
#include <esp_wifi.h>
#include <SD.h>
#include <time.h>

// ============================================================
// SURVEILLANCE FINGERPRINT DATABASE
// Matched against every beacon received in real-time.
// Each entry defines OUI prefix, SSID pattern, and vendor IE.
// ============================================================

struct SurveillanceFingerprint {
    const char* name;
    ThreatClass classification;
    const char* ouiMatch;         // OUI prefix, e.g. "00:1A:7D" or nullptr
    const char* ssidSubstring;   // SSID contains this, e.g. "AXON" or nullptr
    const char* vendorOUI;       // Vendor IE OUI in beacon, e.g. "00:1A:7D" or nullptr
    ThreatLevel defaultLevel;
};

static const SurveillanceFingerprint FINGERPRINTS[] = {
    // AXON body cameras
    {"AXON Body Cam",       ThreatClass::AXON_BODY_CAM, "00:1A:7D", "AXON",      nullptr,             ThreatLevel::LVL_HIGH},
    {"AXON Evidence",       ThreatClass::AXON_BODY_CAM, nullptr,     "EVIDENCE",  nullptr,             ThreatLevel::LVL_HIGH},
    {"AXON BWC",            ThreatClass::AXON_BODY_CAM, nullptr,     "BWC_",      nullptr,             ThreatLevel::LVL_HIGH},
    {"AXON Nexxus",         ThreatClass::AXON_BODY_CAM, nullptr,     "NEXXUS",    nullptr,             ThreatLevel::LVL_HIGH},
    
    // Stingray / IMSI catcher signatures
    {"Possible Stingray",   ThreatClass::STINGRAY,      nullptr,     "STINGRAY",  nullptr,             ThreatLevel::LVL_CRITICAL},
    {"Cell Sim",            ThreatClass::STINGRAY,      nullptr,     "CELLSITE",  nullptr,             ThreatLevel::LVL_CRITICAL},
    
    // Law enforcement vehicle APs
    {"Police AP",           ThreatClass::SURVEILLANCE,  nullptr,     "POLICE",    nullptr,             ThreatLevel::LVL_MEDIUM},
    {"LE Vehicle",          ThreatClass::SURVEILLANCE,  "00:50:C2",  nullptr,     nullptr,             ThreatLevel::LVL_MEDIUM},  // Cisco/LawEnforcement OUI
    {"Gov Equipment",       ThreatClass::SURVEILLANCE,  "08:00:27",  nullptr,     nullptr,             ThreatLevel::LVL_LOW},    // Pwnie Express / covert

    // Common surveillance drone APs
    {"DJI Drone",           ThreatClass::SURVEILLANCE,  nullptr,     "DJI_",      nullptr,             ThreatLevel::LVL_MEDIUM},
    {"Drone AP",            ThreatClass::SURVEILLANCE,  "60:60:1F",  nullptr,     nullptr,             ThreatLevel::LVL_MEDIUM},
};

static constexpr uint8_t FINGERPRINT_COUNT = sizeof(FINGERPRINTS) / sizeof(FINGERPRINTS[0]);

// RSSI-to-distance model: free-space path loss with environmental factor
// d = 10 ^ ((TxPwr - RSSI - FadeMargin) / (10 * n))
// n = 3.0 (urban environment), TxPwr = -30dBm (typical AP), FadeMargin = 0
static constexpr float PATH_LOSS_EXPONENT = 3.0f;
static constexpr int8_t REFERENCE_RSSI = -30;  // RSSI at 1 meter
static constexpr int8_t REFERENCE_DISTANCE = 1; // 1 meter

// Threat file paths
static const char* THREAT_LOG_PATH = "/m5ap_elim/threats/threat_log.csv";
static const char* CLIENT_DB_PATH = "/m5ap_elim/threats/clients.db";
static const char* PMKID_DB_PATH = "/m5ap_elim/threats/pmkids.db";

// Static members
ThreatRecord ThreatDB::threats[MAX_THREATS] = {};
uint8_t ThreatDB::threatCount = 0;
ClientFootprint ThreatDB::clients[MAX_CLIENTS] = {};
uint8_t ThreatDB::clientCount = 0;
PMKIDFingerprint ThreatDB::pmkidFingerprints[MAX_PMKID_FINGERPRINTS] = {};
uint8_t ThreatDB::pmkidFingerprintCount = 0;
bool ThreatDB::ghostProtocol = false;
uint32_t ThreatDB::lastSirenTime = 0;
ThreatRecord ThreatDB::lastSirenThreat = {};

// Distance estimation using log-normal shadowing model
float ThreatDB::estimateDistance(int8_t rssi, int8_t rssiSlope) {
    if (rssi == 0) return -1.0f;
    float exponent = (REFERENCE_RSSI - rssi) / (10.0f * PATH_LOSS_EXPONENT);
    float dist = powf(10.0f, exponent);
    if (dist < 0.5f) dist = 0.5f;
    if (dist > 300.0f) dist = 300.0f;
    return dist;
}

// Classify a device based on OUI, SSID, and vendor IE
ThreatClass ThreatDB::classify(const uint8_t* bssid, const char* ssid,
                                const uint8_t* vendorIE, uint16_t ieLen) {
    // Build BSSID string for OUI matching
    char bssidStr[18] = {0};
    if (bssid) {
        snprintf(bssidStr, sizeof(bssidStr), "%02X:%02X:%02X", bssid[0], bssid[1], bssid[2]);
    }

    for (uint8_t f = 0; f < FINGERPRINT_COUNT; f++) {
        bool match = true;

        // Check OUI
        if (FINGERPRINTS[f].ouiMatch && bssid) {
            if (strncmp(bssidStr, FINGERPRINTS[f].ouiMatch, 8) != 0) match = false;
        }

        // Check SSID substring
        if (match && FINGERPRINTS[f].ssidSubstring && ssid && ssid[0]) {
            if (strstr(ssid, FINGERPRINTS[f].ssidSubstring) == nullptr) match = false;
        }

        // Check vendor IE in beacon
        if (match && FINGERPRINTS[f].vendorOUI && vendorIE && ieLen >= 3) {
            char ieOUI[8] = {0};
            snprintf(ieOUI, sizeof(ieOUI), "%02X:%02X:%02X", vendorIE[0], vendorIE[1], vendorIE[2]);
            if (strcmp(ieOUI, FINGERPRINTS[f].vendorOUI) != 0) match = false;
        }

        if (match) {
            return FINGERPRINTS[f].classification;
        }
    }

    return ThreatClass::UNKNOWN;
}

// Calculate composite threat level
ThreatLevel ThreatDB::calculateThreatLevel(ThreatClass tc, int8_t rssi,
                                            float distance, uint8_t sightingCount,
                                            int8_t rssiSlope) {
    if (tc == ThreatClass::KNOWN_SAFE) return ThreatLevel::LVL_NONE;

    int score = 0;

    // Classification weight
    switch (tc) {
        case ThreatClass::AXON_BODY_CAM:  score += 80; break;
        case ThreatClass::STINGRAY:        score += 100; break;
        case ThreatClass::ROGUE_AP:        score += 50; break;
        case ThreatClass::GHOST_NET:       score += 30; break;
        case ThreatClass::EVIL_TWIN:       score += 70; break;
        case ThreatClass::SURVEILLANCE:    score += 40; break;
        case ThreatClass::TAIL_DEVICE:     score += 90; break;
        default:                           score += 10; break;
    }

    // Proximity weight
    if (distance < 5.0f && distance > 0) score += 40;
    else if (distance < 15.0f)           score += 25;
    else if (distance < 50.0f)           score += 10;

    // Approach speed (RSSI slope positive = getting closer)
    if (rssiSlope > 2)  score += 30;   // Fast approach
    else if (rssiSlope > 1) score += 15;

    // Persistence weight
    if (sightingCount >= 10) score += 20;
    else if (sightingCount >= 5) score += 10;

    if (score >= 150) return ThreatLevel::LVL_CRITICAL;
    if (score >= 80)  return ThreatLevel::LVL_HIGH;
    if (score >= 40)  return ThreatLevel::LVL_MEDIUM;
    if (score > 0)    return ThreatLevel::LVL_LOW;
    return ThreatLevel::LVL_NONE;
}

// Main evaluation function — called for every beacon
void ThreatDB::evaluate(const uint8_t* bssid, const char* ssid, uint8_t channel,
                        int8_t rssi, wifi_auth_mode_t authMode,
                        const uint8_t* vendorIE, uint16_t ieLen) {
    if (!bssid) return;

    // Find existing or create new
    int idx = findThreat(bssid);
    bool isNew = (idx < 0);

    if (isNew) {
        if (threatCount >= MAX_THREATS) return;  // Database full
        idx = threatCount;

        memcpy(threats[idx].bssid, bssid, 6);
        if (ssid) { strncpy(threats[idx].ssid, ssid, 32); threats[idx].ssid[32] = 0; }
        threats[idx].channel = channel;
        threats[idx].firstSeen = millis();
        threats[idx].classification = classify(bssid, ssid, vendorIE, ieLen);
        threats[idx].rssi = rssi;
        threats[idx].estimatedDistanceM = estimateDistance(rssi);
        threats[idx].sightingCount = 1;
        threats[idx].locationCount = 0;

        // Check if this is a ghost network (not on WiGLE)
        if (threats[idx].classification == ThreatClass::UNKNOWN && ssid && ssid[0]) {
            // Mark as GHOST_NET temporarily — WiGLE check is async
            threats[idx].classification = ThreatClass::GHOST_NET;
        }

        // If we have GPS
        if (Config::gps().enabled) {
            GPSData gps = GPS::getData();
            if (gps.fix) {
                threats[idx].hasGPS = true;
                threats[idx].latitude = gps.latitude;
                threats[idx].longitude = gps.longitude;
                threats[idx].locationCount = 1;
                snprintf(threats[idx].lastLocation, 64, "%.6f,%.6f", gps.latitude, gps.longitude);
            }
        }

        threatCount++;
        logToSD("NEW_THREAT", threats[idx].ssid);

    } else {
        // Update existing
        threats[idx].lastSeen = millis();
        int8_t prevRSSI = threats[idx].rssi;
        threats[idx].rssi = rssi;
        threats[idx].estimatedDistanceM = estimateDistance(rssi);
        threats[idx].sightingCount++;

        // Track RSSI slope (approach rate) using EMA
        int8_t delta = rssi - prevRSSI;
        if (delta != 0) {
            threats[idx].rssiSlope = (threats[idx].rssiSlope * 3 + delta) / 4;
        }

        // Update GPS if available
        if (Config::gps().enabled) {
            GPSData gps = GPS::getData();
            if (gps.fix) {
                threats[idx].latitude = gps.latitude;
                threats[idx].longitude = gps.longitude;
            }
        }

        // Re-evaluate threat level
        threats[idx].level = calculateThreatLevel(
            threats[idx].classification,
            threats[idx].rssi,
            threats[idx].estimatedDistanceM,
            threats[idx].sightingCount,
            threats[idx].rssiSlope
        );
    }

    // Initial threat level for new entries
    if (isNew) {
        threats[idx].level = calculateThreatLevel(
            threats[idx].classification,
            threats[idx].rssi,
            threats[idx].estimatedDistanceM,
            threats[idx].sightingCount,
            threats[idx].rssiSlope
        );
    }

    // Check if we should trigger ghost protocol
    if (!ghostProtocol && threats[idx].level >= getGhostProtocolTriggerLevel()) {
        activateGhostProtocol();
    }

    // Check if we should trigger siren
    if (shouldTriggerSiren() && threats[idx].level >= ThreatLevel::LVL_HIGH) {
        lastSirenThreat = threats[idx];
        lastSirenTime = millis();
    }
}

int ThreatDB::findThreat(const uint8_t* bssid) {
    for (uint8_t i = 0; i < threatCount; i++) {
        if (memcmp(threats[i].bssid, bssid, 6) == 0) return i;
    }
    return -1;
}

int ThreatDB::findClient(const uint8_t* mac) {
    for (uint8_t i = 0; i < clientCount; i++) {
        if (memcmp(clients[i].mac, mac, 6) == 0) return i;
    }
    return -1;
}

uint8_t ThreatDB::getThreatCount() { return threatCount; }

const ThreatRecord* ThreatDB::getThreats(uint8_t& count) {
    count = threatCount;
    return threats;
}

ThreatLevel ThreatDB::getHighestThreatLevel() {
    ThreatLevel highest = ThreatLevel::LVL_NONE;
    for (uint8_t i = 0; i < threatCount; i++) {
        if (threats[i].level > highest) highest = threats[i].level;
    }
    return highest;
}

uint8_t ThreatDB::getActiveTailCount() {
    uint8_t count = 0;
    for (uint8_t i = 0; i < clientCount; i++) {
        if (clients[i].isTrackingUs) count++;
    }
    return count;
}

// === GHOST PROTOCOL ===
void ThreatDB::activateGhostProtocol() {
    ghostProtocol = true;
    Serial.println("[THREAT] GHOST PROTOCOL ACTIVATED — killing all RF");
    SDLog::log("THREAT", "GHOST PROTOCOL ACTIVATED");

    // Kill WiFi entirely
    // NetworkRecon::stop() — Ghost Protocol kills WiFi
    esp_wifi_stop();

    // Kill BLE if active
    // NimBLEDevice::deinit(); (would need include)

    // Dim screen
    M5.Display.setBrightness(5);

    // Update UI
    Display::setTopBarMessage("GHOST PROTOCOL", 0);
    Display::notify(NoticeKind::WARNING, "RF KILLED — HIGH THREAT", 0, NoticeChannel::TOAST);

    // Haptic alert
    SFX::play(SFX::SIREN);

    logToSD("GHOST_ACTIVATED", "All RF emissions stopped due to high threat detection");
}

void ThreatDB::deactivateGhostProtocol() {
    ghostProtocol = false;
    Display::clearTopBarMessage();

    // Restart WiFi
    esp_wifi_start();
    // NetworkRecon::start() — Ghost Protocol deactivated

    M5.Display.setBrightness(Config::personality().brightness * 255 / 100);
    Display::showToast("GHOST PROTOCOL DEACTIVATED", 3000);
    SDLog::log("THREAT", "GHOST PROTOCOL DEACTIVATED");
}

// === PROBE REQUEST LOGGING ===
void ThreatDB::logProbeRequest(const uint8_t* stationMAC, const char* ssid) {
    if (!stationMAC || !ssid) return;

    int idx = findClient(stationMAC);
    if (idx < 0) {
        if (clientCount >= MAX_CLIENTS) return;
        idx = clientCount;
        memcpy(clients[idx].mac, stationMAC, 6);
        clients[idx].probeCount = 0;
        clients[idx].seenCount = 1;

        if (Config::gps().enabled) {
            GPSData gps = GPS::getData();
            if (gps.fix) {
                clients[idx].firstLat = gps.latitude;
                clients[idx].firstLon = gps.longitude;
            }
        }

        clientCount++;
    } else {
        clients[idx].seenCount++;
    }

    // Store probe SSID (circular buffer of 5)
    if (clients[idx].probeCount < 5) {
        strncpy(clients[idx].probeSSIDs[clients[idx].probeCount], ssid, 32);
        clients[idx].probeSSIDs[clients[idx].probeCount][32] = 0;
        clients[idx].probeCount++;
    }

    // Update last seen
    clients[idx].lastSeenEpoch = millis() / 1000;
    if (Config::gps().enabled) {
        GPSData gps = GPS::getData();
        if (gps.fix) {
            clients[idx].lastLat = gps.latitude;
            clients[idx].lastLon = gps.longitude;

            // Track locations
            uint8_t locIdx = clients[idx].locationIdx % 3;
            snprintf(clients[idx].locations[locIdx], 64, "%.6f,%.6f", gps.latitude, gps.longitude);
            clients[idx].locationIdx++;
        }
    }

    // Detect potential tail: same device at 3+ distinct GPS locations in same session
    if (clients[idx].locationIdx >= 3 && !clients[idx].isTrackingUs) {
        clients[idx].isTrackingUs = true;
        logToSD("POTENTIAL_TAIL", ssid);
        // Trigger higher threat assessment
        // (This would cross-reference with threat table)
    }
}

// === PMKID FINGERPRINT TRACKING ===
void ThreatDB::logPMKIDCapture(const uint8_t* pmkid, const uint8_t* stationMAC,
                                const uint8_t* apBSSID) {
    if (!pmkid) return;

    int idx = findPMKIDFingerprint(pmkid);
    bool isNew = (idx < 0);

    if (isNew) {
        if (pmkidFingerprintCount >= MAX_PMKID_FINGERPRINTS) return;
        idx = pmkidFingerprintCount;
        memcpy(pmkidFingerprints[idx].pmkid, pmkid, 16);
        if (stationMAC) memcpy(pmkidFingerprints[idx].stationMAC, stationMAC, 6);
        if (apBSSID) memcpy(pmkidFingerprints[idx].apBSSID, apBSSID, 6);
        pmkidFingerprints[idx].matchedCount = 1;

        if (Config::gps().enabled) {
            GPSData gps = GPS::getData();
            if (gps.fix) {
                pmkidFingerprints[idx].latitude = gps.latitude;
                pmkidFingerprints[idx].longitude = gps.longitude;
            }
        }

        pmkidFingerprints[idx].epochTime = millis() / 1000;
        pmkidFingerprintCount++;
    } else {
        pmkidFingerprints[idx].matchedCount++;
    }
}

int ThreatDB::findPMKIDFingerprint(const uint8_t* pmkid) {
    for (uint8_t i = 0; i < pmkidFingerprintCount; i++) {
        if (memcmp(pmkidFingerprints[i].pmkid, pmkid, 16) == 0) return i;
    }
    return -1;
}

bool ThreatDB::hasSeenDeviceBefore(const uint8_t* pmkid, double* outLat, double* outLon) {
    int idx = findPMKIDFingerprint(pmkid);
    if (idx < 0) return false;
    if (outLat) *outLat = pmkidFingerprints[idx].latitude;
    if (outLon) *outLon = pmkidFingerprints[idx].longitude;
    return true;
}

// === SAFE ZONE (stub for now — would load from SD config) ===
void ThreatDB::setSafeZone(const char* name, double lat, double lon, float radiusM) {
    // TODO: write to SD config file
}

bool ThreatDB::isInSafeZone(double lat, double lon) {
    // TODO: check against saved zones
    return false;
}

bool ThreatDB::isKnownSafeAP(const uint8_t* bssid) {
    // TODO: check against learned baseline
    return false;
}

// === WiGLE CROSS-REFERENCE (stub — needs WiFi connection) ===
bool ThreatDB::wigleLookup(const uint8_t* bssid) {
    // Would use FileServer WiFi to query api.wigle.net
    // For now, returns false (ghost network)
    return false;
}

bool ThreatDB::isGhostNetwork(const uint8_t* bssid) {
    // Without WiGLE access, all unknown networks are ghosts
    return true;
}

// === SIREN ===
bool ThreatDB::shouldTriggerSiren() {
    return (millis() - lastSirenTime) > 10000;  // Max once per 10s
}

ThreatRecord ThreatDB::getLastTriggeredThreat() {
    return lastSirenThreat;
}

// === SD LOGGING ===
void ThreatDB::logToSD(const char* eventType, const char* details) {
    // Get GPS if available
    double lat = 0, lon = 0;
    if (Config::gps().enabled) {
        GPSData gps = GPS::getData();
        if (gps.fix) { lat = gps.latitude; lon = gps.longitude; }
    }

    // Ensure directory exists
    SD.mkdir("/m5ap_elim/threats");

    // Append to CSV
    File f = SD.open(THREAT_LOG_PATH, FILE_APPEND);
    if (f) {
        f.printf("%lu,%s,%.6f,%.6f,%s\n", millis()/1000, eventType, lat, lon,
                 details ? details : "null");
        f.close();
    }
}

// === PERSISTENCE ===
void ThreatDB::saveThreatLog() {
    // Already logged in real-time via logToSD()
}

void ThreatDB::saveClientFootprints() {
    SD.mkdir("/m5ap_elim/threats");
    File f = SD.open(CLIENT_DB_PATH, FILE_WRITE);
    if (!f) return;

    f.write((const uint8_t*)&clientCount, 1);
    f.write((const uint8_t*)clients, sizeof(ClientFootprint) * clientCount);
    f.close();
}

bool ThreatDB::loadClientFootprints() {
    if (!SD.exists(CLIENT_DB_PATH)) return false;
    File f = SD.open(CLIENT_DB_PATH, FILE_READ);
    if (!f) return false;

    uint8_t count;
    f.read(&count, 1);
    if (count > MAX_CLIENTS) count = MAX_CLIENTS;
    f.read((uint8_t*)clients, sizeof(ClientFootprint) * count);
    clientCount = count;
    f.close();
    return true;
}

void ThreatDB::savePMKIDFingerprints() {
    SD.mkdir("/m5ap_elim/threats");
    File f = SD.open(PMKID_DB_PATH, FILE_WRITE);
    if (!f) return;

    f.write((const uint8_t*)&pmkidFingerprintCount, 1);
    f.write((const uint8_t*)pmkidFingerprints, sizeof(PMKIDFingerprint) * pmkidFingerprintCount);
    f.close();
}

bool ThreatDB::loadPMKIDFingerprints() {
    if (!SD.exists(PMKID_DB_PATH)) return false;
    File f = SD.open(PMKID_DB_PATH, FILE_READ);
    if (!f) return false;

    uint8_t count;
    f.read(&count, 1);
    if (count > MAX_PMKID_FINGERPRINTS) count = MAX_PMKID_FINGERPRINTS;
    f.read((uint8_t*)pmkidFingerprints, sizeof(PMKIDFingerprint) * count);
    pmkidFingerprintCount = count;
    f.close();
    return true;
}

// === INIT ===
void ThreatDB::init() {
    // Load persistent data
    loadClientFootprints();
    loadPMKIDFingerprints();
    Serial.printf("[THREAT] Loaded %u clients, %u PMKID fingerprints\n",
                  clientCount, pmkidFingerprintCount);
    SDLog::log("THREAT", "Engine initialized — %u fingerprints loaded",
               pmkidFingerprintCount);
}

// === UPDATE — called every loop ===
void ThreatDB::update() {
    // Prune stale threats (not seen in 5 minutes)
    uint32_t now = millis();
    for (uint8_t i = 0; i < threatCount; ) {
        if (now - threats[i].lastSeen > 300000 && threats[i].lastSeen > 0) {
            logToSD("THREAT_EXPIRED", threats[i].ssid);
            threats[i] = threats[--threatCount];
        } else {
            i++;
        }
    }

    // Check ghost protocol deactivation
    if (ghostProtocol) {
        ThreatLevel highest = getHighestThreatLevel();
        if (highest < ThreatLevel::LVL_HIGH) {
            // Deactivate after 30 seconds of no high threats
            // Would need a timer
        }
    }

    // Periodic save (every 60 seconds)
    static uint32_t lastSave = 0;
    if (now - lastSave > 60000) {
        saveClientFootprints();
        savePMKIDFingerprints();
        lastSave = now;
    }
}
