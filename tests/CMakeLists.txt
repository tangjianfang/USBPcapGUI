if(BHPLUS_BUILD_TESTS)
    find_package(GTest CONFIG REQUIRED)
    
    add_executable(bhplus_tests
        core_tests/test_parsers.cpp
    )
    
    target_link_libraries(bhplus_tests PRIVATE
        GTest::gtest
        GTest::gtest_main
        $<LINK_LIBRARY:WHOLE_ARCHIVE,bhplus_core>
    )
    # WHOLE_ARCHIVE ensures parser self-registrars are linked in from the static lib
    
    include(GoogleTest)
    gtest_discover_tests(bhplus_tests)
endif()
