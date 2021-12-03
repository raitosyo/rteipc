FROM debian:buster-slim

RUN set -eux; \
    \
    apt-get update; \
    apt-get install -y --no-install-recommends \
            bash \
            bash-completion \
            libb64-dev \
            libevent-dev \
            libgpiod-dev \
            libudev-dev \
            libyaml-dev \
    ; \
    rm -rf /var/lib/apt/lists/*

RUN set -eux; \
    \
    runtimeDeps="$(apt-mark showmanual)"; \
    apt-get update; \
    apt-get install -y --no-install-recommends \
            ca-certificates \
            cmake \
            g++ \
            gcc \
            make \
            pkg-config \
            wget \
    ; \
    rm -rf /var/lib/apt/lists/*; \
    \
    uri=https://github.com/raitosyo/rteipc/archive/refs/tags/; \
    file=v0.2.2.tar.gz; \
    wget -O rteipc.tar.gz $uri$file; \
    mkdir src; \
    tar -xf rteipc.tar.gz -C src --strip-components=1; \
    rm rteipc.tar.gz; \
    cd src; \
    # build rtemgr and demo
    sed -i '/^#add_subdirectory(src\/rtemgr)/s/^#//' CMakeLists.txt; \
    sed -i '/^#add_subdirectory(demo)/s/^#//' CMakeLists.txt; \
    mkdir build && cd build; \
    cmake .. && cmake --build .; \
    make install; \
    cd .. && rm -rf src; \
    \
    apt-mark auto '.*' > /dev/null; \
    [ -z "$runtimeDeps" ] || apt-mark manual $runtimeDeps; \
    apt-get purge -y --auto-remove -o APT::AutoRemove::RecommendsImportant=false

ENTRYPOINT rtemgrd -B && /bin/bash
