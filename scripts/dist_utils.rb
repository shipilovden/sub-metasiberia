#
# Various utility methods for packing cyberspace distributions.
#
#

require 'fileutils'
require './script_utils.rb'
require './config-lib.rb'


def getCmakeBuildDir(vs_version, config)
	cyberspace_output = getAndCheckEnvVar('CYBERSPACE_OUTPUT')

	if OS.windows?
		return cyberspace_output + "/vs#{vs_version}/cyberspace_x64/#{config}"
	else
		if config == $config_name_release
			return cyberspace_output
		else
			return cyberspace_output + "/test_builds"
		end
	end
end


def copyQtRedistWindows(vs_version, target_dir, copy_debug = false)
	if (!OS.windows?)
		return
	end
	
	# Get Qt path.
	glare_core_libs_dir = getAndCheckEnvVar('GLARE_CORE_LIBS')
	qt_dir = (defined?($indigo_qt_dir) && $indigo_qt_dir && !$indigo_qt_dir.empty?) ? $indigo_qt_dir : "#{glare_core_libs_dir}/Qt/#{$qt_version}-vs#{vs_version}-64"
	lib_path = "#{qt_dir}/bin"
	plugins_path = "#{qt_dir}/plugins"
	
	# Qt dlls. Keep Qt 5 and Qt 6 runtime names isolated by the selected config.rb version.
	qt_major = $qt_version.to_s.split(".").first.to_i
	qt_prefix = qt_major >= 6 ? "Qt6" : "Qt5"
	# QtMultimedia depends on QtNetwork, so include it explicitly.
	dll_files = ["Core", "Gui", "OpenGL", "Widgets", "Network", "Multimedia", "MultimediaWidgets"]
	dll_files << "OpenGLWidgets" if qt_major >= 6
	dll_files << "Gamepad" if qt_major < 6
	dll_files << "Core5Compat" if qt_major >= 6

		
	dll_files.each do |dll_file|
		qt_dll_name = "#{qt_prefix}#{dll_file}"
		FileUtils.cp("#{lib_path}/#{qt_dll_name}.dll", target_dir, :verbose => true) if !copy_debug
		FileUtils.cp("#{lib_path}/#{qt_dll_name}d.dll", target_dir, :verbose => true) if copy_debug
	end

	# Optional Qt SVG support for toolbar/menu SVG icons.
	svg_dll_path = copy_debug ? "#{lib_path}/#{qt_prefix}Svgd.dll" : "#{lib_path}/#{qt_prefix}Svg.dll"
	if File.exist?(svg_dll_path)
		FileUtils.cp(svg_dll_path, target_dir, :verbose => true)
	else
		STDERR.puts "Warning: Qt SVG DLL not found: #{svg_dll_path}"
	end
	
	# Imageformats
	imageformats_dir = "#{plugins_path}/imageformats"
	imageformats_target_dir = "#{target_dir}/imageformats"
	
	FileUtils.mkdir_p(imageformats_target_dir, :verbose => true)
	
	# Keep extended image support in AddObjectDialog working in packaged builds.
	image_formats = ["qjpeg", "qgif", "qtga", "qtiff", "qwbmp", "qwebp", "qsvg"]

	image_formats.each do |format|
		src_path = copy_debug ? "#{imageformats_dir}/#{format}d.dll" : "#{imageformats_dir}/#{format}.dll"
		if File.exist?(src_path)
			FileUtils.cp(src_path, imageformats_target_dir, :verbose => true)
		else
			STDERR.puts "Warning: Qt imageformat plugin not found: #{src_path}"
		end
	end
	
	# Seems to work without copying runtime DLLs into these dirs.
	# copyVCRedist(vs_version, imageformats_target_dir, false)
	
	# copyVCRedist(vs_version, sqldrivers_target_dir, false)
	
	# Platfroms
	platforms_dir = "#{plugins_path}/platforms"
	platforms_dir_target_dir = "#{target_dir}/platforms"
	
	FileUtils.mkdir_p(platforms_dir_target_dir, :verbose => true)
	
	FileUtils.cp("#{platforms_dir}/qwindows.dll",  platforms_dir_target_dir, :verbose => true) if !copy_debug
	FileUtils.cp("#{platforms_dir}/qwindowsd.dll", platforms_dir_target_dir, :verbose => true) if copy_debug
	
	# Styles
	styles_dir = "#{plugins_path}/styles"
	styles_dir_target_dir = "#{target_dir}/styles"
	
	FileUtils.mkdir_p(styles_dir_target_dir, :verbose => true)
	
	style_name = copy_debug ? "qwindowsvistastyled.dll" : "qwindowsvistastyle.dll"
	style_path = "#{styles_dir}/#{style_name}"
	if File.exist?(style_path)
		FileUtils.cp(style_path, styles_dir_target_dir, :verbose => true)
	else
		STDERR.puts "Warning: Qt style plugin not found: #{style_path}"
	end
	
	# Gamepads
	if qt_major < 6
		gamepads_dir = "#{plugins_path}/gamepads"
		gamepads_dir_target_dir = "#{target_dir}/gamepads"
		FileUtils.mkdir_p(gamepads_dir_target_dir, :verbose => true)

		FileUtils.cp("#{gamepads_dir}/xinputgamepad.dll",  gamepads_dir_target_dir, :verbose => true) if !copy_debug
		FileUtils.cp("#{gamepads_dir}/xinputgamepadd.dll", gamepads_dir_target_dir, :verbose => true) if copy_debug
	end

	# Multimedia services (needed for webcam / camera support).
	mediaservice_dir = "#{plugins_path}/mediaservice"
	mediaservice_target_dir = "#{target_dir}/mediaservice"
	if File.directory?(mediaservice_dir)
		FileUtils.mkdir_p(mediaservice_target_dir, :verbose => true)

		Dir.glob("#{mediaservice_dir}/*.dll").each do |path|
			name = File.basename(path)
			if copy_debug
				next if !name.end_with?("d.dll")
			else
				next if name.end_with?("d.dll")
			end
			FileUtils.cp(path, mediaservice_target_dir, :verbose => true)
		end
	end

	# copyVCRedist(vs_version, platforms_dir_target_dir, false)
