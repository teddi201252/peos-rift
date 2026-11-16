# Rift Ascend on PeOS (rift)

This is the port of [Rift Ascend](https://gitgud.io/Xarond/rift-ascend), a
reimplementation of the League of Legends client and server, to
[PeOS](https://github.com/teddi201252): the patches and the platform code
that turn Rift Ascend's client into **rift.app**, an app that plays a match
on PeOS with the Intel GPU (OpenGL 4.5 through Mesa's iris driver).

It plays with **your own** League of Legends files. Nothing of Riot's comes
with it: you extract the game files from your own installation, and this
repository has the tools that pack them for PeOS.

> **Work in progress.** rift.app starts straight into one match (Ashe, on
> Summoner's Rift, against bots), on a game server that runs on another
> computer of your network. There is no launcher and no lobby yet: the
> client of Rift Ascend should come first, then the game. On a MacBook Pro
> 2019 (Intel UHD 630) a match runs at 20-45 frames per second, with
> stutters when the laptop gets hot and the processor slows the GPU down.

## What you need

- **PeOS**, installed on a computer with an **Intel Gen9 GPU** (Skylake to
  Coffee Lake: HD/UHD 5xx-6xx). rift.app needs the GPU: it does not run in
  a virtual machine.
- **League of Legends**, installed on another computer (Windows or macOS).
  Tested with version **16.22**, in **English** (`en_US`: the voices and
  announcers Rift Ascend looks for).
- **A computer for the game server**: Linux (WSL on Windows works), or a
  Mac with Docker Desktop. Both on the same network as PeOS.
- The tools to build: see [docs/BUILDING.md](docs/BUILDING.md).

## How a match is set up

1. **The game files.** `extract_assets.py` takes every `.wad.client` of your
   League installation apart (with CommunityDragon's CDTB) into
   `league-assets/<Name>.wad/...`, about 80 GB. `xpapack` packs each one
   into one archive, `<Name>.wad.xpa` (about 26 GB in all, 380 files
   instead of 800,000): PeOS reads them much faster.
2. **The server.** `./build.sh server` builds Rift Ascend's game server for
   that computer, `./build.sh serve` runs it (4 bots, waiting for one
   player). On a Mac: `./build.sh docker-server` and `./build.sh
   docker-serve`.
3. **The app.** `./build.sh app` builds rift.app, with the server's address
   in its arguments (`RIFT_ARGS`).
4. **Getting it all to PeOS.** `./build.sh xpa-serve` serves the archives,
   Rift Ascend's own data and rift.app on port 8080; **Rift Get**
   (`riftget.app`, `./build.sh riftget`) downloads them on PeOS into
   `/rift` and `/apps/rift.app`. Restart PeOS: Rift Ascend is among the
   apps.

The steps, with every command, are in [docs/BUILDING.md](docs/BUILDING.md).

## What is here

| Path | What |
| --- | --- |
| `build.sh` | everything: fetch, build, pack, serve |
| `patches/rift/` | PeOS's patches to Rift Ascend (paths, audio, archives, loading, the HUD, the network) |
| `patches/glfw/`, `glfw/` | GLFW's PeOS platform: the window, keyboard and mouse on Starlight, GL through Mesa |
| `src/crash_peos.cpp` | the crash handler for PeOS |
| `xpapack/` | the archive packer (Rift Ascend's CMake names it; its source was not published) |
| `extract_assets.py` | the game files out of your League installation |
| `bundle/`, `resources/` | rift.app's `app.ini` and its icon and texts |
| `riftget/` | Rift Get's `app.ini` (its source is PeOS's `userland/progs/riftget.c`) |
| `glfwtest/` | a small GLFW test app |
| `docker/` | the game server's container, for a Mac |

## License

GPL-3.0, as Rift Ascend: see [LICENSE](LICENSE). Third-party software and
its licenses: [NOTICE.md](NOTICE.md).

League of Legends is a trademark of Riot Games, Inc. This project is not
affiliated with or endorsed by Riot Games and contains none of its files;
the game data is the user's own.
