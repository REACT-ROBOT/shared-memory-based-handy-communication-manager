# SHM

[![CI](https://github.com/REACT-ROBOT/shared-memory-based-handy-communication-manager/workflows/Shared%20Memory%20Communication%20Manager%20CI/badge.svg)](https://github.com/REACT-ROBOT/shared-memory-based-handy-communication-manager/actions)
[![Coverage](https://img.shields.io/endpoint?url=https://gist.githubusercontent.com/hijimasa/0de6c8879fb6085dd4e0fdbc3b4cf451/raw/shm_coverage.json)](https://github.com/REACT-ROBOT/shared-memory-based-handy-communication-manager/actions "Live coverage from CI")
[![License](https://img.shields.io/badge/license-Apache%202.0-blue.svg)](LICENSE)

## Abstruct

SHM is short for "Shared-memory based Handy-communication Manager".
This provides ROS(Robot Operating System)-like interprocess Topic communication with shared memory.
Service and Action were also provided up to v1.x, but they had no users and their
handling of pthread objects in shared memory was unsafe, so they were removed in v2.0.0.
This is based on the library developed by Dr. Prof. Koichi Ozaki for his laboratory.

## DEMO

![shm_movie](https://user-images.githubusercontent.com/30435850/222885743-e069eb7a-e07d-4f8c-89e5-f71b443fc39e.gif)

This is Publisher/Subscriber model communication demo.
Left terminal publish class user made.
Right terminal subscribe class.

## Features

- High-speed interprocess communication (expectation)
- If the class you made is fixed size, the class can be used with nothing.
- Access restrictions can be set up with POSIX shared memory permission.

## Requirements

- POSIX shared memory system

  SHM use POSIX shared memory system.
  The system is included in Linux such as Ubuntu.

- Python3 (optional)

  SHM support the usecase with Python.

- Boost.Python (optional)

  This library is required for support the usecase with Python.

## How to build

This use CMake to build.
Below is how to introduce and build SHM.

1. clone or add for submodule this repository.
   ```
   $ cd <Your_cmake_ws>/src
   $ git clone https://github.com/REACT-ROBOT/shared-memory-based-handy-communication-manager.git
   or
   $ git submodule add https://github.com/REACT-ROBOT/shared-memory-based-handy-communication-manager.git
   $ gedit CMakeLists.txt
   add "add_subdirectory(shared-memory-based-handy-communication-manager)"
   ```

2. build programs.
   ```
   $ cd <Your_cmake_ws>
   $ cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
   $ cmake --build build -j$(nproc)
   ```

   If you add this as a subdirectory, put `enable_testing()` in your **top-level**
   `CMakeLists.txt`, before `add_subdirectory(...)`. CTest only registers tests
   for the directory tree below the `enable_testing()` call, so without it
   `ctest` in your build directory reports "No tests were found".

   Note that the shared libraries are built with `PREFIX ""`, so the artifacts are
   `shm_base.so` and `shm_pub_sub.so` — **without** the usual `lib` prefix.
   `-lshm_pub_sub` will not link; use `-l:shm_pub_sub.so`, or link the CMake
   target, which is what the installed package export gives you:

   ```cmake
   find_package(shm_base    REQUIRED)   # must come first
   find_package(shm_pub_sub REQUIRED)
   target_link_libraries(your_target shm_pub_sub)
   ```

### Building with Coverage

To generate test coverage reports locally:

```bash
# Easy local coverage generation (recommended)
./generate_coverage.sh

# View the generated report
xdg-open build_Coverage/coverage_html/index.html  # Linux
```

**Current Coverage**: High test coverage with comprehensive test suite

The coverage report includes:
- Line-by-line coverage visualization
- Function coverage statistics  
- Branch coverage analysis
- Detailed HTML report with source code highlighting

### Build Options

| Option | Default | What it does |
|---|---|---|
| `BUILD_TESTS` | `OFF` | Build the test programs. Also forces `SHM_ENABLE_TEST_HOOKS=ON`. |
| `BUILD_EXAMPLES` | `OFF` | Build `shm_pub_sub/samples`. |
| `DEBUG` | `OFF` | Debug symbols. |
| `ENABLE_COVERAGE` | `OFF` | Coverage instrumentation (implies debug symbols). |
| `SHM_STRICT_TYPE_CHECK` | `ON` | Require trivially-copyable payload types at compile time. |
| `SHM_REQUIRE_LAYOUT` | `OFF` | Require every payload type to declare its wire format with `SHM_DECLARE_LAYOUT` / `SHM_DECLARE_SERIALIZED_FORMAT`. Turn on per package while migrating; `shm_tool doctor` lists which topics are still undeclared. |
| `SHM_ENABLE_TEST_HOOKS` | `OFF` | Hooks that let tests interleave a generation cut-over with a publish. Nothing remains in a release build. |
| `SANITIZER` | `none` | `address` / `thread` / `undefined`. `thread` is the one that matters here; `undefined` is worth running with alignment checking. |

```bash
# Build with tests  (the option is BUILD_TESTS, not CMake's BUILD_TESTING)
cmake -S . -B build -DBUILD_TESTS=ON

# Chase data races
cmake -S . -B build -DBUILD_TESTS=ON -DSANITIZER=thread
```

## Testing

### Running Tests

```bash
cmake -S . -B build -DBUILD_TESTS=ON
cmake --build build -j$(nproc)
cd build && ctest --output-on-failure

# A specific suite
ctest --output-on-failure -R "SHMPubSubTest"
```

The tests share `/dev/shm`, so they are serialized through a CTest resource lock
(`RESOURCE_LOCK dev_shm`) and `ctest -j<N>` will not actually overlap them. To run
several builds at once, or to keep a test run from touching the machine's real
segments, give each one its own `/dev/shm` in a mount namespace:

```bash
unshare -rm --propagation private bash -c \
  "mount -t tmpfs tmpfs /dev/shm && cd build && ctest --output-on-failure"
```

### Test Coverage

The project maintains comprehensive test coverage with automated CI testing:

- **Integration tests**: Publisher/Subscriber patterns, including the vector
  specialization and the Python binding
- **Regression tests**: every finding from the five review rounds has a test that was
  verified to fail when its fix is reverted (see [`review/`](review/))
- **Fault injection**: corrupted headers, killed writers, and generation cut-overs
  interleaved with a publish

**Note**: Performance tests may occasionally fail in CI environments due to timing constraints, but this does not affect the core functionality or coverage reporting.

### Continuous Integration

GitHub Actions automatically:
- Builds and tests on multiple configurations (Release/Debug)
- Runs AddressSanitizer for memory safety validation
- Generates coverage reports and uploads as artifacts
- Comments coverage results on Pull Requests
- Validates cross-platform compatibility

## Documentation Generation

You can generate HTML documentation using Doxygen. The documentation is available in both English and Japanese.

### Build Documentation

```bash
$ cd <Your_cmake_ws>/build
$ make shm_doc         # Build both English and Japanese documentation
$ make shm_doc_en      # Build English documentation only
$ make shm_doc_jp      # Build Japanese documentation only
```

### Documentation Structure

- **English documentation**: Generated in `docs/` directory
- **Japanese documentation**: Generated in `docs/docs_jp/` directory
- **Manual pages**: Located in `manual/` directory (Markdown source files)

### Prerequisites

- Doxygen must be installed on your system
- All documentation sources are included in the `manual/` folder

### Accessing Documentation

After building, open the following files in your web browser:
- English: `docs/index.html`
- Japanese: `docs/docs_jp/index.html`

## Manuals / Tutorials

Documentation is available both locally and online:

- **Online Documentation**: [GitHub Pages](https://react-robot.github.io/shared-memory-based-handy-communication-manager/index.html)
- **Local Documentation**: Available in the `manual/` directory
  - English manuals: `manual/*_en.md`
  - Japanese manuals: `manual/*_jp.md`

The documentation includes:
- Introduction and quickstart guides
- Detailed tutorials for Publisher/Subscriber patterns
- Python integration examples
- API reference and troubleshooting guides

# License

SHM is under [Apache 2.0 License](https://www.apache.org/licenses/LICENSE-2.0).
