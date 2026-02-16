/*=====================================================================
ScreenshotHandlers.cpp
----------------------
Copyright Glare Technologies Limited 2021 -
=====================================================================*/
#include "ScreenshotHandlers.h"


#include "RequestInfo.h"
#include "RequestInfo.h"
#include "Response.h"
#include "WebsiteExcep.h"
#include "Escaping.h"
#include "ResponseUtils.h"
#include "WebServerResponseUtils.h"
#include "LoginHandlers.h"
#include "../server/ServerWorldState.h"
#include <ConPrint.h>
#include <Exception.h>
#include <Lock.h>
#include <StringUtils.h>
#include <PlatformUtils.h>
#include <ConPrint.h>
#include <Parser.h>
#include <MemMappedFile.h>
#include <cmath>


namespace ScreenshotHandlers
{

static std::string parseWorldNameFromSubURL(const std::string& sub_url)
{
	if(!hasPrefix(sub_url, "sub://"))
		return sub_url;

	const size_t host_start = 6; // after "sub://"
	const size_t slash_pos = sub_url.find('/', host_start);
	if(slash_pos == std::string::npos)
		return ""; // URL points to main world.

	const size_t world_start = slash_pos + 1;
	const size_t query_pos = sub_url.find('?', world_start);
	const std::string world_part = (query_pos == std::string::npos) ? sub_url.substr(world_start) : sub_url.substr(world_start, query_pos - world_start);
	return web::Escaping::URLUnescape(world_part);
}


static std::string normaliseWorldNameField(const std::string& world_field)
{
	const std::string trimmed = stripHeadAndTailWhitespace(world_field);
	if(hasPrefix(trimmed, "sub://"))
		return parseWorldNameFromSubURL(trimmed);
	return trimmed;
}


void handleScreenshotRequest(ServerAllWorldsState& world_state, WebDataStore& datastore, const web::RequestInfo& request, web::ReplyInfo& reply_info) // Shows order details
{
	try
	{
		// Parse screenshot id from request path
		Parser parser(request.path);
		if(!parser.parseString("/screenshot/"))
			throw glare::Exception("Failed to parse /screenshot/");

		uint32 screenshot_id;
		if(!parser.parseUnsignedInt(screenshot_id))
			throw glare::Exception("Failed to parse screenshot_id");

		// Get screenshot local path
		std::string local_path;
		{ // lock scope
			Lock lock(world_state.mutex);

			auto res = world_state.screenshots.find(screenshot_id);
			if(res == world_state.screenshots.end())
				throw glare::Exception("Couldn't find screenshot");

			local_path = res->second->local_path;
		} // end lock scope


		try
		{
			MemMappedFile file(local_path); // Load screenshot file
			
			const std::string content_type = web::ResponseUtils::getContentTypeForPath(local_path);

			// Send it to client
			web::ResponseUtils::writeHTTPOKHeaderAndDataWithCacheMaxAge(reply_info, file.fileData(), file.fileSize(), content_type, 3600*24*14); // cache max age = 2 weeks
		}
		catch(glare::Exception&)
		{
			web::ResponseUtils::writeHTTPOKHeaderAndData(reply_info, "Failed to load file '" + local_path + "'.");
		}
	}
	catch(glare::Exception& e)
	{
		web::ResponseUtils::writeHTTPOKHeaderAndData(reply_info, "Error: " + e.what());
	}
}


void handleMapTileRequest(ServerAllWorldsState& world_state, WebDataStore& datastore, const web::RequestInfo& request, web::ReplyInfo& reply_info) // Shows order details
{
	try
	{
		const std::string world_param = request.getURLParam("world").str();
		std::string world_name = normaliseWorldNameField(world_param);
		if(toLowerCase(world_name) == "root")
			world_name = "";

		const int x = request.getURLIntParam("x");
		const int y = -request.getURLIntParam("y") - 1; // NOTE: negated for y-down in leaflet.js, -1 to fix offset also.
		const int z = request.getURLIntParam("z");

		// Guardrails against abusive/accidental huge tile coordinates.
		if(z < 0 || z > 6 || std::abs(x) > 4096 || std::abs(y) > 4096)
			throw glare::Exception("Invalid tile coords.");

		// Get screenshot local path
		std::string local_path;
		{ // lock scope
			WorldStateLock lock(world_state.mutex);

			// Avoid creating per-world tile state for non-existent worlds.
			if(!world_name.empty() && (world_state.world_states.find(world_name) == world_state.world_states.end()))
				throw glare::Exception("Couldn't find world.");

			MapTileInfo& map_tile_info = world_state.getMapTileInfoForWorld(world_name, lock);

			const Vec3<int> v(x, y, z);
			auto res = map_tile_info.info.find(v);
			if(res == map_tile_info.info.end())
			{
				// Lazy-create a tile screenshot request for this world/tile.
				TileInfo info;
				info.cur_tile_screenshot = new Screenshot();
				info.cur_tile_screenshot->id = world_state.getNextScreenshotUIDUnlocked(lock);
				info.cur_tile_screenshot->created_time = TimeStamp::currentTime();
				info.cur_tile_screenshot->state = Screenshot::ScreenshotState_notdone;
				info.cur_tile_screenshot->is_map_tile = true;
				info.cur_tile_screenshot->tile_x = x;
				info.cur_tile_screenshot->tile_y = y;
				info.cur_tile_screenshot->tile_z = z;

				map_tile_info.info[v] = info;
				map_tile_info.db_dirty = true;
				world_state.markAsChanged();

				PendingMapTileScreenshot pending;
				pending.world_name = world_name;
				pending.tile_coords = v;
				world_state.pending_map_tile_screenshots.push_back(pending);

				// Tile isn't ready yet.
				throw glare::Exception("Map tile screenshot not done.");
			}

			const TileInfo& info = res->second;
			if(info.cur_tile_screenshot.nonNull() && info.cur_tile_screenshot->state == Screenshot::ScreenshotState_done)
				local_path = info.cur_tile_screenshot->local_path;
			else if(info.prev_tile_screenshot.nonNull() && info.prev_tile_screenshot->state == Screenshot::ScreenshotState_done)
				local_path = info.prev_tile_screenshot->local_path;
			else
				throw glare::Exception("Map tile screenshot not done.");
		} // end lock scope


		try
		{
			MemMappedFile file(local_path); // Load screenshot file
			
			const std::string content_type = web::ResponseUtils::getContentTypeForPath(local_path);

			// Send it to client
			web::ResponseUtils::writeHTTPOKHeaderAndDataWithCacheMaxAge(reply_info, file.fileData(), file.fileSize(), content_type, 3600*24*14); // cache max age = 2 weeks
		}
		catch(glare::Exception&)
		{
			web::ResponseUtils::writeHTTPOKHeaderAndData(reply_info, "Failed to load file '" + local_path + "'.");
		}
	}
	catch(glare::Exception& e)
	{
		web::ResponseUtils::writeHTTPNotFoundHeaderAndData(reply_info, "Error: " + e.what());
	}
}

} // end namespace ScreenshotHandlers
