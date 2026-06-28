// Piglet ASCII avatar implementation

#include "avatar.h"
#include "../core/cursed.h"
#include "../holmes/detective.h"
#include "weather.h"
#include "../ui/display.h"
#include <time.h>

// Static members
AvatarState Avatar::currentState = AvatarState::NEUTRAL;
bool Avatar::isBlinking = false;
bool Avatar::earsUp = true;
uint32_t Avatar::lastBlinkTime = 0;
uint32_t Avatar::blinkInterval = 3000;
int Avatar::moodIntensity = 0;  // Phase 8: -100 to 100

// Cute jump state
bool Avatar::jumpActive = false;
uint32_t Avatar::jumpStartTime = 0;

// Walk transition state
bool Avatar::transitioning = false;
uint32_t Avatar::transitionStartTime = 0;
int Avatar::transitionFromX = 2;
int Avatar::transitionToX = 2;
bool Avatar::transitionToFacingRight = true;
int Avatar::currentX = 2;

// Sniff animation state
bool Avatar::isSniffing = false;
static uint32_t sniffStartTime = 0;
static const uint32_t SNIFF_DURATION_MS = 600;  // 600ms for proper sniff cycle
static uint8_t sniffFrame = 0;  // Alternates between nose shapes (oo, oO, Oo)

// Walk transition timing
static const uint32_t TRANSITION_DURATION_MS = 1200;  // 1.2s slow relaxed walk (was 400ms - too hectic)

// Rest cooldown after grass stops - prevents immediate re-triggering
static uint32_t lastGrassStopTime = 0;
static const uint32_t GRASS_REST_COOLDOWN_MS = 3000;  // 3 second chill period after grass stops

// Attack shake state (visual feedback for captures)
static bool attackShakeActive = false;
static bool attackShakeStrong = false;
static uint32_t attackShakeRefreshTime = 0;

// Thunder flash state (weather effect - invert colors)
static bool thunderFlashActive = false;

// Night sky star system state
Avatar::Star Avatar::stars[15] = {{0}};
uint8_t Avatar::starCount = 0;
uint32_t Avatar::lastStarSpawn = 0;
uint32_t Avatar::nextSpawnDelay = 2000;
bool Avatar::starsActive = false;
uint32_t Avatar::lastNightCheck = 0;
bool Avatar::cachedNightMode = false;

// Color helper for thunder flash inversion (matches Sirloin)
static uint16_t getDrawColor() {
    if (thunderFlashActive) {
        return getColorBG();  // Swap: draw with BG color during flash
    }
    return getColorFG();
}

static uint16_t getBGColor() {
    if (thunderFlashActive) {
        return getColorFG();  // Inverted while flashing
    }
    return getColorBG();
}

// Grass animation state
bool Avatar::grassMoving = false;
bool Avatar::grassDirection = true;  // true = grass scrolls right
bool Avatar::pendingGrassStart = false;  // Wait for transition before starting grass
uint32_t Avatar::lastGrassUpdate = 0;
uint16_t Avatar::grassSpeed = 80;  // Default fast for OINK
char Avatar::grassPattern[32] = {0};
// Internal state for looking direction
static bool facingRight = true;  // Default: pig looks right
static uint32_t lastFlipTime = 0;
static uint32_t flipInterval = 5000;

// Look behavior (stationary observation)
static uint32_t lastLookTime = 0;
static uint32_t lookInterval = 2000;  // Look around every 2-5s when stationary
bool Avatar::onRightSide = false;  // Track which side of screen pig is on (class static)

// --- DERPY STYLE with direction ---
// Right-looking frames (snout 00 on right side of face, pig looks RIGHT)
const char* AVATAR_NEUTRAL_R[] = {
    " ?  ? ",
    "(o 00)",
    "(    )"
};

const char* AVATAR_HAPPY_R[] = {
    " ^  ^ ",
    "(^ 00)",
    "(    )"
};

const char* AVATAR_EXCITED_R[] = {
    " !  ! ",
    "(@ 00)",
    "(    )"
};

