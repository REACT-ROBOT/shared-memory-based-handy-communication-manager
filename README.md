# SHM

## Abstruct

SHM is short for "Shared-memory based Handy-communication Manager".
This provides ROS(Robot Operating System)-like interprocess communication such as Topic, Service and Action with shared memory.
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
   $ git clone https://github.com/ir-utsunomiya/shared-memory-based-handy-communication-manager.git
   or
   $ git submodule add https://github.com/ir-utsunomiya/shared-memory-based-handy-communication-manager.git
   $ gedit CMakeLists.txt
   add "add_subdirectory(shared-memory-based-handy-communication-manager)"
   ```

2. build programs.
   ```
   $ cd <Your_cmake_ws>/build
   $ cmake ..
   $ make
   ```

## Manuals / Tutorials

TODO: We will add manuals and tutorials to [Github Pages](https://ir-utsunomiya.github.io/shared-memory-based-handy-communication-manager/index.html)

# License

SHM is under [Apache 2.0 License](https://www.apache.org/licenses/LICENSE-2.0).
