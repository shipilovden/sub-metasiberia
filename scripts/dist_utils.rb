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


# Remove nested plugin/locales dirs (e.g. platforms/platforms, gamepads/gamepads) that can break Qt loader
def removeNestedPluginDirs(target_dir)
	return if !OS.windows?
	["platforms", "gamepads", "imageformats", "styles", "locales", "mediaservice", "multimedia"].each do |sub|
		nested = "#{target_dir}/#{sub}/#{sub}"
		if File.directory?(nested)
			FileUtils.rm_r(nested, :verbose => true)
		end
	end
end

def copyQtRedistWindows(vs_version, target_dir, copy_debug = false)
	if (!OS.windows?)
		return
	end
	
	# Use configured/overridden Qt dir from config-lib.rb (supports Qt5 and Qt6 layouts).
	qt_dir = $indigo_qt_dir
	if qt_dir.nil? || qt_dir.strip.empty?
		glare_core_libs_dir = getAndCheckEnvVar('GLARE_CORE_LIBS')
		qt_dir = "#{glare_core_libs_dir}/Qt/#{$qt_version}-vs#{vs_version}-64"
	end

	lib_path = "#{qt_dir}/bin"
	plugins_path = "#{qt_dir}/plugins"
	qt6 = $qt_version.to_s.start_with?("6")
	dll_suffix = copy_debug ? "d" : ""
	
	# Qt runtime DLLs.
	dll_files = if qt6
		["Qt6Core", "Qt6Gui", "Qt6OpenGL", "Qt6OpenGLWidgets", "Qt6Widgets", "Qt6Multimedia", "Qt6MultimediaWidgets", "Qt6Network", "Qt6Core5Compat"]
	else
		["Qt5Core", "Qt5Gui", "Qt5OpenGL", "Qt5Widgets", "Qt5Multimedia", "Qt5MultimediaWidgets", "Qt5Network", "Qt5Gamepad"]
	end
	mandatory_dlls = if qt6
		["Qt6Core", "Qt6Gui", "Qt6Widgets", "Qt6Multimedia", "Qt6MultimediaWidgets"]
	else
		["Qt5Core", "Qt5Gui", "Qt5Widgets", "Qt5Multimedia", "Qt5MultimediaWidgets"]
	end

	dll_files.each do |dll_file|
		dll_path = "#{lib_path}/#{dll_file}#{dll_suffix}.dll"
		if File.exist?(dll_path)
			FileUtils.cp(dll_path, target_dir, :verbose => true)
		elsif mandatory_dlls.include?(dll_file)
			raise "Required Qt DLL not found: #{dll_path}"
		end
	end
	
	# Imageformats
	imageformats_dir = "#{plugins_path}/imageformats"
	imageformats_target_dir = "#{target_dir}/imageformats"
	
	FileUtils.mkdir_p(imageformats_target_dir, :verbose => true)
	
	image_formats = ["qjpeg"]
		
	image_formats.each do |format|
		src = "#{imageformats_dir}/#{format}#{dll_suffix}.dll"
		FileUtils.cp(src, imageformats_target_dir, :verbose => true) if File.exist?(src)
	end
	
	# Seems to work without copying runtime DLLs into these dirs.
	# copyVCRedist(vs_version, imageformats_target_dir, false)
	
	# copyVCRedist(vs_version, sqldrivers_target_dir, false)
	
	# Platfroms
	platforms_dir = "#{plugins_path}/platforms"
	platforms_dir_target_dir = "#{target_dir}/platforms"
	
	FileUtils.mkdir_p(platforms_dir_target_dir, :verbose => true)
	
	qwindows_src = "#{platforms_dir}/qwindows#{dll_suffix}.dll"
	FileUtils.cp(qwindows_src, platforms_dir_target_dir, :verbose => true) if File.exist?(qwindows_src)
	
	# Styles
	styles_dir = "#{plugins_path}/styles"
	styles_dir_target_dir = "#{target_dir}/styles"
	
	FileUtils.mkdir_p(styles_dir_target_dir, :verbose => true)

	["qwindowsvistastyle", "qmodernwindowsstyle"].each do |style_plugin|
		style_src = "#{styles_dir}/#{style_plugin}#{dll_suffix}.dll"
		FileUtils.cp(style_src, styles_dir_target_dir, :verbose => true) if File.exist?(style_src)
	end

	# Gamepads (Qt5)
	if !qt6
		gamepads_dir = "#{plugins_path}/gamepads"
		gamepads_dir_target_dir = "#{target_dir}/gamepads"
		
		if File.directory?(gamepads_dir)
			FileUtils.mkdir_p(gamepads_dir_target_dir, :verbose => true)
			gamepad_src = "#{gamepads_dir}/xinputgamepad#{dll_suffix}.dll"
			FileUtils.cp(gamepad_src, gamepads_dir_target_dir, :verbose => true) if File.exist?(gamepad_src)
		end
	end

	# Multimedia backend plugins (required for webcam/camera support)
	if qt6
		multimedia_dir = "#{plugins_path}/multimedia"
		multimedia_target_dir = "#{target_dir}/multimedia"
		if File.directory?(multimedia_dir)
			FileUtils.mkdir_p(multimedia_target_dir, :verbose => true)
			found_backend = false
			["windowsmediaplugin", "ffmpegmediaplugin"].each do |plugin|
				src = "#{multimedia_dir}/#{plugin}#{dll_suffix}.dll"
				if File.exist?(src)
					FileUtils.cp(src, multimedia_target_dir, :verbose => true)
					found_backend = true
				end
			end
			raise "No Qt6 multimedia backend plugin copied from #{multimedia_dir}" if !found_backend
		else
			raise "Qt6 multimedia plugin directory not found: #{multimedia_dir}"
		end
	else
		mediaservice_dir = "#{plugins_path}/mediaservice"
		mediaservice_target_dir = "#{target_dir}/mediaservice"
		if File.directory?(mediaservice_dir)
			FileUtils.mkdir_p(mediaservice_target_dir, :verbose => true)
			["wmfengine", "dsengine"].each do |plugin|
				src = "#{mediaservice_dir}/#{plugin}#{dll_suffix}.dll"
				FileUtils.cp(src, mediaservice_target_dir, :verbose => true) if File.exist?(src)
			end
		end
	end

	# Remove any nested plugin dirs (e.g. platforms/platforms) that can break Qt
	removeNestedPluginDirs(target_dir)

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
