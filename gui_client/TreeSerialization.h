/*=====================================================================
TreeSerialization.h
-------------------
Copyright Glare Technologies Limited 2026 -
=====================================================================*/
#pragma once


#include "TreeParams.h"
#include <string>


namespace TreeSerialization
{
	const char* contentMarker();
	bool isTreeContent(const std::string& content);

	const char* presetToString(TreePresetType preset);
	TreePresetType presetFromString(const std::string& s);
	const char* leafTypeToString(TreeLeafType type);
	TreeLeafType leafTypeFromString(const std::string& s);
	const char* billboardModeToString(TreeBillboardMode mode);
	TreeBillboardMode billboardModeFromString(const std::string& s);
	const char* qualityToString(TreeQuality quality);
	TreeQuality qualityFromString(const std::string& s);
	const char* collisionModeToString(TreeCollisionMode mode);
	TreeCollisionMode collisionModeFromString(const std::string& s);

	TreeParams defaultParams();
	TreeParams fromContent(const std::string& content, std::string* parse_error_out = 0);
	std::string serialiseToContent(const TreeParams& params);
	void clamp(TreeParams& params);
}

