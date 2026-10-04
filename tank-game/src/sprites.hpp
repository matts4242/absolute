// Procedural top-down vehicle art and NATO (APP-6) map symbols.
#pragma once

#include "vehicles.hpp"

// Draws a vehicle centred on pos. Angles are radians; scale multiplies the type's size.
void drawVehicle(const VehicleType& vt, Vector2 pos, float hull, float turret, float scale, uint32_t camo,
                 bool wreck);

// Draws a NATO-style unit symbol: blue rectangle for friendly, red diamond for hostile.
void drawCounter(const VehicleType& vt, Vector2 center, float size, unsigned char alpha);

Color shadeColor(Color c, int delta);
