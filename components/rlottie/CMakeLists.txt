cmake_minimum_required(VERSION 3.5)

idf_component_register(SRCS ${SOURCES}
                    INCLUDE_DIRS "${CMAKE_CURRENT_LIST_DIR}/rlottie/inc"
                    )

set(LOTTIE_MODULE OFF)
set(LOTTIE_THREAD OFF)
set(BUILD_SHARED_LIBS OFF)
# option(BUILD_TESTING OFF)


function(install)
endfunction()

function(export)
endfunction()

add_compile_options(-Wno-unused-value)

add_subdirectory(rlottie)
target_link_libraries(${COMPONENT_LIB} INTERFACE rlottie)
