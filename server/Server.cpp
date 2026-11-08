/*=====================================================================
Server.cpp
----------
Copyright Glare Technologies Limited 2023 -
=====================================================================*/
#include "Server.h"


#include "ServerConfig.h"
#include "ListenerThread.h"
#include "UDPHandlerThread.h"
#include "MeshLODGenThread.h"
#include "DynamicTextureUpdaterThread.h"
#include "ChunkGenThread.h"
#include "WorkerThread.h"
#include "ServerTestSuite.h"
#include "WorldCreation.h"
#include "LuaHTTPRequestManager.h"
#include "WorldMaintenance.h"
#include "LLMThread.h"
#include "ChatBot.h"
#include "../shared/Protocol.h"
#include "../shared/GameStateDelta.h"
#include "../shared/Version.h"
#include "../shared/MetasiberiaPaths.h"
#include "../shared/MessageUtils.h"
#include "../shared/SubstrataLuaVM.h"
#include "../shared/ObjectEventHandlers.h"
#include "../shared/WorldStateLock.h"
#include "../webserver/WebServerRequestHandler.h"
#include "../webserver/AccountHandlers.h"
#include "../webserver/WebDataStore.h"
#include "../webserver/WebDataFileWatcherThread.h"
#if USE_GLARE_PARCEL_AUCTION_CODE
#include <webserver/CoinbasePollerThread.h>
#include <webserver/OpenSeaPollerThread.h>
#include <server/AuctionManagement.h>
#endif
#include <webserver/WebListenerThread.h>
#include <networking/Networking.h>
#include <networking/TLSSocket.h>
#include <maths/PCG32.h>
#include <maths/Matrix4f.h>
#include <maths/Quat.h>
#include <maths/Rect2.h>
#include <graphics/BasisDecoder.h>
#include <utils/ThreadManager.h>
#include <utils/PlatformUtils.h>
#include <utils/Clock.h>
#include <utils/Timer.h>
#include <utils/FileUtils.h>
#include <utils/ConPrint.h>
#include <utils/Exception.h>
#include <utils/StringUtils.h>
#include <utils/Parser.h>
#include <utils/XMLParseUtils.h>
#include <utils/IndigoXMLDoc.h>
#include <utils/ArgumentParser.h>
#include <utils/SocketBufferOutStream.h>
#include <utils/OpenSSL.h>
#include <tls.h>
#include <algorithm>
#include <cmath>
#if !defined(_WIN32)
#include <signal.h>
#endif


static bool shouldGenerateTextureDerivativesForURLOnServer(const URLString& URL)
{
	return hasExtension(URL, "jpg") || hasExtension(URL, "jpeg") || hasExtension(URL, "png") || hasExtension(URL, "gif");
}


static std::string scriptKeyNameForCode(uint32 key_code)
{
	if((key_code >= Protocol::ScriptKey_CharacterBase + 'a' && key_code <= Protocol::ScriptKey_CharacterBase + 'z') ||
		(key_code >= Protocol::ScriptKey_CharacterBase + '0' && key_code <= Protocol::ScriptKey_CharacterBase + '9'))
		return std::string(1, (char)(key_code - Protocol::ScriptKey_CharacterBase));

	switch(key_code)
	{
	case Protocol::ScriptKey_Left: return "left";
	case Protocol::ScriptKey_Right: return "right";
	case Protocol::ScriptKey_Up: return "up";
	case Protocol::ScriptKey_Down: return "down";
	case Protocol::ScriptKey_Space: return "space";
	case Protocol::ScriptKey_Enter: return "enter";
	case Protocol::ScriptKey_Escape: return "escape";
	case Protocol::ScriptKey_W: return "w";
	case Protocol::ScriptKey_A: return "a";
	case Protocol::ScriptKey_S: return "s";
	case Protocol::ScriptKey_D: return "d";
	case Protocol::ScriptKey_GamepadA: return "gamepad_a";
	case Protocol::ScriptKey_GamepadB: return "gamepad_b";
	case Protocol::ScriptKey_GamepadX: return "gamepad_x";
	case Protocol::ScriptKey_GamepadY: return "gamepad_y";
	case Protocol::ScriptKey_GamepadL1: return "gamepad_l1";
	case Protocol::ScriptKey_GamepadR1: return "gamepad_r1";
	case Protocol::ScriptKey_GamepadL3: return "gamepad_l3";
	case Protocol::ScriptKey_GamepadR3: return "gamepad_r3";
	case Protocol::ScriptKey_GamepadStart: return "gamepad_start";
	case Protocol::ScriptKey_GamepadSelect: return "gamepad_select";
	case Protocol::ScriptKey_GamepadUp: return "gamepad_up";
	case Protocol::ScriptKey_GamepadDown: return "gamepad_down";
	case Protocol::ScriptKey_GamepadLeft: return "gamepad_left";
	case Protocol::ScriptKey_GamepadRight: return "gamepad_right";
	case Protocol::ScriptKey_MouseLeft: return "mouse_left";
	case Protocol::ScriptKey_MouseMiddle: return "mouse_middle";
	case Protocol::ScriptKey_MouseRight: return "mouse_right";
	default: return std::string();
	}
}


static void enqueueTextureDerivativeChecksForLoadedWorlds(Server& server)
{
	std::set<URLString> URLs_to_check;

	{
		WorldStateLock lock(server.world_state->mutex);

		for(auto world_it = server.world_state->world_states.begin(); world_it != server.world_state->world_states.end(); ++world_it)
		{
			ServerWorldState* world = world_it->second.ptr();

			for(auto it = world->getObjects(lock).begin(); it != world->getObjects(lock).end(); ++it)
			{
				DependencyURLSet URLs;
				WorldObject::GetDependencyOptions options;
				options.use_basis = false;
				options.include_lightmaps = false;
				options.get_optimised_mesh = false;
				it->second->getDependencyURLSetBaseLevel(options, URLs);

				for(auto URL_it = URLs.begin(); URL_it != URLs.end(); ++URL_it)
					if(shouldGenerateTextureDerivativesForURLOnServer(URL_it->URL))
						URLs_to_check.insert(URL_it->URL);
			}

			std::set<DependencyURL> world_setting_URLs;
			world->world_settings.getDependencyURLSet(world_setting_URLs);
			for(auto URL_it = world_setting_URLs.begin(); URL_it != world_setting_URLs.end(); ++URL_it)
				if(shouldGenerateTextureDerivativesForURLOnServer(URL_it->URL))
					URLs_to_check.insert(URL_it->URL);
		}

		for(auto user_it = server.world_state->user_id_to_users.begin(); user_it != server.world_state->user_id_to_users.end(); ++user_it)
		{
			Avatar avatar;
			avatar.avatar_settings = user_it->second->avatar_settings;

			DependencyURLSet URLs;
			Avatar::GetDependencyOptions options;
			options.use_basis = false;
			options.get_optimised_mesh = false;
			avatar.getDependencyURLSetBaseLevel(options, URLs);

			for(auto URL_it = URLs.begin(); URL_it != URLs.end(); ++URL_it)
				if(shouldGenerateTextureDerivativesForURLOnServer(URL_it->URL))
					URLs_to_check.insert(URL_it->URL);
		}
	}

	for(auto it = URLs_to_check.begin(); it != URLs_to_check.end(); ++it)
		server.enqueueMsgForLodGenThread(new CheckGenLodResourcesForURL(*it));

	if(!URLs_to_check.empty())
		conPrint("Queued " + toString(URLs_to_check.size()) + " texture derivative check(s) for existing resources.");
}


void updateMapTiles(ServerAllWorldsState& world_state)
{
	WorldStateLock lock(world_state.mutex);
	MapTileInfo& map_tile_info = world_state.getMapTileInfoForWorld(/*world_name=*/"", lock);

	const int z_begin = 0;
	const int z_end = 7;
	if(true) // world_state.map_tile_info.empty())
	{
		// world_state.map_tile_info.clear();

		for(int z = z_begin; z < z_end; ++z)
		{
			const float TILE_WIDTH_M = 5120.f / (1 << z); //TILE_WIDTH_PX * metres_per_pixel;
			//const float TILE_WIDTH_M = 2560.f/*5120.f*/ / (1 << z); //TILE_WIDTH_PX * metres_per_pixel;

			const int span = (int)std::ceil(320 / TILE_WIDTH_M);
			const int neg_x_span  = (int)std::ceil(600 / TILE_WIDTH_M);  // NOTE: pushing out negative x span here to encompass rivermouth district
			const int plus_x_span = (int)std::ceil(700 / TILE_WIDTH_M);  // NOTE: pushing out positive x span here to encompass east districts
			const int plus_y_span = (int)std::ceil(530 / TILE_WIDTH_M);  // NOTE: pushing out positive y span here to encompass north district


			// We want zoom level 3 to have (half) span 2 = 2^1.
			// zoom level 4 : span = 2^(4-2) = 2^2 = 4.
			// So num tiles = (4*2)^2 = 64
			// zoom level 5 : span = 2^(5-2) = 2^3 = 8.
			// So num tiles = (8*2)^2 = 256

			// in general num_tiles = (span*2)^2 = ((2^(z-2))*2)^2 = (2^(z-1))^2 = 2^((z-1)*2) = 2^(2z - 2)
			// zoom level 6: num_tiles = 2^10 = 1024
			//const int span = 1 << myMax(0, z - 2); // 2^(z-2)

			const int x_begin = -neg_x_span;
			const int x_end = plus_x_span;
			const int y_begin = -span;
			const int y_end = plus_y_span;

			

			for(int y = y_begin; y < y_end; ++y)
			for(int x = x_begin; x < x_end; ++x)
			{
				const Vec3<int> v(x, y, z);

				if(map_tile_info.info.count(v) == 0)
				{

					TileInfo info;
					info.cur_tile_screenshot = new Screenshot();
					info.cur_tile_screenshot->id = world_state.getNextScreenshotUIDUnlocked(lock);
					info.cur_tile_screenshot->created_time = TimeStamp::currentTime();
					info.cur_tile_screenshot->state = Screenshot::ScreenshotState_notdone;
					info.cur_tile_screenshot->screenshot_type = Screenshot::ScreenshotType_MapTile;
					info.cur_tile_screenshot->is_map_tile = true;
					info.cur_tile_screenshot->tile_x = x;
					info.cur_tile_screenshot->tile_y = y;
					info.cur_tile_screenshot->tile_z = z;

					map_tile_info.info[v] = info;

					conPrint("Added map tile screenshot: " + v.toString());

					world_state.markAsChanged();

					map_tile_info.db_dirty = true;

					PendingMapTileScreenshot pending;
					pending.world_name = ""; // root world
					pending.tile_coords = v;
					world_state.pending_map_tile_screenshots.push_back(pending);
				}
			}
		}
	}
	else
	{
		// TEMP: Redo screenshot
		/*for(auto it = world_state.map_tile_info.begin(); it != world_state.map_tile_info.end(); ++it)
		{
			it->second.cur_tile_screenshot->state = Screenshot::ScreenshotState_notdone;
		}*/
	}
}


static void enqueueMessageToBroadcast(SocketBufferOutStream& packet_buffer, std::vector<uint8>& broadcast_packets)
{
	MessageUtils::updatePacketLengthField(packet_buffer);

	if(packet_buffer.buf.size() > 0)
	{
		const size_t write_i = broadcast_packets.size();
		broadcast_packets.resize(write_i + packet_buffer.buf.size());
		std::memcpy(&broadcast_packets[write_i], packet_buffer.buf.data(), packet_buffer.buf.size());
	}
}


static std::string getServerStateFilePath(const std::string& server_state_dir, const char* metasiberia_filename, const char* legacy_filename)
{
	const std::string metasiberia_path = server_state_dir + "/" + metasiberia_filename;
	return FileUtils::fileExists(metasiberia_path) ? metasiberia_path : server_state_dir + "/" + legacy_filename;
}


// Throws glare::Exception on failure.
static ServerCredentials parseServerCredentials(const std::string& path)
{

	const std::string contents = FileUtils::readEntireFileTextMode(path);

	ServerCredentials creds;

	Parser parser(contents);

	while(!parser.eof())
	{
		string_view key, value;
		parser.parseToCharOrEOF(':', key);
		if(parser.eof())
			break;

		if(!parser.parseChar(':'))
			throw glare::Exception("Error parsing ':' from '" + path + "'.");

		parser.parseWhiteSpace();
		parser.parseLine(value);

		creds.creds[::stripHeadAndTailWhitespace(toString(key))] = ::stripHeadAndTailWhitespace(toString(value));
	}

	return creds;
}


static ServerConfig parseServerConfig(const std::string& config_path)
{
	IndigoXMLDoc doc(config_path);
	pugi::xml_node root_elem = doc.getRootElement();

	ServerConfig config;
	config.webserver_fragments_dir				= XMLParseUtils::parseStringWithDefault(root_elem, "webserver_fragments_dir", /*default val=*/"");
	config.webserver_public_files_dir			= XMLParseUtils::parseStringWithDefault(root_elem, "webserver_public_files_dir", /*default val=*/"");
	config.webclient_dir						= XMLParseUtils::parseStringWithDefault(root_elem, "webclient_dir", /*default val=*/"");
	config.tls_certificate_path					= XMLParseUtils::parseStringWithDefault(root_elem, "tls_certificate_path", /*default val=*/"");
	config.tls_private_key_path					= XMLParseUtils::parseStringWithDefault(root_elem, "tls_private_key_path", /*default val=*/"");
	config.canonical_web_hostname				= XMLParseUtils::parseStringWithDefault(root_elem, "canonical_web_hostname", /*default val=*/"");
	config.letsencrypt_webroot_dir				= XMLParseUtils::parseStringWithDefault(root_elem, "letsencrypt_webroot_dir", /*default val=*/"");
	config.web_http_port						= XMLParseUtils::parseIntWithDefault(root_elem, "web_http_port", /*default val=*/80);
	config.web_https_port						= XMLParseUtils::parseIntWithDefault(root_elem, "web_https_port", /*default val=*/443);
	config.allow_light_mapper_bot_full_perms	= XMLParseUtils::parseBoolWithDefault(root_elem, "allow_light_mapper_bot_full_perms", /*default val=*/false);
	config.update_parcel_sales					= XMLParseUtils::parseBoolWithDefault(root_elem, "update_parcel_sales", /*default val=*/false);
	config.do_lua_http_request_rate_limiting	= XMLParseUtils::parseBoolWithDefault(root_elem, "do_lua_http_request_rate_limiting", /*default val=*/true);
	config.enable_LOD_chunking					= XMLParseUtils::parseBoolWithDefault(root_elem, "enable_LOD_chunking", /*default val=*/true);
	config.enable_registration					= XMLParseUtils::parseBoolWithDefault(root_elem, "enable_registration", /*default val=*/true);
	config.AI_model_id							= XMLParseUtils::parseStringWithDefault(root_elem, "AI_model_id", /*default val=*/"xai/grok-4-1-fast-non-reasoning");
	config.shared_LLM_prompt_part				= XMLParseUtils::parseStringWithDefault(root_elem, "shared_LLM_prompt_part", /*default val=*/
		std::string("You are a helpful bot in the Metasiberia metaverse.\n") +
		std::string("When a user first talks to you, wave hello to them using the perform_wave_gesture tool function, or bow to them using the perform_bow_gesture tool function.\n") +
		std::string("Bear in mind that other users may chat to each other while standing near you, in which case you don't need to say anything.\n") +
		std::string("You MUST start each response with either [SPEAK] or [SILENT].  After the [SPEAK] prefix the chat message should follow, for example: '[SPEAK] Hello there user, how are you?'.\n") +
		std::string("After the [SILENT] prefix, write your reasoning for staying silent, for example: '[SILENT] The user is speaking to another user, I don't need to join in this conversation.'\n") +
		std::string("Messages that do not begin with a [SPEAK] or [SILENT] prefix will be ignored.\n")
	);
	return config;
}


