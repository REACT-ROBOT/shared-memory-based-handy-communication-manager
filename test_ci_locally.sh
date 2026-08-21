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
    # BUILD_TESTS を渡さないとテストが一切ビルドされず、後段の ctest が
    # 「No tests were found!!!」を出しつつ終了コード 0 で返るため、
    # set -e でも検知できないまま成功したように見えてしまう。
    # .github/workflows/ci.yml と同じく必ず有効にする。
    if [ "$BUILD_TYPE" = "Debug" ]; then
        echo "Configuring with AddressSanitizer..."
        cmake .. -DCMAKE_BUILD_TYPE=$BUILD_TYPE -DDEBUG=ON -DBUILD_TESTS=ON
    else
        echo "Configuring Release build..."
        cmake .. -DCMAKE_BUILD_TYPE=$BUILD_TYPE -DBUILD_TESTS=ON
    fi
    
    # Build
    echo "Building..."
    make -j$(nproc)
    
    # Run tests
    echo "Running tests..."
    if [ "$BUILD_TYPE" = "Debug" ]; then
        echo "Running with AddressSanitizer..."
        export ASAN_OPTIONS=abort_on_error=1:halt_on_error=1:detect_thread_leaks=false:check_initialization_order=false:detect_stack_use_after_return=false
    fi
    
    # テストが 0 件でも ctest は終了コード 0 を返すので、登録件数を先に確認する
    if ! ctest -N | grep -qE "Total Tests: [1-9]"; then
        echo "No tests were registered. Check that BUILD_TESTS is enabled." >&2
        exit 1
    fi

    ctest --output-on-failure
    
    cd ..
    echo "$BUILD_TYPE build completed successfully!"
done

echo "=== All CI tests completed successfully! ==="