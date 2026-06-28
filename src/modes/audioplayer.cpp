#include "audioplayer.h"
#include "../core/config.h"
#include "../ui/display.h"
#include "../audio/sfx.h"
#include "../piglet/mood.h"
#include "../piglet/avatar.h"
#include <M5Cardputer.h>

// ========================
// WAV PLAYER IMPLEMENTATION
// ========================

bool WAVPlayer::playing = false;
bool WAVPlayer::paused = false;
uint8_t WAVPlayer::volume = 80;
char WAVPlayer::trackNames[MAX_TRACKS][64] = {};
char WAVPlayer::trackPaths[MAX_TRACKS][128] = {};
uint8_t WAVPlayer::trackCount = 0;
int8_t WAVPlayer::currentTrack = -1;
File WAVPlayer::wavFile;
uint32_t WAVPlayer::dataStart = 0;
uint32_t WAVPlayer::dataSize = 0;
uint32_t WAVPlayer::bytesPlayed = 0;
uint16_t WAVPlayer::sampleRate = 0;
uint8_t WAVPlayer::bitsPerSample = 0;
uint8_t WAVPlayer::numChannels = 0;
uint32_t WAVPlayer::lastSampleTime = 0;
uint8_t WAVPlayer::sampleBuffer[256] = {0};
uint16_t WAVPlayer::bufferPos = 0;
uint16_t WAVPlayer::bufferLen = 0;

void WAVPlayer::init() {
    // Scan default music directory
    scanDir("/music");
    scanDir("/m5ap_elim/music");
    scanDir("/");
    Serial.printf("[WAV] Found %u tracks\n", trackCount);
}

void WAVPlayer::scanDir(const char* dir) {
    if (!SD.exists(dir)) return;
    File root = SD.open(dir);
    if (!root) return;

    File entry;
    while ((entry = root.openNextFile()) && trackCount < MAX_TRACKS) {
        if (!entry.isDirectory()) {
            const char* name = entry.name();
            int len = strlen(name);
            // Match .wav files (case insensitive)
            if ((len >= 4 && (strcasecmp(name + len - 4, ".wav") == 0))) {
                strncpy(trackNames[trackCount], name, 63);
                trackNames[trackCount][63] = 0;
                // Strip extension for display
                char* dot = strrchr(trackNames[trackCount], '.');
                if (dot) *dot = 0;

                // Build full path
                snprintf(trackPaths[trackCount], 127, "%s/%s", dir, name);
                trackCount++;
            }
        }
        entry.close();
    }
    root.close();
}

const char* WAVPlayer::getTrackName(uint8_t idx) {
    if (idx >= trackCount) return nullptr;
    return trackNames[idx];
}

bool WAVPlayer::playFile(const char* path) {
    if (!SD.exists(path)) return false;

    if (wavFile) wavFile.close();
    wavFile = SD.open(path, FILE_READ);
    if (!wavFile) return false;

    if (!parseWAVHeader()) {
        wavFile.close();
        return false;
    }

    // Seek to data start
    wavFile.seek(dataStart);
    playing = true;
    paused = false;
    bytesPlayed = 0;
    bufferPos = 0;
    bufferLen = 0;
    lastSampleTime = 0;

    // DIM screen slightly during playback for ambiance
    M5.Display.setBrightness(30);

    Serial.printf("[WAV] Playing: %s (%u Hz, %u-bit, %u ch)\n",
                  path, sampleRate, bitsPerSample, numChannels);
    return true;
}