static bool isFeatureFlagSet(Reference<ServerAllWorldsState>& worlds_state, uint64 flag)
{
	WorldStateLock lock(worlds_state->mutex);
	return BitUtils::isBitSet(worlds_state->feature_flag_info.feature_flags, flag);
}


static glare::AtomicInt should_quit(0);


#if !defined(_WIN32)
static void signalHandler(int signal)
{
	assert(signal == SIGTERM || signal == SIGINT);
	should_quit = 1;
}
#endif


int main(int argc, char *argv[])
{
	Clock::init();
	Networking::init();
	PlatformUtils::ignoreUnixSignals();
	TLSSocket::initTLS();
	BasisDecoder::init();

	// Listen for SIGTERM and SIGINT on Linux and Mac.
	// Upon receiving SIGTERM or SIGINT, save dirty data to database, then try and shut down gracefully.
#if !defined(_WIN32)
	// Set a signal handler for SIGTERM
	struct sigaction act;
	act.sa_handler = signalHandler;
	sigemptyset(&act.sa_mask);
	act.sa_flags = 0;
	if(sigaction(SIGTERM, &act, /*oldact=*/nullptr) != 0)
		fatalError("Failed to set SIGTERM signal handler: " + PlatformUtils::getLastErrorString());
	if(sigaction(SIGINT, &act, /*oldact=*/nullptr) != 0)
		fatalError("Failed to set SIGINT signal handler: " + PlatformUtils::getLastErrorString());
#endif

	conPrint("Metasiberia server v" + ::cyberspace_version);

	try
	{
		//---------------------- Parse and process comment line arguments -------------------------
		std::map<std::string, std::vector<ArgumentParser::ArgumentType> > syntax;
		syntax["--enable_dev_mode"] = std::vector<ArgumentParser::ArgumentType>();
		syntax["--test"] = std::vector<ArgumentParser::ArgumentType>();
		syntax["--save_sanitised_database"] = std::vector<ArgumentParser::ArgumentType>(1, ArgumentParser::ArgumentType_string); // One string arg
		syntax["--compact_database_in_place"] = std::vector<ArgumentParser::ArgumentType>(1, ArgumentParser::ArgumentType_string); // One string arg
		syntax["--dump_objects_near"] = std::vector<ArgumentParser::ArgumentType>(4, ArgumentParser::ArgumentType_string); // db path, x, y, radius
		syntax["--db_path"] = std::vector<ArgumentParser::ArgumentType>(1, ArgumentParser::ArgumentType_string); // One string arg: path to database file on disk
		syntax["--do_not_load_resources"] = std::vector<ArgumentParser::ArgumentType>();

		std::vector<std::string> args;
		for(int i=0; i<argc; ++i)
			args.push_back(argv[i]);

		ArgumentParser parsed_args(args, syntax, /*allow_unnamed_arg=*/false);

		const bool dev_mode = parsed_args.isArgPresent("--enable_dev_mode");

		Server server;

		// Run tests if --test is present.
		if(parsed_args.isArgPresent("--test") || parsed_args.getUnnamedArg() == "--test")
		{
			ServerTestSuite::test();
			return 0;
		}
		//-----------------------------------------------------------------------------------------


		const int listen_port = 7600; // Listen port for sub protocol

#if defined(_WIN32)
		const std::string server_state_dir = MetasiberiaPaths::getWindowsServerStateDirectory();
#else
		const std::string home_dir = PlatformUtils::getEnvironmentVariable("HOME");
		const std::string server_state_dir = home_dir + "/cyberspace_server_state";
#endif
		conPrint("server_state_dir: " + server_state_dir);
		FileUtils::createDirIfDoesNotExist(server_state_dir);


		// Parse server config, if present:
		ServerConfig server_config;
		{
			const std::string config_path = getServerStateFilePath(server_state_dir, "metasiberia_server_config.xml", "substrata_server_config.xml");
			if(FileUtils::fileExists(config_path))
			{
				conPrint("Parsing server config from '" + config_path + "'...");
				server_config = parseServerConfig(config_path);
			}
			else
				conPrint("server config not found at '" + config_path + "', using default configuration values instead.");
		}

		server.config = server_config;
		server.world_state->server_config = server_config;

		// Parse server credentials
		try
		{
			const std::string credentials_path = getServerStateFilePath(server_state_dir, "metasiberia_server_credentials.txt", "substrata_server_credentials.txt");
			const ServerCredentials server_credentials = parseServerCredentials(credentials_path);
			server.world_state->server_credentials = server_credentials;
		}
		catch(glare::Exception& e)
		{
			conPrint("WARNING: Error while loading server credentials: " + e.what());
		}


		const std::string server_resource_dir = server_state_dir + "/server_resources";
		FileUtils::createDirIfDoesNotExist(server_resource_dir);

		server.world_state->resource_manager = new ResourceManager(server_resource_dir, /*resources_db_path (not used on server)=*/"dummy");


		// Add all files from /dist_resources into the resource manager.
		{
			const std::vector<std::string> paths = FileUtils::getFilesInDirFullPaths(server_state_dir + "/dist_resources/");
			for(size_t i=0; i<paths.size(); ++i)
				server.world_state->resource_manager->addExternalResource(/*URL=*/URLString(FileUtils::getFilename(paths[i])), /*local_abs_path=*/paths[i]);


			// Issue some warnings if files not present
			const URLString xbot_mesh_URL = "xbot_glb_3242545562312850498.bmesh";
			if(!server.world_state->resource_manager->isFileForURLPresent(xbot_mesh_URL))
				conPrint("WARNING: file '" + toStdString(xbot_mesh_URL) + "' did not exist, default avatar model will be missing for webclient users.");

			const URLString example_gesture_URL = "Excited.subanim";
			if(!server.world_state->resource_manager->isFileForURLPresent(example_gesture_URL))
				conPrint("WARNING: file '" + toStdString(example_gesture_URL) + "' did not exist, gesture animations will be missing for users.");
		}


		// Reads database at the path given by arg 0, writes a sanitised and compacted database at arg 0 path, with "_sanitised" appended to filename.
		if(parsed_args.isArgPresent("--save_sanitised_database"))
		{
			const std::string src_db_path = parsed_args.getArgStringValue("--save_sanitised_database");
			const std::string sanitised_db_path = ::removeDotAndExtension(src_db_path) + "_sanitised.bin";

			// Copy database from src database path to sanitised path.
			FileUtils::copyFile(/*src=*/src_db_path, /*dest=*/sanitised_db_path);

			server.world_state->readFromDisk(sanitised_db_path);

			server.world_state->saveSanitisedDatabase();

			server.world_state = NULL; // Close database

			Database db;
			db.removeOldRecordsOnDisk(sanitised_db_path); // Remove deleted and old records from the database file.
			return 0;
		}

		// Removes old append-only records without making a full-size copy first.  This is
		// intended for emergency compaction of very large database files.
		if(parsed_args.isArgPresent("--compact_database_in_place"))
		{
			const std::string db_path = parsed_args.getArgStringValue("--compact_database_in_place");
			Database db;
			const size_t ignored_tail_bytes = db.removeOldRecordsOnDiskBestEffort(db_path);
			if(ignored_tail_bytes > 0)
				conPrint("Compacted database while ignoring " + toString(ignored_tail_bytes) + " bytes of trailing corrupt data.");
			return 0;
		}


#if defined(_WIN32) || defined(OSX)
		server.screenshot_dir = server_state_dir + "/screenshots"; // Dir generated screenshots will be saved to.
#else
		server.screenshot_dir = "/var/www/cyberspace/screenshots";
#endif
		FileUtils::createDirIfDoesNotExist(server.screenshot_dir);

#if defined(_WIN32) || defined(OSX)
		server.photo_dir = server_state_dir + "/photos"; // Dir uploaded photos will be saved to.
#else
		server.photo_dir = "/var/www/cyberspace/photos";
#endif
		FileUtils::createDirIfDoesNotExist(server.photo_dir);

		std::string server_state_path;
		if(parsed_args.isArgPresent("--dump_objects_near"))
			server_state_path = parsed_args.getArgStringValue("--dump_objects_near", 0);
		else if(parsed_args.isArgPresent("--db_path"))
			server_state_path = parsed_args.getArgStringValue("--db_path");
		else
			server_state_path = server_state_dir + "/server_state.bin"; // If --db_path is not on command line, use default path.

		if(FileUtils::fileExists(server_state_path))
			server.world_state->readFromDisk(server_state_path);
		else
			server.world_state->createNewDatabase(server_state_path);

		// Older MCP object creation could leave its source mesh on disk without a
		// present resource record. Restore only meshes referenced by live objects;
		// otherwise WebClient receives a 404 even though the .bmesh file exists.
		{
			WorldStateLock lock(server.world_state->mutex);
			std::set<URLString> checked_model_urls;
			size_t restored_model_resources = 0;
			for(auto world_it = server.world_state->world_states.begin(); world_it != server.world_state->world_states.end(); ++world_it)
			for(auto ob_it = world_it->second->getObjects(lock).begin(); ob_it != world_it->second->getObjects(lock).end(); ++ob_it)
			{
				const WorldObject& ob = *ob_it->second.ptr();
				const URLString& URL = ob.model_url;
				if(!hasPrefix(URL, "mcp_model_") || !hasExtension(URL, "bmesh") || !checked_model_urls.insert(URL).second)
					continue;
				const std::string raw_path = server.world_state->resource_manager->computeDefaultRawLocalPathForURL(URL);
				const std::string expected_path = server_resource_dir + "/" + raw_path;
				if(!FileUtils::fileExists(expected_path))
					continue;
				ResourceRef resource = server.world_state->resource_manager->getOrCreateResourceForURL(URL);
				if(resource->getState() != Resource::State_Present || !FileUtils::fileExists(server.world_state->resource_manager->getLocalAbsPathForResource(*resource)))
				{
					resource->setRawLocalPath(raw_path);
					resource->setState(Resource::State_Present);
					server.world_state->addResourceAsDBDirty(resource);
					restored_model_resources++;
				}
			}
			if(restored_model_resources > 0)
				conPrint("Restored " + toString(restored_model_resources) + " MCP model resource(s) from disk.");
		}

		// Saved missing-resource records can replace bundled resources registered before
		// loading the database. Restore their distribution copies without changing owners
		// or paths of resources that are already present.
		for(const std::string& path : FileUtils::getFilesInDirFullPaths(server_state_dir + "/dist_resources/"))
		{
			const URLString URL(FileUtils::getFilename(path));
			if(!server.world_state->resource_manager->isFileForURLPresent(URL))
				server.world_state->resource_manager->addExternalResource(URL, path);
		}

		if(parsed_args.isArgPresent("--dump_objects_near"))
		{
			const double centre_x = stringToDouble(parsed_args.getArgStringValue("--dump_objects_near", 1));
			const double centre_y = stringToDouble(parsed_args.getArgStringValue("--dump_objects_near", 2));
			const double radius = stringToDouble(parsed_args.getArgStringValue("--dump_objects_near", 3));
			const double radius2 = radius * radius;

			WorldStateLock lock(server.world_state->mutex);
			size_t num_found = 0;
			for(auto world_it = server.world_state->world_states.begin(); world_it != server.world_state->world_states.end(); ++world_it)
			{
				ServerWorldState* world = world_it->second.ptr();
				for(auto ob_it = world->getObjects(lock).begin(); ob_it != world->getObjects(lock).end(); ++ob_it)
				{
					const WorldObject* ob = ob_it->second.ptr();
					const double dx = ob->pos.x - centre_x;
					const double dy = ob->pos.y - centre_y;
					const double dist2 = dx * dx + dy * dy;
					if(dist2 <= radius2)
					{
						std::string content_preview = ob->content;
						for(size_t i = 0; i < content_preview.size(); ++i)
							if(content_preview[i] == '\n' || content_preview[i] == '\r' || content_preview[i] == '\t')
								content_preview[i] = ' ';
						conPrint(
							"NEAR_OBJECT dist=" + doubleToStringNDecimalPlaces(std::sqrt(dist2), 3) +
							" world='" + world_it->first + "'" +
							" uid=" + ob->uid.toString() +
							" type=" + WorldObject::objectTypeString(ob->object_type) +
							" pos=(" + doubleToStringNDecimalPlaces(ob->pos.x, 3) + "," + doubleToStringNDecimalPlaces(ob->pos.y, 3) + "," + doubleToStringNDecimalPlaces(ob->pos.z, 3) + ")" +
							" scale=(" + doubleToStringNDecimalPlaces(ob->scale.x, 3) + "," + doubleToStringNDecimalPlaces(ob->scale.y, 3) + "," + doubleToStringNDecimalPlaces(ob->scale.z, 3) + ")" +
							" model='" + toStdString(ob->model_url) + "'" +
							" target='" + ob->target_url + "'" +
							" content='" + content_preview.substr(0, 160) + "'"
						);
						num_found++;
					}
				}
			}

			conPrint("NEAR_OBJECT_COUNT " + toString(num_found));
			return 0;
		}

		if(parsed_args.isArgPresent("--do_not_load_resources"))
		{
			Lock lock(server.world_state->resource_manager->getMutex());
			server.world_state->resource_manager->getResourcesForURL().clear();
		}


		WorldCreation::createParcelsAndRoads(server.world_state);

		// WorldCreation::removeHypercardMaterials(*server.world_state);

		updateMapTiles(*server.world_state);

		// updateToUseImageCubeMeshes(*server.world_state);
		
		server.world_state->denormaliseData();





		// If there are explicit paths to cert file and private key file in server config, use them, otherwise use default paths.
		std::string tls_certificate_path, tls_private_key_path;
		if(!server_config.tls_certificate_path.empty())
		{
			tls_certificate_path = server_config.tls_certificate_path;
			tls_private_key_path = server_config.tls_private_key_path;
		}
		else
		{
			tls_certificate_path = server_state_dir + "/MyCertificate.crt"; // Use some default paths
			tls_private_key_path = server_state_dir + "/MyKey.key";

			// See https://substrata.info/running_your_own_server , 'Generating a TLS keypair'.
		}

		conPrint("tls_certificate_path: " + tls_certificate_path);
		conPrint("tls_private_key_path: " + tls_private_key_path);
		
		if(!FileUtils::fileExists(tls_certificate_path))
			throw glare::Exception("ERROR: No file found at TLS certificate path '" + tls_certificate_path + "'");
		if(!FileUtils::fileExists(tls_private_key_path))
			throw glare::Exception("ERROR: No file found at TLS private key path '" + tls_private_key_path + "'");


		//----------------------------------------------- Launch webserver -----------------------------------------------
		// Create TLS configuration
		struct tls_config* web_tls_configuration = tls_config_new();

		if(tls_config_set_cert_file(web_tls_configuration, tls_certificate_path.c_str()) != 0)
			throw glare::Exception("tls_config_set_cert_file failed: " + getTLSConfigErrorString(web_tls_configuration));

		if(tls_config_set_key_file(web_tls_configuration, tls_private_key_path.c_str()) != 0) // set private key
			throw glare::Exception("tls_config_set_key_file failed: " + getTLSConfigErrorString(web_tls_configuration));

		Reference<WebDataStore> web_data_store = new WebDataStore();
		server.world_state->web_data_store = web_data_store.ptr();

		std::string default_fragments_dir, default_webclient_dir, default_webserver_public_files_dir;
#if defined(_WIN32) || defined(OSX)
		default_fragments_dir				= server_state_dir + "/webserver_fragments";
		default_webserver_public_files_dir	= server_state_dir + "/webserver_public_files";
		default_webclient_dir				= server_state_dir + "/webclient";
#else
		default_fragments_dir				= "/var/www/cyberspace/webserver_fragments";
		default_webserver_public_files_dir	= "/var/www/cyberspace/public_html";
		default_webclient_dir				= "/var/www/cyberspace/webclient";
		//web_data_store->letsencrypt_webroot			= "/var/www/cyberspace/letsencrypt_webroot";
#endif
		// Use fragments_dir from the server config.xml file if it's in there (if string is non-empty), otherwise use a default value.
		if(!server_config.webserver_fragments_dir.empty())
			web_data_store->fragments_dir = server_config.webserver_fragments_dir;
		else
			web_data_store->fragments_dir = default_fragments_dir;

		// Use webserver_public_files_dir from the server config.xml file if it's in there (if string is non-empty), otherwise use a default value.
		if(!server_config.webserver_public_files_dir.empty())
			web_data_store->public_files_dir = server_config.webserver_public_files_dir;
		else
			web_data_store->public_files_dir = default_webserver_public_files_dir;

		// Use webclient_dir from the server config.xml file if it's in there (if string is non-empty), otherwise use a default value.
		if(!server_config.webclient_dir.empty())
			web_data_store->webclient_dir = server_config.webclient_dir;
		else
			web_data_store->webclient_dir = default_webclient_dir;

		web_data_store->screenshot_dir = server.screenshot_dir;
		web_data_store->photo_dir = server.photo_dir;

		conPrint("webserver fragments_dir: " + web_data_store->fragments_dir);
		conPrint("webserver public_files_dir: " + web_data_store->public_files_dir);
		conPrint("webserver webclient_dir: " + web_data_store->webclient_dir);

		FileUtils::createDirIfDoesNotExist(web_data_store->fragments_dir);
		FileUtils::createDirIfDoesNotExist(web_data_store->public_files_dir);
		FileUtils::createDirIfDoesNotExist(web_data_store->webclient_dir);

		web_data_store->loadAndCompressFiles();

		Reference<WebServerSharedRequestHandler> shared_request_handler = new WebServerSharedRequestHandler();
		shared_request_handler->data_store = web_data_store.ptr();
		shared_request_handler->server = &server;
		shared_request_handler->world_state = server.world_state.ptr();
		shared_request_handler->dev_mode = dev_mode;

		ThreadManager web_thread_manager;
		web_thread_manager.addThread(new web::WebListenerThread(server_config.web_http_port,  shared_request_handler.getPointer(), NULL));
		web_thread_manager.addThread(new web::WebListenerThread(server_config.web_https_port, shared_request_handler.getPointer(), web_tls_configuration));


		web_thread_manager.addThread(new WebDataFileWatcherThread(web_data_store));

		//----------------------------------------------- End launch webserver -----------------------------------------------


		// While Coinbase webhooks are not working, add a Coinbase polling thread.
#if USE_GLARE_PARCEL_AUCTION_CODE
		if(!dev_mode)
			web_thread_manager.addThread(new CoinbasePollerThread(server.world_state.ptr()));

		if(!dev_mode)
			web_thread_manager.addThread(new OpenSeaPollerThread(server.world_state.ptr()));
#endif


		//----------------------------------------------- Launch Substrata protocol server -----------------------------------------------
		// Create TLS configuration for substrata protocol server
		struct tls_config* tls_configuration = tls_config_new();

		if(tls_config_set_cert_file(tls_configuration, tls_certificate_path.c_str()) != 0)
			throw glare::Exception("tls_config_set_cert_file failed: " + getTLSConfigErrorString(tls_configuration));
		
		if(tls_config_set_key_file(tls_configuration, tls_private_key_path.c_str()) != 0) // set private key
			throw glare::Exception("tls_config_set_key_file failed: " + getTLSConfigErrorString(tls_configuration));

		conPrint("Launching ListenerThread...");

		ThreadManager thread_manager;
		thread_manager.addThread(new ListenerThread(listen_port, &server, tls_configuration));
		
		conPrint("Done.");
		//----------------------------------------------- End launch substrata protocol server -----------------------------------------------

		server.mesh_lod_gen_thread_manager.addThread(new MeshLODGenThread(&server, server.world_state.ptr()));
		enqueueTextureDerivativeChecksForLoadedWorlds(server);

		if(server_config.enable_LOD_chunking)
			thread_manager.addThread(new ChunkGenThread(server.world_state.ptr()));

		server.udp_handler_thread_manager.addThread(new UDPHandlerThread(&server));

		server.dyn_tex_updater_thread_manager.addThread(new DynamicTextureUpdaterThread(&server, server.world_state.ptr()));

		server.lua_http_manager = new LuaHTTPRequestManager(&server);

		//----------------------------------------------- Create any Lua scripts for objects -----------------------------------------------
		if(isFeatureFlagSet(server.world_state, ServerAllWorldsState::SERVER_SCRIPT_EXEC_FEATURE_FLAG))
		{ // Begin scope for world_state->mutex lock
			conPrint("Creating Lua scripts for objects...");

			WorldStateLock lock(server.world_state->mutex);
			for(auto world_it = server.world_state->world_states.begin(); world_it != server.world_state->world_states.end(); ++world_it)
			{
				Reference<ServerWorldState> world_state = world_it->second;
				ServerWorldState::ObjectMapType& objects = world_state->getObjects(lock);
				for(auto i = objects.begin(); i != objects.end(); ++i)
				{
					WorldObject* ob = i->second.ptr();
					if(hasPrefix(ob->script, "--lua") && ob->creator_id.valid())
					{
						try
						{
							runtimeCheck(ob->creator_id.valid()); // Invalid UserID is the empty key so make sure is valid.

							// Get the SubstrataLuaVM for the user who created the object, or create it if it does not yet exist.
							SubstrataLuaVM* lua_vm;
							auto res = server.world_state->lua_vms.find(ob->creator_id);
							if(res == server.world_state->lua_vms.end())
							{
								conPrint("Creating new SubstrataLuaVM for user " + ob->creator_id.toString() + "...");
								Timer timer;
								lua_vm = new SubstrataLuaVM(SubstrataLuaVM::SubstrataLuaVMArgs(&server));
								conPrint("\tCreating SubstrataLuaVM took " + timer.elapsedStringMSWIthNSigFigs(4));
								server.world_state->registerLuaVMForUser(ob->creator_id, lua_vm);
							}
							else
								lua_vm = res->second.ptr();

							runtimeCheck(lua_vm);
							ob->lua_script_evaluator = new LuaScriptEvaluator(lua_vm, /*script output handler=*/&server, ob->script, ob, world_state.ptr(), lock);
						}
						catch(LuaScriptExcepWithLocation& e)
						{
							conPrint("Error creating LuaScriptEvaluator for ob " + ob->uid.toString() + ": " + e.messageWithLocations());
							server.logLuaError("Error: " + e.messageWithLocations(), ob->uid, ob->creator_id);
						}
						catch(glare::Exception& e)
						{
							conPrint("Error creating LuaScriptEvaluator for ob " + ob->uid.toString() + ": " + e.what());
							server.logLuaError("Error: " + e.what(), ob->uid, ob->creator_id);
						}
					}
				}
			}
		}
		else
			conPrint("Not creating any Lua scripts for objects, server-side script execution is disabled.");
		//----------------------------------------------- End create any Lua scripts for objects -----------------------------------------------

		// Create avatars for ChatBots
		//if(isFeatureFlagSet(server.world_state, ServerAllWorldsState::CHATBOTS_FEATURE_FLAG))
		{
			WorldStateLock lock(server.world_state->mutex);
			for(auto world_it = server.world_state->world_states.begin(); world_it != server.world_state->world_states.end(); ++world_it)
			{
				Reference<ServerWorldState> world_state = world_it->second;
				for(auto it = world_state->getChatBots(lock).begin(); it != world_state->getChatBots(lock).end(); ++it)
				{
					ChatBot* chatbot = it->second.ptr();

					AvatarRef avatar = world_state->createAndInsertAvatarForChatBot(server.world_state.ptr(), chatbot, lock);

					chatbot->avatar_uid = avatar->uid;
					chatbot->avatar = avatar;
				}
			}
		}
		//else
		//	conPrint("Chatbots are disabled via the ChatBot feature flag.");



		Timer save_state_timer;

		// A map from world to packet data to send to clients connected to that world.
		std::unordered_map<ServerWorldState*, std::vector<uint8>> broadcast_packets;

		SocketBufferOutStream scratch_packet(SocketBufferOutStream::DontUseNetworkByteOrder);

		js::Vector<ThreadMessageRef, 16> temp_thread_messages;

		// Main server loop
		uint64 loop_iter = 0;
		while(!should_quit)
		{
			PlatformUtils::Sleep(100);

			// Do Lua timer callbacks
			if(isFeatureFlagSet(server.world_state, ServerAllWorldsState::SERVER_SCRIPT_EXEC_FEATURE_FLAG))
			{
				{
					WorldStateLock lock(server.world_state->mutex);

					const double cur_time = server.total_timer.elapsed();
					// Keep one overdue-timer burst from monopolising the shared world lock.
					// Timers beyond this per-poll budget stay in the priority queue and
					// retain their original deadlines for the next server tick.
					static const size_t MAX_SCRIPT_TIMER_EVENTS_PER_POLL = 64;
					// A fixed-step game loop can invoke its callback several times for one
					// due timer. Cap actual Lua calls separately so catch-up cannot multiply
					// the work budget. This is a deterministic callback-count limit, not a
					// wall-clock deadline.
					static const size_t MAX_SCRIPT_CALLBACKS_PER_POLL = 64;
					size_t script_callbacks_this_poll = 0;
					server.timer_queue.update(cur_time, /*triggered_timers_out=*/server.temp_triggered_timers,
						MAX_SCRIPT_TIMER_EVENTS_PER_POLL);

					for(size_t i=0; i<server.temp_triggered_timers.size(); ++i)
					{
						if(script_callbacks_this_poll >= MAX_SCRIPT_CALLBACKS_PER_POLL)
						{
							// update() has removed these events from the priority queue. Put
							// unprocessed events back with their original deadlines so no timer
							// is lost when the callback budget is exhausted.
							for(size_t deferred_i=i; deferred_i<server.temp_triggered_timers.size(); ++deferred_i)
								server.timer_queue.addTimer(cur_time, server.temp_triggered_timers[deferred_i]);
							break;
						}
						ScriptTimerQueueTimer& timer = server.temp_triggered_timers[i];
			
						LuaScriptEvaluator* script_evaluator = timer.lua_script_evaluator.getPtrIfAlive();
						if(script_evaluator)
						{
							// Check timer is still valid (has not been destroyed by destroyTimer), by checking the timer id with the same index is still equal to our timer id.
							if(timer.timer_index < 0 || timer.timer_index >= LuaScriptEvaluator::MAX_NUM_TIMERS)
								continue;
							LuaScriptEvaluator::LuaTimerInfo& timer_info = script_evaluator->timers[timer.timer_index];
							if(timer.timer_id == timer_info.id)
							{
								const double delta_time_s = myMax(0.0, cur_time - timer_info.last_trigger_time);
								if(timer_info.fixed_step_s > 0.0)
								{
									const int max_catch_up_steps = 4;
									// Keep bounded simulation debt while the global timer queue is
									// busy. Four steps run per dispatch; remaining time is drained
									// over subsequent ticks instead of being discarded immediately.
									static const double MAX_FIXED_STEP_BACKLOG_S = 5.0;
									timer_info.fixed_step_accumulator_s = myMin(
										timer_info.fixed_step_accumulator_s + delta_time_s, MAX_FIXED_STEP_BACKLOG_S);
									timer_info.last_trigger_time = cur_time;
									int steps = 0;
									while(timer_info.fixed_step_accumulator_s + 1.0e-9 >= timer_info.fixed_step_s &&
										steps < max_catch_up_steps && script_callbacks_this_poll < MAX_SCRIPT_CALLBACKS_PER_POLL &&
										!script_evaluator->hit_error && timer.timer_id == timer_info.id)
									{
										script_evaluator->doOnTimerEvent(timer.onTimerEvent_ref, lock, timer_info.fixed_step_s);
										++script_callbacks_this_poll;
										timer_info.fixed_step_accumulator_s -= timer_info.fixed_step_s;
										++steps;
									}
								}
								else
								{
									timer_info.last_trigger_time = cur_time;
									script_evaluator->doOnTimerEvent(timer.onTimerEvent_ref, lock, delta_time_s);
									++script_callbacks_this_poll;
								}

								if(timer.repeating && !script_evaluator->hit_error && timer.timer_id == timer_info.id)
								{
									// Re-insert timer with updated trigger time
									timer.tigger_time = cur_time + timer.period;
									server.timer_queue.addTimer(cur_time, timer);
								}
								else if(timer.timer_id == timer_info.id) // One-shot, failed, or explicitly cancelled in callback.
								{
									script_evaluator->destroyTimer(timer.timer_index);
								}
							}
						}
					}
				}

				if(isFeatureFlagSet(server.world_state, ServerAllWorldsState::LUA_HTTP_REQUESTS_FEATURE_FLAG))
					server.lua_http_manager->think();
			}
			
			// Handle any queued messages from worker threads
			{
				server.message_queue.dequeueAnyQueuedItems(temp_thread_messages);

				for(size_t msg_i=0; msg_i<temp_thread_messages.size(); ++msg_i)
				{
					Reference<ThreadMessage> msg = temp_thread_messages[msg_i];

					if(dynamic_cast<ScriptGameHUDThreadMessage*>(msg.ptr()))
					{
						const ScriptGameHUDThreadMessage* hud_msg = static_cast<ScriptGameHUDThreadMessage*>(msg.ptr());
						server.sendGameHUDToAvatar(hud_msg->avatar_uid, hud_msg->game_object_uid,
							hud_msg->visible, hud_msg->text, hud_msg->world.ptr(), hud_msg->has_layout, hud_msg->center_x, hud_msg->top_y);
					}
					else if(dynamic_cast<ScriptGameHUDMenuThreadMessage*>(msg.ptr()))
					{
						const ScriptGameHUDMenuThreadMessage* menu_msg = static_cast<ScriptGameHUDMenuThreadMessage*>(msg.ptr());
						GameHUDMenu current_menu;
						bool participant_active = false;
						if(menu_msg->world.nonNull())
						{
							WorldStateLock world_lock(server.world_state->mutex);
							const std::vector<UID> participants = menu_msg->world->getGameSessionParticipants(menu_msg->game_object_uid, world_lock);
							for(const UID& participant_uid : participants)
								if(participant_uid == menu_msg->avatar_uid)
									participant_active = true;
							if(participant_active)
								participant_active = menu_msg->world->getGameHUDMenu(menu_msg->game_object_uid, menu_msg->avatar_uid, menu_msg->menu.panel_id, current_menu, world_lock);
						}
						if(!participant_active) current_menu.panel_id = menu_msg->menu.panel_id;
						server.sendGameHUDMenuToAvatar(menu_msg->avatar_uid, menu_msg->game_object_uid,
							current_menu, menu_msg->world.ptr());
					}
					else if(dynamic_cast<ScriptGameStateThreadMessage*>(msg.ptr()))
					{
						const ScriptGameStateThreadMessage* state_msg = static_cast<ScriptGameStateThreadMessage*>(msg.ptr());
						std::vector<UID> participants;
						if(state_msg->world.nonNull())
						{
							WorldStateLock world_lock(server.world_state->mutex);
							participants = state_msg->world->getGameSessionParticipants(state_msg->game_object_uid, world_lock);
						}
						for(const UID& participant_uid : participants)
							server.sendGameStateToAvatar(participant_uid, state_msg->game_object_uid,
								state_msg->version, state_msg->serialized_value, state_msg->world.ptr(),
								state_msg->previous_version, state_msg->previous_serialized_value);
					}
					else if(dynamic_cast<ScriptInteractionPromptThreadMessage*>(msg.ptr()))
					{
						const ScriptInteractionPromptThreadMessage* prompt_msg = static_cast<ScriptInteractionPromptThreadMessage*>(msg.ptr());
						bool updated = false;
						if(prompt_msg->world.nonNull())
						{
							WorldStateLock lock(server.world_state->mutex);
						const auto object_it = prompt_msg->world->getObjects(lock).find(prompt_msg->object_uid);
						if(object_it != prompt_msg->world->getObjects(lock).end() && object_it->second->state != WorldObject::State_Dead)
							updated = prompt_msg->world->setScriptInteractionPrompt(prompt_msg->object_uid, prompt_msg->prompt, lock);
						}
						if(updated) server.broadcastScriptInteractionPrompt(prompt_msg->object_uid, prompt_msg->prompt, prompt_msg->world.ptr());
					}
					else if(dynamic_cast<ScriptAvatarMaterialiseEffectThreadMessage*>(msg.ptr()))
					{
						const ScriptAvatarMaterialiseEffectThreadMessage* effect_msg = static_cast<ScriptAvatarMaterialiseEffectThreadMessage*>(msg.ptr());
						bool target_is_live = false;
						if(effect_msg->world.nonNull())
						{
							WorldStateLock lock(server.world_state->mutex);
							const auto avatar_it = effect_msg->world->getAvatars(lock).find(effect_msg->avatar_uid);
							target_is_live = avatar_it != effect_msg->world->getAvatars(lock).end() &&
								avatar_it->second->state != Avatar::State_Dead;
						}
						if(target_is_live)
							server.broadcastAvatarMaterialiseEffect(effect_msg->avatar_uid, effect_msg->world.ptr());
					}
					else if(dynamic_cast<ScriptGameEventThreadMessage*>(msg.ptr()))
					{
						const ScriptGameEventThreadMessage* event_msg = static_cast<ScriptGameEventThreadMessage*>(msg.ptr());
						for(const UID& participant_uid : event_msg->recipients)
							server.sendGameEventToAvatar(participant_uid, event_msg->game_object_uid,
								event_msg->sequence, event_msg->server_time, event_msg->name, event_msg->world.ptr());
					}
					else if(dynamic_cast<BotDialogOptionsThreadMessage*>(msg.ptr()))
					{
						const BotDialogOptionsThreadMessage* options_msg = static_cast<BotDialogOptionsThreadMessage*>(msg.ptr());
						server.sendBotDialogOptionsToAvatar(options_msg->avatar_uid, options_msg->bot_avatar_uid,
							options_msg->bot_name, options_msg->node_id, options_msg->options, options_msg->world.ptr());
					}
					else if(dynamic_cast<ScriptMessageThreadMessage*>(msg.ptr()))
					{
						const ScriptMessageThreadMessage* script_msg = static_cast<ScriptMessageThreadMessage*>(msg.ptr());
						server.sendScriptMessageToAvatar(script_msg->avatar_uid, script_msg->msg, script_msg->world.ptr());
					}
					else if(dynamic_cast<UserUsedObjectThreadMessage*>(msg.ptr()))
					{
						const UserUsedObjectThreadMessage* used_msg = static_cast<UserUsedObjectThreadMessage*>(msg.ptr());

						// Look up object
						WorldStateLock world_lock(server.world_state->mutex); // Just hold the world state lock while executing script event handlers for now.
						auto res = used_msg->world->getObjects(world_lock).find(used_msg->object_uid);
						if(res != used_msg->world->getObjects(world_lock).end())
						{
							WorldObject* ob = res->second.ptr();

							// Execute doOnUserUsedObject event handler in any scripts that are listening for onUserUsedObject for this object
							if(ob->event_handlers)
								ob->event_handlers->executeOnUserUsedObjectHandlers(/*avatar_uid=*/used_msg->avatar_uid, ob->uid, world_lock);
						}
					}
					else if(dynamic_cast<UserGameSessionThreadMessage*>(msg.ptr()))
					{
						const UserGameSessionThreadMessage* session_msg = static_cast<UserGameSessionThreadMessage*>(msg.ptr());
						GameStateSnapshot latest_game_state;
						bool has_game_state = false;
						bool session_active = false;
						if(session_msg->world.nonNull())
						{
							{
							WorldStateLock world_lock(server.world_state->mutex);
							const auto object_it = session_msg->world->getObjects(world_lock).find(session_msg->object_uid);
							const auto avatar_it = session_msg->world->getAvatars(world_lock).find(session_msg->avatar_uid);
							if(session_msg->active && object_it != session_msg->world->getObjects(world_lock).end() &&
								avatar_it != session_msg->world->getAvatars(world_lock).end() &&
								object_it->second->state != WorldObject::State_Dead &&
								avatar_it->second->state != Avatar::State_Dead &&
								avatar_it->second->pos.getDist2(object_it->second->pos) <= Maths::square(12.0) &&
								object_it->second->event_handlers &&
								(object_it->second->event_handlers->onUserKeyPressed_handlers.nonEmpty() ||
									object_it->second->event_handlers->onUserKeyReleased_handlers.nonEmpty() ||
									object_it->second->event_handlers->onUserGameSessionChanged_handlers.nonEmpty() ||
								 object_it->second->event_handlers->onUserGameAction_handlers.nonEmpty() ||
									 object_it->second->event_handlers->onUserGameMessage_handlers.nonEmpty() ||
									 object_it->second->event_handlers->game_action_input_binding_count > 0))
								session_active = true;

							// Membership belongs to the server world and is updated before the
							// script callback, so game.getPlayers() observes the new state.
							session_msg->world->setGameSessionParticipant(session_msg->object_uid, session_msg->avatar_uid,
								session_active, world_lock, session_msg->user_id);
							if(!session_active && session_msg->user_id.valid())
								server.world_state->clearGameStorageWriteWindow(session_msg->object_uid, session_msg->user_id);

							if(object_it != session_msg->world->getObjects(world_lock).end() &&
								object_it->second->state != WorldObject::State_Dead && object_it->second->event_handlers &&
								(!session_msg->active || (avatar_it != session_msg->world->getAvatars(world_lock).end() && session_active)))
								object_it->second->event_handlers->executeOnUserGameSessionChangedHandlers(
									session_msg->avatar_uid, session_msg->object_uid, session_active, world_lock);
							has_game_state = session_active && session_msg->world->getGameStateSnapshot(
								session_msg->object_uid, latest_game_state, world_lock);
							}
						}
						if(!session_active)
						{
							std::vector<GameHUDMenu> menus;
							if(session_msg->world.nonNull())
							{
								WorldStateLock world_lock(server.world_state->mutex);
								session_msg->world->getGameHUDMenus(session_msg->object_uid, session_msg->avatar_uid, menus, world_lock);
								session_msg->world->clearGameHUDMenus(session_msg->object_uid, session_msg->avatar_uid, world_lock);
							}
							if(menus.empty()) menus.push_back(GameHUDMenu());
							for(GameHUDMenu& menu : menus)
							{
								menu.title.clear(); menu.body.clear(); menu.actions.clear();
								server.sendGameHUDMenuToAvatar(session_msg->avatar_uid, session_msg->object_uid, menu, session_msg->world.ptr());
							}
						}
						else if(has_game_state)
							server.sendGameStateToAvatar(session_msg->avatar_uid, session_msg->object_uid,
								latest_game_state.version, latest_game_state.serialized_value, session_msg->world.ptr());
					}
					else if(dynamic_cast<UserPressedObjectKeyThreadMessage*>(msg.ptr()))
					{
						const UserPressedObjectKeyThreadMessage* key_msg = static_cast<UserPressedObjectKeyThreadMessage*>(msg.ptr());
						const std::string key_name = scriptKeyNameForCode(key_msg->key_code);
						if(!key_name.empty() && key_msg->world.nonNull())
						{
							WorldStateLock world_lock(server.world_state->mutex);
							const ServerWorldState::AvatarMapType& avatars = key_msg->world->getAvatars(world_lock);
							const auto avatar_it = avatars.find(key_msg->avatar_uid);
							const ServerWorldState::ObjectMapType& objects = key_msg->world->getObjects(world_lock);
							const auto object_it = objects.find(key_msg->object_uid);
							if(avatar_it != avatars.end() && object_it != objects.end() &&
								avatar_it->second->state != Avatar::State_Dead &&
								object_it->second->state != WorldObject::State_Dead &&
								avatar_it->second->pos.getDist2(object_it->second->pos) <= Maths::square(12.0) &&
								key_msg->world->isGameSessionParticipant(key_msg->object_uid, key_msg->avatar_uid, world_lock) &&
								object_it->second->event_handlers &&
								(object_it->second->event_handlers->onUserKeyPressed_handlers.nonEmpty() ||
								 object_it->second->event_handlers->game_action_input_binding_count > 0))
							{
								// Record the transition before callbacks so a game loop can
								// query held state from inside onUserKeyPressed.
								key_msg->world->setGameSessionKeyDown(key_msg->object_uid, key_msg->avatar_uid,
									key_name, /*down=*/true, world_lock);
								if(object_it->second->event_handlers->onUserKeyPressed_handlers.nonEmpty())
									object_it->second->event_handlers->executeOnUserKeyPressedHandlers(
										key_msg->avatar_uid, key_msg->object_uid, key_name, world_lock);
							}
						}
					}
					else if(dynamic_cast<UserReleasedObjectKeyThreadMessage*>(msg.ptr()))
					{
						const UserReleasedObjectKeyThreadMessage* key_msg = static_cast<UserReleasedObjectKeyThreadMessage*>(msg.ptr());
						const std::string key_name = scriptKeyNameForCode(key_msg->key_code);
						if(!key_name.empty() && key_msg->world.nonNull())
						{
							WorldStateLock world_lock(server.world_state->mutex);
							const ServerWorldState::AvatarMapType& avatars = key_msg->world->getAvatars(world_lock);
							const auto avatar_it = avatars.find(key_msg->avatar_uid);
							const ServerWorldState::ObjectMapType& objects = key_msg->world->getObjects(world_lock);
							const auto object_it = objects.find(key_msg->object_uid);
							if(avatar_it != avatars.end() && object_it != objects.end() &&
								avatar_it->second->state != Avatar::State_Dead &&
								object_it->second->state != WorldObject::State_Dead &&
								avatar_it->second->pos.getDist2(object_it->second->pos) <= Maths::square(12.0) &&
								key_msg->world->isGameSessionParticipant(key_msg->object_uid, key_msg->avatar_uid, world_lock))
							{
								key_msg->world->setGameSessionKeyDown(key_msg->object_uid, key_msg->avatar_uid,
									key_name, /*down=*/false, world_lock);
								if(object_it->second->event_handlers &&
									object_it->second->event_handlers->onUserKeyReleased_handlers.nonEmpty())
									object_it->second->event_handlers->executeOnUserKeyReleasedHandlers(
										key_msg->avatar_uid, key_msg->object_uid, key_name, world_lock);
							}
						}
					}
					else if(dynamic_cast<UserGameActionThreadMessage*>(msg.ptr()))
					{
						const UserGameActionThreadMessage* action_msg = static_cast<UserGameActionThreadMessage*>(msg.ptr());
						if(action_msg->world.nonNull())
						{
							WorldStateLock world_lock(server.world_state->mutex);
							const ServerWorldState::AvatarMapType& avatars = action_msg->world->getAvatars(world_lock);
							const auto avatar_it = avatars.find(action_msg->avatar_uid);
							const ServerWorldState::ObjectMapType& objects = action_msg->world->getObjects(world_lock);
							const auto object_it = objects.find(action_msg->object_uid);
							const std::vector<UID> participants = action_msg->world->getGameSessionParticipants(action_msg->object_uid, world_lock);
							const bool active_participant = std::find(participants.begin(), participants.end(), action_msg->avatar_uid) != participants.end();
							if(active_participant && avatar_it != avatars.end() && object_it != objects.end() &&
								avatar_it->second->state != Avatar::State_Dead && object_it->second->state != WorldObject::State_Dead &&
								avatar_it->second->pos.getDist2(object_it->second->pos) <= Maths::square(12.0) &&
					(action_msg->values.empty() ?
						action_msg->world->isGameHUDActionAllowed(action_msg->object_uid, action_msg->avatar_uid, action_msg->action_id, world_lock) :
						action_msg->world->isGameHUDFormSubmitAllowed(action_msg->object_uid, action_msg->avatar_uid, action_msg->action_id, action_msg->values, world_lock)) &&
								object_it->second->event_handlers && object_it->second->event_handlers->onUserGameAction_handlers.nonEmpty())
			{
				std::vector<std::pair<std::string, std::string>> values;
				for(const GameHUDFormValue& value : action_msg->values) values.push_back(std::make_pair(value.id, value.value));
				object_it->second->event_handlers->executeOnUserGameActionHandlers(
					action_msg->avatar_uid, action_msg->object_uid, action_msg->action_id, values, world_lock);
			}
						}
					}
					else if(dynamic_cast<UserGameInputStateThreadMessage*>(msg.ptr()))
					{
						const UserGameInputStateThreadMessage* input_msg = static_cast<UserGameInputStateThreadMessage*>(msg.ptr());
						if(input_msg->world.nonNull())
						{
							WorldStateLock world_lock(server.world_state->mutex);
							const auto avatar_it = input_msg->world->getAvatars(world_lock).find(input_msg->avatar_uid);
							const auto object_it = input_msg->world->getObjects(world_lock).find(input_msg->object_uid);
							if(avatar_it != input_msg->world->getAvatars(world_lock).end() &&
								object_it != input_msg->world->getObjects(world_lock).end() &&
								avatar_it->second->state != Avatar::State_Dead &&
								object_it->second->state != WorldObject::State_Dead &&
								avatar_it->second->pos.getDist2(object_it->second->pos) <= Maths::square(12.0) &&
								input_msg->world->isGameSessionParticipant(input_msg->object_uid, input_msg->avatar_uid, world_lock) &&
								input_msg->world->allowGameInputUpdate(input_msg->object_uid, input_msg->avatar_uid, std::chrono::steady_clock::now(), world_lock))
								input_msg->world->setGameSessionInputState(input_msg->object_uid, input_msg->avatar_uid, input_msg->input, world_lock);
						}
					}
					else if(dynamic_cast<UserGameRPCThreadMessage*>(msg.ptr()))
					{
						const UserGameRPCThreadMessage* rpc_msg = static_cast<UserGameRPCThreadMessage*>(msg.ptr());
						if(rpc_msg->world.nonNull())
						{
							WorldStateLock world_lock(server.world_state->mutex);
							const auto avatar_it = rpc_msg->world->getAvatars(world_lock).find(rpc_msg->avatar_uid);
							const auto object_it = rpc_msg->world->getObjects(world_lock).find(rpc_msg->object_uid);
							if(avatar_it != rpc_msg->world->getAvatars(world_lock).end() && object_it != rpc_msg->world->getObjects(world_lock).end() &&
								avatar_it->second->state != Avatar::State_Dead && object_it->second->state != WorldObject::State_Dead &&
								avatar_it->second->pos.getDist2(object_it->second->pos) <= Maths::square(12.0) &&
								rpc_msg->world->isGameSessionParticipant(rpc_msg->object_uid, rpc_msg->avatar_uid, world_lock) &&
								object_it->second->event_handlers && object_it->second->event_handlers->onUserGameMessage_handlers.nonEmpty() &&
								rpc_msg->world->allowGameRPCMessage(rpc_msg->object_uid, rpc_msg->avatar_uid, std::chrono::steady_clock::now(), world_lock))
								object_it->second->event_handlers->executeOnUserGameMessageHandlers(
									rpc_msg->avatar_uid, rpc_msg->object_uid, rpc_msg->name, rpc_msg->serialized_payload, world_lock);
						}
					}
					else if(dynamic_cast<UserUsedBotThreadMessage*>(msg.ptr()))
					{
						const UserUsedBotThreadMessage* used_msg = static_cast<UserUsedBotThreadMessage*>(msg.ptr());

						WorldStateLock world_lock(server.world_state->mutex);

						// Find user avatar
						AvatarRef user_avatar;
						{
							auto av_it = used_msg->world->getAvatars(world_lock).find(used_msg->user_avatar_uid);
							if(av_it != used_msg->world->getAvatars(world_lock).end())
								user_avatar = av_it->second;
						}

						if(user_avatar.nonNull())
						{
							for(auto& bot_it : used_msg->world->getChatBots(world_lock))
							{
								ChatBotRef bot = bot_it.second;
								if(bot->avatar_uid == used_msg->bot_avatar_uid)
								{
									ChatBot::EventHandlerResults res = bot->processUserUsedBot(user_avatar, &server, world_lock);
									if(res.new_llm_thread)
										server.llm_thread_manager.addThread(res.new_llm_thread);
									break;
								}
							}
						}
					}
					else if(dynamic_cast<UserTouchedObjectThreadMessage*>(msg.ptr()))
					{
						const UserTouchedObjectThreadMessage* touched_msg = static_cast<UserTouchedObjectThreadMessage*>(msg.ptr());

						// Look up object
						WorldStateLock world_lock(server.world_state->mutex);
						auto res = touched_msg->world->getObjects(world_lock).find(touched_msg->object_uid);
						const ServerWorldState::AvatarMapType& avatars = touched_msg->world->getAvatars(world_lock);
						auto avatar_it = avatars.find(touched_msg->avatar_uid);
						if(res != touched_msg->world->getObjects(world_lock).end() && avatar_it != avatars.end())
						{
							WorldObject* ob = res->second.ptr();
							Avatar* avatar = avatar_it->second.ptr();
							// Touch notifications originate from client physics contacts. Verify
							// the sender is alive and close to the object's world bounds before
							// running any server-side script.


							// Execute doOnUserTouchedObject event handler in any scripts that are listening for onUserTouchedObject for this object
							if(ob->state != WorldObject::State_Dead && avatar->state != Avatar::State_Dead && avatar->pos.isFinite() &&
								ob->getAABBWS().distanceToPoint(avatar->pos.toVec4fPoint()) <= 2.5f && ob->event_handlers)
								ob->event_handlers->executeOnUserTouchedObjectHandlers(touched_msg->avatar_uid, ob->uid, world_lock);
						}
					}
					else if(dynamic_cast<UserMovedNearToObjectThreadMessage*>(msg.ptr()))
					{
						const UserMovedNearToObjectThreadMessage* moved_msg = static_cast<UserMovedNearToObjectThreadMessage*>(msg.ptr());

						// Look up object
						WorldStateLock world_lock(server.world_state->mutex);
						auto res = moved_msg->world->getObjects(world_lock).find(moved_msg->object_uid);
						const auto avatar_it = moved_msg->world->getAvatars(world_lock).find(moved_msg->avatar_uid);
						if(res != moved_msg->world->getObjects(world_lock).end() && avatar_it != moved_msg->world->getAvatars(world_lock).end())
						{
							WorldObject* ob = res->second.ptr();
							Avatar* avatar = avatar_it->second.ptr();
							const bool is_near = ob->state != WorldObject::State_Dead && avatar->state != Avatar::State_Dead &&
								avatar->pos.isFinite() && ob->getAABBWS().distanceToPoint(avatar->pos.toVec4fPoint()) < 20.f;

							// Validate the client-reported transition against the server's latest avatar
							// position and suppress duplicate/replayed near notifications.
							if(is_near && moved_msg->world->transitionScriptProximity(moved_msg->avatar_uid, ob->uid, true, world_lock) && ob->event_handlers)
								ob->event_handlers->executeOnUserMovedNearToObjectHandlers(moved_msg->avatar_uid, ob->uid, world_lock);
						}
					}
					else if(dynamic_cast<UserMovedAwayFromObjectThreadMessage*>(msg.ptr()))
					{
						const UserMovedAwayFromObjectThreadMessage* moved_msg = static_cast<UserMovedAwayFromObjectThreadMessage*>(msg.ptr());

						// Look up object
						WorldStateLock world_lock(server.world_state->mutex);
						auto res = moved_msg->world->getObjects(world_lock).find(moved_msg->object_uid);
						const auto avatar_it = moved_msg->world->getAvatars(world_lock).find(moved_msg->avatar_uid);
						if(res != moved_msg->world->getObjects(world_lock).end() && avatar_it != moved_msg->world->getAvatars(world_lock).end())
						{
							WorldObject* ob = res->second.ptr();
							Avatar* avatar = avatar_it->second.ptr();
							const bool away = ob->state != WorldObject::State_Dead && avatar->state != Avatar::State_Dead &&
								avatar->pos.isFinite() && ob->getAABBWS().distanceToPoint(avatar->pos.toVec4fPoint()) >= 20.f;

							// Only accept a genuine exit after a previously accepted near transition.
							if(away && moved_msg->world->transitionScriptProximity(moved_msg->avatar_uid, ob->uid, false, world_lock) && ob->event_handlers)
								ob->event_handlers->executeOnUserMovedAwayFromObjectHandlers(moved_msg->avatar_uid, moved_msg->object_uid, world_lock);
						}
					}
					else if(dynamic_cast<UserEnteredParcelThreadMessage*>(msg.ptr()))
					{
						const UserEnteredParcelThreadMessage* parcel_msg = static_cast<UserEnteredParcelThreadMessage*>(msg.ptr());

						if(parcel_msg->object_uid.valid())
						{
							// Look up object
							WorldStateLock world_lock(server.world_state->mutex);
							auto res = parcel_msg->world->getObjects(world_lock).find(parcel_msg->object_uid);
							if(res != parcel_msg->world->getObjects(world_lock).end())
							{
								WorldObject* ob = res->second.ptr();

								// Execute event handler in any scripts that are listening on this object
								if(ob->event_handlers)
									ob->event_handlers->executeOnUserEnteredParcelHandlers(parcel_msg->avatar_uid, parcel_msg->object_uid, parcel_msg->parcel_id, world_lock);
							}
						}
						else
						{
							// If object_uid is invalid, then this event is not from a script, but just from a user entering a parcel.
							// See if there are any social events currently happening on this parcel, if there are, add user to attendee list.
							if(parcel_msg->client_user_id.valid())
							{
								const TimeStamp current_time = TimeStamp::currentTime();

								WorldStateLock world_lock(server.world_state->mutex);
								for(auto it = server.world_state->events.begin(); it != server.world_state->events.end(); ++it)
								{
									SubEvent* event = it->second.ptr();
									if((event->parcel_id == parcel_msg->parcel_id) && // If event is at this parcel
										(event->start_time <= current_time) && // and is currently happening
										(event->end_time >= current_time))
									{
										// Add the client to the event attendee list (if not already inserted)
										const bool inserted = event->attendee_ids.insert(parcel_msg->client_user_id).second;
										if(inserted)
											server.world_state->addEventAsDBDirty(event);
									}
								}
							}
						}
					}
					else if(dynamic_cast<UserExitedParcelThreadMessage*>(msg.ptr()))
					{
						const UserExitedParcelThreadMessage* parcel_msg = static_cast<UserExitedParcelThreadMessage*>(msg.ptr());

						// Look up object
						WorldStateLock world_lock(server.world_state->mutex);
						auto res = parcel_msg->world->getObjects(world_lock).find(parcel_msg->object_uid);
						if(res != parcel_msg->world->getObjects(world_lock).end())
						{
							WorldObject* ob = res->second.ptr();

							// Execute event handler in any scripts that are listening on this object
							if(ob->event_handlers)
								ob->event_handlers->executeOnUserExitedParcelHandlers(parcel_msg->avatar_uid, parcel_msg->object_uid, parcel_msg->parcel_id, world_lock);
						}
					}
					else if(dynamic_cast<NewResourceGenerated*>(msg.ptr()))
					{
						NewResourceGenerated* gen_msg = static_cast<NewResourceGenerated*>(msg.ptr());

						// Send NewResourceOnServer message to connected clients
						{
							MessageUtils::initPacket(scratch_packet, Protocol::NewResourceOnServer);
							scratch_packet.writeStringLengthFirst(gen_msg->URL);
							MessageUtils::updatePacketLengthField(scratch_packet);

							Lock lock3(server.worker_thread_manager.getMutex());
							for(auto i = server.worker_thread_manager.getThreads().begin(); i != server.worker_thread_manager.getThreads().end(); ++i)
							{
								assert(dynamic_cast<WorkerThread*>(i->getPointer()));
								static_cast<WorkerThread*>(i->getPointer())->enqueueDataToSend(scratch_packet);
							}
						}
					}
					else if(dynamic_cast<AIChatResponseDataMessage*>(msg.ptr()))
					{
						AIChatResponseDataMessage* chat_msg = static_cast<AIChatResponseDataMessage*>(msg.ptr());

						// These messages come from LLMThreads and are sent when some streaming data is received from a LLM cloud server.
						// Pass on to the relevant chatbot.
						{
							ChatBotRef chatbot = chat_msg->chatbot.upgradeToStrongRef();
							if(chatbot)
							{
								WorldStateLock world_lock(server.world_state->mutex);
								chatbot->handleLLMChatResponse(chat_msg->message, &server, world_lock);
							}
						}
					}
					else if(dynamic_cast<AIChatResponseDoneMessage*>(msg.ptr()))
					{
						AIChatResponseDoneMessage* done_msg = static_cast<AIChatResponseDoneMessage*>(msg.ptr());

						{
							ChatBotRef chatbot = done_msg->chatbot.upgradeToStrongRef();
							if(chatbot)
							{
								WorldStateLock world_lock(server.world_state->mutex);
								chatbot->handleLLMChatResponseDone(&server, world_lock);
							}
						}
					}
					else if(dynamic_cast<AIToolFunctionCallMessage*>(msg.ptr()))
					{
						AIToolFunctionCallMessage* tool_msg = static_cast<AIToolFunctionCallMessage*>(msg.ptr());
						ChatBotRef chatbot = tool_msg->chatbot.upgradeToStrongRef();
						if(chatbot)
						{
							WorldStateLock world_lock(server.world_state->mutex);
							chatbot->handleLLMToolFunctionCall(tool_msg->calls, &server, world_lock);
						}
					}
				}
			}

			{ // Begin scope for world_state->mutex lock

				WorldStateLock lock(server.world_state->mutex);

				for(auto world_it = server.world_state->world_states.begin(); world_it != server.world_state->world_states.end(); ++world_it)
				{
					ServerWorldState* world_state = world_it->second.ptr();

					std::vector<uint8>& world_packets = broadcast_packets[world_state];

					// Generate packets for avatar changes
					const ServerWorldState::AvatarMapType& avatars = world_state->getAvatars(lock);
					for(auto i = avatars.begin(); i != avatars.end();)
					{
						Avatar* avatar = i->second.ptr();
						if(avatar->other_dirty)
						{
							if(avatar->state == Avatar::State_Alive)
							{
								// Send AvatarFullUpdate packet
								MessageUtils::initPacket(scratch_packet, Protocol::AvatarFullUpdate);
								writeAvatarToNetworkStream(*avatar, scratch_packet);

								enqueueMessageToBroadcast(scratch_packet, world_packets);

								avatar->other_dirty = false;
								avatar->transform_dirty = false;
								i++;
							}
							else if(avatar->state == Avatar::State_JustCreated)
							{
								// Send AvatarCreated packet
								MessageUtils::initPacket(scratch_packet, Protocol::AvatarCreated);
								writeAvatarToNetworkStream(*avatar, scratch_packet);

								enqueueMessageToBroadcast(scratch_packet, world_packets);

								avatar->state = Avatar::State_Alive;
								avatar->other_dirty = false;
								avatar->transform_dirty = false;

								i++;
							}
							else if(avatar->state == Avatar::State_Dead)
							{
								// Send AvatarDestroyed packet
								MessageUtils::initPacket(scratch_packet, Protocol::AvatarDestroyed);
								writeToStream(avatar->uid, scratch_packet);

								enqueueMessageToBroadcast(scratch_packet, world_packets);

								// Remove avatar from avatar map
								auto old_avatar_iterator = i;
								i++;
								world_state->getAvatars(lock).erase(old_avatar_iterator);

								conPrint("Removed avatar from world_state->avatars");
							}
							else
							{
								assert(0);
							}
						}
						else if(avatar->transform_dirty)
						{
							if(avatar->state == Avatar::State_Alive)
							{
								// Send AvatarTransformUpdate packet
								MessageUtils::initPacket(scratch_packet, Protocol::AvatarTransformUpdate);
								writeToStream(avatar->uid, scratch_packet);
								writeToStream(avatar->pos, scratch_packet);
								writeToStream(avatar->rotation, scratch_packet);
								scratch_packet.writeUInt32(avatar->anim_state);

								enqueueMessageToBroadcast(scratch_packet, world_packets);

								avatar->transform_dirty = false;
							}
							i++;
						}
						else
						{
							i++;
						}
					}


					// Generate packets for object changes
					ServerWorldState::DirtyFromRemoteObjectSetType& dirty_from_remote_objects = world_state->getDirtyFromRemoteObjects(lock);
					std::vector<WorldObjectRef> game_objects_pending_cleanup;
					for(auto i = dirty_from_remote_objects.begin(); i != dirty_from_remote_objects.end(); ++i)
					{
						WorldObject* ob = i->ptr();
						if(ob->from_remote_other_dirty)
						{
							// conPrint("Object 'other' dirty, sending full update");

							if(ob->state == WorldObject::State_Alive)
							{
								// Send ObjectFullUpdate packet
								MessageUtils::initPacket(scratch_packet, Protocol::ObjectFullUpdate);
								ob->writeToNetworkStream(scratch_packet);

								enqueueMessageToBroadcast(scratch_packet, world_packets);

								ob->from_remote_other_dirty = false;
								ob->from_remote_transform_dirty = false; // transform is sent in full packet also.
								server.world_state->markAsChanged();
							}
							else if(ob->state == WorldObject::State_JustCreated)
							{
								// Send ObjectCreated packet
								MessageUtils::initPacket(scratch_packet, Protocol::ObjectCreated);
								ob->writeToNetworkStream(scratch_packet);

								enqueueMessageToBroadcast(scratch_packet, world_packets);

								ob->state = WorldObject::State_Alive;
								ob->from_remote_other_dirty = false;
								server.world_state->markAsChanged();
							}
							else if(ob->state == WorldObject::State_Dead)
							{
								// Send ObjectDestroyed packet
								MessageUtils::initPacket(scratch_packet, Protocol::ObjectDestroyed);
								writeToStream(ob->uid, scratch_packet);

								enqueueMessageToBroadcast(scratch_packet, world_packets);

								// Remove from dirty-set, so it's not updated in DB.
								world_state->getDBDirtyWorldObjects(lock).erase(ob);

								// Game-spawned objects are transient and have no database row.
								if(!ob->game_spawn_owner_uid.valid())
									server.world_state->db_records_to_delete.insert(ob->database_key);

								// Remove this game's active session and queue its transient objects
								// for deletion after this dirty-set traversal (insertion during the
								// traversal could invalidate the unordered-set iterator).
								world_state->clearGameSessionParticipants(ob->uid, lock);
								world_state->clearGameStateSnapshot(ob->uid, lock);
								server.world_state->clearGameStorageWriteWindowsForObject(ob->uid);
								const std::vector<UID> spawned_uids = world_state->takeGameSpawnedObjects(ob->uid, lock);
								for(const UID& spawned_uid : spawned_uids)
								{
									auto spawned_it = world_state->getObjects(lock).find(spawned_uid);
									if(spawned_it != world_state->getObjects(lock).end() && spawned_it->second.ptr() != ob &&
										spawned_it->second->state != WorldObject::State_Dead)
										game_objects_pending_cleanup.push_back(spawned_it->second);
								}

								// Remove ob from object map
								world_state->removeGameRaySpatialIndexObject(ob->uid, lock);
								world_state->getObjects(lock).erase(ob->uid);

								conPrint("Removed object from world_state->objects");
								server.world_state->markAsChanged();
							}
							else
							{
								conPrint("ERROR: invalid object state (ob->state=" + toString(ob->state) + ")");
								assert(0);
							}
						}
						else if(ob->from_remote_transform_dirty)
						{
							//conPrint("Object 'transform' dirty, sending transform update");

							if(ob->state == WorldObject::State_Alive)
							{
								// Send ObjectTransformUpdate packet
								MessageUtils::initPacket(scratch_packet, Protocol::ObjectTransformUpdate);
								writeToStream(ob->uid, scratch_packet);
								writeToStream(ob->pos, scratch_packet);
								writeToStream(ob->axis, scratch_packet);
								scratch_packet.writeFloat(ob->angle);
								writeToStream(ob->scale, scratch_packet);

								scratch_packet.writeUInt32(ob->last_transform_update_avatar_uid);

								enqueueMessageToBroadcast(scratch_packet, world_packets);

								ob->from_remote_transform_dirty = false;
								server.world_state->markAsChanged();
							}
						}
						else if(ob->from_remote_physics_transform_dirty)
						{
							//conPrint("Object 'physics transform' dirty, sending physics transform update");

							if(ob->state == WorldObject::State_Alive)
							{
								// Send ObjectPhysicsTransformUpdate packet
								MessageUtils::initPacket(scratch_packet, Protocol::ObjectPhysicsTransformUpdate);
								writeToStream(ob->uid, scratch_packet);
								writeToStream(ob->pos, scratch_packet);

								const Quatf rot = Quatf::fromAxisAndAngle(ob->axis, ob->angle);
								scratch_packet.writeData(&rot.v.x, sizeof(float) * 4);

								scratch_packet.writeData(ob->linear_vel.x, sizeof(float) * 3);
								scratch_packet.writeData(ob->angular_vel.x, sizeof(float) * 3);

								scratch_packet.writeUInt32(ob->last_transform_update_avatar_uid);
								scratch_packet.writeDouble(ob->last_transform_client_time);

								enqueueMessageToBroadcast(scratch_packet, world_packets);

								ob->from_remote_transform_dirty = false;
								server.world_state->markAsChanged();
							}
						}
						else if(ob->from_remote_lightmap_url_dirty)
						{
							// Send ObjectLightmapURLChanged packet
							MessageUtils::initPacket(scratch_packet, Protocol::ObjectLightmapURLChanged);
							writeToStream(ob->uid, scratch_packet);
							scratch_packet.writeStringLengthFirst(ob->lightmap_url);

							enqueueMessageToBroadcast(scratch_packet, world_packets);

							ob->from_remote_lightmap_url_dirty = false;
							server.world_state->markAsChanged();
						}
						else if(ob->from_remote_model_url_dirty)
						{
							// Send ObjectModelURLChanged packet
							MessageUtils::initPacket(scratch_packet, Protocol::ObjectModelURLChanged);
							writeToStream(ob->uid, scratch_packet);
							scratch_packet.writeStringLengthFirst(ob->model_url);

							enqueueMessageToBroadcast(scratch_packet, world_packets);

							ob->from_remote_model_url_dirty = false;
							server.world_state->markAsChanged();
						}
						else if(ob->from_remote_content_dirty)
						{
							// Send ObjectContentChanged packet
							MessageUtils::initPacket(scratch_packet, Protocol::ObjectContentChanged);
							writeToStream(ob->uid, scratch_packet);
							scratch_packet.writeStringLengthFirst(ob->content);

							enqueueMessageToBroadcast(scratch_packet, world_packets);

							ob->from_remote_content_dirty = false;
							server.world_state->markAsChanged();
						}
						else if(ob->from_remote_flags_dirty)
						{
							// Send ObjectFlagsChanged packet
							MessageUtils::initPacket(scratch_packet, Protocol::ObjectFlagsChanged);
							writeToStream(ob->uid, scratch_packet);
							scratch_packet.writeUInt32(ob->flags);

							enqueueMessageToBroadcast(scratch_packet, world_packets);

							ob->from_remote_flags_dirty = false;
							server.world_state->markAsChanged();
						}
					}

					dirty_from_remote_objects.clear();
					for(const WorldObjectRef& spawned_object : game_objects_pending_cleanup)
					{
						spawned_object->state = WorldObject::State_Dead;
						spawned_object->from_remote_other_dirty = true;
						world_state->getDirtyFromRemoteObjects(lock).insert(spawned_object);
						markLODChunkAsNeedsRebuildForChangedObject(world_state, spawned_object.ptr(), lock);
					}


					// Call think() on chatbots
					const ServerWorldState::ChatBotMapType& chatbots = world_state->getChatBots(lock);
					for(auto i = chatbots.begin(); i != chatbots.end(); ++i)
					{
						ChatBot* chatbot = i->second.ptr();

						try
						{
							ChatBot::ThinkResults results = chatbot->think(&server, lock); // TODO: only do on active chatbots.

							if(results.new_llm_thread)
								server.llm_thread_manager.addThread(results.new_llm_thread);

							if(results.llm_thread_being_killed)
								server.llm_thread_manager.removeThread(results.llm_thread_being_killed);
						}
						catch(glare::Exception& e)
						{
							conPrint("Exception executing chatbot->think: " + e.what());
						}
					}

				} // End for each server world


				if(server.world_state->server_admin_message_changed)
				{
					conPrint("Sending ServerAdminMessages to clients...");

					// Send out ServerAdminMessageID packets to clients
					MessageUtils::initPacket(scratch_packet, Protocol::ServerAdminMessageID);
					scratch_packet.writeStringLengthFirst(server.world_state->server_admin_message);
					MessageUtils::updatePacketLengthField(scratch_packet);

					Lock lock3(server.worker_thread_manager.getMutex());
					for(auto i = server.worker_thread_manager.getThreads().begin(); i != server.worker_thread_manager.getThreads().end(); ++i)
					{
						assert(dynamic_cast<WorkerThread*>(i->ptr()));
						static_cast<WorkerThread*>(i->ptr())->enqueueDataToSend(scratch_packet);
					}

					server.world_state->server_admin_message_changed = false;
				}

			} // End scope for world_state->mutex lock

			// Enqueue packets to worker threads to send
			// For each connected client, get packets for the world the client is connected to, and send to them.
			{
				Lock lock2(server.worker_thread_manager.getMutex());
				for(auto i = server.worker_thread_manager.getThreads().begin(); i != server.worker_thread_manager.getThreads().end(); ++i)
				{
					WorkerThread* worker = static_cast<WorkerThread*>(i->ptr());
					auto res = broadcast_packets.find(worker->cur_world_state.ptr());
					if(res != broadcast_packets.end())
					{
						const std::vector<uint8>& packets = res->second;
						if(!packets.empty())
							worker->enqueueDataToSend(packets);
					}
				}
			}

			// Clear broadcast_packets vectors of packets.
			for(auto it = broadcast_packets.begin(); it != broadcast_packets.end(); ++it)
				it->second.clear();
			
			if((loop_iter % 40) == 0) // Approx every 4 s.
			{
				// Send out TimeSyncMessage packets to clients
				MessageUtils::initPacket(scratch_packet, Protocol::TimeSyncMessage);
				scratch_packet.writeDouble(server.getCurrentGlobalTime());
				MessageUtils::updatePacketLengthField(scratch_packet);

				Lock lock3(server.worker_thread_manager.getMutex());
				for(auto i = server.worker_thread_manager.getThreads().begin(); i != server.worker_thread_manager.getThreads().end(); ++i)
				{
					assert(dynamic_cast<WorkerThread*>(i->ptr()));
					static_cast<WorkerThread*>(i->ptr())->enqueueDataToSend(scratch_packet);
				}
			}

#if USE_GLARE_PARCEL_AUCTION_CODE
			if(server_config.update_parcel_sales && ((loop_iter % 512) == 0)) // Approx every 50 s.
			{
				AuctionManagement::updateParcelSales(*server.world_state);

				// Want want to list new parcels (to bring the total number being listed up to our target number) every day at midnight UTC.
				/*int hour, day, year;
				Clock::getHourDayOfYearAndYear(Clock::getSecsSince1970(), hour, day, year);
				
				const bool different_day = 
					server.world_state->last_parcel_update_info.last_parcel_sale_update_year != year ||
					server.world_state->last_parcel_update_info.last_parcel_sale_update_day != day;
				
				const bool initial_listing = server.world_state->last_parcel_update_info.last_parcel_sale_update_year == 0;
				
				if(initial_listing || different_day)
				{
					updateParcelSales(*server.world_state);
					server.world_state->last_parcel_update_info.last_parcel_sale_update_hour = hour;
					server.world_state->last_parcel_update_info.last_parcel_sale_update_day = day;
					server.world_state->last_parcel_update_info.last_parcel_sale_update_year = year;
					
					server.world_state->last_parcel_update_info.db_dirty = true; // Save to DB
					server.world_state->markAsChanged();
				}*/
			}
#endif
			if((loop_iter % 1024) == 64) // Approx every 100 s.
				if(isFeatureFlagSet(server.world_state, ServerAllWorldsState::DO_WORLD_MAINTENANCE_FEATURE_FLAG))
					WorldMaintenance::removeOldVehicles(server.world_state);

			if(server.world_state->hasChanged() && (save_state_timer.elapsed() > 10.0))
			{
				try
				{
					// Save world state to disk
					WorldStateLock lock(server.world_state->mutex);

					server.world_state->serialiseToDisk(lock);

					server.world_state->clearChangedFlag();
					save_state_timer.reset();
				}
				catch(glare::Exception& e)
				{
					conPrint("Warning: saving world state to disk failed: " + e.what());
					save_state_timer.reset(); // Reset timer so we don't try again straight away.
				}
			}

			loop_iter++;

			//if(loop_iter > 100) // TEMP: test shutting down
			//	break;
		} // End of main server loop

		conPrint("Closing...");

		// Save world state to disk before terminating.
		conPrint("Saving world state to disk before program quits...");
		try
		{
			// Save world state to disk
			WorldStateLock lock(server.world_state->mutex);

			server.world_state->serialiseToDisk(lock);
		}
		catch(glare::Exception& e)
		{
			conPrint("Warning: saving world state to disk failed: " + e.what());
		}

		// Shut down threads in reverse order of creation
		conPrint("Stopping threads...");

		thread_manager.killThreadsBlocking();

		web_thread_manager.killThreadsBlocking();
	}
	catch(ArgumentParserExcep& e)
	{
		stdErrPrint("ArgumentParserExcep: " + e.what());
		return 1;
	}
	catch(glare::Exception& e)
	{
		stdErrPrint("glare::Exception: " + e.what());
		return 1;
	}

	Networking::shutdown();
	return 0;
}


