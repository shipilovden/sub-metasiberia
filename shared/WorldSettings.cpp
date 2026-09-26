/*=====================================================================
WorldSettings.cpp
-----------------
Copyright Glare Technologies Limited 2023 -
=====================================================================*/
#include "WorldSettings.h"


#include <Exception.h>
#include <StringUtils.h>
#include <ConPrint.h>
#include <BufferOutStream.h>
#include <BufferInStream.h>
#include <RuntimeCheck.h>


static const uint32 FOG_WORLD_SETTINGS_VERSION = 1;


FogWorldSettings::FogWorldSettings()
{
	layer_0_A = 0;
	layer_0_scale_height = 1;
	layer_1_A = 0;
	layer_1_scale_height = 1;
}


void FogWorldSettings::writeToStream(RandomAccessOutStream& stream) const
{
	const size_t initial_write_index = stream.getWriteIndex();

	stream.writeUInt32(FOG_WORLD_SETTINGS_VERSION);
	stream.writeUInt32(0); // Size of buffer will be written here later

	stream.writeFloat(layer_0_A);
	stream.writeFloat(layer_0_scale_height);
	stream.writeFloat(layer_1_A);
	stream.writeFloat(layer_1_scale_height);

	const uint32 buffer_size = (uint32)(stream.getWriteIndex() - initial_write_index);
	std::memcpy(stream.getWritePtrAtIndex(initial_write_index + sizeof(uint32)), &buffer_size, sizeof(uint32));
}


void readFogWorldSettingsFromStream(RandomAccessInStream& stream, FogWorldSettings& fog_settings_out)
{
	const size_t initial_read_index = stream.getReadIndex();

	/*const uint32 version =*/ stream.readUInt32();
	const size_t buffer_size = stream.readUInt32();

	checkProperty(buffer_size >= 8ul,        "readFogWorldSettingsFromStream: buffer_size was too small");
	checkProperty(buffer_size <= 10000000ul, "readFogWorldSettingsFromStream: buffer_size was too large");

	fog_settings_out.layer_0_A            = stream.readFloat();
	fog_settings_out.layer_0_scale_height = stream.readFloat();
	fog_settings_out.layer_1_A            = stream.readFloat();
	fog_settings_out.layer_1_scale_height = stream.readFloat();

	const size_t read_B = stream.getReadIndex() - initial_read_index;
	if(read_B < buffer_size)
		stream.advanceReadIndex(buffer_size - read_B);
}


VolumetricCloudWorldSettings::VolumetricCloudWorldSettings()
:
	enabled(false),
	bottom_z(1000.f),
	top_z(2200.f),
	coverage(0.48f),
	density(0.0012f),
	wind_speed(20.f),
	edge_softness(0.55f),
	horizon_fade(0.75f),
	shape_period(10000.f),
	detail_period(1200.f),
	max_march_dist(40000.f),
	wind_direction_deg(20.f)
{}


CloudLightingWorldSettings::CloudLightingWorldSettings()
:
	direct_sun_strength(1.f),
	sky_light_strength(1.f),
	sunset_response(1.f),
	ground_contribution(0.18f),
	ground_albedo(0.35f, 0.32f, 0.28f),
	phase_g(0.55f),
	phase_blend(0.2f),
	multi_scattering(0.25f),
	underside_darkness(0.4f),
	scattering_scale(1.f)
{}


WaterReflectionWorldSettings::WaterReflectionWorldSettings()
:
	cloud_reflection_enabled(false),
	cloud_reflection_strength(0.65f),
	cloud_reflection_samples(24.f),
	cloud_reflection_fade(0.75f)
{}


WaterSurfaceWorldSettings::WaterSurfaceWorldSettings()
:
	wave_amplitude(0.45f),
	wave_length(28.f),
	wave_steepness(0.18f),
	wave_speed(1.0f),
	wave_direction_deg(68.4f),
	wave_direction_spread_deg(25.f),
	secondary_wave_scale(0.18f),
	surf_enabled(false),
	surf_strength(0.28f),
	shoreline_width(0.45f),
	foam_scale(1.6f),
	foam_speed(0.15f),
	foam_fade(0.75f)
{}


