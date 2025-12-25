cmake_minimum_required(VERSION 3.10)

project(shm_base_c C)

option(DEBUG "switch on debug option" OFF)

# for check memory leak
if (DEBUG)
    set(DEBUG_OPTION "-fsanitize=address -fno-omit-frame-pointer")
endif()

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fPIC ${DEBUG_OPTION}")
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

## libshm_base_c.so

add_library(shm_base_c SHARED src/shm_base_c.c)
target_include_directories(shm_base_c PUBLIC
    $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)
target_link_libraries(shm_base_c PRIVATE rt pthread)
set_target_properties(shm_base_c PROPERTIES
    PUBLIC_HEADER include/shm_base_c.h
)

## install
install(TARGETS shm_base_c EXPORT shm_base_cExport
    LIBRARY       DESTINATION lib
    INCLUDES      DESTINATION include
    PUBLIC_HEADER DESTINATION include
)
install(EXPORT shm_base_cExport
    FILE shm_base_c-config.cmake
    DESTINATION share/cmake/shm_base_c
    EXPORT_LINK_INTERFACE_LIBRARIES
)
