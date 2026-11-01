# PS3 homebrew toolchain for this project.
#
# Uses ps3dev's prebuilt nightly rather than compiling GCC twice: the toolchain
# build is roughly an hour, the download is a minute, and the hash pins what we
# actually get. Bump the three ARGs together when moving to a new nightly.
FROM ubuntu:24.04

ARG PS3DEV_TARBALL=https://github.com/ps3dev/ps3dev/releases/download/nightly-2026-10-12/ps3dev-linux-X64.tar.gz
ARG PS3DEV_SHA256=76b1b3bc9ac260a4ece86da520225da7f2f4e334174e37b850093ff925eeb900

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y --no-install-recommends \
        ca-certificates \
        curl \
        file \
        make \
        git \
        python3 \
        zlib1g \
        libelf1 \
        libncurses6 \
        luajit \
        lua5.1 \
    && rm -rf /var/lib/apt/lists/*

# cgcomp loads libCg at runtime and fails without it. Ubuntu ships libCg in multiverse.
RUN sed -i 's/^Components: main.*/Components: main universe multiverse/' /etc/apt/sources.list.d/ubuntu.sources \
    && apt-get update \
    && apt-get install -y --no-install-recommends libcg=3.1.0013-5build1 \
    && rm -rf /var/lib/apt/lists/*

# The release tarball is verified by hash before it is unpacked, and the layout is
# probed rather than assumed: some nightlies wrap everything in one top directory,
# others put bin/ ppu/ spu/ at the root.
RUN set -eux; \
    curl -fsSL --retry 3 -o /tmp/ps3dev.tar.gz "$PS3DEV_TARBALL"; \
    echo "$PS3DEV_SHA256  /tmp/ps3dev.tar.gz" | sha256sum -c -; \
    mkdir -p /tmp/unpack; \
    tar -xzf /tmp/ps3dev.tar.gz -C /tmp/unpack; \
    count=$(ls -A /tmp/unpack | wc -l); \
    if [ "$count" = "1" ] && [ -d "/tmp/unpack/$(ls -A /tmp/unpack)" ]; then \
        mv "/tmp/unpack/$(ls -A /tmp/unpack)" /usr/local/ps3dev; \
    else \
        mkdir -p /usr/local/ps3dev; \
        mv /tmp/unpack/* /usr/local/ps3dev/; \
    fi; \
    rm -rf /tmp/unpack /tmp/ps3dev.tar.gz

ENV PS3DEV=/usr/local/ps3dev
ENV PSL1GHT=$PS3DEV
ENV PATH=$PATH:/usr/local/ps3dev/bin:/usr/local/ps3dev/ppu/bin:/usr/local/ps3dev/spu/bin

# A broken shader compiler fails the image build rather than a later project build.
RUN set -eux; \
    mkdir -p /tmp/cg; \
    echo 'void main(float4 p:POSITION,out float4 o:POSITION,uniform float4x4 m){o=mul(p,m);}' > /tmp/cg/t.vcg; \
    echo 'void main(float2 t:TEXCOORD0,out float4 c:COLOR,uniform sampler2D s){c=tex2D(s,t);}' > /tmp/cg/t.fcg; \
    cgcomp -v /tmp/cg/t.vcg /tmp/cg/t.vpo; \
    cgcomp -f /tmp/cg/t.fcg /tmp/cg/t.fpo; \
    rm -rf /tmp/cg

# Lua 5.1, cross-built for the PPU and installed as a portlib.
#
# Vanilla interpreter on purpose: LuaJIT's PowerPC path is not something this
# project can depend on, and the game only needs 5.1 semantics. The standalone
# interpreter and luac are excluded — we embed the library and supply our own main.
ARG LUA_VERSION=5.1.5
ARG LUA_SHA256=2640fc56a795f29d28ef15e13c34a47e223960b0240e8cb0a82d9b0738695333

RUN set -eux; \
    curl -fsSL -o /tmp/lua.tar.gz "https://www.lua.org/ftp/lua-${LUA_VERSION}.tar.gz"; \
    echo "${LUA_SHA256}  /tmp/lua.tar.gz" | sha256sum -c -; \
    mkdir -p /tmp/lua; \
    tar -xzf /tmp/lua.tar.gz -C /tmp/lua --strip-components=1; \
    cd /tmp/lua/src; \
    for f in *.c; do \
        case "$f" in lua.c|luac.c|print.c) continue ;; esac; \
        ppu-gcc -mcpu=cell -O2 -Wall -c "$f" -o "${f%.c}.o"; \
    done; \
    ppu-ar rcu liblua.a *.o; \
    mkdir -p /usr/local/ps3dev/portlibs/ppu/lib /usr/local/ps3dev/portlibs/ppu/include; \
    cp liblua.a /usr/local/ps3dev/portlibs/ppu/lib/; \
    cp lua.h luaconf.h lualib.h lauxlib.h /usr/local/ps3dev/portlibs/ppu/include/; \
    rm -rf /tmp/lua /tmp/lua.tar.gz

WORKDIR /work
