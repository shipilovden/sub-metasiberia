# Installer application using CEF for web-based UI

if(CEF_SUPPORT AND DEFINED cef_binary_dist_dir)
    set(INSTALLER_TARGET installer_app)
    
    add_executable(${INSTALLER_TARGET}
        InstallerApp.cpp
        InstallerApp.h
    )
    
    # Set C++ standard to C++17
    set_target_properties(${INSTALLER_TARGET} PROPERTIES
        CXX_STANDARD 17
        CXX_STANDARD_REQUIRED ON
        CXX_EXTENSIONS OFF
    )
    
    # Set entry point to WinMain for Windows
    if(WIN32)
        set_target_properties(${INSTALLER_TARGET} PROPERTIES
            LINK_FLAGS "/ENTRY:WinMainCRTStartup"
        )
    endif()
    
    # Include CEF headers
    include_directories(${cef_binary_dist_dir}/include)
    include_directories(${cef_binary_dist_dir})
    
    # Link CEF libraries
    target_link_libraries(${INSTALLER_TARGET}
        debug ${cef_binary_dist_dir}/Debug/libcef.lib
        optimized ${cef_binary_dist_dir}/Release/libcef.lib
        debug ${cef_binary_dist_dir}/libcef_dll_build/libcef_dll_wrapper/Debug/libcef_dll_wrapper.lib
        optimized ${cef_binary_dist_dir}/libcef_dll_build/libcef_dll_wrapper/Release/libcef_dll_wrapper.lib
    )
    
    # Copy CEF files
    set(CEF_BIN_DIR "$<IF:$<CONFIG:Debug>,${cef_binary_dist_dir}/Debug,${cef_binary_dist_dir}/Release>")
    add_custom_command(TARGET ${INSTALLER_TARGET} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CEF_BIN_DIR}/libcef.dll" "$<TARGET_FILE_DIR:${INSTALLER_TARGET}>/" || ${CMAKE_COMMAND} -E echo "libcef.dll not found"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CEF_BIN_DIR}/browser_process.exe" "$<TARGET_FILE_DIR:${INSTALLER_TARGET}>/" || ${CMAKE_COMMAND} -E echo "browser_process.exe not found"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CEF_BIN_DIR}/icudtl.dat" "$<TARGET_FILE_DIR:${INSTALLER_TARGET}>/" || ${CMAKE_COMMAND} -E copy_if_different "${cef_binary_dist_dir}/Resources/icudtl.dat" "$<TARGET_FILE_DIR:${INSTALLER_TARGET}>/" || ${CMAKE_COMMAND} -E echo "icudtl.dat not found"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CEF_BIN_DIR}/resources.pak" "$<TARGET_FILE_DIR:${INSTALLER_TARGET}>/" || ${CMAKE_COMMAND} -E copy_if_different "${cef_binary_dist_dir}/Resources/resources.pak" "$<TARGET_FILE_DIR:${INSTALLER_TARGET}>/" || ${CMAKE_COMMAND} -E echo "resources.pak not found"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_SOURCE_DIR}/scripts/installer_web_ui.html" "$<TARGET_FILE_DIR:${INSTALLER_TARGET}>/installer_ui.html"
    )
endif()