bool WAVPlayer::parseWAVHeader() {
    if (!wavFile || wavFile.size() < 44) return false;

    uint8_t header[44];
    if (wavFile.read(header, 44) != 44) return false;

    // Check RIFF header
    if (header[0] != 'R' || header[1] != 'I' || header[2] != 'F' || header[3] != 'F') return false;
    if (header[8] != 'W' || header[9] != 'A' || header[10] != 'V' || header[11] != 'E') return false;

    // Find fmt chunk
    uint32_t pos = 12;
    while (pos < wavFile.size() - 8) {
        wavFile.seek(pos);
        uint8_t chunk[8];
        if (wavFile.read(chunk, 8) != 8) return false;

        uint32_t chunkSize = chunk[4] | (chunk[5] << 8) | (chunk[6] << 16) | (chunk[7] << 24);

        if (chunk[0] == 'f' && chunk[1] == 'm' && chunk[2] == 't' && chunk[3] == ' ') {
            // Audio format (should be 1 = PCM)
            uint8_t fmt[16];
            wavFile.read(fmt, 16);
            // fmt[0] = format (1=PCM), fmt[2] = channels, fmt[4] = sample rate
            numChannels = fmt[2];
            sampleRate = fmt[4] | (fmt[5] << 8);
            bitsPerSample = fmt[14];
            break;
        }
        pos += 8 + chunkSize;
    }

    // Find data chunk
    pos = 12;
    while (pos < wavFile.size() - 8) {
        wavFile.seek(pos);
        uint8_t chunk[8];
        if (wavFile.read(chunk, 8) != 8) return false;

        uint32_t chunkSize = chunk[4] | (chunk[5] << 8) | (chunk[6] << 16) | (chunk[7] << 24);

        if (chunk[0] == 'd' && chunk[1] == 'a' && chunk[2] == 't' && chunk[3] == 'a') {
            dataStart = pos + 8;
            dataSize = chunkSize;
            return sampleRate > 0 && dataSize > 0;
        }
        pos += 8 + chunkSize;
    }

    return false;
}

void WAVPlayer::fillBuffer() {
    if (!wavFile || paused) return;

    uint32_t bytesToRead = 256;
    uint32_t remaining = dataSize - bytesPlayed;
    if (bytesToRead > remaining) bytesToRead = remaining;

    int read = wavFile.read(sampleBuffer, bytesToRead);
    if (read > 0) {
        bufferLen = read;
        bufferPos = 0;
    } else {
        // End of file or error
        playing = false;
        bufferLen = 0;
        // Auto next track
        if (trackCount > 0) {
            nextTrack();
        }
    }
}

void WAVPlayer::outputSample() {
    if (!playing || paused) return;

    // Calculate delay between samples
    uint32_t interval = 1000000 / sampleRate;  // microseconds per sample
    if (micros() - lastSampleTime < interval) return;

    if (bufferPos >= bufferLen) {
        fillBuffer();
        if (!playing) return;
    }

    // Read sample and output to PWM speaker
    uint8_t sample = sampleBuffer[bufferPos++];

    // Apply volume
    // For 8-bit samples: 128 = silence, 0 = min, 255 = max
    // For 16-bit samples: would need to handle pairs
    if (bitsPerSample == 8) {
        // Convert 8-bit unsigned to PWM duty cycle
        uint8_t output = map(sample, 0, 255, 0, volume);
        M5.Speaker.tone(440 + (sample - 128) * 2, interval / 1000);
    } else if (bitsPerSample == 16) {
        // 16-bit: need 2 bytes per sample (little-endian)
        if (bufferPos + 1 <= bufferLen) {
            int16_t sample16 = sampleBuffer[bufferPos] | (sampleBuffer[bufferPos+1] << 8);
            bufferPos += 2;
            // Map to tone frequency
            uint16_t freq = map(sample16, -32768, 32767, 100, 800);
            M5.Speaker.tone(freq, interval / 1000);
        }
    }

    bytesPlayed += (bitsPerSample == 8 ? 1 : 2);  // Simplified
    lastSampleTime = micros();
}

void WAVPlayer::update() {
    if (playing && !paused) {
        outputSample();

        // Check for end of track
        if (bytesPlayed >= dataSize) {
            playing = false;
            if (trackCount > 0) {
                nextTrack();
            }
        }
    }
}

void WAVPlayer::stop() {
    playing = false;
    paused = false;
    if (wavFile) wavFile.close();
    M5.Display.setBrightness(Config::personality().brightness * 255 / 100);
}

void WAVPlayer::pause() {
    if (playing) paused = !paused;
}

void WAVPlayer::resume() { paused = false; }

bool WAVPlayer::playTrack(uint8_t idx) {
    if (idx >= trackCount) return false;
    currentTrack = idx;
    return playFile(trackPaths[idx]);
}

