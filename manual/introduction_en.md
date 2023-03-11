# Introduction

SHM (Shared-memory based Handy-communication Manager) is a library for easy inter-process communication using shared memory.
This library is based on a library created by Professor Koichi Ozaki in C for robot development in his laboratory.
The original library had a simple API that made it easy for even novice programmers to write inter-process communication.
In addition to the know-how cultivated in the original library, this library has been devised so that object-oriented programs can be realized using C++ classes.
In addition, to improve the reliability and functionality of communication, ring buffers and conditional variables are incorporated with reference to fuRom and ROS (Robot Operating System) communication.

[1] 入江清. "ROS との相互運用性に配慮した共有メモリによる低遅延プロセス間通信フレームワーク." 第 35 回日本ロボット学会学術講演会予稿集, RSJ2017AC2B2-01 (2017).
    <https://furo.org/irie/papers/rsj2017_irie.pdf>

[2] Open Robotics, "ROS.org", <http://wiki.ros.org/>

# API Concepts

## irlab::shm Namespace

All SHM classes and functions are placed within the irlab::shm namespace. This is necessary to avoid conflicts with other similar libraries such as ROS.
```
#include "shm_pub_sub.hpp"
...
irlab::shm::Publisher<int> pub = irlab::shm::Publisher<int>("test");
...
```

## Automatic shared-memory management

## Support for user-defined classes

## Implement various communication models

### Publisher/subscriber model

### Server/Client model (simple)

### Server/Client model (high functionality version)