#pragma once
#include <M5Unified.h>

// The Detective — replaces the pig with a hardboiled noir protagonist
// Fedora, trench coat, cigarette glow tied to threat level
// Venetian blind shadows, neon city glow, rain on window

enum class DetectiveState {
    STAKEOUT,        // Passive scanning — cigarette lit, watching
    INTERROGATION,   // Active deauth — leaning forward
    CASE_FILE,       // Viewing data — reading case notes
    HOT_PURSUIT,     // Threat detected — hat brim down, moving
    COLD_CASE,       // Idle — staring out the window
    GHOSTED          // Ghost protocol active — completely dark
};

class Detective {
public:
    static void init();
    static void setState(DetectiveState state);
    static DetectiveState getState() { return currentState; }

    // Core visual
    static void draw(M5Canvas& canvas);
    static void drawBackground(M5Canvas& canvas);  // Cityscape / window
    static void drawCharacter(M5Canvas& canvas);    // The detective

    // Mood / threat indicators
    static void setThreatLevel(uint8_t level);      // 0-255, affects hat angle
    static void setCigaretteGlow(uint8_t brightness);
    static void setRainIntensity(uint8_t intensity); // 0-255

    // Animation
    static void update();

    // Position for bubble overlay
    static int getCharacterX() { return 30; }
    static int getCharacterY() { return 35; }
    static bool isOnRightSide() { return false; }

    // Screen effects
    static bool shouldInvert() { return thunderFlash; }
    static void setThunderFlash(bool active);

    // Neon city background control
    static void setNeonColor(uint16_t color);

private:
    static DetectiveState currentState;
    static uint8_t threatLevel;       // 0-255
    static uint8_t cigaretteGlow;     // 0-255
    static uint8_t rainIntensity;     // 0-255
    static bool thunderFlash;
    static uint16_t neonColor;

    // Animation state
    static uint32_t lastAnimUpdate;
    static uint8_t animFrame;
    static uint8_t coatSway;
    static uint8_t hatAngle;          // 0=up, 255=down (hiding eyes)

    // Rain
    struct Raindrop {
        int16_t x, y;
        int8_t speed;
        uint8_t length;
    };
    static Raindrop raindrops[30];
    static uint8_t rainCount;

    // Drawing helpers
    static void drawFedora(M5Canvas& canvas, int x, int y, uint8_t angle);
    static void drawEyes(M5Canvas& canvas, int x, int y, uint8_t threatLevel);
    static void drawCoat(M5Canvas& canvas, int x, int y, uint8_t sway);
    static void drawCigarette(M5Canvas& canvas, int x, int y, uint8_t glow);
    static void drawNeonCityscape(M5Canvas& canvas);
    static void drawRain(M5Canvas& canvas);
    static void drawVenetianBlinds(M5Canvas& canvas);  // Shadow bars
};
