/*=====================================================================
MapWorldUtils.h
---------------
Shared helpers for the Metasiberia real-map world.
=====================================================================*/
#pragma once


#include "../shared/URLString.h"
#include <maths/mathstypes.h>
#include <maths/vec2.h>
#include <utils/StringUtils.h>
#include <cmath>
#include <string>


namespace MapWorldUtils
{
	static const double MAP_WORLD_CENTRE_LAT_DEG = 53.691717;
	static const double MAP_WORLD_CENTRE_LON_DEG = 87.432949;
	static const double WEB_MERCATOR_EARTH_RADIUS_M = 6378137.0;
	static const double PI = NICKMATHS_PI;
	static const double WEB_MERCATOR_HALF_EXTENT_M = PI * WEB_MERCATOR_EARTH_RADIUS_M;


	inline Vec2d getMapWorldCentreMercatorMetres()
	{
		const double lat_rad = MAP_WORLD_CENTRE_LAT_DEG * PI / 180.0;
		const double lon_rad = MAP_WORLD_CENTRE_LON_DEG * PI / 180.0;
		return Vec2d(
			WEB_MERCATOR_EARTH_RADIUS_M * lon_rad,
			WEB_MERCATOR_EARTH_RADIUS_M * std::log(std::tan(PI * 0.25 + lat_rad * 0.5))
		);
	}


	inline bool isValidOSMTileCoord(int tile_x, int tile_y, int tile_z)
	{
		if(tile_z < 0 || tile_z > 19)
			return false;

		const int n = 1 << tile_z;
		return tile_x >= 0 && tile_x < n && tile_y >= 0 && tile_y < n;
	}


	inline URLString makeOSMTileURL(const std::string& server_hostname, int tile_x, int tile_y, int tile_z)
	{
		if(!isValidOSMTileCoord(tile_x, tile_y, tile_z))
			return URLString();

		const std::string host = server_hostname.empty() ? "vr.metasiberia.com" : server_hostname;
		return URLString("https://" + host + "/osm_tile/" + toString(tile_z) + "/" + toString(tile_x) + "/" + toString(tile_y) + ".png?v=carto_v1");
	}


	inline float getOSMTileWidthWSForTileZ(int tile_z)
	{
		return (float)(2.0 * WEB_MERCATOR_HALF_EXTENT_M / (double)(1 << tile_z));
	}


	inline int getOSMTileZForMapWidthWS(float map_width_ws)
	{
		return myClamp((int)std::log2(2.0 * 2.0 * WEB_MERCATOR_HALF_EXTENT_M / (double)map_width_ws), 0, 19);
	}


	inline Vec2d getOSMTileCentreLocalCoords(int tile_x, int tile_y, int tile_z)
	{
		const double tile_width_m = 2.0 * WEB_MERCATOR_HALF_EXTENT_M / (double)(1 << tile_z);
		const Vec2d centre_mercator = getMapWorldCentreMercatorMetres();
		const double tile_min_merc_x = -WEB_MERCATOR_HALF_EXTENT_M + (double)tile_x * tile_width_m;
		const double tile_max_merc_y =  WEB_MERCATOR_HALF_EXTENT_M - (double)tile_y * tile_width_m;

		return Vec2d(
			tile_min_merc_x - centre_mercator.x + tile_width_m * 0.5,
			tile_max_merc_y - centre_mercator.y - tile_width_m * 0.5
		);
	}


	inline int getOSMTileXForLocalX(double local_x, int tile_z)
	{
		const Vec2d centre_mercator = getMapWorldCentreMercatorMetres();
		const double tile_width_m = 2.0 * WEB_MERCATOR_HALF_EXTENT_M / (double)(1 << tile_z);
		return Maths::floorToInt((float)((centre_mercator.x + local_x + WEB_MERCATOR_HALF_EXTENT_M) / tile_width_m));
	}


	inline int getOSMTileYForLocalY(double local_y, int tile_z)
	{
		const Vec2d centre_mercator = getMapWorldCentreMercatorMetres();
		const double tile_width_m = 2.0 * WEB_MERCATOR_HALF_EXTENT_M / (double)(1 << tile_z);
		return Maths::floorToInt((float)((WEB_MERCATOR_HALF_EXTENT_M - (centre_mercator.y + local_y)) / tile_width_m));
	}
}
