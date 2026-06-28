// AP_Elim core state machine implementation

#include "ap_elim.h"
#include <M5Cardputer.h>
#include "../ui/display.h"
#include "../ui/menu.h"
#include "../ui/settings_menu.h"
#include "../ui/captures_menu.h"
#include "../ui/achievements_menu.h"
#include "../ui/bounty_status_menu.h"
#include "../ui/crash_viewer.h"
#include "../ui/diagnostics_menu.h"
#include "../ui/swine_stats.h"
#include "../ui/boar_bros_menu.h"
#include "../ui/wigle_menu.h"
#include "../ui/unlockables_menu.h"
#include "../ui/sd_format_menu.h"
#include "../piglet/mood.h"
#include "../piglet/avatar.h"
#include "../modes/oink.h"
#include "heap_policy.h"
#include "../modes/donoham.h"
#include "../modes/warhog.h"
#include "../modes/piggyblues.h"
#include "../modes/spectrum.h"
#include "../modes/pigsync_client.h"
#include "../modes/bacon.h"
#include "../modes/charging.h"
#include "../modes/threatscan.h"
#include "../modes/audioplayer.h"
#include "threat.h"
#include "../holmes/detective.h"
#include "../web/fileserver.h"
#include "../audio/sfx.h"
#include "config.h"
#include "heap_health.h"
#include "xp.h"
#include "sdlog.h"
#include "sd_format.h"
#include "challenges.h"
#include "stress_test.h"
#include "network_recon.h"
#include "wifi_utils.h"
#include <esp_heap_caps.h>
#include <esp_attr.h>
#include <esp_system.h>
#include <WiFi.h>

static const char* modeToString(AP_ElimMode mode) {
    switch (mode) {
        case AP_ElimMode::IDLE: return "IDLE";
        case AP_ElimMode::OINK_MODE: return "OINK";
        case AP_ElimMode::DNH_MODE: return "DNH";
        case AP_ElimMode::WARHOG_MODE: return "WARHOG";
        case AP_ElimMode::PIGGYBLUES_MODE: return "PIGGYBLUES";
        case AP_ElimMode::SPECTRUM_MODE: return "SPECTRUM";
        case AP_ElimMode::MENU: return "MENU";
        case AP_ElimMode::SETTINGS: return "SETTINGS";
        case AP_ElimMode::CAPTURES: return "CAPTURES";
        case AP_ElimMode::ACHIEVEMENTS: return "ACHIEVEMENTS";
        case AP_ElimMode::FILE_TRANSFER: return "FILE_TRANSFER";
        case AP_ElimMode::CRASH_VIEWER: return "CRASH_VIEWER";
        case AP_ElimMode::DIAGNOSTICS: return "DIAGNOSTICS";
        case AP_ElimMode::SWINE_STATS: return "SWINE_STATS";
        case AP_ElimMode::BOAR_BROS: return "BOAR_BROS";
        case AP_ElimMode::WIGLE_MENU: return "WIGLE_MENU";
        case AP_ElimMode::UNLOCKABLES: return "UNLOCKABLES";
        case AP_ElimMode::BOUNTY_STATUS: return "BOUNTY_STATUS";
        case AP_ElimMode::PIGSYNC_DEVICE_SELECT: return "PIGSYNC_DEVICE_SELECT";
        case AP_ElimMode::BACON_MODE: return "BACON";
        case AP_ElimMode::SD_FORMAT: return "SD_FORMAT";
        case AP_ElimMode::THREATSCAN_MODE: return "THREATSCAN";
        case AP_ElimMode::AUDIO_PLAYER_MODE: return "AUDIO_PLAYER";
        case AP_ElimMode::CHARGING: return "CHARGING";
        case AP_ElimMode::ABOUT: return "ABOUT";
        default: return "UNKNOWN";
    }
}

// Crash-loop guard: count early reboots using RTC memory (survives soft resets).
RTC_DATA_ATTR static uint8_t bootGuardStreak = 0;
static uint32_t bootGuardStartMs = 0;
static const uint8_t BOOT_GUARD_THRESHOLD = 3;
static const uint32_t BOOT_GUARD_WINDOW_MS = 60000;

static AP_ElimMode bootModeToAP_Elim(BootMode mode) {
    switch (mode) {
        case BootMode::OINK: return AP_ElimMode::OINK_MODE;
        case BootMode::DNOHAM: return AP_ElimMode::DNH_MODE;
        case BootMode::WARHOG: return AP_ElimMode::WARHOG_MODE;
        case BootMode::IDLE:
        default:
            return AP_ElimMode::IDLE;
    }
}

static const char* bootModeLabel(BootMode mode) {
    switch (mode) {
        case BootMode::OINK: return "OINK";
        case BootMode::DNOHAM: return "DN0HAM";
        case BootMode::WARHOG: return "WARHOG";
        case BootMode::IDLE:
        default:
            return "IDLE";
    }
}

static bool healthBootToastShown = false;

