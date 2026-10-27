cmake_minimum_required(VERSION 3.24)
project(TarvosTool VERSION 0.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Default to arm64 on Apple Silicon; override with -DCMAKE_OSX_ARCHITECTURES=x86_64
if(APPLE AND NOT CMAKE_OSX_ARCHITECTURES)
    set(CMAKE_OSX_ARCHITECTURES "arm64" CACHE STRING "macOS target architecture" FORCE)
endif()

# Homebrew prefix — arm64 uses /opt/homebrew, x86_64 uses /usr/local
if(CMAKE_OSX_ARCHITECTURES STREQUAL "x86_64")
    set(HOMEBREW_PREFIX "/usr/local")
else()
    set(HOMEBREW_PREFIX "/opt/homebrew")
endif()

list(APPEND CMAKE_PREFIX_PATH "${HOMEBREW_PREFIX}")

find_package(wxWidgets REQUIRED COMPONENTS core base)
include(${wxWidgets_USE_FILE})

# ── Executable ────────────────────────────────────────────────────────────────
add_executable(TarvosTool MACOSX_BUNDLE
    src/main.cpp
    src/MainFrame.cpp
    src/ChatFrame.cpp
    src/SerialPort.cpp
    src/TarvosProtocol.cpp
)

target_include_directories(TarvosTool PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${HOMEBREW_PREFIX}/include
)

target_link_libraries(TarvosTool PRIVATE ${wxWidgets_LIBRARIES})

set_target_properties(TarvosTool PROPERTIES
    MACOSX_BUNDLE_BUNDLE_NAME        "TarvosTool"
    MACOSX_BUNDLE_BUNDLE_VERSION     "${PROJECT_VERSION}"
    MACOSX_BUNDLE_GUI_IDENTIFIER     "com.fsm.tarvostool"
    MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
)

# Symlink compile_commands.json to project root for VS Code / clangd
if(CMAKE_PROJECT_NAME STREQUAL PROJECT_NAME)
    add_custom_target(copy_compile_commands ALL
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${CMAKE_BINARY_DIR}/compile_commands.json
            ${CMAKE_CURRENT_SOURCE_DIR}/compile_commands.json
        DEPENDS TarvosTool
        COMMENT "Copying compile_commands.json to source root"
    )
endif()
