cmake_minimum_required(VERSION 3.10)

project(shm_pub_sub CXX)

set(package_version 20100204)
option(DEBUG "switch on debug option" OFF)
option(BUILD_TESTS "Build test programs" OFF)
option(BUILD_EXAMPLES "Build examples" OFF)
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
find_package(Python3 REQUIRED COMPONENTS Interpreter Development)
set(Boost_USE_STATIC_LIBS OFF)
find_package(Boost COMPONENTS python${Python3_VERSION_MAJOR}${Python3_VERSION_MINOR} REQUIRED)

##libshm_pub_sub.a

add_library(shm_pub_sub SHARED src/shm_pub_sub.cpp src/shm_pub_sub_python.cpp)

# Explicitly set C++17 for this target
target_compile_features(shm_pub_sub PUBLIC cxx_std_17)

target_include_directories(shm_pub_sub PUBLIC
    $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
    ${Python3_INCLUDE_DIRS}
)
target_link_libraries(shm_pub_sub PUBLIC shm_base PRIVATE rt pthread ${Boost_LIBRARIES} ${Python3_LIBRARIES})
set_target_properties(shm_pub_sub PROPERTIES
    PREFIX ""  # 接頭辞'lib'を省略するため
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF
)
# 型検査は「共有メモリに何を置くか」の制約なので、リンクした側にも効かせる。
# PUBLIC にすることで、このライブラリを使う全パッケージのコンパイルに伝播する。
# （ディレクトリスコープの add_compile_definitions では兄弟パッケージに届かない）
if(SHM_STRICT_TYPE_CHECK)
    target_compile_definitions(shm_pub_sub PUBLIC SHM_STRICT_TYPE_CHECK)
endif()
if(SHM_REQUIRE_LAYOUT)
    target_compile_definitions(shm_pub_sub PUBLIC SHM_REQUIRE_LAYOUT)
endif()
if(SHM_ENABLE_TEST_HOOKS)
    target_compile_definitions(shm_pub_sub PUBLIC SHM_ENABLE_TEST_HOOKS)
endif()

set_target_properties(shm_pub_sub PROPERTIES
	PUBLIC_HEADER "$<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include/shm_pub_sub.hpp>"
)

##sample
if(BUILD_EXAMPLES)
add_subdirectory(samples)
endif()

##install
install(TARGETS shm_pub_sub EXPORT shm_pub_subExport
	LIBRARY		DESTINATION lib
	INCLUDES	DESTINATION include
	PUBLIC_HEADER	DESTINATION include)
install(FILES ${PROJECT_SOURCE_DIR}/include/shm_pub_sub_vector.hpp
	DESTINATION include
)

