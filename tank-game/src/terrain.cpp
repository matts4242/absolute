#include "terrain.hpp"

#include <algorithm>
#include <limits>
#include <queue>

namespace {

float smoothstep(float t) { return t * t * (3.0f - 2.0f * t); }

// Bilinearly interpolated grid of random values: cheap, smooth "noise" for hills and woods.
class ValueNoise {
 public:
  ValueNoise(int gw, int gh, std::mt19937& eng) : gw_(gw), gh_(gh), v_(gw * gh) {
    std::uniform_real_distribution<float> d(0.0f, 1.0f);
    for (auto& x : v_) x = d(eng);
  }
  float sample(float u, float v) const {
    float x = u * (gw_ - 1), y = v * (gh_ - 1);
    int x0 = std::clamp(static_cast<int>(x), 0, gw_ - 2);
    int y0 = std::clamp(static_cast<int>(y), 0, gh_ - 2);
    float fx = smoothstep(x - x0), fy = smoothstep(y - y0);
    float a = at(x0, y0), b = at(x0 + 1, y0), c = at(x0, y0 + 1), d = at(x0 + 1, y0 + 1);
    return (a * (1 - fx) + b * fx) * (1 - fy) + (c * (1 - fx) + d * fx) * fy;
  }

 private:
  float at(int x, int y) const { return v_[y * gw_ + x]; }
  int gw_, gh_;
  std::vector<float> v_;
};

Color shade(Color c, int delta) {
  auto ch = [&](unsigned char v) { return static_cast<unsigned char>(std::clamp(v + delta, 0, 255)); };
  return {ch(c.r), ch(c.g), ch(c.b), c.a};
}

const char* const kTownNames[] = {"Rasdorf",   "Tann",      "Hilders",  "Schlitz",
                                  "Lauterbach", "Alsfeld",  "Grebenau", "Eiterfeld",
                                  "Burghaun",  "Petersberg", "Eichenzell", "Gersfeld",
                                  "Neuhof",    "Steinau",   "Huenfeld", "Hosenfeld"};

}  // namespace

const Tile& Terrain::atWorld(Vector2 p) const {
  int tx = std::clamp(static_cast<int>(p.x / kTile), 0, kCols - 1);
  int ty = std::clamp(static_cast<int>(p.y / kTile), 0, kRows - 1);
  return at(tx, ty);
}