void WAVPlayer::nextTrack() {
    if (trackCount == 0) return;
    // Stop current
    if (wavFile) wavFile.close();
    playing = false;
    // Next track
    int8_t next = (currentTrack + 1) % trackCount;
    playTrack(next);
}

void WAVPlayer::prevTrack() {
    if (trackCount == 0) return;
    if (wavFile) wavFile.close();
    playing = false;
    int8_t prev = (currentTrack - 1 + trackCount) % trackCount;
    playTrack(prev);
}

uint32_t WAVPlayer::getPosition() {
    if (sampleRate == 0) return 0;
    return (bytesPlayed * 1000) / (sampleRate * numChannels * (bitsPerSample / 8));
}

uint32_t WAVPlayer::getDuration() {
    if (sampleRate == 0) return 0;
    return (dataSize * 1000) / (sampleRate * numChannels * (bitsPerSample / 8));
}

uint8_t WAVPlayer::getProgressPercent() {
    if (dataSize == 0) return 0;
    return (bytesPlayed * 100) / dataSize;
}

// ========================
// AUDIO PLAYER MODE
// ========================

bool AudioPlayerMode::running = false;
uint8_t AudioPlayerMode::menuState = 0;
uint32_t AudioPlayerMode::lastDrawMs = 0;

void AudioPlayerMode::init() {
    WAVPlayer::init();
}

void AudioPlayerMode::start() {
    running = true;
    menuState = 0;  // Browser view
    Mood::setStatusMessage("time to get high and fly and slow down...");
    Display::setTopBarMessage("NOW SPINNIN'", 5000);
}

void AudioPlayerMode::stop() {
    running = false;
    WAVPlayer::stop();
    Display::clearTopBarMessage();
}

void AudioPlayerMode::update() {
    if (!running) return;
    handleInput();
    WAVPlayer::update();
}

void AudioPlayerMode::handleInput() {
    M5Cardputer.update();
    if (!M5Cardputer.Keyboard.isChange()) return;

    auto keys = M5Cardputer.Keyboard.keysState();
    for (auto c : keys.word) {
        switch (c) {
            case ';':           // Up — prev track / scroll up
                if (menuState == 0) WAVPlayer::prevTrack();
                break;
            case '.':           // Down — next track / scroll down
                if (menuState == 0) WAVPlayer::nextTrack();
                break;
            case KEY_ENTER:     // Play / pause
                if (WAVPlayer::isPlaying()) {
                    if (WAVPlayer::isPaused()) WAVPlayer::resume();
                    else WAVPlayer::pause();
                } else if (WAVPlayer::getTrackCount() > 0) {
                    WAVPlayer::playTrack(0);
                }
                menuState = 1;
                break;
            case 's': case 'S': // Stop
                WAVPlayer::stop();
                break;
            case 'b': case 'B': // Browse mode
                menuState = 0;
                break;
            case '+':           // Volume up
                WAVPlayer::setVolume(WAVPlayer::getVolume() + 5);
                break;
            case '-':           // Volume down
                WAVPlayer::setVolume(WAVPlayer::getVolume() - 5);
                break;
        }
    }
}

void AudioPlayerMode::draw(M5Canvas& canvas) {
    canvas.fillSprite(COLOR_BG);

    if (menuState == 0) drawBrowser(canvas);
    else drawNowPlaying(canvas);

    // ESC / back hint
    canvas.setTextColor(COLOR_FG);
    canvas.setTextSize(1);
    canvas.drawString("ESC:EXIT  B:BROWSE  S:STOP", 5, 120, 1);
}

