cmake_minimum_required(VERSION 3.28)
project(USBPcapGUI VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)

# Options
option(BHPLUS_BUILD_DRIVER    "Build kernel-mode filter driver (requires WDK)" OFF)
option(BHPLUS_BUILD_CLI       "Build command-line tool"                        ON)
option(BHPLUS_BUILD_SDK       "Build automation SDK"                           ON)
option(BHPLUS_BUILD_TESTS     "Build unit tests"                               ON)
option(BHPLUS_BUILD_LAUNCHER  "Build GUI launcher (USBPcapGUI.exe)"            ON)

# Output directories
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

# Shared icon / version resource for all EXE and DLL targets
set(BHPLUS_APP_RC ${CMAKE_SOURCE_DIR}/resources/app.rc)

# Shared headers available to all subprojects
include_directories(${CMAKE_SOURCE_DIR}/shared/include)

# Sub-projects
add_subdirectory(shared)
add_subdirectory(core)

# GUI is now a Node.js project — built separately via npm
# See gui/README.md for instructions: cd gui && npm install && npm start

if(BHPLUS_BUILD_CLI)
    add_subdirectory(cli)
endif()

if(BHPLUS_BUILD_SDK)
    add_subdirectory(sdk)
endif()

if(BHPLUS_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

if(BHPLUS_BUILD_LAUNCHER)
    add_subdirectory(launcher)
endif()

# Driver is built separately via WDK MSBuild (see driver/README.md)
