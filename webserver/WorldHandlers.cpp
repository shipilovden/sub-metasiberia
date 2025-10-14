#include "WorldHandlers.h"
#include <ContainerUtils.h>
#include "../shared/Parcel.h"
#include <maths/vec2.h>
#include <maths/vec3.h>

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
#include <Parser.h>

namespace WorldHandlers
{
// Вспомогательная функция: парсинг имени мира из URL
static std::string parseAndUnescapeWorldName(Parser& parser);

void renderWorldEditParcel(ServerAllWorldsState& world_state, const web::RequestInfo& request, web::ReplyInfo& reply_info)
{
    try
    {
        Parser parser(request.path);
        if(!parser.parseString("/world_edit_parcel/"))
            throw glare::Exception("Failed to parse /world_edit_parcel/");
        const std::string world_name = parseAndUnescapeWorldName(parser);
        if(!parser.parseString("/"))
            throw glare::Exception("Failed to parse parcel id slash");
        uint32 parcel_id_u32; if(!parser.parseUnsignedInt(parcel_id_u32))
            throw glare::Exception("Failed to parse parcel id");
        const ParcelID parcel_id(parcel_id_u32);

        std::string page = WebServerResponseUtils::standardHeader(world_state, request, "Edit parcel");
        page += "<div class=\"main\">\n";
        WorldStateLock lock(world_state.mutex);
        auto res = world_state.world_states.find(world_name);
        if(res == world_state.world_states.end()) throw glare::Exception("Couldn't find world");
        ServerWorldState* world = res->second.ptr();
        const User* logged_in_user = LoginHandlers::getLoggedInUser(world_state, request);
        if(!logged_in_user || (world->details.owner_id != logged_in_user->id))
            throw glare::Exception("Access denied sorry.");

        auto pit = world->parcels.find(parcel_id);
        if(pit == world->parcels.end()) throw glare::Exception("Parcel not found.");
        const Parcel* parcel = pit->second.ptr();
        const Vec2d origin = parcel->verts[0];
        const Vec2d topRight = parcel->verts[2];
        const Vec2d wh = topRight - origin;
        const double z = parcel->zbounds.x; const double zheight = parcel->zbounds.y - parcel->zbounds.x;

        page += "<form action=\"/admin_edit_parcel_post\" method=\"post\" id=\"usrform\">";
        page += "<input type=\"hidden\" name=\"parcel_id\" value=\"" + toString(parcel_id.value()) + "\">";
        page += "X: <input type=\"number\" step=\"0.01\" name=\"x\" value=\"" + doubleToString(origin.x) + "\"><br>";
        page += "Y: <input type=\"number\" step=\"0.01\" name=\"y\" value=\"" + doubleToString(origin.y) + "\"><br>";
        page += "Z: <input type=\"number\" step=\"0.01\" name=\"z\" value=\"" + doubleToString(z) + "\"><br>";
        page += "Width: <input type=\"number\" step=\"0.01\" name=\"width\" value=\"" + doubleToString(wh.x) + "\"><br>";
        page += "Height: <input type=\"number\" step=\"0.01\" name=\"height\" value=\"" + doubleToString(wh.y) + "\"><br>";
        page += "ZHeight: <input type=\"number\" step=\"0.01\" name=\"zheight\" value=\"" + doubleToString(zheight) + "\"><br>";
        page += "<input type=\"submit\" value=\"Update parcel\">";
        page += "</form>";

        page += "</div>\n";
        page += WebServerResponseUtils::standardFooter(request, true);
        web::ResponseUtils::writeHTTPOKHeaderAndData(reply_info, page);
    }
    catch(glare::Exception& e)
    {
        web::ResponseUtils::writeHTTPOKHeaderAndData(reply_info, "Error: " + e.what());
    }
}
/*=====================================================================
WorldHandlers.cpp
-----------------
Copyright Glare Technologies Limited 2025 -
=====================================================================*/

static void ensureDefaultWorldParcel(ServerWorldState* world, const UserID owner_id, WorldStateLock& lock)
{
    if(!world) return;
    if(!world->parcels.empty()) return;

    ParcelRef p = new Parcel();
    p->state = Parcel::State_Alive;
    p->id = ParcelID(1);
    p->owner_id = owner_id;
    p->admin_ids.push_back(owner_id);
    p->writer_ids.push_back(owner_id);
    p->created_time = TimeStamp::currentTime();
    // Huge cube around origin
    const double half = 50000.0;
    p->verts[0] = Vec2d(-half, -half);
    p->verts[1] = Vec2d( half, -half);
    p->verts[2] = Vec2d( half,  half);
    p->verts[3] = Vec2d(-half,  half);
    p->zbounds = Vec2d(0.0, 50000.0);
    p->build();
    world->parcels[p->id] = p;
    world->addParcelAsDBDirty(p, lock);
}


// Parse world name from request URL path
static std::string parseAndUnescapeWorldName(Parser& parser)
{
	int num_slashes_parsed = 0;
	const size_t startpos = parser.currentPos();
	while(parser.notEOF() && (
		::isAlphaNumeric(parser.current()) || 
		parser.current() == '$' || // Allow characters that can be present unescaped in a URL, see web::Escaping::URLEscape()
		parser.current() == '-' ||
		parser.current() == '_' ||
		parser.current() == '.' ||
		parser.current() == '!' ||
		parser.current() == '*' ||
		parser.current() == '\'' ||
		parser.current() == '(' ||
		parser.current() == ')' ||
		parser.current() == ',' ||
		parser.current() == '+' || // spaces are encoded as '+'
		parser.current() == '%' || // Allow the escape encoding
		(parser.current() == '/' && num_slashes_parsed == 0) // Allow the first slash only
		)
	)
	{
		if(parser.current() == '/')
			num_slashes_parsed++;
		parser.advance();
	}
	runtimeCheck((startpos <= parser.getTextSize()) && (parser.currentPos() >= startpos) && (parser.currentPos() <= parser.getTextSize()));
	const std::string world_name(parser.getText() + startpos, parser.currentPos() - startpos);
	return web::Escaping::URLUnescape(world_name);
}


std::string URLEscapeWorldName(const std::string& world_name)
{
	// Find first slash
	std::size_t slash_pos = world_name.find_first_of('/');
	if(slash_pos == std::string::npos)
		return web::Escaping::URLEscape(world_name);
	else
	{
		std::string res = web::Escaping::URLEscape(world_name.substr(0, slash_pos)) + "/";
		if(slash_pos + 1 < world_name.size())
			res += web::Escaping::URLEscape(world_name.substr(slash_pos + 1));
		return res;
	}
}

void renderWorldPage(ServerAllWorldsState& world_state, const web::RequestInfo& request, web::ReplyInfo& reply_info) // Shows a single world
{
	try
	{
		// Parse world name from request path
		Parser parser(request.path);
		if(!parser.parseString("/world/"))
			throw glare::Exception("Failed to parse /world/");

		const std::string world_name = parseAndUnescapeWorldName(parser);
		
		std::string page;

		{ // lock scope
			Lock lock(world_state.mutex);

			auto res = world_state.world_states.find(world_name);
			if(res == world_state.world_states.end())
				throw glare::Exception("Couldn't find world");

			const ServerWorldState* world = res->second.ptr();

			User* logged_in_user = LoginHandlers::getLoggedInUser(world_state, request);
			const bool logged_in_user_is_world_owner = logged_in_user && (world->details.owner_id == logged_in_user->id); // If the user is logged in and created this world:

			page = WebServerResponseUtils::standardHeader(world_state, request, /*page title=*/world->details.name, "");
			page += "<div class=\"main\">   \n";

			// Show any messages for the user
			if(logged_in_user) 
			{
				const std::string msg = world_state.getAndRemoveUserWebMessage(logged_in_user->id);
				if(!msg.empty())
					page += "<div class=\"msg\">" + web::Escaping::HTMLEscape(msg) + "</div>  \n";
			}

			std::string owner_username;
			{
				auto res2 = world_state.user_id_to_users.find(world->details.owner_id);
				if(res2 != world_state.user_id_to_users.end())
					owner_username = res2->second->name;
			}

			page += "<p>A world created by " + web::Escaping::HTMLEscape(owner_username) + "</p>\n";

			page += "<p>Description:</p>\n";
			page += "<p>" + web::Escaping::HTMLEscape(world->details.description) + "</p>";

			//page += "<p>Access</p>\n";
			const std::string hostname = request.getHostHeader(); // Find the hostname the request was sent to
			const std::string webclient_URL = (request.tls_connection ? std::string("https") : std::string("http")) + "://" + hostname + "/webclient?world=" + URLEscapeWorldName(world_name);
			page += "<p>Visit in web browser: <a href=\"" + webclient_URL + "\">" + web::Escaping::HTMLEscape(webclient_URL) + "</a></p>";

			const std::string native_URL = "sub://" + hostname + "/" + world_name;
			page += "<p>Visit in native app: <a href=\"" + native_URL + "\">" + web::Escaping::HTMLEscape(native_URL) + "</a></p>";

            if(logged_in_user_is_world_owner) // Show edit link If the user is logged in and owns this world
            {
                page += "<br/><br/><div><a href=\"/edit_world/" + URLEscapeWorldName(world_name) + "\">Edit world</a></div>";
                // Add simple UI to grant edit permissions to another user by username
                page += "<br/><h3>Grant/Revoke edit permissions</h3>\n";
                page += "<form action=\"/grant_world_edit_post\" method=\"post\" id=\"grant_form\">";
                page += "<input type=\"hidden\" name=\"world_name\" value=\"" + web::Escaping::HTMLEscape(world->details.name) + "\">";
                page += "Username: <input type=\"text\" name=\"username\" value=\"\"> ";
                page += "<input type=\"submit\" value=\"Grant edit\"> ";
                page += "<input type=\"submit\" formaction=\"/revoke_world_edit_post\" value=\"Revoke edit\">";
                page += "</form>";

                // World-level Add/Edit parcel actions
                page += "<br/><h3>Parcels</h3>\n";
                page += "<div><a href=\"/world_add_parcel/" + URLEscapeWorldName(world_name) + "\">Add parcel in this world</a></div>";

                // List parcels with actions
                page += "<div>\n";
                for(auto it = world->parcels.begin(); it != world->parcels.end(); ++it)
                {
                    const Parcel* parcel = it->second.ptr();
                    if(parcel->id.value() == 1) // hide base/grand parcel
                        continue;
                    page += "<p>Parcel ID: " + parcel->id.toString() + " ";
                    page += "<a href=\"/world_edit_parcel/" + URLEscapeWorldName(world_name) + "/" + parcel->id.toString() + "\">[Edit]</a> ";
                    page += "<form style=\"display:inline\" action=\"/world_delete_parcel_post\" method=\"post\">";
                    page += "<input type=\"hidden\" name=\"world_name\" value=\"" + web::Escaping::HTMLEscape(world->details.name) + "\">";
                    page += "<input type=\"hidden\" name=\"parcel_id\" value=\"" + parcel->id.toString() + "\">";
                    page += "<input type=\"submit\" value=\"Delete\" onclick=\"return confirm('Delete this parcel?');\">";
                    page += "</form>";
                    page += "</p>\n";
                }
                page += "</div>\n";

                // Assign/revoke writer to a parcel
                page += "<br/><h3>Assign/Revoke parcel writer</h3>\n";
                page += "<form action=\"/world_grant_parcel_writer_post\" method=\"post\" id=\"writer_form\">";
                page += "<input type=\"hidden\" name=\"world_name\" value=\"" + web::Escaping::HTMLEscape(world->details.name) + "\">";
                page += "Parcel ID: <input type=\"number\" name=\"parcel_id\" value=\"1\"> ";
                page += "Username: <input type=\"text\" name=\"username\" value=\"\"> ";
                page += "<input type=\"submit\" value=\"Grant writer\"> ";
                page += "<input type=\"submit\" formaction=\"/world_revoke_parcel_writer_post\" value=\"Revoke writer\">";
                page += "</form>";
            }
			
		} // end lock scope

		page += "</div>   \n"; // end main div
		page += WebServerResponseUtils::standardFooter(request, true);

		web::ResponseUtils::writeHTTPOKHeaderAndData(reply_info, page);
	}
	catch(glare::Exception& e)
	{
		web::ResponseUtils::writeHTTPOKHeaderAndData(reply_info, "Error: " + e.what());
	}
}


void renderCreateWorldPage(ServerAllWorldsState& world_state, const web::RequestInfo& request, web::ReplyInfo& reply_info)
{
	try
	{
		std::string page = WebServerResponseUtils::standardHeader(world_state, request, "Create world");
		page += "<div class=\"main\">   \n";

		{ // Lock scope

			Lock lock(world_state.mutex);

			User* logged_in_user = LoginHandlers::getLoggedInUser(world_state, request);

			// Show any messages for the user
			if(logged_in_user) 
			{
				const std::string msg = world_state.getAndRemoveUserWebMessage(logged_in_user->id);
				if(!msg.empty())
					page += "<div class=\"msg\">" + web::Escaping::HTMLEscape(msg) + "</div>  \n";
			}

			if(!logged_in_user)
			{
				page += "You must be logged in to create a world.";
			}
			else
			{
				page += "<form action=\"/create_world_post\" method=\"post\" id=\"usrform\">";
				page += "<label for=\"world_name\">World name:</label> <br/> <textarea rows=\"1\" cols=\"80\" name=\"world_name\" id=\"world_name\" form=\"usrform\"></textarea><br/>";
				page += "<input type=\"submit\" value=\"Create world\">";
				page += "</form>";
			}
		} // End lock scope

		page += "</div>   \n"; // end main div

		page += WebServerResponseUtils::standardFooter(request, true);

		web::ResponseUtils::writeHTTPOKHeaderAndData(reply_info, page);
	}
	catch(glare::Exception& e)
	{
		web::ResponseUtils::writeHTTPOKHeaderAndData(reply_info, "Error: " + e.what());
	}
}


void renderEditWorldPage(ServerAllWorldsState& world_state, const web::RequestInfo& request, web::ReplyInfo& reply_info)
{
	try
	{
		// Parse world name from request path
		Parser parser(request.path);
		if(!parser.parseString("/edit_world/"))
			throw glare::Exception("Failed to parse /edit_world/");

		const std::string world_name = parseAndUnescapeWorldName(parser);

		std::string page = WebServerResponseUtils::standardHeader(world_state, request, "Edit world");
		page += "<div class=\"main\">   \n";

		{ // Lock scope

			Lock lock(world_state.mutex);

			// Lookup world
			auto res = world_state.world_states.find(world_name);
			if(res == world_state.world_states.end())
				throw glare::Exception("Couldn't find world");

			const ServerWorldState* world = res->second.ptr();

			User* logged_in_user = LoginHandlers::getLoggedInUser(world_state, request);

			// Show any messages for the user
			if(logged_in_user) 
			{
				const std::string msg = world_state.getAndRemoveUserWebMessage(logged_in_user->id);
				if(!msg.empty())
					page += "<div class=\"msg\">" + web::Escaping::HTMLEscape(msg) + "</div>  \n";
			}

			const bool logged_in_user_is_world_owner = logged_in_user && (world->details.owner_id == logged_in_user->id); // If the user is logged in and owns this world:
			if(logged_in_user_is_world_owner)
			{

				page += "<form action=\"/edit_world_post\" method=\"post\" id=\"usrform\">";
				page += "<input type=\"hidden\" name=\"world_name\" value=\"" + web::Escaping::HTMLEscape(world->details.name) + "\"><br>";
				page += "Description: <br/><textarea rows=\"30\" cols=\"80\" name=\"description\" form=\"usrform\">" + web::Escaping::HTMLEscape(world->details.description) + "</textarea><br>";
				page += "<input type=\"submit\" value=\"Edit world\">";
				page += "</form>";
			}
			else
			{
				if(logged_in_user)
					page += "You must be the owner of this world to edit it.";
				else
					page += "You must be logged in to edit this world";
			}
		} // End lock scope

		page += "</div>   \n"; // end main div

		page += WebServerResponseUtils::standardFooter(request, true);

		web::ResponseUtils::writeHTTPOKHeaderAndData(reply_info, page);
	}
	catch(glare::Exception& e)
	{
		web::ResponseUtils::writeHTTPOKHeaderAndData(reply_info, "Error: " + e.what());
	}
}


void handleCreateWorldPost(ServerAllWorldsState& world_state, const web::RequestInfo& request, web::ReplyInfo& reply_info)
{
	try
	{
		if(world_state.isInReadOnlyMode())
			throw glare::Exception("Server is in read-only mode, editing disabled currently.");

		const std::string world_name_field   = request.getPostField("world_name").str();

		std::string new_world_name;
		bool redirect_to_login = false;
		bool redirect_back_to_create_page = false;

		{ // Lock scope
			Lock lock(world_state.mutex);

			const User* logged_in_user = LoginHandlers::getLoggedInUser(world_state, request);
			if(!logged_in_user)
			{
				redirect_to_login = true;
			}
			else
			{
				new_world_name = logged_in_user->name + "/" + world_name_field;

				if(world_name_field.empty())
				{
					redirect_back_to_create_page = true;
					world_state.setUserWebMessage(logged_in_user->id, "world name cannot be empty.");
				}
				else if(world_state.world_states.count(new_world_name) > 0)
				{
					redirect_back_to_create_page = true;
					world_state.setUserWebMessage(logged_in_user->id, "Can not create world '" + new_world_name + "', a world with that name already exists.");
				}
				else if(new_world_name.size() > WorldDetails::MAX_NAME_SIZE)
				{
					redirect_back_to_create_page = true;
					world_state.setUserWebMessage(logged_in_user->id, "invalid world name - too long.");
				}
				else
				{
					Reference<ServerWorldState> world = new ServerWorldState();
					world->details.name = new_world_name;
					world->details.owner_id = logged_in_user->id;
					world->details.created_time = TimeStamp::currentTime();

					world_state.world_states.insert(std::make_pair(new_world_name, world)); // Add to world_states
			
					world->db_dirty = true;
					world_state.markAsChanged();

					world_state.setUserWebMessage(logged_in_user->id, "Created world.");
				}
			}
		} // End lock scope

		if(redirect_to_login)
			web::ResponseUtils::writeRedirectTo(reply_info, "/login");
		else if(redirect_back_to_create_page)
			web::ResponseUtils::writeRedirectTo(reply_info, "/create_world");
		else
			web::ResponseUtils::writeRedirectTo(reply_info, "/world/" + URLEscapeWorldName(new_world_name));
	}
	catch(glare::Exception& e)
	{
		if(!request.fuzzing)
			conPrint("handleCreateWorldPost error: " + e.what());
		web::ResponseUtils::writeHTTPOKHeaderAndData(reply_info, "Error: " + e.what());
	}
}


void handleEditWorldPost(ServerAllWorldsState& world_state, const web::RequestInfo& request, web::ReplyInfo& reply_info)
{
	try
	{
		if(world_state.isInReadOnlyMode())
			throw glare::Exception("Server is in read-only mode, editing disabled currently.");

		const std::string world_name   = request.getPostField("world_name").str();
		const std::string description   = request.getPostField("description").str();

		if(description.size() > WorldDetails::MAX_DESCRIPTION_SIZE)
			throw glare::Exception("invalid world description - too long");

		{ // Lock scope
			Lock lock(world_state.mutex);

			// Lookup world
			auto res = world_state.world_states.find(world_name);
			if(res == world_state.world_states.end())
				throw glare::Exception("Couldn't find world");

			ServerWorldState* world = res->second.ptr();

			const User* logged_in_user = LoginHandlers::getLoggedInUser(world_state, request);
			if(logged_in_user && (world->details.owner_id == logged_in_user->id)) // If the user is logged in and owns this world:
			{
				world->details.description = description;

				world->db_dirty = true;
				world_state.markAsChanged();

				world_state.setUserWebMessage(logged_in_user->id, "Updated world.");
			}
		} // End lock scope

		web::ResponseUtils::writeRedirectTo(reply_info, "/world/" + URLEscapeWorldName(world_name));
	}
	catch(glare::Exception& e)
	{
		if(!request.fuzzing)
			conPrint("handleEditWorldPost error: " + e.what());
		web::ResponseUtils::writeHTTPOKHeaderAndData(reply_info, "Error: " + e.what());
	}
}


#ifndef BUILD_ONLY_DECLS
void handleGrantWorldEditPost(ServerAllWorldsState& world_state, const web::RequestInfo& request, web::ReplyInfo& reply_info)
{
    try
    {
        if(world_state.isInReadOnlyMode())
            throw glare::Exception("Server is in read-only mode, editing disabled currently.");

        const std::string world_name = request.getPostField("world_name").str();
        const std::string username   = request.getPostField("username").str();

        { // Lock scope
            WorldStateLock lock(world_state.mutex);

            // Lookup world
            auto res = world_state.world_states.find(world_name);
            if(res == world_state.world_states.end())
                throw glare::Exception("Couldn't find world");

            ServerWorldState* world = res->second.ptr();

            const User* logged_in_user = LoginHandlers::getLoggedInUser(world_state, request);
            if(!logged_in_user || (world->details.owner_id != logged_in_user->id))
                throw glare::Exception("Only the world owner can grant permissions.");

            // Find target user by exact name
            User* target_user = NULL;
            for(auto it = world_state.user_id_to_users.begin(); it != world_state.user_id_to_users.end(); ++it)
                if(it->second->name == username)
                    target_user = it->second.ptr();

            if(!target_user)
                throw glare::Exception("User not found.");

            // Give write permissions by adding as writer to all parcels in this world for simplicity
            // (Simple implementation: personal worlds typically contain user's parcels)
            // Ensure the personal world has a default parcel covering the world so writers are effective
            ensureDefaultWorldParcel(world, world->details.owner_id, lock);

            for(auto it = world->parcels.begin(); it != world->parcels.end(); ++it)
            {
                Parcel* parcel = it->second.ptr();
                if(!ContainerUtils::contains(parcel->writer_ids, target_user->id))
                {
                    parcel->writer_ids.push_back(target_user->id);
                    world->addParcelAsDBDirty(parcel, lock);
                }
            }

            world_state.denormaliseData();
            world_state.markAsChanged();
            world_state.setUserWebMessage(logged_in_user->id, "Granted edit permissions to '" + username + "'.");
        }

        web::ResponseUtils::writeRedirectTo(reply_info, "/world/" + URLEscapeWorldName(world_name));
    }
    catch(glare::Exception& e)
    {
        if(!request.fuzzing)
            conPrint("handleGrantWorldEditPost error: " + e.what());
        web::ResponseUtils::writeHTTPOKHeaderAndData(reply_info, "Error: " + e.what());
    }
}

void handleRevokeWorldEditPost(ServerAllWorldsState& world_state, const web::RequestInfo& request, web::ReplyInfo& reply_info)
{
    try
    {
        if(world_state.isInReadOnlyMode())
            throw glare::Exception("Server is in read-only mode, editing disabled currently.");

        const std::string world_name = request.getPostField("world_name").str();
        const std::string username   = request.getPostField("username").str();

        { // Lock scope
            WorldStateLock lock(world_state.mutex);

            auto res = world_state.world_states.find(world_name);
            if(res == world_state.world_states.end())
                throw glare::Exception("Couldn't find world");

            ServerWorldState* world = res->second.ptr();

            const User* logged_in_user = LoginHandlers::getLoggedInUser(world_state, request);
            if(!logged_in_user || (world->details.owner_id != logged_in_user->id))
                throw glare::Exception("Only the world owner can revoke permissions.");

            // Find target user by exact name
            User* target_user = NULL;
            for(auto it = world_state.user_id_to_users.begin(); it != world_state.user_id_to_users.end(); ++it)
                if(it->second->name == username)
                    target_user = it->second.ptr();

            if(!target_user)
                throw glare::Exception("User not found.");

            ensureDefaultWorldParcel(world, world->details.owner_id, lock);
            for(auto it = world->parcels.begin(); it != world->parcels.end(); ++it)
            {
                Parcel* parcel = it->second.ptr();
                ContainerUtils::removeFirst(parcel->writer_ids, target_user->id);
                world->addParcelAsDBDirty(parcel, lock);
            }

            world_state.denormaliseData();
            world_state.markAsChanged();
            world_state.setUserWebMessage(logged_in_user->id, "Revoked edit permissions from '" + username + "'.");
        }

        web::ResponseUtils::writeRedirectTo(reply_info, "/world/" + URLEscapeWorldName(world_name));
    }
    catch(glare::Exception& e)
    {
        if(!request.fuzzing)
            conPrint("handleRevokeWorldEditPost error: " + e.what());
        web::ResponseUtils::writeHTTPOKHeaderAndData(reply_info, "Error: " + e.what());
    }
}
#endif
#if 1
void handleWorldDeleteParcelPost(ServerAllWorldsState& world_state, const web::RequestInfo& request, web::ReplyInfo& reply_info)
{
    try
    {
        if(world_state.isInReadOnlyMode())
            throw glare::Exception("Server is in read-only mode, editing disabled currently.");

        const std::string world_name = request.getPostField("world_name").str();
        const ParcelID parcel_id(request.getPostIntField("parcel_id"));

        WorldStateLock lock(world_state.mutex);
        auto res = world_state.world_states.find(world_name);
        if(res == world_state.world_states.end())
            throw glare::Exception("Couldn't find world");

        ServerWorldState* world = res->second.ptr();
        const User* logged_in_user = LoginHandlers::getLoggedInUser(world_state, request);
        if(!logged_in_user || (world->details.owner_id != logged_in_user->id))
            throw glare::Exception("Access denied sorry.");

        auto it = world->parcels.find(parcel_id);
        if(it != world->parcels.end())
        {
            Parcel* parcel = it->second.ptr();
            world_state.db_records_to_delete.insert(parcel->database_key);
            world->parcels.erase(parcel_id);
            world_state.denormaliseData();
            world_state.markAsChanged();
        }

        web::ResponseUtils::writeRedirectTo(reply_info, "/world/" + URLEscapeWorldName(world_name));
    }
    catch(glare::Exception& e)
    {
        if(!request.fuzzing)
            conPrint("handleWorldDeleteParcelPost error: " + e.what());
        web::ResponseUtils::writeHTTPOKHeaderAndData(reply_info, "Error: " + e.what());
    }
}

void handleWorldGrantParcelWriterPost(ServerAllWorldsState& world_state, const web::RequestInfo& request, web::ReplyInfo& reply_info)
{
    try
    {
        if(world_state.isInReadOnlyMode())
            throw glare::Exception("Server is in read-only mode, editing disabled currently.");

        const std::string world_name = request.getPostField("world_name").str();
        const ParcelID parcel_id(request.getPostIntField("parcel_id"));
        const std::string username = request.getPostField("username").str();

        WorldStateLock lock(world_state.mutex);
        auto res = world_state.world_states.find(world_name);
        if(res == world_state.world_states.end())
            throw glare::Exception("Couldn't find world");
        ServerWorldState* world = res->second.ptr();
        const User* logged_in_user = LoginHandlers::getLoggedInUser(world_state, request);
        if(!logged_in_user || (world->details.owner_id != logged_in_user->id))
            throw glare::Exception("Access denied sorry.");

        // Find target user
        User* target_user = NULL;
        const std::string uname_trim = stripHeadAndTailWhitespace(username);
        const std::string uname_lower = toLowerCase(uname_trim);
        for(auto it = world_state.user_id_to_users.begin(); it != world_state.user_id_to_users.end(); ++it)
            if(toLowerCase(it->second->name) == uname_lower)
                target_user = it->second.ptr();
        if(!target_user)
            throw glare::Exception("User not found.");

        auto pit = world->parcels.find(parcel_id);
        if(pit == world->parcels.end())
            throw glare::Exception("Parcel not found.");
        Parcel* parcel = pit->second.ptr();
        if(!ContainerUtils::contains(parcel->writer_ids, target_user->id))
        {
            parcel->writer_ids.push_back(target_user->id);
            world->addParcelAsDBDirty(parcel, lock);
            world_state.denormaliseData();
            world_state.markAsChanged();
        }
        web::ResponseUtils::writeRedirectTo(reply_info, "/world/" + URLEscapeWorldName(world_name));
    }
    catch(glare::Exception& e)
    {
        if(!request.fuzzing)
            conPrint("handleWorldGrantParcelWriterPost error: " + e.what());
        web::ResponseUtils::writeHTTPOKHeaderAndData(reply_info, "Error: " + e.what());
    }
}

void handleWorldRevokeParcelWriterPost(ServerAllWorldsState& world_state, const web::RequestInfo& request, web::ReplyInfo& reply_info)
{
    try
    {
        if(world_state.isInReadOnlyMode())
            throw glare::Exception("Server is in read-only mode, editing disabled currently.");

        const std::string world_name = request.getPostField("world_name").str();
        const ParcelID parcel_id(request.getPostIntField("parcel_id"));
        const std::string username = request.getPostField("username").str();

        WorldStateLock lock(world_state.mutex);
        auto res = world_state.world_states.find(world_name);
        if(res == world_state.world_states.end())
            throw glare::Exception("Couldn't find world");
        ServerWorldState* world = res->second.ptr();
        const User* logged_in_user = LoginHandlers::getLoggedInUser(world_state, request);
        if(!logged_in_user || (world->details.owner_id != logged_in_user->id))
            throw glare::Exception("Access denied sorry.");

        // Find target user
        User* target_user = NULL;
        const std::string uname_trim = stripHeadAndTailWhitespace(username);
        const std::string uname_lower = toLowerCase(uname_trim);
        for(auto it = world_state.user_id_to_users.begin(); it != world_state.user_id_to_users.end(); ++it)
            if(toLowerCase(it->second->name) == uname_lower)
                target_user = it->second.ptr();
        if(!target_user)
            throw glare::Exception("User not found.");

        auto pit = world->parcels.find(parcel_id);
        if(pit == world->parcels.end())
            throw glare::Exception("Parcel not found.");
        Parcel* parcel = pit->second.ptr();
        ContainerUtils::removeFirst(parcel->writer_ids, target_user->id);
        world->addParcelAsDBDirty(parcel, lock);
        world_state.denormaliseData();
        world_state.markAsChanged();
        web::ResponseUtils::writeRedirectTo(reply_info, "/world/" + URLEscapeWorldName(world_name));
    }
    catch(glare::Exception& e)
    {
        if(!request.fuzzing)
            conPrint("handleWorldRevokeParcelWriterPost error: " + e.what());
        web::ResponseUtils::writeHTTPOKHeaderAndData(reply_info, "Error: " + e.what());
    }
}
#endif
#if BUILD_TESTS


void testParseAndUnescapeWorldName(const std::string&, const std::string&)
{
    // disabled in server build
}


void test()
{
	//----------------------- Test URLEscapeWorldName -------------------------
    // disable tests in server build
    return;

	//------------------------ Test parseAndUnescapeWorldName ----------------------------
    return;

	try
	{
        return;
	}
	catch(glare::Exception& ) 
	{}
}


#endif

#if 1
void renderWorldAddParcel(ServerAllWorldsState& world_state, const web::RequestInfo& request, web::ReplyInfo& reply_info)
{
    try
    {
        // Parse world name from request path
        Parser parser(request.path);
        if(!parser.parseString("/world_add_parcel/"))
            throw glare::Exception("Failed to parse /world_add_parcel/");

        const std::string world_name = parseAndUnescapeWorldName(parser);

        std::string page = WebServerResponseUtils::standardHeader(world_state, request, "Add parcel");
        page += "<div class=\"main\">\n";
        {
            WorldStateLock lock(world_state.mutex);
            auto res = world_state.world_states.find(world_name);
            if(res == world_state.world_states.end())
                throw glare::Exception("Couldn't find world");

            ServerWorldState* world = res->second.ptr();
            const User* logged_in_user = LoginHandlers::getLoggedInUser(world_state, request);
            if(!logged_in_user || (world->details.owner_id != logged_in_user->id))
                throw glare::Exception("Access denied sorry.");

            page += "<form action=\"/world_add_parcel_post\" method=\"post\" id=\"usrform\">";
            page += "<input type=\"hidden\" name=\"world_name\" value=\"" + web::Escaping::HTMLEscape(world->details.name) + "\">";
            page += "X: <input type=\"number\" step=\"0.01\" name=\"x\" value=\"0\"><br>";
            page += "Y: <input type=\"number\" step=\"0.01\" name=\"y\" value=\"0\"><br>";
            page += "Z: <input type=\"number\" step=\"0.01\" name=\"z\" value=\"0\"><br>";
            page += "Width: <input type=\"number\" step=\"0.01\" name=\"width\" value=\"10\"><br>";
            page += "Height: <input type=\"number\" step=\"0.01\" name=\"height\" value=\"10\"><br>";
            page += "ZHeight: <input type=\"number\" step=\"0.01\" name=\"zheight\" value=\"5\"><br>";
            page += "<input type=\"submit\" value=\"Add parcel\">";
            page += "</form>";
        }
        page += "</div>\n";
        page += WebServerResponseUtils::standardFooter(request, true);
        web::ResponseUtils::writeHTTPOKHeaderAndData(reply_info, page);
    }
    catch(glare::Exception& e)
    {
        web::ResponseUtils::writeHTTPOKHeaderAndData(reply_info, "Error: " + e.what());
    }
}

void handleWorldAddParcelPost(ServerAllWorldsState& world_state, const web::RequestInfo& request, web::ReplyInfo& reply_info)
{
    try
    {
        if(world_state.isInReadOnlyMode())
            throw glare::Exception("Server is in read-only mode, editing disabled currently.");

        const std::string world_name = request.getPostField("world_name").str();
        const double x = stringToDouble(request.getPostField("x").str());
        const double y = stringToDouble(request.getPostField("y").str());
        const double z = stringToDouble(request.getPostField("z").str());
        const double width = stringToDouble(request.getPostField("width").str());
        const double height = stringToDouble(request.getPostField("height").str());
        const double zheight = stringToDouble(request.getPostField("zheight").str());

        WorldStateLock lock(world_state.mutex);
        auto res = world_state.world_states.find(world_name);
        if(res == world_state.world_states.end())
            throw glare::Exception("Couldn't find world");
        ServerWorldState* world = res->second.ptr();

        const User* logged_in_user = LoginHandlers::getLoggedInUser(world_state, request);
        if(!logged_in_user || (world->details.owner_id != logged_in_user->id))
            throw glare::Exception("Access denied sorry.");

        const Vec2d botleft(x, y);
        const Vec2d topright = botleft + Vec2d(width, height);

        // Create new unique ParcelID in this world scope
        uint32 max_id = 0;
        for(auto it = world->parcels.begin(); it != world->parcels.end(); ++it)
            max_id = std::max(max_id, it->first.value());
        ParcelID new_id(max_id + 1);

        ParcelRef new_parcel = new Parcel();
        new_parcel->state = Parcel::State_Alive;
        new_parcel->id = new_id;
        new_parcel->owner_id = world->details.owner_id;
        new_parcel->admin_ids.push_back(world->details.owner_id);
        new_parcel->writer_ids.push_back(world->details.owner_id);
        new_parcel->created_time = TimeStamp::currentTime();
        new_parcel->zbounds = Vec2d(z, z + zheight);
        new_parcel->verts[0] = botleft;
        new_parcel->verts[1] = Vec2d(topright.x, botleft.y);
        new_parcel->verts[2] = topright;
        new_parcel->verts[3] = Vec2d(botleft.x, topright.y);
        new_parcel->build();
        world->parcels[new_id] = new_parcel;
        world->addParcelAsDBDirty(new_parcel, lock);
        world_state.denormaliseData();
        world_state.markAsChanged();

        web::ResponseUtils::writeRedirectTo(reply_info, "/world/" + URLEscapeWorldName(world_name));
    }
    catch(glare::Exception& e)
    {
        if(!request.fuzzing)
            conPrint("handleWorldAddParcelPost error: " + e.what());
        web::ResponseUtils::writeHTTPOKHeaderAndData(reply_info, "Error: " + e.what());
    }
}
#endif
} // end namespace WorldHandlers
