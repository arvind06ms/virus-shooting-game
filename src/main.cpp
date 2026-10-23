#include "raylib.h"
#include "raymath.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <random>
#include <string>

namespace {

constexpr int kScreenW = 960;
constexpr int kScreenH = 540;
constexpr float kPlayerR = 14.0f;
constexpr float kZombieR = 18.0f;
constexpr float kBulletR = 4.0f;
constexpr float kPickupR = 12.0f;

struct Bullet {
  Vector2 pos{};
  Vector2 vel{};
  bool active = false;
};

enum class ZombieType : std::uint8_t { Normal, Fast, Tanky, Spitter, Boss };
enum class Difficulty : std::uint8_t { Easy, Normal, Hard };
enum class WeaponType : std::uint8_t { Standard, Spread, RapidFire, Heavy };

struct Zombie {
  Vector2 pos{};
  float hp = 0.0f;
  float maxHp = 0.0f;
  float speed = 0.0f;
  ZombieType type = ZombieType::Normal;
  float attackCooldown = 0.0f;
  bool active = false;
};

enum class PickupKind : std::uint8_t { Damage, FireRate, Speed, MaxHp };

struct Pickup {
  Vector2 pos{};
  PickupKind kind = PickupKind::Damage;
  bool active = false;
};

struct Particle {
  Vector2 pos{};
  Vector2 vel{};
  Color color = RAYWHITE;
  float life = 0.0f;
  float maxLife = 0.0f;
  float size = 2.0f;
  bool active = false;
};

struct FloatingText {
  Vector2 pos{};
  std::string text;
  Color color = RAYWHITE;
  float life = 0.0f;
  float maxLife = 0.0f;
  int fontSize = 16;
  bool active = false;
};

struct Rng {
  std::mt19937 gen;
  explicit Rng(std::uint32_t seed) : gen(seed) {}
  float uniform(float a, float b) {
    std::uniform_real_distribution<float> d(a, b);
    return d(gen);
  }
  int uniformInt(int lo, int hi) {
    std::uniform_int_distribution<int> d(lo, hi);
    return d(gen);
  }
};

Vector2 NormalizeOr(Vector2 v, Vector2 fallback) {
  const float len = std::sqrt(v.x * v.x + v.y * v.y);
  if (len < 1e-4f) return fallback;
  return {v.x / len, v.y / len};
}

bool CirclesOverlap(Vector2 a, float ra, Vector2 b, float rb) {
  const float dx = a.x - b.x;
  const float dy = a.y - b.y;
  const float rr = ra + rb;
  return (dx * dx + dy * dy) <= (rr * rr);
}

// First 60s: "wave 1" — viruses move slowly. After that, speed rises smoothly, stepping up over each 30s window.
float VirusSpeedFactor(float elapsedSec) {
  constexpr float kWave1Duration = 60.0f;
  constexpr float kWave1SlowMult = 0.40f;

  if (elapsedSec < kWave1Duration) {
    return kWave1SlowMult;
  }

  const float past = elapsedSec - kWave1Duration;
  constexpr float kStepSec = 30.0f;
  constexpr float kPerStep = 0.12f; // +12% per 30s after minute 1 (linear within each step)

  const float prog = past / kStepSec;
  const float lo = std::floor(prog);
  const float frac = prog - lo;
  const float scaleLo = 1.0f + kPerStep * lo;
  const float scaleHi = 1.0f + kPerStep * (lo + 1.0f);
  return scaleLo + (scaleHi - scaleLo) * frac;
}

float VirusSpawnCooldown(float elapsedSec, int wave) {
  const float wv = std::max(0.35f, 1.35f - static_cast<float>(wave) * 0.06f);

  if (elapsedSec < 60.0f) {
    return std::max(0.95f, wv * 1.75f);
  }

  const float past = elapsedSec - 60.0f;
  const float steps = past / 30.0f;
  const float spawnTighten = 1.0f - 0.055f * steps;
  return std::max(0.28f, wv * std::max(0.50f, spawnTighten));
}

void SpawnZombieAtEdge(Zombie& z, int wave, Difficulty difficulty, Rng& rng) {
  const int edge = rng.uniformInt(0, 3);
  const float pad = 24.0f;
  switch (edge) {
    case 0:
      z.pos = {pad, rng.uniform(pad, static_cast<float>(kScreenH - pad))};
      break;
    case 1:
      z.pos = {static_cast<float>(kScreenW - pad), rng.uniform(pad, static_cast<float>(kScreenH - pad))};
      break;
    case 2:
      z.pos = {rng.uniform(pad, static_cast<float>(kScreenW - pad)), pad};
      break;
    default:
      z.pos = {rng.uniform(pad, static_cast<float>(kScreenW - pad)), static_cast<float>(kScreenH - pad)};
      break;
  }

  // Difficulty scaling
  const float diffMult = (difficulty == Difficulty::Easy) ? 0.75f : (difficulty == Difficulty::Hard) ? 1.35f : 1.0f;
  const float w = static_cast<float>(wave) * diffMult;

  // Determine zombie type based on wave and rng
  int typeRoll = rng.uniformInt(0, 9);
  if (wave >= 15 && rng.uniform(0.0f, 1.0f) < 0.15f) {
    z.type = ZombieType::Boss;
  } else if (wave >= 10 && typeRoll < 2) {
    z.type = ZombieType::Spitter;
  } else if (wave >= 6 && typeRoll < 3) {
    z.type = ZombieType::Tanky;
  } else if (wave >= 3 && typeRoll < 4) {
    z.type = ZombieType::Fast;
  } else {
    z.type = ZombieType::Normal;
  }

  // Stat scaling per type
  switch (z.type) {
    case ZombieType::Fast:
      z.maxHp = (18.0f + w * 4.0f) * diffMult + rng.uniform(-1.0f, 2.0f);
      z.speed = (55.0f + w * 5.5f) * diffMult + rng.uniform(-2.0f, 3.0f);
      break;
    case ZombieType::Tanky:
      z.maxHp = (40.0f + w * 8.0f) * diffMult + rng.uniform(-3.0f, 5.0f);
      z.speed = (20.0f + w * 2.0f) * diffMult + rng.uniform(-2.0f, 2.0f);
      break;
    case ZombieType::Spitter:
      z.maxHp = (30.0f + w * 6.0f) * diffMult + rng.uniform(-2.0f, 3.0f);
      z.speed = (32.0f + w * 3.0f) * diffMult + rng.uniform(-1.0f, 2.0f);
      break;
    case ZombieType::Boss:
      z.maxHp = (100.0f + w * 15.0f) * diffMult + rng.uniform(-5.0f, 10.0f);
      z.speed = (30.0f + w * 3.5f) * diffMult + rng.uniform(-2.0f, 3.0f);
      break;
    default:
      z.maxHp = (22.0f + w * 5.0f) * diffMult + rng.uniform(-2.0f, 4.0f);
      z.speed = (38.0f + w * 4.0f) * diffMult + rng.uniform(-3.0f, 5.0f);
      break;
  }
  z.hp = z.maxHp;
  z.attackCooldown = 0.0f;
  z.active = true;
}

void SpawnPickupRandom(Pickup& p, Rng& rng) {
  const float pad = 40.0f;
  p.pos = {rng.uniform(pad, static_cast<float>(kScreenW - pad)),
           rng.uniform(pad, static_cast<float>(kScreenH - pad))};
  const int k = rng.uniformInt(0, 3);
  p.kind = static_cast<PickupKind>(k);
  p.active = true;
}

Color PickupColor(PickupKind k) {
  switch (k) {
    case PickupKind::Damage:
      return ORANGE;
    case PickupKind::FireRate:
      return SKYBLUE;
    case PickupKind::Speed:
      return LIME;
    case PickupKind::MaxHp:
      return VIOLET;
  }
  return RAYWHITE;
}

const char* PickupLabel(PickupKind k) {
  switch (k) {
    case PickupKind::Damage:
      return "DMG";
    case PickupKind::FireRate:
      return "ROF";
    case PickupKind::Speed:
      return "SPD";
    case PickupKind::MaxHp:
      return "HP+";
  }
  return "?";
}

// `feet` = ground reference / logical player position (matches collision circle center).
void DrawHumanFigure(Vector2 feet, Vector2 aimDir) {
  const Vector2 d = NormalizeOr(aimDir, {1.0f, 0.0f});
  const Vector2 perp = {-d.y, d.x};

  const float headY = feet.y - 36.0f;
  const Vector2 head = {feet.x, headY};
  DrawCircleV(head, 9.0f, Color{230, 195, 165, 255});
  DrawCircleLinesV(head, 9.0f, Color{170, 130, 105, 255});

  const Vector2 neck = {feet.x, headY + 11.0f};
  const Vector2 hip = {feet.x, feet.y - 11.0f};
  DrawLineEx(neck, hip, 5.0f, Color{65, 105, 180, 255});

  const Vector2 footL = Vector2Add(feet, Vector2Scale(perp, -8.0f));
  const Vector2 footR = Vector2Add(feet, Vector2Scale(perp, 8.0f));
  DrawLineEx(hip, footL, 4.0f, Color{48, 48, 72, 255});
  DrawLineEx(hip, footR, 4.0f, Color{48, 48, 72, 255});

  const Vector2 shoulderL = Vector2Add(neck, Vector2Scale(perp, -9.0f));
  const Vector2 shoulderR = Vector2Add(neck, Vector2Scale(perp, 9.0f));
  const Vector2 handGun = Vector2Add(shoulderR, Vector2Scale(d, 22.0f));
  DrawLineEx(shoulderL, Vector2Add(shoulderL, Vector2Scale(d, -10.0f)), 3.5f, Color{230, 195, 165, 255});
  DrawLineEx(shoulderR, handGun, 3.5f, Color{230, 195, 165, 255});
  DrawLineEx(handGun, Vector2Add(handGun, Vector2Scale(d, 14.0f)), 5.0f, Color{55, 55, 62, 255});
  DrawCircleV(Vector2Add(handGun, Vector2Scale(d, 16.0f)), 3.0f, Color{255, 220, 80, 255});
}

// EXP needed for the *next* level rises quickly so the bar fills slowly over time.
float ExpToAdvanceFromLevel(int level) {
  const float L = static_cast<float>(level);
  return 110.0f + L * 58.0f + L * L * 9.5f + L * L * L * 0.42f;
}

void ApplyExpLevelUps(float& exp, float& expToNext, int& playerLevel, float& hp, float& hpMax,
                      float& bulletDamage, float& fireInterval, float& moveSpeed) {
  while (exp >= expToNext) {
    exp -= expToNext;
    playerLevel += 1;
    expToNext = ExpToAdvanceFromLevel(playerLevel);
    const float L = static_cast<float>(playerLevel);

    // Stronger ability growth each level (HP, damage, fire rate, move speed).
    hpMax += 12.0f + L * 1.5f;
    hp = std::min(hpMax, hp + 20.0f + L * 2.5f);
    bulletDamage *= 1.048f;
    bulletDamage += 0.6f + L * 0.08f;
    fireInterval *= 0.983f;
    fireInterval = std::max(0.09f, fireInterval);
    moveSpeed += 8.0f + L * 0.6f;
  }
}

int LoadHighScore() {
  std::ifstream file("highscore.txt");
  if (file.is_open()) {
    int score = 0;
    file >> score;
    file.close();
    return score;
  }
  return 0;
}

void SaveHighScore(int score) {
  std::ofstream file("highscore.txt");
  if (file.is_open()) {
    file << score;
    file.close();
  }
}

void DrawProgressBar(float x, float y, float w, float h, float fill01, Color fill, Color track) {
  fill01 = std::clamp(fill01, 0.0f, 1.0f);
  DrawRectangleRec(Rectangle{x, y, w, h}, track);
  if (fill01 > 0.001f) {
    DrawRectangleRec(Rectangle{x, y, w * fill01, h}, fill);
  }
  DrawRectangleLinesEx(Rectangle{x, y, w, h}, 1, Color{255, 255, 255, 200});
}

void SpawnExplosion(std::array<Particle, 512>& particles, Vector2 pos, Color color, Rng& rng) {
  for (auto& p : particles) {
    if (!p.active) {
      p.active = true;
      p.pos = pos;
      float angle = rng.uniform(0.0f, 2.0f * PI);
      float speed = rng.uniform(80.0f, 280.0f);
      p.vel = {std::cos(angle) * speed, std::sin(angle) * speed};
      p.color = color;
      p.life = 0.0f;
      p.maxLife = rng.uniform(0.3f, 0.6f);
      p.size = rng.uniform(1.5f, 4.0f);
    }
  }
}

bool DrawButton(Rectangle rect, const char* text, Color color) {
  DrawRectangleRec(rect, color);
  DrawRectangleLinesEx(rect, 2, RAYWHITE);
  const int textWidth = MeasureText(text, 20);
  DrawText(text, static_cast<int>(rect.x + rect.width / 2 - textWidth / 2),
           static_cast<int>(rect.y + rect.height / 2 - 10), 20, RAYWHITE);
  
  Vector2 mousePos = GetMousePosition();
  if (CheckCollisionPointRec(mousePos, rect) && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
    return true;
  }
  return false;
}

void DrawVirusEnemy(Vector2 c, float baseR, float animT, float health01, ZombieType type) {
  if (type == ZombieType::Fast) {
    // Fast zombie: blue, smaller, more active
    const Color color = Color{100, 150, 255, 255};
    const Color dark = Color{50, 100, 200, 255};
    DrawCircleV(c, baseR * 0.85f, color);
    DrawCircleV(c, baseR * 0.5f, dark);
    // Spikes showing speed
    for (int i = 0; i < 8; ++i) {
      const float a = (static_cast<float>(i) * 2.0f * PI) / 8.0f + animT * 4.0f;
      const Vector2 p = {c.x + std::cos(a) * baseR, c.y + std::sin(a) * baseR};
      DrawLineEx(c, p, 1.5f, ColorAlpha(color, 0.6f));
    }
    return;
  }
  
  if (type == ZombieType::Tanky) {
    // Tanky zombie: green/gray, large, armored
    const Color armor = Color{80, 120, 80, 255};
    const Color core = Color{120, 160, 120, 255};
    DrawCircleV(c, baseR * 1.2f, armor);
    DrawCircleV(c, baseR * 0.85f, core);
    // Armor plates
    for (int i = 0; i < 6; ++i) {
      const float a = (static_cast<float>(i) * 2.0f * PI) / 6.0f;
      const Vector2 p = {c.x + std::cos(a) * baseR * 1.0f, c.y + std::sin(a) * baseR * 1.0f};
      DrawCircleV(p, 4.0f, ColorAlpha(armor, 0.8f));
    }
    return;
  }
  
  if (type == ZombieType::Spitter) {
    // Spitter zombie: red, with projectile indicator
    const Color color = Color{255, 100, 100, 255};
    const Color dark = Color{200, 50, 50, 255};
    DrawCircleV(c, baseR, color);
    DrawCircleV(c, baseR * 0.6f, dark);
    // Charging indicator
    const float charge = 0.5f + 0.5f * std::sin(animT * 2.5f);
    DrawCircleV(c, baseR * 0.25f, ColorAlpha(Color{255, 200, 100, 255}, charge));
    return;
  }
  
  if (type == ZombieType::Boss) {
    // Boss zombie: purple, huge, menacing
    const Color shellOuter = Color{200, 50, 200, 255};
    const Color shellMid = Color{150, 30, 150, 255};
    DrawCircleV(c, baseR * 1.3f, ColorAlpha(shellOuter, 0.2f));
    
    const int spikes = 16;
    for (int i = 0; i < spikes; ++i) {
      const float a0 = (static_cast<float>(i) * 2.0f * PI) / static_cast<float>(spikes);
      const float wobble = 0.25f * std::sin(animT * 2.0f + static_cast<float>(i) * 0.5f);
      const float len = baseR * (1.3f + 0.3f * health01 + wobble);
      const float a = a0 + animT * 0.25f;
      const Vector2 tip = {c.x + std::cos(a) * len, c.y + std::sin(a) * len};
      const Vector2 inner = {c.x + std::cos(a) * (baseR * 0.7f), c.y + std::sin(a) * (baseR * 0.7f)};
      DrawTriangle(c, inner, tip, shellMid);
      DrawLineEx(inner, tip, 3.0f, shellOuter);
    }
    
    DrawCircleV(c, baseR * 0.85f, shellOuter);
    DrawCircleV(c, baseR * 0.55f, Color{255, 100, 255, 255});
    DrawCircleV(c, baseR * 0.28f, Color{255, 200, 255, 255});
    return;
  }

  // Normal zombie (keep original)
  const int spikes = 12;
  const Color shellOuter = Color{40, 220, 120, 255};
  const Color shellMid = Color{20, 160, 90, 255};
  const Color nucleus = Color{220, 40, 200, 255};
  const Color core = Color{255, 240, 255, 255};

  DrawCircleV(c, baseR + 10.0f, ColorAlpha(shellOuter, 0.12f));

  for (int i = 0; i < spikes; ++i) {
    const float a0 = (static_cast<float>(i) * 2.0f * PI) / static_cast<float>(spikes);
    const float wobble = 0.18f * std::sin(animT * 3.2f + static_cast<float>(i) * 0.7f);
    const float len = baseR * (1.05f + 0.28f * health01 + wobble);
    const float a = a0 + animT * 0.35f;
    const Vector2 tip = {c.x + std::cos(a) * len, c.y + std::sin(a) * len};
    const Vector2 inner = {c.x + std::cos(a) * (baseR * 0.52f), c.y + std::sin(a) * (baseR * 0.52f)};
    DrawTriangle(c, inner, tip, shellMid);
    DrawLineEx(inner, tip, 2.0f, shellOuter);
  }

  DrawCircleV(c, baseR * 0.58f, shellOuter);
  DrawCircleV(c, baseR * 0.42f, nucleus);
  DrawCircleV(c, baseR * 0.22f, core);

  for (int k = 0; k < 6; ++k) {
    const float b = animT * 2.0f + static_cast<float>(k) * 1.04f;
    const Vector2 p = {c.x + std::cos(b) * baseR * 0.65f, c.y + std::sin(b) * baseR * 0.65f};
    DrawCircleV(p, 2.5f, ColorAlpha(YELLOW, 0.55f));
  }
}

// ============================================================================
// GameState Class - Encapsulates all game logic and state
// ============================================================================

class GameState {
private:
  // Entity collections
  std::vector<Zombie> zombies;
  std::vector<Bullet> bullets;
  std::vector<Pickup> pickups;
  std::array<Particle, 512> particles;
  std::array<FloatingText, 64> floatingText;
  
