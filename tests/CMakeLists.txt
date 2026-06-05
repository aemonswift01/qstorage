cmake_minimum_required(VERSION 3.30)

project(test)

set(BUILD_OUTPUT_TEST_DIR ${BUILD_OUTPUT_DIR}/test)

file(GLOB dirs LIST_DIRECTORIES TRUE  ${CMAKE_CURRENT_LIST_DIR}/*)


foreach(dir IN LISTS dirs)
    if(EXISTS ${dir}/CMakeLists.txt)
        add_subdirectory(${dir})
    endif()
endforeach()