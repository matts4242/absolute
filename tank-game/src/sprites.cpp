#include "sprites.hpp"

#include <algorithm>

namespace {

void quad(Vector2 c, float ang, float len, float wid, Color col) {
  DrawRectanglePro({c.x, c.y, len, wid}, {len / 2, wid / 2}, ang * RAD2DEG, col);
}

void tri(Vector2 a, Vector2 b, Vector2 c, Color col) {
  // Submit both windings so the triangle shows regardless of back-face culling.
  DrawTriangle(a, b, c, col);
  DrawTriangle(a, c, b, col);
}

struct Hash {
  uint32_t s;
  float next() {
    s ^= s << 13;
    s ^= s >> 17;
    s ^= s << 5;
    return (s & 0xffff) / 65535.0f;
  }
  float range(float lo, float hi) { return lo + (hi - lo) * next(); }
};

void ellipse(Vector2 c, float rx, float ry, float thick, Color col) {
  const int n = 28;
  for (int i = 0; i < n; ++i) {
    float a0 = 2 * PI * i / n, a1 = 2 * PI * (i + 1) / n;
    DrawLineEx({c.x + cosf(a0) * rx, c.y + sinf(a0) * ry}, {c.x + cosf(a1) * rx, c.y + sinf(a1) * ry}, thick, col);
  }
}

}  // namespace

Color shadeColor(Color c, int delta) {
  auto ch = [&](unsigned char v) { return static_cast<unsigned char>(std::clamp(v + delta, 0, 255)); };
  return {ch(c.r), ch(c.g), ch(c.b), c.a};
}

