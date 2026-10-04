// Battle simulation: units, spotting, gunnery, guided missiles, artillery, smoke,
// objectives and the Soviet attack AI. Pure simulation - no drawing or input here.
#pragma once

#include <string>
#include <vector>

#include "terrain.hpp"
#include "vehicles.hpp"

enum class SoundKind { Cannon, Autocannon, Missile, Explosion, Artillery };

struct SoundEvent {
  SoundKind kind;
  Vector2 pos;
};

struct Unit {
  int id = 0;
  int type = 0;
  Side side = Side::NATO;
  std::string callsign;
  int platoon = -1;

  Vector2 pos{};
  float heading = 0;  // radians, 0 = east
  float turret = 0;   // absolute turret angle
  float speed = 0;
  std::vector<Vector2> path;

  bool alive = true;
  bool damaged = false;
  int hp = 2;
  float suppression = 0;  // 0..100
  float gunCd = 0, atgmCd = 0;
  int gunAmmo = 0, atgmAmmo = 0, smoke = 0;

  int target = -1;        // current engagement
  int forcedTarget = -1;  // player-assigned priority target
  float retarget = 0;
  bool holdFire = false;
  float firedRecently = 0;  // muzzle flash makes the shooter easy to spot

  float spotted[2] = {0, 0};  // seconds of remaining contact, indexed by spotting side
  bool everSeen = false;      // by NATO, so wrecks stay on the player's map
  int guiding = -1;           // id of the missile this launcher is steering
  float deathTime = -1;
  int kills = 0;
  uint32_t camo = 0;

  float aiHalt = 0, aiMove = 0;  // Soviet fire-and-movement timers
  float stillTime = 0;           // seconds since the vehicle last moved (hull-down bonus)

  const VehicleType& vt() const { return vehicleType(type); }
};

struct Shot {
  Vector2 from, to;
  float progress = 0, length = 0, speed = 1800;
  int shooter = -1, target = -1;
  bool hit = false;
  float pen = 0, killChance = 0;
  bool light = false;
  Side side = Side::NATO;
};

struct Missile {
  int id = 0;
  Vector2 pos{}, origin{}, aim{};
  float heading = 0, fuel = 0;
  int shooter = -1, target = -1;
  bool willHit = false;
  float pen = 0, killChance = 0;
  Side side = Side::NATO;
  bool alive = true;
};

struct SmokeCloud {
  Vector2 pos;
  float radius, life, maxLife;
  uint32_t seed;
  float currentRadius() const;
};

struct Barrage {
  Side side;
  Vector2 target;
  bool smoke;
  float delay;
  int shells;
  float interval, timer, radius;
};

struct Particle {
  Vector2 pos, vel;
  float life, maxLife, size, growth, drag;
  Color color;
};

struct Objective {
  std::string name;
  Vector2 pos;
  float radius;
  Side owner;
  int points;
};

struct Platoon {
  std::string name;
  Side side;
  std::vector<int> units;
  int objective = -1;
  float holdTimer = 0;
  float lastContactReport = -1000;
};

struct Message {
  std::string text;
  Color color;
  double time;
};

struct SpawnOrder {
  float time;
  int type;
  int platoon;
  int road;
};

enum class Phase { Deploy, Battle, Over };
enum class Outcome { None, DecisiveVictory, MarginalVictory, Draw, MarginalDefeat, DecisiveDefeat };

class Game {
 public:
  void start(int difficulty, uint32_t seed);
  void beginBattle();
  void update(float dt);

  // Player orders.
  void orderMove(const std::vector<int>& ids, Vector2 dest);
  void orderTarget(const std::vector<int>& ids, int enemy);
  void orderStop(const std::vector<int>& ids);
  void toggleHoldFire(const std::vector<int>& ids);
  void popSmoke(const std::vector<int>& ids);
  bool callArtillery(Vector2 target, bool smoke);

  bool visibleToPlayer(const Unit& u) const;
  bool hasLos(Vector2 a, Vector2 b) const;
  std::string nearestTown(Vector2 p) const;
  const char* outcomeName() const;

  Terrain terrain;
  std::vector<Unit> units;  // index == id; destroyed units stay as wrecks
  std::vector<Platoon> platoons;
  std::vector<Objective> objectives;
  std::vector<Shot> shots;
  std::vector<Missile> missiles;
  std::vector<SmokeCloud> smokes;
  std::vector<Barrage> barrages;
  std::vector<Particle> particles;
  std::vector<SoundEvent> sounds;
  std::vector<Message> messages;
  std::vector<SpawnOrder> spawnQueue;

  Phase phase = Phase::Deploy;
  Outcome outcome = Outcome::None;
  int difficulty = 1;
  float clock = 0;          // battle time in seconds
  float duration = 720;     // 12 minutes
  double elapsed = 0;       // wall time including deployment, for message fading
  int artyHE = 4, artySmoke = 2;
  bool artyBusy = false;
  int lossPoints[2] = {0, 0};
  int lossCount[2] = {0, 0};
  int vp[2] = {0, 0};
  bool revealAll = false;
  Vector2 wind{5.0f, -2.0f};

 private:
  void setupNato();
  void setupWaves();
  int addUnit(int type, Side side, Vector2 pos, float heading, int platoon, const std::string& callsign);
  Vector2 findSpot(Vector2 near, const VehicleType& vt, float minSpacing) const;

  void processSpawns();
  void updateAi();
  void updateWpArtillery(float dt);
  void updateSpotting();
  void updateUnit(Unit& u, float dt);
  void updateCombat(Unit& u, float dt);
  void separateUnits();
  void updateShots(float dt);
  void updateMissiles(float dt);
  void updateBarrages(float dt);
  void updateObjectives();
  void updateParticles(float dt);
  void checkEnd();
  void finish();

  int chooseTarget(Unit& u);
  const Weapon* pickWeapon(const Unit& u, const Unit& t, float dist) const;
  float hitChance(const Unit& u, const Weapon& w, const Unit& t, float dist) const;
  float armorFacing(const Unit& t, Vector2 from) const;
  void fire(Unit& u, Unit& t, const Weapon& w, float dist);
  void resolveHit(Unit& t, int shooter, float pen, float killChance, Vector2 from, bool light);
  void destroy(Unit& t, int killer);
  void shellImpact(Vector2 p, bool smoke);
  void deploySmoke(Unit& u);
  void sendToObjective(Unit& u, int objective);

  void log(const std::string& text, Color color);
  void sound(SoundKind k, Vector2 p);
  void emit(Vector2 pos, Vector2 vel, float life, float size, float growth, Color c, float drag = 1.5f);
  void explosion(Vector2 p, float scale);
  void dust(Vector2 p, float scale);

  float spotTimer_ = 0, aiTimer_ = 0, objTimer_ = 0, wpArtyTimer_ = 75;
  int nextMissileId_ = 1;
};
