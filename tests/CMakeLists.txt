if(BHPLUS_BUILD_TESTS)
    find_package(GTest CONFIG REQUIRED)
    
    add_executable(bhplus_tests
        core_tests/test_parsers.cpp
        core_tests/test_core_utils.cpp
        core_tests/test_usb_actions.cpp
        core_tests/test_capture_config.cpp
    )
    
    target_link_libraries(bhplus_tests PRIVATE
        GTest::gtest
        GTest::gtest_main
        $<LINK_LIBRARY:WHOLE_ARCHIVE,bhplus_core>
    )
    # WHOLE_ARCHIVE ensures parser self-registrars are linked in from the static lib

    # Copy vcpkg DLLs next to the test exe so gtest_discover_tests can run it
    add_custom_command(TARGET bhplus_tests POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_RUNTIME_DLLS:bhplus_tests> $<TARGET_FILE_DIR:bhplus_tests>
        COMMAND_EXPAND_LISTS
    )

    include(GoogleTest)
    gtest_discover_tests(bhplus_tests)
endif()
