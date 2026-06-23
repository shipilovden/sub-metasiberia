/*=====================================================================
MapWorldLayer.cpp
-----------------
Copyright Glare Technologies Limited 2026 -
=====================================================================*/
#include "MapWorldLayer.h"


#include "ClientThread.h"
#include "GUIClient.h"
#include "LoadItemQueue.h"
#include "MapWorldUtils.h"
#include "../shared/ImageDecoding.h"
#include "../shared/MessageUtils.h"
#include "../shared/Protocol.h"
#include "../shared/ResourceManager.h"
#include <graphics/ImageMap.h>
#include <graphics/SRGBUtils.h>
#include <opengl/OpenGLEngine.h>
#include <utils/Clock.h>
#include <utils/StringUtils.h>
#include <cmath>


namespace
{
struct MapWorldLayerSpec
{
	int tile_z;
	int grid_res;
	float z_offset_m;
	bool show_placeholder_while_loading;
};


static const MapWorldLayerSpec MAP_WORLD_LAYER_SPECS[] =
{
	{11, 5, 0.02f, true },
	{13, 7, 0.04f, false},
	{15, 7, 0.06f, false},
	{17, 5, 0.08f, false},
	{19, 5, 0.10f, false}
};


static const int MAX_TILE_QUERY_COUNT = 900;
static const char* const MAP_WORLD_TILE_PLACEHOLDER_TEX_KEY = "__metasiberia_map_world_tile_placeholder__";


static float getPlaceholderAlpha(const MapWorldTileLayer& layer)
{
	return layer.show_placeholder_while_loading ? 1.f : 0.f;
}


static TextureParams makeMapTileTextureParams()
{
	TextureParams params;
	params.allow_compression = false;
	params.use_mipmaps = false;
	params.filtering = OpenGLTexture::Filtering_Bilinear;
	params.wrapping = OpenGLTexture::Wrapping_Clamp;
	return params;
}


}


float MapWorldLayer::getTileWidthWSForTileZ(int tile_z)
{
	return MapWorldUtils::getOSMTileWidthWSForTileZ(tile_z);
}


MapWorldLayer::MapWorldLayer(const Reference<OpenGLEngine>& opengl_engine_, GUIClient* gui_client_)
:	gui_client(gui_client_),
	opengl_engine(opengl_engine_),
	scratch_packet(SocketBufferOutStream::DontUseNetworkByteOrder),
	layer_visible(false)
{
	ImageMapUInt8Ref placeholder_map = new ImageMapUInt8(32, 32, 4);
	for(int y = 0; y < 32; ++y)
	for(int x = 0; x < 32; ++x)
	{
		const bool major_line = (x == 0) || (y == 0) || (x == 31) || (y == 31) || (x == 15) || (y == 15);
		const bool checker = (((x / 8) + (y / 8)) % 2) == 0;
		uint8* px = placeholder_map->getPixel(x, y);
		px[0] = major_line ? 172 : (checker ? 198 : 214);
		px[1] = major_line ? 184 : (checker ? 208 : 223);
		px[2] = major_line ? 192 : (checker ? 214 : 229);
		px[3] = 255;
	}
	tile_placeholder_tex = opengl_engine->getOrLoadOpenGLTextureForMap2D(
		OpenGLTextureKey(MAP_WORLD_TILE_PLACEHOLDER_TEX_KEY), *placeholder_map, makeMapTileTextureParams());

	const size_t num_layers = sizeof(MAP_WORLD_LAYER_SPECS) / sizeof(MAP_WORLD_LAYER_SPECS[0]);
	tile_layers.resize(num_layers);
	for(size_t layer_i = 0; layer_i < num_layers; ++layer_i)
	{
		MapWorldTileLayer& layer = tile_layers[layer_i];
		layer.tile_z = MAP_WORLD_LAYER_SPECS[layer_i].tile_z;
		layer.grid_res = MAP_WORLD_LAYER_SPECS[layer_i].grid_res;
		layer.z_offset_m = MAP_WORLD_LAYER_SPECS[layer_i].z_offset_m;
		layer.show_placeholder_while_loading = MAP_WORLD_LAYER_SPECS[layer_i].show_placeholder_while_loading;
		layer.tiles.resize(layer.grid_res, layer.grid_res);

		for(int y = 0; y < layer.grid_res; ++y)
		for(int x = 0; x < layer.grid_res; ++x)
		{
			MapWorldTile& tile = layer.tiles.elem(x, y);
			tile.gl_ob = opengl_engine->allocateObject();
			tile.gl_ob->mesh_data = opengl_engine->getUnitQuadMeshData();
			tile.gl_ob->materials.resize(1);
			tile.gl_ob->materials[0].albedo_linear_rgb = toLinearSRGB(Colour3f(1.f));
			tile.gl_ob->materials[0].albedo_texture = tile_placeholder_tex;
			tile.gl_ob->materials[0].roughness = 1.f;
			tile.gl_ob->materials[0].fresnel_scale = 0.f;
			tile.gl_ob->materials[0].cast_shadows = false;
			tile.gl_ob->materials[0].alpha_blend = true;
			tile.gl_ob->materials[0].alpha = getPlaceholderAlpha(layer);
			tile.gl_ob->materials[0].tex_matrix = Matrix2f(1, 0, 0, -1);
			tile.gl_ob->materials[0].tex_translation = Vec2f(0, 1);
		}
	}
}


