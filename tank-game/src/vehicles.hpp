// Vehicle database: every armoured vehicle in the game and its combat statistics.
#pragma once

#include "common.hpp"

enum class UnitClass { Tank, IFV, APC, Recon, AntiTank, AirDefense };
enum class SpriteStyle { Tank, IFV, APC, ITV, Wheeled4, Wheeled8, Shilka };

struct Weapon {
  const char* name = nullptr;  // nullptr means the vehicle has no such weapon
  float range = 0;             // world units
  float pen = 0;               // armour penetration, mm RHA equivalent
  float reload = 0;            // seconds between shots
  float accuracy = 0;          // hit chance at point-blank against a stationary target
  float killChance = 0.85f;    // chance a penetrating hit destroys rather than damages
  int ammo = -1;               // -1 means effectively unlimited
  bool missile = false;        // wire-guided: the launcher must halt while guiding
  bool stabilized = false;     // can shoot accurately on the move
  bool light = false;          // autocannon / machine gun (tracer look, small sound)

  bool valid() const { return name != nullptr; }
};

struct VehicleType {
  const char* name;
  const char* nation;
  const char* role;
  Side side;
  UnitClass cls;
  SpriteStyle style;
  bool tracked;
  bool amphibious;
  bool roundTurret;
  float speed;      // cross-country, world units per second
  float roadSpeed;  // on roads
  float turnRate;   // hull, radians per second
  float turretRate; // radians per second
  float armorFront;
  float armorSide;
  float optics;     // spotting range in clear terrain (thermals give NATO an edge)
  float size;       // signature: bigger is easier to spot and hit
  Weapon gun;
  Weapon atgm;
  int smokeCharges;
  int points;       // victory points when destroyed
  float length;     // sprite size in world units
  float width;
  Color paint;
  Color camo;
  const char* blurb;
};

enum VehicleId {
  kM1Abrams,
  kLeopard2,
  kM60A3,
  kM2Bradley,
  kM3Bradley,
  kM113,
  kM901Itv,
  kLuchs,
  kT80B,
  kT72A,
  kT62M,
  kBMP2,
  kBTR70,
  kBRDM2,
  kZSU234,
  kVehicleCount
};

const VehicleType& vehicleType(int id);
const char* className(UnitClass c);
