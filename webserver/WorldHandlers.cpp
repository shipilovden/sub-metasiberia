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
#include <fstream>
#include <regex>

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

static void removeGrandParcelFromPersonalWorld(ServerWorldState* world, WorldStateLock& lock)
{
    if(!world) return;
    
    // Remove the grand parcel (ID: 1) from personal worlds
    auto it = world->parcels.find(ParcelID(1));
    if(it != world->parcels.end())
    {
        world->parcels.erase(it);
        conPrint("DEBUG: Removed grand parcel from personal world: " + world->details.name);
    }
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
		
		std::string page = WebServerResponseUtils::standardHeader(world_state, request, "World Management", "");
		page += "<div class=\"main\">\n";

		{ // lock scope
			Lock lock(world_state.mutex);

			auto res = world_state.world_states.find(world_name);
			if(res == world_state.world_states.end())
				throw glare::Exception("Couldn't find world");

			ServerWorldState* world = res->second.ptr();
			
			// Remove grand parcel from personal worlds (worlds with single name like "denshipilov")
			// Only remove from personal worlds, not from user-created worlds like "denshipilov/d1"
			if(world_name.find('/') == std::string::npos)
			{
				WorldStateLock world_lock(world_state.mutex);
				removeGrandParcelFromPersonalWorld(world, world_lock);
			}

			User* logged_in_user = LoginHandlers::getLoggedInUser(world_state, request);

			// Show any messages for the user
			if(logged_in_user) 
			{
				const std::string msg = world_state.getAndRemoveUserWebMessage(logged_in_user->id);
				if(!msg.empty())
					page += "<div class=\"msg\">" + web::Escaping::HTMLEscape(msg) + "</div>  \n";
			}

		// Get owner username
			std::string owner_username;
			{
				auto res2 = world_state.user_id_to_users.find(world->details.owner_id);
				if(res2 != world_state.user_id_to_users.end())
					owner_username = res2->second->name;
			}

		// Basic world info
		page += "<h1>" + web::Escaping::HTMLEscape(world->details.name) + "</h1>\n";
		page += "<p><strong>Owner:</strong> " + web::Escaping::HTMLEscape(owner_username) + "</p>\n";
		page += "<p><strong>Description:</strong> " + web::Escaping::HTMLEscape(world->details.description) + "</p>\n";
		page += "<p><strong>Created:</strong> " + world->details.created_time.dayString() + "</p>\n";
		
		// Generate URLs
		const std::string hostname = request.getHostHeader();
			const std::string webclient_URL = (request.tls_connection ? std::string("https") : std::string("http")) + "://" + hostname + "/webclient?world=" + URLEscapeWorldName(world_name);
			const std::string native_URL = "sub://" + hostname + "/" + world_name;
		
		page += "<h2>🔗 Quick Links</h2>\n";
		page += "<p><a href=\"" + webclient_URL + "\" target=\"_blank\" style=\"color: #0066cc; text-decoration: underline; font-weight: bold;\">🌐 Visit in Web</a>";
		page += "&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;"; // Добавляем пробелы
		page += "<a href=\"sub://" + hostname + "/" + world_name + "\" data-app-href=\"sub://" + hostname + "/" + world_name + "\" style=\"color: #0066cc; text-decoration: underline; font-weight: bold;\">💻 Visit in Metasiberia</a></p>\n";
		page += "<p><small style=\"color: #666; font-size: 11px;\">";
		page += "Web: " + webclient_URL + "<br>";
		page += "App: " + native_URL;
		page += "</small></p>\n";
		
		// World management section
		page += "<div style=\"border: 2px solid #ddd; margin: 15px 0; padding: 15px; background: #f8f9fa;\">\n";
		page += "<h2>🌍 World Management</h2>\n";
		page += "<p><strong>Quick Actions:</strong></p>\n";
		
		// Add delete world button for created worlds (not personal worlds)
		if(world_name.find('/') != std::string::npos && logged_in_user && world->details.owner_id == logged_in_user->id)
		{
			page += "<form action=\"/world_delete_post\" method=\"post\" style=\"margin-top: 15px; padding: 10px; border: 2px solid #ff0000; background: #ffeeee; border-radius: 5px;\">\n";
			page += "<input type=\"hidden\" name=\"world_name\" value=\"" + web::Escaping::HTMLEscape(world_name) + "\">\n";
			page += "<input type=\"submit\" value=\"🗑️ DELETE WORLD\" onclick=\"return confirm('Are you sure you want to delete this world? This action cannot be undone!');\" style=\"background: #ff0000; color: white; font-weight: bold; padding: 8px 15px; border: none; border-radius: 3px; cursor: pointer;\">\n";
			page += "</form>\n";
		}
		page += "</div>\n";
		
		
		// Parcels section
		page += "<div style=\"border: 2px solid #28a745; margin: 15px 0; padding: 15px; background: #f8fff8;\">\n";
		page += "<h2>🏗️ Parcels Management</h2>\n";
		int parcel_count = 0;
		for(auto it = world->parcels.begin(); it != world->parcels.end(); ++it) {
			const Parcel* parcel = it->second.ptr();
			// Only hide base parcel (ID=1) in personal worlds, not in user-created worlds
			// Hide grand parcel (ID=1) for all worlds
			if(parcel->id.value() == 1) continue;
			parcel_count++;
			
			// Calculate parcel size
			const Vec2d origin = parcel->verts[0];
			const Vec2d topRight = parcel->verts[2];
			const Vec2d size = topRight - origin;
			const double zheight = parcel->zbounds.y - parcel->zbounds.x;
			
			// Get writer names
			std::string writer_names;
			for(size_t i = 0; i < parcel->writer_ids.size(); ++i) {
				if(i > 0) writer_names += ", ";
				auto user_it = world_state.user_id_to_users.find(parcel->writer_ids[i]);
				if(user_it != world_state.user_id_to_users.end()) {
					writer_names += user_it->second->name;
				}
			}
			if(writer_names.empty()) writer_names = "—";
			
			// Generate parcel HTML in table format
			page += "<div style=\"border: 1px solid #ccc; margin: 10px 0; padding: 10px; background: #f9f9f9;\">\n";
			page += "<table style=\"width: 100%; border-collapse: collapse;\">\n";
			page += "<tr><td style=\"width: 50px; font-weight: bold;\">ID</td><td style=\"width: 100px; font-weight: bold;\">Size</td><td style=\"font-weight: bold;\">Writers</td><td style=\"font-weight: bold;\">Actions</td></tr>\n";
			page += "<tr>\n";
			page += "<td>" + parcel->id.toString() + "</td>\n";
			page += "<td>" + doubleToStringMaxNDecimalPlaces(size.x, 0) + "×" + doubleToStringMaxNDecimalPlaces(size.y, 0) + "×" + doubleToStringMaxNDecimalPlaces(zheight, 0) + "</td>\n";
			page += "<td>" + web::Escaping::HTMLEscape(writer_names) + "</td>\n";
			page += "<td>";
			
			// Generate parcel URLs
			const Vec3d parcel_center = Vec3d(origin.x + size.x/2, origin.y + size.y/2, parcel->zbounds.x + zheight/2);
			const std::string parcel_webclient_URL = webclient_URL + "&x=" + doubleToStringMaxNDecimalPlaces(parcel_center.x, 1) + "&y=" + doubleToStringMaxNDecimalPlaces(parcel_center.y, 1) + "&z=" + doubleToStringMaxNDecimalPlaces(parcel_center.z, 1);
			const std::string parcel_native_URL = "sub://" + hostname + "/" + world_name + "?x=" + doubleToStringMaxNDecimalPlaces(parcel_center.x, 1) + "&y=" + doubleToStringMaxNDecimalPlaces(parcel_center.y, 1) + "&z=" + doubleToStringMaxNDecimalPlaces(parcel_center.z, 1);
			
			page += "<form action=\"/world_delete_parcel_post\" method=\"post\" style=\"display: inline; margin-right: 10px;\">";
			page += "<input type=\"hidden\" name=\"world_name\" value=\"" + web::Escaping::HTMLEscape(world_name) + "\">";
			page += "<input type=\"hidden\" name=\"parcel_id\" value=\"" + parcel->id.toString() + "\">";
			page += "<input type=\"submit\" value=\"🗑️ Delete\" onclick=\"return confirm('Delete parcel " + parcel->id.toString() + "?');\" style=\"background: #ff0000; color: white; border: none; padding: 4px 8px; cursor: pointer; border-radius: 3px;\">";
			page += "</form>";
			page += "<a href=\"" + parcel_webclient_URL + "\" target=\"_blank\" style=\"color: #0066cc; text-decoration: underline; font-weight: bold;\">🌐 Visit in Web</a>";
			page += "&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;"; // Добавляем пробелы
			page += "<a href=\"sub://" + hostname + "/" + world_name + "?x=" + doubleToString(parcel->aabb_min.x) + "&y=" + doubleToString(parcel->aabb_min.y) + "&z=" + doubleToString(parcel->aabb_min.z) + "\" data-app-href=\"sub://" + hostname + "/" + world_name + "?x=" + doubleToString(parcel->aabb_min.x) + "&y=" + doubleToString(parcel->aabb_min.y) + "&z=" + doubleToString(parcel->aabb_min.z) + "\" style=\"color: #0066cc; text-decoration: underline; font-weight: bold;\">💻 Visit in Metasiberia</a>";
			page += "<br><small style=\"color: #666; font-size: 11px;\">";
			page += "Web: " + parcel_webclient_URL + "<br>";
			page += "App: " + parcel_native_URL;
			page += "</small>";
			page += "</td>\n";
			page += "</tr>\n";
			page += "</table>\n";
			
			// Parcel writer rights management
			page += "<h4>Parcel Writer Rights</h4>\n";
			page += "<p>Grant/revoke rights to edit this specific parcel:</p>\n";
			
			// Show current writers (clean list, no debug info)
			page += "<h5>Current Writers:</h5>\n";
			if(parcel->writer_ids.empty())
			{
				page += "<ul><li>None</li></ul>\n";
			}
			else
			{
				page += "<ul>\n";
				for(size_t i = 0; i < parcel->writer_ids.size(); ++i)
				{
					auto user_it = world_state.user_id_to_users.find(parcel->writer_ids[i]);
					if(user_it != world_state.user_id_to_users.end())
					{
						page += "<li>" + web::Escaping::HTMLEscape(user_it->second->name) + "</li>\n";
					}
					else
					{
						page += "<li>Unknown user (ID: " + toString(parcel->writer_ids[i].value()) + ")</li>\n";
					}
				}
				page += "</ul>\n";
			}
			
			page += "<form action=\"/world_grant_parcel_writer_post\" method=\"post\">\n";
			page += "<input type=\"hidden\" name=\"world_name\" value=\"" + web::Escaping::HTMLEscape(world_name) + "\">\n";
			page += "<input type=\"hidden\" name=\"parcel_id\" value=\"" + parcel->id.toString() + "\">\n";
			page += "<input type=\"text\" name=\"username\" placeholder=\"Username\" required>\n";
			page += "<input type=\"submit\" value=\"Grant Writer Rights\">\n";
			page += "</form>\n";
			page += "<form action=\"/world_revoke_parcel_writer_post\" method=\"post\">\n";
			page += "<input type=\"hidden\" name=\"world_name\" value=\"" + web::Escaping::HTMLEscape(world_name) + "\">\n";
			page += "<input type=\"hidden\" name=\"parcel_id\" value=\"" + parcel->id.toString() + "\">\n";
			page += "<input type=\"text\" name=\"username\" placeholder=\"Username\" required>\n";
			page += "<input type=\"submit\" value=\"Revoke Writer Rights\">\n";
			page += "</form>\n";
			
			
			page += "</div>\n";
		}
		
		if(parcel_count == 0) {
			page += "<p>No parcels found.</p>\n";
		}
		
		// Parcel management forms for world owner
		if(logged_in_user && world->details.owner_id == logged_in_user->id)
		{
			// Add new parcel form
			page += "<div style=\"border: 1px solid #17a2b8; margin: 15px 0; padding: 15px; background: #e7f3ff; border-radius: 5px;\">\n";
			page += "<h3>➕ Add New Parcel</h3>\n";
			page += "<form action=\"/world_add_parcel_post\" method=\"post\">\n";
			page += "<input type=\"hidden\" name=\"world_name\" value=\"" + web::Escaping::HTMLEscape(world_name) + "\">\n";
			page += "<table style=\"width: 100%;\">\n";
			page += "<tr><td style=\"width: 120px;\">X Position:</td><td><input type=\"number\" name=\"x\" step=\"0.1\" value=\"0\" required style=\"width: 100px;\"></td></tr>\n";
			page += "<tr><td>Y Position:</td><td><input type=\"number\" name=\"y\" step=\"0.1\" value=\"0\" required style=\"width: 100px;\"></td></tr>\n";
			page += "<tr><td>Z Position:</td><td><input type=\"number\" name=\"z\" step=\"0.1\" value=\"0\" required style=\"width: 100px;\"></td></tr>\n";
			page += "<tr><td>X Size:</td><td><input type=\"number\" name=\"width\" step=\"0.1\" min=\"0.1\" value=\"10\" required style=\"width: 100px;\"></td></tr>\n";
			page += "<tr><td>Y Size:</td><td><input type=\"number\" name=\"height\" step=\"0.1\" min=\"0.1\" value=\"10\" required style=\"width: 100px;\"></td></tr>\n";
			page += "<tr><td>Z Height:</td><td><input type=\"number\" name=\"zheight\" step=\"0.1\" min=\"0.1\" value=\"5\" required style=\"width: 100px;\"></td></tr>\n";
			page += "<tr><td colspan=\"2\"><input type=\"submit\" value=\"➕ Add Parcel\" style=\"margin-top: 10px; padding: 8px 15px; background: #28a745; color: white; border: none; border-radius: 3px; cursor: pointer;\"></td></tr>\n";
			page += "</table>\n";
			page += "</form>\n";
			page += "</div>\n";
			
			// Edit parcel size form (only if parcels exist)
			if(parcel_count > 0)
			{
				page += "<div style=\"border: 1px solid #ffc107; margin: 15px 0; padding: 15px; background: #fff8e1; border-radius: 5px;\">\n";
				page += "<h3>✏️ Edit Parcel Size</h3>\n";
				page += "<form action=\"/world_update_parcel_size_post\" method=\"post\">\n";
				page += "<input type=\"hidden\" name=\"world_name\" value=\"" + web::Escaping::HTMLEscape(world_name) + "\">\n";
				page += "<table style=\"width: 100%;\">\n";
				page += "<tr><td style=\"width: 120px;\">Parcel ID:</td><td><select name=\"parcel_id\" id=\"edit-parcel-select\" required style=\"width: 100px;\">\n";
				
				// Add parcel options
				for(auto it = world->parcels.begin(); it != world->parcels.end(); ++it) {
					const Parcel* parcel = it->second.ptr();
					// Only show parcels that are not the base parcel in personal worlds
					// Do not include grand parcel (ID=1) in edit list
					if(!(parcel->id.value() == 1)) {
						const Vec2d origin = parcel->verts[0];
						const Vec2d topRight = parcel->verts[2];
						const Vec2d size = topRight - origin;
						const double zheight = parcel->zbounds.y - parcel->zbounds.x;
						page += "<option value=\"" + parcel->id.toString() + "\" ";
						page += "data-x=\"" + doubleToStringMaxNDecimalPlaces(origin.x, 3) + "\" ";
						page += "data-y=\"" + doubleToStringMaxNDecimalPlaces(origin.y, 3) + "\" ";
						page += "data-z=\"" + doubleToStringMaxNDecimalPlaces(parcel->zbounds.x, 3) + "\" ";
						page += "data-width=\"" + doubleToStringMaxNDecimalPlaces(size.x, 3) + "\" ";
						page += "data-height=\"" + doubleToStringMaxNDecimalPlaces(size.y, 3) + "\" ";
						page += "data-zheight=\"" + doubleToStringMaxNDecimalPlaces(zheight, 3) + "\">";
						page += parcel->id.toString() + "</option>\n";
					}
				}
				
				page += "</select></td></tr>\n";
				page += "<tr><td>X Position:</td><td><input type=\"number\" name=\"x\" id=\"edit-x\" step=\"0.1\" required style=\"width: 100px;\"></td></tr>\n";
				page += "<tr><td>Y Position:</td><td><input type=\"number\" name=\"y\" id=\"edit-y\" step=\"0.1\" required style=\"width: 100px;\"></td></tr>\n";
				page += "<tr><td>Z Position:</td><td><input type=\"number\" name=\"z\" id=\"edit-z\" step=\"0.1\" required style=\"width: 100px;\"></td></tr>\n";
				page += "<tr><td>X Size:</td><td><input type=\"number\" name=\"width\" id=\"edit-width\" step=\"0.1\" min=\"0.1\" required style=\"width: 100px;\"></td></tr>\n";
				page += "<tr><td>Y Size:</td><td><input type=\"number\" name=\"height\" id=\"edit-height\" step=\"0.1\" min=\"0.1\" required style=\"width: 100px;\"></td></tr>\n";
				page += "<tr><td>Z Height:</td><td><input type=\"number\" name=\"zheight\" id=\"edit-zheight\" step=\"0.1\" min=\"0.1\" required style=\"width: 100px;\"></td></tr>\n";
				page += "<tr><td colspan=\"2\"><input type=\"submit\" value=\"✏️ Update Parcel Size\" style=\"margin-top: 10px; padding: 8px 15px; background: #ffc107; color: #000; border: none; border-radius: 3px; cursor: pointer;\"></td></tr>\n";
				page += "</table>\n";
				page += "</form>\n";
				// JS to auto-fill fields from selected option data
				page += "<script>\n";
				page += "(function(){\n";
				page += "  var sel=document.getElementById('edit-parcel-select');\n";
				page += "  var x=document.getElementById('edit-x');\n";
				page += "  var y=document.getElementById('edit-y');\n";
				page += "  var z=document.getElementById('edit-z');\n";
				page += "  var w=document.getElementById('edit-width');\n";
				page += "  var h=document.getElementById('edit-height');\n";
				page += "  var zh=document.getElementById('edit-zheight');\n";
				page += "  function fill(){ var o=sel.options[sel.selectedIndex]; if(!o) return; x.value=o.getAttribute('data-x'); y.value=o.getAttribute('data-y'); z.value=o.getAttribute('data-z'); w.value=o.getAttribute('data-width'); h.value=o.getAttribute('data-height'); zh.value=o.getAttribute('data-zheight'); }\n";
				page += "  sel.addEventListener('change', fill);\n";
				page += "  fill();\n"; // initial fill with current selection
				page += "})();\n";
				page += "</script>\n";
				page += "</div>\n";
			}
		}
		page += "</div>\n"; // Close parcels section
			
		} // end lock scope

		page += "</div>\n";
		
		// Add JavaScript for app link fallback
		page += "<script>\n";
		page += "(function(){\n";
		page += "  const appLinks = document.querySelectorAll('a[data-app-href]');\n";
		page += "  appLinks.forEach(function(appLink) {\n";
		page += "    appLink.addEventListener('click', function(e) {\n";
		page += "      e.preventDefault();\n";
		page += "      const url = appLink.getAttribute('data-app-href') || appLink.href;\n";
		page += "      console.log('Trying to open app with URL:', url);\n";
		page += "      alert('Попытка открыть приложение: ' + url);\n";
		page += "      let done = false;\n";
		page += "      const t = setTimeout(function() {\n";
		page += "        if (done) return;\n";
		page += "        alert('Метасиберия-приложение не обнаружено. Установите приложение или зарегистрируйте протокол sub://');\n";
		page += "      }, 800);\n";
		page += "      try {\n";
		page += "        window.location.href = url;\n";
		page += "        done = true;\n";
		page += "        clearTimeout(t);\n";
		page += "      } catch(_) {\n";
		page += "        const ifr = document.createElement('iframe');\n";
		page += "        ifr.style.display = 'none';\n";
		page += "        ifr.src = url;\n";
		page += "        document.body.appendChild(ifr);\n";
		page += "        setTimeout(() => { document.body.removeChild(ifr); }, 2000);\n";
		page += "      }\n";
		page += "    });\n";
		page += "  });\n";
		page += "})();\n";
		page += "</script>\n";
		
		// Close main container
		page += "</div>\n";
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

void handleWorldDeletePost(ServerAllWorldsState& world_state, const web::RequestInfo& request, web::ReplyInfo& reply_info)
{
    try
    {
        if(world_state.isInReadOnlyMode())
            throw glare::Exception("Server is in read-only mode, editing disabled currently.");
            
        const std::string world_name = request.getPostField("world_name").str();
        
        { // Lock scope
            Lock lock(world_state.mutex);
            
            // Find world
            auto world_res = world_state.world_states.find(world_name);
            if(world_res == world_state.world_states.end())
                throw glare::Exception("World not found");
                
            ServerWorldState* world = world_res->second.ptr();
            
            // Check if user is world owner
            const User* logged_in_user = LoginHandlers::getLoggedInUser(world_state, request);
            if(!logged_in_user)
                throw glare::Exception("Must be logged in");
                
            if(world->details.owner_id != logged_in_user->id)
                throw glare::Exception("Only world owner can delete the world");
            
            // Don't allow deletion of personal worlds (single name like "denshipilov")
            if(world_name.find('/') == std::string::npos)
                throw glare::Exception("Cannot delete personal worlds");
            
            // Queue persistent DB records for deletion so the world does not reappear after restart
            if(world->database_key.valid())
                world_state.db_records_to_delete.insert(world->database_key);
            if(world->world_settings.database_key.valid())
                world_state.db_records_to_delete.insert(world->world_settings.database_key);

            // Delete the world (in-memory)
            world_state.world_states.erase(world_res);
            world_state.markAsChanged();
            
            conPrint("DEBUG: Deleted world: " + world_name);
        }
        
        web::ResponseUtils::writeRedirectTo(reply_info, "/account");
    }
    catch(glare::Exception& e)
    {
        if(!request.fuzzing)
            conPrint("handleWorldDeletePost error: " + e.what());
        web::ResponseUtils::writeHTTPOKHeaderAndData(reply_info, "Error: " + e.what());
    }
}

void handleWorldUpdateParcelSizePost(ServerAllWorldsState& world_state, const web::RequestInfo& request, web::ReplyInfo& reply_info)
{
    try
    {
        if(world_state.isInReadOnlyMode())
            throw glare::Exception("Server is in read-only mode, editing disabled currently.");
            
        const std::string world_name = request.getPostField("world_name").str();
        const ParcelID parcel_id(request.getPostIntField("parcel_id"));
        const double x = stringToDouble(request.getPostField("x").str());
        const double y = stringToDouble(request.getPostField("y").str());
        const double z = stringToDouble(request.getPostField("z").str());
        const double width = stringToDouble(request.getPostField("width").str());
        const double height = stringToDouble(request.getPostField("height").str());
        const double zheight = stringToDouble(request.getPostField("zheight").str());
        
        { // Lock scope
            Lock lock(world_state.mutex);
            
            // Find world
            auto world_res = world_state.world_states.find(world_name);
            if(world_res == world_state.world_states.end())
                throw glare::Exception("World not found");
                
            ServerWorldState* world = world_res->second.ptr();
            
            // Check if user is world owner
            const User* logged_in_user = LoginHandlers::getLoggedInUser(world_state, request);
            if(!logged_in_user)
                throw glare::Exception("Must be logged in");
                
            if(world->details.owner_id != logged_in_user->id)
                throw glare::Exception("Only world owner can update parcel size");
            
            // Find parcel
            auto parcel_res = world->parcels.find(parcel_id);
            if(parcel_res == world->parcels.end())
                throw glare::Exception("Parcel not found");
                
            Parcel* parcel = parcel_res->second.ptr();
            
            // Update parcel position and size
            parcel->verts[0] = Vec2d(x, y);
            parcel->verts[1] = Vec2d(x + width, y);
            parcel->verts[2] = Vec2d(x + width, y + height);
            parcel->verts[3] = Vec2d(x, y + height);
            parcel->zbounds = Vec2d(z, z + zheight);
            parcel->build();
            
            WorldStateLock world_lock(world_state.mutex);
            world->addParcelAsDBDirty(parcel, world_lock);
            
            conPrint("DEBUG: Updated parcel " + parcel_id.toString() + " size: " + toString(width) + "x" + toString(height) + "x" + toString(zheight));
        }
        
        web::ResponseUtils::writeRedirectTo(reply_info, "/world/" + URLEscapeWorldName(world_name));
    }
    catch(glare::Exception& e)
    {
        if(!request.fuzzing)
            conPrint("handleWorldUpdateParcelSizePost error: " + e.what());
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


void handleWorldGrantParcelWriterPost(ServerAllWorldsState& world_state, const web::RequestInfo& request, web::ReplyInfo& reply_info)
{
    try
    {
        if(world_state.isInReadOnlyMode())
            throw glare::Exception("Server is in read-only mode, editing disabled currently.");
            
        const std::string world_name = request.getPostField("world_name").str();
        const ParcelID parcel_id(request.getPostIntField("parcel_id"));
        const std::string username = request.getPostField("username").str();
        
        { // Lock scope
            Lock lock(world_state.mutex);
            
            // Find world
            auto world_res = world_state.world_states.find(world_name);
            if(world_res == world_state.world_states.end())
                throw glare::Exception("World not found");
                
            ServerWorldState* world = world_res->second.ptr();
            
            // Check if user is world owner or editor
            const User* logged_in_user = LoginHandlers::getLoggedInUser(world_state, request);
            if(!logged_in_user)
                throw glare::Exception("Must be logged in");
                
            bool can_manage = (world->details.owner_id == logged_in_user->id);
            if(!can_manage)
                throw glare::Exception("Only world owner can grant parcel writer rights");
            
            // Find parcel
            auto parcel_res = world->parcels.find(parcel_id);
            if(parcel_res == world->parcels.end())
                throw glare::Exception("Parcel not found");
                
            Parcel* parcel = parcel_res->second.ptr();
            
            // Find target user
            User* target_user = nullptr;
            for(auto it = world_state.user_id_to_users.begin(); it != world_state.user_id_to_users.end(); ++it)
            {
                if(it->second->name == username)
                {
                    target_user = it->second.ptr();
                    break;
                }
            }
            if(!target_user)
                throw glare::Exception("User not found: " + username);
            
            // Add to writer list (if not already there)
            bool already_writer = false;
            for(size_t i = 0; i < parcel->writer_ids.size(); ++i)
            {
                if(parcel->writer_ids[i] == target_user->id)
                {
                    already_writer = true;
                    break;
                }
            }
            
            if(!already_writer)
            {
                parcel->writer_ids.push_back(target_user->id);
                WorldStateLock world_lock(world_state.mutex);
                world->addParcelAsDBDirty(parcel, world_lock);
                world_state.denormaliseData();
                world_state.markAsChanged();
            }
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
        const bool force_revoke = request.getPostField("force_revoke").str() == "true";
        
        { // Lock scope
            Lock lock(world_state.mutex);
            
            // Find world
            auto world_res = world_state.world_states.find(world_name);
            if(world_res == world_state.world_states.end())
                throw glare::Exception("World not found");
                
            ServerWorldState* world = world_res->second.ptr();
            
            // Check if user is world owner or editor
            const User* logged_in_user = LoginHandlers::getLoggedInUser(world_state, request);
            if(!logged_in_user)
                throw glare::Exception("Must be logged in");
                
            bool can_manage = (world->details.owner_id == logged_in_user->id);
            if(!can_manage && !force_revoke)
                throw glare::Exception("Only world owner can revoke parcel writer rights");
            
            // Find parcel
            auto parcel_res = world->parcels.find(parcel_id);
            if(parcel_res == world->parcels.end())
                throw glare::Exception("Parcel not found");
                
            Parcel* parcel = parcel_res->second.ptr();
            
            // Find target user
            User* target_user = nullptr;
            for(auto it = world_state.user_id_to_users.begin(); it != world_state.user_id_to_users.end(); ++it)
            {
                if(it->second->name == username)
                {
                    target_user = it->second.ptr();
                    break;
                }
            }
            if(!target_user)
                throw glare::Exception("User not found: " + username);
            
            // Remove from writer list
            bool found = false;
            for(size_t i = 0; i < parcel->writer_ids.size(); ++i)
            {
                if(parcel->writer_ids[i] == target_user->id)
                {
                    parcel->writer_ids.erase(parcel->writer_ids.begin() + i);
                    WorldStateLock world_lock(world_state.mutex);
                    world->addParcelAsDBDirty(parcel, world_lock);
                    world_state.denormaliseData();
                    world_state.markAsChanged();
                    conPrint("DEBUG: Removed user " + username + " (ID: " + toString(target_user->id.value()) + ") from parcel " + parcel_id.toString() + " writers. Remaining writers: " + toString(parcel->writer_ids.size()));
                    found = true;
                    break;
                }
            }
            if(!found)
            {
                conPrint("DEBUG: User " + username + " (ID: " + toString(target_user->id.value()) + ") not found in parcel " + parcel_id.toString() + " writers. Total writers: " + toString(parcel->writer_ids.size()));
            }
            
            // Set success message
            if(found)
            {
                world_state.setUserWebMessage(logged_in_user->id, "Successfully revoked writer rights from '" + username + "'" + (force_revoke ? " (FORCED)" : ""));
            }
            else
            {
                world_state.setUserWebMessage(logged_in_user->id, "User '" + username + "' was not found in parcel writers" + (force_revoke ? " (FORCED)" : ""));
            }
        }
        
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
} // end namespace WorldHandlers
