add_library(bhplus_sdk SHARED
    src/bhplus_sdk.cpp
    ${BHPLUS_APP_RC}
)

target_include_directories(bhplus_sdk PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/shared/include
)

target_link_libraries(bhplus_sdk PRIVATE bhplus_core)

# Export symbols
target_compile_definitions(bhplus_sdk PRIVATE BHPLUS_SDK_EXPORTS)