void drawVehicle(const VehicleType& vt, Vector2 pos, float hull, float turret, float scale, uint32_t camo,
                 bool wreck) {
  const float L = vt.length * scale, W = vt.width * scale;
  const Color paint = wreck ? Color{56, 50, 44, 255} : vt.paint;
  const Color camoC = wreck ? Color{40, 36, 32, 255} : vt.camo;
  const Color dark = shadeColor(paint, -32);
  const Color light = shadeColor(paint, 22);
  const Color track = wreck ? Color{28, 26, 24, 255} : Color{40, 38, 34, 255};
  const Color metal = shadeColor(paint, -48);
  auto at = [&](float f, float r) { return localPoint(pos, hull, f * L, r * W); };

  if (wreck) DrawCircleV(pos, L * 0.6f, {18, 16, 14, 90});  // scorch mark
  quad(pos + Vector2{2.0f * scale, 3.0f * scale}, hull, L, W, {0, 0, 0, 70});  // drop shadow

  const bool wheeled = vt.style == SpriteStyle::Wheeled4 || vt.style == SpriteStyle::Wheeled8;
  if (!wheeled) {
    // Tracks with link detail.
    const float tw = W * 0.24f;
    for (float s : {-1.0f, 1.0f}) {
      Vector2 c = localPoint(pos, hull, 0, s * (W / 2 - tw / 2));
      quad(c, hull, L, tw, track);
      for (float f = -0.48f; f <= 0.48f; f += 3.5f / vt.length) {
        Vector2 a = localPoint(c, hull, f * L, -tw / 2), b = localPoint(c, hull, f * L, tw / 2);
        DrawLineEx(a, b, std::max(0.6f, 0.5f * scale), {22, 20, 18, 255});
      }
    }
  } else {
    // Road wheels on each side.
    const int perSide = vt.style == SpriteStyle::Wheeled8 ? 4 : 2;
    for (int i = 0; i < perSide; ++i) {
      float f = perSide == 4 ? -0.32f + i * 0.2f : (i == 0 ? -0.26f : 0.24f);
      for (float s : {-1.0f, 1.0f}) quad(at(f, s * 0.44f), hull, L * 0.13f, W * 0.18f, track);
    }
  }

  // Hull.
  switch (vt.style) {
    case SpriteStyle::Tank:
      quad(pos, hull, L * 0.96f, W * 0.56f, paint);
      quad(at(0.41f, 0), hull, L * 0.12f, W * 0.56f, light);           // glacis
      quad(at(-0.36f, 0), hull, L * 0.2f, W * 0.38f, dark);            // engine deck
      for (float r = -0.15f; r <= 0.16f; r += 0.075f) {                // grille slats
        DrawLineEx(at(-0.44f, r), at(-0.28f, r), std::max(0.5f, 0.4f * scale), metal);
      }
      break;
    case SpriteStyle::IFV:
      quad(pos, hull, L * 0.96f, W * 0.6f, paint);
      quad(at(0.38f, 0), hull, L * 0.18f, W * 0.6f, light);
      DrawLineEx(at(-0.45f, -0.25f), at(-0.45f, 0.25f), std::max(0.8f, scale), dark);  // rear ramp
      break;
    case SpriteStyle::APC:
    case SpriteStyle::ITV:
      quad(pos, hull, L * 0.96f, W * 0.62f, paint);
      quad(at(0.4f, 0), hull, L * 0.14f, W * 0.62f, light);
      quad(at(-0.18f, 0), hull, L * 0.3f, W * 0.34f, dark);  // cargo hatch
      break;
    case SpriteStyle::Shilka:
      quad(pos, hull, L * 0.96f, W * 0.58f, paint);
      quad(at(0.4f, 0), hull, L * 0.14f, W * 0.58f, light);
      break;
    case SpriteStyle::Wheeled8:
    case SpriteStyle::Wheeled4: {
      quad(at(-0.02f, 0), hull, L * 0.8f, W * 0.72f, paint);
      tri(at(0.38f, -0.36f), at(0.38f, 0.36f), at(0.5f, 0.0f), light);    // boat nose
      tri(at(-0.42f, -0.36f), at(-0.42f, 0.36f), at(-0.49f, 0.0f), paint);
      tri(at(0.38f, -0.36f), at(0.5f, 0.0f), at(0.38f, 0.36f), light);
      break;
    }
  }

  // Camouflage blotches.
  if (!wreck) {
    Hash h{camo | 1u};
    int blotches = vt.side == Side::NATO ? 5 : 2;
    for (int i = 0; i < blotches; ++i) {
      Vector2 p = at(h.range(-0.35f, 0.3f), h.range(-0.16f, 0.16f));
      Color c = (vt.side == Side::NATO && i % 3 == 2) ? Color{34, 32, 28, 255} : camoC;
      DrawCircleV(p, W * h.range(0.09f, 0.15f), c);
    }
  }

  // Soviet tanks carry external fuel drums on the rear deck.
  if (vt.side == Side::WP && vt.style == SpriteStyle::Tank) {
    DrawCircleV(at(-0.45f, -0.16f), W * 0.09f, dark);
    DrawCircleV(at(-0.45f, 0.16f), W * 0.09f, dark);
  }

  // Turrets and weapons.
  const float barrelW = std::max(1.2f, W * 0.085f);
  auto barrel = [&](Vector2 base, float len, float wid) {
    quad(localPoint(base, turret, len / 2, 0), turret, len, wid, metal);
  };
  switch (vt.style) {
    case SpriteStyle::Tank: {
      Vector2 tc = at(-0.04f, 0);
      if (vt.roundTurret) {
        DrawCircleV(tc + Vector2{scale, scale}, W * 0.34f, {0, 0, 0, 60});
        DrawCircleV(tc, W * 0.34f, paint);
        DrawCircleV(localPoint(tc, turret, -W * 0.06f, -W * 0.06f), W * 0.2f, light);
        barrel(localPoint(tc, turret, W * 0.25f, 0), L * 0.66f, barrelW);
        quad(localPoint(tc, turret, W * 0.25f + L * 0.3f, 0), turret, L * 0.08f, barrelW * 1.5f, metal);
        DrawCircleV(localPoint(tc, turret, -W * 0.05f, W * 0.15f), W * 0.08f, dark);  // cupola
      } else {
        quad(localPoint(tc, turret, -L * 0.2f, 0), turret, L * 0.16f, W * 0.48f, dark);  // bustle
        quad(tc, turret, L * 0.42f, W * 0.6f, paint);
        quad(localPoint(tc, turret, L * 0.16f, 0), turret, L * 0.1f, W * 0.6f, light);
        barrel(localPoint(tc, turret, L * 0.2f, 0), L * 0.6f, barrelW);
        quad(localPoint(tc, turret, L * 0.2f + L * 0.3f, 0), turret, L * 0.07f, barrelW * 1.5f, metal);
        DrawCircleV(localPoint(tc, turret, -L * 0.04f, W * 0.15f), W * 0.08f, dark);
        DrawCircleV(localPoint(tc, turret, -L * 0.02f, -W * 0.15f), W * 0.06f, dark);
      }
      break;
    }
    case SpriteStyle::IFV: {
      if (vt.roundTurret) {  // BMP-2
        Vector2 tc = at(0.02f, 0);
        DrawCircleV(tc, W * 0.26f, paint);
        DrawCircleV(localPoint(tc, turret, -W * 0.05f, 0), W * 0.14f, light);
        barrel(localPoint(tc, turret, W * 0.2f, 0), L * 0.4f, barrelW * 0.8f);
        quad(localPoint(tc, turret, -W * 0.05f, -W * 0.26f), turret, L * 0.24f, W * 0.08f, dark);  // AT-5 tube
      } else {  // Bradley
        Vector2 tc = at(0.0f, 0.06f);
        quad(tc, turret, L * 0.3f, W * 0.44f, paint);
        quad(localPoint(tc, turret, L * 0.1f, 0), turret, L * 0.08f, W * 0.44f, light);
        barrel(localPoint(tc, turret, L * 0.14f, 0.05f * W), L * 0.34f, barrelW * 0.8f);
        quad(localPoint(tc, turret, -L * 0.02f, -W * 0.29f), turret, L * 0.24f, W * 0.13f, dark);  // TOW box
      }
      break;
    }
    case SpriteStyle::APC: {
      Vector2 tc = at(0.12f, -0.12f);
      DrawCircleV(tc, W * 0.12f, dark);
      barrel(tc, L * 0.3f, std::max(0.9f, barrelW * 0.6f));
      break;
    }
    case SpriteStyle::ITV: {
      Vector2 tc = at(0.08f, 0);
      DrawCircleV(tc, W * 0.13f, dark);
      quad(tc, turret, L * 0.18f, W * 0.62f, paint);  // hammerhead
      quad(localPoint(tc, turret, L * 0.1f, -W * 0.2f), turret, L * 0.06f, W * 0.14f, metal);
      quad(localPoint(tc, turret, L * 0.1f, W * 0.2f), turret, L * 0.06f, W * 0.14f, metal);
      break;
    }
    case SpriteStyle::Shilka: {
      Vector2 tc = at(-0.02f, 0);
      quad(tc, turret, L * 0.44f, W * 0.62f, paint);
      quad(localPoint(tc, turret, L * 0.16f, 0), turret, L * 0.1f, W * 0.62f, light);
      for (float r : {-0.22f, -0.12f, 0.12f, 0.22f}) {
        quad(localPoint(tc, turret, L * 0.22f + L * 0.2f, r * W), turret, L * 0.4f, std::max(0.8f, W * 0.05f), metal);
      }
      DrawCircleV(localPoint(tc, turret, -L * 0.2f, 0), W * 0.17f, light);  // radar dish
      DrawCircleLinesV(localPoint(tc, turret, -L * 0.2f, 0), W * 0.17f, dark);
      break;
    }
    case SpriteStyle::Wheeled8: {
      Vector2 tc = at(0.1f, 0);
      DrawCircleV(tc, W * 0.2f, paint);
      DrawCircleV(localPoint(tc, turret, -W * 0.04f, 0), W * 0.11f, light);
      barrel(localPoint(tc, turret, W * 0.15f, 0), L * 0.3f, barrelW * 0.7f);
      break;
    }
    case SpriteStyle::Wheeled4: {
      Vector2 tc = at(-0.05f, 0);
      quad(tc, turret, L * 0.2f, W * 0.52f, dark);
      for (int k = -2; k <= 2; ++k) {
        quad(localPoint(tc, turret, L * 0.12f, k * W * 0.1f), turret, L * 0.34f, std::max(0.8f, W * 0.06f), metal);
      }
      break;
    }
  }

  if (wreck) {  // soot over everything
    DrawCircleV(at(0.0f, 0.0f), W * 0.3f, {20, 18, 16, 120});
  }
}