end


def copySDLRedistWindows(vs_version, target_dir, copy_debug)
	if (!OS.windows?)
		return
	end
	
	# Get SDL path.
	glare_core_libs_dir = getAndCheckEnvVar('GLARE_CORE_LIBS')
	sdl_dir = "#{glare_core_libs_dir}/SDL/sdl_2.30.9_build"

	FileUtils.cp("#{sdl_dir}/Debug/SDL2d.dll",  target_dir, :verbose => true) if  copy_debug
	FileUtils.cp("#{sdl_dir}/Release/SDL2.dll", target_dir, :verbose => true) if !copy_debug
end


def copyOpenXRRedistWindows(target_dir)
	if (!OS.windows?)
		return
	end

	# XR_SUPPORT links against the OpenXR loader DLL on Windows. Keep this copy
	# optional so desktop-only builds and machines without the SDK still package
	# normally.
	glare_core_libs_dir = getAndCheckEnvVar('GLARE_CORE_LIBS')
	candidate_sdk_dirs = []
	candidate_sdk_dirs << ENV['OPENXR_SDK_DIR'] if ENV['OPENXR_SDK_DIR'] && ENV['OPENXR_SDK_DIR'].length > 0
	candidate_sdk_dirs << "#{glare_core_libs_dir}/OpenXR-SDK-1.1.57/install"

	candidate_sdk_dirs.each do |sdk_dir|
		dll_path = "#{sdk_dir}/x64/bin/openxr_loader.dll"
		if File.exist?(dll_path)
			FileUtils.cp(dll_path, target_dir, :verbose => true)
			return
		end
	end

	STDERR.puts "Warning: OpenXR loader DLL not found; XR builds may require openxr_loader.dll next to gui_client.exe."
end


def copyCEFRedistWindows(target_dir, copy_debug = false)
	if (!OS.windows?)
		return
	end
	
	# Get CEF binary distibution path.
	cef_bin_distrib_dir = getAndCheckEnvVar('CEF_BINARY_DISTRIB_DIR')
	
	# See e.g. C:\cef\chromium\src\cef\binary_distrib\cef_binary_101.0.0-Unknown.0+gUnknown+chromium-101.0.4951.26_windows64\README.txt for needed files.
	
	debug_or_rel_cef_distrib_dir = cef_bin_distrib_dir + "/" + (copy_debug ? "Debug" : "Release")

	FileUtils.cp(debug_or_rel_cef_distrib_dir + "/chrome_elf.dll", target_dir, :verbose => true)
	#FileUtils.cp(debug_or_rel_cef_distrib_dir + "/snapshot_blob.bin", target_dir, :verbose => true)
	FileUtils.cp(debug_or_rel_cef_distrib_dir + "/d3dcompiler_47.dll", target_dir, :verbose => true)
	FileUtils.cp(debug_or_rel_cef_distrib_dir + "/dxcompiler.dll", target_dir, :verbose => true)
	FileUtils.cp(debug_or_rel_cef_distrib_dir + "/dxil.dll", target_dir, :verbose => true)
	FileUtils.cp(debug_or_rel_cef_distrib_dir + "/libcef.dll", target_dir, :verbose => true)
	FileUtils.cp(debug_or_rel_cef_distrib_dir + "/libEGL.dll", target_dir, :verbose => true)
	FileUtils.cp(debug_or_rel_cef_distrib_dir + "/libGLESv2.dll", target_dir, :verbose => true)
	FileUtils.cp(debug_or_rel_cef_distrib_dir + "/v8_context_snapshot.bin", target_dir, :verbose => true)
	FileUtils.cp(debug_or_rel_cef_distrib_dir + "/vk_swiftshader.dll", target_dir, :verbose => true)
	FileUtils.cp(debug_or_rel_cef_distrib_dir + "/vk_swiftshader_icd.json", target_dir, :verbose => true)
	FileUtils.cp(debug_or_rel_cef_distrib_dir + "/vulkan-1.dll", target_dir, :verbose => true)
	#FileUtils.cp_r(debug_or_rel_cef_distrib_dir + "/swiftshader", target_dir, :verbose => true)
	#
	FileUtils.cp_r(cef_bin_distrib_dir + "/Resources/locales", target_dir, :verbose => true)
	FileUtils.cp(cef_bin_distrib_dir + "/Resources/chrome_100_percent.pak", target_dir, :verbose => true)
	FileUtils.cp(cef_bin_distrib_dir + "/Resources/chrome_200_percent.pak", target_dir, :verbose => true)
	FileUtils.cp(cef_bin_distrib_dir + "/Resources/icudtl.dat", target_dir, :verbose => true)
	FileUtils.cp(cef_bin_distrib_dir + "/Resources/resources.pak", target_dir, :verbose => true)
