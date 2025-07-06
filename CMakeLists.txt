cmake_minimum_required(VERSION 3.10)

project(shm CXX)

# Global options
option(DEBUG "switch on debug option" OFF)
#for check memory leak
if (DEBUG)
    set(DEBUG_OPTION "-fsanitize=address -fno-omit-frame-pointer")
endif()
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DBOOST_NO_AUTO_PTR -fPIC ${DEBUG_OPTION}")
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Enable testing
enable_testing()

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
