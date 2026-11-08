cmake_minimum_required(VERSION 3.21)
project(BreeferComposer VERSION 0.3.0 LANGUAGES C CXX
        DESCRIPTION "Free, open-source non-linear video editor"
        HOMEPAGE_URL "https://github.com/breefer/breefer-composer")

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release)
endif()

option(BREEFER_BUILD_TESTS "Build the headless unit tests" ON)
option(BREEFER_BUILD_SAMPLE_PLUGINS "Build the sample native effect plugins" ON)
option(BREEFER_DEPLOY_RUNTIME "Copy Qt/FFmpeg runtime next to the executable after build (Windows)" ON)

# Target the x86-64 baseline (SSE2) so the app runs on 2nd-gen Core i3/i5 and
# newer; FFmpeg picks faster SIMD paths at runtime.
if(CMAKE_SYSTEM_PROCESSOR MATCHES "AMD64|x86_64" AND (CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang"))
    add_compile_options(-march=x86-64 -mtune=generic)
endif()

# ---------------------------------------------------------------- dependencies
# Qt: pass -DCMAKE_PREFIX_PATH=<qt>/lib/cmake or -DQT_ROOT=<qt>.
set(QT_ROOT "" CACHE PATH "Qt installation (optional, otherwise CMAKE_PREFIX_PATH)")
if(QT_ROOT)
    list(APPEND CMAKE_PREFIX_PATH "${QT_ROOT}")
elseif(WIN32 AND EXISTS "C:/Qt/6.12.0/mingw_64")
    set(QT_ROOT "C:/Qt/6.12.0/mingw_64")
    list(APPEND CMAKE_PREFIX_PATH "${QT_ROOT}")
endif()
find_package(Qt6 6.9 REQUIRED COMPONENTS Core Gui Widgets)

# FFmpeg 7.1+ (libavcodec 61+): either a prebuilt shared folder (FFMPEG_ROOT
# with include/ lib/ bin/) or pkg-config.
set(FFMPEG_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/.toolchain/ffmpeg/ffmpeg-master-latest-win64-lgpl-shared"
    CACHE PATH "Prebuilt shared FFmpeg (include/, lib/, bin/)")
set(FFMPEG_COMPONENTS avformat avcodec avutil swscale swresample)
if(EXISTS "${FFMPEG_ROOT}/include/libavcodec/avcodec.h")
    add_library(ffmpeg INTERFACE)
    target_include_directories(ffmpeg INTERFACE "${FFMPEG_ROOT}/include")
    foreach(component ${FFMPEG_COMPONENTS})
        find_library(FFMPEG_${component}_LIB NAMES ${component} lib${component}
                     PATHS "${FFMPEG_ROOT}/lib" NO_DEFAULT_PATH REQUIRED)
        target_link_libraries(ffmpeg INTERFACE "${FFMPEG_${component}_LIB}")
    endforeach()
    set(FFMPEG_BIN "${FFMPEG_ROOT}/bin")
    message(STATUS "FFmpeg: prebuilt at ${FFMPEG_ROOT}")
else()
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(FFMPEG_PC REQUIRED IMPORTED_TARGET
        libavformat libavcodec>=61 libavutil libswscale libswresample)
    add_library(ffmpeg INTERFACE)
    target_link_libraries(ffmpeg INTERFACE PkgConfig::FFMPEG_PC)
    message(STATUS "FFmpeg: pkg-config ${FFMPEG_PC_libavcodec_VERSION}")
endif()

# ------------------------------------------------------------ headless core
# Timeline model, edit engine, evaluation and interchange. No GUI, no FFmpeg:
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
    src/io/Inflate.cpp
    src/io/PremiereImport.cpp
)
target_include_directories(breefer_core PUBLIC src)
target_link_libraries(breefer_core PUBLIC Qt6::Core Qt6::Gui)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(breefer_core PRIVATE -Wall -Wextra -Wno-unused-parameter)
endif()

if(BREEFER_BUILD_TESTS)
    enable_testing()
    add_executable(breefer_tests tests/test_core.cpp)
    target_link_libraries(breefer_tests PRIVATE breefer_core)
    target_compile_definitions(breefer_tests PRIVATE BC_TEST_DATA="${CMAKE_CURRENT_SOURCE_DIR}/tests/data")
    if(MINGW)
        target_link_options(breefer_tests PRIVATE -static-libgcc -static-libstdc++)
    endif()
    add_test(NAME core COMMAND breefer_tests)
endif()

# ------------------------------------------------------------------ the app
set(BREEFER_SOURCES
    src/main.cpp

    src/media/MediaProbe.cpp
    src/media/Decoder.cpp
    src/media/AudioEngine.cpp
    src/media/WaveformCache.cpp
    src/media/ProxyManager.cpp
    src/media/Exporter.cpp
    src/media/AudioMix.cpp
    src/media/ThumbnailCache.cpp
    src/media/RemotionBridge.cpp

    src/engine/Compositor.cpp
    src/engine/Effects.cpp
    src/engine/PlaybackEngine.cpp
    src/engine/Model3D.cpp
    src/engine/PluginHost.cpp

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

if(APPLE)
    add_executable(breefer MACOSX_BUNDLE ${BREEFER_SOURCES})
    set_target_properties(breefer PROPERTIES
        OUTPUT_NAME "Breefer Composer"
        MACOSX_BUNDLE_BUNDLE_NAME "Breefer Composer"
        MACOSX_BUNDLE_GUI_IDENTIFIER "org.breefer.composer"
        MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}")
elseif(WIN32)
    add_executable(breefer WIN32 ${BREEFER_SOURCES})
else()
    add_executable(breefer ${BREEFER_SOURCES})
endif()

target_include_directories(breefer PRIVATE src third_party/miniaudio sdk/include)
target_compile_definitions(breefer PRIVATE BREEFER_VERSION="${PROJECT_VERSION}")
target_link_libraries(breefer PRIVATE breefer_core Qt6::Core Qt6::Gui Qt6::Widgets ffmpeg)

if(WIN32)
    target_compile_definitions(breefer PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN UNICODE _UNICODE)
    target_link_libraries(breefer PRIVATE ole32 uuid winmm user32 gdi32 shlwapi version dwmapi)
elseif(APPLE)
    target_link_libraries(breefer PRIVATE "-framework CoreFoundation" "-framework CoreAudio" "-framework AudioToolbox")
else()
    find_package(Threads REQUIRED)
    target_link_libraries(breefer PRIVATE Threads::Threads ${CMAKE_DL_LIBS} m)
endif()

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(breefer PRIVATE -Wall -Wextra -Wno-unused-parameter)
endif()
if(MINGW)
    target_link_options(breefer PRIVATE -static-libgcc -static-libstdc++)
endif()

# ----------------------------------------------------------- sample plugins
if(BREEFER_BUILD_SAMPLE_PLUGINS)
    add_library(breefer_stylize MODULE sdk/samples/stylize/stylize.c)
    add_library(breefer_tint MODULE sdk/samples/tint/tint.cpp)
    foreach(plugin breefer_stylize breefer_tint)
        target_include_directories(${plugin} PRIVATE sdk/include)
        set_target_properties(${plugin} PROPERTIES PREFIX "" C_VISIBILITY_PRESET hidden CXX_VISIBILITY_PRESET hidden
                              AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)
        if(APPLE)
            set_target_properties(${plugin} PROPERTIES
                LIBRARY_OUTPUT_DIRECTORY "$<TARGET_BUNDLE_CONTENT_DIR:breefer>/MacOS/plugins")
        else()
            set_target_properties(${plugin} PROPERTIES
                LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/plugins"
                RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/plugins")
        endif()
        if(NOT WIN32)
            target_link_libraries(${plugin} PRIVATE m)
        endif()
        if(MINGW)
            target_link_options(${plugin} PRIVATE -static-libgcc -static-libstdc++)
        endif()
        add_dependencies(breefer ${plugin})
    endforeach()
endif()

# ------------------------------------------------- Windows runtime staging
if(WIN32 AND BREEFER_DEPLOY_RUNTIME)
    if(FFMPEG_BIN AND EXISTS "${FFMPEG_BIN}")
        file(GLOB FFMPEG_DLLS "${FFMPEG_BIN}/*.dll")
        foreach(dll ${FFMPEG_DLLS})
            add_custom_command(TARGET breefer POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different "${dll}" "$<TARGET_FILE_DIR:breefer>"
                VERBATIM)
        endforeach()
    endif()
    set(QT_BIN_DIR "${QT_ROOT}/bin")
    if(TARGET Qt6::qmake)
        get_target_property(QMAKE_EXE Qt6::qmake IMPORTED_LOCATION)
        get_filename_component(QT_BIN_DIR "${QMAKE_EXE}" DIRECTORY)
    endif()
    find_program(WINDEPLOYQT windeployqt HINTS "${QT_BIN_DIR}")
    if(WINDEPLOYQT)
        add_custom_command(TARGET breefer POST_BUILD
            COMMAND "${WINDEPLOYQT}" --no-translations --no-system-d3d-compiler --no-opengl-sw
                    --no-compiler-runtime "$<TARGET_FILE:breefer>"
            VERBATIM)
    endif()
    if(MINGW)
        get_filename_component(MINGW_BIN "${CMAKE_CXX_COMPILER}" DIRECTORY)
        foreach(rt libwinpthread-1.dll libgcc_s_seh-1.dll libstdc++-6.dll)
            if(EXISTS "${MINGW_BIN}/${rt}")
                add_custom_command(TARGET breefer POST_BUILD
                    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${MINGW_BIN}/${rt}" "$<TARGET_FILE_DIR:breefer>"
                    VERBATIM)
            endif()
        endforeach()
    endif()
endif()

# ------------------------------------------------------------------ install
include(GNUInstallDirs)
if(APPLE)
    install(TARGETS breefer BUNDLE DESTINATION .)
elseif(WIN32)
    install(TARGETS breefer RUNTIME DESTINATION .)
    # Runtime DLLs staged by windeployqt / the FFmpeg copy step, plus plugins.
    install(DIRECTORY "${CMAKE_BINARY_DIR}/" DESTINATION .
            FILES_MATCHING PATTERN "*.dll"
            PATTERN "CMakeFiles" EXCLUDE PATTERN "_CPack_Packages" EXCLUDE PATTERN "Testing" EXCLUDE)
else()
    install(TARGETS breefer RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
    if(BREEFER_BUILD_SAMPLE_PLUGINS)
        install(TARGETS breefer_stylize breefer_tint LIBRARY DESTINATION ${CMAKE_INSTALL_BINDIR}/plugins)
    endif()
    install(FILES packaging/linux/org.breefer.Composer.desktop DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
    install(FILES packaging/linux/org.breefer.Composer.svg DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps)
endif()
if(WIN32 OR APPLE)
    install(DIRECTORY sdk/ DESTINATION sdk)
else()
    install(DIRECTORY sdk/ DESTINATION ${CMAKE_INSTALL_DATADIR}/breefer/sdk)
endif()

# ------------------------------------------------------------------ packaging
set(CPACK_PACKAGE_NAME "BreeferComposer")
set(CPACK_PACKAGE_VENDOR "Breefer")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "${PROJECT_DESCRIPTION}")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE")
set(CPACK_PACKAGE_INSTALL_DIRECTORY "Breefer Composer")
set(CPACK_PACKAGE_EXECUTABLES "breefer;Breefer Composer")
if(WIN32)
    set(CPACK_GENERATOR "ZIP;NSIS")
    set(CPACK_NSIS_DISPLAY_NAME "Breefer Composer")
    set(CPACK_NSIS_ENABLE_UNINSTALL_BEFORE_INSTALL ON)
elseif(APPLE)
    set(CPACK_GENERATOR "DragNDrop")
else()
    set(CPACK_GENERATOR "TGZ")
endif()
include(CPack)
