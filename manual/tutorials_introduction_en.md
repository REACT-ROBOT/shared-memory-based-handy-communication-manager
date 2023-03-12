# Introduction to SHM
[English | [日本語](docs_jp/md_manual_tutorials_introduction_jp.html)]

# Development environment construction

This library is intended for POSIX-compliant operating systems (mainly Linux).
The following shows the procedure for building a development environment on Ubuntu.
For example, on CentOS, it is necessary to replace apt with yum and make other changes as necessary.

1. install C++ compiler and build tools
   ````
   sudo apt update && sudo apt install build-essential
   ````

2. install CMake and Git
   ```
   sudo apt install cmake git
   ```

3. install Python and Boost.Python (optional)
   ```
   sudo apt install python3-dev libboost-python-dev
   ```

4. Create a workspace.
   ```
   cd {where you want to create the workspace}
   mkdir -p cmake_ws/src
   ```

5. clone SHM
   ```
   cd cmake_ws/src/
   git clone https://github.com/ir-utsunomiya/shared-memory-based-handy-communication-manager.git
   ```

6. create CMakeLists.txt directly under workspace.
   ```
   cd {where you created the workspace}/cmake_ws/
   gedit CMakeLists.txt
   ```
   Copy the following text into the editor you opened and save it.
   ```
   cmake_minimum_required(VERSION 3.16)
   cmake_policy(SET CMP0060 NEW)

   project(MobileRobot_cmake)
   set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} ${CMAKE_BINARY_DIR}/cmake)
   set(CMAKE_INSTALL_PREFIX ${CMAKE_BINARY_DIR})
   set(CMAKE_PREFIX_PATH ${CMAKE_BINARY_DIR})
   SET(CMAKE_INSTALL_RPATH ${CMAKE_BINARY_DIR}/lib)

   add_subdirectory(src)
   ```

7. Create CMakeLists.txt in src directory.
   ```
   cd {where you created your workspace}/cmake_ws/src/
   gedit CMakeLists.txt
   ```
   Copy the following text into the editor you opened and save it.
   ```
   cmake_minimum_required(VERSION 3.16)

   add_subdirectory(shared-memory-based-handy-communication-manager)
   ```

# Build

1. create build directory
   ```
   cd {where you created workspace}/cmake_ws
   mkdir build
   ```

2. run cmake
   ```
   cd build
   cmake ..
   ```

3. run make.
   ```
   make
   ```