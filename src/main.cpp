/*
 * PROJECT: Neo-OutRun Holographic Engine
 * HARDWARE: ESP32, 2.4" ILI9341 TFT (8-bit Parallel), Dual Passive Buzzers
 * DESCRIPTION: A pseudo-3D perspective racing engine running natively on an ESP32.
 * Includes a custom PROGMEM mirrored font engine for Pepper's Ghost optical 
 * reflection, bounding-box collision, and non-blocking dual-channel audio.
 */

#include <Arduino.h>
#include <Arduino_GFX_Library.h>

// ==============================================================================
// 1. HARDWARE INTERFACE SETUP
// ==============================================================================
// Utilizing an 8-bit parallel bus instead of SPI to maximize data bandwidth.
// This allows the ESP32 to push 16-bit color data in just two clock cycles (WR pulses),
// sustaining 60 FPS while rendering 3D math and active sprites.
Arduino_DataBus *bus = new Arduino_ESP32PAR8(
    4, GFX_NOT_DEFINED, 2, GFX_NOT_DEFINED, // DC on Pin 4, WR on Pin 2. CS/RD are hardwired.
    12, 13, 14, 15, 25, 26, 21, 22          // D0-D7 8-bit Data Bus lines
);
Arduino_GFX *gfx = new Arduino_ILI9341(bus, -1, 1, false);

// 16-bit RGB565 Color Definitions
#define COLOR_BG   0x0000 // Pure Black (Backlight blocked)
#define COLOR_DRAW 0x001F // Pure Blue

// GPIO Pin Mapping
const int BTN_LEFT = 27;       // Player Left Steer
const int BTN_RIGHT = 32;      // Player Right Steer
const int BTN_NITRO = 33;      // Speed Boost
const int BUZZER_PIN = 16;     // Event-driven SFX (Crashes, Nitro pulse)
const int BGM_BUZZER_PIN = 17; // Continuous Engine Drone (Background)

// Display & Camera Constants
const int SCREEN_W = 320;
const int SCREEN_H = 240;
const int HORIZON = 60; // Y-coordinate where the road vanishes

