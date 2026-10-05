cmake_minimum_required(VERSION 3.16)
project(animation_editor_tests LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_AUTOMOC ON)
find_package(Qt5 REQUIRED COMPONENTS Core Widgets Svg Test)
enable_testing()
add_executable(animation_panel_test panel.cpp ../../gui_client/AnimationEditorPanel.cpp ../../gui_client/AnimationEditorPanel.h ../../gui_client/LucideIconUtils.cpp)
target_link_libraries(animation_panel_test PRIVATE Qt5::Widgets Qt5::Svg Qt5::Test)
add_test(NAME panel COMMAND animation_panel_test)

# Build only the importer's CPU dependencies; no client entry point, GL or network.
set(GLARE_CORE_DIR "C:/programming/glare-core" CACHE PATH "Engine source headers")
set(NATIVE_OUTPUT_DIR "C:/programming/substrata_output_qt/vs2022/cyberspace_x64/RelWithDebInfo" CACHE PATH "Native libraries")
add_executable(animation_import_test import.cpp interpolation.cpp orientation.cpp vrm_object.cpp ../../gui_client/AnimationImport.cpp
  ../../gui_client/GestureAnimationRetarget.cpp
  ../../gui_client/AnimationFBXImport.cpp ../../gui_client/AnimationGLBTracks.cpp
  ../../third_party/ufbx/ufbx.c ../../shared/GestureSettings.cpp)
target_include_directories(animation_import_test PRIVATE ${GLARE_CORE_DIR} ${GLARE_CORE_DIR}/utils
  ${GLARE_CORE_DIR}/tracy/public ${GLARE_CORE_DIR}/zstd/lib/common ${GLARE_CORE_DIR}/zstd/lib
  ${GLARE_CORE_DIR}/Imath/src/Imath ${GLARE_CORE_DIR}/Imath/config_windows)
target_compile_definitions(animation_import_test PRIVATE NOMINMAX _USE_MATH_DEFINES UNICODE _UNICODE COMPILE_SSE4_CODE=1)
set_source_files_properties(../../third_party/ufbx/ufbx.c PROPERTIES COMPILE_DEFINITIONS "UFBX_MINIMAL;UFBX_NO_STDIO")
target_link_libraries(animation_import_test PRIVATE Qt5::Core "${NATIVE_OUTPUT_DIR}/libs.lib" ws2_32 Iphlpapi)
foreach(unit graphics/AnimationData graphics/FormatDecoderGLTF graphics/BatchedMesh maths/Matrix4f
  utils/MemAlloc utils/StringUtils utils/RuntimeCheck utils/ConPrint utils/Clock utils/Timer
  utils/FileInStream utils/FileOutStream utils/BufferViewInStream utils/BufferOutStream utils/BufferInStream
  utils/MemMappedFile utils/FileUtils utils/PlatformUtils utils/JSONParser utils/Parser utils/Base64
  utils/InStream utils/OutStream utils/Lock utils/Mutex utils/UTF8Utils dll/IndigoMesh dll/IndigoAllocation)
  target_sources(animation_import_test PRIVATE "${GLARE_CORE_DIR}/${unit}.cpp")
endforeach()
file(GLOB ANIMATION_MESHOPT "${GLARE_CORE_DIR}/meshoptimizer/src/*.cpp")
file(GLOB ANIMATION_DOUBLE_CONVERSION "${GLARE_CORE_DIR}/double-conversion/*.cc")
target_sources(animation_import_test PRIVATE ${ANIMATION_MESHOPT} ${ANIMATION_DOUBLE_CONVERSION})
if(MSVC)
  target_compile_options(animation_import_test PRIVATE /utf-8 /Gy /Zc:__cplusplus)
  target_compile_options(animation_panel_test PRIVATE /utf-8)
  target_link_options(animation_import_test PRIVATE /OPT:REF)
endif()
add_test(NAME import COMMAND animation_import_test "${CMAKE_CURRENT_SOURCE_DIR}/../../resources/animations")
add_test(NAME vrm_object COMMAND animation_import_test --vrm-object-only "${CMAKE_CURRENT_SOURCE_DIR}/../../resources/animations/Idle.subanim")
