#include "game.hpp"

#include <algorithm>
#include <cstdio>

Rng& simRng() {
  static Rng r(1);
  return r;
}

Rng& fxRng() {
  static Rng r(7);
  return r;
}

namespace {

constexpr float kMissileSpeed = 230.0f;
constexpr size_t kMaxParticles = 4000;

struct WaveSpec {
  float time;
  const char* name;
  int road;
  std::vector<std::pair<int, int>> vehicles;  // (type, count)
};

std::string fmtMeters(float worldDist) {
  char buf[32];
  std::snprintf(buf, sizeof buf, "%d m", static_cast<int>(worldDist * kMetersPerUnit / 10) * 10);
  return buf;
}

float segmentPointDistance(Vector2 a, Vector2 b, Vector2 p) {
  Vector2 ab = b - a;
  float len2 = Vector2LengthSqr(ab);
  if (len2 < 1e-4f) return Vector2Distance(a, p);
  float t = std::clamp(Vector2DotProduct(p - a, ab) / len2, 0.0f, 1.0f);
  return Vector2Distance(a + ab * t, p);
}

}  // namespace

const char* moveModeName(MoveMode m) {
  switch (m) {
    case MoveMode::Hold: return "Hold";
    case MoveMode::Move: return "Move";
    case MoveMode::Quick: return "Quick move";
    case MoveMode::Deliberate: return "Deliberate move";
    case MoveMode::Assault: return "Assault";
  }
  return "";
}

const char* formationName(Formation f) {
  switch (f) {
    case Formation::None: return "No formation";
    case Formation::Column: return "Column";
    case Formation::Line: return "Line";
    case Formation::Wedge: return "Wedge";
    case Formation::EchelonLeft: return "Echelon left";
    case Formation::EchelonRight: return "Echelon right";
  }
  return "";
}

Color moveModeColor(MoveMode m) {
  switch (m) {
    case MoveMode::Hold: return {220, 220, 220, 255};
    case MoveMode::Move: return {120, 190, 255, 255};
    case MoveMode::Quick: return {90, 240, 240, 255};
    case MoveMode::Deliberate: return {140, 240, 120, 255};
    case MoveMode::Assault: return {255, 110, 80, 255};
  }
  return WHITE;
}

namespace {

float modeSpeed(MoveMode m) {
  switch (m) {
    case MoveMode::Quick: return 1.3f;
    case MoveMode::Deliberate: return 0.55f;
    case MoveMode::Assault: return 0.9f;
    default: return 1.0f;
  }
}

float modeRoadBias(MoveMode m) { return m == MoveMode::Quick ? 2.0f : m == MoveMode::Deliberate ? 0.8f : 1.0f; }

}  // namespace

float SmokeCloud::currentRadius() const {
  float age = maxLife - life;
  float grow = std::min(1.0f, 0.35f + age / 4.0f);
  float fade = std::min(1.0f, life / 6.0f);
  return radius * grow * (0.6f + 0.4f * fade);
}

// ---------------------------------------------------------------------------
// Setup
// ---------------------------------------------------------------------------

void Game::start(int diff, uint32_t seed) {
  *this = Game{};
  difficulty = diff;
  simRng().seed(seed);
  terrain.generate(seed);

  const char* fallbackNames[] = {"Hill 410", "Hill 385", "Hill 402"};
  for (int i : terrain.objectiveTowns()) {
    const Town& t = terrain.towns()[i];
    objectives.push_back({t.name, t.center, std::max(t.radius, 110.0f), Side::NATO, 100});
  }
  // Generation always places three bridge towns, but stay robust if one failed.
  for (int i = static_cast<int>(objectives.size()); i < 3; ++i) {
    Vector2 p{terrain.width() * (0.2f + 0.3f * i), terrain.height() * 0.68f};
    objectives.push_back({fallbackNames[i], p, 110.0f, Side::NATO, 100});
  }
  std::sort(objectives.begin(), objectives.end(),
            [](const Objective& a, const Objective& b) { return a.pos.x < b.pos.x; });

  setupNato();
  setupWaves();
  phase = Phase::Deploy;
  log("Deployment: position your forces, then press ENTER to start the battle.", RAYWHITE);
  log("Intelligence expects a Soviet attack across the river within minutes.", {200, 200, 140, 255});
}

void Game::beginBattle() {
  if (phase != Phase::Deploy) return;
  // Deployment moves are finished instantly so turn 1 starts from the chosen positions.
  for (Unit& u : units) {
    if (!u.path.empty()) u.pos = u.path.back();
    u.path.clear();
    u.pathLeg.clear();
    u.speed = 0;
    u.mode = MoveMode::Hold;
    leaveFormation(u);
  }
  phase = Phase::Orders;
  turn = 1;
  log("H-Hour. Soviet forces are crossing the inner German border.", {255, 180, 120, 255});
  log("Turn 1 - orders phase. Give orders, then press ENTER to execute the turn.", RAYWHITE);
}

void Game::executeTurn() {
  if (phase != Phase::Orders) return;
  phase = Phase::Battle;
  turnClock = 0;
}

Vector2 Game::findSpot(Vector2 near, const VehicleType& vt, float minSpacing) const {
  for (int ring = 0; ring < 12; ++ring) {
    int samples = ring == 0 ? 1 : ring * 8;
    for (int i = 0; i < samples; ++i) {
      float a = 2 * PI * i / samples;
      Vector2 p = near + fromAngle(a, ring * 16.0f);
      if (!terrain.passable(p, vt)) continue;
      bool clear = true;
      for (const Unit& u : units) {
        if (u.alive && Vector2Distance(u.pos, p) < minSpacing) {
          clear = false;
          break;
        }
      }
      if (clear) return p;
    }
  }
  return near;
}

int Game::addUnit(int type, Side side, Vector2 pos, float heading, int platoon,
                  const std::string& callsign) {
  Unit u;
  u.id = static_cast<int>(units.size());
  u.type = type;
  u.side = side;
  u.pos = pos;
  u.heading = u.turret = heading;
  u.platoon = platoon;
  u.callsign = callsign;
  const VehicleType& vt = vehicleType(type);
  u.gunAmmo = vt.gun.ammo;
  u.atgmAmmo = vt.atgm.valid() ? vt.atgm.ammo : 0;
  u.smoke = vt.smokeCharges;
  u.camo = static_cast<uint32_t>(simRng().range(0, 1 << 30));
  u.retarget = simRng().uniform(0, 0.5f);
  units.push_back(u);
  if (platoon >= 0) platoons[platoon].units.push_back(u.id);
  return u.id;
}

void Game::setupNato() {
  struct Group {
    const char* name;
    std::vector<int> types;
    Vector2 pos;
  };
  const float north = -PI / 2;
  Vector2 o0 = objectives[0].pos, o1 = objectives[1].pos, o2 = objectives[2].pos;
  std::vector<Group> groups = {
      {"Ghost", {kM3Bradley, kM3Bradley, kLuchs}, o1 + Vector2{0, -110}},
      {"Tango", {kM1Abrams, kM1Abrams, kM1Abrams, kM1Abrams}, o1 + Vector2{0, 90}},
      {"Panther", {kLeopard2, kLeopard2, kLeopard2}, o0 + Vector2{0, 90}},
      {"Mustang", {kM2Bradley, kM2Bradley, kM2Bradley, kM2Bradley}, o2 + Vector2{0, 90}},
      {"Hammer", {kM901Itv, kM901Itv}, (o1 + o2) * 0.5f + Vector2{0, 170}},
      {"Reserve", {kM60A3, kM60A3, kM113}, Vector2{terrain.width() * 0.5f, terrain.height() * 0.9f}},
  };
  for (const Group& g : groups) {
    int pi = static_cast<int>(platoons.size());
    platoons.push_back({g.name, Side::NATO, {}, -1});
    for (size_t i = 0; i < g.types.size(); ++i) {
      const VehicleType& vt = vehicleType(g.types[i]);
      float offset = (static_cast<float>(i) - (g.types.size() - 1) / 2.0f) * 45.0f;
      Vector2 p = findSpot(g.pos + Vector2{offset, 0}, vt, 30.0f);
      addUnit(g.types[i], Side::NATO, p, north, pi, std::string(g.name) + "-" + std::to_string(i + 1));
    }
  }
}

