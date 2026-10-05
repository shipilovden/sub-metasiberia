cmake_minimum_required(VERSION 3.16)
project(photo_mode_tests LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_AUTOMOC ON)
find_package(Qt5 REQUIRED COMPONENTS Widgets Svg Test OpenGL)
add_executable(photo_postprocess_test postprocess.cpp ../../gui_client/PhotoPostProcess.cpp)
target_link_libraries(photo_postprocess_test PRIVATE Qt5::Widgets Qt5::OpenGL)
add_executable(photo_path_test path.cpp)
add_executable(photo_frame_test frame_geometry.cpp)
target_link_libraries(photo_frame_test PRIVATE Qt5::Core)
add_executable(photo_panel_pro_test panel_pro.cpp
  ../../gui_client/LucideIconUtils.cpp
  ../../gui_client/PhotoVideoSettingsPanel.cpp
  ../../gui_client/PhotoVideoSettingsPanel.h)
target_link_libraries(photo_panel_pro_test PRIVATE Qt5::Widgets Qt5::Svg Qt5::Test)
add_executable(photo_mode_tests main.cpp
  ../../gui_client/LucideIconUtils.cpp
  ../../gui_client/PhotoVideoSettingsPanel.cpp
  ../../gui_client/PhotoVideoSettingsPanel.h)
target_link_libraries(photo_mode_tests PRIVATE Qt5::Widgets Qt5::Svg)

# Compile the actual dock helpers without MainWindow's world/network runtime.
set(PHOTO_MAIN_WINDOW "${CMAKE_CURRENT_SOURCE_DIR}/../../gui_client/MainWindow.cpp")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${PHOTO_MAIN_WINDOW}")
file(READ "${PHOTO_MAIN_WINDOW}" PHOTO_MAIN_SOURCE)
string(FIND "${PHOTO_MAIN_SOURCE}" "static void applyEditorDockTitleBarTheme(" PHOTO_TITLE_START)
string(FIND "${PHOTO_MAIN_SOURCE}" "static void dockMetasiberiaMapLikeChat(" PHOTO_TITLE_END)
if(PHOTO_TITLE_START LESS 0 OR PHOTO_TITLE_END LESS PHOTO_TITLE_START)
  message(FATAL_ERROR "Dock helper boundaries changed; update the isolated title test")
endif()
math(EXPR PHOTO_TITLE_LENGTH "${PHOTO_TITLE_END} - ${PHOTO_TITLE_START}")
string(SUBSTRING "${PHOTO_MAIN_SOURCE}" ${PHOTO_TITLE_START} ${PHOTO_TITLE_LENGTH} PHOTO_TITLE_SOURCE)
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/photo_title_helpers.h" "${PHOTO_TITLE_SOURCE}")
add_executable(photo_title_test title.cpp)
target_include_directories(photo_title_test PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
target_link_libraries(photo_title_test PRIVATE Qt5::Widgets)
if(MSVC)
  target_compile_options(photo_postprocess_test PRIVATE /utf-8)
  target_compile_options(photo_frame_test PRIVATE /utf-8)
  target_compile_options(photo_panel_pro_test PRIVATE /utf-8)
  target_compile_options(photo_mode_tests PRIVATE /utf-8)
  target_compile_options(photo_title_test PRIVATE /utf-8)
endif()

set(GLARE_CORE_DIR "" CACHE PATH "Optional glare-core source for native MP4 encoder smoke")
if(WIN32 AND GLARE_CORE_DIR)
  file(GLOB PHOTO_TEST_DOUBLE_CONVERSION ${GLARE_CORE_DIR}/double-conversion/*.cc)
  add_executable(photo_recording_test recording.cpp
    ../../gui_client/PhotoVideoRecorder.cpp
    ../../gui_client/PhotoAudioCapture.cpp
    ../../gui_client/PhotoAudioMuxer.cpp
    ../../gui_client/PhotoAudioTimeline.cpp
    ${GLARE_CORE_DIR}/video/WMFVideoWriter.cpp
    ${GLARE_CORE_DIR}/video/VideoWriter.cpp
    ${GLARE_CORE_DIR}/utils/PlatformUtils.cpp
    ${GLARE_CORE_DIR}/utils/StringUtils.cpp
    ${GLARE_CORE_DIR}/utils/FileUtils.cpp
    ${GLARE_CORE_DIR}/utils/MemMappedFile.cpp
    ${GLARE_CORE_DIR}/utils/Clock.cpp
    ${PHOTO_TEST_DOUBLE_CONVERSION})
  target_include_directories(photo_recording_test PRIVATE ${GLARE_CORE_DIR} ${GLARE_CORE_DIR}/utils)
  target_compile_definitions(photo_recording_test PRIVATE NOMINMAX _USE_MATH_DEFINES UNICODE _UNICODE COMPILE_SSE4_CODE=1)
  target_compile_options(photo_recording_test PRIVATE /utf-8 /Gy /Zc:__cplusplus)
  target_link_options(photo_recording_test PRIVATE /OPT:REF)
  target_link_libraries(photo_recording_test PRIVATE Qt5::Gui mfplat mfreadwrite mfuuid ole32 mmdevapi)
  add_executable(photo_audio_test audio_native.cpp
    ../../gui_client/PhotoVideoRecorder.cpp
    ../../gui_client/PhotoAudioCapture.cpp
    ../../gui_client/PhotoAudioMuxer.cpp
    ../../gui_client/PhotoAudioTimeline.cpp)
  target_compile_options(photo_audio_test PRIVATE /utf-8)
  target_compile_definitions(photo_audio_test PRIVATE NOMINMAX UNICODE _UNICODE)
  target_link_libraries(photo_audio_test PRIVATE Qt5::Gui mfplat mfreadwrite mfuuid ole32 mmdevapi winmm)
endif()