// ==============================================================================
// 2. CUSTOM MIRRORED FONT ENGINE (PROGMEM)
// ==============================================================================
// To counteract the lateral inversion caused by the 45-degree glass reflection 
// (Pepper's Ghost), text must be drawn horizontally flipped. Storing a 5x7 bitmap 
// in Flash memory (PROGMEM) saves dynamic RAM and allows O(1) bit-shifting to 
// draw characters backward pixel-by-pixel, avoiding heavy global screen rotation.
const uint8_t Font5x7[59][5] PROGMEM = {
  {0x00, 0x00, 0x00, 0x00, 0x00}, // 32: Space
  {0x00, 0x00, 0x5F, 0x00, 0x00}, // 33: !
  {0x00, 0x07, 0x00, 0x07, 0x00}, // 34: "
  {0x14, 0x7F, 0x14, 0x7F, 0x14}, // 35: #
  {0x24, 0x2A, 0x7F, 0x2A, 0x12}, // 36: $
  {0x23, 0x13, 0x08, 0x64, 0x62}, // 37: %
  {0x36, 0x49, 0x55, 0x22, 0x50}, // 38: &
  {0x00, 0x05, 0x03, 0x00, 0x00}, // 39: '
  {0x00, 0x1C, 0x22, 0x41, 0x00}, // 40: (
  {0x00, 0x41, 0x22, 0x1C, 0x00}, // 41: )
  {0x14, 0x08, 0x3E, 0x08, 0x14}, // 42: *
  {0x08, 0x08, 0x3E, 0x08, 0x08}, // 43: +
  {0x00, 0x50, 0x30, 0x00, 0x00}, // 44: ,
  {0x08, 0x08, 0x08, 0x08, 0x08}, // 45: -
  {0x00, 0x60, 0x60, 0x00, 0x00}, // 46: .
  {0x20, 0x10, 0x08, 0x04, 0x02}, // 47: /
  {0x3E, 0x51, 0x49, 0x45, 0x3E}, // 48: 0
  {0x00, 0x42, 0x7F, 0x40, 0x00}, // 49: 1
  {0x42, 0x61, 0x51, 0x49, 0x46}, // 50: 2
  {0x21, 0x41, 0x45, 0x4B, 0x31}, // 51: 3
  {0x18, 0x14, 0x12, 0x7F, 0x10}, // 52: 4
  {0x27, 0x45, 0x45, 0x45, 0x39}, // 53: 5
  {0x3C, 0x4A, 0x49, 0x49, 0x30}, // 54: 6
  {0x01, 0x71, 0x09, 0x05, 0x03}, // 55: 7
  {0x36, 0x49, 0x49, 0x49, 0x36}, // 56: 8
  {0x06, 0x49, 0x49, 0x29, 0x1E}, // 57: 9
  {0x00, 0x36, 0x36, 0x00, 0x00}, // 58: :
  {0x00, 0x56, 0x36, 0x00, 0x00}, // 59: ;
  {0x00, 0x08, 0x14, 0x22, 0x41}, // 60: <
  {0x14, 0x14, 0x14, 0x14, 0x14}, // 61: =
  {0x41, 0x22, 0x14, 0x08, 0x00}, // 62: >
  {0x02, 0x01, 0x51, 0x09, 0x06}, // 63: ?
  {0x32, 0x49, 0x79, 0x41, 0x3E}, // 64: @
  {0x7E, 0x11, 0x11, 0x11, 0x7E}, // 65: A
  {0x7F, 0x49, 0x49, 0x49, 0x36}, // 66: B
  {0x3E, 0x41, 0x41, 0x41, 0x22}, // 67: C
  {0x7F, 0x41, 0x41, 0x22, 0x1C}, // 68: D
  {0x7F, 0x49, 0x49, 0x49, 0x41}, // 69: E
  {0x7F, 0x09, 0x09, 0x09, 0x01}, // 70: F
  {0x3E, 0x41, 0x49, 0x49, 0x7A}, // 71: G
  {0x7F, 0x08, 0x08, 0x08, 0x7F}, // 72: H
  {0x00, 0x41, 0x7F, 0x41, 0x00}, // 73: I
  {0x20, 0x40, 0x41, 0x3F, 0x01}, // 74: J
  {0x7F, 0x08, 0x14, 0x22, 0x41}, // 75: K
  {0x7F, 0x40, 0x40, 0x40, 0x40}, // 76: L
  {0x7F, 0x02, 0x0C, 0x02, 0x7F}, // 77: M
  {0x7F, 0x04, 0x08, 0x10, 0x7F}, // 78: N
  {0x3E, 0x41, 0x41, 0x41, 0x3E}, // 79: O
  {0x7F, 0x09, 0x09, 0x09, 0x06}, // 80: P
  {0x3E, 0x41, 0x51, 0x21, 0x5E}, // 81: Q
  {0x7F, 0x09, 0x19, 0x29, 0x46}, // 82: R
  {0x46, 0x49, 0x49, 0x49, 0x31}, // 83: S
  {0x01, 0x01, 0x7F, 0x01, 0x01}, // 84: T
  {0x3F, 0x40, 0x40, 0x40, 0x3F}, // 85: U
  {0x1F, 0x20, 0x40, 0x20, 0x1F}, // 86: V
  {0x3F, 0x40, 0x38, 0x40, 0x3F}, // 87: W
  {0x63, 0x14, 0x08, 0x14, 0x63}, // 88: X
  {0x07, 0x08, 0x70, 0x08, 0x07}, // 89: Y
  {0x61, 0x51, 0x49, 0x45, 0x43}  // 90: Z
};

// Draws a single character horizontally flipped
void drawMirroredChar(int x, int y, char c, int scale, uint16_t color) {
  if (c < 32 || c > 90) c = ' '; // Default to space for unsupported chars
  int idx = c - 32;

  for (int col = 0; col < 5; col++) {
    uint8_t line = pgm_read_byte(&(Font5x7[idx][col]));
    for (int row = 0; row < 7; row++) {
      if (line & (1 << row)) {
        // MIRROR EFFECT: (4 - col) forces the columns to draw right-to-left
        gfx->fillRect(x + (4 - col) * scale, y + row * scale, scale, scale, color);
      }
    }
  }
}