void Game::setupWaves() {
  std::vector<WaveSpec> waves = {
      {20, "Recon Group Sokol", 0, {{kBRDM2, 2}, {kBTR70, 1}}},
      {26, "Recon Group Berkut", 2, {{kBRDM2, 1}, {kBTR70, 2}}},
      {70, "1st Motor Rifle Coy", 1, {{kBMP2, 3}, {kT72A, 1}}},
      {80, "2nd Motor Rifle Coy", 0, {{kBMP2, 3}, {kZSU234, 1}}},
      {90, "3rd Motor Rifle Coy", 2, {{kBMP2, 3}, {kBTR70, 1}}},
      {170, "1st Tank Coy", 1, {{kT80B, 3}}},
      {180, "2nd Tank Coy", 0, {{kT80B, 3}}},
      {190, "3rd Tank Coy", 2, {{kT72A, 3}}},
      {280, "2nd Echelon Tank Bn", 1, {{kT62M, 3}, {kBMP2, 2}}},
      {290, "2nd Echelon MR Bn", 2, {{kT72A, 2}, {kBMP2, 3}}},
      {300, "2nd Echelon Recon", 0, {{kBTR70, 3}, {kBRDM2, 1}}},
      {400, "Operational Manoeuvre Group", 1, {{kT80B, 2}, {kT72A, 2}}},
  };
  if (difficulty >= 2) {
    waves.push_back({440, "OMG Second Wave", 0, {{kT80B, 3}, {kBMP2, 2}}});
    waves.push_back({460, "OMG Third Wave", 2, {{kT72A, 3}, {kZSU234, 1}}});
  }
  const float scale = difficulty == 0 ? 0.7f : difficulty == 1 ? 1.0f : 1.25f;

  for (const WaveSpec& w : waves) {
    int pi = static_cast<int>(platoons.size());
    int road = std::min(w.road, static_cast<int>(terrain.northRoads().size()) - 1);
    Platoon p{w.name, Side::WP, {}, std::min(w.road, static_cast<int>(objectives.size()) - 1)};
    platoons.push_back(p);
    float t = w.time;
    for (const auto& [type, count] : w.vehicles) {
      int n = std::max(1, static_cast<int>(count * scale + 0.5f));
      for (int i = 0; i < n; ++i) {
        spawnQueue.push_back({t, type, pi, road});
        t += 2.5f;
      }
    }
  }
  std::sort(spawnQueue.begin(), spawnQueue.end(),
            [](const SpawnOrder& a, const SpawnOrder& b) { return a.time < b.time; });
}

// ---------------------------------------------------------------------------
// Main update
// ---------------------------------------------------------------------------

void Game::update(float dt) {
  elapsed += dt;
  updateParticles(dt);
  for (auto& s : smokes) s.life -= dt;
  smokes.erase(std::remove_if(smokes.begin(), smokes.end(), [](const SmokeCloud& s) { return s.life <= 0; }),
               smokes.end());
  if (phase == Phase::Over || phase == Phase::Orders) return;  // the clock is frozen while planning

  if (phase == Phase::Battle) {
    clock += dt;
    turnClock += dt;
    // Orders work their way down the chain of command before the units act on them.
    for (Order& o : pendingOrders) o.delay -= dt;
    for (size_t i = 0; i < pendingOrders.size();) {
      if (pendingOrders[i].delay <= 0) {
        Order o = pendingOrders[i];
        pendingOrders.erase(pendingOrders.begin() + static_cast<long>(i));
        applyOrder(o);
      } else {
        ++i;
      }
    }
    processSpawns();
    aiTimer_ -= dt;
    if (aiTimer_ <= 0) {
      aiTimer_ = 1.0f;
      updateAi();
    }
    updateWpArtillery(dt);
  }

  spotTimer_ -= dt;
  if (spotTimer_ <= 0) {
    updateSpotting();
    spotTimer_ = 0.25f;
  }

  for (Unit& u : units) updateUnit(u, dt);
  separateUnits();
  updateShots(dt);
  updateMissiles(dt);
  updateBarrages(dt);

  if (phase == Phase::Battle) {
    objTimer_ -= dt;
    if (objTimer_ <= 0) {
      objTimer_ = 1.0f;
      updateObjectives();
    }
    checkEnd();
    if (phase == Phase::Battle && turnClock >= turnLength) {
      phase = Phase::Orders;
      ++turn;
      log("Turn " + std::to_string(turn) + " - orders phase (H+" + std::to_string(static_cast<int>(clock) / 60) + ":" +
              (static_cast<int>(clock) % 60 < 10 ? "0" : "") + std::to_string(static_cast<int>(clock) % 60) + ").",
          RAYWHITE);
    }
  }
}

void Game::processSpawns() {
  for (auto it = spawnQueue.begin(); it != spawnQueue.end();) {
    if (it->time > clock) break;
    int col = terrain.northRoads()[it->road];
    Vector2 entry = terrain.tileCenter(col, 0) + Vector2{0, 6};
    bool blocked = false;
    for (const Unit& u : units) {
      if (u.alive && Vector2Distance(u.pos, entry) < 36) {
        blocked = true;
        break;
      }
    }
    if (blocked) {
      ++it;
      continue;
    }
    Platoon& p = platoons[it->platoon];
    std::string callsign = p.name + " " + std::to_string(p.units.size() + 1);
    int id = addUnit(it->type, Side::WP, entry, PI / 2, it->platoon, callsign);
    sendToObjective(units[id], p.objective);
    it = spawnQueue.erase(it);
  }
}

void Game::sendToObjective(Unit& u, int objective) {
  const Objective& o = objectives[objective];
  Vector2 spot = o.pos + fromAngle(simRng().uniform(0, 2 * PI), simRng().uniform(0, o.radius * 0.7f));
  if (!terrain.passable(spot, u.vt())) spot = o.pos;
  // Soviet SOP: scouts creep forward, everyone else races up the roads, then assaults.
  const UnitClass cls = u.vt().cls;
  float d = Vector2Distance(u.pos, o.pos);
  if (cls == UnitClass::Recon || cls == UnitClass::AntiTank) u.mode = MoveMode::Deliberate;
  else u.mode = d > 900.0f ? MoveMode::Quick : MoveMode::Assault;
  u.assaultPoint = spot;
  u.path = terrain.findPath(u.pos, spot, u.vt(), modeRoadBias(u.mode));
  u.pathLeg.clear();
}

