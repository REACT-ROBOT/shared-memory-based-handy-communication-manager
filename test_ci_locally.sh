#!/bin/bash

# Local CI Test Script
# This script simulates the GitHub Actions CI pipeline locally

set -e

echo "=== Local CI Test for shared-memory-based-handy-communication-manager ==="

# Test both Release and Debug builds
BUILD_TYPES=("Release" "Debug")

for BUILD_TYPE in "${BUILD_TYPES[@]}"; do
    echo "================================="
    echo "Testing $BUILD_TYPE build"
    echo "================================="
    
    # Clean previous build
    rm -rf build_$BUILD_TYPE
    mkdir -p build_$BUILD_TYPE
    cd build_$BUILD_TYPE
    
    # Configure CMake
    if [ "$BUILD_TYPE" = "Debug" ]; then
        echo "Configuring with AddressSanitizer..."
        cmake .. -DCMAKE_BUILD_TYPE=$BUILD_TYPE -DDEBUG=ON
    else
        echo "Configuring Release build..."
        cmake .. -DCMAKE_BUILD_TYPE=$BUILD_TYPE
    fi
    
    # Build
    echo "Building..."
    make -j$(nproc)
    
    # Run tests
    echo "Running tests..."
    if [ "$BUILD_TYPE" = "Debug" ]; then
        echo "Running with AddressSanitizer..."
        export ASAN_OPTIONS=abort_on_error=1:halt_on_error=1
    fi
    
    ctest --output-on-failure
    
    cd ..
    echo "$BUILD_TYPE build completed successfully!"
done

echo "=== All CI tests completed successfully! ==="