# Building rift.app and playing a match

Everything runs on Linux (WSL on Windows works) or macOS, with the
commands of `build.sh`. It builds **with a PeOS checkout**: the x86_64-peos
toolchain and the ports Rift Ascend needs (Mesa, OpenSSL, FreeType) come
from there, not yet from the Starlight App Kit's SDK.

## 1. PeOS and its ports

```bash
git clone https://github.com/teddi201252/peos
cd peos
tools/peos-toolchain/build.sh            # the x86_64-peos cross compiler
bash userland/ports/mesa/build.sh all    # Mesa (iris) into the sysroot
bash userland/ports/openssl/build.sh all
bash userland/ports/freetype/build.sh all
```

Mesa needs LLVM, Clang, libclc and the SPIR-V translator on your computer
(only to build its shader compiler): on Ubuntu 24.04 `apt install
python3.12-venv llvm-18-dev clang-18 libclang-18-dev libclang-cpp18-dev
libclc-18-dev libllvmspirvlib-18-dev spirv-tools`; on macOS `brew install
llvm libclc spirv-llvm-translator bison`.

Put this repository **beside** the PeOS checkout (`../peos`), or tell
`build.sh` where it is with `PEOS_DIR=/path/to/peos`. The work goes to
`build/rift` in the PeOS checkout (`RIFT_WORK` to change it). On WSL with
the checkout on a Windows drive, build the client on the Linux disk:
`RIFT_BUILD=$HOME/rift-peos`.

## 2. The game files

From your League of Legends installation (version 16.22, English), with
CommunityDragon's file names:

```bash
python3 extract_assets.py --league "/path/to/League of Legends" --out ~/league-assets \
    --hashes hashes.game.txt --jobs 6
```

(`./build.sh assets LEAGUE DIR` does the same one archive at a time and
fetches CDTB and the names list for you.) `hashes.game.txt` is
CommunityDragon's list of file names (`Data` repository on GitHub,
`hashes/lol/hashes.game.txt.0` ... `.8`, joined).

## 3. The game server and xpapack

On the computer that hosts the match (Linux or WSL):

```bash
./build.sh server        # Rift Ascend's GameServer, and xpapack, for this computer
```

The game server reads the folder `league-assets` itself (or only its `.bin`
files, grids and tables, ~3 GB) from `~/league-assets-server`
(`RIFT_ASSETS` for another place). For PeOS, pack the files into archives,
first moving aside the maps Rift Ascend does not use (`Map12.wad`,
`Map22.wad`, `Map30.wad`, `Map453.wad` and their language archives:
they only make the download bigger):

```bash
<server build>/bin/RelWithDebInfo/xpapack ~/league-assets ~/xpa --jobs 8
```

Then run the server: 4 bots, waiting for one player, UDP 5119.

```bash
./build.sh serve
```

On a Mac with Docker Desktop: `./build.sh docker-server`, then
`./build.sh docker-serve` (`./build.sh docker-stop` to stop it). The server
closes when its player leaves, or when the bots finish the match alone.

## 4. rift.app and Rift Get

```bash
./build.sh fetch                                     # Rift Ascend at its commit, the patches
./build.sh glfw
RIFT_BUILD=$HOME/rift-peos ./build.sh client         # ~20 minutes the first time
RIFT_ARGS="--connect <server>:5119 --champion Ashe --log-dir /starlight/config/rift" ./build.sh app
RIFTGET_ARGS="http://<this computer>:8080/" ./build.sh riftget
XPA_DIR=~/xpa ./build.sh xpa-serve                   # serves everything on port 8080
```

## 5. On PeOS

1. Copy `riftget.app` to `/apps` (with Curiosity, from a USB stick), then
   restart.
2. Connect to the network and open **Rift Get**: it downloads the archives
   into `/rift` and rift.app into `/apps` (~28 GB). Stopped? Open it again:
   it keeps the files already whole.
3. Restart PeOS and open **Rift Ascend**, with the game server running. The
   first start prepares the fonts once.

The client's log is `/starlight/config/rift/client.log`; the kernel's,
with the GPU's numbers every 10 s, `/boot.log`.
