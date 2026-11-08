cmake_minimum_required(VERSION 3.21)
project(BreeferComposer VERSION 0.2.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

# ---------------------------------------------------------------- dependencies
set(QT_ROOT "C:/Qt/6.12.0/mingw_64" CACHE PATH "Qt installation")
set(FFMPEG_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/.toolchain/ffmpeg/ffmpeg-master-latest-win64-lgpl-shared"
    CACHE PATH "FFmpeg shared (LGPL) build")

list(APPEND CMAKE_PREFIX_PATH "${QT_ROOT}")
find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets)

set(FFMPEG_INCLUDE "${FFMPEG_ROOT}/include")
set(FFMPEG_LIB     "${FFMPEG_ROOT}/lib")
set(FFMPEG_BIN     "${FFMPEG_ROOT}/bin")

if(NOT EXISTS "${FFMPEG_INCLUDE}/libavcodec/avcodec.h")
    message(FATAL_ERROR "FFmpeg headers not found at ${FFMPEG_INCLUDE}")
endif()

# ------------------------------------------------------------ headless core
# Timeline model, edit engine, evaluation and project IO. No GUI, no FFmpeg:
# unit-testable on its own.
add_library(breefer_core STATIC
    src/core/Project.cpp
    src/core/Commands.cpp
    src/core/Document.cpp
    src/core/EditOps.cpp
    src/core/Evaluate.cpp
    src/io/ProjectIO.cpp
    src/io/Interchange.cpp
    src/io/InterchangeImport.cpp
)
target_include_directories(breefer_core PUBLIC src)
target_link_libraries(breefer_core PUBLIC Qt6::Core Qt6::Gui)
if(MINGW)
    target_compile_options(breefer_core PRIVATE -Wall -Wextra -Wno-unused-parameter)
endif()

enable_testing()
add_executable(breefer_tests tests/test_core.cpp)
target_link_libraries(breefer_tests PRIVATE breefer_core)
if(MINGW)
    target_link_options(breefer_tests PRIVATE -static-libgcc -static-libstdc++)
endif()
add_test(NAME core COMMAND breefer_tests)

# ---------------------------------------------------------------------- sources
add_executable(breefer
    src/main.cpp

    src/media/MediaProbe.cpp
    src/media/Decoder.cpp
    src/media/AudioEngine.cpp
    src/media/WaveformCache.cpp
    src/media/ProxyManager.cpp
    src/media/Exporter.cpp
    src/media/AudioMix.cpp
    src/media/ThumbnailCache.cpp

    src/engine/Compositor.cpp
    src/engine/Effects.cpp
    src/engine/PlaybackEngine.cpp

    src/ui/Theme.cpp
    src/ui/Icons.cpp
    src/ui/ColorPanels.cpp
    src/ui/EffectsPanel.cpp
    src/ui/StartDialog.cpp
    src/ui/MainWindow.cpp
    src/ui/TimelineWidget.cpp
    src/ui/MonitorWidget.cpp
    src/ui/MediaBin.cpp
    src/ui/EffectControls.cpp
    src/ui/AudioMeters.cpp
    src/ui/Dialogs.cpp
    src/ui/Panels.cpp
    src/ui/Widgets.cpp
)

target_include_directories(breefer PRIVATE
    src
    third_party/miniaudio
    ${FFMPEG_INCLUDE}
)

target_compile_definitions(breefer PRIVATE
    NOMINMAX
    WIN32_LEAN_AND_MEAN
    UNICODE
    _UNICODE
)

if(MINGW)
    target_compile_options(breefer PRIVATE -Wall -Wextra -Wno-unused-parameter)
    target_link_options(breefer PRIVATE -static-libgcc -static-libstdc++)
endif()

target_link_libraries(breefer PRIVATE
    breefer_core
    Qt6::Core Qt6::Gui Qt6::Widgets
    "${FFMPEG_LIB}/libavformat.dll.a"
    "${FFMPEG_LIB}/libavcodec.dll.a"
    "${FFMPEG_LIB}/libavutil.dll.a"
    "${FFMPEG_LIB}/libswscale.dll.a"
    "${FFMPEG_LIB}/libswresample.dll.a"
    ole32 uuid winmm user32 gdi32 shlwapi version dwmapi
)

# ------------------------------------------------------------- runtime staging
foreach(dll avformat-63.dll avcodec-63.dll avutil-61.dll swscale-10.dll swresample-7.dll)
    add_custom_command(TARGET breefer POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${FFMPEG_BIN}/${dll}" "$<TARGET_FILE_DIR:breefer>/${dll}"
        VERBATIM)
endforeach()

# Qt runtime + platform plugins, laid out the way Qt expects next to the exe.
add_custom_command(TARGET breefer POST_BUILD
    COMMAND "${QT_ROOT}/bin/windeployqt.exe"
        --no-translations --no-system-d3d-compiler --no-opengl-sw --no-compiler-runtime
        "$<TARGET_FILE:breefer>"
    VERBATIM)

add_custom_command(TARGET breefer POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "C:/Qt/Tools/mingw1310_64/bin/libwinpthread-1.dll"
        "C:/Qt/Tools/mingw1310_64/bin/libgcc_s_seh-1.dll"
        "C:/Qt/Tools/mingw1310_64/bin/libstdc++-6.dll"
        "$<TARGET_FILE_DIR:breefer>/"
    VERBATIM)