end

def copyCEFRedistLinux(target_dir, strip_symbols)

	# The rpath is set to look in lib, so put the CEF files there.
	target_lib_dir = target_dir + "/lib"

	FileUtils.mkdir_p(target_lib_dir, :verbose => true) if !File.exists?(target_lib_dir) # Make target_lib_dir if it doesn't exist already.
	
	# Get CEF binary distibution path.
	cef_bin_distrib_dir = getAndCheckEnvVar('CEF_BINARY_DISTRIB_DIR')
	
	# See e.g. C:\cef\chromium\src\cef\binary_distrib\cef_binary_101.0.0-Unknown.0+gUnknown+chromium-101.0.4951.26_windows64\README.txt for needed files.
	FileUtils.cp(cef_bin_distrib_dir + "/Release/libcef.so",				target_lib_dir, :verbose => true)
	FileUtils.cp(cef_bin_distrib_dir + "/Release/chrome-sandbox",			target_lib_dir, :verbose => true)
	FileUtils.cp(cef_bin_distrib_dir + "/Release/snapshot_blob.bin",		target_lib_dir, :verbose => true)
	FileUtils.cp(cef_bin_distrib_dir + "/Release/v8_context_snapshot.bin",	target_lib_dir, :verbose => true)
	FileUtils.cp(cef_bin_distrib_dir + "/Release/libEGL.so",				target_lib_dir, :verbose => true)
	FileUtils.cp(cef_bin_distrib_dir + "/Release/libGLESv2.so",				target_lib_dir, :verbose => true)
	FileUtils.cp(cef_bin_distrib_dir + "/Release/libvk_swiftshader.so",		target_lib_dir, :verbose => true)
	FileUtils.cp(cef_bin_distrib_dir + "/Release/vk_swiftshader_icd.json",	target_lib_dir, :verbose => true)
	FileUtils.cp(cef_bin_distrib_dir + "/Release/libvulkan.so.1",			target_lib_dir, :verbose => true)
	FileUtils.cp_r(cef_bin_distrib_dir + "/Release/swiftshader",			target_lib_dir, :verbose => true)
	
	FileUtils.cp_r(cef_bin_distrib_dir + "/Resources/locales",				target_lib_dir, :verbose => true)
	FileUtils.cp(cef_bin_distrib_dir + "/Resources/chrome_100_percent.pak",	target_lib_dir, :verbose => true)
	FileUtils.cp(cef_bin_distrib_dir + "/Resources/chrome_200_percent.pak",	target_lib_dir, :verbose => true)
	FileUtils.cp(cef_bin_distrib_dir + "/Resources/icudtl.dat",				target_lib_dir, :verbose => true)
	FileUtils.cp(cef_bin_distrib_dir + "/Resources/resources.pak",			target_lib_dir, :verbose => true)
	
	# We strip symbols for the Substrata distribution, because libcef.so is gigantic without stripping (e.g. 1.3 GB), and much smaller with stripping (e.g. 194 MB)
	if strip_symbols
		print_and_exec_command("strip --strip-unneeded \"#{target_lib_dir}/libcef.so\"")
		print_and_exec_command("strip --strip-unneeded \"#{target_lib_dir}/libEGL.so\"")
		print_and_exec_command("strip --strip-unneeded \"#{target_lib_dir}/libGLESv2.so\"")
		print_and_exec_command("strip --strip-unneeded \"#{target_lib_dir}/libvk_swiftshader.so\"")
		print_and_exec_command("strip --strip-unneeded \"#{target_lib_dir}/libvulkan.so.1\"")
		print_and_exec_command("strip --strip-unneeded \"#{target_lib_dir}/swiftshader/libEGL.so\"")
		print_and_exec_command("strip --strip-unneeded \"#{target_lib_dir}/swiftshader/libGLESv2.so\"")
	end
