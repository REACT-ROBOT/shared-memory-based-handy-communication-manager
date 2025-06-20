cmake_minimum_required(VERSION 3.10)

##shm_tool
add_executable(shm_tool src/main.cpp)
target_link_libraries(shm_tool shm_base rt pthread)

##install
install(TARGETS shm_tool 
	DESTINATION bin
)
