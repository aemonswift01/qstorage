cmake_minimum_required(VERSION 3.30)


project(failpoint LANGUAGES CXX)


add_library(${PROJECT_NAME} INTERFACE)




target_include_directories(${PROJECT_NAME}  INTERFACE 
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
    $<INSTALL_INTERFACE:include>
)


if(${ENABLE_FAILPOINT})
    target_compile_definitions(${PROJECT_NAME} INTERFACE ENABLE_FAILPOINT)
endif()