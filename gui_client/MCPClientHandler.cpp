/*=====================================================================
MCPClientHandler.cpp
--------------------
Copyright Glare Technologies Limited 2026 -
=====================================================================*/
#include "MCPClientHandler.h"
#include "MCPTextureTools.h"
#include "MCPImageGeneration.h"
#include "MCPAnimationTools.h"
#include "MCPTextMeshTools.h"
#include "MCPBlenderBridge.h"
#include "MCPClientLocalOnlyProtocol.h"

#include "../shared/Version.h"


#include "CredentialManager.h"
#include <webserver/RequestInfo.h>
#include <webserver/ResponseUtils.h>
#include <webserver/Escaping.h>
#include <networking/HTTPClient.h>
#include <networking/IPAddress.h>
#include <networking/URL.h>
#include <utils/JSONParser.h>
#include <utils/Base64.h>
#include <utils/StringUtils.h>
#include <utils/ConPrint.h>
#include <utils/Exception.h>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <exception>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QJsonArray>
#include <QtCore/QMap>
#include <QtCore/QDir>


namespace
{

const size_t MAX_MCP_REQUEST_BODY_SIZE = 8 * 1024 * 1024;


// Kept in sync with the current upstream render_view contract.
const char* RENDER_VIEW_TOOL_JSON =
	"{"
		"\"name\":\"render_view\","
		"\"description\":\"Render an image of the currently-connected world from a given camera, and return it as an image. "
			"Use this to inspect your model from front, side and perspective views. Renders only assets already loaded near the avatar, "
			"without moving the avatar or the user's camera. Retry if loading is in progress. Distant unloaded objects may be absent.\","
		"\"inputSchema\":{\"type\":\"object\",\"properties\":{"
			"\"cam_pos\":{\"type\":\"object\",\"description\":\"Camera position as {x,y,z} in metres (z is up).\"},"
			"\"cam_angles\":{\"type\":\"object\",\"description\":\"Camera orientation as {heading,pitch,roll} in radians. heading rotates in the x-y plane from +x towards +y (0 looks along +x, pi/2 looks along +y). pitch is a POLAR angle from the +z (up) axis: 0 looks straight up, pi/2 (~1.571) is level/horizontal, pi (~3.14) looks straight down. roll is usually 0.\"},"
			"\"width\":{\"type\":\"number\",\"description\":\"Image width, integer 16..2048 pixels (default 1024).\"},"
			"\"height\":{\"type\":\"number\",\"description\":\"Image height, integer 16..2048 pixels (default 768).\"}"
		"},\"required\":[\"cam_pos\",\"cam_angles\"]}"
	"}";

const char* INSPECT_TERRAIN_TOOL_JSON =
	"{\"name\":\"inspect_terrain\",\"description\":\"Inspect the active world's terrain sections before editing. Returns section coordinates and bounds, whether each heightmap is currently loaded in the client, heightmap and mask layer presence, section width, base height, height scale and water level. Also returns the highlighted real-world map selection (WGS84 bounds and its full polygon contour when present) and target section X/Y currently selected in World Settings > Map. import_terrain_from_map can use this UI selection and section directly. The heightmap is the EXR layer that controls terrain shape; masks and detail textures are separate and are preserved when editing. If the target section is not loaded, move the client camera near it and inspect again.\",\"inputSchema\":{\"type\":\"object\",\"properties\":{},\"additionalProperties\":false}}";
const char* SMOOTH_TERRAIN_TOOL_JSON =
	"{\"name\":\"smooth_terrain\",\"description\":\"Smooth terrain heights in a circular world-space region by editing only the active section heightmap EXR layer. Use inspect_terrain first and target sections where heightmap_loaded=true. A brush that crosses an outer/unloaded section edge edits its loaded portion and skips missing sections instead of failing the whole operation. For a long straight shoreline or section edge, place several overlapping circular brushes along the visible edge so the full requested stretch is smoothed, with a soft transition inland. Derive world x/y from the screenshot and render_view; do not assume the camera position is the edit center. Strength is 0.05..1; passes is 1..8. The result reports edited and skipped section counts, is saved as new EXR resources and sent through the normal world-settings update. It preserves tree, road, building, terrain masks and detail textures. Requires owner/god write access.\",\"inputSchema\":{\"type\":\"object\",\"properties\":{\"center\":{\"type\":\"object\",\"properties\":{\"x\":{\"type\":\"number\"},\"y\":{\"type\":\"number\"}},\"required\":[\"x\",\"y\"],\"additionalProperties\":false},\"radius_m\":{\"type\":\"number\",\"minimum\":1},\"strength\":{\"type\":\"number\",\"minimum\":0.01,\"maximum\":1},\"passes\":{\"type\":\"integer\",\"minimum\":1,\"maximum\":8}},\"required\":[\"center\",\"radius_m\",\"strength\"],\"additionalProperties\":false}}";
const char* SCULPT_TERRAIN_TOOL_JSON =
	"{\"name\":\"sculpt_terrain\",\"description\":\"Apply native Metasiberia terrain sculpt tools at absolute world x/y points in metres (+Z up). Each point is one brush stamp; use ordered overlapping points for ridges, rivers and long forms. Read inspect_terrain first and use loaded sections only. radius_m is 0.25..half a section; strength_m is metres. target_height_m is used by level tools. Island tools also accept island_sea_floor_m, island_land_base_m, island_peak_m, island_seed. Every call creates one undo step, saves a new EXR resource and submits world settings. Requires owner/god write access.\",\"inputSchema\":{\"type\":\"object\",\"properties\":{\"tool\":{\"type\":\"string\",\"enum\":[\"ridge\",\"raise\",\"lower\",\"soft_raise\",\"hard_raise\",\"soft_lower\",\"hard_lower\",\"pinch\",\"inflate\",\"deflate\",\"clay\",\"blob\",\"amplify\",\"dampen\",\"smooth\",\"polish\",\"sharpen\",\"flatten\",\"plateau\",\"ramp\",\"cliff\",\"wall\",\"basin\",\"gorge\",\"berm\",\"saddle\",\"notch\",\"lake\",\"fill\",\"lowland\",\"coast\",\"shelf\",\"cove\",\"spit\",\"river\",\"channel\",\"delta\",\"sea\",\"stamp_volcano\",\"stamp_crater\",\"stamp_hill\",\"stamp_cone\",\"stamp_mesa\",\"stamp_caldera\",\"stamp_ridge\",\"stamp_spire\",\"stamp_pyramid\",\"stamp_bowl\",\"stamp_arch\",\"stamp_atoll\",\"stamp_twin\",\"stamp_dunes\",\"stamp_tor\",\"stamp_ramp\",\"island_classic\",\"island_high\",\"island_low\",\"island_barrier\",\"island_twin\",\"island_caldera\",\"island_mesa\",\"island_rias\",\"island_spit\",\"island_arch\",\"island_chain\",\"island_reef\",\"island_ring\",\"island_crescent\",\"island_star\",\"island_spiral\",\"island_serpent\",\"island_shards\",\"island_crystal\",\"island_plateau\",\"island_mushroom\",\"island_heart\",\"island_needle\",\"island_lagoon_chain\"]},\"points\":{\"type\":\"array\",\"minItems\":1,\"maxItems\":64,\"items\":{\"type\":\"object\",\"properties\":{\"x\":{\"type\":\"number\"},\"y\":{\"type\":\"number\"}},\"required\":[\"x\",\"y\"],\"additionalProperties\":false}},\"radius_m\":{\"type\":\"number\",\"minimum\":0.25},\"strength_m\":{\"type\":\"number\",\"minimum\":0.001,\"maximum\":1000},\"target_height_m\":{\"type\":\"number\"},\"island_sea_floor_m\":{\"type\":\"number\"},\"island_land_base_m\":{\"type\":\"number\"},\"island_peak_m\":{\"type\":\"number\"},\"island_seed\":{\"type\":\"integer\"}},\"required\":[\"tool\",\"points\",\"radius_m\",\"strength_m\"],\"additionalProperties\":false}}";
const char* PAINT_TERRAIN_MAP_TOOL_JSON =
	"{\"name\":\"paint_terrain_map\",\"description\":\"Paint an existing terrain's native material blend or tree placement mask with a circular brush in absolute world x/y metres. Layers: rock, ground, vegetation, overlay or trees. action=paint raises the chosen material weight (or allows trees); action=restore returns the touched pixels to their imported/original mask. For trees, allow_trees=false paints the blocked state. Use inspect_terrain first and only edit loaded sections. Radius is 0.25..half a section; strength is 0.01..1. Each call is one undo step, saves updated mask PNG resources and submits world settings. Does not change terrain height, roads or buildings. Requires owner/god write access.\",\"inputSchema\":{\"type\":\"object\",\"properties\":{\"layer\":{\"type\":\"string\",\"enum\":[\"rock\",\"ground\",\"vegetation\",\"overlay\",\"trees\"]},\"action\":{\"type\":\"string\",\"enum\":[\"paint\",\"restore\"]},\"center\":{\"type\":\"object\",\"properties\":{\"x\":{\"type\":\"number\"},\"y\":{\"type\":\"number\"}},\"required\":[\"x\",\"y\"],\"additionalProperties\":false},\"radius_m\":{\"type\":\"number\",\"minimum\":0.25},\"strength\":{\"type\":\"number\",\"minimum\":0.01,\"maximum\":1},\"allow_trees\":{\"type\":\"boolean\"}},\"required\":[\"layer\",\"action\",\"center\",\"radius_m\",\"strength\"],\"additionalProperties\":false}}";
const char* IMPORT_TERRAIN_FROM_MAP_TOOL_JSON =
	"{\"name\":\"import_terrain_from_map\",\"description\":\"Download real-world elevation for a map area and assign it to a terrain section. If bounds are omitted, use the area currently highlighted in World Settings > Map > Real-world terrain; this preserves a freehand contour as well as a rectangle. If section_x/section_y are omitted, use the target section currently selected in that panel. Bounds, when supplied, use WGS84 degrees and replace the current selection. Imports a 512x512 Terrarium elevation heightmap plus derived material/tree masks. The same import downloads OpenStreetMap road and building reference masks for the selected area; if OSM is temporarily unavailable, terrain still imports and the result reports that the masks were not updated. Check inspect_terrain before claiming the road and building masks exist. Reimport the same selection to retry. One import can run at a time. Use inspect_terrain first to read the current map selection and section target. The client shows progress; on completion the heightmap/masks are uploaded and world settings are submitted. Requires owner/god write access.\",\"inputSchema\":{\"type\":\"object\",\"properties\":{\"bounds\":{\"type\":\"object\",\"properties\":{\"north\":{\"type\":\"number\"},\"south\":{\"type\":\"number\"},\"west\":{\"type\":\"number\"},\"east\":{\"type\":\"number\"}},\"required\":[\"north\",\"south\",\"west\",\"east\"],\"additionalProperties\":false},\"section_x\":{\"type\":\"integer\",\"minimum\":-100,\"maximum\":100},\"section_y\":{\"type\":\"integer\",\"minimum\":-100,\"maximum\":100}},\"additionalProperties\":false}}";
const char* CONFIGURE_TERRAIN_TOOL_JSON =
	"{\"name\":\"configure_terrain\",\"description\":\"Change global terrain geometry and existing terrain-section map settings in the active world. Global fields: section_width_m (8..1000000), base_height_m (0..99999), height_scale (0..10000), exact_heightmap (boolean, digital-twin mode). Optional sections[] entries target existing coordinates only and can set heightmap_scale, material_mask_scale, tree_mask_scale, road_mask_scale, building_mask_scale (legacy aliases that set both axes), plus heightmap_scale_x/y, material_mask_scale_x/y, tree_mask_scale_x/y, road_mask_scale_x/y and building_mask_scale_x/y (each 0.01..100) and enable_heightmap, enable_material_mask, enable_tree_mask, enable_road_mask, enable_building_mask. This does not assign new files; use import_terrain_from_map for terrain data. At least one property is required. Settings are submitted through the normal world-settings pipeline. Requires owner/god write access.\",\"inputSchema\":{\"type\":\"object\",\"properties\":{\"section_width_m\":{\"type\":\"number\",\"minimum\":8,\"maximum\":1000000},\"base_height_m\":{\"type\":\"number\",\"minimum\":0,\"maximum\":99999},\"height_scale\":{\"type\":\"number\",\"minimum\":0,\"maximum\":10000},\"exact_heightmap\":{\"type\":\"boolean\"},\"sections\":{\"type\":\"array\",\"maxItems\":256,\"items\":{\"type\":\"object\",\"properties\":{\"section_x\":{\"type\":\"integer\"},\"section_y\":{\"type\":\"integer\"},\"heightmap_scale\":{\"type\":\"number\",\"minimum\":0.01,\"maximum\":100},\"heightmap_scale_x\":{\"type\":\"number\",\"minimum\":0.01,\"maximum\":100},\"heightmap_scale_y\":{\"type\":\"number\",\"minimum\":0.01,\"maximum\":100},\"material_mask_scale\":{\"type\":\"number\",\"minimum\":0.01,\"maximum\":100},\"material_mask_scale_x\":{\"type\":\"number\",\"minimum\":0.01,\"maximum\":100},\"material_mask_scale_y\":{\"type\":\"number\",\"minimum\":0.01,\"maximum\":100},\"tree_mask_scale\":{\"type\":\"number\",\"minimum\":0.01,\"maximum\":100},\"tree_mask_scale_x\":{\"type\":\"number\",\"minimum\":0.01,\"maximum\":100},\"tree_mask_scale_y\":{\"type\":\"number\",\"minimum\":0.01,\"maximum\":100},\"road_mask_scale\":{\"type\":\"number\",\"minimum\":0.01,\"maximum\":100},\"road_mask_scale_x\":{\"type\":\"number\",\"minimum\":0.01,\"maximum\":100},\"road_mask_scale_y\":{\"type\":\"number\",\"minimum\":0.01,\"maximum\":100},\"building_mask_scale\":{\"type\":\"number\",\"minimum\":0.01,\"maximum\":100},\"building_mask_scale_x\":{\"type\":\"number\",\"minimum\":0.01,\"maximum\":100},\"building_mask_scale_y\":{\"type\":\"number\",\"minimum\":0.01,\"maximum\":100},\"enable_heightmap\":{\"type\":\"boolean\"},\"enable_material_mask\":{\"type\":\"boolean\"},\"enable_tree_mask\":{\"type\":\"boolean\"},\"enable_road_mask\":{\"type\":\"boolean\"},\"enable_building_mask\":{\"type\":\"boolean\"}},\"required\":[\"section_x\",\"section_y\"],\"additionalProperties\":false}}},\"additionalProperties\":false}}";
const char* INSPECT_WORLD_ENVIRONMENT_TOOL_JSON =
	"{\"name\":\"inspect_world_environment\",\"description\":\"Read the active world's environment settings: water enabled/level, sun angles, both fog layers, volumetric clouds and lighting, water reflections, waves, shore foam and underwater caustics. Angles are returned in degrees.\",\"inputSchema\":{\"type\":\"object\",\"properties\":{},\"additionalProperties\":false}}";
const char* CONFIGURE_WORLD_ENVIRONMENT_TOOL_JSON =
	"{\"name\":\"configure_world_environment\",\"description\":\"Change sky, fog and water settings in the active world. All properties are optional; provide at least one. Sun and wind/wave directions use degrees. cloud_bottom_z_m must remain below cloud_top_z_m. Water is controlled separately from underwater caustics. Requires owner/god write access.\",\"inputSchema\":{\"type\":\"object\",\"properties\":{\"water_enabled\":{\"type\":\"boolean\"},\"water_level_m\":{\"type\":\"number\"},\"sun_angle_from_vertical_deg\":{\"type\":\"number\",\"minimum\":0,\"maximum\":120},\"sun_azimuth_deg\":{\"type\":\"number\",\"minimum\":0,\"maximum\":720},\"fog_layer_0_density\":{\"type\":\"number\",\"minimum\":0,\"maximum\":1},\"fog_layer_0_height_m\":{\"type\":\"number\",\"minimum\":0.1,\"maximum\":10000},\"fog_layer_1_density\":{\"type\":\"number\",\"minimum\":0,\"maximum\":1},\"fog_layer_1_height_m\":{\"type\":\"number\",\"minimum\":0.1,\"maximum\":10000},\"clouds_enabled\":{\"type\":\"boolean\"},\"cloud_bottom_z_m\":{\"type\":\"number\",\"minimum\":0,\"maximum\":100000},\"cloud_top_z_m\":{\"type\":\"number\",\"minimum\":0,\"maximum\":100000},\"cloud_coverage\":{\"type\":\"number\",\"minimum\":0,\"maximum\":1},\"cloud_density\":{\"type\":\"number\",\"minimum\":0,\"maximum\":0.02},\"cloud_wind_speed_mps\":{\"type\":\"number\",\"minimum\":0,\"maximum\":200},\"cloud_underside_darkness\":{\"type\":\"number\",\"minimum\":0,\"maximum\":1},\"cloud_edge_softness\":{\"type\":\"number\",\"minimum\":0,\"maximum\":1},\"cloud_horizon_fade\":{\"type\":\"number\",\"minimum\":0,\"maximum\":1},\"cloud_shape_scale_m\":{\"type\":\"number\",\"minimum\":100,\"maximum\":100000},\"cloud_detail_scale_m\":{\"type\":\"number\",\"minimum\":50,\"maximum\":10000},\"cloud_draw_distance_m\":{\"type\":\"number\",\"minimum\":100,\"maximum\":100000},\"cloud_wind_direction_deg\":{\"type\":\"number\",\"minimum\":0,\"maximum\":360},\"cloud_scattering\":{\"type\":\"number\",\"minimum\":0,\"maximum\":2},\"cloud_direct_sun\":{\"type\":\"number\",\"minimum\":0,\"maximum\":4},\"cloud_sky_light\":{\"type\":\"number\",\"minimum\":0,\"maximum\":4},\"cloud_sunset_response\":{\"type\":\"number\",\"minimum\":0,\"maximum\":4},\"cloud_ground_contribution\":{\"type\":\"number\",\"minimum\":0,\"maximum\":2},\"cloud_forward_scattering\":{\"type\":\"number\",\"minimum\":-0.85,\"maximum\":0.85},\"cloud_phase_blend\":{\"type\":\"number\",\"minimum\":0,\"maximum\":1},\"cloud_multiple_scattering\":{\"type\":\"number\",\"minimum\":0,\"maximum\":1},\"clouds_reflect_in_water\":{\"type\":\"boolean\"},\"water_reflection_strength\":{\"type\":\"number\",\"minimum\":0,\"maximum\":1},\"water_reflection_quality\":{\"type\":\"number\",\"minimum\":8,\"maximum\":48},\"water_reflection_horizon_fade\":{\"type\":\"number\",\"minimum\":0,\"maximum\":1},\"wave_height_m\":{\"type\":\"number\",\"minimum\":0,\"maximum\":10},\"wave_length_m\":{\"type\":\"number\",\"minimum\":0.5,\"maximum\":500},\"wave_steepness\":{\"type\":\"number\",\"minimum\":0,\"maximum\":1},\"wave_speed\":{\"type\":\"number\",\"minimum\":0,\"maximum\":8},\"wave_direction_deg\":{\"type\":\"number\",\"minimum\":0,\"maximum\":360},\"wave_direction_spread_deg\":{\"type\":\"number\",\"minimum\":0,\"maximum\":180},\"secondary_wave_scale\":{\"type\":\"number\",\"minimum\":0,\"maximum\":1},\"shore_surf_enabled\":{\"type\":\"boolean\"},\"underwater_caustics_enabled\":{\"type\":\"boolean\"},\"shore_surf_strength\":{\"type\":\"number\",\"minimum\":0,\"maximum\":2},\"shoreline_foam_width_m\":{\"type\":\"number\",\"minimum\":0.1,\"maximum\":100},\"foam_scale\":{\"type\":\"number\",\"minimum\":0.05,\"maximum\":10},\"foam_speed\":{\"type\":\"number\",\"minimum\":0,\"maximum\":8},\"foam_fade\":{\"type\":\"number\",\"minimum\":0,\"maximum\":1}},\"additionalProperties\":false}}";
bool isLoopbackAddress(const IPAddress& addr)
{
	const std::string s = addr.toString();
	return s == "127.0.0.1" || s == "::1";
}


bool isValidServerHostname(const std::string& hostname)
{
	if(hostname.empty() || hostname.size() > 512)
		return false;

	// The value is appended to an HTTPS URL and must come from the active
	// connection, never from an MCP request. Reject URL/user-info/path syntax.
	for(size_t i=0; i<hostname.size(); ++i)
	{
		const unsigned char c = (unsigned char)hostname[i];
		if(c <= 0x20 || c == 0x7f || c == '/' || c == '\\' || c == '@' || c == '?' || c == '#')
			return false;
	}
	return true;
}


std::string extractIdJSON(const JSONParser& parser, const JSONNode& root)
{
	if(!root.hasChild("id"))
		return "null";
	const JSONNode& id_node = root.getChildNode(parser, "id");
	if(id_node.type == JSONNode::Type_String)
		return "\"" + web::Escaping::JSONEscape(id_node.getStringValue()) + "\"";
	else if(id_node.type == JSONNode::Type_Number)
	{
		const double v = id_node.getDoubleValue();
		if(std::isfinite(v) && v == (double)(int64)v)
			return toString((int64)v);
		else if(std::isfinite(v))
			return doubleToString(v);
	}
	return "null";
}


void writeJSONRPCResult(web::ReplyInfo& reply_info, const std::string& id_json, const std::string& result_json)
{
	const std::string s = "{\"jsonrpc\":\"2.0\",\"id\":" + id_json + ",\"result\":" + result_json + "}";
	web::ResponseUtils::writeHTTPOKHeaderAndData(reply_info, s.data(), s.size(), /*content type=*/"application/json");
}


void writeJSONRPCError(web::ReplyInfo& reply_info, const std::string& id_json, int code, const std::string& message)
{
	const std::string s = "{\"jsonrpc\":\"2.0\",\"id\":" + id_json + ",\"error\":{\"code\":" + toString(code) + ",\"message\":\"" +
		web::Escaping::JSONEscape(message) + "\"}}";
	web::ResponseUtils::writeHTTPOKHeaderAndData(reply_info, s.data(), s.size(), /*content type=*/"application/json");
}


QJsonObject neuroToolSchema(const QString& name, const QString& description, const QJsonObject& properties,
	const QStringList& required, bool read_only)
{
	const QJsonObject input_schema{{"type", "object"}, {"properties", properties}, {"required", QJsonArray::fromStringList(required)}, {"additionalProperties", false}};
	return QJsonObject{{"name", name}, {"description", description}, {"inputSchema", input_schema},
		{"annotations", QJsonObject{{"readOnlyHint", read_only}, {"destructiveHint", !read_only}, {"openWorldHint", false}}}};
}


QJsonArray neuroControlToolSchemas()
{
	static const QJsonArray schemas = [] {
		const auto stringProperty = [](const QString& description) { return QJsonObject{{"type", "string"}, {"description", description}}; };
		const auto numberProperty = [](const QString& description, double minimum, double maximum) { return QJsonObject{{"type", "number"}, {"description", description}, {"minimum", minimum}, {"maximum", maximum}}; };
		const auto booleanProperty = [](const QString& description) { return QJsonObject{{"type", "boolean"}, {"description", description}}; };
		QJsonArray a;
		const QJsonObject no_args;
		a.append(neuroToolSchema("neuro.get_status", "Прочитать фактическое состояние NeuroControl в этом Qt-клиенте: источник, запись, модель, пороги, воспроизведение, управление, трассу контроллера и сведения о запущенном файле/версии клиента.", no_args, {}, true));
		a.append(neuroToolSchema("neuro.list_sources", "Перечислить доступные источники ЭЭГ и указать, какие из них реально могут управлять аватаром.", no_args, {}, true));
		a.append(neuroToolSchema("neuro.configure_lsl", "Автоматически найти установленный официальный пакет LabRecorder, проверить x64 PE, обязательные экспорты и загрузку lsl.dll, затем сохранить путь в пользовательских настройках. Работает асинхронно; поток не ищет и не подключает.", no_args, {}, false));
		a.append(neuroToolSchema("neuro.discover_lsl_streams", "Асинхронно найти числовые LSL-потоки через настроенную lsl.dll. Если библиотека ещё не готова, сначала запускается её безопасная автоконфигурация; результат читайте через neuro.get_lsl_status. ЭЭГ не передаётся.", no_args, {}, false));
		a.append(neuroToolSchema("neuro.get_lsl_status", "Получить найденные потоки, метаданные выбранного потока, скорость приёма, паузы, ошибки и качество исходных timestamps отдельно от монотонного времени получения. Сырые отсчёты не возвращаются.", no_args, {}, true));
		a.append(neuroToolSchema("neuro.run_lsl_diagnostic", "Снять ограниченную сводку уже подключённого LSL-приёма заданной длительности, сохранить Markdown-отчёт без сырых ЭЭГ и автоматически отключить поток по завершении. Результат читайте через neuro.get_lsl_status.",
			QJsonObject{{"duration_seconds", QJsonObject{{"type", "integer"}, {"minimum", 1}, {"maximum", 300}, {"default", 15}, {"description", "Длительность наблюдения; по умолчанию 15 секунд."}}}}, {}, false));
		a.append(neuroToolSchema("neuro.get_lsl_timestamps", "Прочитать не более 200 последних пар исходной метки LSL и монотонного времени получения из ограниченного буфера; значения ЭЭГ не передаются.",
			QJsonObject{{"limit", QJsonObject{{"type", "integer"}, {"minimum", 1}, {"maximum", 200}}}}, {}, true));
		a.append(neuroToolSchema("neuro.connect_lsl_stream", "Асинхронно подключить выбранный по UID поток для графиков и диагностики. Пользователь отдельно включает теневую оценку после проверки каналов, единиц, стабильной частоты и монтажа/референса; FIR-ресемплер приводит поддерживаемую частоту к модели. Команды аватару не отправляются.",
			QJsonObject{{"stream_uid", stringProperty("UID из neuro.get_lsl_status.")},
			{"units_override", QJsonObject{{"type", "string"}, {"enum", QJsonArray{"auto", "microvolts", "volts", "millivolts"}}, {"description", "По умолчанию единицы из XML; явное переопределение требуется, если они неизвестны."}}}}, {"stream_uid"}, false));
		a.append(neuroToolSchema("neuro.disconnect_lsl_stream", "Отключить LSL-поток без блокировки Qt и оставить нейроуправление выключенным.", no_args, {}, false));
		a.append(neuroToolSchema("neuro.start_lsl_recording", "Начать lossless-запись подключённого float32 LSL-потока в ограниченный бинарный файл и JSON sidecar; запись идёт в фоне независимо от графика.", no_args, {}, false));
		a.append(neuroToolSchema("neuro.stop_lsl_recording", "Запросить штатное завершение текущей LSL-записи; дождитесь state=complete через neuro.get_lsl_recording_status.", no_args, {}, false));
		a.append(neuroToolSchema("neuro.get_lsl_recording_status", "Получить состояние записи/загрузки, полный список каналов и метаданные загруженной LSL-записи. Сырые EEG-отсчёты не передаются.", no_args, {}, true));
		a.append(neuroToolSchema("neuro.list_lsl_recordings", "Перечислить сохранённые локальные LSL-записи и их состояние; путь ограничен каталогом данных приложения.", no_args, {}, true));
		a.append(neuroToolSchema("neuro.audit_lsl_recording", "Асинхронно проверить целостность сохранённой .nlsr и пропустить её EEG-каналы через общий FIR-ресемплер. target_sample_rate независим от модели и по умолчанию равен 160 Гц. Возвращаются агрегаты, целостность и причины совместимости; классификация и команды аватара не запускаются.",
			QJsonObject{{"recording_id", stringProperty("Безопасный recording_id из neuro.list_lsl_recordings.")},
			{"target_sample_rate", QJsonObject{{"type", "integer"}, {"minimum", 8}, {"maximum", 2000}, {"default", 160}, {"description", "Запрошенная частота выхода ресемплера в Гц; не зависит от загруженной модели."}}}}, {"recording_id"}, false));
		a.append(neuroToolSchema("neuro.save_lsl_audit_report", "Сохранить завершённый результат последнего офлайн LSL-аудита как агрегированный локальный Markdown-отчёт SNN в neurocontrol/docs/experiments. Сырые отсчёты не включаются; существующий отчёт не перезаписывается.",
			QJsonObject{{"experiment_id", QJsonObject{{"type", "string"}, {"pattern", "^S[0-9]{2}$"}, {"description", "Номер эксперимента, например S04 или S05."}}}}, {"experiment_id"}, false));
		a.append(neuroToolSchema("neuro.load_lsl_recording", "Асинхронно проверить checksum и структуру завершённой LSL-записи, восстановить каналы и загрузить её для архивного воспроизведения; живой поток должен быть отключён.", QJsonObject{{"recording_id", stringProperty("Безопасный recording_id из neuro.list_lsl_recordings.")}}, {"recording_id"}, false));
		a.append(neuroToolSchema("neuro.play_lsl_recording", "Запустить только визуальное воспроизведение сохранённой LSL-записи по расчётной шкале sample_index / nominal_rate_hz; исходные timestamps остаются доступными отдельно. Классификация и аватар не используются.", no_args, {}, false));
		a.append(neuroToolSchema("neuro.resume_playback", "Продолжить приостановленное воспроизведение EDF или сохранённой LSL-записи; LSL-воспроизведение остаётся только визуальным.", no_args, {}, false));
		a.append(neuroToolSchema("neuro.stop_lsl_recording_playback", "Остановить архивное воспроизведение LSL-записи, оставив её загруженной.", no_args, {}, false));
		a.append(neuroToolSchema("neuro.seek_lsl_recording", "Перейти к позиции в загруженной LSL-записи; значения исходных timestamps при этом не меняются.", QJsonObject{{"position_seconds", numberProperty("Позиция от 0 до длительности записи по номинальной частоте.", 0.0, 86400.0)}}, {"position_seconds"}, false));
		a.append(neuroToolSchema("neuro.get_lsl_recording_timestamps", "Получить ограниченную страницу исходных LSL timestamps и времени приёма из загруженной записи. Время приёма может быть общим для отсчётов одного чанка; значения ЭЭГ не передаются.", QJsonObject{{"offset", QJsonObject{{"type", "integer"}, {"minimum", 0}, {"maximum", 1000000000}}}, {"limit", QJsonObject{{"type", "integer"}, {"minimum", 1}, {"maximum", 1000}}}}, {}, true));
		a.append(neuroToolSchema("neuro.configure_lsl_visualization", "Настроить окно графика, вертикальный масштаб и видимые каналы в текущем виджете «Сигналы ЭЭГ» без изменения сохранённых данных.", QJsonObject{{"window_seconds", QJsonObject{{"type", "integer"}, {"minimum", 2}, {"maximum", 30}}}, {"amplitude_scale_uv", numberProperty("Вертикальный масштаб в микровольтах.", 5.0, 1000.0)}, {"visible_channels", QJsonObject{{"type", "array"}, {"items", QJsonObject{{"type", "string"}}}, {"maxItems", 256}}}, {"detail_channel", stringProperty("Если указан, показать только этот канал подробно.")}}, {}, false));
		a.append(neuroToolSchema("neuro.list_recordings", "Рекурсивно найти поддерживаемые EDF/EDF+, BDF и проверенные пары F32/JSON в ограниченных каталогах neurocontrol/datasets/raw и processed. Возвращает безопасные ID, метаданные, события и целостность; сырые отсчёты не передаются.", no_args, {}, true));
		a.append(neuroToolSchema("neuro.get_recording_info", "Получить метаданные текущей записи либо найденной по ID/пути внутри разрешённых каталогов datasets/raw и datasets/processed.", QJsonObject{{"recording_id", stringProperty("Безопасный ID из neuro.list_recordings. Используйте либо его, либо path.")}, {"path", stringProperty("Полный путь к файлу внутри neurocontrol/datasets/raw (EDF/BDF) или datasets/processed (F32/JSON). Путь за пределами этих каталогов отклоняется.")}}, {}, true));
		a.append(neuroToolSchema("neuro.list_models", "Перечислить JSON-модели только из каталога neurocontrol/models.", no_args, {}, true));
		a.append(neuroToolSchema("neuro.get_model_info", "Получить параметры модели и фактический отчёт проверки, если он существует.", QJsonObject{{"model_id", stringProperty("Необязательное имя модели из neuro.list_models. Без него возвращаются сведения о загруженной модели.")}}, {}, true));
		a.append(neuroToolSchema("neuro.evaluate_model", "Асинхронно проверить указанную модель на размеченной подготовленной записи, рассчитать метрики и сохранить отдельный отчёт без изменения исходной модели или старых метрик.", QJsonObject{{"model_id", stringProperty("Имя JSON-модели из neuro.list_models.")}, {"recording_id", stringProperty("ID подготовленной записи, например S001R14.")}}, {"model_id", "recording_id"}, false));
		a.append(neuroToolSchema("neuro.check_model_compatibility", "Сравнить частоту дискретизации и точный порядок каналов выбранной модели с текущей загруженной записью.", QJsonObject{{"model_id", stringProperty("Необязательное имя модели из neuro.list_models; по умолчанию — загруженная модель.")}}, {}, true));
		a.append(neuroToolSchema("neuro.get_signal_info", "Получить параметры и счётчики текущего потока: частоту, каналы, количество принятых и локально отброшенных из буфера отсчётов. Локальные отбросы не равны потерям источника.", no_args, {}, true));
		a.append(neuroToolSchema("neuro.get_spectrum", "Получить текущий спектр выбранного канала, рассчитанный тем же виджетом, что отображает спектр ЭЭГ.", no_args, {}, true));
		a.append(neuroToolSchema("neuro.get_prediction", "Получить последний класс-кандидат отдельно от команды, отправленной контроллеру, счётчик двухоконного подтверждения, оба порога, причину STOP и соответствующую трассу контроллера.", no_args, {}, true));
		a.append(neuroToolSchema("neuro.get_avatar_status", "Получить фактическое решение контроллера аватара, причины фильтрации, применённый ввод и измеренную скорость.", no_args, {}, true));
		a.append(neuroToolSchema("neuro.get_diagnostics", "Прочитать журнал NeuroControl и последнюю трассу. Необязательный фильтр уровня: INFO, WARNING, ERROR или DEBUG.", QJsonObject{{"limit", QJsonObject{{"type", "integer"}, {"minimum", 1}, {"maximum", 500}}}, {"level", stringProperty("Необязательный уровень журнала.")}}, {}, true));
		a.append(neuroToolSchema("neuro.get_training_status", "Проверить состояние асинхронной подготовки EDF, аудита, обучения или оценки выбранной модели; возвращает последний отчёт оценки.", no_args, {}, true));
		a.append(neuroToolSchema("neuro.list_experiments", "Перечислить локальные сеансы журналирования NeuroControl, их состояние и краткие итоговые счётчики. Инструменты принимают только безопасный ID сеанса, не принимают произвольные пути и не передают сырые данные ЭЭГ.", QJsonObject{{"limit", QJsonObject{{"type", "integer"}, {"minimum", 1}, {"maximum", 100}, {"description", "Максимум сеансов в ответе; по умолчанию 20."}}}}, {}, true));
		a.append(neuroToolSchema("neuro.get_experiment_summary", "Прочитать параметры и итоговую статистику локального сеанса NeuroControl по безопасному session_id из neuro.list_experiments. Доступ ограничен каталогом экспериментов этого клиента.", QJsonObject{{"session_id", stringProperty("Безопасный ID из neuro.list_experiments.")}}, {"session_id"}, true));
		a.append(neuroToolSchema("neuro.get_experiment_events", "Постранично прочитать JSONL-события локального сеанса NeuroControl. Используйте next_cursor_bytes из предыдущего ответа; каждый ответ ограничен 200 событиями и 2 МиБ. Сырые отсчёты ЭЭГ не журналируются и не передаются.", QJsonObject{{"session_id", stringProperty("Безопасный ID из neuro.list_experiments.")}, {"cursor_bytes", QJsonObject{{"type", "integer"}, {"minimum", 0}, {"maximum", 67108864}, {"description", "Байтовый курсор из предыдущего ответа; по умолчанию 0."}}}, {"limit", QJsonObject{{"type", "integer"}, {"minimum", 1}, {"maximum", 200}, {"description", "Число событий; по умолчанию 100."}}}}, {"session_id"}, true));
		const QJsonObject topic_property{{"type", "string"}, {"enum", QJsonArray{"overview", "architecture", "glossary", "datasets", "models", "tools", "experiments", "troubleshooting", "medical_eeg", "ai_instructions"}}};
		a.append(neuroToolSchema("neuro.get_knowledge", "Прочитать один тематический документ NeuroControl на русском языке из встроенной базы знаний.", QJsonObject{{"topic", topic_property}}, {"topic"}, true));
		const QJsonObject source_property{{"type", "string"}, {"enum", QJsonArray{"recorded_edf", "brainflow_synthetic", "live_eeg", "lsl_live", "lsl_recorded"}}};
		a.append(neuroToolSchema("neuro.select_source", "Выбрать EDF/F32, Synthetic Board, LSL в реальном времени или уже загруженную архивную LSL-запись. LSL-источники и синтетический поток не могут управлять аватаром; прямой драйвер физического устройства недоступен.", QJsonObject{{"source_id", source_property}}, {"source_id"}, false));
		a.append(neuroToolSchema("neuro.load_recording", "Загрузить любую корректную EDF/EDF+, BDF или подготовленную пару F32/JSON по ID из neuro.list_recordings либо полному пути внутри разрешённых каталогов. Сырые EDF/BDF подготавливаются существующим асинхронным импортёром; F32 проверяется по метаданным, размеру и SHA-256. Воспроизведение и управление аватаром не запускаются.", QJsonObject{{"recording_id", stringProperty("Безопасный ID из neuro.list_recordings. Укажите recording_id или path, но не оба.")}, {"path", stringProperty("Полный путь только к файлу внутри neurocontrol/datasets/raw (EDF/BDF) или datasets/processed (F32/JSON). Любой иной путь отклоняется.")}}, {}, false));
		a.append(neuroToolSchema("neuro.load_model", "Загрузить JSON-модель из разрешённого каталога neurocontrol/models.", QJsonObject{{"model_id", stringProperty("Имя файла модели из neuro.list_models, с .json или без.")}}, {"model_id"}, false));
		a.append(neuroToolSchema("neuro.start_playback", "Запустить выбранный EDF или визуализацию BrainFlow Synthetic Board. Synthetic Board никогда не выдаёт команд аватару.", no_args, {}, false));
		a.append(neuroToolSchema("neuro.pause_playback", "Поставить текущее воспроизведение на паузу.", no_args, {}, false));
		a.append(neuroToolSchema("neuro.stop_playback", "Остановить поток/воспроизведение, снять разрешение NeuroControl и отправить STOP контроллеру.", no_args, {}, false));
		a.append(neuroToolSchema("neuro.seek_playback", "Перейти к позиции в текущей загруженной записи EDF.", QJsonObject{{"position_seconds", numberProperty("Позиция от 0 до длительности записи в секундах.", 0.0, 86400.0)}}, {"position_seconds"}, false));
		a.append(neuroToolSchema("neuro.set_avatar_control", "Явно разрешить/запретить управление аватаром. Разрешение возможно только при совместимой модели и активном воспроизведении реальной записи EDF.", QJsonObject{{"enabled", booleanProperty("true — запросить разрешение; false — немедленно снять разрешение и отправить STOP.")}}, {"enabled"}, false));
		a.append(neuroToolSchema("neuro.emergency_stop", "НЕМЕДЛЕННО установить STOP и снять разрешение управления; если фоновое отключение LSL ещё завершается, ответ укажет lsl_cleanup_pending, а neuro.get_lsl_status покажет завершение рабочего потока.", no_args, {}, false));
		a.append(neuroToolSchema("neuro.train_model", "Запустить существующее обучение CSP + LDA в отдельном процессе. Поддерживаются только настроенные задачи PhysioNet feet/hands и разрешённые параметры; результат проверяется через neuro.get_training_status.",
			QJsonObject{{"task", QJsonObject{{"type", "string"}, {"enum", QJsonArray{"feet", "hands"}}}},
			{"low_hz", numberProperty("Нижняя частота фильтра, Гц.", 0.5, 40)}, {"high_hz", numberProperty("Верхняя частота фильтра, Гц.", 5, 79)},
			{"window_seconds", numberProperty("Длительность эпохи, секунды.", 1, 8)}, {"step_seconds", numberProperty("Шаг окна, секунды.", 0.5, 8)},
			{"offset_seconds", numberProperty("Смещение от начала события, секунды.", 0, 3)}, {"threshold", numberProperty("Порог положительного класса.", 0.55, 0.95)},
			{"notch_50", booleanProperty("Включить режекторный фильтр 50 Гц.")}, {"channels", QJsonObject{{"type", "array"}, {"items", QJsonObject{{"type", "string"}}}, {"minItems", 2}, {"maxItems", 64}}}}, {}, false));
		a.append(neuroToolSchema("neuro.run_diagnostics", "Выполнить безопасную проверку состояния файлов, Python, записи, модели, совместимости, данных и контроллера без запуска оборудования или внешних команд.", no_args, {}, true));
		return a;
	}();
	return schemas;
}


QJsonArray neuroKnowledgeResources()
{
	static const QJsonArray resources = [] {
		QJsonArray a;
		const QMap<QString, QString> docs{{"overview", "NEUROCONTROL_OVERVIEW.md"}, {"architecture", "NEUROCONTROL_ARCHITECTURE.md"},
			{"glossary", "NEUROCONTROL_GLOSSARY_RU.md"}, {"datasets", "NEUROCONTROL_DATASETS.md"}, {"models", "NEUROCONTROL_MODELS.md"},
			{"tools", "NEUROCONTROL_MCP_TOOLS.md"}, {"experiments", "NEUROCONTROL_EXPERIMENTS.md"}, {"troubleshooting", "NEUROCONTROL_TROUBLESHOOTING.md"},
			{"medical_eeg", "NEUROCONTROL_MEDICAL_EEG.md"}, {"ai_instructions", "NEUROCONTROL_SAFE_USE.md"}};
		for(auto it = docs.cbegin(); it != docs.cend(); ++it)
		{
			const QString uri = QStringLiteral("metasiberia://neurocontrol/%1").arg(it.key());
			a.append(QJsonObject{{"uri", uri}, {"name", it.key()}, {"title", it.value()}, {"mimeType", "text/markdown"}, {"description", QStringLiteral("Тематическая документация NeuroControl на русском языке.")}});
		}
		return a;
	}();
	return resources;
}


QMap<QString, QString> neuroKnowledgeFileNames()
{
	return {{"overview", "NEUROCONTROL_OVERVIEW.md"}, {"architecture", "NEUROCONTROL_ARCHITECTURE.md"},
		{"glossary", "NEUROCONTROL_GLOSSARY_RU.md"}, {"datasets", "NEUROCONTROL_DATASETS.md"}, {"models", "NEUROCONTROL_MODELS.md"},
		{"tools", "NEUROCONTROL_MCP_TOOLS.md"}, {"experiments", "NEUROCONTROL_EXPERIMENTS.md"}, {"troubleshooting", "NEUROCONTROL_TROUBLESHOOTING.md"},
		{"medical_eeg", "NEUROCONTROL_MEDICAL_EEG.md"}, {"ai_instructions", "NEUROCONTROL_SAFE_USE.md"}};
}


bool isPathWithinDirectory(const QString& root, const QString& path)
{
	const QString canonical_root = QDir(root).canonicalPath();
	const QString canonical_path = QFileInfo(path).canonicalFilePath();
	if(canonical_root.isEmpty() || canonical_path.isEmpty()) return false;
	const QString relative = QDir(canonical_root).relativeFilePath(canonical_path);
	return relative != QStringLiteral("..") && !relative.startsWith(QStringLiteral("../")) && !QDir::isAbsolutePath(relative);
}


QString findNeuroKnowledgeRoot()
{
	for(QString start : {QDir::currentPath(), QFileInfo(QString::fromUtf8(__FILE__)).absolutePath()})
	{
		QDir dir(start);
		for(int depth = 0; depth < 10; ++depth)
		{
			if(dir.exists(QStringLiteral("neurocontrol/knowledge"))) return dir.filePath(QStringLiteral("neurocontrol"));
			if(dir.dirName() == QStringLiteral("neurocontrol") && dir.exists(QStringLiteral("knowledge"))) return dir.absolutePath();
			if(!dir.cdUp()) break;
		}
	}
	return QString();
}


std::string spliceNeuroResourcesIntoList(const std::string& response)
{
	QJsonDocument document = QJsonDocument::fromJson(QByteArray::fromStdString(response));
	if(document.isNull()) return response;
	QJsonObject root = document.object();
	if(root.contains(QStringLiteral("error")) || !root.value(QStringLiteral("result")).isObject()) return response;
	QJsonObject result = root.value(QStringLiteral("result")).toObject();
	QJsonArray resources = result.value(QStringLiteral("resources")).toArray();
	for(const QJsonValue& resource : neuroKnowledgeResources())
	{
		const QString uri = resource.toObject().value(QStringLiteral("uri")).toString();
		for(int i = resources.size() - 1; i >= 0; --i)
			if(resources.at(i).toObject().value(QStringLiteral("uri")).toString() == uri) resources.removeAt(i);
		resources.append(resource);
	}
	result.insert(QStringLiteral("resources"), resources);
	root.insert(QStringLiteral("result"), result);
	document.setObject(root);
	return document.toJson(QJsonDocument::Compact).toStdString();
}


std::string spliceNeuroResourcesIntoInitializeResponse(const std::string& response)
{
	QJsonDocument document = QJsonDocument::fromJson(QByteArray::fromStdString(response));
	if(document.isNull() || !document.isObject()) return response;
	QJsonObject root = document.object();
	if(root.contains(QStringLiteral("error")) || !root.value(QStringLiteral("result")).isObject()) return response;
	QJsonObject result = root.value(QStringLiteral("result")).toObject();
	QJsonObject capabilities = result.value(QStringLiteral("capabilities")).toObject();
	QJsonObject resources = capabilities.value(QStringLiteral("resources")).toObject();
	resources.insert(QStringLiteral("subscribe"), false);
	resources.insert(QStringLiteral("listChanged"), false);
	capabilities.insert(QStringLiteral("resources"), resources);
	result.insert(QStringLiteral("capabilities"), capabilities);
	const QString neuro_instructions = QStringLiteral(
		"MetaSiberia NeuroControl is a local research module. Use neuro.* tools to inspect the live Qt widget and approved local project data; read metasiberia://neurocontrol resources or neuro.get_knowledge for verified documentation. "
		"For past experiments, call neuro.list_experiments, then neuro.get_experiment_summary and page neuro.get_experiment_events with the returned session_id/cursor. These tools expose only the client's bounded local journal, never arbitrary paths or raw EEG samples. "
		"Treat recorded EEG and BrainFlow Synthetic Board as different sources: synthetic data never represents brain activity and must never authorize avatar control. "
		"LSL is available for live visualization, diagnostics and recording. After loading a model, the user may explicitly enable the GUI's shadow-classification checkbox only after unique EEG channel names and supported units are mapped, the measured receive rate is stable, and montage/reference metadata matches or the operator explicitly attests when those fields are absent. A causal FIR resampler handles supported sample-rate differences. Known mismatches or stream gaps block classification; shadow predictions are diagnostic only and never send avatar commands. neuro.get_lsl_status exposes resampler/shadow counters, rates and block reasons. neuro.audit_lsl_recording accepts an optional target_sample_rate (default 160 Hz), independent of the loaded model; it verifies the sidecar, complete binary structure and both stored checksums, then reports integrity_status and integrity_reason. It runs no classifier and sends no avatar commands. neuro.save_lsl_audit_report writes only aggregate metrics/reasons under the local ignored neurocontrol/docs/experiments tree and refuses to overwrite an existing SNN report. Archived chunk timestamps cannot establish source production rate. On the user's request use neuro.get_status, neuro.select_source, neuro.configure_lsl, neuro.discover_lsl_streams, neuro.get_lsl_status, neuro.connect_lsl_stream, neuro.configure_lsl_visualization, neuro.get_lsl_timestamps, neuro.run_lsl_diagnostic, neuro.start_lsl_recording, neuro.stop_lsl_recording, neuro.get_lsl_recording_status and neuro.disconnect_lsl_stream in sequence. The diagnostic tool observes the requested interval, saves a metadata-only report and disconnects automatically. After the recording is finalized and the live stream is disconnected, use neuro.list_lsl_recordings and neuro.audit_lsl_recording to verify and process it, then neuro.save_lsl_audit_report with the chosen SNN ID; use neuro.load_lsl_recording for visualization; archived display and timestamp pages are available through neuro.play_lsl_recording, neuro.pause_playback, neuro.resume_playback, neuro.seek_lsl_recording, neuro.get_lsl_recording_timestamps and neuro.stop_lsl_recording_playback. "
		"If discovery finds no streams, do not infer that a running Neurosoft application is publishing EEG; report that no numeric LSL stream is visible and ask the user to enable Neurosoft's supported LSL export/registration. Never simulate medical-device UI actions or change device settings. "
		"For state-changing actions, first inspect neuro.get_status and act only on the user's explicit request. Keep raw EEG on this computer. Report unavailable hardware or rejected commands with the returned reason.");
	const QString existing_instructions = result.value(QStringLiteral("instructions")).toString().trimmed();
	result.insert(QStringLiteral("instructions"), existing_instructions.isEmpty() ? neuro_instructions : existing_instructions + QStringLiteral("\n\n") + neuro_instructions);
	root.insert(QStringLiteral("result"), result);
	document.setObject(root);
	return document.toJson(QJsonDocument::Compact).toStdString();
}


std::string spliceRenderViewIntoToolsList(const std::string& response, bool animation_available, bool text_mesh_available, bool neuro_control_available, bool blender_available)
{
	// This handler is part of the Qt local bridge. Parse the envelope rather than
	// matching raw JSON text: the server may pretty-print its tool schema.
	QJsonDocument document = QJsonDocument::fromJson(QByteArray::fromStdString(response));
	QJsonObject root = document.object();
	QJsonObject result = root.value("result").toObject();
	if(!result.value("tools").isArray())
		return response;
	QJsonArray tools = result.value("tools").toArray();
	// The local implementation is authoritative for a view of this client's world.
	for(int i = tools.size() - 1; i >= 0; --i)
		if(tools.at(i).toObject().value("name").toString() == "render_view") tools.removeAt(i);
	tools.prepend(QJsonDocument::fromJson(QByteArray(RENDER_VIEW_TOOL_JSON)).object());
	tools.prepend(QJsonDocument::fromJson(QByteArray(INSPECT_TERRAIN_TOOL_JSON)).object());
	tools.prepend(QJsonDocument::fromJson(QByteArray(SMOOTH_TERRAIN_TOOL_JSON)).object());
	tools.prepend(QJsonDocument::fromJson(QByteArray(SCULPT_TERRAIN_TOOL_JSON)).object());
	tools.prepend(QJsonDocument::fromJson(QByteArray(PAINT_TERRAIN_MAP_TOOL_JSON)).object());
	tools.prepend(QJsonDocument::fromJson(QByteArray(IMPORT_TERRAIN_FROM_MAP_TOOL_JSON)).object());
	tools.prepend(QJsonDocument::fromJson(QByteArray(CONFIGURE_TERRAIN_TOOL_JSON)).object());
	tools.prepend(QJsonDocument::fromJson(QByteArray(INSPECT_WORLD_ENVIRONMENT_TOOL_JSON)).object());
	tools.prepend(QJsonDocument::fromJson(QByteArray(CONFIGURE_WORLD_ENVIRONMENT_TOOL_JSON)).object());
	for(const QJsonValue& tool : MCPTextures::schemas()) tools.append(tool);
	for(const QJsonValue& tool : MCPImages::schemas()) tools.append(tool);
	if(animation_available) for(const QJsonValue& tool : MCPAnimationTools::schemas()) tools.append(tool);
	if(text_mesh_available) for(const QJsonValue& tool : MCPTextMeshTools::schemas()) tools.append(tool);
	if(neuro_control_available)
	{
		// The connected desktop client owns this registry. Replace any server
		// copy on every tools/list response so a newer server cannot leave stale
		// schemas behind or advertise duplicate names after reconnect/version drift.
		for(int i = tools.size() - 1; i >= 0; --i)
			if(tools.at(i).toObject().value(QStringLiteral("name")).toString().startsWith(QStringLiteral("neuro."))) tools.removeAt(i);
		for(const QJsonValue& schema : neuroControlToolSchemas()) tools.append(schema);
	}
	if(blender_available)
	{
		for(int i = tools.size() - 1; i >= 0; --i)
			if(tools.at(i).toObject().value(QStringLiteral("name")).toString().startsWith(QStringLiteral("blender."))) tools.removeAt(i);
		for(const QJsonValue& schema : MCPBlenderBridge::toolSchemas()) tools.append(schema);
	}
	result.insert("tools", tools);
	root.insert("result", result);
	document.setObject(root);
	return document.toJson(QJsonDocument::Compact).toStdString();
}


bool isFiniteVec(const Vec3d& v)
{
	return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
}

} // anonymous namespace