WorldSettings::WorldSettings()
{
	terrain_spec.terrain_section_width_m = 8192;
	terrain_spec.terrain_height_scale = 1.f;
	terrain_spec.water_z = -4;
	terrain_spec.default_terrain_z = 0;
	terrain_spec.disabled_detail_map_flags = 0;
	terrain_spec.flags = 0;

	sun_phi = 1.f;
	sun_theta = Maths::pi<float>() / 4;

	db_dirty = false;
}


WorldSettings::~WorldSettings()
{
}


void WorldSettings::clear()
{
	terrain_spec = TerrainSpec();

	fog_settings = FogWorldSettings();
	volumetric_cloud_settings = VolumetricCloudWorldSettings();
	cloud_lighting_settings = CloudLightingWorldSettings();
	water_reflection_settings = WaterReflectionWorldSettings();
	water_surface_settings = WaterSurfaceWorldSettings();
}


void WorldSettings::getDependencyURLSet(std::set<DependencyURL>& URLs_out)
{
	for(size_t i=0; i<terrain_spec.section_specs.size(); ++i)
	{
		const TerrainSpecSection& section_spec = terrain_spec.section_specs[i];

		if(!section_spec.heightmap_URL.empty())
			URLs_out.insert(DependencyURL(section_spec.heightmap_URL));
		if(!section_spec.mask_map_URL.empty())
			URLs_out.insert(DependencyURL(section_spec.mask_map_URL));
		if(!section_spec.tree_mask_map_URL.empty())
			URLs_out.insert(DependencyURL(section_spec.tree_mask_map_URL));
		if(!section_spec.road_mask_map_URL.empty())
			URLs_out.insert(DependencyURL(section_spec.road_mask_map_URL));
		if(!section_spec.building_mask_map_URL.empty())
			URLs_out.insert(DependencyURL(section_spec.building_mask_map_URL));
	}

	for(int i=0; i<4; ++i)
		if(!terrain_spec.detail_col_map_URLs[i].empty())
			URLs_out.insert(DependencyURL(terrain_spec.detail_col_map_URLs[i]));

	for(int i=0; i<4; ++i)
		if(!terrain_spec.detail_height_map_URLs[i].empty())
			URLs_out.insert(DependencyURL(terrain_spec.detail_height_map_URLs[i]));
}


// v14 marks the new presentation defaults.  The payload is unchanged; the
// version lets readers distinguish old v13 defaults from explicit settings.
static const uint32 WORLDSETTINGS_SERIALISATION_VERSION = 14;


