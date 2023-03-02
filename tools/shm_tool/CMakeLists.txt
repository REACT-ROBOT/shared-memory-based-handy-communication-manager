cmake_minimum_required(VERSION 3.1)

##sample
add_executable(shm_tool src/main.cpp)
target_include_directories(shm PUBLIC
    $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/samples> 
    $<INSTALL_INTERFACE:samples>
)
target_link_libraries(shm_tool shm_base rt pthread)