void Game::updateAi() {
  for (Platoon& p : platoons) {
    if (p.side != Side::WP || p.objective < 0) continue;
    std::vector<Unit*> alive;
    Vector2 center{0, 0};
    for (int id : p.units) {
      if (units[id].alive) {
        alive.push_back(&units[id]);
        center += units[id].pos;
      }
    }
    if (alive.empty()) continue;
    center = center / static_cast<float>(alive.size());

    // Once an objective is taken, the platoon pushes on to the next NATO-held one.
    if (objectives[p.objective].owner == Side::WP) {
      p.holdTimer += 1.0f;
      if (p.holdTimer > 25.0f) {
        int best = -1;
        float bestD = 1e9f;
        for (size_t i = 0; i < objectives.size(); ++i) {
          if (objectives[i].owner == Side::NATO) {
            float d = Vector2Distance(center, objectives[i].pos);
            if (d < bestD) bestD = d, best = static_cast<int>(i);
          }
        }
        if (best >= 0) {
          p.objective = best;
          p.holdTimer = 0;
          for (Unit* u : alive) sendToObjective(*u, best);
        }
      }
    } else {
      p.holdTimer = 0;
    }

    const Objective& o = objectives[p.objective];
    for (Unit* u : alive) {
      // Shake out of the road march into the assault once close to the objective.
      if (u->mode == MoveMode::Quick && Vector2Distance(u->pos, o.pos) < 900.0f) sendToObjective(*u, p.objective);
      if (!u->path.empty() || u->mode == MoveMode::Assault) continue;
      if (Vector2Distance(u->pos, o.pos) > o.radius) {
        sendToObjective(*u, p.objective);
      } else if (o.owner != Side::WP && u->target < 0 && simRng().chance(0.15f)) {
        sendToObjective(*u, p.objective);  // still contested: sweep through the town
      }
    }
  }
}

void Game::updateWpArtillery(float dt) {
  wpArtyTimer_ -= dt;
  if (wpArtyTimer_ > 0) return;
  // Target the densest cluster of NATO vehicles the Soviets can currently see.
  int best = -1, bestCount = 0;
  for (const Unit& u : units) {
    if (!u.alive || u.side != Side::NATO || u.spotted[sideIndex(Side::WP)] <= 0) continue;
    int count = 0;
    for (const Unit& o : units) {
      if (o.alive && o.side == Side::NATO && Vector2Distance(o.pos, u.pos) < 150) ++count;
    }
    if (count > bestCount) bestCount = count, best = u.id;
  }
  if (best < 0) {
    wpArtyTimer_ = 8.0f;
    return;
  }
  Vector2 aim = units[best].pos + fromAngle(simRng().uniform(0, 2 * PI), simRng().uniform(0, 60));
  barrages.push_back({Side::WP, aim, false, 12.0f, 10, 0.5f, 0.0f, 110.0f});
  wpArtyTimer_ = difficulty >= 2 ? simRng().uniform(40, 60) : simRng().uniform(55, 85);
}

// ---------------------------------------------------------------------------
// Spotting
// ---------------------------------------------------------------------------

bool Game::hasLos(Vector2 a, Vector2 b) const {
  for (const SmokeCloud& s : smokes) {
    if (segmentPointDistance(a, b, s.pos) < s.currentRadius() * 0.85f) return false;
  }
  return terrain.lineOfSight(a, b);
}

void Game::updateSpotting() {
  const float step = 0.25f;
  for (int si = 0; si < 2; ++si) {
    Side spotter = static_cast<Side>(si);
    for (Unit& e : units) {
      if (e.side == spotter) continue;
      e.spotted[si] = std::max(0.0f, e.spotted[si] - step);
      if (!e.alive) continue;
      float conceal = terrain.concealmentAt(e.pos) * e.vt().size;
      if (e.speed > 2.0f) {
        // Quick moves kick up dust; deliberate moves creep from cover to cover.
        conceal *= e.mode == MoveMode::Quick ? 1.5f : e.mode == MoveMode::Deliberate ? 1.0f : 1.25f;
      }
      if (e.firedRecently > 0) conceal *= 1.8f;
      conceal = std::min(conceal, 1.6f);
      bool seen = false;
      for (const Unit& f : units) {
        if (!f.alive || f.side != spotter) continue;
        float d = Vector2Distance(f.pos, e.pos);
        float optics = f.vt().optics * (f.mode == MoveMode::Deliberate ? 1.2f : 1.0f);
        if (f.mode == MoveMode::Quick && f.speed > 2.0f) optics *= 0.8f;  // eyes on the road
        if (d > optics * conceal) continue;
        if (!hasLos(f.pos, e.pos)) continue;
        seen = true;
        break;
      }
      if (!seen) continue;
      if (spotter == Side::NATO) {
        e.everSeen = true;
        Platoon& p = platoons[e.platoon];
        if (e.spotted[si] <= 0 && clock - p.lastContactReport > 40.0f) {
          p.lastContactReport = clock;
          log("Contact! " + std::string(e.vt().name) + " spotted near " + nearestTown(e.pos) + ".",
              {255, 210, 120, 255});
        }
      }
      e.spotted[si] = 1.5f;
    }
  }
}

std::string Game::nearestTown(Vector2 p) const {
  std::string best = "the river";
  float bestD = 1e9f;
  for (const Town& t : terrain.towns()) {
    float d = Vector2Distance(t.center, p);
    if (d < bestD) bestD = d, best = t.name;
  }
  return best;
}

bool Game::visibleToPlayer(const Unit& u) const {
  if (u.side == Side::NATO || revealAll) return true;
  if (!u.alive) return u.everSeen;
  return u.spotted[sideIndex(Side::NATO)] > 0;
}

// ---------------------------------------------------------------------------
// Units
// ---------------------------------------------------------------------------