  // Game parameters
  Difficulty difficulty;
  int highScore;
  Rng rng;
  
  // Player state
  Vector2 playerPos;
  Vector2 playerLastDir;
  float playerHp;
  float playerHpMax;
  int playerLevel;
  float playerExp;
  float playerExpToNext;
  float playerSpeed;
  float playerBulletDamage;
  float playerFireInterval;
  float playerFireCooldown;
  
  // Game progression
  int wave;
  int killsWave;
  int killsTotal;
  int killsToNextWave;
  int upgradePoints;
  int pickupsCollected;
  float gameTime;
  float spawnTimer;
  float pickupRespawn;
  
  // Visual effects
  float screenShakeAmount;
  float screenShakeDuration;
  float cameraZoom;
  float cameraTargetZoom;
  
  // Game state
  WeaponType currentWeapon;
  bool isDead;
  
  // Constants
  static constexpr int kMaxZombies = 256;
  static constexpr int kMaxBullets = 128;
  static constexpr int kMaxPickups = 16;
  
public:
  GameState(Difficulty diff) 
    : difficulty(diff), highScore(0), rng(std::random_device{}()),
      playerPos{kScreenW * 0.5f, kScreenH * 0.5f},
      playerLastDir{1.0f, 0.0f}, playerHp(100.0f), playerHpMax(100.0f),
      playerLevel(1), playerExp(0.0f), playerSpeed(200.0f),
      playerBulletDamage(14.0f), playerFireInterval(0.28f), playerFireCooldown(0.0f),
      wave(1), killsWave(0), killsTotal(0), killsToNextWave(10), upgradePoints(0),
      pickupsCollected(0), gameTime(0.0f), spawnTimer(2.0f), pickupRespawn(4.5f),
      screenShakeAmount(0.0f), screenShakeDuration(0.0f), cameraZoom(1.0f), cameraTargetZoom(1.0f),
      currentWeapon(WeaponType::Standard), isDead(false) {
    highScore = LoadHighScore();
    playerExpToNext = ExpToAdvanceFromLevel(1);
    InitializeEntities();
  }
  
