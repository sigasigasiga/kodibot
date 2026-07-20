# TDLIB BUILDER ================================================================
FROM alpine:latest AS tdlib_builder

# tdlib build essentials
RUN apk add --no-cache build-base cmake

# tdlib build dependencies
RUN apk add --no-cache gperf openssl-dev zlib-dev linux-headers

WORKDIR /tdlib
COPY third_party/tdlib /tdlib
RUN cmake -S . -B build -DCMAKE_BUILD_TYPE=Release && \
    cmake --build build --parallel `nproc` && \
    cmake --install build --prefix /usr/local

# KODIBOT BUILDER ==============================================================
FROM alpine:latest AS kodibot_builder

# kodibot build essentials
RUN apk add --no-cache build-base clang22 clang22-extra-tools cmake ninja-build

ENV CC=clang-22
ENV CXX=clang++-22

# kodibot build dependencies
RUN apk add --no-cache spdlog-dev openssl-dev boost-dev

WORKDIR /source
COPY --parents ./CMakeLists.txt ./src/ ./third_party/  /source/
COPY --from=tdlib_builder /usr/local /usr/local

ARG BUILD_TYPE=RelWithDebInfo
ARG VERSION=v0.0.0-UNKNOWN

RUN --mount=type=cache,target=/source/build \
    cmake \
        -S . -B build \
        -G Ninja -DCMAKE_MAKE_PROGRAM='/usr/lib/ninja-build/bin/ninja' \
        -DCMAKE_BUILD_TYPE=${BUILD_TYPE} \
        -DKODIBOT_VERSION=${VERSION} && \
    cmake --build build --parallel `nproc` && \
    cp build/src/kodibot.x /usr/local/bin/kodibot && \
    echo 'Build finished, artifact at /usr/local/bin/kodibot'

# RUNTIME ======================================================================
FROM alpine:latest AS runtime

RUN apk add --no-cache openssl zstd boost-program_options spdlog

COPY --from=kodibot_builder /usr/local/bin/kodibot /usr/local/bin/kodibot

EXPOSE 9988

ENTRYPOINT ["/usr/local/bin/kodibot"]
