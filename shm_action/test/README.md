# shm_action Unit Tests

This directory contains comprehensive unit tests for the shm_action module.

## Test Coverage

The test suite covers the following functionality:

### Basic Functionality Tests
- **BasicActionTest**: Tests basic Goal-Feedback-Result communication workflow
- **ComplexClassActionTest**: Tests with complex class structures (ClassTest)
- **ActionCancellationTest**: Tests goal cancellation and preemption
- **GoalRejectionTest**: Tests server-side goal rejection

### Error Handling Tests
- **NonPODClassErrorTest**: Verifies proper error handling for non-POD classes
- **ClientWithoutServerTest**: Tests behavior when server is not available

### Concurrency Tests
- **MultipleClientsTest**: Tests multiple clients accessing the same action server
- **FeedbackMonitoringTest**: Tests feedback message progression and monitoring

### Robustness Tests
- **ActionReconnectionTest**: Tests action server restart scenarios
- **PerformanceTest**: Measures action processing performance and latency

## Action Communication Pattern

The shm_action module implements a ROS-like action pattern:

```
Client                    Server
  |                         |
  |-------- Goal --------->|
  |                         |
  |<----- Feedback --------|  (periodic)
  |<----- Feedback --------|
  |                         |
  |<------ Result ---------|  (final)
```

### Action States
- **ACTIVE**: Action is currently being processed
- **SUCCEEDED**: Action completed successfully
- **REJECTED**: Server rejected the goal
- **PREEMPTED**: Action was cancelled by client

## Test Data Classes

### Simple Types
- `SimpleGoal`: Simple integer goal wrapper
- `SimpleResult`: Simple integer result wrapper  
- `SimpleFeedback`: Simple float feedback wrapper

### Complex Types
- `ClassTest`: Complex class with arrays for comprehensive testing
- `NonPODClass`: Non-POD class for error testing

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
cd /workspace/shm_ws/src/shared-memory-based-handy-communication-manager/shm_action/test

# Run the tests
./run_tests.sh
```

#### Manual Build (if not using Docker)
```bash
# Navigate to the test directory
cd /home/hijikata/irlab_ws/REACT-simulator/shm_ws/src/shared-memory-based-handy-communication-manager/shm_action/test

# Create build directory
mkdir -p build
cd build

# Configure and build
cmake ..
make -j$(nproc)

# Run tests
./shm_action_test
```

## Test Scenarios

### 1. Basic Action Workflow
Tests the complete action lifecycle:
- Client sends goal to server
- Server processes goal and publishes periodic feedback
- Server publishes final result
- Client receives and validates result

### 2. Action Cancellation
Tests the cancellation mechanism:
- Client sends goal and server starts processing
- Client cancels the goal mid-execution
- Server detects cancellation and sets PREEMPTED status

### 3. Goal Rejection
Tests server-side validation:
- Client sends invalid goal (negative value)
- Server rejects the goal immediately
- Status is set to REJECTED

### 4. Feedback Monitoring
Tests feedback progression:
- Server publishes multiple feedback messages
- Client collects all feedback values
- Validates feedback progression over time

### 5. Multiple Clients
Tests concurrent access:
- Multiple clients send goals to same server
- Server processes goals sequentially
- All clients receive correct results

## Expected Output

Successful test execution should show:
```
[==========] Running 10 tests from 1 test suite.
[----------] Global test environment set-up.
[----------] 10 tests from SHMActionTest
[ RUN      ] SHMActionTest.BasicActionTest
[       OK ] SHMActionTest.BasicActionTest (XXX ms)
[ RUN      ] SHMActionTest.ComplexClassActionTest
[       OK ] SHMActionTest.ComplexClassActionTest (XXX ms)
...
[----------] 10 tests from SHMActionTest (XXX ms total)
[----------] Global test environment tear-down
[==========] 10 tests from 1 test suite ran. (XXX ms total)
[  PASSED  ] 10 tests.
```

## Performance Characteristics

The performance test typically shows:
- Processing time: ~1-5ms per action
- Throughput: 200+ actions per second
- Memory usage: Efficient shared memory allocation

## Notes

- Tests require shared memory support (available in Docker environment)
- Some tests use multi-threading to simulate real-world client-server scenarios
- Action pattern supports long-running operations with progress feedback
- All tests automatically clean up shared memory resources
- Tests validate both successful and error conditions

## Comparison with ROS Actions

This implementation provides similar functionality to ROS actionlib:
- Goal-Result-Feedback pattern
- Cancellation support
- Multiple client support
- State management (ACTIVE, SUCCEEDED, PREEMPTED, REJECTED)

Key differences:
- Uses shared memory instead of ROS topics
- Designed for high-performance applications
- Supports inter-process communication without ROS middleware