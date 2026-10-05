// Steel Curtain - a real-time Cold War armoured battle inspired by Flashpoint Campaigns.
//
// You command a NATO task force holding the river line in the Fulda Gap, 1985, against
// successive Soviet echelons. Pause at any time to issue orders.

#include <algorithm>
#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "audio.hpp"
#include "game.hpp"
#include "sprites.hpp"

namespace {

constexpr float kStep = 1.0f / 30.0f;      // fixed simulation step
constexpr float kKmhPerSpeed = 2.5f;       // display conversion (game time is compressed)

enum class Screen { Title, Guide, Playing };
enum class Mode { Normal, ArtyHE, ArtySmoke };

const char* kDifficultyNames[] = {"Easy", "Normal", "Hard"};
const char* kDifficultyText[] = {
    "Fewer Soviet vehicles in each echelon. A good way to learn the controls.",
    "A full motor-rifle regiment with tank support. Hold the bridges.",
    "More vehicles, an extra Operational Manoeuvre Group and more artillery."};

// ---------- small text helpers ----------
void text(const std::string& s, float x, float y, float size, Color c) {
  DrawTextEx(GetFontDefault(), s.c_str(), {x, y}, size, size / 10.0f, c);
}
float textWidth(const std::string& s, float size) {
  return MeasureTextEx(GetFontDefault(), s.c_str(), size, size / 10.0f).x;
}
void textShadow(const std::string& s, float x, float y, float size, Color c) {
  text(s, x + 1.5f, y + 1.5f, size, {0, 0, 0, static_cast<unsigned char>(c.a * 0.8f)});
  text(s, x, y, size, c);
}
void textCentered(const std::string& s, float cx, float y, float size, Color c) {
  textShadow(s, cx - textWidth(s, size) / 2, y, size, c);
}
std::string fmt(const char* f, ...) {
  char buf[512];
  va_list args;
  va_start(args, f);
  std::vsnprintf(buf, sizeof buf, f, args);
  va_end(args);
  return buf;
}
void panel(Rectangle r, unsigned char alpha = 225) {
  DrawRectangleRec(r, {16, 20, 18, alpha});
  DrawRectangleLinesEx(r, 1.0f, {120, 130, 110, 160});
}
void dashedLine(Vector2 a, Vector2 b, float thick, float dash, Color c) {
  float len = Vector2Distance(a, b);
  if (len < 1) return;
  Vector2 dir = (b - a) / len;
  for (float t = 0; t < len; t += dash * 2) {
    DrawLineEx(a + dir * t, a + dir * std::min(len, t + dash), thick, c);
  }
}
void arrow(Vector2 at, float angle, float len, float thick, Color c) {
  Vector2 tip = at + fromAngle(angle, len);
  DrawLineEx(at, tip, thick, c);
  DrawLineEx(tip, tip + fromAngle(angle + 2.6f, len * 0.35f), thick, c);
  DrawLineEx(tip, tip + fromAngle(angle - 2.6f, len * 0.35f), thick, c);
}
std::string clockText(float seconds) {
  int s = static_cast<int>(seconds);
  return fmt("%02d:%02d", s / 60, s % 60);
}

struct App {
  Game game;
  AudioBank audio;
  RenderTexture2D terrainTex{};
  bool terrainReady = false;
  Camera2D cam{};
  Screen screen = Screen::Title;
  Screen guideReturn = Screen::Title;
  Mode mode = Mode::Normal;
  std::vector<int> selection;
  bool paused = false;
  int timeScale = 1;
  bool showCounters = false, showGrid = true, showHelp = false, showRanges = true;
  bool dragging = false;
  Vector2 dragStart{};
  double lastClickTime = 0;
  int lastClickUnit = -1;
  int difficulty = 1;
  uint32_t seed = 1;
  float accumulator = 0;
  float guideTime = 0;
  MoveMode orderMode = MoveMode::Move;
  Formation formation = Formation::None;

  static constexpr MoveMode kOrderModes[] = {MoveMode::Move, MoveMode::Quick, MoveMode::Deliberate, MoveMode::Assault};
  static constexpr Formation kFormations[] = {Formation::None, Formation::Column, Formation::Line,
                                              Formation::Wedge, Formation::EchelonLeft, Formation::EchelonRight};
  static constexpr const char* kOrderKeys[] = {"Move", "Quick", "Deliberate", "Assault"};
  static constexpr const char* kFormationKeys[] = {"None", "Column", "Line", "Wedge", "Ech. L", "Ech. R"};

  Rectangle orderButton(int i) const { return {96.0f + i * 112.0f, 44, 106, 26}; }
  Rectangle formationButton(int i) const { return {96.0f + i * 82.0f, 76, 76, 26}; }
  Rectangle executeButton() const {
    return {static_cast<float>(GetScreenWidth()) - 250, static_cast<float>(GetScreenHeight()) - 66, 240, 56};
  }