Server::Server()
{
	world_state = new ServerAllWorldsState();
}


Server::~Server()
{
	conPrint("Stopping Server threads...");

	// Stop any threads that may refer to other data members first
	llm_thread_manager.killThreadsBlocking();
	dyn_tex_updater_thread_manager.killThreadsBlocking();
	udp_handler_thread_manager.killThreadsBlocking();
	mesh_lod_gen_thread_manager.killThreadsBlocking();
	worker_thread_manager.killThreadsBlocking();

	lua_http_manager = nullptr;

	message_queue.clear();
	timer_queue.clear();

	world_state = nullptr;
}


void Server::printFromLuaScript(LuaScript* script, const char* s, size_t len)
{
	const std::string message(s, len);

	// conPrint("LUA: " + message);

	// Store log message for user in the per-user script message log.
	
	LuaScriptEvaluator* script_evaluator = (LuaScriptEvaluator*)script->userdata;

	logLuaMessage(message, UserScriptLogMessage::MessageType_print, script_evaluator->world_object->uid, script_evaluator->world_object->creator_id);
}


void Server::errorOccurredFromLuaScript(LuaScript* script, const std::string& msg)
{
	conPrint("LUA ERROR: " + msg);

	// Store log message for user in the per-user script message log.

	LuaScriptEvaluator* script_evaluator = (LuaScriptEvaluator*)script->userdata;

	logLuaError("Error: " + msg, script_evaluator->world_object->uid, script_evaluator->world_object->creator_id);
}


