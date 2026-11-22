# Host harness: the game's Lua on the same Lua 5.1.5 that the PS3 build embeds,
# compiled for the host, so long runs can be measured in minutes instead of on a console.
FROM ubuntu:24.04

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y --no-install-recommends \
        ca-certificates \
        curl \
        gcc \
        libc6-dev \
        make \
        patch \
        zlib1g-dev \
        libluajit-5.1-dev \
        valgrind \
    && rm -rf /var/lib/apt/lists/*

# Same version and hash as docker/Dockerfile. run.sh builds it on every run, with the
# repository's docker/lua/fast_table_access.patch, so the harness and the PS3 build run the
# same interpreter.
ARG LUA_VERSION=5.1.5
ARG LUA_SHA256=2640fc56a795f29d28ef15e13c34a47e223960b0240e8cb0a82d9b0738695333

RUN set -eux; \
    curl -fsSL -o /opt/lua-5.1.5.tar.gz "https://www.lua.org/ftp/lua-${LUA_VERSION}.tar.gz"; \
    echo "${LUA_SHA256}  /opt/lua-5.1.5.tar.gz" | sha256sum -c -

WORKDIR /work
