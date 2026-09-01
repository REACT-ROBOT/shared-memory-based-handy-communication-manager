cmake_minimum_required(VERSION 3.10)

project(shm_base CXX)

set(package_version 20100204)
option(DEBUG "switch on debug option" OFF)
option(BUILD_TESTS "Build test programs" OFF)
# サニタイザの選択は最上位の CMakeLists.txt が行う（-DSANITIZER=...）。
# add_subdirectory で取り込まれている場合は DEBUG_OPTION が既に設定されて
# いるので上書きしない。単体でビルドされた場合のみ、従来どおり DEBUG=ON を
# AddressSanitizer として解釈する。
if (DEBUG AND NOT DEFINED DEBUG_OPTION)
    set(DEBUG_OPTION "-fsanitize=address -fno-omit-frame-pointer -g")
endif()
if (DEFINED DEBUG_OPTION AND NOT DEBUG_OPTION STREQUAL "")
    set(CMAKE_EXE_LINKER_FLAGS    "${CMAKE_EXE_LINKER_FLAGS} ${DEBUG_OPTION}")
    set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} ${DEBUG_OPTION}")
endif()
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DBOOST_NO_AUTO_PTR -fPIC ${DEBUG_OPTION}")
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(PkgConfig)
if(BUILD_TESTS)
    if(NOT DEFINED SHM_TEST_TIMEOUT)
        set(SHM_TEST_TIMEOUT 60)
    endif()
    pkg_search_module(GTEST REQUIRED gtest_main)
endif()

##libshm_pub_sub.a

add_library(shm_base SHARED src/shared_memory.cpp src/ring_buffer.cpp src/shm_topic.cpp)

# Explicitly set C++17 for this target
target_compile_features(shm_base PUBLIC cxx_std_17)

target_include_directories(shm_base PUBLIC
    $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)
target_link_libraries(shm_base PRIVATE rt pthread)
set_target_properties(shm_base PROPERTIES
    PREFIX ""  # 接頭辞'lib'を省略するため
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF
)
# 型検査は「共有メモリに何を置くか」の制約なので、リンクした側にも効かせる。
# PUBLIC にすることで、このライブラリを使う全パッケージのコンパイルに伝播する。
# （ディレクトリスコープの add_compile_definitions では兄弟パッケージに届かない）
if(SHM_STRICT_TYPE_CHECK)
    target_compile_definitions(shm_base PUBLIC SHM_STRICT_TYPE_CHECK)
endif()

set_target_properties(shm_base PROPERTIES
	PUBLIC_HEADER include/shm_base.hpp
)

##install
install(TARGETS shm_base EXPORT shm_baseExport
	LIBRARY		DESTINATION lib
	INCLUDES	DESTINATION include
	PUBLIC_HEADER	DESTINATION include)
# 参照側が find_package(shm_base <version>) でバージョンを確認できるようにする
include(CMakePackageConfigHelpers)
write_basic_package_version_file(
	"${CMAKE_CURRENT_BINARY_DIR}/shm_base-config-version.cmake"
	VERSION ${SHM_VERSION}
	COMPATIBILITY SameMajorVersion
)
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/shm_base-config-version.cmake"
	DESTINATION share/cmake/shm_base
)
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
  gtest_add_tests(TARGET shm_base_test TEST_LIST shm_base_test_cases)
  # ハングを FAIL にする（ctest の --timeout に依存させない）
  # /dev/shm はプロセス横断の単一の名前空間であり、各 fixture の TearDown は
  # 自分の担当トピックを unlink する。並列実行すると兄弟テストが使用中の
  # セグメントを消してしまうので、共通のリソース鍵で直列化する（R04-F28）。
  # RUN_SERIAL ではなく RESOURCE_LOCK を使うのは、競合しているのが
  # 「CPU」ではなく「/dev/shm という名前空間」だからである。
  set_tests_properties(${shm_base_test_cases} PROPERTIES TIMEOUT ${SHM_TEST_TIMEOUT} RESOURCE_LOCK dev_shm)
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

