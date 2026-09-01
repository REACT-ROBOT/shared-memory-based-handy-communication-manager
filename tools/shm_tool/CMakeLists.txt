cmake_minimum_required(VERSION 3.10)

##shm_tool
add_executable(shm_tool src/main.cpp)
target_link_libraries(shm_tool shm_base rt pthread)

##install
install(TARGETS shm_tool 
	DESTINATION bin
)

## doctor の回帰テスト（R04 / 制約 D）
if(BUILD_TESTS)
  find_package(PkgConfig REQUIRED)
  pkg_check_modules(GTEST gtest_main)
  if(GTEST_FOUND)
    include_directories(${GTEST_INCLUDE_DIRS})
    link_directories(${GTEST_LIBRARY_DIRS})
    include(GoogleTest)

    add_executable(shm_tool_doctor_test test/shm_tool_doctor_test.cpp)
    target_link_libraries(shm_tool_doctor_test PRIVATE shm_pub_sub ${GTEST_LDFLAGS})
    add_dependencies(shm_tool_doctor_test shm_tool)
    gtest_add_tests(TARGET shm_tool_doctor_test TEST_LIST shm_tool_doctor_test_cases)
    # テストから shm_tool を起動する。ビルドツリー内の実体を指す。
    set_tests_properties(${shm_tool_doctor_test_cases} PROPERTIES
        TIMEOUT ${SHM_TEST_TIMEOUT}
        RESOURCE_LOCK dev_shm
        ENVIRONMENT "SHM_TOOL_PATH=$<TARGET_FILE:shm_tool>")
  endif()
endif()
