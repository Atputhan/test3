#include "detective.h"
#include "../ui/display.h"
#include <M5Cardputer.h>
#include <stdlib.h>

// ASCII frames — same system as the pig, just a detective instead.
// 4 lines tall, rendered at 60fps via canvas.drawString/drawChar
// Hat + face + coat + cigarette

// NEUTRAL: standard stakeout
const char* Detective::FRAME_NEUTRAL_R[5] = {
    " .---. ",
    "( -  -)",
    "/|  | \\",
    " ^  ^  "
};

// HAPPY: case solved
const char* Detective::FRAME_HAPPY_R[5] = {
    " .---. ",
    "( ^  ^)",
    "/|  | \\",
    " ~~ ~~ "
};

// HUNTING: threat detected
const char* Detective::FRAME_HUNTING_R[5] = {
    " .---. ",  // brim pulled down
    "( =  =)",
    "/|  | \\",
    " @  @  "
};

// SAD: lost signal / failed capture
const char* Detective::FRAME_SAD_R[5] = {
    " .---. ",
    "( T  T)",
    "/|  | \\",
    " .. .. "
};

// ANGRY: critical threat
const char* Detective::FRAME_ANGRY_R[5] = {
    " .---. ",  // brim low, eyes hard
    "( #  #)",
    "/|  | \\",
    " !! !! "
};

// Static members
DetectiveState Detective::currentState = DetectiveState::STAKEOUT;
uint8_t Detective::threatLevel = 0;
uint8_t Detective::cigaretteGlow = 80;
uint8_t Detective::rainIntensity = 80;
bool Detective::thunderFlash = false;
uint32_t Detective::lastAnimUpdate = 0;
uint8_t Detective::animFrame = 0;
bool Detective::facingRight = true;
uint8_t Detective::hatAngle = 30;

void Detective::init() {
    currentState = DetectiveState::STAKEOUT;
    threatLevel = 0;
    cigaretteGlow = 80;
    rainIntensity = 80;
}

void Detective::setState(DetectiveState state) {
    currentState = state;
}

void Detective::setThreatLevel(uint8_t level) {
    threatLevel = level;
    if (level > 200) currentState = DetectiveState::HOT_PURSUIT;
    else if (level > 100) currentState = DetectiveState::INTERROGATION;
    else if (level > 50) currentState = DetectiveState::STAKEOUT;
    else currentState = DetectiveState::COLD_CASE;
}

void Detective::setCigaretteGlow(uint8_t brightness) {
    cigaretteGlow = brightness;
}

void Detective::setRainIntensity(uint8_t intensity) {
    rainIntensity = intensity;
}

void Detective::setThunderFlash(bool active) {
    thunderFlash = active;
}

void Detective::update() {
    uint32_t now = millis();

    // Cigarette flicker
    if (now - lastAnimUpdate > 200) {
        animFrame = (animFrame + 1) % 4;
        // Animate cigarette glow 
        if (random(0, 5) == 0) {
            cigaretteGlow += random(-20, 20);
            if (cigaretteGlow > 200) cigaretteGlow = 200;
            if (cigaretteGlow < 20) cigaretteGlow = 20;
        }
        lastAnimUpdate = now;
    }

    // Thunder flash (random)
    if (random(0, 500) == 0) thunderFlash = true;
    if (thunderFlash && now % 250 == 0) thunderFlash = false;
}

void Detective::drawCharacter(M5Canvas& canvas, const char** frame, uint8_t lines) {
    uint16_t colorFG = thunderFlash ? getColorBG() : getColorFG();
    uint16_t colorBG = thunderFlash ? getColorFG() : getColorBG();
    int x = 15;
    int y = 20;

    canvas.setTextColor(colorFG, colorBG);
    canvas.setTextSize(1);

    for (uint8_t i = 0; i < lines; i++) {
        if (frame[i]) {
            // Special character handling for cigarette glow
            canvas.drawString(frame[i], x, y + (i * 10));
        }
    }
}

void Detective::draw(M5Canvas& canvas) {
    if (currentState == DetectiveState::GHOSTED) {
        canvas.fillSprite(0x0000);
        canvas.setTextColor(0x6000);
        canvas.drawString("GHOST PROTOCOL", 5, 60, 1);
        return;
    }

    // Background
    canvas.fillSprite(COLOR_BG);

    // Venetian blinds
    drawVenetianBlinds(canvas);

    // Neon glow on horizon
    drawNeonGlow(canvas);

    // Character
    switch (currentState) {
        case DetectiveState::STAKEOUT:
            drawCharacter(canvas, FRAME_NEUTRAL_R, 4);
            break;
        case DetectiveState::INTERROGATION:
            drawCharacter(canvas, FRAME_HUNTING_R, 4);
            break;
        case DetectiveState::HOT_PURSUIT:
            drawCharacter(canvas, FRAME_ANGRY_R, 4);
            break;
        case DetectiveState::COLD_CASE:
            drawCharacter(canvas, FRAME_SAD_R, 4);
            break;
        default:
            drawCharacter(canvas, FRAME_NEUTRAL_R, 4);
            break;
    }

    // Cigarette glow (drawn on top of character at fixed position)
    if (cigaretteGlow > 10) {
        uint8_t r = map(cigaretteGlow, 0, 255, 0, 255);
        uint16_t glowColor = canvas.color565(r, r/3, 0);
        int cx = 55; // right side of character
        int cy = 40; // mouth level
        // Glow radius based on intensity
        canvas.drawPixel(cx, cy, glowColor);
        if (cigaretteGlow > 50) canvas.drawPixel(cx+1, cy, glowColor);
        if (cigaretteGlow > 100) canvas.drawPixel(cx-1, cy, glowColor);
        if (cigaretteGlow > 150) canvas.drawPixel(cx, cy-1, glowColor);
    }

    // Rain
    if (rainIntensity > 10) {
        drawRain(canvas);
    }
}

void Detective::drawVenetianBlinds(M5Canvas& canvas) {
    // Horizontal shadow bars (subtle noir effect)
    for (int y = 0; y < 135; y += 14) {
        canvas.fillRect(0, y, 240, 3, canvas.color565(0, 0, 15));
    }
}

void Detective::drawRain(M5Canvas& canvas) {
    // Simple rain streaks using character rendering
    uint16_t rainColor = canvas.color565(80, 80, 120);
    uint32_t now = millis();
    for (int i = 0; i < 10; i++) {
        int rx = ((now / 50 + i * 37) % 240);
        int ry = ((now * (1 + i % 3) / 100 + i * 47) % 135);
        canvas.drawFastVLine(rx, ry, 4, rainColor);
    }
}

void Detective::drawNeonGlow(M5Canvas& canvas) {
    // Neon city glow near the bottom
    uint8_t glow = 20 + (sinf(millis() / 2000.0f) * 0.5f + 0.5f) * 15;
    uint16_t neonColor = thunderFlash ? TFT_WHITE : canvas.color565(glow/2, 0, glow);
    canvas.drawFastHLine(0, 110, 240, neonColor);
    canvas.drawFastHLine(0, 111, 240, canvas.color565(glow/4, 0, glow/4));
}
