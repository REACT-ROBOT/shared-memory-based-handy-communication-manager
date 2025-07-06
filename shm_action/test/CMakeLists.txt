cmake_minimum_required(VERSION 3.8)
project(shm_action_test)

# Find required packages
find_package(PkgConfig REQUIRED)
pkg_search_module(GTEST REQUIRED gtest_main)

# Set C++ standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Include directories
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/../include)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/../../shm_base/include)

# Add executable
add_executable(shm_action_test shm_action_test.cpp)

# Link libraries
target_link_libraries(shm_action_test ${GTEST_LIBRARIES} pthread rt shm_base)

# Compiler flags
target_compile_options(shm_action_test PRIVATE ${GTEST_CFLAGS_OTHER})

# Apply debug flags if DEBUG is enabled
if (DEBUG)
    target_compile_options(shm_action_test PRIVATE -fsanitize=address -fno-omit-frame-pointer)
    target_link_options(shm_action_test PRIVATE -fsanitize=address)
endif()

# Enable testing
enable_testing()
add_test(NAME shm_action_test COMMAND shm_action_test)