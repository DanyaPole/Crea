cmake_minimum_required(VERSION 3.22)
project(TectonicSynthV5 VERSION 5.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

option(TECTONIC_ENABLE_IMGUI_OPENGL "Fetch and link Dear ImGui + OpenGL3 backend" ON)

include(FetchContent)

# 1. Fetch official free-audio CLAP C-ABI headers
FetchContent_Declare(
    clap
    GIT_REPOSITORY https://github.com/free-audio/clap.git
    GIT_TAG        1.2.1
)
FetchContent_MakeAvailable(clap)

# 2. Optionally fetch Dear ImGui + OpenGL3 for the 60 FPS Quantum Observatory GUI
if(TECTONIC_ENABLE_IMGUI_OPENGL)
    FetchContent_Declare(
        imgui
        GIT_REPOSITORY https://github.com/ocornut/imgui.git
        GIT_TAG        v1.91.0
    )
    FetchContent_MakeAvailable(imgui)
    find_package(OpenGL REQUIRED)
endif()

# 3. Define the MODULE library target for the .clap plugin
add_library(TectonicSynthV5 MODULE
    tectonic_synth.cpp
)

target_link_libraries(TectonicSynthV5 PRIVATE clap)

if(TECTONIC_ENABLE_IMGUI_OPENGL)
    target_sources(TectonicSynthV5 PRIVATE
        ${imgui_SOURCE_DIR}/imgui.cpp
        ${imgui_SOURCE_DIR}/imgui_draw.cpp
        ${imgui_SOURCE_DIR}/imgui_tables.cpp
        ${imgui_SOURCE_DIR}/imgui_widgets.cpp
        ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp
    )
    target_include_directories(TectonicSynthV5 PRIVATE
        ${imgui_SOURCE_DIR}
        ${imgui_SOURCE_DIR}/backends
    )
    target_compile_definitions(TectonicSynthV5 PRIVATE TECTONIC_HAS_IMGUI_OPENGL=1)
    target_link_libraries(TectonicSynthV5 PRIVATE OpenGL::GL)
endif()

# 4. Architecture-specific SIMD flags (AVX2 + FMA on x86_64, NEON on ARM64)
if(MSVC)
    target_compile_options(TectonicSynthV5 PRIVATE
        /W4 /O2 /Oi /Ot /fp:fast /permissive-
    )
    if(CMAKE_SYSTEM_PROCESSOR MATCHES "AMD64|x86_64")
        target_compile_options(TectonicSynthV5 PRIVATE /arch:AVX2)
    endif()
else()
    target_compile_options(TectonicSynthV5 PRIVATE
        -Wall -Wextra -Wpedantic -O3 -ffast-math -fno-rtti
    )
    if(CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64")
        target_compile_options(TectonicSynthV5 PRIVATE -mavx2 -mfma)
    endif()
endif()

# 5. Platform-specific .clap bundle / binary configuration
if(APPLE)
    set_target_properties(TectonicSynthV5 PROPERTIES
        BUNDLE TRUE
        BUNDLE_EXTENSION "clap"
        OUTPUT_NAME "TectonicSynthV5"
        MACOSX_BUNDLE_GUI_IDENTIFIER "org.tectonic-audio.tectonic-synth-v5"
        MACOSX_BUNDLE_BUNDLE_NAME "TectonicSynthV5"
        MACOSX_BUNDLE_BUNDLE_VERSION "5.0.0"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "5.0.0"
    )
    target_link_libraries(TectonicSynthV5 PRIVATE
        "-framework Cocoa"
        "-framework OpenGL"
        "-framework CoreFoundation"
    )
elseif(WIN32)
    set_target_properties(TectonicSynthV5 PROPERTIES
        PREFIX ""
        OUTPUT_NAME "TectonicSynthV5"
        SUFFIX ".clap"
    )
    target_link_libraries(TectonicSynthV5 PRIVATE opengl32 gdi32 user32 shell32 ole32)
else()
    # Linux X11 + OpenGL + pthread
    find_package(Threads REQUIRED)
    set_target_properties(TectonicSynthV5 PROPERTIES
        PREFIX ""
        OUTPUT_NAME "TectonicSynthV5"
        SUFFIX ".clap"
    )
    target_link_libraries(TectonicSynthV5 PRIVATE Threads::Threads dl)
endif()
# 6. Install rules & CPack Cross-Platform Installer Configuration
if(APPLE)
    install(TARGETS TectonicSynthV5
        BUNDLE DESTINATION "."
        COMPONENT Plugin
    )
else()
    install(TARGETS TectonicSynthV5
        LIBRARY DESTINATION "."
        RUNTIME DESTINATION "."
        COMPONENT Plugin
    )
endif()

set(CPACK_PACKAGE_NAME "TectonicSynthV5")
set(CPACK_PACKAGE_VENDOR "Tectonic Audio Research Lab")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Tectonic Synth v5 — Quantum Observatory CLAP Synthesizer")
set(CPACK_PACKAGE_VERSION_MAJOR 5)
set(CPACK_PACKAGE_VERSION_MINOR 0)
set(CPACK_PACKAGE_VERSION_PATCH 0)
set(CPACK_PACKAGE_INSTALL_DIRECTORY "TectonicSynthV5")

if(WIN32)
    set(CPACK_GENERATOR "ZIP;NSIS")
    set(CPACK_NSIS_DISPLAY_NAME "Tectonic Synth v5 (CLAP)")
    set(CPACK_NSIS_INSTALL_ROOT "$COMMONFILES64/CLAP")
elseif(APPLE)
    set(CPACK_GENERATOR "ZIP;productbuild")
else()
    set(CPACK_GENERATOR "TGZ;DEB")
    set(CPACK_DEBIAN_PACKAGE_MAINTAINER "Tectonic Audio Research Lab")
endif()

include(CPack)