  void Update(float dt) {
    if (isDead) return;
    
    UpdateParticles(dt);
    UpdateFloatingText(dt);
    UpdateCamera(dt);
    UpdatePlayer(dt);
    UpdateZombies(dt);
    UpdateBullets(dt);
    UpdatePickups(dt);
    CheckCollisions();
    UpdateWaveProgression();
  }
  
  void Draw(float animT) {
    DrawBackground();
    DrawEntities(animT);
    DrawParticles();
    DrawFloatingText();
    DrawHUD();
  }
  
  void Restart() {
    playerPos = {kScreenW * 0.5f, kScreenH * 0.5f};
    playerLastDir = {1.0f, 0.0f};
    playerHp = 100.0f;
    playerHpMax = 100.0f;
    playerLevel = 1;
    playerExp = 0.0f;
    playerExpToNext = ExpToAdvanceFromLevel(1);
    playerSpeed = 200.0f;
    playerBulletDamage = 14.0f;
    playerFireInterval = 0.28f;
    playerFireCooldown = 0.0f;
    wave = 1;
    killsWave = 0;
    killsTotal = 0;
    killsToNextWave = 10;
    upgradePoints = 0;
    pickupsCollected = 0;
    gameTime = 0.0f;
    spawnTimer = 2.0f;
    pickupRespawn = 4.5f;
    screenShakeAmount = 0.0f;
    screenShakeDuration = 0.0f;
    cameraZoom = 1.0f;
    isDead = false;
    InitializeEntities();
  }
  
