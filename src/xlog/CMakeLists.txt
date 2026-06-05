


cmake_minimum_required(VERSION 3.30)

project(xlog)

# CMAKE_SOURCE_DIR：全局常量，由 cmake 命令行决定，永不改变。
# PROJECT_SOURCE_DIR：局部变量，指向最近一次 project() 所在的目录。

include(${CMAKE_SOURCE_DIR}/cmake/common.cmake)

cc_library(${PROJECT_NAME})


# target_link_libraries(${PROJECT_NAME} PRIVATE config)


#[[
set(name xlog)
set(LIB_NAME ${name})
configure_file(${CMAKE_SOURCE_DIR}/cmake/dll.h.in ${CMAKE_CURRENT_SOURCE_DIR}/include/${name}_export.h)

#是否是动态库
option(${name}_SHARED "OFF is static" ON)
set(TYPE STATIC)
if (${name}_SHARED)
    set(TYPE SHARED)
endif()


aux_source_directory(${CMAKE_CURRENT_SOURCE_DIR} SRC)
# 对内接口
FILE(GLOB H_FILE CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/*.h )
# 对外接口
FILE(GLOB H_FILE_I CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/include/*.h  
    # EXCLUDE  ${CMAKE_CURRENT_SOURCE_DIR}/include/${name}_export.h 
)

add_library(${name} ${TYPE} ${SRC}  ${H_FILE} ${H_FILE_I}) 




target_include_directories(${name} PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(${PROJECT_NAME} PUBLIC config)
]]


