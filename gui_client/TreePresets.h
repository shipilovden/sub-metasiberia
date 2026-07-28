/*=====================================================================
TreePresets.h
-------------
Copyright Glare Technologies Limited 2026 -
=====================================================================*/
#pragma once


#include "TreeParams.h"


#include <cstddef>
#include <string>


namespace TreePresets
{
	struct PresetInfo
	{
		const char* id;
		const char* display_name;
	};

	const PresetInfo* allPresets(size_t& count);
	const char* defaultPresetId();
	TreeParams presetById(const std::string& id);
	TreeParams Oak();
	TreeParams Birch();
	TreeParams Pine();
	TreeParams DeadTree();
	TreeParams Bush();
	TreeParams preset(TreePresetType type);
}