void Terrain::generate(uint32_t seed) {
  seed_ = seed;
  std::mt19937 eng(seed);
  auto irange = [&](int lo, int hi) { return std::uniform_int_distribution<int>(lo, hi)(eng); };
  auto uni = [&](float lo, float hi) { return std::uniform_real_distribution<float>(lo, hi)(eng); };

  tiles_.assign(kCols * kRows, Tile{});
  towns_.clear();
  northRoads_.clear();
  objectiveTowns_.clear();

  // Rolling hills.
  ValueNoise hillsA(9, 6, eng), hillsB(17, 11, eng), woodsA(13, 9, eng), woodsB(27, 17, eng);
  for (int ty = 0; ty < kRows; ++ty) {
    for (int tx = 0; tx < kCols; ++tx) {
      float u = (tx + 0.5f) / kCols, v = (ty + 0.5f) / kRows;
      float h = hillsA.sample(u, v) * 0.7f + hillsB.sample(u, v) * 0.3f;
      mut(tx, ty).height = h > 0.64f ? 2 : h > 0.47f ? 1 : 0;
    }
  }

  // A river meandering west to east across the middle of the map.
  std::vector<int> riverLo(kCols), riverHi(kCols);
  const int riverMin = static_cast<int>(kRows * 0.38f), riverMax = static_cast<int>(kRows * 0.56f);
  int ry = static_cast<int>(kRows * 0.47f) + irange(-2, 2);
  for (int x = 0; x < kCols; ++x) {
    int lo = ry, hi = ry;
    if (x > 0 && irange(0, 2) == 0) {
      int ny = std::clamp(ry + (irange(0, 1) ? 1 : -1), riverMin, riverMax);
      lo = std::min(ry, ny);
      hi = std::max(ry, ny);
      ry = ny;
    }
    riverLo[x] = lo;
    riverHi[x] = hi;
    for (int y = lo - 1; y <= hi + 1; ++y) mut(x, y).height = 0;  // river valley
    for (int y = lo; y <= hi; ++y) mut(x, y).ground = Ground::Water;
  }
  auto nearRiver = [&](int x, int y) {
    for (int dx = -1; dx <= 1; ++dx) {
      int cx = std::clamp(x + dx, 0, kCols - 1);
      if (y >= riverLo[cx] - 2 && y <= riverHi[cx] + 2) return true;
    }
    return false;
  };

  // East-west roads north and south of the river.
  auto buildEastWest = [&](int baseY) {
    std::vector<int> ys(kCols);
    int y = baseY;
    for (int x = 0; x < kCols; ++x) {
      if (x > 0 && x % 4 == 0 && irange(0, 2) == 0) {
        int ny = std::clamp(y + (irange(0, 1) ? 1 : -1), baseY - 3, baseY + 3);
        mut(x, y).road = true;
        y = ny;
      }
      mut(x, y).road = true;
      ys[x] = y;
    }
    return ys;
  };
  std::vector<int> northEW = buildEastWest(static_cast<int>(kRows * 0.17f) + irange(-1, 1));
  std::vector<int> southEW = buildEastWest(static_cast<int>(kRows * 0.82f) + irange(-1, 1));

  // North-south roads crossing the river on bridges.
  struct RoadInfo {
    int bridgeX = -1, bridgeY = -1, northX = -1, northY = -1, southX = -1, southY = -1;
  };
  std::vector<RoadInfo> roads;
  const float columns[] = {0.17f, 0.5f, 0.83f};
  for (float c : columns) {
    RoadInfo info;
    int x = std::clamp(static_cast<int>(kCols * c) + irange(-3, 3), 3, kCols - 4);
    northRoads_.push_back(x);
    for (int y = 0; y < kRows; ++y) {
      if (y > 0 && y % 3 == 0 && !nearRiver(x, y) && irange(0, 2) == 0) {
        mut(x, y).road = true;
        x = std::clamp(x + (irange(0, 1) ? 1 : -1), 3, kCols - 4);
      }
      Tile& t = mut(x, y);
      t.road = true;
      if (t.ground == Ground::Water) {
        t.bridge = true;
        info.bridgeX = x;
        info.bridgeY = y;
      }
      if (info.northX < 0 && y == northEW[x]) info.northX = x, info.northY = y;
      if (info.southX < 0 && y == southEW[x]) info.southX = x, info.southY = y;
    }
    roads.push_back(info);
  }

  // Towns: bridgehead towns are the objectives; crossroads villages add cover.
  std::vector<std::string> names(std::begin(kTownNames), std::end(kTownNames));
  std::shuffle(names.begin(), names.end(), eng);
  size_t nameIdx = 0;
  for (const RoadInfo& r : roads) {
    int ty = std::min(r.bridgeY + 4, kRows - 2);
    for (int x = 0; x < kCols; ++x) {  // follow the road south of the bridge
      if (mut(x, ty).road && std::abs(x - r.bridgeX) <= 3) {
        placeTown(x, ty, 2.4f, names[nameIdx++], true);
        break;
      }
    }
  }
  for (const RoadInfo& r : roads) {
    if (r.northX >= 0) placeTown(r.northX, r.northY, uni(1.6f, 2.4f), names[nameIdx++], false);
  }
  for (const RoadInfo& r : roads) {
    if (r.southX >= 0 && irange(0, 2) > 0) {
      placeTown(r.southX, r.southY, uni(1.5f, 2.2f), names[nameIdx++], false);
    }
  }

  // Woods: large forests from noise plus scattered copses.
  for (int ty = 0; ty < kRows; ++ty) {
    for (int tx = 0; tx < kCols; ++tx) {
      float u = (tx + 0.5f) / kCols, v = (ty + 0.5f) / kRows;
      float w = woodsA.sample(u, v) * 0.6f + woodsB.sample(u, v) * 0.4f;
      Tile& t = mut(tx, ty);
      if (w > 0.6f && t.ground == Ground::Clear) t.ground = Ground::Woods;
    }
  }
  for (int i = 0; i < 28; ++i) {
    int cx = irange(2, kCols - 3), cy = irange(2, kRows - 3);
    float r = uni(0.8f, 1.7f);
    for (int y = cy - 2; y <= cy + 2; ++y) {
      for (int x = cx - 2; x <= cx + 2; ++x) {
        Tile& t = mut(x, y);
        if (std::hypot(x - cx, y - cy) <= r && t.ground == Ground::Clear && !t.road) {
          t.ground = Ground::Woods;
        }
      }
    }
  }
}