  bool IsDead() const { return isDead; }
  Vector2 GetPlayerPos() const { return playerPos; }
  Vector2 GetPlayerDir() const { return playerLastDir; }
  void SetPlayerDir(Vector2 dir) { playerLastDir = dir; }
  float GetScreenShakeAmount() const { return screenShakeAmount; }
  int GetHighScore() const { return highScore; }
  Difficulty GetDifficulty() const { return difficulty; }
  void HandleGameOverHighScore() {
    if (killsTotal > highScore) {
      highScore = killsTotal;
      SaveHighScore(highScore);
    }
  }
  
private:
  void InitializeEntities() {
    zombies.clear();
    bullets.clear();
    pickups.clear();
    
    for (auto& p : particles) p.active = false;
    for (auto& ft : floatingText) ft.active = false;
    
    for (auto& p : pickups) {
      Pickup pickup{};
      SpawnPickupRandom(pickup, rng);
      pickups.push_back(pickup);
    }
    
    int initial = 3 + wave;
    for (int i = 0; i < initial; ++i) {
      Zombie z{};
      SpawnZombieAtEdge(z, wave, difficulty, rng);
      zombies.push_back(z);
    }
  }
  
  void UpdateParticles(float dt) {
    for (auto& p : particles) {
      if (!p.active) continue;
      p.pos.x += p.vel.x * dt;
      p.pos.y += p.vel.y * dt;
      p.vel.y += 200.0f * dt;
      p.life += dt;
      if (p.life >= p.maxLife) p.active = false;
    }
  }
  