const char* AVATAR_HUNTING_R[] = {
    " |  | ",
    "(= 00)",
    "(    )"
};

const char* AVATAR_SLEEPY_R[] = {
    " v  v ",
    "(- 00)",
    "(    )"
};

const char* AVATAR_SAD_R[] = {
    " .  . ",
    "(T 00)",
    "(    )"
};

const char* AVATAR_ANGRY_R[] = {
    " \\  / ",
    "(# 00)",
    "(    )"
};

// Left-looking frames (snout 00 on left side of face, pig looks LEFT, z pigtail)
const char* AVATAR_NEUTRAL_L[] = {
    " ?  ? ",
    "(00 o)",
    "(    )z"
};

const char* AVATAR_HAPPY_L[] = {
    " ^  ^ ",
    "(00 ^)",
    "(    )z"
};

const char* AVATAR_EXCITED_L[] = {
    " !  ! ",
    "(00 @)",
    "(    )z"
};

const char* AVATAR_HUNTING_L[] = {
    " |  | ",
    "(00 =)",
    "(    )z"
};

const char* AVATAR_SLEEPY_L[] = {
    " v  v ",
    "(00 -)",
    "(    )z"
};

const char* AVATAR_SAD_L[] = {
    " .  . ",
    "(00 T)",
    "(    )z"
};

const char* AVATAR_ANGRY_L[] = {
    " \\  / ",
    "(00 #)",
    "(    )z"
};

void Avatar::init() {
    currentState = AvatarState::NEUTRAL;
    isBlinking = false;
    isSniffing = false;
    earsUp = true;
    lastBlinkTime = millis();
    blinkInterval = random(4000, 8000);
    
    // Init direction - start at LEFT or RIGHT edge (not center)
    // This ensures bubble can float beside pig from the start
    bool startRight = random(0, 2) == 0;
    onRightSide = startRight;
    currentX = startRight ? 108 : 20;  // Start at proper edge position
    facingRight = !startRight;  // Face toward center (more interesting)
    lastFlipTime = millis();
    flipInterval = random(25000, 50000);  // First walk: 25-50s
    lastLookTime = millis();
    lookInterval = random(3000, 8000);  // First look: 3-8s (let pig settle in)
    
    // Init grass pattern - full screen width at size 2 (~24 chars)
    grassMoving = false;
    grassDirection = true;
    pendingGrassStart = false;
    grassSpeed = 80;
    lastGrassUpdate = millis();
    lastGrassStopTime = 0;  // No cooldown on fresh init
    for (int i = 0; i < 26; i++) {
        // Random grass pattern /\/\\//\/
        grassPattern[i] = (random(0, 2) == 0) ? '/' : '\\';
    }
    grassPattern[26] = '\0';

    // Init star system, dormant until night
    starsActive = false;
    starCount = 0;
    lastStarSpawn = 0;
    nextSpawnDelay = 2000;
    lastNightCheck = 0;
    cachedNightMode = false;
    initStarPositions();
    
    // Remove duplicate grass pattern initialization - already done earlier in init()
}

void Avatar::setState(AvatarState state) {
    currentState = state;
}

void Avatar::setMoodIntensity(int intensity) {
    moodIntensity = constrain(intensity, -100, 100);
}

bool Avatar::isFacingRight() {
    return facingRight;
}

bool Avatar::isOnRightSide() {
    return onRightSide;
}

bool Avatar::isTransitioning() {
    return transitioning;
}

int Avatar::getCurrentX() {
    return currentX;
}

void Avatar::blink() {
    isBlinking = true;
}

void Avatar::wiggleEars() {
    earsUp = !earsUp;
}

void Avatar::sniff() {
    if (!isSniffing) {
        sniffFrame = 0;  // Reset frame on new sniff
    }
    isSniffing = true;
    sniffStartTime = millis();
}

void Avatar::cuteJump() {
    // Trigger a cute celebratory jump - higher and slower than walk bounce
    jumpActive = true;
    jumpStartTime = millis();
}