// Reverses the entire string order across the X-axis for HUD rendering
void drawMirroredString(int startX, int y, const char* text, int scale, uint16_t color) {
  int len = strlen(text);
  int charWidth = 6 * scale; // 5 pixel columns + 1 pixel tracking space
  
  for (int i = 0; i < len; i++) {
    int targetX = startX - (i * charWidth); // Steps backward across the screen
    drawMirroredChar(targetX, y, text[i], scale, color);
  }
}

// ==============================================================================
// 3. GAME STATE & ENTITY SETUP
// ==============================================================================
const float Z_CAMERA_OFFSET = 6.0;
const float Z_PROJ_SCALE = 6.0;    
const float CAR_W_WORLD = 0.35;    
const float CAR_L_WORLD = 2.5;      

unsigned long scorePoints = 0;
unsigned long lastScoreUpdate = 0;
unsigned long gameStartTime = 0;

// Spawner Tracking
unsigned long lastCopSpawnTime = 0;
unsigned long lastCivSpawnTime = 0;
unsigned long lastNitroSpawnTime = 0;
unsigned long sfxLockUntil = 0; // Async timer lock for audio priority

const unsigned long ESCALATION_TIME = 240000; // 4 minutes to max difficulty
bool gameOver = false;
bool bustedSoundPlayed = false;

float difficultyMultiplier = 0.8;
float gameProgress = 0.0;

// Dynamic Heat & Nitro Systems
float bustedMeter = 0.0;
const float BUSTED_LIMIT = 100.0;
float nitroFuel = 100.0;      
bool isNitroActive = false;

// Speed Control
float globalTargetSpeed = 0.12;    
const float MAX_SPEED = 0.65;
const float NITRO_MAX_SPEED = 0.95;

// Entity Structure (Player & NPCs)
const int CAR_W = 40;
const int CAR_Y = SCREEN_H - 30;

struct Entity {
  int type;      // 1: Cop, 2: Civilian, 3: Powerup
  float x;       // World X Position (-1.0 to 1.0)
  float z;       // World Z Depth (Distance from camera)
  float speed;
  int state;     // AI state machine (Patrol vs Chase)
  bool active;
};

#define MAX_NPCS 10
Entity player;
Entity npcs[MAX_NPCS];
bool isColliding = false;
int collidingNpcIndex = -1;

void resetGame() {
  scorePoints = 0;
  bustedMeter = 0.0;
  nitroFuel = 100.0;
  globalTargetSpeed = 0.15;
  gameStartTime = millis();
  lastCopSpawnTime = millis();
  lastCivSpawnTime = millis();
  lastNitroSpawnTime = millis();
  sfxLockUntil = 0;
  gameProgress = 0.0;
  difficultyMultiplier = 0.8;
  
  player = {0, 0.0, 20.0, 0.15, 0, true};

  // Clear entity array
  for(int i = 0; i < MAX_NPCS; i++) {
    npcs[i] = {1, 0.0, 0.0, 0.0, 0, false};
  }

  // Initial populate
  npcs[0] = {1, -0.4, player.z + 50.0, MAX_SPEED * 0.4, 0, true};
  npcs[1] = {1, 0.4, player.z - 25.0, MAX_SPEED * 1.5, 1, true};
  npcs[2] = {2, 0.0, player.z + 90.0, MAX_SPEED * 0.35, 1, true};
  
  gameOver = false;
  bustedSoundPlayed = false;
  lastScoreUpdate = millis();
}

void setup() {
  Serial.begin(115200);
  
  // Configure physical push buttons with internal pull-ups
  pinMode(BTN_LEFT, INPUT_PULLUP);
  pinMode(BTN_RIGHT, INPUT_PULLUP);
  pinMode(BTN_NITRO, INPUT_PULLUP);
  
  pinMode(BUZZER_PIN, OUTPUT);
  pinMode(BGM_BUZZER_PIN, OUTPUT);
  digitalWrite(BUZZER_PIN, LOW);
  digitalWrite(BGM_BUZZER_PIN, LOW);

  // Init TFT Graphic Bus
  if (!gfx->begin()) {
    Serial.println("Display Initialization Failed!");
  }
  gfx->fillScreen(COLOR_BG);
  randomSeed(analogRead(0));

  resetGame();
}

