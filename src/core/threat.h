#pragma once
#include <Arduino.h>
#include <esp_wifi.h>
#include <vector>
#include <SD.h>
#include "../gps/gps.h"

// Threat classifications
enum class ThreatClass : uint8_t {
    UNKNOWN       = 0,
    AXON_BODY_CAM = 1,   // Police body camera
    STINGRAY      = 2,   // IMSI catcher / cell simulator
    ROGUE_AP      = 3,   // Evil twin / Karma attack AP
    GHOST_NET     = 4,   // Network not in WiGLE database
    EVIL_TWIN     = 5,   // Same SSID as legitimate but different BSSID/location
    SURVEILLANCE  = 6,   // General surveillance equipment
    KNOWN_SAFE    = 7,   // Matches safe zone baseline
    TAIL_DEVICE   = 8    // Client MAC seen at 3+ operator locations
};

// Threat severity levels
enum class ThreatLevel : uint8_t {
    LVL_NONE    = 0,
    LVL_LOW     = 1,
    LVL_MEDIUM  = 2,
    LVL_HIGH    = 3,
    LVL_CRITICAL = 4
};

// A single threat observation
struct ThreatRecord {
    uint8_t bssid[6];
    char ssid[33];
    ThreatClass classification;
    ThreatLevel level;
    uint8_t channel;
    int8_t rssi;              // Current RSSI
    int8_t rssiSlope;         // RSSI change per second (+ = approaching, - = leaving)
    float estimatedDistanceM; // Log-normal distance estimation
    uint32_t firstSeen;       // millis() first detection
    uint32_t lastSeen;        // millis() last detection
    bool hasGPS;
    double latitude;
    double longitude;
    uint8_t sightingCount;    // How many times this threat has been observed
    uint8_t locationCount;    // How many distinct GPS locations
    char lastLocation[64];    // Human-readable last location
};

// Persistent client tracking record (stored on SD)
struct ClientFootprint {
    uint8_t mac[6];
    char probeSSIDs[5][33];   // Last 5 probe request SSIDs
    uint8_t probeCount;
    uint8_t seenCount;        // How many separate sessions
    double firstLat;
    double firstLon;
    double lastLat;
    double lastLon;
    uint32_t lastSeenEpoch;
    char locations[3][64];    // Last 3 locations seen at
    uint8_t locationIdx;
    bool isTrackingUs;        // Flagged as potential tail
};

// PMKID fingerprint (cross-session device recognition)
struct PMKIDFingerprint {
    uint8_t pmkid[16];
    uint8_t stationMAC[6];
    uint8_t apBSSID[6];
    double latitude;
    double longitude;
    uint32_t epochTime;
    uint8_t matchedCount;     // How many times this PMKID was re-captured
};

// Threat database
class ThreatDB {
public:
    static constexpr uint8_t MAX_THREATS = 50;
    static constexpr uint8_t MAX_CLIENTS = 100;
    static constexpr uint8_t MAX_PMKID_FINGERPRINTS = 50;

    static void init();
    static void update();              // Called every loop iteration
    static void evaluate(const uint8_t* bssid, const char* ssid, uint8_t channel,
                         int8_t rssi, wifi_auth_mode_t authMode,
                         const uint8_t* vendorIE, uint16_t ieLen);
    
    // Access
    static uint8_t getThreatCount();
    static const ThreatRecord* getThreats(uint8_t& count);
    static ThreatLevel getHighestThreatLevel();
    static uint8_t getActiveTailCount();
    
    // Threat scoring
    static ThreatClass classify(const uint8_t* bssid, const char* ssid,
                                const uint8_t* vendorIE, uint16_t ieLen);
    static float estimateDistance(int8_t rssi, int8_t rssiSlope = 0);
    static ThreatLevel calculateThreatLevel(ThreatClass tc, int8_t rssi,
                                            float distance, uint8_t sightingCount,
                                            int8_t rssiSlope);
    
    // WiGLE cross-reference (async, uses FileServer WiFi)
    static bool wigleLookup(const uint8_t* bssid);
    static bool isGhostNetwork(const uint8_t* bssid);
    
    // Client / PMKID tracking
    static void logProbeRequest(const uint8_t* stationMAC, const char* ssid);
    static void logPMKIDCapture(const uint8_t* pmkid, const uint8_t* stationMAC,
                                const uint8_t* apBSSID);
    static int findPMKIDFingerprint(const uint8_t* pmkid);
    static bool hasSeenDeviceBefore(const uint8_t* pmkid, double* outLat = nullptr,
                                    double* outLon = nullptr);
    
    // Safe zone management
    static void setSafeZone(const char* name, double lat, double lon, float radiusM);
    static bool isInSafeZone(double lat, double lon);
    static bool isKnownSafeAP(const uint8_t* bssid);
    
    // Ghost Protocol
    static bool isGhostProtocolActive() { return ghostProtocol; }
    static void activateGhostProtocol();
    static void deactivateGhostProtocol();
    static ThreatLevel getGhostProtocolTriggerLevel() { return ThreatLevel::LVL_HIGH; }
    
    // Persistence
    static void saveThreatLog();
    static void saveClientFootprints();
    static bool loadClientFootprints();
    static void savePMKIDFingerprints();
    static bool loadPMKIDFingerprints();
    
    // Siren / alert
    static bool shouldTriggerSiren();
    static ThreatRecord getLastTriggeredThreat();
    
private:
    static ThreatRecord threats[MAX_THREATS];
    static uint8_t threatCount;
    static ClientFootprint clients[MAX_CLIENTS];
    static uint8_t clientCount;
    static PMKIDFingerprint pmkidFingerprints[MAX_PMKID_FINGERPRINTS];
    static uint8_t pmkidFingerprintCount;
    static bool ghostProtocol;
    static uint32_t lastSirenTime;
    static ThreatRecord lastSirenThreat;
    
    // Internal
    static int findThreat(const uint8_t* bssid);
    static int findClient(const uint8_t* mac);
    static void updateThreatRSSI(int idx, int8_t rssi);
    static void logToSD(const char* eventType, const char* details);
    static void updateGhostProtocol();
};
