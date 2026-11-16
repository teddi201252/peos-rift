# Third-party software

rift.app is built from these projects. Their sources are downloaded by
`build.sh` (or by PeOS's own port scripts) at the versions below; none of
them is copied here.

| Project | Version | License | Used for |
| --- | --- | --- | --- |
| [Rift Ascend](https://gitgud.io/Xarond/rift-ascend) | commit `508d282` | GPL-3.0 | the client and the game server |
| Rift Ascend's `ThirdParty/` (GLFW, glm, glad, ImGui, Lua, sol3, ENet, miniaudio, stb, cgltf, xxHash, zstd, SQLite, Tracy, ww2ogg, doctest) | as pinned by Rift Ascend | zlib, MIT, BSD, public domain (see Rift Ascend's `THIRD-PARTY.md`) | windows, maths, scripts, network, sound, files |
| [Mesa](https://mesa3d.org) | 26.2.7, PeOS's port (`userland/ports/mesa`) | MIT | OpenGL 4.5 on the Intel GPU (iris) |
| [OpenSSL](https://openssl.org) | 3.5.9, PeOS's port | Apache-2.0 | the client's TLS |
| [FreeType](https://freetype.org) | 2.15.0, PeOS's port | FreeType License | the fonts |
| [GCC](https://gcc.gnu.org) | 16.2.0 (PeOS's toolchain) | GPL-3.0 with the Runtime Library Exception | the compiler, libstdc++ and libgcc in the app |

Tools that run on your computer, not part of the app:
[CDTB](https://github.com/CommunityDragon/CDTB) (installed by `build.sh
assets` in its own Python environment) and CommunityDragon's
[file name lists](https://github.com/CommunityDragon/Data) take the game
files out of your League installation.

`glfw/peos_glfw.c` and `glfw/peos_platform.h` are GLFW's platform for PeOS;
`xpapack/main.cpp`, `src/crash_peos.cpp` and `extract_assets.py` are this
port's own. The PeOS SDK and libraries the app is linked with (libpeos,
libpeos-compat) are PeOS's: the operating system's own libraries.

League of Legends is a trademark of Riot Games, Inc. This project is not
affiliated with or endorsed by Riot Games and contains none of its files;
the game data is the user's own.
