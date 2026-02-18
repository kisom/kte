# Minimal Dockerfile for building and testing kte on Linux
# This container provides a build environment with all dependencies.
# Mount the source tree at /kte when running the container.
FROM ubuntu:22.04

# Avoid interactive prompts during package installation
ENV DEBIAN_FRONTEND=noninteractive

# Install build dependencies
RUN apt-get update && apt-get install -y \
    build-essential \
    cmake \
    libncursesw5-dev \
    && rm -rf /var/lib/apt/lists/*

# Set working directory where source will be mounted
WORKDIR /kte

# Default command: build and run tests
CMD ["sh", "-c", "cmake -B build -DBUILD_GUI=OFF -DBUILD_TESTS=ON && cmake --build build --target kte && cmake --build build --target kte_tests && ./build/kte_tests"]
