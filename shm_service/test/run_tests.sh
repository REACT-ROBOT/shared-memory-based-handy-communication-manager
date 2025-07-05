#!/bin/bash

# shm_service Unit Test Runner
# This script builds and runs the shm_service unit tests in a Docker environment

set -e

echo "Building and running shm_service unit tests..."

# Create build directory
mkdir -p build
cd build

# Build the project
echo "Building shm_service tests..."
cmake ..
make -j$(nproc)

# Run the tests
echo "Running shm_service unit tests..."
./shm_service_test

echo "Test execution completed!"