void Server::logLuaMessage(const std::string& msg, UserScriptLogMessage::MessageType message_type, UID world_ob_uid, UserID script_creator_user_id)
{
	const size_t MAX_MESSAGE_BYTES = 4096;
	const std::string truncation_marker = " [truncated]";
	std::string bounded_message = msg;
	if(bounded_message.size() > MAX_MESSAGE_BYTES)
	{
		size_t kept_bytes = MAX_MESSAGE_BYTES - truncation_marker.size();
		while(kept_bytes > 0 && (static_cast<unsigned char>(bounded_message[kept_bytes]) & 0xC0) == 0x80)
			--kept_bytes;
		bounded_message.resize(kept_bytes);
		bounded_message += truncation_marker;
	}

	// Get (and create if needed) the per-user script log for script_creator_user_id
	Reference<UserScriptLog> user_script_log;
	{
		Lock lock(world_state->mutex);
		auto res = world_state->user_script_log.find(script_creator_user_id);
		if(res == world_state->user_script_log.end())
		{
			user_script_log = new UserScriptLog();
			world_state->user_script_log.insert(std::make_pair(script_creator_user_id, user_script_log));
		}
		else
			user_script_log = res->second;
	}

	{
		Lock lock(user_script_log->mutex);

		const size_t MAX_NUM_PER_USER_LUA_MESSAGES = 1000;
		const size_t MAX_BYTES_PER_USER_LUA_MESSAGES = 256 * 1024;
		user_script_log->messages.push_back(UserScriptLogMessage({TimeStamp::currentTime(), message_type, world_ob_uid, bounded_message}));
		user_script_log->total_message_bytes += bounded_message.size();

		while(user_script_log->messages.size() > MAX_NUM_PER_USER_LUA_MESSAGES ||
			user_script_log->total_message_bytes > MAX_BYTES_PER_USER_LUA_MESSAGES)
		{
			assert(!user_script_log->messages.empty());
			user_script_log->total_message_bytes -= user_script_log->messages.beginIt()->msg.size();
			user_script_log->messages.pop_front();
		}
	}
}


