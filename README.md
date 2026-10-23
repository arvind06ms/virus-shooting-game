## Zombie Waves (C++ Console Game)

A small **console-based** C++ game prototype:

- **Zombie waves** increase after you kill enough zombies.
- You gain **EXP** from kills; leveling improves your stats.
- Your weapon gets **time-based upgrade drops** (damage / fire rate), and EXP gain scales.

This project uses **only the C++ standard library** — no extra packages required.

## Requirements

- Windows 10/11
- A C++ compiler:
  - **MSVC** (Visual Studio 2022 “Desktop development with C++”), or
  - **MinGW-w64** (g++), or
  - **clang**
- **CMake** (recommended)

## Build (CMake)

From PowerShell:

```powershell
cd C:\Users\arvin\zombie_waves_game
cmake -S . -B build
cmake --build build --config Release
.\build\Release\zombie_waves_game.exe
```

If you built a non-MSVC generator (MinGW/clang), the exe may be at:

```powershell
.\build\zombie_waves_game.exe
```

## How to play

- Press:
  - `A` to attack (you have a short cooldown)
  - `H` to heal (uses a medkit)
  - `Q` to quit
- Zombies attack you automatically over time.
- Kill enough zombies to trigger the next wave.
- Survive as long as you can.