void WorldSettings::writeToStream(OutStream& stream) const
{
	BufferOutStream buffer;
	buffer.buf.reserve(4096);

	buffer.writeUInt32(WORLDSETTINGS_SERIALISATION_VERSION);
	buffer.writeUInt32(0); // Size of buffer will be written here later
	
	buffer.writeUInt32((uint32)terrain_spec.section_specs.size());
	for(size_t i=0; i<terrain_spec.section_specs.size(); ++i)
	{
		const TerrainSpecSection& section_spec = terrain_spec.section_specs[i];

		buffer.writeInt32(section_spec.x);
		buffer.writeInt32(section_spec.y);
		buffer.writeStringLengthFirst(section_spec.heightmap_URL);
		buffer.writeStringLengthFirst(section_spec.mask_map_URL);
	}

	for(int i=0; i<4; ++i)
		buffer.writeStringLengthFirst(terrain_spec.detail_col_map_URLs[i]);
	for(int i=0; i<4; ++i)
		buffer.writeStringLengthFirst(terrain_spec.detail_height_map_URLs[i]);

	buffer.writeFloat(terrain_spec.terrain_section_width_m);
	buffer.writeFloat(terrain_spec.water_z);
	buffer.writeUInt32(terrain_spec.flags);

	buffer.writeFloat(terrain_spec.default_terrain_z); // New in v2

	// New in v3: Write tree_mask_map_URLs
	for(size_t i=0; i<terrain_spec.section_specs.size(); ++i)
	{
		const TerrainSpecSection& section_spec = terrain_spec.section_specs[i];
		buffer.writeStringLengthFirst(section_spec.tree_mask_map_URL);
	}

	buffer.writeFloat(sun_theta);
	buffer.writeFloat(sun_phi);

	buffer.writeFloat(terrain_spec.terrain_height_scale); // New in v5

	fog_settings.writeToStream(buffer);

	// New in v7: append independent reference masks after the v1-v6 payload.
	// Keeping the extension at the end lets older servers read all legacy fields
	// correctly and skip the additional bytes.
	for(size_t i=0; i<terrain_spec.section_specs.size(); ++i)
	{
		const TerrainSpecSection& section_spec = terrain_spec.section_specs[i];
		buffer.writeStringLengthFirst(section_spec.road_mask_map_URL);
		buffer.writeStringLengthFirst(section_spec.building_mask_map_URL);
	}

	// New in v8: append map enable/disable flags after the v1-v7 payload.
	for(size_t i=0; i<terrain_spec.section_specs.size(); ++i)
		buffer.writeUInt32(terrain_spec.section_specs[i].disabled_map_flags);
	buffer.writeUInt32(terrain_spec.disabled_detail_map_flags);

	// New in v9: volumetric cloud settings are part of the world state.
	buffer.writeUInt32(volumetric_cloud_settings.enabled ? 1u : 0u);
	buffer.writeFloat(volumetric_cloud_settings.bottom_z);
	buffer.writeFloat(volumetric_cloud_settings.top_z);
	buffer.writeFloat(volumetric_cloud_settings.coverage);
	buffer.writeFloat(volumetric_cloud_settings.density);
	buffer.writeFloat(volumetric_cloud_settings.wind_speed);
	buffer.writeFloat(cloud_lighting_settings.underside_darkness); // Legacy v10 slot.
	buffer.writeFloat(volumetric_cloud_settings.edge_softness);
	buffer.writeFloat(volumetric_cloud_settings.horizon_fade);
	buffer.writeFloat(volumetric_cloud_settings.shape_period);
	buffer.writeFloat(volumetric_cloud_settings.detail_period);
	buffer.writeFloat(volumetric_cloud_settings.max_march_dist);
	buffer.writeFloat(volumetric_cloud_settings.wind_direction_deg);
	buffer.writeFloat(cloud_lighting_settings.scattering_scale); // Legacy v11 slot.
	buffer.writeFloat(water_reflection_settings.cloud_reflection_strength); // Legacy v11 slot.

	// New in v12: keep cloud lighting and water reflection settings in their
	// own extensions.  The legacy slots above remain populated so older clients
	// still render a sensible version of the world.
	buffer.writeFloat(cloud_lighting_settings.direct_sun_strength);
	buffer.writeFloat(cloud_lighting_settings.sky_light_strength);
	buffer.writeFloat(cloud_lighting_settings.sunset_response);
	buffer.writeFloat(cloud_lighting_settings.ground_contribution);
	buffer.writeFloat(cloud_lighting_settings.ground_albedo.r);
	buffer.writeFloat(cloud_lighting_settings.ground_albedo.g);
	buffer.writeFloat(cloud_lighting_settings.ground_albedo.b);
	buffer.writeFloat(cloud_lighting_settings.phase_g);
	buffer.writeFloat(cloud_lighting_settings.phase_blend);
	buffer.writeFloat(cloud_lighting_settings.multi_scattering);
	buffer.writeFloat(cloud_lighting_settings.underside_darkness);
	buffer.writeFloat(cloud_lighting_settings.scattering_scale);
	buffer.writeUInt32(water_reflection_settings.cloud_reflection_enabled ? 1u : 0u);
	buffer.writeFloat(water_reflection_settings.cloud_reflection_strength);
	buffer.writeFloat(water_reflection_settings.cloud_reflection_samples);
	buffer.writeFloat(water_reflection_settings.cloud_reflection_fade);

	// New in v13: physically based surface waves and near-shore surf settings.
	buffer.writeFloat(water_surface_settings.wave_amplitude);
	buffer.writeFloat(water_surface_settings.wave_length);
	buffer.writeFloat(water_surface_settings.wave_steepness);
	buffer.writeFloat(water_surface_settings.wave_speed);
	buffer.writeFloat(water_surface_settings.wave_direction_deg);
	buffer.writeFloat(water_surface_settings.wave_direction_spread_deg);
	buffer.writeFloat(water_surface_settings.secondary_wave_scale);
	buffer.writeUInt32(water_surface_settings.surf_enabled ? 1u : 0u);
	buffer.writeFloat(water_surface_settings.surf_strength);
	buffer.writeFloat(water_surface_settings.shoreline_width);
	buffer.writeFloat(water_surface_settings.foam_scale);
	buffer.writeFloat(water_surface_settings.foam_speed);
	buffer.writeFloat(water_surface_settings.foam_fade);

	// Go back and write size of buffer to buffer size field
	const uint32 buffer_size = (uint32)buffer.buf.size();
	std::memcpy(buffer.buf.data() + sizeof(uint32), &buffer_size, sizeof(uint32));

	// Write buffer to actual output stream
	stream.writeData(buffer.buf.data(), buffer.buf.size());
}