void Game::updateUnit(Unit& u, float dt) {
  const VehicleType& vt = u.vt();
  if (!u.alive) {
    float age = clock - u.deathTime;
    if (age < 90.0f && fxRng().chance(dt * 5.0f * (1.0f - age / 90.0f))) {
      emit(u.pos, wind + Vector2{fxRng().uniform(-3, 3), fxRng().uniform(-3, 3)}, fxRng().uniform(3, 6),
           fxRng().uniform(4, 7), 5.0f, {40, 38, 36, 150}, 0.2f);
    }
    if (age < 30.0f && fxRng().chance(dt * 10.0f)) {
      emit(u.pos + Vector2{fxRng().uniform(-5, 5), fxRng().uniform(-5, 5)}, {0, -6}, 0.4f, 3.5f, 2.0f,
           {255, static_cast<unsigned char>(fxRng().range(110, 180)), 40, 220});
    }
    return;
  }

  u.gunCd -= dt;
  u.atgmCd -= dt;
  u.firedRecently -= dt;
  u.haltTimer -= dt;
  u.boundTimer -= dt;
  u.suppression = std::max(0.0f, u.suppression - 7.0f * dt);

  // Formation members wait at the end of each leg until the whole group has caught up.
  bool waiting = false;
  float speedCap = 1e9f;
  if (u.formation >= 0 && !u.pathLeg.empty()) {
    const FormationGroup& g = formations[u.formation];
    int leg = u.pathLeg.front();
    for (int m : g.members) {
      const Unit& o = units[m];
      if (o.alive && o.formation == u.formation && !o.pathLeg.empty() && o.legsDone < leg) waiting = true;
    }
    speedCap = g.speedCap;
    // Don't wait forever for a vehicle that is stuck or pinned down.
    u.waitTime = waiting ? u.waitTime + dt : 0.0f;
    if (u.waitTime > 20.0f) waiting = false;
  }

  // Movement along the path.
  bool halted = u.guiding >= 0 || u.haltTimer > 0 || waiting;
  float targetSpeed = 0;
  if (!u.path.empty() && !halted) {
    Vector2 wp = u.path.front();
    float d = Vector2Distance(u.pos, wp);
    float arrive = u.path.size() == 1 ? 6.0f : 18.0f;
    if (d < arrive) {
      u.path.erase(u.path.begin());
      if (!u.pathLeg.empty()) {
        int leg = u.pathLeg.front();
        u.pathLeg.erase(u.pathLeg.begin());
        if (u.pathLeg.empty() || u.pathLeg.front() != leg) u.legsDone = leg + 1;
      }
      if (u.path.empty() && u.mode != MoveMode::Assault) u.mode = MoveMode::Hold;
    } else {
      float desired = angleTo(u.pos, wp);
      u.heading = turnTowards(u.heading, desired, vt.turnRate * dt);
      float err = fabsf(wrapAngle(desired - u.heading));
      float sf = terrain.speedFactorAt(u.pos, vt);
      if (sf <= 0) sf = 0.3f;
      const float pinnedAt = u.mode == MoveMode::Assault ? 90.0f : 70.0f;  // assaulting troops press on
      float base = std::min(vt.speed, speedCap);
      float maxSp = base * sf * modeSpeed(u.mode) * (u.damaged ? 0.5f : 1.0f) * (u.suppression > pinnedAt ? 0.4f : 1.0f);
      targetSpeed = err > 1.2f ? maxSp * 0.1f : maxSp * (1.0f - err / 1.5f);
      if (u.path.size() == 1) targetSpeed = std::min(targetSpeed, d * 0.8f + 2.0f);
    }
  }
  float accel = 14.0f * dt;
  u.speed = u.speed < targetSpeed ? std::min(targetSpeed, u.speed + accel) : std::max(targetSpeed, u.speed - accel * 2);
  u.stillTime = u.speed > 0.5f ? 0.0f : u.stillTime + dt;
  if (u.speed > 0.01f) {
    Vector2 step = fromAngle(u.heading, u.speed * dt);
    if (terrain.passable(u.pos + step, vt)) u.pos += step;
    else if (terrain.passable(u.pos + Vector2{step.x, 0}, vt)) u.pos.x += step.x;
    else if (terrain.passable(u.pos + Vector2{0, step.y}, vt)) u.pos.y += step.y;
    else u.speed = 0;
  }

  if (phase == Phase::Battle) {
    updateAssault(u, dt);
    updateCombat(u, dt);
  } else {
    u.turret = turnTowards(u.turret, u.heading, vt.turretRate * dt);
  }
}

// Once an assault reaches its objective, the unit closes with any enemy it can see nearby.
void Game::updateAssault(Unit& u, float dt) {
  if (u.mode != MoveMode::Assault || !u.path.empty()) return;
  u.assaultTimer -= dt;
  if (u.assaultTimer > 0) return;
  u.assaultTimer = 2.0f;
  const int si = sideIndex(u.side);
  int best = -1;
  float bestD = 1e9f;
  for (const Unit& e : units) {
    if (!e.alive || e.side == u.side || e.spotted[si] <= 0) continue;
    if (Vector2Distance(e.pos, u.assaultPoint) > 300.0f) continue;
    float d = Vector2Distance(u.pos, e.pos);
    if (d < bestD) bestD = d, best = e.id;
  }
  if (best < 0) {
    u.mode = MoveMode::Hold;  // position cleared
    return;
  }
  if (bestD > 70.0f) u.path = terrain.findPath(u.pos, units[best].pos, u.vt());
}

void Game::separateUnits() {
  const float minDist = 16.0f;
  for (size_t i = 0; i < units.size(); ++i) {
    Unit& a = units[i];
    if (!a.alive) continue;
    for (size_t j = i + 1; j < units.size(); ++j) {
      Unit& b = units[j];
      if (!b.alive) continue;
      Vector2 d = b.pos - a.pos;
      float len = Vector2Length(d);
      if (len >= minDist) continue;
      Vector2 push = len > 0.01f ? d * ((minDist - len) / len * 0.5f) : Vector2{0.5f, 0};
      if (terrain.passable(a.pos - push, a.vt())) a.pos -= push;
      if (terrain.passable(b.pos + push, b.vt())) b.pos += push;
    }
  }
}

// ---------------------------------------------------------------------------
// Combat
// ---------------------------------------------------------------------------

float Game::armorFacing(const Unit& t, Vector2 from) const {
  float rel = fabsf(wrapAngle(angleTo(t.pos, from) - t.heading));
  if (rel < 0.9f) return t.vt().armorFront;
  if (rel > 2.5f) return t.vt().armorSide * 0.8f;  // rear
  return t.vt().armorSide;
}

int Game::chooseTarget(Unit& u) {
  const VehicleType& vt = u.vt();
  const int si = sideIndex(u.side);
  float maxRange = 0;
  if (vt.gun.valid() && u.gunAmmo != 0) maxRange = vt.gun.range;
  if (vt.atgm.valid() && u.atgmAmmo > 0) maxRange = std::max(maxRange, vt.atgm.range);
  if (maxRange <= 0) return -1;

  if (u.forcedTarget >= 0) {
    const Unit& f = units[u.forcedTarget];
    if (!f.alive) {
      u.forcedTarget = -1;
    } else if (f.spotted[si] > 0 && Vector2Distance(u.pos, f.pos) <= maxRange && hasLos(u.pos, f.pos)) {
      return f.id;
    }
  }

  int best = -1;
  float bestScore = -1e9f;
  for (const Unit& e : units) {
    if (!e.alive || e.side == u.side || e.spotted[si] <= 0) continue;
    float d = Vector2Distance(u.pos, e.pos);
    if (d > maxRange) continue;
    if (!hasLos(u.pos, e.pos)) continue;
    float armor = armorFacing(e, u.pos);
    bool canKill = (vt.gun.valid() && vt.gun.pen * 1.1f > armor && d <= vt.gun.range) ||
                   (vt.atgm.valid() && u.atgmAmmo > 0 && vt.atgm.pen > armor && d <= vt.atgm.range);
    float score = e.vt().points * (canKill ? 1.0f : 0.1f) - d * 0.02f;
    if (e.target == u.id) score += 25;  // shoot back at whoever is shooting at us
    if (e.id == u.target) score += 10;  // avoid flip-flopping
    if (score > bestScore) bestScore = score, best = e.id;
  }
  return best;
}

const Weapon* Game::pickWeapon(const Unit& u, const Unit& t, float dist) const {
  const VehicleType& vt = u.vt();
  float armor = armorFacing(t, u.pos);
  bool gunOk = vt.gun.valid() && u.gunAmmo != 0 && dist <= vt.gun.range && u.gunCd <= 0;
  bool atgmOk = vt.atgm.valid() && u.atgmAmmo > 0 && dist <= vt.atgm.range && u.atgmCd <= 0 &&
                u.speed < 3.0f;
  bool gunEffective = gunOk && vt.gun.pen * 1.1f > armor;
  if (t.vt().armorFront >= 150) {  // tank-class target
    if (gunEffective) return &vt.gun;
    if (atgmOk && vt.atgm.pen > armor * 0.8f) return &vt.atgm;
    return nullptr;
  }
  if (gunOk) return &vt.gun;
  if (atgmOk && !(vt.gun.valid() && dist <= vt.gun.range)) return &vt.atgm;
  return nullptr;
}

