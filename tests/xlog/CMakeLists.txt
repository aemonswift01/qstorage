


# aux_source_directory(${CMAKE_CURRENT_SOURCE_DIR} SRC)

# add_executable(xlog_test ${SRC})

# target_link_libraries(xlog_test xlog gtest_main)

# add_test(NAME xlog_test COMMAND xlog_test)


include(${CMAKE_SOURCE_DIR}/cmake/common.cmake)


cc_test(xlog_test xlog gtest_main)