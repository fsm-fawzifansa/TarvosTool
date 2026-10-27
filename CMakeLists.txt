cmake_minimum_required(VERSION 3.24)
project(TarvosTool VERSION 0.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(APPLE)
    # Default to arm64 on Apple Silicon; override with -DCMAKE_OSX_ARCHITECTURES=x86_64
    if(NOT CMAKE_OSX_ARCHITECTURES)
        set(CMAKE_OSX_ARCHITECTURES "arm64" CACHE STRING "macOS target architecture" FORCE)
    endif()

    # Homebrew prefix — arm64 uses /opt/homebrew, x86_64 uses /usr/local
    if(CMAKE_OSX_ARCHITECTURES STREQUAL "x86_64")
        set(HOMEBREW_PREFIX "/usr/local")
    else()
        set(HOMEBREW_PREFIX "/opt/homebrew")
    endif()
    list(APPEND CMAKE_PREFIX_PATH "${HOMEBREW_PREFIX}")
endif()

if(WIN32)
    # wxWidgets from vcpkg: vcpkg install wxwidgets:x64-windows
    # and configure with -DCMAKE_TOOLCHAIN_FILE=<vcpkg>/scripts/buildsystems/vcpkg.cmake
    find_package(wxWidgets CONFIG REQUIRED COMPONENTS core base)
    set(TARVOS_WX_LIBS wx::core wx::base)
else()
    find_package(wxWidgets REQUIRED COMPONENTS core base)
    include(${wxWidgets_USE_FILE})
    set(TARVOS_WX_LIBS ${wxWidgets_LIBRARIES})
endif()

# ── Executable ────────────────────────────────────────────────────────────────
add_executable(TarvosTool WIN32 MACOSX_BUNDLE
    src/main.cpp
    src/MainFrame.cpp
    src/ChatFrame.cpp
    src/SerialPort.cpp
    src/TarvosProtocol.cpp
)

target_include_directories(TarvosTool PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)
if(APPLE)
    target_include_directories(TarvosTool PRIVATE ${HOMEBREW_PREFIX}/include)
endif()

target_link_libraries(TarvosTool PRIVATE ${TARVOS_WX_LIBS})

set_target_properties(TarvosTool PROPERTIES
    MACOSX_BUNDLE_BUNDLE_NAME        "TarvosTool"
    MACOSX_BUNDLE_BUNDLE_VERSION     "${PROJECT_VERSION}"
    MACOSX_BUNDLE_GUI_IDENTIFIER     "com.fsm.tarvostool"
    MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
)

# Copy compile_commands.json to project root for VS Code / clangd.
# Only the Makefile/Ninja generators produce it (not Visual Studio).
if(NOT WIN32)
    add_custom_target(copy_compile_commands ALL
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${CMAKE_BINARY_DIR}/compile_commands.json
            ${CMAKE_CURRENT_SOURCE_DIR}/compile_commands.json
        DEPENDS TarvosTool
        COMMENT "Copying compile_commands.json to source root"
    )
endif()