float Game::hitChance(const Unit& u, const Weapon& w, const Unit& t, float dist) const {
  float p = w.accuracy;
  float r = dist / w.range;
  p *= 1.0f - 0.5f * r * r;
  if (w.missile) {
    if (t.speed > 2) p *= 0.9f;
    p *= std::sqrt(terrain.coverAt(t.pos));
  } else {
    if (u.speed > 2) {
      if (u.mode == MoveMode::Quick) p *= 0.6f;  // firing on the run
      p *= w.stabilized ? 0.75f : 0.35f;
      if (u.side == Side::WP) p *= 0.7f;  // cruder Soviet fire control on the move
    }
    if (t.speed > 2) p *= 0.85f;
    p *= terrain.coverAt(t.pos);
  }
  if (t.stillTime > 10.0f) p *= 0.75f;  // settled hull-down in a firing position
  p *= 0.8f + 0.2f * t.vt().size;
  p *= 1.0f - (u.mode == MoveMode::Assault ? 0.25f : 0.5f) * u.suppression / 100.0f;
  if (u.damaged) p *= 0.8f;
  return std::clamp(p, 0.03f, 0.97f);
}

void Game::updateCombat(Unit& u, float dt) {
  const VehicleType& vt = u.vt();
  u.retarget -= dt;
  if (u.retarget <= 0) {
    u.retarget = 0.5f + simRng().uniform(0, 0.2f);
    u.target = chooseTarget(u);
  }
  if (u.target >= 0) {
    const Unit& t = units[u.target];
    if (!t.alive || t.spotted[sideIndex(u.side)] <= 0) u.target = -1;
  }
  if (u.target < 0) {
    u.turret = turnTowards(u.turret, u.heading, vt.turretRate * dt);
    return;
  }
  Unit& t = units[u.target];
  float d = Vector2Distance(u.pos, t.pos);
  float desired = angleTo(u.pos, t.pos);
  u.turret = turnTowards(u.turret, desired, vt.turretRate * dt);

  // Fire and movement: a deliberate move (and Soviet doctrine, except when racing up a road
  // or assaulting) halts briefly to shoot from a standstill, then presses on.
  bool shortHalts = u.mode == MoveMode::Deliberate ||
                    (u.side == Side::WP && u.mode != MoveMode::Quick && u.mode != MoveMode::Assault);
  if (shortHalts && !u.path.empty() && u.haltTimer <= 0 && u.boundTimer <= 0) {
    bool inRange = (vt.gun.valid() && d <= vt.gun.range) || (vt.atgm.valid() && u.atgmAmmo > 0 && d <= vt.atgm.range);
    if (inRange) {
      bool overwatch = vt.cls == UnitClass::AntiTank || vt.cls == UnitClass::Recon || u.mode == MoveMode::Deliberate;
      u.haltTimer = overwatch ? 9.0f : 4.0f;
      u.boundTimer = u.haltTimer + 5.0f;
    }
  }

  if (u.guiding >= 0) return;
  if (u.holdFire && u.forcedTarget != u.target) return;
  if (fabsf(wrapAngle(desired - u.turret)) > 0.06f) return;
  const Weapon* w = pickWeapon(u, t, d);
  if (!w) return;
  if (!hasLos(u.pos, t.pos)) {
    u.target = -1;
    return;
  }
  fire(u, t, *w, d);
}

void Game::fire(Unit& u, Unit& t, const Weapon& w, float dist) {
  const VehicleType& vt = u.vt();
  float p = hitChance(u, w, t, dist);
  Vector2 muzzle = localPoint(u.pos, u.turret, vt.length * 0.6f, 0);

  if (w.missile) {
    u.atgmCd = w.reload;
    --u.atgmAmmo;
    Missile m;
    m.id = nextMissileId_++;
    m.pos = m.origin = muzzle;
    m.heading = u.turret;
    m.shooter = u.id;
    m.target = t.id;
    m.willHit = simRng().chance(p);
    m.aim = t.pos + fromAngle(simRng().uniform(0, 2 * PI), simRng().uniform(40, 90));
    m.pen = w.pen;
    m.killChance = w.killChance;
    m.side = u.side;
    m.fuel = w.range / kMissileSpeed + 2.0f;
    missiles.push_back(m);
    u.guiding = m.id;
    u.speed = 0;
    u.firedRecently = 3.0f;
    for (int i = 0; i < 8; ++i) {  // launch back-blast
      emit(localPoint(u.pos, u.turret, -vt.length * 0.3f, 0),
           fromAngle(u.turret + PI + fxRng().uniform(-0.6f, 0.6f), fxRng().uniform(20, 50)), 1.0f, 3, 8,
           {200, 196, 186, 160});
    }
    sound(SoundKind::Missile, u.pos);
    return;
  }

  u.gunCd = w.reload * (1.0f + u.suppression / 100.0f * 0.8f) * (w.light ? simRng().uniform(0.8f, 1.2f) : 1.0f);
  if (u.gunAmmo > 0) --u.gunAmmo;
  bool hit = simRng().chance(p);
  Shot s;
  s.from = muzzle;
  s.to = hit ? t.pos + Vector2{simRng().uniform(-4, 4), simRng().uniform(-4, 4)}
             : t.pos + fromAngle(simRng().uniform(0, 2 * PI), simRng().uniform(15, 45) * (1 + dist / 1000));
  s.length = Vector2Distance(s.from, s.to);
  s.speed = w.light ? 1300.0f : 1800.0f;
  s.shooter = u.id;
  s.target = t.id;
  s.hit = hit;
  s.pen = w.pen;
  s.killChance = w.killChance;
  s.light = w.light;
  s.side = u.side;
  shots.push_back(s);
  u.firedRecently = w.light ? 1.0f : 2.5f;

  if (w.light) {
    emit(muzzle, {0, 0}, 0.06f, 2.5f, 10, {255, 230, 140, 255});
    sound(SoundKind::Autocannon, u.pos);
  } else {
    emit(muzzle, {0, 0}, 0.1f, 6, 30, {255, 240, 170, 255});
    for (int i = 0; i < 6; ++i) {
      emit(muzzle, fromAngle(u.turret + fxRng().uniform(-0.8f, 0.8f), fxRng().uniform(10, 40)), 1.5f, 4, 9,
           {190, 186, 176, 150});
    }
    sound(SoundKind::Cannon, u.pos);
  }
}

void Game::resolveHit(Unit& t, int shooter, float pen, float killChance, Vector2 from, bool light) {
  if (!t.alive) return;
  float armor = armorFacing(t, from);
  float effective = pen * simRng().uniform(0.85f, 1.15f);
  if (effective > armor) {
    t.suppression = std::min(100.0f, t.suppression + 50);
    if (simRng().chance(killChance) || t.hp <= 1) {
      destroy(t, shooter);
      return;
    }
    --t.hp;
    t.damaged = true;
    for (int i = 0; i < 10; ++i) {
      emit(t.pos, fromAngle(fxRng().uniform(0, 2 * PI), fxRng().uniform(20, 70)), 0.3f, 1.5f, 0,
           {255, 200, 90, 255}, 4.0f);
    }
    if (t.side == Side::NATO) log(t.callsign + " (" + t.vt().name + ") has been hit and damaged!", {255, 160, 90, 255});
    if (t.smoke > 0) deploySmoke(t);
  } else {
    t.suppression = std::min(100.0f, t.suppression + (light ? 6.0f : 25.0f));
    for (int i = 0; i < (light ? 2 : 8); ++i) {
      emit(t.pos, fromAngle(fxRng().uniform(0, 2 * PI), fxRng().uniform(30, 90)), 0.2f, 1.2f, 0,
           {255, 240, 180, 255}, 4.0f);
    }
    if (!light && t.smoke > 0 && simRng().chance(0.4f)) deploySmoke(t);
  }
}