MCPClientRequestHandler::MCPClientRequestHandler(const MCPClientForwardingTarget& forwarding_target, const MCPClientRenderCallback& render_callback_, const MCPClientStatusCallback& status_callback_, const std::function<bool()>& poly_haven_enabled_, const MCPClientTerrainCallback& terrain_callback_, const MCPClientTerrainCallback& animation_callback_, const MCPClientTerrainCallback& text_mesh_callback_, const MCPClientNeuroControlCallback& neuro_control_callback_, const MCPClientBlenderCallback& blender_callback_)
:	server_hostname(forwarding_target.server_hostname),
	render_callback(render_callback_),
	terrain_callback(terrain_callback_),
	animation_callback(animation_callback_),
	text_mesh_callback(text_mesh_callback_),
	neuro_control_callback(neuro_control_callback_),
	blender_callback(blender_callback_),
	status_callback(status_callback_),
	poly_haven_enabled(poly_haven_enabled_),
	keepalive_configured(false)
{
	http_client = new HTTPClient();
	http_client->max_data_size = 16 * 1024 * 1024;

	if(!forwarding_target.username.empty() && !forwarding_target.password.empty())
	{
		const std::string login_header = "Substrata-Login " +
			StringUtils::convertByteArrayToHexString((const unsigned char*)forwarding_target.username.data(), forwarding_target.username.size()) + "." +
			StringUtils::convertByteArrayToHexString((const unsigned char*)forwarding_target.password.data(), forwarding_target.password.size());
		// Keep the standard header and a dedicated copy.  Some TLS reverse-proxy
		// configurations drop Authorization before forwarding to the game
		// webserver; the second header keeps the local bridge authenticated without
		// putting credentials into the MCP URL or Codex configuration.
		http_client->additional_headers.push_back("Authorization: " + login_header);
		http_client->additional_headers.push_back("X-Substrata-Login: " + login_header);
		// The embedded webserver always parses standard Cookie headers, while a
		// reverse proxy or an older HTTP listener may drop custom auth headers.
		// Keep the same token in a short-lived request cookie as a transport
		// fallback; it is sent only over the HTTPS MCP connection.
		http_client->additional_headers.push_back("Cookie: mcp-login=" + login_header.substr(std::string("Substrata-Login ").size()));
		// The same user can be connected to the main world or a personal world.
		// Use an explicit sentinel for the empty root-world name so the server can
		// distinguish it from older clients that did not send this header.
		http_client->additional_headers.push_back("X-Substrata-World: " + (forwarding_target.world_name.empty() ? "__root__" : forwarding_target.world_name));
	}
}


