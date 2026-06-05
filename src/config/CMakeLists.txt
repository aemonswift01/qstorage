
#[[
CMAKE_CURRENT_SOURCE_DIR	始终指向当前子项目的源码根目录（由 add_subdirectory() 决定）
CMAKE_CURRENT_LIST_DIR	指向当前正在执行的 CMake 脚本文件（.cmake 或 CMakeLists.txt）所在目录
]]

configure_file(${CMAKE_CURRENT_SOURCE_DIR}/template/config.h.in ${CMAKE_CURRENT_SOURCE_DIR}/config.h)


add_library(config INTERFACE)


# FILE(GLOB H_FILE CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/*.h )

target_include_directories(config INTERFACE 
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
    $<INSTALL_INTERFACE:include>
)


# 根据实际文件调整,若头文件依赖此文件，则需要导出
# install(FILES config.h DESTINATION include)