void Avatar::draw(M5Canvas& canvas) {
    Detective::update();
    Detective::draw(canvas);
}

void Avatar::drawFrame(M5Canvas& canvas, const char** frame, uint8_t lines, bool blink, bool faceRight, bool sniff) {
    // Star system background layer (behind pig)
    updateStars();
    drawStars(canvas);
    fillPigBoundingBox(canvas);

    canvas.setTextDatum(top_left);
    canvas.setTextSize(3);
    canvas.setTextColor(getDrawColor());  // Thunder-aware color
    
    uint32_t now = millis();
    
    // Watchdog: if caller stops refreshing attack shake, auto-disable after 250ms
    if (attackShakeRefreshTime == 0 || (now - attackShakeRefreshTime) > 250) {
        attackShakeActive = false;
        attackShakeStrong = false;
    }
    
    // Handle cute jump timeout
    if (jumpActive && (now - jumpStartTime > JUMP_DURATION_MS)) {
        jumpActive = false;
    }
    
    // Calculate vertical shake/jump offset
    int shakeY = 0;
    if (jumpActive) {
        // Cute jump: smooth arc up and down (sine-like)
        // First half: go up, second half: come down
        uint32_t elapsed = now - jumpStartTime;
        float t = (float)elapsed / (float)JUMP_DURATION_MS;  // 0.0 to 1.0
        // Parabolic arc: peaks at t=0.5
        float arc = 4.0f * t * (1.0f - t);  // 0 → 1 → 0
        shakeY = -(int)(arc * JUMP_HEIGHT);  // Negative = up
    } else if (attackShakeActive) {
        // Combat shake: random ±4px (normal) / ±6px (strong)
        const int amp = attackShakeStrong ? 6 : 4;
        shakeY = (esp_random() % 2 == 0) ? amp : -amp;
    } else if (transitioning || grassMoving) {
        // Heavy walk bounce: 4-phase weighted pattern (heavier landing feel)
        // Phase: down(0) → up-overshoot(-3) → settle-low(-1) → settle-mid(-2)
        // 80ms per phase = 320ms full cycle, slower than Sirloin's snappy bounce
        static const int bouncePattern[4] = {0, -3, -1, -2};
        int phase = (now / 80) % 4;
        shakeY = bouncePattern[phase];
    }
    
    // Use animated currentX position (set during transition or at rest)
    int startX = currentX;
    int startY = 23 + shakeY;  // Apply shake offset (shifted down for XP bar at top)
    int lineHeight = 22;
    
    for (uint8_t i = 0; i < lines; i++) {
        // Handle body line (i=2) for dynamic tail
        if (i == 2) {
            char bodyLine[16];
            bool tailOnLeft = false;  // Track if tail prefix added (needs X offset)
            if (grassMoving || pendingGrassStart) {
                // Treadmill mode: always show tail
                if (faceRight) {
                    strncpy(bodyLine, "z(    )", sizeof(bodyLine));  // Tail on left when facing right
                    tailOnLeft = true;
                } else {
                    strncpy(bodyLine, "(    )z", sizeof(bodyLine));  // Tail on right when facing left
                }
            } else if (transitioning) {
                // During transition: show tail on trailing side
                bool movingRight = (transitionToX > transitionFromX);
                if (movingRight) {
                    strncpy(bodyLine, "z(    )", sizeof(bodyLine));  // Tail trails on left
                    tailOnLeft = true;
                } else {
                    strncpy(bodyLine, "(    )z", sizeof(bodyLine));  // Tail trails on right
                }
            } else {
                // Stationary: always show tail based on facing direction
                if (faceRight) {
                    strncpy(bodyLine, "z(    )", sizeof(bodyLine));  // Facing right, tail on left
                    tailOnLeft = true;
                } else {
                    strncpy(bodyLine, "(    )z", sizeof(bodyLine));  // Facing left, tail on right
                }
            }
            bodyLine[sizeof(bodyLine) - 1] = '\0';
            // When tail is on left (z prefix), offset X back by 1 char width (18px at size 3)
            // to keep body aligned with head
            int bodyX = tailOnLeft ? (startX - 18) : startX;
            canvas.drawString(bodyLine, bodyX, startY + i * lineHeight);
        } else if (i == 1 && (blink || sniff)) {
            // Face line - modify eye and/or nose
            // Face format: "(X 00)" for right-facing, "(00 X)" for left-facing
            char modifiedLine[16];
            strncpy(modifiedLine, frame[i], sizeof(modifiedLine) - 1);
            modifiedLine[sizeof(modifiedLine) - 1] = '\0';
            
            if (blink) {
                // Replace eye character with '-' for blink
                if (faceRight) {
                    modifiedLine[1] = '-';  // Eye position in "(X 00)"
                } else {
                    modifiedLine[4] = '-';  // Eye position in "(00 X)"
                }
            }
            
            if (sniff) {
                // Animated sniff - cycle through nose shapes
                char n1, n2;
                switch (sniffFrame) {
                    case 0: n1 = 'o'; n2 = 'o'; break;  // oo
                    case 1: n1 = 'o'; n2 = 'O'; break;  // oO
                    case 2: n1 = 'O'; n2 = 'o'; break;  // Oo
                    default: n1 = 'o'; n2 = 'o'; break;
                }
                // Nose is at positions 3-4 for right-facing "(X 00)"
                // Nose is at positions 1-2 for left-facing "(00 X)"
                if (faceRight) {
                    modifiedLine[3] = n1;
                    modifiedLine[4] = n2;
                } else {
                    modifiedLine[1] = n1;
                    modifiedLine[2] = n2;
                }
            }
            
            canvas.drawString(modifiedLine, startX, startY + i * lineHeight);
        } else {
            canvas.drawString(frame[i], startX, startY + i * lineHeight);
        }
    }
    
    // Draw grass below piglet
    drawGrass(canvas);
}