AP_Elim::AP_Elim() 
    : currentMode(AP_ElimMode::IDLE)
    , previousMode(AP_ElimMode::IDLE)
    , startTime(0)
    , handshakeCount(0)
    , networkCount(0)
    , deauthCount(0) {
}

static bool isAutoConditionSafe(AP_ElimMode mode) {
    switch (mode) {
        case AP_ElimMode::IDLE:
        case AP_ElimMode::MENU:
        case AP_ElimMode::SETTINGS:
        case AP_ElimMode::ABOUT:
        case AP_ElimMode::ACHIEVEMENTS:
        case AP_ElimMode::CRASH_VIEWER:
        case AP_ElimMode::DIAGNOSTICS:
        case AP_ElimMode::SWINE_STATS:
        case AP_ElimMode::BOAR_BROS:
        case AP_ElimMode::UNLOCKABLES:
        case AP_ElimMode::BOUNTY_STATUS:
        case AP_ElimMode::SD_FORMAT:
            return true;
        default:
            return false;
    }
}

static void maybeAutoConditionHeap(AP_ElimMode mode) {
    if (!isAutoConditionSafe(mode)) {
        return;
    }
    if (FileServer::isRunning() || FileServer::isConnecting()) {
        return;
    }
    if (WiFi.status() == WL_CONNECTED) {
        return;
    }
    // At Critical pressure (<30KB free), brew needs 35KB transient — would fail anyway
    if (static_cast<uint8_t>(HeapHealth::getPressureLevel()) > HeapPolicy::kMaxPressureLevelForAutoBrew) {
        return;
    }
    if (!HeapHealth::consumeConditionRequest()) {
        return;
    }

    bool wasReconRunning = NetworkRecon::isRunning();
    if (wasReconRunning) {
        NetworkRecon::pause();
    }
    // Small, low-disruption brew to coalesce heap when health drops.
    WiFiUtils::brewHeap(HeapPolicy::kBrewAutoDwellMs, false);
    if (wasReconRunning) {
        NetworkRecon::resume();
    }
}

void AP_Elim::init() {
    startTime = millis();
    
    // Initialize background network reconnaissance service
    NetworkRecon::init();
    
    // Initialize XP system
    XP::init();
    
    // Initialize SwineStats (buff/debuff system)
    SwineStats::init();
    
    // Register level up callback to show popup
    XP::setLevelUpCallback([](uint8_t oldLevel, uint8_t newLevel) {
        Display::showLevelUp(oldLevel, newLevel);
        Avatar::cuteJump();  // Celebratory jump on level up!
        
        // Check if class tier changed (every 5 levels: 6, 11, 16, 21, 26, 31, 36)
        PorkClass oldClass = XP::getClassForLevel(oldLevel);
        PorkClass newClass = XP::getClassForLevel(newLevel);
        if (newClass != oldClass) {
            // Small delay between popups
            delay(500);
            Display::showClassPromotion(
                XP::getClassNameFor(oldClass),
                XP::getClassNameFor(newClass)
            );
        }
    });
    
    // Register default event handlers
    registerCallback(AP_ElimEvent::HANDSHAKE_CAPTURED, [this](AP_ElimEvent, void*) {
        handshakeCount++;
    });
    
    registerCallback(AP_ElimEvent::NETWORK_FOUND, [this](AP_ElimEvent, void*) {
        networkCount++;
    });
    
    registerCallback(AP_ElimEvent::DEAUTH_SENT, [this](AP_ElimEvent, void*) {
        deauthCount++;
    });
    
    // Menu selection handler - items now defined in menu.cpp as static arrays
    Menu::setCallback([this](uint8_t actionId) {
        switch (actionId) {
            case 1: setMode(AP_ElimMode::OINK_MODE); break;
            case 2: setMode(AP_ElimMode::WARHOG_MODE); break;
            case 3: setMode(AP_ElimMode::FILE_TRANSFER); break;
            case 4: setMode(AP_ElimMode::CAPTURES); break;
            case 5: setMode(AP_ElimMode::SETTINGS); break;
            case 6: setMode(AP_ElimMode::ABOUT); break;
            case 7: setMode(AP_ElimMode::CRASH_VIEWER); break;
            case 8: setMode(AP_ElimMode::PIGGYBLUES_MODE); break;
            case 9: setMode(AP_ElimMode::ACHIEVEMENTS); break;
            case 10: setMode(AP_ElimMode::SPECTRUM_MODE); break;
            case 11: setMode(AP_ElimMode::SWINE_STATS); break;
            case 12: setMode(AP_ElimMode::BOAR_BROS); break;
            case 13: setMode(AP_ElimMode::WIGLE_MENU); break;
            case 14: setMode(AP_ElimMode::DNH_MODE); break;
            case 15: setMode(AP_ElimMode::UNLOCKABLES); break;
            case 16: setMode(AP_ElimMode::PIGSYNC_DEVICE_SELECT); break;
            case 17: setMode(AP_ElimMode::BOUNTY_STATUS); break;
            case 18: setMode(AP_ElimMode::BACON_MODE); break;
            case 19: setMode(AP_ElimMode::DIAGNOSTICS); break;
            case 20: setMode(AP_ElimMode::SD_FORMAT); break;
            case 21: setMode(AP_ElimMode::CHARGING); break;
            case 22: setMode(AP_ElimMode::THREATSCAN_MODE); break;
            case 23: setMode(AP_ElimMode::AUDIO_PLAYER_MODE); break;
        }
    });

    bootGuardStartMs = millis();
    if (bootGuardStreak < 255) {
        bootGuardStreak++;
    }
    bool bootGuardActive = bootGuardStreak >= BOOT_GUARD_THRESHOLD;

    BootMode bootMode = Config::personality().bootMode;
    bootModeTarget = bootModeToAP_Elim(bootMode);
    if (bootModeTarget != AP_ElimMode::IDLE && !bootGuardActive) {
        bootModePending = true;
        bootModeStartMs = millis();
        char buf[32];
        snprintf(buf, sizeof(buf), "BOOT -> %s IN 5S", bootModeLabel(bootMode));
        Display::showToast(buf, 5000);
    } else if (bootModeTarget != AP_ElimMode::IDLE && bootGuardActive) {
        Display::showToast("BOOT GUARD - IDLE", 4000);
    }
    
    Avatar::setState(AvatarState::HAPPY);
    
    // Initialize non-blocking audio system
    SFX::init();

    if (!healthBootToastShown) {
        healthBootToastShown = true;
        Display::showToast(
            "HEALTH BAR IS HEAP HEALTH.\n"
            "LARGEST CONTIG DRIVES TLS.\n"
            "FRAGMENTATION YOINKS IT.\n"
            "BREW FIXES. AP_ELIM READY.",
            5000
        );
    }
    
    Serial.println("[AP_ELIM] Initialized");
    SDLog::log("PORK", "Initialized - LV%d %s", XP::getLevel(), XP::getTitle());
    cursed_init();

}

