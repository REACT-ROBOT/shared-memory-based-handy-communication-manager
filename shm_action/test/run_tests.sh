#!/bin/bash

# shm_action Unit Test Runner
# This script builds and runs the shm_action unit tests in a Docker environment

set -e

echo "Building and running shm_action unit tests..."

# Create build directory
mkdir -p build
cd build

# Build the project
echo "Building shm_action tests..."
cmake ..
make -j$(nproc)

# Run the tests
echo "Running shm_action unit tests..."
./shm_action_test

echo "Test execution completed!"