void drawCounter(const VehicleType& vt, Vector2 c, float size, unsigned char alpha) {
  const bool friendly = vt.side == Side::NATO;
  const Color frame = {0, 0, 0, alpha};
  const Color fill = friendly ? Color{128, 224, 255, alpha} : Color{255, 128, 128, alpha};
  const float thick = std::max(1.0f, size * 0.08f);
  float iw, ih;  // icon box

  if (friendly) {
    float w = size * 1.5f, h = size;
    DrawRectangleRec({c.x - w / 2, c.y - h / 2, w, h}, fill);
    DrawRectangleLinesEx({c.x - w / 2, c.y - h / 2, w, h}, thick, frame);
    iw = w;
    ih = h;
  } else {
    float r = size * 0.8f;
    Vector2 top{c.x, c.y - r}, right{c.x + r, c.y}, bottom{c.x, c.y + r}, left{c.x - r, c.y};
    tri(top, left, right, fill);
    tri(bottom, right, left, fill);
    DrawLineEx(top, right, thick, frame);
    DrawLineEx(right, bottom, thick, frame);
    DrawLineEx(bottom, left, thick, frame);
    DrawLineEx(left, top, thick, frame);
    iw = r;
    ih = r * 0.66f;
  }
  const float x0 = c.x - iw / 2, x1 = c.x + iw / 2, y0 = c.y - ih / 2, y1 = c.y + ih / 2;
  const float lt = std::max(1.0f, size * 0.07f);

  auto armourOval = [&]() { ellipse(c, iw * 0.3f, ih * 0.22f, lt, frame); };
  switch (vt.cls) {
    case UnitClass::Tank:
      ellipse(c, iw * 0.36f, ih * 0.26f, lt, frame);
      break;
    case UnitClass::IFV:
    case UnitClass::APC:
      DrawLineEx({x0, y0}, {x1, y1}, lt, frame);  // infantry cross
      DrawLineEx({x0, y1}, {x1, y0}, lt, frame);
      armourOval();
      break;
    case UnitClass::Recon:
      DrawLineEx({x0, y1}, {x1, y0}, lt, frame);  // cavalry slash
      armourOval();
      break;
    case UnitClass::AntiTank:
      DrawLineEx({x0, y1}, {c.x, y0}, lt, frame);  // inverted V
      DrawLineEx({x1, y1}, {c.x, y0}, lt, frame);
      armourOval();
      break;
    case UnitClass::AirDefense: {
      const int n = 12;  // dome
      for (int i = 0; i < n; ++i) {
        float a0 = PI + PI * i / n, a1 = PI + PI * (i + 1) / n;
        DrawLineEx({c.x + cosf(a0) * iw * 0.4f, y1 + sinf(a0) * ih * 0.5f},
                   {c.x + cosf(a1) * iw * 0.4f, y1 + sinf(a1) * ih * 0.5f}, lt, frame);
      }
      armourOval();
      break;
    }
  }
  if (!vt.tracked) {  // wheeled mobility indicator
    float yb = friendly ? c.y + size / 2 + size * 0.18f : c.y + size * 0.8f + size * 0.12f;
    DrawCircleLinesV({c.x - size * 0.22f, yb}, size * 0.1f, frame);
    DrawCircleLinesV({c.x + size * 0.22f, yb}, size * 0.1f, frame);
  }
}
