#include "detective.h"
#include "../ui/display.h"
#include <M5Cardputer.h>
#include <stdlib.h>

// Static members
DetectiveState Detective::currentState = DetectiveState::STAKEOUT;
uint8_t Detective::threatLevel = 0;
uint8_t Detective::cigaretteGlow = 0;
uint8_t Detective::rainIntensity = 80;
bool Detective::thunderFlash = false;
uint16_t Detective::neonColor = TFT_PURPLE;

uint32_t Detective::lastAnimUpdate = 0;
uint8_t Detective::animFrame = 0;
uint8_t Detective::coatSway = 0;
uint8_t Detective::hatAngle = 0;

Detective::Raindrop Detective::raindrops[30] = {};
uint8_t Detective::rainCount = 20;

void Detective::init() {
    currentState = DetectiveState::STAKEOUT;
    threatLevel = 0;
    cigaretteGlow = 80;
    rainIntensity = 80;
    hatAngle = 40;  // Slightly tilted (cool)
    coatSway = 0;
    neonColor = TFT_PURPLE;

    // Initialize rain
    for (int i = 0; i < 30; i++) {
        raindrops[i].x = random(-10, 250);
        raindrops[i].y = random(-50, 140);
        raindrops[i].speed = random(2, 6);
        raindrops[i].length = random(4, 10);
    }

    Serial.println("[DETECTIVE] The detective is on the case.");
}

void Detective::setState(DetectiveState state) {
    currentState = state;
    switch (state) {
        case DetectiveState::STAKEOUT:
            hatAngle = 40;     // Cool tilt, watching
            cigaretteGlow = 80;
            break;
        case DetectiveState::INTERROGATION:
            hatAngle = 20;     // Raised brim, intense
            cigaretteGlow = 150;
            break;
        case DetectiveState::HOT_PURSUIT:
            hatAngle = 180;    // Brim down, serious
            cigaretteGlow = 200;
            break;
        case DetectiveState::COLD_CASE:
            hatAngle = 120;    // Head down, staring
            cigaretteGlow = 30;
            break;
        case DetectiveState::GHOSTED:
            hatAngle = 255;    // Completely hidden
            cigaretteGlow = 0;
            break;
    }
}

void Detective::setThreatLevel(uint8_t level) {
    threatLevel = level;
    if (level > 200) setState(DetectiveState::HOT_PURSUIT);
    else if (level > 100) setState(DetectiveState::INTERROGATION);
    else if (level > 0) setState(DetectiveState::STAKEOUT);
}

void Detective::setCigaretteGlow(uint8_t brightness) {
    cigaretteGlow = brightness;
}

void Detective::setRainIntensity(uint8_t intensity) {
    rainIntensity = intensity;
    rainCount = map(intensity, 0, 255, 0, 25);
}

void Detective::setNeonColor(uint16_t color) {
    neonColor = color;
}

void Detective::setThunderFlash(bool active) {
    thunderFlash = active;
}

// === UPDATE (called every frame) ===
void Detective::update() {
    uint32_t now = millis();

    // Animate coat sway
    if (now - lastAnimUpdate > 100) {
        coatSway = (coatSway + 1) % 4;
        lastAnimUpdate = now;
    }

    // Animate rain
    for (int i = 0; i < rainCount; i++) {
        raindrops[i].y += raindrops[i].speed;
        raindrops[i].x -= 1;  // Wind from right
        if (raindrops[i].y > 140) {
            raindrops[i].y = -raindrops[i].length;
            raindrops[i].x = random(0, 250);
        }
    }

    // Cigarette glow flicker
    if (random(0, 10) == 0) {
        cigaretteGlow += random(-15, 15);
        if (cigaretteGlow > 200) cigaretteGlow = 200;
        if (cigaretteGlow < 10) cigaretteGlow = 10;
    }

    // Thunder flash effect
    if (random(0, 1000) == 0) {
        thunderFlash = true;
    }
    if (thunderFlash) {
        if (now % 300 < 100) thunderFlash = false;
    }

    // Hat angle adjusts based on threat
    if (currentState != DetectiveState::GHOSTED) {
        uint8_t targetAngle = 40 + (threatLevel * 140 / 255);
        if (targetAngle > 180) targetAngle = 180;
        hatAngle = (hatAngle * 3 + targetAngle) / 4;  // Smooth
    }
}