  void UpdateFloatingText(float dt) {
    for (auto& ft : floatingText) {
      if (!ft.active) continue;
      ft.pos.y -= 60.0f * dt;
      ft.life += dt;
      if (ft.life >= ft.maxLife) ft.active = false;
    }
  }
  
  void UpdateCamera(float dt) {
    cameraTargetZoom = 1.0f - (static_cast<float>(wave) * 0.02f);
    cameraTargetZoom = std::clamp(cameraTargetZoom, 0.7f, 1.2f);
    cameraZoom += (cameraTargetZoom - cameraZoom) * 0.1f;
  }
  
  void UpdatePlayer(float dt) {
    Vector2 move{0.0f, 0.0f};
    if (IsKeyDown(KEY_UP)) move.y -= 1.0f;
    if (IsKeyDown(KEY_DOWN)) move.y += 1.0f;
    if (IsKeyDown(KEY_LEFT)) move.x -= 1.0f;
    if (IsKeyDown(KEY_RIGHT)) move.x += 1.0f;
    move = NormalizeOr(move, playerLastDir);
    if (IsKeyDown(KEY_UP) || IsKeyDown(KEY_DOWN) || IsKeyDown(KEY_LEFT) || IsKeyDown(KEY_RIGHT)) {
      playerLastDir = move;
    }
    playerPos.x += move.x * playerSpeed * dt;
    playerPos.y += move.y * playerSpeed * dt;
    playerPos.x = std::clamp(playerPos.x, kPlayerR, static_cast<float>(kScreenW) - kPlayerR);
    playerPos.y = std::clamp(playerPos.y, kPlayerR, static_cast<float>(kScreenH) - kPlayerR);
    
    playerFireCooldown = std::max(0.0f, playerFireCooldown - dt);
    if (IsKeyDown(KEY_SPACE) && playerFireCooldown <= 0.0f) {
      playerFireCooldown = playerFireInterval;
      Bullet b{};
      b.active = true;
      b.pos = playerPos;
      b.vel = NormalizeOr(playerLastDir, {1.0f, 0.0f});
      b.vel.x *= 420.0f;
      b.vel.y *= 420.0f;
      bullets.push_back(b);
    }
  }
  
