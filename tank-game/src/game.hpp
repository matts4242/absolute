// Battle simulation: units, spotting, gunnery, guided missiles, artillery, smoke,
// objectives and the Soviet attack AI. Pure simulation - no drawing or input here.
#pragma once

#include <string>
#include <vector>

#include "terrain.hpp"
#include "vehicles.hpp"

enum class SoundKind { Cannon, Autocannon, Missile, Explosion, Artillery };

// How a unit moves. Faster modes trade accuracy and concealment for speed.
enum class MoveMode { Hold, Move, Quick, Deliberate, Assault };
// Formation kept by a group moving together under one order.
enum class Formation { None, Column, Line, Wedge, EchelonLeft, EchelonRight };

const char* moveModeName(MoveMode m);
const char* formationName(Formation f);
Color moveModeColor(MoveMode m);

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
  std::vector<int> pathLeg;  // formation leg of each waypoint (empty when moving alone)

  MoveMode mode = MoveMode::Hold;
  int formation = -1;        // index into Game::formations, -1 when not in one
  int legsDone = 0;          // formation legs completed (members wait for each other)
  Vector2 assaultPoint{};    // where an assault is aimed
  float assaultTimer = 0;
  float waitTime = 0;        // time spent waiting for formation members
  bool orderPending = false; // an order is on its way down the chain of command

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

  float haltTimer = 0, boundTimer = 0;  // short halts to shoot during fire and movement
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

struct Order {
  MoveMode mode = MoveMode::Move;
  Formation formation = Formation::None;
  Vector2 dest{};
  float facing = 0;
  std::vector<int> units;
  std::vector<Vector2> slots;  // destination of each unit, same order as units
  float delay = 0;             // seconds of battle time until the units act on it
};

struct FormationGroup {
  std::vector<int> members;
  float speedCap = 0;  // the group moves at the pace of its slowest vehicle
};

struct SpawnOrder {
  float time;
  int type;
  int platoon;
  int road;
};

// Deploy: free set-up. Orders: clock frozen, both sides plan. Battle: the turn executes.
enum class Phase { Deploy, Orders, Battle, Over };
enum class Outcome { None, DecisiveVictory, MarginalVictory, Draw, MarginalDefeat, DecisiveDefeat };

class Game {
 public:
  void start(int difficulty, uint32_t seed);
  void beginBattle();   // Deploy -> first orders phase
  void executeTurn();   // Orders -> Battle for one turn
  void update(float dt);

  // Player orders. Movement orders are queued in the orders phase and reach the units
  // after a command delay once the turn executes; during deployment they act at once.
  bool issueOrder(const std::vector<int>& ids, Vector2 dest, MoveMode mode, Formation formation);
  void orderTarget(const std::vector<int>& ids, int enemy);
  void orderStop(const std::vector<int>& ids);
  // Where each unit would end up for a given order (used for the on-map preview).
  std::vector<Vector2> formationSlots(const std::vector<int>& ids, Vector2 dest, Formation f, float* facing) const;
  const Order* pendingOrderFor(int unit) const;
  bool canIssueOrders() const { return phase == Phase::Deploy || phase == Phase::Orders; }
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
  std::vector<Order> pendingOrders;
  std::vector<FormationGroup> formations;

  Phase phase = Phase::Deploy;
  Outcome outcome = Outcome::None;
  int difficulty = 1;
  int turn = 0;
  float turnLength = 60;    // battle seconds per turn
  float turnClock = 0;      // seconds into the current turn
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
  void applyOrder(const Order& o);
  void cancelPending(int unit);
  void leaveFormation(Unit& u);
  void updateAssault(Unit& u, float dt);
  std::vector<Vector2> formationOffsets(const std::vector<int>& ids, Vector2 dest, Formation f, float* facing) const;

  void log(const std::string& text, Color color);
  void sound(SoundKind k, Vector2 p);
  void emit(Vector2 pos, Vector2 vel, float life, float size, float growth, Color c, float drag = 1.5f);
  void explosion(Vector2 p, float scale);
  void dust(Vector2 p, float scale);

  float spotTimer_ = 0, aiTimer_ = 0, objTimer_ = 0, wpArtyTimer_ = 75;
  int nextMissileId_ = 1;
};
