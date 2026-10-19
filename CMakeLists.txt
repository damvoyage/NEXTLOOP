cmake_minimum_required(VERSION 3.22)

project(NEXTLOOP VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(APPLE)
    # Universal binary (Apple Silicon + Intel) unless the caller overrides it
    if(NOT CMAKE_OSX_ARCHITECTURES)
        set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "" FORCE)
    endif()
    set(CMAKE_OSX_DEPLOYMENT_TARGET "10.13" CACHE STRING "" FORCE)
endif()

if(MSVC)
    # static runtime so the .exe runs without installing the VC++ redistributable
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

# JUCE: use a local checkout if present, otherwise download the pinned version.
# JUCE 9 needs Windows 10 (1607+). The Windows 7 build uses JUCE 7.0.12.
set(LOOPDECK_JUCE_VERSION "9.0.4" CACHE STRING "JUCE version to download (7.0.12 for Windows 7 builds)")
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/JUCE/CMakeLists.txt AND LOOPDECK_JUCE_VERSION STREQUAL "9.0.4")
    add_subdirectory(JUCE)
else()
    include(FetchContent)
    FetchContent_Declare(JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG ${LOOPDECK_JUCE_VERSION}
        GIT_SHALLOW ON)
    FetchContent_MakeAvailable(JUCE)
endif()

option(LOOPDECK_BUILD_TESTS "Build the headless engine test runner" ON)
option(LOOPDECK_BUILD_APP "Build the NEXTLOOP application" ON)

# ---------------------------------------------------------------------------
# Engine sources (shared by the app and the test runner)
# ---------------------------------------------------------------------------
set(LOOPDECK_ENGINE_SOURCES
    Source/engine/Track.cpp
    Source/engine/TrackLoader.cpp
    Source/engine/Analysis.cpp
    Source/engine/TimeStretch.cpp
    Source/engine/Deck.cpp
    Source/engine/Mixer.cpp
    Source/engine/AudioEngine.cpp
)

set(LOOPDECK_UI_SOURCES
    Source/Main.cpp
    Source/ui/LookAndFeel.cpp
    Source/ui/WaveformView.cpp
    Source/ui/DeckPanel.cpp
    Source/ui/MixerPanel.cpp
    Source/ui/FileBrowserPanel.cpp
    Source/ui/SettingsComponent.cpp
    Source/ui/MainComponent.cpp
)

set(LOOPDECK_SYSTEM_INCLUDES
    ${CMAKE_CURRENT_SOURCE_DIR}/ThirdParty/signalsmith-linear/include
    ${CMAKE_CURRENT_SOURCE_DIR}/ThirdParty/signalsmith-stretch/include
)


set(LOOPDECK_THIRDPARTY_INCLUDES ${CMAKE_CURRENT_SOURCE_DIR}/Source)

set(LOOPDECK_COMMON_DEFS
    JUCE_USE_FLAC=1
    JUCE_USE_OGGVORBIS=1
    JUCE_USE_MP3AUDIOFORMAT=1
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
    JUCE_REPORT_APP_USAGE=0
    JUCE_STRICT_REFCOUNTEDPOINTER=1
)

if(WIN32)
    list(APPEND LOOPDECK_COMMON_DEFS JUCE_ASIO=1 JUCE_WASAPI=1 JUCE_DIRECTSOUND=1)
    if(LOOPDECK_JUCE_VERSION VERSION_LESS 8)
        # JUCE 7 doesn't bundle the ASIO SDK headers; use the copy in ThirdParty/asio (GPLv3)
        list(APPEND LOOPDECK_SYSTEM_INCLUDES ${CMAKE_CURRENT_SOURCE_DIR}/ThirdParty/asio)
    endif()
elseif(UNIX AND NOT APPLE)
    # Linux is only used for development/testing builds. Allow building
    # without the optional X11 extension and ALSA headers.
    list(APPEND LOOPDECK_COMMON_DEFS
        JUCE_USE_XRANDR=0 JUCE_USE_XINERAMA=0 JUCE_USE_XRENDER=0
        JUCE_USE_XCURSOR=0 JUCE_USE_XINPUT=0 JUCE_USE_XSHM=1)
    if(NOT LOOPDECK_LINUX_ALSA)
        list(APPEND LOOPDECK_COMMON_DEFS JUCE_ALSA=0 JUCE_JACK=0)
    endif()
endif()

set(LOOPDECK_JUCE_MODULES
    juce::juce_audio_basics
    juce::juce_audio_devices
    juce::juce_audio_formats
    juce::juce_audio_utils
    juce::juce_core
    juce::juce_cryptography
    juce::juce_data_structures
    juce::juce_dsp
    juce::juce_events
    juce::juce_graphics
    juce::juce_gui_basics
    juce::juce_gui_extra
)

# ---------------------------------------------------------------------------
# The application
# ---------------------------------------------------------------------------
if(LOOPDECK_BUILD_APP)
juce_add_gui_app(NEXTLOOP
    PRODUCT_NAME "NEXTLOOP"
    COMPANY_NAME "NEXTLOOP"
    BUNDLE_ID "com.nextloop.app"
    VERSION ${PROJECT_VERSION}
    MICROPHONE_PERMISSION_ENABLED TRUE
    MICROPHONE_PERMISSION_TEXT "NEXTLOOP uses your audio interface inputs for recording."
)

target_sources(NEXTLOOP PRIVATE ${LOOPDECK_ENGINE_SOURCES} ${LOOPDECK_UI_SOURCES})
target_include_directories(NEXTLOOP PRIVATE ${LOOPDECK_THIRDPARTY_INCLUDES})
    target_include_directories(NEXTLOOP SYSTEM PRIVATE ${LOOPDECK_SYSTEM_INCLUDES})
target_compile_definitions(NEXTLOOP PRIVATE ${LOOPDECK_COMMON_DEFS} JUCE_MODAL_LOOPS_PERMITTED=1
    JUCE_APPLICATION_NAME_STRING="$<TARGET_PROPERTY:NEXTLOOP,JUCE_PRODUCT_NAME>"
    JUCE_APPLICATION_VERSION_STRING="$<TARGET_PROPERTY:NEXTLOOP,JUCE_VERSION>")
target_link_libraries(NEXTLOOP PRIVATE ${LOOPDECK_JUCE_MODULES}
    PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)
endif()

# ---------------------------------------------------------------------------
# Headless test runner for the audio engine
# ---------------------------------------------------------------------------
if(LOOPDECK_BUILD_TESTS)
    juce_add_console_app(NEXTLOOPTests PRODUCT_NAME "NEXTLOOPTests")
    target_sources(NEXTLOOPTests PRIVATE ${LOOPDECK_ENGINE_SOURCES} Tests/EngineTests.cpp)
    target_include_directories(NEXTLOOPTests PRIVATE ${LOOPDECK_THIRDPARTY_INCLUDES})
    target_include_directories(NEXTLOOPTests SYSTEM PRIVATE ${LOOPDECK_SYSTEM_INCLUDES})
    target_compile_definitions(NEXTLOOPTests PRIVATE ${LOOPDECK_COMMON_DEFS} JUCE_MODAL_LOOPS_PERMITTED=1)
    target_link_libraries(NEXTLOOPTests PRIVATE
        juce::juce_audio_basics juce::juce_audio_devices juce::juce_audio_formats
        juce::juce_core juce::juce_cryptography juce::juce_data_structures juce::juce_dsp juce::juce_events
        PUBLIC juce::juce_recommended_config_flags)
endif()