MapWorldLayer::~MapWorldLayer()
{
	if(opengl_engine.nonNull())
	{
		for(size_t layer_i = 0; layer_i < tile_layers.size(); ++layer_i)
		{
			MapWorldTileLayer& layer = tile_layers[layer_i];
			for(int y = 0; y < layer.grid_res; ++y)
			for(int x = 0; x < layer.grid_res; ++x)
			{
				MapWorldTile& tile = layer.tiles.elem(x, y);
				if(tile.gl_ob.nonNull() && tile.in_engine)
				{
					opengl_engine->removeObject(tile.gl_ob);
					tile.in_engine = false;
					tile.gl_ob = NULL;
				}
			}
		}
	}
}


void MapWorldLayer::think()
{
	const bool active = gui_client && gui_client->isMetasiberiaMapWorld();
	if(!active)
	{
		if(layer_visible)
			setLayerVisible(false);
		return;
	}

	if(!layer_visible)
		setLayerVisible(true);

	std::vector<Vec3i> query_indices;
	query_indices.reserve(768);

	const Vec3d camera_pos = gui_client->cam_controller.getFirstPersonPosition();
	const double current_time = Clock::getTimeSinceInit();

	for(size_t layer_i = 0; layer_i < tile_layers.size(); ++layer_i)
	{
		MapWorldTileLayer& layer = tile_layers[layer_i];
		const int centre_tile_x = MapWorldUtils::getOSMTileXForLocalX(camera_pos.x, layer.tile_z);
		const int centre_tile_y = MapWorldUtils::getOSMTileYForLocalY(camera_pos.y, layer.tile_z);
		const bool centre_changed = (centre_tile_x != layer.last_centre_tile_x) || (centre_tile_y != layer.last_centre_tile_y);

		if(centre_changed)
		{
			layer.last_centre_tile_x = centre_tile_x;
			layer.last_centre_tile_y = centre_tile_y;
			refreshVisibleTileGrid(layer, /*force_full_refresh=*/true);
		}
		else
		{
			refreshVisibleTileGrid(layer, /*force_full_refresh=*/false);
		}

		collectTileQueriesForLayer(layer, centre_tile_x, centre_tile_y, current_time, query_indices);
	}

	if(!query_indices.empty())
	{
		const TextureParams tex_params = makeMapTileTextureParams();

		for(size_t i = 0; i < query_indices.size(); ++i)
		{
			const Vec3i indices = query_indices[i];
			const URLString URL = MapWorldUtils::makeOSMTileURL(gui_client->server_hostname, indices.x, indices.y, indices.z);
			if(URL.empty())
				continue;

			tile_infos[indices].image_URL = URL;

			const float tile_width_ws = getTileWidthWSForTileZ(indices.z);
			const Vec2d tile_centre = MapWorldUtils::getOSMTileCentreLocalCoords(indices.x, indices.y, indices.z);
			const Vec4f tile_centre_ws((float)tile_centre.x, (float)tile_centre.y, 0.f, 1.f);

			DownloadingResourceInfo downloading_info;
			downloading_info.texture_params = tex_params;
			downloading_info.pos = Vec3d(tile_centre.x, tile_centre.y, 0.0);
			downloading_info.size_factor = LoadItemQueueItem::sizeFactorForAABBWS(tile_width_ws, /*importance_factor=*/1.f);
			downloading_info.used_by_other = true;
			downloading_info.net_download_priority = 100 - indices.z;

			gui_client->startDownloadingResource(URL, tile_centre_ws, tile_width_ws, downloading_info);
			gui_client->startLoadingTextureIfPresent(URL, tile_centre_ws, tile_width_ws,
				/*max_task_dist=*/std::numeric_limits<float>::infinity(), /*importance_factor=*/1.f, tex_params);
		}
	}
}


