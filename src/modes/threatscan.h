#pragma once
#include <Arduino.h>
#include <M5Unified.h>
#include "../core/threat.h"

// ThreatScan Mode — real-time surveillance radar
// Shows all detected threats with distance, direction, classification
// The detective's primary operating screen

class ThreatScanMode {
public:
    static void init();
    static void start();
    static void stop();
    static void update();
    static void draw(M5Canvas& canvas);
    static bool isRunning() { return running; }

    // Filter controls
    static void cycleFilter();          // ALL / HIGH_ONLY / SURVEILLANCE_ONLY
    static void selectNextThreat();
    static void selectPrevThreat();

    // Detail view for selected threat
    static void toggleDetailView();

private:
    static bool running;
    static uint8_t filter;              // 0=ALL, 1=HIGH+, 2=SURVEILLANCE
    static int8_t selectedThreat;
    static bool detailView;
    static uint32_t lastUpdateMs;

    static void drawRadar(M5Canvas& canvas);
    static void drawThreatList(M5Canvas& canvas);
    static void drawDetailPanel(M5Canvas& canvas, const ThreatRecord& threat);
    static void drawCompass(M5Canvas& canvas, float heading);
    static void drawStats(M5Canvas& canvas);

    // Radar rendering
    static void drawRadarSweep(M5Canvas& canvas, int cx, int cy, int radius);
    static void drawThreatBlip(M5Canvas& canvas, int cx, int cy, int radius,
                                const ThreatRecord& threat);

    static uint16_t threatColor(ThreatLevel level);
    static uint16_t threatClassColor(ThreatClass tc);
    static const char* threatClassName(ThreatClass tc);
    static const char* threatLevelName(ThreatLevel level);
};
