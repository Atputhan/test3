// AP_Elim core state machine
#pragma once

#include <Arduino.h>
#include <functional>
#include <vector>

#include "cursed.h"

// Operating modes
enum class AP_ElimMode : uint8_t {
    IDLE = 0,       // Main screen, piglet idle
    OINK_MODE,      // Deauth + sniff mode
    DNH_MODE,       // DO NO HAM - passive recon (no attacks)
    WARHOG_MODE,    // Wardriving mode
    PIGGYBLUES_MODE,// BLE notification spam
    SPECTRUM_MODE,  // WiFi spectrum analyzer
    MENU,           // Menu navigation
    SETTINGS,       // Settings screen
    CAPTURES,       // View captured handshakes
    ACHIEVEMENTS,   // View achievements
    ABOUT,          // About screen
    FILE_TRANSFER,  // WiFi file transfer mode
    CRASH_VIEWER,     // Crash viewer
    DIAGNOSTICS,    // System diagnostics
    SWINE_STATS,    // Lifetime stats and buffs overlay
    BOAR_BROS,      // Manage excluded networks
    WIGLE_MENU,     // WiGLE file uploads
    UNLOCKABLES,    // Secret challenges menu
    BOUNTY_STATUS,  // View active bounties
    PIGSYNC_DEVICE_SELECT, // PigSync device selection
    PIGSYNC_CALL, // PigSync active call
    THREATSCAN_MODE,  // Live surveillance threat radar
    AUDIO_PLAYER_MODE, // WAV/MP3 player
    BACON_MODE,     // Hide and seek beacon broadcaster
    SD_FORMAT,      // SD card format utility
    CHARGING,        // Low power charging mode
};

// Events for async callbacks
enum class AP_ElimEvent : uint8_t {
    NONE = 0,
    MODE_CHANGE,
    ML_RESULT,
    GPS_FIX,
    GPS_LOST,
    HANDSHAKE_CAPTURED,
    NETWORK_FOUND,
    DEAUTH_SENT,
    ROGUE_AP_DETECTED,
    OTA_AVAILABLE,
    LOW_BATTERY
};

// Event callback type
using EventCallback = std::function<void(AP_ElimEvent, void*)>;

class AP_Elim {
public:
    AP_Elim();
    
    void init();
    void triggerMassacre();
    void triggerSeance();
    void shutdownSequence();
    uint16_t getMassacreCount() const;
    bool isShutdownAnimating() const { return shutdownAnimating; }

    
void update();
    
    // Mode control
    void setMode(AP_ElimMode mode);
    AP_ElimMode getMode() const { return currentMode; }
    
    // Event system
    void postEvent(AP_ElimEvent event, void* data = nullptr);
    void registerCallback(AP_ElimEvent event, EventCallback callback);
    
    // Stats
    uint32_t getUptime() const;
    uint16_t getHandshakeCount() const;  // Gets from OinkMode
    uint16_t getNetworkCount() const;     // Gets from OinkMode
    uint16_t getDeauthCount() const;      // Gets from OinkMode
    
private:
    AP_ElimMode currentMode;
    AP_ElimMode previousMode;
    
    uint32_t startTime;
    uint16_t handshakeCount;
    uint16_t networkCount;
    uint16_t deauthCount;

    // Boot mode auto-entry
    bool bootModePending = false;
    AP_ElimMode bootModeTarget = AP_ElimMode::IDLE;
    bool shutdownAnimating = false;
    uint32_t bootModeStartMs = 0;
    
    // Event queue with max capacity to prevent memory exhaustion
    static constexpr size_t MAX_EVENT_QUEUE_SIZE = 32;  // Prevent runaway allocations
    struct EventItem {
        AP_ElimEvent event;
        void* data;
    };
    std::vector<EventItem> eventQueue;
    std::vector<std::pair<AP_ElimEvent, EventCallback>> callbacks;
    
    void processEvents();
    void handleInput();
    void updateMode();
};

