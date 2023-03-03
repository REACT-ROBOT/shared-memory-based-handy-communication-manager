# SHM Specification {#mainpage}
Japanese pages are [HERE](docs_jp/index.html)

\tableofcontents

# Purpose of SHM
The purpose of SHM (Shared-memory based Handy-communication Manager) is to provide as secure and fast communication as possible between different processes. It should also be designed with consideration to make it easy for students to use. Please refer to README.md for installation instructions.

# Abstruct of SHM
## Contexts
The Instrumentation and Robotics Laboratory at Utsunomiya University uses shared memory, which can be used to exchange data between programs, in addition to local memory generally used by programs.
Shared memory differs from local memory in that it does not require the developer to release allocated memory (if memory is inadvertently released, data cannot be passed to other programs), it has a slightly higher barrier for novice programmers because of the use of pointers, and it requires the designer to create processes for each memory each time a new library is created. The designer needs to create a process for each memory each time a new library is created.
This framework hides data exchange using shared memory and provides easy-to-understand inter-process communication for novice programmers.

## System Functions

### Hiding memory management processes
The ability to easily communicate between processes is realized by hiding shared memory area allocation and buffer access in classes. However, by default, only standard layout type classes are supported. Other classes can be supported by defining specialized in each case. See the sample for details.

### Pointerless Coding
Basically, the system only passes variables allocated in local memory to the Publisher or receives topics from the Subscriber, eliminating the need to code with pointers to shared memory in mind as in the past.

## User Characteristics
### Developer
Developers are those who create new programs using libraries inside and outside the laboratory, such as this library. It is mainly intended for first-time programming students such as fourth-year undergraduates.

### Designer
Designers are those who create new libraries using this library and pass on their current know-how to junior members. This category is mainly intended for second-year master students.

## Definitions and Terms
### Local Memory
Local memory is a virtual storage area that can be accessed within a process. It is a storage area used during normal programming, and if it is not properly released after use, it may cause problems in the future (a program that has been working properly for a while may suddenly stop working).

### Shared memory
Shared memory is a storage area that can be used commonly among processes. Shared memory is a memory area allocated by special means and can be implemented in various ways. This time, POSIX file-mapped memory is used. This is a method in which data to be stored in shared memory is treated as a file, and the memory area allocated directly under /dev/shm can be confirmed in Linux.

## Standard layout type
If a class or structure does not contain specific C++ language features such as virtual functions not found in C, and all members have the same access control, it is a standard layout type. memcpy is possible and the layout is clearly defined for use in C programs. The standard layout type is a user-defined layout type. Standard layout types can have special user-defined member functions. In addition, standard layout types have the following characteristics
- No virtual functions or virtual base classes
- All non-static data members have the same access control
- All non-static members of the class type are standard layout
- All base classes have standard layout
- There is no base class of the same type as the first non-static data member
- One of the following conditions is met
  - The most derived class has no non-static data members and only one base class with non-static data members
  - There is no base class that contains a non-static data member

# References
## man shm_overview
The following is a URL that provides an overview of Posix shared memory.
<https://linuxjm.osdn.jp/html/LDP_man-pages/man7/shm_overview.7.html>