void Game::destroy(Unit& t, int killer) {
  t.alive = false;
  t.deathTime = clock;
  t.speed = 0;
  t.path.clear();
  t.target = -1;
  const int si = sideIndex(t.side);
  lossPoints[si] += t.vt().points;
  lossCount[si] += 1;
  explosion(t.pos, 1.0f);
  sound(SoundKind::Explosion, t.pos);
  for (Unit& o : units) {
    if (o.alive && o.side == t.side && Vector2Distance(o.pos, t.pos) < 120) {
      o.suppression = std::min(100.0f, o.suppression + 20);
    }
  }
  if (t.side == Side::NATO) {
    log(t.callsign + " (" + t.vt().name + ") destroyed.", {255, 110, 90, 255});
  } else if (killer >= 0 && units[killer].side == Side::NATO) {
    Unit& k = units[killer];
    ++k.kills;
    log(k.callsign + " destroyed a " + t.vt().name + " at " + fmtMeters(Vector2Distance(k.pos, t.pos)) + ".",
        {140, 200, 255, 255});
  } else if (killer < 0 && t.everSeen) {
    log("Artillery destroyed a " + std::string(t.vt().name) + ".", {140, 200, 255, 255});
  }
}

void Game::deploySmoke(Unit& u) {
  if (u.smoke <= 0) return;
  --u.smoke;
  Vector2 p = localPoint(u.pos, u.turret, 35, 0);
  smokes.push_back({p, 48, 25, 25, static_cast<uint32_t>(fxRng().range(0, 1 << 30))});
  for (int i = 0; i < 6; ++i) {
    emit(p, fromAngle(fxRng().uniform(0, 2 * PI), fxRng().uniform(10, 30)), 1.0f, 6, 12, {220, 220, 220, 200});
  }
}

void Game::updateShots(float dt) {
  for (Shot& s : shots) {
    s.progress += s.speed * dt;
    if (s.progress < s.length) continue;
    Unit& t = units[s.target];
    if (s.hit && t.alive) {
      resolveHit(t, s.shooter, s.pen, s.killChance, s.from, s.light);
    } else {
      dust(s.to, s.light ? 0.3f : 0.7f);
      if (t.alive && Vector2Distance(t.pos, s.to) < 60) {
        t.suppression = std::min(100.0f, t.suppression + (s.light ? 4.0f : 12.0f));
      }
    }
  }
  shots.erase(std::remove_if(shots.begin(), shots.end(), [](const Shot& s) { return s.progress >= s.length; }),
              shots.end());
}

void Game::updateMissiles(float dt) {
  for (Missile& m : missiles) {
    Unit& s = units[m.shooter];
    Unit& t = units[m.target];
    // SACLOS: the gunner must stay alive and keep the target in sight until impact.
    if (m.willHit && !(s.alive && t.alive && hasLos(s.pos, t.pos))) {
      m.willHit = false;
      m.aim = m.pos + fromAngle(m.heading + simRng().uniform(-0.4f, 0.4f), 250);
    }
    Vector2 dest = m.willHit ? t.pos : m.aim;
    m.heading = turnTowards(m.heading, angleTo(m.pos, dest), 3.0f * dt);
    m.pos += fromAngle(m.heading, kMissileSpeed * dt);
    m.fuel -= dt;
    emit(m.pos, {fxRng().uniform(-2, 2), fxRng().uniform(-2, 2)}, 1.2f, 1.6f, 4, {215, 212, 205, 170}, 0.5f);

    bool done = false;
    if (m.willHit && Vector2Distance(m.pos, t.pos) < 10) {
      resolveHit(t, m.shooter, m.pen, m.killChance, m.origin, false);
      done = true;
    } else if (!m.willHit && (Vector2Distance(m.pos, m.aim) < 10 || m.fuel <= 0)) {
      dust(m.pos, 0.8f);
      done = true;
    } else if (m.fuel <= -3) {
      done = true;
    }
    if (done) {
      m.alive = false;
      if (s.guiding == m.id) s.guiding = -1;
    }
  }
  missiles.erase(std::remove_if(missiles.begin(), missiles.end(), [](const Missile& m) { return !m.alive; }),
                 missiles.end());
}

bool Game::callArtillery(Vector2 target, bool smoke) {
  if (phase != Phase::Orders || artyBusy) return false;  // fire missions are planned between turns
  int& left = smoke ? artySmoke : artyHE;
  if (left <= 0) return false;
  --left;
  artyBusy = true;
  if (smoke) {
    barrages.push_back({Side::NATO, target, true, 7.0f, 5, 0.5f, 0.0f, 55.0f});
    log("Fire mission: smoke at grid " + nearestTown(target) + " sector. Splash in 7 seconds.", {170, 220, 255, 255});
  } else {
    barrages.push_back({Side::NATO, target, false, 9.0f, 8, 0.55f, 0.0f, 70.0f});
    log("Fire mission: 155mm HE near " + nearestTown(target) + ". Splash in 9 seconds.", {170, 220, 255, 255});
  }
  return true;
}

void Game::updateBarrages(float dt) {
  for (Barrage& b : barrages) {
    if (b.delay > 0) {
      b.delay -= dt;
      if (b.delay <= 0 && b.side == Side::WP) {
        for (const Unit& u : units) {
          if (u.alive && u.side == Side::NATO && Vector2Distance(u.pos, b.target) < 250) {
            log("Incoming artillery near " + u.callsign + "!", {255, 140, 100, 255});
            break;
          }
        }
      }
      continue;
    }
    b.timer -= dt;
    if (b.timer <= 0 && b.shells > 0) {
      --b.shells;
      b.timer = b.interval;
      Vector2 p = b.target + fromAngle(simRng().uniform(0, 2 * PI), std::sqrt(simRng().uniform()) * b.radius);
      shellImpact(p, b.smoke);
    }
  }
  for (const Barrage& b : barrages) {
    if (b.shells <= 0 && b.side == Side::NATO) artyBusy = false;
  }
  barrages.erase(std::remove_if(barrages.begin(), barrages.end(), [](const Barrage& b) { return b.shells <= 0; }),
                 barrages.end());
}

void Game::shellImpact(Vector2 p, bool smoke) {
  if (smoke) {
    smokes.push_back({p, 75, 55, 55, static_cast<uint32_t>(fxRng().range(0, 1 << 30))});
    dust(p, 0.5f);
    sound(SoundKind::Artillery, p);
    return;
  }
  explosion(p, 0.8f);
  dust(p, 1.4f);
  sound(SoundKind::Artillery, p);
  for (Unit& u : units) {
    if (!u.alive) continue;
    float d = Vector2Distance(u.pos, p);
    if (d > 90) continue;
    u.suppression = std::min(100.0f, u.suppression + 55.0f * (1.0f - d / 90.0f));
    if (d > 28) continue;
    bool light = u.vt().armorSide < 50;
    float roll = simRng().uniform();
    float kill = light ? 0.3f : 0.03f, damage = light ? 0.3f : 0.1f;
    if (roll < kill) {
      destroy(u, -1);
    } else if (roll < kill + damage) {
      u.damaged = true;
      if (--u.hp <= 0) destroy(u, -1);
    }
  }
}

// ---------------------------------------------------------------------------
// Objectives and victory
// ---------------------------------------------------------------------------

