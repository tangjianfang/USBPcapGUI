add_executable(USBPcapGUI WIN32
    src/main.cpp
)

target_link_libraries(USBPcapGUI PRIVATE
    shell32
    shlwapi
)

# Ensure Chinese/Unicode string literals compile correctly on MSVC
if(MSVC)
    target_compile_options(USBPcapGUI PRIVATE /utf-8)
endif()

set_target_properties(USBPcapGUI PROPERTIES
    OUTPUT_NAME "USBPcapGUI"
    WIN32_EXECUTABLE TRUE
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)

