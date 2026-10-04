// Tank Battle — a terminal tank game in a single C++17 file.
//
// Build:  g++ -std=c++17 -O2 -Wall -Wextra -o tank tank.cpp
// Run:    ./tank
//
// Runs in Linux and macOS terminals (termios + ANSI escape codes, no libraries).
// On Windows, build and run it inside WSL.

#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>

#include <algorithm>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <random>
#include <string>
#include <thread>
#include <vector>

namespace {

// ---------- Tunables ----------
constexpr int kWidth = 31;   // map cells; each cell is drawn 2 columns wide
constexpr int kHeight = 21;
constexpr int kTickMs = 50;  // 20 updates per second
constexpr int kPlayerMoveCd = 2;
constexpr int kPlayerFireCd = 6;
constexpr int kRespawnTicks = 30;
constexpr int kInvulnTicks = 60;
constexpr int kStartLives = 3;
constexpr int kStartX = kWidth / 2;
constexpr int kStartY = kHeight - 2;
constexpr int kSpawnXs[] = {1, kWidth / 2, kWidth - 2};

enum Dir { kUp, kRight, kDown, kLeft };
constexpr int kDx[] = {0, 1, 0, -1};
constexpr int kDy[] = {-1, 0, 1, 0};
constexpr const char* kTankGlyph[] = {"/\\", "=>", "\\/", "<="};

constexpr char kEmpty = ' ', kBrick = '#', kSteel = '@';

struct Tank {
  int x, y;
  Dir dir;
  int moveCd = 0, fireCd = 0, invuln = 0;
  bool alive = true;
};

struct Bullet {
  int x, y, px, py;  // current and previous position
  Dir dir;
  bool fromPlayer;
  bool alive = true;
};

struct Effect {
  int x, y, ttl;
};

enum class Key { kUp, kDown, kLeft, kRight, kFire, kPause, kQuit, kRestart };

// ---------- Terminal ----------
termios g_original;
bool g_raw = false;

void writeAll(const std::string& s) {
  const char* p = s.data();
  size_t left = s.size();
  while (left > 0) {
    ssize_t n = write(STDOUT_FILENO, p, left);
    if (n <= 0) return;
    p += n;
    left -= static_cast<size_t>(n);
  }
}

void restoreTerminal() {
  if (!g_raw) return;
  g_raw = false;
  tcsetattr(STDIN_FILENO, TCSAFLUSH, &g_original);
  static const char kReset[] = "\x1b[0m\x1b[?25h\x1b[?1049l";
  if (write(STDOUT_FILENO, kReset, sizeof kReset - 1) < 0) {
  }
}

void onSignal(int) {
  restoreTerminal();
  std::_Exit(130);
}

void enterRawMode() {
  tcgetattr(STDIN_FILENO, &g_original);
  termios raw = g_original;
  raw.c_lflag &= ~static_cast<tcflag_t>(ICANON | ECHO);
  raw.c_cc[VMIN] = 0;  // non-blocking reads
  raw.c_cc[VTIME] = 0;
  tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
  g_raw = true;
  std::atexit(restoreTerminal);
  std::signal(SIGINT, onSignal);
  std::signal(SIGTERM, onSignal);
  writeAll("\x1b[?1049h\x1b[?25l\x1b[2J");  // alt screen, hide cursor, clear
}

std::vector<Key> readKeys() {
  std::vector<Key> keys;
  char buf[64];
  ssize_t n;
  while ((n = read(STDIN_FILENO, buf, sizeof buf)) > 0) {
    for (ssize_t i = 0; i < n; ++i) {
      if (buf[i] == '\x1b' && i + 2 < n && buf[i + 1] == '[') {
        switch (buf[i + 2]) {
          case 'A': keys.push_back(Key::kUp); break;
          case 'B': keys.push_back(Key::kDown); break;
          case 'C': keys.push_back(Key::kRight); break;
          case 'D': keys.push_back(Key::kLeft); break;
        }
        i += 2;
        continue;
      }
      switch (buf[i]) {
        case 'w': case 'W': keys.push_back(Key::kUp); break;
        case 's': case 'S': keys.push_back(Key::kDown); break;
        case 'a': case 'A': keys.push_back(Key::kLeft); break;
        case 'd': case 'D': keys.push_back(Key::kRight); break;
        case ' ': keys.push_back(Key::kFire); break;
        case 'p': case 'P': keys.push_back(Key::kPause); break;
        case 'q': case 'Q': keys.push_back(Key::kQuit); break;
        case 'r': case 'R': keys.push_back(Key::kRestart); break;
      }
    }
  }
  return keys;
}

// ---------- Game ----------
class Game {
 public:
  Game() : rng_(std::random_device{}()) { newGame(); }