void Avatar::setGrassMoving(bool moving, bool directionRight) {
    // Early exit if already in requested state (prevents per-frame overhead)
    if (moving && (grassMoving || pendingGrassStart)) {
        return;  // Already moving or pending - don't interrupt
    }
    if (!moving && !grassMoving && !pendingGrassStart) {
        return;  // Already stopped
    }
    
    if (moving) {
        // COOLDOWN CHECK: Don't start grass if we just stopped
        // This prevents rapid on/off/on/off state changes from causing macarena
        uint32_t now = millis();
        if (lastGrassStopTime > 0 && (now - lastGrassStopTime) < GRASS_REST_COOLDOWN_MS) {
            return;  // Still in cooldown period - pig needs rest
        }
        
        grassDirection = directionRight;
        
        // Calculate correct treadmill position based on direction
        // Grass RIGHT: pig at X=108 (tail margin on right)
        // Grass LEFT: pig at X=20 (tail margin on left: 20-18=2)
        int targetX = directionRight ? 108 : 20;
        
        if (transitioning) {
            // Check if this is a coast-back transition (pig returning to rest at X=20)
            // Don't interrupt coast-back with grass start - let the pig chill first
            // This prevents the "macarena" bug where rapid state changes cause endless back-and-forth
            if (transitionToX == 20) {
                return;  // Coast-back in progress - pig needs a break
            }
            // Already sliding to grass position - queue grass
            pendingGrassStart = true;
            grassMoving = false;
        } else if (currentX != targetX) {
            // Not at correct treadmill position - slide there first
            startWindupSlide(targetX, directionRight);  // face direction of travel
            pendingGrassStart = true;
            grassMoving = false;
        } else {
            // Already at correct position - start grass immediately
            facingRight = !directionRight;
            grassMoving = true;
            pendingGrassStart = false;
        }
        
        // Clear cooldown since we successfully started
        lastGrassStopTime = 0;
    } else {
        // Stop grass and coast back to resting position
        grassMoving = false;
        pendingGrassStart = false;
        
        // Start cooldown timer - pig needs rest before grass can start again
        lastGrassStopTime = millis();
        
        // Reset walk timer to prevent immediate post-coast walk trigger
        lastFlipTime = millis();
        
        // Coast back to left resting position (X=20 for tail margin)
        startWindupSlide(20, false);  // X=20, face left when done
    }
}

