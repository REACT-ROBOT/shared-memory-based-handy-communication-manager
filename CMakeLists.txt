cmake_minimum_required(VERSION 3.10)

project(shm VERSION 1.1.0 LANGUAGES CXX)

# ----------------------------------------------------------------------------
# バージョン
#
# 参照側 (react_cv/shm_pub_sub_cv など) が「必要な API を備えた版か」を
# configure 時に判定できるようにするために付与する。
#
#   - add_subdirectory で同一ビルドツリーに取り込まれる場合
#       キャッシュ変数 SHM_VERSION を見る。
#   - インストール済みを find_package する場合
#       各サブプロジェクトが出力する <name>-config-version.cmake を見る。
#       find_package(shm_base 1.0.0 REQUIRED) のように書ける。
#
# バージョンを持たない版 (この仕組みの導入以前) では SHM_VERSION が未定義に
# なるため、参照側はそれを「古すぎる」として区別できる。
#
# 更新の目安:
#   PATCH … 挙動の修正のみで、参照側のコード変更が不要なもの
#   MINOR … 公開 API の追加。参照側が新 API を使うなら要求版を上げる
#   MAJOR … 公開 API の削除・非互換変更
#
# 1.0.0: バージョン付与の開始。RingBuffer::isLayoutChanged() を含む。
# 1.1.0: reserve_aligned() を追加。shm_service / shm_action の共有メモリ配置を
#        境界揃えに変更（ARM の SIGBUS 修正）。配置が変わるため、この版と
#        1.0.0 以前を混在させて同じトピックに接続してはならない。
# ----------------------------------------------------------------------------
set(SHM_VERSION "${PROJECT_VERSION}" CACHE INTERNAL
    "Version of shared-memory-based-handy-communication-manager in this build tree")
message(STATUS "shared-memory-based-handy-communication-manager version: ${SHM_VERSION}")

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