  // Modifier keys pick the order type for a single right-click.
  MoveMode effectiveMode() const {
    if (IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT)) return MoveMode::Quick;
    if (IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL)) return MoveMode::Deliberate;
    if (IsKeyDown(KEY_LEFT_ALT) || IsKeyDown(KEY_RIGHT_ALT)) return MoveMode::Assault;
    return orderMode;
  }

  void advance() {  // the Enter key / Execute button
    if (game.phase == Phase::Deploy) game.beginBattle();
    else if (game.phase == Phase::Orders) {
      game.executeTurn();
      paused = false;
    }
  }

  void newMap() {
    seed = static_cast<uint32_t>(std::chrono::steady_clock::now().time_since_epoch().count());
    loadBattle();
  }

  void loadBattle() {
    game.start(difficulty, seed);
    if (terrainReady) UnloadRenderTexture(terrainTex);
    terrainTex = LoadRenderTexture(static_cast<int>(game.terrain.width()), static_cast<int>(game.terrain.height()));
    SetTextureFilter(terrainTex.texture, TEXTURE_FILTER_BILINEAR);
    game.terrain.render(terrainTex);
    terrainReady = true;
    selection.clear();
    mode = Mode::Normal;
    paused = false;
    timeScale = 1;
    accumulator = 0;
    Vector2 c{0, 0};
    for (const Objective& o : game.objectives) c += o.pos;
    cam.target = c / static_cast<float>(game.objectives.size());
    cam.zoom = std::max(minZoom(), 0.8f);
    cam.rotation = 0;
  }

  float minZoom() const {
    return std::min(GetScreenWidth() / game.terrain.width(), GetScreenHeight() / game.terrain.height()) * 0.95f;
  }

  Vector2 mouseWorld() const { return GetScreenToWorld2D(GetMousePosition(), cam); }
  float drawScale() const { return std::max(1.0f, 0.55f / cam.zoom); }

  int unitAt(Vector2 world, bool friendly) const {
    int best = -1;
    float bestD = std::max(18.0f, 14.0f / cam.zoom) * (showCounters ? 1.3f : 1.0f);
    for (const Unit& u : game.units) {
      if (!u.alive || (u.side == Side::NATO) != friendly || !game.visibleToPlayer(u)) continue;
      float d = Vector2Distance(u.pos, world);
      if (d < bestD) bestD = d, best = u.id;
    }
    return best;
  }

  void selectPlatoon(int p) {
    if (p < 0 || p >= static_cast<int>(game.platoons.size()) || game.platoons[p].side != Side::NATO) return;
    selection.clear();
    for (int id : game.platoons[p].units) {
      if (game.units[id].alive) selection.push_back(id);
    }
  }

  void pruneSelection() {
    selection.erase(std::remove_if(selection.begin(), selection.end(), [&](int id) { return !game.units[id].alive; }),
                    selection.end());
  }

  // ------------------------------------------------------------------ input
  void updateCamera(float dt) {
    float pan = 900.0f / cam.zoom * dt;
    if (IsKeyDown(KEY_W) || IsKeyDown(KEY_UP)) cam.target.y -= pan;
    if (IsKeyDown(KEY_S) || IsKeyDown(KEY_DOWN)) cam.target.y += pan;
    if (IsKeyDown(KEY_A) || IsKeyDown(KEY_LEFT)) cam.target.x -= pan;
    if (IsKeyDown(KEY_D) || IsKeyDown(KEY_RIGHT)) cam.target.x += pan;
    if (IsMouseButtonDown(MOUSE_BUTTON_MIDDLE)) cam.target -= GetMouseDelta() / cam.zoom;
    float wheel = GetMouseWheelMove();
    if (wheel != 0) {
      Vector2 before = mouseWorld();
      cam.zoom = std::clamp(cam.zoom * (1.0f + wheel * 0.12f), minZoom(), 3.0f);
      Vector2 after = mouseWorld();
      cam.target += before - after;
    }
    if (IsKeyPressed(KEY_HOME)) {
      cam.zoom = minZoom();
      cam.target = {game.terrain.width() / 2, game.terrain.height() / 2};
    }
    cam.offset = {GetScreenWidth() / 2.0f, GetScreenHeight() / 2.0f};
    cam.zoom = std::max(cam.zoom, minZoom());
    cam.target.x = std::clamp(cam.target.x, 0.0f, game.terrain.width());
    cam.target.y = std::clamp(cam.target.y, 0.0f, game.terrain.height());
  }

  bool mouseOverUi() const {
    Vector2 m = GetMousePosition();
    if (m.y < 36) return true;
    if (CheckCollisionPointRec(m, selectionPanelRect())) return true;
    if (CheckCollisionPointRec(m, rosterRect())) return true;
    if (CheckCollisionPointRec(m, executeButton())) return true;
    if (m.x < 96 + 6 * 82 && m.y < 106) return true;  // order and formation toolbar
    return false;
  }

  Rectangle selectionPanelRect() const {
    return {10, static_cast<float>(GetScreenHeight()) - 230, 380, 220};
  }
  Rectangle rosterRect() const {
    return {static_cast<float>(GetScreenWidth()) - 250, 46, 240, 24.0f + 40.0f * natoPlatoonCount() + 70};
  }
  int natoPlatoonCount() const {
    int n = 0;
    for (const Platoon& p : game.platoons) n += p.side == Side::NATO;
    return n;
  }

  void handlePlayingInput() {
    const bool over = game.phase == Phase::Over;
    if (IsKeyPressed(KEY_F1)) showHelp = !showHelp;
    if (IsKeyPressed(KEY_V)) {
      guideReturn = Screen::Playing;
      screen = Screen::Guide;
      return;
    }
    if (IsKeyPressed(KEY_C)) showCounters = !showCounters;
    if (IsKeyPressed(KEY_G)) showGrid = !showGrid;
    if (IsKeyPressed(KEY_L)) showRanges = !showRanges;
    if (over) {
      if (IsKeyPressed(KEY_N)) newMap();
      if (IsKeyPressed(KEY_ESCAPE)) screen = Screen::Title;
      return;
    }
    if (IsKeyPressed(KEY_SPACE) && game.phase == Phase::Battle) paused = !paused;
    if (IsKeyPressed(KEY_F)) timeScale = timeScale == 1 ? 2 : timeScale == 2 ? 4 : 1;
    if (IsKeyPressed(KEY_T)) game.turnLength = game.turnLength >= 90 ? 30 : game.turnLength + 30;
    if (IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_KP_ENTER)) advance();
    if (IsKeyPressed(KEY_TAB)) {
      int i = 0;
      while (kOrderModes[i] != orderMode) ++i;
      orderMode = kOrderModes[(i + 1) % 4];
    }
    if (IsKeyPressed(KEY_O)) {
      int i = 0;
      while (kFormations[i] != formation) ++i;
      formation = kFormations[(i + 1) % 6];
    }
    if (IsKeyPressed(KEY_ESCAPE)) {
      if (mode != Mode::Normal) mode = Mode::Normal;
      else if (showHelp) showHelp = false;
      else selection.clear();
    }
    if (IsKeyPressed(KEY_E)) {
      selection.clear();
      for (const Unit& u : game.units) {
        if (u.alive && u.side == Side::NATO) selection.push_back(u.id);
      }
    }
    for (int k = 0; k < 9; ++k) {
      if (IsKeyPressed(KEY_ONE + k)) {
        int n = 0;
        for (size_t p = 0; p < game.platoons.size(); ++p) {
          if (game.platoons[p].side != Side::NATO) continue;
          if (n++ == k) selectPlatoon(static_cast<int>(p));
        }
      }
    }
    if (IsKeyPressed(KEY_H)) game.toggleHoldFire(selection);
    if (IsKeyPressed(KEY_X)) game.orderStop(selection);
    if (IsKeyPressed(KEY_Z)) game.popSmoke(selection);
    if (IsKeyPressed(KEY_Q)) mode = mode == Mode::ArtyHE ? Mode::Normal : Mode::ArtyHE;
    if (IsKeyPressed(KEY_R)) mode = mode == Mode::ArtySmoke ? Mode::Normal : Mode::ArtySmoke;

    // Toolbar and button clicks.
    Vector2 m = GetMousePosition();
    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
      for (int i = 0; i < 4; ++i) {
        if (CheckCollisionPointRec(m, orderButton(i))) orderMode = kOrderModes[i];
      }
      for (int i = 0; i < 6; ++i) {
        if (CheckCollisionPointRec(m, formationButton(i))) formation = kFormations[i];
      }
      if (CheckCollisionPointRec(m, executeButton())) advance();
    }
    // Roster clicks.
    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && CheckCollisionPointRec(m, rosterRect())) {
      int row = static_cast<int>((m.y - rosterRect().y - 30) / 40);
      int n = 0;
      for (size_t p = 0; p < game.platoons.size(); ++p) {
        if (game.platoons[p].side != Side::NATO) continue;
        if (n++ == row) selectPlatoon(static_cast<int>(p));
      }
      return;
    }
    if (mouseOverUi() && !dragging) return;

    Vector2 w = mouseWorld();
    if (mode != Mode::Normal) {
      if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
        if (!game.callArtillery(w, mode == Mode::ArtySmoke)) {
          // Not available right now; the HUD explains why.
        }
        mode = Mode::Normal;
      }
      if (IsMouseButtonPressed(MOUSE_BUTTON_RIGHT)) mode = Mode::Normal;
      return;
    }

    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
      dragging = true;
      dragStart = GetMousePosition();
    }
    if (dragging && IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) {
      dragging = false;
      bool additive = IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT);
      Vector2 end = GetMousePosition();
      if (Vector2Distance(end, dragStart) < 6) {
        int id = unitAt(w, true);
        double now = GetTime();
        if (id >= 0 && id == lastClickUnit && now - lastClickTime < 0.35) {
          selectPlatoon(game.units[id].platoon);  // double-click selects the platoon
        } else if (id >= 0) {
          auto it = std::find(selection.begin(), selection.end(), id);
          if (!additive) selection = {id};
          else if (it == selection.end()) selection.push_back(id);
          else selection.erase(it);
        } else if (!additive) {
          selection.clear();
        }
        lastClickUnit = id;
        lastClickTime = now;
      } else {
        Vector2 a = GetScreenToWorld2D(dragStart, cam), b = GetScreenToWorld2D(end, cam);
        Rectangle box{std::min(a.x, b.x), std::min(a.y, b.y), fabsf(a.x - b.x), fabsf(a.y - b.y)};
        if (!additive) selection.clear();
        for (const Unit& u : game.units) {
          if (u.alive && u.side == Side::NATO && CheckCollisionPointRec(u.pos, box) &&
              std::find(selection.begin(), selection.end(), u.id) == selection.end()) {
            selection.push_back(u.id);
          }
        }
      }
    }

    if (IsMouseButtonPressed(MOUSE_BUTTON_RIGHT) && !selection.empty()) {
      int enemy = unitAt(w, false);
      if (enemy >= 0) {
        game.orderTarget(selection, enemy);
      } else if (!game.issueOrder(selection, w, effectiveMode(), formation)) {
        game.messages.push_back({"Orders can only be given in the orders phase.", {255, 200, 120, 255}, game.elapsed});
      }
    }
  }

  void stepSimulation(float frameDt) {
    if (game.phase == Phase::Orders) {
      game.update(0.0f);  // clock frozen while planning
      return;
    }
    if (paused) return;
    if (game.phase == Phase::Over) {  // let fires and smoke keep drifting behind the results
      game.update(std::min(frameDt, 0.1f));
      return;
    }
    accumulator += std::min(frameDt, 0.1f) * timeScale;
    int steps = 0;
    while (accumulator >= kStep && steps < 12) {
      game.update(kStep);
      accumulator -= kStep;
      ++steps;
    }
  }

  void playSounds() {
    Vector2 center = cam.target;
    float hearing = 1400.0f / std::max(cam.zoom, 0.3f);
    for (const SoundEvent& e : game.sounds) {
      float d = Vector2Distance(e.pos, center);
      float vol = std::clamp(1.0f - d / hearing, 0.0f, 1.0f);
      float base = e.kind == SoundKind::Autocannon ? 0.35f : e.kind == SoundKind::Missile ? 0.5f : 0.8f;
      float pan = std::clamp((e.pos.x - center.x) / (hearing * 0.5f), -1.0f, 1.0f);
      audio.play(e.kind, vol * vol * base, pan);
    }
    game.sounds.clear();
  }

  // ------------------------------------------------------------------ world drawing
  void drawWorld() {
    BeginMode2D(cam);
    DrawTextureRec(terrainTex.texture,
                   {0, 0, static_cast<float>(terrainTex.texture.width), -static_cast<float>(terrainTex.texture.height)},
                   {0, 0}, WHITE);
    const float px = 1.0f / cam.zoom;  // one screen pixel in world units

    if (showGrid) {
      for (float x = 500; x < game.terrain.width(); x += 500) {
        DrawLineEx({x, 0}, {x, game.terrain.height()}, px, {20, 20, 20, 60});
      }
      for (float y = 500; y < game.terrain.height(); y += 500) {
        DrawLineEx({0, y}, {game.terrain.width(), y}, px, {20, 20, 20, 60});
      }
    }

    for (const Objective& o : game.objectives) {
      Color c = sideColor(o.owner);
      DrawCircleV(o.pos, o.radius, {c.r, c.g, c.b, 28});
      DrawRing(o.pos, o.radius - 2.5f * px, o.radius, 0, 360, 64, {c.r, c.g, c.b, 170});
    }

    // Wrecks first, so living vehicles drive over them.
    for (const Unit& u : game.units) {
      if (u.alive || !game.visibleToPlayer(u)) continue;
      drawVehicle(u.vt(), u.pos, u.heading, u.turret + 0.4f, drawScale(), u.camo, true);
    }

    // Orders being carried out by the selection, coloured by order type.
    for (int id : selection) {
      const Unit& u = game.units[id];
      Color c = moveModeColor(u.mode);
      c.a = 160;
      Vector2 prev = u.pos;
      for (const Vector2& p : u.path) {
        DrawLineEx(prev, p, 1.5f * px, c);
        prev = p;
      }
      if (!u.path.empty()) DrawCircleV(u.path.back(), 3.5f * px, c);
      if (u.target >= 0) DrawLineEx(u.pos, game.units[u.target].pos, 1.2f * px, {255, 90, 70, 120});
    }

    // Orders still on their way down the chain of command: dashed lines to each unit's slot.
    for (const Order& o : game.pendingOrders) {
      Color c = moveModeColor(o.mode);
      for (size_t i = 0; i < o.units.size(); ++i) {
        const Unit& u = game.units[o.units[i]];
        if (!u.alive) continue;
        if (o.mode == MoveMode::Hold) {
          DrawRing(u.pos, 14 * px, 16.5f * px, 0, 360, 24, c);
          continue;
        }
        dashedLine(u.pos, o.slots[i], 1.8f * px, 10 * px, {c.r, c.g, c.b, 210});
        DrawRing(o.slots[i], 5 * px, 7 * px, 0, 360, 20, c);
      }
      if (o.mode != MoveMode::Hold && o.formation != Formation::None) arrow(o.dest, o.facing, 40 * px, 2 * px, c);
    }

    // Preview of the order a right-click would give.
    if (game.canIssueOrders() && !selection.empty() && mode == Mode::Normal && !mouseOverUi()) {
      Vector2 w = mouseWorld();
      if (unitAt(w, false) < 0) {
        MoveMode m = effectiveMode();
        Formation f = selection.size() > 1 ? formation : Formation::None;
        float facing = 0;
        std::vector<Vector2> slots = game.formationSlots(selection, w, f, &facing);
        Color c = moveModeColor(m);
        for (size_t i = 0; i < slots.size(); ++i) {
          DrawRing(slots[i], 6 * px, 8 * px, 0, 360, 20, {c.r, c.g, c.b, 150});
          if (f != Formation::None && i + 1 < slots.size()) {
            DrawLineEx(slots[i], slots[i + 1], 1.0f * px, {c.r, c.g, c.b, 60});
          }
        }
        if (f != Formation::None) arrow(w, facing, 34 * px, 1.6f * px, {c.r, c.g, c.b, 170});
      }
    }

    if (showRanges && selection.size() == 1) {
      const Unit& u = game.units[selection[0]];
      const VehicleType& vt = u.vt();
      DrawCircleLinesV(u.pos, vt.optics, {255, 255, 255, 50});
      if (vt.gun.valid()) DrawRing(u.pos, vt.gun.range - px, vt.gun.range + px, 0, 360, 96, {255, 230, 120, 110});
      if (vt.atgm.valid()) DrawRing(u.pos, vt.atgm.range - px, vt.atgm.range + px, 0, 360, 96, {255, 150, 80, 110});
    }

    // Units.
    for (const Unit& u : game.units) {
      if (!u.alive || !game.visibleToPlayer(u)) continue;
      bool selected = std::find(selection.begin(), selection.end(), u.id) != selection.end();
      float s = drawScale();
      if (showCounters) {
        float size = 15.0f * px;
        if (selected) DrawRectangleLinesEx({u.pos.x - size, u.pos.y - size * 0.75f, size * 2, size * 1.5f}, 2 * px, {120, 255, 140, 255});
        drawCounter(u.vt(), u.pos, size, 255);
      } else {
        float r = u.vt().length * 0.6f * s;
        Color sc = sideColor(u.side);
        DrawRing(u.pos, r - 1.2f * px, r + 0.4f * px, 0, 360, 32, {sc.r, sc.g, sc.b, 150});
        if (selected) DrawRing(u.pos, r + 2 * px, r + 4 * px, 0, 360, 32, {120, 255, 140, 230});
        drawVehicle(u.vt(), u.pos, u.heading, u.turret, s, u.camo, false);
      }
    }

    // Projectiles.
    for (const Shot& s : game.shots) {
      Vector2 dir = Vector2Normalize(s.to - s.from);
      Vector2 head = s.from + dir * s.progress;
      Vector2 tail = s.from + dir * std::max(0.0f, s.progress - (s.light ? 25.0f : 45.0f));
      Color c = s.light ? Color{255, 210, 110, 230} : Color{255, 246, 200, 255};
      if (s.side == Side::WP && s.light) c = {140, 255, 140, 230};  // Soviet green tracer
      DrawLineEx(tail, head, (s.light ? 1.2f : 2.2f) * std::max(1.0f, px), c);
    }
    for (const Missile& m : game.missiles) {
      DrawCircleV(m.pos, 2.4f * std::max(1.0f, px * 0.8f), {60, 60, 50, 255});
      DrawCircleV(localPoint(m.pos, m.heading, -3, 0), 1.8f * std::max(1.0f, px * 0.8f), {255, 220, 120, 255});
    }

    // Particles and smoke.
    for (const Particle& p : game.particles) {
      float a = std::clamp(p.life / p.maxLife, 0.0f, 1.0f);
      Color c = p.color;
      c.a = static_cast<unsigned char>(c.a * a);
      DrawCircleV(p.pos, std::max(0.5f, p.size), c);
    }
    for (const SmokeCloud& s : game.smokes) {
      float r = s.currentRadius();
      float fade = std::min(1.0f, s.life / 6.0f);
      uint32_t h = s.seed | 1u;
      for (int i = 0; i < 8; ++i) {
        h ^= h << 13, h ^= h >> 17, h ^= h << 5;
        float a = (h & 0xffff) / 65535.0f * 2 * PI;
        float d = ((h >> 16) & 0xff) / 255.0f * r * 0.55f;
        Vector2 p = s.pos + fromAngle(a, d) + game.wind * ((s.maxLife - s.life) * 0.4f);
        DrawCircleV(p, r * 0.55f, {222, 222, 218, static_cast<unsigned char>(95 * fade)});
      }
    }

    if (mode != Mode::Normal) {
      Vector2 w = mouseWorld();
      float r = mode == Mode::ArtyHE ? 70.0f : 55.0f;
      Color c = mode == Mode::ArtyHE ? Color{255, 120, 60, 255} : Color{230, 230, 230, 255};
      DrawRing(w, r - 1.5f * px, r + 1.5f * px, 0, 360, 48, c);
      DrawLineEx(w - Vector2{r * 0.3f, 0}, w + Vector2{r * 0.3f, 0}, 1.5f * px, c);
      DrawLineEx(w - Vector2{0, r * 0.3f}, w + Vector2{0, r * 0.3f}, 1.5f * px, c);
    }
    EndMode2D();

    // Screen-space labels stay crisp at any zoom.
    for (const Town& t : game.terrain.towns()) {
      Vector2 p = GetWorldToScreen2D(t.center + Vector2{0, t.radius + 12}, cam);
      if (t.objective) continue;
      textCentered(t.name, p.x, p.y, 14, {235, 230, 210, 220});
    }
    for (size_t i = 0; i < game.objectives.size(); ++i) {
      const Objective& o = game.objectives[i];
      Vector2 p = GetWorldToScreen2D(o.pos + Vector2{0, -o.radius - 6}, cam);
      Color c = o.owner == Side::NATO ? Color{150, 200, 255, 255} : Color{255, 140, 120, 255};
      textCentered(fmt("OBJ %c  %s", 'A' + static_cast<int>(i), o.name.c_str()), p.x, p.y - 20, 18, c);
    }
    for (const Order& o : game.pendingOrders) {
      if (o.mode == MoveMode::Hold || o.units.empty()) continue;
      Vector2 p = GetWorldToScreen2D(o.dest, cam);
      std::string label = moveModeName(o.mode);
      if (o.formation != Formation::None) label += std::string(" - ") + formationName(o.formation);
      label += fmt("  (in %ds)", static_cast<int>(std::ceil(o.delay)));
      textCentered(label, p.x, p.y + 14, 13, moveModeColor(o.mode));
    }
    if (cam.zoom > 0.9f && !showCounters) {
      for (const Unit& u : game.units) {
        if (!u.alive || u.side != Side::NATO) continue;
        Vector2 p = GetWorldToScreen2D(u.pos + Vector2{0, u.vt().length * 0.65f * drawScale()}, cam);
        textCentered(u.callsign, p.x, p.y + 2, 11, {220, 235, 255, 200});
      }
    }
    // Status bars above selected units.
    for (int id : selection) {
      const Unit& u = game.units[id];
      Vector2 p = GetWorldToScreen2D(u.pos - Vector2{0, u.vt().length * 0.7f * drawScale()}, cam);
      DrawRectangle(static_cast<int>(p.x - 16), static_cast<int>(p.y - 8), 32, 4, {0, 0, 0, 160});
      DrawRectangle(static_cast<int>(p.x - 16), static_cast<int>(p.y - 8), static_cast<int>(32 * u.suppression / 100), 4, {255, 200, 60, 230});
      if (u.damaged) DrawRectangle(static_cast<int>(p.x - 16), static_cast<int>(p.y - 13), 6, 4, {255, 90, 60, 255});
      if (u.holdFire) text("HF", p.x + 18, p.y - 12, 10, {255, 255, 255, 220});
    }
    if (dragging) {
      Vector2 m = GetMousePosition();
      Rectangle r{std::min(m.x, dragStart.x), std::min(m.y, dragStart.y), fabsf(m.x - dragStart.x), fabsf(m.y - dragStart.y)};
      DrawRectangleRec(r, {120, 255, 140, 30});
      DrawRectangleLinesEx(r, 1, {120, 255, 140, 200});
    }
  }

  // ------------------------------------------------------------------ HUD
  void drawHud() {
    const float sw = static_cast<float>(GetScreenWidth()), sh = static_cast<float>(GetScreenHeight());
    DrawRectangle(0, 0, static_cast<int>(sw), 36, {12, 16, 14, 225});
    textShadow("STEEL CURTAIN", 12, 9, 20, {230, 220, 180, 255});
    text("Fulda Gap, 1985", 190, 13, 14, {170, 170, 150, 255});

    std::string status;
    Color statusColor = RAYWHITE;
    switch (game.phase) {
      case Phase::Deploy:
        status = "DEPLOYMENT  -  set up freely, then ENTER";
        statusColor = {255, 220, 120, 255};
        break;
      case Phase::Orders:
        status = fmt("TURN %d  -  ORDERS PHASE  -  H+%s", game.turn, clockText(game.clock).c_str());
        statusColor = {255, 220, 120, 255};
        break;
      case Phase::Battle:
        status = fmt("TURN %d  -  EXECUTING %s/%s  -  H+%s  x%d", game.turn, clockText(game.turnClock).c_str(),
                     clockText(game.turnLength).c_str(), clockText(game.clock).c_str(), timeScale);
        if (paused) status += "   [PAUSED]";
        break;
      case Phase::Over: status = "BATTLE OVER"; break;
    }
    float x = sw - 12;
    std::string arty = fmt("Arty  HE %d  Smoke %d%s", game.artyHE, game.artySmoke, game.artyBusy ? " (firing)" : "");
    x -= textWidth(arty, 16);
    text(arty, x, 11, 16, {200, 220, 255, 255});
    std::string losses = fmt("Losses  NATO %d  |  WP %d", game.lossCount[0], game.lossCount[1]);
    x -= textWidth(losses, 16) + 24;
    text(losses, x, 11, 16, RAYWHITE);
    for (int i = static_cast<int>(game.objectives.size()) - 1; i >= 0; --i) {
      x -= 30;
      Color c = sideColor(game.objectives[i].owner);
      DrawRectangle(static_cast<int>(x), 9, 24, 18, c);
      text(std::string(1, static_cast<char>('A' + i)), x + 7, 11, 16, RAYWHITE);
    }

    // Centre the status between the title and the right-hand readouts, shrinking it if needed.
    const float left = 300, right = x - 16;
    float size = 20;
    while (size > 13 && textWidth(status, size) > right - left) size -= 1;
    float cx = std::clamp(sw / 2, left + textWidth(status, size) / 2, right - textWidth(status, size) / 2);
    textCentered(status, cx, 18 - size / 2, size, statusColor);

    if (mode != Mode::Normal) {
      std::string m = mode == Mode::ArtyHE ? "Left-click to call a 155mm HE fire mission  (right-click to cancel)"
                                           : "Left-click to lay an artillery smoke screen  (right-click to cancel)";
      if (game.phase != Phase::Orders) m = "Fire missions are planned in the orders phase";
      else if (game.artyBusy) m = "The battery is still firing the last mission";
      else if ((mode == Mode::ArtyHE ? game.artyHE : game.artySmoke) <= 0) m = "No fire missions of that type left";
      textCentered(m, sw / 2, 46, 18, {255, 200, 140, 255});
    }

    drawToolbar();
    drawExecuteButton();
    drawRoster();
    drawSelectionPanel();
    drawMessages(sh);
    if (showHelp) drawHelp();
    if (game.phase == Phase::Over) drawResults();
  }

  void drawToolbar() {
    const bool active = game.canIssueOrders();
    DrawRectangle(0, 38, 96 + 6 * 82 + 4, 70, {12, 16, 14, 190});
    text("ORDER", 12, 50, 15, {230, 220, 180, 255});
    text("FORMATION", 12, 82, 13, {230, 220, 180, 255});
    Vector2 m = GetMousePosition();
    for (int i = 0; i < 4; ++i) {
      Rectangle r = orderButton(i);
      bool on = kOrderModes[i] == effectiveMode();
      Color c = moveModeColor(kOrderModes[i]);
      DrawRectangleRec(r, on ? Color{c.r, c.g, c.b, 90} : Color{30, 36, 32, 230});
      DrawRectangleLinesEx(r, on ? 2.0f : 1.0f, CheckCollisionPointRec(m, r) ? WHITE : Color{c.r, c.g, c.b, 200});
      textCentered(kOrderKeys[i], r.x + r.width / 2, r.y + 5, 16, active ? RAYWHITE : Color{150, 150, 150, 255});
    }
    for (int i = 0; i < 6; ++i) {
      Rectangle r = formationButton(i);
      bool on = kFormations[i] == formation;
      DrawRectangleRec(r, on ? Color{200, 190, 140, 90} : Color{30, 36, 32, 230});
      DrawRectangleLinesEx(r, on ? 2.0f : 1.0f, CheckCollisionPointRec(m, r) ? WHITE : Color{200, 190, 140, 200});
      textCentered(kFormationKeys[i], r.x + r.width / 2, r.y + 6, 14, RAYWHITE);
    }
  }

  void drawExecuteButton() {
    if (game.phase == Phase::Over) return;
    Rectangle r = executeButton();
    Vector2 m = GetMousePosition();
    bool hover = CheckCollisionPointRec(m, r);
    if (game.phase == Phase::Battle) {
      panel(r);
      float f = std::clamp(game.turnClock / game.turnLength, 0.0f, 1.0f);
      DrawRectangle(static_cast<int>(r.x + 8), static_cast<int>(r.y + 36), static_cast<int>((r.width - 16) * f), 10, {120, 190, 255, 220});
      textCentered(fmt("Turn %d executing", game.turn), r.x + r.width / 2, r.y + 8, 18, RAYWHITE);
      return;
    }
    Color base = game.phase == Phase::Deploy ? Color{150, 120, 40, 255} : Color{60, 120, 60, 255};
    DrawRectangleRec(r, hover ? shadeColor(base, 30) : base);
    DrawRectangleLinesEx(r, 2, {230, 220, 180, 255});
    std::string label = game.phase == Phase::Deploy ? "BEGIN BATTLE" : fmt("EXECUTE TURN %d", game.turn);
    textCentered(label, r.x + r.width / 2, r.y + 8, 22, RAYWHITE);
    textCentered(fmt("Enter    turn length %d s (T)", static_cast<int>(game.turnLength)), r.x + r.width / 2, r.y + 34, 13,
                 {230, 230, 210, 255});
  }

  void drawRoster() {
    Rectangle r = rosterRect();
    panel(r);
    text("TASK FORCE (1-6)", r.x + 10, r.y + 8, 15, {230, 220, 180, 255});
    float y = r.y + 30;
    int n = 0;
    for (const Platoon& p : game.platoons) {
      if (p.side != Side::NATO) continue;
      ++n;
      bool any = false;
      for (int id : p.units) any |= std::find(selection.begin(), selection.end(), id) != selection.end();
      if (any) DrawRectangle(static_cast<int>(r.x + 2), static_cast<int>(y - 2), static_cast<int>(r.width - 4), 38, {80, 120, 80, 90});
      text(fmt("%d  %s", n, p.name.c_str()), r.x + 10, y, 15, RAYWHITE);
      const VehicleType& vt = game.units[p.units.front()].vt();
      text(vt.name, r.x + 110, y + 1, 12, {170, 170, 160, 255});
      float px = r.x + 26;
      for (int id : p.units) {
        const Unit& u = game.units[id];
        Color c = !u.alive ? Color{90, 90, 90, 255} : u.damaged ? Color{240, 170, 60, 255} : Color{110, 220, 120, 255};
        DrawRectangle(static_cast<int>(px), static_cast<int>(y + 19), 14, 10, c);
        if (u.alive && u.suppression > 50) DrawRectangle(static_cast<int>(px), static_cast<int>(y + 30), 14, 2, {255, 220, 60, 255});
        px += 18;
      }
      y += 40;
    }
    int spotted = 0;
    for (const Unit& u : game.units) spotted += u.alive && u.side == Side::WP && u.spotted[0] > 0;
    text(fmt("Enemy in sight: %d", spotted), r.x + 10, y + 6, 14, {255, 170, 150, 255});
    text(fmt("Enemy destroyed: %d", game.lossCount[1]), r.x + 10, y + 26, 14, {255, 170, 150, 255});
    text("F1 help   V vehicle guide", r.x + 10, y + 46, 12, {160, 160, 150, 255});
  }

  void drawSelectionPanel() {
    pruneSelection();
    Rectangle r = selectionPanelRect();
    panel(r);
    if (selection.empty()) {
      text("No units selected", r.x + 12, r.y + 12, 18, {230, 220, 180, 255});
      const char* hints[] = {"Left-click or drag: select units  (dbl-click: platoon)",
                             "Right-click: order the selected type of move",
                             "  Shift: quick   Ctrl: deliberate   Alt: assault",
                             "Tab: order type   O: formation   X: halt",
                             "Enter: execute the turn   Q/R: artillery",
                             "C: NATO symbols    WASD / wheel: camera"};
      for (int i = 0; i < 6; ++i) text(hints[i], r.x + 12, r.y + 46 + i * 24, 15, {200, 200, 190, 255});
      return;
    }
    if (selection.size() > 1) {
      text(fmt("%zu units selected", selection.size()), r.x + 12, r.y + 12, 18, {230, 220, 180, 255});
      float y = r.y + 42;
      for (size_t i = 0; i < selection.size() && i < 7; ++i) {
        const Unit& u = game.units[selection[i]];
        text(fmt("%-12s %-16s %s%s", u.callsign.c_str(), u.vt().name, moveModeName(u.mode), u.damaged ? " (dmg)" : ""),
             r.x + 12, y, 14, RAYWHITE);
        y += 22;
      }
      if (selection.size() > 7) text(fmt("... and %zu more", selection.size() - 7), r.x + 12, y, 14, {170, 170, 160, 255});
      return;
    }

    const Unit& u = game.units[selection[0]];
    const VehicleType& vt = u.vt();
    DrawRectangle(static_cast<int>(r.x + 10), static_cast<int>(r.y + 10), 96, 96, {50, 62, 44, 255});
    drawVehicle(vt, {r.x + 58, r.y + 58}, -PI / 2, -PI / 2 + (u.turret - u.heading), 2.4f, u.camo, false);
    text(u.callsign, r.x + 116, r.y + 10, 20, {230, 220, 180, 255});
    text(vt.name, r.x + 116, r.y + 34, 16, RAYWHITE);
    text(fmt("%s - %s", vt.nation, vt.role), r.x + 116, r.y + 54, 12, {170, 170, 160, 255});
    std::string state = u.damaged ? "DAMAGED" : "Operational";
    if (u.suppression > 70) state += ", PINNED";
    else if (u.suppression > 35) state += ", suppressed";
    text(state, r.x + 116, r.y + 72, 14, u.damaged ? Color{255, 160, 90, 255} : Color{120, 230, 130, 255});
    std::string orders = u.guiding >= 0 ? "Guiding missile" : moveModeName(u.mode);
    if (u.formation >= 0 && !u.path.empty()) orders += " (formation)";
    if (const Order* po = game.pendingOrderFor(u.id)) {
      orders += fmt(" -> %s in %ds", moveModeName(po->mode), static_cast<int>(std::ceil(po->delay)));
    }
    if (u.holdFire) orders += "  (HOLD FIRE)";
    text(orders + fmt("  %d km/h", static_cast<int>(u.speed * kKmhPerSpeed)), r.x + 116, r.y + 90, 14, RAYWHITE);

    float y = r.y + 116;
    if (vt.gun.valid()) {
      std::string ammo = vt.gun.ammo < 0 ? "plenty" : fmt("%d rds", u.gunAmmo);
      text(fmt("Gun:  %s  (%s, %d m)", vt.gun.name, ammo.c_str(), static_cast<int>(vt.gun.range * kMetersPerUnit)), r.x + 12, y, 14, RAYWHITE);
      y += 20;
    }
    if (vt.atgm.valid()) {
      text(fmt("ATGM: %s  (%d left, %d m)", vt.atgm.name, u.atgmAmmo, static_cast<int>(vt.atgm.range * kMetersPerUnit)), r.x + 12, y, 14, RAYWHITE);
      y += 20;
    }
    text(fmt("Armour: front %d / side %d mm    Optics: %d m", static_cast<int>(vt.armorFront), static_cast<int>(vt.armorSide),
             static_cast<int>(vt.optics * kMetersPerUnit)), r.x + 12, y, 14, {200, 200, 190, 255});
    y += 20;
    text(fmt("Terrain: %s    Kills: %d    Smoke: %d", game.terrain.groundName(u.pos), u.kills, u.smoke), r.x + 12, y, 14,
         {200, 200, 190, 255});
    y += 22;
    DrawRectangle(static_cast<int>(r.x + 12), static_cast<int>(y), 200, 8, {0, 0, 0, 160});
    DrawRectangle(static_cast<int>(r.x + 12), static_cast<int>(y), static_cast<int>(2 * u.suppression), 8, {255, 200, 60, 230});
    text("Suppression", r.x + 220, y - 3, 12, {200, 200, 190, 255});
  }

  void drawMessages(float sh) {
    float y = sh - 30;
    int shown = 0;
    for (auto it = game.messages.rbegin(); it != game.messages.rend() && shown < 7; ++it, ++shown) {
      double age = game.elapsed - it->time;
      if (age > 16) break;
      float a = static_cast<float>(std::clamp(1.0 - (age - 12) / 4, 0.0, 1.0));
      Color c = it->color;
      c.a = static_cast<unsigned char>(255 * a);
      textShadow(it->text, 404, y, 16, c);
      y -= 22;
    }
  }

  void drawHelp() {
    float sw = static_cast<float>(GetScreenWidth()), sh = static_cast<float>(GetScreenHeight());
    Rectangle r{sw / 2 - 340, sh / 2 - 300, 680, 600};
    panel(r, 235);
    textCentered("FIELD MANUAL", sw / 2, r.y + 16, 24, {230, 220, 180, 255});
    const char* lines[] = {
        "Each turn: plan in the ORDERS PHASE, then press Enter to EXECUTE it.",
        "Orders reach units after a few seconds' command delay.",
        "Left-click / drag        Select units (Shift adds, double-click = platoon)",
        "Right-click              Order a move / make a spotted enemy the target",
        "Tab, or Shift/Ctrl/Alt   Move, Quick, Deliberate, Assault (or toolbar)",
        "O                        Formation: column, line, wedge, echelon L/R",
        "1 - 6, E  H  X  Z        Platoon / all; hold fire; halt; smoke grenades",
        "Q  R  T                  155mm HE / smoke mission; turn length",
        "Space  F                 Pause / speed during execution",
        "WASD, arrows, wheel      Pan and zoom; middle-drag pans; Home fits map",
        "C  G  L                  NATO symbols / km grid / range rings",
        "V  F1  Esc               Vehicle guide / this help / cancel",
        "",
        "Quick: fast, prefers roads, shoots badly, easy to see. Deliberate: slow,",
        "halts to shoot, sees further. Assault: presses on under fire and closes in.",
        "Hold the three bridgehead towns (OBJ A-C) until the clock runs out.",
        "Units only see what is in line of sight: hills, woods and towns block it,",
        "and so does smoke. Woods and towns also make units harder to hit.",
        "Front armour is much thicker than side armour: flank the enemy.",
        "Missile carriers must stop and keep sight of the target while guiding.",
        "Suppressed units shoot worse; pinned units barely move. Firing gives",
        "your position away. NATO thermal sights see further than Soviet optics.",
    };
    float y = r.y + 60;
    for (const char* l : lines) {
      text(l, r.x + 28, y, 15, RAYWHITE);
      y += 25;
    }
  }

  void drawResults() {
    float sw = static_cast<float>(GetScreenWidth()), sh = static_cast<float>(GetScreenHeight());
    Rectangle r{sw / 2 - 300, sh / 2 - 190, 600, 380};
    panel(r, 240);
    bool win = game.outcome == Outcome::DecisiveVictory || game.outcome == Outcome::MarginalVictory;
    Color c = win ? Color{140, 200, 255, 255} : game.outcome == Outcome::Draw ? Color{230, 220, 180, 255} : Color{255, 130, 110, 255};
    textCentered(game.outcomeName(), sw / 2, r.y + 24, 30, c);
    float y = r.y + 80;
    auto row = [&](const std::string& label, const std::string& value) {
      text(label, r.x + 60, y, 18, {200, 200, 190, 255});
      text(value, r.x + 360, y, 18, RAYWHITE);
      y += 30;
    };
    row("Victory points (NATO / WP)", fmt("%d / %d", game.vp[0], game.vp[1]));
    row("Vehicles lost (NATO / WP)", fmt("%d / %d", game.lossCount[0], game.lossCount[1]));
    int held = 0;
    for (const Objective& o : game.objectives) held += o.owner == Side::NATO;
    row("Objectives held", fmt("%d of %zu", held, game.objectives.size()));
    row("Battle time", clockText(game.clock));
    const Unit* ace = nullptr;
    for (const Unit& u : game.units) {
      if (u.side == Side::NATO && (!ace || u.kills > ace->kills)) ace = &u;
    }
    if (ace && ace->kills > 0) row("Top crew", fmt("%s (%s), %d kills", ace->callsign.c_str(), ace->vt().name, ace->kills));
    textCentered("N: new battle     V: vehicle guide     Esc: title screen", sw / 2, r.y + r.height - 44, 16, {230, 220, 180, 255});
  }

  // ------------------------------------------------------------------ title and guide
  void drawTitle() {
    float sw = static_cast<float>(GetScreenWidth()), sh = static_cast<float>(GetScreenHeight());
    float scale = std::max(sw / terrainTex.texture.width, sh / terrainTex.texture.height);
    DrawTexturePro(terrainTex.texture,
                   {0, 0, static_cast<float>(terrainTex.texture.width), -static_cast<float>(terrainTex.texture.height)},
                   {0, 0, terrainTex.texture.width * scale, terrainTex.texture.height * scale}, {0, 0}, 0, WHITE);
    DrawRectangle(0, 0, static_cast<int>(sw), static_cast<int>(sh), {8, 10, 8, 170});

    textCentered("STEEL CURTAIN", sw / 2, sh * 0.12f, 76, {232, 222, 182, 255});
    textCentered("FULDA GAP  -  1985", sw / 2, sh * 0.12f + 84, 26, {200, 80, 60, 255});

    // A column of vehicles parading across the title.
    const int parade[] = {kM1Abrams, kLeopard2, kM2Bradley, kM901Itv, kLuchs, kT80B, kBMP2, kBRDM2, kZSU234, kBTR70};
    float px = sw / 2 - 4.5f * 110;
    for (int i = 0; i < 10; ++i) {
      float t = static_cast<float>(GetTime());
      drawVehicle(vehicleType(parade[i]), {px + i * 110, sh * 0.37f}, -PI / 2, -PI / 2 + 0.5f * sinf(t * 0.8f + i), 2.2f,
                  static_cast<uint32_t>(i * 7919 + 13), false);
    }

    Rectangle r{sw / 2 - 380, sh * 0.47f, 760, 300};
    panel(r, 215);
    const char* brief[] = {
        "Situation: at dawn the Soviet 8th Guards Army attacked across the inner German border.",
        "Mission: your combined US / West German task force holds the river line. Deny the three",
        "bridgehead towns until the corps reserve arrives in 12 minutes.",
        "Enemy: a reconnaissance screen, then motor-rifle companies, then T-80 tank companies,",
        "then a second echelon. BMPs, BTRs and BRDMs can swim the river.",
        "Support: one 155mm battery (4 HE and 2 smoke missions). Air support is not available.",
    };
    float y = r.y + 18;
    for (const char* l : brief) {
      text(l, r.x + 22, y, 16, RAYWHITE);
      y += 26;
    }
    y += 12;
    textCentered(fmt("<  Difficulty: %s  >", kDifficultyNames[difficulty]), sw / 2, y, 22, {255, 220, 120, 255});
    textCentered(kDifficultyText[difficulty], sw / 2, y + 30, 15, {200, 200, 190, 255});
    textCentered("ENTER: deploy     V: vehicle recognition guide     M: new map     Esc: quit", sw / 2, r.y + r.height - 34, 17,
                 {230, 220, 180, 255});
  }

  void drawGuide(float dt) {
    guideTime += dt;
    float sw = static_cast<float>(GetScreenWidth()), sh = static_cast<float>(GetScreenHeight());
    ClearBackground({22, 26, 22, 255});
    textCentered("VEHICLE RECOGNITION GUIDE", sw / 2, 14, 30, {232, 222, 182, 255});
    textCentered("Blue: NATO    Red: Warsaw Pact    (V or Esc to return)", sw / 2, 50, 15, {180, 180, 170, 255});
    const int cols = 5, rows = 3;
    const float margin = 16, gap = 10, top = 76;
    float cw = (sw - 2 * margin - (cols - 1) * gap) / cols;
    float ch = (sh - top - margin - (rows - 1) * gap) / rows;
    for (int i = 0; i < kVehicleCount; ++i) {
      const VehicleType& vt = vehicleType(i);
      float x = margin + (i % cols) * (cw + gap), y = top + (i / cols) * (ch + gap);
      panel({x, y, cw, ch}, 230);
      Color sc = sideColor(vt.side);
      DrawRectangle(static_cast<int>(x), static_cast<int>(y), static_cast<int>(cw), 4, sc);
      float art = std::min(cw * 0.42f, ch * 0.5f);
      DrawRectangle(static_cast<int>(x + 8), static_cast<int>(y + 12), static_cast<int>(art), static_cast<int>(art), {70, 88, 56, 255});
      float s = art * 0.8f / vt.length;
      drawVehicle(vt, {x + 8 + art / 2, y + 12 + art / 2}, -PI / 2, -PI / 2 + 0.6f * sinf(guideTime + i), s,
                  static_cast<uint32_t>(i * 2654435761u), false);
      drawCounter(vt, {x + cw - 30, y + 26}, 16, 255);
      float tx = x + art + 18;
      text(vt.name, tx, y + 14, 18, RAYWHITE);
      text(vt.nation, tx, y + 36, 13, {180, 180, 170, 255});
      text(vt.role, tx, y + 52, 13, {180, 180, 170, 255});
      text(fmt("Speed %d / %d km/h", static_cast<int>(vt.speed * kKmhPerSpeed), static_cast<int>(vt.roadSpeed * kKmhPerSpeed)),
           tx, y + 72, 13, {210, 210, 200, 255});
      text(fmt("Armour %d / %d mm", static_cast<int>(vt.armorFront), static_cast<int>(vt.armorSide)), tx, y + 88, 13,
           {210, 210, 200, 255});
      text(fmt("Optics %d m%s", static_cast<int>(vt.optics * kMetersPerUnit), vt.amphibious ? "  amphibious" : ""), tx, y + 104,
           13, {210, 210, 200, 255});
      float ly = y + 20 + art;
      if (vt.gun.valid()) {
        text(fmt("%s  %d m, %d mm", vt.gun.name, static_cast<int>(vt.gun.range * kMetersPerUnit), static_cast<int>(vt.gun.pen)),
             x + 10, ly, 13, {255, 230, 140, 255});
        ly += 17;
      }
      if (vt.atgm.valid()) {
        text(fmt("%s  %d m, %d mm", vt.atgm.name, static_cast<int>(vt.atgm.range * kMetersPerUnit), static_cast<int>(vt.atgm.pen)),
             x + 10, ly, 13, {255, 170, 110, 255});
        ly += 17;
      }
      // Word-wrap the blurb into the remaining space.
      std::string word, line;
      std::string blurb = vt.blurb;
      blurb += ' ';
      for (char c : blurb) {
        if (c != ' ') {
          word += c;
          continue;
        }
        std::string trial = line.empty() ? word : line + " " + word;
        if (textWidth(trial, 12) > cw - 20) {
          if (ly + 14 < y + ch) text(line, x + 10, ly, 12, {170, 175, 160, 255});
          ly += 15;
          line = word;
        } else {
          line = trial;
        }
        word.clear();
      }
      if (!line.empty() && ly + 14 < y + ch) text(line, x + 10, ly, 12, {170, 175, 160, 255});
    }
  }
};

