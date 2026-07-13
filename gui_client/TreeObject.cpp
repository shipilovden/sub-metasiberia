/*=====================================================================
TreeObject.cpp
--------------
Copyright Glare Technologies Limited 2026 -
=====================================================================*/
#include "TreeObject.h"
#include "TreeGenerator.h"
#include "TreeSerialization.h"


#include "../shared/WorldObject.h"
#include "../shared/WorldMaterial.h"
#include <BitUtils.h>


namespace
{
std::string barkFolderForParams(const TreeParams& params)
{
	return params.barkTextureType == "Bark006" ? "Bark006_1K-JPG" : "Bark001_1K-JPG";
}


std::string barkPrefixForParams(const TreeParams& params)
{
	return params.barkTextureType == "Bark006" ? "Bark006_1K-JPG" : "Bark001_1K-JPG";
}


std::string leafTextureForParams(const TreeParams& params)
{
	switch(params.leafType)
	{
	case TreeLeafType::Oak:         return "tree_assets/leaves/oak.png";
	case TreeLeafType::Birch:       return "tree_assets/leaves/aspen.png";
	case TreeLeafType::PineNeedles: return "tree_assets/leaves/pine.png";
	case TreeLeafType::Simple:      return "tree_assets/leaves/ash.png";
	case TreeLeafType::None:        break;
	}
	return "";
}
}


TreeObject::TreeObject()
:	params(TreeSerialization::defaultParams()),
	dirty(true)
{
}


TreeObject::TreeObject(const TreeParams& params_)
:	params(params_),
	dirty(true)
{
	TreeSerialization::clamp(params);
}


void TreeObject::rebuild()
{
	model_path = TreeGenerator::writeObjToTempFile(params);
	dirty = false;
}


void TreeObject::setParams(const TreeParams& new_params)
{
	params = new_params;
	TreeSerialization::clamp(params);
	dirty = true;
}


const TreeParams& TreeObject::getParams() const
{
	return params;
}


const std::string& TreeObject::generatedModelPath() const
{
	return model_path;
}


bool TreeObject::isTreeObject(const WorldObject& ob)
{
	return ob.object_type == WorldObject::ObjectType_Generic && TreeSerialization::isTreeContent(ob.content);
}


TreeParams TreeObject::paramsFromObject(const WorldObject& ob)
{
	return TreeSerialization::fromContent(ob.content);
}


void TreeObject::applyToWorldObject(WorldObject& ob, const TreeParams& params_, bool rebuild_mesh)
{
	TreeParams params = params_;
	TreeSerialization::clamp(params);

	ob.object_type = WorldObject::ObjectType_Generic;
	ob.content = TreeSerialization::serialiseToContent(params);
	ob.changed_flags |= WorldObject::CONTENT_CHANGED;

	if(rebuild_mesh)
	{
		TreeObject tree(params);
		tree.rebuild();
		ob.model_url = tree.generatedModelPath();
		ob.changed_flags |= WorldObject::MODEL_URL_CHANGED;
	}

	if(ob.materials.size() < 2)
		ob.materials.resize(2);
	if(ob.materials[0].isNull())
		ob.materials[0] = new WorldMaterial();
	if(ob.materials[1].isNull())
		ob.materials[1] = new WorldMaterial();

	ob.materials[0]->name = "Tree Bark";
	ob.materials[0]->colour_rgb = Colour3f(params.barkColor.r, params.barkColor.g, params.barkColor.b);
	ob.materials[0]->roughness = ScalarVal(0.82f);
	if(params.barkTextured)
	{
		const std::string folder = barkFolderForParams(params);
		const std::string prefix = barkPrefixForParams(params);
		ob.materials[0]->colour_texture_url = "tree_assets/bark/" + folder + "/" + prefix + "_Color.jpg";
		ob.materials[0]->normal_map_url = "tree_assets/bark/" + folder + "/" + prefix + "_NormalGL.jpg";
		ob.materials[0]->roughness.texture_url = "tree_assets/bark/" + folder + "/" + prefix + "_Roughness.jpg";
	}
	else
	{
		ob.materials[0]->colour_texture_url = "";
		ob.materials[0]->normal_map_url = "";
		ob.materials[0]->roughness.texture_url = "";
	}

	ob.materials[1]->name = "Tree Leaves";
	ob.materials[1]->colour_rgb = Colour3f(params.leafColor.r, params.leafColor.g, params.leafColor.b);
	ob.materials[1]->colour_texture_url = leafTextureForParams(params);
	ob.materials[1]->opacity = ScalarVal(params.leafAlpha);
	ob.materials[1]->roughness = ScalarVal(0.65f);
	ob.materials[1]->flags = WorldMaterial::DOUBLE_SIDED_FLAG | WorldMaterial::USE_VERT_COLOURS_FOR_WIND;
	if(params.leafAlpha < 0.999f)
		BitUtils::setBit(ob.materials[1]->flags, WorldMaterial::COLOUR_TEX_HAS_ALPHA_FLAG);
	if(!ob.materials[1]->colour_texture_url.empty())
		BitUtils::setBit(ob.materials[1]->flags, WorldMaterial::COLOUR_TEX_HAS_ALPHA_FLAG);

	ob.setCollidable(params.collisionMode != TreeCollisionMode::None);
	ob.setDynamic(false);
	ob.setIsSensor(false);
}
