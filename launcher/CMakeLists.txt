find_package(spdlog CONFIG REQUIRED)

add_executable(USBPcapGUI WIN32
    src/main.cpp
    ${BHPLUS_APP_RC}
)

target_include_directories(USBPcapGUI PRIVATE
    ${CMAKE_SOURCE_DIR}/shared/include
)

target_link_libraries(USBPcapGUI PRIVATE
    shell32
    shlwapi
    spdlog::spdlog
)

# UAC: require admin so USBPcapGUI.exe can launch bhplus-core.exe elevated
if(MSVC)
    target_link_options(USBPcapGUI PRIVATE
        "/MANIFESTUAC:level='requireAdministrator' uiAccess='false'"
    )
endif()

# Ensure Chinese/Unicode string literals compile correctly on MSVC
if(MSVC)
    target_compile_options(USBPcapGUI PRIVATE /utf-8)
endif()

set_target_properties(USBPcapGUI PROPERTIES
    OUTPUT_NAME "USBPcapGUI"
    WIN32_EXECUTABLE TRUE
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)

