/*=====================================================================
WorldSettings.h
---------------
Copyright Glare Technologies Limited 2023 -
=====================================================================*/
#pragma once


#include "DependencyURL.h"
#include "TimeStamp.h"
#include <vec3.h>
#include <vec2.h>
#include <graphics/colour3.h>
#include <OutStream.h>
#include <InStream.h>
#include <DatabaseKey.h>
#include <string>
#include <vector>
#include <set>
class RandomAccessInStream;
class RandomAccessOutStream;


struct TerrainSpecSection
{
	bool operator == (const TerrainSpecSection& other) const;

	int x, y; // section coordinates.  (0,0) is section centered on world origin.

	URLString heightmap_URL;
	URLString mask_map_URL;
	URLString tree_mask_map_URL;
	URLString road_mask_map_URL;
	URLString building_mask_map_URL;

	// A set bit means that the corresponding map is not used for rendering.
	// Zero keeps all maps enabled, including for worlds written by older clients.
	static const uint32 HEIGHTMAP_DISABLED_FLAG = 1;
	static const uint32 MASK_MAP_DISABLED_FLAG = 2;
	static const uint32 TREE_MASK_MAP_DISABLED_FLAG = 4;
	static const uint32 ROAD_MASK_MAP_DISABLED_FLAG = 8;
	static const uint32 BUILDING_MASK_MAP_DISABLED_FLAG = 16;
	uint32 disabled_map_flags;
};

struct TerrainSpec
{
	bool operator == (const TerrainSpec& other) const;

	std::vector<TerrainSpecSection> section_specs;

	URLString detail_col_map_URLs[4];
	URLString detail_height_map_URLs[4];

	float terrain_section_width_m;
	float terrain_height_scale; // Multiplier applied to raw heightmap values. Default value is 1.
	float water_z;
	float default_terrain_z;

	// A set bit disables the corresponding detail map while retaining its URL,
	// so it can be switched back on without selecting the file again.
	static const uint32 DETAIL_COL_MAP_0_DISABLED_FLAG = 1;
	static const uint32 DETAIL_COL_MAP_1_DISABLED_FLAG = 2;
	static const uint32 DETAIL_COL_MAP_2_DISABLED_FLAG = 4;
	static const uint32 DETAIL_COL_MAP_3_DISABLED_FLAG = 8;
	static const uint32 DETAIL_HEIGHT_MAP_0_DISABLED_FLAG = 16;
	uint32 disabled_detail_map_flags;

	static const uint32 WATER_ENABLED_FLAG = 1;
	// Use the heightmap values directly, without procedural vegetation/rock displacement.
	// This is required when the terrain represents a digital twin.
	static const uint32 EXACT_HEIGHTMAP_FLAG = 2;
	uint32 flags;
};


struct FogWorldSettings
{
	FogWorldSettings();

	// Fog sigma_t is A exp(-B z)
	float layer_0_A;
	float layer_0_scale_height; // = 1 / B
	float layer_1_A;
	float layer_1_scale_height; // = 1 / B

	void writeToStream(RandomAccessOutStream& stream) const;
};
void readFogWorldSettingsFromStream(RandomAccessInStream& stream, FogWorldSettings& fog_settings_out);


struct VolumetricCloudWorldSettings
{
	VolumetricCloudWorldSettings();

	bool enabled;
	float bottom_z;
	float top_z;
	float coverage;
	float density;
	float wind_speed;
	float edge_softness;
	float horizon_fade;
	float shape_period;
	float detail_period;
	float max_march_dist;
	float wind_direction_deg;
};


// Lighting controls are kept separate from the cloud shape and animation
// controls so that the same atmosphere can be lit consistently at different
// times of day without mixing it with water rendering settings.
struct CloudLightingWorldSettings
{
	CloudLightingWorldSettings();

	float direct_sun_strength;
	float sky_light_strength;
	float sunset_response;
	float ground_contribution;
	Colour3f ground_albedo;
	float phase_g;
	float phase_blend;
	float multi_scattering;
	float underside_darkness;
	float scattering_scale;
};


struct WaterReflectionWorldSettings
{
	WaterReflectionWorldSettings();

	bool cloud_reflection_enabled;
	float cloud_reflection_strength;
	float cloud_reflection_samples;
	float cloud_reflection_fade;
};


/*=====================================================================
WorldSettings
-------------

=====================================================================*/
class WorldSettings
{
public:
	WorldSettings();
	~WorldSettings();

	GLARE_ALIGNED_16_NEW_DELETE

	void clear();

	// NOTE: not setting use_sRGB etc. in DependencyURLs, as this getDependencyURLSet method is just used on the server currently, which doesn't need those.
	void getDependencyURLSet(std::set<DependencyURL>& URLs_out);

	void writeToStream(OutStream& stream) const;

	void copyNetworkStateFrom(const WorldSettings& other);

	TerrainSpec terrain_spec;

	float sun_theta;
	float sun_phi;

	FogWorldSettings fog_settings;
	VolumetricCloudWorldSettings volumetric_cloud_settings;
	CloudLightingWorldSettings cloud_lighting_settings;
	WaterReflectionWorldSettings water_reflection_settings;

	DatabaseKey database_key;
	bool db_dirty; // If true, there is a change that has not been saved to the DB.

private:
	GLARE_DISABLE_COPY(WorldSettings)
};


void readWorldSettingsFromStream(InStream& stream, WorldSettings& settings);