  void UpdateZombies(float dt) {
    float virusSpeedScale = VirusSpeedFactor(gameTime);
    for (auto& z : zombies) {
      if (!z.active) continue;
      Vector2 to{playerPos.x - z.pos.x, playerPos.y - z.pos.y};
      to = NormalizeOr(to, {0.0f, 1.0f});
      float v = z.speed * virusSpeedScale;
      z.pos.x += to.x * v * dt;
      z.pos.y += to.y * v * dt;
      
      if (CirclesOverlap(playerPos, kPlayerR, z.pos, kZombieR)) {
        float dmgScale = (gameTime < 60.0f) ? 0.65f : (1.0f + 0.04f * std::max(0.0f, (gameTime - 60.0f) / 30.0f));
        playerHp -= 28.0f * dmgScale * dt;
        screenShakeAmount = 3.0f;
        screenShakeDuration = 0.1f;
        SpawnExplosion(particles, playerPos, Color{255, 100, 100, 255}, rng);
      }
    }
  }
  
  void UpdateBullets(float dt) {
    for (auto& b : bullets) {
      if (!b.active) continue;
      b.pos.x += b.vel.x * dt;
      b.pos.y += b.vel.y * dt;
      if (b.pos.x < -40 || b.pos.x > kScreenW + 40 || b.pos.y < -40 || b.pos.y > kScreenH + 40) {
        b.active = false;
      }
    }
  }
  
  void UpdatePickups(float dt) {
    spawnTimer -= dt;
    pickupRespawn -= dt;
    
    if (spawnTimer <= 0.0f) {
      spawnTimer = VirusSpawnCooldown(gameTime, wave);
      Zombie z{};
      SpawnZombieAtEdge(z, wave, difficulty, rng);
      if (zombies.size() < kMaxZombies) zombies.push_back(z);
    }
    
    if (pickupRespawn <= 0.0f) {
      pickupRespawn = 3.2f;
      int inactive = 0;
      for (const auto& p : pickups) {
        if (!p.active) ++inactive;
      }
      if (inactive > 0) {
        for (auto& p : pickups) {
          if (!p.active) {
            SpawnPickupRandom(p, rng);
            break;
          }
        }
      }
    }
  }
  
  void CheckCollisions() {
    // Bullets vs Zombies
    for (auto& b : bullets) {
      if (!b.active) continue;
      for (auto& z : zombies) {
        if (!z.active) continue;
        if (CirclesOverlap(b.pos, kBulletR, z.pos, kZombieR)) {
          z.hp -= playerBulletDamage;
          playerExp += playerBulletDamage * 0.11f;
          b.active = false;
          
          if (z.hp <= 0.0f) {
            SpawnExplosion(particles, z.pos, Color{100, 220, 100, 255}, rng);
            for (auto& ft : floatingText) {
              if (!ft.active) {
                ft.active = true;
                ft.pos = z.pos;
                ft.text = "KILL";
                ft.color = Color{255, 200, 100, 255};
                ft.life = 0.0f;
                ft.maxLife = 0.8f;
                ft.fontSize = 20;
                break;
              }
            }
            z.active = false;
            killsWave += 1;
            killsTotal += 1;
            playerHp = std::min(playerHpMax, playerHp + 10.0f + 0.04f * playerHpMax);
            upgradePoints += 1;
            playerExp += 9.0f + static_cast<float>(playerLevel) * 2.5f;
          }
          ApplyExpLevelUps(playerExp, playerExpToNext, playerLevel, playerHp, playerHpMax,
                          playerBulletDamage, playerFireInterval, playerSpeed);
          break;
        }
      }
    }
    
    // Player vs Pickups
    for (auto& p : pickups) {
      if (!p.active) continue;
      if (CirclesOverlap(playerPos, kPlayerR + 4.0f, p.pos, kPickupR)) {
        SpawnExplosion(particles, p.pos, PickupColor(p.kind), rng);
        for (auto& ft : floatingText) {
          if (!ft.active) {
            ft.active = true;
            ft.pos = p.pos;
            ft.text = PickupLabel(p.kind);
            ft.color = PickupColor(p.kind);
            ft.life = 0.0f;
            ft.maxLife = 0.6f;
            ft.fontSize = 16;
            break;
          }
        }
        pickupsCollected += 1;
        p.active = false;
        
        switch (p.kind) {
          case PickupKind::Damage:
            playerExp += 16.0f + playerBulletDamage * 0.35f;
            playerBulletDamage *= 1.12f;
            break;
          case PickupKind::FireRate: {
            float rofScore = 1.0f / std::max(0.11f, playerFireInterval);
            playerExp += 16.0f + rofScore * 1.8f;
            playerFireInterval *= 0.90f;
            playerFireInterval = std::max(0.10f, playerFireInterval);
            break;
          }
          case PickupKind::Speed:
            playerExp += 11.0f;
            playerSpeed *= 1.08f;
            break;
          case PickupKind::MaxHp:
            playerExp += 11.0f;
            playerHpMax += 12.0f;
            playerHp = std::min(playerHpMax, playerHp + 25.0f);
            break;
        }
        ApplyExpLevelUps(playerExp, playerExpToNext, playerLevel, playerHp, playerHpMax,
                        playerBulletDamage, playerFireInterval, playerSpeed);
      }
    }
  }
  