void WorldSettings::copyNetworkStateFrom(const WorldSettings& other)
{
	terrain_spec = other.terrain_spec;

	sun_theta = other.sun_theta;
	sun_phi   = other.sun_phi;

	fog_settings = other.fog_settings;
	volumetric_cloud_settings = other.volumetric_cloud_settings;
	cloud_lighting_settings = other.cloud_lighting_settings;
	water_reflection_settings = other.water_reflection_settings;
	water_surface_settings = other.water_surface_settings;
}


void readWorldSettingsFromStream(InStream& stream_, WorldSettings& settings)
{
	const uint32 version = stream_.readUInt32();
	const uint32 buffer_size = stream_.readUInt32();

	checkProperty(buffer_size >= 8ul, "WorldSettings readFromStream: buffer_size was too small");
	checkProperty(buffer_size <= 131072ul, "WorldSettings readFromStream: buffer_size was too large");

	// Read rest of data to buffer
	const uint32 remaining_buffer_size = buffer_size - sizeof(uint32)*2;

	BufferInStream buffer_stream;
	buffer_stream.buf.resize(remaining_buffer_size);

	stream_.readData(buffer_stream.buf.data(), remaining_buffer_size);

	// Read terrain spec sections
	const uint32 num_section_specs = buffer_stream.readUInt32();
	checkProperty(num_section_specs <= 4096, "num_section_specs was too large");

	settings.terrain_spec.section_specs.resize(num_section_specs);
	for(uint32 i=0; i<num_section_specs; ++i)
	{
		TerrainSpecSection& section_spec = settings.terrain_spec.section_specs[i];

		section_spec.x = buffer_stream.readInt32();
		section_spec.y = buffer_stream.readInt32();
		section_spec.heightmap_URL = buffer_stream.readStringLengthFirst(/*max_string_length=*/1024);
		section_spec.mask_map_URL  = buffer_stream.readStringLengthFirst(/*max_string_length=*/1024);
		section_spec.disabled_map_flags = 0;
		section_spec.road_mask_map_URL.clear();
		section_spec.building_mask_map_URL.clear();
	}

	for(int i=0; i<4; ++i)
		settings.terrain_spec.detail_col_map_URLs[i]    = buffer_stream.readStringLengthFirst(/*max_string_length=*/1024);
	for(int i=0; i<4; ++i)
		settings.terrain_spec.detail_height_map_URLs[i] = buffer_stream.readStringLengthFirst(/*max_string_length=*/1024);

	settings.terrain_spec.terrain_section_width_m = buffer_stream.readFloat();
	settings.terrain_spec.water_z = buffer_stream.readFloat();
	settings.terrain_spec.flags = buffer_stream.readUInt32();

	if(version >= 2)
		settings.terrain_spec.default_terrain_z = buffer_stream.readFloat();

	if(version >= 3)
	{
		// Read tree_mask_map_URLs
		for(uint32 i=0; i<num_section_specs; ++i)
		{
			TerrainSpecSection& section_spec = settings.terrain_spec.section_specs[i];
			section_spec.tree_mask_map_URL = buffer_stream.readStringLengthFirst(/*max_string_length=*/1024);
		}
	}

	if(version >= 4)
	{
		settings.sun_theta = buffer_stream.readFloat();
		settings.sun_phi   = buffer_stream.readFloat();
	}

	if(version >= 5)
		settings.terrain_spec.terrain_height_scale = buffer_stream.readFloat();

	if(version >= 6)
		readFogWorldSettingsFromStream(buffer_stream, settings.fog_settings);

	if(version >= 7 && !buffer_stream.endOfStream())
	{
		for(uint32 i=0; i<num_section_specs; ++i)
		{
			TerrainSpecSection& section_spec = settings.terrain_spec.section_specs[i];
			section_spec.road_mask_map_URL = buffer_stream.readStringLengthFirst(/*max_string_length=*/1024);
			section_spec.building_mask_map_URL = buffer_stream.readStringLengthFirst(/*max_string_length=*/1024);
		}
	}

	settings.terrain_spec.disabled_detail_map_flags = 0;
	if(version >= 8 && !buffer_stream.endOfStream())
	{
		for(uint32 i=0; i<num_section_specs; ++i)
			settings.terrain_spec.section_specs[i].disabled_map_flags = buffer_stream.readUInt32();
		settings.terrain_spec.disabled_detail_map_flags = buffer_stream.readUInt32();
	}

	// Defaults keep worlds written by older clients compatible.  The cloud
	// extension is appended after all v8 fields so old readers can ignore it.
	settings.volumetric_cloud_settings = VolumetricCloudWorldSettings();
	settings.cloud_lighting_settings = CloudLightingWorldSettings();
	settings.water_reflection_settings = WaterReflectionWorldSettings();
	settings.water_surface_settings = WaterSurfaceWorldSettings();
	const size_t volumetric_cloud_payload_size = sizeof(uint32) + sizeof(float) * 5;
	const size_t remaining_bytes = buffer_stream.buf.size() - buffer_stream.getReadIndex();
	if(version >= 9 && remaining_bytes >= volumetric_cloud_payload_size)
	{
		settings.volumetric_cloud_settings.enabled = buffer_stream.readUInt32() != 0;
		settings.volumetric_cloud_settings.bottom_z = buffer_stream.readFloat();
		settings.volumetric_cloud_settings.top_z = buffer_stream.readFloat();
		settings.volumetric_cloud_settings.coverage = buffer_stream.readFloat();
		settings.volumetric_cloud_settings.density = buffer_stream.readFloat();
		settings.volumetric_cloud_settings.wind_speed = buffer_stream.readFloat();

		const size_t cloud_appearance_payload_size = sizeof(float) * 3;
		const size_t appearance_remaining_bytes = buffer_stream.buf.size() - buffer_stream.getReadIndex();
		if(version >= 10 && appearance_remaining_bytes >= cloud_appearance_payload_size)
		{
			settings.cloud_lighting_settings.underside_darkness = buffer_stream.readFloat();
			settings.volumetric_cloud_settings.edge_softness = buffer_stream.readFloat();
			settings.volumetric_cloud_settings.horizon_fade = buffer_stream.readFloat();

			const size_t cloud_render_payload_size = sizeof(float) * 6;
			const size_t render_remaining_bytes = buffer_stream.buf.size() - buffer_stream.getReadIndex();
			if(version >= 11 && render_remaining_bytes >= cloud_render_payload_size)
			{
				settings.volumetric_cloud_settings.shape_period = buffer_stream.readFloat();
				settings.volumetric_cloud_settings.detail_period = buffer_stream.readFloat();
				settings.volumetric_cloud_settings.max_march_dist = buffer_stream.readFloat();
				settings.volumetric_cloud_settings.wind_direction_deg = buffer_stream.readFloat();
				settings.cloud_lighting_settings.scattering_scale = buffer_stream.readFloat();
				settings.water_reflection_settings.cloud_reflection_strength = buffer_stream.readFloat();

				// v12 appends the new grouped settings.  Every extension is read
				// only when its complete payload is available.
				const size_t cloud_lighting_payload_size = sizeof(float) * 12;
				const size_t lighting_remaining_bytes = buffer_stream.buf.size() - buffer_stream.getReadIndex();
				if(version >= 12 && lighting_remaining_bytes >= cloud_lighting_payload_size)
				{
					settings.cloud_lighting_settings.direct_sun_strength = buffer_stream.readFloat();
					settings.cloud_lighting_settings.sky_light_strength = buffer_stream.readFloat();
					settings.cloud_lighting_settings.sunset_response = buffer_stream.readFloat();
					settings.cloud_lighting_settings.ground_contribution = buffer_stream.readFloat();
					settings.cloud_lighting_settings.ground_albedo.r = buffer_stream.readFloat();
					settings.cloud_lighting_settings.ground_albedo.g = buffer_stream.readFloat();
					settings.cloud_lighting_settings.ground_albedo.b = buffer_stream.readFloat();
					settings.cloud_lighting_settings.phase_g = buffer_stream.readFloat();
					settings.cloud_lighting_settings.phase_blend = buffer_stream.readFloat();
					settings.cloud_lighting_settings.multi_scattering = buffer_stream.readFloat();
					settings.cloud_lighting_settings.underside_darkness = buffer_stream.readFloat();
					settings.cloud_lighting_settings.scattering_scale = buffer_stream.readFloat();

					const size_t water_reflection_payload_size = sizeof(uint32) + sizeof(float) * 3;
					const size_t reflection_remaining_bytes = buffer_stream.buf.size() - buffer_stream.getReadIndex();
					if(reflection_remaining_bytes >= water_reflection_payload_size)
					{
						settings.water_reflection_settings.cloud_reflection_enabled = buffer_stream.readUInt32() != 0;
						settings.water_reflection_settings.cloud_reflection_strength = buffer_stream.readFloat();
						settings.water_reflection_settings.cloud_reflection_samples = buffer_stream.readFloat();
						settings.water_reflection_settings.cloud_reflection_fade = buffer_stream.readFloat();

						const size_t water_surface_payload_size = sizeof(float) * 7 + sizeof(uint32) + sizeof(float) * 5;
						const size_t water_surface_remaining_bytes = buffer_stream.buf.size() - buffer_stream.getReadIndex();
						if(version >= 13 && water_surface_remaining_bytes >= water_surface_payload_size)
						{
							settings.water_surface_settings.wave_amplitude = buffer_stream.readFloat();
							settings.water_surface_settings.wave_length = buffer_stream.readFloat();
							settings.water_surface_settings.wave_steepness = buffer_stream.readFloat();
							settings.water_surface_settings.wave_speed = buffer_stream.readFloat();
							settings.water_surface_settings.wave_direction_deg = buffer_stream.readFloat();
							settings.water_surface_settings.wave_direction_spread_deg = buffer_stream.readFloat();
							settings.water_surface_settings.secondary_wave_scale = buffer_stream.readFloat();
							settings.water_surface_settings.surf_enabled = buffer_stream.readUInt32() != 0;
							settings.water_surface_settings.surf_strength = buffer_stream.readFloat();
							settings.water_surface_settings.shoreline_width = buffer_stream.readFloat();
							settings.water_surface_settings.foam_scale = buffer_stream.readFloat();
							settings.water_surface_settings.foam_speed = buffer_stream.readFloat();
							settings.water_surface_settings.foam_fade = buffer_stream.readFloat();
						}
					}
				}
			}
		}
	}

	// Migrate worlds written with the old v13 presentation defaults.  Only a
	// complete match is migrated, so a world with an intentional custom value
	// keeps that value.
	if(version < WORLDSETTINGS_SERIALISATION_VERSION)
	{
		const VolumetricCloudWorldSettings& clouds = settings.volumetric_cloud_settings;
		const bool legacy_cloud_defaults =
			clouds.enabled &&
			clouds.bottom_z == 1000.f && clouds.top_z == 2200.f &&
			clouds.coverage == 0.48f && clouds.density == 0.0012f &&
			clouds.wind_speed == 20.f && clouds.edge_softness == 0.55f &&
			clouds.horizon_fade == 0.75f && clouds.shape_period == 10000.f &&
			clouds.detail_period == 1200.f && clouds.max_march_dist == 40000.f &&
			clouds.wind_direction_deg == 20.f;
		if(legacy_cloud_defaults)
			settings.volumetric_cloud_settings.enabled = false;

		const WaterReflectionWorldSettings& reflection = settings.water_reflection_settings;
		const bool legacy_reflection_defaults =
			reflection.cloud_reflection_enabled &&
			reflection.cloud_reflection_strength == 0.65f &&
			reflection.cloud_reflection_samples == 24.f &&
			reflection.cloud_reflection_fade == 0.75f;
		if(legacy_reflection_defaults)
			settings.water_reflection_settings.cloud_reflection_enabled = false;

		const WaterSurfaceWorldSettings& water = settings.water_surface_settings;
		const bool legacy_water_defaults =
			water.wave_amplitude == 0.35f && water.wave_length == 28.f &&
			water.wave_steepness == 0.32f && water.wave_speed == 1.f &&
			water.wave_direction_deg == 20.f &&
			water.wave_direction_spread_deg == 35.f &&
			water.secondary_wave_scale == 0.35f && water.surf_enabled &&
			water.surf_strength == 0.38f && water.shoreline_width == 0.1f &&
			water.foam_scale == 1.f && water.foam_speed == 0.12f &&
			water.foam_fade == 0.f;
		if(legacy_water_defaults)
			settings.water_surface_settings = WaterSurfaceWorldSettings();
	}

	// We effectively skip any remaining data we have not processed by discarding buffer_stream.
}


