#include "vehicles.hpp"

namespace {

// Paint schemes.
constexpr Color kMerdcGreen{88, 96, 52, 255};   // US/FRG NATO three-tone base
constexpr Color kMerdcBrown{92, 70, 44, 255};
constexpr Color kFrgGreen{82, 92, 60, 255};
constexpr Color kSovietGreen{84, 104, 58, 255};
constexpr Color kSovietDark{62, 78, 44, 255};

constexpr float kDeg = PI / 180.0f;

const VehicleType kTypes[kVehicleCount] = {
    // ---------------- NATO ----------------
    {.name = "M1 Abrams", .nation = "USA", .role = "Main battle tank", .side = Side::NATO,
     .cls = UnitClass::Tank, .style = SpriteStyle::Tank, .tracked = true, .amphibious = false,
     .roundTurret = false, .speed = 17, .roadSpeed = 28, .turnRate = 80 * kDeg,
     .turretRate = 60 * kDeg, .armorFront = 470, .armorSide = 90, .optics = 1500, .size = 1.0f,
     .gun = {.name = "105mm M68A1", .range = 1000, .pen = 420, .reload = 6.0f, .accuracy = 0.86f,
             .killChance = 0.85f, .ammo = 55, .stabilized = true},
     .atgm = {}, .smokeCharges = 2, .points = 50, .length = 32, .width = 15,
     .paint = kMerdcGreen, .camo = kMerdcBrown,
     .blurb = "Gas-turbine tank with Chobham armour and thermal sights. Fast and hard to kill "
              "from the front."},
    {.name = "Leopard 2A4", .nation = "West Germany", .role = "Main battle tank",
     .side = Side::NATO, .cls = UnitClass::Tank, .style = SpriteStyle::Tank, .tracked = true,
     .amphibious = false, .roundTurret = false, .speed = 18, .roadSpeed = 29,
     .turnRate = 80 * kDeg, .turretRate = 60 * kDeg, .armorFront = 480, .armorSide = 90,
     .optics = 1500, .size = 1.0f,
     .gun = {.name = "120mm Rh-120", .range = 1050, .pen = 520, .reload = 6.5f,
             .accuracy = 0.87f, .killChance = 0.9f, .ammo = 42, .stabilized = true},
     .atgm = {}, .smokeCharges = 2, .points = 55, .length = 32, .width = 15,
     .paint = kFrgGreen, .camo = kMerdcBrown,
     .blurb = "The Bundeswehr's 120mm smoothbore outranges and outpunches anything the Warsaw "
              "Pact fields."},
    {.name = "M60A3 TTS", .nation = "USA", .role = "Main battle tank", .side = Side::NATO,
     .cls = UnitClass::Tank, .style = SpriteStyle::Tank, .tracked = true, .amphibious = false,
     .roundTurret = true, .speed = 13, .roadSpeed = 21, .turnRate = 65 * kDeg,
     .turretRate = 45 * kDeg, .armorFront = 300, .armorSide = 70, .optics = 1400, .size = 1.2f,
     .gun = {.name = "105mm M68", .range = 950, .pen = 400, .reload = 7.0f, .accuracy = 0.8f,
             .killChance = 0.85f, .ammo = 63, .stabilized = true},
     .atgm = {}, .smokeCharges = 1, .points = 35, .length = 32, .width = 16,
     .paint = kMerdcGreen, .camo = kMerdcBrown,
     .blurb = "Older and taller than the Abrams, but its thermal sight still lets it shoot "
              "first."},
    {.name = "M2A1 Bradley", .nation = "USA", .role = "Infantry fighting vehicle",
     .side = Side::NATO, .cls = UnitClass::IFV, .style = SpriteStyle::IFV, .tracked = true,
     .amphibious = false, .roundTurret = false, .speed = 16, .roadSpeed = 26,
     .turnRate = 85 * kDeg, .turretRate = 70 * kDeg, .armorFront = 60, .armorSide = 35,
     .optics = 1450, .size = 1.0f,
     .gun = {.name = "25mm M242 Bushmaster", .range = 700, .pen = 70, .reload = 0.5f,
             .accuracy = 0.55f, .killChance = 0.25f, .ammo = -1, .stabilized = true,
             .light = true},
     .atgm = {.name = "TOW-2", .range = 1500, .pen = 800, .reload = 14.0f, .accuracy = 0.9f,
              .killChance = 0.9f, .ammo = 7, .missile = true},
     .smokeCharges = 1, .points = 30, .length = 28, .width = 14,
     .paint = kMerdcGreen, .camo = kMerdcBrown,
     .blurb = "Chain gun for light armour, twin TOW launcher for tanks. It must halt to guide "
              "a missile."},
    {.name = "M3 Bradley CFV", .nation = "USA", .role = "Cavalry scout vehicle",
     .side = Side::NATO, .cls = UnitClass::Recon, .style = SpriteStyle::IFV, .tracked = true,
     .amphibious = false, .roundTurret = false, .speed = 16, .roadSpeed = 26,
     .turnRate = 85 * kDeg, .turretRate = 70 * kDeg, .armorFront = 60, .armorSide = 35,
     .optics = 1700, .size = 1.0f,
     .gun = {.name = "25mm M242 Bushmaster", .range = 700, .pen = 70, .reload = 0.5f,
             .accuracy = 0.55f, .killChance = 0.25f, .ammo = -1, .stabilized = true,
             .light = true},
     .atgm = {.name = "TOW-2", .range = 1500, .pen = 800, .reload = 14.0f, .accuracy = 0.9f,
              .killChance = 0.9f, .ammo = 10, .missile = true},
     .smokeCharges = 1, .points = 30, .length = 28, .width = 14,
     .paint = kMerdcGreen, .camo = kMerdcBrown,
     .blurb = "Cavalry version of the Bradley: extra missiles and the best eyes on the "
              "battlefield."},
    {.name = "M113A2", .nation = "USA", .role = "Armoured personnel carrier",
     .side = Side::NATO, .cls = UnitClass::APC, .style = SpriteStyle::APC, .tracked = true,
     .amphibious = true, .roundTurret = false, .speed = 16, .roadSpeed = 26,
     .turnRate = 90 * kDeg, .turretRate = 90 * kDeg, .armorFront = 30, .armorSide = 25,
     .optics = 1000, .size = 0.95f,
     .gun = {.name = ".50 cal M2HB", .range = 450, .pen = 25, .reload = 0.35f,
             .accuracy = 0.45f, .killChance = 0.2f, .ammo = -1, .light = true},
     .atgm = {}, .smokeCharges = 0, .points = 15, .length = 24, .width = 13,
     .paint = kMerdcGreen, .camo = kMerdcBrown,
     .blurb = "The aluminium battle taxi. It can swim rivers, but keep it away from tanks."},
    {.name = "M901 ITV", .nation = "USA", .role = "Improved TOW Vehicle", .side = Side::NATO,
     .cls = UnitClass::AntiTank, .style = SpriteStyle::ITV, .tracked = true,
     .amphibious = true, .roundTurret = false, .speed = 16, .roadSpeed = 26,
     .turnRate = 90 * kDeg, .turretRate = 60 * kDeg, .armorFront = 30, .armorSide = 25,
     .optics = 1450, .size = 0.95f, .gun = {},
     .atgm = {.name = "TOW-2 (hammerhead)", .range = 1500, .pen = 800, .reload = 11.0f,
              .accuracy = 0.9f, .killChance = 0.9f, .ammo = 12, .missile = true},
     .smokeCharges = 0, .points = 25, .length = 24, .width = 13,
     .paint = kMerdcGreen, .camo = kMerdcBrown,
     .blurb = "An M113 with an armoured TOW launcher. It can fire from behind a crest and "
              "kill any tank."},
    {.name = "Spz Luchs", .nation = "West Germany", .role = "Armoured reconnaissance",
     .side = Side::NATO, .cls = UnitClass::Recon, .style = SpriteStyle::Wheeled8,
     .tracked = false, .amphibious = true, .roundTurret = true, .speed = 15, .roadSpeed = 38,
     .turnRate = 70 * kDeg, .turretRate = 90 * kDeg, .armorFront = 25, .armorSide = 15,
     .optics = 1700, .size = 0.85f,
     .gun = {.name = "20mm Rh 202", .range = 600, .pen = 45, .reload = 0.4f, .accuracy = 0.5f,
             .killChance = 0.2f, .ammo = -1, .stabilized = false, .light = true},
     .atgm = {}, .smokeCharges = 1, .points = 20, .length = 30, .width = 13,
     .paint = kFrgGreen, .camo = kMerdcBrown,
     .blurb = "Quiet, fast 8x8 scout that can swim. Spot the enemy and then get out of the "
              "way."},

    // ---------------- Warsaw Pact ----------------
    {.name = "T-80B", .nation = "USSR", .role = "Main battle tank", .side = Side::WP,
     .cls = UnitClass::Tank, .style = SpriteStyle::Tank, .tracked = true, .amphibious = false,
     .roundTurret = true, .speed = 18, .roadSpeed = 30, .turnRate = 75 * kDeg,
     .turretRate = 45 * kDeg, .armorFront = 450, .armorSide = 85, .optics = 1100,
     .size = 0.9f,
     .gun = {.name = "125mm 2A46-2", .range = 950, .pen = 450, .reload = 7.0f,
             .accuracy = 0.72f, .killChance = 0.85f, .ammo = 38, .stabilized = true},
     .atgm = {.name = "9M112 Kobra", .range = 1150, .pen = 500, .reload = 15.0f,
              .accuracy = 0.75f, .killChance = 0.85f, .ammo = 4, .missile = true},
     .smokeCharges = 1, .points = 45, .length = 31, .width = 15,
     .paint = kSovietGreen, .camo = kSovietDark,
     .blurb = "Gas-turbine spearhead of the Group of Soviet Forces in Germany. It can fire "
              "missiles through its gun."},
    {.name = "T-72A", .nation = "USSR", .role = "Main battle tank", .side = Side::WP,
     .cls = UnitClass::Tank, .style = SpriteStyle::Tank, .tracked = true, .amphibious = false,
     .roundTurret = true, .speed = 15, .roadSpeed = 25, .turnRate = 75 * kDeg,
     .turretRate = 45 * kDeg, .armorFront = 410, .armorSide = 80, .optics = 1050,
     .size = 0.9f,
     .gun = {.name = "125mm 2A46", .range = 950, .pen = 420, .reload = 7.5f, .accuracy = 0.7f,
             .killChance = 0.85f, .ammo = 44, .stabilized = true},
     .atgm = {}, .smokeCharges = 1, .points = 35, .length = 31, .width = 15,
     .paint = kSovietGreen, .camo = kSovietDark,
     .blurb = "A low, rugged workhorse built in huge numbers. Its autoloader has a three-man "
              "crew."},
    {.name = "T-62M", .nation = "USSR", .role = "Main battle tank", .side = Side::WP,
     .cls = UnitClass::Tank, .style = SpriteStyle::Tank, .tracked = true, .amphibious = false,
     .roundTurret = true, .speed = 13, .roadSpeed = 22, .turnRate = 70 * kDeg,
     .turretRate = 40 * kDeg, .armorFront = 280, .armorSide = 65, .optics = 1000,
     .size = 0.95f,
     .gun = {.name = "115mm U-5TS", .range = 900, .pen = 330, .reload = 8.0f,
             .accuracy = 0.65f, .killChance = 0.85f, .ammo = 40, .stabilized = false},
     .atgm = {}, .smokeCharges = 0, .points = 22, .length = 30, .width = 15,
     .paint = kSovietGreen, .camo = kSovietDark,
     .blurb = "A second-echelon tank with add-on 'eyebrow' armour. There are a lot of them."},
    {.name = "BMP-2", .nation = "USSR", .role = "Infantry fighting vehicle", .side = Side::WP,
     .cls = UnitClass::IFV, .style = SpriteStyle::IFV, .tracked = true, .amphibious = true,
     .roundTurret = true, .speed = 16, .roadSpeed = 28, .turnRate = 85 * kDeg,
     .turretRate = 70 * kDeg, .armorFront = 40, .armorSide = 25, .optics = 1000,
     .size = 0.9f,
     .gun = {.name = "30mm 2A42", .range = 700, .pen = 70, .reload = 0.45f, .accuracy = 0.5f,
             .killChance = 0.25f, .ammo = -1, .stabilized = true, .light = true},
     .atgm = {.name = "9M113 Konkurs", .range = 1300, .pen = 600, .reload = 15.0f,
              .accuracy = 0.8f, .killChance = 0.85f, .ammo = 4, .missile = true},
     .smokeCharges = 1, .points = 20, .length = 27, .width = 13,
     .paint = kSovietGreen, .camo = kSovietDark,
     .blurb = "Swims rivers and carries a fast-firing 30mm cannon and an AT-5 Spandrel "
              "launcher."},
    {.name = "BTR-70", .nation = "USSR", .role = "Armoured personnel carrier", .side = Side::WP,
     .cls = UnitClass::APC, .style = SpriteStyle::Wheeled8, .tracked = false,
     .amphibious = true, .roundTurret = true, .speed = 14, .roadSpeed = 34,
     .turnRate = 70 * kDeg, .turretRate = 80 * kDeg, .armorFront = 15, .armorSide = 10,
     .optics = 900, .size = 1.0f,
     .gun = {.name = "14.5mm KPVT", .range = 550, .pen = 35, .reload = 0.35f,
             .accuracy = 0.45f, .killChance = 0.2f, .ammo = -1, .light = true},
     .atgm = {}, .smokeCharges = 0, .points = 12, .length = 30, .width = 12,
     .paint = kSovietGreen, .camo = kSovietDark,
     .blurb = "An eight-wheeled motor-rifle carrier. Fast on roads and fragile in a fight."},
    {.name = "BRDM-2 AT-5", .nation = "USSR", .role = "Tank destroyer / scout", .side = Side::WP,
     .cls = UnitClass::AntiTank, .style = SpriteStyle::Wheeled4, .tracked = false,
     .amphibious = true, .roundTurret = false, .speed = 15, .roadSpeed = 34,
     .turnRate = 75 * kDeg, .turretRate = 70 * kDeg, .armorFront = 12, .armorSide = 10,
     .optics = 1300, .size = 0.75f, .gun = {},
     .atgm = {.name = "9M113 Konkurs x5", .range = 1300, .pen = 600, .reload = 10.0f,
              .accuracy = 0.8f, .killChance = 0.85f, .ammo = 14, .missile = true},
     .smokeCharges = 0, .points = 18, .length = 24, .width = 11,
     .paint = kSovietGreen, .camo = kSovietDark,
     .blurb = "A small scout car with five Spandrel missiles on a pop-up launcher."},
    {.name = "ZSU-23-4 Shilka", .nation = "USSR", .role = "Self-propelled AA gun",
     .side = Side::WP, .cls = UnitClass::AirDefense, .style = SpriteStyle::Shilka,
     .tracked = true, .amphibious = false, .roundTurret = false, .speed = 15,
     .roadSpeed = 25, .turnRate = 80 * kDeg, .turretRate = 90 * kDeg, .armorFront = 15,
     .armorSide = 12, .optics = 1050, .size = 1.0f,
     .gun = {.name = "4x 23mm AZP-23", .range = 750, .pen = 40, .reload = 0.3f,
             .accuracy = 0.55f, .killChance = 0.3f, .ammo = -1, .stabilized = true,
             .light = true},
     .atgm = {}, .smokeCharges = 0, .points = 20, .length = 27, .width = 14,
     .paint = kSovietGreen, .camo = kSovietDark,
     .blurb = "Radar-guided quad cannon built to shoot down aircraft. Against light armour "
              "it is just as deadly."},
};

}  // namespace

const VehicleType& vehicleType(int id) { return kTypes[id]; }

const char* className(UnitClass c) {
  switch (c) {
    case UnitClass::Tank: return "Tank";
    case UnitClass::IFV: return "IFV";
    case UnitClass::APC: return "APC";
    case UnitClass::Recon: return "Recon";
    case UnitClass::AntiTank: return "Anti-tank";
    case UnitClass::AirDefense: return "Air defence";
  }
  return "";
}
