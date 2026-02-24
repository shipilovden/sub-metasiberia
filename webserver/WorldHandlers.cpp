/*=====================================================================
WorldHandlers.cpp
-----------------
Copyright Glare Technologies Limited 2025 -
=====================================================================*/
#include "WorldHandlers.h"


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
#include <TestUtils.h>
#include <ContainerUtils.h>
#include <algorithm>
#include <cctype>


namespace WorldHandlers
{


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


static bool getUserIDByName(ServerAllWorldsState& world_state, const std::string& username, UserID& user_id_out)
{
	const auto res = world_state.name_to_users.find(username);
	if(res == world_state.name_to_users.end())
		return false;

	user_id_out = res->second->id;
	return true;
}


static bool isAllDigitsString(const std::string& s)
{
	if(s.empty())
		return false;
	for(size_t i=0; i<s.size(); ++i)
		if(!::isdigit((unsigned char)s[i]))
			return false;
	return true;
}


static bool getUserIDByRef(ServerAllWorldsState& world_state, const std::string& user_ref, UserID& user_id_out, std::string& error_out)
{
	const std::string trimmed = stripHeadAndTailWhitespace(user_ref);
	if(trimmed.empty())
	{
		error_out = "User reference is empty.";
		return false;
	}

	if(isAllDigitsString(trimmed))
	{
		const UserID candidate((uint32)stringToUInt64(trimmed));
		const auto user_it = world_state.user_id_to_users.find(candidate);
		if(user_it == world_state.user_id_to_users.end())
		{
			error_out = "Could not find user with id '" + trimmed + "'.";
			return false;
		}
		user_id_out = candidate;
		return true;
	}

	if(!getUserIDByName(world_state, trimmed, user_id_out))
	{
		error_out = "Could not find user '" + trimmed + "'.";
		return false;
	}
	return true;
}


static bool parseCommaSeparatedUserRefsToIDs(ServerAllWorldsState& world_state, const std::string& user_refs_csv, std::vector<UserID>& user_ids_out, std::string& error_out)
{
	const std::vector<std::string> user_ref_fields = ::split(user_refs_csv, ',');
	for(size_t i=0; i<user_ref_fields.size(); ++i)
	{
		const std::string user_ref = stripHeadAndTailWhitespace(user_ref_fields[i]);
		if(user_ref.empty())
			continue;

		UserID user_id;
		if(!getUserIDByRef(world_state, user_ref, user_id, error_out))
			return false;

		if(!ContainerUtils::contains(user_ids_out, user_id))
			user_ids_out.push_back(user_id);
	}

	return true;
}


static double parseDoubleFieldForParcelCreation(const web::RequestInfo& request, const std::string& field_name)
{
	std::string s = stripHeadAndTailWhitespace(request.getPostField(field_name).str());
	if(s.empty())
		throw glare::Exception("Missing field '" + field_name + "'.");
	std::replace(s.begin(), s.end(), ',', '.');
	return stringToDouble(s);
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
			WorldStateLock lock(world_state.mutex);

			auto res = world_state.world_states.find(world_name);
			if(res == world_state.world_states.end())
				throw glare::Exception("Couldn't find world");

			ServerWorldState* world = res->second.ptr();

			User* logged_in_user = LoginHandlers::getLoggedInUser(world_state, request);
			const bool logged_in_user_is_world_owner = logged_in_user && (world->details.owner_id == logged_in_user->id); // If the user is logged in and created this world:
			const bool logged_in_user_is_superadmin = logged_in_user && isGodUser(logged_in_user->id);

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
			const std::string webclient_URL = (request.tls_connection ? std::string("https") : std::string("http")) + "://" + hostname + "/visit?world=" + URLEscapeWorldName(world_name);
			page += "<p>Visit in web browser: <a href=\"" + webclient_URL + "\">" + web::Escaping::HTMLEscape(webclient_URL) + "</a></p>";

			const std::string native_URL = "sub://" + hostname + "/" + world_name;
			page += "<p>Visit in native app: <a href=\"" + native_URL + "\">" + web::Escaping::HTMLEscape(native_URL) + "</a></p>";

			if(logged_in_user_is_world_owner || logged_in_user_is_superadmin) // Show edit link if the user owns this world or is superadmin.
			{
				page += "<br/><br/><div><a href=\"/edit_world/" + URLEscapeWorldName(world_name) + "\">Edit world</a></div>";
			}

			struct WorldParcelRow
			{
				ParcelRef parcel;
				std::string owner_username;
				double size_x = 0.0;
				double size_y = 0.0;
				bool logged_user_is_owner = false;
				bool logged_user_is_editor = false;
			};

			std::vector<WorldParcelRow> parcel_rows;
			parcel_rows.reserve(world->getParcels(lock).size());
			int logged_user_owned_parcel_count = 0;
			int logged_user_editable_parcel_count = 0;
			ParcelRef most_recent_parcel;
			uint32 max_parcel_id = 0;

			for(auto parcel_it = world->getParcels(lock).begin(); parcel_it != world->getParcels(lock).end(); ++parcel_it)
			{
				ParcelRef parcel = parcel_it->second;
				WorldParcelRow row;
				row.parcel = parcel;

				const auto owner_res = world_state.user_id_to_users.find(parcel->owner_id);
				row.owner_username = (owner_res == world_state.user_id_to_users.end()) ? std::string("[No user found]") : owner_res->second->name;

				const Vec3d span = parcel->aabb_max - parcel->aabb_min;
				row.size_x = span.x;
				row.size_y = span.y;

				if(logged_in_user)
				{
					row.logged_user_is_owner = (parcel->owner_id == logged_in_user->id);
					for(size_t i=0; i<parcel->writer_ids.size(); ++i)
					{
						if(parcel->writer_ids[i] == logged_in_user->id)
						{
							row.logged_user_is_editor = true;
							break;
						}
					}

					if(row.logged_user_is_owner)
						logged_user_owned_parcel_count++;
					else if(row.logged_user_is_editor)
						logged_user_editable_parcel_count++;
				}

				if(parcel->id.valid() && (parcel->id.value() >= max_parcel_id))
				{
					max_parcel_id = parcel->id.value();
					most_recent_parcel = parcel;
				}

				parcel_rows.push_back(row);
			}

			double suggested_origin_x = 0.0;
			double suggested_origin_y = 0.0;
			double suggested_size_x = 16.0;
			double suggested_size_y = 16.0;
			double suggested_min_z = -1.0;
			double suggested_max_z = 4.0;
			if(most_recent_parcel)
			{
				const Vec3d most_recent_span = most_recent_parcel->aabb_max - most_recent_parcel->aabb_min;
				suggested_origin_x = most_recent_parcel->aabb_min.x + 32.0;
				suggested_origin_y = most_recent_parcel->aabb_min.y;
				suggested_size_x = std::max(1.0, most_recent_span.x);
				suggested_size_y = std::max(1.0, most_recent_span.y);
				suggested_min_z = most_recent_parcel->zbounds.x;
				suggested_max_z = most_recent_parcel->zbounds.y;
			}

			page += "<br/><section class=\"grouped-region\">";
			page += "<h3>Parcels in this world</h3>";
			page += "<div class=\"msb-kpi-grid\">";
			page += "<div class=\"msb-kpi\"><div class=\"msb-kpi-label\">Parcels total</div><div class=\"msb-kpi-value\">" + toString((int)parcel_rows.size()) + "</div></div>";
			if(logged_in_user)
			{
				page += "<div class=\"msb-kpi\"><div class=\"msb-kpi-label\">Your parcels (owner)</div><div class=\"msb-kpi-value\">" + toString(logged_user_owned_parcel_count) + "</div></div>";
				page += "<div class=\"msb-kpi\"><div class=\"msb-kpi-label\">Your parcels (editor)</div><div class=\"msb-kpi-value\">" + toString(logged_user_editable_parcel_count) + "</div></div>";
			}
			page += "</div>";

			if(logged_in_user_is_superadmin)
			{
				page += "<div class=\"field-description\">Superadmin mode: you can create parcels, assign owner/editors, and delete parcels in this world.</div>";
				page += "<form action=\"/create_world_parcel_post\" method=\"post\" class=\"msb-create-parcel-form\" data-admin-create-parcel-form=\"1\">";
				page += "<input type=\"hidden\" name=\"world_name\" value=\"" + web::Escaping::HTMLEscape(world_name) + "\">";
				page += "<label for=\"new-world-parcel-owner\">Owner (id or username)</label>";
				page += "<input id=\"new-world-parcel-owner\" type=\"text\" name=\"owner_username\" value=\"" + web::Escaping::HTMLEscape(owner_username) + "\" title=\"User id or username that will own the new parcel\">";
				page += "<label for=\"new-world-parcel-editors\">Editors (ids or usernames, comma-separated)</label>";
				page += "<input id=\"new-world-parcel-editors\" type=\"text\" name=\"writer_usernames\" value=\"\" title=\"Comma-separated list of user ids or usernames that can edit objects in the parcel\">";
				page += "<div class=\"msb-inline-fields\">";
				page += "<label for=\"new-world-origin-x\">Origin X</label>";
				page += "<input id=\"new-world-origin-x\" type=\"text\" name=\"origin_x\" value=\"" + doubleToStringMaxNDecimalPlaces(suggested_origin_x, 2) + "\" title=\"Parcel bottom-left X coordinate\">";
				page += "<label for=\"new-world-origin-y\">Origin Y</label>";
				page += "<input id=\"new-world-origin-y\" type=\"text\" name=\"origin_y\" value=\"" + doubleToStringMaxNDecimalPlaces(suggested_origin_y, 2) + "\" title=\"Parcel bottom-left Y coordinate\">";
				page += "<label for=\"new-world-size-x\">Size X</label>";
				page += "<input id=\"new-world-size-x\" type=\"text\" name=\"size_x\" value=\"" + doubleToStringMaxNDecimalPlaces(suggested_size_x, 2) + "\" title=\"Parcel width on X axis\">";
				page += "<label for=\"new-world-size-y\">Size Y</label>";
				page += "<input id=\"new-world-size-y\" type=\"text\" name=\"size_y\" value=\"" + doubleToStringMaxNDecimalPlaces(suggested_size_y, 2) + "\" title=\"Parcel width on Y axis\">";
				page += "</div>";
				page += "<div class=\"msb-inline-fields\">";
				page += "<label for=\"new-world-min-z\">Min Z</label>";
				page += "<input id=\"new-world-min-z\" type=\"text\" name=\"min_z\" value=\"" + doubleToStringMaxNDecimalPlaces(suggested_min_z, 2) + "\" title=\"Minimum parcel build height\">";
				page += "<label for=\"new-world-max-z\">Max Z</label>";
				page += "<input id=\"new-world-max-z\" type=\"text\" name=\"max_z\" value=\"" + doubleToStringMaxNDecimalPlaces(suggested_max_z, 2) + "\" title=\"Maximum parcel build height\">";
				page += "</div>";
				page += "<div class=\"msb-live-preview\" data-parcel-live-preview=\"1\">";
				page += "<div class=\"msb-live-item\">Area: <b data-preview-area>256.0</b> m<sup>2</sup></div>";
				page += "<div class=\"msb-live-item\">Height: <b data-preview-height>5.0</b> m</div>";
				page += "<div class=\"msb-live-item\">Build volume: <b data-preview-volume>1280.0</b> m<sup>3</sup></div>";
				page += "</div>";
				page += "<input type=\"submit\" value=\"Create parcel in this world\" title=\"Create a new parcel in this world and assign owner/editors\" onclick=\"return confirm('Create parcel with these settings in this world?');\" >";
				page += "</form>";
			}
			else if(logged_in_user)
			{
				page += "<div class=\"field-description\">Only superadmin can create parcels in worlds. Parcel owners can manage editors of their parcels below.</div>";
			}
			else
			{
				page += "<div class=\"field-description\">Log in to view your parcel role. Only superadmin can create parcels in worlds.</div>";
			}

			if(parcel_rows.empty())
			{
				page += "<div class=\"field-description\">No parcels created in this world yet.</div>";
			}
			else
			{
				page += "<div class=\"msb-table-wrap\">";
				page += "<table class=\"msb-table\" aria-label=\"World parcels table\">";
				page += "<thead><tr><th>ID</th><th>Owner</th><th>Size</th><th>Z-bounds</th><th>Editors</th><th>Created</th><th>Your role</th><th>Actions</th></tr></thead><tbody>";
				for(size_t i=0; i<parcel_rows.size(); ++i)
				{
					const WorldParcelRow& row = parcel_rows[i];
					const Parcel* parcel = row.parcel.ptr();

					std::string writer_refs_initial;
					for(size_t z=0; z<parcel->writer_ids.size(); ++z)
					{
						const auto writer_res = world_state.user_id_to_users.find(parcel->writer_ids[z]);
						if(z > 0)
							writer_refs_initial += ", ";
						if(writer_res != world_state.user_id_to_users.end())
							writer_refs_initial += writer_res->second->name;
						else
							writer_refs_initial += parcel->writer_ids[z].toString();
					}

					std::string role_desc = "-";
					if(row.logged_user_is_owner)
						role_desc = "owner";
					else if(row.logged_user_is_editor)
						role_desc = "editor";
					const double origin_x = parcel->aabb_min.x;
					const double origin_y = parcel->aabb_min.y;

					page += "<tr>";
					page += "<td>" + parcel->id.toString() + "</td>";
					page += "<td>" + web::Escaping::HTMLEscape(row.owner_username) + "</td>";
					page += "<td>" + doubleToStringMaxNDecimalPlaces(row.size_x, 1) + " x " + doubleToStringMaxNDecimalPlaces(row.size_y, 1) + " m</td>";
					page += "<td>" + doubleToStringMaxNDecimalPlaces(parcel->zbounds.x, 1) + " .. " + doubleToStringMaxNDecimalPlaces(parcel->zbounds.y, 1) + " m</td>";
					page += "<td>" + toString((int)parcel->writer_ids.size()) + "</td>";
					page += "<td>" + parcel->created_time.timeAgoDescription() + "</td>";
					page += "<td>" + role_desc + "</td>";
					page += "<td><div class=\"msb-row-actions\">";
					if(logged_in_user_is_superadmin)
					{
						page += "<form action=\"/set_world_parcel_geometry_post\" method=\"post\" class=\"msb-create-parcel-form msb-inline-form\" data-admin-create-parcel-form=\"1\">";
						page += "<input type=\"hidden\" name=\"world_name\" value=\"" + web::Escaping::HTMLEscape(world_name) + "\">";
						page += "<input type=\"hidden\" name=\"parcel_id\" value=\"" + parcel->id.toString() + "\">";
						page += "<div class=\"msb-inline-fields\">";
						page += "<label>Origin X</label>";
						page += "<input type=\"text\" name=\"origin_x\" value=\"" + doubleToStringMaxNDecimalPlaces(origin_x, 2) + "\" title=\"Parcel bottom-left X coordinate\">";
						page += "<label>Origin Y</label>";
						page += "<input type=\"text\" name=\"origin_y\" value=\"" + doubleToStringMaxNDecimalPlaces(origin_y, 2) + "\" title=\"Parcel bottom-left Y coordinate\">";
						page += "<label>Size X</label>";
						page += "<input type=\"text\" name=\"size_x\" value=\"" + doubleToStringMaxNDecimalPlaces(row.size_x, 2) + "\" title=\"Parcel width on X axis\">";
						page += "<label>Size Y</label>";
						page += "<input type=\"text\" name=\"size_y\" value=\"" + doubleToStringMaxNDecimalPlaces(row.size_y, 2) + "\" title=\"Parcel width on Y axis\">";
						page += "</div>";
						page += "<div class=\"msb-inline-fields\">";
						page += "<label>Min Z</label>";
						page += "<input type=\"text\" name=\"min_z\" value=\"" + doubleToStringMaxNDecimalPlaces(parcel->zbounds.x, 2) + "\" title=\"Minimum parcel build height\">";
						page += "<label>Max Z</label>";
						page += "<input type=\"text\" name=\"max_z\" value=\"" + doubleToStringMaxNDecimalPlaces(parcel->zbounds.y, 2) + "\" title=\"Maximum parcel build height\">";
						page += "</div>";
						page += "<div class=\"msb-live-preview\" data-parcel-live-preview=\"1\">";
						page += "<div class=\"msb-live-item\">Area: <b data-preview-area>0.0</b> m<sup>2</sup></div>";
						page += "<div class=\"msb-live-item\">Height: <b data-preview-height>0.0</b> m</div>";
						page += "<div class=\"msb-live-item\">Build volume: <b data-preview-volume>0.0</b> m<sup>3</sup></div>";
						page += "</div>";
						page += "<input type=\"submit\" value=\"Save geometry\" onclick=\"return confirm('Update geometry for world parcel " + parcel->id.toString() + "?');\" >";
						page += "</form>";

						page += "<form action=\"/set_world_parcel_owner_post\" method=\"post\" class=\"msb-inline-form\">";
						page += "<input type=\"hidden\" name=\"world_name\" value=\"" + web::Escaping::HTMLEscape(world_name) + "\">";
						page += "<input type=\"hidden\" name=\"parcel_id\" value=\"" + parcel->id.toString() + "\">";
						page += "<input type=\"text\" name=\"new_owner_ref\" value=\"" + web::Escaping::HTMLEscape(row.owner_username) + "\" placeholder=\"owner id/name\">";
						page += "<input type=\"submit\" value=\"Set owner\" onclick=\"return confirm('Set owner for world parcel " + parcel->id.toString() + "?');\" >";
						page += "</form>";
					}

					if(logged_in_user && (logged_in_user_is_superadmin || row.logged_user_is_owner))
					{
						page += "<form action=\"/set_world_parcel_writers_post\" method=\"post\" class=\"msb-inline-form\">";
						page += "<input type=\"hidden\" name=\"world_name\" value=\"" + web::Escaping::HTMLEscape(world_name) + "\">";
						page += "<input type=\"hidden\" name=\"parcel_id\" value=\"" + parcel->id.toString() + "\">";
						page += "<input type=\"text\" name=\"writer_refs\" value=\"" + web::Escaping::HTMLEscape(writer_refs_initial) + "\" placeholder=\"editors ids/names\">";
						page += "<input type=\"submit\" value=\"Set editors\" onclick=\"return confirm('Set editors for world parcel " + parcel->id.toString() + "?');\" >";
						page += "</form>";
					}

					if(logged_in_user_is_superadmin)
					{
						page += "<form action=\"/admin_delete_parcel\" method=\"post\" class=\"msb-inline-form\">";
						page += "<input type=\"hidden\" name=\"world_name\" value=\"" + web::Escaping::HTMLEscape(world_name) + "\">";
						page += "<input type=\"hidden\" name=\"parcel_id\" value=\"" + parcel->id.toString() + "\">";
						page += "<input type=\"submit\" value=\"Delete\" onclick=\"return confirm('Delete world parcel " + parcel->id.toString() + "?');\" >";
						page += "</form>";
					}

					if(!logged_in_user || (!logged_in_user_is_superadmin && !row.logged_user_is_owner))
						page += "<span class=\"field-description\">-</span>";

					page += "</div></td>";
					page += "</tr>";
				}
				page += "</tbody></table></div>";
			}
			page += "</section>";

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


void handleCreateWorldParcelPost(ServerAllWorldsState& world_state, const web::RequestInfo& request, web::ReplyInfo& reply_info)
{
	try
	{
		if(world_state.isInReadOnlyMode())
			throw glare::Exception("Server is in read-only mode, editing disabled currently.");

		const std::string world_name = request.getPostField("world_name").str();

		bool redirect_to_login = false;
		bool redirect_back_to_world = false;

		{ // Lock scope
			WorldStateLock lock(world_state.mutex);

			const User* logged_in_user = LoginHandlers::getLoggedInUser(world_state, request);
			if(!logged_in_user)
			{
				redirect_to_login = true;
			}
			else
			{
				auto world_res = world_state.world_states.find(world_name);
				if(world_res == world_state.world_states.end())
					throw glare::Exception("Couldn't find world");

				ServerWorldState* world = world_res->second.ptr();

				if(!isGodUser(logged_in_user->id))
				{
					world_state.setUserWebMessage(logged_in_user->id, "Only superadmin can create parcels in worlds.");
					redirect_back_to_world = true;
				}

				if(!redirect_back_to_world)
				{
					const std::string owner_ref_field = stripHeadAndTailWhitespace(request.getPostField("owner_username").str());
					const std::string writer_refs_csv = request.getPostField("writer_usernames").str();
					double origin_x = 0.0;
					double origin_y = 0.0;
					double size_x   = 0.0;
					double size_y   = 0.0;
					double min_z    = 0.0;
					double max_z    = 0.0;

					try
					{
						origin_x = parseDoubleFieldForParcelCreation(request, "origin_x");
						origin_y = parseDoubleFieldForParcelCreation(request, "origin_y");
						size_x   = parseDoubleFieldForParcelCreation(request, "size_x");
						size_y   = parseDoubleFieldForParcelCreation(request, "size_y");
						min_z    = parseDoubleFieldForParcelCreation(request, "min_z");
						max_z    = parseDoubleFieldForParcelCreation(request, "max_z");

						if(size_x <= 0.0 || size_y <= 0.0)
							throw glare::Exception("Parcel sizes must be > 0.");
						if(max_z <= min_z)
							throw glare::Exception("Max Z must be greater than Min Z.");
					}
					catch(glare::Exception& e)
					{
						world_state.setUserWebMessage(logged_in_user->id, e.what());
						redirect_back_to_world = true;
					}

					UserID owner_id = world->details.owner_id;
					if(!owner_ref_field.empty())
					{
						std::string owner_error;
						if(!getUserIDByRef(world_state, owner_ref_field, owner_id, owner_error))
						{
							world_state.setUserWebMessage(logged_in_user->id, owner_error);
							redirect_back_to_world = true;
						}
					}

					std::vector<UserID> writer_ids;
					std::string parse_error;
					if(!parseCommaSeparatedUserRefsToIDs(world_state, writer_refs_csv, writer_ids, parse_error))
					{
						world_state.setUserWebMessage(logged_in_user->id, parse_error);
						redirect_back_to_world = true;
					}

					if(!redirect_back_to_world && !ContainerUtils::contains(writer_ids, owner_id))
						writer_ids.push_back(owner_id);

					if(!redirect_back_to_world)
					{
						uint32 max_id = 0;
						for(auto it = world->getParcels(lock).begin(); it != world->getParcels(lock).end(); ++it)
						{
							if(it->second->id.valid())
								max_id = std::max(max_id, it->second->id.value());
						}

						const ParcelID new_id(max_id + 1);

						ParcelRef parcel = new Parcel();
						parcel->id = new_id;
						parcel->owner_id = owner_id;
						parcel->admin_ids = writer_ids;
						parcel->writer_ids = writer_ids;
						parcel->created_time = TimeStamp::currentTime();
						parcel->verts[0] = Vec2d(origin_x, origin_y);
						parcel->verts[1] = Vec2d(origin_x + size_x, origin_y);
						parcel->verts[2] = Vec2d(origin_x + size_x, origin_y + size_y);
						parcel->verts[3] = Vec2d(origin_x, origin_y + size_y);
						parcel->zbounds = Vec2d(min_z, max_z);

						parcel->build();

						world->getParcels(lock)[new_id] = parcel;
						world->addParcelAsDBDirty(parcel, lock);
						world_state.markAsChanged();
						const auto owner_res = world_state.user_id_to_users.find(owner_id);
						const std::string owner_name = (owner_res != world_state.user_id_to_users.end()) ? owner_res->second->name : std::string("[unknown]");
						world_state.setUserWebMessage(logged_in_user->id, "Parcel " + new_id.toString() + " created in world '" + world_name + "' for owner '" + owner_name + "'.");
						world_state.addAdminAuditLogEntry(
							logged_in_user->id,
							logged_in_user->name,
							"Created parcel " + new_id.toString() + " in world '" + world_name + "', owner='" + owner_name + "', editors_count=" + toString(writer_ids.size()) +
							", origin=(" + doubleToStringMaxNDecimalPlaces(origin_x, 2) + ", " + doubleToStringMaxNDecimalPlaces(origin_y, 2) + "), size=(" +
							doubleToStringMaxNDecimalPlaces(size_x, 2) + ", " + doubleToStringMaxNDecimalPlaces(size_y, 2) + "), z=(" +
							doubleToStringMaxNDecimalPlaces(min_z, 2) + ", " + doubleToStringMaxNDecimalPlaces(max_z, 2) + ").");
					}
				}
			}
		} // End lock scope

		if(redirect_to_login)
			web::ResponseUtils::writeRedirectTo(reply_info, "/login");
		else
			web::ResponseUtils::writeRedirectTo(reply_info, "/world/" + URLEscapeWorldName(world_name));
	}
	catch(glare::Exception& e)
	{
		if(!request.fuzzing)
			conPrint("handleCreateWorldParcelPost error: " + e.what());
		web::ResponseUtils::writeHTTPOKHeaderAndData(reply_info, "Error: " + e.what());
	}
}


void handleSetWorldParcelGeometryPost(ServerAllWorldsState& world_state, const web::RequestInfo& request, web::ReplyInfo& reply_info)
{
	try
	{
		if(world_state.isInReadOnlyMode())
			throw glare::Exception("Server is in read-only mode, editing disabled currently.");

		const std::string world_name = request.getPostField("world_name").str();
		const ParcelID parcel_id = ParcelID(request.getPostIntField("parcel_id"));
		const std::string redirect_path = stripHeadAndTailWhitespace(request.getPostField("redirect_path").str());

		bool redirect_to_login = false;

		{ // Lock scope
			WorldStateLock lock(world_state.mutex);

			const User* logged_in_user = LoginHandlers::getLoggedInUser(world_state, request);
			if(!logged_in_user)
			{
				redirect_to_login = true;
			}
			else
			{
				auto world_res = world_state.world_states.find(world_name);
				if(world_res == world_state.world_states.end())
					throw glare::Exception("Couldn't find world");

				ServerWorldState* world = world_res->second.ptr();
				if(!isGodUser(logged_in_user->id))
				{
					world_state.setUserWebMessage(logged_in_user->id, "Only superadmin can edit world parcel geometry.");
				}
				else
				{
					auto parcel_res = world->getParcels(lock).find(parcel_id);
					if(parcel_res == world->getParcels(lock).end())
					{
						world_state.setUserWebMessage(logged_in_user->id, "Parcel " + parcel_id.toString() + " not found in world '" + world_name + "'.");
					}
					else
					{
						const double origin_x = parseDoubleFieldForParcelCreation(request, "origin_x");
						const double origin_y = parseDoubleFieldForParcelCreation(request, "origin_y");
						const double size_x   = parseDoubleFieldForParcelCreation(request, "size_x");
						const double size_y   = parseDoubleFieldForParcelCreation(request, "size_y");
						const double min_z    = parseDoubleFieldForParcelCreation(request, "min_z");
						const double max_z    = parseDoubleFieldForParcelCreation(request, "max_z");
						if(size_x <= 0.0 || size_y <= 0.0)
							throw glare::Exception("Parcel sizes must be > 0.");
						if(max_z <= min_z)
							throw glare::Exception("Max Z must be greater than Min Z.");

						ParcelRef parcel = parcel_res->second;
						parcel->verts[0] = Vec2d(origin_x, origin_y);
						parcel->verts[1] = Vec2d(origin_x + size_x, origin_y);
						parcel->verts[2] = Vec2d(origin_x + size_x, origin_y + size_y);
						parcel->verts[3] = Vec2d(origin_x, origin_y + size_y);
						parcel->zbounds = Vec2d(min_z, max_z);
						parcel->build();

						world->addParcelAsDBDirty(parcel, lock);
						world_state.markAsChanged();

						world_state.setUserWebMessage(
							logged_in_user->id,
							"Updated parcel " + parcel_id.toString() + " in world '" + world_name + "'.");
						world_state.addAdminAuditLogEntry(
							logged_in_user->id,
							logged_in_user->name,
							"Updated world parcel geometry: world='" + world_name + "', parcel=" + parcel_id.toString() +
							", origin=(" + doubleToStringMaxNDecimalPlaces(origin_x, 2) + ", " + doubleToStringMaxNDecimalPlaces(origin_y, 2) + "), size=(" +
							doubleToStringMaxNDecimalPlaces(size_x, 2) + ", " + doubleToStringMaxNDecimalPlaces(size_y, 2) + "), z=(" +
							doubleToStringMaxNDecimalPlaces(min_z, 2) + ", " + doubleToStringMaxNDecimalPlaces(max_z, 2) + ").");
					}
				}
			}
		} // End lock scope

		if(redirect_to_login)
			web::ResponseUtils::writeRedirectTo(reply_info, "/login");
		else if(!redirect_path.empty() && hasPrefix(redirect_path, "/") && !hasPrefix(redirect_path, "//"))
			web::ResponseUtils::writeRedirectTo(reply_info, redirect_path);
		else
			web::ResponseUtils::writeRedirectTo(reply_info, "/world/" + URLEscapeWorldName(world_name));
	}
	catch(glare::Exception& e)
	{
		if(!request.fuzzing)
			conPrint("handleSetWorldParcelGeometryPost error: " + e.what());
		web::ResponseUtils::writeHTTPOKHeaderAndData(reply_info, "Error: " + e.what());
	}
}


void handleSetWorldParcelOwnerPost(ServerAllWorldsState& world_state, const web::RequestInfo& request, web::ReplyInfo& reply_info)
{
	try
	{
		if(world_state.isInReadOnlyMode())
			throw glare::Exception("Server is in read-only mode, editing disabled currently.");

		const std::string world_name = request.getPostField("world_name").str();
		const ParcelID parcel_id = ParcelID(request.getPostIntField("parcel_id"));
		const std::string new_owner_ref = stripHeadAndTailWhitespace(request.getPostField("new_owner_ref").str());
		const std::string redirect_path = stripHeadAndTailWhitespace(request.getPostField("redirect_path").str());

		bool redirect_to_login = false;

		{ // Lock scope
			WorldStateLock lock(world_state.mutex);

			const User* logged_in_user = LoginHandlers::getLoggedInUser(world_state, request);
			if(!logged_in_user)
			{
				redirect_to_login = true;
			}
			else
			{
				auto world_res = world_state.world_states.find(world_name);
				if(world_res == world_state.world_states.end())
					throw glare::Exception("Couldn't find world");

				ServerWorldState* world = world_res->second.ptr();
				if(!isGodUser(logged_in_user->id))
				{
					world_state.setUserWebMessage(logged_in_user->id, "Only superadmin can set parcel owner in worlds.");
				}
				else
				{
					auto parcel_res = world->getParcels(lock).find(parcel_id);
					if(parcel_res == world->getParcels(lock).end())
					{
						world_state.setUserWebMessage(logged_in_user->id, "Parcel " + parcel_id.toString() + " not found in world '" + world_name + "'.");
					}
					else
					{
						std::string resolve_error;
						UserID new_owner_id;
						if(!getUserIDByRef(world_state, new_owner_ref, new_owner_id, resolve_error))
						{
							world_state.setUserWebMessage(logged_in_user->id, resolve_error);
						}
						else
						{
							ParcelRef parcel = parcel_res->second;
							parcel->owner_id = new_owner_id;
							if(!ContainerUtils::contains(parcel->admin_ids, new_owner_id))
								parcel->admin_ids.push_back(new_owner_id);
							if(!ContainerUtils::contains(parcel->writer_ids, new_owner_id))
								parcel->writer_ids.push_back(new_owner_id);
							world->addParcelAsDBDirty(parcel, lock);
							world_state.denormaliseData();
							world_state.markAsChanged();

							std::string owner_name = "[unknown]";
							const auto user_res = world_state.user_id_to_users.find(new_owner_id);
							if(user_res != world_state.user_id_to_users.end())
								owner_name = user_res->second->name;

							world_state.setUserWebMessage(logged_in_user->id, "Set parcel " + parcel_id.toString() + " owner to '" + owner_name + "' in world '" + world_name + "'.");
							world_state.addAdminAuditLogEntry(
								logged_in_user->id,
								logged_in_user->name,
								"Set world parcel owner: world='" + world_name + "', parcel=" + parcel_id.toString() + ", owner='" + owner_name + "'.");
						}
					}
				}
			}
		} // End lock scope

		if(redirect_to_login)
			web::ResponseUtils::writeRedirectTo(reply_info, "/login");
		else if(!redirect_path.empty() && hasPrefix(redirect_path, "/") && !hasPrefix(redirect_path, "//"))
			web::ResponseUtils::writeRedirectTo(reply_info, redirect_path);
		else
			web::ResponseUtils::writeRedirectTo(reply_info, "/world/" + URLEscapeWorldName(world_name));
	}
	catch(glare::Exception& e)
	{
		if(!request.fuzzing)
			conPrint("handleSetWorldParcelOwnerPost error: " + e.what());
		web::ResponseUtils::writeHTTPOKHeaderAndData(reply_info, "Error: " + e.what());
	}
}


void handleSetWorldParcelWritersPost(ServerAllWorldsState& world_state, const web::RequestInfo& request, web::ReplyInfo& reply_info)
{
	try
	{
		if(world_state.isInReadOnlyMode())
			throw glare::Exception("Server is in read-only mode, editing disabled currently.");

		const std::string world_name = request.getPostField("world_name").str();
		const ParcelID parcel_id = ParcelID(request.getPostIntField("parcel_id"));
		const std::string writer_refs_csv = request.getPostField("writer_refs").str();
		const std::string redirect_path = stripHeadAndTailWhitespace(request.getPostField("redirect_path").str());

		bool redirect_to_login = false;

		{ // Lock scope
			WorldStateLock lock(world_state.mutex);

			const User* logged_in_user = LoginHandlers::getLoggedInUser(world_state, request);
			if(!logged_in_user)
			{
				redirect_to_login = true;
			}
			else
			{
				auto world_res = world_state.world_states.find(world_name);
				if(world_res == world_state.world_states.end())
					throw glare::Exception("Couldn't find world");

				ServerWorldState* world = world_res->second.ptr();
				auto parcel_res = world->getParcels(lock).find(parcel_id);
				if(parcel_res == world->getParcels(lock).end())
				{
					world_state.setUserWebMessage(logged_in_user->id, "Parcel " + parcel_id.toString() + " not found in world '" + world_name + "'.");
				}
				else
				{
					ParcelRef parcel = parcel_res->second;
					const bool is_superadmin = isGodUser(logged_in_user->id);
					const bool is_parcel_owner = (parcel->owner_id == logged_in_user->id);
					if(!is_superadmin && !is_parcel_owner)
					{
						world_state.setUserWebMessage(logged_in_user->id, "Only superadmin or parcel owner can set editors for this parcel.");
					}
					else
					{
						std::vector<UserID> writer_ids;
						std::string parse_error;
						if(!parseCommaSeparatedUserRefsToIDs(world_state, writer_refs_csv, writer_ids, parse_error))
						{
							world_state.setUserWebMessage(logged_in_user->id, parse_error);
						}
						else
						{
							if(!ContainerUtils::contains(writer_ids, parcel->owner_id))
								writer_ids.push_back(parcel->owner_id);

							parcel->admin_ids = writer_ids;
							parcel->writer_ids = writer_ids;
							world->addParcelAsDBDirty(parcel, lock);
							world_state.denormaliseData();
							world_state.markAsChanged();

							world_state.setUserWebMessage(logged_in_user->id,
								"Updated editors for parcel " + parcel_id.toString() + " in world '" + world_name + "'.");
							if(is_superadmin)
							{
								world_state.addAdminAuditLogEntry(
									logged_in_user->id,
									logged_in_user->name,
									"Set world parcel editors: world='" + world_name + "', parcel=" + parcel_id.toString() + ", editors_count=" + toString(writer_ids.size()) + ".");
							}
						}
					}
				}
			}
		} // End lock scope

		if(redirect_to_login)
			web::ResponseUtils::writeRedirectTo(reply_info, "/login");
		else if(!redirect_path.empty() && hasPrefix(redirect_path, "/") && !hasPrefix(redirect_path, "//"))
			web::ResponseUtils::writeRedirectTo(reply_info, redirect_path);
		else
			web::ResponseUtils::writeRedirectTo(reply_info, "/world/" + URLEscapeWorldName(world_name));
	}
	catch(glare::Exception& e)
	{
		if(!request.fuzzing)
			conPrint("handleSetWorldParcelWritersPost error: " + e.what());
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


#if BUILD_TESTS


void testParseAndUnescapeWorldName(const std::string& path, const std::string& expected_world_name)
{
	Parser parser(path);
	const std::string world_name = parseAndUnescapeWorldName(parser);
	testEqual(world_name, expected_world_name);
}


void test()
{
	//----------------------- Test URLEscapeWorldName -------------------------
	testAssert(URLEscapeWorldName("") == "");
	testAssert(URLEscapeWorldName("abc") == "abc");
	testAssert(URLEscapeWorldName("a?b") == "a%3Fb");
	testAssert(URLEscapeWorldName("a b") == "a+b"); // space is encoded with '+'
	testAssert(URLEscapeWorldName("a?b/cd") == "a%3Fb/cd");
	testAssert(URLEscapeWorldName("a?b/c?d") == "a%3Fb/c%3Fd"); // first slash is not escaped
	testAssert(URLEscapeWorldName("a?b/cd/zz") == "a%3Fb/cd%2Fzz"); // Subsequent slashes are escaped
	testAssert(URLEscapeWorldName("a?b/") == "a%3Fb/"); // check with no string after slash

	//------------------------ Test parseAndUnescapeWorldName ----------------------------
	testParseAndUnescapeWorldName("", "");
	testParseAndUnescapeWorldName("abc", "abc");
	testParseAndUnescapeWorldName("ab/cd", "ab/cd");
	testParseAndUnescapeWorldName("ab/cd/ef", "ab/cd"); // second slash stops the parsing
	testParseAndUnescapeWorldName("a+b", "a b");
	testParseAndUnescapeWorldName("a%3Fb", "a?b");
	testParseAndUnescapeWorldName("a%3Fb/c%3Fd", "a?b/c?d");

	try
	{
		testParseAndUnescapeWorldName("a%3", "a?b/c?d"); // Truncated encoded char
		failTest("Excep expected");
	}
	catch(glare::Exception& ) 
	{}
}


#endif

} // end namespace WorldHandlers