void AudioPlayerMode::drawBrowser(M5Canvas& canvas) {
    canvas.setTextSize(1);
    canvas.setTextColor(TFT_GREEN);
    canvas.drawString("=== TIME TO GET HIGH AND FLY ===", 5, 5, 1);
    canvas.drawString("=== AND SLOW DOWN... ===", 5, 15, 1);
    canvas.drawLine(0, 25, 240, 25, TFT_DARKGREY);

    canvas.setTextColor(TFT_WHITE);
    canvas.drawString("TRACKS:", 5, 28, 1);
    char countStr[16];
    snprintf(countStr, sizeof(countStr), "%u FILES", WAVPlayer::getTrackCount());
    canvas.drawRightString(countStr, 235, 28, 1);

    uint8_t maxDisplay = 6;
    uint8_t start = 0;
    for (uint8_t i = start; i < WAVPlayer::getTrackCount() && i < start + maxDisplay; i++) {
        int y = 40 + (i - start) * 12;
        const char* name = WAVPlayer::getTrackName(i);
        if ((int8_t)i == WAVPlayer::getCurrentTrackIndex() && WAVPlayer::isPlaying()) {
            canvas.setTextColor(TFT_GREEN);
            canvas.drawChar('>', 5, y, 1);
            canvas.drawString(name ? name : "???", 15, y, 1);
            // Progress bar
            char prog[48];
            snprintf(prog, sizeof(prog), "%u%%", WAVPlayer::getProgressPercent());
            canvas.drawRightString(prog, 235, y, 1);
        } else {
            canvas.setTextColor(TFT_WHITE);
            canvas.drawString(name ? name : "???", 10, y, 1);
        }
    }
}

void AudioPlayerMode::drawNowPlaying(M5Canvas& canvas) {
    // Now playing view — cinematic
    canvas.fillSprite(0x0000);

    // Top border
    canvas.drawLine(0, 35, 240, 35, 0x6300);
    canvas.drawLine(0, 100, 240, 100, 0x6300);

    // Song info
    canvas.setTextSize(2);
    canvas.setTextColor(TFT_GREEN);
    const char* name = WAVPlayer::getTrackName(WAVPlayer::getCurrentTrackIndex() >= 0 ? WAVPlayer::getCurrentTrackIndex() : 0);
    canvas.drawCentreString(name ? name : "NO TRACK", 120, 10, 2);

    // Visual equalizer (sine wave display of audio state)
    canvas.setTextSize(1);
    for (int x = 0; x < 240; x += 4) {
        float val = sinf((millis() / 1000.0f) * TWO_PI + x * 0.1f) * 10;
        int16_t h = 30 + (int16_t)val;
        if (WAVPlayer::isPlaying()) {
            canvas.drawPixel(x, h, TFT_GREEN);
            canvas.drawPixel(x, h + 1, TFT_GREEN);
            canvas.drawPixel(x + 1, h, TFT_GREEN);
        }
    }

    // Progress bar
    uint8_t prog = WAVPlayer::getProgressPercent();
    canvas.fillRect(10, 80, 220, 6, 0x2104);
    if (prog > 0) {
        canvas.fillRect(10, 80, (220 * prog) / 100, 6, TFT_GREEN);
    }

    // Time
    char timeStr[32];
    uint32_t pos = WAVPlayer::getPosition() / 1000;
    uint32_t dur = WAVPlayer::getDuration() / 1000;
    snprintf(timeStr, sizeof(timeStr), "%02u:%02u / %02u:%02u",
             pos / 60, pos % 60, dur / 60, dur % 60);
    canvas.setTextColor(TFT_DARKGREY);
    canvas.drawCentreString(timeStr, 120, 90, 1);

    // Volume
    char volStr[8];
    snprintf(volStr, sizeof(volStr), "VOL:%u", WAVPlayer::getVolume());
    canvas.drawRightString(volStr, 235, 105, 1);

    // Playing status
    if (WAVPlayer::isPaused()) {
        canvas.setTextColor(TFT_YELLOW);
        canvas.drawCentreString("|| PAUSED", 120, 105, 1);
    } else if (WAVPlayer::isPlaying()) {
        canvas.setTextColor(TFT_GREEN);
        // Animated spinner
        const char* spin = "-\\|/";
        uint8_t si = (millis() / 200) % 4;
        char status[4] = {spin[si], 0};
        canvas.drawCentreString(status, 10, 105, 1);
    }

    // Detective's note at bottom
    canvas.setTextSize(1);
    canvas.setTextColor(TFT_DARKGREY);
    canvas.drawCentreString("time to get high and fly and slow down...", 120, 115, 1);
}