// === DRAW (main method) ===
void Detective::draw(M5Canvas& canvas) {
    if (currentState == DetectiveState::GHOSTED) {
        canvas.fillSprite(0x0000);  // Pure black
        // Dim red text only
        canvas.setTextColor(0x6000);
        canvas.setTextSize(1);
        canvas.drawCentreString("GHOST PROTOCOL", 120, 60, 1);
        return;
    }

    // Background layer
    drawBackground(canvas);

    // Venetian blinds overlay
    drawVenetianBlinds(canvas);

    // Draw the detective character
    drawCharacter(canvas);

    // Rain overlay (on top of everything)
    drawRain(canvas);
}

void Detective::drawBackground(M5Canvas& canvas) {
    // Night sky gradient
    for (int y = 0; y < 80; y++) {
        uint8_t brightness = map(y, 0, 80, 5, 15);
        uint16_t color = canvas.color565(brightness, brightness, brightness * 2);
        canvas.drawFastHLine(0, y, 240, color);
    }

    // Neon city skyline on horizon
    drawNeonCityscape(canvas);

    // Window frame
    canvas.drawRect(2, 2, 236, 131, 0x3186);  // Dim window frame
}

void Detective::drawNeonCityscape(M5Canvas& canvas) {
    // Silhouette buildings
    const uint8_t buildings[] = {0, 15, 30, 45, 55, 70, 85, 95, 110, 125, 140, 155, 165, 180, 195, 210, 225, 240};
    const uint8_t heights[] = {20, 35, 15, 40, 25, 50, 18, 30, 55, 22, 45, 15, 35, 60, 20, 40, 25, 30};

    for (int i = 0; i < 18; i++) {
        int bw = 15;
        int bh = heights[i];
        int bx = buildings[i];
        int by = 80 - bh;

        canvas.fillRect(bx, by, bw, bh, 0x1082);  // Dark silhouette

        // Neon windows (some lit)
        for (int wy = by + 3; wy < by + bh - 3; wy += 6) {
            for (int wx = bx + 2; wx < bx + bw - 2; wx += 5) {
                if (random(0, 100) < 30 && !thunderFlash) {
                    uint8_t winBright = 100 + random(0, 50);
                    canvas.drawPixel(wx, wy, canvas.color565(winBright, winBright * 0.8, winBright * 0.3));
                }
            }
        }
    }

    // Neon sign glow
    uint8_t glow = 50 + (sinf(millis() / 2000.0f) * 0.5f + 0.5f) * 30;
    uint16_t signColor = neonColor;
    canvas.fillCircle(180, 55, 3 + (thunderFlash ? 0 : glow / 30), signColor);
    canvas.fillRect(170, 55, 20, 2, signColor);
    canvas.fillRect(178, 50, 2, 10, signColor);

    // Text on building
    canvas.setTextSize(1);
    canvas.setTextColor(0x2108);  // Very dim
    canvas.drawString("H", 72, 35, 1);
    canvas.drawString("O", 77, 35, 1);
    canvas.drawString("T", 82, 35, 1);
    canvas.drawString("E", 87, 35, 1);
    canvas.drawString("L", 92, 35, 1);
}

void Detective::drawCharacter(M5Canvas& canvas) {
    // The detective stands at left side, facing right
    int cx = 35;  // Character center X
    int cy = 65;  // Character center Y

    // Coat body (dark silhouette)
    drawCoat(canvas, cx, cy, coatSway);

    // Fedora hat
    drawFedora(canvas, cx, cy - 20, hatAngle);

    // Eyes
    drawEyes(canvas, cx, cy - 12, threatLevel);

    // Cigarette
    drawCigarette(canvas, cx + 8, cy - 4, cigaretteGlow);
}

void Detective::drawFedora(M5Canvas& canvas, int x, int y, uint8_t angle) {
    // Fedora hat with brim tilt based on angle
    // Higher angle = brim tilted down (hiding eyes)

    uint16_t hatColor = 0x39E7;  // Dark gray

    // Brim (wide)
    int tilt = map(angle, 0, 255, -2, 4);
    canvas.drawLine(x - 14, y + tilt, x + 12, y - tilt, hatColor);
    canvas.drawLine(x - 15, y + 1 + tilt, x + 13, y + 1 - tilt, hatColor);

    // Crown
    canvas.fillRect(x - 6, y - 8 + tilt, 12, 8 + tilt/2, hatColor);

    // Hat band
    canvas.fillRect(x - 7, y - 3 + tilt, 14, 2, 0x0000);  // Black band

    // Fedora pinch (top indent)
    canvas.drawLine(x, y - 8 + tilt, x, y - 5 + tilt, 0x0000);
}

