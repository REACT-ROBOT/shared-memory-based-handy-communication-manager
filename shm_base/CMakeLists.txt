cmake_minimum_required(VERSION 3.10)

project(shm_base CXX)

set(package_version 20100204)
option(DEBUG "switch on debug option" OFF)
option(BUILD_TESTS "Build test programs" OFF)
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

##libshm_pub_sub.a

add_library(shm_base SHARED src/shared_memory.cpp src/ring_buffer.cpp)
target_include_directories(shm_base PUBLIC
    $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include> 
    $<INSTALL_INTERFACE:include>
)
target_link_libraries(shm_base PRIVATE rt pthread)
set_target_properties(shm_base PROPERTIES PREFIX "") # 接頭辞'lib'を省略するため
set_target_properties(shm_base PROPERTIES
	PUBLIC_HEADER include/shm_base.hpp
)

##install
install(TARGETS shm_base EXPORT shm_baseExport
	LIBRARY		DESTINATION lib
	INCLUDES	DESTINATION include
	PUBLIC_HEADER	DESTINATION include)
install(EXPORT shm_baseExport
	FILE shm_base-config.cmake
	DESTINATION share/cmake/shm_base
	EXPORT_LINK_INTERFACE_LIBRARIES
)

# shm_base_test
if(BUILD_TESTS)
  include(GoogleTest)
  include_directories(test)
  add_executable(shm_base_test
      test/shm_base_test.cpp
    )
  target_link_libraries(shm_base_test
    PRIVATE
      shm_base
      # Google Testをリンクする
      ${GTEST_LDFLAGS}
    )
  gtest_add_tests(TARGET shm_base_test)
endif()

FIND_PACKAGE(Doxygen)
IF(DOXYGEN_FOUND)
        SET(DOXYGEN_CONF_FILE "Doxyconfig")
        SET(DOXYGEN_CONF_PATH   ${CMAKE_SOURCE_DIR}/${DOXYGEN_CONF_FILE})
        SET(DOXYGEN_SOURCE_DIR ${CMAKE_SOURCE_DIR})
        SET(DOXYGEN_TARGET "doc" )

        ADD_CUSTOM_TARGET(${DOXYGEN_TARGET}
                        ${DOXYGEN_EXECUTABLE} ${DOXYGEN_CONF_PATH}
                        DEPENDS  ${DOXYGEN_CONF_PATH})
ELSE(DOXYGEN_FOUND)
#ない場合は，warningを出す．
        MESSAGE (WARNING "doxygen binary couldn't be found")
ENDIF(DOXYGEN_FOUND)