  void UpdateWaveProgression() {
    gameTime += GetFrameTime();
    
    if (killsWave >= killsToNextWave) {
      wave += 1;
      killsWave = 0;
      killsToNextWave = 8 + wave * 3;
      playerHp = std::min(playerHpMax, playerHp + 18.0f);
      
      int burst = 2 + wave / 2;
      for (int n = 0; n < burst; ++n) {
        Zombie z{};
        SpawnZombieAtEdge(z, wave, difficulty, rng);
        if (zombies.size() < kMaxZombies) zombies.push_back(z);
      }
    }
    
    if (playerHp <= 0.0f) {
      isDead = true;
    }
  }
  
  void DrawBackground() {
    for (int x = 0; x < kScreenW; x += 48) {
      DrawLine(x, 0, x, kScreenH, Color{255, 255, 255, 12});
    }
    for (int y = 0; y < kScreenH; y += 48) {
      DrawLine(0, y, kScreenW, y, Color{255, 255, 255, 12});
    }
  }
  
  void DrawEntities(float animT) {
    for (const auto& p : pickups) {
      if (!p.active) continue;
      DrawCircleV(p.pos, kPickupR + 3.0f, ColorAlpha(PickupColor(p.kind), 0.25f));
      DrawCircleV(p.pos, kPickupR, PickupColor(p.kind));
      DrawCircleLinesV(p.pos, kPickupR, RAYWHITE);
      DrawText(PickupLabel(p.kind), static_cast<int>(p.pos.x) - 14, static_cast<int>(p.pos.y) - 28, 10, RAYWHITE);
    }
    
    for (const auto& z : zombies) {
      if (!z.active) continue;
      float t = std::clamp(z.hp / z.maxHp, 0.0f, 1.0f);
      DrawVirusEnemy(z.pos, kZombieR, animT + z.pos.x * 0.01f + z.pos.y * 0.01f, t, z.type);
      float barW = 32.0f;
      Rectangle back{z.pos.x - barW * 0.5f, z.pos.y - kZombieR - 22.0f, barW, 5.0f};
      Rectangle front{back.x, back.y, barW * t, back.height};
      DrawRectangleRec(back, Color{0, 0, 0, 150});
      DrawRectangleRec(front, Color{255, 90, 90, 255});
    }
    
    for (const auto& b : bullets) {
      if (!b.active) continue;
      DrawCircleV(b.pos, kBulletR + 1.0f, Color{255, 240, 150, 120});
      DrawCircleV(b.pos, kBulletR, Color{255, 230, 90, 255});
    }
    
    DrawHumanFigure(playerPos, playerLastDir);
    
    Vector2 aimEnd{playerPos.x + playerLastDir.x * 52.0f, playerPos.y + playerLastDir.y * 52.0f - 20.0f};
    DrawLineEx({playerPos.x, playerPos.y - 20.0f}, aimEnd, 1.5f, ColorAlpha(RAYWHITE, 0.28f));
  }
  
  void DrawParticles() {
    for (const auto& p : particles) {
      if (!p.active) continue;
      float alpha = 1.0f - (p.life / p.maxLife);
      DrawCircleV(p.pos, p.size, ColorAlpha(p.color, alpha));
    }
  }
  
  void DrawFloatingText() {
    for (const auto& ft : floatingText) {
      if (!ft.active) continue;
      float alpha = 1.0f - (ft.life / ft.maxLife);
      DrawText(ft.text.c_str(), static_cast<int>(ft.pos.x) - 20, static_cast<int>(ft.pos.y), 
               ft.fontSize, ColorAlpha(ft.color, alpha));
    }
  }
  
