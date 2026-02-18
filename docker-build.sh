#!/bin/bash
# Helper script to test Linux builds using Docker/Podman
# This script mounts the current source tree into a Linux container,
# builds kte in terminal-only mode, and runs the test suite.

set -e

# Detect whether to use docker or podman
if command -v docker &> /dev/null; then
    CONTAINER_CMD="docker"
elif command -v podman &> /dev/null; then
    CONTAINER_CMD="podman"
else
    echo "Error: Neither docker nor podman found in PATH"
    exit 1
fi

IMAGE_NAME="kte-linux"

# Check if image exists, if not, build it
if ! $CONTAINER_CMD image inspect "$IMAGE_NAME" &> /dev/null; then
    echo "Building $IMAGE_NAME image..."
    $CONTAINER_CMD build -t "$IMAGE_NAME" .
fi

# Run the container with the current directory mounted
echo "Running Linux build and tests..."
$CONTAINER_CMD run --rm -v "$(pwd):/kte" "$IMAGE_NAME"