void MapWorldLayer::handleMapTilesResultReceivedMessage(const MapTilesResultReceivedMessage& msg)
{
	(void)msg;
}


void MapWorldLayer::handleUploadedTexture(const OpenGLTextureKey& /*path*/, const URLString& URL, const Reference<OpenGLTexture>& opengl_tex)
{
	for(size_t layer_i = 0; layer_i < tile_layers.size(); ++layer_i)
	{
		MapWorldTileLayer& layer = tile_layers[layer_i];
		for(int y = 0; y < layer.grid_res; ++y)
		for(int x = 0; x < layer.grid_res; ++x)
		{
			MapWorldTile& tile = layer.tiles.elem(x, y);
			if(tile.image_URL == URL && tile.gl_ob.nonNull())
			{
				OpenGLMaterial& material = tile.gl_ob->materials[0];
				if(material.albedo_texture != opengl_tex || material.alpha != 1.f)
				{
					material.albedo_texture = opengl_tex;
					material.alpha = 1.f;
					if(tile.in_engine)
						opengl_engine->objectMaterialsUpdated(*tile.gl_ob);
				}
			}
		}
	}
}


void MapWorldLayer::setLayerVisible(bool visible)
{
	layer_visible = visible;

	for(size_t layer_i = 0; layer_i < tile_layers.size(); ++layer_i)
	{
		MapWorldTileLayer& layer = tile_layers[layer_i];
		for(int y = 0; y < layer.grid_res; ++y)
		for(int x = 0; x < layer.grid_res; ++x)
		{
			MapWorldTile& tile = layer.tiles.elem(x, y);
			if(tile.gl_ob.nonNull())
			{
				if(visible && !tile.in_engine)
				{
					opengl_engine->addObject(tile.gl_ob);
					tile.in_engine = true;
				}
				else if(!visible && tile.in_engine)
				{
					opengl_engine->removeObject(tile.gl_ob);
					tile.in_engine = false;
				}
			}
		}
	}
}


void MapWorldLayer::collectTileQueriesForLayer(const MapWorldTileLayer& layer, int centre_tile_x, int centre_tile_y, double current_time, std::vector<Vec3i>& query_indices)
{
	const int query_rad = layer.grid_res / 2;

	for(int x = centre_tile_x - query_rad; x <= centre_tile_x + query_rad; ++x)
	for(int y = centre_tile_y - query_rad; y <= centre_tile_y + query_rad; ++y)
	{
		Vec3i tile_coords(x, y, layer.tile_z);
		while(tile_coords.z >= 0)
		{
			if((int)query_indices.size() >= MAX_TILE_QUERY_COUNT)
				return;

			if(MapWorldUtils::isValidOSMTileCoord(tile_coords.x, tile_coords.y, tile_coords.z))
			{
				const URLString URL = MapWorldUtils::makeOSMTileURL(gui_client->server_hostname, tile_coords.x, tile_coords.y, tile_coords.z);
				tile_infos[tile_coords].image_URL = URL;

				ResourceRef resource = gui_client->resource_manager->getExistingResourceForURL(URL);
				const bool resource_present = resource.nonNull() && (resource->getState() == Resource::State_Present);
				if(resource_present)
				{
					next_tile_query_time.erase(tile_coords);
				}
				else
				{
					auto next_query_res = next_tile_query_time.find(tile_coords);
					if(next_query_res == next_tile_query_time.end() || current_time >= next_query_res->second)
					{
						query_indices.push_back(tile_coords);
						const double retry_interval_s = (tile_coords.z <= 13) ? 0.35 : 0.75;
						next_tile_query_time[tile_coords] = current_time + retry_interval_s;
					}
				}
			}

			tile_coords.x = Maths::divideByTwoRoundedDown(tile_coords.x);
			tile_coords.y = Maths::divideByTwoRoundedDown(tile_coords.y);
			tile_coords.z--;
		}
	}
}