void MCPClientRequestHandler::reportStatus(const std::string& message) const
{
	if(status_callback)
		status_callback(message);
}


std::string MCPClientRequestHandler::forwardToServer(const std::string& request_body)
{
	if(!isValidServerHostname(server_hostname))
		throw glare::Exception("Not connected to a valid server.");

	const std::string server_mcp_url = "https://" + server_hostname + "/mcp";
	for(int attempt=0; attempt<2; ++attempt)
	{
		try
		{
			if(!keepalive_configured)
			{
				const URL url = URL::parseURL(server_mcp_url);
				http_client->connectAndEnableKeepAlive(url.scheme, url.host, url.port);
				keepalive_configured = true;
			}

			std::string response;
			const HTTPClient::ResponseInfo response_info = http_client->sendPost(server_mcp_url, request_body, /*content type=*/"application/json", response);
			if(response_info.response_code < 200 || response_info.response_code >= 300)
				throw glare::Exception("Substrata MCP returned HTTP " + toString(response_info.response_code) + ".");
			size_t first_non_whitespace = 0;
			while(first_non_whitespace < response.size() && isWhitespace(response[first_non_whitespace]))
				first_non_whitespace++;
			if(first_non_whitespace >= response.size() || response[first_non_whitespace] != '{')
				throw glare::Exception("Substrata MCP returned an invalid JSON response.");
			return response;
		}
		catch(HTTPClientExcep& e)
		{
			http_client->resetConnection();
			if(!((e.excepType() == HTTPClientExcep::ExcepType_ConnectionClosedGracefully) && attempt == 0))
				throw;
		}
		catch(glare::Exception&)
		{
			http_client->resetConnection();
			throw;
		}
	}

	throw glare::Exception("Unreachable");
}


