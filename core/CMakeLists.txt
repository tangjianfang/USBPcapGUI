find_package(spdlog CONFIG REQUIRED)
find_package(fmt CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)

add_library(bhplus_core STATIC
    src/driver_manager.cpp
    src/capture_engine.cpp
    src/capture_config_parser.cpp
    src/pcap_parser.cpp
    src/usbpcap_reader.cpp
    src/usb_actions.cpp
    src/device_enumerator.cpp
    src/filter_engine.cpp
    src/trigger_engine.cpp
    src/export_engine.cpp
    src/ipc_server.cpp
    src/parsers/parser_interface.cpp
    src/parsers/usb_parser.cpp
    src/parsers/scsi_parser.cpp
    src/parsers/nvme_parser.cpp
    src/parsers/ata_parser.cpp
    src/parsers/serial_parser.cpp
)

target_include_directories(bhplus_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/shared/include
)

target_link_libraries(bhplus_core PUBLIC
    spdlog::spdlog
    fmt::fmt
    nlohmann_json::nlohmann_json
    setupapi
    cfgmgr32
    shell32
    winusb
)

# Disable fmt consteval on MSVC (known compatibility issue with fmt 12+)
if(MSVC)
    target_compile_definitions(bhplus_core PUBLIC FMT_USE_CONSTEVAL=0)
endif()

# Core service executable
add_executable(bhplus-core src/main.cpp ${BHPLUS_APP_RC})
target_link_libraries(bhplus-core PRIVATE $<LINK_LIBRARY:WHOLE_ARCHIVE,bhplus_core>)

# UAC: require admin — the USBPcap capture devices deny access to non-admin processes
if(MSVC)
    target_link_options(bhplus-core PRIVATE
        "/MANIFESTUAC:level='requireAdministrator' uiAccess='false'"
    )
endif()
