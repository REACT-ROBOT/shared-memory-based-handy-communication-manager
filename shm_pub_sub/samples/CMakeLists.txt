cmake_minimum_required(VERSION 3.1)

##sample
add_executable(shm_pub_sub_sample test1.cpp)
target_include_directories(shm_pub_sub_sample PUBLIC
    $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/samples> 
    $<INSTALL_INTERFACE:samples>
)
target_link_libraries(shm_pub_sub_sample shm_base shm_pub_sub rt pthread)
