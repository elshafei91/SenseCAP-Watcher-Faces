FILE(GLOB_RECURSE BYTETRACK_SRCS
    ${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp
)

list(APPEND BYTETRACK_INCS
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

list(APPEND BYTETRACK_PRIV_INCS
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

if("${IDF_VERSION_MAJOR}.${IDF_VERSION_MINOR}" VERSION_GREATER "4.1")
    list(APPEND BYTETRACK_PRIV_REQ
        eigen
    )
endif()

idf_component_register(
    SRCS "${BYTETRACK_SRCS}"
    INCLUDE_DIRS "${BYTETRACK_INCS}"
    PRIV_INCLUDE_DIRS "${BYTETRACK_PRIV_INCS}"
    PRIV_REQUIRES "${BYTETRACK_PRIV_REQ}"
)

