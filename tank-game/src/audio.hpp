// Battle sounds synthesised at startup (no audio files needed).
#pragma once

#include <vector>

#include "game.hpp"

class AudioBank {
 public:
  void init();
  void shutdown();
  // volume 0..1, pan -1 (left) .. 1 (right)
  void play(SoundKind kind, float volume, float pan);

 private:
  static constexpr int kKinds = 5;
  static constexpr int kVoices = 6;
  bool ready_ = false;
  Sound base_[kKinds]{};
  std::vector<Sound> voices_[kKinds];
  int next_[kKinds]{};
};
