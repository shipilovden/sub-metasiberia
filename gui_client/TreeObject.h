/*=====================================================================
TreeObject.h
------------
Copyright Glare Technologies Limited 2026 -
=====================================================================*/
#pragma once


#include "TreeParams.h"
#include <string>


class WorldObject;


class TreeObject
{
public:
	TreeObject();
	explicit TreeObject(const TreeParams& params);

	void rebuild();
	void setParams(const TreeParams& new_params);
	const TreeParams& getParams() const;
	const std::string& generatedModelPath() const;

	static bool isTreeObject(const WorldObject& ob);
	static TreeParams paramsFromObject(const WorldObject& ob);
	static void applyToWorldObject(WorldObject& ob, const TreeParams& params, bool rebuild_mesh);

private:
	TreeParams params;
	std::string model_path;
	bool dirty;
};

