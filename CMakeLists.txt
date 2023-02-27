cmake_minimum_required(VERSION 3.10)

project(shm CXX)

#project(core_cpp CXX)
add_subdirectory(shm_base)
add_subdirectory(shm_pub_sub)
add_subdirectory(shm_service)
add_subdirectory(shm_action)

FIND_PACKAGE(Doxygen)
IF(DOXYGEN_FOUND)
        SET(DOXYGEN_CONF_FILE "Doxyconfig")
        SET(DOXYGEN_CONF_PATH   ${CMAKE_CURRENT_SOURCE_DIR}/${DOXYGEN_CONF_FILE})
        SET(DOXYGEN_SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR})
        SET(DOXYGEN_TARGET "shm_doc" )

        ADD_CUSTOM_TARGET(${DOXYGEN_TARGET}
                        ${DOXYGEN_EXECUTABLE} ${DOXYGEN_CONF_PATH}
                        DEPENDS  ${DOXYGEN_CONF_PATH})
ELSE(DOXYGEN_FOUND)
#ない場合は，warningを出す．
        MESSAGE (WARNING "doxygen binary couldn't be found")
ENDIF(DOXYGEN_FOUND)