  bool quit() const { return quit_; }
  int score() const { return score_; }
  int level() const { return level_; }

  void handle(Key k) {
    switch (k) {
      case Key::kUp: wantDir_ = kUp; break;
      case Key::kDown: wantDir_ = kDown; break;
      case Key::kLeft: wantDir_ = kLeft; break;
      case Key::kRight: wantDir_ = kRight; break;
      case Key::kFire: wantFire_ = true; break;
      case Key::kPause: if (!over_) paused_ = !paused_; break;
      case Key::kQuit: quit_ = true; break;
      case Key::kRestart: if (over_) newGame(); break;
    }
  }

  void update() {
    ++tick_;
    if (paused_ || over_) return;
    if (banner_ > 0) --banner_;

    updatePlayer();
    for (size_t i = 1; i < tanks_.size(); ++i) {
      if (tanks_[i].alive) updateEnemy(i);
    }
    hitTanksUnderBullets();
    moveBullets();
    for (auto& fx : effects_) --fx.ttl;

    bullets_.erase(std::remove_if(bullets_.begin(), bullets_.end(),
                                  [](const Bullet& b) { return !b.alive; }),
                   bullets_.end());
    effects_.erase(std::remove_if(effects_.begin(), effects_.end(),
                                  [](const Effect& e) { return e.ttl <= 0; }),
                   effects_.end());
    tanks_.erase(std::remove_if(tanks_.begin() + 1, tanks_.end(),
                                [](const Tank& t) { return !t.alive; }),
                 tanks_.end());

    spawnEnemies();
    if (!over_ && toSpawn_ == 0 && enemiesAlive() == 0) {
      score_ += 500 * level_;
      ++level_;
      buildLevel();
    }
  }

  std::string render() const {
    std::vector<std::string> cell(kWidth * kHeight);
    for (int y = 0; y < kHeight; ++y) {
      for (int x = 0; x < kWidth; ++x) {
        char c = map_[y][x];
        cell[y * kWidth + x] = c == kSteel   ? "\x1b[30;47m[]"
                               : c == kBrick ? "\x1b[31;43m##"
                                             : "  ";
      }
    }
    for (const auto& fx : effects_) cell[fx.y * kWidth + fx.x] = "\x1b[1;31;103m%%";
    for (const auto& b : bullets_) {
      cell[b.y * kWidth + b.x] = b.fromPlayer ? "\x1b[1;92m()" : "\x1b[1;93m()";
    }
    for (size_t i = 0; i < tanks_.size(); ++i) {
      const Tank& t = tanks_[i];
      if (!t.alive) continue;
      std::string color;
      if (i != 0) {
        color = "\x1b[1;97;41m";
      } else if (t.invuln > 0 && (tick_ / 3) % 2) {
        color = "\x1b[1;32m";  // blink while invulnerable
      } else {
        color = "\x1b[1;30;42m";
      }
      cell[t.y * kWidth + t.x] = color + kTankGlyph[t.dir];
    }

    std::string out = "\x1b[H";
    char buf[256];
    std::snprintf(buf, sizeof buf,
                  "\x1b[0m\x1b[1m TANK BATTLE\x1b[0m   Level %d   Score %d   Lives %d"
                  "   Enemies left %d\x1b[K\n",
                  level_, score_, lives_, toSpawn_ + enemiesAlive());
    out += buf;
    for (int y = 0; y < kHeight; ++y) {
      for (int x = 0; x < kWidth; ++x) {
        out += "\x1b[0m";
        out += cell[y * kWidth + x];
      }
      out += "\x1b[0m\x1b[K\n";
    }
    if (over_) {
      std::snprintf(buf, sizeof buf,
                    "\x1b[1;31m GAME OVER\x1b[0m  Final score %d  -  R: restart  Q: quit",
                    score_);
    } else if (paused_) {
      std::snprintf(buf, sizeof buf, "\x1b[1;33m PAUSED\x1b[0m  -  P: resume  Q: quit");
    } else if (banner_ > 0) {
      std::snprintf(buf, sizeof buf,
                    "\x1b[1;32m LEVEL %d\x1b[0m  -  destroy every enemy tank!", level_);
    } else {
      std::snprintf(buf, sizeof buf,
                    " WASD/arrows: move   Space: fire   P: pause   Q: quit");
    }
    out += buf;
    out += "\x1b[0m\x1b[K";
    return out;
  }

