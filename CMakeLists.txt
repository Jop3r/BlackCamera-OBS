cmake_minimum_required(VERSION 3.25)

project(BlackCameraOBS VERSION 0.1.0 DESCRIPTION "BlackCamera ASCII video filter for OBS Studio" LANGUAGES CXX)

if(NOT WIN32)
    message(FATAL_ERROR "BlackCamera OBS is Windows-only (Direct2D/DirectWrite glyph atlas)")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(OpenCV CONFIG REQUIRED COMPONENTS core imgproc objdetect dnn)

# ── libobs ──────────────────────────────────────────────────────────────────
# Either a real CMake package (-Dlibobs_DIR=..., e.g. an obs-studio build or
# obs-plugintemplate's .deps) or the minimal SDK made by tools/obs-sdk.ps1
# (headers of the installed OBS version + import library of its obs.dll).
find_package(libobs CONFIG QUIET)
if(NOT TARGET OBS::libobs)
    set(OBS_SDK_DIR "${PROJECT_SOURCE_DIR}/.deps/obs-sdk" CACHE PATH "Minimal libobs SDK made by tools/obs-sdk.ps1")
    if(NOT EXISTS "${OBS_SDK_DIR}/lib/obs.lib" OR NOT EXISTS "${OBS_SDK_DIR}/include/obs-module.h")
        message(FATAL_ERROR "libobs not found. Run: powershell -ExecutionPolicy Bypass -File tools\\obs-sdk.ps1 "
                            "(or pass -Dlibobs_DIR=<libobs CMake package>)")
    endif()
    file(READ "${OBS_SDK_DIR}/version.txt" OBS_SDK_VERSION)
    string(STRIP "${OBS_SDK_VERSION}" OBS_SDK_VERSION)
    message(STATUS "libobs SDK ${OBS_SDK_VERSION} from ${OBS_SDK_DIR}")
    add_library(OBS::libobs UNKNOWN IMPORTED)
    set_target_properties(OBS::libobs PROPERTIES
        IMPORTED_LOCATION "${OBS_SDK_DIR}/lib/obs.lib"
        INTERFACE_INCLUDE_DIRECTORIES "${OBS_SDK_DIR}/include"
    )
endif()

# ── The effect: settings, face tracking, ASCII renderer ─────────────────────
set(CORE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/src/core)
add_library(blackcamera_effect STATIC
    ${CORE_DIR}/platform.cpp
    ${CORE_DIR}/log.cpp
    ${CORE_DIR}/settings.cpp
    ${CORE_DIR}/face_tracker.cpp
    ${CORE_DIR}/ascii_renderer.cpp
)
target_include_directories(blackcamera_effect PUBLIC ${CORE_DIR})
target_compile_definitions(blackcamera_effect PUBLIC WIN32_LEAN_AND_MEAN NOMINMAX UNICODE _UNICODE)
target_compile_options(blackcamera_effect PUBLIC /utf-8 /W3 /permissive-)
target_link_libraries(blackcamera_effect PUBLIC
    opencv_core opencv_imgproc opencv_objdetect
    d2d1 dwrite windowscodecs ole32
)

# ── The plugin ──────────────────────────────────────────────────────────────
set(PLUGIN_NAME blackcamera-obs)
add_library(${PLUGIN_NAME} MODULE
    src/plugin-main.cpp
    src/ascii_filter.cpp
    src/ascii_filter.h
)
# vcpkg's post-build step copies the OpenCV DLLs (with their own dependencies, e.g. dnn) next to the plugin
target_link_libraries(${PLUGIN_NAME} PRIVATE blackcamera_effect OBS::libobs)
target_compile_definitions(${PLUGIN_NAME} PRIVATE
    PLUGIN_NAME="${PLUGIN_NAME}"
    PLUGIN_VERSION="${PROJECT_VERSION}"
)

# Staged in the layout OBS loads from %ProgramData%\obs-studio\plugins:
#   <stage>\blackcamera-obs\bin\64bit\blackcamera-obs.dll (+ OpenCV DLLs)
#   <stage>\blackcamera-obs\data\{locale\*.ini, face_detection_yunet_2023mar.onnx}
set(PLUGIN_STAGE "${CMAKE_BINARY_DIR}/stage/$<CONFIG>/${PLUGIN_NAME}")
set_target_properties(${PLUGIN_NAME} PROPERTIES
    PREFIX ""
    LIBRARY_OUTPUT_DIRECTORY "${PLUGIN_STAGE}/bin/64bit"
)
add_custom_command(TARGET ${PLUGIN_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_CURRENT_SOURCE_DIR}/data" "${PLUGIN_STAGE}/data"
    COMMENT "Staging the plugin data"
)

# cmake --install build --config Release --prefix "%ProgramData%\obs-studio\plugins"
install(DIRECTORY "${CMAKE_BINARY_DIR}/stage/$<CONFIG>/${PLUGIN_NAME}" DESTINATION .)