void Terrain::placeTown(int cx, int cy, float radius, const std::string& name, bool objective) {
  std::mt19937 eng(seed_ + static_cast<uint32_t>(cx * 131 + cy));
  std::uniform_real_distribution<float> jitter(-0.5f, 0.5f);
  int r = static_cast<int>(radius) + 1;
  for (int y = cy - r; y <= cy + r; ++y) {
    for (int x = cx - r; x <= cx + r; ++x) {
      if (!inBounds(x, y)) continue;
      Tile& t = mut(x, y);
      if (t.ground == Ground::Water) continue;
      if (std::hypot(x - cx, y - cy) + jitter(eng) <= radius) t.ground = Ground::Town;
    }
  }
  if (objective) objectiveTowns_.push_back(static_cast<int>(towns_.size()));
  towns_.push_back({name, tileCenter(cx, cy), radius * kTile, objective});
}

float Terrain::speedFactor(const Tile& t, const VehicleType& vt) const {
  if (t.bridge || (t.road && t.ground != Ground::Water)) return vt.roadSpeed / vt.speed;
  switch (t.ground) {
    case Ground::Water: return vt.amphibious ? 0.3f : 0.0f;
    case Ground::Woods: return vt.tracked ? 0.45f : 0.3f;
    case Ground::Town: return 0.75f;
    case Ground::Clear: return vt.tracked ? 1.0f : 0.85f;
  }
  return 1.0f;
}

float Terrain::speedFactorAt(Vector2 p, const VehicleType& vt) const {
  if (p.x < 0 || p.y < 0 || p.x >= width() || p.y >= height()) return 0.0f;
  return speedFactor(atWorld(p), vt);
}

float Terrain::coverAt(Vector2 p) const {
  switch (atWorld(p).ground) {
    case Ground::Woods: return 0.6f;
    case Ground::Town: return 0.5f;
    default: return 1.0f;
  }
}

float Terrain::concealmentAt(Vector2 p) const {
  switch (atWorld(p).ground) {
    case Ground::Woods: return 0.45f;
    case Ground::Town: return 0.5f;
    default: return 1.0f;
  }
}

const char* Terrain::groundName(Vector2 p) const {
  const Tile& t = atWorld(p);
  if (t.bridge) return "Bridge";
  if (t.road && t.ground == Ground::Clear) return "Road";
  switch (t.ground) {
    case Ground::Woods: return "Woods";
    case Ground::Town: return "Town";
    case Ground::Water: return "River";
    case Ground::Clear: return t.height == 2 ? "Ridge" : t.height == 1 ? "Rolling ground" : "Open ground";
  }
  return "";
}

bool Terrain::lineOfSight(Vector2 a, Vector2 b) const {
  float d = Vector2Distance(a, b);
  const float step = kTile * 0.4f;
  int n = static_cast<int>(d / step);
  if (n < 2) return true;
  float ha = atWorld(a).height + 0.45f, hb = atWorld(b).height + 0.45f;
  const float stepLen = d / n;
  float screened = 0;  // distance travelled through trees or buildings
  for (int i = 1; i < n; ++i) {
    float t = static_cast<float>(i) / n;
    const Tile& tile = atWorld(Vector2Lerp(a, b, t));
    float lineH = ha + (hb - ha) * t;
    if (tile.height > lineH + 0.05f) return false;  // a crest is in the way
    // Woods and towns screen vision once the sight line runs deep enough through them, so
    // vehicles can still see out of an edge, and fight each other inside a town.
    bool obstacle = tile.ground == Ground::Woods || tile.ground == Ground::Town;
    bool nearEnd = d * t < 20.0f || d * (1 - t) < 20.0f;  // the cover a unit sits in
    if (obstacle && !nearEnd && d > 70.0f && tile.height + 0.8f > lineH) {
      screened += stepLen;
      if (screened > 45.0f) return false;
    }
  }
  return true;
}

bool Terrain::segmentClear(Vector2 a, Vector2 b, const VehicleType& vt, float minFactor) const {
  float d = Vector2Distance(a, b);
  int n = std::max(1, static_cast<int>(d / 8.0f));
  for (int i = 0; i <= n; ++i) {
    float f = speedFactorAt(Vector2Lerp(a, b, static_cast<float>(i) / n), vt);
    if (f <= 0.0f || f < minFactor) return false;
  }
  return true;
}

