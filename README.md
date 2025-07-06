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
   $ git clone https://github.com/REACT-ROBOT/shared-memory-based-handy-communication-manager.git
   or
   $ git submodule add https://github.com/REACT-ROBOT/shared-memory-based-handy-communication-manager.git
   $ gedit CMakeLists.txt
   add "add_subdirectory(shared-memory-based-handy-communication-manager)"
   ```

2. build programs.
   ```
   $ cd <Your_cmake_ws>/build
   $ cmake ..
   $ make
   ```

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
- Detailed tutorials for Publisher/Subscriber, Service, and Action patterns
- Python integration examples
- API reference and troubleshooting guides

# License

SHM is under [Apache 2.0 License](https://www.apache.org/licenses/LICENSE-2.0).
