# SHM

## Abstruct

SHM is short for "Shared-memory based Handy-communication Manager".
This provides ROS(Robot Operating System)-like internal communication such as Topic, Service and Action with shared memory.
This is based on the library developed by Dr. Prof. Koichi Ozaki for his laboratory.

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
   $ git clone 
   or
   $ git submodule add
   ```

2. build programs.
   ```
   $ cd <Your_cmake_ws>/build
   $ cmake ..
   $ make
   ```

## How to use

SHM is separated 4 parts.

  - shm_base
    This library include common class such as how to access shared memory.

  - shm_pub_sub
    This is the library that enables publisher/subscriber communication by topic.

  - shm_service
    This is the library that enables service communication.

  - shm_action
    This is the library that enables action communication.

The details are written in each library.