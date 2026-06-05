cmake_minimum_required(VERSION 3.30)

project(cpp_frame)

#===============config======================
option(ENABLE_FAILPOINT "Enable failpoint (test only)" ON)

option(BUILD_TEST "build test" OFF)

option(ENABLE_COVERAGE "Enable coverage build" ON)

#===========================================

#===================config.h.in===================
set(DEBUG 1)
set(VERSION "0.0.1")
set(MAX_THREADS 5)
#==================================================


#==================build====================
set(BUILD_OUTPUT_DIR ${CMAKE_SOURCE_DIR}/build_out)
if(CMAKE_BUILD_TYPE STREQUAL "")
    set(CMAKE_BUILD_TYPE "Debug")
endif()
#===========================================


#==================deps===================
include(FetchContent)
set(DPES_DIR ${CMAKE_SOURCE_DIR}/deps/tars)
set(EXTRACT_DIR ${CMAKE_SOURCE_DIR}/deps/extract)
set(BUILD_DEPS_DIR ${CMAKE_BINARY_DIR})


include(${CMAKE_SOURCE_DIR}/cmake/fetchcontent.cmake)
fetch_content(googletest 
    https://github.com/google/googletest/archive/refs/tags/v1.17.0.tar.gz
)
#=========================================




#==================coverage===================

if (ENABLE_COVERAGE) 
    set(BUILD_TEST ON)
    set(CMAKE_BUILD_TYPE "Debug")
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        set(COVERAGE_FLAGS "--coverage")
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${COVERAGE_FLAGS}")
        set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} ${COVERAGE_FLAGS}")
        message(STATUS "Coverage enabled: ${COVERAGE_FLAGS}")
    else()
        message(WARNING "Coverage not supported for ${CMAKE_CXX_COMPILER_ID}")
    endif()
endif()
#============================================




add_subdirectory(src)

add_subdirectory(tools)


#============test===================
if (BUILD_TEST) 
    #message("build test")
    # cd build && ctest --verbose     # 使用 ctest（推荐）
    enable_testing()
    add_subdirectory(tests)
endif()
#===================================



#============generate html=================
if(ENABLE_COVERAGE AND (CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang"))
    find_program(GCOVR_PATH gcovr)
    file(MAKE_DIRECTORY ${CMAKE_BINARY_DIR}/coverage_report)
    if(GCOVR_PATH)
        add_custom_target(coverage
            COMMAND ${GCOVR_PATH}
                --r ${CMAKE_SOURCE_DIR}
                --exclude '${CMAKE_SOURCE_DIR}/deps/*'
                --exclude '${CMAKE_SOURCE_DIR}/tests/*'
                --html
                --html-details
                -o ${CMAKE_BINARY_DIR}/coverage_report/coverage.html
            WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
            COMMENT "Generating coverage report..."
        )
    endif()
endif()

#[[
cmake -S . -B build  
cmake --build build 
cd build 
ctest 
make coverage

cmake --build build --target coverage

]]
#=========================================