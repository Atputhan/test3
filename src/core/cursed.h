#pragma once
#include <Arduino.h>
#include <esp_attr.h>
#include <esp_wifi.h>
#include <M5Unified.h>

static const uint8_t BROADCAST_MAC[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

// Massacre counter — survives reboot, survives format
RTC_DATA_ATTR extern uint16_t cursed_massacreCount;
void cursed_massacreBegin();
void cursed_massacreUpdate();
bool cursed_massacreIsActive();
uint16_t cursed_massacreGetCount();

// BSSID Séance — RTC ghost storage, channels the dead
#define SEANCE_GHOST_COUNT 10
struct SeanceGhost {
    uint8_t bssid[6];
    char ssid[33];
    uint32_t timestamp;
};
RTC_DATA_ATTR extern SeanceGhost cursed_ghosts[SEANCE_GHOST_COUNT];
RTC_DATA_ATTR extern uint8_t cursed_ghostWriteIndex;
RTC_DATA_ATTR extern uint8_t cursed_ghostCount;
void cursed_ghostRecord(const uint8_t* bssid, const char* ssid);
void cursed_seanceBegin();
bool cursed_seanceIsActive();
void cursed_seanceUpdate();

// PMKID Marquee — scrolls captured PMKID hex right-to-left
void cursed_pmkidMarqueeSet(const uint8_t* pmkid, const char* ssid);
void cursed_pmkidMarqueeDraw(M5Canvas& canvas, int16_t y, uint16_t fg, uint16_t bg);

// Pig Jumpscare — full-screen flash on PMKID capture
void cursed_jumpscareTrigger();
bool cursed_jumpscareIsActive();
void cursed_jumpscareUpdate();

// Shutdown Ritual — the pig refuses to die quietly
void cursed_shutdownRitual();
bool cursed_shutdownIsActive();
void cursed_shutdownUpdate();
bool cursed_shutdownIsDone();

// USB Haptic — ASCII bell at aggressive moments
void cursed_usbBeep();
void cursed_usbBeepOnEvent(const char* eventName);

// Pig Body Horror — avatar corruption at high XP levels
uint8_t cursed_getCorruptionLevel();
void cursed_corruptBuffer(uint8_t* buffer, size_t len, uint8_t level);

// Init + Update
void cursed_init();
void cursed_update();
