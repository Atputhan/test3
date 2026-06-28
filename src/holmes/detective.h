#pragma once
#include <M5Unified.h>

// The Detective — pure ASCII art, same rendering as the pig.
// Fast, clean, 60fps. Fedora + coat + cigarette + city backdrop.

enum class DetectiveState {
    STAKEOUT = 0,
    INTERROGATION,
    HOT_PURSUIT,
    COLD_CASE,
    GHOSTED
};

class Detective {
public:
    static void init();
    static void update();
    static void draw(M5Canvas& canvas);
    static void setState(DetectiveState state);
    static DetectiveState getState() { return currentState; }

    // Mood/threat indicators
    static void setThreatLevel(uint8_t level); // 0-255
    static void setCigaretteGlow(uint8_t brightness); // 0-255
    static void setRainIntensity(uint8_t intensity); // 0-255

    // Position (for bubble placement, matches piglet interface)
    static int getCharacterX() { return 20; }
    static int getCharacterY() { return 40; }
    static bool isOnRightSide() { return false; }

    // Screen effects
    static void setThunderFlash(bool active);
    static bool isThunderFlashing() { return thunderFlash; }

private:
    static DetectiveState currentState;
    static uint8_t threatLevel;
    static uint8_t cigaretteGlow;
    static uint8_t rainIntensity;
    static bool thunderFlash;

    // Animation
    static uint32_t lastAnimUpdate;
    static uint8_t animFrame;
    static bool facingRight;
    static uint8_t hatAngle; // 0=up, 255=down

    // ASCII frame definitions (mirroring piglet system)
    static void drawCharacter(M5Canvas& canvas, const char** frame, uint8_t lines);

    // Facing-right frames
    static const char* FRAME_NEUTRAL_R[5];
    static const char* FRAME_HAPPY_R[5];
    static const char* FRAME_HUNTING_R[5];
    static const char* FRAME_SAD_R[5];
    static const char* FRAME_ANGRY_R[5];

    // Background elements
    static void drawVenetianBlinds(M5Canvas& canvas);
    static void drawRain(M5Canvas& canvas);
    static void drawNeonGlow(M5Canvas& canvas);
};
