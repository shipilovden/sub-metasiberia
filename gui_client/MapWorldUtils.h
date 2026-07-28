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
	static const double WEB_MERCATOR_MAX_LAT_DEG = 85.05112878;
	static const char* const MAP_TILE_CACHE_NAMESPACE = "metasiberia_raster_v4";


	inline double clampLatitudeForWebMercator(double lat_deg)
	{
		return myClamp(lat_deg, -WEB_MERCATOR_MAX_LAT_DEG, WEB_MERCATOR_MAX_LAT_DEG);
	}


	inline Vec2d latLonToMercatorMetres(double lat_deg, double lon_deg)
	{
		const double lat_rad = clampLatitudeForWebMercator(lat_deg) * PI / 180.0;
		const double lon_rad = lon_deg * PI / 180.0;
		return Vec2d(
			WEB_MERCATOR_EARTH_RADIUS_M * lon_rad,
			WEB_MERCATOR_EARTH_RADIUS_M * std::log(std::tan(PI * 0.25 + lat_rad * 0.5))
		);
	}


	inline void mercatorMetresToLatLon(const Vec2d& mercator_m, double& lat_deg_out, double& lon_deg_out)
	{
		lon_deg_out = mercator_m.x / WEB_MERCATOR_EARTH_RADIUS_M * 180.0 / PI;
		lat_deg_out = (2.0 * std::atan(std::exp(mercator_m.y / WEB_MERCATOR_EARTH_RADIUS_M)) - PI * 0.5) * 180.0 / PI;
	}


	inline Vec2d getMapWorldCentreMercatorMetres()
	{
		return latLonToMercatorMetres(MAP_WORLD_CENTRE_LAT_DEG, MAP_WORLD_CENTRE_LON_DEG);
	}


	inline Vec2d latLonToLocalCoords(double lat_deg, double lon_deg)
	{
		return latLonToMercatorMetres(lat_deg, lon_deg) - getMapWorldCentreMercatorMetres();
	}


	inline void localCoordsToLatLon(double local_x, double local_y, double& lat_deg_out, double& lon_deg_out)
	{
		mercatorMetresToLatLon(getMapWorldCentreMercatorMetres() + Vec2d(local_x, local_y), lat_deg_out, lon_deg_out);
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
		return URLString("https://" + host + "/osm_tile/" + MAP_TILE_CACHE_NAMESPACE + "/" + toString(tile_z) + "/" + toString(tile_x) + "/" + toString(tile_y) + ".png");
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