std::vector<Vector2> Terrain::findPath(Vector2 from, Vector2 to, const VehicleType& vt) const {
  auto tileOf = [&](Vector2 p) {
    int tx = std::clamp(static_cast<int>(p.x / kTile), 0, kCols - 1);
    int ty = std::clamp(static_cast<int>(p.y / kTile), 0, kRows - 1);
    return ty * kCols + tx;
  };
  auto factor = [&](int idx) { return speedFactor(tiles_[idx], vt); };

  int start = tileOf(from), goal = tileOf(to);
  Vector2 finalPoint = to;
  if (factor(goal) <= 0.0f) {  // clicked on impassable ground: use the nearest usable tile
    int gx = goal % kCols, gy = goal / kCols, best = -1;
    float bestD = std::numeric_limits<float>::max();
    for (int y = gy - 6; y <= gy + 6; ++y) {
      for (int x = gx - 6; x <= gx + 6; ++x) {
        if (!inBounds(x, y) || factor(y * kCols + x) <= 0.0f) continue;
        float dd = std::hypot(x - gx, y - gy);
        if (dd < bestD) bestD = dd, best = y * kCols + x;
      }
    }
    if (best < 0) return {};
    goal = best;
    finalPoint = tileCenter(goal % kCols, goal / kCols);
  }

  const float maxFactor = std::max(1.0f, vt.roadSpeed / vt.speed);
  auto heuristic = [&](int idx) {
    float dx = static_cast<float>(idx % kCols - goal % kCols);
    float dy = static_cast<float>(idx / kCols - goal / kCols);
    return std::sqrt(dx * dx + dy * dy) * kTile / maxFactor;
  };

  const int count = kCols * kRows;
  std::vector<float> g(count, std::numeric_limits<float>::max());
  std::vector<int> came(count, -1);
  std::vector<bool> closed(count, false);
  using Entry = std::pair<float, int>;
  std::priority_queue<Entry, std::vector<Entry>, std::greater<Entry>> open;
  g[start] = 0;
  open.push({heuristic(start), start});

  while (!open.empty()) {
    int cur = open.top().second;
    open.pop();
    if (closed[cur]) continue;
    closed[cur] = true;
    if (cur == goal) break;
    int cx = cur % kCols, cy = cur / kCols;
    float fc = std::max(factor(cur), 0.3f);
    for (int dy = -1; dy <= 1; ++dy) {
      for (int dx = -1; dx <= 1; ++dx) {
        if (dx == 0 && dy == 0) continue;
        int nx = cx + dx, ny = cy + dy;
        if (!inBounds(nx, ny)) continue;
        int ni = ny * kCols + nx;
        float fn = factor(ni);
        if (fn <= 0.0f || closed[ni]) continue;
        bool diagonal = dx != 0 && dy != 0;
        if (diagonal && (factor(cy * kCols + nx) <= 0.0f || factor(ny * kCols + cx) <= 0.0f)) continue;
        float cost = (diagonal ? 1.4142f : 1.0f) * kTile / ((fc + fn) * 0.5f);
        if (g[cur] + cost < g[ni]) {
          g[ni] = g[cur] + cost;
          came[ni] = cur;
          open.push({g[ni] + heuristic(ni), ni});
        }
      }
    }
  }
  if (goal != start && came[goal] < 0) return {};

  std::vector<int> tiles;
  for (int i = goal; i != -1; i = came[i]) tiles.push_back(i);
  std::reverse(tiles.begin(), tiles.end());

  // String-pull: skip waypoints when a straight line is never slower than the grid route.
  std::vector<Vector2> points;
  const size_t n = tiles.size();
  auto pointAt = [&](size_t j) {
    return j == n - 1 ? finalPoint : tileCenter(static_cast<int>(tiles[j] % kCols), static_cast<int>(tiles[j] / kCols));
  };
  Vector2 anchor = from;
  size_t k = 0;
  while (k < n - 1) {
    size_t best = k + 1;
    float minF = std::min(factor(tiles[k]) > 0 ? factor(tiles[k]) : 10.0f, factor(tiles[k + 1]));
    for (size_t j = k + 1; j < n; ++j) {
      minF = std::min(minF, factor(tiles[j]));
      if (segmentClear(anchor, pointAt(j), vt, minF * 0.999f)) best = j;
      else break;
    }
    anchor = pointAt(best);
    points.push_back(anchor);
    k = best;
  }
  if (points.empty()) points.push_back(finalPoint);
  return points;
}

