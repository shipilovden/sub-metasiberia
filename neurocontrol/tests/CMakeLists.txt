cmake_minimum_required(VERSION 3.20)
project(neuro_classifier_check LANGUAGES CXX)
include(CTest)

add_executable(neuro_classifier_check
    ClassifierParity.cpp
    ../processing/NeuroClassifier.cpp)
target_compile_features(neuro_classifier_check PRIVATE cxx_std_17)
target_include_directories(neuro_classifier_check PRIVATE
    ../processing ../../third_party/brainflow/third_party/json)
add_executable(neuro_classifier_safety
    ClassifierSafety.cpp
    ../processing/NeuroClassifier.cpp)
target_compile_features(neuro_classifier_safety PRIVATE cxx_std_17)
target_include_directories(neuro_classifier_safety PRIVATE ../processing)
add_executable(neuro_command_policy CommandPolicy.cpp)
target_compile_features(neuro_command_policy PRIVATE cxx_std_17)
target_include_directories(neuro_command_policy PRIVATE ../avatar)
add_executable(neuro_lsl_recording_roundtrip
    LslRecordingRoundTrip.cpp
    ../acquisition/LslRecordingFile.cpp)
target_compile_features(neuro_lsl_recording_roundtrip PRIVATE cxx_std_17)
target_include_directories(neuro_lsl_recording_roundtrip PRIVATE ../acquisition)

add_test(NAME neuro_classifier_safety COMMAND neuro_classifier_safety)
add_test(NAME neuro_command_policy COMMAND neuro_command_policy)
add_test(NAME neuro_lsl_recording_roundtrip COMMAND neuro_lsl_recording_roundtrip)

find_package(Python3 COMPONENTS Interpreter REQUIRED)
add_test(NAME neuro_mcp_contracts
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/MCPContracts.py
        ${CMAKE_CURRENT_SOURCE_DIR}/../..)
add_test(NAME neuro_mcp_protocol_probe
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/../../scripts/tests/test_mcp_protocol_probe.py)
add_test(NAME neuro_no_secret_logging
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/../../scripts/tests/test_no_secret_logging.py
        ${CMAKE_CURRENT_SOURCE_DIR}/../..)
add_test(NAME neuro_classifier_recording_smoke
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/ClassifierSmoke.py
        $<TARGET_FILE:neuro_classifier_check>
        ${CMAKE_CURRENT_SOURCE_DIR}/../models/S001_feet_vs_rest.json
        ${CMAKE_CURRENT_SOURCE_DIR}/../datasets/processed/S001R14.f32)
