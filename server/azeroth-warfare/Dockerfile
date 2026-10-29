# Azeroth Warfare server image: builds this repository (vmangos + the fusion rules) and the map
# extractors, then ships a slim runtime. Build from the repository root:
#   docker build -f azeroth-warfare/Dockerfile -t azeroth-warfare .
FROM ubuntu:24.04 AS build
RUN apt-get update && DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends \
      build-essential cmake ninja-build git ca-certificates \
      libmysqlclient-dev libssl-dev zlib1g-dev libcurl4-openssl-dev \
    && rm -rf /var/lib/apt/lists/*
WORKDIR /src
COPY . /src
# Parallel compile jobs; empty means one per CPU. Lower it if the build runs out of memory.
ARG AW_BUILD_JOBS=
RUN cmake -S /src -B /build -G Ninja \
      -DCMAKE_BUILD_TYPE=RelWithDebInfo -DDEBUG_SYMBOLS=OFF -DBUILD_FOR_HOST_CPU=OFF \
      -DSUPPORTED_CLIENT_BUILD=5875 -DBUILD_EXTRACTORS=ON -DCMAKE_INSTALL_PREFIX=/opt/aw \
 && cmake --build /build ${AW_BUILD_JOBS:+-j $AW_BUILD_JOBS} \
 && /build/src/fusion/fusion_tests \
 && cmake --install /build

FROM ubuntu:24.04
RUN apt-get update && DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends \
      libmysqlclient21 libssl3t64 zlib1g libcurl4t64 mariadb-client ca-certificates curl unzip p7zip-full \
    && rm -rf /var/lib/apt/lists/*
COPY --from=build /opt/aw /opt/aw
COPY sql /opt/aw/sql
# The seed weapons.json, and catalog.json when this tree has one (it is made from an MW2 install:
# see private-assets.md at the repository root; without it the server reads weapons.json).
COPY contrib/fusion/ /opt/aw/seed/
COPY azeroth-warfare/scripts /opt/aw/scripts
COPY contrib/mmap/config.json /opt/aw/share/mmap-config.json
RUN chmod +x /opt/aw/scripts/*.sh
WORKDIR /opt/aw/bin
