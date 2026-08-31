cmake_minimum_required(VERSION 3.10)

project(shm VERSION 2.0.0 LANGUAGES CXX)

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
# 2.0.0: shm_service / shm_action を削除。利用者が居らず、レビュー R01 で
#        pthread オブジェクトの無条件再初期化 (F02)、mutex 外での述語アクセス
#        (F03)、robust でない mutex とプロセス死からの復旧不能 (F08) という
#        Critical/High 所見を抱えたままだったため、修正ではなく撤去を選んだ。
#        再導入する場合は R01 の指摘を解消した設計で作り直すこと。
#        提供するのは shm_base / shm_pub_sub / shm_tool のみ。
# ----------------------------------------------------------------------------
set(SHM_VERSION "${PROJECT_VERSION}" CACHE INTERNAL
    "Version of shared-memory-based-handy-communication-manager in this build tree")
message(STATUS "shared-memory-based-handy-communication-manager version: ${SHM_VERSION}")

# Global options
option(DEBUG "switch on debug option" OFF)
option(BUILD_TESTS "Build test programs" OFF)
option(ENABLE_COVERAGE "Enable code coverage reporting" OFF)

# ----------------------------------------------------------------------------
# サニタイザ
#
# 従来は DEBUG=ON が AddressSanitizer 固定だった。ASan は範囲外アクセスには
# 有効だが、データ競合（TSan）やアライメント違反・未定義動作（UBSan）は
# 検出できない。R01-F10 への対応として、種類を選べるようにする。
#
#   -DSANITIZER=address    範囲外アクセス、use-after-free
#   -DSANITIZER=thread     データ競合（本ライブラリの主戦場）
#   -DSANITIZER=undefined  未定義動作。alignment を含めるのが要点
#   -DSANITIZER=none       既定
#
# DEBUG=ON は従来どおり address として扱う（後方互換）。
# サニタイザはリンク時にも同じフラグが必要なので、リンカフラグにも渡す。
# ----------------------------------------------------------------------------
set(SANITIZER "none" CACHE STRING "Sanitizer to enable: none|address|thread|undefined")
set_property(CACHE SANITIZER PROPERTY STRINGS none address thread undefined)

if (DEBUG AND SANITIZER STREQUAL "none")
    set(SANITIZER "address")
endif()

if (SANITIZER STREQUAL "address")
    set(DEBUG_OPTION "-fsanitize=address -fno-omit-frame-pointer -g")
elseif (SANITIZER STREQUAL "thread")
    set(DEBUG_OPTION "-fsanitize=thread -fno-omit-frame-pointer -g")
elseif (SANITIZER STREQUAL "undefined")
    # alignment を明示的に含める。ARM の SIGBUS の原因になる非アライン
    # アクセスは x86 では動いてしまうため、UBSan でしか捕まえられない。
    set(DEBUG_OPTION "-fsanitize=undefined,alignment -fno-sanitize-recover=all -fno-omit-frame-pointer -g")
elseif (NOT SANITIZER STREQUAL "none")
    message(FATAL_ERROR "Unknown SANITIZER '${SANITIZER}'. Use none|address|thread|undefined.")
endif()

if (NOT SANITIZER STREQUAL "none")
    message(STATUS "Sanitizer enabled: ${SANITIZER}")
    set(CMAKE_EXE_LINKER_FLAGS    "${CMAKE_EXE_LINKER_FLAGS} ${DEBUG_OPTION}")
    set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} ${DEBUG_OPTION}")
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

# ----------------------------------------------------------------------------
# テスト実行時に古い共有ライブラリを掴まないようにする
#
# 環境に shm をインストール済みだと LD_LIBRARY_PATH がそちらを指しており、
# ビルドツリーのテストバイナリが「今ビルドした shm_base.so」ではなく
# 「インストール済みの古い shm_base.so」にリンク解決されてしまう。
# 症状は undefined symbol か、もっと悪いと「古い実装で緑になる」。
#
# LD_LIBRARY_PATH は DT_RUNPATH より優先されるため rpath だけでは防げない。
# --disable-new-dtags で DT_RPATH を出す（DT_RPATH は LD_LIBRARY_PATH より
# 優先される）ことで、ビルドツリーのライブラリを確実に掴ませる。
# ----------------------------------------------------------------------------
set(CMAKE_BUILD_WITH_INSTALL_RPATH OFF)
set(CMAKE_SKIP_BUILD_RPATH OFF)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -Wl,--disable-new-dtags")
endif()

# ハングしたテストを PASS でも無言でもなく FAIL にするための上限[秒]。
# ctest の呼び出し方（--timeout の有無）に依存させないため、テストごとの
# プロパティとして設定する。
set(SHM_TEST_TIMEOUT 60 CACHE STRING "Per-test timeout in seconds")

#project(core_cpp CXX)
add_subdirectory(shm_base)
add_subdirectory(shm_pub_sub)
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
