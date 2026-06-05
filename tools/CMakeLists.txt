file(GLOB dirs LIST_DIRECTORIES TRUE  ${CMAKE_CURRENT_LIST_DIR}/*)


foreach(dir IN LISTS dirs)
    if(EXISTS ${dir}/CMakeLists.txt)
        add_subdirectory(${dir})
    endif()
endforeach()
