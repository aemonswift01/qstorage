
# add_subdirectory(config)

# add_subdirectory(xlog)

file(GLOB dirs LIST_DIRECTORIES TRUE  ${CMAKE_CURRENT_LIST_DIR}/*)


foreach(dir IN LISTS dirs)
    if(EXISTS ${dir}/CMakeLists.txt)
        add_subdirectory(${dir})
    endif()
endforeach()



# add_executable(${PROJECT_NAME} main.cc)

# target_link_libraries(${PROJECT_NAME} PRIVATE xlog)
# build_output(${PROJECT_NAME})

include(${CMAKE_SOURCE_DIR}/cmake/common.cmake)

cc_executable(${PROJECT_NAME} xlog)

