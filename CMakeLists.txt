cmake_minimum_required(VERSION 3.10)

project(shm CXX)

if(NOT DEFINED BUILD_ON_RASPBERRY_PI_DETECTED)
  if(NOT DEFINED BUILD_ON_RASPBERRY_PI)
    set(BUILD_ON_RASPBERRY_PI "AUTO" CACHE STRING "Raspberry Pi build policy: AUTO, ON, or OFF")
  endif()
  set_property(CACHE BUILD_ON_RASPBERRY_PI PROPERTY STRINGS AUTO ON OFF)
  string(TOUPPER "${BUILD_ON_RASPBERRY_PI}" _build_on_raspberry_pi)
  if(_build_on_raspberry_pi STREQUAL "ON")
    set(BUILD_ON_RASPBERRY_PI_DETECTED TRUE CACHE INTERNAL "Resolved Raspberry Pi build policy" FORCE)
  elseif(_build_on_raspberry_pi STREQUAL "OFF")
    set(BUILD_ON_RASPBERRY_PI_DETECTED FALSE CACHE INTERNAL "Resolved Raspberry Pi build policy" FORCE)
  else()
    set(_build_on_raspberry_pi_detected FALSE)
    if(EXISTS "/proc/device-tree/model")
      file(READ "/proc/device-tree/model" _build_host_model ERROR_QUIET)
      if(_build_host_model MATCHES "Raspberry Pi")
        set(_build_on_raspberry_pi_detected TRUE)
      endif()
    endif()
    if(NOT _build_on_raspberry_pi_detected AND EXISTS "/proc/cpuinfo")
      file(READ "/proc/cpuinfo" _build_cpuinfo ERROR_QUIET)
      if(_build_cpuinfo MATCHES "Raspberry Pi|BCM27|BCM28")
        set(_build_on_raspberry_pi_detected TRUE)
      endif()
    endif()
    set(BUILD_ON_RASPBERRY_PI_DETECTED ${_build_on_raspberry_pi_detected} CACHE INTERNAL "Resolved Raspberry Pi build policy" FORCE)
  endif()
endif()

if(NOT DEFINED BUILD_PROJECT_TESTS_RESOLVED)
  if(NOT DEFINED BUILD_PROJECT_TESTS)
    set(BUILD_PROJECT_TESTS "AUTO" CACHE STRING "Project test build policy: AUTO, ON, or OFF")
  endif()
  set_property(CACHE BUILD_PROJECT_TESTS PROPERTY STRINGS AUTO ON OFF)
  string(TOUPPER "${BUILD_PROJECT_TESTS}" _build_project_tests)
  if(_build_project_tests STREQUAL "ON")
    set(BUILD_PROJECT_TESTS_RESOLVED ON CACHE INTERNAL "Resolved project test build policy" FORCE)
  elseif(_build_project_tests STREQUAL "OFF" OR BUILD_ON_RASPBERRY_PI_DETECTED)
    set(BUILD_PROJECT_TESTS_RESOLVED OFF CACHE INTERNAL "Resolved project test build policy" FORCE)
  else()
    set(BUILD_PROJECT_TESTS_RESOLVED DEFAULT CACHE INTERNAL "Resolved project test build policy" FORCE)
  endif()
endif()

if(NOT "${BUILD_PROJECT_TESTS_RESOLVED}" STREQUAL "DEFAULT")
  set(BUILD_TESTING ${BUILD_PROJECT_TESTS_RESOLVED} CACHE BOOL "Build tests" FORCE)
  set(BUILD_TESTS ${BUILD_PROJECT_TESTS_RESOLVED} CACHE BOOL "Build tests" FORCE)
endif()

# Global options
option(DEBUG "switch on debug option" OFF)
option(BUILD_TESTS "Build test programs" OFF)
option(ENABLE_COVERAGE "Enable code coverage reporting" OFF)

#for check memory leak
if (DEBUG)
    set(DEBUG_OPTION "-fsanitize=address -fno-omit-frame-pointer")
endif()

# Coverage options
if(ENABLE_COVERAGE)
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        set(COVERAGE_FLAGS "--coverage -g -O0")
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${COVERAGE_FLAGS}")
        set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} ${COVERAGE_FLAGS}")
        set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} --coverage")
        set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} --coverage")
        message(STATUS "Code coverage enabled")
    else()
        message(WARNING "Code coverage requires GCC or Clang")
        set(ENABLE_COVERAGE OFF)
    endif()
endif()

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DBOOST_NO_AUTO_PTR -fPIC ${DEBUG_OPTION}")
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Enable testing
if(BUILD_TESTS)
enable_testing()
endif()

#project(core_cpp CXX)
add_subdirectory(shm_base)
add_subdirectory(shm_pub_sub)
add_subdirectory(shm_service)
add_subdirectory(shm_action)
add_subdirectory(tools)

FIND_PACKAGE(Doxygen)
IF(DOXYGEN_FOUND)
        SET(DOXYGEN_CONF_FILE "Doxyconfig")
        SET(DOXYGEN_CONF_PATH   ${CMAKE_CURRENT_SOURCE_DIR}/${DOXYGEN_CONF_FILE})
        SET(DOXYGEN_CONF_FILE_JP "Doxyconfig_jp")
        SET(DOXYGEN_CONF_PATH_JP   ${CMAKE_CURRENT_SOURCE_DIR}/${DOXYGEN_CONF_FILE_JP})
        SET(DOXYGEN_SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR})
        SET(DOXYGEN_TARGET_EN "shm_doc_en")
        SET(DOXYGEN_TARGET_JP "shm_doc_jp")

        ADD_CUSTOM_TARGET(${DOXYGEN_TARGET_EN}
                        ${DOXYGEN_EXECUTABLE} ${DOXYGEN_CONF_PATH}
                        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
                        DEPENDS  ${DOXYGEN_CONF_PATH})

        ADD_CUSTOM_TARGET(${DOXYGEN_TARGET_JP}
                        ${DOXYGEN_EXECUTABLE} ${DOXYGEN_CONF_PATH_JP}
                        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
                        DEPENDS  ${DOXYGEN_CONF_PATH_JP})

        ADD_CUSTOM_TARGET(shm_doc
                        DEPENDS ${DOXYGEN_TARGET_EN} ${DOXYGEN_TARGET_JP})

ELSE(DOXYGEN_FOUND)
#ない場合は，warningを出す．
        MESSAGE (WARNING "doxygen binary couldn't be found")
ENDIF(DOXYGEN_FOUND)