bool TerrainSpec::operator==(const TerrainSpec& other) const
{
	return
		section_specs == other.section_specs &&
		detail_col_map_URLs[0] == other.detail_col_map_URLs[0] &&
		detail_col_map_URLs[1] == other.detail_col_map_URLs[1] &&
		detail_col_map_URLs[2] == other.detail_col_map_URLs[2] &&
		detail_col_map_URLs[3] == other.detail_col_map_URLs[3] &&
		detail_height_map_URLs[0] == other.detail_height_map_URLs[0] &&
		detail_height_map_URLs[1] == other.detail_height_map_URLs[1] &&
		detail_height_map_URLs[2] == other.detail_height_map_URLs[2] &&
		detail_height_map_URLs[3] == other.detail_height_map_URLs[3] &&
		terrain_section_width_m == other.terrain_section_width_m &&
		terrain_height_scale == other.terrain_height_scale &&
		water_z == other.water_z &&
		default_terrain_z == other.default_terrain_z &&
		disabled_detail_map_flags == other.disabled_detail_map_flags &&
		flags == other.flags;
}


bool TerrainSpecSection::operator==(const TerrainSpecSection& other) const
{
	return
		x == other.x &&
		y == other.y &&
		heightmap_URL == other.heightmap_URL &&
		mask_map_URL == other.mask_map_URL &&
		tree_mask_map_URL == other.tree_mask_map_URL &&
		road_mask_map_URL == other.road_mask_map_URL &&
		building_mask_map_URL == other.building_mask_map_URL &&
		disabled_map_flags == other.disabled_map_flags;
}