void Game::updateObjectives() {
  for (Objective& o : objectives) {
    int count[2] = {0, 0};
    for (const Unit& u : units) {
      if (u.alive && Vector2Distance(u.pos, o.pos) < o.radius * 1.1f) ++count[sideIndex(u.side)];
    }
    int nato = count[0], wp = count[1];
    if (wp > 0 && nato == 0 && o.owner != Side::WP) {
      o.owner = Side::WP;
      log(o.name + " has fallen to Soviet forces!", {255, 110, 90, 255});
    } else if (nato > 0 && wp == 0 && o.owner != Side::NATO) {
      o.owner = Side::NATO;
      log(o.name + " is back in NATO hands.", {140, 200, 255, 255});
    }
  }
}

void Game::checkEnd() {
  bool natoAlive = false, wpAlive = false;
  for (const Unit& u : units) {
    if (!u.alive) continue;
    (u.side == Side::NATO ? natoAlive : wpAlive) = true;
  }
  if (!natoAlive || (spawnQueue.empty() && !wpAlive) || clock >= duration) finish();
}

void Game::finish() {
  phase = Phase::Over;
  revealAll = true;
  vp[0] = lossPoints[1];
  vp[1] = lossPoints[0];
  for (const Objective& o : objectives) vp[sideIndex(o.owner)] += o.points;
  float ratio = static_cast<float>(vp[0]) / std::max(1, vp[1]);
  outcome = ratio >= 2.0f   ? Outcome::DecisiveVictory
            : ratio >= 1.2f ? Outcome::MarginalVictory
            : ratio >= 0.8f ? Outcome::Draw
            : ratio >= 0.5f ? Outcome::MarginalDefeat
                            : Outcome::DecisiveDefeat;
  log(std::string("Battle over: ") + outcomeName() + ".", RAYWHITE);
}

const char* Game::outcomeName() const {
  switch (outcome) {
    case Outcome::DecisiveVictory: return "NATO Decisive Victory";
    case Outcome::MarginalVictory: return "NATO Marginal Victory";
    case Outcome::Draw: return "Draw";
    case Outcome::MarginalDefeat: return "Soviet Marginal Victory";
    case Outcome::DecisiveDefeat: return "Soviet Decisive Victory";
    case Outcome::None: break;
  }
  return "In progress";
}

// ---------------------------------------------------------------------------
// Player orders
// ---------------------------------------------------------------------------

bool Game::issueOrder(const std::vector<int>& ids, Vector2 dest, MoveMode mode, Formation formation) {
  if (!canIssueOrders()) return false;
  std::vector<int> alive;
  for (int id : ids) {
    if (units[id].alive) alive.push_back(id);
  }
  if (alive.empty()) return false;
  for (int id : alive) cancelPending(id);

  Order o;
  o.mode = mode;
  o.formation = alive.size() > 1 ? formation : Formation::None;
  o.dest = dest;
  o.units = alive;
  o.slots = formationSlots(alive, dest, o.formation, &o.facing);
  if (phase == Phase::Deploy) {
    applyOrder(o);
    return true;
  }
  // Command delay: a few seconds for the order to be passed on, longer for a suppressed
  // unit or for a whole group that has to coordinate a formation move.
  float supp = 0;
  for (int id : alive) supp += units[id].suppression;
  supp /= static_cast<float>(alive.size());
  o.delay = 2.0f + simRng().uniform(0.0f, 4.0f) + supp / 25.0f + (o.formation != Formation::None ? 2.0f : 0.0f);
  for (int id : alive) units[id].orderPending = true;
  pendingOrders.push_back(o);
  return true;
}

const Order* Game::pendingOrderFor(int unit) const {
  for (const Order& o : pendingOrders) {
    if (std::find(o.units.begin(), o.units.end(), unit) != o.units.end()) return &o;
  }
  return nullptr;
}

void Game::cancelPending(int unit) {
  for (Order& o : pendingOrders) {
    for (size_t i = 0; i < o.units.size(); ++i) {
      if (o.units[i] == unit) {
        o.units.erase(o.units.begin() + static_cast<long>(i));
        o.slots.erase(o.slots.begin() + static_cast<long>(i));
        break;
      }
    }
  }
  pendingOrders.erase(std::remove_if(pendingOrders.begin(), pendingOrders.end(),
                                     [](const Order& o) { return o.units.empty(); }),
                      pendingOrders.end());
  units[unit].orderPending = false;
}

void Game::leaveFormation(Unit& u) {
  u.formation = -1;
  u.pathLeg.clear();
  u.legsDone = 0;
  u.waitTime = 0;
}

std::vector<Vector2> Game::formationOffsets(const std::vector<int>& ids, Vector2 dest, Formation f,
                                            float* facingOut) const {
  std::vector<Vector2> offsets(ids.size(), Vector2{0, 0});  // (forward, right) from dest
  std::vector<size_t> alive;
  Vector2 center{0, 0};
  for (size_t i = 0; i < ids.size(); ++i) {
    if (units[ids[i]].alive) alive.push_back(i), center += units[ids[i]].pos;
  }
  if (alive.empty()) {
    if (facingOut) *facingOut = 0;
    return offsets;
  }
  center = center / static_cast<float>(alive.size());
  float facing = Vector2Distance(center, dest) > 1.0f ? angleTo(center, dest) : units[ids[alive[0]]].heading;
  if (facingOut) *facingOut = facing;
  const float c = cosf(facing), sn = sinf(facing);
  auto toLocal = [&](Vector2 v) { return Vector2{v.x * c + v.y * sn, -v.x * sn + v.y * c}; };

  if (f == Formation::None) {  // keep the current layout, compressed to 90 units from the centre
    float maxLen = 0;
    for (size_t i : alive) maxLen = std::max(maxLen, Vector2Distance(units[ids[i]].pos, center));
    float scale = maxLen > 90 ? 90 / maxLen : 1.0f;
    for (size_t i : alive) offsets[i] = toLocal((units[ids[i]].pos - center) * scale);
    return offsets;
  }

  const int n = static_cast<int>(alive.size());
  const float sp = 45.0f;  // 90 m between vehicles
  std::vector<Vector2> slots(n);
  for (int k = 0; k < n; ++k) {
    switch (f) {
      case Formation::Column: slots[k] = {-k * sp * 1.1f, 0}; break;
      case Formation::Line: slots[k] = {0, (k - (n - 1) / 2.0f) * sp}; break;
      case Formation::Wedge: {
        int j = (k + 1) / 2;
        slots[k] = {-j * sp * 0.8f, (k % 2 ? -1.0f : 1.0f) * j * sp};
        break;
      }
      case Formation::EchelonLeft: slots[k] = {-k * sp * 0.7f, -k * sp * 0.7f}; break;
      case Formation::EchelonRight: slots[k] = {-k * sp * 0.7f, k * sp * 0.7f}; break;
      case Formation::None: break;
    }
  }
  // Give each vehicle the slot on its own side of the group so paths don't cross.
  auto key = [&](Vector2 local) {
    return f == Formation::Column ? -local.x : local.y * 1000.0f - local.x;
  };
  std::vector<int> unitOrder(n), slotOrder(n);
  for (int k = 0; k < n; ++k) unitOrder[k] = slotOrder[k] = k;
  std::sort(unitOrder.begin(), unitOrder.end(), [&](int a, int b) {
    return key(toLocal(units[ids[alive[a]]].pos - center)) < key(toLocal(units[ids[alive[b]]].pos - center));
  });
  std::sort(slotOrder.begin(), slotOrder.end(), [&](int a, int b) { return key(slots[a]) < key(slots[b]); });
  for (int k = 0; k < n; ++k) offsets[alive[unitOrder[k]]] = slots[slotOrder[k]];
  return offsets;
}

