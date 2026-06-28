#pragma once
#include <Arduino.h>
#include <M5Unified.h>
#include <SD.h>
#include <vector>

// High-fidelity WAV player for the detective's office
// Plays 8/16-bit mono WAV files from SD card via PWM speaker
// Optional I2S DAC support if external MAX98357 detected on EXT port

class WAVPlayer {
public:
    static void init();
    static void update();

    // Playback control
    static bool playFile(const char* path);
    static void stop();
    static void pause();
    static void resume();
    static bool isPlaying() { return playing; }
    static bool isPaused() { return paused; }

    // Track list
    static uint8_t scanDirectory(const char* dir);
    static uint8_t getTrackCount() { return trackCount; }
    static const char* getTrackName(uint8_t idx);
    static bool playTrack(uint8_t idx);
    static void nextTrack();
    static void prevTrack();

    // Volume
    static void setVolume(uint8_t vol) { volume = constrain(vol, 0, 100); }
    static uint8_t getVolume() { return volume; }

    // Track info
    static int8_t getCurrentTrackIndex() { return currentTrack; }
    
    // Status
    static uint32_t getPosition();
    static uint32_t getDuration();
    static uint8_t getProgressPercent();

private:
    static bool playing;
    static bool paused;
    static uint8_t volume;

    // Track system
    static constexpr uint8_t MAX_TRACKS = 50;
    static char trackNames[MAX_TRACKS][64];
    static char trackPaths[MAX_TRACKS][128];
    static uint8_t trackCount;
    static int8_t currentTrack;

    // WAV playback state
    static File wavFile;
    static uint32_t dataStart;      // Byte offset where PCM data begins
    static uint32_t dataSize;       // Bytes of PCM data
    static uint32_t bytesPlayed;
    static uint16_t sampleRate;
    static uint8_t bitsPerSample;
    static uint8_t numChannels;
    static uint32_t lastSampleTime;
    static uint8_t sampleBuffer[256];
    static uint16_t bufferPos;
    static uint16_t bufferLen;

    // Internal
    static bool parseWAVHeader();
    static void fillBuffer();
    static void outputSample();
    static void scanDir(const char* dir);
};

// Audio player mode (the "Time to get high and fly and slow down..." mode)
class AudioPlayerMode {
public:
    static void init();
    static void start();
    static void stop();
    static void update();
    static void draw(M5Canvas& canvas);
    static bool isRunning() { return running; }

private:
    static bool running;
    static uint8_t menuState;    // 0=browse, 1=playing, 2=equalizer
    static uint32_t lastDrawMs;

    static void handleInput();
    static void drawBrowser(M5Canvas& canvas);
    static void drawNowPlaying(M5Canvas& canvas);
    static void drawEqualizer(M5Canvas& canvas);
};