void AP_Elim::update() {
    cursed_update();
    ThreatDB::update();
    Detective::update();
    // Update background network reconnaissance (channel hopping, cleanup)
    NetworkRecon::update();
    
    processEvents();
    yield(); // Allow other tasks to run between operations
    handleInput();
    yield(); // Allow other tasks to run between operations
    
    if (bootGuardStreak > 0 && (millis() - bootGuardStartMs >= BOOT_GUARD_WINDOW_MS)) {
        bootGuardStreak = 0;
    }
    if (bootModePending) {
        if (currentMode != AP_ElimMode::IDLE) {
            bootModePending = false;
        } else if (millis() - bootModeStartMs >= 5000) {
            bootModePending = false;
            setMode(bootModeTarget);
        }
    }
    updateMode();

    maybeAutoConditionHeap(currentMode);
    
    // Tick non-blocking audio engine
    SFX::update();
    yield(); // Allow other tasks to run between operations
    
    // Process one queued achievement celebration (debounced)
    XP::processAchievementQueue();
    yield(); // Allow other tasks to run between operations
    
    // Stress test injection (if active)
    StressTest::update();
    yield(); // Allow other tasks to run between operations
    
    // Check for session time XP bonuses
    XP::updateSessionTime();
    yield(); // Allow other tasks to run between operations
}

