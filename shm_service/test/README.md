# shm_service Unit Tests

This directory contains comprehensive unit tests for the shm_service module.

## Test Coverage

The test suite covers the following functionality:

### Basic Functionality Tests
- **BasicServiceCallTest**: Tests basic request-response communication
- **SimpleIntServiceTest**: Tests with simple integer wrapper class
- **ClassTestServiceTest**: Tests with complex class structures
- **FloatServiceTest**: Tests with floating-point data types

### Error Handling Tests
- **NonPODClassErrorTest**: Verifies proper error handling for non-POD classes
- **ClientCallWithoutServerTest**: Tests behavior when server is not available

### Concurrency Tests
- **MultipleClientsTest**: Tests multiple clients accessing the same service
- **RapidRequestsTest**: Tests rapid sequential requests

### Robustness Tests
- **ServiceReconnectionTest**: Tests service restart scenarios
- **LargeDataTest**: Tests with large data structures (1000 integers)
- **PerformanceTest**: Measures request-response latency

## How to Build and Run

### Prerequisites
- Docker environment with the project setup
- Google Test framework
- CMake 3.8 or higher

### Build and Run Tests

#### Using Docker (Recommended)
```bash
# Navigate to the project root
cd /home/hijikata/irlab_ws/REACT-simulator

# Build the Docker image
docker build -t react-simulator .

# Run the container with the test
docker run -it react-simulator bash

# Inside the container, navigate to the test directory
cd /workspace/shm_ws/src/shared-memory-based-handy-communication-manager/shm_service/test

# Run the tests
./run_tests.sh
```

#### Manual Build (if not using Docker)
```bash
# Navigate to the test directory
cd /home/hijikata/irlab_ws/REACT-simulator/shm_ws/src/shared-memory-based-handy-communication-manager/shm_service/test

# Create build directory
mkdir -p build
cd build

# Configure and build
cmake ..
make -j$(nproc)

# Run tests
./shm_service_test
```

## Test Structure

### Test Data Classes
- `SimpleInt`: Simple integer wrapper for testing
- `SimpleFloat`: Simple float wrapper for testing  
- `ClassTest`: Complex class with arrays for comprehensive testing
- `BadClass`: Non-POD class for error testing

### Service Functions
- `addOneService`: Adds 1 to integer input
- `doubleService`: Doubles SimpleInt input
- `processClassTest`: Complex processing of ClassTest objects
- `divideService`: Divides float input by 2

### Test Fixture
- `SHMServiceTest`: Provides setup/teardown for each test
- Automatically cleans up shared memory before and after tests

## Expected Output

Successful test execution should show:
```
[==========] Running 10 tests from 1 test suite.
[----------] Global test environment set-up.
[----------] 10 tests from SHMServiceTest
[ RUN      ] SHMServiceTest.BasicServiceCallTest
[       OK ] SHMServiceTest.BasicServiceCallTest (XXX ms)
[ RUN      ] SHMServiceTest.SimpleIntServiceTest
[       OK ] SHMServiceTest.SimpleIntServiceTest (XXX ms)
...
[----------] 10 tests from SHMServiceTest (XXX ms total)
[----------] Global test environment tear-down
[==========] 10 tests from 1 test suite ran. (XXX ms total)
[  PASSED  ] 10 tests.
```

## Notes

- Tests require shared memory support (available in Docker environment)
- Some tests use multi-threading to simulate real-world usage
- Performance test provides timing information for request-response latency
- All tests automatically clean up shared memory resources