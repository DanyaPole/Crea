cmake_minimum_required(VERSION 3.16)
project(TectonicCLAP VERSION 4.0.1 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_library(TectonicCLAP MODULE tectonic_synth.cpp)
target_include_directories(TectonicCLAP PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/include")

if(MSVC)
    target_compile_options(TectonicCLAP PRIVATE /O2 /fp:fast /utf-8)
else()
    target_compile_options(TectonicCLAP PRIVATE -O3 -ffast-math -fvisibility=hidden)
endif()

if(APPLE)
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