void MCPClientRequestHandler::handleRenderView(const JSONParser& parser, const JSONNode& root, web::ReplyInfo& reply_info)
{
	const std::string id_json = extractIdJSON(parser, root);
	try
	{
		if(!render_callback)
			throw glare::Exception("render_view is not available in this client build.");

		const JSONNode& params = root.getChildObject(parser, "params");
		if(!params.hasChild("arguments"))
			throw glare::Exception("render_view requires 'arguments'.");
		const JSONNode& args = params.getChildObject(parser, "arguments");

		const JSONNode& cam_pos_node = args.getChildObject(parser, "cam_pos");
		const JSONNode& ang_node = args.getChildObject(parser, "cam_angles");

		MCPClientRenderRequest request;
		request.cam_pos = Vec3d(cam_pos_node.getChildDoubleValue(parser, "x"), cam_pos_node.getChildDoubleValue(parser, "y"), cam_pos_node.getChildDoubleValue(parser, "z"));
		request.cam_angles = Vec3d(
			ang_node.getChildDoubleValue(parser, "heading"),
			ang_node.getChildDoubleValue(parser, "pitch"),
			ang_node.getChildDoubleValueWithDefaultVal(parser, "roll", /*default=*/0.0));
		const double width = args.getChildDoubleValueWithDefaultVal(parser, "width", /*default=*/1024);
		const double height = args.getChildDoubleValueWithDefaultVal(parser, "height", /*default=*/768);
		if(!std::isfinite(width) || !std::isfinite(height) || width < 16 || width > 2048 || height < 16 || height > 2048 ||
			width != std::floor(width) || height != std::floor(height))
			throw glare::Exception("width/height must be integers in [16, 2048].");
		request.width = (int)width;
		request.height = (int)height;

		if(!isFiniteVec(request.cam_pos) || !isFiniteVec(request.cam_angles))
			throw glare::Exception("Camera position and angles must be finite.");

		reportStatus("Doing MCP render...");
		MCPClientRenderResult render_result;
		render_callback(request, render_result); // Integration owns GUI-thread marshalling and timeout.
		if(render_result.encoded_image.empty())
			throw glare::Exception("The render callback returned no image data.");
		if(render_result.mime_type != "image/jpeg" && render_result.mime_type != "image/png")
			throw glare::Exception("The render callback returned an unsupported image type.");

		std::string b64;
		Base64::encode(render_result.encoded_image.data(), render_result.encoded_image.size(), b64);
		const std::string result = "{\"content\":[{\"type\":\"image\",\"data\":\"" + b64 + "\",\"mimeType\":\"" + render_result.mime_type + "\"}],\"isError\":false}";
		writeJSONRPCResult(reply_info, id_json, result);
	}
	catch(glare::Exception& e)
	{
		conPrint("MCP client: render_view failed: " + e.what());
		const std::string result = "{\"content\":[{\"type\":\"text\",\"text\":\"" + web::Escaping::JSONEscape(e.what()) + "\"}],\"isError\":true}";
		writeJSONRPCResult(reply_info, id_json, result);
	}
	catch(std::exception& e)
	{
		const std::string result = "{\"content\":[{\"type\":\"text\",\"text\":\"" + web::Escaping::JSONEscape(e.what()) + "\"}],\"isError\":true}";
		writeJSONRPCResult(reply_info, id_json, result);
	}
}


