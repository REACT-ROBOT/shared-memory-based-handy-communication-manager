cmake_minimum_required(VERSION 3.10)

project(shm_pub_sub_c C)

set(package_version 20100204)
option(DEBUG "switch on debug option" OFF)
option(BUILD_EXAMPLES "Build examples" OFF)

# for check memory leak
if (DEBUG)
    set(DEBUG_OPTION "-fsanitize=address -fno-omit-frame-pointer")
endif()

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fPIC ${DEBUG_OPTION}")
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

## libshm_pub_sub_c.so

add_library(shm_pub_sub_c SHARED src/shm_pub_sub_c.c)
target_include_directories(shm_pub_sub_c PUBLIC
    $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)
target_link_libraries(shm_pub_sub_c PUBLIC shm_base_c)
set_target_properties(shm_pub_sub_c PROPERTIES
    PUBLIC_HEADER include/shm_pub_sub_c.h
)

## samples
if(BUILD_EXAMPLES)
    add_executable(sample_c_publisher samples/sample_c_publisher.c)
    target_link_libraries(sample_c_publisher PRIVATE shm_pub_sub_c)

    add_executable(sample_c_subscriber samples/sample_c_subscriber.c)
    target_link_libraries(sample_c_subscriber PRIVATE shm_pub_sub_c)
endif()

## install
install(TARGETS shm_pub_sub_c EXPORT shm_pub_sub_cExport
    LIBRARY       DESTINATION lib
    INCLUDES      DESTINATION include
    PUBLIC_HEADER DESTINATION include
)
install(EXPORT shm_pub_sub_cExport
    FILE shm_pub_sub_c-config.cmake
    DESTINATION share/cmake/shm_pub_sub_c
    EXPORT_LINK_INTERFACE_LIBRARIES
)
