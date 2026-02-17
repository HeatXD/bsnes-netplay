bsnes-netplay
=====
**Note: This project is still a work in progress!**

A custom build of the bsnes emulator, integrated with GekkoNet, to provide
robust and low-latency netplay capabilities.

Currently, it supports sessions up to 5 players. Port
forwarding (or another solution, such as a VPN that simulates a Local-Area
Network) might be required.

Spectating is also supported, also through direct connection.
Other connection methods are currently in the works.

[**Download the latest release of bsnes-netplay here.**](https://github.com/HeatXD/bsnes-netplay/releases)

# Original bsnes project README.md:

![bsnes logo](bsnes/target-bsnes/resource/logo.png)

bsnes is a multi-platform Super Nintendo (Super Famicom) emulator, originally
developed by Near, which focuses on performance,
features, and ease of use.

Unique Features
---------------

  - True Super Game Boy emulation (using the SameBoy core by Lior Halphon)
  - HD mode 7 graphics with optional supersampling (by DerKoun)
  - Low-level emulation of all SNES coprocessors (DSP-n, ST-01n, Cx4)
  - Multi-threaded PPU graphics renderer
  - Speed mode settings which retain smooth audio output (50%, 75%, 100%, 150%, 200%)
  - Built-in games database with thousands of game entries
  - Built-in cheat code database for hundreds of popular games (by mightymo)
  - Built-in save state manager with screenshot previews and naming capabilities
  - Customizable per-byte game mappings to support any cartridges, including prototype games
  - 7-zip decompression support
  - Extensive Satellaview emulation, including BS Memory flash write and wear-leveling emulation
  - Optional higan game folder support (standard game ROM files are also fully supported!)
  - Advanced mapping system allowing multiple bindings to every emulated input

Standard Features
-----------------

  - MSU1 support
  - BPS and IPS soft-patching support
  - Save states with undo and redo support (for reverting accidental saves and loads)
  - OpenGL multi-pass pixel shaders
  - Several built-in software filters, including HQ2x (by MaxSt) and snes_ntsc (by blargg)
  - Adaptive sync and dynamic rate control for perfect audio/video synchronization
  - Just-in-time input polling for minimal input latency
  - Run-ahead support for removing internal game engine input latency
  - Support for Direct3D exclusive mode video
  - Support for WASAPI exclusive mode audio
  - Periodic auto-saving of game saves
  - Auto-saving of states when unloading games, and auto-resuming of states when reloading games
  - Sprite limit disable support
  - Cubic audio interpolation support
  - Optional high-level emulation of most SNES coprocessors
  - Optional emulation of flaws in older emulators for compatibility with older unofficial software
  - CPU, SA1, and SuperFX overclocking support
  - Frame advance support
  - Screenshot support
  - Cheat code search support
  - Movie recording and playback support
  - Rewind support
  - HiDPI support
  - Multi-monitor support
  - Turbo support for controller inputs

Links (Non-Netplay Build)
-----

  - [Official git repository](https://github.com/bsnes-emu/bsnes)
  - [Official Discord](https://discord.gg/B27hf27ZVf)

<!-- Dummied out to avoid confusion: -->
<!-- Nightly Builds -->
<!-- -------------- -->
<!---->
<!--   - [Windows](https://github.com/bsnes-emu/bsnes/releases/download/nightly/bsnes-windows.zip) -->
<!--   - [macOS](https://github.com/bsnes-emu/bsnes/releases/download/nightly/bsnes-macos.zip) -->
<!--   - [Linux](https://github.com/bsnes-emu/bsnes/releases/download/nightly/bsnes-ubuntu.zip) -->
<!--   - [FreeBSD](https://api.cirrus-ci.com/v1/artifact/github/bsnes-emu/bsnes/freebsd-x86_64-binaries/bsnes-nightly/bsnes-nightly.zip) -->

Preview
-------

![bsnes user interface](.assets/user-interface.png)
![bsnes running Bahamut Lagoon](.assets/bahamut-lagoon.png)
![bsnes running Tengai Makyou Zero](.assets/tengai-makyou-zero.png)
