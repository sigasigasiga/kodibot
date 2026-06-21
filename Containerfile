#### BUILDER
FROM alpine:latest AS builder

# Build essentials
RUN apk add --no-cache build-base clang22 clang22-extra-tools cmake ninja-build

ENV CC=clang-22
ENV CXX=clang++-22

# tdlib dependencies
RUN apk add --no-cache gperf openssl-dev

# kodibot dependencies
RUN apk add --no-cache spdlog-dev openssl-dev boost-dev

WORKDIR /src
COPY . /src
RUN mkdir -p /out && \
    cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo -DCMAKE_MAKE_PROGRAM='/usr/lib/ninja-build/bin/ninja' && \
    cmake --build build -j && \
    cp build/src/kodibot.x /out/ && \
    echo 'Build finished, artifact at /out/kodibot.x'

#### RUNTIME
FROM alpine:latest AS runtime

RUN apk add --no-cache openssl boost-libs libgcc libstdc++ ca-certificates spdlog

COPY --from=builder /out/kodibot.x /usr/local/bin/kodibot

EXPOSE 9988

# Entrypoint runs the binary. Kodi host/port are configurable via command-line options
# or environment variables as the program supports --kodi-host and --kodi-port.
ENTRYPOINT ["/usr/local/bin/kodibot"]
