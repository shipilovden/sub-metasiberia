# Script to fix MOC files for Qt 5 - replace QOpenGLWidget with QGLWidget
# Usage: cmake -P fix_moc_qt5.cmake <input_file> <output_file>

# CMake script mode: arguments are passed via CMAKE_ARGC and CMAKE_ARGV
# CMAKE_ARGV0 is the script path, CMAKE_ARGV1 is first argument, etc.
if(CMAKE_ARGC LESS 3)
	message(FATAL_ERROR "Usage: cmake -P fix_moc_qt5.cmake <input_file> <output_file>. Got ${CMAKE_ARGC} arguments")
endif()

set(INPUT_FILE "${CMAKE_ARGV1}")
set(OUTPUT_FILE "${CMAKE_ARGV2}")

if(NOT EXISTS "${INPUT_FILE}")
	message(FATAL_ERROR "Input file does not exist: ${INPUT_FILE}")
endif()

file(READ "${INPUT_FILE}" CONTENT)

# Replace QOpenGLWidget with QGLWidget in the MOC file
string(REPLACE "QOpenGLWidget" "QGLWidget" FIXED_CONTENT "${CONTENT}")

# Write the fixed content
file(WRITE "${OUTPUT_FILE}" "${FIXED_CONTENT}")
