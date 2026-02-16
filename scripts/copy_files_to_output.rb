#
# Copies various files that are need to run Substrata into the CYBERSPACE_OUTPUT directories.
#
# Note that copyCyberspaceResources() is defined in dist_utils.rb
#

require './dist_utils.rb'
require './config-lib.rb'


$copy_cef = true
$copy_bugsplat = true


def printUsage()
	puts "Usage: copy_files_to_output.rb [arguments]"
	puts ""
	puts "\t--no_cef, \t\tSkip copying CEF files."
	puts "\t--config CONFIG, \tCopy files only for specified config (Debug, RelWithDebInfo, Release)."
	puts "\t\t\t\tIf not specified, copies for all configs."
	puts ""
	puts "\t--help, -h\t\tShows this help."
	puts ""
end


$config_filter = nil

arg_parser = ArgumentParser.new(ARGV)
arg_parser.options.each do |opt|
	if opt[0] == "--no_cef"
		$copy_cef = false
	elsif opt[0] == "--no_bugsplat"
		$copy_bugsplat = false
	elsif opt[0] == "--config"
		$config_filter = opt[1]
		if !["Debug", "RelWithDebInfo", "Release"].include?($config_filter)
			puts "Error: Invalid config '#{$config_filter}'. Must be one of: Debug, RelWithDebInfo, Release"
			exit 1
		end
	elsif opt[0] == "--help" || opt[0] == "-h"
		printUsage()
		exit 0
	else
		puts "Unrecognised argument: #{opt[0]}"
		exit 1
	end
end


def copy_files(vs_version, substrata_repos_dir, glare_core_repos_dir, config = nil)

	# If config is specified, only copy files for that config
	# Otherwise, copy for all configs (backward compatibility)
	configs_to_copy = config ? [config] : ["Debug", "RelWithDebInfo", "Release"]

	configs_to_copy.each do |config_name|
		begin
			output_dir = getCmakeBuildDir(vs_version, config_name)
			
			# Determine if this is a debug build (Debug config uses debug DLLs)
			is_debug = (config_name == "Debug")
			
			copyCyberspaceResources(substrata_repos_dir, glare_core_repos_dir, output_dir)
			copyCEFRedistWindows(output_dir, is_debug) if $copy_cef
			copyBugSplatRedist(output_dir) if $copy_bugsplat
			copyQtRedistWindows(vs_version, output_dir, is_debug)
			copySDLRedistWindows(vs_version, output_dir, is_debug)
		rescue => e
			puts "Warning: Failed to copy files for #{config_name}: #{e.message}"
		end
	end
end



substrata_repos_dir = ".."
glare_core_repos_dir = getAndCheckEnvVar('GLARE_CORE_TRUNK_DIR')

if OS.windows?
	copy_files(2022, substrata_repos_dir, glare_core_repos_dir, $config_filter)
elsif OS.mac?
	begin
        build_dir = getCmakeBuildDir(0, "Debug")
		appdir = build_dir + "/gui_client.app"
		output_dir = build_dir + "/gui_client.app/Contents/MacOS/../Resources"

		copyCyberspaceResources(substrata_repos_dir, glare_core_repos_dir, output_dir)
		copyCEFRedistMac(build_dir, appdir) if $copy_cef
	end

	begin
		#output_dir = getCmakeBuildDir(vs_version, "RelWithDebInfo")

		#copyCyberspaceResources(output_dir)
	end
else # else Linux:
	output_dir = getCmakeBuildDir(0, "Debug")
	puts "output_dir: #{output_dir}"
	copyCyberspaceResources(substrata_repos_dir, glare_core_repos_dir, output_dir)
	
	copyCEFRedistLinux(output_dir,
		false # strip_symbols
	) if $copy_cef
end