void MapWorldLayer::refreshVisibleTileGrid(MapWorldTileLayer& layer, bool force_full_refresh)
{
	if(layer.last_centre_tile_x == std::numeric_limits<int>::min() || layer.last_centre_tile_y == std::numeric_limits<int>::min())
		return;

	const float tile_width_ws = getTileWidthWSForTileZ(layer.tile_z);
	const int tile_index_offset = layer.grid_res / 2;

	for(int y = 0; y < layer.grid_res; ++y)
	for(int x = 0; x < layer.grid_res; ++x)
	{
		const int dx = x - tile_index_offset;
		const int dy = y - tile_index_offset;
		const int tile_x = layer.last_centre_tile_x + dx;
		const int tile_y = layer.last_centre_tile_y + dy;

		MapWorldTile& tile = layer.tiles.elem(x, y);
		const bool tile_changed = force_full_refresh || (tile.tile_x != tile_x) || (tile.tile_y != tile_y) || (tile.tile_z != layer.tile_z);
		if(tile_changed)
			updateTile(layer, tile, tile_x, tile_y, tile_width_ws);
		else
			assignBestAvailableTexture(layer, tile, tile_width_ws);
	}
}


void MapWorldLayer::updateTile(MapWorldTileLayer& layer, MapWorldTile& tile, int tile_x, int tile_y, float tile_width_ws)
{
	const Vec2d tile_centre = MapWorldUtils::getOSMTileCentreLocalCoords(tile_x, tile_y, layer.tile_z);
	const Vec4f tile_centre_ws((float)tile_centre.x, (float)tile_centre.y, layer.z_offset_m, 1.f);

	tile.tile_x = tile_x;
	tile.tile_y = tile_y;
	tile.tile_z = layer.tile_z;
	tile.image_URL = URLString();
	tile.gl_ob->ob_to_world_matrix =
		Matrix4f::translationMatrix(tile_centre_ws) *
		Matrix4f::scaleMatrix(tile_width_ws, tile_width_ws, 1.f) *
		Matrix4f::translationMatrix(-0.5f, -0.5f, 0.f);

	applyPlaceholderToTile(layer, tile);

	if(tile.in_engine)
	{
		opengl_engine->updateObjectTransformData(*tile.gl_ob);
		opengl_engine->objectMaterialsUpdated(*tile.gl_ob);
	}

	assignBestAvailableTexture(layer, tile, tile_width_ws);
}


void MapWorldLayer::applyPlaceholderToTile(const MapWorldTileLayer& layer, MapWorldTile& tile)
{
	if(tile.gl_ob.isNull())
		return;

	OpenGLMaterial& material = tile.gl_ob->materials[0];
	material.albedo_texture = tile_placeholder_tex;
	material.alpha = getPlaceholderAlpha(layer);
	material.tex_matrix = Matrix2f(1, 0, 0, -1);
	material.tex_translation = Vec2f(0, 1);
}