# ----------------------------------------------------------------------------
# 適合性スイート（test/shm_pub_sub_conformance.hpp）を外部へ公開する
#
# Publisher<T> / Subscriber<T> の特殊化は 5 つあり、うち 3 つは別リポジトリに
# ある（react_cv / sensor_daemons）。型に依存しない約 150 行が 5 箇所に
# コピーされているため、片方だけ直す漏れが実際に起きた（R04-F12）。
#
# 契約を 1 箇所に書いたヘッダをここで公開しておけば、外部の特殊化は
#     target_link_libraries(<自分のテスト> PRIVATE shm_pub_sub_conformance)
# と書くだけで同じ検査を受けられる。ヘッダオンリーなのでリンクは発生しない。
# ----------------------------------------------------------------------------
add_library(shm_pub_sub_conformance INTERFACE)
target_include_directories(shm_pub_sub_conformance INTERFACE
	$<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/test>
	$<INSTALL_INTERFACE:include>
)
target_link_libraries(shm_pub_sub_conformance INTERFACE shm_pub_sub)
install(FILES ${PROJECT_SOURCE_DIR}/test/shm_pub_sub_conformance.hpp
	DESTINATION include
)
install(TARGETS shm_pub_sub_conformance EXPORT shm_pub_subExport)
# 参照側が find_package(shm_pub_sub <version>) でバージョンを確認できるようにする
include(CMakePackageConfigHelpers)
write_basic_package_version_file(
	"${CMAKE_CURRENT_BINARY_DIR}/shm_pub_sub-config-version.cmake"
	VERSION ${SHM_VERSION}
	COMPATIBILITY SameMajorVersion
)
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/shm_pub_sub-config-version.cmake"
	DESTINATION share/cmake/shm_pub_sub
)
install(EXPORT shm_pub_subExport
	FILE shm_pub_sub-config.cmake
	DESTINATION share/cmake/shm_pub_sub
	EXPORT_LINK_INTERFACE_LIBRARIES
)
#find_program(PYTHON "python3")

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
# Python module installation - CMake only approach
if(Python3_FOUND)
    # Set installation directory (prefer user site-packages for development)
    set(PYTHON_INSTALL_DIR "${CMAKE_BINARY_DIR}/python" CACHE STRING "Python module installation directory")
    
    message(STATUS "Python module will be installed to: ${PYTHON_INSTALL_DIR}")
    
    # Create __init__.py file for Python package
    set(INIT_PY_CONTENT "# shm_pub_sub Python module
\"\"\"
Shared Memory Publisher/Subscriber Python bindings.

This module provides Python bindings for the shared memory communication library.
\"\"\"

# Import the compiled module
from .shm_pub_sub import *

__version__ = \"${package_version}\"
__author__ = \"Masaaki HIJIKATA\"
")
    
    # Write __init__.py file
    file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/__init__.py" "${INIT_PY_CONTENT}")
    
    # Install the built Python extension without re-installing target headers.
    install(FILES "$<TARGET_FILE:shm_pub_sub>"
        DESTINATION "${PYTHON_INSTALL_DIR}/shm_pub_sub"
        COMPONENT python_module
    )
    
    # Install __init__.py
    install(FILES "${CMAKE_CURRENT_BINARY_DIR}/__init__.py"
        DESTINATION "${PYTHON_INSTALL_DIR}/shm_pub_sub"
        COMPONENT python_module
    )
    
    # Create custom target for Python module installation
    add_custom_target(install_python_module
        COMMAND ${CMAKE_COMMAND} --install ${CMAKE_BINARY_DIR} --component python_module
        DEPENDS shm_pub_sub
        COMMENT "Installing Python module to ${PYTHON_INSTALL_DIR}"
    )
    
    # Add option to install to virtual environment if active
    if(DEFINED ENV{VIRTUAL_ENV})
        set(VENV_SITELIB "$ENV{VIRTUAL_ENV}/lib/python${Python3_VERSION_MAJOR}.${Python3_VERSION_MINOR}/site-packages")
        message(STATUS "Virtual environment detected: $ENV{VIRTUAL_ENV}")
        message(STATUS "Virtual environment site-packages: ${VENV_SITELIB}")
        
        # Add alternative target for virtual environment
        add_custom_target(install_python_module_venv
            COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:shm_pub_sub> "${VENV_SITELIB}/shm_pub_sub/"
            COMMAND ${CMAKE_COMMAND} -E copy "${CMAKE_CURRENT_BINARY_DIR}/__init__.py" "${VENV_SITELIB}/shm_pub_sub/"
            COMMAND ${CMAKE_COMMAND} -E make_directory "${VENV_SITELIB}/shm_pub_sub"
            DEPENDS shm_pub_sub
            COMMENT "Installing Python module to virtual environment"
        )
    endif()
    
    # Add test target to verify Python module
    add_custom_target(test_python_module
        COMMAND ${Python3_EXECUTABLE} -c "import shm_pub_sub; print('Python module imported successfully')"
        DEPENDS shm_pub_sub
        COMMENT "Testing Python module import"
    )
endif()

if(BUILD_TESTS)
    # shm_frame_cppy_test
    include(GoogleTest)
    include_directories(test)
    add_executable(shm_pub_sub_test
        test/shm_pub_sub_test.cpp
      )
    target_link_libraries(shm_pub_sub_test
        PRIVATE
        shm_pub_sub 
        # Google Testをリンクする
        ${GTEST_LDFLAGS}
      )
    gtest_add_tests(TARGET shm_pub_sub_test TEST_LIST shm_pub_sub_test_cases)
  # ハングを FAIL にする（ctest の --timeout に依存させない）
  # /dev/shm はプロセス横断の単一の名前空間であり、各 fixture の TearDown は
  # 自分の担当トピックを unlink する。並列実行すると兄弟テストが使用中の
  # セグメントを消してしまうので、共通のリソース鍵で直列化する（R04-F28）。
  # RUN_SERIAL ではなく RESOURCE_LOCK を使うのは、競合しているのが
  # 「CPU」ではなく「/dev/shm という名前空間」だからである。
  set_tests_properties(${shm_pub_sub_test_cases} PROPERTIES TIMEOUT ${SHM_TEST_TIMEOUT} RESOURCE_LOCK dev_shm)

    # データ破損・レースコンディション回帰テスト
    add_executable(shm_pub_sub_race_test
        test/shm_pub_sub_race_test.cpp
      )
    target_link_libraries(shm_pub_sub_race_test
        PRIVATE
        shm_pub_sub
        ${GTEST_LDFLAGS}
      )
    gtest_add_tests(TARGET shm_pub_sub_race_test TEST_LIST shm_pub_sub_race_test_cases)
  # ハングを FAIL にする（ctest の --timeout に依存させない）
  set_tests_properties(${shm_pub_sub_race_test_cases} PROPERTIES TIMEOUT ${SHM_TEST_TIMEOUT} RESOURCE_LOCK dev_shm)

    # タイムスタンプ整合性テスト
    add_executable(shm_pub_sub_timestamp_test
        test/shm_pub_sub_timestamp_test.cpp
      )
    target_link_libraries(shm_pub_sub_timestamp_test
        PRIVATE
        shm_pub_sub
        ${GTEST_LDFLAGS}
      )
    gtest_add_tests(TARGET shm_pub_sub_timestamp_test TEST_LIST shm_pub_sub_timestamp_test_cases)
  # ハングを FAIL にする（ctest の --timeout に依存させない）
  set_tests_properties(${shm_pub_sub_timestamp_test_cases} PROPERTIES TIMEOUT ${SHM_TEST_TIMEOUT} RESOURCE_LOCK dev_shm)

    # 後発 Publisher による共有メモリ再初期化の回帰テスト
    add_executable(shm_pub_sub_late_publisher_test
        test/shm_pub_sub_late_publisher_test.cpp
      )
    target_link_libraries(shm_pub_sub_late_publisher_test
        PRIVATE
        shm_pub_sub
        ${GTEST_LDFLAGS}
      )
    gtest_add_tests(TARGET shm_pub_sub_late_publisher_test TEST_LIST shm_pub_sub_late_publisher_test_cases)
  # ハングを FAIL にする（ctest の --timeout に依存させない）
  set_tests_properties(${shm_pub_sub_late_publisher_test_cases} PROPERTIES TIMEOUT ${SHM_TEST_TIMEOUT} RESOURCE_LOCK dev_shm)

    # 入力検証・壊れた共有メモリ・マッピング漏れの回帰テスト (R01-F06 / F09)
    add_executable(shm_pub_sub_validation_test
        test/shm_pub_sub_validation_test.cpp
      )
    target_link_libraries(shm_pub_sub_validation_test
        PRIVATE
        shm_pub_sub
        ${GTEST_LDFLAGS}
      )
    gtest_add_tests(TARGET shm_pub_sub_validation_test TEST_LIST shm_pub_sub_validation_test_cases)
  # ハングを FAIL にする（ctest の --timeout に依存させない）
  set_tests_properties(${shm_pub_sub_validation_test_cases} PROPERTIES TIMEOUT ${SHM_TEST_TIMEOUT} RESOURCE_LOCK dev_shm)

    # 共有メモリ形式 v2 の回帰テスト (R01-F04 / F05 / F06 / F07-a / 複数 Publisher)
    add_executable(shm_pub_sub_format_v2_test
        test/shm_pub_sub_format_v2_test.cpp
      )
    target_link_libraries(shm_pub_sub_format_v2_test
        PRIVATE
        shm_pub_sub
        ${GTEST_LDFLAGS}
      )
    gtest_add_tests(TARGET shm_pub_sub_format_v2_test TEST_LIST shm_pub_sub_format_v2_test_cases)
  # ハングを FAIL にする（ctest の --timeout に依存させない）
  set_tests_properties(${shm_pub_sub_format_v2_test_cases} PROPERTIES TIMEOUT ${SHM_TEST_TIMEOUT} RESOURCE_LOCK dev_shm)

    # 再レビュー R02 の指摘に対する回帰テスト
    add_executable(shm_pub_sub_contract_test
        test/shm_pub_sub_contract_test.cpp
      )
    target_link_libraries(shm_pub_sub_contract_test
        PRIVATE
        shm_pub_sub
        ${GTEST_LDFLAGS}
      )
    gtest_add_tests(TARGET shm_pub_sub_contract_test TEST_LIST shm_pub_sub_contract_test_cases)
  # ハングを FAIL にする（ctest の --timeout に依存させない）
  set_tests_properties(${shm_pub_sub_contract_test_cases} PROPERTIES TIMEOUT ${SHM_TEST_TIMEOUT} RESOURCE_LOCK dev_shm)

    # 3 回目のレビュー R03 の指摘に対する回帰テスト
    add_executable(shm_pub_sub_r03_test
        test/shm_pub_sub_r03_test.cpp
      )
    target_link_libraries(shm_pub_sub_r03_test
        PRIVATE
        shm_pub_sub
        ${GTEST_LDFLAGS}
      )
    gtest_add_tests(TARGET shm_pub_sub_r03_test TEST_LIST shm_pub_sub_r03_test_cases)
  # ハングを FAIL にする（ctest の --timeout に依存させない）
  set_tests_properties(${shm_pub_sub_r03_test_cases} PROPERTIES TIMEOUT ${SHM_TEST_TIMEOUT} RESOURCE_LOCK dev_shm)

    # 4 回目のレビュー R04 の指摘に対する回帰テスト
    add_executable(shm_pub_sub_r04_test
        test/shm_pub_sub_r04_test.cpp
      )
    target_link_libraries(shm_pub_sub_r04_test
        PRIVATE
        shm_pub_sub
        ${GTEST_LDFLAGS}
      )
    gtest_add_tests(TARGET shm_pub_sub_r04_test TEST_LIST shm_pub_sub_r04_test_cases)
  # ハングを FAIL にする（ctest の --timeout に依存させない）
  set_tests_properties(${shm_pub_sub_r04_test_cases} PROPERTIES TIMEOUT ${SHM_TEST_TIMEOUT} RESOURCE_LOCK dev_shm)

    # 5 つの特殊化が同じ振る舞いをすることを確かめる適合性スイート。
    #
    # 型に依存しない約 150 行が 5 箇所にコピーされており、R01〜R05 では
    # それを手で 5 回適用する運用だった。実際 R04-F12 は本体 2 つにしか
    # 入らず、世代切替が最も頻繁に起きる外部 3 つに届いていなかった。
    #
    # 契約は test/shm_pub_sub_conformance.hpp に 1 箇所だけ書いてある。
    # 外部リポジトリ（react_cv / sensor_daemons）は同じヘッダを取り込んで
    # Traits を書くだけで同じ検査を受けられる。
    add_executable(shm_pub_sub_conformance_test
        test/shm_pub_sub_conformance_test.cpp
      )
    target_link_libraries(shm_pub_sub_conformance_test
        PRIVATE
        shm_pub_sub
        ${GTEST_LDFLAGS}
      )
    # NOTE: ここは gtest_add_tests ではなく gtest_discover_tests を使う。
    #       前者はソースを正規表現で走査して TEST_F を拾う実装なので、
    #       型パラメータ化テスト（TYPED_TEST_P + INSTANTIATE_TYPED_TEST_SUITE_P）は
    #       1 件も見つけられず、黙って「テスト 0 件」になる。
    #       後者はビルド済みバイナリに --gtest_list_tests を掛けるので正しく拾える。
    #
    # NOTE: DISCOVERY_MODE PRE_TEST が必要である。既定の POST_BUILD は
    #       **ビルド中に**バイナリを実行して一覧を得るが、-DSANITIZER=thread の
    #       ビルドではそれが
    #         FATAL: ThreadSanitizer: unexpected memory mapping
    #       で失敗し、テストの実行以前にビルドごと落ちる（実際に落ちた）。
    #       TSan は ASLR のエントロピーが大きい環境で起動できないため
    #       `setarch $(uname -m) -R` を噛ませる必要があるが、ビルド中の実行には
    #       それを挟めない。PRE_TEST なら一覧の取得は ctest の中で起きるので、
    #       ctest 自体を setarch 配下で回せば解決する（ci.yml と同じ運用）。
    gtest_discover_tests(shm_pub_sub_conformance_test
        DISCOVERY_MODE PRE_TEST
        PROPERTIES TIMEOUT ${SHM_TEST_TIMEOUT} RESOURCE_LOCK dev_shm
      )

    # ペイロード書式の宣言（R04 / 制約 A）の回帰テスト
    add_executable(shm_pub_sub_layout_hash_test
        test/shm_pub_sub_layout_hash_test.cpp
      )
    target_link_libraries(shm_pub_sub_layout_hash_test
        PRIVATE
        shm_pub_sub
        ${GTEST_LDFLAGS}
      )
    gtest_add_tests(TARGET shm_pub_sub_layout_hash_test TEST_LIST shm_pub_sub_layout_hash_test_cases)
  set_tests_properties(${shm_pub_sub_layout_hash_test_cases} PROPERTIES TIMEOUT ${SHM_TEST_TIMEOUT} RESOURCE_LOCK dev_shm)

    # 書式の宣言が二重インクルードで壊れないこと（Pi4 の全体ビルドで露見）
    add_executable(shm_pub_sub_double_include_test
        test/shm_pub_sub_double_include_test.cpp
      )
    target_link_libraries(shm_pub_sub_double_include_test
        PRIVATE
        shm_pub_sub
        ${GTEST_LDFLAGS}
      )
    gtest_add_tests(TARGET shm_pub_sub_double_include_test TEST_LIST shm_pub_sub_double_include_test_cases)
  set_tests_properties(${shm_pub_sub_double_include_test_cases} PROPERTIES TIMEOUT ${SHM_TEST_TIMEOUT} RESOURCE_LOCK dev_shm)

    # 書式の宣言の置き場所を静的に検査する（R04 / Pi4 のビルドで露見）。
    # 「暗黙実体化の後に特殊化を宣言した」はコンパイラの版によって通ってしまうため、
    # コンパイルに頼らずここで弾く。共有メモリを触らないので直列化は要らない。
    add_test(NAME FormatDeclarationPlacement
        COMMAND ${Python3_EXECUTABLE}
                ${CMAKE_CURRENT_SOURCE_DIR}/test/check_format_declaration_placement.py
                ${CMAKE_CURRENT_SOURCE_DIR}/..)
    set_tests_properties(FormatDeclarationPlacement PROPERTIES TIMEOUT ${SHM_TEST_TIMEOUT})

    # 世代切替と publish の競合を決定的に検査する（R04-F24）
    add_executable(shm_pub_sub_cutover_test
        test/shm_pub_sub_cutover_test.cpp
      )
    target_link_libraries(shm_pub_sub_cutover_test
        PRIVATE
        shm_pub_sub
        ${GTEST_LDFLAGS}
      )
    gtest_add_tests(TARGET shm_pub_sub_cutover_test TEST_LIST shm_pub_sub_cutover_test_cases)
  set_tests_properties(${shm_pub_sub_cutover_test_cases} PROPERTIES TIMEOUT ${SHM_TEST_TIMEOUT} RESOURCE_LOCK dev_shm)

    # Python バインディングの回帰テスト（R04-F16）
    #
    # サニタイザ構成では動かせない。python3 本体がサニタイザ付きで
    # ビルドされていないため、サニタイズされた .so を後から読み込むと
    # 「ASan runtime does not come first」で拒否される。
    # LD_PRELOAD で回避はできるが、Python 本体のリークまで報告されて
    # 実用にならないので、その構成では登録しない。
    if(SANITIZER STREQUAL "none")
    add_test(NAME PythonBindingTest
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test/python_binding_test.py)
    set_tests_properties(PythonBindingTest PROPERTIES
        TIMEOUT ${SHM_TEST_TIMEOUT}
        RESOURCE_LOCK dev_shm
        ENVIRONMENT "PYTHONPATH=$<TARGET_FILE_DIR:shm_pub_sub>;LD_LIBRARY_PATH=$<TARGET_FILE_DIR:shm_base>")
    endif()

    # 時刻指定読み出し（タイムマシン）の回帰テスト
    add_executable(shm_pub_sub_timemachine_test
        test/shm_pub_sub_timemachine_test.cpp
      )
    target_link_libraries(shm_pub_sub_timemachine_test
        PRIVATE
        shm_pub_sub
        ${GTEST_LDFLAGS}
      )
    gtest_add_tests(TARGET shm_pub_sub_timemachine_test TEST_LIST shm_pub_sub_timemachine_test_cases)
  # ハングを FAIL にする（ctest の --timeout に依存させない）
  set_tests_properties(${shm_pub_sub_timemachine_test_cases} PROPERTIES TIMEOUT ${SHM_TEST_TIMEOUT} RESOURCE_LOCK dev_shm)

    # レイアウト世代セグメントの回帰テスト (R01-F01)
    add_executable(shm_pub_sub_generation_test
        test/shm_pub_sub_generation_test.cpp
      )
    target_link_libraries(shm_pub_sub_generation_test
        PRIVATE
        shm_pub_sub
        ${GTEST_LDFLAGS}
      )
    gtest_add_tests(TARGET shm_pub_sub_generation_test TEST_LIST shm_pub_sub_generation_test_cases)
  # ハングを FAIL にする（ctest の --timeout に依存させない）
  set_tests_properties(${shm_pub_sub_generation_test_cases} PROPERTIES TIMEOUT ${SHM_TEST_TIMEOUT} RESOURCE_LOCK dev_shm)

    # レイアウト変更・共有メモリ作り直しの回帰テスト
    add_executable(shm_pub_sub_layout_test
        test/shm_pub_sub_layout_test.cpp
      )
    target_link_libraries(shm_pub_sub_layout_test
        PRIVATE
        shm_pub_sub
        ${GTEST_LDFLAGS}
      )
    gtest_add_tests(TARGET shm_pub_sub_layout_test TEST_LIST shm_pub_sub_layout_test_cases)
  # ハングを FAIL にする（ctest の --timeout に依存させない）
  set_tests_properties(${shm_pub_sub_layout_test_cases} PROPERTIES TIMEOUT ${SHM_TEST_TIMEOUT} RESOURCE_LOCK dev_shm)
endif()