void Detective::drawEyes(M5Canvas& canvas, int x, int y, uint8_t threatLevel) {
    // Eyes that glow when threat is high, hidden when hat is down

    if (hatAngle > 100) {
        // Eyes hidden by hat shadow — just a faint glow line
        canvas.drawFastHLine(x - 4, y, 8, 0x2108);
        return;
    }

    if (threatLevel > 150) {
        // High threat: eyes glow red
        canvas.setTextColor(TFT_RED);
        canvas.drawChar('.', x - 3, y, 1);
        canvas.drawChar('.', x + 3, y, 1);
        // Glow effect
        canvas.drawPixel(x - 3, y - 1, TFT_RED);
        canvas.drawPixel(x + 3, y - 1, TFT_RED);
    } else {
        // Normal: steady gaze
        canvas.setTextColor(TFT_WHITE);
        canvas.drawChar('.', x - 3, y, 1);
        canvas.drawChar('.', x + 3, y, 1);
    }
}

void Detective::drawCoat(M5Canvas& canvas, int x, int y, uint8_t sway) {
    uint16_t coatColor = 0x2108;  // Dark trench coat

    // Collar (up, noir style)
    canvas.drawLine(x - 8, y - 10, x - 12, y - 3, coatColor);
    canvas.drawLine(x + 2, y - 10, x + 6, y - 3, coatColor);

    // Shoulders
    canvas.drawLine(x - 12, y - 3, x - 16, y + 5, coatColor);
    canvas.drawLine(x + 6, y - 3, x + 10, y + 5, coatColor);

    // Body
    canvas.fillRect(x - 8, y - 3, 16, 20, coatColor);

    // Coat sway (subtle movement from walking/breathing)
    int swayOff = (sway % 4) - 2;
    canvas.drawLine(x - 8, y + 17, x - 10 + swayOff, y + 22, coatColor);
    canvas.drawLine(x + 8, y + 17, x + 10 + swayOff, y + 22, coatColor);

    // Belt (trench coat belt line)
    canvas.fillRect(x - 8, y + 8, 16, 1, 0x0841);
    canvas.fillRect(x - 1, y + 7, 2, 3, 0x39E7);  // Buckle
}

void Detective::drawCigarette(M5Canvas& canvas, int x, int y, uint8_t glow) {
    if (glow < 5) return;

    // The cigarette itself
    canvas.drawFastHLine(x, y, 8, 0xFFFF);  // White paper
    canvas.drawPixel(x + 8, y, 0x2108);     // Ash tip

    // Red glow at the tip
    uint8_t r = map(glow, 0, 255, 0, 255);
    uint8_t g = map(glow, 0, 255, 0, 80);
    uint16_t glowColor = canvas.color565(r, g, 0);
    canvas.drawPixel(x, y, glowColor);
    canvas.drawPixel(x - 1, y, glowColor);
    canvas.drawPixel(x, y - 1, glowColor);

    // Smoke wisps (animated)
    uint8_t wispOffset = (millis() / 100) % 6;
    for (int i = 0; i < 3; i++) {
        int wx = x - 2 - wispOffset * 0.5 + i * 4;
        int wy = y - 4 - i * 4;
        if (wx >= 0 && wx < 240 && wy >= 0) {
            uint8_t smokeAlpha = 100 - i * 30;
            canvas.drawPixel(wx, wy, canvas.color565(smokeAlpha, smokeAlpha, smokeAlpha + 50));
            canvas.drawPixel(wx - 1, wy - 1, canvas.color565(smokeAlpha/2, smokeAlpha/2, smokeAlpha/2));
        }
    }
}

void Detective::drawRain(M5Canvas& canvas) {
    for (int i = 0; i < rainCount; i++) {
        uint8_t alpha = map(raindrops[i].speed, 2, 6, 80, 180);
        uint16_t rainColor = canvas.color565(alpha, alpha, alpha + 50);
        canvas.drawFastVLine(raindrops[i].x, raindrops[i].y, raindrops[i].length, rainColor);
    }
}

void Detective::drawVenetianBlinds(M5Canvas& canvas) {
    // Horizontal shadow bars across the screen
    // Creates the classic noir interrogration room look
    uint8_t intensity = 20;
    for (int y = 0; y < 135; y += 12) {
        canvas.fillRect(0, y, 240, 4, canvas.color565(0, 0, intensity));
        // Blinds angle based on threat — more slanted = more danger
        if (threatLevel > 100) {
            int offset = (threatLevel / 10) % 6 - 3;
            canvas.fillRect(0, y + 1, 240, 1, canvas.color565(offset * 5, offset * 5, offset * 5));
        }
    }
}