end

def copyCEFRedistMac(cyb_output_dir, appdir)
    
    cef_binary_distrib_dir = getAndCheckEnvVar('CEF_BINARY_DISTRIB_DIR')

    FileUtils.mkdir(appdir + "/Contents/Frameworks", {:verbose=>true}) if !File.exists?(appdir + "/Contents/Frameworks") # Make frameworks dir if not existing.

    FileUtils.cp_r(cef_binary_distrib_dir + "/Release/Chromium Embedded Framework.framework", appdir + "/Contents/Frameworks", {:verbose=>true})

    # Copy helper apps.  Assuming they are built to the same directory as gui_client.app.
    FileUtils.cp_r(cyb_output_dir + "/gui_client Helper.app",            appdir + "/Contents/Frameworks", {:verbose=>true})
    FileUtils.cp_r(cyb_output_dir + "/gui_client Helper (Plugin).app",   appdir + "/Contents/Frameworks", {:verbose=>true})
    FileUtils.cp_r(cyb_output_dir + "/gui_client Helper (Renderer).app", appdir + "/Contents/Frameworks", {:verbose=>true})
    FileUtils.cp_r(cyb_output_dir + "/gui_client Helper (GPU).app",      appdir + "/Contents/Frameworks", {:verbose=>true})
end


# Copy BugSplat support files (See https://docs.bugsplat.com/introduction/getting-started/integrations/desktop/cplusplus)
def copyBugSplatRedist(dist_dir)

	bugsplat_dir = getAndCheckEnvVar('GLARE_CORE_LIBS') + "/BugSplat"
	FileUtils.copy(bugsplat_dir + "/BugSplat/x64/Release/BsSndRpt64.exe",   "#{dist_dir}/", :verbose => true)
	FileUtils.copy(bugsplat_dir + "/BugSplat/x64/Release/BugSplat64.dll",   "#{dist_dir}/", :verbose => true)
	FileUtils.copy(bugsplat_dir + "/BugSplat/x64/Release/BugSplatRc64.dll", "#{dist_dir}/", :verbose => true)
end


def copyVCRedist(vs_version, target_dir)
	if(vs_version == 2022)
		redist_path = "C:/Program Files/Microsoft Visual Studio/#{vs_version}/Community/VC/Redist/MSVC/14.32.31326/x64"
		
		copyAllFilesInDirDelete("#{redist_path}/Microsoft.VC143.CRT", target_dir)
	elsif(vs_version == 2019)
		redist_path = "C:/Program Files (x86)/Microsoft Visual Studio/#{vs_version}/Community/VC/Redist/MSVC/14.29.30133/x64"
		
		copyAllFilesInDirDelete("#{redist_path}/Microsoft.VC142.CRT", target_dir)
	else
		STDERR.puts "Unhandled vs version in copyVCRedist: #{vs_version}"
		exit 1
	end
end


def copyCyberspaceResources(substrata_repos_dir, glare_core_repos_dir, dist_dir, vs_version = $vs_version, config = $config_name_release, copy_build_output = true)
	
	FileUtils.mkdir_p("#{dist_dir}/data", :verbose => true) # Make 'data' dir, so that setting it as a target will make data/shaders be created etc..

	FileUtils.cp_r(substrata_repos_dir + "/resources", dist_dir + "/data", :verbose => true)
	
	FileUtils.cp_r(substrata_repos_dir + "/shaders", dist_dir + "/data", :verbose => true) # Copy OpenGL shaders from the Substrata repo.
	
	# Copy misc. files.
	#FileUtils.cp("../lang/ISL_stdlib.txt", dist_dir, :verbose => true)
	
	# Copy OpenGL shaders.
	FileUtils.cp_r("#{glare_core_repos_dir}/opengl/shaders", dist_dir + "/data", :verbose => true)
	
	# Copy OpenGL data.
	FileUtils.cp_r("#{glare_core_repos_dir}/opengl/gl_data", dist_dir + "/data", :verbose => true)

	# Copy licence.txt
	FileUtils.cp_r(substrata_repos_dir + "/docs/licence.txt", dist_dir + "/", :verbose => true)

	# Make sure files are group/other readable, they weren't for some reason.  The Dir.glob gets all files (recursively) in dir.
	FileUtils.chmod("u=wr,go=rr", Dir.glob("#{dist_dir}/data/shaders/*.*"))
	FileUtils.chmod("u=wr,go=rr", Dir.glob("#{dist_dir}/data/gl_data/*.*"))
end