void MCPClientRequestHandler::handleRequest(const web::RequestInfo& request_info, web::ReplyInfo& reply_info)
{
	if(!isLoopbackAddress(request_info.client_ip_address))
	{
		web::ResponseUtils::writeHTTPUnauthorizedHeaderAndData(reply_info, "The MCP endpoint may only be accessed from localhost.");
		return;
	}
	if(request_info.path != "/mcp")
	{
		web::ResponseUtils::writeHTTPNotFoundHeaderAndData(reply_info, "Not found.");
		return;
	}
	if(!StringUtils::equalCaseInsensitive(request_info.verb, "post"))
	{
		writeJSONRPCError(reply_info, "null", -32600, "The MCP endpoint accepts POST requests only.");
		return;
	}
	if(request_info.post_content.size() > MAX_MCP_REQUEST_BODY_SIZE)
	{
		writeJSONRPCError(reply_info, "null", -32600, "MCP request body is too large.");
		return;
	}

	const std::string body((const char*)request_info.post_content.data(), request_info.post_content.size());
	std::string method;
	std::string id_json = "null";
	try
	{
		JSONParser parser;
		parser.parseBuffer(body.data(), body.size());
		if(parser.nodes.empty() || parser.nodes[0].type != JSONNode::Type_Object)
			throw glare::Exception("Expected a JSON-RPC object.");
		const JSONNode& root = parser.nodes[0];
		id_json = extractIdJSON(parser, root);

		if(!root.hasChild("jsonrpc") || root.getChildStringValue(parser, "jsonrpc") != "2.0" || !root.hasChild("method"))
			throw glare::Exception("Expected a JSON-RPC 2.0 request with a method.");
	method = root.getChildStringValue(parser, "method");
		if(method.empty() || method.size() > 256)
			throw glare::Exception("Invalid JSON-RPC method.");
		// MCP notifications do not have a response body.  Returning a JSON-RPC
		// response with id=null makes streamable HTTP clients close the transport.
		if(method == "notifications/initialized" && !root.hasChild("id"))
		{
			web::ResponseUtils::writeRawString(reply_info, "HTTP/1.1 202 Accepted\r\nConnection: Keep-Alive\r\nContent-Length: 0\r\n\r\n");
			return;
		}
		if(method == "resources/read" && root.hasChild("params"))
		{
			const QJsonObject json_request = QJsonDocument::fromJson(QByteArray::fromStdString(body)).object();
			const QString uri = json_request.value(QStringLiteral("params")).toObject().value(QStringLiteral("uri")).toString();
			const QString prefix = QStringLiteral("metasiberia://neurocontrol/");
			if(uri.startsWith(prefix) && neuro_control_callback)
			{
				const QString topic = uri.mid(prefix.size());
				const QMap<QString, QString> docs = neuroKnowledgeFileNames();
				if(!docs.contains(topic))
				{
					writeJSONRPCError(reply_info, id_json, -32602, "Unknown NeuroControl knowledge resource.");
					return;
				}
				const QString docs_root = findNeuroKnowledgeRoot();
				const QString path = docs_root.isEmpty() ? QString() : QDir(docs_root).filePath(QStringLiteral("knowledge/%1").arg(docs.value(topic)));
				if(path.isEmpty() || !QFileInfo::exists(path) || !isPathWithinDirectory(docs_root, path))
				{
					writeJSONRPCError(reply_info, id_json, -32603, "NeuroControl knowledge document is unavailable.");
					return;
				}
				QFile file(path);
				if(!file.open(QIODevice::ReadOnly) || file.size() > 128 * 1024)
				{
					writeJSONRPCError(reply_info, id_json, -32603, "NeuroControl knowledge document could not be read.");
					return;
				}
				const QJsonObject contents{{"uri", uri}, {"mimeType", "text/markdown"}, {"text", QString::fromUtf8(file.readAll())}};
				writeJSONRPCResult(reply_info, id_json, QJsonDocument(QJsonObject{{"contents", QJsonArray{contents}}}).toJson(QJsonDocument::Compact).toStdString());
				return;
			}
		}

		if(method == "tools/call" && root.hasChild("params"))
		{
			const JSONNode& params = root.getChildObject(parser, "params");
			const QString name=QString::fromStdString(params.getChildStringValue(parser,"name"));
			if(name.startsWith(QStringLiteral("blender.")))
			{
				try
				{
					if(!blender_callback) throw glare::Exception("Metasiberia Blender tools are unavailable in this client session.");
					const QJsonObject request = QJsonDocument::fromJson(QByteArray::fromStdString(body)).object();
					const QJsonValue arg_value = request.value(QStringLiteral("params")).toObject().value(QStringLiteral("arguments"));
					if(!arg_value.isUndefined() && !arg_value.isObject()) throw glare::Exception("Blender tool arguments must be an object.");
					QJsonObject tool_result = blender_callback(name, arg_value.toObject());
					QJsonArray content;
					const QJsonObject image = tool_result.take(QStringLiteral("_mcp_image")).toObject();
					if(!image.isEmpty())
					{
						const QString mime = image.value(QStringLiteral("mimeType")).toString();
						const QString data = image.value(QStringLiteral("data")).toString();
						if(mime != QStringLiteral("image/jpeg") || data.isEmpty() || data.size() > 4 * 1024 * 1024)
							throw glare::Exception("Blender returned an invalid or oversized MCP image block.");
						content.append(QJsonObject{{"type", "image"}, {"data", data}, {"mimeType", mime}});
						tool_result.insert(QStringLiteral("image_included"), true);
					}
					const QString text = QString::fromUtf8(QJsonDocument(tool_result).toJson(QJsonDocument::Compact));
					content.append(QJsonObject{{"type", "text"}, {"text", text}});
					const QJsonObject envelope{{"isError", false}, {"structuredContent", tool_result}, {"content", content}};
					writeJSONRPCResult(reply_info, id_json, QJsonDocument(envelope).toJson(QJsonDocument::Compact).toStdString());
				}
				catch(const std::exception& e)
				{
					const QString message = QString::fromUtf8(e.what()).left(2048);
					const QJsonObject error{{"code", "BLENDER_BRIDGE_FAILED"}, {"message", message}};
					const QJsonObject envelope{{"isError", true}, {"structuredContent", QJsonObject{{"error", error}}},
						{"content", QJsonArray{QJsonObject{{"type", "text"}, {"text", QStringLiteral("BLENDER_BRIDGE_FAILED: %1").arg(message)}}}}};
					writeJSONRPCResult(reply_info, id_json, QJsonDocument(envelope).toJson(QJsonDocument::Compact).toStdString());
				}
				return;
			}
			if(name.startsWith(QStringLiteral("neuro.")))
			{
				try
				{
					if(!neuro_control_callback) throw glare::Exception("NeuroControl tools are unavailable in this client build.");
					const QJsonObject request = QJsonDocument::fromJson(QByteArray::fromStdString(body)).object();
					const QJsonValue arg_value = request.value(QStringLiteral("params")).toObject().value(QStringLiteral("arguments"));
					if(!arg_value.isUndefined() && !arg_value.isObject()) throw glare::Exception("NeuroControl tool arguments must be an object.");
					QJsonObject tool_result = neuro_control_callback(name, arg_value.toObject());
					const QJsonObject error = tool_result.take(QStringLiteral("_mcp_error")).toObject();
					const bool is_error = !error.isEmpty();
					const QJsonObject structured = is_error ? QJsonObject{{"error", error}} : tool_result;
					const QString response_text = is_error ? QStringLiteral("%1: %2").arg(error.value(QStringLiteral("code")).toString(), error.value(QStringLiteral("message")).toString()) :
						QString::fromUtf8(QJsonDocument(structured).toJson(QJsonDocument::Compact));
					const QJsonObject envelope{{"isError", is_error}, {"structuredContent", structured},
						{"content", QJsonArray{QJsonObject{{"type", "text"}, {"text", response_text}}}}};
					writeJSONRPCResult(reply_info, id_json, QJsonDocument(envelope).toJson(QJsonDocument::Compact).toStdString());
				}
				catch(const glare::Exception& e)
				{
					const QString message = QString::fromStdString(e.what());
					const QJsonObject error{{"code", "MCP_CALLBACK_FAILED"}, {"message", message}};
					const QJsonObject envelope{{"isError", true}, {"structuredContent", QJsonObject{{"error", error}}},
						{"content", QJsonArray{QJsonObject{{"type", "text"}, {"text", QStringLiteral("MCP_CALLBACK_FAILED: %1").arg(message)}}}}};
					writeJSONRPCResult(reply_info, id_json, QJsonDocument(envelope).toJson(QJsonDocument::Compact).toStdString());
				}
				catch(const std::exception& e)
				{
					const QString message = QString::fromUtf8(e.what());
					const QJsonObject error{{"code", "MCP_CALLBACK_FAILED"}, {"message", message}};
					const QJsonObject envelope{{"isError", true}, {"structuredContent", QJsonObject{{"error", error}}},
						{"content", QJsonArray{QJsonObject{{"type", "text"}, {"text", QStringLiteral("MCP_CALLBACK_FAILED: %1").arg(message)}}}}};
					writeJSONRPCResult(reply_info, id_json, QJsonDocument(envelope).toJson(QJsonDocument::Compact).toStdString());
				}
				return;
			}
			if(MCPAnimationTools::handles(name) || MCPTextMeshTools::handles(name))
			{
				try {
					const auto& callback = MCPTextMeshTools::handles(name) ? text_mesh_callback : animation_callback;
					if(!callback) throw glare::Exception("The requested native editor bridge is unavailable in this client.");
					const QJsonValue arguments = QJsonDocument::fromJson(QByteArray::fromStdString(body)).object().value("params").toObject().value("arguments");
					if(!arguments.isUndefined() && !arguments.isObject()) throw glare::Exception("Animation tool arguments must be an object.");
					const QJsonObject result = callback(name, arguments.toObject());
					const QJsonObject envelope{{"isError",false},{"structuredContent",result},{"content",QJsonArray{QJsonObject{{"type","text"},{"text",QString::fromUtf8(QJsonDocument(result).toJson(QJsonDocument::Compact))}}}}};
					writeJSONRPCResult(reply_info,id_json,QJsonDocument(envelope).toJson(QJsonDocument::Compact).toStdString());
				} catch(const glare::Exception& e) {
					writeJSONRPCResult(reply_info,id_json,"{\"isError\":true,\"content\":[{\"type\":\"text\",\"text\":\""+web::Escaping::JSONEscape(e.what())+"\"}]}");
				} catch(const std::exception& e) {
					writeJSONRPCResult(reply_info,id_json,"{\"isError\":true,\"content\":[{\"type\":\"text\",\"text\":\""+web::Escaping::JSONEscape(e.what())+"\"}]}");
				}
				return;
			}
			if(MCPTextures::handles(name) || MCPImages::handles(name))
			{
				try {
					const auto server_call=[this](const QString& tool,const QJsonObject& args) {
						const QJsonObject request{{"jsonrpc","2.0"},{"id",1},{"method","tools/call"},{"params",QJsonObject{{"name",tool},{"arguments",args}}}};
						const QJsonObject response=QJsonDocument::fromJson(QByteArray::fromStdString(forwardToServer(QJsonDocument(request).toJson(QJsonDocument::Compact).toStdString()))).object();
						const QJsonObject result=response.value("result").toObject();
						if(response.contains("error")) MCPTextures::fail(response.value("error").toObject().value("message").toString());
						if(result.value("isError").toBool()) MCPTextures::fail(result.value("content").toArray().at(0).toObject().value("text").toString());
						if(!result.value("structuredContent").isObject()) MCPTextures::fail("Server did not return texture data. Update the server to the texture release.");
						return result.value("structuredContent").toObject();
					};
					const QJsonObject args=QJsonDocument::fromJson(QByteArray::fromStdString(body)).object().value("params").toObject().value("arguments").toObject();
					const QJsonObject result=MCPImages::handles(name) ? MCPImages::call(name,args,server_call) :
						MCPTextures::call(name,args,poly_haven_enabled && poly_haven_enabled(),server_call);
					const QString text=QString::fromUtf8(QJsonDocument(result).toJson(QJsonDocument::Compact));
					const QJsonObject envelope{{"isError",false},{"structuredContent",result},{"content",QJsonArray{QJsonObject{{"type","text"},{"text",text}}}}};
					writeJSONRPCResult(reply_info,id_json,QJsonDocument(envelope).toJson(QJsonDocument::Compact).toStdString());
				} catch(const glare::Exception& e) {
					writeJSONRPCResult(reply_info,id_json,"{\"isError\":true,\"content\":[{\"type\":\"text\",\"text\":\""+web::Escaping::JSONEscape(e.what())+"\"}]}");
				}
				return;
			}
			if(params.hasChild("name") && params.getChildStringValue(parser, "name") == "render_view")
			{
				handleRenderView(parser, root, reply_info);
				return;
			}
			if(params.hasChild("name"))
			{
				const QString tool_name=QString::fromStdString(params.getChildStringValue(parser,"name"));
				if(tool_name == "inspect_terrain" || tool_name == "smooth_terrain" || tool_name == "sculpt_terrain" ||
					tool_name == "paint_terrain_map" || tool_name == "import_terrain_from_map" || tool_name == "configure_terrain" ||
					tool_name == "inspect_world_environment" || tool_name == "configure_world_environment")
				{
					if(!terrain_callback)
						throw glare::Exception("World settings tools are not available in this client build.");
					const QJsonObject arguments=QJsonDocument::fromJson(QByteArray::fromStdString(body)).object().value("params").toObject().value("arguments").toObject();
					const QJsonObject result=terrain_callback(tool_name, arguments);
					const QString result_text=QString::fromUtf8(QJsonDocument(result).toJson(QJsonDocument::Compact));
					const QJsonObject envelope{{"isError",false},{"structuredContent",result},{"content",QJsonArray{QJsonObject{{"type","text"},{"text",result_text}}}}};
					writeJSONRPCResult(reply_info,id_json,QJsonDocument(envelope).toJson(QJsonDocument::Compact).toStdString());
					return;
				}
			}
		}
	}
	catch(glare::Exception& e)
	{
		writeJSONRPCError(reply_info, id_json, -32600, e.what());
		return;
	}

	try
	{
		reportStatus("Handling MCP '" + method + "' method.");
		if(!isValidServerHostname(server_hostname))
		{
			const QJsonObject request = QJsonDocument::fromJson(QByteArray::fromStdString(body)).object();
			if(MCPClientLocalOnlyProtocol::handlesMethod(QString::fromStdString(method)))
			{
				const QByteArray local_response = MCPClientLocalOnlyProtocol::makeResponse(QString::fromStdString(method),
					request.value(QStringLiteral("id")), request.value(QStringLiteral("params")).toObject(),
					blender_callback ? MCPBlenderBridge::toolSchemas() : QJsonArray(),
					QString::fromStdString(cyberspace_version));
				web::ResponseUtils::writeHTTPOKHeaderAndData(reply_info, local_response.constData(), (size_t)local_response.size(), "application/json");
				return;
			}
			writeJSONRPCError(reply_info, id_json, -32000,
				"No Metasiberia world is connected. Only local Blender MCP tools are available in this session.");
			return;
		}
		// Keep NeuroControl tool registration on this desktop bridge. Its
		// tools/list response is composed locally below, and tools/call is
		// dispatched to the live widget above. Sending the registry upstream
		// couples discovery to server limits and version, though the server does
		// not execute these desktop-only tools.
		std::string response = forwardToServer(body);
		if(method == "initialize" && neuro_control_callback)
			response = spliceNeuroResourcesIntoInitializeResponse(response);
		if(method == "tools/list" && (render_callback || terrain_callback || animation_callback || text_mesh_callback || neuro_control_callback || blender_callback))
			response = spliceRenderViewIntoToolsList(response, (bool)animation_callback, (bool)text_mesh_callback, (bool)neuro_control_callback, (bool)blender_callback);
		if(method == "resources/list" && neuro_control_callback) response = spliceNeuroResourcesIntoList(response);
		web::ResponseUtils::writeHTTPOKHeaderAndData(reply_info, response.data(), response.size(), /*content type=*/"application/json");
	}
	catch(glare::Exception& e)
	{
		writeJSONRPCError(reply_info, id_json, -32603, "Forwarding to Substrata server failed: " + std::string(e.what()));
	}
}