void AP_Elim::setMode(AP_ElimMode mode) {
    if (mode == currentMode) return;
    
    // Store the mode we're leaving for cleanup
    AP_ElimMode oldMode = currentMode;

    Serial.printf("[MODE] EXIT %s free=%u largest=%u\n",
        modeToString(oldMode),
        (unsigned)esp_get_free_heap_size(),
        (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
    
    // Save "real" modes as previous (not modal menus)
    // Exception: CAPTURES and WIGLE_MENU ARE saved as previousMode so OINK recovery returns to them
    if (currentMode != AP_ElimMode::SETTINGS &&
        currentMode != AP_ElimMode::ABOUT &&
        currentMode != AP_ElimMode::ACHIEVEMENTS &&
        currentMode != AP_ElimMode::MENU &&
        currentMode != AP_ElimMode::FILE_TRANSFER &&
        currentMode != AP_ElimMode::CRASH_VIEWER &&
        currentMode != AP_ElimMode::DIAGNOSTICS &&
        currentMode != AP_ElimMode::SWINE_STATS &&
        currentMode != AP_ElimMode::BOAR_BROS &&
        currentMode != AP_ElimMode::BOUNTY_STATUS &&
        currentMode != AP_ElimMode::PIGSYNC_DEVICE_SELECT &&
        currentMode != AP_ElimMode::UNLOCKABLES &&
        currentMode != AP_ElimMode::SD_FORMAT) {
        previousMode = currentMode;
    }
    // ALSO save CAPTURES and WIGLE_MENU as return points from OINK recovery
    if (currentMode == AP_ElimMode::CAPTURES ||
        currentMode == AP_ElimMode::WIGLE_MENU) {
        previousMode = currentMode;
    }
    currentMode = mode;

    Serial.printf("[MODE] ENTER %s free=%u largest=%u\n",
        modeToString(currentMode),
        (unsigned)esp_get_free_heap_size(),
        (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
    
    // Cleanup the mode we're actually leaving (oldMode), not previousMode
    switch (oldMode) {
        case AP_ElimMode::OINK_MODE:
            OinkMode::stop();
            break;
        case AP_ElimMode::DNH_MODE:
            DoNoHamMode::stop();
            break;
        case AP_ElimMode::WARHOG_MODE:
            WarhogMode::stop();
            break;
        case AP_ElimMode::PIGGYBLUES_MODE:
            PiggyBluesMode::stop();
            break;
        case AP_ElimMode::SPECTRUM_MODE:
            SpectrumMode::stop();
            break;
        case AP_ElimMode::MENU:
            Menu::hide();
            break;
        case AP_ElimMode::SETTINGS:
            SettingsMenu::hide();
            break;
        case AP_ElimMode::CAPTURES:
            CapturesMenu::hide();
            break;
        case AP_ElimMode::ACHIEVEMENTS:
            AchievementsMenu::hide();
            break;
        case AP_ElimMode::FILE_TRANSFER:
            FileServer::stop();
            // Restart NetworkRecon after FILE_TRANSFER to resume background scanning
            NetworkRecon::start();
            break;
        case AP_ElimMode::CRASH_VIEWER:
            CrashViewer::hide();
            break;
        case AP_ElimMode::DIAGNOSTICS:
            DiagnosticsMenu::hide();
            break;
        case AP_ElimMode::SD_FORMAT:
            SdFormatMenu::hide();
            break;
        case AP_ElimMode::SWINE_STATS:
            SwineStats::hide();
            break;
        case AP_ElimMode::BOAR_BROS:
            BoarBrosMenu::hide();
            break;
        case AP_ElimMode::WIGLE_MENU:
            WigleMenu::hide();
            break;
        case AP_ElimMode::UNLOCKABLES:
            UnlockablesMenu::hide();
            break;
        case AP_ElimMode::BOUNTY_STATUS:
            BountyStatusMenu::hide();
            break;
        case AP_ElimMode::PIGSYNC_DEVICE_SELECT:
            PigSyncMode::stopDiscovery();
            PigSyncMode::stop();
            break;
        case AP_ElimMode::BACON_MODE:
            BaconMode::stop();
            break;
        case AP_ElimMode::CHARGING:
            ChargingMode::stop();
            break;
        default:
            break;
    }
    
    // Init new mode
    switch (currentMode) {
        case AP_ElimMode::IDLE:
            Avatar::setState(AvatarState::NEUTRAL);
            Mood::onIdle();
            XP::save();  // Save XP when returning to idle
            SDLog::log("PORK", "Mode: IDLE");
            break;
        case AP_ElimMode::OINK_MODE:
            Avatar::setState(AvatarState::HUNTING);
            Display::notify(NoticeKind::STATUS, "PROPER MAD ONE INNIT", 5000, NoticeChannel::TOP_BAR);
            SDLog::log("PORK", "Mode: OINK");
            OinkMode::start();
            break;
        case AP_ElimMode::DNH_MODE:
            Avatar::setState(AvatarState::NEUTRAL);  // Calm, passive state
            SDLog::log("PORK", "Mode: DO NO HAM");
            DoNoHamMode::start();
            break;
        case AP_ElimMode::WARHOG_MODE:
            Avatar::setState(AvatarState::EXCITED);
            Display::notify(NoticeKind::STATUS, "SNIFFING THE AIR", 5000, NoticeChannel::TOP_BAR);
            SDLog::log("PORK", "Mode: WARHOG");
            // Disable ML/Enhanced features for heap savings
            {
                auto mlCfg = Config::ml();
                mlCfg.enabled = false;
                mlCfg.collectionMode = MLCollectionMode::BASIC;
                Config::setML(mlCfg);
            }
            WarhogMode::start();
            break;
        case AP_ElimMode::PIGGYBLUES_MODE:
            Avatar::setState(AvatarState::ANGRY);
            SDLog::log("PORK", "Mode: PIGGYBLUES");
            PiggyBluesMode::start();
            // If user aborted warning dialog, return to menu
            if (!PiggyBluesMode::isRunning()) {
                currentMode = AP_ElimMode::MENU;
                Menu::show();
            }
            break;
        case AP_ElimMode::SPECTRUM_MODE:
            Avatar::setState(AvatarState::HUNTING);
            SDLog::log("PORK", "Mode: SPECTRUM");
            SpectrumMode::start();
            break;
        case AP_ElimMode::MENU:
            Menu::show();
            break;
        case AP_ElimMode::SETTINGS:
            SettingsMenu::show();
            break;
        case AP_ElimMode::CAPTURES:
            CapturesMenu::show();
            break;
        case AP_ElimMode::ACHIEVEMENTS:
            AchievementsMenu::show();
            break;
        case AP_ElimMode::FILE_TRANSFER:
            // Stop NetworkRecon and free its ~19KB network vector — FILE_TRANSFER doesn't use it
            NetworkRecon::stop();
            NetworkRecon::freeNetworks();
            Avatar::setState(AvatarState::HAPPY);
            FileServer::start(Config::wifi().otaSSID, Config::wifi().otaPassword);
            break;
        case AP_ElimMode::CRASH_VIEWER:
            CrashViewer::show();
            break;
        case AP_ElimMode::DIAGNOSTICS:
            DiagnosticsMenu::show();
            break;
        case AP_ElimMode::SD_FORMAT:
            SdFormatMenu::show();
            break;
        case AP_ElimMode::SWINE_STATS:
            SwineStats::show();
            break;
        case AP_ElimMode::BOAR_BROS:
            BoarBrosMenu::show();
            break;
        case AP_ElimMode::WIGLE_MENU:
            WigleMenu::show();
            break;
        case AP_ElimMode::UNLOCKABLES:
            UnlockablesMenu::show();
            break;
        case AP_ElimMode::BOUNTY_STATUS:
            BountyStatusMenu::show();
            break;
        case AP_ElimMode::PIGSYNC_DEVICE_SELECT:
            Avatar::setState(AvatarState::EXCITED);
            SDLog::log("PORK", "Mode: PIGSYNC Device Select");
            PigSyncMode::start();
            PigSyncMode::startDiscovery();
            break;
        case AP_ElimMode::BACON_MODE:
            Avatar::setState(AvatarState::HAPPY);
            SDLog::log("PORK", "Mode: BACON");
            BaconMode::init();
            BaconMode::start();
            break;
        case AP_ElimMode::ABOUT:
            Display::resetAboutState();
            break;
        case AP_ElimMode::CHARGING:
            SDLog::log("PORK", "Mode: CHARGING");
            if (!shutdownAnimating) {
                shutdownAnimating = true;
                shutdownSequence();
            }
            ChargingMode::start();
            break;
        default:
            break;
    }
    
    postEvent(AP_ElimEvent::MODE_CHANGE, nullptr);
}

void AP_Elim::postEvent(AP_ElimEvent event, void* data) {
    // Prevent event queue overflow that could cause heap fragmentation
    if (eventQueue.size() >= MAX_EVENT_QUEUE_SIZE) {
        // Drop oldest event to maintain queue size
        eventQueue.erase(eventQueue.begin());
    }
    eventQueue.push_back({event, data});
}

void AP_Elim::registerCallback(AP_ElimEvent event, EventCallback callback) {
    // Prevent duplicate callbacks for the same event to avoid multiple executions
    // Note: We can't reliably compare std::function objects, so we just ensure each event
    // type has only one callback by replacing any existing one
    for (auto& pair : callbacks) {
        if (pair.first == event) {
            pair.second = callback; // Replace existing callback
            return;
        }
    }
    // Add bounds checking to prevent unlimited growth
    if (callbacks.size() >= MAX_EVENT_QUEUE_SIZE) {
        // Remove the oldest callback if we're at capacity
        callbacks.erase(callbacks.begin());
    }
    callbacks.push_back({event, callback});
}

void AP_Elim::processEvents() {
    // Process events with bounds checking and yield for WDT safety
    // NOTE: All postEvent() callers pass nullptr for data — no ownership to track.
    size_t processed = 0;
    const size_t MAX_EVENTS_PER_UPDATE = 16; // Limit events processed per update to prevent WDT

    // Index-based loop to avoid iterator invalidation from erase()
    size_t i = 0;
    while (i < eventQueue.size() && processed < MAX_EVENTS_PER_UPDATE) {
        const auto& item = eventQueue[i];

        for (const auto& cb : callbacks) {
            if (cb.first == item.event) {
                cb.second(item.event, item.data);

                if (++processed % 4 == 0) {
                    yield();
                }
            }
        }
        i++;
    }

    // Erase all processed events in one operation after the loop
    if (i >= eventQueue.size()) {
        eventQueue.clear();
    } else {
        eventQueue.erase(eventQueue.begin(), eventQueue.begin() + i);
    }
}

void AP_Elim::handleInput() {
    // G0 button (GPIO0 on top side) - configurable action
    static bool g0WasPressed = false;
    bool g0Pressed = (digitalRead(0) == LOW);  // G0 is active LOW

    if (g0Pressed && !g0WasPressed) {
        G0Action g0Action = Config::personality().g0Action;
        if (g0Action != G0Action::SCREEN_TOGGLE) {
            Display::resetDimTimer();  // Wake screen on G0
        }
        Serial.printf("[AP_ELIM] G0 pressed! Current mode: %d\n", (int)currentMode);
        switch (g0Action) {
            case G0Action::SCREEN_TOGGLE:
                Display::toggleScreenPower();
                break;
            case G0Action::OINK:
                setMode(AP_ElimMode::OINK_MODE);
                break;
            case G0Action::DNOHAM:
                setMode(AP_ElimMode::DNH_MODE);
                break;
            case G0Action::SPECTRUM:
                setMode(AP_ElimMode::SPECTRUM_MODE);
                break;
            case G0Action::PIGSYNC:
                setMode(AP_ElimMode::PIGSYNC_DEVICE_SELECT);
                break;
            case G0Action::IDLE:
                setMode(AP_ElimMode::IDLE);
                break;
            default:
                break;
        }
        g0WasPressed = true;
        return;
    }
    if (!g0Pressed) {
        g0WasPressed = false;
    }
    
    if (!M5Cardputer.Keyboard.isChange()) return;
    
    // Any keyboard input resets the screen dim timer
    Display::resetDimTimer();
    
    auto keys = M5Cardputer.Keyboard.keysState();
    // ESC maps to the key above Tab (shares ` / ~)
    bool escPressed = M5Cardputer.Keyboard.isKeyPressed('`');

    // ESC to return to IDLE from any active mode
    if (escPressed && currentMode != AP_ElimMode::IDLE) {
        setMode(AP_ElimMode::IDLE);
        return;
    }
    
    // In MENU mode, let Menu::handleInput() process navigation keys
    if (currentMode == AP_ElimMode::MENU) {
        // Do NOT return here - let Menu::update() handle navigation
        // But we already consumed isChange(), so Menu won't see it
        // Instead, call Menu::update() directly here
        Menu::update();
        yield(); // Allow other tasks to run during menu updates
        return;
    }
    
    // In SETTINGS mode, let SettingsMenu handle everything
    if (currentMode == AP_ElimMode::SETTINGS) {
        // Check if settings wants to exit
        if (SettingsMenu::shouldExit()) {
            SettingsMenu::clearExit();
            SettingsMenu::hide();
            setMode(AP_ElimMode::MENU);
        }
        return;
    }

    // In PIGSYNC_DEVICE_SELECT mode, handle navigation and channel switching
    if (currentMode == AP_ElimMode::PIGSYNC_DEVICE_SELECT) {
        uint8_t deviceCount = PigSyncMode::getDeviceCount();

        // Handle device navigation (up/down) - only if devices exist
        if (deviceCount > 0) {
            if (M5Cardputer.Keyboard.isKeyPressed(';')) {
                // Up arrow - select previous device
                PigSyncMode::selectDevice(PigSyncMode::getSelectedIndex() > 0 ?
                    PigSyncMode::getSelectedIndex() - 1 : deviceCount - 1);
            }
            if (M5Cardputer.Keyboard.isKeyPressed('.')) {
                // Down arrow - select next device
                PigSyncMode::selectDevice((PigSyncMode::getSelectedIndex() + 1) % deviceCount);
            }
        }

        // Enter to connect to selected device
        if (M5Cardputer.Keyboard.isKeyPressed(KEY_ENTER) && PigSyncMode::getDeviceCount() > 0) {
            uint8_t selectedIdx = PigSyncMode::getSelectedIndex();
            if (selectedIdx < PigSyncMode::getDeviceCount()) {
                PigSyncMode::connectTo(selectedIdx);
            }
        }

        // A to abort sync (when connected)
        if (PigSyncMode::isConnected() && M5Cardputer.Keyboard.isKeyPressed('a')) {
            if (PigSyncMode::isSyncing()) {
                PigSyncMode::abortSync();
            }
        }

        // D to disconnect (when connected)
        if (PigSyncMode::isConnected() && M5Cardputer.Keyboard.isKeyPressed('d')) {
            PigSyncMode::disconnect();
        }

        // R to rescan (when not connected)
        if (!PigSyncMode::isConnected() && M5Cardputer.Keyboard.isKeyPressed('r')) {
            PigSyncMode::startScan();
        }

        return; // Consume input for PIGSYNC_DEVICE_SELECT
    }
    
    // Backtick opens menu from IDLE (kept out of back/exit flow)
    if (currentMode == AP_ElimMode::IDLE &&
        M5Cardputer.Keyboard.isKeyPressed('`')) {
        setMode(AP_ElimMode::MENU);
        return;
    }
    
    // Screenshot with P key (global, works in any mode)
    if (M5Cardputer.Keyboard.isKeyPressed('p') || M5Cardputer.Keyboard.isKeyPressed('P')) {
        if (!Display::isSnapping()) {
            Display::takeScreenshot();
        }
        return;
    }
    
    // T key stress test cycle disabled
    
    // Enter key in About mode - easter egg
    if (M5Cardputer.Keyboard.isKeyPressed(KEY_ENTER)) {
        if (currentMode == AP_ElimMode::ABOUT) {
            Display::onAboutEnterPressed();
            return;
        }
    }
    
    // Mode shortcuts when in IDLE
    if (currentMode == AP_ElimMode::IDLE) {
        for (auto c : keys.word) {
            switch (c) {
                case 'o': // Oink mode
                case 'O':
                    setMode(AP_ElimMode::OINK_MODE);
                    break;
                case 'w': // Warhog mode
                case 'W':
                    setMode(AP_ElimMode::WARHOG_MODE);
                    break;
                case 'b': // Piggy Blues mode
                case 'B':
                    setMode(AP_ElimMode::PIGGYBLUES_MODE);
                    break;
                case 'h': // HOG ON SPECTRUM mode
                case 'H':
                    setMode(AP_ElimMode::SPECTRUM_MODE);
                    break;
                case 's': // SWINE STATS
                case 'S':
                    setMode(AP_ElimMode::SWINE_STATS);
                    break;
                case 't': // Settings (Tweak)
                case 'T':
                    setMode(AP_ElimMode::SETTINGS);
                    break;
                case 'd': // DO NO HAM mode
                case 'D':
                    setMode(AP_ElimMode::DNH_MODE);
                    break;
                case 'f': // File transfer (AP_ELIM COMMANDER)
                case 'F':
                    setMode(AP_ElimMode::FILE_TRANSFER);
                    break;
                case '1': // PIG DEMANDS overlay
                    Display::showChallenges();
                    break;
                case '2': // PIGSYNC device select
                    setMode(AP_ElimMode::PIGSYNC_DEVICE_SELECT);
                    break;
                case 'r': case 'R': setMode(AP_ElimMode::THREATSCAN_MODE); break;
                case 'z': case 'Z': setMode(AP_ElimMode::AUDIO_PLAYER_MODE); break;
                case 'c': // Charging mode
                case 'C':
                    setMode(AP_ElimMode::CHARGING);
                    break;
                case 'm': // Massacre
                case 'M':
                    triggerMassacre();
                    break;
                case 'v': // BSSID Seance
                case 'V':
                    triggerSeance();
                    break;
            }
        }
        yield(); // Allow other tasks to run after processing all keys
    }
    
    // OINK mode - B to exclude network
    if (currentMode == AP_ElimMode::OINK_MODE) {
        // B key - add selected network to BOAR BROS exclusion list
        static bool bWasPressed = false;
        bool bPressed = M5Cardputer.Keyboard.isKeyPressed('b') || M5Cardputer.Keyboard.isKeyPressed('B');
        if (bPressed && !bWasPressed) {
            int idx = OinkMode::getSelectionIndex();
            if (OinkMode::excludeNetwork(idx)) {
                Display::showToast("BOAR BRO ADDED!");
                delay(500);
                OinkMode::moveSelectionDown();
            } else {
                Display::showToast("ALREADY A BRO");
                delay(500);
            }
        }
        bWasPressed = bPressed;
        
        // D key - switch to DO NO HAM mode (seamless mode switch)
        static bool dWasPressed_oink = false;
        bool dPressed = M5Cardputer.Keyboard.isKeyPressed('d') || M5Cardputer.Keyboard.isKeyPressed('D');
        if (dPressed && !dWasPressed_oink) {
            // Track passive time for achievements
            SessionStats& sess = const_cast<SessionStats&>(XP::getSession());
            sess.passiveTimeStart = millis();
            
            // Show toast before mode switch (loading screen)
            Display::notify(NoticeKind::STATUS, "IRIE VIBES ONLY NOW", 0, NoticeChannel::TOP_BAR);
            delay(800);
            
            // Seamless switch to DNH mode
            setMode(AP_ElimMode::DNH_MODE);
            return;  // Prevent fall-through to DNH block this frame
        }
        dWasPressed_oink = dPressed;
    }
    
    // DNH mode - O key to switch back to OINK
    if (currentMode == AP_ElimMode::DNH_MODE) {
        // O key - switch back to OINK mode (seamless mode switch)
        static bool oWasPressed_dnh = false;
        bool oPressed = M5Cardputer.Keyboard.isKeyPressed('o') || M5Cardputer.Keyboard.isKeyPressed('O');
        if (oPressed && !oWasPressed_dnh) {
            // Clear passive time tracking
            SessionStats& sess = const_cast<SessionStats&>(XP::getSession());
            sess.passiveTimeStart = 0;
            
            // Show toast before mode switch (loading screen)
            Display::notify(NoticeKind::STATUS, "PROPER MAD ONE INNIT", 0, NoticeChannel::TOP_BAR);
            delay(800);
            
            // Seamless switch to OINK mode
            setMode(AP_ElimMode::OINK_MODE);
            return;  // Prevent any subsequent key handling this frame
        }
        oWasPressed_dnh = oPressed;
    }
    
    // WARHOG mode - use ESC to return to idle
    if (currentMode == AP_ElimMode::WARHOG_MODE) {
        // no-op: ESC handled globally
    }
    
    // PIGGYBLUES mode - use ESC to return to idle
    if (currentMode == AP_ElimMode::PIGGYBLUES_MODE) {
        // no-op: ESC handled globally
    }
    
    
    // SPECTRUM mode - ESC returns to idle globally
    // If monitoring a network, Spectrum handles its own keys
    if (currentMode == AP_ElimMode::SPECTRUM_MODE) {
        // no-op: ESC handled globally
    }
    
    // FILE_TRANSFER mode - use ESC to return to idle
    if (currentMode == AP_ElimMode::FILE_TRANSFER) {
        // no-op: ESC handled globally
    }
    
    yield(); // Allow other tasks to run after processing input
}

void AP_Elim::updateMode() {
    switch (currentMode) {
        case AP_ElimMode::OINK_MODE:
            OinkMode::update();
            break;
        case AP_ElimMode::DNH_MODE:
            DoNoHamMode::update();
            break;
        case AP_ElimMode::WARHOG_MODE:
            WarhogMode::update();
            break;
        case AP_ElimMode::PIGGYBLUES_MODE:
            PiggyBluesMode::update();
            break;
        case AP_ElimMode::SPECTRUM_MODE:
            SpectrumMode::update();
            break;
        case AP_ElimMode::BACON_MODE:
            BaconMode::update();
            // Check if user exited
            if (!BaconMode::isRunning()) {
                setMode(AP_ElimMode::MENU);
            }
            break;
        case AP_ElimMode::CAPTURES:
            CapturesMenu::update();
            if (!CapturesMenu::isActive()) {
                setMode(AP_ElimMode::MENU);
            }
            break;
        case AP_ElimMode::ACHIEVEMENTS:
            AchievementsMenu::update();
            if (!AchievementsMenu::isActive()) {
                setMode(AP_ElimMode::MENU);
            }
            break;
        case AP_ElimMode::FILE_TRANSFER:
            FileServer::update();
            break;
        case AP_ElimMode::CRASH_VIEWER:
            CrashViewer::update();
            if (!CrashViewer::isActive()) {
                setMode(AP_ElimMode::MENU);
            }
            break;
        case AP_ElimMode::DIAGNOSTICS:
            DiagnosticsMenu::update();
            if (!DiagnosticsMenu::isActive()) {
                setMode(AP_ElimMode::MENU);
            }
            break;
        case AP_ElimMode::SD_FORMAT:
            SdFormatMenu::update();
            if (!SdFormatMenu::isActive()) {
                setMode(AP_ElimMode::MENU);
            }
            break;
        case AP_ElimMode::SWINE_STATS:
            SwineStats::update();
            if (!SwineStats::isActive()) {
                setMode(AP_ElimMode::MENU);
            }
            break;
        case AP_ElimMode::BOAR_BROS:
            BoarBrosMenu::update();
            if (!BoarBrosMenu::isActive()) {
                setMode(AP_ElimMode::MENU);
            }
            break;
        case AP_ElimMode::WIGLE_MENU:
            WigleMenu::update();
            if (!WigleMenu::isActive()) {
                setMode(AP_ElimMode::MENU);
            }
            break;
        case AP_ElimMode::UNLOCKABLES:
            UnlockablesMenu::update();
            if (!UnlockablesMenu::isActive()) {
                setMode(AP_ElimMode::MENU);
            }
            break;
        case AP_ElimMode::BOUNTY_STATUS:
            BountyStatusMenu::update();
            if (!BountyStatusMenu::isActive()) {
                setMode(AP_ElimMode::MENU);
            }
            break;
        case AP_ElimMode::PIGSYNC_DEVICE_SELECT:
            // Update PigSync discovery process (includes dialogue phases)
            PigSyncMode::update();
            // Stay in device select mode for terminal display
            if (!PigSyncMode::isRunning()) {
                // User exited, go back to menu
                setMode(AP_ElimMode::MENU);
            }
            break;
        case AP_ElimMode::CHARGING:
            ChargingMode::update();
            if (ChargingMode::shouldExit()) {
                setMode(AP_ElimMode::IDLE);
            }
            break;
        default:
            break;
    }
}

uint32_t AP_Elim::getUptime() const {
    return (millis() - startTime) / 1000;
}

uint16_t AP_Elim::getHandshakeCount() const {
    // Include both handshakes and PMKIDs - both are crackable captures
    return OinkMode::getCompleteHandshakeCount() + OinkMode::getPMKIDCount();
}

uint16_t AP_Elim::getNetworkCount() const {
    return OinkMode::getNetworkCount();
}

uint16_t AP_Elim::getDeauthCount() const {
    return OinkMode::getDeauthCount();
}

// ---- CURSED FEATURES ----
void AP_Elim::triggerMassacre() {
    if (currentMode != AP_ElimMode::IDLE && currentMode != AP_ElimMode::OINK_MODE) {
        Display::showToast("MASSACRE: IDLE ONLY", 2000);
        return;
    }
    cursed_massacreBegin();
    cursed_usbBeepOnEvent("MASSACRE TRIGGERED");
}
void AP_Elim::triggerSeance() {
    cursed_seanceBegin();
    if (cursed_seanceIsActive()) {
        Display::setTopBarMessage("SILENT SCREAM", 3000);
        cursed_usbBeep();
    }
}
void AP_Elim::shutdownSequence() {
    cursed_shutdownRitual();
}
uint16_t AP_Elim::getMassacreCount() const {
    return cursed_massacreCount;
}