void Server::logLuaError(const std::string& msg, UID world_ob_uid, UserID script_creator_user_id)
{
	const std::string full_msg = msg + "\nScript will be disabled.";
	logLuaMessage(full_msg, UserScriptLogMessage::MessageType_error, world_ob_uid, script_creator_user_id);
}


double Server::getCurrentGlobalTime() const
{
	return Clock::getTimeSinceInit();
}


void Server::enqueueMsg(ThreadMessageRef msg)
{
	message_queue.enqueue(msg);
}


void Server::enqueueLuaHTTPRequest(Reference<LuaHTTPRequest> request)
{
	if(lua_http_manager)
		lua_http_manager->enqueueHTTPRequest(request);
}


void Server::clientUDPPortOpen(WorkerThread* worker_thread, const IPAddress& ip_addr, UID client_avatar_id)
{
	conPrint("Server::clientUDPPortOpen(): worker_thread: 0x" + toHexString((uint64)worker_thread) + ", ip_addr: " + ip_addr.toString());// + ", port: " + toString(client_UDP_port));

	{
		Lock lock(connected_clients_mutex);
		if(connected_clients.count(worker_thread) == 0)
		{
			connected_clients.insert(std::make_pair(worker_thread, 
				ServerConnectedClientInfo({ip_addr, client_avatar_id, /*client_UDP_port=*/-1})));
			connected_clients_changed = 1;
		}
	}
}


