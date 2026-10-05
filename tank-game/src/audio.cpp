#include "audio.hpp"

#include <algorithm>
#include <cmath>
#include <random>

namespace {

constexpr int kRate = 22050;

// Builds a mono 16-bit wave by sampling fn(t, noise) for the given duration.
template <typename Fn>
Wave synth(float seconds, Fn fn) {
  int n = static_cast<int>(seconds * kRate);
  auto* data = static_cast<short*>(MemAlloc(n * sizeof(short)));
  std::mt19937 eng(1234);
  std::uniform_real_distribution<float> noise(-1.0f, 1.0f);
  for (int i = 0; i < n; ++i) {
    float t = static_cast<float>(i) / kRate;
    float v = fn(t, noise(eng));
    float fadeOut = std::min(1.0f, (seconds - t) * 20.0f);  // avoid a click at the end
    data[i] = static_cast<short>(std::clamp(v * fadeOut, -1.0f, 1.0f) * 30000.0f);
  }
  Wave w{};
  w.frameCount = static_cast<unsigned int>(n);
  w.sampleRate = kRate;
  w.sampleSize = 16;
  w.channels = 1;
  w.data = data;
  return w;
}

struct LowPass {
  float a, y = 0;
  float operator()(float x) { return y += a * (x - y); }
};

}  // namespace

void AudioBank::init() {
  InitAudioDevice();
  if (!IsAudioDeviceReady()) return;
  const float twoPi = 2.0f * PI;

  LowPass cannonLp{0.12f};
  Wave cannon = synth(1.0f, [&](float t, float n) {
    return cannonLp(n) * std::exp(-t * 5.0f) * 1.8f + std::sin(twoPi * 52.0f * t) * std::exp(-t * 8.0f) * 0.9f;
  });
  LowPass autoLp{0.35f};
  Wave autocannon = synth(0.16f, [&](float t, float n) {
    return autoLp(n) * std::exp(-t * 32.0f) * 1.2f + std::sin(twoPi * 140.0f * t) * std::exp(-t * 45.0f) * 0.5f;
  });
  LowPass missileLp{0.2f};
  Wave missile = synth(1.1f, [&](float t, float n) {
    return missileLp(n) * std::min(1.0f, t * 10.0f) * std::exp(-t * 1.6f) * 0.9f;
  });
  LowPass boomLp{0.05f};
  Wave explosion = synth(1.8f, [&](float t, float n) {
    return boomLp(n) * std::exp(-t * 2.2f) * 3.2f + std::sin(twoPi * 38.0f * t) * std::exp(-t * 4.0f) * 0.7f;
  });
  LowPass artyLp{0.08f};
  Wave artillery = synth(1.4f, [&](float t, float n) {
    return artyLp(n) * std::exp(-t * 3.0f) * 2.6f + n * std::exp(-t * 50.0f) * 0.6f;
  });

  Wave waves[kKinds] = {cannon, autocannon, missile, explosion, artillery};
  for (int k = 0; k < kKinds; ++k) {
    base_[k] = LoadSoundFromWave(waves[k]);
    UnloadWave(waves[k]);
    for (int v = 0; v < kVoices; ++v) voices_[k].push_back(LoadSoundAlias(base_[k]));
  }
  ready_ = true;
}

void AudioBank::shutdown() {
  if (ready_) {
    for (int k = 0; k < kKinds; ++k) {
      for (Sound& s : voices_[k]) UnloadSoundAlias(s);
      UnloadSound(base_[k]);
    }
    ready_ = false;
  }
  if (IsAudioDeviceReady()) CloseAudioDevice();
}

void AudioBank::play(SoundKind kind, float volume, float pan) {
  if (!ready_ || volume <= 0.01f) return;
  int k = static_cast<int>(kind);
  Sound& s = voices_[k][next_[k]];
  next_[k] = (next_[k] + 1) % kVoices;
  SetSoundVolume(s, std::clamp(volume, 0.0f, 1.0f));
  SetSoundPan(s, std::clamp(0.5f - pan * 0.4f, 0.0f, 1.0f));
  SetSoundPitch(s, 0.92f + static_cast<float>(GetRandomValue(0, 16)) / 100.0f);
  PlaySound(s);
}
