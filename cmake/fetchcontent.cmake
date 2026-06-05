
#[[
# 自动下载、解压、add_subdirectory
FetchContent_MakeAvailable(googletest)
]]

function(fetch_content name url)
    file(GLOB src_dirs ${EXTRACT_DIR}/${name}*)
    if (NOT src_dirs) 
        file(GLOB tars ${DPES_DIR}/${name}*.tar.gz)
        if (tars) 
            list(GET tars 0 targz)
            file(MAKE_DIRECTORY ${EXTRACT_DIR})
            execute_process(COMMAND ${CMAKE_COMMAND} -E tar xzf ${targz} 
                WORKING_DIRECTORY ${EXTRACT_DIR}
                RESULT_VARIABLE tar_result
            )
            if(NOT tar_result EQUAL 0) 
                message(FATAL_ERROR "Failed to extract ${targz}")
            endif()

            file(GLOB src_dirs ${EXTRACT_DIR}/${name}*)
            if(NOT src_dirs)
                message(FATAL_ERROR "No ${name} source directory found after extraction!")
            endif()
        endif()
    endif()

    if(src_dirs)
        list(GET src_dirs 0 src_dir)
        set(USE_LOCAL ON)
    endif()

    if(USE_LOCAL)
        message(STATUS "Using local ${name}")
        FetchContent_Declare(
            googletest 
            SOURCE_DIR ${src_dir}
            BINARY_DIR  ${BUILD_DEPS_DIR}/${name}
        )
    else()
        message(STATUS "Downloading ${name}")
        FetchContent_Declare(
            googletest
            URL ${url}
            # GIT_TAG        release-1.11.0
            BINARY_DIR  ${BUILD_DEPS_DIR}/${name}
        )
    endif()

    FetchContent_MakeAvailable(${name})
endfunction()



