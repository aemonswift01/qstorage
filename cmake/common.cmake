
macro(get_srcs_includes)
    aux_source_directory(${CMAKE_CURRENT_SOURCE_DIR} SRC)
    # 对内接口
    FILE(GLOB H_FILE CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/*.h )
    # 对外接口
    FILE(GLOB H_FILE_I CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/include/*.h  
        EXCLUDE  ${CMAKE_CURRENT_SOURCE_DIR}/include/${name}_export.h 
    )   
endmacro()


macro(cc_executable_internal)
    get_srcs_includes()

    add_executable(${name} ${SRC} ${H_FILE} ${H_FILE_I})

    if (MSVC)
        set_target_properties(${name} PROPERTIES 
            COMPILE_FLAGS "-bigobj"
        )
    endif()

    math(EXPR size ${ARGC}-1)
    if (size GREATER_EQUAL 1) 
        foreach(i RANGE 1 ${size}) #1,2,...,${size}
            #message(${i} " " ${ARGV${i}})
            target_link_libraries(${name} PRIVATE ${ARGV${i}})
        endforeach()
    endif()

endmacro()



function(cc_test name)
    cc_executable_internal(${ARGV})

    build_output(${name} ${BUILD_OUTPUT_TEST_DIR} TRUE)

    add_test(NAME ${name} COMMAND ${name})
endfunction()


function(cc_executable name)

    cc_executable_internal(${ARGV})

    build_output(${name} ${BUILD_OUTPUT_DIR})
    install(FILES ${name} DESTINATION bin)
endfunction()



function(cc_library name)
    message(STATUS "=====Building library ${name}========")

    set(LIB_NAME ${name})
    configure_file(${CMAKE_SOURCE_DIR}/cmake/dll.h.in ${CMAKE_CURRENT_SOURCE_DIR}/include/${name}_export.h)

    #是否是动态库
    option(${name}_SHARED "OFF is static" OFF)
    set(TYPE STATIC)
    if (${name}_SHARED)
        set(TYPE SHARED)
    endif()

    get_srcs_includes()

    add_library(${name} ${TYPE} ${SRC}  ${H_FILE} ${H_FILE_I}) 


    get_target_property(CONFIG_INCLUDE_DIRS config INTERFACE_INCLUDE_DIRECTORIES)

    target_include_directories(${name} PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
        PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
       # PRIVATE ${CMAKE_SOURCE_DIR}/src/config
       PRIVATE ${CONFIG_INCLUDE_DIRS}
    )

    # target_compile_features(${name} PRIVATE 
    #     cxx_std_20
    # )

    if (MSVC)
        set_target_properties(${name} PROPERTIES 
            COMPILE_FLAGS "-bigobj"
        )
    endif()

    if(${name}_SHARED)
        target_compile_definitions(${name} PUBLIC ${name}_EXPORTS)
    else()
        target_compile_definitions(${name} PUBLIC ${name}_STATIC)
    endif()

    build_output(${name} ${BUILD_OUTPUT_DIR})


    if (MSVC)
        set_target_properties(${name} PROPERTIES 
            DEBUG_POSTFIX "d"
        )
    endif()


    set_target_properties(${name} PROPERTIES 
        PUBLIC_HEADER "${H_FILE_I}"
    )

    install_func(${name})

    message(STATUS "==================================")
endfunction()




function(install_func name)
    install(TARGETS ${name}
        EXPORT ${name}
        RUNTIME DESTINATION bin
        LIBRARY DESTINATION lib
        ARCHIVE DESTINATION lib
        PUBLIC_HEADER DESTINATION include
    )

    install(EXPORT ${name} FILE  ${name}Config.cmake
        DESTINATION lib/cmake/${name}-${VERSION}/
    )


    set(GENERATE_CMAKE_DIR ${BUILD_OUTPUT_DIR}/cmake/${name}-${VERSION})
    include(CMakePackageConfigHelpers)
    write_basic_package_version_file(
        ${GENERATE_CMAKE_DIR}/${name}ConfigVersion.cmake
        VERSION ${VERSION}
        COMPATIBILITY SameMajorVersion
    )

    install(FILES ${GENERATE_CMAKE_DIR}/${name}ConfigVersion.cmake
        DESTINATION lib/cmake/${name}-${VERSION}/
    )
endfunction()


#[[
/*
在非 Windows 平台（Linux / macOS）
+ 共享库（.so / .dylib）默认所有符号都导出。
+ 不需要特殊关键字，直接用 void func(); 即可被外部调用。
在 Windows 平台
+ 静态库（.lib）：不需要导出，和普通函数一样。
+ 动态库（DLL）：
    + 编译 DLL 时：需要用 __declspec(dllexport) 标记要导出的函数/类。
    + 使用 DLL 时：需要用 __declspec(dllimport) 告诉编译器这个符号来自 DLL。
+ 如果不加，链接会失败！
*/
]]
#   #[[
#   #if defined(_WIN32) && !defined(xlog_static)
#     #ifdef xlog_exports
#         #define xcpp_api __declspec(dllexport)
#     #else
#         #define xcpp_api __declspec(dllimport)
#     #endif
#   #else
#     #define xcpp_api
#   #endif
#   ]]  
#     include(GenerateExportHeader)
#     generate_export_header(${name} 
#         BASE_NAME ${name}               # 可选，用于生成 MYLIB_STATIC_DEFINE 等
#         EXPORT_MACRO_NAME ${name}_API   # 宏名：MYLIB_API
#         STATIC_DEFINE ${name}_STATIC_DEFINE  # 静态库开关宏
#     )


function(build_output name output_dir)
    set(output_dir_bin ${output_dir}/bin)
    if (${ARGV2})
        set(output_dir_bin ${output_dir})
    endif()
    set(CONF_TYPES Debug Release MinSizeRel RelWithDebInfo) 
    list(APPEND CONF_TYPES "")
    foreach(type IN LISTS CONF_TYPES)
        set(conf "")
        if (type)
            string(TOUPPER _${type} conf)
        endif()
        set_target_properties(${name} PROPERTIES 
            RUNTIME_OUTPUT_DIRECTORY${conf} ${output_dir_bin} #dll pdb exe 执行程序
            LIBRARY_OUTPUT_DIRECTORY${conf} ${output_dir}/lib # .so .dylib 动态库导出文件
            ARCHIVE_OUTPUT_DIRECTORY${conf} ${output_dir}/lib # .lib .a 静态库导出文件
            PDB_OUTPUT_DIRECTORY${conf} ${output_dir_bin} #pdb
        )
    endforeach()
endfunction()