 private:
  // ----- Setup -----
  void newGame() {
    level_ = 1;
    score_ = 0;
    lives_ = kStartLives;
    over_ = paused_ = false;
    buildLevel();
  }

  void buildLevel() {
    map_.assign(kHeight, std::string(kWidth, kEmpty));
    for (int x = 0; x < kWidth; ++x) map_[0][x] = map_[kHeight - 1][x] = kSteel;
    for (int y = 0; y < kHeight; ++y) map_[y][0] = map_[y][kWidth - 1] = kSteel;

    // Brick clusters, mirrored left/right for a fair arena.
    int clusters = 10 + std::min(level_, 8);
    for (int k = 0; k < clusters; ++k) {
      int w = rnd(1, 3), h = rnd(1, 3);
      int x0 = rnd(1, kWidth / 2), y0 = rnd(3, kHeight - 4);
      for (int y = y0; y < y0 + h && y < kHeight - 1; ++y) {
        for (int x = x0; x < x0 + w && x <= kWidth / 2; ++x) {
          map_[y][x] = map_[y][kWidth - 1 - x] = kBrick;
        }
      }
    }
    // Isolated steel pillars: indestructible cover that can never wall anyone in.
    for (int y = 4; y < kHeight - 3; y += 4) {
      for (int x = 4; x <= kWidth / 2; x += 6) {
        if (chance(0.35)) map_[y][x] = map_[y][kWidth - 1 - x] = kSteel;
      }
    }
    for (int sx : kSpawnXs) clearAround(sx, 1);
    clearAround(kStartX, kStartY);

    tanks_.clear();
    bullets_.clear();
    effects_.clear();
    tanks_.push_back(Tank{kStartX, kStartY, kUp});
    tanks_[0].invuln = kInvulnTicks;
    toSpawn_ = 5 + 3 * level_;
    spawnCd_ = 20;
    banner_ = 50;
    wantDir_ = -1;
    wantFire_ = false;
  }

  void clearAround(int cx, int cy) {
    for (int y = cy - 1; y <= cy + 1; ++y) {
      for (int x = cx - 1; x <= cx + 1; ++x) {
        if (x > 0 && y > 0 && x < kWidth - 1 && y < kHeight - 1) map_[y][x] = kEmpty;
      }
    }
  }

  // ----- Difficulty curve -----
  int maxActive() const { return std::min(2 + level_, 6); }
  int enemyMoveCd() const { return std::max(2, 7 - level_); }
  int enemyFireCd() const { return std::max(12, 40 - 4 * level_); }

  // ----- Helpers -----
  int rnd(int lo, int hi) { return std::uniform_int_distribution<int>(lo, hi)(rng_); }
  bool chance(double p) { return std::bernoulli_distribution(p)(rng_); }

  int tankAt(int x, int y) const {
    for (size_t i = 0; i < tanks_.size(); ++i) {
      if (tanks_[i].alive && tanks_[i].x == x && tanks_[i].y == y) return static_cast<int>(i);
    }
    return -1;
  }

  int enemiesAlive() const {
    return static_cast<int>(std::count_if(tanks_.begin() + 1, tanks_.end(),
                                          [](const Tank& t) { return t.alive; }));
  }

  bool canMove(const Tank& t, Dir d) const {
    int nx = t.x + kDx[d], ny = t.y + kDy[d];
    return map_[ny][nx] == kEmpty && tankAt(nx, ny) < 0;
  }