void Server::clientUDPPortBecameKnown(UID client_avatar_uid, const IPAddress& ip_addr, int client_UDP_port)
{
	bool change_made = false;
	{
		Lock lock(connected_clients_mutex);
		for(auto it = connected_clients.begin(); it != connected_clients.end(); ++it)
		{
			ServerConnectedClientInfo& info = it->second;
			if(info.client_avatar_id == client_avatar_uid)
			{
				if(it->second.client_UDP_port != client_UDP_port)
				{
					it->second.client_UDP_port = client_UDP_port;
					change_made = true;
					
				}
			}
		}
	}

	if(change_made)
	{
		connected_clients_changed = 1;

		conPrint("Server::clientUDPPortBecameKnown(): client with client_avatar_uid " + client_avatar_uid.toString() + ", ip_addr: " + ip_addr.toString() + ", has port: " + toString(client_UDP_port));
	}
}


void Server::enqueueAdminWorldEntryForAllWorlds(const SocketBufferOutStream& packet_buffer)
{
	// Entry events are infrequent. Each recipient checks its authenticated role
	// under its routing lock; normal broadcasts never contain this information.
	Lock lock(worker_thread_manager.getMutex());
	for(const auto& thread : worker_thread_manager.getThreads())
		static_cast<WorkerThread*>(thread.ptr())->enqueueAdminWorldEntry(packet_buffer);
}


