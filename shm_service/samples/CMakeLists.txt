cmake_minimum_required(VERSION 3.10)

##sample
add_executable(shm_service_sample test1.cpp)
target_include_directories(shm_service_sample PUBLIC
    $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/samples> 
    $<INSTALL_INTERFACE:samples>
)
target_link_libraries(shm_service_sample shm_base shm_service rt pthread)
