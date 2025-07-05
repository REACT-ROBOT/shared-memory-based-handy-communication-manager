cmake_minimum_required(VERSION 3.8)
project(shm_service_test)

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
add_executable(shm_service_test shm_service_test.cpp)

# Link libraries
target_link_libraries(shm_service_test ${GTEST_LIBRARIES} pthread rt shm_base)

# Compiler flags
target_compile_options(shm_service_test PRIVATE ${GTEST_CFLAGS_OTHER})

# Enable testing
enable_testing()
add_test(NAME shm_service_test COMMAND shm_service_test)