  void tryMove(Tank& t) {
    if (canMove(t, t.dir)) {
      t.x += kDx[t.dir];
      t.y += kDy[t.dir];
    }
  }

  void addEffect(int x, int y, int ttl) { effects_.push_back({x, y, ttl}); }

  // ----- Player -----
  void updatePlayer() {
    Tank& p = tanks_[0];
    if (!p.alive) {
      if (lives_ > 0 && --respawn_ <= 0 && tankAt(kStartX, kStartY) < 0) {
        p = Tank{kStartX, kStartY, kUp};
        p.invuln = kInvulnTicks;
      }
    } else {
      if (p.invuln > 0) --p.invuln;
      if (p.moveCd > 0) --p.moveCd;
      if (p.fireCd > 0) --p.fireCd;
      if (wantDir_ >= 0 && p.moveCd == 0) {
        p.dir = static_cast<Dir>(wantDir_);
        tryMove(p);
        p.moveCd = kPlayerMoveCd;
      }
      if (wantFire_ && p.fireCd == 0) {
        fire(p, true);
        p.fireCd = kPlayerFireCd;
      }
    }
    wantDir_ = -1;
    wantFire_ = false;
  }

  // ----- Enemy AI -----
  void updateEnemy(size_t i) {
    Tank& e = tanks_[i];
    if (e.fireCd > 0) --e.fireCd;
    if (e.moveCd > 0) {
      --e.moveCd;
      return;
    }
    e.moveCd = enemyMoveCd();

    // Lined up with the player and nothing in the way: turn and shoot.
    int aim = lineOfSight(e, tanks_[0]);
    if (aim >= 0) {
      e.dir = static_cast<Dir>(aim);
      if (e.fireCd == 0) {
        fire(e, false);
        e.fireCd = enemyFireCd();
      }
      return;
    }

    if (!canMove(e, e.dir) || chance(0.25)) e.dir = pickDir(e);
    tryMove(e);
    if (e.fireCd == 0 && chance(0.1)) {  // the occasional speculative shot
      fire(e, false);
      e.fireCd = enemyFireCd();
    }
  }

  int lineOfSight(const Tank& from, const Tank& to) const {
    if (!to.alive || (from.x != to.x && from.y != to.y)) return -1;
    int d = from.x == to.x ? (to.y < from.y ? kUp : kDown) : (to.x < from.x ? kLeft : kRight);
    int x = from.x + kDx[d], y = from.y + kDy[d];
    while (x != to.x || y != to.y) {
      if (map_[y][x] != kEmpty || tankAt(x, y) >= 0) return -1;
      x += kDx[d];
      y += kDy[d];
    }
    return d;
  }

  Dir pickDir(const Tank& e) {
    const Tank& p = tanks_[0];
    if (p.alive && chance(0.5)) {  // head toward the player half the time
      int dx = p.x - e.x, dy = p.y - e.y;
      Dir d = std::abs(dx) > std::abs(dy) ? (dx > 0 ? kRight : kLeft) : (dy > 0 ? kDown : kUp);
      if (canMove(e, d)) return d;
    }
    std::vector<Dir> open;
    for (int d = 0; d < 4; ++d) {
      if (canMove(e, static_cast<Dir>(d))) open.push_back(static_cast<Dir>(d));
    }
    if (open.empty()) return static_cast<Dir>(rnd(0, 3));
    return open[rnd(0, static_cast<int>(open.size()) - 1)];
  }

  void spawnEnemies() {
    if (toSpawn_ == 0 || enemiesAlive() >= maxActive()) return;
    if (--spawnCd_ > 0) return;
    std::vector<int> free;
    for (int sx : kSpawnXs) {
      if (tankAt(sx, 1) < 0) free.push_back(sx);
    }
    if (free.empty()) return;
    Tank e{free[rnd(0, static_cast<int>(free.size()) - 1)], 1, kDown};
    e.moveCd = enemyMoveCd();
    e.fireCd = enemyFireCd();
    tanks_.push_back(e);
    --toSpawn_;
    spawnCd_ = std::max(15, 50 - 5 * level_);
  }