// Blocking fail-state audio sequence
void playBustedSound() {
  noTone(BGM_BUZZER_PIN);
  digitalWrite(BGM_BUZZER_PIN, LOW);
  
  // Frequency sweep downwards to simulate engine failure
  for (int hz = 800; hz > 150; hz -= 8) {
    tone(BUZZER_PIN, hz, 15);
    delay(10);
  }
  noTone(BUZZER_PIN);
  digitalWrite(BUZZER_PIN, LOW);
}

// ==============================================================================
// 4. NON-BLOCKING AUDIO ENGINE
// ==============================================================================
// Utilizes millis() instead of delay() to modulate the background drone tone 
// synchronously with the graphics rendering loop to prevent frame-rate jitter.
void updateEngineSound() {
  static unsigned long lastEngineUpdate = 0;
  if (millis() - lastEngineUpdate > 50) {
    if (!gameOver) {
      int enginePitch = 40 + (int)(player.speed * 250); // Pitch scales with velocity
      if (isNitroActive) enginePitch += 150;
      tone(BGM_BUZZER_PIN, enginePitch);
    } else {
      noTone(BGM_BUZZER_PIN);
      digitalWrite(BGM_BUZZER_PIN, LOW);
    }
    lastEngineUpdate = millis();
  }
}

// ==============================================================================
// 5. MAIN GAME LOOP
// ==============================================================================
void loop() {
  // --- A. GAME OVER STATE ---
  if (gameOver) {
    if (!bustedSoundPlayed) {
      playBustedSound();
      bustedSoundPlayed = true;
      
      gfx->fillScreen(COLOR_BG);
      
      // Draw end-screen using mirrored font (Right-anchored coords)
      drawMirroredString(280, 50, "BUSTED", 6, COLOR_DRAW);
      char finalScoreStr[20];
      sprintf(finalScoreStr, "SCORE:%lu", scorePoints);
      drawMirroredString(280, 110, finalScoreStr, 4, COLOR_DRAW);
      drawMirroredString(300, 180, "PRESS KEY TO RETRY", 2, COLOR_DRAW);
    }

    updateEngineSound();

    // Restart check
    if (digitalRead(BTN_LEFT) == LOW || digitalRead(BTN_RIGHT) == LOW || digitalRead(BTN_NITRO) == LOW) {
      delay(200);
      resetGame();
    }
    return;
  }

  updateEngineSound();

  // --- B. GAME ESCALATION & SPWANS ---
  gameProgress = (float)(millis() - gameStartTime) / ESCALATION_TIME;
  if (gameProgress > 1.0) gameProgress = 1.0;

  difficultyMultiplier = 0.8 + (gameProgress * 1.5);
  globalTargetSpeed = 0.15 + (gameProgress * (MAX_SPEED - 0.15));

  // Dynamic Spawners based on time/difficulty
  unsigned long copSpawnDelay = 2000 - (unsigned long)(gameProgress * 1200);
  if (millis() - lastCopSpawnTime > copSpawnDelay) {
    for (int i = 0; i < MAX_NPCS; i++) {
      if (!npcs[i].active) {
        npcs[i].type = 1;
        npcs[i].active = true;
        npcs[i].x = (random(-6, 7) / 10.0);
        if (random(0, 10) < 5) {
          npcs[i].z = player.z - random(30, 50);
          npcs[i].speed = player.speed + 0.4;
          npcs[i].state = 1; // Chase State
        } else {
          npcs[i].z = player.z + random(80, 120);
          npcs[i].speed = player.speed * 0.4;
          npcs[i].state = 0; // Patrol State
        }
        lastCopSpawnTime = millis();
        break;
      }
    }
  }

  unsigned long civSpawnDelay = 2500;
  if (millis() - lastCivSpawnTime > civSpawnDelay) {
    for (int i = 0; i < MAX_NPCS; i++) {
      if (!npcs[i].active) {
        npcs[i].type = 2;
        npcs[i].active = true;
        npcs[i].x = (random(-6, 7) / 10.0);
        npcs[i].z = player.z + random(100, 160);
        npcs[i].speed = MAX_SPEED * 0.35;
        npcs[i].state = random(0, 3);
        lastCivSpawnTime = millis();
        break;
      }
    }
  }

  unsigned long nitroSpawnDelay = 5000 - (unsigned long)(gameProgress * 2500);
  if (millis() - lastNitroSpawnTime > nitroSpawnDelay) {
    for (int i = 0; i < MAX_NPCS; i++) {
      if (!npcs[i].active) {
        npcs[i].type = 3;
        npcs[i].active = true;
        npcs[i].x = (random(-6, 7) / 10.0);
        npcs[i].z = player.z + random(120, 160);
        npcs[i].speed = 0.0;
        npcs[i].state = 0;
        lastNitroSpawnTime = millis();
        break;
      }
    }
  }
  
  if (millis() - lastScoreUpdate >= 100) {
    scorePoints += (isNitroActive ? 3 : 1);
    lastScoreUpdate = millis();
  }

  // --- C. CONTROLS & PHYSICS ---
  float steeringForce = 0.03;
  // Hardware Inversion: Left button pushes Right, Right pushes Left.
  // When viewed through the 45-degree mirror, the controls feel optically correct.
  if (digitalRead(BTN_RIGHT) == LOW) player.x -= steeringForce;
  if (digitalRead(BTN_LEFT) == LOW)  player.x += steeringForce;
  
  if (player.x < -0.65) player.x = -0.65;
  if (player.x > 0.65)  player.x = 0.65;

  isNitroActive = false;
  float currentMaxSpeed = globalTargetSpeed;

  if (digitalRead(BTN_NITRO) == LOW && nitroFuel > 0.0) {
    isNitroActive = true;
    nitroFuel -= 1.2;    
    currentMaxSpeed = NITRO_MAX_SPEED;
    player.speed += 0.08;
  } else {
    if (nitroFuel < 100.0) nitroFuel += 0.15;
  }

  player.speed = player.speed * 0.85 + currentMaxSpeed * 0.15; // Velocity smoothing
  player.z += player.speed;

  // --- D. AI STATE MACHINES ---
  for (int i = 0; i < MAX_NPCS; i++) {
    if (!npcs[i].active) continue;
    float relZ = npcs[i].z - player.z;
    
    // Police AI Logic
    if (npcs[i].type == 1) {
      // Transition from Patrol (0) to Chase (1) if player is close
      if (npcs[i].state == 0) {
        if (relZ < 80.0 && relZ > -15.0) npcs[i].state = 1;
      } else if (npcs[i].state == 1) {
        if (isNitroActive && relZ < -25.0) npcs[i].state = 0; // Lose player
      }

      float targetSpeed = 0.0;
      float steerAgression = 0.01 + (0.04 * gameProgress);

      if (npcs[i].state == 0) { // Patrol
        targetSpeed = player.speed * 0.45;
        if (i % 2 == 0 && npcs[i].x < 0.3) npcs[i].x += 0.005;
        else if (i % 2 != 0 && npcs[i].x > -0.3) npcs[i].x -= 0.005;
      } else { // Chase
        if (relZ < -15.0) targetSpeed = player.speed + 0.35;
        else if (relZ < -2.0) targetSpeed = player.speed + (0.12 * difficultyMultiplier);
        else if (relZ >= -2.0 && relZ <= 15.0) {
          targetSpeed = player.speed * 0.90; // Match speed alongside
          steerAgression *= 1.4; // Aggressive ramming
        } else {
          targetSpeed = player.speed * 0.65;
          steerAgression *= 0.5;
        }
        
        // Lateral tracking (Steer toward player)
        if (npcs[i].x < player.x) npcs[i].x += steerAgression;
        else if (npcs[i].x > player.x) npcs[i].x -= steerAgression;
      }

      npcs[i].speed = npcs[i].speed * 0.9 + targetSpeed * 0.1;
      if (isNitroActive && npcs[i].speed > NITRO_MAX_SPEED * 0.75) {
          npcs[i].speed = NITRO_MAX_SPEED * 0.75;
      }
    }
    // Civilian AI Logic
    else if (npcs[i].type == 2) {
      npcs[i].speed = MAX_SPEED * 0.35;
      if (random(0, 50) == 0) npcs[i].state = random(0, 3); // Random lane drifting
      float targetX = (npcs[i].state - 1) * 0.4;
      if (npcs[i].x < targetX) npcs[i].x += 0.006;
      else if (npcs[i].x > targetX) npcs[i].x -= 0.006;
    }

    // World Bounds
    if (npcs[i].type != 3) {
      if (npcs[i].x < -0.65) npcs[i].x = -0.65;
      if (npcs[i].x > 0.65)  npcs[i].x = 0.65;
    }

    npcs[i].z += npcs[i].speed;

    // Despawn if out of render distance
    if (npcs[i].z < player.z - 55.0 || npcs[i].z > player.z + 160.0) {
      npcs[i].active = false;
    }
  }

  // --- E. BOUNDING-BOX COLLISION DETECTION ---
  isColliding = false;
  collidingNpcIndex = -1;
  bool isGrinding = false;
  
  for (int i = 0; i < MAX_NPCS; i++) {
    if (!npcs[i].active) continue;

    float dx = abs(player.x - npcs[i].x);
    float dz = abs(player.z - npcs[i].z);

    // Box Overlap Math
    if (dx < CAR_W_WORLD && dz < CAR_L_WORLD) {
      if (npcs[i].type == 3) { // Powerup Hit
        nitroFuel += 50.0;
        if (nitroFuel > 100.0) nitroFuel = 100.0;
        npcs[i].active = false;
        tone(BUZZER_PIN, 2500, 150);
        sfxLockUntil = millis() + 150;
        continue;
      }
      isColliding = true;
      collidingNpcIndex = i;
      
      float penX = CAR_W_WORLD - dx;
      float penZ = CAR_L_WORLD - dz;

      // Physics Resolution (Pushback or Lateral Shunt)
      if (penZ < penX / 3.0) {
        if (player.z < npcs[i].z) player.z = npcs[i].z - CAR_L_WORLD;
        else npcs[i].z = player.z - CAR_L_WORLD;
      } else {
        if (player.x < npcs[i].x) {
          player.x -= penX / 2.0;
          npcs[i].x += penX / 2.0;
        } else {
          player.x += penX / 2.0;
          npcs[i].x -= penX / 2.0;
        }
      }
    }
  }

  // Gameplay Penalties for Collisions
  if (isColliding && collidingNpcIndex != -1) {
    if (npcs[collidingNpcIndex].type == 1) { // Hit Cop
      if (npcs[collidingNpcIndex].z > player.z - 1.2) {
        bustedMeter += (0.60 * difficultyMultiplier); // Heat rises when grinding cops
        isGrinding = true;
        if (bustedMeter >= BUSTED_LIMIT) gameOver = true;
      } else {
        player.speed += 0.02; // Bumping cops from behind slightly boosts them
      }
    } else if (npcs[collidingNpcIndex].type == 2) { // Hit Civilian
      player.speed *= 0.5; // Massive speed penalty
      isGrinding = true;
    }
  } else {
    if (bustedMeter > 0) bustedMeter -= 0.08; // Cool down if driving clean
  }

  // --- F. SFX AUDIO HANDLER (PRIORITY QUEUE) ---
  if (millis() >= sfxLockUntil) {
    if (isGrinding) {
      tone(BUZZER_PIN, random(80, 150)); // Low freq crunch noise
      sfxLockUntil = millis() + 20;
    } else if (isNitroActive) {
      static unsigned long lastNitroPulse = 0;
      if (millis() - lastNitroPulse > 30) {
        tone(BUZZER_PIN, 1500 + random(0, 400)); // High pitched distorted scream
        sfxLockUntil = millis() + 30;
        lastNitroPulse = millis();
      }
    } else {
      noTone(BUZZER_PIN);
      digitalWrite(BUZZER_PIN, LOW);
    }
  }

  // ==============================================================================
  // 6. PSEUDO-3D RENDERING PIPELINE (CAMERA & GEOMETRY)
  // ==============================================================================
  gfx->fillScreen(COLOR_BG);
  
  float cameraZ = player.z - Z_CAMERA_OFFSET;

  // Render Road (Line-by-line Perspective Division)
  for (int y = HORIZON + 1; y < SCREEN_H; y+=2) {
    float perspective = (float)(y - HORIZON) / (SCREEN_H - HORIZON);
    float zDist = Z_PROJ_SCALE / perspective;
    float worldZ = cameraZ + zDist;
    
    // Scale road width mathematically without GPU
    float roadWidth = perspective * SCREEN_W * 2.2;

    int leftEdge = (SCREEN_W / 2) + ((-1.0 - player.x) * (roadWidth / 2.0));
    int rightEdge = (SCREEN_W / 2) + ((1.0 - player.x) * (roadWidth / 2.0));

    // Draw solid pixels at calculated road boundaries
    if (leftEdge >= 0 && leftEdge < SCREEN_W) gfx->drawPixel(leftEdge, y, COLOR_DRAW);
    if (rightEdge >= 0 && rightEdge < SCREEN_W) gfx->drawPixel(rightEdge, y, COLOR_DRAW);

    // Draw dashed lane dividers
    int segment = (int)(worldZ * (isNitroActive ? 2.5 : 1.5)) % 6;
    if (segment < 3) {
      int lane1 = (SCREEN_W / 2) + ((-0.33 - player.x) * (roadWidth / 2.0));
      int lane2 = (SCREEN_W / 2) + ((0.33 - player.x) * (roadWidth / 2.0));
      if (lane1 >= 0 && lane1 < SCREEN_W) gfx->drawPixel(lane1, y, COLOR_DRAW);
      if (lane2 >= 0 && lane2 < SCREEN_W) gfx->drawPixel(lane2, y, COLOR_DRAW);
    }
  }

  // Render Sprites (Z-Sorting / Painter's Algorithm)
  Entity* renderList[MAX_NPCS];
  int renderCount = 0;
  for(int i = 0; i < MAX_NPCS; i++) {
    if (npcs[i].active) {
      renderList[renderCount] = &npcs[i];
      renderCount++;
    }
  }

  // Bubble sort entities from far-to-near so near objects draw on top
  for(int i = 0; i < renderCount - 1; i++) {
    for(int j = 0; j < renderCount - i - 1; j++) {
      if(renderList[j]->z < renderList[j+1]->z) {
        Entity* temp = renderList[j];
        renderList[j] = renderList[j+1];
        renderList[j+1] = temp;
      }
    }
  }

  // Draw Sorted NPC Entities
  for(int i = 0; i < renderCount; i++) {
    Entity* e = renderList[i];
    float zDist = e->z - cameraZ;
    if (zDist < 1.0) continue;

    // Map 3D World Space back to 2D Screen Space
    float perspective = Z_PROJ_SCALE / zDist;
    int screenY = HORIZON + (perspective * (SCREEN_H - HORIZON));
    float roadWidth = perspective * SCREEN_W * 2.2;
    int screenX = (SCREEN_W / 2) + ((e->x - player.x) * (roadWidth / 2.0));

    // Scale geometric sprites based on distance
    int sizeW = perspective * 60;
    if (sizeW < 12) sizeW = 12;
    
    // Draw Vehicles (using fillRect over drawPixel for parallel bus speed)
    if (e->type == 1 || e->type == 2) {
      int bodyW = sizeW;
      int bodyH = sizeW * 0.3;
      if (bodyH < 3) bodyH = 3;
      
      int cabinW = sizeW * 0.8;
      int cabinH = sizeW * 0.15;
      if (cabinH < 2) cabinH = 2;
      
      int wheelW = sizeW * 0.2;
      if (wheelW < 2) wheelW = 2;
      int wheelH = sizeW * 0.15;
      if (wheelH < 2) wheelH = 2;

      int bodyX = screenX - (bodyW / 2);
      int bodyY = screenY - bodyH;
      int cabinX = bodyX + (bodyW - cabinW) / 2;
      int cabinY = bodyY - cabinH;
      int wheel1X = bodyX + (sizeW * 0.05);
      int wheel2X = bodyX + (sizeW * 0.75);
      int wheelY = bodyY + bodyH;

      gfx->fillRect(bodyX, cabinY, bodyW, (wheelY + wheelH) - cabinY, COLOR_BG); // Erase road behind car
      gfx->drawRect(bodyX, bodyY, bodyW, bodyH, COLOR_DRAW);        
      gfx->drawRect(cabinX, cabinY, cabinW, cabinH, COLOR_DRAW);    
      gfx->fillRect(wheel1X, wheelY, wheelW, wheelH, COLOR_DRAW);    
      gfx->fillRect(wheel2X, wheelY, wheelW, wheelH, COLOR_DRAW);    

      // Police Strobe Additions
      if (e->type == 1) {
        int strobe = (millis() / 80) % 2;
        int sirenW = sizeW * 0.25;
        if (sirenW < 4) sirenW = 4;
        int sirenH = sizeW * 0.15;
        if (sirenH < 3) sirenH = 3;
        int sirenX = cabinX + (cabinW - sirenW) / 2;
        int sirenY = cabinY - sirenH;
        
        gfx->fillRect(sirenX, sirenY, sirenW, sirenH, COLOR_BG);
        gfx->drawRect(sirenX, sirenY, sirenW, sirenH, COLOR_DRAW);
        
        if (e->state == 1) { // Flash blue sirens if chasing
          if (strobe == 0) gfx->fillRect(sirenX, sirenY, sirenW / 2, sirenH, COLOR_DRAW);
          else gfx->fillRect(sirenX + sirenW / 2, sirenY, sirenW / 2, sirenH, COLOR_DRAW);
        }
      } else if (e->type == 2) { // Civilian details
        if (cabinW > 6 && cabinH > 3) gfx->fillRect(cabinX + 2, cabinY + 2, cabinW - 4, cabinH - 4, COLOR_BG);
        if (bodyW > 8 && bodyH > 3) gfx->fillRect(bodyX + 3, bodyY + 2, bodyW - 6, bodyH - 4, COLOR_BG);
      }
    } 
    // Draw Power-ups (Lightning Bolt Geometry)
    else if (e->type == 3) {
      int h = sizeW * 0.9;
      int w = sizeW * 0.6;
      if (h < 6) h = 6;
      if (w < 4) w = 4;

      int lx = screenX;
      int ly = screenY - h;

      gfx->fillRect(lx - w/2, ly, w+1, h+1, COLOR_BG);
      gfx->drawTriangle(lx + w/4, ly, lx - w/2, ly + h/2, lx + w/4, ly + h/2, COLOR_DRAW);  
      gfx->drawTriangle(lx - w/4, ly + h/2, lx + w/2, ly + h/2, lx - w/4, ly + h, COLOR_DRAW);      
    }
  }

  // Draw Player Sprite (Fixed to bottom screen, shifts X-axis only)
  int currentCarX = (SCREEN_W / 2) + (player.x * (SCREEN_W / 2.5)) - (CAR_W / 2);
  gfx->fillRect(currentCarX, CAR_Y - 5, CAR_W, 20, COLOR_BG);

  gfx->fillRect(currentCarX, CAR_Y, CAR_W, 10, COLOR_DRAW);        
  gfx->fillRect(currentCarX + 4, CAR_Y - 5, 32, 5, COLOR_DRAW);    
  gfx->fillRect(currentCarX + 2, CAR_Y + 10, 8, 5, COLOR_DRAW);    
  gfx->fillRect(currentCarX + 30, CAR_Y + 10, 8, 5, COLOR_DRAW);

  // Render Nitro Flames
  if (isNitroActive) {
    for (int dy = 0; dy <= 10; dy+=2) {
      int flameWidth = 4 - (dy / 3);
      for (int dx = -flameWidth; dx <= flameWidth; dx++) {
        if ((dx + dy + (millis() / 30)) % 2 == 0) {
          gfx->drawPixel(currentCarX + 8 + dx, CAR_Y + 15 + dy, COLOR_DRAW);
          gfx->drawPixel(currentCarX + 32 + dx, CAR_Y + 15 + dy, COLOR_DRAW);
        }
      }
    }
  }

  // --- 7. HUD RENDERING (Mirrored) ---
  char scoreStr[12];
  sprintf(scoreStr, "%05lu", scorePoints);
  drawMirroredString(310, 10, scoreStr, 2, COLOR_DRAW);

  drawMirroredString(170, 10, "N2O:", 2, COLOR_DRAW);
  gfx->drawRect(185, 8, 50, 16, COLOR_DRAW);
  int nWidth = (nitroFuel / 100.0) * 46;
  gfx->fillRect(187, 10, nWidth, 12, COLOR_DRAW);

  if (bustedMeter > 0) {
    drawMirroredString(60, 10, "HEAT:", 2, COLOR_DRAW);
    gfx->drawRect(75, 8, 50, 16, COLOR_DRAW);
    int fillW = (bustedMeter / BUSTED_LIMIT) * 46;
    gfx->fillRect(77, 10, fillW, 12, COLOR_DRAW);
  } 

  delay(10); // Throttle loop for 60 FPS target
}
