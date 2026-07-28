#!/usr/bin/env ruby
# Script to create installer for Metasiberia Beta
# Uses Inno Setup to create installer with web-based UI

require 'fileutils'
require 'date'

# Configuration
SOURCE_DIR = ENV['CYBERSPACE_OUTPUT'] || 'C:/programming/substrata_output'
BUILD_DIR = File.join(SOURCE_DIR, 'vs2022/cyberspace_x64/RelWithDebInfo')
OUTPUT_DIR = File.join(SOURCE_DIR, 'installers')
INNO_SETUP_PATH = 'C:/Program Files (x86)/Inno Setup 6/ISCC.exe'

# Get Metasiberia version from MetasiberiaVersion.h or use default
def get_version
  version_file = File.join(File.dirname(__FILE__), '..', 'shared', 'MetasiberiaVersion.h')
  if File.exist?(version_file)
    content = File.read(version_file)
    # Match: const std::string metasiberia_version = "00001";
    if match = content.match(/metasiberia_version\s*=\s*"([^"]+)"/)
      return match[1]
    end
  end
  # Default version if not found (semantic versioning format)
  '0.0.2'
end

def check_inno_setup
  unless File.exist?(INNO_SETUP_PATH)
    puts "ERROR: Inno Setup not found at: #{INNO_SETUP_PATH}"
    puts "Please install Inno Setup from: https://jrsoftware.org/isinfo.php"
    puts ""
    puts "Alternative locations to check:"
    alt_paths = [
      'C:/Program Files/Inno Setup 6/ISCC.exe',
      'C:/Program Files (x86)/Inno Setup 5/ISCC.exe',
      'C:/Program Files/Inno Setup 5/ISCC.exe'
    ]
    alt_paths.each do |path|
      if File.exist?(path)
        puts "  Found: #{path}"
        return path
      end
    end
    return nil
  end
  INNO_SETUP_PATH
end

def check_source_files
  required_files = [
    'gui_client.exe',
    'libcef.dll',
    'browser_process.exe',
    'SDL2.dll'
  ]
  
  missing = []
  required_files.each do |file|
    path = File.join(BUILD_DIR, file)
    unless File.exist?(path)
      missing << file
    end
  end
  
  if missing.any?
    puts "ERROR: Missing required files:"
    missing.each { |f| puts "  - #{f}" }
    return false
  end
  
  puts "All required files found!"
  return true
end

def create_installer
  puts "Creating installer for Metasiberia Beta..."
  puts ""
  
  # Check Inno Setup
  iscc_path = check_inno_setup
  unless iscc_path
    puts "Cannot proceed without Inno Setup."
    return false
  end
  
  # Check source files
  unless check_source_files
    puts "Cannot proceed without required files."
    return false
  end
  
  # Create output directory
  FileUtils.mkdir_p(OUTPUT_DIR)
  
  # Get version
  version = get_version
  puts "Version: #{version}"
  
  # Get script directory
  script_dir = File.dirname(__FILE__)
  iss_file = File.join(script_dir, 'create_installer.iss')
  
  unless File.exist?(iss_file)
    puts "ERROR: Installer script not found: #{iss_file}"
    return false
  end
  
  # Build installer with version parameter
  puts "Building installer..."
  puts "  Source: #{BUILD_DIR}"
  puts "  Output: #{OUTPUT_DIR}"
  puts "  Script: #{iss_file}"
  puts "  Version: #{version}"
  puts ""
  
  # Pass version to Inno Setup
  cmd = "\"#{iscc_path}\" /DMyAppVersion=\"#{version}\" \"#{iss_file}\""
  puts "Running: #{cmd}"
  puts ""
  
  result = system(cmd)
  
  if result
    puts ""
    puts "SUCCESS: Installer created!"
    puts "Location: #{OUTPUT_DIR}"
    puts ""
    
    # List created files
    installer_files = Dir.glob(File.join(OUTPUT_DIR, '*.exe'))
    installer_files.each do |file|
      size = File.size(file) / (1024.0 * 1024.0)
      puts "  #{File.basename(file)} - #{size.round(2)} MB"
    end
  else
    puts ""
    puts "ERROR: Installer build failed!"
    return false
  end
  
  true
end

if __FILE__ == $0
  create_installer
end
