/*=====================================================================
MapWorldLayer.h
---------------
Copyright Glare Technologies Limited 2026 -
=====================================================================*/
#pragma once


#include "../shared/URLString.h"
#include <maths/vec3.h>
#include <opengl/OpenGLTexture.h>
#include <utils/Array2D.h>
#include <utils/SocketBufferOutStream.h>
#include <utils/ThreadSafeRefCounted.h>
#include <limits>
#include <map>
#include <vector>


class GUIClient;
class OpenGLEngine;
class OpenGLTexture;
class MapTilesResultReceivedMessage;
struct GLObject;


struct MapWorldTile
{
	MapWorldTile()
	: tile_x(std::numeric_limits<int>::min()),
	  tile_y(std::numeric_limits<int>::min()),
	  tile_z(std::numeric_limits<int>::min()),
	  in_engine(false)
	{}

	Reference<GLObject> gl_ob;
	URLString image_URL;
	int tile_x;
	int tile_y;
	int tile_z;
	bool in_engine;
};


struct MapWorldTileLayer
{
	MapWorldTileLayer()
	: tile_z(0),
	  grid_res(0),
	  z_offset_m(0.f),
	  show_placeholder_while_loading(false),
	  last_centre_tile_x(std::numeric_limits<int>::min()),
	  last_centre_tile_y(std::numeric_limits<int>::min())
	{}

	int tile_z;
	int grid_res;
	float z_offset_m;
	bool show_placeholder_while_loading;
	Array2D<MapWorldTile> tiles;
	int last_centre_tile_x;
	int last_centre_tile_y;
};


struct MapWorldTileInfo
{
	URLString image_URL;
};


class MapWorldLayer : public ThreadSafeRefCounted
{
public:
	MapWorldLayer(const Reference<OpenGLEngine>& opengl_engine_, GUIClient* gui_client_);
	~MapWorldLayer();

	void think();
	void handleUploadedTexture(const OpenGLTextureKey& path, const URLString& URL, const Reference<OpenGLTexture>& opengl_tex);
	void handleMapTilesResultReceivedMessage(const MapTilesResultReceivedMessage& msg);

private:
	void setLayerVisible(bool visible);
	void collectTileQueriesForLayer(const MapWorldTileLayer& layer, int centre_tile_x, int centre_tile_y, double current_time, std::vector<Vec3i>& query_indices);
	void refreshVisibleTileGrid(MapWorldTileLayer& layer, bool force_full_refresh);
	void updateTile(MapWorldTileLayer& layer, MapWorldTile& tile, int tile_x, int tile_y, float tile_width_ws);
	void applyPlaceholderToTile(const MapWorldTileLayer& layer, MapWorldTile& tile);
	void assignBestAvailableTexture(MapWorldTileLayer& layer, MapWorldTile& tile, float tile_width_ws);

	static float getTileWidthWSForTileZ(int tile_z);

	GUIClient* gui_client;
	Reference<OpenGLEngine> opengl_engine;

	std::vector<MapWorldTileLayer> tile_layers;
	Reference<OpenGLTexture> tile_placeholder_tex;
	std::map<Vec3i, MapWorldTileInfo> tile_infos;
	std::map<Vec3i, double> next_tile_query_time;
	SocketBufferOutStream scratch_packet;

	bool layer_visible;
};
