# RizWEB container image (Linux, netRector backend, BoringSSL). Not built in CI yet:
#   docker build -t rizweb .
#   docker run --rm -p 8080:8080 -p 8443:8443 -v $PWD/www:/app/www rizweb
FROM ubuntu:22.04 AS build
ARG BORINGSSL_REF=289645a450721a7210bf9c69d42bf40a0f1ee730
ARG NETRECTOR_REF=main
RUN apt-get update && apt-get install -y --no-install-recommends \
        build-essential cmake ninja-build git ca-certificates golang-go zlib1g-dev \
    && rm -rf /var/lib/apt/lists/*
WORKDIR /src
RUN git clone --depth 1 --branch "$NETRECTOR_REF" https://github.com/crazyfull/netRector.git deps/netRector
RUN git clone https://github.com/google/boringssl.git deps/boringssl-src \
    && git -C deps/boringssl-src checkout "$BORINGSSL_REF" \
    && deps/netRector/scripts/patch_boringssl.sh deps/boringssl-src \
    && cmake -GNinja -S deps/boringssl-src -B deps/boringssl-build -DCMAKE_BUILD_TYPE=Release \
    && ninja -C deps/boringssl-build crypto ssl \
    && mkdir -p /opt/bssl/lib && cp deps/boringssl-build/libssl.a deps/boringssl-build/libcrypto.a /opt/bssl/lib/ \
    && cp -r deps/boringssl-src/include /opt/bssl/include
COPY . /src/rizweb
RUN cmake -GNinja -S rizweb -B out -DCMAKE_BUILD_TYPE=Release -DRIZWEB_STATIC=ON -DRIZWEB_BUILD_TESTS=OFF \
        -DNETRECTOR_DIR=/src/deps/netRector -DRIZWEB_BORINGSSL_DIR=/opt/bssl \
    && cmake --build out

FROM scratch
COPY --from=build /src/out/rizweb /app/rizweb
COPY --from=build /src/rizweb/resource /app/resource
WORKDIR /app
EXPOSE 8080 8443
ENTRYPOINT ["/app/rizweb"]
