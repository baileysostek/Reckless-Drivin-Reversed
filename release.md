## Reckless Drivin' v1.0 — SDL2 Port

A modern cross-platform port of **Reckless Drivin'**, the classic Mac OS racing game originally released in 2000 by Jonas Echterhoff.

This release brings the game to modern Windows and macOS systems using SDL2 and OpenGL, preserving the original gameplay, physics, and software-rendered aesthetic.

### What's New in This Port

- **Cross-platform support** — runs natively on Windows and macOS
- **Resizable window** with 4:3 aspect ratio letterboxing
- **Gamepad support** via SDL2
- **High score name entry** — type your name when you set a new record
- **Registration system removed** — the full game is free to play, as Jonas intended when he released the registration code years ago

### Downloads

| Platform | Download |
|----------|----------|
| Windows | [RecklessDrivin-Windows.zip](PLACEHOLDER) |
| macOS | [RecklessDrivin-macOS.zip](PLACEHOLDER) |

### Windows

Extract the zip and run `RecklessDrivin.exe`. No installation required.

### macOS

Extract the zip and open `RecklessDrivin.app`. On first launch you may need to right-click > Open to bypass Gatekeeper since the app is not notarized.

### Controls

| Action | Key |
|--------|-----|
| Accelerate | Up Arrow |
| Brake | Down Arrow |
| Steer | Left / Right Arrow |
| Kickdown | Left Shift |
| Handbrake | Space |
| Fire | Z |
| Missile | X |
| Pause | P |
| Quit | Escape |

Gamepad input is also supported.

### Credits

- **[Jonas Echterhoff](https://github.com/jechter)** — Original game and [source code](https://github.com/jechter/RecklessDrivin)
- **[Nathan Craddock](https://github.com/natecraddock)** — LZRW3-A decompression and PPic extraction from [open-reckless-drivin](https://github.com/natecraddock/open-reckless-drivin)
