#!/bin/bash

# SHM (Shared Memory Communication Manager) Coverage Report Generator
# This script generates test coverage reports locally for development

set -e

echo "🔍 SHM Coverage Report Generator"
echo "================================"

# Configuration
BUILD_DIR="build_Coverage"
COVERAGE_DIR="$BUILD_DIR/coverage_html"

# Check if lcov and gcovr are installed
if ! command -v lcov &> /dev/null; then
    echo "❌ lcov is not installed. Installing..."
    sudo apt-get update && sudo apt-get install -y lcov
fi

if ! command -v gcovr &> /dev/null; then
    echo "❌ gcovr is not installed. Installing..."
    sudo apt-get update && sudo apt-get install -y gcovr
fi

# Clean previous build
echo "🧹 Cleaning previous build..."
rm -rf "$BUILD_DIR"
mkdir -p "$BUILD_DIR"

# Configure with coverage
echo "⚙️  Configuring CMake with coverage..."
cd "$BUILD_DIR"
# BUILD_TESTS を渡さないとテストが一切ビルドされず、ctest がテスト 0 件のまま
# 終了コード 0 で返る。その結果 gcda が 1 つも生成されず、後段の
# 「No coverage data found」で初めて失敗するという分かりにくい壊れ方をする。
cmake .. -DCMAKE_BUILD_TYPE=Debug -DDEBUG=ON -DENABLE_COVERAGE=ON -DBUILD_TESTS=ON

# Build
echo "🔨 Building project..."
make -j$(nproc)

# Run tests
echo "🧪 Running tests..."
# テストが 0 件でも ctest は終了コード 0 を返すので、登録件数を先に確認する
if ! ctest -N | grep -qE "Total Tests: [1-9]"; then
    echo "❌ No tests were registered. Check that BUILD_TESTS is enabled."
    exit 1
fi
ctest --output-on-failure

# Check for gcda files
echo "📊 Checking coverage data..."
GCDA_COUNT=$(find . -name "*.gcda" -type f | wc -l)
echo "Found $GCDA_COUNT gcda files"

if [ $GCDA_COUNT -eq 0 ]; then
    echo "❌ No coverage data found. Make sure tests are running."
    exit 1
fi

# Generate coverage report with environment-specific error handling
echo "📈 Generating coverage report..."

# Try different lcov configurations based on version
LOCAL_ERROR_FLAGS="--ignore-errors gcov,source"
CI_ERROR_FLAGS="--ignore-errors mismatch,gcov,source,negative"

# Detect lcov version and use appropriate flags
LCOV_VERSION=$(lcov --version 2>/dev/null | grep -o '[0-9]\+\.[0-9]\+' | head -n1 || echo "1.14")
echo "LCOV version: $LCOV_VERSION"

# Try different branch coverage options and error flags
if lcov --directory . --capture --output-file coverage.info --rc branch_coverage=1 $LOCAL_ERROR_FLAGS 2>/dev/null; then
    echo "✅ Using new lcov syntax with local error flags"
    BRANCH_FLAG="--rc branch_coverage=1"
    ERROR_FLAGS="$LOCAL_ERROR_FLAGS"
elif lcov --directory . --capture --output-file coverage.info --rc lcov_branch_coverage=1 $LOCAL_ERROR_FLAGS 2>/dev/null; then
    echo "✅ Using old lcov syntax with local error flags"
    BRANCH_FLAG="--rc lcov_branch_coverage=1"
    ERROR_FLAGS="$LOCAL_ERROR_FLAGS"
elif lcov --directory . --capture --output-file coverage.info $LOCAL_ERROR_FLAGS 2>/dev/null; then
    echo "✅ Using basic lcov syntax with local error flags"
    BRANCH_FLAG=""
    ERROR_FLAGS="$LOCAL_ERROR_FLAGS"
else
    echo "⚠️  Using fallback lcov configuration"
    lcov --directory . --capture --output-file coverage.info || {
        echo "❌ Failed to capture coverage data"
        exit 1
    }
    BRANCH_FLAG=""
    ERROR_FLAGS=""
fi

# Filter out external libraries and test files
echo "🔧 Filtering coverage data..."
lcov --remove coverage.info '/usr/*' '*/external/*' '*/tests/*' '*/test/*' --output-file coverage_filtered.info $BRANCH_FLAG $ERROR_FLAGS --ignore-errors unused || \
lcov --remove coverage.info '/usr/*' '*/tests/*' '*/test/*' --output-file coverage_filtered.info $ERROR_FLAGS --ignore-errors unused || {
    echo "❌ Failed to filter coverage data"
    exit 1
}

# Generate HTML report
echo "🌐 Generating HTML report..."
genhtml coverage_filtered.info --output-directory coverage_html --branch-coverage 2>/dev/null || \
genhtml coverage_filtered.info --output-directory coverage_html || {
    echo "❌ Failed to generate HTML report"
    exit 1
}

# Extract coverage percentage
COVERAGE_PERCENT=$(lcov --summary coverage_filtered.info 2>&1 | grep "lines" | grep -o '[0-9]\+\.[0-9]\+%' | head -n1 || echo "0.0%")
FUNCTION_COVERAGE=$(lcov --summary coverage_filtered.info 2>&1 | grep "functions" | grep -o '[0-9]\+\.[0-9]\+%' | head -n1 || echo "0.0%")

echo ""
echo "✅ Coverage Report Generated Successfully!"
echo "========================================"
echo "📊 Line Coverage:     $COVERAGE_PERCENT"
echo "🔧 Function Coverage: $FUNCTION_COVERAGE"
echo "📁 Report Location:   $(pwd)/coverage_html/index.html"
echo ""
echo "🌐 To view the report:"
echo "   xdg-open $(pwd)/coverage_html/index.html"
echo "   # or"
echo "   firefox $(pwd)/coverage_html/index.html"
echo ""

# Return to original directory
cd ..

echo "🎉 Coverage generation complete!"