// Enqueues packet to all WorkerThreads connected to the same world.
void Server::enqueuePacketToBroadcastForWorld(const SocketBufferOutStream& packet_buffer, ServerWorldState* world)
{
	Lock lock(worker_thread_manager.getMutex());
	for(auto i = worker_thread_manager.getThreads().begin(); i != worker_thread_manager.getThreads().end(); ++i)
	{
		assert(dynamic_cast<WorkerThread*>(i->ptr()));
		WorkerThread* worker_thread = static_cast<WorkerThread*>(i->ptr());

		if(worker_thread->cur_world_state.ptr() == world) // If connected to the given world:
		{
			worker_thread->enqueueDataToSend(packet_buffer);
		}
	}
}


void Server::enqueueScriptMessageForAvatar(const UID& avatar_uid, const std::string& msg, ServerWorldState* world)
{
	if(!world || !avatar_uid.valid() || msg.empty() || msg.size() > 1024)
		return;

	Reference<ScriptMessageThreadMessage> message = new ScriptMessageThreadMessage();
	message->avatar_uid = avatar_uid;
	message->world = world;
	message->msg = msg;
	enqueueMsg(message);
}


void Server::sendScriptMessageToAvatar(const UID& avatar_uid, const std::string& msg, ServerWorldState* world)
{
	if(!world || !avatar_uid.valid() || msg.empty() || msg.size() > 1024)
		return;

	SocketBufferOutStream packet(SocketBufferOutStream::DontUseNetworkByteOrder);
	// InfoMessageID is supported by older clients too; game notifications must
	// never make a legacy client reject an otherwise valid world packet.
	MessageUtils::initPacket(packet, Protocol::InfoMessageID);
	packet.writeStringLengthFirst(msg);
	MessageUtils::updatePacketLengthField(packet);

	Lock lock(worker_thread_manager.getMutex());
	for(auto i = worker_thread_manager.getThreads().begin(); i != worker_thread_manager.getThreads().end(); ++i)
	{
		assert(dynamic_cast<WorkerThread*>(i->ptr()));
		WorkerThread* worker_thread = static_cast<WorkerThread*>(i->ptr());
		if(worker_thread->cur_world_state.ptr() == world && worker_thread->getRoutingAvatarUID() == avatar_uid)
			worker_thread->enqueueDataToSend(packet);
	}
}


void Server::enqueueGameHUDForAvatar(const UID& avatar_uid, const UID& game_object_uid, bool visible, const std::string& text, ServerWorldState* world, bool has_layout, float center_x, float top_y)
{
	if(!world || !avatar_uid.valid() || !game_object_uid.valid() || text.size() > 2048 || (visible && text.empty()))
		return;
	if(has_layout && (!std::isfinite(center_x) || center_x < -1.f || center_x > 1.f ||
		!std::isfinite(top_y) || top_y < -1.f || top_y > 1.f))
		return;

	Reference<ScriptGameHUDThreadMessage> message = new ScriptGameHUDThreadMessage();
	message->avatar_uid = avatar_uid;
	message->game_object_uid = game_object_uid;
	message->world = world;
	message->visible = visible;
	message->text = text;
	message->has_layout = visible && has_layout;
	message->center_x = center_x;
	message->top_y = top_y;
	enqueueMsg(message);
}