std::vector<Vector2> Game::formationSlots(const std::vector<int>& ids, Vector2 dest, Formation f,
                                          float* facing) const {
  float a = 0;
  std::vector<Vector2> offs = formationOffsets(ids, dest, f, &a);
  if (facing) *facing = a;
  std::vector<Vector2> out;
  for (const Vector2& o : offs) out.push_back(localPoint(dest, a, o.x, o.y));
  return out;
}

void Game::applyOrder(const Order& o) {
  std::vector<int> ids;
  std::vector<Vector2> slots;
  for (size_t i = 0; i < o.units.size(); ++i) {
    if (units[o.units[i]].alive) ids.push_back(o.units[i]), slots.push_back(o.slots[i]);
  }
  if (ids.empty()) return;
  const float bias = modeRoadBias(o.mode);
  for (int id : ids) {
    Unit& u = units[id];
    u.orderPending = false;
    leaveFormation(u);
    u.mode = o.mode;
    u.haltTimer = u.boundTimer = 0;
    if (o.mode == MoveMode::Hold) u.path.clear();
  }
  if (o.mode == MoveMode::Hold) return;

  auto moveAlone = [&](size_t i) {
    Unit& u = units[ids[i]];
    Vector2 d = terrain.passable(slots[i], u.vt()) ? slots[i] : o.dest;
    u.path = terrain.findPath(u.pos, d, u.vt(), bias);
    u.assaultPoint = d;
    if (u.path.empty()) {
      u.mode = MoveMode::Hold;
      if (u.side == Side::NATO) log(u.callsign + " cannot reach that position.", {255, 200, 120, 255});
    }
  };
  if (o.formation == Formation::None || ids.size() == 1) {
    for (size_t i = 0; i < ids.size(); ++i) moveAlone(i);
    return;
  }

  // The route is planned for the least capable vehicle (one that can't swim, then the
  // slowest); every member follows it, offset to its formation slot.
  int lead = ids.front();
  float speedCap = 1e9f;
  for (int id : ids) {
    const VehicleType& a = units[id].vt();
    const VehicleType& b = units[lead].vt();
    if ((!a.amphibious && b.amphibious) || (a.amphibious == b.amphibious && a.speed < b.speed)) lead = id;
    speedCap = std::min(speedCap, a.speed);
  }
  const VehicleType& leadVt = units[lead].vt();
  Vector2 center{0, 0};
  for (int id : ids) center += units[id].pos;
  center = center / static_cast<float>(ids.size());
  if (!terrain.passable(center, leadVt)) center = units[lead].pos;
  std::vector<Vector2> route = terrain.findPath(center, o.dest, leadVt, bias);
  if (route.empty()) {
    for (size_t i = 0; i < ids.size(); ++i) moveAlone(i);
    return;
  }
  float finalFacing = 0;
  std::vector<Vector2> offs = formationOffsets(ids, o.dest, o.formation, &finalFacing);

  const int group = static_cast<int>(formations.size());
  formations.push_back({ids, speedCap});
  for (size_t i = 0; i < ids.size(); ++i) {
    Unit& u = units[ids[i]];
    const VehicleType& vt = u.vt();
    Vector2 from = u.pos;
    for (size_t j = 0; j < route.size(); ++j) {
      Vector2 prev = j == 0 ? center : route[j - 1];
      float h = j + 1 == route.size() ? finalFacing : angleTo(prev, route[j]);
      Vector2 t = localPoint(route[j], h, offs[i].x, offs[i].y);
      if (!terrain.passable(t, vt)) t = route[j];
      std::vector<Vector2> leg = terrain.findPath(from, t, vt, bias);
      for (const Vector2& p : leg) {
        u.path.push_back(p);
        u.pathLeg.push_back(static_cast<int>(j));
      }
      if (!leg.empty()) from = leg.back();
    }
    u.formation = group;
    u.assaultPoint = u.path.empty() ? o.dest : u.path.back();
    if (u.path.empty()) u.mode = MoveMode::Hold;
  }
}

void Game::orderTarget(const std::vector<int>& ids, int enemy) {
  for (int id : ids) {
    units[id].forcedTarget = enemy;
    units[id].retarget = 0;
  }
}

void Game::orderStop(const std::vector<int>& ids) {
  for (int id : ids) units[id].forcedTarget = -1;
  issueOrder(ids, {0, 0}, MoveMode::Hold, Formation::None);
}

void Game::toggleHoldFire(const std::vector<int>& ids) {
  if (ids.empty()) return;
  bool hold = !units[ids.front()].holdFire;
  for (int id : ids) units[id].holdFire = hold;
  log(hold ? "Selected units: HOLD FIRE." : "Selected units: weapons free.", RAYWHITE);
}

void Game::popSmoke(const std::vector<int>& ids) {
  for (int id : ids) {
    if (units[id].alive) deploySmoke(units[id]);
  }
}

// ---------------------------------------------------------------------------
// Effects plumbing
// ---------------------------------------------------------------------------

void Game::log(const std::string& text, Color color) {
  messages.push_back({text, color, elapsed});
  if (messages.size() > 60) messages.erase(messages.begin());
}

void Game::sound(SoundKind k, Vector2 p) {
  if (sounds.size() < 32) sounds.push_back({k, p});
}

void Game::emit(Vector2 pos, Vector2 vel, float life, float size, float growth, Color c, float drag) {
  if (particles.size() >= kMaxParticles) return;
  particles.push_back({pos, vel, life, life, size, growth, drag, c});
}

void Game::explosion(Vector2 p, float scale) {
  emit(p, {0, 0}, 0.25f, 10 * scale, 60 * scale, {255, 245, 200, 255});
  for (int i = 0; i < static_cast<int>(14 * scale); ++i) {
    emit(p, fromAngle(fxRng().uniform(0, 2 * PI), fxRng().uniform(10, 50) * scale), fxRng().uniform(0.4f, 0.9f),
         fxRng().uniform(4, 8) * scale, 6,
         {255, static_cast<unsigned char>(fxRng().range(90, 190)), 30, 230});
  }
  for (int i = 0; i < static_cast<int>(10 * scale); ++i) {
    emit(p, fromAngle(fxRng().uniform(0, 2 * PI), fxRng().uniform(5, 25) * scale) + wind, fxRng().uniform(2, 4),
         fxRng().uniform(5, 9) * scale, 7, {50, 46, 42, 170}, 0.6f);
  }
  for (int i = 0; i < 8; ++i) {
    emit(p, fromAngle(fxRng().uniform(0, 2 * PI), fxRng().uniform(60, 140)), 0.5f, 1.6f, 0, {30, 26, 22, 255}, 3.0f);
  }
}

void Game::dust(Vector2 p, float scale) {
  for (int i = 0; i < static_cast<int>(6 * scale) + 1; ++i) {
    emit(p, fromAngle(fxRng().uniform(0, 2 * PI), fxRng().uniform(5, 30) * scale), fxRng().uniform(0.8f, 1.6f),
         fxRng().uniform(3, 6) * scale, 12 * scale, {130, 112, 82, 170}, 1.2f);
  }
}

void Game::updateParticles(float dt) {
  for (Particle& p : particles) {
    p.life -= dt;
    p.pos += p.vel * dt;
    p.vel = p.vel * std::max(0.0f, 1.0f - p.drag * dt);
    p.size += p.growth * dt;
  }
  particles.erase(std::remove_if(particles.begin(), particles.end(), [](const Particle& p) { return p.life <= 0; }),
                  particles.end());
}
