# Tank Battle

A small arcade tank game for the terminal, written in one C++17 file with no dependencies.

You drive the green tank at the bottom of the arena. Red enemy tanks enter from the top. Destroy all of them to clear the level. Each level adds more enemies, and they move and shoot faster.

## Build and run

```sh
make        # or: g++ -std=c++17 -O2 -Wall -Wextra -o tank tank.cpp
./tank
```

You need a Linux or macOS terminal that is at least 62×23 characters. On Windows, use WSL.

## Controls

| Key            | Action           |
| -------------- | ---------------- |
| WASD / arrows  | Move and turn    |
| Space          | Fire             |
| P              | Pause            |
| R              | Restart (after game over) |
| Q              | Quit             |

## Rules

- `##` brick walls break when a shot hits them. `[]` steel walls can't be destroyed.
- You start with 3 lives. After a respawn your tank blinks and can't be hit for a few seconds.
- Each enemy you destroy scores 100 points. Clearing a level scores 500 × the level number.
- Shots that meet head-on cancel each other out.

## Ideas to extend it

- Power-ups such as faster shots, a shield or an extra life
- A home base you must protect, as in *Battle City*
- A second player on the same keyboard
- Hand-designed levels loaded from text files
- A graphical version built with SDL2 or raylib that reuses the same `Game` logic