  // ----- Combat -----
  void fire(const Tank& t, bool fromPlayer) {
    Bullet b{t.x, t.y, t.x, t.y, t.dir, fromPlayer};
    stepBullet(b);
    if (b.alive) bullets_.push_back(b);
  }

  void stepBullet(Bullet& b) {
    int nx = b.x + kDx[b.dir], ny = b.y + kDy[b.dir];
    char& c = map_[ny][nx];
    if (c == kSteel) {
      b.alive = false;
      return;
    }
    if (c == kBrick) {
      c = kEmpty;
      b.alive = false;
      addEffect(nx, ny, 4);
      return;
    }
    int t = tankAt(nx, ny);
    if (t >= 0) {
      b.alive = false;
      hitTank(t, b.fromPlayer);
      return;
    }
    b.x = nx;
    b.y = ny;
  }

  // A tank that drives into a bullet still gets hit.
  void hitTanksUnderBullets() {
    for (auto& b : bullets_) {
      int t = tankAt(b.x, b.y);
      if (!b.alive || t < 0 || b.fromPlayer == (t == 0)) continue;
      b.alive = false;
      hitTank(t, b.fromPlayer);
    }
  }

  void moveBullets() {
    for (auto& b : bullets_) {
      if (!b.alive) continue;
      b.px = b.x;
      b.py = b.y;
      stepBullet(b);
    }
    // Opposing bullets that meet (or pass through each other) cancel out.
    for (size_t i = 0; i < bullets_.size(); ++i) {
      for (size_t j = i + 1; j < bullets_.size(); ++j) {
        Bullet& a = bullets_[i];
        Bullet& b = bullets_[j];
        if (!a.alive || !b.alive || a.fromPlayer == b.fromPlayer) continue;
        bool same = a.x == b.x && a.y == b.y;
        bool swapped = a.x == b.px && a.y == b.py && b.x == a.px && b.y == a.py;
        if (same || swapped) {
          a.alive = b.alive = false;
          addEffect(a.x, a.y, 3);
        }
      }
    }
  }

  void hitTank(int idx, bool byPlayer) {
    Tank& t = tanks_[idx];
    if (idx == 0) {
      if (byPlayer || t.invuln > 0) return;
      t.alive = false;
      addEffect(t.x, t.y, 10);
      if (--lives_ <= 0) {
        over_ = true;
      } else {
        respawn_ = kRespawnTicks;
      }
    } else {
      if (!byPlayer) return;  // no friendly fire between enemies
      t.alive = false;
      addEffect(t.x, t.y, 10);
      score_ += 100;
    }
  }

  std::mt19937 rng_;
  std::vector<std::string> map_;  // map_[y][x]
  std::vector<Tank> tanks_;       // tanks_[0] is always the player
  std::vector<Bullet> bullets_;
  std::vector<Effect> effects_;
  int level_ = 1, score_ = 0, lives_ = kStartLives;
  int toSpawn_ = 0, spawnCd_ = 0, respawn_ = 0, banner_ = 0;
  long tick_ = 0;
  int wantDir_ = -1;
  bool wantFire_ = false, paused_ = false, over_ = false, quit_ = false;
};

}  // namespace

int main() {
  if (!isatty(STDIN_FILENO) || !isatty(STDOUT_FILENO)) {
    std::fprintf(stderr, "Tank Battle needs an interactive terminal.\n");
    return 1;
  }
  winsize ws{};
  if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0 && ws.ws_col > 0 &&
      (ws.ws_col < kWidth * 2 || ws.ws_row < kHeight + 2)) {
    std::fprintf(stderr, "Terminal too small: need at least %dx%d, have %dx%d.\n",
                 kWidth * 2, kHeight + 2, ws.ws_col, ws.ws_row);
    return 1;
  }

  enterRawMode();
  Game game;
  auto next = std::chrono::steady_clock::now();
  while (!game.quit()) {
    for (Key k : readKeys()) game.handle(k);
    game.update();
    writeAll(game.render());
    next += std::chrono::milliseconds(kTickMs);
    std::this_thread::sleep_until(next);
  }
  restoreTerminal();
  std::printf("Thanks for playing! Final score: %d (reached level %d)\n", game.score(),
              game.level());
  return 0;
}
