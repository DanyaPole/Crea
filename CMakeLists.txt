cmake_minimum_required(VERSION 3.16)
project(TectonicCLAP VERSION 5.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(APPLE)
    enable_language(OBJCXX)
    set(CMAKE_OBJCXX_STANDARD 20)
    set(CMAKE_OBJCXX_STANDARD_REQUIRED ON)
endif()

include(FetchContent)
FetchContent_Declare(
    imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG v1.90.1
    GIT_SHALLOW TRUE
)
FetchContent_GetProperties(imgui)
if(NOT imgui_POPULATED)
    FetchContent_Populate(imgui)
endif()

set(IMGUI_SOURCES
    "${imgui_SOURCE_DIR}/imgui.cpp"
    "${imgui_SOURCE_DIR}/imgui_draw.cpp"
    "${imgui_SOURCE_DIR}/imgui_tables.cpp"
    "${imgui_SOURCE_DIR}/imgui_widgets.cpp"
    "${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp"
)

if(WIN32)
    list(APPEND IMGUI_SOURCES "${imgui_SOURCE_DIR}/backends/imgui_impl_win32.cpp")
    set(PLATFORM_GUI_SOURCE imgui_gui_win32.cpp)
elseif(APPLE)
    list(APPEND IMGUI_SOURCES "${imgui_SOURCE_DIR}/backends/imgui_impl_osx.mm")
    set(PLATFORM_GUI_SOURCE imgui_gui_macos.mm)
else()
    message(FATAL_ERROR "Tectonic Synth supports Windows and macOS builds only.")
endif()

add_library(imgui STATIC ${IMGUI_SOURCES})
target_include_directories(imgui PUBLIC "${imgui_SOURCE_DIR}" "${imgui_SOURCE_DIR}/backends")
set_target_properties(imgui PROPERTIES POSITION_INDEPENDENT_CODE ON)
if(WIN32)
    target_compile_definitions(imgui PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
endif()

add_library(TectonicCLAP MODULE tectonic_synth.cpp)
target_sources(TectonicCLAP PRIVATE "${PLATFORM_GUI_SOURCE}")
target_link_libraries(TectonicCLAP PRIVATE imgui)
target_include_directories(TectonicCLAP PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/include")

if(MSVC)
    target_compile_options(TectonicCLAP PRIVATE /O2 /fp:fast /utf-8)
    target_compile_definitions(TectonicCLAP PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
else()
    target_compile_options(TectonicCLAP PRIVATE -O3 -ffast-math -fvisibility=hidden)
endif()

if(WIN32)
    target_link_libraries(TectonicCLAP PRIVATE opengl32)
elseif(APPLE)
    target_link_libraries(TectonicCLAP PRIVATE "-framework Cocoa" "-framework OpenGL" "-framework GameController")
    set_target_properties(TectonicCLAP PROPERTIES
        BUNDLE TRUE
        BUNDLE_EXTENSION "clap"
        OUTPUT_NAME "TectonicSynth"
        MACOSX_BUNDLE_GUI_IDENTIFIER "com.experimental.tectonic-synth"
        MACOSX_BUNDLE_BUNDLE_NAME "TectonicSynth"
    )
else()
    set_target_properties(TectonicCLAP PROPERTIES
        PREFIX ""
        OUTPUT_NAME "TectonicSynth"
        SUFFIX ".clap"
    )
endif()