void MapWorldLayer::assignBestAvailableTexture(MapWorldTileLayer& layer, MapWorldTile& tile, float tile_width_ws)
{
	if(tile.gl_ob.isNull())
		return;

	const TextureParams tex_params = makeMapTileTextureParams();
	const Vec2d tile_centre = MapWorldUtils::getOSMTileCentreLocalCoords(tile.tile_x, tile.tile_y, layer.tile_z);
	const Vec4f tile_centre_ws((float)tile_centre.x, (float)tile_centre.y, layer.z_offset_m, 1.f);

	URLString selected_URL;
	Vec2f selected_lower_left(0.f);
	float selected_tex_scale = 1.f;

	URLString first_missing_URL;
	Vec2f first_missing_lower_left(0.f);
	float first_missing_tex_scale = 1.f;

	int query_tile_x = tile.tile_x;
	int query_tile_y = tile.tile_y;
	Vec2f lower_left_coords(0.f);
	float tex_scale = 1.f;

	for(int z = layer.tile_z; z >= 0; --z)
	{
		if(MapWorldUtils::isValidOSMTileCoord(query_tile_x, query_tile_y, z))
		{
			const URLString candidate_URL = MapWorldUtils::makeOSMTileURL(gui_client->server_hostname, query_tile_x, query_tile_y, z);
			ResourceRef resource = gui_client->resource_manager->getExistingResourceForURL(candidate_URL);
			if(resource.nonNull() && (resource->getState() == Resource::State_Present))
			{
				const OpenGLTextureKey local_abs_tex_path(gui_client->resource_manager->getLocalAbsPathForResource(*resource));
				const Reference<OpenGLTexture> loaded_tex = opengl_engine->getTextureIfLoaded(local_abs_tex_path);
				if(loaded_tex.nonNull())
				{
					selected_URL = candidate_URL;
					selected_lower_left = lower_left_coords;
					selected_tex_scale = tex_scale;
					break;
				}
				else
				{
					gui_client->startLoadingTextureForLocalPath(local_abs_tex_path, resource, tile_centre_ws, tile_width_ws,
						/*max_task_dist=*/std::numeric_limits<float>::infinity(), /*importance_factor=*/1.f, tex_params);

					if(first_missing_URL.empty())
					{
						first_missing_URL = candidate_URL;
						first_missing_lower_left = lower_left_coords;
						first_missing_tex_scale = tex_scale;
					}
				}
			}
			else if(first_missing_URL.empty())
			{
				first_missing_URL = candidate_URL;
				first_missing_lower_left = lower_left_coords;
				first_missing_tex_scale = tex_scale;
			}
		}

		lower_left_coords *= 0.5f;
		lower_left_coords.x += (float)Maths::intMod(query_tile_x, 2) * 0.5f;
		lower_left_coords.y += (float)Maths::intMod(query_tile_y, 2) * 0.5f;

		query_tile_x = Maths::divideByTwoRoundedDown(query_tile_x);
		query_tile_y = Maths::divideByTwoRoundedDown(query_tile_y);
		tex_scale *= 0.5f;
	}

	OpenGLMaterial& material = tile.gl_ob->materials[0];
	const URLString use_URL = selected_URL.empty() ? first_missing_URL : selected_URL;
	const Vec2f use_lower_left = selected_URL.empty() ? first_missing_lower_left : selected_lower_left;
	const float use_tex_scale = selected_URL.empty() ? first_missing_tex_scale : selected_tex_scale;

	if(use_URL.empty())
	{
		if(!tile.image_URL.empty())
		{
			tile.image_URL = URLString();
			applyPlaceholderToTile(layer, tile);
			if(tile.in_engine)
				opengl_engine->objectMaterialsUpdated(*tile.gl_ob);
		}
		return;
	}

	const bool selected_URL_changed = (tile.image_URL != use_URL);
	tile.image_URL = use_URL;
	material.tex_matrix = Matrix2f(use_tex_scale, 0, 0, -use_tex_scale);
	material.tex_translation = Vec2f(use_lower_left.x, 1.f - use_lower_left.y);

	ResourceRef resource = gui_client->resource_manager->getExistingResourceForURL(use_URL);
	if(!selected_URL.empty() && resource.nonNull() && (resource->getState() == Resource::State_Present))
	{
		const OpenGLTextureKey local_abs_tex_path(gui_client->resource_manager->getLocalAbsPathForResource(*resource));
		const Reference<OpenGLTexture> loaded_tex = opengl_engine->getTextureIfLoaded(local_abs_tex_path);
		if(loaded_tex.nonNull())
		{
			if(selected_URL_changed || material.albedo_texture != loaded_tex || material.alpha != 1.f)
			{
				material.albedo_texture = loaded_tex;
				material.alpha = 1.f;
				if(tile.in_engine)
					opengl_engine->objectMaterialsUpdated(*tile.gl_ob);
			}
			return;
		}
	}

	if(resource.isNull() || resource->getState() != Resource::State_Present)
	{
		DownloadingResourceInfo downloading_info;
		downloading_info.texture_params = tex_params;
		downloading_info.pos = Vec3d(tile_centre.x, tile_centre.y, layer.z_offset_m);
		downloading_info.size_factor = LoadItemQueueItem::sizeFactorForAABBWS(tile_width_ws, /*importance_factor=*/1.f);
		downloading_info.used_by_other = true;
		downloading_info.net_download_priority = 100 - layer.tile_z;
		gui_client->startDownloadingResource(use_URL, tile_centre_ws, tile_width_ws, downloading_info);
	}

	applyPlaceholderToTile(layer, tile);
	material.tex_matrix = Matrix2f(use_tex_scale, 0, 0, -use_tex_scale);
	material.tex_translation = Vec2f(use_lower_left.x, 1.f - use_lower_left.y);
	if(tile.in_engine)
		opengl_engine->objectMaterialsUpdated(*tile.gl_ob);
}
