# Minimal Dockerfile for building and testing kte on Linux
# This container provides a build environment with all dependencies.
# Mount the source tree at /kte when running the container.
FROM alpine:3.19

# Install build dependencies
RUN apk add --no-cache \
    g++ \
    cmake \
    make \
    ncurses-dev \
    sdl2-dev \
    mesa-dev \
    freetype-dev \
    libx11-dev \
    libxext-dev

# Set working directory where source will be mounted
WORKDIR /kte

# Default command: build and run tests
# Add DirectFB include path for SDL2 compatibility on Alpine
CMD ["sh", "-c", "cmake -B build -DBUILD_GUI=ON -DBUILD_TESTS=ON -DCMAKE_CXX_FLAGS='-I/usr/include/directfb' && cmake --build build --target kte && cmake --build build --target kge && cmake --build build --target kte_tests && ./build/kte_tests"]
