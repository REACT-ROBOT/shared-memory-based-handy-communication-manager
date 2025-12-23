cmake_minimum_required(VERSION 3.10)

project(shm_service CXX)

set(package_version 20230222)
option(DEBUG "switch on debug option" OFF)
option(BUILD_TESTS "Build test programs" OFF)
option(BUILD_EXAMPLES "Build examples" OFF)
#for check memory leak
if (DEBUG)
set(DEBUG_OPTION "-fsanitize=address -fno-omit-frame-pointer")
endif()
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DBOOST_NO_AUTO_PTR -fPIC ${DEBUG_OPTION}")
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(PkgConfig)
if(BUILD_TESTS)
    pkg_search_module(GTEST REQUIRED gtest_main)
endif()
#FIND_PACKAGE(tomlplusplus REQUIRED)
find_package(Python3 COMPONENTS Interpreter Development)
set(Boost_USE_STATIC_LIBS OFF)
find_package(Boost COMPONENTS python REQUIRED)

##libshm_service.a

add_library(shm_service SHARED src/shm_service.cpp)

# Explicitly set C++17 for this target
target_compile_features(shm_service PUBLIC cxx_std_17)

target_include_directories(shm_service PUBLIC
    $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
#    ${tomlplusplus_INCLUDE_DIRS}
    ${Python3_INCLUDE_DIRS}
)
target_link_libraries(shm_service PUBLIC pthread PRIVATE rt shm_base ${Boost_LIBRARIES} ${Python3_LIBRARIES})
set_target_properties(shm_service PROPERTIES
    PREFIX ""  # 接頭辞'lib'を省略するため
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF
)
set_target_properties(shm_service PROPERTIES
	PUBLIC_HEADER include/shm_service.hpp
)

##sample
if (BUILD_EXAMPLES)
add_subdirectory(samples)
endif()

##install
install(TARGETS shm_service EXPORT shm_serviceExport
	LIBRARY		DESTINATION lib
	INCLUDES	DESTINATION include
	PUBLIC_HEADER	DESTINATION include)
install(EXPORT shm_serviceExport
	FILE shm_service-config.cmake
	DESTINATION share/cmake/shm_service
	EXPORT_LINK_INTERFACE_LIBRARIES
)
find_program(PYTHON "python3")

#if (PYTHON)
#    set(SETUP_PY_IN  "${PROJECT_SOURCE_DIR}/scripts/setup.py.in")
#    set(SETUP_PY     "${PROJECT_BINARY_DIR}/setup.py")
#    set(PACKAGE_NAME "${PROJECT_NAME}")
#    set(LIBRARY_PATH "${PROJECT_BINARY_DIR}/${PROJECT_NAME}.so")
## for colcon
##    string(REPLACE "build" "install" INSTALL_PATH "${PROJECT_BINARY_DIR}")
#    set(INSTALL_PATH "${CMAKE_BINARY_DIR}")
#    set(ENV{PYTHONPATH} "${INSTALL_PATH}/lib/python${Python3_VERSION_MAJOR}.${Python3_VERSION_MINOR}/site-packages/")
#
#    configure_file(${SETUP_PY_IN} ${SETUP_PY})
#
#    install(CODE "set(ENV{PYTHONPATH} \"${INSTALL_PATH}/lib/python${Python3_VERSION_MAJOR}.${Python3_VERSION_MINOR}/site-packages/\")
#                  execute_process(COMMAND pip install ${PROJECT_BINARY_DIR} --prefix=${INSTALL_PATH})")
#endif()

# shm_service_test
if(BUILD_TESTS)
add_subdirectory(test)
endif()

#FIND_PACKAGE(Doxygen)
#IF(DOXYGEN_FOUND)
#        SET(DOXYGEN_CONF_FILE "Doxyconfig")
#        SET(DOXYGEN_CONF_PATH   ${CMAKE_SOURCE_DIR}/${DOXYGEN_CONF_FILE})
#        SET(DOXYGEN_SOURCE_DIR ${CMAKE_SOURCE_DIR})
#        SET(DOXYGEN_TARGET "shm_service_doc" )
#
#        ADD_CUSTOM_TARGET(${DOXYGEN_TARGET}
#                        ${DOXYGEN_EXECUTABLE} ${DOXYGEN_CONF_PATH}
#                        DEPENDS  ${DOXYGEN_CONF_PATH})
#ELSE(DOXYGEN_FOUND)
##ない場合は，warningを出す．
#        MESSAGE (WARNING "doxygen binary couldn't be found")
#ENDIF(DOXYGEN_FOUND)