void Avatar::setGrassSpeed(uint16_t ms) {
    grassSpeed = ms;
}

void Avatar::setGrassPattern(const char* pattern) {
    strncpy(grassPattern, pattern, 26);
    grassPattern[26] = '\0';
}

void Avatar::resetGrassPattern() {
    // Reset to random grass pattern /\/\\//\/
    for (int i = 0; i < 26; i++) {
        grassPattern[i] = (random(0, 2) == 0) ? '/' : '\\';
    }
    grassPattern[26] = '\0';
}

void Avatar::updateGrass() {
    if (!grassMoving) return;
    
    uint32_t now = millis();
    if (now - lastGrassUpdate < grassSpeed) return;
    lastGrassUpdate = now;
    
    // Shift pattern based on grassDirection (set when grass started)
    // grassDirection=true: grass scrolls RIGHT (pig faces left, walking left through world)
    // grassDirection=false: grass scrolls LEFT (pig faces right, walking right through world)
    if (grassDirection) {
        // Shift right (grass scrolls right)
        char last = grassPattern[25];
        for (int i = 25; i > 0; i--) {
            grassPattern[i] = grassPattern[i - 1];
        }
        grassPattern[0] = last;
    } else {
        // Shift left (grass scrolls left)
        char first = grassPattern[0];
        for (int i = 0; i < 25; i++) {
            grassPattern[i] = grassPattern[i + 1];
        }
        grassPattern[25] = first;
    }
    
    // Occasionally mutate a character for variety
    if (random(0, 30) == 0) {
        int pos = random(0, 26);
        grassPattern[pos] = (random(0, 2) == 0) ? '/' : '\\';
    }
}

void Avatar::drawGrass(M5Canvas& canvas) {
    updateGrass();
    
    canvas.setTextSize(2);  // Same as menu items
    canvas.setTextColor(getDrawColor());  // Thunder-aware color
    canvas.setTextDatum(top_left);
    
    // Draw at bottom of avatar area, full screen width
    int grassY = 91;  // Below the pig face (at edge of main canvas)
    canvas.drawString(grassPattern, 0, grassY);
}

// --- Night sky star system ---
bool Avatar::isNightTime() {
    uint32_t now = millis();

    // Cache rtc reads, check every 60 seconds
    if (now - lastNightCheck < 60000 && lastNightCheck != 0) {
        return cachedNightMode;
    }
    lastNightCheck = now;

    auto dt = M5.Rtc.getDateTime();
    if (dt.date.year >= 2024) {
        uint8_t hour = dt.time.hours;
        cachedNightMode = (hour >= 20 || hour < 6);
        return cachedNightMode;
    }

    time_t unixNow = time(nullptr);
    if (unixNow >= 1700000000) {
        struct tm timeinfo;
        localtime_r(&unixNow, &timeinfo);
        uint8_t hour = (uint8_t)timeinfo.tm_hour;
        cachedNightMode = (hour >= 20 || hour < 6);
        return cachedNightMode;
    }

    cachedNightMode = false;
    return false;
}

bool Avatar::areStarsActive() {
    return starsActive;
}

void Avatar::initStarPositions() {
    // Pre-gen star positions, hide until spawn
    for (uint8_t i = 0; i < MAX_STARS; i++) {
        // y 20-100 sky/backdrop, bubble still wins
        // x 5-235 near full width
        stars[i].x = random(5, 235);
        // Match rain clip: keep stars above grass (rain clips at y < 88)
        stars[i].y = random(20, 88);
        stars[i].size = 1;
        stars[i].brightness = 0;
        stars[i].fadeInStart = 0;
        // About 20 percent twinkle
        stars[i].isBlinking = (random(0, 100) < 20);
    }
}

