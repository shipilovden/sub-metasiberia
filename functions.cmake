
# Function for checking program is on path
function(programAvailable program)
	find_program(PROGRAM_FOUND_${program} "${program}")

	if("PROGRAM_FOUND_${program}-NOTFOUND" STREQUAL "${PROGRAM_FOUND_${program}}")
		MESSAGE(FATAL_ERROR "ERROR: ${program} not found.")
	endif()
endfunction()

# Function for getting chaotica config.
function(getConfigOption config_name result)
	# Execute config.rb.
	execute_process(COMMAND "ruby" "${CMAKE_SOURCE_DIR}/scripts/config.rb" "${config_name}"
		WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}/scripts"
		OUTPUT_VARIABLE TEMP
		ERROR_VARIABLE error_var
		RESULT_VARIABLE result_var
		)

	if(NOT result_var STREQUAL "0")
		MESSAGE(FATAL_ERROR "ERROR: config.rb returned ${result_var}:  ${TEMP}, ${error_var}.")
	endif()

	# Remove newlines.
	string(REPLACE "\n" "" TEMP ${TEMP})

	# Set result.
	set (${result} ${TEMP} PARENT_SCOPE)
endfunction()


function(convertToForwardSlashes str result)
	string(REPLACE "\\" "/" var str)
	set (${result} ${var} PARENT_SCOPE)
endfunction()

# Function for checking an environment variable exists
function(checkAndGetEnvVar envvar result)
	if(DEFINED ENV{${envvar}})
		# To prevent issues with \ characters in macros, replace possible \ INDIGO_LIBS env vars and store it in INDIGO_LIBS_ENV.
		string(REPLACE "\\" "/" var $ENV{${envvar}})
		set (${result} ${var} PARENT_SCOPE)
	else()
		MESSAGE(FATAL_ERROR "ERROR: Environment variable ${envvar} not defined.")
	endif()
endfunction()

# Function for adding a rebuild-mocfile command/dependency
function(addMocFileRule dir header_name)
	set(MOC_QT6_DEFINE 0)
	if(INDIGO_QT_VERSION MATCHES "^6(\\.|$)")
		set(MOC_QT6_DEFINE 1)
	endif()
	set(MOC_OUTPUT_DIR "${CMAKE_BINARY_DIR}/${dir}")
	set(MOC_OUTPUT_FILE "${MOC_OUTPUT_DIR}/moc_${header_name}.cpp")

	add_custom_command(
		OUTPUT "${MOC_OUTPUT_FILE}"
		COMMAND "${CMAKE_COMMAND}" -E make_directory "${MOC_OUTPUT_DIR}"
		COMMAND "${INDIGO_QT_DIR}/bin/moc" "-DSUBSTRATA_QT6=${MOC_QT6_DEFINE}" -o "${MOC_OUTPUT_FILE}" "${CMAKE_SOURCE_DIR}/${dir}/${header_name}.h"
		DEPENDS ${CMAKE_SOURCE_DIR}/${dir}/${header_name}.h
		VERBATIM
	)
endfunction()


# Function for adding a rebuild- QT ui header file command/dependency
function(addUIFileRule dir header_name)
	set(UI_OUTPUT_DIR "${CMAKE_BINARY_DIR}/${dir}")
	set(UI_OUTPUT_FILE "${UI_OUTPUT_DIR}/ui_${header_name}.h")

	add_custom_command(
		OUTPUT "${UI_OUTPUT_FILE}"
		COMMAND "${CMAKE_COMMAND}" -E make_directory "${UI_OUTPUT_DIR}"
		COMMAND "${INDIGO_QT_DIR}/bin/uic" "${CMAKE_SOURCE_DIR}/${dir}/${header_name}.ui" --output "${UI_OUTPUT_FILE}"
		DEPENDS ${CMAKE_SOURCE_DIR}/${dir}/${header_name}.ui
		VERBATIM
	)
endfunction()