void Server::enqueueGameHUDMenuForAvatar(const UID& avatar_uid, const UID& game_object_uid, const GameHUDMenu& menu, ServerWorldState* world)
{
	if(!world || !avatar_uid.valid() || !game_object_uid.valid() || menu.panel_id.empty() || menu.panel_id.size() > 24 || menu.title.size() > 128 || menu.body.size() > 2048 || menu.actions.size() > 8 || menu.text_widgets.size() > 8 ||
		!std::isfinite(menu.center_x) || menu.center_x < -1.f || menu.center_x > 1.f ||
		!std::isfinite(menu.top_y) || menu.top_y < -1.f || menu.top_y > 1.f ||
		!std::isfinite(menu.width) || menu.width < 0.2f || menu.width > 1.f ||
		(menu.actions.empty() && menu.progress_bars.empty() && menu.text_widgets.empty() && (!menu.title.empty() || !menu.body.empty())) ||
		((!menu.actions.empty() || !menu.progress_bars.empty() || !menu.text_widgets.empty()) && menu.title.empty()))
		return;
	for(const GameHUDMenuAction& action : menu.actions)
		if(action.id.empty() || action.id.size() > 32 || action.label.empty() || action.label.size() > 64)
			return;
	if(menu.progress_bars.size() > 4) return;
	for(const GameHUDProgressBar& bar : menu.progress_bars)
		if(bar.id.empty() || bar.id.size() > 32 || bar.label.empty() || bar.label.size() > 64 ||
			!std::isfinite(bar.value) || !std::isfinite(bar.maximum) || bar.value < 0.f || bar.maximum <= 0.f || bar.maximum > 1.0e9f || bar.value > bar.maximum)
			return;
	for(const GameHUDTextWidget& widget : menu.text_widgets)
		if(widget.id.empty() || widget.id.size() > 32 || widget.text.empty() || widget.text.size() > 256 ||
			!std::isfinite(widget.center_x) || widget.center_x < -1.f || widget.center_x > 1.f ||
			!std::isfinite(widget.top_y) || widget.top_y < -1.f || widget.top_y > 1.f ||
			!std::isfinite(widget.width) || widget.width < 0.05f || widget.width > 0.8f ||
			widget.center_x - widget.width * 0.5f < -1.f || widget.center_x + widget.width * 0.5f > 1.f ||
			!std::isfinite(widget.text_r) || widget.text_r < 0.f || widget.text_r > 1.f ||
			!std::isfinite(widget.text_g) || widget.text_g < 0.f || widget.text_g > 1.f ||
			!std::isfinite(widget.text_b) || widget.text_b < 0.f || widget.text_b > 1.f ||
			!std::isfinite(widget.background_r) || widget.background_r < 0.f || widget.background_r > 1.f ||
			!std::isfinite(widget.background_g) || widget.background_g < 0.f || widget.background_g > 1.f ||
			!std::isfinite(widget.background_b) || widget.background_b < 0.f || widget.background_b > 1.f ||
			!std::isfinite(widget.background_alpha) || widget.background_alpha < 0.f || widget.background_alpha > 1.f)
			return;
	for(const unsigned char c : menu.panel_id)
		if(!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-')) return;

	Reference<ScriptGameHUDMenuThreadMessage> message = new ScriptGameHUDMenuThreadMessage();
	message->avatar_uid = avatar_uid;
	message->game_object_uid = game_object_uid;
	message->world = world;
	message->menu = menu;
	enqueueMsg(message);
}


void Server::enqueueGameStateUpdate(const UID& game_object_uid, uint32 version, uint32 previous_version,
	const std::vector<uint8>& previous_serialized_value, const std::vector<uint8>& serialized_value, ServerWorldState* world)
{
	if(!world || !game_object_uid.valid() || version == 0 || serialized_value.empty() || serialized_value.size() > 16 * 1024)
		return;
	Reference<ScriptGameStateThreadMessage> message = new ScriptGameStateThreadMessage();
	message->game_object_uid = game_object_uid;
	message->world = world;
	message->version = version;
	message->previous_version = previous_version;
	message->previous_serialized_value = previous_serialized_value;
	message->serialized_value = serialized_value;
	enqueueMsg(message);
}



void Server::enqueueGameEvent(const UID& game_object_uid, uint32 sequence, double server_time, const std::string& name,
	const std::vector<UID>& recipients, ServerWorldState* world)
{
	if(!world || !game_object_uid.valid() || sequence == 0 || !std::isfinite(server_time) || server_time < 0 || name.empty() || name.size() > 32)
		return;
	for(unsigned char c : name)
		if(!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_'))
			return;
	Reference<ScriptGameEventThreadMessage> message = new ScriptGameEventThreadMessage();
	message->game_object_uid = game_object_uid;
	message->world = world;
	message->sequence = sequence;
	message->server_time = server_time;
	message->name = name;
	message->recipients = recipients;
	enqueueMsg(message);
}

void Server::enqueueScriptInteractionPromptUpdate(const UID& object_uid, const ScriptInteractionPrompt& prompt, ServerWorldState* world)
{
	if(!world || !object_uid.valid() || prompt.input_mode > 2 || prompt.text.size() > 128) return;
	Reference<ScriptInteractionPromptThreadMessage> message = new ScriptInteractionPromptThreadMessage();
	message->world = world;
	message->object_uid = object_uid;
	message->prompt = prompt;
	enqueueMsg(message);
}


void Server::enqueueAvatarMaterialiseEffect(const UID& avatar_uid, ServerWorldState* world)
{
	if(!world || !avatar_uid.valid()) return;
	Reference<ScriptAvatarMaterialiseEffectThreadMessage> message = new ScriptAvatarMaterialiseEffectThreadMessage();
	message->world = world;
	message->avatar_uid = avatar_uid;
	enqueueMsg(message);
}

void Server::broadcastScriptInteractionPrompt(const UID& object_uid, const ScriptInteractionPrompt& prompt, ServerWorldState* world)
{
	if(!world || !object_uid.valid() || prompt.input_mode > 2 || prompt.text.size() > 128) return;
	SocketBufferOutStream packet(SocketBufferOutStream::DontUseNetworkByteOrder);
	MessageUtils::initPacket(packet, Protocol::ScriptInteractionPromptUpdateID);
	::writeToStream(object_uid, packet);
	packet.writeUInt32(prompt.visible ? 1u : 0u);
	packet.writeUInt32(prompt.input_mode);
	packet.writeStringLengthFirst(prompt.text);
	packet.writeUInt32(prompt.key_code);
	MessageUtils::updatePacketLengthField(packet);
	Lock lock(worker_thread_manager.getMutex());
	for(auto i = worker_thread_manager.getThreads().begin(); i != worker_thread_manager.getThreads().end(); ++i)
	{
		WorkerThread* worker = static_cast<WorkerThread*>(i->ptr());
		if(worker->cur_world_state.ptr() == world && worker->supportsScriptedInteractionPrompts() &&
			(prompt.key_code == 0 || prompt.key_code == Protocol::ScriptKey_CharacterBase + 'e' || worker->supportsScriptedInteractionKeys()))
			worker->enqueueDataToSend(packet);
	}
}


void Server::broadcastAvatarMaterialiseEffect(const UID& avatar_uid, ServerWorldState* world)
{
	if(!world || !avatar_uid.valid()) return;
	SocketBufferOutStream packet(SocketBufferOutStream::DontUseNetworkByteOrder);
	MessageUtils::initPacket(packet, Protocol::AvatarMaterialiseEffectID);
	::writeToStream(avatar_uid, packet);
	MessageUtils::updatePacketLengthField(packet);
	Lock lock(worker_thread_manager.getMutex());
	for(auto i = worker_thread_manager.getThreads().begin(); i != worker_thread_manager.getThreads().end(); ++i)
	{
		WorkerThread* worker = static_cast<WorkerThread*>(i->ptr());
		if(worker->cur_world_state.ptr() == world && worker->supportsAvatarMaterialiseEffect())
			worker->enqueueDataToSend(packet);
	}
}


void Server::enqueueBotDialogOptionsForAvatar(const UID& avatar_uid, const UID& bot_avatar_uid, const std::string& bot_name, uint32 node_id, const std::vector<BotDialogOption>& options, ServerWorldState* world)
{
	if(!world || !avatar_uid.valid() || !bot_avatar_uid.valid() || bot_name.size() > 200 || options.size() > 8)
		return;
	for(const BotDialogOption& option : options)
		if(option.label.empty() || option.label.size() > 200)
			return;

	Reference<BotDialogOptionsThreadMessage> message = new BotDialogOptionsThreadMessage();
	message->avatar_uid = avatar_uid;
	message->bot_avatar_uid = bot_avatar_uid;
	message->world = world;
	message->bot_name = bot_name;
	message->node_id = node_id;
	message->options = options;
	enqueueMsg(message);
}


void Server::sendBotDialogOptionsToAvatar(const UID& avatar_uid, const UID& bot_avatar_uid, const std::string& bot_name, uint32 node_id, const std::vector<BotDialogOption>& options, ServerWorldState* world)
{
	if(!world || !avatar_uid.valid() || !bot_avatar_uid.valid() || options.size() > 8)
		return;
	SocketBufferOutStream packet(SocketBufferOutStream::DontUseNetworkByteOrder);
	MessageUtils::initPacket(packet, Protocol::ChatBotDialogChoices);
	::writeToStream(bot_avatar_uid, packet);
	packet.writeStringLengthFirst(bot_name);
	packet.writeUInt32(options.empty() ? BotDialogChoice::END_DIALOG : node_id);
	packet.writeUInt32((uint32)options.size());
	for(const BotDialogOption& option : options)
	{
		packet.writeUInt32(option.choice_index);
		packet.writeStringLengthFirst(option.label);
	}
	MessageUtils::updatePacketLengthField(packet);

	Lock lock(worker_thread_manager.getMutex());
	for(auto i = worker_thread_manager.getThreads().begin(); i != worker_thread_manager.getThreads().end(); ++i)
	{
		WorkerThread* worker_thread = static_cast<WorkerThread*>(i->ptr());
		if(worker_thread->cur_world_state.ptr() == world && worker_thread->getRoutingAvatarUID() == avatar_uid)
			worker_thread->enqueueDataToSend(packet);
	}
}


void Server::sendGameHUDToAvatar(const UID& avatar_uid, const UID& game_object_uid, bool visible, const std::string& text, ServerWorldState* world, bool has_layout, float center_x, float top_y)
{
	if(!world || !avatar_uid.valid() || !game_object_uid.valid() || text.size() > 2048 || (visible && text.empty()))
		return;
	if(has_layout && (!std::isfinite(center_x) || center_x < -1.f || center_x > 1.f ||
		!std::isfinite(top_y) || top_y < -1.f || top_y > 1.f))
		return;

	Lock lock(worker_thread_manager.getMutex());
	for(auto i = worker_thread_manager.getThreads().begin(); i != worker_thread_manager.getThreads().end(); ++i)
	{
		assert(dynamic_cast<WorkerThread*>(i->ptr()));
		WorkerThread* worker_thread = static_cast<WorkerThread*>(i->ptr());
		if(worker_thread->cur_world_state.ptr() == world &&
			worker_thread->getRoutingAvatarUID() == avatar_uid &&
			worker_thread->supportsScriptedGameHUD() &&
			worker_thread->getActiveScriptKeyboardObjectUID() == game_object_uid)
		{
			SocketBufferOutStream packet(SocketBufferOutStream::DontUseNetworkByteOrder);
			MessageUtils::initPacket(packet, Protocol::GameHUDUpdateID);
			writeToStream(game_object_uid, packet);
			packet.writeUInt32(visible ? 1 : 0);
			packet.writeStringLengthFirst(text);
			if(worker_thread->supportsScriptedGameHUDTextLayout())
			{
				packet.writeUInt32(visible && has_layout ? 1u : 0u);
				if(visible && has_layout)
				{
					packet.writeFloat(center_x);
					packet.writeFloat(top_y);
				}
			}
			MessageUtils::updatePacketLengthField(packet);
			worker_thread->enqueueDataToSend(packet);
		}
	}
}


void Server::sendGameHUDMenuToAvatar(const UID& avatar_uid, const UID& game_object_uid, const GameHUDMenu& menu, ServerWorldState* world)
{
	if(!world || !avatar_uid.valid() || !game_object_uid.valid() || menu.panel_id.empty() || menu.panel_id.size() > 24 || menu.title.size() > 128 || menu.actions.size() > 8 || menu.progress_bars.size() > 4 ||
		!std::isfinite(menu.center_x) || menu.center_x < -1.f || menu.center_x > 1.f ||
		!std::isfinite(menu.top_y) || menu.top_y < -1.f || menu.top_y > 1.f ||
		!std::isfinite(menu.width) || menu.width < 0.2f || menu.width > 1.f ||
		(menu.actions.empty() && menu.progress_bars.empty() && menu.text_widgets.empty() && !menu.title.empty()) ||
		((!menu.actions.empty() || !menu.progress_bars.empty() || !menu.text_widgets.empty()) && menu.title.empty()) || menu.text_widgets.size() > 8)
		return;

	for(const GameHUDMenuAction& action : menu.actions)
	{
		if(action.id.empty() || action.id.size() > 32 || action.label.empty() || action.label.size() > 64)
			return;
		if(action.has_layout && (!std::isfinite(action.center_x) || action.center_x < -1.f || action.center_x > 1.f ||
			!std::isfinite(action.top_y) || action.top_y < -1.f || action.top_y > 1.f ||
			!std::isfinite(action.width) || action.width < 0.05f || action.width > 1.f ||
			!std::isfinite(action.height) || action.height < 0.025f || action.height > 0.5f ||
			action.center_x - action.width * 0.5f < -1.f || action.center_x + action.width * 0.5f > 1.f ||
			action.top_y - action.height < -1.f))
			return;
	}
	std::set<std::string> progress_ids;
	for(const GameHUDProgressBar& bar : menu.progress_bars)
	{
		if(bar.id.empty() || bar.id.size() > 32 || bar.label.empty() || bar.label.size() > 64 ||
			!std::isfinite(bar.value) || !std::isfinite(bar.maximum) || bar.value < 0.f || bar.maximum <= 0.f || bar.maximum > 1.0e9f || bar.value > bar.maximum ||
			!progress_ids.insert(bar.id).second)
			return;
	}
	std::set<std::string> text_widget_ids;
	for(const GameHUDTextWidget& widget : menu.text_widgets)
	{
		if(widget.id.empty() || widget.id.size() > 32 || widget.text.empty() || widget.text.size() > 256 ||
			!std::isfinite(widget.center_x) || widget.center_x < -1.f || widget.center_x > 1.f ||
			!std::isfinite(widget.top_y) || widget.top_y < -1.f || widget.top_y > 1.f ||
			!std::isfinite(widget.width) || widget.width < 0.05f || widget.width > 0.8f ||
			widget.center_x - widget.width * 0.5f < -1.f || widget.center_x + widget.width * 0.5f > 1.f ||
			!std::isfinite(widget.text_r) || widget.text_r < 0.f || widget.text_r > 1.f ||
			!std::isfinite(widget.text_g) || widget.text_g < 0.f || widget.text_g > 1.f ||
			!std::isfinite(widget.text_b) || widget.text_b < 0.f || widget.text_b > 1.f ||
			!std::isfinite(widget.background_r) || widget.background_r < 0.f || widget.background_r > 1.f ||
			!std::isfinite(widget.background_g) || widget.background_g < 0.f || widget.background_g > 1.f ||
			!std::isfinite(widget.background_b) || widget.background_b < 0.f || widget.background_b > 1.f ||
			!std::isfinite(widget.background_alpha) || widget.background_alpha < 0.f || widget.background_alpha > 1.f ||
			!text_widget_ids.insert(widget.id).second)
			return;
	}
	for(const unsigned char c : menu.panel_id)
		if(!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-')) return;

	Lock lock(worker_thread_manager.getMutex());
	for(auto i = worker_thread_manager.getThreads().begin(); i != worker_thread_manager.getThreads().end(); ++i)
	{
		assert(dynamic_cast<WorkerThread*>(i->ptr()));
		WorkerThread* worker_thread = static_cast<WorkerThread*>(i->ptr());
		if(worker_thread->cur_world_state.ptr() == world &&
			worker_thread->getRoutingAvatarUID() == avatar_uid &&
			worker_thread->supportsScriptedGameHUD() &&
			(worker_thread->supportsScriptedGameHUDPanels() || menu.panel_id == "main") &&
			worker_thread->getActiveScriptKeyboardObjectUID() == game_object_uid)
		{
			const bool has_supported_content = !menu.actions.empty() ||
				(worker_thread->supportsScriptedGameHUDProgress() && !menu.progress_bars.empty()) ||
				(worker_thread->supportsScriptedGameHUDTextWidgets() && !menu.text_widgets.empty());
			const bool show_menu = has_supported_content && (menu.fields.empty() || worker_thread->supportsScriptedGameHUDForms());
			SocketBufferOutStream packet(SocketBufferOutStream::DontUseNetworkByteOrder);
			MessageUtils::initPacket(packet, Protocol::GameHUDMenuUpdateID);
			writeToStream(game_object_uid, packet);
			if(worker_thread->supportsScriptedGameHUDPanels()) packet.writeStringLengthFirst(menu.panel_id);
			packet.writeUInt32(show_menu ? 1 : 0);
			packet.writeStringLengthFirst(show_menu ? menu.title : std::string());
			packet.writeStringLengthFirst(show_menu ? menu.body : std::string());
			if(worker_thread->supportsScriptedGameHUDLayout())
			{
				packet.writeFloat(menu.center_x);
				packet.writeFloat(menu.top_y);
				packet.writeFloat(menu.width);
			}
			packet.writeUInt32(show_menu ? (uint32)menu.actions.size() : 0);
			if(show_menu) for(const GameHUDMenuAction& action : menu.actions)
			{
				packet.writeStringLengthFirst(action.id);
				packet.writeStringLengthFirst(action.label);
				if(worker_thread->supportsScriptedGameHUDActionLayout())
				{
					packet.writeUInt32(action.has_layout ? 1u : 0u);
					if(action.has_layout)
					{
						packet.writeFloat(action.center_x);
						packet.writeFloat(action.top_y);
						packet.writeFloat(action.width);
						packet.writeFloat(action.height);
					}
				}
			}
			if(worker_thread->supportsScriptedGameHUDForms())
			{
				packet.writeUInt32(show_menu ? (uint32)menu.fields.size() : 0);
				if(show_menu) for(const GameHUDFormField& field : menu.fields)
				{
					packet.writeStringLengthFirst(field.id);
					packet.writeStringLengthFirst(field.label);
					packet.writeUInt32(field.required ? 1 : 0);
					if(worker_thread->supportsScriptedGameHUDChoices())
					{
						const bool numeric_supported = worker_thread->supportsScriptedGameHUDNumeric();
						const bool checkbox_supported = worker_thread->supportsScriptedGameHUDCheckbox();
						const uint32 type = field.type == GameHUDFormField::Select ? 1u :
							(field.type == GameHUDFormField::Number && numeric_supported ? 2u :
							(field.type == GameHUDFormField::Checkbox && checkbox_supported ? 3u : 0u));
						packet.writeUInt32(type);
						packet.writeUInt32(type == 1 ? (uint32)field.options.size() : 0u);
						if(type == 1)
							for(const std::string& option : field.options) packet.writeStringLengthFirst(option);
						if(type == 3) packet.writeUInt32(field.checked ? 1u : 0u);
					}
					if(worker_thread->supportsScriptedGameHUDNumeric())
					{
						packet.writeDouble(field.type == GameHUDFormField::Number ? field.minimum : 0.0);
						packet.writeDouble(field.type == GameHUDFormField::Number ? field.maximum : 0.0);
					}
				}
			}
			if(worker_thread->supportsScriptedGameHUDProgress())
			{
				packet.writeUInt32(show_menu ? (uint32)menu.progress_bars.size() : 0u);
				if(show_menu) for(const GameHUDProgressBar& bar : menu.progress_bars)
				{
					packet.writeStringLengthFirst(bar.id);
					packet.writeStringLengthFirst(bar.label);
					packet.writeFloat(bar.value);
					packet.writeFloat(bar.maximum);
				}
			}
			if(worker_thread->supportsScriptedGameHUDTextWidgets())
			{
				packet.writeUInt32(show_menu ? (uint32)menu.text_widgets.size() : 0u);
				if(show_menu) for(const GameHUDTextWidget& widget : menu.text_widgets)
				{
					packet.writeStringLengthFirst(widget.id);
					packet.writeStringLengthFirst(widget.text);
					packet.writeFloat(widget.center_x);
					packet.writeFloat(widget.top_y);
					packet.writeFloat(widget.width);
					if(worker_thread->supportsScriptedGameHUDTextStyle())
					{
						packet.writeFloat(widget.text_r);
						packet.writeFloat(widget.text_g);
						packet.writeFloat(widget.text_b);
						packet.writeFloat(widget.background_r);
						packet.writeFloat(widget.background_g);
						packet.writeFloat(widget.background_b);
						packet.writeFloat(widget.background_alpha);
					}
				}
			}
			MessageUtils::updatePacketLengthField(packet);
			worker_thread->enqueueDataToSend(packet);
		}
	}
}


void Server::sendGameStateToAvatar(const UID& avatar_uid, const UID& game_object_uid, uint32 version,
	const std::vector<uint8>& serialized_value, ServerWorldState* world,
	uint32 previous_version, const std::vector<uint8>& previous_serialized_value)
{
	if(!world || !avatar_uid.valid() || !game_object_uid.valid() || version == 0 || serialized_value.empty() || serialized_value.size() > 16 * 1024)
		return;

	Lock lock(worker_thread_manager.getMutex());
	for(auto i = worker_thread_manager.getThreads().begin(); i != worker_thread_manager.getThreads().end(); ++i)
	{
		WorkerThread* worker_thread = static_cast<WorkerThread*>(i->ptr());
		if(worker_thread->cur_world_state.ptr() == world &&
			worker_thread->getRoutingAvatarUID() == avatar_uid &&
			worker_thread->supportsScriptedGameHUD() &&
			worker_thread->getActiveScriptKeyboardObjectUID() == game_object_uid)
		{
			GameStateDelta::Delta delta;
			// Periodic full snapshots let a focused client recover after a local
			// focus transition caused it to skip a delta packet.
			const bool checkpoint_snapshot = version % 16u == 0u;
			const bool use_delta = worker_thread->supportsScriptedGameStateDelta() &&
				!checkpoint_snapshot &&
				previous_version + 1 == version &&
				GameStateDelta::create(previous_serialized_value, serialized_value, previous_version, delta);
			SocketBufferOutStream packet(SocketBufferOutStream::DontUseNetworkByteOrder);
			MessageUtils::initPacket(packet, Protocol::GameStateUpdateID);
			writeToStream(game_object_uid, packet);
			packet.writeUInt32(version);
			if(worker_thread->supportsScriptedGameStateDelta())
			{
				packet.writeUInt32(use_delta ? 1 : 0);
				if(use_delta)
				{
					packet.writeUInt32(delta.base_version);
					packet.writeUInt32(delta.result_size);
					packet.writeUInt32(delta.prefix_size);
					packet.writeUInt32(delta.suffix_size);
					packet.writeStringLengthFirst(std::string(delta.replacement.begin(), delta.replacement.end()));
				}
				else
					packet.writeStringLengthFirst(std::string(serialized_value.begin(), serialized_value.end()));
			}
			else
				packet.writeStringLengthFirst(std::string(serialized_value.begin(), serialized_value.end()));
			MessageUtils::updatePacketLengthField(packet);
			worker_thread->enqueueDataToSend(packet);
		}
	}
}


void Server::sendGameEventToAvatar(const UID& avatar_uid, const UID& game_object_uid, uint32 sequence, double server_time,
	const std::string& name, ServerWorldState* world)
{
	if(!world || !avatar_uid.valid() || !game_object_uid.valid() || sequence == 0 || !std::isfinite(server_time) || server_time < 0 || name.empty() || name.size() > 32)
		return;

	Lock lock(worker_thread_manager.getMutex());
	for(auto i = worker_thread_manager.getThreads().begin(); i != worker_thread_manager.getThreads().end(); ++i)
	{
		WorkerThread* worker_thread = static_cast<WorkerThread*>(i->ptr());
		if(worker_thread->cur_world_state.ptr() == world &&
			worker_thread->getRoutingAvatarUID() == avatar_uid &&
			worker_thread->supportsScriptedGameEvents() &&
			worker_thread->getActiveScriptKeyboardObjectUID() == game_object_uid)
		{
			SocketBufferOutStream packet(SocketBufferOutStream::DontUseNetworkByteOrder);
			MessageUtils::initPacket(packet, Protocol::GameEventID);
			writeToStream(game_object_uid, packet);
			packet.writeUInt32(sequence);
			packet.writeStringLengthFirst(name);
			if(worker_thread->supportsScriptedGameEventTime())
				packet.writeDouble(server_time);
			MessageUtils::updatePacketLengthField(packet);
			worker_thread->enqueueDataToSend(packet);
		}
	}
}


bool Server::enqueuePrivateChatPacketForWorld(const SocketBufferOutStream& recipient_packet, const SocketBufferOutStream& sender_packet, ServerWorldState* world, WorkerThread* sender, const std::string& recipient_name, const UID& recipient_avatar_uid)
{
	bool sent = false;
	std::map<UID, std::string> avatar_names;
	std::map<UID, std::string> avatar_use_names;

	if(world && this->world_state.nonNull())
	{
		WorldStateLock world_lock(this->world_state->mutex);
		const auto& avatars = world->getAvatars(world_lock);
		for(auto i = avatars.begin(); i != avatars.end(); ++i)
		{
			if(i->second.nonNull())
			{
				avatar_names[i->first] = i->second->name;
				avatar_use_names[i->first] = i->second->getUseName();
			}
		}
	}

	Lock lock(worker_thread_manager.getMutex());
	for(auto i = worker_thread_manager.getThreads().begin(); i != worker_thread_manager.getThreads().end(); ++i)
	{
		assert(dynamic_cast<WorkerThread*>(i->ptr()));
		WorkerThread* worker_thread = static_cast<WorkerThread*>(i->ptr());

		if(worker_thread->cur_world_state.ptr() != world)
			continue;

		const UID avatar_uid = worker_thread->getRoutingAvatarUID();
		bool is_recipient = recipient_avatar_uid.valid() && (avatar_uid == recipient_avatar_uid);
		// A supplied UID is authoritative. Never broaden a private recipient to
		// other users with matching display names, even if that UID is offline.
		const bool allow_name_lookup = !recipient_avatar_uid.valid();

		if(allow_name_lookup && !is_recipient)
			is_recipient = StringUtils::equalCaseInsensitive(worker_thread->getRoutingUserName(), recipient_name);

		auto avatar_name_res = avatar_names.find(avatar_uid);
		if(allow_name_lookup && !is_recipient && avatar_name_res != avatar_names.end())
			is_recipient = StringUtils::equalCaseInsensitive(avatar_name_res->second, recipient_name);

		auto avatar_use_name_res = avatar_use_names.find(avatar_uid);
		if(allow_name_lookup && !is_recipient && avatar_use_name_res != avatar_use_names.end())
			is_recipient = StringUtils::equalCaseInsensitive(avatar_use_name_res->second, recipient_name);

		if(is_recipient)
		{
			worker_thread->enqueueDataToSend(recipient_packet);
			sent = true;
		}
	}

	if(sent && sender)
		sender->enqueueDataToSend(sender_packet);

	return sent;
}



void Server::clientDisconnected(WorkerThread* worker_thread)
{
	conPrint("Server::clientDisconnected(): worker_thread: 0x" + toHexString((uint64)worker_thread));

	{
		Lock lock(connected_clients_mutex);
		connected_clients.erase(worker_thread);
		connected_clients_changed = 1;
	}
}