void Avatar::updateStars() {
    uint32_t now = millis();

    // Never show stars while raining
    if (Weather::isRaining()) {
        if (starsActive) {
            starsActive = false;
            starCount = 0;
        }
        return;
    }

    // Night mode transition
    bool nightNow = isNightTime();

    if (nightNow && !starsActive) {
        // Night starting, spawn sequence online
        starsActive = true;
        starCount = 0;
        lastStarSpawn = now;
        nextSpawnDelay = random(800, 4001);  // 800ms to 4s
        initStarPositions();
    } else if (!nightNow && starsActive) {
        // Day starting, kill stars
        starsActive = false;
        starCount = 0;
    }

    if (!starsActive) return;

    // Spawn new star when timer expires
    if (starCount < MAX_STARS && (now - lastStarSpawn >= nextSpawnDelay)) {
        stars[starCount].fadeInStart = now;
        stars[starCount].brightness = 0;
        starCount++;
        lastStarSpawn = now;
        nextSpawnDelay = random(800, 4001);
    }

    // Update visible stars, fade-in, twinkle handled in draw
    for (uint8_t i = 0; i < starCount; i++) {
        uint32_t age = now - stars[i].fadeInStart;
        if (age < 500) {
            stars[i].brightness = (age * 255) / 500;
        } else {
            stars[i].brightness = 255;
        }
    }
}

void Avatar::fillPigBoundingBox(M5Canvas& canvas) {
    if (!starsActive || starCount == 0) return;

    int boxX = currentX - 25;
    int boxW = 155;  // covers tail + 7 chars + margin
    int boxY = 11;   // base y (23) minus jump headroom (12)
    int boxH = 84;   // stops above grass near y95

    // Clamp to screen
    if (boxX < 0) { boxW += boxX; boxX = 0; }
    if (boxX + boxW > 240) boxW = 240 - boxX;

    canvas.fillRect(boxX, boxY, boxW, boxH, getBGColor());
}

void Avatar::drawStars(M5Canvas& canvas) {
    if (!starsActive || starCount == 0) return;

    uint32_t now = millis();
    uint16_t fg = getDrawColor();
    canvas.setTextSize(1);
    canvas.setTextColor(fg);
    canvas.setTextDatum(top_left);

    for (uint8_t i = 0; i < starCount; i++) {
        if (stars[i].brightness < 128) continue;
        if (stars[i].y >= 88) continue;  // Match rain clip above grass

        char starChar = '.';
        if (stars[i].isBlinking) {
            uint32_t phase = (now + i * 700) % 4000;
            if (phase >= 1700 && phase < 2300) {
                starChar = '*';
            }
        }
        canvas.drawChar(starChar, stars[i].x, stars[i].y);
    }
}

// --- Phase 8: Direction control helpers ---
void Avatar::setFacingLeft() {
    facingRight = false;
}

void Avatar::setFacingRight() {
    facingRight = true;
}

// --- Phase 2: Attack shake control ---
void Avatar::setAttackShake(bool active, bool strong) {
    attackShakeActive = active;
    attackShakeStrong = strong;
    attackShakeRefreshTime = active ? millis() : 0;
}

// --- Thunder flash control (weather effect) ---
void Avatar::setThunderFlash(bool active) {
    thunderFlashActive = active;
}

bool Avatar::isThunderFlashing() {
    return thunderFlashActive;
}

// --- Phase 6: Windup slide for coast-back ---
void Avatar::startWindupSlide(int targetX, bool faceRight) {
    // Start a smooth transition to target position
    // Uses standard TRANSITION_DURATION_MS (300ms) from draw() logic
    if (currentX != targetX) {
        transitioning = true;
        transitionFromX = currentX;
        transitionToX = targetX;
        transitionStartTime = millis();
        transitionToFacingRight = faceRight;
    }
    // Set facing direction for when transition completes
    facingRight = faceRight;
}

