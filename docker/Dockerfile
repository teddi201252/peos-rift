# Rift Ascend's game server on the Mac (docs/rift-ascend-port-plan.md): a Linux
# container that builds and runs GameServer, as WSL did on the home PC. Only
# the tools: the source (build/rift/rift-ascend), the build, the game's files
# and the logs are mounted from the Mac (userland/ports/rift/build.sh
# docker-server, docker-serve), never copied into the image.
FROM ubuntu:24.04

ENV DEBIAN_FRONTEND=noninteractive
# The packages Rift Ascend's own build.sh lists for Linux, without the
# windowing and GPU ones (the server draws nothing: GLFW is built with no
# X11 and no Wayland).
RUN apt-get update && apt-get install -y --no-install-recommends \
        build-essential cmake ninja-build pkg-config git ca-certificates \
        libssl-dev libfreetype-dev libcurl4-openssl-dev python3 \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /build