int runSimulation(int count, int difficulty) {
  for (int i = 0; i < count; ++i) {
    Game g;
    g.start(difficulty, 1000u + static_cast<uint32_t>(i));
    g.beginBattle();
    while (g.phase != Phase::Over) {
      if (g.phase == Phase::Orders) g.executeTurn();
      g.update(kStep);
      g.particles.clear();
      g.sounds.clear();
    }
    int held = 0;
    for (const Objective& o : g.objectives) held += o.owner == Side::NATO;
    std::printf("battle %d: %-26s VP %4d/%-4d losses NATO %2d WP %2d objectives held %d time %s\n", i,
                g.outcomeName(), g.vp[0], g.vp[1], g.lossCount[0], g.lossCount[1], held, clockText(g.clock).c_str());
  }
  return 0;
}

}  // namespace

int main(int argc, char** argv) {
  // Developer options: headless balance runs and screenshots for testing.
  if (argc >= 2 && std::strcmp(argv[1], "--simulate") == 0) {
    int n = argc >= 3 ? std::atoi(argv[2]) : 5;
    int d = argc >= 4 ? std::atoi(argv[3]) : 1;
    return runSimulation(n, d);
  }
  const char* capturePath = nullptr;
  std::string captureMode;
  if (argc >= 4 && std::strcmp(argv[1], "--capture") == 0) {
    captureMode = argv[2];
    capturePath = argv[3];
  }

  SetConfigFlags(FLAG_WINDOW_RESIZABLE | FLAG_MSAA_4X_HINT | FLAG_VSYNC_HINT);
  InitWindow(1600, 900, "Steel Curtain - Fulda Gap 1985");
  SetWindowMinSize(1024, 640);
  {  // Fit smaller (laptop) screens: use 85% of the monitor if 1600x900 doesn't fit.
    int mon = GetCurrentMonitor();
    int mw = GetMonitorWidth(mon), mh = GetMonitorHeight(mon);
    if (mw > 0 && mh > 0 && (mw < 1700 || mh < 1000)) {
      int w = std::max(1024, static_cast<int>(mw * 0.85f)), h = std::max(640, static_cast<int>(mh * 0.85f));
      SetWindowSize(w, h);
      SetWindowPosition((mw - w) / 2, (mh - h) / 2);
    }
  }
  SetExitKey(KEY_NULL);
  SetTargetFPS(60);

  App app;
  app.audio.init();
  app.newMap();

  int frame = 0;
  if (capturePath && captureMode.rfind("battle", 0) == 0) {
    // battle:<seconds> fast-forwards the fight so the screenshot has action in it.
    float seconds = captureMode.size() > 7 ? static_cast<float>(std::atof(captureMode.c_str() + 7)) : 200.0f;
    app.screen = Screen::Playing;
    app.game.beginBattle();
    while (app.game.clock < seconds && app.game.phase != Phase::Over) {
      if (app.game.phase == Phase::Orders) app.game.executeTurn();
      app.game.update(kStep);
    }
    // Centre on the NATO vehicle closest to an enemy so the screenshot shows the fighting.
    int focus = -1;
    float bestD = 1e9f;
    for (const Unit& u : app.game.units) {
      if (!u.alive || u.side != Side::NATO) continue;
      for (const Unit& e : app.game.units) {
        if (e.alive && e.side == Side::WP && Vector2Distance(u.pos, e.pos) < bestD) {
          bestD = Vector2Distance(u.pos, e.pos);
          focus = u.id;
        }
      }
    }
    if (focus >= 0) {
      app.selection = {focus};
      app.cam.target = app.game.units[focus].pos;
      app.cam.zoom = captureMode.find(":zoom") != std::string::npos ? 1.6f : app.cam.zoom;
    }
    app.showCounters = captureMode.find(":counters") != std::string::npos;
  } else if (capturePath && captureMode == "orders") {
    // Turn 3 orders phase with several orders queued and a formation preview under the mouse.
    app.screen = Screen::Playing;
    app.game.beginBattle();
    while (app.game.turn < 3 && app.game.phase != Phase::Over) {
      if (app.game.phase == Phase::Orders) app.game.executeTurn();
      app.game.update(kStep);
    }
    const auto& pl = app.game.platoons;
    Vector2 o1 = app.game.objectives[1].pos;
    app.game.issueOrder(pl[1].units, o1 + Vector2{-120, -260}, MoveMode::Move, Formation::Wedge);
    app.game.issueOrder(pl[3].units, app.game.objectives[2].pos + Vector2{-200, 160}, MoveMode::Quick, Formation::Column);
    app.game.issueOrder(pl[2].units, app.game.objectives[0].pos + Vector2{60, -80}, MoveMode::Assault, Formation::Line);
    app.selection.assign(pl[4].units.begin(), pl[4].units.end());
    app.pruneSelection();
    app.formation = Formation::Line;
    app.orderMode = MoveMode::Deliberate;
    app.cam.target = o1 + Vector2{0, 60};
    app.cam.zoom = 0.85f;
    app.cam.offset = {GetScreenWidth() / 2.0f, GetScreenHeight() / 2.0f};
    SetMousePosition(static_cast<int>(GetScreenWidth() * 0.62f), static_cast<int>(GetScreenHeight() * 0.62f));
  } else if (capturePath && captureMode == "guide") {
    app.screen = Screen::Guide;
  }

  bool quit = false;
  while (!quit && !WindowShouldClose()) {
    float dt = GetFrameTime();
    switch (app.screen) {
      case Screen::Title:
        if (IsKeyPressed(KEY_LEFT)) app.difficulty = (app.difficulty + 2) % 3;
        if (IsKeyPressed(KEY_RIGHT)) app.difficulty = (app.difficulty + 1) % 3;
        if (IsKeyPressed(KEY_M)) app.newMap();
        if (IsKeyPressed(KEY_V)) app.guideReturn = Screen::Title, app.screen = Screen::Guide;
        if (IsKeyPressed(KEY_ESCAPE)) quit = true;
        if (IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_KP_ENTER)) {
          app.loadBattle();
          app.screen = Screen::Playing;
        }
        break;
      case Screen::Guide:
        if (IsKeyPressed(KEY_V) || IsKeyPressed(KEY_ESCAPE)) app.screen = app.guideReturn;
        break;
      case Screen::Playing:
        app.updateCamera(dt);
        app.handlePlayingInput();
        if (app.screen == Screen::Playing) app.stepSimulation(dt);
        app.playSounds();
        break;
    }

    BeginDrawing();
    ClearBackground({30, 36, 28, 255});
    switch (app.screen) {
      case Screen::Title: app.drawTitle(); break;
      case Screen::Guide: app.drawGuide(dt); break;
      case Screen::Playing:
        app.drawWorld();
        app.drawHud();
        break;
    }
    EndDrawing();

    if (capturePath && ++frame == 30) {
      Image img = LoadImageFromScreen();
      ExportImage(img, capturePath);
      UnloadImage(img);
      quit = true;
    }
  }

  if (app.terrainReady) UnloadRenderTexture(app.terrainTex);
  app.audio.shutdown();
  CloseWindow();
  return 0;
}