  void DrawHUD() {
    float barX = 14.0f;
    float barW = static_cast<float>(kScreenW) - 28.0f;
    float barH = 14.0f;
    float hpFill = (playerHpMax > 0.1f) ? (playerHp / playerHpMax) : 0.0f;
    float denom = std::max(playerExpToNext, 1.0f);
    float expFill = playerExp / denom;
    
    DrawText((std::string("LV ") + std::to_string(playerLevel)).c_str(), static_cast<int>(barX), 4, 16,
             Color{255, 230, 150, 255});
    
    DrawText("HP", static_cast<int>(barX) + 52, 4, 16, Color{200, 255, 200, 255});
    DrawProgressBar(barX + 88.0f, 4.0f, barW - 92.0f, barH, hpFill, Color{45, 200, 95, 255},
                    Color{28, 32, 40, 255});
    std::string hpNums = std::to_string(static_cast<int>(playerHp)) + " / " + std::to_string(static_cast<int>(playerHpMax));
    DrawText(hpNums.c_str(), static_cast<int>(barX + barW - 118.0f), 4, 14, Color{220, 240, 220, 255});
    
    DrawText("EXP", static_cast<int>(barX) + 52, 22, 16, Color{210, 180, 255, 255});
    DrawProgressBar(barX + 88.0f, 22.0f, barW - 92.0f, barH - 2.0f, expFill,
                    Color{160, 110, 255, 255}, Color{28, 32, 40, 255});
    std::string expNums = std::to_string(static_cast<int>(playerExp)) + " / " + std::to_string(static_cast<int>(playerExpToNext));
    DrawText(expNums.c_str(), static_cast<int>(barX + barW - 118.0f), 22, 14, Color{230, 210, 255, 255});
    
    std::string hsDisplay = "Best: " + std::to_string(highScore);
    int hsWidth = MeasureText(hsDisplay.c_str(), 14);
    DrawText(hsDisplay.c_str(), kScreenW - hsWidth - 14, 4, 14, Color{255, 215, 0, 255});
    
    const char* diffText = (difficulty == Difficulty::Easy) ? "EASY" 
                         : (difficulty == Difficulty::Hard) ? "HARD" : "NORMAL";
    Color diffColor = (difficulty == Difficulty::Easy) ? Color{100, 200, 100, 255}
                    : (difficulty == Difficulty::Hard) ? Color{255, 100, 100, 255} : Color{200, 200, 100, 255};
    DrawText(diffText, kScreenW - 60, 22, 14, diffColor);
    
    std::string hud = "Wave " + std::to_string(wave) + "   Kills " + std::to_string(killsTotal) +
                      "   Pickups " + std::to_string(pickupsCollected) + "   Points " + std::to_string(upgradePoints);
    DrawText(hud.c_str(), 14, 42, 17, RAYWHITE);
    
    int tSec = static_cast<int>(gameTime);
    int m = tSec / 60;
    int s = tSec % 60;
    const char* phase = (gameTime < 60.0f) ? "Wave 1 (calm)" : "Wave 2+ (ramping)";
    float virusScale = VirusSpeedFactor(gameTime);
    std::string timeLine = "Time " + std::to_string(m) + ":" + (s < 10 ? "0" : "") + std::to_string(s) +
                          "   " + phase + "   Virus speed x" + std::to_string(static_cast<int>(virusScale * 100.0f)) + "%";
    DrawText(timeLine.c_str(), 14, 62, 16, Color{180, 255, 200, 255});
    
    std::string statLine = "DMG " + std::to_string(static_cast<int>(playerBulletDamage)) + "   ROF " +
                          std::to_string(static_cast<int>(playerFireInterval * 1000.0f)) + "ms   SPD " +
                          std::to_string(static_cast<int>(playerSpeed));
    DrawText(statLine.c_str(), 14, 82, 16, Color{200, 230, 255, 255});
    
    DrawText("Arrows move   Space shoot   Esc quit", 14, kScreenH - 28, 15, Color{180, 180, 190, 255});
  }
  
  void UpdateScreenShake(float dt) {
    screenShakeDuration = std::max(0.0f, screenShakeDuration - dt);
    if (screenShakeDuration <= 0.0f) {
      screenShakeAmount = 0.0f;
    }
  }
};

} // namespace

int main() {
  SetConfigFlags(FLAG_VSYNC_HINT);
  InitWindow(kScreenW, kScreenH, "Outbreak — virus sweep");
  SetTargetFPS(60);

  // Difficulty selection screen
  Difficulty selectedDifficulty = Difficulty::Normal;
  bool difficultySelected = false;
  
  while (!WindowShouldClose() && !difficultySelected) {
    BeginDrawing();
    ClearBackground(Color{18, 20, 26, 255});
    
    DrawText("SELECT DIFFICULTY", kScreenW / 2 - 120, 100, 40, RAYWHITE);
    
    if (DrawButton(Rectangle{kScreenW / 2 - 120, 200, 100, 50}, "EASY", Color{100, 200, 100, 255})) {
      selectedDifficulty = Difficulty::Easy;
      difficultySelected = true;
    }
    
    if (DrawButton(Rectangle{kScreenW / 2 - 25, 200, 120, 50}, "NORMAL", Color{200, 200, 100, 255})) {
      selectedDifficulty = Difficulty::Normal;
      difficultySelected = true;
    }
    
    if (DrawButton(Rectangle{kScreenW / 2 + 120, 200, 100, 50}, "HARD", Color{255, 100, 100, 255})) {
      selectedDifficulty = Difficulty::Hard;
      difficultySelected = true;
    }
    
    DrawText("Press ESC to quit", kScreenW / 2 - 100, 350, 20, Color{180, 180, 180, 255});
    
    EndDrawing();
  }

  if (WindowShouldClose()) {
    CloseWindow();
    return 0;
  }

  // Create game state with selected difficulty
  GameState gameState(selectedDifficulty);
  
  // Main game loop
  while (!WindowShouldClose()) {
    float dt = GetFrameTime();
    float animT = static_cast<float>(GetTime());
    
    // Update game state
    gameState.Update(dt);
    
    // Render
    BeginDrawing();
    ClearBackground(Color{18, 20, 26, 255});
    gameState.Draw(animT);
    
    // Draw death screen if needed
    if (gameState.IsDead()) {
      gameState.HandleGameOverHighScore();
      
      DrawRectangle(0, 0, kScreenW, kScreenH, Color{0, 0, 0, 160});
      const char* msg = "YOU DIED";
      const int fs = 48;
      const int tw = MeasureText(msg, fs);
      DrawText(msg, kScreenW / 2 - tw / 2, kScreenH / 2 - 40, fs, RAYWHITE);
      
      DrawText("Press R to restart", kScreenW / 2 - MeasureText("Press R to restart", 20) / 2, 
               kScreenH / 2 + 48, 20, SKYBLUE);
    }
    
    EndDrawing();
    
    // Handle restart
    if (gameState.IsDead() && IsKeyPressed(KEY_R)) {
      gameState.Restart();
    }
  }

  CloseWindow();
  return 0;
}
