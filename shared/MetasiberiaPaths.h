/*=====================================================================
MetasiberiaPaths.h
------------------
Compatibility-aware locations for Metasiberia-owned local data.

Existing installations used the Cyberspace (client) and Substrata
(Windows server) application-data directories.  Do not move or copy those
directories implicitly: caches and user settings can be large, and copying
them is both slow and error-prone.  Instead, keep using an existing legacy
directory and use the Metasiberia directory only for new installations.
=====================================================================*/

#pragma once

#include "../utils/FileUtils.h"
#include "../utils/PlatformUtils.h"


namespace MetasiberiaPaths
{

inline bool hasClientAppData(const std::string& appdata_dir)
{
	return FileUtils::fileExists(appdata_dir + "/log.txt") ||
		FileUtils::fileExists(appdata_dir + "/settings_store.xml") ||
		FileUtils::isDirectory(appdata_dir + "/screenshots") ||
		FileUtils::isDirectory(appdata_dir + "/indigo_scenes");
}


inline std::string getClientAppDataDirectory()
{
	const std::string metasiberia_dir = PlatformUtils::getAppDataDirectory("Metasiberia");
	if(hasClientAppData(metasiberia_dir))
		return metasiberia_dir;

	const std::string legacy_dir = PlatformUtils::getAppDataDirectory("Cyberspace");
	if(FileUtils::isDirectory(legacy_dir))
		return legacy_dir;

	// A Metasiberia directory can be created by the Windows server for server_data only.
	// With no legacy client data to preserve, it is also the correct directory for a new client.
	if(FileUtils::isDirectory(metasiberia_dir))
		return metasiberia_dir;

	return PlatformUtils::getOrCreateAppDataDirectory("Metasiberia");
}


inline std::string getWindowsServerStateDirectory()
{
	const std::string metasiberia_state_dir = PlatformUtils::getAppDataDirectory("Metasiberia") + "/server_data";
	if(FileUtils::isDirectory(metasiberia_state_dir))
		return metasiberia_state_dir;

	const std::string legacy_state_dir = PlatformUtils::getAppDataDirectory("Substrata") + "/server_data";
	if(FileUtils::isDirectory(legacy_state_dir))
		return legacy_state_dir;

	return PlatformUtils::getOrCreateAppDataDirectory("Metasiberia") + "/server_data";
}

}
