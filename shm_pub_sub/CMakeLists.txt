cmake_minimum_required(VERSION 3.10)

project(shm_pub_sub CXX)

set(package_version 20100204)
option(DEBUG "switch on debug option" OFF)
#for check memory leak
if (DEBUG)
set(DEBUG_OPTION "-fsanitize=address -fno-omit-frame-pointer")
endif()
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DBOOST_NO_AUTO_PTR -fPIC ${DEBUG_OPTION}")
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(PkgConfig)
pkg_search_module(GTEST REQUIRED gtest_main)
find_package(Python3 COMPONENTS Interpreter Development)
set(Boost_USE_STATIC_LIBS OFF)
find_package(Boost COMPONENTS python REQUIRED)

##libshm_pub_sub.a

add_library(shm_pub_sub SHARED src/shm_pub_sub.cpp src/shm_pub_sub_python.cpp)
target_include_directories(shm_pub_sub PUBLIC
    $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include> 
    $<INSTALL_INTERFACE:include>
    ${Python3_INCLUDE_DIRS}
)
target_link_libraries(shm_pub_sub PRIVATE rt pthread shm_base ${Boost_LIBRARIES} ${Python3_LIBRARIES})
set_target_properties(shm_pub_sub PROPERTIES PREFIX "") # 接頭辞'lib'を省略するため
set_target_properties(shm_pub_sub PROPERTIES
	PUBLIC_HEADER include/shm_pub_sub.hpp
)

##sample
add_subdirectory(samples)

##install
install(TARGETS shm_pub_sub EXPORT shm_pub_subExport
	LIBRARY		DESTINATION lib
	INCLUDES	DESTINATION include
	PUBLIC_HEADER	DESTINATION include)
install(FILES ${PROJECT_SOURCE_DIR}/include/shm_pub_sub_vector.hpp
	DESTINATION include
)
install(EXPORT shm_pub_subExport
	FILE shm_pub_sub-config.cmake
	DESTINATION share/cmake/shm_pub_sub
	EXPORT_LINK_INTERFACE_LIBRARIES
)
find_program(PYTHON "python3")

if (PYTHON)
    set(SETUP_PY_IN  "${PROJECT_SOURCE_DIR}/scripts/setup.py.in")
    set(SETUP_PY     "${PROJECT_BINARY_DIR}/setup.py")
    set(PACKAGE_NAME "${PROJECT_NAME}")
    set(LIBRARY_PATH "${PROJECT_BINARY_DIR}/${PROJECT_NAME}.so")
# for colcon
#    string(REPLACE "build" "install" INSTALL_PATH "${PROJECT_BINARY_DIR}")
    set(INSTALL_PATH "${CMAKE_BINARY_DIR}")
    set(ENV{PYTHONPATH} "${INSTALL_PATH}/lib/python${Python3_VERSION_MAJOR}.${Python3_VERSION_MINOR}/site-packages/")

    configure_file(${SETUP_PY_IN} ${SETUP_PY})

    install(CODE "set(ENV{PYTHONPATH} \"${INSTALL_PATH}/lib/python${Python3_VERSION_MAJOR}.${Python3_VERSION_MINOR}/site-packages/\")
                  execute_process(COMMAND pip install ${PROJECT_BINARY_DIR} --prefix=${INSTALL_PATH})")
endif()

# shm_frame_cppy_test
include(GoogleTest)
include_directories(test)
add_executable(shm_pub_sub_test
    test/shm_pub_sub_test.cpp
  )
target_link_libraries(shm_pub_sub_test
    PRIVATE
    shm_base
    shm_pub_sub 
    # Google Testをリンクする
    ${GTEST_LDFLAGS}
  )
gtest_add_tests(TARGET shm_pub_sub_test)

