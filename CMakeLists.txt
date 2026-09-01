cmake_minimum_required(VERSION 3.10)

project(shm VERSION 4.0.0 LANGUAGES CXX)

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
# 3.0.0: 共有メモリ形式を v2 に変更（レビュー R01 の F04/F05/F06/F07-a）。
#        自己記述的なヘッダ（magic / ABI 版 / 総サイズ / payload_alignment /
#        generation / sequence）とスロット単位の robust mutex を導入した。
#        **共有メモリのレイアウトが変わるため、同じトピックに接続する全プロセスを
#        再ビルドして同時に入れ替えること。入れ替え前に `shm_tool remove` で
#        既存の /dev/shm/shm_* を消すこと。**
#        新しいコードが v1 の領域を読んだ場合は magic 不一致として明確に失敗する。
# 4.0.0: 再レビュー R02 の指摘に対応。共有メモリヘッダに topic contract
#        （payload kind / element_size / schema_id）を追加したため ABI を 3 に上げた。
#        **v3 以前のセグメントとは接続できない。入れ替え前に `shm_tool remove`。**
#        既定権限を 0666 から 0660 に変更した（other を落とした）。別ユーザー間で
#        共有する必要がある場合は同じグループにするか PERM_ALL を明示すること。
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

# ----------------------------------------------------------------------------
# 型制約の厳格化（R01-F07-b / 独立トラック）
#
# 共有メモリはバイト列でしかないため、置ける型は「memcpy で複製しても
# 意味が壊れない型」= trivially copyable でなければならない。
# 現状この検査は ARM でのみ実行時に行っており、x86 では
# standard-layout しか見ていないため、コピーコンストラクタや
# デストラクタを持つ型が素通りして値が黙って化ける。
#
# 既定は ON である。導入時に次の手順を踏んで安全を確認した:
#   1. 全パッケージを -DSHM_STRICT_TYPE_CHECK=ON でビルドし、コンパイルエラーに
#      なる型を洗い出した（実行不要・網羅的）
#   2. 見つかった 4 件を修正した。いずれも std::vector を持つ型を汎用 Publisher で
#      publish しており、別プロセスから購読すると SIGSEGV する実害があった
#        - lidar / point_cloud の Python バインディングが特殊化を include し忘れ
#        - sensor_daemon_base の例が std::vector を持つ型を publish
#   3. Raspberry Pi 4 実機で全パッケージのビルドとテスト 92/92 を確認した
#
# 新しい型を publish するときにこの検査で止められた場合、その型は共有メモリに
# 置けない。エラーメッセージに直し方が書いてある。どうしても可変長が必要なら
# シリアライズする Publisher の特殊化を書くこと
# （lidar_2D_data / point_cloud_2D_data / cv::Mat が実例）。
#
# OFF にすると検査が消えるが、実行時の検査（standard-layout は全環境、
# trivially-copyable は ARM のみ）だけが残る。x86 では素通りするので推奨しない。
# ----------------------------------------------------------------------------
option(SHM_STRICT_TYPE_CHECK
       "Require trivially-copyable payload types at compile time (R01-F07-b)" ON)
if(SHM_STRICT_TYPE_CHECK)
    message(STATUS "Strict payload type checking is ON (trivially-copyable required)")
endif()

# ----------------------------------------------------------------------------
# ペイロード書式の宣言を必須にするか（R04 / 制約 A）
#
# `element_size`（sizeof）の照合は「同サイズのままメンバを並べ替えた」変更を
# 通してしまう。再デプロイ後に古いプロセスが生き残っている場合や、古い
# セグメントが残っている場合に、別物を同じ形式として読み合うことになる。
#
# SHM_DECLARE_LAYOUT() でメンバを並べておくと、offsetof と sizeof から
# レイアウトのハッシュが自動で作られ、その穴が閉じる。版番号を手で維持する
# 必要はない（並べ替え・型入れ替え・追加・削除・アライメント変更が全て出る）。
#
# **既定は OFF である。** workspace には 90 種類以上のペイロード型があり、
# 一斉に必須化すると複数リポジトリのビルドが同時に止まるため。
# 移行はパッケージ単位で行う:
#   1. そのパッケージを -DSHM_REQUIRE_LAYOUT=ON でビルドする
#   2. 止まった型に SHM_DECLARE_LAYOUT() を足す
#      （ワイヤ形式が serialize() で決まる型は SHM_DECLARE_SERIALIZED_FORMAT()）
#   3. 全パッケージが済んだらここの既定を ON にする
#
# 宣言していない型は従来どおり動く（schema_id と element_size だけで照合する）。
# どのトピックが未宣言かは `shm_tool doctor` で確認できる。
# ----------------------------------------------------------------------------
option(SHM_REQUIRE_LAYOUT
       "Require every payload type to declare its wire format at compile time (R04)" OFF)
if(SHM_REQUIRE_LAYOUT)
    message(STATUS "Payload format declaration is REQUIRED (SHM_DECLARE_LAYOUT)")
endif()

# ----------------------------------------------------------------------------
# 競合を決定的に再現するためのテスト用フック（R04-F24）
#
# 世代切替と publish の競合は、外から叩くだけでは狙った順序で起こせない。
# R04 のレビューでは、cutover 三点セット（publish 後の世代確認 / 切替直後の
# 回収）を丸ごと取り消しても全テストが緑のままであることが実証された。
# 決定的に検査するには publish の途中で切替を差し込む必要がある。
#
# テストのビルドでのみ ON にする。製品ビルドには何も残らない
# （フックの宣言ごとプリプロセッサで消える）。
# ----------------------------------------------------------------------------
option(SHM_ENABLE_TEST_HOOKS
       "Expose hooks that let tests interleave a generation cutover with a publish (R04-F24)" OFF)
if(BUILD_TESTS)
    # テストを作るときは常に有効にする
    set(SHM_ENABLE_TEST_HOOKS ON CACHE BOOL "" FORCE)
endif()
if(SHM_ENABLE_TEST_HOOKS)
    message(STATUS "Test hooks are ON (SHM_ENABLE_TEST_HOOKS)")
endif()
# NOTE: ここで add_compile_definitions() を使ってはならない。
#       ディレクトリスコープなので、このサブツリー（shm_base / shm_pub_sub / tools）
#       にしか効かず、上位のワークスペースに並ぶ兄弟パッケージ
#       （lidar_2D_data、point_cloud_2D_data、react_cv、navigators など）には
#       伝播しない。洗い出したいのはまさにそれらが publish している型なので、
#       それでは検査した意味がなくなる。
#       定義は shm_base / shm_pub_sub ターゲットの PUBLIC に付け、
#       リンクした側へ伝播させる（各サブディレクトリの CMakeLists.txt 参照）。

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
