FROM ubuntu:24.04 AS build
ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y --no-install-recommends \
      build-essential g++-13 cmake ninja-build pkg-config \
      libopencv-dev libgrpc++-dev libprotobuf-dev protobuf-compiler protobuf-compiler-grpc \
    && rm -rf /var/lib/apt/lists/*
WORKDIR /src
COPY CMakeLists.txt ./
COPY src ./src
COPY server ./server
COPY proto ./proto
COPY tests/cpp ./tests/cpp
RUN cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_CXX_COMPILER=g++-13 -DSVG_BUILD_SERVER=ON -DSVG_BUILD_TESTS=ON \
 && cmake --build build --target svg-grpc-server

FROM build AS test
RUN cmake --build build --target engine-common-test && ctest --test-dir build --output-on-failure

FROM ubuntu:24.04
ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y --no-install-recommends \
      libopencv-core406t64 libopencv-imgproc406t64 libgrpc++1.51t64 libprotobuf32t64 \
    && rm -rf /var/lib/apt/lists/* \
    && useradd --system --no-create-home svg
COPY --from=build /src/build/svg-grpc-server /usr/local/bin/svg-grpc-server
USER svg
ENV SVG_GRPC_ADDR=0.0.0.0:50051
EXPOSE 50051
ENTRYPOINT ["svg-grpc-server"]
