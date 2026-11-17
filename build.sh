#!/usr/bin/env bash
# Rift Ascend (the client) for PeOS: docs/rift-ascend-port-plan.md. The
# source (GPL-3.0) is cloned at a fixed commit under build/rift/ (never in
# git); the game's files are the user's own and never come here.
#
#   userland/ports/rift/build.sh fetch      the source at RIFT_COMMIT, with its submodules (GLFW,
#                                           glm), PeOS's patches (patches/rift/) and GLFW platform
#                                           (glfw/, patches/glfw/)
#   userland/ports/rift/build.sh glfw       GLFW for PeOS (phase C): build/rift/glfw-peos/src/libglfw3.a
#   userland/ports/rift/build.sh glfwtest   the GLFW Test app (glfwtest/): build/rift/glfwtest.app
#   userland/ports/rift/build.sh client     the client (phase D), in RIFT_BUILD (default build/rift/client-peos;
#                                           on WSL a folder outside /mnt is much faster)
#   userland/ports/rift/build.sh app        the bundle: build/rift/rift.app (app.bin, app.ini with RIFT_ARGS)
#   userland/ports/rift/build.sh data DIR   Rift Ascend's own data as DIR/rift-data: DIR is the game's disk
#                                           (beside league-assets; the client finds both on /mediaN)
#   userland/ports/rift/build.sh assets LEAGUE DIR   the game's files from the user's League
#                                           installation as DIR/league-assets (CDTB; ~40 GB)
#   userland/ports/rift/build.sh server     the game server for this computer (Linux), not PeOS
#   userland/ports/rift/build.sh serve      runs it for the MacBook: --lan, 4 bots, RIFT_ASSETS
#   userland/ports/rift/build.sh docker-server, docker-serve, docker-stop   the same on a Mac, in Docker
#
# Needs: the x86_64-peos toolchain, and in the sysroot Mesa's libGL-peos.a
# (userland/ports/mesa/build.sh), OpenSSL (userland/ports/openssl) and
# FreeType (userland/ports/freetype).
#
# RIFT_ARGS: the client's arguments in the bundle (it refuses to start with
# none); by default the game server on the home PC's WSL.
#
# The same script is the public copy's (the peos-rift repository, the port's
# files at its top): there PEOS_DIR names a PeOS checkout (by default
# ../peos beside it), whose toolchain and ports it builds with; the work
# goes to RIFT_WORK (by default build/rift in that checkout).
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
if [ -n "${PEOS_DIR:-}" ]; then
    ROOT="$(cd "$PEOS_DIR" && pwd)"
elif [ -f "$HERE/../../../kernel/main.c" ]; then   # inside PeOS: userland/ports/rift
    ROOT="$(cd "$HERE/../../.." && pwd)"
else                                                # the public copy, beside a PeOS checkout
    ROOT="$(cd "$HERE/../peos" 2>/dev/null && pwd)" ||
        { echo "rift: no PeOS checkout: set PEOS_DIR (see docs/BUILDING.md)"; exit 1; }
fi
[ -f "$ROOT/kernel/main.c" ] || { echo "rift: $ROOT is not a PeOS checkout (PEOS_DIR)"; exit 1; }
W="${RIFT_WORK:-$ROOT/build/rift}"
SRC="$W/rift-ascend"
PREFIX="$ROOT/build/toolchain/peos"
TOOLCHAIN="$ROOT/userland/ports/d2decomped/toolchain-peos.cmake"
TARGET=x86_64-peos

RIFT_URL=https://gitgud.io/Xarond/rift-ascend.git
RIFT_COMMIT=508d282ed9b8b8a2ac5125999a43fbb3f8b46432   # 2026-11-13