MCPClientSharedRequestHandler::MCPClientSharedRequestHandler()
{}


MCPClientSharedRequestHandler::~MCPClientSharedRequestHandler()
{
	clearForwardingTarget();
}


bool MCPClientSharedRequestHandler::configureForwardingTargetFromCredentialManager(const std::string& server_hostname, const std::string& world_name, CredentialManager& credential_manager)
{
	clearForwardingTarget();
	if(!isValidServerHostname(server_hostname))
		return false;
	if(world_name.size() > 1000 || world_name.find_first_of("\r\n") != std::string::npos)
		return false;

	const std::string username = credential_manager.getUsernameForDomain(server_hostname);
	const std::string password = credential_manager.getDecryptedPasswordForDomain(server_hostname);
	if(username.empty() || password.empty() || username.size() > 4096 || password.size() > 4096)
		return false;

	forwarding_target.server_hostname = server_hostname;
	forwarding_target.world_name = world_name;
	forwarding_target.username = username;
	forwarding_target.password = password;
	return true;
}


void MCPClientSharedRequestHandler::clearForwardingTarget()
{
	std::fill(forwarding_target.password.begin(), forwarding_target.password.end(), '\0');
	forwarding_target = MCPClientForwardingTarget();
}


bool MCPClientSharedRequestHandler::isConfigured() const
{
	return isValidServerHostname(forwarding_target.server_hostname) && !forwarding_target.username.empty() && !forwarding_target.password.empty();
}


Reference<web::RequestHandler> MCPClientSharedRequestHandler::getOrMakeRequestHandler()
{
	return new MCPClientRequestHandler(forwarding_target, render_callback, status_callback, poly_haven_enabled, terrain_callback, animation_callback, text_mesh_callback, neuro_control_callback, blender_callback);
}