void Terrain::render(RenderTexture2D& target) const {
  std::mt19937 eng(seed_ ^ 0x5eedu);
  auto irange = [&](int lo, int hi) { return std::uniform_int_distribution<int>(lo, hi)(eng); };
  auto uni = [&](float lo, float hi) { return std::uniform_real_distribution<float>(lo, hi)(eng); };
  const float T = static_cast<float>(kTile);

  const Color fieldPalette[] = {{104, 128, 66, 255}, {150, 146, 86, 255}, {92, 122, 58, 255},
                                {122, 104, 74, 255}, {124, 136, 80, 255}, {112, 132, 70, 255}};
  auto blockOf = [](int tx, int ty) {
    int by = ty / 3;
    int bx = (tx + (by % 2) * 2) / 4;
    return bx * 97 + by * 31;
  };
  auto hash = [](int v) {
    uint32_t h = static_cast<uint32_t>(v) * 2654435761u;
    return static_cast<int>((h >> 16) & 0x7fff);
  };

  BeginTextureMode(target);
  ClearBackground({100, 124, 64, 255});

  // Pass 1: ground.
  for (int ty = 0; ty < kRows; ++ty) {
    for (int tx = 0; tx < kCols; ++tx) {
      const Tile& t = at(tx, ty);
      float x = tx * T, y = ty * T;
      Color c;
      switch (t.ground) {
        case Ground::Water: c = {54, 92, 134, 255}; break;
        case Ground::Woods: c = {46, 72, 38, 255}; break;
        case Ground::Town: c = {146, 142, 128, 255}; break;
        case Ground::Clear: {
          int b = hash(blockOf(tx, ty));
          c = fieldPalette[b % 6];
          break;
        }
      }
      if (t.ground != Ground::Water) c = shade(c, t.height * 9 + irange(-3, 3));
      DrawRectangle(static_cast<int>(x), static_cast<int>(y), kTile, kTile, c);

      if (t.ground == Ground::Clear) {
        int b = hash(blockOf(tx, ty));
        int kind = b % 6;
        if (kind == 1 || kind == 3 || kind == 2) {  // furrows in crops and ploughed land
          Color fc = shade(c, -14);
          fc.a = 110;
          bool vertical = (b / 6) % 2;
          for (int i = 3; i < kTile; i += 6) {
            if (vertical) DrawLineEx({x + i, y}, {x + i, y + T}, 1.2f, fc);
            else DrawLineEx({x, y + i}, {x + T, y + i}, 1.2f, fc);
          }
        }
      } else if (t.ground == Ground::Water) {
        for (int i = 0; i < 3; ++i) {
          float rx = x + uni(4, T - 14), ryy = y + uni(4, T - 4);
          DrawLineEx({rx, ryy}, {rx + uni(6, 12), ryy}, 1.5f, {86, 126, 168, 200});
        }
      }
    }
  }

  // Pass 2: river banks, hedgerows and contour lines.
  for (int ty = 0; ty < kRows; ++ty) {
    for (int tx = 0; tx < kCols; ++tx) {
      const Tile& t = at(tx, ty);
      float x = tx * T, y = ty * T;
      const int nbs[2][2] = {{1, 0}, {0, 1}};
      for (const auto& nb : nbs) {
        int nx = tx + nb[0], ny = ty + nb[1];
        if (!inBounds(nx, ny)) continue;
        const Tile& o = at(nx, ny);
        Vector2 a = nb[0] ? Vector2{x + T, y} : Vector2{x, y + T};
        Vector2 b = nb[0] ? Vector2{x + T, y + T} : Vector2{x + T, y + T};
        if ((t.ground == Ground::Water) != (o.ground == Ground::Water)) {
          DrawLineEx(a, b, 3.0f, {88, 78, 54, 255});
        } else if (t.ground == Ground::Clear && o.ground == Ground::Clear && !t.road && !o.road &&
                   blockOf(tx, ty) != blockOf(nx, ny)) {
          for (float s = 0; s < T; s += 5.0f) {  // hedgerow
            if (irange(0, 9) < 7) {
              Vector2 p = Vector2Lerp(a, b, s / T);
              DrawCircleV(p, uni(1.8f, 3.2f), {50, 76, 38, 230});
            }
          }
        }
        if (t.height != o.height) DrawLineEx(a, b, 2.0f, {74, 62, 40, 110});
      }
    }
  }

  // Pass 3: roads and bridges.
  auto roadLinks = [&](float width, Color color, float joint) {
    for (int ty = 0; ty < kRows; ++ty) {
      for (int tx = 0; tx < kCols; ++tx) {
        if (!at(tx, ty).road) continue;
        Vector2 c = tileCenter(tx, ty);
        if (inBounds(tx + 1, ty) && at(tx + 1, ty).road) DrawLineEx(c, tileCenter(tx + 1, ty), width, color);
        if (inBounds(tx, ty + 1) && at(tx, ty + 1).road) DrawLineEx(c, tileCenter(tx, ty + 1), width, color);
        DrawCircleV(c, joint, color);
        if (tx == 0) DrawLineEx({0, c.y}, c, width, color);
        if (ty == 0) DrawLineEx({c.x, 0}, c, width, color);
        if (tx == kCols - 1) DrawLineEx(c, {c.x + T, c.y}, width, color);
        if (ty == kRows - 1) DrawLineEx(c, {c.x, c.y + T}, width, color);
      }
    }
  };
  for (int ty = 0; ty < kRows; ++ty) {
    for (int tx = 0; tx < kCols; ++tx) {
      if (!at(tx, ty).bridge) continue;
      float x = tx * T, y = ty * T;
      DrawRectangle(static_cast<int>(x + T / 2 - 9), static_cast<int>(y - 4), 18, kTile + 8, {118, 112, 104, 255});
      DrawLineEx({x + T / 2 - 9, y - 4}, {x + T / 2 - 9, y + T + 4}, 2.0f, {60, 56, 52, 255});
      DrawLineEx({x + T / 2 + 9, y - 4}, {x + T / 2 + 9, y + T + 4}, 2.0f, {60, 56, 52, 255});
    }
  }
  roadLinks(11.0f, {98, 88, 68, 255}, 5.5f);
  roadLinks(7.5f, {192, 178, 140, 255}, 3.75f);

  // Pass 4: buildings with red tiled roofs.
  const Color roofs[] = {{158, 72, 56, 255}, {136, 60, 48, 255}, {170, 92, 66, 255}, {116, 110, 108, 255}};
  for (int ty = 0; ty < kRows; ++ty) {
    for (int tx = 0; tx < kCols; ++tx) {
      const Tile& t = at(tx, ty);
      if (t.ground != Ground::Town) continue;
      float x = tx * T, y = ty * T;
      for (int i = 0; i < 5; ++i) {
        float w = uni(9, 16), h = uni(7, 12);
        if (irange(0, 1)) std::swap(w, h);
        float bx = x + uni(1, T - w - 1), by = y + uni(1, T - h - 1);
        float cx = bx + w / 2 - (x + T / 2), cy = by + h / 2 - (y + T / 2);
        if (t.road && (std::fabs(cx) < 11 || std::fabs(cy) < 11)) continue;
        Color roof = roofs[irange(0, 3)];
        DrawRectangleRec({bx + 2, by + 2, w, h}, {30, 30, 30, 90});
        DrawRectangleRec({bx, by, w, h}, roof);
        if (w > h) DrawLineEx({bx, by + h / 2}, {bx + w, by + h / 2}, 1.2f, shade(roof, -40));
        else DrawLineEx({bx + w / 2, by}, {bx + w / 2, by + h}, 1.2f, shade(roof, -40));
      }
    }
  }

  // Pass 5: tree crowns (allowed to spill over tile edges for organic forest borders).
  for (int ty = 0; ty < kRows; ++ty) {
    for (int tx = 0; tx < kCols; ++tx) {
      const Tile& t = at(tx, ty);
      if (t.ground != Ground::Woods) continue;
      float cx = tx * T + T / 2, cy = ty * T + T / 2;
      for (int i = 0; i < 12; ++i) {
        float ox = uni(-T / 2 - 2, T / 2 + 2), oy = uni(-T / 2 - 2, T / 2 + 2);
        if (t.road && (std::fabs(ox) < 9 || std::fabs(oy) < 9)) continue;
        float r = uni(5.0f, 9.0f);
        Color crown = {static_cast<unsigned char>(irange(48, 70)), static_cast<unsigned char>(irange(86, 108)),
                       static_cast<unsigned char>(irange(40, 52)), 255};
        DrawCircleV({cx + ox + 2, cy + oy + 2}, r, {24, 36, 20, 140});
        DrawCircleV({cx + ox, cy + oy}, r, crown);
        DrawCircleV({cx + ox - r * 0.3f, cy + oy - r * 0.3f}, r * 0.45f, shade(crown, 14));
      }
    }
  }
  EndTextureMode();
}