fetch() {
    mkdir -p "$W"
    [ -d "$SRC/.git" ] || git clone -q "$RIFT_URL" "$SRC"
    git -C "$SRC" fetch -q origin
    git -C "$SRC" checkout -q --detach "$RIFT_COMMIT"
    git -C "$SRC" submodule update -q --init --recursive   # GLFW and glm, at the commits Rift Ascend pins
    [ "$(git -C "$SRC" rev-parse HEAD)" = "$RIFT_COMMIT" ] || { echo "rift: not at $RIFT_COMMIT"; exit 1; }
    # PeOS's patches on a clean tree (the build writes NEXT_BUILD_NUMBER)
    git -C "$SRC" checkout -q -- .
    for p in "$HERE"/patches/rift/*.patch; do
        git -C "$SRC" apply "$p" || { echo "rift: $p does not apply"; exit 1; }
    done
    cp "$HERE"/src/crash_peos.cpp "$SRC/src/core/"   # the crash handler for PeOS (patches/rift: the CMake)
    # xpapack (the game's files into XPA archives): its CMake target is there,
    # its source is not in Rift Ascend's published tree
    mkdir -p "$SRC/tools/xpapack" && cp "$HERE"/xpapack/main.cpp "$SRC/tools/xpapack/"
    # GLFW's PeOS platform: the patch on a clean GLFW, then the files of its own
    local g="$SRC/ThirdParty/glfw"
    git -C "$g" checkout -q -- . && git -C "$g" clean -qfd
    for p in "$HERE"/patches/glfw/*.patch; do
        git -C "$g" apply "$p" || { echo "rift: $p does not apply"; exit 1; }
    done
    cp "$HERE"/glfw/peos_platform.h "$HERE"/glfw/peos_glfw.c "$g/src/"
    echo "rift: rift-ascend at ${RIFT_COMMIT:0:12} in build/rift/rift-ascend, GLFW with PeOS's platform"
}

# RIFT_JOBS: compilers at once (each C++ one of the client can take 1-2 GB:
# WSL's memory, half the PC's, ran out with two builds at full width)
jobs() { echo "${RIFT_JOBS:-$(nproc 2>/dev/null || sysctl -n hw.ncpu)}"; }

glfw() {
    [ -f "$SRC/ThirdParty/glfw/src/peos_glfw.c" ] || fetch
    rm -rf "$W/glfw-peos"
    cmake -S "$SRC/ThirdParty/glfw" -B "$W/glfw-peos" -G "Unix Makefiles" -DCMAKE_TOOLCHAIN_FILE="$TOOLCHAIN" \
        -DCMAKE_BUILD_TYPE=Release -DBUILD_SHARED_LIBS=OFF -DGLFW_BUILD_X11=OFF -DGLFW_BUILD_WAYLAND=OFF \
        -DGLFW_BUILD_EXAMPLES=OFF -DGLFW_BUILD_TESTS=OFF -DGLFW_BUILD_DOCS=OFF -DGLFW_INSTALL=OFF \
        -DCMAKE_C_FLAGS="-I$ROOT/build/peos-sysroot/usr/include" >/dev/null
    cmake --build "$W/glfw-peos" -j"$(jobs)" >/dev/null
    echo "rift: build/rift/glfw-peos/src/libglfw3.a ($(du -h "$W/glfw-peos/src/libglfw3.a" | cut -f1))"
}

glfwtest() {
    [ -f "$W/glfw-peos/src/libglfw3.a" ] || glfw
    local app="$W/glfwtest.app" src="$HERE/glfwtest" tp="$SRC/ThirdParty"
    rm -rf "$app" && mkdir -p "$app"
    "$PREFIX/bin/$TARGET-gcc" -std=gnu11 -O2 -Wall -Wextra -Werror -I"$tp/glfw/include" -I"$tp/glad/include" \
        -c "$src/src/main.c" -o "$W/glfwtest.o"
    "$PREFIX/bin/$TARGET-gcc" -std=gnu11 -O2 -w -I"$tp/glad/include" -c "$tp/glad/src/gl.c" -o "$W/glad.o"
    "$PREFIX/bin/$TARGET-g++" -o "$W/glfwtest.elf" "$W/glfwtest.o" "$W/glad.o" "$W/glfw-peos/src/libglfw3.a" \
        -lGL-peos -ldrm -lz -lm
    "$PREFIX/bin/$TARGET-strip" -o "$app/app.bin" "$W/glfwtest.elf"
    cp "$src/bundle/app.ini" "$app/app.ini"
    cp -R "$src/resources" "$app/resources"
    echo "rift: build/rift/glfwtest.app ($(du -h "$app/app.bin" | cut -f1))"
}

RIFT_BUILD="${RIFT_BUILD:-$W/client-peos}"
# --prof 10: every 10 s the CPU table and each GPU zone's average and worst
# (patches/rift/0017) in the log: the stutter on the MacBook (2026-11-16)
RIFT_ARGS="${RIFT_ARGS:---connect 192.168.1.4:5119 --champion Ashe --log-dir /starlight/config/rift --prof 10}"

client() {
    [ -f "$SRC/src/core/crash_peos.cpp" ] || fetch
    cmake -S "$SRC" -B "$RIFT_BUILD" -G "Unix Makefiles" -DCMAKE_TOOLCHAIN_FILE="$TOOLCHAIN" \
        -DCMAKE_BUILD_TYPE=Release -DLOLCLONE_DEV=OFF -DLOLCLONE_BUILD_TESTS=OFF -DLOLCLONE_PCH=OFF >/dev/null
    cmake --build "$RIFT_BUILD" --target Client -j"$(jobs)" >/dev/null
    "$PREFIX/bin/$TARGET-strip" -o "$W/Client.stripped" "$RIFT_BUILD/bin/Release/Client"
    echo "rift: the client, build/rift/Client.stripped ($(du -h "$W/Client.stripped" | cut -f1))"
}

app() {
    [ -f "$W/Client.stripped" ] || client
    local app="$W/rift.app"
    rm -rf "$app" && mkdir -p "$app"
    cp "$W/Client.stripped" "$app/app.bin"
    sed "s|@ARGS@|$RIFT_ARGS|" "$HERE/bundle/app.ini.in" > "$app/app.ini"
    cp -R "$HERE/resources" "$app/resources"
    # Rift Ascend's own data is not in the bundle (2000 files: a live root
    # holds 1024): build.sh data puts it on the game's disk, beside
    # league-assets, where the client finds it (patches/rift/0003)
    echo "rift: build/rift/rift.app ($(du -sh "$app" | cut -f1)), args: $RIFT_ARGS"
}

# riftget.app: Rift Ascend's files from the PC to an installed PeOS
# (userland/progs/riftget.c), the base URL from RIFTGET_ARGS; build.sh
# xpa-serve on the PC serves them.
RIFTGET_ARGS="${RIFTGET_ARGS:-http://192.168.1.4:8080/}"
riftget() {
    make -C "$ROOT" -s userland/bin/riftget >/dev/null || { echo "rift: riftget does not build"; exit 1; }
    local app="$W/riftget.app"
    rm -rf "$app" && mkdir -p "$app"
    "$PREFIX/bin/$TARGET-strip" -o "$app/app.bin" "$ROOT/userland/bin/riftget"
    sed "s|@ARGS@|$RIFTGET_ARGS|" "$HERE/riftget/app.ini.in" > "$app/app.ini"
    cp -R "$HERE/resources" "$app/resources"
    echo "rift: build/rift/riftget.app ($(du -sh "$app" | cut -f1)), args: $RIFTGET_ARGS"
}

# What riftget downloads, from the PC: a web server on port 8080 (WSL; the
# MacBook reaches it through WSL's mirrored networking and a Hyper-V
# firewall rule for TCP 8080) serving SERVE_DIR (default ~/rift-serve):
#   rift/league-assets  -> XPA_DIR (the archives, default ~/xpa-layered)
#   rift/rift-data      Rift's own data (build.sh data)
#   apps/rift.app       the app (build.sh app)
#   manifest.txt        "<size> <path>" for every file; the app's files
#                       "<size>:<crc32> <path>" (a new build of the same
#                       size is downloaded again)
# The user's game files stay in XPA_DIR: only linked, never copied here.
# On a Mac (2026-11-16) XPA_DIR is by default build/rift/xpa when there
# (xpapack of the Mac's League, in Docker: docs/rift-macbook-istruzioni.md).
xpa_serve() {
    local dir="${XPA_DIR:-$HOME/xpa-layered}" serve="${SERVE_DIR:-$HOME/rift-serve}"
    [ -n "${XPA_DIR:-}" ] || [ -d "$dir" ] || dir="$W/xpa"
    [ -d "$dir" ] || { echo "rift: no $dir (xpapack, then the layered set)"; exit 1; }
    [ -f "$W/rift.app/app.bin" ] || app
    mkdir -p "$serve/rift" "$serve/apps"
    ln -sfn "$dir" "$serve/rift/league-assets"
    data "$serve/rift"
    rm -rf "$serve/apps/rift.app" && cp -R "$W/rift.app" "$serve/apps/rift.app"
    # every file, links followed (find -printf is GNU's: not on a Mac)
    (cd "$serve" && python3 -c '
import os, zlib
paths = []
for top in ("rift", "apps"):
    for d, _, files in os.walk(top, followlinks=True):
        paths += [os.path.join(d, f) for f in files if not f.startswith(".")]
for path in sorted(paths):
    size = str(os.path.getsize(path))
    if path.startswith("apps/"):
        crc = 0
        with open(path, "rb") as f:
            for block in iter(lambda: f.read(1 << 20), b""):
                crc = zlib.crc32(block, crc)
        size = "%s:%08x" % (size, crc)
    print(size, path)' > manifest.txt)
    echo "rift: $serve: $(wc -l < "$serve/manifest.txt") files, $(du -shL "$serve" | cut -f1)"
    cd "$serve" && exec python3 -m http.server 8080 --bind 0.0.0.0
}

# Rift Ascend's own data (its scripts and assets, GPL-3.0; the Vorbis
# codebooks of ww2ogg, BSD), as <dir>/rift-data: <dir> is the game's disk,
# the folder that holds league-assets.
data() {
    local dir="${1:?usage: build.sh data DIR, the folder on the game disk that holds league-assets}"
    [ -d "$SRC/scripts" ] || fetch
    local out="$dir/rift-data"
    rm -rf "$out" && mkdir -p "$out/ThirdParty/ww2ogg"
    cp -R "$SRC/scripts" "$SRC/assets" "$out/"
    cp "$SRC/ThirdParty/ww2ogg/packed_codebooks_aoTuV_603.bin" "$SRC/ThirdParty/ww2ogg/COPYING" "$out/ThirdParty/ww2ogg/"
    echo "rift: $out ($(du -sh "$out" | cut -f1))"
}

# The game's files from the user's own League of Legends installation, as
# <dir>/league-assets/<Name>.wad/... (Rift Ascend's layout), with CDTB
# (CommunityDragon's toolbox, Python, in its own venv): each .wad.client of
# Game/DATA/FINAL, the files' names from CommunityDragon's hash lists; the
# files without a known name are left out (the game looks files up by
# name). --lazy: an interrupted run goes on where it stopped. ~40 GB.
# Never in git or in an image: the user's files (AGENTS.md).
assets() {
    local league="${1:?usage: build.sh assets LEAGUE DIR (LEAGUE: the League of Legends folder)}"
    local dir="${2:?usage: build.sh assets LEAGUE DIR (DIR: the folder on the game disk)}"
    local final="$league/Game/DATA/FINAL"
    [ -d "$final" ] || final="$league/DATA/FINAL"
    [ -d "$final" ] || { echo "rift: no Game/DATA/FINAL in $league"; exit 1; }
    local venv="${CDTB_VENV:-$HOME/cdtb-venv}"
    if [ ! -x "$venv/bin/cdtb" ]; then
        python3 -m venv "$venv"
        "$venv/bin/pip" install -q cdtb
    fi
    # Only the game files' names (hashes.game.txt): from CommunityDragon's
    # server, or (it answered 522 on 2026-11-14) the same list in parts from
    # its GitHub repository
    local hashes="$HOME/.local/share/cdragon/data/hashes/lol/hashes.game.txt"
    if [ ! -s "$hashes" ] &&
        ! "$venv/bin/python" -c "from cdtb.hashes import update_default_hashfile as u; u('hashes.game.txt')" 2>/dev/null; then
        echo "rift: CommunityDragon's server does not answer: the names from its GitHub repository"
        mkdir -p "$(dirname "$hashes")"
        rm -f "$hashes.part"
        for i in 0 1 2 3 4 5 6 7 8; do
            curl -sSLf "https://raw.githubusercontent.com/CommunityDragon/Data/master/hashes/lol/hashes.game.txt.$i" >> "$hashes.part"
        done
        mv "$hashes.part" "$hashes"
    fi
    echo "rift: $(wc -l < "$hashes") file names known"
    local out="$dir/league-assets" n=0 total
    total="$(find "$final" -name '*.wad.client' | wc -l)"
    mkdir -p "$out"
    find "$final" -name '*.wad.client' | sort | while read -r wad; do
        n=$((n + 1))
        local name
        name="$(basename "$wad" .client)"   # Map11.wad.client -> Map11.wad
        echo "rift: [$n/$total] $name"
        "$venv/bin/cdtb" wad-extract --lazy -u no -o "$out/$name" "$wad" >/dev/null
    done
    echo "rift: $out ($(du -sh "$out" | cut -f1))"
}

# The game server, for this computer (Linux: WSL on the home PC), not for
# PeOS: the client on the MacBook plays against it. RIFT_ASSETS: the user's
# league-assets (the server reads the game's files too).
RIFT_SERVER_BUILD="${RIFT_SERVER_BUILD:-$W/server-linux}"

server() {
    [ -f "$SRC/CMakeLists.txt" ] || fetch
    cmake -S "$SRC" -B "$RIFT_SERVER_BUILD" -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo -DLOLCLONE_BUILD_TESTS=OFF \
        -DLOLCLONE_PCH=OFF -DRIFT_WITH_PLATFORM=OFF -DGLFW_BUILD_X11=OFF -DGLFW_BUILD_WAYLAND=OFF >/dev/null
    # xpapack too: it packs the game's files for PeOS on this computer
    ninja -C "$RIFT_SERVER_BUILD" -j"$(jobs)" GameServer xpapack >/dev/null
    echo "rift: the game server, $RIFT_SERVER_BUILD/bin/RelWithDebInfo/GameServer (and xpapack beside it)"
}

# On every interface (--lan: the MacBook reaches it), 4 bots, waiting for
# one player, never closing by itself.
serve() {
    local bin="$RIFT_SERVER_BUILD/bin/RelWithDebInfo/GameServer"
    [ -x "$bin" ] || server
    # The game's data copied into WSL's own disk (only .bin, grids, tables:
    # 3 GB): from /mnt/<drive> every read is a 9p round trip, minutes.
    if [ -z "${RIFT_ASSETS:-}" ] && [ -d "$HOME/league-assets-server" ]; then
        RIFT_ASSETS="$HOME/league-assets-server"
    fi
    # A client on PeOS loads for minutes (the game on a USB stick): it may be
    # silent that long before the server drops it (patches/rift/0007)
    export RIFT_LOAD_TIMEOUT_S="${RIFT_LOAD_TIMEOUT_S:-900}"
    exec "$bin" --lan --name "PeOS test" --bots 4 --expect-players 1 --idle-timeout 0 \
        ${RIFT_ASSETS:+--assets "$RIFT_ASSETS"} --data "$SRC" --log-dir "$W/server-logs"
}

# The same game server on a Mac (2026-11-16): a Linux container of Docker
# Desktop (userland/ports/rift/docker/Dockerfile), with the source, the build,
# the game's files and the logs mounted from the Mac. UDP 5119 is published
# on the Mac's own addresses: the MacBook connects to this Mac's address on
# the home network (RIFT_ARGS, --connect <address>:5119).
RIFT_DOCKER_IMAGE="${RIFT_DOCKER_IMAGE:-peos-rift-server}"
RIFT_DOCKER_BUILD="${RIFT_DOCKER_BUILD:-$W/server-docker}"

docker_image() {
    docker info >/dev/null 2>&1 || { echo "rift: Docker does not answer: open -a Docker (and leave it on)"; exit 1; }
    docker build -q -t "$RIFT_DOCKER_IMAGE" "$HERE/docker" >/dev/null
}

docker_server() {
    [ -f "$SRC/CMakeLists.txt" ] || fetch
    docker_image
    mkdir -p "$RIFT_DOCKER_BUILD"
    # 2 compilers at a time by default: Docker Desktop's VM has 8 GB, and 12 at
    # once ran out of memory on the big files (luavm.cpp) with no clear error
    # (2026-11-16). The output goes to ninja.log; its end is shown on failure.
    docker run --rm -e JOBS="${RIFT_DOCKER_JOBS:-2}" -v "$SRC":/src -v "$RIFT_DOCKER_BUILD":/build \
        "$RIFT_DOCKER_IMAGE" bash -c '
        cmake -S /src -B /build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo -DLOLCLONE_BUILD_TESTS=OFF \
            -DLOLCLONE_PCH=OFF -DRIFT_WITH_PLATFORM=OFF -DGLFW_BUILD_X11=OFF -DGLFW_BUILD_WAYLAND=OFF >/dev/null &&
        ninja -C /build -j"$JOBS" GameServer > /build/ninja.log 2>&1 ||
        { grep -E " error: |fatal error|Killed|internal compiler error" /build/ninja.log | head -20; tail -5 /build/ninja.log; exit 1; }'
    [ -x "$RIFT_DOCKER_BUILD/bin/RelWithDebInfo/GameServer" ] || { echo "rift: no GameServer built"; exit 1; }
    echo "rift: the game server (Linux, in Docker), $RIFT_DOCKER_BUILD/bin/RelWithDebInfo/GameServer"
}

# RIFT_ASSETS: the game's files for the server; by default
# ~/league-assets-server (only the .bin, grids and tables, ~3 GB, as WSL had
# them), else build/rift/league-assets (the whole extraction of the Mac's own
# League, extract_assets.py: Docker on the Mac reads it fast enough, no copy
# needed; tried 2026-11-16). In the background: docker logs -f rift-server
# to follow it, docker-stop to stop it.
docker_serve() {
    local bin="$RIFT_DOCKER_BUILD/bin/RelWithDebInfo/GameServer"
    [ -x "$bin" ] || docker_server
    local assets="${RIFT_ASSETS:-$HOME/league-assets-server}"
    [ -n "${RIFT_ASSETS:-}" ] || [ -d "$assets" ] || assets="$W/league-assets"
    [ -d "$assets" ] || { echo "rift: no game files for the server in $assets (RIFT_ASSETS)"; exit 1; }
    mkdir -p "$W/server-logs"
    docker rm -f rift-server >/dev/null 2>&1 || true
    docker run -d --name rift-server -p 5119:5119/udp -e RIFT_LOAD_TIMEOUT_S="${RIFT_LOAD_TIMEOUT_S:-900}" \
        -v "$SRC":/src:ro -v "$RIFT_DOCKER_BUILD":/build:ro -v "$assets":/assets:ro -v "$W/server-logs":/logs \
        "$RIFT_DOCKER_IMAGE" /build/bin/RelWithDebInfo/GameServer --lan --name "PeOS test" --bots 4 \
        --expect-players 1 --idle-timeout 0 --assets /assets --data /src --log-dir /logs >/dev/null
    echo "rift: the game server runs (container rift-server, UDP 5119); the MacBook connects to one of:"
    ipconfig getifaddr en0 2>/dev/null || true
    ipconfig getifaddr en1 2>/dev/null || true
}

case "${1:-}" in
    server) server ;;
    serve) serve ;;
    docker-server) docker_server ;;
    docker-serve) docker_serve ;;
    docker-stop) docker rm -f rift-server >/dev/null 2>&1 && echo "rift: the game server stopped" ;;
    fetch) fetch ;;
    glfw) glfw ;;
    glfwtest) glfwtest ;;
    client) client ;;
    app) app ;;
    riftget) riftget ;;
    xpa-serve) xpa_serve ;;
    data) data "${2:-}" ;;
    assets) assets "${2:-}" "${3:-}" ;;
    *) echo "usage: $0 fetch|glfw|glfwtest|client|app|riftget|xpa-serve|data|assets|server|serve|docker-server|docker-serve|docker-stop"; exit 2 ;;
esac
