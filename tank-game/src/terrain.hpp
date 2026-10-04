// Procedurally generated West German countryside: farmland, hills, woods, towns, a river
// with bridges, and the roads that tie them together.
#pragma once

#include <string>
#include <vector>

#include "common.hpp"
#include "vehicles.hpp"

enum class Ground : uint8_t { Clear, Woods, Town, Water };

struct Tile {
  Ground ground = Ground::Clear;
  uint8_t height = 0;  // 0 valley, 1 rolling ground, 2 high ridge
  bool road = false;
  bool bridge = false;
};

struct Town {
  std::string name;
  Vector2 center;
  float radius;
  bool objective = false;
};

class Terrain {
 public:
  static constexpr int kTile = 40;  // world units per tile
  static constexpr int kCols = 80;
  static constexpr int kRows = 50;

  void generate(uint32_t seed);

  float width() const { return kCols * kTile; }
  float height() const { return kRows * kTile; }
  bool inBounds(int tx, int ty) const { return tx >= 0 && ty >= 0 && tx < kCols && ty < kRows; }
  const Tile& at(int tx, int ty) const { return tiles_[ty * kCols + tx]; }
  const Tile& atWorld(Vector2 p) const;
  Vector2 tileCenter(int tx, int ty) const {
    return {(tx + 0.5f) * kTile, (ty + 0.5f) * kTile};
  }

  // Movement speed multiplier for a vehicle on this tile; 0 means impassable.
  float speedFactor(const Tile& t, const VehicleType& vt) const;
  float speedFactorAt(Vector2 p, const VehicleType& vt) const;
  bool passable(Vector2 p, const VehicleType& vt) const { return speedFactorAt(p, vt) > 0; }

  // Hit-chance multiplier for a target standing here (woods and buildings protect).
  float coverAt(Vector2 p) const;
  // Spotting-range multiplier for a target standing here.
  float concealmentAt(Vector2 p) const;
  // Terrain-only line of sight: hills, woods and towns block.
  bool lineOfSight(Vector2 a, Vector2 b) const;

  std::vector<Vector2> findPath(Vector2 from, Vector2 to, const VehicleType& vt) const;

  const std::vector<Town>& towns() const { return towns_; }
  const std::vector<int>& northRoads() const { return northRoads_; }
  const std::vector<int>& objectiveTowns() const { return objectiveTowns_; }
  const char* groundName(Vector2 p) const;

  // Paints the static map into a render texture (call after the window exists).
  void render(RenderTexture2D& target) const;

 private:
  Tile& mut(int tx, int ty) { return tiles_[ty * kCols + tx]; }
  void placeTown(int cx, int cy, float radius, const std::string& name, bool objective);
  bool segmentClear(Vector2 a, Vector2 b, const VehicleType& vt, float minFactor) const;

  std::vector<Tile> tiles_ = std::vector<Tile>(kCols * kRows);
  std::vector<Town> towns_;
  std::vector<int> northRoads_;      // tile columns where N-S roads enter the map
  std::vector<int> objectiveTowns_;  // indices into towns_
  uint32_t seed_ = 1;
};
