# Steel Curtain: Fulda Gap, 1985

A turn-based Cold War armoured battle for Windows, inspired by *Flashpoint Campaigns: Cold War*. You command a combined US and West German task force holding a river line against successive Soviet echelons.

The game uses Flashpoint-style **asynchronous WEGO turns**. In each orders phase the clock is frozen while you plan. When you execute the turn, both sides act at the same time for a minute of battle. Your orders reach the units only after a command delay, so they don't all react at once.

The game is written in C++20 with [raylib](https://www.raylib.com/). The game itself ships no art or sound files. Vehicle art, terrain and sound effects are all generated in code, so the build produces a single standalone `.exe`. CMake downloads raylib when you configure the build.

## Playing on Windows

### Option 1: download a build

Every push that changes `tank-game/` runs the **Steel Curtain (Windows build)** GitHub Action. Open the run's **Artifacts** section, download `SteelCurtain-windows`, unzip it and double-click `steel_curtain.exe`.

### Option 2: build it yourself

You need [Visual Studio 2022](https://visualstudio.microsoft.com/) with the *Desktop development with C++* workload, which includes CMake. In a *Developer PowerShell for VS 2022*, run:

```powershell
cd tank-game
cmake -S . -B build -A x64
cmake --build build --config Release
.\build\Release\steel_curtain.exe
```

You can also open the `tank-game` folder in Visual Studio (*File → Open → Folder*), choose `steel_curtain.exe` as the startup item and press F5.

### Linux, or cross-compiling for Windows from Linux

```sh
cmake -S . -B build && cmake --build build -j && ./build/steel_curtain          # Linux
cmake -S . -B build-win -DCMAKE_TOOLCHAIN_FILE=cmake/mingw-w64.cmake            # Windows .exe via MinGW
cmake --build build-win -j
```

On Linux you need the X11 and OpenGL development packages, for example `libx11-dev libxrandr-dev libxinerama-dev libxcursor-dev libxi-dev libgl1-mesa-dev`.

## The battle

The map is a new piece of procedurally generated Hessian countryside each time. It has farmland and hedgerows, wooded ridges, villages with red-tiled roofs, and a river crossed by three bridges. The bridgehead towns south of the river are your objectives (**OBJ A–C**).

1. **Deployment.** The clock is stopped and moves happen at once. Put your platoons on ridges, at wood edges or overlooking the bridges. Press **Enter** to start turn 1.
2. **Turns.** Each turn has two phases:
   - **Orders phase.** Time is frozen. Select units, pick an order type and a formation, and right-click a destination. Each order is drawn as a dashed line with its delay. Fire missions are also planned now.
   - **Execution phase.** Press **Enter** (or **Execute turn**). The battle runs for the turn length, 60 seconds by default (**T** switches between 30, 60 and 90). Each order takes effect after its command delay: a few seconds, longer for suppressed units and for coordinated formation moves. Units still fight, halt and fire smoke on their own. You can pause (**Space**) and change speed (**F**), but you can't give new orders until the next orders phase.
3. **The battle.** It lasts 12 minutes:
   - **About H+0:20:** a Soviet reconnaissance screen of BRDM-2s and BTR-70s.
   - **About H+1:10:** motor-rifle companies in BMP-2s, with T-72s and a Shilka.
   - **About H+3:00:** T-80B tank companies.
   - **From about H+4:40:** a second echelon, then an Operational Manoeuvre Group.
4. **Result.** Each objective is worth 100 victory points to whoever holds it at the end. Each destroyed vehicle scores its point value to the other side. The ratio of the two totals decides the result, from Decisive Victory to Decisive Defeat.

### Orders

| Order | Speed | Effect |
| ----- | ----- | ------ |
| **Move** | Normal | The balanced default. Units shoot on the move. |
| **Quick move** | +30% | Strongly prefers roads. Shooting on the run is much less accurate, the dust makes units easier to spot, and crews see less. Use it to reposition out of contact. |
| **Deliberate move** | 55% | Creeps from cover to cover. Units halt to shoot from a standstill when they see a target, see 20% further, and are harder to spot while moving. Use it to advance into contact. |
| **Assault** | 90% | Presses on through suppression: units only pin at very high suppression and shoot better while suppressed. On arrival they close with any spotted enemy within about 600 m of the objective. |
| **Halt** (X) | – | Stop and hold the current position. |

Right-click uses the order type selected in the toolbar (**Tab** cycles it). Hold **Shift** for Quick, **Ctrl** for Deliberate or **Alt** for Assault to override it for one click. Right-clicking a spotted enemy makes it the priority target.

### Formations

Select two or more units, choose a formation in the toolbar (**O** cycles them), and the move shows a preview of every vehicle's slot under the cursor.

- **Column:** single file, good on roads.
- **Line:** abreast, for maximum firepower forward.
- **Wedge:** a leader in front and the others stepped back to either side. A good all-round approach formation.
- **Echelon left / right:** stepped back diagonally, to guard a flank.
- **None:** keeps the group's current layout, compressed.

A formation faces the direction of travel. The group plans one route for its least capable vehicle (one that can't swim, then the slowest). Each member follows that route offset to its slot, moves at the slowest member's pace, and waits at each waypoint until the others catch up (up to 20 seconds, so one stuck vehicle can't hold up the rest). Giving a member a new order takes it out of the formation.

### Your task force

| Key | Platoon | Vehicles |
| --- | ------- | -------- |
| 1 | Ghost (cavalry scouts) | 2× M3 Bradley CFV, 1× Spz Luchs |
| 2 | Tango (US tanks) | 4× M1 Abrams |
| 3 | Panther (FRG tanks) | 3× Leopard 2A4 |
| 4 | Mustang (mech infantry) | 4× M2A1 Bradley |
| 5 | Hammer (anti-tank) | 2× M901 ITV |
| 6 | Reserve | 2× M60A3 TTS, 1× M113A2 |

You also have one 155mm battery with 4 HE and 2 smoke fire missions.

### Vehicles

There are 15 vehicles, each with its own top-down art, NATO map symbol and statistics. Press **V** in game for the recognition guide.

- **NATO:** M1 Abrams, Leopard 2A4, M60A3 TTS, M2A1 Bradley, M3 Bradley CFV, M113A2, M901 ITV, Spz Luchs
- **Warsaw Pact:** T-80B, T-72A, T-62M, BMP-2, BTR-70, BRDM-2 AT-5, ZSU-23-4 Shilka

### How combat works

- **Line of sight and spotting.** You only see enemies that one of your units can see. Hills block sight, deep woods and built-up areas screen it, and smoke blocks it. A vehicle sitting in woods or a town is hard to spot. Firing gives your position away. NATO thermal sights see further than Soviet optics.
- **Armour facing.** Each hit is checked against the armour on the side that was struck. Tank front armour is far thicker than side or rear armour, so flanking pays.
- **Order types matter.** See [Orders](#orders): speed is traded against accuracy, concealment and spotting.
- **Guided missiles** (TOW, Konkurs, Kobra) outrange tank guns. The launcher must stop and keep the target in sight until the missile hits. Killing the gunner, or putting smoke between them, makes the missile miss.
- **Suppression.** Near misses, hits and artillery suppress a vehicle. It then shoots less accurately and reloads more slowly. A pinned vehicle barely moves.
- **Cover and posture.** Woods and towns make a vehicle harder to hit. A vehicle that has been stationary for a while is treated as hull-down. Moving spoils accuracy, especially for Soviet fire control.
- **Amphibious vehicles** (BMP-2, BTR-70, BRDM-2, M113, ITV, Luchs) can swim the river away from the bridges. Watch your flanks.

## Controls

| Input | Action |
| ----- | ------ |
| Enter / Execute button | Begin the battle (deployment) / execute the turn (orders phase) |
| Left-click / drag | Select units (Shift adds; double-click selects the platoon) |
| Right-click | Give the selected order to the selected units. On a spotted enemy: make it the priority target |
| Tab, or toolbar | Order type: Move, Quick, Deliberate, Assault |
| Shift / Ctrl / Alt + right-click | Quick move / Deliberate move / Assault for this click |
| O, or toolbar | Formation: none, column, line, wedge, echelon left, echelon right |
| 1–6, E | Select a platoon, or every unit |
| H / X / Z | Toggle hold fire / halt / fire smoke grenades |
| Q / R | Plan a 155mm HE / smoke fire mission (then left-click the target) |
| T | Turn length: 30, 60 or 90 seconds |
| Space / F | Pause / cycle game speed ×1, ×2, ×4 during execution |
| WASD, arrow keys, mouse wheel | Pan and zoom. Middle-drag pans. Home shows the whole map |
| C / G / L | NATO map symbols / 1 km grid / range rings for the selected unit |
| V / F1 / Esc | Vehicle guide / help / cancel |

## Code layout

| File | Contents |
| ---- | -------- |
| `src/vehicles.cpp` | The vehicle database: speeds, armour, optics, weapons, ammunition, paint |
| `src/terrain.cpp` | Map generation, movement costs, line of sight, A* pathfinding, map rendering |
| `src/game.cpp` | Simulation: turns and orders, formations, spotting, gunnery, missiles, artillery, smoke, objectives, Soviet AI, scenario |
| `src/sprites.cpp` | Procedural vehicle art and APP-6 NATO symbols |
| `src/audio.cpp` | Synthesised gun, missile and explosion sounds |
| `src/main.cpp` | Window, camera, input, HUD, title, recognition guide and results screens |

To add a vehicle, add an entry to `VehicleId` in `vehicles.hpp`, then fill in its stats in `vehicles.cpp`. Its art comes from the `SpriteStyle` you pick. To change the enemy's order of battle, edit `Game::setupWaves()`. To change yours, edit `Game::setupNato()`.

### Developer options

- `steel_curtain --simulate 10 1` runs ten headless battles on Normal, with NATO holding its starting positions, and prints the results. Use it for balance testing.
- `steel_curtain --capture battle:200:zoom shot.png` fast-forwards a battle and saves a screenshot. `title`, `guide` and `orders` (a turn-3 orders phase with orders queued) work as capture modes too.
